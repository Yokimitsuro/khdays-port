/* KH Mod SDK: hits of a plugin's own (kh.combat) -- moves and Limits a plugin makes, dealt through the game's routines.
 *
 * damage() works a hit's damage out as the game does for a player's combo hits (its strength and bonuses, its
 * abilities, Ov022_ComputeStrengthDamage, a hook point: what other plugins do to it applies). hit() deals it to an
 * enemy through the routine the players' attacks reach (Ov107_AiState_ApplyHit, a hook point too): the enemy's own
 * code takes its HP down, flinches, burns ..., is defeated and rewards the attacker as the game decides; a hit during
 * the enemy's grace after the last one is not taken, as the game's are not. Hits are the host's in a room (the game
 * works them out there alone): on a guest's copy hit() answers KH_UNSUPPORTED. On the game's thread. */
#ifndef KH_COMBAT_H
#define KH_COMBAT_H

#include "kh_actors.h"
#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_COMBAT "kh.combat"
#define KH_COMBAT_VERSION 1

/* A status a hit may give (the game's four). */
typedef enum KhStatus {
    KH_STATUS_NONE = 0,
    KH_STATUS_BURN = 1,
    KH_STATUS_FREEZE = 2,
    KH_STATUS_JOLT = 3,
    KH_STATUS_TOSS = 4 /* thrown into the air */
} KhStatus;

/* Flags of a hit. */
#define KH_HIT_NO_FLINCH 1u /* the enemy takes the damage without its hit reaction */

typedef struct KhHit {
    uint32_t size;  /* the caller sets sizeof(KhHit) */
    int32_t damage; /* taken as it is (the enemy's resistances are for the game's own attacks) */
    float at[3];    /* where it lands: the reaction's side */
    int32_t status; /* KhStatus */
    int32_t chance; /* 0-100: the status's chance, before the enemy's resistance to it */
    uint32_t flags; /* KH_HIT_* */
} KhHit;

typedef struct KhCombat {
    uint32_t size;

    /* The damage of a hit of `attacker` (a player) of factor `factor` (1.0: its strength; the game's combo hits
     * 0.75-1.5) as the game works it out; -1 when it is gone or the battle is not there. */
    int32_t (*damage)(KhActor attacker, float factor);

    /* `target` (a standing enemy) hit by `attacker` (a player: the hit's author, who gets its reward): KH_OK when
     * the enemy took it, KH_ERROR when not (gone, its grace, its guard), KH_UNSUPPORTED on a guest's copy. */
    KhResult (*hit)(KhActor target, KhActor attacker, const KhHit *hit);

    /* Every standing enemy within `radius` of `at` hit as hit() does: how many took it. */
    int32_t (*hit_around)(KhActor attacker, const float at[3], float radius, const KhHit *hit);
} KhCombat;

#ifdef __cplusplus
}
#endif

#endif
