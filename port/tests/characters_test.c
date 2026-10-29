/* The mods' characters (runtime/characters/): descriptions read and refused, their identities and numbers, their
 * movesets in their combo files -- on made-up descriptions and files only. */
#include "../runtime/characters/moveset.h"
#include "../runtime/characters/registry.h"

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

static void reported(const char *mod, const char *key, int line, const char *why, void *context)
{
    Reports *r = context;
    (void)mod;
    (void)key;
    if (r->count < 16) {
        r->lines[r->count] = line;
    }
    ++r->count;
    snprintf(r->last, sizeof(r->last), "%s", why);
}

static int read_text(KhdaysCharacters *r, const char *mod, const char *key, const char *text, Reports *reports)
{
    return khdays_characters_read(r, mod, mod, key, text, strlen(text), reported, reports);
}

static const char block[] = "\xef\xbb\xbf# a made-up character\n"
                            "[character]\n"
                            "format = 1\n"
                            "key = block\n"
                            "base = Roxas          # plays as him\n"
                            "name = Block\n"
                            "name es = Bloque\n"
                            "weapon = Stick\n"
                            "face = face.png\n"
                            "locked = base\n"
                            "gauges = 1 2 3 4 5 6 7 8 9\n"
                            "\n"
                            "[model]\n"
                            "file = model/block.glb\n"
                            "skeleton = paired\n";

static void a_description_read(void)
{
    KhdaysCharacters r;
    Reports reports;
    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    CHECK(read_text(&r, "test.characters", "block", block, &reports) == 1);
    CHECK(reports.count == 0);
    CHECK(r.count == 1);
    const KhdaysCharacter *c = &r.list[0];
    CHECK(strcmp(c->identity, "test.characters/block") == 0);
    CHECK(strcmp(c->key, "block") == 0);
    CHECK(c->base == 0);
    CHECK(strcmp(khdays_characters_name(c, 0), "Block") == 0);  /* en: the one without a language */
    CHECK(strcmp(khdays_characters_name(c, 4), "Bloque") == 0); /* es */
    CHECK(strcmp(khdays_characters_weapon(c, 2), "Stick") == 0);
    CHECK(strcmp(c->face, "face.png") == 0);
    CHECK(strcmp(c->model, "model/block.glb") == 0);
    CHECK(c->own_skeleton == 0);
    CHECK(c->locked_with_base == 1);
    CHECK(c->has_gauges == 1 && c->gauges[0] == 1 && c->gauges[8] == 9);
    CHECK(c->number == 0);
    khdays_characters_free(&r);
}

/* Each broken description is left out with its reason and, for a bad line, the line. */
static void bad_descriptions_left_out(void)
{
    static const struct {
        const char *key, *text, *reason;
        int line;
    } cases[] = {
        {"newer", "[character]\nformat = 2\nbase = roxas\nname = A\n[model]\nfile = a.glb\n", "newer than this port",
         2},
        {"nobase", "[character]\nformat = 1\nname = A\n[model]\nfile = a.glb\n", "base missing", 0},
        {"badbase", "[character]\nformat = 1\nbase = cloud\nname = A\n[model]\nfile = a.glb\n", "not one of the game's",
         3},
        {"noname", "[character]\nformat = 1\nbase = axel\n[model]\nfile = a.glb\n", "name missing", 0},
        {"nomodel", "[character]\nformat = 1\nbase = axel\nname = A\n", "model missing", 0},
        {"noformat", "[character]\nbase = axel\nname = A\n[model]\nfile = a.glb\n", "format missing", 0},
        {"up", "[character]\nformat = 1\nbase = axel\nname = A\n[model]\nfile = ../a.glb\n", "inside the character's",
         6},
        {"root", "[character]\nformat = 1\nbase = axel\nname = A\nface = C:/a.png\n[model]\nfile = a.glb\n",
         "inside the character's", 5},
        {"gauges", "[character]\nformat = 1\nbase = axel\nname = A\ngauges = 1 2 3\n[model]\nfile = a.glb\n", "gauges",
         5},
        {"bigauge",
         "[character]\nformat = 1\nbase = axel\nname = A\ngauges = 1 2 3 4 5 6 7 8 256\n[model]\nfile = a.glb\n",
         "gauges", 5},
        {"otherkey", "[character]\nformat = 1\nkey = other\nbase = axel\nname = A\n[model]\nfile = a.glb\n",
         "folder's name", 3},
        {"noequals", "[character]\nformat = 1\nbase axel\n", "name = value", 3},
        {"before", "format = 1\n", "before any part", 1},
        {"skeleton", "[character]\nformat = 1\nbase = axel\nname = A\n[model]\nfile = a.glb\nskeleton = mine\n",
         "paired or own", 7},
        {"bad key!", "[character]\nformat = 1\nbase = axel\nname = A\n[model]\nfile = a.glb\n", "not a key", 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        KhdaysCharacters r;
        Reports reports;
        memset(&r, 0, sizeof(r));
        memset(&reports, 0, sizeof(reports));
        const int taken = read_text(&r, "test.characters", cases[i].key, cases[i].text, &reports);
        if (taken != 0 || reports.count != 1 || strstr(reports.last, cases[i].reason) == NULL ||
            reports.lines[0] != cases[i].line) {
            fprintf(stderr, "case %s: taken %d, %d reports, line %d, \"%s\"\n", cases[i].key, taken, reports.count,
                    reports.lines[0], reports.last);
            ++failures;
        }
        CHECK(r.count == 0);
        khdays_characters_free(&r);
    }
}

/* An unknown name or part is told, the character still taken. */
static void unknown_names_told(void)
{
    static const char text[] = "[character]\nformat = 1\nbase = goofy\nname = A\nhat = big\n[model]\nfile = a.glb\n"
                               "[voices]\nhit = a.wav\n";
    KhdaysCharacters r;
    Reports reports;
    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    CHECK(read_text(&r, "m", "a", text, &reports) == 1);
    CHECK(reports.count == 2);
    CHECK(reports.lines[0] == 5 && reports.lines[1] == 8);
    CHECK(r.count == 1 && r.list[0].base == 18);
    khdays_characters_free(&r);
}

static void numbering(void)
{
    static const char text[] = "[character]\nformat = 1\nbase = xion\nname = A\n[model]\nfile = a.glb\n";
    KhdaysCharacters r;
    Reports reports;
    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    /* read in one order, numbered in their identities' */
    CHECK(read_text(&r, "zeta", "one", text, &reports) == 1);
    CHECK(read_text(&r, "alpha", "two", text, &reports) == 1);
    CHECK(read_text(&r, "alpha", "one", text, &reports) == 1);
    CHECK(read_text(&r, "alpha", "one", text, &reports) == 1); /* the same identity again (another folder) */
    CHECK(reports.count == 0);
    CHECK(khdays_characters_number(&r, reported, &reports) == 3);
    CHECK(reports.count == 1 && strstr(reports.last, "same identity") != NULL);
    CHECK(r.list[2].number == KHDAYS_CHARACTER_FIRST);     /* alpha/one */
    CHECK(r.list[1].number == KHDAYS_CHARACTER_FIRST + 1); /* alpha/two */
    CHECK(r.list[0].number == KHDAYS_CHARACTER_FIRST + 2); /* zeta/one */
    CHECK(r.list[3].number == 0);
    CHECK(khdays_characters_by_number(&r, KHDAYS_CHARACTER_FIRST + 1) == &r.list[1]);
    CHECK(khdays_characters_by_number(&r, 0) == NULL);
    CHECK(khdays_characters_by_number(&r, KHDAYS_CHARACTER_FIRST + 3) == NULL);
    /* taken out before numbering: the rest move up */
    khdays_characters_drop(&r, 3);
    CHECK(r.count == 3);
    khdays_characters_free(&r);
}

/* Past the byte's last number: the rest are left out, told. */
static void the_bytes_limit(void)
{
    static const char text[] = "[character]\nformat = 1\nbase = riku\nname = A\n[model]\nfile = a.glb\n";
    const int room = KHDAYS_CHARACTER_LAST - KHDAYS_CHARACTER_FIRST + 1;
    KhdaysCharacters r;
    Reports reports;
    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    for (int i = 0; i < room + 2; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "c%04d", i);
        CHECK(read_text(&r, "many", key, text, &reports) == 1);
    }
    CHECK(khdays_characters_number(&r, reported, &reports) == room);
    CHECK(reports.count == 2 && strstr(reports.last, "signed byte") != NULL);
    CHECK(r.list[room + 1].number == 0 && r.list[room - 1].number == KHDAYS_CHARACTER_LAST);
    khdays_characters_free(&r);
}

/* A skeleton of its own: its clips by the base's animations, its size, its weapon's bone. */
static void own_skeleton_read(void)
{
    static const char text[] = "[character]\nformat = 1\nbase = roxas\nname = Wind\n"
                               "[model]\nfile = model/wind.fbx\nskeleton = own\nscale = 100   # centimetres\n"
                               "weapon_bone = R_buki\nweapon_turn = 0 90 -45\n"
                               "[actions]\nfallback = clips/idle.fbx\n"
                               "b_ro_A000 = clips/idle.fbx\n"
                               "b_ro_A001 = clips/run.fbx blend 120\n"
                               "b_ro_B009 = clips/hit1.fbx root keep blend 0 time game\n";
    KhdaysCharacters r;
    Reports reports;
    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    CHECK(read_text(&r, "m", "wind", text, &reports) == 1);
    CHECK(reports.count == 0);
    const KhdaysCharacter *c = &r.list[0];
    CHECK(c->own_skeleton == 1);
    CHECK(c->model_scale == 100.0f);
    CHECK(strcmp(c->weapon_bone, "R_buki") == 0);
    CHECK(c->has_weapon_turn == 1 && c->weapon_turn[1] == 90.0f && c->weapon_turn[2] == -45.0f);
    CHECK(strcmp(c->fallback, "clips/idle.fbx") == 0);
    CHECK(c->action_count == 3);
    const KhdaysCharacterAction *run = khdays_characters_action(c, "b_ro_A001");
    CHECK(run != NULL && strcmp(run->clip, "clips/run.fbx") == 0 && run->blend_ms == 120 && run->root_keep == 0);
    const KhdaysCharacterAction *hit = khdays_characters_action(c, "b_ro_B009");
    CHECK(hit != NULL && hit->blend_ms == 0 && hit->root_keep == 1 && hit->game_time == 1);
    const KhdaysCharacterAction *idle = khdays_characters_action(c, "b_ro_A000");
    CHECK(idle != NULL && idle->blend_ms == KHDAYS_CHARACTER_BLEND_MS && idle->game_time == 0);
    CHECK(khdays_characters_action(c, "b_ro_A004") == NULL);
    khdays_characters_free(&r);

    static const struct {
        const char *text, *reason;
        int line;
    } bad[] = {
        {"[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\nscale = -2\n", "above 0", 7},
        {"[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\nweapon_turn = 1 2\n", "three angles",
         7},
        {"[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n[actions]\nb_ro_A000 =\n",
         "clip missing", 8},
        {"[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n[actions]\nb_ro_A000 = ../x.fbx\n",
         "inside the character's", 8},
        {"[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n[actions]\nb_ro_A000 = x.fbx blend soon\n",
         "not milliseconds", 8},
        {"[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n[actions]\nb_ro_A000 = x.fbx loop yes\n",
         "not blend <ms>", 8},
        {"[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n[actions]\nb_ro_A000 = x.fbx root\n",
         "without its value", 8},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        KhdaysCharacters b;
        memset(&b, 0, sizeof(b));
        memset(&reports, 0, sizeof(reports));
        const int taken = read_text(&b, "m", "bad", bad[i].text, &reports);
        if (taken != 0 || reports.count != 1 || strstr(reports.last, bad[i].reason) == NULL ||
            reports.lines[0] != bad[i].line) {
            fprintf(stderr, "own case %zu: taken %d, %d reports, line %d, \"%s\"\n", i, taken, reports.count,
                    reports.lines[0], reports.last);
            ++failures;
        }
        khdays_characters_free(&b);
    }
}

/* Its own sounds for the base's: a file, silence, every sound a name begins. */
static void sounds_read(void)
{
    static const char text[] = "[character]\nformat = 1\nbase = roxas\nname = Wind\n[model]\nfile = a.fbx\n"
                               "[sounds]\n"
                               "seqarc/PLY_ROX/vo_rs02at07 = voices/attack 1.wav\n"
                               "seqarc/PLY_ROX/* = none\n"
                               "seqarc/PLY_ROX/vo_* = voices/any.ogg\n"
                               "seqarc/PLY_ROX/vo_rs02at03 = voices/old.wav\n"
                               "seqarc/PLY_ROX/vo_rs02at03 = NONE   # a later line replaces it\n";
    KhdaysCharacters r;
    Reports reports;
    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    CHECK(read_text(&r, "m", "wind", text, &reports) == 1);
    CHECK(reports.count == 0);
    const KhdaysCharacter *c = &r.list[0];
    CHECK(c->sound_count == 4);
    const KhdaysCharacterSound *s = khdays_characters_sound(c, "seqarc/PLY_ROX/vo_rs02at07");
    CHECK(s != NULL && strcmp(s->file, "voices/attack 1.wav") == 0);
    s = khdays_characters_sound(c, "seqarc/PLY_ROX/vo_rs02at03");
    CHECK(s != NULL && s->file == NULL);
    s = khdays_characters_sound(c, "seqarc/PLY_ROX/vo_rs02at05"); /* the longest that begins it */
    CHECK(s != NULL && strcmp(s->file, "voices/any.ogg") == 0);
    s = khdays_characters_sound(c, "seqarc/PLY_ROX/se_step");
    CHECK(s != NULL && s->file == NULL);
    CHECK(khdays_characters_sound(c, "seqarc/WEP_ROX26/se_rox_weap_swing03") == NULL);
    CHECK(khdays_characters_sound(NULL, "seqarc/PLY_ROX/vo_rs02at07") == NULL);
    khdays_characters_free(&r);

    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    CHECK(read_text(&r, "m", "bad",
                    "[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n"
                    "[sounds]\nseqarc/PLY_ROX/vo_a = ../b.wav\n",
                    &reports) == 0);
    CHECK(reports.count == 1 && reports.lines[0] == 8 && strstr(reports.last, "neither none") != NULL);
    khdays_characters_free(&r);
}

/* Its moveset: combos of the base's attacks, attacks changed. */
static void moveset_read(void)
{
    static const char text[] = "[character]\nformat = 1\nbase = roxas\nname = Wind\n[model]\nfile = a.fbx\n"
                               "[moveset]\n"
                               "ground = 7 1  0\n"
                               "ground finisher = 2\n"
                               "air finisher = 5\n"
                               "attack 1 = damage 2 reach 1.5 push 0\n"
                               "attack 7 = push 3\n"
                               "[limit]\ndamage = 2.5\n";
    KhdaysCharacters r;
    Reports reports;
    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    CHECK(read_text(&r, "m", "wind", text, &reports) == 1);
    CHECK(reports.count == 0);
    const KhdaysCharacter *c = &r.list[0];
    const KhdaysCharacterCombo *ground = &c->combos[KHDAYS_CHARACTER_GROUND], *air = &c->combos[KHDAYS_CHARACTER_AIR];
    CHECK(ground->count == 3 && ground->attacks[0] == 7 && ground->attacks[2] == 0 && ground->finisher == 2);
    CHECK(air->count == 0 && air->finisher == 5);
    CHECK(c->attack_count == 2);
    CHECK(c->attacks[0].id == 1 && c->attacks[0].damage == 2.0f && c->attacks[0].reach == 1.5f &&
          c->attacks[0].push == 0.0f);
    CHECK(c->attacks[1].id == 7 && c->attacks[1].damage == 1.0f && c->attacks[1].push == 3.0f);
    CHECK(khdays_moveset_wanted(c));
    CHECK(c->own_moves_damage == 2.5f);
    khdays_characters_free(&r);

    memset(&r, 0, sizeof(r));
    CHECK(read_text(&r, "m", "plain", "[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n",
                    &reports) == 1);
    CHECK(r.list[0].combos[0].finisher == -1 && !khdays_moveset_wanted(&r.list[0]));
    CHECK(r.list[0].own_moves_damage == 1.0f); /* the base's */
    khdays_characters_free(&r);

    static const struct {
        const char *line, *reason;
    } bad[] = {
        {"ground = 1 x\n", "not an attack's number"},          {"ground =\n", "no attacks"},
        {"air finisher = -1\n", "not an attack's number"},     {"attack x = damage 2\n", "attack <number>"},
        {"attack 1 = speed 2\n", "not damage, reach or push"}, {"attack 1 = damage -1\n", "0 or more"},
        {"attack 1 = damage\n", "not damage, reach or push"},  {"combo = 1 2\n", "attack <number>"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char description[300];
        KhdaysCharacters b;
        memset(&b, 0, sizeof(b));
        memset(&reports, 0, sizeof(reports));
        snprintf(description, sizeof(description),
                 "[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n[moveset]\n%s", bad[i].line);
        const int taken = read_text(&b, "m", "bad", description, &reports);
        if (taken != 0 || reports.count != 1 || strstr(reports.last, bad[i].reason) == NULL || reports.lines[0] != 8) {
            fprintf(stderr, "moveset case %zu: taken %d, %d reports, line %d, \"%s\"\n", i, taken, reports.count,
                    reports.lines[0], reports.last);
            ++failures;
        }
        khdays_characters_free(&b);
    }
}

/* Made-up combo files, laid out as the game's (moveset.h). */
typedef struct Bytes {
    unsigned char data[8192];
    size_t size;
} Bytes;

static void put_word(Bytes *b, size_t at, unsigned v)
{
    for (int i = 0; i < 4; ++i) {
        b->data[at + (size_t)i] = (unsigned char)(v >> (8 * i));
    }
}

static void word(Bytes *b, unsigned v)
{
    put_word(b, b->size, v);
    b->size += 4;
}

static unsigned word_of(const unsigned char *d, size_t at)
{
    return (unsigned)d[at] | (unsigned)d[at + 1] << 8 | (unsigned)d[at + 2] << 16 | (unsigned)d[at + 3] << 24;
}

static int half_of(const unsigned char *d, size_t at)
{
    return (short)(d[at] | d[at + 1] << 8);
}

/* cm.b: attacks (number, kind, animation, hits), each hit the same made-up sweep. */
typedef struct MadeAttack {
    int id, mode, animation, hits;
} MadeAttack;

static void make_cm(Bytes *b, const MadeAttack *a, int n)
{
    b->size = 0;
    word(b, (unsigned)n);
    for (int i = 0; i < n; ++i) {
        word(b, 0);
    }
    for (int i = 0; i < n; ++i) {
        put_word(b, 4 + (size_t)i * 4, (unsigned)b->size);
        word(b, (unsigned)a[i].id);
        word(b, (unsigned)a[i].mode);
        word(b, (unsigned)a[i].animation);
        for (int w = 3; w < 14; ++w) {
            word(b, 0);
        }
        word(b, (unsigned)a[i].hits);
        for (int h = 0; h < a[i].hits; ++h) {
            static const unsigned hit[15] = {4096, 6 << 12, 12 << 12, 100,  200, 0, 0, 0xffffffffu,
                                             1000, 4300,    2000,     3000, 0,   0, 31};
            for (int w = 0; w < 15; ++w) {
                word(b, hit[w]);
            }
        }
    }
}

/* ci.b: two keyblades, each a ground graph 0>1 1>E, finisher 2, and an air one 3>E, no finisher, branch 7. */
static void make_ci(Bytes *b)
{
    b->size = 0;
    word(b, 2);
    b->size += 2 * 0x34;
    for (int k = 0; k < 2; ++k) {
        const size_t record = 4 + (size_t)k * 0x34;
        static const int nodes[2][3][2][3] = {{{{0, 1, -1}, {1, 0x80, -1}}, {{2, -1, -1}}, {{0}}},
                                              {{{3, 0x80, -1}}, {{0}}, {{7, -1, -1}}}};
        static const int counts[2][3] = {{2, 1, 0}, {1, 0, 1}};
        static const short entries[2][4] = {{0, 1, -1, -1}, {0, 0, -1, -1}};
        for (int g = 0; g < 2; ++g) {
            for (int p = 0; p < 3; ++p) {
                put_word(b, record + (size_t)(g * 3 + p) * 4, (unsigned)b->size);
                b->data[record + 0x18 + g * 6 + p * 2] = (unsigned char)counts[g][p];
                b->data[record + 0x19 + g * 6 + p * 2] = 0;
                for (int i = 0; i < counts[g][p]; ++i) {
                    word(b, (unsigned)nodes[g][p][i][0]);
                    word(b, (unsigned)(nodes[g][p][i][1] & 0xffff) | (unsigned)(nodes[g][p][i][2] & 0xffff) << 16);
                }
            }
            for (int e = 0; e < 4; ++e) {
                b->data[record + 0x24 + g * 8 + e * 2] = (unsigned char)entries[g][e];
                b->data[record + 0x25 + g * 8 + e * 2] = (unsigned char)(entries[g][e] >> 8);
            }
        }
    }
}

/* Keyblade k's graph g's pool p in a ci.b: its first node's place and its count. */
static size_t pool_of(const unsigned char *ci, int k, int g, int p, int *count)
{
    const unsigned char *record = ci + 4 + k * 0x34;
    *count = half_of(record, 0x18 + (size_t)(g * 6 + p * 2));
    return word_of(record, (size_t)(g * 3 + p) * 4);
}

static void moveset_files(void)
{
    static const MadeAttack attacks[] = {{0, 0, 0, 1}, {1, 0, 1, 2}, {2, 0, 2, 1}, {3, 1, 3, 0}, {7, 0, 0, 1}};
    static const char text[] = "[character]\nformat = 1\nbase = roxas\nname = Wind\n[model]\nfile = a.fbx\n"
                               "[moveset]\nground = 7 1 0\nground finisher = 2\nattack 1 = damage 2 reach 1.5 push 0\n";
    Bytes ci, cm;
    KhdaysCharacters r;
    Reports reports;
    char why[300];
    unsigned char *out = NULL;
    size_t out_size = 0;
    make_cm(&cm, attacks, 5);
    make_ci(&ci);
    memset(&r, 0, sizeof(r));
    memset(&reports, 0, sizeof(reports));
    CHECK(read_text(&r, "m", "wind", text, &reports) == 1);
    const KhdaysCharacter *c = &r.list[0];

    CHECK(khdays_moveset_attacks(c, cm.data, cm.size, why, sizeof(why)) == 1);
    const size_t one = word_of(cm.data, 4 + 1 * 4), seven = word_of(cm.data, 4 + 4 * 4);
    for (int h = 0; h < 2; ++h) {
        const size_t hit = one + 60 + (size_t)h * 60;
        CHECK(word_of(cm.data, hit) == 8192);                                                   /* damage x2 */
        CHECK(word_of(cm.data, hit + 3 * 4) == 0 && word_of(cm.data, hit + 4 * 4) == 0);        /* push x0 */
        CHECK(word_of(cm.data, hit + 8 * 4) == 1500 && word_of(cm.data, hit + 10 * 4) == 3000); /* out x1.5 */
        CHECK(word_of(cm.data, hit + 9 * 4) == 4300);                                           /* not its height */
        CHECK(word_of(cm.data, hit + 11 * 4) == 4500);                                          /* radius x1.5 */
        CHECK(word_of(cm.data, hit + 1 * 4) == 6 << 12);                                        /* nor its frames */
    }
    CHECK(word_of(cm.data, seven + 60) == 4096); /* the others as they were */

    CHECK(khdays_moveset_combos(c, ci.data, ci.size, cm.data, cm.size, &out, &out_size, why, sizeof(why)) == 1);
    for (int k = 0; k < 2 && out != NULL; ++k) {
        int count;
        size_t at = pool_of(out, k, 0, 0, &count);
        CHECK(count == 3 && at + 3 * 8 <= out_size);
        CHECK(word_of(out, at) == 7 && half_of(out, at + 4) == 1 && half_of(out, at + 6) == -1);
        CHECK(word_of(out, at + 8) == 1 && half_of(out, at + 12) == 2);
        CHECK(word_of(out, at + 16) == 0 && half_of(out, at + 20) == 0x80);
        at = pool_of(out, k, 0, 1, &count);
        CHECK(count == 1 && word_of(out, at) == 2 && half_of(out, at + 4) == -1);
        pool_of(out, k, 0, 2, &count);
        CHECK(count == 0);
        const unsigned char *record = out + 4 + k * 0x34;
        CHECK(half_of(record, 0x24) == 0 && half_of(record, 0x26) == 2 && half_of(record, 0x28) == -1);
        /* the air graph as the keyblade's */
        at = pool_of(out, k, 1, 0, &count);
        CHECK(count == 1 && word_of(out, at) == 3 && half_of(out, at + 4) == 0x80);
        pool_of(out, k, 1, 1, &count);
        CHECK(count == 0);
        at = pool_of(out, k, 1, 2, &count);
        CHECK(count == 1 && word_of(out, at) == 7);
        CHECK(half_of(record, 0x2c) == 0 && half_of(record, 0x2e) == 0);
    }
    free(out);
    khdays_characters_free(&r);

    /* refused: an attack its base lacks, hits of another kind, past the game's tables */
    static const char *const refused[][2] = {
        {"ground = 0 9\n", "no such attack"},
        {"attack 5 = damage 2\n", "no such attack"},
        {"attack 3 = damage 2\n", "not the sweeping kind"},
        {"ground = 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116\n", "more than 16 animations"},
    };
    MadeAttack many[22];
    memcpy(many, attacks, sizeof(attacks));
    for (int i = 0; i < 17; ++i) {
        many[5 + i] = (MadeAttack){100 + i, 0, 10 + i, 1};
    }
    make_cm(&cm, many, 22);
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        char description[400];
        memset(&r, 0, sizeof(r));
        snprintf(description, sizeof(description),
                 "[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.fbx\n[moveset]\n%s",
                 refused[i][0]);
        CHECK(read_text(&r, "m", "x", description, &reports) == 1);
        why[0] = '\0';
        const int done =
            khdays_moveset_attacks(&r.list[0], cm.data, cm.size, why, sizeof(why)) &&
            khdays_moveset_combos(&r.list[0], ci.data, ci.size, cm.data, cm.size, &out, &out_size, why, sizeof(why));
        if (done || strstr(why, refused[i][1]) == NULL) {
            fprintf(stderr, "refused moveset %zu: done %d, \"%s\"\n", i, done, why);
            ++failures;
        }
        if (done) {
            free(out);
        }
        khdays_characters_free(&r);
    }
}

static void the_bases(void)
{
    CHECK(strcmp(khdays_character_base_name(0), "roxas") == 0);
    CHECK(strcmp(khdays_character_base_code(0), "ro") == 0);
    CHECK(strcmp(khdays_character_base_code(14), "xo") == 0); /* Xion's folder */
    CHECK(strcmp(khdays_character_base_code(18), "go") == 0);
    CHECK(khdays_character_base_name(KHDAYS_CHARACTER_BASES) == NULL);
}

int main(void)
{
    a_description_read();
    bad_descriptions_left_out();
    unknown_names_told();
    numbering();
    the_bytes_limit();
    own_skeleton_read();
    sounds_read();
    moveset_read();
    moveset_files();
    the_bases();
    if (failures == 0) {
        printf("characters: all checks passed\n");
    }
    return failures != 0;
}
