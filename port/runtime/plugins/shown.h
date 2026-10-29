/* What the settings menu's Mods page shows of the native plugins: a copy the game's thread makes once their states
 * settle (after the start, after the stop), taken by the window's thread. */
#ifndef KHDAYS_PLUGINS_SHOWN_H
#define KHDAYS_PLUGINS_SHOWN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysPluginShown {
    char *name, *version, *author, *why; /* its mod's; why it is not running ("" none) */
    int state;                           /* KhdaysPluginState (plugins.h) */
} KhdaysPluginShown;

typedef struct KhdaysPluginsShown {
    unsigned serial; /* 0: never taken */
    int safe_mode;   /* the game started in safe mode: none is loaded, whatever the setting */
    KhdaysPluginShown *plugins;
    int count;
} KhdaysPluginsShown;

/* The game's thread: the plugins as they are now (the strings are copied). */
void khdays_plugins_shown_set(const KhdaysPluginsShown *now);

/* The window's thread: replaces `view` by the latest copy when it is newer than `view->serial`. Returns 1 when it
 * did. */
int khdays_plugins_shown(KhdaysPluginsShown *view);

void khdays_plugins_shown_free(KhdaysPluginsShown *view);

#ifdef __cplusplus
}
#endif

#endif
