// The XPBD solver (solver.h). Per substep: velocities integrated (gravity, wind, damping and inertia
// relative to each particle's target), positions predicted, then for each iteration the links, the
// pull toward the targets, the limits and last the collisions (model, room, self), so contact wins over
// shape; velocities from the positions. Multipliers live for one substep (Macklin et al., "XPBD").

#include "solver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>

namespace khdays::secondary {

namespace {

using Clock = std::chrono::steady_clock;

double us(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::micro>(b - a).count();
}

constexpr float kTwoPi = 2.0F * kPi;
// a particle further than this from its target is lost: its group starts again on its targets
constexpr float kLostDistance = 4.0F;
// a shallow penetration of the room's back side (stationary or initial overlap) is pushed out to
// the front; deeper ones are left (a wall's other face)
constexpr float kShallowBehind = 0.05F;
// ... and one whose target is in front of the face, up to this deep
constexpr float kDeepBehind = 0.3F;

// q's barycentric coordinates in triangle abc
std::array<float, 3> barycentric(Vec3 q, Vec3 a, Vec3 b, Vec3 c)
{
    const Vec3 v0 = b - a, v1 = c - a, v2 = q - a;
    const float d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    if (std::fabs(den) < 1e-20F)
        return {1.0F, 0.0F, 0.0F};
    const float v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den;
    return {1.0F - v - w, v, w};
}

// the closest point of a (tapered) capsule's core to p: its parameter, point and radius there
void core_point(const Simulation::Shape &s, Vec3 p, float &t, Vec3 &q, float &r)
{
    const Vec3 ab = s.b - s.a;
    const float ll = dot(ab, ab);
    t = ll > 1e-12F ? minf(1.0F, maxf(0.0F, dot(p - s.a, ab) / ll)) : 0.0F;
    q = s.a + ab * t;
    r = s.ra + (s.rb - s.ra) * t;
}

// an edge or face contact moves none of its corners further than this many times the overlap
constexpr float kMaxLever = 2.0F;
// a face a body pierces is carried out at most this far (m) in one contact (the velocity clamp keeps it
// from flying on)
constexpr float kMaxPierce = 0.5F;
// a face's friction is drawn from at most this much of its push (m): a face held hard against a leg (by
// a wall, say) can still slip out
constexpr float kFaceFrictionReach = 0.01F;
constexpr float kDrapeEase = 0.06F;  // s: the drape's swing of a target eased over this (ease_drape)
constexpr float kSettleKeep = 0.75F; // settle: of each control's velocity kept from tick to tick
// passes of strain then contact closing each substep (a push spread through the coat)
constexpr int kSpreadPasses = 2;
} // namespace

Simulation::Simulation(const Rig &rig) : rig_(rig)
{
    const std::size_t n = rig.particles.size();
    drive_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const ParamBlock &b = rig.params[static_cast<std::size_t>(rig.particles[i].params)];
        drive_[i].params = b;
        drive_[i].guide = b.mode == Mode::simulated ? 0.0F : 1.0F;
        drive_[i].authority = b.mode == Mode::authored ? 1.0F : 0.0F;
    }
    wind_.assign(rig.groups.size(), {});
    x_.assign(n, {});
    prev_.assign(n, {});
    v_.assign(n, {});
    target_.assign(n, {});
    target_prev_.assign(n, {});
    target_vel_.assign(n, {});
    lambda_follow_.assign(n, 0.0F);
    pinned_.assign(n, 0);
    lambda_.assign(rig.distances.size(), 0.0F);
    compliance_.assign(rig.distances.size(), 0.0F);
    pushed_.assign(n, {});
    rest_now_.resize(rig.distances.size());
    for (std::size_t c = 0; c < rig.distances.size(); ++c)
        rest_now_[c] = rig.distances[c].rest;
    tether_now_.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        tether_now_[i] = rig.particles[i].tether;
    sub0_.assign(rig.skeleton.names.size(), {});
    sub1_.assign(rig.skeleton.names.size(), {});
    shape0_.assign(rig.colliders.size(), {});
    shape1_.assign(rig.colliders.size(), {});
    env_.reserve(512);
    env_boxes_.reserve(512);
    bone_out_.assign(rig.bones_out.size(), {});
    bone_done_.assign(rig.bones_out.size(), 0);
    face_normal_prev_.assign(rig.faces.size(), {});
    face_box_.assign(rig.faces.size(), {});
    for (const bool rest : {false, true})
        for (std::size_t c = 0; c < rig.colliders.size(); ++c)
            if (rig.colliders[c].rest_side == rest)
                collider_order_.push_back(static_cast<int>(c));
    // each collider's candidates, once
    collider_points_.assign(rig.colliders.size(), {});
    collider_edges_.assign(rig.colliders.size(), {});
    collider_faces_.assign(rig.colliders.size(), {});
    for (std::size_t c = 0; c < rig.colliders.size(); ++c) {
        const std::uint32_t tags = rig.colliders[c].tags;
        const auto out = [&](std::size_t i) {
            return std::binary_search(rig.collider_excluded.begin(), rig.collider_excluded.end(),
                                      (static_cast<std::uint64_t>(c) << 32) | i);
        };
        for (std::size_t i = 0; i < n; ++i)
            if (rig.particles[i].dynamic && (rig.particles[i].collide & tags) && !out(i))
                collider_points_[c].push_back(static_cast<int>(i));
        for (std::size_t e = 0; e < rig.edges.size(); ++e) {
            const auto a = static_cast<std::size_t>(rig.edges[e][0]), b = static_cast<std::size_t>(rig.edges[e][1]);
            const std::uint32_t mask = rig.particles[a].dynamic ? rig.particles[a].collide : rig.particles[b].collide;
            if ((mask & tags) && !out(a) && !out(b))
                collider_edges_[c].push_back(static_cast<int>(e));
        }
        for (std::size_t f = 0; f < rig.faces.size(); ++f) {
            const Face &face = rig.faces[f];
            const auto a = static_cast<std::size_t>(face.a), b = static_cast<std::size_t>(face.b),
                       cc = static_cast<std::size_t>(face.c);
            const bool across = std::binary_search(rig.face_excluded.begin(), rig.face_excluded.end(),
                                                   (static_cast<std::uint64_t>(c) << 32) | f);
            if ((rig.particles[a].collide & tags) && !out(a) && !out(b) && !out(cc) && !across)
                collider_faces_[c].push_back(static_cast<int>(f));
        }
    }
    // outward: a garment is a shell around the body, so a control's or a face's way out is away from its
    // group's reference bone's vertical axis at rest (kept in that bone's space); body colliders (rest
    // side) push there whichever part of the body meets it
    {
        std::vector<Frame> rest(rig.skeleton.rest.size());
        for (std::size_t b = 0; b < rest.size(); ++b)
            rest[b] = frame_of(rig.skeleton.rest[b]);
        const auto out_of = [&](std::size_t group, Vec3 at) {
            const Frame &f = rest[static_cast<std::size_t>(rig.groups[group].reference_bone)];
            Vec3 d = at - f.t;
            d.y = 0.0F;
            return normalized(d, {0.0F, 0.0F, 1.0F});
        };
        std::vector<Vec3> at(n);
        outward_.assign(n, {});
        for (std::size_t i = 0; i < n; ++i) {
            const Particle &p = rig.particles[i];
            if (!p.dynamic)
                continue;
            const auto g = static_cast<std::size_t>(p.group);
            const Frame &f = rest[static_cast<std::size_t>(rig.groups[g].reference_bone)];
            at[i] = apply(f, p.reference);
            outward_[i] = rotate(conjugate(f.q), out_of(g, at[i]));
        }
        face_out_.assign(rig.faces.size(), 1);
        for (std::size_t fi = 0; fi < rig.faces.size(); ++fi) {
            const Face &f = rig.faces[fi];
            const Vec3 a = at[static_cast<std::size_t>(f.a)], b = at[static_cast<std::size_t>(f.b)],
                       c = at[static_cast<std::size_t>(f.c)];
            const Vec3 out = out_of(static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(f.a)].group),
                                    (a + b + c) * (1.0F / 3.0F));
            face_out_[fi] = dot(cross(b - a, c - a), out) >= 0.0F ? std::int8_t{1} : std::int8_t{-1};
        }
    }
    // the drape: each particle's colliders, the particles anchors first
    particle_colliders_.assign(n, {});
    for (std::size_t c = 0; c < collider_points_.size(); ++c)
        for (std::size_t li = 0; li < collider_points_[c].size(); ++li)
            particle_colliders_[static_cast<std::size_t>(collider_points_[c][li])].push_back(
                {static_cast<int>(c), static_cast<int>(li)});
    {
        std::vector<int> hops(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            int at = static_cast<int>(i), count = 0;
            while (at >= 0 && rig.particles[static_cast<std::size_t>(at)].dynamic && count < 1000) {
                at = rig.particles[static_cast<std::size_t>(at)].anchor;
                ++count;
            }
            hops[i] = count;
        }
        for (std::size_t i = 0; i < n; ++i)
            if (rig.particles[i].dynamic)
                drape_order_.push_back(static_cast<int>(i));
        std::stable_sort(drape_order_.begin(), drape_order_.end(), [&](int a, int b) {
            return hops[static_cast<std::size_t>(a)] < hops[static_cast<std::size_t>(b)];
        });
    }
    raw_.assign(n, {});
    draped_.assign(n, {});
    // rest positions (model space) for the self-collision's choices
    rest_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Particle &p = rig.particles[i];
        rest_[i] = apply(frame_of(rig.skeleton.rest[static_cast<std::size_t>(p.bone)]), p.offset);
    }
    const std::size_t words = (rig.faces.size() + 63) / 64;
    for (std::size_t i = 0; i < n; ++i) {
        const Particle &p = rig.particles[i];
        if (!p.dynamic || p.self_group < 0)
            continue;
        std::vector<std::uint64_t> ok(words, 0);
        bool any = false;
        for (std::size_t f = 0; f < rig.faces.size(); ++f) {
            const Face &face = rig.faces[f];
            const int v[3] = {face.a, face.b, face.c};
            if (rig.particles[static_cast<std::size_t>(face.a)].self_group != p.self_group)
                continue;
            bool skip = false;
            for (const int k : v) {
                if (k == static_cast<int>(i) || std::binary_search(rig.self_excluded.begin(), rig.self_excluded.end(),
                                                                   pair_key(static_cast<int>(i), k)))
                    skip = true;
            }
            if (skip)
                continue;
            const Vec3 q =
                closest_on_triangle(rest_[i], rest_[static_cast<std::size_t>(face.a)],
                                    rest_[static_cast<std::size_t>(face.b)], rest_[static_cast<std::size_t>(face.c)]);
            const float th = p.thickness + rig.particles[static_cast<std::size_t>(face.a)].thickness;
            if (length(rest_[i] - q) < th) // close at rest (a slit, an overlapping layer): not forced apart
                continue;
            ok[f / 64] |= 1ULL << (f % 64);
            any = true;
        }
        if (any) {
            self_points_.push_back(static_cast<int>(i));
            self_faces_ok_.push_back(std::move(ok));
        }
    }
}

float Simulation::w(std::size_t i) const
{
    return rig_.particles[i].dynamic && !pinned_[i] ? rig_.particles[i].inv_mass : 0.0F;
}

void Simulation::frames_at(const std::vector<Frame> &from, const std::vector<Frame> &to, float u,
                           std::vector<Frame> &out) const
{
    for (const int b : rig_.needed)
        out[static_cast<std::size_t>(b)] =
            interpolate(from[static_cast<std::size_t>(b)], to[static_cast<std::size_t>(b)], u);
}

Vec3 Simulation::animated_of(std::size_t i, const std::vector<Frame> &pose) const
{
    const Particle &p = rig_.particles[i];
    return apply(pose[static_cast<std::size_t>(p.bone)], p.offset);
}

// A blended reference's weights at `pose`: a ridden bone (a thigh) carries the control only as far as it
// has come up into it, by how much closer the bone is to the control's hanging place than it was at rest
// (none standing; all its weight once it reaches the hanging place); the reference bone keeps the rest.
std::array<float, 4> Simulation::blend_weights(const ReferenceBlend &b, const std::vector<Frame> &pose,
                                               float scale) const
{
    std::array<float, 4> w{};
    const Vec3 hang = apply(pose[static_cast<std::size_t>(b.bone[0])], b.point[0]);
    float total = 0.0F;
    for (int k = 1; k < b.count; ++k) {
        const auto j = static_cast<std::size_t>(k);
        const Frame &f = pose[static_cast<std::size_t>(b.bone[j])];
        const Vec3 head = f.t, end = apply(f, b.end[j]), ab = end - head;
        const float ll = dot(ab, ab);
        const float t = ll > 0.0F ? clampf(dot(hang - head, ab) / ll, 0.0F, 1.0F) : 0.0F;
        const float gap = length(hang - (head + ab * t));
        const float u = clampf((b.gap[j] - gap) / maxf(0.03F, b.gap[j]), 0.0F, 1.0F);
        w[j] = maxf(b.always[j], b.weight[j] * u * u * (3.0F - 2.0F * u)) * scale;
        total += w[j];
    }
    if (total > 0.95F)
        for (int k = 1; k < b.count; ++k)
            w[static_cast<std::size_t>(k)] *= 0.95F / total;
    total = 0.0F;
    for (int k = 1; k < b.count; ++k)
        total += w[static_cast<std::size_t>(k)];
    w[0] = 1.0F - total;
    return w;
}

Vec3 Simulation::reference_point(std::size_t i, const std::vector<Frame> &pose) const
{
    const Particle &p = rig_.particles[i];
    if (p.blend < 0)
        return apply(pose[static_cast<std::size_t>(rig_.groups[static_cast<std::size_t>(p.group)].reference_bone)],
                     p.reference);
    const ReferenceBlend &b = rig_.blends[static_cast<std::size_t>(p.blend)];
    const auto w = blend_weights(b, pose, drive_[i].params.get(Param::follow_bone_scale));
    Vec3 sum;
    for (int k = 0; k < b.count; ++k) {
        const auto j = static_cast<std::size_t>(k);
        sum += apply(pose[static_cast<std::size_t>(b.bone[j])], b.point[j]) * w[j];
    }
    return sum;
}

Frame Simulation::reference_frame(int si, const std::vector<Frame> &pose) const
{
    const SimBone &sb = rig_.bones_out[static_cast<std::size_t>(si)];
    const Particle &p = rig_.particles[static_cast<std::size_t>(sb.particle)];
    if (p.blend < 0)
        return compose(pose[static_cast<std::size_t>(rig_.groups[static_cast<std::size_t>(sb.group)].reference_bone)],
                       sb.reference);
    const ReferenceBlend &b = rig_.blends[static_cast<std::size_t>(p.blend)];
    const auto w =
        blend_weights(b, pose, drive_[static_cast<std::size_t>(sb.particle)].params.get(Param::follow_bone_scale));
    Frame f = compose(pose[static_cast<std::size_t>(b.bone[0])], b.frame[0]);
    float total = w[0];
    for (int k = 1; k < b.count; ++k) { // a running weighted average of the frames
        const auto j = static_cast<std::size_t>(k);
        total += w[j];
        if (total > 0.0F && w[j] > 0.0F)
            f = interpolate(f, compose(pose[static_cast<std::size_t>(b.bone[j])], b.frame[j]), w[j] / total);
    }
    return f;
}

Vec3 Simulation::target_of(std::size_t i, const std::vector<Frame> &pose) const
{
    const ParticleDrive &d = drive_[i];
    const float g = maxf(d.guide, d.authority);
    const Vec3 anim = animated_of(i, pose);
    const Vec3 own = g >= 1.0F ? anim : lerp(reference_point(i, pose), anim, g);
    const float gs = game_shape_of(i);
    if (gs <= 0.0F)
        return own;
    return lerp(own, game_now_[i], gs); // the game model's own surface there (its coat, skinned to its legs)
}

float Simulation::game_shape_of(std::size_t i) const
{
    return i < game_now_.size() && rig_.particles[i].dynamic ? drive_[i].params.get(Param::game_shape) * game_weight_[i]
                                                             : 0.0F;
}

void Simulation::set_game_targets(const std::vector<Vec3> &from, const std::vector<Vec3> &to,
                                  const std::vector<float> &weight)
{
    game_from_ = from;
    game_to_ = to;
    game_now_ = to;
    game_weight_ = weight;
}

void Simulation::shapes(const std::vector<Frame> &pose, std::vector<Shape> &out) const
{
    for (std::size_t c = 0; c < rig_.colliders.size(); ++c) {
        const Collider &k = rig_.colliders[c];
        const Frame &f = pose[static_cast<std::size_t>(k.bone)];
        Shape s;
        s.a = apply(f, k.offset);
        s.b = k.kind == ShapeKind::capsule ? apply(pose[static_cast<std::size_t>(k.end_bone)], k.end_offset) : s.a;
        const float scale = (std::fabs(f.s.x) + std::fabs(f.s.y) + std::fabs(f.s.z)) / 3.0F;
        s.ra = k.radius * scale;
        s.rb = k.end_radius * scale;
        out[c] = s;
    }
}

const Frame &Simulation::reference_bone_frame(std::size_t i, const std::vector<Frame> &pose) const
{
    return pose[static_cast<std::size_t>(
        rig_.groups[static_cast<std::size_t>(rig_.particles[i].group)].reference_bone)];
}

float Simulation::point_clearance(std::size_t c, std::size_t i, const Shape &s) const
{
    const float k = rig_.colliders[c].radius;
    if (rig_.point_clearance.empty() || k <= 0.0F)
        return 0.0F;
    return rig_.point_clearance[c * rig_.particles.size() + i] * (s.ra / k);
}

float Simulation::face_clearance(std::size_t c, std::size_t f, const Shape &s) const
{
    const float k = rig_.colliders[c].radius;
    if (rig_.face_clearance.empty() || k <= 0.0F)
        return 0.0F;
    return rig_.face_clearance[c * rig_.faces.size() + f] * (s.ra / k);
}

Frame Simulation::bone_frame(int si, const std::vector<Frame> &pose, bool twist)
{
    const auto index = static_cast<std::size_t>(si);
    if (bone_done_[index])
        return bone_out_[index];
    const SimBone &sb = rig_.bones_out[index];
    const auto pi = static_cast<std::size_t>(sb.particle);
    const Particle &p = rig_.particles[pi];
    const ParticleDrive &d = drive_[pi];
    const Frame &anim = pose[static_cast<std::size_t>(sb.bone)];
    if (d.authority >= 0.999F && d.params.policy == AuthoredPolicy::exact) {
        bone_done_[index] = 1;
        return bone_out_[index] = anim; // the animation's matrix as it is, scale and all
    }
    const GroupRig &g = rig_.groups[static_cast<std::size_t>(sb.group)];
    // its reference: the prepared shape on the group's reference bone(s), or the animation
    Frame ref = reference_frame(si, pose);
    ref.s = anim.s;
    ref = interpolate(ref, anim, maxf(d.guide, d.authority));
    Frame base = ref;
    if (sb.parent_sim >= 0) { // carried by its simulated parent as the reference carries it
        const SimBone &parent = rig_.bones_out[static_cast<std::size_t>(sb.parent_sim)];
        const Frame parent_new = bone_frame(sb.parent_sim, pose, twist);
        const ParticleDrive &pd = drive_[static_cast<std::size_t>(parent.particle)];
        Frame parent_ref = reference_frame(sb.parent_sim, pose);
        parent_ref = interpolate(parent_ref, pose[static_cast<std::size_t>(parent.bone)], maxf(pd.guide, pd.authority));
        base = compose(parent_new, relative(parent_ref, ref));
        base.s = anim.s;
    }
    const Vec3 so{p.offset.x * base.s.x, p.offset.y * base.s.y, p.offset.z * base.s.z};
    const Vec3 d_ref = rotate(base.q, so);
    const Vec3 d_sol = x_[pi] - base.t;
    Quat q = base.q;
    const float lr = length(d_ref), ls = length(d_sol);
    if (lr > 1e-7F && ls > 1e-7F) {
        // turned onto its solved point; a link squeezed short (a thigh pushing the coat up under the belt)
        // or folded back past its head gives that turn no clear direction, so it eases back to the
        // reference's instead of spinning with the noise
        const Vec3 a = d_ref * (1.0F / lr), b = d_sol * (1.0F / ls);
        const float trust = clampf((ls / lr - 0.2F) / 0.3F, 0.0F, 1.0F) * clampf((dot(a, b) + 0.7F) / 0.4F, 0.0F, 1.0F);
        q = nlerp(base.q, normalized(arc(a, b) * base.q), trust);
    }
    if (twist && g.twist_from_neighbours && p.left >= 0 && p.right >= 0 && ls > 1e-7F) {
        const auto l = static_cast<std::size_t>(p.left), r = static_cast<std::size_t>(p.right);
        const Vec3 axis = d_sol * (1.0F / ls);
        const Vec3 s_sol = x_[r] - x_[l];
        // the same span in the reference, turned as the bone was turned onto its point
        const Vec3 s_ref = rotate(q * conjugate(ref.q), target_of(r, pose) - target_of(l, pose));
        const Vec3 a = s_ref - axis * dot(s_ref, axis), b = s_sol - axis * dot(s_sol, axis);
        const float la = length(a), lb = length(b);
        if (la > 1e-6F && lb > 1e-6F) {
            const float c = dot(a, b) / (la * lb);
            float angle = std::atan2(dot(axis, cross(a, b)), dot(a, b));
            const float limit = d.params.get(Param::twist_limit);
            // neighbours bunched together or crossed (the coat folded) say nothing of the roll: it eases out
            const float trust = clampf((lb / la - 0.3F) / 0.3F, 0.0F, 1.0F) * clampf((c + 0.2F) / 0.4F, 0.0F, 1.0F);
            angle = maxf(-limit, minf(limit, angle)) * trust;
            q = normalized(axis_angle(axis, angle) * q);
        }
    }
    Frame out;
    out.q = q;
    out.s = anim.s;
    // translation = solved point - R * S * local offset: the bone lands on its point, its head moved with it
    // (a lagging tip pulls the head along: hair stretched off the scalp in a fast move); `rotate_only` keeps
    // the head where its simulated parent carries it, else where the animation has it
    if (d.params.get(Param::rotate_only) >= 0.5F)
        out.t = sb.parent_sim >= 0 ? base.t : anim.t;
    else
        out.t = x_[pi] - rotate(q, so);
    bone_done_[index] = 1;
    return bone_out_[index] = out;
}

void Simulation::place_kinematic(const std::vector<Frame> &pose)
{
    bool carried = false;
    for (std::size_t i = 0; i < x_.size(); ++i) {
        const Particle &p = rig_.particles[i];
        if (p.dynamic)
            continue;
        if (p.carrier >= 0) {
            carried = true;
            continue;
        }
        x_[i] = animated_of(i, pose);
    }
    if (!carried)
        return;
    std::fill(bone_done_.begin(), bone_done_.end(), std::uint8_t{0});
    for (std::size_t i = 0; i < x_.size(); ++i) {
        const Particle &p = rig_.particles[i];
        if (p.dynamic || p.carrier < 0)
            continue;
        const SimBone &by = rig_.bones_out[static_cast<std::size_t>(p.carrier)];
        const Frame f = bone_frame(p.carrier, pose, false);
        const Frame rel = relative(pose[static_cast<std::size_t>(by.bone)], pose[static_cast<std::size_t>(p.bone)]);
        x_[i] = apply(compose(f, rel), p.offset);
    }
}

void Simulation::initialize(const std::vector<Frame> &pose, const TickSettings &s, int relax)
{
    const auto t0 = Clock::now();
    for (const int b : rig_.needed)
        sub1_[static_cast<std::size_t>(b)] = pose[static_cast<std::size_t>(b)];
    sub0_ = sub1_;
    shapes(sub1_, shape1_);
    drape(sub1_, shape1_, draped_);
    ease_drape(0.0F, true);
    for (std::size_t i = 0; i < x_.size(); ++i) {
        pinned_[i] = 0;
        if (!rig_.particles[i].dynamic)
            continue;
        target_[i] = target_prev_[i] = draped_[i];
        x_[i] = prev_[i] = target_[i];
        v_[i] = target_vel_[i] = {};
        lambda_follow_[i] = 0.0F;
    }
    place_kinematic(sub1_);
    for (std::size_t i = 0; i < x_.size(); ++i)
        if (!rig_.particles[i].dynamic)
            prev_[i] = x_[i];
    shapes(sub1_, shape1_);
    shape0_ = shape1_;
    bounds_ = {};
    for (std::size_t i = 0; i < x_.size(); ++i)
        bounds_.add(x_[i]);
    bounds_.grow(0.5F);
    env_.clear();
    if (s.environment && s.env != nullptr)
        s.env->gather(bounds_, env_);
    if (s.ground_only)
        std::erase_if(env_, [](const Triangle &t) { return t.kind != Surface::ground; });
    env_boxes_.clear();
    for (const auto &t : env_) {
        Aabb b;
        b.add(t.a);
        b.add(t.b);
        b.add(t.c);
        b.grow(s.environment_thickness + kDeepBehind); // what a face may push out reaches this far
        env_boxes_.push_back(b);
    }
    // bounded relaxation: links and contacts settle before anything is drawn. The colliders sweep in from
    // where the rest pose has them on the first group's reference bone, so a pose that starts with the legs
    // apart carries the coat out around them (contact keeps the side a control came from) instead of
    // squeezing it between them.
    std::vector<Shape> arrived(shape1_), from(shape1_);
    if (!rig_.groups.empty() && relax > 0) {
        const auto rb = static_cast<std::size_t>(rig_.groups.front().reference_bone);
        std::vector<Frame> rest(rig_.skeleton.rest.size());
        for (const int b : rig_.needed) {
            const auto i = static_cast<std::size_t>(b);
            rest[i] = compose(sub1_[rb], relative(frame_of(rig_.skeleton.rest[rb]), frame_of(rig_.skeleton.rest[i])));
        }
        shapes(rest, from);
    }
    animated_lengths();
    const float h = s.dt / static_cast<float>(std::max(1, s.substeps));
    const int sweep = std::max(1, relax * 2 / 3);
    for (int k = 0; k < relax; ++k) {
        shape0_ = shape1_;
        const float u = minf(1.0F, static_cast<float>(k + 1) / static_cast<float>(sweep));
        for (std::size_t c = 0; c < shape1_.size(); ++c)
            shape1_[c] = {lerp(from[c].a, arrived[c].a, u), lerp(from[c].b, arrived[c].b, u),
                          from[c].ra + (arrived[c].ra - from[c].ra) * u, from[c].rb + (arrived[c].rb - from[c].rb) * u};
        for (std::size_t i = 0; i < x_.size(); ++i)
            if (rig_.particles[i].dynamic)
                prev_[i] = x_[i];
        for (std::size_t f = 0; f < rig_.faces.size(); ++f) { // the side each face is on, as a substep keeps it
            const Face &face = rig_.faces[f];
            face_normal_prev_[f] =
                normalized(cross(x_[static_cast<std::size_t>(face.b)] - x_[static_cast<std::size_t>(face.a)],
                                 x_[static_cast<std::size_t>(face.c)] - x_[static_cast<std::size_t>(face.a)]));
        }
        std::fill(lambda_.begin(), lambda_.end(), 0.0F);
        for (std::size_t c = 0; c < compliance_.size(); ++c)
            compliance_[c] = 0.0F;
        solve_links(h);
        solve_limits();
        limit_strain(false);
        collide_model(0.0F, true);
        if (s.environment)
            collide_environment(s);
        if (s.self_collision)
            collide_self();
        limit_strain(true);
        for (int spread = 0; spread < 2; ++spread) { // as a tick spreads a push, contact last
            limit_strain(false);
            limit_strain(true);
            collide_model(0.0F, true);
        }
    }
    shape0_ = shape1_ = arrived;
    for (std::size_t i = 0; i < x_.size(); ++i)
        prev_[i] = x_[i];
    for (std::size_t f = 0; f < rig_.faces.size(); ++f) {
        const Face &face = rig_.faces[f];
        face_normal_prev_[f] =
            normalized(cross(x_[static_cast<std::size_t>(face.b)] - x_[static_cast<std::size_t>(face.a)],
                             x_[static_cast<std::size_t>(face.c)] - x_[static_cast<std::size_t>(face.a)]));
    }
    fresh_ = true;
    shown_.assign(x_.size(), {});
    for (std::size_t i = 0; i < x_.size(); ++i)
        if (rig_.particles[i].dynamic)
            shown_[i] = apply_inverse(reference_bone_frame(i, sub1_), x_[i]);
    times.startup += us(t0, Clock::now());
}

void Simulation::settle(const std::vector<Frame> &pose, const TickSettings &s, int ticks)
{
    // only the groups that hang (`settle on`: cords, a hood) start in a shape the pose may not have; the others
    // start on their targets, which follow the pose, and are put back as they were
    const std::vector<Vec3> x0 = x_, prev0 = prev_, v0 = v_;
    const auto settles = [&](std::size_t i) {
        return rig_.particles[i].dynamic && rig_.groups[static_cast<std::size_t>(rig_.particles[i].group)].settle;
    };
    for (int k = 0; k < ticks; ++k) {
        tick(pose, pose, s);
        for (std::size_t i = 0; i < v_.size(); ++i)
            if (rig_.particles[i].dynamic)
                v_[i] = v_[i] * kSettleKeep;
    }
    for (std::size_t i = 0; i < x_.size(); ++i)
        if (rig_.particles[i].dynamic && !settles(i)) {
            x_[i] = x0[i];
            prev_[i] = prev0[i];
            v_[i] = v0[i];
        }
    for (std::size_t i = 0; i < x_.size() && shown_.size() == x_.size(); ++i)
        if (settles(i))
            shown_[i] = apply_inverse(reference_bone_frame(i, pose), x_[i]);
    fresh_ = true; // its first tick takes up the body's velocity, as a start's does (spawned while running)
}

void Simulation::rebase(const std::vector<Frame> &a, const std::vector<Frame> &b)
{
    for (std::size_t i = 0; i < x_.size(); ++i) {
        const Particle &p = rig_.particles[i];
        const auto rb = static_cast<std::size_t>(rig_.groups[static_cast<std::size_t>(p.group)].reference_bone);
        x_[i] = apply(b[rb], apply_inverse(a[rb], x_[i]));
        prev_[i] = x_[i];
        target_[i] = apply(b[rb], apply_inverse(a[rb], target_[i]));
        target_prev_[i] = target_[i];
        v_[i] = target_vel_[i] = {};
    }
    fresh_ = true;
}

void Simulation::tick(const std::vector<Frame> &from, const std::vector<Frame> &to, const TickSettings &s)
{
    const int n = std::max(1, s.substeps);
    const float h = s.dt / static_cast<float>(n);
    contacts = 0;
    // the room's triangles near where the particles can reach this tick
    if (s.environment && s.env != nullptr) {
        const auto t0 = Clock::now();
        Aabb box;
        float reach = 0.0F;
        for (std::size_t i = 0; i < x_.size(); ++i) {
            box.add(x_[i]);
            if (rig_.particles[i].dynamic)
                reach = maxf(reach, drive_[i].params.get(Param::max_speed));
        }
        frames_at(from, to, 1.0F, sub1_);
        for (std::size_t i = 0; i < x_.size(); ++i)
            if (rig_.particles[i].dynamic)
                box.add(target_of(i, sub1_));
        box.grow(minf(reach * s.dt, 1.0F) + s.environment_thickness + kDeepBehind);
        env_.clear();
        s.env->gather(box, env_);
        if (s.ground_only) // walls caught the cloth (pinned between a leg and a wall): floors alone
            std::erase_if(env_, [](const Triangle &t) { return t.kind != Surface::ground; });
        env_boxes_.clear();
        for (const auto &t : env_) {
            Aabb b;
            b.add(t.a);
            b.add(t.b);
            b.add(t.c);
            b.grow(s.environment_thickness + kDeepBehind);
            env_boxes_.push_back(b);
        }
        environment_triangles = env_.size();
        times.environment_gather += us(t0, Clock::now());
    } else {
        env_.clear();
        env_boxes_.clear();
        environment_triangles = 0;
    }
    // compliance of each link from its particles' settings
    for (std::size_t c = 0; c < rig_.distances.size(); ++c) {
        const Distance &d = rig_.distances[c];
        const auto ia = static_cast<std::size_t>(d.a), ib = static_cast<std::size_t>(d.b);
        const Param which = d.kind == Link::shear  ? Param::shear_compliance
                            : d.kind == Link::bend ? Param::bend_compliance
                            : d.kind == Link::row  ? Param::row_compliance
                                                   : Param::stretch_compliance;
        const bool da = rig_.particles[ia].dynamic, db = rig_.particles[ib].dynamic;
        const float ca = drive_[ia].params.get(which), cb = drive_[ib].params.get(which);
        compliance_[c] = da && db ? 0.5F * (ca + cb) : da ? ca : cb;
    }
    frames_at(from, to, 0.0F, sub0_);
    shapes(sub0_, shape0_);
    for (int sub = 0; sub < n; ++sub) {
        const auto t0 = Clock::now();
        frames_at(from, to, static_cast<float>(sub + 1) / static_cast<float>(n), sub1_);
        if (game_now_.size() == x_.size())
            for (std::size_t i = 0; i < x_.size(); ++i)
                game_now_[i] = lerp(game_from_[i], game_to_[i], static_cast<float>(sub + 1) / static_cast<float>(n));
        shapes(sub1_, shape1_);
        for (std::size_t f = 0; f < rig_.faces.size(); ++f) {
            const Face &face = rig_.faces[f];
            face_normal_prev_[f] =
                normalized(cross(x_[static_cast<std::size_t>(face.b)] - x_[static_cast<std::size_t>(face.a)],
                                 x_[static_cast<std::size_t>(face.c)] - x_[static_cast<std::size_t>(face.a)]));
        }
        const auto t1 = Clock::now();
        times.pose += us(t0, t1);
        bounds_ = {};
        // the targets, draped around the colliders (and at the substep's start too, just started)
        std::vector<Vec3> &start = target_prev_;
        if (fresh_)
            drape(sub0_, shape0_, start);
        drape(sub1_, shape1_, draped_);
        ease_drape(h, false);
        for (std::size_t i = 0; i < x_.size(); ++i) {
            const Particle &p = rig_.particles[i];
            if (!p.dynamic) {
                prev_[i] = x_[i];
                continue;
            }
            const ParticleDrive &d = drive_[i];
            const ParamBlock &k = d.params;
            const Vec3 t = draped_[i];
            const Vec3 g = s.gravity * k.get(Param::gravity_scale);
            // just started: the target's own velocity from the substep's start, so a moving start is still
            const Vec3 tp = fresh_ ? start[i] : target_[i];
            const Vec3 vt = (t - tp) * (1.0F / h);
            target_prev_[i] = tp;
            target_[i] = t;
            pinned_[i] = d.authority >= 0.999F ? 1 : 0;
            prev_[i] = x_[i];
            if (pinned_[i]) {
                x_[i] = animated_of(i, sub1_);
                v_[i] = vt;
            } else {
                Vec3 v = fresh_ ? vt : v_[i];
                const float inertia = k.get(Param::inertia) + (k.get(Param::guided_inertia) - k.get(Param::inertia)) *
                                                                  maxf(d.guide, d.authority);
                if (!fresh_)
                    v += (vt - target_vel_[i]) * (1.0F - inertia); // the frame's acceleration not felt
                v = vt + (v - vt) * std::exp(-k.get(Param::damping) * h);
                v += g * h;
                const Vec3 air = wind_[static_cast<std::size_t>(p.group)] - v;
                v += air * minf(1.0F, k.get(Param::wind_drag) * h);
                Vec3 rel = v - vt;
                const float speed = length(rel), most = k.get(Param::max_speed);
                if (speed > most)
                    rel = rel * (most / speed);
                v_[i] = vt + rel;
                x_[i] += v_[i] * h;
                // `no_sim`: not simulated, each substep on its target (its bones, the legs' carry, the drape);
                // contact with the body still keeps it out of the legs, carried on from the last substep
                const float ns = k.get(Param::no_sim);
                if (ns > 0.0F) {
                    x_[i] = lerp(x_[i], t, ns);
                    v_[i] = lerp(v_[i], vt, ns);
                }
            }
            target_vel_[i] = vt;
            bounds_.add(x_[i]);
        }
        fresh_ = false;
        bounds_.grow(0.05F);
        animated_lengths();
        std::fill(lambda_.begin(), lambda_.end(), 0.0F);
        std::fill(lambda_follow_.begin(), lambda_follow_.end(), 0.0F);
        std::fill(pushed_.begin(), pushed_.end(), Vec3{});
        place_kinematic(sub1_);
        const auto t2 = Clock::now();
        times.solve += us(t1, t2);
        for (int it = 0; it < std::max(1, s.iterations); ++it) {
            const auto a = Clock::now();
            if (it > 0)
                place_kinematic(sub1_); // anchors riding simulated bones follow them
            solve_links(h);
            solve_follow(h);
            solve_limits();
            limit_strain(false);
            const auto b = Clock::now();
            // controls each pass; the edges and faces between them on the substep's last
            collide_model(1.0F, it + 1 == std::max(1, s.iterations));
            const auto c = Clock::now();
            if (s.environment && !env_.empty())
                collide_environment(s);
            const auto e = Clock::now();
            const bool last = it + 1 == std::max(1, s.iterations);
            if (s.self_collision && last)
                collide_self();
            limit_strain(true); // the body attachment over contact: an overlap stays at the belt
            const auto f = Clock::now();
            times.solve += us(a, b);
            times.model_collision += us(b, c);
            times.environment_query += us(c, e);
            times.self_collision += us(e, f);
        }
        // a body's push spread to the neighbours: strain held, the controls kept out, twice
        const auto t25 = Clock::now();
        for (int k = 0; k < kSpreadPasses; ++k) { // contact last, the body's after the room's: the body is
            limit_strain(false);                  // never left inside the coat
            limit_strain(true);
            if (s.environment && !env_.empty())
                collide_environment(s);
            collide_model(0.0F, true);
        }
        // `game_shape`: that far onto the game model's surface after everything else (at 1 nothing pushes it off)
        if (game_now_.size() == x_.size())
            for (std::size_t i = 0; i < x_.size(); ++i) {
                const float gs = game_shape_of(i);
                if (gs > 0.0F)
                    x_[i] = lerp(x_[i], game_now_[i], gs);
            }
        const auto t3 = Clock::now();
        times.model_collision += us(t25, t3);
        // velocity from the motion, never more than max_speed past the target's: a contact's push moves
        // the cloth, it does not launch it. Contact is inelastic: what a push gave it away from the body,
        // past the target's own motion, is taken back (cloth hanging on a heel otherwise bounces off it,
        // falls back and is thrown again, a flapping that a loop's small kick restarts)
        for (std::size_t i = 0; i < x_.size(); ++i)
            if (rig_.particles[i].dynamic && !pinned_[i]) {
                v_[i] = (x_[i] - prev_[i]) * (1.0F / h);
                const float lp = length(pushed_[i]);
                if (lp > 1e-7F) {
                    const Vec3 dir = pushed_[i] * (1.0F / lp);
                    const float out = dot(v_[i] - target_vel_[i], dir);
                    if (out > 0.0F)
                        v_[i] -= dir * out;
                }
                const Vec3 rel = v_[i] - target_vel_[i];
                const float speed = length(rel), most = drive_[i].params.get(Param::max_speed);
                if (speed > most)
                    v_[i] = target_vel_[i] + rel * (most / speed);
            }
        sub0_.swap(sub1_);
        shape0_.swap(shape1_);
        times.solve += us(t3, Clock::now());
    }
    // sub0_ and shape0_ hold the tick's end now; keep shape1_ as the colliders' last place
    shape1_ = shape0_;
    sub1_ = sub0_;
    recover();
    // the drawn shape: eased toward the solved one in the reference bone's space, so the body's own
    // motion is never delayed, only the cloth's jumps from step to step (contact against a leg sweeping
    // through it) are smoothed
    for (std::size_t i = 0; i < x_.size(); ++i) {
        if (!rig_.particles[i].dynamic)
            continue;
        const Frame &ref = reference_bone_frame(i, sub1_);
        const Vec3 local = apply_inverse(ref, x_[i]);
        const float tau = drive_[i].params.get(Param::smoothing);
        if (tau <= 0.0F || game_shape_of(i) >= 1.0F) { // on the game's surface: nothing to smooth
            shown_[i] = local;
            continue;
        }
        shown_[i] = shown_[i] + (local - shown_[i]) * (1.0F - std::exp(-s.dt / tau));
        // never drawn inside the body where the solved point is clear of it: out on the solved point's side
        Vec3 w = apply(ref, shown_[i]);
        bool moved = false;
        for (const auto &[c, li] : particle_colliders_[i]) {
            (void)li;
            const auto ci = static_cast<std::size_t>(c);
            if (!rig_.colliders[ci].rest_side)
                continue;
            const Shape &sh = shape1_[ci];
            float t, r;
            Vec3 q;
            core_point(sh, w, t, q, r);
            const float th = pair_reach(r + rig_.particles[i].thickness, point_clearance(ci, i, sh),
                                        0.5F * r + rig_.particles[i].thickness);
            const Vec3 d = w - q;
            const float dist = length(d);
            if (dist >= th)
                continue;
            Vec3 dir = dist > 1e-6F ? d * (1.0F / dist) : normalized(x_[i] - q, {0.0F, 1.0F, 0.0F});
            if (dot(dir, x_[i] - q) < 0.0F)
                dir = normalized(x_[i] - q, dir);
            w = q + dir * th;
            moved = true;
        }
        if (moved)
            shown_[i] = apply_inverse(ref, w);
    }
    guard_drawn_faces();
}

// Nor a drawn face cutting into the body deeper than the solved one: its corners shown as solved this
// tick. The smoothing trails a fast leg, and a calf or knee swinging through the lagging coat (a run, a
// jump) is the clipping it shows; where nothing cuts, the smoothing is left as it is.
void Simulation::guard_drawn_faces()
{
    constexpr float kSlack = 0.005F; // m: deeper than the solved face by more than this
    bool smoothed = false;
    for (std::size_t i = 0; i < x_.size() && !smoothed; ++i)
        smoothed = rig_.particles[i].dynamic && drive_[i].params.get(Param::smoothing) > 0.0F &&
                   drive_[i].params.get(Param::draw_guard) >= 0.5F;
    if (!smoothed)
        return;
    drawn_.resize(x_.size());
    for (std::size_t i = 0; i < x_.size(); ++i)
        drawn_[i] = rig_.particles[i].dynamic ? apply(reference_bone_frame(i, sub1_), shown_[i]) : x_[i];
    for (const int ci : collider_order_) {
        const auto c = static_cast<std::size_t>(ci);
        if (!rig_.colliders[c].rest_side)
            continue;
        const Shape &sh = shape1_[c];
        for (const int fi : collider_faces_[c]) {
            const Face &f = rig_.faces[static_cast<std::size_t>(fi)];
            const std::size_t v[3] = {static_cast<std::size_t>(f.a), static_cast<std::size_t>(f.b),
                                      static_cast<std::size_t>(f.c)};
            if (drive_[v[0]].params.get(Param::draw_guard) < 0.5F)
                continue;
            const float thick =
                (rig_.particles[v[0]].thickness + rig_.particles[v[1]].thickness + rig_.particles[v[2]].thickness) /
                3.0F;
            const auto depth = [&](const std::vector<Vec3> &at, bool &crosses) {
                Vec3 on_core, on_face;
                const float d =
                    closest_segment_triangle(sh.a, sh.b, at[v[0]], at[v[1]], at[v[2]], on_core, on_face, crosses);
                const float ab2 = dot(sh.b - sh.a, sh.b - sh.a);
                const float t = ab2 > 1e-12F ? clampf(dot(on_core - sh.a, sh.b - sh.a) / ab2, 0.0F, 1.0F) : 0.0F;
                const float r = sh.ra + (sh.rb - sh.ra) * t;
                return pair_reach(r + thick, face_clearance(c, static_cast<std::size_t>(fi), sh), 0.5F * r + thick) - d;
            };
            bool drawn_crosses = false, solved_crosses = false;
            const float drawn_depth = depth(drawn_, drawn_crosses);
            if (drawn_depth <= kSlack && !drawn_crosses)
                continue;
            const float solved_depth = depth(x_, solved_crosses);
            const float allowed = maxf(0.0F, solved_depth) + kSlack;
            if (!(drawn_crosses && !solved_crosses) && drawn_depth <= allowed)
                continue;
            // toward the solved face only as far as it takes to come out (a whole step back to it is a jump):
            // the share found by halving between the drawn and the solved corners
            const Vec3 from[3] = {drawn_[v[0]], drawn_[v[1]], drawn_[v[2]]};
            std::vector<Vec3> &at = drawn_;
            const auto share = [&](float limit) {
                float lo = 0.0F, hi = 1.0F;
                for (int k = 0; k < 6; ++k) {
                    const float mid = 0.5F * (lo + hi);
                    for (std::size_t j = 0; j < 3; ++j)
                        at[v[j]] = lerp(from[j], x_[v[j]], mid);
                    bool crosses = false;
                    const float dm = depth(at, crosses);
                    if ((crosses && !solved_crosses) || dm > limit)
                        lo = mid;
                    else
                        hi = mid;
                }
                return hi;
            };
            const float hi = share(allowed);
            for (std::size_t j = 0; j < 3; ++j) {
                if (!rig_.particles[v[j]].dynamic) {
                    drawn_[v[j]] = from[j];
                    continue;
                }
                drawn_[v[j]] = lerp(from[j], x_[v[j]], hi);
                shown_[v[j]] = apply_inverse(reference_bone_frame(v[j], sub1_), drawn_[v[j]]);
            }
        }
    }
}

// The targets at `pose` draped down each strip: a control's segment from its anchor kept as the target
// shape has it, swung out of any collider it would enter to the side it belongs on, its length kept. The
// pull then leads around a raised knee instead of through it.
// The drape's swing of each target eased in its reference bone's space: where the drape's answer jumps (a
// target between a thigh and a calf swung out of one into the other, step to step) the target glides over
// kDrapeEase instead of snapping, and the coat no longer chases it back and forth once a cycle
void Simulation::ease_drape(float h, bool exact)
{
    drape_off_.resize(x_.size());
    const float k = exact ? 1.0F : 1.0F - std::exp(-h / kDrapeEase);
    for (std::size_t i = 0; i < x_.size(); ++i) {
        if (!rig_.particles[i].dynamic)
            continue;
        const Quat q = reference_bone_frame(i, sub1_).q;
        const Vec3 off = rotate(conjugate(q), draped_[i] - raw_[i]);
        drape_off_[i] = drape_off_[i] + (off - drape_off_[i]) * k;
        draped_[i] = raw_[i] + rotate(q, drape_off_[i]);
    }
}

void Simulation::drape(const std::vector<Frame> &pose, const std::vector<Shape> &shapes, std::vector<Vec3> &out)
{
    for (std::size_t i = 0; i < x_.size(); ++i) {
        if (rig_.particles[i].dynamic)
            raw_[i] = target_of(i, pose);
        else
            raw_[i] = rig_.particles[i].carrier >= 0 ? x_[i] : animated_of(i, pose);
        out[i] = raw_[i];
    }
    for (const int pi : drape_order_) {
        const auto i = static_cast<std::size_t>(pi);
        const Particle &p = rig_.particles[i];
        if (p.anchor < 0 || particle_colliders_[i].empty() || drive_[i].authority >= 0.999F ||
            drive_[i].params.get(Param::drape) < 0.5F)
            continue;
        const auto j = static_cast<std::size_t>(p.anchor);
        const Vec3 base = out[j], seg = raw_[i] - raw_[j];
        const float len = length(seg);
        Vec3 at = base + seg;
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto &[c, li] : particle_colliders_[i]) {
                if (!rig_.colliders[static_cast<std::size_t>(c)].rest_side)
                    continue; // only the body says where to drape (the garment is outside it)
                const Shape &sh = shapes[static_cast<std::size_t>(c)];
                float t, r;
                Vec3 q;
                core_point(sh, at, t, q, r);
                const float th = pair_reach(r + p.thickness, point_clearance(static_cast<std::size_t>(c), i, sh),
                                            0.5F * r + p.thickness);
                const Vec3 d = at - q;
                const float dist = length(d);
                if (dist >= th)
                    continue;
                const Vec3 axis = normalized(sh.b - sh.a, {0.0F, 0.0F, 0.0F});
                Vec3 belongs = rotate(
                    pose[static_cast<std::size_t>(rig_.groups[static_cast<std::size_t>(p.group)].reference_bone)].q,
                    outward_[i]);
                belongs = normalized(belongs - axis * dot(belongs, axis), belongs);
                Vec3 dir = dist > 1e-6F ? d * (1.0F / dist) : belongs;
                if (dot(dir, belongs) < 0.0F)
                    dir = belongs;
                at = q + dir * (th + 0.002F);
                if (len > 1e-6F) // swung about its anchor, its length kept
                    at = base + normalized(at - base, seg * (1.0F / len)) * len;
            }
        }
        out[i] = at;
    }
}

// The links' and tethers' lengths now: their rest ones, or as far as the controls are guided, the
// animation's (a hand-animated coat stretches its bones: KH2's battle coats several times over)
void Simulation::animated_lengths()
{
    const auto aim = [&](std::size_t i) { return rig_.particles[i].dynamic ? target_[i] : x_[i]; };
    const auto guided = [&](std::size_t i) {
        return rig_.particles[i].dynamic ? maxf(drive_[i].guide, drive_[i].authority) : 1.0F;
    };
    for (std::size_t c = 0; c < rig_.distances.size(); ++c) {
        const Distance &d = rig_.distances[c];
        const auto a = static_cast<std::size_t>(d.a), b = static_cast<std::size_t>(d.b);
        const float g = minf(guided(a), guided(b));
        rest_now_[c] = g > 0.0F ? d.rest + (length(aim(b) - aim(a)) - d.rest) * g : d.rest;
    }
    for (std::size_t i = 0; i < x_.size(); ++i) {
        const Particle &p = rig_.particles[i];
        tether_now_[i] = p.tether;
        if (p.root >= 0 && p.tether > 0.0F && guided(i) > 0.0F)
            tether_now_[i] = maxf(
                p.tether, p.tether + (length(aim(i) - aim(static_cast<std::size_t>(p.root))) - p.tether) * guided(i));
    }
}

void Simulation::solve_links(float h)
{
    const float inv_h2 = 1.0F / (h * h);
    for (std::size_t c = 0; c < rig_.distances.size(); ++c) {
        const Distance &d = rig_.distances[c];
        const auto a = static_cast<std::size_t>(d.a), b = static_cast<std::size_t>(d.b);
        const float wa = w(a), wb = w(b);
        if (wa + wb <= 0.0F)
            continue;
        const Vec3 delta = x_[b] - x_[a];
        const float len = length(delta);
        if (len < 1e-9F)
            continue;
        const Vec3 nrm = delta * (1.0F / len);
        const float C = len - rest_now_[c];
        const float at = compliance_[c] * inv_h2;
        const float dl = (-C - at * lambda_[c]) / (wa + wb + at);
        lambda_[c] += dl;
        x_[a] -= nrm * (wa * dl);
        x_[b] += nrm * (wb * dl);
    }
}

// Links held within their strain limit (`max_strain` along strips and to anchors, `row_max_strain`
// around rows and across faces): a body pushing a control out pulls its neighbours along instead of
// tearing it off them.
void Simulation::limit_strain(bool anchors_only)
{
    for (std::size_t c = 0; c < rig_.distances.size(); ++c) {
        const Distance &d = rig_.distances[c];
        if (d.kind == Link::bend || (anchors_only && d.kind != Link::anchor))
            continue;
        const auto a = static_cast<std::size_t>(d.a), b = static_cast<std::size_t>(d.b);
        const float wa = w(a), wb = w(b);
        if (wa + wb <= 0.0F)
            continue;
        const Vec3 delta = x_[b] - x_[a];
        const Param which = d.kind == Link::row || d.kind == Link::shear ? Param::row_max_strain : Param::max_strain;
        const auto &pa = drive_[a].params, &pb = drive_[b].params;
        const float strain = rig_.particles[a].dynamic && rig_.particles[b].dynamic
                                 ? 0.5F * (pa.get(which) + pb.get(which))
                                 : (rig_.particles[a].dynamic ? pa : pb).get(which);
        const float len = length(delta), most = rest_now_[c] * (1.0F + strain);
        if (len <= most)
            continue;
        const Vec3 move = delta * ((len - most) / (len * (wa + wb)));
        x_[a] += move * wa;
        x_[b] -= move * wb;
    }
}

void Simulation::solve_follow(float h)
{
    for (std::size_t i = 0; i < x_.size(); ++i) {
        const float wi = w(i);
        if (wi <= 0.0F)
            continue;
        const ParticleDrive &d = drive_[i];
        float f = d.params.get(Param::follow_frequency);
        if (d.authority > 0.0F) // part-authored: pulled harder toward the animation
            f += (30.0F - f) * d.authority;
        if (f <= 0.0F)
            continue;
        const Vec3 delta = x_[i] - target_[i];
        const float C = length(delta);
        if (C < 1e-9F)
            continue;
        const Vec3 nrm = delta * (1.0F / C);
        const float om = kTwoPi * f;
        const float alpha = wi / (om * om) / (h * h); // compliance 1 / (m w^2), over h^2
        const float gamma = 2.0F * d.params.get(Param::follow_damping) / (om * h);
        const float rel = dot(nrm, (x_[i] - prev_[i]) - (target_[i] - target_prev_[i]));
        const float dl = (-C - alpha * lambda_follow_[i] - gamma * rel) / ((1.0F + gamma) * wi + alpha);
        lambda_follow_[i] += dl;
        x_[i] += nrm * (wi * dl);
    }
}

void Simulation::solve_limits()
{
    for (std::size_t i = 0; i < x_.size(); ++i) {
        if (w(i) <= 0.0F)
            continue;
        const Particle &p = rig_.particles[i];
        const ParamBlock &k = drive_[i].params;
        // tether: no further from the strip's anchor than its rest path, plus slack
        if (p.root >= 0 && p.tether > 0.0F) {
            const Vec3 r = x_[static_cast<std::size_t>(p.root)];
            const float most = tether_now_[i] * (1.0F + k.get(Param::tether_slack));
            const Vec3 d = x_[i] - r;
            const float l = length(d);
            if (l > most)
                x_[i] = r + d * (most / l);
        }
        // motion limit around the target
        const float md = k.get(Param::max_distance);
        if (md > 0.0F) {
            const Vec3 d = x_[i] - target_[i];
            const float l = length(d);
            if (l > md)
                x_[i] = target_[i] + d * (md / l);
        }
        // swing: within a cone about the target's direction from the head
        const float limit = k.get(Param::swing_limit);
        if (limit < kPi - 1e-3F && p.anchor >= 0) {
            const auto a = static_cast<std::size_t>(p.anchor);
            const Vec3 head = x_[a];
            const Vec3 head_target = rig_.particles[a].dynamic ? target_[a] : x_[a];
            const Vec3 dir = x_[i] - head, want = target_[i] - head_target;
            const float ld = length(dir), lw = length(want);
            if (ld < 1e-7F || lw < 1e-7F)
                continue;
            const Vec3 u = dir * (1.0F / ld), t = want * (1.0F / lw);
            const float angle = std::acos(minf(1.0F, maxf(-1.0F, dot(u, t))));
            if (angle > limit) {
                const Vec3 axis = normalized(cross(u, t), perpendicular(t));
                x_[i] = head + rotate(axis_angle(axis, angle - limit), dir);
            }
        }
    }
}

void Simulation::collide_model(float friction_scale, bool surfaces)
{
    // arms (side motion) first, the body (rest side) last: a coat caught between a hand and a leg stays
    // out of the leg
    for (const int ci : collider_order_) {
        const auto c = static_cast<std::size_t>(ci);
        const Shape &s1 = shape1_[c], &s0 = shape0_[c];
        Aabb box;
        box.add(s1.a);
        box.add(s1.b);
        box.grow(maxf(s1.ra, s1.rb) + 0.03F);
        if (!box.overlaps(bounds_))
            continue;
        const auto near_box = [&](Vec3 p) {
            return p.x >= box.lo.x && p.x <= box.hi.x && p.y >= box.lo.y && p.y <= box.hi.y && p.z >= box.lo.z &&
                   p.z <= box.hi.z;
        };
        // controls: pushed out to the side of the core they belong on (their rest side, in the collider's
        // bone space), with friction
        for (std::size_t li = 0; li < collider_points_[c].size(); ++li) {
            const int pi = collider_points_[c][li];
            const auto i = static_cast<std::size_t>(pi);
            if (!near_box(x_[i]) || (w(i) <= 0.0F && drive_[i].params.policy == AuthoredPolicy::exact))
                continue;
            const Particle &p = rig_.particles[i];
            float t, r;
            Vec3 q;
            core_point(s1, x_[i], t, q, r);
            const float th = pair_reach(r + p.thickness, point_clearance(c, i, s1), 0.5F * r + p.thickness);
            const Vec3 d = x_[i] - q;
            float dist = length(d);
            if (dist >= th)
                continue;
            // its side: the rest side about the core now, across the core's axis; or the side it came from
            Vec3 belongs;
            if (rig_.colliders[c].rest_side) {
                const Vec3 axis = normalized(s1.b - s1.a, {0.0F, 0.0F, 0.0F});
                belongs = rotate(
                    sub1_[static_cast<std::size_t>(rig_.groups[static_cast<std::size_t>(p.group)].reference_bone)].q,
                    outward_[i]);
                belongs = normalized(belongs - axis * dot(belongs, axis), belongs);
            } else {
                float t0, r0;
                Vec3 q0;
                core_point(s0, prev_[i], t0, q0, r0);
                belongs = normalized(prev_[i] - q0, d);
            }
            Vec3 nrm = dist > 1e-6F ? d * (1.0F / dist) : belongs;
            if (dot(nrm, belongs) < 0.0F) { // on the wrong side of the core: back out on its own side
                nrm = belongs;
                dist = -dist;
            }
            const float pen = th - dist;
            x_[i] += nrm * pen;
            pushed_[i] += nrm * pen;
            ++contacts;
            const float mu = drive_[i].params.get(Param::friction) * friction_scale;
            if (mu > 0.0F) {
                const Vec3 surface = (s1.a + (s1.b - s1.a) * t) - (s0.a + (s0.b - s0.a) * t);
                Vec3 slide = (x_[i] - prev_[i]) - surface;
                slide = slide - nrm * dot(slide, nrm);
                const float ls = length(slide);
                if (ls > 1e-9F)
                    x_[i] -= slide * minf(1.0F, mu * pen / ls);
            }
        }
        if (!surfaces)
            continue;
        // edges between controls: their closest point to the core pushed out, shared by their ends
        for (const int ei : collider_edges_[c]) {
            const auto &e = rig_.edges[static_cast<std::size_t>(ei)];
            const auto a = static_cast<std::size_t>(e[0]), b = static_cast<std::size_t>(e[1]);
            // its bounding sphere against the core's first
            const Vec3 mid = (x_[a] + x_[b]) * 0.5F;
            float tm, rm;
            Vec3 qm;
            core_point(s1, mid, tm, qm, rm);
            const float reach = 0.5F * length(x_[b] - x_[a]) + maxf(s1.ra, s1.rb) + rig_.particles[a].thickness;
            if (dot(mid - qm, mid - qm) > reach * reach)
                continue;
            const float wa = w(a), wb = w(b);
            if (wa + wb <= 0.0F)
                continue;
            float u, t;
            closest_segments(x_[a], x_[b], s1.a, s1.b, u, t);
            if (u < 0.02F || u > 0.98F)
                continue; // its ends are the controls' own tests
            const Vec3 pe = lerp(x_[a], x_[b], u), q = lerp(s1.a, s1.b, t);
            const float re = s1.ra + (s1.rb - s1.ra) * t,
                        te = 0.5F * (rig_.particles[a].thickness + rig_.particles[b].thickness);
            const float ca0 = point_clearance(c, a, s1), cb0 = point_clearance(c, b, s1);
            const float th = pair_reach(re + te, ca0 > 0.0F && cb0 > 0.0F ? minf(ca0, cb0) : 0.0F, 0.5F * re + te);
            const Vec3 d = pe - q;
            const float dist = length(d);
            if (dist >= th || dist < 1e-7F)
                continue;
            const Vec3 nrm = d * (1.0F / dist);
            const float ca = (1.0F - u) * wa, cb = u * wb, den = (1.0F - u) * ca + u * cb;
            if (den <= 0.0F)
                continue;
            // shared by the ends as their lever allows, no end moved more than twice the overlap (an end
            // near a fixed anchor would otherwise be thrown)
            const float pen = th - dist, s = pen / den;
            const float ma = minf(s * ca, kMaxLever * pen), mb = minf(s * cb, kMaxLever * pen);
            x_[a] += nrm * ma;
            x_[b] += nrm * mb;
            pushed_[a] += nrm * ma;
            pushed_[b] += nrm * mb;
            ++contacts;
        }
        // faces: sample points inside each coarse face, pushed out and shared by its corners
        for (std::size_t lf = 0; lf < collider_faces_[c].size(); ++lf) {
            const int fi = collider_faces_[c][lf];
            const Face &f = rig_.faces[static_cast<std::size_t>(fi)];
            const std::size_t v[3] = {static_cast<std::size_t>(f.a), static_cast<std::size_t>(f.b),
                                      static_cast<std::size_t>(f.c)};
            // its bounding sphere about its centre against the core's first
            const Vec3 centre = (x_[v[0]] + x_[v[1]] + x_[v[2]]) * (1.0F / 3.0F);
            const float fr =
                maxf(length(x_[v[0]] - centre), maxf(length(x_[v[1]] - centre), length(x_[v[2]] - centre)));
            float tc, rc;
            Vec3 qc;
            core_point(s1, centre, tc, qc, rc);
            const float reach = fr + maxf(s1.ra, s1.rb) + rig_.particles[v[0]].thickness;
            if (dot(centre - qc, centre - qc) > reach * reach)
                continue;
            const float wv[3] = {w(v[0]), w(v[1]), w(v[2])};
            if (wv[0] + wv[1] + wv[2] <= 0.0F)
                continue;
            const float thick =
                (rig_.particles[v[0]].thickness + rig_.particles[v[1]].thickness + rig_.particles[v[2]].thickness) /
                3.0F;
            // the face against the core exactly (its interior, not sample points: a flared face is wide)
            Vec3 on_core, on_face;
            bool crosses = false;
            const float dist =
                closest_segment_triangle(s1.a, s1.b, x_[v[0]], x_[v[1]], x_[v[2]], on_core, on_face, crosses);
            const float ab2 = dot(s1.b - s1.a, s1.b - s1.a);
            const float tcore = ab2 > 1e-12F ? clampf(dot(on_core - s1.a, s1.b - s1.a) / ab2, 0.0F, 1.0F) : 0.0F;
            const float rf = s1.ra + (s1.rb - s1.ra) * tcore;
            const float th =
                pair_reach(rf + thick, face_clearance(c, static_cast<std::size_t>(fi), s1), 0.5F * rf + thick);
            if (dist >= th)
                continue;
            // pushed to the side it belongs on (its rest side along its normal), wherever the core is now
            Vec3 side = normalized(cross(x_[v[1]] - x_[v[0]], x_[v[2]] - x_[v[0]]),
                                   face_normal_prev_[static_cast<std::size_t>(fi)]);
            if (rig_.colliders[c].rest_side &&
                crosses) { // pierced: its normal's sign that agrees with its corners' way out
                const Quat ref =
                    sub1_[static_cast<std::size_t>(
                              rig_.groups[static_cast<std::size_t>(rig_.particles[v[0]].group)].reference_bone)]
                        .q;
                const Vec3 out = rotate(ref, outward_[v[0]] + outward_[v[1]] + outward_[v[2]]);
                if (dot(side, out) < 0.0F)
                    side = -side;
            } else { // touching, not pierced (or an arm): the side it was on at the substep's start
                const Vec3 was = (prev_[v[0]] + prev_[v[1]] + prev_[v[2]]) * (1.0F / 3.0F);
                float t0, r0;
                Vec3 q0;
                core_point(s0, was, t0, q0, r0);
                side = face_normal_prev_[static_cast<std::size_t>(fi)];
                if (dot(was - q0, side) < 0.0F)
                    side = -side;
            }
            const float ahead = dot(on_face - on_core, side); // how far in front of the core, that side
            float pen = th - ahead;
            if (crosses) // pierced: carried out past the core's end on its side, as cloth rides over a kick
                pen = minf(kMaxPierce, maxf(pen, th + maxf(dot(s1.a - on_face, side), dot(s1.b - on_face, side))));
            if (pen <= 0.0F)
                continue;
            // the whole face out together: a coarse face is small beside a leg, and shared by its contact
            // point's barycentric weights it pivots on its far corner and stays inside
            for (std::size_t j = 0; j < 3; ++j)
                if (wv[j] > 0.0F) {
                    x_[v[j]] += side * pen;
                    pushed_[v[j]] += side * pen;
                }
            // friction, as a control's: a face resting on a knee slides only as the knee moves it. Without
            // it the face slid freely round the knee, climbing it and dropping back off (battle idle)
            const Vec3 surface = (s1.a + (s1.b - s1.a) * tcore) - (s0.a + (s0.b - s0.a) * tcore);
            for (std::size_t j = 0; j < 3; ++j) {
                const float mu = drive_[v[j]].params.get(Param::face_friction) * friction_scale;
                if (wv[j] <= 0.0F || mu <= 0.0F)
                    continue;
                Vec3 slide = (x_[v[j]] - prev_[v[j]]) - surface;
                slide = slide - side * dot(slide, side);
                const float ls = length(slide);
                if (ls > 1e-9F)
                    x_[v[j]] -= slide * minf(1.0F, mu * minf(pen, kFaceFrictionReach) / ls);
            }
            ++contacts;
        }
    }
}

void Simulation::collide_environment(const TickSettings &s)
{
    // `goal`: where the shape wants it; one behind a face it should be in front of comes back out
    const auto point_vs_room = [&](Vec3 x, Vec3 before, Vec3 goal, float th, Vec3 &push) {
        Aabb me;
        me.add(x);
        bool hit = false;
        push = {};
        for (std::size_t k = 0; k < env_.size(); ++k) {
            if (!env_boxes_[k].overlaps(me))
                continue;
            const Triangle &tri = env_[k];
            const Vec3 at = x + push;
            const float side = dot(at - tri.a, tri.n);
            if (side >= th)
                continue;
            const Vec3 q = closest_on_triangle(at, tri.a, tri.b, tri.c);
            if (side >= 0.0F) {
                const Vec3 d = at - q;
                const float dist = length(d);
                if (dist < th) {
                    push += (dist > 1e-7F ? d * (1.0F / dist) : tri.n) * (th - dist);
                    hit = true;
                }
                continue;
            }
            // behind it: out to the front if it crossed it now, or only just sits behind it
            const Vec3 onplane = at - tri.n * side;
            if (length(closest_on_triangle(onplane, tri.a, tri.b, tri.c) - onplane) > 1e-4F)
                continue;
            const float was = dot(before - tri.a, tri.n);
            // a floor is solid below it; a wall or ceiling only where the shape belongs in front of it
            const bool belongs_in_front =
                -side < kDeepBehind && (tri.kind == Surface::ground || dot(goal - tri.a, tri.n) >= 0.0F);
            if (was >= -1e-4F || -side < kShallowBehind || belongs_in_front) {
                push += tri.n * (th - side);
                hit = true;
            }
        }
        return hit;
    };
    const auto unsimulated = [&](std::size_t i) { return drive_[i].params.get(Param::no_sim) >= 0.5F; };
    for (std::size_t i = 0; i < x_.size(); ++i) {
        const Particle &p = rig_.particles[i];
        if (!p.dynamic || (w(i) <= 0.0F && drive_[i].params.policy == AuthoredPolicy::exact) || unsimulated(i))
            continue;
        const float th = maxf(p.thickness, s.environment_thickness);
        Vec3 push;
        if (!point_vs_room(x_[i], prev_[i], target_[i], th, push))
            continue;
        x_[i] += push;
        ++contacts;
        const float pen = length(push);
        const float mu = drive_[i].params.get(Param::friction);
        if (mu > 0.0F && pen > 0.0F) {
            const Vec3 nrm = push * (1.0F / pen);
            Vec3 slide = x_[i] - prev_[i];
            slide = slide - nrm * dot(slide, nrm);
            const float ls = length(slide);
            if (ls > 1e-9F)
                x_[i] -= slide * minf(1.0F, mu * pen / ls);
        }
    }
    // each coarse face's centre too: a floor between controls
    for (const Face &f : rig_.faces) {
        const std::size_t v[3] = {static_cast<std::size_t>(f.a), static_cast<std::size_t>(f.b),
                                  static_cast<std::size_t>(f.c)};
        const float wv[3] = {w(v[0]), w(v[1]), w(v[2])};
        const float wsum = wv[0] + wv[1] + wv[2];
        if (wsum <= 0.0F || unsimulated(v[0]) || unsimulated(v[1]) || unsimulated(v[2]))
            continue;
        const Vec3 c = (x_[v[0]] + x_[v[1]] + x_[v[2]]) * (1.0F / 3.0F);
        const Vec3 cb = (prev_[v[0]] + prev_[v[1]] + prev_[v[2]]) * (1.0F / 3.0F);
        Vec3 push;
        const Vec3 cg = (target_[v[0]] + target_[v[1]] + target_[v[2]]) * (1.0F / 3.0F);
        if (!point_vs_room(c, cb, cg, maxf(rig_.particles[v[0]].thickness, s.environment_thickness), push))
            continue;
        const float den = (wv[0] + wv[1] + wv[2]) / 9.0F;
        for (int j = 0; j < 3; ++j)
            x_[v[j]] += push * ((1.0F / 3.0F) * wv[j] / den);
        ++contacts;
    }
}

void Simulation::collide_self()
{
    if (self_points_.empty())
        return;
    for (std::size_t f = 0; f < rig_.faces.size(); ++f) {
        const Face &face = rig_.faces[f];
        Aabb b;
        b.add(x_[static_cast<std::size_t>(face.a)]);
        b.add(x_[static_cast<std::size_t>(face.b)]);
        b.add(x_[static_cast<std::size_t>(face.c)]);
        b.grow(2.0F * rig_.particles[static_cast<std::size_t>(face.a)].thickness);
        face_box_[f] = b;
    }
    for (std::size_t k = 0; k < self_points_.size(); ++k) {
        const auto i = static_cast<std::size_t>(self_points_[k]);
        const float wi = w(i);
        const Vec3 x = x_[i];
        const float thi = rig_.particles[i].thickness;
        const auto &ok = self_faces_ok_[k];
        for (std::size_t f = 0; f < rig_.faces.size(); ++f) {
            if (!(ok[f / 64] & (1ULL << (f % 64))))
                continue;
            const Face &face = rig_.faces[f];
            const std::size_t v[3] = {static_cast<std::size_t>(face.a), static_cast<std::size_t>(face.b),
                                      static_cast<std::size_t>(face.c)};
            const Aabb &fb = face_box_[f];
            if (x.x < fb.lo.x || x.x > fb.hi.x || x.y < fb.lo.y || x.y > fb.hi.y || x.z < fb.lo.z || x.z > fb.hi.z)
                continue;
            const float th = thi + rig_.particles[v[0]].thickness;
            const Vec3 a = x_[v[0]], b = x_[v[1]], c = x_[v[2]];
            const Vec3 q = closest_on_triangle(x_[i], a, b, c);
            Vec3 nrm = normalized(cross(b - a, c - a), face_normal_prev_[f]);
            // the side it was on at the substep's start
            const float before = dot(prev_[i] - prev_[v[0]], face_normal_prev_[f]);
            if (before < 0.0F)
                nrm = -nrm;
            if (length(x_[i] - q) >= th)
                continue;
            const float signed_dist = dot(x_[i] - q, nrm); // below 0: it crossed the face this substep
            const auto bc = barycentric(q, a, b, c);
            const float wv[3] = {w(v[0]), w(v[1]), w(v[2])};
            const float den = wi + bc[0] * bc[0] * wv[0] + bc[1] * bc[1] * wv[1] + bc[2] * bc[2] * wv[2];
            if (den <= 0.0F)
                continue;
            const float s = (th - signed_dist) / den;
            x_[i] += nrm * (s * wi);
            for (int j = 0; j < 3; ++j)
                x_[v[j]] -= nrm * (s * bc[static_cast<std::size_t>(j)] * wv[j]);
            ++contacts;
        }
    }
}

void Simulation::recover()
{
    for (std::size_t g = 0; g < rig_.groups.size(); ++g) {
        const GroupRig &gr = rig_.groups[g];
        bool lost = false;
        for (int i = gr.first; i < gr.last && !lost; ++i) {
            const auto k = static_cast<std::size_t>(i);
            if (!rig_.particles[k].dynamic)
                continue;
            lost = !finite(x_[k]) || !finite(v_[k]) || length(x_[k] - target_[k]) > kLostDistance;
        }
        if (!lost)
            continue;
        ++resets;
        for (int i = gr.first; i < gr.last; ++i) {
            const auto k = static_cast<std::size_t>(i);
            if (!rig_.particles[k].dynamic)
                continue;
            x_[k] = prev_[k] = target_[k];
            v_[k] = target_vel_[k];
        }
    }
}

void Simulation::reconstruct(const std::vector<Frame> &pose, std::vector<Frame> &out,
                             std::vector<std::uint8_t> &written)
{
    const auto t0 = Clock::now();
    std::fill(bone_done_.begin(), bone_done_.end(), std::uint8_t{0});
    // the bones rebuilt on the drawn (smoothed) shape, the solved one kept for the next step
    solved_ = x_;
    if (shown_.size() == x_.size())
        for (std::size_t i = 0; i < x_.size(); ++i)
            if (rig_.particles[i].dynamic)
                x_[i] = apply(reference_bone_frame(i, pose), shown_[i]);
    for (std::size_t si = 0; si < rig_.bones_out.size(); ++si) {
        const auto b = static_cast<std::size_t>(rig_.bones_out[si].bone);
        out[b] = bone_frame(static_cast<int>(si), pose, true);
        written[b] = 1;
    }
    for (const Carried &c : rig_.carried) {
        const auto by = static_cast<std::size_t>(rig_.bones_out[static_cast<std::size_t>(c.by)].bone);
        const auto b = static_cast<std::size_t>(c.bone);
        out[b] = compose(out[by], relative(pose[by], pose[b]));
        written[b] = 1;
    }
    x_.swap(solved_);
    times.reconstruct += us(t0, Clock::now());
}

namespace {

// the reference of the particles of the groups `which` takes: where `sim` has them, and their bones'
// frames as rebuilt there, in each reference bone's space
void keep_reference(Rig &rig, Simulation &sim, const std::vector<Frame> &pose, const std::vector<std::uint8_t> &which)
{
    std::vector<Frame> out(pose.size());
    std::vector<std::uint8_t> written(pose.size(), 0);
    sim.reconstruct(pose, out, written);
    for (std::size_t i = 0; i < rig.particles.size(); ++i) {
        Particle &p = rig.particles[i];
        if (!p.dynamic || !which[static_cast<std::size_t>(p.group)])
            continue;
        const GroupRig &g = rig.groups[static_cast<std::size_t>(p.group)];
        p.reference = apply_inverse(pose[static_cast<std::size_t>(g.reference_bone)], sim.positions()[i]);
        if (p.blend >= 0) {
            ReferenceBlend &b = rig.blends[static_cast<std::size_t>(p.blend)];
            for (int k = 0; k < b.count; ++k) {
                const auto j = static_cast<std::size_t>(k);
                const Frame &f = pose[static_cast<std::size_t>(b.bone[j])];
                b.point[j] = apply_inverse(f, sim.positions()[i]);
                b.frame[j] = relative(f, out[static_cast<std::size_t>(p.bone)]);
                if (k > 0) { // its distance from the ridden bone, from where it now hangs
                    const Vec3 end = apply(f, b.end[j]), ab = end - f.t;
                    const float ll = dot(ab, ab);
                    const float t = ll > 0.0F ? clampf(dot(sim.positions()[i] - f.t, ab) / ll, 0.0F, 1.0F) : 0.0F;
                    b.gap[j] = length(sim.positions()[i] - (f.t + ab * t));
                }
            }
        }
    }
    for (auto &sb : rig.bones_out) {
        const GroupRig &g = rig.groups[static_cast<std::size_t>(sb.group)];
        if (which[static_cast<std::size_t>(sb.group)])
            sb.reference =
                relative(pose[static_cast<std::size_t>(g.reference_bone)], out[static_cast<std::size_t>(sb.bone)]);
    }
}

} // namespace

void prepare_references(Rig &rig, Diagnostics *diagnostics)
{
    std::vector<Frame> pose(rig.skeleton.rest.size());
    for (std::size_t b = 0; b < pose.size(); ++b)
        pose[b] = frame_of(rig.skeleton.rest[b]);
    TickSettings s;
    s.dt = 1.0F / rig.config.rate;
    s.substeps = rig.config.substeps;
    s.iterations = std::max(2, rig.config.iterations);
    s.gravity = {0.0F, -9.81F, 0.0F}; // the rest pose stands y up
    s.environment = false;
    rig.collider_excluded.clear();
    rig.face_excluded.clear();
    // faces a collider passes through at rest, deeper than half its radius, are not the garment's outside
    // (a face across its inside): left out of that collider, not pushed out of it
    {
        std::vector<Vec3> at(rig.particles.size());
        for (std::size_t i = 0; i < rig.particles.size(); ++i)
            at[i] = apply(pose[static_cast<std::size_t>(rig.particles[i].bone)], rig.particles[i].offset);
        int across = 0;
        for (std::size_t c = 0; c < rig.colliders.size(); ++c) {
            const Collider &k = rig.colliders[c];
            const Vec3 a = apply(pose[static_cast<std::size_t>(k.bone)], k.offset);
            const Vec3 b =
                k.kind == ShapeKind::capsule ? apply(pose[static_cast<std::size_t>(k.end_bone)], k.end_offset) : a;
            for (std::size_t f = 0; f < rig.faces.size(); ++f) {
                const Face &face = rig.faces[f];
                if (!(rig.particles[static_cast<std::size_t>(face.a)].collide & k.tags))
                    continue;
                Vec3 on_core, on_face;
                bool crosses = false;
                const float d = closest_segment_triangle(
                    a, b, at[static_cast<std::size_t>(face.a)], at[static_cast<std::size_t>(face.b)],
                    at[static_cast<std::size_t>(face.c)], on_core, on_face, crosses);
                const float ab2 = dot(b - a, b - a);
                const float t = ab2 > 0.0F ? clampf(dot(on_core - a, b - a) / ab2, 0.0F, 1.0F) : 0.0F;
                const float r = k.radius + (k.end_radius - k.radius) * t;
                if (crosses || d < 0.5F * r) {
                    rig.face_excluded.push_back((static_cast<std::uint64_t>(c) << 32) | f);
                    ++across;
                }
            }
        }
        std::sort(rig.face_excluded.begin(), rig.face_excluded.end());
        if (across > 0 && diagnostics != nullptr) {
            char text[200];
            std::snprintf(text, sizeof(text),
                          "%d face-collider pairs pass through each other at rest (faces across the body's inside) "
                          "and are left out of each other",
                          across);
            diagnostics->push_back({false,
                                    rig.config.files.empty() ? std::string("secondary motion")
                                                             : rig.config.files.front().filename().string(),
                                    0, text});
        }
    }
    const std::vector<std::uint8_t> all(rig.groups.size(), 1);
    // every group's rest shape out of the body: contact relaxation, its links held
    {
        Simulation sim(rig);
        for (auto &d : sim.drives())
            d.guide = d.authority = 0.0F;
        sim.initialize(pose, s, 80);
        keep_reference(rig, sim, pose, all);
    }
    // the groups that hang, hung
    std::vector<std::uint8_t> settle(rig.groups.size(), 0);
    int steps = 0;
    for (std::size_t g = 0; g < rig.groups.size(); ++g)
        if (rig.groups[g].settle) {
            settle[g] = 1;
            steps = std::max(steps, rig.groups[g].settle_steps);
        }
    if (steps > 0) {
        Simulation sim(rig);
        for (std::size_t i = 0; i < rig.particles.size(); ++i) {
            const Particle &p = rig.particles[i];
            ParticleDrive &d = sim.drives()[i];
            if (settle[static_cast<std::size_t>(p.group)]) { // hanging freely, well damped
                d.params.v[static_cast<std::size_t>(Param::follow_frequency)] = 0.0F;
                d.params.v[static_cast<std::size_t>(Param::damping)] = maxf(6.0F, d.params.get(Param::damping));
                d.params.v[static_cast<std::size_t>(Param::swing_limit)] = kPi;
                d.params.v[static_cast<std::size_t>(Param::max_distance)] = 0.0F;
                d.guide = d.authority = 0.0F;
            } else { // held on its prepared shape, still something to hang against
                d.params.v[static_cast<std::size_t>(Param::follow_frequency)] = 30.0F;
                d.guide = d.authority = 0.0F;
            }
        }
        sim.initialize(pose, s, 0);
        for (int k = 0; k < steps; ++k)
            sim.tick(pose, pose, s);
        keep_reference(rig, sim, pose, settle);
    }
    // what is still inside a collider it collides with is left out of it
    std::vector<Vec3> at(rig.particles.size());
    for (std::size_t i = 0; i < rig.particles.size(); ++i) {
        const Particle &p = rig.particles[i];
        if (p.dynamic)
            at[i] = apply(pose[static_cast<std::size_t>(rig.groups[static_cast<std::size_t>(p.group)].reference_bone)],
                          p.reference);
    }
    for (std::size_t c = 0; c < rig.colliders.size(); ++c) {
        const Collider &k = rig.colliders[c];
        const Vec3 a = apply(pose[static_cast<std::size_t>(k.bone)], k.offset);
        const Vec3 b =
            k.kind == ShapeKind::capsule ? apply(pose[static_cast<std::size_t>(k.end_bone)], k.end_offset) : a;
        for (std::size_t i = 0; i < rig.particles.size(); ++i) {
            const Particle &p = rig.particles[i];
            if (!p.dynamic || !(p.collide & k.tags))
                continue;
            const Vec3 ab = b - a;
            const float ll = dot(ab, ab);
            const float t = ll > 0.0F ? clampf(dot(at[i] - a, ab) / ll, 0.0F, 1.0F) : 0.0F;
            const float r = k.radius + (k.end_radius - k.radius) * t;
            const float clearance =
                rig.point_clearance.empty() ? 0.0F : rig.point_clearance[c * rig.particles.size() + i];
            const float inside =
                pair_reach(r + p.thickness, clearance, 0.5F * r + p.thickness) - length(at[i] - (a + ab * t));
            if (inside <= 0.002F)
                continue;
            rig.collider_excluded.push_back((static_cast<std::uint64_t>(c) << 32) | i);
            if (diagnostics != nullptr) {
                char text[256];
                std::snprintf(text, sizeof(text),
                              "collider %s: control %s stays %.1f cm inside it at rest even pushed out (held by its "
                              "anchor) and is left out of it; shrink or move the collider to keep that contact",
                              k.name.c_str(), rig.skeleton.names[static_cast<std::size_t>(p.bone)].c_str(),
                              static_cast<double>(inside * 100.0F));
                diagnostics->push_back({false,
                                        rig.config.files.empty() ? std::string("secondary motion")
                                                                 : rig.config.files.front().filename().string(),
                                        0, text});
            }
        }
    }
    std::sort(rig.collider_excluded.begin(), rig.collider_excluded.end());
}

} // namespace khdays::secondary
