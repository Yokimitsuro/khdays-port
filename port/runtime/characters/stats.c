/* A character's or a form's level table (stats.h). */
#include "stats.h"

#include <string.h>

int khdays_stats_offset(int stat)
{
    return stat == KHDAYS_STAT_UNKNOWN_12 ? 12 : stat * 2;
}

long khdays_stats_value(const unsigned char *table, int level, int stat)
{
    const unsigned char *at = table + (size_t)(level - 1) * KHDAYS_STATS_ROW + khdays_stats_offset(stat);
    if (stat == KHDAYS_STAT_UNKNOWN_12) {
        return (long)((unsigned long)at[0] | (unsigned long)at[1] << 8 | (unsigned long)at[2] << 16 |
                      (unsigned long)at[3] << 24);
    }
    return (long)(at[0] | at[1] << 8);
}

static void put(unsigned char *table, int level, int stat, long value)
{
    unsigned char *at = table + (size_t)(level - 1) * KHDAYS_STATS_ROW + khdays_stats_offset(stat);
    at[0] = (unsigned char)value;
    at[1] = (unsigned char)(value >> 8);
    if (stat == KHDAYS_STAT_UNKNOWN_12) {
        at[2] = (unsigned char)(value >> 16);
        at[3] = (unsigned char)(value >> 24);
    }
}

int khdays_stats_table(const KhdaysCharacterStatPoint *points, int count, const unsigned char *base, size_t base_size,
                       unsigned char *out)
{
    if (base == NULL || base_size < KHDAYS_STATS_TABLE) {
        return 0;
    }
    memcpy(out, base, KHDAYS_STATS_TABLE);
    for (int stat = 0; stat < KHDAYS_STATS; ++stat) {
        /* the stat's given values by level (a later line for the same level wins) */
        long given[KHDAYS_CHARACTER_LEVELS + 1] = {0};
        int has[KHDAYS_CHARACTER_LEVELS + 1] = {0}, any = 0;
        for (int i = 0; i < count; ++i) {
            if (points[i].stat == stat && points[i].level >= 1 && points[i].level <= KHDAYS_CHARACTER_LEVELS) {
                given[points[i].level] = points[i].value;
                has[points[i].level] = 1;
                any = 1;
            }
        }
        if (!any) {
            continue;
        }
        int before = 0;
        for (int level = 1; level <= KHDAYS_CHARACTER_LEVELS; ++level) {
            if (has[level]) {
                before = level;
                put(out, level, stat, given[level]);
                continue;
            }
            int after = 0;
            for (int next = level + 1; next <= KHDAYS_CHARACTER_LEVELS && after == 0; ++next) {
                after = has[next] ? next : 0;
            }
            long v;
            if (before == 0) {
                v = given[after];
            } else if (after == 0) {
                v = given[before];
            } else {
                const long span = after - before, step = level - before;
                const long d = given[after] - given[before];
                v = given[before] + (d * step + (d >= 0 ? span / 2 : -span / 2)) / span;
            }
            put(out, level, stat, v);
        }
    }
    return 1;
}
