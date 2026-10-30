/* The Menu Overhaul's pause over an HD movie, a scene's videos and a day's monologue (modern_pause.c; opened by
 * runtime/movies/movie_hd.h's panel): Continue and Skip, in KH2's pause look. In place of the
 * DS's scene pause (movie_pause.h), with that mod only; and the game's own pause drawn as KH2's (game_pause.h). */
#ifndef KHDAYS_MODERN_PAUSE_SCREEN_H
#define KHDAYS_MODERN_PAUSE_SCREEN_H

#include <SDL3/SDL.h>

/* The window's thread: the panel over the picture while it is open; the renderer gone; a mouse event on the
 * panel (1: taken). */
void khdays_movie_pause_draw(SDL_Renderer *renderer);
void khdays_modern_pause_renderer_gone(void);
int khdays_movie_pause_event(SDL_Renderer *renderer, const SDL_Event *event);
/* Whether the window's point (wx, wy) is on the settings key a pause shows at its top right. */
int khdays_movie_pause_settings_at(SDL_Renderer *renderer, float wx, float wy);

#endif
