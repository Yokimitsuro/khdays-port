/* The bridge between the game's thread and the sound (sound_bridge.h). One lock guards the driver, its
 * channels and the output; the sound thread moves them every millisecond and the game's thread when it
 * sends a word or reaches an event point, never past the DS clock's time. The driver's replies wait in
 * a queue for the game's thread (the only one touching the IPC FIFO and the interrupt flags), waking
 * it. The driver is the ARM7 binary's sound driver rewritten in C (port/sound/snd7_*.c). */
#include "sound_bridge.h"
#include "../core/window.h"
#include "../core/arm7.h"
#include "../core/clock.h"
#include "../core/events.h"
#include "../core/rom.h"
#include "sound_mods.h"
#include "sound.h"
#include "../../host/app/host.h"
#include "../../sound/archive.h"
#include "../../sound/bus.h"
#include "../core/session_log.h"
#include "../movies/video_mod.h"
#include "../../sound/mix_pc.h"
#include "../../sound/snd7_native.h"
#include "../../sound/stream_reader.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static SRWLOCK lock = SRWLOCK_INIT;
static const KhdaysSnd7Driver *driver;
static KhdaysSoundHw hw; /* the channels the player hears */
static u64 driver_time;  /* how far the driver has run */
static u64 next_sample;  /* the cycle of the next output sample */
static u32 shared_work;  /* where the ARM9 put the shared work (SND_COMMAND_SHARED_WORK) */

static const KhdaysSnd7Driver *forced_driver; /* a test's driver (khdays_sound_bridge_use_driver) */

/* --- Replies ---------------------------------------------------------------------- */

/* The driver's replies to the game, queued in a ring that grows as needed,
 * until the game's thread takes them. */

typedef struct Reply {
    u32 tag, data, err;
} Reply;

static SRWLOCK reply_lock = SRWLOCK_INIT;
static Reply *replies;
static int reply_size, reply_head, reply_count;
static volatile LONG replies_waiting;

/* The driver's reply: queued, and the game's thread woken to take it. */
static void reply(void *context, u32 tag, u32 data, u32 err)
{
    (void)context;
    AcquireSRWLockExclusive(&reply_lock);
    if (reply_count == reply_size) {
        const int size = reply_size ? reply_size * 2 : 64;
        Reply *grown = malloc((size_t)size * sizeof *grown);
        if (grown == NULL) {
            fprintf(stderr, "sound: out of memory keeping %d replies\n", reply_count);
            exit(15);
        }
        for (int i = 0; i < reply_count; ++i) {
            grown[i] = replies[(reply_head + i) % reply_size];
        }
        free(replies);
        replies = grown;
        reply_size = size;
        reply_head = 0;
    }
    replies[(reply_head + reply_count) % reply_size] = (Reply){tag, data, err};
    ++reply_count;
    InterlockedExchange(&replies_waiting, reply_count);
    ReleaseSRWLockExclusive(&reply_lock);
    khdays_clock_wake();
}

/* On the game's thread: puts the waiting replies into the FIFO, in order (64
 * at a time, so the lock is not held while the FIFO takes them). */
static void take_replies(void)
{
    Reply taken[64];
    int count;
    if (InterlockedCompareExchange(&replies_waiting, 0, 0) == 0) { /* written by the driver's thread */
        return;
    }
    do {
        AcquireSRWLockExclusive(&reply_lock);
        count = reply_count < 64 ? reply_count : 64;
        for (int i = 0; i < count; ++i) {
            taken[i] = replies[(reply_head + i) % reply_size];
        }
        reply_head = reply_size ? (reply_head + count) % reply_size : 0;
        reply_count -= count;
        InterlockedExchange(&replies_waiting, reply_count);
        ReleaseSRWLockExclusive(&reply_lock);
        for (int i = 0; i < count; ++i) {
            khdays_arm7_reply(taken[i].tag, taken[i].data, taken[i].err);
        }
    } while (count == 64);
}

int khdays_sound_bridge_replies_waiting(void)
{
    return InterlockedCompareExchange(&replies_waiting, 0, 0) != 0;
}

/* --- Output ------------------------------------------------------------------------ */

/* The mixer: either the DS's (a sample every 1024 cycles: 32,728 Hz, 16 bits)
 * or the PC's (floats at the output device's rate). A change of mixer waits
 * until the buffer being filled is finished. */
static int output_pc; /* the PC's mixer is in use */
static u32 output_rate = KHDAYS_SOUND_RATE;
static u64 output_phase;    /* PC mixer: KHDAYS_CLOCK_HZ x how far into a cycle the next sample is */
static int pending_pc = -1; /* a change asked for (-1: none) */
static KhdaysSoundLimiter limiter = {1.0f, 1.0f}; /* the PC's output's (mix_pc.h), set again with the output */
static u32 pending_rate;

/* How many cycles until the next output sample. */
static u32 cycles_to_next_sample(void)
{
    u32 cycles;
    if (!output_pc) {
        return KHDAYS_SOUND_CYCLES_PER_SAMPLE;
    }
    output_phase += KHDAYS_CLOCK_HZ;
    cycles = (u32)(output_phase / output_rate);
    output_phase -= (u64)cycles * output_rate;
    return cycles;
}

/* KHDAYS_AUDIO_WAV=file: the output also goes to a WAV file, in the format
 * the mixer had when the file was opened (16-bit at 32,728 Hz, or 32-bit
 * float at the device's rate). Its header's sizes are kept up to date as it
 * grows, so a run that is killed still leaves a valid file. */
static FILE *wav;
static u32 wav_bytes;
static int wav_pc; /* the file holds the PC mixer's floats */
static u32 wav_rate;
static s16 frames[2 * 512];
static float pc_frames[2 * 512];
static int frame_count;

static void wav_header(void)
{
    const u32 bytes = wav_pc ? 8 : 4;
    u8 h[44] = {0};
    memcpy(h, "RIFF", 4);
    *(u32 *)(h + 4) = 36 + wav_bytes;
    memcpy(h + 8, "WAVEfmt ", 8);
    *(u32 *)(h + 16) = 16;
    *(u16 *)(h + 20) = wav_pc ? 3 : 1; /* IEEE float, PCM */
    *(u16 *)(h + 22) = 2;
    *(u32 *)(h + 24) = wav_rate;
    *(u32 *)(h + 28) = wav_rate * bytes;
    *(u16 *)(h + 32) = (u16)bytes;
    *(u16 *)(h + 34) = wav_pc ? 32 : 16;
    memcpy(h + 36, "data", 4);
    *(u32 *)(h + 40) = wav_bytes;
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), wav);
    fseek(wav, 0, SEEK_END);
}

static void flush_output(void)
{
    if (frame_count != 0) {
        if (output_pc) {
            khdays_host_audio_float(pc_frames, frame_count);
        } else {
            khdays_host_audio(frames, frame_count);
        }
        if (wav != NULL && wav_pc == output_pc && wav_rate == output_rate) {
            fwrite(output_pc ? (const void *)pc_frames : (const void *)frames, wav_pc ? 8 : 4, (size_t)frame_count,
                   wav);
            wav_bytes += (u32)frame_count * (wav_pc ? 8 : 4);
            wav_header();
        }
        frame_count = 0;
    }
    if (pending_pc >= 0) {
        output_pc = pending_pc;
        output_rate = pending_rate;
        output_phase = 0;
        pending_pc = -1;
        khdays_sound_limiter_init(&limiter, output_rate);
    }
}

/* --- What each channel plays, for the volumes (port/sound/bus.h) --------------- */

static KhdaysSoundBus bus = {1.0f, {1.0f, 1.0f, 1.0f}};

/* The sound archive (port/sound/archive.h), read from the ROM at the
 * archive the game opens (khdays_sound_data_path): for the players' and
 * sounds' names. */
#define SOUND_DATA_PATH khdays_sound_data_path()
static KhdaysSoundArchive *archive;
static u32 archive_offset, archive_size;

static u32 read_archive(void *context, u32 offset, void *destination, u32 size)
{
    (void)context;
    if (offset >= archive_size) {
        return 0;
    }
    if (size > archive_size - offset) {
        size = archive_size - offset;
    }
    khdays_rom_read(archive_offset + offset, destination, size);
    return size;
}

/* Called on the game's thread (by the hooks) before taking the lock, or by
 * the dump. */
const KhdaysSoundArchive *khdays_sound_bridge_archive(void)
{
    static int tried;
    if (!tried) {
        tried = 1;
        if (khdays_rom_find(SOUND_DATA_PATH, &archive_offset, &archive_size)) {
            archive = khdays_sound_archive_open(read_archive, NULL);
        } else {
            fprintf(stderr, "sound: no %s in the ROM: every sound counts as an effect\n", SOUND_DATA_PATH);
        }
    }
    return archive;
}

/* What each ARM7 sequence player plays (khdays_sound_player_assigned), what
 * each stream player plays (khdays_sound_stream_prepared), and each channel's
 * last known category (a note whose track let it go keeps its category). */
static int sequence_category[16] = {
    KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS,
    KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS,
    KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS,
    KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS, KHDAYS_SOUND_EFFECTS};
static int stream_category[4] = {KHDAYS_SOUND_MUSIC, KHDAYS_SOUND_MUSIC, KHDAYS_SOUND_MUSIC, KHDAYS_SOUND_MUSIC};
static int channel_category[16];
static int channel_replaced[16]; /* a mod's file plays instead (sound_mods.c): this channel is silenced */
static u32 channel_starts[16];
static float channel_gain[16];
static float player_gain[16], stream_gain[4];

static int delay_on(void);
static double delay_ms(LARGE_INTEGER from, LARGE_INTEGER to);
static void sound_name(int kind, int number, int entry, const char *prefix, char *name, int size);
static int delay_stderr;
static LARGE_INTEGER sequence_requested_at; /* when StartSeq was entered (KHDAYS_TRACE_SOUND_COMMANDS) */
static char sequence_playing[16][96];       /* each ARM7 player's sequence name, for the log's "stopped" lines */
static const char *const category_names[KHDAYS_SOUND_CATEGORIES] = {"music", "effects", "voices", "movies"};
static volatile LONG movie_channels; /* khdays_sound_movie_channels */
/* khdays_sound_bridge_hold_new_music: whether held, and the sequence players and stream players (bits)
 * whose music began meanwhile, not heard */
static volatile LONG music_held, held_players, held_streams;

void khdays_sound_bridge_hold_new_music(int hold)
{
    InterlockedExchange(&music_held, hold != 0);
    if (!hold) {
        InterlockedExchange(&held_players, 0);
        InterlockedExchange(&held_streams, 0);
    }
}

void khdays_sound_sequence_requested(void)
{
    if (delay_on()) {
        QueryPerformanceCounter(&sequence_requested_at);
    }
}

void khdays_sound_sequence_started(int arm7_player, int kind, int number, int entry, u32 command_tag)
{
    const KhdaysSoundArchive *a;
    char identifier[96];
    double loading = -1.0;
    sound_name(kind, number, entry, "seq", identifier, (int)sizeof(identifier));
    if (kind == 0 && delay_on() && sequence_requested_at.QuadPart != 0) {
        /* StartSeq has loaded the sequence's bank, waves and data, then started it */
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        loading = delay_ms(sequence_requested_at, now);
        if (delay_stderr) {
            fprintf(stderr, "sound: sequence %s loaded and started after %.1f ms\n", identifier, loading);
        }
        sequence_requested_at.QuadPart = 0;
    }
    if (khdays_log_on() && arm7_player >= 0 && arm7_player < 16) {
        snprintf(sequence_playing[arm7_player], sizeof(sequence_playing[arm7_player]), "%s", identifier);
        if (loading >= 0.0) {
            khdays_log("sound: %s starts on player %d (%s), loaded and started %.1f ms after it was asked for",
                       identifier, arm7_player, category_names[sequence_category[arm7_player]], loading);
        } else {
            khdays_log("sound: %s starts on player %d (%s)", identifier, arm7_player,
                       category_names[sequence_category[arm7_player]]);
        }
    }
    if (music_held && arm7_player >= 0 && arm7_player < 16 && sequence_category[arm7_player] == KHDAYS_SOUND_MUSIC) {
        InterlockedOr(&held_players, 1 << arm7_player);
        khdays_log("sound: %s not heard: a scene's HD videos come", identifier);
    }
    if (driver == NULL || (!hw.trace && !khdays_sound_mods_present())) {
        return;
    }
    a = khdays_sound_bridge_archive();
    if (hw.trace && a != NULL) {
        fprintf(stderr, "sound: %s starts on sequence player %d\n", identifier, arm7_player);
    }
    if (!khdays_sound_mods_present()) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    khdays_sound_mods_sequence(a, arm7_player, kind, number, entry, command_tag);
    ReleaseSRWLockExclusive(&lock);
}

void khdays_sound_player_assigned(int arm7_player, int archive_player)
{
    const KhdaysSoundArchive *a = khdays_sound_bridge_archive();
    const int category = a != NULL ? khdays_sound_category(khdays_sound_archive_player_name(a, archive_player), 0)
                                   : KHDAYS_SOUND_EFFECTS;
    if (arm7_player < 0 || arm7_player > 15) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    sequence_category[arm7_player] = category;
    if (hw.trace) {
        static const char *const names[KHDAYS_SOUND_CATEGORIES] = {"music", "effects", "voices", "movies"};
        fprintf(stderr, "sound: sequence player %d plays for %s (%s)\n", arm7_player,
                a != NULL ? khdays_sound_archive_player_name(a, archive_player) : "?", names[category]);
    }
    ReleaseSRWLockExclusive(&lock);
}

/* Sets each channel's gain from its category: a stream's stream player's, a sequence note's ARM7
 * player's, an effect's for a channel the game set up itself (the movies), 0 when a mod's file plays
 * in its place. A note whose track let it go keeps what it was. */
static void update_gains(void)
{
    static int told;
    const LONG players_held = held_players, streams_held = held_streams;
    for (int p = 0; p < 16; ++p) {
        player_gain[p] = (players_held >> p & 1) ? 0.0f : bus.category[sequence_category[p]];
    }
    for (int s = 0; s < 4; ++s) {
        stream_gain[s] = (streams_held >> s & 1) ? 0.0f : bus.category[stream_category[s]];
    }
    for (int n = 0; n < 16; ++n) {
        const KhdaysSoundChannel *c = &hw.channels[n];
        int category = channel_category[n];
        int replaced = channel_replaced[n];
        if (c->source >= 0 && !(c->source & KHDAYS_WAVE_HANDLE)) {
            const int s = khdays_streams_handle_player(c->source);
            category = stream_category[s];
            replaced = khdays_sound_mods_replaces_stream(s) || (streams_held >> s & 1);
        } else {
            const int player = driver->channel_player != NULL ? driver->channel_player(n) : -1;
            if (player >= 0) {
                category = sequence_category[player];
                replaced = khdays_sound_mods_replaces_player(player) || (players_held >> player & 1);
            } else if (movie_channels & 1 << n) {
                category = KHDAYS_SOUND_MOVIES;      /* a movie's sound: one of the channels its player locked */
                replaced = khdays_video_mod_sound(); /* a mod's movie plays its own sound instead */
            } else if (c->starts != channel_starts[n]) {
                category = KHDAYS_SOUND_EFFECTS;
                replaced = 0;
                if (!(told & 1 << n)) {
                    told |= 1 << n;
                    fprintf(stderr,
                            "sound: channel %d plays a sound no sequence or stream started (the game set "
                            "it up itself): counted as an effect\n",
                            n);
                }
            }
        }
        channel_starts[n] = c->starts;
        channel_category[n] = category;
        channel_replaced[n] = replaced;
        channel_gain[n] = replaced ? 0.0f : bus.category[category];
    }
}

void khdays_sound_bridge_set_volumes(int volume, int music, int effects, int voices, int movies)
{
    AcquireSRWLockExclusive(&lock);
    khdays_sound_bus_set(&bus, volume, music, effects, voices, movies);
    ReleaseSRWLockExclusive(&lock);
}

void khdays_sound_movie_channels(u32 mask)
{
    InterlockedExchange(&movie_channels, (LONG)mask);
    if (mask != 0) {
        khdays_log("sound: a movie's sound on the channels of mask 0x%x", mask);
    } else {
        khdays_log("sound: the movie's channels released");
    }
}

/* KHDAYS_PROFILE: the time the driver itself takes, not counting the channel
 * runs it asks for. */
static int profiling;
static LONG64 driver_ticks;

static LONG64 ticks_now(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

/* Runs the channels up to `cycle`; the mixer takes an output sample at each
 * of its instants (every 1024 cycles, as the DS's PWM does, or at the PC's
 * rate). */
static void run(void *context, u64 cycle)
{
    const LONG64 start = profiling ? ticks_now() : 0;
    (void)context;
    while (next_sample <= cycle) {
        const u32 cycles = cycles_to_next_sample();
        const int movie = khdays_video_mod_sound(); /* a mod's movie's sound (video_mod.h) */
        const int mods = khdays_sound_mods_present() || movie;
        const int unity = khdays_sound_bus_unity(&bus) && !mods && !(held_players | held_streams);
        float extra[2] = {0.0f, 0.0f};
        hw.smooth = output_pc; /* the PC's output reads the channels between their samples */
        khdays_sound_hw_step(&hw, cycles);
        if (!unity && (frame_count & 63) == 0) {
            update_gains(); /* what a channel plays changes when its notes start: check every 64 samples */
        }
        if (mods && khdays_sound_mods_present()) {
            khdays_sound_mods_mix(&hw, driver, output_rate,
                                  shared_work != 0 ? *(volatile u32 *)KHDAYS_W(shared_work) : 0, player_gain,
                                  stream_gain, channel_gain, extra);
        }
        if (movie) {
            khdays_video_mod_mix(output_rate, bus.category[KHDAYS_SOUND_MOVIES], extra);
        }
        if (output_pc) {
            khdays_sound_mix_pc(&hw, unity ? NULL : channel_gain, bus.volume, mods ? extra : NULL, &limiter,
                                &pc_frames[2 * frame_count]);
        } else {
            int16_t *out = &frames[2 * frame_count];
            khdays_sound_mix_ds(&hw, unity ? NULL : channel_gain, bus.volume, out);
            for (int side = 0; mods && side < 2; ++side) {
                const float v = (float)out[side] + extra[side] * bus.volume * 32768.0f;
                out[side] = (int16_t)(v < -32768.0f ? -32768.0f : v > 32767.0f ? 32767.0f : v);
            }
        }
        next_sample += cycles;
        if (++frame_count == 512) {
            flush_output();
        }
    }
    hw.clock = next_sample;
    if (profiling) {
        driver_ticks -= ticks_now() - start;
    }
}

static const KhdaysSnd7Host host = {NULL, run, reply};

/* --- The music streams (port/sound/stream_reader.h) ---------------------------- */

/* The streams the port reads itself. Made when the first stream is prepared
 * (by then the game's data, the ADPCM tables among it, is in place). */
static KhdaysStreams *streams;
static KhdaysRomReader *stream_rom; /* the sound's own reads of the ROM */

/* Where each stream player's file is: in the ROM (the sound archive's file
 * runs from `top` to `bottom`, and the stream is at `at` in it; FS_ReadFile
 * reads no further than the archive's end), or, if loaded, at `at` in
 * memory. */
static struct {
    int in_memory;
    u32 top, bottom, at;
} stream_file[KHDAYS_STREAM_PLAYERS];

static u32 read_stream(void *context, u32 offset, void *destination, u32 size)
{
    const int player = (int)(size_t)context;
    if (stream_file[player].in_memory) {
        memcpy(destination, KHDAYS_W(stream_file[player].at + offset), size);
        return size;
    } else {
        const u32 from = stream_file[player].top + stream_file[player].at + offset;
        const u32 left = from < stream_file[player].bottom ? stream_file[player].bottom - from : 0;
        if (size > left) {
            size = left;
        }
        khdays_rom_reader_read(stream_rom, from, destination, size);
        return size;
    }
}

/* KHDAYS_STREAM_CHECK=1: each sample a heard channel takes from a stream is compared with the game's
 * ring; every 600 VBlanks, how many differed and what the ring held instead (zero, an earlier
 * quarter, or anything else). */
static int stream_check = -1;
static u64 stream_samples, stream_differing, stream_zeroed, stream_late;
static u32 stream_report_at = 600;

/* KHDAYS_TRACE_SOUND_COMMANDS: for each stream asked for, the milliseconds until it is prepared and
 * until the port first plays it. */
static int delay_trace = -1;
static LARGE_INTEGER *requested_at; /* when each stream number was asked for (grows) */
static int requested_count;
static LARGE_INTEGER delay_request[KHDAYS_STREAM_PLAYERS], delay_prepared[KHDAYS_STREAM_PLAYERS];
static volatile LONG delay_waiting[KHDAYS_STREAM_PLAYERS]; /* stream number + 1 until heard */

static int delay_stderr; /* KHDAYS_TRACE_SOUND_COMMANDS: print the delays on stderr too */

/* Whether the delays are measured (for the trace or the session log). */
static int delay_on(void)
{
    if (delay_trace < 0) {
        delay_stderr = getenv("KHDAYS_TRACE_SOUND_COMMANDS") != NULL;
        delay_trace = delay_stderr || khdays_log_on();
    }
    return delay_trace;
}

/* A sound's name in the archive ("strm/Title_BGM_PCM8"), or its number. */
static void sound_name(int kind, int number, int entry, const char *prefix, char *name, int size)
{
    const KhdaysSoundArchive *a = khdays_sound_bridge_archive();
    if (a == NULL || !khdays_sound_archive_identifier(a, kind, number, entry, name, size)) {
        snprintf(name, (size_t)size, "%s/%d", prefix, number);
    }
}

/* Notes when stream `number` was asked for. */
static void note_request(int number)
{
    if (number < 0) {
        return;
    }
    if (number >= requested_count) {
        int count = requested_count ? requested_count : 64;
        LARGE_INTEGER *grown;
        while (count <= number)
            count *= 2;
        grown = (LARGE_INTEGER *)realloc(requested_at, (size_t)count * sizeof(*grown));
        if (grown == NULL) {
            return; /* the delay is not measured */
        }
        memset(grown + requested_count, 0, (size_t)(count - requested_count) * sizeof(*grown));
        requested_at = grown;
        requested_count = count;
    }
    QueryPerformanceCounter(&requested_at[number]);
}

/* When stream `number` was last asked for (0 if never). */
static LARGE_INTEGER request_time(int number)
{
    LARGE_INTEGER never;
    never.QuadPart = 0;
    return number >= 0 && number < requested_count ? requested_at[number] : never;
}

/* The milliseconds from `from` to `to`. */
static double delay_ms(LARGE_INTEGER from, LARGE_INTEGER to)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (double)(to.QuadPart - from.QuadPart) * 1000.0 / (double)f.QuadPart;
}

void khdays_sound_stream_requested(int stream_number)
{
    if (delay_on()) {
        note_request(stream_number);
    }
    if (khdays_log_on()) {
        char name[96];
        sound_name(KHDAYS_SOUND_STREAM, stream_number, 0, "strm", name, (int)sizeof(name));
        khdays_log("music: stream %s asked for", name);
    }
}

static void stream_heard(int handle)
{
    const int player = khdays_streams_handle_player(handle);
    LONG waiting;
    LARGE_INTEGER now;
    char name[96];
    const KhdaysSoundArchive *a;
    if (player < 0 || player >= KHDAYS_STREAM_PLAYERS ||
        (waiting = InterlockedExchange(&delay_waiting[player], 0)) == 0) {
        return;
    }
    QueryPerformanceCounter(&now);
    a = khdays_sound_bridge_archive();
    if (a == NULL ||
        !khdays_sound_archive_identifier(a, KHDAYS_SOUND_STREAM, (int)waiting - 1, 0, name, sizeof(name))) {
        snprintf(name, sizeof(name), "strm/%ld", waiting - 1);
    }
    if (delay_stderr) {
        fprintf(stderr, "sound: stream %s prepared after %.1f ms, heard after %.1f ms\n", name,
                delay_ms(delay_request[player], delay_prepared[player]), delay_ms(delay_request[player], now));
    }
    khdays_log("music: stream %s prepared after %.1f ms, heard after %.1f ms", name,
               delay_ms(delay_request[player], delay_prepared[player]), delay_ms(delay_request[player], now));
}

static int heard_stream_sample(void *context, int handle, uint64_t index, int32_t *sample)
{
    if (!khdays_streams_sample(context, handle, index, sample)) {
        return 0;
    }
    if (delay_trace > 0 && *sample != 0) {
        stream_heard(handle);
    }
    if (stream_check) {
        int unit = 0;
        const u32 at = khdays_streams_ring_address(streams, handle, index, &unit);
        if (at != 0) {
            const u8 *ring = (const u8 *)KHDAYS_W(at);
            const int32_t held =
                unit == 1 ? (int32_t)(int8_t)ring[0] * 256 : (int32_t)(int16_t)(ring[0] | ring[1] << 8);
            ++stream_samples;
            if (held != *sample) {
                const u32 ring_samples = khdays_streams_ring_samples(streams, handle);
                int32_t before;
                ++stream_differing;
                if (held == 0) {
                    ++stream_zeroed;
                } else {
                    /* look for the same two samples some quarters back,
                     * trying first the lag last found for the channel */
                    static u64 lag[64];
                    const u64 quarter = ring_samples / 4;
                    const u32 next = khdays_streams_ring_address(streams, handle, index + 1, &unit);
                    const int32_t held_next = next == 0   ? 0
                                              : unit == 1 ? (int32_t)(int8_t)((const u8 *)KHDAYS_W(next))[0] * 256
                                                          : (int32_t)(int16_t)(((const u8 *)KHDAYS_W(next))[0] |
                                                                               ((const u8 *)KHDAYS_W(next))[1] << 8);
                    u64 *last = &lag[handle & 63];
                    int32_t after;
                    int found = *last != 0 && *last < index &&
                                khdays_streams_sample(context, handle, index - *last, &before) && before == held &&
                                khdays_streams_sample(context, handle, index - *last + 1, &after) && after == held_next;
                    for (u64 back = quarter; !found && quarter != 0 && back < index && back < (1u << 18) - quarter;
                         back += quarter) {
                        if (khdays_streams_sample(context, handle, index - back, &before) && before == held &&
                            khdays_streams_sample(context, handle, index - back + 1, &after) && after == held_next) {
                            *last = back;
                            found = 1;
                        }
                    }
                    stream_late += found;
                    if (found && khdays_events_vblank_count() >= stream_report_at) {
                        fprintf(stderr, "stream check: stream player %d's channel %d: its ring %llu quarters behind\n",
                                handle & 3, (handle >> 2) & 15, (unsigned long long)(*last / quarter));
                    }
                }
            }
        }
        if (khdays_events_vblank_count() >= stream_report_at) {
            stream_report_at = khdays_events_vblank_count() + 600;
            fprintf(stderr,
                    "stream check: %llu samples of %llu differed from the ring (%llu zero, %llu from quarters "
                    "before, %llu other)\n",
                    (unsigned long long)stream_differing, (unsigned long long)stream_samples,
                    (unsigned long long)stream_zeroed, (unsigned long long)stream_late,
                    (unsigned long long)(stream_differing - stream_zeroed - stream_late));
            stream_samples = stream_differing = stream_zeroed = stream_late = 0;
        }
    }
    return 1;
}

/* Where the heard channels' samples come from: the streams (checked, with
 * KHDAYS_STREAM_CHECK), then the waves that mods replace. */
static int heard_bind(void *context, int channel, uint32_t sad, uint32_t len, int format)
{
    const int handle = context != NULL ? khdays_streams_bind(context, channel, sad, len, format) : -1;
    return handle >= 0 || !khdays_sound_mods_present() ? handle
                                                       : khdays_sound_mods_bind_wave(channel, sad, len, format);
}

static int heard_sample(void *context, int handle, uint64_t index, int32_t *sample)
{
    return handle & KHDAYS_WAVE_HANDLE ? khdays_sound_mods_wave_sample(handle, index, sample)
                                       : heard_stream_sample(context, handle, index, sample);
}

static KhdaysSoundSource heard_source = {NULL, heard_bind, heard_sample};

void khdays_sound_wave_archive_loaded(int wave_archive, const void *address)
{
    const KhdaysSoundArchive *a;
    if (driver == NULL || address == NULL || !khdays_sound_mods_present()) {
        return;
    }
    a = khdays_sound_bridge_archive();
    AcquireSRWLockExclusive(&lock);
    if (hw.source == NULL) {
        hw.source = &heard_source; /* before any stream is prepared: only the replaced waves */
    }
    khdays_sound_mods_wave_archive(a, wave_archive,
                                   (u32)((const unsigned char *)address - khdays_window)); /* its DS address */
    ReleaseSRWLockExclusive(&lock);
}

void khdays_sound_stream_prepared(int player, int stream_number, int channels, const u8 *channel, u32 ring,
                                  u32 ring_bytes, const void *header, u32 file_at, int in_memory, u32 top, u32 bottom,
                                  u32 start_sample, int mono)
{
    KhdaysStreamPrepared prepared;
    if (driver == NULL || player < 0 || player >= KHDAYS_STREAM_PLAYERS || channels < 1 ||
        channels > KHDAYS_STREAM_CHANNELS) {
        return;
    }
    memset(&prepared, 0, sizeof(prepared));
    prepared.player = player;
    prepared.channels = channels;
    memcpy(prepared.channel, channel, (size_t)channels);
    prepared.ring = ring;
    prepared.ring_bytes = ring_bytes;
    memcpy(prepared.header, header, sizeof(prepared.header));
    prepared.start_sample = start_sample;
    prepared.mono = mono;
    {
        const KhdaysSoundArchive *a = khdays_sound_bridge_archive();
        const int category = a != NULL ? khdays_sound_category(khdays_sound_archive_stream_player_name(a, player), 1)
                                       : KHDAYS_SOUND_MUSIC;
        /* the stream's loop, as the stream player plays it (NNSSndStrmData) */
        const u8 *h = (const u8 *)header;
        const u32 blocks = *(const u32 *)(h + 0x2c), block_samples = *(const u32 *)(h + 0x34);
        const KhdaysOriginalLoop loop = {*(const u16 *)(h + 0x1c), h[0x19], *(const u32 *)(h + 0x20),
                                         blocks != 0 ? (blocks - 1) * block_samples + *(const u32 *)(h + 0x3c) : 0};
        AcquireSRWLockExclusive(&lock);
        stream_category[player] = category;
        if (music_held && category == KHDAYS_SOUND_MUSIC) {
            InterlockedOr(&held_streams, 1 << player);
            khdays_log("sound: stream %d not heard: a scene's HD videos come", stream_number);
        }
        if (khdays_sound_mods_present()) {
            khdays_sound_mods_stream(a, player, stream_number, start_sample, loop.rate, &loop, block_samples);
        }
    }
    if (streams == NULL) {
        streams = khdays_streams_new((const int8_t *)KHDAYS_W(KHDAYS_NNS_ADPCM_INDEX_TABLE),
                                     (const int16_t *)KHDAYS_W(KHDAYS_NNS_ADPCM_STEP_TABLE));
        stream_rom = khdays_rom_reader_new();
        if (stream_check < 0) {
            stream_check = getenv("KHDAYS_STREAM_CHECK") != NULL;
        }
        heard_source.context = streams;
        hw.source = &heard_source;
    }
    if (delay_on()) {
        delay_request[player] = request_time(stream_number);
        QueryPerformanceCounter(&delay_prepared[player]);
        InterlockedExchange(&delay_waiting[player], stream_number + 1);
    }
    stream_file[player].in_memory = in_memory;
    stream_file[player].top = top;
    stream_file[player].bottom = bottom;
    stream_file[player].at = file_at;
    khdays_streams_prepared(streams, &prepared, (KhdaysStreamFile){(void *)(size_t)player, read_stream});
    ReleaseSRWLockExclusive(&lock);
}

/* Loads the ARM7 binary's driver segment from the ROM, for the native
 * driver's tables and initialised data. */
static u8 segment[KHDAYS_SND7_SEGMENT_SIZE];

int khdays_sound_bridge_native_segment(void)
{
    const u8 *header = khdays_rom_header();
    const u32 rom = *(const u32 *)(header + 0x30), ram = *(const u32 *)(header + 0x38);
    const u32 size = *(const u32 *)(header + 0x3c);
    u32 hash = 0x811c9dc5u;
    if (ram != KHDAYS_SND7_ARM7_RAM || size < KHDAYS_SND7_SEGMENT_OFFSET + KHDAYS_SND7_SEGMENT_SIZE) {
        fprintf(stderr, "sound: no native driver: the ARM7 binary is not the one it was read from\n");
        return 0;
    }
    khdays_rom_read(rom + KHDAYS_SND7_SEGMENT_OFFSET, segment, KHDAYS_SND7_SEGMENT_SIZE);
    for (u32 i = 0; i < KHDAYS_SND7_SEGMENT_SIZE; ++i) {
        hash = (hash ^ segment[i]) * 0x01000193u;
    }
    if (hash != KHDAYS_SND7_SEGMENT_FNV1A) {
        fprintf(stderr, "sound: no native driver: the ARM7 binary is not the one it was read from\n");
        return 0;
    }
    khdays_snd7_native_set_segment(segment);
    return 1;
}

/* Notes where the ARM9 put the shared work, from command 0x1d (SHARED_WORK)
 * in a command list (SNDCommand: next at +0, id at +4, arg[0] at +8). */
static void note_shared_work(u32 data)
{
    for (u32 command = data; command >= 0x02000000u && command != 0; command = *(volatile u32 *)KHDAYS_W(command)) {
        if (*(volatile u32 *)KHDAYS_W(command + 4) == 0x1d) {
            shared_work = *(volatile u32 *)KHDAYS_W(command + 8);
        }
    }
}

/* --- Time -------------------------------------------------------------------------- */

/* Runs the driver up to `cycle` (under the lock) and passes on the output. */
static void advance_locked(u64 cycle)
{
    if (cycle > driver_time) {
        const LONG64 start = profiling ? ticks_now() : 0;
        driver->advance(cycle);
        if (profiling) {
            driver_ticks += ticks_now() - start;
        }
        driver_time = cycle;
    }
    flush_output();
}

void khdays_sound_bridge_use_driver(const struct KhdaysSnd7Driver *d)
{
    forced_driver = d;
}

int khdays_sound_bridge_start(void)
{
    const char *setting = getenv("KHDAYS_SOUND");
    const char *path = getenv("KHDAYS_AUDIO_WAV");
    const u64 now = khdays_clock_cycles();
    if (setting != NULL && strcmp(setting, "0") == 0) {
        fprintf(stderr, "sound: off (KHDAYS_SOUND=0)\n");
        return 0;
    }
    if (forced_driver != NULL) {
        driver = forced_driver; /* a test's driver */
    } else if (khdays_sound_bridge_native_segment()) {
        driver = &khdays_snd7_native;
    } else {
        return 0;
    }
    profiling = getenv("KHDAYS_PROFILE") != NULL;
    khdays_sound_hw_reset(&hw, khdays_sound_ds_memory);
    hw.trace = getenv("KHDAYS_TRACE_SOUND") != NULL;
    khdays_sound_mods_trace(hw.trace);
    /* The ARM7 sets the bias only through the SoundBias BIOS call, which
     * moves it to 200h (or to 0 before sleep, GBATEK); the game starts with
     * it at 200h. */
    khdays_sound_hw_write(&hw, 0x104, 2, 0x200);
    next_sample = driver_time = now;
    hw.clock = next_sample;
    flush_output(); /* apply a mixer the host asked for before the start */
    if (path != NULL) {
        wav = fopen(path, "wb");
        if (wav == NULL) {
            fprintf(stderr, "sound: cannot write %s\n", path);
        } else {
            wav_pc = output_pc;
            wav_rate = output_rate;
            wav_header();
        }
    }
    if (!driver->start(&hw, &host, now)) {
        driver = NULL;
        return 0;
    }
    khdays_host_sound_start();
    return 1;
}

/* The game's reset, like the console restarting: the driver stops, every channel goes silent, the
 * replies for the game that ended are dropped; it starts anew once the static module is loaded again
 * (khdays_sound_bridge_restart). The PC's output goes on. */
static const KhdaysSnd7Driver *stopped_driver;

void khdays_sound_bridge_stop(void)
{
    AcquireSRWLockExclusive(&lock);
    stopped_driver = driver;
    driver = NULL;
    {
        const int trace = hw.trace;
        khdays_sound_hw_reset(&hw, khdays_sound_ds_memory);
        hw.trace = trace;
    }
    shared_work = 0;
    InterlockedExchange(&movie_channels, 0);
    ReleaseSRWLockExclusive(&lock);
    AcquireSRWLockExclusive(&reply_lock);
    reply_head = 0;
    reply_count = 0;
    InterlockedExchange(&replies_waiting, 0);
    ReleaseSRWLockExclusive(&reply_lock);
}

void khdays_sound_bridge_restart(void)
{
    const u64 now = khdays_clock_cycles();
    AcquireSRWLockExclusive(&lock);
    if (stopped_driver != NULL) {
        khdays_sound_hw_write(&hw, 0x104, 2, 0x200);
        next_sample = driver_time = now;
        hw.clock = next_sample;
        if (streams != NULL) {
            hw.source = &heard_source;
        }
        driver = stopped_driver->start(&hw, &host, now) ? stopped_driver : NULL;
        stopped_driver = NULL;
    }
    ReleaseSRWLockExclusive(&lock);
}

/* KHDAYS_TRACE_SOUND_COMMANDS: counts the commands the ARM9 sends, by id, and
 * how many lists a VBlank carries, and prints the totals every 600 VBlanks.
 * (A word points to a list of SNDCommand: next at +0, id at +4, arg[4] at
 * +8; a word of 0 asks the driver to take what was sent.) */
static void count_commands(u32 data)
{
    static int trace = -1;
    static u32 per_id[64], lists, empty, frames_with[8], last_vblank, in_vblank, report_at = 600;
    const u32 vblank = khdays_events_vblank_count();
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_SOUND_COMMANDS") != NULL;
    }
    if (!trace) {
        return;
    }
    if (vblank != last_vblank) {
        ++frames_with[in_vblank < 7 ? in_vblank : 7];
        in_vblank = 0;
        last_vblank = vblank;
    }
    if (data == 0) {
        ++empty;
    } else {
        ++lists;
        ++in_vblank;
        for (u32 command = data; command != 0; command = *(volatile u32 *)KHDAYS_W(command)) {
            const u32 id = *(volatile u32 *)KHDAYS_W(command + 4);
            ++per_id[id < 64 ? id : 63];
        }
    }
    if (vblank >= report_at) {
        report_at = vblank + 600;
        fprintf(stderr,
                "sound commands at VBlank %u: %u lists, %u empty words; lists in each VBlank that sent any:", vblank,
                lists, empty);
        for (int n = 0; n < 8; ++n) {
            fprintf(stderr, " %d%s x%u", n, n == 7 ? "+" : "", frames_with[n]);
        }
        fprintf(stderr, "; by id:");
        for (int id = 0; id < 64; ++id) {
            if (per_id[id] != 0) {
                fprintf(stderr, " %d x%u", id, per_id[id]);
            }
        }
        fprintf(stderr, "\n");
    }
}

int khdays_sound_bridge_word(u32 data, u32 err)
{
    if (driver == NULL) {
        return 0;
    }
    count_commands(data);
    if (khdays_log_on() && data != 0) {
        for (u32 command = data; command != 0; command = *(volatile u32 *)KHDAYS_W(command)) {
            if (*(volatile u32 *)KHDAYS_W(command + 4) == 1) {
                const u32 p = *(volatile u32 *)KHDAYS_W(command + 8);
                khdays_log("sound: player %u stopped (%s)", p,
                           p < 16 && sequence_playing[p][0] ? sequence_playing[p] : "?");
            }
        }
    }
    AcquireSRWLockExclusive(&lock);
    advance_locked(khdays_clock_cycles());
    note_shared_work(data);
    {
        const LONG64 start = profiling ? ticks_now() : 0;
        driver->word(data, err);
        if (profiling) {
            driver_ticks += ticks_now() - start;
        }
    }
    flush_output();
    ReleaseSRWLockExclusive(&lock);
    take_replies();
    return 1;
}

void khdays_sound_bridge_advance(u64 cycle)
{
    if (driver == NULL) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    advance_locked(cycle);
    ReleaseSRWLockExclusive(&lock);
}

static LONG64 catchup_ticks;

void khdays_sound_bridge_event(u64 cycle)
{
    if (driver == NULL) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    /* What the game reads (the shared work, the replies) changes only when
     * the driver does something: an alarm, or a word (which is taken at
     * once). So the game's thread catches the driver up only for an alarm
     * due by now that the sound thread has not run yet. */
    if (cycle > driver_time && driver->next_event() <= cycle) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        advance_locked(cycle);
        QueryPerformanceCounter(&t1);
        catchup_ticks += t1.QuadPart - t0.QuadPart;
    }
    ReleaseSRWLockExclusive(&lock);
    take_replies();
}

/* The milliseconds counted in `ticks` since the last call, which resets it. */
static double take_ms(LONG64 *ticks)
{
    LARGE_INTEGER frequency;
    LONG64 taken;
    AcquireSRWLockExclusive(&lock);
    taken = *ticks;
    *ticks = 0;
    ReleaseSRWLockExclusive(&lock);
    QueryPerformanceFrequency(&frequency);
    return taken * 1000.0 / (double)frequency.QuadPart;
}

double khdays_sound_bridge_take_catchup_ms(void)
{
    return take_ms(&catchup_ticks);
}

double khdays_sound_bridge_take_driver_ms(void)
{
    return take_ms(&driver_ticks);
}

void khdays_sound_bridge_set_output(int ds_exact, int rate)
{
    AcquireSRWLockExclusive(&lock);
    pending_pc = !ds_exact && rate > 0;
    pending_rate = pending_pc ? (u32)rate : KHDAYS_SOUND_RATE;
    if (frame_count == 0) {
        flush_output();
    }
    ReleaseSRWLockExclusive(&lock);
}
