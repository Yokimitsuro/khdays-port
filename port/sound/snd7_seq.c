/* The native sound driver's sequencer (snd7_internal.h): the 16 sequence players and the 32 tracks
 * they take, playing the sequences' notes with the bank's instruments. Each function is the routine at
 * the ARM7 address it names; the records keep the ARM7's layout, which the ARM9 sets by offset. */
#include "snd7_internal.h"
#include "snd7_native.h"

#include <stddef.h>
#include <string.h>

/* --- The records ------------------------------------------------------------------- */

enum {
    PLAYER_ACTIVE = 0x01,  /* prepared: it has its tracks */
    PLAYER_STARTED = 0x02, /* playing */
    PLAYER_PAUSED = 0x04,
};

/* A sequence player (0x24 bytes each at 0x03808258). */
typedef struct Player {
    uint8_t flags;          /* +0x00: PLAYER_* */
    uint8_t number;         /* +0x01: its own */
    uint8_t unused_02[2];   /* +0x02: the driver's code never touches them */
    uint8_t priority;       /* +0x04: its notes' channel priority, with each track's */
    uint8_t volume;         /* +0x05: 0-127, through the decibel table */
    int16_t volume_offset;  /* +0x06: tenths of a dB, with each track's */
    uint8_t tracks[16];     /* +0x08: the numbers of its tracks, 0xff none */
    uint16_t tempo;         /* +0x18: beats a minute */
    uint16_t tempo_ratio;   /* +0x1a: 256 as written */
    uint16_t tempo_counter; /* +0x1c: a tick for every 240 */
    uint16_t unused_1e;     /* +0x1e */
    uint32_t bank;          /* +0x20: its bank's address */
} Player;

enum {
    TRACK_ACTIVE = 0x01,           /* a player's */
    TRACK_NOTE_WAIT = 0x02,        /* after a note, it waits the note's length */
    TRACK_MUTED = 0x04,            /* it starts no notes */
    TRACK_TIE = 0x08,              /* a note changes the one playing instead of starting */
    TRACK_NOTE_FINISH_WAIT = 0x10, /* it waits until its notes have ended (after a note of length 0) */
    TRACK_PORTAMENTO = 0x20,       /* a note slides from the key before it */
    TRACK_CONDITION = 0x40,        /* the last comparison's result, for the 0xa2 prefix */
    TRACK_OWN_CHANNELS = 0x80,     /* the ARM9 gave it its channels (ALLOCATABLE_CHANNEL): its
                                    * notes may take the weakly locked ones */
};

/* A track (0x40 bytes each at 0x03808498). */
typedef struct Track {
    uint8_t flags;           /* +0x00: TRACK_* */
    uint8_t pan_range;       /* +0x01: 127 the full width */
    uint16_t program;        /* +0x02: its instrument in the player's bank */
    uint8_t volume;          /* +0x04: 0-127, through the decibel table */
    uint8_t expression;      /* +0x05: a second volume, as the first */
    int8_t bend;             /* +0x06: the pitch bend, in 128ths of its range */
    uint8_t bend_range;      /* +0x07: semitones */
    int8_t pan;              /* +0x08: -64..63 */
    int8_t pan_offset;       /* +0x09 */
    int16_t volume_offset;   /* +0x0a: tenths of a dB, with the player's */
    int16_t pitch_offset;    /* +0x0c: 64ths of a semitone */
    uint8_t attack;          /* +0x0e: 0xff the instrument's */
    uint8_t decay;           /* +0x0f: 0xff the instrument's */
    uint8_t sustain;         /* +0x10: 0xff the instrument's */
    uint8_t release;         /* +0x11: 0xff the instrument's */
    uint8_t priority;        /* +0x12: with the player's */
    int8_t transpose;        /* +0x13: semitones */
    uint8_t portamento_key;  /* +0x14: the key a portamento slides from, the last note's */
    uint8_t portamento_time; /* +0x15: 0: the slide lasts the note */
    int16_t sweep_pitch;     /* +0x16: 64ths of a semitone each note starts off by */
    Snd7LfoParam lfo;        /* +0x18: its notes' */
    uint16_t channel_mask;   /* +0x1e: the channels its notes may take */
    int32_t wait;            /* +0x20: ticks before its next command */
    uint32_t base;           /* +0x24: its sequence's data */
    uint32_t position;       /* +0x28: its next command, 0 before it starts */
    uint32_t call_stack[3];  /* +0x2c: where a return or a loop goes back to */
    uint8_t loop_count[3];   /* +0x38: 0 forever */
    uint8_t depth;           /* +0x3b: in the call stack */
    /* +0x3c in the ARM7's: its notes playing, a list through their next */
    Snd7Channel *channels;
} Track;

_Static_assert(sizeof(Player) == KHDAYS_SND7_PLAYER_SIZE, "a player is the ARM7's 0x24 bytes");
_Static_assert(offsetof(Player, bank) == 0x20, "the player's layout is the ARM7's");
_Static_assert(offsetof(Track, lfo) == 0x18, "the track's layout is the ARM7's");
_Static_assert(offsetof(Track, wait) == 0x20, "the track's layout is the ARM7's");
_Static_assert(offsetof(Track, depth) == 0x3b, "the track's layout is the ARM7's");
_Static_assert(offsetof(Track, channels) == KHDAYS_SND7_TRACK_COMPARED ||
                   offsetof(Track, channels) == KHDAYS_SND7_TRACK_SIZE,
               "the track's notes come after its ARM7 fields");

static Player players[16]; /* 0x03808258 */
static Track tracks[32];   /* 0x03808498 */

const uint8_t *khdays_snd7_native_player_record(int n)
{
    return (const uint8_t *)&players[n];
}

const uint8_t *khdays_snd7_native_track_record(int n)
{
    return (const uint8_t *)&tracks[n];
}

static Player *player_at(int n)
{
    if (n < 0 || n > 15) {
        snd7_stop("player %d is not one of the 16", n);
    }
    return &players[n];
}

/* --- Reading a sequence -------------------------------------------------------------- */

/* 0x03807cfc: the 16 bytes of sequence read last (a sequence is read a
 * word at a time into it). */
static struct {
    uint32_t begin, end;
    uint32_t data[4];
} cache;

/* 0x03800784 */
static void cache_load(uint32_t address)
{
    cache.begin = address & ~3u;
    cache.end = cache.begin + 16;
    for (uint32_t i = 0; i < 4; ++i) {
        cache.data[i] = snd7_rd32(cache.begin + 4 * i);
    }
}

/* 0x03800290 */
static uint8_t read_byte(Track *t)
{
    const uint32_t address = t->position;
    if (address < cache.begin || address >= cache.end) {
        cache_load(address);
    }
    ++t->position;
    return ((const uint8_t *)cache.data)[address - cache.begin];
}

static uint16_t read16(Track *t)
{
    const uint16_t low = read_byte(t);
    return (uint16_t)(low | read_byte(t) << 8);
}

/* 0x038007c0 */
static uint32_t read24(Track *t)
{
    const uint32_t low = read16(t);
    return low | (uint32_t)read_byte(t) << 16;
}

/* 0x03801840: where a player's variable `n` is in the shared work: 0-15
 * the player's own, 16 and up the global ones; -1 without the shared work. */
static int32_t variable(const Player *p, int n)
{
    if (snd7_shared_work == 0) {
        return -1;
    }
    if (n >= 16) {
        return (int32_t)(0x260 + 2 * (n - 16));
    }
    return (int32_t)(0x20 + 0x24 * p->number + 2 * n);
}

/* A variable's value and a new one (past the shared work, where the ARM7
 * would reach the ARM9's memory beyond it, the port stops). */
static int16_t variable_value(int32_t at)
{
    if ((uint32_t)at + 2 > SND7_SHARED_WORK_SIZE) {
        snd7_stop("a sequence's variable at +0x%x is past the shared work", (unsigned)at);
    }
    return (int16_t)snd7_shared_read16((uint32_t)at);
}

static void set_variable(int32_t at, int16_t value)
{
    if ((uint32_t)at + 2 > SND7_SHARED_WORK_SIZE) {
        snd7_stop("a sequence's variable at +0x%x is past the shared work", (unsigned)at);
    }
    snd7_shared_write16((uint32_t)at, (uint16_t)value);
}

/* A random number from 0 to `range` - 1, the ARM7's way: the random
 * halfword times the range, the product's upper half. */
static int32_t random_part(int32_t range)
{
    return (int32_t)((uint32_t)snd7_random() * (uint32_t)range) >> 16;
}

enum { ARG_BYTE, ARG_HALFWORD, ARG_VARIABLE_LENGTH, ARG_RANDOM, ARG_VARIABLE };

/* 0x038007f0: a command's argument: a byte, a halfword, a number of 7 bits
 * a byte (the top bit: more follow), a random number between two
 * halfwords, or a variable's value. */
static int32_t read_arg(Track *t, const Player *p, int type)
{
    switch (type) {
    case ARG_BYTE: return read_byte(t);
    case ARG_HALFWORD: return read16(t);
    case ARG_VARIABLE_LENGTH: {
        uint32_t value = 0;
        uint8_t b;
        do {
            b = read_byte(t);
            value = value << 7 | (b & 0x7f);
        } while (b & 0x80);
        return (int32_t)value;
    }
    case ARG_RANDOM: {
        const int16_t low = (int16_t)read16(t);
        const int16_t high = (int16_t)read16(t);
        return random_part(high - low + 1) + low;
    }
    case ARG_VARIABLE: {
        const int32_t at = variable(p, read_byte(t));
        /* Without the shared work the ARM7 returns what its caller held in
         * that register: the argument's type, 4. */
        return at < 0 ? ARG_VARIABLE : variable_value(at);
    }
    default: return 0;
    }
}

/* --- Tracks and their notes ---------------------------------------------------------- */

/* 0x03801884: a free track, now taken; -1 if all 32 are. */
static int alloc_track(void)
{
    for (int i = 0; i < 32; ++i) {
        if (!(tracks[i].flags & TRACK_ACTIVE)) {
            tracks[i].flags |= TRACK_ACTIVE;
            return i;
        }
    }
    return -1;
}

/* 0x038008f0: a track as a sequence starts it. */
static void track_init(Track *t)
{
    t->base = 0;
    t->position = 0;
    t->flags = (uint8_t)((t->flags | TRACK_NOTE_WAIT | TRACK_CONDITION) &
                         ~(TRACK_MUTED | TRACK_TIE | TRACK_NOTE_FINISH_WAIT | TRACK_PORTAMENTO | TRACK_OWN_CHANNELS));
    t->depth = 0;
    t->program = 0;
    t->priority = 64;
    t->volume = 127;
    t->expression = 127;
    t->volume_offset = 0;
    t->pan = 0;
    t->pan_offset = 0;
    t->bend = 0;
    t->pitch_offset = 0;
    t->attack = 0xff;
    t->decay = 0xff;
    t->sustain = 0xff;
    t->release = 0xff;
    t->pan_range = 127;
    t->bend_range = 2;
    t->portamento_key = 60;
    t->portamento_time = 0;
    t->sweep_pitch = 0;
    t->transpose = 0;
    t->channel_mask = 0xffff;
    snd7_lfo_init(&t->lfo);
    t->wait = 0;
    t->channels = NULL;
}

/* 0x03800a7c: a player's track `n`, NULL if it has none. */
static Track *player_track(Player *p, int n)
{
    if (n > 15 || p->tracks[n] == 0xff) {
        return NULL;
    }
    if (p->tracks[n] >= 32) {
        snd7_stop("player %d's track %d is number %d, not one of the 32", p->number, n, p->tracks[n]);
    }
    return &tracks[p->tracks[n]];
}

/* 0x03800b98: a track's settings to its notes -- their volume (the
 * player's, the track's and its expression, through the decibel table),
 * pitch, pan and LFO; a note in its release keeps its own. With `release`
 * a note whose length ran out is released. */
static void update_channels(Track *t, const Player *p, int release)
{
    int32_t volume = snd7_segment_s16(SND7_DECIBEL_TABLE + 2u * p->volume) +
                     snd7_segment_s16(SND7_DECIBEL_TABLE + 2u * t->volume) +
                     snd7_segment_s16(SND7_DECIBEL_TABLE + 2u * t->expression);
    int32_t volume_offset = t->volume_offset + p->volume_offset;
    const int32_t pitch = t->pitch_offset + ((t->bend * (t->bend_range << 6)) >> 7);
    int32_t pan = t->pan;
    if (t->pan_range != 127) {
        pan = (pan * t->pan_range + 0x40) >> 7;
    }
    pan += t->pan_offset;
    if (volume < -0x8000) {
        volume = -0x8000;
    }
    if (volume_offset < -0x8000) {
        volume_offset = -0x8000;
    }
    if (pan < -128) {
        pan = -128;
    } else if (pan > 127) {
        pan = 127;
    }
    for (Snd7Channel *c = t->channels; c != NULL; c = c->next) {
        c->user_decay2 = (int16_t)volume_offset;
        if (c->envelope_status == 3) {
            continue;
        }
        c->user_decay = (int16_t)volume;
        c->user_pitch = (int16_t)pitch;
        c->user_pan = (int8_t)pan;
        c->pan_range = t->pan_range;
        c->lfo.param = t->lfo;
        if (c->length == 0 && release) {
            c->priority = 1;
            snd7_channel_release(c);
        }
    }
}

/* 0x038009e0: a track's notes released, at `release` (-1: at their own),
 * their settings brought up to date first. */
static void release_channels(Track *t, const Player *p, int release)
{
    update_channels(t, p, 0);
    for (Snd7Channel *c = t->channels; c != NULL; c = c->next) {
        if (snd7_channel_is_active(c)) {
            if (release >= 0) {
                snd7_channel_set_release(c, release & 0xff);
            }
            c->priority = 1;
            snd7_channel_release(c);
        }
    }
}

/* 0x03800a48: a track lets go of its notes (they play on, no longer its). */
static void free_channels(Track *t)
{
    for (Snd7Channel *c = t->channels; c != NULL; c = c->next) {
        snd7_channel_forget_callback(c);
    }
    t->channels = NULL;
}

/* 0x03800b38: a note's channel tells its track it ended or was taken: it
 * leaves the track's notes. */
static void channel_event(Snd7Channel *c, int event, void *data)
{
    Track *t = data;
    if (event == SND7_CHANNEL_FINISHED) {
        c->priority = 0;
        snd7_channel_forget_callback(c);
    }
    if (t->channels == c) {
        t->channels = c->next;
        return;
    }
    for (Snd7Channel *before = t->channels; before != NULL && before->next != NULL; before = before->next) {
        if (before->next == c) {
            before->next = c->next;
            return;
        }
    }
}

/* 0x03800aa8: a player's track `n` ends: its notes released and let go,
 * the track free again. */
static void finish_track(Player *p, int n)
{
    Track *t = player_track(p, n);
    if (t == NULL) {
        return;
    }
    release_channels(t, p, -1);
    free_channels(t);
    t->flags &= (uint8_t)~TRACK_ACTIVE;
    p->tracks[n] = 0xff;
}

/* 0x03800b00 */
static void finish_player(Player *p)
{
    for (int n = 0; n < 16; ++n) {
        finish_track(p, n);
    }
    p->flags &= (uint8_t)~PLAYER_ACTIVE;
}

/* 0x038018c8: 0 unmuted; 1 muted; 2 muted, its notes released; 3 muted,
 * its notes released fast and let go. */
static void mute_track(Track *t, const Player *p, int mode)
{
    switch (mode) {
    case 0: t->flags &= (uint8_t)~TRACK_MUTED; break;
    case 1: t->flags |= TRACK_MUTED; break;
    case 2:
        t->flags |= TRACK_MUTED;
        release_channels(t, p, -1);
        break;
    case 3:
        t->flags |= TRACK_MUTED;
        release_channels(t, p, 127);
        free_channels(t);
        break;
    default: break;
    }
}

/* --- The commands of a sequence (0x03800cf0's switch) ------------------------------ */

/* A note, on a channel its instrument can use -- all for a wave, 8-13 for
 * PSG, 14-15 for noise -- among the track's. */
static Snd7Channel *start_note(Track *t, Player *p, int key, int velocity, int32_t length)
{
    Snd7Instrument instrument;
    Snd7Channel *c;
    uint32_t mask;
    if (!snd7_bank_read_instrument(p->bank, t->program, key, &instrument)) {
        return NULL;
    }
    switch (instrument.type) {
    case 1:
    case 4: mask = 0xffff; break;
    case 2: mask = 0x3f00; break;
    case 3: mask = 0xc000; break;
    default: return NULL;
    }
    /* a wave's note may take one of the port's voices past the sixteen (the PC's output); PSG and noise
     * the DS's channels that have them */
    c = (mask == 0xffff ? snd7_channel_alloc_wave : snd7_channel_alloc)(
        mask & t->channel_mask, p->priority + t->priority, (t->flags & TRACK_OWN_CHANNELS) != 0, channel_event, t);
    if (c == NULL) {
        return NULL;
    }
    if (!snd7_bank_note_on(c, key, velocity, (t->flags & TRACK_TIE) || length <= 0 ? -1 : length, p->bank,
                           &instrument)) {
        c->priority = 0;
        snd7_channel_forget_callback(c);
        return NULL;
    }
    c->next = t->channels;
    t->channels = c;
    return c;
}

/* 0x00-0x7f: a note of that key (with the transpose), its velocity and
 * length in ticks. Tied, it changes the note playing. */
static void note(Track *t, Player *p, int play, int key, int execute, int type)
{
    const int velocity = read_byte(t);
    const int32_t length = read_arg(t, p, type);
    key += t->transpose;
    if (!execute) {
        return;
    }
    if (key < 0) {
        key = 0;
    } else if (key > 127) {
        key = 127;
    }
    if (!(t->flags & TRACK_MUTED) && play) {
        Snd7Channel *c = NULL;
        if (t->flags & TRACK_TIE) {
            c = t->channels;
            if (c != NULL) {
                c->key = (uint8_t)key;
                c->velocity = (uint8_t)velocity;
            }
        }
        if (c == NULL) {
            c = start_note(t, p, key, velocity, length);
        }
        if (c != NULL) {
            if (t->attack != 0xff) {
                snd7_channel_set_attack(c, t->attack);
            }
            if (t->decay != 0xff) {
                snd7_channel_set_decay(c, t->decay);
            }
            if (t->sustain != 0xff) {
                snd7_channel_set_sustain(c, t->sustain);
            }
            if (t->release != 0xff) {
                snd7_channel_set_release(c, t->release);
            }
            c->sweep_pitch = t->sweep_pitch;
            if (t->flags & TRACK_PORTAMENTO) {
                c->sweep_pitch = (int16_t)(c->sweep_pitch + (t->portamento_key - key) * 64);
            }
            if (t->portamento_time == 0) {
                c->sweep_length = length > 0 ? length : -1;
                c->auto_sweep = 0;
            } else {
                const int32_t time = t->portamento_time * t->portamento_time;
                const int32_t sweep = c->sweep_pitch < 0 ? -c->sweep_pitch : c->sweep_pitch;
                c->sweep_length = (time * sweep) >> 11;
            }
            c->sweep_counter = 0;
        }
    }
    t->portamento_key = (uint8_t)key;
    if (t->flags & TRACK_NOTE_WAIT) {
        t->wait = length;
        if (length == 0) {
            t->flags |= TRACK_NOTE_FINISH_WAIT;
        }
    }
}

/* The ARM7's shifts by a register: by its low byte, 32 or more shifting
 * everything out. */
static int32_t shift_left(int32_t value, int32_t by)
{
    by &= 0xff;
    return by >= 32 ? 0 : (int32_t)((uint32_t)value << by);
}

static int32_t shift_right(int32_t value, int32_t by)
{
    by &= 0xff;
    return by >= 32 ? (value < 0 ? -1 : 0) : value >> by;
}

/* 0xb0-0xbd: a variable set from the argument, or compared with it (the
 * result for the 0xa2 prefix). */
static void variable_command(Track *t, Player *p, int command, int execute, int type)
{
    const int n = read_byte(t);
    int16_t arg = (int16_t)read_arg(t, p, type);
    const int32_t at = variable(p, n);
    int16_t value;
    int condition;
    if (!execute || at < 0) {
        return;
    }
    value = variable_value(at);
    switch (command) {
    case 0xb0: set_variable(at, arg); return;
    case 0xb1: set_variable(at, (int16_t)(value + arg)); return;
    case 0xb2: set_variable(at, (int16_t)(value - arg)); return;
    case 0xb3: set_variable(at, (int16_t)(value * arg)); return;
    case 0xb4:
        if (arg != 0) {
            set_variable(at, (int16_t)(value / arg));
        }
        return;
    case 0xb5: set_variable(at, (int16_t)(arg >= 0 ? shift_left(value, arg) : shift_right(value, -arg))); return;
    case 0xb6: {
        const int negative = arg < 0;
        int32_t random;
        if (negative) {
            arg = (int16_t)-arg;
        }
        random = random_part(arg + 1);
        set_variable(at, (int16_t)(negative ? -random : random));
        return;
    }
    case 0xb8: condition = value == arg; break;
    case 0xb9: condition = value >= arg; break;
    case 0xba: condition = value > arg; break;
    case 0xbb: condition = value <= arg; break;
    case 0xbc: condition = value < arg; break;
    case 0xbd: condition = value != arg; break;
    default: return;
    }
    t->flags = (uint8_t)((t->flags & ~TRACK_CONDITION) | (condition ? TRACK_CONDITION : 0));
}

static void set_flag(Track *t, uint8_t flag, int on)
{
    t->flags = (uint8_t)((t->flags & ~flag) | (on & 1 ? flag : 0));
}

/* 0xc0-0xd7: a setting of a byte. */
static void setting_command(Track *t, Player *p, int command, int execute, int type)
{
    const uint8_t arg = (uint8_t)read_arg(t, p, type);
    if (!execute) {
        return;
    }
    switch (command) {
    case 0xc0: t->pan = (int8_t)(arg - 0x40); break;
    case 0xc1: t->volume = arg; break;
    case 0xc2: p->volume = arg; break;
    case 0xc3: t->transpose = (int8_t)arg; break;
    case 0xc4: t->bend = (int8_t)arg; break;
    case 0xc5: t->bend_range = arg; break;
    case 0xc6: t->priority = arg; break;
    case 0xc7: set_flag(t, TRACK_NOTE_WAIT, arg); break;
    case 0xc8:
        set_flag(t, TRACK_TIE, arg);
        release_channels(t, p, -1);
        free_channels(t);
        break;
    case 0xc9:
        t->portamento_key = (uint8_t)(arg + t->transpose);
        t->flags |= TRACK_PORTAMENTO;
        break;
    case 0xca: t->lfo.depth = arg; break;
    case 0xcb: t->lfo.speed = arg; break;
    case 0xcc: t->lfo.target = arg; break;
    case 0xcd: t->lfo.range = arg; break;
    case 0xce: set_flag(t, TRACK_PORTAMENTO, arg); break;
    case 0xcf: t->portamento_time = arg; break;
    case 0xd0: t->attack = arg; break;
    case 0xd1: t->decay = arg; break;
    case 0xd2: t->sustain = arg; break;
    case 0xd3: t->release = arg; break;
    case 0xd4: /* a loop starts: back here, `arg` times (0 forever) */
        if (t->depth < 3) {
            t->call_stack[t->depth] = t->position;
            t->loop_count[t->depth] = arg;
            ++t->depth;
        }
        break;
    case 0xd5: t->expression = arg; break;
    case 0xd6: /* a variable printed: nothing -- the ARM7 looks for a flag (0x03807cf8) nothing sets */ break;
    case 0xd7: mute_track(t, p, arg); break;
    default: break;
    }
}

/* One command of a track. 1 when it ends the track (0xff). */
static int run_command(Track *t, Player *p, int play)
{
    int execute = 1, type_given = 0, type = 0;
    int command = read_byte(t);
    if (command == 0xa2) { /* only if the last comparison held */
        command = read_byte(t);
        execute = (t->flags & TRACK_CONDITION) != 0;
    }
    if (command == 0xa0) { /* its argument a random number */
        command = read_byte(t);
        type = ARG_RANDOM;
        type_given = 1;
    }
    if (command == 0xa1) { /* its argument a variable's value */
        command = read_byte(t);
        type = ARG_VARIABLE;
        type_given = 1;
    }
    if (!(command & 0x80)) {
        note(t, p, play, command, execute, type_given ? type : ARG_VARIABLE_LENGTH);
        return 0;
    }
    switch (command & 0xf0) {
    case 0x80: {
        const int32_t arg = read_arg(t, p, type_given ? type : ARG_VARIABLE_LENGTH);
        if (!execute) {
            break;
        }
        if (command == 0x80) { /* a rest */
            t->wait = arg;
        } else if (command == 0x81 && arg < 0x10000) { /* a program */
            t->program = (uint16_t)arg;
        }
        break;
    }
    case 0x90:
        if (command == 0x93) { /* another of the player's tracks starts, at an offset */
            const int n = read_byte(t);
            const uint32_t offset = read24(t);
            Track *other;
            if (!execute) {
                break;
            }
            other = player_track(p, n);
            if (other != NULL && other != t) {
                release_channels(other, p, -1);
                free_channels(other);
                other->base = t->base; /* 0x038009d0 */
                other->position = t->base + offset;
            }
        } else if (command == 0x94) { /* a jump */
            const uint32_t offset = read24(t);
            if (execute) {
                t->position = t->base + offset;
            }
        } else if (command == 0x95) { /* a call */
            const uint32_t offset = read24(t);
            if (execute && t->depth < 3) {
                t->call_stack[t->depth++] = t->position;
                t->position = t->base + offset;
            }
        }
        break;
    case 0xb0: variable_command(t, p, command, execute, type_given ? type : ARG_HALFWORD); break;
    case 0xc0:
    case 0xd0: setting_command(t, p, command, execute, type_given ? type : ARG_BYTE); break;
    case 0xe0: {
        const int16_t arg = (int16_t)read_arg(t, p, type_given ? type : ARG_HALFWORD);
        if (!execute) {
            break;
        }
        if (command == 0xe0) {
            t->lfo.delay = (uint16_t)arg;
        } else if (command == 0xe1) {
            p->tempo = (uint16_t)arg;
        } else if (command == 0xe3) {
            t->sweep_pitch = arg;
        }
        break;
    }
    case 0xf0:
        if (!execute) {
            break;
        }
        if (command == 0xfc) { /* a loop's end */
            uint8_t count;
            if (t->depth == 0) {
                break;
            }
            count = t->loop_count[t->depth - 1];
            if (count != 0 && --count == 0) {
                --t->depth;
                break;
            }
            t->loop_count[t->depth - 1] = count;
            t->position = t->call_stack[t->depth - 1];
        } else if (command == 0xfd) { /* a return */
            if (t->depth != 0) {
                t->position = t->call_stack[--t->depth];
            }
        } else if (command == 0xff) { /* the end */
            return 1;
        }
        break;
    default: break;
    }
    return 0;
}

/* A tick of a track (0x03800cf0's loop): its notes' lengths count down
 * (and their sweeps, those it times), then it runs its commands until one
 * makes it wait. 1 when it ended. */
static int step_track(Track *t, Player *p, int play)
{
    for (Snd7Channel *c = t->channels; c != NULL; c = c->next) {
        if (c->length > 0) {
            --c->length;
        }
        if (!c->auto_sweep && c->sweep_counter < c->sweep_length) {
            ++c->sweep_counter;
        }
    }
    if (t->flags & TRACK_NOTE_FINISH_WAIT) {
        if (t->channels != NULL) {
            return 0;
        }
        t->flags &= (uint8_t)~TRACK_NOTE_FINISH_WAIT;
    }
    if (t->wait > 0 && --t->wait > 0) {
        return 0;
    }
    cache_load(t->position);
    while (t->wait == 0 && !(t->flags & TRACK_NOTE_FINISH_WAIT)) {
        if (run_command(t, p, play)) {
            return 1;
        }
    }
    return 0;
}

/* 0x03800cf0: a tick of a player's tracks, those started; one that ends
 * is finished. With `play` 0 (a skip) no note starts. 1 when none is
 * left playing. */
static int step_tracks(Player *p, int play)
{
    int playing = 0;
    for (int n = 0; n < 16; ++n) {
        Track *t = player_track(p, n);
        if (t == NULL || t->position == 0) {
            continue;
        }
        if (step_track(t, p, play)) {
            finish_track(p, n);
        } else {
            playing = 1;
        }
    }
    return !playing;
}

/* --- The players -------------------------------------------------------------------- */

void snd7_seq_reset(void)
{
    memset(players, 0, sizeof(players));
    memset(tracks, 0, sizeof(tracks));
    memset(&cache, 0, sizeof(cache));
}

/* 0x037ffef0 */
void snd7_seq_init(void)
{
    for (int n = 0; n < 16; ++n) {
        players[n].flags &= (uint8_t)~PLAYER_ACTIVE;
        players[n].number = (uint8_t)n;
    }
    for (int n = 0; n < 32; ++n) {
        tracks[n].flags &= (uint8_t)~TRACK_ACTIVE;
    }
}

/* 0x037fff54: the sequencer's part of each loop of the driver. With
 * `periodic` (every 5.21 ms) each playing player's tempo counter gives its
 * ticks, which its tracks play; then every started player's notes take
 * their tracks' settings. The ARM9 reads which players are active (+4 of
 * the shared work) and how many ticks each has played. */
void snd7_seq_main(int periodic)
{
    uint32_t active = 0;
    for (int n = 0; n < 16; ++n) {
        Player *p = &players[n];
        if (!(p->flags & PLAYER_ACTIVE)) {
            continue;
        }
        if (p->flags & PLAYER_STARTED) {
            if (periodic && !(p->flags & PLAYER_PAUSED)) {
                int ticks = 0, played = 0;
                while (p->tempo_counter >= 240) {
                    p->tempo_counter -= 240;
                    ++ticks;
                }
                for (; played < ticks; ++played) {
                    if (step_tracks(p, 1)) {
                        finish_player(p);
                        break;
                    }
                }
                if (snd7_shared_work != 0) {
                    const uint32_t at = 0x40 + 0x24 * p->number;
                    snd7_shared_write32(at, snd7_shared_read32(at) + (uint32_t)played);
                }
                p->tempo_counter = (uint16_t)(p->tempo_counter + ((int32_t)((uint32_t)p->tempo * p->tempo_ratio) >> 8));
            }
            for (int i = 0; i < 16; ++i) {
                Track *t = player_track(p, i);
                if (t != NULL) {
                    update_channels(t, p, 1);
                }
            }
        }
        if (p->flags & PLAYER_ACTIVE) {
            active |= 1u << n;
        }
    }
    if (snd7_shared_work != 0) {
        snd7_shared_write32(4, active);
    }
}

/* 0x038000b4: player `n` takes a sequence -- its data at `sequence`, its
 * first track's commands at `offset` -- and a bank, with its tracks: one,
 * or, when the commands begin with 0xfe and a mask of tracks, one more
 * for each track of the mask past the first. It does not start yet. Its
 * variables are -1 and its ticks 0. */
void snd7_seq_prepare(int n, uint32_t sequence, uint32_t offset, uint32_t bank)
{
    Player *p = player_at(n);
    Track *first;
    int t;
    if (p->flags & PLAYER_ACTIVE) {
        finish_player(p);
    }
    p->flags &= (uint8_t)~PLAYER_PAUSED;
    p->bank = bank;
    p->tempo = 120;
    p->tempo_ratio = 256;
    p->tempo_counter = 240;
    p->volume = 127;
    p->volume_offset = 0;
    p->priority = 64;
    memset(p->tracks, 0xff, sizeof(p->tracks));
    if (snd7_shared_work != 0) {
        snd7_shared_write32(0x40 + 0x24 * p->number, 0);
        for (uint32_t i = 0; i < 16; ++i) {
            snd7_shared_write16(0x20 + 0x24 * p->number + 2 * i, 0xffff);
        }
    }
    t = alloc_track();
    if (t < 0) {
        return;
    }
    first = &tracks[t];
    track_init(first);
    first->base = sequence;
    first->position = sequence + offset;
    p->tracks[0] = (uint8_t)t;
    cache_load(first->position);
    if (read_byte(first) != 0xfe) {
        --first->position;
    } else {
        uint32_t mask = read16(first) >> 1;
        for (int i = 1; mask != 0; ++i, mask >>= 1) {
            if (mask & 1) {
                const int u = alloc_track();
                if (u < 0) {
                    break;
                }
                track_init(&tracks[u]);
                p->tracks[i] = (uint8_t)u;
            }
        }
    }
    p->flags = (uint8_t)((p->flags | PLAYER_ACTIVE) & ~PLAYER_STARTED);
    if (snd7_shared_work != 0) {
        snd7_shared_write32(4, snd7_shared_read32(4) | 1u << n);
    }
}

/* 0x038002f0 */
void snd7_seq_start_prepared(int n)
{
    player_at(n)->flags |= PLAYER_STARTED;
}

/* 0x03800310 */
void snd7_seq_start(int n, uint32_t sequence, uint32_t offset, uint32_t bank)
{
    snd7_seq_prepare(n, sequence, offset, bank);
    player_at(n)->flags |= PLAYER_STARTED;
}

/* 0x03800340 */
static uint32_t stops[16]; /* snd7_seq_player_stops */

uint32_t snd7_seq_player_stops(int n)
{
    return n >= 0 && n < 16 ? stops[n] : 0;
}

void snd7_seq_stop(int n)
{
    Player *p = player_at(n);
    if (n >= 0 && n < 16) {
        ++stops[n];
    }
    if (!(p->flags & PLAYER_ACTIVE)) {
        return;
    }
    finish_player(p);
    if (snd7_shared_work != 0) {
        snd7_shared_write32(4, snd7_shared_read32(4) & ~(1u << n));
    }
}

/* 0x0380039c: paused, its notes are released fast and let go. */
void snd7_seq_pause(int n, int pause)
{
    Player *p = player_at(n);
    p->flags = (uint8_t)((p->flags & ~PLAYER_PAUSED) | (pause & 1 ? PLAYER_PAUSED : 0));
    if (pause == 0) {
        return;
    }
    for (int i = 0; i < 16; ++i) {
        Track *t = player_track(p, i);
        if (t != NULL) {
            release_channels(t, p, 127);
            free_channels(t);
        }
    }
}

/* 0x03800414: `ticks` ticks played at once, without their notes (theirs
 * released fast and let go first); the periodic alarm starts again after. */
void snd7_seq_skip(int n, uint32_t ticks)
{
    Player *p = player_at(n);
    uint32_t played = 0;
    for (int i = 0; i < 16; ++i) {
        Track *t = player_track(p, i);
        if (t != NULL) {
            release_channels(t, p, 127);
            free_channels(t);
        }
    }
    snd7_interval_timer_stop();
    for (; played < ticks; ++played) {
        if (step_tracks(p, 0)) {
            finish_player(p);
            break;
        }
    }
    snd7_interval_timer_start();
    if (snd7_shared_work != 0) {
        const uint32_t at = 0x40 + 0x24 * p->number;
        snd7_shared_write32(at, snd7_shared_read32(at) + played);
    }
}

/* 0x038004e0 */
void snd7_seq_mute_track(int n, uint32_t mask, int mode)
{
    Player *p = player_at(n);
    for (int i = 0; i < 16 && mask != 0; ++i, mask >>= 1) {
        if (mask & 1) {
            Track *t = player_track(p, i);
            if (t != NULL) {
                mute_track(t, p, mode);
            }
        }
    }
}

/* 0x0380054c */
void snd7_seq_allocatable_channels(int n, uint32_t mask, uint32_t channels)
{
    Player *p = player_at(n);
    for (int i = 0; i < 16 && mask != 0; ++i, mask >>= 1) {
        if (mask & 1) {
            Track *t = player_track(p, i);
            if (t != NULL) {
                t->channel_mask = (uint16_t)channels;
                t->flags |= TRACK_OWN_CHANNELS;
            }
        }
    }
}

/* 0x038005bc: the players reading data in [start, end] stop (it is going
 * away). */
void snd7_seq_invalidate(uint32_t start, uint32_t end)
{
    for (int n = 0; n < 16; ++n) {
        Player *p = &players[n];
        if (!(p->flags & PLAYER_ACTIVE)) {
            continue;
        }
        for (int i = 0; i < 16; ++i) {
            const Track *t = player_track(p, i);
            if (t != NULL && start <= t->position && t->position <= end) {
                finish_player(p);
                break;
            }
        }
    }
}

/* 0x03800648: the players whose bank is in [start, end] stop. */
void snd7_seq_invalidate_bank(uint32_t start, uint32_t end)
{
    for (int n = 0; n < 16; ++n) {
        Player *p = &players[n];
        if ((p->flags & PLAYER_ACTIVE) && start <= p->bank && p->bank <= end) {
            finish_player(p);
        }
    }
}

/* A field of a record set by its offset, `size` 1, 2 or 4 bytes (any other
 * does nothing). */
static void set_field(void *record, size_t record_size, uint32_t offset, uint32_t data, int size)
{
    uint8_t *at = (uint8_t *)record + offset;
    if (size != 1 && size != 2 && size != 4) {
        return;
    }
    if (offset % (uint32_t)size != 0 || offset + (uint32_t)size > record_size) {
        snd7_stop("a parameter of %d bytes at +0x%x, not a field of the record", size, offset);
    }
    for (int i = 0; i < size; ++i) {
        at[i] = (uint8_t)(data >> (8 * i));
    }
}

/* 0x038006a4 */
void snd7_seq_player_param(int n, uint32_t offset, uint32_t data, int size)
{
    set_field(player_at(n), KHDAYS_SND7_PLAYER_SIZE, offset, data, size);
}

/* 0x038006e8: the field at `offset` of each of the player's tracks in
 * `mask`. */
void snd7_seq_track_param(int n, uint32_t mask, uint32_t offset, uint32_t data, int size)
{
    Player *p = player_at(n);
    for (int i = 0; i < 16 && mask != 0; ++i, mask >>= 1) {
        if (mask & 1) {
            Track *t = player_track(p, i);
            if (t != NULL) {
                set_field(t, KHDAYS_SND7_TRACK_COMPARED, offset, data, size);
            }
        }
    }
}

/* 0x03801c50: player `n`'s variable (by its number, which is the player's
 * own; the ARM7 writes it without looking for the shared work -- to its
 * BIOS, which takes nothing, before there is one). */
void snd7_seq_set_local_variable(int n, int variable_number, int16_t value)
{
    if (snd7_shared_work != 0) {
        set_variable((int32_t)(0x20 + 0x24 * n + 2 * variable_number), value);
    }
}

/* 0x03801c70 */
void snd7_seq_set_global_variable(int variable_number, int16_t value)
{
    if (snd7_shared_work != 0) {
        set_variable((int32_t)(0x260 + 2 * variable_number), value);
    }
}

/* A channel's note's player: the note's track (the channel's callback data
 * while it is the sequencer's, 0x03800b38), and the active player that has
 * that track. */
int snd7_seq_channel_player(int channel)
{
    const Snd7Channel *c = channel >= 0 && channel < snd7_channel_count() ? snd7_channel_at(channel) : NULL;
    int track;
    if (c == NULL || c->callback != channel_event) {
        return -1;
    }
    track = (int)((const Track *)c->callback_data - tracks);
    for (int n = 0; n < 16; ++n) {
        if (players[n].flags & PLAYER_ACTIVE) {
            for (int i = 0; i < 16; ++i) {
                if (players[n].tracks[i] == track) {
                    return n;
                }
            }
        }
    }
    return -1;
}
