/* The two screens at each VBlank (display.c). */
#ifndef KHDAYS_DISPLAY_H
#define KHDAYS_DISPLAY_H

#include "../../hal/hal.h"

/* Reads KHDAYS_SHOTS ("FRAME FRAME ...": save both screens of those frames
 * as KHDAYS_SHOT_DIR/frame_NNNNN.bmp, default directory "shots"). */
void khdays_display_init(void);

/* F12 (the window's thread): the frame two VBlanks on saved to <saves>/marks/ with engine A's
 * layer registers and the pause's pieces in the session log. */
void khdays_display_mark(void);

/* VBlank begins: the frame just drawn, from the state the game left for it. */
void khdays_display_vblank(u32 frame);

/* Moves by one with each new 3D picture shown (the game's SwapBuffers): how many the player saw in a
 * time. */
u32 khdays_display_3d_serial(void);
/* The game's thread: the VBlanks in a row engine A has changed screens (2 and up: the 3D on both). */
u32 khdays_display_engine_swaps(void);

#endif
