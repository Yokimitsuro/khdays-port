/* A reaction command offered to this game's own player, as KINGDOM HEARTS II shows it over the command deck -- a KH2
 * enemy's (runtime/kh2_enemies: Cerberus's), and with the setting on Days's own Talk, Examine and the rest -- on the
 * field's upper screen while it is offered: a port addition (Days has none). */
#ifndef KHDAYS_UI_REACTION_COMMAND_H
#define KHDAYS_UI_REACTION_COMMAND_H

#include "../gfx/sequence_image.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "../gfx/g3d.h" /* a C header */
#include "elements.h"  /* a C header */

/* Called by the HUD's emit at every VBlank: "hud.reaction_command" above the deck -- KH2's general command menu's bar
 * with the pad's top button's icon and the command's name in the game's language, or without the bar's pieces the
 * icon and the name -- and without KH2's mark (below) "hud.reaction_command_mark", the icon where it would be. */
void khdays_ui_reaction_command_emit(void);

/* The bar's GPU sequence program for the frame package (empty when none shows). */
void khdays_ui_reaction_command_commands(KhdaysSequenceImage *out);

/* KH2's command menu in an enemy's own pictures (runtime/kh2_enemies, 1:359: Zexion's book in his Lexicon), drawn in
 * place of the command deck: hud.c gives the deck's element (its DS place) at every VBlank it is there; 1 when the
 * menu stands over it (the deck's cells and sprites then left out), 0 for Days's deck. Whether it stood this VBlank;
 * its GPU sequence program for the frame package (empty when none). The reaction command's bar takes the same
 * pictures while it stands, as KH2's menu does. */
int khdays_ui_command_image_compose(const KhdaysUiElement *deck);
int khdays_ui_command_image_owns(void);
void khdays_ui_command_image_commands(KhdaysSequenceImage *out);
/* Whether it will stand this VBlank: for the text part, which emits before the HUD (the deck's names then left out,
 * as the menu writes its own). */
int khdays_ui_command_image_standing(void);

/* KH2's mark where an enemy offering this game's own player a reaction command aims it, a picture of the 3D layer:
 * drawn while the game builds its 3D list, round `spot` (clip space x, y, z, w: runtime/kh2_enemies projects it as it
 * draws the enemy), when it shows (without KH2's piece, the button's icon there); its program for the frame package's
 * last 3D program. */
void khdays_ui_reaction_mark_draw(const float spot[4]);
void khdays_ui_reaction_mark_commands(const KhdaysG3dList *list, KhdaysSequenceImage *out);

/* prepare.py's joins. Whether the bar takes Days's commands (the setting on, its pieces there); in
 * Ov022_UpdateCommandInput, Days's command of the thing aimed at (1 when the bar takes it: the deck keeps Attack), the
 * reaction button's press of it (1: a command chosen; also a KH2 enemy's command's press, which chooses none, the deck
 * left as it is) and the chosen command made from it: 9 with argument 1 the bar's
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
