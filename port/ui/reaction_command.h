/* A reaction command offered to this game's own player, as KINGDOM HEARTS II shows it over the command deck -- a KH2
 * enemy's (runtime/kh2_enemies: Cerberus's), and with the setting on Days's own Talk, Examine and the rest -- on the
 * field's upper screen while it is offered: a port addition (Days has none). */
#ifndef KHDAYS_UI_REACTION_COMMAND_H
#define KHDAYS_UI_REACTION_COMMAND_H

#include "../gfx/sequence_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called by the HUD's emit at every VBlank: "hud.reaction_command" above the deck -- KH2's general command menu's bar
 * with the pad's top button's icon and the command's name in the game's language, or without the bar's pieces the
 * icon and the name -- and "hud.reaction_command_mark", the icon over a KH2 enemy offering one. */
void khdays_ui_reaction_command_emit(void);

/* The bar's GPU sequence program for the frame package (empty when none shows). */
void khdays_ui_reaction_command_commands(KhdaysSequenceImage *out);

/* prepare.py's joins. Whether the bar takes Days's commands (the setting on, its pieces there); in
 * Ov022_UpdateCommandInput, Days's command of the thing aimed at (1 when the bar takes it: the deck keeps Attack), the
 * reaction button's press of it (1: a command chosen) and the chosen command made from it: 9 with argument 1 the bar's
 * press (1 returned: chosen without A, as a shortcut's, so the host takes it), argument 2 the deck's own press while
 * the bar has a command (Ov022_TranslateCommandToAction attacks then). */
int khdays_reaction_talk_active(void);
int khdays_reaction_talk_offer(int kind, int usable);
int khdays_reaction_talk_pressed(int pressed, int panel_open);
int khdays_reaction_talk_commit(signed char *kind, unsigned short *arg);

#ifdef __cplusplus
}
#endif

#endif
