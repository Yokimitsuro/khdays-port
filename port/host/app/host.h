/* The PC side of the port: the window that shows the screens and collects
 * the input (window.c). It runs SDL3 on a thread of its own, so none of it
 * runs on the game's execution contexts (context.h). */
#ifndef KHDAYS_HOST_H
#define KHDAYS_HOST_H

#include <stdint.h>

typedef struct KhdaysFramePackage KhdaysFramePackage; /* runtime/display/frame_package.h */

/* Opens the window, unless KHDAYS_HEADLESS is set. Returns 0 on failure. */
int khdays_host_start(void);

/* The game's data cannot be used (runtime/core/rom.cpp), before the port stops: the player told, in the
 * system's language, on stderr and in a message box (none when headless). `where` the folder
 * searched or the data's; `broken` NULL when there is no data there, else what is wrong with it. */
void khdays_host_tell_no_data(const char *where, const char *broken);

/* Closes the window and the sound, and waits until they are closed (used by a
 * reset). */
void khdays_host_close(void);

/* Wakes the window's loop to look again at what it shows: an HD scene's video begun while the game,
 * held for it, sends no frames that would. */
void khdays_host_wake(void);

/* Shows a finished frame: both screens, 256x192 each, 0x00RRGGBB (copied). */
void khdays_host_present(const uint32_t *upper, const uint32_t *lower);

/* While the window is open, the game thread hands over its frames as "frame
 * packages" (runtime/display/frame_package.h). This says what the window wants in
 * them besides the two screens (KHDAYS_WANT_*): 0 when headless, or when it
 * shows the two screens as they are. It only changes between frames. */
int khdays_host_frame_wants(void);

/* How many times the DS's width the 3D should cover (more than 1 in
 * widescreen; 1 when headless or at 4:3), for khdays_g3d_set_widen. It only
 * changes between frames. */
float khdays_host_frame_widen(void);

/* The internal resolution the picture is composed at (1 to 8; 1 when headless), for what the game
 * thread draws at that resolution (the port's own elements). It only changes between frames. */
int khdays_host_frame_scale(void);

/* The video settings the game thread reads (KHDAYS_HOST_*): HD graphics from
 * mods, dumping what is shown, and how movies play. 0 when headless. */
#define KHDAYS_HOST_HD_GRAPHICS 0x1
#define KHDAYS_HOST_DUMP_GRAPHICS 0x2
#define KHDAYS_HOST_MOVIES_DS 0x4   /* [video] movies = ds: the DS's own pictures */
#define KHDAYS_HOST_MOVIES_HD 0x8   /* [video] movies = hd: the player's HD videos */
#define KHDAYS_HOST_ONE_SCREEN 0x10 /* [video] screens = one */
int khdays_host_graphics_flags(void);

/* The package the game thread fills next (NULL without a window). It belongs
 * to the game thread until khdays_host_frame_publish; the next call then
 * gives another one. */
KhdaysFramePackage *khdays_host_frame_begin(void);

/* The package from khdays_host_frame_begin is finished, and the window will
 * show it. (The window always shows the newest it has; one it never got to
 * is dropped.) This never waits. */
void khdays_host_frame_publish(KhdaysFramePackage *package);

/* The enhancement actions a snapshot can hold (KhdaysInputSnapshot.actions),
 * in the same order as their actions in host/app/profile.h. */
enum {
    KHDAYS_ACTION_LOCK_ON = 1 << 0,
    KHDAYS_ACTION_TARGET_NEXT = 1 << 1,
    KHDAYS_ACTION_TARGET_PREV = 1 << 2,
    KHDAYS_ACTION_MENU_UP = 1 << 3,
    KHDAYS_ACTION_MENU_DOWN = 1 << 4,
    KHDAYS_ACTION_MENU_LEFT = 1 << 5,
    KHDAYS_ACTION_MENU_RIGHT = 1 << 6,
};

/* The optional enhancements that are switched on (KhdaysInputSnapshot.enhancements). */
enum {
    KHDAYS_ENH_ANALOG_CAMERA = 1 << 0,
    KHDAYS_ENH_SEPARATE_LOCK_ON = 1 << 1,
    KHDAYS_ENH_DPAD_COMMAND_MENU = 1 << 2,
    KHDAYS_ENH_ANALOG_MOVEMENT = 1 << 3,
    KHDAYS_ENH_MOUSE_CAMERA = 1 << 4, /* the camera turned by the mouse's motion (the analog camera's join) */
};

/* Everything the game needs to know about the player's input at one moment.
 * The window publishes a new snapshot whenever something changes, and the game
 * takes one each frame (runtime/input/input.c). It has only fixed-width fields and
 * no pointers, so it means the same on every platform. */
typedef struct KhdaysInputSnapshot {
    uint32_t generation;       /* counts the snapshots published */
    uint16_t ds_buttons;       /* the DS buttons held (runtime/input/input.h, KHDAYS_KEY_*) */
    uint16_t ds_buttons_field; /* the same, minus the inputs the field layer takes over
                                  (used in the field when the D-pad command menu is on) */
    uint32_t actions;          /* KHDAYS_ACTION_* held */
    int16_t camera_x;          /* the analog camera's stick, -32767..32767, 0 when off */
    int16_t camera_y;
    uint16_t camera_speed;     /* percent of the game's own turn rate */
    int16_t move_x;            /* the movement stick, 0 inside its dead zone;  */
    int16_t move_y;            /* y positive is up; both 0 unless analog movement is on */
    uint8_t run_threshold;     /* analog movement: how far (percent) the stick must go to run */
    uint8_t walk_speed;        /* analog movement: percent of the game's speed when walking */
    uint16_t touch_x, touch_y; /* the stylus on the lower screen, in its pixels (the input scripts') */
    uint8_t touching;          /* 1 while the stylus is down */
    int32_t mouse_x, mouse_y;  /* the mouse camera: the mouse's motion summed since the start, in its counts,
                                  right and up positive (the camera's inversions taken) */
    uint16_t mouse_speed;      /* the mouse camera: percent of MOUSE_COUNTS_A_FRAME (controls_hooks.c) */
    uint8_t ui_open;           /* 1 while the settings menu is open: no input reaches the game */
    uint16_t enhancements;     /* KHDAYS_ENH_* switched on */
    int16_t sticks[2][2];      /* the pad's two sticks as they are, [left, right][x, y] in SDL's range (y grows
                                  downwards), whatever their roles: the Enemy Profiles' model viewer turns and
                                  zooms with them (runtime/menus/enemy_viewer.c); 0 without a pad */
} KhdaysInputSnapshot;

/* The latest snapshot, for the game's own read of the pad once a game frame: a button pressed and let
 * go since the last read still counts as held this once (press_edges.h). Headless, everything is 0. */
void khdays_host_input_take(KhdaysInputSnapshot *out);

/* The DS buttons held (KHDAYS_KEY_* bits, runtime/input/input.h), from the latest
 * snapshot. */
uint16_t khdays_host_buttons(void);

/* Where the mouse is held on the lower screen, in its pixels; returns 0 when
 * not touching. */
int khdays_host_touch(int *x, int *y);

/* Sound output: `count` stereo frames (left, right), from whichever thread
 * runs the sound bridge (the sound thread, or the game thread catching up;
 * sound_output.c). khdays_host_audio takes the DS mixer's 16-bit samples at
 * KHDAYS_SOUND_RATE (runtime/sound/sound.h); khdays_host_audio_float takes the PC
 * mixer's floats at the device's rate. */
void khdays_host_audio(const int16_t *frames, int count);
void khdays_host_audio_float(const float *frames, int count);

/* Starts the sound thread (sound_output.c). From then on it runs the sound
 * bridge up to the DS clock every millisecond (runtime/sound/sound_bridge.c). */
void khdays_host_sound_start(void);

/* The language the game starts in, as the DS firmware's code (1 English,
 * 2 French, 3 German, 4 Italian, 5 Spanish): KHDAYS_LANGUAGE if set, else the
 * settings file's, else the system's (profile.c). */
int khdays_settings_game_language(void);

/* Mission Mode's multiplayer settings (profile.c): the player's nickname ("" for an automatic one),
 * and the server to use (returns 1 with its address in `server`, or 0 for none). */
void khdays_settings_nickname(char *nickname, int size);
int khdays_settings_server(char *server, int size);

#endif
