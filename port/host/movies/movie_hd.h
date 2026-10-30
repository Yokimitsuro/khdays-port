/* The player's HD video, shown in place of one of the game's movies: one at a time, decoded by the
 * system on a thread of its own (Media Foundation, movie_hd_win.c), its sound on an audio stream of its
 * own whose progress is the video's clock. When one plays is the game's side's (runtime/movies). */
#ifndef KHDAYS_HOST_MOVIE_HD_H
#define KHDAYS_HOST_MOVIE_HD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysHdInfo {
    int width, height;  /* the picture shown (the file's display area) */
    double fps;         /* the file's frame rate, 0 when it does not say */
    int channels, rate; /* its sound, 0 when it has none */
} KhdaysHdInfo;

/* Opens `path` and decodes its first picture. Returns 1, or 0 with the reason
 * in `why`. A video already open is closed first. The new video then waits,
 * showing its first picture, until khdays_hd_start. */
int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size);

/* The same for a part of the file: from `from` seconds of it (0 its start) to `until` (0 its end),
 * as if the file began and ended there; its clock reads the file's own time. */
int khdays_hd_open_part(const char *path, double from, double until, KhdaysHdInfo *info, char *why, size_t size);

/* Fades for the next video opened only: in from black and silence over `in` seconds, out over the
 * last `out` seconds (of its part); 0 for none. */
void khdays_hd_fades(double in, double out);

/* Starts the video: its sound starts, and its clock with it. */
void khdays_hd_start(void);

/* Pauses or resumes the picture and the sound (the settings menu pauses them). */
void khdays_hd_pause(int pause);

/* Stops the picture and the sound at once and frees everything; the
 * presenter keeps the last picture it took. Does nothing if no video is open. */
void khdays_hd_close(void);

/* The same at the end of a scene's videos watched to their end: the theater's music that goes on into
 * the next video keeps playing a little while, for the next scene's video to take it from there (the
 * event's next scene begins at once); unless one does, it fades out (theater_music.h). */
void khdays_hd_close_scene_end(void);

/* The video's state: 0 while it plays (or waits, or is paused); 1 once it has
 * ended (its last picture is due and its sound has played); -1 if it failed
 * part-way (with the reason in `why`); 2 if no video is open. */
int khdays_hd_status(char *why, size_t size);

/* Sets the sound's volume (0-1): the master volume times the movies' volume. */
void khdays_hd_set_gain(float gain);

/* Called by the host's thread at each screen refresh. If a picture newer than
 * the last one taken is due on the video's clock, returns 1 and its planes
 * (NV12: the luma, then the interleaved chroma at half height, both `pitch`
 * bytes per row). They stay valid, and the video waits, until
 * khdays_hd_picture_done is called. Otherwise returns 0. */
typedef struct KhdaysHdPicture {
    const uint8_t *luma, *chroma;
    int pitch, width, height;
    double time; /* its presentation time in the file, in seconds */
    double fps;  /* the file's frame rate, 0 when it does not say */
    float level; /* how much of it shows through the video's fades (khdays_hd_fades): 1 all, 0 black */
} KhdaysHdPicture;
int khdays_hd_picture_begin(KhdaysHdPicture *picture);
void khdays_hd_picture_done(void);

/* The picture taken last, as 8-bit RGBA rows of `*w` x `*h` (BT.709, limited range, as the window
 * shows it), copied into `*rgba` (grown as needed with SDL_realloc; the caller's to keep). Returns 0
 * when no picture has been taken. For a paused video shown under the game's pause. */
int khdays_hd_picture_rgba(uint8_t **rgba, size_t *capacity, int *w, int *h);

/* A voice heard over the game, apart from the video: the sound alone of the files `paths` (UTF-8),
 * one after another, decoded on a thread of its own into an audio stream of its own; their pictures
 * are never read. A voice still heard is stopped first. */
void khdays_hd_voice_play(const char *const *paths, int count);

/* Stops the voice at once; does nothing if none is heard. */
void khdays_hd_voice_stop(void);

/* Pauses or resumes the voice (the settings menu pauses it). */
void khdays_hd_voice_pause(int pause);

/* Sets the voice's volume (0-1): the master volume times the voices' volume. */
void khdays_hd_voice_set_gain(float gain);

/* The voice's clock, for its subtitles: the video whose sound is heard now (its name, "hd007b") and
 * how far into it (seconds). 0 when no voice is heard. Any thread. */
int khdays_hd_voice_clock(char *name, size_t size, double *seconds);

/* Whether the voice still speaks (a moment's silence after its last word counted in). Any thread. */
int khdays_hd_voice_speaking(void);

#ifdef __cplusplus
}
#endif

#endif
