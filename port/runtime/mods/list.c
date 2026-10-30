/* The mods the game reads (list.h). */
#include "list.h"

#include "../core/session_log.h"
#include "../scripts/folders.h"
#include "../scripts/manifest.h"
#include "../scripts/order.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IGNORE_FILE ".ignore"

typedef struct Mod {
    KhdaysModEntry entry;
    char *folder, *path;
} Mod;

static Mod *mods;
static int count, capacity;
static KhdaysOrder order;
static char *user_folder;
/* The active mods as the game started (khdays_mods_start), in load order: what it reads until it starts again.
 * Written once, before the game's other threads run, and never changed: any thread may read it. */
static char **started_folders, **started_paths;
static int started_count, started;

static char *copy(const char *s)
{
    const size_t n = strlen(s);
    char *out = malloc(n + 1);
    if (out != NULL) {
        memcpy(out, s, n + 1);
    }
    return out;
}

static char *joined(const char *a, const char *b)
{
    const size_t na = strlen(a), nb = strlen(b);
    char *out = malloc(na + nb + 2);
    if (out != NULL) {
        memcpy(out, a, na);
        out[na] = '/';
        memcpy(out + na + 1, b, nb + 1);
    }
    return out;
}

static void clear(void)
{
#pragma warning(push)
#pragma warning(disable : 6001) /* each of the first `count` mods was written whole by add() */
    for (int i = 0; i < count; ++i) {
        free(mods[i].folder);
        free(mods[i].path);
    }
#pragma warning(pop)
    free(mods);
    mods = NULL;
    count = capacity = 0;
    khdays_order_free(&order);
}

typedef struct Found {
    char **names;
    int count, capacity;
} Found;

static void found_folder(const char *name, void *context)
{
    Found *f = context;
    if (strcmp(name, KHDAYS_DAYS_CACHE) == 0) {
        return; /* the .days mods' copies: each listed by its .days */
    }
    if (f->count == f->capacity) {
        const int grown = f->capacity == 0 ? 8 : f->capacity * 2;
        char **n = realloc(f->names, (size_t)grown * sizeof(*n));
        if (n == NULL) {
            return;
        }
        f->names = n;
        f->capacity = grown;
    }
    f->names[f->count] = copy(name);
    if (f->names[f->count] != NULL) {
        ++f->count;
    }
}

static void found_days(const char *name, void *context)
{
    if (khdays_mods_is_days(name)) {
        found_folder(name, context);
    }
}

static void save(void)
{
    char *path = user_folder != NULL ? joined(user_folder, "mods/order.ini") : NULL;
    char *text = khdays_order_text(&order);
    if (path != NULL && text != NULL) {
        khdays_folders_write(path, text, strlen(text));
    }
    free(path);
    free(text);
}

static void no_name(const char *name, void *context)
{
    (void)name;
    (void)context;
}

/* Whether a mod's folder is a native plugin's alone: no scripts/ folder, and its mod.ini names the plugin's SDK
 * (`sdk`: runtime/plugins/plugins.h). */
static int native_only(const char *folder, const char *ini)
{
    char *scripts = joined(folder, "scripts"), *text, *sdk;
    size_t size = 0;
    const int has_scripts = scripts != NULL && khdays_folders_list(scripts, no_name, NULL);
    free(scripts);
    if (has_scripts || (text = khdays_folders_read(ini, &size)) == NULL) {
        return 0;
    }
    sdk = khdays_manifest_value(text, size, "sdk");
    free(text);
    if (sdk == NULL) {
        return 0;
    }
    free(sdk);
    return 1;
}

/* Where a .days mod's files are read: the port's copy of them (days.h), mods/.days/<its name without .days>. */
static char *days_copy(const char *mods_path, const char *name)
{
    char *cache = joined(mods_path, KHDAYS_DAYS_CACHE), *stem = copy(name), *out = NULL;
    if (cache != NULL && stem != NULL) {
        stem[strlen(stem) - strlen(KHDAYS_DAYS_EXTENSION)] = '\0';
        out = joined(cache, stem);
    }
    free(cache);
    free(stem);
    return out;
}

static int add(const char *mods_path, const KhdaysOrderEntry *o)
{
    Mod *m;
    char *ini, *ignore;
    if (count == capacity) {
        const int grown = capacity == 0 ? 8 : capacity * 2;
        Mod *n = realloc(mods, (size_t)grown * sizeof(*n));
        if (n == NULL) {
            return 0;
        }
        mods = n;
        capacity = grown;
    }
    m = &mods[count];
    memset(m, 0, sizeof(*m));
    m->folder = copy(o->folder);
    m->path = khdays_mods_is_days(o->folder) ? days_copy(mods_path, o->folder) : joined(mods_path, o->folder);
    ini = m->path != NULL ? joined(m->path, "mod.ini") : NULL;
    ignore = m->path != NULL ? joined(m->path, IGNORE_FILE) : NULL;
    if (m->folder == NULL || m->path == NULL || ini == NULL || ignore == NULL) {
        free(m->folder);
        free(m->path);
        free(ini);
        free(ignore);
        return 0;
    }
    m->entry.folder = m->folder;
    m->entry.path = m->path;
    m->entry.scripted = khdays_folders_is_file(ini) && !native_only(m->path, ini);
    m->entry.on = o->on;
    m->entry.ignored = m->folder[0] == '.' || khdays_folders_is_file(ignore);
    free(ini);
    free(ignore);
    ++count;
    return 1;
}

void khdays_mods_scan(const char *mods_path, const char *user)
{
    Found found = {NULL, 0, 0};
    char *order_path, *order_text;
    clear();
    free(user_folder);
    user_folder = user != NULL ? copy(user) : NULL;
    khdays_folders_list(mods_path, found_folder, &found);
    khdays_folders_list_files(mods_path, found_days, &found);
    order_path = user_folder != NULL ? joined(user_folder, "mods/order.ini") : NULL;
    order_text = order_path != NULL ? khdays_folders_read(order_path, NULL) : NULL;
    khdays_order_parse(&order, order_text);
    khdays_order_merge(&order, (const char *const *)found.names, found.count);
    free(order_text);
    free(order_path);
    for (int i = 0; i < found.count; ++i) {
        free(found.names[i]);
    }
    free(found.names);
    for (int i = 0; i < order.count; ++i) {
        if (order.entries[i].present && !add(mods_path, &order.entries[i])) {
            break;
        }
    }
    if (count == 0) {
        khdays_log("mods: none in %s", mods_path);
    }
    for (int i = 0; i < count; ++i) {
        const KhdaysModEntry *e = &mods[i].entry;
        khdays_log("mods: %d. %s: %s%s", i + 1, e->folder,
                   e->ignored ? "off by hand (" IGNORE_FILE ", or a name beginning with a dot)"
                   : e->on    ? "on, its files replace the game's"
                              : "off (order.ini)",
                   e->scripted ? "; scripted" : "");
    }
}

int khdays_mods_count(void)
{
    return count;
}

const KhdaysModEntry *khdays_mods_at(int index)
{
    return index >= 0 && index < count ? &mods[index].entry : NULL;
}

const KhdaysModEntry *khdays_mods_by_folder(const char *folder)
{
    for (int i = 0; folder != NULL && i < count; ++i) {
        if (strcmp(mods[i].folder, folder) == 0) {
            return &mods[i].entry;
        }
    }
    return NULL;
}

int khdays_mods_active(const KhdaysModEntry *entry)
{
    return entry != NULL && entry->on && !entry->ignored;
}

static int order_index(const char *folder)
{
    for (int i = 0; folder != NULL && i < order.count; ++i) {
        if (strcmp(order.entries[i].folder, folder) == 0) {
            return i;
        }
    }
    return -1;
}

static int mod_index(const char *folder)
{
    for (int i = 0; folder != NULL && i < count; ++i) {
        if (strcmp(mods[i].folder, folder) == 0) {
            return i;
        }
    }
    return -1;
}

void khdays_mods_set_on(const char *folder, int on)
{
    const int at = order_index(folder), i = mod_index(folder);
    if (at < 0 || i < 0) {
        return;
    }
    order.entries[at].on = on != 0;
    mods[i].entry.on = on != 0;
    save();
}

void khdays_mods_move(const char *folder, int by)
{
    const int at = order_index(folder), i = mod_index(folder);
    Mod t;
    if (at < 0 || i < 0 || (by != -1 && by != 1) || i + by < 0 || i + by >= count) {
        return;
    }
    khdays_order_move(&order, at, by);
    t = mods[i];
    mods[i] = mods[i + by];
    mods[i + by] = t;
    save();
}

void khdays_mods_start(void)
{
    if (started) {
        return;
    }
    started = 1;
    for (int i = 0; i < count; ++i) {
        started_count += khdays_mods_active(&mods[i].entry);
    }
    started_folders = started_count > 0 ? calloc((size_t)started_count, sizeof(*started_folders)) : NULL;
    started_paths = started_count > 0 ? calloc((size_t)started_count, sizeof(*started_paths)) : NULL;
    if (started_folders == NULL || started_paths == NULL) {
        free(started_folders);
        free(started_paths);
        started_folders = started_paths = NULL;
        started_count = 0;
        return;
    }
    for (int i = 0, n = 0; i < count && n < started_count; ++i) {
        if (khdays_mods_active(&mods[i].entry)) {
            started_folders[n] = copy(mods[i].folder);
            started_paths[n] = copy(mods[i].path);
            ++n;
        }
    }
}

int khdays_mods_started_count(void)
{
    return started_count;
}

const char *khdays_mods_started_folder(int index)
{
    return index >= 0 && index < started_count ? started_folders[index] : NULL;
}

const char *khdays_mods_started_path(int index)
{
    return index >= 0 && index < started_count ? started_paths[index] : NULL;
}

int khdays_mods_started_has(const char *folder)
{
    for (int i = 0; folder != NULL && i < started_count; ++i) {
        if (started_folders[i] != NULL && strcmp(started_folders[i], folder) == 0) {
            return 1;
        }
    }
    return 0;
}
