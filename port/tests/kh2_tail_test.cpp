// A KH2 motion played out after its game animation (runtime/mods/kh2_tail.h): the rest of the motion, then a
// `finish` slot, cross-faded, then out into the idle; the game leaving its idles dropping it; a cut without a blend;
// nothing when the motion was over; a pause standing still.
#include "../runtime/mods/kh2_tail.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "kh2_tail: failed: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-4F;
}

} // namespace

int main()
{
    using khdays::kh2_tail::Piece;
    using khdays::kh2_tail::Tail;
    // the damage (slot 54, 70 ticks) left at tick 60, then slot 24 (26 ticks), fades of 8 frames
    Tail t;
    t.start({{54, 60.0F, 70.0F}, {24, 0.0F, 26.0F}}, 8.0F);
    check(t.active() && t.slot() == 54 && near(t.ticks(), 60.0F) && near(t.weight(), 1.0F), "starts where it stood");
    t.step(4.0F, true); // 8 ticks: 68
    check(t.slot() == 54 && near(t.ticks(), 68.0F), "the rest plays on the game's frames");
    t.step(2.0F, true); // 72: 2 into slot 24
    check(t.slot() == 24 && near(t.ticks(), 2.0F), "then the finish slot, from its start");
    check(t.faded_slot() == 54 && near(t.faded_ticks(), 70.0F) && near(t.faded_weight(), 1.0F),
          "the piece before held at its end, fading out under it");
    t.step(4.0F, true);
    check(near(t.faded_weight(), 0.5F), "fading over the blend's frames");
    t.step(0.0F, true);
    check(near(t.ticks(), 10.0F) && near(t.faded_weight(), 0.5F), "a pause stands still");
    t.step(8.0F, true); // 26: slot 24 at its end
    check(t.ending() && near(t.ticks(), 26.0F) && near(t.weight(), 1.0F) && t.faded_slot() < 0,
          "the last piece held at its end");
    t.step(4.0F, true);
    check(t.active() && near(t.weight(), 0.5F), "fading out into the idle's motion");
    t.step(4.0F, true);
    check(!t.active(), "then over");

    // the game attacks again during it: dropped, fading out where it stands
    Tail c;
    c.start({{604, 40.0F, 50.0F}}, 8.0F);
    c.step(1.0F, true);
    c.step(2.0F, false);
    check(c.active() && near(c.weight(), 0.75F) && near(c.ticks(), 42.0F),
          "the game out of its idles: fades out, standing");
    c.step(6.0F, true);
    check(!c.active(), "and stays dropped when the game is idle again");

    // no blend: cuts
    Tail k;
    k.start({{604, 40.0F, 50.0F}, {24, 0.0F, 26.0F}}, 0.0F);
    k.step(6.0F, true);
    check(k.slot() == 24 && k.faded_slot() < 0 && near(k.ticks(), 2.0F), "pieces cut into each other");
    k.step(1.0F, false);
    check(!k.active(), "and out at once");

    // the motion was over already and nothing follows: nothing plays
    Tail n;
    n.start({{604, 50.0F, 50.0F}}, 8.0F);
    check(!n.active(), "nothing left: nothing plays");

    // the game cutting into another animation: gone at once, its fading piece too, whatever its fade
    Tail g;
    g.start({{604, 40.0F, 50.0F}, {24, 0.0F, 26.0F}}, 8.0F);
    g.step(6.0F, true); // into the second piece: the first fading out under it
    g.stop();
    check(!g.active() && g.faded_weight() == 0.0F, "a cut: gone at once");
    g.step(1.0F, false);
    check(!g.active(), "and stays gone");
    if (failures == 0)
        std::printf("kh2_tail: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
