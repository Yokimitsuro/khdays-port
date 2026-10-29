/* Days information text on the gaug window/reveal sequences; game-side observation only. */
#ifndef KHDAYS_KH2_INFORMATION_H
#define KHDAYS_KH2_INFORMATION_H
#ifdef __cplusplus
extern "C" {
#endif
#include "elements.h"
#include "../gfx/sequence_image.h"
void khdays_ui_information_frame(unsigned frame);
void khdays_ui_information_message(const unsigned short *text);
void khdays_ui_information_panel(const unsigned short *text);
void khdays_ui_information_crawl(const unsigned short *text);
void khdays_ui_information_reset(void);
unsigned khdays_ui_information_epoch(void);
void khdays_ui_information_hold(int held, int leaving);
void khdays_ui_information_begin(void);
int khdays_ui_information_wants(void);
int khdays_ui_information_configured(void);
/* RGBA contains only glyphs, rasterized by the existing Days font painter. */
int khdays_ui_information_text(const KhdaysUiElement *anchor, const unsigned *rgba, int w, int h, unsigned version,
                               float first_x, float first_y, unsigned coverage);
void khdays_ui_information_end(void);
int khdays_ui_information_active(int engine);
void khdays_ui_information_commands(KhdaysSequenceImage out[2]);
unsigned khdays_ui_information_hold_frames(void);
/* Bits 0/1 own the native banner; bits 8/9 have an image; KHDAYS_INFORMATION_AVAILABLE disables crops. */
unsigned khdays_ui_information_state(void);
#ifdef __cplusplus
}
#endif
#endif
