/* Sounds the port starts itself, beside the game's: a file decoded once and heard among the effects (their volume),
 * from its start to its end, at a volume and a pan -- a KH2 enemy's sounds (docs/KH2_ENEMIES.md, "Sounds"). The
 * game's thread starts them, the sound's thread mixes them; either may call while the other does. */
#ifndef KHDAYS_SOUND_SHOTS_H
#define KHDAYS_SOUND_SHOTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* `path` (a WAV, FLAC or Ogg file) decoded once and kept: its number, -1 when it cannot be (said once). */
int khdays_sound_shot_file(const char *path);

/* File `file` heard from its start at `volume` (1 the file's own) and `pan` (-1 left, 0 the middle, 1 right): 0 when
 * `file` is no file's. */
int khdays_sound_shot_start(int file, float volume, float pan);

/* The same, a number back for the one sound (0 none): whether it is still heard, and it ended at once (a KH2 enemy's
 * voice line, which the next of a priority as high takes the place of). */
int khdays_sound_shot_play(int file, float volume, float pan);
int khdays_sound_shot_heard(int id);
void khdays_sound_shot_end(int id);

/* Every sound heard stopped (an area left). */
void khdays_sound_shots_stop(void);

/* Whether any is heard now. */
int khdays_sound_shots_playing(void);

/* The sound's thread: one output sample at `rate` added to `out` (the PC mixer's scale) under `gain` (the effects'
 * volume); those at their end let go. */
void khdays_sound_shots_mix(uint32_t rate, float gain, float out[2]);

#ifdef __cplusplus
}
#endif

#endif
