/* The port's multiplayer session (net_session.h): the game's messages over UDP, rooms announced on the
 * local network and through khdays-server, a star around the host, numbered records resent until
 * acknowledged and handed in order. A thread of its own receives and resends; the game's thread sends
 * and takes what arrived (khdays_net_next). docs/MULTIPLAYER.md. */
#include "net_session.h"
#include "../core/session_log.h"
#include "../core/sha1.h"
#include "../../host/net/net.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long long u64;

/* The multicast group and port where rooms are announced on the local network; KHDAYS_NET_PORT
 * changes the port. */
#define GROUP_IP 0xefff2352u /* 239.255.35.82 */
#define GROUP_PORT 35820
#define PORTS 16 /* the DS wireless's port numbers, 0 to 15 (a numbering, not a limit) */
#define ID_SIZE 6
#define SSID_SIZE 24

#define MAGIC 0x3144484bu /* "KHD1" */
#define HEADER 16
#define RECORD_HEADER 6 /* each record in a RECORD datagram starts with: size u16, sequence u32 */
/* The most bytes of records one datagram carries: 1200, which crosses the internet unfragmented (as
 * QUIC's, RFC 9000 section 14). A bigger record is still sent, alone. */
#define DATAGRAM_RECORDS 1200
enum { ANNOUNCE = 1, GONE, JOIN, WELCOME, REFUSE, RECORD, ACK, PING, PONG, LEAVE };
/* the kinds khdays-server uses */
enum { REGISTER = 11, REGISTERED, LIST, ROOMS, CONNECT, PEER, PUNCH, RELAY, UNREGISTER };
/* a guest's movement stick, beside its controls (khdays_net_set_stick): x i16, y i16, the run
 * threshold u8, the walk speed u8, analog movement on u8. Not acknowledged: the next one replaces it. */
enum { STICK = 20 };
#define STICK_BODY 7

/* The server players meet through online: the player's settings name it (host/app/profile.c),
 * KHDAYS_NET_SERVER overrides it (empty: no server). This is the port used when the address gives
 * none. */
#define SERVER_PORT 35821
static int (*server_source)(char *text, int size);

void khdays_net_settings_source(int (*server)(char *text, int size))
{
    server_source = server;
}
/* A LIST is padded to this size, because the server never answers with more
 * bytes than it was asked with. */
#define LIST_DATAGRAM 1200
#define CONNECT_BODY 16
/* The largest announcement a ROOMS answer can hold (the server's BODY_MOST: a LIST datagram less
 * the headers of the answer and of one room in it); the server drops a REGISTER past it. */
#define ANNOUNCE_MOST (LIST_DATAGRAM - HEADER - 6 - 14)
/* The room's details, after the game's information in an announcement: "KHR1", the host's round trip
 * to the server u16 (ms, 0xffff not measured), the players it takes u8 (the host too), flags u8 (bit
 * 0: a password), the password's salt (16 bytes), the name's size u16, the name (UTF-8). Their size
 * stays the same while the room lives. */
#define DETAILS_MAGIC 0x3152484bu
#define DETAILS_FIXED 26
#define SALT_SIZE 16
#define DIGEST_SIZE 20

/* Where a datagram goes: straight to an address, or, when `relay` is a pair's
 * token, to the server, which passes it on to the other player. */
typedef struct Route {
    KhdaysNetAddress address;
    unsigned int relay;
} Route;

/* The timings, in nanoseconds. With two copies, a 1 s round trip and 10% of datagrams lost the game
 * kept playing, so the session waits for the network as long as it answers. A member counts as gone
 * after member_silent without a word (twenty missed pings). */
#define MS 1000000ull
static const u64 ANNOUNCE_EVERY = 500 * MS;
static const u64 ROOM_SILENT = 3000 * MS;
static const u64 PING_EVERY = 250 * MS;
/* A member's Stop, known: STOP_KNOWN with STOP_BYTES bytes, the most casts of each of its three levels, then each
 * level's Magic LV. */
#define STOP_KNOWN (1ull << 48)
#define STOP_BYTES 6u
static const u64 JOIN_EVERY = 250 * MS;
static const u64 JOIN_SILENT = 5000 * MS;
static u64 member_silent = 5000 * MS;
static const u64 REPORT_EVERY = 5000 * MS;
/* The port the game's messages use, and the number of "gates" (message types: 5 bits of each
 * message's header). The session log counts messages by gate. */
enum { GAME_PORT = 12, GATES = 32 };
/* The mods' own records (runtime/scripts), on a port the game never opens (it opens 12 and ov105's
 * data port 14), handed to the scripts and never to the game: a kind byte, then
 *   MODS_SEED: the room's seed (u64), the host's to each member as it joins and whenever it changes;
 *   MODS_SHARE: a piece of a message the host's mods share, a byte 1 on its last piece, then its bytes;
 *               a message's pieces come in order, as every port's records do;
 *   MODS_MISSIONS: a piece of a copy's new missions (runtime/missions/room.h), as MODS_SHARE's pieces: the
 *               host's to each member as it joins, a guest's to the host as it is welcomed. */
enum { MODS_PORT = 15, MODS_SEED = 1, MODS_SHARE, MODS_MISSIONS };
#define MODS_PIECE 1024 /* a share's bytes in one record: a datagram's worth */
/* Online: a host registers its room every 2 s (the server forgets it after
 * 10 s of silence), and a guest asks for the rooms every second while the
 * game is looking. To join, a guest first tries to reach the host directly
 * ("hole punching" through both routers), and goes through the server's
 * relay if the host has not answered within 1.5 s. */
static const u64 REGISTER_EVERY = 2000 * MS;
static const u64 LIST_EVERY = 1000 * MS;
static const u64 PUNCH_EVERY = 100 * MS;
static const u64 PUNCH_FOR = 3000 * MS;
static const u64 RELAY_AFTER = 1500 * MS;
/* While looking for rooms, each room of the local network is pinged this often (its host
 * answers anyone's ping, at most PROBES_ANSWERED a second: a reply no bigger than the ping, and
 * no more of them than that, whoever asks). */
static const u64 PROBE_EVERY = 1000 * MS;
#define PROBES_ANSWERED 64

enum { NONE, HOST, GUEST };
enum { PENDING, MEMBER };

/* One of the game's messages, numbered within its port's stream. */
typedef struct Record {
    unsigned int seq;
    unsigned int size;
    u64 sent;
    int resent; /* sent more than once (so its round trip cannot be measured) */
    unsigned char *data;
} Record;

typedef struct Records {
    Record *items;
    int count, room;
} Records;

/* The records of one port, to and from one peer. */
typedef struct Stream {
    unsigned int next_send;     /* the next sequence number to send */
    Records unacked;            /* sent, not yet acknowledged */
    unsigned int next_expected; /* the next sequence number to hand to the game */
    Records early;              /* received ahead of their turn */
    int ack_due;                /* an acknowledgement is owed */
} Stream;

/* Another copy of the game in the session. On the host, one per guest; on a
 * guest, one: the host. */
typedef struct Peer {
    int used;
    int state;        /* PENDING (joining) or MEMBER */
    unsigned int aid; /* its player number (0 is the host) */
    unsigned char id[ID_SIZE];
    Route address;
    u64 heard, pinged;    /* when it was last heard from, and last pinged */
    double rtt_ms;        /* round trip; < 0 until measured */
    double loss;          /* share of pings unanswered in the last report period */
    int level;            /* host: this guest's level as its pings give it (0 not known) */
    u64 stop;             /* host: this guest's Stop as its pings give it (STOP_KNOWN; 0 not known) */
    KhdaysNetStick stick; /* host: this guest's movement stick as it last sent it, */
    u64 stick_at;         /* and when (0 never) */
    unsigned int pings, pongs;
    /* For the session log's report (every REPORT_EVERY): records resent, and
     * the most records that waited for the game, and for an acknowledgement. */
    unsigned int resent, waiting_most, unacked_most;
    /* Host only: this member's messages handed to the game, counted by gate. */
    unsigned int census_got[GATES];

    /* Host only: the controls (the guest's buttons, one packet per battle tick) are handed to the game
     * at most one per tick. control_tick is the tick (battle_ticks + 1) the last one was handed for. */
    u64 control_tick;
    unsigned int controls_held, controls_dropped;

    /* Host only: the playout store, which hands a guest's controls to the game one per tick however
     * the network delivers them (docs/MULTIPLAYER.md): its depth, whether it is filling, whether a
     * battle's controls started, the trim and calm counters, the second's window and least depth,
     * and the report's counts. */
    unsigned int play_depth;
    int play_filling, play_started, play_trim;
    unsigned int play_calm, play_window, play_least;
    unsigned int play_dry, play_trimmed;
    /* the new missions this copy has (MODS_MISSIONS): a host's guest's, a guest's host's; known once its last
     * piece came (an older port never sends them), and the pieces being put together */
    unsigned char *missions;
    unsigned int missions_size;
    int missions_known;
    unsigned char *missions_part;
    int missions_part_size, missions_part_room;
    /* Host only: when the last control arrived (in battle ticks), and the
     * longest gap between two controls in this ten-second window and the one
     * before. The store never grows deeper than that gap: deep enough to ride
     * out the gap, and no deeper (a deeper store only delays the guest). */
    u64 control_came;
    int control_came_seen;
    unsigned int gap_most, gap_most_before, gap_window;

    u64 reported; /* when the last report line was written */
    Stream streams[PORTS];
    /* Hand-over to the game: one delivery per port and frame. */
    unsigned int delivered_frame[PORTS];
    int delivered[PORTS];
    unsigned int blocked[PORTS]; /* the khdays_net_next call that found this port's next record must wait */
} Peer;

/* A room a guest has seen (on the local network or from the server). */
typedef struct Room {
    unsigned char id[ID_SIZE];
    KhdaysNetAddress address;
    unsigned int ggid, build;
    int open;
    int local; /* seen on the local network: join it there, whatever the server says */
    u64 seen;
    unsigned int info_size;
    unsigned char *info;
    /* the room's details (none from a build without them): its name (UTF-8, not ended), the
     * players in it and the most it takes, its password's salt, the host's round trip to the
     * server; and, on the local network, the round trip to the host measured here */
    int details, locked, players, most;
    char *name;
    unsigned int name_size;
    unsigned char salt[SALT_SIZE];
    int host_server_ms;
    double probe_ms;
    u64 probed;
} Room;

/* Something waiting for the game to take it (khdays_net_next). */
typedef struct Queued {
    KhdaysNetEvent event;
    unsigned char *data;
    int held;    /* a control already counted as held back (it can wait through many calls) */
    u64 arrived; /* the battle tick it arrived at (net.battle_ticks) */
} Queued;

/* A datagram held back by the delay knobs (KHDAYS_NET_DELAY_MS, ..._JITTER_MS). */
typedef struct Outgoing {
    u64 due;
    KhdaysNetAddress to;
    unsigned int size;
    unsigned char *bytes;
} Outgoing;

static struct {
    int started;
    SDL_Mutex *lock;
    SDL_Thread *thread;
    volatile int running;
    KhdaysUdp *session, *lobby;
    KhdaysNetAddress group;
    unsigned char id[ID_SIZE];
    unsigned int build;
    int role;
    /* host */
    unsigned int ggid, max_members;
    int open;
    unsigned char *info;
    unsigned int info_size;
    u64 announced;
    /* both: on the host peers[aid] are the guests, on a guest peers[0] is the host */
    Peer *peers;
    int peer_room;
    /* guest */
    int scanning;
    unsigned int scan_ggid;
    Room *rooms;
    int room_count, room_room;
    int joining;
    unsigned char join_id[ID_SIZE];
    Route join_address;
    /* a join through the server: whether it is one, whether it is relayed
     * now, the pair's token from PEER (0: none yet), when PEER came and when
     * CONNECT was sent */
    int join_online, join_relayed;
    unsigned int join_token;
    u64 join_met, connect_sent;
    unsigned char ssid[SSID_SIZE];
    unsigned int ssid_size;
    u64 join_started, join_sent;
    unsigned int own_aid;
    /* the players' levels (the game's own, 0 not known): this copy's, set by the game's thread; on a
     * guest, the members' as the host's pings list them, by aid */
    volatile int own_level;
    int *levels;
    int level_room;
    /* each player's Stop (STOP_KNOWN with its casts and Magic LVs, from its deck; 0 not known): this copy's, set by
     * the game's thread; on a guest, the members' as the host's pings list them, by aid */
    volatile u64 own_stop;
    u64 *stops;
    int stop_room;
    /* the mods' records (MODS_PORT): the room's seed (the host's as its scripts set it; a guest's as
     * the host sent it) and whether there is one; on a guest, a share being put together and the
     * shares put together, for the scripts */
    unsigned long long mods_seed;
    int mods_seed_known;
    unsigned char *share_part;
    int share_size, share_room;
    struct Shared {
        unsigned char *data;
        unsigned int size;
    } *shares;
    int share_count, shares_room;
    /* counted up each time a copy's new missions come, or a copy joins or leaves (khdays_net_missions_version) */
    unsigned int missions_version;
    /* guest: this copy's movement stick (the game's thread sets it), the one sent last and when */
    KhdaysNetStick own_stick, sent_stick;
    u64 stick_sent;
    /* to the game */
    Queued *events;
    int event_count, event_room;
    unsigned int call;
    /* the testing knobs: delay, loss and jitter added to every datagram sent, and the datagrams held back */
    double delay_ms, loss;
    double jitter_ms; /* KHDAYS_NET_JITTER_MS: up to this much extra delay, at random, per datagram */
    Outgoing *out;
    int out_count, out_room;
    unsigned int random;
    /* per port: how many bytes of waiting records may be joined into one
     * delivery to the game (0: never joined; khdays_net_join_records) */
    unsigned int join_up_to[PORTS];
    /* scratch: the records being packed into one datagram */
    Record **list;
    int list_room;
    /* online: whether a server is used and which, whether the local network is used too
     * (KHDAYS_NET_LAN=0: no), and whether a join first tries to reach the host directly
     * (KHDAYS_NET_PUNCH=0: never, to test the relay) */
    int online, lan, punch;
    KhdaysNetAddress server;
    u64 registered, listed;
    int registered_told;
    /* the round trip to the server: a host's from its REGISTER to the REGISTERED, a guest's
     * from its LIST to the first ROOMS (ms, smoothed; -1 not measured) */
    double server_ms;
    u64 register_sent, list_sent;
    int register_waiting, list_waiting;
    /* host: the room's name (UTF-8), its password (its digest) and salt */
    char *room_name;
    unsigned int room_name_size;
    int locked;
    unsigned char pass_digest[DIGEST_SIZE], salt[SALT_SIZE];
    /* guest: the password the next join proves (its digest), and the room's salt */
    int join_password, join_locked;
    unsigned char join_digest[DIGEST_SIZE], join_salt[SALT_SIZE];
    /* guest: the one room the game is told of (the port's list chooses it), or every room */
    int only;
    unsigned char only_id[ID_SIZE];
    /* host: the pings of non-members answered in this second */
    u64 probes_since;
    unsigned int probes_answered;
    /* guest: why the last join was refused (0: it was not), and with KHDAYS_NET_REFUSE_MODS the host's
     * mods as its answer listed them */
    unsigned int last_refusal;
    char *refusal_mods;
    int refusal_more;
    int refusal_stop_off; /* that host plays without the Stop magic */
    /* host only: guests the server introduced, to punch toward until they join */
    struct Introduced {
        unsigned int token;
        KhdaysNetAddress address;
        u64 until, punched;
    } *introduced;
    int introduced_count, introduced_room;
    /* datagrams and bytes sent since the last report, to anyone (not counting
     * the 28 bytes of IP and UDP headers each) */
    u64 sent_datagrams, sent_bytes;
    /* The battle's tick counter: a tick is counted when the game sends a message that comes once a tick
     * -- on the host the characters' states (gate 0), on a guest its controls (gate 3). Not every send:
     * with enemies around the host sends twice a tick, and counting every send made the playout store
     * grow without end. one_control is the one-control-per-tick rule (KHDAYS_NET_ONE_CONTROL=0 turns it
     * off). */
    u64 battle_ticks, battle_ticks_seen;
    unsigned int battle_tick_frame, frame_now;
    int one_control;
    /* guest only: the last control packet its game sent, for the prediction of its own character
     * (mp_predict.c) */
    unsigned char own_control[32];
    unsigned int own_control_size;
    /* guest only: the filter set by khdays_net_filter */
    unsigned int (*filter)(unsigned char *data, unsigned int size);
} net;

static u64 now_ns(void)
{
    return SDL_GetTicksNS();
}

/* The time from `then` to `now`; 0 if `then` is later (it can be, when it was
 * taken after the caller read `now`). */
static u64 since(u64 now, u64 then)
{
    return now > then ? now - then : 0;
}

static void ping_levels(Peer *p, const unsigned char *body, unsigned int size);
static void send_stick(void);
static void send_seed(Peer *p);
static void send_missions(Peer *p);
static void mods_forget(void);

/* Makes room for `need` items, doubling, the new part zeroed. Out of memory,
 * the process stops. */
static void *grow(void *items, int *room, int need, size_t item)
{
    if (need <= *room) {
        return items;
    }
    {
        int bigger = *room > 0 ? *room * 2 : 8;
        void *more;
        while (bigger < need) {
            bigger *= 2;
        }
        more = realloc(items, (size_t)bigger * item);
        if (more == NULL) {
            fprintf(stderr, "net: out of memory\n");
            abort();
        }
        memset((char *)more + (size_t)*room * item, 0, (size_t)(bigger - *room) * item);
        *room = bigger;
        return more;
    }
}

/* The members' Stop forgotten: a room joined or left (the host's pings bring them again). */
static void forget_stops(void)
{
    for (int i = 0; i < net.stop_room; ++i) {
        net.stops[i] = 0;
    }
}

/* A copy of `size` bytes (at least one byte is allocated, so an empty copy is
 * not NULL). Out of memory, the process stops. */
static unsigned char *copy_of(const void *data, size_t size)
{
    unsigned char *copy = (unsigned char *)malloc(size > 0 ? size : 1);
    if (copy == NULL) {
        fprintf(stderr, "net: out of memory\n");
        abort();
    }
    if (size > 0) {
        memcpy(copy, data, size);
    }
    return copy;
}

/* A simple pseudo-random number, for the testing knobs only. */
static unsigned int next_random(void)
{
    net.random = net.random * 1664525u + 1013904223u;
    return net.random;
}

/* --- datagrams -------------------------------------------------------- */

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, unsigned int v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

static unsigned int get16(const unsigned char *p)
{
    return (unsigned int)p[0] | (unsigned int)p[1] << 8;
}

static unsigned int get32(const unsigned char *p)
{
    return get16(p) | get16(p + 2) << 16;
}

static void put64(unsigned char *p, u64 v)
{
    put32(p, (unsigned int)v);
    put32(p + 4, (unsigned int)(v >> 32));
}

static u64 get64(const unsigned char *p)
{
    return (u64)get32(p) | (u64)get32(p + 4) << 32;
}

/* Sends a datagram, applying the testing knobs: it may be dropped (loss) or
 * held back (delay, jitter). */
static void transmit(const KhdaysNetAddress *to, const unsigned char *bytes, unsigned int size)
{
    ++net.sent_datagrams; /* counted even if the loss knob drops it */
    net.sent_bytes += size;
    if (net.loss > 0.0 && (next_random() >> 8) < (unsigned int)(net.loss * 16777216.0)) {
        return;
    }
    if (net.delay_ms > 0.0 || net.jitter_ms > 0.0) {
        Outgoing *o;
        const double extra = net.jitter_ms > 0.0 ? net.jitter_ms * (double)(next_random() >> 8) / 16777216.0 : 0.0;
        net.out = (Outgoing *)grow(net.out, &net.out_room, net.out_count + 1, sizeof(*net.out));
        o = &net.out[net.out_count++];
        o->due = now_ns() + (u64)((net.delay_ms + extra) * 1e6);
        o->to = *to;
        o->size = size;
        o->bytes = copy_of(bytes, size);
        return;
    }
    khdays_host_udp_send(net.session, to, bytes, size);
}

/* Sends a datagram of `kind` with a `size`-byte body along route `to`:
 * straight to its address, or, for a relayed pair, wrapped in a RELAY
 * datagram to the server (the pair's token, then the whole datagram). */
/* This copy's gameplay mods (khdays_net_set_mods), under their own lock: the scripts set them on the
 * game's thread, the session's thread reads them. */
static SDL_Mutex *mods_lock;
static SDL_InitState mods_once;
static unsigned int mods_fingerprint, mods_count, mods_size;
static unsigned char *mods_list;
/* this copy's new missions for the rooms (runtime/missions/room.h), set as the game starts */
static unsigned char *own_missions;
static unsigned int own_missions_size;

static void mods_ready(void)
{
    if (SDL_ShouldInit(&mods_once)) {
        mods_lock = SDL_CreateMutex();
        SDL_SetInitialized(&mods_once, mods_lock != NULL);
    }
}

void khdays_net_set_mods(unsigned int fingerprint, const unsigned char *list, unsigned int size, unsigned int count)
{
    unsigned char *copy = size > 0 ? (unsigned char *)malloc(size) : NULL;
    if (copy != NULL) {
        memcpy(copy, list, size);
    }
    mods_ready();
    SDL_LockMutex(mods_lock);
    free(mods_list);
    mods_list = copy;
    mods_size = copy != NULL ? size : 0;
    mods_count = copy != NULL ? count : 0;
    mods_fingerprint = fingerprint;
    SDL_UnlockMutex(mods_lock);
}

/* The Stop magic ([gameplay] stop_magic), set before any session: off, it counts in the fingerprint as
 * a gameplay mod would, so that a room takes only copies that agree. */
#define STOP_OFF_NAME "\x01stop"
#define STOP_OFF_MARK 0x504f5453u
static int stop_magic = 1;

void khdays_net_set_stop_magic(int on)
{
    stop_magic = on != 0;
}

static unsigned int own_mods_fingerprint(void)
{
    unsigned int fingerprint;
    mods_ready();
    SDL_LockMutex(mods_lock);
    fingerprint = mods_fingerprint;
    SDL_UnlockMutex(mods_lock);
    return stop_magic ? fingerprint : fingerprint ^ STOP_OFF_MARK;
}

static void send_kind(const Route *to, int kind, const unsigned char *body, unsigned int size)
{
    const unsigned int wrap = to->relay != 0 ? HEADER + 4 : 0;
    unsigned char *bytes = (unsigned char *)malloc(wrap + HEADER + (size_t)size);
    unsigned char *d = bytes + wrap;
    if (bytes == NULL) {
        return;
    }
    put32(d, MAGIC);
    d[4] = (unsigned char)kind;
    d[5] = 1; /* version */
    put16(d + 6, size);
    memcpy(d + 8, net.id, ID_SIZE);
    put16(d + 14, 0);
    if (size > 0) {
        memcpy(d + HEADER, body, size);
    }
    if (wrap == 0) {
        transmit(&to->address, bytes, HEADER + size);
    } else {
        put32(bytes, MAGIC);
        bytes[4] = RELAY;
        bytes[5] = 1;
        put16(bytes + 6, 4 + HEADER + size);
        memcpy(bytes + 8, net.id, ID_SIZE);
        put16(bytes + 14, 0);
        put32(bytes + HEADER, to->relay);
        transmit(&net.server, bytes, wrap + HEADER + size);
    }
    free(bytes);
}

/* A route straight to `address`. */
static Route direct(const KhdaysNetAddress *address)
{
    Route r;
    r.address = *address;
    r.relay = 0;
    return r;
}

/* Sends a datagram to the server itself (not relayed). */
static void to_server(int kind, const unsigned char *body, unsigned int size)
{
    const Route r = direct(&net.server);
    send_kind(&r, kind, body, size);
}

/* --- events for the game --------------------------------------------- */

/* Queues an event for the game to take (khdays_net_next). */
static Queued *push(unsigned int kind, unsigned int aid, const void *data, unsigned int size)
{
    Queued *q;
    net.events = (Queued *)grow(net.events, &net.event_room, net.event_count + 1, sizeof(*net.events));
    q = &net.events[net.event_count++];
    memset(q, 0, sizeof(*q));
    q->arrived = net.battle_ticks;
    q->event.kind = kind;
    q->event.aid = aid;
    q->event.size = size;
    if (size > 0) {
        q->data = copy_of(data, size);
    }
    return q;
}

static void drop_events(void)
{
    for (int i = 0; i < net.event_count; ++i) {
#pragma warning(suppress : 6001) /* grow() zeroes what it adds: each item's data is NULL or its own copy */
        free(net.events[i].data);
    }
    net.event_count = 0;
}

/* --- peers ------------------------------------------------------------ */

static void free_records(Records *r)
{
    for (int i = 0; i < r->count; ++i) {
#pragma warning(suppress : 6001) /* grow() zeroes what it adds: each item's data is NULL or its own copy */
        free(r->items[i].data);
    }
    free(r->items);
    memset(r, 0, sizeof(*r));
}

static void free_peer(Peer *p)
{
    for (int i = 0; i < PORTS; ++i) {
        free_records(&p->streams[i].unacked);
        free_records(&p->streams[i].early);
    }
    free(p->missions);
    free(p->missions_part);
    memset(p, 0, sizeof(*p));
    ++net.missions_version;
}

/* The peer at `index`, making room for it if needed. */
static Peer *peer_slot(unsigned int index)
{
    net.peers = (Peer *)grow(net.peers, &net.peer_room, (int)index + 1, sizeof(*net.peers));
    return &net.peers[index];
}

static Peer *peer_by_id(const unsigned char *id)
{
    for (int i = 0; i < net.peer_room; ++i) {
        if (net.peers[i].used && memcmp(net.peers[i].id, id, ID_SIZE) == 0) {
            return &net.peers[i];
        }
    }
    return NULL;
}

static Peer *peer_by_aid(unsigned int aid)
{
    for (int i = 0; i < net.peer_room; ++i) {
        if (net.peers[i].used && net.peers[i].aid == aid) {
            return &net.peers[i];
        }
    }
    return NULL;
}

/* How many peers there are (joining or joined). */
static int members(void)
{
    int count = 0;
    for (int i = 0; i < net.peer_room; ++i) {
        count += net.peers[i].used;
    }
    return count;
}

static void new_peer(Peer *p, unsigned int aid, const unsigned char *id, const Route *address)
{
    free_peer(p);
    p->used = 1;
    p->aid = aid;
    memcpy(p->id, id, ID_SIZE);
    p->address = *address;
    p->heard = now_ns();
    p->rtt_ms = -1.0;
    p->reported = p->heard;
}

static void drop_peers(void)
{
    for (int i = 0; i < net.peer_room; ++i) {
        free_peer(&net.peers[i]);
    }
}

/* --- records ---------------------------------------------------------- */

/* Sends `count` records of `port` to `p` in one datagram, in the order given. */
static void send_records(Peer *p, unsigned int port, Record *const *list, int count)
{
    size_t size = 2; /* size_t, so the sum cannot overflow */
    unsigned char *body, *at;
    for (int i = 0; i < count; ++i) {
        size += RECORD_HEADER + (size_t)list[i]->size;
    }
    if (size > 0xffff) {
        return; /* too big for one datagram (send_list never asks for this) */
    }
    body = (unsigned char *)malloc(size);
    if (body == NULL) {
        return;
    }
    body[0] = (unsigned char)port;
    body[1] = (unsigned char)count;
    at = body + 2;
    for (int i = 0; i < count; ++i) {
        put16(at, list[i]->size);
        put32(at + 2, list[i]->seq);
        memcpy(at + RECORD_HEADER, list[i]->data, list[i]->size);
        at += RECORD_HEADER + list[i]->size;
    }
    send_kind(&p->address, RECORD, body, (unsigned int)size);
    free(body);
}

/* Sends the records in `list` to `p`, in order, packing as many into each
 * datagram as DATAGRAM_RECORDS allows (a bigger record goes alone). */
static void send_list(Peer *p, unsigned int port, Record *const *list, int count)
{
    int first = 0;
    while (first < count) {
        unsigned int bytes = RECORD_HEADER + list[first]->size;
        int last = first + 1;
        while (last < count && last - first < 255 && bytes + RECORD_HEADER + list[last]->size <= DATAGRAM_RECORDS) {
            bytes += RECORD_HEADER + list[last]->size;
            ++last;
        }
        send_records(p, port, list + first, last - first);
        first = last;
    }
}

/* Adds a record for `p` on `port` and sends it right away. */
static void queue_record(Peer *p, unsigned int port, const void *data, unsigned int size)
{
    Stream *s = &p->streams[port];
    Record *r;
    int first;
    unsigned int bytes;
    s->unacked.items = (Record *)grow(s->unacked.items, &s->unacked.room, s->unacked.count + 1, sizeof(Record));
    r = &s->unacked.items[s->unacked.count++];
    r->seq = s->next_send++;
    r->size = size;
    r->data = copy_of(data, size);
    r->sent = now_ns();
    r->resent = 0;
    /* The datagram also carries the latest records not yet acknowledged, as
     * many as fit. If a datagram is lost, the next one (a frame later) brings
     * its records again, instead of waiting a round trip for a resend. */
    first = s->unacked.count - 1;
    bytes = RECORD_HEADER + size;
    while (first > 0 && s->unacked.count - first < 255 &&
           bytes + RECORD_HEADER + s->unacked.items[first - 1].size <= DATAGRAM_RECORDS) {
        --first;
        bytes += RECORD_HEADER + s->unacked.items[first].size;
    }
    net.list = (Record **)grow(net.list, &net.list_room, s->unacked.count - first, sizeof(*net.list));
    for (int k = first; k < s->unacked.count; ++k) {
        net.list[k - first] = &s->unacked.items[k];
    }
    send_records(p, port, net.list, s->unacked.count - first);
}

/* Acknowledges what arrived on `port`: the next sequence number expected,
 * and a bit for each of the 32 records after it that arrived early. */
static void send_ack(Peer *p, unsigned int port)
{
    Stream *s = &p->streams[port];
    unsigned char body[12];
    unsigned int mask = 0;
    for (int i = 0; i < s->early.count; ++i) {
        const unsigned int ahead = s->early.items[i].seq - s->next_expected - 1;
        if (ahead < 32) {
            mask |= 1u << ahead;
        }
    }
    body[0] = (unsigned char)port;
    body[1] = body[2] = body[3] = 0;
    put32(body + 4, s->next_expected);
    put32(body + 8, mask);
    send_kind(&p->address, ACK, body, sizeof(body));
    s->ack_due = 0;
}

static void control_came(Peer *p, unsigned int port, const unsigned char *data, unsigned int size);

/* A piece of copy `p`'s new missions (MODS_MISSIONS): added, and the list kept once its last piece came. */
static void missions_record(Peer *p, const unsigned char *data, unsigned int size)
{
    const int bytes = (int)size - 2;
    p->missions_part =
        (unsigned char *)grow(p->missions_part, &p->missions_part_room, p->missions_part_size + bytes, 1);
    memcpy(p->missions_part + p->missions_part_size, data + 2, (size_t)bytes);
    p->missions_part_size += bytes;
    if (data[1] == 1) {
        free(p->missions);
        p->missions = p->missions_part_size > 0 ? copy_of(p->missions_part, (unsigned int)p->missions_part_size) : NULL;
        p->missions_size = p->missions != NULL ? (unsigned int)p->missions_part_size : 0;
        p->missions_known = 1;
        p->missions_part_size = 0;
        ++net.missions_version;
        khdays_log("net: %s %u's new missions came (%u bytes)", net.role == HOST ? "member" : "host", p->aid,
                   p->missions_size);
    }
}

/* A mods' record (MODS_PORT): from the host, the room's seed kept or a share's piece added; from any copy, a piece
 * of its new missions. */
static void mods_record(Peer *p, const unsigned char *data, unsigned int size)
{
    if (size >= 2 && data[0] == MODS_MISSIONS) {
        missions_record(p, data, size);
        return;
    }
    if (net.role != GUEST || size < 1) {
        return;
    }
    if (data[0] == MODS_SEED && size >= 9) {
        net.mods_seed = get64(data + 1);
        net.mods_seed_known = 1;
    } else if (data[0] == MODS_SHARE && size >= 2) {
        const int bytes = (int)size - 2;
        net.share_part = (unsigned char *)grow(net.share_part, &net.share_room, net.share_size + bytes, 1);
        memcpy(net.share_part + net.share_size, data + 2, (size_t)bytes);
        net.share_size += bytes;
        if (data[1] == 1) {
            net.shares = (struct Shared *)grow(net.shares, &net.shares_room, net.share_count + 1, sizeof(*net.shares));
            net.shares[net.share_count].data = copy_of(net.share_part, (unsigned int)net.share_size);
            net.shares[net.share_count++].size = (unsigned int)net.share_size;
            net.share_size = 0;
        }
    }
}

/* A record in its order: the mods' to the scripts, the others to the game. */
static void record_in_order(Peer *p, unsigned int port, const unsigned char *data, unsigned int size)
{
    if (port == MODS_PORT) {
        mods_record(p, data, size);
        return;
    }
    push(KHDAYS_NET_RECORD, p->aid, data, size)->event.port = port;
    control_came(p, port, data, size);
}

/* A record arrived. If it is the next in order it goes to the game, followed
 * by any early ones that now follow on. An early record waits; one already
 * received is ignored. */
static void received_record(Peer *p, unsigned int port, unsigned int seq, const unsigned char *data, unsigned int size)
{
    Stream *s = &p->streams[port];
    s->ack_due = 1;
    if ((int)(seq - s->next_expected) < 0) {
        return; /* already received */
    }
    if (seq != s->next_expected) {
        Record *r;
        for (int i = 0; i < s->early.count; ++i) {
            if (s->early.items[i].seq == seq) {
                return;
            }
        }
        s->early.items = (Record *)grow(s->early.items, &s->early.room, s->early.count + 1, sizeof(Record));
        r = &s->early.items[s->early.count++];
        r->seq = seq;
        r->size = size;
        r->data = copy_of(data, size);
        return;
    }
    record_in_order(p, port, data, size);
    ++s->next_expected;
    for (;;) {
        int found = -1;
        for (int i = 0; i < s->early.count; ++i) {
            if (s->early.items[i].seq == s->next_expected) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            break;
        }
        record_in_order(p, port, s->early.items[found].data, s->early.items[found].size);
        free(s->early.items[found].data);
        s->early.items[found] = s->early.items[--s->early.count];
        ++s->next_expected;
    }
}

/* The peer acknowledged every record before `next`, and the early ones in
 * `mask`: they need no resending. The first one also measures the round trip
 * (smoothed over time). */
static void acknowledged(Peer *p, unsigned int port, unsigned int next, unsigned int mask, u64 now)
{
    Records *u = &p->streams[port].unacked;
    for (int i = 0; i < u->count;) {
        const unsigned int seq = u->items[i].seq;
        const unsigned int ahead = seq - next - 1;
        if ((int)(seq - next) < 0 || (ahead < 32 && (mask & (1u << ahead)))) {
            /* Karn's rule: a resent record says nothing about the round trip,
             * since we cannot tell which of its sendings was acknowledged. */
            if ((int)(seq - next) == -1 && u->items[i].sent != 0 && !u->items[i].resent) {
                const double sample = (double)since(now, u->items[i].sent) / 1e6;
                p->rtt_ms = p->rtt_ms < 0 ? sample : p->rtt_ms * 0.875 + sample * 0.125;
            }
            free(u->items[i].data);
            memmove(&u->items[i], &u->items[i + 1], (size_t)(u->count - i - 1) * sizeof(Record));
            --u->count;
        } else {
            ++i;
        }
    }
}

/* How long to wait for an acknowledgement before resending: twice the round
 * trip, at least 30 ms (and 100 ms as the round trip until it is measured). */
static u64 resend_after(const Peer *p)
{
    const double rtt = p->rtt_ms < 0 ? 100.0 : p->rtt_ms;
    const double wait = rtt * 2.0 > 30.0 ? rtt * 2.0 : 30.0;
    return (u64)(wait * 1e6);
}

/* --- announcing a room --------------------------------------------------- */

/* Announces the host's room on the local network (ANNOUNCE, or GONE when
 * `gone`) and, online, to the server (REGISTER with the same body, or
 * UNREGISTER). */
static void announce(int gone)
{
    unsigned char *body;
    const size_t game = 20 + (size_t)net.info_size; /* size_t, so the sums cannot overflow */
    const size_t size = game + DETAILS_FIXED + net.room_name_size;
    const Route group = direct(&net.group);
    if (gone) {
        if (net.lan) {
            send_kind(&group, GONE, NULL, 0);
        }
        if (net.online) {
            to_server(UNREGISTER, NULL, 0);
        }
        return;
    }
    body = (unsigned char *)malloc(size);
    if (body == NULL) {
        return;
    }
    put32(body, net.build);
    put32(body + 4, 0); /* reserved for a digest of the file mods: 0 for now */
    put16(body + 8, khdays_host_udp_port(net.session));
#pragma warning(suppress : 6386) /* body holds 20 + info_size bytes (size above, in size_t): 12 fit */
    body[10] = (unsigned char)net.open;
    body[11] = (unsigned char)members();
    put32(body + 12, net.ggid);
    put32(body + 16, net.info_size);
    memcpy(body + 20, net.info, net.info_size);
    put32(body + game, DETAILS_MAGIC);
    put16(body + game + 4, net.server_ms < 0 ? 0xffffu : (unsigned int)(net.server_ms + 0.5));
    body[game + 6] = (unsigned char)(net.max_members + 1);
    body[game + 7] = (unsigned char)(net.locked ? 1 : 0);
    memcpy(body + game + 8, net.salt, SALT_SIZE);
    put16(body + game + 24, net.room_name_size);
    memcpy(body + game + DETAILS_FIXED, net.room_name, net.room_name_size);
    if (net.lan) {
        send_kind(&group, ANNOUNCE, body, (unsigned int)size);
    }
    /* The server is told less often than the local network. (To send a
     * change at once, set net.registered to 0.) */
    if (net.online && since(now_ns(), net.registered) >= REGISTER_EVERY) {
        to_server(REGISTER, body, (unsigned int)size);
        net.registered = now_ns();
        net.register_sent = net.registered;
        net.register_waiting = 1;
    }
    free(body);
}

/* Asks the server for the rooms, starting from room number `first`. The
 * datagram is padded to the size the server requires. */
static void ask_rooms(unsigned int first)
{
    unsigned char body[LIST_DATAGRAM - HEADER];
    memset(body, 0, sizeof(body));
    put32(body, net.build);
    put32(body + 4, net.scan_ggid);
    put16(body + 8, first);
    to_server(LIST, body, sizeof(body));
    net.listed = now_ns();
    if (first == 0) {
        net.list_sent = net.listed;
        net.list_waiting = 1;
    }
}

static Room *room_by_id(const unsigned char *id)
{
    for (int i = 0; i < net.room_count; ++i) {
        if (memcmp(net.rooms[i].id, id, ID_SIZE) == 0) {
            return &net.rooms[i];
        }
    }
    return NULL;
}

static void forget_room(Room *r)
{
    free(r->info);
    free(r->name);
    *r = net.rooms[--net.room_count];
}

/* Tells the game about a room. Only one report per room waits at a time:
 * a newer one replaces it. */
static void report_room(const Room *r)
{
    Queued *q;
    if (net.only && memcmp(r->id, net.only_id, ID_SIZE) != 0) {
        return;
    }
    for (int i = 0; i < net.event_count; ++i) {
        if (net.events[i].event.kind == KHDAYS_NET_ROOM && memcmp(net.events[i].event.id, r->id, ID_SIZE) == 0) {
            free(net.events[i].data);
            memmove(&net.events[i], &net.events[i + 1], (size_t)(net.event_count - i - 1) * sizeof(Queued));
            --net.event_count;
            break;
        }
    }
    q = push(KHDAYS_NET_ROOM, 0, r->info, r->info_size);
    memcpy(q->event.id, r->id, ID_SIZE);
    q->event.reason = (unsigned int)r->open;
}

/* --- what arrives ----------------------------------------------------- */

/* A peer is gone: tell the game (if it had joined) and forget it. */
static void left(Peer *p, const char *why)
{
    khdays_log("net: %s %u gone (%s)", net.role == HOST ? "member" : "host", p->aid, why);
    if (p->state == MEMBER) {
        push(KHDAYS_NET_LEFT, p->aid, NULL, 0);
    }
    free_peer(p);
    if (net.role == GUEST) {
        net.role = NONE;
    }
}

/* This machine's own addresses (read when the network starts), and whether
 * `ip` is one of them. */
static unsigned int *own_ips;
static int own_count;

static int own_address(unsigned int ip)
{
    if ((ip >> 24) == 127) {
        return 1;
    }
    for (int i = 0; i < own_count; ++i) {
        if (own_ips[i] == ip) {
            return 1;
        }
    }
    return 0;
}

/* A room announcement (ANNOUNCE's body), heard on the local network (`local`) or listed by the
 * server, reachable at `address`. A room seen on the local network keeps its local address. */
static void room_seen(const unsigned char *id, const KhdaysNetAddress *address, int local, const unsigned char *body,
                      unsigned int size, u64 now)
{
    Room *r;
    unsigned int info_size;
    if (size < 20 || net.role == HOST || memcmp(id, net.id, ID_SIZE) == 0) {
        return;
    }
    info_size = get32(body + 16);
    if (info_size > size - 20) {
        return;
    }
    r = room_by_id(id);
    if (r == NULL) {
        net.rooms = (Room *)grow(net.rooms, &net.room_room, net.room_count + 1, sizeof(*net.rooms));
        r = &net.rooms[net.room_count++];
        memset(r, 0, sizeof(*r));
        memcpy(r->id, id, ID_SIZE);
        r->probe_ms = -1.0;
    }
    if (local || !r->local) {
        r->address = *address;
        r->local = local;
    }
    r->build = get32(body);
    r->open = body[10] != 0;
    r->ggid = get32(body + 12);
    r->seen = now;
    free(r->info);
    r->info = copy_of(body + 20, info_size);
    r->info_size = info_size;
    r->players = 1 + body[11];
    {
        /* its details, when its build sends them */
        const unsigned int at = 20 + info_size;
        r->details = size - at >= DETAILS_FIXED && get32(body + at) == DETAILS_MAGIC;
        if (r->details) {
            const unsigned int ms = get16(body + at + 4);
            const unsigned int name = get16(body + at + 24);
            r->host_server_ms = ms == 0xffffu ? -1 : (int)ms;
            r->most = body[at + 6];
            r->locked = (body[at + 7] & 1) != 0;
            memcpy(r->salt, body + at + 8, SALT_SIZE);
            free(r->name);
            r->name_size = name <= size - at - DETAILS_FIXED ? name : size - at - DETAILS_FIXED;
            r->name = (char *)copy_of(body + at + DETAILS_FIXED, r->name_size);
            if (r->name == NULL) {
                r->name_size = 0;
            }
        }
    }
    if (net.scanning && r->ggid == net.scan_ggid && r->build == net.build) {
        report_room(r);
    }
}

/* A round trip to the server, from `sent` to `now`, smoothed in (as the members' are). */
static void server_round_trip(u64 sent, u64 now)
{
    const double sample = (double)since(now, sent) / 1e6;
    net.server_ms = net.server_ms < 0 ? sample : net.server_ms * 0.875 + sample * 0.125;
}

/* A message from the server (accepted only from the server's own address,
 * never relayed): a registration confirmed, a list of rooms, an introduction
 * of a guest and a host, or a room that is gone. */
static void on_server(unsigned int kind, const unsigned char *body, unsigned int size, u64 now)
{
    switch (kind) {
    case REGISTERED:
        if (net.register_waiting) {
            server_round_trip(net.register_sent, now);
            net.register_waiting = 0;
        }
        if (!net.registered_told) {
            net.registered_told = 1;
            khdays_log("net: the room is listed on the server");
        }
        return;
    case ROOMS: {
        /* total u16, first u16, count u16, then for each room: id, ip u32,
         * port u16, announcement size u16, announcement */
        unsigned int total, first, count, at = 6;
        if (size < 6 || !net.scanning) {
            return;
        }
        total = get16(body);
        first = get16(body + 2);
        count = get16(body + 4);
        if (first == 0 && net.list_waiting) {
            server_round_trip(net.list_sent, now);
            net.list_waiting = 0;
        }
        for (unsigned int i = 0; i < count; ++i) {
            KhdaysNetAddress address;
            unsigned int length;
            if (size - at < 14) {
                return;
            }
            length = get16(body + at + 12);
            if (length > size - at - 14) {
                return;
            }
            address.ip = get32(body + at + 6);
            address.port = (unsigned short)get16(body + at + 10);
            room_seen(body + at, &address, 0, body + at + 14, length, now);
            at += 14 + length;
        }
        if (count > 0 && first + count < total) {
            ask_rooms(first + count); /* the next page */
        }
        return;
    }
    case PEER: {
        /* token u32, the other side's id, ip u32, port u16 */
        KhdaysNetAddress address;
        unsigned char punch[4];
        unsigned int token;
        if (size < CONNECT_BODY) {
            return;
        }
        token = get32(body);
        address.ip = get32(body + 10);
        address.port = (unsigned short)get16(body + 14);
        put32(punch, token);
        if (net.role == HOST) {
            /* A guest is on its way: punch toward it until it arrives. Only
             * as many are kept as there are free places, the newest, so a
             * flood of forged introductions costs no more than a full room's
             * joins. */
            struct Introduced *g;
            const int free_places = (int)net.max_members - members();
            if (free_places <= 0) {
                return;
            }
            if (net.introduced_count >= free_places) {
                memmove(&net.introduced[0], &net.introduced[1],
                        (size_t)(net.introduced_count - 1) * sizeof(*net.introduced));
                --net.introduced_count;
            }
            net.introduced = (struct Introduced *)grow(net.introduced, &net.introduced_room, net.introduced_count + 1,
                                                       sizeof(*net.introduced));
            g = &net.introduced[net.introduced_count++];
            g->token = token;
            g->address = address;
            g->until = now + PUNCH_FOR;
            g->punched = 0;
        } else if (net.joining && net.join_online && net.join_token == 0 &&
                   memcmp(body + 4, net.join_id, ID_SIZE) == 0) {
            net.join_token = token;
            net.join_met = now;
            net.join_sent = 0;
            if (net.punch) {
                const Route host = direct(&address);
                net.join_address = host;
                send_kind(&host, PUNCH, punch, sizeof(punch));
            } else {
                net.join_address.address = net.server;
                net.join_address.relay = token;
                net.join_relayed = 1;
            }
        }
        return;
    }
    case GONE:
        if (net.joining && net.join_online && size >= ID_SIZE && memcmp(body, net.join_id, ID_SIZE) == 0) {
            net.joining = 0;
            push(KHDAYS_NET_REFUSED, 0, NULL, 0)->event.reason = KHDAYS_NET_REFUSE_SILENT;
            khdays_log("net: the room asked for is no longer on the server");
        }
        return;
    default: return;
    }
}

static int same_address(const KhdaysNetAddress *a, const KhdaysNetAddress *b)
{
    return a->ip == b->ip && a->port == b->port;
}

/* Handles one datagram from `from` (a direct address, or a pair relayed by
 * the server). */
/* A guest's proof of the room's password: the digest of the salt, the guest's id and the
 * password's digest (the password itself never travels, and the server never sees a proof it
 * could use for another guest). */
static void password_proof(const unsigned char *salt, const unsigned char *id, const unsigned char *digest,
                           unsigned char proof[DIGEST_SIZE])
{
    unsigned char joined[SALT_SIZE + ID_SIZE + DIGEST_SIZE];
    memcpy(joined, salt, SALT_SIZE);
    memcpy(joined + SALT_SIZE, id, ID_SIZE);
    memcpy(joined + SALT_SIZE + ID_SIZE, digest, DIGEST_SIZE);
    khdays_sha1(joined, sizeof(joined), proof);
}

/* Whether a JOIN (its body: build u32, 4 bytes, the SSID's size u8, 1 byte, the SSID, then the
 * proof) proves the room's password; any does for a room without one. */
static int password_proven(const unsigned char *sender, const unsigned char *body, unsigned int size)
{
    unsigned char expected[DIGEST_SIZE];
    unsigned int at;
    if (!net.locked) {
        return 1;
    }
    at = 10 + (body[8] <= size - 10 ? body[8] : size - 10);
    if (size - at < DIGEST_SIZE) {
        return 0;
    }
    password_proof(net.salt, sender, net.pass_digest, expected);
    return memcmp(body + at, expected, DIGEST_SIZE) == 0;
}

/* The refusal for other gameplay mods: the reason, how many the host plays with (u16), then as many of
 * them as one datagram holds, each its name and version as a u8 size and the bytes; first, a host
 * without the Stop magic names STOP_OFF_NAME with no version. */
static void refuse_for_mods(const Route *to)
{
    unsigned char body[DATAGRAM_RECORDS];
    unsigned int at = 3, read = 0, listed = 0;
    body[0] = KHDAYS_NET_REFUSE_MODS;
    if (!stop_magic) {
        body[at++] = (unsigned char)(sizeof(STOP_OFF_NAME) - 1);
        memcpy(body + at, STOP_OFF_NAME, sizeof(STOP_OFF_NAME) - 1);
        at += (unsigned int)sizeof(STOP_OFF_NAME) - 1;
        body[at++] = 0;
    }
    mods_ready();
    SDL_LockMutex(mods_lock);
    while (listed < mods_count && read < mods_size) {
        unsigned int entry = 0;
        for (int part = 0; part < 2 && read + entry < mods_size; ++part) {
            entry += 1u + mods_list[read + entry];
        }
        if (read + entry > mods_size || at + entry > sizeof(body)) {
            break;
        }
        memcpy(body + at, mods_list + read, entry);
        at += entry;
        read += entry;
        ++listed;
    }
    put16(body + 1, (unsigned short)(mods_count + !stop_magic));
    SDL_UnlockMutex(mods_lock);
    send_kind(to, REFUSE, body, at);
}

/* A guest's reading of that refusal: "name version, ..." and how many were left out; the host's being
 * without Stop apart (khdays_net_last_refusal_stop_off). */
static void take_refusal_mods(const unsigned char *body, unsigned int size)
{
    const unsigned int count = size >= 3 ? get16(body + 1) : 0;
    unsigned int at = 3, shown = 0, written = 0, length = 0;
    char *text = (char *)malloc(size * 2 + 1);
    free(net.refusal_mods);
    net.refusal_mods = text;
    net.refusal_more = 0;
    net.refusal_stop_off = 0;
    if (text == NULL) {
        return;
    }
    while (at < size && shown < count) {
        const unsigned int name = body[at];
        if (name == sizeof(STOP_OFF_NAME) - 1 && at + 2 + name <= size &&
            memcmp(body + at + 1, STOP_OFF_NAME, name) == 0 && body[at + 1 + name] == 0) {
            net.refusal_stop_off = 1;
            at += 2 + name;
            ++shown;
            continue;
        }
        for (int part = 0; part < 2; ++part) {
            const unsigned int n = at < size ? body[at] : 0;
            if (at + 1 + n > size) {
                at = size;
                break;
            }
            if (part == 0 && written > 0) {
                text[length++] = ',';
                text[length++] = ' ';
            } else if (part == 1) {
                text[length++] = ' ';
            }
            memcpy(text + length, body + at + 1, n);
            length += n;
            at += 1 + n;
        }
        ++shown;
        ++written;
    }
    text[length] = 0;
    net.refusal_more = (int)(count - shown);
}

static void on_datagram(const Route *from, const unsigned char *d, long got, u64 now)
{
    const unsigned char *sender, *body;
    unsigned int kind, size;
    Peer *p;
    if (got < HEADER || get32(d) != MAGIC || d[5] != 1) {
        return;
    }
    kind = d[4];
    size = get16(d + 6);
    if ((long)HEADER + (long)size > got) {
        return;
    }
    sender = d + 8;
    body = d + HEADER;
    if (net.online && from->relay == 0 && same_address(&from->address, &net.server)) {
        if (kind == RELAY && size >= 4 + HEADER) {
            /* a relayed datagram: handle it as if it came from the other player */
            Route relayed;
            relayed.address = net.server;
            relayed.relay = get32(body);
            if (relayed.relay != 0) {
                on_datagram(&relayed, body + 4, (long)size - 4, now);
            }
        } else {
            on_server(kind, body, size, now);
        }
        return;
    }
    if (kind >= REGISTER && kind <= UNREGISTER) {
        return; /* server kinds from anyone but the server are ignored; a PUNCH only opens the routers */
    }
    if (memcmp(sender, net.id, ID_SIZE) == 0) {
        return; /* our own announcement, looped back */
    }
    p = peer_by_id(sender);
    if (p != NULL) {
        p->heard = now;
    }
    switch (kind) {
    case ANNOUNCE: {
        KhdaysNetAddress address;
        if (!net.lan || from->relay != 0 || size < 20) {
            return;
        }
        /* A room on this same machine is joined through the loopback address,
         * which no firewall blocks (its announcement may have come in on any
         * of the machine's interfaces). */
        address.ip = own_address(from->address.ip) ? 0x7f000001u : from->address.ip;
        address.port = (unsigned short)get16(body + 8);
        room_seen(sender, &address, 1, body, size, now);
        return;
    }
    case GONE: {
        Room *r = room_by_id(sender);
        if (r != NULL && from->relay == 0) {
            forget_room(r);
        }
        return;
    }
    case JOIN: {
        unsigned char refuse;
        unsigned int aid;
        Peer *slot;
        if (net.role != HOST || size < 10) {
            return;
        }
        if (p != NULL) {
            if (p->state == MEMBER) {
                unsigned char welcome[8] = {(unsigned char)p->aid};
                send_kind(&p->address, WELCOME, welcome, sizeof(welcome));
            }
            return;
        }
        refuse = get32(body) != net.build                     ? KHDAYS_NET_REFUSE_BUILD
                 : get32(body + 4) != own_mods_fingerprint()  ? KHDAYS_NET_REFUSE_MODS
                 : !net.open                                  ? KHDAYS_NET_REFUSE_CLOSED
                 : (unsigned int)members() >= net.max_members ? KHDAYS_NET_REFUSE_FULL
                 : !password_proven(sender, body, size)       ? KHDAYS_NET_REFUSE_PASSWORD
                                                              : 0;
        if (refuse == KHDAYS_NET_REFUSE_MODS) {
            refuse_for_mods(from);
            khdays_log("net: a join refused: other gameplay mods");
            return;
        }
        if (refuse != 0) {
            send_kind(from, REFUSE, &refuse, 1);
            khdays_log("net: a join refused (reason %u)", refuse);
            return;
        }
        for (aid = 1; peer_by_aid(aid) != NULL; ++aid) {
        }
        slot = peer_slot(aid);
        new_peer(slot, aid, sender, from);
        slot->state = PENDING;
        /* a guest the server introduced is here: no more punching toward it */
        for (int i = 0; i < net.introduced_count;) {
            const struct Introduced *g = &net.introduced[i];
            if (from->relay != 0 ? g->token == from->relay : same_address(&g->address, &from->address)) {
                /* keep the order: oldest first */
                memmove(&net.introduced[i], &net.introduced[i + 1],
                        (size_t)(net.introduced_count - i - 1) * sizeof(*net.introduced));
                --net.introduced_count;
            } else {
                ++i;
            }
        }
        {
            const unsigned int ssid = body[8] <= size - 10 ? body[8] : size - 10;
            Queued *q = push(KHDAYS_NET_JOINING, aid, body + 10, ssid);
            memcpy(q->event.id, sender, ID_SIZE);
        }
        return;
    }
    case WELCOME:
        if (!net.joining || memcmp(sender, net.join_id, ID_SIZE) != 0 || size < 1) {
            return;
        }
        net.joining = 0;
        net.role = GUEST;
        net.own_aid = body[0];
        for (int i = 0; i < net.level_room; ++i) {
            net.levels[i] = 0; /* this room's come with the host's pings */
        }
        forget_stops();
        mods_forget();
        /* from now on, reach the host the way its welcome came: directly or
         * through the server */
        new_peer(peer_slot(0), 0, sender, from);
        net.peers[0].state = MEMBER;
        send_missions(&net.peers[0]);
        push(KHDAYS_NET_JOINED, net.own_aid, NULL, 0);
        khdays_log("net: joined as member %u%s", net.own_aid, from->relay != 0 ? ", through the server" : "");
        return;
    case REFUSE:
        if (!net.joining || memcmp(sender, net.join_id, ID_SIZE) != 0 || size < 1) {
            return;
        }
        net.joining = 0;
        net.last_refusal = body[0];
        if (body[0] == KHDAYS_NET_REFUSE_MODS) {
            take_refusal_mods(body, size);
        }
        push(KHDAYS_NET_REFUSED, 0, NULL, 0)->event.reason = body[0];
        khdays_log("net: the join was refused (reason %u)", body[0]);
        return;
    case RECORD: {
        unsigned int port, count, at = 2;
        if (p == NULL || p->state != MEMBER || size < 2) {
            return;
        }
        port = body[0];
        count = body[1];
        if (port >= PORTS) {
            return;
        }
        /* take each record only if it is whole; a broken one ends the datagram */
        for (unsigned int i = 0; i < count; ++i) {
            unsigned int length;
            if (size - at < RECORD_HEADER) {
                return;
            }
            length = get16(body + at);
            if (length > size - at - RECORD_HEADER) {
                return;
            }
            received_record(p, port, get32(body + at + 2), body + at + RECORD_HEADER, length);
            at += RECORD_HEADER + length;
        }
        return;
    }
    case ACK:
        if (p == NULL || size < 12 || body[0] >= PORTS) {
            return;
        }
        acknowledged(p, body[0], get32(body + 4), get32(body + 8), now);
        return;
    case PING:
        if (p != NULL && size >= 8) {
            send_kind(&p->address, PONG, body, 8);
            if (p->state == MEMBER) {
                ping_levels(p, body, size);
            }
        } else if (p == NULL && net.role == HOST && from->relay == 0 && size >= 8) {
            /* a guest looking at our room (its list's ping) */
            if (since(now, net.probes_since) >= 1000 * MS) {
                net.probes_since = now;
                net.probes_answered = 0;
            }
            if (net.probes_answered < PROBES_ANSWERED) {
                ++net.probes_answered;
                send_kind(from, PONG, body, 8);
            }
        }
        return;
    case PONG:
        if (p != NULL && size >= 8) {
            const double sample = (double)since(now, get64(body)) / 1e6;
            ++p->pongs;
            p->rtt_ms = p->rtt_ms < 0 ? sample : p->rtt_ms * 0.875 + sample * 0.125;
        } else if (p == NULL && size >= 8 && from->relay == 0) {
            Room *r = room_by_id(sender);
            if (r != NULL && r->local) {
                const double sample = (double)since(now, get64(body)) / 1e6;
                r->probe_ms = r->probe_ms < 0 ? sample : r->probe_ms * 0.75 + sample * 0.25;
            }
        }
        return;
    case LEAVE:
        if (p != NULL) {
            left(p, "said goodbye");
        }
        return;
    case STICK:
        if (net.role == HOST && p != NULL && p->state == MEMBER && size >= STICK_BODY) {
            p->stick.x = (short)get16(body);
            p->stick.y = (short)get16(body + 2);
            p->stick.run_threshold = body[4];
            p->stick.walk_speed = body[5];
            p->stick.on = body[6];
            p->stick_at = now;
        }
        return;
    default: return;
    }
}

/* --- the thread ------------------------------------------------------- */

/* Counts of the game's messages by gate, as sent and as handed to it, logged every REPORT_EVERY while
 * there are members. A message is a u16 header (bit 0 always set by MsgQueue_Enqueue, the gate in bits
 * 1-5, the payload size in bits 6-15) and its payload; zero bytes between messages are padding. */
static unsigned int census_sent[GATES], census_got[GATES];
static u64 census_reported;

static void census(unsigned int *counts, const unsigned char *d, unsigned int size)
{
    unsigned int at = 0;
    while (at + 2 <= size) {
        unsigned int header;
        if (d[at] == 0) {
            ++at;
            continue;
        }
        header = (unsigned int)d[at] | (unsigned int)d[at + 1] << 8;
        ++counts[(header >> 1) & 31u];
        at += 2 + (header >> 6);
    }
}

/* KHDAYS_TRACE_WM=1 writes every send and hand-over on the game's port to the log, with its gates and
 * frame (written from the game's thread). */
static int trace_messages(void)
{
    static int on = -1;
    if (on < 0) {
        const char *value = getenv("KHDAYS_TRACE_WM");
        on = value != NULL && value[0] == '1';
    }
    return on;
}

static void census_text(const unsigned int *counts, char *out, size_t room);
static int control_in(const unsigned char *d, unsigned int size, int *only_control, const unsigned char **payload,
                      unsigned int *length_out);

static void trace_record(const char *what, unsigned int aid, const unsigned char *d, unsigned int size)
{
    unsigned int counts[GATES] = {0};
    char gates[256];
    census(counts, d, size);
    census_text(counts, gates, sizeof(gates));
    khdays_log("net: %s %u:%s", what, aid, gates);
}

/* Writes " gate:count" for each gate counted into `out` (" none" if none). */
static void census_text(const unsigned int *counts, char *out, size_t room)
{
    size_t at = 0;
    out[0] = '\0';
    for (int g = 0; g < GATES; ++g) {
        if (counts[g] != 0 && at + 16 < room) {
            at += (size_t)snprintf(out + at, room - at, " %d:%u", g, counts[g]);
        }
    }
    if (at == 0) {
        snprintf(out, room, " none");
    }
}

/* How many peers have joined (not just joining). */
static int members_now(void)
{
    int n = 0;
    for (int i = 0; i < net.peer_room; ++i) {
        n += net.peers[i].used && net.peers[i].state == MEMBER;
    }
    return n;
}

/* Every REPORT_EVERY, writes the message counts and the playout store's
 * figures to the session log, then starts counting again. */
static void census_report(u64 now)
{
    char sent[256], got[256];
    const double seconds = (double)since(now, census_reported) / 1e9;
    if (since(now, census_reported) < REPORT_EVERY) {
        return;
    }
    if (members_now() > 0 && seconds > 0.0) {
        census_text(census_sent, sent, sizeof(sent));
        census_text(census_got, got, sizeof(got));
        khdays_log("net: messages by gate (gate:count) sent%s; handed to the game%s; %.0f datagrams and %.1f KB "
                   "sent a second",
                   sent, got, (double)net.sent_datagrams / seconds, (double)net.sent_bytes / 1024.0 / seconds);
        if (net.role == HOST) {
            for (int i = 0; i < net.peer_room; ++i) {
                Peer *m = &net.peers[i];
                if (m->used && m->state == MEMBER) {
                    census_text(m->census_got, got, sizeof(got));
                    khdays_log("net: member %u's messages handed to the game%s; controls one a tick from a store of "
                               "%u kept ahead, %u held over a tick, the store run dry %u times, %u held repeats "
                               "left out",
                               m->aid, got, m->play_depth, m->controls_held, m->play_dry, m->play_trimmed);
                }
                memset(m->census_got, 0, sizeof(m->census_got));
                m->controls_held = m->controls_dropped = 0;
                m->play_dry = m->play_trimmed = 0;
            }
        }
    }
    memset(census_sent, 0, sizeof(census_sent));
    memset(census_got, 0, sizeof(census_got));
    net.sent_datagrams = net.sent_bytes = 0;
    census_reported = now;
}

/* Sends the delayed datagrams that are due (KHDAYS_NET_DELAY_MS). */
static void send_delayed(u64 now)
{
    for (int i = 0; i < net.out_count;) {
        if (net.out[i].due <= now) {
            khdays_host_udp_send(net.session, &net.out[i].to, net.out[i].bytes, net.out[i].size);
            free(net.out[i].bytes);
            net.out[i] = net.out[--net.out_count];
        } else {
            ++i;
        }
    }
}

/* Host only: punches toward the guests the server introduced, so that their
 * routers let the guests' datagrams through. Only as many as there are free
 * places are kept (the oldest go first), and each for PUNCH_FOR at most. */
static void punch_introduced(u64 now)
{
    while (net.introduced_count > 0 && net.introduced_count > (int)net.max_members - members()) {
        memmove(&net.introduced[0], &net.introduced[1], (size_t)(net.introduced_count - 1) * sizeof(*net.introduced));
        --net.introduced_count;
    }
    for (int i = 0; i < net.introduced_count;) {
        struct Introduced *g = &net.introduced[i];
        if (now > g->until || net.role != HOST) {
            memmove(&net.introduced[i], &net.introduced[i + 1],
                    (size_t)(net.introduced_count - i - 1) * sizeof(*net.introduced));
            --net.introduced_count;
            continue;
        }
        if (net.punch && since(now, g->punched) >= PUNCH_EVERY) {
            unsigned char punch[4];
            const Route guest = direct(&g->address);
            put32(punch, g->token);
            send_kind(&guest, PUNCH, punch, sizeof(punch));
            g->punched = now;
        }
        ++i;
    }
}

/* Forgets the rooms not heard from for ROOM_SILENT. */
static void forget_silent_rooms(u64 now)
{
    for (int i = 0; i < net.room_count;) {
        if (since(now, net.rooms[i].seen) > ROOM_SILENT) {
            forget_room(&net.rooms[i]);
        } else {
            ++i;
        }
    }
}

/* Guest only: moves a join forward. Online it asks the server for an
 * introduction until it gets one, then sends JOIN to the host directly, and
 * through the server's relay if the host stays silent for RELAY_AFTER. It
 * gives up after JOIN_SILENT. */
static void join_step(u64 now)
{
    if (net.joining && net.join_online && net.join_token != 0 && !net.join_relayed &&
        since(now, net.join_met) >= RELAY_AFTER) {
        /* the host did not answer directly: go through the server */
        net.join_address.address = net.server;
        net.join_address.relay = net.join_token;
        net.join_relayed = 1;
        net.join_sent = 0;
        khdays_log("net: the host cannot be reached straight; joining through the server");
    }
    if (!net.joining) {
        return;
    }
    {
        /* online, the time limit counts from the introduction */
        const u64 began = net.join_online && net.join_token != 0 ? net.join_met : net.join_started;
        if (since(now, began) > JOIN_SILENT) {
            net.joining = 0;
            push(KHDAYS_NET_REFUSED, 0, NULL, 0)->event.reason = KHDAYS_NET_REFUSE_SILENT;
            khdays_log("net: the room did not answer");
        } else if (net.join_online && net.join_token == 0) {
            /* not introduced yet: ask the server again */
            if (since(now, net.connect_sent) >= JOIN_EVERY) {
                unsigned char body[CONNECT_BODY];
                memset(body, 0, sizeof(body));
                memcpy(body, net.join_id, ID_SIZE);
                to_server(CONNECT, body, sizeof(body));
                net.connect_sent = now;
            }
        } else if (since(now, net.join_sent) >= JOIN_EVERY) {
            unsigned char body[10 + SSID_SIZE + DIGEST_SIZE];
            unsigned int size = 10 + net.ssid_size;
            memset(body, 0, sizeof(body));
            put32(body, net.build);
            put32(body + 4, own_mods_fingerprint()); /* the gameplay mods this copy plays with */
            body[8] = (unsigned char)net.ssid_size;
            memcpy(body + 10, net.ssid, net.ssid_size);
            if (net.join_locked && net.join_password) {
                password_proof(net.join_salt, net.id, net.join_digest, body + size);
                size += DIGEST_SIZE;
            }
            send_kind(&net.join_address, JOIN, body, size);
            net.join_sent = now;
        }
    }
}

/* A member's Stop (STOP_KNOWN with its bytes) as its STOP_BYTES bytes, and back. */
static void put_stop(unsigned char *at, u64 stop)
{
    for (unsigned int i = 0; i < STOP_BYTES; ++i) {
        at[i] = (unsigned char)(stop >> 8 * (STOP_BYTES - 1 - i));
    }
}

static u64 get_stop(const unsigned char *at)
{
    u64 stop = STOP_KNOWN;
    for (unsigned int i = 0; i < STOP_BYTES; ++i) {
        stop |= (u64)at[i] << 8 * (STOP_BYTES - 1 - i);
    }
    return stop;
}

/* A ping's body, and its size: when it was sent (the pong gives it back), then the players' levels
 * for the members' screen and their Stop for the battle. A guest sends its own level (u16) and, when
 * known, its own Stop (STOP_BYTES); the host a count (u8), then an aid (u8) and a level (u16) for each
 * member whose level it knows, itself included, then a count (u8) and an aid and a Stop for each member
 * whose Stop it knows. */
static unsigned int ping_body(u64 now, unsigned char **body, int *room)
{
    const int own = net.own_level;
    const u64 own_stop = net.own_stop;
    unsigned int size, count = 0, stops = 0;
    if (net.role == HOST) {
        for (int i = 0; i < net.peer_room; ++i) {
            count += net.peers[i].used && net.peers[i].state == MEMBER && net.peers[i].level > 0;
            stops += net.peers[i].used && net.peers[i].state == MEMBER && net.peers[i].stop != 0;
        }
        count += own > 0;
        stops += own_stop != 0;
    }
    *body = (unsigned char *)grow(*body, room, (int)(8 + 3 + 3 * count + 1 + (1 + STOP_BYTES) * stops + STOP_BYTES), 1);
    put64(*body, now);
    if (net.role == GUEST) {
        put16(*body + 8, (unsigned int)(own > 0 ? own : 0));
        if (own_stop == 0) {
            return 10;
        }
        put_stop(*body + 10, own_stop);
        return 10 + STOP_BYTES;
    }
    (*body)[8] = (unsigned char)count;
    size = 9;
    if (own > 0) {
        (*body)[size] = 0;
        put16(*body + size + 1, (unsigned int)own);
        size += 3;
    }
    for (int i = 0; i < net.peer_room; ++i) {
        const Peer *q = &net.peers[i];
        if (q->used && q->state == MEMBER && q->level > 0) {
            (*body)[size] = (unsigned char)q->aid;
            put16(*body + size + 1, (unsigned int)q->level);
            size += 3;
        }
    }
    (*body)[size++] = (unsigned char)stops;
    if (own_stop != 0) {
        (*body)[size] = 0;
        put_stop(*body + size + 1, own_stop);
        size += 1 + STOP_BYTES;
    }
    for (int i = 0; i < net.peer_room; ++i) {
        const Peer *q = &net.peers[i];
        if (q->used && q->state == MEMBER && q->stop != 0) {
            (*body)[size] = (unsigned char)q->aid;
            put_stop(*body + size + 1, q->stop);
            size += 1 + STOP_BYTES;
        }
    }
    return size;
}

/* A member's ping: on the host a guest's level and Stop, on a guest the members' (ping_body). */
static void ping_levels(Peer *p, const unsigned char *body, unsigned int size)
{
    if (net.role == HOST && size >= 10) {
        p->level = (int)get16(body + 8);
        p->stop = size >= 10 + STOP_BYTES ? get_stop(body + 10) : 0;
    } else if (net.role == GUEST && size >= 9) {
        const unsigned int count = body[8];
        unsigned int at = 9 + 3 * count;
        for (int i = 0; i < net.level_room; ++i) {
            net.levels[i] = 0;
        }
        for (unsigned int k = 0; k < count && 9 + 3 * k + 3 <= size; ++k) {
            const unsigned int aid = body[9 + 3 * k];
            net.levels = (int *)grow(net.levels, &net.level_room, (int)aid + 1, sizeof(*net.levels));
            net.levels[aid] = (int)get16(body + 9 + 3 * k + 1);
        }
        forget_stops();
        if (at < size) {
            const unsigned int stops = body[at++];
            for (unsigned int k = 0; k < stops && at + 1 + STOP_BYTES <= size; ++k, at += 1 + STOP_BYTES) {
                const unsigned int aid = body[at];
                net.stops = (u64 *)grow(net.stops, &net.stop_room, (int)aid + 1, sizeof(*net.stops));
                net.stops[aid] = get_stop(body + at + 1);
            }
        }
    }
}

/* Keeps one peer's link alive: drops it if it fell silent, pings it, sends
 * owed acknowledgements, resends what was not acknowledged in time, and
 * writes its line of the session log every REPORT_EVERY. */
static void peer_step(Peer *p, u64 now)
{
    if (since(now, p->heard) > member_silent) {
        left(p, "silent");
        return;
    }
    if (since(now, p->pinged) >= PING_EVERY) {
        static unsigned char *body;
        static int body_room;
        const unsigned int size = ping_body(now, &body, &body_room);
        send_kind(&p->address, PING, body, size);
        p->pinged = now;
        ++p->pings;
    }
    for (unsigned int port = 0; port < PORTS; ++port) {
        Stream *stream = &p->streams[port];
        const u64 wait = resend_after(p);
        int due = 0;
        if (stream->ack_due) {
            send_ack(p, port);
        }
        /* resend the records still unacknowledged after the wait (no later
         * datagram brought them again) */
        net.list = (Record **)grow(net.list, &net.list_room, stream->unacked.count, sizeof(*net.list));
        for (int k = 0; k < stream->unacked.count; ++k) {
            Record *r = &stream->unacked.items[k];
            if (since(now, r->sent) >= wait) {
                r->sent = now;
                r->resent = 1;
                net.list[due++] = r;
            }
        }
        if (due > 0) {
            p->resent += (unsigned int)due;
            send_list(p, port, net.list, due);
        }
    }
    if (p->state == MEMBER) {
        unsigned int waiting = 0, unacked = 0;
        for (int k = 0; k < net.event_count; ++k) {
            waiting += net.events[k].event.kind == KHDAYS_NET_RECORD && net.events[k].event.aid == p->aid;
        }
        for (unsigned int port = 0; port < PORTS; ++port) {
            unacked += (unsigned int)p->streams[port].unacked.count;
        }
        p->waiting_most = waiting > p->waiting_most ? waiting : p->waiting_most;
        p->unacked_most = unacked > p->unacked_most ? unacked : p->unacked_most;
        if (since(now, p->reported) >= REPORT_EVERY) {
            p->loss = p->pings > 0 && p->pongs < p->pings ? 1.0 - (double)p->pongs / (double)p->pings : 0.0;
            khdays_log("net: %s %u: round trip %.1f ms, %.0f%% of pings unanswered, %u records resent, at most %u "
                       "waiting for the game and %u for an acknowledgement",
                       net.role == HOST ? "member" : "host", p->aid, p->rtt_ms, p->loss * 100.0, p->resent,
                       p->waiting_most, p->unacked_most);
            p->pings = p->pongs = p->resent = p->waiting_most = p->unacked_most = 0;
            p->reported = now;
        }
    }
}

/* Everything the network thread does on a timer, after reading what arrived. */
/* Pings the rooms of the local network (their hosts answer: PING above), for the list's ping. */
static void probe_rooms(u64 now)
{
    for (int i = 0; i < net.room_count; ++i) {
        Room *r = &net.rooms[i];
        if (r->local && since(now, r->probed) >= PROBE_EVERY) {
            unsigned char ping[8];
            const Route to = direct(&r->address);
            put64(ping, now);
            send_kind(&to, PING, ping, sizeof(ping));
            r->probed = now;
        }
    }
}

static void tick(u64 now)
{
    send_delayed(now);
    if (net.role == HOST && since(now, net.announced) >= ANNOUNCE_EVERY) {
        net.announced = now;
        announce(0);
    }
    /* online, while the game looks for rooms: ask the server for them */
    if (net.online && net.scanning && net.role != HOST && since(now, net.listed) >= LIST_EVERY) {
        ask_rooms(0);
    }
    if (net.scanning && net.role != HOST) {
        probe_rooms(now);
    }
    punch_introduced(now);
    forget_silent_rooms(now);
    join_step(now);
    for (int i = 0; i < net.peer_room; ++i) {
        if (net.peers[i].used) {
            peer_step(&net.peers[i], now);
        }
    }
    census_report(now);
}

/* The network thread: waits for datagrams (5 ms at most), handles them, then
 * does the timed work. Everything runs under net.lock. */
static int SDLCALL run(void *unused)
{
    static unsigned char buffer[65536];
    (void)unused;
    while (net.running) {
        KhdaysUdp *udps[2];
        u64 now;
        SDL_LockMutex(net.lock);
        udps[0] = net.session;
        udps[1] = net.lobby;
        SDL_UnlockMutex(net.lock);
        khdays_host_udp_wait(udps, 2, 5);
        SDL_LockMutex(net.lock);
        now = now_ns();
        for (int which = 0; which < 2; ++which) {
            Route from;
            long got;
            if (udps[which] == NULL) {
                continue;
            }
            from.relay = 0;
            while ((got = khdays_host_udp_receive(udps[which], &from.address, buffer, sizeof(buffer))) >= 0) {
                on_datagram(&from, buffer, got, now);
            }
        }
        tick(now);
        SDL_UnlockMutex(net.lock);
    }
    return 0;
}

/* --- the functions the wireless layer calls ---------------------------- */

/* Starts the network: reads the testing knobs and the server setting, opens
 * the sockets and starts the network thread. */
int khdays_net_begin(void)
{
    const char *value;
    if (net.started) {
        return 1;
    }
    if (!khdays_host_net_start()) {
        khdays_log("net: no network");
        return 0;
    }
    if (net.lock == NULL) {
        net.lock = SDL_CreateMutex();
    }
    value = getenv("KHDAYS_NET_DELAY_MS");
    net.delay_ms = value != NULL ? atof(value) : 0.0;
    value = getenv("KHDAYS_NET_LOSS");
    net.loss = value != NULL ? atof(value) : 0.0;
    value = getenv("KHDAYS_NET_JITTER_MS");
    net.jitter_ms = value != NULL ? atof(value) : 0.0;
    value = getenv("KHDAYS_NET_ONE_CONTROL");
    net.one_control = value == NULL || value[0] != '0'; /* on unless set to 0 */
    value = getenv("KHDAYS_NET_TIMEOUT_MS");
    if (value != NULL && atof(value) > 0) {
        member_silent = (u64)(atof(value) * 1e6);
    }
    value = getenv("KHDAYS_NET_PORT");
    net.group.ip = GROUP_IP;
    net.group.port = (unsigned short)(value != NULL ? atoi(value) : GROUP_PORT);
    /* The server: KHDAYS_NET_SERVER if set (empty: none), else the player's
     * settings. KHDAYS_NET_LAN=0 skips the local network, KHDAYS_NET_PUNCH=0
     * never tries to reach a host directly. */
    value = getenv("KHDAYS_NET_SERVER");
    if (value == NULL) {
        static char setting[262];
        setting[0] = '\0';
        if (server_source == NULL || !server_source(setting, (int)sizeof(setting))) {
            setting[0] = '\0';
        }
        value = setting;
    }
    net.online = value[0] != '\0' && khdays_host_net_resolve(value, SERVER_PORT, &net.server);
    if (value[0] != '\0' && !net.online) {
        khdays_log("net: the server's name does not resolve: rooms on the local network only");
    }
    value = getenv("KHDAYS_NET_LAN");
    net.lan = value == NULL || strcmp(value, "0") != 0;
    value = getenv("KHDAYS_NET_PUNCH");
    net.punch = value == NULL || strcmp(value, "0") != 0;
    net.session = khdays_host_udp_open(0, 0);
    net.lobby = khdays_host_udp_open(net.group.port, 1);
    if (net.session == NULL || net.lobby == NULL) {
        khdays_host_udp_close(net.session);
        khdays_host_udp_close(net.lobby);
        net.session = net.lobby = NULL;
        khdays_log("net: no socket for the session");
        return 0;
    }
    free(own_ips);
    own_count = khdays_host_net_addresses(NULL, 0);
    own_ips = (unsigned int *)calloc((size_t)own_count, sizeof(*own_ips));
    if (own_ips != NULL) {
        khdays_host_net_addresses(own_ips, own_count);
    } else {
        own_count = 0;
    }
    khdays_host_udp_join(net.lobby, GROUP_IP);
    khdays_host_udp_join(net.session, GROUP_IP); /* so announcements go out on every interface */
    khdays_host_random(net.id, ID_SIZE);
    net.id[0] = (unsigned char)((net.id[0] & 0xfe) | 0x02); /* unicast, locally administered */
    khdays_host_random(&net.random, sizeof(net.random));
    net.build = khdays_host_build_identity();
    net.server_ms = -1.0;
    net.role = NONE;
    net.running = 1;
    net.started = 1;
    net.thread = SDL_CreateThread(run, "khdays-net", NULL);
    khdays_log("net: up, session port %u, rooms on port %u%s%s%s%s", khdays_host_udp_port(net.session), net.group.port,
               net.online ? ", online through the server" : "", net.lan ? "" : ", not on the local network",
               net.punch ? "" : ", never straight",
               net.delay_ms > 0 || net.loss > 0 || net.jitter_ms > 0 ? " (delay/loss/jitter knobs on)" : "");
    return 1;
}

/* Leaves the session (the caller holds net.lock): says goodbye to every
 * peer and closes the room. */
static void leave_locked(void)
{
    if (net.role == HOST) {
        announce(1);
    }
    for (int i = 0; i < net.peer_room; ++i) {
        if (net.peers[i].used) {
            unsigned char reason = 0;
            send_kind(&net.peers[i].address, LEAVE, &reason, 1);
        }
    }
    drop_peers();
    for (int i = 0; i < net.level_room; ++i) {
        net.levels[i] = 0;
    }
    forget_stops();
    mods_forget();
    net.role = NONE;
    net.joining = 0;
    net.join_online = net.join_relayed = 0;
    net.join_token = 0;
    net.introduced_count = 0;
    net.open = 0;
}

void khdays_net_end(void)
{
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    leave_locked();
    net.scanning = 0;
    drop_events();
    memset(net.join_up_to, 0, sizeof(net.join_up_to));
    while (net.room_count > 0) {
        forget_room(&net.rooms[0]);
    }
    /* send the goodbyes before the sockets close, whatever the delay knob says */
    for (int i = 0; i < net.out_count; ++i) {
        khdays_host_udp_send(net.session, &net.out[i].to, net.out[i].bytes, net.out[i].size);
        free(net.out[i].bytes);
    }
    net.out_count = 0;
    net.running = 0;
    SDL_UnlockMutex(net.lock);
    SDL_WaitThread(net.thread, NULL);
    net.thread = NULL;
    khdays_host_udp_close(net.session);
    khdays_host_udp_close(net.lobby);
    net.session = net.lobby = NULL;
    net.started = 0;
    khdays_log("net: down");
}

void khdays_net_id(unsigned char *id)
{
    memcpy(id, net.id, ID_SIZE);
}

int khdays_net_host(unsigned int ggid, const void *info, unsigned int size, unsigned int max_members)
{
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    leave_locked();
    net.role = HOST;
    khdays_host_random(&net.mods_seed, sizeof(net.mods_seed)); /* the room's first seed */
    net.mods_seed_known = 1;
    net.ggid = ggid;
    net.max_members = max_members;
    net.open = 1;
    free(net.info);
    net.info = copy_of(info, size);
    net.info_size = size;
    {
        const unsigned int most =
            ANNOUNCE_MOST > 20 + size + DETAILS_FIXED ? ANNOUNCE_MOST - 20 - size - DETAILS_FIXED : 0;
        if (net.room_name_size > most) {
            net.room_name_size = most;
            while (net.room_name_size > 0 && ((unsigned char)net.room_name[net.room_name_size] & 0xc0) == 0x80) {
                --net.room_name_size; /* not in the middle of a character */
            }
        }
    }
    if (net.locked) {
        khdays_host_random(net.salt, SALT_SIZE); /* each room its own */
    }
    net.announced = now_ns();
    net.registered = 0; /* register on the server right away */
    net.registered_told = 0;
    announce(0);
    SDL_UnlockMutex(net.lock);
    khdays_log("net: a room of ours (group 0x%x, up to %u guests)", ggid, max_members);
    return 1;
}

void khdays_net_entry(int open)
{
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    net.open = open != 0;
    if (net.role == HOST) {
        net.registered = 0; /* tell the server right away */
        announce(0);
    }
    SDL_UnlockMutex(net.lock);
}

void khdays_net_scan(unsigned int ggid, int on)
{
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    net.scanning = on != 0;
    net.scan_ggid = ggid;
    if (on) {
        /* report the rooms already known right away, and ask the server now */
        for (int i = 0; i < net.room_count; ++i) {
            if (net.rooms[i].ggid == ggid && net.rooms[i].build == net.build) {
                report_room(&net.rooms[i]);
            }
        }
        if (net.online && net.role != HOST) {
            ask_rooms(0);
        }
    }
    SDL_UnlockMutex(net.lock);
}

int khdays_net_join(const unsigned char *id, const void *ssid, unsigned int size)
{
    Room *r;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    r = room_by_id(id);
    if (r == NULL) {
        SDL_UnlockMutex(net.lock);
        khdays_log("net: the room to join is not known");
        return 0;
    }
    leave_locked();
    net.joining = 1;
    memcpy(net.join_id, id, ID_SIZE);
    net.join_address = direct(&r->address);
    /* a room known only from the server: the server introduces us first (PEER) */
    net.join_online = net.online && !r->local;
    net.join_token = 0;
    net.join_relayed = 0;
    net.connect_sent = 0;
    net.ssid_size = size < SSID_SIZE ? size : SSID_SIZE;
    memcpy(net.ssid, ssid, net.ssid_size);
    net.join_locked = r->details && r->locked;
    net.last_refusal = 0;
    memcpy(net.join_salt, r->salt, SALT_SIZE);
    net.join_started = now_ns();
    net.join_sent = 0;
    SDL_UnlockMutex(net.lock);
    return 1;
}

void khdays_net_accept(unsigned int aid, int accepted)
{
    Peer *p;
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    p = peer_by_aid(aid);
    if (p != NULL && p->state == PENDING) {
        if (accepted) {
            unsigned char welcome[8] = {(unsigned char)aid};
            p->state = MEMBER;
            send_kind(&p->address, WELCOME, welcome, sizeof(welcome));
            if (net.mods_seed_known) {
                send_seed(p);
            }
            send_missions(p);
            push(KHDAYS_NET_JOINED, aid, NULL, 0);
            khdays_log("net: member %u in", aid);
        } else {
            unsigned char refuse = KHDAYS_NET_REFUSE_JUDGE;
            send_kind(&p->address, REFUSE, &refuse, 1);
            free_peer(p);
        }
    }
    SDL_UnlockMutex(net.lock);
}

int khdays_net_send(unsigned int destinations, unsigned int port, const void *data, unsigned int size)
{
    int any = 0;
    if (!net.started || port >= PORTS) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    if (port == GAME_PORT) {
        /* count the battle's ticks (see net.battle_ticks) and the messages sent */
        unsigned int gates[GATES] = {0};
        census(gates, (const unsigned char *)data, size);
        if (gates[net.role == HOST ? 0 : 3] > 0) {
            ++net.battle_ticks;
        }
        census(census_sent, (const unsigned char *)data, size);
        if (net.role == GUEST) {
            /* keep the last control sent, for the prediction */
            const unsigned char *control = NULL;
            unsigned int length = 0;
            if (control_in((const unsigned char *)data, size, NULL, &control, &length) > 0 && control != NULL &&
                length <= sizeof(net.own_control)) {
                memcpy(net.own_control, control, length);
                net.own_control_size = length;
                send_stick();
            }
        }
        if (trace_messages()) {
            trace_record("sent to", destinations, (const unsigned char *)data, size);
        }
    }
    if (net.role == GUEST) {
        if (net.peers != NULL && net.peers[0].used) {
            queue_record(&net.peers[0], port, data, size);
            any = 1;
        }
    } else if (net.role == HOST) {
        for (int i = 0; i < net.peer_room; ++i) {
            Peer *p = &net.peers[i];
            if (p->used && p->state == MEMBER && p->aid < 32 && (destinations & (1u << p->aid))) {
                queue_record(p, port, data, size);
                any = 1;
            }
        }
    }
    SDL_UnlockMutex(net.lock);
    return any;
}

void khdays_net_leave(void)
{
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    leave_locked();
    SDL_UnlockMutex(net.lock);
}

int khdays_net_link_level(void)
{
    int level = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    for (int i = 0; i < net.peer_room; ++i) {
        const Peer *p = &net.peers[i];
        int this_one;
        if (!p->used || p->state != MEMBER) {
            continue;
        }
        /* Signal bars from the round trip and the loss. Measured with the
         * delay and loss knobs: a 60 ms round trip with 2%
         * lost plays as well as on one PC, a 300 ms one with 10% lost still
         * plays. A member that is still heard never shows zero bars: zero
         * means the connection is lost (member_silent). */
        this_one = p->rtt_ms < 100 ? 3 : p->rtt_ms < 200 ? 2 : 1;
        if (p->loss > 0.25 && this_one > 1) {
            --this_one;
        }
        /* like the DS, show the weakest member's */
        if (level == 0 || this_one < level) {
            level = this_one;
        }
    }
    SDL_UnlockMutex(net.lock);
    return level;
}

/* A guest's stick to the host, with a control packet: when it changed, or a fifth of a second after
 * the last (the caller holds net.lock). */
static void send_stick(void)
{
    const KhdaysNetStick *s = &net.own_stick;
    const u64 now = now_ns();
    unsigned char body[STICK_BODY];
    if (net.role != GUEST || net.peers == NULL || !net.peers[0].used ||
        (memcmp(s, &net.sent_stick, sizeof(*s)) == 0 && since(now, net.stick_sent) < 200 * MS)) {
        return;
    }
    put16(body, (unsigned int)(unsigned short)s->x);
    put16(body + 2, (unsigned int)(unsigned short)s->y);
    body[4] = s->run_threshold;
    body[5] = s->walk_speed;
    body[6] = s->on;
    send_kind(&net.peers[0].address, STICK, body, sizeof(body));
    net.sent_stick = *s;
    net.stick_sent = now;
}

void khdays_net_set_stick(const KhdaysNetStick *stick)
{
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    net.own_stick = *stick;
    SDL_UnlockMutex(net.lock);
}

int khdays_net_stick(unsigned int aid, KhdaysNetStick *out)
{
    int known = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    if (net.role == HOST) {
        const Peer *p = peer_by_aid(aid);
        if (p != NULL && p->state == MEMBER && p->stick_at != 0 && since(now_ns(), p->stick_at) < 1000 * MS) {
            *out = p->stick;
            known = 1;
        }
    }
    SDL_UnlockMutex(net.lock);
    return known;
}

void khdays_net_set_level(int level)
{
    net.own_level = level > 0 && level <= 0xffff ? level : 0;
}

/* One more known level for khdays_net_levels. */
static void add_level(unsigned int *aids, int *levels, int room, int *count, unsigned int aid, int level)
{
    if (level <= 0) {
        return;
    }
    if (*count < room) {
        aids[*count] = aid;
        levels[*count] = level;
    }
    ++*count;
}

int khdays_net_levels(unsigned int *aids, int *levels, int room)
{
    int count = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    if (net.role == HOST) {
        add_level(aids, levels, room, &count, 0, net.own_level);
        for (int i = 0; i < net.peer_room; ++i) {
            const Peer *p = &net.peers[i];
            if (p->used && p->state == MEMBER) {
                add_level(aids, levels, room, &count, p->aid, p->level);
            }
        }
    } else if (net.role == GUEST) {
        /* its own as it knows it, else as the host's pings give it back */
        const int own = net.own_level > 0                   ? net.own_level
                        : (int)net.own_aid < net.level_room ? net.levels[net.own_aid]
                                                            : 0;
        add_level(aids, levels, room, &count, net.own_aid, own);
        for (int aid = 0; aid < net.level_room; ++aid) {
            if (aid != (int)net.own_aid) {
                add_level(aids, levels, room, &count, (unsigned int)aid, net.levels[aid]);
            }
        }
    }
    SDL_UnlockMutex(net.lock);
    return count;
}

void khdays_net_set_stop_casts(const unsigned char most[3], const unsigned char level[3])
{
    unsigned char bytes[STOP_BYTES];
    if (most == NULL) {
        net.own_stop = 0;
        return;
    }
    memcpy(bytes, most, 3);
    memcpy(bytes + 3, level, 3);
    net.own_stop = get_stop(bytes);
}

int khdays_net_stop_casts(unsigned int aid, unsigned char most[3], unsigned char level[3])
{
    unsigned char bytes[STOP_BYTES];
    u64 stop = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    if ((net.role == HOST && aid == 0) || (net.role == GUEST && aid == net.own_aid)) {
        stop = net.own_stop;
    } else if (net.role == HOST) {
        const Peer *q = peer_by_aid(aid);
        stop = q != NULL && q->state == MEMBER ? q->stop : 0;
    } else if (net.role == GUEST && (int)aid < net.stop_room) {
        stop = net.stops[aid];
    }
    SDL_UnlockMutex(net.lock);
    if (stop == 0) {
        return 0;
    }
    put_stop(bytes, stop);
    memcpy(most, bytes, 3);
    memcpy(level, bytes + 3, 3);
    return 1;
}

/* --- the mods' records ------------------------------------------------- */

/* The room's seed to member `p` (the caller holds net.lock). */
static void send_seed(Peer *p)
{
    unsigned char record[9] = {MODS_SEED};
    put64(record + 1, net.mods_seed);
    queue_record(p, MODS_PORT, record, sizeof(record));
}

/* This copy's new missions to copy `p`, in pieces; an empty list as one empty last piece, so that `p` knows there
 * are none (the caller holds net.lock). */
static void send_missions(Peer *p)
{
    unsigned char record[2 + MODS_PIECE];
    unsigned int at = 0;
    mods_ready();
    SDL_LockMutex(mods_lock);
    do {
        const unsigned int piece = own_missions_size - at < MODS_PIECE ? own_missions_size - at : MODS_PIECE;
        record[0] = MODS_MISSIONS;
        record[1] = at + piece == own_missions_size;
        if (piece > 0) {
            memcpy(record + 2, own_missions + at, piece);
        }
        queue_record(p, MODS_PORT, record, 2 + piece);
        at += piece;
    } while (at < own_missions_size);
    SDL_UnlockMutex(mods_lock);
}

void khdays_net_set_missions(const unsigned char *list, unsigned int size)
{
    unsigned char *copy = size > 0 ? (unsigned char *)malloc(size) : NULL;
    if (copy != NULL) {
        memcpy(copy, list, size);
    }
    mods_ready();
    SDL_LockMutex(mods_lock);
    free(own_missions);
    own_missions = copy;
    own_missions_size = copy != NULL ? size : 0;
    SDL_UnlockMutex(mods_lock);
}

int khdays_net_room_missions(void (*each)(const unsigned char *list, unsigned int size, int known, void *context),
                             void *context)
{
    int role = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    if (net.role != NONE && members() > 0) {
        role = net.role == HOST ? 1 : 2;
        for (int i = 0; i < net.peer_room; ++i) {
            const Peer *p = &net.peers[i];
            if (p->used && p->state == MEMBER) {
                each(p->missions, p->missions_size, p->missions_known, context);
            }
        }
    }
    SDL_UnlockMutex(net.lock);
    return role;
}

unsigned int khdays_net_missions_version(void)
{
    unsigned int version;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    version = net.missions_version + (unsigned int)net.role * 0x10000u;
    SDL_UnlockMutex(net.lock);
    return version;
}

/* The room's seed and the shares waiting, forgotten (the caller holds net.lock). */
static void mods_forget(void)
{
    net.mods_seed_known = 0;
    net.share_size = 0;
    for (int i = 0; i < net.share_count; ++i) {
#pragma warning(suppress : 6001) /* every share below the count was written before the count moved */
        free(net.shares[i].data);
    }
    net.share_count = 0;
}

void khdays_net_set_seed(unsigned long long seed)
{
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    if (net.role == HOST) {
        net.mods_seed = seed;
        net.mods_seed_known = 1;
        for (int i = 0; i < net.peer_room; ++i) {
            if (net.peers[i].used && net.peers[i].state == MEMBER) {
                send_seed(&net.peers[i]);
            }
        }
    }
    SDL_UnlockMutex(net.lock);
}

int khdays_net_seed(unsigned long long *seed)
{
    int known;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    known = net.role != NONE && net.mods_seed_known;
    *seed = net.mods_seed;
    SDL_UnlockMutex(net.lock);
    return known;
}

int khdays_net_share(const void *data, unsigned int size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    unsigned char record[2 + MODS_PIECE];
    int any = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    for (int i = 0; net.role == HOST && i < net.peer_room; ++i) {
        Peer *p = &net.peers[i];
        unsigned int at = 0;
        if (!p->used || p->state != MEMBER) {
            continue;
        }
        do {
            const unsigned int piece = size - at < MODS_PIECE ? size - at : MODS_PIECE;
            record[0] = MODS_SHARE;
            record[1] = at + piece == size;
            memcpy(record + 2, bytes + at, piece);
            queue_record(p, MODS_PORT, record, 2 + piece);
            at += piece;
        } while (at < size);
        any = 1;
    }
    SDL_UnlockMutex(net.lock);
    return any;
}

int khdays_net_shared(unsigned char **data, unsigned int *size)
{
    int found = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    if (net.share_count > 0) {
        *data = net.shares[0].data;
        *size = net.shares[0].size;
        memmove(&net.shares[0], &net.shares[1], (size_t)(net.share_count - 1) * sizeof(*net.shares));
        --net.share_count;
        found = 1;
    }
    SDL_UnlockMutex(net.lock);
    return found;
}

int khdays_net_round_trips(unsigned int *aids, int *ms, int room)
{
    int count = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    for (int i = 0; i < net.peer_room; ++i) {
        const Peer *p = &net.peers[i];
        if (!p->used || p->state != MEMBER) {
            continue;
        }
        if (count < room) {
            aids[count] = p->aid;
            ms[count] = p->rtt_ms < 0 ? -1 : (int)(p->rtt_ms + 0.5);
        }
        ++count;
    }
    SDL_UnlockMutex(net.lock);
    return count;
}

/* The field's frame mode for a session of `players`. With three or more players the DS game slows the
 * field to a step every three frames (mode 1); the port keeps a step every two frames (mode 0) unless
 * KHDAYS_MP_DS_PACE=1 (prepare.py's join at Ov002_ConstructGameplayScene). */
int khdays_mp_field_mode(int players)
{
    const char *value = getenv("KHDAYS_MP_DS_PACE");
    const int ds = value != NULL && value[0] == '1';
    khdays_log("multiplayer: %d players: the field at %s", players,
               ds ? "the DS's pace for three or more (a step every three frames, KHDAYS_MP_DS_PACE)"
                  : "the pace of one or two (a step every two frames)");
    return ds ? 1 : 0;
}

void khdays_net_filter(unsigned int (*filter)(unsigned char *data, unsigned int size))
{
    if (net.lock != NULL) {
        SDL_LockMutex(net.lock);
    }
    net.filter = filter;
    if (net.lock != NULL) {
        SDL_UnlockMutex(net.lock);
    }
}

unsigned int khdays_net_own_control(unsigned char *out, unsigned int room)
{
    unsigned int size = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    if (net.role == GUEST && net.own_control_size > 0 && net.own_control_size <= room) {
        memcpy(out, net.own_control, net.own_control_size);
        size = net.own_control_size;
    }
    SDL_UnlockMutex(net.lock);
    return size;
}

int khdays_net_in_session(void)
{
    int up;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    up = net.role != NONE || net.joining;
    SDL_UnlockMutex(net.lock);
    return up;
}

int khdays_net_pending(void)
{
    int waiting;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    waiting = net.event_count > 0;
    SDL_UnlockMutex(net.lock);
    return waiting;
}

/* Callbacks the wireless layer owes the game (see net_session.h), in order. */
static KhdaysNetOwed *owed_items;
static int owed_count, owed_room;

void khdays_net_owe(const KhdaysNetOwed *owed)
{
    owed_items = (KhdaysNetOwed *)grow(owed_items, &owed_room, owed_count + 1, sizeof(*owed_items));
    owed_items[owed_count++] = *owed;
}

int khdays_net_owed_count(void)
{
    return owed_count;
}

int khdays_net_take_owed(KhdaysNetOwed *owed)
{
    if (owed_count == 0) {
        return 0;
    }
    *owed = owed_items[0];
    memmove(&owed_items[0], &owed_items[1], (size_t)(owed_count - 1) * sizeof(*owed_items));
    --owed_count;
    return 1;
}

void khdays_net_trace(const char *what, unsigned int a, unsigned int b)
{
    static int on = -1;
    if (on < 0) {
        const char *value = getenv("KHDAYS_TRACE_WM");
        on = value != NULL && value[0] == '1';
    }
    if (on) {
        khdays_log("wm: %s (0x%x, 0x%x)", what, a, b);
    }
}

static void join_waiting(KhdaysNetEvent *event, unsigned char *data, unsigned int room, int from);

/* Looks for a control packet (gate 3) in a record: 0 none, 1 one, 2 one carrying a command (its kind
 * in the low 4 bits of the second word, CHOSEN_NONE none). *only_control: the record holds nothing but
 * it; *payload and *length_out: its payload. */
enum { GATE_CONTROL = 3, CHOSEN_NONE = 7 };
static int control_in(const unsigned char *d, unsigned int size, int *only_control, const unsigned char **payload,
                      unsigned int *length_out)
{
    unsigned int at = 0, controls = 0, others = 0;
    int found = 0;
    while (at + 2 <= size) {
        unsigned int header, length;
        if (d[at] == 0) {
            ++at;
            continue;
        }
        header = (unsigned int)d[at] | (unsigned int)d[at + 1] << 8;
        length = header >> 6;
        if (((header >> 1) & 31u) == GATE_CONTROL) {
            const int command = length >= 8 && at + 2 + 8 <= size && (d[at + 2 + 4] & 15u) != CHOSEN_NONE;
            found = command ? 2 : (found > 0 ? found : 1);
            ++controls;
            if (payload != NULL && at + 2 + length <= size) {
                *payload = d + at + 2;
                *length_out = length;
            }
        } else {
            ++others;
        }
        at += 2 + length;
    }
    if (only_control != NULL) {
        *only_control = controls == 1 && others == 0;
    }
    return found;
}

/* Whether the host hands each member's controls at most one per battle tick
 * now: the rule is on, this copy is the host, and a battle is running (its
 * game ran a tick in the last three frames). */
static int one_control_now(void)
{
    return net.one_control && net.role == HOST && net.battle_ticks > 0 && net.frame_now - net.battle_tick_frame < 3;
}

/* How many control packets of `q`'s member wait on its port, counting `q`
 * and those after it. */
static unsigned int controls_queued(const Queued *q, int index)
{
    unsigned int n = 0;
    for (int j = index; j < net.event_count; ++j) {
        const Queued *r = &net.events[j];
        if (r->event.kind == KHDAYS_NET_RECORD && r->event.aid == q->event.aid && r->event.port == q->event.port &&
            control_in(r->data, r->event.size, NULL, NULL, NULL) > 0) {
            ++n;
        }
    }
    return n;
}

/* Whether record `q` (at `index`) only repeats a held state: it is a lone
 * control packet with no command, and the member's next record is the same
 * control packet apart from its first halfword (same buttons, same target).
 * Such a record can be dropped without losing anything. */
static int held_repeat(const Queued *q, int index)
{
    int only = 0;
    const unsigned char *mine = NULL;
    unsigned int length = 0;
    if (control_in(q->data, q->event.size, &only, &mine, &length) != 1 || !only || mine == NULL || length <= 2) {
        return 0;
    }
    for (int j = index + 1; j < net.event_count; ++j) {
        const Queued *later = &net.events[j];
        const unsigned char *theirs = NULL;
        unsigned int their_length = 0;
        if (later->event.kind != KHDAYS_NET_RECORD || later->event.aid != q->event.aid ||
            later->event.port != q->event.port) {
            continue;
        }
        return control_in(later->data, later->event.size, NULL, &theirs, &their_length) > 0 && theirs != NULL &&
               their_length == length && memcmp(mine + 2, theirs + 2, length - 2) == 0;
    }
    return 0;
}

/* The longest gap between two of member `p`'s control packets in the last
 * two ten-second windows, in battle ticks (at least 1). */
static unsigned int gap_cover(const Peer *p)
{
    const unsigned int most = p->gap_most > p->gap_most_before ? p->gap_most : p->gap_most_before;
    return most > 1u ? most : 1u;
}

/* Host only: a record of member `p` arrived in order on `port`. If it holds a
 * control packet, note the gap since the previous one. */
static void control_came(Peer *p, unsigned int port, const unsigned char *data, unsigned int size)
{
    if (net.role != HOST || port != GAME_PORT || control_in(data, size, NULL, NULL, NULL) <= 0) {
        return;
    }
    if (p->control_came_seen) {
        const u64 gap = net.battle_ticks - p->control_came;
        p->gap_most = gap > p->gap_most ? (unsigned int)gap : p->gap_most;
    }
    p->control_came = net.battle_ticks;
    p->control_came_seen = 1;
}

/* What to do with member `p`'s record `q`. */
enum { HAND_OVER, WAIT, DROP };

/* Host only: whether record `q` of member `p` goes to the game now. In a battle a member's controls go
 * one per tick from the playout store, which fills to its depth first, deepens by one each tick it runs
 * dry, drops one repeated control after a whole second too full, and shrinks after ten seconds without
 * running dry (docs/MULTIPLAYER.md, "The playout store"). */
static int control_waits(Peer *p, const Queued *q, int index)
{
    const int control = control_in(q->data, q->event.size, NULL, NULL, NULL);
    unsigned int queued;
    if (control == 0) {
        return HAND_OVER;
    }
    if (!one_control_now()) {
        /* no battle: hand them over as they come; the next battle fills the store again */
        p->play_started = 0;
        p->play_filling = 1;
        return HAND_OVER;
    }
    if (p->control_tick == net.battle_ticks + 1) {
        return WAIT; /* one was already handed for this tick */
    }
    if (p->play_depth == 0) {
        p->play_depth = 1;
        p->play_filling = 1;
    }
    queued = controls_queued(q, index);
    if (p->play_filling) {
        if (queued <= p->play_depth && net.battle_ticks - q->arrived <= p->play_depth) {
            /* still filling: wait until `play_depth` more controls are in store
             * (or, if no more come, until this one has waited as many ticks) */
            return WAIT;
        }
        p->play_filling = 0;
        p->play_started = 1;
        p->play_window = 0;
        p->play_least = queued;
    }
    if (p->play_trim && held_repeat(q, index)) {
        p->play_trim = 0;
        ++p->play_trimmed;
        return DROP;
    }
    return HAND_OVER;
}

/* Host only: the game just ran a battle tick. Updates each member's playout
 * store: it ran dry if no control was handed for the tick that ended. */
static void playout_tick(void)
{
    if (net.role != HOST || !net.one_control) {
        return;
    }
    for (int i = 0; i < net.peer_room; ++i) {
        Peer *p = &net.peers[i];
        unsigned int queued = 0;
        if (!p->used || p->state != MEMBER) {
            continue;
        }
        if (++p->gap_window >= 300u) {
            /* every ten seconds (300 ticks), a new window for the gaps */
            p->gap_most_before = p->gap_most;
            p->gap_most = 0;
            p->gap_window = 0;
            if (p->play_depth > gap_cover(p)) {
                p->play_depth = gap_cover(p); /* no deeper than the last 20 s's longest gap */
            }
        }
        if (!p->play_started || p->play_filling) {
            continue;
        }
        if (p->control_tick < net.battle_ticks) {
            ++p->play_dry;
            if (p->play_depth < gap_cover(p)) {
                ++p->play_depth;
            }
            p->play_filling = 1;
            p->play_calm = 0;
            continue;
        }
        for (int j = 0; j < net.event_count; ++j) {
            const Queued *r = &net.events[j];
            if (r->event.kind == KHDAYS_NET_RECORD && r->event.aid == p->aid && r->event.port == GAME_PORT &&
                control_in(r->data, r->event.size, NULL, NULL, NULL) > 0) {
                ++queued;
            }
        }
        p->play_least = queued < p->play_least ? queued : p->play_least;
        if (++p->play_window >= 30u) {
            /* every second (30 ticks): too full if it never went down to its depth */
            p->play_trim = p->play_least > p->play_depth;
            p->play_window = 0;
            p->play_least = ~0u;
        }
        if (++p->play_calm >= 300u && p->play_depth > 1u) {
            /* ten seconds without running dry: keep one less */
            --p->play_depth;
            p->play_calm = 0;
        }
    }
}

/* Hands the game the next event (see net_session.h). A record waits when its
 * port already had a delivery this frame, when an earlier record of its port
 * waits, or when the playout store holds it back. */
int khdays_net_next(KhdaysNetEvent *event, void *data, unsigned int room, unsigned int frame)
{
    int found = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    ++net.call;
    net.frame_now = frame;
    if (net.battle_ticks != net.battle_ticks_seen) {
        net.battle_ticks_seen = net.battle_ticks;
        net.battle_tick_frame = frame; /* the game ran a battle tick this frame */
        playout_tick();
    }
    for (int i = 0; i < net.event_count; ++i) {
        Queued *q = &net.events[i];
        if (q->event.kind == KHDAYS_NET_RECORD) {
            Peer *p = net.role == GUEST ? (net.peers != NULL && net.peers[0].used ? &net.peers[0] : NULL)
                                        : peer_by_aid(q->event.aid);
            const unsigned int port = q->event.port;
            if (p != NULL) {
                if (p->blocked[port] == net.call) {
                    continue; /* an earlier record of this port waits, so this one waits too */
                }
                if (port == GAME_PORT && net.role == HOST) {
                    const int decision = control_waits(p, q, i);
                    if (decision == DROP) {
                        ++p->controls_dropped;
                        free(q->data);
                        memmove(&net.events[i], &net.events[i + 1], (size_t)(net.event_count - i - 1) * sizeof(Queued));
                        --net.event_count;
                        --i;
                        continue;
                    }
                    if (decision == WAIT) {
                        if (!q->held) {
                            q->held = 1;
                            ++p->controls_held;
                        }
                        p->blocked[port] = net.call; /* and the records behind it wait too */
                        continue;
                    }
                }
                if (p->delivered[port] && p->delivered_frame[port] == frame) {
                    p->blocked[port] = net.call;
                    continue;
                }
                p->delivered[port] = 1;
                p->delivered_frame[port] = frame;
            }
        }
        *event = q->event;
        event->size = q->event.size < room ? q->event.size : room;
        if (event->size > 0) {
            memcpy(data, q->data, event->size);
        }
        free(q->data);
        memmove(&net.events[i], &net.events[i + 1], (size_t)(net.event_count - i - 1) * sizeof(Queued));
        --net.event_count;
        if (event->kind == KHDAYS_NET_RECORD && net.join_up_to[event->port] > 0) {
            join_waiting(event, (unsigned char *)data, room, i);
        }
        if (event->kind == KHDAYS_NET_RECORD && event->port == GAME_PORT && net.role == GUEST && net.filter != NULL) {
            event->size = net.filter((unsigned char *)data, event->size);
            if (event->size == 0) {
                --i; /* the filter left nothing: look at the next event instead */
                continue;
            }
        }
        if (event->kind == KHDAYS_NET_RECORD && event->port == GAME_PORT) {
            Peer *m = net.role == HOST ? peer_by_aid(event->aid) : NULL;
            census(census_got, (const unsigned char *)data, event->size);
            if (trace_messages()) {
                trace_record("handed from", event->aid, (const unsigned char *)data, event->size);
            }
            if (m != NULL) {
                census(m->census_got, (const unsigned char *)data, event->size);
                if (control_in((const unsigned char *)data, event->size, NULL, NULL, NULL) > 0) {
                    m->control_tick = net.battle_ticks + 1;
                }
            }
        }
        found = 1;
        break;
    }
    SDL_UnlockMutex(net.lock);
    return found;
}

/* Joins to `event` the records of the same member and port that wait behind
 * it (from queue index `from` on), in order, while they fit. */
static void join_waiting(KhdaysNetEvent *event, unsigned char *data, unsigned int room, int from)
{
    const unsigned int limit = net.join_up_to[event->port] < room ? net.join_up_to[event->port] : room;
    for (int j = from; j < net.event_count; ++j) {
        Queued *q = &net.events[j];
        if (q->event.kind != KHDAYS_NET_RECORD || q->event.aid != event->aid || q->event.port != event->port) {
            continue;
        }
        if (event->size + q->event.size > limit) {
            return; /* the next one does not fit: it waits, and so do those after it */
        }
        if (event->port == GAME_PORT && one_control_now() && control_in(data, event->size, NULL, NULL, NULL) > 0 &&
            control_in(q->data, q->event.size, NULL, NULL, NULL) > 0) {
            return; /* a second control packet belongs to the next tick */
        }
        memcpy(data + event->size, q->data, q->event.size);
        event->size += q->event.size;
        free(q->data);
        memmove(&net.events[j], &net.events[j + 1], (size_t)(net.event_count - j - 1) * sizeof(Queued));
        --net.event_count;
        --j;
    }
}

void khdays_net_join_records(unsigned int port, unsigned int up_to)
{
    if (port >= PORTS) {
        return;
    }
    if (net.lock != NULL) {
        SDL_LockMutex(net.lock);
    }
    net.join_up_to[port] = up_to;
    if (net.lock != NULL) {
        SDL_UnlockMutex(net.lock);
    }
}

/* --- the port's list of rooms ------------------------------------------- */

void khdays_net_room_details(const char *name, const char *password)
{
    const size_t size = name != NULL ? strlen(name) : 0;
    char *copy = (char *)malloc(size + 1);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, size > 0 ? name : "", size + 1);
    if (net.started) {
        SDL_LockMutex(net.lock);
    }
    free(net.room_name);
    net.room_name = copy;
    net.room_name_size = (unsigned int)size;
    net.locked = password != NULL && password[0] != '\0';
    if (net.locked) {
        khdays_sha1((const unsigned char *)password, (unsigned int)strlen(password), net.pass_digest);
    }
    if (net.started) {
        SDL_UnlockMutex(net.lock);
    }
}

void khdays_net_join_password(const char *password)
{
    if (net.started) {
        SDL_LockMutex(net.lock);
    }
    net.join_password = password != NULL && password[0] != '\0';
    if (net.join_password) {
        khdays_sha1((const unsigned char *)password, (unsigned int)strlen(password), net.join_digest);
    }
    if (net.started) {
        SDL_UnlockMutex(net.lock);
    }
}

void khdays_net_scan_only(const unsigned char *id)
{
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    net.only = id != NULL;
    if (id != NULL) {
        memcpy(net.only_id, id, ID_SIZE);
        if (net.scanning) {
            Room *r = room_by_id(id);
            if (r != NULL && r->ggid == net.scan_ggid && r->build == net.build) {
                report_room(r); /* the game hears of it at once */
            }
        }
    }
    SDL_UnlockMutex(net.lock);
}

/* Whether the game would take room `r` (its group and build). */
static int listed(const Room *r)
{
    return r->ggid == net.scan_ggid && r->build == net.build;
}

int khdays_net_room_count(void)
{
    int count = 0;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    for (int i = 0; i < net.room_count; ++i) {
        count += listed(&net.rooms[i]);
    }
    SDL_UnlockMutex(net.lock);
    return count;
}

int khdays_net_room_at(int index, KhdaysNetRoomView *out, char *name, unsigned int name_room)
{
    int found = -1;
    if (!net.started) {
        return -1;
    }
    SDL_LockMutex(net.lock);
    for (int i = 0, n = 0; i < net.room_count && found < 0; ++i) {
        const Room *r = &net.rooms[i];
        if (!listed(r) || n++ != index) {
            continue;
        }
        memset(out, 0, sizeof(*out));
        memcpy(out->id, r->id, ID_SIZE);
        out->open = r->open;
        out->local = r->local;
        out->locked = r->details && r->locked;
        out->players = r->players;
        out->most = r->details ? r->most : 0;
        /* the round trip a session with it would take: on the local network straight to the host,
         * online through the server (ours to it and the host's) */
        out->ping_ms = r->local ? (r->probe_ms < 0 ? -1 : (int)(r->probe_ms + 0.5))
                       : r->details && r->host_server_ms >= 0 && net.server_ms >= 0
                           ? r->host_server_ms + (int)(net.server_ms + 0.5)
                           : -1;
        /* the host's name as the game shows it: the start of the game's information
         * (WMGameInfo's userGameInfo, UTF-16), up to its end */
        for (unsigned int k = 0; k + 1 < sizeof(out->host) / sizeof(out->host[0]) && 16 + 2 * k + 1 < r->info_size;
             ++k) {
            out->host[k] = (unsigned short)(r->info[16 + 2 * k] | r->info[17 + 2 * k] << 8);
            if (out->host[k] == 0) {
                break;
            }
        }
        if (name != NULL && name_room > 0) {
            const unsigned int n_copy = r->name_size < name_room - 1 ? r->name_size : name_room - 1;
            memcpy(name, r->name, n_copy);
            name[n_copy] = '\0';
        }
        found = r->details ? (int)r->name_size : 0;
    }
    SDL_UnlockMutex(net.lock);
    return found;
}

int khdays_net_last_refusal(void)
{
    return (int)net.last_refusal;
}

void khdays_net_last_refusal_mods(char *text, int room, int *more)
{
    text[0] = 0;
    *more = 0;
    if (!net.started) {
        return;
    }
    SDL_LockMutex(net.lock);
    if (net.refusal_mods != NULL) {
        SDL_strlcpy(text, net.refusal_mods, (size_t)room);
        *more = net.refusal_more;
    }
    SDL_UnlockMutex(net.lock);
}

int khdays_net_last_refusal_stop_off(void)
{
    int off;
    if (!net.started) {
        return 0;
    }
    SDL_LockMutex(net.lock);
    off = net.refusal_stop_off;
    SDL_UnlockMutex(net.lock);
    return off;
}

int khdays_net_own_aid(void)
{
    int aid;
    if (!net.started) {
        return -1;
    }
    SDL_LockMutex(net.lock);
    aid = net.role == HOST ? 0 : net.role == GUEST ? (int)net.own_aid : -1;
    SDL_UnlockMutex(net.lock);
    return aid;
}
