/* What the settings menu's Mods page shows of the native plugins (shown.h). */
#include "shown.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static SDL_Mutex *lock;
static SDL_InitState once;
static KhdaysPluginsShown published;

static void ready(void)
{
    if (SDL_ShouldInit(&once)) {
        lock = SDL_CreateMutex();
        SDL_SetInitialized(&once, lock != NULL);
    }
}

static char *copy(const char *s)
{
    const size_t n = strlen(s != NULL ? s : "") + 1;
    char *out = malloc(n);
    if (out != NULL) {
        memcpy(out, s != NULL ? s : "", n);
    }
    return out;
}

/* `out` a copy of `in`, its strings its own; no plugin at all when memory runs out. */
static void copy_shown(KhdaysPluginsShown *out, const KhdaysPluginsShown *in)
{
    out->serial = in->serial;
    out->safe_mode = in->safe_mode;
    out->plugins = in->count > 0 ? calloc((size_t)in->count, sizeof(*out->plugins)) : NULL;
    out->count = out->plugins != NULL ? in->count : 0;
    for (int i = 0; i < out->count; ++i) {
        const KhdaysPluginShown *a = &in->plugins[i];
        KhdaysPluginShown *b = &out->plugins[i];
        b->name = copy(a->name);
        b->version = copy(a->version);
        b->author = copy(a->author);
        b->why = copy(a->why);
        b->state = a->state;
    }
}

void khdays_plugins_shown_free(KhdaysPluginsShown *view)
{
#pragma warning(push)
#pragma warning(disable : 6001) /* every view comes from calloc and copy_shown: each string NULL or its own */
    for (int i = 0; i < view->count; ++i) {
        free(view->plugins[i].name);
        free(view->plugins[i].version);
        free(view->plugins[i].author);
        free(view->plugins[i].why);
    }
#pragma warning(pop)
    free(view->plugins);
    view->plugins = NULL;
    view->count = 0;
}

void khdays_plugins_shown_set(const KhdaysPluginsShown *now)
{
    unsigned serial;
    ready();
    SDL_LockMutex(lock);
    serial = published.serial + 1 != 0 ? published.serial + 1 : 1;
    khdays_plugins_shown_free(&published);
    copy_shown(&published, now);
    published.serial = serial;
    SDL_UnlockMutex(lock);
}

int khdays_plugins_shown(KhdaysPluginsShown *view)
{
    int newer;
    ready();
    SDL_LockMutex(lock);
    newer = published.serial != view->serial;
    if (newer) {
        khdays_plugins_shown_free(view);
        copy_shown(view, &published);
    }
    SDL_UnlockMutex(lock);
    return newer;
}
