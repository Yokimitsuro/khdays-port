/* KH Mod SDK: events (kh.events) -- what happens in the game, and what plugins say to one another.
 *
 * A plugin subscribes a function to an event by its name and is called, on the game's thread, each time it is
 * raised. The game's events are the port's (the same the Lua mods hear: docs/SCRIPTING.md, "Events"); a plugin
 * raises events of its own under names with a dot ("<mod id>.<name>"), with a payload of bytes, and every plugin
 * subscribed to that name is called at once. A plugin's subscriptions end when it stops.
 *
 * The game's events and their values:
 *   "frame"         a: the frame, b: 1 while the game is paused     -- once a game frame, after its logic
 *   "scene"         a: from, b: to, c: the argument                  -- the game changed scenes
 *   "title"                                                          -- the title scene was entered
 *   "mission_start" a: the mission's file
 *   "mission_end"   a: the mission, b: the rank, c: complete (1, 0; -1 not told), d: given up (1, 0); text: ending
 *   "hp"            a: before, b: after, c: the most; text "player" or "other"
 *   "item"          a: the item, b: its count, c: the change
 *   "munny"         a: the total, b: the change
 *   "menu_open", "menu_close"   text: "pause", "camp", "settings"
 *   "save"
 * In a callback a plugin may read and change the game's state and raise events; it must not stop itself.
 * An event raised from inside a callback is delivered at once (nested). */
#ifndef KH_EVENTS_H
#define KH_EVENTS_H

#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_EVENTS "kh.events"
#define KH_EVENTS_VERSION 1

typedef struct KhEvent {
    uint32_t size;
    const char *name;
    int64_t a, b, c, d; /* the game's events' values (above); 0 for a plugin's own */
    const char *text;   /* NULL when it has none */
    const void *data;   /* a plugin's own event's payload, valid during the call */
    uint32_t data_size;
} KhEvent;

typedef void (*KhEventFunction)(const KhEvent *event, void *user);

/* A subscription: 0 is none. */
typedef uint64_t KhSubscription;

typedef struct KhEvents {
    uint32_t size;

    /* Calls `function` with `user` at each event `name` until unsubscribed or the plugin stops; 0 on failure. */
    KhSubscription (*subscribe)(KhPlugin *plugin, const char *name, KhEventFunction function, void *user);

    void (*unsubscribe)(KhPlugin *plugin, KhSubscription subscription);

    /* Raises the plugin's own event `name` (with a dot; the port's names are refused) with `size` bytes of `data`:
     * KH_OK, or KH_ERROR (a port's name, or not on the game's thread). */
    KhResult (*raise)(KhPlugin *plugin, const char *name, const void *data, uint32_t size);
} KhEvents;

#ifdef __cplusplus
}
#endif

#endif
