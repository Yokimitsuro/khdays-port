/* The mods' new missions (runtime/missions/): descriptions read and refused, their numbers, their progress where
 * the game keeps a mission's, the save part, and their rows in Mission Mode's list and the missions' table --
 * on made-up tables, and on the game's own when the player's data is extracted (argument: its decompressed/). */
#include "../runtime/missions/field_store.h"
#include "../runtime/missions/registry.h"
#include "../runtime/missions/save_part.h"
#include "../runtime/missions/tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                                                 \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* The reports a reading gave: their lines and the last reason. */
typedef struct Reports {
    int count, lines[16];
    char last[300];
} Reports;

static void reported(const char *mod, int line, const char *why, void *context)
{
    Reports *r = context;
    (void)mod;
    if (r->count < 16) {
        r->lines[r->count] = line;
    }
    ++r->count;
    snprintf(r->last, sizeof(r->last), "%s", why);
}

static int read_text(KhdaysMissions *r, const char *mod, const char *text, Reports *reports)
{
    return khdays_missions_read(r, mod, mod, text, strlen(text), reported, reports);
}

static void a_description_read(void)
{
    static const char text[] = "\xef\xbb\xbf# a mod's missions\n"
                               "[mission clock]\n"
                               "name en = The Lost Clock\n"
                               "name es = El reloj perdido\n"
                               "objective en = Defeat every Heartless. # all of them\n"
                               "description en = Two lines,\\nthe second.\n"
                               "category = 3                # the list's\n"
                               "world = tt\n"
                               "room = 2\n"
                               "level = 12\n"
                               "time = 120000\n"
                               "unlock = cleared 94\n"
                               "template = 0105\n"
                               "\n"
                               "[mission second-one]\n"
                               "name en = Second\n"
                               "objective en = Win.\n"
                               "world = he\n"
                               "unlock = cleared Other/clock\n";
    KhdaysMissions r = {0};
    Reports reports = {0};
    CHECK(read_text(&r, "Clocks", text, &reports) == 2);
    CHECK(reports.count == 0);
    const KhdaysMission *m = &r.list[0];
    CHECK(strcmp(m->identity, "Clocks/clock") == 0 && strcmp(m->key, "clock") == 0);
    CHECK(strcmp(m->name[0], "The Lost Clock") == 0 && strcmp(m->name[4], "El reloj perdido") == 0);
    CHECK(m->name[1] == NULL);
    /* a text keeps its '#'; the others end at it */
    CHECK(strcmp(m->objective[0], "Defeat every Heartless. # all of them") == 0);
    CHECK(strcmp(m->description[0], "Two lines,\nthe second.") == 0);
    CHECK(m->category == 3 && strcmp(m->world, "tt") == 0 && m->room == 2 && m->level == 12);
    CHECK(m->time_ms == 120000 && m->unlock_number == 94 && m->unlock_after == NULL && m->template_file == 105);
    m = &r.list[1];
    CHECK(strcmp(m->identity, "Clocks/second-one") == 0);
    CHECK(m->category == 2); /* its world's: he */
    CHECK(m->room == 0 && strcmp(m->unlock_after, "Other/clock") == 0);
    khdays_missions_free(&r);
}

static void bad_blocks_left_out(void)
{
    static const char text[] = "stray = 1\n"
                               "[mission ok]\nname en = A\nobjective en = B\nworld = tt\n"
                               "[mission no-english]\nname es = A\nobjective en = B\nworld = tt\n"
                               "[mission bad key!]\nname en = A\nobjective en = B\nworld = tt\n"
                               "[mission ok]\nname en = A\nobjective en = B\nworld = tt\n"
                               "[mission unknown]\nname en = A\nobjective en = B\nworld = tt\nspeed = 3\n"
                               "[mission no-world]\nname en = A\nobjective en = B\nworld = hb\n"
                               "[mission range]\nname en = A\nobjective en = B\nworld = tt\nlevel = 121\n"
                               "[mission language]\nname xx = A\nobjective en = B\nworld = tt\n"
                               "[mission last]\nname en = A\nobjective en = B\nworld = aw\ncategory = 12\n"
                               "[world kept]\n";
    KhdaysMissions r = {0};
    Reports reports = {0};
    CHECK(read_text(&r, "Bad", text, &reports) == 1);
    CHECK(r.count == 1 && strcmp(r.list[0].key, "ok") == 0);
    /* the stray line, then each bad block by the line it went wrong on */
    CHECK(reports.count == 10);
    CHECK(reports.lines[0] == 1);
    CHECK(reports.lines[1] == 6);  /* no English name: the block's line */
    CHECK(reports.lines[2] == 10); /* the bad key */
    CHECK(reports.lines[3] == 14); /* the key taken */
    CHECK(reports.lines[4] == 22); /* speed */
    CHECK(reports.lines[5] == 26); /* hb: not a world the game ships */
    CHECK(reports.lines[6] == 31); /* level 121 */
    CHECK(reports.lines[7] == 33); /* xx */
    CHECK(reports.lines[8] == 40); /* category 12 */
    CHECK(reports.lines[9] == 41); /* not a [mission] block */
    khdays_missions_free(&r);
}

/* A mission's chests: their flags past every field, opened through the game's flag calls, kept in the save part. */
static void chests_kept(void)
{
    static const char text[] = "[mission a]\nname en = A\nobjective en = O\nworld = tt\n"
                               "chest = 2 at 0 0 0 0\nchest = 3 at 1 0 0 0\nchest = 4 at 2 0 0 0\n";
    KhdaysMissions r = {0};
    KhdaysFieldStore s = {0};
    KhdaysSavePart kept = {0};
    unsigned first = 0, v = 0;
    CHECK(read_text(&r, "Mod", text, &(Reports){0}) == 1);
    CHECK(khdays_missions_number(&r, &s, NULL, NULL) == 1);
    CHECK(khdays_field_store_place_chests(&s, 0, 3, &first));
    CHECK(first >= KHDAYS_FLAG_BITS && first + 2 <= KHDAYS_CHEST_FLAG_LAST);
    for (int rec = 0; rec < KHDAYS_RECORDS; ++rec) { /* none of the mission's own fields */
        unsigned bits;
        const unsigned start = khdays_mission_field(r.list[0].number, rec, &bits);
        CHECK(first + 3 <= start || start + bits <= first);
    }
    CHECK(khdays_field_store_set(&s, first + 1, 1, 1)); /* the second opened */
    CHECK(khdays_field_store_get(&s, first + 1, 1, &v) && v == 1);
    CHECK(khdays_field_store_get(&s, first, 1, &v) && v == 0);
    size_t size = 0;
    char *out = khdays_save_part_text(&r, &s, &kept, &size);
    CHECK(out != NULL && strstr(out, "Mod/a 0 0 0 0 -1 010\n") != NULL);
    khdays_field_store_clear(&s);
    CHECK(khdays_field_store_get(&s, first + 1, 1, &v) && v == 0);
    if (out != NULL) {
        khdays_save_part_read(out, size, &r, &s, &kept);
    }
    CHECK(s.progress[0].chest_count == 3 && s.progress[0].opened[1] == 1 && s.progress[0].opened[0] == 0);
    free(out);
    khdays_save_part_free(&kept);
    khdays_field_store_free(&s);
    khdays_missions_free(&r);
}

static void waves_chests_and_emblems_read(void)
{
    static const char text[] = "[mission waves]\n"
                               "name en = Two Waves\nobjective en = Win.\nworld = eh\nroom = 2\n"
                               "start = 0 0.049 0 -90\n"
                               "wave 1 = 0x1 at 0 0.049 -1.5\n"
                               "wave 2 = 0x2 32x1 at 0 0.049 1.5\n"
                               "wave 2 = 3x1 at 4 0 4\n"
                               "chest = 257 at 1 0 -2 180\n"
                               "emblem = 2 at -1 0 2\n"
                               "[mission bad-wave]\nname en = A\nobjective en = B\nworld = tt\nwave 1 = 0x0 at 0 0 0\n"
                               "[mission bad-start]\nname en = A\nobjective en = B\nworld = tt\nstart = 0 0 0\n"
                               "[mission bad-emblem]\nname en = A\nobjective en = B\nworld = tt\nemblem = 3 at 0 0 0\n";
    KhdaysMissions r = {0};
    Reports reports = {0};
    CHECK(read_text(&r, "Waves", text, &reports) == 1);
    const KhdaysMission *m = &r.list[0];
    CHECK(m->has_start && m->start[1] == 0.049 && m->facing == -90);
    CHECK(m->spawner_count == 3);
    if (m->spawner_count == 3) {
        CHECK(m->spawners[0].wave == 1 && m->spawners[0].enemy_count == 1 && m->spawners[0].at[2] == -1.5);
        CHECK(m->spawners[1].wave == 2 && m->spawners[1].enemy_count == 2 &&
              m->spawners[1].enemies[1].enemy_class == 32);
        CHECK(m->spawners[2].wave == 2 && m->spawners[2].enemies[0].enemy_class == 3 && m->spawners[2].at[0] == 4.0);
    }
    CHECK(m->chest_count == 1 && m->chests[0].item == 257 && m->chests[0].heading == 180 && m->chests[0].at[2] == -2.0);
    CHECK(m->emblem_count == 1 && m->emblems[0].kind == 2 && m->emblems[0].at[0] == -1.0);
    /* a count of none, a start without its facing, an emblem's kind past the three: each block left out */
    CHECK(reports.count == 3);
    khdays_missions_free(&r);
}

/* Every field of every numbered mission past the array, and no two missions' bits the same. */
static void numbers_apart(const KhdaysMissions *r)
{
    unsigned char *seen = calloc(0x80000, 1);
    if (seen == NULL) {
        CHECK(0);
        return;
    }
    for (int i = 0; i < r->count; ++i) {
        const KhdaysMission *m = &r->list[i];
        CHECK(m->number >= KHDAYS_MISSION_FIRST && m->number <= KHDAYS_MISSION_LAST);
        CHECK(m->file == KHDAYS_FILE_FIRST + i);
        CHECK(m->shown == (KHDAYS_GAME_MISSIONS + 1 + i) % 100);
        CHECK(m->number % 100 == m->shown);
        for (int k = 0; k < KHDAYS_RECORDS; ++k) {
            unsigned bits;
            const unsigned start = khdays_mission_field(m->number, k, &bits);
            CHECK(start >= KHDAYS_FLAG_BITS);
            for (unsigned b = 0; b < bits && start + b < 0x80000u; ++b) {
                CHECK(!seen[start + b]);
                seen[start + b] = 1;
            }
        }
    }
    free(seen);
}

static void numbering(void)
{
    KhdaysMissions a = {0}, b = {0};
    KhdaysFieldStore sa = {0}, sb = {0};
    /* 1,000 missions from three mods, twice: the same numbers */
    for (int pass = 0; pass < 2; ++pass) {
        KhdaysMissions *r = pass == 0 ? &a : &b;
        for (int mod = 0; mod < 3; ++mod) {
            const int count = mod == 2 ? 334 : 333;
            size_t room = (size_t)count * 96 + 1, used = 0;
            char *text = malloc(room), name[8];
            for (int i = 0; i < count && text != NULL; ++i) {
                used += (size_t)snprintf(text + used, room - used,
                                         "[mission m%d]\nname en = M\nobjective en = O\nworld = tt\n", i);
            }
            snprintf(name, sizeof(name), "Mod%d", mod);
            CHECK(text != NULL && khdays_missions_read(r, name, name, text, used, NULL, NULL) == count);
            free(text);
        }
    }
    CHECK(khdays_missions_number(&a, &sa, NULL, NULL) == 1000);
    CHECK(khdays_missions_number(&b, &sb, NULL, NULL) == 1000);
    for (int i = 0; i < a.count; ++i) {
        CHECK(a.list[i].number == b.list[i].number && a.list[i].file == b.list[i].file);
    }
    CHECK(a.list[0].number == 3595); /* the first shows 95, after the tutorial's 94 */
    numbers_apart(&a);
    CHECK(khdays_missions_by_number(&a, a.list[500].number) == &a.list[500]);
    CHECK(khdays_missions_by_file(&a, a.list[999].file) == &a.list[999]);
    CHECK(khdays_missions_index(&a, "Mod2/m333") == 999);
    khdays_missions_free(&a);
    khdays_missions_free(&b);
    khdays_field_store_free(&sa);
    khdays_field_store_free(&sb);
}

static void the_field_store(void)
{
    KhdaysFieldStore s = {0};
    unsigned v = 99, bits;
    CHECK(!khdays_field_store_get(&s, 0x5000, 3, &v));
    CHECK(!khdays_field_store_fits(&s, 100));      /* a game mission's: inside the array */
    CHECK(!khdays_field_store_place(&s, 0, 3509)); /* its badges cross into the array */
    CHECK(khdays_field_store_place(&s, 0, 3594));
    CHECK(khdays_field_store_place(&s, 1, 3595));
    CHECK(!khdays_field_store_place(&s, 2, 3594));
    CHECK(s.count == 2 && s.progress[0].best == -1);
    /* status, stage, transfer and badges, as the game reads and writes them */
    const unsigned status = khdays_mission_field(3594, KHDAYS_RECORD_STATUS, &bits);
    CHECK(status == 0x28e4u + 3u * 3594u && bits == 3);
    CHECK(khdays_field_store_get(&s, status, 3, &v) && v == 0);
    CHECK(khdays_field_store_set(&s, status, 3, 2));
    CHECK(khdays_field_store_get(&s, status, 3, &v) && v == 2);
    CHECK(s.progress[0].value[KHDAYS_RECORD_STATUS] == 2 && s.progress[1].value[KHDAYS_RECORD_STATUS] == 0);
    const unsigned badges = khdays_mission_field(3595, KHDAYS_RECORD_BADGES, &bits);
    CHECK(bits == 4 && khdays_field_store_set(&s, badges, 4, 0x5));
    CHECK(s.progress[1].value[KHDAYS_RECORD_BADGES] == 0x5);
    /* a flag of a field's bit: the field's first bit its value's highest (BitArray_TestBit) */
    CHECK(khdays_field_store_get(&s, badges, 1, &v) && v == 0);
    CHECK(khdays_field_store_get(&s, badges + 1, 1, &v) && v == 1);
    CHECK(khdays_field_store_get(&s, badges + 3, 1, &v) && v == 1);
    CHECK(khdays_field_store_set(&s, badges, 1, 1));
    CHECK(s.progress[1].value[KHDAYS_RECORD_BADGES] == 0xd);
    /* bits beyond the mission's own, or a game field: not the store's, nothing written */
    CHECK(!khdays_field_store_get(&s, badges + 3, 2, &v));
    CHECK(!khdays_field_store_set(&s, badges + 3, 2, 3));
    CHECK(s.progress[1].value[KHDAYS_RECORD_BADGES] == 0xd);
    CHECK(!khdays_field_store_get(&s, 0x28e4u + 3u * 7u, 3, &v));
    CHECK(!khdays_field_store_set(&s, 0x3fff, 1, 1));
    khdays_field_store_clear(&s);
    CHECK(s.progress[1].value[KHDAYS_RECORD_BADGES] == 0 && s.progress[0].best == -1);
    khdays_field_store_free(&s);
}

static void the_save_part(void)
{
    static const char text[] = "[mission a]\nname en = A\nobjective en = O\nworld = tt\n"
                               "[mission b]\nname en = B\nobjective en = O\nworld = tt\n";
    static const char saved[] = "# a comment\r\n"
                                "Mod/b 2 1 0 4 12345\r\n"
                                "Gone/x 3 3 3 15 7\n"
                                "Mod/a 1 0 0 0 -1\n"
                                "nonsense\n";
    KhdaysMissions r = {0};
    KhdaysFieldStore s = {0};
    KhdaysSavePart kept = {0};
    CHECK(read_text(&r, "Mod", text, &(Reports){0}) == 2);
    CHECK(khdays_missions_number(&r, &s, NULL, NULL) == 2);
    s.progress[0].value[KHDAYS_RECORD_STAGE] = 3; /* the game's progress before the load: gone after it */
    khdays_save_part_read(saved, sizeof(saved) - 1, &r, &s, &kept);
    CHECK(s.progress[0].value[KHDAYS_RECORD_STATUS] == 1 && s.progress[0].value[KHDAYS_RECORD_STAGE] == 0);
    CHECK(s.progress[1].value[KHDAYS_RECORD_STATUS] == 2 && s.progress[1].value[KHDAYS_RECORD_STAGE] == 1);
    CHECK(s.progress[1].value[KHDAYS_RECORD_BADGES] == 4 && s.progress[1].best == 12345);
    CHECK(kept.kept_count == 1 && strcmp(kept.kept[0], "Gone/x 3 3 3 15 7") == 0);
    size_t size = 0;
    char *out = khdays_save_part_text(&r, &s, &kept, &size);
    CHECK(out != NULL && strlen(out) == size);
    if (out != NULL) {
        CHECK(strstr(out, "Mod/a 1 0 0 0 -1\n") != NULL);
        CHECK(strstr(out, "Mod/b 2 1 0 4 12345\n") != NULL);
        CHECK(strstr(out, "Gone/x 3 3 3 15 7\n") != NULL);
        /* read back: the same */
        KhdaysFieldStore s2 = {0};
        KhdaysMissions r2 = {0};
        KhdaysSavePart kept2 = {0};
        CHECK(read_text(&r2, "Mod", text, &(Reports){0}) == 2);
        CHECK(khdays_missions_number(&r2, &s2, NULL, NULL) == 2);
        khdays_save_part_read(out, size, &r2, &s2, &kept2);
        CHECK(memcmp(s2.progress, s.progress, 2 * sizeof(*s.progress)) == 0);
        CHECK(kept2.kept_count == 1);
        khdays_save_part_free(&kept2);
        khdays_field_store_free(&s2);
        khdays_missions_free(&r2);
    }
    free(out);
    /* no progress: no line */
    khdays_field_store_clear(&s);
    khdays_save_part_free(&kept);
    out = khdays_save_part_text(&r, &s, &kept, &size);
    CHECK(out != NULL && strchr(out, '/') == NULL);
    free(out);
    khdays_save_part_free(&kept);
    khdays_field_store_free(&s);
    khdays_missions_free(&r);
}

/* ---- The tables -------------------------------------------------------------------------------- */

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static unsigned rd32(const unsigned char *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

/* A list record of the given file, number and kind with `texts` (ASCII, each its own), as the game's. */
static size_t make_record(unsigned char *at, unsigned file, unsigned number, unsigned kind, const char *const *texts)
{
    size_t n = KHDAYS_LIST_TEXTS;
    memset(at, 0, KHDAYS_LIST_TEXTS);
    at[KHDAYS_LIST_FILE] = (unsigned char)file;
    at[KHDAYS_LIST_FILE + 1] = (unsigned char)(file >> 8);
    at[KHDAYS_LIST_NUMBER] = (unsigned char)number;
    at[KHDAYS_LIST_FIRST_DAY] = 8;
    at[KHDAYS_LIST_SHOWN] = (unsigned char)number;
    at[0x10] = (unsigned char)(kind + 3); /* (the template's own, kept: tells them apart) */
    at[KHDAYS_LIST_KIND] = (unsigned char)kind;
    for (int t = 0; t < 4; ++t) {
        for (const char *c = texts[t]; *c != '\0'; ++c) {
            at[n++] = (unsigned char)*c;
            at[n++] = 0;
        }
        at[n++] = 0;
        at[n++] = 0;
    }
    at[0] = (unsigned char)n;
    at[1] = (unsigned char)(n >> 8);
    return n;
}

/* The record's text `t` as ASCII into `out`. */
static void record_text(const unsigned char *record, int t, char *out, size_t size)
{
    const unsigned char *p = record + KHDAYS_LIST_TEXTS;
    for (int k = 0; k < t; ++k) {
        while (rd16(p) != 0) {
            p += 2;
        }
        p += 2;
    }
    size_t n = 0;
    for (; rd16(p) != 0 && n + 1 < size; p += 2) {
        out[n++] = (char)rd16(p);
    }
    out[n] = '\0';
}

static void list_rows(void)
{
    static const char *const mission[4] = {"Game", "Long words", "Short", ""};
    static const char *const other[4] = {"Game", "Other words", "Short", "%d"}; /* another kind's entry */
    static const char text[] = "[mission new]\nname en = New\nname fr = Nouveau\nobjective en = Win.\n"
                               "world = aw\ntemplate = 105\ntime = 90500\n"
                               "[mission plain]\nname en = Plain\nobjective en = Go.\nworld = tt\ncategory = 4\n";
    unsigned char file[2048];
    size_t size = 2;
    size += make_record(file + size, 105, 14, 0, mission);
    size += make_record(file + size, 105, 14, 1, other);
    size += make_record(file + size, 105, 14, 4, mission);
    size += make_record(file + size, 106, 20, 0, mission);
    file[0] = 4;
    file[1] = 0;
    KhdaysMissions r = {0};
    KhdaysFieldStore s = {0};
    CHECK(read_text(&r, "Mod", text, &(Reports){0}) == 2);
    CHECK(khdays_missions_number(&r, &s, NULL, NULL) == 2);
    size_t out_size = 0;
    unsigned char *out = khdays_tables_list(file, size, &r, 1, &out_size);
    CHECK(out != NULL);
    if (out != NULL) {
        /* the game's four, then each new mission's Mission Mode row alone (no story row, none of its template's
         * other entries) */
        CHECK(rd16(out) == 6 && memcmp(out + 2, file + 2, size - 2) == 0);
        const unsigned char *rec = out + size;
        CHECK(rd16(rec + KHDAYS_LIST_FILE) == (unsigned)r.list[0].file);
        CHECK(rd16(rec + KHDAYS_LIST_NUMBER) == (unsigned)r.list[0].number);
        CHECK(rd16(rec + KHDAYS_LIST_FIRST_DAY) == 0xffff && rd16(rec + KHDAYS_LIST_LAST_DAY) == 0xffff);
        CHECK(rd16(rec + KHDAYS_LIST_CATEGORY) == 1 && rd16(rec + KHDAYS_LIST_SHOWN) == 95);
        CHECK(rec[0x10] == 7 && rec[KHDAYS_LIST_KIND] == KHDAYS_ENTRY_MISSION && rd16(rec + KHDAYS_LIST_SECONDS) == 90);
        char words[64];
        record_text(rec, 0, words, sizeof(words));
        CHECK(strcmp(words, "Nouveau") == 0); /* French: its own */
        record_text(rec, 1, words, sizeof(words));
        CHECK(strcmp(words, "Win.") == 0); /* no description: the objective */
        record_text(rec, 2, words, sizeof(words));
        CHECK(strcmp(words, "Win.") == 0);
        const unsigned char *p = rec + rd16(rec);
        CHECK(rd16(p + KHDAYS_LIST_CATEGORY) == 4 && p[0x10] == 0 && rd16(p + KHDAYS_LIST_SHOWN) == 96);
        CHECK(p[KHDAYS_LIST_KIND] == KHDAYS_ENTRY_MISSION && rd16(p + KHDAYS_LIST_REWARD) == KHDAYS_REWARD_MISSION);
        record_text(p, 0, words, sizeof(words));
        CHECK(strcmp(words, "Plain") == 0); /* no French: the English */
        CHECK(p + rd16(p) == out + out_size);
    }
    free(out);
    /* a list that does not read: left as it is */
    file[0] = 9;
    CHECK(khdays_tables_list(file, size, &r, 0, &out_size) == NULL);
    khdays_field_store_free(&s);
    khdays_missions_free(&r);
}

static void mdb_rows(void)
{
    static const char text[] = "[mission new]\nname en = N\nobjective en = O\nworld = tt\ntemplate = 105\nlevel = 30\n"
                               "[mission none]\nname en = N\nobjective en = O\nworld = tt\n";
    unsigned char file[4 + 2 * KHDAYS_MDB_ROW];
    memset(file, 0, sizeof(file));
    file[0] = 2;
    file[4] = 104;
    file[4 + KHDAYS_MDB_ROW] = 105;
    file[4 + KHDAYS_MDB_ROW + 4] = 7;
    file[4 + KHDAYS_MDB_ROW + KHDAYS_MDB_LEVEL] = 9;
    KhdaysMissions r = {0};
    KhdaysFieldStore s = {0};
    CHECK(read_text(&r, "Mod", text, &(Reports){0}) == 2);
    CHECK(khdays_missions_number(&r, &s, NULL, NULL) == 2);
    size_t out_size = 0;
    unsigned char *out = khdays_tables_mdb(file, sizeof(file), &r, &out_size);
    CHECK(out != NULL && out_size == 4 + 4 * KHDAYS_MDB_ROW && rd32(out) == 4);
    if (out != NULL) {
        const unsigned char *row = out + 4 + 2 * KHDAYS_MDB_ROW;
        CHECK(rd16(row) == (unsigned)r.list[0].file && row[4] == 7 && rd32(row + KHDAYS_MDB_LEVEL) == 30);
        CHECK(rd32(row + KHDAYS_MDB_LEVEL_SESSION) == 30);
        row += KHDAYS_MDB_ROW;
        CHECK(rd16(row) == (unsigned)r.list[1].file && row[4] == 0);
    }
    free(out);
    khdays_field_store_free(&s);
    khdays_missions_free(&r);
}

static unsigned char *read_whole(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = n > 0 ? malloc((size_t)n) : NULL;
    if (data != NULL && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return data;
}

/* The game's own list in each language and its missions' table, with 20 missions appended: every record read
 * back by the game's walk, the game's untouched. */
static void the_games_tables(const char *decompressed)
{
    static const char block[] = "[mission m%d]\nname en = Mission %d\nobjective en = Win.\nworld = tt\ntemplate = %d\n";
    char text[4096], path[600];
    size_t used = 0;
    for (int i = 0; i < 20; ++i) {
        used += (size_t)snprintf(text + used, sizeof(text) - used, block, i, i, i % 2 == 0 ? 105 : 10);
    }
    KhdaysMissions r = {0};
    KhdaysFieldStore s = {0};
    CHECK(read_text(&r, "Mod", text, &(Reports){0}) == 20);
    CHECK(khdays_missions_number(&r, &s, NULL, NULL) == 20);
    for (int l = 0; l < KHDAYS_MISSION_LANGUAGES; ++l) {
        size_t size = 0, out_size = 0;
        snprintf(path, sizeof(path), "%s/UI/cm/msl_%s.msi", decompressed, khdays_mission_language(l));
        unsigned char *file = read_whole(path, &size);
        CHECK(file != NULL);
        if (file == NULL) {
            continue;
        }
        unsigned char *out = khdays_tables_list(file, size, &r, l, &out_size);
        CHECK(out != NULL && memcmp(out + 2, file + 2, size - 2) == 0);
        if (out != NULL) {
            const unsigned count = rd16(out), game = rd16(file);
            unsigned mine = 0;
            size_t at = 2;
            for (unsigned i = 0; i < count && at + KHDAYS_LIST_TEXTS <= out_size; ++i) {
                if (i >= game) {
                    mine += rd16(out + at + KHDAYS_LIST_FILE) >= KHDAYS_FILE_FIRST;
                }
                at += rd16(out + at);
            }
            CHECK(at == out_size && mine == count - game);
            CHECK(count == game + 20); /* each its Mission Mode row alone */
        }
        free(out);
        free(file);
    }
    size_t size = 0, out_size = 0;
    snprintf(path, sizeof(path), "%s/mi/mi/mdb", decompressed);
    unsigned char *file = read_whole(path, &size);
    CHECK(file != NULL);
    if (file != NULL) {
        unsigned char *out = khdays_tables_mdb(file, size, &r, &out_size);
        CHECK(out != NULL && rd32(out) == rd32(file) + 20 && out_size == size + 20 * KHDAYS_MDB_ROW);
        CHECK(out != NULL && memcmp(out + 4, file + 4, size - 4) == 0);
        free(out);
        free(file);
    }
    khdays_field_store_free(&s);
    khdays_missions_free(&r);
}

int main(int argc, char **argv)
{
    a_description_read();
    bad_blocks_left_out();
    waves_chests_and_emblems_read();
    chests_kept();
    numbering();
    the_field_store();
    the_save_part();
    list_rows();
    mdb_rows();
    if (argc >= 2 && argv[1][0] != '\0') {
        the_games_tables(argv[1]);
    } else {
        printf("missions: the game's own tables skipped (no extracted data)\n");
    }
    if (failures == 0) {
        printf("missions: all checks passed\n");
    }
    return failures != 0;
}
