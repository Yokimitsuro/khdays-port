/* The buttons and the touch screen, as the game reads them (input.c). */
#ifndef KHDAYS_INPUT_H
#define KHDAYS_INPUT_H

#include "../hal/hal.h"

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

/* Reads KHDAYS_INPUT, a script of presses for runs without a window: items
 * `FRAME+KEY` (press) and `FRAME-KEY` (release) separated by spaces, KEY one
 * of A B X Y L R START SELECT UP DOWN LEFT RIGHT, or `FRAME@X,Y` / `FRAME@-`
 * to touch the lower screen / lift the stylus. After an item `sN` the frames
 * count from the first frame of scene N, after `uName` from the first frame
 * the object update Name ran, and wait for it: loading times vary from run
 * to run, where the game has got to does not. */
void khdays_input_init(void);

/* An object update ran (runtime/diag.c, from Obj_UpdateAll). */
void khdays_input_update_ran(u32 update);

/* At each VBlank: apply the script's items for this frame. */
void khdays_input_frame(u32 frame);

/* KEYINPUT (0x04000130): the ten buttons, active low. */
u16 khdays_input_keyinput(void);

/* What the ARM7 leaves at 0x027fffa8: X, Y and debug in bits 10, 11 and 13
 * (active low), the hinge in bit 15 (0 = open) -- 0x2c00 with nothing held,
 * as read from DeSmuME savestates of this game. */
u16 khdays_input_xy(void);

/* The stylus position on the lower screen, in pixels; 0 when not touching. */
int khdays_input_touch(int *x, int *y);

#endif
