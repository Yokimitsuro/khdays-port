// KH2's "sys" font as the KH2 enemies' mod copies it, for the port's pieces that write in it: the reaction commands'
// names (reaction_command.cpp) and the player's name on a KH2 mission's piece (kh2_mission_2d.cpp).
#pragma once

#include "khdays/assets/texture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace khdays::ui::kh2 {

// The font (kh2/msg/us/: fontinfo.bar's "sys" widths, fontimage.bar's HD picture): 280 glyphs of 18 x 24 dots in 28
// columns over a 512 x 256 picture, a glyph's slot its KH2 byte - 0x20, the last one the space (6 dots wide); grey
// glyphs KH2 draws white
struct SysFont {
    static constexpr int kCellW = 18, kCellH = 24, kColumns = 28, kGlyphs = 280, kSpace = 6;
    bool tried = false;
    std::vector<std::uint8_t> widths;
    khdays::assets::DecodedTexture atlas;
    float texel = 1; // the picture's texels a KH2 dot

    // Read once from the mod; false without it (told once)
    bool get();
    // A character's slot (KH2's international glyphs, our reading of the font: its bytes from 0x20 in groups), -1
    // none; the space the last
    static int slot(char16_t c);
    float advance(int s) const;
    // The text's width, KH2 dots
    float width(const std::u16string &text) const;
    // The glyphs' cover at `out`'s texels (`w` x `h`): the text from x, y texels, `k` texels a KH2 dot, the
    // picture read between its texels
    void cover(const std::u16string &text, std::vector<float> &out, int w, int h, float x, float y, float k) const;
};

// The one font, shared
SysFont &sys_font();

// The text in KH2's font painted into `out` (`w` x `h` texels) from x, y texels, `k` texels a KH2 dot: white with a
// dark edge, as KH2's command names
void paint_text(const std::u16string &text, std::vector<std::uint32_t> &out, int w, int h, float x, float y, float k);

} // namespace khdays::ui::kh2
