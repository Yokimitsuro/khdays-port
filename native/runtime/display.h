/* The two screens at each VBlank (display.c). */
#ifndef KHDAYS_DISPLAY_H
#define KHDAYS_DISPLAY_H

#include "../hal/hal.h"

/* Reads KHDAYS_SHOTS ("FRAME FRAME ...": save both screens of those frames
 * as KHDAYS_SHOT_DIR/frame_NNNNN.bmp, default directory "shots"). */
void khdays_display_init(void);

/* VBlank begins: the frame just drawn, from the state the game left for it. */
void khdays_display_vblank(u32 frame);

#endif
