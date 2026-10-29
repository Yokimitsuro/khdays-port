/* The player's settings (see profile.h): their defaults, the names used in
 * the settings file, the rules that keep a profile valid, and reading and
 * writing saves/settings.ini. The file's format is summed up in the header the
 * port writes at the top of the file. */
#include "profile.h"
#include "host.h" /* KHDAYS_ENH_* */
#include "instance.h"
#include "../screens/ui_text.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

_Static_assert(KHDAYS_SETTINGS_KEY == SDL_SCANCODE_ESCAPE, "the settings key is Esc");

static const char *const action_names[KHDAYS_ACT_COUNT] = {
    [KHDAYS_ACT_A] = "a",
    [KHDAYS_ACT_B] = "b",
    [KHDAYS_ACT_SELECT] = "select",
    [KHDAYS_ACT_START] = "start",
    [KHDAYS_ACT_RIGHT] = "right",
    [KHDAYS_ACT_LEFT] = "left",
    [KHDAYS_ACT_UP] = "up",
    [KHDAYS_ACT_DOWN] = "down",
    [KHDAYS_ACT_R] = "r",
    [KHDAYS_ACT_L] = "l",
    [KHDAYS_ACT_X] = "x",
    [KHDAYS_ACT_Y] = "y",
    [KHDAYS_ACT_LOCK_ON] = "lock_on",
    [KHDAYS_ACT_TARGET_NEXT] = "target_next",
    [KHDAYS_ACT_TARGET_PREV] = "target_prev",
    [KHDAYS_ACT_MENU_UP] = "menu_up",
    [KHDAYS_ACT_MENU_DOWN] = "menu_down",
    [KHDAYS_ACT_MENU_LEFT] = "menu_left",
    [KHDAYS_ACT_MENU_RIGHT] = "menu_right",
    [KHDAYS_ACT_OPEN_SETTINGS] = "open_settings",
    [KHDAYS_ACT_HUD_MODE] = "hud_mode",
};

static const char *const pad_names[KHDAYS_PAD_COUNT] = {
    [KHDAYS_PAD_SOUTH] = "south",
    [KHDAYS_PAD_EAST] = "east",
    [KHDAYS_PAD_WEST] = "west",
    [KHDAYS_PAD_NORTH] = "north",
    [KHDAYS_PAD_DPUP] = "dpup",
    [KHDAYS_PAD_DPDOWN] = "dpdown",
    [KHDAYS_PAD_DPLEFT] = "dpleft",
    [KHDAYS_PAD_DPRIGHT] = "dpright",
    [KHDAYS_PAD_LEFTSHOULDER] = "leftshoulder",
    [KHDAYS_PAD_RIGHTSHOULDER] = "rightshoulder",
    [KHDAYS_PAD_LEFTTRIGGER] = "lefttrigger",
    [KHDAYS_PAD_RIGHTTRIGGER] = "righttrigger",
    [KHDAYS_PAD_LEFTSTICK] = "leftstick",
    [KHDAYS_PAD_RIGHTSTICK] = "rightstick",
    [KHDAYS_PAD_START] = "start",
    [KHDAYS_PAD_BACK] = "back",
    [KHDAYS_PAD_GUIDE] = "guide",
    [KHDAYS_PAD_MISC1] = "misc1",
    [KHDAYS_PAD_TOUCHPAD] = "touchpad",
    [KHDAYS_PAD_PADDLE1] = "paddle1",
    [KHDAYS_PAD_PADDLE2] = "paddle2",
    [KHDAYS_PAD_PADDLE3] = "paddle3",
    [KHDAYS_PAD_PADDLE4] = "paddle4",
};

const char *khdays_action_name(int action)
{
    return action >= 0 && action < KHDAYS_ACT_COUNT ? action_names[action] : NULL;
}

int khdays_action_from_name(const char *name)
{
    for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
        if (SDL_strcasecmp(name, action_names[a]) == 0) {
            return a;
        }
    }
    return -1;
}

const char *khdays_pad_input_name(int input)
{
    return input >= 0 && input < KHDAYS_PAD_COUNT ? pad_names[input] : NULL;
}

static const char *const mouse_names[KHDAYS_MOUSE_COUNT] = {
    "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse X1", "Mouse X2", "Mouse Wheel Up", "Mouse Wheel Down",
};

const char *khdays_mouse_input_name(int input)
{
    return input >= KHDAYS_MOUSE_INPUT && input < KHDAYS_KEYBOARD_INPUTS ? mouse_names[input - KHDAYS_MOUSE_INPUT]
                                                                         : NULL;
}

int khdays_mouse_input_from_name(const char *name)
{
    for (int i = 0; i < KHDAYS_MOUSE_COUNT; ++i) {
        if (SDL_strcasecmp(name, mouse_names[i]) == 0) {
            return KHDAYS_MOUSE_INPUT + i;
        }
    }
    return -1;
}

int khdays_pad_input_from_name(const char *name)
{
    for (int i = 0; i < KHDAYS_PAD_COUNT; ++i) {
        if (SDL_strcasecmp(name, pad_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static void empty(KhdaysProfile *p)
{
    for (int d = 0; d < KHDAYS_DEVICE_COUNT; ++d) {
        for (int l = 0; l < KHDAYS_LAYER_COUNT; ++l) {
            for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
                for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
                    p->input[d][l][a][s] = KHDAYS_NO_INPUT;
                }
            }
        }
    }
}

static void bind(KhdaysProfile *p, int device, int layer, int action, int first, int second)
{
    p->input[device][layer][action][0] = (int16_t)first;
    p->input[device][layer][action][1] = (int16_t)second;
}

void khdays_profile_default(KhdaysProfile *p)
{
    enum {
        K = KHDAYS_DEVICE_KEYBOARD,
        G = KHDAYS_DEVICE_GAMEPAD,
        BASE = KHDAYS_LAYER_BASE,
        FIELD = KHDAYS_LAYER_FIELD,
        NONE = KHDAYS_NO_INPUT
    };
    memset(p, 0, sizeof(*p));
    p->version = 1;
    p->preset = KHDAYS_PRESET_DS;
    empty(p);

    /* The keyboard and mouse, as in PC games: WASD for the D-pad, the left mouse button A, the right
     * one Y, Space B; in a field the wheel and Q/E move the command menu (when it is on the D-pad).
     * Keys are scancodes: the same physical keys on every keyboard layout. */
    bind(p, K, BASE, KHDAYS_ACT_UP, SDL_SCANCODE_W, NONE);
    bind(p, K, BASE, KHDAYS_ACT_DOWN, SDL_SCANCODE_S, NONE);
    bind(p, K, BASE, KHDAYS_ACT_LEFT, SDL_SCANCODE_A, NONE);
    bind(p, K, BASE, KHDAYS_ACT_RIGHT, SDL_SCANCODE_D, NONE);
    bind(p, K, BASE, KHDAYS_ACT_A, KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_LEFT, NONE);
    bind(p, K, BASE, KHDAYS_ACT_B, SDL_SCANCODE_SPACE, NONE);
    bind(p, K, BASE, KHDAYS_ACT_X, SDL_SCANCODE_C, NONE);
    bind(p, K, BASE, KHDAYS_ACT_Y, KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_RIGHT, NONE);
    bind(p, K, BASE, KHDAYS_ACT_L, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LEFT);
    bind(p, K, BASE, KHDAYS_ACT_R, KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_MIDDLE, SDL_SCANCODE_RIGHT);
    bind(p, K, BASE, KHDAYS_ACT_START, SDL_SCANCODE_M, SDL_SCANCODE_RETURN);
    bind(p, K, BASE, KHDAYS_ACT_SELECT, SDL_SCANCODE_N, NONE);
    bind(p, K, FIELD, KHDAYS_ACT_MENU_UP, KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_WHEEL_UP, NONE);
    bind(p, K, FIELD, KHDAYS_ACT_MENU_DOWN, KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_WHEEL_DOWN, NONE);
    bind(p, K, FIELD, KHDAYS_ACT_MENU_LEFT, SDL_SCANCODE_Q, NONE);
    bind(p, K, FIELD, KHDAYS_ACT_MENU_RIGHT, SDL_SCANCODE_E, NONE);

    /* The gamepad: the DS's own layout, by position (the right-hand face
     * button is DS A on every controller, as on the DS and the Switch Pro). */
    bind(p, G, BASE, KHDAYS_ACT_A, KHDAYS_PAD_EAST, NONE);
    bind(p, G, BASE, KHDAYS_ACT_B, KHDAYS_PAD_SOUTH, NONE);
    bind(p, G, BASE, KHDAYS_ACT_Y, KHDAYS_PAD_WEST, NONE);
    bind(p, G, BASE, KHDAYS_ACT_X, KHDAYS_PAD_NORTH, NONE);
    bind(p, G, BASE, KHDAYS_ACT_L, KHDAYS_PAD_LEFTSHOULDER, NONE);
    bind(p, G, BASE, KHDAYS_ACT_R, KHDAYS_PAD_RIGHTSHOULDER, NONE);
    bind(p, G, BASE, KHDAYS_ACT_START, KHDAYS_PAD_START, NONE);
    bind(p, G, BASE, KHDAYS_ACT_SELECT, KHDAYS_PAD_BACK, NONE);
    bind(p, G, BASE, KHDAYS_ACT_UP, KHDAYS_PAD_DPUP, NONE);
    bind(p, G, BASE, KHDAYS_ACT_DOWN, KHDAYS_PAD_DPDOWN, NONE);
    bind(p, G, BASE, KHDAYS_ACT_LEFT, KHDAYS_PAD_DPLEFT, NONE);
    bind(p, G, BASE, KHDAYS_ACT_RIGHT, KHDAYS_PAD_DPRIGHT, NONE);
    /* Two ways to open the settings menu: Steam and the Steam Deck keep the
     * Guide button for themselves, and neither the DS layout nor the KH II
     * layout uses L3. */
    bind(p, G, BASE, KHDAYS_ACT_OPEN_SETTINGS, KHDAYS_PAD_GUIDE, KHDAYS_PAD_LEFTSTICK);
    /* Inputs the DS layout leaves free, used by the enhancements when they are
     * on; they do nothing while their enhancement is off. */
    bind(p, G, BASE, KHDAYS_ACT_LOCK_ON, KHDAYS_PAD_RIGHTSTICK, NONE);
    bind(p, G, BASE, KHDAYS_ACT_TARGET_NEXT, KHDAYS_PAD_RIGHTTRIGGER, NONE);
    bind(p, G, BASE, KHDAYS_ACT_TARGET_PREV, KHDAYS_PAD_LEFTTRIGGER, NONE);
    bind(p, G, FIELD, KHDAYS_ACT_MENU_UP, KHDAYS_PAD_DPUP, NONE);
    bind(p, G, FIELD, KHDAYS_ACT_MENU_DOWN, KHDAYS_PAD_DPDOWN, NONE);
    bind(p, G, FIELD, KHDAYS_ACT_MENU_LEFT, KHDAYS_PAD_DPLEFT, NONE);
    bind(p, G, FIELD, KHDAYS_ACT_MENU_RIGHT, KHDAYS_PAD_DPRIGHT, NONE);

    /* Port choices, adjustable in the settings menu. */
    p->stick[0] = (KhdaysStick){KHDAYS_STICK_DPAD, 30, 5};
    p->stick[1] = (KhdaysStick){KHDAYS_STICK_CAMERA, 30, 5};
    p->enh.camera_speed = 100;
    p->enh.mouse_speed = 100;
    p->enh.run_threshold = 75;
    p->enh.walk_speed = 50;
    khdays_video_default(&p->video);
    khdays_sound_default(&p->sound);
    khdays_network_default(&p->network);
    khdays_save_default(&p->save);
    p->gameplay.stop_magic = 1;
    khdays_hud_settings_default(p->enh.hud);
    p->mods.native_code = 0;
}

void khdays_sound_default(KhdaysSound *s)
{
    s->volume = s->music = s->effects = s->voices = s->movies = 100;
    s->ds_exact = 0;
}

void khdays_save_default(KhdaysSave *s)
{
    s->all_files = 1;
    s->autosave = KHDAYS_AUTOSAVE_FILE99;
}

void khdays_network_default(KhdaysNetwork *n)
{
    n->nickname[0] = '\0';
    n->online = 1;
    /* the maintainer's public server */
    SDL_strlcpy(n->server, "212.227.76.27", sizeof(n->server));
    n->show_ping = 0;
    n->check_updates = 1;
}

int khdays_nickname_char(int c)
{
    /* printable ASCII only: the port's own text can draw these, and whether
     * the game's font has more has not been checked yet */
    return c >= 0x20 && c < 0x7f;
}

int khdays_server_char(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-' ||
           c == ':';
}

void khdays_video_default(KhdaysVideo *v)
{
    /* the window's own size: on a PC the DS's 1x is too small to read, and HD models and textures need the
     * pixels to show */
    v->internal_resolution = KHDAYS_RESOLUTION_WINDOW;
    v->antialiasing = 1;            /* off: 2x is four times the 3D's work */
    v->aspect = KHDAYS_ASPECT_16_9; /* for one screen; two screens use 4:3 */
    v->smooth_scaling = 0;
    v->texture_smoothing = 0;
    v->movies = KHDAYS_MOVIES_HD; /* the maintainer's choice */
    v->picture_ds = 0;            /* the maintainer's choice */
    v->show_fps = 0;
    v->backend = KHDAYS_BACKEND_AUTO;
    v->hd_graphics = 1;
    v->hd_fonts = 1;
    v->dump_graphics = 0;
    v->one_screen = 1; /* the maintainer's choice */
    v->hud_size = 4;
    v->subtitles = 1; /* as the DS showed them */
    v->display = KHDAYS_DISPLAY_WINDOW;
}

/* The display values as the file writes them. */
static const char *const displays[KHDAYS_DISPLAY_COUNT] = {"window", "borderless", "fullscreen"};

/* The aspect ratio used when the file gives none: 16:9 on one screen, 4:3
 * with the DS's two screens. */
static int default_aspect(int one_screen)
{
    return one_screen ? KHDAYS_ASPECT_16_9 : KHDAYS_ASPECT_4_3;
}

/* The aspect values as the file writes them. */
static const char *const aspects[KHDAYS_ASPECT_COUNT] = {"4:3", "16:9", "16:10", "window"};

/* Whether `n` is a valid internal resolution: any whole multiple (the only
 * limit is the graphics card's, and gpu_present.c scale_now steps down from
 * it), or the window's size. */
static int valid_resolution(int n)
{
    return n >= 1 || n == KHDAYS_RESOLUTION_WINDOW;
}

static int input_count(int device)
{
    return device == KHDAYS_DEVICE_KEYBOARD ? KHDAYS_KEYBOARD_INPUTS : KHDAYS_PAD_COUNT;
}

int khdays_profile_owner(const KhdaysProfile *p, int device, int layer, int input)
{
    for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
        for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
            if (p->input[device][layer][a][s] == input) {
                return a;
            }
        }
    }
    return -1;
}

int khdays_profile_set(KhdaysProfile *p, int device, int layer, int action, int slot, int input)
{
    if (action < 0 || action >= KHDAYS_ACT_COUNT || slot < 0 || slot >= KHDAYS_INPUTS_PER_ACTION || input < 0 ||
        input >= input_count(device)) {
        return -1;
    }
    if (device == KHDAYS_DEVICE_KEYBOARD && (input == KHDAYS_SETTINGS_KEY || action == KHDAYS_ACT_OPEN_SETTINGS)) {
        return -1; /* Esc is the keyboard's settings key, and it needs no other */
    }
    /* An input does one thing per device and layer: take it from any other action. */
    for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
        for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
            if (p->input[device][layer][a][s] == input) {
                p->input[device][layer][a][s] = KHDAYS_NO_INPUT;
            }
        }
    }
    p->input[device][layer][action][slot] = (int16_t)input;
    return 0;
}

void khdays_profile_clear(KhdaysProfile *p, int device, int layer, int action, int slot)
{
    if (action >= 0 && action < KHDAYS_ACT_COUNT && slot >= 0 && slot < KHDAYS_INPUTS_PER_ACTION) {
        p->input[device][layer][action][slot] = KHDAYS_NO_INPUT;
    }
}

static int clamp(int *value, int low, int high)
{
    const int old = *value;
    *value = *value < low ? low : *value > high ? high : *value;
    return *value != old;
}

int khdays_profile_repair(KhdaysProfile *p)
{
    int changed = 0;
    for (int d = 0; d < KHDAYS_DEVICE_COUNT; ++d) {
        for (int l = 0; l < KHDAYS_LAYER_COUNT; ++l) {
            uint8_t seen[KHDAYS_KEYBOARD_INPUTS] = {0}; /* 1 once an action has taken the input */
            for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
                for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
                    int16_t *slot = &p->input[d][l][a][s];
                    const int bad =
                        *slot != KHDAYS_NO_INPUT && (*slot < 0 || *slot >= input_count(d) || seen[*slot] ||
                                                     (d == KHDAYS_DEVICE_KEYBOARD &&
                                                      (*slot == KHDAYS_SETTINGS_KEY || a == KHDAYS_ACT_OPEN_SETTINGS)));
                    if (bad) {
                        *slot = KHDAYS_NO_INPUT;
                        ++changed;
                    } else if (*slot != KHDAYS_NO_INPUT) {
                        seen[*slot] = 1;
                    }
                }
            }
        }
    }
    {
        int16_t *open = p->input[KHDAYS_DEVICE_GAMEPAD][KHDAYS_LAYER_BASE][KHDAYS_ACT_OPEN_SETTINGS];
        if (open[0] == KHDAYS_NO_INPUT && open[1] == KHDAYS_NO_INPUT) {
            /* The gamepad must always be able to open the settings menu:
             * give it Guide and L3 again, taking them from whatever had them. */
            khdays_profile_set(p, KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_BASE, KHDAYS_ACT_OPEN_SETTINGS, 0,
                               KHDAYS_PAD_GUIDE);
            khdays_profile_set(p, KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_BASE, KHDAYS_ACT_OPEN_SETTINGS, 1,
                               KHDAYS_PAD_LEFTSTICK);
            ++changed;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (p->stick[i].role < KHDAYS_STICK_NONE || p->stick[i].role > KHDAYS_STICK_CAMERA) {
            p->stick[i].role = KHDAYS_STICK_NONE;
            ++changed;
        }
        changed += clamp(&p->stick[i].dead_zone, 5, 90);
        changed += clamp(&p->stick[i].hysteresis, 0, 20);
    }
    changed += clamp(&p->enh.camera_speed, 10, 300);
    changed += clamp(&p->enh.mouse_speed, 10, 300);
    changed += clamp(&p->enh.run_threshold, 50, 95);
    changed += clamp(&p->enh.walk_speed, 20, 90);
    for (int i = 0; i < KHDAYS_OPT_COUNT; ++i)
        changed +=
            clamp(&p->enh.hud[i], i >= KHDAYS_OPT_MISSION_ANCHOR ? 1 : 0, i >= KHDAYS_OPT_MISSION_ANCHOR ? 9 : 1);
    if (p->preset != KHDAYS_PRESET_DS && p->preset != KHDAYS_PRESET_KH2) {
        p->preset = KHDAYS_PRESET_DS;
        ++changed;
    }
    {
        KhdaysVideo defaults;
        khdays_video_default(&defaults);
        if (!valid_resolution(p->video.internal_resolution)) {
            p->video.internal_resolution = defaults.internal_resolution;
            ++changed;
        }
        changed += clamp(&p->video.one_screen, 0, 1);
        if (p->video.aspect < 0 || p->video.aspect >= KHDAYS_ASPECT_COUNT) {
            p->video.aspect = default_aspect(p->video.one_screen);
            ++changed;
        }
        changed += clamp(&p->video.hud_size, 1, 8);
        changed += clamp(&p->video.display, 0, KHDAYS_DISPLAY_COUNT - 1);
    }
    changed += clamp(&p->sound.volume, 0, 100);
    changed += clamp(&p->sound.music, 0, 100);
    changed += clamp(&p->sound.effects, 0, 100);
    changed += clamp(&p->sound.voices, 0, 100);
    changed += clamp(&p->sound.movies, 0, 100);
    return changed;
}

/* ---- The gamepad presets ---------------------------------------------- */

/* The Kingdom Hearts II layout for Days's actions (Kingdom Hearts III uses the same): attack on the
 * bottom button, jump on the right, guard on the left, DS X on the top button, L1 for the shortcuts
 * (DS L), R1 lock-on, R2 switch target, R3 for DS R (with Config Controls Type A, R resets the camera,
 * as R3 does in KH), View or touch pad for DS Select, the D-pad for the command menu and the right
 * stick for the camera. Returns the enhancements it needs that are off. */
static unsigned kh2_gamepad(KhdaysProfile *p, unsigned available)
{
    enum { G = KHDAYS_DEVICE_GAMEPAD, BASE = KHDAYS_LAYER_BASE, FIELD = KHDAYS_LAYER_FIELD, NONE = KHDAYS_NO_INPUT };
    unsigned missing = 0;
    for (int l = 0; l < KHDAYS_LAYER_COUNT; ++l) {
        for (int a = 0; a < KHDAYS_ACT_COUNT; ++a) {
            p->input[G][l][a][0] = p->input[G][l][a][1] = KHDAYS_NO_INPUT;
        }
    }
    bind(p, G, BASE, KHDAYS_ACT_A, KHDAYS_PAD_SOUTH, NONE);
    bind(p, G, BASE, KHDAYS_ACT_B, KHDAYS_PAD_EAST, NONE);
    bind(p, G, BASE, KHDAYS_ACT_Y, KHDAYS_PAD_WEST, NONE);
    bind(p, G, BASE, KHDAYS_ACT_X, KHDAYS_PAD_NORTH, NONE);
    bind(p, G, BASE, KHDAYS_ACT_L, KHDAYS_PAD_LEFTSHOULDER, NONE);
    bind(p, G, BASE, KHDAYS_ACT_START, KHDAYS_PAD_START, NONE);
    bind(p, G, BASE, KHDAYS_ACT_SELECT, KHDAYS_PAD_BACK, NONE);
    bind(p, G, BASE, KHDAYS_ACT_UP, KHDAYS_PAD_DPUP, NONE);
    bind(p, G, BASE, KHDAYS_ACT_DOWN, KHDAYS_PAD_DPDOWN, NONE);
    bind(p, G, BASE, KHDAYS_ACT_LEFT, KHDAYS_PAD_DPLEFT, NONE);
    bind(p, G, BASE, KHDAYS_ACT_RIGHT, KHDAYS_PAD_DPRIGHT, NONE);
    bind(p, G, BASE, KHDAYS_ACT_OPEN_SETTINGS, KHDAYS_PAD_GUIDE, KHDAYS_PAD_LEFTSTICK);
    p->stick[0].role = KHDAYS_STICK_DPAD;
    p->stick[1].role = KHDAYS_STICK_CAMERA;

    if (available & KHDAYS_ENH_SEPARATE_LOCK_ON) {
        bind(p, G, BASE, KHDAYS_ACT_LOCK_ON, KHDAYS_PAD_RIGHTSHOULDER, NONE);
        bind(p, G, BASE, KHDAYS_ACT_TARGET_NEXT, KHDAYS_PAD_RIGHTTRIGGER, NONE);
        bind(p, G, BASE, KHDAYS_ACT_R, KHDAYS_PAD_RIGHTSTICK, NONE);
        p->enh.separate_lock_on = 1;
    } else {
        /* the DS's own lock-on (and camera reset) on R1, and on R3 */
        bind(p, G, BASE, KHDAYS_ACT_R, KHDAYS_PAD_RIGHTSHOULDER, KHDAYS_PAD_RIGHTSTICK);
        p->enh.separate_lock_on = 0;
        missing |= KHDAYS_ENH_SEPARATE_LOCK_ON;
    }
    if (available & KHDAYS_ENH_DPAD_COMMAND_MENU) {
        bind(p, G, FIELD, KHDAYS_ACT_MENU_UP, KHDAYS_PAD_DPUP, NONE);
        bind(p, G, FIELD, KHDAYS_ACT_MENU_DOWN, KHDAYS_PAD_DPDOWN, NONE);
        bind(p, G, FIELD, KHDAYS_ACT_MENU_LEFT, KHDAYS_PAD_DPLEFT, NONE);
        bind(p, G, FIELD, KHDAYS_ACT_MENU_RIGHT, KHDAYS_PAD_DPRIGHT, NONE);
        p->enh.dpad_command_menu = 1;
    } else {
        p->enh.dpad_command_menu = 0;
        missing |= KHDAYS_ENH_DPAD_COMMAND_MENU;
    }
    p->enh.analog_camera = (available & KHDAYS_ENH_ANALOG_CAMERA) != 0;
    if (!p->enh.analog_camera) {
        p->stick[1].role = KHDAYS_STICK_NONE;
        missing |= KHDAYS_ENH_ANALOG_CAMERA;
    }
    p->enh.analog_movement = (available & KHDAYS_ENH_ANALOG_MOVEMENT) != 0;
    if (!p->enh.analog_movement) {
        missing |= KHDAYS_ENH_ANALOG_MOVEMENT;
    }
    return missing;
}

unsigned khdays_profile_apply_preset(KhdaysProfile *p, int preset, unsigned available)
{
    unsigned missing = 0;
    if (preset == KHDAYS_PRESET_KH2) {
        missing = kh2_gamepad(p, available);
    } else {
        KhdaysProfile defaults;
        khdays_profile_default(&defaults);
        memcpy(p->input[KHDAYS_DEVICE_GAMEPAD], defaults.input[KHDAYS_DEVICE_GAMEPAD],
               sizeof(p->input[KHDAYS_DEVICE_GAMEPAD]));
        p->stick[0].role = defaults.stick[0].role;
        p->stick[1].role = defaults.stick[1].role;
        p->enh.analog_camera = p->enh.separate_lock_on = 0;
        p->enh.dpad_command_menu = p->enh.analog_movement = 0;
        preset = KHDAYS_PRESET_DS;
    }
    p->preset = preset;
    p->preset_modified = 0;
    return missing;
}

int khdays_profile_matches_preset(const KhdaysProfile *p, unsigned available)
{
    KhdaysProfile fresh = *p;
    khdays_profile_apply_preset(&fresh, p->preset, available);
    return memcmp(fresh.input[KHDAYS_DEVICE_GAMEPAD], p->input[KHDAYS_DEVICE_GAMEPAD],
                  sizeof(fresh.input[KHDAYS_DEVICE_GAMEPAD])) == 0 &&
           fresh.stick[0].role == p->stick[0].role && fresh.stick[1].role == p->stick[1].role &&
           fresh.enh.analog_camera == p->enh.analog_camera && fresh.enh.separate_lock_on == p->enh.separate_lock_on &&
           fresh.enh.dpad_command_menu == p->enh.dpad_command_menu &&
           fresh.enh.analog_movement == p->enh.analog_movement;
}

/* ---- The settings file ------------------------------------------------ */

void khdays_host_user_path(const char *name, char *path, int size)
{
    SDL_snprintf(path, (size_t)size, "%s/%s", khdays_host_user_folder(), name);
}

int khdays_settings_game_language(void)
{
    KhdaysProfile profile;
    char path[512];
    khdays_settings_path(path, sizeof(path));
    khdays_profile_load(&profile, path, NULL);
    return khdays_language_choose(profile.language);
}

void khdays_settings_nickname(char *nickname, int size)
{
    KhdaysProfile profile;
    char path[512];
    khdays_settings_path(path, sizeof(path));
    khdays_profile_load(&profile, path, NULL);
    SDL_strlcpy(nickname, profile.network.nickname, (size_t)size);
}

int khdays_settings_server(char *server, int size)
{
    KhdaysProfile profile;
    char path[512];
    khdays_settings_path(path, sizeof(path));
    khdays_profile_load(&profile, path, NULL);
    SDL_strlcpy(server, profile.network.server, (size_t)size);
    return profile.network.online && profile.network.server[0] != '\0';
}

void khdays_settings_path(char *path, int size)
{
    const char *chosen = SDL_getenv("KHDAYS_SETTINGS");
    if (chosen != NULL && *chosen != '\0') {
        SDL_strlcpy(path, chosen, (size_t)size);
    } else {
        khdays_host_user_path("settings.ini", path, size);
        /* another player's copy on the same PC starts from the first player's
         * settings (same PC, same screen and sound) */
        if (khdays_host_player_slot_taken() > 1 && !SDL_GetPathInfo(path, NULL)) {
            size_t length = 0;
            void *first = SDL_LoadFile("saves/settings.ini", &length);
            if (first != NULL) {
                SDL_SaveFile(path, first, length);
                SDL_free(first);
            }
        }
    }
}

/* The lines (and their sections) the last load did not understand. They are
 * written back unchanged, so a newer or hand-made file loses nothing. */
typedef struct UnknownLine {
    char *section;
    char *line;
} UnknownLine;
static UnknownLine *unknown;
static int unknown_count, unknown_room;

static void forget_unknown(void)
{
    for (int i = 0; i < unknown_count; ++i) {
        SDL_free(unknown[i].section);
        SDL_free(unknown[i].line);
    }
    unknown_count = 0;
}

static void keep_unknown(const char *section, const char *line)
{
    UnknownLine kept;
    if (unknown_count == unknown_room) {
        const int room = unknown_room > 0 ? unknown_room * 2 : 16;
        UnknownLine *bigger = (UnknownLine *)SDL_realloc(unknown, sizeof(UnknownLine) * (size_t)room);
        if (bigger == NULL) {
            return; /* out of memory: this line is not kept */
        }
        unknown = bigger;
        unknown_room = room;
    }
    kept.section = SDL_strdup(section);
    kept.line = SDL_strdup(line);
    if (kept.section == NULL || kept.line == NULL) {
        SDL_free(kept.section);
        SDL_free(kept.line);
        return;
    }
    unknown[unknown_count++] = kept;
}

static void say(KhdaysNotice notice, const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (notice != NULL) {
        notice(text);
    }
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        *--end = '\0';
    }
    return s;
}

/* Cuts a comment off a value. A comment starts at a '#' with a space on each
 * side ("left = dpad   # the D-pad"), so a key named "Keypad #" survives. */
static void cut_comment(char *value)
{
    for (char *c = value; *c != '\0'; ++c) {
        if (c > value && c[0] == '#' && (c[-1] == ' ' || c[-1] == '\t') && (c[1] == ' ' || c[1] == '\t')) {
            *c = '\0';
            return;
        }
    }
}

enum Section {
    SEC_TOP,
    SEC_KEYS,
    SEC_STICKS,
    SEC_ENHANCEMENTS,
    SEC_VIDEO,
    SEC_SOUND,
    SEC_NETWORK,
    SEC_SAVE,
    SEC_GAMEPLAY,
    SEC_MODS,
    SEC_UNKNOWN
};

typedef struct Reader {
    KhdaysProfile *profile;
    KhdaysNotice notice;
    enum Section section;
    int device, layer; /* for SEC_KEYS */
    char section_name[40];
    /* Which action this file gave each input to (action + 1), to catch a
     * later line giving the same input to another action. */
    uint8_t owner[KHDAYS_DEVICE_COUNT][KHDAYS_LAYER_COUNT][KHDAYS_KEYBOARD_INPUTS];
    uint8_t action_seen[KHDAYS_DEVICE_COUNT][KHDAYS_LAYER_COUNT][KHDAYS_ACT_COUNT];
    int version_seen;
    int aspect_seen; /* the file gave [video] aspect (else it follows the screens) */
} Reader;

static void enter_section(Reader *r, const char *name)
{
    static const struct {
        const char *name;
        int device, layer;
    } key_sections[] = {
        {"keyboard", KHDAYS_DEVICE_KEYBOARD, KHDAYS_LAYER_BASE},
        {"keyboard.field", KHDAYS_DEVICE_KEYBOARD, KHDAYS_LAYER_FIELD},
        {"gamepad", KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_BASE},
        {"gamepad.field", KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_FIELD},
    };
    SDL_strlcpy(r->section_name, name, sizeof(r->section_name));
    for (size_t i = 0; i < sizeof(key_sections) / sizeof(key_sections[0]); ++i) {
        if (SDL_strcasecmp(name, key_sections[i].name) == 0) {
            r->section = SEC_KEYS;
            r->device = key_sections[i].device;
            r->layer = key_sections[i].layer;
            return;
        }
    }
    if (SDL_strcasecmp(name, "sticks") == 0) {
        r->section = SEC_STICKS;
    } else if (SDL_strcasecmp(name, "enhancements") == 0) {
        r->section = SEC_ENHANCEMENTS;
    } else if (SDL_strcasecmp(name, "video") == 0) {
        r->section = SEC_VIDEO;
    } else if (SDL_strcasecmp(name, "sound") == 0) {
        r->section = SEC_SOUND;
    } else if (SDL_strcasecmp(name, "network") == 0) {
        r->section = SEC_NETWORK;
    } else if (SDL_strcasecmp(name, "save") == 0) {
        r->section = SEC_SAVE;
    } else if (SDL_strcasecmp(name, "gameplay") == 0) {
        r->section = SEC_GAMEPLAY;
    } else if (SDL_strcasecmp(name, "mods") == 0) {
        r->section = SEC_MODS;
    } else {
        r->section = SEC_UNKNOWN;
        say(r->notice, "settings: section [%s] is not understood by this version; kept as it is", name);
    }
}

/* The input a name stands for on this device, or -1. */
static int input_named(int device, const char *name)
{
    if (device == KHDAYS_DEVICE_KEYBOARD) {
        const int mouse = khdays_mouse_input_from_name(name);
        const SDL_Scancode code = mouse < 0 ? SDL_GetScancodeFromName(name) : SDL_SCANCODE_UNKNOWN;
        return mouse >= 0 ? mouse : code == SDL_SCANCODE_UNKNOWN ? -1 : (int)code;
    }
    return khdays_pad_input_from_name(name);
}

static void read_binding(Reader *r, const char *key, char *value, const char *line)
{
    KhdaysProfile *p = r->profile;
    const int action = khdays_action_from_name(key);
    int slot = 0;
    if (action < 0 || (r->device == KHDAYS_DEVICE_KEYBOARD && action == KHDAYS_ACT_OPEN_SETTINGS)) {
        say(r->notice, "settings: [%s] has no action \"%s\"; kept as it is", r->section_name, key);
        keep_unknown(r->section_name, line);
        return;
    }
    if (r->action_seen[r->device][r->layer][action]) {
        say(r->notice, "settings: [%s] gives \"%s\" twice; the last one counts", r->section_name, key);
    }
    r->action_seen[r->device][r->layer][action] = 1;
    /* The file says which inputs this action has: drop what it had before. */
    for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
        const int old = p->input[r->device][r->layer][action][s];
        if (old != KHDAYS_NO_INPUT && r->owner[r->device][r->layer][old] == action + 1) {
            r->owner[r->device][r->layer][old] = 0;
        }
        p->input[r->device][r->layer][action][s] = KHDAYS_NO_INPUT;
    }
    for (char *item = value, *next; item != NULL && *item != '\0'; item = next) {
        char *comma = strchr(item, ',');
        int input;
        next = comma != NULL ? comma + 1 : NULL;
        if (comma != NULL) {
            *comma = '\0';
        }
        item = trim(item);
        if (*item == '\0') {
            continue;
        }
        input = input_named(r->device, item);
        if (input < 0) {
            say(r->notice, "settings: [%s] %s: \"%s\" is not an input this port knows; ignored", r->section_name, key,
                item);
            continue;
        }
        if (r->device == KHDAYS_DEVICE_KEYBOARD && input == KHDAYS_SETTINGS_KEY) {
            say(r->notice, "settings: [%s] %s: Escape always opens the settings menu; ignored", r->section_name, key);
            continue;
        }
        if (r->owner[r->device][r->layer][input] != 0 && r->owner[r->device][r->layer][input] != action + 1) {
            say(r->notice, "settings: [%s] %s: \"%s\" is already given to \"%s\"; ignored here", r->section_name, key,
                item, khdays_action_name(r->owner[r->device][r->layer][input] - 1));
            continue;
        }
        if (slot == KHDAYS_INPUTS_PER_ACTION) {
            say(r->notice, "settings: [%s] %s: only two inputs per action; \"%s\" ignored", r->section_name, key, item);
            continue;
        }
        khdays_profile_set(p, r->device, r->layer, action, slot++, input);
        r->owner[r->device][r->layer][input] = (uint8_t)(action + 1);
    }
}

static int read_number(Reader *r, const char *key, const char *value, int low, int high, int *out)
{
    char *end;
    const long n = SDL_strtol(value, &end, 10);
    if (end == value || *trim(end) != '\0') {
        say(r->notice, "settings: [%s] %s: \"%s\" is not a number; the default is kept", r->section_name, key, value);
        return 0;
    }
    *out = (int)(n < low ? low : n > high ? high : n);
    if (n < low || n > high) {
        say(r->notice, "settings: [%s] %s: %ld is outside %d..%d; %d is used", r->section_name, key, n, low, high,
            *out);
    }
    return 1;
}

static int read_switch(Reader *r, const char *key, const char *value, int *out)
{
    if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "yes") == 0 || SDL_strcasecmp(value, "true") == 0 ||
        strcmp(value, "1") == 0) {
        *out = 1;
        return 1;
    }
    if (SDL_strcasecmp(value, "off") == 0 || SDL_strcasecmp(value, "no") == 0 || SDL_strcasecmp(value, "false") == 0 ||
        strcmp(value, "0") == 0) {
        *out = 0;
        return 1;
    }
    say(r->notice, "settings: [%s] %s: \"%s\" should be on or off; the default is kept", r->section_name, key, value);
    return 0;
}

static void read_stick_setting(Reader *r, const char *key, const char *value, const char *line)
{
    static const char *const roles[] = {"none", "dpad", "camera"};
    for (int i = 0; i < 2; ++i) {
        const char *side = i == 0 ? "left" : "right";
        char name[32];
        KhdaysStick *stick = &r->profile->stick[i];
        if (SDL_strcasecmp(key, side) == 0) {
            for (int role = 0; role < 3; ++role) {
                if (SDL_strcasecmp(value, roles[role]) == 0) {
                    stick->role = role;
                    return;
                }
            }
            say(r->notice, "settings: [sticks] %s: \"%s\" should be dpad, camera or none; the default is kept", key,
                value);
            return;
        }
        SDL_snprintf(name, sizeof(name), "%s_dead_zone", side);
        if (SDL_strcasecmp(key, name) == 0) {
            read_number(r, key, value, 5, 90, &stick->dead_zone);
            return;
        }
        SDL_snprintf(name, sizeof(name), "%s_hysteresis", side);
        if (SDL_strcasecmp(key, name) == 0) {
            read_number(r, key, value, 0, 20, &stick->hysteresis);
            return;
        }
    }
    say(r->notice, "settings: [sticks] has no setting \"%s\"; kept as it is", key);
    keep_unknown("sticks", line);
}

static void read_enhancement(Reader *r, const char *key, const char *value, const char *line)
{
    KhdaysEnhancements *e = &r->profile->enh;
    for (int i = 0; i < KHDAYS_OPT_COUNT; ++i)
        if (SDL_strcasecmp(key, khdays_hud_setting_keys[i]) == 0) {
            if (i < KHDAYS_OPT_MISSION_VISIBILITY)
                read_switch(r, key, value, &e->hud[i]);
            else
                read_number(r, key, value, i >= KHDAYS_OPT_MISSION_ANCHOR ? 1 : 0,
                            i >= KHDAYS_OPT_MISSION_ANCHOR ? 9 : 1, &e->hud[i]);
            return;
        }
    static const struct {
        const char *name;
        size_t offset;
        int low, high; /* both 0: an on/off switch */
    } settings[] = {
        {"analog_camera", offsetof(KhdaysEnhancements, analog_camera), 0, 0},
        {"camera_speed", offsetof(KhdaysEnhancements, camera_speed), 10, 300},
        {"camera_invert_x", offsetof(KhdaysEnhancements, camera_invert_x), 0, 0},
        {"camera_invert_y", offsetof(KhdaysEnhancements, camera_invert_y), 0, 0},
        {"mouse_camera", offsetof(KhdaysEnhancements, mouse_camera), 0, 0},
        {"mouse_speed", offsetof(KhdaysEnhancements, mouse_speed), 10, 300},
        {"separate_lock_on", offsetof(KhdaysEnhancements, separate_lock_on), 0, 0},
        {"dpad_command_menu", offsetof(KhdaysEnhancements, dpad_command_menu), 0, 0},
        {"analog_movement", offsetof(KhdaysEnhancements, analog_movement), 0, 0},
        {"run_threshold", offsetof(KhdaysEnhancements, run_threshold), 50, 95},
        {"walk_speed", offsetof(KhdaysEnhancements, walk_speed), 20, 90},
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        if (SDL_strcasecmp(key, settings[i].name) == 0) {
            int *field = (int *)((char *)e + settings[i].offset);
            if (settings[i].low == 0 && settings[i].high == 0) {
                read_switch(r, key, value, field);
            } else {
                read_number(r, key, value, settings[i].low, settings[i].high, field);
            }
            return;
        }
    }
    say(r->notice, "settings: [enhancements] has no setting \"%s\"; kept as it is", key);
    keep_unknown("enhancements", line);
}

/* A [video] or [sound] notice, in the language the file sets (its top lines
 * come first) or else the system's: the settings menu's language. */
static void say_localized(Reader *r, int text, ...)
{
    char out[256];
    va_list args;
    const char *format = khdays_text_in(text, khdays_language_choose(r->profile->language));
    va_start(args, text);
    SDL_vsnprintf(out, sizeof(out), format != NULL ? format : khdays_text_in(text, KHDAYS_LANGUAGE_ENGLISH), args);
    va_end(args);
    if (r->notice != NULL) {
        r->notice(out);
    }
}

static void read_video(Reader *r, const char *key, const char *value, const char *line)
{
    KhdaysVideo *v = &r->profile->video;
    KhdaysVideo defaults;
    khdays_video_default(&defaults);
    if (SDL_strcasecmp(key, "internal_resolution") == 0) {
        char *end;
        const long n = SDL_strtol(value, &end, 10);
        if (SDL_strcasecmp(value, "window") == 0) {
            v->internal_resolution = KHDAYS_RESOLUTION_WINDOW;
        } else if (end != value && *trim(end) == '\0' && n != KHDAYS_RESOLUTION_WINDOW && valid_resolution((int)n)) {
            v->internal_resolution = (int)n;
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "window");
            v->internal_resolution = defaults.internal_resolution;
        }
        return;
    }
    if (SDL_strcasecmp(key, "antialiasing") == 0) {
        if (SDL_strcasecmp(value, "off") == 0) {
            v->antialiasing = 1;
        } else if ((value[0] == '2' || value[0] == '3' || value[0] == '4') && (value[1] == 'x' || value[1] == 'X') &&
                   value[2] == '\0') {
            v->antialiasing = value[0] - '0';
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "off");
            v->antialiasing = defaults.antialiasing;
        }
        return;
    }
    if (SDL_strcasecmp(key, "aspect") == 0) {
        for (int a = 0; a < KHDAYS_ASPECT_COUNT; ++a) {
            if (SDL_strcasecmp(value, aspects[a]) == 0) {
                v->aspect = a;
                r->aspect_seen = 1;
                return;
            }
        }
        say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, aspects[default_aspect(v->one_screen)]);
        v->aspect = default_aspect(v->one_screen); /* and again once the screens are known */
        return;
    }
    if (SDL_strcasecmp(key, "screens") == 0) {
        if (SDL_strcasecmp(value, "one") == 0 || SDL_strcasecmp(value, "two") == 0) {
            v->one_screen = SDL_strcasecmp(value, "one") == 0;
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, defaults.one_screen ? "one" : "two");
            v->one_screen = defaults.one_screen;
        }
        return;
    }
    if (SDL_strcasecmp(key, "hud_size") == 0) {
        char *end = NULL;
        const long n = SDL_strtol(value, &end, 10);
        if (end != value && *trim(end) == '\0' && n >= 1 && n <= 8) {
            v->hud_size = (int)n;
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "4");
            v->hud_size = defaults.hud_size;
        }
        return;
    }
    if (SDL_strcasecmp(key, "show_fps") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            v->show_fps = SDL_strcasecmp(value, "on") == 0;
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "off");
            v->show_fps = defaults.show_fps;
        }
        return;
    }
    if (SDL_strcasecmp(key, "backend") == 0) {
        static const char *const names[KHDAYS_BACKEND_COUNT] = {"auto", "direct3d12", "vulkan"};
        for (int b = 0; b < KHDAYS_BACKEND_COUNT; ++b) {
            if (SDL_strcasecmp(value, names[b]) == 0) {
                v->backend = b;
                return;
            }
        }
        say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "auto");
        v->backend = defaults.backend;
        return;
    }
    if (SDL_strcasecmp(key, "picture_pace") == 0) {
        if (SDL_strcasecmp(value, "smooth") == 0 || SDL_strcasecmp(value, "ds") == 0) {
            v->picture_ds = SDL_strcasecmp(value, "ds") == 0;
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "smooth");
            v->picture_ds = defaults.picture_ds;
        }
        return;
    }
    if (SDL_strcasecmp(key, "display") == 0) {
        int d = -1;
        for (int i = 0; i < KHDAYS_DISPLAY_COUNT; ++i) {
            if (SDL_strcasecmp(value, displays[i]) == 0) {
                d = i;
            }
        }
        if (d < 0) {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, displays[defaults.display]);
            d = defaults.display;
        }
        v->display = d;
        return;
    }
    if (SDL_strcasecmp(key, "subtitles") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            v->subtitles = SDL_strcasecmp(value, "on") == 0;
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "on");
            v->subtitles = defaults.subtitles;
        }
        return;
    }
    if (SDL_strcasecmp(key, "movies") == 0) {
        if (SDL_strcasecmp(value, "hd") == 0) {
            v->movies = KHDAYS_MOVIES_HD;
        } else if (SDL_strcasecmp(value, "pc") == 0) {
            v->movies = KHDAYS_MOVIES_PC;
        } else if (SDL_strcasecmp(value, "ds") == 0) {
            v->movies = KHDAYS_MOVIES_DS;
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "hd");
            v->movies = defaults.movies;
        }
        return;
    }
    if (SDL_strcasecmp(key, "scaling") == 0) {
        if (SDL_strcasecmp(value, "sharp") == 0 || SDL_strcasecmp(value, "smooth") == 0) {
            v->smooth_scaling = SDL_strcasecmp(value, "smooth") == 0;
        } else {
            say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, "sharp");
            v->smooth_scaling = defaults.smooth_scaling;
        }
        return;
    }
    {
        static const struct {
            const char *name;
            size_t offset;
        } switches[] = {
            {"texture_smoothing", offsetof(KhdaysVideo, texture_smoothing)},
            {"hd_graphics", offsetof(KhdaysVideo, hd_graphics)},
            {"hd_fonts", offsetof(KhdaysVideo, hd_fonts)},
            {"dump_graphics", offsetof(KhdaysVideo, dump_graphics)},
        };
        for (size_t i = 0; i < sizeof(switches) / sizeof(switches[0]); ++i) {
            if (SDL_strcasecmp(key, switches[i].name) == 0) {
                int *field = (int *)((char *)v + switches[i].offset);
                if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
                    *field = SDL_strcasecmp(value, "on") == 0;
                } else {
                    const int fallback = *(const int *)((const char *)&defaults + switches[i].offset);
                    say_localized(r, TXT_VIDEO_BAD_VALUE, key, value, fallback ? "on" : "off");
                    *field = fallback;
                }
                return;
            }
        }
    }
    say_localized(r, TXT_VIDEO_UNKNOWN_KEY, key);
    keep_unknown("video", line);
}

static void read_sound(Reader *r, const char *key, const char *value, const char *line)
{
    static const struct {
        const char *name;
        size_t offset;
    } volumes[] = {
        {"volume", offsetof(KhdaysSound, volume)},   {"music", offsetof(KhdaysSound, music)},
        {"effects", offsetof(KhdaysSound, effects)}, {"voices", offsetof(KhdaysSound, voices)},
        {"movies", offsetof(KhdaysSound, movies)},
    };
    KhdaysSound *s = &r->profile->sound;
    KhdaysSound defaults;
    khdays_sound_default(&defaults);
    for (size_t i = 0; i < sizeof(volumes) / sizeof(volumes[0]); ++i) {
        if (SDL_strcasecmp(key, volumes[i].name) == 0) {
            int *field = (int *)((char *)s + volumes[i].offset);
            char *end;
            const long n = SDL_strtol(value, &end, 10);
            if (end != value && *trim(end) == '\0' && n >= 0 && n <= 100) {
                *field = (int)n;
            } else {
                *field = *(const int *)((const char *)&defaults + volumes[i].offset);
                say_localized(r, TXT_SOUND_BAD_VALUE, key, value, "100");
            }
            return;
        }
    }
    if (SDL_strcasecmp(key, "ds_exact") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            s->ds_exact = SDL_strcasecmp(value, "on") == 0;
        } else {
            s->ds_exact = defaults.ds_exact;
            say_localized(r, TXT_SOUND_BAD_VALUE, key, value, "off");
        }
        return;
    }
    say_localized(r, TXT_SOUND_UNKNOWN_KEY, key);
    keep_unknown("sound", line);
}

/* [network]: nickname, online, server, show_ping, check_updates. */
static void read_network(Reader *r, const char *key, const char *value, const char *line)
{
    KhdaysNetwork *n = &r->profile->network;
    if (SDL_strcasecmp(key, "nickname") == 0) {
        size_t length = 0;
        for (const char *c = value; *c != '\0' && length < KHDAYS_NICKNAME_CHARS; ++c) {
            if (khdays_nickname_char((unsigned char)*c)) {
                n->nickname[length++] = *c;
            }
        }
        n->nickname[length] = '\0';
        if (length < SDL_strlen(value)) {
            say(r->notice, "settings: [network] nickname keeps %d characters the game can show: \"%s\"",
                KHDAYS_NICKNAME_CHARS, n->nickname);
        }
    } else if (SDL_strcasecmp(key, "online") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            n->online = SDL_strcasecmp(value, "on") == 0;
        } else {
            say(r->notice, "settings: [network] online = \"%s\" is neither on nor off; on is used", value);
            n->online = 1;
        }
    } else if (SDL_strcasecmp(key, "server") == 0) {
        size_t length = 0;
        for (const char *c = value; *c != '\0'; ++c) {
            if (!khdays_server_char((unsigned char)*c) || length + 1 >= sizeof(n->server)) {
                say(r->notice, "settings: [network] server = \"%s\" is not an address; the default is used", value);
                khdays_network_default(n);
                return;
            }
            n->server[length++] = *c;
        }
        n->server[length] = '\0';
    } else if (SDL_strcasecmp(key, "show_ping") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            n->show_ping = SDL_strcasecmp(value, "on") == 0;
        } else {
            say(r->notice, "settings: [network] show_ping = \"%s\" is neither on nor off; off is used", value);
            n->show_ping = 0;
        }
    } else if (SDL_strcasecmp(key, "check_updates") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            n->check_updates = SDL_strcasecmp(value, "on") == 0;
        } else {
            say(r->notice, "settings: [network] check_updates = \"%s\" is neither on nor off; on is used", value);
            n->check_updates = 1;
        }
    } else {
        say(r->notice, "settings: [network] has no setting \"%s\"; kept as it is", key);
        keep_unknown("network", line);
    }
}

/* [save]: files, autosave. */
static void read_save(Reader *r, const char *key, const char *value, const char *line)
{
    KhdaysSave *s = &r->profile->save;
    if (SDL_strcasecmp(key, "files") == 0) {
        if (strcmp(value, "99") == 0 || strcmp(value, "3") == 0) {
            s->all_files = strcmp(value, "99") == 0;
        } else {
            say(r->notice, "settings: [save] files = \"%s\" is neither 99 nor 3; 99 is used", value);
            s->all_files = 1;
        }
    } else if (SDL_strcasecmp(key, "autosave") == 0) {
        if (SDL_strcasecmp(value, "file99") == 0) {
            s->autosave = KHDAYS_AUTOSAVE_FILE99;
        } else if (SDL_strcasecmp(value, "played") == 0) {
            s->autosave = KHDAYS_AUTOSAVE_PLAYED;
        } else if (SDL_strcasecmp(value, "off") == 0) {
            s->autosave = KHDAYS_AUTOSAVE_OFF;
        } else {
            say(r->notice, "settings: [save] autosave = \"%s\" is not file99, played or off; file99 is used", value);
            s->autosave = KHDAYS_AUTOSAVE_FILE99;
        }
    } else {
        say(r->notice, "settings: [save] has no setting \"%s\"; kept as it is", key);
        keep_unknown("save", line);
    }
}

/* [mods]: native_code. */
static void read_mods(Reader *r, const char *key, const char *value, const char *line)
{
    KhdaysModsSettings *m = &r->profile->mods;
    if (SDL_strcasecmp(key, "native_code") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            m->native_code = SDL_strcasecmp(value, "on") == 0;
        } else {
            say(r->notice, "settings: [mods] native_code = \"%s\" is neither on nor off; off is used", value);
            m->native_code = 0;
        }
    } else {
        say(r->notice, "settings: [mods] has no setting \"%s\"; kept as it is", key);
        keep_unknown("mods", line);
    }
}

/* [gameplay]: stop_magic. */
static void read_gameplay(Reader *r, const char *key, const char *value, const char *line)
{
    KhdaysGameplay *g = &r->profile->gameplay;
    if (SDL_strcasecmp(key, "stop_magic") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            g->stop_magic = SDL_strcasecmp(value, "on") == 0;
        } else {
            say(r->notice, "settings: [gameplay] stop_magic = \"%s\" is neither on nor off; on is used", value);
            g->stop_magic = 1;
        }
    } else {
        say(r->notice, "settings: [gameplay] has no setting \"%s\"; kept as it is", key);
        keep_unknown("gameplay", line);
    }
}

static void read_top(Reader *r, const char *key, const char *value, const char *line)
{
    if (SDL_strcasecmp(key, "version") == 0) {
        r->version_seen = 1;
        if (strcmp(value, "1") != 0) {
            say(r->notice, "settings: version %s is not known to this port; read as version 1", value);
        }
    } else if (SDL_strcasecmp(key, "log") == 0) {
        if (SDL_strcasecmp(value, "on") == 0 || SDL_strcasecmp(value, "off") == 0) {
            r->profile->session_log = SDL_strcasecmp(value, "on") == 0;
        } else {
            say(r->notice, "settings: log = \"%s\" is neither on nor off; off is used", value);
            r->profile->session_log = 0;
        }
    } else if (SDL_strcasecmp(key, "language") == 0) {
        r->profile->language = khdays_language_from_code(value);
        if (r->profile->language == KHDAYS_LANGUAGE_SYSTEM && SDL_strcasecmp(value, "system") != 0) {
            say(r->notice, "settings: language \"%s\" is not one of the game's; the system's is used", value);
        }
    } else if (SDL_strcasecmp(key, "preset") == 0) {
        if (SDL_strcasecmp(value, "kh2") == 0 || SDL_strcasecmp(value, "kh3") == 0) { /* kh3: its old name */
            r->profile->preset = KHDAYS_PRESET_KH2;
        } else {
            r->profile->preset = KHDAYS_PRESET_DS;
            if (SDL_strcasecmp(value, "ds") != 0) {
                say(r->notice, "settings: preset \"%s\" is not known; ds is used", value);
            }
        }
    } else {
        say(r->notice, "settings: \"%s\" is not understood by this version; kept as it is", key);
        keep_unknown("", line);
    }
}

/* Reads `text` into r->profile. Returns 0 if it is not a settings file at all
 * (a line that is not a section, a setting, a comment or blank, or a zero
 * byte). */
static int read_text(Reader *r, char *text, size_t size)
{
    if (memchr(text, '\0', size) != NULL) {
        return 0;
    }
    if ((unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb && (unsigned char)text[2] == 0xbf) {
        text += 3; /* a UTF-8 byte order mark */
    }
    for (char *line = text, *next; line != NULL; line = next) {
        char *equals, *key, *value, *original;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        line = trim(line);
        if (*line == '\0' || *line == '#' || *line == ';') {
            continue;
        }
        if (*line == '[') {
            char *close = strchr(line, ']');
            if (close == NULL) {
                return 0;
            }
            *close = '\0';
            enter_section(r, trim(line + 1));
            continue;
        }
        equals = strchr(line, '=');
        if (equals == NULL) {
            return 0;
        }
        original = SDL_strdup(line); /* the whole line, before it is cut up */
        if (original == NULL) {
            continue; /* out of memory: skip the line */
        }
        *equals = '\0';
        key = trim(line);
        value = equals + 1;
        cut_comment(value);
        value = trim(value);
        switch (r->section) {
        case SEC_TOP: read_top(r, key, value, original); break;
        case SEC_KEYS: read_binding(r, key, value, original); break;
        case SEC_STICKS: read_stick_setting(r, key, value, original); break;
        case SEC_ENHANCEMENTS: read_enhancement(r, key, value, original); break;
        case SEC_VIDEO: read_video(r, key, value, original); break;
        case SEC_SOUND: read_sound(r, key, value, original); break;
        case SEC_NETWORK: read_network(r, key, value, original); break;
        case SEC_SAVE: read_save(r, key, value, original); break;
        case SEC_GAMEPLAY: read_gameplay(r, key, value, original); break;
        case SEC_MODS: read_mods(r, key, value, original); break;
        case SEC_UNKNOWN: keep_unknown(r->section_name, original); break;
        }
        SDL_free(original);
    }
    return 1;
}

void khdays_profile_load(KhdaysProfile *profile, const char *path, KhdaysNotice notice)
{
    size_t size = 0;
    char *text;
    Reader *r;
    khdays_profile_default(profile);
    forget_unknown();
    if (!SDL_GetPathInfo(path, NULL)) {
        return; /* no settings yet: the defaults */
    }
    text = (char *)SDL_LoadFile(path, &size);
    r = (Reader *)SDL_calloc(1, sizeof(Reader));
    if (text != NULL && r != NULL) {
        r->profile = profile;
        r->notice = notice;
        r->section = SEC_TOP;
        if (!read_text(r, text, size)) {
            char bad[512];
            SDL_snprintf(bad, sizeof(bad), "%s.bad", path);
            SDL_CopyFile(path, bad);
            say(notice,
                "settings: %s is not a settings file the port can read; it was kept as %s and the "
                "defaults are used",
                path, bad);
            khdays_profile_default(profile);
            forget_unknown();
        } else if (!r->aspect_seen) {
            profile->video.aspect = default_aspect(profile->video.one_screen);
        }
    } else {
        say(notice, "settings: %s could not be read (%s); the defaults are used", path, SDL_GetError());
    }
    SDL_free(r);
    SDL_free(text);
    if (khdays_profile_repair(profile) != 0) {
        say(notice, "settings: some settings broke the rules (an input given twice, the gamepad with no way to "
                    "open the settings menu); they were put right");
    }
}

/* A growing text buffer, for writing the file. */
typedef struct Text {
    char *data;
    size_t length, capacity;
} Text;

/* Appends `format` to `t`, however long it comes out: it is measured first,
 * then written in place. (A fixed buffer here once cut a long section short.) */
static void add(Text *t, const char *format, ...)
{
    int n;
    va_list args, again;
    va_start(args, format);
    va_copy(again, args);
    n = SDL_vsnprintf(NULL, 0, format, args);
    va_end(args);
    if (n < 0) {
        va_end(again);
        return;
    }
    if (t->length + (size_t)n + 1 > t->capacity) {
        const size_t capacity = (t->capacity + (size_t)n + 1) * 2;
        char *data = (char *)SDL_realloc(t->data, capacity);
        if (data == NULL) {
            va_end(again);
            return;
        }
        t->data = data;
        t->capacity = capacity;
    }
    SDL_vsnprintf(t->data + t->length, (size_t)n + 1, format, again);
    va_end(again);
    t->length += (size_t)n;
}

/* The actions each section lists, in the order the file shows them. */
static const int base_actions[] = {
    KHDAYS_ACT_UP,      KHDAYS_ACT_DOWN,        KHDAYS_ACT_LEFT,        KHDAYS_ACT_RIGHT,
    KHDAYS_ACT_A,       KHDAYS_ACT_B,           KHDAYS_ACT_X,           KHDAYS_ACT_Y,
    KHDAYS_ACT_L,       KHDAYS_ACT_R,           KHDAYS_ACT_START,       KHDAYS_ACT_SELECT,
    KHDAYS_ACT_LOCK_ON, KHDAYS_ACT_TARGET_NEXT, KHDAYS_ACT_TARGET_PREV, KHDAYS_ACT_OPEN_SETTINGS,
};
static const int field_actions[] = {
    KHDAYS_ACT_MENU_UP,
    KHDAYS_ACT_MENU_DOWN,
    KHDAYS_ACT_MENU_LEFT,
    KHDAYS_ACT_MENU_RIGHT,
};

static void add_unknown(Text *t, const char *section)
{
    int first = 1;
    for (int i = 0; i < unknown_count; ++i) {
        if (SDL_strcasecmp(unknown[i].section, section) == 0) {
            if (first) {
                add(t, "# not understood by this version\n");
                first = 0;
            }
            add(t, "%s\n", unknown[i].line);
        }
    }
}

static void add_bindings(Text *t, const KhdaysProfile *p, int device, int layer, const char *section)
{
    const int *actions = layer == KHDAYS_LAYER_BASE ? base_actions : field_actions;
    const size_t count = layer == KHDAYS_LAYER_BASE ? sizeof(base_actions) / sizeof(base_actions[0])
                                                    : sizeof(field_actions) / sizeof(field_actions[0]);
    add(t, "\n[%s]\n", section);
    for (size_t i = 0; i < count; ++i) {
        const int a = actions[i];
        if (device == KHDAYS_DEVICE_KEYBOARD && a == KHDAYS_ACT_OPEN_SETTINGS) {
            continue; /* the keyboard's settings key is always Esc */
        }
        add(t, "%s =", khdays_action_name(a));
        for (int s = 0, written = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
            const int input = p->input[device][layer][a][s];
            if (input == KHDAYS_NO_INPUT) {
                continue;
            }
            add(t, "%s %s", written++ ? "," : "",
                device != KHDAYS_DEVICE_KEYBOARD ? khdays_pad_input_name(input)
                : input >= KHDAYS_MOUSE_INPUT    ? khdays_mouse_input_name(input)
                                                 : SDL_GetScancodeName((SDL_Scancode)input));
        }
        add(t, "\n");
    }
    add_unknown(t, section);
}

int khdays_profile_save(const KhdaysProfile *p, const char *path, KhdaysNotice notice)
{
    static const char *const roles[] = {"none", "dpad", "camera"};
    const KhdaysEnhancements *e = &p->enh;
    Text t = {0};
    char tmp[512], dir[512];
    char *slash;
    int ok;

    add(&t,
        "# Kingdom Hearts 358/2 Days, native port: the player's control settings.\n"
        "# The port writes this file when its settings menu closes; you may edit it.\n"
        "# Each line is \"action = input\" (two inputs at most, separated by a comma;\n"
        "# nothing after = means no input). Keyboard inputs are SDL key names, gamepad\n"
        "# inputs are positions: south, east, west, north, dpup, leftshoulder, ...\n"
        "version = 1\n"
        "# language: system (the system's, if the game has it), en, fr, de, it or es\n"
        "language = %s\n"
        "# log: on writes the session log to saves/logs (F12 marks a moment in it)\n"
        "log = %s\n"
        "preset = %s\n",
        khdays_language_code(p->language), p->session_log ? "on" : "off",
        p->preset == KHDAYS_PRESET_KH2 ? "kh2" : "ds");
    add_unknown(&t, "");
    add_bindings(&t, p, KHDAYS_DEVICE_KEYBOARD, KHDAYS_LAYER_BASE, "keyboard");
    for (int i = 0; i < KHDAYS_ACT_COUNT; ++i) {
        if (p->input[KHDAYS_DEVICE_KEYBOARD][KHDAYS_LAYER_FIELD][i][0] != KHDAYS_NO_INPUT ||
            p->input[KHDAYS_DEVICE_KEYBOARD][KHDAYS_LAYER_FIELD][i][1] != KHDAYS_NO_INPUT) {
            add_bindings(&t, p, KHDAYS_DEVICE_KEYBOARD, KHDAYS_LAYER_FIELD, "keyboard.field");
            break;
        }
    }
    add_bindings(&t, p, KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_BASE, "gamepad");
    add_bindings(&t, p, KHDAYS_DEVICE_GAMEPAD, KHDAYS_LAYER_FIELD, "gamepad.field");
    add(&t, "\n[sticks]\n");
    add(&t, "left = %s\nright = %s\n", roles[p->stick[0].role], roles[p->stick[1].role]);
    add(&t, "left_dead_zone = %d\nright_dead_zone = %d\n", p->stick[0].dead_zone, p->stick[1].dead_zone);
    add(&t, "left_hysteresis = %d\nright_hysteresis = %d\n", p->stick[0].hysteresis, p->stick[1].hysteresis);
    add_unknown(&t, "sticks");
    add(&t, "\n[enhancements]\n");
    add(&t, "analog_camera = %s\ncamera_speed = %d\n", e->analog_camera ? "on" : "off", e->camera_speed);
    add(&t, "camera_invert_x = %s\ncamera_invert_y = %s\n", e->camera_invert_x ? "on" : "off",
        e->camera_invert_y ? "on" : "off");
    add(&t, "mouse_camera = %s\nmouse_speed = %d\n", e->mouse_camera ? "on" : "off", e->mouse_speed);
    add(&t, "separate_lock_on = %s\n", e->separate_lock_on ? "on" : "off");
    add(&t, "dpad_command_menu = %s\n", e->dpad_command_menu ? "on" : "off");
    add(&t, "analog_movement = %s\nrun_threshold = %d\nwalk_speed = %d\n", e->analog_movement ? "on" : "off",
        e->run_threshold, e->walk_speed);
    for (int i = 0; i < KHDAYS_OPT_COUNT; ++i) {
        if (i < KHDAYS_OPT_MISSION_VISIBILITY)
            add(&t, "%s = %s\n", khdays_hud_setting_keys[i], e->hud[i] ? "on" : "off");
        else
            add(&t, "%s = %d\n", khdays_hud_setting_keys[i], e->hud[i]);
    }
    add_unknown(&t, "enhancements");
    {
        const KhdaysVideo *v = &p->video;
        char resolution[16], antialiasing[8];
        if (v->internal_resolution == KHDAYS_RESOLUTION_WINDOW) {
            SDL_strlcpy(resolution, "window", sizeof(resolution));
        } else {
            SDL_snprintf(resolution, sizeof(resolution), "%d", v->internal_resolution);
        }
        if (v->antialiasing > 1) {
            SDL_snprintf(antialiasing, sizeof(antialiasing), "%dx", v->antialiasing);
        } else {
            SDL_strlcpy(antialiasing, "off", sizeof(antialiasing));
        }
        add(&t,
            "\n[video]\n"
            "# display: window, borderless (a window without borders over the whole screen) or fullscreen\n"
            "display = %s\n"
            "# internal_resolution: window (the window's size, the default) or a whole multiple of the DS's\n"
            "# picture: the settings offer 2 to 16; 1, the DS's own, only for comparing with the DS\n"
            "internal_resolution = %s\n"
            "# antialiasing: off, 2x, 3x or 4x -- the picture made that many times larger and shrunk,\n"
            "# the 3D's edges smooth (2x is four times the work, 4x sixteen)\n"
            "antialiasing = %s\n"
            "# screens: two (the DS's, one above the other) or one (one picture)\n"
            "screens = %s\n"
            "# hud_size: 1 to 8, the HUD's size on one screen (4 the default)\n"
            "hud_size = %d\n"
            "# aspect: 4:3 (the DS's), 16:9, 16:10, or window\n"
            "aspect = %s\n"
            "# scaling: sharp (the DS's pixels) or smooth\n"
            "scaling = %s\n"
            "texture_smoothing = %s\nhd_graphics = %s\n"
            "# hd_fonts: on (the mods' fonts over the DS's text) or off (the DS's own pixels)\n"
            "hd_fonts = %s\ndump_graphics = %s\n"
            "# movies: hd (the HD collection's videos where data/cutscenes/cinematics has them,\n"
            "#         else pc), pc (full colour) or ds (the DS's 15-bit colour, dithered)\n"
            "movies = %s\n"
            "# subtitles: on or off, the movies' subtitles (a new game asks)\n"
            "subtitles = %s\n"
            "# picture_pace: smooth (the 3D at the monitor's rate, up to a logic frame behind) or ds\n"
            "picture_pace = %s\n"
            "# show_fps: the pictures and the game's frames a second, in a corner\n"
            "show_fps = %s\n"
            "# backend: the graphics API -- auto (Direct3D 12 on Windows, Vulkan under Wine or Proton),\n"
            "#          direct3d12 or vulkan\n"
            "backend = %s\n",
            displays[v->display >= 0 && v->display < KHDAYS_DISPLAY_COUNT ? v->display : 0], resolution, antialiasing,
            v->one_screen ? "one" : "two", v->hud_size,
            aspects[v->aspect >= 0 && v->aspect < KHDAYS_ASPECT_COUNT ? v->aspect : 0],
            v->smooth_scaling ? "smooth" : "sharp", v->texture_smoothing ? "on" : "off", v->hd_graphics ? "on" : "off",
            v->hd_fonts ? "on" : "off", v->dump_graphics ? "on" : "off",
            v->movies == KHDAYS_MOVIES_DS   ? "ds"
            : v->movies == KHDAYS_MOVIES_PC ? "pc"
                                            : "hd",
            v->subtitles ? "on" : "off", v->picture_ds ? "ds" : "smooth", v->show_fps ? "on" : "off",
            v->backend == KHDAYS_BACKEND_D3D12    ? "direct3d12"
            : v->backend == KHDAYS_BACKEND_VULKAN ? "vulkan"
                                                  : "auto");
    }
    add_unknown(&t, "video");
    {
        const KhdaysSound *snd = &p->sound;
        add(&t,
            "\n[sound]\n"
            "# volume, music, effects, voices, movies: 0 to 100\n"
            "volume = %d\nmusic = %d\neffects = %d\nvoices = %d\nmovies = %d\n"
            "# ds_exact: on gives the DS's own output (32,728 Hz, 10 bits); off, the PC's\n"
            "ds_exact = %s\n",
            snd->volume, snd->music, snd->effects, snd->voices, snd->movies, snd->ds_exact ? "on" : "off");
    }
    add_unknown(&t, "sound");
    {
        const KhdaysNetwork *n = &p->network;
        add(&t,
            "\n[network]\n"
            "# nickname: the name Mission Mode's multiplayer shows the others, up to 10 characters;\n"
            "# nothing after = gives an automatic one (\"Player\" and four digits)\n"
            "nickname = %s\n"
            "# online: on also finds and offers rooms through the server, besides this PC and the\n"
            "# local network; server: its address (host or host:port, the port 35821 if none)\n"
            "online = %s\nserver = %s\n"
            "# show_ping: in a session, the round trip to each other player, in a corner\n"
            "show_ping = %s\n"
            "# check_updates: once a day, ask GitHub whether a newer release of the port is out, and\n"
            "# say so on the title screen (nothing about you is sent)\n"
            "check_updates = %s\n",
            n->nickname, n->online ? "on" : "off", n->server, n->show_ping ? "on" : "off",
            n->check_updates ? "on" : "off");
    }
    add_unknown(&t, "network");
    {
        static const char *const autosaves[KHDAYS_AUTOSAVE_MODES] = {"off", "file99", "played"};
        const KhdaysSave *s = &p->save;
        add(&t,
            "\n[save]\n"
            "# files: 99, the game's own save lists page through the port's 99 files; 3, the DS's three\n"
            "files = %s\n"
            "# autosave: after each completed mission, where the game lets you save next --\n"
            "# file99 (file 99, \"Autosave\" in the lists), played (the file being played; a new game\n"
            "# the first file without a save) or off\n"
            "autosave = %s\n",
            s->all_files ? "99" : "3",
            autosaves[s->autosave >= 0 && s->autosave < KHDAYS_AUTOSAVE_MODES ? s->autosave : KHDAYS_AUTOSAVE_FILE99]);
    }
    add_unknown(&t, "save");
    add(&t,
        "\n[gameplay]\n"
        "# stop_magic: on, the Stop magic Days left unfinished, restored (its panels, its recipes at the\n"
        "# moogle's, its casts); off, the game as the DS's. Read as the game starts\n"
        "stop_magic = %s\n",
        p->gameplay.stop_magic ? "on" : "off");
    add_unknown(&t, "gameplay");
    add(&t,
        "\n[mods]\n"
        "# native_code: on, the mods' native plugins (code built for this computer that a mod carries) run"
        "\n# inside the game: only for mods you trust -- a plugin can do anything a program can, crash the"
        "\n# game or damage its saves. off (the default): no plugin runs. Read as the game starts\n"
        "native_code = %s\n",
        p->mods.native_code ? "on" : "off");
    add_unknown(&t, "mods");
    for (int i = 0; i < unknown_count; ++i) {
        /* whole sections this version does not know, written back as they were */
        const char *s = unknown[i].section;
        if (*s == '\0' || SDL_strcasecmp(s, "sticks") == 0 || SDL_strcasecmp(s, "enhancements") == 0 ||
            SDL_strcasecmp(s, "video") == 0 || SDL_strcasecmp(s, "sound") == 0 || SDL_strcasecmp(s, "network") == 0 ||
            SDL_strcasecmp(s, "save") == 0 || SDL_strcasecmp(s, "gameplay") == 0 || SDL_strcasecmp(s, "mods") == 0 ||
            SDL_strncasecmp(s, "keyboard", 8) == 0 || SDL_strncasecmp(s, "gamepad", 7) == 0) {
            continue;
        }
        if (i == 0 || SDL_strcasecmp(unknown[i - 1].section, s) != 0) {
            add(&t, "\n[%s]\n", s);
        }
        add(&t, "%s\n", unknown[i].line);
    }
    if (t.data == NULL) {
        say(notice, "settings: out of memory writing %s", path);
        return -1;
    }

    /* Make the folder, write a temporary file beside the real one, then
     * replace it (so a crash never leaves half a file). */
    SDL_strlcpy(dir, path, sizeof(dir));
    slash = SDL_strrchr(dir, '/');
    if (slash == NULL) {
        slash = SDL_strrchr(dir, '\\');
    }
    if (slash != NULL) {
        *slash = '\0';
        SDL_CreateDirectory(dir);
    }
    SDL_snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    ok = SDL_SaveFile(tmp, t.data, t.length) && SDL_RenamePath(tmp, path);
    if (!ok) {
        say(notice, "settings: %s could not be written: %s", path, SDL_GetError());
    }
    SDL_free(t.data);
    return ok ? 0 : -1;
}
