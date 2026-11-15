/* The frame's phases timed (phases.h). */
#include "phases.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static volatile LONG on = -1;
static LONGLONG frequency;
static volatile LONGLONG ticks[KHDAYS_PHASE_COUNT]; /* the counter's ticks spent in each, since the last report */
static volatile LONGLONG runs[KHDAYS_PHASE_COUNT];

static const char *const names[KHDAYS_PHASE_COUNT] = {
    "model load",   "model pose", "cloth",  "skin",        "HD decode",     "shadow capture",
    "shadow build", "3D finish",  "interp", "render prep", "render submit",
};

int khdays_phases_on(void)
{
    LONG v = InterlockedCompareExchange(&on, -1, -1); /* read as written: by an Interlocked call */
    if (v < 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        frequency = f.QuadPart;
        v = getenv("KHDAYS_PROFILE") != NULL;
        InterlockedExchange(&on, v);
    }
    return v;
}

unsigned long long khdays_phase_start(void)
{
    LARGE_INTEGER now;
    if (!khdays_phases_on()) {
        return 0;
    }
    QueryPerformanceCounter(&now);
    return (unsigned long long)now.QuadPart;
}

void khdays_phase_end(KhdaysPhase phase, unsigned long long started)
{
    LARGE_INTEGER now;
    if (started == 0 || (unsigned)phase >= KHDAYS_PHASE_COUNT) {
        return;
    }
    QueryPerformanceCounter(&now);
    InterlockedAdd64(&ticks[phase], now.QuadPart - (LONGLONG)started);
    InterlockedIncrement64(&runs[phase]);
}

void khdays_phase_more(KhdaysPhase phase, unsigned long long started)
{
    LARGE_INTEGER now;
    if (started == 0 || (unsigned)phase >= KHDAYS_PHASE_COUNT) {
        return;
    }
    QueryPerformanceCounter(&now);
    InterlockedAdd64(&ticks[phase], now.QuadPart - (LONGLONG)started);
}

void khdays_phases_second(void)
{
    char line[1024];
    int at = 0;
    if (!khdays_phases_on()) {
        return;
    }
    at += snprintf(line + at, sizeof line - (size_t)at, "phases:");
    for (int p = 0; p < KHDAYS_PHASE_COUNT; ++p) {
        const LONGLONG t = InterlockedExchange64(&ticks[p], 0), n = InterlockedExchange64(&runs[p], 0);
        if (n == 0 || at >= (int)sizeof line - 64) {
            continue;
        }
        at += snprintf(line + at, sizeof line - (size_t)at, " %s %.2f ms (%lld, %.3f ms each),", names[p],
                       (double)t * 1000.0 / (double)frequency, n, (double)t * 1000.0 / (double)frequency / (double)n);
    }
    if (at > 0 && line[at - 1] == ',') {
        line[at - 1] = '\0';
    }
    fprintf(stderr, "%s\n", line);
}
