/* The theater's music over an HD video (theater_music.h). `control` guards the tracks and the
 * schedule; `decoder` guards each track's decoding, which the audio thread does on demand. Nothing
 * calls into an SDL stream while holding `decoder`: the audio thread holds the stream's lock while it
 * waits for `decoder`. */
#include "theater_music.h"

#include "music_schedule.h"
#include "../../runtime/mods/installed.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define FADE_SECONDS 0.4  /* a stopped track fades out this long */
#define CHUNK_FRAMES 1024 /* decoded at a time */

typedef struct Track {
    SDL_AudioStream *stream;
    void *file; /* the .ogg, read whole: stb_vorbis decodes from memory */
    stb_vorbis *ogg;
    int channels, rate;
    long long length; /* in samples */
    long long loop;   /* where its end goes back to; -1: it plays once */
    int ended;        /* it played out, or its fade did */
    int fade_total, fade_left;
    float *buffer; /* CHUNK_FRAMES frames */
    long long fed; /* samples given to the stream, for the message when it fades out */
    float peak;    /* their loudest */
    char name[64];
} Track;

static void *control_lock, *decoder_lock; /* SDL_Mutex *, made at first use */
static SDL_AtomicInt gain_bits;           /* khdays_theater_music_set_gain's float, as bits (0: not set, 1.0) */
static Track *playing, *leaving;          /* the track the schedule plays; one fading out */
static KhdaysMusicSchedule schedule;
static int have_schedule;
static char folder[1024]; /* the schedule's folder, where its tracks are */
static char video[96];    /* the video's name, for the messages */
static int playing_cue = -1;
static int carried;        /* the playing track goes on into the next video */
static Uint64 kept_since;  /* when the playing track was kept for a next video (keep_for_next) */
static int video_open;     /* a video is open (between khdays_theater_music_open and its close) */
static float applied_gain; /* the gain set on the playing track's stream */
static int stream_paused;

/* A lock made at first use (any thread may be first). */
static SDL_Mutex *lock_of(void **slot)
{
    SDL_Mutex *m = (SDL_Mutex *)SDL_GetAtomicPointer(slot);
    if (m == NULL) {
        SDL_Mutex *made = SDL_CreateMutex();
        if (!SDL_CompareAndSwapAtomicPointer(slot, NULL, made)) {
            SDL_DestroyMutex(made);
        }
        m = (SDL_Mutex *)SDL_GetAtomicPointer(slot);
    }
    return m;
}

static void (*logger)(const char *line); /* khdays_theater_music_log_to's */

void khdays_theater_music_log_to(void (*log)(const char *line))
{
    logger = log;
}

/* A line for the console and, when one was given, the session log. */
static void say(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    fprintf(stderr, "movies: %s\n", line);
    if (logger != NULL) {
        logger(line);
    }
}

static float base_gain(void)
{
    const int bits = SDL_GetAtomicInt(&gain_bits);
    float gain = 1.0f;
    if (bits != 0) {
        memcpy(&gain, &bits, sizeof(gain));
    }
    return gain;
}

/* The audio thread, when the stream wants more: the track decoded on from where it is, back to its
 * loop at its end, faded out when it is stopping. */
static void SDLCALL feed(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    Track *t = (Track *)userdata;
    const int frame_bytes = t->channels * (int)sizeof(float);
    SDL_Mutex *decoder = lock_of(&decoder_lock);
    (void)total;
    SDL_LockMutex(decoder);
    while (additional > 0 && !t->ended) {
        int got = stb_vorbis_get_samples_float_interleaved(t->ogg, t->channels, t->buffer, CHUNK_FRAMES * t->channels);
        if (got <= 0) {
            if (t->loop >= 0 && stb_vorbis_seek(t->ogg, (unsigned int)t->loop)) {
                continue;
            }
            t->ended = 1;
            break;
        }
        if (t->fade_total > 0) {
            for (int i = 0; i < got; ++i) {
                const float k = t->fade_left > 0 ? (float)t->fade_left / (float)t->fade_total : 0.0f;
                for (int c = 0; c < t->channels; ++c) {
                    t->buffer[i * t->channels + c] *= k;
                }
                if (t->fade_left > 0) {
                    --t->fade_left;
                }
            }
            if (t->fade_left == 0) {
                t->ended = 1;
            }
        }
        for (int i = 0; i < got * t->channels; ++i) {
            const float v = t->buffer[i] < 0.0f ? -t->buffer[i] : t->buffer[i];
            t->peak = v > t->peak ? v : t->peak;
        }
        t->fed += got;
        SDL_PutAudioStreamData(stream, t->buffer, got * frame_bytes);
        additional -= got * frame_bytes;
    }
    SDL_UnlockMutex(decoder);
}

static void track_free(Track *t)
{
    if (t == NULL) {
        return;
    }
    if (t->stream != NULL) {
        SDL_DestroyAudioStream(t->stream); /* after this the audio thread no longer feeds it */
    }
    if (t->ogg != NULL) {
        stb_vorbis_close(t->ogg);
    }
    SDL_free(t->file);
    free(t->buffer);
    free(t);
}

/* The cue's track opened and started where the cue is at `frame`; NULL when it cannot be, or when a
 * track that plays once has already ended there. */
static Track *track_start(const KhdaysMusicCue *cue, double frame)
{
    char path[1200];
    size_t size = 0;
    int error = 0;
    stb_vorbis_info info;
    long long sample;
    Track *t = (Track *)calloc(1, sizeof(Track));
    if (t == NULL) {
        return NULL;
    }
    snprintf(path, sizeof(path), "%s%s.ogg", folder, cue->track);
    snprintf(t->name, sizeof(t->name), "%s", cue->track);
    t->loop = cue->loop;
    t->file = SDL_LoadFile(path, &size);
    t->ogg = t->file != NULL && size <= 0x7fffffff
                 ? stb_vorbis_open_memory((const unsigned char *)t->file, (int)size, &error, NULL)
                 : NULL;
    if (t->ogg == NULL) {
        say("%s: the music %s cannot be read (%s)", video, path,
            t->file == NULL ? SDL_GetError() : "not an Ogg Vorbis file stb_vorbis reads");
        track_free(t);
        return NULL;
    }
    info = stb_vorbis_get_info(t->ogg);
    t->channels = info.channels;
    t->rate = (int)info.sample_rate;
    t->length = stb_vorbis_stream_length_in_samples(t->ogg);
    t->buffer = (float *)malloc((size_t)CHUNK_FRAMES * (size_t)t->channels * sizeof(float));
    sample = khdays_music_track_sample(cue, frame, schedule.rate, t->rate, t->length);
    if (t->buffer == NULL || t->channels <= 0 || sample < 0 || !stb_vorbis_seek(t->ogg, (unsigned int)sample)) {
        track_free(t);
        return NULL;
    }
    {
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, t->channels, t->rate};
        t->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, t);
    }
    if (t->stream == NULL) {
        say("%s: no sound for the music: %s", video, SDL_GetError());
        track_free(t);
        return NULL;
    }
    say("%s: the music %s from frame %d (%.1f s into it)", video, cue->track, (int)frame, (double)sample / t->rate);
    return t;
}

/* Under `control`: a finished fade freed. */
static void reclaim(void)
{
    int done = 0;
    if (leaving == NULL) {
        return;
    }
    SDL_LockMutex(lock_of(&decoder_lock));
    done = leaving->ended;
    SDL_UnlockMutex(lock_of(&decoder_lock));
    if (done) {
        track_free(leaving);
        leaving = NULL;
    }
}

/* Under `control`: the playing track fades out (a fade still going stops at once). */
static void fade_out_playing(void)
{
    long long fed;
    float peak;
    if (playing == NULL) {
        return;
    }
    track_free(leaving);
    SDL_LockMutex(lock_of(&decoder_lock));
    playing->fade_total = playing->fade_left = (int)(FADE_SECONDS * playing->rate);
    fed = playing->fed;
    peak = playing->peak;
    SDL_UnlockMutex(lock_of(&decoder_lock));
    if (stream_paused) {
        SDL_ResumeAudioStreamDevice(playing->stream); /* it fades out, whatever paused it */
        stream_paused = 0;
    }
    say("%s: the music %s fades out after %.1f s of it (its loudest %.2f)", video, playing->name,
        (double)fed / playing->rate, peak);
    leaving = playing;
    playing = NULL;
    carried = 0;
}

/* Under `control`: the playing track kept for a next video, for a while (khdays_theater_music_poll). */
static void keep_for_next(void)
{
    carried = 1;
    kept_since = SDL_GetTicks();
}

void khdays_theater_music_poll(void)
{
    SDL_Mutex *control = lock_of(&control_lock);
    SDL_LockMutex(control);
    reclaim();
    if (carried && playing != NULL && !video_open && SDL_GetTicks() - kept_since > KHDAYS_THEATER_MUSIC_WAIT_MS) {
        say("%s: no video took the music on", video);
        fade_out_playing();
    }
    SDL_UnlockMutex(control);
}

void khdays_theater_music_open(const char *video_path)
{
    SDL_Mutex *control = lock_of(&control_lock);
    const char *slash, *dot;
    char path[1200];
    SDL_LockMutex(control);
    reclaim();
    khdays_music_schedule_free(&schedule);
    slash = strrchr(video_path, '/');
    if (strrchr(video_path, '\\') > slash) {
        slash = strrchr(video_path, '\\');
    }
    slash = slash != NULL ? slash + 1 : video_path;
    dot = strrchr(slash, '.');
    snprintf(video, sizeof(video), "%.*s", dot != NULL ? (int)(dot - slash) : (int)strlen(slash), slash);
    snprintf(folder, sizeof(folder), "%.*smusic/", (int)(slash - video_path), video_path);
    snprintf(path, sizeof(path), "%s%s.txt", folder, video);
    have_schedule = khdays_music_schedule_read(path, &schedule);
    playing_cue = -1;
    say("%s: opened, %s (from %s, to %s), %s playing%s", video,
        have_schedule ? "its music schedule read" : "no music schedule",
        have_schedule && schedule.from_field ? "field" : "stop", have_schedule && schedule.to_field ? "field" : "stop",
        playing != NULL ? playing->name : "nothing", carried ? " (kept for it)" : "");
    if (playing != NULL) {
        /* the video before's music: on into this one when it takes it from there with the same track */
        if (carried && have_schedule && schedule.from_field && schedule.cues[0].first == 0 &&
            strcmp(schedule.cues[0].track, playing->name) == 0) {
            playing_cue = 0;
            carried = 0;
            say("%s: the music %s goes on from the video before", video, playing->name);
        } else if (carried && !have_schedule && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
            /* the Menu Overhaul's: a video without music of its own lets the kept music play on through it
             * (kept still: its close decides again) */
            say("%s: the music %s plays on through it", video, playing->name);
        } else {
            fade_out_playing();
        }
    }
    if (playing == NULL) {
        carried = 0;
    }
    video_open = 1;
    SDL_UnlockMutex(control);
}

void khdays_theater_music_tick(double seconds, int started, int paused)
{
    SDL_Mutex *control = lock_of(&control_lock);
    double frame;
    int cue;
    SDL_LockMutex(control);
    reclaim();
    if (!have_schedule || !started) {
        SDL_UnlockMutex(control); /* before the video starts, the music from the one before plays on */
        return;
    }
    frame = seconds * schedule.rate;
    cue = khdays_music_cue_at(&schedule, frame);
    if (cue < 0 && playing != NULL && schedule.to_field && playing_cue == schedule.cue_count - 1 &&
        frame >= (double)schedule.cues[playing_cue].first && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        /* the Menu Overhaul's: the last cue's music goes on into the next video (`to field`), past its end (a
         * frame or two before the video's), for the next video to take it from there */
        cue = playing_cue;
    }
    if (cue != playing_cue) {
        fade_out_playing();
        playing_cue = cue;
        if (cue >= 0 && !paused) {
            playing = track_start(&schedule.cues[cue], frame);
            applied_gain = -1.0f;
            stream_paused = 1; /* resumed below, once its gain is set */
        }
    }
    if (playing != NULL) {
        const float gain = base_gain() * (float)khdays_music_level_at(&schedule, frame);
        if (gain - applied_gain > 0.001f || applied_gain - gain > 0.001f) {
            SDL_SetAudioStreamGain(playing->stream, gain);
            applied_gain = gain;
        }
        if (paused != stream_paused) {
            if (paused) {
                SDL_PauseAudioStreamDevice(playing->stream);
            } else {
                SDL_ResumeAudioStreamDevice(playing->stream);
            }
            stream_paused = paused;
        }
    } else if (paused) {
        playing_cue = -1; /* a cue that comes while paused starts on the resume */
    }
    SDL_UnlockMutex(control);
}

void khdays_theater_music_close(int next_follows)
{
    SDL_Mutex *control = lock_of(&control_lock);
    int was_open;
    SDL_LockMutex(control);
    reclaim();
    was_open = video_open;
    video_open = 0;
    say("%s: closed (%s, %s), %s playing%s", video,
        next_follows == 1   ? "a video follows"
        : next_follows == 2 ? "a scene may follow"
                            : "the end",
        was_open ? "open" : "none open", playing != NULL ? playing->name : "nothing", carried ? ", kept" : "");
    if (playing != NULL) {
        const int goes_on =
            (have_schedule && schedule.to_field) || (carried && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL));
        if (next_follows == 1 && goes_on) {
            keep_for_next(); /* the next video decides (khdays_theater_music_open) */
        } else if (next_follows == 2 && goes_on) {
            keep_for_next(); /* a next scene's video may take it (else khdays_theater_music_poll ends it) */
        } else if (!was_open && carried && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
            /* no video open (the course's own close after its video's): the wait decides */
        } else {
            fade_out_playing();
        }
    }
    khdays_music_schedule_free(&schedule);
    have_schedule = 0;
    playing_cue = -1;
    SDL_UnlockMutex(control);
}

void khdays_theater_music_set_gain(float gain)
{
    int bits;
    memcpy(&bits, &gain, sizeof(bits));
    SDL_SetAtomicInt(&gain_bits, bits != 0 ? bits : 1); /* 0 means not set: a silent gain stays near 0 */
}
