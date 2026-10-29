#include "khdays/assets/graphics2d.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/message.h"  // lz_decompress

// Decoders for the Nintendo DS 2D graphics resources — NCLR (palettes), NCGR
// (character/tile graphics), and NSCR (tilemaps/screens) — plus composition to
// RGBA. The container magics are the little-endian Nitro forms ("RLCN", "RGCN",
// "RCSN") each holding one data section ("TTLP", "RAHC", "NRCS").

namespace khdays::assets {

namespace {

using ByteVector = std::vector<std::uint8_t>;

std::uint16_t read_u16(const ByteVector& d, const std::size_t o) {
    if (o + 2U > d.size()) {
        throw std::runtime_error("2D: read past end of file");
    }
    return static_cast<std::uint16_t>(d[o] | (d[o + 1U] << 8U));
}

std::uint32_t read_u32(const ByteVector& d, const std::size_t o) {
    if (o + 4U > d.size()) {
        throw std::runtime_error("2D: read past end of file");
    }
    return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1U]) << 8U) |
           (static_cast<std::uint32_t>(d[o + 2U]) << 16U) | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

bool has_magic(const ByteVector& d, const char* magic) {
    return d.size() >= 4U && d[0] == magic[0] && d[1] == magic[1] && d[2] == magic[2] && d[3] == magic[3];
}

std::array<std::uint8_t, 4> rgb555_to_rgba(const std::uint16_t c) {
    const auto r5 = static_cast<std::uint8_t>(c & 0x1FU);
    const auto g5 = static_cast<std::uint8_t>((c >> 5U) & 0x1FU);
    const auto b5 = static_cast<std::uint8_t>((c >> 10U) & 0x1FU);
    return {static_cast<std::uint8_t>((r5 << 3U) | (r5 >> 2U)), static_cast<std::uint8_t>((g5 << 3U) | (g5 >> 2U)),
            static_cast<std::uint8_t>((b5 << 3U) | (b5 >> 2U)), 255U};
}

}  // namespace

Palette2D decode_nclr(const std::uint8_t* data, std::size_t size) {
    const ByteVector d(data, data + size);
    if (!has_magic(d, "RLCN")) {
        throw std::runtime_error("not an NCLR palette");
    }
    // TTLP section at 0x10: bitDepth @0x18, dataSize @0x20, palette @0x28.
    const auto bit_depth = read_u32(d, 0x18U);
    const auto data_size = static_cast<std::size_t>(read_u32(d, 0x20U));

    Palette2D palette;
    palette.colors_per_palette = bit_depth == 4U ? 256 : 16;
    const std::size_t colors = data_size / 2U;
    palette.colors.reserve(colors);
    for (std::size_t i = 0; i < colors; ++i) {
        palette.colors.push_back(rgb555_to_rgba(read_u16(d, 0x28U + i * 2U)));
    }
    return palette;
}

TileGraphics decode_ncgr(const std::uint8_t* data, std::size_t size) {
    const ByteVector d(data, data + size);
    if (!has_magic(d, "RGCN")) {
        throw std::runtime_error("not an NCGR graphic");
    }
    // RAHC section at 0x10: bpp @0x1C, dataSize @0x28, dataOffset @0x2C.
    const auto bpp_flag = read_u32(d, 0x1CU);
    const auto data_size = static_cast<std::size_t>(read_u32(d, 0x28U));
    const auto data_offset = static_cast<std::size_t>(read_u32(d, 0x2CU));
    const std::size_t tiles_at = 0x18U + data_offset;  // section data + offset

    TileGraphics tiles;
    tiles.bpp = bpp_flag == 4U ? 8 : 4;
    const std::size_t bytes_per_tile = tiles.bpp == 8 ? 64U : 32U;
    tiles.tile_count = static_cast<int>(data_size / bytes_per_tile);
    tiles.indices.resize(static_cast<std::size_t>(tiles.tile_count) * 64U);

    for (int t = 0; t < tiles.tile_count; ++t) {
        const std::size_t src = tiles_at + static_cast<std::size_t>(t) * bytes_per_tile;
        const std::size_t dst = static_cast<std::size_t>(t) * 64U;
        for (int px = 0; px < 64; ++px) {
            std::uint8_t index = 0;
            if (tiles.bpp == 8) {
                if (src + static_cast<std::size_t>(px) < d.size()) {
                    index = d[src + static_cast<std::size_t>(px)];
                }
            } else {
                const std::size_t byte = src + static_cast<std::size_t>(px) / 2U;
                if (byte < d.size()) {
                    index = (px & 1) == 0 ? (d[byte] & 0x0FU) : (d[byte] >> 4U);
                }
            }
            tiles.indices[dst + static_cast<std::size_t>(px)] = index;
        }
    }
    return tiles;
}

Tilemap decode_nscr(const std::uint8_t* data, std::size_t size) {
    const ByteVector d(data, data + size);
    if (!has_magic(d, "RCSN")) {
        throw std::runtime_error("not an NSCR screen");
    }
    // NRCS section at 0x10: widthPx @0x18, heightPx @0x1A, dataSize @0x20.
    Tilemap map;
    map.width_tiles = read_u16(d, 0x18U) / 8;
    map.height_tiles = read_u16(d, 0x1AU) / 8;
    const auto data_size = static_cast<std::size_t>(read_u32(d, 0x20U));
    const std::size_t count = data_size / 2U;
    map.cells.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto raw = read_u16(d, 0x24U + i * 2U);
        Tilemap::Cell cell;
        cell.tile = static_cast<std::uint16_t>(raw & 0x03FFU);
        cell.flip_h = (raw & 0x0400U) != 0U;
        cell.flip_v = (raw & 0x0800U) != 0U;
        cell.palette = static_cast<std::uint8_t>((raw >> 12U) & 0x0FU);
        map.cells.push_back(cell);
    }
    return map;
}

namespace {

// Read a little-endian u32 straight from a raw pointer + offset (bounds-checked
// by the caller).
std::uint32_t raw_u32(const std::uint8_t* p, const std::size_t o) {
    return static_cast<std::uint32_t>(p[o]) | (static_cast<std::uint32_t>(p[o + 1U]) << 8U) |
           (static_cast<std::uint32_t>(p[o + 2U]) << 16U) | (static_cast<std::uint32_t>(p[o + 3U]) << 24U);
}

// Route a resource into the pack by its own Nitro magic.
void classify_pk2d_resource(Pk2dPack& out, const ResourceView view) {
    if (view.size < 4U) {
        return;
    }
    const char m0 = static_cast<char>(view.data[0]);
    const char m1 = static_cast<char>(view.data[1]);
    const char m2 = static_cast<char>(view.data[2]);
    const char m3 = static_cast<char>(view.data[3]);
    const auto is = [&](const char* magic) {
        return m0 == magic[0] && m1 == magic[1] && m2 == magic[2] && m3 == magic[3];
    };
    if (is("RLCN")) {
        out.palettes.push_back(view);
    } else if (is("RGCN")) {
        out.tiles.push_back(view);
    } else if (is("RCSN")) {
        out.screens.push_back(view);
    } else if (is("RECN")) {
        out.cells.push_back(view);
    } else if (is("RNAN")) {
        out.anims.push_back(view);
    }
}

}  // namespace

SlotContainer parse_slot_container(const std::uint8_t* data, const std::size_t size) {
    SlotContainer out;
    if (data == nullptr || size < 0x28U) {
        return out;
    }
    const bool kaph = data[0] == 'K' && data[1] == 'A' && data[2] == 'P' && data[3] == 'H';
    const bool d2kp = data[0] == 'D' && data[1] == '2' && data[2] == 'K' && data[3] == 'P';
    if (!kaph && !d2kp) {
        return out;
    }
    out.valid = true;
    // Eight slot pointers at +0x08..+0x24. Each points at a section
    // {u32 count; u32 offsets[count]; u32 sizes[count]}.
    for (std::size_t slot = 0U; slot < 8U; ++slot) {
        const std::uint32_t section = raw_u32(data, 0x08U + slot * 4U);
        if (section == 0xFFFFFFFFU || section == 0U || static_cast<std::size_t>(section) + 4U > size) {
            continue;
        }
        const std::uint32_t count = raw_u32(data, section);
        if (count == 0U || count > 0x1000U) {
            continue;
        }
        const std::size_t offsets = static_cast<std::size_t>(section) + 4U;
        const std::size_t sizes = offsets + static_cast<std::size_t>(count) * 4U;
        if (sizes + static_cast<std::size_t>(count) * 4U > size) {
            continue;
        }
        for (std::uint32_t i = 0U; i < count; ++i) {
            const std::size_t off = raw_u32(data, offsets + i * 4U);
            const std::size_t len = raw_u32(data, sizes + i * 4U);
            if (off < size && len >= 4U && off + len <= size) {
                out.slots[slot].push_back(ResourceView{data + off, len});
            }
        }
    }
    return out;
}

Pk2dPack parse_pk2d(const std::uint8_t* data, const std::size_t size) {
    Pk2dPack out;
    // A D2KP is a slot container; classify every listed resource by its own
    // magic, so the slot->type assignment does not need to be hard-coded.
    if (data == nullptr || size < 0x28U || data[0] != 'D' || data[1] != '2' || data[2] != 'K' || data[3] != 'P') {
        return out;
    }
    const SlotContainer container = parse_slot_container(data, size);
    for (const auto& slot : container.slots) {
        for (const ResourceView& entry : slot) {
            classify_pk2d_resource(out, entry);
        }
    }
    return out;
}

DecodedTexture compose_background(const Tilemap& map, const TileGraphics& tiles, const Palette2D& palette,
                                  const bool color_zero_transparent) {
    if (map.width_tiles <= 0 || map.height_tiles <= 0 || map.width_tiles > 128 || map.height_tiles > 128 ||
        (tiles.bpp != 4 && tiles.bpp != 8) || palette.colors_per_palette <= 0 ||
        map.cells.size() != static_cast<std::size_t>(map.width_tiles) * map.height_tiles) {
        throw std::runtime_error("Invalid background dimensions or tile format");
    }
    DecodedTexture out;
    out.width = map.width_tiles * 8;
    out.height = map.height_tiles * 8;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const auto& cell = map.cells[static_cast<std::size_t>(y / 8) * map.width_tiles + x / 8];
            const int tx = cell.flip_h ? 7 - x % 8 : x % 8;
            const int ty = cell.flip_v ? 7 - y % 8 : y % 8;
            const std::size_t source = static_cast<std::size_t>(cell.tile) * 64 + ty * 8 + tx;
            if (cell.tile >= tiles.tile_count || source >= tiles.indices.size()) {
                throw std::runtime_error("Background references a missing tile");
            }
            const auto index = tiles.indices[source];
            const std::size_t color = index + (tiles.bpp == 4 ? cell.palette * palette.colors_per_palette : 0);
            if (color >= palette.colors.size()) {
                throw std::runtime_error("Background references a missing palette color");
            }
            auto rgba = palette.colors[color];
            if (color_zero_transparent && index == 0) {
                rgba[3] = 0;
            }
            std::copy(rgba.begin(), rgba.end(), out.rgba.begin() + (static_cast<std::size_t>(y) * out.width + x) * 4);
        }
    }
    return out;
}

}  // namespace khdays::assets
