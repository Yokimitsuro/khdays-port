#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "khdays/assets/battle_hud.h"

namespace {

void expect(const bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::array<std::uint8_t, 4> pixel(
    const khdays::assets::DecodedTexture& image,
    const int x,
    const int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.width
         + static_cast<std::size_t>(x)) * 4U;
    return {
        image.rgba[offset],
        image.rgba[offset + 1U],
        image.rgba[offset + 2U],
        image.rgba[offset + 3U]};
}

khdays::assets::DecodedTexture solid(
    const int width,
    const int height,
    const std::uint8_t value,
    const std::uint8_t alpha) {
    khdays::assets::DecodedTexture image;
    image.width = width;
    image.height = height;
    image.rgba.resize(
        static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t offset = 0; offset < image.rgba.size(); offset += 4U) {
        image.rgba[offset] = value;
        image.rgba[offset + 1U] = value;
        image.rgba[offset + 2U] = value;
        image.rgba[offset + 3U] = alpha;
    }
    return image;
}

}  // namespace

int main() {
    try {
        khdays::assets::Ov002PlayerGauge source;
        source.empty.width = 80;
        source.empty.height = 8;
        source.empty.rgba.assign(80U * 8U * 4U, 0U);
        for (std::size_t i = 0; i < source.palette.size(); ++i) {
            source.palette[i] = {
                static_cast<std::uint8_t>(i), 0U, 0U, 255U};
        }

        const auto empty =
            khdays::assets::compose_ov002_player_gauge(source, 0U, 100U);
        expect(pixel(empty, 78, 0)[3] == 0U, "zero HP leaves the strip empty");

        const auto minimum =
            khdays::assets::compose_ov002_player_gauge(source, 1U, 100U);
        expect(pixel(minimum, 78, 0)[0] == 6U,
               "non-zero HP keeps the rightmost cell");
        expect(pixel(minimum, 78, 5)[0] == 3U,
               "one cell uses ov002's six-row shade ramp");
        expect(pixel(minimum, 77, 0)[3] == 0U,
               "one HP does not paint a second cell");

        const auto half =
            khdays::assets::compose_ov002_player_gauge(source, 50U, 100U);
        expect(pixel(half, 78, 0)[0] == 6U,
               "half gauge includes its right edge");
        expect(pixel(half, 41, 0)[0] == 6U,
               "half gauge paints 38 cells");
        expect(pixel(half, 40, 0)[3] == 0U,
               "half gauge stops after 38 cells");

        const auto full =
            khdays::assets::compose_ov002_player_gauge(source, 150U, 100U);
        expect(pixel(full, 2, 0)[0] == 6U,
               "HP is clamped and fills all 77 cells");
        expect(pixel(full, 1, 0)[3] == 0U,
               "the original two-pixel left inset is retained");

        const auto invalid =
            khdays::assets::compose_ov002_player_gauge(source, 50U, 0U);
        expect(pixel(invalid, 78, 0)[3] == 0U,
               "zero maximum is handled without division");

        khdays::assets::Ov002CommandMenuArtwork menu_source;
        menu_source.header = solid(3, 1, 11U, 255U);
        menu_source.idle_row = solid(4, 2, 22U, 255U);
        menu_source.selected_row = solid(5, 2, 33U, 255U);
        for (auto& label : menu_source.labels) {
            label = solid(1, 1, 255U, 255U);
        }
        const auto menu = khdays::assets::compose_ov002_command_menu(
            menu_source, 1U);
        expect(menu.width == 96 && menu.height == 64,
               "command menu retains ov002's 12x8-tile footprint");
        expect(pixel(menu, 0, 0)[0] == 11U,
               "command header starts at row 0");
        expect(pixel(menu, 0, 16)[0] == 22U,
               "first inactive row starts at y=16");
        expect(pixel(menu, 0, 32)[0] == 33U,
               "selected second row starts at y=32");
        expect(pixel(menu, 8, 19)[0] == 107U,
               "inactive label uses palette-14 grey and one-tile inset");
        expect(pixel(menu, 16, 35)[0] == 255U,
               "selected label uses palette-15 white and two-tile inset");

        expect(khdays::assets::advance_ov002_command(
                   0U, {true, true, true}) == 1U,
               "X advances to the next primary command");
        expect(khdays::assets::advance_ov002_command(
                   0U, {true, false, true}) == 2U,
               "X skips unavailable command slots");
        expect(khdays::assets::advance_ov002_command(
                   2U, {true, true, true}) == 0U,
               "X wraps the primary command ring");
        expect(khdays::assets::advance_ov002_command(
                   1U, {false, true, false}) == 1U,
               "X stays put when no other command is available");

        std::cout << "battle HUD tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
