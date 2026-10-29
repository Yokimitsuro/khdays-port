/* Animated character portraits, with their own assets and per-slot playback. */
#ifndef KHDAYS_KH2_PORTRAIT_HUD_H
#define KHDAYS_KH2_PORTRAIT_HUD_H
#ifdef __cplusplus
extern "C" {
#endif
#include "../gfx/sequence_image.h"
void khdays_ui_kh2_portrait_frame(unsigned frame);
void khdays_ui_kh2_portrait_reference(int reference);
/* Prepare replacement images before suppressing their exact native portrait cells. */
void khdays_ui_kh2_portrait_prepare(void);
void khdays_ui_kh2_portrait_emit(void);
void khdays_ui_kh2_portrait_commands(KhdaysSequenceImage out[4]);
#ifdef __cplusplus
}
#endif
#endif
