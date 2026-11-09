/* A KH2 enemy's reaction command offered to the player (runtime/kh2_enemies: Cerberus's): its button and KH2's name
 * for it, on the field's upper screen while it is offered -- a port addition (Days has none). */
#ifndef KHDAYS_UI_REACTION_COMMAND_H
#define KHDAYS_UI_REACTION_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Called by the HUD's emit at every VBlank, while this game's own player is offered one, as KH2 shows it: the element
 * "hud.reaction_command" above the command deck (the DS X button's glyph, the pad's top button, then the command's
 * name in the game's language) and "hud.reaction_command_mark", the glyph over the enemy offering it. */
void khdays_ui_reaction_command_emit(void);

#ifdef __cplusplus
}
#endif

#endif
