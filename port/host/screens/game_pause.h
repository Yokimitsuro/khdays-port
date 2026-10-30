/* The game's own pause (a DS scene's: PAUSE, Continue, Skip) drawn as Kingdom Hearts II's by the
 * window, as the HD videos' pause is (movie_pause.c) -- a scene's (PAUSE, Continue, Skip) and a
 * mission's (Continue, Retry, Withdraw, its question): the pause part (ui/pause.cpp) tells it, on the
 * game's thread, what the game's pause shows, and leaves the game's own pieces out. */
#ifndef KHDAYS_GAME_PAUSE_H
#define KHDAYS_GAME_PAUSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the window draws KH2's pause now (one picture, and the HD PACK has KH2's pause). */
int khdays_game_pause_kh2_ready(void);

/* What the game's pause shows (game's thread, each frame): `shown` 0 when it is not up; its `rows` (0
 * the title alone, up to 3) with their words `keys` (runtime/menus/retry.h KHDAYS_PAUSE_*); `row` the
 * one lit; `question` "Are you sure?" over them; `language` the game's (1-5). */
void khdays_game_pause_publish(int shown, int rows, const int *keys, int row, int question, int language);

/* The same for the game over's menu (runtime/menus/retry.h KhdaysGameOverView): drawn as KH2's pause
 * without its title, over the game over's picture. */
void khdays_game_over_publish(int shown, int rows, const int *keys, int row, int question, int language);

/* Whether the window draws the game's pause (or its game over's menu) as KH2's now: it animates, so the window draws it at the
 * display's rate whether or not the paused game sends pictures (window thread). */
int khdays_game_pause_kh2_showing(void);

#ifdef __cplusplus
}
#endif

#endif
