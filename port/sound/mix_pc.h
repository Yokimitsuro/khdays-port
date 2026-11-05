/* The PC's mixer, the default: the same channels as the DS's mixer (mix_ds.h), each read at the
 * instant of each output sample at the PC's rate -- between its samples when the hardware's `smooth` is
 * set (channels.h), as it is held otherwise, as on the DS -- mixed in floating point without the DS's
 * 10-bit rounding or bias, then kept within full scale. Its scale is the DS mixer's before it rounds,
 * divided by 32768. */
#ifndef KHDAYS_SOUND_MIX_PC_H
#define KHDAYS_SOUND_MIX_PC_H

#include "channels.h"

/* A limiter on the mix: a sound louder than full scale is turned down at once, just enough, and back up
 * over a tenth of a second, rather than clipped (a clip crackles); below full scale it changes nothing. */
typedef struct KhdaysSoundLimiter {
    float gain;    /* what the output is multiplied by now, 1 at rest */
    float release; /* how much of the way back to 1 each output sample goes */
} KhdaysSoundLimiter;

void khdays_sound_limiter_init(KhdaysSoundLimiter *limiter, uint32_t rate);

/* The output sample the channels make now, left and right, in [-1, 1].
 * `gain` (NULL: none) scales each channel and `volume` the whole: the
 * player's volumes (bus.h). `extra` (NULL: none), at the same scale, is
 * added before the clip, under `volume` too: the replaced sounds'.
 * `limiter` (NULL: clipped at full scale) keeps the result within it. */
void khdays_sound_mix_pc(const KhdaysSoundHw *hw, const float *gain, float volume, const float extra[2],
                         KhdaysSoundLimiter *limiter, float out[2]);

#endif
