/* KH Mod SDK: hooks (kh.hooks) -- running before and after the game's own routines, or instead of them.
 *
 * The port places hook points at named routines of the game (the decompilation's names, listed by the interface:
 * point_count, point_name, point_signature). A plugin adds a function before or after a point, with a priority:
 * before-functions run highest priority first, and may change the routine's arguments, or skip the routine giving
 * its result themselves (the before-functions after one that skips are not called); after-functions run lowest
 * priority first, and may change the result. A point no plugin hooks costs the game one test.
 *
 * Arguments and results are 64-bit slots: integers as the routine's C types widened (signed ones sign-extended),
 * the game's pointers as references (KhRef in kh_engine.h, which turns them into host addresses).
 *
 * Two plugins that each declare they replace the same point (KH_HOOK_REPLACES) are told in the mods' log: the
 * higher priority wins, the other's function is still called when the winner does not skip.
 *
 * The catalogue of points grows with what plugins need; ask for one (docs/MODDING_NATIVE.md). Hook points are
 * placed where the game is built, never by patching its code while it runs. */
#ifndef KH_HOOKS_H
#define KH_HOOKS_H

#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_HOOKS "kh.hooks"
#define KH_HOOKS_VERSION 1

typedef enum KhHookWhen { KH_HOOK_BEFORE = 0, KH_HOOK_AFTER = 1 } KhHookWhen;

/* Flags of a hook. */
#define KH_HOOK_REPLACES 1u /* its before-function means to skip the routine (conflicts with another's told) */

typedef struct KhHookCall {
    uint32_t size;
    const char *point;  /* the hook point's name */
    uint32_t arg_count; /* the routine's arguments */
    uint64_t *args;     /* changeable by before-functions */
    uint64_t result;    /* the routine's result (after it, or as a skipping before-function sets it); 0 for void */
    int32_t skip;       /* a before-function sets 1: the routine is not called, `result` is its result */
    int32_t has_result; /* 0 for a routine that returns nothing */
} KhHookCall;

typedef void (*KhHookFunction)(KhHookCall *call, void *user);

/* A hook: 0 is none. */
typedef uint64_t KhHook;

typedef struct KhHooks {
    uint32_t size;

    /* Adds `function` (with `user`) before or after point `point`, at `priority`, with `flags`; 0 when there is no
     * such point (told). Removed by remove() or when the plugin stops. */
    KhHook (*add)(KhPlugin *plugin, const char *point, KhHookWhen when, int32_t priority, uint32_t flags,
                  KhHookFunction function, void *user);

    void (*remove)(KhPlugin *plugin, KhHook hook);

    /* The catalogue: how many points, each one's name and its C signature ("void Ov022_ActorSetHp(ref pEnt, int
     * nValue)"). */
    uint32_t (*point_count)(void);
    const char *(*point_name)(uint32_t index);
    const char *(*point_signature)(uint32_t index);
} KhHooks;

#ifdef __cplusplus
}
#endif

#endif
