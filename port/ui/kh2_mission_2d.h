/* KINGDOM HEARTS II's mission pieces on the field's HUD: the timer and counters of the KH2 mission a KH2 boss's
 * battle runs (runtime/kh2_enemies), drawn from the mission's own pictures and sequences as KH2 builds them
 * (kh2_mission_2d.cpp). */
#ifndef KHDAYS_UI_KH2_MISSION_2D_H
#define KHDAYS_UI_KH2_MISSION_2D_H
#ifdef __cplusplus
extern "C" {
#endif
/* Each frame (display.c): the mission's pieces moved on by the KH2 ticks it ran. */
void khdays_ui_kh2_mission_2d_frame(unsigned frame);
/* The HUD part's emit (hud.c): their images. */
void khdays_ui_kh2_mission_2d_emit(void);
#ifdef __cplusplus
}
#endif
#endif
