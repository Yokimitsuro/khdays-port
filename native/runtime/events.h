/* Timed hardware events: VBlank and timer overflows, and delivering the
 * interrupts they raise. */
#ifndef KHDAYS_EVENTS_H
#define KHDAYS_EVENTS_H

#include "../hal/hal.h"

/* Raise the IF bits of every event up to now. */
void khdays_events_update(void);

/* A DMA channel armed to start at the next VBlank. */
void khdays_events_dma_at_vblank(int channel);

/* VBlanks so far (the runtime presents a frame for each). */
u32 khdays_events_vblank_count(void);

/* io_regs.c */
void khdays_timers_update(u64 last, u64 now);
u64 khdays_timer_next_overflow(int n, u64 after);
void khdays_dma_run(int channel);

/* hal/sdk_irq.c */
int khdays_irq_deliver(void);

/* The host side of a frame: present it, read input (runtime.c). */
void khdays_host_frame(void);

#endif
