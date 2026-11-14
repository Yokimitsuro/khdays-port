/* A sound a mod replaces: mods/<Mod>/sounds/<identifier>.<wav|flac|ogg>, decoded once to 16 bits with
 * its loop -- a WAV's `smpl`, else a FLAC or Ogg file's LOOPSTART and LOOPLENGTH, else the original's
 * scaled to the file's rate. Plain C (the decoders in third_party_decoders.c). */
#ifndef KHDAYS_SOUND_REPLACE_H
#define KHDAYS_SOUND_REPLACE_H

#include <stdint.h>

typedef struct KhdaysReplacement {
    uint32_t rate;
    int channels; /* 1 or 2 */
    uint64_t frames;
    int16_t *samples; /* interleaved */
    int looped;
    uint64_t loop_start; /* frames */
    uint64_t loop_end;   /* frames, past the loop's last */
} KhdaysReplacement;

/* The original's loop, for a file that gives none: in the original's
 * samples at its rate; `looped` 0 if it plays once. Looped at rate 0
 * (KHDAYS_WHOLE_FILE_LOOP): the whole file, start to end. */
typedef struct KhdaysOriginalLoop {
    uint32_t rate;
    int looped;
    uint64_t start, end;
} KhdaysOriginalLoop;

#define KHDAYS_WHOLE_FILE_LOOP {0, 1, 0, 0}

/* Decodes `path` by its extension. 0 when it cannot, `why` saying so. */
int khdays_replacement_load(KhdaysReplacement *replacement, const char *path, const KhdaysOriginalLoop *original,
                            char *why, int why_size);
void khdays_replacement_free(KhdaysReplacement *replacement);

/* The replacement's sample at `position` frames (fractional) of the time
 * the original has played, folded into the file's loop: 0 past the end of
 * one that plays once. `channel` 1 of a mono file is its only one. Cubic
 * (Hermite) between the file's samples. */
float khdays_replacement_sample(const KhdaysReplacement *replacement, double position, int channel);

/* A 16-bit WAV of `frames` frames of `channels` (1 or 2) interleaved at
 * `rate`, and, when `looped`, its loop in a `smpl` chunk as the loader
 * reads it back: the originals written out (--dump-sounds), in the form a
 * replacement takes. 0 when it cannot be written. */
int khdays_replacement_write_wav(const char *path, uint32_t rate, int channels, const int16_t *samples, uint64_t frames,
                                 int looped, uint64_t loop_start, uint64_t loop_end);

#endif
