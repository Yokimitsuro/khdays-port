/* The native sound driver's channels (snd7_internal.h): the sixteen records
 * the ARM7's driver plays notes on (0x03807d18, 0x54 bytes each) -- taking
 * and freeing them, locking them, their envelope, LFO and sweep, and how
 * their volume, pitch and pan reach the registers. Each function is the
 * routine at the ARM7 address it names. Past the sixteen, the port's voices
 * for the PC's output (snd7_channel_alloc_wave). */
#include "snd7_internal.h"

#include <stdio.h>
#include <stdlib.h>

Snd7Channel snd7_channel[16];
static Snd7Channel **voices; /* the port's voices, channels 16 on, each made once */
static int voice_count, voice_room;

int snd7_channel_count(void)
{
    return 16 + voice_count;
}

Snd7Channel *snd7_channel_at(int n)
{
    return n < 16 ? &snd7_channel[n] : voices[n - 16];
}

/* 0x03807cf0: the channels no note may take (+4), and those only a strong
 * request may (+0). */
static uint32_t lock_mask;      /* +4 */
static uint32_t weak_lock_mask; /* +0 */

static int32_t level_of(uint16_t volume)
{
    return ((int32_t)(volume & 0xff) << 4) >> snd7_segment_u8(SND7_SHIFT_TABLE + (uint32_t)(volume >> 8));
}

/* The channel is done: its owner is told, or its priority falls to 0. */
static void finish(Snd7Channel *c)
{
    if (c->callback != NULL) {
        c->callback(c, SND7_CHANNEL_FINISHED, c->callback_data);
    } else {
        c->priority = 0;
    }
    c->volume = 0;
    c->active = 0;
}

/* 0x037ff164 */
void snd7_channel_init(void)
{
    for (int n = 0; n < 16; ++n) {
        snd7_channel[n].number = (uint16_t)n;
        snd7_channel[n].sync = 0;
        snd7_channel[n].active = 0;
    }
    /* the voices go with the hardware's (khdays_sound_hw_reset) */
    for (int v = 0; v < voice_count; ++v) {
        free(voices[v]);
    }
    voice_count = 0;
    lock_mask = 0;
    weak_lock_mask = 0;
}

/* 0x037ff1c0: what each channel's record changed goes to its registers;
 * then the channels started are started, all at once. */
void snd7_channel_sync(void)
{
    for (int n = 0; n < snd7_channel_count(); ++n) {
        Snd7Channel *c = snd7_channel_at(n);
        const int sync = c->sync;
        if (sync == 0) {
            continue;
        }
        if (sync & SND7_SYNC_STOP) {
            snd7_hw_stop(n, 0);
        }
        if (sync & SND7_SYNC_START) {
            switch (c->type) {
            case 0:
                snd7_hw_setup_pcm(n, c->data, c->format, c->loop ? 1 : 2, c->loop_start, c->loop_length,
                                  c->volume & 0xff, c->volume >> 8, c->timer, c->pan);
                break;
            case 1: snd7_hw_setup_psg(n, (int)c->data, c->volume & 0xff, c->volume >> 8, c->timer, c->pan); break;
            case 2: snd7_hw_setup_noise(n, c->volume & 0xff, c->volume >> 8, c->timer, c->pan); break;
            default: break;
            }
            continue;
        }
        if (sync & SND7_SYNC_TIMER) {
            snd7_hw_set_timer(n, c->timer);
        }
        if (sync & SND7_SYNC_VOLUME) {
            snd7_hw_set_volume(n, c->volume & 0xff, c->volume >> 8);
        }
        if (sync & SND7_SYNC_PAN) {
            snd7_hw_set_pan(n, c->pan);
        }
    }
    for (int n = 0; n < snd7_channel_count(); ++n) {
        Snd7Channel *c = snd7_channel_at(n);
        if (c->sync == 0) {
            continue;
        }
        if (c->sync & SND7_SYNC_START) {
            const uint32_t cnt = khdays_sound_hw_base(n);
            snd7_reg_write(cnt + 3, 1, snd7_reg_read(cnt + 3, 1) | 0x80);
        }
        c->sync = 0;
    }
}

/* 0x037ff3ac: each playing channel's volume (tenths of a dB), pitch
 * (64ths of a semitone) and pan from its note, envelope, sweep, LFO and
 * the track's settings; what changed is marked for the registers. With
 * `periodic`, the envelope, sweep and LFO move on a step. */
void snd7_channel_main(int periodic)
{
    for (int n = 0; n < snd7_channel_count(); ++n) {
        Snd7Channel *c = snd7_channel_at(n);
        int32_t volume = 0, pitch = 0, pan = 0;
        int32_t sweep, lfo_value = 0;
        int32_t lfo;
        uint16_t volume_register, timer;
        if (!c->active) {
            continue;
        }
        if (c->start) {
            c->sync |= SND7_SYNC_START;
            c->start = 0;
        } else if (!snd7_hw_is_active(n)) {
            finish(c);
            continue;
        }
        volume += snd7_segment_s16(SND7_DECIBEL_TABLE + 2u * c->velocity);
        pitch += (c->key - c->original_key) * 64;
        volume += snd7_channel_envelope(c, periodic);
        if (c->sweep_pitch == 0 || c->sweep_counter >= c->sweep_length) {
            sweep = 0;
        } else {
            sweep =
                (int32_t)((int64_t)c->sweep_pitch * (c->sweep_length - c->sweep_counter) / (int64_t)c->sweep_length);
            if (periodic && c->auto_sweep) {
                ++c->sweep_counter;
            }
        }
        volume += c->user_decay + c->user_decay2;
        pitch += sweep + c->user_pitch;
        lfo = snd7_lfo_value(&c->lfo);
        if (lfo != 0) {
            int64_t x = lfo;
            switch (c->lfo.param.target) {
            case 0: x *= 64; break;
            case 1: x *= 60; break;
            case 2: x *= 64; break;
            default: break;
            }
            lfo_value = (int32_t)(x >> 14);
        }
        if (periodic) {
            snd7_lfo_update(&c->lfo);
        }
        switch (c->lfo.param.target) {
        case 0: pitch += lfo_value; break;
        case 1:
            if (volume > -0x8000) {
                volume += lfo_value;
            }
            break;
        case 2: pan += lfo_value; break;
        default: break;
        }
        pan += c->init_pan;
        if (c->pan_range != 127) {
            pan = (pan * c->pan_range + 0x40) >> 7;
        }
        pan += c->user_pan;
        if (c->envelope_status == 3 && volume <= -723) {
            c->sync = SND7_SYNC_STOP;
            finish(c);
            continue;
        }
        volume_register = snd7_hw_calc_volume(volume);
        timer = snd7_hw_calc_timer(c->base_timer, pitch);
        if (c->type == 1) {
            timer &= 0xfffc;
        }
        pan += 0x40;
        if (pan < 0) {
            pan = 0;
        } else if (pan > 0x7f) {
            pan = 0x7f;
        }
        if (volume_register != c->volume) {
            c->volume = volume_register;
            c->sync |= SND7_SYNC_VOLUME;
        }
        if (timer != c->timer) {
            c->timer = timer;
            c->sync |= SND7_SYNC_TIMER;
        }
        if (pan != c->pan) {
            c->pan = (uint8_t)pan;
            c->sync |= SND7_SYNC_PAN;
        }
    }
}

/* 0x037ff760: a wave (its SNDWaveParam, 12 bytes at `wave_param` in the
 * DS's memory, and its data). */
int snd7_channel_start_pcm(Snd7Channel *c, uint32_t wave_param, uint32_t data, int32_t length)
{
    c->type = 0;
    c->format = snd7_rd8(wave_param);
    c->loop = snd7_rd8(wave_param + 1);
    c->rate = snd7_rd16(wave_param + 2);
    c->base_timer = snd7_rd16(wave_param + 4);
    c->loop_start = snd7_rd16(wave_param + 6);
    c->loop_length = snd7_rd32(wave_param + 8);
    c->data = data;
    snd7_channel_start(c, length);
    return 1;
}

/* 0x037ff79c: PSG, channels 8-13 only. */
int snd7_channel_start_psg(Snd7Channel *c, int duty, int32_t length)
{
    if (c->number < 8 || c->number > 13) {
        return 0;
    }
    c->type = 1;
    c->data = (uint32_t)duty;
    c->base_timer = 0x1f46;
    snd7_channel_start(c, length);
    return 1;
}

/* 0x037ff7e8: noise, channels 14 and 15 only. */
int snd7_channel_start_noise(Snd7Channel *c, int32_t length)
{
    if (c->number < 14 || c->number > 15) {
        return 0;
    }
    c->type = 2;
    c->base_timer = 0x1f46;
    snd7_channel_start(c, length);
    return 1;
}

/* 0x037ff82c: the envelope, in tenths of a dB (0 at full). */
int snd7_channel_envelope(Snd7Channel *c, int periodic)
{
    if (periodic) {
        switch (c->envelope_status) {
        case 0: /* attack: toward 0 */
            c->envelope = -((-c->envelope * c->attack) >> 8);
            if (c->envelope == 0) {
                c->envelope_status = 1;
            }
            break;
        case 1: { /* decay: down to the sustain level */
            const int32_t sustain = snd7_segment_s16(SND7_DECIBEL_TABLE + 2u * c->sustain);
            c->envelope -= c->decay;
            if (c->envelope <= sustain * 128) {
                c->envelope = sustain * 128;
                c->envelope_status = 2;
            }
            break;
        }
        case 3: /* release */ c->envelope -= c->release; break;
        default: break;
        }
    }
    return c->envelope >> 7;
}

/* 0x037ff8d4 */
void snd7_channel_set_attack(Snd7Channel *c, int attack)
{
    if (attack < 0x6d) {
        c->attack = (uint8_t)(0xff - attack);
    } else {
        c->attack = snd7_segment_u8(SND7_ATTACK_TABLE + (uint32_t)(0x7f - attack));
    }
}

/* 0x037ff8f4 */
void snd7_channel_set_decay(Snd7Channel *c, int decay)
{
    c->decay = snd7_release_rate(decay);
}

/* 0x037ff910 */
void snd7_channel_set_sustain(Snd7Channel *c, int sustain)
{
    c->sustain = (uint8_t)sustain;
}

/* 0x037ff918 */
void snd7_channel_set_release(Snd7Channel *c, int release)
{
    c->release = snd7_release_rate(release);
}

/* 0x037ff934 */
void snd7_channel_release(Snd7Channel *c)
{
    c->envelope_status = 3;
}

/* 0x037ff940 */
int snd7_channel_is_active(const Snd7Channel *c)
{
    return c->active;
}

static Snd7Channel *take(Snd7Channel *best, int priority, Snd7ChannelCallback callback, void *data);

/* 0x037ff950: a channel of `mask` for a note of `priority`: of those not
 * locked (the weakly locked too, for a strong request), in the driver's
 * order, the one of lowest priority, then of lowest level; none if it
 * outranks the note. Its owner is told, and it starts over as a note's. */
Snd7Channel *snd7_channel_alloc(uint32_t mask, int priority, int strong, Snd7ChannelCallback callback, void *data)
{
    Snd7Channel *best = NULL;
    mask &= ~lock_mask;
    if (!strong) {
        mask &= ~weak_lock_mask;
    }
    for (int i = 0; i < 16; ++i) {
        const int n = snd7_segment_u8(SND7_CHANNEL_ORDER + (uint32_t)i);
        Snd7Channel *c = &snd7_channel[n];
        if (!(mask & (1u << n))) {
            continue;
        }
        if (best == NULL) {
            best = c;
            continue;
        }
        if (c->priority > best->priority) {
            continue;
        }
        if (c->priority == best->priority && level_of(best->volume) <= level_of(c->volume)) {
            continue;
        }
        best = c;
    }
    if (best == NULL || priority < best->priority) {
        return NULL;
    }
    return take(best, priority, callback, data);
}

/* The port's voice for a wave's note: a free one, else a new one with the hardware's own; NULL without the
 * memory. */
static Snd7Channel *free_voice(void)
{
    Snd7Channel *v;
    int n;
    for (int i = 0; i < voice_count; ++i) {
        if (!voices[i]->active && voices[i]->sync == 0) {
            return voices[i];
        }
    }
    if (voice_count == voice_room) {
        const int room = voice_room > 0 ? voice_room * 2 : 16;
        Snd7Channel **grown = (Snd7Channel **)realloc(voices, sizeof(*grown) * (size_t)room);
        if (grown == NULL) {
            return NULL;
        }
        voices = grown;
        voice_room = room;
    }
    v = (Snd7Channel *)calloc(1, sizeof(*v));
    if (v == NULL) {
        return NULL;
    }
    n = khdays_sound_hw_add_voice(snd7_channels);
    if (n != 16 + voice_count) {
        free(v); /* (the hardware's and the driver's voices are made together, one for one) */
        return NULL;
    }
    v->number = (uint16_t)n;
    voices[voice_count++] = v;
    if (snd7_channels->trace) {
        fprintf(stderr, "sound: voice %d made: a note found its channels sounding\n", n);
    }
    return v;
}

Snd7Channel *snd7_channel_alloc_wave(uint32_t mask, int priority, int strong, Snd7ChannelCallback callback, void *data)
{
    if (snd7_channels->allow_voices) {
        uint32_t open = mask & ~lock_mask & (strong ? ~0u : ~weak_lock_mask);
        int free_one = 0;
        for (int n = 0; n < 16 && !free_one; ++n) {
            free_one = (open >> n & 1) && !snd7_channel[n].active && snd7_channel[n].sync == 0;
        }
        if (!free_one) {
            Snd7Channel *v = free_voice();
            if (v != NULL) {
                return take(v, priority, callback, data);
            }
        }
    }
    return snd7_channel_alloc(mask, priority, strong, callback, data);
}

/* The channel starts over as a note's, its owner told it is dropped. */
static Snd7Channel *take(Snd7Channel *best, int priority, Snd7ChannelCallback callback, void *data)
{
    if (best->callback != NULL) {
        best->callback(best, SND7_CHANNEL_DROPPED, best->callback_data);
    }
    best->sync = SND7_SYNC_STOP;
    best->active = 0;
    best->next = NULL;
    best->callback = callback;
    best->callback_data = data;
    best->length = 0;
    best->priority = (uint8_t)priority;
    best->volume = 0x7f;
    best->start = 0;
    best->auto_sweep = 1;
    best->key = 60;
    best->original_key = 60;
    best->velocity = 127;
    best->init_pan = 0;
    best->user_decay = 0;
    best->user_decay2 = 0;
    best->user_pitch = 0;
    best->user_pan = 0;
    best->pan_range = 127;
    best->sweep_pitch = 0;
    best->sweep_length = 0;
    best->sweep_counter = 0;
    best->attack = 0;
    best->decay = 0xffff;
    best->sustain = 127;
    best->release = 0xffff;
    snd7_lfo_init(&best->lfo.param);
    return best;
}

/* 0x037ffb1c */
void snd7_channel_forget_callback(Snd7Channel *c)
{
    if (c != NULL) {
        c->callback = NULL;
        c->callback_data = NULL;
    }
}

/* The channel stopped and freed: its owner told, its registers stopped. */
static void drop(int n)
{
    Snd7Channel *c = &snd7_channel[n];
    if (c->callback != NULL) {
        c->callback(c, SND7_CHANNEL_DROPPED, c->callback_data);
    }
    snd7_hw_stop(n, 0);
    c->priority = 0;
    snd7_channel_forget_callback(c);
    c->sync = 0;
    c->active = 0;
}

/* 0x037ffb30: the channels of `mask` not locked. */
void snd7_channel_free(uint32_t mask)
{
    for (int n = 0; n < 16 && mask != 0; ++n, mask >>= 1) {
        if ((mask & 1) && !(lock_mask & (1u << n))) {
            drop(n);
        }
    }
}

/* 0x037ffbe0: the channels of `mask` stopped (those not locked already)
 * and locked -- weakly with flags bit 0. */
void snd7_channel_lock(uint32_t mask, int flags)
{
    uint32_t left = mask;
    for (int n = 0; n < 16 && left != 0; ++n, left >>= 1) {
        if ((left & 1) && !(lock_mask & (1u << n))) {
            drop(n);
        }
    }
    if (flags & 1) {
        weak_lock_mask |= mask;
    } else {
        lock_mask |= mask;
    }
}

/* 0x037ffcc0 */
void snd7_channel_unlock(uint32_t mask, int flags)
{
    if (flags & 1) {
        weak_lock_mask &= ~mask;
    } else {
        lock_mask &= ~mask;
    }
}

/* 0x037ffcf8 */
uint32_t snd7_channel_locked(int flags)
{
    return (flags & 1) ? weak_lock_mask : lock_mask;
}

/* 0x037ffd14: the waves in [start, end] stop playing (their data is
 * going away). */
void snd7_channel_invalidate_wave(uint32_t start, uint32_t end)
{
    for (int n = 0; n < snd7_channel_count(); ++n) {
        Snd7Channel *c = snd7_channel_at(n);
        if (c->active && c->type == 0 && start <= c->data && c->data <= end) {
            c->start = 0;
            snd7_hw_stop(n, 0);
        }
    }
}

/* 0x037ffd90 */
void snd7_lfo_init(Snd7LfoParam *param)
{
    param->target = 0;
    param->depth = 0;
    param->range = 1;
    param->speed = 16;
    param->delay = 0;
}

/* 0x037ffdb4: past its delay, the LFO's phase moves on by its speed (its
 * upper byte a 0-127 position, the lower byte the fraction). */
void snd7_lfo_update(Snd7Lfo *lfo)
{
    uint32_t position;
    if (lfo->delay_count < lfo->param.delay) {
        ++lfo->delay_count;
        return;
    }
    position = (uint32_t)(lfo->counter + (lfo->param.speed << 6)) >> 8;
    while (position >= 0x80) {
        position -= 0x80;
    }
    lfo->counter = (uint16_t)(lfo->counter + (lfo->param.speed << 6));
    lfo->counter &= 0xff;
    lfo->counter |= (uint16_t)(position << 8);
}

/* 0x037ffe14 */
int32_t snd7_lfo_value(const Snd7Lfo *lfo)
{
    if (lfo->param.depth == 0 || lfo->delay_count < lfo->param.delay) {
        return 0;
    }
    return lfo->param.range * (lfo->param.depth * snd7_hw_sin(lfo->counter >> 8));
}

/* 0x037ffe64: a 0-127 decay or release as the envelope's step. */
uint16_t snd7_release_rate(int value)
{
    if (value == 0x7f) {
        return 0xffff;
    }
    if (value == 0x7e) {
        return 0x3c00;
    }
    if (value < 0x32) {
        return (uint16_t)((value << 1) + 1);
    }
    return (uint16_t)(0x1e00 / (0x7e - value));
}

/* 0x037ffeb8: the note starts: the envelope at its bottom, in attack, the
 * LFO from its start, the start asked of the registers. */
void snd7_channel_start(Snd7Channel *c, int32_t length)
{
    c->envelope = (int32_t)0xfffe9680;
    c->envelope_status = 0;
    c->length = length;
    c->lfo.counter = 0;
    c->lfo.delay_count = 0;
    c->start = 1;
    c->active = 1;
}

/* 0x037ff0fc: SNDCAPxCNT and its destination and length. */
void snd7_capture_setup(int unit, int format, uint32_t destination, int length, int loop, int add, int source)
{
    const uint32_t control =
        (uint32_t)source | ((uint32_t)(loop == 0) << 2 | (uint32_t)format << 3 | (uint32_t)add << 1);
    snd7_reg_write(0x108 + (uint32_t)unit, 1, (uint8_t)control);
    snd7_reg_write(0x110 + (uint32_t)unit * 8, 4, destination);
    snd7_reg_write(0x114 + (uint32_t)unit * 8, 2, (uint16_t)length);
}

/* 0x037ff14c */
int snd7_capture_is_active(int unit)
{
    return (snd7_reg_read(0x108 + (uint32_t)unit, 1) & 0x80) != 0;
}
