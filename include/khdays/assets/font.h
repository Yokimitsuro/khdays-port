#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "khdays/assets/tex0.h"  // DecodedTexture

namespace khdays::assets {

// A decoded NFTR bitmap font: per-glyph alpha coverage plus proportional
// widths, and a character-code -> glyph map.
struct Font final {
    int cell_width = 0;
    int cell_height = 0;
    int bpp = 1;
    int line_feed = 0;

    struct Glyph final {
        std::vector<std::uint8_t> alpha;  // cell_width * cell_height, 0..255
        int left = 0;      // pixels to skip before drawing
        int width = 0;     // drawn glyph width
        int advance = 0;   // pen advance
    };
    std::vector<Glyph> glyphs;
    std::map<char16_t, int> char_to_glyph;

    // Glyph index for a character, or -1 if unmapped.
    int glyph_of(char16_t code) const {
        const auto it = char_to_glyph.find(code);
        return it == char_to_glyph.end() ? -1 : it->second;
    }
};

// Decode an NFTR font, transparently LZ-decompressing a ".z" blob. Throws
// std::runtime_error on malformed data.
Font decode_nftr(const std::filesystem::path& path);

// Render a UTF-16 string to an RGBA image: white, anti-aliased glyphs on a
// transparent background. Unmapped characters advance by the font's default
// space. Newlines start a new line (line_feed tall).
DecodedTexture render_text(const Font& font, const std::u16string& text);

}  // namespace khdays::assets
