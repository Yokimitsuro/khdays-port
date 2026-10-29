/* Saved HUD preferences, published atomically for the game without changing mod configuration. */
#ifndef KHDAYS_HUD_SETTINGS_H
#define KHDAYS_HUD_SETTINGS_H
#ifdef __cplusplus
extern "C" {
#endif
enum {
    KHDAYS_OPT_FONT,
    KHDAYS_OPT_MINIMAP,
    KHDAYS_OPT_PARTY_HP,
    KHDAYS_OPT_ENEMY_HP,
    KHDAYS_OPT_MISSION,
    KHDAYS_OPT_INFORMATION,
    KHDAYS_OPT_RETICLES,
    KHDAYS_OPT_LOCKED_ON,
    KHDAYS_OPT_NOTIFICATIONS,
    KHDAYS_OPT_HEART_POINTS,
    KHDAYS_OPT_PAUSE_TARGET,
    KHDAYS_OPT_INFORMATION_HUD,
    KHDAYS_OPT_UI_PADDING,
    KHDAYS_OPT_MISSION_VISIBILITY,
    KHDAYS_OPT_MISSION_ANCHOR,
    KHDAYS_OPT_MISSION_PAUSE_ANCHOR,
    KHDAYS_OPT_COUNT
};
extern const char *const khdays_hud_setting_keys[KHDAYS_OPT_COUNT];
void khdays_hud_settings_default(int values[KHDAYS_OPT_COUNT]);
void khdays_hud_settings_publish(const int values[KHDAYS_OPT_COUNT]);
int khdays_hud_setting(int setting);
unsigned khdays_hud_settings_generation(void);
#ifdef __cplusplus
}
#endif
#endif
