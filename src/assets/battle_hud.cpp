#include "khdays/assets/battle_hud.h"

#include <algorithm>
#include <cstddef>

namespace khdays::assets {

namespace {

constexpr int kGaugeCells = 77;
constexpr int kGaugePadding = 1;
constexpr int kGaugeRows = 6;
constexpr std::array<std::uint8_t, kGaugeRows> kFilledShade{
    6U, 6U, 5U, 5U, 4U, 3U};

void draw_cell(
    DecodedTexture& image,
    const std::array<std::array<std::uint8_t, 4>, 16>& palette,
    const int cell) {
    // Raster equivalent of func_ov002_020576d8. The DS routine rounds the
    // padded width to a tile boundary, then counts columns from the right.
    const int aligned =
        ((kGaugePadding + kGaugeCells + 7) / 8) * 8;
    const int x = std::max(
        0, aligned - (cell + kGaugePadding + 1));
    if (x >= image.width) {
        return;
    }
    for (int row = 0; row < kGaugeRows && row < image.height; ++row) {
        const auto& colour =
            palette[kFilledShade[static_cast<std::size_t>(row)]];
        const std::size_t dst =
            (static_cast<std::size_t>(row) * image.width
             + static_cast<std::size_t>(x)) * 4U;
        std::copy(colour.begin(), colour.end(), image.rgba.begin() + dst);
    }
}

}  // namespace

DecodedTexture compose_ov002_player_gauge(
    const Ov002PlayerGauge& source,
    const std::uint16_t current,
    const std::uint16_t maximum) {
    DecodedTexture out = source.empty;
    out.name = "ov002_player_hp";
    out.format_name = "ov002-4bpp-gauge";
    if (maximum == 0U || out.width <= 0 || out.height <= 0
        || out.rgba.size()
            < static_cast<std::size_t>(out.width) * out.height * 4U) {
        return out;
    }

    const std::uint32_t clamped =
        std::min<std::uint32_t>(current, maximum);
    int cells = static_cast<int>(
        clamped * static_cast<std::uint32_t>(kGaugeCells) / maximum);
    if (clamped != 0U && cells == 0) {
        cells = 1;
    }
    for (int cell = 0; cell < cells; ++cell) {
        draw_cell(out, source.palette, cell);
    }
    return out;
}

}  // namespace khdays::assets
