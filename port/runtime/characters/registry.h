/* The playable characters the mods add: each mod's characters/<key>/character.ini read into descriptions, then
 * given this run's numbers past the game's, in the order of their identities ("<mod id>/<key>"), so the same mods
 * give the same numbers on every copy. The numbers are never saved. docs/RUNTIME_MAP.md, "Characters of mods".
 * Plain C: no game, no system calls. */
#ifndef KHDAYS_CHARACTERS_REGISTRY_H
#define KHDAYS_CHARACTERS_REGISTRY_H

#include "rules.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_CHARACTER_FORMAT 2    /* the description's version this port reads (2: forms and stats) */
#define KHDAYS_CHARACTER_LANGUAGES 5 /* the game's order (GetLanguage - 1): en fr de it es */

/* A new character's number is held by the select's places only (their +2, a signed byte:
 * Ov006_GetMissionRowInfo), past the game's characters 0x00-0x15 (data_02042160, the signature skills, has 22). */
#define KHDAYS_CHARACTER_FIRST 0x16
#define KHDAYS_CHARACTER_LAST 0x7f

/* The game's characters a new one can play as: their data folders' numbers (data_02042a70), 0-18. A form may also
 * play as the game's dual-wield Roxas, folder 19 (`roxas_dual`, ba/ch/r2). */
#define KHDAYS_CHARACTER_BASES 19
#define KHDAYS_CHARACTER_FOLDERS 20
#define KHDAYS_CHARACTER_DUAL 19

/* A form's folder number: past the characters' and past a byte (the select's places, read `& 0xff`, never
 * reach one), with no end. */
#define KHDAYS_CHARACTER_FORM_FIRST 0x100

/* A level table's levels (lv.b.z: LevelTable_ReadEntry's rows). */
#define KHDAYS_CHARACTER_LEVELS 100

/* A level row's values as the game uses them: strength (Ov022_ComputeStrengthDamage), magic, defence
 * (Ov022_ComputeDamageTaken), HP (Ov022_ApplyMissionLevelStats), and two the port does not know yet. */
enum KhdaysCharacterStat {
    KHDAYS_STAT_STRENGTH,
    KHDAYS_STAT_MAGIC,
    KHDAYS_STAT_DEFENCE,
    KHDAYS_STAT_HP,
    KHDAYS_STAT_UNKNOWN_8,  /* the row's +8 */
    KHDAYS_STAT_UNKNOWN_12, /* the row's +12 (a 20.12 number in the game's tables: 5 for every level) */
    KHDAYS_STATS
};

/* A value given at a level in [stats]: the rest of the levels are made from these (stats.h). */
typedef struct KhdaysCharacterStatPoint {
    int level; /* 1-100 */
    int stat;  /* KhdaysCharacterStat */
    long value;
} KhdaysCharacterStatPoint;

/* What a form gives itself; the rest is its default form's (the character's own). */
#define KHDAYS_FORM_GIVES_MODEL 0x001u
#define KHDAYS_FORM_GIVES_SKELETON 0x002u
#define KHDAYS_FORM_GIVES_SCALE 0x004u
#define KHDAYS_FORM_GIVES_WEAPON_BONE 0x008u
#define KHDAYS_FORM_GIVES_WEAPON_TURN 0x010u
#define KHDAYS_FORM_GIVES_BASE_WEAPON_NODE 0x020u
#define KHDAYS_FORM_GIVES_ACTIONS 0x040u
#define KHDAYS_FORM_GIVES_SOUNDS 0x080u
#define KHDAYS_FORM_GIVES_MOVESET 0x100u
#define KHDAYS_FORM_GIVES_LIMIT 0x200u
#define KHDAYS_FORM_GIVES_STATS 0x400u
#define KHDAYS_FORM_GIVES_WEAPONS 0x800u

/* The select's nine numbers for a character (data_ov006_020561e0's row). */
#define KHDAYS_CHARACTER_GAUGES 9

/* A blend's time when an action gives none: from the clip before to the new one. */
#define KHDAYS_CHARACTER_BLEND_MS 100

/* One of the base's animations given a clip of the character's own (its skeleton's). */
typedef struct KhdaysCharacterAction {
    char *game;    /* the animation the base's code plays ("b_ro_A001") */
    char *clip;    /* relative to the character's folder */
    int blend_ms;  /* from the clip before */
    int root_keep; /* the clip's own travel of its root kept (else the game's movement alone) */
    int game_time; /* stretched to the game's animation's length (else at its own speed) */
} KhdaysCharacterAction;

/* A combo of the character's ([moveset] ground / air): the base's attacks in order (their records' numbers in its
 * cm.b), then its finisher; each keyblade's combo of the base's is replaced by it. */
typedef struct KhdaysCharacterCombo {
    int *attacks;
    int count;    /* 0: each keyblade's own chain */
    int finisher; /* -1: each keyblade's own */
} KhdaysCharacterCombo;

/* One of the base's attacks changed ([moveset] attack <number>): its hits' damage, reach (their radius and how far
 * out they are) and push, times the base's. */
typedef struct KhdaysCharacterAttack {
    int id;
    float damage, reach, push;
} KhdaysCharacterAttack;

#define KHDAYS_CHARACTER_GROUND 0
#define KHDAYS_CHARACTER_AIR 1

/* One of the base's weapons' models drawn as a model of the character's ([weapons]): the game's model's name
 * (`ro_w13600`, as KHDAYS_TRACE_MODELS names it in ba/ch/<code>/w_.p2) and its file. */
typedef struct KhdaysCharacterWeapon {
    char *game;
    char *file; /* relative to the character's folder */
} KhdaysCharacterWeapon;

/* One of the base's sounds the character makes its own way ([sounds]): its voice. */
typedef struct KhdaysCharacterSound {
    char *game; /* the sound's name in the game ("seqarc/PLY_ROX/vo_rs02at07"); ending in '*': each one it begins */
    char *file; /* relative to the character's folder; NULL: not heard */
} KhdaysCharacterSound;

typedef struct KhdaysCharacter {
    char *identity;  /* "<mod id>/<key>" */
    const char *key; /* inside identity: the character's folder's name */
    int mod;         /* in the registry's mods */
    int base;        /* the game character it plays as, 0-18 */
    /* UTF-8; NULL: the one without a language */
    char *name[KHDAYS_CHARACTER_LANGUAGES], *weapon[KHDAYS_CHARACTER_LANGUAGES];
    char *any_name, *any_weapon;
    char *face;  /* relative to the character's folder */
    char *model; /* the same */
    int own_skeleton;
    float model_scale;              /* the model's size to the game's (an FBX in centimetres: 100); 0 not given */
    KhdaysCharacterAction *actions; /* skeleton = own: [actions] */
    int action_count;
    KhdaysCharacterSound *sounds; /* [sounds] */
    int sound_count;
    KhdaysCharacterCombo combos[2]; /* [moveset]: KHDAYS_CHARACTER_GROUND, KHDAYS_CHARACTER_AIR */
    KhdaysCharacterAttack *attacks;
    int attack_count;
    float own_moves_damage; /* [limit] damage: its base's own moves' (its Limit's), times the base's (1 unchanged) */
    char *fallback;         /* the clip of an animation with none */
    char *weapon_bone;      /* skeleton = own: its bone the weapon follows; NULL: the base's hand's place */
    char *base_weapon_node; /* the base model's node the game puts the weapon on; NULL: its *_w_tg_R */
    int has_weapon_turn;    /* the weapon follows its bone's own axes, turned by weapon_turn (degrees x y z);
                             * else it is held as the base holds it */
    float weapon_turn[3];
    int has_gauges;
    unsigned char gauges[KHDAYS_CHARACTER_GAUGES];
    int locked_with_base; /* offered only when its base is */
    int number;           /* this run's (khdays_characters_number); 0 not numbered */
    /* format 2 */
    KhdaysCharacterStatPoint *stats; /* [stats]: its values at some levels; none: its base's table */
    int stat_count;
    KhdaysCharacterWeapon *weapons; /* [weapons]: the base's weapons drawn as its own models */
    int weapon_count;
    struct KhdaysCharacter *forms; /* the character's other forms, in the order written ([form <id>]) */
    int form_count;
    /* a form's own: its id (NULL for a character), its rule and priority, what it gives itself */
    char *form;
    KhdaysRule *rule;
    int priority;
    unsigned gives;
} KhdaysCharacter;

typedef struct KhdaysCharacterMod {
    char *id, *folder;
} KhdaysCharacterMod;

typedef struct KhdaysCharacters {
    KhdaysCharacter *list;
    int count, capacity;
    KhdaysCharacterMod *mods;
    int mod_count, mod_capacity;
} KhdaysCharacters;

/* Where a character that is left out is told of: its mod, its key, the line of its description (0 none) and
 * why. */
typedef void (*KhdaysCharactersReport)(const char *mod, const char *key, int line, const char *why, void *context);

/* The game's two letters of language `l` (0-4): "en" ... */
const char *khdays_character_language(int l);

/* Base `number`'s name in a description ("roxas"), its folder's code ("ro"); NULL past the folders (19,
 * "roxas_dual", only a form's). */
const char *khdays_character_base_name(int number);
const char *khdays_character_base_code(int number);

/* Reads the description `text` (`size` bytes) of the character in folder `key` of mod `mod_id` (folder
 * `mod_folder`). Returns 1 when it is taken; 0 with the reason told through `report`. */
int khdays_characters_read(KhdaysCharacters *r, const char *mod_id, const char *mod_folder, const char *key,
                           const char *text, size_t size, KhdaysCharactersReport report, void *context);

/* Gives each character its number, KHDAYS_CHARACTER_FIRST on in the order of their identities; a character
 * whose identity another one read before it has, or past the last number, is left out (told). Returns how
 * many are numbered. */
int khdays_characters_number(KhdaysCharacters *r, KhdaysCharactersReport report, void *context);

/* The numbered character or form with `number` (a form's folder number from KHDAYS_CHARACTER_FORM_FIRST); NULL for
 * any other. A form's character: `khdays_characters_owner`. */
const KhdaysCharacter *khdays_characters_by_number(const KhdaysCharacters *r, int number);
const KhdaysCharacter *khdays_characters_owner(const KhdaysCharacters *r, const KhdaysCharacter *form);

/* Character `c`'s form whose rule holds for `facts`, by priority then the order written; NULL for its own (its
 * default form) when none holds. */
const KhdaysCharacter *khdays_characters_choose(const KhdaysCharacter *c, const KhdaysFormFacts *facts);

/* Character `c`'s form `id` ("default" or NULL: `c` itself); NULL when it has none. */
const KhdaysCharacter *khdays_characters_form(const KhdaysCharacter *c, const char *id);

/* A form's id for the logs and the plugins: "default" for a character. */
const char *khdays_characters_form_id(const KhdaysCharacter *form);

/* The action of the base's animation `game`; NULL when the character gives it no clip. */
const KhdaysCharacterAction *khdays_characters_action(const KhdaysCharacter *c, const char *game);

/* The character's model for the base's weapon model `game` (`ro_w13600`); NULL when it has none. */
const KhdaysCharacterWeapon *khdays_characters_weapon_model(const KhdaysCharacter *c, const char *game);

/* What the character makes of the base's sound `game`: its own line for it, else the longest of its lines ending
 * in '*' that begins it; NULL when it has none (the game's sound plays). */
const KhdaysCharacterSound *khdays_characters_sound(const KhdaysCharacter *c, const char *game);

/* The character's name, or its weapon's, in language `l` (0-4): its own, else the one without a language. */
const char *khdays_characters_name(const KhdaysCharacter *c, int l);
const char *khdays_characters_weapon(const KhdaysCharacter *c, int l);

/* Takes character `index` out of the registry (before numbering). */
void khdays_characters_drop(KhdaysCharacters *r, int index);

void khdays_characters_free(KhdaysCharacters *r);

#ifdef __cplusplus
}
#endif

#endif
