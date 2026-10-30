/* Testing traces of the field's event scripts (KHDAYS_TRACE_SCENES): which script runs, where its
 * skip target stands, its status, the game's movies, a line per change. Never shown to players. */
#ifndef KHDAYS_SCENE_TRACE_H
#define KHDAYS_SCENE_TRACE_H

#include <stdint.h>

/* The game's thread, once a frame (khdays_host_frame). */
void khdays_scene_trace_frame(uint32_t frame);

/* The scene running now (EV_S#4), "" when none. */
const char *khdays_scene_current(void);
/* Whether a day's monologue runs now (any thread). */
int khdays_monologue_running(void);

#endif
