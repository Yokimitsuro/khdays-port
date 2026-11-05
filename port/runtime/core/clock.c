/* The DS's time from the PC's performance counter (clock.h), less the time spent held, at the rate
 * the screen's lock sets. */
#include "clock.h"

#include <windows.h>
#include <dwmapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static LARGE_INTEGER start;
static LARGE_INTEGER frequency;

/* The hold: the time is the counter less all the time spent held, so after a hold it goes on from
 * where it stopped. */
static SRWLOCK pause_lock = SRWLOCK_INIT;
static unsigned reasons;      /* what holds the time now (KHDAYS_HOLD_*) */
static int paused;            /* reasons != 0 */
static LONGLONG paused_at;    /* the counter when the current hold began */
static LONGLONG paused_ticks; /* all the counter ticks spent held before */
/* The rate (under pause_lock): the DS's time runs `rate` times the counter's from `rate_ticks` (the
 * counter less the holds, from the start), where it was `rate_cycles`; 1 from the start. */
static double rate = 1.0;
static LONGLONG rate_ticks;
static u64 rate_cycles;

static LONGLONG counter_now(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

void khdays_clock_init(void)
{
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    timeBeginPeriod(1);
}

/* The cycles at counter ticks `ticks` (less the holds, from the start), under pause_lock. */
static u64 cycles_at(LONGLONG ticks)
{
    if (rate == 1.0 && rate_ticks == 0) {
        /* ticks * HZ / frequency, without overflowing 64 bits */
        return (u64)ticks / (u64)frequency.QuadPart * KHDAYS_CLOCK_HZ +
               (u64)ticks % (u64)frequency.QuadPart * KHDAYS_CLOCK_HZ / (u64)frequency.QuadPart;
    }
    /* since the rate was last set: a VBlank ago while the lock holds it */
    return rate_cycles +
           (u64)((double)(ticks - rate_ticks) / (double)frequency.QuadPart * (double)KHDAYS_CLOCK_HZ * rate);
}

u64 khdays_clock_cycles(void)
{
    LONGLONG now;
    u64 cycles;
    AcquireSRWLockShared(&pause_lock);
    now = (paused ? paused_at : counter_now()) - paused_ticks;
    cycles = cycles_at(now - start.QuadPart);
    ReleaseSRWLockShared(&pause_lock);
    return cycles;
}

double khdays_clock_rate(void)
{
    double r;
    AcquireSRWLockShared(&pause_lock);
    r = rate;
    ReleaseSRWLockShared(&pause_lock);
    return r;
}

/* Sets the rate from now on, the time going on from where it is. */
static void set_rate(double r)
{
    AcquireSRWLockExclusive(&pause_lock);
    if (r != rate) {
        const LONGLONG ticks = (paused ? paused_at : counter_now()) - paused_ticks - start.QuadPart;
        rate_cycles = cycles_at(ticks);
        rate_ticks = ticks;
        rate = r;
    }
    ReleaseSRWLockExclusive(&pause_lock);
}

/* The counter's value when the DS's time reaches `cycle` at the rate now (the inverse of cycles_at). */
static LONGLONG counter_at(u64 cycle)
{
    LONGLONG ticks;
    AcquireSRWLockShared(&pause_lock);
    if (rate == 1.0 && rate_ticks == 0) {
        ticks = (LONGLONG)(cycle / KHDAYS_CLOCK_HZ * (u64)frequency.QuadPart +
                           cycle % KHDAYS_CLOCK_HZ * (u64)frequency.QuadPart / KHDAYS_CLOCK_HZ);
    } else {
        ticks = rate_ticks + (LONGLONG)((double)(LONGLONG)(cycle - rate_cycles) / (double)KHDAYS_CLOCK_HZ / rate *
                                        (double)frequency.QuadPart);
    }
    ticks += start.QuadPart + paused_ticks;
    ReleaseSRWLockShared(&pause_lock);
    return ticks;
}

/* The screen's lock (clock.h). */
static volatile LONG lock_screen;  /* the host's: a window shows the game */
static int lock_trace = -1;        /* KHDAYS_TRACE_LOCK: the phase every 300 VBlanks */
static double lock_test_hz = -1.0; /* KHDAYS_SCREEN_LOCK=HZ: a screen at that rate, whatever this one's (testing) */

void khdays_clock_lock_screen(int on)
{
    InterlockedExchange(&lock_screen, on != 0);
}

void khdays_clock_vblank(u64 vblank)
{
    /* where the VBlank should come: this long before the screen's refresh (the picture made, drawn and
     * presented meanwhile), and how hard each VBlank's error pulls the rate */
    static const double lead_ms = 6.0, pull = 0.05, reach = 0.005;
    static unsigned traced;
    DWM_TIMING_INFO info;
    const double ds_frame = (double)KHDAYS_CYCLES_PER_FRAME / KHDAYS_CLOCK_HZ * (double)frequency.QuadPart;
    double refresh, phase, error, wanted;
    LONGLONG when;
    int refreshes;
    if (lock_trace < 0) {
        const char *test = getenv("KHDAYS_SCREEN_LOCK");
        lock_trace = getenv("KHDAYS_TRACE_LOCK") != NULL;
        lock_test_hz = test != NULL ? atof(test) : 0.0;
    }
    const LONG screen = InterlockedCompareExchange(&lock_screen, 0, 0);
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (!screen || paused || FAILED(DwmGetCompositionTimingInfo(NULL, &info)) || info.qpcRefreshPeriod == 0) {
        if (lock_trace && ++traced % 300 == 0) {
            HRESULT hr;
            ZeroMemory(&info, sizeof(info));
            info.cbSize = sizeof(info);
            hr = DwmGetCompositionTimingInfo(NULL, &info);
            fprintf(stderr, "lock: off (screen %ld, held %d, compositor %08lx, period %llu)\n", (long)screen, paused,
                    (unsigned long)hr, (unsigned long long)info.qpcRefreshPeriod);
        }
        set_rate(1.0);
        return;
    }
    if (lock_test_hz > 0.0) {
        /* refreshes from the game's start on, at that rate */
        info.qpcRefreshPeriod = (QPC_TIME)((double)frequency.QuadPart / lock_test_hz);
        info.qpcVBlank = (QPC_TIME)start.QuadPart;
    }
    /* the screen's refreshes a VBlank: one at 60 Hz, two at 120 Hz; none that fits at 75 or 144 Hz */
    refreshes = (int)(ds_frame / (double)info.qpcRefreshPeriod + 0.5);
    refresh = (double)info.qpcRefreshPeriod * (refreshes > 0 ? refreshes : 1);
    if (refreshes < 1 || refresh / ds_frame < 0.99 || refresh / ds_frame > 1.01) {
        set_rate(1.0);
        return;
    }
    /* the VBlank's place in the screen's frame (when it was due: the game's thread takes it a little
     * later), and how far from where it should be (-half to half) */
    when = counter_at(vblank);
    phase = fmod((double)(when - (LONGLONG)info.qpcVBlank), refresh);
    phase = phase < 0.0 ? phase + refresh : phase;
    error = phase - (refresh - lead_ms / 1000.0 * (double)frequency.QuadPart);
    error -= refresh * floor(error / refresh + 0.5);
    /* late in the frame: run a little faster, so the next VBlanks come sooner */
    wanted = error / refresh * pull;
    wanted = wanted > reach ? reach : wanted < -reach ? -reach : wanted;
    set_rate(ds_frame / refresh * (1.0 + wanted));
    if (lock_trace && ++traced % 300 == 0) {
        fprintf(stderr, "lock: the VBlank %.2f ms after the screen's refresh (%.2f ms a frame), the time at %.4f\n",
                phase * 1000.0 / (double)frequency.QuadPart, refresh * 1000.0 / (double)frequency.QuadPart,
                khdays_clock_rate());
    }
}

void khdays_clock_hold(unsigned reason, int on)
{
    AcquireSRWLockExclusive(&pause_lock);
    if (on) {
        if (reasons == 0) {
            paused_at = counter_now();
        }
        reasons |= reason;
    } else if (reasons & reason) {
        reasons &= ~reason;
        if (reasons == 0) {
            paused_ticks += counter_now() - paused_at;
        }
    }
    paused = reasons != 0;
    ReleaseSRWLockExclusive(&pause_lock);
}

int khdays_clock_held(unsigned reason)
{
    int held;
    AcquireSRWLockShared(&pause_lock);
    held = (reasons & reason) != 0;
    ReleaseSRWLockShared(&pause_lock);
    return held;
}

/* An auto-reset event: set by khdays_clock_wake, it ends the wait of
 * khdays_clock_sleep_until_or_woken (and, set with no one waiting, the next
 * one at once, so a wake is never lost). */
static HANDLE wake_event;
static INIT_ONCE wake_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK create_wake_event(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    return wake_event != NULL;
}

void khdays_clock_wake(void)
{
    InitOnceExecuteOnce(&wake_once, create_wake_event, NULL, NULL);
    SetEvent(wake_event);
}

static void sleep_until(u64 cycle, int wakeable)
{
    if (wakeable) {
        InitOnceExecuteOnce(&wake_once, create_wake_event, NULL, NULL);
    }
    for (;;) {
        const u64 now = khdays_clock_cycles();
        u64 ms;
        if (now >= cycle) {
            return;
        }
        if (paused) {
            /* the time stands still: nothing to wait for yet */
            if (!wakeable) {
                Sleep(10);
            } else if (WaitForSingleObject(wake_event, 10) == WAIT_OBJECT_0) {
                return;
            }
            continue;
        }
        ms = (cycle - now) * 1000 / KHDAYS_CLOCK_HZ;
        /* Sleep coarsely, then spin the last millisecond. */
        if (ms > 1) {
            if (wakeable) {
                if (WaitForSingleObject(wake_event, (DWORD)(ms - 1)) == WAIT_OBJECT_0) {
                    return;
                }
            } else {
                Sleep((DWORD)(ms - 1));
            }
        } else {
            if (wakeable && WaitForSingleObject(wake_event, 0) == WAIT_OBJECT_0) {
                return;
            }
            YieldProcessor();
        }
    }
}

void khdays_clock_sleep_until(u64 cycle)
{
    sleep_until(cycle, 0);
}

void khdays_clock_sleep_until_or_woken(u64 cycle)
{
    sleep_until(cycle, 1);
}
