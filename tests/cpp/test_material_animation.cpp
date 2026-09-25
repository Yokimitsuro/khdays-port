#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/material_animation.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

void set_u16(Bytes& b, std::size_t o, std::uint16_t v) {
    b.at(o) = static_cast<std::uint8_t>(v & 0xFFU);
    b.at(o + 1U) = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}
void set_u32(Bytes& b, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0; i < 4U; ++i) {
        b.at(o + i) = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
    }
}

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4F; }

// One NNSG3dResDict with a single entry of `unit` data bytes at `base`;
// returns the offset of the entry's data.
std::size_t write_dict(Bytes& b, std::size_t base, std::uint16_t unit,
                       const char* name) {
    b.at(base + 1U) = 1U;                       // numEntry
    set_u16(b, base + 6U, 16U);                 // ofsEntry: header + 2 nodes
    const std::size_t entries = base + 16U;
    set_u16(b, entries, unit);                  // sizeUnit
    set_u16(b, entries + 2U, static_cast<std::uint16_t>(4U + unit));  // ofsName
    for (std::size_t i = 0; name[i] != '\0'; ++i) {
        b.at(entries + 4U + unit + i) = static_cast<std::uint8_t>(name[i]);
    }
    return entries + 4U;
}

}  // namespace

int main() {
    try {
        // BMA0 -> MAT0 -> one animation "a" (8 frames) -> one material "m".
        Bytes b(0x200U, 0U);
        b[0] = 'B'; b[1] = 'M'; b[2] = 'A'; b[3] = '0';
        set_u32(b, 0x10U, 0x14U);  // first section
        const std::size_t mat0 = 0x14U;
        b[mat0] = 'M'; b[mat0 + 1U] = 'A'; b[mat0 + 2U] = 'T'; b[mat0 + 3U] = '0';
        const std::size_t anim_entry = write_dict(b, mat0 + 8U, 4U, "a");
        const std::size_t anim = mat0 + 48U;
        set_u32(b, anim_entry, static_cast<std::uint32_t>(anim - mat0));
        b[anim] = 'M'; b[anim + 2U] = 'A'; b[anim + 3U] = 'M';
        set_u16(b, anim + 4U, 8U);  // numFrame
        const std::size_t tracks = write_dict(b, anim + 8U, 20U, "m");
        set_u32(b, tracks, 0x20007FFFU);        // diffuse: constant white
        set_u32(b, tracks + 4U, 0x2000001FU);   // ambient: constant red
        set_u32(b, tracks + 8U, 0x20000000U);   // specular: constant black
        set_u32(b, tracks + 12U, 0x20000000U);  // emission: constant black
        // Polygon alpha sampled every 2nd frame, interpolated up to frame 6,
        // data at anim + 64: {0, 16, 31, 20, 10}.
        set_u32(b, tracks + 16U, 0x40000000U | (6U << 16U) | 64U);
        const std::uint8_t alpha[5] = {0U, 16U, 31U, 20U, 10U};
        for (std::size_t i = 0; i < 5U; ++i) {
            b.at(anim + 64U + i) = alpha[i];
        }

        const auto animation =
            khdays::assets::load_nsbma(b.data(), b.size(), 0U);
        expect(animation.name == "a" && animation.frame_count == 8U,
               "BMA0 animation header");
        expect(!khdays::assets::sample_material_color(animation, "x", 0U),
               "an unanimated material has no state");

        // NitroSystem's step-2 rule: even frames read a sample, odd frames
        // average two, and odd frames past the last interpolated one read
        // the trailing sample.
        const unsigned expected[8] = {0U, 8U, 16U, 23U, 31U, 25U, 20U, 10U};
        for (std::uint32_t f = 0; f < 8U; ++f) {
            const auto s = khdays::assets::sample_material_color(animation, "m", f);
            expect(s.has_value(), "material state");
            expect(near(s->alpha, static_cast<float>(expected[f]) / 31.0F),
                   "step-2 alpha sample");
        }
        const auto s0 = khdays::assets::sample_material_color(animation, "m", 0U);
        expect(near(s0->diffuse[0], 1.0F) && near(s0->diffuse[2], 1.0F),
               "constant diffuse");
        expect(near(s0->ambient[0], 1.0F) && near(s0->ambient[1], 0.0F),
               "constant ambient is red");
        // Frames past the end clamp to the last one.
        const auto late = khdays::assets::sample_material_color(animation, "m", 99U);
        expect(near(late->alpha, 10.0F / 31.0F), "frame clamps to the end");

        bool threw = false;
        try {
            (void)khdays::assets::load_nsbma(b.data(), 8U, 0U);
        } catch (const std::exception&) {
            threw = true;
        }
        expect(threw, "truncated BMA0 is rejected");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "material animation tests passed\n";
    return 0;
}
