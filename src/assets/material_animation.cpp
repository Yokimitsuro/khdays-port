#include "khdays/assets/material_animation.h"

#include <algorithm>
#include <stdexcept>

namespace khdays::assets {

namespace {

// Track `info` word fields (NitroSystem NNSG3dMatCElem).
constexpr std::uint32_t kConst = 0x20000000U;
constexpr std::uint32_t kStep2 = 0x40000000U;
constexpr std::uint32_t kStepMask = 0xC0000000U;
constexpr std::uint32_t kLastInterpMask = 0x1FFF0000U;
constexpr std::uint32_t kOffsetMask = 0x0000FFFFU;

std::uint16_t u16_at(const std::uint8_t* d, const std::size_t size,
                     const std::size_t offset) {
    if (offset > size || size - offset < 2U) {
        throw std::runtime_error("BMA0: read past end (u16)");
    }
    return static_cast<std::uint16_t>(
        d[offset] | static_cast<std::uint16_t>(d[offset + 1U] << 8U));
}

std::uint32_t u32_at(const std::uint8_t* d, const std::size_t size,
                     const std::size_t offset) {
    if (offset > size || size - offset < 4U) {
        throw std::runtime_error("BMA0: read past end (u32)");
    }
    return static_cast<std::uint32_t>(d[offset])
        | (static_cast<std::uint32_t>(d[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(d[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(d[offset + 3U]) << 24U);
}

// One NNSG3dResDict: returns (name, entry data offset) per entry.
std::vector<std::pair<std::string, std::size_t>> read_dictionary(
    const std::uint8_t* d, const std::size_t size, const std::size_t base) {
    if (base > size || size - base < 8U) {
        throw std::runtime_error("BMA0: dictionary out of range");
    }
    const std::size_t count = d[base + 1U];
    const std::size_t entries = base + u16_at(d, size, base + 6U);
    const std::size_t unit = u16_at(d, size, entries);
    const std::size_t names = entries + u16_at(d, size, entries + 2U);
    std::vector<std::pair<std::string, std::size_t>> out;
    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t data = entries + 4U + i * unit;
        const std::size_t name = names + i * 16U;
        if (data > size || name > size || size - name < 16U) {
            throw std::runtime_error("BMA0: dictionary entry out of range");
        }
        const auto* first = reinterpret_cast<const char*>(d + name);
        out.emplace_back(
            std::string{first, static_cast<std::size_t>(
                                   std::find(first, first + 16, '\0') - first)},
            data);
    }
    return out;
}

// func_01ff9fd4: an RGB555 colour at `frame`.
std::uint16_t colour_at(const std::vector<std::uint8_t>& block,
                        const std::uint32_t info, const std::uint32_t frame) {
    if ((info & kConst) != 0U) {
        return static_cast<std::uint16_t>(info & kOffsetMask);
    }
    const std::size_t head = info & kOffsetMask;
    const auto value = [&](const std::size_t i) {
        return static_cast<std::uint32_t>(
            u16_at(block.data(), block.size(), head + i * 2U));
    };
    if ((info & kStepMask) == 0U) {
        return static_cast<std::uint16_t>(value(frame));
    }
    const std::uint32_t last = (info & kLastInterpMask) >> 16U;
    const auto half = [&](const std::size_t i) {
        const std::uint32_t a = value(i);
        const std::uint32_t b = value(i + 1U);
        return static_cast<std::uint16_t>(
            ((((a & 0x7C1FU) + (b & 0x7C1FU)) >> 1U) & 0x7C1FU)
            | ((((a & 0x03E0U) + (b & 0x03E0U)) >> 1U) & 0x03E0U));
    };
    if ((info & kStep2) != 0U) {
        if ((frame & 1U) != 0U) {
            return frame > last
                ? static_cast<std::uint16_t>(value((last >> 1U) + 1U))
                : half(frame >> 1U);
        }
        return static_cast<std::uint16_t>(value(frame >> 1U));
    }
    if ((frame & 3U) != 0U) {
        if (frame > last) {
            return static_cast<std::uint16_t>(
                value((last >> 2U) + (frame & 3U)));
        }
        if ((frame & 1U) != 0U) {
            std::size_t idx = 0U;
            std::size_t idx_sub = 0U;
            if ((frame & 2U) != 0U) {
                idx_sub = frame >> 2U;
                idx = idx_sub + 1U;
            } else {
                idx = frame >> 2U;
                idx_sub = idx + 1U;
            }
            const std::uint32_t v = value(idx);
            const std::uint32_t w = value(idx_sub);
            const std::uint32_t green = (v & 0x03E0U) * 3U + (w & 0x03E0U);
            const std::uint32_t red_blue = (v & 0x7C1FU) * 3U + (w & 0x7C1FU);
            return static_cast<std::uint16_t>(
                ((red_blue >> 2U) & 0x7C1FU) | ((green >> 2U) & 0x03E0U));
        }
        return half(frame >> 2U);
    }
    return static_cast<std::uint16_t>(value(frame >> 2U));
}

// func_02018830: the 5-bit polygon alpha at `frame`.
std::uint32_t alpha_at(const std::vector<std::uint8_t>& block,
                       const std::uint32_t info, const std::uint32_t frame) {
    if ((info & kConst) != 0U) {
        return info & kOffsetMask;
    }
    const std::size_t head = info & kOffsetMask;
    const auto value = [&](const std::size_t i) -> std::uint32_t {
        if (head + i >= block.size()) {
            throw std::runtime_error("BMA0: read past end (alpha)");
        }
        return block[head + i];
    };
    if ((info & kStepMask) == 0U) {
        return value(frame);
    }
    const std::uint32_t last = (info & kLastInterpMask) >> 16U;
    if ((info & kStep2) != 0U) {
        if ((frame & 1U) != 0U) {
            return frame > last
                ? value((last >> 1U) + 1U)
                : (value(frame >> 1U) + value((frame >> 1U) + 1U)) >> 1U;
        }
        return value(frame >> 1U);
    }
    if ((frame & 3U) != 0U) {
        if (frame > last) {
            return value((last >> 2U) + (frame & 3U));
        }
        if ((frame & 1U) != 0U) {
            std::size_t idx = 0U;
            std::size_t idx_sub = 0U;
            if ((frame & 2U) != 0U) {
                idx_sub = frame >> 2U;
                idx = idx_sub + 1U;
            } else {
                idx = frame >> 2U;
                idx_sub = idx + 1U;
            }
            return (value(idx) * 3U + value(idx_sub)) >> 2U;
        }
        return (value(frame >> 2U) + value((frame >> 2U) + 1U)) >> 1U;
    }
    return value(frame >> 2U);
}

std::array<float, 3> to_rgb(const std::uint16_t c) {
    return {static_cast<float>(c & 0x1FU) / 31.0F,
            static_cast<float>((c >> 5U) & 0x1FU) / 31.0F,
            static_cast<float>((c >> 10U) & 0x1FU) / 31.0F};
}

}  // namespace

MaterialColorAnimation load_nsbma(const std::uint8_t* data,
                                  const std::size_t size,
                                  const std::size_t index) {
    if (data == nullptr || size < 0x14U || data[0] != 'B' || data[1] != 'M'
        || data[2] != 'A' || data[3] != '0') {
        throw std::runtime_error("not a BMA0 resource");
    }
    const std::size_t section = u32_at(data, size, 0x10U);
    if (section > size || size - section < 8U || data[section] != 'M'
        || data[section + 1U] != 'A' || data[section + 2U] != 'T'
        || data[section + 3U] != '0') {
        throw std::runtime_error("BMA0: missing MAT0 block");
    }
    const auto animations = read_dictionary(data, size, section + 8U);
    if (index >= animations.size()) {
        throw std::runtime_error("BMA0: animation index out of range");
    }
    const std::size_t start =
        section + u32_at(data, size, animations[index].second);
    if (start > size || size - start < 8U || data[start] != 'M'
        || data[start + 2U] != 'A' || data[start + 3U] != 'M') {
        throw std::runtime_error("BMA0: animation block is not 'M' 'AM'");
    }

    MaterialColorAnimation out;
    out.name = animations[index].first;
    out.frame_count = u16_at(data, size, start + 4U);
    // Track offsets are relative to the animation block and may reach to the
    // end of the resource.
    out.block.assign(data + start, data + size);
    for (const auto& [material, entry] :
         read_dictionary(out.block.data(), out.block.size(), 8U)) {
        MaterialColorAnimation::Track track;
        track.material = material;
        for (std::size_t i = 0U; i < track.info.size(); ++i) {
            track.info[i] = u32_at(out.block.data(), out.block.size(),
                                   entry + i * 4U);
        }
        out.tracks.push_back(std::move(track));
    }
    return out;
}

std::optional<MaterialColorState> sample_material_color(
    const MaterialColorAnimation& animation, const std::string_view material,
    std::uint32_t frame) {
    const auto track = std::find_if(
        animation.tracks.begin(), animation.tracks.end(),
        [&](const auto& t) { return t.material == material; });
    if (track == animation.tracks.end()) {
        return std::nullopt;
    }
    if (animation.frame_count != 0U) {
        frame = std::min<std::uint32_t>(frame, animation.frame_count - 1U);
    }
    MaterialColorState state;
    state.diffuse = to_rgb(colour_at(animation.block, track->info[0], frame));
    state.ambient = to_rgb(colour_at(animation.block, track->info[1], frame));
    state.specular = to_rgb(colour_at(animation.block, track->info[2], frame));
    state.emission = to_rgb(colour_at(animation.block, track->info[3], frame));
    state.alpha =
        static_cast<float>(alpha_at(animation.block, track->info[4], frame))
        / 31.0F;
    return state;
}

}  // namespace khdays::assets
