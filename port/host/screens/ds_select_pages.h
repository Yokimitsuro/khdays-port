/* Mission Mode's character select as the game draws it (no layout of the HD pieces), on the mods' characters'
 * pages (runtime/menus/charselect.h, `ds_page` 1 and on): over the lower screen's grid, drawn by the presenter in
 * the picture's own units. */
#ifndef KHDAYS_DS_SELECT_PAGES_H
#define KHDAYS_DS_SELECT_PAGES_H

#include <SDL3/SDL.h>

/* Draws the page over the lower screen, whose top left is (x0, y0) in the renderer's units, `sx` and `sy` of them a
 * DS pixel. Nothing on the game's own page or elsewhere. */
void khdays_ds_select_pages_draw(SDL_Renderer *renderer, float x0, float y0, float sx, float sy);

/* The renderer went away: its faces with it. */
void khdays_ds_select_pages_renderer_gone(void);

#endif
