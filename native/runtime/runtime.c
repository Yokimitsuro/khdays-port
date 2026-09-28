/* Host-side services the HAL calls into. */
#include "../hal/hal.h"
#include "clock.h"
#include "events.h"
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
    khdays_boot_environment();
    khdays_io_reset();
    khdays_io_trap_init();
    khdays_threads_init();
    khdays_clock_init();
    return 1;
}

/* No window yet: nothing touches the screen. */
int khdays_host_touch(int *x, int *y)
{
    *x = *y = 0;
    return 0;
}

void khdays_host_frame(void)
{
    const u32 frame = khdays_events_vblank_count();
    if (frame % 60 == 0) {
        /* the scene controller's current scene id (0x0204bda4 + 0x0c) */
        fprintf(stderr, "khdays-native: frame %u, scene %u\n", frame, *(volatile u32 *)0x0204bdb0);
        fflush(stderr);
    }
}
