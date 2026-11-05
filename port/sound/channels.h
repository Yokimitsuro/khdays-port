/* The DS's sixteen sound channels, as GBATEK describes them: their registers and what each plays; and the
 * voices the PC's mixer adds past them, channels alike that the DS did not have.
 * Plain C, no operating system and no SDL, so the tests, the offline tools and the sound driver each
 * keep a set of their own. The mixers read the channels' current samples. */
#ifndef KHDAYS_SOUND_CHANNELS_H
#define KHDAYS_SOUND_CHANNELS_H

#include <stdint.h>

/* The sound registers, 0x04000400-0x0400051f, as offsets from 0x04000400:
 * the channels' at 16 each (CNT +0, SAD +4, TMR +8, PNT +0xa, LEN +0xc),
 * SOUNDCNT 0x100, SOUNDBIAS 0x104, the capture registers 0x108-0x11f. */
#define KHDAYS_SOUND_REG_SIZE 0x120u

typedef struct KhdaysSoundChannel {
    uint32_t sad, pnt, len; /* as they were when the channel started */
    int busy;               /* the status bit */
    int playing;            /* still producing samples (the last one outlives busy) */
    int ended;              /* a one-shot sound that ran to its end (not stopped by the driver) */
    int32_t position;       /* the sample from SAD (ADPCM: the nibble); < 0 while starting */
    uint32_t counter;       /* the timer: a sample each time it passes 0xffff */
    int32_t sample;         /* what the channel outputs, PCM16 */
    int32_t history[3];     /* the samples before it, the last first (khdays_sound_hw_smooth_sample) */
    int32_t pcm, index;     /* ADPCM */
    int32_t loop_pcm, loop_index;
    int loop_saved;
    uint32_t noise;
    int source;      /* its samples' source's handle (KhdaysSoundSource), -1: its data in memory */
    uint64_t played; /* samples taken since it started */
    uint32_t starts; /* how many times it has started */
} KhdaysSoundChannel;

/* Where a channel's sample data is: the bytes at a DS address, or NULL for
 * an address the channel cannot read (it plays silence). */
typedef const uint8_t *(*KhdaysSoundMemory)(uint32_t address, int channel);

/* Samples from elsewhere than a channel's data in memory: a music stream the port reads itself
 * (stream_reader.h), a wave a mod replaces (runtime/sound/sound_mods.h). At each start of a PCM or
 * ADPCM channel the source is asked whether that data is its own; if so the channel's samples come
 * from it until it answers 0. */
typedef struct KhdaysSoundSource {
    void *context;
    int (*bind)(void *context, int channel, uint32_t sad, uint32_t len, int format); /* a handle, or -1 */
    int (*sample)(void *context, int handle, uint64_t index, int32_t *sample);       /* PCM16 */
} KhdaysSoundSource;

typedef struct KhdaysSoundHw {
    uint8_t regs[KHDAYS_SOUND_REG_SIZE];
    KhdaysSoundChannel channels[16];
    /* The voices past the sixteen (khdays_sound_hw_add_voice): channel 16 + v, its registers at offset
     * KHDAYS_SOUND_REG_SIZE + 16 v, laid out as a channel's. The driver adds them when `allow_voices` (the
     * PC's output: its mixer plays them, the DS's does not). */
    int voices;
    uint8_t *voice_regs;
    KhdaysSoundChannel *voice_channels;
    int allow_voices;
    KhdaysSoundMemory memory;
    const KhdaysSoundSource *source; /* NULL: every channel reads its memory */
    int trace;                       /* each channel start, on stderr */
    int smooth;                      /* the PC's mixer reads each channel between its samples */
    uint64_t clock;                  /* the cycle the trace prints, kept by the owner */
    uint32_t half;                   /* a cycle left over: the timers count at half the clock */
} KhdaysSoundHw;

/* The channels: the DS's sixteen, then the voices. */
static inline int khdays_sound_hw_count(const KhdaysSoundHw *hw)
{
    return 16 + hw->voices;
}

static inline KhdaysSoundChannel *khdays_sound_hw_channel(KhdaysSoundHw *hw, int n)
{
    return n < 16 ? &hw->channels[n] : &hw->voice_channels[n - 16];
}

static inline const KhdaysSoundChannel *khdays_sound_hw_channel_of(const KhdaysSoundHw *hw, int n)
{
    return n < 16 ? &hw->channels[n] : &hw->voice_channels[n - 16];
}

/* Channel n's registers' offset (its CNT). */
static inline uint32_t khdays_sound_hw_base(int n)
{
    return n < 16 ? (uint32_t)n * 16u : KHDAYS_SOUND_REG_SIZE + (uint32_t)(n - 16) * 16u;
}

/* The register byte at `offset` (a voice's past KHDAYS_SOUND_REG_SIZE). */
static inline const uint8_t *khdays_sound_hw_reg(const KhdaysSoundHw *hw, uint32_t offset)
{
    return offset < KHDAYS_SOUND_REG_SIZE ? hw->regs + offset : hw->voice_regs + (offset - KHDAYS_SOUND_REG_SIZE);
}

/* Adds a voice, stopped: its channel number, or -1 without the memory. */
int khdays_sound_hw_add_voice(KhdaysSoundHw *hw);

/* Power on: every register 0 and every channel stopped, no voices. The owner sets the
 * bias where the SoundBias BIOS call leaves it. */
void khdays_sound_hw_reset(KhdaysSoundHw *hw, KhdaysSoundMemory memory);

/* The registers, by offset; a write that sets a channel's start bit starts
 * it, one that clears it stops it. */
uint32_t khdays_sound_hw_read(const KhdaysSoundHw *hw, uint32_t offset, int size);
void khdays_sound_hw_write(KhdaysSoundHw *hw, uint32_t offset, int size, uint32_t value);

/* `cycles` of the DS clock pass: each playing channel's timer counts half
 * as many, and takes its next sample each time it overflows. */
void khdays_sound_hw_step(KhdaysSoundHw *hw, uint32_t cycles);

/* The samples a channel plays from `words` words of data in memory the
 * first time through, as PCM16, into `out`: PCM8 (format 0) times 256,
 * PCM16 (1), or IMA-ADPCM (2) after its header word, decoded as the
 * channel decodes it. Returns how many (4, 2 or 8 a word; ADPCM's header
 * word gives none). */
uint32_t khdays_sound_decode(const uint8_t *data, uint32_t words, int format, int16_t *out);

/* A channel's current output, PCM16, and its CNT register. */
static inline int32_t khdays_sound_hw_sample(const KhdaysSoundHw *hw, int n)
{
    return khdays_sound_hw_channel_of(hw, n)->sample;
}

/* How far channel `n`'s timer is towards its next sample, 0 to 1. */
float khdays_sound_hw_between(const KhdaysSoundHw *hw, int n);

/* A channel's output now read between its samples, as a PC plays sound rather than holding each sample
 * as the DS does: cubic (Catmull-Rom) from its last four, so two of its samples late; a square wave or
 * noise as it is. */
float khdays_sound_hw_smooth_sample(const KhdaysSoundHw *hw, int n);

static inline uint32_t khdays_sound_hw_control(const KhdaysSoundHw *hw, int n)
{
    const uint8_t *r = khdays_sound_hw_reg(hw, khdays_sound_hw_base(n));
    return (uint32_t)r[0] | (uint32_t)r[1] << 8 | (uint32_t)r[2] << 16 | (uint32_t)r[3] << 24;
}

#endif
