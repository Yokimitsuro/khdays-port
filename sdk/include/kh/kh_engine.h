/* KH Mod SDK: the engine itself -- the game's memory, its routines and its data by name (kh.engine).
 *
 * This is the advanced level: it reaches everything the game is, with nothing in between. What it gives is the
 * game's own (the decompilation's names, its memory's layout), which changes when the decompilation does: a plugin
 * built on it may need rebuilding, or changing, for a later port. The port checks nothing a plugin does with it.
 *
 * The game's memory: the game keeps its pointers as 4-byte references (KhRef, the addresses the DS gave them);
 * pointer() turns one into the host's address, reference() the other way. Its routines are compiled C: call one
 * with its decompilation's prototype, pointers passed as references (KhRef), on the game's thread. An overlay's
 * routines are always there, but its data and its state only while it is loaded (module_loaded). */
#ifndef KH_ENGINE_H
#define KH_ENGINE_H

#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_ENGINE "kh.engine"
#define KH_ENGINE_VERSION 2 /* 2 added replace; a plugin may still ask for 1 */

/* A reference into the game's memory. */
typedef uint32_t KhRef;

/* Modules as the decompilation numbers them: the main module, the ITCM, then the overlays by number. */
#define KH_MODULE_MAIN (-1)
#define KH_MODULE_ITCM (-2)

typedef struct KhEngine {
    uint32_t size;

    /* The host's address of reference `ref`; NULL when it is not in the game's memory. */
    void *(*pointer)(KhRef ref);

    /* The reference of a host address inside the game's memory; 0 outside it. */
    KhRef (*reference)(const void *address);

    /* The game's routine `name` (the decompilation's), its module in *module when not NULL; NULL when the build
     * has none of that name. The pointer is the compiled function: cast it to its prototype. Calling it reaches
     * whatever replaces the routine now (replace). */
    void *(*routine)(const char *name, int32_t *module);

    /* The game's datum `name` (the decompilation's: a global, a table), its reference; 0 when there is none. */
    KhRef (*datum)(const char *name);

    /* Whether module `module` (KH_MODULE_MAIN, KH_MODULE_ITCM or an overlay's number) is loaded now. */
    int32_t (*module_loaded)(int32_t module);

    /* The frames since the game started (60 a second), and whether the caller is on the game's thread. */
    uint32_t (*frame)(void);
    int32_t (*on_game_thread)(void);

    /* Version 2. Every call of the game's routine `name` -- one of the game's own code (src/), not a static one,
     * not one taking a variable number of arguments -- goes to `function` from now on: the game's own calls, those
     * through a function reference, and routine(name)'s. `function` has the routine's prototype; *previous (when
     * not NULL) is what the calls reached before -- the game's own body, or another plugin's replacement -- for
     * `function` to call on to. Replacing with *previous puts it back. The port puts it back itself as the plugin
     * stops, and while in a room when the mod is not made for rooms (as its hooks are skipped there). On the game's
     * thread. KH_OK, else KH_ERROR (no such routine, one that cannot be replaced, or another thread: the mod's log
     * says which). */
    KhResult (*replace)(const char *name, void *function, void **previous);
} KhEngine;

#ifdef __cplusplus
}
#endif

#endif
