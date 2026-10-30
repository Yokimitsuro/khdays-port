/* The Moogle Shop on one screen (shop_screen.c): the story's shop and Mission Mode's, as the game shows
 * them (runtime/menus/shop.h), drawn over the one-screen picture -- which shows the screen's backdrop (the
 * DS's stripes, a pattern of moogles drifting, the moogle's shadow) and under it the game's moogle, its
 * 3D moved into the screen's window for it (onescreen.c) -- from a mod's layouts/shop. */
#ifndef KHDAYS_HOST_SHOP_SCREEN_H
#define KHDAYS_HOST_SHOP_SCREEN_H

#include "../../ui/composition.h"

#include <SDL3/SDL.h>
#include <stdint.h>

/* What the one-screen picture needs: the DS's frame of the moogle (its upper screen's dots: every
 * polygon there is the moogle's), the part of it shown, the screen's window for it (1920 x 1080); the
 * backdrop's atlas (RGBA, red first; kept while the port runs), the ground beyond it and this frame's
 * pieces of it (a 1920 x 1080 animated picture: ui/composition.h). */
typedef struct KhdaysShopModel {
    float source[4], view[4], window[4];
    const uint8_t *atlas;
    int atlas_w, atlas_h;
    uint32_t ground;
    const KhdaysPiece *pieces;
    int piece_count;
} KhdaysShopModel;

/* The window's thread: whether the screen shows the shop now, and its picture; and whether the one-screen
 * picture took it (the screen draws only over that picture). */
int khdays_shop_screen_model(KhdaysShopModel *out);
void khdays_shop_screen_applied(int yes);
/* Whether the last picture was the shop's (its timings in the session log: gpu_present.c). */
int khdays_shop_screen_shown(void);
/* The window's thread, at each of the game's pictures: whether the screens are one (the moogle is turned
 * to the menu, from the moment the shop opens, when the screen can show it). */
void khdays_shop_screen_wanted(int one_screen);
/* The window's thread: what the screen wants of the DS's screens while a list is up (0 none: the screens
 * float out as they last were) -- the list and the counter without the DS's stripes (`alone`), Synthesize's
 * recipe up, where the counter and the recipe are on the upper screen (its dots), a number that changes
 * with what is shown (another list, its tab, the recipe); then what was made of them (onescreen.c, a thread
 * of its own: the newest of the game's pictures): each screen as the DS shows it and without its stripes
 * (0xAARRGGBB, transparent where nothing is), NULL when not made (copied). */
typedef struct KhdaysShopRequest {
    int alone, recipe, tab; /* (tab: the list's, 0 Buy .. 3 Redeem, for the session log) */
    float header[4], recipe_rect[4];
    unsigned generation;
} KhdaysShopRequest;
int khdays_shop_screen_request(KhdaysShopRequest *out);
/* The window's thread, after the results below: the DS's screens the page draws this picture, to be composed on
 * the card with their HD tiles and fonts (host/render/gfx_render.h khdays_gfx_render_shop): by screen (0 the
 * upper) which (KHDAYS_SHOP_WANT_* bits; none while no list is up: those last made are held, floating out),
 * whether as the DS shows it the screen keeps its fade, and their texels a DS dot (0: the most). */
void khdays_shop_screen_card(unsigned wants[2], int fade[2], int *scale);
void khdays_shop_screen_results(const uint32_t *upper, const uint32_t *lower, const uint32_t *upper_cut,
                                const uint32_t *lower_cut);
/* The window's thread: draws the screen over the window while the shop is open; 0 when it does not show. */
int khdays_shop_screen_draw(SDL_Renderer *renderer);
/* Last, after the camp's menu (camp_screen.c): Mission Mode's transitions between the two (below); 1 while
 * one draws (the window drawn at the display's rate). */
int khdays_shop_screen_draw_over(SDL_Renderer *renderer);

/* Mission Mode's camp menu and the shop into each other, without the game's black between them
 * (camp_screen.c): whether the shop's screen can show it; the camp's menu, its Shop chosen, hands the
 * window over (called in its drawing, with the window as it shows: kept and shown until the shop shows);
 * and whether the window is being handed over to the shop or back to the menu. */
enum { KHDAYS_SHOP_HAND_IN = 1, KHDAYS_SHOP_HAND_OUT = 2 };
int khdays_shop_screen_ready(void);
void khdays_shop_screen_take_over(SDL_Renderer *renderer);
int khdays_shop_screen_handing(void);
/* The renderer is gone: its textures with it. */
void khdays_shop_screen_renderer_gone(void);

#endif
