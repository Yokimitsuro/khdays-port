#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace khdays::assets::kh2 {

// PC SEQD 0x150/0x160. Offsets and execution follow YI::SPRITE/SEQUENCE in the
// KH2 HD debug executable; runtime objects and PS3 bitfield layouts are not disk types.
struct SequencePart final {
    std::uint32_t palette = 0;
    std::array<std::int32_t, 4> uv{};
    std::array<float, 2> scroll{};
    std::array<std::uint32_t, 4> colors{};  // RGBA, red in the low byte; 128 neutral
};

struct SequenceGroup final {
    std::array<std::int32_t, 4> rect{};
    std::uint32_t part = 0;
};

struct SequenceRange final {
    std::uint16_t first = 0, count = 0;
};

struct SequenceAnimation final {
    std::uint32_t flags = 0, sprite = 0;
    std::array<std::int32_t, 2> time{}, x{}, y{}, offset_x{}, offset_y{};
    std::array<float, 2> rotation_x{}, rotation_y{}, rotation_z{}, scale{}, scale_x{}, scale_y{};
    std::array<float, 4> curve{};  // evaluator uses entries 1 and 3
    std::array<float, 2> sway_x{}, sway_y{};
    std::array<std::uint16_t, 2> sway_count{};
    std::uint32_t blend = 0;
    std::array<std::uint32_t, 2> colors{};
};

struct SequenceClip final {
    SequenceRange animations;
    std::uint16_t loop = 0, flags = 0;
    std::uint32_t restart = 0, end = 0;
    std::array<std::int32_t, 5> parameters{};  // x, y, size, cr, ex
};

struct SequenceData final {
    std::vector<SequencePart> parts;
    std::vector<SequenceGroup> groups;
    std::vector<SequenceRange> sprites;
    std::vector<SequenceAnimation> animations;
    std::vector<SequenceClip> clips;
};

SequenceData read_sequence(const std::uint8_t* data, std::size_t size);
std::array<int, 2> read_sequence_image_size(const std::uint8_t* data, std::size_t size);

struct SequencePose final {
    float x = 0, y = 0;
    std::array<float, 3> rotation{};
    float scale = 1, scale_x = 1, scale_y = 1;
    std::array<float, 4> color_rate{1, 1, 1, 1};
    float alpha = 1;
    // A host's own reshaping, none by default (the Menu Overhaul's buttons and pause band): a sprite's edges right
    // of `widen_from` moved `widen` further before its transform, and its turned height times `stretch_y` about
    // its animation's place.
    float widen = 0, widen_from = 0, stretch_y = 1;
};

struct SequenceVertex final {
    float x = 0, y = 0, u = 0, v = 0;
    std::uint32_t color = 0x80808080;
};

struct SequenceQuad final {
    std::array<SequenceVertex, 4> vertices{};  // TL, TR, BL, BR before transforms
    std::uint32_t blend = 0;
    std::uint32_t depth = 0;
    bool depth_test = false, depth_write = false, color_write = true;
    bool textured = true, linear_filter = true;
};

struct SequenceActive final {
    int x = 0, y = 0;
    float scale_x = 1, scale_y = 1;
    std::uint32_t color = 0x80808080;
};

struct SequenceFrame final {
    std::vector<SequenceQuad> quads;
    std::optional<SequenceActive> active;
    std::vector<std::size_t> tags;  // tagged animation starts at this evaluated frame
};

int sequence_frame_count(const SequenceData& data, std::size_t clip);
SequenceFrame evaluate_sequence(const SequenceData& data, std::size_t clip, float frame, const SequencePose& pose = {});

// Advancement is in source timeline units, independently of rendering or presentation rate.
class SequencePlayer final {
public:
    void select(const SequenceData& data, std::size_t clip, bool force = false);
    void advance(const SequenceData& data, float frames);
    void seek(float frame);
    std::size_t clip() const {
        return clip_;
    }
    float frame() const {
        return frame_;
    }
    bool playing() const {
        return selected_ && !stopped_;
    }
    unsigned completed_loops() const {
        return completed_;
    }

private:
    std::size_t clip_ = 0;
    float frame_ = 0;
    unsigned remaining_ = 0, completed_ = 0;
    bool selected_ = false, stopped_ = false;
};

}  // namespace khdays::assets::kh2
