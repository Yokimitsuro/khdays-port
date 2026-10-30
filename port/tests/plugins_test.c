/* The mods' native plugins (runtime/plugins): real libraries -- the SDK's Hello World and the test plugins
 * (tests/plugins/test_plugins.c, core_plugins.c, character_plugins.c) -- put in made-up mods, found, checked,
 * loaded, started, using the universal core (a service, events, hooks, the engine by name) and the actors and
 * characters (a made-up seat, character and enemies), stopped on the game's thread when another thread asks,
 * released. One mode per run (the manager keeps its plugins for the process):
 *   off   native code off: nothing loaded
 *   safe  native code on, safe mode: nothing loaded
 *   on    native code on: each case's state, the order of starts and stops, the stop asked from another thread */
#include "../runtime/characters/characters.h"
#include "../runtime/characters/forms.h"
#include "../runtime/plugins/actors.h"
#include "../runtime/plugins/interfaces.h"
#include "../runtime/plugins/plugins.h"
#include "../runtime/plugins/shown.h"
#include "../runtime/scripts/enemies_link.h"
#include "../runtime/scripts/game_link.h"

#include <kh/kh_actors.h>
#include <kh/kh_characters.h>
#include <kh/kh_combat.h>
#include <kh/kh_input.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the dispatcher's entries the game's hook points call (runtime/plugins/interfaces.c) */
int khdays_hook_on(int point);
int khdays_hook_before(int point, unsigned long long *args, unsigned long long *result);
void khdays_hook_after(int point, unsigned long long *args, unsigned long long *result);

/* The engine's side for kh.engine (runtime/plugins/engine.c): a made-up memory, one routine, one datum. */
typedef struct {
    const char *name;
    void (*native)(void);
    int module;
} KhdaysRoutineName;
typedef struct {
    const char *name;
    unsigned reference;
} KhdaysDatumName;
static int test_routine(void)
{
    return 42;
}
const KhdaysRoutineName khdays_routine_names[] = {{"TestRoutine", (void (*)(void))test_routine, 22}};
const unsigned khdays_routine_name_count = 1;
const KhdaysDatumName khdays_datum_names[] = {{"gTestDatum", 0x02000010u}};
const unsigned khdays_datum_name_count = 1;
static int fake_ram[16];
unsigned char *khdays_window;
int khdays_overlay_loaded(int id)
{
    return id == 22;
}
static unsigned vblank;
unsigned khdays_events_vblank_count(void)
{
    return vblank;
}

/* The game's side for kh.actors and kh.characters (runtime/plugins/actors.c, characters.c): seat 0 plays the
 * made-up character test.fighter/block on roxas's code; two enemies stand. */
#define PLAYER 0x02000020u
#define ENEMY_1 0x02000030u
#define ENEMY_2 0x02000034u
static unsigned seat_actor[4] = {PLAYER};
#define MADE 0x02000038u
static unsigned standing[3] = {ENEMY_1, ENEMY_2};
static int standing_count = 2;
static int player_hp = 30;
unsigned khdays_link_seat_actor(int seat)
{
    return seat >= 0 && seat < 4 ? seat_actor[seat] : 0;
}
int khdays_link_local_seat(void)
{
    return 0;
}
void khdays_link_actor_position(unsigned actor, float at[3])
{
    (void)actor;
    at[0] = 1.5f;
    at[1] = 0.0f;
    at[2] = -2.0f;
}
int khdays_link_actor_kind(unsigned actor)
{
    (void)actor;
    return 0; /* roxas */
}
const char *khdays_link_place_seat(int seat, const float at[3])
{
    (void)seat;
    (void)at;
    return "not now";
}
const char *khdays_link_set_actor_hp(unsigned actor, int hp)
{
    (void)actor;
    player_hp = hp;
    return NULL;
}
void khdays_link_entity_hp(const void *entity, int *hp, int *max)
{
    (void)entity;
    *hp = player_hp;
    *max = 40;
}
int khdays_link_language(void)
{
    return 5; /* Spanish */
}
static int guest, in_room;
int khdays_link_guest(void)
{
    return guest;
}
int khdays_net_in_session(void)
{
    return in_room;
}
/* the last message the mods shared (the host's, to every guest) */
static unsigned char shared[256];
static unsigned shared_size;
int khdays_net_share(const void *data, unsigned int size)
{
    shared_size = size < sizeof(shared) ? size : 0;
    memcpy(shared, data, shared_size);
    return 1;
}
/* a plugin built into the test as KHDAYS_STATIC_PLUGINS builds one into the port (tests/plugins/static_plugin.c) */
typedef struct KhdaysStaticPlugin {
    const char *mod_id;
    KhPluginQuery query;
} KhdaysStaticPlugin;
const KhPluginDescriptor *khdays_test_static_query(void);
const KhdaysStaticPlugin khdays_static_plugins[] = {{"test.static", khdays_test_static_query}};
const int khdays_static_plugin_count = 1;
int khdays_link_strength_damage(unsigned actor, int scale)
{
    return actor == PLAYER ? scale * 10 / 4096 : -1; /* strength 10 */
}
unsigned khdays_input_game_frames(void)
{
    return vblank;
}
/* the last hit dealt and enemy moved (kh.combat, kh.actors) */
static struct {
    unsigned enemy, attacker;
    int seat, damage, chance, no_flinch, count;
    unsigned kinds;
} dealt;
static unsigned moved;
int khdays_enemies_hit(unsigned enemy, unsigned attacker, int seat, int damage, const float at[3], unsigned kinds,
                       int chance, int no_flinch)
{
    (void)at;
    dealt.enemy = enemy;
    dealt.attacker = attacker;
    dealt.seat = seat;
    dealt.damage = damage;
    dealt.kinds = kinds;
    dealt.chance = chance;
    dealt.no_flinch = no_flinch;
    ++dealt.count;
    return 1;
}
int khdays_enemies_move(unsigned enemy, const float at[3])
{
    (void)at;
    moved = enemy;
    return 1;
}
unsigned khdays_enemies_make(int kind, const float at[3], int level, double hp, double strength, const char **why)
{
    (void)at;
    (void)level;
    (void)hp;
    (void)strength;
    *why = "test: only kind 3";
    if (kind != 3) {
        return 0;
    }
    standing[standing_count++] = MADE;
    return MADE;
}
/* the clip a plugin plays (kh.characters' play): character 0x16 has "spin" */
static int played_number;
int khdays_characters_play(int number, const char *action, int loop, unsigned now)
{
    (void)loop;
    (void)now;
    if (number != 0x16 || strcmp(action, "spin") != 0) {
        return 0;
    }
    played_number = number;
    return 1;
}
void khdays_characters_play_none(int number)
{
    if (number == played_number) {
        played_number = 0;
    }
}
int khdays_enemies_list(unsigned *out, int room)
{
    for (int i = 0; i < standing_count && out != NULL && i < room; ++i) {
        out[i] = standing[i];
    }
    return standing_count;
}
int khdays_enemies_state(unsigned actor, KhdaysEnemyInfo *out)
{
    for (int i = 0; i < standing_count; ++i) {
        if (standing[i] == actor) {
            if (out != NULL) {
                memset(out, 0, sizeof(*out));
                out->kind = 3;
                out->hp = 50;
                out->max_hp = 75;
            }
            return 1;
        }
    }
    return 0;
}
static char fighter_identity[] = "test.fighter/block", fighter_name[] = "Bloque";
static KhdaysCharacter fighter = {
    .identity = fighter_identity, .key = fighter_identity + 13, .name[4] = fighter_name, .number = 0x16};
int khdays_characters_of(int slot, int kind)
{
    return slot == 0 && kind == 0 ? 0x16 : kind;
}
int khdays_characters_count(void)
{
    return 1;
}
const KhdaysCharacter *khdays_characters_at(int index)
{
    return index == 0 ? &fighter : NULL;
}
const KhdaysCharacter *khdays_characters_numbered(int number)
{
    return number == 0x16 ? &fighter : NULL;
}
/* its forms (runtime/characters/forms.c): its own and "dual"; the conditions the plugins add, seat 0's forced form */
static char dual_id[] = "dual";
static KhdaysCharacter fighter_dual = {.identity = fighter_identity, .form = dual_id, .number = 0x100, .base = 19};
static struct {
    char name[64];
    KhdaysFormCondition test;
    void *user;
    const void *owner;
} condition;
static const KhdaysCharacter *forced_form;
static KhdaysFormChanged form_listener;
const KhdaysCharacter *khdays_characters_seat_character(int seat)
{
    return seat == 0 ? &fighter : NULL;
}
const char *khdays_characters_form_id(const KhdaysCharacter *form)
{
    return form->form != NULL ? form->form : "default";
}
const KhdaysCharacter *khdays_characters_form(const KhdaysCharacter *c, const char *id)
{
    return id == NULL || strcmp(id, "default") == 0 ? c : strcmp(id, "dual") == 0 ? &fighter_dual : NULL;
}
int khdays_characters_add_condition(const char *name, KhdaysFormCondition test, void *user, const void *owner)
{
    if (condition.test != NULL && strcmp(condition.name, name) == 0) {
        return 0;
    }
    snprintf(condition.name, sizeof(condition.name), "%s", name);
    condition.test = test;
    condition.user = user;
    condition.owner = owner;
    return 1;
}
void khdays_characters_remove_conditions(const void *owner)
{
    if (condition.owner == owner) {
        memset(&condition, 0, sizeof(condition));
    }
}
const KhdaysCharacter *khdays_characters_seat_form_now(int seat)
{
    return seat != 0 ? NULL : forced_form != NULL ? forced_form : &fighter;
}
void khdays_characters_force_form(int seat, const KhdaysCharacter *form)
{
    if (seat == 0) {
        forced_form = form;
    }
}
void khdays_characters_on_form(KhdaysFormChanged changed)
{
    form_listener = changed;
}
const char *khdays_character_base_name(int number)
{
    static const char *const names[] = {"roxas",   "axel",    "xigbar", "saix",     "xaldin", "sora",  "demyx",
                                        "larxene", "lexaeus", "luxord", "marluxia", "riku",   "vexen", "xemnas",
                                        "xion",    "zexion",  "mickey", "donald",   "goofy"};
    return number >= 0 && number < 19 ? names[number] : NULL;
}

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                                                 \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* every line said, "<mod id>: <text>\n" one after another */
static char said[16384];

static void heard(const char *mod_id, const char *text)
{
    const size_t n = strlen(said);
    snprintf(said + n, sizeof(said) - n, "%s: %s\n", mod_id, text);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb"), *out = fopen(to, "wb");
    char block[65536];
    size_t n;
    while (in != NULL && out != NULL && (n = fread(block, 1, sizeof(block), in)) > 0) {
        fwrite(block, 1, n, out);
    }
    if (in != NULL) {
        fclose(in);
    }
    if (out != NULL) {
        fclose(out);
    }
}

/* A made-up mod: its folder, its mod.ini (`ini`, with %s for the platform's key) and its library (NULL none). */
static void mod(const char *folder, const char *ini, const char *library)
{
    char path[512], text[1024];
    snprintf(path, sizeof(path), "plugins_test_mods/%s", folder);
    SDL_CreateDirectory(path);
    snprintf(path, sizeof(path), "plugins_test_mods/%s/native", folder);
    SDL_CreateDirectory(path);
    snprintf(text, sizeof(text), ini, khdays_plugins_platform());
    snprintf(path, sizeof(path), "plugins_test_mods/%s/mod.ini", folder);
    write_file(path, text);
    if (library != NULL) {
        snprintf(path, sizeof(path), "plugins_test_mods/%s/native/plugin.dll", folder);
        copy_file(library, path);
    }
}

static const KhdaysPluginView *by_id(const char *id)
{
    for (int i = 0; i < khdays_plugins_count(); ++i) {
        if (strcmp(khdays_plugins_at(i)->mod_id, id) == 0) {
            return khdays_plugins_at(i);
        }
    }
    return NULL;
}

static int state(const char *id)
{
    const KhdaysPluginView *v = by_id(id);
    return v != NULL ? v->state : -1;
}

static int why_has(const char *id, const char *part)
{
    const KhdaysPluginView *v = by_id(id);
    return v != NULL && strstr(v->why, part) != NULL;
}

static void make_mods(void)
{
    SDL_CreateDirectory("plugins_test_mods");
    remove("plugin_stopped.txt");
    mod("A hello",
        "id = example.hello-world\nname = Hello\nversion = 1.0\nauthor = Tester\nsdk = 1\n"
        "native.%s = native/plugin.dll\n",
        KHDAYS_TEST_HELLO);
    mod("B other sdk", "id = test.other-sdk\nname = B\nversion = 1\nsdk = 1\nnative.%s = native/plugin.dll\n",
        KHDAYS_TEST_OTHER_SDK);
    mod("C failing", "id = test.failing\nname = C\nversion = 1\nsdk = 1\nnative.%s = native/plugin.dll\n",
        KHDAYS_TEST_FAILING);
    mod("D counting",
        "id = test.counting\nname = D\nversion = 1\nsdk = 1\nnative.%s = native/plugin.dll\n"
        "requires = example.hello-world, test.data-only\n",
        KHDAYS_TEST_COUNTING);
    mod("E missing", "id = test.missing\nname = E\nversion = 1\nsdk = 1\nnative.%s = native/none.dll\n", NULL);
    mod("F sdk 9", "id = test.sdk9\nname = F\nversion = 1\nsdk = 9\nnative.%s = native/plugin.dll\n",
        KHDAYS_TEST_HELLO);
    mod("G requires",
        "id = test.requires\nname = G\nversion = 1\nsdk = 1\nnative.%s = native/plugin.dll\n"
        "requires = nobody.here\n",
        KHDAYS_TEST_HELLO);
    mod("H outside", "id = test.outside\nname = H\nversion = 1\nsdk = 1\nnative.%s = ../A hello/native/plugin.dll\n",
        NULL);
    mod("I data only", "id = test.data-only\nname = I\nversion = 1\n# a mod%s without a plugin\n", NULL);
    mod("J provider", "id = test.provider\nname = J\nversion = 1\nsdk = 1\nnative.%s = native/plugin.dll\n",
        KHDAYS_TEST_PROVIDER);
    mod("K user",
        "id = test.user\nname = K\nversion = 1\nsdk = 1\nnative.%s = native/plugin.dll\nrequires = test.provider\n",
        KHDAYS_TEST_USER);
    mod("L fighter",
        "id = test.fighter\nname = L\nversion = 1\nsdk = 1\nmultiplayer = yes\nnative.%s = native/plugin.dll\n",
        KHDAYS_TEST_FIGHTER);
    /* no library for any platform: built in */
    mod("N static", "id = test.static\nname = N\nversion = 2\nsdk = 1\n# no library for %s\n", NULL);
    /* its parameters: described (one bad part, one bad default), then some chosen (one out of bounds, one unknown) */
    write_file("plugins_test_mods/L fighter/parameters.ini",
               "[spins]\ntype = integer\ndefault = 8\nmin = 1\nmax = 30\nlabel = Spins\nlabel es = Giros\n"
               "[damage]\ntype = number\ndefault = 1.25\n"
               "[loud]\ntype = flag\ndefault = yes\n"
               "[sound]\ntype = choice\nchoices = none, soft, loud\ndefault = soft\n"
               "[name]\ntype = text\ndefault = Spinner\n"
               "[broken]\ndefault = 3\n"
               "[far]\ntype = integer\nmin = 0\nmax = 5\ndefault = 9\n");
    write_file("plugins_test_mods/L fighter/parameters.values.ini",
               "spins = 12\ndamage = 2.5\nloud = no\nsound = loud\nname = Peonza\nfar = 99\nwhat = 1\n");
}

static int count_of(const char *text)
{
    int n = 0;
    for (const char *at = strstr(said, text); at != NULL; at = strstr(at + 1, text)) {
        ++n;
    }
    return n;
}

/* kh.input, kh.combat, the enemies made and moved, a clip played: through the tables, for a made-up plugin. */
static void check_input_and_combat(void)
{
    static int made_up;
    KhPlugin *me = (KhPlugin *)&made_up; /* a plugin's handle is opaque: these calls only keep it */
    const KhInput *in = khdays_plugins_input();
    const KhCombat *c = khdays_plugins_combat();
    const KhActors *a = khdays_plugins_actors();
    const KhCharacters *ch = khdays_plugins_characters();
    const float at[3] = {1, 0, 0};
    KhHit hit = {sizeof(KhHit), 40, {1, 0, 0}, KH_STATUS_BURN, 30, 0};
    KhActor player, enemy, made;

    /* the buttons: read, hidden from the game while the plugin reads them */
    CHECK(khdays_plugins_buttons(KH_BUTTON_A | KH_BUTTON_UP) == (KH_BUTTON_A | KH_BUTTON_UP));
    CHECK(in->held() == (KH_BUTTON_A | KH_BUTTON_UP) && in->pressed() == (KH_BUTTON_A | KH_BUTTON_UP));
    in->hide(me, KH_BUTTON_A);
    CHECK(khdays_plugins_buttons(KH_BUTTON_A) == 0 && in->held() == KH_BUTTON_A && in->pressed() == 0);
    CHECK(in->released() == KH_BUTTON_UP && khdays_plugins_hidden_buttons() == KH_BUTTON_A);
    in->hide(me, 0);
    CHECK(khdays_plugins_buttons(KH_BUTTON_A) == KH_BUTTON_A);
    in->hide(me, KH_BUTTONS_DIRECTIONS);
    khdays_plugins_input_forget(me); /* as at its stop */
    CHECK(khdays_plugins_hidden_buttons() == 0);

    /* hits: worked out and dealt through the game's side, a guest's refused */
    seat_actor[0] = PLAYER;
    player = a->player(0);
    enemy = a->of(ENEMY_1);
    CHECK(c->damage(player, 1.5f) == 15 && c->damage(enemy, 1.0f) == -1);
    CHECK(c->hit(enemy, player, &hit) == KH_OK && dealt.enemy == ENEMY_1 && dealt.attacker == PLAYER);
    CHECK(dealt.seat == 0 && dealt.damage == 40 && dealt.kinds == 0x80 && dealt.chance == 30 && !dealt.no_flinch);
    CHECK(c->hit(player, player, &hit) == KH_ERROR && c->hit(enemy, enemy, &hit) == KH_ERROR);
    dealt.count = 0;
    hit.flags = KH_HIT_NO_FLINCH;
    CHECK(c->hit_around(player, at, 2.0f, &hit) == 2 && dealt.count == 2 && dealt.no_flinch); /* both at 0 0 0 */
    CHECK(c->hit_around(player, (const float[3]){9, 0, 0}, 2.0f, &hit) == 0);
    guest = 1;
    CHECK(c->hit(enemy, player, &hit) == KH_UNSUPPORTED && c->hit_around(player, at, 2.0f, &hit) == 0);
    guest = 0;

    /* enemies moved and made */
    CHECK(a->set_position(enemy, at) == KH_OK && moved == ENEMY_1);
    made = a->make_enemy(me, 3, at, 1, 2.0f, 1.0f);
    CHECK(made != 0 && a->kind(made) == KH_ACTOR_ENEMY && a->ref(made) == MADE && a->enemies(NULL, 0) == 3);
    standing_count = 2;

    /* a clip played on the character, refused on an enemy, stopped with its plugin */
    CHECK(ch->play(me, player, "spin", KH_PLAY_LOOP) == KH_OK && played_number == 0x16);
    CHECK(ch->play(me, player, "none", 0) == KH_UNSUPPORTED && ch->play(me, enemy, "spin", 0) == KH_UNSUPPORTED);
    khdays_plugins_characters_forget(me);
    CHECK(played_number == 0);
    CHECK(ch->play(me, player, "spin", 0) == KH_OK);
    ch->stop_playing(me, player);
    CHECK(played_number == 0);
}

/* In a room: what a plugin not made for rooms may change, test.fighter's (made for rooms) messages, the plugins in
 * the rooms' fingerprint; then a guest's copy. */
static void check_rooms(void)
{
    unsigned long long args[2] = {99, 1}, result = 5;
    unsigned char digest[32];
    const char *name, *version;
    int listed = 0, fighter_listed = 0;
    in_room = 1;
    /* test.provider and test.user hook TestPoint, neither made for rooms: their hooks are not called */
    CHECK(!khdays_hook_before(0, args, &result) && args[0] == 99 && args[1] == 1);
    khdays_hook_after(0, args, &result);
    CHECK(result == 5);
    /* test.fighter is: its attack heals, its message goes to the guests and is heard here as there */
    seat_actor[0] = PLAYER;
    vblank += 100;
    khdays_plugins_frame();
    khdays_plugins_character_attack(PLAYER, 0, 9);
    CHECK(count_of("plugin attack 9, healed: yes") == 1 && count_of("plugin sent struck: 0") == 1);
    CHECK(count_of("plugin heard struck v3: 9") == 1 && shared_size > 0);
    khdays_plugins_shared(shared, shared_size); /* as a guest's copy gets it */
    CHECK(count_of("plugin heard struck v3: 9") == 2);
    khdays_plugins_shared((const unsigned char *)"test.lua\0x", 10); /* a Lua mod's: not a plugin's */
    CHECK(count_of("plugin heard struck") == 2);
    for (int i = 0; khdays_plugins_room_entry(i, digest, &name, &version); ++i) {
        ++listed;
        fighter_listed += strcmp(name, "L") == 0 && strcmp(version, "1") == 0;
    }
    CHECK(listed == 6 && fighter_listed == 1); /* the started ones: hello, counting, provider, user, fighter, static */
    /* a guest's copy sends nothing */
    guest = 1;
    vblank += 100;
    khdays_plugins_frame();
    khdays_plugins_character_attack(PLAYER, 0, 10);
    CHECK(count_of("plugin sent struck: -2") == 1);
    guest = 0;
    in_room = 0;
}

/* kh.actors and kh.characters through test.fighter (its lines) and the table itself. */
static void check_actors_and_characters(void)
{
    const KhActors *a = khdays_plugins_actors();
    const float origin[3] = {0, 0, 0};
    KhActor player, enemy, again;
    CHECK(state("test.fighter") == KHDAYS_PLUGIN_STARTED);
    CHECK(strstr(said, "test.fighter: plugin block 22, roxas 0, by identity 22, nobody -1; 20 characters, the last "
                       "test.fighter/block (Bloque) on 0") != NULL);
    CHECK(strstr(said, "test.fighter: not listening: no character 999") != NULL);
    CHECK(strstr(said, "test.fighter: plugin 999 refused") != NULL);
    /* its parameters: the values chosen, else the defaults; what was wrong told */
    CHECK(strstr(said, "test.fighter: plugin parameters: spins 12, damage 2.50, loud 0, sound loud (2), name Peonza, "
                       "missing -7") != NULL);
    CHECK(strstr(said, "test.fighter: parameters: line 21: [broken]: its type is missing or unknown") != NULL);
    CHECK(strstr(said, "test.fighter: parameters: line 23: [far]: its default: outside its bounds") != NULL);
    CHECK(strstr(said, "test.fighter: parameters: line 6: far: outside its bounds") != NULL);
    CHECK(strstr(said, "test.fighter: parameters: line 7: what: no such parameter") != NULL);
    CHECK(strstr(said, "test.fighter: parameters: 6, the values chosen read") != NULL);

    /* the seat's actor enters, is called each frame, attacks */
    vblank = 100;
    khdays_plugins_frame();
    CHECK(strstr(said, "test.fighter: plugin entered seat 0 as 22 (seat 0, kind 1), HP 30/40 at 1.5 0.0 -2.0") != NULL);
    CHECK(strstr(said, "test.fighter: plugin enemies 2, the first of kind 3, HP 50/75") != NULL);
    khdays_plugins_character_attack(PLAYER, 0, 5);
    vblank = 101;
    khdays_plugins_character_attack(PLAYER, 0, 5); /* the same attack, a frame on */
    khdays_plugins_character_attack(PLAYER, 0, 6);
    khdays_plugins_character_attack(ENEMY_1, 0, 7); /* not the seat's actor */
    CHECK(count_of("plugin attack 5, healed: yes") == 1 && count_of("plugin attack 6") == 1 && player_hp == 32);
    CHECK(count_of("plugin attack 7") == 0);
    vblank = 110;
    khdays_plugins_frame(); /* untested for a while: ended */
    khdays_plugins_character_attack(PLAYER, 0, 6);
    CHECK(count_of("plugin attack 6") == 2);

    /* handles: the player's, an enemy's ended and found again, refused once gone */
    player = a->player(0);
    CHECK(player != 0 && a->kind(player) == KH_ACTOR_PLAYER && a->character(player) == 0x16 && a->seat(player) == 0);
    CHECK(a->of(PLAYER) == player && a->ref(player) == PLAYER && a->player(1) == 0 && a->of(0x02000040u) == 0);
    CHECK(a->set_position(player, origin) == KH_UNSUPPORTED);
    enemy = a->of(ENEMY_1);
    CHECK(enemy != 0 && enemy != player && a->kind(enemy) == KH_ACTOR_ENEMY && a->character(enemy) == 3);
    CHECK(a->seat(enemy) == -1 && a->set_hp(enemy, 1) == KH_UNSUPPORTED);
    khdays_actors_ended(ENEMY_1); /* defeated, then set going again */
    CHECK(a->kind(enemy) == KH_ACTOR_NONE && a->ref(enemy) == 0 && a->hp(enemy, NULL, NULL) == KH_ERROR);
    again = a->of(ENEMY_1);
    CHECK(again != 0 && again != enemy && a->kind(again) == KH_ACTOR_ENEMY && a->kind(enemy) == KH_ACTOR_NONE);
    enemy = a->of(ENEMY_2);
    CHECK(enemy != 0);
    standing_count = 1; /* ENEMY_2 sent away: ended at the next frame, before it could stand again */
    khdays_plugins_frame();
    standing_count = 2;
    CHECK(a->kind(enemy) == KH_ACTOR_NONE && a->of(ENEMY_2) != enemy);
    khdays_actors_area_left();
    CHECK(a->kind(player) == KH_ACTOR_NONE && a->kind(again) == KH_ACTOR_NONE);
    khdays_plugins_frame(); /* the same body, a new handle: it enters again */
    CHECK(count_of("plugin entered seat 0") == 2 && count_of("plugin left after") == 1);

    /* the seat emptied: the character's actor leaves */
    seat_actor[0] = 0;
    khdays_plugins_frame();
    CHECK(count_of("plugin left after") == 2 && count_of("frames, its handle refused") == 2);

    /* forms: its condition named by its mod's id and asked as its own, a name taken or bad refused; seat 0's form
     * read and forced; a form taken told to it */
    CHECK(strstr(said, "test.fighter: plugin condition tired added, again refused, bad name refused") != NULL);
    CHECK(strcmp(condition.name, "test.fighter.tired") == 0 && condition.test != NULL);
    CHECK(condition.test != NULL && condition.test(0, 0x16, condition.user) == 1 &&
          condition.test(1, 0x16, condition.user) == 0);
    CHECK(strstr(said, "test.fighter: plugin seat 0 plays default; set dual 0, nothing -1, seat 2 -2") != NULL);
    CHECK(forced_form == &fighter_dual);
    CHECK(form_listener != NULL);
    if (form_listener != NULL) {
        form_listener(0, &fighter_dual);
    }
    CHECK(strstr(said, "test.fighter: plugin seat 0 takes form dual") != NULL);
}

static int SDLCALL quit_thread(void *unused)
{
    (void)unused;
    khdays_plugins_quit(5000);
    return 0;
}

/* What the settings menu's Mods page is shown: the plugins' own views, as they are now (shown.h). */
static void check_shown(int safe_mode)
{
    KhdaysPluginsShown shown;
    memset(&shown, 0, sizeof(shown));
    CHECK(khdays_plugins_shown(&shown) && shown.count == khdays_plugins_count() && shown.safe_mode == safe_mode);
    for (int i = 0; i < shown.count; ++i) {
        const KhdaysPluginView *v = khdays_plugins_at(i);
        CHECK(shown.plugins[i].state == v->state && strcmp(shown.plugins[i].name, v->name) == 0 &&
              strcmp(shown.plugins[i].author, v->author) == 0 && strcmp(shown.plugins[i].why, v->why) == 0);
    }
    CHECK(!khdays_plugins_shown(&shown)); /* nothing newer */
    khdays_plugins_shown_free(&shown);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "on";
    fake_ram[4] = 1234;                                      /* gTestDatum, at 0x02000010 */
    khdays_window = (unsigned char *)fake_ram - 0x02000000u; /* references from main RAM's start */
    make_mods();
    khdays_plugins_set_native_code(strcmp(mode, "off") != 0);
    khdays_plugins_set_safe_mode(strcmp(mode, "safe") == 0);
    khdays_plugins_start("plugins_test_mods", heard);
    CHECK(khdays_plugins_count() == 12); /* I has no plugin */
    check_shown(strcmp(mode, "safe") == 0);
    if (strcmp(mode, "on") != 0) {
        CHECK(state("example.hello-world") == KHDAYS_PLUGIN_WAITING);
        CHECK(state("test.counting") == KHDAYS_PLUGIN_WAITING);
        CHECK(strstr(said, strcmp(mode, "safe") == 0 ? "safe mode" : "native code is off") != NULL);
        CHECK(strstr(said, "Hello from") == NULL);
        CHECK(state("test.sdk9") == KHDAYS_PLUGIN_REFUSED); /* told before any loading */
    } else {
        CHECK(state("example.hello-world") == KHDAYS_PLUGIN_STARTED);
        CHECK(strstr(said, "example.hello-world: plugin Hello from a native plugin") != NULL);
        CHECK(strstr(said, "by Tester") != NULL);
        CHECK(state("test.other-sdk") == KHDAYS_PLUGIN_REFUSED && why_has("test.other-sdk", "built for SDK 2.0"));
        CHECK(state("test.failing") == KHDAYS_PLUGIN_FAILED && why_has("test.failing", "start failed"));
        CHECK(strstr(said, "test.failing: plugin error: start fails on purpose") != NULL);
        CHECK(state("test.counting") == KHDAYS_PLUGIN_STARTED); /* after the mod it requires */
        CHECK(strstr(said, "Hello from") < strstr(said, "test.counting: plugin counting"));
        CHECK(state("test.missing") == KHDAYS_PLUGIN_REFUSED && why_has("test.missing", "is not there"));
        CHECK(state("test.sdk9") == KHDAYS_PLUGIN_REFUSED && why_has("test.sdk9", "built for SDK 9"));
        CHECK(state("test.requires") == KHDAYS_PLUGIN_REFUSED && why_has("test.requires", "nobody.here"));
        CHECK(state("test.outside") == KHDAYS_PLUGIN_REFUSED && why_has("test.outside", "not a file inside"));

        /* the universal core, between test.provider and test.user */
        CHECK(state("test.provider") == KHDAYS_PLUGIN_STARTED && state("test.user") == KHDAYS_PLUGIN_STARTED);
        CHECK(strstr(said, "test.provider: plugin no such point refused") != NULL);
        CHECK(strstr(said, "test.provider: hook not added: no hook point NoSuchPoint") != NULL);
        CHECK(strstr(said, "test.user: plugin math 5") != NULL);
        CHECK(strstr(said, "test.user: plugin no math 2") != NULL);
        CHECK(strstr(said, "test.provider: plugin test.user.ping hello") != NULL);
        CHECK(strstr(said, "test.user: plugin frame refused") != NULL);
        CHECK(strstr(said, "replaces it as test.provider's does") != NULL);
        CHECK(strstr(said, "routine 42 in module 22, datum 1234, none 0000000000000000, back same") != NULL);
        khdays_plugins_game_event("frame", 7, 0, 0, 0, NULL);
        CHECK(strstr(said, "test.provider: plugin frame 7") != NULL);
        {
            /* as a hooked routine's wrapper does: TestPoint(a, b) = a + b */
            unsigned long long args[2] = {5, 10}, result = 0;
            CHECK(khdays_hook_on(0));
            CHECK(!khdays_hook_before(0, args, &result) && args[1] == 20); /* doubled by the user, first */
            result = args[0] + args[1];
            khdays_hook_after(0, args, &result);
            CHECK(result == 26); /* the provider's after-function */
            args[0] = 99;
            args[1] = 1;
            result = 0;
            CHECK(khdays_hook_before(0, args, &result) && result == 7 && args[1] == 2); /* skipped by the provider */
            khdays_hook_after(0, args, &result);
            CHECK(result == 8);
        }

        check_actors_and_characters();
        check_input_and_combat();
        check_rooms();
        CHECK(state("test.static") == KHDAYS_PLUGIN_STARTED);
        CHECK(strstr(said, "test.static: plugin built in (built into the port, by no one named) starts") != NULL);
        CHECK(strstr(said, "test.static: plugin built in, started") != NULL);

        /* the window's thread asks; this, the game's, stops them at its next frame */
        SDL_Thread *thread = SDL_CreateThread(quit_thread, "quit", NULL);
        for (int frame = 0; frame < 500 && state("test.counting") == KHDAYS_PLUGIN_STARTED; ++frame) {
            SDL_Delay(10);
            khdays_plugins_frame();
        }
        SDL_WaitThread(thread, NULL);
        CHECK(state("example.hello-world") == KHDAYS_PLUGIN_STOPPED);
        CHECK(state("test.counting") == KHDAYS_PLUGIN_STOPPED);
        /* the last started first */
        CHECK(strstr(said, "test.counting: plugin stopping") != NULL &&
              strstr(said, "test.counting: plugin stopping") < strstr(said, "Goodbye"));
        {
            FILE *f = fopen("plugin_stopped.txt", "rb");
            char text[32] = "";
            if (f != NULL) {
                fgets(text, sizeof(text), f);
                fclose(f);
            }
            CHECK(strcmp(text, "stopped once") == 0);
        }
        CHECK(strstr(said, "test.static: plugin built in, stopped") != NULL);
        CHECK(!khdays_hook_on(0) && !khdays_plugins_listening()); /* their hooks and subscriptions gone */
        khdays_plugins_stop();                                    /* again: nothing left to stop */
        check_shown(0);
        CHECK(strstr(strstr(said, "Goodbye") + 1, "Goodbye") == NULL);
    }
    if (failures != 0) {
        fprintf(stderr, "what was said:\n%s", said);
    } else {
        printf("plugins (%s): all checks passed\n", mode);
    }
    return failures != 0;
}
