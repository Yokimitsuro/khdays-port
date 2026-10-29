/* The game's tables a new mission needs its rows in (tables.h). */
#include "tables.h"

#include "../core/utf8.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static unsigned rd32(const unsigned char *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

static void wr16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void wr32(unsigned char *p, unsigned v)
{
    wr16(p, v);
    wr16(p + 2, v >> 16);
}

typedef struct Bytes {
    unsigned char *data;
    size_t size, capacity;
    int failed;
} Bytes;

static unsigned char *grow(Bytes *b, size_t n)
{
    if (b->failed) {
        return NULL;
    }
    if (b->size + n > b->capacity) {
        size_t capacity = b->capacity == 0 ? 4096 : b->capacity;
        while (b->size + n > capacity) {
            capacity *= 2;
        }
        unsigned char *data = realloc(b->data, capacity);
        if (data == NULL) {
            b->failed = 1;
            return NULL;
        }
        b->data = data;
        b->capacity = capacity;
    }
    unsigned char *at = b->data + b->size;
    b->size += n;
    return at;
}

/* ---- Mission Mode's list ----------------------------------------------------------------------- */

/* A UTF-16 text's length in units, its 0 included, from `p` within `end`; 0 when it does not end there. */
static size_t text_units(const unsigned char *p, const unsigned char *end)
{
    for (size_t n = 0; p + 2 * n + 1 < end; ++n) {
        if (rd16(p + 2 * n) == 0) {
            return n + 1;
        }
    }
    return 0;
}

/* A UTF-8 text in UTF-16 with its 0. */
static void put_text(Bytes *b, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text != NULL ? text : "");
    while (*p != '\0') {
        unsigned char *at = grow(b, 2);
        const unsigned short c = khdays_utf8_next(&p);
        if (at != NULL) {
            wr16(at, c);
        }
    }
    unsigned char *at = grow(b, 2);
    if (at != NULL) {
        wr16(at, 0);
    }
}

/* The records of the list file, checked: their places in `at` (malloc'd), their count; -1 when it does not read. */
static int list_records(const unsigned char *file, size_t size, const unsigned char ***at)
{
    if (size < 2) {
        return -1;
    }
    const int count = (int)rd16(file);
    const unsigned char **places = malloc(((size_t)count + 1) * sizeof(*places));
    if (places == NULL) {
        return -1;
    }
    size_t offset = 2;
    for (int i = 0; i < count; ++i) {
        const size_t record = offset + 2 <= size ? rd16(file + offset) : 0;
        if (record < KHDAYS_LIST_TEXTS || record > size - offset) {
            free(places);
            return -1;
        }
        places[i] = file + offset;
        offset += record;
    }
    *at = places;
    return count;
}

static const char *in_language(char *const texts[], int language)
{
    return texts[language] != NULL ? texts[language] : texts[0];
}

/* One record: `pattern`'s fixed part (or none) with the mission's numbers, then its texts -- its own for its Mission
 * Mode entry (`entry`), the pattern's for the others. */
static void put_record(Bytes *b, const unsigned char *pattern, const KhdaysMission *m, int language, int entry)
{
    const size_t start = b->size;
    unsigned char *fixed = grow(b, KHDAYS_LIST_TEXTS);
    if (fixed == NULL) {
        return;
    }
    if (pattern != NULL) {
        memcpy(fixed, pattern, KHDAYS_LIST_TEXTS);
    } else {
        memset(fixed, 0, KHDAYS_LIST_TEXTS);
    }
    wr16(fixed + KHDAYS_LIST_FILE, (unsigned)m->file);
    wr16(fixed + KHDAYS_LIST_NUMBER, (unsigned)m->number);
    /* past every day of the story: its lists by the day never show it */
    wr16(fixed + KHDAYS_LIST_FIRST_DAY, 0xffffu);
    wr16(fixed + KHDAYS_LIST_LAST_DAY, 0xffffu);
    wr16(fixed + KHDAYS_LIST_CATEGORY, (unsigned)m->category);
    wr16(fixed + KHDAYS_LIST_HIDDEN, 0);
    wr16(fixed + KHDAYS_LIST_SHOWN, (unsigned)m->shown);
    if (entry) {
        if (fixed[KHDAYS_LIST_KIND] != KHDAYS_ENTRY_MISSION) {
            fixed[KHDAYS_LIST_KIND] = KHDAYS_ENTRY_MISSION; /* made from a story row, or from nothing */
            wr16(fixed + KHDAYS_LIST_REWARD, KHDAYS_REWARD_MISSION);
        }
        if (m->time_ms > 0) {
            wr16(fixed + KHDAYS_LIST_SECONDS, (unsigned)(m->time_ms / 1000 > 0xffff ? 0xffff : m->time_ms / 1000));
        }
        const char *objective = in_language(m->objective, language);
        const char *description = m->description[language] != NULL ? m->description[language]
                                  : m->description[0] != NULL      ? m->description[0]
                                                                   : objective;
        put_text(b, in_language(m->name, language));
        put_text(b, description);
        put_text(b, objective);
    }
    /* the rest of the pattern's texts as they are (its format) */
    const unsigned char *p = pattern != NULL ? pattern + KHDAYS_LIST_TEXTS : NULL;
    const unsigned char *end = pattern != NULL ? pattern + rd16(pattern) : NULL;
    for (int t = 0; t < 4; ++t) {
        const size_t units = p != NULL ? text_units(p, end) : 0;
        if (t >= (entry ? 3 : 0)) {
            unsigned char *at = grow(b, units != 0 ? 2 * units : 2);
            if (at != NULL) {
                if (units != 0) {
                    memcpy(at, p, 2 * units);
                } else {
                    wr16(at, 0);
                }
            }
        }
        p = units != 0 ? p + 2 * units : NULL;
    }
    if (b->failed || b->size - start > 0xffffu) {
        b->size = start; /* (a record's size is 16 bits) */
        return;
    }
    wr16(b->data + start, (unsigned)(b->size - start));
}

unsigned char *khdays_tables_list(const unsigned char *file, size_t size, const KhdaysMissions *r, int language,
                                  size_t *out_size)
{
    const unsigned char **records;
    const int count = list_records(file, size, &records);
    if (count < 0) {
        return NULL;
    }
    Bytes b = {0};
    unsigned added = 0;
    (void)grow(&b, size);
    if (b.data != NULL) {
        memcpy(b.data, file, size);
    }
    for (int i = 0; i < r->count && !b.failed; ++i) {
        const KhdaysMission *m = &r->list[i];
        const unsigned char *entry = NULL, *story = NULL;
        if (m->number == 0) {
            continue;
        }
        for (int k = 0; k < count && m->template_file != 0; ++k) {
            const unsigned kind = records[k][KHDAYS_LIST_KIND];
            if ((int)rd16(records[k] + KHDAYS_LIST_FILE) == m->template_file) {
                entry = entry == NULL && kind == KHDAYS_ENTRY_MISSION ? records[k] : entry;
                story = story == NULL && kind == KHDAYS_ENTRY_STORY ? records[k] : story;
            }
        }
        /* its Mission Mode entry, as its template's (none of the story's rows: the story never lists it) */
        const size_t before = b.size;
        put_record(&b, entry != NULL ? entry : story, m, language, 1);
        added += b.size != before;
    }
    free(records);
    if (b.failed || added == 0 || (unsigned)count + added > 0xffffu) {
        free(b.data);
        return NULL;
    }
    wr16(b.data, (unsigned)count + added);
    *out_size = b.size;
    return b.data;
}

/* ---- The missions' table ------------------------------------------------------------------------ */

unsigned char *khdays_tables_mdb(const unsigned char *file, size_t size, const KhdaysMissions *r, size_t *out_size)
{
    if (size < 4) {
        return NULL;
    }
    const unsigned count = rd32(file);
    if (count > (size - 4) / KHDAYS_MDB_ROW) {
        return NULL;
    }
    unsigned added = 0;
    for (int i = 0; i < r->count; ++i) {
        added += r->list[i].number != 0;
    }
    if (added == 0) {
        return NULL;
    }
    const size_t total = 4 + ((size_t)count + added) * KHDAYS_MDB_ROW;
    unsigned char *out = malloc(total);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, file, 4 + (size_t)count * KHDAYS_MDB_ROW);
    unsigned char *row = out + 4 + (size_t)count * KHDAYS_MDB_ROW;
    for (int i = 0; i < r->count; ++i) {
        const KhdaysMission *m = &r->list[i];
        if (m->number == 0) {
            continue;
        }
        memset(row, 0, KHDAYS_MDB_ROW);
        /* the last of the template's rows, as the game's lookup takes the last (Ov002_SelectTableEntry) */
        for (unsigned k = 0; k < count && m->template_file != 0; ++k) {
            const unsigned char *game = file + 4 + (size_t)k * KHDAYS_MDB_ROW;
            if ((int)(short)rd16(game) == m->template_file) {
                memcpy(row, game, KHDAYS_MDB_ROW);
            }
        }
        wr16(row, (unsigned)m->file);
        if (m->level > 0) {
            wr32(row + KHDAYS_MDB_LEVEL, (unsigned)m->level);
            wr32(row + KHDAYS_MDB_LEVEL_SESSION, (unsigned)m->level);
        }
        row += KHDAYS_MDB_ROW;
    }
    wr32(out, count + added);
    *out_size = total;
    return out;
}

/* ---- The chests and the emblems ---------------------------------------------------------------- */

/* An emblem's kind (0-2) as its bit of the mission's four (the list's kinds 1, 2 and 4: 1 << (kind - 1)). */
static const unsigned emblem_bits[] = {0, 1, 3};
#define UNITY_BIT 3u

static unsigned fixed(double v)
{
    return (unsigned)(int)lround(v * 4096.0);
}

unsigned char *khdays_tables_placed(const unsigned char *file, size_t size, const KhdaysMissions *r,
                                    const KhdaysFieldStore *s, int chests, size_t *out_size)
{
    if (size < 4) {
        return NULL;
    }
    const unsigned count = rd32(file);
    if (count > (size - 4) / 4) {
        return NULL;
    }
    unsigned added = 0;
    for (int i = 0; i < r->count; ++i) {
        const KhdaysMission *m = &r->list[i];
        const int rows = chests ? (i < s->count ? s->progress[i].chest_count : 0) : m->emblem_count;
        added += m->number != 0 && rows > 0;
    }
    if (added == 0) {
        return NULL;
    }
    /* the offsets' table grows: every set the game's has moves on by as much */
    const size_t moved = 4u * added, body = size - 4 - 4u * count;
    Bytes b = {0};
    unsigned char *head = grow(&b, 4 + 4u * ((size_t)count + added) + body);
    if (head == NULL) {
        return NULL;
    }
    wr32(head, count + added);
    for (unsigned k = 0; k < count; ++k) {
        wr32(head + 4 + 4 * k, rd32(file + 4 + 4 * k) + (unsigned)moved);
    }
    memcpy(head + 4 + 4u * ((size_t)count + added), file + 4 + 4u * count, body);
    unsigned next = count;
    for (int i = 0; i < r->count; ++i) {
        const KhdaysMission *m = &r->list[i];
        const int rows = chests ? (i < s->count ? s->progress[i].chest_count : 0) : m->emblem_count;
        if (m->number == 0 || rows <= 0) {
            continue;
        }
        while (b.size % 4 != 0 && grow(&b, 1) != NULL) {
            b.data[b.size - 1] = 0;
        }
        const size_t at = b.size;
        unsigned char *set = grow(&b, KHDAYS_PLACED_SET_HEAD + (size_t)rows * KHDAYS_PLACED_ROW);
        if (set == NULL) {
            return NULL;
        }
        memset(set, 0, KHDAYS_PLACED_SET_HEAD + (size_t)rows * KHDAYS_PLACED_ROW);
        wr32(b.data + 4 + 4u * next++, (unsigned)at);
        wr16(set, (unsigned)m->file);
        set[2] = (unsigned char)rows;
        for (int k = 0; k < rows; ++k) {
            unsigned char *row = set + KHDAYS_PLACED_SET_HEAD + (size_t)k * KHDAYS_PLACED_ROW;
            const double *point;
            if (chests) {
                const KhdaysMissionChest *c = &m->chests[k];
                row[0] = (unsigned char)k;
                row[1] = (unsigned char)m->room;
                wr16(row + 2, (unsigned)c->item);
                wr16(row + 4, s->progress[i].first_chest_flag + (unsigned)k - KHDAYS_CHEST_FLAG_LESS);
                wr16(row + 6, (unsigned)c->heading);
                point = c->at;
            } else {
                const KhdaysMissionEmblem *e = &m->emblems[k];
                row[0] = (unsigned char)emblem_bits[e->kind];
                wr16(row + 2, emblem_bits[e->kind] == UNITY_BIT);
                point = e->at;
            }
            for (int p = 0; p < 3; ++p) {
                wr32(row + 8 + 4 * p, fixed(point[p]));
            }
        }
    }
    if (b.failed) {
        free(b.data);
        return NULL;
    }
    *out_size = b.size;
    return b.data;
}
