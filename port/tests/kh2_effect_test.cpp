// KINGDOM HEARTS II's effects read and played (assets/kh2_effect.h): a made-up PAX in the layout KH2's own have
// (docs/FORMATS.md, "KH2's effects") -- one block, one emitter, a model and a picture -- and its run: a layer's
// keyed records placing and colouring a quad, its life, a repeating effect's hold and its end once stopped.
#include "khdays/assets/kh2_effect.h"
#include "kh2_effect_made_up.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace khdays::assets::kh2;
using khdays_test::made_up;

namespace {

void expect(bool test, const char *why)
{
    if (!test)
        throw std::runtime_error(why);
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-4F;
}

EffectMatrix identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

void test_reading()
{
    const auto bytes = made_up(0, 0, false);
    const auto blocks = read_effect(bytes.data(), bytes.size());
    expect(blocks.size() == 1, "one block");
    const auto &b = blocks[0];
    expect(b.emitters.size() == 1 && b.emitters[0].layers.size() == 1, "one emitter, one layer");
    expect(b.emitters[0].length == 30 * 0x1000 && !b.emitters[0].repeat, "the emitter's length and flag");
    const auto &L = b.emitters[0].layers[0];
    expect(L.commands.size() == 6 && L.commands[2].keys.size() == 2, "the commands and their keys");
    expect(L.commands[3].slots[1] == 0x30, "a command's slots");
    expect(b.models.size() == 1 && b.models[0].primitives.size() == 1 && b.models[0].vertices.size() == 4,
           "the model");
    expect(b.models[0].alpha == 0x48 && b.models[0].primitives[0].corners == 4, "the model's blend and quad");
    expect(near(b.models[0].primitives[0].uv[3][0], 1.0F), "a quad's UV");
    expect(b.pictures.size() == 1 && b.pictures[0].w == 2, "the picture");
    const auto page = effect_page({&b.pictures[0]}, b.models[0].primitives[0].tex0);
    expect(page.width == 2 && page.height == 2, "the page's size from the TEX0");
    expect(page.rgba[4] == 0xff && page.rgba[7] == 0xff, "entry 1 red, its alpha 0x80 made 255");
    expect(page.rgba[15] == 0x80, "entry 3's alpha 0x40 made 0x80");

    auto broken = bytes;
    broken[0x10] = 0;
    bool refused = false;
    try {
        read_effect(broken.data(), broken.size());
    } catch (const std::runtime_error &) {
        refused = true;
    }
    expect(refused, "no directory refused");
}

void test_run()
{
    const auto bytes = made_up(3, 0, false);
    auto block = std::make_shared<const EffectBlock>(read_effect(bytes.data(), bytes.size())[0]);
    EffectInstance fx(block, 0, 1);
    EffectCamera cam{identity(), identity(), identity()};
    std::vector<EffectDraw> draws;
    fx.tick(cam, &draws);
    expect(draws.size() == 1 && draws[0].triangles.size() == 6, "a quad as two triangles");
    const auto &t = draws[0].triangles;
    // vertex 0 (-1, -1) x scale 2 + (0, -10): (-2, -12); its colour 0x80 x 0x80 / 128 / 128
    expect(near(t[0].x, -2) && near(t[0].y, -12) && near(t[0].w, 1), "placed by its matrix");
    expect(near(t[0].r, 1) && near(t[0].a, 1), "coloured by pppColor at 1");
    expect(draws[0].alpha == 0x48 && !draws[0].depth_write, "the model's blend, no depth written (flag bit 0)");
    draws.clear();
    fx.tick(cam, &draws);
    draws.clear();
    fx.tick(cam, &draws); // frame 2: the colour's second key halves the alpha
    expect(!draws.empty() && near(draws[0].triangles[0].a, 0.5F), "a keyed record on its frame");
    expect(fx.objects() == 0, "the layer gone after its life");
    for (int k = 0; k < 40 && !fx.done(); ++k)
        fx.tick(cam, nullptr);
    expect(fx.done(), "a non-repeating effect done after its length");
}

void test_hold_and_stop()
{
    const auto bytes = made_up(0, 4, true);
    auto block = std::make_shared<const EffectBlock>(read_effect(bytes.data(), bytes.size())[0]);
    EffectInstance fx(block, 0, 1);
    EffectCamera cam{identity(), identity(), identity()};
    for (int k = 0; k < 200; ++k)
        fx.tick(cam, nullptr);
    expect(!fx.done() && fx.objects() == 1, "a repeating effect holds, its layer looping");
    fx.stop();
    for (int k = 0; k < 10 && !fx.done(); ++k)
        fx.tick(cam, nullptr);
    expect(fx.done(), "stopped: its never-ending layer let go, the effect done");
}

void test_unknown_command_refused()
{
    auto bytes = made_up(0, 0, false);
    // the first command's code -> one not read
    const std::size_t L = 0x40 + 0x80 + 0x110 + 0x20;
    bytes[L + 0x28] = 0xf0;
    auto block = std::make_shared<const EffectBlock>(read_effect(bytes.data(), bytes.size())[0]);
    bool refused = false;
    try {
        EffectInstance fx(block, 0, 1);
    } catch (const std::runtime_error &) {
        refused = true;
    }
    expect(refused, "a command not read refused");
}

} // namespace

int main()
{
    try {
        test_reading();
        test_run();
        test_hold_and_stop();
        test_unknown_command_refused();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "kh2 effect: %s\n", e.what());
        return 1;
    }
    std::printf("kh2 effect: all checks passed\n");
    return 0;
}
