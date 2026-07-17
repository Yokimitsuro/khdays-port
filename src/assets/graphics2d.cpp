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
    return static_cast<std::uint32_t>(d[o])
        | (static_cast<std::uint32_t>(d[o + 1U]) << 8U)
        | (static_cast<std::uint32_t>(d[o + 2U]) << 16U)
        | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

// Read a resource, transparently LZ-decompressing a ".z" blob.
ByteVector load_resource(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("cannot open 2D resource: " + path.string());
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff end = stream.tellg();
    ByteVector data(end > 0 ? static_cast<std::size_t>(end) : 0U);
    stream.seekg(0, std::ios::beg);
    if (!data.empty()) {
        stream.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    }
    if (!data.empty() && (data[0] == 0x10U || data[0] == 0x11U)) {
        data = lz_decompress(data);
    }
    return data;
}

bool has_magic(const ByteVector& d, const char* magic) {
    return d.size() >= 4U && d[0] == magic[0] && d[1] == magic[1]
        && d[2] == magic[2] && d[3] == magic[3];
}

std::array<std::uint8_t, 4> rgb555_to_rgba(const std::uint16_t c) {
    const auto r5 = static_cast<std::uint8_t>(c & 0x1FU);
    const auto g5 = static_cast<std::uint8_t>((c >> 5U) & 0x1FU);
    const auto b5 = static_cast<std::uint8_t>((c >> 10U) & 0x1FU);
    return {
        static_cast<std::uint8_t>((r5 << 3U) | (r5 >> 2U)),
        static_cast<std::uint8_t>((g5 << 3U) | (g5 >> 2U)),
        static_cast<std::uint8_t>((b5 << 3U) | (b5 >> 2U)),
        255U};
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
                    index = (px & 1) == 0 ? (d[byte] & 0x0FU)
                                          : (d[byte] >> 4U);
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

Palette2D decode_nclr(const std::filesystem::path& path) {
    const auto d = load_resource(path);
    return decode_nclr(d.data(), d.size());
}
TileGraphics decode_ncgr(const std::filesystem::path& path) {
    const auto d = load_resource(path);
    return decode_ncgr(d.data(), d.size());
}
Tilemap decode_nscr(const std::filesystem::path& path) {
    const auto d = load_resource(path);
    return decode_nscr(d.data(), d.size());
}

ResourceView find_nitro_resource(
    const std::uint8_t* pack, const std::size_t size, const char magic[4]) {
    if (pack == nullptr || size < 12U) {
        return {};
    }
    for (std::size_t i = 0; i + 12U <= size; ++i) {
        if (pack[i] == static_cast<std::uint8_t>(magic[0])
            && pack[i + 1U] == static_cast<std::uint8_t>(magic[1])
            && pack[i + 2U] == static_cast<std::uint8_t>(magic[2])
            && pack[i + 3U] == static_cast<std::uint8_t>(magic[3])) {
            const std::size_t file_size = static_cast<std::size_t>(pack[i + 8U])
                | (static_cast<std::size_t>(pack[i + 9U]) << 8U)
                | (static_cast<std::size_t>(pack[i + 10U]) << 16U)
                | (static_cast<std::size_t>(pack[i + 11U]) << 24U);
            if (file_size >= 16U && i + file_size <= size) {
                return ResourceView{pack + i, file_size};
            }
        }
    }
    return {};
}

namespace {

// Read a little-endian u32 straight from a raw pointer + offset (bounds-checked
// by the caller).
std::uint32_t raw_u32(const std::uint8_t* p, const std::size_t o) {
    return static_cast<std::uint32_t>(p[o])
        | (static_cast<std::uint32_t>(p[o + 1U]) << 8U)
        | (static_cast<std::uint32_t>(p[o + 2U]) << 16U)
        | (static_cast<std::uint32_t>(p[o + 3U]) << 24U);
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

Pk2dPack parse_pk2d(const std::uint8_t* data, const std::size_t size) {
    Pk2dPack out;
    if (data == nullptr || size < 0x28U
        || data[0] != 'D' || data[1] != '2' || data[2] != 'K' || data[3] != 'P') {
        return out;
    }
    // Fixed type-section pointer slots live at header +0x08..+0x24. Each points
    // at a section: {u32 count; u32 offsets[count]; u32 sizes[count]}. We read
    // every non-empty slot and classify each listed resource by its own magic,
    // so the slot→type assignment does not need to be hard-coded.
    for (std::size_t slot = 0x08U; slot + 4U <= 0x28U; slot += 4U) {
        const std::uint32_t section = raw_u32(data, slot);
        if (section == 0xFFFFFFFFU || section == 0U
            || static_cast<std::size_t>(section) + 4U > size) {
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
                classify_pk2d_resource(out, ResourceView{data + off, len});
            }
        }
    }
    return out;
}

namespace {

// Look up a palette colour, guarding out-of-range indices.
std::array<std::uint8_t, 4> palette_color(
    const Palette2D& palette, const int sub, const int index) {
    const std::size_t offset =
        static_cast<std::size_t>(sub) * static_cast<std::size_t>(palette.colors_per_palette)
        + static_cast<std::size_t>(index);
    if (offset < palette.colors.size()) {
        return palette.colors[offset];
    }
    return {0U, 0U, 0U, 255U};
}

}  // namespace

DecodedTexture render_tile_sheet(
    const TileGraphics& tiles,
    const Palette2D& palette,
    const int palette_index,
    const int tiles_per_row) {
    const int per_row = tiles_per_row > 0 ? tiles_per_row : 16;
    const int rows = (tiles.tile_count + per_row - 1) / std::max(per_row, 1);

    DecodedTexture image;
    image.name = "tile_sheet";
    image.format_name = tiles.bpp == 8 ? "NCGR-8bpp" : "NCGR-4bpp";
    image.width = per_row * 8;
    image.height = std::max(rows, 1) * 8;
    image.rgba.assign(
        static_cast<std::size_t>(image.width) * image.height * 4U, 0U);

    for (int t = 0; t < tiles.tile_count; ++t) {
        const int tx = (t % per_row) * 8;
        const int ty = (t / per_row) * 8;
        for (int py = 0; py < 8; ++py) {
            for (int px = 0; px < 8; ++px) {
                const auto index = tiles.indices[
                    static_cast<std::size_t>(t) * 64U
                    + static_cast<std::size_t>(py) * 8U
                    + static_cast<std::size_t>(px)];
                const auto color = palette_color(palette, palette_index, index);
                const std::size_t dst =
                    (static_cast<std::size_t>(ty + py) * image.width
                     + static_cast<std::size_t>(tx + px)) * 4U;
                image.rgba[dst] = color[0];
                image.rgba[dst + 1U] = color[1];
                image.rgba[dst + 2U] = color[2];
                image.rgba[dst + 3U] = color[3];
            }
        }
    }
    return image;
}

DecodedTexture compose_background(
    const Tilemap& map,
    const TileGraphics& tiles,
    const Palette2D& palette,
    const bool color_zero_transparent) {
    DecodedTexture image;
    image.name = "background";
    image.format_name = "NSCR";
    image.color_zero_transparent = color_zero_transparent;
    image.width = map.width_tiles * 8;
    image.height = map.height_tiles * 8;
    image.rgba.assign(
        static_cast<std::size_t>(image.width) * image.height * 4U, 0U);

    for (int cy = 0; cy < map.height_tiles; ++cy) {
        for (int cx = 0; cx < map.width_tiles; ++cx) {
            const auto& cell =
                map.cells[static_cast<std::size_t>(cy) * map.width_tiles + cx];
            if (cell.tile >= tiles.tile_count) {
                continue;
            }
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    const int sx = cell.flip_h ? 7 - px : px;
                    const int sy = cell.flip_v ? 7 - py : py;
                    const auto index = tiles.indices[
                        static_cast<std::size_t>(cell.tile) * 64U
                        + static_cast<std::size_t>(sy) * 8U
                        + static_cast<std::size_t>(sx)];
                    if (index == 0 && color_zero_transparent) {
                        continue;  // transparent
                    }
                    const auto color = palette_color(palette, cell.palette, index);
                    const std::size_t dst =
                        (static_cast<std::size_t>(cy * 8 + py) * image.width
                         + static_cast<std::size_t>(cx * 8 + px)) * 4U;
                    image.rgba[dst] = color[0];
                    image.rgba[dst + 1U] = color[1];
                    image.rgba[dst + 2U] = color[2];
                    image.rgba[dst + 3U] = color[3];
                }
            }
        }
    }
    return image;
}

// A 32-bit BMP written with the plain 40-byte BITMAPINFOHEADER and BI_RGB has
// no alpha: the fourth byte of each pixel is undefined by the format, so
// readers are entitled to ignore it, and they do -- Pillow opens such a file as
// RGB and drops it on the floor. That silently flattened every A3I5/A5I3
// texture in the game (3- and 5-bit alpha) to fully opaque on the way to the
// preview gallery's PNGs.
//
// BITMAPV4HEADER carries explicit channel masks, so the alpha is part of the
// file's declared meaning rather than a byte we hope survives. load_bmp() reads
// it back, and so do image editors, which matters because a --dump-textures BMP
// is exactly what a modder edits and feeds back through mods/.
constexpr std::uint32_t kBmpV4HeaderSize = 108U;
constexpr std::uint32_t kBmpPixelOffset = 14U + kBmpV4HeaderSize;  // 122
constexpr std::uint32_t kBmpCompressionBitfields = 3U;             // BI_BITFIELDS

std::vector<std::uint8_t> to_bmp(const DecodedTexture& image) {
    const int w = image.width;
    const int h = image.height;
    const std::uint32_t pixel_bytes = static_cast<std::uint32_t>(w) * h * 4U;
    const std::uint32_t size = kBmpPixelOffset + pixel_bytes;

    std::vector<std::uint8_t> bmp;
    bmp.reserve(size);
    const auto put16 = [&](std::uint16_t v) {
        bmp.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        bmp.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    };
    const auto put32 = [&](std::uint32_t v) {
        bmp.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        bmp.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
        bmp.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
        bmp.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
    };

    put16(0x4D42);  // "BM"
    put32(size);
    put32(0);
    put32(kBmpPixelOffset);
    put32(kBmpV4HeaderSize);
    put32(static_cast<std::uint32_t>(w));
    put32(static_cast<std::uint32_t>(h));
    put16(1);        // planes
    put16(32);       // bits per pixel
    put32(kBmpCompressionBitfields);
    put32(pixel_bytes);
    put32(2835);     // 72 DPI
    put32(2835);
    put32(0);
    put32(0);
    // The masks that make the alpha official. Pixels are stored little-endian
    // as B,G,R,A bytes, so as a u32 that is 0xAARRGGBB.
    put32(0x00FF0000U);  // red
    put32(0x0000FF00U);  // green
    put32(0x000000FFU);  // blue
    put32(0xFF000000U);  // alpha
    put32(0x73524742U);  // colour space "sRGB", big-endian in the file
    for (int i = 0; i < 9; ++i) {
        put32(0);        // CIEXYZTRIPLE endpoints (3 x 3), unused for sRGB
    }
    put32(0);            // gamma red
    put32(0);            // gamma green
    put32(0);            // gamma blue

    // BMP rows are bottom-up; store BGRA.
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t src =
                (static_cast<std::size_t>(y) * w + x) * 4U;
            bmp.push_back(image.rgba[src + 2U]);  // B
            bmp.push_back(image.rgba[src + 1U]);  // G
            bmp.push_back(image.rgba[src]);       // R
            bmp.push_back(image.rgba[src + 3U]);  // A
        }
    }
    return bmp;
}

}  // namespace khdays::assets
