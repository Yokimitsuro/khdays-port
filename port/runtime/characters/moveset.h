/* A character's [moveset] written into its own copies of its base's combo files (unpacked), which the game's own
 * combat then runs: ci.b, each keyblade's two combo graphs (ground, air: Ov022_RebaseAnimRecord,
 * Ov022_BuildNodeGraph), and cm.b, the attacks they name (Ov022_LinkActionAnims, Ov022_UnpackChannelGrid).
 * docs/RUNTIME_MAP.md, "Characters of mods". Plain C: no game, no system calls. */
#ifndef KHDAYS_CHARACTERS_MOVESET_H
#define KHDAYS_CHARACTERS_MOVESET_H

#include "registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The game's tables a keyblade's combos fill: the attacks of its two graphs (Ov022_LinkActionAnims's set) and their
 * animations (the 16 rows Ov022_FindOrClaimTagSlot claims). */
#define KHDAYS_MOVESET_ATTACKS 64
#define KHDAYS_MOVESET_ANIMATIONS 16

/* Whether the character's description changes its base's combos or attacks. */
int khdays_moveset_wanted(const KhdaysCharacter *c);

/* The base's ci.b rewritten with the character's combos, each keyblade's: a new buffer in *out (free it), 1. 0
 * with `why` when a combo names an attack cm.b does not have, when a keyblade's combos would pass the game's
 * tables, or when a file is not as the game's. */
int khdays_moveset_combos(const KhdaysCharacter *c, const unsigned char *ci, size_t ci_size, const unsigned char *cm,
                          size_t cm_size, unsigned char **out, size_t *out_size, char *why, size_t why_size);

/* The base's cm.b changed in place by the character's attack lines. 0 with `why` for an attack it does not have,
 * one whose hits are not the sweeping kind (mode 0, the only one whose fields are known), or a file not as the
 * game's; the attacks before it are changed. */
int khdays_moveset_attacks(const KhdaysCharacter *c, unsigned char *cm, size_t cm_size, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif

#endif
