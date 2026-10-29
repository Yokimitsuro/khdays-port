/* Optional field notification sequences with native Days text. */
#ifndef KHDAYS_KH2_OBTAINED_H
#define KHDAYS_KH2_OBTAINED_H
#ifdef __cplusplus
extern "C" {
#endif
#include "elements.h"
#include "../gfx/sequence_image.h"
int khdays_ui_obtained_enabled(void);
int khdays_ui_obtained_configured(void);
void khdays_ui_obtained_request(int announcement);
void khdays_ui_obtained_frame(unsigned frame);
void khdays_ui_obtained_begin(void);
int khdays_ui_obtained_text(const KhdaysUiElement *anchor, const unsigned *rgba, int w, int h, unsigned version);
int khdays_ui_obtained_owns(void);
void khdays_ui_obtained_end(void);
void khdays_ui_obtained_commands(KhdaysSequenceImage *out);
/* Additional notices: 1 backpack full, 2 recon breakthrough, 3 locked-on caption. */
int khdays_ui_notification_enabled(int kind);
int khdays_ui_notification_configured(int kind);
void khdays_ui_notification_request(int kind, int present);
int khdays_ui_notification_owns(int kind);
int khdays_ui_notification_box(int kind, KhdaysUiElement *out);
int khdays_ui_notification_text(int kind, const KhdaysUiElement *anchor, const unsigned *rgba, int w, int h,
                                unsigned version);
int khdays_ui_notification_cell(int engine, int bg, int x, int y);
void khdays_ui_notification_layout(const void *request, int shown);
void khdays_ui_notification_commands(KhdaysSequenceImage *out);
#ifdef __cplusplus
}
#endif
#endif
