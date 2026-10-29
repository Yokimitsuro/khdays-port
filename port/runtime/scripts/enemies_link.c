/* The field's enemies, the game's side (enemies_link.h). An area makes its enemies idle from the
 * mission's enemy table (Ov002_CreateActorFromMarker) and its spawners set them going as the player comes
 * near (Ov107_Spawner_Run op 0). A mod's enemy is made the same way and set going at the mod's point;
 * once idle again, the next one of its kind reuses it. docs/RUNTIME_MAP.md, "The field's enemies". */
#include "enemies_link.h"

#include "../battle/stop_magic.h"
#include "../plugins/actors.h"

#include "api_v2.h"
#include "../core/window.h"

#include "khdays_ds_symbols.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Ov002_CreateActorFromMarker(int nSourceIndex, int nRowIndex);
extern int Ov002_GetEffectiveHeight(KhdaysRef row);
extern int Ov002_GetStateWord(void);
extern void Ov107_AiState_LoadResistances(KhdaysRef actor, unsigned profile);
extern int Ov107_MoveNodeAndRelayout(KhdaysRef node, KhdaysRef at);
extern void Ov107_SetStatusAndEmit(KhdaysRef node, unsigned char status);
extern int Ov107_GetActorManager(void);
extern int Ov107_AiState_ApplyHit(KhdaysRef obj, KhdaysRef source, KhdaysRef packet);
extern void Ov107_Scene_BuildIdTable(void);
extern void FreeInstanceMemory(KhdaysRef p);
extern int Session_IsActive(void);
extern KhdaysRef NNSi_FndAllocFromDefaultExpHeap(unsigned int size);
extern void NNSi_FndFreeFromDefaultHeap(KhdaysRef block);
int khdays_overlay_loaded(int id); /* runtime/core/overlays.c */
int khdays_overlay_size(int id);
unsigned khdays_link_frame(void);      /* game_link.c */
const char *khdays_enemy_name(int id); /* enemy_names.c */

#define OV_FIELD 2
#define OV_BATTLE 22
#define OV_ENEMIES 107
#define FIELD_SCENE 2u
#define SCENE_CURRENT 0x8u

/* data_ov002_0207fa14: the field's objects */
#define OBJECTS 0x0207fa14u
#define OBJECTS_TABLE 0x4u        /* the mission's enemy table: u8 rows, u8 groups, ..; +4 the rows (16 bytes each) */
#define OBJECTS_SOURCES 0x1cu     /* one per room, 8 */
#define OBJECTS_PLACES 0x4cu      /* the rows the area placed: {s8 row, u8 handle} */
#define OBJECTS_PLACE_COUNT 0x51u /* u8 */
#define SOURCES 8
#define ROW 16u
#define ROW_BOOST 0x2u
#define ROW_PROFILE 0x3u
#define ROW_PARAM 0x5u
/* bit 0 shown: as a source is shown or hidden it tells each of its enemies (Ov107_Region_SetVisible,
 * Ov107_BroadcastValueToChildren: their AI_SHOW); bit 1 enabled (Ov107_Region_SetEnabled) */
#define SOURCE_FLAGS 0x40u
#define STATE_FLAT 2000 /* Ov002_GetStateWord: enemies made without their level */

/* A spawner (Ov107_Spawner_Run): its slots, an enemy each -- its row in the spawners' table of kinds and how it
 * enters (its first action; -1 the enemy's own) -- and that table (Ov107_Spawner_FindFreeActor: 4 bytes a row, +1 the
 * kind) */
#define SPAWNER_OWNER 0x4u
#define OWNER_TABLE 0x4u
#define SPAWNER_COUNT 0x4cu
#define SPAWNER_SLOTS 0x50u
#define SPAWNER_SLOT_BYTES 12u
#define SLOT_ROW 0u
#define SLOT_ENTRY 3u
#define TABLE_KINDS 0x94u
#define TABLE_ROW_BYTES 4u
#define TABLE_ROW_KIND 1u
#define KIND_VALUES 256 /* an enemy's kind is a byte */

/* An enemy (ov107's AI state) */
#define AI_SHOW 0x18u     /* shown or hidden by its source (Ov107_AiState_SetVisible): its model drawn or not */
#define AI_FLAGS 0x60u    /* 0x100 standing: set going (event 0), cleared at the defeat and once away */
#define AI_POSITION 0xb0u /* 20.12 fixed point */
#define AI_KIND 0x19cu
#define AI_LEVEL 0x19du /* the stats' record (Ov107_AiState_LoadStats) */
#define AI_BOOST 0x19fu
#define AI_POOLED 0x1acu /* bit 7 */
#define AI_FLAGS_1AE 0x1aeu
#define AI_FLAGS_1C5 0x1c5u
#define AI_ENTRY 0x1c9u /* its first action once active (a spawner slot's entry; 1 when made) */
#define AI_EVENT 0x1ccu /* 0 set going, 1 sent away, 2 settled once away */
#define AI_RETIRE 0x1e8u
#define AI_LOAD_STATS 0x1ecu
#define AI_ACTIVATE 0x1f0u
#define AI_PLACE 0x1f4u  /* the home and its reach (Ov107_StoreHitInfo) */
#define AI_HP_MAX 0x218u /* s16 */
#define AI_HP 0x21au     /* s16 */
#define AI_ATTACKER 0x25cu
#define AI_ATTACKS 0x290u /* s16 each 6 bytes, 8 of them (Ov107_StoreSlotEvent) */
#define AI_BUSY 0x2e8u
#define AI_SPAWNER 0x2d4u
#define ATTACKS 8
#define REACH (16 * 4096) /* the mission's spawners' (Ov002_ScriptCreateSlotObject's 9th) */
#define MANAGER_IDS 0x48u /* gOv107ActorManager: an actor by its id (+2), made by Ov107_Scene_BuildIdTable */
/* A hit as the players' attacks give it (Ov022_SendSweepHit's packet, Ov107_AiState_ApplyHit): u16 flags (4 a
 * physical hit, 0x80 no flinch), u16 its kinds (0x4000 a physical hit; the statuses' bits 0x80 burn, 0x8 freeze, 0x800
 * jolt, 0x4 toss), its place (20.12), s16 its damage, s16 an extra, u16 its author's seat, its part node (none: the
 * damage taken as it is, Ov107_CalcHitDamage), s8 the statuses' chance (0-100), then what the routine answers. */
#define HIT_BYTES 0x2cu
#define HIT_FLAGS 0x0u
#define HIT_KINDS 0x2u
#define HIT_PLACE 0x4u
#define HIT_DAMAGE 0x10u
#define HIT_AUTHOR 0x14u
#define HIT_CHANCE 0x1cu
#define HIT_PHYSICAL 0x4u
#define HIT_NO_FLINCH 0x80u
#define KIND_PHYSICAL 0x4000u
#define PLAYER_SUB 0x4ecu /* a player actor's part its hits come from (Ov022_SendSweepHit's source) */
#define MANAGER_LIST 0x4u /* gOv107ActorManager: its actors' list (List_First): +4 its first node, +0x10 its end */
#define LIST_FIRST 0x4u
#define LIST_END 0x10u
#define NODE_NEXT 0x4u
#define NODE_ITEM 0xcu /* the element, whose first word is the actor (Ov107_Scene_BuildIdTable) */
#define STANDING 0x100u
#define LEVEL_MAX 0x78 /* Ov002_GetEffectiveHeight */
/* The enemies' overlays (Ov107_SpawnEntityClass, Ov107_LoadEnemyOverlay): six places of 0x1e00 bytes;
 * each kind has a build of its overlay for some of them (ov107's table, 0xff none), overlay 114 + the
 * build, which takes its size / 0x1e00 + 1 places. A kind's overlay is loaded as its first enemy is
 * made, at the first place from the next free one it has a build for. */
#define KIND_BUILDS 0x020cb6a4u /* data_ov107_020cb6a4: six bytes a kind */
#define KIND_PLACE 0x020cbb20u  /* data_ov107_020cbb20: a kind's place + 1, 0 not loaded */
#define NEXT_PLACE 0x020cbb04u  /* data_ov107_020cbb04: the next free place (int) */
#define KINDS (708 / 6)
#define PLACES 6
#define PLACE_SIZE 0x1e00
#define FIRST_BUILD 114 /* FS_OVERLAY_ID_ov113 + 1 */

typedef void (*PlaceFn)(KhdaysRef self, KhdaysRef at, int reach);
typedef void (*ShowFn)(KhdaysRef self, int shown);
typedef int (*EventFn)(KhdaysRef self, int event);
typedef void (*StatsFn)(KhdaysRef self, int level);
typedef void (*ActivateFn)(KhdaysRef self, int param);

/* An actor made for the mods in this area. */
typedef struct Pooled {
    unsigned actor;
    int kind;
    short base_max, base_attacks[ATTACKS]; /* its stats at its level, before the mod's scaling */
    int leaving;                           /* sent away, until it settles */
} Pooled;

static Pooled *pool;
static int pool_count, pool_room;
/* How the area's spawners let each kind enter (a slot's entry + 1; 0 none seen), learnt as they run: the made actor's
 * own is the game's first one (a Shadow rises from a flat shape in a second), the area's may differ (mission 94's
 * Shadows come standing). */
static unsigned char entry_of[KIND_VALUES];
static unsigned made; /* the actor Ov002_CreateActorFromMarker made last (its join) */

static int tracing = -1;

static int traced(void)
{
    if (tracing < 0) {
        const char *v = getenv("KHDAYS_TRACE_ENEMIES");
        tracing = v != NULL && *v != '\0' && *v != '0';
    }
    return tracing;
}

static unsigned rd32(KhdaysRef at)
{
    return khdays_game_ram(at) ? *(const unsigned *)KHDAYS_W(at) : 0;
}

static unsigned rd16(KhdaysRef at)
{
    return khdays_game_ram(at) ? *(const unsigned short *)KHDAYS_W(at) : 0;
}

static unsigned rd8(KhdaysRef at)
{
    return khdays_game_ram(at) ? *(const unsigned char *)KHDAYS_W(at) : 0;
}

static void wr32(KhdaysRef at, unsigned v)
{
    *(unsigned *)KHDAYS_W(at) = v;
}

static void wr16(KhdaysRef at, unsigned v)
{
    *(unsigned short *)KHDAYS_W(at) = (unsigned short)v;
}

static void wr8(KhdaysRef at, unsigned v)
{
    *(unsigned char *)KHDAYS_W(at) = (unsigned char)v;
}

static unsigned ref_of(const void *p)
{
    return p != NULL ? (unsigned)KHDAYS_REF(p) : 0;
}

static int clamp(long long v, int low, int high)
{
    return v < low ? low : v > high ? high : (int)v;
}

/* The field's objects while the field runs with its enemies loaded; 0 otherwise. */
static unsigned objects(void)
{
    if (rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) != FIELD_SCENE || !khdays_overlay_loaded(OV_FIELD) ||
        !khdays_overlay_loaded(OV_BATTLE) || !khdays_overlay_loaded(OV_ENEMIES)) {
        return 0;
    }
    return rd32(OBJECTS);
}

static unsigned row_at(unsigned objs, int row)
{
    return rd32(rd32(objs + OBJECTS_TABLE) + 4u) + (unsigned)row * ROW;
}

/* The row of `kind` the area placed, -1 without. */
static int placed_row(unsigned objs, int kind)
{
    const unsigned places = rd32(objs + OBJECTS_PLACES);
    const unsigned count = rd8(objs + OBJECTS_PLACE_COUNT);
    for (unsigned i = 0; i < count; ++i) {
        const int row = (signed char)rd8(places + 2u * i);
        if (row >= 0 && (int)rd8(row_at(objs, row)) == kind) {
            return row;
        }
    }
    return -1;
}

int khdays_enemies_kinds(int *out, int room)
{
    const unsigned objs = objects();
    int n = 0;
    if (objs == 0 || rd32(objs + OBJECTS_SOURCES) == 0) {
        return 0;
    }
    for (unsigned i = 0, count = rd8(objs + OBJECTS_PLACE_COUNT); i < count; ++i) {
        const int row = (signed char)rd8(rd32(objs + OBJECTS_PLACES) + 2u * i);
        const int kind = row >= 0 ? (int)rd8(row_at(objs, row)) : -1;
        int seen = kind < 0;
        for (unsigned j = 0; j < i && !seen; ++j) {
            const int other = (signed char)rd8(rd32(objs + OBJECTS_PLACES) + 2u * j);
            seen = other >= 0 && (int)rd8(row_at(objs, other)) == kind;
        }
        if (!seen) {
            if (out != NULL && n < room) {
                out[n] = kind;
            }
            ++n;
        }
    }
    return n;
}

const char *khdays_enemies_name(int kind)
{
    return khdays_enemy_name(kind);
}

int khdays_enemies_kind_count(void)
{
    return KINDS;
}

/* Whether `kind`'s overlay is loaded, or would still fit. */
static int overlay_fits(int kind)
{
    if (rd8(KIND_PLACE + (unsigned)kind) != 0) {
        return 1;
    }
    for (int place = (int)rd32(NEXT_PLACE); place >= 0 && place < PLACES; ++place) {
        const unsigned build = rd8(KIND_BUILDS + 6u * (unsigned)kind + (unsigned)place);
        if (build != 0xff) {
            const int size = khdays_overlay_size(FIRST_BUILD + (int)build);
            return size >= 0 && place + size / PLACE_SIZE + 1 <= PLACES;
        }
    }
    return 0;
}

const char *khdays_enemies_can_make(int kind)
{
    const unsigned objs = objects();
    if (objs == 0) {
        return "not in the field";
    }
    if (kind < 0 || kind >= KINDS) {
        return "unknown kind";
    }
    if (rd8(objs + OBJECTS_PLACE_COUNT) == 0) {
        return "no enemies here";
    }
    return placed_row(objs, kind) >= 0 || overlay_fits(kind) ? NULL : "no room for its kind";
}

/* The source a mod's enemy joins: the first room's that is shown, else the first. */
static int source_index(unsigned objs)
{
    int first = -1;
    for (int i = 0; i < SOURCES; ++i) {
        const unsigned source = rd32(objs + OBJECTS_SOURCES + 4u * (unsigned)i);
        if (source == 0) {
            continue;
        }
        if (rd32(source + SOURCE_FLAGS) & 2u) {
            return i;
        }
        if (first < 0) {
            first = i;
        }
    }
    return first;
}

/* Ov107_Spawner_FindFreeActor's own test of an idle actor. */
static int idle(unsigned actor)
{
    const unsigned flags = rd16(actor + AI_FLAGS) & 0xffu;
    return !(flags & 1u) && (flags & 0x80u) && (rd16(actor + AI_POOLED) & 0x80u) && (int)rd32(actor + AI_BUSY) <= 0 &&
           rd32(actor + AI_SPAWNER) == 0;
}

static Pooled *pooled(unsigned actor)
{
    for (int i = 0; i < pool_count; ++i) {
        if (pool[i].actor == actor) {
            return &pool[i];
        }
    }
    return NULL;
}

static Pooled *add_pooled(unsigned actor, int kind)
{
    if (pool_count == pool_room) {
        const int room = pool_room ? pool_room * 2 : 16;
        Pooled *grown = realloc(pool, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        pool = grown;
        pool_room = room;
    }
    memset(&pool[pool_count], 0, sizeof(*pool));
    pool[pool_count].actor = actor;
    pool[pool_count].kind = kind;
    return &pool[pool_count++];
}

static void keep_base(Pooled *p)
{
    p->base_max = (short)rd16(p->actor + AI_HP_MAX);
    for (int i = 0; i < ATTACKS; ++i) {
        p->base_attacks[i] = (short)rd16(p->actor + AI_ATTACKS + 6u * (unsigned)i);
    }
}

/* The row's level for the actor again, as Ov002_CreateActorFromMarker gives it (Ov107_AiState_LoadStats
 * resets the boost and the resistances, which the row then sets). */
static void set_level(unsigned actor, unsigned row, int level)
{
    const unsigned boost = rd8(row + ROW_BOOST);
    const int param = (signed char)rd8(row + ROW_PARAM);
    const unsigned stats = rd32(actor + AI_LOAD_STATS), activate = rd32(actor + AI_ACTIVATE);
    if (stats != 0) {
        ((StatsFn)KHDAYS_FN(stats))(actor, level);
    }
    if (param > 0 && activate != 0) {
        ((ActivateFn)KHDAYS_FN(activate))(actor, param);
    }
    if (boost != 0) {
        wr8(actor + AI_BOOST, (unsigned)clamp((long long)boost + rd8(actor + AI_BOOST), 0, 255));
    }
    Ov107_AiState_LoadResistances(actor, rd8(row + ROW_PROFILE));
}

static void scale(const Pooled *p, double hp, double strength)
{
    const int max = clamp(llround(p->base_max * hp), 1, 0x7fff);
    wr16(p->actor + AI_HP_MAX, (unsigned)max);
    wr16(p->actor + AI_HP, (unsigned)max);
    for (int i = 0; i < ATTACKS; ++i) {
        wr16(p->actor + AI_ATTACKS + 6u * (unsigned)i,
             (unsigned)clamp(llround(p->base_attacks[i] * strength), 0, 0x7fff));
    }
}

/* Ov107_Spawner_Run op 0 for one actor, without a spawner: its spawner word holds its source instead,
 * so no spawner of the mission takes it while it stands (Ov107_AiState_OnDefeat clears it). */
static int set_going(unsigned actor, unsigned source, const float at[3])
{
    const KhdaysRef place = NNSi_FndAllocFromDefaultExpHeap(12);
    const unsigned place_fn = rd32(actor + AI_PLACE), event_fn = rd32(actor + AI_EVENT);
    if (place == 0 || event_fn == 0) {
        if (place != 0) {
            NNSi_FndFreeFromDefaultHeap(place);
        }
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        wr32(place + 4u * (unsigned)i, (unsigned)(int)lroundf(at[i] * 4096.0f));
    }
    wr16(actor + AI_HP, rd16(actor + AI_HP_MAX));
    wr8(actor + AI_FLAGS_1C5, rd8(actor + AI_FLAGS_1C5) & ~0x17u);
    wr32(actor + AI_RETIRE, 0);
    wr32(actor + AI_SPAWNER, source);
    if (entry_of[rd8(actor + AI_KIND)] != 0) {
        wr8(actor + AI_ENTRY, entry_of[rd8(actor + AI_KIND)] - 1u);
    }
    Ov107_SetStatusAndEmit(actor, 0);
    wr16(actor + AI_FLAGS_1AE, rd16(actor + AI_FLAGS_1AE) & ~0x100u);
    if (place_fn != 0) {
        ((PlaceFn)KHDAYS_FN(place_fn))(actor, place, REACH);
    }
    Ov107_MoveNodeAndRelayout(actor, place);
    ((EventFn)KHDAYS_FN(event_fn))(actor, 0);
    NNSi_FndFreeFromDefaultHeap(place);
    /* the source showed its enemies before this one was made, which would stand undrawn (hit, targeted, unseen) */
    if (rd32(actor + AI_SHOW) != 0) {
        ((ShowFn)KHDAYS_FN(rd32(actor + AI_SHOW)))(actor, (int)(rd32(source + SOURCE_FLAGS) & 1u));
    }
    return 1;
}

/* The area's script makes the table of its actors by id once it made them all; a message to an actor
 * made after (its id past the table) would read beyond it, so the table is made again. */
static void ids_again(void)
{
    const unsigned manager = (unsigned)Ov107_GetActorManager();
    const unsigned old = manager != 0 ? rd32(manager + MANAGER_IDS) : 0;
    if (manager == 0) {
        return;
    }
    Ov107_Scene_BuildIdTable();
    if (old != 0) {
        FreeInstanceMemory(old);
    }
}

static void trace_actor(const char *what, unsigned actor)
{
    fprintf(stderr, "enemies: f%u %s %08x kind 0x%02x level %u HP %d/%d flags %04x entry %d at %d %d %d attacks",
            khdays_link_frame(), what, actor, rd8(actor + AI_KIND), rd8(actor + AI_LEVEL), (short)rd16(actor + AI_HP),
            (short)rd16(actor + AI_HP_MAX), rd16(actor + AI_FLAGS), (signed char)rd8(actor + AI_ENTRY),
            (int)rd32(actor + AI_POSITION), (int)rd32(actor + AI_POSITION + 4), (int)rd32(actor + AI_POSITION + 8));
    for (int i = 0; i < ATTACKS; ++i) {
        fprintf(stderr, " %d", (short)rd16(actor + AI_ATTACKS + 6u * (unsigned)i));
    }
    fprintf(stderr, "\n");
}

/* An enemy of `kind` made from row `row` in source `source`: an idle one of the mod's reused, else made. */
static unsigned make_from(unsigned objs, int source, int row, int kind, const float at[3], int level, double hp,
                          double strength, const char **why)
{
    const unsigned row_ref = row_at(objs, row);
    unsigned actor;
    Pooled *p = NULL;
    for (int i = 0; i < pool_count && p == NULL; ++i) {
        if (pool[i].kind == kind && !pool[i].leaving && idle(pool[i].actor)) {
            p = &pool[i];
        }
    }
    if (p != NULL) {
        actor = p->actor;
        if (Ov002_GetStateWord() != STATE_FLAT) {
            set_level(actor, row_ref, clamp((long long)Ov002_GetEffectiveHeight(row_ref) + level, 1, LEVEL_MAX));
            keep_base(p);
        }
    } else {
        made = 0;
        Ov002_CreateActorFromMarker(source, row);
        actor = made;
        if (actor != 0) {
            ids_again();
        }
        if (actor == 0 || (p = add_pooled(actor, kind)) == NULL) {
            *why = actor == 0 ? "not now" : "not enough memory";
            return 0;
        }
        if (level != 0 && Ov002_GetStateWord() != STATE_FLAT) {
            set_level(actor, row_ref, clamp((long long)Ov002_GetEffectiveHeight(row_ref) + level, 1, LEVEL_MAX));
        }
        keep_base(p);
    }
    scale(p, hp, strength);
    if (!set_going(actor, rd32(objs + OBJECTS_SOURCES + 4u * (unsigned)source), at)) {
        *why = "not enough memory";
        return 0;
    }
    if (traced()) {
        trace_actor("made for a mod", actor);
    }
    return actor;
}

unsigned khdays_enemies_make(int kind, const float at[3], int level, double hp, double strength, const char **why)
{
    const unsigned objs = objects();
    int row, source;
    unsigned actor, borrowed = 0;
    unsigned char kept_row[ROW];
    *why = NULL;
    if (objs == 0) {
        *why = "not in the field";
        return 0;
    }
    if (Session_IsActive()) {
        *why = "not in multiplayer yet";
        return 0;
    }
    source = source_index(objs);
    if (source < 0) {
        *why = "no enemies here";
        return 0;
    }
    row = placed_row(objs, kind);
    if (row < 0) {
        /* a kind the area does not make: from a row of the area's, as that kind with the mission's level
         * and nothing of its own, put back once the enemy is made (an enemy keeps nothing of its row) */
        *why = khdays_enemies_can_make(kind);
        if (*why != NULL) {
            return 0;
        }
        row = (signed char)rd8(rd32(objs + OBJECTS_PLACES));
        borrowed = row_at(objs, row);
        for (unsigned i = 0; i < ROW; ++i) {
            kept_row[i] = (unsigned char)rd8(borrowed + i);
            wr8(borrowed + i, i == 0 ? (unsigned)kind : 0);
        }
    }
    actor = make_from(objs, source, row, kind, at, level, hp, strength, why);
    for (unsigned i = 0; borrowed != 0 && i < ROW; ++i) {
        wr8(borrowed + i, kept_row[i]);
    }
    return actor;
}

int khdays_enemies_remove(unsigned actor)
{
    Pooled *p = pooled(actor);
    const unsigned event_fn = p != NULL ? rd32(actor + AI_EVENT) : 0;
    if (p == NULL || p->leaving || objects() == 0 || event_fn == 0) {
        return 0;
    }
    ((EventFn)KHDAYS_FN(event_fn))(actor, 1);
    p->leaving = 1;
    if (traced()) {
        trace_actor("sent away", actor);
    }
    return 1;
}

static void read_info(unsigned actor, KhdaysEnemyInfo *out)
{
    out->kind = (int)rd8(actor + AI_KIND);
    out->hp = (short)rd16(actor + AI_HP);
    out->max_hp = (short)rd16(actor + AI_HP_MAX);
    out->level = (int)rd8(actor + AI_LEVEL);
    for (int i = 0; i < 3; ++i) {
        out->at[i] = (float)(int)rd32(actor + AI_POSITION + 4u * (unsigned)i) / 4096.0f;
    }
}

int khdays_enemies_info(unsigned actor, KhdaysEnemyInfo *out)
{
    if (pooled(actor) == NULL || objects() == 0) {
        return 0;
    }
    read_info(actor, out);
    return 1;
}

/* The actor manager's standing actors with HP, walked as List_First and List_Next do but without their cursor (a walk
 * of the game's own may be under way): up to `room` into `out`, how many; with `only`, whether it is one of them. */
static int standing(unsigned *out, int room, unsigned only)
{
    const unsigned manager = objects() != 0 ? (unsigned)Ov107_GetActorManager() : 0;
    const unsigned list = manager + MANAGER_LIST;
    int n = 0;
    if (manager == 0) {
        return 0;
    }
    for (unsigned node = rd32(list + LIST_FIRST); node != 0 && node != list + LIST_END; node = rd32(node + NODE_NEXT)) {
        const unsigned actor = rd32(rd32(node + NODE_ITEM));
        /* not the manager's actors with no HP: seen beside the waves mission's Shadows, kind 67, taking no hit */
        if (actor == 0 || !(rd16(actor + AI_FLAGS) & STANDING) || (short)rd16(actor + AI_HP_MAX) <= 0) {
            continue;
        }
        if (only != 0) {
            if (actor == only) {
                return 1;
            }
            continue;
        }
        if (out != NULL && n < room) {
            out[n] = actor;
        }
        ++n;
    }
    return n;
}

int khdays_enemies_hit(unsigned enemy, unsigned attacker, int seat, int damage, const float at[3], unsigned kinds,
                       int chance, int no_flinch)
{
    KhdaysRef hit;
    int took;
    if (!khdays_enemies_state(enemy, NULL) || !khdays_game_ram(attacker)) {
        return 0;
    }
    hit = NNSi_FndAllocFromDefaultExpHeap(HIT_BYTES);
    if (hit == 0) {
        return 0;
    }
    for (unsigned i = 0; i < HIT_BYTES; i += 4) {
        wr32(hit + i, 0);
    }
    wr16(hit + HIT_FLAGS, HIT_PHYSICAL | (no_flinch ? HIT_NO_FLINCH : 0));
    wr16(hit + HIT_KINDS, KIND_PHYSICAL | kinds);
    for (int i = 0; i < 3; ++i) {
        wr32(hit + HIT_PLACE + 4u * (unsigned)i,
             (unsigned)clamp(llround((double)at[i] * 4096.0), -0x7fffffff, 0x7fffffff));
    }
    wr16(hit + HIT_DAMAGE, (unsigned)clamp(damage, 0, 0x7fff));
    wr16(hit + HIT_AUTHOR, (unsigned)seat);
    wr8(hit + HIT_CHANCE, (unsigned)clamp(chance, 0, 100));
    took = Ov107_AiState_ApplyHit(enemy, rd32(attacker + PLAYER_SUB), hit);
    NNSi_FndFreeFromDefaultHeap(hit);
    return took != 0;
}

int khdays_enemies_move(unsigned enemy, const float at[3])
{
    KhdaysRef place;
    if (!khdays_enemies_state(enemy, NULL) || (place = NNSi_FndAllocFromDefaultExpHeap(12)) == 0) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        wr32(place + 4u * (unsigned)i, (unsigned)clamp(llround((double)at[i] * 4096.0), -0x7fffffff, 0x7fffffff));
    }
    Ov107_MoveNodeAndRelayout(enemy, place);
    NNSi_FndFreeFromDefaultHeap(place);
    return 1;
}

int khdays_enemies_list(unsigned *out, int room)
{
    return standing(out, room, 0);
}

int khdays_enemies_state(unsigned actor, KhdaysEnemyInfo *out)
{
    if (actor == 0 || !standing(NULL, 0, actor)) {
        return 0;
    }
    if (out != NULL) {
        read_info(actor, out);
    }
    return 1;
}

void khdays_enemies_marker_made(int source, int row, void *actor, int id)
{
    made = ref_of(actor);
    if (traced()) {
        fprintf(stderr, "enemies: f%u marker actor %08x from source %d row %d, class 0x%02x\n", khdays_link_frame(),
                made, source, row, id);
        trace_actor("made", made);
    }
}

void khdays_enemies_class(int id)
{
    if (traced()) {
        fprintf(stderr, "enemies: f%u class 0x%02x asked for\n", khdays_link_frame(), id);
    }
}

static int mission_held;

void khdays_enemies_hold_mission(int on)
{
    mission_held = on;
}

/* Spawner `s`'s slots: how each lets its kind enter. */
static void learn_entries(unsigned s)
{
    const unsigned owner = rd32(s + SPAWNER_OWNER), table = owner != 0 ? rd32(owner + OWNER_TABLE) : 0;
    const unsigned kinds = table != 0 ? rd32(table + TABLE_KINDS) : 0;
    for (int i = 0; kinds != 0 && i < (short)rd16(s + SPAWNER_COUNT); ++i) {
        const unsigned slot = s + SPAWNER_SLOTS + SPAWNER_SLOT_BYTES * (unsigned)i;
        const int entry = (signed char)rd8(slot + SLOT_ENTRY);
        if (entry >= 0) {
            entry_of[rd8(kinds + TABLE_ROW_BYTES * rd8(slot + SLOT_ROW) + TABLE_ROW_KIND)] = (unsigned char)(entry + 1);
        }
    }
}

int khdays_enemies_spawner_run(void *spawner, int op)
{
    const unsigned s = ref_of(spawner);
    if (traced() && op != 1) {
        fprintf(stderr, "enemies: f%u spawner %08x op %d%s, entries", khdays_link_frame(), s, op,
                op == 0 && mission_held ? ", held" : "");
        for (int i = 0; i < (short)rd16(s + SPAWNER_COUNT); ++i) {
            fprintf(stderr, " %d", (signed char)rd8(s + SPAWNER_SLOTS + SPAWNER_SLOT_BYTES * (unsigned)i + SLOT_ENTRY));
        }
        fprintf(stderr, "\n");
    }
    if (op == 0) {
        learn_entries(s);
    }
    return op == 0 && mission_held;
}

void khdays_enemies_retired(void *actor)
{
    khdays_stop_enemy_gone(actor);
    khdays_actors_ended(ref_of(actor));
    if (traced()) {
        fprintf(stderr, "enemies: f%u actor %08x retired\n", khdays_link_frame(), ref_of(actor));
    }
}

void khdays_enemies_defeated(void *actor)
{
    const unsigned a = ref_of(actor);
    khdays_actors_ended(a);
    if (traced()) {
        fprintf(stderr, "enemies: f%u defeated by %08x (%s): ", khdays_link_frame(), rd32(a + AI_ATTACKER),
                pooled(a) != NULL ? "a mod's" : "the mission's");
        trace_actor("actor", a);
    }
    khdays_api_enemies_defeated(a, (int)rd8(a + AI_KIND));
}

static void forget(void)
{
    if (pool_count > 0 && traced()) {
        fprintf(stderr, "enemies: f%u the area's %d enemies for mods let go\n", khdays_link_frame(), pool_count);
    }
    pool_count = 0;
    memset(entry_of, 0, sizeof(entry_of));
    khdays_api_enemies_field_left();
}

void khdays_enemies_area_left(void)
{
    khdays_stop_area_left();
    khdays_actors_area_left();
    forget();
}

void khdays_enemies_frame(void)
{
    if (pool_count == 0) {
        return;
    }
    if (objects() == 0) {
        forget(); /* the field ended without its area's release (the title, a reset) */
        return;
    }
    /* the ones sent away, until settled: op 2 of a spawner, its slot then free (Ov107_Spawner_Run) */
    for (int i = 0; i < pool_count; ++i) {
        Pooled *p = &pool[i];
        const unsigned event_fn = rd32(p->actor + AI_EVENT);
        if (!p->leaving) {
            continue;
        }
        if (!(rd16(p->actor + AI_FLAGS) & STANDING) || (event_fn != 0 && ((EventFn)KHDAYS_FN(event_fn))(p->actor, 2))) {
            p->leaving = 0;
            wr32(p->actor + AI_SPAWNER, 0);
            khdays_api_enemies_gone(p->actor);
            khdays_actors_ended(p->actor);
            if (traced()) {
                trace_actor("settled away", p->actor);
            }
        }
    }
}
