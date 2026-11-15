/* Timed hardware events (events.h): VBlank, the alarm timer, the interrupts they raise, and the
 * game's waits, in which time runs forward to the next event. */
#include "events.h"
#include "window.h"
#include "files.h"
#include "clock.h"
#include "io.h"
#include "arm7.h"
#include "../display/display.h"
#include "../input/input.h"
#include "session_log.h"
#include "phases.h"
#include "../movies/video.h"
#include "../net/net_session.h"
#include "../sound/sound_bridge.h"
#include "../sound/room_place.h"
#include "runtime.h"
#include "../../gfx/g3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static u64 last_update;
static u64 alarm_at; /* the cycle timer 1 would overflow at (khdays_alarm_set_after) */
static int alarm_armed;
static u64 next_vblank = KHDAYS_VBLANK_LINE * KHDAYS_CYCLES_PER_LINE;
static u32 vblank_count;
static u32 presented;
static u32 dma_at_vblank; /* channel mask */

/* The next timed event on the game's side. The sound's alarms are not among them: the sound thread
 * keeps the ARM7's time, and a reply it has for the game wakes the wait. */
static u64 next_event(void)
{
    u64 next = next_vblank;
    if (alarm_armed && alarm_at < next) {
        next = alarm_at;
    }
    return next;
}

void khdays_alarm_set_after(u64 ticks)
{
    alarm_at = khdays_clock_cycles() + ticks * 64;
    alarm_armed = 1;
}

void khdays_alarm_clear(void)
{
    alarm_armed = 0;
}

void khdays_events_reset(void)
{
    alarm_armed = 0;
    dma_at_vblank = 0;
}

void khdays_events_dma_at_vblank(int channel)
{
    dma_at_vblank |= 1u << channel;
}

u32 khdays_events_vblank_count(void)
{
    return vblank_count;
}

u64 khdays_events_vblank_time(void)
{
    return next_vblank;
}

/* --- KHDAYS_TRACE_TIMING (events.h) ---------------------------------------------- */

static int timing = -1;
static u32 timing_alarms, timing_countups, timing_flips, timing_outside, timing_merged;
static u32 timing_flip_hash = 0x811c9dc5u, timing_first_flip, timing_last_flip;
static u32 timing_accesses[KHDAYS_TIMING_KINDS];

/* The instructions already named, by kind (the list grows with the game's). */
typedef struct TimingSite {
    int kind;
    khdays_uptr site; /* the instruction's native address */
} TimingSite;
static TimingSite *timing_sites;
static int timing_nsites, timing_room;

int khdays_timing_on(void)
{
    if (timing < 0) {
        timing = getenv("KHDAYS_TRACE_TIMING") != NULL;
    }
    return timing;
}

void khdays_timing_alarm(void)
{
    ++timing_alarms;
}

void khdays_timing_countup(void)
{
    ++timing_countups;
}

void khdays_timing_flip(int outside)
{
    if (outside) {
        ++timing_outside;
        return;
    }
    if (timing_flips++ == 0) {
        timing_first_flip = vblank_count;
    }
    timing_last_flip = vblank_count;
    for (int i = 0; i < 4; ++i) {
        timing_flip_hash = (timing_flip_hash ^ ((vblank_count >> (8 * i)) & 0xff)) * 0x01000193u;
    }
}

void khdays_timing_access(int kind, khdays_uptr site)
{
    static const char *const what[KHDAYS_TIMING_KINDS] = {"a timer register read", "a timer register written",
                                                          "DISPSTAT read", "VCOUNT read"};
    ++timing_accesses[kind];
    for (int i = 0; i < timing_nsites; ++i) {
        if (timing_sites[i].kind == kind && timing_sites[i].site == site) {
            return;
        }
    }
    if (timing_nsites == timing_room) {
        const int room = timing_room ? timing_room * 2 : 32;
        TimingSite *grown = (TimingSite *)realloc(timing_sites, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        timing_sites = grown;
        timing_room = room;
    }
    timing_sites[timing_nsites].kind = kind;
    timing_sites[timing_nsites].site = site;
    ++timing_nsites;
    fprintf(stderr, "timing: %s by %s (%p)\n", what[kind], khdays_diag_function_at((const void *)site),
            (const void *)site);
}

static void timing_report(void)
{
    fprintf(stderr,
            "timing: %u vblanks, %u logic frames, %u alarm interrupts, %u tick count-ups; %u movie flips (hash %08x, "
            "vblanks %u-%u), %u outside the line window; accesses: %u timer reads, %u timer writes, %u DISPSTAT, "
            "%u VCOUNT; %u vblanks merged into the one before (its interrupt not taken yet)\n",
            vblank_count, khdays_g3d_swap_count(), timing_alarms, timing_countups, timing_flips, timing_flip_hash,
            timing_first_flip, timing_last_flip, timing_outside, timing_accesses[KHDAYS_TIMING_TIMER_READ],
            timing_accesses[KHDAYS_TIMING_TIMER_WRITE], timing_accesses[KHDAYS_TIMING_DISPSTAT_READ],
            timing_accesses[KHDAYS_TIMING_VCOUNT_READ], timing_merged);
    fflush(stderr);
}

void khdays_events_update(void)
{
    const u64 now = khdays_clock_cycles();
    if (alarm_armed && now >= alarm_at) {
        alarm_armed = 0;
        khdays_io_request_irq(1u << 4); /* timer 1: the SDK's alarm handler */
    }
    if (khdays_g3d_fifo_irq()) {
        khdays_io_request_irq(1u << 21); /* GXFIFO: raised for as long as the condition holds */
    }
    while (now >= next_vblank) {
        /* VBlank: the frame the game prepared is shown (only the last when several VBlanks passed at once),
         * then the next frame's input is read. */
        if (now < next_vblank + KHDAYS_CYCLES_PER_FRAME) {
            khdays_display_vblank(vblank_count);
        }
        khdays_input_frame(vblank_count + 1);
        khdays_room_place_frame(); /* a room's music: where the player is */
        if (IO16(0x004) & 0x0008) { /* DISPSTAT: VBlank IRQ enabled */
            if (IO32(0x214) & 1u) {
                ++timing_merged; /* the last VBlank's interrupt is not taken yet: one handler serves both */
            }
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
        khdays_clock_vblank(next_vblank); /* the screen's lock */
        ++vblank_count;
        next_vblank += KHDAYS_CYCLES_PER_FRAME;
        if (vblank_count % 600 == 0 && khdays_timing_on()) {
            timing_report();
        }
    }
    khdays_sound_bridge_event(now);
    last_update = now;
}

static int irq_line(void)
{
    return IO32(0x208) != 0 && (IO32(0x210) & IO32(0x214)) != 0;
}

/* An unmask point (interrupts enabled again): takes what is pending, as the
 * CPU would the moment the mask lifts. */
void khdays_irq_poll(void)
{
    khdays_events_update();
    while (khdays_irq_deliver()) {
    }
}

/* KHDAYS_PROFILE: once a second, how long the game ran between its waits and how long it waited (a
 * game that never waits starves its lower-priority threads), and its longest run without a wait. */
static int profile = -1;
static u64 wait_end, busy, waited, waits, report_at, longest;

static void profile_wait(u64 enter)
{
    if (profile < 0) {
        profile = getenv("KHDAYS_PROFILE") != NULL;
    }
    if (!profile) {
        return;
    }
    if (wait_end != 0) {
        busy += enter - wait_end;
        if (enter - wait_end > longest) {
            longest = enter - wait_end;
        }
    }
    ++waits;
    if (enter >= report_at) {
        fprintf(stderr, "wait: %llu waits, %.1f ms running, %.1f ms waiting in the last second, longest %.1f ms\n",
                waits, busy * 1000.0 / KHDAYS_CLOCK_HZ, waited * 1000.0 / KHDAYS_CLOCK_HZ,
                longest * 1000.0 / KHDAYS_CLOCK_HZ);
        busy = waited = waits = longest = 0;
        khdays_files_second();  /* the file reads of that second */
        khdays_video_second();  /* the movie frames of that second */
        khdays_phases_second(); /* the frame's phases in that second */
        report_at = enter + KHDAYS_CLOCK_HZ;
    }
}

int khdays_events_in_vblank_period(void)
{
    const u64 began = next_vblank - KHDAYS_CYCLES_PER_FRAME;
    return khdays_events_in_vblank() || khdays_clock_cycles() - began < KHDAYS_VBLANK_CYCLES;
}

void khdays_wait_point(void)
{
    khdays_runtime_wait();
}

/* In a multiplayer session, how evenly the frames come: the time between two frames handed to the
 * picture, counted in five spans and logged every 5 s. */
static void frame_pace(void)
{
    static u64 last, reported;
    static unsigned int spans[5];
    static u32 pictures3d;
    const u64 now = khdays_clock_cycles();
    if (last != 0) {
        const double ms = (double)(now - last) * 1000.0 / KHDAYS_CLOCK_HZ;
        ++spans[ms < 18.0 ? 0 : ms < 25.0 ? 1 : ms < 34.0 ? 2 : ms < 50.0 ? 3 : 4];
    }
    last = now;
    if (reported == 0) {
        reported = now;
        pictures3d = khdays_display_3d_serial();
    }
    if (now - reported >= 5ull * KHDAYS_CLOCK_HZ) {
        if (khdays_log_on() && khdays_net_in_session()) {
            /* the game's frame-rate mode (gObjSystem, main.c's frame loop): 1 means a
             * step every two VBlanks, other values no pace of its own */
            khdays_log("frames: %u under 18 ms, %u 18-25 ms, %u 25-34 ms, %u 34-50 ms, %u over 50 ms; %u new 3D "
                       "pictures; frame mode %u",
                       spans[0], spans[1], spans[2], spans[3], spans[4], khdays_display_3d_serial() - pictures3d,
                       (unsigned)*(const volatile u8 *)KHDAYS_W(0x0204c058));
        }
        memset(spans, 0, sizeof(spans));
        pictures3d = khdays_display_3d_serial();
        reported = now;
    }
}

/* The CPU halted until an interrupt (OS_Halt, SWI Halt, the RTC busy-wait): runs time forward to the
 * next event, presents finished frames, and returns once the interrupt line is up. */
void khdays_runtime_wait(void)
{
    static u64 log_wait_end;
    const u64 enter = khdays_clock_cycles();
    profile_wait(enter);
    if (log_wait_end != 0 && enter - log_wait_end > KHDAYS_CLOCK_HZ / 20 && khdays_log_on()) {
        /* the session log: over 50 ms without a wait (so without a frame), and in which scene */
        /* the memory free then: a PC short of it pages, and the game's thread waits on the disk */
        MEMORYSTATUSEX memory;
        memory.dwLength = sizeof(memory);
        if (!GlobalMemoryStatusEx(&memory)) {
            memory.ullAvailPhys = 0;
        }
        khdays_files_log_reads();
        khdays_log("stall: %.0f ms without a wait, scene %u, %llu MB of memory free",
                   (enter - log_wait_end) * 1000.0 / KHDAYS_CLOCK_HZ, *(const volatile u32 *)KHDAYS_W(0x0204bdb0),
                   (unsigned long long)(memory.ullAvailPhys >> 20));
    }
    for (;;) {
        khdays_events_update();
        if (presented != vblank_count) {
            presented = vblank_count;
            khdays_host_frame();
            frame_pace();
        }
        if (irq_line()) {
            if (profile) {
                wait_end = khdays_clock_cycles();
                waited += wait_end - enter;
            }
            log_wait_end = khdays_clock_cycles();
            khdays_irq_deliver();
            return;
        }
        khdays_clock_sleep_until_or_woken(next_event());
    }
}
