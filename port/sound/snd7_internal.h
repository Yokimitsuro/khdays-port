/* What the native sound driver's parts share (port/sound/snd7_*.c): the
 * channels it drives, the DS's memory, the ARM7 segment its tables come
 * from, and the parts' functions. Each function names the ARM7 address of
 * the driver's routine it is (docs/RUNTIME_MAP.md section 6). */
#ifndef KHDAYS_SOUND_SND7_INTERNAL_H
#define KHDAYS_SOUND_SND7_INTERNAL_H

#include "channels.h"
#include "snd7.h"

#include <stdint.h>

/* --- The driver's world ------------------------------------------------------------ */

extern KhdaysSoundHw *snd7_channels;    /* the sixteen channels it drives */
extern const KhdaysSnd7Host *snd7_host; /* the bridge */
extern uintptr_t snd7_ds_base;          /* where DS address 0 is */
extern const uint8_t *snd7_segment;     /* the ARM7 binary's driver segment, at 0x037f8000 */
extern uint64_t snd7_now;               /* the driver's time, in cycles */

/* The DS's memory (the ARM9's lists, sequences, banks, waves). */
static inline uint8_t *snd7_ds(uint32_t address)
{
    return (uint8_t *)(snd7_ds_base + address);
}
static inline uint8_t snd7_rd8(uint32_t a)
{
    return *(volatile uint8_t *)snd7_ds(a);
}
static inline uint16_t snd7_rd16(uint32_t a)
{
    return *(volatile uint16_t *)snd7_ds(a);
}
static inline uint32_t snd7_rd32(uint32_t a)
{
    return *(volatile uint32_t *)snd7_ds(a);
}

/* The ARM7 segment's data (its tables), by ARM7 address. */
static inline int8_t snd7_segment_s8(uint32_t address)
{
    return (int8_t)snd7_segment[address - 0x037f8000u];
}
static inline uint8_t snd7_segment_u8(uint32_t address)
{
    return snd7_segment[address - 0x037f8000u];
}
static inline int16_t snd7_segment_s16(uint32_t address)
{
    const uint8_t *p = snd7_segment + (address - 0x037f8000u);
    return (int16_t)(p[0] | p[1] << 8);
}
static inline uint32_t snd7_segment_u32(uint32_t address)
{
    const uint8_t *p = snd7_segment + (address - 0x037f8000u);
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The segment's tables the driver reads. */
#define SND7_DECIBEL_TABLE 0x03806e84u /* s16[128]: a 0-127 level (velocity, sustain) in tenths of a dB */
#define SND7_ATTACK_TABLE 0x03807098u  /* u8[19]: the attack rates of 109-127 */
#define SND7_SHIFT_TABLE 0x03807084u   /* u8[4]: a volume's divider as a shift */
#define SND7_CHANNEL_ORDER 0x03807088u /* u8[16]: the order channels are taken in */
#define SND7_SINE_TABLE 0x03806e60u    /* s8[33]: a quarter of the LFO's sine */

/* --- The channels the driver plays notes on (snd7_channel.c) ------------------------- */

typedef struct Snd7Channel Snd7Channel;

/* Why a channel's owner hears from it: it was taken or freed (0), or its
 * sound ended (1). */
enum { SND7_CHANNEL_DROPPED = 0, SND7_CHANNEL_FINISHED = 1 };
typedef void (*Snd7ChannelCallback)(Snd7Channel *channel, int event, void *data);

/* An LFO's settings: a track's (+0x18 in the ARM7's), which each of its
 * notes takes (a channel's, +0x28). */
typedef struct Snd7LfoParam {
    uint8_t target; /* +0: 0 pitch, 1 volume, 2 pan */
    uint8_t speed;  /* +1 */
    uint8_t depth;  /* +2 */
    uint8_t range;  /* +3 */
    uint16_t delay; /* +4 */
} Snd7LfoParam;

typedef struct Snd7Lfo {  /* +0x28 in the ARM7's channel */
    Snd7LfoParam param;   /* +0 */
    uint16_t delay_count; /* +6 */
    uint16_t counter;     /* +8 */
} Snd7Lfo;

/* The ARM7's channel record (0x54 bytes each at 0x03807d18), field for
 * field, the offsets in the comments. */
struct Snd7Channel {
    uint16_t number;         /* +0x00 (a byte; the port's voices are 16 on) */
    uint8_t type;            /* +0x01: 0 PCM, 1 PSG, 2 noise */
    uint8_t envelope_status; /* +0x02: 0 attack, 1 decay, 2 sustain, 3 release */
    /* +0x03: bit 0 active, bit 1 start asked, bit 2 auto sweep, bits 3-7
     * what the registers still need (SND7_SYNC_*) */
    uint8_t active, start, auto_sweep, sync;
    uint8_t pan_range;     /* +0x04 */
    uint8_t original_key;  /* +0x05 */
    int16_t user_decay2;   /* +0x06 */
    uint8_t key;           /* +0x08 */
    uint8_t velocity;      /* +0x09 */
    int8_t init_pan;       /* +0x0a */
    int8_t user_pan;       /* +0x0b */
    int16_t user_decay;    /* +0x0c */
    int16_t user_pitch;    /* +0x0e */
    int32_t envelope;      /* +0x10: in 128ths of a tenth of a dB */
    int32_t sweep_counter; /* +0x14 */
    int32_t sweep_length;  /* +0x18 */
    uint8_t attack;        /* +0x1c */
    uint8_t sustain;       /* +0x1d */
    uint16_t decay;        /* +0x1e */
    uint16_t release;      /* +0x20 */
    uint8_t priority;      /* +0x22 */
    uint8_t pan;           /* +0x23: as last given to the registers */
    uint16_t volume;       /* +0x24: volume | divider << 8, as last given */
    uint16_t timer;        /* +0x26: as last given */
    Snd7Lfo lfo;           /* +0x28 */
    int16_t sweep_pitch;   /* +0x32 */
    int32_t length;        /* +0x34 */
    /* +0x38: the wave's parameters (SNDWaveParam, 12 bytes) */
    uint8_t format;               /* +0x38 */
    uint8_t loop;                 /* +0x39 */
    uint16_t rate;                /* +0x3a */
    uint16_t base_timer;          /* +0x3c */
    uint16_t loop_start;          /* +0x3e */
    uint32_t loop_length;         /* +0x40 */
    uint32_t data;                /* +0x44: the wave's address; PSG: the duty */
    Snd7ChannelCallback callback; /* +0x48 */
    void *callback_data;          /* +0x4c */
    Snd7Channel *next;            /* +0x50: the next of its track's notes */
};

enum { SND7_SYNC_START = 1, SND7_SYNC_STOP = 2, SND7_SYNC_TIMER = 4, SND7_SYNC_VOLUME = 8, SND7_SYNC_PAN = 16 };

extern Snd7Channel snd7_channel[16];

/* The channels: the DS's sixteen, then the port's voices (snd7_channel_alloc_wave), each record where it
 * was made (a track's notes point at them). */
int snd7_channel_count(void);
Snd7Channel *snd7_channel_at(int n);

/* A channel for a note of a wave (snd7_channel_alloc's arguments): with the hardware's voices allowed
 * (KhdaysSoundHw allow_voices: the PC's output), when none of the note's channels is free one of the
 * port's voices -- a free one, or a new one -- so that the note takes no channel still sounding nor is
 * left out; else snd7_channel_alloc's, as the DS's. */
Snd7Channel *snd7_channel_alloc_wave(uint32_t mask, int priority, int strong, Snd7ChannelCallback callback, void *data);

void snd7_channel_init(void);                                                                         /* 0x037ff164 */
void snd7_channel_sync(void);                                                                         /* 0x037ff1c0 */
void snd7_channel_main(int periodic);                                                                 /* 0x037ff3ac */
int snd7_channel_start_pcm(Snd7Channel *channel, uint32_t wave_param, uint32_t data, int32_t length); /* 0x037ff760 */
int snd7_channel_start_psg(Snd7Channel *channel, int duty, int32_t length);                           /* 0x037ff79c */
int snd7_channel_start_noise(Snd7Channel *channel, int32_t length);                                   /* 0x037ff7e8 */
int snd7_channel_envelope(Snd7Channel *channel, int periodic);                                        /* 0x037ff82c */
void snd7_channel_set_attack(Snd7Channel *channel, int attack);                                       /* 0x037ff8d4 */
void snd7_channel_set_decay(Snd7Channel *channel, int decay);                                         /* 0x037ff8f4 */
void snd7_channel_set_sustain(Snd7Channel *channel, int sustain);                                     /* 0x037ff910 */
void snd7_channel_set_release(Snd7Channel *channel, int release);                                     /* 0x037ff918 */
void snd7_channel_release(Snd7Channel *channel);                                                      /* 0x037ff934 */
int snd7_channel_is_active(const Snd7Channel *channel);                                               /* 0x037ff940 */
Snd7Channel *snd7_channel_alloc(uint32_t mask, int priority, int strong, Snd7ChannelCallback callback,
                                void *data);                     /* 0x037ff950 */
void snd7_channel_forget_callback(Snd7Channel *channel);         /* 0x037ffb1c */
void snd7_channel_free(uint32_t mask);                           /* 0x037ffb30 */
void snd7_channel_lock(uint32_t mask, int flags);                /* 0x037ffbe0 */
void snd7_channel_unlock(uint32_t mask, int flags);              /* 0x037ffcc0 */
uint32_t snd7_channel_locked(int flags);                         /* 0x037ffcf8 */
void snd7_channel_invalidate_wave(uint32_t start, uint32_t end); /* 0x037ffd14 */
void snd7_lfo_init(Snd7LfoParam *param);                         /* 0x037ffd90 */
void snd7_lfo_update(Snd7Lfo *lfo);                              /* 0x037ffdb4 */
int32_t snd7_lfo_value(const Snd7Lfo *lfo);                      /* 0x037ffe14 */
uint16_t snd7_release_rate(int value);                           /* 0x037ffe64 */
void snd7_channel_start(Snd7Channel *channel, int32_t length);   /* 0x037ffeb8 */

/* --- The shared work, where the ARM9 reads the driver (snd7_command.c) ------------- */

/* Its address in the DS's memory (SND_COMMAND_SHARED_WORK; 0 before), and
 * its reads and writes -- the real one's, or the lockstep check's copy's.
 * +0x04 the players active, +0x08 the channels playing, +0x0a the capture
 * units, +0x20 + 0x24 * n player n's 16 variables, +0x40 + 0x24 * n its
 * ticks played, +0x260 the 16 global variables. */
#define SND7_SHARED_WORK_SIZE 0x280u
extern uint32_t snd7_shared_work;
uint32_t snd7_shared_read32(uint32_t offset);
uint16_t snd7_shared_read16(uint32_t offset);
void snd7_shared_write32(uint32_t offset, uint32_t value);
void snd7_shared_write16(uint32_t offset, uint16_t value);
void snd7_update_shared_status(void); /* 0x03801c8c */

/* --- The rest of the driver -------------------------------------------------------- */

uint16_t snd7_random(void); /* 0x037feec4 */
void snd7_random_reset(void);

/* The driver's periodic alarm (every 5.21 ms), stopped and started again
 * around a skip. */
void snd7_interval_timer_start(void); /* 0x037fef64 */
void snd7_interval_timer_stop(void);  /* 0x037fefb4 */

/* What the ARM7 would do past what the port models (a printf format):
 * said, and the port stops. */
__declspec(noreturn) void snd7_stop(const char *format, ...);

void snd7_command_init(void);             /* 0x03801ed8 */
void snd7_command_process(void);          /* 0x03801f1c */
void snd7_command_receive(uint32_t data); /* 0x038025cc */

void snd7_alarm_init(void);                                                /* 0x03801d24 */
void snd7_alarm_setup(int alarm, uint64_t ticks, uint64_t period, int id); /* 0x03801d50 */
void snd7_alarm_start(int alarm);                                          /* 0x03801dac */
void snd7_alarm_stop(int alarm);                                           /* 0x03801e68 */

/* The sequencer (snd7_seq.c). */
void snd7_seq_reset(void);        /* its state as the ARM7's BSS starts */
void snd7_seq_init(void);         /* 0x037ffef0 */
void snd7_seq_main(int periodic); /* 0x037fff54 */
void snd7_seq_prepare(int player, uint32_t sequence, uint32_t offset, uint32_t bank);             /* 0x038000b4 */
void snd7_seq_start_prepared(int player);                                                         /* 0x038002f0 */
void snd7_seq_start(int player, uint32_t sequence, uint32_t offset, uint32_t bank);               /* 0x03800310 */
void snd7_seq_stop(int player);                                                                   /* 0x03800340 */
void snd7_seq_pause(int player, int pause);                                                       /* 0x0380039c */
void snd7_seq_skip(int player, uint32_t ticks);                                                   /* 0x03800414 */
void snd7_seq_mute_track(int player, uint32_t tracks, int mute);                                  /* 0x038004e0 */
void snd7_seq_allocatable_channels(int player, uint32_t tracks, uint32_t mask);                   /* 0x0380054c */
void snd7_seq_invalidate(uint32_t start, uint32_t end);                                           /* 0x038005bc */
void snd7_seq_invalidate_bank(uint32_t start, uint32_t end);                                      /* 0x03800648 */
void snd7_seq_player_param(int player, uint32_t offset, uint32_t data, int size);                 /* 0x038006a4 */
void snd7_seq_track_param(int player, uint32_t tracks, uint32_t offset, uint32_t data, int size); /* 0x038006e8 */
void snd7_seq_set_local_variable(int player, int variable, int16_t value);                        /* 0x03801c50 */
void snd7_seq_set_global_variable(int variable, int16_t value);                                   /* 0x03801c70 */
int snd7_seq_channel_player(int channel); /* the player whose note a channel plays, -1 none */
uint32_t snd7_seq_player_stops(int n);    /* the ARM9's stops of player n so far */

/* The banks (snd7_bank.c): an instrument as a program of a bank gives it
 * for a key -- the 10 bytes of its settings, and its type. */
typedef struct Snd7Instrument {
    uint8_t type;         /* 1 a wave of the bank's archives, 2 PSG, 3 noise, 4 a wave by its address */
    uint16_t wave;        /* 1: its number in the archive; 2: the duty; 4: the address's low half */
    uint16_t archive;     /* 1: which of the bank's four archives; 4: the address's high half */
    uint8_t original_key; /* the key the wave plays at its own rate */
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release; /* 0xff: the note plays until released */
    uint8_t pan;     /* 0-127, 64 the centre */
} Snd7Instrument;

int snd7_bank_read_instrument(uint32_t bank, int program, int key, Snd7Instrument *instrument); /* 0x03801948 */
uint32_t snd7_bank_wave(uint32_t archive, int wave);                                            /* 0x03801ac8 */
int snd7_bank_note_on(Snd7Channel *channel, int key, int velocity, int32_t length, uint32_t bank,
                      const Snd7Instrument *instrument); /* 0x03801b08 */

/* The capture units (their registers only; the port models no capture). */
void snd7_capture_setup(int unit, int format, uint32_t destination, int length, int loop, int add,
                        int source);  /* 0x037ff0fc */
int snd7_capture_is_active(int unit); /* 0x037ff14c */

/* The sound registers, as the ARM7's accesses reach them (size 1, 2 or 4):
 * the same writes, in the same order, as the ARM7 driver makes. */
static inline uint32_t snd7_reg_read(uint32_t offset, int size)
{
    return khdays_sound_hw_read(snd7_channels, offset, size);
}
static inline void snd7_reg_write(uint32_t offset, int size, uint32_t value)
{
    khdays_sound_hw_write(snd7_channels, offset, size, value);
}

/* --- The BIOS's tables ------------------------------------------------------------- */

/* GetPitchTable (SWI 1Bh): 0x10000 * (2^(i/768) - 1), rounded, i < 768. */
uint16_t snd7_pitch_table(int index);
/* GetVolumeTable (SWI 1Ch): the ARM9's copy of it (0x02041588), i < 724. */
uint8_t snd7_volume_table(int index);

/* --- The channels' registers (snd7_hw.c) ------------------------------------------ */

void snd7_hw_setup_pcm(int channel, uint32_t source, int format, int repeat, int loop_start, uint32_t length,
                       int volume, int shift, int timer, int pan);                        /* 0x037fe804 */
void snd7_hw_setup_psg(int channel, int duty, int volume, int shift, int timer, int pan); /* 0x037fe8d0 */
void snd7_hw_setup_noise(int channel, int volume, int shift, int timer, int pan);         /* 0x037fe97c */
void snd7_hw_stop(int channel, int hold);                                                 /* 0x037fea20 */
void snd7_hw_set_volume(int channel, int volume, int shift);                              /* 0x037fea48 */
void snd7_hw_set_timer(int channel, int timer);                                           /* 0x037feac0 */
void snd7_hw_set_pan(int channel, int pan);                                               /* 0x037fead8 */
int snd7_hw_is_active(int channel);                                                       /* 0x037feb50 */
void snd7_hw_set_master_pan(int pan);                                                     /* 0x037feb6c */
uint32_t snd7_hw_control(int channel);                                                    /* 0x037febd0 */
void snd7_hw_set_surround_decay(int decay);                                               /* 0x037febe0 */
uint16_t snd7_hw_calc_timer(int timer, int pitch);                                        /* 0x037fecac */
uint16_t snd7_hw_calc_volume(int decibels);                                               /* 0x037fedd8 */
int8_t snd7_hw_sin(int index);                                                            /* 0x037fee58 */
void snd7_hw_enable(void);                                                                /* 0x037fe69c */
void snd7_hw_set_master_volume(int volume);                                               /* 0x037fe7bc */
void snd7_hw_set_output_selector(int left, int right, int mixch1, int mixch3);            /* 0x037fe7cc */
void snd7_hw_reset(void); /* the layer's own state, as the ARM7's BSS starts */

#endif
