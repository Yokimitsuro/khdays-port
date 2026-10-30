/* The field's room change (field_wipe.h). */
#include "field_wipe.h"

#include "../core/events.h"

#include <windows.h>

extern void Ov002_TickSceneFadeIn(void);
extern void Ov002_TickSceneFadeOut(void);

#define RECENT 3u /* VBlanks: a step every one or two of them while the wipe lasts */

static volatile LONG seen; /* a step has run */
static volatile LONG at;   /* the VBlank count when the last one ran */

void khdays_field_wipe_update_ran(uintptr_t update)
{
    if (update == (uintptr_t)Ov002_TickSceneFadeIn || update == (uintptr_t)Ov002_TickSceneFadeOut) {
        InterlockedExchange(&at, (LONG)khdays_events_vblank_count());
        InterlockedExchange(&seen, 1);
    }
}

int khdays_field_wipe_running(uint32_t frame)
{
    if (!InterlockedCompareExchange(&seen, 0, 0)) {
        return 0;
    }
    return frame - (uint32_t)InterlockedCompareExchange(&at, 0, 0) <= RECENT;
}
