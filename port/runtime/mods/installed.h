/* Mods that switch on features of the port's own (installed.c): a folder of mods/ holding days-mods/<id>.txt, its
 * pieces beside it. Without the mod the port behaves as without the feature. */
#ifndef KHDAYS_MODS_INSTALLED_H
#define KHDAYS_MODS_INSTALLED_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    KHDAYS_MOD_MENU_OVERHAUL, /* days-mods/menu-overhaul.txt: the Menu Overhaul's screens, transitions and pauses */
    KHDAYS_MOD_WALK,          /* days-mods/walk.txt: the analog walk's own animation (runtime/input/walk.h) */
    KHDAYS_MODS
};

/* Any thread: whether the mod is installed (looked for once). */
int khdays_mod_installed(int mod);
/* The mod named in the compositions' conditions (`mod-menu-overhaul`), -1 none. */
int khdays_mod_named(const char *name);

#ifdef __cplusplus
}
#endif

#endif
