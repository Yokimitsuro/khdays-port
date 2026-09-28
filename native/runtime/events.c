#include "events.h"
#include "clock.h"
#include "io.h"
#include "arm7.h"
#include "display.h"
#include "input.h"
#include "../gpu/gpu3d.h"

#include <windows.h>

static u64 last_update;
static u64 next_vblank = KHDAYS_VBLANK_LINE * KHDAYS_CYCLES_PER_LINE;
static u32 vblank_count;
static u32 presented;
static u32 dma_at_vblank;  /* channel mask */

/* For the interrupt watcher (async_irq.c), on another thread: the cycle of
 * the next timed event, and whether the game is waiting here (this code then
 * delivers interrupts itself). */
volatile LONG64 khdays_next_event;
volatile LONG khdays_game_waiting;

static u64 next_event(void)
{
    u64 next = next_vblank;
    for (int n = 0; n < 4; ++n) {
        const u64 overflow = khdays_timer_next_overflow(n, last_update);
        if (overflow < next) {
            next = overflow;
        }
    }
    return next;
}

void khdays_events_dma_at_vblank(int channel)
{
    dma_at_vblank |= 1u << channel;
}

u32 khdays_events_vblank_count(void)
{
    return vblank_count;
}

void khdays_events_update(void)
{
    const u64 now = khdays_clock_cycles();
    khdays_timers_update(last_update, now);
    if (khdays_gpu3d_irq()) {
        khdays_io_request_irq(1u << 21);  /* GXFIFO: set as long as the condition holds */
    }
    while (now >= next_vblank) {
        /* VBlank begins: the frame the game prepared is done (only the last
         * one when several passed at once), then the input for the next. */
        if (now < next_vblank + KHDAYS_CYCLES_PER_FRAME) {
            khdays_display_vblank(vblank_count);
        }
        khdays_input_frame(vblank_count + 1);
        if (IO16(0x004) & 0x0008) {  /* DISPSTAT: VBlank IRQ enabled */
            khdays_io_request_irq(1u << 0);
        }
        for (int channel = 0; channel < 4; ++channel) {
            if (dma_at_vblank & (1u << channel)) {
                khdays_dma_run(channel);
                /* a repeating channel stays armed for the next VBlank */
                if (!(IO32(0x0b8 + 12 * channel) & 0x80000000u)) {
                    dma_at_vblank &= ~(1u << channel);
                }
            }
        }
        khdays_arm7_frame();
        ++vblank_count;
        next_vblank += KHDAYS_CYCLES_PER_FRAME;
    }
    last_update = now;
    InterlockedExchange64(&khdays_next_event, (LONG64)next_event());
}

static int irq_line(void)
{
    return IO32(0x208) != 0 && (IO32(0x210) & IO32(0x214)) != 0;
}

/* An unmask point (interrupts enabled again): take what is pending, as the
 * CPU would the moment the mask lifts. */
void khdays_irq_poll(void)
{
    khdays_events_update();
    while (khdays_irq_deliver()) {
    }
}

/* The CPU halted until an interrupt (OS_Halt, SWI Halt, the RTC busy-wait):
 * run time forward to the next event, present finished frames, and return
 * once the interrupt line is up -- taking the IRQ if the CPSR allows it. */
void khdays_runtime_wait(void)
{
    InterlockedIncrement(&khdays_game_waiting);
    for (;;) {
        khdays_events_update();
        if (presented != vblank_count) {
            presented = vblank_count;
            khdays_host_frame();
        }
        if (irq_line()) {
            InterlockedDecrement(&khdays_game_waiting);
            khdays_irq_deliver();
            return;
        }
        khdays_clock_sleep_until(next_event());
    }
}
