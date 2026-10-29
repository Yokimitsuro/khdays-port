#include "kh2_hp_gauge.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace khdays::ui::kh2 {
namespace {
namespace seq = assets::kh2;
constexpr float circle_rad = 0.078539819f;

seq::SequenceQuad mask(float x, float y, float w, float h, float rotation, std::uint32_t depth, float parent_x,
                       float parent_y, float geometry_scale)
{
    seq::SequenceQuad q;
    q.textured = false;
    q.color_write = false;
    q.depth_write = true;
    q.depth = depth;
    float c = std::cos(rotation), s = std::sin(rotation);
    // Ft4Base::calcXYZ snaps the other component when one is exactly +/-1.
    if (std::abs(s) == 1)
        c = 0;
    if (std::abs(c) == 1)
        s = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const float px = (x + ((i & 1) ? w : 0)) * geometry_scale;
        const float py = (y + ((i & 2) ? h : 0)) * geometry_scale;
        q.vertices[i].x = parent_x + float(int((px * c - py * s) * 16)) / 16 + 0.5f;
        q.vertices[i].y = parent_y + float(int((px * s + py * c) * 16)) / 16 + 0.5f;
    }
    if (s == 0 || c == 0) {
        // Cardinal cuts share the same half-open pixel edges as the generated gauge rectangles.
        const auto bounds = q.vertices;
        const float x0 = std::min(bounds[0].x, bounds[3].x), x1 = std::max(bounds[0].x, bounds[3].x);
        const float y0 = std::min(bounds[0].y, bounds[3].y), y1 = std::max(bounds[0].y, bounds[3].y);
        for (std::size_t i = 0; i < 4; ++i) {
            q.vertices[i].x = (i & 1) ? x1 : x0;
            q.vertices[i].y = (i & 2) ? y1 : y0;
        }
    }
    return q;
}

void append_mask(seq::SequenceFrame &out, const HpGaugeExtent &extent, std::uint32_t depth, float parent_x,
                 float parent_y, float geometry_scale)
{
    if (!extent.circle_mask)
        return;
    out.quads.push_back(mask(-64, -64, 64, 64, extent.angle, depth, parent_x, parent_y, geometry_scale));
    float width = 0, height = 50, y = 0;
    if (extent.hp < 20) {
        width = 64;
        height = 100;
        y = -50;
    } else if (extent.hp < 40)
        width = 64;
    if (width != 0)
        out.quads.push_back(mask(0, y, width, height, 0, depth, parent_x, parent_y, geometry_scale));
}

float looping_frame(const seq::SequenceData &data, std::size_t clip, float frame)
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

void append_sprite(seq::SequenceFrame &out, const seq::SequenceData &data, std::size_t clip, float frame,
                   const seq::SequencePose &pose, std::uint32_t depth)
{
    auto evaluated = seq::evaluate_sequence(data, clip, frame, pose);
    for (auto &q : evaluated.quads) {
        q.depth = depth;
        q.depth_test = depth != 0;
        out.quads.push_back(q);
    }
}

seq::SequencePose endpoint_pose(seq::SequencePose pose, const HpGaugeExtent &extent, const std::array<float, 2> &offset)
{
    pose.x += offset[0];
    pose.y += offset[1];
    pose.rotation[2] = extent.bar ? -4.712389f : -extent.angle;
    pose.x -= extent.bar_length;
    return pose;
}
} // namespace

HpGaugeExtent hp_gauge_extent(float days_hp, float geometry_scale)
{
    if (!std::isfinite(days_hp) || days_hp < 0 || !std::isfinite(geometry_scale) || geometry_scale <= 0)
        throw std::runtime_error("KH2 HP gauge: invalid HP");
    HpGaugeExtent extent;
    extent.hp = days_hp * 10.0f / 31.0f;
    if (!std::isfinite(extent.hp))
        throw std::runtime_error("KH2 HP gauge: HP exceeds the supported numeric range");
    extent.angle = extent.hp * circle_rad;
    extent.circle_mask = extent.hp < 60;
    extent.bar = extent.hp >= 60;
    extent.bar_scale = std::max(0.0f, extent.hp - 60) * 0.03f * geometry_scale;
    extent.bar_length = std::max(0.0f, extent.hp - 60) * 3 * geometry_scale;
    return extent;
}

seq::SequenceFrame evaluate_hp_gauge(const seq::SequenceData &data, const HpGaugeSample &sample,
                                     const HpGaugeStyle &style)
{
    if (data.clips.size() <= 137)
        throw std::runtime_error("KH2 HP gauge: gaug requires clips through 137");
    if (!std::isfinite(sample.max_hp) || sample.max_hp <= 0 || !std::isfinite(sample.hp) || sample.hp < 0 ||
        !std::isfinite(sample.previous_hp) || sample.previous_hp < 0 || !std::isfinite(sample.frame) ||
        sample.frame < 0 || !std::isfinite(sample.damage_frame) || sample.damage_frame < 0 ||
        !std::isfinite(sample.limit_hp) || sample.limit_hp < 0 || !std::isfinite(sample.limit_marker_hp) ||
        sample.limit_marker_hp < -1)
        throw std::runtime_error("KH2 HP gauge: invalid sample");
    const auto maximum = hp_gauge_extent(sample.max_hp, style.geometry_scale);
    const auto previous = hp_gauge_extent(std::min(sample.max_hp, sample.previous_hp), style.geometry_scale);
    const auto current = hp_gauge_extent(std::min(sample.max_hp, sample.hp), style.geometry_scale);
    const std::size_t status = sample.status == HpGaugeStatus::Alert ? 1 : sample.status == HpGaugeStatus::Dead ? 2 : 0;
    const auto &base_clips = style.center, &circle_clips = style.circle, &bar_clips = style.bar;
    std::size_t parent_clip = base_clips[status];
    float parent_frame = looping_frame(data, parent_clip, sample.frame);
    if (sample.reaction != HpGaugeReaction::None) {
        parent_clip = style.damage[sample.reaction == HpGaugeReaction::Small    ? 0
                                   : sample.reaction == HpGaugeReaction::Medium ? 1
                                                                                : 2];
        const float end = float(seq::sequence_frame_count(data, parent_clip));
        if (sample.damage_frame < end)
            parent_frame = sample.damage_frame;
        else {
            parent_clip = base_clips[status];
            parent_frame = looping_frame(data, parent_clip, sample.frame);
        }
    }
    if (style.play_intro && sample.entering && sample.frame < seq::sequence_frame_count(data, style.intro)) {
        parent_clip = style.intro;
        parent_frame = sample.frame;
    }
    if (sample.exiting) {
        parent_clip = style.fade;
        parent_frame = std::min(sample.frame, float(seq::sequence_frame_count(data, style.fade) - 1));
    }
    if (style.center_override >= 0) {
        parent_clip = std::size_t(style.center_override);
        parent_frame = looping_frame(data, parent_clip, sample.frame);
    }
    const auto parent = seq::evaluate_sequence(data, parent_clip, parent_frame);
    if (!parent.active)
        throw std::runtime_error("KH2 HP gauge: missing active parent animation");
    seq::SequencePose pose;
    pose.x = float(parent.active->x) - style.source_origin_x;
    pose.y = float(parent.active->y) - style.source_origin_y;
    pose.color_rate[3] = float(parent.active->color >> 24) / 128;
    seq::SequenceFrame out;
    out.active = parent.active;
    for (auto q : parent.quads) {
        for (auto &v : q.vertices) {
            v.x += style.center_offset[0] - style.source_origin_x;
            v.y += style.center_offset[1] - style.source_origin_y;
        }
        out.quads.push_back(q);
    }
    const auto base_pose = pose;
    pose.x += style.circle_offset[0];
    pose.y += style.circle_offset[1];
    out.quads.push_back(mask(-50, -50, 100, 100, 0, 0, pose.x, pose.y, style.geometry_scale));
    append_mask(out, maximum, 200, pose.x, pose.y, style.geometry_scale);
    append_sprite(out, data, circle_clips[status], looping_frame(data, circle_clips[status], sample.frame), pose, 100);
    append_mask(out, previous, 600, pose.x, pose.y, style.geometry_scale);
    append_sprite(out, data, style.circle_damage, sample.damage_frame, pose, 500);
    append_mask(out, current, 2000, pose.x, pose.y, style.geometry_scale);
    append_sprite(out, data, style.circle_fill, looping_frame(data, style.circle_fill, sample.frame), pose, 1000);
    auto bar_pose = base_pose;
    bar_pose.x += style.bar_offset[0];
    bar_pose.y += style.bar_offset[1];
    if (maximum.bar) {
        bar_pose.scale_x = maximum.bar_scale;
        append_sprite(out, data, bar_clips[status], looping_frame(data, bar_clips[status], sample.frame), bar_pose, 0);
    }
    if (previous.bar) {
        bar_pose.scale_x = previous.bar_scale;
        append_sprite(out, data, style.bar_damage, sample.damage_frame, bar_pose, 0);
    }
    if (current.bar) {
        bar_pose.scale_x = current.bar_scale;
        append_sprite(out, data, style.bar_fill, looping_frame(data, style.bar_fill, sample.frame), bar_pose, 0);
    }
    if (sample.limit_hp > 0 && sample.hp > 0 && style.limit_circle >= 0 && style.limit_bar >= 0) {
        const auto limit = hp_gauge_extent(std::min({sample.limit_hp, sample.hp, sample.max_hp}), style.geometry_scale);
        // The existing 2000-depth current-HP masks also clip the yellow layer.
        append_mask(out, limit, 4000, pose.x, pose.y, style.geometry_scale);
        const auto circle_clip = std::size_t(style.limit_circle), bar_clip = std::size_t(style.limit_bar);
        append_sprite(out, data, circle_clip, looping_frame(data, circle_clip, sample.frame), pose, 1000);
        if (limit.bar) {
            bar_pose.scale_x = limit.bar_scale;
            append_sprite(out, data, bar_clip, looping_frame(data, bar_clip, sample.frame), bar_pose, 0);
        }
    }
    const auto endpoint = endpoint_pose(base_pose, maximum, style.cap_offset);
    append_sprite(out, data, style.cap, looping_frame(data, style.cap, sample.frame), endpoint, 0);
    if (style.limit_marker >= 0 && sample.limit_marker_hp >= 0) {
        const auto marker = hp_gauge_extent(std::min(sample.limit_marker_hp, sample.max_hp), style.geometry_scale);
        auto marker_pose = endpoint_pose(base_pose, marker, style.limit_marker_offset);
        const auto &offset = marker.bar ? style.bar_offset : style.circle_offset;
        marker_pose.x += offset[0];
        marker_pose.y += offset[1];
        const auto clip = std::size_t(style.limit_marker);
        append_sprite(out, data, clip, looping_frame(data, clip, sample.frame), marker_pose, 0);
    }
    return out;
}

} // namespace khdays::ui::kh2
