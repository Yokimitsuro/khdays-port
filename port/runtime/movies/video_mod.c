/* A movie a mod puts in place of the game's (video_mod.h): two pl_mpeg readers of the file, picture
 * and sound, on one thread. Each frame up to the game's movie time (its frame index over the .mods
 * header's frame rate at 0x14, 24 fractional bits) becomes a changing replacement texture, the mod's
 * frame centred on a black canvas shaped like the game's. The sound plays from a two-second ring
 * filled ahead, resampled to the output's rate. */
#include "video_mod.h"

#include "movie_hd.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../mods/textures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
#include "../core/bytes.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

static struct {
    volatile LONG active; /* a mod's movie plays */
    char id[64];
    plm_t *video, *audio;
    double fps;  /* the game's movie's frames a second */
    int picture; /* the canvas: a changing replacement */
    int width, height, left, top;
    volatile LONG target; /* the game's frame index + 1; 0 before its first */
    volatile LONG stop;
    HANDLE thread, wake;
    /* the sound's ring, stereo frames at `rate` */
    float *ring;
    u32 ring_frames;
    volatile LONG written; /* frames written, all told */
    double read;           /* frames read, all told (the sound thread's) */
    int rate;
    volatile LONG sound_on; /* from the game's first frame */
} mod = {0, "", NULL, NULL, 0.0, -1};

/* the ring, between the sound thread and a stop */
static SRWLOCK ring_lock = SRWLOCK_INIT;

static void show(const plm_frame_t *frame)
{
    uint32_t *texels = khdays_textures_change_begin(mod.picture);
    plm_frame_to_rgba((plm_frame_t *)frame, (uint8_t *)(texels + (size_t)mod.top * mod.width + mod.left),
                      mod.width * 4);
    khdays_textures_change_end(mod.picture);
}

static DWORD WINAPI decode(LPVOID unused)
{
    plm_frame_t *ahead = NULL; /* decoded, not due yet */
    (void)unused;
    while (!mod.stop) {
        WaitForSingleObject(mod.wake, 4);
        if (mod.target > 0) {
            const double due = (double)(mod.target - 1) / mod.fps;
            for (;;) {
                if (ahead == NULL) {
                    ahead = plm_decode_video(mod.video);
                    if (ahead == NULL) {
                        break; /* its end: the last picture stays */
                    }
                }
                if (ahead->time > due + 0.5 / mod.fps) {
                    break;
                }
                show(ahead);
                ahead = NULL;
            }
        }
        while (mod.audio != NULL &&
               (u32)(mod.written - (LONG)mod.read) + PLM_AUDIO_SAMPLES_PER_FRAME <= mod.ring_frames) {
            const plm_samples_t *samples = plm_decode_audio(mod.audio);
            if (samples == NULL) {
                break;
            }
            for (u32 i = 0; i < samples->count; ++i) {
                const u32 at = ((u32)mod.written + i) % mod.ring_frames;
                mod.ring[2 * at] = samples->interleaved[2 * i];
                mod.ring[2 * at + 1] = samples->interleaved[2 * i + 1];
            }
            InterlockedExchangeAdd(&mod.written, (LONG)samples->count);
        }
    }
    return 0;
}

/* Streams the game opened since its last stop: a movie of more than one is
 * not replaced. */
static int streams;

static void stop_replacement(void)
{
    if (!mod.active) {
        return;
    }
    InterlockedExchange(&mod.active, 0);
    InterlockedExchange(&mod.sound_on, 0);
    InterlockedExchange(&mod.stop, 1);
    SetEvent(mod.wake);
    WaitForSingleObject(mod.thread, INFINITE);
    CloseHandle(mod.thread);
    CloseHandle(mod.wake);
    plm_destroy(mod.video);
    if (mod.audio != NULL) {
        plm_destroy(mod.audio);
    }
    AcquireSRWLockExclusive(&ring_lock);
    free(mod.ring);
    mod.ring = NULL;
    ReleaseSRWLockExclusive(&ring_lock);
    mod.video = mod.audio = NULL;
    khdays_log("movie %s: its replacement stopped", mod.id);
}

void khdays_video_movie_stopped(void)
{
    streams = 0;
    stop_replacement();
    khdays_movie_hd_stopped(); /* an HD video in its place */
}

static void mod_opened(int slot, const char *path);

void khdays_video_movie_opened(int slot, const char *path)
{
    mod_opened(slot, path);
    /* an HD video in its place, unless a mod's movie took it */
    khdays_movie_hd_stream(slot, path, mod.active != 0);
}

static void mod_opened(int slot, const char *path)
{
    const char *base, *dot;
    char relative[96], file[MAX_PATH];
    u8 header[0x30];
    u32 offset, size;
    size_t n;
    khdays_log("movie stream %d opened: %s", slot, path != NULL ? path : "(none)");
    if (path == NULL) {
        return;
    }
    if (++streams > 1) {
        if (mod.active) {
            khdays_log("movie %s: a second stream (%s); not replaced", mod.id, path);
            stop_replacement();
        }
        return;
    }
    (void)slot; /* the opening opens its one stream in slot 1 */
    base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    dot = strrchr(base, '.');
    n = dot != NULL ? (size_t)(dot - base) : strlen(base);
    if (n == 0 || n >= sizeof(mod.id)) {
        return;
    }
    memcpy(mod.id, base, n);
    mod.id[n] = '\0';
    snprintf(relative, sizeof(relative), "movies/%s.mpg", mod.id);
    if (!khdays_mods_find(relative, file, (int)sizeof(file))) {
        return;
    }
    if (!khdays_rom_find(path, &offset, &size) || size < sizeof(header)) {
        return;
    }
    khdays_rom_read(offset, header, sizeof(header));
    mod.fps = le32(header + 0x14) / (double)0x01000000;
    mod.video = plm_create_with_filename(file);
    mod.audio = plm_create_with_filename(file);
    if (mod.fps <= 0.0 || mod.video == NULL || mod.audio == NULL || !plm_has_headers(mod.video) ||
        plm_get_width(mod.video) <= 0) {
        static char told[64];
        if (strcmp(told, mod.id) != 0) {
            snprintf(told, sizeof(told), "%s", mod.id);
            fprintf(stderr, "movies: %s is not an MPEG-1 file pl_mpeg reads; the game's movie plays\n", file);
            khdays_log("movie %s: %s is not an MPEG-1 file pl_mpeg reads; the game's movie plays", mod.id, file);
        }
        if (mod.video != NULL)
            plm_destroy(mod.video);
        if (mod.audio != NULL)
            plm_destroy(mod.audio);
        mod.video = mod.audio = NULL;
        return;
    }
    plm_set_audio_enabled(mod.video, 0);
    plm_set_video_enabled(mod.audio, 0);
    if (!plm_has_headers(mod.audio) || plm_get_num_audio_streams(mod.audio) == 0) {
        plm_destroy(mod.audio);
        mod.audio = NULL;
    } else {
        plm_set_audio_enabled(mod.audio, 1);
    }
    {
        /* the canvas: the game's frame's shape (the header's 0x0c, 0x10: 256x160),
         * the mod's frame in its middle; drawn as video.c draws the game's (its
         * 32-byte units a row and its rows) */
        const int w = plm_get_width(mod.video), h = plm_get_height(mod.video);
        const int ds_width = (int)le32(header + 0x0c), ds_height = (int)le32(header + 0x10);
        char name[96];
        uint32_t *texels;
        if (ds_width <= 0 || ds_height <= 0) {
            plm_destroy(mod.video);
            if (mod.audio != NULL)
                plm_destroy(mod.audio);
            mod.video = mod.audio = NULL;
            return;
        }
        if ((long long)w * ds_height >= (long long)h * ds_width) {
            mod.width = w;
            mod.height = (int)(((long long)w * ds_height + ds_width - 1) / ds_width);
        } else {
            mod.height = h;
            mod.width = (int)(((long long)h * ds_width + ds_height - 1) / ds_height);
        }
        mod.left = (mod.width - w) / 2;
        mod.top = (mod.height - h) / 2;
        snprintf(name, sizeof(name), "movie-mod@%s %dx%d", mod.id, mod.width, mod.height);
        mod.picture = khdays_textures_add_changing(name, mod.width, mod.height,
                                                   KHDAYS_REPLACEMENT_BITMAP | (u32)(ds_width * 2 / 32) << 8 |
                                                       (u32)ds_height << 16);
        if (mod.picture < 0) {
            plm_destroy(mod.video);
            if (mod.audio != NULL)
                plm_destroy(mod.audio);
            mod.video = mod.audio = NULL;
            return;
        }
        texels = khdays_textures_change_begin(mod.picture);
        for (int i = 0; i < mod.width * mod.height; ++i) {
            texels[i] = 0xff000000u; /* black */
        }
        khdays_textures_change_end(mod.picture);
    }
    mod.rate = mod.audio != NULL ? plm_get_samplerate(mod.audio) : 0;
    mod.ring_frames = mod.rate > 0 ? (u32)mod.rate * 2u : 0u;
    mod.ring = mod.ring_frames ? (float *)calloc(mod.ring_frames * 2u, sizeof(float)) : NULL;
    mod.written = 0;
    mod.read = 0.0;
    mod.target = 0;
    mod.stop = 0;
    mod.wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    mod.thread = CreateThread(NULL, 0, decode, NULL, 0, NULL);
    InterlockedExchange(&mod.active, 1);
    fprintf(stderr, "movies: %s drawn instead from %s (%dx%d, %s)\n", mod.id, file, plm_get_width(mod.video),
            plm_get_height(mod.video), mod.audio != NULL ? "with its sound" : "no sound");
    khdays_log("movie %s: replaced by %s", mod.id, file);
}

void khdays_video_mod_frame(u32 index)
{
    if (!mod.active) {
        return;
    }
    InterlockedExchange(&mod.target, (LONG)index + 1);
    if (mod.audio != NULL) {
        InterlockedExchange(&mod.sound_on, 1);
    }
    SetEvent(mod.wake);
}

int khdays_video_mod_picture(void)
{
    return mod.active ? mod.picture : -1;
}

int khdays_video_mod_sound(void)
{
    return mod.active && mod.audio != NULL;
}

void khdays_video_mod_mix(u32 rate, float gain, float out[2])
{
    if (!mod.sound_on || rate == 0) {
        return;
    }
    AcquireSRWLockShared(&ring_lock);
    if (mod.ring != NULL && mod.read + 1.0 < (double)mod.written) {
        const u32 at = (u32)mod.read;
        const double f = mod.read - (double)at;
        const u32 a = at % mod.ring_frames, b = (at + 1) % mod.ring_frames;
        out[0] += gain * (float)(mod.ring[2 * a] * (1.0 - f) + mod.ring[2 * b] * f);
        out[1] += gain * (float)(mod.ring[2 * a + 1] * (1.0 - f) + mod.ring[2 * b + 1] * f);
        mod.read += (double)mod.rate / (double)rate;
    } /* else not decoded yet: silence */
    ReleaseSRWLockShared(&ring_lock);
}
