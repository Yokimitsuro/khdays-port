/* A character's or a form's level table (lv.b.z, LevelTable_ReadEntry): its base's with the values its [stats] give.
 * Plain C: no game. */
#ifndef KHDAYS_CHARACTERS_STATS_H
#define KHDAYS_CHARACTERS_STATS_H

#include "registry.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A row's size and the table's (KHDAYS_CHARACTER_LEVELS rows). */
#define KHDAYS_STATS_ROW 16
#define KHDAYS_STATS_TABLE (KHDAYS_STATS_ROW * KHDAYS_CHARACTER_LEVELS)

/* Each stat's place in a row: u16 strength, magic, defence, hp, unknown_8, a pad, then s32 unknown_12. */
int khdays_stats_offset(int stat);

/* `out` (KHDAYS_STATS_TABLE bytes) made from `base` (the base's table unpacked, at least KHDAYS_STATS_TABLE bytes)
 * and `points`: a stat given at some levels has, between two of them, the value in between (rounded), before the
 * first the first's and after the last the last's; a stat given at no level keeps the base's. 0 when `base` is too
 * short. */
int khdays_stats_table(const KhdaysCharacterStatPoint *points, int count, const unsigned char *base, size_t base_size,
                       unsigned char *out);

/* Stat `stat` at level `level` (1-100) of a table. */
long khdays_stats_value(const unsigned char *table, int level, int stat);

#ifdef __cplusplus
}
#endif

#endif
