#include "clock.h"

#include <windows.h>

static LARGE_INTEGER start;
static LARGE_INTEGER frequency;

void khdays_clock_init(void)
{
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    timeBeginPeriod(1);
}

u64 khdays_clock_cycles(void)
{
    LARGE_INTEGER now;
    u64 ticks;
    QueryPerformanceCounter(&now);
    ticks = (u64)(now.QuadPart - start.QuadPart);
    /* ticks * HZ / frequency, without overflowing 64 bits */
    return ticks / (u64)frequency.QuadPart * KHDAYS_CLOCK_HZ +
           ticks % (u64)frequency.QuadPart * KHDAYS_CLOCK_HZ / (u64)frequency.QuadPart;
}

u32 khdays_clock_line(void)
{
    return (u32)(khdays_clock_cycles() % KHDAYS_CYCLES_PER_FRAME / KHDAYS_CYCLES_PER_LINE);
}

void khdays_wait_for_line(u32 line)
{
    const u64 now = khdays_clock_cycles();
    const u64 frame_start = now - now % KHDAYS_CYCLES_PER_FRAME;
    u64 target = frame_start + (u64)line * KHDAYS_CYCLES_PER_LINE;
    if (now >= target + KHDAYS_CYCLES_PER_LINE) {
        target += KHDAYS_CYCLES_PER_FRAME;  /* this frame's is past: the next one */
    }
    khdays_clock_sleep_until(target);
}

void khdays_clock_sleep_until(u64 cycle)
{
    for (;;) {
        const u64 now = khdays_clock_cycles();
        u64 ms;
        if (now >= cycle) {
            return;
        }
        ms = (cycle - now) * 1000 / KHDAYS_CLOCK_HZ;
        /* Sleep coarsely, then spin the last millisecond. */
        if (ms > 1) {
            Sleep((DWORD)(ms - 1));
        } else {
            YieldProcessor();
        }
    }
}
