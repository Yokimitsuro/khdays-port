/* Host-side services the HAL calls into. */
#include "../hal/hal.h"
#include "../host/host.h"
#include "arm.h"
#include "clock.h"
#include "display.h"
#include "events.h"
#include "input.h"
#include "io.h"
#include "rom.h"
#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>

extern void khdays_threads_init(void);

void khdays_hal_unimplemented(const char *what)
{
    fprintf(stderr, "khdays-native: reached an unimplemented routine: %s\n", what);
    fflush(stderr);
    exit(3);
}

void khdays_abi_gap(const char *what)
{
    fprintf(stderr, "khdays-native: reached a call the ROM makes differently: %s\n", what);
    fflush(stderr);
    exit(10);
}

int khdays_runtime_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!khdays_rom_init()) {
        return 0;
    }
    khdays_input_init();
    khdays_display_init();
    if (!khdays_host_start()) {
        return 0;
    }
    khdays_boot_environment();
    khdays_io_reset();
    khdays_io_trap_init();
    khdays_arm_init();
    khdays_threads_init();
    {
        extern void khdays_async_irq_start(void);
        khdays_async_irq_start();
    }
    khdays_clock_init();
    return 1;
}

/* r9 as the ARM9 leaves it where the decomp's C reads a local it never set
 * because the ROM keeps it in r9 (prepare.py ABI_FIXES). Obj_UpdateAll
 * saves r9 on entry -- main's, which the firmware left, unknown here -- and
 * sets it to each update's result (`blx r0; mov sb, r0`, 0x02023b64/68); the
 * update chains the port has checked leave it alone. Reading it before the
 * frame's first update ran stops, naming the read. */
static int rom_r9, rom_r9_known;

void khdays_rom_r9_unknown(void)
{
    rom_r9_known = 0;
}

void khdays_rom_r9_set(int value)
{
    rom_r9 = value;
    rom_r9_known = 1;
}

int khdays_rom_r9(const char *reader)
{
    if (!rom_r9_known) {
        khdays_abi_gap(reader);
    }
    return rom_r9;
}

static int in_main_ram(u32 p)
{
    return p >= 0x02000000u && p < 0x02400000u;
}

/* KHDAYS_TRACE_PLAYER: the local player's position every 30 frames, for
 * scripts that walk. The field keeps its players' actors in the table at
 * *(data_ov022_020b2e78 + 4), 0xc bytes an entry, the actor at the entry's
 * +4, then +0x20 (GetEntryField20ByIndex); the position is the actor's
 * +0x48c (func_ov022_020881f8), fx32 (1.0 = 0x1000). */
static void trace_player(u32 frame)
{
    static int trace = -1;
    u32 base, entry, actor;
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_PLAYER") != NULL;
    }
    if (!trace || frame % 30 != 0) {
        return;
    }
    base = *(const volatile u32 *)0x020b2e7c;
    for (u32 i = 0; i < 8 && in_main_ram(base); ++i) {
        entry = *(const volatile u32 *)(base + i * 0xc + 4);
        actor = in_main_ram(entry) ? *(const volatile u32 *)(entry + 0x20) : 0;
        if (in_main_ram(actor)) {
            const volatile s32 *pos = (const volatile s32 *)(actor + 0x48c);
            fprintf(stderr, "player: frame %u, entry %u, actor %08x at (%.2f, %.2f, %.2f)\n", frame, i,
                    actor, pos[0] / 4096.0, pos[1] / 4096.0, pos[2] / 4096.0);
        }
    }
}

void khdays_host_frame(void)
{
    const u32 frame = khdays_events_vblank_count();
    trace_player(frame);
    if (frame % 60 == 0) {
        khdays_arm_report();
        /* the scene controller (0x0204bda8: object, table entry, current id,
         * pending id and argument; src/calls/func_0202099c.c) */
        const volatile u32 *scene = (const volatile u32 *)0x0204bda8;
        fprintf(stderr, "khdays-native: frame %u, scene %u (object %08x, pending %u arg %u)\n", frame,
                scene[2], scene[0], scene[3], scene[4]);
        fflush(stderr);
    }
}
