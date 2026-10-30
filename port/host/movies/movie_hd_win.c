/* The player's HD video through Media Foundation (movie_hd.h): a thread of its own, two Source Readers
 * reading ahead at their own pace (the picture as NV12, the sound as floats into its SDL stream); the
 * clock is the sound played so far, else the performance counter less the pauses; each refresh takes
 * the newest picture due (docs/PLATFORM.md, "The PC's side"). Apart from it, a voice: files' sound
 * alone over the game, on a thread and a stream of their own. */
#define COBJMACROS
#include "movie_hd.h"

#include "theater_music.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PICTURES_AHEAD 0.15 /* seconds of pictures decoded ahead of the clock */
#define SOUND_AHEAD 0.30    /* seconds of sound queued ahead of the device */

typedef struct Picture {
    double time; /* its presentation time in the file, in seconds */
    long number; /* counted from 0, in decoding order */
    int width, height;
    uint8_t *data;
    size_t size;
} Picture;

static SRWLOCK lock = SRWLOCK_INIT; /* guards the pictures, the status and the clock's bases */
static float gain = 1.0f;           /* the sound's volume (khdays_hd_set_gain) */

static struct {
    HANDLE thread;
    HANDLE opened; /* set once the thread has opened the file, or failed to */
    HANDLE wake;   /* wakes the thread early (room ahead, a pause, the start) */
    volatile LONG stop, started, paused;
    int open_ok;
    int status; /* what khdays_hd_status says (movie_hd.h): 0 playing, 1 ended, -1 failed, 2 none */
    char why[256];
    WCHAR *url; /* the file's full path */
    KhdaysHdInfo info;
    /* the picture */
    IMFSourceReader *video;
    int coded_height;   /* the decoder's rows per plane (MF_MT_FRAME_SIZE) */
    int default_stride; /* MF_MT_DEFAULT_STRIDE, or 0 when the file does not say */
    int video_eos;
    Picture *pictures; /* decoded, oldest first */
    int count, capacity;
    Picture *spare; /* picture buffers kept to be used again */
    int spare_count, spare_capacity;
    long decoded, taken;      /* pictures decoded; the number of the last one taken (-1: none) */
    double first_time;        /* the first picture's time */
    double duration;          /* how long the file says it lasts (MF_PD_DURATION), or 0 when it does not say */
    double from, end;         /* the part played (khdays_hd_open_part): its start, its end (0 the file's) */
    double fade_in, fade_out; /* its fades (khdays_hd_fades), in seconds; 0 none */
    /* the sound */
    IMFSourceReader *audio;
    SDL_AudioStream *sound;
    int sound_eos;
    volatile LONGLONG put_frames; /* sound frames put into the stream so far */
    double sound_start;           /* the first sound sample's time */
    int last_queued;              /* after the sound's end: what was still queued, and since when */
    LONGLONG still_since;
    /* the counter's clock: used without sound, or once the sound has all played */
    volatile LONG counter_clock;
    double counter_base;              /* the video time it starts from */
    LONGLONG counter_start;           /* the counter's value at that time */
    LONGLONG paused_at, paused_ticks; /* when the pause began; the counter ticks spent paused */
    LARGE_INTEGER frequency;
    /* what happened, for the report when it closes */
    long shown, late;
    double worst_offset;
} hd = {.status = 2, .taken = -1};
static double next_fade_in, next_fade_out; /* khdays_hd_fades: the next opening's */

/* The performance counter now. */
static LONGLONG counter(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

/* How much of the video shows and sounds at `t` seconds of the file through its fades: 0 to 1. */
static float fade_level(double t)
{
    const double end = hd.end > 0.0 ? hd.end : hd.duration;
    double level = 1.0;
    if (hd.fade_in > 0.0) {
        level = SDL_min(level, (t - hd.from) / hd.fade_in);
    }
    if (hd.fade_out > 0.0 && end > 0.0) {
        level = SDL_min(level, (end - t) / hd.fade_out);
    }
    return (float)SDL_clamp(level, 0.0, 1.0);
}

/* Keeps the reason it failed, with the system's error code. */
static void say(const char *what, HRESULT hr)
{
    snprintf(hd.why, sizeof(hd.why), "%s (0x%08lx)", what, (unsigned long)hr);
}

/* The video's clock, in seconds of the file's time (see the top of this file). */
static double clock_now(void)
{
    if (!hd.started) {
        return hd.first_time;
    }
    if (!hd.counter_clock && hd.sound != NULL) {
        int queued = SDL_GetAudioStreamQueued(hd.sound);
        const int frame_bytes = hd.info.channels * (int)sizeof(float);
        if (queued < 0) {
            queued = 0;
        }
        return hd.sound_start + (double)(hd.put_frames - queued / frame_bytes) / hd.info.rate;
    }
    {
        const LONGLONG now = hd.paused ? hd.paused_at : counter();
        return hd.counter_base + (double)(now - hd.counter_start - hd.paused_ticks) / (double)hd.frequency.QuadPart;
    }
}

/* Moves `reader` to the part's start (hd.from): it then reads from the key picture before it, and
 * read_picture and read_sound leave out what comes before. */
static void seek_to_part(IMFSourceReader *reader)
{
    PROPVARIANT at;
    if (hd.from <= 0.0) {
        return;
    }
    PropVariantInit(&at);
    at.vt = VT_I8;
    at.hVal.QuadPart = (LONGLONG)(hd.from * 1e7);
    IMFSourceReader_SetCurrentPosition(reader, &GUID_NULL, &at);
    PropVariantClear(&at);
}

/* Opens a Source Reader on the file (with hardware decoding allowed). On
 * failure keeps `what` as the reason and returns NULL. */
static IMFSourceReader *make_reader(const char *what)
{
    IMFAttributes *attributes = NULL;
    IMFSourceReader *reader = NULL;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr)) {
        IMFAttributes_SetUINT32(attributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        hr = MFCreateSourceReaderFromURL(hd.url, attributes, &reader);
        IMFAttributes_Release(attributes);
    }
    if (FAILED(hr)) {
        say(what, hr);
        return NULL;
    }
    return reader;
}

/* Reads the picture's format as it is decoded now: its size, the area shown
 * and the frame rate. */
static int read_video_type(void)
{
    IMFMediaType *type = NULL;
    UINT64 size = 0, rate = 0;
    UINT32 stride = 0, blob = 0;
    MFVideoArea area;
    HRESULT hr = IMFSourceReader_GetCurrentMediaType(hd.video, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
    if (FAILED(hr)) {
        say("the picture's format cannot be read", hr);
        return 0;
    }
    hr = IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &size);
    if (FAILED(hr) || (UINT32)(size >> 32) == 0 || (UINT32)size == 0) {
        IMFMediaType_Release(type);
        say("the picture has no size", hr);
        return 0;
    }
    hd.info.width = (int)(UINT32)(size >> 32);
    hd.info.height = hd.coded_height = (int)(UINT32)size;
    /* the decoder's rows are whole macroblocks (1088 for 1080); the display
     * area says which part is shown */
    if (SUCCEEDED(IMFMediaType_GetBlob(type, &MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8 *)&area, sizeof(area), &blob)) &&
        blob == sizeof(area) && area.Area.cx > 0 && area.Area.cy > 0 && area.Area.cx <= hd.info.width &&
        area.Area.cy <= hd.coded_height) {
        hd.info.width = (int)area.Area.cx;
        hd.info.height = (int)area.Area.cy;
    }
    hd.default_stride = SUCCEEDED(IMFMediaType_GetUINT32(type, &MF_MT_DEFAULT_STRIDE, &stride)) ? (int)stride : 0;
    hd.info.fps = SUCCEEDED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_RATE, &rate)) && (UINT32)rate != 0
                      ? (double)(UINT32)(rate >> 32) / (double)(UINT32)rate
                      : 0.0;
    IMFMediaType_Release(type);
    hd.info.width &= ~1; /* NV12's chroma has one sample per two columns and two rows */
    hd.info.height &= ~1;
    return 1;
}

/* Opens the picture's reader, decoding to NV12, and reads its format and the
 * file's length. Returns 0 (with hd.why) if the picture cannot be played. */
static int open_video(void)
{
    IMFMediaType *type = NULL;
    HRESULT hr;
    hd.video = make_reader("the file cannot be opened");
    if (hd.video == NULL) {
        return 0;
    }
    IMFSourceReader_SetStreamSelection(hd.video, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    hr = IMFSourceReader_SetStreamSelection(hd.video, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (FAILED(hr)) {
        say("the file has no picture", hr);
        return 0;
    }
    hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) {
        IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFVideoFormat_NV12);
        hr = IMFSourceReader_SetCurrentMediaType(hd.video, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, type);
        IMFMediaType_Release(type);
    }
    if (FAILED(hr)) {
        say("the picture cannot be decoded to NV12", hr);
        return 0;
    }
    {
        /* the length the file says it has: a file cut short ends sooner (see read_picture) */
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(hd.video, (DWORD)MF_SOURCE_READER_MEDIASOURCE,
                                                               &MF_PD_DURATION, &value)) &&
            value.vt == VT_UI8) {
            hd.duration = (double)value.uhVal.QuadPart / 1e7;
        }
        PropVariantClear(&value);
    }
    seek_to_part(hd.video);
    return read_video_type();
}

/* Has `reader` read the file's first sound alone, as floats; its channels and rate. Fails, or leaves
 * them 0, when the file has no sound the system can decode. */
static HRESULT read_sound_as_floats(IMFSourceReader *reader, UINT32 *channels, UINT32 *rate)
{
    IMFMediaType *type = NULL;
    HRESULT hr;
    *channels = *rate = 0;
    IMFSourceReader_SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    hr = IMFSourceReader_SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (SUCCEEDED(hr)) {
        hr = MFCreateMediaType(&type);
    }
    if (SUCCEEDED(hr)) {
        IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
        hr = IMFSourceReader_SetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, type);
        IMFMediaType_Release(type);
        type = NULL;
    }
    if (SUCCEEDED(hr)) {
        hr = IMFSourceReader_GetCurrentMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &type);
    }
    if (SUCCEEDED(hr)) {
        IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        IMFMediaType_GetUINT32(type, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
        IMFMediaType_Release(type);
    }
    return hr;
}

/* Opens the sound, if the file has one the system can decode: its own reader
 * and audio stream (paused until the video starts). Returns 0 if there is no
 * sound to play; the video then plays silent. */
static int open_audio(void)
{
    UINT32 channels = 0, rate = 0;
    HRESULT hr;
    hd.audio = make_reader("the file cannot be opened for its sound");
    if (hd.audio == NULL) {
        return 0;
    }
    hr = read_sound_as_floats(hd.audio, &channels, &rate);
    if (FAILED(hr) || channels == 0 || rate == 0) {
        IMFSourceReader_Release(hd.audio);
        hd.audio = NULL;
        return 0;
    }
    {
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, (int)channels, (int)rate};
        hd.sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    }
    if (hd.sound == NULL) {
        fprintf(stderr, "movies: no sound for the HD video: %s\n", SDL_GetError());
        IMFSourceReader_Release(hd.audio);
        hd.audio = NULL;
        return 0;
    }
    SDL_SetAudioStreamGain(hd.sound, gain);
    hd.info.channels = (int)channels;
    hd.info.rate = (int)rate;
    seek_to_part(hd.audio);
    return 1;
}

/* A buffer for a picture of `size` bytes: a spare one of that size, or a new one. The decoder's
 * thread asks while the presenter's gives spares back (khdays_hd_picture_begin, keep_spare): under
 * the lock, or the list is taken apart under both. */
static uint8_t *picture_buffer(size_t size)
{
    AcquireSRWLockExclusive(&lock);
    while (hd.spare_count > 0) {
        Picture p = hd.spare[--hd.spare_count];
        if (p.size == size) {
            ReleaseSRWLockExclusive(&lock);
            return p.data;
        }
#pragma warning(suppress : 6001) /* every item below the count was written before the count moved: none uninitialised */
        free(p.data);
    }
    ReleaseSRWLockExclusive(&lock);
    return (uint8_t *)malloc(size);
}

/* Keeps a picture's buffer to be used again (freed if there is no room). */
static void keep_spare(Picture p)
{
    if (hd.spare_count == hd.spare_capacity) {
        const int capacity = hd.spare_capacity ? hd.spare_capacity * 2 : 8;
        Picture *grown = (Picture *)realloc(hd.spare, (size_t)capacity * sizeof(Picture));
        if (grown == NULL) {
            free(p.data);
            return;
        }
        hd.spare = grown;
        hd.spare_capacity = capacity;
    }
    hd.spare[hd.spare_count++] = p;
}

/* Adds a decoded picture (`size` bytes at `data`) to the list. */
static int add_picture(uint8_t *data, size_t size, double time)
{
    AcquireSRWLockExclusive(&lock);
    if (hd.count == hd.capacity) {
        const int capacity = hd.capacity ? hd.capacity * 2 : 16;
        Picture *grown = (Picture *)realloc(hd.pictures, (size_t)capacity * sizeof(Picture));
        if (grown == NULL) {
            ReleaseSRWLockExclusive(&lock);
            free(data);
            snprintf(hd.why, sizeof(hd.why), "out of memory for the pictures");
            return 0;
        }
        hd.pictures = grown;
        hd.capacity = capacity;
    }
    hd.pictures[hd.count].time = time;
    hd.pictures[hd.count].number = hd.decoded++;
    hd.pictures[hd.count].width = hd.info.width;
    hd.pictures[hd.count].height = hd.info.height;
    hd.pictures[hd.count].data = data;
    hd.pictures[hd.count].size = size;
    hd.count++;
    ReleaseSRWLockExclusive(&lock);
    return 1;
}

/* Copies a decoded sample's planes into a new picture of the shown size and
 * adds it to the list. */
static int keep_picture(IMFSample *sample, double time)
{
    IMFMediaBuffer *buffer = NULL;
    IMF2DBuffer *two_d = NULL;
    BYTE *scan0 = NULL;
    LONG pitch = 0;
    DWORD length = 0;
    int locked_2d = 0;
    const int w = hd.info.width, h = hd.info.height;
    const size_t size = (size_t)w * (size_t)h * 3 / 2;
    uint8_t *data;
    int rows;
    HRESULT hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);
    if (FAILED(hr)) {
        say("a picture cannot be read", hr);
        return 0;
    }
    IMFMediaBuffer_GetCurrentLength(buffer, &length);
    if (SUCCEEDED(IMFMediaBuffer_QueryInterface(buffer, &IID_IMF2DBuffer, (void **)&two_d)) &&
        SUCCEEDED(IMF2DBuffer_Lock2D(two_d, &scan0, &pitch))) {
        locked_2d = 1;
    } else {
        DWORD max_length = 0;
        hr = IMFMediaBuffer_Lock(buffer, &scan0, &max_length, &length);
        if (FAILED(hr)) {
            if (two_d != NULL)
                IMF2DBuffer_Release(two_d);
            IMFMediaBuffer_Release(buffer);
            say("a picture cannot be read", hr);
            return 0;
        }
        pitch = hd.default_stride > 0 ? hd.default_stride : hd.info.width;
    }
    /* the luma plane has the decoder's rows (MF_MT_FRAME_SIZE); the chroma comes after them */
    rows = hd.coded_height;
    if (pitch <= 0 || (length != 0 && (DWORD)pitch * (DWORD)rows * 3 / 2 > length)) {
        rows = 0; /* not the layout this code reads */
    }
    data = rows >= h ? picture_buffer(size) : NULL;
    if (data != NULL) {
        for (int y = 0; y < h; ++y) {
            memcpy(data + (size_t)y * w, scan0 + (size_t)y * pitch, (size_t)w);
        }
        for (int y = 0; y < h / 2; ++y) {
            memcpy(data + (size_t)w * h + (size_t)y * w, scan0 + (size_t)pitch * rows + (size_t)y * pitch, (size_t)w);
        }
    }
    if (locked_2d) {
        IMF2DBuffer_Unlock2D(two_d);
    } else {
        IMFMediaBuffer_Unlock(buffer);
    }
    if (two_d != NULL) {
        IMF2DBuffer_Release(two_d);
    }
    IMFMediaBuffer_Release(buffer);
    if (data == NULL) {
        snprintf(hd.why, sizeof(hd.why), "a picture is not laid out as NV12 of %dx%d (pitch %ld, %lu bytes)", w,
                 hd.coded_height, (long)pitch, (unsigned long)length);
        return 0;
    }
    return add_picture(data, size, time);
}

static double newest_time(void);

/* Decodes one picture (or finds the end). Returns 0 on an error. */
static int read_picture(void)
{
    DWORD index = 0, flags = 0;
    LONGLONG time = 0;
    IMFSample *sample = NULL;
    HRESULT hr = IMFSourceReader_ReadSample(hd.video, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &index, &flags,
                                            &time, &sample);
    int ok = 1;
    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
        say("the picture cannot be decoded", hr);
        return 0;
    }
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
        ok = read_video_type();
    }
    if (sample != NULL && hd.end > 0.0 && (double)time / 1e7 >= hd.end) {
        /* the part's end: neither this picture nor those after it are shown */
        IMFSample_Release(sample);
        hd.video_eos = 1;
        return ok;
    }
    if (sample != NULL && hd.from > 0.0 &&
        (double)time / 1e7 + (hd.info.fps > 0.0 ? 0.5 / hd.info.fps : 0.008) < hd.from) {
        /* before the part's start (the seek goes to the key picture before it) */
        IMFSample_Release(sample);
        sample = NULL;
    }
    if (ok && sample != NULL) {
        ok = keep_picture(sample, (double)time / 1e7);
    }
    if (sample != NULL) {
        IMFSample_Release(sample);
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        const double last = newest_time();
        hd.video_eos = 1;
        if (ok && hd.end == 0.0 && hd.duration > 0.0 && last + 0.5 < hd.duration) {
            /* its pictures end well before its length: a file cut short. It
             * fails as a broken file does (and the game's movie is shown) */
            snprintf(hd.why, sizeof(hd.why), "its pictures end at %.2f s of its %.2f s (the file cut short?)", last,
                     hd.duration);
            ok = 0;
        }
    }
    return ok;
}

/* Decodes some sound into the stream (or finds its end). An error ends the
 * sound there. */
static void read_sound(void)
{
    DWORD index = 0, flags = 0;
    LONGLONG time = 0;
    IMFSample *sample = NULL;
    HRESULT hr = IMFSourceReader_ReadSample(hd.audio, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &index, &flags,
                                            &time, &sample);
    if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
        fprintf(stderr, "movies: the HD video's sound cannot be decoded (0x%08lx); it ends there\n", (unsigned long)hr);
        hd.sound_eos = 1;
        SDL_FlushAudioStream(hd.sound);
        return;
    }
    if (sample != NULL) {
        IMFMediaBuffer *buffer = NULL;
        BYTE *data = NULL;
        DWORD length = 0;
        if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buffer))) {
            if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length))) {
                const int frame_bytes = hd.info.channels * (int)sizeof(float);
                double at = (double)time / 1e7;
                if (hd.from > 0.0 && at < hd.from) {
                    /* before the part's start: left out */
                    const DWORD early = (DWORD)((hd.from - at) * hd.info.rate) * (DWORD)frame_bytes;
                    const DWORD skipped = early < length ? early : length;
                    data += skipped;
                    length -= skipped;
                    at += (double)(skipped / (DWORD)frame_bytes) / hd.info.rate;
                }
                if (hd.end > 0.0 && at + (double)(length / (DWORD)frame_bytes) / hd.info.rate > hd.end) {
                    /* the part's end: the sound up to it */
                    const double kept = hd.end > at ? (hd.end - at) * hd.info.rate : 0.0;
                    length = (DWORD)kept * (DWORD)frame_bytes;
                    hd.sound_eos = 1;
                }
                if (hd.fade_in > 0.0 || hd.fade_out > 0.0) {
                    float *samples = (float *)data;
                    const DWORD frames = length / (DWORD)frame_bytes;
                    for (DWORD i = 0; i < frames; ++i) {
                        const float level = fade_level(at + (double)i / hd.info.rate);
                        for (int c = 0; c < hd.info.channels; ++c) {
                            samples[i * (DWORD)hd.info.channels + (DWORD)c] *= level;
                        }
                    }
                }
                if (hd.put_frames == 0) {
                    hd.sound_start = at;
                }
                SDL_PutAudioStreamData(hd.sound, data, (int)length);
                InterlockedExchangeAdd64(&hd.put_frames, (LONGLONG)(length / (DWORD)frame_bytes));
                IMFMediaBuffer_Unlock(buffer);
            }
            IMFMediaBuffer_Release(buffer);
        }
        IMFSample_Release(sample);
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        hd.sound_eos = 1;
    }
    if (hd.sound_eos) {
        /* nothing more comes: flush, so what the stream holds back for its
         * resampler plays too (otherwise a few frames stay queued for ever:
         * 56 bytes at the end of hd818.mp4) */
        SDL_FlushAudioStream(hd.sound);
    }
}

/* The seconds of sound queued in the stream, not yet played. */
static double sound_queued(void)
{
    const int queued = SDL_GetAudioStreamQueued(hd.sound);
    return queued > 0 ? (double)queued / (double)(hd.info.channels * (int)sizeof(float)) / hd.info.rate : 0.0;
}

/* The newest decoded picture's time (very negative when there is none). */
static double newest_time(void)
{
    double t;
    AcquireSRWLockShared(&lock);
    t = hd.count > 0 ? hd.pictures[hd.count - 1].time : -1.0e9;
    ReleaseSRWLockShared(&lock);
    return t;
}

/* Switches to the counter's clock from now on, starting at `time`. */
static void use_counter_clock(double time)
{
    AcquireSRWLockExclusive(&lock);
    hd.counter_base = time;
    hd.counter_start = counter();
    hd.paused_ticks = 0;
    if (hd.paused) {
        hd.paused_at = hd.counter_start;
    }
    InterlockedExchange(&hd.counter_clock, 1);
    ReleaseSRWLockExclusive(&lock);
}

/* Opens the file and decodes its first picture and a little of its sound.
 * Returns whether the video can play (if not, hd.why says why). */
static int open_and_begin(void)
{
    if (!open_video()) {
        return 0;
    }
    while (hd.count == 0 && !hd.video_eos) { /* the first picture */
        if (!read_picture()) {
            break;
        }
    }
    if (hd.count == 0) {
        if (hd.why[0] == '\0') {
            snprintf(hd.why, sizeof(hd.why), "the file has no picture");
        }
        return 0;
    }
    hd.first_time = hd.pictures[0].time;
    if (open_audio()) {
        while (!hd.sound_eos && sound_queued() < SOUND_AHEAD) {
            read_sound();
        }
    } else {
        use_counter_clock(hd.first_time);
    }
    return 1;
}

/* Once the sound has all played -- or what is left of it has not moved for a
 * quarter of a second -- the clock goes on from the sound's end on the
 * performance counter. */
static void follow_sound_end(void)
{
    const int queued = SDL_GetAudioStreamQueued(hd.sound);
    if (queued != hd.last_queued || hd.still_since == 0) {
        hd.last_queued = queued;
        hd.still_since = counter();
    }
    if (queued <= 0 || counter() - hd.still_since > hd.frequency.QuadPart / 4) {
        use_counter_clock(clock_now());
    }
}

/* One turn of the thread while the video plays: decodes pictures and sound to
 * keep them a little ahead of the clock. Returns 1 if it decoded something, 0
 * if nothing was needed, or -1 if a picture cannot be decoded. */
static int decode_ahead(void)
{
    const double now = clock_now();
    int did = 0;
    if (!hd.video_eos && newest_time() < now + PICTURES_AHEAD) {
        if (!read_picture()) {
            return -1;
        }
        did = 1;
    }
    if (hd.sound != NULL && !hd.sound_eos && sound_queued() < SOUND_AHEAD) {
        read_sound();
        did = 1;
    }
    if (hd.started && hd.sound != NULL && !hd.counter_clock && hd.sound_eos) {
        follow_sound_end();
    }
    return did;
}

/* The video's thread: opens the file, then keeps decoding ahead until it is
 * told to stop or a picture fails. */
static DWORD WINAPI run(LPVOID unused)
{
    const HRESULT com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT hr;
    (void)unused;
    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        say("Media Foundation did not start", hr);
    } else {
        if (open_and_begin()) {
            hd.open_ok = 1;
            hd.status = 0;
        }
        SetEvent(hd.opened);
        while (hd.open_ok && !hd.stop) {
            const int did = hd.paused ? 0 : decode_ahead();
            khdays_theater_music_tick(clock_now(), hd.started, hd.paused);
            if (did < 0) {
                AcquireSRWLockExclusive(&lock);
                hd.status = -1;
                ReleaseSRWLockExclusive(&lock);
                break;
            }
            if (did == 0) {
                WaitForSingleObject(hd.wake, 2); /* nothing to do: wait a little, or until woken */
            }
        }
        if (hd.video != NULL) {
            IMFSourceReader_Release(hd.video);
            hd.video = NULL;
        }
        if (hd.audio != NULL) {
            IMFSourceReader_Release(hd.audio);
            hd.audio = NULL;
        }
        MFShutdown();
    }
    if (!hd.open_ok) {
        SetEvent(hd.opened); /* setting it again is harmless */
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return 0;
}

/* The video closed; the theater's music with it, unless `next_follows` (another video opens at once)
 * and the music goes on into it. */
static void close_video(int next_follows)
{
    khdays_theater_music_close(next_follows);
    if (hd.thread == NULL) {
        return;
    }
    InterlockedExchange(&hd.stop, 1);
    SetEvent(hd.wake);
    WaitForSingleObject(hd.thread, INFINITE);
    CloseHandle(hd.thread);
    CloseHandle(hd.opened);
    CloseHandle(hd.wake);
    if (hd.sound != NULL) {
        SDL_DestroyAudioStream(hd.sound); /* the sound stops at once */
    }
    if (hd.open_ok) {
        fprintf(stderr,
                "movies: the HD video showed %ld pictures, %ld late (passed over), its picture at most "
                "%.1f ms off its clock\n",
                hd.shown, hd.late, hd.worst_offset * 1000.0);
    }
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < hd.count; ++i) {
#pragma warning(suppress : 6001) /* every item below the count was written before the count moved: none uninitialised */
        free(hd.pictures[i].data);
    }
    for (int i = 0; i < hd.spare_count; ++i) {
#pragma warning(suppress : 6001) /* every item below the count was written before the count moved: none uninitialised */
        free(hd.spare[i].data);
    }
    free(hd.pictures);
    free(hd.spare);
    free(hd.url);
    memset(&hd, 0, sizeof(hd));
    hd.status = 2;
    hd.taken = -1;
    ReleaseSRWLockExclusive(&lock);
}

void khdays_hd_close(void)
{
    close_video(0);
}

void khdays_hd_close_scene_end(void)
{
    close_video(2);
}

/* `path` (UTF-8) as a full Windows path, in a new string to free; NULL if it
 * cannot be converted. As long as the path is: no fixed limit. */
static WCHAR *full_path(const char *path)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    WCHAR *wide = n > 0 ? (WCHAR *)malloc(sizeof(WCHAR) * (size_t)n) : NULL;
    WCHAR *full = NULL;
    if (wide != NULL && MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, n) == n) {
        const DWORD need = GetFullPathNameW(wide, 0, NULL, NULL); /* with its final zero */
        full = need > 0 ? (WCHAR *)malloc(sizeof(WCHAR) * need) : NULL;
        if (full != NULL) {
            const DWORD got = GetFullPathNameW(wide, need, full, NULL);
            if (got == 0 || got >= need) {
                free(full);
                full = NULL;
            }
        }
    }
    free(wide);
    return full;
}

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    return khdays_hd_open_part(path, 0.0, 0.0, info, why, size);
}

int khdays_hd_open_part(const char *path, double from, double until, KhdaysHdInfo *info, char *why, size_t size)
{
    close_video(1);
    hd.from = from > 0.0 ? from : 0.0;
    hd.end = until > hd.from ? until : 0.0;
    hd.fade_in = next_fade_in;
    hd.fade_out = next_fade_out;
    next_fade_in = next_fade_out = 0.0;
    QueryPerformanceFrequency(&hd.frequency);
    hd.url = full_path(path);
    if (hd.url == NULL) {
        snprintf(why, size, "the path is not one Windows reads");
        return 0;
    }
    hd.opened = CreateEventW(NULL, TRUE, FALSE, NULL);
    hd.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    hd.thread = hd.opened != NULL && hd.wake != NULL ? CreateThread(NULL, 0, run, NULL, 0, NULL) : NULL;
    if (hd.thread == NULL) {
        snprintf(why, size, "no thread for it");
        if (hd.opened != NULL)
            CloseHandle(hd.opened);
        if (hd.wake != NULL)
            CloseHandle(hd.wake);
        hd.opened = hd.wake = NULL;
        free(hd.url);
        hd.url = NULL;
        return 0;
    }
    WaitForSingleObject(hd.opened, INFINITE); /* the thread always sets it, opened or not */
    if (!hd.open_ok) {
        snprintf(why, size, "%s", hd.why[0] ? hd.why : "it cannot be played");
        khdays_hd_close();
        return 0;
    }
    *info = hd.info;
    khdays_theater_music_open(path);
    return 1;
}

void khdays_hd_fades(double in, double out)
{
    next_fade_in = in > 0.0 ? in : 0.0;
    next_fade_out = out > 0.0 ? out : 0.0;
}

void khdays_hd_start(void)
{
    if (!hd.open_ok || hd.started) {
        return;
    }
    if (hd.counter_clock) {
        hd.counter_start = counter();
    }
    InterlockedExchange(&hd.started, 1);
    if (hd.sound != NULL) {
        SDL_ResumeAudioStreamDevice(hd.sound);
    }
    SetEvent(hd.wake);
}

void khdays_hd_pause(int pause)
{
    if (!hd.open_ok || (hd.paused != 0) == (pause != 0)) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (pause) {
        hd.paused_at = counter();
    } else {
        hd.paused_ticks += counter() - hd.paused_at;
    }
    InterlockedExchange(&hd.paused, pause != 0);
    ReleaseSRWLockExclusive(&lock);
    if (hd.sound != NULL) {
        if (pause) {
            SDL_PauseAudioStreamDevice(hd.sound);
        } else if (hd.started) {
            SDL_ResumeAudioStreamDevice(hd.sound);
        }
    }
    SetEvent(hd.wake);
}

int khdays_hd_status(char *why, size_t size)
{
    static int trace = -1;
    static Uint64 traced_at;
    int status;
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_MOVIE_HD") != NULL;
    }
    AcquireSRWLockShared(&lock);
    if (trace && SDL_GetTicks() - traced_at >= 1000) {
        /* with KHDAYS_TRACE_MOVIE_HD: once a second, where the video is */
        traced_at = SDL_GetTicks();
        fprintf(stderr,
                "movie hd: status %d, started %ld, clock %.3f, pictures %d (newest %.3f), taken %ld, "
                "decoded %ld, video end %d, sound end %d, queued %d, counter clock %ld\n",
                hd.status, (long)hd.started, clock_now(), hd.count,
                hd.count > 0 ? hd.pictures[hd.count - 1].time : -1.0, hd.taken, hd.decoded, hd.video_eos, hd.sound_eos,
                hd.sound != NULL ? SDL_GetAudioStreamQueued(hd.sound) : -1, (long)hd.counter_clock);
    }
    status = hd.status;
    if (status == 0 && hd.started && hd.video_eos && (hd.sound == NULL || hd.counter_clock)) {
        /* every picture is decoded; it has ended once the last one was taken and its time has passed */
        const double last = hd.count > 0 ? hd.pictures[hd.count - 1].time : hd.first_time;
        const double frame = hd.info.fps > 0.0 ? 1.0 / hd.info.fps : 0.0;
        if ((hd.count == 0 || hd.pictures[hd.count - 1].number == hd.taken) && clock_now() >= last + frame) {
            status = 1;
        }
    }
    if (status == -1 && why != NULL && size > 0) {
        snprintf(why, size, "%s", hd.why);
    }
    ReleaseSRWLockShared(&lock);
    return status;
}

void khdays_hd_set_gain(float g)
{
    gain = g;
    if (hd.sound != NULL) {
        SDL_SetAudioStreamGain(hd.sound, g);
    }
}

int khdays_hd_picture_begin(KhdaysHdPicture *picture)
{
    double now, half;
    int pick = -1;
    AcquireSRWLockExclusive(&lock);
    if (!hd.open_ok || hd.count == 0) {
        ReleaseSRWLockExclusive(&lock);
        return 0;
    }
    now = clock_now();
    half = hd.info.fps > 0.0 ? 0.5 / hd.info.fps : 0.0;
    for (int i = 0; i < hd.count && hd.pictures[i].time <= now + half; ++i) {
        pick = i;
    }
    if (pick < 0 || hd.pictures[pick].number == hd.taken) {
        ReleaseSRWLockExclusive(&lock);
        return 0;
    }
    for (int i = 0; i < pick; ++i) {
        if (hd.pictures[i].number != hd.taken) {
            ++hd.late; /* its time passed before a refresh took it: it was never shown */
        }
        keep_spare(hd.pictures[i]);
    }
    memmove(hd.pictures, hd.pictures + pick, (size_t)(hd.count - pick) * sizeof(Picture));
    hd.count -= pick;
    hd.taken = hd.pictures[0].number;
    ++hd.shown;
    if (hd.started) {
        const double off = now - hd.pictures[0].time;
        if ((off < 0 ? -off : off) > hd.worst_offset) {
            hd.worst_offset = off < 0 ? -off : off;
        }
    }
    picture->width = hd.pictures[0].width;
    picture->height = hd.pictures[0].height;
    picture->pitch = hd.pictures[0].width;
    picture->luma = hd.pictures[0].data;
    picture->chroma = hd.pictures[0].data + (size_t)hd.pictures[0].width * (size_t)hd.pictures[0].height;
    picture->time = hd.pictures[0].time;
    picture->fps = hd.info.fps;
    picture->level = fade_level(hd.pictures[0].time);
    SetEvent(hd.wake); /* there is room ahead now: the thread decodes on */
    return 1;          /* the lock stays held until khdays_hd_picture_done */
}

void khdays_hd_picture_done(void)
{
    ReleaseSRWLockExclusive(&lock);
}

static uint8_t clamp8(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

int khdays_hd_picture_rgba(uint8_t **rgba, size_t *capacity, int *w, int *h)
{
    int found = 0;
    AcquireSRWLockShared(&lock);
    for (int i = 0; i < hd.count && !found; ++i) {
        const Picture *p = &hd.pictures[i];
        size_t need;
        if (p->number != hd.taken) {
            continue;
        }
        found = 1;
        need = (size_t)p->width * (size_t)p->height * 4u;
        if (*capacity < need) {
            uint8_t *grown = (uint8_t *)SDL_realloc(*rgba, need);
            if (grown == NULL) {
                found = 0;
                break;
            }
            *rgba = grown;
            *capacity = need;
        }
        *w = p->width;
        *h = p->height;
        /* NV12: the luma, then the chroma at half the rows, U and V interleaved; BT.709 limited range
         * in 16.16 fixed point (Y' = 1.164 (Y - 16), R += 1.793 V, G -= 0.213 U + 0.533 V, B += 2.112 U) */
        for (int y = 0; y < p->height; ++y) {
            const uint8_t *luma = p->data + (size_t)y * (size_t)p->width;
            const uint8_t *chroma = p->data + (size_t)p->width * (size_t)p->height + (size_t)(y / 2) * (size_t)p->width;
            uint8_t *out = *rgba + (size_t)y * (size_t)p->width * 4u;
            for (int x = 0; x < p->width; ++x) {
                const int yy = 76284 * (luma[x] - 16);
                const int u = chroma[(x & ~1)] - 128, v = chroma[(x & ~1) + 1] - 128;
                out[4 * x + 0] = clamp8((yy + 117504 * v + 32768) >> 16);
                out[4 * x + 1] = clamp8((yy - 13954 * u - 34903 * v + 32768) >> 16);
                out[4 * x + 2] = clamp8((yy + 138392 * u + 32768) >> 16);
                out[4 * x + 3] = 255;
            }
        }
    }
    ReleaseSRWLockShared(&lock);
    return found;
}

/* --- A voice over the game (khdays_hd_voice_play) ---------------------------------------------- */

static SRWLOCK voice_lock = SRWLOCK_INIT; /* guards the voice's stream against the pause and the volume */
static float voice_gain = 1.0f;           /* khdays_hd_voice_set_gain */
static volatile LONG voice_paused;        /* khdays_hd_voice_pause */

static struct {
    HANDLE thread;
    volatile LONG stop;
    WCHAR **files; /* full paths */
    int count;
    SDL_AudioStream *sound; /* opened by the thread for the first file; destroyed once it has ended */
    /* The voice's clock, under voice_lock: the seconds of sound put into the stream, where each file
     * began among them, the files begun, their videos' names ("hd007b"), the format queued now, and
     * whether the thread has put all it will. */
    double put;
    double *began; /* as many as the files */
    int begun;
    char **names;
    int channels, rate;
    volatile LONG done;
    ULONGLONG quiet_since; /* when the last of it was heard (0: not yet) */
} voice;

/* The voice's stream, opened for `spec` the first time, else told the next file's format (what is
 * queued plays on in its own). NULL if the system gives no sound. */
static SDL_AudioStream *voice_stream(const SDL_AudioSpec *spec)
{
    SDL_AudioStream *stream;
    AcquireSRWLockExclusive(&voice_lock);
    if (voice.sound == NULL) {
        voice.sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, spec, NULL, NULL);
        if (voice.sound == NULL) {
            fprintf(stderr, "movies: no sound for the voice: %s\n", SDL_GetError());
        } else {
            SDL_SetAudioStreamGain(voice.sound, voice_gain);
            if (!voice_paused) {
                SDL_ResumeAudioStreamDevice(voice.sound);
            }
        }
    } else {
        SDL_SetAudioStreamFormat(voice.sound, spec, NULL);
    }
    stream = voice.sound;
    ReleaseSRWLockExclusive(&voice_lock);
    return stream;
}

/* One file's sound into the voice's stream, a little ahead of the device, to its end or a stop. */
static void voice_file(const WCHAR *file, int nth)
{
    IMFSourceReader *reader = NULL;
    SDL_AudioStream *stream = NULL;
    UINT32 channels = 0, rate = 0;
    double put = 0.0; /* seconds of sound put into the stream */
    HRESULT hr = MFCreateSourceReaderFromURL(file, NULL, &reader);
    if (SUCCEEDED(hr)) {
        hr = read_sound_as_floats(reader, &channels, &rate);
    }
    if (FAILED(hr) || channels == 0 || rate == 0) {
        fprintf(stderr, "movies: no voice in %ls (0x%08lx)\n", file, (unsigned long)hr);
    } else {
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, (int)channels, (int)rate};
        stream = voice_stream(&spec);
        if (stream != NULL) {
            AcquireSRWLockExclusive(&voice_lock);
            if (nth < voice.count) {
                voice.began[nth] = voice.put;
                voice.begun = nth + 1;
            }
            voice.channels = (int)channels;
            voice.rate = (int)rate;
            ReleaseSRWLockExclusive(&voice_lock);
        }
    }
    while (stream != NULL && !voice.stop) {
        DWORD index = 0, flags = 0;
        LONGLONG time = 0;
        IMFSample *sample = NULL;
        const double queued = (double)SDL_GetAudioStreamQueued(stream) / (double)(channels * sizeof(float)) / rate;
        if (voice_paused || queued >= SOUND_AHEAD) {
            Sleep(10);
            continue;
        }
        hr = IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &index, &flags, &time,
                                        &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            fprintf(stderr, "movies: the voice in %ls cannot be decoded (0x%08lx); it ends there\n", file,
                    (unsigned long)hr);
            break;
        }
        if (sample != NULL) {
            IMFMediaBuffer *buffer = NULL;
            BYTE *data = NULL;
            DWORD length = 0;
            if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buffer))) {
                if (SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length))) {
                    const double seconds = (double)length / (double)(channels * sizeof(float)) / rate;
                    AcquireSRWLockExclusive(&voice_lock);
                    SDL_PutAudioStreamData(stream, data, (int)length);
                    voice.put += seconds;
                    ReleaseSRWLockExclusive(&voice_lock);
                    put += seconds;
                    IMFMediaBuffer_Unlock(buffer);
                }
                IMFMediaBuffer_Release(buffer);
            }
            IMFSample_Release(sample);
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break;
        }
    }
    if (stream != NULL) {
        SDL_FlushAudioStream(stream); /* what the resampler holds back plays too */
        fprintf(stderr, "movies: the voice of %ls: %.1f s of its sound %s\n", file, put,
                voice.stop ? "before it was stopped" : "to its end");
    }
    if (reader != NULL) {
        IMFSourceReader_Release(reader);
    }
}

/* The voice's thread: the files' sound one after another; the stream plays on what is queued. */
static DWORD WINAPI voice_run(LPVOID unused)
{
    const HRESULT com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    (void)unused;
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        for (int i = 0; i < voice.count && !voice.stop; ++i) {
            voice_file(voice.files[i], i);
        }
        MFShutdown();
    }
    InterlockedExchange(&voice.done, 1);
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return 0;
}

void khdays_hd_voice_stop(void)
{
    if (voice.thread == NULL) {
        return;
    }
    InterlockedExchange(&voice.stop, 1);
    WaitForSingleObject(voice.thread, INFINITE);
    CloseHandle(voice.thread);
    AcquireSRWLockExclusive(&voice_lock);
    if (voice.sound != NULL) {
        SDL_DestroyAudioStream(voice.sound); /* the voice stops at once */
    }
    for (int i = 0; i < voice.count; ++i) {
        free(voice.files[i]);
        free(voice.names[i]);
    }
    free(voice.files);
    free(voice.names);
    free(voice.began);
    memset(&voice, 0, sizeof(voice));
    ReleaseSRWLockExclusive(&voice_lock);
}

void khdays_hd_voice_play(const char *const *paths, int count)
{
    WCHAR **files;
    char **names;
    double *began;
    int n = 0;
    khdays_hd_voice_stop();
    if (count <= 0) {
        return;
    }
    files = (WCHAR **)calloc((size_t)count, sizeof(*files));
    names = (char **)calloc((size_t)count, sizeof(*names));
    began = (double *)calloc((size_t)count, sizeof(*began));
    if (files == NULL || names == NULL || began == NULL) {
        free(files);
        free(names);
        free(began);
        return;
    }
    for (int i = 0; i < count; ++i) {
        WCHAR *file = full_path(paths[i]);
        const char *base = paths[i];
        char *dot;
        if (file == NULL) {
            continue;
        }
        /* its video's name, for its subtitles: the path's file name without its extension */
        for (const char *p = paths[i]; *p; ++p) {
            if (*p == '/' || *p == '\\') {
                base = p + 1;
            }
        }
        names[n] = _strdup(base);
        dot = names[n] != NULL ? strrchr(names[n], '.') : NULL;
        if (dot != NULL) {
            *dot = '\0';
        }
        files[n++] = file;
    }
    voice.files = files;
    voice.names = names;
    voice.began = began;
    voice.count = n;
    voice.thread = CreateThread(NULL, 0, voice_run, NULL, 0, NULL);
    if (voice.thread == NULL) {
        for (int i = 0; i < n; ++i) {
            free(files[i]);
            free(names[i]);
        }
        free(files);
        free(names);
        free(began);
        memset(&voice, 0, sizeof(voice));
    }
}

/* The seconds of the stream's sound not heard yet. Under voice_lock. */
static double voice_unheard(void)
{
    if (voice.sound == NULL || voice.channels <= 0 || voice.rate <= 0) {
        return 0.0;
    }
    return (double)SDL_GetAudioStreamQueued(voice.sound) / (double)(voice.channels * (int)sizeof(float)) / voice.rate;
}

int khdays_hd_voice_clock(char *name, size_t size, double *seconds)
{
    int found = 0;
    AcquireSRWLockExclusive(&voice_lock);
    if (voice.thread != NULL && voice.sound != NULL && voice.begun > 0) {
        const double heard = voice.put - voice_unheard();
        int file = 0;
        for (int i = 0; i < voice.begun; ++i) {
            if (voice.began[i] <= heard) {
                file = i;
            }
        }
        snprintf(name, size, "%s", voice.names[file] != NULL ? voice.names[file] : "");
        *seconds = heard - voice.began[file] > 0.0 ? heard - voice.began[file] : 0.0;
        found = 1;
    }
    ReleaseSRWLockExclusive(&voice_lock);
    return found;
}

int khdays_hd_voice_speaking(void)
{
    int speaking = 0;
    AcquireSRWLockExclusive(&voice_lock);
    if (voice.thread != NULL) {
        if (!InterlockedCompareExchange(&voice.done, 0, 0) || voice_paused || voice_unheard() > 0.0) {
            voice.quiet_since = 0;
            speaking = 1;
        } else {
            /* the last word heard: a moment's silence after it, as the theater leaves */
            if (voice.quiet_since == 0) {
                voice.quiet_since = GetTickCount64();
            }
            speaking = GetTickCount64() - voice.quiet_since < 700;
        }
    }
    ReleaseSRWLockExclusive(&voice_lock);
    return speaking;
}

void khdays_hd_voice_pause(int pause)
{
    AcquireSRWLockExclusive(&voice_lock);
    InterlockedExchange(&voice_paused, pause != 0);
    if (voice.sound != NULL) {
        if (pause) {
            SDL_PauseAudioStreamDevice(voice.sound);
        } else {
            SDL_ResumeAudioStreamDevice(voice.sound);
        }
    }
    ReleaseSRWLockExclusive(&voice_lock);
}

void khdays_hd_voice_set_gain(float g)
{
    AcquireSRWLockExclusive(&voice_lock);
    voice_gain = g;
    if (voice.sound != NULL) {
        SDL_SetAudioStreamGain(voice.sound, g);
    }
    ReleaseSRWLockExclusive(&voice_lock);
}
