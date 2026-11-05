// A KH2 model's motions against the game's state (runtime/mods/kh2_moves.h): a landing straight out of the damage
// in the air against one after a fall; the fade into an idle started only on going to an idle from an animation with
// a fade, down over its frames, standing in a pause, ended by anything else.
#include "../runtime/mods/kh2_moves.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "kh2_moves: failed: %s\n", what);
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
    using khdays::kh2_moves::IdleFade;
    using khdays::kh2_moves::landed_out_of_air;
    check(landed_out_of_air(true, 1003, 1000, 8), "landing 3 vblanks after the damage in the air: out of it");
    check(!landed_out_of_air(true, 1040, 1000, 8), "landing after a fall it recovered into: not");
    check(!landed_out_of_air(false, 1003, 1000, 8), "another state: not");
    check(!landed_out_of_air(true, 1003, 0, 8), "never in the air: not");

    IdleFade f;
    check(!f.starts(true, false, false, 0.0F, false), "no fade on the animation: none");
    check(!f.starts(true, true, false, 8.0F, false), "an idle already played: none");
    check(!f.starts(false, false, false, 8.0F, false), "going to something else than an idle: none");
    check(!f.starts(true, false, true, 8.0F, false), "from an idle: none");
    check(!f.starts(true, false, false, 8.0F, true), "a tail takes it: none");
    check(f.starts(true, false, false, 8.0F, false), "to an idle from an animation with a fade: starts");
    f.start(8.0F);
    check(near(f.weight(), 1.0F), "the pose before whole at first");
    f.step(2.0F, false);
    check(near(f.weight(), 0.75F), "down over its frames");
    f.step(0.0F, false);
    check(near(f.weight(), 0.75F), "standing in a pause");
    check(!f.starts(true, false, false, 8.0F, false), "not started again while it fades");
    f.step(1.0F, true);
    check(near(f.weight(), 0.0F), "anything else the game plays ends it");
    f.start(4.0F);
    f.step(5.0F, false);
    check(near(f.weight(), 0.0F), "and it ends at its frames");
    if (failures == 0)
        std::printf("kh2_moves: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
