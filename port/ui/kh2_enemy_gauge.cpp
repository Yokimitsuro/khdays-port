// GAUGE_ENEMY's composition; Days supplies its own HP capacity, never KH2's 200-HP gameplay units.
#include "kh2_enemy_gauge.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace khdays::ui::kh2 {
namespace {
namespace seq = assets::kh2;
float timeline(const seq::SequenceData &data, std::size_t clip, float frame)
{
    const auto &c = data.clips.at(clip);
    const float end = float(seq::sequence_frame_count(data, clip));
    if (frame < end)
        return frame;
    if (c.loop == 1)
        return end - 1;
    const float restart = float(c.restart);
    return restart >= end ? restart : restart + std::fmod(frame - end, end - restart);
}
void append(seq::SequenceFrame &out, const seq::SequenceData &data, std::size_t clip, float frame,
            const seq::SequencePose &pose)
{
    auto draw = seq::evaluate_sequence(data, clip, timeline(data, clip, frame), pose);
    out.quads.insert(out.quads.end(), draw.quads.begin(), draw.quads.end());
}
} // namespace

EnemyGaugeExtent enemy_gauge_extent(int hp, int capacity)
{
    if (hp < 0 || capacity <= 0)
        throw std::runtime_error("KH2 enemy gauge: invalid HP capacity");
    const int tanks = hp ? (hp - 1) / capacity : 0;
    return {tanks, float(hp - tanks * capacity) * 200.0f / float(capacity)};
}

assets::kh2::SequenceFrame evaluate_enemy_gauge(const assets::kh2::SequenceData &data, const EnemyGaugeSample &sample,
                                                const EnemyGaugeStyle &style)
{
    if (sample.max_hp <= 0 || sample.hp < 0 || sample.previous_hp < 0 || sample.bar_capacity <= 0 ||
        !std::isfinite(sample.frame) || sample.frame < 0 || !std::isfinite(sample.damage_frame) ||
        sample.damage_frame < 0)
        throw std::runtime_error("KH2 enemy gauge: invalid sample");
    const auto maximum = enemy_gauge_extent(sample.max_hp, sample.bar_capacity);
    const auto current = enemy_gauge_extent(std::min(sample.hp, sample.max_hp), sample.bar_capacity);
    const auto previous = enemy_gauge_extent(std::min(sample.previous_hp, sample.max_hp), sample.bar_capacity);
    const float length = current.bar > maximum.bar || current.tanks != maximum.tanks ? 200 : maximum.bar;
    const float red = current.tanks != previous.tanks ? 200 : previous.bar;
    auto out = seq::evaluate_sequence(data, style.background, timeline(data, style.background, sample.frame));
    if (!out.active)
        throw std::runtime_error("KH2 enemy gauge: missing active parent");
    for (auto &q : out.quads)
        for (auto &v : q.vertices) {
            v.x -= style.source_origin_x;
            v.y -= style.source_origin_y;
        }
    seq::SequencePose parent;
    parent.x = float(out.active->x) - style.source_origin_x;
    parent.y = float(out.active->y) - style.source_origin_y;
    parent.scale_x = out.active->scale_x;
    parent.scale_y = out.active->scale_y;
    parent.alpha = std::clamp(float(out.active->color >> 24) / 128, 0.0f, 1.0f);
    auto bar = parent;
    bar.x += style.bar_offset_x * parent.scale_x;
    bar.y += style.bar_offset_y * parent.scale_y;
    if (maximum.tanks && current.tanks == maximum.tanks)
        append(out, data, style.preview, sample.frame, bar);
    bar.x -= length * parent.scale_x;
    bar.scale_x = length * 0.01f * parent.scale_x;
    append(out, data, style.bar_background, sample.frame, bar);
    bar.scale_x = parent.scale_x;
    append(out, data, style.cap, sample.frame, bar);
    bar.x += (length - red) * parent.scale_x;
    bar.scale_x = red * 0.01f * parent.scale_x;
    if (sample.previous_hp > sample.hp)
        append(out, data, style.damage, sample.damage_frame, bar);
    bar.x += (red - current.bar) * parent.scale_x;
    bar.scale_x = current.bar * 0.01f * parent.scale_x;
    if (current.bar > 0)
        append(out, data, style.fill, sample.frame, bar);
    const float spacing = float(data.clips.at(style.tank_empty).parameters[4]);
    if (spacing <= 0)
        throw std::runtime_error("KH2 enemy gauge: tank spacing must be positive");
    for (int i = 0; i < maximum.tanks; ++i) {
        auto tank = parent;
        tank.x += (style.tank_offset_x - float(i) * spacing) * parent.scale_x;
        tank.y += style.tank_offset_y * parent.scale_y;
        append(out, data, style.tank_empty, sample.frame, tank);
        if (i < current.tanks)
            append(out, data, style.tank_full, sample.frame, tank);
        else if (i < previous.tanks)
            append(out, data, style.tank_damage, sample.damage_frame, tank);
    }
    return out;
}
} // namespace khdays::ui::kh2
