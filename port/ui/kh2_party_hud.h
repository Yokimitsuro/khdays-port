/* Player-style gauges for the three party HUD slots. */
#ifndef KHDAYS_KH2_PARTY_HUD_H
#define KHDAYS_KH2_PARTY_HUD_H
#ifdef __cplusplus
extern "C" {
#endif
#include "elements.h"
#include "bg.h"
#include "hud_parent.h"
#include "../gfx/sequence_image.h"
void khdays_ui_kh2_party_frame(unsigned frame);
void khdays_ui_kh2_party_reference(int reference);
void khdays_ui_kh2_party_prepare(void);
int khdays_ui_kh2_party_ready(int slot);
int khdays_ui_kh2_party_box(int slot, KhdaysUiElement *box);
int khdays_ui_kh2_party_parent(int slot, KhdaysHudParent *parent);
int khdays_ui_kh2_party_compose(int slot, const KhdaysUiElement *face, const KhdaysUiElement *hd);
void khdays_ui_kh2_party_commands(KhdaysSequenceImage out[3]);
#ifdef __cplusplus
}
#endif
#endif
