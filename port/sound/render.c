/* A sequence played offline (render.h). */
#include "render.h"

#include "mix_pc.h"
#include "snd7_native.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLOCK_HZ 33513982u         /* the ARM7's clock: the driver's cycles */
#define STEP_CYCLES (0xaa8u * 64u) /* the driver's periodic step, 5.21 ms (snd7_driver.c) */
#define PLAYER 0                   /* the ARM7 sequence player it plays on */
#define TAIL_SECONDS 30u           /* the most an ended sequence's notes may ring on */
#define WAIT_SECONDS 30u           /* the most a track may wait for its notes to end (waits_on_notes) */
#define NOT_WAITING UINT64_MAX

/* The commands it sends (the ARM7's numbers, snd7_command.c). */
enum { PREPARE_SEQ = 2, START_PREPARED_SEQ = 3, PLAYER_PARAM = 6, ALLOCATABLE_CHANNEL = 9, SHARED_WORK = 29 };

/* Where they go in the work: two lists, then the shared work. */
#define FIRST_LIST 0x000u
#define SECOND_LIST 0x080u
#define SHARED 0x100u

static KhdaysSoundHw hw;
static uint64_t channels_at; /* the cycle the channels have run to */

static void run(void *context, uint64_t cycle)
{
    (void)context;
    if (cycle > channels_at) {
        khdays_sound_hw_step(&hw, (uint32_t)(cycle - channels_at));
        channels_at = cycle;
    }
}

static void reply(void *context, uint32_t tag, uint32_t data, uint32_t err)
{
    (void)context;
    (void)tag;
    (void)data;
    (void)err; /* the replies are the ARM9's; nothing here waits for them */
}

static const KhdaysSnd7Host host = {NULL, run, reply};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* An SNDCommand (next +0, id +4, arg[4] +8) at `at` of the work, the one
 * before it (at `previous`, or none) pointing at it. Returns where the next
 * one goes. */
static uint32_t command(const KhdaysRenderRequest *q, uint32_t previous, uint32_t at, uint32_t id, uint32_t a0,
                        uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint8_t *c = q->work_bytes + at;
    put32(c, 0);
    put32(c + 4, id);
    put32(c + 8, a0);
    put32(c + 12, a1);
    put32(c + 16, a2);
    put32(c + 20, a3);
    if (previous != ~0u) {
        put32(q->work_bytes + previous, q->work + at);
    }
    return at + 0x18;
}

/* --- The loop ---------------------------------------------------------------------- */

/* The sequencer's state at a tick (the player's and its tracks' records), but for the tempo's counter:
 * with it a loop whose ticks are not a whole number of steps would come back only after several loops
 * (7 at 140 beats a minute); without it the loop is found the second time round, within one step. */
static uint64_t state_hash(void)
{
    const uint8_t *player = khdays_snd7_native_player_record(PLAYER);
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < KHDAYS_SND7_PLAYER_SIZE; ++i) {
        if (i != 0x1c && i != 0x1d) {
            h = (h ^ player[i]) * 0x100000001b3ull;
        }
    }
    for (int t = 0; t < 16; ++t) {
        if (player[8 + t] != 0xff) {
            const uint8_t *track = khdays_snd7_native_track_record(player[8 + t]);
            for (uint32_t i = 0; i < KHDAYS_SND7_TRACK_COMPARED; ++i) {
                h = (h ^ track[i]) * 0x100000001b3ull;
            }
        }
    }
    return h;
}

/* The states seen, each with the first frame at or after it: open
 * addressing, a hash of 0 the empty slot's. */
typedef struct Seen {
    uint64_t hash;
    uint64_t frame;
} Seen;

static Seen *seen;
static uint32_t seen_capacity, seen_count;

static int seen_add(uint64_t hash, uint64_t frame, uint64_t *first)
{
    uint32_t i;
    if (hash == 0) {
        hash = 1;
    }
    if (2 * (seen_count + 1) > seen_capacity) {
        const uint32_t capacity = seen_capacity != 0 ? 2 * seen_capacity : 4096;
        Seen *bigger = calloc(capacity, sizeof(Seen));
        if (bigger == NULL) {
            return -1;
        }
        for (uint32_t j = 0; j < seen_capacity; ++j) {
            if (seen[j].hash != 0) {
                uint32_t k = (uint32_t)seen[j].hash & (capacity - 1);
                while (bigger[k].hash != 0) {
                    k = (k + 1) & (capacity - 1);
                }
                bigger[k] = seen[j];
            }
        }
        free(seen);
        seen = bigger;
        seen_capacity = capacity;
    }
    for (i = (uint32_t)hash & (seen_capacity - 1); seen[i].hash != 0; i = (i + 1) & (seen_capacity - 1)) {
        if (seen[i].hash == hash) {
            *first = seen[i].frame;
            return 1;
        }
    }
    seen[i].hash = hash;
    seen[i].frame = frame;
    ++seen_count;
    return 0;
}

static void seen_clear(void)
{
    free(seen);
    seen = NULL;
    seen_capacity = seen_count = 0;
}

/* Whether a track of the player waits for its notes to end (its flags' 0x10, the driver's
 * TRACK_NOTE_FINISH_WAIT: after a note of length 0, which plays its wave through, as the menus' cues do). Its
 * state stands still from tick to tick then while the note goes on: no loop. */
static int waits_on_notes(void)
{
    const uint8_t *player = khdays_snd7_native_player_record(PLAYER);
    for (int t = 0; t < 16; ++t) {
        if (player[8 + t] != 0xff && (khdays_snd7_native_track_record(player[8 + t])[0] & 0x10u)) {
            return 1;
        }
    }
    return 0;
}

/* --- The render ------------------------------------------------------------------ */

static int16_t to16(float v)
{
    const long s = lrintf(v * 32768.0f);
    return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
}

static int any_playing(void)
{
    for (int n = 0; n < 16; ++n) {
        if (hw.channels[n].playing) {
            return 1;
        }
    }
    return 0;
}

int khdays_render_sequence(const KhdaysRenderRequest *q, KhdaysRendered *out)
{
    const KhdaysSnd7Driver *driver = &khdays_snd7_native;
    const uint64_t limit = (uint64_t)q->limit_seconds * q->rate;
    uint64_t start, stop_at = limit, capacity = 0, k, wait_from = NOT_WAITING;
    uint32_t at, ticks = 0;
    int playing = 1;

    memset(out, 0, sizeof(*out));
    khdays_sound_hw_reset(&hw, q->memory);
    channels_at = 0;
    if (!driver->start(&hw, &host, 0)) {
        return 0;
    }
    /* The driver's first periodic step, then the commands halfway to its
     * next -- in the game they come at any point between two. */
    start = driver->next_event();
    driver->advance(start);
    start += STEP_CYCLES / 2;
    driver->advance(start);

    memset(q->work_bytes, 0, 0x400);
    at = command(q, ~0u, FIRST_LIST, SHARED_WORK, q->work + SHARED, 0, 0, 0);
    at = command(q, FIRST_LIST, at, PREPARE_SEQ, PLAYER, q->sequence, q->offset, q->bank);
    if (q->channels != 0) {
        at = command(q, at - 0x18, at, ALLOCATABLE_CHANNEL, PLAYER, 0xffff, q->channels, 0);
    }
    command(q, at - 0x18, at, PLAYER_PARAM, PLAYER, 4, q->channel_priority, 1); /* the player's priority, +4 */
    command(q, ~0u, SECOND_LIST, START_PREPARED_SEQ, PLAYER, 0, 0, 0);
    driver->word(q->work + FIRST_LIST, 0);
    driver->word(q->work + SECOND_LIST, 0);
    driver->word(0, 0); /* the ARM9's wake-up (RequestCommandProc) */

    for (k = 0; k < stop_at; ++k) {
        const uint64_t now = start + k * (uint64_t)CLOCK_HZ / q->rate;
        float mixed[2];
        uint64_t event;
        while ((event = driver->next_event()) <= now) {
            driver->advance(event);
            if (playing) {
                /* the first frame at or after the step */
                const uint64_t frame = ((event - start) * q->rate + CLOCK_HZ - 1) / CLOCK_HZ;
                const uint8_t *player = khdays_snd7_native_player_record(PLAYER);
                /* the ticks it has played (the shared work's +0x40 + 0x24 * n) */
                const uint8_t *played = q->work_bytes + SHARED + 0x40 + 0x24 * PLAYER;
                const uint32_t now_ticks = (uint32_t)played[0] | (uint32_t)played[1] << 8 | (uint32_t)played[2] << 16 |
                                           (uint32_t)played[3] << 24;
                uint64_t first;
                int found;
                if (!(player[0] & 1)) {
                    playing = 0;
                    out->ended = 1;
                    stop_at = frame + (uint64_t)TAIL_SECONDS * q->rate < limit
                                  ? frame + (uint64_t)TAIL_SECONDS * q->rate
                                  : limit;
                    continue;
                }
                if (now_ticks == ticks) {
                    continue; /* no tick this step: the state as it was */
                }
                ticks = now_ticks;
                if (waits_on_notes()) {
                    /* a note playing its wave through; one that never ends (a looping wave) is cut after
                     * WAIT_SECONDS of it */
                    if (wait_from == NOT_WAITING) {
                        wait_from = frame;
                    }
                    if (frame - wait_from >= (uint64_t)WAIT_SECONDS * q->rate) {
                        playing = 0;
                        stop_at = frame;
                    }
                    continue;
                }
                wait_from = NOT_WAITING;
                found = seen_add(state_hash(), frame, &first);
                if (found < 0) {
                    fprintf(stderr, "sound: out of memory rendering a sequence\n");
                    seen_clear();
                    free(out->samples);
                    memset(out, 0, sizeof(*out));
                    return 0;
                }
                if (found && frame > first) {
                    playing = 0;
                    out->looped = 1;
                    if (frame + (frame - first) <= limit) {
                        out->loop_start = frame; /* the second time round */
                        out->loop_end = frame + (frame - first);
                    } else {
                        out->loop_start = first; /* no room for it: the first */
                        out->loop_end = frame;
                    }
                    stop_at = out->loop_end;
                }
            }
        }
        if (k >= stop_at) {
            break;
        }
        driver->advance(now);
        khdays_sound_mix_pc(&hw, NULL, 1.0f, NULL, NULL, mixed);
        if (2 * k + 2 > capacity) {
            uint64_t bigger_capacity = capacity != 0 ? 2 * capacity : (uint64_t)q->rate * 2 * 8;
            int16_t *bigger;
            while (bigger_capacity < 2 * k + 2) {
                bigger_capacity *= 2;
            }
            bigger = realloc(out->samples, (size_t)bigger_capacity * sizeof(int16_t));
            if (bigger == NULL) {
                fprintf(stderr, "sound: out of memory rendering a sequence\n");
                seen_clear();
                free(out->samples);
                memset(out, 0, sizeof(*out));
                return 0;
            }
            out->samples = bigger;
            capacity = bigger_capacity;
        }
        /* capacity >= 2 * k + 2: grown above whenever it was not */
        out->samples[2 * k] = to16(mixed[0]);
#pragma warning(suppress : 6386)
        out->samples[2 * k + 1] = to16(mixed[1]);
        if (!playing && out->ended && !any_playing()) {
            ++k; /* its last notes have died away */
            break;
        }
    }
    out->frames = k;
    seen_clear();
    return 1;
}
