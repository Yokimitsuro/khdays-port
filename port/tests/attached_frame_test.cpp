// A weapon on a mod's KH2 model (runtime/mods/attached_frame.h): the KH2 joint's frame in a KH2 motion, the game's
// turn at the joint's place in the game's own animation, halfway between them across a fade, the shorter way.
#include "../runtime/mods/attached_frame.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "attached_frame: failed: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 1e-4F;
}

using khdays::models::Mat;

// a turn about z by `degrees`, scaled by `s`, at (x, y, z)
Mat about_z(float degrees, float s, float x, float y, float z)
{
    const float c = std::cos(degrees * 3.14159265F / 180.0F), n = std::sin(degrees * 3.14159265F / 180.0F);
    return {c * s, n * s, 0, 0, -n * s, c * s, 0, 0, 0, 0, s, 0, x, y, z, 1};
}

float angle_of(const Mat &m)
{
    return std::atan2(m[1], m[0]) * 180.0F / 3.14159265F;
}

} // namespace

int main()
{
    using khdays::models::attached_frame;
    const Mat kh2 = about_z(10.0F, 2.0F, 1.0F, 2.0F, 3.0F), game = about_z(55.0F, 2.0F, 9.0F, 9.0F, 9.0F);
    const Mat motion = attached_frame(kh2, game, 0.0F);
    check(motion == kh2, "a KH2 motion keeps the KH2 joint's frame");
    const Mat own = attached_frame(kh2, game, 1.0F);
    check(near(angle_of(own), 55.0F), "the game's animation takes the game's turn");
    check(near(own[12], 1.0F) && near(own[13], 2.0F) && near(own[14], 3.0F), "at the KH2 joint's place");
    check(near(std::hypot(own[0], own[1]), 2.0F) && near(own[10], 2.0F), "its scale kept");
    const Mat half = attached_frame(kh2, game, 0.5F);
    check(near(angle_of(half), 32.5F), "halfway across a fade");
    const Mat wrap = attached_frame(about_z(170.0F, 1.0F, 0, 0, 0), about_z(-170.0F, 1.0F, 0, 0, 0), 0.5F);
    check(near(std::fabs(angle_of(wrap)), 180.0F), "the shorter way round");
    if (failures == 0)
        std::printf("attached_frame: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
