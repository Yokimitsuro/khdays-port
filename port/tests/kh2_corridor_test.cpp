// KH2's Dark Corridor in Days's ovals (runtime/mods/kh2_corridor_fit.h): its axes, size and units in an oval's place
// -- KH2's floor on Days's own floor shadow, its oval's top (430 up, y down) on Days's seen top, its front on the
// oval's front, its clip space back in Days's units -- and when it closes: how long its closing takes, measured on a made-up effect
// (kh2_effect_made_up.h), and the moment within the oval's closing clip that ends both together.
#include "../runtime/mods/kh2_corridor_fit.h"
#include "kh2_effect_made_up.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>

using namespace khdays::kh2_corridor;

namespace {

void expect(bool test, const char *why)
{
    if (!test)
        throw std::runtime_error(why);
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-4F * std::fmax(1.0F, std::fabs(b));
}

Matrix identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// A point of the effect through its camera, back in Days's clip space (x `units`, as the corridors submit it).
std::array<float, 4> through(const kh2::EffectCamera &c, float units, float x, float y, float z)
{
    std::array<float, 4> p{x, y, z, 1};
    for (const Matrix *m : {&c.world, &c.view, &c.projection}) {
        std::array<float, 4> q{};
        for (std::size_t k = 0; k < 4; ++k)
            q[k] = p[0] * (*m)[k] + p[1] * (*m)[4 + k] + p[2] * (*m)[8 + k] + p[3] * (*m)[12 + k];
        p = q;
    }
    for (float &v : p)
        v *= units;
    return p;
}

void test_axes_and_size()
{
    // the oval drawn at the view's origin, no projection: what comes out is the oval's own space
    Matrix view = identity();
    const float units = corridor_units(view);
    expect(near(units, (kDaysOvalTop - kDaysFloorLift) / kKh2OvalTop),
           "Days's units a KH2 unit: Days's floor shadow to its seen top over KH2's oval's");
    const auto cam = corridor_camera(view, identity(), units);
    const auto top = through(cam, units, 0, -kKh2OvalTop, 0);
    expect(near(top[0], 0) && near(top[1], kDaysOvalTop) && near(top[2], 0) && near(top[3], 1),
           "KH2's oval's top (y down) on Days's oval's seen top");
    const auto floor = through(cam, units, 0, 0, 0);
    expect(near(floor[0], 0) && near(floor[1], kDaysFloorLift) && near(floor[2], 0),
           "KH2's floor on Days's floor shadow, above the oval's floor");
    const auto front = through(cam, units, 0, 0, 100);
    expect(front[2] > 0 && near(front[2], 100 * units), "KH2's front (+z, where its mask's opening looks) on Days's");
    const auto side = through(cam, units, 150, 0, 0);
    expect(near(side[0], -150 * units), "x turned with them: half a turn about z, not a mirror");

    // KH2's view space: y down, z forward -- above the oval is -y there
    const std::array<float, 4> up{0, -kKh2OvalTop, 0, 1};
    float view_y = 0;
    for (std::size_t r = 0; r < 4; ++r)
        view_y += up[r] * cam.view[r * 4 + 1];
    expect(view_y < 0, "up is -y in KH2's view space");
}

void test_scaled_and_moved()
{
    // the oval drawn twice its size, moved: the corridor twice KH2's fit too, sprites with it (the view made unit)
    Matrix view = identity();
    view[0] = view[5] = view[10] = 2.0F;
    view[12] = 5, view[13] = 6, view[14] = 7;
    const float units = corridor_units(view);
    expect(near(units, 2.0F * kDaysPerKh2), "the oval's own scale taken into the units");
    expect(near(view[0], 1.0F) && near(view[12], 5.0F), "the view made unit, its place kept");
    const auto cam = corridor_camera(view, identity(), units);
    const auto top = through(cam, units, 0, -kKh2OvalTop, 0);
    expect(near(top[0], 5) && near(top[1], 6 + 2 * kDaysOvalTop) && near(top[2], 7), "placed and sized");
    expect(near(through(cam, units, 0, 0, 0)[1], 6 + 2 * kDaysFloorLift), "its floor lifted at the oval's scale");
}

void test_closing()
{
    const auto bytes = khdays_test::made_up(0, 4, true);
    auto block = std::make_shared<const kh2::EffectBlock>(kh2::read_effect(bytes.data(), bytes.size())[0]);
    const std::uint32_t ticks = closing_ticks(block);
    // the same run by hand: its length (30 frames), then stopped
    kh2::EffectInstance fx(block, 0, 0);
    kh2::EffectCamera none{identity(), identity(), identity()};
    for (int t = 0; t < 30; ++t)
        fx.tick(none, nullptr);
    fx.stop();
    std::uint32_t by_hand = 0;
    while (!fx.done() && by_hand < 1000) {
        fx.tick(none, nullptr);
        ++by_hand;
    }
    expect(fx.done() && ticks == by_hand && ticks > 0, "the closing's length measured on its own run");

    expect(closes_now(46, 2, 92) && !closes_now(47, 2, 92), "closes when the clip has as long left as it takes");
    expect(closes_now(0, 2, 0) && !closes_now(-1, 2, 92), "at the clip's end at the latest; not without the clip");
}

} // namespace

int main()
{
    try {
        test_axes_and_size();
        test_scaled_and_moved();
        test_closing();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "kh2 corridor: %s\n", e.what());
        return 1;
    }
    std::printf("kh2 corridor: all checks passed\n");
    return 0;
}
