/* The day's calendar card on the screen (day_screen.c): what runtime/menus/daycard.h says the game's
 * card is, drawn over the whole window as the theater of KINGDOM HEARTS HD 1.5+2.5 ReMIX draws a
 * chapter's day -- DAY, the digits rolling from the day before to the day, the line and its ornaments,
 * the turning heart, the day's title -- in a mod's pieces (layouts/day/<lang>/day.txt,
 * tools/hd/theater_day.py). The window's thread only. */
#ifndef KHDAYS_DAY_SCREEN_H
#define KHDAYS_DAY_SCREEN_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draws the card over whatever was rendered when the game's shows; nothing otherwise. Returns whether
 * it drew. */
int khdays_day_screen_draw(SDL_Renderer *renderer);

/* The renderer is about to go: its textures are freed (made again on the next draw). */
void khdays_day_screen_renderer_gone(void);

#ifdef __cplusplus
}
#endif

#endif
