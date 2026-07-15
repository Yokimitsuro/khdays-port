#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/graphics2d.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

void set_u16(Bytes& b, std::size_t o, std::uint16_t v) {
    b.at(o) = static_cast<std::uint8_t>(v & 0xFFU);
    b.at(o + 1U) = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}
void set_u32(Bytes& b, std::size_t o, std::uint32_t v) {
    b.at(o) = static_cast<std::uint8_t>(v & 0xFFU);
    b.at(o + 1U) = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
    b.at(o + 2U) = static_cast<std::uint8_t>((v >> 16U) & 0xFFU);
    b.at(o + 3U) = static_cast<std::uint8_t>((v >> 24U) & 0xFFU);
}
void set_magic(Bytes& b, const char* m) {
    for (int i = 0; i < 4; ++i) {
        b.at(static_cast<std::size_t>(i)) = static_cast<std::uint8_t>(m[i]);
    }
}

std::filesystem::path write_temp(const char* name, const Bytes& data) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out{path, std::ios::binary};
    out.write(
        reinterpret_cast<const char*>(data.data()),
        static_cast<std::streamsize>(data.size()));
    return path;
}

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    std::vector<std::filesystem::path> temps;
    try {
        // NCLR: 4-color palette (index 1 = red RGB555 0x001F).
        Bytes nclr(0x30, 0U);
        set_magic(nclr, "RLCN");
        set_u32(nclr, 0x18U, 3U);   // bit depth 3 -> 16 colors/palette
        set_u32(nclr, 0x20U, 8U);   // data size = 4 colors
        set_u16(nclr, 0x28U + 0U, 0x0000U);  // index 0 black
        set_u16(nclr, 0x28U + 2U, 0x001FU);  // index 1 red
        set_u16(nclr, 0x28U + 4U, 0x03E0U);  // index 2 green
        set_u16(nclr, 0x28U + 6U, 0x7FFFU);  // index 3 white

        // NCGR: one 4bpp tile, every pixel index 1.
        Bytes ncgr(0x50, 0U);
        set_magic(ncgr, "RGCN");
        set_u32(ncgr, 0x1CU, 3U);       // 4bpp
        set_u32(ncgr, 0x28U, 32U);      // data size = 1 tile
        set_u32(ncgr, 0x2CU, 0x18U);    // data offset -> tiles at 0x30
        for (std::size_t i = 0x30U; i < 0x50U; ++i) {
            ncgr[i] = 0x11U;  // two index-1 pixels per byte
        }

        // NSCR: one cell, tile 0, palette 0.
        Bytes nscr(0x26, 0U);
        set_magic(nscr, "RCSN");
        set_u16(nscr, 0x18U, 8U);   // width px
        set_u16(nscr, 0x1AU, 8U);   // height px
        set_u32(nscr, 0x20U, 2U);   // data size = 1 cell
        set_u16(nscr, 0x24U, 0x0000U);

        const auto nclr_path = write_temp("khdays_test.nclr", nclr);
        const auto ncgr_path = write_temp("khdays_test.ncgr", ncgr);
        const auto nscr_path = write_temp("khdays_test.nscr", nscr);
        temps = {nclr_path, ncgr_path, nscr_path};

        const auto palette = khdays::assets::decode_nclr(nclr_path);
        expect(palette.colors.size() == 4U, "palette color count");
        expect(palette.colors[1][0] == 255 && palette.colors[1][1] == 0
                   && palette.colors[1][2] == 0,
               "palette index 1 is red");

        const auto tiles = khdays::assets::decode_ncgr(ncgr_path);
        expect(tiles.tile_count == 1, "tile count");
        expect(tiles.bpp == 4, "bpp");
        expect(tiles.indices.size() == 64U, "tile index count");
        expect(tiles.indices[0] == 1 && tiles.indices[63] == 1, "tile indices");

        const auto map = khdays::assets::decode_nscr(nscr_path);
        expect(map.width_tiles == 1 && map.height_tiles == 1, "tilemap size");
        expect(map.cells.size() == 1U && map.cells[0].tile == 0, "tilemap cell");

        const auto image =
            khdays::assets::compose_background(map, tiles, palette);
        expect(image.width == 8 && image.height == 8, "composed size");
        // Every pixel is index 1 -> red, opaque.
        expect(image.rgba[0] == 255 && image.rgba[1] == 0 && image.rgba[2] == 0
                   && image.rgba[3] == 255,
               "composed pixel is red");

        const auto sheet =
            khdays::assets::render_tile_sheet(tiles, palette, 0, 16);
        expect(sheet.width == 128 && sheet.height == 8, "tile sheet size");

        const auto bmp = khdays::assets::to_bmp(image);
        expect(bmp.size() == 54U + 8U * 8U * 4U, "bmp size");
        expect(bmp[0] == 'B' && bmp[1] == 'M', "bmp magic");

        for (const auto& p : temps) {
            std::filesystem::remove(p);
        }
        std::cout << "2D graphics test passed\n";
        return 0;
    } catch (const std::exception& error) {
        for (const auto& p : temps) {
            std::filesystem::remove(p);
        }
        std::cerr << "2D graphics test failed: " << error.what() << '\n';
        return 1;
    }
}
