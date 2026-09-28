/* The ARM7's sound hardware, as GBATEK describes it ("DS Sound Channels
 * 0..15", "Control Registers", "Notes"), fed by the ARM7's own sound driver
 * (snd_driver.c):
 *
 *  - a channel plays PCM8, PCM16 or IMA-ADPCM from memory, or a PSG square
 *    wave (channels 8-13) or noise (14, 15), a sample per overflow of its
 *    timer, which counts at half the clock from SOUNDxTMR;
 *  - a start takes effect after 3 samples (PCM), 11 (ADPCM: 3 and the
 *    header's 8) or 1 (PSG, noise); a one-shot sound clears its busy bit as
 *    its last sample begins and stops when it ends, keeping that sample out
 *    while Hold is set; a looped one plays the part before the loop start
 *    once and the rest forever, ADPCM taking back the state it had there;
 *  - the mixer works at GBATEK's bit widths: volume divider (16.4), volume
 *    factor (16.11), panning (16.18), rounded down to 16.8, summed (20.8),
 *    master volume, the fraction stripped (14.0), the bias added and the
 *    result clipped to 10 bits.
 *
 * The output is that 10-bit value, one per 1024 cycles (the PWM's rate); the
 * mixer runs faster inside the chip (1.04876 MHz), but GBATEK does not say
 * how the PWM takes its value from it, so the channels are read as each
 * output sample is due. The capture units are not modelled: a capture that
 * starts is reported. */
#include "sound.h"
#include "../host/host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REG_BASE 0x04000400u
#define REG_SIZE 0x120u

enum { PCM8, PCM16, ADPCM, PSG };
enum { MANUAL, LOOP, ONE_SHOT };

typedef struct Channel {
    u32 sad, pnt, len;  /* as they were when the channel started */
    int busy;           /* the status bit */
    int playing;        /* still producing samples (the last one outlives busy) */
    s32 position;       /* the sample from SAD (ADPCM: the nibble); < 0 while starting */
    u32 counter;        /* the timer: a sample each time it passes 0xffff */
    s32 sample;         /* what the channel outputs, PCM16 */
    s32 pcm, index;     /* ADPCM */
    s32 loop_pcm, loop_index;
    int loop_saved;
    u32 noise;
} Channel;

static u8 regs[REG_SIZE];
static Channel channels[16];
static u64 next_sample;
static int trace;  /* KHDAYS_TRACE_SOUND: each channel start */

#define REG8(offset)  (*(u8 *)(regs + (offset)))
#define REG16(offset) (*(u16 *)(regs + (offset)))
#define REG32(offset) (*(u32 *)(regs + (offset)))

/* IMA-ADPCM (GBATEK "DS Sound Notes"). */
static const s8 adpcm_index_table[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static const u16 adpcm_table[89] = {
    0x0007, 0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x0010, 0x0011, 0x0013, 0x0015,
    0x0017, 0x0019, 0x001c, 0x001f, 0x0022, 0x0025, 0x0029, 0x002d, 0x0032, 0x0037, 0x003c, 0x0042,
    0x0049, 0x0050, 0x0058, 0x0061, 0x006b, 0x0076, 0x0082, 0x008f, 0x009d, 0x00ad, 0x00be, 0x00d1,
    0x00e6, 0x00fd, 0x0117, 0x0133, 0x0151, 0x0173, 0x0198, 0x01c1, 0x01ee, 0x0220, 0x0256, 0x0292,
    0x02d4, 0x031c, 0x036c, 0x03c3, 0x0424, 0x048e, 0x0502, 0x0583, 0x0610, 0x06ab, 0x0756, 0x0812,
    0x08e0, 0x09c3, 0x0abd, 0x0bd0, 0x0cff, 0x0e4c, 0x0fba, 0x114c, 0x1307, 0x14ee, 0x1706, 0x1954,
    0x1bdc, 0x1ea5, 0x21b6, 0x2515, 0x28ca, 0x2cdf, 0x315b, 0x364b, 0x3bb9, 0x41b2, 0x4844, 0x4f7e,
    0x5771, 0x602f, 0x69ce, 0x7462, 0x7fff,
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

/* Sample memory as the ARM7 sees it: main RAM through its 4 MB mirrors, or
 * the ARM7's WRAM (memory.c). */
static const u8 *memory(u32 address, int channel)
{
    if (address >= 0x02000000u && address < 0x03000000u) {
        return (const u8 *)(size_t)(0x02000000u | (address & 0x3fffffu));
    }
    if (address >= 0x037f8000u && address < 0x03810000u) {
        return (const u8 *)(size_t)address;
    }
    report("sample data outside main RAM and WRAM, played as silence", channel);
    return NULL;
}

static u32 control(int n)
{
    return REG32(n * 16);
}

static void set_busy(int n, int busy)
{
    channels[n].busy = busy;
    if (busy) {
        REG8(n * 16 + 3) |= 0x80;
    } else {
        REG8(n * 16 + 3) &= 0x7f;
    }
}

/* The channel stops: its output falls to zero, or stays at the last sample
 * while Hold is set. */
static void silence(int n)
{
    channels[n].playing = 0;
    if (!(control(n) & 0x8000)) {
        channels[n].sample = 0;
    }
}

static void start(int n)
{
    Channel *c = &channels[n];
    const u32 cnt = control(n);
    const u32 format = (cnt >> 29) & 3;
    c->sad = REG32(n * 16 + 4) & 0x07fffffcu;
    c->pnt = REG16(n * 16 + 0xa);
    c->len = REG32(n * 16 + 0xc) & 0x3fffffu;
    c->counter = REG16(n * 16 + 8);
    c->busy = 1;
    c->playing = 1;
    c->sample = 0;
    c->loop_saved = 0;
    if (trace) {
        fprintf(stderr, "sound: %llu ch%d start cnt %08x sad %08x tmr %04x pnt %04x len %06x\n",
                (unsigned long long)next_sample, n, cnt, c->sad, c->counter, c->pnt, c->len);
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
        const u8 *header = memory(c->sad, n);
        const u32 word = header != NULL ? *(const u32 *)header : 0;
        c->pcm = (s16)word;
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

static s32 adpcm_step(Channel *c, u32 nibble)
{
    const s32 step = adpcm_table[c->index];
    s32 diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) {
        c->pcm -= diff;
        if (c->pcm < -0x7fff) c->pcm = -0x7fff;
    } else {
        c->pcm += diff;
        if (c->pcm > 0x7fff) c->pcm = 0x7fff;
    }
    c->index += adpcm_index_table[nibble & 7];
    if (c->index < 0) c->index = 0;
    if (c->index > 88) c->index = 88;
    return c->pcm;
}

/* The channel's timer overflowed: its next sample. */
static void advance(int n)
{
    Channel *c = &channels[n];
    const u32 cnt = control(n);
    const u32 format = (cnt >> 29) & 3;
    const u32 per_word = format == PCM8 ? 4 : format == PCM16 ? 2 : 8;
    s32 total, loop_start;
    const u8 *data;

    ++c->position;
    if (format == PSG) {
        if (n >= 8 && n <= 13) {
            /* eight steps, starting at the LOW part: HIGH for (duty + 1)/8 */
            const u32 duty = (cnt >> 24) & 7;
            c->sample = duty != 7 && (u32)(c->position & 7) >= 7 - duty ? 0x7fff : -0x7fff;
        } else if (n >= 14) {
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
        return;  /* the start delay; or hung */
    }
    total = (s32)((c->pnt + c->len) * per_word);
    loop_start = (s32)(c->pnt * per_word);
    if (c->position >= total) {
        if (((cnt >> 27) & 3) != LOOP) {
            silence(n);
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
        set_busy(n, 0);  /* the busy bit clears as the last sample begins */
    }
    if (format == ADPCM && c->position < 8) {
        c->sample = 0;  /* the header's time */
        return;
    }
    if (format == ADPCM && c->position == loop_start && !c->loop_saved) {
        c->loop_pcm = c->pcm;
        c->loop_index = c->index;
        c->loop_saved = 1;
    }
    switch (format) {
    case PCM8:
        data = memory(c->sad + (u32)c->position, n);
        c->sample = data != NULL ? (s32)(s8)*data * 256 : 0;
        break;
    case PCM16:
        data = memory(c->sad + (u32)c->position * 2, n);
        c->sample = data != NULL ? (s32)*(const s16 *)data : 0;
        break;
    default:
        data = memory(c->sad + ((u32)c->position >> 1), n);
        c->sample = data != NULL ? adpcm_step(c, (*data >> ((c->position & 1) * 4)) & 0xf) : c->pcm;
        break;
    }
}

/* --- Registers ------------------------------------------------------------------ */

static void unmodelled(u32 address, const char *what)
{
    fprintf(stderr, "khdays-native: the ARM7 %s I/O register 0x%08x, which is not modelled\n", what,
            address);
    fflush(stderr);
    exit(14);
}

u32 khdays_sound_io_read(u32 address, int size)
{
    u32 value = 0;
    if (address < REG_BASE || address - REG_BASE + (u32)size > REG_SIZE) {
        unmodelled(address, "read");
    }
    memcpy(&value, regs + (address - REG_BASE), (size_t)size);
    return value;
}

void khdays_sound_io_write(u32 address, int size, u32 value)
{
    u32 offset;
    if (address < REG_BASE || address - REG_BASE + (u32)size > REG_SIZE) {
        unmodelled(address, "wrote");
    }
    offset = address - REG_BASE;
    if (offset < 0x100) {
        /* an aligned access stays inside one register of one channel */
        const int n = (int)(offset >> 4);
        const u32 reg = offset & 15;
        const int was_busy = channels[n].busy;
        memcpy(regs + offset, &value, (size_t)size);
        if (reg < 4) {
            const int start_bit = (REG8(n * 16 + 3) & 0x80) != 0;
            if (start_bit && !was_busy) {
                start(n);
            } else if (!start_bit && was_busy) {
                channels[n].busy = 0;
                silence(n);
            } else if (!channels[n].playing && !(control(n) & 0x8000)) {
                channels[n].sample = 0;  /* Hold cleared: the held sample ends */
            }
        } else if (reg != 8 && channels[n].playing) {
            report("SAD, PNT or LEN written while playing (taken at the next start)", n);
        }
        return;
    }
    memcpy(regs + offset, &value, (size_t)size);
    if (offset <= 0x109 && offset + (u32)size > 0x108 && ((REG8(0x108) | REG8(0x109)) & 0x80)) {
        report("a sound capture started (not modelled)", -1);
    }
}

/* --- Mixing ----------------------------------------------------------------------- */

/* 7-bit volume and panning: 0..126 are N/128, 127 is 128/128. */
static s32 factor(u32 value)
{
    return value == 127 ? 128 : (s32)value;
}

static void mix(s16 *out)
{
    static const int divider[4] = {0, 1, 2, 4};
    const u32 soundcnt = REG16(0x100);
    const u32 bias = REG16(0x104) & 0x3ff;
    s64 left[16], right[16], mix_left = 0, mix_right = 0;
    s64 master_left, master_right;

    for (int n = 0; n < 16; ++n) {
        Channel *c = &channels[n];
        const u32 cnt = control(n);
        s32 v;
        if (c->playing) {
            const u32 reload = REG16(n * 16 + 8);
            c->counter += KHDAYS_SOUND_CYCLES_PER_SAMPLE / 2;
            while (c->counter >= 0x10000u && c->playing) {
                c->counter = c->counter - 0x10000u + reload;
                advance(n);
            }
        }
        v = (c->sample * 16) >> divider[(cnt >> 8) & 3];         /* 16.4 */
        v *= factor(cnt & 0x7f);                                  /* 16.11 */
        left[n] = ((s64)v * (128 - factor((cnt >> 16) & 0x7f))) >> 10;  /* 16.18 -> 16.8 */
        right[n] = ((s64)v * factor((cnt >> 16) & 0x7f)) >> 10;
        if ((n == 1 && (soundcnt & 0x1000)) || (n == 3 && (soundcnt & 0x2000))) {
            continue;  /* not to the mixer */
        }
        mix_left += left[n];
        mix_right += right[n];
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
    for (int side = 0; side < 2; ++side) {
        s64 v = side == 0 ? master_left : master_right;
        v = (soundcnt & 0x8000) ? (v * factor(soundcnt & 0x7f)) >> 21 : 0;  /* 14.0 */
        v += bias;
        if (v < 0) v = 0;
        if (v > 0x3ff) v = 0x3ff;
        out[side] = (s16)((v - (s64)bias) * 64);
    }
}

/* --- Output ------------------------------------------------------------------------ */

/* KHDAYS_AUDIO_WAV=file: the output also goes to a WAV file (its sizes
 * brought up to date as it grows, so a killed run leaves a valid file). */
static FILE *wav;
static u32 wav_bytes;

static void wav_header(void)
{
    const u32 rate = KHDAYS_SOUND_RATE;
    u8 h[44];
    memcpy(h, "RIFF", 4);
    *(u32 *)(h + 4) = 36 + wav_bytes;
    memcpy(h + 8, "WAVEfmt ", 8);
    *(u32 *)(h + 16) = 16;
    *(u16 *)(h + 20) = 1;
    *(u16 *)(h + 22) = 2;
    *(u32 *)(h + 24) = rate;
    *(u32 *)(h + 28) = rate * 4;
    *(u16 *)(h + 32) = 4;
    *(u16 *)(h + 34) = 16;
    memcpy(h + 36, "data", 4);
    *(u32 *)(h + 40) = wav_bytes;
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), wav);
    fseek(wav, 0, SEEK_END);
}

static void output(const s16 *frames, int count)
{
    khdays_host_audio(frames, count);
    if (wav != NULL) {
        fwrite(frames, 4, (size_t)count, wav);
        wav_bytes += (u32)count * 4;
        wav_header();
    }
}

void khdays_sound_reset(u64 cycle)
{
    const char *path = getenv("KHDAYS_AUDIO_WAV");
    memset(regs, 0, sizeof(regs));
    memset(channels, 0, sizeof(channels));
    trace = getenv("KHDAYS_TRACE_SOUND") != NULL;
    /* The ARM7 sets the bias only through the SoundBias BIOS call, which
     * moves it to 200h or, before sleep, to 0 (GBATEK); the game is started
     * with it at 200h. */
    REG16(0x104) = 0x200;
    next_sample = cycle;
    if (path != NULL && wav == NULL) {
        wav = fopen(path, "wb");
        if (wav == NULL) {
            fprintf(stderr, "sound: cannot write %s\n", path);
        } else {
            wav_header();
        }
    }
}

void khdays_sound_run(u64 cycle)
{
    s16 frames[2 * 512];
    int count = 0;
    while (next_sample <= cycle) {
        mix(&frames[2 * count]);
        next_sample += KHDAYS_SOUND_CYCLES_PER_SAMPLE;
        if (++count == 512) {
            output(frames, count);
            count = 0;
        }
    }
    if (count > 0) {
        output(frames, count);
    }
}
