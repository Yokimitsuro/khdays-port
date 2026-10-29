/* The game's tables a new mission needs its rows in, made as the game loads them: the missions' list
 * (UI/cm/msl_<lang>.msi.z: a u16 count, then records led by their u16 size) and the missions' table
 * (mi/mi/mdb.z: a u32 count, then 0x3c-byte rows keyed by the file number). A new mission's rows are its
 * template's (the game mission it was made from) with its own numbers and words, after the game's own: in the
 * list, its Mission Mode row alone (never a story row).
 * docs/RUNTIME_MAP.md, "Missions and worlds of mods". Plain C: no game, no system calls. */
#ifndef KHDAYS_MISSIONS_TABLES_H
#define KHDAYS_MISSIONS_TABLES_H

#include "field_store.h"
#include "registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The list's record: its fixed part, then four UTF-16 texts (name, description, objective, a format). */
#define KHDAYS_LIST_FILE 0x02u
#define KHDAYS_LIST_NUMBER 0x04u
#define KHDAYS_LIST_FIRST_DAY 0x06u /* the story's lists show it on these days (Ov008_RecordFilter_InRange) */
#define KHDAYS_LIST_LAST_DAY 0x08u
#define KHDAYS_LIST_CATEGORY 0x0au
#define KHDAYS_LIST_HIDDEN 0x0cu
#define KHDAYS_LIST_SHOWN 0x0eu   /* the number the row shows */
#define KHDAYS_LIST_SECONDS 0x18u /* the time limit the info shows */
#define KHDAYS_LIST_KIND 0x23u    /* below */
#define KHDAYS_LIST_REWARD 0x28u  /* the results' reward kind */
#define KHDAYS_LIST_TEXTS 0x30u

/* A record's kind: the story's row of a mission (its calendar's lists, by its days), Mission Mode's row of it
 * (Ov008_IsEncounterEligible), or one of its other entries there (kinds 1 and 2: not read yet, and counted in the
 * camp's completion percentage, so a new mission has none). Mission Mode's rows reward by the transfer field's
 * rank. */
#define KHDAYS_ENTRY_STORY 0
#define KHDAYS_ENTRY_MISSION 4
#define KHDAYS_REWARD_MISSION 8

#define KHDAYS_MDB_ROW 0x3cu
#define KHDAYS_MDB_LEVEL 0x08u /* the enemies' level base alone, then in a session */
#define KHDAYS_MDB_LEVEL_SESSION 0x0cu

/* The list file (language `language`, 0-4) as loaded, `size` bytes, with the numbered missions' records
 * after the game's: malloc'd, its size in *out_size; NULL when it adds nothing or the file does not read. */
unsigned char *khdays_tables_list(const unsigned char *file, size_t size, const KhdaysMissions *r, int language,
                                  size_t *out_size);

/* The same for the missions' table. */
unsigned char *khdays_tables_mdb(const unsigned char *file, size_t size, const KhdaysMissions *r, size_t *out_size);

/* The chests (mi/mi/trbox) and the emblems (mi/mi/evi): a u32 count, as many u32 offsets from the file's
 * start, then the sets they point at -- u16 the mission's file number (the field's key,
 * Ov002_FindHandlerByKey), s8 how many rows, a byte, rows of 0x14 bytes. A chest's row (Ov002_ScriptSpawnTimedSet):
 * s8 its slot (state field 0x20c0 + 2 x slot), s8 the room it is in, s16 its contents, s16 its opened flag less 9,
 * s16 its heading in degrees, then its point (20.12). An emblem's (Ov002_SpawnStateSpots): s8 its bit of the
 * mission's badges, s8 a kind, s16 1 for the bit 3 one, s16 a value, s16 -, its point. */
#define KHDAYS_PLACED_SET_HEAD 4u
#define KHDAYS_PLACED_ROW 0x14u
#define KHDAYS_CHEST_FLAG_LESS 9u

/* The same for trbox (`chests` 1) or evi: a set for each numbered mission that has some, after the game's. A
 * chest's flag is its mission's first chest flag in `s` + its number. */
unsigned char *khdays_tables_placed(const unsigned char *file, size_t size, const KhdaysMissions *r,
                                    const KhdaysFieldStore *s, int chests, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
