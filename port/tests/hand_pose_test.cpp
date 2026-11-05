// A mod's KH2 hand (runtime/mods/hand_pose.h): the wrist's bend, a pose's share, a share fading, the turn between the
// poses, a body part's shape and the arm bent to a moved wrist.
#include "../runtime/mods/hand_pose.h"

#include <cmath>
#include <cstdio>

using namespace khdays::models;

namespace {

int failures;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "hand_pose: failed: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 0.01F)
{
    return std::fabs(a - b) <= eps;
}

Quat about_y(float degrees)
{
    const float h = degrees * 0.5F * 0.0174532925F;
    return {0.0F, std::sin(h), 0.0F, std::cos(h)};
}

// the angle about y of a turn about y
float y_degrees(const Quat &q)
{
    const Mat m = hand_turn_matrix(q);
    return std::atan2(m[8], m[0]) * 57.2957795F;
}

} // namespace

int main()
{
    // the hand's axes: x along the fingers, z its back. Hand bent up by a: the forearm runs (cos a, 0, -sin a) in them
    const HandVec back{0.0F, 0.0F, 1.0F};
    const auto way = [](float a) {
        const float r = a * 0.0174532925F;
        return HandVec{std::cos(r), 0.0F, -std::sin(r)};
    };
    check(near(hand_bend_degrees(way(0.0F), back), 0.0F), "straight is 0");
    check(near(hand_bend_degrees(way(45.0F), back), 45.0F), "bent up is positive");
    check(near(hand_bend_degrees(way(-30.0F), back), -30.0F), "bent toward the palm is negative");
    check(near(hand_bend_degrees({0.8F, 0.6F, 0.0F}, back), 0.0F), "a bend sideways does not count");
    check(near(hand_bend_degrees(way(45.0F), {0.0F, 0.0F, -1.0F}), -45.0F), "the other hand's back");

    check(near(hand_share(-20.0F, 0.0F, 45.0F), 0.0F), "below: none");
    check(near(hand_share(22.5F, 0.0F, 45.0F), 0.5F), "half way: half");
    check(near(hand_share(80.0F, 0.0F, 45.0F), 1.0F), "above: all");

    check(near(hand_fade(0.0F, 1.0F, 0.05F, 0.2F, 0.4F), 0.25F), "rises by its own time");
    check(near(hand_fade(1.0F, 0.0F, 0.1F, 0.2F, 0.4F), 0.75F), "falls by its own time");
    check(near(hand_fade(0.9F, 1.0F, 0.5F, 0.2F, 0.4F), 1.0F), "stops at the target");
    check(near(hand_fade(0.0F, 1.0F, 0.01F, 0.0F, 0.0F), 1.0F), "no fade: at once");

    HandPoseTurns poses{about_y(20.0F), about_y(5.0F), about_y(70.0F), about_y(-10.0F), about_y(60.0F)};
    const auto mixed = [&](float open, float fist, float flat, float grip) {
        HandShares shares{};
        shares[kHandOpen] = open;
        shares[kHandFist] = fist;
        shares[kHandFlat] = flat;
        shares[kHandGrip] = grip;
        return y_degrees(hand_pose_turn(poses, shares));
    };
    check(near(mixed(0, 0, 0, 0), 20.0F), "relaxed alone");
    check(near(mixed(1, 0, 0, 0), 5.0F), "open alone");
    check(near(mixed(1, 1, 0, 0), 70.0F), "the fist beats the open hand");
    check(near(mixed(1, 1, 1, 0), -10.0F), "contact beats the fist");
    check(near(mixed(1, 1, 1, 1), 60.0F), "the grip beats all");
    check(near(mixed(0, 0, 0.5F, 0), 5.0F), "between relaxed and flat");
    poses[kHandGrip] = {-poses[kHandGrip][0], -poses[kHandGrip][1], -poses[kHandGrip][2], -poses[kHandGrip][3]};
    check(near(mixed(0, 0, 0, 0.5F), 40.0F), "the shorter way round");
    check(near(y_degrees(hand_curled(about_y(30.0F), 15.0F)), 45.0F), "a curl adds about y");

    const auto linked = hand_linked_open({0.9F, 0.0F, 0.0F, 0.8F, 0.0F}, 0.5F);
    check(near(linked[0], 0.9F), "the thumb keeps its own");
    check(near(linked[3], 0.8F), "a finger keeps its own need");
    check(near(linked[2], 0.4F) && near(linked[4], 0.4F), "its neighbours open by the link");
    check(near(linked[1], 0.2F), "further off, less");
    const auto thumb_alone = hand_linked_open({1.0F, 0.0F, 0.0F, 0.0F, 0.0F}, 1.0F);
    check(thumb_alone[1] == 0.0F && thumb_alone[4] == 0.0F, "the thumb opens no finger");
    const auto apart = hand_linked_open({0.0F, 0.0F, 0.6F, 0.0F, 0.0F}, 0.0F);
    check(apart[1] == 0.0F && near(apart[2], 0.6F), "no link: each alone");
    const auto untouched = hand_linked_open({0.0F, 0.0F, 0.0F, 0.0F, 0.0F}, 0.8F);
    check(untouched[1] == 0.0F && untouched[4] == 0.0F, "nothing in the way: nothing opens");

    // a part 10 wide on x, 5 deep on z, 20 long on y
    std::vector<HandVec> points;
    for (int k = 0; k < 64; ++k) {
        const float a = static_cast<float>(k) / 64.0F * 6.2831853F;
        for (float y = 0.0F; y <= 20.0F; y += 2.0F)
            points.push_back({10.0F * std::cos(a), y, 5.0F * std::sin(a)});
    }
    HandBodyShape part;
    part.bake({0.0F, 0.0F, 0.0F}, {0.0F, 20.0F, 0.0F}, points);
    HandVec out{};
    check(part.depth({0.0F, 10.0F, 2.0F}, 0.0F, out) > 2.0F, "inside, near its flat side");
    check(near(std::fabs(out[2]), 1.0F, 0.05F), "comes out the way it is nearest the axis");
    check(part.depth({0.0F, 10.0F, 6.5F}, 0.0F, out) == 0.0F,
          "outside its deep side, inside the same reach on its wide");
    check(part.depth({8.5F, 10.0F, 0.0F}, 0.0F, out) > 1.0F, "inside its wide side");
    check(part.depth({12.0F, 10.0F, 0.0F}, 0.0F, out) == 0.0F, "outside its wide side");
    check(part.depth({12.0F, 10.0F, 0.0F}, 3.0F, out) > 0.0F, "within the margin");
    check(part.depth({0.0F, 40.0F, 0.0F}, 0.0F, out) == 0.0F, "past its end");
    HandBodyShape none;
    none.bake({0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {});
    check(none.depth({0.0F, 0.0F, 0.0F}, 1.0F, out) == 0.0F, "a part without vertices holds nothing");

    // the arm: shoulder at the origin, bones of 30 and 25, bent
    const HandVec shoulder{0.0F, 0.0F, 0.0F}, elbow{30.0F, 0.0F, 0.0F}, wrist{30.0F, 25.0F, 0.0F};
    HandVec moved{28.0F, 28.0F, 3.0F};
    const HandVec e = hand_arm_elbow(shoulder, elbow, wrist, moved);
    check(near(hand_length(hand_sub(e, shoulder)), 30.0F), "the upper arm keeps its length");
    check(near(hand_length(hand_sub(moved, e)), 25.0F), "the forearm keeps its length");
    check(e[1] < 14.0F, "the elbow stays on its side");
    HandVec far{100.0F, 100.0F, 0.0F};
    const HandVec stretched = hand_arm_elbow(shoulder, elbow, wrist, far);
    check(hand_length(far) <= 55.0F && hand_length(hand_sub(far, stretched)) > 24.9F,
          "a wrist out of reach comes back");

    if (failures == 0)
        std::printf("hand_pose: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
