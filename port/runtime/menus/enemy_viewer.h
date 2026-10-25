/* The story camp's Enemy Profiles page (camp.c's Files pages, mode 1): the chosen enemy's model, the
 * game's own, shown whole in the middle of the window, turning, zooming and playing its calmest animation (its idle, mostly)
 * (enemy_viewer.c); host/screens/camp_screen.c darkens the page behind it and shows its controls. */
#ifndef KHDAYS_ENEMY_VIEWER_H
#define KHDAYS_ENEMY_VIEWER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysEnemyViewer {
    int open;  /* asked for (X on a met enemy) until closed (B or X) */
    int ready; /* its model loaded and drawn: the window's screen shows it */
    int enemy; /* its class (the enm_&.z record's first byte, Ms/<class>.p) */
    int anim;  /* the animation playing, of `anims` */
    int anims;
    unsigned serial;  /* a new one at each opening, and at each enemy shown */
    int row;          /* the page's row of the enemy shown (L/R step to the met ones before and after) */
    unsigned opening; /* a new one at each opening */
} KhdaysEnemyViewer;

#define KHDAYS_ENEMY_VIEWER_ROWS 192

/* The game's thread, at each update of the story camp: the Files page's state (`on` while it runs;
 * `enemies` its mode is Enemy Profiles; its cursor's row, its rows' enemies' classes, -1 not met).
 * Opens, draws and closes the viewer. */
void khdays_enemy_viewer_update(int on, int enemies, int cursor, int rows, const int16_t *ids);
/* The game's thread, at Pad_Sample: the buttons the game gets (none while the viewer is open; X
 * opens it). */
uint16_t khdays_enemy_viewer_buttons(uint16_t buttons);
/* Any thread. */
void khdays_enemy_viewer_read(KhdaysEnemyViewer *out);

#ifdef __cplusplus
}
#endif

#endif
