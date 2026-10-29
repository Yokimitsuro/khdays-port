/* kh.engine for native plugins (sdk/include/kh/kh_engine.h): the game's memory by its references, its routines and
 * data by their decompilation names (the tables prepare.py generates, ds_names.c), its modules and frames. */
#include "interfaces.h"

#include "../core/events.h"
#include "../core/window.h"

#include <kh/kh_engine.h>

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

static const KhEngine engine = {sizeof(KhEngine), pointer, reference,     routine, datum,
                                module_loaded,    frame,   on_game_thread};

const void *khdays_plugins_engine(void)
{
    return &engine;
}
