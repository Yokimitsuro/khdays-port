/* The PC side: a window showing the two screens and the input it collects
 * (window.c, SDL3 on a thread of its own, so nothing of it runs on the game's
 * fibers). */
#ifndef KHDAYS_HOST_H
#define KHDAYS_HOST_H

#include <stdint.h>

/* Opens the window, unless KHDAYS_HEADLESS is set. Returns 0 on failure. */
int khdays_host_start(void);

/* Closes the window and the sound, and waits until they are (a reset). */
void khdays_host_close(void);

/* A finished frame: both screens, 256x192 each, 0x00RRGGBB. Copied. */
void khdays_host_present(const uint32_t *upper, const uint32_t *lower);

/* The DS buttons held (KHDAYS_KEY_* bits, runtime/input.h). */
uint16_t khdays_host_buttons(void);

/* The mouse held on the lower screen, in its pixels; 0 when not touching. */
int khdays_host_touch(int *x, int *y);

/* Sound: `count` stereo frames (left, right) at KHDAYS_SOUND_RATE
 * (runtime/sound.h), from the game's thread. */
void khdays_host_audio(const int16_t *frames, int count);

#endif
