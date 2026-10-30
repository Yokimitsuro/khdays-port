/* The window publishes saved preferences; renderers retain their own JSON enable flags. */
#include "hud_settings.h"
#include <SDL3/SDL.h>

const char *const khdays_hud_setting_keys[KHDAYS_OPT_COUNT] = {"hd_font",
                                                               "kh2_minimap",
                                                               "party_hp_sequence",
                                                               "enemy_hp_sequence",
                                                               "mission_panel_sequence",
                                                               "information_sequence",
                                                               "target_sequence",
                                                               "locked_on_sequence",
                                                               "notification_sequences",
                                                               "heart_points_sequence",
                                                               "kh2_prizes",
                                                               "pause_target",
                                                               "information_hud",
                                                               "ui_padding",
                                                               "mission_panel_visibility",
                                                               "mission_panel_anchor",
                                                               "mission_panel_pause_anchor"};
/* Unpublished tools retain JSON presentation; the normal profile publishes explicit defaults. */
static SDL_AtomicInt live[KHDAYS_OPT_COUNT] = {{1}, {1}, {1}, {1}, {1},  {1},  {1}, {1}, {1},
                                               {1}, {1}, {1}, {1}, {-1}, {-1}, {0}, {0}};
static SDL_AtomicInt generation;

/* The HD fonts on, as before these settings; the KH2 replacements, and the play during INFORMATION, the player's to
 * turn on. */
void khdays_hud_settings_default(int values[KHDAYS_OPT_COUNT])
{
    for (int i = 0; i < KHDAYS_OPT_COUNT; ++i)
        values[i] = 0;
    values[KHDAYS_OPT_FONT] = 1;
    values[KHDAYS_OPT_PRIZES] = 1; /* made only with the HD menus' HD PACK: shown once made */
    values[KHDAYS_OPT_MISSION_ANCHOR] = 1;
    values[KHDAYS_OPT_MISSION_PAUSE_ANCHOR] = 8;
}
void khdays_hud_settings_publish(const int values[KHDAYS_OPT_COUNT])
{
    int changed = 0;
    for (int i = 0; i < KHDAYS_OPT_COUNT; ++i) {
        const int low = i >= KHDAYS_OPT_MISSION_ANCHOR ? 1 : 0;
        const int high = i >= KHDAYS_OPT_MISSION_ANCHOR ? 9 : 1;
        const int v = SDL_clamp(values[i], low, high);
        changed |= SDL_SetAtomicInt(&live[i], v) != v;
    }
    if (changed)
        SDL_AddAtomicInt(&generation, 1);
}
int khdays_hud_setting(int setting)
{
    return setting >= 0 && setting < KHDAYS_OPT_COUNT ? SDL_GetAtomicInt(&live[setting]) : 0;
}
unsigned khdays_hud_settings_generation(void)
{
    return (unsigned)SDL_GetAtomicInt(&generation);
}
