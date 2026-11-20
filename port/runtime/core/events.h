/* Timed hardware events: VBlank and timer overflows, and delivering the
 * interrupts they raise. */
#ifndef KHDAYS_EVENTS_H
#define KHDAYS_EVENTS_H

#include "../../hal/hal.h"

/* Raise the IF bits of every event up to now. */
void khdays_events_update(void);

/* A DMA channel armed to start at the next VBlank. */
void khdays_events_dma_at_vblank(int channel);

/* The game's reset (start.c): no alarm armed, no DMA waiting for VBlank;
 * the VBlanks go on counting. */
void khdays_events_reset(void);

/* VBlanks so far (the runtime presents a frame for each). */
u32 khdays_events_vblank_count(void);

/* The time of the VBlank being handled now (khdays_display_vblank's), in the
 * game's cycles. */
u64 khdays_events_vblank_time(void);

/* KHDAYS_TRACE_TIMING: what the game's time does, counted from the start and printed every 600
 * VBlanks (VBlanks, logic frames, alarm interrupts, the tick's count-ups, the movie's flips), and the
 * game's accesses to the timers, DISPSTAT and VCOUNT, the first from each instruction named. The
 * hooks are prepare.py's. */
enum {
    KHDAYS_TIMING_TIMER_READ,
    KHDAYS_TIMING_TIMER_WRITE,
    KHDAYS_TIMING_DISPSTAT_READ,
    KHDAYS_TIMING_VCOUNT_READ,
    KHDAYS_TIMING_KINDS
};
int khdays_timing_on(void);
void khdays_timing_alarm(void);
void khdays_timing_countup(void);
void khdays_timing_flip(int outside);
void khdays_timing_access(int kind, khdays_uptr site);

/* io_regs.c */
void khdays_dma_run(int channel);

/* hal/sdk_irq.c */
int khdays_irq_deliver(void);

/* Whether the VBlank interrupt's handler is running (hal/sdk_irq.c). */
int khdays_events_in_vblank(void);

/* DISPSTAT's VBlank bit: set for the DS's VBlank length after the frame loop's VBlank, and while the
 * VBlank handler runs. */
int khdays_events_in_vblank_period(void);

/* A waiting loop's wait point: the game spins on something only an interrupt advances, so this halts
 * until an interrupt is due and takes it, as the CPU would between two of the loop's instructions. */
void khdays_wait_point(void);

/* A halt until an interrupt is taken or `stop` says to end it, asked each time the wait is woken
 * (khdays_clock_wake) or an event passes: the press pace's (runtime/input/input.c). */
void khdays_runtime_wait_or(int (*stop)(void));

/* The alarm timer (hal/sdk_time.c): timer 1 overflowing `ticks` ticks (64 cycles each) from now,
 * raising IE bit 4 once; or stopped. */
void khdays_alarm_set_after(u64 ticks);
void khdays_alarm_clear(void);

/* The host side of a frame: present it, read input (runtime.c). */
void khdays_host_frame(void);

#endif
