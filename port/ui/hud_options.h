/* Optional gauge.json presentation settings, copied into each frame package. */
#ifndef KHDAYS_HUD_OPTIONS_H
#define KHDAYS_HUD_OPTIONS_H
#ifdef __cplusplus
extern "C" {
#endif
enum {
    KHDAYS_HUD_KEEP_INFORMATION = 1u,
    KHDAYS_HUD_ALIGN_EDGES = 2u,
    KHDAYS_HUD_PAUSE_TARGET = 4u,
    KHDAYS_HUD_PANEL_HIDDEN = 8u /* frame-local: move counters back above a hidden mission panel */
};
unsigned khdays_ui_hud_options(void);
int khdays_ui_information_keep_hud(void);
unsigned
khdays_ui_information_policy(void); /* bit 0: replacement, bit 1: retain HUD; held through native transitions */
void khdays_ui_hud_settings_frame(void);
int khdays_ui_command_panel_open(void);
void khdays_ui_information_push_maps(void);
void khdays_ui_information_pop_maps(void);
int khdays_ui_information_preserving_maps(void);
int khdays_ui_information_capture_scroll(unsigned x, unsigned y);
int khdays_ui_information_text_scroll(int *x, int *y);
#ifdef __cplusplus
}
#endif
#endif
