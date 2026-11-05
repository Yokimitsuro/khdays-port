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

    // a joint's frames of several poses mixed: turns the shorter way, no joint shrunk between turns far apart
    using khdays::models::blend_frames;
    const Mat two[2] = {about_z(0.0F, 1.0F, 0, 0, 0), about_z(170.0F, 1.0F, 2, 0, 0)};
    const float even[2] = {1.0F, 1.0F};
    const Mat mixed = blend_frames(two, even, 2);
    check(near(angle_of(mixed), 85.0F), "an even mix turns halfway");
    check(near(std::hypot(mixed[0], mixed[1]), 1.0F), "a mix of turns 170 degrees apart keeps the joint's size");
    check(near(mixed[12], 1.0F), "its place the weighted mean");
    const Mat three[3] = {about_z(10.0F, 2.0F, 0, 0, 0), about_z(20.0F, 2.0F, 0, 0, 0), about_z(30.0F, 2.0F, 0, 0, 0)};
    const float some[3] = {0.0F, 0.5F, 0.5F};
    const Mat part = blend_frames(three, some, 3);
    check(near(angle_of(part), 25.0F) && near(std::hypot(part[0], part[1]), 2.0F), "a weight of 0 leaves a pose out");
    const Mat flipped[2] = {about_z(-170.0F, 1.0F, 0, 0, 0), about_z(170.0F, 1.0F, 0, 0, 0)};
    check(near(std::fabs(angle_of(blend_frames(flipped, even, 2))), 180.0F), "the shorter way round, mixed");
    // as KH2's kn::blend: the axes mixed, not the turn, so a quarter of 90 degrees is atan(1/3), 18.43
    const Mat right[2] = {about_z(0.0F, 1.0F, 0, 0, 0), about_z(90.0F, 1.0F, 0, 0, 0)};
    const float quarter[2] = {3.0F, 1.0F};
    check(std::fabs(angle_of(blend_frames(right, quarter, 2)) - 18.435F) < 0.01F, "the axes are mixed, as kn::blend");
    const Mat opposite[2] = {about_z(0.0F, 1.0F, 0, 0, 0), about_z(180.0F, 1.0F, 0, 0, 0)};
    const float more_first[2] = {2.0F, 1.0F}, half_each[2] = {1.0F, 1.0F};
    check(near(angle_of(blend_frames(opposite, more_first, 2)), 0.0F), "a lean still mixes across 180 degrees");
    const Mat cancelled = blend_frames(opposite, half_each, 2);
    check(near(std::hypot(cancelled[0], cancelled[1]), 1.0F), "axes that cancel take a pose's turn, whole");
    if (failures == 0)
        std::printf("attached_frame: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
