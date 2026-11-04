/* The game's reliable messages past its 32 slots (see msg_overflow.h). The queue as MsgQueue_Post fills it
 * (gMsgQueue + 0xc): a u32 of free slots (bit 31 slot 0), 32 slots of 6 bytes (u16 number, u16 size with its 4-byte
 * header, u8 kind, u8 the players that have it), 32 blocks of 0x34 bytes (a 4-byte header, then the message), then
 * the u16 counter that numbers them. */
#include "msg_overflow.h"
#include "../core/session_log.h"

#include <stdlib.h>
#include <string.h>

#define FREE 0x0u
#define SLOTS 0x4u
#define SLOT_BYTES 6u
#define STATES 0xc4u
#define STATE_BYTES 0x34u
#define STATE_MESSAGE 4u
#define MESSAGE_BYTES 0x30u
#define COUNTER 0x744u

typedef struct Waiting {
    int kind;
    unsigned short size, handle;
    unsigned char message[MESSAGE_BYTES];
} Waiting;

static Waiting *waiting; /* a ring: `count` from `head` */
static int head, count, ring;
static unsigned most; /* the most that waited at once, since the queue was made */

int khdays_msgqueue_waiting(void)
{
    return count > 0;
}

unsigned short khdays_msgqueue_wait(void *ctx, int kind, const void *cmd, unsigned short size)
{
    unsigned char *c = (unsigned char *)ctx;
    unsigned short handle, next;
    Waiting *w;
    memcpy(&handle, c + COUNTER, 2);
    next = (unsigned short)(handle + 1u);
    if (next >= 0xffffu) {
        next = 0; /* as MsgQueue_Post: never the "no slot" answer */
    }
    memcpy(c + COUNTER, &next, 2);
    if (count == ring) {
        const int grown = ring ? ring * 2 : 64;
        Waiting *more = (Waiting *)malloc((size_t)grown * sizeof(Waiting));
        if (more == NULL) {
            return 0xffffu; /* no memory: lost, as the game did */
        }
        for (int i = 0; i < count; ++i) {
            more[i] = waiting[(head + i) % ring];
        }
        free(waiting);
        waiting = more;
        head = 0;
        ring = grown;
    }
    w = &waiting[(head + count) % ring];
    w->kind = kind;
    w->size = size <= MESSAGE_BYTES ? size : (unsigned short)MESSAGE_BYTES;
    w->handle = handle;
    memcpy(w->message, cmd, w->size);
    ++count;
    if ((unsigned)count > most) {
        if (most == 0) {
            khdays_log("network: the game's 32 message slots are taken: messages wait for one (none lost)");
        }
        most = (unsigned)count;
    }
    return handle;
}

void khdays_msgqueue_drain(void *ctx)
{
    unsigned char *c = (unsigned char *)ctx;
    unsigned free_slots;
    if (c == NULL || count == 0) {
        return;
    }
    memcpy(&free_slots, c + FREE, 4);
    while (count > 0 && free_slots != 0) {
        const Waiting *w = &waiting[head];
        unsigned idx = 0;
        unsigned short slot_size = (unsigned short)(w->size + 4u);
        unsigned char *slot, *state;
        while (!(free_slots & (0x80000000u >> idx))) {
            ++idx; /* the highest-order free slot, as MsgQueue_Post's clz */
        }
        free_slots &= ~(0x80000000u >> idx);
        slot = c + SLOTS + SLOT_BYTES * idx;
        state = c + STATES + STATE_BYTES * idx;
        memcpy(slot, &w->handle, 2);
        memcpy(slot + 2, &slot_size, 2);
        slot[4] = (unsigned char)w->kind;
        slot[5] = 0;
        memcpy(state + STATE_MESSAGE, w->message, w->size);
        head = (head + 1) % ring;
        --count;
    }
    memcpy(c + FREE, &free_slots, 4);
}

int khdays_msgqueue_holds(unsigned int handle)
{
    for (int i = 0; i < count; ++i) {
        if (waiting[(head + i) % ring].handle == handle) {
            return 1;
        }
    }
    return 0;
}

/* The frames' buffers grown, and their room (a few: two players' kinds, two buffers each). */
typedef struct Grown {
    const void *buffer;
    int room;
} Grown;

static Grown *grown;
static int ngrown, grown_room, largest;

int khdays_msgframe_room(const void *buffer, int room)
{
    for (int i = 0; i < ngrown; ++i) {
        if (grown[i].buffer == buffer) {
            return grown[i].room;
        }
    }
    return room;
}

void khdays_msgframe_grown(const void *old, const void *buffer, int room)
{
    int i = 0;
    while (i < ngrown && grown[i].buffer != old) {
        ++i;
    }
    if (i == ngrown) {
        if (ngrown == grown_room) {
            const int more = grown_room ? grown_room * 2 : 8;
            Grown *bigger = (Grown *)realloc(grown, (size_t)more * sizeof(Grown));
            if (bigger == NULL) {
                return; /* (its room then read as the game's: the messages past it dropped, as before) */
            }
            grown = bigger;
            grown_room = more;
        }
        ++ngrown;
    }
    grown[i].buffer = buffer;
    grown[i].room = room;
    if (room > largest) {
        largest = room;
        khdays_log("network: a frame's messages past the DS's 480 bytes: its buffer grown to %d", room);
    }
}

/* The receiving slots grown (their buffers in the game's heap), and the largest. */
static const void **slots_grown;
static int nslots, slots_room;
static unsigned slot_largest;

static void note_slot(const void *buffer);

int khdays_msgslot_grown(const void *old, const void *buffer, unsigned int room, unsigned int need)
{
    int heap = 0;
    for (int i = 0; i < nslots; ++i) {
        if (slots_grown[i] == old) {
            slots_grown[i] = slots_grown[--nslots];
            heap = 1;
            break;
        }
    }
    if (room > slot_largest) {
        slot_largest = room;
        khdays_log("network: a received frame needs %u bytes, past the DS's slot: its slot grown to %u", need, room);
    }
    note_slot(buffer);
    return heap;
}

/* A slot's buffer from the game's heap, to free at its next growth. */
static void note_slot(const void *buffer)
{
    if (nslots == slots_room) {
        const int more = slots_room ? slots_room * 2 : 8;
        const void **bigger = (const void **)realloc((void *)slots_grown, (size_t)more * sizeof(*slots_grown));
        if (bigger == NULL) {
            return; /* (never freed then: a small loss) */
        }
        slots_grown = bigger;
        slots_room = more;
    }
    slots_grown[nslots++] = buffer;
}

void khdays_msgqueue_reset(void)
{
    if (most > 0) {
        khdays_log("network: at most %u of the game's messages waited for a slot", most);
    }
    head = count = 0;
    most = 0;
    ngrown = 0;
    largest = 0;
    nslots = 0;
    slot_largest = 0;
}
