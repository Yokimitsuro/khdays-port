/* The sound output (sound_output.h): the sound thread runs the sound bridge up to the DS clock every
 * millisecond and puts what is made into the SDL stream (the PC's mixer's floats, or the DS's 16-bit
 * samples with `ds_exact`), keeping a lead ahead as silence where it opens, resumes or runs dry, and
 * dropping what queues past a tenth of a second (docs/PLATFORM.md, "The PC's side"). */
#include "sound_output.h"
#include "host.h"
#include "../movies/movie_hd.h"
#include "../movies/theater_music.h"
#include "profile.h"
#include "../../runtime/core/clock.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/sound/sound.h"
#include "../../runtime/sound/sound_bridge.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define LEAD_MS 30 /* how much sound the stream keeps ahead of the device */

static void *audio;              /* the SDL_AudioStream (SDL_GetAtomicPointer, SDL_SetAtomicPointer) */
static SDL_AtomicInt paused;     /* the settings menu paused the device */
static SDL_AtomicInt muted;      /* an HD video plays (khdays_sound_output_mute) */
static float level_set = 1.0f;   /* the game's level around a scene's videos, as last applied */
static SDL_AtomicInt needs_lead; /* the next samples go in after LEAD_MS of silence */
static SDL_AtomicInt dry;        /* times the stream ran dry since the last report */
static int device_rate;          /* the device's own rate, for the PC's mixer */

/* The format the stream takes now. It is changed by the sound thread, or by
 * the game's thread when it catches the sound up; either way under the
 * bridge's lock. */
static SDL_AudioSpec input = {SDL_AUDIO_S16, 2, KHDAYS_SOUND_RATE};

/* The bytes of one stereo frame in the current format. */
static int frame_bytes(void)
{
    return input.format == SDL_AUDIO_F32 ? 8 : 4;
}

/* The stream's gain: 0 while muted, else the game's fade level around a scene's videos. */
static void apply_gain(SDL_AudioStream *stream)
{
    level_set = khdays_movie_hd_game_level();
    SDL_SetAudioStreamGain(stream, SDL_GetAtomicInt(&muted) ? 0.0f : level_set);
}

/* Puts LEAD_MS of silence into the stream. */
static void put_lead(SDL_AudioStream *stream)
{
    static const float silence[2 * 48000 * LEAD_MS / 1000] = {0}; /* zeros in either format */
    int bytes = input.freq * LEAD_MS / 1000 * frame_bytes();
    while (bytes > 0) { /* in pieces, at whatever rate the device has */
        const int piece = bytes < (int)sizeof(silence) ? bytes : (int)sizeof(silence);
        SDL_PutAudioStreamData(stream, silence, piece);
        bytes -= piece;
    }
}

/* Puts `count` frames of sound into the stream, switching its format first
 * if it changed, and keeping the lead (see the top of this file). */
static void put(const void *frames, int count, SDL_AudioFormat format, int rate)
{
    SDL_AudioStream *stream = (SDL_AudioStream *)SDL_GetAtomicPointer(&audio);
    int queued;
    if (stream == NULL) {
        return;
    }
    if (input.format != format || input.freq != rate) {
        input.format = format;
        input.freq = rate;
        SDL_SetAudioStreamFormat(stream, &input, NULL);
    }
    queued = SDL_GetAudioStreamQueued(stream);
    if (SDL_GetAtomicInt(&needs_lead)) {
        SDL_SetAtomicInt(&needs_lead, 0);
        put_lead(stream);
    } else if (queued == 0 && !SDL_GetAtomicInt(&paused)) {
        SDL_AddAtomicInt(&dry, 1);
        put_lead(stream);
    } else if (queued > input.freq * frame_bytes() / 10) {
        return; /* more than a tenth of a second is already waiting: drop this */
    }
    SDL_PutAudioStreamData(stream, frames, count * frame_bytes());
}

void khdays_host_audio(const int16_t *frames, int count)
{
    put(frames, count, SDL_AUDIO_S16, KHDAYS_SOUND_RATE);
}

void khdays_host_audio_float(const float *frames, int count)
{
    put(frames, count, SDL_AUDIO_F32, device_rate);
}

void khdays_sound_output_open(void)
{
    SDL_AudioSpec device;
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &input, NULL, NULL);
    if (stream == NULL) {
        fprintf(stderr, "host: no sound: %s\n", SDL_GetError());
        return;
    }
    device_rate =
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &device, NULL) && device.freq > 0 ? device.freq : 0;
    SDL_SetAtomicInt(&needs_lead, 1);
    apply_gain(stream);
    SDL_ResumeAudioStreamDevice(stream);
    SDL_SetAtomicPointer(&audio, stream);
}

void khdays_sound_output_set(const KhdaysSound *settings)
{
    /* Without the device's rate, use the DS's mixer (the stream converts it). */
    khdays_sound_bridge_set_output(settings->ds_exact || device_rate == 0, device_rate);
    khdays_sound_bridge_set_volumes(settings->volume, settings->music, settings->effects, settings->voices,
                                    settings->movies);
    /* an HD video's volume is the master volume times the movies' volume */
    khdays_hd_set_gain((float)settings->volume / 100.0f * (float)settings->movies / 100.0f);
    /* a monologue's voice (the theater's) is a voice: the master volume times the voices' */
    khdays_hd_voice_set_gain((float)settings->volume / 100.0f * (float)settings->voices / 100.0f);
    /* the theater's music over an HD video is music: the master volume times the music's */
    khdays_theater_music_set_gain((float)settings->volume / 100.0f * (float)settings->music / 100.0f);
}

void khdays_sound_output_mute(int mute)
{
    SDL_AudioStream *stream = (SDL_AudioStream *)SDL_GetAtomicPointer(&audio);
    const int was = SDL_GetAtomicInt(&muted);
    SDL_SetAtomicInt(&muted, mute != 0);
    if (stream != NULL) {
        if (was && !mute) {
            /* what waits in the stream was made under the video, unheard (the music left playing then):
             * dropped, the game's sound goes on from now after a lead of silence */
            SDL_ClearAudioStream(stream);
            SDL_SetAtomicInt(&needs_lead, 1);
        }
        apply_gain(stream);
    }
}

void khdays_sound_output_close(void)
{
    SDL_AudioStream *stream = (SDL_AudioStream *)SDL_SetAtomicPointer(&audio, NULL);
    if (stream != NULL) {
        SDL_DestroyAudioStream(stream);
    }
}

void khdays_sound_output_pause(int pause)
{
    SDL_AudioStream *stream = (SDL_AudioStream *)SDL_GetAtomicPointer(&audio);
    SDL_SetAtomicInt(&paused, pause);
    if (stream == NULL) {
        return;
    }
    if (pause) {
        SDL_PauseAudioStreamDevice(stream);
    } else {
        SDL_SetAtomicInt(&needs_lead, SDL_GetAudioStreamQueued(stream) == 0);
        SDL_ResumeAudioStreamDevice(stream);
    }
}

/* The sound thread: runs the sound bridge every millisecond, and with
 * KHDAYS_PROFILE reports once a second. */
static int sound_thread(void *unused)
{
    const int profile = getenv("KHDAYS_PROFILE") != NULL;
    Uint64 report_at = SDL_GetTicks() + 1000;
    (void)unused;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    for (;;) {
        SDL_Delay(1);
        khdays_sound_bridge_advance(khdays_clock_cycles());
        if (khdays_movie_hd_game_level() != level_set) {
            SDL_AudioStream *stream = (SDL_AudioStream *)SDL_GetAtomicPointer(&audio);
            if (stream != NULL) {
                apply_gain(stream);
            }
        }
        if (profile && SDL_GetTicks() >= report_at) {
            SDL_AudioStream *stream = (SDL_AudioStream *)SDL_GetAtomicPointer(&audio);
            const int queued = stream != NULL ? SDL_GetAudioStreamQueued(stream) : 0;
            fprintf(stderr,
                    "sound: %d times ran dry, %d ms queued, %.2f ms catching the driver up "
                    "on the game's thread, %.2f ms in the driver, in the last second\n",
                    SDL_SetAtomicInt(&dry, 0), queued * 1000 / (input.freq * frame_bytes()),
                    khdays_sound_bridge_take_catchup_ms(), khdays_sound_bridge_take_driver_ms());
            report_at += 1000;
        }
    }
}

void khdays_host_sound_start(void)
{
    SDL_Thread *thread = SDL_CreateThread(sound_thread, "sound", NULL);
    if (thread == NULL) {
        fprintf(stderr, "host: no sound thread: %s\n", SDL_GetError());
        return;
    }
    SDL_DetachThread(thread);
}
