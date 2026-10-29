/* The missions the mods add (registry.h). */
#include "registry.h"

#include "worlds.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const languages[KHDAYS_MISSION_LANGUAGES] = {"en", "fr", "de", "it", "es"};

const char *khdays_mission_language(int l)
{
    return l >= 0 && l < KHDAYS_MISSION_LANGUAGES ? languages[l] : NULL;
}

static char *copy_span(const char *p, size_t n)
{
    char *s = malloc(n + 1);
    if (s != NULL) {
        memcpy(s, p, n);
        s[n] = '\0';
    }
    return s;
}

/* A text's value: `\n` a line break, `\\` a backslash. */
static char *copy_text(const char *p, size_t n)
{
    char *s = malloc(n + 1), *o = s;
    if (s == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == '\\' && i + 1 < n && (p[i + 1] == 'n' || p[i + 1] == '\\')) {
            *o++ = p[i + 1] == 'n' ? '\n' : '\\';
            ++i;
        } else {
            *o++ = p[i];
        }
    }
    *o = '\0';
    return s;
}

static void free_mission(KhdaysMission *m)
{
    for (int l = 0; l < KHDAYS_MISSION_LANGUAGES; ++l) {
        free(m->name[l]);
        free(m->objective[l]);
        free(m->description[l]);
    }
    free(m->identity);
    free(m->unlock_after);
#pragma warning(push)
#pragma warning(disable : 6001) /* the spawners up to spawner_count are read_wave's, their enemies set */
    for (int i = 0; i < m->spawner_count; ++i) {
        free(m->spawners[i].enemies);
    }
#pragma warning(pop)
    free(m->spawners);
    free(m->chests);
    free(m->emblems);
    memset(m, 0, sizeof(*m));
}

static int whole(const char *text, int low, int high, int *out);

/* Room for one more of `*array`'s `size`-byte elements past `count`; 0 with no memory. */
static int grow(void **array, int count, size_t size)
{
    if ((count & (count - 1)) != 0 && count != 0) {
        return 1; /* (the capacity doubles at each power of two) */
    }
    void *more = realloc(*array, (size_t)(count == 0 ? 4 : 2 * count) * size);
    if (more == NULL) {
        return 0;
    }
    *array = more;
    return 1;
}

/* The words of `value` (spaces between them) into `words`, as many as `room`; how many there were. */
static int split(char *value, char **words, int room)
{
    int n = 0;
    for (char *p = value; *p != '\0';) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        if (words != NULL && n < room) {
            words[n] = p;
        }
        ++n;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            ++p;
        }
        if (*p != '\0') {
            if (words != NULL) {
                *p = '\0'; /* (counting only: the text left as it is) */
            }
            ++p;
        }
    }
    return n;
}

static int real(const char *text, double *out)
{
    char *end;
    *out = strtod(text, &end);
    return end != text && *end == '\0';
}

/* `<x> <y> <z>` from words[0..2]. */
static int point(char **words, double at[3])
{
    return real(words[0], &at[0]) && real(words[1], &at[1]) && real(words[2], &at[2]);
}

/* `wave <n> = <class>x<count> [...] at <x> <y> <z>`: one spawner of wave n. */
static int read_wave(KhdaysMission *m, const char *number, char *value)
{
    int wave, ok = 0;
    const int n = split(value, NULL, 0);
    char **words = n > 0 ? malloc((size_t)n * sizeof(*words)) : NULL;
    if (words == NULL) {
        return 0;
    }
    split(value, words, n);
    const int at = n >= 5 && strcmp(words[n - 4], "at") == 0 ? n - 4 : -1;
    if (whole(number, 1, 255, &wave) && at >= 1 &&
        grow((void **)&m->spawners, m->spawner_count, sizeof(*m->spawners))) {
        KhdaysMissionSpawner *s = &m->spawners[m->spawner_count];
        memset(s, 0, sizeof(*s));
        s->wave = wave;
        if (point(&words[at + 1], s->at) && (s->enemies = calloc((size_t)at, sizeof(*s->enemies))) != NULL) {
            ++m->spawner_count;
            ok = 1;
            for (int i = 0; i < at && ok; ++i) {
                char *times = strchr(words[i], 'x');
                KhdaysMissionEnemies *e = &s->enemies[s->enemy_count++];
                e->count = 1;
                if (times != NULL) {
                    *times++ = '\0';
                    ok = whole(times, 1, 255, &e->count);
                }
                ok = ok && whole(words[i], 0, 255, &e->enemy_class);
            }
        }
    }
    free(words);
    return ok;
}

/* Why the last chest or emblem line was refused, when its form was right. */
static const char *refused_why;

/* `chest = <item> at <x> <y> <z> <heading>`, `emblem = <kind> at <x> <y> <z>`, `start = <x> <y> <z> <facing>` */

static int read_chest(KhdaysMission *m, char *value)
{
    char *words[8];
    if (m->chest_count == KHDAYS_MOST_CHESTS) {
        refused_why = "chest: the game's chests of a mission are 127 at most";
        return 0;
    }
    if (split(value, words, 8) != 6 || strcmp(words[1], "at") != 0 ||
        !grow((void **)&m->chests, m->chest_count, sizeof(*m->chests))) {
        return 0;
    }
    KhdaysMissionChest *c = &m->chests[m->chest_count];
    if (!whole(words[0], 0, 0xffff, &c->item) || !point(&words[2], c->at) || !whole(words[5], -360, 360, &c->heading)) {
        return 0;
    }
    ++m->chest_count;
    return 1;
}

static int read_emblem(KhdaysMission *m, char *value)
{
    char *words[8];
    if (split(value, words, 8) != 5 || strcmp(words[1], "at") != 0 ||
        !grow((void **)&m->emblems, m->emblem_count, sizeof(*m->emblems))) {
        return 0;
    }
    KhdaysMissionEmblem *e = &m->emblems[m->emblem_count];
    if (!whole(words[0], 0, 2, &e->kind) || !point(&words[2], e->at)) {
        return 0;
    }
    for (int i = 0; i < m->emblem_count; ++i) {
        if (m->emblems[i].kind == e->kind) {
            refused_why = "emblem: one of each kind (the save keeps a bit for each)";
            return 0;
        }
    }
    ++m->emblem_count;
    return 1;
}

static int read_start(KhdaysMission *m, char *value)
{
    char *words[8];
    m->has_start = split(value, words, 8) == 4 && point(words, m->start) && whole(words[3], -360, 360, &m->facing);
    return m->has_start;
}

static int valid_key(const char *key)
{
    if (*key == '\0') {
        return 0;
    }
    for (const char *p = key; *p != '\0'; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            return 0;
        }
    }
    return 1;
}

/* A whole number in [low, high]; 0 when the text is not one. */
static int whole(const char *text, int low, int high, int *out)
{
    char *end;
    const long v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < low || v > high) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

typedef struct Reading {
    KhdaysMissions *r;
    int mod;
    KhdaysMissionsReport report;
    void *context;
    KhdaysMission m; /* the block being read */
    int open, bad, bad_line;
    char why[200];
} Reading;

static void refuse(Reading *g, int line, const char *why)
{
    if (!g->bad) {
        g->bad = 1;
        g->bad_line = line;
        snprintf(g->why, sizeof(g->why), "%s", why);
    }
}

static int key_taken(const KhdaysMissions *r, int mod, const char *key)
{
    for (int i = 0; i < r->count; ++i) {
        if (r->list[i].mod == mod && strcmp(r->list[i].key, key) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The block read so far: kept when whole, else told of and dropped. */
static int close_block(Reading *g)
{
    KhdaysMission *m = &g->m;
    if (!g->open) {
        return 0;
    }
    g->open = 0;
    if (!g->bad && m->name[0] == NULL) {
        refuse(g, m->line, "no English name (name en = ...)");
    }
    if (!g->bad && m->objective[0] == NULL) {
        refuse(g, m->line, "no English objective (objective en = ...)");
    }
    if (!g->bad && m->world[0] == '\0') {
        refuse(g, m->line, "no world (world = tt ...)");
    }
    if (!g->bad && key_taken(g->r, g->mod, m->key)) {
        refuse(g, m->line, "the key is taken by another of the mod's missions");
    }
    if (g->bad) {
        char why[260];
        snprintf(why, sizeof(why), "mission %s left out: %s", m->key != NULL ? m->key : "?", g->why);
        if (g->report != NULL) {
            g->report(g->r->mods[g->mod].id, g->bad_line, why, g->context);
        }
        free_mission(m);
        return 0;
    }
    if (m->category < 0) {
        m->category = khdays_world_index(m->world);
    }
    KhdaysMissions *r = g->r;
    if (r->count == r->capacity) {
        const int grown = r->capacity == 0 ? 8 : r->capacity * 2;
        KhdaysMission *list = realloc(r->list, (size_t)grown * sizeof(*list));
        if (list == NULL) {
            free_mission(m);
            return 0;
        }
        r->list = list;
        r->capacity = grown;
    }
    r->list[r->count++] = *m;
    memset(m, 0, sizeof(*m));
    return 1;
}

static void open_block(Reading *g, const char *key, int line)
{
    KhdaysMission *m = &g->m;
    const char *mod = g->r->mods[g->mod].id;
    memset(m, 0, sizeof(*m));
    g->open = 1;
    g->bad = 0;
    m->mod = g->mod;
    m->line = line;
    m->category = -1;
    const size_t size = strlen(mod) + 1 + strlen(key) + 1;
    m->identity = malloc(size);
    if (m->identity == NULL) {
        refuse(g, line, "not enough memory");
        return;
    }
    snprintf(m->identity, size, "%s/%s", mod, key);
    m->key = m->identity + strlen(mod) + 1;
    if (!valid_key(key)) {
        refuse(g, line, "a key is letters, digits, '_' and '-'");
    }
}

static int language_of(const char *code)
{
    for (int l = 0; l < KHDAYS_MISSION_LANGUAGES; ++l) {
        if (strcmp(languages[l], code) == 0) {
            return l;
        }
    }
    return -1;
}

/* `unlock = start | cleared <number> | cleared <mod id>/<key>` */
static int read_unlock(KhdaysMission *m, char *value)
{
    if (strcmp(value, "start") == 0) {
        return 1;
    }
    if (strncmp(value, "cleared ", 8) != 0) {
        return 0;
    }
    char *what = value + 8;
    while (*what == ' ') {
        ++what;
    }
    if (strchr(what, '/') != NULL) {
        m->unlock_after = copy_span(what, strlen(what));
        return m->unlock_after != NULL;
    }
    return whole(what, 1, 0xffff, &m->unlock_number);
}

static void trim(char **start, char **end)
{
    while (*start < *end && isspace((unsigned char)**start)) {
        ++*start;
    }
    while (*end > *start && isspace((unsigned char)(*end)[-1])) {
        --*end;
    }
}

/* One `key [language] = value` line of the open block. */
static void read_line(Reading *g, char *name, char *value_start, char *value_end, int line)
{
    KhdaysMission *m = &g->m;
    char *language = strchr(name, ' ');
    if (language != NULL) {
        *language++ = '\0';
        while (*language == ' ') {
            ++language;
        }
    }
    char **texts = strcmp(name, "name") == 0          ? m->name
                   : strcmp(name, "objective") == 0   ? m->objective
                   : strcmp(name, "description") == 0 ? m->description
                                                      : NULL;
    if (texts != NULL) {
        const int l = language != NULL ? language_of(language) : -1;
        trim(&value_start, &value_end);
        if (l < 0) {
            refuse(g, line, "a text names its language: en, fr, de, it or es");
        } else if (value_start == value_end) {
            refuse(g, line, "an empty text");
        } else {
            free(texts[l]);
            texts[l] = copy_text(value_start, (size_t)(value_end - value_start));
        }
        return;
    }
    if (language != NULL && strcmp(name, "wave") != 0) {
        refuse(g, line, "only name, objective and description take a language (and wave its number)");
        return;
    }
    /* the others' values end at a '#' */
    char *hash = memchr(value_start, '#', (size_t)(value_end - value_start));
    if (hash != NULL) {
        value_end = hash;
    }
    trim(&value_start, &value_end);
    *value_end = '\0';
    char *value = value_start;
    int ok;
    refused_why = NULL;
    if (strcmp(name, "wave") == 0) {
        ok = language != NULL && read_wave(m, language, value);
    } else if (strcmp(name, "start") == 0) {
        ok = read_start(m, value);
    } else if (strcmp(name, "chest") == 0) {
        ok = read_chest(m, value);
    } else if (strcmp(name, "emblem") == 0) {
        ok = read_emblem(m, value);
    } else if (strcmp(name, "category") == 0) {
        ok = whole(value, 0, KHDAYS_GAME_WORLDS - 1, &m->category);
    } else if (strcmp(name, "world") == 0) {
        ok = khdays_world_index(value) >= 0;
        if (ok) {
            snprintf(m->world, sizeof(m->world), "%s", value);
        }
    } else if (strcmp(name, "room") == 0) {
        ok = whole(value, 0, 254, &m->room);
    } else if (strcmp(name, "level") == 0) {
        ok = whole(value, 1, 120, &m->level);
    } else if (strcmp(name, "time") == 0) {
        ok = whole(value, 1, 0x7fffffff, &m->time_ms);
    } else if (strcmp(name, "unlock") == 0) {
        ok = read_unlock(m, value);
    } else if (strcmp(name, "template") == 0) {
        ok = whole(value, 1, KHDAYS_FILE_FIRST - 1, &m->template_file);
    } else {
        char why[96];
        snprintf(why, sizeof(why), "unknown setting \"%.40s\"", name);
        refuse(g, line, why);
        return;
    }
    if (!ok) {
        static const char *const forms[][2] = {{"wave", "wave <n> = <class>x<count> ... at <x> <y> <z>"},
                                               {"start", "start = <x> <y> <z> <facing>"},
                                               {"chest", "chest = <item> at <x> <y> <z> <heading>"},
                                               {"emblem", "emblem = <kind 0-2> at <x> <y> <z>"}};
        const char *form = NULL;
        for (size_t i = 0; i < sizeof(forms) / sizeof(forms[0]); ++i) {
            form = strcmp(name, forms[i][0]) == 0 ? forms[i][1] : form;
        }
        char why[160];
        if (refused_why != NULL) {
            snprintf(why, sizeof(why), "%s", refused_why);
        } else if (form != NULL) {
            snprintf(why, sizeof(why), "%s: not a valid value (%s)", name, form);
        } else {
            snprintf(why, sizeof(why), "%s: not a valid value", name);
        }
        refuse(g, line, why);
    }
}

static int add_mod(KhdaysMissions *r, const char *id, const char *folder)
{
    if (r->mod_count == r->mod_capacity) {
        const int grown = r->mod_capacity == 0 ? 4 : r->mod_capacity * 2;
        KhdaysMissionMod *mods = realloc(r->mods, (size_t)grown * sizeof(*mods));
        if (mods == NULL) {
            return -1;
        }
        memset(mods + r->mod_capacity, 0, (size_t)(grown - r->mod_capacity) * sizeof(*mods));
        r->mods = mods;
        r->mod_capacity = grown;
    }
    KhdaysMissionMod *mod = &r->mods[r->mod_count];
    mod->id = copy_span(id, strlen(id));
    mod->folder = copy_span(folder, strlen(folder));
    if (mod->id == NULL || mod->folder == NULL) {
        free(mod->id);
        free(mod->folder);
        return -1;
    }
    return r->mod_count++;
}

int khdays_missions_read(KhdaysMissions *r, const char *id, const char *folder, const char *text, size_t size,
                         KhdaysMissionsReport report, void *context)
{
    Reading g;
    int added = 0, line = 0;
    memset(&g, 0, sizeof(g));
    g.r = r;
    g.report = report;
    g.context = context;
    g.mod = add_mod(r, id, folder);
    if (g.mod < 0) {
        return 0;
    }
    char *copy = copy_span(text, size);
    if (copy == NULL) {
        return 0;
    }
    for (char *p = copy, *next; p != NULL && *p != '\0'; p = next) {
        char *end = strchr(p, '\n');
        next = end != NULL ? end + 1 : NULL;
        end = end != NULL ? end : p + strlen(p);
        ++line;
        if (line == 1 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf) {
            p += 3; /* a byte order mark */
        }
        trim(&p, &end);
        if (p == end || *p == '#') {
            continue;
        }
        *end = '\0';
        if (*p == '[') {
            added += close_block(&g);
            char *close = strchr(p, ']');
            if (strncmp(p, "[mission ", 9) != 0 || close == NULL || close[1] != '\0') {
                if (report != NULL) {
                    report(id, line, "not a block this port reads (\"[mission <key>]\"): skipped to the next", context);
                }
                continue;
            }
            char *key = p + 9, *key_end = close;
            trim(&key, &key_end);
            *key_end = '\0';
            open_block(&g, key, line);
            continue;
        }
        if (!g.open) {
            if (report != NULL) {
                report(id, line, "a setting outside a [mission <key>] block: ignored", context);
            }
            continue;
        }
        char *equals = strchr(p, '=');
        if (equals == NULL) {
            refuse(&g, line, "not a \"setting = value\" line");
            continue;
        }
        char *name = p, *name_end = equals;
        trim(&name, &name_end);
        *name_end = '\0';
        read_line(&g, name, equals + 1, end, line);
    }
    added += close_block(&g);
    free(copy);
    return added;
}

int khdays_missions_number(KhdaysMissions *r, KhdaysFieldStore *store, KhdaysMissionsReport report, void *context)
{
    int numbered = 0, file = KHDAYS_FILE_FIRST;
    for (int i = 0; i < r->count; ++i) {
        KhdaysMission *m = &r->list[i];
        /* the number the game shows (the list's, the results' n % 100): on after its own 93 */
        const int shown = (KHDAYS_GAME_MISSIONS + 1 + numbered) % 100;
        int n = KHDAYS_MISSION_FIRST + (shown - KHDAYS_MISSION_FIRST % 100 + 100) % 100;
        while (n <= KHDAYS_MISSION_LAST && !khdays_field_store_fits(store, n)) {
            n += 100;
        }
        if (n > KHDAYS_MISSION_LAST || file > KHDAYS_FILE_LAST || !khdays_field_store_place(store, i, n)) {
            if (report != NULL) {
                report(r->mods[m->mod].id, m->line, "no mission number left for it: left out", context);
            }
            continue;
        }
        m->number = n;
        m->file = file++;
        m->shown = shown;
        ++numbered;
    }
    return numbered;
}

const KhdaysMission *khdays_missions_by_number(const KhdaysMissions *r, int number)
{
    for (int i = 0; i < r->count; ++i) {
        if (r->list[i].number != 0 && r->list[i].number == number) {
            return &r->list[i];
        }
    }
    return NULL;
}

const KhdaysMission *khdays_missions_by_file(const KhdaysMissions *r, int file)
{
    for (int i = 0; i < r->count; ++i) {
        if (r->list[i].number != 0 && r->list[i].file == file) {
            return &r->list[i];
        }
    }
    return NULL;
}

int khdays_missions_index(const KhdaysMissions *r, const char *identity)
{
    for (int i = 0; i < r->count; ++i) {
        if (strcmp(r->list[i].identity, identity) == 0) {
            return i;
        }
    }
    return -1;
}

void khdays_missions_free(KhdaysMissions *r)
{
    for (int i = 0; i < r->count; ++i) {
        free_mission(&r->list[i]);
    }
#pragma warning(push)
#pragma warning(disable : 6001) /* the mods up to mod_count are add_mod's, each field set */
    for (int i = 0; i < r->mod_count; ++i) {
        free(r->mods[i].id);
        free(r->mods[i].folder);
    }
#pragma warning(pop)
    free(r->list);
    free(r->mods);
    memset(r, 0, sizeof(*r));
}
