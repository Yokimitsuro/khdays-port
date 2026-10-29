/* KH Mod SDK: services (kh.services) -- interfaces plugins publish for one another.
 *
 * A plugin publishes a table of its functions under a name and a version; any plugin finds it with the host
 * table's find(), as it finds the port's own interfaces. So a kind of mod the SDK never thought of -- a new magic
 * system, a camera framework, a quest engine -- is one plugin's service that others build on, with no change to
 * the SDK. Name services "<mod id>.<name>"; the "kh." names are the port's.
 *
 * A table begins with its size (uint32_t) and only grows at its end within a version; a plugin keeps answering the
 * versions it published. A plugin that needs another's service lists that mod in its mod.ini's `requires`, so that
 * the other starts first. A plugin's services are withdrawn when it stops: tables found from it must not be used
 * after its stop (a plugin that requires it stops first). */
#ifndef KH_SERVICES_H
#define KH_SERVICES_H

#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_SERVICES "kh.services"
#define KH_SERVICES_VERSION 1

typedef struct KhServices {
    uint32_t size;

    /* Publishes `table` as `name` at `version` until withdrawn or the plugin stops: KH_OK, or KH_ERROR when that
     * name and version are someone's already (the port's log names the owner). `table` stays the plugin's. */
    KhResult (*publish)(KhPlugin *plugin, const char *name, uint32_t version, const void *table);

    /* Withdraws one of the plugin's services. */
    void (*withdraw)(KhPlugin *plugin, const char *name, uint32_t version);
} KhServices;

#ifdef __cplusplus
}
#endif

#endif
