/* Gameplay mods' scripts (runtime/scripts) against a game of the test's own: SHA-256, manifests, the
 * load order, the sandbox, a stopped mod beside a running one, an endless loop paused, the events and
 * the state names, the interface's version 2. Each case writes its mods under a folder of its own,
 * removed at the end. */
#include "../runtime/core/sha256.h"
#include "../runtime/mods/list.h"
#include "../runtime/scripts/api_v2.h"
#include "../runtime/scripts/deck_link.h"
#include "../runtime/scripts/enemies_link.h"
#include "../runtime/scripts/folders.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/scripts/manifest.h"
#include "../runtime/scripts/mod.h"
#include "../runtime/scripts/order.h"
#include "../runtime/scripts/queue.h"
#include "../runtime/scripts/scripts.h"
#include "../runtime/scripts/screens.h"
#include "../runtime/scripts/time_link.h"
#include "../runtime/scripts/view.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* ---- the game, as the test has it ---------------------------------------------------------------- */

static unsigned fake_scene = 1, fake_frame, fake_state = 0x02100000u;
static unsigned char fake_items[KHDAYS_LINK_ITEMS];
static unsigned char fake_flags[KHDAYS_LINK_FLAGS / 8];
static int fake_munny, fake_guest;

void khdays_log(const char *format, ...)
{
    (void)format;
}

void khdays_link_folders(const char **mods, const char **user)
{
    *mods = "unused";
    *user = "unused";
}

unsigned khdays_link_frame(void)
{
    return fake_frame;
}

static int fake_paused;

int khdays_link_paused(void)
{
    return fake_paused;
}

unsigned khdays_link_scene(unsigned *argument)
{
    if (argument != NULL) {
        *argument = 7;
    }
    return fake_scene;
}

unsigned khdays_link_state(void)
{
    return fake_state;
}

int khdays_link_munny(void)
{
    return fake_munny;
}

void khdays_link_set_munny(int value)
{
    fake_munny = value;
}

int khdays_link_items(unsigned char *counts)
{
    if (fake_state == 0) {
        return 0;
    }
    memcpy(counts, fake_items, sizeof(fake_items));
    return 1;
}

int khdays_link_flag(int n)
{
    return (fake_flags[n / 8] >> (n % 8)) & 1;
}

/* The port's autosave hears the mission's end too (runtime/menus/autosave.h): not under test here. */
void khdays_autosave_mission_end(void) {}

void khdays_link_set_flag(int n, int on)
{
    if (on) {
        fake_flags[n / 8] |= (unsigned char)(1 << (n % 8));
    } else {
        fake_flags[n / 8] &= (unsigned char)~(1 << (n % 8));
    }
}

void khdays_link_set_item(int id, int count)
{
    fake_items[id] = (unsigned char)count;
    if (count > 0) {
        khdays_link_set_flag(0x4db + id, 1);
    }
}

int khdays_link_mission(void)
{
    return fake_scene == 2 ? 15 : -1;
}

int khdays_link_position(float at[3])
{
    at[0] = 1.5f;
    at[1] = 0;
    at[2] = -2;
    return fake_scene == 2;
}

static int fake_hp = 40, fake_hp_max = 50;
static int fake_gauge = 1; /* the field's panel set up, its HP gauge there (0 while the field loads) */

static float fake_set_at[3];

const char *khdays_link_set_position(const float at[3])
{
    memcpy(fake_set_at, at, sizeof(fake_set_at));
    return NULL;
}

int khdays_link_player_hp(int *hp, int *max)
{
    *hp = fake_hp;
    *max = fake_hp_max;
    return fake_scene == 2;
}

const char *khdays_link_set_player_hp(int hp)
{
    if (fake_scene != 2) {
        return "no player";
    }
    if (!fake_gauge) {
        return "not now";
    }
    fake_hp = hp > fake_hp_max ? fake_hp_max : hp;
    return NULL;
}

void khdays_link_entity_hp(const void *entity, int *hp, int *max)
{
    const int *e = entity;
    *hp = e[0];
    *max = e[1];
}

int khdays_link_is_player(const void *entity)
{
    return ((const int *)entity)[2];
}

/* The hit being dealt's attacker, the game over, the results' rule. */
static unsigned fake_attacker;
static int fake_attacker_kind = -1, fake_game_over, fake_cleared;

void khdays_link_hit_from(void *source)
{
    (void)source;
}

int khdays_link_attacker(unsigned *actor)
{
    *actor = fake_attacker;
    return fake_attacker_kind;
}

int khdays_link_game_over(void)
{
    return fake_game_over;
}

int khdays_link_results_cleared(void)
{
    return fake_cleared;
}

static int fake_left, fake_started, fake_title_idle;

int khdays_link_language(void)
{
    return 5; /* Spanish */
}

int khdays_link_title_idle(void)
{
    return fake_title_idle;
}

const char *khdays_link_start_mission(int number)
{
    if (fake_scene != 19) {
        return "not in a camp";
    }
    if (number != 10 && number != 2) {
        return "unknown mission";
    }
    fake_started = number;
    return NULL;
}

const char *khdays_link_leave_mission(void)
{
    if (fake_scene != 2) {
        return "not in the field";
    }
    ++fake_left;
    return NULL;
}

static char fake_sound[64], fake_notice[64];
static unsigned int fake_mods_fingerprint, fake_mods_count;
static unsigned char fake_mods_list[256];

void khdays_net_set_mods(unsigned int fingerprint, const unsigned char *list, unsigned int size, unsigned int count)
{
    fake_mods_fingerprint = fingerprint;
    fake_mods_count = count;
    memset(fake_mods_list, 0, sizeof(fake_mods_list));
    memcpy(fake_mods_list, list, size < sizeof(fake_mods_list) ? size : sizeof(fake_mods_list));
}

/* no native plugins (runtime/plugins/plugins.h) */
int khdays_plugins_listening(void)
{
    return 0;
}
int khdays_plugins_room_entry(int index, unsigned char digest[32], const char **name, const char **version)
{
    (void)index;
    (void)digest;
    (void)name;
    (void)version;
    return 0; /* no native plugin in these tests */
}
void khdays_plugins_shared(const unsigned char *data, unsigned size)
{
    (void)data;
    (void)size;
}
void khdays_plugins_game_event(const char *name, long long a, long long b, long long c, long long d, const char *text)
{
    (void)name;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)text;
}

/* the mods' characters (runtime/characters/characters.h): `fake_characters` of them, each part filled with
 * `fake_character_part` */
static int fake_characters;
static unsigned char fake_character_part = 1;

int khdays_characters_room_entry(int index, unsigned char digest[32], const char **name, const char **identity)
{
    if (index >= fake_characters) {
        return 0;
    }
    memset(digest, fake_character_part, 32);
    *name = "Wind";
    *identity = "test.wind/wind";
    return 1;
}

const char *khdays_link_play_sound(const char *sequence_archive, const char *entry)
{
    if (strcmp(sequence_archive, "COMMON_SYS") != 0) {
        return "unknown sound";
    }
    snprintf(fake_sound, sizeof(fake_sound), "%s/%s", sequence_archive, entry);
    return NULL;
}

const char *khdays_link_show_text(const unsigned short *text, int length)
{
    if (fake_scene != 2) {
        return "not in the field";
    }
    if (fake_notice[0] != 0) {
        return "busy";
    }
    for (int i = 0; i < length && i < (int)sizeof(fake_notice) - 1; ++i) {
        fake_notice[i] = text[i] < 0x80 ? (char)text[i] : '?';
    }
    return NULL;
}

int khdays_link_menu_open(int menu)
{
    (void)menu;
    return 0;
}

int khdays_link_guest(void)
{
    return fake_guest;
}

/* The game's memory: 64 KB of main RAM, nothing else. */
#define FAKE_RAM 0x02000000u
static unsigned char fake_ram[0x10000];

int khdays_link_reachable(unsigned address, unsigned long long size)
{
    return address >= FAKE_RAM && address + size <= FAKE_RAM + sizeof(fake_ram);
}

int khdays_link_read(unsigned address, void *out, size_t size)
{
    if (!khdays_link_reachable(address, size)) {
        return 0;
    }
    memcpy(out, fake_ram + (address - FAKE_RAM), size);
    return 1;
}

int khdays_link_write(unsigned address, const void *in, size_t size)
{
    if (!khdays_link_reachable(address, size)) {
        return 0;
    }
    memcpy(fake_ram + (address - FAKE_RAM), in, size);
    return 1;
}

int khdays_link_overlay_loaded(int id)
{
    return id == 22; /* the battle's */
}

int khdays_link_where(void)
{
    return fake_scene == 2    ? KHDAYS_WHERE_FIELD
           : fake_scene == 19 ? KHDAYS_WHERE_CAMP
           : fake_scene == 1  ? KHDAYS_WHERE_TITLE
                              : KHDAYS_WHERE_ELSEWHERE;
}

/* Time, music, the seed, sharing: VBlanks the test counts; tracks 2 TwilightR_F and 8 Herc_B; a fixed
 * seed; the bytes shared last, and shares waiting for a guest. */
static unsigned fake_vblanks;
static int fake_music = -1, fake_music_holding;
static unsigned long long fake_seed = 12345;
static unsigned char *fake_share_sent;
static unsigned fake_share_sent_size;
static unsigned char *fake_share_waiting;
static unsigned fake_share_waiting_size;

unsigned khdays_link_vblanks(void)
{
    return fake_vblanks;
}

int khdays_link_music_track(const char *name)
{
    return strcmp(name, "TwilightR_F") == 0 || strcmp(name, "seq/TwilightR_F") == 0 ? 2
           : strcmp(name, "Herc_B") == 0                                            ? 8
                                                                                    : -1;
}

const char *khdays_link_music_play(int track, int fade)
{
    (void)fade;
    fake_music = track;
    fake_music_holding = 1;
    return NULL;
}

void khdays_link_music_stop(int fade)
{
    (void)fade;
    fake_music = -1;
    fake_music_holding = 0;
}

int khdays_link_music_playing(void)
{
    return fake_music_holding;
}

void khdays_link_music_frame(void) {}

unsigned long long khdays_link_seed(int *in_room)
{
    *in_room = 0;
    return fake_seed;
}

void khdays_link_next_seed(void) {}

int khdays_link_share(const void *data, unsigned size)
{
    free(fake_share_sent);
    fake_share_sent = malloc(size);
    memcpy(fake_share_sent, data, size);
    fake_share_sent_size = size;
    return 1;
}

int khdays_link_shared(unsigned char **data, unsigned *size)
{
    if (fake_share_waiting == NULL) {
        return 0;
    }
    *data = fake_share_waiting;
    *size = fake_share_waiting_size;
    fake_share_waiting = NULL;
    return 1;
}

/* The deck: panels 2-9 one cell each, 10 two cells across (its own the left), all of category 1; 11 a
 * link panel (category 4); each its id's kind. No other item is a panel. */
static unsigned short fake_deck[KHDAYS_LINK_DECK_CELLS];
static int fake_deck_busy;

int khdays_link_deck(unsigned short *cells)
{
    if (fake_state == 0) {
        return 0;
    }
    memcpy(cells, fake_deck, sizeof(fake_deck));
    return 1;
}

const char *khdays_link_deck_busy(void)
{
    return fake_deck_busy ? "not now" : NULL;
}

void khdays_link_set_deck(const unsigned short *cells)
{
    memcpy(fake_deck, cells, sizeof(fake_deck));
}

int khdays_link_panel(int id, KhdaysLinkPanel *out)
{
    if (id < 2 || id > 11) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->category = id == 11 ? 4 : 1;
    out->kind = id;
    out->shape = id == 10 ? 1 : -1; /* one cell: no shape, as the game has them */
    out->cells = id == 10 ? 3u : 0;
    return 1;
}

/* The field's enemies: kinds 0 and 1, actors from 0x1000 up; the last make's level, HP and strength. */
static unsigned fake_next_actor;
static int fake_level;
static double fake_enemy_hp, fake_strength;
static unsigned fake_removed;

int khdays_enemies_kinds(int *out, int room)
{
    if (fake_scene != 2) {
        return 0;
    }
    for (int i = 0; out != NULL && i < room && i < 2; ++i) {
        out[i] = i;
    }
    return 2;
}

const char *khdays_enemies_name(int kind)
{
    return kind == 0 ? "Shadow" : kind == 1 ? "Yellow Opera" : NULL;
}

/* Six kinds: the area's 0 and 1, 2 of another mission whose overlay fits, 3-5 that do not. */
static int fake_held;

void khdays_enemies_hold_mission(int on)
{
    fake_held = on;
}

int khdays_enemies_kind_count(void)
{
    return 6;
}

const char *khdays_enemies_can_make(int kind)
{
    return fake_scene != 2         ? "not in the field"
           : kind < 0 || kind >= 6 ? "unknown kind"
           : kind > 2              ? "no room for its kind"
                                   : NULL;
}

unsigned khdays_enemies_make(int kind, const float at[3], int level, double hp, double strength, const char **why)
{
    (void)at;
    *why = NULL;
    if (kind > 2) {
        *why = "no room for its kind";
        return 0;
    }
    fake_level = level;
    fake_enemy_hp = hp;
    fake_strength = strength;
    return 0x1000u + fake_next_actor++;
}

int khdays_enemies_remove(unsigned actor)
{
    fake_removed = actor;
    return 1;
}

int khdays_enemies_info(unsigned actor, KhdaysEnemyInfo *out)
{
    out->kind = 0;
    out->hp = out->max_hp = 30;
    out->level = 5;
    out->at[0] = (float)(actor - 0x1000u);
    out->at[1] = out->at[2] = 0;
    return 1;
}

void khdays_enemies_frame(void) {}

static void reset_game(void)
{
    fake_scene = 1;
    fake_frame = 0;
    fake_state = 0x02100000u;
    fake_munny = 0;
    fake_guest = 0;
    fake_hp = 40;
    fake_gauge = 1;
    fake_characters = 0;
    fake_character_part = 1;
    fake_sound[0] = fake_notice[0] = 0;
    fake_next_actor = 0;
    fake_removed = 0;
    fake_attacker = 0;
    fake_attacker_kind = -1;
    fake_game_over = fake_cleared = fake_left = fake_started = fake_title_idle = 0;
    memset(fake_items, 0, sizeof(fake_items));
    memset(fake_flags, 0, sizeof(fake_flags));
    memset(fake_deck, 0, sizeof(fake_deck));
    memset(fake_ram, 0, sizeof(fake_ram));
    fake_deck_busy = 0;
    fake_paused = 0;
    fake_vblanks = 0;
    fake_music = -1;
    fake_music_holding = 0;
    free(fake_share_sent);
    fake_share_sent = NULL;
    free(fake_share_waiting);
    fake_share_waiting = NULL;
}

/* ---- the cases' folders ----------------------------------------------------------------------------- */

static char root[512];

static void put(const char *relative, const char *text)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    CHECK(khdays_folders_write(path, text, strlen(text)));
}

/* A mod folder with its mod.ini (the interface `api`) and main script. */
static void mod_folder_api(const char *folder, const char *id, const char *kind, int api, const char *main_lua)
{
    char path[512], ini[512];
    snprintf(ini, sizeof(ini), "id = %s\nname = %s\nversion = 1.0\nkind = %s\napi = %d\n", id, id, kind, api);
    snprintf(path, sizeof(path), "mods/%s/mod.ini", folder);
    put(path, ini);
    snprintf(path, sizeof(path), "mods/%s/scripts/main.lua", folder);
    put(path, main_lua);
}

static void mod_folder(const char *folder, const char *id, const char *kind, const char *main_lua)
{
    mod_folder_api(folder, id, kind, 1, main_lua);
}

static void remove_all(const char *path)
{
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) {
        return;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        int n = 0;
        char **names = SDL_GlobDirectory(path, NULL, 0, &n);
        for (int i = 0; names != NULL && i < n; ++i) {
            char child[1024];
            snprintf(child, sizeof(child), "%s/%s", path, names[i]);
            remove_all(child);
        }
        SDL_free(names);
    }
    SDL_RemovePath(path);
}

static void begin_case(const char *name)
{
    snprintf(root, sizeof(root), "script_test_%s", name);
    remove_all(root);
    reset_game();
}

static void start(void)
{
    char mods[600], user[600];
    snprintf(mods, sizeof(mods), "%s/mods", root);
    snprintf(user, sizeof(user), "%s/saves", root);
    khdays_scripts_start_in(mods, user);
}

static void end_case(void)
{
    khdays_scripts_stop();
    remove_all(root);
}

static KhdaysMod *mod_by_folder(const char *folder)
{
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        if (strcmp(khdays_scripts_mod(i)->folder, folder) == 0) {
            return khdays_scripts_mod(i);
        }
    }
    return NULL;
}

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ++fake_frame;
        khdays_scripts_frame();
    }
}

static int log_has(const char *text)
{
    char path[600];
    char *log;
    int found;
    snprintf(path, sizeof(path), "%s/saves/logs/mods.log", root);
    log = khdays_folders_read(path, NULL);
    found = log != NULL && strstr(log, text) != NULL;
    free(log);
    return found;
}

/* ---- the cases ----------------------------------------------------------------------------------- */

static void hex(const unsigned char *d, char *out)
{
    for (int i = 0; i < 32; ++i) {
        snprintf(out + i * 2, 3, "%02x", d[i]);
    }
}

static void test_sha256(void)
{
    unsigned char d[32];
    char h[65];
    const char *million = NULL;
    khdays_sha256("", 0, d);
    hex(d, h);
    CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    khdays_sha256("abc", 3, d);
    hex(d, h);
    CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    khdays_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d);
    hex(d, h);
    CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    {
        KhdaysSha256 s;
        char a[1000];
        memset(a, 'a', sizeof(a));
        khdays_sha256_begin(&s);
        for (int i = 0; i < 1000; ++i) {
            khdays_sha256_add(&s, a, sizeof(a));
        }
        khdays_sha256_end(&s, d);
        hex(d, h);
        million = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
        CHECK(strcmp(h, million) == 0);
    }
}

static void test_manifest(void)
{
    KhdaysManifest m;
    char reason[200];
    const char *good = "\xef\xbb\xbf# a mod\nid = a.b-c_1\nname = A mod\nversion = 1.2\nauthor = me\n"
                       "kind = gameplay\napi = 1\nextra = kept\n";
    CHECK(khdays_manifest_parse(good, strlen(good), &m, reason, sizeof(reason)));
    CHECK(strcmp(m.id, "a.b-c_1") == 0 && strcmp(m.name, "A mod") == 0 && strcmp(m.main, "scripts/main.lua") == 0);
    CHECK(m.kind == KHDAYS_MOD_GAMEPLAY && m.api == 1 && strcmp(m.author, "me") == 0 && !m.memory_write);
    khdays_manifest_free(&m);
    {
        const char *writes = "id = a\nname = x\nversion = 1\nkind = gameplay\napi = 2\nmemory = write\n";
        const char *reads = "id = a\nname = x\nversion = 1\nkind = gameplay\napi = 2\nmemory = read\n";
        CHECK(khdays_manifest_parse(writes, strlen(writes), &m, reason, sizeof(reason)) && m.memory_write);
        khdays_manifest_free(&m);
        CHECK(khdays_manifest_parse(reads, strlen(reads), &m, reason, sizeof(reason)) && !m.memory_write);
        khdays_manifest_free(&m);
    }
    {
        static const struct {
            const char *text, *reason;
        } bad[] = {
            {"name = x\nversion = 1\nkind = visual\napi = 1\n", "mod.ini: id missing or not valid"},
            {"id = a b\nname = x\nversion = 1\nkind = visual\napi = 1\n", "mod.ini: id missing or not valid"},
            {"id = a\nversion = 1\nkind = visual\napi = 1\n", "mod.ini: name missing or not valid"},
            {"id = a\nname = x\nkind = visual\napi = 1\n", "mod.ini: version missing or not valid"},
            {"id = a\nname = x\nversion = 1\napi = 1\n", "mod.ini: kind missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = both\napi = 1\n", "mod.ini: kind missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = one\n", "mod.ini: api missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = 1\nmain = ../x.lua\n",
             "mod.ini: main missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = 1\nmain = scripts/../../x.lua\n",
             "mod.ini: main missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = 1\nmain = C:/x.lua\n",
             "mod.ini: main missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = 2\nmemory = all\n",
             "mod.ini: memory missing or not valid"},
        };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            CHECK(!khdays_manifest_parse(bad[i].text, strlen(bad[i].text), &m, reason, sizeof(reason)));
            if (strcmp(reason, bad[i].reason) != 0) {
                fprintf(stderr, "case %u: '%s'\n", (unsigned)i, reason);
                ++failures;
            }
        }
    }
}

static void test_order(void)
{
    KhdaysOrder o;
    const char *found[] = {"Alpha", "Beta", "New"};
    char *text;
    khdays_order_parse(&o, "Beta = on\nGone = off\nAlpha = off\n");
    khdays_order_merge(&o, found, 3);
    text = khdays_order_text(&o);
    CHECK(text != NULL && strcmp(text, "Beta = on\nAlpha = off\nNew = on\nGone = off\n") == 0);
    free(text);
    khdays_order_move(&o, 3, -1); /* New (entry 3, after Gone) before Alpha: Gone is skipped, absent */
    text = khdays_order_text(&o);
    CHECK(text != NULL && strcmp(text, "Beta = on\nNew = on\nAlpha = off\nGone = off\n") == 0);
    free(text);
    khdays_order_free(&o);
}

static void test_no_mods(void)
{
    char log[600];
    begin_case("none");
    put("mods/Content only/textures/readme.txt", "a content mod");
    start();
    frames(3);
    CHECK(!khdays_scripts_any());
    snprintf(log, sizeof(log), "%s/saves/logs/mods.log", root);
    CHECK(!khdays_folders_is_file(log));
    end_case();
}

static void test_sandbox(void)
{
    begin_case("sandbox");
    mod_folder("Box", "test.box", "gameplay",
               "local ok = io == nil and package == nil and debug == nil and dofile == nil and loadfile == nil\n"
               "ok = ok and os.execute == nil and os.remove == nil and os.getenv == nil and os.time ~= nil\n"
               "ok = ok and load(string.dump(function() end)) == nil\n"
               "ok = ok and load('return 41 + 1')() == 42\n"
               "ok = ok and not pcall(require, '../x') and not pcall(require, 'a..b')\n"
               "ok = ok and require('lib.util').twice(4) == 8 and require('lib.util').loads == 1\n"
               "ok = ok and not pcall(string.rep, 'x', 100 * 1024 * 1024)\n"
               "ok = ok and not pcall(setmetatable, {}, { __gc = function() end })\n"
               "if ok then game.set_munny(1) end\n"
               "print('sandbox', ok)\n");
    put("mods/Box/scripts/lib/util.lua",
        "local n = (package_loads or 0) + 1\nreturn { twice = function(x) return x * 2 end, loads = n }\n");
    start();
    CHECK(fake_munny == 1);
    CHECK(mod_by_folder("Box") != NULL && mod_by_folder("Box")->state == KHDAYS_MOD_LOADED);
    CHECK(log_has("test.box print scripts/main.lua:10 sandbox\ttrue"));
    end_case();
}

static void test_faults(void)
{
    KhdaysMod *m;
    begin_case("faults");
    mod_folder("A broken", "test.broken", "gameplay",
               "mod.on('frame', function(e)\n  if e.frame == 2 then local x = nil; x.y = 1 end\nend)\n");
    mod_folder("B counter", "test.counter", "gameplay",
               "mod.on('frame', function() game.set_munny(game.munny + 1) end)\n");
    mod_folder("C loop", "test.loop", "gameplay", "mod.on('frame', function() while true do end end)\n");
    mod_folder("D caught loop", "test.caught", "gameplay",
               "mod.on('frame', function()\n  while true do pcall(function() while true do end end) end\nend)\n");
    put("mods/E newer/mod.ini", "id = test.newer\nname = Newer\nversion = 1\nkind = gameplay\napi = 3\n");
    put("mods/F same id/mod.ini", "id = test.counter\nname = Twin\nversion = 1\nkind = gameplay\napi = 1\n");
    put("mods/G no main/mod.ini", "id = test.nomain\nname = No main\nversion = 1\nkind = gameplay\napi = 1\n");
    put("mods/H syntax/mod.ini", "id = test.syntax\nname = Syntax\nversion = 1\nkind = gameplay\napi = 1\n");
    put("mods/H syntax/scripts/main.lua", "this is not lua\n");
    start();
    frames(4);
    m = mod_by_folder("A broken");
    CHECK(m != NULL && m->state == KHDAYS_MOD_STOPPED && m->reason != NULL &&
          strstr(m->reason, "scripts/main.lua:2: attempt to index") == m->reason);
    CHECK(fake_munny == 4); /* B ran every frame, A's error notwithstanding */
    m = mod_by_folder("C loop");
    CHECK(m != NULL && m->state == KHDAYS_MOD_PAUSED);
    m = mod_by_folder("D caught loop");
    CHECK(m != NULL && m->state == KHDAYS_MOD_PAUSED);
    m = mod_by_folder("E newer");
    CHECK(m != NULL && m->state == KHDAYS_MOD_REFUSED &&
          strcmp(m->reason, "needs mod interface 3, this port has 2") == 0);
    m = mod_by_folder("F same id");
    CHECK(m != NULL && m->state == KHDAYS_MOD_REFUSED && strcmp(m->reason, "same id as B counter") == 0);
    m = mod_by_folder("G no main");
    CHECK(m != NULL && m->state == KHDAYS_MOD_REFUSED &&
          strcmp(m->reason, "main script scripts/main.lua not found") == 0);
    m = mod_by_folder("H syntax");
    CHECK(m != NULL && m->state == KHDAYS_MOD_STOPPED);
    CHECK(log_has("test.broken error scripts/main.lua:2 attempt to index"));
    CHECK(log_has("test.broken stopped - the error above"));
    CHECK(log_has("test.loop paused - over its time allowance"));
    end_case();
}

static void test_order_file(void)
{
    begin_case("order");
    mod_folder("One", "test.one", "gameplay", "game.set_munny(game.munny * 10 + 1)\n");
    mod_folder("Two", "test.two", "gameplay", "game.set_munny(game.munny * 10 + 2)\n");
    mod_folder("Three", "test.three", "gameplay", "game.set_munny(game.munny * 10 + 3)\n");
    put("saves/mods/order.ini", "Two = on\nOne = off\n");
    start();
    CHECK(fake_munny == 23); /* Two, then Three (new, after the listed), One off */
    CHECK(mod_by_folder("One")->state == KHDAYS_MOD_OFF);
    end_case();
}

static void test_events_and_state(void)
{
    int entity[3] = {30, 50, 1};
    begin_case("events");
    mod_folder("Ears", "test.ears", "gameplay",
               "mod.on('title', function() game.set_flag(1, true) end)\n"
               "mod.on('scene', function(e)\n"
               "  if e.from == 1 and e.to == 2 and e.argument == 7 then game.set_flag(2, true) end\nend)\n"
               "mod.on('item', function(e)\n"
               "  if e.item == 5 and e.count == 3 and e.change == 3 then game.set_flag(3, true) end\n"
               "  if e.item == 9 then game.set_flag(9, true) end\nend)\n"
               "mod.on('munny', function(e)\n"
               "  if e.change == 10 and e.total == 10 then game.set_munny(e.total + e.change) end\nend)\n"
               "mod.on('hp', function(e)\n"
               "  if e.who == 'player' and e.before == 50 and e.after == 30 and e.max == 50 then\n"
               "    game.set_flag(4, true)\n  end\nend)\n"
               "mod.on('mission_end', function(e)\n"
               "  if e.mission == 15 and not e.given_up and e.rank == 2 and e.complete == true then\n"
               "    game.set_flag(5, true)\n  end\nend)\n"
               "local h = mod.on('frame', function() game.set_flag(6, not game.flag(6)) end)\n"
               "mod.on('frame', function(e) if e.frame == 4 then mod.off(h) end end)\n"
               "mod.on('save', function() game.set_flag(7, true) end)\n"
               "mod.on('mission_start', function(e) if e.mission == 15 then game.set_flag(8, true) end end)\n");
    mod_folder("Hands", "test.hands", "gameplay",
               "local a, b = game.set_item(0x8d0, 1)\n"
               "if a == nil and b == 'unknown item' then game.set_flag(20, true) end\n"
               "game.set_item(4, 150)\n"
               "if game.item(4) == 99 and game.flag(0x4db + 4) then game.set_flag(21, true) end\n"
               "game.set_munny(2000000)\n"
               "if game.munny == 999999 then game.set_flag(22, true) end\n"
               "game.set_munny(0)\n"
               "if mission.id == nil and scene.id == 1 and player.position() == nil and player.hp == nil and\n"
               "   select(2, player.set_hp(5)) == 'no player' then game.set_flag(23, true) end\n"
               "local f, why = game.flag(0x4000)\n"
               "if f == nil and why == 'unknown flag' then game.set_flag(24, true) end\n");
    mod_folder("Looks", "test.looks", "visual",
               "local a, b = game.set_munny(5)\n"
               "if a == nil and b == 'visual mod' then print('refused') end\n");
    start();
    frames(1); /* the title, and the items' first copy */
    CHECK(khdays_link_flag(1));
    CHECK(khdays_link_flag(20));
    CHECK(khdays_link_flag(21));
    CHECK(khdays_link_flag(22));
    CHECK(khdays_link_flag(23));
    CHECK(khdays_link_flag(24));
    CHECK(log_has("test.looks print scripts/main.lua:2 refused"));
    fake_items[9] = 4; /* in the title: a save loaded, nothing obtained */
    frames(1);
    CHECK(!khdays_link_flag(9));
    fake_scene = 2;
    frames(1);
    CHECK(khdays_link_flag(2));
    fake_items[5] = 3;
    fake_munny = 10;
    frames(1);
    CHECK(khdays_link_flag(3));
    CHECK(fake_munny == 20);
    khdays_scripts_hp(entity, 50);
    CHECK(khdays_link_flag(4));
    khdays_scripts_mission_end(15, 2, 1);
    CHECK(khdays_link_flag(5));
    khdays_scripts_mission_start(); /* no field made: not a mission's start */
    CHECK(!khdays_link_flag(8));
    khdays_scripts_field_begins();
    khdays_scripts_mission_start();
    CHECK(khdays_link_flag(8));
    khdays_link_set_flag(8, 0);
    khdays_scripts_mission_start(); /* the same mission's next run */
    CHECK(!khdays_link_flag(8));
    khdays_scripts_saved();
    CHECK(khdays_link_flag(7));
    {
        /* flag 6 toggles each frame until the frame 4 handler takes it off */
        int at4;
        frames(1);
        at4 = khdays_link_flag(6);
        frames(2);
        CHECK(khdays_link_flag(6) == at4);
    }
    end_case();
}

static void test_player(void)
{
    begin_case("player");
    mod_folder("Infinite", "test.infinite", "gameplay",
               "mod.on('frame', function()\n"
               "  local hp, max = player.hp, player.max_hp\n"
               "  if hp ~= nil and hp < max and select(2, player.set_hp(max + 100)) == 'not now' then\n"
               "    game.set_flag(24, true)\n"
               "  end\nend)\n");
    start();
    frames(1);
    CHECK(fake_hp == 40); /* the title: no player */
    fake_scene = 2;
    fake_gauge = 0;
    frames(1);
    CHECK(fake_hp == 40 && khdays_link_flag(24)); /* the field loading: not now */
    fake_gauge = 1;
    frames(1);
    CHECK(fake_hp == 50); /* clamped to the maximum */
    end_case();
}

static void test_options(void)
{
    KhdaysModsView view;
    char path[600], *kept;
    memset(&view, 0, sizeof(view));
    begin_case("options");
    mod_folder("Options", "test.options", "gameplay",
               "local t = mod.option{ key = 'hp', kind = 'toggle', label = { en = 'Infinite HP', es = 'PV' },\n"
               "  on_change = function(v) game.set_flag(1, v) end }\n"
               "local c = mod.option{ key = 'speed', kind = 'choice', label = 'Speed', values = { 'a', 'b', 'c' },\n"
               "  default = 2, on_change = function(v) game.set_munny(v) end }\n"
               "local n = mod.option{ key = 'boost', kind = 'number', label = 'Boost', min = 0, max = 10, step = 2,\n"
               "  default = 4 }\n"
               "mod.option{ key = 'potion', kind = 'button', label = 'Give',\n"
               "  on_change = function() game.set_item(2, game.item(2) + 1) end }\n"
               "local bad = 0\n"
               "for _, o in ipairs({ { key = 'hp', kind = 'toggle', label = 'x' },\n"
               "                     { key = 'k', kind = 'nope', label = 'x' },\n"
               "                     { key = 'k', kind = 'choice', label = 'x', values = { 'one' } },\n"
               "                     { key = 'k', kind = 'number', label = 'x', min = 0, max = 1, step = 0.5 },\n"
               "                     { key = 'k', kind = 'toggle' } }) do\n"
               "  if not pcall(mod.option, o) then bad = bad + 1 end\n"
               "end\n"
               "if bad == 5 then game.set_flag(2, true) end\n"
               "if t.value == false and c.value == 2 then game.set_flag(4, true) end\n"
               "if t.value == true and c.value == 3 then game.set_flag(5, true) end\n"
               "if n.value == 4 then game.set_flag(6, true) end\n"
               "n:set(7)\n"
               "if n.value == 8 then game.set_flag(3, true) end\n");
    start();
    CHECK(khdays_link_flag(2) && khdays_link_flag(4) && khdays_link_flag(6) && !khdays_link_flag(5));
    CHECK(khdays_link_flag(3)); /* 7 on steps of 2 from 0: 8 */
    CHECK(khdays_scripts_view(&view) && view.count == 1 && view.mods[0].option_count == 4);
    CHECK(view.count == 1 && strcmp(khdays_scripts_text(view.mods[0].options[0].label, 5), "PV") == 0 &&
          strcmp(khdays_scripts_text(view.mods[0].options[0].label, 2), "Infinite HP") == 0);
    khdays_scripts_ask(KHDAYS_ASK_OPTION, "test.options", "hp", 1);
    khdays_scripts_ask(KHDAYS_ASK_OPTION, "test.options", "speed", 3);
    khdays_scripts_ask(KHDAYS_ASK_OPTION, "test.options", "speed", 9); /* not one of its values: dropped */
    khdays_scripts_ask(KHDAYS_ASK_BUTTON, "test.options", "potion", 0);
    frames(1);
    CHECK(khdays_link_flag(1) && fake_munny == 3 && fake_items[2] == 1);
    CHECK(khdays_scripts_view(&view) && view.mods[0].options[1].value == 3);
    snprintf(path, sizeof(path), "%s/saves/mods/test.options/options.ini", root);
    kept = khdays_folders_read(path, NULL);
    CHECK(kept != NULL && strstr(kept, "hp = 1\n") != NULL && strstr(kept, "speed = 3\n") != NULL &&
          strstr(kept, "boost = 8\n") != NULL && strstr(kept, "potion") == NULL);
    free(kept);
    /* the next start: the values kept; one out of its range back to its default */
    khdays_scripts_stop();
    put("saves/mods/test.options/options.ini", "hp = 1\nspeed = 3\nboost = 99\n");
    reset_game();
    start();
    CHECK(khdays_link_flag(5) && !khdays_link_flag(4)); /* the kept values */
    CHECK(khdays_link_flag(6));                         /* boost's 99 out of range: its default */
    khdays_scripts_view_free(&view);
    end_case();
}

static void test_menu_asks(void)
{
    KhdaysModsView view;
    char path[600], *order;
    memset(&view, 0, sizeof(view));
    begin_case("asks");
    mod_folder("A", "test.a", "gameplay", "game.set_munny(game.munny + 1)\n");
    mod_folder("B", "test.b", "gameplay", "mod.on('frame', function() while true do end end)\n");
    start();
    frames(1);
    CHECK(mod_by_folder("B")->state == KHDAYS_MOD_PAUSED);
    khdays_scripts_ask(KHDAYS_ASK_MOVE, "B", NULL, -1);
    khdays_scripts_ask(KHDAYS_ASK_ENABLE, "A", NULL, 0);
    frames(1);
    CHECK(khdays_scripts_mod(0) == mod_by_folder("B") && mod_by_folder("A")->state == KHDAYS_MOD_OFF);
    snprintf(path, sizeof(path), "%s/saves/mods/order.ini", root);
    order = khdays_folders_read(path, NULL);
    CHECK(order != NULL && strcmp(order, "B = on\nA = off\n") == 0);
    free(order);
    khdays_scripts_ask(KHDAYS_ASK_ENABLE, "A", NULL, 1);
    frames(1);
    CHECK(mod_by_folder("A")->state == KHDAYS_MOD_LOADED && fake_munny == 2); /* its main ran again */
    CHECK(khdays_scripts_view(&view) && view.count == 2 && strcmp(view.mods[0].folder, "B") == 0 &&
          view.mods[0].state == KHDAYS_MOD_PAUSED);
    khdays_scripts_ask(KHDAYS_ASK_RESUME, "test.b", NULL, 0);
    frames(1); /* resumed, and paused again by its loop */
    CHECK(mod_by_folder("B")->state == KHDAYS_MOD_PAUSED);
    CHECK(log_has("test.b resumed - by the player"));
    khdays_scripts_view_free(&view);
    end_case();
}

/* Mods of files alone (no mod.ini) are listed with the scripted ones, turned off from the Mods page
 * (order.ini), and off by hand with a .ignore file or a name beginning with a dot. */
static void test_content_mods(void)
{
    KhdaysModsView view;
    char path[600], *order;
    const KhdaysModEntry *e;
    memset(&view, 0, sizeof(view));
    begin_case("content");
    mod_folder("S", "test.s", "gameplay", "game.set_munny(game.munny + 1)\n");
    put("mods/Looks/files/x.bin", "x");
    put("mods/Ignored/files/x.bin", "x");
    put("mods/Ignored/.ignore", "");
    put("mods/.Hidden/files/x.bin", "x");
    put("saves/mods/order.ini", "S = on\nLooks = off\n");
    start();
    frames(1);
    CHECK(khdays_mods_count() == 4 && khdays_scripts_mod_count() == 1);
    CHECK(strcmp(khdays_mods_at(0)->folder, "S") == 0 && khdays_mods_at(0)->scripted);
    /* what the game reads: the mods on as it started, S alone (Looks off, the other two off by hand) */
    khdays_mods_start();
    CHECK(khdays_mods_started_count() == 1 && khdays_mods_started_has("S") && !khdays_mods_started_has("Looks"));
    CHECK(strcmp(khdays_mods_started_folder(0), "S") == 0 && khdays_mods_started_folder(1) == NULL);
    e = khdays_mods_by_folder("Looks");
    CHECK(e != NULL && !e->scripted && !e->on && !e->ignored && !khdays_mods_active(e));
    e = khdays_mods_by_folder("Ignored");
    CHECK(e != NULL && e->on && e->ignored && !khdays_mods_active(e));
    e = khdays_mods_by_folder(".Hidden");
    CHECK(e != NULL && e->ignored);
    CHECK(khdays_scripts_view(&view) && view.count == 4);
    CHECK(strcmp(view.mods[0].folder, "S") == 0 && !view.mods[0].content && view.mods[0].state == KHDAYS_MOD_LOADED);
    /* the listed ones in order.ini's order, then the new ones in name order */
    CHECK(view.mods[1].content && !view.mods[1].enabled && view.mods[1].state == KHDAYS_MOD_OFF &&
          strcmp(view.mods[1].folder, "Looks") == 0);
    CHECK(view.mods[2].content && view.mods[2].ignored && strcmp(view.mods[2].folder, ".Hidden") == 0);
    CHECK(view.mods[3].content && view.mods[3].ignored && strcmp(view.mods[3].folder, "Ignored") == 0);
    khdays_scripts_ask(KHDAYS_ASK_ENABLE, "Looks", NULL, 1);
    khdays_scripts_ask(KHDAYS_ASK_MOVE, "Looks", NULL, -1); /* before S */
    frames(1);
    e = khdays_mods_by_folder("Looks");
    CHECK(e != NULL && khdays_mods_active(e) && strcmp(khdays_mods_at(0)->folder, "Looks") == 0);
    /* its files at the next start: the start's list is as it was */
    CHECK(khdays_mods_started_count() == 1 && !khdays_mods_started_has("Looks"));
    CHECK(khdays_scripts_mod_count() == 1 && mod_by_folder("S")->state == KHDAYS_MOD_LOADED);
    snprintf(path, sizeof(path), "%s/saves/mods/order.ini", root);
    order = khdays_folders_read(path, NULL);
    CHECK(order != NULL && strcmp(order, "Looks = on\nS = on\n.Hidden = on\nIgnored = on\n") == 0);
    free(order);
    khdays_scripts_view_free(&view);
    CHECK(khdays_scripts_view(&view) && view.count == 4 && view.mods[0].enabled &&
          view.mods[0].state == KHDAYS_MOD_LOADED && strcmp(view.mods[0].folder, "Looks") == 0);
    khdays_scripts_view_free(&view);
    end_case();

    /* a native plugin's mod alone (no scripts/, its mod.ini naming the SDK): listed, not a scripted mod */
    begin_case("native_only");
    put("mods/Native/mod.ini", "id = test.native\nname = Native\nversion = 1\nsdk = 1\n");
    start();
    frames(1);
    CHECK(khdays_mods_count() == 1 && !khdays_mods_at(0)->scripted && khdays_scripts_mod_count() == 0);
    end_case();

    /* content mods alone: no scripted mod, the page's asks still taken */
    begin_case("content_only");
    put("mods/Only/files/x.bin", "x");
    start();
    frames(1);
    CHECK(khdays_scripts_mod_count() == 0 && khdays_mods_count() == 1);
    CHECK(khdays_scripts_view(&view) && view.count == 1 && view.mods[0].content && view.mods[0].enabled);
    khdays_scripts_view_free(&view);
    khdays_scripts_ask(KHDAYS_ASK_ENABLE, "Only", NULL, 0);
    frames(1);
    CHECK(!khdays_mods_active(khdays_mods_by_folder("Only")));
    CHECK(khdays_scripts_view(&view) && view.count == 1 && !view.mods[0].enabled &&
          view.mods[0].state == KHDAYS_MOD_OFF);
    khdays_scripts_view_free(&view);
    end_case();
}

static void test_actions(void)
{
    begin_case("actions");
    mod_folder("Acts", "test.acts", "gameplay",
               "local function says(want, a, b) if a == nil and b == want then return true end\n"
               "  print('unexpected', a, b) return false end\n"
               "local n = 0\n"
               "local function ok(c) if c then n = n + 1 end end\n"
               "mod.on('frame', function(e)\n"
               "  if e.frame ~= 1 then return end\n"
               "  ok(game.give_item(3) and game.item(3) == 1 and game.flag(0x4db + 3))\n"
               "  ok(game.give_item(3, 500) and game.item(3) == 99)\n"
               "  ok(game.take_item(3, 40) and game.item(3) == 59 and game.take_item(3, 500) and game.item(3) == 0)\n"
               "  ok(says('unknown item', game.give_item(0x8d0)))\n"
               "  ok(not pcall(game.give_item, 3, -1))\n"
               "  ok(game.play_sound('COMMON_SYS', 'se_sys_open'))\n"
               "  ok(says('unknown sound', game.play_sound('NOPE', 'x')))\n"
               "  ok(says('not in the field', game.show_text('Hello')))\n"
               "  print('frame 1', n)\n"
               "end)\n"
               "mod.on('scene', function(e)\n"
               "  if e.to == 2 then\n"
               "    ok(game.show_text('Hola, \xc3\xb1"
               "and\xc3\xba') and says('busy', game.show_text('again')))\n"
               "    ok(says('not a text the game can show', game.show_text('bad\\1')))\n"
               "    print('field', n)\n"
               "  end\n"
               "end)\n");
    mod_folder("Looks", "test.looks", "visual",
               "mod.on('frame', function(e)\n"
               "  if e.frame == 1 then\n"
               "    local a, b = game.give_item(3)\n"
               "    print(a == nil and b == 'visual mod', game.play_sound('COMMON_SYS', 'se_sys_close'))\n"
               "  end\n"
               "end)\n");
    start();
    frames(1);
    CHECK(log_has("test.acts print scripts/main.lua:15 frame 1\t8"));
    CHECK(log_has("test.looks print scripts/main.lua:4 true\ttrue"));
    CHECK(strcmp(fake_sound, "COMMON_SYS/se_sys_close") == 0);
    fake_scene = 2;
    frames(1);
    CHECK(log_has("test.acts print scripts/main.lua:21 field\t10"));
    CHECK(strcmp(fake_notice, "Hola, ?and?") == 0);
    end_case();
}

/* Every notice waiting, dropped. */
static void drop_notices(void)
{
    int kind;
    char *mod, *text;
    while (khdays_scripts_take_notice(&kind, &mod, &text)) {
        free(mod);
        free(text);
    }
}

static void test_files_and_notices(void)
{
    char path[600], *text;
    int kind;
    char *mod, *said;
    begin_case("files");
    drop_notices();
    put("mods/Files/data/shipped.txt", "from the mod");
    mod_folder("Files", "test.files", "gameplay",
               "local n = 0\n"
               "local function ok(c) if c then n = n + 1 else print('not ok') end end\n"
               "ok(mod.read('shipped.txt') == 'from the mod')\n"
               "ok(mod.write('notes/kept.txt', 'kept'))\n"
               "ok(mod.read('notes/kept.txt') == 'kept')\n"
               "for _, bad in ipairs({ '../x', '/x', 'C:/x', 'a/../../x', 'a\\\\b', '', 'a//b', 'a/' }) do\n"
               "  local a, why = mod.write(bad, 'x')\n"
               "  ok(a == nil and why == \"not a path of the mod's\")\n"
               "  ok(mod.read(bad) == nil)\n"
               "end\n"
               "ok(select(2, mod.read('missing.txt')) == 'not found')\n"
               "mod.notice('hello there')\n"
               "print('files', n)\n");
    mod_folder("Broken", "test.broken", "gameplay", "mod.on('frame', function() error('on purpose') end)\n");
    start();
    CHECK(log_has("test.files print scripts/main.lua:13 files\t20"));
    snprintf(path, sizeof(path), "%s/saves/mods/test.files/notes/kept.txt", root);
    text = khdays_folders_read(path, NULL);
    CHECK(text != NULL && strcmp(text, "kept") == 0);
    free(text);
    CHECK(khdays_scripts_take_notice(&kind, &mod, &said) && kind == KHDAYS_NOTICE_SAID &&
          strcmp(mod, "test.files") == 0 && strcmp(said, "hello there") == 0);
    free(mod);
    free(said);
    frames(1);
    CHECK(khdays_scripts_take_notice(&kind, &mod, &said) && kind == KHDAYS_NOTICE_STOPPED &&
          strcmp(mod, "test.broken") == 0 && strstr(said, "on purpose") != NULL);
    free(mod);
    free(said);
    end_case();
}

static void test_room_fingerprint(void)
{
    unsigned int first, second, alone;
    begin_case("rooms");
    mod_folder("Look", "test.look", "visual", "print('a visual mod')\n");
    start();
    CHECK(fake_mods_fingerprint == 0 && fake_mods_count == 0); /* visual mods are each player's own */
    khdays_scripts_stop();
    fake_characters = 1; /* a mod's character counts, its mod without scripts */
    start();
    alone = fake_mods_fingerprint;
    CHECK(alone != 0 && fake_mods_count == 1 && fake_mods_list[0] == 4 && memcmp(fake_mods_list + 1, "Wind", 4) == 0 &&
          fake_mods_list[5] == 14 && memcmp(fake_mods_list + 6, "test.wind/wind", 14) == 0);
    khdays_scripts_stop();
    fake_character_part = 2; /* its description or its data changed */
    start();
    CHECK(fake_mods_fingerprint != 0 && fake_mods_fingerprint != alone);
    khdays_scripts_stop();
    fake_characters = 0;
    mod_folder("Play", "test.play", "gameplay", "print('a gameplay mod')\n");
    start();
    first = fake_mods_fingerprint;
    CHECK(first != 0 && fake_mods_count == 1 && fake_mods_list[0] == 9 &&
          memcmp(fake_mods_list + 1, "test.play", 9) == 0 && fake_mods_list[10] == 3 &&
          memcmp(fake_mods_list + 11, "1.0", 3) == 0);
    khdays_scripts_stop();
    start();
    CHECK(fake_mods_fingerprint == first); /* the same mods, the same fingerprint */
    khdays_scripts_stop();
    put("mods/Play/scripts/main.lua", "print('changed')\n");
    start();
    second = fake_mods_fingerprint;
    CHECK(second != 0 && second != first); /* a script changed: another fingerprint */
    khdays_scripts_ask(KHDAYS_ASK_ENABLE, "Play", NULL, 0);
    frames(1);
    CHECK(fake_mods_fingerprint == 0 && fake_mods_count == 0);
    end_case();
}

static void test_reload(void)
{
    char path[600];
    begin_case("reload");
    mod_folder("Live", "test.live", "gameplay",
               "local o = mod.option{ key = 'n', kind = 'number', label = 'N', min = 0, max = 10 }\n"
               "o:set(5)\n"
               "game.set_munny(game.munny + 1)\n");
    start();
    CHECK(fake_munny == 1);
    /* the studio asks: loaded again, its main run again, its option's value kept */
    snprintf(path, sizeof(path), "%s/saves/mods/reload.txt", root);
    CHECK(khdays_folders_write(path, "test.live\n", 10));
    frames(30);
    CHECK(fake_munny == 2 && !khdays_folders_is_file(path));
    CHECK(log_has("test.live reloaded - its files changed or it was asked to"));
    /* its script changed: loaded again by itself */
    SDL_Delay(20); /* a newer write time */
    put("mods/Live/scripts/main.lua", "game.set_munny(game.munny + 10)\n");
    frames(30);
    CHECK(fake_munny == 12);
    /* a stopped mod comes back once fixed */
    put("mods/Live/scripts/main.lua", "mod.on('frame', function() error('x') end)\n");
    frames(30);
    frames(1);
    CHECK(mod_by_folder("Live")->state == KHDAYS_MOD_STOPPED);
    SDL_Delay(20);
    put("mods/Live/scripts/main.lua", "game.set_munny(100)\n");
    frames(30);
    CHECK(mod_by_folder("Live")->state == KHDAYS_MOD_LOADED && fake_munny == 100);
    end_case();
}

static void test_guest(void)
{
    begin_case("guest");
    mod_folder("Host only", "test.host", "gameplay",
               "local a, b = game.set_munny(5)\nif a == nil and b == 'guest' then print('refused') end\n");
    fake_guest = 1;
    start();
    CHECK(fake_munny == 0);
    CHECK(log_has("test.host print scripts/main.lua:2 refused"));
    end_case();
}

static void test_api_versions(void)
{
    begin_case("versions");
    mod_folder("One", "test.one", "gameplay",
               "print('one', enemies == nil, pcall(mod.on, 'enemy_defeated', function() end))\n");
    mod_folder_api(
        "Two", "test.two", "gameplay", 2,
        "print('two', type(enemies), type(enemies.make), mod.on('enemy_defeated', function() end) ~= nil)\n");
    mod_folder_api("Three", "test.three", "gameplay", 3, "print('three')\n");
    start();
    CHECK(log_has("test.one print scripts/main.lua:1 one\ttrue\tfalse"));
    CHECK(log_has("test.two print scripts/main.lua:1 two\ttable\tfunction\ttrue"));
    CHECK(mod_by_folder("Three")->state == KHDAYS_MOD_REFUSED);
    end_case();
}

static void test_enemies(void)
{
    begin_case("enemies");
    mod_folder_api(
        "Waves", "test.waves", "gameplay", 2,
        "local n, seen, heard = 0, 0, {}\n"
        "local function ok(c, what) if c then n = n + 1 else print('not ok', what) end end\n"
        "local function says(want, a, b) return a == nil and b == want end\n"
        "local made\n"
        "mod.on('enemy_appeared', function(e) seen = seen + 1 end)\n"
        "mod.on('enemy_defeated', function(e)\n"
        "  heard[#heard + 1] = tostring(e.enemy) .. ' ' .. e.kind .. ' ' .. tostring(e.mine)\n"
        "end)\n"
        "ok(says('not in the field', enemies.make{ kind = 0, at = 'player' }), 'title')\n"
        "mod.on('scene', function(e)\n"
        "  if e.to ~= 2 then return end\n"
        "  local k = enemies.kinds()\n"
        "  ok(#k == 2 and k[1].kind == 0 and k[1].name == 'Shadow', 'kinds')\n"
        "  made = enemies.make{ kind = 0, at = 'player', count = 3, spread = 2, level = 4, hp = 2,"
        " strength = 1.5 }\n"
        "  ok(#made == 3 and made[1] ~= made[2], 'made')\n"
        "  ok(says('no room for its kind', enemies.make{ kind = 5, at = { 0, 0, 0 } }), 'kind 5')\n"
        "  ok(says('unknown kind', enemies.make{ kind = 300, at = { 0, 0, 0 } }), 'kind 300')\n"
        "  ok(not pcall(enemies.make, { kind = 0, at = 'player', count = 0 }), 'count 0')\n"
        "  ok(not pcall(enemies.make, { kind = 'x', at = 'player' }), 'kind x')\n"
        "  ok(#enemies.list() == 3 and enemies.info(made[1]).max_hp == 30, 'list')\n"
        "  ok(enemies.remove(made[1]) == true and says('gone', enemies.remove(made[1])), 'remove')\n"
        "  ok(#enemies.list() == 2 and says('gone', enemies.info(made[1])), 'removed')\n"
        "  local all = enemies.kinds('all')\n"
        "  ok(#all == 6 and all[3].ready and not all[4].ready and all[1].name == 'Shadow', 'all')\n"
        "  ok(#enemies.make{ kind = 2, at = { 0, 0, 0 }, level = 4, hp = 2, strength = 1.5 } == 1 and #enemies.list() == 3, 'another kind')\n"
        "  print('field', n)\n"
        "end)\n"
        "mod.on('frame', function(e)\n"
        "  if e.frame == 4 then print('heard', seen, table.concat(heard, ','), #enemies.list()) end\n"
        "end)\n");
    mod_folder_api("Other", "test.other", "gameplay", 2,
                   "mod.on('enemy_appeared', function(e) print('not mine appeared') end)\n"
                   "mod.on('enemy_defeated', function(e) print('other', tostring(e.enemy), e.mine) end)\n");
    start();
    frames(1); /* the title seen, then the field */
    fake_scene = 2;
    frames(1);
    CHECK(log_has("test.waves print scripts/main.lua:26 field\t12"));
    CHECK(fake_level == 4 && fake_enemy_hp == 2 && fake_strength == 1.5 && fake_removed == 0x1000u);
    khdays_api_enemies_defeated(0x1001u, 0); /* a made one */
    khdays_api_enemies_defeated(0x2000u, 1); /* the mission's own */
    frames(1);
    khdays_api_enemies_field_left();
    frames(1);
    CHECK(log_has("test.waves print scripts/main.lua:29 heard\t3\t2 0 true,nil 1 false\t0"));
    CHECK(log_has("test.other print scripts/main.lua:2 other\tnil\tfalse"));
    CHECK(!log_has("not mine appeared"));
    end_case();
}

static void test_enemies_guest(void)
{
    begin_case("enemies_guest");
    mod_folder_api("Guest", "test.guest", "gameplay", 2,
                   "mod.on('scene', function(e)\n"
                   "  if e.to == 2 then print('guest', enemies.make{ kind = 0, at = 'player' }) end\n"
                   "end)\n");
    fake_guest = 1;
    start();
    frames(1);
    fake_scene = 2;
    frames(1);
    CHECK(log_has("test.guest print scripts/main.lua:2 guest\tnil\tguest"));
    end_case();
}

static void test_defeat(void)
{
    int entity[3] = {30, 50, 1}; /* HP 30 of 50, the player */
    begin_case("defeat");
    mod_folder_api("A holder", "test.holder", "gameplay", 2,
                   "print('take', player.take_defeat(true))\n"
                   "mod.on('defeat', function(e) print('defeat', e.damage, tostring(e.enemy), tostring(e.kind)) end)\n"
                   "mod.on('mission_end', function(e) print('end', e.ending) end)\n"
                   "mod.on('frame', function(e) if e.frame == 2 then error('on purpose') end end)\n");
    mod_folder_api("B second", "test.second", "gameplay", 2,
                   "print('second', player.take_defeat(true))\n"
                   "mod.on('defeat', function(e) print('second heard') end)\n");
    mod_folder("C one", "test.one", "gameplay",
               "mod.on('mission_end', function(e) print('one', tostring(e.ending)) end)\n");
    start();
    CHECK(log_has("test.holder print scripts/main.lua:1 take\ttrue"));
    CHECK(log_has("test.second print scripts/main.lua:1 second\tnil\ttaken by test.holder"));
    frames(1);
    fake_attacker = 0x2000u; /* the mission's Scarlet Tango */
    fake_attacker_kind = 26;
    CHECK(khdays_scripts_lethal(entity, -5) == 1);
    CHECK(log_has("test.holder print scripts/main.lua:2 defeat\t35\tnil\t26"));
    CHECK(!log_has("second heard"));
    CHECK(khdays_scripts_lethal(entity, 10) == 10); /* not a defeat */
    entity[2] = 0;
    CHECK(khdays_scripts_lethal(entity, 0) == 0); /* not the player */
    entity[2] = 1;
    /* the endings */
    khdays_scripts_field_begins();
    khdays_scripts_mission_start();
    fake_cleared = 1;
    khdays_scripts_mission_end(15, 1, -1);
    CHECK(log_has("test.holder print scripts/main.lua:3 end\tcleared"));
    CHECK(log_has("test.one print scripts/main.lua:1 one\tnil")); /* api 1: as before */
    entity[0] = 0;
    khdays_scripts_hp(entity, 30); /* the defeat given back: the player falls */
    entity[0] = 30;
    fake_game_over = 1;
    frames(1); /* frame 2: the game over seen; the holder stops on its error */
    fake_game_over = 0;
    khdays_link_set_flag(KHDAYS_LINK_GIVEN_UP, 1); /* the game over's give-up */
    khdays_scripts_mission_end(15, -1, -1);
    CHECK(mod_by_folder("A holder")->state == KHDAYS_MOD_STOPPED);
    CHECK(khdays_scripts_lethal(entity, -5) == -5); /* a stopped holder gives the defeat back */
    end_case();
}

static void test_deck(void)
{
    begin_case("deck");
    mod_folder_api("Loadout", "test.loadout", "gameplay", 2,
                   "local n = 0\n"
                   "local function ok(c, what) if c then n = n + 1 else print('not ok', what) end end\n"
                   "local function says(want, a, b) return a == nil and b == want end\n"
                   "local function at(id)\n"
                   "  for _, p in ipairs(deck.read().panels) do if p.panel == id then return p end end\n"
                   "end\n"
                   "local d = deck.read()\n"
                   "ok(d.open == 16 and #d.panels == 13 and d.spare[3] == 1 and d.spare[9] == nil, 'read')\n"
                   "ok(at(2).page == 1 and at(2).row == 1 and at(2).col == 2, 'cell')\n"
                   "local saved = deck.save()\n"
                   "ok(says('unknown panel', deck.give(40)) and says('unknown panel', deck.info(1)), 'unknown')\n"
                   "ok(deck.info(10).link == false and #deck.info(10).cells == 2 and deck.info(11).link, 'info')\n"
                   "ok(deck.give(4) == true and deck.read().spare[4] == 1, 'spare')\n"
                   "ok(deck.give(10, true) == true and at(10).col == 3, 'first fit')\n"
                   "ok(says('no room', deck.give(10, { page = 1, row = 1, col = 5 })), 'taken cell')\n"
                   "ok(says('no room', deck.give(5, { page = 1, row = 4, col = 2 })), 'closed')\n"
                   "ok(deck.give(5, { page = 1, row = 4, col = 1 }) == true, 'at')\n"
                   "ok(says('no room', deck.give(11, true)), 'a link panel without its panel')\n"
                   "ok(deck.give(6, true) == true and at(6).col == 4, 'on a panel of its category')\n"
                   "ok(says('no room', deck.give(7, true)), 'full')\n"
                   "ok(deck.read().spare[7] == nil and deck.read().spare[11] == nil, 'nothing changed')\n"
                   "ok(says('not there', deck.take(3, 2)) and deck.take(2) == true and at(2) == nil, 'take')\n"
                   "local bad = deck.save()\n"
                   "bad.grid[40] = 7\n"
                   "bad.owned[7] = 1\n"
                   "ok(says('does not fit', deck.restore(bad)) and deck.read().spare[7] == nil, 'refused whole')\n"
                   "ok(deck.restore(saved) == true, 'restore')\n"
                   "ok(at(2) ~= nil and at(10) == nil and deck.read().spare[4] == nil, 'restored')\n"
                   "ok(not pcall(deck.restore, {}) and not pcall(deck.give, 2, 'x'), 'bad arguments')\n"
                   "print('deck', n)\n");
    fake_items[1] = 1; /* 16 cells open: rows 1-3 of page 1 and the first of row 4 */
    fake_items[2] = 1;
    fake_items[3] = 1;
    fake_deck[1] = 2;
    /* full but for the cells panel 10 and 5 take: everything else on page 1's open cells */
    for (int c = 0; c < 15; ++c) {
        if (c != 1 && c != 2 && c != 3) {
            fake_deck[c] = 9;
        }
    }
    fake_items[9] = 12;
    start();
    CHECK(log_has("test.loadout print scripts/main.lua:30 deck\t18"));
    CHECK(fake_deck[1] == 2 && fake_deck[2] == 0 && fake_items[4] == 0 && fake_items[10] == 0);
    end_case();
}

static void test_deck_busy(void)
{
    begin_case("deck_busy");
    mod_folder_api("Loadout", "test.loadout", "gameplay", 2,
                   "print('busy', select(2, deck.give(2)), deck.read() ~= nil)\n");
    fake_deck_busy = 1;
    start();
    CHECK(log_has("test.loadout print scripts/main.lua:1 busy\tnot now\ttrue"));
    CHECK(fake_items[2] == 0);
    end_case();
}

static void test_timers(void)
{
    int fired_at = 0, every = 0;
    begin_case("timers");
    mod_folder_api(
        "Clock", "test.clock", "gameplay", 2,
        "local n = 0\n"
        "timer.after(60, function() print('fired') end)\n"
        "local h\n"
        "h = timer.every(1, function() n = n + 1; if n == 3 then timer.cancel(h) end; print('tick', n) end)\n"
        "print('bad', pcall(timer.every, 0, print), pcall(timer.after, -1, print), select(2, "
        "timer.cancel(999)))\n");
    start();
    for (int second = 1; second <= 90; ++second) {
        fake_paused = second > 40 && second <= 60; /* 20 seconds paused */
        fake_vblanks += 60;
        frames(1);
        if (fired_at == 0 && log_has("fired")) {
            fired_at = second;
        }
    }
    every = log_has("tick\t3") && !log_has("tick\t4");
    CHECK(fired_at == 80);
    CHECK(every);
    CHECK(log_has("bad\tfalse\tfalse\tno such timer"));
    end_case();
}

static void test_draws_and_music(void)
{
    begin_case("draws_music");
    mod_folder_api("Clock", "test.clock", "gameplay", 2,
                   "local function ten() local t = {} for i = 1, 10 do t[i] = random(100) end return "
                   "table.concat(t, ' ') end\n"
                   "local seen = {}\n"
                   "for i = 1, 1000 do seen[random(3, 5)] = true end\n"
                   "print('range', seen[3], seen[4], seen[5], seen[2], seen[6], random(7, 7))\n"
                   "print('bad', pcall(random, 5, 4), pcall(random))\n"
                   "local first\n"
                   "mod.on('mission_start', function()\n"
                   "  local now = ten()\n"
                   "  if first == nil then first = now else print('same again', first == now) end\n"
                   "end)\n"
                   "print('track', select(2, music.play('Nope')), music.play('TwilightR_F'), music.stop(), "
                   "select(2, music.stop()))\n");
    start();
    khdays_scripts_field_begins();
    khdays_scripts_mission_start();
    khdays_scripts_field_begins();
    khdays_scripts_mission_start();
    frames(1);
    CHECK(log_has("range\ttrue\ttrue\ttrue\tnil\tnil\t7"));
    CHECK(log_has("bad\tfalse\tfalse"));
    CHECK(log_has("same again\ttrue"));
    CHECK(log_has("track\tunknown track\ttrue\ttrue\tnot playing"));
    CHECK(fake_music == -1 && !fake_music_holding);
    end_case();
}

static void test_share(void)
{
    begin_case("share");
    mod_folder_api("Clock", "test.clock", "gameplay", 2,
                   "mod.on('shared', function(e)\n"
                   "  print('heard', e.data.wave, e.data.names[2], e.data.done, e.data.deep.x.y)\n"
                   "end)\n"
                   "print('shared', mod.share({ wave = 3, names = { 'a', 'b' }, done = false, deep = { x = { y = 1.5 "
                   "} } }))\n"
                   "print('bad', pcall(mod.share, { f = print }))\n");
    mod_folder_api("Other", "test.other", "gameplay", 2, "mod.on('shared', function(e) print('not mine') end)\n");
    start();
    CHECK(log_has("test.clock print scripts/main.lua:4 shared\ttrue"));
    CHECK(log_has("bad\tfalse"));
    CHECK(fake_share_sent != NULL && strcmp((const char *)fake_share_sent, "test.clock") == 0);
    if (fake_share_sent != NULL) {
        fake_share_waiting = malloc(fake_share_sent_size); /* as a guest's copy gets it */
        memcpy(fake_share_waiting, fake_share_sent, fake_share_sent_size);
        fake_share_waiting_size = fake_share_sent_size;
    }
    frames(1);
    CHECK(log_has("heard\t3\tb\tfalse\t1.5"));
    CHECK(!log_has("not mine"));
    end_case();
}

static void test_share_guest(void)
{
    begin_case("share_guest");
    mod_folder_api("Clock", "test.clock", "gameplay", 2, "print('guest', select(2, mod.share({})))\n");
    fake_guest = 1;
    start();
    CHECK(log_has("guest\tguest"));
    CHECK(fake_share_sent == NULL);
    end_case();
}

static void test_set_position(void)
{
    begin_case("set_position");
    mod_folder_api(
        "Edge", "test.edge", "gameplay", 2,
        "print('title', select(2, player.set_position(1, 2, 3)))\n"
        "mod.on('scene', function(e)\n"
        "  if e.to == 2 then print('field', player.set_position(4, 0.5, -6), pcall(player.set_position, 1, 2)) "
        "end\n"
        "end)\n");
    start();
    frames(1);
    fake_scene = 2;
    frames(1);
    CHECK(log_has("test.edge print scripts/main.lua:1 title\tnot in the field"));
    CHECK(log_has("test.edge print scripts/main.lua:3 field\ttrue\tfalse"));
    CHECK(fake_set_at[0] == 4.0f && fake_set_at[1] == 0.5f && fake_set_at[2] == -6.0f);
    end_case();
}

static void test_hold_mission(void)
{
    begin_case("hold_mission");
    mod_folder_api("Arena", "test.arena", "gameplay", 2,
                   "print('hold', enemies.hold_mission(true), pcall(enemies.hold_mission, 'yes'))\n");
    mod_folder_api("Other", "test.other", "gameplay", 2, "print('other', select(2, enemies.hold_mission(true)))\n");
    start();
    CHECK(log_has("test.arena print scripts/main.lua:1 hold\ttrue\tfalse"));
    CHECK(log_has("test.other print scripts/main.lua:1 other\theld by test.arena"));
    CHECK(fake_held == 1);
    khdays_scripts_stop(); /* the holder stopped: the mission's own come back */
    CHECK(fake_held == 0);
    remove_all(root);
}

static void test_leave(void)
{
    begin_case("leave");
    mod_folder_api("Leaver", "test.leaver", "gameplay", 2,
                   "print('title', mission.leave())\n"
                   "mod.on('scene', function(e) if e.to == 2 then print('field', mission.leave()) end end)\n");
    mod_folder("Old", "test.old", "gameplay", "print('old', mission.leave == nil)\n");
    start();
    frames(1);
    fake_scene = 2;
    frames(1);
    CHECK(log_has("test.leaver print scripts/main.lua:1 title\tnil\tnot in the field"));
    CHECK(log_has("test.leaver print scripts/main.lua:2 field\ttrue") && fake_left == 1);
    CHECK(log_has("test.old print scripts/main.lua:1 old\ttrue"));
    end_case();
}

static void test_start(void)
{
    begin_case("start");
    mod_folder_api("Starter", "test.starter", "gameplay", 2,
                   "print('title', mission.start(10))\n"
                   "mod.on('scene', function(e)\n"
                   "  if e.to == 19 then print('camp', mission.start(4000), mission.start(10, {})) end\n"
                   "end)\n"
                   "print('bad', pcall(mission.start, 'x'), pcall(mission.start, 10, 5))\n");
    start();
    frames(1);
    fake_scene = 19;
    frames(1);
    CHECK(log_has("test.starter print scripts/main.lua:1 title\tnil\tnot in a camp"));
    CHECK(log_has("test.starter print scripts/main.lua:3 camp\tnil\ttrue") && fake_started == 10);
    CHECK(log_has("test.starter print scripts/main.lua:5 bad\tfalse"));
    end_case();
}

static void test_endings(void)
{
    int entity[3] = {0, 50, 1}; /* the player at 0 HP */
    begin_case("endings");
    mod_folder_api("Ends", "test.ends", "gameplay", 2,
                   "mod.on('mission_end', function(e) print('end', e.ending) end)\n");
    start();
    frames(1);
    khdays_scripts_field_begins();
    khdays_scripts_mission_start();
    khdays_scripts_hp(entity, 30); /* a fall, then the game over */
    fake_game_over = 1;
    frames(1);
    fake_game_over = 0;
    khdays_link_set_flag(KHDAYS_LINK_GIVEN_UP, 1);
    khdays_scripts_mission_end(15, -1, -1);
    CHECK(log_has("test.ends print scripts/main.lua:1 end\tlost"));
    khdays_scripts_field_begins(); /* Retry: the next run, given up from the pause through the same sub-flow */
    khdays_scripts_mission_start();
    frames(1);
    fake_game_over = 1;
    frames(1);
    fake_game_over = 0;
    khdays_scripts_mission_end(15, -1, -1);
    CHECK(log_has("test.ends print scripts/main.lua:1 end\tabandoned"));
    end_case();
}

static void test_screens(void)
{
    begin_case("screens");
    mod_folder_api(
        "Menu", "test.menu", "gameplay", 2,
        "local h\n"
        "title.entry{ label = { en = 'Endless', es = 'Sin fin' }, on_select = function()\n"
        "  h = screen.list{ title = 'Pick', entries = { 'One', { es = 'Dos' }, 'Three' },\n"
        "    on_choose = function(i) print('chose', i)\n"
        "      screen.result{ title = 'Done', rows = { { 'Waves', 3 }, { label = 'Time', value = '1:00' } },\n"
        "        on_close = function() print('result closed') end }\n"
        "    end,\n"
        "    on_cancel = function() print('cancelled') end }\n"
        "  print('second', screen.page{ title = 'x' })\n"
        "end }\n"
        "mod.on('frame', function(e)\n"
        "  if e.frame == 2 then print('late', title.entry{ label = 'Late' }) end\n"
        "end)\n");
    start();
    CHECK(khdays_screens_entry_count() == 1);
    frames(1);
    /* the title's prompt: Y chooses the one entry */
    fake_title_idle = 1;
    CHECK(khdays_scripts_buttons(0) == 0);
    CHECK(khdays_scripts_buttons(0x0800) == 0); /* Y: the prompt's, not the game's */
    frames(1);
    CHECK(log_has("test.menu print scripts/main.lua:12 late\tnil\tonly while loading"));
    CHECK(log_has("test.menu print scripts/main.lua:9 second\tnil\tanother screen shows"));
    CHECK(khdays_screens_shown() != 0);
    khdays_screens_lock();
    CHECK(khdays_screens_current()->kind == KHDAYS_SCREEN_LIST && khdays_screens_current()->row_count == 3 &&
          strcmp(khdays_screens_current()->rows[1].label, "Dos") == 0);
    khdays_screens_unlock();
    CHECK(khdays_scripts_buttons(0) == 0);
    CHECK(khdays_scripts_buttons(0x0080) == 0); /* DOWN */
    CHECK(khdays_scripts_buttons(0) == 0);
    CHECK(khdays_scripts_buttons(0x0001) == 0); /* A: chosen */
    CHECK(khdays_scripts_buttons(0x0001) == 0); /* still held: not the game's */
    frames(1);
    CHECK(log_has("test.menu print scripts/main.lua:4 chose\t2"));
    khdays_screens_lock();
    CHECK(khdays_screens_current()->kind == KHDAYS_SCREEN_RESULT &&
          strcmp(khdays_screens_current()->rows[0].value, "3") == 0 &&
          strcmp(khdays_screens_current()->rows[1].label, "Time") == 0);
    khdays_screens_unlock();
    CHECK(khdays_scripts_buttons(0) == 0);
    CHECK(khdays_scripts_buttons(0x0002) == 0); /* B: a result closes */
    frames(1);
    CHECK(log_has("result closed") && khdays_screens_shown() == 0);
    fake_title_idle = 0;
    CHECK(khdays_scripts_buttons(0x0001) == 0x0001); /* the game's again */
    end_case();
    CHECK(khdays_screens_entry_count() == 0); /* gone with the mods */
}

static void test_screens_stop(void)
{
    begin_case("screens_stop");
    mod_folder_api("Broken", "test.broken", "gameplay", 2,
                   "screen.page{ title = 'Hello', text = 'A page' }\n"
                   "mod.on('frame', function() error('on purpose') end)\n");
    start();
    CHECK(khdays_screens_shown() != 0);
    frames(1);
    CHECK(mod_by_folder("Broken")->state == KHDAYS_MOD_STOPPED);
    end_case(); /* its screen goes with its state */
    CHECK(khdays_screens_shown() == 0);
}

#define RUN(test) (fprintf(stderr, "%s\n", #test), test())

static void test_memory(void)
{
    static const unsigned char word[] = {0xef, 0xcd, 0xab, 0x89};
    begin_case("memory");
    put("mods/Poke/mod.ini", "id = test.poke\nname = Poke\nversion = 1.0\nkind = gameplay\napi = 2\nmemory = write\n");
    put("mods/Poke/scripts/main.lua",
        "local n = 0\n"
        "local function ok(c, what) if c then n = n + 1 else print('not ok', what) end end\n"
        "local function says(want, a, b) return a == nil and b == want end\n"
        "local R, out = 0x02000000, \"not the game's memory\"\n"
        "ok(memory.u32(R + 0x10) == 0x89abcdef and memory.s32(R + 0x10) == 0x89abcdef - 0x100000000, '32')\n"
        "ok(memory.u16(R + 0x10) == 0xcdef and memory.s16(R + 0x10) == 0xcdef - 0x10000, '16')\n"
        "ok(memory.u8(R + 0x13) == 0x89 and memory.s8(R + 0x13) == 0x89 - 0x100, '8')\n"
        "ok(memory.bytes(R + 0x10, 4) == '\\xef\\xcd\\xab\\x89' and memory.bytes(R, 0) == '', 'bytes')\n"
        "ok(says(out, memory.u32(R + 0xfffe)) and says(out, memory.u8(-1)) and says(out, memory.u8(R - 1)), 'out')\n"
        "ok(says(out, memory.bytes(R + 0xff00, 0x200)), 'bytes out')\n"
        "ok(memory.set_u16(R + 0x20, 0xbeef) == true and memory.set_u8(R + 0x22, -1) == true, 'set')\n"
        "ok(memory.set_u32(R + 0x24, -2) == true and memory.u32(R + 0x24) == 0xfffffffe, 'set negative')\n"
        "ok(memory.set_bytes(R + 0x30, 'hi') == true and memory.bytes(R + 0x30, 2) == 'hi', 'set_bytes')\n"
        "ok(says(out, memory.set_u32(R + 0xfffe, 1)) and says(out, memory.set_bytes(R - 1, 'x')), 'set out')\n"
        "ok(not pcall(memory.set_u8, R, 256) and not pcall(memory.set_u16, R, -32769), 'does not fit')\n"
        "ok(not pcall(memory.bytes, R, -1) and not pcall(memory.u8, 'x'), 'bad arguments')\n"
        "ok(memory.loaded(22) and not memory.loaded(23) and not memory.loaded(2^40), 'loaded')\n"
        "print('memory', n)\n");
    mod_folder_api("Look", "test.look", "gameplay", 2,
                   "print('look', memory.u8(0x02000010), select(2, memory.set_u8(0x02000010, 1)))\n");
    put("mods/Glance/mod.ini",
        "id = test.glance\nname = Glance\nversion = 1.0\nkind = visual\napi = 2\nmemory = write\n");
    put("mods/Glance/scripts/main.lua",
        "print('glance', memory.u8(0x02000011), select(2, memory.set_u8(0x02000011, 1)))\n");
    mod_folder("Old", "test.old", "gameplay", "print('old', memory == nil)\n");
    memcpy(fake_ram + 0x10, word, sizeof(word));
    start();
    CHECK(log_has("test.poke print scripts/main.lua:18 memory\t13"));
    CHECK(fake_ram[0x20] == 0xef && fake_ram[0x21] == 0xbe && fake_ram[0x22] == 0xff);
    CHECK(fake_ram[0x24] == 0xfe && fake_ram[0x25] == 0xff && fake_ram[0x27] == 0xff && fake_ram[0x30] == 'h');
    CHECK(log_has("test.look print scripts/main.lua:1 look\t239\tmod.ini has no memory = write"));
    CHECK(log_has("test.glance print scripts/main.lua:1 glance\t205\tvisual mod"));
    CHECK(log_has("test.old print scripts/main.lua:1 old\ttrue"));
    CHECK(fake_ram[0x10] == 0xef && fake_ram[0x11] == 0xcd);
    end_case();
}

static void test_memory_guest(void)
{
    begin_case("memory_guest");
    put("mods/Poke/mod.ini", "id = test.poke\nname = Poke\nversion = 1.0\nkind = gameplay\napi = 2\nmemory = write\n");
    put("mods/Poke/scripts/main.lua",
        "print('poke', memory.u8(0x02000000), select(2, memory.set_u8(0x02000000, 7)))\n");
    fake_guest = 1;
    start();
    CHECK(log_has("test.poke print scripts/main.lua:1 poke\t0\tguest"));
    CHECK(fake_ram[0] == 0);
    end_case();
}

int main(void)
{
    RUN(test_sha256);
    RUN(test_manifest);
    RUN(test_order);
    RUN(test_no_mods);
    RUN(test_sandbox);
    RUN(test_faults);
    RUN(test_order_file);
    RUN(test_events_and_state);
    RUN(test_player);
    RUN(test_options);
    RUN(test_menu_asks);
    RUN(test_content_mods);
    RUN(test_actions);
    RUN(test_files_and_notices);
    RUN(test_room_fingerprint);
    RUN(test_reload);
    RUN(test_guest);
    RUN(test_api_versions);
    RUN(test_enemies);
    RUN(test_enemies_guest);
    RUN(test_defeat);
    RUN(test_endings);
    RUN(test_leave);
    RUN(test_start);
    RUN(test_screens);
    RUN(test_screens_stop);
    RUN(test_deck);
    RUN(test_deck_busy);
    RUN(test_timers);
    RUN(test_draws_and_music);
    RUN(test_share);
    RUN(test_share_guest);
    RUN(test_set_position);
    RUN(test_hold_mission);
    RUN(test_memory);
    RUN(test_memory_guest);
    if (failures != 0) {
        fprintf(stderr, "script_test: %d failures\n", failures);
        return 1;
    }
    printf("script_test: all passed\n");
    return 0;
}
