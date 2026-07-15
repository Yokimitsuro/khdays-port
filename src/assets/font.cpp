#include "khdays/assets/font.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/message.h"  // lz_decompress

// Decoder for the Nintendo DS NFTR bitmap font ("RTFN"): the FINF header points
// at the glyph bitmaps (CGLP), per-glyph widths (CWDH), and character maps
// (CMAP, three addressing modes, chained). Glyph bitmaps are a packed 1- or
// 2-bpp coverage grid.

namespace khdays::assets {

namespace {

using ByteVector = std::vector<std::uint8_t>;

std::uint16_t read_u16(const ByteVector& d, const std::size_t o) {
    if (o + 2U > d.size()) {
        throw std::runtime_error("NFTR: read past end of file");
    }
    return static_cast<std::uint16_t>(d[o] | (d[o + 1U] << 8U));
}

std::uint32_t read_u32(const ByteVector& d, const std::size_t o) {
    if (o + 4U > d.size()) {
        throw std::runtime_error("NFTR: read past end of file");
    }
    return static_cast<std::uint32_t>(d[o])
        | (static_cast<std::uint32_t>(d[o + 1U]) << 8U)
        | (static_cast<std::uint32_t>(d[o + 2U]) << 16U)
        | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

ByteVector load_resource(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("cannot open font: " + path.string());
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

}  // namespace

Font decode_nftr(const std::filesystem::path& path) {
    const auto d = load_resource(path);
    if (d.size() < 0x20U || d[0] != 'R' || d[1] != 'T' || d[2] != 'F'
        || d[3] != 'N') {
        throw std::runtime_error("not an NFTR font: " + path.string());
    }

    // FINF at 0x10; its pointers target section data (8 past the section tag).
    Font font;
    font.line_feed = d[0x19U];
    const auto cglp = static_cast<std::size_t>(read_u32(d, 0x20U));
    const auto cwdh = static_cast<std::size_t>(read_u32(d, 0x24U));
    const auto cmap = static_cast<std::size_t>(read_u32(d, 0x28U));

    // CGLP: cell_width, cell_height, u16 cell_size, baseline, max_width, bpp.
    font.cell_width = d[cglp];
    font.cell_height = d[cglp + 1U];
    const auto cell_size = static_cast<std::size_t>(read_u16(d, cglp + 2U));
    font.bpp = d[cglp + 6U];
    const std::size_t glyph_count = cell_size > 0U
        ? (static_cast<std::size_t>(read_u32(d, cglp - 4U)) - 0x10U) / cell_size
        : 0U;
    const std::size_t pixels =
        static_cast<std::size_t>(font.cell_width) * font.cell_height;

    font.glyphs.resize(glyph_count);
    const std::size_t bitmaps = cglp + 8U;
    const int levels = (1 << font.bpp) - 1;
    for (std::size_t g = 0; g < glyph_count; ++g) {
        auto& glyph = font.glyphs[g];
        glyph.alpha.assign(pixels, 0U);
        const std::size_t base = bitmaps + g * cell_size;
        for (std::size_t p = 0; p < pixels; ++p) {
            const std::size_t bit = p * static_cast<std::size_t>(font.bpp);
            std::uint32_t value = 0;
            for (int b = 0; b < font.bpp; ++b) {
                const std::size_t byte = base + (bit + static_cast<std::size_t>(b)) / 8U;
                const int shift = 7 - static_cast<int>((bit + static_cast<std::size_t>(b)) % 8U);
                const std::uint32_t v =
                    byte < d.size() ? ((d[byte] >> shift) & 1U) : 0U;
                value = (value << 1U) | v;
            }
            glyph.alpha[p] = static_cast<std::uint8_t>(
                levels > 0 ? value * 255U / static_cast<std::uint32_t>(levels) : 0U);
        }
    }

    // CWDH chain: per-glyph {left, width, advance} for glyphs [first, last].
    std::size_t widths_off = cwdh;
    while (widths_off >= 8U && widths_off < d.size()) {
        const auto first = read_u16(d, widths_off);
        const auto last = read_u16(d, widths_off + 2U);
        const auto next = read_u32(d, widths_off + 4U);
        std::size_t entry = widths_off + 8U;
        for (int g = first; g <= last; ++g) {
            if (g >= 0 && static_cast<std::size_t>(g) < font.glyphs.size()
                && entry + 3U <= d.size()) {
                font.glyphs[static_cast<std::size_t>(g)].left =
                    static_cast<std::int8_t>(d[entry]);
                font.glyphs[static_cast<std::size_t>(g)].width = d[entry + 1U];
                font.glyphs[static_cast<std::size_t>(g)].advance = d[entry + 2U];
            }
            entry += 3U;
        }
        if (next == 0U) {
            break;
        }
        widths_off = next;
    }

    // CMAP chain: map character codes to glyph indices.
    std::size_t map_off = cmap;
    while (map_off >= 8U && map_off < d.size()) {
        const auto first_char = read_u16(d, map_off);
        const auto last_char = read_u16(d, map_off + 2U);
        const auto method = read_u32(d, map_off + 4U);
        const auto next = read_u32(d, map_off + 8U);
        const std::size_t data = map_off + 12U;

        if (method == 0U) {  // direct
            const auto first_glyph = read_u16(d, data);
            for (int c = first_char; c <= last_char; ++c) {
                font.char_to_glyph[static_cast<char16_t>(c)] =
                    first_glyph + (c - first_char);
            }
        } else if (method == 1U) {  // table
            std::size_t entry = data;
            for (int c = first_char; c <= last_char; ++c) {
                if (entry + 2U > d.size()) {
                    break;
                }
                const auto glyph = read_u16(d, entry);
                entry += 2U;
                if (glyph != 0xFFFFU) {
                    font.char_to_glyph[static_cast<char16_t>(c)] = glyph;
                }
            }
        } else if (method == 2U) {  // scan
            const auto pairs = read_u16(d, data);
            for (std::size_t i = 0; i < pairs; ++i) {
                const std::size_t entry = data + 2U + i * 4U;
                if (entry + 4U > d.size()) {
                    break;
                }
                font.char_to_glyph[static_cast<char16_t>(read_u16(d, entry))] =
                    read_u16(d, entry + 2U);
            }
        }
        if (next == 0U) {
            break;
        }
        map_off = next;
    }

    return font;
}

DecodedTexture render_text(const Font& font, const std::u16string& text) {
    // Measure.
    int width = 0;
    int line_width = 0;
    int lines = 1;
    const int line_height = font.line_feed > 0 ? font.line_feed : font.cell_height;
    for (const char16_t c : text) {
        if (c == u'\n') {
            width = std::max(width, line_width);
            line_width = 0;
            ++lines;
            continue;
        }
        const int g = font.glyph_of(c);
        line_width += g >= 0 ? font.glyphs[static_cast<std::size_t>(g)].advance
                             : font.cell_width / 2;
    }
    width = std::max(width, line_width);

    DecodedTexture image;
    image.name = "text";
    image.format_name = "NFTR";
    image.color_zero_transparent = true;
    image.width = std::max(width, 1);
    image.height = std::max(lines * line_height, 1);
    image.rgba.assign(
        static_cast<std::size_t>(image.width) * image.height * 4U, 0U);

    int pen_x = 0;
    int pen_y = 0;
    for (const char16_t c : text) {
        if (c == u'\n') {
            pen_x = 0;
            pen_y += line_height;
            continue;
        }
        const int g = font.glyph_of(c);
        if (g < 0) {
            pen_x += font.cell_width / 2;
            continue;
        }
        const auto& glyph = font.glyphs[static_cast<std::size_t>(g)];
        for (int gy = 0; gy < font.cell_height; ++gy) {
            for (int gx = 0; gx < font.cell_width; ++gx) {
                const auto a = glyph.alpha[
                    static_cast<std::size_t>(gy) * font.cell_width
                    + static_cast<std::size_t>(gx)];
                if (a == 0) {
                    continue;
                }
                const int x = pen_x + glyph.left + gx;
                const int y = pen_y + gy;
                if (x < 0 || x >= image.width || y < 0 || y >= image.height) {
                    continue;
                }
                const std::size_t dst =
                    (static_cast<std::size_t>(y) * image.width + x) * 4U;
                image.rgba[dst] = 255U;
                image.rgba[dst + 1U] = 255U;
                image.rgba[dst + 2U] = 255U;
                image.rgba[dst + 3U] = a;
            }
        }
        pen_x += glyph.advance;
    }
    return image;
}

}  // namespace khdays::assets
