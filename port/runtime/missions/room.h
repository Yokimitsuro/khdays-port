/* The new missions a copy brings to a multiplayer room, as the room's copies tell each other: each copy numbers the
 * mods' missions in its own mods' order (registry.h), so the same mission can have another number on another copy,
 * and a copy may have missions another lacks. The host lists only the missions every member has, the same, and a
 * guest plays the host's choice under its own number (missions.c).
 *
 * The list, for each mission numbered this run whose block was kept: its identity's length (u16) and bytes, its
 * digest (32 bytes), its number and its file (u16 each); numbers little-endian. Plain C: no game, no network. */
#ifndef KHDAYS_MISSIONS_ROOM_H
#define KHDAYS_MISSIONS_ROOM_H

#include "registry.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysRoomMission {
    const char *identity; /* not NUL-terminated: identity_size bytes */
    size_t identity_size;
    const unsigned char *digest; /* 32 bytes */
    int number, file;
} KhdaysRoomMission;

/* This copy's list (malloc'd, `*size` bytes); NULL with `*size` 0 when it offers none, or out of memory. */
unsigned char *khdays_missions_room_list(const KhdaysMissions *r, size_t *size);

/* The entry at `*at` of a list, `*at` moved past it: 1, or 0 at the end or where the list stops making sense. */
int khdays_missions_room_next(const unsigned char *list, size_t size, size_t *at, KhdaysRoomMission *out);

/* Whether `list` has mission `m`: its identity with its digest. */
int khdays_missions_room_holds(const unsigned char *list, size_t size, const KhdaysMission *m);

/* This copy's mission that is the one `list` (another copy's) numbers `number`: the same identity and digest. NULL
 * when the list names no such number (`*named` 0) or this copy has no such mission (`*named` 1). */
const KhdaysMission *khdays_missions_room_local(const KhdaysMissions *r, const unsigned char *list, size_t size,
                                                int number, int *named);

#ifdef __cplusplus
}
#endif

#endif
