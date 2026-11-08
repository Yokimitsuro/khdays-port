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

/* words of the game's heap for the routines' pointers (a vector, a quaternion), made once */
static KhdaysRef scratch(void)
{
    static KhdaysRef at;
    if (at == 0) {
        at = NNSi_FndAllocFromDefaultExpHeap(32);
    }
    return at;
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

void khdays_kh2_shell_place(unsigned actor, const float place[3], float yaw)
{
    const KhdaysRef v = scratch();
    if (v == 0 || !khdays_game_ram(actor)) {
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
    Ov107_UpdateCollisionSphere(actor);
}

unsigned khdays_kh2_shell_point(const float place[3])
{
    static KhdaysRef at;
    if (at == 0) {
        at = NNSi_FndAllocFromDefaultExpHeap(16);
    }
    if (at == 0) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        *(int *)KHDAYS_W(at + 4u * (unsigned)i) = to_fx(place[i]);
    }
    return at;
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

int khdays_kh2_shell_hit(unsigned actor, unsigned victim, int slot, const float push[3])
{
    const KhdaysRef v = scratch();
    if (v == 0 || !khdays_game_ram(actor) || !khdays_game_ram(victim)) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        *(int *)KHDAYS_W(v + 4u * (unsigned)i) = to_fx(push[i]);
    }
    return Ov107_InvokeHitCallback(victim, actor, actor, slot, v, 0) != 0;
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
