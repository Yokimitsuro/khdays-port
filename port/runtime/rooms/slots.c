/* The rooms' loaded slots (slots.h). */
#include "slots.h"

#include <stdlib.h>
#include <string.h>

static signed char *slots;
static int count;

int khdays_room_slot(int room)
{
    return room >= 0 && room < count ? slots[room] : -1;
}

void khdays_room_slot_set(int room, int slot)
{
    if (room < 0) {
        return;
    }
    if (room >= count) {
        const int grown = room + 1 > count * 2 ? room + 1 : count * 2;
        signed char *more = (signed char *)realloc(slots, (size_t)grown);
        if (more == NULL) {
            return;
        }
        memset(more + count, 0xff, (size_t)(grown - count));
        slots = more;
        count = grown;
    }
    slots[room] = (signed char)slot;
}

void khdays_room_slots_clear(void)
{
    if (slots != NULL) {
        memset(slots, 0xff, (size_t)count);
    }
}
