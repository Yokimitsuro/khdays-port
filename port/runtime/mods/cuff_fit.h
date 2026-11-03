// A mod's KH2 sleeve cuff fitted round its hand (models.cpp, bones.txt's `cuff`; docs/KH2_MODELS.md, "Cuffs"): the cuff's
// rest surface baked by distance along its axis and angle round it, what each flap carries of it, and how far each flap
// opens and shortens so the hand's points stay inside. Nothing collides: the fit is a function of the pose, and its
// springs cannot overshoot, so nothing can fight or shake. Pure.
#ifndef KHDAYS_CUFF_FIT_H
#define KHDAYS_CUFF_FIT_H

#include "hand_pose.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace khdays::models {

struct CuffFit {
    static constexpr int kStations = 8, kBins = 32, kFlaps = 4;
    using Shares = std::array<float, kFlaps>;

    struct Triangle {
        std::array<HandVec, 3> at;
        std::array<int, 3> flap; // each corner's flap, -1 the root's ring, which does not open
    };
    struct Slot {
        float reach = 0.0F;
        Shares share{}; // how far out the surface here goes per radian each flap opens: its corners' levers, blended
    };
    struct Settings {
        float clearance = 0.004F; // how far inside the surface the hand's points stay
        float most_open = 0.6F;   // radians
        float shortest = 0.85F;   // of a flap's length
        float neighbours = 0.35F; // of a flap's opening, each flap beside it opens at least
        float band_from = 0.82F;  // of the cuff's length: past here a point is cleared by shortening, not opening
        float band_to = 0.9F;
        float most_shift = 0.015F;      // how far the whole cuff slides across its axis (KH2's own moves up to 2-3 cm)
        float neighbours_length = 0.0F; // of a flap's shortening, each flap beside it takes
        // one flap on its own (`cuff_thumb`: the thumb's tab), none of it copied by its neighbours: more clearance from
        // what it carries, its own most open (0: as the others), and an opening it always has on top (radians)
        int own_flap = -1;
        float own_clearance = 0.0F, own_most_open = 0.0F, own_always = 0.0F;
    };
    struct Target {
        Shares open{};
        Shares length{1.0F, 1.0F, 1.0F, 1.0F};
        HandVec shift{}; // the whole cuff, across its axis
    };

    HandVec center{}, axis{1.0F, 0.0F, 0.0F}, u{0.0F, 1.0F, 0.0F}, v{0.0F, 0.0F, 1.0F};
    float from = 0.0F, to = 0.0F;
    std::array<HandVec, kFlaps> hinge{}, tip{}, along{}, out{}, turn_axis{};
    Shares hinge_x{};
    std::array<Slot, kStations * kBins> slots{};
    bool ready = false;

    // `hinges` and `tips`: each flap's root and far end at rest
    void bake(const HandVec &root, const std::array<HandVec, kFlaps> &hinges, const std::array<HandVec, kFlaps> &tips,
              const std::vector<Triangle> &triangles)
    {
        center = root;
        hinge = hinges;
        tip = tips;
        HandVec sum{};
        for (int k = 0; k < kFlaps; ++k)
            sum = hand_add(sum, hand_unit(hand_sub(tips[idx(k)], hinges[idx(k)])));
        axis = hand_unit(sum, {1.0F, 0.0F, 0.0F});
        u = hand_unit(
            hand_cross(axis, std::fabs(axis[1]) < 0.9F ? HandVec{0.0F, 1.0F, 0.0F} : HandVec{0.0F, 0.0F, 1.0F}));
        v = hand_cross(axis, u);
        ready = !triangles.empty();
        if (!ready)
            return;
        for (int k = 0; k < kFlaps; ++k) {
            along[idx(k)] = hand_unit(hand_sub(tips[idx(k)], hinges[idx(k)]));
            hinge_x[idx(k)] = hand_dot(hand_sub(hinges[idx(k)], center), axis);
            const HandVec d = hand_sub(hinges[idx(k)], center);
            const HandVec radial = hand_unit(hand_add(d, axis, -hand_dot(d, axis)), u);
            // out: square to the flap, on its radial side; the turn about the axis that takes `along` toward it
            out[idx(k)] = hand_unit(hand_add(radial, along[idx(k)], -hand_dot(radial, along[idx(k)])), radial);
            turn_axis[idx(k)] = hand_cross(along[idx(k)], out[idx(k)]);
        }
        from = 1e30F;
        to = -1e30F;
        for (const Triangle &t : triangles)
            for (const HandVec &p : t.at) {
                const float x = hand_dot(hand_sub(p, center), axis);
                from = std::min(from, x);
                to = std::max(to, x);
            }
        to = std::max(to, from + 1e-4F);
        for (int s = 0; s < kStations; ++s) {
            const float x = from + (static_cast<float>(s) + 0.5F) * (to - from) / static_cast<float>(kStations);
            const HandVec start = hand_add(center, axis, x);
            for (int b = 0; b < kBins; ++b) {
                const float angle =
                    (static_cast<float>(b) + 0.5F) / static_cast<float>(kBins) * 6.2831853F - 3.14159265F;
                const HandVec way = hand_add(hand_add({0.0F, 0.0F, 0.0F}, u, std::cos(angle)), v, std::sin(angle));
                Slot &slot = slots[index(s, b)];
                slot = Slot{};
                for (const Triangle &t : triangles)
                    cast(start, way, t, slot);
            }
        }
    }

    // A point (in the rest frame) along the axis, out from it, and the surface there
    struct Reading {
        float x = 0.0F, r = 0.0F, reach = 0.0F;
        float covered = 0.0F; // how much of here the surface is: its tabs have gaps between them
        Shares share{};
        bool within = false;
    };
    Reading read(const HandVec &p) const
    {
        Reading out_of;
        const HandVec d = hand_sub(p, center);
        out_of.x = hand_dot(d, axis);
        const float a = hand_dot(d, u), b = hand_dot(d, v);
        out_of.r = std::sqrt(a * a + b * b);
        out_of.within = out_of.x >= from && out_of.x <= to;
        if (!out_of.within)
            return out_of;
        const float ps = std::clamp((out_of.x - from) / (to - from) * static_cast<float>(kStations) - 0.5F, 0.0F,
                                    static_cast<float>(kStations - 1));
        const int s0 = static_cast<int>(ps), s1 = std::min(s0 + 1, kStations - 1);
        const float fs = ps - static_cast<float>(s0);
        float pb = (std::atan2(b, a) + 3.14159265F) / 6.2831853F * static_cast<float>(kBins) - 0.5F;
        if (pb < 0.0F)
            pb += static_cast<float>(kBins);
        const int b0 = std::min(static_cast<int>(pb), kBins - 1), b1 = (b0 + 1) % kBins;
        const float fb = pb - static_cast<float>(b0);
        const Slot &aa = slots[index(s0, b0)], &ab = slots[index(s0, b1)], &ba = slots[index(s1, b0)],
                   &bb = slots[index(s1, b1)];
        const float wa = (1.0F - fs) * (1.0F - fb), wb = (1.0F - fs) * fb, wc = fs * (1.0F - fb), wd = fs * fb;
        // only what is really there: a slot in a gap counts for nothing
        const std::array<const Slot *, 4> near = {&aa, &ab, &ba, &bb};
        const std::array<float, 4> weight = {wa, wb, wc, wd};
        for (std::size_t n = 0; n < 4; ++n) {
            if (near[n]->reach <= 0.0F)
                continue;
            out_of.covered += weight[n];
            out_of.reach += near[n]->reach * weight[n];
            for (std::size_t k = 0; k < kFlaps; ++k)
                out_of.share[k] += near[n]->share[k] * weight[n];
        }
        if (out_of.covered > 0.0F) {
            out_of.reach /= out_of.covered;
            for (std::size_t k = 0; k < kFlaps; ++k)
                out_of.share[k] /= out_of.covered;
        }
        return out_of;
    }

    // The surface's reach at a reading, the flaps opened by `open`
    float opened_reach(const Reading &at, const Shares &open) const
    {
        float reach = at.reach;
        for (std::size_t k = 0; k < kFlaps; ++k)
            reach += at.share[k] * open[k];
        return reach;
    }

    // How far each flap opens and shortens for these points of the hand (rest frame)
    Target fit(const std::vector<HandVec> &points, const Settings &settings) const
    {
        Target target;
        if (!ready)
            return target;
        const float length = to - from, band_from = from + length * settings.band_from,
                    band_to = from + length * settings.band_to;
        // first the whole cuff slides: near the wrist a flap has no leverage, so a small poke would need a huge angle
        for (int pass = 0; pass < 3; ++pass)
            for (const HandVec &p : points) {
                const Reading r = read(hand_sub(p, target.shift));
                if (!r.within || r.covered <= 0.25F || r.x >= band_from)
                    continue;
                const float need = (r.r + settings.clearance - r.reach) * hand_share(r.covered, 0.25F, 0.75F);
                if (need <= 0.0F || r.r < 1e-4F)
                    continue;
                const HandVec d = hand_sub(hand_sub(p, target.shift), center);
                const HandVec across = hand_add(d, axis, -hand_dot(d, axis));
                target.shift = hand_add(target.shift, across, 0.5F * need / r.r);
                const float l = hand_length(target.shift);
                if (l > settings.most_shift)
                    target.shift = hand_add({0.0F, 0.0F, 0.0F}, target.shift, settings.most_shift / l);
            }
        readings_.clear();
        for (const HandVec &p : points) {
            const Reading r = read(hand_sub(p, target.shift));
            if (r.within && r.covered > 0.25F)
                readings_.push_back(r);
        }
        for (int pass = 0; pass < 2; ++pass)
            for (const Reading &r : readings_) {
                const float need =
                    (r.r + settings.clearance - opened_reach(r, target.open)) * hand_share(r.covered, 0.25F, 0.75F);
                if (need <= 0.0F)
                    continue;
                // near the far end a point is cleared by the flap drawing back past it, not by opening
                const float by_length = hand_share(r.x, band_from, band_to);
                const float pull = std::min(1.0F, need / 0.01F);
                const float wanted = std::max(settings.shortest, (r.x - 0.003F - from) / length);
                for (std::size_t k = 0; k < kFlaps; ++k)
                    target.length[k] =
                        std::min(target.length[k],
                                 1.0F - by_length * pull *
                                            std::min(1.0F, r.share[k] / std::max(r.x - hinge_x[k], 0.001F) * 2.0F) *
                                            (1.0F - wanted));
                const float wants = need * (1.0F - by_length);
                float sum = 0.0F;
                const Shares &give = r.share;
                for (std::size_t k = 0; k < kFlaps; ++k)
                    sum += give[k] * give[k];
                if (sum < 1e-6F)
                    continue;
                for (std::size_t k = 0; k < kFlaps; ++k)
                    target.open[k] = std::min(settings.most_open, target.open[k] + wants * give[k] / sum);
            }
        // the ring stretches: each flap comes along with its neighbours (flaps are in ring order)
        const Shares own = target.open, own_length = target.length;
        for (std::size_t k = 0; k < kFlaps; ++k)
            target.length[k] =
                std::min(own_length[k],
                         1.0F - settings.neighbours_length *
                                    (1.0F - std::min(own_length[(k + 1) % kFlaps], own_length[(k + 3) % kFlaps])));
        for (std::size_t k = 0; k < kFlaps; ++k)
            target.open[k] =
                std::max(own[k], settings.neighbours * std::max(own[(k + 1) % kFlaps], own[(k + 3) % kFlaps]));
        if (settings.own_flap >= 0 && settings.own_flap < kFlaps) {
            const auto t = static_cast<std::size_t>(settings.own_flap);
            const float most = settings.own_most_open > 0.0F ? settings.own_most_open : settings.most_open;
            // after the neighbours took theirs: this flap alone, by how much of each point's surface it carries
            if (settings.own_clearance > 0.0F || most > settings.most_open)
                for (const Reading &r : readings_) {
                    float levers = 0.0F;
                    for (std::size_t k = 0; k < kFlaps; ++k)
                        levers += r.share[k];
                    if (r.share[t] < 1e-4F)
                        continue;
                    const float carried = r.share[t] / levers;
                    const float need =
                        (r.r + settings.clearance + settings.own_clearance * carried - opened_reach(r, target.open)) *
                        hand_share(r.covered, 0.25F, 0.75F) * (1.0F - hand_share(r.x, band_from, band_to));
                    if (need > 0.0F)
                        target.open[t] = std::min(most, target.open[t] + need * carried / r.share[t]);
                }
            target.open[t] = std::min(target.open[t], most) + settings.own_always;
        }
        return target;
    }

    // The outermost of these triangles out from the axis at `x`, at that angle (0 none): what is really drawn there
    float surface(float x, const HandVec &way, const std::vector<Triangle> &triangles) const
    {
        Slot slot;
        const HandVec start = hand_add(center, axis, x);
        for (const Triangle &t : triangles)
            cast(start, way, t, slot);
        return slot.reach;
    }

private:
    mutable std::vector<Reading> readings_;

    static std::size_t idx(int k)
    {
        return static_cast<std::size_t>(k);
    }
    static std::size_t index(int s, int b)
    {
        return static_cast<std::size_t>(s * kBins + b);
    }
    // The ray from the axis against one triangle: the outermost hit is the surface, its flaps by the hit's corners
    void cast(const HandVec &start, const HandVec &way, const Triangle &t, Slot &slot) const
    {
        const HandVec e1 = hand_sub(t.at[1], t.at[0]), e2 = hand_sub(t.at[2], t.at[0]);
        const HandVec p = hand_cross(way, e2);
        const float det = hand_dot(e1, p);
        if (std::fabs(det) < 1e-10F)
            return;
        const float inv = 1.0F / det;
        const HandVec s = hand_sub(start, t.at[0]);
        const float a = hand_dot(s, p) * inv;
        if (a < 0.0F || a > 1.0F)
            return;
        const HandVec q = hand_cross(s, e1);
        const float b = hand_dot(way, q) * inv;
        if (b < 0.0F || a + b > 1.0F)
            return;
        const float r = hand_dot(e2, q) * inv;
        if (r <= slot.reach)
            return;
        slot.reach = r;
        slot.share = {};
        const std::array<float, 3> corners = {1.0F - a - b, a, b};
        for (std::size_t c = 0; c < 3; ++c)
            if (t.flap[c] >= 0) {
                const auto k = static_cast<std::size_t>(t.flap[c]);
                slot.share[k] += corners[c] * std::max(0.0F, hand_dot(hand_sub(t.at[c], center), axis) - hinge_x[k]);
            }
    }
};

// A cuff's state from draw to draw: the fit eased, each flap's sway round it
struct CuffMotion {
    CuffFit::Shares open{}, open_speed{}, length{1.0F, 1.0F, 1.0F, 1.0F}, length_speed{};
    CuffFit::Shares sway{}, sway_speed{}, sway_late{};
    HandVec shift{}, shift_speed{};
    HandVec at{}, moving{};
};

// `push`: how the wrist's acceleration throws each flap (its share along the flap's way out, m/s^2); `droop`: where its
// weight would hang it, radians. The fit by a critically damped spring; the sway on top by a soft one, held near it.
inline void cuff_motion_step(CuffMotion &motion, const CuffFit::Target &target, const CuffFit::Shares &push,
                             const CuffFit::Shares &droop, float seconds, bool fresh)
{
    for (std::size_t c = 0; c < 3; ++c) {
        if (fresh) {
            motion.shift[c] = target.shift[c];
            motion.shift_speed[c] = 0.0F;
        } else if (seconds > 0.0F) {
            hand_spring(motion.shift[c], motion.shift_speed[c], target.shift[c], 30.0F, seconds);
        }
    }
    for (std::size_t k = 0; k < CuffFit::kFlaps; ++k) {
        if (fresh) {
            motion.open[k] = target.open[k];
            motion.length[k] = target.length[k];
            motion.open_speed[k] = motion.length_speed[k] = 0.0F;
            motion.sway[k] = motion.sway_late[k] = droop[k];
            motion.sway_speed[k] = 0.0F;
            continue;
        }
        if (seconds <= 0.0F)
            continue;
        hand_spring(motion.open[k], motion.open_speed[k], target.open[k], 30.0F, seconds);
        hand_spring(motion.length[k], motion.length_speed[k], target.length[k], 30.0F, seconds);
        const float rate = 20.0F, damping = 0.5F;
        motion.sway_speed[k] += (rate * rate * (droop[k] - motion.sway[k]) -
                                 2.0F * damping * rate * motion.sway_speed[k] - push[k] * 6.0F) *
                                seconds;
        motion.sway[k] = std::clamp(motion.sway[k] + motion.sway_speed[k] * seconds, -0.05F, 0.22F);
        if (motion.sway[k] <= -0.05F || motion.sway[k] >= 0.22F)
            motion.sway_speed[k] = 0.0F;
        motion.sway_late[k] += (motion.sway[k] - motion.sway_late[k]) * (1.0F - std::exp(-seconds / 0.09F));
    }
}

} // namespace khdays::models

#endif
