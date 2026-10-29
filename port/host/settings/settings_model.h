/* What the settings menu shows (settings_model.c): its sections and their rows -- settings, bindings,
 * commands, headings and lines -- built from the working profile, so they always show what it holds.
 * Plain data and the rules for changing it; the drawing and the input are settings_menu.cpp's. */
#ifndef KHDAYS_SETTINGS_MODEL_H
#define KHDAYS_SETTINGS_MODEL_H

#include "../app/profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The enhancements this build can switch on (all four are built). */
#define KHDAYS_AVAILABLE_ENHANCEMENTS 0x0fu

enum KhdaysSection {
    KHDAYS_SECTION_CONTROLS,
    KHDAYS_SECTION_ENHANCEMENTS,
    KHDAYS_SECTION_STICKS,
    KHDAYS_SECTION_VIDEO,
    KHDAYS_SECTION_SOUND,
    KHDAYS_SECTION_MULTIPLAYER,
    KHDAYS_SECTION_LANGUAGE,
    KHDAYS_SECTION_GENERAL,
    KHDAYS_SECTION_MODS, /* only while a scripted mod or a native plugin is present (khdays_model_section_shown) */
    KHDAYS_SECTION_COUNT
};

enum KhdaysRowKind {
    KHDAYS_ROW_SWITCH,
    KHDAYS_ROW_NUMBER,
    KHDAYS_ROW_CHOICE,
    KHDAYS_ROW_BINDING, /* an action and its four cells: keyboard 1 and 2, gamepad 1 and 2 */
    KHDAYS_ROW_TEXT,
    KHDAYS_ROW_COMMAND,
    KHDAYS_ROW_HEADING, /* a title over the rows that follow */
    KHDAYS_ROW_LINE,    /* a line of information */
    KHDAYS_ROW_STICKS,  /* the two sticks as they are now, with their dead zones */
    KHDAYS_ROW_PAD,     /* the gamepad's picture, each input with what it does */
    KHDAYS_ROW_TEST     /* "press something: the action it does" */
};

/* When a change takes effect. */
enum KhdaysApply {
    KHDAYS_APPLY_NOW,    /* at once, with the menu still open */
    KHDAYS_APPLY_SAMPLE, /* a sample of the sound plays at once; the game hears it when it resumes */
    KHDAYS_APPLY_RESUME, /* when the game resumes */
    KHDAYS_APPLY_NEXT    /* at the moment its `takes` text says */
};

enum KhdaysCommand {
    KHDAYS_CMD_NONE,
    KHDAYS_CMD_RESTORE_KEYBOARD,
    KHDAYS_CMD_RESTORE_GAMEPAD,
    KHDAYS_CMD_RESTORE_BOTH, /* the whole default profile */
    KHDAYS_CMD_RESTORE_VIDEO,
    KHDAYS_CMD_RESTORE_SOUND,
    KHDAYS_CMD_PRESET_DS,
    KHDAYS_CMD_PRESET_KH2
};

/* A stable name for each row, for the tests (binding rows are told apart by
 * their layer and action). */
enum KhdaysSettingId {
    KHDAYS_SET_NONE,
    KHDAYS_SET_GAMEPAD_NAME,
    KHDAYS_SET_PRESET,
    KHDAYS_SET_PRESET_DS,
    KHDAYS_SET_PRESET_KH2,
    KHDAYS_SET_BINDING,
    KHDAYS_SET_RESTORE_KEYBOARD,
    KHDAYS_SET_RESTORE_GAMEPAD,
    KHDAYS_SET_TEST,
    KHDAYS_SET_PAD_VIEW,
    KHDAYS_SET_ENH_CAMERA,
    KHDAYS_SET_ENH_LOCK_ON,
    KHDAYS_SET_ENH_MENU,
    KHDAYS_SET_ENH_MOVEMENT,
    KHDAYS_SET_ENH_MOUSE,
    KHDAYS_SET_LEFT_STICK,
    KHDAYS_SET_RIGHT_STICK,
    KHDAYS_SET_LEFT_DEAD_ZONE,
    KHDAYS_SET_RIGHT_DEAD_ZONE,
    KHDAYS_SET_LEFT_HYSTERESIS,
    KHDAYS_SET_RIGHT_HYSTERESIS,
    KHDAYS_SET_CAMERA_SPEED,
    KHDAYS_SET_CAMERA_X,
    KHDAYS_SET_CAMERA_Y,
    KHDAYS_SET_MOUSE_SPEED,
    KHDAYS_SET_RUN_FROM,
    KHDAYS_SET_WALK_SPEED,
    KHDAYS_SET_STICKS_VIEW,
    KHDAYS_SET_DISPLAY,
    KHDAYS_SET_RESOLUTION,
    KHDAYS_SET_ANTIALIASING,
    KHDAYS_SET_ASPECT,
    KHDAYS_SET_SCREENS,
    KHDAYS_SET_HUD_SIZE,
    KHDAYS_SET_SCALING,
    KHDAYS_SET_TEXTURES,
    KHDAYS_SET_HD_GRAPHICS,
    KHDAYS_SET_HD_FONTS,
    KHDAYS_SET_DUMP_GRAPHICS,
    KHDAYS_SET_MOVIES,
    KHDAYS_SET_SUBTITLES,
    KHDAYS_SET_PACE,
    KHDAYS_SET_SHOW_FPS,
    KHDAYS_SET_BACKEND,
    KHDAYS_SET_RESTORE_VIDEO,
    KHDAYS_SET_VOLUME,
    KHDAYS_SET_MUSIC,
    KHDAYS_SET_EFFECTS,
    KHDAYS_SET_VOICES,
    KHDAYS_SET_MOVIES_VOLUME,
    KHDAYS_SET_DS_EXACT,
    KHDAYS_SET_RESTORE_SOUND,
    KHDAYS_SET_NICKNAME,
    KHDAYS_SET_ONLINE,
    KHDAYS_SET_SERVER,
    KHDAYS_SET_SHOW_PING,
    KHDAYS_SET_CHECK_UPDATES,
    KHDAYS_SET_LANGUAGE,
    KHDAYS_SET_SESSION_LOG,
    KHDAYS_SET_STOP_MAGIC,
    KHDAYS_SET_RESTORE_BOTH,
    KHDAYS_SET_AUTOSAVE,
    KHDAYS_SET_SAVE_FILES,
    /* the Mods section's rows: `layer` the mod (its place in the list), `action` its option */
    KHDAYS_SET_MOD_NOTE,
    KHDAYS_SET_MOD_HEADING,
    KHDAYS_SET_MOD_STATE,
    KHDAYS_SET_MOD_ENABLED,
    KHDAYS_SET_MOD_EARLIER,
    KHDAYS_SET_MOD_LATER,
    KHDAYS_SET_MOD_RESUME,
    KHDAYS_SET_MOD_OPTION,
    KHDAYS_SET_HUD_FONT,
    KHDAYS_SET_HUD_MINIMAP,
    KHDAYS_SET_HUD_PARTY_HP,
    KHDAYS_SET_HUD_ENEMY_HP,
    KHDAYS_SET_HUD_MISSION,
    KHDAYS_SET_HUD_INFORMATION,
    KHDAYS_SET_HUD_RETICLES,
    KHDAYS_SET_HUD_LOCKED_ON,
    KHDAYS_SET_HUD_NOTIFICATIONS,
    KHDAYS_SET_HUD_HEART_POINTS,
    KHDAYS_SET_HUD_PAUSE_TARGET,
    KHDAYS_SET_HUD_INFORMATION_HUD,
    KHDAYS_SET_HUD_UI_PADDING,
    KHDAYS_SET_HUD_MISSION_VISIBILITY,
    KHDAYS_SET_HUD_MISSION_ANCHOR,
    KHDAYS_SET_HUD_MISSION_PAUSE_ANCHOR,
    /* its native code: the switch, then each plugin (`layer` its place among them) */
    KHDAYS_SET_NATIVE_CODE,
    KHDAYS_SET_PLUGIN_HEADING,
    KHDAYS_SET_PLUGIN_STATE,
    KHDAYS_SET_COUNT
};

/* A binding row's four cells: the keyboard's two inputs, then the gamepad's. */
enum { KHDAYS_CELLS = KHDAYS_DEVICE_COUNT * KHDAYS_INPUTS_PER_ACTION };
#define KHDAYS_CELL_DEVICE(cell) ((cell) / KHDAYS_INPUTS_PER_ACTION)
#define KHDAYS_CELL_SLOT(cell) ((cell) % KHDAYS_INPUTS_PER_ACTION)

typedef struct KhdaysRow {
    int kind;            /* KhdaysRowKind */
    int id;              /* KhdaysSettingId */
    const char *label;   /* in the menu's language */
    const char *note;    /* shown while the row is chosen, or NULL */
    int *value;          /* switch, number, choice: in the working profile */
    int low, high, step; /* number; for a choice, high is the last choice */
    const char *const *choices;
    const int *values; /* choice: what *value holds for each choice (NULL: its index) */
    int layer, action; /* binding */
    unsigned cells;    /* binding: the cells it has (bit n = cell n) */
    char *text;        /* text: the string it edits, `text_size` bytes including the end */
    int text_size;
    int (*accepts)(int c);
    const char *placeholder; /* text: shown while it is empty, or NULL */
    int command;             /* KhdaysCommand */
    int available;           /* 0: shown greyed out; `note` says why */
    int apply;               /* KhdaysApply */
    int takes;               /* a TXT_* id saying when the change takes effect, or -1 */
    const char *warning;     /* shown under the note in the warning colour while *value is not `safe`, or
                          * NULL */
    int safe;
} KhdaysRow;

/* The gamepad's family (from SDL_GetGamepadType): which picture and icons the
 * Controls section shows. */
enum KhdaysPadFamily {
    KHDAYS_PAD_GENERIC, /* none connected, or one SDL does not name: shown as an Xbox pad */
    KHDAYS_PAD_XBOX,
    KHDAYS_PAD_PLAYSTATION,
    KHDAYS_PAD_NINTENDO /* its face buttons are lettered the other way round from Xbox's */
};

/* What the rows depend on besides the profile. */
typedef struct KhdaysModelContext {
    int graphics_card;   /* the picture is drawn by the graphics card */
    int in_session;      /* a multiplayer session is open */
    int hd_videos;       /* the HD videos are installed (the movies' volume has a sample) */
    const char *gamepad; /* the gamepad's name, or NULL */
    int pad_family;      /* KhdaysPadFamily */
    int guest;           /* a guest in a multiplayer session: the host's gameplay mods' options count */
} KhdaysModelContext;

/* The most rows a section can have: one binding per action, plus the
 * section's other rows. */
#define KHDAYS_MODEL_ROWS_MAX (KHDAYS_ACT_COUNT + 32)

/* Loads the words the rows use, in the menu's language (the profile's
 * choice, unless KHDAYS_LANGUAGE is set). Call it before building rows. */
void khdays_model_refresh(const KhdaysProfile *work);

const char *khdays_model_section_title(int section);

/* Builds the rows of `section` for the profile `work` (the rows point into
 * it). Returns how many. */
int khdays_model_rows(int section, KhdaysProfile *work, const KhdaysModelContext *context, KhdaysRow *rows);

/* The same into `room` rows; returns how many the section has, which may be more than `room` (the Mods
 * section grows with the mods: build it again into that many). */
int khdays_model_rows_into(int section, KhdaysProfile *work, const KhdaysModelContext *context, KhdaysRow *rows,
                           int room);

/* Whether the menu shows a section: the Mods section only while a scripted mod or a native plugin is present. */
int khdays_model_section_shown(int section);

/* Enter on a command row (a mod's button, its place in the list, Resume, or the profile's commands). */
void khdays_model_row_command(KhdaysProfile *work, const KhdaysRow *row);

/* An action's name, as the player reads it. */
const char *khdays_model_action_label(int action);

/* A choice row's current choice, and setting one (with its side effects:
 * choosing one screen also switches the aspect from 4:3 to 16:9). */
int khdays_model_choice_of(const KhdaysRow *row);
void khdays_model_set_choice(KhdaysProfile *work, const KhdaysRow *row, int choice);

/* Left (-1) or right (+1) on a row: a number moves by its step within its
 * range, a choice moves round its choices, a switch goes off or on. Returns
 * whether the row has a value to change (a binding, a command or a text has
 * none). */
int khdays_model_adjust(KhdaysProfile *work, const KhdaysRow *row, int dir);

/* Enter on a switch or a choice: flips the switch, or picks the next choice. */
void khdays_model_activate(KhdaysProfile *work, const KhdaysRow *row);

/* Applies a command to the profile. (The menu asks before applying a preset;
 * this is called once the player agreed.) */
void khdays_model_command(KhdaysProfile *work, int command);

/* Lists in `text` the DS buttons that have no input on either device.
 * Returns how many. */
int khdays_model_unbound(const KhdaysProfile *work, char *text, int size);

/* Which parts of two profiles differ (KHDAYS_CHANGE_* bits). */
enum {
    KHDAYS_CHANGE_CONTROLS = 1 << 0, /* bindings, preset, enhancements, sticks */
    KHDAYS_CHANGE_VIDEO = 1 << 1,
    KHDAYS_CHANGE_SOUND = 1 << 2,
    KHDAYS_CHANGE_NETWORK = 1 << 3,
    KHDAYS_CHANGE_LANGUAGE = 1 << 4,
    KHDAYS_CHANGE_LOG = 1 << 5
};
unsigned khdays_model_changes(const KhdaysProfile *a, const KhdaysProfile *b);

#ifdef __cplusplus
}
#endif

#endif
