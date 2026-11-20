/* The music streams, read by the port itself (stream_reader.h). The steps
 * are NNSi_SndArcStrm_MakeWaveData's (0x0201d6dc, libs/nns/snd/calls in
 * khdays-decomp), each on this stream's own copy of the player's state. */
#include "stream_reader.h"
#include "../runtime/core/bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FORMAT_PCM8, FORMAT_PCM16, FORMAT_ADPCM }; /* the file's (StrmFormat) */

/* The samples made and not yet played past, per channel: a window of the
 * sequence the ring would hold, as the ring holds it (PCM8 bytes, else
 * PCM16) -- seconds of it, for KHDAYS_STREAM_CHECK to find how far behind
 * a starved player's ring is. */
#define WINDOW (1u << 18)
/* How much each step makes: a quarter of the ring, as each load does. */
#define STEP_BYTES 512u
/* A handle: the player (2 bits), the channel (4), the prepare it is from
 * (24) -- below 1 << 30, which the waves' handles have (runtime/sound/sound_mods.h). */
#define GENERATION_MASK 0xffffffu

typedef struct AdpcmState {
    int16_t sample; /* prevSample */
    uint8_t index;  /* prevIndex */
} AdpcmState;

typedef struct Stream {
    int active;
    unsigned generation; /* one more at each prepare: a channel bound before is not this stream's */
    KhdaysStreamPrepared prepared;
    KhdaysStreamFile file;
    /* The file's header (NNSSndStrmData, offsets from the file's start). */
    int format;                  /* +0x18 */
    int loop;                    /* +0x19 */
    int file_channels;           /* +0x1a */
    uint32_t loop_start;         /* +0x20 */
    uint32_t data_offset;        /* +0x28 */
    uint32_t blocks;             /* +0x2c */
    uint32_t block_size;         /* +0x30: bytes, each channel's part of a block */
    uint32_t block_samples;      /* +0x34 */
    uint32_t last_block_size;    /* +0x38 */
    uint32_t last_block_samples; /* +0x3c */
    /* The player's state as MakeWaveData keeps it. */
    uint32_t cur_sample;
    int dirty;
    int finished;
    AdpcmState adpcm[KHDAYS_STREAM_CHANNELS];
    /* What it has made. */
    uint32_t unit; /* bytes a sample in the ring: 1 or 2 */
    uint64_t made; /* samples of the sequence made so far */
    uint8_t *window[KHDAYS_STREAM_CHANNELS];
    uint8_t *step[KHDAYS_STREAM_CHANNELS]; /* one step's output */
    uint8_t *read_buffer;                  /* sDecodeBuffer's part: an ADPCM read */
    uint32_t read_buffer_size;
    uint8_t *discard; /* the samples a dirty start decodes and drops */
    uint32_t discard_size;
} Stream;

struct KhdaysStreams {
    Stream stream[KHDAYS_STREAM_PLAYERS];
    int8_t index_table[16];
    int16_t step_table[89];
};

static void *grow(uint8_t **buffer, uint32_t *size, uint32_t need)
{
    if (need > *size) {
        uint8_t *bigger = realloc(*buffer, need);
        if (bigger == NULL) {
            fprintf(stderr, "sound: out of memory reading a stream\n");
            exit(15);
        }
        *buffer = bigger;
        *size = need;
    }
    return *buffer;
}

KhdaysStreams *khdays_streams_new(const int8_t *index_table, const int16_t *step_table)
{
    KhdaysStreams *streams = calloc(1, sizeof(*streams));
    if (streams == NULL) {
        return NULL;
    }
    memcpy(streams->index_table, index_table, sizeof(streams->index_table));
    memcpy(streams->step_table, step_table, sizeof(streams->step_table));
    return streams;
}

/* PrepareStrm's own steps for the state (the first sample, the dirty start
 * of IMA-ADPCM inside a block), with the header's fields. */
void khdays_streams_prepared(KhdaysStreams *streams, const KhdaysStreamPrepared *prepared, KhdaysStreamFile file)
{
    Stream *s;
    const uint8_t *h = prepared->header;
    if (prepared->player < 0 || prepared->player >= KHDAYS_STREAM_PLAYERS || prepared->channels < 1 ||
        prepared->channels > KHDAYS_STREAM_CHANNELS) {
        return;
    }
    s = &streams->stream[prepared->player];
    s->active = 1;
    ++s->generation;
    s->prepared = *prepared;
    s->file = file;
    s->format = h[0x18];
    s->loop = h[0x19];
    s->file_channels = h[0x1a];
    s->loop_start = le32(h + 0x20);
    s->data_offset = le32(h + 0x28);
    s->blocks = le32(h + 0x2c);
    s->block_size = le32(h + 0x30);
    s->block_samples = le32(h + 0x34);
    s->last_block_size = le32(h + 0x38);
    s->last_block_samples = le32(h + 0x3c);
    s->cur_sample = prepared->start_sample;
    s->dirty = s->cur_sample != 0 && s->format == FORMAT_ADPCM;
    s->finished = s->blocks == 0 || s->block_samples == 0;
    memset(s->adpcm, 0, sizeof(s->adpcm));
    s->unit = s->format == FORMAT_PCM8 ? 1 : 2;
    s->made = 0;
    for (int ch = 0; ch < prepared->channels; ++ch) {
        if (s->window[ch] == NULL) {
            s->window[ch] = malloc(WINDOW * 2);
            s->step[ch] = malloc(STEP_BYTES);
            if (s->window[ch] == NULL || s->step[ch] == NULL) {
                fprintf(stderr, "sound: out of memory reading a stream\n");
                exit(15);
            }
        }
    }
}

/* DecodeAdpcm (MakeWaveData's): NitroSystem's IMA-ADPCM, its sample kept
 * within 16 bits. */
static int16_t decode_adpcm(const KhdaysStreams *streams, int code, AdpcmState *state)
{
    const int step = streams->step_table[state->index];
    int sample = state->sample, index = state->index;
    int d = step >> 3;
    if (code & 4)
        d += step;
    if (code & 2)
        d += step >> 1;
    if (code & 1)
        d += step >> 2;
    if (code & 8) {
        sample -= d;
        if (sample < -32768)
            sample = -32768;
    } else {
        sample += d;
        if (sample > 32767)
            sample = 32767;
    }
    index += streams->index_table[code];
    if (index < 0) {
        index = 0;
    } else if (index > 88) {
        index = 88;
    }
    state->sample = (int16_t)sample;
    state->index = (uint8_t)index;
    return (int16_t)sample;
}

/* 0x0201d6dc's loop: the next `bytes` of each channel's ring, into its step
 * buffer. */
static void make_step(const KhdaysStreams *streams, Stream *s, uint32_t bytes)
{
    uint32_t dest_offset = 0, rest = bytes;
    const int channels = s->prepared.channels;
    while (rest > 0) {
        uint32_t block, block_size, block_samples, block_offset_sample, samples, size, read_size;
        uint32_t block_offset, offset;
        int loop = 0;
        if (s->finished) {
            for (int ch = 0; ch < channels; ++ch) {
                memset(s->step[ch] + dest_offset, 0, rest);
            }
            break;
        }
        block = s->cur_sample / s->block_samples;
        if (block < s->blocks - 1) {
            block_size = s->block_size;
            block_samples = s->block_samples;
        } else {
            block_size = s->last_block_size;
            block_samples = s->last_block_samples;
        }
        block_offset_sample = s->cur_sample - block * s->block_samples;
        samples = rest;
        if (s->format != FORMAT_PCM8) {
            samples >>= 1;
        }
        if (s->dirty) {
            if (block_offset_sample == 0) {
                s->dirty = 0;
            } else {
                samples = block_offset_sample; /* from the block's start, to reach its state */
                block_offset_sample = 0;
            }
        }
        if (block_offset_sample + samples >= block_samples) {
            samples = block_samples - block_offset_sample;
            if (block >= s->blocks - 1) {
                if (s->loop) {
                    loop = 1;
                } else {
                    s->finished = 1;
                }
            }
        }
        block_offset = block_offset_sample;
        size = samples;
        switch (s->format) {
        case FORMAT_PCM8: read_size = size; break;
        case FORMAT_PCM16:
            block_offset <<= 1;
            size <<= 1;
            read_size = size;
            break;
        default: {
            uint32_t end = block_offset_sample + samples;
            block_offset >>= 1;
            end = (end + 1) >> 1;
            read_size = end - block_offset;
            if (block_offset_sample == 0) {
                read_size += 4; /* the block's AdpcmState first */
            } else {
                block_offset += 4;
            }
            size <<= 1;
            break;
        }
        }
        offset = block_offset + block * s->block_size * (uint32_t)s->file_channels + s->data_offset;
        for (int ch = 0; ch < channels; ++ch) {
            /* A dirty start's samples are overwritten in the ring by what
             * follows (and what it writes past the ring is not the ring's). */
            uint8_t *dest = s->dirty ? grow(&s->discard, &s->discard_size, size) : s->step[ch] + dest_offset;
            if (ch < s->file_channels) {
                uint8_t *read_dest =
                    s->format == FORMAT_ADPCM ? grow(&s->read_buffer, &s->read_buffer_size, read_size) : dest;
                if (s->file.read(s->file.context, offset + (uint32_t)ch * block_size, read_dest, read_size) !=
                    read_size) {
                    size = 0;
                    samples = 0;
                    loop = 0;
                    s->finished = 1;
                    break;
                }
                if (s->format == FORMAT_ADPCM) {
                    AdpcmState *state = &s->adpcm[ch];
                    const uint8_t *src = read_dest;
                    int16_t *out = (int16_t *)dest;
                    uint32_t i = block_offset_sample;
                    const uint32_t end = block_offset_sample + samples;
                    if (block_offset_sample == 0) {
                        state->sample = (int16_t)(src[0] | src[1] << 8);
                        state->index = src[2];
                        src += 4;
                    }
                    if (i & 1) {
                        *out++ = decode_adpcm(streams, (*src >> 4) & 0xf, state);
                        ++i;
                        ++src;
                    }
                    while (i < (end & ~1u)) {
                        *out++ = decode_adpcm(streams, *src & 0xf, state);
                        ++i;
                        *out++ = decode_adpcm(streams, (*src >> 4) & 0xf, state);
                        ++i;
                        ++src;
                    }
                    if (i < end) {
                        *out++ = decode_adpcm(streams, *src & 0xf, state);
                        ++i;
                    }
                }
            } else if (s->prepared.mono) {
                memset(dest, 0, size);
            } else if (!s->dirty) {
                memcpy(dest, s->step[0] + dest_offset, size); /* the first channel's, for a stereo player */
            }
        }
        if (s->dirty) {
            s->dirty = 0;
            continue;
        }
        if (loop) {
            s->cur_sample = s->loop_start;
        } else {
            s->cur_sample += samples;
        }
        dest_offset += size;
        rest -= size;
    }
}

static int decode_handle(int handle, int *player, int *index, unsigned *generation)
{
    if (handle < 0) {
        return 0;
    }
    *player = handle & 3;
    *index = (handle >> 2) & 15;
    *generation = (unsigned)handle >> 6;
    return 1;
}

int khdays_streams_bind(void *context, int channel, uint32_t sad, uint32_t len, int format)
{
    KhdaysStreams *streams = context;
    for (int p = 0; p < KHDAYS_STREAM_PLAYERS; ++p) {
        const Stream *s = &streams->stream[p];
        if (!s->active || format != (s->unit == 1 ? 0 : 1) || len * 4 != s->prepared.ring_bytes) {
            continue;
        }
        for (int i = 0; i < s->prepared.channels; ++i) {
            if (s->prepared.channel[i] == channel && s->prepared.ring + (uint32_t)i * s->prepared.ring_bytes == sad) {
                return p | i << 2 | (int)((s->generation & GENERATION_MASK) << 6);
            }
        }
    }
    return -1;
}

int khdays_streams_sample(void *context, int handle, uint64_t index, int32_t *sample)
{
    KhdaysStreams *streams = context;
    int player, channel;
    unsigned generation;
    Stream *s;
    const uint8_t *at;
    if (!decode_handle(handle, &player, &channel, &generation)) {
        return 0;
    }
    s = &streams->stream[player];
    if (!s->active || (s->generation & GENERATION_MASK) != generation) {
        return 0;
    }
    while (index >= s->made) {
        const uint32_t step_samples = STEP_BYTES / s->unit;
        make_step(streams, s, STEP_BYTES);
        for (int ch = 0; ch < s->prepared.channels; ++ch) {
            for (uint32_t i = 0; i < step_samples; ++i) {
                const uint32_t slot = (uint32_t)((s->made + i) % WINDOW);
                memcpy(s->window[ch] + slot * s->unit, s->step[ch] + i * s->unit, s->unit);
            }
        }
        s->made += step_samples;
    }
    if (index + WINDOW < s->made) {
        return 0; /* further back than it keeps: the channel reads its ring */
    }
    at = s->window[channel] + (uint32_t)(index % WINDOW) * s->unit;
    *sample = s->unit == 1 ? (int32_t)(int8_t)at[0] * 256 : (int32_t)(int16_t)(at[0] | at[1] << 8);
    return 1;
}

uint32_t khdays_streams_ring_address(const KhdaysStreams *streams, int handle, uint64_t index, int *unit)
{
    int player, channel;
    unsigned generation;
    const Stream *s;
    uint32_t ring_samples;
    if (!decode_handle(handle, &player, &channel, &generation)) {
        return 0;
    }
    s = &streams->stream[player];
    if (!s->active || (s->generation & GENERATION_MASK) != generation) {
        return 0;
    }
    ring_samples = s->prepared.ring_bytes / s->unit;
    *unit = (int)s->unit;
    return s->prepared.ring + (uint32_t)channel * s->prepared.ring_bytes + (uint32_t)(index % ring_samples) * s->unit;
}

uint32_t khdays_streams_ring_samples(const KhdaysStreams *streams, int handle)
{
    int player, channel;
    unsigned generation;
    const Stream *s;
    if (!decode_handle(handle, &player, &channel, &generation)) {
        return 0;
    }
    s = &streams->stream[player];
    if (!s->active || (s->generation & GENERATION_MASK) != generation) {
        return 0;
    }
    return s->prepared.ring_bytes / s->unit;
}

int khdays_streams_handle_player(int handle)
{
    return handle & 3;
}

int khdays_streams_handle_channel(int handle)
{
    return (handle >> 2) & 15;
}
