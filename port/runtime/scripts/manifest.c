/* A scripted mod's manifest (manifest.h). */
#include "manifest.h"

#include "folders.h"
#include "../core/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_span(const char *s, size_t n)
{
    char *out = malloc(n + 1);
    if (out != NULL) {
        memcpy(out, s, n);
        out[n] = 0;
    }
    return out;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int id_valid(const char *id)
{
    if (id == NULL || *id == 0) {
        return 0;
    }
    for (const char *p = id; *p; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-')) {
            return 0;
        }
    }
    return 1;
}

/* A path under scripts/ with no way out of it: no "..", no absolute path, no drive, no backslash. */
static int main_valid(const char *path)
{
    if (strncmp(path, "scripts/", 8) != 0 || path[8] == 0 || strchr(path, '\\') != NULL || strchr(path, ':') != NULL) {
        return 0;
    }
    for (const char *p = path; *p; ++p) {
        if (p[0] == '.' && p[1] == '.' && (p == path || p[-1] == '/') && (p[2] == '/' || p[2] == 0)) {
            return 0;
        }
        if (p[0] == '/' && p[1] == '/') {
            return 0;
        }
    }
    return 1;
}

static int whole_number(const char *s, int *out)
{
    long v = 0;
    if (s == NULL || *s == 0) {
        return 0;
    }
    for (const char *p = s; *p; ++p) {
        if (*p < '0' || *p > '9' || v > 1000000) {
            return 0;
        }
        v = v * 10 + (*p - '0');
    }
    *out = (int)v;
    return 1;
}

static void refuse(char *reason, size_t size, const char *key)
{
    snprintf(reason, size, "mod.ini: %s missing or not valid", key);
}

/* mod.ini's next `key = value` line from *at (blank lines, comments and lines without '=' skipped): its key's and
 * value's spans, trimmed. 0 at the end. */
static int next_pair(const char **at, const char *end, const char **k0, const char **k1, const char **v0,
                     const char **v1)
{
    while (*at < end) {
        const char *line = *at, *eol = memchr(*at, '\n', (size_t)(end - *at)), *eq;
        if (eol == NULL) {
            eol = end;
        }
        *at = eol < end ? eol + 1 : end;
        while (line < eol && is_space(*line)) {
            ++line;
        }
        if (line == eol || *line == '#' || *line == ';') {
            continue;
        }
        eq = memchr(line, '=', (size_t)(eol - line));
        if (eq == NULL) {
            continue;
        }
        *k0 = line, *k1 = eq;
        while (*k1 > *k0 && is_space((*k1)[-1])) {
            --*k1;
        }
        *v0 = eq + 1, *v1 = eol;
        while (*v0 < *v1 && is_space(**v0)) {
            ++*v0;
        }
        while (*v1 > *v0 && is_space((*v1)[-1])) {
            --*v1;
        }
        return 1;
    }
    return 0;
}

static const char *after_mark(const char *text, size_t size)
{
    return size >= 3 && (unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb &&
                   (unsigned char)text[2] == 0xbf
               ? text + 3 /* a UTF-8 mark */
               : text;
}

char *khdays_manifest_value(const char *text, size_t size, const char *key)
{
    const char *p = after_mark(text, size), *end = text + size, *k0, *k1, *v0, *v1;
    const size_t n = strlen(key);
    char *found = NULL;
    while (next_pair(&p, end, &k0, &k1, &v0, &v1)) {
        if ((size_t)(k1 - k0) == n && memcmp(k0, key, n) == 0) {
            free(found);
            found = copy_span(v0, (size_t)(v1 - v0));
        }
    }
    return found;
}

int khdays_manifest_parse(const char *text, size_t size, KhdaysManifest *out, char *reason, size_t reason_size)
{
    char *kind = NULL, *api = NULL, *memory = NULL;
    const char *p = after_mark(text, size), *end = text + size, *k0, *k1, *v0, *v1;
    memset(out, 0, sizeof(*out));
    while (next_pair(&p, end, &k0, &k1, &v0, &v1)) {
        char **slot = NULL;
#define KEY(name) ((size_t)(k1 - k0) == sizeof(name) - 1 && memcmp(k0, name, sizeof(name) - 1) == 0)
        if (KEY("id")) {
            slot = &out->id;
        } else if (KEY("name")) {
            slot = &out->name;
        } else if (KEY("version")) {
            slot = &out->version;
        } else if (KEY("author")) {
            slot = &out->author;
        } else if (KEY("description")) {
            slot = &out->description;
        } else if (KEY("main")) {
            slot = &out->main;
        } else if (KEY("kind")) {
            slot = &kind;
        } else if (KEY("api")) {
            slot = &api;
        } else if (KEY("memory")) {
            slot = &memory;
        }
#undef KEY
        if (slot != NULL) {
            free(*slot);
            *slot = copy_span(v0, (size_t)(v1 - v0));
        }
    }
    {
        const char *bad = !id_valid(out->id)                           ? "id"
                          : out->name == NULL || *out->name == 0       ? "name"
                          : out->version == NULL || *out->version == 0 ? "version"
                          : kind == NULL                               ? "kind"
                          : !whole_number(api, &out->api)              ? "api"
                                                                       : NULL;
        if (bad == NULL) {
            if (strcmp(kind, "gameplay") == 0) {
                out->kind = KHDAYS_MOD_GAMEPLAY;
            } else if (strcmp(kind, "visual") == 0) {
                out->kind = KHDAYS_MOD_VISUAL;
            } else {
                bad = "kind";
            }
        }
        if (bad == NULL && memory != NULL) {
            if (strcmp(memory, "write") == 0) {
                out->memory_write = 1;
            } else if (strcmp(memory, "read") != 0) {
                bad = "memory";
            }
        }
        if (bad == NULL && out->main == NULL) {
            out->main = copy_span("scripts/main.lua", 16);
        }
        if (bad == NULL && !main_valid(out->main)) {
            bad = "main";
        }
        free(kind);
        free(api);
        free(memory);
        if (bad != NULL) {
            refuse(reason, reason_size, bad);
            khdays_manifest_free(out);
            return 0;
        }
    }
    return 1;
}

void khdays_manifest_free(KhdaysManifest *m)
{
    free(m->id);
    free(m->name);
    free(m->version);
    free(m->author);
    free(m->description);
    free(m->main);
    memset(m, 0, sizeof(*m));
}

typedef struct Fingerprint {
    KhdaysSha256 sha;
    const char *folder;
    int ok;
} Fingerprint;

static void add_file(Fingerprint *f, const char *relative)
{
    size_t size = 0, n = strlen(f->folder) + strlen(relative) + 2;
    char *path = malloc(n), *bytes;
    if (path == NULL) {
        f->ok = 0;
        return;
    }
    snprintf(path, n, "%s/%s", f->folder, relative);
    bytes = khdays_folders_read(path, &size);
    free(path);
    if (bytes == NULL) {
        f->ok = 0;
        return;
    }
    khdays_sha256_add(&f->sha, relative, strlen(relative) + 1);
    khdays_sha256_add(&f->sha, bytes, size);
    free(bytes);
}

static void add_script(const char *relative, void *context)
{
    Fingerprint *f = context;
    size_t n = strlen(relative) + 9;
    char *path = malloc(n);
    if (path == NULL) {
        f->ok = 0;
        return;
    }
    snprintf(path, n, "scripts/%s", relative);
    add_file(f, path);
    free(path);
}

int khdays_manifest_fingerprint(const char *folder, unsigned char digest[32])
{
    Fingerprint f;
    size_t n = strlen(folder) + 9;
    char *scripts = malloc(n);
    if (scripts == NULL) {
        return 0;
    }
    f.folder = folder;
    f.ok = 1;
    khdays_sha256_begin(&f.sha);
    /* "mod.ini" sorts before "scripts/..." */
    add_file(&f, "mod.ini");
    snprintf(scripts, n, "%s/scripts", folder);
    khdays_folders_files(scripts, add_script, &f);
    free(scripts);
    khdays_sha256_end(&f.sha, digest);
    return f.ok;
}
