/* The native sound driver's channel registers (snd7_internal.h): set up, stop, volume, timer and pan,
 * the master pan and surround decay, and its timer and volume arithmetic. Each function is the routine
 * at the ARM7 address it names, the registers written in the same order and sizes (the channels react
 * to each write). */
#include "snd7_internal.h"

#include <stdlib.h>
#include <string.h>

/* The layer's state, at its ARM7 addresses. */
static int32_t master_pan;     /* 0x03807400 (initialised data): < 0, none */
static int32_t surround_decay; /* 0x038077b8: 0, none */
static uint8_t pan_of[16];     /* 0x038077bc: each channel's own pan */
static uint8_t volume_of[16];  /* 0x038077cc: each channel's own volume */
/* the port's voices' own pan and volume (channels 16 on), grown as they are made */
static uint8_t *voice_pan, *voice_volume;
static int voice_slots;

static uint8_t *slot(uint8_t *ds, uint8_t **voice, int channel)
{
    static uint8_t spare;
    if (channel < 16) {
        return &ds[channel];
    }
    if (channel - 16 >= voice_slots) {
        const int room = (channel - 16 + 1) * 2;
        uint8_t *p = (uint8_t *)realloc(voice_pan, (size_t)room);
        uint8_t *v = p != NULL ? (uint8_t *)realloc(voice_volume, (size_t)room) : NULL;
        if (p != NULL)
            voice_pan = p;
        if (v == NULL) {
            return &spare;
        }
        voice_volume = v;
        memset(voice_pan + voice_slots, 0, (size_t)(room - voice_slots));
        memset(voice_volume + voice_slots, 0, (size_t)(room - voice_slots));
        voice_slots = room;
    }
    return &(*voice)[channel - 16];
}
#define PAN_OF(n) (*slot(pan_of, &voice_pan, (n)))
#define VOLUME_OF(n) (*slot(volume_of, &voice_volume, (n)))

/* The channels the surround decay leaves alone are 1 and 3 (the literal
 * 0xfff5 each routine tests). */
#define SURROUND_CHANNELS 0xfff5u

/* (a voice's past the DS's, khdays_sound_hw_base) */
#define CNT(n) khdays_sound_hw_base(n)
#define SAD(n) (khdays_sound_hw_base(n) + 4)
#define TMR(n) (khdays_sound_hw_base(n) + 8)
#define PNT(n) (khdays_sound_hw_base(n) + 0xa)
#define LEN(n) (khdays_sound_hw_base(n) + 0xc)

void snd7_hw_reset(void)
{
    const uint8_t *word = snd7_segment + (0x03807400u - 0x037f8000u);
    master_pan = (int32_t)(word[0] | word[1] << 8 | word[2] << 16 | (uint32_t)word[3] << 24);
    surround_decay = 0;
    for (int n = 0; n < 16; ++n) {
        pan_of[n] = 0;
        volume_of[n] = 0;
    }
}

/* 0x037fec40: a volume taken down toward the sides, by the pan. */
static int surround(int volume, int pan)
{
    if (pan < 0x18) {
        return (volume * (surround_decay * (pan + 0x28) + ((0x7fff - surround_decay) << 6))) >> 21;
    }
    if (pan <= 0x68) {
        return volume;
    }
    return (volume * (-surround_decay * (pan - 0x28) + ((surround_decay + 0x7fff) << 6))) >> 21;
}

static int decays(int channel)
{
    return surround_decay > 0 && (channel >= 16 || (SURROUND_CHANNELS & (1u << channel)) != 0);
}

/* 0x037fe804 */
void snd7_hw_setup_pcm(int channel, uint32_t source, int format, int repeat, int loop_start, uint32_t length,
                       int volume, int shift, int timer, int pan)
{
    PAN_OF(channel) = (uint8_t)pan;
    if (master_pan >= 0) {
        pan = master_pan;
    }
    VOLUME_OF(channel) = (uint8_t)volume;
    if (decays(channel)) {
        volume = surround(volume, pan);
    }
    snd7_reg_write(CNT(channel), 4,
                   (uint32_t)volume |
                       ((uint32_t)format << 29 | (uint32_t)repeat << 27 | (uint32_t)pan << 16 | (uint32_t)shift << 8));
    snd7_reg_write(TMR(channel), 2, (uint16_t)(0x10000 - timer));
    snd7_reg_write(PNT(channel), 2, (uint16_t)loop_start);
    snd7_reg_write(LEN(channel), 4, length);
    snd7_reg_write(SAD(channel), 4, source);
}

/* 0x037fe8d0 */
void snd7_hw_setup_psg(int channel, int duty, int volume, int shift, int timer, int pan)
{
    PAN_OF(channel) = (uint8_t)pan;
    if (master_pan >= 0) {
        pan = master_pan;
    }
    VOLUME_OF(channel) = (uint8_t)volume;
    if (decays(channel)) {
        volume = surround(volume, pan);
    }
    snd7_reg_write(CNT(channel), 4,
                   (uint32_t)volume |
                       ((uint32_t)duty << 24 | 0x60000000u | (uint32_t)pan << 16 | (uint32_t)shift << 8));
    snd7_reg_write(TMR(channel), 2, (uint16_t)(0x10000 - timer));
}

/* 0x037fe97c */
void snd7_hw_setup_noise(int channel, int volume, int shift, int timer, int pan)
{
    PAN_OF(channel) = (uint8_t)pan;
    if (master_pan >= 0) {
        pan = master_pan;
    }
    VOLUME_OF(channel) = (uint8_t)volume;
    if (decays(channel)) {
        volume = surround(volume, pan);
    }
    snd7_reg_write(CNT(channel), 4, (uint32_t)volume | ((uint32_t)pan << 16 | 0x60000000u | (uint32_t)shift << 8));
    snd7_reg_write(TMR(channel), 2, (uint16_t)(0x10000 - timer));
}

/* 0x037fea20: the start bit cleared; Hold set when asked. */
void snd7_hw_stop(int channel, int hold)
{
    uint32_t cnt = snd7_reg_read(CNT(channel), 4) & ~0x80000000u;
    if (hold & 1) {
        cnt |= 0x8000u;
    }
    snd7_reg_write(CNT(channel), 4, cnt);
}

/* 0x037fea48 */
void snd7_hw_set_volume(int channel, int volume, int shift)
{
    VOLUME_OF(channel) = (uint8_t)volume;
    if (decays(channel)) {
        volume = surround(volume, (int)snd7_reg_read(CNT(channel) + 2, 1));
    }
    snd7_reg_write(CNT(channel), 2, (uint16_t)((uint32_t)volume | (uint32_t)shift << 8));
}

/* 0x037feac0 */
void snd7_hw_set_timer(int channel, int timer)
{
    snd7_reg_write(TMR(channel), 2, (uint16_t)(0x10000 - timer));
}

/* 0x037fead8 */
void snd7_hw_set_pan(int channel, int pan)
{
    PAN_OF(channel) = (uint8_t)pan;
    if (master_pan >= 0) {
        pan = master_pan;
    }
    snd7_reg_write(CNT(channel) + 2, 1, (uint8_t)pan);
    if (decays(channel)) {
        snd7_reg_write(CNT(channel), 1, (uint8_t)surround(VOLUME_OF(channel), pan));
    }
}

/* 0x037feb50: the busy bit. */
int snd7_hw_is_active(int channel)
{
    return (snd7_reg_read(CNT(channel) + 3, 1) & 0x80) != 0;
}

/* 0x037feb6c: a pan over every channel, or (< 0) each its own again. */
void snd7_hw_set_master_pan(int pan)
{
    master_pan = pan;
    for (int n = 0; n < snd7_channel_count(); ++n) {
        snd7_reg_write(CNT(n) + 2, 1, pan >= 0 ? (uint8_t)pan : PAN_OF(n));
    }
}

/* 0x037febd0 */
uint32_t snd7_hw_control(int channel)
{
    return snd7_reg_read(CNT(channel), 4);
}

/* 0x037febe0: the volumes taken down again with the new decay. */
void snd7_hw_set_surround_decay(int decay)
{
    surround_decay = decay;
    for (int n = 0; n < snd7_channel_count(); ++n) {
        if (n >= 16 || (SURROUND_CHANNELS & (1u << n))) {
            snd7_reg_write(CNT(n), 1, (uint8_t)surround(VOLUME_OF(n), (int)snd7_reg_read(CNT(n) + 2, 1)));
        }
    }
}

/* 0x037fecac: a timer raised by `pitch` 64ths of a semitone (768 an
 * octave), through the BIOS's pitch table, as 64 bits; 0x10 at least, 0xffff
 * at most (and when it does not fit). */
uint16_t snd7_hw_calc_timer(int timer, int pitch)
{
    int octaves = 0;
    int step = -pitch;
    uint64_t value;
    while (step < 0) {
        --octaves;
        step += 0x300;
    }
    while (step >= 0x300) {
        ++octaves;
        step -= 0x300;
    }
    value = (uint64_t)((int64_t)timer * (int64_t)(uint32_t)(snd7_pitch_table(step) + 0x10000));
    octaves -= 16;
    if (octaves <= 0) {
        value = -octaves < 64 ? value >> -octaves : 0;
    } else if (octaves < 32) {
        if (value & (~(uint64_t)0 << (64 - octaves))) {
            return 0xffff;
        }
        value <<= octaves;
    } else {
        return 0xffff;
    }
    if (value < 0x10) {
        return 0x10;
    }
    if (value > 0xffff) {
        return 0xffff;
    }
    return (uint16_t)value;
}

/* 0x037fedd8: a level in tenths of a decibel (-723..0) as the channel's
 * volume (the BIOS's volume table) and divider (the shift in bits 8-9). */
uint16_t snd7_hw_calc_volume(int decibels)
{
    int shift;
    if (decibels < -723) {
        decibels = -723;
    } else if (decibels > 0) {
        decibels = 0;
    }
    if (decibels < -240) {
        shift = 3;
    } else if (decibels < -120) {
        shift = 2;
    } else if (decibels < -60) {
        shift = 1;
    } else {
        shift = 0;
    }
    return (uint16_t)(snd7_volume_table(decibels + 723) | shift << 8);
}

/* 0x037fee58: the LFO's sine, a quarter wave of 33 entries in the segment
 * (0x03806e60) read four ways. */
int8_t snd7_hw_sin(int index)
{
    if (index < 0x20) {
        return snd7_segment_s8(0x03806e60u + (uint32_t)index);
    }
    if (index < 0x40) {
        return snd7_segment_s8(0x03806e60u + (uint32_t)(0x40 - index));
    }
    if (index < 0x60) {
        return (int8_t)-snd7_segment_s8(0x03806e60u + (uint32_t)(index - 0x40));
    }
    return (int8_t)-snd7_segment_s8(0x03806e60u + (uint32_t)(0x20 - (index - 0x60)));
}

/* 0x037fe69c: SOUNDCNT's master enable (bit 15). */
void snd7_hw_enable(void)
{
    snd7_reg_write(0x101, 1, snd7_reg_read(0x101, 1) | 0x80);
}

/* 0x037fe7bc */
void snd7_hw_set_master_volume(int volume)
{
    snd7_reg_write(0x100, 1, (uint8_t)volume);
}

/* 0x037fe7cc: SOUNDCNT's upper byte, the enable kept. */
void snd7_hw_set_output_selector(int left, int right, int mixch1, int mixch3)
{
    const uint32_t enabled = (snd7_reg_read(0x101, 1) & 0x80) != 0;
    snd7_reg_write(0x101, 1,
                   (uint8_t)((uint32_t)left |
                             ((uint32_t)mixch3 << 5 | enabled << 7 | (uint32_t)mixch1 << 4 | (uint32_t)right << 2)));
}
