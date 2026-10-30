/* The mods Kingdom Hearts DS Project Editor makes: a .days file (a ZIP of mod.yml, its icon, and root/ -- the
 * game's files it replaces, as the card's filesystem names them) left in the mods folder is a mod of its own.
 * The port copies its files out into mods/.days/<its name>/files/ (where the VFS reads a mod's), again only when
 * the .days changes, each file checked against mod.yml's hash; a copy whose .days is gone is removed. */
#ifndef KHDAYS_MODS_DAYS_H
#define KHDAYS_MODS_DAYS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Every .days in `mods_path` copied out when it changed, the copies of the ones gone removed (each told in the
 * session log). Before the mods are listed (list.h). */
void khdays_days_unpack_all(const char *mods_path);

/* The .days mods the game started with, checked against the player's copy of the game (`nitrofs`: its files):
 * a mod made for another one (mod.yml's originalHash differing) is told in the session log. */
void khdays_days_check_originals(const char *nitrofs);

#ifdef __cplusplus
}
#endif

#endif
