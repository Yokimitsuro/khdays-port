/* The DS's time, derived from the host clock. */
#ifndef KHDAYS_CLOCK_H
#define KHDAYS_CLOCK_H

#include "../../hal/hal.h"

/* The ARM9 bus clock the timers count (33.513982 MHz), and the display
 * timing: 263 lines of 355 dots x 6 cycles, VBlank from line 192 to 261. */
#define KHDAYS_CLOCK_HZ 33513982u
#define KHDAYS_CYCLES_PER_LINE 2130u
#define KHDAYS_LINES_PER_FRAME 263u
#define KHDAYS_CYCLES_PER_FRAME (KHDAYS_CYCLES_PER_LINE * KHDAYS_LINES_PER_FRAME)
#define KHDAYS_VBLANK_LINE 192u
/* How long the DS's VBlank lasts (lines 192-261): DISPSTAT's VBlank bit stays set that long. */
#define KHDAYS_VBLANK_CYCLES (70u * KHDAYS_CYCLES_PER_LINE)

void khdays_clock_init(void);

/* Cycles since the game started (not counting the time it was paused). */
u64 khdays_clock_cycles(void);

/* The game is held by stopping its time: nothing comes due, and let go, time goes on from the same
 * cycle with nothing to catch up. The time stands still while any reason holds it (the settings
 * screen, the game waiting at a movie's end for its HD video). Safe from any thread. */
enum {
    KHDAYS_HOLD_SETTINGS = 1u << 0,
    KHDAYS_HOLD_MOVIE = 1u << 1,
    KHDAYS_HOLD_MOVIE_MENU = 1u << 2,
};
void khdays_clock_hold(unsigned reason, int on);

/* Whether a reason holds the time now. */
int khdays_clock_held(unsigned reason);

/* Blocks until the given cycle. */
void khdays_clock_sleep_until(u64 cycle);

/* The same, ending early when another thread calls khdays_clock_wake (the
 * sound bridge, with a reply for the game's thread). */
void khdays_clock_sleep_until_or_woken(u64 cycle);
void khdays_clock_wake(void);

#endif
