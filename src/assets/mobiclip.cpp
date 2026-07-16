#include "khdays/assets/mobiclip.h"

#include <stdexcept>

namespace khdays::assets::mobiclip {

namespace {
// The DS's saturation table, `clamp(x >> 3, 0, 31)` — it lives in the ROM as 768
// bytes indexed from -256, but the formula reproduces it exactly (verified for
// all 768 entries), so there is nothing to carry around.
inline int saturate5(const int value) {
    if (value < 0) {
        return 0;
    }
    return value > 248 ? 31 : (value >> 3);
}

// Half the pixels get Y - 4 before the lookup — a 2x2 checkerboard, which is
// -1/2 LSB in the input domain of the >>3 above.
inline int dither_bias(const int x, const int y) {
    return ((x ^ y) & 1) != 0 ? 4 : 0;
}

void check(const std::uint8_t* luma, const std::uint8_t* chroma, const int width,
           const int height) {
    if (luma == nullptr || chroma == nullptr) {
        throw std::invalid_argument("mobiclip: null plane");
    }
    if (width <= 0 || height <= 0 || (height & 1) != 0) {
        throw std::invalid_argument("mobiclip: bad frame size");
    }
}

// Chroma rows are a full 256 bytes: 128 of Co, then 128 of Cg.
constexpr int kChromaRowBytes = 256;
constexpr int kChromaCgOffset = 0x80;
constexpr int kLumaRowBytes = 256;
}  // namespace

void frame_to_bgr555(const std::uint8_t* luma, const std::uint8_t* chroma,
                     const int width, const int height, std::uint16_t* dst,
                     const int stride_pixels) {
    check(luma, chroma, width, height);
    if (dst == nullptr || stride_pixels < width) {
        throw std::invalid_argument("mobiclip: bad destination");
    }
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = luma + static_cast<std::size_t>(y) * kLumaRowBytes;
        const std::uint8_t* crow =
            chroma + static_cast<std::size_t>(y >> 1) * kChromaRowBytes;
        std::uint16_t* out = dst + static_cast<std::size_t>(y) * stride_pixels;
        for (int x = 0; x < width; ++x) {
            const int co = crow[x >> 1] - 128;
            const int cg = crow[kChromaCgOffset + (x >> 1)] - 128;
            const int yy = row[x] - dither_bias(x, y);
            const int t = yy - cg;
            out[x] = static_cast<std::uint16_t>(
                saturate5(t + co) | (saturate5(yy + cg) << 5)
                | (saturate5(t - co) << 10) | 0x8000);
        }
    }
}

DecodedTexture frame_to_rgba(const std::uint8_t* luma, const std::uint8_t* chroma,
                             const int width, const int height,
                             const bool ds_exact) {
    check(luma, chroma, width, height);
    DecodedTexture out;
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * height * 4U);

    const auto clamp8 = [](const int v) {
        return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    };
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = luma + static_cast<std::size_t>(y) * kLumaRowBytes;
        const std::uint8_t* crow =
            chroma + static_cast<std::size_t>(y >> 1) * kChromaRowBytes;
        for (int x = 0; x < width; ++x) {
            const int co = crow[x >> 1] - 128;
            const int cg = crow[kChromaCgOffset + (x >> 1)] - 128;
            const int yy = row[x] - (ds_exact ? dither_bias(x, y) : 0);
            const int t = yy - cg;
            std::uint8_t r{};
            std::uint8_t g{};
            std::uint8_t b{};
            if (ds_exact) {
                // Quantise to the DS's 5 bits, then widen back — same pixels the
                // hardware shows.
                r = static_cast<std::uint8_t>(saturate5(t + co) * 255 / 31);
                g = static_cast<std::uint8_t>(saturate5(yy + cg) * 255 / 31);
                b = static_cast<std::uint8_t>(saturate5(t - co) * 255 / 31);
            } else {
                r = clamp8(t + co);
                g = clamp8(yy + cg);
                b = clamp8(t - co);
            }
            const std::size_t i =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            out.rgba[i] = r;
            out.rgba[i + 1U] = g;
            out.rgba[i + 2U] = b;
            out.rgba[i + 3U] = 255U;
        }
    }
    return out;
}

}  // namespace khdays::assets::mobiclip
