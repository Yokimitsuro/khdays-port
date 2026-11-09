// An actor's secondary motion over time (actor.h).

#include "actor.h"

#include <algorithm>
#include <chrono>

namespace khdays::secondary {

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t kProfileRing = 1024;
// a draw's time is a sum of frame lengths in floating point: a tick due a hair later still counts
constexpr double kTickSlack = 1e-3;
// ticks a fresh start or a camera cut settles the cloth in its pose for (Simulation::settle)
constexpr int kSettleTicks = 40;
// m: a place on the game model moving this far about its hips in one draw is the game's pose snapping (further than
// a leg swinging at 6 m/s moves in a 30 Hz draw)
constexpr float kGameSnap = 0.35F;
// s: a cut this soon after the last settle is not settled again (a pose snapping over several draws would settle
// on each, a stall)
constexpr double kSettleGap = 0.5;
// m: a body collider's end moving this far about the first group's reference bone across a pose cut starts the
// cloth afresh
constexpr float kPoseCutJump = 0.12F;

// settling: one substep a tick (it only has to come to rest, not follow motion)
TickSettings settling(TickSettings s)
{
    s.substeps = 1;
    return s;
}

bool matches(const ControlConfig &k, const AnimationState &a)
{
    bool named = false;
    for (const auto &n : k.animations)
        named = named || n == "*" || n == a.name;
    if (!named)
        return false;
    if (!k.slots.empty() && std::none_of(k.slots.begin(), k.slots.end(), [&](const std::pair<int, int> &r) {
            return a.slot >= r.first && a.slot <= r.second;
        }))
        return false;
    if (k.section >= 0.0F && std::fabs(k.section - a.section) > 1e-3F)
        return false;
    return true;
}

// a control's weight for one animation: in its time range, eased in and out over its blends
float envelope(const ControlConfig &k, const AnimationState &a)
{
    const float per_second = k.unit == TimeUnit::frames ? 30.0F : k.unit == TimeUnit::ticks ? 60.0F : 1.0F;
    float t = a.seconds;
    if (k.loop && a.length > 0.0F)
        t = std::fmod(maxf(0.0F, t), a.length);
    const float from = k.from / per_second, to = k.to / per_second;
    if (t < from || t > to)
        return 0.0F;
    float e = 1.0F;
    // eased in from a range that starts later than the animation; one that starts with it is not eased again each
    // time the animation starts over (a game animation looping, or played again): the control's own easing toward
    // what the animations ask covers its turning on. Eased here, a coat guided by its KH2 motion fell back to the
    // plain cloth at every loop of the game's run (0.6 s)
    if (k.blend_in > 0.0F && from > 0.0F)
        e = minf(e, (t - from) / k.blend_in);
    if (k.blend_out > 0.0F && std::isfinite(to))
        e = minf(e, (to - t) / k.blend_out);
    return maxf(0.0F, minf(1.0F, e));
}

} // namespace

float smooth_noise(float t, std::uint32_t seed)
{
    // per-instance phases from the seed (a small integer hash)
    std::uint32_t h = seed * 2654435761U + 0x9e3779b9U;
    float phase[3];
    for (float &p : phase) {
        h ^= h >> 15;
        h *= 2246822519U;
        h ^= h >> 13;
        p = static_cast<float>(h & 0xffffU) / 65536.0F * 2.0F * kPi;
    }
    return 0.55F * std::sin(2.0F * kPi * t + phase[0]) + 0.3F * std::sin(2.0F * kPi * 2.31F * t + phase[1]) +
           0.15F * std::sin(2.0F * kPi * 5.17F * t + phase[2]);
}

void Profile::add(float total)
{
    if (totals.size() < kProfileRing)
        totals.push_back(total);
    else
        totals[next] = total;
    next = (next + 1) % kProfileRing;
    ++updates;
}

float Profile::percentile(float p) const
{
    if (totals.empty())
        return 0.0F;
    std::vector<float> s(totals);
    const auto k = std::min(s.size() - 1, static_cast<std::size_t>(p * static_cast<float>(s.size())));
    std::nth_element(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(k), s.end());
    return s[k];
}

Actor::Actor(std::shared_ptr<const Rig> rig, std::uint32_t seed) : rig_(std::move(rig)), sim_(*rig_), seed_(seed)
{
    const std::size_t n = rig_->skeleton.names.size();
    last_pose_.assign(n, {});
    from_.assign(n, {});
    to_.assign(n, {});
    control_applied_.assign(rig_->controls.size(), 0.0F);
    full_.substeps = rig_->config.substeps;
    full_.iterations = rig_->config.iterations;
    profile_.totals.reserve(kProfileRing);
}

const LodConfig &Actor::lod(const ActorInput &in) const
{
    const LodConfig *best = &full_;
    for (const auto &l : rig_->config.lods)
        if (in.camera_distance >= l.distance)
            best = &l;
    return *best;
}

Vec3 Actor::wind_of(const Wind &w, int bone, const ActorInput &in, std::size_t group) const
{
    if (!w.given || w.speed <= 0.0F)
        return {};
    Vec3 dir = w.direction;
    if (w.space == WindSpace::actor) {
        dir = rotate(in.placement.q, dir);
    } else if (w.space == WindSpace::bone && bone >= 0) {
        dir = rotate((*in.pose)[static_cast<std::size_t>(bone)].q, dir);
    }
    dir = normalized(dir);
    const float t = static_cast<float>(sim_seconds_);
    const std::uint32_t s = seed_ + static_cast<std::uint32_t>(group) * 7919U;
    float speed = w.speed * (1.0F + w.gust * smooth_noise(t * w.frequency, s));
    Vec3 v = dir * maxf(0.0F, speed);
    if (w.turbulence > 0.0F) {
        const Vec3 side = perpendicular(dir), up = cross(dir, side);
        v += side * (w.speed * w.turbulence * smooth_noise(t * w.frequency * 3.1F, s + 1U));
        v += up * (w.speed * w.turbulence * 0.5F * smooth_noise(t * w.frequency * 2.3F, s + 2U));
    }
    return v;
}

// each particle's settings and authority this tick, from its group's and the controls that apply
void Actor::drive(const ActorInput &in, float dt)
{
    const Rig &rig = *rig_;
    std::vector<ParticleDrive> &d = sim_.drives();
    for (std::size_t i = 0; i < d.size(); ++i) {
        const ParamBlock &b = rig.params[static_cast<std::size_t>(rig.particles[i].params)];
        d[i].params = b;
        d[i].guide = b.mode == Mode::simulated ? 0.0F : 1.0F;
        d[i].authority = b.mode == Mode::authored ? 1.0F : 0.0F;
    }
    std::vector<Vec3> &winds = sim_.winds();
    for (std::size_t g = 0; g < rig.groups.size(); ++g)
        winds[g] = rig.groups[g].wind.given ? wind_of(rig.groups[g].wind, rig.groups[g].wind_bone, in, g)
                                            : wind_of(rig.config.wind, rig.wind_bone, in, g);
    // controls by priority, the higher applied over the lower
    std::vector<std::size_t> order(rig.controls.size());
    for (std::size_t k = 0; k < order.size(); ++k)
        order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return rig.controls[a].config->priority < rig.controls[b].config->priority;
    });
    for (const std::size_t k : order) {
        const ControlRig &c = rig.controls[k];
        const ControlConfig &cfg = *c.config;
        float want = 0.0F;
        for (const auto &a : in.animations)
            if (matches(cfg, a))
                want += maxf(0.0F, minf(1.0F, a.weight)) * envelope(cfg, a);
        want = minf(1.0F, want) * cfg.weight;
        // eased toward what the animations ask, so a cut between animations does not pop
        float &applied = control_applied_[k];
        const float rate_in = cfg.blend_in > 0.0F ? dt / cfg.blend_in : 1.0F;
        const float rate_out = cfg.blend_out > 0.0F ? dt / cfg.blend_out : 1.0F;
        applied = want > applied ? minf(want, applied + rate_in) : maxf(want, applied - rate_out);
        const float wgt = applied;
        if (wgt <= 0.0F)
            continue;
        float guide = 0.0F, authority = 0.0F;
        if (cfg.mode.given) {
            guide = cfg.mode.mode == Mode::simulated ? 0.0F : 1.0F;
            authority = cfg.mode.mode == Mode::authored ? 1.0F : 0.0F;
        }
        for (const int pi : c.particles) {
            ParticleDrive &pd = d[static_cast<std::size_t>(pi)];
            const float depth = rig.particles[static_cast<std::size_t>(pi)].depth;
            for (int p = 0; p < kParamCount; ++p) {
                const auto pp = static_cast<std::size_t>(p);
                if (!cfg.params.given[pp])
                    continue;
                const float v = cfg.params.at(static_cast<Param>(p), depth);
                pd.params.v[pp] += (v - pd.params.v[pp]) * wgt;
            }
            if (cfg.mode.given) {
                pd.guide += (guide - pd.guide) * wgt;
                pd.authority += (authority - pd.authority) * wgt;
            }
            if (cfg.policy.given && wgt >= 0.5F)
                pd.params.policy = cfg.policy.policy;
        }
        if (cfg.wind.given)
            for (const int g : c.groups) {
                const Vec3 v = wind_of(cfg.wind, c.wind_bone, in, static_cast<std::size_t>(g));
                winds[static_cast<std::size_t>(g)] += (v - winds[static_cast<std::size_t>(g)]) * wgt;
            }
    }
}

TickSettings Actor::tick_settings(const LodConfig &l, const ActorInput &in) const
{
    TickSettings s;
    s.dt = 1.0F / rig_->config.rate;
    s.substeps = l.substeps;
    s.iterations = l.iterations;
    s.gravity = rig_->config.gravity;
    s.environment = rig_->config.environment && l.environment && in.environment != nullptr;
    s.self_collision = l.self_collision;
    s.environment_thickness = rig_->config.environment_thickness;
    s.ground_only = rig_->config.environment_ground_only;
    s.env = in.environment;
    return s;
}

void Actor::start(const ActorInput &in)
{
    const TickSettings s = tick_settings(lod(in), in);
    drive(in, 0.0F);
    if (in.game_targets != nullptr && in.game_weight != nullptr) {
        last_game_ = *in.game_targets;
        sim_.set_game_targets(last_game_, last_game_, *in.game_weight);
        if (in.game_shell != nullptr)
            sim_.set_game_shell(*in.game_shell,
                                in.game_shell_open != nullptr ? *in.game_shell_open : std::vector<std::uint8_t>{});
    }
    sim_.initialize(*in.pose, s, rig_->config.settle_iterations);
    // the hanging groups hung as this first pose has them (they start in their rest shape on their bones: a bent
    // first pose flung the cords)
    sim_.settle(*in.pose, settling(s), kSettleTicks);
    settled_at_ = in.seconds;
    sim_.snap_to_game(*in.pose);
    for (const int b : rig_->needed)
        last_pose_[static_cast<std::size_t>(b)] = (*in.pose)[static_cast<std::size_t>(b)];
    last_seconds_ = sim_seconds_ = in.seconds;
    ticks_done_ =
        static_cast<std::int64_t>(std::floor(in.seconds * static_cast<double>(rig_->config.rate) + kTickSlack));
    epoch_ = in.epoch;
    started_ = true;
    ++generation_;
    ++profile_.restarts;
    profile_.startup_max = maxf(profile_.startup_max, static_cast<float>(sim_.times.startup));
}

void Actor::update(const ActorInput &in)
{
    const auto t0 = Clock::now();
    const Rig &rig = *rig_;
    const LodConfig &l = lod(in);
    if (l.frozen) { // drawn as animated; starts afresh when it comes back
        frozen_ = true;
        started_ = false;
        return;
    }
    frozen_ = false;
    const auto restart = [&] {
        cut_waiting_ = pose_cut_waiting_ = false;
        sim_.times.startup = 0.0;
        start(in);
        profile_.phases.startup += sim_.times.startup;
        profile_.add(static_cast<float>(std::chrono::duration<double, std::micro>(Clock::now() - t0).count()));
    };
    const double gap = in.seconds - last_seconds_;
    if (!started_ || in.epoch != epoch_ || gap < 0.0 || gap > rig.config.idle_reset) {
        restart();
        return;
    }
    cut_waiting_ = cut_waiting_ || in.cut;
    pose_cut_waiting_ = pose_cut_waiting_ || in.pose_cut;
    if (gap <= 0.0)
        return; // the same moment drawn again: nothing moves
    const bool view_cut = cut_waiting_, pose_cut = pose_cut_waiting_;
    cut_waiting_ = pose_cut_waiting_ = false;
    // a pose cut jumping the legs: carried on, the cloth would be left on the wrong side of a leg (it keeps the side
    // it came from); started afresh, the colliders sweep in from rest and drape it round them (Simulation::initialize)
    if (pose_cut && !rig.groups.empty()) {
        std::uint32_t touch = 0;
        for (const Particle &p : rig.particles)
            if (p.dynamic)
                touch |= p.collide;
        const auto rb = static_cast<std::size_t>(rig.groups.front().reference_bone);
        // a collider's end (its offset on its bone) about the reference bone, before and now
        const auto jumped = [&](int bone, Vec3 offset) {
            if (bone < 0)
                return false;
            const auto b = static_cast<std::size_t>(bone);
            return length(apply_inverse((*in.pose)[rb], apply((*in.pose)[b], offset)) -
                          apply_inverse(last_pose_[rb], apply(last_pose_[b], offset))) > kPoseCutJump;
        };
        for (const Collider &c : rig.colliders)
            if (c.rest_side && (c.tags & touch) != 0 &&
                (jumped(c.bone, c.offset) || (c.kind == ShapeKind::capsule && jumped(c.end_bone, c.end_offset)))) {
                restart();
                return;
            }
    }
    const float dt = 1.0F / rig.config.rate;
    // a teleport: every group's reference bone jumping further than a tick could carry it. Some alone (a chain on an
    // arm snapping to a new animation) are rebased by themselves below and the rest simulated: rebasing the whole
    // coat for them would skip the tick, and the legs jump to the new pose through it
    jumped_.assign(rig.groups.size(), 0);
    std::size_t jumps = 0;
    for (std::size_t g = 0; g < rig.groups.size(); ++g) {
        const auto b = static_cast<std::size_t>(rig.groups[g].reference_bone);
        const Frame &a = last_pose_[b], &c = (*in.pose)[b];
        if (length(c.t - a.t) > rig.config.teleport_distance ||
            angle_between(a.q, c.q) > rig.config.teleport_degrees * kDegrees) {
            jumped_[g] = 1;
            ++jumps;
        }
    }
    const bool jump = jumps > 0 && jumps == rig.groups.size();
    // the game's pose snapping (a cutscene's first frames): a place on the game model a particle takes jumping further
    // in one draw, about its group's hips, than a leg carries it; a new shot like a camera cut
    bool snap = false;
    if (in.game_targets != nullptr && in.game_weight != nullptr && last_game_.size() == in.game_targets->size())
        for (std::size_t i = 0; i < last_game_.size() && i < rig.particles.size() && !snap; ++i) {
            if ((*in.game_weight)[i] <= 0.0F || !sim_.follows_game(i))
                continue;
            const auto b =
                static_cast<std::size_t>(rig.groups[static_cast<std::size_t>(rig.particles[i].group)].reference_bone);
            snap = length(apply_inverse((*in.pose)[b], (*in.game_targets)[i]) -
                          apply_inverse(last_pose_[b], last_game_[i])) > kGameSnap;
        }
    const bool cut = view_cut || snap;
    if (jump || cut) {
        if (in.game_targets != nullptr && in.game_weight != nullptr) {
            last_game_ = *in.game_targets;
            sim_.set_game_targets(last_game_, last_game_, *in.game_weight);
            if (in.game_shell != nullptr)
                sim_.set_game_shell(*in.game_shell,
                                    in.game_shell_open != nullptr ? *in.game_shell_open : std::vector<std::uint8_t>{});
        }
        sim_.rebase(last_pose_, *in.pose);
        // a new shot: settled in its pose (a jump keeps the cloth's swing shape, carried), not again right after one
        if (cut && in.seconds - settled_at_ > kSettleGap) {
            settled_at_ = in.seconds;
            drive(in, 0.0F);
            sim_.settle(*in.pose, settling(tick_settings(l, in)), kSettleTicks);
        }
        sim_.snap_to_game(*in.pose);
        ++profile_.rebases;
        for (const int b : rig.needed)
            last_pose_[static_cast<std::size_t>(b)] = (*in.pose)[static_cast<std::size_t>(b)];
        last_seconds_ = sim_seconds_ = in.seconds;
        ticks_done_ =
            static_cast<std::int64_t>(std::floor(in.seconds * static_cast<double>(rig.config.rate) + kTickSlack));
        return;
    }
    if (jumps > 0) { // those groups carried to the new pose; their bones (the reference bone, those under it, their
                     // colliders') start the ticks there
        sim_.rebase(last_pose_, *in.pose, &jumped_);
        std::uint32_t touch = 0;
        for (const Particle &p : rig.particles)
            if (jumped_[static_cast<std::size_t>(p.group)])
                touch |= p.collide;
        const auto now = [&](int bone) {
            if (bone >= 0)
                last_pose_[static_cast<std::size_t>(bone)] = (*in.pose)[static_cast<std::size_t>(bone)];
        };
        for (const Collider &c : rig.colliders)
            if (c.tags & touch) {
                now(c.bone);
                now(c.end_bone);
            }
        for (const int bone : rig.needed) {
            bool under = false;
            for (int up = bone; up >= 0 && !under; up = rig.skeleton.parent[static_cast<std::size_t>(up)])
                for (std::size_t g = 0; g < rig.groups.size() && !under; ++g)
                    under = jumped_[g] && rig.groups[g].reference_bone == up;
            if (under)
                now(bone);
        }
        ++profile_.rebases;
    }
    // the ticks due on the game's clock (counted whole, so rounding never doubles one), at most
    // max_catchup (a longer stall is skipped, not replayed)
    const auto due =
        static_cast<std::int64_t>(std::floor(in.seconds * static_cast<double>(rig.config.rate) + kTickSlack));
    int ticks = static_cast<int>(std::max<std::int64_t>(0, due - ticks_done_));
    if (ticks > rig.config.max_catchup) {
        ticks_done_ = due - rig.config.max_catchup;
        ticks = rig.config.max_catchup;
    }
    sim_seconds_ = static_cast<double>(ticks_done_) / static_cast<double>(rig.config.rate);
    const TickSettings s = tick_settings(l, in);
    for (int k = 0; k < ticks; ++k) {
        // the poses at the tick's start and end, between the last pose received and this one
        const double a = (sim_seconds_ - last_seconds_) / gap, b = (sim_seconds_ + dt - last_seconds_) / gap;
        const auto ua = static_cast<float>(std::fmin(1.0, std::fmax(0.0, a)));
        const auto ub = static_cast<float>(std::fmin(1.0, std::fmax(0.0, b)));
        for (const int bone : rig.needed) {
            const auto i = static_cast<std::size_t>(bone);
            from_[i] = interpolate(last_pose_[i], (*in.pose)[i], ua);
            to_[i] = interpolate(last_pose_[i], (*in.pose)[i], ub);
        }
        if (in.game_targets != nullptr && in.game_weight != nullptr && last_game_.size() == in.game_targets->size()) {
            game_a_.resize(last_game_.size());
            game_b_.resize(last_game_.size());
            for (std::size_t i = 0; i < last_game_.size(); ++i) {
                game_a_[i] = lerp(last_game_[i], (*in.game_targets)[i], ua);
                game_b_[i] = lerp(last_game_[i], (*in.game_targets)[i], ub);
            }
            sim_.set_game_targets(game_a_, game_b_, *in.game_weight);
            if (in.game_shell != nullptr)
                sim_.set_game_shell(*in.game_shell,
                                    in.game_shell_open != nullptr ? *in.game_shell_open : std::vector<std::uint8_t>{});
        }
        drive(in, dt);
        sim_.tick(from_, to_, s);
        ++ticks_done_;
        sim_seconds_ = static_cast<double>(ticks_done_) / static_cast<double>(rig.config.rate);
        ++profile_.ticks;
    }
    for (const int bone : rig.needed)
        last_pose_[static_cast<std::size_t>(bone)] = (*in.pose)[static_cast<std::size_t>(bone)];
    if (in.game_targets != nullptr)
        last_game_ = *in.game_targets;
    last_seconds_ = in.seconds;
    PhaseTimes &p = profile_.phases;
    const PhaseTimes &t = sim_.times;
    p.pose += t.pose;
    p.solve += t.solve;
    p.model_collision += t.model_collision;
    p.environment_gather += t.environment_gather;
    p.environment_query += t.environment_query;
    p.self_collision += t.self_collision;
    p.reconstruct += t.reconstruct;
    sim_.times.clear();
    profile_.add(static_cast<float>(std::chrono::duration<double, std::micro>(Clock::now() - t0).count()));
}

void Actor::output(const std::vector<Frame> &pose, std::vector<Frame> &out, std::vector<std::uint8_t> &written)
{
    if (frozen_ || !started_)
        return;
    sim_.reconstruct(pose, out, written);
}

} // namespace khdays::secondary
