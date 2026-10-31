/* A scripted mod as the module keeps it (scripts.c), shared by its parts. */
#ifndef KHDAYS_SCRIPTS_MOD_H
#define KHDAYS_SCRIPTS_MOD_H

#include "manifest.h"

#include <stddef.h>
#include <stdint.h>

struct lua_State;

enum KhdaysModState {
    KHDAYS_MOD_OFF,     /* the player turned it off */
    KHDAYS_MOD_LOADED,  /* running */
    KHDAYS_MOD_STOPPED, /* a script error: nothing of it runs until it is reloaded */
    KHDAYS_MOD_PAUSED,  /* over its time allowance: nothing of it runs until resumed or reloaded */
    KHDAYS_MOD_REFUSED  /* not loaded: its manifest, its interface version, its id, its main script */
};

typedef struct KhdaysHandler {
    int event;  /* KhdaysEventId */
    int ref;    /* the function, in the state's registry; LUA_NOREF once removed */
    int handle; /* what mod.on returned */
} KhdaysHandler;

enum KhdaysOptionKind { KHDAYS_OPTION_TOGGLE, KHDAYS_OPTION_CHOICE, KHDAYS_OPTION_NUMBER, KHDAYS_OPTION_BUTTON };

#define KHDAYS_OPTION_LANGUAGES 6 /* [0] for every language, then the game's 1..5 (ui_text.h) */

/* An option a mod adds to the Mods page (mod.option). */
typedef struct KhdaysOption {
    char *key;
    int kind; /* KhdaysOptionKind */
    char *label[KHDAYS_OPTION_LANGUAGES];
    char *description[KHDAYS_OPTION_LANGUAGES];
    char **values; /* a choice's */
    int value_count;
    double low, high, step; /* a number's */
    double fallback;        /* its default */
    double value;           /* a toggle 0 or 1, a choice's 1-based index, a number */
    int ref;                /* on_change, in the state's registry; LUA_NOREF without */
} KhdaysOption;

typedef struct KhdaysMod {
    char *folder; /* its name in mods/ */
    char *path;   /* mods/<folder> */
    char *data;   /* the player's saves folder/mods/<id>: its own files */
    KhdaysManifest manifest;
    unsigned char fingerprint[32];
    int enabled;
    int state; /* KhdaysModState */
    char *reason;
    struct lua_State *L;
    size_t bytes;       /* what its interpreter holds */
    uint64_t spent;     /* ns in its scripts this frame */
    uint64_t entered;   /* when the call into it running now began; 0 outside one */
    uint64_t allowance; /* ns it may spend this frame (or in its main script) */
    int over;           /* past its allowance: its errors can no longer be caught by its own scripts */
    KhdaysHandler *handlers;
    int handler_count, handler_capacity, next_handle;
    KhdaysOption *options;
    int option_count, option_capacity;
    long long stamp;                 /* the newest time among its files when they were last read */
    uint64_t traced_sum, traced_max; /* KHDAYS_TRACE_MODS: its time per frame since the last report */
    unsigned traced_frames;
    struct KhdaysModV2 *v2; /* what the interface's version 2 holds for it (api_v2.h); NULL for api 1 */
} KhdaysMod;

/* The mods, in load order (scripts.c). */
int khdays_scripts_mod_count(void);
KhdaysMod *khdays_scripts_mod(int index);

/* Sets the mod's state and its reason (copied; NULL for none). */
void khdays_mod_set_state(KhdaysMod *mod, int state, const char *reason);

#endif
