// Frames of a mod's KH2 model mixed (models.cpp): what the game attaches to one of its model's nodes (its weapon),
// where the KH2 joint puts it, turned as the game turns that node by the game's own animation's share; and a joint's
// frames of several poses mixed by their weights, turns the shorter way round. Pure.
#ifndef KHDAYS_ATTACHED_FRAME_H
#define KHDAYS_ATTACHED_FRAME_H

#include <array>
#include <cmath>
#include <cstddef>

namespace khdays::models {

using Mat = std::array<float, 16>; // column-major, for column vectors
using Quat = std::array<float, 4>; // x y z w

namespace frame_detail {

// a frame's turn (its axes made unit) and its axes' scales
inline Mat turn_of(const Mat &m, std::array<float, 3> &scale)
{
    Mat r = Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (std::size_t c = 0; c < 3; ++c) {
        const std::size_t i = c * 4;
        scale[c] = std::sqrt(m[i] * m[i] + m[i + 1] * m[i + 1] + m[i + 2] * m[i + 2]);
        const float l = scale[c] > 1e-12F ? 1.0F / scale[c] : 0.0F;
        r[i] = m[i] * l;
        r[i + 1] = m[i + 1] * l;
        r[i + 2] = m[i + 2] * l;
    }
    return r;
}

inline Quat quat_of(const Mat &r)
{
    const float tr = r[0] + r[5] + r[10];
    if (tr > 0.0F) {
        const float k = std::sqrt(tr + 1.0F) * 2.0F;
        return {(r[6] - r[9]) / k, (r[8] - r[2]) / k, (r[1] - r[4]) / k, 0.25F * k};
    }
    if (r[0] > r[5] && r[0] > r[10]) {
        const float k = std::sqrt(1.0F + r[0] - r[5] - r[10]) * 2.0F;
        return {0.25F * k, (r[4] + r[1]) / k, (r[8] + r[2]) / k, (r[6] - r[9]) / k};
    }
    if (r[5] > r[10]) {
        const float k = std::sqrt(1.0F + r[5] - r[0] - r[10]) * 2.0F;
        return {(r[4] + r[1]) / k, 0.25F * k, (r[9] + r[6]) / k, (r[8] - r[2]) / k};
    }
    const float k = std::sqrt(1.0F + r[10] - r[0] - r[5]) * 2.0F;
    return {(r[8] + r[2]) / k, (r[9] + r[6]) / k, 0.25F * k, (r[1] - r[4]) / k};
}

// a unit quaternion's turn, its axes scaled, at `at`
inline Mat frame_of(Quat q, const std::array<float, 3> &scale, float x0, float y0, float z0)
{
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n > 1e-12F)
        for (float &v : q)
            v /= n;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat out = {1 - 2 * (y * y + z * z),
               2 * (x * y + z * w),
               2 * (x * z - y * w),
               0,
               2 * (x * y - z * w),
               1 - 2 * (x * x + z * z),
               2 * (y * z + x * w),
               0,
               2 * (x * z + y * w),
               2 * (y * z - x * w),
               1 - 2 * (x * x + y * y),
               0,
               x0,
               y0,
               z0,
               1};
    for (std::size_t c = 0; c < 3; ++c)
        for (std::size_t r = 0; r < 3; ++r)
            out[c * 4 + r] *= scale[c];
    return out;
}

} // namespace frame_detail

// What the game attaches to a node (its weapon), at the KH2 joint's place `at` (a `weapon` line's bone and turn),
// turned as `game` (the node's frame as the game sends it) by the game's share: the game's own animations hold it
// as Days does (KH2's grip is turned against Days': b_ro_D002's keyblade, pointed ahead, pointed 45 degrees up);
// KH2 motions as KH2's hand does; a fade between them turns from one to the other
inline Mat attached_frame(const Mat &at, const Mat &game, float share)
{
    using namespace frame_detail;
    if (!(share > 0.001F))
        return at;
    std::array<float, 3> sa{}, sg{};
    const Mat ra = turn_of(at, sa), rg = turn_of(game, sg);
    if (share >= 0.999F)
        return frame_of(quat_of(rg), sg, at[12], at[13], at[14]);
    // between the two turns (quaternions, the shorter way), the scales between theirs
    const Quat qa = quat_of(ra);
    Quat qg = quat_of(rg);
    float d = qa[0] * qg[0] + qa[1] * qg[1] + qa[2] * qg[2] + qa[3] * qg[3];
    if (d < 0.0F) {
        for (float &v : qg)
            v = -v;
        d = -d;
    }
    const float angle = std::acos(std::fmin(1.0F, d));
    const float wa = angle > 1e-4F ? std::sin((1.0F - share) * angle) / std::sin(angle) : 1.0F - share;
    const float wg = angle > 1e-4F ? std::sin(share * angle) / std::sin(angle) : share;
    const Quat q = {qa[0] * wa + qg[0] * wg, qa[1] * wa + qg[1] * wg, qa[2] * wa + qg[2] * wg, qa[3] * wa + qg[3] * wg};
    std::array<float, 3> scale{};
    for (std::size_t c = 0; c < 3; ++c)
        scale[c] = sa[c] + (sg[c] - sa[c]) * share;
    return frame_of(q, scale, at[12], at[13], at[14]);
}

// A joint's frames of `n` poses mixed by their weights (normalized here), as KH2's kn::blend (PS3 0xF659C) for two: its
// x and y axes (made unit) each the weighted sum, made unit; z = x cross y, y = z cross x; its place and its axes'
// scales their weighted mean. Axes that cancel (turns 180 degrees apart) take the heaviest pose's turn.
inline Mat blend_frames(const Mat *frames, const float *weights, std::size_t n)
{
    using namespace frame_detail;
    float total = 0.0F;
    for (std::size_t i = 0; i < n; ++i)
        total += weights[i] > 0.0F ? weights[i] : 0.0F;
    if (n == 0 || !(total > 0.0F))
        return Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 3> x{}, y{}, scale{}, at{};
    Mat heaviest = Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float most = 0.0F;
    for (std::size_t i = 0; i < n; ++i) {
        if (!(weights[i] > 0.0F))
            continue;
        const float w = weights[i] / total;
        std::array<float, 3> s{};
        const Mat r = turn_of(frames[i], s);
        if (w > most) {
            most = w;
            heaviest = r;
        }
        for (std::size_t c = 0; c < 3; ++c) {
            x[c] += r[c] * w;
            y[c] += r[4 + c] * w;
            scale[c] += s[c] * w;
            at[c] += frames[i][12 + c] * w;
        }
    }
    const auto cross = [](const std::array<float, 3> &a, const std::array<float, 3> &b) {
        return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    const auto unit = [](std::array<float, 3> &a) {
        const float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (l < 1e-5F)
            return false;
        for (float &v : a)
            v /= l;
        return true;
    };
    std::array<float, 3> z{};
    bool whole = unit(x) && unit(y);
    if (whole) {
        z = cross(x, y);
        whole = unit(z);
    }
    if (whole) {
        y = cross(z, x);
        whole = unit(y);
    }
    if (!whole)
        for (std::size_t c = 0; c < 3; ++c) {
            x[c] = heaviest[c];
            y[c] = heaviest[4 + c];
            z[c] = heaviest[8 + c];
        }
    Mat out = {x[0] * scale[0], x[1] * scale[0], x[2] * scale[0], 0.0F,
               y[0] * scale[1], y[1] * scale[1], y[2] * scale[1], 0.0F,
               z[0] * scale[2], z[1] * scale[2], z[2] * scale[2], 0.0F,
               at[0],           at[1],           at[2],           1.0F};
    return out;
}

} // namespace khdays::models

#endif
