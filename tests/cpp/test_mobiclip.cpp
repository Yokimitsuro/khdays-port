#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/mobiclip.h"

namespace {

void expect(const bool condition, const std::string& what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

constexpr int kW = 8;
constexpr int kH = 4;
constexpr int kLumaRow = 256;
constexpr int kChromaRow = 256;
constexpr int kCgOffset = 0x80;

// Build planes in the DS's layout: luma is height x 256; chroma is height/2 rows
// of 256 bytes, 128 of Co then 128 of Cg.
struct Planes {
    std::vector<std::uint8_t> luma;
    std::vector<std::uint8_t> chroma;
};

Planes make_planes(const std::uint8_t y, const std::uint8_t co,
                   const std::uint8_t cg) {
    Planes p;
    p.luma.assign(static_cast<std::size_t>(kH) * kLumaRow, y);
    p.chroma.assign(static_cast<std::size_t>(kH / 2) * kChromaRow, 128);
    for (int row = 0; row < kH / 2; ++row) {
        for (int i = 0; i < kW / 2; ++i) {
            p.chroma[static_cast<std::size_t>(row) * kChromaRow + i] = co;
            p.chroma[static_cast<std::size_t>(row) * kChromaRow + kCgOffset + i] = cg;
        }
    }
    return p;
}

int saturate5(const int v) {
    return v < 0 ? 0 : (v > 248 ? 31 : (v >> 3));
}

}  // namespace

int main() {
    try {
        // A neutral, mid-grey frame: Co = Cg = 0 means R = G = B = Y.
        {
            const auto p = make_planes(128, 128, 128);
            std::vector<std::uint16_t> out(static_cast<std::size_t>(kW) * kH, 0);
            khdays::assets::mobiclip::frame_to_bgr555(
                p.luma.data(), p.chroma.data(), kW, kH, out.data(), kW);
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) {
                    // The 2x2 checkerboard subtracts 4 from half the pixels.
                    const int yy = 128 - (((x ^ y) & 1) != 0 ? 4 : 0);
                    const int c = saturate5(yy);
                    const std::uint16_t want = static_cast<std::uint16_t>(
                        c | (c << 5) | (c << 10) | 0x8000);
                    expect(out[static_cast<std::size_t>(y) * kW + x] == want,
                           "neutral frame is grey, with the checkerboard applied");
                }
            }
        }

        // Bit 15 (the DS alpha bit) is always set.
        {
            const auto p = make_planes(200, 100, 150);
            std::vector<std::uint16_t> out(static_cast<std::size_t>(kW) * kH, 0);
            khdays::assets::mobiclip::frame_to_bgr555(
                p.luma.data(), p.chroma.data(), kW, kH, out.data(), kW);
            for (const auto pixel : out) {
                expect((pixel & 0x8000U) != 0U, "bit 15 set on every pixel");
            }
        }

        // The YCoCg inverse, on a case where the three channels differ:
        //   t = Y - Cg;  R = t + Co;  G = Y + Cg;  B = t - Co
        {
            const int y_in = 160;
            const int co = 20;   // stored as 128 + 20
            const int cg = -12;  // stored as 128 - 12
            const auto p = make_planes(static_cast<std::uint8_t>(y_in),
                                       static_cast<std::uint8_t>(128 + co),
                                       static_cast<std::uint8_t>(128 + cg));
            std::vector<std::uint16_t> out(static_cast<std::size_t>(kW) * kH, 0);
            khdays::assets::mobiclip::frame_to_bgr555(
                p.luma.data(), p.chroma.data(), kW, kH, out.data(), kW);
            // Pixel (0,0) is on the un-dithered half of the checkerboard.
            const int t = y_in - cg;
            const std::uint16_t want = static_cast<std::uint16_t>(
                saturate5(t + co) | (saturate5(y_in + cg) << 5)
                | (saturate5(t - co) << 10) | 0x8000);
            expect(out[0] == want, "YCoCg inverse at (0,0)");
            // (1,0) is dithered: Y - 4.
            const int yd = y_in - 4;
            const int td = yd - cg;
            const std::uint16_t want_d = static_cast<std::uint16_t>(
                saturate5(td + co) | (saturate5(yd + cg) << 5)
                | (saturate5(td - co) << 10) | 0x8000);
            expect(out[1] == want_d, "YCoCg inverse at (1,0), dithered");
        }

        // Saturation: the table clamps to 0..31 rather than wrapping.
        {
            const auto dark = make_planes(0, 0, 255);
            std::vector<std::uint16_t> out(static_cast<std::size_t>(kW) * kH, 0);
            khdays::assets::mobiclip::frame_to_bgr555(
                dark.luma.data(), dark.chroma.data(), kW, kH, out.data(), kW);
            for (const auto pixel : out) {
                expect((pixel & 31U) <= 31U && ((pixel >> 5U) & 31U) <= 31U,
                       "channels stay inside 5 bits");
            }
            const auto bright = make_planes(255, 255, 0);
            khdays::assets::mobiclip::frame_to_bgr555(
                bright.luma.data(), bright.chroma.data(), kW, kH, out.data(), kW);
            expect((out[0] & 31U) == 31U, "over-range saturates to 31, not wraps");
        }

        // Cg is read 0x80 into the *same* chroma row, not from the next row.
        {
            auto p = make_planes(128, 128, 128);
            p.chroma[kCgOffset] = 128 + 40;  // Cg of pixel (0,0) only
            std::vector<std::uint16_t> out(static_cast<std::size_t>(kW) * kH, 0);
            khdays::assets::mobiclip::frame_to_bgr555(
                p.luma.data(), p.chroma.data(), kW, kH, out.data(), kW);
            const int g = static_cast<int>((out[0] >> 5U) & 31U);
            expect(g == saturate5(128 + 40), "Cg comes from chroma[0x80] of the same row");
        }

        // The RGBA path: ds_exact keeps the dither, the default drops it (and the
        // 5-bit quantisation) for a better image than the hardware could show.
        {
            const auto p = make_planes(200, 128, 128);
            const auto exact = khdays::assets::mobiclip::frame_to_rgba(
                p.luma.data(), p.chroma.data(), kW, kH, /*ds_exact=*/true);
            const auto full = khdays::assets::mobiclip::frame_to_rgba(
                p.luma.data(), p.chroma.data(), kW, kH, /*ds_exact=*/false);
            expect(exact.width == kW && exact.height == kH, "rgba size");
            expect(exact.rgba.size() == static_cast<std::size_t>(kW) * kH * 4U,
                   "rgba buffer size");
            expect(full.rgba[0] == 200 && full.rgba[1] == 200 && full.rgba[2] == 200,
                   "full-precision path is exact, un-dithered");
            expect(full.rgba[3] == 255, "alpha opaque");
            // (1,0) is a dithered pixel, so the two paths must disagree there.
            expect(exact.rgba[4] != full.rgba[4],
                   "ds_exact dithers where the full-precision path does not");
        }

        std::cout << "MobiClip colour test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MobiClip colour test failed: " << error.what() << '\n';
        return 1;
    }
}
