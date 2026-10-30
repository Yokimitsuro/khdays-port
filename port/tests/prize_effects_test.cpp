// A prize picture drawn as a mod's effect (ui/prize_effects_model.*): the effect file read and checked, the prize
// table's pictures found by their texture words, a frame made into sequence quads and drawn by the reference
// rasterizer. Made-up effects only.
#include "../ui/prize_effects_model.h"
#include "../gfx/kh2_sequence.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace prizes = khdays::ui::prizes;
namespace render = khdays::render::kh2;

void expect(bool test, const char *why)
{
    if (!test)
        throw std::runtime_error(why);
}

// contracts/effect-format.md's example (made up, not KH2's)
const char *const example = R"(effect 1
page 0 orb.png
body 1
frame_time 2
frame
layer page 0 rect 0.5 0.5 0.25 0.5 corners -42 -42 42 -42 -42 42 42 42 colour 1.1 0.25 0.25 1 blend add
layer page 0 rect 0 0.5 0.25 0.5 corners -28 -28 28 -28 -28 28 28 28 colour 1 1 1 1 blend alpha
frame
layer page 0 rect 0.5 0.5 0.25 0.5 corners -42 -42 42 -42 -42 42 42 42 colour 1.2 0.3 0.3 0.8 blend add
layer page 0 rect 0 0.5 0.25 0.5 corners -28 -28 28 -28 -28 28 28 28 colour 1.1 1.1 1.1 1 blend alpha
)";

void reads_the_example()
{
    std::string why;
    const auto e = prizes::parse_effect(example, why);
    expect(e.has_value(), why.c_str());
    expect(e->body == 1 && e->frame_time == 2 && e->pages.size() == 1 && e->pages[0] == "orb.png", "header");
    expect(e->frames.size() == 2 && e->frames[0].layers.size() == 2, "frames and layers");
    const auto &glow = e->frames[0].layers[0];
    expect(glow.blend == prizes::Blend::add && glow.page == 0, "the glow adds");
    expect(glow.u == 0.5F && glow.v == 0.5F && glow.w == 0.25F && glow.h == 0.5F, "the glow's part of the page");
    expect(glow.corners[0] == -42 && glow.corners[7] == 42, "the glow's corners");
    expect(glow.colour == (141u | 32u << 8 | 32u << 16 | 128u << 24), "colour 1.1 0.25 0.25 1 as bytes (0x80 = 1)");
    expect(e->frames[0].layers[1].blend == prizes::Blend::alpha, "the core is mixed by its alpha");
    // a frame lasts frame_time game frames, and the frames loop
    expect(prizes::frame_at(*e, 0) == 0 && prizes::frame_at(*e, 1) == 0 && prizes::frame_at(*e, 2) == 1 &&
               prizes::frame_at(*e, 4) == 0,
           "the loop");
}

void refuses_what_breaks_the_rules()
{
    const std::string body = "page 0 orb.png\nframe\n";
    const std::string layer = "layer page 0 rect 0 0 1 1 corners -1 -1 1 -1 -1 1 1 1 colour 1 1 1 1 blend alpha\n";
    const std::vector<std::string> broken{
        "effect 2\n" + body + layer,  // another version
        "frame\n" + layer,            // no version first
        "effect 1\nframe\n" + layer,  // its page not declared
        "effect 1\npage 0 orb.png\n", // no frame
        "effect 1\n" + body,          // a frame with no layer
        "effect 1\n" + body + "layer page 0 rect 0 0 0 1 corners -1 -1 1 -1 -1 1 1 1 colour 1 1 1 1 blend alpha\n",
        "effect 1\n" + body + "layer page 0 rect 0 0 1 1 corners -1 -1 1 -1 -1 1 1 1 colour 3 1 1 1 blend alpha\n",
        "effect 1\n" + body + "layer page 0 rect 0 0 1 1 corners -1 -1 1 -1 -1 1 1 1 colour 1 1 1 1 blend screen\n",
        "effect 1\nframe_time 0\n" + body + layer, // a frame of no time
        "effect 1\nbody 2\n" + body + layer,       // a body the first frame has not
        "effect 1\n" + body + "layer page 0 rect 0 0 1 1 corners -1 -1 1 colour 1 1 1 1 blend alpha\n",
    };
    for (const auto &text : broken) {
        std::string why;
        expect(!prizes::parse_effect(text, why).has_value() && !why.empty(), text.c_str());
    }
    std::string why;
    expect(prizes::parse_effect("# a comment\neffect 1 # its version\n" + body + layer, why).has_value(), why.c_str());
}

void finds_the_pictures()
{
    // a made-up table: eleven texture words 8 bytes apart, two not known yet (0)
    std::vector<std::uint32_t> words;
    for (std::uint32_t i = 0; i < 11; ++i)
        words.push_back(i == 4 || i == 9 ? 0 : 0x2100060u + 8 * i);
    expect(prizes::picture_of(0x2100060u + 8, words) == 1, "the second picture");
    expect(prizes::picture_of(0x2100060u + 8 * 10, words) == 10, "the last picture");
    expect(prizes::picture_of(0x2100060u + 8 * 4, words) == -1, "a picture not known yet");
    expect(prizes::picture_of(0x2100064u, words) == -1, "an address inside an entry is none");
    expect(prizes::picture_of(0, words) == -1, "no texture");
    expect(prizes::picture_of(0x2100060u, {}) == -1, "no table");
}

void draws_a_frame()
{
    std::string why;
    const auto e = prizes::parse_effect(example, why);
    expect(e.has_value(), why.c_str());
    // one 64 x 32 page: its left half white (opaque), its right half red, all opaque
    khdays::assets::DecodedTexture atlas;
    atlas.width = 64;
    atlas.height = 32;
    atlas.rgba.assign(64 * 32 * 4, 255);
    for (int y = 0; y < 32; ++y)
        for (int x = 32; x < 64; ++x) {
            auto *p = &atlas.rgba[(std::size_t(y) * 64 + std::size_t(x)) * 4];
            p[1] = p[2] = 0;
        }
    const auto a = prizes::stack_pages({{64, 32}});
    expect(a.width == 64 && a.height == 32 && a.top[0] == 0, "one page stacked");
    const auto reach = prizes::reach_of(*e);
    expect(reach.half == 42 && reach.body_width == 56, "the reach and the body's width");
    const auto quads = prizes::frame_quads(*e, 0, reach, a);
    expect(quads.size() == 2 && quads[0].blend == 1 && quads[1].blend == 0, "two quads: added, then mixed");
    expect(quads[0].vertices[0].x == 0 && quads[0].vertices[3].x == 84, "the glow spans the canvas");
    expect(quads[1].vertices[0].x == 14 && quads[1].vertices[0].u == 0 && quads[1].vertices[3].u == 16,
           "the core's place and texels");
    // drawn on a dark grey canvas 84 x 84, one pixel a unit
    const int side = 84;
    std::vector<std::uint32_t> canvas(std::size_t(side * side), 0xff202020u);
    render::draw_sequence(quads, atlas, a.width, a.height, canvas, side, side);
    const auto at = [&](int x, int y) { return canvas[std::size_t(y * side + x)]; };
    const auto red = [](std::uint32_t c) { return c & 255u; };
    const auto green = [](std::uint32_t c) { return (c >> 8) & 255u; };
    // the corner: only the glow, its red light added to the grey (red up, green little changed)
    expect(red(at(4, 4)) > 0x20 + 100 && green(at(4, 4)) < 0x20 + 40, "the glow adds red light");
    // the middle: the core covers it, white (the page's left half) times 1
    expect(red(at(42, 42)) > 240 && green(at(42, 42)) > 240, "the core covers the middle");
    // outside the canvas's quads nothing changes: none here, the glow spans it all
}

void lays_the_frames_side_by_side()
{
    std::string why;
    const auto e = prizes::parse_effect(example, why);
    expect(e.has_value(), why.c_str());
    const auto a = prizes::stack_pages({{64, 32}});
    const auto reach = prizes::reach_of(*e);
    const auto sheet = prizes::sheet_quads(*e, reach, a, 90);
    const auto first = prizes::frame_quads(*e, 0, reach, a), second = prizes::frame_quads(*e, 1, reach, a);
    expect(sheet.size() == first.size() + second.size(), "every frame's layers");
    expect(sheet[0].vertices[0].x == first[0].vertices[0].x, "the first frame in the first column");
    expect(sheet[2].vertices[0].x == second[0].vertices[0].x + 90 && sheet[2].vertices[3].x == 84 + 90,
           "the second frame one column right");
    expect(sheet[2].vertices[0].u == second[0].vertices[0].u && sheet[3].blend == second[1].blend,
           "its texels and blends kept");
    expect(sheet[1].vertices[0].color == first[1].vertices[0].color, "its colours kept");
}

} // namespace

int main()
{
    try {
        reads_the_example();
        refuses_what_breaks_the_rules();
        finds_the_pictures();
        draws_a_frame();
        lays_the_frames_side_by_side();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "prize effects: %s\n", e.what());
        return 1;
    }
    std::puts("prize effects: ok");
    return 0;
}
