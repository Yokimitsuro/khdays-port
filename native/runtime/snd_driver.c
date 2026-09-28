/* The ARM7's sound driver: NitroSDK's, as the game's ARM7 binary carries
 * it, run in the interpreter (arm.c) against the sound hardware (sound.c),
 * so music and effects come out as the DS makes them -- the sequencer, the
 * envelopes, the channel allocation are the driver's own code.
 *
 * The ARM7's main is not run (the rest of the ARM7 is answered at a high
 * level, arm7.c); only its call SND_Init(6) is, after the driver's part of
 * the binary is loaded where its autoload puts it. What the driver asks of
 * the ARM7's operating system is done natively, each routine replaced by
 * what it does in this binary (read from its code):
 *
 *   OS_CreateThread, OS_WakeupThreadDirect  the one thread, SndThread
 *   OS_SleepThread, OS_WakeupThread         its waits on its message queues
 *   OS_GetTick                              the clock / 64 (timer 0 at F/64)
 *   OS_SetAlarm, OS_SetPeriodicAlarm,       the alarm list, fired as the
 *   OS_CancelAlarm                          ARM7's timer interrupt would
 *   PXI_SetFifoRecvCallback,                the FIFO to and from the ARM9
 *   PXI_SendWordByFifo
 *   OS_Panic                                a stop, with a message
 *
 * and the BIOS's GetPitchTable and GetVolumeTable. The message queues and
 * everything else run as they are. The ARM7's time is that of the event
 * being handled: an alarm fires at its own cycle, however late the game's
 * thread gets to it, so a stall costs no driver ticks. */
#include "snd_driver.h"
#include "arm.h"
#include "arm7.h"
#include "clock.h"
#include "rom.h"
#include "sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This ROM's ARM7 binary (loaded at 0x02380000): its first autoload
 * segment, which holds the driver, goes to 0x037f8000 (crt0's module
 * parameters at +0x1d0 list it). The addresses below are in it; the hash
 * makes sure they are. */
#define ARM7_RAM          0x02380000u
#define SEGMENT_OFFSET    0x01e8u
#define SEGMENT_ADDRESS   0x037f8000u
#define SEGMENT_SIZE      0xf4b0u
#define SEGMENT_BSS       0x3e64u
#define SEGMENT_FNV1A     0x9f4bf423u

#define SND_INIT          0x037feef8u  /* SND_Init(priority) */
#define SND_PRIORITY      6            /* what the ARM7's main passes */
/* crt0's stacks: system mode below the IRQ stack; interrupt handlers (the
 * PXI callback, alarm handlers) run on the IRQ stack. */
#define MAIN_STACK        0x0380fb7cu
#define IRQ_STACK         0x0380ff80u

#define PXI_TAG_SOUND     7

/* The ARM9's copy of NitroSDK's volume table (snd_util.c, for its own
 * SND_CalcChannelVolume): byte for byte the ARM7 BIOS's GetVolumeTable,
 * as checked against a dump of that BIOS. */
#define ARM9_VOLUME_TABLE 0x02041588u
#define VOLUME_TABLE_SIZE 724  /* 0..0x2d3 (GBATEK) */

static int enabled;
static int trace;     /* KHDAYS_TRACE_SOUND */
static u64 arm7_now;  /* the ARM7's time, in cycles */
static u16 pitch_table[768];

static KhdaysArmThread *thread;
static u32 thread_address;   /* its OSThread */
static int thread_ready;     /* woken, not yet run */
static u32 thread_waits_on;  /* the OSThreadQueue it sleeps on */
#define WAITS_DIRECT 1u      /* slept on no queue: only a direct wakeup */

static u32 pxi_callback[32];

static void stop(const char *what, u32 value)
{
    fprintf(stderr, "khdays-native: ARM7 sound driver: %s (0x%08x)\n", what, value);
    fflush(stderr);
    exit(15);
}

static u32 rd32(u32 address)
{
    return *(volatile u32 *)(size_t)address;
}

static void wr32(u32 address, u32 value)
{
    *(volatile u32 *)(size_t)address = value;
}

static u64 rd64(u32 address)
{
    return (u64)rd32(address + 4) << 32 | rd32(address);
}

static void wr64(u32 address, u64 value)
{
    wr32(address, (u32)value);
    wr32(address + 4, (u32)(value >> 32));
}

static u32 stack_arg(const u32 *r, int n)
{
    return rd32(r[13] + 4u * (u32)n);
}

static u64 tick_now(void)
{
    return arm7_now / 64;
}

/* --- Threads ------------------------------------------------------------------ */

/* OS_CreateThread(thread, func, arg, stack_top, stack_size, priority): the
 * context starts at func(arg) with sp = stack_top - 4 - 0x40, 8-aligned
 * (OS_InitContext). */
static int os_create_thread(u32 *r)
{
    u32 sp = r[3] - 4 - 0x40;
    if (thread != NULL) {
        stop("a second ARM7 thread", r[1]);
    }
    if (sp & 4) {
        sp -= 4;
    }
    thread = khdays_arm7cpu_thread(r[1], r[2], sp);
    thread_address = r[0];
    return 0;
}

static int os_wakeup_thread_direct(u32 *r)
{
    if (r[0] != thread_address) {
        stop("a wakeup of an unknown thread", r[0]);
    }
    thread_ready = 1;
    thread_waits_on = 0;
    return 0;
}

/* OS_SleepThread(queue): the thread waits for OS_WakeupThread(queue). */
static int os_sleep_thread(u32 *r)
{
    thread_waits_on = r[0] != 0 ? r[0] : WAITS_DIRECT;
    thread_ready = 0;
    return 1;
}

static int os_wakeup_thread(u32 *r)
{
    if (thread_waits_on != 0 && thread_waits_on == r[0]) {
        thread_waits_on = 0;
        thread_ready = 1;
    }
    return 0;
}

static void run_thread(void)
{
    while (thread != NULL && thread_ready) {
        thread_ready = 0;
        if (khdays_arm7cpu_resume(thread)) {
            stop("the sound thread returned", 0);
        }
    }
}

/* --- Time and alarms --------------------------------------------------------------
 * OSAlarm: handler +0x00, arg +0x04, fire +0x0c (u64), prev/next +0x14/+0x18
 * (the list, kept here instead), period +0x1c (u64), start +0x24 (u64). */

static int os_get_tick(u32 *r)
{
    const u64 tick = tick_now();
    r[0] = (u32)tick;
    r[1] = (u32)(tick >> 32);
    return 0;
}

#define MAX_ALARMS 32
static struct {
    u32 alarm;
    u64 fire;
} alarms[MAX_ALARMS];
static int alarm_count;

static void unlink_alarm(u32 alarm)
{
    for (int i = 0; i < alarm_count; ++i) {
        if (alarms[i].alarm == alarm) {
            memmove(&alarms[i], &alarms[i + 1], (size_t)(alarm_count - i - 1) * sizeof(alarms[0]));
            --alarm_count;
            return;
        }
    }
}

/* OSi_InsertAlarm: a periodic alarm fires at the first start + k * period
 * after now (or at start, if that is still ahead); the list is ordered by
 * fire time, a new alarm after those due at the same tick. */
static void insert_alarm(u32 alarm, u64 fire)
{
    const u64 period = rd64(alarm + 0x1c);
    int at;
    if (period != 0) {
        const u64 start = rd64(alarm + 0x24), now = tick_now();
        fire = start;
        if (start < now) {
            fire = start + period * ((now - start) / period + 1);
        }
    }
    wr64(alarm + 0x0c, fire);
    unlink_alarm(alarm);
    if (alarm_count == MAX_ALARMS) {
        stop("too many ARM7 alarms", alarm);
    }
    for (at = 0; at < alarm_count && (s64)(fire - alarms[at].fire) >= 0; ++at) {
    }
    memmove(&alarms[at + 1], &alarms[at], (size_t)(alarm_count - at) * sizeof(alarms[0]));
    alarms[at].alarm = alarm;
    alarms[at].fire = fire;
    ++alarm_count;
}

/* OS_SetAlarm(alarm, tick, handler, arg): once, `tick` from now. */
static int os_set_alarm(u32 *r)
{
    const u32 alarm = r[0];
    if (alarm == 0 || rd32(alarm) != 0) {
        stop("OS_SetAlarm on an alarm in use", alarm);
    }
    wr64(alarm + 0x1c, 0);
    wr32(alarm, r[3]);
    wr32(alarm + 4, stack_arg(r, 0));
    insert_alarm(alarm, tick_now() + ((u64)r[2] << 32 | r[1]));
    return 0;
}

/* OS_SetPeriodicAlarm(alarm, start, period, handler, arg). */
static int os_set_periodic_alarm(u32 *r)
{
    const u32 alarm = r[0];
    if (alarm == 0 || rd32(alarm) != 0) {
        stop("OS_SetPeriodicAlarm on an alarm in use", alarm);
    }
    if (trace) {
        fprintf(stderr, "snd7: %llu periodic alarm %08x start %llu period %u handler %08x\n",
                (unsigned long long)arm7_now, alarm, (unsigned long long)((u64)r[2] << 32 | r[1]),
                r[3], stack_arg(r, 1));
    }
    wr64(alarm + 0x1c, (u64)stack_arg(r, 0) << 32 | r[3]);
    wr64(alarm + 0x24, (u64)r[2] << 32 | r[1]);
    wr32(alarm, stack_arg(r, 1));
    wr32(alarm + 4, stack_arg(r, 2));
    insert_alarm(alarm, 0);
    return 0;
}

static int os_cancel_alarm(u32 *r)
{
    const u32 alarm = r[0];
    if (rd32(alarm) == 0) {
        return 0;
    }
    unlink_alarm(alarm);
    wr32(alarm, 0);
    wr64(alarm + 0x1c, 0);
    return 0;
}

/* The ARM7's alarm interrupt (OSi_AlarmHandler) for the first alarm: it
 * leaves the list, a one-time alarm loses its handler, the handler runs
 * with its argument, and a periodic alarm goes back in unless the handler
 * cancelled it. By then the ARM7's tick is past the fire tick (the
 * interrupt's entry alone takes more than the 64 cycles of a tick), which
 * is what makes the periodic alarm's next fire the next period. */
static void fire_first_alarm(void)
{
    const u32 alarm = alarms[0].alarm;
    const u32 handler = rd32(alarm);
    const u64 after = (alarms[0].fire + 1) * 64;
    unlink_alarm(alarm);
    if (rd64(alarm + 0x1c) == 0) {
        wr32(alarm, 0);
    }
    if (handler != 0) {
        const u32 arg = rd32(alarm + 4);
        khdays_arm7cpu_call(handler, &arg, 1, IRQ_STACK);
    }
    if (arm7_now < after) {
        arm7_now = after;
    }
    if (rd64(alarm + 0x1c) != 0) {
        wr32(alarm, handler);
        insert_alarm(alarm, 0);
    }
}

/* --- PXI -------------------------------------------------------------------------- */

static int pxi_set_fifo_recv_callback(u32 *r)
{
    pxi_callback[r[0] & 31] = r[1];
    return 0;
}

/* PXI_SendWordByFifo(tag, data, err): 0 = sent. */
static int pxi_send_word_by_fifo(u32 *r)
{
    if (trace) {
        fprintf(stderr, "snd7: %llu to ARM9: tag %u data %07x err %u\n", (unsigned long long)arm7_now,
                r[0] & 31, r[1], r[2] & 1);
    }
    khdays_arm7_reply(r[0] & 31, r[1], r[2] & 1);
    r[0] = 0;
    return 0;
}

static int os_panic(u32 *r)
{
    stop("OS_Panic, called from", r[14]);
    return 0;
}

/* --- The BIOS ---------------------------------------------------------------------- */

/* GetPitchTable (SWI 1Bh) and GetVolumeTable (SWI 1Ch), the two the driver
 * calls (SND_CalcTimer, SND_CalcChannelVolume). GBATEK gives their ranges,
 * not their contents: the pitch table is 0x10000 * (2^(i/768) - 1) rounded,
 * which matches a dump of the BIOS table in all 768 entries (and GBATEK's
 * last value, FF8Ah); the volume table is the ARM9's copy. */
static int bios(u32 number, u32 *r)
{
    switch (number) {
    case 0x1b:
        if (r[0] >= 768) stop("GetPitchTable out of range", r[0]);
        r[0] = pitch_table[r[0]];
        return 1;
    case 0x1c:
        if (r[0] >= VOLUME_TABLE_SIZE) stop("GetVolumeTable out of range", r[0]);
        r[0] = *(const u8 *)(size_t)(ARM9_VOLUME_TABLE + r[0]);
        return 1;
    default:
        return 0;
    }
}

/* --- Running ------------------------------------------------------------------------ */

static void advance_to(u64 cycle)
{
    if (cycle > arm7_now) {
        arm7_now = cycle;
    }
    khdays_sound_run(arm7_now);
}

void khdays_snd_driver_update(u64 cycle)
{
    if (!enabled) {
        return;
    }
    while (alarm_count > 0 && alarms[0].fire * 64 <= cycle) {
        advance_to(alarms[0].fire * 64);
        fire_first_alarm();
        run_thread();
    }
    advance_to(cycle);
}

u64 khdays_snd_driver_next_event(void)
{
    return enabled && alarm_count > 0 ? alarms[0].fire * 64 : ~(u64)0;
}

/* The ARM7's PXI interrupt: the word goes to the tag's callback. */
int khdays_snd_driver_pxi(u32 data, u32 err)
{
    u32 args[3];
    if (!enabled) {
        return 0;
    }
    khdays_snd_driver_update(khdays_clock_cycles());
    if (pxi_callback[PXI_TAG_SOUND] == 0) {
        stop("a sound message before the driver listens", data);
    }
    if (trace) {
        fprintf(stderr, "snd7: %llu from ARM9: %07x\n", (unsigned long long)arm7_now, data);
    }
    args[0] = PXI_TAG_SOUND;
    args[1] = data;
    args[2] = err;
    khdays_arm7cpu_call(pxi_callback[PXI_TAG_SOUND], args, 3, IRQ_STACK);
    run_thread();
    return 1;
}

int khdays_snd_driver_init(void)
{
    const char *setting = getenv("KHDAYS_SOUND");
    const u8 *header = khdays_rom_header();
    const u32 rom = *(const u32 *)(header + 0x30), ram = *(const u32 *)(header + 0x38);
    const u32 size = *(const u32 *)(header + 0x3c);
    const u8 *volume = (const u8 *)(size_t)ARM9_VOLUME_TABLE;
    u32 hash = 0x811c9dc5u;
    static const struct { u32 address; KhdaysArmHook hook; } hooks[] = {
        {0x037fc054u, os_create_thread},
        {0x037fc290u, os_sleep_thread},
        {0x037fc2e4u, os_wakeup_thread},
        {0x037fc36cu, os_wakeup_thread_direct},
        {0x037fd21cu, os_get_tick},
        {0x037fd4dcu, os_set_alarm},
        {0x037fd54cu, os_set_periodic_alarm},
        {0x037fd5c0u, os_cancel_alarm},
        {0x037fde70u, os_panic},
        {0x037fe39cu, pxi_set_fifo_recv_callback},
        {0x037fe410u, pxi_send_word_by_fifo},
    };

    if (setting != NULL && atoi(setting) == 0) {
        fprintf(stderr, "sound: off (KHDAYS_SOUND=0)\n");
        return 0;
    }
    if (ram != ARM7_RAM || size < SEGMENT_OFFSET + SEGMENT_SIZE) {
        fprintf(stderr, "sound: off: the ARM7 binary is not the one the driver was read from\n");
        return 0;
    }
    khdays_rom_read(rom + SEGMENT_OFFSET, (u8 *)(size_t)SEGMENT_ADDRESS, SEGMENT_SIZE);
    for (u32 i = 0; i < SEGMENT_SIZE; ++i) {
        hash = (hash ^ ((const u8 *)(size_t)SEGMENT_ADDRESS)[i]) * 0x01000193u;
    }
    if (hash != SEGMENT_FNV1A) {
        memset((void *)(size_t)SEGMENT_ADDRESS, 0, SEGMENT_SIZE);
        fprintf(stderr, "sound: off: the ARM7 binary is not the one the driver was read from\n");
        return 0;
    }
    memset((void *)(size_t)(SEGMENT_ADDRESS + SEGMENT_SIZE), 0, SEGMENT_BSS);
    if (volume[0] != 0x00 || volume[VOLUME_TABLE_SIZE - 1] != 0x7f) {
        fprintf(stderr, "sound: off: no volume table at 0x%08x\n", ARM9_VOLUME_TABLE);
        return 0;
    }
    for (int i = 0; i < 768; ++i) {
        pitch_table[i] = (u16)floor(65536.0 * (pow(2.0, i / 768.0) - 1.0) + 0.5);
    }
    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        khdays_arm7cpu_hook(hooks[i].address, hooks[i].hook);
    }
    khdays_arm7cpu_bios(bios);

    trace = getenv("KHDAYS_TRACE_SOUND") != NULL;
    arm7_now = khdays_clock_cycles();
    khdays_sound_reset(arm7_now);
    enabled = 1;
    {
        const u32 priority = SND_PRIORITY;
        khdays_arm7cpu_call(SND_INIT, &priority, 1, MAIN_STACK);
    }
    run_thread();
    if (thread == NULL || thread_waits_on == 0) {
        stop("the sound thread did not start and wait", 0);
    }
    return 1;
}
