#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "khdays/assets/font.h"

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
    const auto path =
        std::filesystem::temp_directory_path() / "khdays_test.nftr";
    try {
        // Minimal NFTR: one 2x2 1bpp glyph mapped from 'A', advance 3.
        Bytes d(0x80, 0U);
        d[0] = 'R'; d[1] = 'T'; d[2] = 'F'; d[3] = 'N';
        d[0x19] = 2;               // line feed
        set_u32(d, 0x20U, 0x40U);  // CGLP data pointer
        set_u32(d, 0x24U, 0x60U);  // CWDH data pointer
        set_u32(d, 0x28U, 0x70U);  // CMAP data pointer

        set_u32(d, 0x3CU, 0x11U);  // CGLP section size (read at cglp-4)
        d[0x40U] = 2;              // cell width
        d[0x41U] = 2;              // cell height
        set_u16(d, 0x42U, 1U);     // cell size (1 byte per glyph)
        d[0x46U] = 1;              // bpp
        d[0x48U] = 0xA0U;          // glyph 0: pixels 1,0,1,0 (MSB first)

        set_u16(d, 0x60U, 0U);     // CWDH first glyph
        set_u16(d, 0x62U, 0U);     // CWDH last glyph
        set_u32(d, 0x64U, 0U);     // next
        d[0x68U] = 0;              // left
        d[0x69U] = 2;              // width
        d[0x6AU] = 3;              // advance

        set_u16(d, 0x70U, 0x41U);  // CMAP first char 'A'
        set_u16(d, 0x72U, 0x41U);  // CMAP last char 'A'
        set_u32(d, 0x74U, 0U);     // method 0 (direct)
        set_u32(d, 0x78U, 0U);     // next
        set_u16(d, 0x7CU, 0U);     // first glyph index

        {
            std::ofstream out{path, std::ios::binary};
            out.write(
                reinterpret_cast<const char*>(d.data()),
                static_cast<std::streamsize>(d.size()));
        }

        const auto font = khdays::assets::decode_nftr(path);
        expect(font.glyphs.size() == 1U, "glyph count");
        expect(font.cell_width == 2 && font.cell_height == 2, "cell size");
        expect(font.bpp == 1, "bpp");
        expect(font.glyph_of(u'A') == 0, "char map");
        expect(font.glyph_of(u'B') == -1, "unmapped char");
        expect(font.glyphs[0].advance == 3, "advance");
        expect(font.glyphs[0].alpha[0] == 255 && font.glyphs[0].alpha[1] == 0
                   && font.glyphs[0].alpha[2] == 255,
               "glyph bitmap");

        const auto image = khdays::assets::render_text(font, u"A");
        expect(image.width == 3 && image.height == 2, "text image size");
        expect(image.rgba[3] == 255, "pixel (0,0) opaque");   // alpha
        expect(image.rgba[4 + 3] == 0, "pixel (1,0) transparent");

        const auto two = khdays::assets::render_text(font, u"AA");
        expect(two.width == 6, "two-glyph width");

        std::filesystem::remove(path);
        std::cout << "Font decoder test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << "Font decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
