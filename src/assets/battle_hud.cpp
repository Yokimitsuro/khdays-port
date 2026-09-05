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

void blit(
    DecodedTexture& destination,
    const DecodedTexture& source,
    const int origin_x,
    const int origin_y,
    const std::uint8_t tint = 255U) {
    if (source.width <= 0 || source.height <= 0
        || source.rgba.size()
            < static_cast<std::size_t>(source.width) * source.height * 4U) {
        return;
    }
    for (int sy = 0; sy < source.height; ++sy) {
        const int dy = origin_y + sy;
        if (dy < 0 || dy >= destination.height) {
            continue;
        }
        for (int sx = 0; sx < source.width; ++sx) {
            const int dx = origin_x + sx;
            if (dx < 0 || dx >= destination.width) {
                continue;
            }
            const std::size_t src =
                (static_cast<std::size_t>(sy) * source.width + sx) * 4U;
            const std::uint32_t source_alpha = source.rgba[src + 3U];
            if (source_alpha == 0U) {
                continue;
            }
            const std::size_t dst =
                (static_cast<std::size_t>(dy) * destination.width + dx) * 4U;
            const std::uint32_t destination_alpha =
                destination.rgba[dst + 3U];
            const std::uint32_t inverse_alpha = 255U - source_alpha;
            const std::uint32_t output_alpha = source_alpha
                + (destination_alpha * inverse_alpha + 127U) / 255U;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
                const std::uint32_t source_colour =
                    (static_cast<std::uint32_t>(source.rgba[src + channel])
                     * tint + 127U) / 255U;
                const std::uint32_t premultiplied =
                    source_colour * source_alpha
                    + (static_cast<std::uint32_t>(
                           destination.rgba[dst + channel])
                       * destination_alpha * inverse_alpha + 127U) / 255U;
                destination.rgba[dst + channel] = output_alpha == 0U
                    ? 0U
                    : static_cast<std::uint8_t>(
                        (premultiplied + output_alpha / 2U) / output_alpha);
            }
            destination.rgba[dst + 3U] =
                static_cast<std::uint8_t>(output_alpha);
        }
    }
}

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

DecodedTexture compose_ov002_command_menu(
    const Ov002CommandMenuArtwork& source,
    const std::size_t selected) {
    constexpr int kWidth = 96;
    constexpr int kHeight = 64;
    constexpr int kHeaderHeight = 16;
    constexpr int kRowHeight = 16;
    constexpr int kTextTop = 3;
    constexpr int kSelectedInset = 16;
    constexpr int kIdleInset = 8;
    constexpr std::uint8_t kIdleText = 107U;

    DecodedTexture out;
    out.name = "ov002_primary_commands";
    out.format_name = "ov002-command-menu";
    out.color_zero_transparent = true;
    out.width = kWidth;
    out.height = kHeight;
    out.rgba.assign(
        static_cast<std::size_t>(kWidth) * kHeight * 4U, 0U);

    blit(out, source.header, 0, 0);
    const std::size_t active = std::min(selected, source.labels.size() - 1U);
    for (std::size_t row = 0; row < source.labels.size(); ++row) {
        const bool is_selected = row == active;
        const int y = kHeaderHeight + static_cast<int>(row) * kRowHeight;
        blit(out, is_selected ? source.selected_row : source.idle_row, 0, y);
        blit(
            out, source.labels[row],
            is_selected ? kSelectedInset : kIdleInset,
            y + kTextTop,
            is_selected ? 255U : kIdleText);
    }
    return out;
}

std::size_t advance_ov002_command(
    const std::size_t current,
    const std::array<bool, 3>& available) {
    const std::size_t from = current % available.size();
    for (std::size_t step = 1U; step < available.size(); ++step) {
        const std::size_t candidate = (from + step) % available.size();
        if (available[candidate]) {
            return candidate;
        }
    }
    return from;
}

}  // namespace khdays::assets
