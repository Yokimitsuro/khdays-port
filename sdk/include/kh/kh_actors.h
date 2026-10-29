/* KH Mod SDK: the game's actors as handles (kh.actors) -- the players' and the enemies' bodies in the field.
 *
 * A handle (KhActor) names one actor for as long as it lives. Once it is gone -- an enemy defeated or sent away, the
 * players' bodies when their area ends, the field left -- every call with its handle is refused (KH_ACTOR_NONE, 0,
 * -1 or KH_ERROR), and never reaches another actor made in its place. Handles come from a seat (the players), from
 * the list of the standing enemies, or from a game reference a hook point gives (of). Everything here runs on the
 * game's thread.
 *
 * Places are in the field's units (the game's 20.12 fixed point, as floats). */
#ifndef KH_ACTORS_H
#define KH_ACTORS_H

#include "kh_engine.h"
#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_ACTORS "kh.actors"
#define KH_ACTORS_VERSION 1

/* An actor: 0 is none. */
typedef uint64_t KhActor;

typedef enum KhActorKind { KH_ACTOR_NONE = 0, KH_ACTOR_PLAYER = 1, KH_ACTOR_ENEMY = 2 } KhActorKind;

typedef struct KhActors {
    uint32_t size;

    /* Seat `seat`'s player in the field (the room's places 0-3; alone, 0), 0 without one. The seat this copy of the
     * game plays. */
    KhActor (*player)(int32_t seat);
    int32_t (*local_seat)(void);

    /* The standing enemies (made and set going, not yet defeated or sent away): up to `room` of them into `out`
     * (NULL to count); how many there are. */
    int32_t (*enemies)(KhActor *out, int32_t room);

    /* The actor at the game's reference `ref` (a hook point's argument, kh_engine.h), 0 when it is no player's body
     * and no standing enemy. */
    KhActor (*of)(KhRef ref);

    /* Its kind (KH_ACTOR_NONE once gone) and its game reference for kh.engine (0 once gone). */
    int32_t (*kind)(KhActor actor);
    KhRef (*ref)(KhActor actor);

    /* A player's seat, -1 for an enemy. Its character: a player's number (kh_characters.h: the game's 0-18 or a
     * mod's), an enemy's kind (the game's 0-117). -1 once gone. */
    int32_t (*seat)(KhActor actor);
    int32_t (*character)(KhActor actor);

    /* Where it stands, its HP and its maximum. */
    KhResult (*position)(KhActor actor, float at[3]);
    KhResult (*hp)(KhActor actor, int32_t *hp, int32_t *max);

    /* An actor moved: a player placed as the game's scripts place one (KH_UNSUPPORTED before its battle has begun),
     * an enemy as the game moves its own (Ov107_MoveNodeAndRelayout). */
    KhResult (*set_position)(KhActor actor, const float at[3]);

    /* A player's HP set through the game's routine (its gauge follows; 0 is its defeat): KH_UNSUPPORTED while the
     * field is still being set up, and for an enemy. */
    KhResult (*set_hp)(KhActor actor, int32_t hp);

    /* An enemy of `kind` made where the area makes its own, at `at`, its level the area's for its kind (the
     * mission's for a kind the area does not make) plus `level`, its HP and attacks times `hp` and `strength`, and
     * set going: its handle; 0 when it cannot be (told in the mods' log: not in the field, in a room, no room left
     * for its kind's code). Its defeat gives the mission nothing. */
    KhActor (*make_enemy)(KhPlugin *plugin, int32_t kind, const float at[3], int32_t level, float hp, float strength);
} KhActors;

#ifdef __cplusplus
}
#endif

#endif
