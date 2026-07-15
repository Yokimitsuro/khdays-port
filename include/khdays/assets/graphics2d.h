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

// Decode from an in-memory (already decompressed) resource — e.g. a Nitro
// resource carved out of a pack.
Palette2D decode_nclr(const std::uint8_t* data, std::size_t size);
TileGraphics decode_ncgr(const std::uint8_t* data, std::size_t size);
Tilemap decode_nscr(const std::uint8_t* data, std::size_t size);

// Locate a Nitro resource by its little-endian magic (e.g. "RLCN", "RGCN",
// "RCSN") inside a larger pack blob, returning a view of it (its declared file
// size from the Nitro header @0x08). Empty if not found.
struct ResourceView final {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    explicit operator bool() const { return data != nullptr; }
};
ResourceView find_nitro_resource(
    const std::uint8_t* pack, std::size_t size, const char magic[4]);

// A parsed D2KP ("PK2D") pack — the game's UI containers (e.g. the sub-files of
// UI/mlt/res.p2). A D2KP is a typed bank: a small header points at per-type
// sections ({count, offsets[], sizes[]}), each listing Nitro resources. Every
// vector holds raw (already decompressed) resource bytes, indexed as the game
// indexes that type; decode with decode_nclr/decode_ncgr/decode_nscr (and the
// NCER/NANR decoders in cell.h).
struct Pk2dPack final {
    std::vector<ResourceView> palettes;  // NCLR (RLCN)
    std::vector<ResourceView> tiles;     // NCGR (RGCN)
    std::vector<ResourceView> screens;   // NSCR (RCSN)
    std::vector<ResourceView> cells;     // NCER (RECN)
    std::vector<ResourceView> anims;     // NANR (RNAN)
};

// Parse a D2KP pack (views point into `data`, which must outlive the result).
// Resources are classified by their own magic, so a resource lands in the right
// vector regardless of which header slot referenced it. Absent types stay empty;
// a non-D2KP blob yields an all-empty pack.
Pk2dPack parse_pk2d(const std::uint8_t* data, std::size_t size);

// Lay every tile out into a single RGBA image (a tile sheet), `tiles_per_row`
// wide, colored with sub-palette `palette_index`. Index 0 renders opaque here so
// the sheet is easy to see.
DecodedTexture render_tile_sheet(
    const TileGraphics& tiles,
    const Palette2D& palette,
    int palette_index = 0,
    int tiles_per_row = 16);

// Compose a full background image from a tilemap, its tile graphics, and the
// palette (each cell selects tile, sub-palette, and flip). With
// `color_zero_transparent` (the default), palette index 0 is transparent;
// pass false for an opaque background (e.g. a full-screen logo).
DecodedTexture compose_background(
    const Tilemap& map,
    const TileGraphics& tiles,
    const Palette2D& palette,
    bool color_zero_transparent = true);

// Serialize an RGBA texture as a 32-bit BMP (for export and viewing).
std::vector<std::uint8_t> to_bmp(const DecodedTexture& image);

}  // namespace khdays::assets
