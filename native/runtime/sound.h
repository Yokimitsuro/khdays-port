/* The ARM7's sound hardware (GBATEK "DS Sound"): sixteen channels, the
 * mixer and the capture units, run on the DS clock; what it outputs goes to
 * the host (sound.c). */
#ifndef KHDAYS_SOUND_H
#define KHDAYS_SOUND_H

#include "../hal/hal.h"

/* The output: one stereo sample per 1024 cycles of the DS clock, which is
 * how the 10-bit PWM output divides it (33513982 / 1024 = 32728.5 Hz). */
#define KHDAYS_SOUND_CYCLES_PER_SAMPLE 1024u
#define KHDAYS_SOUND_RATE 32728

/* Power on at `cycle`: every channel stopped, the bias where the SoundBias
 * BIOS call leaves it. */
void khdays_sound_reset(u64 cycle);

/* The ARM7's accesses to its I/O registers (0x04000400-0x0400051f are the
 * sound's; anything else stops the run, as nothing else is modelled). */
u32 khdays_sound_io_read(u32 address, int size);
void khdays_sound_io_write(u32 address, int size, u32 value);

/* Runs the hardware up to `cycle`, sending what it outputs to the host. */
void khdays_sound_run(u64 cycle);

#endif
