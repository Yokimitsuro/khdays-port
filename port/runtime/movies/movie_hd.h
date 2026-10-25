/* HD movies and scenes: with Video `hd` and the video there (data/cutscenes/cinematics/hd<id>.mp4),
 * the window shows it with its sound while the game plays its own movie unseen and silent, and holds
 * the game until it ends. START pauses it where the DS lets the movie be skipped; with the Menu Overhaul
 * (runtime/mods/installed.h) it opens that mod's panel instead: Continue and Skip. */
#ifndef KHDAYS_MOVIE_HD_H
#define KHDAYS_MOVIE_HD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Menu Overhaul's pause panel: 1 while open, its row (0 Continue, 1 Skip), whether Skip is offered; opened
 * or closed; a row chosen (the mouse), and taken. */
int khdays_movie_pause_menu(int *row, int *can_skip);
void khdays_movie_pause_toggle(void);
void khdays_movie_pause_focus(int row, int activate);

/* The game's thread. A stream of a movie opens (Ov024_MobiClip_OpenStreamSlot,
 * the path as the game built it); `replaced` when a mod's movie takes it. */
void khdays_movie_hd_stream(int slot, const char *path, int replaced);

/* The game's movie stops (Ov024_MobiClip_StopPlayback). */
void khdays_movie_hd_stopped(void);

/* The game's thread: whether the last of the game's movies was shown as the player's own HD video
 * `stem` (data/cutscenes/cinematics/<stem>.mp4), for a scene that video shows too. */
int khdays_movie_hd_last_shown(const char *stem);

/* The game's reset (start.c): an HD video showing is closed, the game shown
 * and heard again, its clock let go. */
void khdays_movie_hd_reset(void);

/* The credits scene, ov011: 1 it begins (Ov011_CreateScene), 2 its staff roll ends
 * (Ov011_StartTitleExit), 0 it ends. The HD video carries the roll too, so the game runs its roll
 * behind the video and is held where the roll ends if the video still plays. */
void khdays_movie_hd_credits(int phase);

/* The movie's loop, each turn (Ov024_MobiClip_UpdatePlayback's player flags,
 * Ov012_RunOpeningScene's with bit 3 set): bit 3 the DS lets START skip it,
 * bit 2 it has been skipped. */
void khdays_movie_hd_flags(unsigned flags);

/* The DS buttons the game is given, from those pressed (the player's and a
 * script's): none of them while a course runs -- only START while the port
 * skips the game's movie -- and, after it, none of those still held from it
 * until they are let go. A START pressed during a course asks for the skip (not with the Menu Overhaul,
 * whose panel takes the window's presses). */
uint16_t khdays_movie_hd_game_buttons(uint16_t buttons);

/* Either thread: whether the window shows the HD video. */
int khdays_movie_hd_showing(void);
/* Either thread: whether an HD video shows or a scene's is about to (the game fading out first). */
int khdays_movie_hd_running(void);
/* Any thread: how much of the game's picture and sound shows, 1 to 0, as it fades around a scene's
 * videos. */
float khdays_movie_hd_game_level(void);
/* Either thread: whether the game's own movie plays (a stream opened, its playback not stopped). */
int khdays_movie_game_playing(void);

/* The host's thread, each refresh while a course runs: the video's end or
 * failure. */
void khdays_movie_hd_poll(void);

/* The host's thread: the player's DS buttons now (while the game is held its
 * thread reads none). */
void khdays_movie_hd_host_buttons(uint16_t buttons);
/* The same with `taps` (buttons pressed since the last call, even if let go again), and over a day's
 * monologue as well (`monologue`: one runs, its START opening the Menu Overhaul's panel). */
void khdays_media_host_presses(uint16_t buttons, uint16_t taps, int monologue);
/* A day's monologue's pause (the panel above, Continue and Skip): the game held, the voice paused. The
 * window's thread. */
void khdays_monologue_pause_toggle(void);
/* The game's thread: Skip was chosen in it (taken once). */
int khdays_monologue_skip_taken(void);
/* Whether the pause panel just closed by a choice made (Continue, or Skip) rather than by
 * backing out; told once. */
int khdays_movie_pause_closed_by_choice(void);
/* Whether the theater's subtitles of the video `stem` ("hd000") exist in the game's language. */
int khdays_movie_hd_subtitles_exist(const char *stem);

/* The pause START opens over a movie's HD video (a copy of the DS's scene pause: Continue, Skip).
 * Whether it is open, the entry chosen (0 Continue, 1 Skip), its blink (1 or 2) and the game's
 * language (1 English to 5 Spanish), for the window to draw it (host/screens/movie_pause.h). */
int khdays_movie_hd_movie_pause(int *selected, int *lit, int *language);

/* Whether a pause holds the video (so closing the settings menu does not resume it). */
int khdays_movie_hd_course_paused(void);

/* The theater's HD scenes. The game's thread, as an event's scene begins its skippable stretch
 * (`state` the event machine's reference): with Video `hd`, the videos `stems` play in a row in its
 * place (a mod's cutscenes/cinematics/<stem>.mp4 first), each from `from` to `until` seconds (NULL or
 * 0: the whole video), the game held and faded out around them. Returns 0 when the scene plays as the
 * game's (no Video `hd`, or a video missing: logged once). */
int khdays_movie_hd_scene(const char *key, uint32_t state, char *const *stems, const double *from, const double *until,
                          int count);

/* The game's thread, at each step of a script machine (`state` its reference): once the scene's videos
 * have ended, the game's own skip is asked there (the sound effects stopped, the skip request set),
 * as the event pause's Skip does. */
void khdays_movie_hd_scene_step(uint32_t state, void *host_state);

/* The game's thread: the scene's stretch has ended and the game shows again. Returns 1 when the
 * port's own skip ended it (its videos over), 0 for the player's Skip. */
int khdays_movie_hd_scene_ended(void);

/* The game's thread, each frame, after START paused a scene's videos and let the game open its pause:
 * `pause_open` (PauseMenu_GetMode) and `skip_requested` (the event's skip). Skip lets the game run to
 * the scene's end; Continue holds the game again and the video goes on. */
void khdays_movie_hd_scene_frame(int pause_open, int skip_requested);

/* Whether the game's pause is over a scene's paused video (the window draws the pause over it). */
int khdays_movie_hd_scene_paused(void);

/* The event machine's state (a reference) of the scene whose videos are shown, 0 when none. */
uint32_t khdays_movie_hd_scene_state(void);

/* A Theater chapter's videos in a row (host/screens/cine_screen.c), the game's list held under them,
 * as a scene's are; START or their end back to the list. Returns 1 when they start, 0 when not (no
 * Video `hd`, a video missing). */
int khdays_movie_hd_chapter(const char *key, char *const *stems, int count);

/* A scene video's file: a mod's cutscenes/cinematics/<stem>.mp4 first, then the player's own. */
void khdays_movie_hd_scene_file(const char *stem, char *file, size_t size);

/* The game's thread, as a day's monologue begins (scene 10): with Video `hd`, the sound of the videos
 * `stems` in a row (the theater's voice-only clips; their pictures are black) heard over the game's
 * own page, the game going on as on the DS. Returns 1 when the voice starts, 0 when there is none (no
 * Video `hd`, a video missing: said in the log once). */
int khdays_movie_hd_voice(const char *key, char *const *stems, int count);

/* The game's thread: the monologue has ended; its voice stops if it still speaks. */
void khdays_movie_hd_voice_stop(void);

#ifdef __cplusplus
}
#endif

#endif
