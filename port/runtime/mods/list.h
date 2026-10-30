/* The mods the game reads: every folder in the mods folder, scripted (a mod.ini) or not, in load order.
 * The order and which are on are saves/mods/order.ini's, a `<folder> = on|off` line per folder
 * (scripts/order.h); a folder not listed yet comes after the listed ones, on. A mod is also off, by hand
 * and whatever the file says, when its folder's name begins with '.' or when it holds a file named
 * .ignore. The VFS reads the on mods in this order (the first with a file wins), and so do the sounds
 * and the scripts. The files, the sounds and the mods' characters, missions and native plugins are the ones on as
 * the game started (khdays_mods_start): a mod turned or moved on the Mods page changes them at the next start.
 * A .days file in the mods folder (KH DS Project Editor's) is a mod too, its files read from the port's copy of
 * them in mods/.days/<its name> (days.h). */
#ifndef KHDAYS_MODS_LIST_H
#define KHDAYS_MODS_LIST_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_DAYS_EXTENSION ".days"
#define KHDAYS_DAYS_CACHE ".days" /* the folder of the .days mods' copies, in the mods folder */

/* Whether a file's name is a .days mod's (any case). */
static inline int khdays_mods_is_days(const char *name)
{
    const size_t n = strlen(name), e = sizeof(KHDAYS_DAYS_EXTENSION) - 1;
    if (n <= e) {
        return 0;
    }
    for (size_t i = 0; i < e; ++i) {
        const char c = name[n - e + i];
        if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != KHDAYS_DAYS_EXTENSION[i]) {
            return 0;
        }
    }
    return 1;
}

typedef struct KhdaysModEntry {
    const char *folder; /* its name in the mods folder */
    const char *path;   /* <mods folder>/<folder> */
    int scripted;       /* it has a mod.ini: runtime/scripts loads it */
    int on;             /* order.ini's (on when not listed) */
    int ignored;        /* off by hand: a .ignore file, or a name beginning with '.' */
} KhdaysModEntry;

/* Lists `mods_path`'s folders and reads `user`/mods/order.ini (`user`: the player's saves folder; NULL
 * for none), replacing what was known. Each mod is told in the session log. */
void khdays_mods_scan(const char *mods_path, const char *user);

int khdays_mods_count(void);
const KhdaysModEntry *khdays_mods_at(int index);
const KhdaysModEntry *khdays_mods_by_folder(const char *folder);

/* Whether the mod's files are read: on and not ignored. */
int khdays_mods_active(const KhdaysModEntry *entry);

/* Turns a mod on or off, or moves it one place earlier (`by` -1) or later (+1): order.ini is written, for the
 * scripts now and for the rest at the next start. */
void khdays_mods_set_on(const char *folder, int on);
void khdays_mods_move(const char *folder, int by);

/* Takes the mods on now as the ones the game reads until it starts again. Once, after the first scan and
 * before the game's other threads start; later calls do nothing. */
void khdays_mods_start(void);

/* The mods taken at the start, in load order: their folders' names and paths (NULL past the count). */
int khdays_mods_started_count(void);
const char *khdays_mods_started_folder(int index);
const char *khdays_mods_started_path(int index);
/* Whether the mod in `folder` (its name in the mods folder) was taken at the start. */
int khdays_mods_started_has(const char *folder);

#ifdef __cplusplus
}
#endif

#endif
