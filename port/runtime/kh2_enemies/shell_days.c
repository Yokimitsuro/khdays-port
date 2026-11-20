/* The Days side of a KH2 enemy's shell (shell_days.h). The actor's fields as game/actor.h names them. */
#include "shell_days.h"

#include "../core/window.h"

#include <math.h>
#include <stddef.h>

extern void Srt_SetTranslation(KhdaysRef srt, KhdaysRef translation);
extern int Ov107_UpdateCollisionSphere(KhdaysRef actor);
extern int Srt_SetRotationQuat(KhdaysRef srt, KhdaysRef quaternion);
extern int Ov107_InvokeHitCallback(KhdaysRef victim, KhdaysRef a, KhdaysRef b, int mode, KhdaysRef push, int flags);
extern KhdaysRef NNSi_FndAllocFromDefaultExpHeap(unsigned int size);
extern void NNSi_FndFreeFromDefaultHeap(KhdaysRef block);
extern int Session_GetLocalPlayerIndex(void);
extern void Actor_SetVecAndSyncChild(KhdaysRef node, KhdaysRef pos);
extern int Ov022_HeadingFromHeldDpad(int actor);
extern KhdaysRef Collision_RunRayCast(KhdaysRef world, KhdaysRef params);
extern KhdaysRef Collision_RunSphereCast(KhdaysRef world, KhdaysRef params);
extern KhdaysRef Collision_CastNearest(KhdaysRef world, KhdaysRef params);
extern int GameState_GetField(int a, int b);

#define ACTOR_SCENE 0x4u
#define ACTOR_MODE 0x50u    /* 1: this game's own */
#define ACTOR_FLAGS60 0x60u /* 0x100 standing; its low bit: collision on */
#define ACTOR_SPHERE_OFFSET 0x64u /* the sphere's centre from its place (fx32 x3) */
#define ACTOR_BASE_RADIUS 0x70u   /* the sphere's radius, made + 0x80 each frame */
#define ACTOR_SPHERE 0x74u  /* centre (fx32 x3), radius */
#define ACTOR_RADIUS 0x80u
#define ACTOR_SRT 0xa0u /* rotation (4), translation (+0x10), scale */
#define ACTOR_PLACE 0xb0u
#define ACTOR_GROUND 0x13cu /* the ground's distance under its sphere's centre (Ov107_AiState_ResolveContacts) */
#define ACTOR_GROUND_HIT 0x140u
#define ACTOR_ATTACKS 0x290u /* s16 power, 6 bytes each, 8 */
#define ACTOR_HP_MAX 0x218u
#define ACTOR_HP 0x21au
#define ACTOR_KIND 0x19cu
#define STANDING 0x100u
#define SCENE_PLAYERS 0xa8u /* the players' list (Ov107_FindNearestObject) */
#define LIST_FIRST 0x4u
#define LIST_END 0x10u
#define NODE_NEXT 0x4u
#define NODE_ITEM 0xcu
#define PLAYER_ACTIVE 0x40u /* bit 1 */
/* a player's actor (Ov022_ActorUpdate's) */
#define PLAYER_OWNER 0x8u   /* u8: its player's number in the session */
#define PLAYER_HP 0x12u     /* u16 */
#define PLAYER_NODE 0x20u   /* its model's node */
#define PLAYER_POS 0x48cu   /* its place (fx32 x3) */
#define PLAYER_FIELD 0x4ecu /* its field object, the players' list's */
#define PLAYER_PRESSED 0x18u /* u16: Days's buttons pressed this frame (1 the command, 0x800 the jump) */
#define PLAYER_ANGLE_BIAS 0x478u /* s16: the camera's turn its D-pad's heading is made from (nAngleBias) */
#define NODE_FLAGS 0x0u    /* u32 of its model's node: 0x20 -- Ov022_ActorUpdate, Ov022_ActorSetState and
                               Ov022_RunReplayFrame leave the node undrawn (nothing of the game sets it on an
                               actor's node) */
#define NODE_UNDRAWN 0x20u
#define NODE_ANGLE 0x80u    /* u16: the way its model looks, (sin, 0, cos) of it (Ov022_StepGlideState: the heading
                               + 0x8000, its way (-sin, -cos) of the heading) */

static unsigned rd32(KhdaysRef at)
{
    return khdays_game_ram(at) ? *(const unsigned *)KHDAYS_W(at) : 0;
}

static unsigned rd16(KhdaysRef at)
{
    return khdays_game_ram(at) ? *(const unsigned short *)KHDAYS_W(at) : 0;
}

static float fx(KhdaysRef at)
{
    return (float)(int)rd32(at) / 4096.0f;
}

static int to_fx(float v)
{
    const double d = (double)v * 4096.0;
    return d > 2147483647.0 ? 0x7fffffff : d < -2147483647.0 ? -0x7fffffff : (int)lrint(d);
}

/* words of the game's heap for a routine's pointers (a vector, a quaternion): taken for the call and given back
 * after it -- the field's heap goes with its area, so a block kept from one area would be another's in the next (a
 * mission played again: a task's or the information panel's list written over) */
static KhdaysRef words(unsigned size)
{
    return NNSi_FndAllocFromDefaultExpHeap(size);
}

static void words_done(KhdaysRef at)
{
    if (at != 0) {
        NNSi_FndFreeFromDefaultHeap(at);
    }
}

/* the point khdays_kh2_shell_point gives the game's code, which reads it after the call: kept for the area */
static KhdaysRef area_point;

void khdays_kh2_shell_area_left(void)
{
    words_done(area_point); /* the field's heap still stands (Ov002_ReleaseResources frees its own after) */
    area_point = 0;
}

int khdays_kh2_shell_read(unsigned actor, KhdaysKh2Shell *out)
{
    if (actor == 0 || !khdays_game_ram(actor) || !khdays_game_ram(actor + ACTOR_HP)) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        out->place[i] = fx(actor + ACTOR_PLACE + 4u * (unsigned)i);
    }
    out->radius = fx(actor + ACTOR_RADIUS);
    out->above = rd32(actor + ACTOR_GROUND_HIT) != 0 ? fx(actor + ACTOR_GROUND) - out->radius : -1.0f;
    out->standing = (rd16(actor + ACTOR_FLAGS60) & STANDING) != 0;
    out->own = rd32(actor + ACTOR_MODE) == 1;
    out->hp = (short)rd16(actor + ACTOR_HP);
    out->hp_max = (short)rd16(actor + ACTOR_HP_MAX);
    out->kind = khdays_game_ram(actor + ACTOR_KIND) ? *(const unsigned char *)KHDAYS_W(actor + ACTOR_KIND) : -1;
    return 1;
}

void khdays_kh2_shell_set_hp(unsigned actor, int hp)
{
    if (actor == 0 || !khdays_game_ram(actor + ACTOR_HP)) {
        return;
    }
    const int most = (short)rd16(actor + ACTOR_HP_MAX);
    hp = hp > most ? most : hp;
    *(unsigned short *)KHDAYS_W(actor + ACTOR_HP) = (unsigned short)(hp < 1 ? 1 : hp);
}

void khdays_kh2_shell_set_health(unsigned actor, int hp, int most)
{
    if (actor == 0 || !khdays_game_ram(actor + ACTOR_HP_MAX) || !khdays_game_ram(actor + ACTOR_HP)) {
        return;
    }
    most = most < 1 ? 1 : most;
    hp = hp > most ? most : hp;
    *(unsigned short *)KHDAYS_W(actor + ACTOR_HP_MAX) = (unsigned short)most;
    *(unsigned short *)KHDAYS_W(actor + ACTOR_HP) = (unsigned short)(hp < 1 ? 1 : hp);
}

void khdays_kh2_shell_place(unsigned actor, const float place[3], float yaw)
{
    if (!khdays_game_ram(actor)) {
        return;
    }
    const KhdaysRef v = words(32);
    if (v == 0) {
        return;
    }
    /* its turn about the up (a quaternion w, x, y, z: Quat_FromTwoVectors'), so that what Days puts on its model's
       nodes (its lock-on mark) and its sense of front turn with the KH2 enemy: KH2's yaw turns +z to (sin, 0, cos),
       as a turn about +y does */
    *(int *)KHDAYS_W(v + 16u) = to_fx(cosf(yaw * 0.5f));
    *(int *)KHDAYS_W(v + 20u) = 0;
    *(int *)KHDAYS_W(v + 24u) = to_fx(sinf(yaw * 0.5f));
    *(int *)KHDAYS_W(v + 28u) = 0;
    Srt_SetRotationQuat(actor + ACTOR_SRT, v + 16u);
    for (int i = 0; i < 3; ++i) {
        *(int *)KHDAYS_W(v + 4u * (unsigned)i) = to_fx(place[i]);
    }
    Srt_SetTranslation(actor + ACTOR_SRT, v);
    words_done(v);
    Ov107_UpdateCollisionSphere(actor);
}

unsigned khdays_kh2_shell_point(const float place[3])
{
    if (area_point == 0) {
        area_point = words(16);
    }
    if (area_point == 0) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        *(int *)KHDAYS_W(area_point + 4u * (unsigned)i) = to_fx(place[i]);
    }
    return area_point;
}

void khdays_kh2_shell_size(unsigned actor, float radius, float centre, float *old_radius, float *old_centre)
{
    if (!khdays_game_ram(actor) || !khdays_game_ram(actor + ACTOR_SPHERE_OFFSET + 8u)) {
        return;
    }
    if (old_radius != NULL) {
        *old_radius = fx(actor + ACTOR_BASE_RADIUS);
    }
    if (old_centre != NULL) {
        *old_centre = fx(actor + ACTOR_SPHERE_OFFSET + 4u);
    }
    *(int *)KHDAYS_W(actor + ACTOR_BASE_RADIUS) = to_fx(radius);
    *(int *)KHDAYS_W(actor + ACTOR_SPHERE_OFFSET + 4u) = to_fx(centre);
    Ov107_UpdateCollisionSphere(actor);
}

int khdays_kh2_shell_players(unsigned actor, KhdaysKh2Player *out, int room)
{
    const unsigned scene = rd32(actor + ACTOR_SCENE);
    const unsigned list = scene + SCENE_PLAYERS;
    int n = 0;
    if (scene == 0) {
        return 0;
    }
    /* walked as List_First and List_Next do, without their cursor (the game's own walk may be under way) */
    for (unsigned node = rd32(list + LIST_FIRST); node != 0 && node != list + LIST_END; node = rd32(node + NODE_NEXT)) {
        const unsigned e = rd32(rd32(node + NODE_ITEM));
        if (e == 0 || ((rd32(e + PLAYER_ACTIVE) >> 1) & 1u) == 0 || (rd16(e + ACTOR_FLAGS60) & 1u) == 0) {
            continue;
        }
        if (out != NULL && n < room) {
            out[n].object = e;
            out[n].radius = fx(e + ACTOR_RADIUS);
            for (int i = 0; i < 3; ++i) {
                out[n].place[i] = fx(e + ACTOR_SPHERE + 4u * (unsigned)i);
            }
            out[n].place[1] -= out[n].radius;
        }
        ++n;
    }
    return n;
}

int khdays_kh2_shell_difficulty(void)
{
    return GameState_GetField(0x40a, 2) & 3;
}

int khdays_kh2_shell_hit(unsigned actor, unsigned victim, int slot, const float push[3], int unguarded)
{
    if (!khdays_game_ram(actor) || !khdays_game_ram(victim)) {
        return 0;
    }
    const KhdaysRef v = words(12);
    if (v == 0) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        *(int *)KHDAYS_W(v + 4u * (unsigned)i) = to_fx(push[i]);
    }
    /* the request's 8 (Ov022_TryBlockHit's: no block) */
    const int hit = Ov107_InvokeHitCallback(victim, actor, actor, slot, v, unguarded ? 8 : 0) != 0;
    words_done(v);
    return hit;
}

#define PLAYER_FLAGS 0x0u    /* u64 */
#define PLAYER_FLAGS2 0x464u /* u64 */
#define PLAYER_DRIFT 0x47cu  /* fx32 x3: the push he is moved by, halved each frame (Ov022_IntegrateRecoilVec) */
#define PLAYER_STAGGER 0x10u
#define PLAYER_STAGGER_LOCAL 0x100u

void khdays_kh2_shell_player_recoil(unsigned actor, const float push[3])
{
    /* the host's alone, as Ov022_ReceiveHit (through func_ov022_02088398) runs there */
    if (Session_GetLocalPlayerIndex() != 0 || !khdays_game_ram(actor) || !khdays_game_ram(actor + PLAYER_DRIFT + 8u) ||
        rd16(actor + PLAYER_HP) == 0) {
        return;
    }
    *(unsigned long long *)KHDAYS_W(actor + PLAYER_FLAGS) |= PLAYER_STAGGER;
    *(unsigned long long *)KHDAYS_W(actor + PLAYER_FLAGS2) |= PLAYER_STAGGER_LOCAL;
    for (int i = 0; i < 3; ++i) {
        *(int *)KHDAYS_W(actor + PLAYER_DRIFT + 4u * (unsigned)i) = to_fx(push[i]);
    }
}

#define SCENE_COLLISION 0x7cu /* a scene's room collision (the actor's + 0x4's) */
#define HIT_SHARE 0xcu        /* a hit's distance along the way cast, a Q27 share of it */

/* CollCastParams: `map_only` (the decomp's directionIsUnit) leaves the collision's objects out -- what
 * Coll_WalkTreeForHit and Coll_WalkSphereTreeForHit read it for -- as the camera's casts set it */
#define CAST_ORIGIN 0x0u
#define CAST_WAY 0x4u
#define CAST_RADIUS 0x8u
#define CAST_MAP_ONLY 0xcu /* u16 */
#define CAST_FLAGS 0xeu    /* u16 */
#define CAST_EXCLUDE 0x10u
#define CAST_SIZE 0x14u
#define HIT_FACE 0x4u     /* the face met */
#define FACE_NORMAL 0x14u /* its plane's normal, s16 x3 */

typedef KhdaysRef (*CastRoutine)(KhdaysRef world, KhdaysRef params);

/* The room's collision under the actor (its scene's), 0 none */
static unsigned room_collision(unsigned actor)
{
    const unsigned scene = rd32(actor + ACTOR_SCENE);
    return scene != 0 ? rd32(scene + SCENE_COLLISION) : 0;
}

int khdays_kh2_shell_face_normal(unsigned face, float normal[3])
{
    const short *plane = face != 0 ? (const short *)KHDAYS_W(face + FACE_NORMAL) : NULL;
    const double size = plane != NULL ? sqrt((double)plane[0] * plane[0] + (double)plane[1] * plane[1] +
                                             (double)plane[2] * plane[2])
                                      : 0.0;
    for (int i = 0; i < 3; ++i) {
        normal[i] = size > 0.0 ? (float)(plane[i] / size) : 0.0f;
    }
    return size > 0.0;
}

static void along(const float from[3], const float to[3], double share, float hit[3]);

#define FACE_SLOTS 0x80u  /* u8 x4: the named records it names (0xff none); its surface tags in 1 and 2 */
#define RECORD_TAG 0xcu   /* u8: the tag the game gives a record's name at load (data_ov002_0207e640) */
#define TAG_NOHIT 2u      /* `nohit`: Ov022_CastMove's parts go through such a face */
#define THROUGH 0.002f    /* metres: how far past a face gone through the cast goes on */

extern KhdaysRef Actor_GetRecord(KhdaysRef hit, int index);

/* Whether the face a cast met (its hit record) is named nohit -- slots 1 and 2, as Ov022_StepJumpState reads a floor's
 * tags (a ground's slot 3 is no name, Days's data puts none in 0) */
static int named_nohit(KhdaysRef h)
{
    const KhdaysRef face = rd32(h + HIT_FACE);
    if (face == 0) { /* (a hit with no face of the room's: none named) */
        return 0;
    }
    for (unsigned i = 1; i <= 2; ++i) {
        const unsigned slot = *(const unsigned char *)KHDAYS_W(face + FACE_SLOTS + i);
        const KhdaysRef record = slot != 0xffu ? Actor_GetRecord(h, (int)slot) : 0;
        if (record != 0 && *(const unsigned char *)KHDAYS_W(record + RECORD_TAG) == TAG_NOHIT) {
            return 1;
        }
    }
    return 0;
}

/* One of Days's casts of the room's collision from `from` along to `to` (metres) with `radius`: how far along the way
 * it meets a face (0 to 1, its distance a Q27 share of the way) and that face's normal (a unit way); whether it does.
 * With `attacks` a face named nohit is gone through: cast again from just past it. The hit record and the face are
 * the collision's own (not the game's heap): read where they lie, as the camera's casts do. */
static int cast_room(CastRoutine cast, unsigned world, const float from[3], const float to[3], float radius,
                     int attacks, double *share, float normal[3], KhdaysRef *met)
{
    const double length = sqrt((double)(to[0] - from[0]) * (to[0] - from[0]) +
                               (double)(to[1] - from[1]) * (to[1] - from[1]) +
                               (double)(to[2] - from[2]) * (to[2] - from[2]));
    double done = 0.0; /* the share of the way behind the cast's start */
    float start[3] = {from[0], from[1], from[2]};
    for (;;) {
        const KhdaysRef v = words(24 + CAST_SIZE);
        if (v == 0) {
            return 0;
        }
        const KhdaysRef params = v + 24u;
        for (int i = 0; i < 3; ++i) {
            *(int *)KHDAYS_W(v + 4u * (unsigned)i) = to_fx(start[i]);
            *(int *)KHDAYS_W(v + 12u + 4u * (unsigned)i) = to_fx(to[i] - start[i]);
        }
        *(KhdaysRef *)KHDAYS_W(params + CAST_ORIGIN) = v;
        *(KhdaysRef *)KHDAYS_W(params + CAST_WAY) = v + 12u;
        *(int *)KHDAYS_W(params + CAST_RADIUS) = to_fx(radius);
        *(unsigned short *)KHDAYS_W(params + CAST_MAP_ONLY) = 1;
        *(unsigned short *)KHDAYS_W(params + CAST_FLAGS) = 0;
        *(KhdaysRef *)KHDAYS_W(params + CAST_EXCLUDE) = 0;
        const KhdaysRef h = cast(world, params);
        words_done(v);
        if (h == 0) {
            return 0;
        }
        const double at = done + (1.0 - done) * ((double)*(const int *)KHDAYS_W(h + HIT_SHARE) / 134217728.0);
        if (attacks && length > 0.0 && named_nohit(h)) {
            done = at + THROUGH / length;
            if (done >= 1.0) {
                return 0;
            }
            along(from, to, done, start);
            continue;
        }
        *share = at;
        const KhdaysRef face = *(const KhdaysRef *)KHDAYS_W(h + HIT_FACE);
        if (met != NULL) {
            *met = face;
        }
        khdays_kh2_shell_face_normal(face, normal);
        return 1;
    }
}

static void along(const float from[3], const float to[3], double share, float hit[3])
{
    for (int i = 0; i < 3; ++i) {
        hit[i] = from[i] + (float)((double)(to[i] - from[i]) * share);
    }
}

int khdays_kh2_shell_ray(unsigned actor, const float from[3], const float to[3], float hit[3])
{
    return khdays_kh2_shell_ray_face(actor, from, to, 0, hit, NULL);
}

int khdays_kh2_shell_ray_face(unsigned actor, const float from[3], const float to[3], int attacks, float hit[3],
                              unsigned *face)
{
    const unsigned world = room_collision(actor);
    if (world == 0) {
        return 0;
    }
    /* the three lists, as the camera's line meets them: the ray's floors and slopes, the sphere cast's walls (with no
     * radius a line), the plain cast's ceilings -- the nearest */
    const CastRoutine casts[3] = {Collision_RunRayCast, Collision_RunSphereCast, Collision_CastNearest};
    double nearest = 2.0;
    KhdaysRef nearest_face = 0;
    for (int k = 0; k < 3; ++k) {
        double share = 0.0;
        float normal[3];
        KhdaysRef met = 0;
        if (cast_room(casts[k], world, from, to, 0.0f, attacks, &share, normal, &met) && share < nearest) {
            nearest = share;
            nearest_face = met;
        }
    }
    if (nearest > 1.0) {
        return 0;
    }
    along(from, to, nearest, hit);
    if (face != NULL) {
        *face = nearest_face;
    }
    return 1;
}

int khdays_kh2_shell_wall(unsigned actor, const float from[3], const float to[3], float radius, float hit[3],
                          float normal[3])
{
    const unsigned world = room_collision(actor);
    double share = 0.0;
    if (world == 0 || !cast_room(Collision_RunSphereCast, world, from, to, radius, 0, &share, normal, NULL)) {
        return 0;
    }
    along(from, to, share, hit);
    return 1;
}

int khdays_kh2_shell_attack_slot(unsigned actor)
{
    for (int i = 0; i < 8; ++i) { /* the actor's own eight slots */
        if ((short)rd16(actor + ACTOR_ATTACKS + 6u * (unsigned)i) > 0) {
            return i;
        }
    }
    return 0;
}

unsigned khdays_kh2_shell_player_object(unsigned actor, int *local, unsigned *node)
{
    if (actor == 0 || !khdays_game_ram(actor + PLAYER_FIELD)) {
        return 0;
    }
    if (local != NULL) {
        *local = *(const unsigned char *)KHDAYS_W(actor + PLAYER_OWNER) == (unsigned)Session_GetLocalPlayerIndex();
    }
    if (node != NULL) {
        *node = rd32(actor + PLAYER_NODE);
    }
    return rd32(actor + PLAYER_FIELD);
}

void khdays_kh2_shell_put_player(unsigned actor, const float place[3], float yaw)
{
    const unsigned node = rd32(actor + PLAYER_NODE);
    const double turn = 6.283185307179586;
    if (node == 0 || !khdays_game_ram(node + NODE_ANGLE) || !khdays_game_ram(actor + PLAYER_POS + 8u)) {
        return;
    }
    const KhdaysRef v = words(12);
    if (v == 0) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        *(int *)KHDAYS_W(v + 4u * (unsigned)i) = to_fx(place[i]);
        *(int *)KHDAYS_W(actor + PLAYER_POS + 4u * (unsigned)i) = to_fx(place[i]);
    }
    Actor_SetVecAndSyncChild(node, v);
    words_done(v);
    *(unsigned short *)KHDAYS_W(node + NODE_ANGLE) = (unsigned short)(lrint((double)yaw / turn * 65536.0) & 0xffff);
}

int khdays_kh2_shell_player_hp(unsigned actor)
{
    return actor != 0 && khdays_game_ram(actor + PLAYER_HP) ? (int)rd16(actor + PLAYER_HP) : -1;
}

#define PLAYER_HP_MOST 0x16u /* u16 */
#define PLAYER_MEMBER 0x9u   /* u8: its member record's number (nId) */
#define PARTY_MEMBERS 0x0204c678u /* gPartyMembers: four records of 0x104 bytes */
#define MEMBER_SIZE 0x104u
#define MEMBER_LEVEL 0x2u /* u8: its level less 1 */
#define MEMBER_MADE 0x6u  /* u16: not 0 once made (runtime/net/roomlist.c's own_level) */

extern void Ov022_ActorSetHp(KhdaysRef actor, int hp);

int khdays_kh2_shell_player_most_hp(unsigned actor)
{
    return actor != 0 && khdays_game_ram(actor + PLAYER_HP_MOST) ? (int)rd16(actor + PLAYER_HP_MOST) : -1;
}

int khdays_kh2_shell_player_level(unsigned actor)
{
    if (actor == 0 || !khdays_game_ram(actor + PLAYER_MEMBER)) {
        return 0;
    }
    const unsigned member = *(const unsigned char *)KHDAYS_W(actor + PLAYER_MEMBER);
    const unsigned record = PARTY_MEMBERS + MEMBER_SIZE * (member & 3u);
    if (!khdays_game_ram(record + MEMBER_MADE) || rd16(record + MEMBER_MADE) == 0) {
        return 0;
    }
    return (int)*(const unsigned char *)KHDAYS_W(record + MEMBER_LEVEL) + 1;
}

void khdays_kh2_shell_player_set_hp(unsigned actor, int hp)
{
    /* the host's alone: its actors' HP are the game's (a guest's copy follows) */
    if (Session_GetLocalPlayerIndex() != 0 || actor == 0 || !khdays_game_ram(actor + PLAYER_HP_MOST)) {
        return;
    }
    Ov022_ActorSetHp(actor, hp);
}

int khdays_kh2_shell_player_pad(unsigned actor, unsigned *pressed, int *heading)
{
    if (actor == 0 || !khdays_game_ram(actor + PLAYER_ANGLE_BIAS + 1u) || pressed == NULL || heading == NULL) {
        return 0;
    }
    const int dpad = Ov022_HeadingFromHeldDpad((int)actor);
    *pressed = rd16(actor + PLAYER_PRESSED);
    *heading = dpad < 0 ? -1 : (int)(((unsigned)dpad + (unsigned)(short)rd16(actor + PLAYER_ANGLE_BIAS)) & 0xffffu);
    return 1;
}

void khdays_kh2_shell_player_hidden(unsigned actor, int hidden)
{
    const unsigned node = actor != 0 ? rd32(actor + PLAYER_NODE) : 0u;
    if (node == 0 || !khdays_game_ram(node + NODE_FLAGS)) {
        return;
    }
    unsigned *flags = (unsigned *)KHDAYS_W(node + NODE_FLAGS);
    *flags = hidden ? (*flags | NODE_UNDRAWN) : (*flags & ~NODE_UNDRAWN);
}
