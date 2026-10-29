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
} bases[KHDAYS_CHARACTER_BASES] = {
    {"roxas", "ro"},    {"axel", "ax"},   {"xigbar", "xi"},  {"saix", "sa"},    {"xaldin", "xa"},
    {"sora", "so"},     {"demyx", "de"},  {"larxene", "la"}, {"lexaeus", "le"}, {"luxord", "lu"},
    {"marluxia", "ma"}, {"riku", "ri"},   {"vexen", "ve"},   {"xemnas", "xe"},  {"xion", "xo"},
    {"zexion", "ze"},   {"mickey", "mi"}, {"donald", "do"},  {"goofy", "go"},
};

const char *khdays_character_language(int l)
{
    return l >= 0 && l < KHDAYS_CHARACTER_LANGUAGES ? languages[l] : NULL;
}

const char *khdays_character_base_name(int number)
{
    return number >= 0 && number < KHDAYS_CHARACTER_BASES ? bases[number].name : NULL;
}

const char *khdays_character_base_code(int number)
{
    return number >= 0 && number < KHDAYS_CHARACTER_BASES ? bases[number].code : NULL;
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
    return copy_n(s, strlen(s));
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
/* the actions and sounds up to their counts are read_action's and read_sound's, their strings set (a sound's file
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
    free(c->combos[KHDAYS_CHARACTER_GROUND].attacks);
    free(c->combos[KHDAYS_CHARACTER_AIR].attacks);
#pragma warning(pop)
    free(c->actions);
    free(c->sounds);
    free(c->attacks);
    free(c->fallback);
    free(c->weapon_bone);
    free(c->base_weapon_node);
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

enum Section { NONE, CHARACTER, MODEL, ACTIONS, SOUNDS, MOVESET, LIMIT, OTHER };

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
 * `attack <number>` = any of damage <x>, reach <x>, push <x> (times the base's). */
static void read_moveset(KhdaysCharacter *c, const char *name, const char *value, char *why, size_t why_size)
{
    char word[32] = "", number[32] = "";
    int used = 0, id;
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

int khdays_characters_read(KhdaysCharacters *r, const char *mod_id, const char *mod_folder, const char *key,
                           const char *text, size_t size, KhdaysCharactersReport report, void *context)
{
    KhdaysCharacter c;
    char why[300] = "";
    int why_line = 0, line = 0, format = 0, has_format = 0, has_base = 0;
    enum Section section = NONE;
    memset(&c, 0, sizeof(c));
    c.base = -1;
    c.combos[KHDAYS_CHARACTER_GROUND].finisher = c.combos[KHDAYS_CHARACTER_AIR].finisher = -1;
    c.own_moves_damage = 1.0f;
    if (!good_key(key)) {
        report(mod_id, key, 0, "its folder's name is not a key (letters, digits, '_' and '-' only)", context);
        return 0;
    }
    for (size_t at = 0; at < size && why[0] == '\0';) {
        size_t end = at;
        while (end < size && text[end] != '\n') {
            ++end;
        }
        ++line;
        /* the line without its ends' spaces (and a carriage return) */
        size_t a = at, b = end;
        at = end + 1;
        while (a < b && isspace((unsigned char)text[a])) {
            ++a;
        }
        while (b > a && isspace((unsigned char)text[b - 1])) {
            --b;
        }
        if (line == 1 && b - a >= 3 && (unsigned char)text[a] == 0xef && (unsigned char)text[a + 1] == 0xbb &&
            (unsigned char)text[a + 2] == 0xbf) {
            a += 3; /* a UTF-8 mark */
        }
        /* a comment: from a # at the start or after a space */
        for (size_t i = a; i < b; ++i) {
            if (text[i] == '#' && (i == a || isspace((unsigned char)text[i - 1]))) {
                b = i;
            }
        }
        while (b > a && isspace((unsigned char)text[b - 1])) {
            --b;
        }
        if (a == b) {
            continue;
        }
        if (text[a] == '[') {
            char *s = copy_n(text + a, b - a);
            if (s == NULL) {
                snprintf(why, sizeof(why), "out of memory");
                why_line = line;
                break;
            }
            section = strcmp(s, "[character]") == 0 ? CHARACTER
                      : strcmp(s, "[model]") == 0   ? MODEL
                      : strcmp(s, "[actions]") == 0 ? ACTIONS
                      : strcmp(s, "[sounds]") == 0  ? SOUNDS
                      : strcmp(s, "[moveset]") == 0 ? MOVESET
                      : strcmp(s, "[limit]") == 0   ? LIMIT
                                                    : OTHER;
            if (section == OTHER) {
                char note[200];
                snprintf(note, sizeof(note), "%.80s: no such part (its lines are not read)", s);
                report(mod_id, key, line, note, context);
            }
            free(s);
            continue;
        }
        const char *eq = memchr(text + a, '=', b - a);
        if (eq == NULL) {
            snprintf(why, sizeof(why), "not a `name = value` line");
            why_line = line;
            break;
        }
        size_t ne = (size_t)(eq - text), vs = ne + 1;
        while (ne > a && isspace((unsigned char)text[ne - 1])) {
            --ne;
        }
        while (vs < b && isspace((unsigned char)text[vs])) {
            ++vs;
        }
        char *name = copy_n(text + a, ne - a), *value = copy_n(text + vs, b - vs);
        if (name == NULL || value == NULL) {
            free(name);
            free(value);
            snprintf(why, sizeof(why), "out of memory");
            why_line = line;
            break;
        }
        int l;
        if (section == OTHER) {
            /* told at the part's head */
        } else if (section == ACTIONS) {
            read_action(&c, name, value, why, sizeof(why));
        } else if (section == SOUNDS) {
            read_sound(&c, name, value, why, sizeof(why));
        } else if (section == MOVESET) {
            read_moveset(&c, name, value, why, sizeof(why));
        } else if (section == LIMIT && strcmp(name, "damage") == 0) {
            char *number_end;
            const float v = strtof(value, &number_end);
            if (number_end == value || *number_end != '\0' || !(v >= 0.0f)) {
                snprintf(why, sizeof(why), "damage %.40s: a number of times, 0 or more", value);
            } else {
                c.own_moves_damage = v;
            }
        } else if (section == NONE) {
            snprintf(why, sizeof(why), "%.60s: before any part ([character] first)", name);
        } else if (section == CHARACTER && strcmp(name, "format") == 0) {
            char *digits_end;
            const long v = strtol(value, &digits_end, 10);
            if (digits_end == value || *digits_end != '\0' || v < 1) {
                snprintf(why, sizeof(why), "format: not a version (%.40s)", value);
            } else if (v > KHDAYS_CHARACTER_FORMAT) {
                snprintf(why, sizeof(why), "format %ld is newer than this port reads (%d)", v, KHDAYS_CHARACTER_FORMAT);
            } else {
                format = (int)v;
                has_format = 1;
            }
        } else if (section == CHARACTER && strcmp(name, "key") == 0) {
            if (strcmp(value, key) != 0) {
                snprintf(why, sizeof(why), "key %.60s: not its folder's name (%.60s)", value, key);
            }
        } else if (section == CHARACTER && strcmp(name, "base") == 0) {
            c.base = -1;
            for (int i = 0; i < KHDAYS_CHARACTER_BASES; ++i) {
                if (same_word(value, bases[i].name)) {
                    c.base = i;
                }
            }
            if (c.base < 0) {
                snprintf(why, sizeof(why), "base %.60s is not one of the game's characters", value);
            }
            has_base = 1;
        } else if (section == CHARACTER && (l = named(name, "name")) != -2) {
            char **slot = l < 0 ? &c.any_name : &c.name[l];
            free(*slot);
            *slot = value;
            value = NULL;
        } else if (section == CHARACTER && (l = named(name, "weapon")) != -2) {
            char **slot = l < 0 ? &c.any_weapon : &c.weapon[l];
            free(*slot);
            *slot = value;
            value = NULL;
        } else if (section == CHARACTER && strcmp(name, "face") == 0) {
            if (!good_reference(value)) {
                snprintf(why, sizeof(why), "face %.80s: not a file inside the character's folder", value);
            } else {
                free(c.face);
                c.face = value;
                value = NULL;
            }
        } else if (section == CHARACTER && strcmp(name, "locked") == 0) {
            if (same_word(value, "no")) {
                c.locked_with_base = 0;
            } else if (same_word(value, "base")) {
                c.locked_with_base = 1;
            } else {
                snprintf(why, sizeof(why), "locked %.40s: no or base", value);
            }
        } else if (section == CHARACTER && strcmp(name, "gauges") == 0) {
            if (!read_bytes(value, c.gauges, KHDAYS_CHARACTER_GAUGES)) {
                snprintf(why, sizeof(why), "gauges: %d numbers 0-255", KHDAYS_CHARACTER_GAUGES);
            }
            c.has_gauges = 1;
        } else if (section == MODEL && strcmp(name, "file") == 0) {
            if (!good_reference(value)) {
                snprintf(why, sizeof(why), "file %.80s: not a file inside the character's folder", value);
            } else {
                free(c.model);
                c.model = value;
                value = NULL;
            }
        } else if (section == MODEL && strcmp(name, "scale") == 0) {
            char *number_end;
            const double v = strtod(value, &number_end);
            if (number_end == value || *number_end != '\0' || !(v > 0.0)) {
                snprintf(why, sizeof(why), "scale %.40s: a number above 0", value);
            } else {
                c.model_scale = (float)v;
            }
        } else if (section == MODEL && (strcmp(name, "weapon_bone") == 0 || strcmp(name, "base_weapon_node") == 0)) {
            char **slot = strcmp(name, "weapon_bone") == 0 ? &c.weapon_bone : &c.base_weapon_node;
            free(*slot);
            *slot = value;
            value = NULL;
        } else if (section == MODEL && strcmp(name, "weapon_turn") == 0) {
            if (sscanf(value, "%f %f %f", &c.weapon_turn[0], &c.weapon_turn[1], &c.weapon_turn[2]) != 3) {
                snprintf(why, sizeof(why), "weapon_turn %.40s: three angles in degrees (x y z)", value);
            } else {
                c.has_weapon_turn = 1;
            }
        } else if (section == MODEL && strcmp(name, "skeleton") == 0) {
            if (same_word(value, "paired")) {
                c.own_skeleton = 0;
            } else if (same_word(value, "own")) {
                c.own_skeleton = 1;
            } else {
                snprintf(why, sizeof(why), "skeleton %.40s: paired or own", value);
            }
        } else {
            char note[200];
            snprintf(note, sizeof(note), "%.80s: not a name this part has (the line is not read)", name);
            report(mod_id, key, line, note, context);
        }
        if (why[0] != '\0') {
            why_line = line;
        }
        free(name);
        free(value);
    }
    if (why[0] == '\0') {
        why_line = 0;
        if (!has_format) {
            snprintf(why, sizeof(why), "format missing ([character] format = %d)", KHDAYS_CHARACTER_FORMAT);
        } else if (!has_base) {
            snprintf(why, sizeof(why), "base missing (the game's character it plays as)");
        } else if (c.any_name == NULL && c.name[0] == NULL) {
            snprintf(why, sizeof(why), "name missing (name, or name.en)");
        } else if (c.model == NULL) {
            snprintf(why, sizeof(why), "its model missing ([model] file)");
        }
    }
    (void)format;
    if (why[0] != '\0') {
        report(mod_id, key, why_line, why, context);
        free_character(&c);
        return 0;
    }
    c.mod = add_mod(r, mod_id, mod_folder);
    const size_t n = strlen(mod_id) + 1 + strlen(key) + 1;
    c.identity = c.mod >= 0 ? malloc(n) : NULL;
    if (c.identity == NULL) {
        report(mod_id, key, 0, "out of memory", context);
        free_character(&c);
        return 0;
    }
    snprintf(c.identity, n, "%s/%s", mod_id, key);
    c.key = c.identity + strlen(mod_id) + 1;
    if (r->count == r->capacity) {
        const int grown = r->capacity == 0 ? 4 : r->capacity * 2;
        KhdaysCharacter *list = realloc(r->list, (size_t)grown * sizeof(*list));
        if (list == NULL) {
            report(mod_id, key, 0, "out of memory", context);
            free_character(&c);
            return 0;
        }
        r->list = list;
        r->capacity = grown;
    }
    r->list[r->count++] = c;
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
    int kept = 0, numbered = 0;
    for (int i = 0; i < r->count; ++i) {
        KhdaysCharacter *c = &r->list[i];
        int taken = 0;
        c->number = 0;
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
        if (r->list[i].number == number) {
            return &r->list[i];
        }
    }
    return NULL;
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
