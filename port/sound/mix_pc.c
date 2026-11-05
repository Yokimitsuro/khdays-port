/* The PC's mixer (mix_pc.h): the DS's steps (mix_ds.c) at full precision -- divider, volume and each
 * side's share of the pan, channels 1 and 3 routed as SOUNDCNT says, the master volume -- divided by
 * 32768 and kept within [-1, 1], where the DS clips its 10 bits. */
#include "mix_pc.h"

#include <math.h>
#include <stddef.h>

#define LIMIT_CEILING 0.98f  /* the limiter's level: just under full scale */
#define LIMIT_RELEASE_S 0.1f /* its way back up, in seconds */

/* 7-bit volume and panning: 0..126 are N/128, 127 is 128/128. */
static float factor(uint32_t value)
{
    return (value == 127 ? 128.0f : (float)value) / 128.0f;
}

static float clip(float v)
{
    return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
}

void khdays_sound_limiter_init(KhdaysSoundLimiter *limiter, uint32_t rate)
{
    limiter->gain = 1.0f;
    limiter->release = rate != 0 ? 1.0f - expf(-1.0f / (LIMIT_RELEASE_S * (float)rate)) : 1.0f;
}

/* Both sides by one gain, so the picture of the sound stays where it is. */
static void limit(KhdaysSoundLimiter *limiter, float out[2])
{
    const float peak = fmaxf(fabsf(out[0]), fabsf(out[1]));
    const float needed = peak > LIMIT_CEILING ? LIMIT_CEILING / peak : 1.0f;
    if (needed < limiter->gain) {
        limiter->gain = needed;
    } else {
        limiter->gain += (needed - limiter->gain) * limiter->release;
    }
    out[0] *= limiter->gain;
    out[1] *= limiter->gain;
}

void khdays_sound_mix_pc(const KhdaysSoundHw *hw, const float *gain, float volume, const float extra[2],
                         KhdaysSoundLimiter *limiter, float out[2])
{
    static const float divider[4] = {1.0f, 2.0f, 4.0f, 16.0f};
    const uint32_t soundcnt = khdays_sound_hw_read(hw, 0x100, 2);
    float left[4] = {0.0f}, right[4] = {0.0f}, mix_left = 0.0f, mix_right = 0.0f; /* (channels 1 and 3 kept) */
    float master_left, master_right, master;

    for (int n = 0; n < khdays_sound_hw_count(hw); ++n) {
        const uint32_t cnt = khdays_sound_hw_control(hw, n);
        const float pan = factor((cnt >> 16) & 0x7f);
        const float sample = hw->smooth ? khdays_sound_hw_smooth_sample(hw, n) : (float)khdays_sound_hw_sample(hw, n);
        const float v = sample / divider[(cnt >> 8) & 3] * factor(cnt & 0x7f) * (gain != NULL ? gain[n] : 1.0f);
        if (n == 1 || n == 3) {
            left[n] = v * (1.0f - pan);
            right[n] = v * pan;
            if ((n == 1 && (soundcnt & 0x1000)) || (n == 3 && (soundcnt & 0x2000))) {
                continue; /* not to the mixer */
            }
        }
        mix_left += v * (1.0f - pan);
        mix_right += v * pan;
    }
    switch ((soundcnt >> 8) & 3) {
    case 0: master_left = mix_left; break;
    case 1: master_left = left[1]; break;
    case 2: master_left = left[3]; break;
    default: master_left = left[1] + left[3]; break;
    }
    switch ((soundcnt >> 10) & 3) {
    case 0: master_right = mix_right; break;
    case 1: master_right = right[1]; break;
    case 2: master_right = right[3]; break;
    default: master_right = right[1] + right[3]; break;
    }
    master = (soundcnt & 0x8000) ? factor(soundcnt & 0x7f) * volume / 32768.0f : 0.0f;
    out[0] = master_left * master + (extra != NULL ? extra[0] * volume : 0.0f);
    out[1] = master_right * master + (extra != NULL ? extra[1] * volume : 0.0f);
    if (limiter != NULL) {
        limit(limiter, out);
    }
    out[0] = clip(out[0]);
    out[1] = clip(out[1]);
}
