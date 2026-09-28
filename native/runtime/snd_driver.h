/* The ARM7's sound driver, the game's own, run in the interpreter
 * (snd_driver.c). */
#ifndef KHDAYS_SND_DRIVER_H
#define KHDAYS_SND_DRIVER_H

#include "../hal/hal.h"

/* Loads the driver from the ARM7 binary and starts it, as the ARM7's main
 * does at boot. Returns 0, and says why, when there is no sound
 * (KHDAYS_SOUND=0, or an ARM7 binary other than the one the driver's
 * addresses were read from). Needs the clock running. */
int khdays_snd_driver_init(void);

/* A word the ARM9 sent on the sound's PXI tag (7); 0 when the driver is
 * not running. */
int khdays_snd_driver_pxi(u32 data, u32 err);

/* Runs the ARM7's side up to `cycle`: its alarms (the driver's 5.2 ms
 * tick among them) and the sound hardware. */
void khdays_snd_driver_update(u64 cycle);

/* The cycle of the ARM7's next alarm (~0 when none). */
u64 khdays_snd_driver_next_event(void);

#endif
