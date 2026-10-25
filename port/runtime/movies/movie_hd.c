/* An HD movie or scene played in place of the game's (movie_hd.h), as the states below. The game's
 * thread brings the movie's events, the window's thread the video's and START; one lock keeps them in
 * order (docs/PLATFORM.md, "HD movies"). */
#include "movie_hd.h"
#include "scenes.h"

#include "../core/clock.h"
#include "../input/input.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../../host/app/host.h"
#include "../../host/movies/movie_hd.h"
#include "../../host/movies/movie_subtitles.h"
#include "../../host/app/sound_output.h"
#include "../sound/sound_bridge.h"
#include "../mods/installed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern int GetLanguage(void);     /* the game's: gLangPath's language */
int khdays_language_wanted(void); /* language.c */

enum {
    IDLE,
    PLAYING,       /* a movie's video plays */
    HELD,          /* the game's movie ended first: held until the video ends */
    FINISHING,     /* the video ended first */
    SKIPPING,      /* skipped */
    SCENE_FADE,    /* a scene's video is open; the game fades to black and silence first */
    SCENE,         /* a scene's videos play, the game held */
    SCENE_SKIP,    /* the videos are over: the game takes its own skip */
    SCENE_LANDING, /* that skip asked, until the scene's stretch ends */
    SCENE_PAUSE,   /* START over a scene: the game opens its own pause over the video */
    MOVIE_PAUSE    /* START over a movie: the port's copy of the DS's scene pause, the game held */
};

static SRWLOCK lock = SRWLOCK_INIT;
static volatile LONG state = IDLE;    /* read without the lock to see whether a course runs */
static char id[64];                   /* the movie's, as the game names it */
static int skippable = -1;            /* the DS lets START skip it: 1, 0, -1 not said yet */
static int skip_wanted;               /* START came before the movie said */
static int skipped;                   /* the game marked its skip */
static uint16_t game_last, host_last; /* the buttons last seen, for START's press */
static volatile LONG ended;           /* a course has just ended: the buttons held then are withheld */

/* state and ended read as they are written: atomically (the decoder's thread
 * and the game's both look; volatile alone is atomic only by MSVC's reading) */
static LONG state_now(void)
{
    return InterlockedCompareExchange(&state, 0, 0);
}

static LONG ended_now(void)
{
    return InterlockedCompareExchange(&ended, 0, 0);
}
static uint16_t stuck;     /* ... until they are let go */
/* The Menu Overhaul's pause panel (runtime/mods/installed.h; host/screens/modern_pause.c draws it) in place of
 * the DS's pause: Continue and Skip (at once: no question). Also over a day's monologue, whose START otherwise
 * leaves it at once: the game held, the voice paused; Skip leaves it as START does (khdays_monologue_skip_taken). */
static volatile LONG menu_open;
static int menu_row;
static int menu_closed_by_choice; /* the panel just closed by a choice made (Continue, Skip), not by backing out */
static int mono_menu;
static volatile LONG mono_skip;

static int overhaul(void)
{
    return khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL);
}

int khdays_movie_pause_menu(int *row, int *can_skip)
{
    int open;
    AcquireSRWLockShared(&lock);
    open = menu_open;
    *row = menu_row;
    *can_skip = mono_menu || skippable != 0;
    ReleaseSRWLockShared(&lock);
    return open;
}

static void resume_menu(void)
{
    if (menu_open) {
        khdays_log("%s %s: pause closed", mono_menu ? "monologue" : "movie", mono_menu ? "" : id);
    }
    menu_open = 0;
    menu_row = 0;
    if (mono_menu) {
        khdays_hd_voice_pause(khdays_clock_held(KHDAYS_HOLD_SETTINGS));
        mono_menu = 0;
    } else {
        khdays_hd_pause(khdays_clock_held(KHDAYS_HOLD_SETTINGS));
    }
    khdays_clock_hold(KHDAYS_HOLD_MOVIE_MENU, 0);
}

static void toggle_menu(void)
{
    if (menu_open) {
        resume_menu();
    } else if (overhaul() &&
               (state_now() == PLAYING || state_now() == HELD || state_now() == FINISHING || state_now() == SCENE)) {
        menu_open = 1;
        menu_row = 0;
        khdays_clock_hold(KHDAYS_HOLD_MOVIE_MENU, 1);
        khdays_hd_pause(1);
        khdays_log("movie %s: pause opened (Continue selected)", id);
    }
}

void khdays_movie_pause_toggle(void)
{
    AcquireSRWLockExclusive(&lock);
    toggle_menu();
    ReleaseSRWLockExclusive(&lock);
}

static void toggle_monologue_menu(void)
{
    if (menu_open) {
        resume_menu();
    } else if (overhaul() && state_now() == IDLE) {
        menu_open = 1;
        mono_menu = 1;
        menu_row = 0;
        khdays_clock_hold(KHDAYS_HOLD_MOVIE_MENU, 1);
        khdays_hd_voice_pause(1);
        khdays_log("monologue: pause opened (Continue selected)");
    }
}

void khdays_monologue_pause_toggle(void)
{
    AcquireSRWLockExclusive(&lock);
    toggle_monologue_menu();
    ReleaseSRWLockExclusive(&lock);
}

int khdays_monologue_skip_taken(void)
{
    return InterlockedExchange(&mono_skip, 0) != 0;
}
/* SCENE_PAUSE: START given to open the game's pause; the buttons held then; the pause seen open;
 * the frames since START */
static int pause_start_given, pause_seen, pause_frames, pause_press;
static uint16_t pause_stuck;
#define PAUSE_WAIT_FRAMES 90 /* a pause the game does not open within this: the video goes on */
#define FADE_SECONDS 0.5     /* the game and the HD videos go down and come up over this */
static int credits_scene;    /* the credits scene runs (khdays_movie_hd_credits) */
static int course_credits;   /* the course is its movie's */
/* The player's own HD video (its stem) shown for the last of the game's movies; "" when that movie
 * was the game's, or a mod's video. */
static char shown[64];

/* The ids already told about (no video, or one that cannot be played): once each. */
static char (*told)[64];
static int told_count, told_capacity;

static int tell_once(const char *movie)
{
    for (int i = 0; i < told_count; ++i) {
        if (strcmp(told[i], movie) == 0) {
            return 0;
        }
    }
    if (told_count == told_capacity) {
        const int capacity = told_capacity ? told_capacity * 2 : 16;
        char(*grown)[64] = realloc(told, (size_t)capacity * sizeof(*told));
        if (grown == NULL) {
            return 1;
        }
        told = grown;
        told_capacity = capacity;
    }
    snprintf(told[told_count++], sizeof(told[0]), "%s", movie);
    return 1;
}

/* The movie's id: the path's file name before its first dot ("/mv/837.mods" -> "837", "843.z" ->
 * "843"). */
static int movie_id(const char *path, char *out, size_t size)
{
    const char *base = path, *dot;
    size_t n;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    dot = strchr(base, '.');
    n = dot != NULL ? (size_t)(dot - base) : strlen(base);
    if (n == 0 || n >= size) {
        return 0;
    }
    memcpy(out, base, n);
    out[n] = '\0';
    return 1;
}

static const char *skippable_text(void)
{
    return skippable > 0 ? "skippable" : skippable == 0 ? "not skippable" : "not said";
}

/* Back to the game: shown, heard, its clock let go. */
static void finish(const char *how)
{
    menu_open = menu_row = 0;
    khdays_clock_hold(KHDAYS_HOLD_MOVIE_MENU, 0);
    khdays_hd_close();
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    khdays_sound_output_mute(0);
    khdays_sound_bridge_hold_new_music(0);
    khdays_log("movie %s: the HD video %s (the game's movie %s); the game goes on", id, how, skippable_text());
    fprintf(stderr, "movies: %s: the HD video %s; the game goes on\n", id, how);
    InterlockedExchange(&ended, 1);
    InterlockedExchange(&state, IDLE);
}

/* The game's own movie plays: from a stream's opening to the playback's stop (the hooks below), HD
 * video or not -- for the one screen's situation (ui/situation_decide.c). */
static volatile LONG game_movie;

void khdays_movie_hd_reset(void)
{
    InterlockedExchange(&game_movie, 0);
    shown[0] = '\0';
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        finish("stopped by the game's reset");
    }
    ReleaseSRWLockExclusive(&lock);
}

static void scene_videos_over(const char *how);
static int scene_chapter; /* the course is a Theater chapter's (no event: it ends with its videos) */
static int scene_player_skip; /* the player took the game's pause's Skip (not the port, its videos over) */
static void scene_pause_begin(void);

static void begin_skip(void)
{
    khdays_hd_close(); /* the picture and the sound stop at once; the window keeps the last picture */
    skipped = 0;
    InterlockedExchange(&state, SKIPPING);
    khdays_log("movie %s: the HD video skipped; START given to the game for its movie", id);
}

/* The movie skipped where the DS lets START skip it (its pause's Skip, or START itself). */
static void movie_skip(void)
{
    switch (state_now()) {
    case PLAYING:
        if (skippable > 0) {
            begin_skip();
        } else if (skippable < 0) {
            skip_wanted = 1; /* decided when the movie says (khdays_movie_hd_flags) */
        }
        break;
    case FINISHING:
        if (skippable > 0) {
            skipped = 0;
            InterlockedExchange(&state, SKIPPING);
        }
        break;
    case HELD:
        if (skippable != 0) {
            finish("skipped");
        }
        break;
    default: break;
    }
}

/* The pause over a movie's video (MOVIE_PAUSE) */
static LONG pause_from;                   /* the state it paused */
static int pause_selected;                /* 0 Continue, 1 Skip */
static int pause_language;                /* the game's, as it opened */
static int pause_fresh;                   /* the START that opened it is still held */
static volatile double pause_lit_from;    /* when the entry began to blink */
#define PAUSE_BLINK_SECONDS (16.0 / 60.0) /* Input_DebounceSlot: 16 frames each look */
static double seconds_now(void);

/* Under the lock: START over a movie's video (Playing or Held) pauses it under the port's pause. */
static void movie_pause_begin(void)
{
    pause_from = state_now();
    pause_selected = 0;
    pause_fresh = 1;
    pause_lit_from = seconds_now();
    pause_language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    khdays_hd_pause(1);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1); /* the game's movie waits with it (held already when Held) */
    InterlockedExchange(&state, MOVIE_PAUSE);
    khdays_log("movie %s: START: the HD video paused under the pause", id);
}

/* Under the lock: the pause closes: Continue, the video and the game going on as they were; or Skip. */
static void movie_pause_end(int skip)
{
    if (pause_from == PLAYING) {
        khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    }
    game_last = 0xffff; /* the button that closed it is no press of the game's (it would open it again) */
    InterlockedExchange(&state, pause_from);
    if (skip) {
        khdays_log("movie %s: the pause's Skip", id);
        movie_skip();
    }
    if (state_now() == PLAYING || state_now() == HELD) {
        khdays_hd_pause(0);
        if (!skip) {
            khdays_log("movie %s: the pause's Continue; the HD video goes on", id);
        }
    }
}

static void request_skip(void)
{
    switch (state_now()) {
    case PLAYING:
    case HELD:
        if (course_credits || skippable == 0) {
            movie_skip(); /* the credits' roll as on the DS; none where the DS lets none */
        } else {
            movie_pause_begin();
        }
        break;
    case FINISHING: movie_skip(); break; /* its video already over: no pause over it */
    case SCENE:
        if (scene_chapter) {
            scene_videos_over("stopped by START");
        } else {
            scene_pause_begin();
        }
        break;
    default: break;
    }
}

/* A takes the row lit: Continue resumes, Skip skips at once (no question: a scene or a monologue is seen again
 * from the theater); START and B always resume. */
static void menu_activate(void)
{
    const int monologue = mono_menu, skip = menu_row == 1;
    if (skip && !(monologue || skippable != 0)) {
        return; /* (Skip not offered here: drawn faint) */
    }
    resume_menu(); /* (its row back to Continue for the next opening) */
    menu_closed_by_choice = 1;
    if (!skip) {
        return;
    }
    if (monologue) {
        InterlockedExchange(&mono_skip, 1);
        khdays_log("monologue: Skip chosen; it is left as its START leaves it");
    } else if (state_now() == SCENE) {
        scene_videos_over("skipped in the pause menu");
    } else {
        movie_skip(); /* at once: request_skip would open the DS's pause over it */
    }
}

void khdays_movie_pause_focus(int row, int activate)
{
    AcquireSRWLockExclusive(&lock);
    if (menu_open && row >= 0 && row <= 1) {
        menu_row = row;
        if (activate)
            menu_activate();
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_movie_game_playing(void)
{
    return InterlockedCompareExchange(&game_movie, 0, 0) != 0;
}

int khdays_movie_hd_last_shown(const char *stem)
{
    return shown[0] != '\0' && strcmp(shown, stem) == 0;
}

/* The HD videos played in a row in place of a movie: a mod's hd<id>, or the player's as the scene
 * map's mv#<id> says (hd<id> whole without an entry), each with its subtitles' id and its part. */
typedef struct MoviePart {
    char file[1024], stem[64], subtitles[64];
    double from, until;
} MoviePart;
static MoviePart *parts;
static int part_count, part_next;

/* The movie's parts (parts, part_count); 0 when it plays as the game's (the map's `off`). */
static int movie_parts(const char *movie)
{
    char relative[64], key[80];
    const KhdaysSceneEntry *e;
    MoviePart *grown;
    snprintf(key, sizeof(key), "mv#%s", movie);
    e = khdays_scene_map_find(khdays_scenes_map(), key);
    snprintf(relative, sizeof(relative), "movies/hd%s.mp4", movie);
    grown =
        (MoviePart *)realloc(parts, (size_t)(e != NULL && e->stem_count > 1 ? e->stem_count : 1) * sizeof(MoviePart));
    if (grown == NULL) {
        return 0;
    }
    parts = grown;
    memset(&parts[0], 0, sizeof(parts[0]));
    part_count = 1;
    snprintf(parts[0].subtitles, sizeof(parts[0].subtitles), "%s", movie);
    if (khdays_mods_find(relative, parts[0].file, (int)sizeof(parts[0].file))) {
        return 1; /* a mod's: whole, and no video of the player's shown */
    }
    if (e != NULL && e->off) {
        return 0;
    }
    if (e == NULL || e->stem_count == 0) {
        snprintf(parts[0].stem, sizeof(parts[0].stem), "hd%s", movie);
        snprintf(parts[0].file, sizeof(parts[0].file), "data/cutscenes/cinematics/%s.mp4", parts[0].stem);
        return 1;
    }
    part_count = e->stem_count;
    for (int i = 0; i < part_count; ++i) {
        MoviePart *p = &parts[i];
        const char *stem = e->stems[i];
        memset(p, 0, sizeof(*p));
        snprintf(p->stem, sizeof(p->stem), "%s", stem);
        snprintf(p->file, sizeof(p->file), "data/cutscenes/cinematics/%s.mp4", stem);
        snprintf(p->subtitles, sizeof(p->subtitles), "%s", strncmp(stem, "hd", 2) == 0 ? stem + 2 : stem);
        p->from = e->from != NULL ? e->from[i] : 0.0;
        p->until = e->until != NULL ? e->until[i] : 0.0;
    }
    return 1;
}

/* Opens part `index` (Media Foundation takes its time: never under the lock). */
static int movie_part_open(int index)
{
    const MoviePart *p = &parts[index];
    KhdaysHdInfo info;
    char why[320];
    /* as a scene's: the first comes up from black and silence, the last goes down to them (a part cut
     * out of a longer video ends so, not mid-picture) */
    khdays_hd_fades(index == 0 ? FADE_SECONDS : 0.0, index == part_count - 1 ? FADE_SECONDS : 0.0);
    if (!khdays_hd_open_part(p->file, p->from, p->until, &info, why, sizeof(why))) {
        if (tell_once(p->file)) {
            khdays_log("movie %s: %s cannot be played (%s)", id, p->file, why);
            fprintf(stderr, "movies: %s cannot be played (%s)\n", p->file, why);
        }
        return 0;
    }
    khdays_log("movie %s: the HD video %s plays (%dx%d, %.3f frames a second, %s)", id, p->file, info.width,
               info.height, info.fps, info.channels > 0 ? "with its sound" : "no sound");
    if (p->until > 0.0) {
        khdays_log("movie %s: %s from %.2f s to %.2f s (the scene map's mv#%s)", id, p->stem, p->from, p->until, id);
    } else if (p->from > 0.0) {
        khdays_log("movie %s: %s from %.2f s to its end (the scene map's mv#%s)", id, p->stem, p->from, id);
    }
    fprintf(stderr, "movies: %s shown instead from %s (%dx%d, %s)\n", id, p->file, info.width, info.height,
            info.channels > 0 ? "with its sound" : "no sound");
    return 1;
}

/* Under the lock: the movie's last video has ended; the game goes on, or finishes its own movie first. */
static void movie_videos_ended(const char *how)
{
    if (state_now() == HELD || course_credits) {
        finish(how);
    } else {
        InterlockedExchange(&state, FINISHING);
        khdays_log("movie %s: the HD video ended before the game's movie; its last picture stays", id);
        fprintf(stderr, "movies: %s: the HD video ended before the game's movie\n", id);
    }
}

/* Under the lock: the part just opened starts, with the theater's subtitles for it in the game's
 * language (a language just chosen first). */
static void movie_part_start(void)
{
    khdays_movie_subtitles_load(parts[part_next].subtitles,
                                khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage());
    khdays_hd_start();
    snprintf(shown, sizeof(shown), "%s", parts[part_next].stem);
    ++part_next;
}

void khdays_movie_hd_stream(int slot, const char *path, int replaced)
{
    char movie[64];
    (void)slot;
    InterlockedExchange(&game_movie, 1);
    shown[0] = '\0';
    if (path == NULL || replaced || state_now() != IDLE || !(khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_HD) ||
        !movie_id(path, movie, sizeof(movie)) || !movie_parts(movie)) {
        return;
    }
    for (int i = 0; i < part_count; ++i) {
        if (GetFileAttributesA(parts[i].file) == INVALID_FILE_ATTRIBUTES) {
            if (tell_once(movie)) {
                khdays_log("movie %s: no HD video (%s); the game's movie plays", movie, parts[i].file);
            }
            return;
        }
    }
    snprintf(id, sizeof(id), "%s", movie);
    /* opened outside the lock: only this thread starts a course */
    if (!movie_part_open(0)) {
        if (tell_once(movie)) {
            khdays_log("movie %s: the game's movie plays", movie);
        }
        return;
    }
    AcquireSRWLockExclusive(&lock);
    skippable = -1;
    skip_wanted = 0;
    skipped = 0;
    course_credits = credits_scene;
    game_last = host_last = 0xffff; /* what is held now is no press */
    khdays_sound_output_mute(1);
    part_next = 0;
    movie_part_start();
    InterlockedExchange(&state, PLAYING);
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_stopped(void)
{
    InterlockedExchange(&game_movie, 0);
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == PLAYING && course_credits) {
        khdays_log("movie %s: the game's movie ended; its staff roll runs on behind the HD video", id);
    } else if (state_now() == PLAYING) {
        khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1); /* the game stops at its next wait */
        InterlockedExchange(&state, HELD);
        khdays_log("movie %s: the game's movie ended; the game waits for the HD video", id);
        fprintf(stderr, "movies: %s: the game's movie ended; the game waits for the HD video\n", id);
    } else if (state_now() == FINISHING) {
        finish("ended");
    } else if (state_now() == SKIPPING) {
        finish("skipped");
    } else if (state_now() == MOVIE_PAUSE && pause_from == PLAYING && !course_credits) {
        pause_from = HELD; /* it ended as the pause opened: held from now on, as at a movie's end */
        khdays_log("movie %s: the game's movie ended under the pause; the game waits for the HD video", id);
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_credits(int phase)
{
    AcquireSRWLockExclusive(&lock);
    if (phase == 1) {
        credits_scene = 1;
    } else if (state_now() != IDLE && course_credits) {
        if (phase == 2 && state_now() == PLAYING) {
            khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1);
            InterlockedExchange(&state, HELD);
            khdays_log("movie %s: the staff roll ended; the game waits for the HD video", id);
            fprintf(stderr, "movies: %s: the staff roll ended; the game waits for the HD video\n", id);
        } else if (phase == 0 || state_now() == SKIPPING) {
            finish(state_now() == SKIPPING ? "skipped" : "ended with the credits scene");
        }
    }
    if (phase == 0) {
        credits_scene = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_flags(unsigned flags)
{
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        skippable = (flags & 8) != 0;
        if (state_now() == SKIPPING && (flags & 4)) {
            skipped = 1;
        }
        if (skip_wanted) {
            skip_wanted = 0;
            movie_skip(); /* a skip already asked (the pause's): not the pause again */
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

uint16_t khdays_movie_hd_game_buttons(uint16_t buttons)
{
    uint16_t given;
    if (state_now() == IDLE && !ended_now() && stuck == 0) {
        return buttons;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        const uint16_t pressed = (uint16_t)(buttons & ~game_last);
        game_last = buttons;
        if ((pressed & KHDAYS_KEY_START) && !overhaul()) {
            request_skip(); /* (the Menu Overhaul's panel takes the window's presses instead) */
        }
        given = state_now() == SKIPPING && !skipped ? (uint16_t)KHDAYS_KEY_START : 0; /* a scene's: none */
        if (state_now() == SCENE_PAUSE) {
            /* the game's own pause: START pressed every other frame until it opens (the game refuses
             * one while its screen fades: a scene held at its start), then the player's buttons, but
             * the ones held as it opened (the START that asked for it) only once let go */
            if (!pause_seen) {
                pause_start_given = 1;
                pause_press = !pause_press;
                pause_stuck = buttons;
                given = pause_press ? (uint16_t)KHDAYS_KEY_START : 0;
            } else {
                pause_stuck &= buttons;
                given = (uint16_t)(buttons & ~pause_stuck);
            }
        }
    } else {
        if (ended_now()) {
            stuck = buttons; /* held as the course ended */
            InterlockedExchange(&ended, 0);
        }
        stuck &= buttons;
        given = (uint16_t)(buttons & ~stuck);
    }
    ReleaseSRWLockExclusive(&lock);
    return given;
}

int khdays_movie_hd_showing(void)
{
    const LONG now = state_now();
    return now != IDLE && now != SCENE_FADE;
}

int khdays_movie_hd_running(void)
{
    return state_now() != IDLE;
}

/* --- The theater's HD scenes, its chapters, the monologues' voice --------------------------------- */

#define SCENE_SKIP_REQUEST 0x588u /* the event machine's skip request (Game_UnwindActionStack) */

void SoundMgr_StopAllSePlayers(void); /* the game's: what the event pause's Skip stops */

/* When the game began to fade out before a scene's videos and back in after them (0: not yet) */
static volatile double fade_began, back_began;

static double seconds_now(void)
{
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (double)now.QuadPart / (double)frequency.QuadPart;
}

float khdays_movie_hd_game_level(void)
{
    const LONG now = state_now();
    double level = 1.0;
    if (now == SCENE_FADE) {
        level = 1.0 - (seconds_now() - fade_began) / FADE_SECONDS;
    } else if (now == SCENE || now == SCENE_SKIP || now == SCENE_LANDING) {
        level = 0.0;
    } else if (now == IDLE && back_began > 0.0) {
        level = (seconds_now() - back_began) / FADE_SECONDS;
    }
    return (float)(level < 0.0 ? 0.0 : level > 1.0 ? 1.0 : level);
}

/* A scene's course ends: the game shown and heard again, coming up from black and silence. */
static void scene_finish(const char *how)
{
    back_began = seconds_now(); /* before the state turns idle: no picture of the game at full light */
    finish(how);
}

static char scene_key[48];
static uint32_t scene_state; /* the event machine's state (a reference) */
static int scene_watched;    /* its videos ran to their end: the game's skip asked is the port's own */
static char (*scene_stems)[64];
static double *scene_parts; /* each video's part: from, until (seconds of it; 0 its own start or end) */
static int scene_count, scene_next;

/* KHDAYS_TEST_SCENE_START=<ms>: START pressed that long into each scene's course (the scripted
 * input cannot: the game, held, reads none), once a course. */
static int test_start_ms = -2; /* -2 not read, -1 none */
static ULONGLONG scene_began_at;
static int test_start_done;

/* A scene video's file: a mod's cutscenes/cinematics/<stem>.mp4 first, then the player's own. */
void khdays_movie_hd_scene_file(const char *stem, char *file, size_t size)
{
    char relative[128];
    snprintf(relative, sizeof(relative), "cutscenes/cinematics/%s.mp4", stem);
    if (!khdays_mods_find(relative, file, (int)size)) {
        snprintf(file, size, "data/cutscenes/cinematics/%s.mp4", stem);
    }
}

/* The next of the scene's videos: opened (Media Foundation takes its time: never under the lock), then
 * started with its subtitles (the stem without its "hd", as the movies' are named). */
static int scene_open(int index)
{
    char file[1024], why[320];
    KhdaysHdInfo info;
    const double from = scene_parts[2 * index], until = scene_parts[2 * index + 1];
    khdays_movie_hd_scene_file(scene_stems[index], file, sizeof(file));
    /* the first comes up from black and silence, the last goes down to them */
    khdays_hd_fades(index == 0 ? FADE_SECONDS : 0.0, index == scene_count - 1 ? FADE_SECONDS : 0.0);
    if (!khdays_hd_open_part(file, from, until, &info, why, sizeof(why))) {
        khdays_log("scene %s: %s cannot be played (%s)", scene_key, file, why);
        fprintf(stderr, "scenes: %s: %s cannot be played (%s)\n", scene_key, file, why);
        return 0;
    }
    khdays_log("scene %s: the HD video %s plays (%dx%d, %s)", scene_key, file, info.width, info.height,
               info.channels > 0 ? "with its sound" : "no sound");
    if (until > 0.0) {
        khdays_log("scene %s: %s from %.2f s to %.2f s", scene_key, scene_stems[index], from, until);
    } else if (from > 0.0) {
        khdays_log("scene %s: %s from %.2f s to its end", scene_key, scene_stems[index], from);
    }
    fprintf(stderr, "scenes: %s: %s shown instead (%dx%d)\n", scene_key, file, info.width, info.height);
    return 1;
}

/* Under the lock: the video just opened starts. */
static void scene_start_opened(void)
{
    const char *stem = scene_stems[scene_next];
    khdays_movie_subtitles_load(strncmp(stem, "hd", 2) == 0 ? stem + 2 : stem,
                                khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage());
    khdays_hd_start();
    ++scene_next;
}

/* The videos are over (ended, or stopped): the game is let go to take its own skip at its next step;
 * the window keeps the last picture until the scene's stretch ends. Under the lock. */
static void scene_videos_over(const char *how)
{
    if (scene_chapter) {
        scene_finish(how); /* a Theater chapter's: back to its list */
        return;
    }
    if (overhaul()) {
        /* the player's Skip (the panel) skips as the game's does; an end is the port's own skip, and the
         * theater's music may go on into the next scene's video */
        scene_watched = strcmp(how, "skipped in the pause menu") != 0;
        scene_player_skip = !scene_watched; /* (khdays_movie_hd_scene_ended) */
        if (scene_watched) {
            khdays_hd_close_scene_end();
        } else {
            khdays_hd_close();
        }
    } else {
        khdays_hd_close();
    }
    InterlockedExchange(&state, SCENE_SKIP);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    khdays_log("scene %s: the HD videos %s; the game's own skip is asked", scene_key, how);
    fprintf(stderr, "scenes: %s: the HD videos %s; the game's skip is asked\n", scene_key, how);
}

/* START during the videos: they pause, picture and sound, and the game is let go with START (its
 * buttons, khdays_movie_hd_game_buttons) to open its own pause over them; khdays_movie_hd_scene_frame
 * watches it. Under the lock. */
static void scene_pause_begin(void)
{
    khdays_hd_pause(1);
    pause_start_given = pause_seen = pause_frames = pause_press = 0;
    pause_stuck = 0;
    InterlockedExchange(&state, SCENE_PAUSE);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    khdays_log("scene %s: START: the video paused, the game's own pause asked", scene_key);
    fprintf(stderr, "scenes: %s: START: the video paused, the game's pause asked\n", scene_key);
}

void khdays_movie_hd_scene_frame(int pause_open, int skip_requested)
{
    if (state_now() != SCENE_PAUSE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == SCENE_PAUSE && pause_start_given) {
        ++pause_frames;
        if (pause_open) {
            pause_seen = 1;
        } else if (pause_seen || pause_frames > PAUSE_WAIT_FRAMES) {
            if (skip_requested) {
                /* its Skip: the game unwinds to the scene's end, which ends the course */
                khdays_hd_close();
                scene_player_skip = 1;
                InterlockedExchange(&state, SCENE_LANDING);
                khdays_log("scene %s: the game's pause skipped the scene", scene_key);
                fprintf(stderr, "scenes: %s: the game's pause skipped the scene\n", scene_key);
            } else {
                /* closed (or never opened): the game held again, the video on */
                khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1);
                khdays_hd_pause(0);
                InterlockedExchange(&state, SCENE);
                khdays_log("scene %s: the game's pause %s; the video goes on", scene_key,
                           pause_seen ? "closed" : "did not open");
                fprintf(stderr, "scenes: %s: the game's pause %s; the video goes on\n", scene_key,
                        pause_seen ? "closed" : "did not open");
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_movie_hd_scene_paused(void)
{
    return state_now() == SCENE_PAUSE;
}

uint32_t khdays_movie_hd_scene_state(void)
{
    const LONG now = state_now();
    return now == SCENE || now == SCENE_SKIP || now == SCENE_LANDING || now == SCENE_PAUSE ? scene_state : 0u;
}

/* Under the lock: the scene's first video starts, the game silent and held. */
static void scene_begin(void)
{
    scene_start_opened();
    khdays_sound_output_mute(1);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1); /* the game stops at its next wait */
    scene_began_at = GetTickCount64();
    test_start_done = 0;
    InterlockedExchange(&state, SCENE);
}

static int scene_course(const char *key, uint32_t event_state, char *const *stems, const double *from,
                        const double *until, int count, int chapter)
{
    char file[1024];
    if (count <= 0 || state_now() != IDLE || !(khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_HD)) {
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        khdays_movie_hd_scene_file(stems[i], file, sizeof(file));
        if (GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) {
            if (tell_once(key)) {
                khdays_log("scene %s: no HD video %s; the game's scene plays", key, file);
                fprintf(stderr, "scenes: %s: no HD video %s; the game's scene plays\n", key, file);
            }
            return 0;
        }
    }
    AcquireSRWLockExclusive(&lock);
    {
        char (*grown)[64] = realloc(scene_stems, (size_t)count * sizeof(*scene_stems));
        double *spans = grown != NULL ? (double *)realloc(scene_parts, (size_t)count * 2 * sizeof(double)) : NULL;
        if (grown != NULL) {
            scene_stems = grown;
        }
        if (spans == NULL) {
            ReleaseSRWLockExclusive(&lock);
            return 0;
        }
        scene_parts = spans;
    }
    for (int i = 0; i < count; ++i) {
        snprintf(scene_stems[i], sizeof(scene_stems[i]), "%s", stems[i]);
        scene_parts[2 * i] = from != NULL ? from[i] : 0.0;
        scene_parts[2 * i + 1] = until != NULL ? until[i] : 0.0;
    }
    scene_count = count;
    scene_chapter = chapter;
    scene_player_skip = 0;
    scene_next = 0;
    scene_state = event_state;
    scene_watched = 0;
    snprintf(scene_key, sizeof(scene_key), "%s", key);
    snprintf(id, sizeof(id), "%s", key);
    skippable = 1;
    game_last = host_last = 0xffff; /* what is held now is no press */
    ReleaseSRWLockExclusive(&lock);
    /* opened outside the lock: only this thread starts a course */
    if (!scene_open(0)) {
        if (tell_once(key)) {
            khdays_log("scene %s: the game's scene plays", key);
        }
        return 0;
    }
    AcquireSRWLockExclusive(&lock);
    if (chapter) {
        scene_begin(); /* from the Theater's list, at once */
    } else {
        /* the game fades out first and is held once black (poll), so no sound or picture is cut off; the
         * music its event begins meanwhile is not heard (a moment of it before the videos) */
        fade_began = seconds_now();
        khdays_sound_bridge_hold_new_music(1);
        InterlockedExchange(&state, SCENE_FADE);
        khdays_log("scene %s: the game goes down to black first", key);
    }
    ReleaseSRWLockExclusive(&lock);
    /* the held game sends no frames: without this the window may sleep on through the video, every
     * picture passed over (seen: 3.8 s of hd002's 4.1 s) */
    khdays_host_wake();
    return 1;
}

int khdays_movie_hd_scene(const char *key, uint32_t event_state, char *const *stems, const double *from,
                          const double *until, int count)
{
    return scene_course(key, event_state, stems, from, until, count, 0);
}

int khdays_movie_hd_chapter(const char *key, char *const *stems, int count)
{
    return scene_course(key, 0u, stems, NULL, NULL, count, 1);
}

void khdays_movie_hd_scene_step(uint32_t event_state, void *host_state)
{
    if (state_now() != SCENE_SKIP) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == SCENE_SKIP && event_state == scene_state) {
        SoundMgr_StopAllSePlayers();
        *(volatile uint32_t *)((uint8_t *)host_state + SCENE_SKIP_REQUEST) = 1u;
        InterlockedExchange(&state, SCENE_LANDING);
        khdays_log("scene %s: the game's skip asked", scene_key);
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_movie_hd_voice(const char *key, char *const *stems, int count)
{
    char **files;
    int ok = 1;
    if (count <= 0 || !(khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_HD)) {
        return 0;
    }
    files = (char **)calloc((size_t)count, sizeof(*files));
    if (files == NULL) {
        return 0;
    }
    for (int i = 0; i < count && ok; ++i) {
        files[i] = (char *)malloc(1024);
        if (files[i] == NULL) {
            ok = 0;
            break;
        }
        khdays_movie_hd_scene_file(stems[i], files[i], 1024);
        if (GetFileAttributesA(files[i]) == INVALID_FILE_ATTRIBUTES) {
            if (tell_once(key)) {
                khdays_log("scene %s: no HD video %s; the game's page alone", key, files[i]);
                fprintf(stderr, "scenes: %s: no HD video %s; the game's page alone\n", key, files[i]);
            }
            ok = 0;
        }
    }
    if (ok) {
        khdays_hd_voice_play((const char *const *)files, count);
        khdays_log("scene %s: the voice of %s heard over the game's page", key, files[0]);
        fprintf(stderr, "scenes: %s: the voice of %s heard over the game's page\n", key, files[0]);
    }
    for (int i = 0; i < count; ++i) {
        free(files[i]);
    }
    free(files);
    return ok;
}

int khdays_movie_hd_subtitles_exist(const char *stem)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    char relative[128], path[1024];
    const int language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    if (language < 1 || language > 5 || stem == NULL) {
        return 0;
    }
    snprintf(relative, sizeof(relative), "movies/subtitles/%s/%s.txt", codes[language], stem);
    if (khdays_mods_find(relative, path, (int)sizeof(path))) {
        return 1;
    }
    snprintf(path, sizeof(path), "data/cutscenes/cinematics/subtitles/%s/%s.txt", codes[language], stem);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

void khdays_movie_hd_voice_stop(void)
{
    khdays_hd_voice_stop();
}

int khdays_movie_hd_scene_ended(void)
{
    LONG now = state_now();
    int port_skip = 0;
    if (now != SCENE_FADE && now != SCENE && now != SCENE_SKIP && now != SCENE_LANDING && now != SCENE_PAUSE) {
        return 0;
    }
    AcquireSRWLockExclusive(&lock);
    now = state_now();
    if (now == SCENE_FADE || now == SCENE || now == SCENE_SKIP || now == SCENE_LANDING || now == SCENE_PAUSE) {
        port_skip = (now == SCENE_SKIP || now == SCENE_LANDING) && !scene_player_skip;
        scene_finish(now == SCENE_LANDING ? "ended, the game's scene skipped to its end" : "ended with its scene");
    }
    ReleaseSRWLockExclusive(&lock);
    return port_skip;
}

void khdays_movie_hd_poll(void)
{
    char why[320];
    int status;
    if (InterlockedCompareExchange(&menu_open, 0, 0)) {
        return; /* (the video paused under the panel) */
    }
    if (state_now() == SCENE_FADE) {
        AcquireSRWLockExclusive(&lock);
        if (state_now() == SCENE_FADE && seconds_now() - fade_began >= FADE_SECONDS) {
            scene_begin(); /* the game is black and silent: its videos now */
            khdays_log("scene %s: the game is down to black; its HD videos begin", scene_key);
        }
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    if (state_now() == SCENE) {
        int open_next = 0;
        AcquireSRWLockExclusive(&lock);
        if (test_start_ms == -2) {
            const char *v = getenv("KHDAYS_TEST_SCENE_START");
            test_start_ms = v != NULL ? atoi(v) : -1;
        }
        if (state_now() == SCENE && test_start_ms >= 0 && !test_start_done &&
            GetTickCount64() - scene_began_at >= (ULONGLONG)test_start_ms) {
            test_start_done = 1;
            fprintf(stderr, "scenes: %s: KHDAYS_TEST_SCENE_START: START pressed\n", scene_key);
            if (overhaul()) {
                toggle_menu();
            } else {
                request_skip();
            }
        }
        if (state_now() == SCENE && !menu_open) {
            status = khdays_hd_status(why, sizeof(why));
            if (status == -1) {
                khdays_log("scene %s: the HD video stopped part-way (%s)", scene_key, why);
                scene_videos_over("stopped part-way");
            } else if (status == 1 && scene_next >= scene_count) {
                scene_videos_over("ended");
            } else if (status == 1) {
                open_next = 1;
            }
        }
        ReleaseSRWLockExclusive(&lock);
        if (open_next) {
            /* the next video opened here, the presenter's own thread, outside the lock (the opening
             * takes its time): nothing asks it for pictures meanwhile, the window keeps the last */
            const int opened = scene_open(scene_next);
            AcquireSRWLockExclusive(&lock);
            if (state_now() == SCENE) {
                if (opened) {
                    scene_start_opened();
                } else {
                    scene_videos_over("ended (the next could not be played)");
                }
            }
            ReleaseSRWLockExclusive(&lock);
        }
        return;
    }
    if (state_now() != PLAYING && state_now() != HELD) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    {
        int open_next = 0;
        if (state_now() == PLAYING || state_now() == HELD) {
            status = khdays_hd_status(why, sizeof(why));
            if (status == -1) {
                khdays_log("movie %s: the HD video stopped part-way (%s); the game's movie shows", id, why);
                fprintf(stderr, "movies: %s: the HD video stopped part-way (%s)\n", id, why);
                finish("stopped part-way");
            } else if (status == 1 && part_next < part_count) {
                open_next = 1;
            } else if (status == 1) {
                movie_videos_ended("ended");
            }
        }
        ReleaseSRWLockExclusive(&lock);
        if (open_next) {
            /* the movie's next video, opened here as a scene's are (outside the lock: it takes its time) */
            const int opened = movie_part_open(part_next);
            AcquireSRWLockExclusive(&lock);
            if (state_now() == MOVIE_PAUSE) {
                /* the pause opened meanwhile: the next video waits under it */
                if (opened) {
                    movie_part_start();
                    khdays_hd_pause(1);
                } else {
                    movie_pause_end(0);
                    movie_videos_ended("ended (the next could not be played)");
                }
            } else if (state_now() != PLAYING && state_now() != HELD) {
                if (opened) {
                    khdays_hd_close(); /* START or a reset meanwhile */
                }
            } else if (opened) {
                movie_part_start();
            } else {
                movie_videos_ended("ended (the next could not be played)");
            }
            ReleaseSRWLockExclusive(&lock);
        }
    }
}

void khdays_movie_hd_host_buttons(uint16_t buttons)
{
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == MOVIE_PAUSE) {
        /* the pause's buttons as PauseMenu_Frame takes them: up/down choose, A takes, START or B goes on */
        const uint16_t pressed = (uint16_t)(buttons & ~host_last);
        host_last = buttons;
        if (pause_fresh) {
            pause_fresh = buttons != 0;
        } else if (pressed & (KHDAYS_KEY_UP | KHDAYS_KEY_DOWN)) {
            pause_selected ^= 1;
            pause_lit_from = seconds_now();
        } else if (pressed & KHDAYS_KEY_A) {
            movie_pause_end(pause_selected == 1);
        } else if (pressed & (KHDAYS_KEY_START | KHDAYS_KEY_B)) {
            movie_pause_end(0);
        }
    } else if (state_now() != IDLE) {
        const uint16_t pressed = (uint16_t)(buttons & ~host_last);
        host_last = buttons;
        if (pressed & KHDAYS_KEY_START) {
            request_skip();
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_movie_pause_closed_by_choice(void)
{
    int closed;
    AcquireSRWLockExclusive(&lock);
    closed = menu_closed_by_choice;
    menu_closed_by_choice = 0;
    ReleaseSRWLockExclusive(&lock);
    return closed;
}

void khdays_media_host_presses(uint16_t buttons, uint16_t taps, int monologue)
{
    if (!overhaul()) {
        if (!monologue) {
            khdays_movie_hd_host_buttons(buttons); /* the DS's pause */
        }
        return;
    }
    if (state_now() == IDLE && !monologue) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    {
        /* a press since the last look, or one the window saw come and go between two looks (`taps`) */
        const uint16_t pressed = (uint16_t)((buttons & ~host_last) | taps);
        host_last = buttons;
        if (pressed & KHDAYS_KEY_START) {
            if (state_now() != IDLE) {
                toggle_menu();
            } else {
                toggle_monologue_menu();
            }
        } else if (menu_open && (pressed & KHDAYS_KEY_B)) {
            resume_menu();
        } else if (menu_open && (pressed & (KHDAYS_KEY_UP | KHDAYS_KEY_DOWN))) {
            menu_row ^= 1; /* two rows: up and down both go round to the other */
        } else if (menu_open && (pressed & KHDAYS_KEY_A)) {
            menu_activate();
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_movie_hd_movie_pause(int *selected, int *lit, int *language)
{
    if (state_now() != MOVIE_PAUSE) {
        return 0;
    }
    *selected = pause_selected;
    *lit = 1 + (int)((seconds_now() - pause_lit_from) / PAUSE_BLINK_SECONDS) % 2;
    *language = pause_language;
    return 1;
}

int khdays_movie_hd_course_paused(void)
{
    const LONG now = state_now();
    return now == SCENE_PAUSE || now == MOVIE_PAUSE;
}
