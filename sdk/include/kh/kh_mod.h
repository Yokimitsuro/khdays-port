/* KH Mod SDK: the core of the boundary between the khdays port and a mod's native plugin.
 *
 * A plugin is a native library (.dll on Windows, .so on Linux) a mod carries, which the port loads once the player
 * has turned native code on. It exports one function, kh_plugin_query, returning its descriptor; the port checks
 * the descriptor's ABI version, then calls its start function with the host table, and its stop function before
 * releasing it.
 *
 * Everything that crosses the boundary is plain C: fixed-size integers, UTF-8 strings, opaque handles, and structs
 * of function pointers whose first field is their size (they only grow by appending). C++ stays inside a plugin:
 * no exception, no standard-library type, no RTTI crosses it -- a plugin catches its own exceptions.
 *
 * Threads: start, stop and every callback run on the game's thread unless an interface says otherwise. A plugin
 * runs inside the game's process and is not isolated from it: it can crash the game or corrupt its state. */
#ifndef KH_MOD_H
#define KH_MOD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The ABI this header describes. The port loads a plugin built for its own major and a minor not above its own. */
#define KH_ABI_MAJOR 1
#define KH_ABI_MINOR 0

#if defined(_WIN32)
#define KH_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define KH_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define KH_PLUGIN_EXPORT
#endif

typedef int32_t KhResult;
#define KH_OK 0
#define KH_ERROR (-1)       /* failed: the plugin's log line says why */
#define KH_UNSUPPORTED (-2) /* asked for something this port does not have */

typedef enum KhLogLevel { KH_LOG_INFO = 0, KH_LOG_WARNING = 1, KH_LOG_ERROR = 2 } KhLogLevel;

/* This plugin, for the port (opaque). */
typedef struct KhPlugin KhPlugin;

/* What the port gives a plugin at start. Read a field only when `size` covers it (an older port has fewer). */
typedef struct KhHost {
    uint32_t size; /* bytes of this struct as the port has it */
    uint16_t abi_major, abi_minor;
    KhPlugin *plugin;         /* this plugin's handle, for the calls below */
    const char *port_version; /* the port's version, "0.1.4" */

    /* A line in the mods' log and the session log, under the plugin's mod. */
    void (*log)(KhPlugin *plugin, KhLogLevel level, const char *text);

    /* The port's interface `name` ("kh.events", ...) at `version`, a struct whose first field is its size; NULL
     * when this port has not got it. The pointer stays valid until the plugin's stop returns. */
    const void *(*find)(KhPlugin *plugin, const char *name, uint32_t version);
} KhHost;

/* What a plugin is: returned by kh_plugin_query, valid while the library is loaded (a static object). */
typedef struct KhPluginDescriptor {
    uint32_t size; /* sizeof(KhPluginDescriptor) as the plugin was built */
    uint16_t abi_major, abi_minor;
    const char *name; /* for the logs */

    /* Called once after loading, with the host table (valid until stop returns). KH_OK to go on; anything else and
     * the plugin is stopped no further and released (what it registered is undone). */
    KhResult (*start)(const KhHost *host);

    /* Called once before release: no call of the plugin's comes after it. */
    void (*stop)(void);
} KhPluginDescriptor;

/* The one function a plugin exports. */
#define KH_PLUGIN_QUERY "kh_plugin_query"
typedef const KhPluginDescriptor *(*KhPluginQuery)(void);

/* A plugin's descriptor filled for this header's ABI. */
#define KH_PLUGIN_DESCRIPTOR(plugin_name, start_function, stop_function)                                               \
    {sizeof(KhPluginDescriptor), KH_ABI_MAJOR, KH_ABI_MINOR, (plugin_name), (start_function), (stop_function)}

#ifdef __cplusplus
}
#endif

#endif
