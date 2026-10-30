/* The window, run by SDL3 on a thread of its own: the game's thread hands over frames and reads the
 * input through SDL's locks and atomics; gpu_present.c draws, devices.c reads the devices through the
 * player's profile, the settings menu opens on Esc or the pad's settings button, sound_output.c plays.
 * Closing the window ends the process (docs/PLATFORM.md, "The PC's side"). */
#include "host.h"
#include "devices.h"
#include "press_edges.h"
#include "../render/gpu_present.h"
#include "../render/onescreen.h"
#include "../movies/movie_hd.h"
#include "../movies/movie_subtitles.h"
#include "../screens/game_pause.h"
#include "profile.h"
#include "../../runtime/menus/newgame.h"
#include "../../runtime/menus/savelist.h"
#include "../../runtime/battle/stop_magic.h"
#include "../../runtime/plugins/plugins.h"
#include "../settings/settings_model.h"
#include "../settings/settings_sample.h"
#include "../settings/settings_ui.h"
#include "../screens/cine_screen.h"
#include "../screens/day_screen.h"
#include "../movies/theater_music.h"
#include "../../runtime/menus/cinelist.h"
#include "../screens/mod_screen.h"
#include "../../runtime/menus/camp.h"
#include "../screens/room_screen.h"
#include "../screens/select_screen.h"
#include "../screens/camp_screen.h"
#include "../screens/shop_screen.h"
#include "../screens/movie_pause.h"
#include "../screens/modern_pause.h"
#include "../screens/save_screen.h"
#include "sound_output.h"
#include "updates.h"
#include "../screens/ui_font.h"
#include "../screens/ui_text.h"
#include "../../runtime/core/clock.h"
#include "../../runtime/display/display.h"
#include "../../runtime/display/frame_package.h"
#include "../../runtime/core/language.h"
#include "../../runtime/mods/installed.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/movies/scene_trace.h"
#include "../../runtime/net/net_session.h"
#include "../../runtime/scripts/queue.h"
#include "../../runtime/scripts/view.h"
#include "../../runtime/core/runtime.h"
#include "../../runtime/core/session_log.h"
#include "../../ui/situation.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define H 192
#define WALK_SPEED_DEFAULT 50 /* profile.c's walk_speed */
#define WALK_SPEED_MOD 55     /* the Walk mod's */

/* The plain two-screen frame of khdays_host_present (when there are no frame
 * packages). */
static SDL_Mutex *frame_lock;
static uint32_t frame[2 * W * H];
static SDL_AtomicInt frame_new;     /* set by khdays_host_present, cleared by the window */
static SDL_AtomicInt frame_pending; /* a frame event is already on its way */
static Uint32 frame_event;          /* our SDL event: "a new frame is ready" */
static Uint64 last_redraw;          /* SDL_GetTicks of the last redraw */
#define MOVIE_REDRAW_MS 8           /* an HD video drawn at least this often (ms) */
#define MOVIE_REDRAW_LATE_MS 25     /* ... and, while events keep coming, at most this late (ms) */

/* The frame packages. There are three, so neither thread ever waits for the
 * other: the game thread fills one, the newest finished one waits in
 * `mailbox`, and the window shows the third. `mailbox` holds the waiting
 * package's index, with FRESH set until the window takes it. Only the window
 * clears FRESH, so a swap never loses a package either thread holds. */
enum { FRESH = 4 };
static KhdaysFramePackage *packages[3];
static int game_slot;         /* the game thread's */
static int host_slot = 1;     /* the host thread's */
static SDL_AtomicInt mailbox; /* = 2 at the start */
/* What the window asks the game thread to put in each package. */
static SDL_AtomicInt wants;   /* KHDAYS_WANT_* */
static SDL_AtomicInt columns; /* how wide the 3D should be, in DS pixels (256 is the DS's width) */
static SDL_AtomicInt scale;   /* the internal resolution the picture is composed at */

static SDL_Mutex *input_lock;
static KhdaysInputSnapshot input;
/* Presses and releases since the game's last read of the pad (press_edges.h), under input_lock. */
static KhdaysPressEdges edges, edges_field;
/* The DS buttons pressed since the window's last look for the HD movies' pause panel (even let go
 * again since), under input_lock: a quick tap is never lost however long a look takes. */
static uint16_t pause_taps;

static SDL_Semaphore *ready; /* signalled when the window is up (or failed) */
static SDL_AtomicInt started;
static SDL_Semaphore *closed; /* signalled when the window and the sound are gone */
static Uint32 close_event;    /* our SDL event: khdays_host_close's request */
static Uint32 wake_event;     /* our SDL event: khdays_host_wake's, the loop looks again */

/* Stores the latest input for the game thread to read. */
static void publish(const KhdaysInputSnapshot *snapshot)
{
    SDL_LockMutex(input_lock);
    const uint32_t generation = input.generation + 1;
    khdays_press_edges_note(&edges, input.ds_buttons, snapshot->ds_buttons);
    khdays_press_edges_note(&edges_field, input.ds_buttons_field, snapshot->ds_buttons_field);
    pause_taps |= (uint16_t)(snapshot->ds_buttons & ~input.ds_buttons);
    input = *snapshot;
    input.generation = generation;
    SDL_UnlockMutex(input_lock);
}

/* Reports a problem with the settings file or a controller: in the log, and
 * on the settings menu's notice line. */
static void notice(const char *text)
{
    fprintf(stderr, "host: %s\n", text);
    khdays_settings_add_notice(text);
}

/* Tells the game thread what to put in the frame packages for the picture
 * the window shows now (it reads this at its next VBlank). */
static void tell_game(void)
{
    SDL_SetAtomicInt(&wants, khdays_present_wants());
    SDL_SetAtomicInt(&columns, khdays_present_columns());
    SDL_SetAtomicInt(&scale, khdays_present_scale());
}

/* The window as [video] display says (profile.h KhdaysDisplay): a window, one without borders
 * over its screen (SDL's full screen at the desktop's mode, NULL), or the screen the game's alone
 * at the desktop's mode. */
static SDL_Window *host_window;
static int last_full = KHDAYS_DISPLAY_BORDERLESS; /* the full screen Alt+Enter goes back to */

static void display_now(int display)
{
    static int shown = -1;
    if (host_window == NULL || display == shown) {
        return;
    }
    shown = display;
    if (display != KHDAYS_DISPLAY_WINDOW) {
        last_full = display;
    }
    if (display == KHDAYS_DISPLAY_WINDOW) {
        SDL_SetWindowFullscreen(host_window, false);
        SDL_SetWindowBordered(host_window, true);
    } else {
        const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(host_window));
        SDL_SetWindowFullscreenMode(host_window, display == KHDAYS_DISPLAY_FULLSCREEN ? desktop : NULL);
        SDL_SetWindowFullscreen(host_window, true);
    }
    SDL_SyncWindow(host_window);
    khdays_log("window: %s", display == KHDAYS_DISPLAY_WINDOW       ? "a window"
                             : display == KHDAYS_DISPLAY_BORDERLESS ? "a borderless window over the screen"
                                                                    : "full screen");
}

/* Applies a change in the menu's Video section at once: the picture behind
 * the menu is drawn again from the last frame with the new settings. (The game
 * stays paused; if the last frame did not carry the 3D, the change shows from
 * the next frame the game draws.) */
static void video_now(void)
{
    static KhdaysVideo applied;
    const KhdaysVideo *v = &khdays_settings_profile()->video;
    if (memcmp(v, &applied, sizeof(applied)) == 0) {
        return;
    }
    applied = *v;
    display_now(v->display);
    khdays_present_set_video(v);
    tell_game();
    if (packages[0] != NULL) {
        khdays_present_package(packages[host_slot]);
    }
}

/* The text of the ping display ([network] show_ping), to SDL_free; NULL when
 * it is off or outside a session. It lists each member's round trip, joined
 * by " / " ("-" until measured, "<1" under half a millisecond). While the
 * menu is open its setting counts, so a change shows at once. */
static char *ping_line(void)
{
    const KhdaysProfile *p = khdays_settings_is_open() ? khdays_settings_profile() : khdays_devices_profile();
    unsigned int *aids;
    int *ms, count, shown = 0;
    size_t size;
    char *numbers, *line;
    if (!p->network.show_ping || !khdays_net_in_session()) {
        return NULL;
    }
    count = khdays_net_round_trips(NULL, NULL, 0);
    aids = (unsigned int *)SDL_malloc(sizeof(*aids) * (size_t)(count > 0 ? count : 1));
    ms = (int *)SDL_malloc(sizeof(*ms) * (size_t)(count > 0 ? count : 1));
    size = 16 * (size_t)(count > 0 ? count : 1) + 2; /* at most "-2147483648 / " per member */
    numbers = (char *)SDL_malloc(size);
    if (aids == NULL || ms == NULL || numbers == NULL) {
        SDL_free(aids);
        SDL_free(ms);
        SDL_free(numbers);
        return NULL;
    }
    numbers[0] = '\0';
    /* members who joined since the count wait for the next picture */
    count = SDL_min(count, khdays_net_round_trips(aids, ms, count));
    for (int i = 0; i < count; ++i) {
        char one[16];
        if (ms[i] < 0) {
            SDL_strlcpy(one, "-", sizeof(one));
        } else if (ms[i] == 0) {
            SDL_strlcpy(one, "<1", sizeof(one)); /* under half a millisecond: copies on the same PC */
        } else {
            SDL_snprintf(one, sizeof(one), "%d", ms[i]);
        }
        if (shown++ > 0) {
            SDL_strlcat(numbers, " / ", size);
        }
        SDL_strlcat(numbers, one, size);
    }
    SDL_asprintf(&line, khdays_text(TXT_PING_LINE), shown > 0 ? numbers : "-");
    SDL_free(aids);
    SDL_free(ms);
    SDL_free(numbers);
    return line;
}

/* The mods' notices shown now (runtime/scripts/view.h), each for five seconds; each also kept for the
 * settings menu's notice line. */
typedef struct ModNotice {
    char *line;
    Uint64 until;
} ModNotice;
static ModNotice *mod_notices;
static int mod_notice_count, mod_notice_capacity;

static void take_mod_notices(void)
{
    const Uint64 now = SDL_GetTicks();
    int kind, kept = 0;
    char *mod, *text;
    for (int i = 0; i < mod_notice_count; ++i) {
        if (mod_notices[i].until > now) {
            mod_notices[kept++] = mod_notices[i];
        } else {
            SDL_free(mod_notices[i].line);
        }
    }
    mod_notice_count = kept;
    while (khdays_scripts_take_notice(&kind, &mod, &text)) {
        static const int formats[] = {[KHDAYS_NOTICE_STOPPED] = TXT_MOD_STOPPED,
                                      [KHDAYS_NOTICE_PAUSED] = TXT_MOD_PAUSED,
                                      [KHDAYS_NOTICE_REFUSED] = TXT_MOD_REFUSED};
        char said[400], *line = NULL;
        if (kind == KHDAYS_NOTICE_SAID) {
            SDL_snprintf(said, sizeof(said), "%s", text);
        } else {
            SDL_snprintf(said, sizeof(said), khdays_text(formats[kind]), text);
        }
        if (SDL_asprintf(&line, "%s: %s", mod, said) >= 0) {
            khdays_settings_add_notice(line);
            if (mod_notice_count == mod_notice_capacity) {
                const int grown = mod_notice_capacity == 0 ? 4 : mod_notice_capacity * 2;
                ModNotice *more = SDL_realloc(mod_notices, (size_t)grown * sizeof(*more));
                if (more != NULL) {
                    mod_notices = more;
                    mod_notice_capacity = grown;
                }
            }
            if (mod_notice_count < mod_notice_capacity) {
                mod_notices[mod_notice_count].line = line;
                mod_notices[mod_notice_count].until = now + 5000;
                ++mod_notice_count;
            } else {
                SDL_free(line);
            }
        }
        free(mod);
        free(text);
    }
}

/* The DS buttons pressed since the last call (pause_taps), taken. */
static uint16_t take_pause_taps(void)
{
    uint16_t taps;
    if (input_lock == NULL) {
        return 0;
    }
    SDL_LockMutex(input_lock);
    taps = pause_taps;
    pause_taps = 0;
    SDL_UnlockMutex(input_lock);
    return taps;
}

/* The menu sounds of the HD movies' pause panel (the game's se_sys_*, settings_sample.h): opening,
 * the cursor moving, a choice made, backing out. Looked at once a pass of the loop. */
static void pause_panel_sounds(void)
{
    static int was_open, was_row;
    int row = 0, can_skip = 0;
    const int open = khdays_movie_pause_menu(&row, &can_skip);
    const int by_choice = khdays_movie_pause_closed_by_choice();
    if (open && !was_open) {
        khdays_menu_cue(KHDAYS_SETTINGS_CUE_OPEN);
    } else if (!open && was_open) {
        khdays_menu_cue(by_choice ? KHDAYS_SETTINGS_CUE_ACCEPT : KHDAYS_SETTINGS_CUE_BACK);
    } else if (open && row != was_row) {
        khdays_menu_cue(KHDAYS_SETTINGS_CUE_CURSOR);
    }
    was_open = open;
    was_row = row;
}

/* A day's monologue heard in the theater's voice (runtime/movies/scene_trace.c): the theater's
 * subtitles of what it says, at the voice's clock, over the game's page -- the video named by the
 * voice's clock, loaded when it changes. */
static void draw_voice_subtitles(SDL_Renderer *renderer)
{
    static char loaded[64] = "";
    char name[64] = "";
    double seconds = 0.0;
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    if (!khdays_subtitles_shown() || !khdays_hd_voice_clock(name, sizeof(name), &seconds)) {
        if (loaded[0] != '\0') {
            loaded[0] = '\0';
            khdays_movie_subtitles_load("", 0); /* none */
        }
        return;
    }
    if (strcmp(name, loaded) != 0) {
        const int chosen = khdays_language_choose(khdays_devices_profile()->language);
        snprintf(loaded, sizeof(loaded), "%s", name);
        khdays_movie_subtitles_load(strncmp(name, "hd", 2) == 0 ? name + 2 : name,
                                    chosen >= 1 && chosen <= 5 ? chosen : 1);
    }
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    khdays_movie_subtitles_draw(renderer, seconds, 0.0, 1920, 1080);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
}

/* A port's screen with the swaying glove showed at the last picture (save, chapters, rooms, camp, shop). */
static int glove_screen;

/* The theater's music's lines (host/movies/theater_music.c) in the session log. */
static void theater_music_line(const char *line)
{
    khdays_log("theater music: %s", line);
}

/* Draws the last frame, with the FPS and ping displays and the settings menu
 * over it. Draws nothing while the graphics card is lost (the loop recreates
 * it). */
static void redraw(SDL_Window *window, SDL_Renderer *renderer)
{
    int pictures, frames, fps, corner_rows;
    char *ping;
    const Uint64 began = SDL_GetTicksNS();
    Uint64 drawn;
    last_redraw = SDL_GetTicks();
    if (khdays_present_lost()) {
        return;
    }
    khdays_present_draw();
    {
        static int logging;
        if (!logging) {
            logging = 1;
            khdays_theater_music_log_to(theater_music_line);
            khdays_theater_music_overhaul(khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL));
        }
    }
    khdays_theater_music_poll(); /* the theater's music kept for a next scene's video, or let go */
    /* (the port's lists sway their glove: drawn at the display's rate while one shows, glove_screen) */
    glove_screen =
        khdays_save_screen_draw(renderer); /* the port's save list over the game's (runtime/menus/savelist.h) */
    glove_screen |=
        khdays_cine_screen_draw(renderer); /* MODO CINE's list as the HD theater's (runtime/menus/cinelist.h) */
    khdays_day_screen_draw(renderer); /* the day's card as the HD theater's chapter days (runtime/menus/daycard.h) */
    glove_screen |=
        khdays_room_screen_draw(renderer); /* the port's list of rooms over the game's (runtime/net/roomlist.h) */
    khdays_select_screen_draw(renderer);   /* Mission Mode's character select on one screen (charselect.h) */
    /* the Moogle Shop on one screen (shop.h): before the camp's, whose black comes over it when Mission
     * Mode's shop opens from its menu or closes back to it */
    glove_screen |= khdays_shop_screen_draw(renderer);
    glove_screen |= khdays_camp_screen_draw(renderer);      /* Mission Mode's camp menu on one screen (camp.h) */
    glove_screen |= khdays_shop_screen_draw_over(renderer); /* Mission Mode's shop and menu into each other */
    khdays_mod_screen_draw(renderer); /* a mod's screen, the title's mods' prompt (runtime/scripts/screens.h) */
    if (khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL) && !khdays_movie_hd_showing()) {
        draw_voice_subtitles(renderer); /* a monologue's voice: what it says */
    }
    fps = khdays_present_fps(&pictures, &frames);
    if (fps) {
        khdays_settings_fps_draw(renderer, pictures, frames);
    }
    ping = ping_line();
    corner_rows = (fps != 0) + (ping != NULL);
    if (ping != NULL) {
        khdays_settings_ping_draw(renderer, ping, fps);
        SDL_free(ping);
    }
    take_mod_notices();
    for (int i = 0; i < mod_notice_count; ++i) {
        khdays_settings_corner_draw(renderer, mod_notices[i].line, corner_rows + i);
    }
    khdays_movie_pause_draw(renderer);
    khdays_settings_draw(renderer, window);
    drawn = SDL_GetTicksNS();
    SDL_RenderPresent(renderer);
    khdays_present_redraw_took(drawn - began, SDL_GetTicksNS() - drawn); /* (the shop's timings) */
}

/* Whether the open settings menu paused the game. In a multiplayer session
 * it does not, since the other players' games go on. */
static int settings_stopped;

/* Keeps the subtitles the new game's question chose (runtime/menus/newgame.h): in the profile, the
 * picture's settings and the settings file. */
static void keep_subtitles(void)
{
    KhdaysProfile p;
    char path[512];
    int on;
    if (!khdays_newgame_take_answer(&on) || khdays_devices_profile()->video.subtitles == on) {
        return;
    }
    p = *khdays_devices_profile();
    p.video.subtitles = on;
    khdays_devices_set_profile(&p);
    khdays_present_set_video(&p.video);
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(&p, path, notice);
}

/* Keeps Chase Camera's Free as the Config's page chose it (runtime/menus/camp.h): in the profile, the game's input
 * and the settings file. */
static void keep_chase_free(void)
{
    KhdaysProfile p;
    char path[512];
    int on;
    if (!khdays_camp_chase_free_chosen(&on) || khdays_devices_profile()->enh.chase_free == on) {
        return;
    }
    p = *khdays_devices_profile();
    p.enh.chase_free = on;
    khdays_devices_set_profile(&p);
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(&p, path, notice);
}

/* Alt+Enter, as on most PC games: from a window to the full screen chosen last (the borderless
 * window if none), from either full screen to a window; kept in the profile and the settings file
 * as the menu keeps it. */
static void toggle_display(void)
{
    KhdaysProfile p = *khdays_devices_profile();
    char path[512];
    p.video.display = p.video.display == KHDAYS_DISPLAY_WINDOW ? last_full : KHDAYS_DISPLAY_WINDOW;
    khdays_devices_set_profile(&p);
    display_now(p.video.display);
    khdays_present_set_video(&p.video);
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(&p, path, notice);
}

static int is_alt_enter(const SDL_Event *e)
{
    return e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && (e->key.mod & SDL_KMOD_ALT) &&
           (e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_KP_ENTER);
}

/* Writes the settings file from the menu's profile as it is now. */
static void save_settings(void)
{
    char path[512];
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(khdays_settings_profile(), path, notice);
}

/* Applies each change in the open menu as it is made: each part of the profile goes where it would go
 * when the menu closes. (The game gets no input while the menu is open, so
 * new controls take effect from the first press after it closes; the menu
 * itself keeps the buttons it opened with.) */
static KhdaysSound heard; /* the sound settings applied last */

static void apply_changes(void)
{
    const unsigned changes = khdays_settings_take_changes();
    const KhdaysProfile *p = khdays_settings_profile();
    if (changes & KHDAYS_CHANGE_CONTROLS) {
        khdays_devices_set_profile(p);
    }
    if (changes & KHDAYS_CHANGE_VIDEO) {
        video_now(); /* the picture behind the menu follows */
    }
    if (changes & KHDAYS_CHANGE_SOUND) {
        khdays_sound_output_set(&p->sound);
        if (settings_stopped) {
            /* the game is paused, so play a sample of the sound whose volume changed */
            khdays_settings_sample(&heard, &p->sound);
        }
        heard = p->sound;
    }
    if (changes & KHDAYS_CHANGE_NETWORK) {
        save_settings();          /* the nickname is read back from the file */
        khdays_boot_owner_name(); /* for the next multiplayer session the game starts */
    }
    if (changes & KHDAYS_CHANGE_LANGUAGE) {
        /* the menu's text changes at once, the game's when the current scene
         * ends (runtime/core/language.c); KHDAYS_LANGUAGE still takes precedence */
        khdays_language_request(khdays_language_choose(p->language));
    }
    if (changes & KHDAYS_CHANGE_LOG) {
        khdays_log_want(p->session_log);
    }
}

/* While the settings menu shows, the game pauses: its time stops, it gets no input and its sound
 * is silent. In a multiplayer session it only stops getting input. */
static void stop_game_for_menu(void)
{
    khdays_devices_set_game_input(0);
    settings_stopped = !khdays_net_in_session();
    if (settings_stopped) {
        khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1);
        khdays_sound_output_pause(1);
        khdays_hd_pause(1);       /* an HD video playing for a movie pauses too */
        khdays_hd_voice_pause(1); /* and a monologue's voice */
    }
}

/* Opens the menu with only the notice of a newer release (host/app/updates.h). */
static void open_update_notice(const char *tag)
{
    static const KhdaysUpdateHooks hooks = {khdays_updates_can_install, khdays_updates_download,
                                            khdays_updates_download_state, khdays_updates_download_cancel,
                                            khdays_updates_install};
    khdays_log("updates: telling the player of %s", tag);
    stop_game_for_menu();
    khdays_settings_offer_update(khdays_devices_profile(), khdays_updates_version(), tag, &hooks);
}

static void close_settings(void);

/* The menu has closed: acts on the update notice's answer, and closes the game once the update's
 * installer runs (it replaces the game's files). */
static void settings_closed(SDL_Window *window, SDL_Renderer *renderer)
{
    const int update = khdays_settings_take_update_answer();
    close_settings();
    if (update >= 0) {
        khdays_updates_answer(update); /* the notice's: the page opened, or not told again */
    }
    if (update == KHDAYS_UPDATE_INSTALL) {
        khdays_log("updates: the game closes for its installer");
        _Exit(0);
    }
    redraw(window, renderer);
}

/* With the Menu Overhaul, whether the settings menu may open now: in the game's menus and pauses (a situation
 * the port does not know taken as one), not in the middle of play -- the field, Sora's memories, the DS's
 * movies, the day's card, the end -- as the one-screen picture decided last. With two screens: always. */
static int settings_allowed(void)
{
    static int refused_said;
    int allowed = 1;
    if (khdays_devices_profile()->video.one_screen) {
        switch (khdays_onescreen_scene()) {
        case KHDAYS_SIT_FIELD:
        case KHDAYS_SIT_FIELD_3D_BOTH:
        case KHDAYS_SIT_MOVIE:
        case KHDAYS_SIT_DAY_COUNTER:
        case KHDAYS_SIT_THOUGHTS:
        case KHDAYS_SIT_THE_END: allowed = 0; break;
        default: break;
        }
    }
    if (!allowed && !refused_said) {
        refused_said = 1;
        khdays_log("settings: not opened in the middle of play (%s); the game's menus and pauses open it",
                   khdays_situation_name(khdays_onescreen_scene()));
    }
    return allowed;
}

/* Opens the settings menu (the game pauses, stop_game_for_menu). */
static void open_settings(void)
{
    char text[160];
    stop_game_for_menu();
    while (khdays_devices_take_notice(text, sizeof(text))) {
        khdays_settings_add_notice(text);
    }
    khdays_settings_set_situation(!settings_stopped, khdays_settings_sample_has_movies());
    khdays_settings_set_guest(khdays_net_own_aid() > 0);
    khdays_settings_open(khdays_devices_profile());
    heard = khdays_settings_profile()->sound;
    khdays_scripts_ask(KHDAYS_ASK_SETTINGS_OPENED, NULL, NULL, 0); /* the mods hear it */
}

/* Closes the settings menu: saves and applies what changed, and lets the game
 * run again. */
static void close_settings(void)
{
    apply_changes();
    khdays_scripts_ask(KHDAYS_ASK_SETTINGS_CLOSED, NULL, NULL, 0);
    khdays_settings_sample_stop();
    if (khdays_settings_changed()) {
        khdays_devices_set_profile(khdays_settings_profile()); /* as closing may have fixed it */
        save_settings();
        khdays_present_set_video(&khdays_settings_profile()->video);
        khdays_sound_output_set(&khdays_settings_profile()->sound);
        khdays_log_want(khdays_settings_profile()->session_log);
        khdays_savelist_set_settings(khdays_settings_profile()->save.all_files,
                                     khdays_settings_profile()->save.autosave);
        khdays_boot_owner_name(); /* the nickname, for the next multiplayer session */
        tell_game();
    }
    /* Whatever is still held (the button that closed the menu) only counts for
     * the game once it has been released, and the settings button that closed
     * the menu does not reopen it. */
    khdays_devices_set_game_input(1);
    (void)khdays_devices_take_settings_request();
    if (settings_stopped) {
        khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
        khdays_sound_output_pause(0);
        {
            /* an HD video's pause still up (the menu opened from it), or one its course keeps paused: the
             * video stays paused */
            int row, skip;
            if (!khdays_movie_hd_course_paused() && !khdays_movie_pause_menu(&row, &skip)) {
                khdays_hd_pause(0);
            }
        }
        khdays_hd_voice_pause(0);
        settings_stopped = 0;
    }
}

/* The mouse pointer is hidden over the game while its window has the focus (the touch screen is not
 * used) and shown over the settings menu or when another window has the focus. With the mouse camera
 * on, the window also holds the mouse while the game has the input (devices.c). */
static void mouse_follow(SDL_Window *window)
{
    int row, skip;
    const int menu = khdays_settings_is_open() || khdays_movie_pause_menu(&row, &skip);
    const int focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const int hold = !menu && khdays_devices_profile()->enh.mouse_camera && focused;
    const int shown = menu || !focused;
    if (hold != (int)SDL_GetWindowRelativeMouseMode(window)) {
        SDL_SetWindowRelativeMouseMode(window, hold != 0);
    }
    if (shown != (int)SDL_CursorVisible()) {
        if (shown) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }
}

/* Whether this event opens the settings menu (which is closed now). */
static int opens_settings(const SDL_Event *event, SDL_Renderer *renderer)
{
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return 1;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT &&
        ((khdays_present_settings_button() && khdays_present_settings_button_at(event->button.x, event->button.y)) ||
         khdays_movie_pause_settings_at(renderer, event->button.x, event->button.y))) {
        return 1; /* the picture's own settings key (the title's), or the one a pause shows */
    }
    return khdays_devices_take_settings_request();
}

/* The settings key pressed: the settings menu. With the Menu Overhaul, over an HD video or a monologue it opens
 * that mod's pause (as START does), and with the pause up the settings menu, the video staying paused under it;
 * elsewhere the settings menu where settings_allowed lets it. */
static void settings_key(void)
{
    int row, skip;
    if (!khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        open_settings();
    } else if (khdays_movie_pause_menu(&row, &skip)) {
        open_settings();
    } else if (khdays_movie_hd_showing()) {
        khdays_movie_pause_toggle();
    } else if (khdays_monologue_running()) {
        khdays_monologue_pause_toggle(); /* a day's monologue: its pause first, as a movie's */
    } else if (settings_allowed()) {
        open_settings();
    }
}

/* Creates the window: the DS's two screens at 2x (one above the other, or
 * side by side), or with one picture the largest whole multiple of 342 x 192
 * (16:9) that fits in three quarters of the desktop. */
static SDL_Window *open_window(const KhdaysProfile *profile)
{
    const int beside = profile->video.two_screens == KHDAYS_TWO_SIDE_BY_SIDE;
    int w = beside ? 4 * W : 2 * W, h = beside ? 2 * H : 4 * H;
    SDL_Rect desk;
    if (profile->video.one_screen && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &desk)) {
        const int kx = desk.w * 3 / 4 / 342, ky = desk.h * 3 / 4 / H;
        const int k = kx < ky ? kx : ky;
        w = 342 * (k > 1 ? k : 1);
        h = H * (k > 1 ? k : 1);
    }
    return SDL_CreateWindow("Kingdom Hearts 358/2 Days (native)", w, h, SDL_WINDOW_RESIZABLE);
}

/* The window's thread: opens everything, then handles SDL's events until the
 * window closes. */
static int host_thread(void *unused)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    KhdaysProfile profile;
    (void)unused;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "host: SDL_Init failed: %s\n", SDL_GetError());
        SDL_SignalSemaphore(ready);
        return 1;
    }
    khdays_sound_output_open();
    {
        /* The player's settings (or the defaults, the first time). Read first,
         * so that the window speaks the player's language. */
        char path[512];
        khdays_settings_path(path, sizeof(path));
        khdays_profile_load(&profile, path, notice);
        if (khdays_mod_installed(KHDAYS_MOD_WALK) && profile.enh.walk_speed == WALK_SPEED_DEFAULT) {
            /* the Walk mod's walk is timed for 55% of the game's speed: the setting set so once (a value of the
             * player's own, any but the port's default, kept) */
            profile.enh.walk_speed = WALK_SPEED_MOD;
            khdays_profile_save(&profile, path, notice);
        }
        khdays_ui_set_language(khdays_language_choose(profile.language));
        khdays_sound_output_set(&profile.sound);
        khdays_log_want(profile.session_log);
        khdays_savelist_set_settings(profile.save.all_files, profile.save.autosave);
        khdays_stop_set_enabled(profile.gameplay.stop_magic);     /* for this run: a change waits for the next */
        khdays_plugins_set_native_code(profile.mods.native_code); /* the same */
    }
    window = open_window(&profile);
    host_window = window;
    display_now(profile.video.display);
    khdays_present_set_video(&profile.video);
    renderer = window ? khdays_present_open(window, notice) : NULL;
    frame_event = SDL_RegisterEvents(3);
    close_event = frame_event + 1;
    wake_event = frame_event + 2;
    if (renderer == NULL || frame_event == 0) {
        fprintf(stderr, "host: cannot open the window: %s\n", SDL_GetError());
        SDL_SignalSemaphore(ready);
        return 1;
    }
    khdays_devices_init(&profile, publish);
    khdays_settings_set_graphics_card(khdays_present_gpu());
    tell_game();
    khdays_updates_start(profile.network.check_updates); /* in the background; told of on the title */
    SDL_SetAtomicInt(&started, 1);
    SDL_SignalSemaphore(ready);

    for (;;) {
        SDL_Event event;
        /* While the settings menu shows, it keeps animating even if the paused
         * game sends no frames. */
        int animating;
        int menu_row, menu_skip; /* the pause panel's (khdays_movie_pause_menu) */
        /* A newer release is told of on the front-end (the boot logos, the title: scene 1), never
         * in the middle of the game, and once a run. */
        if (!khdays_settings_is_open() && khdays_runtime_scene() == 1) {
            char tag[64];
            if (khdays_updates_offer(tag, sizeof(tag))) {
                open_update_notice(tag);
                redraw(window, renderer);
            }
        }
        animating = khdays_settings_is_open();
        khdays_devices_frame(); /* a wheel's notch let go */
        mouse_follow(window);
        /* The 3D at the monitor's refresh rate: draw a picture
         * whenever VSync lets one through, between events. */
        const int smooth = khdays_present_smooth();
        /* An HD video playing for a movie is drawn at the display's
         * refresh rate whether or not the game sends frames (the game waits at
         * the movie's end). Here we watch for its end and pass on START, which
         * the waiting game thread cannot read. */
        const int movie = khdays_movie_hd_running();
        /* So, with the Menu Overhaul, are the game's pause drawn as KH2's (game_pause.h: the paused game
         * sends a picture a second or so, its PAUSE and glove move on), a port's screen with its swaying
         * glove (glove_screen) and a monologue's pause */
        const int monologue = !movie && khdays_monologue_running();
        const int kh2_pause = !movie && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL) &&
                              (khdays_game_pause_kh2_showing() || glove_screen ||
                               (monologue && khdays_movie_pause_menu(&menu_row, &menu_skip)));
        /* So is an animated backdrop (the title's, ui/composition.h): its pieces move while the
         * game's still menu sends no frames. */
        const int backdrop = khdays_present_backdrop_moving();
        /* So is a situation still settling on a still picture (a pause opened where the 3D was
         * on both screens): it is decided again as the game's VBlanks pass. */
        const int settling = khdays_present_settling();
        keep_subtitles();
        keep_chase_free();
        if (!khdays_settings_is_open()) {
            khdays_cine_screen_buttons(khdays_cinelist_seen()); /* MODO CINE's chapters, the port's list */
        }
        {
            /* taken on every pass, so presses from before a movie never reach its pause */
            const uint16_t taps = take_pause_taps();
            if ((movie || monologue) && !khdays_settings_is_open()) {
                /* (a day's monologue too: with the Menu Overhaul its START opens the pause panel) */
                khdays_media_host_presses(khdays_host_buttons(), taps, monologue);
            }
        }
        pause_panel_sounds();
        if (kh2_pause && !khdays_settings_is_open()) {
            const Uint64 since = SDL_GetTicks() - last_redraw;
            if (since >= MOVIE_REDRAW_MS &&
                (since >= MOVIE_REDRAW_LATE_MS || !SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST))) {
                redraw(window, renderer);
            }
        }
        if (movie && !khdays_settings_is_open()) {
            khdays_movie_hd_poll();
            /* drawn on time even while events keep coming (a gamepad's stream of them): the game,
             * held for a scene's video, sends no frames that would draw it */
            /* but never one picture per event: a picture waits for the display's refresh (~16 ms), and a
             * gamepad's or a mouse's stream of events (hundreds a second, SDL sending a joystick's and a
             * gamepad's of each) then queued faster than one a picture could take them, the pause's
             * presses seconds -- minutes -- behind. The waiting events first, a picture at least every
             * MOVIE_REDRAW_LATE_MS however many keep coming. */
            const Uint64 since = SDL_GetTicks() - last_redraw;
            if (since >= MOVIE_REDRAW_MS &&
                (since >= MOVIE_REDRAW_LATE_MS || !SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST))) {
                redraw(window, renderer);
            }
        }
        if (khdays_present_lost()) {
            /* the port noticed it itself (SDL's renderer does not on D3D12):
             * handle it like SDL's event */
            SDL_zero(event);
            event.type = SDL_EVENT_RENDER_DEVICE_LOST;
        } else if (!(smooth || movie || backdrop || kh2_pause ? SDL_WaitEventTimeout(&event, 1)
                     : settling  ? SDL_WaitEventTimeout(&event, 4) /* (a VBlank is ~17 ms) */
                     : animating ? SDL_WaitEventTimeout(&event, 50)
                                 : SDL_WaitEvent(&event))) {
            if (animating) {
                khdays_settings_tick();
                if (khdays_settings_is_open()) {
                    apply_changes(); /* a held direction keeps repeating its change */
                } else {
                    settings_closed(window, renderer); /* by itself: the update's installer runs */
                    continue;
                }
            }
            if ((smooth && khdays_present_refresh()) || (backdrop && khdays_present_refresh_backdrop()) ||
                (settling && khdays_present_refresh_settling()) || animating || movie || kh2_pause) {
                redraw(window, renderer);
            }
            continue;
        }
        mouse_follow(window);
        if (event.type == SDL_EVENT_QUIT) {
            if (khdays_settings_is_open() && khdays_settings_profile_changed()) {
                save_settings(); /* closed with the menu open: keep what was changed */
            }
            khdays_plugins_quit(1000); /* stopped on the game's thread, if it answers within a second */
            _Exit(0);
        }
        if (event.type == close_event) {
            if (khdays_settings_is_open()) {
                khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0); /* a reset while the menu was open */
                if (khdays_settings_profile_changed()) {
                    save_settings(); /* keep what was changed, as closing the menu would */
                }
            }
            khdays_sound_output_close();
            khdays_hd_voice_stop(); /* a monologue's voice still heard */
            khdays_ui_font_forget();
            khdays_settings_renderer_gone();
            khdays_save_screen_renderer_gone();
            khdays_cine_screen_renderer_gone();
            khdays_day_screen_renderer_gone();
            khdays_room_screen_renderer_gone();
            khdays_mod_screen_renderer_gone();
            khdays_select_screen_renderer_gone();
            khdays_camp_screen_renderer_gone();
            khdays_shop_screen_renderer_gone();
            khdays_movie_pause_renderer_gone();
            khdays_modern_pause_renderer_gone();
            khdays_present_close();
            SDL_DestroyWindow(window);
            SDL_Quit();
            SDL_SignalSemaphore(closed);
            return 0;
        }
        if (event.type == frame_event) {
            /* clear this first, so a frame published from now on sends its own event */
            SDL_SetAtomicInt(&frame_pending, 0);
            if (SDL_GetAtomicInt(&mailbox) & FRESH) {
                host_slot = SDL_SetAtomicInt(&mailbox, host_slot) & 3;
                khdays_present_package(packages[host_slot]);
            } else if (SDL_SetAtomicInt(&frame_new, 0)) {
                SDL_LockMutex(frame_lock);
                khdays_present_screens(frame);
                SDL_UnlockMutex(frame_lock);
            } else {
                continue; /* already taken with an earlier event */
            }
            tell_game(); /* (a scale the graphics card refused changes what the game should fill) */
            redraw(window, renderer);
            continue;
        }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            khdays_present_resized(); /* settings set to "window" follow the new size */
            tell_game();
        }
        if (event.type == SDL_EVENT_RENDER_DEVICE_RESET || event.type == SDL_EVENT_RENDER_DEVICE_LOST) {
            int renewed;
            khdays_ui_font_forget(); /* free its textures before their renderer goes */
            khdays_settings_renderer_gone();
            khdays_save_screen_renderer_gone();
            khdays_cine_screen_renderer_gone();
            khdays_day_screen_renderer_gone();
            khdays_room_screen_renderer_gone();
            khdays_mod_screen_renderer_gone();
            khdays_select_screen_renderer_gone();
            khdays_camp_screen_renderer_gone();
            khdays_shop_screen_renderer_gone();
            khdays_movie_pause_renderer_gone();
            khdays_modern_pause_renderer_gone();
            renderer = khdays_present_event(&event, &renewed);
            khdays_settings_set_graphics_card(khdays_present_gpu());
            tell_game();
            redraw(window, renderer);
            continue;
        }

        /* Alt+Enter: the window or the full screen; neither the game's Enter nor the menu's (the
         * menu has its own Display mode, and its profile would undo a change made under it) */
        if (is_alt_enter(&event)) {
            if (!khdays_settings_is_open()) {
                toggle_display();
                tell_game();
            }
            continue;
        }

        /* Input. The devices always see it (they track what is held). While the
         * settings menu is open, the menu gets the input and the game none. */
        if (khdays_settings_is_open()) {
            /* One picture for the events waiting, not one each: a gamepad's sticks and
             * triggers send their moves many times a frame (and SDL each twice, as a joystick's
             * and a gamepad's), and a picture each -- every one waiting for the display's
             * refresh -- left the menu behind the player's presses. At least a picture a frame
             * while they keep coming. */
            static Uint64 drawn_at;
            khdays_devices_handle(&event, renderer);
            if (khdays_settings_handle(&event, window)) {
                settings_closed(window, renderer);
                continue;
            }
            khdays_settings_tick(); /* also while events keep coming (a gamepad's stream of them) */
            if (!khdays_settings_is_open()) {
                settings_closed(window, renderer);
                continue;
            }
            apply_changes();
            if (!SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST) || SDL_GetTicks() - drawn_at >= 16) {
                redraw(window, renderer);
                drawn_at = SDL_GetTicks();
            }
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F12) {
            khdays_log_mark();     /* F12 marks the session log: "the player saw something here" */
            khdays_display_mark(); /* and saves what the DS shows then (runtime/display/display.c) */
        }
        if (khdays_room_screen_event(&event)) {
            /* a field of the list of rooms takes what is typed (Escape there is its own); the
             * devices still track what is held */
            khdays_devices_handle(&event, renderer);
            continue;
        }
        if (opens_settings(&event, renderer)) {
            settings_key();
            redraw(window, renderer);
            continue;
        }
        if (khdays_movie_pause_event(renderer, &event)) {
            /* The pause owns this click. Publishing it as a bound game A
             * would take the row the click just chose a second time. Not drawn here: the
             * movie's pictures above draw it (a picture for each of a mouse's moves left the pause
             * behind the player). */
            continue;
        }
        khdays_devices_handle(&event, renderer); /* keys, mouse, gamepads, focus */
        for (int n = khdays_devices_take_hud_mode_request(); n > 0; --n) {
            khdays_situation_hud_mode_next(); /* the one-screen HUD mode */
            khdays_log("one screen: the HUD mode now %d (0 complete, 1 exploration, 2 none)",
                       khdays_situation_hud_mode());
        }
        if (khdays_devices_take_settings_request()) {
            settings_key();
            redraw(window, renderer);
        }
    }
}

void khdays_host_tell_no_data(const char *where, const char *broken)
{
    const int language = khdays_language_of_system();
    const char *format = khdays_text_in(broken != NULL ? TXT_GAME_DATA_BROKEN : TXT_NEEDS_GAME_DATA, language);
    char text[1600];
    if (format == NULL) {
        format = khdays_text_in(broken != NULL ? TXT_GAME_DATA_BROKEN : TXT_NEEDS_GAME_DATA, KHDAYS_LANGUAGE_ENGLISH);
    }
    if (broken != NULL) {
        SDL_snprintf(text, sizeof(text), format, where, broken);
    } else {
        SDL_snprintf(text, sizeof(text), format, where);
    }
    fprintf(stderr, "host: %s\n", text);
    fflush(stderr);
    if (getenv("KHDAYS_HEADLESS") == NULL) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kingdom Hearts 358/2 Days (native)", text, NULL);
    }
}

int khdays_host_start(void)
{
    if (frame_lock == NULL) {
        frame_lock = SDL_CreateMutex();
        input_lock = SDL_CreateMutex();
    }
    if (frame_lock == NULL || input_lock == NULL) {
        return 0;
    }
    if (getenv("KHDAYS_HEADLESS") != NULL) {
        /* No window and no devices: a KHDAYS_INPUT script plays instead. It
         * uses the enhancements (and their speeds) and whether mods' native code
         * runs from the settings file named by KHDAYS_SETTINGS, if any; the
         * regression walks name none, so they run with the defaults. */
        const char *path = SDL_getenv("KHDAYS_SETTINGS");
        if (path != NULL) {
            KhdaysProfile profile;
            KhdaysDeviceState nothing;
            khdays_profile_load(&profile, path, notice);
            khdays_plugins_set_native_code(profile.mods.native_code);
            khdays_bindings_reset(&nothing);
            SDL_LockMutex(input_lock);
            khdays_bindings_resolve(&profile, &nothing, &input);
            SDL_UnlockMutex(input_lock);
        }
        return 1;
    }
    if (packages[0] == NULL) {
        for (int n = 0; n < 3; ++n) {
            packages[n] = (KhdaysFramePackage *)SDL_calloc(1, sizeof(KhdaysFramePackage));
            if (packages[n] == NULL) {
                return 0;
            }
        }
        SDL_SetAtomicInt(&mailbox, 2);
    }
    ready = SDL_CreateSemaphore(0);
    closed = SDL_CreateSemaphore(0);
    if (ready == NULL || closed == NULL || SDL_CreateThread(host_thread, "host", NULL) == NULL) {
        return 0;
    }
    SDL_WaitSemaphore(ready);
    return SDL_GetAtomicInt(&started) != 0;
}

void khdays_host_close(void)
{
    SDL_Event event;
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_SetAtomicInt(&started, 0); /* accept no more frames */
    SDL_zero(event);
    event.type = close_event;
    if (SDL_PushEvent(&event)) {
        SDL_WaitSemaphoreTimeout(closed, 5000);
    }
}

void khdays_host_wake(void)
{
    SDL_Event event;
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_zero(event);
    event.type = wake_event;
    SDL_PushEvent(&event);
}

void khdays_host_present(const uint32_t *upper, const uint32_t *lower)
{
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_LockMutex(frame_lock);
    memcpy(frame, upper, W * H * 4);
    memcpy(frame + W * H, lower, W * H * 4);
    SDL_UnlockMutex(frame_lock);
    SDL_SetAtomicInt(&frame_new, 1);
    if (SDL_SetAtomicInt(&frame_pending, 1) == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = frame_event;
        SDL_PushEvent(&event);
    }
}

int khdays_host_frame_wants(void)
{
    return SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&wants) : 0;
}

float khdays_host_frame_widen(void)
{
    const int n = SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&columns) : 0;
    return n > 256 ? (float)n / 256.0f : 1.0f;
}

int khdays_host_frame_scale(void)
{
    const int n = SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&scale) : 1;
    return n < 1 ? 1 : n;
}

KhdaysFramePackage *khdays_host_frame_begin(void)
{
    return SDL_GetAtomicInt(&started) && packages[0] != NULL ? packages[game_slot] : NULL;
}

void khdays_host_frame_publish(KhdaysFramePackage *package)
{
    if (package != packages[game_slot]) {
        return;
    }
    game_slot = SDL_SetAtomicInt(&mailbox, game_slot | FRESH) & 3;
    if (SDL_GetAtomicInt(&started) && SDL_SetAtomicInt(&frame_pending, 1) == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = frame_event;
        SDL_PushEvent(&event);
    }
}

/* Copies the latest snapshot. Headless (no window), everything is 0. */
static void khdays_host_input(KhdaysInputSnapshot *out)
{
    if (input_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    SDL_LockMutex(input_lock);
    *out = input;
    SDL_UnlockMutex(input_lock);
}

void khdays_host_input_take(KhdaysInputSnapshot *out)
{
    if (input_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    SDL_LockMutex(input_lock);
    *out = input;
    out->ds_buttons = khdays_press_edges_take(&edges, input.ds_buttons);
    out->ds_buttons_field = khdays_press_edges_take(&edges_field, input.ds_buttons_field);
    SDL_UnlockMutex(input_lock);
}

uint16_t khdays_host_buttons(void)
{
    KhdaysInputSnapshot now;
    khdays_host_input(&now);
    return now.ds_buttons;
}

int khdays_host_touch(int *x, int *y)
{
    KhdaysInputSnapshot now;
    khdays_host_input(&now);
    if (!now.touching) {
        return 0;
    }
    *x = now.touch_x;
    *y = now.touch_y;
    return 1;
}
