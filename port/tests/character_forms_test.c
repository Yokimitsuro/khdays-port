/* The characters' forms (runtime/characters/): rules parsed and evaluated, forms read with what they take from their
 * character, chosen by their rules, numbered; level tables made from [stats] -- on made-up descriptions only. */
#include "../runtime/characters/registry.h"
#include "../runtime/characters/rules.h"
#include "../runtime/characters/stats.h"

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

typedef struct Reports {
    int count;
    char all[2000];
} Reports;

static void reported(const char *mod, const char *key, int line, const char *why, void *context)
{
    Reports *r = context;
    (void)mod;
    (void)key;
    ++r->count;
    const size_t n = strlen(r->all);
    snprintf(r->all + n, sizeof(r->all) - n, "%d: %s\n", line, why);
}

/* The Zero Gear with three Ability Units on it, and a Fire set. */
static const unsigned short zero_gear_panels[] = {230, 241, 241, 241, 93};
static const unsigned short three_units[] = {241, 241, 241};
static const unsigned short two_units[] = {241, 241};

static KhdaysFormFacts facts(int gear, const unsigned short *links, int link_count, int dual, int level)
{
    KhdaysFormFacts f;
    memset(&f, 0, sizeof(f));
    f.panels = zero_gear_panels;
    f.panel_count = 5;
    f.gear = gear;
    f.gear_links = links;
    f.gear_link_count = link_count;
    f.dual = dual;
    f.level = level;
    f.hp_percent = -1;
    return f;
}

static int holds(const char *text, const KhdaysFormFacts *f)
{
    char why[200] = "";
    KhdaysRule *r = khdays_rule_parse(text, why, sizeof(why));
    if (r == NULL) {
        fprintf(stderr, "rule %s: %s\n", text, why);
        ++failures;
        return -1;
    }
    const int v = khdays_rule_holds(r, f);
    khdays_rule_free(r);
    return v;
}

static int answers_yes(const char *name, int seat, int number, void *user)
{
    (void)seat;
    (void)number;
    (void)user;
    return strcmp(name, "test.yes") == 0;
}

static void rules(void)
{
    KhdaysFormFacts three = facts(230, three_units, 3, 1, 12), two = facts(230, two_units, 2, 0, 12);
    CHECK(holds("gear 230 and links 241 >= 3", &three) == 1);
    CHECK(holds("gear 230 and links 241 >= 3", &two) == 0);
    CHECK(holds("links >= 2", &two) == 1);           /* any links */
    CHECK(holds("links 999 == 0", &three) == 1);     /* none of that one */
    CHECK(holds("dual", &three) == 1);
    CHECK(holds("not dual", &three) == 0);
    CHECK(holds("panel 93", &three) == 1);           /* a Fire set */
    CHECK(holds("panel 93 >= 2", &three) == 0);
    CHECK(holds("panel 241 == 3", &three) == 1);
    CHECK(holds("level >= 10 and level < 13", &three) == 1);
    CHECK(holds("gear 231 or (dual and level == 12)", &three) == 1);
    CHECK(holds("gear 231 or dual and level == 1", &three) == 0); /* and before or */
    CHECK(holds("not (gear 230)", &two) == 0);
    CHECK(holds("hp < 50", &three) == 0); /* outside the field: false */
    three.hp_percent = 40;
    CHECK(holds("hp < 50", &three) == 1);
    CHECK(holds("plugin test.yes", &three) == 0); /* no plugins asked: false */
    three.plugin = answers_yes;
    CHECK(holds("plugin test.yes and not plugin test.no", &three) == 1);

    /* in the field or not; the items named */
    char why[200] = "";
    KhdaysRule *r = khdays_rule_parse("gear 230 and (hp > 1 or links 241 >= 2)", why, sizeof(why));
    CHECK(r != NULL && khdays_rule_in_field(r));
    khdays_rule_free(r);
    r = khdays_rule_parse("gear 230", why, sizeof(why));
    CHECK(r != NULL && !khdays_rule_in_field(r));
    khdays_rule_free(r);

    /* mistakes named with their place */
    static const struct {
        const char *text, *reason;
    } bad[] = {
        {"", "condition expected"},
        {"gear", "item number expected"},
        {"links 241 3", "comparison"},
        {"gear 230 and", "condition expected"},
        {"(gear 230", "')' expected"},
        {"gear 230 links 1 > 0", "the rule's end expected"},
        {"wings 3", "not a condition"},
        {"level 5", "comparison"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        why[0] = '\0';
        r = khdays_rule_parse(bad[i].text, why, sizeof(why));
        CHECK(r == NULL);
        if (strstr(why, bad[i].reason) == NULL || strstr(why, "at character") == NULL) {
            fprintf(stderr, "rule \"%s\": \"%s\" is not \"%s\"\n", bad[i].text, why, bad[i].reason);
            ++failures;
        }
        khdays_rule_free(r);
    }
}

static const char ventus[] = "[character]\n"
                             "format = 2\n"
                             "base = roxas\n"
                             "name = Ventus\n"
                             "[model]\n"
                             "file = model/ventus.glb\n"
                             "skeleton = own\n"
                             "scale = 100\n"
                             "[actions]\n"
                             "fallback = clips/idle.glb\n"
                             "b_ro_A001 = clips/run.glb blend 50\n"
                             "[sounds]\n"
                             "seqarc/PLY_ROX/vo_* = voices/v.wav\n"
                             "[moveset]\n"
                             "ground = 0 1\n"
                             "[stats]\n"
                             "1 = hp 40 strength 10\n"
                             "[weapons]\n"
                             "ro_w00600 = weapons/stick.glb\n"
                             "[form dual]\n"
                             "base = roxas_dual\n"
                             "priority = 100\n"
                             "when = gear 230 and links 241 >= 3   # the Zero Gear with three Ability Units\n"
                             "model = model/ventus_dual.glb\n"
                             "[form dual weapons]\n"
                             "ro_w13600 = weapons/right.glb\n"
                             "ro_w14600 = weapons/left.glb\n"
                             "[form dual actions]\n"
                             "fallback = clips/dual.glb\n"
                             "[form dual stats]\n"
                             "1 = hp 50\n"
                             "[form calm]\n"
                             "when = level >= 50\n"
                             "[form calm limit]\n"
                             "damage = 2\n";

static void forms_read(void)
{
    KhdaysCharacters r;
    Reports reports = {0};
    memset(&r, 0, sizeof(r));
    CHECK(khdays_characters_read(&r, "test.forms", "Forms", "ventus", ventus, strlen(ventus), reported, &reports));
    /* the dual form's base is the game's dual-wield Roxas, its moveset not its default's: told */
    CHECK(strstr(reports.all, "[form dual]: its default's [moveset] names roxas's attacks") != NULL);
    CHECK(r.count == 1);
    const KhdaysCharacter *c = &r.list[0];
    CHECK(c->form_count == 2 && c->stat_count == 2);
    const KhdaysCharacter *dual = khdays_characters_form(c, "dual"), *calm = khdays_characters_form(c, "calm");
    CHECK(dual != NULL && calm != NULL && khdays_characters_form(c, "default") == c);
    CHECK(khdays_characters_form(c, "nothing") == NULL);
    CHECK(strcmp(khdays_characters_form_id(dual), "dual") == 0 && strcmp(khdays_characters_form_id(c), "default") == 0);
    /* its own parts, the rest its default's */
    CHECK(dual->base == KHDAYS_CHARACTER_DUAL && dual->priority == 100);
    CHECK(strcmp(dual->model, "model/ventus_dual.glb") == 0 && dual->own_skeleton && dual->model_scale == 100.0f);
    CHECK(dual->weapon_count == 2 &&
          strcmp(khdays_characters_weapon_model(dual, "ro_w14600")->file, "weapons/left.glb") == 0);
    CHECK(khdays_characters_weapon_model(dual, "ro_w00600") == NULL); /* another base's weapons: its own only */
    CHECK(strcmp(dual->fallback, "clips/dual.glb") == 0 && dual->action_count == 0); /* its own actions only */
    CHECK(dual->sound_count == 1 && strcmp(dual->sounds[0].file, "voices/v.wav") == 0);
    CHECK(dual->combos[KHDAYS_CHARACTER_GROUND].count == 0); /* another base's attacks: none */
    CHECK(dual->stat_count == 1 && dual->stats[0].value == 50);
    CHECK(strcmp(dual->identity, c->identity) == 0 && strcmp(dual->key, "ventus") == 0);
    CHECK(calm->base == 0 && strcmp(calm->model, "model/ventus.glb") == 0);
    CHECK(calm->action_count == 1 && calm->actions[0].blend_ms == 50 && strcmp(calm->fallback, "clips/idle.glb") == 0);
    CHECK(calm->combos[KHDAYS_CHARACTER_GROUND].count == 2); /* the same base: its default's moveset */
    CHECK(calm->weapon_count == 1 &&
          strcmp(khdays_characters_weapon_model(calm, "ro_w00600")->file, "weapons/stick.glb") == 0);
    CHECK(calm->own_moves_damage == 2.0f && c->own_moves_damage == 1.0f);
    CHECK(calm->stat_count == 2);

    /* chosen: the highest priority whose rule holds, else the default */
    KhdaysFormFacts f = facts(230, three_units, 3, 0, 60);
    CHECK(khdays_characters_choose(c, &f) == dual);
    f = facts(230, two_units, 2, 0, 60);
    CHECK(khdays_characters_choose(c, &f) == calm);
    f = facts(230, two_units, 2, 0, 5);
    CHECK(khdays_characters_choose(c, &f) == NULL);

    /* numbered: the character as before, its forms past the characters' */
    CHECK(khdays_characters_number(&r, reported, &reports) == 1);
    CHECK(c->number == KHDAYS_CHARACTER_FIRST);
    CHECK(dual->number == KHDAYS_CHARACTER_FORM_FIRST && calm->number == KHDAYS_CHARACTER_FORM_FIRST + 1);
    CHECK(khdays_characters_by_number(&r, KHDAYS_CHARACTER_FORM_FIRST + 1) == calm);
    CHECK(khdays_characters_owner(&r, calm) == c && khdays_characters_owner(&r, c) == c);
    khdays_characters_free(&r);
}

static void forms_left_out(void)
{
    static const struct {
        const char *text, *told;
        int forms;
    } cases[] = {
        {"[character]\nformat = 2\nbase = roxas\nname = A\n[model]\nfile = a.glb\n[form x]\nbase = roxas\n",
         "no `when`", 0},
        {"[character]\nformat = 2\nbase = roxas\nname = A\n[model]\nfile = a.glb\n[form x]\nwhen = gear\n",
         "when: gear: the gear's item number expected", 0},
        {"[character]\nformat = 2\nbase = roxas\nname = A\n[model]\nfile = a.glb\n[form x]\nwhen = dual\nbase = "
         "kairi\n[form y]\nwhen = dual\n",
         "base kairi is not one of the game's characters", 1},
        {"[character]\nformat = 1\nbase = roxas\nname = A\n[model]\nfile = a.glb\n[form x]\nwhen = dual\n",
         "need format = 2", 0},
        {"[character]\nformat = 2\nbase = roxas\nname = A\n[model]\nfile = a.glb\n[form default]\nwhen = dual\n",
         "not default", -1},
        {"[character]\nformat = 2\nbase = roxas\nname = A\n[model]\nfile = a.glb\n[stats]\n101 = hp 3\n",
         "a level, 1-100", -1},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        KhdaysCharacters r;
        Reports reports = {0};
        memset(&r, 0, sizeof(r));
        const int read = khdays_characters_read(&r, "t", "t", "a", cases[i].text, strlen(cases[i].text), reported,
                                                &reports);
        if (strstr(reports.all, cases[i].told) == NULL) {
            fprintf(stderr, "case %d: \"%s\" not told in:\n%s", (int)i, cases[i].told, reports.all);
            ++failures;
        }
        if (cases[i].forms < 0) {
            CHECK(!read); /* the character's own part wrong: left out */
        } else {
            CHECK(read && r.count == 1 && r.list[0].form_count == cases[i].forms);
        }
        khdays_characters_free(&r);
    }
}

/* `moves = base`: a form of another base plays that base's own moves, nothing told */
static void moves_of_base(void)
{
    static const char text[] = "[character]\nformat = 2\nbase = roxas\nname = A\n[model]\nfile = a.glb\n"
                               "[moveset]\nground = 0 1\n[form d]\nbase = roxas_dual\nwhen = dual\n"
                               "[form d moveset]\nmoves = base\n";
    KhdaysCharacters r;
    Reports reports = {0};
    memset(&r, 0, sizeof(r));
    CHECK(khdays_characters_read(&r, "t", "t", "a", text, strlen(text), reported, &reports));
    CHECK(reports.all[0] == '\0');
    CHECK(r.count == 1 && r.list[0].form_count == 1);
    if (r.count == 1 && r.list[0].form_count == 1) {
        CHECK(r.list[0].forms[0].combos[KHDAYS_CHARACTER_GROUND].count == 0);
        CHECK(r.list[0].combos[KHDAYS_CHARACTER_GROUND].count == 2);
    }
    khdays_characters_free(&r);
}

static void stats_tables(void)
{
    unsigned char base[KHDAYS_STATS_TABLE], out[KHDAYS_STATS_TABLE];
    for (int level = 1; level <= KHDAYS_CHARACTER_LEVELS; ++level) {
        for (int s = 0; s < KHDAYS_STATS; ++s) {
            unsigned char *at = base + (level - 1) * KHDAYS_STATS_ROW + khdays_stats_offset(s);
            const long v = s == KHDAYS_STAT_UNKNOWN_12 ? 0x5000 : level + s;
            at[0] = (unsigned char)v;
            at[1] = (unsigned char)(v >> 8);
            if (s == KHDAYS_STAT_UNKNOWN_12) {
                at[2] = (unsigned char)(v >> 16);
                at[3] = (unsigned char)(v >> 24);
            }
        }
    }
    const KhdaysCharacterStatPoint points[] = {
        {10, KHDAYS_STAT_HP, 100}, {20, KHDAYS_STAT_HP, 200}, {50, KHDAYS_STAT_STRENGTH, 999}, {20, KHDAYS_STAT_HP, 300},
    };
    CHECK(!khdays_stats_table(points, 4, base, KHDAYS_STATS_TABLE - 1, out));
    CHECK(khdays_stats_table(points, 4, base, KHDAYS_STATS_TABLE, out));
    CHECK(khdays_stats_value(out, 1, KHDAYS_STAT_HP) == 100);   /* before the first given: the first's */
    CHECK(khdays_stats_value(out, 10, KHDAYS_STAT_HP) == 100);
    CHECK(khdays_stats_value(out, 15, KHDAYS_STAT_HP) == 200);  /* in between (the later line for 20 wins: 300) */
    CHECK(khdays_stats_value(out, 20, KHDAYS_STAT_HP) == 300);
    CHECK(khdays_stats_value(out, 100, KHDAYS_STAT_HP) == 300); /* after the last: the last's */
    CHECK(khdays_stats_value(out, 1, KHDAYS_STAT_STRENGTH) == 999 && khdays_stats_value(out, 100, 0) == 999);
    CHECK(khdays_stats_value(out, 7, KHDAYS_STAT_MAGIC) == 7 + KHDAYS_STAT_MAGIC); /* never given: the base's */
    CHECK(khdays_stats_value(out, 7, KHDAYS_STAT_UNKNOWN_12) == 0x5000);
}

int main(void)
{
    rules();
    forms_read();
    forms_left_out();
    moves_of_base();
    stats_tables();
    if (failures == 0) {
        printf("character forms: all checks passed\n");
    }
    return failures != 0;
}
