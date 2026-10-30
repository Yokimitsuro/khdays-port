/* The field's prize pictures (mo/prize.pak.z: the HP orb, munny, the item spheres...) drawn as a mod's effect
 * instead (graphics/mo/prize.pak.z/fx/<picture>.txt): KH2's prizes when the installer made them. */
#ifndef KHDAYS_PRIZE_EFFECTS_H
#define KHDAYS_PRIZE_EFFECTS_H
#ifdef __cplusplus
extern "C" {
#endif
#include "../gfx/g3d.h"
#include "../gfx/sequence_image.h"
/* The prize table's pictures: the game's own table holds eleven (the field stage's eleven texture words,
 * Ov002_LoadShopTable), each drawn with its own program. */
#define KHDAYS_PRIZE_PROGRAMS 11
/* Ov002_LoadShopTable: picture `index`, its BTX0 (`btx0`, a DS reference: its name read from its texture
 * dictionary) and its texture words in the table (`words`, the reference the billboards point to). */
void khdays_prize_picture(int index, unsigned btx0, unsigned words);
/* Billboard_DrawList, each node before it is drawn (`node` its host address): 1 when the port drew its picture's
 * effect instead, 0 to let the game draw it. */
int khdays_prize_node(const void *node);
/* Ov002_CollectNearbySpots, a prize taken (KHDAYS_TRACE_PRIZES: its kind, tier and picture; `row` its DS
 * reference). */
void khdays_prize_collected(int kind, int tier, unsigned row);
/* The frame package's programs for the prizes' polygons in `list`. */
void khdays_prize_commands(const KhdaysG3dList *list, KhdaysSequenceImage out[KHDAYS_PRIZE_PROGRAMS]);
#ifdef __cplusplus
}
#endif
#endif
