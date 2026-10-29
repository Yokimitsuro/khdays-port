/* The game's side of the mods' interface (game_link.h), by the decompilation's names. */
#include "game_link.h"

#include "../core/language.h"
#include "../core/session_log.h"
#include "../core/window.h"
#include "../core/events.h"
#include "../../host/app/instance.h"
#include "../menus/autosave.h"
#include "../menus/camp.h"
#include "../menus/mission_start.h"
#include "../sound/sound_bridge.h"
#include "khdays_data_area.h"
#include "khdays_ds_symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int GameState_IsFlagSet(int flag);
extern int GameState_SetFlag(int flag);
extern void GameState_ClearFlag(int flag);
extern unsigned int Session_GetLocalPlayerIndex(void);
extern int GetEntryField20ByIndex(int idx);
extern void func_ov022_02088218(int player, KhdaysRef at);
extern int PlaySound(int sequence_archive, int entry);
extern KhdaysRef NNSi_FndAllocFromDefaultExpHeap(unsigned int size);
extern void NNSi_FndFreeFromDefaultHeap(KhdaysRef block);
extern void Ov002_ShowNoticeText(KhdaysRef text);
extern void Ov022_ActorSetHp(KhdaysRef pEnt, int nValue); /* the game's pointers are references (window.h) */
extern int Ov022_ComputeStrengthDamage(KhdaysRef pActor, int nScale);
extern void Ov002_PauseMissionScene(void);
extern int GetLanguage(void);
extern void Ov000_TickMenuLoop(void);
extern void GameState_SetField(unsigned int field, unsigned int bits, unsigned int value);
extern int func_ov022_020ad7b0(KhdaysRef actor);
extern int Ov022_ComputeChipGauge(KhdaysRef block);
extern int Ov022_GetGlobal34(void);
int khdays_overlay_loaded(int id); /* runtime/core/overlays.c */

#define TITLE_SCENE 1u
#define FIELD_SCENE 2u
#define STORY_CAMP_SCENE 5u
#define MISSION_CAMP_SCENE 19u
#define OV_BATTLE 22
#define OV_TITLE 0
#define OBJECT_UPDATE 0x14u /* an object's update (its next state) */
#define OV_FIELD 2
#define OV_MISSION_CAMP 8
#define OV_STORY_CAMP 25
#define CAPTIONS 0x0207f62cu /* data_ov002_0207f62c: [1] the field's captions (Ov002_ShowNoticeText) */
#define CAPTION_LINE 0xb8u   /* the notice held, 0 while none */
#define OBJ_FRAME 0x8u       /* gObjSystem: [2] the game's frame count */
#define SCENE_CURRENT 0x8u   /* gSceneCtl: current id, pending +0xc, argument +0x10 */
#define SCENE_ARGUMENT 0x10u
#define STATE_MUNNY 0xcu      /* GameState: nMunny (Ov008_CanConfirmShopRow) */
#define STATE_ITEMS 0x810u    /* GameState: aItemCount */
#define FLAG_ITEM_OWNED 0x4db /* Ov008_AddItemCount */
#define PLAYERS 0x020b2e7cu   /* *: the field's players, 0xc bytes each; +4 a record whose +0x20 is the actor */
#define BATTLE 0x020b2e60u    /* data_ov022_020b2e60: the battle's context, set as it begins (Ov022_BeginScene) */
#define ACTOR_POSITION 0x48cu
#define ACTOR_KIND 0xcu /* its character: its overlay's constant */
#define SEATS 4         /* the game's (data_020429c8) */
#define ENT_HP 0x12u    /* Ov022_ActorSetHp's entity: nHp, and nHpMax +0x16 */
#define ENT_HP_MAX 0x16u
#define OV_GAME_OVER 27
#define FIELD_ROOT 0x0207fa00u /* data_ov002_0207fa00: the field's root */
#define PANEL 0x0207f614u      /* data_ov002_0207f614: the field's panel, 0 until it is set up */
#define GAUGES 0x0207f618u     /* data_ov002_0207f618: its gauges (Ov002_SetGaugeValue's), 0 until they are made */
#define ROOT_STOPPING 0x8b64u  /* bits 0-1: the field stopping or stopped (Ov002_PauseMissionScene) */
#define GIVE_UP_FIELD 0x20efu  /* the game field the pause's Give Up sets as it confirms */
#define ENEMY_KIND 0x19cu

static unsigned rd32(KhdaysRef at)
{
    return *(const volatile unsigned *)KHDAYS_W(at);
}

void khdays_link_folders(const char **mods, const char **user)
{
    *mods = "mods";
    *user = khdays_host_user_folder();
}

unsigned khdays_link_frame(void)
{
    return rd32(KHDAYS_DS_gObjSystem + OBJ_FRAME);
}

int khdays_link_paused(void)
{
    /* main's loop: mode 1 runs the callbacks only, the objects stand still */
    return *(const volatile unsigned char *)KHDAYS_W(KHDAYS_DS_gPauseMode) == 1;
}

unsigned khdays_link_scene(unsigned *argument)
{
    if (argument != NULL) {
        *argument = rd32(KHDAYS_DS_gSceneCtl + SCENE_ARGUMENT);
    }
    return rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT);
}

int khdays_link_where(void)
{
    const unsigned scene = khdays_link_scene(NULL);
    if (scene == FIELD_SCENE && khdays_overlay_loaded(OV_FIELD) && khdays_overlay_loaded(OV_BATTLE)) {
        return KHDAYS_WHERE_FIELD;
    }
    if ((scene == MISSION_CAMP_SCENE && khdays_overlay_loaded(OV_MISSION_CAMP)) ||
        (scene == STORY_CAMP_SCENE && khdays_overlay_loaded(OV_STORY_CAMP))) {
        return KHDAYS_WHERE_CAMP;
    }
    return scene == TITLE_SCENE ? KHDAYS_WHERE_TITLE : KHDAYS_WHERE_ELSEWHERE;
}

unsigned khdays_link_state(void)
{
    const unsigned state = rd32(KHDAYS_DS_gGameState);
    return khdays_game_ram(state) ? state : 0;
}

int khdays_link_munny(void)
{
    const unsigned state = khdays_link_state();
    return state != 0 ? (int)rd32(state + STATE_MUNNY) : 0;
}

void khdays_link_set_munny(int value)
{
    const unsigned state = khdays_link_state();
    if (state != 0) {
        *(volatile unsigned *)KHDAYS_W(state + STATE_MUNNY) = (unsigned)value;
    }
}

int khdays_link_items(unsigned char *counts)
{
    const unsigned state = khdays_link_state();
    if (state == 0) {
        return 0;
    }
    for (int i = 0; i < KHDAYS_LINK_ITEMS; ++i) {
        counts[i] = *(const volatile unsigned char *)KHDAYS_W(state + STATE_ITEMS + (unsigned)i);
    }
    return 1;
}

void khdays_link_set_item(int id, int count)
{
    const unsigned state = khdays_link_state();
    if (state == 0) {
        return;
    }
    *(volatile unsigned char *)KHDAYS_W(state + STATE_ITEMS + (unsigned)id) = (unsigned char)count;
    if (count > 0) {
        GameState_SetFlag(FLAG_ITEM_OWNED + id);
    }
}

int khdays_link_flag(int n)
{
    return GameState_IsFlagSet(n) != 0;
}

void khdays_link_set_flag(int n, int on)
{
    if (on) {
        GameState_SetFlag(n);
    } else {
        GameState_ClearFlag(n);
    }
}

int khdays_link_mission(void)
{
    if (khdays_link_scene(NULL) != FIELD_SCENE) {
        return -1;
    }
    return *(const volatile unsigned short *)KHDAYS_W(KHDAYS_DS_data_0204c23c);
}

unsigned khdays_link_seat_actor(int seat)
{
    unsigned actor;
    if (seat < 0 || seat >= SEATS || !khdays_overlay_loaded(OV_BATTLE) || !khdays_game_ram(rd32(PLAYERS))) {
        return 0;
    }
    actor = (unsigned)GetEntryField20ByIndex(seat);
    return khdays_game_ram(actor) ? actor : 0;
}

int khdays_link_local_seat(void)
{
    return (int)Session_GetLocalPlayerIndex();
}

/* The local player's battle actor, 0 without one. */
static unsigned player_actor(void)
{
    return khdays_link_seat_actor(khdays_link_local_seat());
}

void khdays_link_actor_position(unsigned actor, float at[3])
{
    for (int i = 0; i < 3; ++i) {
        at[i] = (float)(int)rd32(actor + ACTOR_POSITION + 4u * (unsigned)i) / 4096.0f;
    }
}

int khdays_link_actor_kind(unsigned actor)
{
    return (int)rd32(actor + ACTOR_KIND);
}

int khdays_link_strength_damage(unsigned actor, int scale)
{
    return khdays_overlay_loaded(OV_BATTLE) && khdays_game_ram(actor) ? Ov022_ComputeStrengthDamage(actor, scale) : -1;
}

int khdays_link_position(float at[3])
{
    const unsigned actor = player_actor();
    if (actor == 0) {
        return 0;
    }
    khdays_link_actor_position(actor, at);
    return 1;
}

const char *khdays_link_set_position(const float at[3])
{
    return khdays_link_place_seat(khdays_link_local_seat(), at);
}

const char *khdays_link_place_seat(int seat, const float at[3])
{
    KhdaysRef vec;
    if (khdays_link_seat_actor(seat) == 0) {
        return "no player";
    }
    if (rd32(BATTLE) == 0) {
        return "not now"; /* the routine reads the battle's frame time: the battle has not begun */
    }
    vec = NNSi_FndAllocFromDefaultExpHeap(12);
    if (vec == 0) {
        return "not enough memory";
    }
    for (int i = 0; i < 3; ++i) {
        const double v = (double)at[i] * 4096.0;
        *(volatile int *)KHDAYS_W(vec + 4u * (unsigned)i) = (int)(v < -2147483647.0  ? -2147483647.0
                                                                  : v > 2147483647.0 ? 2147483647.0
                                                                                     : v);
    }
    func_ov022_02088218(seat, vec);
    NNSi_FndFreeFromDefaultHeap(vec);
    return NULL;
}

int khdays_link_player_hp(int *hp, int *max)
{
    const unsigned actor = player_actor();
    if (actor == 0) {
        return 0;
    }
    khdays_link_entity_hp(KHDAYS_W(actor), hp, max);
    return 1;
}

const char *khdays_link_set_player_hp(int hp)
{
    return khdays_link_set_actor_hp(player_actor(), hp);
}

const char *khdays_link_set_actor_hp(unsigned actor, int hp)
{
    if (actor == 0) {
        return "no player";
    }
    /* Ov022_ActorSetHp shows the HP on the player's gauge (Ov002_GetPanelWord0220), which the field's panel and
     * its gauges have only once they are set up: while the field loads the actor is there before them */
    if (!khdays_overlay_loaded(OV_FIELD) || !khdays_game_ram(rd32(PANEL)) || !khdays_game_ram(rd32(GAUGES))) {
        return "not now";
    }
    Ov022_ActorSetHp(actor, hp);
    return NULL;
}

int khdays_link_player_low_hp(void)
{
    const unsigned actor = player_actor();
    return actor != 0 && func_ov022_020ad7b0(actor);
}

int khdays_link_hud_portrait(int slot, int *icon, int *hp, int *maximum, int *low_hp)
{
    /* Ov002_OpenPanelScreen keeps the already mapped nId/nIcon pairs at +0x130.
     * Ov002_CreateAndRestoreHud puts the local player first even in multiplayer. */
    if (slot < 0 || slot >= 4 || !khdays_overlay_loaded(OV_FIELD) || !khdays_overlay_loaded(OV_BATTLE) ||
        khdays_link_scene(NULL) != FIELD_SCENE || !khdays_game_ram(rd32(PLAYERS))) {
        return 0;
    }
    const unsigned context = rd32(0x0207f614u);
    if (!khdays_game_ram(context)) {
        return 0;
    }
    const unsigned record = context + 0x130u + (unsigned)slot * 16u;
    const int member = (int)rd32(record), portrait = (int)rd32(record + 4u);
    if (member < 0 || member >= 4 || portrait < 0 || portrait >= 21) {
        return 0;
    }
    const unsigned actor = (unsigned)GetEntryField20ByIndex(member);
    if (!khdays_game_ram(actor)) {
        return 0;
    }
    khdays_link_entity_hp(KHDAYS_W(actor), hp, maximum);
    if (*maximum <= 0) {
        return 0;
    }
    *icon = portrait;
    *low_hp = func_ov022_020ad7b0(actor) != 0;
    return 1;
}

int khdays_link_player_limit_hp(void)
{
    const unsigned actor = player_actor();
    const unsigned block = actor + 0x22f8u; /* Ov022_UpdateSubsystems */
    if (!actor || !khdays_game_ram(rd32(block + 0x328u)))
        return 0;
    return Ov022_ComputeChipGauge(block);
}

int khdays_link_limit_available(void)
{
    if (!khdays_overlay_loaded(OV_FIELD) || khdays_link_scene(NULL) != FIELD_SCENE)
        return 0;
    const unsigned context = rd32(0x0207f618u);
    /* Ov022_UpdateSubsystems -> Ov002_SetPanelMode_2 arms LIMIT's tag 0x1a and stores +0x2c. */
    return khdays_game_ram(context) && khdays_game_ram(context + 0x2fu) && rd32(context + 0x2cu) != 0;
}

int khdays_link_mission_target(int *entry, int *primary)
{
    if (!khdays_overlay_loaded(OV_FIELD) || khdays_link_scene(NULL) != FIELD_SCENE)
        return 0;
    const unsigned context = rd32(0x0207f630u); /* Ov002_QueuePanelGraphics / Ov002_Field_GetWordAC */
    if (!khdays_game_ram(context) || !khdays_game_ram(context + 0xafu))
        return 0;
    const int selected = (int)rd32(context + 0xacu);
    if (selected < 0 || selected > 0x3d)
        return 0;
    *primary = selected == 0;
    *entry = selected == 0 ? 11 : selected + 0x2f;
    return 1;
}

int khdays_link_player_limit_clock(struct KhdaysLinkLimitClock *clock)
{
    const unsigned actor = player_actor();
    const unsigned block = actor + 0x22f8u;
    if (!actor || !khdays_game_ram(rd32(block + 0x328u)))
        return 0;
    clock->elapsed = (int)rd32(block + 0x32cu);
    clock->outward = (int)rd32(block + 0x338u);
    clock->start_hp = *(const volatile unsigned short *)KHDAYS_W(block + 0x336u);
    clock->step = Ov022_GetGlobal34();
    return 1;
}

void khdays_link_entity_hp(const void *entity, int *hp, int *max)
{
    const unsigned char *e = entity;
    *hp = *(const unsigned short *)(e + ENT_HP);
    *max = *(const unsigned short *)(e + ENT_HP_MAX);
}

int khdays_link_is_player(const void *entity)
{
    const unsigned actor = player_actor();
    return actor != 0 && KHDAYS_REF(entity) == actor;
}

static unsigned hit_source; /* the hit being dealt's source, 0 between hits */

void khdays_link_hit_from(void *source)
{
    hit_source = source != NULL ? KHDAYS_REF(source) : 0;
}

int khdays_link_attacker(unsigned *actor)
{
    *actor = 0;
    /* an enemy is an ov107 actor: its first halfword's 0x40 files it among a room's enemies
     * (Ov107_Region_AddMember), +0x19c its kind (Ov107_Spawner_FindFreeActor) */
    if (hit_source == 0 || !khdays_game_ram(hit_source) ||
        !(*(const volatile unsigned short *)KHDAYS_W(hit_source) & 0x40u)) {
        return -1;
    }
    *actor = hit_source;
    return *(const volatile unsigned char *)KHDAYS_W(hit_source + ENEMY_KIND);
}

int khdays_link_language(void)
{
    return khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
}

int khdays_link_title_idle(void)
{
    const unsigned object = rd32(KHDAYS_DS_gSceneCtl); /* the scene's task */
    return khdays_link_scene(NULL) == TITLE_SCENE && khdays_overlay_loaded(OV_TITLE) && khdays_game_ram(object) &&
           khdays_function_lookup(rd32(object + OBJECT_UPDATE)) == (khdays_function_pointer)Ov000_TickMenuLoop;
}

int khdays_link_game_over(void)
{
    return khdays_overlay_loaded(OV_GAME_OVER);
}

int khdays_link_results_cleared(void)
{
    return khdays_autosave_results_cleared();
}

const char *khdays_link_start_mission(int number)
{
    return khdays_mission_start_request(number);
}

const char *khdays_link_leave_mission(void)
{
    const unsigned root = rd32(FIELD_ROOT);
    if (khdays_link_scene(NULL) != FIELD_SCENE || !khdays_overlay_loaded(OV_FIELD) || !khdays_game_ram(root)) {
        return "not in the field";
    }
    if (khdays_link_where() != KHDAYS_WHERE_FIELD) {
        return "not now"; /* the field's opening, before its battle is loaded */
    }
    if (khdays_link_paused() || khdays_link_menu_open(KHDAYS_LINK_PAUSE) || (rd32(root + ROOT_STOPPING) & 3u)) {
        return "not now";
    }
    Ov002_PauseMissionScene();
    if (!(rd32(root + ROOT_STOPPING) & 1u)) {
        return "not now"; /* the field is held by something else (an event, a talk) */
    }
    GameState_SetField(GIVE_UP_FIELD, 1, 0);
    return NULL;
}

static int in_field(void)
{
    return khdays_link_scene(NULL) == FIELD_SCENE && khdays_overlay_loaded(OV_FIELD);
}

const char *khdays_link_play_sound(const char *sequence_archive, const char *entry)
{
    const KhdaysSoundArchive *archive = khdays_sound_bridge_archive();
    int number, index;
    if (archive == NULL || !khdays_sound_archive_find_entry(archive, sequence_archive, entry, &number, &index)) {
        return "unknown sound";
    }
    PlaySound(number, index);
    return NULL;
}

const char *khdays_link_show_text(const unsigned short *text, int length)
{
    unsigned captions;
    KhdaysRef block;
    if (!in_field()) {
        return "not in the field";
    }
    captions = rd32(CAPTIONS + 4u);
    if (!khdays_game_ram(captions)) {
        return "not in the field";
    }
    if (rd32(captions + CAPTION_LINE) != 0) {
        return "busy";
    }
    block = NNSi_FndAllocFromDefaultExpHeap((unsigned)(length + 1) * 2u);
    if (!khdays_game_ram(block)) {
        return "no room in the game's heap";
    }
    for (int i = 0; i < length; ++i) {
        ((volatile unsigned short *)KHDAYS_W(block))[i] = text[i];
    }
    ((volatile unsigned short *)KHDAYS_W(block))[length] = 0;
    Ov002_ShowNoticeText(block); /* it keeps a copy of its own */
    NNSi_FndFreeFromDefaultHeap(block);
    return NULL;
}

int khdays_link_menu_open(int menu)
{
    if (menu == KHDAYS_LINK_PAUSE) {
        return khdays_link_scene(NULL) == FIELD_SCENE &&
               *(const volatile unsigned char *)KHDAYS_W(KHDAYS_DS_gPauseMode) != 0;
    }
    {
        int story;
        return khdays_camp_screen(&story) != KHDAYS_CAMP_NONE;
    }
}

int khdays_link_guest(void)
{
    /* the game's own test for the host: player 0 (Ov022_ActorSetHp, Ov022_ApplyDamageAndFlagHit) */
    return Session_GetLocalPlayerIndex() != 0;
}

int khdays_link_reachable(unsigned address, unsigned long long size)
{
    static const unsigned memories[][2] = {{0x02000000u, 0x02400000u}, /* main RAM */
                                           {KHDAYS_HEADERS_BASE, KHDAYS_HEADERS_END},
                                           {KHDAYS_DATA_AREA, KHDAYS_DATA_AREA_END},
                                           {KHDAYS_HEAPS_BASE, KHDAYS_HEAPS_END}};
    const unsigned long long end = address + size;
    for (size_t i = 0; i < sizeof(memories) / sizeof(memories[0]); ++i) {
        if (address >= memories[i][0] && end <= memories[i][1]) {
            return 1;
        }
    }
    return 0;
}

int khdays_link_read(unsigned address, void *out, size_t size)
{
    unsigned char *to = out;
    if (!khdays_link_reachable(address, size)) {
        return 0;
    }
    while (size > 0) {
        size_t n = size;
        int untouched = 0;
        if (address >= KHDAYS_HEAPS_BASE) { /* a granule at a time */
            unsigned rest;
            untouched = !khdays_heaps_touched(address, &rest);
            n = rest < size ? rest : size;
        }
        if (untouched) {
            memset(to, 0, n);
        } else {
            memcpy(to, KHDAYS_W(address), n);
        }
        to += n;
        address += (unsigned)n;
        size -= n;
    }
    return 1;
}

int khdays_link_write(unsigned address, const void *in, size_t size)
{
    if (!khdays_link_reachable(address, size)) {
        return 0;
    }
    memcpy(KHDAYS_W(address), in, size);
    return 1;
}

int khdays_link_overlay_loaded(int id)
{
    return khdays_overlay_loaded(id);
}
