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

void khdays_host_frame(void)
{
    const u32 frame = khdays_events_vblank_count();
    if (frame % 60 == 0) {
        fprintf(stderr, "khdays-native: frame %u\n", frame);
        fflush(stderr);
    }
}
