// A prize picture's effect as a mod gives it (graphics/mo/prize.pak.z/fx/<picture>.txt, docs/KH2_SEQUENCES.md "Field
// prizes"): read and checked, its pages stacked into one atlas, a frame made into the sequence quads the 3D layer
// draws. No game and no GPU here (ui/prize_effects.cpp draws them).
#pragma once

#include "khdays/assets/kh2_sequence.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace khdays::ui::prizes {

enum Blend : std::uint32_t { alpha = 0, add = 1 }; // the sequence quads' blends

struct Layer final {
    int page = 0;
    float u = 0, v = 0, w = 0, h = 0;   // its part of the page, fractions of the page
    std::array<float, 8> corners{};     // top-left, top-right, bottom-left, bottom-right: x, y (y down)
    std::uint32_t colour = 0x80808080u; // RGBA, red in the low byte, 0x80 = the picture as it is
    std::uint32_t blend = Blend::alpha;
};

struct Frame final {
    std::vector<Layer> layers;
};

struct Effect final {
    int body = 0;                   // the first frame's layer made as wide as the game's picture
    int frame_time = 1;             // game frames a frame shows
    std::vector<std::string> pages; // page n's file, beside the effect's
    std::vector<Frame> frames;
};

// The effect's text; nullopt with the reason in `why` when it breaks a rule of the format (its version, a page
// not declared, a frame with no layer, a part of no size, a colour out of 0..2, a blend not alpha or add).
std::optional<Effect> parse_effect(std::string_view text, std::string &why);

// The pages stacked top to bottom: page n's top row and size in the atlas.
struct Atlas final {
    int width = 0, height = 0;
    std::vector<int> top, page_width, page_height;
};
Atlas stack_pages(const std::vector<std::pair<int, int>> &sizes);

// What a frame needs drawn round the picture's middle: half its square (the corners' farthest reach, the
// effect's units) and the body layer's width (the first frame's).
struct Reach final {
    float half = 0, body_width = 0;
};
Reach reach_of(const Effect &e);

// Frame `f`'s layers as quads on a canvas `2 * half` square, the middle the picture's, their texels the atlas's.
std::vector<assets::kh2::SequenceQuad> frame_quads(const Effect &e, int f, const Reach &r, const Atlas &a);

// Every frame's quads side by side, frame k's moved right by k * `column` (at least `2 * half`): the whole
// animation on one canvas, each billboard showing its own frame's column.
std::vector<assets::kh2::SequenceQuad> sheet_quads(const Effect &e, const Reach &r, const Atlas &a, float column);

// The frame shown `frames` game frames on (the loop).
int frame_at(const Effect &e, unsigned frames);

// Which of the prize table's pictures a billboard shows: its texture words' reference among the pictures'
// (`words`, 0 for one not known yet), -1 for none.
int picture_of(std::uint32_t texture, const std::vector<std::uint32_t> &words);

} // namespace khdays::ui::prizes
