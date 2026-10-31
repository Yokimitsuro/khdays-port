/* kh.engine for native plugins (sdk/include/kh/kh_engine.h): the game's memory by its references, its routines and
 * data by their decompilation names (the tables prepare.py generates, ds_names.c), its modules and frames. */
#include "interfaces.h"

#include "../core/events.h"
#include "../core/window.h"

#include <kh/kh_engine.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int khdays_overlay_loaded(int id); /* runtime/core/overlays.c */

/* The build's tables, each sorted by name (ds_names.c). */
typedef struct {
    const char *name;
    void (*native)(void);
    int module;
} KhdaysRoutineName;
typedef struct {
    const char *name;
    unsigned reference;
} KhdaysDatumName;
extern const KhdaysRoutineName khdays_routine_names[];
extern const unsigned khdays_routine_name_count;
extern const KhdaysDatumName khdays_datum_names[];
extern const unsigned khdays_datum_name_count;

/* The game's memory a plugin may reach: its RAM (main RAM, the heaps: window.h), the ITCM and the DTCM. */
static int in_memory(KhRef ref)
{
    return khdays_game_ram(ref) || (ref >= 0x01ff8000u && ref < 0x02000000u) ||
           (ref >= 0x027e0000u && ref < 0x027e4000u);
}

static void *pointer(KhRef ref)
{
    return in_memory(ref) ? KHDAYS_W(ref) : NULL;
}

static KhRef reference(const void *address)
{
    const unsigned char *a = address;
    if (a == NULL || a < khdays_window || (unsigned long long)(a - khdays_window) > 0xffffffffull) {
        return 0;
    }
    return in_memory((KhRef)(a - khdays_window)) ? (KhRef)(a - khdays_window) : 0;
}

static int by_routine_name(const void *key, const void *entry)
{
    return strcmp(key, ((const KhdaysRoutineName *)entry)->name);
}

static int by_datum_name(const void *key, const void *entry)
{
    return strcmp(key, ((const KhdaysDatumName *)entry)->name);
}

static void *routine(const char *name, int32_t *module)
{
    const KhdaysRoutineName *r =
        name != NULL ? bsearch(name, khdays_routine_names, khdays_routine_name_count, sizeof(*r), by_routine_name)
                     : NULL;
    if (r == NULL) {
        return NULL;
    }
    if (module != NULL) {
        *module = r->module;
    }
    return (void *)r->native;
}

static KhRef datum(const char *name)
{
    const KhdaysDatumName *d =
        name != NULL ? bsearch(name, khdays_datum_names, khdays_datum_name_count, sizeof(*d), by_datum_name) : NULL;
    return d != NULL ? d->reference : 0;
}

static int32_t module_loaded(int32_t module)
{
    return module == KH_MODULE_MAIN || module == KH_MODULE_ITCM || (module >= 0 && khdays_overlay_loaded(module));
}

static uint32_t frame(void)
{
    return khdays_events_vblank_count();
}

static int32_t on_game_thread(void)
{
    return khdays_plugins_on_game_thread();
}

/* The game's routines a plugin may replace: one record each in section .khrt$m (compat/khdays_references.h,
 * references/emit.py routed), sorted by name the first time one is asked for. */
typedef struct {
    const char *name;
    void **route;
    void *body;
    void *unused;
} RouteRecord;

#pragma section(".khrt$a", read)
#pragma section(".khrt$z", read)
__declspec(allocate(".khrt$a")) static const RouteRecord first_route = {0};
__declspec(allocate(".khrt$z")) static const RouteRecord last_route = {0};

static RouteRecord *routes;
static size_t route_count;

static int by_route_name(const void *a, const void *b)
{
    return strcmp(((const RouteRecord *)a)->name, ((const RouteRecord *)b)->name);
}

static const RouteRecord *route_named(const char *name)
{
    if (routes == NULL) {
        size_t n = 0;
        for (const RouteRecord *r = &first_route + 1; r < &last_route; ++r) {
            n += r->name != NULL;
        }
        routes = malloc((n ? n : 1) * sizeof(RouteRecord));
        if (routes == NULL) {
            return NULL;
        }
        for (const RouteRecord *r = &first_route + 1; r < &last_route; ++r) {
            if (r->name != NULL) {
                routes[route_count++] = *r;
            }
        }
        qsort(routes, route_count, sizeof(RouteRecord), by_route_name);
    }
    const RouteRecord key = {name, NULL, NULL, NULL};
    return bsearch(&key, routes, route_count, sizeof(RouteRecord), by_route_name);
}

/* The replacements made, oldest first: whose, the route, what it put there and what the calls reached before. While
 * its plugin may not change the game (in a room, its mod not made for rooms: as its hooks are skipped) one is put
 * back, and given again after; a plugin's are put back as it stops, before its library goes. Each only while the
 * route holds what it left there (another plugin's replacement on top of it stays). */
typedef struct {
    KhPlugin *owner;
    void **route;
    void *function, *previous;
    int suspended;
} Replacement;

static Replacement *replacements;
static size_t replacement_count, replacement_room;

/* `text` in the calling plugin's mod's log and the game's. */
static void say(const char *text)
{
    KhPlugin *plugin = khdays_plugins_called();
    if (plugin != NULL) {
        khdays_plugins_tell(plugin, text);
    }
    fprintf(stderr, "plugins: %s\n", text);
}

static KhResult replace(const char *name, void *function, void **previous)
{
    char text[200];
    if (!khdays_plugins_on_game_thread()) {
        snprintf(text, sizeof(text), "kh.engine replace %.80s refused: not on the game's thread", name ? name : "-");
        say(text);
        return KH_ERROR;
    }
    const RouteRecord *r = name != NULL && function != NULL ? route_named(name) : NULL;
    if (r == NULL) {
        snprintf(text, sizeof(text), "kh.engine replace %.80s: no routine of the game's that can be replaced by that name",
                 name ? name : "-");
        say(text);
        return KH_ERROR;
    }
    if (replacement_count == replacement_room) {
        const size_t room = replacement_room ? replacement_room * 2 : 16;
        Replacement *grown = realloc(replacements, room * sizeof(Replacement));
        if (grown == NULL) {
            return KH_ERROR;
        }
        replacements = grown;
        replacement_room = room;
    }
    Replacement *made = &replacements[replacement_count++];
    made->owner = khdays_plugins_called();
    made->route = r->route;
    made->function = function;
    made->previous = *r->route;
    made->suspended = !khdays_plugins_may_change(made->owner);
    if (previous != NULL) {
        *previous = made->previous;
    }
    if (!made->suspended) {
        *r->route = function;
    }
    return KH_OK;
}

void khdays_plugins_engine_frame(void)
{
    for (size_t i = 0; i < replacement_count; ++i) {
        Replacement *m = &replacements[i];
        const int suspend = !khdays_plugins_may_change(m->owner);
        if (suspend && !m->suspended && *m->route == m->function) {
            *m->route = m->previous;
        } else if (!suspend && m->suspended && *m->route == m->previous) {
            *m->route = m->function;
        }
        m->suspended = suspend;
    }
}

void khdays_plugins_engine_forget(KhPlugin *plugin)
{
    for (size_t i = replacement_count; i-- > 0;) {
        Replacement *m = &replacements[i];
        if (m->owner != plugin) {
            continue;
        }
        if (!m->suspended && *m->route == m->function) {
            *m->route = m->previous;
        }
        memmove(m, m + 1, (replacement_count - i - 1) * sizeof(Replacement));
        --replacement_count;
    }
}

static const KhEngine engine = {sizeof(KhEngine), pointer, reference,      routine, datum,
                                module_loaded,    frame,   on_game_thread, replace};

const void *khdays_plugins_engine(void)
{
    return &engine;
}
