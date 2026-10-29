/* khdays-sound-test: the port's sound without the game -- the channels and both mixers, the volumes,
 * the sound bridge, the stream reader, the native driver (with the ARM7 binary given), the replacements
 * and the offline render (docs/BUILD.md, "The port's tests"). Prints "all sound checks passed" and
 * exits 0, or names each failure. */
#include "../runtime/mods/list.h"
#include "../sound/channels.h"
#include "../sound/mix_ds.h"
#include "../sound/bus.h"
#include "../sound/mix_pc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> /* the UTF-8 path's check: MultiByteToWideChar, GetFileAttributesW */
#endif

static int failures;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition);                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* --- The channels and the DS's mixer ------------------------------------------------ */

/* The program: sample data at DS 0x02000000 from a fixed generator, and
 * writes to the sound registers at fixed output samples. It covers PCM8,
 * PCM16 and IMA-ADPCM, one-shot and looped sounds, Hold, PSG and noise, the
 * dividers and panning, the master volume, the output selectors with
 * channels 1 and 3 taken off the mixer, and the bias. */
#define GOLDEN_DATA_SIZE 0x4000u
#define GOLDEN_SAMPLES 40000
/* FNV-1a over the 40000 stereo samples, little-endian, as runtime/sound/sound.c
 * made them before the channels and the mixer moved to port/sound
 * (commit b0f2373, the program compiled with that sound.c on its own). */
#define GOLDEN_HASH 0x3bf6f0a3u

static uint8_t golden_data[GOLDEN_DATA_SIZE];
static KhdaysSoundHw golden_hw;

static void golden_fill(uint8_t *data)
{
    uint32_t x = 0x12345678u;
    for (uint32_t i = 0; i < GOLDEN_DATA_SIZE; ++i) {
        x = x * 1664525u + 1013904223u;
        data[i] = (uint8_t)(x >> 24);
    }
    /* the ADPCM sounds' headers: pcm, index */
    data[0x2000] = 0x00;
    data[0x2001] = 0x00;
    data[0x2002] = 0x00;
    data[0x2003] = 0x00;
    data[0x3000] = 0x34;
    data[0x3001] = 0x12;
    data[0x3002] = 0x20;
    data[0x3003] = 0x00;
}

static const uint8_t *golden_memory(uint32_t address, int channel)
{
    (void)channel;
    if (address >= 0x02000000u && address - 0x02000000u < GOLDEN_DATA_SIZE) {
        return golden_data + (address - 0x02000000u);
    }
    return NULL;
}

static void golden_write(uint32_t offset, int size, uint32_t value)
{
    khdays_sound_hw_write(&golden_hw, offset, size, value);
}

/* The channel registers: CNT +0, SAD +4, TMR +8, PNT +0xa, LEN +0xc. */
static void golden_channel(int n, uint32_t cnt, uint32_t sad, uint16_t tmr, uint16_t pnt, uint32_t len)
{
    golden_write((uint32_t)n * 16 + 4, 4, sad);
    golden_write((uint32_t)n * 16 + 8, 2, tmr);
    golden_write((uint32_t)n * 16 + 0xa, 2, pnt);
    golden_write((uint32_t)n * 16 + 0xc, 4, len);
    golden_write((uint32_t)n * 16, 4, cnt);
}

/* CNT: volume 0-6, divider 8-9, hold 15, pan 16-22, duty 24-26, repeat
 * 27-28 (1 loop, 2 one-shot), format 29-30 (PCM8, PCM16, ADPCM, PSG), start 31. */
#define CNT(vol, div, hold, pan, duty, rep, fmt)                                                                       \
    (0x80000000u | (uint32_t)(vol) | (uint32_t)(div) << 8 | (uint32_t)(hold) << 15 | (uint32_t)(pan) << 16 |           \
     (uint32_t)(duty) << 24 | (uint32_t)(rep) << 27 | (uint32_t)(fmt) << 29)

/* The writes due at output sample `s`. */
static void golden_step(int s)
{
    switch (s) {
    case 0:
        golden_write(0x100, 2, 0x807f);                                            /* master on, 127 */
        golden_channel(0, CNT(100, 0, 0, 64, 0, 2, 0), 0x02000000, 0xfe00, 0, 64); /* PCM8 one-shot */
        golden_channel(1, CNT(127, 1, 0, 0, 0, 1, 1), 0x02001000, 0xfd00, 8, 56);  /* PCM16 loop */
        golden_channel(2, CNT(90, 0, 0, 100, 0, 1, 2), 0x02002000, 0xfc80, 4, 60); /* ADPCM loop */
        golden_channel(8, CNT(64, 2, 0, 30, 3, 0, 3), 0, 0xff00, 0, 0);            /* PSG */
        golden_channel(14, CNT(40, 0, 0, 90, 0, 0, 3), 0, 0xfff0, 0, 0);           /* noise */
        break;
    case 3000:
        golden_channel(3, CNT(127, 0, 1, 127, 0, 2, 2), 0x02003000, 0xfb00, 0, 40); /* ADPCM one-shot, hold */
        break;
    case 9000:
        golden_write(0x100, 2, 0x8040); /* master 64 */
        break;
    case 12000:
        golden_write(0x10, 4, CNT(127, 1, 0, 0, 0, 1, 1) & 0x7fffffffu);           /* ch1 stopped */
        golden_channel(0, CNT(127, 3, 0, 0, 0, 1, 0), 0x02000100, 0xf800, 16, 48); /* PCM8 loop again */
        break;
    case 20000:
        golden_write(0x100, 2, 0xb17f); /* ch1 and ch3 off the mixer; L = ch1, R = the mixer */
        golden_channel(1, CNT(127, 0, 0, 20, 0, 1, 1), 0x02001800, 0xfe80, 4, 60);
        break;
    case 26000:
        golden_write(0x100, 2, 0xa77f);    /* L = ch3, R = ch1 + ch3; ch3 off the mixer */
        golden_write(0x30, 4, 0x00000000); /* ch3 stopped, hold cleared */
        golden_channel(3, CNT(127, 0, 0, 64, 0, 2, 0), 0x02000800, 0xfa00, 0, 200);
        break;
    case 32000:
        golden_write(0x104, 2, 0x180); /* bias */
        golden_write(0x80, 4, 0);      /* the PSG stopped */
        break;
    default: break;
    }
}

static uint32_t fnv1a(uint32_t hash, const uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        hash = (hash ^ bytes[i]) * 0x01000193u;
    }
    return hash;
}

/* The PC's mixer against the DS's, measured on the program: at most PC_STEPS
 * of the DS's 10-bit steps (64 in 16 bits) apart where the DS does not clip
 * -- the DS rounds each channel down to 16.8 and the sum down to 14.0.
 * Measured: 1.000 of a step at most, over the 76,171 samples compared. */
#define PC_STEPS 1.0f

static void test_channels_golden(void)
{
    static int16_t out[GOLDEN_SAMPLES][2];
    uint32_t hash;
    float worst = 0.0f;
    int compared = 0;
    golden_fill(golden_data);
    khdays_sound_hw_reset(&golden_hw, golden_memory);
    golden_write(0x104, 2, 0x200); /* the bias the SoundBias call leaves */
    for (int s = 0; s < GOLDEN_SAMPLES; ++s) {
        const int32_t bias = (int32_t)(khdays_sound_hw_read(&golden_hw, 0x104, 2) & 0x3ff);
        float pc[2];
        golden_step(s);
        khdays_sound_hw_step(&golden_hw, KHDAYS_SOUND_CYCLES_PER_SAMPLE);
        khdays_sound_mix_ds(&golden_hw, NULL, 1.0f, out[s]);
        khdays_sound_mix_pc(&golden_hw, NULL, 1.0f, NULL, NULL, pc);
        for (int side = 0; side < 2; ++side) {
            const int32_t ds10 = out[s][side] / 64 + bias; /* the DS's 10 bits */
            float steps;
            if (ds10 <= 0 || ds10 >= 0x3ff) {
                continue; /* the DS clipped */
            }
            steps = pc[side] * 32768.0f / 64.0f - (float)(ds10 - bias);
            steps = steps < 0 ? -steps : steps;
            if (steps > worst) {
                worst = steps;
            }
            ++compared;
        }
    }
    if (worst > PC_STEPS) {
        fprintf(stderr, "the PC's mixer: %.3f of the DS's steps from its output\n", (double)worst);
    }
    CHECK(compared > GOLDEN_SAMPLES && worst <= PC_STEPS);
    hash = fnv1a(0x811c9dc5u, (const uint8_t *)out, sizeof(out));
    if (hash != GOLDEN_HASH) {
        fprintf(stderr, "channels: output hash %08x, expected %08x\n", hash, GOLDEN_HASH);
    }
    CHECK(hash == GOLDEN_HASH);
    /* The hash does see one sample changed. */
    out[GOLDEN_SAMPLES / 2][0] ^= 1;
    CHECK(fnv1a(0x811c9dc5u, (const uint8_t *)out, sizeof(out)) != GOLDEN_HASH);
}

/* --- The sound bridge (runtime/sound/sound_bridge.c) --------------------------------------
 * Built here with stand-ins for what it talks to: the DS clock, the IPC
 * FIFO, the host's output, and a driver of the test's own in place of the
 * native one. */
#include "../runtime/sound/sound_bridge.h"
#include "../sound/snd7.h"
#include "../sound/snd7_native.h"
#include "../sound/stream_reader.h"

#include <stdlib.h>

static uint64_t test_now;
static uint64_t driver_reached; /* the furthest cycle the driver was advanced to */
static int driver_went_past;    /* advanced beyond the clock's now */
static const KhdaysSnd7Host *driver_host;
static uint32_t fifo[20000];
static int fifo_count;

uint64_t khdays_clock_cycles(void)
{
    return test_now;
}
unsigned char *khdays_window; /* the game's memory (runtime/core/window.h): its DS addresses, here */
void khdays_clock_wake(void) {}
uint32_t khdays_events_vblank_count(void)
{
    return 0;
}
const uint8_t *khdays_rom_header(void)
{
    static uint8_t header[0x200];
    return header;
}
void khdays_rom_read(uint32_t offset, uint8_t *destination, uint32_t size)
{
    (void)offset;
    memset(destination, 0, size);
}
const char *khdays_sound_data_path(void)
{
    return "/snd/sound_data.sdat";
}
int khdays_rom_find(const char *path, uint32_t *offset, uint32_t *size)
{
    (void)path;
    (void)offset;
    (void)size;
    return 0;
}
int khdays_mods_find(const char *relative, char *path, int size)
{
    (void)relative;
    (void)path;
    (void)size;
    return 0;
}
int khdays_mods_started_count(void)
{
    return 0;
}
const char *khdays_mods_started_path(int index)
{
    (void)index;
    return NULL;
}
/* no mods' characters */
int khdays_characters_speaker_sound(const char *game, char *path, int size, char *name, int name_size)
{
    (void)game;
    (void)path;
    (void)size;
    (void)name;
    (void)name_size;
    return 0;
}
int khdays_characters_have_sounds(void)
{
    return 0;
}
int khdays_characters_sound_file(int index, char *path, int size, char *name, int name_size)
{
    (void)index;
    (void)path;
    (void)size;
    (void)name;
    (void)name_size;
    return 0;
}
struct KhdaysRomReader {
    int unused;
};
struct KhdaysRomReader *khdays_rom_reader_new(void)
{
    static struct KhdaysRomReader reader;
    return &reader;
}
void khdays_rom_reader_read(struct KhdaysRomReader *reader, uint32_t offset, uint8_t *destination, uint32_t size)
{
    (void)reader;
    (void)offset;
    memset(destination, 0, size);
}
/* No movie a mod replaces (runtime/movies/video_mod.h). */
int khdays_video_mod_sound(void)
{
    return 0;
}
void khdays_video_mod_mix(uint32_t rate, float gain, float out[2])
{
    (void)rate;
    (void)gain;
    (void)out;
}
void khdays_arm7_reply(uint32_t tag, uint32_t data, uint32_t err)
{
    (void)tag;
    (void)err;
    if (fifo_count < 20000) {
        fifo[fifo_count] = data;
    }
    ++fifo_count;
}
void khdays_host_audio(const int16_t *frames, int count)
{
    (void)frames;
    (void)count;
}
void khdays_host_audio_float(const float *frames, int count)
{
    (void)frames;
    (void)count;
}
void khdays_host_sound_start(void) {}
const uint8_t *khdays_sound_ds_memory(uint32_t address, int channel)
{
    (void)address;
    (void)channel;
    return NULL;
}

static int fake_start(KhdaysSoundHw *hw, const KhdaysSnd7Host *host, uint64_t cycle)
{
    (void)hw;
    driver_host = host;
    driver_reached = cycle;
    return 1;
}

/* A word of N makes N replies, numbered. */
static void fake_word(uint32_t data, uint32_t err)
{
    (void)err;
    for (uint32_t i = 0; i < data; ++i) {
        driver_host->reply(driver_host->context, 7, i, 0);
    }
}

static void fake_advance(uint64_t cycle)
{
    if (cycle > test_now) {
        driver_went_past = 1;
    }
    if (cycle > driver_reached) {
        driver_reached = cycle;
    }
    driver_host->run(driver_host->context, cycle);
}

static uint64_t fake_alarm = ~(uint64_t)0; /* the driver's next alarm */
static uint64_t fake_next_event(void)
{
    return fake_alarm;
}

static const KhdaysSnd7Driver fake_driver = {"test", fake_start, fake_word, fake_advance, fake_next_event};

static void test_bridge(void)
{
    int in_order = 1;
    khdays_sound_bridge_use_driver(&fake_driver); /* the fake above stands in for the native one */
    test_now = 1000;
    CHECK(khdays_sound_bridge_start() == 1);
    /* The sound thread runs it to the clock; the game's thread finds it there. */
    test_now = 50000;
    khdays_sound_bridge_advance(test_now);
    CHECK(driver_reached == 50000);
    khdays_sound_bridge_event(test_now);
    CHECK(driver_reached == 50000 && fifo_count == 0);
    /* The game's thread leaves it behind when nothing of the driver's is due... */
    test_now = 90000;
    khdays_sound_bridge_event(test_now);
    CHECK(driver_reached == 50000);
    /* ...and catches it up for an alarm due by now. */
    fake_alarm = 80000;
    khdays_sound_bridge_event(test_now);
    CHECK(driver_reached == 90000);
    fake_alarm = ~(uint64_t)0;
    /* A word is taken at once, its 10,000 replies all reach the FIFO, in order. */
    test_now = 120000;
    CHECK(khdays_sound_bridge_word(10000, 0) == 1);
    CHECK(driver_reached == 120000 && fifo_count == 10000);
    for (int i = 0; i < 10000 && i < fifo_count; ++i) {
        in_order &= fifo[i] == (uint32_t)i;
    }
    CHECK(in_order);
    CHECK(!khdays_sound_bridge_replies_waiting());
    /* Never past the clock's now. */
    CHECK(!driver_went_past);
}

/* --- The native driver, offline ---------------------------------------------------- */

/* The DS's memory it reaches: 64 KB from 0x02000000. */
#define NATIVE_RAM 0x02000000u
static uint8_t native_ram[0x50000]; /* up to past the ARM9's volume table (render) */
static uint8_t native_segment[KHDAYS_SND7_SEGMENT_SIZE];
static KhdaysSoundHw native_hw;
static uint32_t native_replies[16];
static uint64_t native_reply_cycle[16];
static int native_reply_count;
static uint64_t native_channels_at;

static const uint8_t *native_memory(uint32_t address, int channel)
{
    (void)channel;
    return address >= NATIVE_RAM && address < NATIVE_RAM + sizeof(native_ram) ? native_ram + (address - NATIVE_RAM)
                                                                              : NULL;
}

static void native_run(void *context, uint64_t cycle)
{
    (void)context;
    if (cycle > native_channels_at) {
        khdays_sound_hw_step(&native_hw, (uint32_t)(cycle - native_channels_at));
        native_channels_at = cycle;
    }
}

static void native_reply(void *context, uint32_t tag, uint32_t data, uint32_t err)
{
    (void)context;
    (void)err;
    if (tag == 7 && native_reply_count < 16) {
        native_replies[native_reply_count] = data;
        native_reply_cycle[native_reply_count] = native_channels_at;
        ++native_reply_count;
    }
}

static const KhdaysSnd7Host native_host = {NULL, native_run, native_reply};

static uint32_t ram32(uint32_t address)
{
    uint32_t value;
    memcpy(&value, native_ram + (address - NATIVE_RAM), 4);
    return value;
}

static uint32_t reg32(uint32_t offset)
{
    return khdays_sound_hw_read(&native_hw, offset, 4);
}

/* An SNDCommand (next +0, id +4, arg[4] +8) at `address`. */
static void command(uint32_t address, uint32_t next, uint32_t id, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t words[6] = {next, id, a0, a1, a2, a3};
    memcpy(native_ram + (address - NATIVE_RAM), words, sizeof(words));
}

static int load_arm7(const char *path)
{
    FILE *f = path != NULL ? fopen(path, "rb") : NULL;
    uint32_t hash = 0x811c9dc5u;
    size_t got;
    if (f == NULL) {
        return 0;
    }
    fseek(f, KHDAYS_SND7_SEGMENT_OFFSET, SEEK_SET);
    got = fread(native_segment, 1, sizeof(native_segment), f);
    fclose(f);
    for (size_t i = 0; i < got; ++i) {
        hash = (hash ^ native_segment[i]) * 0x01000193u;
    }
    return got == sizeof(native_segment) && hash == KHDAYS_SND7_SEGMENT_FNV1A;
}

enum {
    SND_LOCK_CHANNEL = 26,
    SND_SETUP_CHANNEL_PCM = 14,
    SND_SETUP_ALARM = 18,
    SND_START_TIMER = 12,
    SND_SHARED_WORK = 29
};

static void test_native_driver(const char *arm7)
{
    const uint32_t list = 0x02000100u, work = 0x02001000u, wave = 0x02002000u;
    if (!load_arm7(arm7)) {
        printf("native driver: skipped (no ROM ARM7 binary given, or not this one)\n");
        return;
    }
    khdays_snd7_native_set_segment(native_segment);
    khdays_snd7_native_set_ds_base((uintptr_t)native_ram - NATIVE_RAM);
    khdays_sound_hw_reset(&native_hw, native_memory);
    CHECK(khdays_snd7_native.start(&native_hw, &native_host, 0));
    /* The list: the shared work; channel 4 locked, set up as PCM16, looped,
     * at timer 0x2000, volume 127, centre pan, 0x100 words; alarm 2 once
     * in 0x100 ticks with id 5; channel 4 and alarm 2 started. */
    command(list, list + 0x18, SND_SHARED_WORK, work, 0, 0, 0);
    command(list + 0x18, list + 0x30, SND_LOCK_CHANNEL, 1u << 4, 0, 0, 0);
    command(list + 0x30, list + 0x48, SND_SETUP_CHANNEL_PCM, 4 | 0x2000u << 16, wave, 127u << 24 | 0x100,
            0x40u << 16 | 1u << 24 | 1u << 26);
    command(list + 0x48, list + 0x60, SND_SETUP_ALARM, 2, 0x100, 0, 5);
    command(list + 0x60, 0, SND_START_TIMER, 1u << 4, 0, 1u << 2, 0);
    khdays_snd7_native.word(list, 0);
    khdays_snd7_native.word(0, 0);
    /* SOUNDxCNT: volume, pan 64 (bits 16-22), repeat 1 (27-28), PCM16
     * (29-30), started (31); TMR -timer; SAD, PNT, LEN as given (GBATEK). */
    CHECK(reg32(4 * 16) == (127u | 0x40u << 16 | 1u << 27 | 1u << 29 | 1u << 31));
    CHECK(reg32(4 * 16 + 4) == wave);
    CHECK(khdays_sound_hw_read(&native_hw, 4 * 16 + 8, 2) == 0x10000u - 0x2000u);
    CHECK(khdays_sound_hw_read(&native_hw, 4 * 16 + 0xa, 2) == 0);
    CHECK(reg32(4 * 16 + 0xc) == 0x100);
    /* The shared work: one list done (+0), channel 4 playing (+8). */
    CHECK(ram32(work) == 1);
    CHECK((ram32(work + 8) & 0xffff) == 1u << 4);
    /* The alarm: 0x100 ticks of 64 cycles, then 2 | 5 << 8 on the sound's tag. */
    khdays_snd7_native.advance(0x100 * 64 - 1);
    CHECK(native_reply_count == 0);
    khdays_snd7_native.advance(0x100 * 64);
    CHECK(native_reply_count == 1 && native_replies[0] == (2u | 5u << 8) && native_reply_cycle[0] == 0x100 * 64);
    /* Once: no more by the driver's first periodic alarm (0x10000 ticks). */
    khdays_snd7_native.advance(0x10000ull * 64);
    CHECK(native_reply_count == 1);
}

/* --- The volumes ----------------------------------------------------------------------- */

static int16_t bus_data[256];

static const uint8_t *bus_memory(uint32_t address, int channel)
{
    (void)channel;
    return address >= 0x02000000u && address - 0x02000000u < sizeof(bus_data)
               ? (const uint8_t *)bus_data + (address - 0x02000000u)
               : NULL;
}

static void test_volumes(void)
{
    static KhdaysSoundHw hw;
    KhdaysSoundBus bus;
    float gain[16], unity[16], full[2], half[2], quiet[2], same[2];
    int16_t ds_full[2], ds_half[2], ds_same[2];
    for (int i = 0; i < 256; ++i) {
        bus_data[i] = 0x2000;
    }
    for (int n = 0; n < 16; ++n) {
        gain[n] = unity[n] = 1.0f;
    }
    khdays_sound_hw_reset(&hw, bus_memory);
    khdays_sound_hw_write(&hw, 0x104, 2, 0x200);
    khdays_sound_hw_write(&hw, 0x100, 2, 0x807f);
    /* channel 5: PCM16, looped over the constant, full volume, centred */
    khdays_sound_hw_write(&hw, 5 * 16 + 4, 4, 0x02000000u);
    khdays_sound_hw_write(&hw, 5 * 16 + 8, 2, 0xfe00);
    khdays_sound_hw_write(&hw, 5 * 16 + 0xa, 2, 0);
    khdays_sound_hw_write(&hw, 5 * 16 + 0xc, 4, 128);
    khdays_sound_hw_write(&hw, 5 * 16, 4, 0x80000000u | 127u | 64u << 16 | 1u << 27 | 1u << 29);
    khdays_sound_hw_step(&hw, 4 * KHDAYS_SOUND_CYCLES_PER_SAMPLE);
    khdays_sound_mix_pc(&hw, NULL, 1.0f, NULL, NULL, full);
    CHECK(full[0] > 0.1f && full[0] == full[1]);
    gain[5] = 0.5f;
    khdays_sound_mix_pc(&hw, gain, 1.0f, NULL, NULL, half);
    CHECK(half[0] == full[0] * 0.5f && half[1] == full[1] * 0.5f);
    khdays_sound_mix_pc(&hw, gain, 0.5f, NULL, NULL, quiet);
    CHECK(quiet[0] == full[0] * 0.25f);
    khdays_sound_mix_pc(&hw, unity, 1.0f, NULL, NULL, same);
    CHECK(same[0] == full[0] && same[1] == full[1]);
    khdays_sound_mix_ds(&hw, NULL, 1.0f, ds_full);
    khdays_sound_mix_ds(&hw, unity, 1.0f, ds_same);
    CHECK(ds_same[0] == ds_full[0] && ds_same[1] == ds_full[1]);
    khdays_sound_mix_ds(&hw, gain, 1.0f, ds_half);
    CHECK(ds_half[0] >= ds_full[0] / 2 - 64 && ds_half[0] <= ds_full[0] / 2 + 64);
    /* the settings' 0-100 */
    khdays_sound_bus_set(&bus, 100, 100, 100, 100, 100);
    CHECK(khdays_sound_bus_unity(&bus));
    khdays_sound_bus_set(&bus, 80, 0, 55, 100, 30);
    CHECK(!khdays_sound_bus_unity(&bus) && bus.volume == 0.8f && bus.category[KHDAYS_SOUND_MUSIC] == 0.0f &&
          bus.category[KHDAYS_SOUND_EFFECTS] == 0.55f && bus.category[KHDAYS_SOUND_VOICES] == 1.0f &&
          bus.category[KHDAYS_SOUND_MOVIES] == 0.3f);
    /* the categories, by the archive's player names */
    CHECK(khdays_sound_category("PLAYER_BGM", 0) == KHDAYS_SOUND_MUSIC);
    CHECK(khdays_sound_category("PLAYER_BGM_EVENT", 0) == KHDAYS_SOUND_MUSIC);
    CHECK(khdays_sound_category("PLAYER_SE_EVENT", 0) == KHDAYS_SOUND_EFFECTS);
    CHECK(khdays_sound_category("PLAYER_WEAPON", 0) == KHDAYS_SOUND_EFFECTS);
    CHECK(khdays_sound_category("PLAYER_VO3", 0) == KHDAYS_SOUND_VOICES);
    CHECK(khdays_sound_category("PLAYER_STRM_BGM", 1) == KHDAYS_SOUND_MUSIC);
    CHECK(khdays_sound_category("PLAYER_STRM", 1) == KHDAYS_SOUND_MUSIC);
    CHECK(khdays_sound_category("PLAYER_STRM_V01", 1) == KHDAYS_SOUND_VOICES);
}

/* --- The stream reader --------------------------------------------------------------- */

/* A stream's file: the 0x40-byte header (NNSSndStrmData), its data at
 * 0x40, each block holding each channel's part in turn. */
typedef struct TestStream {
    uint8_t bytes[0x20000];
    uint32_t size;
    int format, loop, channels;
    uint32_t loop_start, blocks, block_size, block_samples, last_block_size, last_block_samples, total;
} TestStream;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t bytes_for(const TestStream *t, uint32_t samples)
{
    return t->format == 0 ? samples : t->format == 1 ? samples * 2 : 4 + (samples + 1) / 2;
}

/* The header, and the data from `fill` (a byte of channel c's part of block b). */
static void test_stream_build(TestStream *t, int format, int loop, int channels, uint32_t block_samples,
                              uint32_t blocks, uint32_t last_block_samples, uint32_t loop_start,
                              uint8_t (*fill)(int c, uint32_t b, uint32_t i))
{
    uint32_t at = 0x40;
    memset(t, 0, sizeof(*t));
    t->format = format;
    t->loop = loop;
    t->channels = channels;
    t->block_samples = block_samples;
    t->blocks = blocks;
    t->last_block_samples = last_block_samples;
    t->block_size = bytes_for(t, block_samples);
    t->last_block_size = bytes_for(t, last_block_samples);
    t->loop_start = loop_start;
    t->total = (blocks - 1) * block_samples + last_block_samples;
    t->bytes[0x18] = (uint8_t)format;
    t->bytes[0x19] = (uint8_t)loop;
    t->bytes[0x1a] = (uint8_t)channels;
    put32(t->bytes + 0x20, loop_start);
    put32(t->bytes + 0x24, t->total);
    put32(t->bytes + 0x28, 0x40);
    put32(t->bytes + 0x2c, blocks);
    put32(t->bytes + 0x30, t->block_size);
    put32(t->bytes + 0x34, block_samples);
    put32(t->bytes + 0x38, t->last_block_size);
    put32(t->bytes + 0x3c, last_block_samples);
    for (uint32_t b = 0; b < blocks; ++b) {
        const uint32_t size = b < blocks - 1 ? t->block_size : t->last_block_size;
        for (int c = 0; c < channels; ++c) {
            for (uint32_t i = 0; i < size; ++i) {
                t->bytes[at++] = fill(c, b, i);
            }
        }
    }
    t->size = at;
}

static uint32_t test_stream_read(void *context, uint32_t offset, void *destination, uint32_t size)
{
    const TestStream *t = context;
    if (offset > t->size) {
        return 0;
    }
    if (size > t->size - offset) {
        size = t->size - offset;
    }
    memcpy(destination, t->bytes + offset, size);
    return size;
}

/* A sample of the file, worked out on its own: PCM from its bytes; IMA-ADPCM
 * decoded from its block's start as NitroSystem decodes it. */
static const int8_t test_index_table[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static int16_t test_step_table[89];

static const uint8_t *part_of(const TestStream *t, int c, uint32_t block)
{
    const uint32_t size = block < t->blocks - 1 ? t->block_size : t->last_block_size;
    return t->bytes + 0x40 + block * t->block_size * (uint32_t)t->channels + (uint32_t)c * size;
}

typedef struct {
    int sample, index;
} TestAdpcm;

static int test_adpcm_step(TestAdpcm *s, int code)
{
    const int step = test_step_table[s->index];
    int d = step >> 3;
    if (code & 4)
        d += step;
    if (code & 2)
        d += step >> 1;
    if (code & 1)
        d += step >> 2;
    s->sample += code & 8 ? -d : d;
    if (s->sample < -32768)
        s->sample = -32768;
    if (s->sample > 32767)
        s->sample = 32767;
    s->index += test_index_table[code];
    if (s->index < 0)
        s->index = 0;
    if (s->index > 88)
        s->index = 88;
    return s->sample;
}

/* The sequence a ring filled in time holds for channel c, from `start`:
 * the file's samples in order, a looped file again from its loop start (an
 * IMA-ADPCM loop start inside a block goes on from the state the end left),
 * zeros after the end of one that is not. */
static void test_expected(const TestStream *t, int c, uint32_t start, int32_t *out, uint32_t count)
{
    uint32_t p = start;
    TestAdpcm state = {0, 0};
    int finished = 0;
    if (t->format == 2) {
        const uint32_t block = start / t->block_samples;
        const uint8_t *part = part_of(t, c, block);
        state.sample = (int16_t)(part[0] | part[1] << 8);
        state.index = part[2];
        for (uint32_t i = 0; i < start - block * t->block_samples; ++i) {
            test_adpcm_step(&state, (part[4 + i / 2] >> ((i & 1) * 4)) & 0xf);
        }
    }
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t block = p / t->block_samples, i = p - block * t->block_samples;
        const uint8_t *part = part_of(t, c, block);
        if (finished) {
            out[k] = 0;
            continue;
        }
        if (t->format == 0) {
            out[k] = (int8_t)part[i] * 256;
        } else if (t->format == 1) {
            out[k] = (int16_t)(part[2 * i] | part[2 * i + 1] << 8);
        } else {
            if (i == 0) {
                state.sample = (int16_t)(part[0] | part[1] << 8);
                state.index = part[2];
            }
            out[k] = test_adpcm_step(&state, (part[4 + i / 2] >> ((i & 1) * 4)) & 0xf);
        }
        if (++p == t->total) {
            if (t->loop) {
                p = t->loop_start;
            } else {
                finished = 1;
            }
        }
    }
}

static uint8_t fill_pcm(int c, uint32_t b, uint32_t i)
{
    return (uint8_t)(i * 37u + b * 101u + (uint32_t)c * 53u + (i >> 3));
}

static uint8_t fill_adpcm(int c, uint32_t b, uint32_t i)
{
    if (i == 0) {
        return (uint8_t)(b * 91u + (uint32_t)c * 17u); /* the state's sample, low */
    }
    if (i == 1) {
        return (uint8_t)(b * 13u + 0x10u); /* its high byte */
    }
    if (i == 2) {
        return (uint8_t)((b * 7u + (uint32_t)c) % 89u); /* its index */
    }
    if (i == 3) {
        return 0;
    }
    return (uint8_t)(i * 29u + b * 11u + (uint32_t)c * 5u + (i * i >> 4));
}

static KhdaysStreams *test_streams;
static TestStream test_stream;

/* Two hardware channels, 6 and 7, on rings of 2048 bytes; the file's
 * channels read through the reader against the expected sequences, channel
 * 7 a little behind channel 6 as the sound's steps leave it. */
static void test_stream_case(const char *what, int format, int loop, int file_channels, int mono,
                             uint32_t block_samples, uint32_t blocks, uint32_t last, uint32_t loop_start,
                             uint32_t start, uint8_t (*fill)(int c, uint32_t b, uint32_t i))
{
    enum { COUNT = 6000 };
    static int32_t expected[2][COUNT];
    KhdaysStreamPrepared p;
    int handle[2], wrong = 0;
    test_stream_build(&test_stream, format, loop, file_channels, block_samples, blocks, last, loop_start, fill);
    memset(&p, 0, sizeof(p));
    p.player = 1;
    p.channels = 2;
    p.channel[0] = 6;
    p.channel[1] = 7;
    p.ring = 0x02100000u;
    p.ring_bytes = 2048;
    memcpy(p.header, test_stream.bytes, sizeof(p.header));
    p.start_sample = start;
    p.mono = mono;
    khdays_streams_prepared(test_streams, &p, (KhdaysStreamFile){&test_stream, test_stream_read});
    for (int c = 0; c < 2; ++c) {
        const uint32_t unit = format == 0 ? 1 : 2;
        handle[c] = khdays_streams_bind(test_streams, 6 + c, p.ring + (uint32_t)c * 2048, 512, format == 0 ? 0 : 1);
        CHECK(handle[c] >= 0);
        (void)unit;
        if (c < file_channels) {
            test_expected(&test_stream, c, start, expected[c], COUNT);
        } else if (mono) {
            memset(expected[c], 0, sizeof(expected[c]));
        } else {
            memcpy(expected[c], expected[0], sizeof(expected[c]));
        }
    }
    CHECK(khdays_streams_bind(test_streams, 6, p.ring + 4, 512, format == 0 ? 0 : 1) < 0);
    for (int k = 0; k < COUNT + 300 && handle[0] >= 0 && handle[1] >= 0; ++k) {
        int32_t got;
        if (k < COUNT &&
            (!khdays_streams_sample(test_streams, handle[0], (uint64_t)k, &got) || got != expected[0][k])) {
            if (wrong++ == 0) {
                fprintf(stderr, "stream %s: channel 0 sample %d: %d, expected %d\n", what, k, got, expected[0][k]);
            }
        }
        if (k >= 300 && (!khdays_streams_sample(test_streams, handle[1], (uint64_t)(k - 300), &got) ||
                         got != expected[1][k - 300])) {
            if (wrong++ == 0) {
                fprintf(stderr, "stream %s: channel 1 sample %d: %d, expected %d\n", what, k - 300, got,
                        expected[1][k - 300]);
            }
        }
    }
    CHECK(wrong == 0);
    /* A new stream on the player: a channel bound to the one before is not its. */
    khdays_streams_prepared(test_streams, &p, (KhdaysStreamFile){&test_stream, test_stream_read});
    {
        int32_t got;
        CHECK(!khdays_streams_sample(test_streams, handle[0], 0, &got));
    }
}

static void test_stream_reader(void)
{
    /* the IMA-ADPCM step table: 7 * 1.1^n, as IMA defines it (the test's
     * own; the reader is given the game's) */
    static const int16_t steps[89] = {
        7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
        31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
        130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
        544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
        2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
    memcpy(test_step_table, steps, sizeof(steps));
    test_streams = khdays_streams_new(test_index_table, test_step_table);
    CHECK(test_streams != NULL);
    if (test_streams == NULL) {
        return;
    }
    test_stream_case("PCM16 stereo, looped", 1, 1, 2, 0, 700, 4, 333, 500, 0, fill_pcm);
    test_stream_case("PCM16 stereo, from 1234", 1, 1, 2, 0, 700, 4, 333, 500, 1234, fill_pcm);
    test_stream_case("PCM8 mono, not looped", 0, 0, 1, 1, 900, 3, 77, 0, 10, fill_pcm);
    test_stream_case("PCM8 mono on two, copied", 0, 0, 1, 0, 900, 3, 77, 0, 0, fill_pcm);
    test_stream_case("IMA-ADPCM, from inside a block, loop inside one", 2, 1, 2, 0, 1000, 3, 601, 1333, 777,
                     fill_adpcm);
    test_stream_case("IMA-ADPCM, loop inside the last block", 2, 1, 2, 0, 1000, 3, 601, 2100, 0, fill_adpcm);
    test_stream_case("IMA-ADPCM, odd start", 2, 0, 2, 0, 1000, 3, 601, 0, 1001, fill_adpcm);
}

/* --- The replacements --------------------------------------------------------------- */

#include "../sound/render.h"
#include "../sound/replace.h"

#include <math.h>

static const char *temp_file(const char *name)
{
    static char path[512];
    const char *directory = getenv("TEMP");
    snprintf(path, sizeof(path), "%s/%s", directory != NULL ? directory : ".", name);
    return path;
}

/* FLAC by hand (the format's own framing): STREAMINFO, a VORBIS_COMMENT with
 * the given comments, then frames of 256 mono 16-bit samples, each a
 * VERBATIM subframe, with their CRC-8 and CRC-16. `frames` a multiple of 256. */
static uint8_t flac_crc8(const uint8_t *p, size_t n)
{
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; ++i) {
            c = (uint8_t)(c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
        }
    }
    return c;
}

static uint16_t flac_crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0;
    while (n--) {
        c ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; ++i) {
            c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x8005 : c << 1);
        }
    }
    return c;
}

static int write_flac(const char *path, const int16_t *samples, uint32_t frames, uint32_t rate,
                      const char *const *comments, int count)
{
    FILE *f = fopen(path, "wb");
    uint8_t info[4 + 34] = {0}, frame[6 + 1 + 512 + 2];
    uint64_t packed;
    uint32_t length = 4 + 4 + 4;
    if (f == NULL) {
        return 0;
    }
    fwrite("fLaC", 1, 4, f);
    info[3] = 34;                                                /* STREAMINFO, not the last */
    info[4] = 1, info[5] = 0, info[6] = 1, info[7] = 0;          /* blocks of 256 */
    packed = (uint64_t)rate << 44 | (uint64_t)15 << 36 | frames; /* mono, 16 bits */
    for (int i = 0; i < 8; ++i) {
        info[4 + 10 + i] = (uint8_t)(packed >> (56 - 8 * i));
    }
    fwrite(info, 1, sizeof(info), f);
    for (int i = 0; i < count; ++i) {
        length += 4 + (uint32_t)strlen(comments[i]);
    }
    {
        const uint8_t header[4] = {0x84, (uint8_t)(length >> 16), (uint8_t)(length >> 8), (uint8_t)length};
        const uint8_t vendor[8] = {4, 0, 0, 0, 't', 'e', 's', 't'};
        const uint8_t how_many[4] = {(uint8_t)count, 0, 0, 0};
        fwrite(header, 1, 4, f); /* VORBIS_COMMENT, the last */
        fwrite(vendor, 1, 8, f);
        fwrite(how_many, 1, 4, f);
        for (int i = 0; i < count; ++i) {
            const uint32_t n = (uint32_t)strlen(comments[i]);
            const uint8_t size[4] = {(uint8_t)n, (uint8_t)(n >> 8), 0, 0};
            fwrite(size, 1, 4, f);
            fwrite(comments[i], 1, n, f);
        }
    }
    for (uint32_t b = 0; b < frames / 256; ++b) {
        uint16_t crc;
        frame[0] = 0xff;
        frame[1] = 0xf8;       /* sync, fixed blocks */
        frame[2] = 0x80;       /* 256 samples; the rate STREAMINFO's */
        frame[3] = 0x08;       /* mono, 16 bits */
        frame[4] = (uint8_t)b; /* the frame's number, one byte of UTF-8 below 128 */
        frame[5] = flac_crc8(frame, 5);
        frame[6] = 0x02; /* VERBATIM */
        for (int i = 0; i < 256; ++i) {
            frame[7 + 2 * i] = (uint8_t)((uint16_t)samples[b * 256 + (uint32_t)i] >> 8);
            frame[8 + 2 * i] = (uint8_t)samples[b * 256 + (uint32_t)i];
        }
        crc = flac_crc16(frame, 7 + 512);
        frame[7 + 512] = (uint8_t)(crc >> 8);
        frame[8 + 512] = (uint8_t)crc;
        fwrite(frame, 1, sizeof(frame), f);
    }
    return fclose(f) == 0;
}

static void test_loops(void)
{
    static int16_t samples[1024];
    KhdaysReplacement r;
    char why[160];
    for (int i = 0; i < 1024; ++i) {
        samples[i] = (int16_t)(i * 61 - 30000);
    }
    /* a WAV's smpl */
    CHECK(khdays_replacement_write_wav(temp_file("khdays_loop.wav"), 22050, 1, samples, 1000, 1, 100, 900));
    CHECK(khdays_replacement_load(&r, temp_file("khdays_loop.wav"), NULL, why, sizeof(why)));
    CHECK(r.rate == 22050 && r.channels == 1 && r.frames == 1000);
    CHECK(r.looped && r.loop_start == 100 && r.loop_end == 900);
    CHECK(r.samples != NULL && memcmp(r.samples, samples, 1000 * sizeof(int16_t)) == 0);
    khdays_replacement_free(&r);
    /* a FLAC file's Vorbis comments, the keys in any case */
    {
        static const char *const comments[] = {"LoopStart=100", "LOOPLENGTH=800"};
        CHECK(write_flac(temp_file("khdays_loop.flac"), samples, 1024, 44100, comments, 2));
        CHECK(khdays_replacement_load(&r, temp_file("khdays_loop.flac"), NULL, why, sizeof(why)));
        CHECK(r.rate == 44100 && r.channels == 1 && r.frames == 1024);
        CHECK(r.looped && r.loop_start == 100 && r.loop_end == 900);
        CHECK(r.samples != NULL && memcmp(r.samples, samples, 1024 * sizeof(int16_t)) == 0);
        khdays_replacement_free(&r);
    }
    /* none of its own: the original's, scaled to the file's rate */
    {
        const KhdaysOriginalLoop original = {22050, 1, 100, 400};
        CHECK(khdays_replacement_write_wav(temp_file("khdays_noloop.wav"), 44100, 1, samples, 1000, 0, 0, 0));
        CHECK(khdays_replacement_load(&r, temp_file("khdays_noloop.wav"), &original, why, sizeof(why)));
        CHECK(r.looped && r.loop_start == 200 && r.loop_end == 800);
        khdays_replacement_free(&r);
    }
    /* a broken file: refused, with its reason */
    {
        FILE *f = fopen(temp_file("khdays_broken.wav"), "wb");
        if (f != NULL) {
            fwrite("RIFF\x04\0\0\0WAVEjunk", 1, 16, f);
            fclose(f);
        }
        why[0] = '\0';
        CHECK(!khdays_replacement_load(&r, temp_file("khdays_broken.wav"), NULL, why, sizeof(why)));
        CHECK(why[0] != '\0');
    }
#ifdef _WIN32
    /* a name outside the system's code page (a player's folder in Japanese, or José's): written and read
     * by its UTF-8 path, and on disk by its real name, the wide one Windows shows */
    {
        char path[512];
        wchar_t wide[512];
        snprintf(path, sizeof(path), "%s", temp_file("khdays_\xe3\x81\x82_jos\xc3\xa9.wav")); /* あ, é */
        CHECK(khdays_replacement_write_wav(path, 22050, 1, samples, 1000, 0, 0, 0));
        CHECK(MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 512) > 0 &&
              GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES);
        CHECK(khdays_replacement_load(&r, path, NULL, why, sizeof(why)) && r.frames == 1000);
        khdays_replacement_free(&r);
        DeleteFileW(wide);
    }
#endif
    remove(temp_file("khdays_loop.wav"));
    remove(temp_file("khdays_loop.flac"));
    remove(temp_file("khdays_noloop.wav"));
    remove(temp_file("khdays_broken.wav"));
}

/* The round trip: a wave in memory, written out as the dump writes it
 * (khdays_sound_decode, its loop from PNT), read back, and played on a
 * channel from the file as runtime/sound/sound_mods.c plays a replaced wave at
 * the original's rate -- the index-th sample since the start, folded into
 * the file's loop -- sample for sample the channel playing the original. */
#define TRIP_ADDRESS 0x02100000u
static uint8_t trip_data[64 * 4];
static KhdaysReplacement trip;

static const uint8_t *trip_memory(uint32_t address, int channel)
{
    (void)channel;
    return address >= TRIP_ADDRESS && address < TRIP_ADDRESS + sizeof(trip_data) ? trip_data + (address - TRIP_ADDRESS)
                                                                                 : NULL;
}

static int trip_bind(void *context, int channel, uint32_t sad, uint32_t len, int format)
{
    (void)context;
    (void)len;
    (void)format;
    return channel == 0 && sad == TRIP_ADDRESS ? 0 : -1;
}

static int trip_sample(void *context, int handle, uint64_t index, int32_t *sample)
{
    const long s = lrintf(khdays_replacement_sample(&trip, (double)index, 0) * 32768.0f);
    (void)context;
    (void)handle;
    *sample = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
    return 1;
}

static void test_round_trip(void)
{
    static const KhdaysSoundSource source = {NULL, trip_bind, trip_sample};
    const uint32_t loop_words = 16, words = 64, timer = 0xfe00;
    static KhdaysSoundHw original, replaced;
    for (int format = 0; format < 3; ++format) {
        static int16_t decoded[64 * 8];
        const uint32_t per_word = format == 0 ? 4 : format == 1 ? 2 : 8, header = format == 2 ? 8 : 0;
        uint32_t frames, state = 12345u + (uint32_t)format;
        char why[160];
        int same = 1;
        for (uint32_t i = 0; i < sizeof(trip_data); ++i) {
            state = state * 1103515245u + 12345u;
            trip_data[i] = (uint8_t)(state >> 16);
        }
        if (format == 2) {
            trip_data[0] = 0, trip_data[1] = 0, trip_data[2] = 20, trip_data[3] = 0; /* ADPCM: sample 0, index 20 */
        }
        frames = khdays_sound_decode(trip_data, words, format, decoded);
        CHECK(frames == words * per_word - header);
        CHECK(khdays_replacement_write_wav(temp_file("khdays_trip.wav"), 32728, 1, decoded, frames, 1,
                                           loop_words * per_word - header, frames));
        CHECK(khdays_replacement_load(&trip, temp_file("khdays_trip.wav"), NULL, why, sizeof(why)));
        khdays_sound_hw_reset(&original, trip_memory);
        khdays_sound_hw_reset(&replaced, trip_memory);
        replaced.source = &source;
        for (int h = 0; h < 2; ++h) {
            KhdaysSoundHw *hw = h == 0 ? &original : &replaced;
            khdays_sound_hw_write(hw, 4, 4, TRIP_ADDRESS);
            khdays_sound_hw_write(hw, 8, 2, timer);
            khdays_sound_hw_write(hw, 0xa, 2, loop_words);
            khdays_sound_hw_write(hw, 0xc, 4, words - loop_words);
            khdays_sound_hw_write(hw, 0, 4, 127u | 1u << 27 | (uint32_t)format << 29 | 1u << 31); /* looped */
        }
        for (uint32_t n = 0; n < 3 * frames + 16; ++n) {
            khdays_sound_hw_step(&original, 2 * (0x10000 - timer));
            khdays_sound_hw_step(&replaced, 2 * (0x10000 - timer));
            same = same && original.channels[0].sample == replaced.channels[0].sample;
        }
        CHECK(same);
        CHECK(replaced.channels[0].source == 0);
        khdays_replacement_free(&trip);
        remove(temp_file("khdays_trip.wav"));
    }
}

/* --- The offline render ---------------------------------------------------------------- */

static void put_bytes(uint32_t address, const uint8_t *bytes, size_t count)
{
    memcpy(native_ram + (address - NATIVE_RAM), bytes, count);
}

static void put_word(uint32_t address, uint32_t value)
{
    const uint8_t b[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    put_bytes(address, b, 4);
}

static void test_render(const char *arm7)
{
    const uint32_t work = 0x02010000u, swar = 0x02011000u, bank = 0x02012000u, once = 0x02013000u, looped = 0x02013100u;
    /* a note of 48 ticks (key 60, velocity 127); then the end, or a jump back */
    static const uint8_t note_once[] = {0x3c, 0x7f, 0x30, 0xff};
    static const uint8_t note_looped[] = {0x3c, 0x7f, 0x30, 0x94, 0x00, 0x00, 0x00};
    /* the instrument: the archive's wave 0, key 60 its own, the fastest
     * envelope, full sustain, centred */
    static const uint8_t settings[10] = {0, 0, 0, 0, 60, 127, 127, 127, 127, 64};
    /* the wave: PCM16 (+0), looped (+1), 32728 Hz (+2), timer 512 (+4),
     * the loop from word 0 (+6) for 32 words (+8) */
    static const uint8_t param[12] = {1, 1, 0xd8, 0x7f, 0x00, 0x02, 0, 0, 32, 0, 0, 0};
    KhdaysRenderRequest q;
    KhdaysRendered r;
    int16_t peak = 0;
    if (!load_arm7(arm7)) {
        printf("offline render: skipped (no ROM ARM7 binary given, or not this one)\n");
        return;
    }
    memset(native_ram, 0, sizeof(native_ram));
    khdays_snd7_native_set_segment(native_segment);
    khdays_snd7_native_set_ds_base((uintptr_t)native_ram - NATIVE_RAM);
    /* the ARM9's copy of the BIOS's volume table is not in the ARM7 binary:
     * here every level is the loudest (the checks are of time, not level) */
    memset(native_ram + (KHDAYS_SND7_ARM9_VOLUME_TABLE - NATIVE_RAM), 127, 724);
    put_word(swar + 0x38, 1);
    put_word(swar + 0x3c, 0x40);
    put_bytes(swar + 0x40, param, sizeof(param));
    for (int i = 0; i < 64; ++i) {
        const int16_t v = i < 32 ? 8000 : -8000;
        put_bytes(swar + 0x4c + 2u * (uint32_t)i, (const uint8_t *)&v, 2);
    }
    put_word(bank + 0x18, swar);
    put_word(bank + 0x38, 1);
    put_word(bank + 0x3c, 1u | 0x40u << 8); /* program 0: a wave (type 1), its settings at +0x40 */
    put_bytes(bank + 0x40, settings, sizeof(settings));
    put_bytes(once, note_once, sizeof(note_once));
    put_bytes(looped, note_looped, sizeof(note_looped));

    memset(&q, 0, sizeof(q));
    q.sequence = once;
    q.bank = bank;
    q.channel_priority = 64;
    q.work = work;
    q.work_bytes = native_ram + (work - NATIVE_RAM);
    q.memory = native_memory;
    q.rate = 48000;
    q.limit_seconds = 10;
    CHECK(khdays_render_sequence(&q, &r));
    /* 48 ticks at 120 beats a minute (48 a beat) are half a second */
    CHECK(r.ended && !r.looped);
    CHECK(r.frames > 24000 && r.frames < 48000 * 2);
    for (uint64_t i = 0; i < 2 * r.frames; ++i) {
        peak = (int16_t)(abs(r.samples[i]) > peak ? abs(r.samples[i]) : peak);
    }
    CHECK(peak > 1000);
    free(r.samples);

    q.sequence = looped;
    CHECK(khdays_render_sequence(&q, &r));
    /* the loop: 48 ticks, 96 of the driver's steps of 0xaa8 * 64 cycles --
     * 16,760,832 cycles, 24,005.4 frames at 48,000 Hz -- the second time */
    CHECK(r.looped && !r.ended);
    CHECK(r.loop_end - r.loop_start >= 24005 && r.loop_end - r.loop_start <= 24006);
    CHECK(r.loop_end == r.frames && r.loop_start >= 24005);
    free(r.samples);
}

/* --- The PC's output: between the samples, the limiter, a file brought down ------------- */

static void test_smoothing(void)
{
    static KhdaysSoundHw hw, quiet_hw;
    KhdaysSoundLimiter limiter;
    float held[2], smooth[2], out[2];
    for (int i = 0; i < 128; ++i) {
        bus_data[i] = (int16_t)(i * 0x100); /* a ramp */
    }
    khdays_sound_hw_reset(&hw, bus_memory);
    khdays_sound_hw_write(&hw, 0x100, 2, 0x807f);
    /* channel 5: PCM16, one sample every 0x200 timer counts (0x400 cycles), centred */
    khdays_sound_hw_write(&hw, 5 * 16 + 4, 4, 0x02000000u);
    khdays_sound_hw_write(&hw, 5 * 16 + 8, 2, 0xfe00);
    khdays_sound_hw_write(&hw, 5 * 16 + 0xa, 2, 0);
    khdays_sound_hw_write(&hw, 5 * 16 + 0xc, 4, 64);
    khdays_sound_hw_write(&hw, 5 * 16, 4, 0x80000000u | 127u | 64u << 16 | 2u << 27 | 1u << 29);
    khdays_sound_hw_step(&hw, 20 * 0x400 + 0x200); /* half way to the next sample */
    {
        const KhdaysSoundChannel *c = &hw.channels[5];
        const float between = khdays_sound_hw_smooth_sample(&hw, 5);
        /* on a ramp the cubic is the line: half way between the samples two before */
        CHECK(c->history[0] - c->history[1] == 0x100 && c->history[1] - c->history[2] == 0x100);
        CHECK(fabsf(between - ((float)c->history[1] + 0.5f * (float)(c->history[0] - c->history[1]))) < 0.5f);
        CHECK(between < (float)c->sample);
    }
    hw.smooth = 0;
    khdays_sound_mix_pc(&hw, NULL, 1.0f, NULL, NULL, held);
    hw.smooth = 1;
    khdays_sound_mix_pc(&hw, NULL, 1.0f, NULL, NULL, smooth);
    CHECK(smooth[0] > 0.0f && smooth[0] < held[0]);

    /* a one-shot sound run to its end is marked so (a mod's longer wave plays on); one the driver stops is not */
    khdays_sound_hw_step(&hw, 120 * 0x400);
    CHECK(!hw.channels[5].playing && hw.channels[5].ended);
    khdays_sound_hw_write(&hw, 5 * 16, 4, 0x80000000u | 127u | 64u << 16 | 2u << 27 | 1u << 29);
    CHECK(hw.channels[5].playing && !hw.channels[5].ended);
    khdays_sound_hw_write(&hw, 5 * 16, 4, 127u | 64u << 16 | 2u << 27 | 1u << 29);
    CHECK(!hw.channels[5].playing && !hw.channels[5].ended);

    /* the limiter: too loud turned down to just under full scale, both sides alike, then back up */
    khdays_sound_hw_reset(&quiet_hw, bus_memory);
    khdays_sound_hw_write(&quiet_hw, 0x100, 2, 0x807f);
    khdays_sound_limiter_init(&limiter, 48000);
    {
        const float loud[2] = {2.0f, -1.0f}, soft[2] = {0.5f, 0.5f};
        khdays_sound_mix_pc(&quiet_hw, NULL, 1.0f, loud, &limiter, out);
        CHECK(out[0] > 0.97f && out[0] <= 0.98f && fabsf(out[1] + out[0] / 2.0f) < 1e-6f);
        khdays_sound_mix_pc(&quiet_hw, NULL, 1.0f, soft, &limiter, out);
        CHECK(out[0] < 0.3f);
        for (int i = 0; i < 48000; ++i) {
            khdays_sound_mix_pc(&quiet_hw, NULL, 1.0f, soft, &limiter, out);
        }
        CHECK(fabsf(out[0] - 0.5f) < 0.001f);
        khdays_sound_mix_pc(&quiet_hw, NULL, 1.0f, loud, NULL, out);
        CHECK(out[0] == 1.0f); /* without it, the clip */
    }

    /* a 96 kHz file: brought down to 48 kHz, its 1 kHz tone kept, its 30 kHz one gone */
    {
        enum { FRAMES = 9600 };
        static int16_t samples[FRAMES];
        KhdaysReplacement r;
        char why[160];
        float worst = 0.0f;
        for (int i = 0; i < FRAMES; ++i) {
            samples[i] = (int16_t)lrint(16000.0 * sin(2.0 * 3.14159265358979 * 1000.0 * i / 96000.0) +
                                        8000.0 * sin(2.0 * 3.14159265358979 * 30000.0 * i / 96000.0));
        }
        CHECK(khdays_replacement_write_wav(temp_file("khdays_96k.wav"), 96000, 1, samples, FRAMES, 1, 960, 8640));
        CHECK(khdays_replacement_load(&r, temp_file("khdays_96k.wav"), NULL, why, sizeof(why)));
        CHECK(r.rate == 48000 && r.frames == FRAMES / 2 && r.looped && r.loop_start == 480 && r.loop_end == 4320);
        for (int j = 64; j < FRAMES / 2 - 64; ++j) {
            const float want = (float)(16000.0 * sin(2.0 * 3.14159265358979 * 1000.0 * j / 48000.0));
            worst = fmaxf(worst, fabsf((float)r.samples[j] - want));
        }
        CHECK(worst < 100.0f);
        khdays_replacement_free(&r);
    }
}

int main(int argc, char **argv)
{
    test_channels_golden();
    test_bridge();
    test_volumes();
    test_smoothing();
    test_stream_reader();
    test_native_driver(argc > 1 ? argv[1] : NULL);
    test_loops();
    test_round_trip();
    test_render(argc > 1 ? argv[1] : NULL);
    if (failures != 0) {
        fprintf(stderr, "%d sound checks failed\n", failures);
        return 1;
    }
    printf("all sound checks passed\n");
    return 0;
}
