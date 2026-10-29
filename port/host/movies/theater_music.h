/* The theater's music over an HD video (theater_music.c): its schedule's tracks (music_schedule.h)
 * decoded by stb_vorbis into an audio stream of their own, at the video's clock. The HD video drives
 * it (movie_hd_win.c): opened with the video, told its clock from the video's thread, and closed
 * with it -- or kept playing into the next video when the schedules say the music goes on. */
#ifndef KHDAYS_HOST_THEATER_MUSIC_H
#define KHDAYS_HOST_THEATER_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* A video opened from `video_path`: its schedule, music/<stem>.txt beside it, or none. The music
 * kept from the video before plays on when this one takes it from there with the same track. */
void khdays_theater_music_open(const char *video_path);

/* The video's thread, often: its clock (`seconds` of the file), whether it has started and is
 * paused. Starts, stops, moves and sets the volume of the music it says. */
void khdays_theater_music_tick(double seconds, int started, int paused);

/* The video closed. With `next_follows` 1 (another video opens at once, as a scene's videos do), music
 * that goes on into the next video keeps playing; with 2 (a scene's videos watched to their end: the
 * event's next scene, a course of its own, may open its video in a moment) it keeps playing for
 * KHDAYS_THEATER_MUSIC_WAIT_MS, faded out then unless a video took it; with 0, or when it does not go
 * on, it fades out (but see khdays_theater_music_overhaul). */
#define KHDAYS_THEATER_MUSIC_WAIT_MS 1500
void khdays_theater_music_close(int next_follows);

/* The window's thread, once a frame: music kept for a next video that none took within
 * KHDAYS_THEATER_MUSIC_WAIT_MS fades out. */
void khdays_theater_music_poll(void);

/* Where its lines go besides the console (the session log: host/app/window.c); NULL none. */
void khdays_theater_music_log_to(void (*log)(const char *line));

/* The Menu Overhaul's ways with the music, on when its mod is installed (host/app/window.c says so): kept
 * music plays on through a video with no schedule of its own and waits after a course's own close, and a
 * last cue that goes on into the next video plays past its end. Off unless set (the mod studio, which plays
 * the videos too, leaves them off). */
void khdays_theater_music_overhaul(int on);

/* The music's volume (0-1): the master volume times the music's. Any thread. */
void khdays_theater_music_set_gain(float gain);

#ifdef __cplusplus
}
#endif

#endif
