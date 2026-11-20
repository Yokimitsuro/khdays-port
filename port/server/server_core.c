/* The server's logic: what it keeps (groups, pairs and per-game indexes, in slabs found by hash
 * tables, chained by when they were last heard from) and how it answers each datagram; server.c owns
 * the socket and the loop. Past the memory budget the oldest record goes, and silent ones are
 * forgotten (docs/MULTIPLAYER.md, "khdays-server"). */
#include "server_core.h"
#include "../runtime/core/bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long long u64;

/* The datagram header and the message kinds
 * the server reads or writes. */
#define MAGIC 0x3144484bu /* "KHD1" */
#define HEADER 16
#define VERSION 1
#define ID_SIZE 6
enum { GONE = 2, REGISTER = 11, REGISTERED, LIST, ROOMS, CONNECT, PEER, PUNCH, RELAY, UNREGISTER };

/* The largest datagram the server sends. It is also the size that crosses the
 * internet without being split (RFC 9000, section 14). An answer is never
 * bigger than the question: a LIST arrives padded to this size, and a CONNECT
 * is as big as its answer, so a forged sender cannot use the server to send
 * someone more bytes than it sent itself. */
#define DATAGRAM 1200
#define CONNECT_BODY 16
/* The largest room announcement a ROOMS answer can hold (after its header and
 * one entry's fields). A bigger one could never be listed, so it is refused.
 * The game's are at most 148 bytes (hal/sdk_wireless.c). */
#define BODY_MOST (DATAGRAM - HEADER - 6 - 14)

/* How long silence lasts before a record is forgotten. A host registers every
 * 2 s and a session pings every 250 ms (runtime/net/net_session.c). */
static const u64 GROUP_SILENT_MS = 10000;
static const u64 PAIR_SILENT_MS = 30000;
/* A group stays with its host's address. Another sender using the same group
 * id can only take it over after the host has been silent this long. */
static const u64 GROUP_MOVES_AFTER_MS = 5000;
/* A joining guest asks every 250 ms until it is introduced (net_session.c,
 * JOIN_EVERY); a host is not introduced to new guests more often than that. */
static const u64 INTRODUCE_EVERY_MS = 250;
/* How many datagrams a pair may relay each way: this many per second, and up
 * to this many in a burst. It is four times the busiest second measured
 * (98 a second at 150 ms with 10% lost), leaving room
 * for busier missions and for the burst after a stall. */
#define RELAY_PER_SECOND (4 * 98.0)
#define RELAY_BURST RELAY_PER_SECOND

/* Links and table cells store "slot + 1", so that 0 can mean "none". */
#define NONE 0u

/* A room a host has registered. */
typedef struct Group {
    int used;
    unsigned char id[ID_SIZE];
    KhdaysNetAddress address; /* the host's public address */
    unsigned int ggid, build; /* the game and the port build it belongs to */
    unsigned char *body;      /* the room's announcement, as the host sent it */
    unsigned int body_size;
    u64 seen;                /* when the host last registered it */
    u64 introduced;          /* when a guest was last introduced to it */
    unsigned int index;      /* the slot of its game's index */
    unsigned int index_pos;  /* its position in that index */
    unsigned int prev, next; /* the "last heard from" chain (slot + 1) */
    u64 bytes;               /* what it costs from the memory budget */
} Group;

/* A guest introduced to a host. */
typedef struct Pair {
    int used;
    unsigned int token; /* names the pair in RELAY datagrams */
    unsigned char group_id[ID_SIZE], guest_id[ID_SIZE];
    KhdaysNetAddress guest, host;
    u64 heard; /* when either side was last heard from */
    int relayed;
    /* The relay allowance, one per direction (0: from the guest, 1: from the
     * host): datagrams left, and when it was last refilled. */
    double allowance[2];
    u64 refilled[2];
    /* For the status counts: datagrams relayed this second, and when the
     * second began. */
    unsigned int this_second[2];
    u64 second_began[2];
    unsigned int prev, next; /* the "last heard from" chain (slot + 1) */
} Pair;

/* The groups of one game and build, in the order LIST pages them. */
typedef struct Index {
    int used;
    unsigned int ggid, build;
    unsigned int *slots; /* group slots + 1 */
    unsigned int count, room;
} Index;

/* A hash table of slots + 1, with linear probing. A cell is NONE when empty
 * and REMOVED when its record was dropped (so later probes keep going). */
#define REMOVED 0xffffffffu
typedef struct Table {
    unsigned int *cell;
    unsigned int size, used, removed;
} Table;

/* A growing array whose slots never move, and the free slots to reuse. */
typedef struct Slab {
    void *items;
    unsigned int count, room; /* slots made, slots allocated */
    unsigned int *free_list;
    unsigned int free_count, free_room;
} Slab;

static struct {
    KhdaysUdp *udp;
    unsigned char id[ID_SIZE]; /* the server's own id, in every datagram it sends */
    int relay;                 /* whether it relays at all */
    u64 budget, bytes;         /* the memory allowed, and in use */
    Slab groups, pairs, indexes;
    Table group_by_id, pair_by_token, pair_by_guest, index_by_game;
    /* the two "last heard from" chains: their ends (slot + 1) */
    unsigned int group_oldest, group_newest, pair_oldest, pair_newest;
    KhdaysServerCounts counts;
} server;

#define GROUP(slot) (((Group *)server.groups.items)[slot])
#define PAIR(slot) (((Pair *)server.pairs.items)[slot])
#define INDEX(slot) (((Index *)server.indexes.items)[slot])

/* --- helpers ------------------------------------------------------------- */

/* Makes room for `need` items of `item` bytes, doubling, the new part zeroed.
 * Out of memory, the server stops. */
static void *grow(void *items, unsigned int *room, unsigned int need, size_t item)
{
    unsigned int bigger;
    void *more;
    if (need <= *room) {
        return items;
    }
    bigger = *room > 0 ? *room : 8;
    while (bigger < need) {
        bigger *= 2;
    }
    more = realloc(items, (size_t)bigger * item);
    if (more == NULL) {
        fprintf(stderr, "khdays-server: out of memory\n");
        exit(1);
    }
    memset((char *)more + (size_t)*room * item, 0, (size_t)(bigger - *room) * item);
    *room = bigger;
    return more;
}

/* Little-endian reads and writes. */
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

static int same_address(const KhdaysNetAddress *x, const KhdaysNetAddress *y)
{
    return x->ip == y->ip && x->port == y->port;
}

/* Milliseconds from `then` to `now` (0 if the clock went back). */
static u64 since(u64 now, u64 then)
{
    return now > then ? now - then : 0;
}

/* The FNV-1a hash of `n` bytes, continuing from `h`. */
static unsigned int fnv1a(const unsigned char *p, size_t n, unsigned int h)
{
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

/* Sends a datagram of `kind` with a `size`-byte body to `to`. */
static void send_kind(const KhdaysNetAddress *to, int kind, const unsigned char *body, unsigned int size)
{
    unsigned char bytes[HEADER + DATAGRAM];
    if (size > DATAGRAM) {
        return;
    }
    put32(bytes, MAGIC);
    bytes[4] = (unsigned char)kind;
    bytes[5] = VERSION;
    put16(bytes + 6, size);
    memcpy(bytes + 8, server.id, ID_SIZE);
    put16(bytes + 14, 0);
    if (size > 0) {
        memcpy(bytes + HEADER, body, size);
    }
    khdays_host_udp_send(server.udp, to, bytes, HEADER + size);
}

/* --- slabs and hash tables ----------------------------------------------- */

/* A zeroed slot: a free one if there is one, else a new one. */
static unsigned int slab_take(Slab *slab, size_t item)
{
    unsigned int slot;
    if (slab->free_count > 0) {
        slot = slab->free_list[--slab->free_count];
    } else {
        slab->items = grow(slab->items, &slab->room, slab->count + 1, item);
        slot = slab->count++;
    }
    memset((char *)slab->items + (size_t)slot * item, 0, item);
    return slot;
}

static void slab_give_back(Slab *slab, unsigned int slot)
{
    slab->free_list =
        (unsigned int *)grow(slab->free_list, &slab->free_room, slab->free_count + 1, sizeof(unsigned int));
    slab->free_list[slab->free_count++] = slot;
}

static void slab_free(Slab *slab)
{
    free(slab->items);
    free(slab->free_list);
    memset(slab, 0, sizeof(*slab));
}

typedef int (*Matches)(unsigned int slot, const void *key);
typedef unsigned int (*HashOf)(unsigned int slot);

/* The slot (+ 1) whose record matches `key`, or NONE. */
static unsigned int table_find(const Table *t, unsigned int hash, const void *key, Matches matches)
{
    if (t->size == 0) {
        return NONE;
    }
    for (unsigned int i = hash & (t->size - 1), n = 0; n < t->size; i = (i + 1) & (t->size - 1), ++n) {
        const unsigned int c = t->cell[i];
        if (c == NONE) {
            return NONE;
        }
        if (c != REMOVED && matches(c - 1, key)) {
            return c;
        }
    }
    return NONE;
}

static void table_put(Table *t, unsigned int hash, unsigned int slot, HashOf hash_of)
{
    if ((t->used + t->removed + 1) * 2 > t->size) {
        /* Over half full: rebuild the table. It doubles when the live cells
         * fill it, and keeps its size when removed cells do. */
        Table rebuilt;
        rebuilt.size = t->size == 0 ? 16 : (t->used + 1) * 4 > t->size ? t->size * 2 : t->size;
        rebuilt.used = rebuilt.removed = 0;
        rebuilt.cell = (unsigned int *)calloc(rebuilt.size, sizeof(unsigned int));
        if (rebuilt.cell == NULL) {
            fprintf(stderr, "khdays-server: out of memory\n");
            exit(1);
        }
        for (unsigned int i = 0; i < t->size; ++i) {
            const unsigned int c = t->cell[i];
            if (c != NONE && c != REMOVED) {
                unsigned int j = hash_of(c - 1) & (rebuilt.size - 1);
                while (rebuilt.cell[j] != NONE) {
                    j = (j + 1) & (rebuilt.size - 1);
                }
                rebuilt.cell[j] = c;
                ++rebuilt.used;
            }
        }
        free(t->cell);
        *t = rebuilt;
    }
    {
        unsigned int i = hash & (t->size - 1);
#pragma warning(suppress : 6001) /* the cells come from calloc: every one is set */
        while (t->cell[i] != NONE && t->cell[i] != REMOVED) {
            i = (i + 1) & (t->size - 1);
        }
        if (t->cell[i] == REMOVED) {
            --t->removed;
        }
        t->cell[i] = slot + 1;
        ++t->used;
    }
}

static void table_remove(Table *t, unsigned int hash, unsigned int slot)
{
    if (t->size == 0) {
        return;
    }
    for (unsigned int i = hash & (t->size - 1), n = 0; n < t->size; i = (i + 1) & (t->size - 1), ++n) {
        if (t->cell[i] == NONE) {
            return;
        }
        if (t->cell[i] == slot + 1) {
            t->cell[i] = REMOVED;
            --t->used;
            ++t->removed;
            return;
        }
    }
}

static void table_free(Table *t)
{
    free(t->cell);
    memset(t, 0, sizeof(*t));
}

/* --- the tables' keys ---------------------------------------------------- */

/* Groups by their id. */
static unsigned int group_hash(const unsigned char *id)
{
    return fnv1a(id, ID_SIZE, 2166136261u);
}

static unsigned int group_hash_of(unsigned int slot)
{
    return group_hash(GROUP(slot).id);
}

static int group_matches(unsigned int slot, const void *key)
{
    return GROUP(slot).used && memcmp(GROUP(slot).id, key, ID_SIZE) == 0;
}

/* Pairs by their relay token. */
static unsigned int token_hash(unsigned int token)
{
    return token * 2654435761u;
}

static unsigned int pair_token_hash_of(unsigned int slot)
{
    return token_hash(PAIR(slot).token);
}

static int pair_token_matches(unsigned int slot, const void *key)
{
    return PAIR(slot).used && PAIR(slot).token == *(const unsigned int *)key;
}

/* Pairs by their guest: the group, the guest's id and its address. */
typedef struct GuestKey {
    unsigned char group_id[ID_SIZE], guest_id[ID_SIZE];
    KhdaysNetAddress guest;
} GuestKey;

static unsigned int guest_hash(const unsigned char *group_id, const unsigned char *guest_id,
                               const KhdaysNetAddress *guest)
{
    unsigned char where[6];
    put32(where, guest->ip);
    put16(where + 4, guest->port);
    return fnv1a(where, 6, fnv1a(guest_id, ID_SIZE, fnv1a(group_id, ID_SIZE, 2166136261u)));
}

static unsigned int pair_guest_hash_of(unsigned int slot)
{
    return guest_hash(PAIR(slot).group_id, PAIR(slot).guest_id, &PAIR(slot).guest);
}

static int pair_guest_matches(unsigned int slot, const void *key)
{
    const GuestKey *k = (const GuestKey *)key;
    return PAIR(slot).used && memcmp(PAIR(slot).group_id, k->group_id, ID_SIZE) == 0 &&
           memcmp(PAIR(slot).guest_id, k->guest_id, ID_SIZE) == 0 && same_address(&PAIR(slot).guest, &k->guest);
}

/* Indexes by game and build. */
typedef struct GameKey {
    unsigned int ggid, build;
} GameKey;

static unsigned int game_hash(unsigned int ggid, unsigned int build)
{
    return (ggid * 2654435761u) ^ (build * 2246822519u);
}

static unsigned int index_hash_of(unsigned int slot)
{
    return game_hash(INDEX(slot).ggid, INDEX(slot).build);
}

static int index_matches(unsigned int slot, const void *key)
{
    const GameKey *k = (const GameKey *)key;
    return INDEX(slot).used && INDEX(slot).ggid == k->ggid && INDEX(slot).build == k->build;
}

/* --- the "last heard from" chains ---------------------------------------- */

static void group_unlink(unsigned int slot)
{
    Group *g = &GROUP(slot);
    if (g->prev != NONE) {
        GROUP(g->prev - 1).next = g->next;
    } else {
        server.group_oldest = g->next;
    }
    if (g->next != NONE) {
        GROUP(g->next - 1).prev = g->prev;
    } else {
        server.group_newest = g->prev;
    }
    g->prev = g->next = NONE;
}

static void group_link_newest(unsigned int slot)
{
    Group *g = &GROUP(slot);
    g->prev = server.group_newest;
    g->next = NONE;
    if (server.group_newest != NONE) {
        GROUP(server.group_newest - 1).next = slot + 1;
    } else {
        server.group_oldest = slot + 1;
    }
    server.group_newest = slot + 1;
}

static void pair_unlink(unsigned int slot)
{
    Pair *p = &PAIR(slot);
    if (p->prev != NONE) {
        PAIR(p->prev - 1).next = p->next;
    } else {
        server.pair_oldest = p->next;
    }
    if (p->next != NONE) {
        PAIR(p->next - 1).prev = p->prev;
    } else {
        server.pair_newest = p->prev;
    }
    p->prev = p->next = NONE;
}

static void pair_link_newest(unsigned int slot)
{
    Pair *p = &PAIR(slot);
    p->prev = server.pair_newest;
    p->next = NONE;
    if (server.pair_newest != NONE) {
        PAIR(server.pair_newest - 1).next = slot + 1;
    } else {
        server.pair_oldest = slot + 1;
    }
    server.pair_newest = slot + 1;
}

/* --- adding and dropping records ----------------------------------------- */

/* What a group costs from the budget: the record, its announcement, its cells
 * in the tables (two per entry, as the tables stay at most half full) and its
 * place in its index. */
static u64 group_bytes(unsigned int body_size)
{
    return sizeof(Group) + body_size + 2 * sizeof(unsigned int) * 2 + sizeof(unsigned int);
}

/* What a pair costs: the record and its cells in two tables. */
static u64 pair_bytes(void)
{
    return sizeof(Pair) + 2 * sizeof(unsigned int) * 4;
}

static void drop_group(unsigned int slot)
{
    Group *g = &GROUP(slot);
    Index *index = &INDEX(g->index);
    /* Out of its index: the index's last group takes its place. */
    const unsigned int last = index->slots[--index->count];
    if (last != slot + 1) {
        index->slots[g->index_pos] = last;
        GROUP(last - 1).index_pos = g->index_pos;
    }
    if (index->count == 0) {
        table_remove(&server.index_by_game, game_hash(index->ggid, index->build), g->index);
        free(index->slots);
        memset(index, 0, sizeof(*index));
        slab_give_back(&server.indexes, g->index);
    }
    table_remove(&server.group_by_id, group_hash(g->id), slot);
    group_unlink(slot);
    server.bytes -= g->bytes;
    free(g->body);
    memset(g, 0, sizeof(*g));
    slab_give_back(&server.groups, slot);
    --server.counts.groups;
}

static void drop_pair(unsigned int slot)
{
    Pair *p = &PAIR(slot);
    table_remove(&server.pair_by_token, token_hash(p->token), slot);
    table_remove(&server.pair_by_guest, guest_hash(p->group_id, p->guest_id, &p->guest), slot);
    pair_unlink(slot);
    server.bytes -= pair_bytes();
    if (p->relayed) {
        --server.counts.relayed_pairs;
    }
    memset(p, 0, sizeof(*p));
    slab_give_back(&server.pairs, slot);
    --server.counts.pairs;
}

/* Makes room in the budget for `need` more bytes by evicting whatever was
 * heard from longest ago, group or pair. Returns 0 if it cannot (the budget is
 * smaller than the record). */
static int make_room(u64 need)
{
    while (server.bytes + need > server.budget) {
        const unsigned int g = server.group_oldest, p = server.pair_oldest;
        if (g == NONE && p == NONE) {
            return 0;
        }
        if (p == NONE || (g != NONE && GROUP(g - 1).seen <= PAIR(p - 1).heard)) {
            drop_group(g - 1);
        } else {
            drop_pair(p - 1);
        }
        ++server.counts.evicted;
    }
    return 1;
}

/* --- the messages -------------------------------------------------------- */

/* REGISTER: a host announces (or keeps alive) its room. The body is the
 * room's announcement: build u32, digest u32,
 * session port u16, open u8, members u8, ggid u32, info length u32, info.
 * The answer, REGISTERED, tells the host its public address. */
static void on_register(const KhdaysNetAddress *from, const unsigned char *sender, const unsigned char *body,
                        unsigned int size, u64 now)
{
    unsigned char seen[6];
    unsigned int found, slot = 0;
    Group *g;
    if (size < 20 || size > BODY_MOST || le32(body + 16) > size - 20) {
        return;
    }
    found = table_find(&server.group_by_id, group_hash(sender), sender, group_matches);
    if (found != NONE) {
        slot = found - 1;
        g = &GROUP(slot);
        if (!same_address(&g->address, from) && since(now, g->seen) < GROUP_MOVES_AFTER_MS) {
            return; /* someone else using this group's id: not its host */
        }
        if (g->ggid != le32(body + 12) || g->build != le32(body) || g->body_size != size) {
            /* Another game or build, or an announcement of another size:
             * start the group again (it is charged to the budget anew). */
            drop_group(slot);
            found = NONE;
        }
    }
    if (found == NONE) {
        const u64 bytes = group_bytes(size);
        GameKey key;
        unsigned int index_slot;
        Index *index;
        if (!make_room(bytes)) {
            return;
        }
        key.ggid = le32(body + 12);
        key.build = le32(body);
        index_slot = table_find(&server.index_by_game, game_hash(key.ggid, key.build), &key, index_matches);
        if (index_slot == NONE) {
            const unsigned int i = slab_take(&server.indexes, sizeof(Index));
            INDEX(i).used = 1;
            INDEX(i).ggid = key.ggid;
            INDEX(i).build = key.build;
            table_put(&server.index_by_game, game_hash(key.ggid, key.build), i, index_hash_of);
            index_slot = i + 1;
        }
        slot = slab_take(&server.groups, sizeof(Group));
        g = &GROUP(slot);
        g->used = 1;
        memcpy(g->id, sender, ID_SIZE);
        g->ggid = key.ggid;
        g->build = key.build;
        g->index = index_slot - 1;
        index = &INDEX(g->index);
        index->slots = (unsigned int *)grow(index->slots, &index->room, index->count + 1, sizeof(unsigned int));
        g->index_pos = index->count;
        index->slots[index->count++] = slot + 1;
        table_put(&server.group_by_id, group_hash(sender), slot, group_hash_of);
        group_link_newest(slot);
        g->bytes = bytes;
        server.bytes += bytes;
        ++server.counts.groups;
        ++server.counts.groups_opened;
    } else {
        group_unlink(slot);
        group_link_newest(slot);
    }
    g = &GROUP(slot);
    g->address = *from;
    if (g->body == NULL) {
        g->body = (unsigned char *)malloc(size);
        if (g->body == NULL) {
            drop_group(slot);
            return;
        }
        g->body_size = size;
    }
    memcpy(g->body, body, size);
    g->seen = now;
    put32(seen, from->ip);
    put16(seen + 4, from->port);
    send_kind(from, REGISTERED, seen, sizeof(seen));
}

/* UNREGISTER: a host closes its room. Only the room's own host may. */
static void on_unregister(const KhdaysNetAddress *from, const unsigned char *sender)
{
    const unsigned int found = table_find(&server.group_by_id, group_hash(sender), sender, group_matches);
    if (found != NONE && same_address(&GROUP(found - 1).address, from)) {
        drop_group(found - 1);
    }
}

/* LIST: a guest asks for the rooms of its game and build (the game shows no
 * others: net_session.c room_seen), starting from room number `first`.
 * The answer, ROOMS: total u16, first u16, count u16, then for each room: id,
 * ip u32, port u16, announcement size u16, announcement. It holds as many
 * rooms as fit in the size of the datagram that asked. */
static void on_list(const KhdaysNetAddress *from, const unsigned char *body, unsigned int size, long got)
{
    unsigned char answer[DATAGRAM];
    const unsigned int room = (unsigned int)(got < DATAGRAM ? got : DATAGRAM) - HEADER;
    unsigned int first, count = 0, at = 6, total = 0;
    GameKey key;
    unsigned int index_slot;
    if (size < 10 || room < 6) {
        return;
    }
    key.build = le32(body);
    key.ggid = le32(body + 4);
    first = le16(body + 8);
    index_slot = table_find(&server.index_by_game, game_hash(key.ggid, key.build), &key, index_matches);
    if (index_slot != NONE) {
        const Index *index = &INDEX(index_slot - 1);
        total = index->count;
        for (unsigned int i = first; i < index->count; ++i) {
            const Group *g = &GROUP(index->slots[i] - 1);
            if (at + 14 + g->body_size > room) {
                break;
            }
            memcpy(answer + at, g->id, ID_SIZE);
            put32(answer + at + 6, g->address.ip);
            put16(answer + at + 10, g->address.port);
            put16(answer + at + 12, g->body_size);
            memcpy(answer + at + 14, g->body, g->body_size);
            at += 14 + g->body_size;
            ++count;
        }
    }
    put16(answer, total > 0xffff ? 0xffff : total);
    put16(answer + 2, first);
    put16(answer + 4, count);
    send_kind(from, ROOMS, answer, at);
}

/* PEER: tells one side of a pair about the other. Body: token u32, the other
 * side's id, its ip u32 and port u16. */
static void send_peer(const KhdaysNetAddress *to, unsigned int token, const unsigned char *id,
                      const KhdaysNetAddress *other)
{
    unsigned char body[CONNECT_BODY];
    memset(body, 0, sizeof(body));
    put32(body, token);
    memcpy(body + 4, id, ID_SIZE);
    put32(body + 10, other->ip);
    put16(body + 14, other->port);
    send_kind(to, PEER, body, sizeof(body));
}

/* A random relay token no pair uses (never 0). */
static unsigned int new_token(void)
{
    unsigned int token;
    do {
        khdays_host_random(&token, sizeof(token));
    } while (token == 0 || table_find(&server.pair_by_token, token_hash(token), &token, pair_token_matches) != NONE);
    return token;
}

/* CONNECT: a guest asks to join a room (the body starts with the room's id).
 * The server introduces both sides to each other with a PEER each, or
 * answers GONE if the room is not known. */
static void on_connect(const KhdaysNetAddress *from, const unsigned char *sender, const unsigned char *body,
                       unsigned int size, u64 now)
{
    GuestKey key;
    unsigned int group, found, slot;
    Group *g;
    Pair *p;
    if (size < CONNECT_BODY) {
        return;
    }
    group = table_find(&server.group_by_id, group_hash(body), body, group_matches);
    if (group == NONE) {
        send_kind(from, GONE, body, ID_SIZE);
        return;
    }
    memcpy(key.group_id, body, ID_SIZE);
    memcpy(key.guest_id, sender, ID_SIZE);
    key.guest = *from;
    found = table_find(&server.pair_by_guest, guest_hash(key.group_id, key.guest_id, from), &key, pair_guest_matches);
    if (found != NONE) {
        /* The same guest asking again: answer it again, the host already knows. */
        p = &PAIR(found - 1);
        p->heard = now;
        pair_unlink(found - 1);
        pair_link_newest(found - 1);
        send_peer(from, p->token, GROUP(group - 1).id, &GROUP(group - 1).address);
        return;
    }
    if (since(now, GROUP(group - 1).introduced) < INTRODUCE_EVERY_MS) {
        ++server.counts.introductions_held; /* the guest will ask again in 250 ms */
        return;
    }
    if (!make_room(pair_bytes())) {
        return;
    }
    /* Making room may have evicted the group itself. */
    group = table_find(&server.group_by_id, group_hash(body), body, group_matches);
    if (group == NONE) {
        send_kind(from, GONE, body, ID_SIZE);
        return;
    }
    g = &GROUP(group - 1);
    slot = slab_take(&server.pairs, sizeof(Pair));
    p = &PAIR(slot);
    p->used = 1;
    p->token = new_token();
    memcpy(p->group_id, g->id, ID_SIZE);
    memcpy(p->guest_id, sender, ID_SIZE);
    p->guest = *from;
    p->host = g->address;
    p->heard = now;
    p->allowance[0] = p->allowance[1] = RELAY_BURST;
    p->refilled[0] = p->refilled[1] = now;
    table_put(&server.pair_by_token, token_hash(p->token), slot, pair_token_hash_of);
    table_put(&server.pair_by_guest, guest_hash(p->group_id, p->guest_id, &p->guest), slot, pair_guest_hash_of);
    pair_link_newest(slot);
    server.bytes += pair_bytes();
    ++server.counts.pairs;
    g->introduced = now;
    send_peer(from, p->token, g->id, &g->address);
    send_peer(&g->address, p->token, sender, from);
}

/* RELAY: token u32, then a whole session datagram. It is
 * passed to the other side of the pair as it came, within the pair's
 * allowance (a "token bucket": refilled at RELAY_PER_SECOND, at most
 * RELAY_BURST). */
static void on_relay(const KhdaysNetAddress *from, const unsigned char *body, unsigned int size, u64 now)
{
    unsigned int found, way, token;
    Pair *p;
    if (!server.relay || size < 4 + HEADER) {
        return;
    }
    token = le32(body);
    found = table_find(&server.pair_by_token, token_hash(token), &token, pair_token_matches);
    if (found == NONE) {
        return;
    }
    p = &PAIR(found - 1);
    if (same_address(from, &p->guest)) {
        way = 0;
    } else if (same_address(from, &p->host)) {
        way = 1;
    } else {
        return;
    }
    p->allowance[way] += (double)since(now, p->refilled[way]) * RELAY_PER_SECOND / 1000.0;
    if (p->allowance[way] > RELAY_BURST) {
        p->allowance[way] = RELAY_BURST;
    }
    p->refilled[way] = now;
    if (p->allowance[way] < 1.0) {
        ++server.counts.relay_dropped;
        return;
    }
    p->allowance[way] -= 1.0;
    if (since(now, p->second_began[way]) >= 1000) {
        p->second_began[way] = now;
        p->this_second[way] = 0;
    }
    if (++p->this_second[way] > server.counts.relay_busiest) {
        server.counts.relay_busiest = p->this_second[way];
    }
    if (!p->relayed) {
        p->relayed = 1;
        ++server.counts.relayed_pairs;
    }
    p->heard = now;
    pair_unlink(found - 1);
    pair_link_newest(found - 1);
    ++server.counts.relayed;
    send_kind(way == 0 ? &p->host : &p->guest, RELAY, body, size);
}

void khdays_server_datagram(const KhdaysNetAddress *from, const unsigned char *d, long got, u64 now)
{
    unsigned int size;
    if (got < HEADER || le32(d) != MAGIC || d[5] != VERSION) {
        return;
    }
    size = le16(d + 6);
    if ((long)HEADER + (long)size > got) {
        return;
    }
    switch (d[4]) {
    case REGISTER: on_register(from, d + 8, d + HEADER, size, now); break;
    case UNREGISTER: on_unregister(from, d + 8); break;
    case LIST: on_list(from, d + HEADER, size, got); break;
    case CONNECT: on_connect(from, d + 8, d + HEADER, size, now); break;
    case RELAY: on_relay(from, d + HEADER, size, now); break;
    default: break; /* PUNCH and the sessions' own kinds are not for the server */
    }
}

void khdays_server_tick(u64 now)
{
    while (server.group_oldest != NONE && since(now, GROUP(server.group_oldest - 1).seen) > GROUP_SILENT_MS) {
        drop_group(server.group_oldest - 1);
    }
    while (server.pair_oldest != NONE && since(now, PAIR(server.pair_oldest - 1).heard) > PAIR_SILENT_MS) {
        drop_pair(server.pair_oldest - 1);
    }
}

void khdays_server_counts(KhdaysServerCounts *out)
{
    *out = server.counts;
    out->bytes = server.bytes;
    out->budget = server.budget;
}

void khdays_server_start(KhdaysUdp *udp, u64 budget, int relay)
{
    memset(&server, 0, sizeof(server));
    server.udp = udp;
    server.budget = budget;
    server.relay = relay;
    khdays_host_random(server.id, ID_SIZE);
}

void khdays_server_stop(void)
{
    for (unsigned int i = 0; i < server.groups.count; ++i) {
#pragma warning(suppress : 6001) /* slab_take and grow zero every slot: a body is NULL or its own */
        free(GROUP(i).body);
    }
    for (unsigned int i = 0; i < server.indexes.count; ++i) {
#pragma warning(suppress : 6001) /* the same: slots NULL or their own */
        free(INDEX(i).slots);
    }
    slab_free(&server.groups);
    slab_free(&server.pairs);
    slab_free(&server.indexes);
    table_free(&server.group_by_id);
    table_free(&server.pair_by_token);
    table_free(&server.pair_by_guest);
    table_free(&server.index_by_game);
    memset(&server, 0, sizeof(server));
}
