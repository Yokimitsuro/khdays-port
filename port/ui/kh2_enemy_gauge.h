#pragma once
#include "khdays/assets/kh2_sequence.h"

namespace khdays::ui::kh2 {
struct EnemyGaugeStyle final {
    std::size_t background = 38, preview = 42, bar_background = 39, cap = 40;
    std::size_t damage = 41, fill = 43, tank_full = 44, tank_empty = 45, tank_damage = 46;
    float source_origin_x = 498, source_origin_y = 31;
    float bar_offset_x = 0, bar_offset_y = 0, tank_offset_x = 0, tank_offset_y = 0;
};
struct EnemyGaugeSample final {
    int hp = 1, max_hp = 1, previous_hp = 1, bar_capacity = 250;
    float frame = 0, damage_frame = 100;
};
struct EnemyGaugeExtent final {
    int tanks = 0;
    float bar = 0;
};
EnemyGaugeExtent enemy_gauge_extent(int hp, int capacity);
assets::kh2::SequenceFrame evaluate_enemy_gauge(const assets::kh2::SequenceData &data, const EnemyGaugeSample &sample,
                                                const EnemyGaugeStyle &style = {});
} // namespace khdays::ui::kh2
