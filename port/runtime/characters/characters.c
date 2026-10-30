/* The mods' characters in the running game (characters.h). */
#include "characters.h"
#include "forms.h"
#include "moveset.h"
#include "packed.h"
#include "stats.h"

#include "../../host/app/instance.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../core/sha256.h"
#include "../core/window.h"
#include "../mods/list.h"
#include "../plugins/actors.h"
#include "../scripts/folders.h"
#include "../scripts/manifest.h"
#include "../scripts/mods_log.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODS "mods"

/* The game's four seats (data_020429c8, Slot4_GetIfOccupied), each a member (gPartyMembers[seat]). */
#define SEATS 4
#define TITLE_SCENE 1

static KhdaysCharacters registry;
static const KhdaysCharacter **numbered; /* by number - KHDAYS_CHARACTER_FIRST */
static int numbered_count;
static const KhdaysCharacter **forms_numbered; /* the forms, by number - KHDAYS_CHARACTER_FORM_FIRST */
static int forms_numbered_count;
static int tracing;
/* The new character each seat was given by the select (its number), 0 none: the seat itself has its
 * base's. */
static int seats[SEATS];

/* What the start found, told once the logs are open. */
typedef struct Note {
    char *mod, *key, *text;
    int line;
} Note;
static Note *notes;
static int note_count, note_capacity;

static char *copy(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c != NULL) {
        memcpy(c, s, n);
    }
    return c;
}

static void note(const char *mod, const char *key, int line, const char *why, void *context)
{
    (void)context;
    if (note_count == note_capacity) {
        const int grown = note_capacity == 0 ? 8 : note_capacity * 2;
        Note *n = realloc(notes, (size_t)grown * sizeof(*n));
        if (n == NULL) {
            return;
        }
        notes = n;
        note_capacity = grown;
    }
    notes[note_count].mod = copy(mod);
    notes[note_count].key = copy(key);
    notes[note_count].text = copy(why);
    notes[note_count].line = line;
    if (notes[note_count].mod != NULL && notes[note_count].key != NULL && notes[note_count].text != NULL) {
        ++note_count;
    }
}

static char *joined(const char *a, const char *b)
{
    const size_t n = strlen(a) + 1 + strlen(b) + 1;
    char *s = malloc(n);
    if (s != NULL) {
        snprintf(s, n, "%s/%s", a, b);
    }
    return s;
}

/* A mod's id: its mod.ini's, else its folder's name. */
static char *mod_id(const char *folder, const char *name)
{
    char *ini = joined(folder, "mod.ini"), *text = ini != NULL ? khdays_folders_read(ini, NULL) : NULL, *id = NULL;
    free(ini);
    if (text != NULL) {
        /* its `id` whatever else it says: a mod with no Lua scripts (no `api`) is the same mod to every part */
        id = khdays_manifest_value(text, strlen(text), "id");
        free(text);
    }
    if (id != NULL && id[0] != '\0') {
        return id;
    }
    free(id);
    return copy(name);
}

typedef struct ModReading {
    const char *id, *name, *folder;
} ModReading;

static void read_character(const char *key, void *context)
{
    const ModReading *mod = context;
    char path[1024];
    size_t size = 0;
    snprintf(path, sizeof(path), "%s/characters/%s/character.ini", mod->folder, key);
    char *text = khdays_folders_read(path, &size);
    if (text == NULL) {
        note(mod->id, key, 0, "characters/<key>/character.ini cannot be read", NULL);
        return;
    }
    khdays_characters_read(&registry, mod->id, mod->name, key, text, size, note, NULL);
    free(text);
}

static void read_mod(const char *name, void *context)
{
    (void)context;
    if (!khdays_mods_started_has(name)) {
        return; /* off on the Mods page as the game started (runtime/mods/list.h) */
    }
    char *folder = joined(MODS, name), *characters = folder != NULL ? joined(folder, "characters") : NULL;
    if (characters != NULL) {
        char *id = mod_id(folder, name);
        if (id != NULL) {
            ModReading mod = {id, name, folder};
            khdays_folders_list(characters, read_character, &mod);
        }
        free(id);
    }
    free(characters);
    free(folder);
}

int khdays_characters_file(const KhdaysCharacter *c, const char *relative, char *path, int size)
{
    const int n =
        snprintf(path, (size_t)size, "%s/%s/characters/%s/%s", MODS, registry.mods[c->mod].folder, c->key, relative);
    return n > 0 && n < size;
}

/* Whether the files the description names are there; told when one is not. */
static int files_there(const KhdaysCharacter *c)
{
    const char *const named[] = {c->model, c->face};
    char path[1024], why[300];
    for (int i = 0; i < 2; ++i) {
        if (named[i] != NULL &&
            !(khdays_characters_file(c, named[i], path, sizeof(path)) && khdays_folders_is_file(path))) {
            snprintf(why, sizeof(why), "%.200s cannot be read: left out", named[i]);
            note(registry.mods[c->mod].id, c->key, 0, why, NULL);
            return 0;
        }
    }
    /* a clip that cannot be read: its animation plays the fallback (told) */
    for (int i = 0; i < c->action_count; ++i) {
        if (!(khdays_characters_file(c, c->actions[i].clip, path, sizeof(path)) && khdays_folders_is_file(path))) {
            snprintf(why, sizeof(why), "%.200s (for %.60s) cannot be read: the fallback plays", c->actions[i].clip,
                     c->actions[i].game);
            note(registry.mods[c->mod].id, c->key, 0, why, NULL);
        }
    }
    for (int i = 0; i < c->weapon_count; ++i) {
        if (!(khdays_characters_file(c, c->weapons[i].file, path, sizeof(path)) && khdays_folders_is_file(path))) {
            snprintf(why, sizeof(why), "%.200s (for %.60s) cannot be read: the base's weapon is drawn",
                     c->weapons[i].file, c->weapons[i].game);
            note(registry.mods[c->mod].id, c->key, 0, why, NULL);
        }
    }
    for (int i = 0; i < c->sound_count; ++i) {
        if (c->sounds[i].file != NULL &&
            !(khdays_characters_file(c, c->sounds[i].file, path, sizeof(path)) && khdays_folders_is_file(path))) {
            snprintf(why, sizeof(why), "%.200s (for %.60s) cannot be read: the game's sound plays", c->sounds[i].file,
                     c->sounds[i].game);
            note(registry.mods[c->mod].id, c->key, 0, why, NULL);
        }
    }
    if (c->own_skeleton && c->fallback == NULL) {
        note(registry.mods[c->mod].id, c->key, 0,
             "skeleton = own without [actions] fallback: an animation with no clip shows its rest pose", NULL);
    }
    return 1;
}

/* Its combo files with its [moveset] (moveset.h), packed as the game's: NULL when it changes nothing there, or
 * when they cannot be made (told: its base's moves play). */
typedef struct Moveset {
    unsigned char *ci, *cm;
    size_t ci_size, cm_size;
} Moveset;

/* A file of its base's folder, unpacked; NULL when it cannot be. */
static unsigned char *base_file(const KhdaysCharacter *c, const char *name, size_t *size)
{
    char path[64];
    u32 offset, bytes;
    unsigned char *packed, *raw = NULL;
    snprintf(path, sizeof(path), "ba/ch/%s/%s", khdays_character_base_code(c->base), name);
    if (!khdays_rom_find(path, &offset, &bytes) || (packed = malloc(bytes > 0 ? bytes : 1)) == NULL) {
        return NULL;
    }
    khdays_rom_read(offset, packed, bytes);
    if (!khdays_characters_unpack(packed, bytes, &raw, size)) {
        raw = NULL;
    }
    free(packed);
    return raw;
}

static void moveset_files(const KhdaysCharacter *c, Moveset *m)
{
    char why[300] = "", note_text[340];
    size_t ci_size = 0, cm_size = 0, combos_size = 0;
    unsigned char *ci, *cm, *combos = NULL;
    memset(m, 0, sizeof(*m));
    if (!khdays_moveset_wanted(c)) {
        return;
    }
    ci = base_file(c, "ci.b.z", &ci_size);
    cm = base_file(c, "cm.b.z", &cm_size);
    if (ci == NULL || cm == NULL) {
        snprintf(why, sizeof(why), "its base's combo files (ci.b.z, cm.b.z) cannot be read");
    } else if (khdays_moveset_attacks(c, cm, cm_size, why, sizeof(why)) &&
               khdays_moveset_combos(c, ci, ci_size, cm, cm_size, &combos, &combos_size, why, sizeof(why)) &&
               (!khdays_characters_pack(combos, combos_size, &m->ci, &m->ci_size) ||
                (c->attack_count > 0 && !khdays_characters_pack(cm, cm_size, &m->cm, &m->cm_size)))) {
        snprintf(why, sizeof(why), "out of memory");
    }
    if (why[0] != '\0') {
        snprintf(note_text, sizeof(note_text), "[moveset] %s: its base's moves play", why);
        note(registry.mods[c->mod].id, c->key, 0, note_text, NULL);
        free(m->ci);
        free(m->cm);
        memset(m, 0, sizeof(*m));
    }
    free(ci);
    free(cm);
    free(combos);
}

/* Its level table made from its [stats] and its base's (stats.h), packed as the game's; NULL when it has none, or
 * when it cannot be made (told: its base's levels). */
static unsigned char *stats_file(const KhdaysCharacter *c, size_t *size)
{
    size_t base_size = 0;
    unsigned char table[KHDAYS_STATS_TABLE], *packed = NULL, *base;
    if (c->stat_count == 0) {
        return NULL;
    }
    base = base_file(c, "lv.b.z", &base_size);
    if (base == NULL || !khdays_stats_table(c->stats, c->stat_count, base, base_size, table) ||
        !khdays_characters_pack(table, sizeof(table), &packed, size)) {
        note(registry.mods[c->mod].id, c->key, 0, "[stats]: its base's level table cannot be read: its base's levels",
             NULL);
        packed = NULL;
    }
    free(base);
    return packed;
}

/* A file of its own given in the mod: a form's forms/<id>/data/<file>, else (a form on its character's base, or the
 * character itself) data/<file>; 0 when there is none. */
static int own_data(const KhdaysCharacter *c, const KhdaysCharacter *owner, const char *rest, char *disk, int size)
{
    char relative[300];
    if (c->form != NULL) {
        snprintf(relative, sizeof(relative), "forms/%s/data/%s", c->form, rest);
        if (khdays_characters_file(c, relative, disk, size) && khdays_folders_is_file(disk)) {
            return 1;
        }
        if (owner == NULL || owner->base != c->base) {
            return 0; /* its character's files are another base's */
        }
    }
    snprintf(relative, sizeof(relative), "data/%s", rest);
    return khdays_characters_file(c, relative, disk, size) && khdays_folders_is_file(disk);
}

/* Its folder ba/ch/<code>/: every file of its base's folder, the mod's data/<file> when there, else its
 * [moveset]'s combo files or its [stats]' level table, else a copy of the base's bytes (its own place in the ROM:
 * what is loaded from it is known as the character's). A form's, the same from its own base's folder. */
static int serve_folder(const KhdaysCharacter *c, const KhdaysCharacter *owner)
{
    char base[24], own[24], path[256], target[256], disk[1024];
    Moveset moveset;
    size_t levels_size = 0;
    unsigned char *levels;
    snprintf(base, sizeof(base), "ba/ch/%s/", khdays_character_base_code(c->base));
    snprintf(own, sizeof(own), "ba/ch/c%02x/", c->number);
    const size_t base_length = strlen(base);
    int served = 0;
    moveset_files(c, &moveset);
    levels = stats_file(c, &levels_size);
    for (int id = 0; id < khdays_rom_file_count(); ++id) {
        u32 offset, bytes;
        if (!khdays_rom_file_info(id, path, (int)sizeof(path), &offset, &bytes) ||
            strncmp(path, base, base_length) != 0) {
            continue;
        }
        const char *rest = path + base_length;
        snprintf(target, sizeof(target), "%s%s", own, rest);
        if (own_data(c, owner, rest, disk, (int)sizeof(disk))) {
            if ((moveset.ci != NULL && strcmp(rest, "ci.b.z") == 0) ||
                (moveset.cm != NULL && strcmp(rest, "cm.b.z") == 0)) {
                note(registry.mods[c->mod].id, c->key, 0, "[moveset] left out where its mod's data/ has the file",
                     NULL);
            }
            if (levels != NULL && strcmp(rest, "lv.b.z") == 0) {
                note(registry.mods[c->mod].id, c->key, 0, "[stats] left out where its mod's data/ has lv.b.z", NULL);
            }
            served += khdays_rom_add_file(target, disk);
            continue;
        }
        if (levels != NULL && strcmp(rest, "lv.b.z") == 0) {
            served += khdays_rom_add_bytes(target, levels, (unsigned)levels_size);
            continue;
        }
        if (moveset.ci != NULL && strcmp(rest, "ci.b.z") == 0) {
            served += khdays_rom_add_bytes(target, moveset.ci, (unsigned)moveset.ci_size);
            continue;
        }
        if (moveset.cm != NULL && strcmp(rest, "cm.b.z") == 0) {
            served += khdays_rom_add_bytes(target, moveset.cm, (unsigned)moveset.cm_size);
            continue;
        }
        unsigned char *data = malloc(bytes > 0 ? bytes : 1);
        if (data == NULL) {
            continue;
        }
        khdays_rom_read(offset, data, bytes);
        served += khdays_rom_add_bytes(target, data, bytes);
        free(data);
    }
    free(moveset.ci);
    free(moveset.cm);
    free(levels);
    return served;
}

/* A form's own files there, as a character's (a clip, a sound or a weapon that cannot be read is told). 0 leaves
 * the form out. */
static int form_files_there(const KhdaysCharacter *f)
{
    return files_there(f);
}

void khdays_characters_start(void)
{
    tracing = getenv("KHDAYS_TRACE_CHARACTERS") != NULL;
    khdays_folders_list(MODS, read_mod, NULL);
    if (registry.count == 0) {
        return;
    }
    for (int i = registry.count - 1; i >= 0; --i) {
        if (!files_there(&registry.list[i])) {
            khdays_characters_drop(&registry, i); /* numbering is over the rest */
        }
    }
    khdays_characters_number(&registry, note, NULL);
    numbered_count = 0;
    for (int i = 0; i < registry.count; ++i) {
        if (registry.list[i].number > 0 && registry.list[i].number - KHDAYS_CHARACTER_FIRST + 1 > numbered_count) {
            numbered_count = registry.list[i].number - KHDAYS_CHARACTER_FIRST + 1;
        }
    }
    numbered = numbered_count > 0 ? calloc((size_t)numbered_count, sizeof(*numbered)) : NULL;
    for (int i = 0; i < registry.count && numbered != NULL; ++i) {
        KhdaysCharacter *c = &registry.list[i];
        if (c->number == 0) {
            continue;
        }
        if (serve_folder(c, NULL) == 0) {
            note(registry.mods[c->mod].id, c->key, 0, "its base's folder cannot be copied: left out", NULL);
            c->number = 0;
            continue;
        }
        numbered[c->number - KHDAYS_CHARACTER_FIRST] = c;
        for (int f = 0; f < c->form_count; ++f) {
            KhdaysCharacter *form = &c->forms[f];
            if (form->number - KHDAYS_CHARACTER_FORM_FIRST + 1 > forms_numbered_count) {
                forms_numbered_count = form->number - KHDAYS_CHARACTER_FORM_FIRST + 1;
            }
        }
    }
    forms_numbered = forms_numbered_count > 0 ? calloc((size_t)forms_numbered_count, sizeof(*forms_numbered)) : NULL;
    for (int i = 0; i < registry.count && forms_numbered != NULL; ++i) {
        KhdaysCharacter *c = &registry.list[i];
        for (int f = 0; c->number != 0 && f < c->form_count; ++f) {
            KhdaysCharacter *form = &c->forms[f];
            char why[300];
            if (form->number < KHDAYS_CHARACTER_FORM_FIRST || !form_files_there(form) ||
                serve_folder(form, c) == 0) {
                snprintf(why, sizeof(why), "[form %.60s] cannot be served (its files, or its base's folder): left out",
                         form->form);
                note(registry.mods[c->mod].id, c->key, 0, why, NULL);
                form->number = 0;
                continue;
            }
            forms_numbered[form->number - KHDAYS_CHARACTER_FORM_FIRST] = form;
        }
    }
}

const KhdaysCharacter *khdays_characters_seat_character(int seat)
{
    return seat >= 0 && seat < SEATS ? khdays_characters_numbered(seats[seat]) : NULL;
}

void khdays_characters_report(void)
{
    if (registry.count == 0 && note_count == 0) {
        return;
    }
    khdays_mods_log_open(khdays_host_user_folder());
    for (int i = 0; i < note_count; ++i) {
        char file[300];
        snprintf(file, sizeof(file), "characters/%s/character.ini", notes[i].key);
        khdays_mods_log(notes[i].mod, "characters", file, notes[i].line, notes[i].text);
        khdays_log("characters: %s/%s: %s%s%d", notes[i].mod, notes[i].key, notes[i].text,
                   notes[i].line > 0 ? ", line " : "", notes[i].line);
        free(notes[i].mod);
        free(notes[i].key);
        free(notes[i].text);
    }
    note_count = 0;
    for (int n = 0; n < numbered_count; ++n) {
        const KhdaysCharacter *c = numbered[n];
        if (c != NULL) {
            khdays_log("characters: %s, number 0x%02x, plays as %s, its folder ba/ch/c%02x/", c->identity, c->number,
                       khdays_character_base_name(c->base), c->number);
        }
        for (int f = 0; c != NULL && f < c->form_count; ++f) {
            const KhdaysCharacter *form = &c->forms[f];
            if (form->number != 0) {
                khdays_log("characters: %s, form %s (0x%02x) on %s, priority %d, its folder ba/ch/c%02x/", c->identity,
                           form->form, form->number, khdays_character_base_name(form->base), form->priority,
                           form->number);
            }
        }
    }
}

const KhdaysCharacter *khdays_characters_numbered(int number)
{
    if (number >= KHDAYS_CHARACTER_FORM_FIRST) {
        const int at = number - KHDAYS_CHARACTER_FORM_FIRST;
        return at < forms_numbered_count ? forms_numbered[at] : NULL;
    }
    const int index = number - KHDAYS_CHARACTER_FIRST;
    return index >= 0 && index < numbered_count ? numbered[index] : NULL;
}

/* The clip a native plugin plays on each numbered character (khdays_characters_play), NULL none. */
typedef struct Played {
    const KhdaysCharacterAction *action;
    unsigned since;
    int loop;
} Played;

static Played *played; /* by number - KHDAYS_CHARACTER_FIRST, made as the first is played */

int khdays_characters_play(int number, const char *action, int loop, unsigned now)
{
    const KhdaysCharacter *c = khdays_characters_numbered(number);
    const KhdaysCharacterAction *a = c != NULL && c->own_skeleton ? khdays_characters_action(c, action) : NULL;
    if (a == NULL) {
        return 0;
    }
    if (played == NULL && (played = calloc((size_t)numbered_count, sizeof(Played))) == NULL) {
        return 0;
    }
    played[number - KHDAYS_CHARACTER_FIRST].action = a;
    played[number - KHDAYS_CHARACTER_FIRST].since = now;
    played[number - KHDAYS_CHARACTER_FIRST].loop = loop;
    return 1;
}

void khdays_characters_play_none(int number)
{
    if (played != NULL && khdays_characters_numbered(number) != NULL) {
        played[number - KHDAYS_CHARACTER_FIRST].action = NULL;
    }
}

const KhdaysCharacterAction *khdays_characters_played(const KhdaysCharacter *c, unsigned *since, int *loop)
{
    const Played *p = played != NULL && c != NULL && c->number > 0 ? &played[c->number - KHDAYS_CHARACTER_FIRST] : NULL;
    if (p == NULL || p->action == NULL) {
        return NULL;
    }
    *since = p->since;
    *loop = p->loop;
    return p->action;
}

int khdays_characters_known(int number)
{
    return khdays_characters_numbered(number) != NULL;
}

int khdays_characters_base(int number)
{
    const KhdaysCharacter *c = khdays_characters_numbered(number);
    return c != NULL ? c->base : number;
}

void khdays_characters_seat(int seat, int place)
{
    if (seat < 0 || seat >= SEATS) {
        return;
    }
    const KhdaysCharacter *c = place >= 0 ? khdays_characters_numbered(place & 0xff) : NULL;
    const int number = c != NULL ? c->number : 0;
    if (tracing && number != seats[seat] && (number != 0 || place >= 0)) {
        khdays_log("characters: seat %d is %s", seat, c != NULL ? c->identity : "one of the game's");
    }
    seats[seat] = number;
}

int khdays_characters_place(int seat, int entry)
{
    const int n = khdays_characters_of(seat, -1);
    return n >= 0 && khdays_characters_menu_entry(n) == entry ? n : entry;
}

void khdays_characters_scene(int scene)
{
    if (scene == TITLE_SCENE) {
        memset(seats, 0, sizeof(seats)); /* Mission Mode left: the story never has them */
        khdays_characters_forms_clear();
    }
}

int khdays_characters_of(int slot, int kind)
{
    if (slot < 0 || slot >= SEATS || seats[slot] == 0) {
        return kind;
    }
    const KhdaysCharacter *c = khdays_characters_numbered(seats[slot]);
    if (c == NULL) {
        return kind;
    }
    /* the seat's form (chosen at the camp, or told by the room) when it plays as `kind`; else any of its forms that
     * plays as `kind` (a room that did not tell: one form a base answers); else its own */
    const KhdaysCharacter *f = khdays_characters_seat_form_now(slot);
    if (f != NULL && f->number != 0 && (kind < 0 || kind == f->base)) {
    } else if (kind >= 0 && kind != c->base) {
        f = NULL;
        for (int i = 0; i < c->form_count && f == NULL; ++i) {
            f = c->forms[i].base == kind && c->forms[i].number != 0 ? &c->forms[i] : NULL;
        }
        if (f == NULL) {
            return kind; /* not the seat's character's actor */
        }
    } else {
        f = c;
    }
    if (tracing) {
        static int told[SEATS];
        if (told[slot] != f->number && f == c) {
            khdays_log("characters: member %d is %s (0x%02x), on %s's code", slot, c->identity, c->number,
                       khdays_character_base_name(c->base));
        } else if (told[slot] != f->number) {
            khdays_log("characters: member %d is %s, form %s (0x%02x), on %s's code", slot, c->identity,
                       khdays_characters_form_id(f), f->number, khdays_character_base_name(f->base));
        }
        told[slot] = f->number;
    }
    return f->number;
}

/* A character's part of the rooms' fingerprint, growing (a file that cannot be read is left out: the copies'
 * parts differ then). */
typedef struct RoomPart {
    KhdaysSha256 sha;
    const char *folder; /* its character's folder */
} RoomPart;

static void room_file(const char *relative, void *context)
{
    RoomPart *part = context;
    char path[1200];
    size_t size = 0;
    char *bytes;
    snprintf(path, sizeof(path), "%s/data/%s", part->folder, relative);
    bytes = khdays_folders_read(path, &size);
    if (bytes == NULL) {
        return;
    }
    khdays_sha256_add(&part->sha, relative, strlen(relative) + 1);
    khdays_sha256_add(&part->sha, bytes, size);
    free(bytes);
}

int khdays_characters_room_entry(int index, unsigned char digest[32], const char **name, const char **identity)
{
    const KhdaysCharacter *c = khdays_characters_at(index);
    RoomPart part;
    char folder[1024], data[1100], *ini;
    size_t size = 0;
    if (c == NULL || !khdays_characters_file(c, "", folder, sizeof(folder))) {
        return 0;
    }
    folder[strlen(folder) - 1] = '\0'; /* its folder, without the '/' an empty reference left */
    part.folder = folder;
    khdays_sha256_begin(&part.sha);
    khdays_sha256_add(&part.sha, c->identity, strlen(c->identity) + 1);
    snprintf(data, sizeof(data), "%s/character.ini", folder);
    ini = khdays_folders_read(data, &size);
    if (ini != NULL) {
        khdays_sha256_add(&part.sha, ini, size);
        free(ini);
    }
    snprintf(data, sizeof(data), "%s/data", folder);
    khdays_folders_files(data, room_file, &part);
    /* each form's own data/ (its description is in character.ini, above) */
    for (int f = 0; f < c->form_count; ++f) {
        char form_folder[1100];
        snprintf(form_folder, sizeof(form_folder), "%s/forms/%s", folder, c->forms[f].form);
        snprintf(data, sizeof(data), "%s/data", form_folder);
        khdays_sha256_add(&part.sha, c->forms[f].form, strlen(c->forms[f].form) + 1);
        part.folder = form_folder;
        khdays_folders_files(data, room_file, &part);
    }
    khdays_sha256_end(&part.sha, digest);
    *name = khdays_characters_name(c, 0);
    *identity = c->identity;
    return 1;
}

int khdays_characters_own_moves(int seat, int kind, int factor)
{
    const KhdaysCharacter *c = khdays_characters_numbered(khdays_characters_of(seat, kind));
    if (c == NULL || kind != c->base || c->own_moves_damage == 1.0f) {
        return factor;
    }
    static int told_factor = -1;
    if (tracing && told_factor != factor) {
        told_factor = factor;
        khdays_log("characters: %s's own move: its damage factor %.3f times %.2f", c->identity, factor / 4096.0,
                   c->own_moves_damage);
    }
    return (int)lroundf((float)factor * c->own_moves_damage);
}

void khdays_characters_attack(const void *actor, int seat, int kind, int attack)
{
    static int told[SEATS] = {-1, -1, -1, -1};
    const KhdaysCharacter *c;
    khdays_plugins_character_attack(actor != NULL ? KHDAYS_REF(actor) : 0, seat, attack);
    if (!tracing || seat < 0 || seat >= SEATS || told[seat] == attack ||
        (c = khdays_characters_numbered(khdays_characters_of(seat, kind))) == NULL || kind != c->base) {
        return;
    }
    told[seat] = attack;
    khdays_log("characters: %s (seat %d) attacks with attack %d", c->identity, seat, attack);
}

/* The new character whose actor the game starts sounds for, NULL none. */
static const KhdaysCharacter *speaker;

void khdays_characters_speaker(int seat, int kind)
{
    const int n = seat >= 0 && kind >= 0 ? khdays_characters_of(seat, kind) : kind;
    speaker = n != kind ? khdays_characters_numbered(n) : NULL;
}

/* A character's sound file: its path, and the name the traces give it ("<identity>: <file>"). */
static int sound_path(const KhdaysCharacter *c, const char *file, char *path, int size, char *name, int name_size)
{
    snprintf(name, (size_t)name_size, "%s: %s", c->identity, file);
    return khdays_characters_file(c, file, path, size);
}

int khdays_characters_speaker_sound(const char *game, char *path, int size, char *name, int name_size)
{
    const KhdaysCharacterSound *s = khdays_characters_sound(speaker, game);
    if (s == NULL) {
        return 0;
    }
    if (tracing) {
        khdays_log("characters: %s's %s: %s", speaker->identity, game, s->file != NULL ? s->file : "none");
    }
    return s->file == NULL ? -1 : sound_path(speaker, s->file, path, size, name, name_size) ? 1 : 0;
}

/* The numbered characters and then the numbered forms with sounds of their own, one by one (`index` from 0); NULL
 * for one to pass over. A form that gives no [sounds] has its character's files (the same names: decoded once). */
static const KhdaysCharacter *numbered_at(int index)
{
    if (index < numbered_count) {
        return numbered[index];
    }
    index -= numbered_count;
    const KhdaysCharacter *f = index < forms_numbered_count ? forms_numbered[index] : NULL;
    return f != NULL && (f->gives & KHDAYS_FORM_GIVES_SOUNDS) ? f : NULL;
}

int khdays_characters_have_sounds(void)
{
    for (int i = 0; i < numbered_count + forms_numbered_count; ++i) {
        const KhdaysCharacter *c = numbered_at(i);
        if (c != NULL && c->sound_count > 0) {
            return 1;
        }
    }
    return 0;
}

int khdays_characters_sound_file(int index, char *path, int size, char *name, int name_size)
{
    for (int i = 0; i < numbered_count + forms_numbered_count; ++i) {
        const KhdaysCharacter *c = numbered_at(i);
        for (int s = 0; c != NULL && s < c->sound_count; ++s) {
            if (c->sounds[s].file != NULL && index-- == 0) {
                if (!sound_path(c, c->sounds[s].file, path, size, name, name_size)) {
                    path[0] = '\0';
                }
                return 1;
            }
        }
    }
    return 0;
}

/* The select's place of each game character by its data folder's number (Ov006_MenuEntryToSlot's inverse). */
static const int menu_of_base[KHDAYS_CHARACTER_BASES] = {12, 7,  1, 6, 2,  15, 8,  11, 4, 9,
                                                         10, 16, 3, 0, 13, 5,  14, 17, 18};

int khdays_characters_menu_entry(int id)
{
    const KhdaysCharacter *c = khdays_characters_numbered(id & 0xff);
    return c != NULL ? menu_of_base[c->base] : id;
}

int khdays_characters_count(void)
{
    int n = 0;
    for (int i = 0; i < numbered_count; ++i) {
        n += numbered[i] != NULL;
    }
    return n;
}

const KhdaysCharacter *khdays_characters_at(int index)
{
    for (int i = 0; i < numbered_count; ++i) {
        if (numbered[i] != NULL && index-- == 0) {
            return numbered[i];
        }
    }
    return NULL;
}

const KhdaysCharacter *khdays_characters_of_model(const char *name)
{
    /* the body's packs (def.p.z, def_hb.p.z, def_hho.p.z: the overlays' constructors load one of them) */
    static const char head[] = "ba/ch/c";
    const size_t n = sizeof(head) - 1;
    if (numbered_count == 0 || strncmp(name, head, n) != 0) {
        return NULL;
    }
    /* its folder's number in hex: two digits for a character, more for a form */
    char *end;
    const long number = strtol(name + n, &end, 16);
    if (end == name + n || strncmp(end, "/def", 4) != 0) {
        return NULL;
    }
    return khdays_characters_numbered((int)number);
}

int khdays_characters_model(const char *name, char *path, int size)
{
    const KhdaysCharacter *c = khdays_characters_of_model(name);
    if (c != NULL) {
        return c->model != NULL && khdays_characters_file(c, c->model, path, size);
    }
    /* a weapon of its folder's (ba/ch/c<number>/w_.p2#<entry>/mdl/<model>): its [weapons] file for the model */
    static const char head[] = "ba/ch/c";
    char *end;
    const size_t n = sizeof(head) - 1;
    if (numbered_count == 0 || strncmp(name, head, n) != 0) {
        return 0;
    }
    const long number = strtol(name + n, &end, 16);
    const char *model = strstr(name, "/mdl/");
    if (end == name + n || strncmp(end, "/w_.p2", 6) != 0 || model == NULL) {
        return 0;
    }
    const KhdaysCharacterWeapon *w = khdays_characters_weapon_model(khdays_characters_numbered((int)number), model + 5);
    return w != NULL && khdays_characters_file(khdays_characters_numbered((int)number), w->file, path, size);
}
