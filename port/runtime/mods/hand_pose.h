// A mod's KH2 hand under the game's animations (model_hands.cpp, motions.txt's `hand` lines; docs/KH2_MODELS.md, "Hands that
// respond"): how far the wrist bends, a pose's share by a measure, a share fading, a finger joint's turn between its
// poses, a body part's shape for the hand to stay out of, and the arm bent to a moved wrist. Pure.
#ifndef KHDAYS_HAND_POSE_H
#define KHDAYS_HAND_POSE_H

#include "attached_frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace khdays::models {

using HandVec = std::array<float, 3>;

inline float hand_dot(const HandVec &a, const HandVec &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline float hand_length(const HandVec &a)
{
    return std::sqrt(hand_dot(a, a));
}
inline HandVec hand_sub(const HandVec &a, const HandVec &b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
inline HandVec hand_add(const HandVec &a, const HandVec &b, float s = 1.0F)
{
    return {a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s};
}
inline HandVec hand_cross(const HandVec &a, const HandVec &b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline HandVec hand_unit(const HandVec &a, const HandVec &otherwise = {0.0F, 1.0F, 0.0F})
{
    const float l = hand_length(a);
    return l > 1e-7F ? HandVec{a[0] / l, a[1] / l, a[2] / l} : otherwise;
}

// Degrees the hand is bent toward its back against the forearm (0 straight, negative toward the palm), from the way
// the forearm runs in the hand's own axes: +x along the fingers (toward the hand), `back` out of the back of the hand.
// A bend sideways does not count, nor a twist about the forearm.
inline float hand_bend_degrees(const HandVec &forearm_way, const HandVec &back)
{
    return std::atan2(-hand_dot(forearm_way, back), forearm_way[0]) * 57.2957795F;
}

// A pose's share of a measure: none up to `from`, all from `to`, eased between.
inline float hand_share(float value, float from, float to)
{
    if (!(to > from))
        return value >= to ? 1.0F : 0.0F;
    const float t = std::clamp((value - from) / (to - from), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

// A share moved toward `target` over `up` seconds for the whole way rising and `down` falling.
inline float hand_fade(float share, float target, float seconds, float up, float down)
{
    const float span = target > share ? up : down;
    const float step = span > 0.0F ? seconds / span : 1.0F;
    return target > share ? std::min(target, share + step) : std::max(target, share - step);
}

// A value moved toward `target` by a critically damped spring of `rate` (1/s): it never overshoots, and its speed never
// jumps however the target does.
inline void hand_spring(float &value, float &speed, float target, float rate, float seconds)
{
    const float gone = value - target, carried = (speed + rate * gone) * seconds, kept = std::exp(-rate * seconds);
    value = target + (gone + carried) * kept;
    speed = (speed - rate * carried) * kept;
}

// How far each finger opens for what it would go into, the thumb [0] apart: a finger opens at least as far as its own
// need and `link` per finger of the others' (1 all alike, 0 each alone). Each opens from its own curl, so they stay apart.
inline std::array<float, 5> hand_linked_open(const std::array<float, 5> &need, float link)
{
    std::array<float, 5> out = need;
    for (std::size_t i = 1; i < 5; ++i) {
        float carried = 1.0F;
        for (std::size_t step = 1; step < 4; ++step) {
            carried *= link;
            if (i > step && i - step >= 1)
                out[i] = std::max(out[i], need[i - step] * carried);
            if (i + step < 5)
                out[i] = std::max(out[i], need[i + step] * carried);
        }
    }
    return out;
}

// A quaternion from `a` toward `b`, the shorter way round.
inline Quat hand_mix(const Quat &a, Quat b, float t)
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0.0F) {
        for (float &v : b)
            v = -v;
        d = -d;
    }
    const float angle = std::acos(std::min(1.0F, d));
    const float wa = angle > 1e-4F ? std::sin((1.0F - t) * angle) / std::sin(angle) : 1.0F - t;
    const float wb = angle > 1e-4F ? std::sin(t * angle) / std::sin(angle) : t;
    return {a[0] * wa + b[0] * wb, a[1] * wa + b[1] * wb, a[2] * wa + b[2] * wb, a[3] * wa + b[3] * wb};
}

// The poses, in the order later ones win: each mixed over the ones before it by its share.
enum HandPoseIndex { kHandRelaxed, kHandOpen, kHandFist, kHandFlat, kHandGrip, kHandPoses };
using HandPoseTurns = std::array<Quat, kHandPoses>;
using HandShares = std::array<float, kHandPoses>; // [kHandRelaxed] unused

inline Quat hand_pose_turn(const HandPoseTurns &poses, const HandShares &shares)
{
    Quat q = poses[kHandRelaxed];
    for (std::size_t p = kHandOpen; p < kHandPoses; ++p)
        q = hand_mix(q, poses[p], shares[p]);
    return q;
}

// The turn followed by `degrees` more about the joint's own y (a finger's curl: fingers bend about +y).
inline Quat hand_curled(const Quat &q, float degrees)
{
    const float h = degrees * 0.5F * 0.0174532925F, s = std::sin(h), c = std::cos(h);
    return {q[0] * c - q[2] * s, q[1] * c + q[3] * s, q[2] * c + q[0] * s, q[3] * c - q[1] * s};
}

inline Mat hand_turn_matrix(const Quat &q)
{
    return frame_detail::frame_of(q, {1.0F, 1.0F, 1.0F}, 0.0F, 0.0F, 0.0F);
}

// A body part's shape round an axis: how far out its vertices reach, by how far along the axis and at what angle
// round it, so a point inside it comes out by the nearest way along the part's own shape and not a fat capsule's.
struct HandBodyShape {
    static constexpr int kStations = 10, kBins = 16;
    HandVec origin{}, axis{0.0F, 1.0F, 0.0F}, u{1.0F, 0.0F, 0.0F}, v{0.0F, 0.0F, 1.0F};
    float from = 0.0F, to = 0.0F;
    std::array<float, kStations * kBins> reach{};
    bool empty = true;

    void bake(const HandVec &a, const HandVec &b, const std::vector<HandVec> &points)
    {
        origin = a;
        axis = hand_unit(hand_sub(b, a));
        u = hand_unit(
            hand_cross(axis, std::fabs(axis[1]) < 0.9F ? HandVec{0.0F, 1.0F, 0.0F} : HandVec{1.0F, 0.0F, 0.0F}));
        v = hand_cross(axis, u);
        empty = points.empty();
        if (empty)
            return;
        from = 1e30F;
        to = -1e30F;
        for (const HandVec &p : points) {
            const float s = hand_dot(hand_sub(p, origin), axis);
            from = std::min(from, s);
            to = std::max(to, s);
        }
        to = std::max(to, from + 1e-4F);
        reach.fill(0.0F);
        for (const HandVec &p : points) {
            const HandVec d = hand_sub(p, origin);
            const float x = hand_dot(d, u), y = hand_dot(d, v), r = std::sqrt(x * x + y * y);
            auto &slot = reach[static_cast<std::size_t>(station_of(hand_dot(d, axis)) * kBins + bin_of(x, y))];
            slot = std::max(slot, r);
        }
        // a slot no vertex fell in: the most of its neighbours round the ring and along the axis
        for (int pass = 0; pass < kBins; ++pass) {
            bool left = false;
            auto filled = reach;
            for (int s = 0; s < kStations; ++s)
                for (int k = 0; k < kBins; ++k) {
                    if (at(s, k) > 0.0F)
                        continue;
                    const float near = std::max({at(s, (k + 1) % kBins), at(s, (k + kBins - 1) % kBins),
                                                 at(std::min(s + 1, kStations - 1), k), at(std::max(s - 1, 0), k)});
                    filled[static_cast<std::size_t>(s * kBins + k)] = near;
                    left = left || near <= 0.0F;
                }
            reach = filled;
            if (!left)
                break;
        }
    }
    // How far a point is inside it (`margin` further out), and the way out; 0 outside.
    float depth(const HandVec &p, float margin, HandVec &out) const
    {
        if (empty)
            return 0.0F;
        const HandVec d = hand_sub(p, origin);
        const float s = hand_dot(d, axis);
        if (s < from - margin || s > to + margin)
            return 0.0F;
        const float x = hand_dot(d, u), y = hand_dot(d, v), r = std::sqrt(x * x + y * y);
        const float pos = std::clamp((s - from) / (to - from) * static_cast<float>(kStations) - 0.5F, 0.0F,
                                     static_cast<float>(kStations - 1));
        const int s0 = static_cast<int>(pos), s1 = std::min(s0 + 1, kStations - 1);
        const float ang = (std::atan2(y, x) + 3.14159265F) / 6.2831853F * static_cast<float>(kBins) - 0.5F;
        const float wrapped = ang < 0.0F ? ang + static_cast<float>(kBins) : ang;
        const int k0 = std::min(static_cast<int>(wrapped), kBins - 1), k1 = (k0 + 1) % kBins;
        const float fs = pos - static_cast<float>(s0), fk = wrapped - static_cast<float>(k0);
        const float near = at(s0, k0) * (1.0F - fk) + at(s0, k1) * fk, far = at(s1, k0) * (1.0F - fk) + at(s1, k1) * fk;
        // no deeper than it is from either end, so a point passing an end comes out without a jump
        const float inside =
            std::min({near * (1.0F - fs) + far * fs + margin - r, s - (from - margin), (to + margin) - s});
        if (inside <= 0.0F)
            return 0.0F;
        const HandVec way = r > 1e-5F ? hand_add(hand_add({0.0F, 0.0F, 0.0F}, u, x / r), v, y / r) : u;
        out = way;
        return inside;
    }

private:
    float at(int s, int k) const
    {
        return reach[static_cast<std::size_t>(s * kBins + k)];
    }
    int station_of(float s) const
    {
        return std::clamp(static_cast<int>((s - from) / (to - from) * static_cast<float>(kStations)), 0, kStations - 1);
    }
    static int bin_of(float x, float y)
    {
        return std::clamp(static_cast<int>((std::atan2(y, x) + 3.14159265F) / 6.2831853F * static_cast<float>(kBins)),
                          0, kBins - 1);
    }
};

// Where the elbow goes when the wrist moves from `wrist` to `moved`, the shoulder staying, both bones keeping their
// lengths and the arm its bend's plane. A wrist past the arm's reach is taken back into it (`moved` is changed).
inline HandVec hand_arm_elbow(const HandVec &shoulder, const HandVec &elbow, const HandVec &wrist, HandVec &moved)
{
    const float upper = hand_length(hand_sub(elbow, shoulder)), lower = hand_length(hand_sub(wrist, elbow));
    HandVec to = hand_sub(moved, shoulder);
    float reach = hand_length(to);
    const float most = (upper + lower) * 0.999F, least = std::fabs(upper - lower) * 1.001F + 1e-5F;
    const HandVec way = hand_unit(to, hand_unit(hand_sub(wrist, shoulder)));
    if (reach > most || reach < least) {
        reach = std::clamp(reach, least, most);
        moved = hand_add(shoulder, way, reach);
    }
    // the elbow's side: as it is, off the shoulder-to-wrist line
    const HandVec from = hand_sub(elbow, shoulder);
    HandVec side = hand_add(from, way, -hand_dot(from, way));
    if (hand_length(side) < 1e-4F)
        side = hand_cross(way, std::fabs(way[1]) < 0.9F ? HandVec{0.0F, 1.0F, 0.0F} : HandVec{1.0F, 0.0F, 0.0F});
    side = hand_unit(side);
    const float along = (upper * upper - lower * lower + reach * reach) / (2.0F * reach);
    const float off = std::sqrt(std::max(0.0F, upper * upper - along * along));
    return hand_add(hand_add(shoulder, way, along), side, off);
}

} // namespace khdays::models

#endif
