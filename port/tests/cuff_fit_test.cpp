// A sleeve cuff fitted round its hand (runtime/mods/cuff_fit.h): the baked surface, each flap opening and shortening for
// the points inside, the ring coming along, and the motion's springs.
#include "../runtime/mods/cuff_fit.h"

#include <cmath>
#include <cstdio>

using namespace khdays::models;

namespace {

int failures;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "cuff_fit: failed: %s\n", what);
        ++failures;
    }
}

// a round cuff along +x: radius 7 at its root to 8 at x = 10, four flaps at +y, +z, -y, -z, joined round the ring
CuffFit cuff()
{
    std::vector<CuffFit::Triangle> triangles;
    constexpr int kAround = 32;
    const auto ring = [](int i, float x, float r) {
        const float a = static_cast<float>(i) / kAround * 6.2831853F;
        return HandVec{x, r * std::cos(a), r * std::sin(a)};
    };
    const auto flap_of = [](int i) { return ((i + kAround / 8) % kAround) / (kAround / 4); };
    for (int i = 0; i < kAround; ++i) {
        const int n = i + 1;
        triangles.push_back({{ring(i, 0, 7), ring(n, 0, 7), ring(i, 10, 8)}, {-1, -1, flap_of(i)}});
        triangles.push_back({{ring(n, 0, 7), ring(n, 10, 8), ring(i, 10, 8)}, {-1, flap_of(n % kAround), flap_of(i)}});
    }
    CuffFit fit;
    fit.bake({0, 0, 0}, {HandVec{0, 7, 0}, HandVec{0, 0, 7}, HandVec{0, -7, 0}, HandVec{0, 0, -7}},
             {HandVec{10, 8, 0}, HandVec{10, 0, 8}, HandVec{10, -8, 0}, HandVec{10, 0, -8}}, triangles);
    return fit;
}

} // namespace

int main()
{
    const CuffFit fit = cuff();
    check(fit.ready, "bakes");
    const auto at = fit.read({5.0F, 0.0F, 0.0F});
    check(at.within && std::fabs(at.reach - 7.5F) < 0.1F, "reads the surface along the axis");
    check(!fit.read({12.0F, 0.0F, 0.0F}).within, "past the end is not within");

    CuffFit::Settings settings;
    settings.clearance = 0.0F;
    settings.most_shift = 0.0F; // the flaps alone, first
    const auto inside = fit.fit({{5.0F, 3.0F, 0.0F}}, settings);
    check(inside.open[0] == 0.0F && inside.length[0] == 1.0F, "a point inside moves nothing");

    const auto thumb = fit.fit({{5.0F, 9.0F, 0.0F}}, settings);
    check(thumb.open[0] > 0.2F, "a point out past a flap opens it");
    const auto r = fit.read({5.0F, 9.0F, 0.0F});
    check(fit.opened_reach(r, thumb.open) >= 8.99F, "and the surface then clears it");
    check(thumb.open[1] > 0.0F && thumb.open[1] < thumb.open[0], "the flaps beside it come along, less");
    check(std::fabs(thumb.open[1] - thumb.open[3]) < 0.02F, "on both sides");
    check(thumb.open[2] < thumb.open[1], "the far flap least");

    const auto far_end = fit.fit({{9.8F, 9.0F, 0.0F}}, settings);
    check(far_end.length[0] < 1.0F && far_end.length[0] >= settings.shortest, "near the end it shortens, not too far");
    check(far_end.open[0] < 0.05F, "and hardly opens");

    settings.most_shift = 2.0F;
    const auto base = fit.fit({{1.0F, 7.6F, 0.0F}}, settings);
    check(base.shift[1] > 0.1F, "near the wrist the whole cuff slides toward a point");
    const auto slid = fit.read(hand_sub({1.0F, 7.6F, 0.0F}, base.shift));
    check(fit.opened_reach(slid, base.open) >= slid.r - 0.01F, "and what is left the flaps open for");
    CuffFit::Settings no_slide = settings;
    no_slide.most_shift = 0.0F;
    check(fit.fit({{1.0F, 7.6F, 0.0F}}, no_slide).open[0] > base.open[0], "so they open less than without it");
    CuffFit::Settings small = settings;
    small.most_shift = 0.05F;
    check(hand_length(fit.fit({{1.0F, 30.0F, 0.0F}}, small).shift) <= 0.0501F, "it slides no further than the most");
    settings.most_shift = 0.0F;

    // one flap on its own: more clearance moves it alone, past the others' most if it may
    CuffFit::Settings tab = settings;
    const CuffFit::Target plain = fit.fit({{5.0F, 9.0F, 0.0F}}, tab);
    tab.own_flap = 0;
    const CuffFit::Target neutral = fit.fit({{5.0F, 9.0F, 0.0F}}, tab);
    check(neutral.open[0] == plain.open[0] && neutral.open[1] == plain.open[1], "nothing asked of it: nothing changes");
    tab.own_clearance = 1.0F;
    const CuffFit::Target more = fit.fit({{5.0F, 9.0F, 0.0F}}, tab);
    check(more.open[0] > plain.open[0] + 0.05F, "more clearance opens it further");
    check(more.open[1] == plain.open[1] && more.open[2] == plain.open[2], "and no other flap");
    tab.own_clearance = 0.0F;
    tab.own_always = 0.1F;
    const CuffFit::Target always = fit.fit({{5.0F, 3.0F, 0.0F}}, tab);
    check(std::fabs(always.open[0] - 0.1F) < 0.001F && always.open[1] == 0.0F, "an opening it always has, it alone");
    tab = settings;
    tab.most_open = 0.1F;
    tab.own_flap = 0;
    tab.own_most_open = 0.2F;
    const CuffFit::Target wider = fit.fit({{5.0F, 20.0F, 0.0F}}, tab);
    check(std::fabs(wider.open[0] - 0.2F) < 0.001F && wider.open[1] <= 0.1001F, "its own most, the others theirs");

    settings.most_open = 0.1F;
    check(fit.fit({{5.0F, 20.0F, 0.0F}}, settings).open[0] <= 0.1001F, "opens no more than the most");

    // the fit eased: no overshoot; the sway bounded and back to where its weight hangs it
    CuffMotion motion;
    CuffFit::Target target;
    const CuffFit::Shares none{}, droop{0.05F, 0.0F, 0.0F, 0.0F};
    cuff_motion_step(motion, target, none, droop, 0.0F, true);
    check(motion.sway[0] == 0.05F, "fresh: at once");
    target.open[0] = 0.4F;
    float most = 0.0F;
    bool smooth = true;
    float last_step = 0.0F;
    for (int i = 0; i < 60; ++i) {
        const float before = motion.open[0];
        cuff_motion_step(motion, target, none, droop, 1.0F / 30.0F, false);
        most = std::max(most, motion.open[0]);
        const float step = motion.open[0] - before;
        smooth = smooth && std::fabs(step - last_step) < 0.15F;
        last_step = step;
    }
    check(most <= 0.4001F, "the fit never overshoots");
    check(std::fabs(motion.open[0] - 0.4F) < 0.001F, "and arrives");
    check(smooth, "without a jump in its speed");
    const CuffFit::Shares thrown{-200.0F, 200.0F, 0.0F, 0.0F};
    cuff_motion_step(motion, target, thrown, droop, 1.0F / 30.0F, false);
    check(motion.sway[0] <= 0.22F && motion.sway[1] >= -0.05F, "the sway stays bounded");
    for (int i = 0; i < 120; ++i)
        cuff_motion_step(motion, target, none, droop, 1.0F / 30.0F, false);
    check(std::fabs(motion.sway[0] - 0.05F) < 0.005F, "and settles");

    if (failures == 0)
        std::printf("cuff_fit: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
