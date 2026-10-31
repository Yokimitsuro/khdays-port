/* The mods' characters' own portraits by their HP gauges in the field (docs/CHARACTERS.md, "Its portrait"). */
#ifndef KHDAYS_CHARACTER_PORTRAITS_H
#define KHDAYS_CHARACTER_PORTRAITS_H
#ifdef __cplusplus
extern "C" {
#endif
/* Before the HUD's pieces: each HUD slot whose seat plays a mod's character with a portrait takes the game's
 * portrait cells, its picture made to stand in their place. */
void khdays_ui_character_portraits_prepare(void);
void khdays_ui_character_portraits_emit(void);
#ifdef __cplusplus
}
#endif
#endif
