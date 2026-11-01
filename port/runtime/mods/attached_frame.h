// What the game attaches to one of its model's nodes (its weapon), drawn on a mod's KH2 model: where the KH2 joint
// puts it, turned as the game turns that node by the game's own animation's share (models.cpp). Pure.
#ifndef KHDAYS_ATTACHED_FRAME_H
#define KHDAYS_ATTACHED_FRAME_H

#include <array>
#include <cmath>

namespace khdays::models {

using Mat = std::array<float, 16>; // column-major, for column vectors

// What the game attaches to a node (its weapon), at the KH2 joint's place `at` (a `weapon` line's bone and turn),
// turned as `game` (the node's frame as the game sends it) by the game's share: the game's own animations hold it
// as Days does (KH2's grip is turned against Days': b_ro_D002's keyblade, pointed ahead, pointed 45 degrees up);
// KH2 motions as KH2's hand does; a fade between them turns from one to the other
inline Mat attached_frame(const Mat &at, const Mat &game, float share)
{
    if (!(share > 0.001F))
        return at;
    const auto turn_of_frame = [](const Mat &m, std::array<float, 3> &scale) {
        Mat r = Mat{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        for (int c = 0; c < 3; ++c) {
            const auto i = static_cast<std::size_t>(c * 4);
            scale[static_cast<std::size_t>(c)] = std::sqrt(m[i] * m[i] + m[i + 1] * m[i + 1] + m[i + 2] * m[i + 2]);
            const float l =
                scale[static_cast<std::size_t>(c)] > 1e-12F ? 1.0F / scale[static_cast<std::size_t>(c)] : 0.0F;
            r[i] = m[i] * l;
            r[i + 1] = m[i + 1] * l;
            r[i + 2] = m[i + 2] * l;
        }
        return r;
    };
    std::array<float, 3> sa{}, sg{};
    const Mat ra = turn_of_frame(at, sa), rg = turn_of_frame(game, sg);
    Mat out = rg;
    std::array<float, 3> scale = sg;
    if (share < 0.999F) { // between the two turns (quaternions, the shorter way), the scales between theirs
        const auto quat = [](const Mat &r) {
            std::array<float, 4> q{}; // x y z w
            const float tr = r[0] + r[5] + r[10];
            if (tr > 0.0F) {
                const float k = std::sqrt(tr + 1.0F) * 2.0F;
                q = {(r[6] - r[9]) / k, (r[8] - r[2]) / k, (r[1] - r[4]) / k, 0.25F * k};
            } else if (r[0] > r[5] && r[0] > r[10]) {
                const float k = std::sqrt(1.0F + r[0] - r[5] - r[10]) * 2.0F;
                q = {0.25F * k, (r[4] + r[1]) / k, (r[8] + r[2]) / k, (r[6] - r[9]) / k};
            } else if (r[5] > r[10]) {
                const float k = std::sqrt(1.0F + r[5] - r[0] - r[10]) * 2.0F;
                q = {(r[4] + r[1]) / k, 0.25F * k, (r[9] + r[6]) / k, (r[8] - r[2]) / k};
            } else {
                const float k = std::sqrt(1.0F + r[10] - r[0] - r[5]) * 2.0F;
                q = {(r[8] + r[2]) / k, (r[9] + r[6]) / k, 0.25F * k, (r[1] - r[4]) / k};
            }
            return q;
        };
        const std::array<float, 4> qa = quat(ra);
        std::array<float, 4> qg = quat(rg);
        float d = qa[0] * qg[0] + qa[1] * qg[1] + qa[2] * qg[2] + qa[3] * qg[3];
        if (d < 0.0F) {
            for (float &v : qg)
                v = -v;
            d = -d;
        }
        const float angle = std::acos(std::fmin(1.0F, d));
        const float wa = angle > 1e-4F ? std::sin((1.0F - share) * angle) / std::sin(angle) : 1.0F - share;
        const float wg = angle > 1e-4F ? std::sin(share * angle) / std::sin(angle) : share;
        const float x = qa[0] * wa + qg[0] * wg, y = qa[1] * wa + qg[1] * wg, z = qa[2] * wa + qg[2] * wg,
                    w = qa[3] * wa + qg[3] * wg;
        out = {1 - 2 * (y * y + z * z),
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
               0,
               0,
               0,
               1};
        for (std::size_t c = 0; c < 3; ++c)
            scale[c] = sa[c] + (sg[c] - sa[c]) * share;
    }
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            out[static_cast<std::size_t>(c * 4 + r)] *= scale[static_cast<std::size_t>(c)];
    out[12] = at[12];
    out[13] = at[13];
    out[14] = at[14];
    return out;
}

} // namespace khdays::models

#endif
