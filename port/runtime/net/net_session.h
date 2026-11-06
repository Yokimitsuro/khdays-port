/* The port's multiplayer session: rooms on the local network and through khdays-server, a star around
 * the host, every record delivered once and in order, a thread keeping the session alive while the
 * game is busy. The port's wireless layer (hal/sdk_wireless.c) uses it; it takes plain values and its
 * events hold no pointer, so both sides read them the same way. */
#ifndef KHDAYS_NET_SESSION_H
#define KHDAYS_NET_SESSION_H

enum {
    KHDAYS_NET_ROOM = 1, /* a room seen (or seen again): id; data = its game information */
    KHDAYS_NET_JOINING,  /* host: a guest asks to join: the aid it would get, id; data = its SSID bytes */
    KHDAYS_NET_JOINED,   /* host: member `aid` joined. guest: we joined, as `aid` */
    KHDAYS_NET_REFUSED,  /* guest: our join failed: reason */
    KHDAYS_NET_RECORD,   /* a record from `aid` on `port`: data */
    KHDAYS_NET_LEFT      /* host: member `aid` is gone. guest: the host is gone (aid 0) */
};

enum {
    KHDAYS_NET_REFUSE_FULL = 1, /* the room is full */
    KHDAYS_NET_REFUSE_CLOSED,   /* it does not take anyone now */
    KHDAYS_NET_REFUSE_JUDGE,    /* the host's game said no */
    KHDAYS_NET_REFUSE_BUILD,    /* another build of the port */
    KHDAYS_NET_REFUSE_SILENT,   /* no answer */
    KHDAYS_NET_REFUSE_PASSWORD, /* the room's password not proven */
    KHDAYS_NET_REFUSE_MODS      /* other gameplay mods than the host's (khdays_net_last_refusal_mods) */
};

typedef struct KhdaysNetEvent {
    unsigned int kind;
    unsigned int aid;
    unsigned int port;
    unsigned int reason;
    unsigned int size;   /* how many bytes khdays_net_next wrote */
    unsigned char id[8]; /* a room's or member's network id (6 bytes used) */
} KhdaysNetEvent;

/* Where khdays_net_begin gets the server's address: `server` fills it in and
 * says whether to use it (the player's settings, host.h
 * khdays_settings_server). It is not asked when there is no source or when
 * KHDAYS_NET_SERVER is set (empty means no server). */
void khdays_net_settings_source(int (*server)(char *text, int size));

/* Starts the network and its thread (again after khdays_net_end). Returns 1
 * on success, 0 without a network (the reason goes to the log). */
int khdays_net_begin(void);

/* Leaves every room and membership (saying goodbye) and drops the events. */
void khdays_net_end(void);

/* This copy's network id (6 random bytes, a locally administered address). */
void khdays_net_id(unsigned char *id);

/* Opens a room of ours: `ggid` is the game's group id, `info` the game
 * information guests see, `max_members` how many guests it takes. Returns 1
 * once announced. */
int khdays_net_host(unsigned int ggid, const void *info, unsigned int size, unsigned int max_members);

/* Opens or closes the room to new guests. */
void khdays_net_entry(int open);

/* While `on`, reports the rooms of `ggid` (KHDAYS_NET_ROOM). */
void khdays_net_scan(unsigned int ggid, int on);

/* Asks to join room `id`, with the guest's SSID bytes. Returns 1 when sent. */
int khdays_net_join(const unsigned char *id, const void *ssid, unsigned int size);

/* The host's answer to a KHDAYS_NET_JOINING. */
void khdays_net_accept(unsigned int aid, int accepted);

/* Sends `size` bytes on `port` to the members in `destinations` (one bit per
 * aid; a guest always sends to the host, whatever the bits). It is queued at
 * once and delivered by the session. Returns 1 if anyone is there to get it. */
int khdays_net_send(unsigned int destinations, unsigned int port, const void *data, unsigned int size);

/* Leaves the room or membership, saying goodbye. */
void khdays_net_leave(void);

/* The connection's quality, 0 to 3; 0 when nobody is connected. */
int khdays_net_link_level(void);

/* The round trip to each member (on the host its guests, on a guest the host), smoothed, for the ping
 * shown in the window: up to `room` aids and round trips in milliseconds (-1 until measured). Returns
 * how many members there are. */
int khdays_net_round_trips(unsigned int *aids, int *ms, int room);

/* A player's movement stick for analog movement, which the DS's control packet has no room for: its
 * position (-32767..32767, y up, 0 0 at rest), the player's run threshold and walk speed (percent)
 * and whether analog movement is on in its settings. */
typedef struct KhdaysNetStick {
    short x, y;
    unsigned char run_threshold, walk_speed, on;
} KhdaysNetStick;
/* The game's thread, on a guest: this copy's stick now, sent to the host with its controls. */
void khdays_net_set_stick(const KhdaysNetStick *stick);
/* On the host: member `aid`'s stick as it last sent it (a second at most ago); 0 when none. */
int khdays_net_stick(unsigned int aid, KhdaysNetStick *out);

/* The game's thread: this copy's level (the game's own, 0 not known), which its pings carry to the
 * room for the members' screen. */
void khdays_net_set_level(int level);
/* The members' levels known, this copy's too (the host's pings give a guest everyone's): up to `room`
 * aids and levels. Returns how many are known. */
int khdays_net_levels(unsigned int *aids, int *levels, int room);
/* The game's thread: this copy's Stop casts (the most of each of its three levels and each one's Magic LV, from
 * its deck; `most` NULL: none known), which its pings carry to the room; and member `aid`'s as the room knows
 * them (the host's pings give a guest everyone's): 1 with `most` and `level` set when known. */
void khdays_net_set_stop_casts(const unsigned char most[3], const unsigned char level[3]);
int khdays_net_stop_casts(unsigned int aid, unsigned char most[3], unsigned char level[3]);

/* The mods' records (runtime/scripts), never the game's. The room's seed: chosen as the room opens,
 * changed by the host (khdays_net_set_seed; a guest's call does nothing), sent to each member, as it
 * joins too. khdays_net_seed gives it, 0 when there is none (not in a room, or not come yet). */
void khdays_net_set_seed(unsigned long long seed);
int khdays_net_seed(unsigned long long *seed);
/* The host: `size` bytes the mods share, to every member (in pieces, in order; any size). Returns 1 if
 * anyone is there to get them. A guest: the next message the host shared, whole, in a block the caller
 * frees (free). Returns 0 when none waits. */
int khdays_net_share(const void *data, unsigned int size);
int khdays_net_shared(unsigned char **data, unsigned int *size);

/* The new missions this copy brings to a room (runtime/missions/room.h's list; copied), set as the game starts: a
 * guest's sent to the host as it is welcomed, the host's to each member as it joins. */
void khdays_net_set_missions(const unsigned char *list, unsigned int size);
/* The lists of the room's other copies, each handed to `each` with the session's lock held (`each` only reads
 * them): a host's members', a guest's host's; `known` 0 while a copy's has not come (an older port never sends
 * one). Returns 1 on a host, 2 on a guest, 0 out of a room. */
int khdays_net_room_missions(void (*each)(const unsigned char *list, unsigned int size, int known, void *context),
                             void *context);
/* Another value each time a list comes, a copy joins or leaves, or this copy's role changes. */
unsigned int khdays_net_missions_version(void);

/* Whether we are in a room or joining one (the settings menu does not pause the game then). */
int khdays_net_in_session(void);

/* Guest only: the last control packet the game sent (gate 3, Ov022_SendControlPacket) into `out`.
 * Returns its size, 0 if none or not a guest (mp_predict.c). */
unsigned int khdays_net_own_control(unsigned char *out, unsigned int room);

/* Guest only: a filter for each record from the host on the game's port, applied before the game gets
 * it: it edits the record in place and returns its new size (mp_predict.c removes the host's copies
 * of the guest's own shots). NULL for none. */
void khdays_net_filter(unsigned int (*filter)(unsigned char *data, unsigned int size));

/* Whether an event is waiting. */
int khdays_net_pending(void);

/* Takes the next event that can be delivered in game frame `frame`, its data (at most `room` bytes)
 * into `data`. Returns 1, or 0 when there is none. A member's record waits for the next frame if its
 * port already had one this frame (the game keeps one record per member and frame); on a joined port
 * (khdays_net_join_records) the records waiting behind it come along, as many as fit. */
int khdays_net_next(KhdaysNetEvent *event, void *data, unsigned int room, unsigned int frame);

/* Lets a member's waiting records on `port` reach the game together, up to `up_to` bytes (0: one at a
 * time), for a port whose receiver reads a record as a series of messages: a delay the network caused
 * is not kept. Lasts until khdays_net_end. */
void khdays_net_join_records(unsigned int port, unsigned int up_to);

/* A callback the wireless layer owes the game at its next delivery (a send's
 * completion). Kept here in a growing queue, as the game's own references and
 * numbers (no port pointer). */
typedef struct KhdaysNetOwed {
    unsigned int kind;
    unsigned int callback;
    unsigned int arg;
    unsigned int data;
    unsigned int length;
    unsigned int port;
    unsigned int dest;
    unsigned int apiid;
} KhdaysNetOwed;

void khdays_net_owe(const KhdaysNetOwed *owed);

/* How many callbacks are owed; and the oldest one taken (returns 1), or 0
 * when there is none. */
int khdays_net_owed_count(void);
int khdays_net_take_owed(KhdaysNetOwed *owed);

/* With KHDAYS_TRACE_WM=1, writes a line about the wireless layer to the
 * session log. */
void khdays_net_trace(const char *what, unsigned int a, unsigned int b);

/* --- the port's list of rooms (runtime/net/roomlist.c) ---------------------- */

/* The room this copy hosts next: its name (UTF-8; cut to what an announcement carries) and its
 * password (NULL or empty: none). A guest proves it to join; it never travels itself. */
void khdays_net_room_details(const char *name, const char *password);

/* The password the next joins prove (NULL or empty: none). */
void khdays_net_join_password(const char *password);

/* While scanning, the game is told of room `id` only (the one the port's list chose), or of
 * every room (NULL). */
void khdays_net_scan_only(const unsigned char *id);

typedef struct KhdaysNetRoomView {
    unsigned char id[8];     /* its network id (6 bytes used) */
    int open;                /* it takes guests now */
    int local;               /* seen on the local network */
    int locked;              /* it asks for a password */
    int players, most;       /* in it now, the host too; the most it takes (0: not said) */
    int ping_ms;             /* the round trip a session with it would take: on the local network
                            * straight to the host, online through the server; -1 not known */
    unsigned short host[16]; /* the host's name as the game shows it (UTF-16, ended) */
} KhdaysNetRoomView;

/* This copy's number in its session: 0 the host, a guest's own, -1 none. */
int khdays_net_own_aid(void);

/* Why the last join was refused (KHDAYS_NET_REFUSE_*), 0 when it was not. */
int khdays_net_last_refusal(void);

/* The gameplay mods this copy plays with (runtime/scripts): their fingerprint, sent when joining and
 * compared by a host (0: none), and their names and versions, each a u8 size and its UTF-8 bytes,
 * name then version, `count` of them, told to a guest the host refuses. Any thread; the strings are
 * copied. */
void khdays_net_set_mods(unsigned int fingerprint, const unsigned char *list, unsigned int size, unsigned int count);

/* Whether this copy plays with the Stop magic ([gameplay] stop_magic, set before any session): rooms
 * take only copies that agree, its being off counting as a gameplay mod in the fingerprint. */
void khdays_net_set_stop_magic(int on);

/* After a KHDAYS_NET_REFUSE_MODS: the host's gameplay mods as "name version, name version" (cut to
 * `room` bytes, ended), and how many of them the host's answer could not hold; and whether that host
 * plays without the Stop magic (not in the text: the guest says it in its own language). */
void khdays_net_last_refusal_mods(char *text, int room, int *more);
int khdays_net_last_refusal_stop_off(void);

/* How many rooms of the group being scanned are known now. */
int khdays_net_room_count(void);

/* Room `index` (0 to the count): its view, and its name (UTF-8, ended) in `name`, cut to
 * `name_room` bytes. Returns the name's whole size in bytes (0 for a room without one), or -1
 * when there is no such room. */
int khdays_net_room_at(int index, KhdaysNetRoomView *out, char *name, unsigned int name_room);

#endif
