#ifndef KHDAYS_UI_KH2_ENEMY_HUD_H
#define KHDAYS_UI_KH2_ENEMY_HUD_H
#include "../gfx/sequence_image.h"
#ifdef __cplusplus
extern "C" {
#endif
void khdays_ui_kh2_enemy_frame(unsigned frame);
void khdays_ui_kh2_enemy_reference(int reference);
void khdays_ui_kh2_enemy_emit(void);
int khdays_ui_kh2_enemy_ready(void);
int khdays_ui_kh2_enemy_sprite(const unsigned char *pixels, int width, int height);
void khdays_ui_kh2_enemy_commands(KhdaysSequenceImage *out);
#ifdef __cplusplus
}
#endif
#endif
