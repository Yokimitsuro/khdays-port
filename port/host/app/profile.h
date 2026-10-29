/* The player's control settings (profile.c): which key or gamepad input
 * drives each DS button and each optional enhancement, what the sticks do, and
 * which enhancements are on. Plain data plus the rules that keep it valid; no
 * device or window code. */
#ifndef KHDAYS_PROFILE_H
#define KHDAYS_PROFILE_H

#include <stdint.h>
#include "hud_settings.h"

#ifdef __cplusplus
extern "C" { /* the settings menu is C++ (settings_menu.cpp) */
#endif

/* What an input can drive. The first twelve are the DS buttons, in the order
 * of their bits (runtime/input/input.h, KHDAYS_KEY_*): DS button n is bit n. */
typedef enum KhdaysAction {
    KHDAYS_ACT_A,
    KHDAYS_ACT_B,
    KHDAYS_ACT_SELECT,
    KHDAYS_ACT_START,
    KHDAYS_ACT_RIGHT,
    KHDAYS_ACT_LEFT,
    KHDAYS_ACT_UP,
    KHDAYS_ACT_DOWN,
    KHDAYS_ACT_R,
    KHDAYS_ACT_L,
    KHDAYS_ACT_X,
    KHDAYS_ACT_Y,
    KHDAYS_ACT_DS_COUNT, /* the DS buttons end here */
    /* Separate lock-on (only while that enhancement is on). */
    KHDAYS_ACT_LOCK_ON = KHDAYS_ACT_DS_COUNT,
    KHDAYS_ACT_TARGET_NEXT,
    KHDAYS_ACT_TARGET_PREV,
    /* The command menu on the gamepad's D-pad (field layer only). */
    KHDAYS_ACT_MENU_UP,
    KHDAYS_ACT_MENU_DOWN,
    KHDAYS_ACT_MENU_LEFT,
    KHDAYS_ACT_MENU_RIGHT,
    /* The port's own: open the settings menu (gamepad only; the keyboard
     * always uses Esc). */
    KHDAYS_ACT_OPEN_SETTINGS,
    /* The port's own: the one-screen picture's HUD mode, complete, exploration or none; unbound at first. */
    KHDAYS_ACT_HUD_MODE,
    KHDAYS_ACT_COUNT
} KhdaysAction;

/* The gamepad's inputs, named by position, the same on every controller
 * (east is the right-hand face button whatever is printed on it). */
typedef enum KhdaysPadInput {
    KHDAYS_PAD_SOUTH,
    KHDAYS_PAD_EAST,
    KHDAYS_PAD_WEST,
    KHDAYS_PAD_NORTH,
    KHDAYS_PAD_DPUP,
    KHDAYS_PAD_DPDOWN,
    KHDAYS_PAD_DPLEFT,
    KHDAYS_PAD_DPRIGHT,
    KHDAYS_PAD_LEFTSHOULDER,
    KHDAYS_PAD_RIGHTSHOULDER,
    KHDAYS_PAD_LEFTTRIGGER, /* a trigger counts as pressed past half its travel */
    KHDAYS_PAD_RIGHTTRIGGER,
    KHDAYS_PAD_LEFTSTICK, /* stick clicks (L3, R3) */
    KHDAYS_PAD_RIGHTSTICK,
    KHDAYS_PAD_START,
    KHDAYS_PAD_BACK,
    KHDAYS_PAD_GUIDE,
    KHDAYS_PAD_MISC1,
    KHDAYS_PAD_TOUCHPAD,
    KHDAYS_PAD_PADDLE1,
    KHDAYS_PAD_PADDLE2,
    KHDAYS_PAD_PADDLE3,
    KHDAYS_PAD_PADDLE4,
    KHDAYS_PAD_COUNT
} KhdaysPadInput;

enum { KHDAYS_DEVICE_KEYBOARD, KHDAYS_DEVICE_GAMEPAD, KHDAYS_DEVICE_COUNT };

/* A binding lives in one of two layers. `base` always applies; `field`
 * applies only in field control with the D-pad command menu on, and there it
 * wins over `base` for the same input (so the gamepad's D-pad can be the
 * command menu in a field and the DS D-pad everywhere else). */
enum { KHDAYS_LAYER_BASE, KHDAYS_LAYER_FIELD, KHDAYS_LAYER_COUNT };

/* Up to two inputs per action per device. An input is a keyboard scancode
 * (SDL_Scancode) or a KhdaysPadInput; KHDAYS_NO_INPUT marks an empty slot. */
#define KHDAYS_INPUTS_PER_ACTION 2
#define KHDAYS_NO_INPUT (-1)

/* The keyboard key that always opens the settings menu and can never be
 * bound to anything (SDL_SCANCODE_ESCAPE). */
#define KHDAYS_SETTINGS_KEY 41

/* The mouse's buttons and its wheel, bound in the keyboard's column: input KHDAYS_MOUSE_INPUT plus
 * one of these, past every scancode (SDL_SCANCODE_COUNT). A notch of the wheel is a press of a
 * moment. Its left button, unbound, is the stylus on the lower screen. */
#define KHDAYS_MOUSE_INPUT 512
typedef enum KhdaysMouseInput {
    KHDAYS_MOUSE_LEFT,
    KHDAYS_MOUSE_MIDDLE,
    KHDAYS_MOUSE_RIGHT,
    KHDAYS_MOUSE_X1, /* the side buttons */
    KHDAYS_MOUSE_X2,
    KHDAYS_MOUSE_WHEEL_UP,
    KHDAYS_MOUSE_WHEEL_DOWN,
    KHDAYS_MOUSE_COUNT
} KhdaysMouseInput;
#define KHDAYS_KEYBOARD_INPUTS (KHDAYS_MOUSE_INPUT + KHDAYS_MOUSE_COUNT)

typedef enum KhdaysStickRole {
    KHDAYS_STICK_NONE,
    KHDAYS_STICK_DPAD,   /* the DS D-pad's eight directions */
    KHDAYS_STICK_CAMERA, /* the analog camera, when it is on */
} KhdaysStickRole;

typedef struct KhdaysStick {
    int role;       /* KhdaysStickRole */
    int dead_zone;  /* percent of travel, 5..90 */
    int hysteresis; /* percent, 0..20 */
} KhdaysStick;

typedef struct KhdaysEnhancements {
    int analog_camera;
    int camera_speed; /* percent of the game's own turn rate, 10..300 */
    int camera_invert_x;
    int camera_invert_y;
    int mouse_camera; /* the camera turned by the mouse's motion, the mouse held by the window */
    int mouse_speed;  /* percent, 10..300 */
    int separate_lock_on;
    int dpad_command_menu;
    int analog_movement;
    int run_threshold; /* percent of stick travel, 50..95 */
    int walk_speed;    /* percent of the game's own speed, 20..90 */
    int hud[KHDAYS_OPT_COUNT];
} KhdaysEnhancements;

typedef enum KhdaysPreset { KHDAYS_PRESET_DS, KHDAYS_PRESET_KH2 } KhdaysPreset;

/* The picture ([video] in the settings file). The defaults are the DS's:
 * its resolution, its proportions, its pixels as they are. */
typedef enum KhdaysAspect {
    KHDAYS_ASPECT_4_3, /* the DS's */
    KHDAYS_ASPECT_16_9,
    KHDAYS_ASPECT_16_10,
    KHDAYS_ASPECT_WINDOW, /* the window's own */
    KHDAYS_ASPECT_COUNT
} KhdaysAspect;

#define KHDAYS_RESOLUTION_WINDOW 0 /* internal_resolution = window */

typedef struct KhdaysVideo {
    int internal_resolution; /* the 3D's scale: 1 (the DS's) or more, or KHDAYS_RESOLUTION_WINDOW */
    int antialiasing;        /* the picture made this many times larger, then shrunk: 1 (off) to 4 */
    int aspect;              /* KhdaysAspect */
    int smooth_scaling;      /* 0 sharp (the DS's pixels), 1 smooth */
    int texture_smoothing;
    int hd_graphics;   /* use the replacements in mods/ */
    int hd_fonts;      /* the mods' fonts (fonts/) over the DS's text (ui/text.c), else the DS's own pixels */
    int dump_graphics; /* write the graphics shown to saves/dump/ */
    int movies;        /* KhdaysMovies: HD videos, full colour or the DS's */
    int picture_ds;    /* the DS's pace (a picture each logic frame), else the 3D at the monitor's rate */
    int show_fps;      /* the pictures and the game's frames a second, in a corner of the window */
    int backend;       /* KhdaysBackend: the graphics API the picture goes through */
    int one_screen;    /* screens = one: one picture, else the DS's two */
    int hud_size;      /* the HUD's size on one screen, 1 to 8 (4) */
    int subtitles;     /* the movies' subtitles show (runtime/menus/newgame.h: the new game asks) */
    int display;       /* KhdaysDisplay: a window, a borderless one over the screen, or full screen */
} KhdaysVideo;

/* The window ([video] display): one that moves and resizes, one without borders
 * over the whole screen, or the screen the game's alone at the desktop's mode. */
typedef enum KhdaysDisplay {
    KHDAYS_DISPLAY_WINDOW,
    KHDAYS_DISPLAY_BORDERLESS,
    KHDAYS_DISPLAY_FULLSCREEN,
    KHDAYS_DISPLAY_COUNT
} KhdaysDisplay;

/* The movies ([video] movies): the player's HD videos where there is one, the rest at full colour;
 * full colour; or as the DS showed them. */
typedef enum KhdaysMovies { KHDAYS_MOVIES_HD, KHDAYS_MOVIES_PC, KHDAYS_MOVIES_DS, KHDAYS_MOVIES_COUNT } KhdaysMovies;

/* The graphics API ([video] backend): SDL's choice (Direct3D 12 on Windows, Vulkan under Wine or
 * Proton), or one asked for -- Vulkan where Direct3D 12 misbehaves on a card. */
typedef enum KhdaysBackend {
    KHDAYS_BACKEND_AUTO,
    KHDAYS_BACKEND_D3D12,
    KHDAYS_BACKEND_VULKAN,
    KHDAYS_BACKEND_COUNT
} KhdaysBackend;

/* The faithful picture. */
void khdays_video_default(KhdaysVideo *video);

/* The sound ([sound] in the settings file): the four volumes, and whether the output is the DS's own. */
typedef struct KhdaysSound {
    int volume;   /* everything, 0..100 */
    int music;    /* 0..100 */
    int effects;  /* 0..100 */
    int voices;   /* 0..100 */
    int movies;   /* 0..100: a movie's sound */
    int ds_exact; /* 1: the DS's output (32,728 Hz, 10 bits); 0: the PC's */
} KhdaysSound;

/* Every volume at 100, the PC's output. */
void khdays_sound_default(KhdaysSound *sound);

/* Mission Mode's multiplayer ([network] in the settings file). The nickname holds what the game does:
 * the DS owner's nickname it reads, ten characters; the server, a host name's 253 bytes, a colon and a
 * port. */
#define KHDAYS_NICKNAME_CHARS 10
#define KHDAYS_SERVER_BYTES 262
typedef struct KhdaysNetwork {
    char nickname[KHDAYS_NICKNAME_CHARS + 1]; /* printable ASCII; empty: an automatic one (khdays_host_player_name) */
    int online;                               /* rooms also through the server */
    char server[KHDAYS_SERVER_BYTES];         /* host[:port] */
    int show_ping;                            /* the round trip to the other players, in a corner, in a session */
    int check_updates;                        /* look once a day for a newer release (host/app/updates.c) */
} KhdaysNetwork;

/* No nickname of the player's (an automatic one), online through the
 * maintainer's server. */
void khdays_network_default(KhdaysNetwork *network);

/* Whether `c` may be in a nickname (what the game's font draws) / in a
 * server's address. */
int khdays_nickname_char(int c);
int khdays_server_char(int c);

/* The saves ([save] in the settings file): whether the game's own save lists page through the
 * port's 99 files or show the DS's three, and where the autosave goes after a completed mission. */
enum { KHDAYS_AUTOSAVE_OFF, KHDAYS_AUTOSAVE_FILE99, KHDAYS_AUTOSAVE_PLAYED, KHDAYS_AUTOSAVE_MODES };
typedef struct KhdaysSave {
    int all_files; /* the game's own lists: the 99 files (1) or the DS's three (0) */
    int autosave;  /* KHDAYS_AUTOSAVE_* */
} KhdaysSave;

/* The 99 files, the autosave into file 99. */
void khdays_save_default(KhdaysSave *save);

/* What the port adds to the game's play ([gameplay] in the settings file): the Stop magic Days left
 * unfinished (runtime/battle/stop_magic.h), read as the game starts. */
typedef struct KhdaysGameplay {
    int stop_magic; /* 1 (the default): Stop's panels, recipes and casts; 0: the game as the DS's */
} KhdaysGameplay;

/* The mods ([mods] in the settings file): whether their native plugins may run (runtime/plugins/plugins.h),
 * read as the game starts. */
typedef struct KhdaysModsSettings {
    int native_code; /* 0 (the default): no plugin is loaded; 1: they are, the player having been told */
} KhdaysModsSettings;

typedef struct KhdaysProfile {
    int version;         /* 1 */
    int language;        /* the game's and this screen's language: 0 the system's, else 1..5 (ui_text.h) */
    int session_log;     /* write the session log (runtime/core/session_log.h) */
    int preset;          /* the gamepad preset last applied (KhdaysPreset) */
    int preset_modified; /* the gamepad bindings differ from that preset */
    int16_t input[KHDAYS_DEVICE_COUNT][KHDAYS_LAYER_COUNT][KHDAYS_ACT_COUNT][KHDAYS_INPUTS_PER_ACTION];
    KhdaysStick stick[2]; /* 0 = left, 1 = right */
    KhdaysEnhancements enh;
    KhdaysVideo video;
    KhdaysSound sound;
    KhdaysNetwork network;
    KhdaysSave save;
    KhdaysGameplay gameplay;
    KhdaysModsSettings mods;
} KhdaysProfile;

/* The faithful default: today's keyboard layout, the DS layout on the
 * gamepad by position, every enhancement off. */
void khdays_profile_default(KhdaysProfile *profile);

/* Names as the settings file writes them ("a", "lock_on", "east", ...);
 * NULL / -1 for anything unknown. */
const char *khdays_action_name(int action);
int khdays_action_from_name(const char *name);
const char *khdays_pad_input_name(int input);
int khdays_pad_input_from_name(const char *name);
/* The keyboard column's mouse inputs by name ("Mouse Left", ..., "Mouse Wheel Down"): NULL for an
 * input that is not the mouse's, -1 for a name that is not one. */
const char *khdays_mouse_input_name(int input);
int khdays_mouse_input_from_name(const char *name);

/* The action an input drives on a device in a layer, or -1. */
int khdays_profile_owner(const KhdaysProfile *profile, int device, int layer, int input);

/* Puts `input` in slot `slot` of `action`, taking it away from whatever
 * other action in the same device and layer had it (the settings menu asks
 * the player first). Returns 0, or -1 when the input may not be bound there
 * (Esc on the keyboard, or an action/slot out of range). */
int khdays_profile_set(KhdaysProfile *profile, int device, int layer, int action, int slot, int input);

/* Empties a slot. */
void khdays_profile_clear(KhdaysProfile *profile, int device, int layer, int action, int slot);

/* Repairs what the rules forbid, so any profile the port uses is valid:
 * an input bound twice in one device and layer keeps its first action, Esc
 * is never bound, numbers are brought into their ranges, and the gamepad's
 * open-settings input comes back to its default if it was left with none.
 * Returns how many things it changed. */
int khdays_profile_repair(KhdaysProfile *profile);

/* The gamepad presets (KhdaysPreset): a starting point for the gamepad's bindings, stick roles and
 * enhancements, never the keyboard's. An action whose enhancement this build lacks (`available`,
 * KHDAYS_ENH_*) keeps its DS binding. Returns the missing enhancements the preset uses (0: none). */
unsigned khdays_profile_apply_preset(KhdaysProfile *profile, int preset, unsigned available);

/* Whether the gamepad part of `profile` is exactly what its preset gives. */
int khdays_profile_matches_preset(const KhdaysProfile *profile, unsigned available);

/* Where the port keeps a per-user file: saves/<name>, next to the game's
 * save. One place, so a later platform can move them all together. */
void khdays_host_user_path(const char *name, char *path, int size);

/* The settings file: KHDAYS_SETTINGS if set, else saves/settings.ini. */
void khdays_settings_path(char *path, int size);

/* Something the player should know about the settings file (a key it did not
 * understand, a value out of range); shown in the log and the settings
 * screen. */
typedef void (*KhdaysNotice)(const char *text);

/* Reads the settings file into `profile`, starting from the defaults.
 * A missing file gives the defaults silently; anything wrong in it is
 * reported through `notice` and falls back to the default for what could not
 * be read; a file that is not readable text is kept as <path>.bad. Never
 * fails: the profile is always usable afterwards. */
void khdays_profile_load(KhdaysProfile *profile, const char *path, KhdaysNotice notice);

/* Writes the profile to the settings file (through <path>.tmp, then renamed,
 * so a crash never leaves half a file). Keys the last load did not
 * understand are written back unchanged. Returns 0, or -1 with a notice. */
int khdays_profile_save(const KhdaysProfile *profile, const char *path, KhdaysNotice notice);

#ifdef __cplusplus
}
#endif

#endif
