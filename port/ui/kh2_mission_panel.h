/* Optional mission status sequences; observes Days state without changing gameplay. */
#ifndef KHDAYS_KH2_MISSION_PANEL_H
#define KHDAYS_KH2_MISSION_PANEL_H
#ifdef __cplusplus
extern "C" {
#endif
#include "../gfx/sequence_image.h"
void khdays_ui_mission_panel_frame(unsigned frame);
void khdays_ui_mission_panel_emit(void);
int khdays_ui_mission_panel_owns(void);
int khdays_ui_mission_panel_cell(int engine, int bg, int x, int y);
void khdays_ui_mission_panel_commands(KhdaysSequenceImage *out);
unsigned khdays_ui_mission_panel_state(void);
int khdays_ui_mission_panel_backdrop(void);
#ifdef __cplusplus
}
#endif
#endif
