#include "khdays/assets/animation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// NSBCA (JNT0 / J0AC) skeletal-animation decoder. Self-contained in the style
// of the other asset modules. The binary layout follows the Nintendo DS format
// as documented by apicula (0BSD); this is an independent reimplementation.

namespace {

using ByteVector = std::vector<std::uint8_t>;

std::uint16_t read_u16(const ByteVector& d, const std::size_t o) {
    if (o + 2U > d.size()) {
        throw std::runtime_error("NSBCA: read past end (u16)");
    }
    return static_cast<std::uint16_t>(d[o] | (d[o + 1U] << 8U));
}

std::uint32_t read_u32(const ByteVector& d, const std::size_t o) {
    if (o + 4U > d.size()) {
        throw std::runtime_error("NSBCA: read past end (u32)");
    }
    return static_cast<std::uint32_t>(d[o])
        | (static_cast<std::uint32_t>(d[o + 1U]) << 8U)
        | (static_cast<std::uint32_t>(d[o + 2U]) << 16U)
        | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

ByteVector read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("cannot open animation file: " + path.string());
    }
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot size animation file: " + path.string());
    }
    ByteVector data(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!data.empty()) {
        stream.read(reinterpret_cast<char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    }
    return data;
}

bool has_magic(const ByteVector& d, const std::size_t o, const std::string_view m) {
    if (o + m.size() > d.size()) {
        return false;
    }
    return std::equal(m.begin(), m.end(), d.begin() + static_cast<std::ptrdiff_t>(o));
}

std::int32_t sign_extend(const std::uint32_t value, const unsigned bits) {
    const std::uint32_t mask = bits >= 32U ? 0xFFFFFFFFU : ((1U << bits) - 1U);
    const std::uint32_t masked = value & mask;
    const std::uint32_t sign_bit = 1U << (bits - 1U);
    if ((masked & sign_bit) != 0U) {
        return static_cast<std::int32_t>(masked | ~mask);
    }
    return static_cast<std::int32_t>(masked);
}

// Signed fixed-point: `total` significant bits, `frac` fractional bits.
float fixed(const std::uint32_t raw, const unsigned total, const unsigned frac) {
    return static_cast<float>(sign_extend(raw, total))
        / static_cast<float>(1U << frac);
}

struct SectionView final {
    std::size_t offset = 0;
    std::size_t size = 0;
};

std::unordered_map<std::string, SectionView> parse_sections(const ByteVector& d) {
    if (d.size() < 0x10U || !has_magic(d, 0U, "BCA0")) {
        throw std::runtime_error("resource is not a BCA0/NSBCA file");
    }
    const auto count = read_u16(d, 0x0EU);
    if (count == 0U || count > 64U) {
        throw std::runtime_error("invalid NSBCA section count");
    }
    std::unordered_map<std::string, SectionView> sections;
    for (std::uint16_t i = 0U; i < count; ++i) {
        const auto off = static_cast<std::size_t>(read_u32(d, 0x10U + i * 4U));
        if (off + 8U > d.size()) {
            throw std::runtime_error("NSBCA section offset exceeds file");
        }
        const auto size = static_cast<std::size_t>(read_u32(d, off + 4U));
        const std::string magic{reinterpret_cast<const char*>(d.data() + off), 4U};
        sections[magic] = SectionView{off, size};
    }
    return sections;
}

// Nitro name-dictionary: returns the u32 data value per entry (offsets).
std::vector<std::uint32_t> parse_dictionary_offsets(
    const ByteVector& d,
    const std::size_t base) {
    if (base + 8U > d.size()) {
        throw std::runtime_error("NSBCA dictionary offset invalid");
    }
    const auto count = static_cast<std::size_t>(d[base + 1U]);
    if (count == 0U) {
        return {};
    }
    const auto entry_header = base + read_u16(d, base + 6U);
    const auto unit_size = static_cast<std::size_t>(read_u16(d, entry_header));
    const auto entries_start = entry_header + 4U;
    if (unit_size < 4U || entries_start + count * unit_size > d.size()) {
        throw std::runtime_error("NSBCA dictionary entries exceed section");
    }
    std::vector<std::uint32_t> offsets;
    offsets.reserve(count);
    for (std::size_t i = 0U; i < count; ++i) {
        offsets.push_back(read_u32(d, entries_start + i * unit_size));
    }
    return offsets;
}

// 3x3 rotation stored column-major (matches the model's convention).
std::array<float, 9> pivot_mat(
    const unsigned select,
    const unsigned neg,
    const float a,
    const float b) {
    const float o = (neg & 0x01U) == 0U ? 1.0F : -1.0F;
    const float c = (neg & 0x02U) == 0U ? b : -b;
    const float dd = (neg & 0x04U) == 0U ? a : -a;
    switch (select) {
        case 0: return {o, 0, 0, 0, a, b, 0, c, dd};
        case 1: return {0, o, 0, a, 0, b, c, 0, dd};
        case 2: return {0, 0, o, a, b, 0, c, dd, 0};
        case 3: return {0, a, b, o, 0, 0, 0, c, dd};
        case 4: return {a, 0, b, 0, o, 0, c, 0, dd};
        case 5: return {a, b, 0, 0, 0, o, c, dd, 0};
        case 6: return {0, a, b, 0, c, dd, o, 0, 0};
        case 7: return {a, 0, b, c, 0, dd, 0, o, 0};
        case 8: return {a, b, 0, c, dd, 0, 0, 0, o};
        default: return {-a, 0, 0, 0, 0, 0, 0, 0, 0};
    }
}

// Compressed orthonormal basis rotation. Column-major output.
std::array<float, 9> basis_mat(const std::array<std::uint16_t, 5>& in) {
    const std::array<std::uint16_t, 5> input{in[4], in[0], in[1], in[2], in[3]};
    std::array<std::uint16_t, 6> out{};
    for (std::size_t i = 0U; i < 5U; ++i) {
        out[i] = static_cast<std::uint16_t>((input[i] >> 3U) & 0x1FFFU);
        out[5] = static_cast<std::uint16_t>((out[5] << 3U) | (input[i] & 0x07U));
    }
    const auto f = [](std::uint16_t x) { return fixed(x, 13U, 12U); };
    const std::array<float, 3> a{f(out[1]), f(out[2]), f(out[3])};
    const std::array<float, 3> b{f(out[4]), f(out[0]), f(out[5])};
    const std::array<float, 3> c{
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]};
    return {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]};
}

struct CurveInfo final {
    std::uint16_t start_frame = 0;
    std::uint16_t end_frame = 0;
    unsigned data_width = 0;
    unsigned rate = 0;
    std::size_t num_samples() const {
        if (end_frame <= start_frame) {
            return 0U;
        }
        return static_cast<std::size_t>((end_frame - start_frame) >> rate);
    }
};

CurveInfo decode_curve_info(const std::uint32_t x) {
    CurveInfo info;
    info.start_frame = static_cast<std::uint16_t>(x & 0xFFFFU);
    info.end_frame = static_cast<std::uint16_t>((x >> 16U) & 0x0FFFU);
    info.data_width = (x >> 28U) & 0x03U;
    info.rate = (x >> 30U) & 0x03U;
    return info;
}

std::array<float, 9> fetch_rotation(
    const ByteVector& d,
    const std::size_t pivot_data,
    const std::size_t basis_data,
    const std::uint16_t x) {
    const unsigned mode = (x >> 15U) & 0x01U;
    const std::size_t idx = x & 0x7FFFU;
    if (mode == 1U) {
        const auto entry = pivot_data + idx * 6U;
        const auto selneg = read_u16(d, entry);
        const auto a = fixed(read_u16(d, entry + 2U), 16U, 12U);
        const auto b = fixed(read_u16(d, entry + 4U), 16U, 12U);
        return pivot_mat(selneg & 0x0FU, (selneg >> 4U) & 0x0FU, a, b);
    }
    const auto entry = basis_data + idx * 10U;
    std::array<std::uint16_t, 5> in{};
    for (std::size_t i = 0U; i < 5U; ++i) {
        in[i] = read_u16(d, entry + i * 2U);
    }
    return basis_mat(in);
}

khdays::assets::SkeletalAnimation read_animation(
    const ByteVector& d,
    const std::size_t base,
    std::string name) {
    if (!has_magic(d, base, "J\0AC")) {
        throw std::runtime_error("NSBCA: missing J0AC stamp");
    }
    khdays::assets::SkeletalAnimation animation;
    animation.name = std::move(name);
    animation.frame_count = read_u16(d, base + 4U);
    const auto num_objects = static_cast<std::size_t>(read_u16(d, base + 6U));
    const auto pivot_data = base + static_cast<std::size_t>(read_u32(d, base + 0x0CU));
    const auto basis_data = base + static_cast<std::size_t>(read_u32(d, base + 0x10U));

    animation.bones.resize(num_objects);
    for (std::size_t obj = 0U; obj < num_objects; ++obj) {
        const auto curves_off = read_u16(d, base + 0x14U + obj * 2U);
        std::size_t cur = base + curves_off;
        const auto flags = read_u16(d, cur);
        cur += 4U;  // flags u16 + dummy u8 + index u8

        khdays::assets::BoneCurves& bone = animation.bones[obj];
        bone.animated = ((flags >> 0U) & 0x01U) == 0U;
        if (!bone.animated) {
            continue;
        }

        const bool trans_animated = ((flags >> 1U) & 0x03U) == 0U;
        const std::array<bool, 3> trans_const{
            ((flags >> 3U) & 0x01U) != 0U,
            ((flags >> 4U) & 0x01U) != 0U,
            ((flags >> 5U) & 0x01U) != 0U};
        const bool rot_animated = ((flags >> 6U) & 0x03U) == 0U;
        const bool rot_const = ((flags >> 8U) & 0x01U) != 0U;
        const bool scale_animated = ((flags >> 9U) & 0x03U) == 0U;
        const std::array<bool, 3> scale_const{
            ((flags >> 11U) & 0x01U) != 0U,
            ((flags >> 12U) & 0x01U) != 0U,
            ((flags >> 13U) & 0x01U) != 0U};

        const auto read_scalar_curve =
            [&](const bool is_const, const bool scale_pair) {
                khdays::assets::AnimationCurve curve;
                if (is_const) {
                    curve.kind = khdays::assets::AnimationCurve::Kind::Constant;
                    curve.constant = fixed(read_u32(d, cur), 32U, 12U);
                    cur += scale_pair ? 8U : 4U;
                    return curve;
                }
                const auto info = decode_curve_info(read_u32(d, cur));
                const auto off = read_u32(d, cur + 4U);
                cur += 8U;
                curve.kind = khdays::assets::AnimationCurve::Kind::Samples;
                curve.start_frame = info.start_frame;
                curve.end_frame = info.end_frame;
                const auto n = info.num_samples();
                curve.samples.reserve(n);
                std::size_t p = base + off;
                for (std::size_t s = 0U; s < n; ++s) {
                    if (info.data_width == 0U) {
                        curve.samples.push_back(fixed(read_u32(d, p), 32U, 12U));
                        p += scale_pair ? 8U : 4U;
                    } else {
                        curve.samples.push_back(fixed(read_u16(d, p), 16U, 12U));
                        p += scale_pair ? 4U : 2U;
                    }
                }
                return curve;
            };

        if (trans_animated) {
            for (std::size_t i = 0U; i < 3U; ++i) {
                bone.translation[i] = read_scalar_curve(trans_const[i], false);
            }
        }

        if (rot_animated) {
            if (rot_const) {
                const auto v = read_u16(d, cur);
                cur += 4U;  // value u16 + padding u16
                bone.rotation.kind =
                    khdays::assets::AnimationRotationCurve::Kind::Constant;
                bone.rotation.constant =
                    fetch_rotation(d, pivot_data, basis_data, v);
            } else {
                const auto info = decode_curve_info(read_u32(d, cur));
                const auto off = read_u32(d, cur + 4U);
                cur += 8U;
                bone.rotation.kind =
                    khdays::assets::AnimationRotationCurve::Kind::Samples;
                bone.rotation.start_frame = info.start_frame;
                bone.rotation.end_frame = info.end_frame;
                const auto n = info.num_samples();
                bone.rotation.samples.reserve(n);
                for (std::size_t s = 0U; s < n; ++s) {
                    const auto v = read_u16(d, base + off + s * 2U);
                    bone.rotation.samples.push_back(
                        fetch_rotation(d, pivot_data, basis_data, v));
                }
            }
        }

        if (scale_animated) {
            for (std::size_t i = 0U; i < 3U; ++i) {
                bone.scale[i] = read_scalar_curve(scale_const[i], true);
            }
        }
    }

    return animation;
}

float sample_scalar(
    const khdays::assets::AnimationCurve& curve,
    const float default_value,
    const float frame) {
    using Kind = khdays::assets::AnimationCurve::Kind;
    if (curve.kind == Kind::Constant) {
        return curve.constant;
    }
    if (curve.kind != Kind::Samples || curve.samples.empty()) {
        return default_value;
    }
    const auto n = curve.samples.size();
    if (frame <= static_cast<float>(curve.start_frame)) {
        return curve.samples.front();
    }
    if (curve.end_frame <= curve.start_frame + 1U
        || frame >= static_cast<float>(curve.end_frame - 1U)) {
        return curve.samples.back();
    }
    const float span =
        static_cast<float>(curve.end_frame - 1U - curve.start_frame);
    const float lam = (frame - static_cast<float>(curve.start_frame)) / span;
    const float idx = lam * static_cast<float>(n - 1U);
    const auto lo = static_cast<std::size_t>(std::floor(idx));
    const auto hi = std::min(lo + 1U, n - 1U);
    const float g = idx - static_cast<float>(lo);
    return curve.samples[lo] * (1.0F - g) + curve.samples[hi] * g;
}

std::array<float, 9> sample_rotation_curve(
    const khdays::assets::AnimationRotationCurve& curve,
    const float frame) {
    using Kind = khdays::assets::AnimationRotationCurve::Kind;
    const std::array<float, 9> identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (curve.kind == Kind::Constant) {
        return curve.constant;
    }
    if (curve.kind != Kind::Samples || curve.samples.empty()) {
        return identity;
    }
    const auto n = curve.samples.size();
    if (frame <= static_cast<float>(curve.start_frame)) {
        return curve.samples.front();
    }
    if (curve.end_frame <= curve.start_frame + 1U
        || frame >= static_cast<float>(curve.end_frame - 1U)) {
        return curve.samples.back();
    }
    const float span =
        static_cast<float>(curve.end_frame - 1U - curve.start_frame);
    const float lam = (frame - static_cast<float>(curve.start_frame)) / span;
    const float idx = lam * static_cast<float>(n - 1U);
    const auto lo = static_cast<std::size_t>(std::floor(idx));
    const auto hi = std::min(lo + 1U, n - 1U);
    const float g = idx - static_cast<float>(lo);
    std::array<float, 9> result{};
    for (std::size_t i = 0U; i < 9U; ++i) {
        result[i] = curve.samples[lo][i] * (1.0F - g) + curve.samples[hi][i] * g;
    }
    return result;
}

}  // namespace

namespace khdays::assets {

SkeletalAnimation load_nsbca(
    const std::filesystem::path& input_path,
    const std::size_t animation_index) {
    const auto file = read_file(input_path);
    const auto sections = parse_sections(file);
    const auto iterator = sections.find("JNT0");
    if (iterator == sections.end()) {
        throw std::runtime_error("NSBCA contains no JNT0 section");
    }
    const auto jnt0 = iterator->second.offset;
    const auto offsets = parse_dictionary_offsets(file, jnt0 + 8U);
    if (animation_index >= offsets.size()) {
        throw std::runtime_error("NSBCA animation index out of range");
    }
    return read_animation(
        file, jnt0 + offsets[animation_index],
        "anim" + std::to_string(animation_index));
}

std::vector<std::array<float, 16>> sample_animation(
    const SkeletalAnimation& animation,
    const float frame,
    const std::vector<std::array<float, 16>>& rest_object_matrices) {
    auto result = rest_object_matrices;
    const auto count = std::min(animation.bones.size(), result.size());
    for (std::size_t i = 0U; i < count; ++i) {
        const auto& bone = animation.bones[i];
        if (!bone.animated) {
            continue;
        }
        const float tx = sample_scalar(bone.translation[0], 0.0F, frame);
        const float ty = sample_scalar(bone.translation[1], 0.0F, frame);
        const float tz = sample_scalar(bone.translation[2], 0.0F, frame);
        const auto r = sample_rotation_curve(bone.rotation, frame);
        const float sx = sample_scalar(bone.scale[0], 1.0F, frame);
        const float sy = sample_scalar(bone.scale[1], 1.0F, frame);
        const float sz = sample_scalar(bone.scale[2], 1.0F, frame);

        // M = Translation * Rotation * Scale (column-major).
        result[i] = std::array<float, 16>{
            r[0] * sx, r[1] * sx, r[2] * sx, 0.0F,
            r[3] * sy, r[4] * sy, r[5] * sy, 0.0F,
            r[6] * sz, r[7] * sz, r[8] * sz, 0.0F,
            tx, ty, tz, 1.0F};
    }
    return result;
}

}  // namespace khdays::assets
