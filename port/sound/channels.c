/* The DS's sixteen sound channels (channels.h) as GBATEK describes them: PCM8, PCM16, IMA-ADPCM, PSG
 * and noise, each start's delay, one-shot and looped sounds (docs/SOUND.md, "The channels and the
 * mixers"). The capture units are not modelled: a capture that starts is reported. */
#include "channels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PCM8, PCM16, ADPCM, PSG };
enum { MANUAL, LOOP, ONE_SHOT };

#define REG8(offset) (*(uint8_t *)khdays_sound_hw_reg(hw, (offset)))
#define REG16(offset) (*(uint16_t *)khdays_sound_hw_reg(hw, (offset)))
#define REG32(offset) (*(uint32_t *)khdays_sound_hw_reg(hw, (offset)))
#define BASE(n) khdays_sound_hw_base(n)

/* IMA-ADPCM (GBATEK "DS Sound Notes"). */
static const int8_t adpcm_index_table[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const uint16_t adpcm_table[89] = {
    0x0007, 0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x0010, 0x0011, 0x0013, 0x0015, 0x0017,
    0x0019, 0x001c, 0x001f, 0x0022, 0x0025, 0x0029, 0x002d, 0x0032, 0x0037, 0x003c, 0x0042, 0x0049, 0x0050,
    0x0058, 0x0061, 0x006b, 0x0076, 0x0082, 0x008f, 0x009d, 0x00ad, 0x00be, 0x00d1, 0x00e6, 0x00fd, 0x0117,
    0x0133, 0x0151, 0x0173, 0x0198, 0x01c1, 0x01ee, 0x0220, 0x0256, 0x0292, 0x02d4, 0x031c, 0x036c, 0x03c3,
    0x0424, 0x048e, 0x0502, 0x0583, 0x0610, 0x06ab, 0x0756, 0x0812, 0x08e0, 0x09c3, 0x0abd, 0x0bd0, 0x0cff,
    0x0e4c, 0x0fba, 0x114c, 0x1307, 0x14ee, 0x1706, 0x1954, 0x1bdc, 0x1ea5, 0x21b6, 0x2515, 0x28ca, 0x2cdf,
    0x315b, 0x364b, 0x3bb9, 0x41b2, 0x4844, 0x4f7e, 0x5771, 0x602f, 0x69ce, 0x7462, 0x7fff,
};

/* Each oddity once, so the model grows from what the driver really does. */
static void report(const char *what, int channel)
{
    static const char *seen[32];
    static int count;
    for (int i = 0; i < count; ++i) {
        if (seen[i] == what) {
            return;
        }
    }
    if (count < 32) {
        seen[count++] = what;
    }
    fprintf(stderr, "sound: %s (channel %d)\n", what, channel);
    fflush(stderr);
}

static const uint8_t *memory(const KhdaysSoundHw *hw, uint32_t address, int channel)
{
    return hw->memory != NULL ? hw->memory(address, channel) : NULL;
}

static uint32_t control(const KhdaysSoundHw *hw, int n)
{
    return REG32(BASE(n));
}

static void set_busy(KhdaysSoundHw *hw, int n, int busy)
{
    khdays_sound_hw_channel(hw, n)->busy = busy;
    if (busy) {
        REG8(BASE(n) + 3) |= 0x80;
    } else {
        REG8(BASE(n) + 3) &= 0x7f;
    }
}

/* The channel stops: its output falls to zero, or stays at the last sample
 * while Hold is set. */
static void silence(KhdaysSoundHw *hw, int n)
{
    KhdaysSoundChannel *c = khdays_sound_hw_channel(hw, n);
    c->playing = 0;
    if (!(control(hw, n) & 0x8000)) {
        c->sample = 0;
    }
}

static void start(KhdaysSoundHw *hw, int n)
{
    KhdaysSoundChannel *c = khdays_sound_hw_channel(hw, n);
    const uint32_t cnt = control(hw, n);
    const uint32_t format = (cnt >> 29) & 3;
    /* the whole address: the DS register's 27 bits would cut the game's heaps',
     * which hold its sounds; a DS address is the same either way */
    c->sad = REG32(BASE(n) + 4) & ~3u;
    c->pnt = REG16(BASE(n) + 0xa);
    c->len = REG32(BASE(n) + 0xc) & 0x3fffffu;
    c->counter = REG16(BASE(n) + 8);
    c->busy = 1;
    c->playing = 1;
    c->ended = 0;
    c->sample = 0;
    memset(c->history, 0, sizeof(c->history));
    c->loop_saved = 0;
    c->played = 0;
    ++c->starts;
    c->source = hw->source != NULL && format != PSG
                    ? hw->source->bind(hw->source->context, n, c->sad, c->len, (int)format)
                    : -1;
    if (hw->trace) {
        fprintf(stderr, "sound: %llu ch%d start cnt %08x sad %08x tmr %04x pnt %04x len %06x\n",
                (unsigned long long)hw->clock, n, cnt, c->sad, c->counter, c->pnt, c->len);
    }
    if (format == PSG) {
        c->position = -1;
        c->noise = 0x7fff;
        if (n < 8) {
            report("PSG format on a channel without PSG or noise, silent", n);
        }
        return;
    }
    c->position = -3;
    if (c->pnt + c->len < 4) {
        /* GBATEK: under 4 words the channel hangs, busy, with no output. */
        report("a sound shorter than 4 words (hangs, as on the DS)", n);
    }
    if (format == ADPCM) {
        const uint8_t *header = memory(hw, c->sad, n);
        const uint32_t word = header != NULL ? *(const uint32_t *)header : 0;
        c->pcm = (int16_t)word;
        c->index = (word >> 16) & 0x7f;
        if (c->index > 88) {
            report("an ADPCM header with a table index over 88, taken as 88", n);
            c->index = 88;
        }
    }
    if (((cnt >> 27) & 3) == MANUAL || ((cnt >> 27) & 3) == 3) {
        report("repeat mode 0 or 3, which GBATEK leaves undefined; played as one-shot", n);
    }
}

static int32_t adpcm_step(KhdaysSoundChannel *c, uint32_t nibble)
{
    const int32_t step = adpcm_table[c->index];
    int32_t diff = step >> 3;
    if (nibble & 1)
        diff += step >> 2;
    if (nibble & 2)
        diff += step >> 1;
    if (nibble & 4)
        diff += step;
    if (nibble & 8) {
        c->pcm -= diff;
        if (c->pcm < -0x7fff)
            c->pcm = -0x7fff;
    } else {
        c->pcm += diff;
        if (c->pcm > 0x7fff)
            c->pcm = 0x7fff;
    }
    c->index += adpcm_index_table[nibble & 7];
    if (c->index < 0)
        c->index = 0;
    if (c->index > 88)
        c->index = 88;
    return c->pcm;
}

uint32_t khdays_sound_decode(const uint8_t *data, uint32_t words, int format, int16_t *out)
{
    uint32_t n = 0;
    switch (format) {
    case PCM8:
        for (; n < words * 4; ++n) {
            out[n] = (int16_t)((int8_t)data[n] * 256);
        }
        return n;
    case PCM16:
        for (; n < words * 2; ++n) {
            out[n] = (int16_t)(data[2 * n] | data[2 * n + 1] << 8);
        }
        return n;
    case ADPCM: {
        KhdaysSoundChannel c;
        if (words < 1) {
            return 0;
        }
        memset(&c, 0, sizeof(c));
        c.pcm = (int16_t)(data[0] | data[1] << 8);
        c.index = data[2] & 0x7f;
        if (c.index > 88) {
            c.index = 88; /* as start() takes it */
        }
        for (; n < (words - 1) * 8; ++n) {
            out[n] = (int16_t)adpcm_step(&c, (data[4 + n / 2] >> ((n & 1) * 4)) & 0xf);
        }
        return n;
    }
    default: return 0;
    }
}

/* The channel's timer overflowed: its next sample. */
static void advance(KhdaysSoundHw *hw, int n)
{
    KhdaysSoundChannel *c = khdays_sound_hw_channel(hw, n);
    const uint32_t cnt = control(hw, n);
    const uint32_t format = (cnt >> 29) & 3;
    const uint32_t per_word = format == PCM8 ? 4 : format == PCM16 ? 2 : 8;
    int32_t total, loop_start;
    const uint8_t *data;

    c->history[2] = c->history[1];
    c->history[1] = c->history[0];
    c->history[0] = c->sample;
    ++c->position;
    if (format == PSG) {
        if (n >= 8 && n <= 13) {
            /* eight steps, starting at the LOW part: HIGH for (duty + 1)/8 */
            const uint32_t duty = (cnt >> 24) & 7;
            c->sample = duty != 7 && (uint32_t)(c->position & 7) >= 7 - duty ? 0x7fff : -0x7fff;
        } else if (n >= 14 && n < 16) {
            if (c->noise & 1) {
                c->noise = (c->noise >> 1) ^ 0x6000;
                c->sample = -0x7fff;
            } else {
                c->noise >>= 1;
                c->sample = 0x7fff;
            }
        }
        return;
    }
    if (c->position < 0 || c->pnt + c->len < 4) {
        return; /* the start delay; or hung */
    }
    total = (int32_t)((c->pnt + c->len) * per_word);
    loop_start = (int32_t)(c->pnt * per_word);
    if (c->position >= total) {
        if (((cnt >> 27) & 3) != LOOP) {
            c->ended = 1;
            silence(hw, n);
            return;
        }
        c->position = loop_start;
        if (format == ADPCM) {
            if (!c->loop_saved) {
                report("an ADPCM loop start the sound never reached", n);
            }
            c->pcm = c->loop_pcm;
            c->index = c->loop_index;
        }
    }
    if (((cnt >> 27) & 3) != LOOP && c->position == total - 1) {
        set_busy(hw, n, 0); /* the busy bit clears as the last sample begins */
    }
    if (format == ADPCM && c->position < 8) {
        c->sample = 0; /* the header's time */
        return;
    }
    if (format == ADPCM && c->position == loop_start && !c->loop_saved) {
        c->loop_pcm = c->pcm;
        c->loop_index = c->index;
        c->loop_saved = 1;
    }
    if (c->source >= 0) {
        int32_t sample;
        if (hw->source->sample(hw->source->context, c->source, c->played++, &sample)) {
            c->sample = sample;
            return;
        }
        c->source = -1;
    }
    switch (format) {
    case PCM8:
        data = memory(hw, c->sad + (uint32_t)c->position, n);
        c->sample = data != NULL ? (int32_t)(int8_t)*data * 256 : 0;
        break;
    case PCM16:
        data = memory(hw, c->sad + (uint32_t)c->position * 2, n);
        c->sample = data != NULL ? (int32_t)*(const int16_t *)data : 0;
        break;
    default:
        data = memory(hw, c->sad + ((uint32_t)c->position >> 1), n);
        c->sample = data != NULL ? adpcm_step(c, (*data >> ((c->position & 1) * 4)) & 0xf) : c->pcm;
        break;
    }
}

/* --- Registers ------------------------------------------------------------------ */

void khdays_sound_hw_reset(KhdaysSoundHw *hw, KhdaysSoundMemory memory_of)
{
    free(hw->voice_regs);
    free(hw->voice_channels);
    memset(hw, 0, sizeof(*hw));
    hw->memory = memory_of;
}

int khdays_sound_hw_add_voice(KhdaysSoundHw *hw)
{
    uint8_t *regs = (uint8_t *)realloc(hw->voice_regs, (size_t)(hw->voices + 1) * 16);
    KhdaysSoundChannel *channels;
    if (regs == NULL) {
        return -1;
    }
    hw->voice_regs = regs;
    channels = (KhdaysSoundChannel *)realloc(hw->voice_channels, (size_t)(hw->voices + 1) * sizeof(*channels));
    if (channels == NULL) {
        return -1;
    }
    hw->voice_channels = channels;
    memset(regs + (size_t)hw->voices * 16, 0, 16);
    memset(&channels[hw->voices], 0, sizeof(*channels));
    return 16 + hw->voices++;
}

/* Whether `size` bytes at `offset` are registers: the DS's, or a voice's. */
static int is_register(const KhdaysSoundHw *hw, uint32_t offset, int size)
{
    return offset + (uint32_t)size <= KHDAYS_SOUND_REG_SIZE + (uint32_t)hw->voices * 16u;
}

uint32_t khdays_sound_hw_read(const KhdaysSoundHw *hw, uint32_t offset, int size)
{
    uint32_t value = 0;
    if (is_register(hw, offset, size)) {
        memcpy(&value, khdays_sound_hw_reg(hw, offset), (size_t)size);
    }
    return value;
}

void khdays_sound_hw_write(KhdaysSoundHw *hw, uint32_t offset, int size, uint32_t value)
{
    if (!is_register(hw, offset, size)) {
        return;
    }
    if (offset < 0x100 || offset >= KHDAYS_SOUND_REG_SIZE) {
        /* an aligned access stays inside one register of one channel */
        const int n = offset < 0x100 ? (int)(offset >> 4) : 16 + (int)((offset - KHDAYS_SOUND_REG_SIZE) >> 4);
        const uint32_t reg = offset & 15;
        KhdaysSoundChannel *c = khdays_sound_hw_channel(hw, n);
        const int was_busy = c->busy;
        memcpy((uint8_t *)khdays_sound_hw_reg(hw, offset), &value, (size_t)size);
        if (reg < 4) {
            const int start_bit = (REG8(BASE(n) + 3) & 0x80) != 0;
            if (start_bit && !was_busy) {
                start(hw, n);
            } else if (!start_bit && was_busy) {
                c->busy = 0;
                silence(hw, n);
            } else if (!c->playing && !(control(hw, n) & 0x8000)) {
                c->sample = 0; /* Hold cleared: the held sample ends */
            }
        } else if (reg != 8 && c->playing) {
            report("SAD, PNT or LEN written while playing (taken at the next start)", n);
        }
        return;
    }
    memcpy(hw->regs + offset, &value, (size_t)size);
    if (offset <= 0x109 && offset + (uint32_t)size > 0x108 && ((REG8(0x108) | REG8(0x109)) & 0x80)) {
        report("a sound capture started (not modelled)", -1);
    }
}

float khdays_sound_hw_between(const KhdaysSoundHw *hw, int n)
{
    const uint32_t reload = REG16(BASE(n) + 8);
    const float t = (float)((int64_t)khdays_sound_hw_channel_of(hw, n)->counter - reload) / (float)(0x10000u - reload);
    return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
}

float khdays_sound_hw_smooth_sample(const KhdaysSoundHw *hw, int n)
{
    const KhdaysSoundChannel *c = khdays_sound_hw_channel_of(hw, n);
    if (!c->playing || ((control(hw, n) >> 29) & 3) == PSG) {
        return (float)c->sample;
    }
    const float t = khdays_sound_hw_between(hw, n);
    const float p0 = (float)c->history[2], p1 = (float)c->history[1], p2 = (float)c->history[0];
    const float p3 = (float)c->sample;
    return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
}

/* --- Time ------------------------------------------------------------------------ */

void khdays_sound_hw_step(KhdaysSoundHw *hw, uint32_t cycles)
{
    const uint32_t counts = (cycles + hw->half) / 2;
    hw->half = (cycles + hw->half) & 1;
    for (int n = 0; n < khdays_sound_hw_count(hw); ++n) {
        KhdaysSoundChannel *c = khdays_sound_hw_channel(hw, n);
        if (c->playing) {
            const uint32_t reload = REG16(BASE(n) + 8);
            c->counter += counts;
            while (c->counter >= 0x10000u && c->playing) {
                c->counter = c->counter - 0x10000u + reload;
                advance(hw, n);
            }
        }
    }
}
