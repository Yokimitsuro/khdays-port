/* The mods' characters' own portraits by their HP gauges in the field and on a room's mission review's cards
 * (docs/CHARACTERS.md). */
#ifndef KHDAYS_CHARACTER_PORTRAITS_H
#define KHDAYS_CHARACTER_PORTRAITS_H
#ifdef __cplusplus
extern "C" {
#endif
/* Before the HUD's pieces: each HUD slot whose seat plays a mod's character with a portrait takes the game's
 * portrait cells, its picture made to stand in their place. */
void khdays_ui_character_portraits_prepare(void);
void khdays_ui_character_portraits_emit(void);
/* A room's mission review's card `entry` (Ov003_SceneInit's join): when its seat plays a mod's character with a
 * portrait (else a face), that picture as the card's face -- 6 x 6 characters of 4 bits in `characters` (0x480
 * bytes) and 16 colours (BGR555, 0 clear) in `palette` -- and 1; else 0 (the base's face stays). */
int khdays_ui_review_face(int entry, unsigned char *characters, unsigned short *palette);
#ifdef __cplusplus
}

#include <cstdint>
#include <vector>

namespace khdays::ui {
// The local player's portrait as the HUD shows it by his HP gauge (slot 0): his mod character's own picture when he
// plays one with a portrait, else the game's portrait cells as the upper screen's BG 3 holds them now
// (Ov002_LoadPanelSlots's); the last one found when the HUD shows none now. Straight-alpha RGBA rows top to bottom,
// red in bits 0-7; `key` another for another picture. False when none has been seen.
bool local_portrait(std::vector<std::uint32_t> &rgba, int &width, int &height, std::uint64_t &key);
} // namespace khdays::ui
#endif
#endif
