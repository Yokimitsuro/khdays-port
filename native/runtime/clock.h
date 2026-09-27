/* The DS's time, derived from the host clock. */
#ifndef KHDAYS_CLOCK_H
#define KHDAYS_CLOCK_H

#include "../hal/hal.h"

/* The ARM9 bus clock the timers count (33.513982 MHz), and the display
 * timing: 263 lines of 355 dots x 6 cycles, VBlank from line 192 to 261. */
#define KHDAYS_CLOCK_HZ        33513982u
#define KHDAYS_CYCLES_PER_LINE 2130u
#define KHDAYS_LINES_PER_FRAME 263u
#define KHDAYS_CYCLES_PER_FRAME (KHDAYS_CYCLES_PER_LINE * KHDAYS_LINES_PER_FRAME)
#define KHDAYS_VBLANK_LINE     192u

void khdays_clock_init(void);

/* Cycles since the game started. */
u64 khdays_clock_cycles(void);

/* The display line being drawn now (VCOUNT). */
u32 khdays_clock_line(void);

/* Blocks until the given cycle. */
void khdays_clock_sleep_until(u64 cycle);

#endif
