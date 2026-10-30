/* The characters the mods add (registry.h). */
#include "registry.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const languages[KHDAYS_CHARACTER_LANGUAGES] = {"en", "fr", "de", "it", "es"};

/* The data folders' order (data_02042a70), with the names a description gives them. */
static const struct {
    const char *name, *code;
} bases[KHDAYS_CHARACTER_FOLDERS] = {
    {"roxas", "ro"},    {"axel", "ax"},   {"xigbar", "xi"},  {"saix", "sa"},    {"xaldin", "xa"},
    {"sora", "so"},     {"demyx", "de"},  {"larxene", "la"}, {"lexaeus", "le"}, {"luxord", "lu"},
    {"marluxia", "ma"}, {"riku", "ri"},   {"vexen", "ve"},   {"xemnas", "xe"},  {"xion", "xo"},
    {"zexion", "ze"},   {"mickey", "mi"}, {"donald", "do"},  {"goofy", "go"},   {"roxas_dual", "r2"},
};

/* [stats]' names, in KhdaysCharacterStat's order. */
static const char *const stat_names[KHDAYS_STATS] = {"strength", "magic", "defence", "hp", "unknown_8", "unknown_12"};

/* A form that cannot be taken: left out at the description's end (told once). */
#define FORM_BROKEN 0x80000000u

const char *khdays_character_language(int l)
{
    return l >= 0 && l < KHDAYS_CHARACTER_LANGUAGES ? languages[l] : NULL;
}

const char *khdays_character_base_name(int number)
{
    return number >= 0 && number < KHDAYS_CHARACTER_FOLDERS ? bases[number].name : NULL;
}

const char *khdays_character_base_code(int number)
{
    return number >= 0 && number < KHDAYS_CHARACTER_FOLDERS ? bases[number].code : NULL;
}

static char *copy_n(const char *s, size_t n)
{
    char *c = malloc(n + 1);
    if (c != NULL) {
        memcpy(c, s, n);
        c[n] = '\0';
    }
    return c;
}

static char *copy(const char *s)
{
    return s != NULL ? copy_n(s, strlen(s)) : NULL;
}

static void free_character(KhdaysCharacter *c)
{
    free(c->identity);
    for (int l = 0; l < KHDAYS_CHARACTER_LANGUAGES; ++l) {
        free(c->name[l]);
        free(c->weapon[l]);
    }
    free(c->any_name);
    free(c->any_weapon);
    free(c->face);
    free(c->model);
#pragma warning(push)
/* the actions, sounds, weapons and forms up to their counts are the reading's, their strings set (a sound's file
 * NULL for none); the combos' lists are read_moveset's, or NULL from the reading's memset */
#pragma warning(disable : 6001)
    for (int i = 0; i < c->action_count; ++i) {
        free(c->actions[i].game);
        free(c->actions[i].clip);
    }
    for (int i = 0; i < c->sound_count; ++i) {
        free(c->sounds[i].game);
        free(c->sounds[i].file);
    }
    for (int i = 0; i < c->weapon_count; ++i) {
        free(c->weapons[i].game);
        free(c->weapons[i].file);
    }
    for (int i = 0; i < c->form_count; ++i) {
        free_character(&c->forms[i]);
    }
    free(c->combos[KHDAYS_CHARACTER_GROUND].attacks);
    free(c->combos[KHDAYS_CHARACTER_AIR].attacks);
#pragma warning(pop)
    free(c->actions);
    free(c->sounds);
    free(c->attacks);
    free(c->fallback);
    free(c->weapon_bone);
    free(c->base_weapon_node);
    free(c->stats);
    free(c->weapons);
    free(c->forms);
    free(c->form);
    khdays_rule_free(c->rule);
    memset(c, 0, sizeof(*c));
}

static int same_word(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
    }
    return *a == *b;
}

/* A key: letters, digits, '_' and '-'. */
static int good_key(const char *key)
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

/* A reference inside the character's folder: relative, never up out of it. */
static int good_reference(const char *path)
{
    if (*path == '\0' || *path == '/' || *path == '\\' || strchr(path, ':') != NULL) {
        return 0;
    }
    for (const char *p = path; *p != '\0';) {
        const char *end = p;
        while (*end != '\0' && *end != '/' && *end != '\\') {
            ++end;
        }
        if (end - p == 2 && p[0] == '.' && p[1] == '.') {
            return 0;
        }
        p = *end != '\0' ? end + 1 : end;
    }
    return 1;
}

/* `n` numbers 0-255 apart by spaces, all of them and nothing else. */
static int read_bytes(const char *text, unsigned char *out, int n)
{
    const char *p = text;
    for (int i = 0; i < n; ++i) {
        char *end;
        const long v = strtol(p, &end, 10);
        if (end == p || v < 0 || v > 255) {
            return 0;
        }
        out[i] = (unsigned char)v;
        p = end;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return *p == '\0';
}

/* `name` or `name <language>`: the language's index, -1 for none, -2 for another name. */
static int named(const char *key, const char *name)
{
    const size_t n = strlen(name);
    if (strcmp(key, name) == 0) {
        return -1;
    }
    if (strncmp(key, name, n) == 0 && key[n] == ' ') {
        for (int l = 0; l < KHDAYS_CHARACTER_LANGUAGES; ++l) {
            if (strcmp(key + n + 1, languages[l]) == 0) {
                return l;
            }
        }
    }
    return -2;
}

static int add_mod(KhdaysCharacters *r, const char *id, const char *folder)
{
    for (int i = 0; i < r->mod_count; ++i) {
        if (strcmp(r->mods[i].id, id) == 0 && strcmp(r->mods[i].folder, folder) == 0) {
            return i;
        }
    }
    if (r->mod_count == r->mod_capacity) {
        const int grown = r->mod_capacity == 0 ? 4 : r->mod_capacity * 2;
        KhdaysCharacterMod *m = realloc(r->mods, (size_t)grown * sizeof(*m));
        if (m == NULL) {
            return -1;
        }
        r->mods = m;
        r->mod_capacity = grown;
    }
    r->mods[r->mod_count].id = copy(id);
    r->mods[r->mod_count].folder = copy(folder);
    if (r->mods[r->mod_count].id == NULL || r->mods[r->mod_count].folder == NULL) {
        free(r->mods[r->mod_count].id);
        free(r->mods[r->mod_count].folder);
        return -1;
    }
    return r->mod_count++;
}

enum Section { NONE, CHARACTER, MODEL, ACTIONS, SOUNDS, MOVESET, LIMIT, STATS, WEAPONS, FORM, OTHER };

/* An attack's number: 0 or more, and nothing else. */
static int read_attack(const char *text, int *out)
{
    char *end;
    const long v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 0 || v > 0x7fffffff) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* A [moveset] line: `ground` / `air` = the base's attacks in order; `ground finisher` / `air finisher` = one;
 * `attack <number>` = any of damage <x>, reach <x>, push <x> (times the base's); `moves = base`: none of these,
 * its base's own moves (a form's, its default's moveset not taken). */
static void read_moveset(KhdaysCharacter *c, const char *name, const char *value, char *why, size_t why_size)
{
    char word[32] = "", number[32] = "";
    int used = 0, id;
    if (strcmp(name, "moves") == 0) {
        if (!same_word(value, "base")) {
            snprintf(why, why_size, "moves %.40s: base (its base's own moves)", value);
        }
        return;
    }
    for (int g = 0; g < 2; ++g) {
        const char *combo = g == KHDAYS_CHARACTER_GROUND ? "ground" : "air";
        KhdaysCharacterCombo *k = &c->combos[g];
        char finisher[40];
        snprintf(finisher, sizeof(finisher), "%s finisher", combo);
        if (strcmp(name, finisher) == 0) {
            if (!read_attack(value, &k->finisher)) {
                snprintf(why, why_size, "%s: %.40s is not an attack's number", finisher, value);
            }
            return;
        }
        if (strcmp(name, combo) != 0) {
            continue;
        }
        k->count = 0;
        for (const char *p = value; sscanf(p, "%31s%n", number, &used) == 1; p += used) {
            int *grown = realloc(k->attacks, (size_t)(k->count + 1) * sizeof(*grown));
            if (grown == NULL) {
                snprintf(why, why_size, "out of memory");
                return;
            }
            k->attacks = grown;
            if (!read_attack(number, &k->attacks[k->count])) {
                snprintf(why, why_size, "%s: %.30s is not an attack's number", combo, number);
                return;
            }
            ++k->count;
        }
        if (k->count == 0) {
            snprintf(why, why_size, "%s: no attacks (the base's attacks' numbers, in order)", combo);
        }
        return;
    }
    if (strncmp(name, "attack ", 7) != 0 || !read_attack(name + 7, &id)) {
        snprintf(why, why_size, "%.60s: ground, air, ground finisher, air finisher or attack <number>", name);
        return;
    }
    KhdaysCharacterAttack a = {id, 1.0f, 1.0f, 1.0f};
    for (const char *p = value; sscanf(p, "%31s%n", word, &used) == 1;) {
        char *end;
        float *field = strcmp(word, "damage") == 0  ? &a.damage
                       : strcmp(word, "reach") == 0 ? &a.reach
                       : strcmp(word, "push") == 0  ? &a.push
                                                    : NULL;
        p += used;
        if (field == NULL || sscanf(p, "%31s%n", number, &used) != 1) {
            snprintf(why, why_size, "%.60s: %.30s is not damage, reach or push <times>", name, word);
            return;
        }
        p += used;
        *field = strtof(number, &end);
        if (end == number || *end != '\0' || !(*field >= 0.0f)) {
            snprintf(why, why_size, "%.60s: %.30s %.30s is not a number of times, 0 or more", name, word, number);
            return;
        }
    }
    KhdaysCharacterAttack *grown = realloc(c->attacks, (size_t)(c->attack_count + 1) * sizeof(*grown));
    if (grown == NULL) {
        snprintf(why, why_size, "out of memory");
        return;
    }
    c->attacks = grown;
    c->attacks[c->attack_count++] = a;
}

/* An [actions] line: `fallback = <clip>`, or `<the base's animation> = <clip> [blend <ms>] [root keep|remove]
 * [time own|game]`. A clip's path has no spaces. */
static void read_action(KhdaysCharacter *c, const char *name, const char *value, char *why, size_t why_size)
{
    char clip[512] = "", word[32] = "", rest[64] = "";
    int blend_ms = KHDAYS_CHARACTER_BLEND_MS, root_keep = 0, game_time = 0, used = 0;
    if (sscanf(value, "%511s%n", clip, &used) != 1) {
        snprintf(why, why_size, "%.60s: its clip missing", name);
        return;
    }
    if (!good_reference(clip)) {
        snprintf(why, why_size, "%.60s: %.80s is not a file inside the character's folder", name, clip);
        return;
    }
    for (const char *p = value + used; sscanf(p, "%31s%n", word, &used) == 1; p += used) {
        int more = 0;
        p += used;
        if (sscanf(p, "%63s%n", rest, &more) != 1) {
            snprintf(why, why_size, "%.60s: %.30s without its value", name, word);
            return;
        }
        used = more;
        if (strcmp(word, "blend") == 0) {
            char *end;
            const long v = strtol(rest, &end, 10);
            if (end == rest || *end != '\0' || v < 0) {
                snprintf(why, why_size, "%.60s: blend %.30s is not milliseconds", name, rest);
                return;
            }
            blend_ms = (int)v;
        } else if (strcmp(word, "root") == 0 && (same_word(rest, "keep") || same_word(rest, "remove"))) {
            root_keep = same_word(rest, "keep");
        } else if (strcmp(word, "time") == 0 && (same_word(rest, "own") || same_word(rest, "game"))) {
            game_time = same_word(rest, "game");
        } else {
            snprintf(why, why_size, "%.60s: %.30s %.30s is not blend <ms>, root keep|remove or time own|game", name,
                     word, rest);
            return;
        }
    }
    if (strcmp(name, "fallback") == 0) {
        free(c->fallback);
        c->fallback = copy(clip);
        return;
    }
    KhdaysCharacterAction *grown = realloc(c->actions, (size_t)(c->action_count + 1) * sizeof(*grown));
    if (grown == NULL) {
        snprintf(why, why_size, "out of memory");
        return;
    }
    c->actions = grown;
    KhdaysCharacterAction *a = &c->actions[c->action_count];
    a->game = copy(name);
    a->clip = copy(clip);
    a->blend_ms = blend_ms;
    a->root_keep = root_keep;
    a->game_time = game_time;
    if (a->game == NULL || a->clip == NULL) {
        free(a->game);
        free(a->clip);
        snprintf(why, why_size, "out of memory");
        return;
    }
    ++c->action_count;
}

/* A [sounds] line: `<the base's sound> = <file>` or `= none`; a later line for the same sound replaces it. */
static void read_sound(KhdaysCharacter *c, const char *name, const char *value, char *why, size_t why_size)
{
    const int silent = same_word(value, "none");
    if (!silent && !good_reference(value)) {
        snprintf(why, why_size, "%.60s: %.80s is neither none nor a file inside the character's folder", name, value);
        return;
    }
    KhdaysCharacterSound *s = NULL;
    for (int i = 0; i < c->sound_count && s == NULL; ++i) {
        s = strcmp(c->sounds[i].game, name) == 0 ? &c->sounds[i] : NULL;
    }
    if (s == NULL) {
        KhdaysCharacterSound *grown = realloc(c->sounds, (size_t)(c->sound_count + 1) * sizeof(*grown));
        if (grown == NULL) {
            snprintf(why, why_size, "out of memory");
            return;
        }
        c->sounds = grown;
        s = &c->sounds[c->sound_count];
        s->game = copy(name);
        s->file = NULL;
        if (s->game == NULL) {
            snprintf(why, why_size, "out of memory");
            return;
        }
        ++c->sound_count;
    }
    free(s->file);
    s->file = silent ? NULL : copy(value);
    if (!silent && s->file == NULL) {
        snprintf(why, why_size, "out of memory");
    }
}

/* A [stats] line: `<level> = <stat> <value> ...` (strength, magic, defence, hp, unknown_8, unknown_12). */
static void read_stats(KhdaysCharacter *c, const char *name, const char *value, char *why, size_t why_size)
{
    char *end, word[32] = "", number[32] = "";
    int used = 0;
    const long level = strtol(name, &end, 10);
    if (end == name || *end != '\0' || level < 1 || level > KHDAYS_CHARACTER_LEVELS) {
        snprintf(why, why_size, "%.40s: a level, 1-%d", name, KHDAYS_CHARACTER_LEVELS);
        return;
    }
    for (const char *p = value; sscanf(p, "%31s%n", word, &used) == 1;) {
        int stat = -1;
        for (int s = 0; s < KHDAYS_STATS; ++s) {
            if (same_word(word, stat_names[s]) || (s == KHDAYS_STAT_DEFENCE && same_word(word, "defense"))) {
                stat = s;
            }
        }
        p += used;
        if (stat < 0 || sscanf(p, "%31s%n", number, &used) != 1) {
            snprintf(why, why_size,
                     "level %ld: %.30s is not strength, magic, defence, hp, unknown_8 or unknown_12 <value>", level,
                     word);
            return;
        }
        p += used;
        const long v = strtol(number, &end, 10);
        const long most = stat == KHDAYS_STAT_UNKNOWN_12 ? 0x7fffffffL : 0xffffL;
        if (end == number || *end != '\0' || v < 0 || v > most) {
            snprintf(why, why_size, "level %ld: %.30s %.30s is not a value, 0-%ld", level, word, number, most);
            return;
        }
        KhdaysCharacterStatPoint *grown = realloc(c->stats, (size_t)(c->stat_count + 1) * sizeof(*grown));
        if (grown == NULL) {
            snprintf(why, why_size, "out of memory");
            return;
        }
        c->stats = grown;
        c->stats[c->stat_count].level = (int)level;
        c->stats[c->stat_count].stat = stat;
        c->stats[c->stat_count].value = v;
        ++c->stat_count;
    }
}

/* A [weapons] line: `<the base's weapon model> = <file>`; a later line for the same model replaces it. */
static void read_weapon(KhdaysCharacter *c, const char *name, const char *value, char *why, size_t why_size)
{
    if (!good_reference(value)) {
        snprintf(why, why_size, "%.60s: %.80s is not a file inside the character's folder", name, value);
        return;
    }
    KhdaysCharacterWeapon *w = NULL;
    for (int i = 0; i < c->weapon_count && w == NULL; ++i) {
        w = strcmp(c->weapons[i].game, name) == 0 ? &c->weapons[i] : NULL;
    }
    if (w == NULL) {
        KhdaysCharacterWeapon *grown = realloc(c->weapons, (size_t)(c->weapon_count + 1) * sizeof(*grown));
        if (grown == NULL) {
            snprintf(why, why_size, "out of memory");
            return;
        }
        c->weapons = grown;
        w = &c->weapons[c->weapon_count];
        w->game = copy(name);
        w->file = NULL;
        if (w->game == NULL) {
            snprintf(why, why_size, "out of memory");
            return;
        }
        ++c->weapon_count;
    }
    free(w->file);
    if ((w->file = copy(value)) == NULL) {
        snprintf(why, why_size, "out of memory");
    }
}

/* A base's number by its name: 0-18 for a character, 0-19 for a form (19 roxas_dual); -1 for none. */
static int base_by_name(const char *value, int folders)
{
    for (int i = 0; i < folders; ++i) {
        if (same_word(value, bases[i].name)) {
            return i;
        }
    }
    return -1;
}

/* A [model] line, or one of a [form <id>]'s model lines: 1 when `name` is one of them (`*value` taken when kept). */
static int read_model(KhdaysCharacter *t, const char *name, char **value, char *why, size_t why_size)
{
    if (strcmp(name, "file") == 0 || (t->form != NULL && strcmp(name, "model") == 0)) {
        if (!good_reference(*value)) {
            snprintf(why, why_size, "%s %.80s: not a file inside the character's folder", name, *value);
        } else {
            free(t->model);
            t->model = *value;
            *value = NULL;
            t->gives |= KHDAYS_FORM_GIVES_MODEL;
        }
    } else if (strcmp(name, "scale") == 0) {
        char *number_end;
        const double v = strtod(*value, &number_end);
        if (number_end == *value || *number_end != '\0' || !(v > 0.0)) {
            snprintf(why, why_size, "scale %.40s: a number above 0", *value);
        } else {
            t->model_scale = (float)v;
            t->gives |= KHDAYS_FORM_GIVES_SCALE;
        }
    } else if (strcmp(name, "weapon_bone") == 0 || strcmp(name, "base_weapon_node") == 0) {
        const int bone = strcmp(name, "weapon_bone") == 0;
        char **slot = bone ? &t->weapon_bone : &t->base_weapon_node;
        free(*slot);
        *slot = *value;
        *value = NULL;
        t->gives |= bone ? KHDAYS_FORM_GIVES_WEAPON_BONE : KHDAYS_FORM_GIVES_BASE_WEAPON_NODE;
    } else if (strcmp(name, "weapon_turn") == 0) {
        if (sscanf(*value, "%f %f %f", &t->weapon_turn[0], &t->weapon_turn[1], &t->weapon_turn[2]) != 3) {
            snprintf(why, why_size, "weapon_turn %.40s: three angles in degrees (x y z)", *value);
        } else {
            t->has_weapon_turn = 1;
            t->gives |= KHDAYS_FORM_GIVES_WEAPON_TURN;
        }
    } else if (strcmp(name, "skeleton") == 0) {
        if (same_word(*value, "paired")) {
            t->own_skeleton = 0;
        } else if (same_word(*value, "own")) {
            t->own_skeleton = 1;
        } else {
            snprintf(why, why_size, "skeleton %.40s: paired or own", *value);
        }
        t->gives |= KHDAYS_FORM_GIVES_SKELETON;
    } else {
        return 0;
    }
    return 1;
}

/* A [form <id>] line other than its model's: base, priority, when. 1 when `name` is one of them. */
static int read_form(KhdaysCharacter *f, const char *name, const char *value, char *why, size_t why_size)
{
    if (strcmp(name, "base") == 0) {
        f->base = base_by_name(value, KHDAYS_CHARACTER_FOLDERS);
        if (f->base < 0) {
            snprintf(why, why_size, "base %.60s is not one of the game's characters (or roxas_dual)", value);
        }
    } else if (strcmp(name, "priority") == 0) {
        char *end;
        const long v = strtol(value, &end, 10);
        if (end == value || *end != '\0') {
            snprintf(why, why_size, "priority %.40s: a whole number", value);
        } else {
            f->priority = (int)v;
        }
    } else if (strcmp(name, "when") == 0) {
        char rule_why[200] = "";
        khdays_rule_free(f->rule);
        f->rule = khdays_rule_parse(value, rule_why, sizeof(rule_why));
        if (f->rule == NULL) {
            snprintf(why, why_size, "when: %s", rule_why);
        }
    } else {
        return 0;
    }
    return 1;
}

/* The form `id` of `c` (made the first time it is named): its index, -1 when it cannot be made. */
static int form_named(KhdaysCharacter *c, const char *id)
{
    for (int i = 0; i < c->form_count; ++i) {
        if (strcmp(c->forms[i].form, id) == 0) {
            return i;
        }
    }
    KhdaysCharacter *grown = realloc(c->forms, (size_t)(c->form_count + 1) * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    c->forms = grown;
    KhdaysCharacter *f = &c->forms[c->form_count];
    memset(f, 0, sizeof(*f));
    f->base = -1;
    f->combos[KHDAYS_CHARACTER_GROUND].finisher = f->combos[KHDAYS_CHARACTER_AIR].finisher = -1;
    f->own_moves_damage = 1.0f;
    f->form = copy(id);
    if (f->form == NULL) {
        return -1;
    }
    return c->form_count++;
}

/* A part's head (`[model]`, `[form dual]`, `[form dual actions]`): its section and its form (-1 the character's
 * own); OTHER for none this port has; `why` when it names a form badly. */
static enum Section head(KhdaysCharacter *c, const char *s, int *form, char *why, size_t why_size)
{
    static const struct {
        const char *name;
        enum Section section;
    } parts[] = {{"character", CHARACTER}, {"model", MODEL}, {"actions", ACTIONS}, {"sounds", SOUNDS},
                 {"moveset", MOVESET},     {"limit", LIMIT}, {"stats", STATS},     {"weapons", WEAPONS}};
    char inside[160] = "", first[64] = "", id[64] = "", part[64] = "", extra[8] = "";
    const size_t n = strlen(s);
    *form = -1;
    if (n < 2 || s[n - 1] != ']' || n - 2 >= sizeof(inside)) {
        return OTHER;
    }
    memcpy(inside, s + 1, n - 2);
    inside[n - 2] = '\0';
    const int words = sscanf(inside, "%63s %63s %63s %7s", first, id, part, extra);
    if (words == 1) {
        for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
            if (strcmp(first, parts[i].name) == 0) {
                return parts[i].section;
            }
        }
        return OTHER;
    }
    if (strcmp(first, "form") != 0 || words > 3) {
        return OTHER;
    }
    if (!good_key(id) || same_word(id, "default")) {
        snprintf(why, why_size, "[form %.60s]: a form's id is letters, digits, '_' and '-', not default", id);
        return OTHER;
    }
    if ((*form = form_named(c, id)) < 0) {
        snprintf(why, why_size, "out of memory");
        return OTHER;
    }
    if (words == 2) {
        return FORM;
    }
    for (size_t i = 2; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        if (strcmp(part, parts[i].name) == 0) {
            return parts[i].section;
        }
    }
    *form = -1;
    return OTHER;
}

/* ---- A form made whole ---------------------------------------------------------------------- */

/* The character's actions, copied into form `f`; 0 when out of memory. */
static int copy_actions(KhdaysCharacter *f, const KhdaysCharacter *c)
{
    int ok = (f->fallback = copy(c->fallback)) != NULL || c->fallback == NULL;
    f->actions = c->action_count > 0 ? calloc((size_t)c->action_count, sizeof(*f->actions)) : NULL;
    for (int i = 0; f->actions != NULL && i < c->action_count; ++i) {
        f->actions[i] = c->actions[i];
        f->actions[i].game = copy(c->actions[i].game);
        f->actions[i].clip = copy(c->actions[i].clip);
        ok &= f->actions[i].game != NULL && f->actions[i].clip != NULL;
        f->action_count = i + 1;
    }
    return ok && (c->action_count == 0 || f->actions != NULL);
}

/* The character's sounds, copied into form `f`; 0 when out of memory. */
static int copy_sounds(KhdaysCharacter *f, const KhdaysCharacter *c)
{
    int ok = 1;
    f->sounds = c->sound_count > 0 ? calloc((size_t)c->sound_count, sizeof(*f->sounds)) : NULL;
    for (int i = 0; f->sounds != NULL && i < c->sound_count; ++i) {
        f->sounds[i].game = copy(c->sounds[i].game);
        f->sounds[i].file = copy(c->sounds[i].file);
        ok &= f->sounds[i].game != NULL && (f->sounds[i].file != NULL || c->sounds[i].file == NULL);
        f->sound_count = i + 1;
    }
    return ok && (c->sound_count == 0 || f->sounds != NULL);
}

/* The character's moveset, copied into form `f` when both play the same base (`why` says so when not: the
 * form's base's moves play); 0 when out of memory. */
static int copy_moveset(KhdaysCharacter *f, const KhdaysCharacter *c, char *why, size_t why_size)
{
    const int has = c->combos[0].count > 0 || c->combos[1].count > 0 || c->combos[0].finisher >= 0 ||
                    c->combos[1].finisher >= 0 || c->attack_count > 0;
    int ok = 1;
    if (has && f->base != c->base) {
        snprintf(why, why_size,
                 "[form %.60s]: its default's [moveset] names %s's attacks, not %s's: its base's "
                 "moves play (give it a [form %.60s moveset], `moves = base` for those)",
                 f->form, bases[c->base].name, bases[f->base].name, f->form);
        return 1;
    }
    if (!has) {
        return 1;
    }
    for (int g = 0; g < 2; ++g) {
        f->combos[g].finisher = c->combos[g].finisher;
        f->combos[g].count = c->combos[g].count;
        f->combos[g].attacks = c->combos[g].count > 0 ? malloc((size_t)c->combos[g].count * sizeof(int)) : NULL;
        if (f->combos[g].attacks != NULL) {
            memcpy(f->combos[g].attacks, c->combos[g].attacks, (size_t)c->combos[g].count * sizeof(int));
        } else {
            ok &= c->combos[g].count == 0;
            f->combos[g].count = 0;
        }
    }
    f->attacks = c->attack_count > 0 ? malloc((size_t)c->attack_count * sizeof(*f->attacks)) : NULL;
    if (f->attacks != NULL) {
        memcpy(f->attacks, c->attacks, (size_t)c->attack_count * sizeof(*f->attacks));
        f->attack_count = c->attack_count;
    } else {
        ok &= c->attack_count == 0;
    }
    return ok;
}

/* The character's level stats, copied into form `f`; 0 when out of memory. */
static int copy_stats(KhdaysCharacter *f, const KhdaysCharacter *c)
{
    if (c->stat_count == 0) {
        return 1;
    }
    f->stats = malloc((size_t)c->stat_count * sizeof(*f->stats));
    if (f->stats == NULL) {
        return 0;
    }
    memcpy(f->stats, c->stats, (size_t)c->stat_count * sizeof(*f->stats));
    f->stat_count = c->stat_count;
    return 1;
}

/* The character's weapons, copied into form `f`; 0 when out of memory. */
static int copy_weapons(KhdaysCharacter *f, const KhdaysCharacter *c)
{
    int ok = 1;
    if (c->weapon_count == 0) {
        return 1;
    }
    f->weapons = calloc((size_t)c->weapon_count, sizeof(*f->weapons));
    for (int i = 0; f->weapons != NULL && i < c->weapon_count; ++i) {
        f->weapons[i].game = copy(c->weapons[i].game);
        f->weapons[i].file = copy(c->weapons[i].file);
        ok &= f->weapons[i].game != NULL && f->weapons[i].file != NULL;
        f->weapon_count = i + 1;
    }
    return ok && f->weapons != NULL;
}

/* A deep copy of what `f` takes from its character `c` (a part it does not give itself). */
static int inherit(KhdaysCharacter *f, const KhdaysCharacter *c, char *why, size_t why_size)
{
    int ok = 1;
    f->mod = c->mod;
    f->has_gauges = c->has_gauges;
    memcpy(f->gauges, c->gauges, sizeof(f->gauges));
    f->locked_with_base = c->locked_with_base;
    for (int l = 0; l < KHDAYS_CHARACTER_LANGUAGES; ++l) {
        ok &= (f->name[l] = copy(c->name[l])) != NULL || c->name[l] == NULL;
        ok &= (f->weapon[l] = copy(c->weapon[l])) != NULL || c->weapon[l] == NULL;
    }
    ok &= (f->any_name = copy(c->any_name)) != NULL || c->any_name == NULL;
    ok &= (f->any_weapon = copy(c->any_weapon)) != NULL || c->any_weapon == NULL;
    ok &= (f->face = copy(c->face)) != NULL || c->face == NULL;
    if (f->base < 0) {
        f->base = c->base;
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_MODEL)) {
        ok &= (f->model = copy(c->model)) != NULL;
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_SKELETON)) {
        f->own_skeleton = c->own_skeleton;
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_SCALE)) {
        f->model_scale = c->model_scale;
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_WEAPON_BONE)) {
        ok &= (f->weapon_bone = copy(c->weapon_bone)) != NULL || c->weapon_bone == NULL;
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_BASE_WEAPON_NODE)) {
        ok &= (f->base_weapon_node = copy(c->base_weapon_node)) != NULL || c->base_weapon_node == NULL;
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_WEAPON_TURN)) {
        f->has_weapon_turn = c->has_weapon_turn;
        memcpy(f->weapon_turn, c->weapon_turn, sizeof(f->weapon_turn));
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_ACTIONS)) {
        ok &= copy_actions(f, c);
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_SOUNDS)) {
        ok &= copy_sounds(f, c);
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_MOVESET)) {
        ok &= copy_moveset(f, c, why, why_size);
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_LIMIT)) {
        f->own_moves_damage = c->own_moves_damage;
    }
    if (!(f->gives & KHDAYS_FORM_GIVES_STATS)) {
        ok &= copy_stats(f, c);
    }
    /* the weapons name the base's models: another base's are its own */
    if (!(f->gives & KHDAYS_FORM_GIVES_WEAPONS) && f->base == c->base) {
        ok &= copy_weapons(f, c);
    }
    ok &= (f->identity = copy(c->identity)) != NULL;
    if (f->identity != NULL) {
        f->key = f->identity + (c->key - c->identity);
    }
    if (!ok) {
        snprintf(why, why_size, "out of memory");
    }
    return ok;
}

/* ---- A description read ----------------------------------------------------------------------- */

/* What reading one description keeps from line to line. */
typedef struct Reading {
    KhdaysCharacter c;
    enum Section section; /* the part being read */
    int form;             /* its form (-1 the character's own) */
    int format, has_format, has_base;
    char why[300]; /* the mistake that leaves the character out, and its line */
    int why_line;
    const char *mod_id, *key; /* for the reports */
    KhdaysCharactersReport report;
    void *context;
} Reading;

static void fail(Reading *rd, int line, const char *why)
{
    snprintf(rd->why, sizeof(rd->why), "%s", why);
    rd->why_line = line;
}

static void note(Reading *rd, int line, const char *text)
{
    rd->report(rd->mod_id, rd->key, line, text, rd->context);
}

/* The next line from `*at`: [*a, *b) without the spaces at its ends, a UTF-8 mark (on the first line) or a
 * comment (from a # at its start or after a space). */
static void next_line(const char *text, size_t size, size_t *at, int first, size_t *a, size_t *b)
{
    size_t from = *at, to = *at;
    while (to < size && text[to] != '\n') {
        ++to;
    }
    *at = to + 1;
    while (from < to && isspace((unsigned char)text[from])) {
        ++from;
    }
    while (to > from && isspace((unsigned char)text[to - 1])) {
        --to;
    }
    if (first && to - from >= 3 && (unsigned char)text[from] == 0xef && (unsigned char)text[from + 1] == 0xbb &&
        (unsigned char)text[from + 2] == 0xbf) {
        from += 3;
    }
    for (size_t i = from; i < to; ++i) {
        if (text[i] == '#' && (i == from || isspace((unsigned char)text[i - 1]))) {
            to = i;
        }
    }
    while (to > from && isspace((unsigned char)text[to - 1])) {
        --to;
    }
    *a = from;
    *b = to;
}

/* A part's head (`[model]`, `[form dual]`...): the part read next. */
static void read_head(Reading *rd, const char *s, size_t n, int line)
{
    char *text = copy_n(s, n);
    if (text == NULL) {
        fail(rd, line, "out of memory");
        return;
    }
    rd->section = head(&rd->c, text, &rd->form, rd->why, sizeof(rd->why));
    if (rd->why[0] != '\0') {
        rd->why_line = line;
    } else if (rd->section == OTHER) {
        char text_note[200];
        snprintf(text_note, sizeof(text_note), "%.80s: no such part (its lines are not read)", text);
        note(rd, line, text_note);
    }
    free(text);
}

/* A line of a part with a reader of its own, into `t` (what the part gives marked); 0 for another part. */
static int read_part(KhdaysCharacter *t, enum Section section, const char *name, const char *value, char *why,
                     size_t why_size)
{
    switch (section) {
    case ACTIONS:
        read_action(t, name, value, why, why_size);
        t->gives |= KHDAYS_FORM_GIVES_ACTIONS;
        return 1;
    case SOUNDS:
        read_sound(t, name, value, why, why_size);
        t->gives |= KHDAYS_FORM_GIVES_SOUNDS;
        return 1;
    case MOVESET:
        read_moveset(t, name, value, why, why_size);
        t->gives |= KHDAYS_FORM_GIVES_MOVESET;
        return 1;
    case STATS:
        read_stats(t, name, value, why, why_size);
        t->gives |= KHDAYS_FORM_GIVES_STATS;
        return 1;
    case WEAPONS:
        read_weapon(t, name, value, why, why_size);
        t->gives |= KHDAYS_FORM_GIVES_WEAPONS;
        return 1;
    case LIMIT: {
        if (strcmp(name, "damage") != 0) {
            return 0;
        }
        char *number_end;
        const float v = strtof(value, &number_end);
        if (number_end == value || *number_end != '\0' || !(v >= 0.0f)) {
            snprintf(why, why_size, "damage %.40s: a number of times, 0 or more", value);
        } else {
            t->own_moves_damage = v;
            t->gives |= KHDAYS_FORM_GIVES_LIMIT;
        }
        return 1;
    }
    default: return 0;
    }
}

/* A [character] line; 0 when the part has no such name. `*value` is taken when kept. */
static int read_character(Reading *rd, const char *name, char **value)
{
    KhdaysCharacter *c = &rd->c;
    char **slot = NULL;
    int l;
    if (strcmp(name, "format") == 0) {
        char *digits_end;
        const long v = strtol(*value, &digits_end, 10);
        if (digits_end == *value || *digits_end != '\0' || v < 1) {
            snprintf(rd->why, sizeof(rd->why), "format: not a version (%.40s)", *value);
        } else if (v > KHDAYS_CHARACTER_FORMAT) {
            snprintf(rd->why, sizeof(rd->why), "format %ld is newer than this port reads (%d)", v,
                     KHDAYS_CHARACTER_FORMAT);
        } else {
            rd->format = (int)v;
            rd->has_format = 1;
        }
    } else if (strcmp(name, "key") == 0) {
        if (strcmp(*value, rd->key) != 0) {
            snprintf(rd->why, sizeof(rd->why), "key %.60s: not its folder's name (%.60s)", *value, rd->key);
        }
    } else if (strcmp(name, "base") == 0) {
        c->base = base_by_name(*value, KHDAYS_CHARACTER_BASES);
        if (c->base < 0) {
            snprintf(rd->why, sizeof(rd->why), "base %.60s is not one of the game's characters", *value);
        }
        rd->has_base = 1;
    } else if ((l = named(name, "name")) != -2) {
        slot = l < 0 ? &c->any_name : &c->name[l];
    } else if ((l = named(name, "weapon")) != -2) {
        slot = l < 0 ? &c->any_weapon : &c->weapon[l];
    } else if (strcmp(name, "face") == 0) {
        if (!good_reference(*value)) {
            snprintf(rd->why, sizeof(rd->why), "face %.80s: not a file inside the character's folder", *value);
        } else {
            slot = &c->face;
        }
    } else if (strcmp(name, "locked") == 0) {
        if (same_word(*value, "no")) {
            c->locked_with_base = 0;
        } else if (same_word(*value, "base")) {
            c->locked_with_base = 1;
        } else {
            snprintf(rd->why, sizeof(rd->why), "locked %.40s: no or base", *value);
        }
    } else if (strcmp(name, "gauges") == 0) {
        if (!read_bytes(*value, c->gauges, KHDAYS_CHARACTER_GAUGES)) {
            snprintf(rd->why, sizeof(rd->why), "gauges: %d numbers 0-255", KHDAYS_CHARACTER_GAUGES);
        }
        c->has_gauges = 1;
    } else {
        return 0;
    }
    if (slot != NULL) {
        free(*slot);
        *slot = *value;
        *value = NULL;
    }
    return 1;
}

/* A `name = value` line of the part being read. A form's mistake leaves that form out, not the character. */
static void read_setting(Reading *rd, const char *s, size_t n, int line)
{
    const char *eq = memchr(s, '=', n);
    if (eq == NULL) {
        fail(rd, line, "not a `name = value` line");
        return;
    }
    size_t ne = (size_t)(eq - s), vs = ne + 1;
    while (ne > 0 && isspace((unsigned char)s[ne - 1])) {
        --ne;
    }
    while (vs < n && isspace((unsigned char)s[vs])) {
        ++vs;
    }
    char *name = copy_n(s, ne), *value = copy_n(s + vs, n - vs);
    if (name == NULL || value == NULL) {
        free(name);
        free(value);
        fail(rd, line, "out of memory");
        return;
    }
    KhdaysCharacter *t = rd->form >= 0 ? &rd->c.forms[rd->form] : &rd->c;
    char form_why[300] = "";
    char *why = rd->form >= 0 ? form_why : rd->why;
    const size_t why_size = rd->form >= 0 ? sizeof(form_why) : sizeof(rd->why);
    if (rd->section == OTHER) {
        /* told at the part's head */
    } else if (read_part(t, rd->section, name, value, why, why_size)) {
    } else if (rd->section == FORM && read_form(t, name, value, why, why_size)) {
    } else if ((rd->section == MODEL || rd->section == FORM) && read_model(t, name, &value, why, why_size)) {
    } else if (rd->section == NONE) {
        snprintf(rd->why, sizeof(rd->why), "%.60s: before any part ([character] first)", name);
    } else if (rd->section == CHARACTER && read_character(rd, name, &value)) {
    } else {
        char text_note[200];
        snprintf(text_note, sizeof(text_note), "%.80s: not a name this part has (the line is not read)", name);
        note(rd, line, text_note);
    }
    if (rd->why[0] != '\0') {
        rd->why_line = line;
    }
    if (rd->form >= 0 && form_why[0] != '\0' && !(rd->c.forms[rd->form].gives & FORM_BROKEN)) {
        char text_note[360];
        snprintf(text_note, sizeof(text_note), "[form %.60s] %s: the form is left out", rd->c.forms[rd->form].form,
                 form_why);
        note(rd, line, text_note);
        rd->c.forms[rd->form].gives |= FORM_BROKEN;
    }
    free(name);
    free(value);
}

/* What every description must have: the first one missing in `why`. */
static void check_complete(Reading *rd)
{
    const KhdaysCharacter *c = &rd->c;
    rd->why_line = 0;
    if (!rd->has_format) {
        snprintf(rd->why, sizeof(rd->why), "format missing ([character] format = %d)", KHDAYS_CHARACTER_FORMAT);
    } else if (!rd->has_base) {
        snprintf(rd->why, sizeof(rd->why), "base missing (the game's character it plays as)");
    } else if (c->any_name == NULL && c->name[0] == NULL) {
        snprintf(rd->why, sizeof(rd->why), "name missing (name, or name.en)");
    } else if (c->model == NULL) {
        snprintf(rd->why, sizeof(rd->why), "its model missing ([model] file)");
    }
}

/* Format 1 has no forms, [stats] or [weapons]: a description of it that names them has them left out (told). */
static void drop_format_two(Reading *rd)
{
    KhdaysCharacter *c = &rd->c;
    if (rd->format >= 2 || (c->form_count == 0 && c->stat_count == 0 && c->weapon_count == 0)) {
        return;
    }
    note(rd, 0, "forms, [stats] and [weapons] need format = 2: they are not read");
    for (int i = 0; i < c->form_count; ++i) {
        free_character(&c->forms[i]);
    }
    c->form_count = 0;
    free(c->stats);
    c->stats = NULL;
    c->stat_count = 0;
#pragma warning(push)
#pragma warning(disable : 6001) /* the weapons up to weapon_count are read_weapon's, `game` set, `file` set or NULL */
    for (int i = 0; i < c->weapon_count; ++i) {
        free(c->weapons[i].game);
        free(c->weapons[i].file);
    }
#pragma warning(pop)
    free(c->weapons);
    c->weapons = NULL;
    c->weapon_count = 0;
}

/* Each form made whole, what it does not give taken from the character; a broken one left out (told). */
static void complete_forms(Reading *rd)
{
    KhdaysCharacter *c = &rd->c;
    for (int i = c->form_count - 1; i >= 0; --i) {
        KhdaysCharacter *f = &c->forms[i];
        char text_note[360] = "";
        if (!(f->gives & FORM_BROKEN) && f->rule == NULL) {
            snprintf(text_note, sizeof(text_note), "[form %.60s]: no `when` (it would never be chosen): left out",
                     f->form);
            f->gives |= FORM_BROKEN;
        }
        if (!(f->gives & FORM_BROKEN) && !inherit(f, c, text_note, sizeof(text_note)) && text_note[0] != '\0') {
            f->gives |= FORM_BROKEN;
        }
        if (text_note[0] != '\0') {
            note(rd, 0, text_note);
        }
        if (f->gives & FORM_BROKEN) {
            free_character(f);
            memmove(f, f + 1, (size_t)(c->form_count - i - 1) * sizeof(*f));
            --c->form_count;
        }
    }
}

/* The character's identity (`mod/key`) and its mod in the registry; 0 when out of memory. */
static int set_identity(KhdaysCharacters *r, KhdaysCharacter *c, const char *mod_id, const char *mod_folder,
                        const char *key)
{
    c->mod = add_mod(r, mod_id, mod_folder);
    const size_t n = strlen(mod_id) + 1 + strlen(key) + 1;
    c->identity = c->mod >= 0 ? malloc(n) : NULL;
    if (c->identity == NULL) {
        return 0;
    }
    snprintf(c->identity, n, "%s/%s", mod_id, key);
    c->key = c->identity + strlen(mod_id) + 1;
    return 1;
}

/* The character added to the registry's list; 0 when out of memory. */
static int append(KhdaysCharacters *r, const KhdaysCharacter *c)
{
    if (r->count == r->capacity) {
        const int grown = r->capacity == 0 ? 4 : r->capacity * 2;
        KhdaysCharacter *list = realloc(r->list, (size_t)grown * sizeof(*list));
        if (list == NULL) {
            return 0;
        }
        r->list = list;
        r->capacity = grown;
    }
    r->list[r->count++] = *c;
    return 1;
}

int khdays_characters_read(KhdaysCharacters *r, const char *mod_id, const char *mod_folder, const char *key,
                           const char *text, size_t size, KhdaysCharactersReport report, void *context)
{
    Reading rd;
    memset(&rd, 0, sizeof(rd));
    rd.section = NONE;
    rd.form = -1;
    rd.mod_id = mod_id;
    rd.key = key;
    rd.report = report;
    rd.context = context;
    rd.c.base = -1;
    rd.c.combos[KHDAYS_CHARACTER_GROUND].finisher = rd.c.combos[KHDAYS_CHARACTER_AIR].finisher = -1;
    rd.c.own_moves_damage = 1.0f;
    if (!good_key(key)) {
        note(&rd, 0, "its folder's name is not a key (letters, digits, '_' and '-' only)");
        return 0;
    }
    int line = 0;
    for (size_t at = 0; at < size && rd.why[0] == '\0';) {
        size_t a, b;
        next_line(text, size, &at, ++line == 1, &a, &b);
        if (a == b) {
            continue;
        }
        if (text[a] == '[') {
            read_head(&rd, text + a, b - a, line);
        } else {
            read_setting(&rd, text + a, b - a, line);
        }
    }
    if (rd.why[0] == '\0') {
        check_complete(&rd);
    }
    if (rd.why[0] != '\0') {
        note(&rd, rd.why_line, rd.why);
        free_character(&rd.c);
        return 0;
    }
    drop_format_two(&rd);
    if (!set_identity(r, &rd.c, mod_id, mod_folder, key)) {
        note(&rd, 0, "out of memory");
        free_character(&rd.c);
        return 0;
    }
    complete_forms(&rd);
    if (!append(r, &rd.c)) {
        note(&rd, 0, "out of memory");
        free_character(&rd.c);
        return 0;
    }
    return 1;
}

static int by_identity(const void *a, const void *b)
{
    const KhdaysCharacter *const *x = a, *const *y = b;
    return strcmp((*x)->identity, (*y)->identity);
}

int khdays_characters_number(KhdaysCharacters *r, KhdaysCharactersReport report, void *context)
{
    KhdaysCharacter **order = r->count > 0 ? malloc((size_t)r->count * sizeof(*order)) : NULL;
    int kept = 0, numbered = 0, form_number = KHDAYS_CHARACTER_FORM_FIRST;
    for (int i = 0; i < r->count; ++i) {
        KhdaysCharacter *c = &r->list[i];
        int taken = 0;
        c->number = 0;
        for (int f = 0; f < c->form_count; ++f) {
            c->forms[f].number = 0;
        }
        for (int j = 0; j < i && !taken; ++j) {
            taken = strcmp(r->list[j].identity, c->identity) == 0;
        }
        if (taken) {
            char why[300];
            snprintf(why, sizeof(why), "the same identity as one read before it (%.200s): left out", c->identity);
            report(r->mods[c->mod].id, c->key, 0, why, context);
        } else if (order != NULL) {
            order[kept++] = c;
        }
    }
    if (order == NULL) {
        return 0;
    }
    qsort(order, (size_t)kept, sizeof(*order), by_identity);
    for (int i = 0; i < kept; ++i) {
        const int number = KHDAYS_CHARACTER_FIRST + i;
        if (number > KHDAYS_CHARACTER_LAST) {
            char why[200];
            snprintf(why, sizeof(why),
                     "no number left: the game's select keeps a character in a signed byte (%d new characters at "
                     "most)",
                     KHDAYS_CHARACTER_LAST - KHDAYS_CHARACTER_FIRST + 1);
            report(r->mods[order[i]->mod].id, order[i]->key, 0, why, context);
            continue;
        }
        order[i]->number = number;
        for (int f = 0; f < order[i]->form_count; ++f) {
            order[i]->forms[f].number = form_number++;
        }
        ++numbered;
    }
    free(order);
    return numbered;
}

const KhdaysCharacter *khdays_characters_by_number(const KhdaysCharacters *r, int number)
{
    if (number < KHDAYS_CHARACTER_FIRST) {
        return NULL;
    }
    for (int i = 0; i < r->count; ++i) {
        const KhdaysCharacter *c = &r->list[i];
        if (c->number == number) {
            return c;
        }
        for (int f = 0; number >= KHDAYS_CHARACTER_FORM_FIRST && f < c->form_count; ++f) {
            if (c->forms[f].number == number) {
                return &c->forms[f];
            }
        }
    }
    return NULL;
}

const KhdaysCharacter *khdays_characters_owner(const KhdaysCharacters *r, const KhdaysCharacter *form)
{
    if (form == NULL || form->form == NULL) {
        return form;
    }
    for (int i = 0; i < r->count; ++i) {
        const KhdaysCharacter *c = &r->list[i];
        if (c->form_count > 0 && form >= c->forms && form < c->forms + c->form_count) {
            return c;
        }
    }
    return NULL;
}

const KhdaysCharacter *khdays_characters_choose(const KhdaysCharacter *c, const KhdaysFormFacts *facts)
{
    const KhdaysCharacter *best = NULL;
    for (int i = 0; c != NULL && i < c->form_count; ++i) {
        const KhdaysCharacter *f = &c->forms[i];
        if ((best == NULL || f->priority > best->priority) && khdays_rule_holds(f->rule, facts)) {
            best = f;
        }
    }
    return best;
}

const KhdaysCharacter *khdays_characters_form(const KhdaysCharacter *c, const char *id)
{
    if (c == NULL || id == NULL || strcmp(id, "default") == 0) {
        return c;
    }
    for (int i = 0; i < c->form_count; ++i) {
        if (strcmp(c->forms[i].form, id) == 0) {
            return &c->forms[i];
        }
    }
    return NULL;
}

const char *khdays_characters_form_id(const KhdaysCharacter *form)
{
    return form != NULL && form->form != NULL ? form->form : "default";
}

const KhdaysCharacterAction *khdays_characters_action(const KhdaysCharacter *c, const char *game)
{
    for (int i = 0; c != NULL && game != NULL && i < c->action_count; ++i) {
        if (strcmp(c->actions[i].game, game) == 0) {
            return &c->actions[i];
        }
    }
    return NULL;
}

const KhdaysCharacterWeapon *khdays_characters_weapon_model(const KhdaysCharacter *c, const char *game)
{
    for (int i = 0; c != NULL && game != NULL && i < c->weapon_count; ++i) {
        if (strcmp(c->weapons[i].game, game) == 0) {
            return &c->weapons[i];
        }
    }
    return NULL;
}

const KhdaysCharacterSound *khdays_characters_sound(const KhdaysCharacter *c, const char *game)
{
    const KhdaysCharacterSound *found = NULL;
    size_t longest = 0;
    for (int i = 0; c != NULL && game != NULL && i < c->sound_count; ++i) {
        const char *pattern = c->sounds[i].game;
        const size_t n = strlen(pattern);
        if (strcmp(pattern, game) == 0) {
            return &c->sounds[i];
        }
        if (n > 0 && pattern[n - 1] == '*' && n - 1 >= longest && strncmp(pattern, game, n - 1) == 0) {
            found = &c->sounds[i];
            longest = n - 1;
        }
    }
    return found;
}

const char *khdays_characters_name(const KhdaysCharacter *c, int l)
{
    if (l >= 0 && l < KHDAYS_CHARACTER_LANGUAGES && c->name[l] != NULL) {
        return c->name[l];
    }
    return c->any_name != NULL ? c->any_name : c->name[0];
}

const char *khdays_characters_weapon(const KhdaysCharacter *c, int l)
{
    if (l >= 0 && l < KHDAYS_CHARACTER_LANGUAGES && c->weapon[l] != NULL) {
        return c->weapon[l];
    }
    return c->any_weapon != NULL ? c->any_weapon : c->weapon[0];
}

void khdays_characters_drop(KhdaysCharacters *r, int index)
{
    if (index < 0 || index >= r->count) {
        return;
    }
    free_character(&r->list[index]);
    memmove(&r->list[index], &r->list[index + 1], (size_t)(r->count - index - 1) * sizeof(*r->list));
    --r->count;
}

void khdays_characters_free(KhdaysCharacters *r)
{
    for (int i = 0; i < r->count; ++i) {
        free_character(&r->list[i]);
    }
#pragma warning(push)
#pragma warning(disable : 6001) /* the mods up to mod_count are add_mod's, both strings set */
    for (int i = 0; i < r->mod_count; ++i) {
        free(r->mods[i].id);
        free(r->mods[i].folder);
    }
#pragma warning(pop)
    free(r->list);
    free(r->mods);
    memset(r, 0, sizeof(*r));
}
