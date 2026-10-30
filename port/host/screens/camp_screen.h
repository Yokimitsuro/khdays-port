/* Mission Mode's camp menu on one screen (camp_screen.c): drawn over the one-screen picture, which
 * shows the screen's backdrop and the game's 3D model moved into the screen's window for it
 * (onescreen.c), while the game is on its menu page (runtime/menus/camp.h). */
#ifndef KHDAYS_HOST_CAMP_SCREEN_H
#define KHDAYS_HOST_CAMP_SCREEN_H

#include "../../ui/composition.h"

#include <SDL3/SDL.h>
#include <stdint.h>

/* What the one-screen picture needs for the menu: the DS's frame of the model (its upper screen's
 * dots: every polygon in it is the model's) and the part of it the window shows, the screen's
 * window for it (1920 x 1080), the backdrop (RGBA, red first; kept while the port runs); `model`
 * 0 on the Preferences page: its own backdrop, nothing of the game's 3D. The Menu Overhaul's story menu has
 * an animated picture instead (`atlas` not NULL: ui/composition.h), this frame's `pieces` of it on `ground`. */
typedef struct KhdaysCampModel {
    float source[4], view[4], window[4];
    const uint8_t *backdrop;
    int backdrop_w, backdrop_h;
    int model;
    int files; /* the story's Files page: the screen wants the DS's upper screen (khdays_camp_screen_upper) */
    const uint8_t *atlas;
    int atlas_w, atlas_h;
    uint32_t ground;
    const KhdaysPiece *pieces;
    int piece_count;
    /* the story's Panels page: no model; the DS's lower screen as a card over the animated picture instead (its
     * grid, its list, its 3D panels in it): its dots `lower_src` at `lower_card` (1920 x 1080; w 0 none) at
     * `lower_alpha`. `panels_upper`: the screen wants the DS's upper screen (what X shows there) composed apart
     * (khdays_camp_screen_panels_upper) */
    int cards;
    float lower_src[4], lower_card[4];
    float lower_alpha;
    int panels_upper;
    int panels_unslide; /* that screen's slide (dots, page B's): composed slid back by it, its list shown */
} KhdaysCampModel;

/* The window's thread: whether the screen shows now, and its model's window; and whether the
 * one-screen picture took it (the screen draws only over that picture). */
int khdays_camp_screen_model(KhdaysCampModel *out);
void khdays_camp_screen_applied(int yes);
/* The window's thread, for the Files page: the DS's upper screen as the package composed it (256 x 192,
 * 0x00RRGGBB; copied; NULL: composed on the card alone), the page's entry drawn in the screen's own frame. Whether
 * the page takes it now (not while it goes: the screen kept as it was), to compose it on the card too, with the
 * HD tiles and fonts (host/render/gfx_render.h khdays_gfx_render_shop), at *scale texels a DS dot (0: the most). */
void khdays_camp_screen_upper(const uint32_t *pixels);
int khdays_camp_screen_upper_wanted(int *scale);
/* The Panels page's (any thread: the one-screen picture's thread composing it apart): the DS's upper screen as
 * composed (256 x 192, 0x00RRGGBB, without the game's fade; copied). The window's thread: whether the last one
 * taken this visit showed the list, to compose it on the card too (as above). */
void khdays_camp_screen_panels_upper(const uint32_t *pixels);
int khdays_camp_screen_panels_listed(int *scale);
/* The window's thread: draws the screen over the window when the game is on its menu page; 0 when
 * it does not show. */
int khdays_camp_screen_draw(SDL_Renderer *renderer);
/* Whether the screen drew the camp's save page at its last picture (the port's save list in the camp's
 * look): the port's own save list (save_screen.c) is not drawn under it. */
int khdays_camp_screen_draws_save(void);
/* The renderer is gone: its textures with it. */
void khdays_camp_screen_renderer_gone(void);

/* With the Menu Overhaul (runtime/mods/installed.h) the four above are its camp's (overhaul_camp_screen.c): the
 * story's menu, its Panels, Files and save pages, the transitions, and Mission Mode's menu in its look. The
 * functions before them in this header are that camp's alone. */
int khdays_overhaul_camp_model(KhdaysCampModel *out);
void khdays_overhaul_camp_applied(int yes);
int khdays_overhaul_camp_draw(SDL_Renderer *renderer);
void khdays_overhaul_camp_renderer_gone(void);

#endif
