/* What the settings menu shows (see settings_model.h): the settings of the
 * old pages laid out as the sections of one menu, with the same ranges,
 * steps, choices and notes. */
#include "settings_model.h"
#include "../app/host.h"
#include "../app/instance.h"
#include "../screens/ui_text.h"
#include "../../runtime/scripts/queue.h"
#include "../../runtime/scripts/view.h"
#include "../../runtime/plugins/plugins.h"
#include "../../runtime/plugins/shown.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(KHDAYS_AVAILABLE_ENHANCEMENTS == (KHDAYS_ENH_ANALOG_CAMERA | KHDAYS_ENH_SEPARATE_LOCK_ON |
                                                 KHDAYS_ENH_DPAD_COMMAND_MENU | KHDAYS_ENH_ANALOG_MOVEMENT),
               "KHDAYS_AVAILABLE_ENHANCEMENTS names the four enhancements");

/* The words of the choices, in the menu's language (khdays_model_refresh). */
static const char *stick_roles[3];
static const char *normal_inverted[2];
static const char *languages[KHDAYS_LANGUAGE_LAST + 1];
/* The internal resolutions: the window's size (the default), then 2x to 16x. 1x, the DS's
 * own picture, is not offered: on a PC it is too small to read
 * (one screen even shrinks the HUD's pieces below it). A profile that holds it (written by
 * hand, for comparing with the DS) shows it first, so the menu says what it is. */
enum { RESOLUTION_CHOICES = 17 };
static const char *resolutions[RESOLUTION_CHOICES], *aspects[KHDAYS_ASPECT_COUNT], *scalings[2],
    *movies[KHDAYS_MOVIES_COUNT], *paces[2], *screens_choices[2], *backends[KHDAYS_BACKEND_COUNT],
    *displays[KHDAYS_DISPLAY_COUNT], *antialiasings[4];
static const int antialiasing_values[4] = {1, 2, 3, 4};
static const char *autosaves[3], *save_files[2];
static const char *hud_anchors[9], *mission_visibility[2];
static const int hud_anchor_values[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
/* the choices' order on screen: file 99 (the default) first */
static const int autosave_values[3] = {KHDAYS_AUTOSAVE_FILE99, KHDAYS_AUTOSAVE_PLAYED, KHDAYS_AUTOSAVE_OFF};
static const int save_files_values[2] = {1, 0};
static const int resolution_values[RESOLUTION_CHOICES] = {
    1, KHDAYS_RESOLUTION_WINDOW, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
/* texts put together from several parts */
static char preset_line[160], kh2_note[1024], menu_note[512], automatic[64];

void khdays_model_refresh(const KhdaysProfile *work)
{
    khdays_ui_set_language(khdays_language_choose(work->language));
    for (int i = 0; i < 9; ++i)
        hud_anchors[i] = khdays_text(TXT_HUD_ANCHOR_0 + i);
    mission_visibility[0] = khdays_text(TXT_HUD_ALWAYS);
    mission_visibility[1] = khdays_text(TXT_HUD_PAUSE_ONLY);
    stick_roles[0] = khdays_text(TXT_NOTHING);
    stick_roles[1] = khdays_text(TXT_DPAD_MOVE);
    stick_roles[2] = khdays_text(TXT_CAMERA);
    normal_inverted[0] = khdays_text(TXT_NORMAL);
    normal_inverted[1] = khdays_text(TXT_INVERTED);
    for (int l = 0; l <= KHDAYS_LANGUAGE_LAST; ++l) {
        languages[l] = khdays_language_name(l);
    }
    {
        static char names[RESOLUTION_CHOICES - 2][8];
        resolutions[0] = "1x (DS)";
        resolutions[1] = khdays_text(TXT_VIDEO_RESOLUTION_WINDOW);
        for (int i = 2; i < RESOLUTION_CHOICES; ++i) {
            SDL_snprintf(names[i - 2], sizeof(names[i - 2]), "%dx", resolution_values[i]);
            resolutions[i] = names[i - 2];
        }
    }
    antialiasings[0] = khdays_text(TXT_OFF);
    antialiasings[1] = "2x";
    antialiasings[2] = "3x";
    antialiasings[3] = "4x";
    displays[KHDAYS_DISPLAY_WINDOW] = khdays_text(TXT_VIDEO_DISPLAY_WINDOW);
    displays[KHDAYS_DISPLAY_BORDERLESS] = khdays_text(TXT_VIDEO_DISPLAY_BORDERLESS);
    displays[KHDAYS_DISPLAY_FULLSCREEN] = khdays_text(TXT_VIDEO_DISPLAY_FULLSCREEN);
    screens_choices[0] = khdays_text(TXT_VIDEO_SCREENS_TWO);
    screens_choices[1] = khdays_text(TXT_VIDEO_SCREENS_ONE);
    aspects[KHDAYS_ASPECT_4_3] = "4:3 (DS)";
    aspects[KHDAYS_ASPECT_16_9] = "16:9";
    aspects[KHDAYS_ASPECT_16_10] = "16:10";
    aspects[KHDAYS_ASPECT_WINDOW] = khdays_text(TXT_VIDEO_ASPECT_WINDOW);
    scalings[0] = khdays_text(TXT_VIDEO_SHARP);
    scalings[1] = khdays_text(TXT_VIDEO_SMOOTH);
    movies[KHDAYS_MOVIES_HD] = khdays_text(TXT_VIDEO_MOVIES_HD);
    movies[KHDAYS_MOVIES_PC] = khdays_text(TXT_VIDEO_MOVIES_PC);
    movies[KHDAYS_MOVIES_DS] = khdays_text(TXT_VIDEO_MOVIES_DS);
    paces[0] = khdays_text(TXT_VIDEO_PACE_SMOOTH);
    paces[1] = khdays_text(TXT_VIDEO_PACE_DS);
    backends[KHDAYS_BACKEND_AUTO] = khdays_text(TXT_VIDEO_BACKEND_AUTO);
    backends[KHDAYS_BACKEND_D3D12] = "Direct3D 12";
    backends[KHDAYS_BACKEND_VULKAN] = "Vulkan";
    autosaves[0] = khdays_text(TXT_AUTOSAVE_FILE99);
    autosaves[1] = khdays_text(TXT_AUTOSAVE_PLAYED);
    autosaves[2] = khdays_text(TXT_MOD_OFF);
    save_files[0] = khdays_text(TXT_SAVE_FILES_ALL);
    save_files[1] = khdays_text(TXT_SAVE_FILES_DS);
}

const char *khdays_model_section_title(int section)
{
    static const int ids[KHDAYS_SECTION_COUNT] = {
        TXT_CONTROLS,    TXT_ENHANCEMENTS, TXT_STICKS_AND_CAMERA, TXT_VIDEO, TXT_SOUND,
        TXT_MULTIPLAYER, TXT_LANGUAGE,     TXT_GENERAL,           TXT_MODS,
    };
    return section >= 0 && section < KHDAYS_SECTION_COUNT ? khdays_text(ids[section]) : "?";
}

/* The action names the player reads (the settings file uses profile.c's
 * names instead). The DS's own buttons keep their names. */
const char *khdays_model_action_label(int action)
{
    static const char *const ds[KHDAYS_ACT_COUNT] = {
        [KHDAYS_ACT_A] = "A", [KHDAYS_ACT_B] = "B", [KHDAYS_ACT_X] = "X",         [KHDAYS_ACT_Y] = "Y",
        [KHDAYS_ACT_L] = "L", [KHDAYS_ACT_R] = "R", [KHDAYS_ACT_START] = "START", [KHDAYS_ACT_SELECT] = "SELECT",
    };
    static const int ids[KHDAYS_ACT_COUNT] = {
        [KHDAYS_ACT_UP] = TXT_ACT_UP,
        [KHDAYS_ACT_DOWN] = TXT_ACT_DOWN,
        [KHDAYS_ACT_LEFT] = TXT_ACT_LEFT,
        [KHDAYS_ACT_RIGHT] = TXT_ACT_RIGHT,
        [KHDAYS_ACT_LOCK_ON] = TXT_ACT_LOCK_ON,
        [KHDAYS_ACT_TARGET_NEXT] = TXT_ACT_TARGET_NEXT,
        [KHDAYS_ACT_TARGET_PREV] = TXT_ACT_TARGET_PREV,
        [KHDAYS_ACT_MENU_UP] = TXT_ACT_MENU_UP,
        [KHDAYS_ACT_MENU_DOWN] = TXT_ACT_MENU_DOWN,
        [KHDAYS_ACT_MENU_LEFT] = TXT_ACT_MENU_LEFT,
        [KHDAYS_ACT_MENU_RIGHT] = TXT_ACT_MENU_RIGHT,
        [KHDAYS_ACT_OPEN_SETTINGS] = TXT_ACT_OPEN_SETTINGS,
        [KHDAYS_ACT_HUD_MODE] = TXT_ACT_HUD_MODE,
    };
    if (action < 0 || action >= KHDAYS_ACT_COUNT) {
        return "?";
    }
    return ds[action] != NULL ? ds[action] : khdays_text(ids[action]);
}

/* ---- Building the rows -------------------------------------------------- */

typedef struct Builder {
    KhdaysRow *rows;
    int count, room; /* rows past `room` are counted, not kept */
    const KhdaysModelContext *context;
} Builder;

static KhdaysRow *add(Builder *b, int kind, int id, const char *label)
{
    static KhdaysRow beyond;
    KhdaysRow *r = b->count < b->room ? &b->rows[b->count] : &beyond;
    ++b->count;
    memset(r, 0, sizeof(*r));
    r->kind = kind;
    r->id = id;
    r->label = label;
    r->available = 1;
    r->apply = KHDAYS_APPLY_NOW;
    r->takes = -1;
    return r;
}

static KhdaysRow *add_switch(Builder *b, int id, int text, int *value, int available, const char *note)
{
    KhdaysRow *r = add(b, KHDAYS_ROW_SWITCH, id, khdays_text(text));
    r->value = value;
    r->high = 1;
    r->available = available;
    r->note = available ? note : khdays_text(TXT_NOT_AVAILABLE);
    return r;
}

static KhdaysRow *add_number(Builder *b, int id, int text, int *value, int low, int high, int step)
{
    KhdaysRow *r = add(b, KHDAYS_ROW_NUMBER, id, khdays_text(text));
    r->value = value;
    r->low = low;
    r->high = high;
    r->step = step;
    return r;
}

static KhdaysRow *add_choice(Builder *b, int id, int text, int *value, const char *const *choices, int n)
{
    KhdaysRow *r = add(b, KHDAYS_ROW_CHOICE, id, khdays_text(text));
    r->value = value;
    r->choices = choices;
    r->high = n - 1;
    return r;
}

static KhdaysRow *add_command(Builder *b, int id, int text, int command)
{
    KhdaysRow *r = add(b, KHDAYS_ROW_COMMAND, id, khdays_text(text));
    r->command = command;
    return r;
}

static KhdaysRow *add_text(Builder *b, int id, int label, char *text, int size, int (*accepts)(int c),
                           const char *placeholder)
{
    KhdaysRow *r = add(b, KHDAYS_ROW_TEXT, id, khdays_text(label));
    r->text = text;
    r->text_size = size;
    r->accepts = accepts;
    r->placeholder = placeholder;
    return r;
}

static void add_binding(Builder *b, int layer, int action, unsigned cells)
{
    KhdaysRow *r = add(b, KHDAYS_ROW_BINDING, KHDAYS_SET_BINDING, khdays_model_action_label(action));
    r->layer = layer;
    r->action = action;
    r->cells = cells;
}

/* A row that needs the graphics card, unavailable without it. */
static void needs_graphics_card(Builder *b, KhdaysRow *r, int note)
{
    r->available = b->context->graphics_card;
    r->note = r->available ? khdays_text(note) : khdays_text(TXT_VIDEO_NEEDS_GPU);
}

enum {
    ALL_CELLS = (1u << KHDAYS_CELLS) - 1,
    PAD_CELLS = ((1u << KHDAYS_INPUTS_PER_ACTION) - 1) << KHDAYS_INPUTS_PER_ACTION
};

static void build_controls(Builder *b, KhdaysProfile *w)
{
    static const int ds_order[] = {
        KHDAYS_ACT_UP, KHDAYS_ACT_DOWN, KHDAYS_ACT_LEFT, KHDAYS_ACT_RIGHT, KHDAYS_ACT_A,     KHDAYS_ACT_B,
        KHDAYS_ACT_X,  KHDAYS_ACT_Y,    KHDAYS_ACT_L,    KHDAYS_ACT_R,     KHDAYS_ACT_START, KHDAYS_ACT_SELECT,
    };
    const char *pad = b->context->gamepad;
    add(b, KHDAYS_ROW_LINE, KHDAYS_SET_GAMEPAD_NAME, pad != NULL ? pad : khdays_text(TXT_NO_GAMEPAD));
    SDL_snprintf(preset_line, sizeof(preset_line), "%s: %s%s", khdays_text(TXT_GAMEPAD_PRESET),
                 w->preset == KHDAYS_PRESET_KH2 ? "Kingdom Hearts II" : "DS",
                 khdays_profile_matches_preset(w, KHDAYS_AVAILABLE_ENHANCEMENTS) ? "" : khdays_text(TXT_MODIFIED));
    add(b, KHDAYS_ROW_LINE, KHDAYS_SET_PRESET, preset_line);
    add_command(b, KHDAYS_SET_PRESET_DS, TXT_PRESET_DS, KHDAYS_CMD_PRESET_DS);
    {
        char shortcuts[256];
        SDL_snprintf(shortcuts, sizeof(shortcuts), khdays_text(TXT_KH2_SHORTCUTS2), khdays_text(TXT_GAME_CONFIG),
                     khdays_text(TXT_GAME_CONTROLS), khdays_text(TXT_GAME_TYPE_A));
        SDL_strlcpy(kh2_note, khdays_text(TXT_KH2_TITLE), sizeof(kh2_note));
        for (int line = TXT_KH2_LINE1; line <= TXT_KH2_LINE8; ++line) {
            const char *s = khdays_text(line);
            while (*s == ' ') {
                ++s; /* drop the old page's indent: here it is one paragraph */
            }
            SDL_strlcat(kh2_note, " ", sizeof(kh2_note));
            SDL_strlcat(kh2_note, s, sizeof(kh2_note));
        }
        SDL_strlcat(kh2_note, " ", sizeof(kh2_note));
        SDL_strlcat(kh2_note, khdays_text(TXT_KH2_SHORTCUTS1), sizeof(kh2_note));
        SDL_strlcat(kh2_note, " ", sizeof(kh2_note));
        SDL_strlcat(kh2_note, shortcuts, sizeof(kh2_note));
        SDL_strlcat(kh2_note, " ", sizeof(kh2_note));
        SDL_strlcat(kh2_note, khdays_text(TXT_KEYBOARD_UNCHANGED), sizeof(kh2_note));
    }
    add_command(b, KHDAYS_SET_PRESET_KH2, TXT_PRESET_KH2, KHDAYS_CMD_PRESET_KH2)->note = kh2_note;
    add(b, KHDAYS_ROW_PAD, KHDAYS_SET_PAD_VIEW, ""); /* the gamepad's picture */
    for (size_t i = 0; i < sizeof(ds_order) / sizeof(ds_order[0]); ++i) {
        add_binding(b, KHDAYS_LAYER_BASE, ds_order[i], ALL_CELLS);
    }
    add_binding(b, KHDAYS_LAYER_BASE, KHDAYS_ACT_OPEN_SETTINGS, PAD_CELLS); /* the keyboard always has Esc */
    add_binding(b, KHDAYS_LAYER_BASE, KHDAYS_ACT_HUD_MODE, ALL_CELLS);      /* one screen's HUD mode */
    if (w->enh.separate_lock_on) {
        add(b, KHDAYS_ROW_HEADING, KHDAYS_SET_NONE, khdays_text(TXT_HEADING_LOCK_ON));
        add_binding(b, KHDAYS_LAYER_BASE, KHDAYS_ACT_LOCK_ON, ALL_CELLS);
        add_binding(b, KHDAYS_LAYER_BASE, KHDAYS_ACT_TARGET_NEXT, ALL_CELLS);
        add_binding(b, KHDAYS_LAYER_BASE, KHDAYS_ACT_TARGET_PREV, ALL_CELLS);
    }
    if (w->enh.dpad_command_menu) {
        add(b, KHDAYS_ROW_HEADING, KHDAYS_SET_NONE, khdays_text(TXT_HEADING_COMMAND_MENU));
        for (int a = KHDAYS_ACT_MENU_UP; a <= KHDAYS_ACT_MENU_RIGHT; ++a) {
            add_binding(b, KHDAYS_LAYER_FIELD, a, ALL_CELLS);
        }
    }
    add(b, KHDAYS_ROW_TEST, KHDAYS_SET_TEST, "");
    add_command(b, KHDAYS_SET_RESTORE_KEYBOARD, TXT_RESTORE_KEYBOARD, KHDAYS_CMD_RESTORE_KEYBOARD);
    add_command(b, KHDAYS_SET_RESTORE_GAMEPAD, TXT_RESTORE_GAMEPAD, KHDAYS_CMD_RESTORE_GAMEPAD);
}

/* Whether this build has an enhancement (KHDAYS_ENH_*). */
static int built(unsigned enhancement)
{
    return (KHDAYS_AVAILABLE_ENHANCEMENTS & enhancement) != 0;
}

static void build_enhancements(Builder *b, KhdaysProfile *w)
{
    KhdaysEnhancements *e = &w->enh;
    SDL_snprintf(menu_note, sizeof(menu_note), khdays_text(TXT_ENH_MENU_NOTE), khdays_text(TXT_GAME_CONFIG),
                 khdays_text(TXT_GAME_COMMAND_LIST), khdays_text(TXT_GAME_ONLY));
    add_switch(b, KHDAYS_SET_ENH_CAMERA, TXT_ENH_CAMERA, &e->analog_camera, built(KHDAYS_ENH_ANALOG_CAMERA),
               khdays_text(TXT_ENH_CAMERA_NOTE));
    add_switch(b, KHDAYS_SET_ENH_LOCK_ON, TXT_ENH_LOCK_ON, &e->separate_lock_on, built(KHDAYS_ENH_SEPARATE_LOCK_ON),
               khdays_text(TXT_ENH_LOCK_ON_NOTE));
    add_switch(b, KHDAYS_SET_ENH_MENU, TXT_ENH_MENU, &e->dpad_command_menu, built(KHDAYS_ENH_DPAD_COMMAND_MENU),
               menu_note);
    add_switch(b, KHDAYS_SET_ENH_MOVEMENT, TXT_ENH_MOVEMENT, &e->analog_movement, built(KHDAYS_ENH_ANALOG_MOVEMENT),
               khdays_text(TXT_ENH_MOVEMENT_NOTE));
    /* the mouse turns the camera where the analog camera does (runtime/input/controls_hooks.c) */
    add_switch(b, KHDAYS_SET_ENH_MOUSE, TXT_ENH_MOUSE, &e->mouse_camera, built(KHDAYS_ENH_ANALOG_CAMERA),
               khdays_text(TXT_ENH_MOUSE_NOTE));
    /* Chase Camera's Free, as the camp's Config page drawn on one screen sets it (runtime/menus/camp.h): here too,
     * for two screens, where the game draws its own page */
    add_switch(b, KHDAYS_SET_ENH_CHASE_FREE, TXT_ENH_CHASE_FREE, &e->chase_free, 1,
               khdays_text(TXT_ENH_CHASE_FREE_NOTE));
    add(b, KHDAYS_ROW_HEADING, KHDAYS_SET_NONE, khdays_text(TXT_HUD_REPLACEMENTS));
    add_switch(b, KHDAYS_SET_HUD_FONT, TXT_HUD_FONT, &e->hud[KHDAYS_OPT_FONT], 1,
               khdays_text(TXT_HUD_REPLACEMENT_NOTE));
    add_switch(b, KHDAYS_SET_HUD_MINIMAP, TXT_HUD_MINIMAP, &e->hud[KHDAYS_OPT_MINIMAP], 1,
               khdays_text(TXT_HUD_MINIMAP_NOTE));
    add_switch(b, KHDAYS_SET_HUD_PARTY_HP, TXT_HUD_PARTY_HP, &e->hud[KHDAYS_OPT_PARTY_HP], 1,
               khdays_text(TXT_HUD_PARTY_NOTE));
    add_switch(b, KHDAYS_SET_HUD_ENEMY_HP, TXT_HUD_ENEMY_HP, &e->hud[KHDAYS_OPT_ENEMY_HP], 1,
               khdays_text(TXT_HUD_REPLACEMENT_NOTE));
    add_switch(b, KHDAYS_SET_HUD_MISSION, TXT_HUD_MISSION, &e->hud[KHDAYS_OPT_MISSION], 1,
               khdays_text(TXT_HUD_REPLACEMENT_NOTE));
    add_switch(b, KHDAYS_SET_HUD_INFORMATION, TXT_HUD_INFORMATION, &e->hud[KHDAYS_OPT_INFORMATION], 1,
               khdays_text(TXT_HUD_INFORMATION_NOTE));
    add_switch(b, KHDAYS_SET_HUD_RETICLES, TXT_HUD_RETICLES, &e->hud[KHDAYS_OPT_RETICLES], 1,
               khdays_text(TXT_HUD_REPLACEMENT_NOTE));
    add_switch(b, KHDAYS_SET_HUD_LOCKED_ON, TXT_HUD_LOCKED_ON, &e->hud[KHDAYS_OPT_LOCKED_ON], 1,
               khdays_text(TXT_HUD_REPLACEMENT_NOTE));
    add_switch(b, KHDAYS_SET_HUD_NOTIFICATIONS, TXT_HUD_NOTIFICATIONS, &e->hud[KHDAYS_OPT_NOTIFICATIONS], 1,
               khdays_text(TXT_HUD_NOTIFICATIONS_NOTE));
    add_switch(b, KHDAYS_SET_HUD_HEART_POINTS, TXT_HUD_HEART_POINTS, &e->hud[KHDAYS_OPT_HEART_POINTS], 1,
               khdays_text(TXT_HUD_REPLACEMENT_NOTE));
    add_switch(b, KHDAYS_SET_HUD_PRIZES, TXT_HUD_PRIZES, &e->hud[KHDAYS_OPT_PRIZES], 1,
               khdays_text(TXT_HUD_REPLACEMENT_NOTE));
    add_switch(b, KHDAYS_SET_HUD_PAUSE_TARGET, TXT_HUD_PAUSE_TARGET, &e->hud[KHDAYS_OPT_PAUSE_TARGET], 1,
               khdays_text(TXT_HUD_REPLACEMENT_NOTE));
    add_switch(b, KHDAYS_SET_HUD_INFORMATION_HUD, TXT_HUD_INFORMATION_HUD, &e->hud[KHDAYS_OPT_INFORMATION_HUD], 1,
               khdays_text(TXT_HUD_INFORMATION_HUD_NOTE));
    add_switch(b, KHDAYS_SET_HUD_UI_PADDING, TXT_HUD_UI_PADDING, &e->hud[KHDAYS_OPT_UI_PADDING], 1,
               khdays_text(TXT_HUD_UI_PADDING_NOTE));
    add(b, KHDAYS_ROW_HEADING, KHDAYS_SET_NONE, khdays_text(TXT_HUD_MISSION_LAYOUT));
    add_choice(b, KHDAYS_SET_HUD_MISSION_VISIBILITY, TXT_HUD_MISSION_VISIBILITY, &e->hud[KHDAYS_OPT_MISSION_VISIBILITY],
               mission_visibility, 2);
    add_choice(b, KHDAYS_SET_HUD_MISSION_ANCHOR, TXT_HUD_MISSION_ANCHOR, &e->hud[KHDAYS_OPT_MISSION_ANCHOR],
               hud_anchors, 9)
        ->values = hud_anchor_values;
    add_choice(b, KHDAYS_SET_HUD_MISSION_PAUSE_ANCHOR, TXT_HUD_MISSION_PAUSE_ANCHOR,
               &e->hud[KHDAYS_OPT_MISSION_PAUSE_ANCHOR], hud_anchors, 9)
        ->values = hud_anchor_values;
}

static void build_sticks(Builder *b, KhdaysProfile *w)
{
    KhdaysEnhancements *e = &w->enh;
    add_choice(b, KHDAYS_SET_LEFT_STICK, TXT_LEFT_STICK, &w->stick[0].role, stick_roles, 3);
    add_choice(b, KHDAYS_SET_RIGHT_STICK, TXT_RIGHT_STICK, &w->stick[1].role, stick_roles, 3);
    add_number(b, KHDAYS_SET_LEFT_DEAD_ZONE, TXT_LEFT_DEAD_ZONE, &w->stick[0].dead_zone, 5, 90, 5);
    add_number(b, KHDAYS_SET_RIGHT_DEAD_ZONE, TXT_RIGHT_DEAD_ZONE, &w->stick[1].dead_zone, 5, 90, 5);
    add_number(b, KHDAYS_SET_LEFT_HYSTERESIS, TXT_LEFT_HYSTERESIS, &w->stick[0].hysteresis, 0, 20, 1);
    add_number(b, KHDAYS_SET_RIGHT_HYSTERESIS, TXT_RIGHT_HYSTERESIS, &w->stick[1].hysteresis, 0, 20, 1);
    if (built(KHDAYS_ENH_ANALOG_CAMERA)) {
        add_number(b, KHDAYS_SET_CAMERA_SPEED, TXT_CAMERA_SPEED, &e->camera_speed, 10, 300, 10);
        add_choice(b, KHDAYS_SET_CAMERA_X, TXT_CAMERA_X, &e->camera_invert_x, normal_inverted, 2);
        add_choice(b, KHDAYS_SET_CAMERA_Y, TXT_CAMERA_Y, &e->camera_invert_y, normal_inverted, 2);
        add_number(b, KHDAYS_SET_MOUSE_SPEED, TXT_MOUSE_SPEED, &e->mouse_speed, 10, 300, 10);
    }
    if (built(KHDAYS_ENH_ANALOG_MOVEMENT)) {
        add_number(b, KHDAYS_SET_RUN_FROM, TXT_RUN_FROM, &e->run_threshold, 50, 95, 5);
        add_number(b, KHDAYS_SET_WALK_SPEED, TXT_WALK_SPEED, &e->walk_speed, 20, 90, 5);
    }
    add(b, KHDAYS_ROW_STICKS, KHDAYS_SET_STICKS_VIEW, "");
}

static void build_video(Builder *b, KhdaysProfile *w)
{
    KhdaysVideo *v = &w->video;
    const int first = v->internal_resolution == 1 ? 0 : 1; /* 1x only when the profile holds it (above) */
    add_choice(b, KHDAYS_SET_DISPLAY, TXT_VIDEO_DISPLAY, &v->display, displays, KHDAYS_DISPLAY_COUNT)->note =
        khdays_text(TXT_VIDEO_DISPLAY_NOTE);
    KhdaysRow *r = add_choice(b, KHDAYS_SET_RESOLUTION, TXT_VIDEO_RESOLUTION, &v->internal_resolution,
                              resolutions + first, RESOLUTION_CHOICES - first);
    r->values = resolution_values + first;
    needs_graphics_card(b, r, TXT_VIDEO_RESOLUTION_NOTE);
    r = add_choice(b, KHDAYS_SET_ANTIALIASING, TXT_VIDEO_ANTIALIASING, &v->antialiasing, antialiasings, 4);
    r->values = antialiasing_values;
    needs_graphics_card(b, r, TXT_VIDEO_ANTIALIASING_NOTE);
    r->warning = khdays_text(TXT_VIDEO_ANTIALIASING_WARNING); /* on top of the internal resolution's */
    r->safe = 1;                                              /* off */
    needs_graphics_card(b, add_choice(b, KHDAYS_SET_ASPECT, TXT_VIDEO_ASPECT, &v->aspect, aspects, KHDAYS_ASPECT_COUNT),
                        TXT_VIDEO_ASPECT_NOTE);
    needs_graphics_card(b, add_choice(b, KHDAYS_SET_SCREENS, TXT_VIDEO_SCREENS, &v->one_screen, screens_choices, 2),
                        TXT_VIDEO_SCREENS_NOTE);
    needs_graphics_card(b, add_number(b, KHDAYS_SET_HUD_SIZE, TXT_VIDEO_HUD_SIZE, &v->hud_size, 1, 8, 1),
                        TXT_VIDEO_HUD_SIZE_NOTE);
    add_choice(b, KHDAYS_SET_SCALING, TXT_VIDEO_SCALING, &v->smooth_scaling, scalings, 2)->note =
        khdays_text(TXT_VIDEO_SCALING_NOTE);
    needs_graphics_card(b, add_switch(b, KHDAYS_SET_TEXTURES, TXT_VIDEO_TEXTURES, &v->texture_smoothing, 1, NULL),
                        TXT_VIDEO_TEXTURES_NOTE);
    /* HD graphics and dumping work with either renderer */
    add_switch(b, KHDAYS_SET_HD_GRAPHICS, TXT_VIDEO_HD, &v->hd_graphics, 0, NULL);
    /* the dump is read at each frame (textures.c, tiles2d.c, sheet_colours.c): a 3D texture
     * already shown is written when its room or screen is loaded again */
    add_switch(b, KHDAYS_SET_DUMP_GRAPHICS, TXT_VIDEO_DUMP, &v->dump_graphics, 1, khdays_text(TXT_VIDEO_DUMP_NOTE));
    r = add_choice(b, KHDAYS_SET_MOVIES, TXT_VIDEO_MOVIES, &v->movies, movies, KHDAYS_MOVIES_COUNT);
    r->note = khdays_text(TXT_VIDEO_MOVIES_NOTE);
    r->apply = KHDAYS_APPLY_NEXT;
    r->takes = TXT_TAKES_NEXT_MOVIE;
    add_switch(b, KHDAYS_SET_SUBTITLES, TXT_VIDEO_SUBTITLES, &v->subtitles, 1, khdays_text(TXT_VIDEO_SUBTITLES_NOTE));
    r = add_choice(b, KHDAYS_SET_PACE, TXT_VIDEO_PACE, &v->picture_ds, paces, 2);
    r->note = khdays_text(TXT_VIDEO_PACE_NOTE);
    r->warning = khdays_text(TXT_VIDEO_PACE_WARNING); /* the pictures between the game's can go wrong */
    r->safe = 1;                                      /* As on the DS */
    add_switch(b, KHDAYS_SET_SHOW_FPS, TXT_VIDEO_SHOW_FPS, &v->show_fps, 1, khdays_text(TXT_VIDEO_SHOW_FPS_NOTE));
    add_choice(b, KHDAYS_SET_BACKEND, TXT_VIDEO_BACKEND, &v->backend, backends, KHDAYS_BACKEND_COUNT)->note =
        khdays_text(TXT_VIDEO_BACKEND_NOTE);
    add_command(b, KHDAYS_SET_RESTORE_VIDEO, TXT_RESTORE_VIDEO, KHDAYS_CMD_RESTORE_VIDEO);
}

static void build_sound(Builder *b, KhdaysProfile *w)
{
    static const struct {
        int id, label, note;
    } volumes[] = {
        {KHDAYS_SET_VOLUME, TXT_SOUND_VOLUME, TXT_SOUND_VOLUME_NOTE},
        {KHDAYS_SET_MUSIC, TXT_SOUND_MUSIC, TXT_SOUND_MUSIC_NOTE},
        {KHDAYS_SET_EFFECTS, TXT_SOUND_EFFECTS, TXT_SOUND_EFFECTS_NOTE},
        {KHDAYS_SET_VOICES, TXT_SOUND_VOICES, TXT_SOUND_VOICES_NOTE},
        {KHDAYS_SET_MOVIES_VOLUME, TXT_SOUND_MOVIES, TXT_SOUND_MOVIES_NOTE},
    };
    KhdaysSound *s = &w->sound;
    int *values[] = {&s->volume, &s->music, &s->effects, &s->voices, &s->movies};
    for (size_t i = 0; i < sizeof(volumes) / sizeof(volumes[0]); ++i) {
        KhdaysRow *r = add_number(b, volumes[i].id, volumes[i].label, values[i], 0, 100, 5);
        r->note = khdays_text(volumes[i].note);
        if (b->context->in_session) {
            r->apply = KHDAYS_APPLY_NOW; /* in a session the game is not paused, so it is heard */
        } else if (volumes[i].id == KHDAYS_SET_MOVIES_VOLUME && !b->context->hd_videos) {
            r->apply = KHDAYS_APPLY_NEXT; /* no movie sound to play as a sample */
            r->takes = TXT_TAKES_NEXT_MOVIE;
        } else {
            r->apply = KHDAYS_APPLY_SAMPLE;
        }
    }
    {
        KhdaysRow *r = add_switch(b, KHDAYS_SET_DS_EXACT, TXT_SOUND_DS_EXACT, &s->ds_exact, 1,
                                  khdays_text(TXT_SOUND_DS_EXACT_NOTE));
        if (!b->context->in_session) {
            r->apply = KHDAYS_APPLY_RESUME;
            r->takes = TXT_TAKES_RESUME;
        }
    }
    add_command(b, KHDAYS_SET_RESTORE_SOUND, TXT_RESTORE_SOUND, KHDAYS_CMD_RESTORE_SOUND);
}

static void in_session(Builder *b, KhdaysRow *r)
{
    if (b->context->in_session) {
        r->apply = KHDAYS_APPLY_NEXT;
        r->takes = TXT_TAKES_NEXT_SESSION;
    }
}

static void build_multiplayer(Builder *b, KhdaysProfile *w)
{
    KhdaysNetwork *n = &w->network;
    KhdaysRow *r;
    khdays_host_automatic_name(automatic, sizeof(automatic));
    r = add_text(b, KHDAYS_SET_NICKNAME, TXT_NICKNAME, n->nickname, (int)sizeof(n->nickname), khdays_nickname_char,
                 automatic);
    r->note = khdays_text(TXT_NICKNAME_NOTE);
    in_session(b, r);
    r = add_switch(b, KHDAYS_SET_ONLINE, TXT_ONLINE, &n->online, 1, khdays_text(TXT_ONLINE_NOTE));
    in_session(b, r);
    r = add_text(b, KHDAYS_SET_SERVER, TXT_SERVER, n->server, (int)sizeof(n->server), khdays_server_char, NULL);
    r->note = khdays_text(TXT_SERVER_NOTE);
    r->available = n->online;
    in_session(b, r);
    /* takes effect at once, in a session or not */
    add_switch(b, KHDAYS_SET_SHOW_PING, TXT_SHOW_PING, &n->show_ping, 1, khdays_text(TXT_SHOW_PING_NOTE));
    /* read at the next start (host/app/updates.c) */
    add_switch(b, KHDAYS_SET_CHECK_UPDATES, TXT_CHECK_UPDATES, &n->check_updates, 1,
               khdays_text(TXT_CHECK_UPDATES_NOTE));
}

static void build_language(Builder *b, KhdaysProfile *w)
{
    KhdaysRow *r = add_choice(b, KHDAYS_SET_LANGUAGE, TXT_LANGUAGE, &w->language, languages, KHDAYS_LANGUAGE_LAST + 1);
    /* The menu's own text changes at once. For the game, KHDAYS_LANGUAGE
     * takes precedence over the setting, as it always has. */
    r->note = khdays_text(khdays_language_from_code(SDL_getenv("KHDAYS_LANGUAGE")) != 0 ? TXT_LANGUAGE_FIXED_NOTE
                                                                                        : TXT_LANGUAGE_NOTE);
}

static void build_general(Builder *b, KhdaysProfile *w)
{
    KhdaysRow *r;
    /* both read by the game's save lists when the game goes on (runtime/menus/savelist.h) */
    r = add_choice(b, KHDAYS_SET_AUTOSAVE, TXT_AUTOSAVE, &w->save.autosave, autosaves, 3);
    r->values = autosave_values;
    r->note = khdays_text(TXT_AUTOSAVE_NOTE);
    r->apply = KHDAYS_APPLY_RESUME;
    r->takes = TXT_TAKES_RESUME;
    r = add_choice(b, KHDAYS_SET_SAVE_FILES, TXT_SAVE_FILES, &w->save.all_files, save_files, 2);
    r->values = save_files_values;
    r->note = khdays_text(TXT_SAVE_FILES_NOTE);
    r->apply = KHDAYS_APPLY_RESUME;
    r->takes = TXT_TAKES_RESUME;
    /* read as the game starts (runtime/battle/stop_magic.h): its records, recipes and casts made then */
    r = add_switch(b, KHDAYS_SET_STOP_MAGIC, TXT_STOP_MAGIC, &w->gameplay.stop_magic, 1,
                   khdays_text(TXT_STOP_MAGIC_NOTE));
    r->apply = KHDAYS_APPLY_NEXT;
    r->takes = TXT_TAKES_NEXT_START;
    add_switch(b, KHDAYS_SET_SESSION_LOG, TXT_SESSION_LOG, &w->session_log, 1, khdays_text(TXT_SESSION_LOG_NOTE));
    add_command(b, KHDAYS_SET_RESTORE_BOTH, TXT_RESTORE_BOTH, KHDAYS_CMD_RESTORE_BOTH);
}

/* ---- The Mods section ---------------------------------------------------- */

/* The mods as the game's thread last showed them (runtime/scripts/view.h), and the rows' values: each
 * mod's enabled flag, and its options' values (a choice's as the row's 0-based index) from its first. */
static KhdaysModsView mods_view;
static int *mod_enabled, *mod_first, *mod_values;
static char (*mod_heads)[160], (*mod_lines)[400];

static void mods_rebuild(void)
{
    const int n = mods_view.count;
    int options = 0;
    free(mod_enabled);
    free(mod_first);
    free(mod_values);
    free(mod_heads);
    free(mod_lines);
    for (int i = 0; i < n; ++i) {
        options += mods_view.mods[i].option_count;
    }
    mod_enabled = calloc((size_t)n + 1, sizeof(*mod_enabled));
    mod_first = calloc((size_t)n + 1, sizeof(*mod_first));
    mod_values = calloc((size_t)options + 1, sizeof(*mod_values));
    mod_heads = calloc((size_t)n + 1, sizeof(*mod_heads));
    mod_lines = calloc((size_t)n + 1, sizeof(*mod_lines));
    if (mod_enabled == NULL || mod_first == NULL || mod_values == NULL || mod_heads == NULL || mod_lines == NULL) {
        khdays_scripts_view_free(&mods_view);
        return;
    }
    options = 0;
    for (int i = 0; i < n; ++i) {
        const KhdaysModView *v = &mods_view.mods[i];
        const char *name = v->name != NULL ? v->name : v->folder;
        mod_enabled[i] = v->enabled;
        mod_first[i] = options;
        for (int o = 0; o < v->option_count; ++o) {
            const KhdaysOptionView *ov = &v->options[o];
            mod_values[options++] = (int)(ov->kind == KHDAYS_OPTION_CHOICE ? ov->value - 1 : ov->value);
        }
        if (v->author != NULL && v->version != NULL) {
            snprintf(mod_heads[i], sizeof(mod_heads[i]), "%s %s -- %s", name, v->version, v->author);
        } else {
            snprintf(mod_heads[i], sizeof(mod_heads[i]), "%s %s", name, v->version != NULL ? v->version : "");
        }
        if (v->ignored) {
            snprintf(mod_lines[i], sizeof(mod_lines[i]), "%s", khdays_text(TXT_MOD_IGNORED));
            continue;
        }
        if (v->content) {
            snprintf(mod_lines[i], sizeof(mod_lines[i]), "%s",
                     khdays_text(v->state == KHDAYS_MOD_LOADED ? TXT_MOD_FILES_ON : TXT_MOD_FILES_OFF));
            continue;
        }
        switch (v->state) {
        case KHDAYS_MOD_LOADED: snprintf(mod_lines[i], sizeof(mod_lines[i]), "%s", khdays_text(TXT_MOD_RUNNING)); break;
        case KHDAYS_MOD_OFF: snprintf(mod_lines[i], sizeof(mod_lines[i]), "%s", khdays_text(TXT_MOD_OFF)); break;
        default:
            snprintf(mod_lines[i], sizeof(mod_lines[i]),
                     khdays_text(v->state == KHDAYS_MOD_STOPPED  ? TXT_MOD_STOPPED
                                 : v->state == KHDAYS_MOD_PAUSED ? TXT_MOD_PAUSED
                                                                 : TXT_MOD_REFUSED),
                     v->reason != NULL ? v->reason : "");
            break;
        }
    }
}

/* The newest view of the mods, when there is one. */
static void mods_take(void)
{
    if (khdays_scripts_view(&mods_view)) {
        mods_rebuild();
    }
}

/* The native plugins as the game's thread last showed them (runtime/plugins/shown.h), and their rows' words. */
static KhdaysPluginsShown plugins_view;
static char (*plugin_heads)[160], (*plugin_lines)[400];

static void plugins_take(void)
{
    if (!khdays_plugins_shown(&plugins_view)) {
        return;
    }
    free(plugin_heads);
    free(plugin_lines);
    plugin_heads = calloc((size_t)plugins_view.count + 1, sizeof(*plugin_heads));
    plugin_lines = calloc((size_t)plugins_view.count + 1, sizeof(*plugin_lines));
    if (plugin_heads == NULL || plugin_lines == NULL) {
        khdays_plugins_shown_free(&plugins_view);
        return;
    }
    for (int i = 0; i < plugins_view.count; ++i) {
        const KhdaysPluginShown *v = &plugins_view.plugins[i];
        if (v->author[0] != '\0') {
            snprintf(plugin_heads[i], sizeof(plugin_heads[i]), "%s %s -- %s", v->name, v->version, v->author);
        } else {
            snprintf(plugin_heads[i], sizeof(plugin_heads[i]), "%s %s", v->name, v->version);
        }
        switch (v->state) {
        case KHDAYS_PLUGIN_STARTED:
            snprintf(plugin_lines[i], sizeof(plugin_lines[i]), "%s", khdays_text(TXT_PLUGIN_RUNNING));
            break;
        case KHDAYS_PLUGIN_STOPPED:
            snprintf(plugin_lines[i], sizeof(plugin_lines[i]), "%s", khdays_text(TXT_PLUGIN_STOPPED));
            break;
        case KHDAYS_PLUGIN_WAITING:
            snprintf(plugin_lines[i], sizeof(plugin_lines[i]), "%s",
                     khdays_text(plugins_view.safe_mode ? TXT_PLUGIN_SAFE_MODE : TXT_PLUGIN_WAITING));
            break;
        default:
            snprintf(plugin_lines[i], sizeof(plugin_lines[i]),
                     khdays_text(v->state == KHDAYS_PLUGIN_FAILED ? TXT_PLUGIN_FAILED : TXT_PLUGIN_REFUSED), v->why);
            break;
        }
    }
}

int khdays_model_section_shown(int section)
{
    if (section != KHDAYS_SECTION_MODS) {
        return section >= 0 && section < KHDAYS_SECTION_COUNT;
    }
    mods_take();
    plugins_take();
    return mods_view.count > 0 || plugins_view.count > 0;
}

/* Native code, and what became of each plugin as the game started: the switch is read at the next start. */
static void build_native_code(Builder *b, KhdaysProfile *w)
{
    KhdaysRow *r;
    plugins_take();
    if (plugins_view.count == 0 || plugin_heads == NULL) {
        return;
    }
    r = add_switch(b, KHDAYS_SET_NATIVE_CODE, TXT_NATIVE_CODE, &w->mods.native_code, 1,
                   khdays_text(TXT_NATIVE_CODE_NOTE));
    r->apply = KHDAYS_APPLY_NEXT;
    r->takes = TXT_TAKES_NEXT_START;
    for (int i = 0; i < plugins_view.count; ++i) {
        r = add(b, KHDAYS_ROW_HEADING, KHDAYS_SET_PLUGIN_HEADING, plugin_heads[i]);
        r->layer = i;
        r = add(b, KHDAYS_ROW_LINE, KHDAYS_SET_PLUGIN_STATE, plugin_lines[i]);
        r->layer = i;
    }
}

static void build_mods(Builder *b, KhdaysProfile *w)
{
    const int language = khdays_ui_language();
    mods_take();
    build_native_code(b, w);
    if (mods_view.count > 0 || b->count == 0) {
        add(b, KHDAYS_ROW_LINE, KHDAYS_SET_MOD_NOTE, khdays_text(TXT_MOD_ORDER_NOTE));
    }
    for (int i = 0; i < mods_view.count && mod_enabled != NULL; ++i) {
        const KhdaysModView *v = &mods_view.mods[i];
        KhdaysRow *r = add(b, KHDAYS_ROW_HEADING, KHDAYS_SET_MOD_HEADING, mod_heads[i]);
        r->layer = i;
        r = add(b, KHDAYS_ROW_LINE, KHDAYS_SET_MOD_STATE, mod_lines[i]);
        r->layer = i;
        if (v->memory_write) {
            r = add(b, KHDAYS_ROW_LINE, KHDAYS_SET_MOD_STATE, khdays_text(TXT_MOD_WRITES_MEMORY));
            r->layer = i;
        }
        if (!v->ignored) { /* a mod turned off by hand is turned on by hand */
            r = add(b, KHDAYS_ROW_SWITCH, KHDAYS_SET_MOD_ENABLED, khdays_text(TXT_MOD_ENABLED));
            r->value = &mod_enabled[i];
            r->high = 1;
            r->layer = i;
            r->note = v->description;
            /* a content mod's files are the ones on as the game started (runtime/mods/list.h) until it starts again */
            r->apply = v->content ? KHDAYS_APPLY_NEXT : KHDAYS_APPLY_RESUME;
            r->takes = v->content ? TXT_TAKES_NEXT_START : TXT_TAKES_RESUME;
        }
        if (i > 0) {
            r = add(b, KHDAYS_ROW_COMMAND, KHDAYS_SET_MOD_EARLIER, khdays_text(TXT_MOD_EARLIER));
            r->layer = i;
        }
        if (i < mods_view.count - 1) {
            r = add(b, KHDAYS_ROW_COMMAND, KHDAYS_SET_MOD_LATER, khdays_text(TXT_MOD_LATER));
            r->layer = i;
        }
        if (v->state == KHDAYS_MOD_PAUSED) {
            r = add(b, KHDAYS_ROW_COMMAND, KHDAYS_SET_MOD_RESUME, khdays_text(TXT_MOD_RESUME));
            r->layer = i;
        }
        if (v->state != KHDAYS_MOD_LOADED) {
            continue; /* its options are its scripts': they show while it runs */
        }
        for (int o = 0; o < v->option_count; ++o) {
            const KhdaysOptionView *ov = &v->options[o];
            const char *label = khdays_scripts_text(ov->label, language);
            static const int kinds[] = {KHDAYS_ROW_SWITCH, KHDAYS_ROW_CHOICE, KHDAYS_ROW_NUMBER, KHDAYS_ROW_COMMAND};
            r = add(b, kinds[ov->kind], KHDAYS_SET_MOD_OPTION, label != NULL ? label : ov->key);
            r->layer = i;
            r->action = o;
            r->note = khdays_scripts_text(ov->description, language);
            if (b->context->guest && v->kind == KHDAYS_MOD_GAMEPLAY) {
                r->available = 0; /* a guest's gameplay mods change nothing: the host's do */
                r->note = khdays_text(TXT_MOD_HOST_SETS);
            }
            if (ov->kind != KHDAYS_OPTION_BUTTON) {
                r->value = &mod_values[mod_first[i] + o];
            }
            switch (ov->kind) {
            case KHDAYS_OPTION_TOGGLE: r->high = 1; break;
            case KHDAYS_OPTION_CHOICE:
                r->choices = (const char *const *)ov->values;
                r->high = ov->value_count - 1;
                break;
            case KHDAYS_OPTION_NUMBER:
                r->low = (int)ov->low;
                r->high = (int)ov->high;
                r->step = (int)ov->step;
                break;
            default: break;
            }
        }
    }
}

/* A Mods row changed by the player: kept in the view (so the rows built again show it) and asked of the
 * game's thread, which applies it when the game runs again. */
static void mods_changed(const KhdaysRow *row)
{
    KhdaysModView *v;
    if ((row->id != KHDAYS_SET_MOD_ENABLED && row->id != KHDAYS_SET_MOD_OPTION) || row->layer < 0 ||
        row->layer >= mods_view.count) {
        return;
    }
    v = &mods_view.mods[row->layer];
    if (row->id == KHDAYS_SET_MOD_ENABLED) {
        v->enabled = *row->value;
        khdays_scripts_ask(KHDAYS_ASK_ENABLE, v->folder, NULL, *row->value);
    } else if (row->action >= 0 && row->action < v->option_count && v->id != NULL) {
        KhdaysOptionView *ov = &v->options[row->action];
        ov->value = ov->kind == KHDAYS_OPTION_CHOICE ? *row->value + 1 : *row->value;
        khdays_scripts_ask(KHDAYS_ASK_OPTION, v->id, ov->key, ov->value);
    }
}

/* A mod one place earlier or later, in the view at once (the game's own list follows when it runs). */
static void mods_move(int index, int by)
{
    const int other = index + by;
    KhdaysModView t;
    if (index < 0 || other < 0 || index >= mods_view.count || other >= mods_view.count) {
        return;
    }
    khdays_scripts_ask(KHDAYS_ASK_MOVE, mods_view.mods[index].folder, NULL, by);
    t = mods_view.mods[index];
    mods_view.mods[index] = mods_view.mods[other];
    mods_view.mods[other] = t;
    mods_rebuild();
}

void khdays_model_row_command(KhdaysProfile *work, const KhdaysRow *row)
{
    const KhdaysModView *v = row->layer >= 0 && row->layer < mods_view.count ? &mods_view.mods[row->layer] : NULL;
    switch (row->id) {
    case KHDAYS_SET_MOD_EARLIER: mods_move(row->layer, -1); break;
    case KHDAYS_SET_MOD_LATER: mods_move(row->layer, 1); break;
    case KHDAYS_SET_MOD_RESUME:
        if (v != NULL && v->id != NULL) {
            khdays_scripts_ask(KHDAYS_ASK_RESUME, v->id, NULL, 0);
        }
        break;
    case KHDAYS_SET_MOD_OPTION:
        if (v != NULL && v->id != NULL && row->action >= 0 && row->action < v->option_count) {
            khdays_scripts_ask(KHDAYS_ASK_BUTTON, v->id, v->options[row->action].key, 0);
        }
        break;
    default: khdays_model_command(work, row->command); break;
    }
}

int khdays_model_rows(int section, KhdaysProfile *work, const KhdaysModelContext *context, KhdaysRow *rows)
{
    return khdays_model_rows_into(section, work, context, rows, KHDAYS_MODEL_ROWS_MAX);
}

int khdays_model_rows_into(int section, KhdaysProfile *work, const KhdaysModelContext *context, KhdaysRow *rows,
                           int room)
{
    Builder b = {rows, 0, room, context};
    switch (section) {
    case KHDAYS_SECTION_CONTROLS: build_controls(&b, work); break;
    case KHDAYS_SECTION_ENHANCEMENTS: build_enhancements(&b, work); break;
    case KHDAYS_SECTION_STICKS: build_sticks(&b, work); break;
    case KHDAYS_SECTION_VIDEO: build_video(&b, work); break;
    case KHDAYS_SECTION_SOUND: build_sound(&b, work); break;
    case KHDAYS_SECTION_MULTIPLAYER: build_multiplayer(&b, work); break;
    case KHDAYS_SECTION_LANGUAGE: build_language(&b, work); break;
    case KHDAYS_SECTION_GENERAL: build_general(&b, work); break;
    case KHDAYS_SECTION_MODS: build_mods(&b, work); break;
    default: break;
    }
    return b.count;
}

/* ---- Changing the rows' values ------------------------------------------ */

int khdays_model_choice_of(const KhdaysRow *row)
{
    if (row->values == NULL) {
        return *row->value >= 0 && *row->value <= row->high ? *row->value : 0;
    }
    for (int i = 0; i <= row->high; ++i) {
        if (row->values[i] == *row->value) {
            return i;
        }
    }
    return 0;
}

void khdays_model_set_choice(KhdaysProfile *work, const KhdaysRow *row, int choice)
{
    *row->value = row->values != NULL ? row->values[choice] : choice;
    mods_changed(row);
    if (row->value == &work->video.one_screen && work->video.one_screen && work->video.aspect == KHDAYS_ASPECT_4_3) {
        work->video.aspect = KHDAYS_ASPECT_16_9; /* the default for one screen */
    }
}

int khdays_model_adjust(KhdaysProfile *work, const KhdaysRow *row, int dir)
{
    if (!row->available) {
        return row->kind == KHDAYS_ROW_SWITCH || row->kind == KHDAYS_ROW_NUMBER || row->kind == KHDAYS_ROW_CHOICE;
    }
    switch (row->kind) {
    case KHDAYS_ROW_NUMBER:
        *row->value += dir * row->step;
        *row->value = *row->value < row->low ? row->low : *row->value > row->high ? row->high : *row->value;
        mods_changed(row);
        return 1;
    case KHDAYS_ROW_CHOICE:
        khdays_model_set_choice(work, row, (khdays_model_choice_of(row) + dir + row->high + 1) % (row->high + 1));
        return 1;
    case KHDAYS_ROW_SWITCH:
        *row->value = dir > 0;
        mods_changed(row);
        return 1;
    default: return 0;
    }
}

void khdays_model_activate(KhdaysProfile *work, const KhdaysRow *row)
{
    if (!row->available) {
        return;
    }
    if (row->kind == KHDAYS_ROW_SWITCH) {
        *row->value = !*row->value;
        mods_changed(row);
    } else if (row->kind == KHDAYS_ROW_CHOICE) {
        const int c = khdays_model_choice_of(row);
        khdays_model_set_choice(work, row, c >= row->high ? 0 : c + 1);
    }
}

static void restore_device(KhdaysProfile *work, int device)
{
    KhdaysProfile defaults;
    khdays_profile_default(&defaults);
    memcpy(work->input[device], defaults.input[device], sizeof(work->input[device]));
    if (device == KHDAYS_DEVICE_GAMEPAD) {
        work->preset = defaults.preset;
    }
}

void khdays_model_command(KhdaysProfile *work, int command)
{
    switch (command) {
    case KHDAYS_CMD_RESTORE_KEYBOARD: restore_device(work, KHDAYS_DEVICE_KEYBOARD); break;
    case KHDAYS_CMD_RESTORE_GAMEPAD: restore_device(work, KHDAYS_DEVICE_GAMEPAD); break;
    case KHDAYS_CMD_RESTORE_VIDEO: khdays_video_default(&work->video); break;
    case KHDAYS_CMD_RESTORE_SOUND: khdays_sound_default(&work->sound); break;
    case KHDAYS_CMD_RESTORE_BOTH: khdays_profile_default(work); break;
    case KHDAYS_CMD_PRESET_DS:
        khdays_profile_apply_preset(work, KHDAYS_PRESET_DS, KHDAYS_AVAILABLE_ENHANCEMENTS);
        break;
    case KHDAYS_CMD_PRESET_KH2:
        khdays_profile_apply_preset(work, KHDAYS_PRESET_KH2, KHDAYS_AVAILABLE_ENHANCEMENTS);
        break;
    default: break;
    }
}

int khdays_model_unbound(const KhdaysProfile *work, char *text, int size)
{
    int n = 0;
    text[0] = '\0';
    for (int a = 0; a < KHDAYS_ACT_DS_COUNT; ++a) {
        int any = 0;
        for (int d = 0; d < KHDAYS_DEVICE_COUNT; ++d) {
            for (int s = 0; s < KHDAYS_INPUTS_PER_ACTION; ++s) {
                any |= work->input[d][KHDAYS_LAYER_BASE][a][s] != KHDAYS_NO_INPUT;
            }
        }
        if (!any) {
            if (n++) {
                SDL_strlcat(text, ", ", (size_t)size);
            }
            SDL_strlcat(text, khdays_model_action_label(a), (size_t)size);
        }
    }
    return n;
}

unsigned khdays_model_changes(const KhdaysProfile *a, const KhdaysProfile *b)
{
    unsigned changes = 0;
    if (memcmp(a->input, b->input, sizeof(a->input)) != 0 || a->preset != b->preset ||
        memcmp(a->stick, b->stick, sizeof(a->stick)) != 0 || memcmp(&a->enh, &b->enh, sizeof(a->enh)) != 0) {
        changes |= KHDAYS_CHANGE_CONTROLS;
    }
    if (memcmp(&a->video, &b->video, sizeof(a->video)) != 0) {
        changes |= KHDAYS_CHANGE_VIDEO;
    }
    if (memcmp(&a->sound, &b->sound, sizeof(a->sound)) != 0) {
        changes |= KHDAYS_CHANGE_SOUND;
    }
    if (memcmp(&a->network, &b->network, sizeof(a->network)) != 0) {
        changes |= KHDAYS_CHANGE_NETWORK;
    }
    if (a->language != b->language) {
        changes |= KHDAYS_CHANGE_LANGUAGE;
    }
    if (a->session_log != b->session_log) {
        changes |= KHDAYS_CHANGE_LOG;
    }
    return changes;
}
