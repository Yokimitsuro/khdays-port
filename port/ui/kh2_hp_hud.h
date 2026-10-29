/* The opt-in KH2 gauge on the Days HUD; the SEQD evaluator stays independent. */
#ifndef KHDAYS_KH2_HP_HUD_H
#define KHDAYS_KH2_HP_HUD_H

#ifdef __cplusplus
extern "C" {
#endif
#include "elements.h"
#include "bg.h"
#include "hud_parent.h"
#include "../gfx/sequence_image.h"
void khdays_ui_kh2_hp_frame(unsigned frame);
int khdays_ui_kh2_hp_ready(void);
void khdays_ui_kh2_hp_prepare(void);
/* Absolute animated center; other slots use their native anchor until they have a gauge. */
int khdays_ui_kh2_hp_parent(int slot, KhdaysHudParent *parent);
int khdays_ui_kh2_hp_cell(int screen, int bg, const KhdaysBgLayer *layer, unsigned entry);
int khdays_ui_kh2_hp_sprite(const unsigned char *pixels, int width, int height);
int khdays_ui_kh2_hp_compose(const KhdaysUiElement *face, const KhdaysUiElement *hd);
void khdays_ui_kh2_hp_reference(int reference);
void khdays_ui_kh2_hp_commands(KhdaysSequenceImage *out);
#ifdef __cplusplus
}
#endif
#endif
