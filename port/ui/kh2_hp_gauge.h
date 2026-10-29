#pragma once

#include "khdays/assets/kh2_sequence.h"

namespace khdays::ui::kh2 {

struct HpGaugeExtent final {
    float hp = 0; // KH2 units: 186 Days HP = 60
    float angle = 0;
    float bar_scale = 0, bar_length = 0;
    bool circle_mask = true, bar = false;
};

HpGaugeExtent hp_gauge_extent(float days_hp, float geometry_scale = 1);

enum class HpGaugeStatus { Normal, Alert, Dead };
enum class HpGaugeReaction { None, Small, Medium, Large };

struct HpGaugeSample final {
    float hp = 186, max_hp = 186, previous_hp = 186;
    float frame = 0;
    float damage_frame = 100;   // the damage clips' intentionally blank restart
    float limit_hp = 0;         // Days HP; 0 disables the limit overlay
    float limit_marker_hp = -1; // Days HP; -1 hides the native limit timer divider
    HpGaugeStatus status = HpGaugeStatus::Normal;
    HpGaugeReaction reaction = HpGaugeReaction::None;
    bool entering = false;
    bool exiting = false;
};

struct HpGaugeStyle final {
    std::array<std::size_t, 3> center{137, 132, 137}, circle{121, 122, 121}, bar{124, 125, 124};
    std::array<std::size_t, 3> damage{135, 134, 133}; // small, medium, large
    std::size_t circle_damage = 126, bar_damage = 127, circle_fill = 128, bar_fill = 129, cap = 123;
    std::size_t intro = 136, fade = 138;
    int center_override = -1;
    bool play_intro = false;
    int limit_circle = -1, limit_bar = -1; // optional mod-added clips
    int limit_marker = -1;
    float source_origin_x = 455, source_origin_y = 355;
    float geometry_scale = 1; // intrinsic art/mask size; independent of the JSON display scale
    std::array<float, 2> center_offset{}, circle_offset{}, bar_offset{}, cap_offset{}, limit_marker_offset{};
};

// HP policy and depth-mask geometry stay outside the reusable SEQD evaluator.
// The returned commands have a local origin at the gauge's circular center.
assets::kh2::SequenceFrame evaluate_hp_gauge(const assets::kh2::SequenceData &data, const HpGaugeSample &sample,
                                             const HpGaugeStyle &style = {});

} // namespace khdays::ui::kh2
