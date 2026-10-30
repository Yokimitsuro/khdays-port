/* The KH DS Project Editor's mods (days.h). */
#include "days.h"
#include "list.h"
#include "zip_read.h"

#include "../core/session_log.h"
#include "../core/sha256.h"
#include "../scripts/folders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARKER "unpacked.txt" /* in a copy: its .days's name, size and time, and the files written */
#define ROOT "root/"          /* the archive's folder of the game's files */
#define MANIFEST "mod.yml"

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

static char *copy_n(const char *s, size_t n)
{
    char *out = malloc(n + 1);
    if (out != NULL) {
        memcpy(out, s, n);
        out[n] = '\0';
    }
    return out;
}

/* ---- mod.yml ------------------------------------------------------------------------------------ */

typedef struct Asset {
    char *path;                  /* root/<the game's path> */
    char hash[65], original[65]; /* SHA-256 in hex: the mod's file, the one of the game it replaces */
} Asset;

typedef struct Manifest {
    char *title, *author;
    Asset *assets;
    int count, capacity;
} Manifest;

/* A line's value after its `key:`, its spaces and quotes taken off. */
static char *value_after(const char *colon, const char *end)
{
    const char *a = colon + 1;
    while (a < end && (*a == ' ' || *a == '\t')) {
        ++a;
    }
    while (end > a && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        --end;
    }
    if (end - a >= 2 && (*a == '"' || *a == '\'') && end[-1] == *a) {
        ++a;
        --end;
    }
    return copy_n(a, (size_t)(end - a));
}

static void set_hex(char out[65], const char *value)
{
    size_t n = 0;
    for (; value != NULL && value[n] != '\0' && n < 64; ++n) {
        const char c = value[n];
        out[n] = c >= 'A' && c <= 'F' ? (char)(c - 'A' + 'a') : c;
    }
    out[n] = '\0';
}

/* The title, the author and the assets' paths and hashes; what it does not know is left out. */
static void read_manifest(const char *text, size_t size, Manifest *m)
{
    memset(m, 0, sizeof(*m));
    for (const char *line = text; line < text + size;) {
        const char *end = memchr(line, '\n', (size_t)(text + size - line));
        end = end != NULL ? end : text + size;
        const char *a = line;
        while (a < end && (*a == ' ' || *a == '\t')) {
            ++a;
        }
        const int top = a == line, item = end - a >= 2 && a[0] == '-' && a[1] == ' ';
        a += item ? 2 : 0;
        const char *colon = memchr(a, ':', (size_t)(end - a));
        if (colon != NULL && *a != '#') {
            const size_t key = (size_t)(colon - a);
            char *value = value_after(colon, end);
            if (top && key == 5 && memcmp(a, "title", 5) == 0 && m->title == NULL) {
                m->title = value;
                value = NULL;
            } else if (top && key == 6 && memcmp(a, "author", 6) == 0 && m->author == NULL) {
                m->author = value;
                value = NULL;
            } else if (item && key == 4 && memcmp(a, "path", 4) == 0) {
                if (m->count == m->capacity) {
                    const int grown = m->capacity == 0 ? 16 : m->capacity * 2;
                    Asset *n = realloc(m->assets, (size_t)grown * sizeof(*n));
                    if (n != NULL) {
                        m->assets = n;
                        m->capacity = grown;
                    }
                }
                if (m->count < m->capacity && value != NULL) {
                    memset(&m->assets[m->count], 0, sizeof(Asset));
                    m->assets[m->count++].path = value;
                    value = NULL;
                }
            } else if (!top && m->count > 0 && key == 4 && memcmp(a, "hash", 4) == 0) {
                set_hex(m->assets[m->count - 1].hash, value);
            } else if (!top && m->count > 0 && key == 12 && memcmp(a, "originalHash", 12) == 0) {
                set_hex(m->assets[m->count - 1].original, value);
            }
            free(value);
        }
        line = end + 1;
    }
}

static void free_manifest(Manifest *m)
{
#pragma warning(push)
#pragma warning(disable : 6001) /* each of the first `count` assets was written whole by read_manifest */
    for (int i = 0; i < m->count; ++i) {
        free(m->assets[i].path);
    }
#pragma warning(pop)
    free(m->assets);
    free(m->title);
    free(m->author);
    memset(m, 0, sizeof(*m));
}

static const Asset *asset_named(const Manifest *m, const char *path)
{
    for (int i = 0; i < m->count; ++i) {
        if (strcmp(m->assets[i].path, path) == 0) {
            return &m->assets[i];
        }
    }
    return NULL;
}

static void sha256_hex(const void *data, size_t size, char out[65])
{
    unsigned char d[32];
    khdays_sha256(data, size, d);
    for (int i = 0; i < 32; ++i) {
        snprintf(out + 2 * i, 3, "%02x", d[i]);
    }
}

/* ---- A copy's files ---------------------------------------------------------------------------- */

/* Whether an archive's entry stays inside the copy: no folder above, no drive, no backslash. */
static int safe_name(const char *name)
{
    if (name[0] == '\0' || name[0] == '/' || strchr(name, '\\') != NULL || strchr(name, ':') != NULL) {
        return 0;
    }
    for (const char *part = name; *part != '\0';) {
        const char *slash = strchr(part, '/');
        const size_t n = slash != NULL ? (size_t)(slash - part) : strlen(part);
        if (n == 0 || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) {
            return slash == NULL && n == 0; /* (a folder's trailing slash) */
        }
        part += n + (slash != NULL ? 1 : 0);
    }
    return 1;
}

/* Where an entry goes in the copy: root/<path> into files/<path>, the archive's own files (mod.yml, its icon)
 * beside them; NULL for a folder or anything else. To free(). */
static char *place_of(const char *name)
{
    const size_t n = strlen(name);
    if (n == 0 || name[n - 1] == '/') {
        return NULL;
    }
    if (strncmp(name, ROOT, strlen(ROOT)) == 0) {
        return joined("files", name + strlen(ROOT));
    }
    return strchr(name, '/') == NULL ? copy_n(name, n) : NULL;
}

static int slashes(const char *path)
{
    int n = 0;
    for (const char *c = path; c != NULL && *c != '\0'; ++c) {
        n += *c == '/';
    }
    return n;
}

/* Takes away the files a copy's marker lists (its `file` lines), then the folders they leave empty, deepest first. */
static void remove_listed(const char *copy, const char *marker_text)
{
    char **folders = NULL;
    int folder_count = 0, folder_capacity = 0;
    for (const char *line = marker_text; line != NULL && *line != '\0';) {
        const char *end = strchr(line, '\n');
        const size_t n = end != NULL ? (size_t)(end - line) : strlen(line);
        if (n > 5 && strncmp(line, "file ", 5) == 0) {
            char *rel = copy_n(line + 5, n - 5), *path = rel != NULL ? joined(copy, rel) : NULL;
            if (path != NULL && safe_name(rel)) {
                khdays_folders_remove(path);
                for (char *slash = strrchr(rel, '/'); slash != NULL; slash = strrchr(rel, '/')) {
                    *slash = '\0';
                    if (folder_count == folder_capacity) {
                        const int grown = folder_capacity == 0 ? 16 : folder_capacity * 2;
                        char **f = realloc(folders, (size_t)grown * sizeof(*f));
                        if (f == NULL) {
                            break;
                        }
                        folders = f;
                        folder_capacity = grown;
                    }
                    folders[folder_count++] = joined(copy, rel);
                }
            }
            free(rel);
            free(path);
        }
        line = end != NULL ? end + 1 : NULL;
    }
    /* deepest first: a folder's own folders gone before it */
    int deepest = 0;
    for (int i = 0; i < folder_count; ++i) {
        deepest = slashes(folders[i]) > deepest ? slashes(folders[i]) : deepest;
    }
    for (int depth = deepest; depth >= 0; --depth) {
        for (int i = 0; i < folder_count; ++i) {
            if (folders[i] != NULL && slashes(folders[i]) == depth) {
                khdays_folders_remove(folders[i]);
            }
        }
    }
    for (int i = 0; i < folder_count; ++i) {
        free(folders[i]);
    }
    free(folders);
}

/* ---- Copying a .days out ----------------------------------------------------------------------- */

typedef struct Unpack {
    const char *name; /* the .days's, in the mods folder */
    KhdaysZip zip;
    Manifest manifest;
    char why[300];
} Unpack;

/* Every entry read and checked against mod.yml's hash, before anything is written. */
static int check_entries(Unpack *u)
{
    for (int i = 0; i < u->zip.count; ++i) {
        const char *name = u->zip.entries[i].name;
        char *place;
        if (!safe_name(name)) {
            snprintf(u->why, sizeof(u->why), "an entry named outside the mod (%.80s)", name);
            return 0;
        }
        if ((place = place_of(name)) == NULL) {
            continue;
        }
        free(place);
        const char *why = "";
        size_t size = 0;
        unsigned char *data = khdays_zip_extract(&u->zip, i, &size, &why);
        if (data == NULL) {
            snprintf(u->why, sizeof(u->why), "%.80s: %s", name, why);
            return 0;
        }
        const Asset *a = asset_named(&u->manifest, name);
        if (a != NULL && a->hash[0] != '\0') {
            char hex[65];
            sha256_hex(data, size, hex);
            if (strcmp(hex, a->hash) != 0) {
                snprintf(u->why, sizeof(u->why), "%.80s is not the file mod.yml names (its hash differs)", name);
                free(data);
                return 0;
            }
        }
        free(data);
    }
    return 1;
}

/* The entries written into `copy`, the marker last (its `source` line `source`). */
static int write_entries(Unpack *u, const char *copy, const char *source)
{
    size_t text_n = 0, text_capacity = 256;
    char *text = malloc(text_capacity);
    int files = 0, ok = text != NULL;
    if (ok) {
        text_n = (size_t)snprintf(text, text_capacity,
                                  "# The port's copy of mods/%s's files (runtime/mods/days.c), made again when it "
                                  "changes.\nname %s\n%s\n",
                                  u->name, u->name, source);
        ok = text_n < text_capacity;
    }
    for (int i = 0; ok && i < u->zip.count; ++i) {
        char *place = place_of(u->zip.entries[i].name), *path = place != NULL ? joined(copy, place) : NULL;
        const char *why = "";
        size_t size = 0;
        unsigned char *data = path != NULL ? khdays_zip_extract(&u->zip, i, &size, &why) : NULL;
        if (path != NULL && data != NULL && khdays_folders_write(path, data, size)) {
            const size_t need = text_n + strlen(place) + 7;
            if (need > text_capacity) {
                text_capacity = need * 2;
                char *grown = realloc(text, text_capacity);
                ok = grown != NULL;
                text = grown != NULL ? grown : text;
            }
            if (ok) {
                text_n += (size_t)snprintf(text + text_n, text_capacity - text_n, "file %s\n", place);
                files += strncmp(place, "files/", 6) == 0;
            }
        } else if (path != NULL) {
            snprintf(u->why, sizeof(u->why), "%.80s could not be written", place);
            ok = 0;
        }
        free(data);
        free(place);
        free(path);
    }
    char *marker = joined(copy, MARKER);
    if (ok && marker != NULL && !khdays_folders_write(marker, text, text_n)) {
        snprintf(u->why, sizeof(u->why), "its " MARKER " could not be written");
        ok = 0;
    }
    free(marker);
    free(text);
    return ok ? files : -1;
}

static void nothing(const char *name, void *context)
{
    (void)name;
    (void)context;
}

/* The .days read, its mod.yml read, every entry checked, then the copy's old files taken away and the new ones
 * written. */
static void copy_out(const char *name, const char *source, const char *copy, const char *old, const char *source_line,
                     const char *stem)
{
    Unpack u;
    size_t size = 0;
    const char *why = "";
    char *data = khdays_folders_read(source, &size);
    memset(&u, 0, sizeof(u));
    u.name = name;
    if (data == NULL) {
        snprintf(u.why, sizeof(u.why), "it cannot be read");
    } else if (!khdays_zip_open(&u.zip, (const unsigned char *)data, size, &why)) {
        snprintf(u.why, sizeof(u.why), "%s", why);
    } else {
        const int at = khdays_zip_find(&u.zip, MANIFEST);
        size_t text_n = 0;
        char *text = at >= 0 ? (char *)khdays_zip_extract(&u.zip, at, &text_n, &why) : NULL;
        if (text == NULL) {
            snprintf(u.why, sizeof(u.why), "no readable " MANIFEST " (not a KH DS Project Editor mod)");
        } else {
            read_manifest(text, text_n, &u.manifest);
            free(text);
        }
    }
    if (u.why[0] == '\0' && check_entries(&u)) {
        remove_listed(copy, old);
        const int files = write_entries(&u, copy, source_line);
        if (files >= 0) {
            khdays_log("mods: %s: \"%s\" by %s: %d of the game's files, copied into mods/%s/%s", name,
                       u.manifest.title != NULL ? u.manifest.title : stem,
                       u.manifest.author != NULL ? u.manifest.author : "?", files, KHDAYS_DAYS_CACHE, stem);
        }
    }
    if (u.why[0] != '\0') {
        khdays_log("mods: %s: %s; the mod is not read", name, u.why);
    }
    khdays_zip_close(&u.zip);
    free_manifest(&u.manifest);
    free(data);
}

/* mods/<name> copied out into mods/.days/<its name without .days>, unless it is as it was when last copied. A
 * folder there the port did not make (no marker) is left alone. */
static void unpack(const char *mods_path, const char *name)
{
    char *source = joined(mods_path, name), *cache = joined(mods_path, KHDAYS_DAYS_CACHE);
    char *stem = copy_n(name, strlen(name) - strlen(KHDAYS_DAYS_EXTENSION));
    char *copy = cache != NULL && stem != NULL ? joined(cache, stem) : NULL;
    char *marker = copy != NULL ? joined(copy, MARKER) : NULL;
    char *old = marker != NULL ? khdays_folders_read(marker, NULL) : NULL;
    char source_line[96], wanted[100];
    snprintf(source_line, sizeof(source_line), "source %lld %lld", source != NULL ? khdays_folders_size(source) : -1,
             source != NULL ? khdays_folders_time(source) : -1);
    snprintf(wanted, sizeof(wanted), "\n%s\n", source_line);
    if (source == NULL || marker == NULL) {
        khdays_log("mods: %s: out of memory", name);
    } else if (old != NULL && strstr(old, wanted) != NULL) {
        /* as it was */
    } else if (old == NULL && khdays_folders_files(copy, nothing, NULL)) {
        khdays_log("mods: %s: mods/%s/%s is not the port's copy of it: left as it is, the mod not read", name,
                   KHDAYS_DAYS_CACHE, stem);
    } else {
        copy_out(name, source, copy, old, source_line, stem);
    }
    free(old);
    free(marker);
    free(copy);
    free(stem);
    free(cache);
    free(source);
}

/* ---- The mods folder --------------------------------------------------------------------------- */

typedef struct Names {
    char **items;
    int count, capacity;
} Names;

static void add_name(const char *name, void *context)
{
    Names *n = context;
    if (n->count == n->capacity) {
        const int grown = n->capacity == 0 ? 8 : n->capacity * 2;
        char **items = realloc(n->items, (size_t)grown * sizeof(*items));
        if (items == NULL) {
            return;
        }
        n->items = items;
        n->capacity = grown;
    }
    if ((n->items[n->count] = copy_n(name, strlen(name))) != NULL) {
        ++n->count;
    }
}

static void free_names(Names *n)
{
    for (int i = 0; i < n->count; ++i) {
        free(n->items[i]);
    }
    free(n->items);
}

/* A copy whose .days is gone (its marker's `name` no longer in the mods folder): its files taken away. */
static void forget_gone(const char *mods_path)
{
    char *cache = joined(mods_path, KHDAYS_DAYS_CACHE);
    Names copies = {NULL, 0, 0};
    if (cache == NULL) {
        return;
    }
    khdays_folders_list(cache, add_name, &copies);
    for (int i = 0; i < copies.count; ++i) {
        char *copy = joined(cache, copies.items[i]), *marker = copy != NULL ? joined(copy, MARKER) : NULL;
        char *text = marker != NULL ? khdays_folders_read(marker, NULL) : NULL;
        const char *line = text != NULL ? strstr(text, "\nname ") : NULL;
        if (line != NULL) {
            const char *end = strchr(line + 1, '\n');
            char *name = copy_n(line + 6, end != NULL ? (size_t)(end - line - 6) : strlen(line + 6));
            char *source = name != NULL ? joined(mods_path, name) : NULL;
            if (source != NULL && !khdays_folders_is_file(source)) {
                remove_listed(copy, text);
                khdays_folders_remove(marker);
                khdays_folders_remove(copy);
                khdays_log("mods: %s is gone: its copy in mods/%s/%s taken away", name, KHDAYS_DAYS_CACHE,
                           copies.items[i]);
            }
            free(source);
            free(name);
        }
        free(text);
        free(marker);
        free(copy);
    }
    free_names(&copies);
    khdays_folders_remove(cache); /* (only when it is left empty) */
    free(cache);
}

void khdays_days_unpack_all(const char *mods_path)
{
    Names files = {NULL, 0, 0};
    khdays_folders_list_files(mods_path, add_name, &files);
    for (int i = 0; i < files.count; ++i) {
        if (khdays_mods_is_days(files.items[i])) {
            unpack(mods_path, files.items[i]);
        }
    }
    free_names(&files);
    forget_gone(mods_path);
}

/* ---- Made for this copy of the game? ----------------------------------------------------------- */

void khdays_days_check_originals(const char *nitrofs)
{
    for (int i = 0; i < khdays_mods_started_count(); ++i) {
        const char *folder = khdays_mods_started_folder(i), *path = khdays_mods_started_path(i);
        if (folder == NULL || path == NULL || !khdays_mods_is_days(folder)) {
            continue;
        }
        char *yml = joined(path, MANIFEST), *text;
        size_t size = 0;
        Manifest m;
        if (yml == NULL || (text = khdays_folders_read(yml, &size)) == NULL) {
            free(yml);
            continue;
        }
        read_manifest(text, size, &m);
        int differ = 0, checked = 0;
        const char *first = NULL;
        for (int k = 0; k < m.count; ++k) {
            const Asset *a = &m.assets[k];
            if (a->original[0] == '\0' || strncmp(a->path, ROOT, strlen(ROOT)) != 0) {
                continue;
            }
            char *game = joined(nitrofs, a->path + strlen(ROOT)), *bytes;
            size_t n = 0;
            if (game != NULL && (bytes = khdays_folders_read(game, &n)) != NULL) {
                char hex[65];
                sha256_hex(bytes, n, hex);
                ++checked;
                if (strcmp(hex, a->original) != 0) {
                    first = first != NULL ? first : a->path + strlen(ROOT);
                    ++differ;
                }
                free(bytes);
            }
            free(game);
        }
        if (differ > 0) {
            khdays_log("mods: %s: made for another copy of the game: %d of the %d files it replaces differ in this "
                       "one (%s first); it may not work",
                       folder, differ, checked, first);
        } else {
            khdays_log("mods: %s: made for this copy of the game (%d files checked)", folder, checked);
        }
        free_manifest(&m);
        free(text);
        free(yml);
    }
}
