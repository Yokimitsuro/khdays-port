/* Native player-number labels and optional sequence replacements, attached to the gauges. */
#ifndef KHDAYS_KH2_HUD_LABELS_H
#define KHDAYS_KH2_HUD_LABELS_H
#ifdef __cplusplus
extern "C" {
#endif
#include "elements.h"
#include "../gfx/sequence_image.h"
void khdays_ui_kh2_labels_frame(unsigned frame);
void khdays_ui_kh2_labels_reference(int reference);
void khdays_ui_kh2_labels_prepare(void);
int khdays_ui_kh2_label_box(int index, KhdaysUiElement *box);
int khdays_ui_kh2_label_cell(int index, unsigned entry);
int khdays_ui_kh2_label_native(int index, const KhdaysUiElement *element);
void khdays_ui_kh2_labels_emit(void);
void khdays_ui_kh2_labels_commands(KhdaysSequenceImage out[5]);
#ifdef __cplusplus
}
#endif
#endif
