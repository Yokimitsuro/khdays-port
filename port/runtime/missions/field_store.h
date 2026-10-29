/* The new missions' progress. The game keeps a mission's progress in its flag array, by the mission's
 * number: status, stage and transfer (3 bits each) and badges (4 bits). A new mission's number puts all of
 * them past the array's 0x4000 bits, and the game's reads and writes there are answered from here
 * (port/tools/prepare.py joins GameState_GetField and the others); with no new mission the game's own
 * calls run unchanged. Bits in the bit array's order: a field's first bit is its value's highest. */
#ifndef KHDAYS_MISSIONS_FIELD_STORE_H
#define KHDAYS_MISSIONS_FIELD_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_FLAG_BITS 0x4000u /* the game's flag array, gGameState + 0x10 */

enum { KHDAYS_RECORD_STATUS, KHDAYS_RECORD_STAGE, KHDAYS_RECORD_TRANSFER, KHDAYS_RECORD_BADGES, KHDAYS_RECORDS };
/* A mission's chests' opened flags: one each, wherever there is room for them (their rows keep the flag less 9
 * in an s16, Ov002_ScriptSpawnTimedSet). */
#define KHDAYS_RECORD_CHESTS KHDAYS_RECORDS
#define KHDAYS_CHEST_FLAG_LAST (0x7fffu + 9u)

/* Where record `record` of mission number `number` starts in the flag array, and how many bits it has. */
unsigned khdays_mission_field(int number, int record, unsigned *bits);

typedef struct KhdaysMissionProgress {
    unsigned value[KHDAYS_RECORDS];
    int best;              /* the best result the results screen keeps (gGameState's missionResultRecords), -1 none */
    unsigned char *opened; /* a chest's: 1 opened */
    int chest_count;
    unsigned first_chest_flag; /* the flag of its first chest, the others after it */
} KhdaysMissionProgress;

typedef struct KhdaysFieldStore {
    KhdaysMissionProgress *progress; /* by the registry's index */
    int count;
    unsigned *keys, *places; /* each placed bit (+1) and its mission, record and bit: open addressing */
    unsigned capacity, used;
} KhdaysFieldStore;

void khdays_field_store_free(KhdaysFieldStore *s);

/* Whether every field of mission number `number` lies past the flag array and is no other mission's. */
int khdays_field_store_fits(const KhdaysFieldStore *s, int number);

/* Gives the registry's mission `index` the fields of number `number`. 0 when they do not fit (or no memory). */
int khdays_field_store_place(KhdaysFieldStore *s, int index, int number);

/* Gives the registry's mission `index` (placed) the flags of its `count` chests, one after another past the flag
 * array: the first in *first. 0 when there is no room for them up to KHDAYS_CHEST_FLAG_LAST (or no memory). */
int khdays_field_store_place_chests(KhdaysFieldStore *s, int index, int count, unsigned *first);

/* Every mission's progress back to none. */
void khdays_field_store_clear(KhdaysFieldStore *s);

/* A field the game reads or writes: 1 when all its bits are new missions', with the value read or stored;
 * 0 when not (the game's own then). */
int khdays_field_store_get(const KhdaysFieldStore *s, unsigned field, unsigned bits, unsigned *value);
int khdays_field_store_set(KhdaysFieldStore *s, unsigned field, unsigned bits, unsigned value);

#ifdef __cplusplus
}
#endif

#endif
