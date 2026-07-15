#include "khdays/assets/cell.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

// NCER (cell/OBJ sprite) and NANR (cell animation) decoders, plus cell → RGBA
// composition. The container magics are the little-endian Nitro forms ("RECN"
// with a "KBEC" section; "RNAN" with a "KNBA" section).

namespace khdays::assets {

namespace {

std::uint16_t u16(const std::uint8_t* d, std::size_t size, std::size_t o) {
    if (o + 2U > size) {
        throw std::runtime_error("cell: read past end");
    }
    return static_cast<std::uint16_t>(d[o] | (d[o + 1U] << 8U));
}

std::uint32_t u32(const std::uint8_t* d, std::size_t size, std::size_t o) {
    if (o + 4U > size) {
        throw std::runtime_error("cell: read past end");
    }
    return static_cast<std::uint32_t>(d[o])
        | (static_cast<std::uint32_t>(d[o + 1U]) << 8U)
        | (static_cast<std::uint32_t>(d[o + 2U]) << 16U)
        | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

// OBJ (shape, size) → (width, height) in pixels.
std::array<int, 2> oam_dimensions(int shape, int size) {
    static const int table[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},   // square
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},   // wide
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},   // tall
    };
    if (shape < 0 || shape > 2 || size < 0 || size > 3) {
        return {8, 8};
    }
    return {table[shape][size][0], table[shape][size][1]};
}

}  // namespace

CellBank decode_ncer(const std::uint8_t* data, const std::size_t size) {
    if (size < 0x30U || data[0] != 'R' || data[1] != 'E' || data[2] != 'C'
        || data[3] != 'N') {
        throw std::runtime_error("not an NCER cell bank");
    }
    constexpr std::size_t kb = 0x10U;  // KBEC section
    const auto num_cells = u16(data, size, kb + 0x08U);
    const auto cell_type = u16(data, size, kb + 0x0AU);
    const auto cell_data_off = u32(data, size, kb + 0x0CU);
    const auto mapping_mode = u32(data, size, kb + 0x10U);

    CellBank bank;
    bank.tile_boundary = 32 << (mapping_mode & 0x7U);
    const std::size_t record_size = cell_type == 1U ? 16U : 8U;
    const std::size_t records = kb + 8U + cell_data_off;
    const std::size_t oam_base = records + static_cast<std::size_t>(num_cells) * record_size;

    bank.cells.reserve(num_cells);
    for (std::uint16_t c = 0; c < num_cells; ++c) {
        const std::size_t rec = records + static_cast<std::size_t>(c) * record_size;
        const auto num_oam = u16(data, size, rec);
        const auto oam_off = u32(data, size, rec + 4U);
        Cell cell;
        cell.pieces.reserve(num_oam);
        for (std::uint16_t j = 0; j < num_oam; ++j) {
            const std::size_t p = oam_base + oam_off + static_cast<std::size_t>(j) * 6U;
            const auto a0 = u16(data, size, p);
            const auto a1 = u16(data, size, p + 2U);
            const auto a2 = u16(data, size, p + 4U);

            OamPiece piece;
            const int shape = a0 >> 14;
            const int obj_size = (a1 >> 14) & 0x3;
            const auto dims = oam_dimensions(shape, obj_size);
            piece.width = dims[0];
            piece.height = dims[1];
            int y = a0 & 0xFF;
            if (y >= 128) {
                y -= 256;
            }
            int x = a1 & 0x1FF;
            if (x >= 256) {
                x -= 512;
            }
            piece.x = x;
            piece.y = y;
            piece.tile = a2 & 0x3FF;
            piece.palette = (a2 >> 12) & 0xF;
            piece.flip_h = (a1 & 0x1000) != 0;
            piece.flip_v = (a1 & 0x2000) != 0;
            cell.pieces.push_back(piece);
        }
        bank.cells.push_back(std::move(cell));
    }
    return bank;
}

DecodedTexture render_cell(
    const Cell& cell,
    const TileGraphics& tiles,
    const Palette2D& palette,
    const int tile_boundary) {
    // Bounds over all pieces.
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    bool first = true;
    for (const auto& piece : cell.pieces) {
        if (first) {
            min_x = piece.x;
            min_y = piece.y;
            max_x = piece.x + piece.width;
            max_y = piece.y + piece.height;
            first = false;
            continue;
        }
        min_x = std::min(min_x, piece.x);
        min_y = std::min(min_y, piece.y);
        max_x = std::max(max_x, piece.x + piece.width);
        max_y = std::max(max_y, piece.y + piece.height);
    }

    DecodedTexture image;
    image.name = "cell";
    image.format_name = "NCER";
    image.color_zero_transparent = true;
    image.width = std::max(max_x - min_x, 1);
    image.height = std::max(max_y - min_y, 1);
    image.rgba.assign(
        static_cast<std::size_t>(image.width) * image.height * 4U, 0U);

    const int bytes_per_tile = tiles.bpp == 8 ? 64 : 32;
    const int tiles_per_unit = std::max(1, tile_boundary / bytes_per_tile);
    const int colors = palette.colors_per_palette;

    for (const auto& piece : cell.pieces) {
        const int start_tile = piece.tile * tiles_per_unit;
        const int tiles_wide = piece.width / 8;
        for (int py = 0; py < piece.height; ++py) {
            for (int px = 0; px < piece.width; ++px) {
                const int sx = piece.flip_h ? piece.width - 1 - px : px;
                const int sy = piece.flip_v ? piece.height - 1 - py : py;
                const int tile = start_tile + (sy / 8) * tiles_wide + (sx / 8);
                if (tile < 0 || tile >= tiles.tile_count) {
                    continue;
                }
                const auto index = tiles.indices[
                    static_cast<std::size_t>(tile) * 64U
                    + static_cast<std::size_t>(sy % 8) * 8U
                    + static_cast<std::size_t>(sx % 8)];
                if (index == 0) {
                    continue;  // transparent
                }
                const std::size_t color_index =
                    static_cast<std::size_t>(piece.palette) * colors + index;
                if (color_index >= palette.colors.size()) {
                    continue;
                }
                const auto& color = palette.colors[color_index];
                const int dx = piece.x - min_x + px;
                const int dy = piece.y - min_y + py;
                const std::size_t dst =
                    (static_cast<std::size_t>(dy) * image.width + dx) * 4U;
                image.rgba[dst] = color[0];
                image.rgba[dst + 1U] = color[1];
                image.rgba[dst + 2U] = color[2];
                image.rgba[dst + 3U] = 255U;
            }
        }
    }
    return image;
}

AnimBank decode_nanr(const std::uint8_t* data, const std::size_t size) {
    if (size < 0x30U || data[0] != 'R' || data[1] != 'N' || data[2] != 'A'
        || data[3] != 'N') {
        throw std::runtime_error("not an NANR animation bank");
    }
    constexpr std::size_t kb = 0x10U;  // KNBA section
    const std::size_t base = kb + 8U;  // section data start
    const auto num_anims = u16(data, size, kb + 0x08U);
    const std::size_t seq_array = base + u32(data, size, kb + 0x0CU);
    const std::size_t frame_array = base + u32(data, size, kb + 0x10U);
    const std::size_t frame_data = base + u32(data, size, kb + 0x14U);

    AnimBank bank;
    bank.animations.reserve(num_anims);
    for (std::uint16_t a = 0; a < num_anims; ++a) {
        const std::size_t e = seq_array + static_cast<std::size_t>(a) * 16U;
        if (e + 16U > size) {
            break;
        }
        const auto num_frames = u16(data, size, e);
        const auto frame_off = u32(data, size, e + 0x0CU);
        Animation anim;
        anim.steps.reserve(num_frames);
        for (std::uint16_t f = 0; f < num_frames; ++f) {
            const std::size_t fr = frame_array + frame_off
                + static_cast<std::size_t>(f) * 8U;
            if (fr + 8U > size) {
                break;
            }
            const auto data_off = u32(data, size, fr);
            const auto duration = u16(data, size, fr + 4U);
            AnimStep step;
            step.duration = duration != 0 ? duration : 1;
            const std::size_t cell_ref = frame_data + data_off;
            if (cell_ref + 2U <= size) {
                step.cell = u16(data, size, cell_ref);
            }
            anim.steps.push_back(step);
        }
        bank.animations.push_back(std::move(anim));
    }
    return bank;
}

}  // namespace khdays::assets
