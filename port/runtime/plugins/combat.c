/* kh.combat for native plugins (sdk/include/kh/kh_combat.h): a plugin's hits, worked out and dealt through the
 * game's routines (game_link.c, enemies_link.c). */
#include "interfaces.h"

#include "../scripts/enemies_link.h"
#include "../scripts/game_link.h"

#include <kh/kh_combat.h>

#include <math.h>
#include <stdlib.h>

/* The statuses' bits in a hit's kinds (Ov107_AiState_ApplyHit: burn, freeze, jolt, toss). */
static const unsigned status_kinds[] = {0, 0x80u, 0x8u, 0x800u, 0x4u};

static int32_t damage(KhActor attacker, float factor)
{
    const KhActors *actors = khdays_plugins_actors();
    const KhRef ref = actors->kind(attacker) == KH_ACTOR_PLAYER ? actors->ref(attacker) : 0;
    const double scale = (double)factor * 4096.0;
    if (ref == 0 || !(scale >= 0.0 && scale <= 0x7fffffff)) {
        return -1;
    }
    return khdays_link_strength_damage(ref, (int)lround(scale));
}

/* Whether `hit` can be dealt from this copy by `attacker` (a player); its author's reference and seat. */
static int dealt_here(KhActor attacker, const KhHit *hit, unsigned *author, int *seat)
{
    const KhActors *actors = khdays_plugins_actors();
    if (hit == NULL || hit->size < sizeof(KhHit) || actors->kind(attacker) != KH_ACTOR_PLAYER) {
        return 0;
    }
    *author = actors->ref(attacker);
    *seat = actors->seat(attacker);
    return 1;
}

static KhResult strike(unsigned enemy, unsigned author, int seat, const KhHit *hit)
{
    const unsigned kinds = hit->status > 0 && hit->status < (int32_t)(sizeof(status_kinds) / sizeof(status_kinds[0]))
                               ? status_kinds[hit->status]
                               : 0;
    return khdays_enemies_hit(enemy, author, seat, hit->damage, hit->at, kinds, hit->chance,
                              (hit->flags & KH_HIT_NO_FLINCH) != 0)
               ? KH_OK
               : KH_ERROR;
}

static KhResult hit_one(KhActor target, KhActor attacker, const KhHit *hit)
{
    const KhActors *actors = khdays_plugins_actors();
    unsigned author;
    int seat;
    if (khdays_link_guest() || !khdays_plugins_may_change(NULL)) {
        return KH_UNSUPPORTED;
    }
    if (actors->kind(target) != KH_ACTOR_ENEMY || !dealt_here(attacker, hit, &author, &seat)) {
        return KH_ERROR;
    }
    return strike(actors->ref(target), author, seat, hit);
}

static int32_t hit_around(KhActor attacker, const float at[3], float radius, const KhHit *hit)
{
    unsigned author, *standing;
    int seat, n, took = 0;
    if (khdays_link_guest() || !khdays_plugins_may_change(NULL) || at == NULL ||
        !dealt_here(attacker, hit, &author, &seat)) {
        return 0;
    }
    n = khdays_enemies_list(NULL, 0);
    standing = n > 0 ? malloc((size_t)n * sizeof(unsigned)) : NULL;
    if (standing == NULL) {
        return 0;
    }
    n = khdays_enemies_list(standing, n);
    for (int i = 0; i < n; ++i) {
        KhdaysEnemyInfo e;
        float d2 = 0.0f;
        if (!khdays_enemies_state(standing[i], &e)) {
            continue; /* defeated by an earlier hit of this one */
        }
        for (int k = 0; k < 3; ++k) {
            d2 += (e.at[k] - at[k]) * (e.at[k] - at[k]);
        }
        if (d2 <= radius * radius && strike(standing[i], author, seat, hit) == KH_OK) {
            ++took;
        }
    }
    free(standing);
    return took;
}

static const KhCombat combat_table = {sizeof(KhCombat), damage, hit_one, hit_around};

const void *khdays_plugins_combat(void)
{
    return &combat_table;
}
