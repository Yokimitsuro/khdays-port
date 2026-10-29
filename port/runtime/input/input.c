/* The buttons, stylus and sticks as the game reads them (input.h): the script of presses for runs
 * without a window, and each game frame's latch that Pad_Sample reads, with the port's menus and
 * enhancements taking their part. */
#include "../core/language.h"
#include "input.h"
#include "../../host/app/press_edges.h"
#include "../menus/autosave.h"
#include "../menus/camp.h"
#include "../menus/charselect.h"
#include "../menus/cinelist.h"
#include "../menus/daycard.h"
#include "../net/roomlist.h"
#include "../net/net_session.h"
#include "controls_hooks.h"
#include "../movies/movie_hd.h"
#include "../menus/newgame.h"
#include "../menus/mission_start.h"
#include "../menus/retry.h"
#include "../menus/savelist.h"
#include "../plugins/plugins.h"
#include "../scripts/scripts.h"
#include "../core/runtime.h"
#include "sequencers.h"
#include "../core/window.h"
#include "../../host/app/host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIRECTIONS (KHDAYS_KEY_UP | KHDAYS_KEY_DOWN | KHDAYS_KEY_LEFT | KHDAYS_KEY_RIGHT)

/* What a step's frame counts from: boot, or the first frame of a scene (sN)
 * or of an object update (uName). */
typedef struct Anchor {
    char type;                /* 's' or 'u' */
    unsigned long long value; /* the scene id, or the update's native address */
    u32 start;                /* the frame it began + 1; 0 = not yet */
} Anchor;

typedef struct Step {
    u32 frame;
    int anchor; /* -1: from boot */
    char kind;  /* '+', '-', '@', '~' or '^' */
    u16 keys;
    u32 actions; /* '+' and '-' of the port's own controls (KHDAYS_ACTION_*) */
    int x, y;    /* '@': -1 lifts the stylus; '~' and '^': a stick's position */
} Step;

static Step *script;
static int script_len;
static int script_next;
static u16 script_keys;
static KhdaysPressEdges script_edges; /* the script's presses between two of the game's reads */
static u32 script_actions;            /* the port's own controls the script holds */
static int script_touch_x = -1, script_touch_y;
static int script_move[2], script_camera[2]; /* the sticks' positions, y up */

static Anchor *anchors; /* the script's anchors, in its order (grows while it is read) */
static int anchor_count, anchor_room;
static u32 last_frame;

/* The scene controller at 0x0204bda8, its current id at +8
 * (src/calls/func_0202099c.c). */
#define SCENE_CURRENT (*(volatile u32 *)KHDAYS_W(0x0204bda8 + 8))
/* The game's own frame count: Obj_UpdateAll adds one per unpaused pass
 * (gObjSystem[2]). */
#define GAME_FRAMES (*(volatile u32 *)KHDAYS_W(0x0204c058 + 8))

/* The game's pad state, as Pad_Sample leaves it (src/engine/Pad_Sample.c):
 * the buttons held at gPadHeld, those newly pressed at gPadPressed. */
#define PAD_HELD (*(volatile u16 *)KHDAYS_W(0x0204c18c))
#define PAD_PRESSED (*(volatile u16 *)KHDAYS_W(0x0204c190))
/* The frame-rate mode (GetFrameRateMode: gObjSystem byte 0): 0 = 30 fps,
 * 1 = 20 fps, 2 = 60 fps. */
#define FRAME_RATE_MODE (*(volatile u8 *)KHDAYS_W(0x0204c058))
/* Where the ARM7 leaves X, Y and the hinge for the ARM9 (khdays_input_xy). */
#define ARM7_XY (*(volatile u16 *)KHDAYS_W(0x027fffa8))

/* The frame latch: what Pad_Sample reads this game frame. */
static KhdaysInputSnapshot snapshot; /* the host's input, taken at the latch */
static KhdaysInputSnapshot previous; /* the one taken at the latch before */
static KhdaysMenuSequencer menu;     /* the command menu on the D-pad */
static KhdaysLockSequencer lock;     /* lock-on on its own buttons */
static u16 latched;                  /* the DS buttons for this game frame */
static int latched_unread;           /* Pad_Sample has not read KEYINPUT yet */
static u32 pad_samples;              /* game frames so far (Pad_Sample calls) */
static int trace_pad;                /* KHDAYS_TRACE_PAD */

static int trace; /* KHDAYS_TRACE_INPUT: each step as it is applied */
/* KEYINPUT reads so far: the game's pad routine reads it once a frame, and
 * X and Y (which the ARM7 passes on) along with it. */
static u32 reads;
static u32 pressed_at[16]; /* the reads when the script pressed each key */

/* Whether the game has read these keys down at least twice. */
static int seen_down(u16 keys)
{
    for (int b = 0; b < 16; ++b) {
        if ((keys & (1u << b)) && reads - pressed_at[b] < 2) {
            return 0;
        }
    }
    return 1;
}

static const struct {
    const char *name;
    u16 bit;
} key_names[] = {
    {"A", KHDAYS_KEY_A},         {"B", KHDAYS_KEY_B},       {"SELECT", KHDAYS_KEY_SELECT}, {"START", KHDAYS_KEY_START},
    {"RIGHT", KHDAYS_KEY_RIGHT}, {"LEFT", KHDAYS_KEY_LEFT}, {"UP", KHDAYS_KEY_UP},         {"DOWN", KHDAYS_KEY_DOWN},
    {"R", KHDAYS_KEY_R},         {"L", KHDAYS_KEY_L},       {"X", KHDAYS_KEY_X},           {"Y", KHDAYS_KEY_Y},
};

/* The port's own controls a script can hold, for the enhancements' runs. */
static const struct {
    const char *name;
    u32 bit;
} action_names[] = {
    {"LOCK_ON", KHDAYS_ACTION_LOCK_ON},         {"TARGET_NEXT", KHDAYS_ACTION_TARGET_NEXT},
    {"TARGET_PREV", KHDAYS_ACTION_TARGET_PREV}, {"MENU_UP", KHDAYS_ACTION_MENU_UP},
    {"MENU_DOWN", KHDAYS_ACTION_MENU_DOWN},     {"MENU_LEFT", KHDAYS_ACTION_MENU_LEFT},
    {"MENU_RIGHT", KHDAYS_ACTION_MENU_RIGHT},
};

__declspec(noreturn) static void bad_script(const char *item)
{
    fprintf(stderr, "KHDAYS_INPUT: cannot read \"%s\"\n", item);
    exit(2);
}

static int add_anchor(const char *item)
{
    Anchor *a;
    char *end;
    if (anchor_count == anchor_room) {
        const int room = anchor_room ? anchor_room * 2 : 16;
        Anchor *grown = (Anchor *)realloc(anchors, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            bad_script(item);
        }
        anchors = grown;
        anchor_room = room;
    }
    a = &anchors[anchor_count];
    a->type = item[0];
    a->start = 0;
    if (a->type == 's') {
        a->value = (u32)strtoul(item + 1, &end, 10);
        if (*end != '\0') {
            bad_script(item);
        }
    } else {
        a->value = khdays_diag_symbol(item + 1);
        if (a->value == 0) {
            fprintf(stderr, "KHDAYS_INPUT: no function named %s\n", item + 1);
            exit(2);
        }
    }
    return anchor_count++;
}

void khdays_input_init(void)
{
    const char *text = getenv("KHDAYS_INPUT");
    char *copy, *item, *rest = NULL;
    int anchor = -1;
    khdays_menu_sequencer_reset(&menu);
    khdays_lock_sequencer_reset(&lock);
    trace = getenv("KHDAYS_TRACE_INPUT") != NULL;
    trace_pad = getenv("KHDAYS_TRACE_PAD") != NULL;
    if (text == NULL) {
        return;
    }
    copy = _strdup(text);
    script = (Step *)calloc(strlen(text) / 2 + 1, sizeof(Step));
    for (item = strtok_s(copy, " ", &rest); item != NULL; item = strtok_s(NULL, " ", &rest)) {
        Step step;
        char *end;
        if (item[0] == 's' || item[0] == 'u') {
            anchor = add_anchor(item);
            continue;
        }
        memset(&step, 0, sizeof(step));
        step.anchor = anchor;
        step.frame = strtoul(item, &end, 10);
        step.kind = *end;
        if (step.kind == '@') {
            if (strcmp(end + 1, "-") == 0) {
                step.x = -1;
            } else if (sscanf_s(end + 1, "%d,%d", &step.x, &step.y) != 2) {
                bad_script(item);
            }
        } else if (step.kind == '~' || step.kind == '^') {
            if (sscanf_s(end + 1, "%d,%d", &step.x, &step.y) != 2) {
                bad_script(item);
            }
        } else if (step.kind == '+' || step.kind == '-') {
            for (size_t n = 0; n < sizeof(key_names) / sizeof(key_names[0]); ++n) {
                if (_stricmp(end + 1, key_names[n].name) == 0) {
                    step.keys = key_names[n].bit;
                }
            }
            for (size_t n = 0; n < sizeof(action_names) / sizeof(action_names[0]); ++n) {
                if (_stricmp(end + 1, action_names[n].name) == 0) {
                    step.actions = action_names[n].bit;
                }
            }
            if (step.keys == 0 && step.actions == 0) {
                bad_script(item);
            }
        } else {
            bad_script(item);
        }
        script[script_len++] = step;
    }
    free(copy);
}

/* The anchors begin in the order the script gives them: the first that has
 * not begun is the only one that can (a second `s2` is the scene's second
 * start, not the first again). */
static Anchor *next_anchor(void)
{
    for (int i = 0; i < anchor_count; ++i) {
        if (anchors[i].start == 0) {
            return &anchors[i];
        }
    }
    return NULL;
}

void khdays_input_update_ran(khdays_uptr update)
{
    Anchor *a = next_anchor();
    if (a != NULL && a->type == 'u' && a->value == update) {
        a->start = last_frame + 1;
    }
}

void khdays_input_frame(u32 frame)
{
    static u32 previous_scene = 0xffffffffu;
    const u32 scene = SCENE_CURRENT;
    Anchor *a = next_anchor();
    last_frame = frame;
    /* a scene's start: the frame its id becomes current */
    if (a != NULL && a->type == 's' && a->value == scene && previous_scene != scene) {
        a->start = frame + 1;
    }
    previous_scene = scene;
    while (script_next < script_len) {
        const Step *s = &script[script_next];
        if (s->anchor < 0 ? s->frame > frame
                          : anchors[s->anchor].start == 0 || anchors[s->anchor].start - 1 + s->frame > frame) {
            break;
        }
        /* A key is let go only once the game has read it down twice: the
         * script counts VBlanks, which follow the host's clock, and a game
         * slowed by a busy machine can read the pad less often than the
         * press lasts (the steps after wait with it). */
        if (s->kind == '-' && !seen_down(s->keys)) {
            break;
        }
        ++script_next;
        if (trace) {
            fprintf(stderr, "input: frame %u, game frame %u, %u pad reads: %c%04x\n", frame, GAME_FRAMES, reads,
                    s->kind, s->keys);
        }
        if (s->kind == '+') {
            khdays_press_edges_note(&script_edges, script_keys, (u16)(script_keys | s->keys));
            script_keys |= s->keys;
            script_actions |= s->actions;
            for (int b = 0; b < 16; ++b) {
                if (s->keys & (1u << b)) {
                    pressed_at[b] = reads;
                }
            }
        } else if (s->kind == '-') {
            khdays_press_edges_note(&script_edges, script_keys, (u16)(script_keys & ~s->keys));
            script_keys &= (u16)~s->keys;
            script_actions &= ~s->actions;
        } else if (s->kind == '~') {
            script_move[0] = s->x;
            script_move[1] = s->y;
        } else if (s->kind == '^') {
            script_camera[0] = s->x;
            script_camera[1] = s->y;
        } else {
            script_touch_x = s->x;
            script_touch_y = s->y;
        }
    }
}

/* The buttons as they are now, withheld while an HD video plays in a movie's place (the port's START
 * aside, runtime/movies/movie_hd.h). */
static u16 pressed(void)
{
    return khdays_movie_hd_game_buttons((u16)(script_keys | khdays_host_buttons()));
}

/* The player's DS buttons for this game frame. With the command menu on the D-pad, while the player
 * has field control the gamepad's D-pad becomes the presses that move the menu (sequencers.h) and
 * does not move Roxas. */
static u16 player_buttons(void)
{
    const KhdaysGameControls *game = khdays_controls_state();
    const int on = (snapshot.enhancements & KHDAYS_ENH_DPAD_COMMAND_MENU) != 0;
    const int field = on && pad_samples > 0 && game->command_input_frame == pad_samples;
    const u16 dpad = KHDAYS_KEY_UP | KHDAYS_KEY_DOWN | KHDAYS_KEY_LEFT | KHDAYS_KEY_RIGHT;
    u16 buttons = field ? snapshot.ds_buttons_field : snapshot.ds_buttons;
    const uint32_t directions = ((snapshot.actions & KHDAYS_ACTION_MENU_UP) ? 1u << KHDAYS_MENU_UP : 0) |
                                ((snapshot.actions & KHDAYS_ACTION_MENU_DOWN) ? 1u << KHDAYS_MENU_DOWN : 0) |
                                ((snapshot.actions & KHDAYS_ACTION_MENU_LEFT) ? 1u << KHDAYS_MENU_LEFT : 0) |
                                ((snapshot.actions & KHDAYS_ACTION_MENU_RIGHT) ? 1u << KHDAYS_MENU_RIGHT : 0);
    const KhdaysMenuFrame step = khdays_menu_sequencer_frame(&menu, on ? directions : 0, field, game->command_scheme,
                                                             game->lockon_scheme, (u16)(buttons | script_keys));
    /* Lock-on on its own buttons: while the game's lock-on logic takes the buttons (it did last frame),
     * their presses become the DS's R presses for them; anywhere else they are the DS's R and L. */
    const int lock_on = (snapshot.enhancements & KHDAYS_ENH_SEPARATE_LOCK_ON) != 0;
    /* (with the 3D on both screens it runs on every other frame: the frame before counts too) */
    const int lock_field = lock_on && pad_samples > 0 && pad_samples - game->selection_frame <= 1u &&
                           pad_samples - game->selection_live_frame <= 1u;
    const uint32_t lock_actions = ((snapshot.actions & KHDAYS_ACTION_LOCK_ON) ? 1u << KHDAYS_LOCK_ON : 0) |
                                  ((snapshot.actions & KHDAYS_ACTION_TARGET_NEXT) ? 1u << KHDAYS_LOCK_NEXT : 0) |
                                  ((snapshot.actions & KHDAYS_ACTION_TARGET_PREV) ? 1u << KHDAYS_LOCK_PREV : 0);
    /* An R press counts as a single one for the game once its counter has
     * passed 0x9000 (9 frames) since the last R (Ov022_ReadSelectionInput). */
    const int r_free = game->r_repeat >= 0x9000 && !game->r_pressed;
    const u16 lock_buttons =
        khdays_lock_sequencer_frame(&lock, lock_on ? lock_actions : 0, lock_field, game->lock_active,
                                    game->lockon_scheme, (u16)(buttons | script_keys), r_free);
    if (step.dpad_own) {
        buttons &= (u16)~dpad;
    }
    return (u16)(buttons | step.buttons | lock_buttons);
}

/* The ARM7's word for a set of buttons: X and Y released unless held (active
 * low), the debug button released, the hinge open. */
static u16 xy_word(u16 buttons)
{
    return (u16)(0x2c00 & ~(buttons & (KHDAYS_KEY_X | KHDAYS_KEY_Y)));
}

/* The movement stick, for a guest to send with its controls: the host moves the guest's actor with it,
 * as the DS's control packet holds only the D-pad. */
static void share_stick(const KhdaysInputSnapshot *s)
{
    KhdaysNetStick stick;
    stick.x = s->move_x;
    stick.y = s->move_y;
    stick.run_threshold = s->run_threshold;
    stick.walk_speed = s->walk_speed;
    stick.on = (s->enhancements & KHDAYS_ENH_ANALOG_MOVEMENT) != 0;
    khdays_net_set_stick(&stick);
}

void khdays_input_pad_sample(void)
{
    khdays_autosave_frame(); /* an owed autosave, before the save list maps the game's slots */
    khdays_language_frame(); /* a language the settings menu chose */
    if (trace_pad && pad_samples > 0) {
        /* what the game made of the previous frame's buttons */
        fprintf(stderr, "pad: game frame %u, mode %u, given %04x, held %04x, pressed %04x\n", pad_samples,
                FRAME_RATE_MODE, latched, PAD_HELD, PAD_PRESSED);
    }
    previous = snapshot;
    khdays_host_input_take(&snapshot); /* a press let go since the last game frame counts too */
    /* The script's sticks and controls, for the enhancements' runs without
     * a gamepad. */
    snapshot.actions |= script_actions;
    if (script_move[0] != 0 || script_move[1] != 0) {
        snapshot.move_x = (int16_t)script_move[0];
        snapshot.move_y = (int16_t)script_move[1];
    }
    if (script_camera[0] != 0 || script_camera[1] != 0) {
        snapshot.camera_x = (int16_t)script_camera[0];
        snapshot.camera_y = (int16_t)script_camera[1];
    }
    if ((khdays_plugins_hidden_buttons() & DIRECTIONS) == DIRECTIONS) {
        snapshot.move_x = snapshot.move_y = 0; /* a native plugin keeps the directions from the game: the stick too */
    }
    share_stick(&snapshot);
    /* While the settings menu is open the player's input is not the
     * game's (the script's still is: headless runs have no screen). */
    latched = khdays_movie_hd_game_buttons(
        (u16)(khdays_press_edges_take(&script_edges, script_keys) | (snapshot.ui_open ? 0 : player_buttons())));
    latched = khdays_newgame_buttons(latched);       /* the port's question in the new game takes them */
    latched = khdays_savelist_buttons(latched);      /* the port's save list takes the files' rows */
    khdays_cinelist_frame();                         /* MODO CINE's list, for the port to draw */
    khdays_daycard_frame();                          /* the day's card, for the port to draw */
    latched = khdays_cinelist_buttons(latched);      /* the port's list of chapters takes all but B and START */
    latched = khdays_roomlist_buttons(latched);      /* the port's list of rooms takes the group list's */
    latched = khdays_charselect_buttons(latched);    /* the port's character select takes the directions */
    latched = khdays_camp_buttons(latched);          /* the port's Preferences page joins the game's tabs */
    latched = khdays_retry_buttons(latched);         /* the pause's Retry line in Mission Mode takes the directions */
    latched = khdays_scripts_buttons(latched);       /* a mod's screen, the title's mods' prompt */
    latched = khdays_mission_start_buttons(latched); /* a mod's mission.start presses the camp's own way */
    latched = (u16)khdays_plugins_buttons(latched);  /* the native plugins read them, and may hide some */
    latched_unread = 1;
    ++pad_samples;
    /* Pad_Sample reads X and Y from here right after: the same moment as the
     * other buttons. */
    ARM7_XY = xy_word(latched);
}

const KhdaysInputSnapshot *khdays_input_snapshot(void)
{
    return &snapshot;
}

const KhdaysInputSnapshot *khdays_input_previous_snapshot(void)
{
    return &previous;
}

u32 khdays_input_game_frames(void)
{
    return pad_samples;
}

u16 khdays_input_keyinput(void)
{
    ++reads;
    if (latched_unread) {
        latched_unread = 0; /* Pad_Sample's own read (it reads KEYINPUT once) */
        return (u16)(~latched & 0x03ff);
    }
    return (u16)(~pressed() & 0x03ff);
}

u16 khdays_input_xy(void)
{
    return xy_word(pressed());
}

int khdays_input_touch(int *x, int *y)
{
    if (khdays_movie_hd_running()) {
        return 0; /* the player's input is not the game's while an HD video plays (or is about to) */
    }
    if (script_touch_x >= 0) {
        *x = script_touch_x;
        *y = script_touch_y;
        return 1;
    }
    return khdays_host_touch(x, y);
}
