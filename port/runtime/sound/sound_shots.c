/* The sounds the port starts itself (sound_shots.h): files decoded once (port/sound/replace.h), each sound heard a
 * voice of its own read between the file's samples at the output's rate, the voices under one lock the game's thread
 * and the sound's take in turn. */
#include "sound_shots.h"
#include "../core/session_log.h"
#include "../../sound/replace.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct File {
    char *path;
    KhdaysReplacement sound; /* frames 0: it could not be decoded */
} File;

typedef struct Voice {
    const KhdaysReplacement *sound;
    double position; /* in the file's frames */
    float left, right;
    int id; /* what khdays_sound_shot_play gave back */
} Voice;

static SRWLOCK lock = SRWLOCK_INIT;
static File **files; /* each where it was made: the voices keep its samples' place */
static int file_count, file_room;
static Voice *voices;
static int voice_count, voice_room;
static volatile LONG heard; /* voice_count, read without the lock */
static int next_id;

int khdays_sound_shot_file(const char *path)
{
    int found = -1, usable = 0;
    AcquireSRWLockShared(&lock);
    for (int i = 0; i < file_count && found < 0; ++i)
        if (strcmp(files[i]->path, path) == 0) {
            found = i;
            usable = files[i]->sound.frames != 0;
        }
    ReleaseSRWLockShared(&lock);
    if (found >= 0)
        return usable ? found : -1;
    File *f = (File *)calloc(1, sizeof(File));
    if (f == NULL || (f->path = _strdup(path)) == NULL) {
        free(f);
        return -1;
    }
    char why[256] = "";
    if (!khdays_replacement_load(&f->sound, path, NULL, why, (int)sizeof(why)) || f->sound.frames == 0) {
        khdays_log("sound: %s cannot be heard (%s)", path, why[0] != 0 ? why : "no samples");
        khdays_replacement_free(&f->sound);
        memset(&f->sound, 0, sizeof(f->sound));
    }
    f->sound.looped = 0; /* heard once, to its end */
    usable = f->sound.frames != 0;
    AcquireSRWLockExclusive(&lock);
    if (file_count == file_room) {
        const int room = file_room != 0 ? file_room * 2 : 16;
        File **grown = (File **)realloc(files, (size_t)room * sizeof(File *));
        if (grown == NULL) {
            ReleaseSRWLockExclusive(&lock);
            khdays_replacement_free(&f->sound);
            free(f->path);
            free(f);
            return -1;
        }
        files = grown;
        file_room = room;
    }
    files[file_count] = f;
    found = file_count++;
    ReleaseSRWLockExclusive(&lock);
    return usable ? found : -1;
}

int khdays_sound_shot_start(int file, float volume, float pan)
{
    return khdays_sound_shot_play(file, volume, pan) != 0;
}

int khdays_sound_shot_play(int file, float volume, float pan)
{
    int started = 0;
    if (pan < -1.0f)
        pan = -1.0f;
    if (pan > 1.0f)
        pan = 1.0f;
    AcquireSRWLockExclusive(&lock);
    if (file >= 0 && file < file_count && files[file]->sound.frames != 0) {
        if (voice_count == voice_room) {
            const int room = voice_room != 0 ? voice_room * 2 : 16;
            Voice *grown = (Voice *)realloc(voices, (size_t)room * sizeof(Voice));
            if (grown != NULL) {
                voices = grown;
                voice_room = room;
            }
        }
        if (voice_count < voice_room) {
            Voice *v = &voices[voice_count++];
            v->sound = &files[file]->sound;
            v->position = 0.0;
            v->left = volume * (pan > 0.0f ? 1.0f - pan : 1.0f);
            v->right = volume * (pan < 0.0f ? 1.0f + pan : 1.0f);
            if (++next_id <= 0)
                next_id = 1;
            v->id = next_id;
            started = next_id;
        }
    }
    InterlockedExchange(&heard, voice_count);
    ReleaseSRWLockExclusive(&lock);
    return started;
}

int khdays_sound_shot_heard(int id)
{
    int found = 0;
    AcquireSRWLockShared(&lock);
    for (int i = 0; i < voice_count && !found; ++i)
        found = voices[i].id == id && voices[i].position < (double)voices[i].sound->frames;
    ReleaseSRWLockShared(&lock);
    return found;
}

void khdays_sound_shot_end(int id)
{
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < voice_count; ++i)
        if (voices[i].id == id) {
            voices[i] = voices[--voice_count];
            break;
        }
    InterlockedExchange(&heard, voice_count);
    ReleaseSRWLockExclusive(&lock);
}

void khdays_sound_shots_stop(void)
{
    AcquireSRWLockExclusive(&lock);
    voice_count = 0;
    InterlockedExchange(&heard, 0);
    ReleaseSRWLockExclusive(&lock);
}

int khdays_sound_shots_playing(void)
{
    return InterlockedCompareExchange(&heard, 0, 0) != 0;
}

void khdays_sound_shots_mix(uint32_t rate, float gain, float out[2])
{
    if (rate == 0)
        return;
    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < voice_count;) {
        Voice *v = &voices[i];
        if (v->position >= (double)v->sound->frames) {
            voices[i] = voices[--voice_count];
            continue;
        }
        out[0] += gain * v->left * khdays_replacement_sample(v->sound, v->position, 0);
        out[1] += gain * v->right * khdays_replacement_sample(v->sound, v->position, 1);
        v->position += (double)v->sound->rate / (double)rate;
        ++i;
    }
    InterlockedExchange(&heard, voice_count);
    ReleaseSRWLockExclusive(&lock);
}
