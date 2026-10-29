#include "khdays/assets/kh2_sequence.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace khdays::assets::kh2 {
namespace {

[[noreturn]] void fail(const char* why) {
    throw std::runtime_error(std::string("KH2 SEQD: ") + why);
}

class Bytes final {
public:
    Bytes(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {
        if (data == nullptr)
            fail("missing data");
    }
    void range(std::size_t at, std::size_t length) const {
        if (at > size_ || length > size_ - at)
            fail("truncated resource");
    }
    std::uint16_t u16(std::size_t at) const {
        range(at, 2);
        return std::uint16_t(data_[at] | (unsigned(data_[at + 1]) << 8));
    }
    std::uint32_t u32(std::size_t at) const {
        range(at, 4);
        return std::uint32_t(u16(at)) | (std::uint32_t(u16(at + 2)) << 16);
    }
    std::int32_t i32(std::size_t at) const {
        return std::bit_cast<std::int32_t>(u32(at));
    }
    float f32(std::size_t at) const {
        const auto value = std::bit_cast<float>(u32(at));
        if (!std::isfinite(value))
            fail("non-finite animation value");
        return value;
    }
    SequenceRange refs(std::size_t at) const {
        return {u16(at), u16(at + 2)};
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
};

template <typename T> void refs(SequenceRange range, const std::vector<T>& table) {
    if (range.first > table.size() || range.count > table.size() - range.first)
        fail("invalid table reference");
}

bool bit(const SequenceAnimation& a, unsigned n) {
    return (a.flags & (1u << n)) != 0;
}
float mix(const std::array<float, 2>& v, float t) {
    return std::lerp(v[0], v[1], t);
}

int truncate(double value) {
    if (!std::isfinite(value) || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        fail("transformation exceeds the coordinate range");
    return static_cast<int>(value);
}

int mix(const std::array<std::int32_t, 2>& v, float t) {
    return truncate(float(v[0]) + float(truncate((float(v[1]) - float(v[0])) * t)));
}

float rate(const SequenceAnimation& a, float frame) {
    const float length = float(a.time[1]) - float(a.time[0]);
    float t = length == 0 ? 1 : (frame - float(a.time[0])) / length;
    if (!bit(a, 0)) {
        const float r = 1 - t;
        t = 1 - (3 * r * t * t * (1 - a.curve[3]) + r * r * r + 3 * r * r * t * (1 - a.curve[1]));
    }
    return t;
}

std::uint32_t color(const SequenceAnimation& a, float t, const SequencePose& pose, bool active) {
    std::uint32_t out = 0;
    for (unsigned c = 0; c < 4; ++c) {
        int value = int((a.colors[0] >> (8 * c)) & 255u);
        if (!active && bit(a, 10))
            value = 128;
        else if (!bit(a, 7))
            value = truncate(double(value) + truncate(float(int((a.colors[1] >> (8 * c)) & 255u) - value) * t));
        value = truncate(float(value) * pose.color_rate[c]);
        out |= std::uint32_t(std::clamp(value, 0, 255)) << (8 * c);
    }
    return out;
}

std::uint32_t multiply(std::uint32_t a, std::uint32_t b, float alpha) {
    std::uint32_t out = 0;
    for (unsigned s = 0; s < 32; s += 8) {
        unsigned v = std::min(255u, (((a >> s) & 255u) * ((b >> s) & 255u)) >> 7);
        if (s == 24 && alpha != 1)
            v = unsigned(std::clamp(truncate(float(v) * alpha), 0, 255));
        out |= v << s;
    }
    return out;
}

struct Transform final {
    float xx = 1, xy = 0, yx = 0, yy = 1;
    float sx = 1, sy = 1;
    int x = 0, y = 0, ox = 0, oy = 0;
};

Transform transform(const SequenceAnimation& a, float t, const SequencePose& pose, bool move_base) {
    Transform m;
    std::array<float, 3> r = pose.rotation;
    if (!bit(a, 5)) {
        r[0] += mix(a.rotation_x, t);
        r[1] += mix(a.rotation_y, t);
        r[2] += mix(a.rotation_z, t);
    }
    const float sin_x = std::sin(r[0]), cos_x = std::cos(r[0]);
    const float sin_y = std::sin(r[1]), cos_y = std::cos(r[1]);
    float sin_z = std::sin(r[2]), cos_z = std::cos(r[2]);
    if (cos_z == 1)
        sin_z = 0;
    else if (sin_z == 1)
        cos_z = 0;
    m.xx = cos_y * cos_z;
    m.yx = -cos_y * sin_z;
    m.xy = sin_x * sin_y * cos_z + cos_x * sin_z;
    m.yy = cos_x * cos_z - sin_x * sin_y * sin_z;
    m.sx = pose.scale * pose.scale_x;
    m.sy = pose.scale * pose.scale_y;
    if (!bit(a, 6)) {
        m.sx *= mix(a.scale, t) * mix(a.scale_x, t);
        m.sy *= mix(a.scale, t) * mix(a.scale_y, t);
    }
    m.x = move_base ? mix(a.x, t) : a.x[0];
    m.y = move_base ? mix(a.y, t) : a.y[0];
    if (move_base && !bit(a, 4)) {
        // UpdateActive (0xC32D8) uses 0x40490FDB: each count is a half-turn.
        constexpr float pi = 3.14159265358979323846f;
        const float x = pi * t * float(a.sway_count[0]), y = pi * t * float(a.sway_count[1]);
        m.x = truncate(float(m.x) + float(truncate(mix(a.sway_x, t) * (bit(a, 3) ? std::cos(x) : std::sin(x)))));
        m.y = truncate(float(m.y) + float(truncate(mix(a.sway_y, t) * (bit(a, 3) ? std::cos(y) : std::sin(y)))));
    }
    if (!bit(a, 14)) {
        m.ox = mix(a.offset_x, t);
        m.oy = mix(a.offset_y, t);
    }
    return m;
}

std::array<float, 2> point(const Transform& m, float x, float y) {
    x = (x + float(m.ox)) * 16 * m.sx;
    y = (y + float(m.oy)) * 16 * m.sy;
    return {float(truncate(x * m.xx + y * m.xy)) / 16, float(truncate(x * m.yx + y * m.yy)) / 16};
}

}  // namespace

SequenceData read_sequence(const std::uint8_t* data, std::size_t size) {
    const Bytes b(data, size);
    b.range(0, 0x30);
    if (b.u32(0) != 0x44514553)
        fail("missing SEQD header");
    // The supplied PC face resources use both versions with the same five table layouts.
    if (b.u32(4) != 0x150 && b.u32(4) != 0x160)
        fail("unsupported version (expected PC 0x150 or 0x160)");
    constexpr std::array<std::size_t, 5> strides{44, 20, 4, 144, 36};
    std::array<std::size_t, 5> counts{}, offsets{};
    for (std::size_t t = 0; t < 5; ++t) {
        counts[t] = b.u32(8 + 8 * t);
        offsets[t] = b.u32(12 + 8 * t);
        if (offsets[t] > size || counts[t] > (size - offsets[t]) / strides[t])
            fail("truncated table");
        if (counts[t] && offsets[t] < 0x30)
            fail("table overlaps the header");
        for (std::size_t j = 0; j < t; ++j) {
            if (counts[t] && counts[j] && offsets[t] < offsets[j] + counts[j] * strides[j] &&
                offsets[j] < offsets[t] + counts[t] * strides[t])
                fail("overlapping tables");
        }
    }
    SequenceData out;
    out.parts.resize(counts[0]);
    out.groups.resize(counts[1]);
    out.sprites.resize(counts[2]);
    out.animations.resize(counts[3]);
    out.clips.resize(counts[4]);
    for (std::size_t i = 0; i < out.parts.size(); ++i) {
        const auto p = offsets[0] + i * 44;
        auto& v = out.parts[i];
        v.palette = b.u32(p);
        for (std::size_t j = 0; j < 4; ++j) {
            v.uv[j] = b.i32(p + 4 + 4 * j);
            v.colors[j] = b.u32(p + 28 + 4 * j);
        }
        v.scroll = {b.f32(p + 20), b.f32(p + 24)};
    }
    for (std::size_t i = 0; i < out.groups.size(); ++i) {
        const auto p = offsets[1] + i * 20;
        auto& v = out.groups[i];
        for (std::size_t j = 0; j < 4; ++j)
            v.rect[j] = b.i32(p + 4 * j);
        v.part = b.u32(p + 16);
        if (v.part >= out.parts.size())
            fail("invalid part index");
    }
    for (std::size_t i = 0; i < out.sprites.size(); ++i) {
        out.sprites[i] = b.refs(offsets[2] + i * 4);
        refs(out.sprites[i], out.groups);
    }
    for (std::size_t i = 0; i < out.animations.size(); ++i) {
        const auto p = offsets[3] + i * 144;
        auto& v = out.animations[i];
        v.flags = b.u32(p);
        v.sprite = b.u32(p + 4);
        if (v.sprite >= out.sprites.size())
            fail("invalid sprite index");
        v.time = {b.i32(p + 8), b.i32(p + 12)};
        if (v.time[0] < 0 || v.time[1] < v.time[0] || v.time[1] == std::numeric_limits<int>::max())
            fail("invalid animation interval");
        auto integers = [&](std::size_t at) { return std::array{b.i32(p + at), b.i32(p + at + 4)}; };
        auto floats = [&](std::size_t at) { return std::array{b.f32(p + at), b.f32(p + at + 4)}; };
        v.x = integers(16);
        v.y = integers(24);
        v.offset_x = integers(32);
        v.offset_y = integers(40);
        v.rotation_x = floats(48);
        v.rotation_y = floats(56);
        v.rotation_z = floats(64);
        v.scale = floats(72);
        v.scale_x = floats(80);
        v.scale_y = floats(88);
        for (std::size_t j = 0; j < 4; ++j)
            v.curve[j] = b.f32(p + 96 + 4 * j);
        v.sway_x = floats(112);
        v.sway_y = floats(120);
        v.sway_count = {b.u16(p + 128), b.u16(p + 130)};
        v.blend = b.u32(p + 132);
        if (v.blend > 2)
            fail("unsupported blend mode");
        v.colors = {b.u32(p + 136), b.u32(p + 140)};
    }
    for (std::size_t i = 0; i < out.clips.size(); ++i) {
        const auto p = offsets[4] + i * 36;
        auto& v = out.clips[i];
        v.animations = b.refs(p);
        refs(v.animations, out.animations);
        v.loop = b.u16(p + 4);
        v.flags = b.u16(p + 6);
        v.restart = b.u32(p + 8);
        v.end = b.u32(p + 12);
        for (std::size_t j = 0; j < 5; ++j)
            v.parameters[j] = b.i32(p + 16 + 4 * j);
    }
    return out;
}

std::array<int, 2> read_sequence_image_size(const std::uint8_t* data, std::size_t size) {
    const Bytes b(data, size);
    b.range(0, 64);
    if (b.u32(0) != 0x44474d49 || b.u32(4) != 0x100)
        fail("unsupported IMGD header");
    const int w = b.u16(28), h = b.u16(30);
    if (w == 0 || h == 0)
        fail("empty image dimensions");
    return {w, h};
}

int sequence_frame_count(const SequenceData& data, std::size_t clip) {
    const auto& c = data.clips.at(clip);
    int last = 0;
    for (std::size_t i = c.animations.first; i < std::size_t(c.animations.first) + c.animations.count; ++i) {
        const auto& a = data.animations[i];
        if (last < a.time[1])
            last = a.time[1] - int(bit(a, 15));
    }
    if (c.end != 0 && std::uint32_t(last) > c.end)
        last = static_cast<int>(c.end);
    return last + 1;
}

SequenceFrame evaluate_sequence(const SequenceData& data, std::size_t clip, float frame, const SequencePose& pose) {
    const auto& c = data.clips.at(clip);
    frame = float(truncate(frame));
    SequenceFrame out;
    bool move_base = false;
    for (std::size_t i = c.animations.first; i < std::size_t(c.animations.first) + c.animations.count; ++i) {
        const auto& a = data.animations[i];
        move_base |= !bit(a, 4) || !bit(a, 12);
    }
    for (std::size_t i = c.animations.first; i < std::size_t(c.animations.first) + c.animations.count; ++i) {
        const auto& a = data.animations[i];
        if (frame == float(a.time[0]) && bit(a, 19))
            out.tags.push_back(i);
        if (frame < float(a.time[0]) || frame > float(a.time[1]))
            continue;
        const float t = rate(a, frame);
        const auto m = transform(a, t, pose, move_base);
        if (!out.active && bit(a, 1)) {
            const auto p = point(m, 0, 0);
            out.active = SequenceActive{truncate(double(m.x) + truncate(p[0])), truncate(double(m.y) + truncate(p[1])),
                                        m.sx, m.sy, color(a, t, pose, true)};
        }
        if (frame == float(a.time[1]) && bit(a, 15))
            continue;
        const auto sprite = data.sprites[a.sprite];
        // Reused table ranges can otherwise expand a small file into billions of quads.
        constexpr std::size_t max_frame_quads = 65536;
        if (sprite.count > max_frame_quads - out.quads.size())
            fail("frame exceeds 65536 quads");
        for (std::size_t g = sprite.first; g < std::size_t(sprite.first) + sprite.count; ++g) {
            const auto& group = data.groups[g];
            const auto& part = data.parts[group.part];
            if (part.palette != 0 || part.scroll[0] != 0 || part.scroll[1] != 0)
                fail("palette selection and scrolling are not implemented");
            SequenceQuad q;
            q.blend = a.blend;
            q.linear_filter = !bit(a, 2);
            const auto tint = color(a, t, pose, false);
            for (std::size_t v = 0; v < 4; ++v) {
                const auto p = point(m, float(group.rect[(v & 1) ? 2 : 0]), float(group.rect[(v & 2) ? 3 : 1]));
                q.vertices[v] = {pose.x + float(m.x) + p[0] + 0.5f, pose.y + float(m.y) + p[1] + 0.5f,
                                 float(part.uv[(v & 1) ? 2 : 0]) + 0.5f, float(part.uv[(v & 2) ? 3 : 1]) + 0.5f,
                                 multiply(part.colors[v], tint, pose.alpha)};
            }
            out.quads.push_back(q);
        }
    }
    return out;
}

void SequencePlayer::select(const SequenceData& data, std::size_t clip, bool force) {
    const auto& c = data.clips.at(clip);
    if (!force && selected_ && clip == clip_)
        return;
    clip_ = clip;
    frame_ = 0;
    remaining_ = c.loop;
    completed_ = 0;
    selected_ = true;
    stopped_ = false;
}

void SequencePlayer::advance(const SequenceData& data, float frames) {
    if (!std::isfinite(frames) || frames < 0)
        fail("invalid playback delta");
    if (!playing())
        return;
    const auto& c = data.clips.at(clip_);
    const float end = float(sequence_frame_count(data, clip_));
    frame_ += frames;
    if (frame_ >= end) {
        frame_ = float(c.restart);  // source Control discards overshoot
        if (remaining_ != 0) {
            --remaining_;
            ++completed_;
            if (remaining_ == 0) {
                frame_ = end - 1;
                stopped_ = true;
            }
        }
    }
}

void SequencePlayer::seek(float frame) {
    if (!std::isfinite(frame) || frame < 0)
        fail("invalid seek frame");
    frame_ = frame;
}

}  // namespace khdays::assets::kh2
