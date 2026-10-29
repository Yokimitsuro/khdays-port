/* The mods' native plugins (the KH Mod SDK: sdk/include/kh/kh_mod.h): a mod whose mod.ini names its SDK (`sdk`) and
 * a library for this platform (`native.<platform>`) has a plugin. Once the player has turned native code on
 * ([mods] native_code) and the game is not in safe mode, each is checked (its file, its SDK, the mods it
 * `requires`), loaded through SDL3, asked for its descriptor, started; and stopped on the game's thread before its
 * library is released. A plugin runs inside the game and is not isolated from it. docs/MODDING_NATIVE.md. */
#ifndef KHDAYS_PLUGINS_H
#define KHDAYS_PLUGINS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum KhdaysPluginState {
    KHDAYS_PLUGIN_WAITING, /* native code is off, or safe mode: never loaded */
    KHDAYS_PLUGIN_REFUSED, /* not loaded: `why` */
    KHDAYS_PLUGIN_FAILED,  /* loaded, but its start failed: released */
    KHDAYS_PLUGIN_STARTED,
    KHDAYS_PLUGIN_STOPPED /* stopped and released */
} KhdaysPluginState;

/* A plugin as the port knows it (strings owned by the plugin manager). */
typedef struct KhdaysPluginView {
    const char *folder;  /* its mod's folder in mods/ */
    const char *mod_id;  /* its mod's id (its folder's name without one) */
    const char *name;    /* its mod's name */
    const char *version; /* its mod's version */
    const char *author;  /* its mod's declared author ("" none) */
    const char *library; /* its library for this platform, in its mod */
    int state;           /* KhdaysPluginState */
    const char *why;     /* refused or failed: why ("" otherwise) */
} KhdaysPluginView;

/* Where the plugins' lines go: the mods' log and the session log in the game (runtime.c), a test's own in tests. */
typedef void (*KhdaysPluginsLog)(const char *mod_id, const char *text);

/* The platform as mod.ini's `native.<platform>` names it: "windows-x64", "linux-x64", "linux-arm64". */
const char *khdays_plugins_platform(void);

/* Whether native code may run this time: the setting ([mods] native_code), before khdays_plugins_start. */
void khdays_plugins_set_native_code(int on);

/* Which mod folders count, by their names, before khdays_plugins_start: a mod off on the Mods page has no plugin (the
 * game's: runtime/mods/list.h's mods on as it started). Not set, every folder. */
void khdays_plugins_mods_on(int (*on)(const char *folder));

/* Safe mode (KHDAYS_SAFE_MODE=1 or --safe-mode): no plugin is loaded whatever the setting. */
void khdays_plugins_set_safe_mode(int on);

/* Finds the plugins of the mods in `mods` (a folder of mod folders) and, when native code may run, loads and starts
 * them, the mods they require first; their lines and the port's about them through `log`. On the game's thread. */
void khdays_plugins_start(const char *mods, KhdaysPluginsLog log);

/* The game's thread, once a frame: stops the plugins when khdays_plugins_quit asked for it. */
void khdays_plugins_frame(void);

/* Any thread but the game's, as the game closes: asks the game's thread to stop the plugins and waits for it up to
 * `ms` milliseconds (the game's thread may be held). */
void khdays_plugins_quit(int ms);

/* Stops every started plugin (each stop called, the last started first) and releases its library. On the game's
 * thread. */
void khdays_plugins_stop(void);

/* The game's events for the plugins (kh.events): whether any plugin listens; one raised (its name as
 * sdk/include/kh/kh_events.h lists them, its values). From the Lua mods' event bus (runtime/scripts). */
int khdays_plugins_listening(void);
void khdays_plugins_game_event(const char *name, long long a, long long b, long long c, long long d, const char *text);

/* The game's pad read (runtime/input/input.c), once a game frame, after the port's own screens took their buttons:
 * the player's buttons (KHDAYS_KEY_*) given to the plugins (kh.input), those they hide taken out. The buttons hidden
 * now (the analog stick's movement stops while all four directions are). */
unsigned khdays_plugins_buttons(unsigned buttons);
unsigned khdays_plugins_hidden_buttons(void);

/* For the multiplayer rooms (runtime/scripts/scripts.c): started plugin `index` (from 0; 0 past the last) -- its part
 * of the gameplay mods' fingerprint (SHA-256 over its library and its parameters' files), its mod's name and
 * version. A message the mods share (net_session.h) given to the plugins: one of theirs (kh.net) is heard as an
 * event, any other left alone. */
int khdays_plugins_room_entry(int index, unsigned char digest[32], const char **name, const char **version);
void khdays_plugins_shared(const unsigned char *data, unsigned size);

/* The plugins found, in the order they were found (their folders' names). */
int khdays_plugins_count(void);
const KhdaysPluginView *khdays_plugins_at(int index);

#ifdef __cplusplus
}
#endif

#endif
