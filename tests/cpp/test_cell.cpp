#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/cell.h"
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

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    try {
        // Minimal NCER: one cell, one 8x8 OAM piece at (0,0), tile 0, palette 0.
        Bytes ncer(0x40, 0U);
        ncer[0] = 'R'; ncer[1] = 'E'; ncer[2] = 'C'; ncer[3] = 'N';
        ncer[0x10] = 'K'; ncer[0x11] = 'B'; ncer[0x12] = 'E'; ncer[0x13] = 'C';
        set_u16(ncer, 0x18U, 1U);      // numCells
        set_u16(ncer, 0x1AU, 0U);      // cellType (short records)
        set_u32(ncer, 0x1CU, 0x18U);   // cell data offset
        set_u32(ncer, 0x20U, 0U);      // mapping mode -> boundary 32
        set_u16(ncer, 0x30U, 1U);      // cell 0: numOAM
        set_u32(ncer, 0x34U, 0U);      // cell 0: OAM offset
        set_u16(ncer, 0x38U, 0U);      // attr0: y 0, square
        set_u16(ncer, 0x3AU, 0U);      // attr1: x 0, size 0 -> 8x8
        set_u16(ncer, 0x3CU, 0U);      // attr2: tile 0, palette 0

        const auto bank = khdays::assets::decode_ncer(ncer.data(), ncer.size());
        expect(bank.cells.size() == 1U, "cell count");
        expect(bank.tile_boundary == 32, "tile boundary");
        expect(bank.cells[0].pieces.size() == 1U, "oam count");
        const auto& piece = bank.cells[0].pieces[0];
        expect(piece.width == 8 && piece.height == 8, "piece size");
        expect(piece.x == 0 && piece.y == 0, "piece position");

        // One 8x8 4bpp tile, every pixel index 1 -> palette red.
        khdays::assets::TileGraphics tiles;
        tiles.bpp = 4;
        tiles.tile_count = 1;
        tiles.indices.assign(64U, 1U);

        khdays::assets::Palette2D palette;
        palette.colors_per_palette = 16;
        palette.colors = {{0, 0, 0, 255}, {255, 0, 0, 255}};

        const auto image = khdays::assets::render_cell(
            bank.cells[0], tiles, palette, bank.tile_boundary);
        expect(image.width == 8 && image.height == 8, "rendered size");
        expect(image.rgba[0] == 255 && image.rgba[1] == 0 && image.rgba[2] == 0
                   && image.rgba[3] == 255,
               "rendered pixel is red");

        std::cout << "Cell decoder test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Cell decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
