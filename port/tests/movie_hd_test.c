/* How an HD movie runs (runtime/movies/movie_hd.c), against a scripted game and video: the ids, the
 * lookup, every transition, the buttons the game is given and the clock's pause reasons (with the real
 * runtime/core/clock.c); an event scene's videos and a monologue's voice. It runs in a
 * folder of its own, which it creates and leaves behind. */
#include "../host/movies/movie_hd.h"
#include "../host/app/host.h"
#include "../host/app/sound_output.h"
#include "../runtime/core/clock.h"
#include "../runtime/input/input.h"
#include "../runtime/movies/movie_hd.h"
#include "../runtime/movies/scenes.h"

#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "movie_hd_test:%d: failed: %s\n", __LINE__, #c);                                           \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* --- what the course calls, scripted --- */
static int graphics_flags = KHDAYS_HOST_MOVIES_HD;
static int open_ok = 1, opens, closes, started, muted;
static int status_now; /* what khdays_hd_status says */
static char opened_path[260];
static int told_cannot; /* log lines saying a file cannot be played */

int khdays_host_graphics_flags(void)
{
    return graphics_flags;
}

/* the window woken as a scene's video begins (the held game sends no frames) */
static int wakes;
void khdays_host_wake(void)
{
    ++wakes;
}

/* A mod's video (mods/<Mod>/movies/hd<id>.mp4, as khdays-studio saves one):
 * the one mod holds hd843's once the test gives it. */
static int mod_has_843;
int khdays_mods_find(const char *relative, char *path, int size)
{
    if (!mod_has_843 || strcmp(relative, "movies/hd843.mp4") != 0) {
        return 0;
    }
    snprintf(path, (size_t)size, "mods/m/movies/hd843.mp4");
    return 1;
}

/* the game's: what the event pause's Skip stops (the scenes' course asks it) */
static int sound_effects_stopped;
void SoundMgr_StopAllSePlayers(void)
{
    ++sound_effects_stopped;
}

/* the scene map (its movies' entries, read by the real scene_map.c) and the end asked of a video */
static KhdaysSceneMap scene_map;
const KhdaysSceneMap *khdays_scenes_map(void)
{
    return &scene_map;
}
static double opened_from = -1.0, opened_until = -1.0; /* the part asked for by the last opening */
static double next_in, next_out;                       /* the fades set for the next opening */
static double opened_in = -1.0, opened_out = -1.0;     /* the last opening's */

void khdays_hd_fades(double in, double out)
{
    next_in = in;
    next_out = out;
}
int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    opened_from = opened_until = 0.0;
    opened_in = next_in;
    opened_out = next_out;
    next_in = next_out = 0.0; /* each opening takes them once */
    snprintf(opened_path, sizeof(opened_path), "%s", path);
    if (!open_ok) {
        snprintf(why, size, "scripted failure");
        return 0;
    }
    ++opens;
    started = 0;
    status_now = 0;
    info->width = 1920;
    info->height = 1080;
    info->fps = 60.0;
    info->channels = 2;
    info->rate = 48000;
    return 1;
}
int khdays_hd_open_part(const char *path, double from, double until, KhdaysHdInfo *info, char *why, size_t size)
{
    const int ok = khdays_hd_open(path, info, why, size);
    opened_from = from;
    opened_until = until;
    return ok;
}
void khdays_hd_start(void)
{
    started = 1;
}
static int hd_paused; /* the decoder paused (START's pause over a scene) */
void khdays_hd_pause(int pause)
{
    hd_paused = pause;
}
void khdays_hd_close(void)
{
    ++closes;
}
/* the Menu Overhaul's (runtime/mods/installed.h): not installed but in its own part, the DS's pause tested */
static int overhaul_installed;
static int scene_end_closes;
void khdays_hd_close_scene_end(void)
{
    ++scene_end_closes;
}
void khdays_hd_voice_pause(int pause)
{
    (void)pause;
}
int khdays_mod_installed(int mod)
{
    (void)mod;
    return overhaul_installed;
}
int khdays_hd_status(char *why, size_t size)
{
    if (status_now == -1) {
        snprintf(why, size, "scripted failure part-way");
    }
    return status_now;
}
void khdays_sound_output_mute(int mute)
{
    muted = mute;
}
/* the music the game starts held silent (a scene's videos coming) */
static int music_held;
void khdays_sound_bridge_hold_new_music(int hold)
{
    music_held = hold;
}
/* a monologue's voice: the files asked for, and the stops */
static char voice_path[260];
static int voices, voice_files, voice_stops;
void khdays_hd_voice_play(const char *const *paths, int count)
{
    snprintf(voice_path, sizeof(voice_path), "%s", count > 0 ? paths[0] : "");
    voice_files = count;
    ++voices;
}
void khdays_hd_voice_stop(void)
{
    ++voice_stops;
}
/* the subtitles asked for when a video starts: its movie, in the game's language */
static char subtitles_movie[16];
static int subtitles_language;
void khdays_movie_subtitles_load(const char *movie, int language)
{
    snprintf(subtitles_movie, sizeof(subtitles_movie), "%s", movie);
    subtitles_language = language;
}
int GetLanguage(void)
{
    return 5;
}
int khdays_language_wanted(void)
{
    return 0;
}
void khdays_log(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (strstr(line, "cannot be played") != NULL) {
        ++told_cannot;
    }
}

static void touch(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f != NULL) {
        fclose(f);
    }
}

static int held(void)
{
    return khdays_clock_held(KHDAYS_HOLD_MOVIE);
}

/* A scene's course past the game's fade to black (half a second): its videos begin at the next poll. */
static void past_the_fade(void)
{
    Sleep(600);
    khdays_movie_hd_poll();
}

int main(void)
{
    char folder[MAX_PATH], previous[MAX_PATH];
    const uint16_t START = KHDAYS_KEY_START, A = KHDAYS_KEY_A;
    GetCurrentDirectoryA(MAX_PATH, previous);
    GetTempPathA(MAX_PATH, folder);
    strncat(folder, "khdays-movie-hd-test", MAX_PATH - strlen(folder) - 1);
    _mkdir(folder);
    SetCurrentDirectoryA(folder);
    _mkdir("data");
    _mkdir("data/cutscenes");
    _mkdir("data/cutscenes/cinematics");
    touch("data/cutscenes/cinematics/hd802.mp4");
    touch("data/cutscenes/cinematics/hd843.mp4");
    khdays_clock_init();

    /* the ids and the lookup */
    khdays_movie_hd_stream(1, "/mv/839_es.mods", 0); /* no file: the game's movie */
    CHECK(!khdays_movie_hd_showing() && opens == 0);
    khdays_movie_hd_stream(0, "/mv/802.mods", 1); /* a mod's movie takes it */
    CHECK(!khdays_movie_hd_showing() && opens == 0);
    graphics_flags = 0; /* movies = pc or ds, or no window */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    CHECK(!khdays_movie_hd_showing() && opens == 0);
    graphics_flags = KHDAYS_HOST_MOVIES_HD;

    /* watched to its end: held at the game's movie's end, let go at the video's */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    CHECK(strcmp(opened_path, "data/cutscenes/cinematics/hd802.mp4") == 0);
    CHECK(khdays_movie_hd_showing() && started && muted);
    CHECK(strcmp(subtitles_movie, "802") == 0 && subtitles_language == 5); /* its subtitles, in the game's language */
    CHECK(khdays_movie_hd_game_buttons(A) == 0);                           /* the player's buttons withheld */
    khdays_movie_hd_stream(2, "/mv/802.mods", 0);                          /* a second stream: the same course */
    CHECK(opens == 1);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    CHECK(khdays_movie_hd_showing() && held());
    khdays_movie_hd_poll(); /* still playing */
    CHECK(held());
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held() && !muted && closes == 1);
    CHECK(khdays_movie_hd_game_buttons(A) == 0); /* held when it ended: withheld until let go */
    CHECK(khdays_movie_hd_game_buttons(0) == 0);
    CHECK(khdays_movie_hd_game_buttons(A) == A);
    khdays_movie_hd_game_buttons(0);

    /* the video ends first: its last picture until the game's movie ends */
    khdays_movie_hd_stream(1, "843.z", 0);
    CHECK(strcmp(opened_path, "data/cutscenes/cinematics/hd843.mp4") == 0);
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(khdays_movie_hd_showing() && !held());
    khdays_movie_hd_stopped();
    CHECK(!khdays_movie_hd_showing() && !held() && !muted);
    khdays_movie_hd_game_buttons(0);

    /* a mod's video before the player's own */
    mod_has_843 = 1;
    _mkdir("mods");
    _mkdir("mods/m");
    _mkdir("mods/m/movies");
    touch("mods/m/movies/hd843.mp4");
    khdays_movie_hd_stream(1, "843.z", 0);
    CHECK(strcmp(opened_path, "mods/m/movies/hd843.mp4") == 0);
    status_now = 1;
    khdays_movie_hd_poll();
    khdays_movie_hd_stopped();
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);
    mod_has_843 = 0;

    /* START where the DS lets skip: the video paused under the DS's scene pause, the game held; its
     * Continue goes on; its Skip stops the video, START given until the game skips */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(8);
    {
        int selected = -1, lit = 0, language = 0;
        const int closed_before = closes;
        CHECK(khdays_movie_hd_game_buttons(START) == 0); /* pressed: the pause, nothing given */
        CHECK(khdays_movie_hd_movie_pause(&selected, &lit, &language) && selected == 0 && lit == 1 && language == 5);
        CHECK(hd_paused && held() && khdays_movie_hd_showing() && khdays_movie_hd_course_paused() &&
              closes == closed_before);
        khdays_movie_hd_host_buttons(START); /* the START that opened it, still held: nothing */
        CHECK(khdays_movie_hd_movie_pause(&selected, &lit, &language));
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(START); /* START again: Continue */
        CHECK(!khdays_movie_hd_movie_pause(&selected, &lit, &language) && !hd_paused && !held() &&
              khdays_movie_hd_showing());
        CHECK(khdays_movie_hd_game_buttons(START) == 0); /* the START that closed it opens nothing */
        CHECK(khdays_movie_hd_movie_pause(&selected, &lit, &language) == 0);
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(START); /* open again: down to Skip, its look blinking */
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(KHDAYS_KEY_DOWN);
        CHECK(khdays_movie_hd_movie_pause(&selected, &lit, &language) && selected == 1 && lit == 1);
        Sleep(300); /* past 16 frames: the entry's other look */
        CHECK(khdays_movie_hd_movie_pause(&selected, &lit, &language) && lit == 2);
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(A); /* Skip */
        CHECK(closes == closed_before + 1 && khdays_movie_hd_showing() && !held() && !khdays_movie_hd_course_paused());
    }
    CHECK(khdays_movie_hd_game_buttons(START) == START); /* START given until the game skips */
    khdays_movie_hd_flags(8 | 4);                        /* the game took the skip */
    CHECK(khdays_movie_hd_game_buttons(START) == 0);
    khdays_movie_hd_stopped();
    CHECK(!khdays_movie_hd_showing() && !muted && !held());
    CHECK(khdays_movie_hd_game_buttons(START) == 0); /* still held from the course */
    CHECK(khdays_movie_hd_game_buttons(0) == 0);
    CHECK(khdays_movie_hd_game_buttons(START) == START); /* a new press is the game's */
    khdays_movie_hd_game_buttons(0);

    /* START before the movie said: the pause too, its Skip decided when the movie says */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_game_buttons(START);
    {
        int selected, lit, language;
        CHECK(khdays_movie_hd_movie_pause(&selected, &lit, &language));
    }
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(KHDAYS_KEY_UP); /* up from Continue: round to Skip */
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(A);
    CHECK(!hd_paused && !held() && khdays_movie_hd_showing() && !khdays_movie_hd_course_paused());
    khdays_movie_hd_flags(8);
    CHECK(khdays_movie_hd_game_buttons(START) == START);
    khdays_movie_hd_flags(8 | 4);
    khdays_movie_hd_stopped();
    CHECK(!khdays_movie_hd_showing());
    khdays_movie_hd_game_buttons(0);

    /* the game's movie ends as the pause opens: held from then on, as at its end */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_game_buttons(START);
    khdays_movie_hd_stopped();
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(START);
    CHECK(!khdays_movie_hd_course_paused() && held() && khdays_movie_hd_showing());
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_game_buttons(0);

    /* where the DS does not let skip: START does nothing, playing or held */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(0);
    CHECK(khdays_movie_hd_game_buttons(START) == 0 && khdays_movie_hd_showing());
    khdays_movie_hd_stopped();
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(START);
    CHECK(khdays_movie_hd_showing() && held());
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);

    /* held, the player's START from the host: the pause, the game still held after its Continue; its
     * Skip lets go at once */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    CHECK(held());
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(START);
    CHECK(khdays_movie_hd_course_paused() && held() && hd_paused);
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(START);
    CHECK(!khdays_movie_hd_course_paused() && held() && !hd_paused && khdays_movie_hd_showing());
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(START);
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(KHDAYS_KEY_DOWN);
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(A);
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);

    /* the settings menu during a hold: closing it leaves the game held */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1);
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
    CHECK(held());
    {
        const u64 a = khdays_clock_cycles();
        Sleep(20);
        CHECK(khdays_clock_cycles() == a); /* the time stands still */
    }
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!held());
    {
        const u64 a = khdays_clock_cycles();
        Sleep(20);
        CHECK(khdays_clock_cycles() > a);
    }
    khdays_movie_hd_game_buttons(0);

    /* the credits scene: not held at its movie's end; held where
     * its roll ends if the video still plays (the story: START does nothing) */
    khdays_movie_hd_credits(1);
    khdays_movie_hd_stream(1, "/mv/843.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(0);
    khdays_movie_hd_stopped();
    CHECK(khdays_movie_hd_showing() && !held()); /* the roll runs on behind the video */
    CHECK(khdays_movie_hd_game_buttons(START) == 0 && khdays_movie_hd_showing());
    khdays_movie_hd_credits(2);
    CHECK(khdays_movie_hd_showing() && held());
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_credits(0);
    khdays_movie_hd_game_buttons(0);
    /* ... the video ends first: the game shown wherever the roll is */
    khdays_movie_hd_credits(1);
    khdays_movie_hd_stream(1, "/mv/843.mods", 0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held() && !muted);
    khdays_movie_hd_credits(0);
    khdays_movie_hd_game_buttons(0);
    /* ... the Theater's (its argument lets skip): START during the roll, the scene leaves */
    khdays_movie_hd_credits(1);
    khdays_movie_hd_stream(1, "/mv/843.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    CHECK(khdays_movie_hd_game_buttons(START) == START && khdays_movie_hd_showing());
    khdays_movie_hd_credits(0);
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);

    /* failing part-way: the game's movie shown from where it is */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    status_now = -1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !muted && !held());
    khdays_movie_hd_game_buttons(0);

    /* an event scene of two videos: the game going down to black first, then held while they play (the
     * first coming up from black, the last going down to it), then let go to take its own skip at its
     * event's next step, shown until the scene ends, and coming up from black again */
    {
        static uint8_t event[0x600]; /* the event machine's state: its skip request at +0x588 */
        char *const stems[] = {"hd010", "hd011"};
        const double from[] = {129.4, 0.0}, until[] = {173.2, 0.0}; /* a part of the first, the second whole */
        const int opened_before = opens, stopped_before = sound_effects_stopped;
        touch("data/cutscenes/cinematics/hd010.mp4");
        touch("data/cutscenes/cinematics/hd011.mp4");
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, from, until, 2) == 1);
        /* the game going down, the first video opened and waiting */
        CHECK(!khdays_movie_hd_showing() && khdays_movie_hd_running() && !held() && !muted && !started &&
              opens == opened_before + 1 && wakes == 1 && khdays_movie_hd_game_level() > 0.5f && music_held);
        CHECK(khdays_movie_hd_game_buttons(A) == 0); /* the game's, but none of the player's buttons */
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(START); /* no pause before the videos */
        khdays_movie_hd_poll();
        CHECK(!khdays_movie_hd_showing() && !khdays_movie_hd_scene_paused());
        Sleep(600);
        CHECK(khdays_movie_hd_game_level() == 0.0f && !khdays_movie_hd_showing()); /* black, until the poll */
        khdays_movie_hd_poll();
        CHECK(khdays_movie_hd_showing() && held() && muted && started && khdays_movie_hd_game_level() == 0.0f);
        CHECK(strcmp(opened_path, "data/cutscenes/cinematics/hd010.mp4") == 0 && strcmp(subtitles_movie, "010") == 0 &&
              opened_from == 129.4 && opened_until == 173.2 && opened_in == 0.5 && opened_out == 0.0);
        CHECK(khdays_movie_hd_game_buttons(A) == 0);
        status_now = 1;
        khdays_movie_hd_poll(); /* the first ended: the second */
        CHECK(opens == opened_before + 2 && strcmp(subtitles_movie, "011") == 0 && held() && opened_from == 0.0 &&
              opened_until == 0.0 && opened_in == 0.0 && opened_out == 0.5);
        status_now = 1;
        khdays_movie_hd_poll(); /* the last ended: let go */
        CHECK(khdays_movie_hd_showing() && !held() && muted);
        CHECK(khdays_movie_hd_game_buttons(START) == 0); /* a scene's own skip: no button given */
        khdays_movie_hd_scene_step(0x9999u, event);      /* another event's step */
        CHECK(*(uint32_t *)(event + 0x588) == 0);
        khdays_movie_hd_scene_step(0x1234u, event);
        CHECK(*(uint32_t *)(event + 0x588) == 1 && sound_effects_stopped == stopped_before + 1);
        CHECK(khdays_movie_hd_scene_ended() == 1); /* the port's skip: the videos were watched */
        CHECK(!khdays_movie_hd_showing() && !muted && !held() && khdays_movie_hd_game_level() < 0.5f && !music_held);
        Sleep(600);
        CHECK(khdays_movie_hd_game_level() == 1.0f); /* up again */
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);
    }

    /* a scene whose video is missing: the game's own scene plays, nothing held */
    {
        char *const stems[] = {"hd010", "hd999"};
        const int opened_before = opens;
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, NULL, NULL, 2) == 0);
        CHECK(!khdays_movie_hd_showing() && !held() && !muted && opens == opened_before);
        graphics_flags = 0; /* movies = pc or ds: none of the scenes */
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, NULL, NULL, 1) == 0 && !khdays_movie_hd_showing());
        graphics_flags = KHDAYS_HOST_MOVIES_HD;
    }

    /* START during a scene's video: the video paused, the game let go with START once to open its own
     * pause; Continue holds it again and the video goes on, Skip lets it unwind to the scene's end */
    {
        static uint8_t event[0x600];
        char *const stems[] = {"hd010"};
        const int closed_before = closes;
        touch("data/cutscenes/cinematics/hd010.mp4");
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, NULL, NULL, 1) == 1);
        past_the_fade();
        CHECK(khdays_movie_hd_scene_state() == 0x1234u && opened_in == 0.5 && opened_out == 0.5); /* one: both */
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(START);
        CHECK(khdays_movie_hd_scene_paused() && khdays_movie_hd_showing() && hd_paused && !held() && muted);
        khdays_movie_hd_scene_frame(0, 0); /* before the game read its START: waits */
        CHECK(khdays_movie_hd_scene_paused());
        CHECK(khdays_movie_hd_game_buttons(START) == START); /* START pressed for the pause ... */
        khdays_movie_hd_scene_frame(0, 0);                   /* (refused: the screen fades) */
        CHECK(khdays_movie_hd_game_buttons(START) == 0);     /* ... every other frame ... */
        khdays_movie_hd_scene_frame(0, 0);
        CHECK(khdays_movie_hd_game_buttons(START) == START); /* ... until it opens */
        khdays_movie_hd_scene_frame(1, 0);                   /* the pause open */
        CHECK(khdays_movie_hd_game_buttons(START) == 0);     /* held since: not a new press */
        CHECK(khdays_movie_hd_game_buttons(0) == 0);
        CHECK(khdays_movie_hd_game_buttons(A) == A); /* the pause's choices are the player's */
        CHECK(khdays_movie_hd_scene_paused());
        khdays_movie_hd_scene_frame(0, 0); /* closed by Continue */
        CHECK(!khdays_movie_hd_scene_paused() && khdays_movie_hd_showing() && held() && !hd_paused);
        CHECK(khdays_movie_hd_game_buttons(A) == 0); /* held for the video again: none */
        /* again, and Skip */
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(START);
        khdays_movie_hd_game_buttons(START);
        khdays_movie_hd_scene_frame(1, 0);
        khdays_movie_hd_scene_frame(0, 1);
        CHECK(!khdays_movie_hd_scene_paused() && khdays_movie_hd_showing() && !held() && closes == closed_before + 1);
        khdays_movie_hd_scene_step(0x1234u, event); /* nothing more to ask: the game's own skip runs */
        CHECK(*(uint32_t *)(event + 0x588) == 0);
        CHECK(khdays_movie_hd_scene_ended() == 0); /* the player's Skip: the scenes after it skipped too */
        CHECK(!khdays_movie_hd_showing() && !muted && khdays_movie_hd_scene_state() == 0u);
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);
        /* a pause the game does not open: the video goes on */
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, NULL, NULL, 1) == 1);
        past_the_fade();
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(START);
        for (int i = 0; i <= 90; ++i) {
            khdays_movie_hd_game_buttons(0);
            khdays_movie_hd_scene_frame(0, 0);
        }
        CHECK(!khdays_movie_hd_scene_paused() && held() && !hd_paused);
        khdays_movie_hd_scene_ended();
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);
    }

    /* a day's monologue (scene 10): its clip's voice over the game's page, the game going on */
    {
        char *const stems[] = {"hd000"}, *const missing[] = {"hd001"};
        touch("data/cutscenes/cinematics/hd000.mp4");
        CHECK(khdays_movie_hd_voice("mnl#0", stems, 1) == 1);
        CHECK(voices == 1 && voice_files == 1 && strcmp(voice_path, "data/cutscenes/cinematics/hd000.mp4") == 0);
        CHECK(!khdays_movie_hd_showing() && !held() && !muted);
        CHECK(khdays_movie_hd_game_buttons(START) == START); /* the game's own page takes its buttons */
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_voice_stop();
        CHECK(voice_stops == 1);
        CHECK(khdays_movie_hd_voice("mnl#9", missing, 1) == 0 && voices == 1); /* no file: the page alone */
        graphics_flags = 0;
        CHECK(khdays_movie_hd_voice("mnl#0", stems, 1) == 0 && voices == 1); /* movies = pc or ds */
        graphics_flags = KHDAYS_HOST_MOVIES_HD;
    }
    DeleteFileA("data/cutscenes/cinematics/hd000.mp4");
    DeleteFileA("data/cutscenes/cinematics/hd010.mp4");
    DeleteFileA("data/cutscenes/cinematics/hd011.mp4");

    /* the scene map's movies: hd833 played in two parts, the first for 833, the second before hd848 for
     * 848, each with its subtitles; the game's own 834; the video shown last (a scene it shows too is
     * skipped after it) */
    {
        static const char text[] = "mv#833 hd833[:173.2]\nmv#848 hd833[173.2:] hd848\nmv#834 off\n";
        int opens_before;
        CHECK(khdays_scene_map_read(&scene_map, text, sizeof(text) - 1, "test", NULL) == 3);
        touch("data/cutscenes/cinematics/hd833.mp4");
        touch("data/cutscenes/cinematics/hd834.mp4");
        touch("data/cutscenes/cinematics/hd848.mp4");
        khdays_movie_hd_stream(1, "/mv/833.mods", 0);
        CHECK(strcmp(opened_path, "data/cutscenes/cinematics/hd833.mp4") == 0 && opened_from == 0.0 &&
              opened_until == 173.2 && strcmp(subtitles_movie, "833") == 0);
        CHECK(opened_in == 0.5 && opened_out == 0.5); /* one video: up from black and down to it */
        CHECK(khdays_movie_hd_showing() && khdays_movie_hd_last_shown("hd833") && !khdays_movie_hd_last_shown("hd830"));
        khdays_movie_hd_stopped();
        status_now = 1;
        khdays_movie_hd_poll();
        CHECK(!khdays_movie_hd_showing() && khdays_movie_hd_last_shown("hd833"));
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);

        opens_before = opens;
        khdays_movie_hd_stream(1, "/mv/848.mods", 0); /* the fight's end first, then hd848 */
        CHECK(opens == opens_before + 1 && strcmp(opened_path, "data/cutscenes/cinematics/hd833.mp4") == 0 &&
              opened_from == 173.2 && opened_until == 0.0 && strcmp(subtitles_movie, "833") == 0 && started);
        CHECK(opened_in == 0.5 && opened_out == 0.0); /* the first of two comes up, the last goes down */
        khdays_movie_hd_poll();                       /* still playing */
        CHECK(opens == opens_before + 1);
        status_now = 1; /* its first video ends: the second opens, its own subtitles with it */
        khdays_movie_hd_poll();
        CHECK(khdays_movie_hd_showing() && opens == opens_before + 2 &&
              strcmp(opened_path, "data/cutscenes/cinematics/hd848.mp4") == 0 && opened_from == 0.0 &&
              strcmp(subtitles_movie, "848") == 0 && started && khdays_movie_hd_last_shown("hd848"));
        CHECK(opened_in == 0.0 && opened_out == 0.5);
        khdays_movie_hd_stopped(); /* the game's movie ends first: held for the video */
        CHECK(khdays_movie_hd_showing() && held());
        status_now = 1;
        khdays_movie_hd_poll();
        CHECK(!khdays_movie_hd_showing() && !held());
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);

        opens_before = opens;
        khdays_movie_hd_stream(1, "/mv/834.mods", 0); /* off: the game's own movie */
        CHECK(!khdays_movie_hd_showing() && opens == opens_before && !khdays_movie_hd_last_shown("hd848"));
        khdays_movie_hd_stopped();
        khdays_movie_hd_stream(1, "/mv/802.mods", 0); /* no entry: hd802, whole */
        CHECK(khdays_movie_hd_showing() && opened_from == 0.0 && opened_until == 0.0 &&
              khdays_movie_hd_last_shown("hd802"));
        khdays_movie_hd_stopped();
        status_now = 1;
        khdays_movie_hd_poll();
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);
        CHECK(!khdays_movie_hd_showing());
        khdays_movie_hd_reset();
        CHECK(!khdays_movie_hd_last_shown("hd802")); /* the game's reset forgets it */
        khdays_scene_map_free(&scene_map);
        DeleteFileA("data/cutscenes/cinematics/hd833.mp4");
        DeleteFileA("data/cutscenes/cinematics/hd834.mp4");
        DeleteFileA("data/cutscenes/cinematics/hd848.mp4");
    }

    /* the Menu Overhaul's panel in place of the DS's pause: START opens it on Continue, B backs out, and A on
     * Skip skips at once (no question asked) */
    overhaul_installed = 1;
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(8);
    {
        int row = -1, can_skip = 0;
        const int closed_before = closes;
        khdays_media_host_presses(0, 0, 0);
        khdays_media_host_presses(START, 0, 0);
        CHECK(khdays_movie_pause_menu(&row, &can_skip) && row == 0 && can_skip && hd_paused &&
              khdays_clock_held(KHDAYS_HOLD_MOVIE_MENU)); /* the video and the game held */
        khdays_media_host_presses(0, 0, 0);
        khdays_media_host_presses(KHDAYS_KEY_B, 0, 0); /* backed out: on as it was */
        CHECK(!khdays_movie_pause_menu(&row, &can_skip) && !hd_paused && !khdays_clock_held(KHDAYS_HOLD_MOVIE_MENU) &&
              !khdays_movie_pause_closed_by_choice());
        khdays_media_host_presses(0, 0, 0);
        khdays_media_host_presses(START, 0, 0);
        khdays_media_host_presses(0, 0, 0);
        khdays_media_host_presses(KHDAYS_KEY_DOWN, 0, 0);
        CHECK(khdays_movie_pause_menu(&row, &can_skip) && row == 1);
        khdays_media_host_presses(0, 0, 0);
        khdays_media_host_presses(A, 0, 0); /* Skip */
        CHECK(!khdays_movie_pause_menu(&row, &can_skip) && khdays_movie_pause_closed_by_choice());
        CHECK(closes == closed_before + 1 && !held() && !khdays_movie_hd_course_paused());
        khdays_media_host_presses(0, 0, 0);
        khdays_media_host_presses(START, 0, 0); /* opened again: on Continue, not where it was left */
        CHECK(!khdays_movie_pause_menu(&row, &can_skip) || row == 0);
        khdays_media_host_presses(0, 0, 0);
    }
    khdays_movie_hd_flags(8 | 4); /* the game took the skip */
    khdays_movie_hd_stopped();
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_game_buttons(0);
    CHECK(!khdays_movie_hd_showing() && !held());
    overhaul_installed = 0;

    /* failing to open: the game's movie, said once */
    open_ok = 0;
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    CHECK(!khdays_movie_hd_showing() && told_cannot == 1);

    DeleteFileA("data/cutscenes/cinematics/hd802.mp4");
    DeleteFileA("data/cutscenes/cinematics/hd843.mp4");
    RemoveDirectoryA("data/cutscenes/cinematics");
    RemoveDirectoryA("data/cutscenes");
    RemoveDirectoryA("data");
    SetCurrentDirectoryA(previous);
    RemoveDirectoryA(folder);
    if (failures == 0) {
        printf("movie_hd_test: all checks passed\n");
    }
    return failures != 0;
}
