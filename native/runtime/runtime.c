/* Host-side services the HAL calls into. */
#include "../hal/hal.h"
#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>

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
    return 1;
}

void khdays_runtime_wait(void)
{
    khdays_hal_unimplemented("khdays_runtime_wait (interrupt delivery)");
}
