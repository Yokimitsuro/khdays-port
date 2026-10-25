/* The settings menu's sample sounds (settings_sample.cpp). While the menu has the game paused, the game is silent; so when a
 * volume changes, a sample of that kind of sound plays on a stream of its own,
 * at the level the mixer would give it. Runs on the window's thread. */
#ifndef KHDAYS_SETTINGS_SAMPLE_H
#define KHDAYS_SETTINGS_SAMPLE_H

#include "../app/profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The sound settings went from `before` to `now`: plays the sample of the
 * volume that changed, at its new level (the master volume plays the effects'
 * sample). */
void khdays_settings_sample(const KhdaysSound *before, const KhdaysSound *now);

/* The menu closed: stops the sample. */
void khdays_settings_sample_stop(void);

/* One of the menu sounds (KHDAYS_SETTINGS_CUE_*, settings_ui.h: the game's se_sys_click, kettei, open,
 * cancel) at the player's effects volume, for the port's own panels (the HD movies' pause). */
void khdays_menu_cue(int which);

/* Whether there is a movie sound to play as the sample of the movies volume. */
int khdays_settings_sample_has_movies(void);

#ifdef __cplusplus
}
#endif

#endif
