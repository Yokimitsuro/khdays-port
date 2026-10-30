/* The world of each save (saveworld.h). */
#include "saveworld.h"
#include "saveslots.h"

#include "../core/rom.h"
#include "../core/session_log.h"
#include "../mods/installed.h"
#include "../../host/app/files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* The mission worlds' archives (mi/wd/wd_<code>) and the world each shows. eh and tw: the Castle and
 * the city round it; zz: none of the story's. */
static const struct {
    const char *code;
    int world;
} CODES[] = {
    {"tt", KHDAYS_WORLD_TWILIGHT_TOWN}, {"al", KHDAYS_WORLD_AGRABAH},
    {"bb", KHDAYS_WORLD_BEASTS_CASTLE}, {"he", KHDAYS_WORLD_OLYMPUS},
    {"aw", KHDAYS_WORLD_WONDERLAND},    {"nm", KHDAYS_WORLD_HALLOWEEN_TOWN},
    {"pp", KHDAYS_WORLD_NEVERLAND},     {"eh", KHDAYS_WORLD_CASTLE},
    {"tw", KHDAYS_WORLD_CASTLE},        {"zz", -1},
};
#define CODE_COUNT ((int)(sizeof(CODES) / sizeof(CODES[0])))

static struct {
    int found;
    u32 start[CODE_COUNT], end[CODE_COUNT];
    u32 lowest, highest;
} archives;

/* The worlds read last: `now` the archive the game read last, `before` the one it read before that
 * (another); -1 none. The Castle's Grey Area, where the story saves, is among the Castle's: back from
 * a mission the world before it is the mission's. */
static volatile int now = -1, before = -1;

/* the saves' worlds by their counters: the saves the files hold (a counter none holds any more let go when a
 * save is kept), the lists grown as needed */
static SRWLOCK lock = SRWLOCK_INIT;
static u32 *kept_counter;
static unsigned char *kept_world;
static int kept, kept_room;
static char path[300];
static u32 highest_seen; /* the highest save counter seen: a write with no higher one saved nothing new */

static void find_archives(void)
{
    archives.found = 1;
    archives.lowest = 0xffffffffu;
    archives.highest = 0;
    for (int i = 0; i < CODE_COUNT; ++i) {
        char name[32];
        u32 offset = 0, size = 0;
        snprintf(name, sizeof(name), "mi/wd/wd_%s", CODES[i].code);
        if (khdays_rom_find(name, &offset, &size) && size > 0) {
            archives.start[i] = offset;
            archives.end[i] = offset + size;
            if (offset < archives.lowest) {
                archives.lowest = offset;
            }
            if (offset + size > archives.highest) {
                archives.highest = offset + size;
            }
        }
    }
}

void khdays_saveworld_card_read(u32 offset)
{
    if (!khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return;
    }
    if (!archives.found) {
        find_archives();
    }
    if (offset < archives.lowest || offset >= archives.highest) {
        return;
    }
    for (int i = 0; i < CODE_COUNT; ++i) {
        if (offset >= archives.start[i] && offset < archives.end[i]) {
            if (i != now) {
                before = now;
                now = i;
                khdays_log("save world: the game reads mi/wd/wd_%s", CODES[i].code);
            }
            return;
        }
    }
}

static int world_of_code(int i)
{
    return i >= 0 && i < CODE_COUNT ? CODES[i].world : -1;
}

/* The world played last now (KHDAYS_WORLD_*). */
static int world_now(void)
{
    const int a = world_of_code(now), b = world_of_code(before);
    /* in the Castle (where the story saves) after a mission elsewhere: that mission's world */
    if (a == KHDAYS_WORLD_CASTLE && b > KHDAYS_WORLD_CASTLE) {
        return b;
    }
    return a >= 0 ? a : b >= 0 ? b : KHDAYS_WORLD_CASTLE;
}

/* Room for one more kept save; 0 when the lists could not grow. */
static int kept_more(void)
{
    if (kept == kept_room) {
        const int room = kept_room ? kept_room * 2 : 64;
        u32 *counters = (u32 *)realloc(kept_counter, (size_t)room * sizeof(*counters));
        unsigned char *worlds;
        if (counters == NULL) {
            return 0;
        }
        kept_counter = counters;
        if ((worlds = (unsigned char *)realloc(kept_world, (size_t)room)) == NULL) {
            return 0;
        }
        kept_world = worlds;
        kept_room = room;
    }
    return 1;
}

/* Whether one of the files holds the save counted `counter` (fields 0xc77 and 0xc87, as saveslots.h reads them). */
static int counter_saved(u32 counter)
{
    for (int f = 0; f < KHDAYS_SLOT_COUNT; ++f) {
        const u8 *state;
        if (khdays_slots_state(f, &state) == KHDAYS_SLOT_SAVED &&
            (khdays_slots_field(state, 0xc77, 16) << 16 | khdays_slots_field(state, 0xc87, 16)) == counter) {
            return 1;
        }
    }
    return 0;
}

/* The whole file at `file`, NUL-ended; NULL when it is missing or cannot be read. */
static char *read_whole(const char *file, long *size)
{
    for (size_t room = 4096;; room *= 2) {
        char *text = (char *)malloc(room + 1);
        if (text == NULL) {
            return NULL;
        }
        *size = khdays_host_read_file(file, text, room);
        if (*size < 0) {
            free(text);
            return NULL;
        }
        if ((size_t)*size < room) {
            text[*size] = 0;
            return text;
        }
        free(text);
    }
}

static void write_kept(void)
{
    char *text;
    size_t at = 0;
    if (path[0] == 0 || (text = (char *)malloc((size_t)kept * 24 + 64)) == NULL) {
        return;
    }
    at += (size_t)snprintf(text + at, 64, "# the world of each save, by its save counter (saveworld.h)\n");
    for (int i = 0; i < kept; ++i) {
        at += (size_t)snprintf(text + at, 24, "%u %d\n", kept_counter[i], kept_world[i]);
    }
    if (!khdays_host_write_file_atomic(path, text, at)) {
        khdays_log("save world: %s could not be written", path);
    }
    free(text);
}

void khdays_saveworld_attach(const char *folder, u32 highest)
{
    char *text;
    long size;
    highest_seen = highest;
    if (!khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return;
    }
    snprintf(path, sizeof(path), "%s/save_worlds.txt", folder);
    AcquireSRWLockExclusive(&lock);
    kept = 0;
    text = read_whole(path, &size);
    if (text != NULL) {
        for (char *line = text; line != NULL && *line != 0;) {
            unsigned counter;
            int world;
            char *next = strchr(line, '\n');
            if (next != NULL) {
                *next++ = 0;
            }
            if (line[0] != '#' && sscanf(line, "%u %d", &counter, &world) == 2 && world >= 0 && world < KHDAYS_WORLDS &&
                kept_more()) {
                kept_counter[kept] = counter;
                kept_world[kept] = (unsigned char)world;
                ++kept;
            }
            line = next;
        }
        free(text);
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_saveworld_saved(u32 counter)
{
    const int world = world_now();
    int i;
    if (counter == 0 || counter <= highest_seen || !khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        return; /* (a write saving nothing new: a deletion, the settings) */
    }
    highest_seen = counter;
    AcquireSRWLockExclusive(&lock);
    /* the saves overwritten or deleted since let go */
    for (i = 0; i < kept;) {
        if (kept_counter[i] != counter && !counter_saved(kept_counter[i])) {
            kept_counter[i] = kept_counter[kept - 1];
            kept_world[i] = kept_world[kept - 1];
            --kept;
        } else {
            ++i;
        }
    }
    for (i = 0; i < kept && kept_counter[i] != counter; ++i) {
    }
    if (i < kept || kept_more()) {
        kept += i == kept;
        kept_counter[i] = counter;
        kept_world[i] = (unsigned char)world;
    }
    write_kept();
    ReleaseSRWLockExclusive(&lock);
    khdays_log("save world: save %u kept with world %d", counter, world);
}

int khdays_saveworld_of(u32 counter)
{
    int world = KHDAYS_WORLD_CASTLE;
    AcquireSRWLockShared(&lock);
    for (int i = 0; i < kept; ++i) {
        if (kept_counter[i] == counter) {
            world = kept_world[i];
            break;
        }
    }
    ReleaseSRWLockShared(&lock);
    return world;
}
