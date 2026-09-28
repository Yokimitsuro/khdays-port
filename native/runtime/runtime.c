/* Host-side services the HAL calls into. */
#include "../hal/hal.h"
#include "../host/host.h"
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
    khdays_threads_init();
    khdays_clock_init();
    return 1;
}

void khdays_host_frame(void)
{
    const u32 frame = khdays_events_vblank_count();
    if (frame % 60 == 0) {
        /* the scene controller (0x0204bda8: object, table entry, current id,
         * pending id and argument; src/calls/func_0202099c.c) */
        const volatile u32 *scene = (const volatile u32 *)0x0204bda8;
        fprintf(stderr, "khdays-native: frame %u, scene %u (object %08x, pending %u arg %u)\n", frame,
                scene[2], scene[0], scene[3], scene[4]);
        fflush(stderr);
    }
}
