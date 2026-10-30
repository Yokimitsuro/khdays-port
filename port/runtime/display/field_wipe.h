/* The field's room change (ov002): whether its wipe is being drawn. The game draws it only from the two
 * steps of its scene's fade (Ov002_TickSceneFadeIn closing, Ov002_TickSceneFadeOut opening: each calls
 * Ov002_UpdateSceneFrame, the quad of the world's shape and the black round it), which run as an object's
 * update; the frame package says so (frame_package.h `field_wipe`), and only then does the window look
 * for the wipe's quad to cover the widescreen's columns beside it (host/render/gpu_present.c). */
#ifndef KHDAYS_FIELD_WIPE_H
#define KHDAYS_FIELD_WIPE_H

#include <stdint.h>

/* The game's thread: an object's update ran (runtime/core/diag.c check_update). */
void khdays_field_wipe_update_ran(uintptr_t update);

/* The game's thread: whether one of the wipe's steps ran in the last few VBlanks before `frame` (the
 * VBlank count); the field runs a step every one or two VBlanks while it lasts. */
int khdays_field_wipe_running(uint32_t frame);

#endif
