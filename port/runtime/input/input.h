/* The buttons and the touch screen, as the game reads them (input.c). */
#ifndef KHDAYS_INPUT_H
#define KHDAYS_INPUT_H

#include "../../hal/hal.h"
#include "../../host/app/host.h"

/* DS button bits, in the order the SDK's PAD_Read combines them: KEYINPUT's
 * ten, then X and Y from the ARM7. */
enum {
    KHDAYS_KEY_A = 1 << 0,
    KHDAYS_KEY_B = 1 << 1,
    KHDAYS_KEY_SELECT = 1 << 2,
    KHDAYS_KEY_START = 1 << 3,
    KHDAYS_KEY_RIGHT = 1 << 4,
    KHDAYS_KEY_LEFT = 1 << 5,
    KHDAYS_KEY_UP = 1 << 6,
    KHDAYS_KEY_DOWN = 1 << 7,
    KHDAYS_KEY_R = 1 << 8,
    KHDAYS_KEY_L = 1 << 9,
    KHDAYS_KEY_X = 1 << 10,
    KHDAYS_KEY_Y = 1 << 11,
};

/* Reads KHDAYS_INPUT, a script of presses for runs without a window: keys, touches and sticks by
 * frame, counted from a scene's or an update's first frame (docs/BUILD.md, "The input scripts"). */
void khdays_input_init(void);

/* An object update ran (runtime/core/diag.c, from Obj_UpdateAll). */
void khdays_input_update_ran(khdays_uptr update); /* its native address */

/* At each VBlank: apply the script's items for this frame. */
void khdays_input_frame(u32 frame);

/* Pad_Sample is starting (prepare.py HOOKS): once a game frame, the frame's input is taken (the
 * script's keys and the window's latest snapshot) and handed to it. KHDAYS_TRACE_PAD logs each game
 * frame's buttons. */
void khdays_input_pad_sample(void);

/* The host snapshot taken at the last khdays_input_pad_sample (all zero
 * before the first), with the script's sticks. */
const KhdaysInputSnapshot *khdays_input_snapshot(void);

/* The snapshot taken at the khdays_input_pad_sample before: what the
 * actors read this frame in the game's own order, their buttons a game frame
 * late (in the control packet). */
const KhdaysInputSnapshot *khdays_input_previous_snapshot(void);

/* The snapshot the local player moves by (its stick for analog movement and the KH2 camera): this game
 * frame's while the port builds the field's control packet before the characters read it (this frame
 * or the last: controls_hooks.c, khdays_controls_send_early), else the one before. */
const KhdaysInputSnapshot *khdays_input_movement_snapshot(void);

/* How many times Pad_Sample has run: the game's own frames. */
u32 khdays_input_game_frames(void);

/* KEYINPUT (0x04000130): the ten buttons, active low. Pad_Sample's own read
 * gets the buttons taken for its frame; any other read gets them as they are
 * now. */
u16 khdays_input_keyinput(void);

/* What the ARM7 leaves at 0x027fffa8: X, Y and debug in bits 10, 11 and 13 (active low), the hinge in
 * bit 15 (0 open); 0x2c00 with nothing held. */
u16 khdays_input_xy(void);

/* The stylus position on the lower screen, in pixels; 0 when not touching. */
int khdays_input_touch(int *x, int *y);

#endif
