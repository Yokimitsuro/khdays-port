#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "khdays/assets/tex0.h"  // DecodedTexture

namespace khdays::assets {

// A decoded NCLR palette: a flat list of RGBA colors grouped into fixed-size
// sub-palettes (16 colors for 4bpp graphics, 256 for 8bpp).
struct Palette2D final {
    std::vector<std::array<std::uint8_t, 4>> colors;
    int colors_per_palette = 16;
};

// Decoded NCGR character (tile) graphics: 8x8 tiles of palette indices, stored
// tile by tile in row-major order.
struct TileGraphics final {
    int bpp = 4;  // 4 or 8
    int tile_count = 0;
    std::vector<std::uint8_t> indices;  // tile_count * 64 palette indices
};

// A decoded NSCR tilemap: one cell per 8x8 screen tile.
struct Tilemap final {
    int width_tiles = 0;
    int height_tiles = 0;
    struct Cell final {
        std::uint16_t tile = 0;
        std::uint8_t palette = 0;
        bool flip_h = false;
        bool flip_v = false;
    };
    std::vector<Cell> cells;
};

// Decode DS 2D graphics resources. Each accepts either the raw resource or an
// LZ-compressed ".z" blob (auto-detected). Throw std::runtime_error on bad data.
Palette2D decode_nclr(const std::filesystem::path& path);
TileGraphics decode_ncgr(const std::filesystem::path& path);
Tilemap decode_nscr(const std::filesystem::path& path);

// Lay every tile out into a single RGBA image (a tile sheet), `tiles_per_row`
// wide, colored with sub-palette `palette_index`. Index 0 renders opaque here so
// the sheet is easy to see.
DecodedTexture render_tile_sheet(
    const TileGraphics& tiles,
    const Palette2D& palette,
    int palette_index = 0,
    int tiles_per_row = 16);

// Compose a full background image from a tilemap, its tile graphics, and the
// palette (each cell selects tile, sub-palette, and flip). Palette index 0 is
// treated as transparent.
DecodedTexture compose_background(
    const Tilemap& map,
    const TileGraphics& tiles,
    const Palette2D& palette);

// Serialize an RGBA texture as a 32-bit BMP (for export and viewing).
std::vector<std::uint8_t> to_bmp(const DecodedTexture& image);

}  // namespace khdays::assets
