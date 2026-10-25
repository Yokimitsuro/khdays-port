/* The world of each save, for the Menu Overhaul's save page (camp_screen.c), as KH2's list shows where it was
 * saved: Days's save holds none, and the story saves in the Castle's Grey Area. The mission played last before
 * the save, from the game's reads of the worlds' archives (mi/wd/wd_<code>), kept beside the saves by their
 * save counter (saveworld.c; nothing without that mod). */
#ifndef KHDAYS_SAVEWORLD_H
#define KHDAYS_SAVEWORLD_H

#include "../../hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The worlds the list shows a picture of (the Holo-Missions world map's models); CASTLE the Castle That
 * Never Was and The World That Never Was, and any save the port saw no mission before. */
enum {
    KHDAYS_WORLD_CASTLE,
    KHDAYS_WORLD_TWILIGHT_TOWN,
    KHDAYS_WORLD_AGRABAH,
    KHDAYS_WORLD_BEASTS_CASTLE,
    KHDAYS_WORLD_OLYMPUS,
    KHDAYS_WORLD_WONDERLAND,
    KHDAYS_WORLD_HALLOWEEN_TOWN,
    KHDAYS_WORLD_NEVERLAND,
    KHDAYS_WORLDS
};

/* The saves' folder (saveslots.c's): the worlds kept there are read; `highest` the saves' highest
 * counter now. */
void khdays_saveworld_attach(const char *folder, u32 highest);
/* The game's thread: a read of the card at `offset` (runtime/core/rom.cpp khdays_rom_read). */
void khdays_saveworld_card_read(u32 offset);
/* The game's thread: a save written (arm7.c khdays_backup_commit), its counter `counter` (the camp's
 * BeginSaveToSlot's: fields 0xc77 and 0xc87): the world played last kept for it. */
void khdays_saveworld_saved(u32 counter);
/* The world of the save of counter `counter` (KHDAYS_WORLD_*). Any thread. */
int khdays_saveworld_of(u32 counter);
/* The world played last now (KHDAYS_WORLD_*), for traces. */
int khdays_saveworld_now(void);

#ifdef __cplusplus
}
#endif

#endif
