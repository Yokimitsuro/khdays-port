#pragma once
#include "kh2_hp_gauge.h"
#include "kh2_enemy_gauge.h"
#include <filesystem>
#include <string>

namespace khdays::ui::kh2 {
struct GaugeLabelConfig final {
    bool enabled = false;
    std::string archive, texture, entry;
    int animation = -1;
    std::array<int, 4> animations{-1, -1, -1, -1};
    float offset_x = 0, offset_y = 0, scale = 0.65f;
    float source_origin_x = 0, source_origin_y = 0, frames_per_tick = 1;
};
struct HpGaugeConfig final {
    std::string archive = "zz0field.2dd", texture = "US_zz0field_2dd1.png", entry = "gaug";
    bool enabled = true;
    float offset_x = 0, offset_y = 0, scale = 0.65f, canvas_padding = 16;
    float portrait_offset_x = 0, portrait_offset_y = 0;
    HpGaugeStyle style;
    GaugeLabelConfig player_icon, limit_text;
};
struct EnemyGaugeConfig final {
    bool enabled = false;
    std::string archive = "zz0field.2dd", texture = "US_zz0field_2dd1.png", entry = "gaug";
    float offset_x = 0, offset_y = 0, scale = 0.65f, frames_per_tick = 1;
    EnemyGaugeStyle style;
};
struct ReticleConfig final {
    bool enabled = false;
    std::string archive = "zz0field.2dd", texture = "US_zz0field_2dd0.png", entry = "fiel";
    int animation = -1, end_animation = -1;
    float offset_x = 0, offset_y = 0, scale = 0.65f;
    float source_origin_x = 0, source_origin_y = 0, frames_per_tick = 1;
};
struct InformationConfig final {
    bool enabled = false;
    std::string archive = "zz0field.2dd", texture = "US_zz0field_2dd1.png", entry = "gaug";
    int intro = 0, loop = 1, reveal = 2, outro = 3;
    float scale = 0.65f, offset_x = 0, offset_y = 0;
    float source_origin_x = 0, source_origin_y = 15, frames_per_tick = 1;
    float text_offset_x = 0, text_offset_y = 0, text_scale = 1;
    float clip_width = 0, clip_height = 64, hold_frames = 300; // 0 width: use the PC sequence's Ex parameter
    float scroll_wait_frames = 180, scroll_speed = 1;
};
InformationConfig load_information_config(const std::filesystem::path &path);
struct ObtainedConfig final {
    bool enabled = false;
    std::string archive = "zz0field.2dd", texture = "US_zz0field_2dd1.png", entry = "gaug";
    int animation = 76;
    bool loop = false;
    float offset_x = 0, offset_y = 0, scale = 0.65f, frames_per_tick = 1;
    float source_origin_x = 0, source_origin_y = 0;
    float text_offset_x = 0, text_offset_y = 0, text_scale = 1;
};
ObtainedConfig load_obtained_config(const std::filesystem::path &path);
ObtainedConfig load_notification_config(const std::filesystem::path &path, const std::string &name);
inline constexpr std::array<const char *, 43> heart_clip_names{"heart_idle",
                                                               "heart_active",
                                                               "pickup",
                                                               "background",
                                                               "streak",
                                                               "particles_0",
                                                               "particles_1",
                                                               "particles_2",
                                                               "chain_intro",
                                                               "chain_loop",
                                                               "chain_outro",
                                                               "bonus",
                                                               "digit_0",
                                                               "digit_1",
                                                               "digit_2",
                                                               "digit_3",
                                                               "digit_4",
                                                               "digit_5",
                                                               "digit_6",
                                                               "digit_7",
                                                               "digit_8",
                                                               "digit_9",
                                                               "small_digit_0",
                                                               "small_digit_1",
                                                               "small_digit_2",
                                                               "small_digit_3",
                                                               "small_digit_4",
                                                               "small_digit_5",
                                                               "small_digit_6",
                                                               "small_digit_7",
                                                               "small_digit_8",
                                                               "small_digit_9",
                                                               "minus",
                                                               "plus",
                                                               "gain",
                                                               "bonus_gain",
                                                               "total_gain",
                                                               "total_loss",
                                                               "total_gain_medium",
                                                               "total_gain_large",
                                                               "heart_grow",
                                                               "total_gain_final",
                                                               "chain_warning"};
struct HeartPointsConfig final {
    bool enabled = false;
    std::string archive = "hearts.2dd", texture = "hearts.png", entry = "hart";
    std::array<int, 43> animations{};
    float scale = .65f, offset_x = 0, offset_y = 0, source_origin_x = 0, source_origin_y = 0;
    float frames_per_tick = 1;
    float total_x = 16.533172f, total_y = 10.800396f, digit_spacing = 10.953388f;
    float rows_x = 76.454646f, rows_y = 22.5193f, row_spacing = 7.087486f;
};
HeartPointsConfig load_heart_points_config(const std::filesystem::path &path);
struct MissionPanelConfig final {
    bool enabled = false;
    bool pause_only = false;
    unsigned anchor = 0, pause_anchor = 0;
    float pause_offset_x = 0, pause_offset_y = 0;
    std::string archive = "mission.2dd", texture = "mission.png", entry = "miss";
    int reference = 0, intro = 1, outro = 2, track = 3, fill = 4, ready_fill = 5, full_fill = 6;
    int heading = 7, complete_heading = 8, full_marker = 9, minimum_marker = 10;
    int bead_empty = 11, bead_full = 12, unity = 13, rank = 14, chest = 15;
    float scale = .65f, offset_x = 0, offset_y = 0, source_origin_x = 0, source_origin_y = 15;
    float frames_per_tick = 1, text_scale = 1, counter_scale = 1, counters_scale = 1;
    float description_x = 30, description_y = 27, tally_x = 246, tally_y = 45;
    float counters_right = 260, counters_y = 27, icon_gap = 4, counter_gap = 6, counter_text_offset_y = 0;
    float beads_x = 30, beads_y = 45, bead_spacing = 16;
    float bar_start_x = 23, bar_end_x = 252, bar_y = 50;
    float heading_x = 23, heading_y = 40, full_marker_x = 254, full_marker_y = 50, minimum_marker_y = 50;
};
MissionPanelConfig load_mission_panel_config(const std::filesystem::path &path);
struct HudPresentationConfig final {
    bool keep_hud_during_information = false, align_hud_to_edges = false, pause_mission_target = false;
};
HudPresentationConfig load_hud_presentation_config(const std::filesystem::path &path);
// Player settings at the root; shared party settings and per-slot overrides below party.
std::array<HpGaugeConfig, 4> load_hp_gauge_configs(const std::filesystem::path &path);
HpGaugeConfig load_hp_gauge_config(const std::filesystem::path &path);
EnemyGaugeConfig load_enemy_gauge_config(const std::filesystem::path &path);
// Soft lock, hard lock, interact, chain, error; each can fail independently.
std::array<ReticleConfig, 5> load_reticle_configs(const std::filesystem::path &path);
} // namespace khdays::ui::kh2
