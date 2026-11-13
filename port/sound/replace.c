/* A replaced sound (replace.h). */
#include "replace.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> /* MultiByteToWideChar: open_utf8 */
#endif

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define DR_FLAC_NO_WCHAR
#include "dr_flac.h"

static void say(char *why, int size, const char *text)
{
    snprintf(why, (size_t)size, "%s", text);
}

/* --- Loops ----------------------------------------------------------------------------- */

/* A Vorbis comment KEY=value, KEY in any case: the value, or -1. */
static int64_t comment_value(const char *comment, size_t length, const char *key)
{
    const size_t n = strlen(key);
    char digits[24];
    size_t d = 0;
    if (length <= n || comment[n] != '=') {
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        if (toupper((unsigned char)comment[i]) != key[i]) {
            return -1;
        }
    }
    for (size_t i = n + 1; i < length && d + 1 < sizeof(digits); ++i) {
        if (!isdigit((unsigned char)comment[i])) {
            return -1;
        }
        digits[d++] = comment[i];
    }
    digits[d] = '\0';
    return d == 0 ? -1 : (int64_t)strtoll(digits, NULL, 10);
}

typedef struct Comments {
    int64_t start, length;
} Comments;

static void take_comment(Comments *c, const char *comment, size_t length)
{
    const int64_t start = comment_value(comment, length, "LOOPSTART");
    const int64_t loop_length = comment_value(comment, length, "LOOPLENGTH");
    if (start >= 0) {
        c->start = start;
    }
    if (loop_length >= 0) {
        c->length = loop_length;
    }
}

/* The loop: the file's own, else the original's scaled to the file's rate (the whole file for
 * KHDAYS_WHOLE_FILE_LOOP). */
static void set_loop(KhdaysReplacement *r, int64_t start, int64_t end, const KhdaysOriginalLoop *original)
{
    if (start >= 0 && end > start && (uint64_t)start < r->frames) {
        r->looped = 1;
        r->loop_start = (uint64_t)start;
        r->loop_end = (uint64_t)end < r->frames ? (uint64_t)end : r->frames;
    } else if (original != NULL && original->looped && original->rate == 0) {
        r->looped = r->frames > 0;
        r->loop_start = 0;
        r->loop_end = r->frames;
    } else if (original != NULL && original->looped && original->rate != 0 && original->end > original->start) {
        const double scale = (double)r->rate / (double)original->rate;
        r->looped = 1;
        r->loop_start = (uint64_t)((double)original->start * scale + 0.5);
        r->loop_end = (uint64_t)((double)original->end * scale + 0.5);
        if (r->loop_end > r->frames) {
            r->loop_end = r->frames;
        }
        if (r->loop_start >= r->loop_end) {
            r->looped = 0;
        }
    } else {
        r->looped = 0;
    }
}

/* --- WAV ----------------------------------------------------------------------------- */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* Opens a file by its UTF-8 path; on Windows through the wide path, so names like "Días" work. */
static FILE *open_utf8(const char *path, const char *mode)
{
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = n > 0 ? malloc((size_t)n * sizeof(wchar_t)) : NULL;
    wchar_t wide_mode[8];
    FILE *f = NULL;
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n) == n &&
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wide_mode, 8) > 0) {
        f = _wfopen(wide, wide_mode);
    }
    free(wide);
    return f;
#else
    return fopen(path, mode);
#endif
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = open_utf8(path, "rb");
    uint8_t *data;
    long length;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = length > 0 ? malloc((size_t)length) : NULL;
    if (data != NULL && fread(data, 1, (size_t)length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = data != NULL ? (size_t)length : 0;
    return data;
}

/* RIFF WAVE: PCM of 8, 16, 24 or 32 bits, or 32-bit float (plain or
 * WAVE_FORMAT_EXTENSIBLE); the first loop of `smpl` (its end the loop's
 * last sample). */
static int load_wav(KhdaysReplacement *r, const char *path, const KhdaysOriginalLoop *original, char *why, int why_size)
{
    size_t size;
    uint8_t *data = read_file(path, &size);
    const uint8_t *fmt = NULL, *samples = NULL;
    uint32_t samples_size = 0;
    int64_t loop_start = -1, loop_end = -1;
    int format, bits;
    if (data == NULL) {
        say(why, why_size, "cannot be read");
        return 0;
    }
    if (size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        free(data);
        say(why, why_size, "is not a RIFF WAVE file");
        return 0;
    }
    for (size_t at = 12; at + 8 <= size;) {
        const uint32_t length = le32(data + at + 4);
        const uint8_t *body = data + at + 8;
        if (length > size - at - 8) {
            break;
        }
        if (memcmp(data + at, "fmt ", 4) == 0 && length >= 16) {
            fmt = body;
        } else if (memcmp(data + at, "data", 4) == 0) {
            samples = body;
            samples_size = length;
        } else if (memcmp(data + at, "smpl", 4) == 0 && length >= 36 + 24 && le32(body + 28) >= 1) {
            loop_start = le32(body + 36 + 8);
            loop_end = (int64_t)le32(body + 36 + 12) + 1;
        }
        at += 8 + length + (length & 1);
    }
    if (fmt == NULL || samples == NULL) {
        free(data);
        say(why, why_size, "has no fmt or data chunk");
        return 0;
    }
    format = le16(fmt);
    r->channels = le16(fmt + 2);
    r->rate = le32(fmt + 4);
    bits = le16(fmt + 14);
    if (format == 0xfffe && le16(fmt + 16) >= 22) {
        format = le16(fmt + 24); /* the subformat GUID's first word */
    }
    if ((format != 1 && format != 3) || (format == 3 && bits != 32) ||
        (format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32)) {
        free(data);
        say(why, why_size, "is neither PCM of 8, 16, 24 or 32 bits nor 32-bit float");
        return 0;
    }
    if (r->channels < 1 || r->channels > 2 || r->rate == 0) {
        free(data);
        say(why, why_size, "has other than 1 or 2 channels, or no rate");
        return 0;
    }
    r->frames = samples_size / (uint32_t)(bits / 8 * r->channels);
    r->samples = malloc((size_t)r->frames * (size_t)r->channels * sizeof(int16_t));
    if (r->samples == NULL) {
        free(data);
        say(why, why_size, "is too large");
        return 0;
    }
    /* to 16 bits: 16-bit PCM as it is (a dumped original comes back the
     * same), the others by their upper bits, floats scaled and clipped */
    for (uint64_t i = 0; i < r->frames * (uint64_t)r->channels; ++i) {
        const uint8_t *p = samples + i * (uint64_t)(bits / 8);
        if (format == 3) {
            float v;
            memcpy(&v, p, 4);
            v = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
            r->samples[i] = (int16_t)(v >= 32767.0f / 32768.0f ? 32767 : lrintf(v * 32768.0f));
        } else if (bits == 8) {
            r->samples[i] = (int16_t)(((int)p[0] - 128) * 256);
        } else if (bits == 16) {
            r->samples[i] = (int16_t)le16(p);
        } else if (bits == 24) {
            r->samples[i] = (int16_t)le16(p + 1);
        } else {
            r->samples[i] = (int16_t)le16(p + 2);
        }
    }
    free(data);
    set_loop(r, loop_start, loop_end, original);
    return 1;
}

/* --- FLAC ---------------------------------------------------------------------------- */

static void flac_metadata(void *user, drflac_metadata *metadata)
{
    if (metadata->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
        drflac_vorbis_comment_iterator it;
        const char *comment;
        drflac_uint32 length;
        drflac_init_vorbis_comment_iterator(&it, metadata->data.vorbis_comment.commentCount,
                                            metadata->data.vorbis_comment.pComments);
        while ((comment = drflac_next_vorbis_comment(&it, &length)) != NULL) {
            take_comment(user, comment, length);
        }
    }
}

static int load_flac(KhdaysReplacement *r, const char *path, const KhdaysOriginalLoop *original, char *why,
                     int why_size)
{
    Comments comments = {-1, -1};
    size_t size;
    uint8_t *data = read_file(path, &size); /* read here: dr_flac's own opening is not UTF-8 on Windows */
    drflac *flac = data != NULL ? drflac_open_memory_with_metadata(data, size, flac_metadata, &comments, NULL) : NULL;
    if (flac == NULL) {
        say(why, why_size, data == NULL ? "cannot be read" : "is not a FLAC file dr_flac can read");
        free(data);
        return 0;
    }
    r->rate = flac->sampleRate;
    r->channels = flac->channels;
    r->frames = flac->totalPCMFrameCount;
    if (r->channels < 1 || r->channels > 2 || r->frames == 0) {
        drflac_close(flac);
        free(data);
        say(why, why_size, "has other than 1 or 2 channels, or no length");
        return 0;
    }
    r->samples = malloc((size_t)r->frames * (size_t)r->channels * sizeof(int16_t));
    if (r->samples == NULL) {
        drflac_close(flac);
        free(data);
        say(why, why_size, "is too large");
        return 0;
    }
    r->frames = drflac_read_pcm_frames_s16(flac, r->frames, r->samples);
    drflac_close(flac);
    free(data);
    set_loop(r, comments.start, comments.start >= 0 && comments.length > 0 ? comments.start + comments.length : -1,
             original);
    return 1;
}

/* --- Ogg Vorbis ---------------------------------------------------------------------- */

static int load_ogg(KhdaysReplacement *r, const char *path, const KhdaysOriginalLoop *original, char *why, int why_size)
{
    int error = 0;
    size_t size;
    uint8_t *data = read_file(path, &size); /* read here: stb_vorbis's own opening is not UTF-8 on Windows */
    stb_vorbis *ogg = data != NULL && size <= INT_MAX ? stb_vorbis_open_memory(data, (int)size, &error, NULL) : NULL;
    stb_vorbis_info info;
    stb_vorbis_comment list;
    Comments comments = {-1, -1};
    uint64_t made = 0;
    if (ogg == NULL) {
        say(why, why_size, data == NULL ? "cannot be read" : "is not an Ogg Vorbis file stb_vorbis can read");
        free(data);
        return 0;
    }
    info = stb_vorbis_get_info(ogg);
    list = stb_vorbis_get_comment(ogg);
    for (int i = 0; i < list.comment_list_length; ++i) {
        take_comment(&comments, list.comment_list[i], strlen(list.comment_list[i]));
    }
    r->rate = info.sample_rate;
    r->channels = info.channels;
    r->frames = stb_vorbis_stream_length_in_samples(ogg);
    if (r->channels < 1 || r->channels > 2 || r->frames == 0) {
        stb_vorbis_close(ogg);
        free(data);
        say(why, why_size, "has other than 1 or 2 channels, or no length");
        return 0;
    }
    r->samples = malloc((size_t)r->frames * (size_t)r->channels * sizeof(int16_t));
    if (r->samples == NULL) {
        stb_vorbis_close(ogg);
        free(data);
        say(why, why_size, "is too large");
        return 0;
    }
    while (made < r->frames) {
        const int got = stb_vorbis_get_samples_short_interleaved(ogg, r->channels, r->samples + made * r->channels,
                                                                 (int)((r->frames - made) * r->channels));
        if (got <= 0) {
            break;
        }
        made += (uint64_t)got;
    }
    stb_vorbis_close(ogg);
    free(data);
    r->frames = made;
    set_loop(r, comments.start, comments.start >= 0 && comments.length > 0 ? comments.start + comments.length : -1,
             original);
    return 1;
}

/* --- The replacement ----------------------------------------------------------------- */

/* A file above the PC's usual output rate is brought down to it as it loads, through a sinc of this many zero
 * crossings each side under a Blackman window. */
#define TOP_RATE 48000u
#define SINC_ZEROS 16
#define SINC_PI 3.14159265358979323846

static double blackman(double t) /* t in [-1, 1] */
{
    return 0.42 + 0.5 * cos(SINC_PI * t) + 0.08 * cos(2.0 * SINC_PI * t);
}

/* Channel `c` of the file at `x` of its samples through the filter, `half` of them each side. */
static double filtered(const KhdaysReplacement *r, double x, int64_t half, double ratio, int c)
{
    const int64_t center = (int64_t)floor(x);
    double sum = 0.0, weights = 0.0;
    for (int64_t k = center - half + 1; k <= center + half; ++k) {
        if (k < 0 || (uint64_t)k >= r->frames) {
            continue;
        }
        const double d = x - (double)k, u = d / ratio;
        const double h = (u == 0.0 ? 1.0 : sin(SINC_PI * u) / (SINC_PI * u)) * blackman(d / (double)half);
        sum += (double)r->samples[(uint64_t)k * (uint64_t)r->channels + (uint64_t)c] * h;
        weights += h;
    }
    return weights != 0.0 ? sum / weights : 0.0;
}

static int16_t pcm16(double v)
{
    return (int16_t)(v > 32767.0 ? 32767 : v < -32768.0 ? -32768 : lrint(v));
}

/* A file above TOP_RATE brought down to it, so that what it holds above half that rate does not fold back as
 * noise, its loop moved with it. 0 without the memory. */
static int bring_down(KhdaysReplacement *r)
{
    const double ratio = (double)r->rate / TOP_RATE;
    const uint64_t frames = (uint64_t)((double)r->frames / ratio);
    const int64_t half = (int64_t)ceil(SINC_ZEROS * ratio); /* the filter's reach each side, in the file's samples */
    const int channels = r->channels;
    int16_t *out = frames > 0 ? malloc((size_t)(frames * (uint64_t)channels * sizeof(*out))) : NULL;
    if (out == NULL) {
        return frames == 0;
    }
    for (uint64_t j = 0; j < frames; ++j) {
        for (int c = 0; c < channels; ++c) {
            out[j * (uint64_t)channels + (uint64_t)c] = pcm16(filtered(r, (double)j * ratio, half, ratio, c));
        }
    }
    free(r->samples);
    r->samples = out;
    r->frames = frames;
    r->loop_start = (uint64_t)((double)r->loop_start / ratio + 0.5);
    r->loop_end = (uint64_t)((double)r->loop_end / ratio + 0.5);
    if (r->loop_end > frames) {
        r->loop_end = frames;
    }
    if (r->looped && r->loop_start >= r->loop_end) {
        r->looped = 0;
    }
    r->rate = TOP_RATE;
    return 1;
}

int khdays_replacement_load(KhdaysReplacement *replacement, const char *path, const KhdaysOriginalLoop *original,
                            char *why, int why_size)
{
    const char *dot = strrchr(path, '.');
    int loaded;
    memset(replacement, 0, sizeof(*replacement));
    if (dot != NULL && strcmp(dot, ".wav") == 0) {
        loaded = load_wav(replacement, path, original, why, why_size);
    } else if (dot != NULL && strcmp(dot, ".flac") == 0) {
        loaded = load_flac(replacement, path, original, why, why_size);
    } else if (dot != NULL && strcmp(dot, ".ogg") == 0) {
        loaded = load_ogg(replacement, path, original, why, why_size);
    } else {
        say(why, why_size, "is not .wav, .flac or .ogg");
        return 0;
    }
    if (loaded && replacement->rate > TOP_RATE && !bring_down(replacement)) {
        khdays_replacement_free(replacement);
        say(why, why_size, "has no memory to be brought down to the PC's rate");
        return 0;
    }
    return loaded;
}

void khdays_replacement_free(KhdaysReplacement *replacement)
{
    free(replacement->samples);
    memset(replacement, 0, sizeof(*replacement));
}

/* Frame `i` of the file as it plays: through the loop, or 0 past its end. */
static float frame_at(const KhdaysReplacement *r, int64_t i, int channel)
{
    if (i < 0) {
        i = 0;
    }
    if (r->looped && (uint64_t)i >= r->loop_end) {
        i = (int64_t)(r->loop_start + ((uint64_t)i - r->loop_start) % (r->loop_end - r->loop_start));
    } else if ((uint64_t)i >= r->frames) {
        return 0.0f;
    }
    return (float)r->samples[(uint64_t)i * (uint64_t)r->channels + (uint64_t)(channel < r->channels ? channel : 0)] /
           32768.0f;
}

float khdays_replacement_sample(const KhdaysReplacement *replacement, double position, int channel)
{
    const KhdaysReplacement *r = replacement;
    double base = floor(position);
    const float t = (float)(position - base);
    int64_t i;
    float a, b, c, d;
    if (r->samples == NULL || position < 0.0) {
        return 0.0f;
    }
    if (r->looped && base >= (double)r->loop_end) {
        const double span = (double)(r->loop_end - r->loop_start);
        base = (double)r->loop_start + fmod(base - (double)r->loop_start, span);
    } else if (!r->looped && base >= (double)r->frames) {
        return 0.0f;
    }
    i = (int64_t)base;
    a = frame_at(r, i - 1, channel);
    b = frame_at(r, i, channel);
    c = frame_at(r, i + 1, channel);
    d = frame_at(r, i + 2, channel);
    /* Catmull-Rom */
    return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
}

/* --- Writing ------------------------------------------------------------------------- */

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

int khdays_replacement_write_wav(const char *path, uint32_t rate, int channels, const int16_t *samples, uint64_t frames,
                                 int looped, uint64_t loop_start, uint64_t loop_end)
{
    const uint64_t data_size = frames * (uint64_t)channels * 2;
    const uint32_t smpl_size = looped ? 36 + 24 : 0;
    uint8_t header[12 + 8 + 16 + 8 + 36 + 24 + 8];
    uint8_t *h = header;
    FILE *f;
    int ok;
    if (channels < 1 || channels > 2 || rate == 0 || data_size > 0xffffffffull - sizeof(header) ||
        (looped && (loop_end <= loop_start || loop_end > frames || loop_end > 0xffffffffull))) {
        return 0;
    }
    memcpy(h, "RIFF", 4);
    put32(h + 4, (uint32_t)(4 + 8 + 16 + (smpl_size != 0 ? 8 + smpl_size : 0) + 8 + data_size));
    memcpy(h + 8, "WAVE", 4);
    h += 12;
    /* fmt: PCM, 16 bits */
    memcpy(h, "fmt ", 4);
    put32(h + 4, 16);
    put16(h + 8, 1);
    put16(h + 10, (uint32_t)channels);
    put32(h + 12, rate);
    put32(h + 16, rate * (uint32_t)channels * 2);
    put16(h + 20, (uint32_t)channels * 2);
    put16(h + 22, 16);
    h += 24;
    if (smpl_size != 0) {
        /* smpl: the sample period in nanoseconds, MIDI unity note 60, one
         * forward loop played forever, its end its last sample */
        memset(h, 0, 8 + smpl_size);
        memcpy(h, "smpl", 4);
        put32(h + 4, smpl_size);
        put32(h + 8 + 8, (uint32_t)(1000000000.0 / rate + 0.5));
        put32(h + 8 + 12, 60);
        put32(h + 8 + 28, 1);
        put32(h + 8 + 36 + 8, (uint32_t)loop_start);
        put32(h + 8 + 36 + 12, (uint32_t)(loop_end - 1));
        h += 8 + smpl_size;
    }
    memcpy(h, "data", 4);
    put32(h + 4, (uint32_t)data_size);
    h += 8;
    f = open_utf8(path, "wb");
    if (f == NULL) {
        return 0;
    }
    ok = fwrite(header, 1, (size_t)(h - header), f) == (size_t)(h - header);
    /* little-endian, as the machines the port runs on */
    ok = ok && fwrite(samples, 2, (size_t)(frames * (uint64_t)channels), f) == (size_t)(frames * (uint64_t)channels);
    ok = fclose(f) == 0 && ok;
    return ok;
}
