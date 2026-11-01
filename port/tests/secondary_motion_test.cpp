// Secondary motion (runtime/mods/secondary, docs/SECONDARY_MOTION.md): configuration, cooking, the
// solver, its clock and its contacts on small synthetic rigs; and the N_EX830 setup when
// KHDAYS_SECONDARY_ASSETS names the folder holding N_EX830.mdlx and its .secondary.txt.

#include "actor.h"

#include "khdays/assets/kh2_mdlx.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

using namespace khdays::secondary;

namespace {

int failures = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                              \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

// --- A synthetic skirt: a hip bone, `strips` strips of `levels` controls hanging around it --------------

// A bone pointing down (its x along -y), its y outward at angle `a` about the hip.
Mat4 down_frame(Vec3 at, float a)
{
    const Vec3 x{0, -1, 0}, y{std::cos(a), 0, std::sin(a)};
    const Vec3 z = cross(x, y);
    return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, at.x, at.y, at.z, 1};
}

struct Skirt {
    SkeletonDesc sk;
    MeshDesc mesh;
    int strips = 0, levels = 0;
    float link = 0.2F, radius = 0.15F;
    int bone(int s, int l) const
    {
        return 2 + s * levels + l;
    }
};

// bone 0 the root, 1 the hips (at y 1), then each strip's bones parent to child; the vertices at each
// control's tip, triangles joining neighbouring strips (`gap`: strips gap and gap + 1 not joined)
Skirt make_skirt(int strips, int levels, int gap = -1)
{
    Skirt s;
    s.strips = strips;
    s.levels = levels;
    s.sk.names = {"root", "hips"};
    s.sk.parent = {-1, 0};
    s.sk.rest = {kIdentity4, kIdentity4};
    s.sk.rest[1][13] = 1.0F;
    for (int k = 0; k < strips; ++k) {
        const float a = 2.0F * kPi * static_cast<float>(k) / static_cast<float>(strips);
        for (int l = 0; l < levels; ++l) {
            s.sk.names.push_back("s" + std::to_string(k) + "_" + std::to_string(l));
            s.sk.parent.push_back(l == 0 ? 1 : s.bone(k, l - 1));
            const Vec3 head{s.radius * std::cos(a), 1.0F - s.link * static_cast<float>(l), s.radius * std::sin(a)};
            s.sk.rest.push_back(down_frame(head, a));
        }
    }
    // a vertex at each control's tip, skinned to it alone
    const auto tip = [&](int k, int l) {
        const Mat4 &m = s.sk.rest[static_cast<std::size_t>(s.bone(k, l))];
        return point(m, {s.link, 0, 0});
    };
    std::vector<std::vector<std::uint32_t>> at(static_cast<std::size_t>(strips));
    for (int k = 0; k < strips; ++k)
        for (int l = 0; l < levels; ++l) {
            MeshDesc::Vertex v;
            v.position = tip(k, l);
            v.bones = {s.bone(k, l), -1, -1, -1};
            v.weights = {1, 0, 0, 0};
            at[static_cast<std::size_t>(k)].push_back(static_cast<std::uint32_t>(s.mesh.vertices.size()));
            s.mesh.vertices.push_back(v);
        }
    for (int k = 0; k < strips; ++k) {
        const int n = (k + 1) % strips;
        if (k == gap)
            continue;
        for (int l = 0; l + 1 < levels; ++l) {
            const auto a = at[static_cast<std::size_t>(k)][static_cast<std::size_t>(l)],
                       b = at[static_cast<std::size_t>(n)][static_cast<std::size_t>(l)],
                       c = at[static_cast<std::size_t>(k)][static_cast<std::size_t>(l + 1)],
                       d = at[static_cast<std::size_t>(n)][static_cast<std::size_t>(l + 1)];
            s.mesh.triangles.insert(s.mesh.triangles.end(), {a, b, c, b, d, c});
        }
    }
    return s;
}

std::string skirt_config(const Skirt &s, const std::string &extra, const std::string &group_extra = "")
{
    std::string t = "secondary_motion 1\nunits m\n" + extra + "group skirt surface\n  attach hips\n";
    for (int k = 0; k < s.strips; ++k) {
        t += "  strip";
        for (int l = 0; l < s.levels; ++l)
            t += " s" + std::to_string(k) + "_" + std::to_string(l);
        t += "\n";
    }
    t += "  topology mesh\n  follow_frequency 3\n  damping 2\n" + group_extra + "end\n";
    return t;
}

std::shared_ptr<Rig> cooked(const std::string &text, const SkeletonDesc &sk, const MeshDesc &mesh, Diagnostics &d)
{
    const Config c = parse_config(text, "test.secondary.txt", d);
    if (has_errors(d))
        return nullptr;
    auto rig = std::make_shared<Rig>(cook(c, sk, mesh, d));
    if (has_errors(d))
        return nullptr;
    prepare_references(*rig);
    return rig;
}

bool has_text(const Diagnostics &d, const std::string &what)
{
    for (const auto &x : d)
        if (format(x).find(what) != std::string::npos)
            return true;
    return false;
}

// The skirt's pose: the hips placed (and turned about y), every other bone carried rigidly as at rest.
std::vector<Frame> pose_of(const SkeletonDesc &sk, Vec3 at, float yaw = 0.0F)
{
    std::vector<Frame> p(sk.rest.size());
    Frame place;
    place.q = axis_angle({0, 1, 0}, yaw);
    place.t = at;
    for (std::size_t b = 0; b < p.size(); ++b)
        p[b] = compose(place, frame_of(sk.rest[b]));
    return p;
}

float max_offset(const Simulation &sim)
{
    float most = 0.0F;
    for (std::size_t i = 0; i < sim.positions().size(); ++i)
        if (sim.rig().particles[i].dynamic)
            most = maxf(most, length(sim.positions()[i] - sim.targets()[i]));
    return most;
}

// --- The tests ---------------------------------------------------------------------------------------------

void configuration()
{
    const Skirt s = make_skirt(6, 3);
    Diagnostics d;
    // a missing version, an unknown bone, a number out of range, a bone in two groups
    parse_config("units m\n", "a.txt", d);
    CHECK(has_text(d, "secondary_motion 1"));
    d.clear();
    std::string t = skirt_config(s, "", "  damping -1\n");
    parse_config(t, "b.txt", d);
    CHECK(has_text(d, "damping -1 is out of range"));
    d.clear();
    t = skirt_config(s, "") + "group again chain\n  attach hips\n  chain s0_0 s0_1\nend\n";
    const Config c = parse_config(t, "c.txt", d);
    CHECK(!has_errors(d));
    cook(c, s.sk, s.mesh, d);
    CHECK(has_text(d, "is a control of group skirt already"));
    d.clear();
    t = "secondary_motion 1\ngroup g chain\n  attach hips\n  chain nothing\nend\n";
    cook(parse_config(t, "d.txt", d), s.sk, s.mesh, d);
    CHECK(has_text(d, "there is no bone nothing"));
    // presets: extends, then the group, then a bone, each over the one before
    d.clear();
    t = "secondary_motion 1\npreset base\n  damping 3\n  mass 0.2\nend\npreset soft extends base\n  damping 5\nend\n" +
        skirt_config(s, "", "  bone s1_2 damping 9\n").substr(std::string("secondary_motion 1\nunits m\n").size());
    t.replace(t.find("group skirt surface"), 19, "group skirt surface preset soft");
    auto rig = cooked(t, s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (rig) {
        for (const auto &p : rig->particles) {
            if (!p.dynamic)
                continue;
            const ParamBlock &b = rig->params[static_cast<std::size_t>(p.params)];
            const bool special = rig->skeleton.names[static_cast<std::size_t>(p.bone)] == "s1_2";
            CHECK(std::fabs(b.get(Param::damping) - (special ? 9.0F : 2.0F)) <
                  1e-6F);                                        // the group's 2 over the preset's 5
            CHECK(std::fabs(b.get(Param::mass) - 0.2F) < 1e-6F); // from the base preset
        }
    }
    // a zero-length helper with no vertices as a control: refused with a way out
    d.clear();
    Skirt h = make_skirt(3, 2);
    h.sk.names.push_back("helper");
    h.sk.parent.push_back(h.bone(0, 1));
    h.sk.rest.push_back(h.sk.rest[static_cast<std::size_t>(h.bone(0, 1))]);
    t = "secondary_motion 1\ngroup g chain\n  attach hips\n  chain s0_0 s0_1 helper\nend\n";
    cook(parse_config(t, "e.txt", d), h.sk, h.mesh, d);
    CHECK(has_text(d, "zero-length helper"));
}

void reconstruction_at_rest()
{
    const Skirt s = make_skirt(8, 3);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, ""), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    CHECK(rig->faces.size() == 8U * 2U * 2U);
    Simulation sim(*rig);
    const auto pose = pose_of(s.sk, {1.0F, 0.0F, 2.0F}, 0.7F);
    TickSettings ts;
    ts.gravity = {};
    sim.initialize(pose, ts, 0);
    std::vector<Frame> out(pose), seen(pose);
    std::vector<std::uint8_t> written(pose.size(), 0);
    sim.reconstruct(pose, out, written);
    // with nothing moved, each rebuilt bone is its pose: translation = point - R S offset holds exactly
    float worst = 0.0F;
    for (std::size_t b = 0; b < pose.size(); ++b) {
        if (!written[b])
            continue;
        worst = maxf(worst, length(out[b].t - pose[b].t));
        worst = maxf(worst, angle_between(out[b].q, pose[b].q));
    }
    CHECK(worst < 1e-4F);
}

void slit_and_helpers()
{
    // strips 2 and 3 are not joined by the mesh (a slit), and their tips start 1 cm apart
    Skirt s = make_skirt(6, 3, 2);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, "", "  self_collision layer\n  thickness 0.02\n"), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    const auto linked = [&](int a, int b) {
        for (const auto &l : rig->distances) {
            const int x = rig->particles[static_cast<std::size_t>(l.a)].bone,
                      y = rig->particles[static_cast<std::size_t>(l.b)].bone;
            if (l.kind != Link::bend && ((x == a && y == b) || (x == b && y == a)))
                return true;
        }
        return false;
    };
    CHECK(!linked(s.bone(2, 1), s.bone(3, 1)));
    CHECK(linked(s.bone(1, 1), s.bone(2, 1)));
    // a disconnect line cuts a mesh link
    d.clear();
    auto cut = cooked(skirt_config(s, "", "  disconnect s0_1 s1_1\n"), s.sk, s.mesh, d);
    CHECK(cut != nullptr);
    if (cut)
        for (const auto &l : cut->distances) {
            const int x = cut->particles[static_cast<std::size_t>(l.a)].bone,
                      y = cut->particles[static_cast<std::size_t>(l.b)].bone;
            CHECK(!(l.kind != Link::bend &&
                    ((x == s.bone(0, 1) && y == s.bone(1, 1)) || (x == s.bone(1, 1) && y == s.bone(0, 1)))));
        }
    // the slit's two sides held at least where they were, never forced apart
    Simulation sim(*rig);
    TickSettings ts;
    const auto pose = pose_of(s.sk, {});
    sim.initialize(pose, ts, 10);
    for (int k = 0; k < 120; ++k)
        sim.tick(pose, pose, ts);
    CHECK(max_offset(sim) < 0.05F);
}

// The same motion drawn at 60 a second, at 60 with each draw repeated, and at 30: the state depends on
// the game's clock only.
void clock_independence()
{
    const Skirt s = make_skirt(6, 3);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, ""), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    const auto run = [&](float hz, int repeat, float seconds) {
        Actor a(rig, 7);
        std::vector<Frame> pose;
        for (int f = 0; f <= static_cast<int>(seconds * hz); ++f) {
            const float t = static_cast<float>(f) / hz;
            pose = pose_of(s.sk, {std::sin(t * 3.0F) * 0.5F, 0.0F, t * 2.0F}, t);
            ActorInput in;
            in.seconds = t;
            in.pose = &pose;
            for (int r = 0; r < repeat; ++r)
                a.update(in);
        }
        return a.simulation().positions();
    };
    const auto once = run(60.0F, 1, 2.0F), thrice = run(60.0F, 3, 2.0F), half = run(30.0F, 1, 2.0F);
    float same = 0.0F, close = 0.0F;
    for (std::size_t i = 0; i < once.size(); ++i) {
        same = maxf(same, length(once[i] - thrice[i]));
        close = maxf(close, length(once[i] - half[i]));
    }
    CHECK(same == 0.0F);  // drawing again changes nothing
    CHECK(close < 0.02F); // 30 draws a second: the same ticks, the pose interpolated between draws
}

void pause_hold_and_rate()
{
    const Skirt s = make_skirt(6, 3);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, ""), s.sk, s.mesh, d);
    if (!rig) {
        CHECK(false);
        return;
    }
    Actor a(rig, 1);
    auto pose = pose_of(s.sk, {});
    ActorInput in;
    in.pose = &pose;
    in.seconds = 0.0;
    a.update(in);
    // a jolt sideways, then the game paused: nothing moves however often it is drawn
    pose = pose_of(s.sk, {0.4F, 0.0F, 0.0F});
    in.seconds = 1.0 / 60.0;
    a.update(in);
    const auto paused = a.simulation().positions();
    for (int k = 0; k < 30; ++k)
        a.update(in);
    float moved = 0.0F;
    for (std::size_t i = 0; i < paused.size(); ++i)
        moved = maxf(moved, length(a.simulation().positions()[i] - paused[i]));
    CHECK(moved == 0.0F);
    CHECK(a.profile().ticks == 1U);
    // the animation held (the same pose) while the clock runs: the cloth swings on, then settles
    std::vector<Vec3> at2, at10, at110;
    for (int k = 2; k < 120; ++k) {
        in.seconds = k / 60.0;
        a.update(in);
        if (k == 2)
            at2 = a.simulation().positions();
        if (k == 10)
            at10 = a.simulation().positions();
        if (k == 110)
            at110 = a.simulation().positions();
    }
    float early = 0.0F, late = 0.0F;
    for (std::size_t i = 0; i < at2.size(); ++i) {
        early = maxf(early, length(at10[i] - at2[i]));
        late = maxf(late, length(a.simulation().positions()[i] - at110[i]));
    }
    std::printf("held: moved %.4f m early, %.5f m late\n", static_cast<double>(early), static_cast<double>(late));
    CHECK(early > 0.002F); // not frozen by the held frame
    CHECK(late < 0.005F);  // and settling (a 28 cm swing down to millimetres)
    // the clock at half rate: half the ticks
    Actor b(rig, 1);
    in.seconds = 0.0;
    b.update(in);
    for (int k = 1; k <= 60; ++k) {
        in.seconds = k / 120.0;
        b.update(in);
    }
    CHECK(b.profile().ticks == 30U);
}

void spawn_teleport_identity()
{
    const Skirt s = make_skirt(6, 3);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, "teleport 1.5 100\n"), s.sk, s.mesh, d);
    if (!rig) {
        CHECK(false);
        return;
    }
    // started while moving at 5 m/s: no jerk, it moves with its target from the start
    Actor a(rig, 3);
    std::vector<Frame> pose;
    ActorInput in;
    in.pose = &pose;
    float worst = 0.0F;
    for (int f = 0; f < 30; ++f) {
        in.seconds = f / 60.0;
        pose = pose_of(s.sk, {0, 0, 5.0F * static_cast<float>(f) / 60.0F});
        a.update(in);
        worst = maxf(worst, max_offset(a.simulation()));
    }
    CHECK(worst < 0.05F);
    // a teleport: rebased, its shape kept, not restarted
    const auto generation = a.generation();
    in.seconds = 31 / 60.0;
    pose = pose_of(s.sk, {20.0F, 0.0F, 0.0F}, 2.0F);
    a.update(in);
    CHECK(a.profile().rebases == 1U);
    CHECK(a.generation() == generation);
    CHECK(max_offset(a.simulation()) < 0.05F);
    // a new epoch (a save state) or a long absence: started afresh
    in.epoch = 9;
    in.seconds = 32 / 60.0;
    a.update(in);
    CHECK(a.generation() == generation + 1);
    in.seconds = 10.0;
    a.update(in);
    CHECK(a.generation() == generation + 2);
}

void partial_jump()
{
    // a chain on an arm snapping 150 degrees in one draw: that group rebased alone, the skirt simulated through
    // the draw exactly as without the snap (a whole rebase skipped its tick: legs jumped through a coat)
    Skirt s = make_skirt(6, 3);
    const int arm = static_cast<int>(s.sk.names.size());
    s.sk.names.insert(s.sk.names.end(), {"arm", "t0", "t1"});
    s.sk.parent.insert(s.sk.parent.end(), {1, arm, arm + 1});
    for (int k = 0; k < 3; ++k)
        s.sk.rest.push_back(down_frame({0.5F, 1.2F - 0.2F * static_cast<float>(k), 0.0F}, 0.0F));
    Diagnostics d;
    auto rig = cooked(skirt_config(s, "teleport 1.5 100\n") +
                          "group tail chain\n  attach arm\n  chain t0 t1\n  offset t1 0.2 0 0\nend\n",
                      s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    Actor a(rig, 3), b(rig, 3);
    std::vector<Frame> pa, pb;
    ActorInput ia, ib;
    ia.pose = &pa;
    ib.pose = &pb;
    for (int f = 0; f <= 30; ++f) {
        ia.seconds = ib.seconds = f / 60.0;
        pa = pb = pose_of(s.sk, {0, 0, 5.0F * static_cast<float>(f) / 60.0F});
        if (f == 30) { // a's arm and the chain under it turned about z at the arm's head
            const Vec3 pivot = pa[static_cast<std::size_t>(arm)].t;
            const Quat q = axis_angle({0, 0, 1}, 150.0F * kDegrees);
            for (int k = arm; k < arm + 3; ++k) {
                Frame &f2 = pa[static_cast<std::size_t>(k)];
                f2.t = pivot + rotate(q, f2.t - pivot);
                f2.q = normalized(q * f2.q);
            }
        }
        a.update(ia);
        b.update(ib);
    }
    float apart = 0.0F;
    for (std::size_t i = 0; i < rig->particles.size(); ++i)
        if (rig->particles[i].dynamic && rig->particles[i].group == 0)
            apart = maxf(apart, length(a.simulation().positions()[i] - b.simulation().positions()[i]));
    std::printf("partial jump: %llu rebases, skirt apart %.7f m, worst off its target %.4f m\n",
                static_cast<unsigned long long>(a.profile().rebases), static_cast<double>(apart),
                static_cast<double>(max_offset(a.simulation())));
    CHECK(a.profile().rebases == 1U);
    CHECK(apart < 1e-5F);
    CHECK(max_offset(a.simulation()) < 0.05F); // the chain carried with its arm
}

void authored_and_release()
{
    const Skirt s = make_skirt(6, 3);
    Diagnostics d;
    // an animation control: authored while `hold` plays, released over 0.25 s
    const std::string text = skirt_config(s, "") +
                             "control own\n  animation hold\n  groups skirt\n  mode authored\n  authored_policy exact\n"
                             "  blend 0 0.25\nend\n";
    auto rig = cooked(text, s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    Actor a(rig, 5);
    std::vector<Frame> pose, out;
    std::vector<std::uint8_t> written;
    ActorInput in;
    in.pose = &pose;
    Vec3 last_tip;
    float jump = 0.0F;
    const std::size_t tip = static_cast<std::size_t>(s.bone(0, 2));
    for (int f = 0; f < 90; ++f) {
        const float t = static_cast<float>(f) / 60.0F;
        pose = pose_of(s.sk, {std::sin(t * 4.0F) * 0.6F, 0.0F, 0.0F});
        // the authored motion swings the strips' ends out, with a scale (exact keeps it)
        for (int k = 0; k < s.strips; ++k) {
            Frame &e = pose[static_cast<std::size_t>(s.bone(k, 2))];
            e.q = normalized(axis_angle({0, 0, 1}, 0.5F * std::sin(t * 6.0F)) * e.q);
            e.s = {1.2F, 1.0F, 1.0F};
        }
        in.seconds = t;
        in.animations = {{f < 45 ? "hold" : "walk", -1, -1.0F, t, 0.0F, 1.0F}};
        a.update(in);
        out = pose;
        written.assign(pose.size(), 0);
        a.output(pose, out, written);
        if (f < 45) { // exact: the animation's own matrices, scale and all
            for (int k = 0; k < s.strips; ++k)
                for (int l = 0; l < s.levels; ++l) {
                    const auto b = static_cast<std::size_t>(s.bone(k, l));
                    const float qd = std::fabs(out[b].q.x - pose[b].q.x) + std::fabs(out[b].q.y - pose[b].q.y) +
                                     std::fabs(out[b].q.z - pose[b].q.z) + std::fabs(out[b].q.w - pose[b].q.w);
                    const bool same = length(out[b].t - pose[b].t) < 1e-6F && qd < 1e-6F &&
                                      std::fabs(out[b].s.x - pose[b].s.x) < 1e-6F;
                    if (!same)
                        std::fprintf(stderr, "authored f%d bone %zu written %d dt %g dq %g ds %g\n", f, b, written[b],
                                     static_cast<double>(length(out[b].t - pose[b].t)),
                                     static_cast<double>(angle_between(out[b].q, pose[b].q)),
                                     static_cast<double>(out[b].s.x - pose[b].s.x));
                    CHECK(same);
                }
        }
        const Vec3 p = apply(out[tip], {0.2F, 0.0F, 0.0F});
        if (f > 0)
            jump = maxf(jump, length(p - last_tip));
        last_tip = p;
    }
    // the release continues from where the authored motion left it: no step bigger than the motion's own
    CHECK(jump < 0.08F);
}

// A guided coat whose animation stretches its bones (KH2's battle coats scale theirs several times
// over): the links, strain limits and tethers take the animation's lengths, so it is followed
void guided_stretch()
{
    const Skirt s = make_skirt(6, 3);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, "", "  mode guided\n  max_strain 0.05\n  tether_slack 0.02\n"), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    Actor a(rig, 9);
    std::vector<Frame> pose, out;
    std::vector<std::uint8_t> written;
    ActorInput in;
    in.pose = &pose;
    float off = 0.0F;
    for (int f = 0; f < 120; ++f) {
        pose = pose_of(s.sk, {});
        // each strip twice its length: heads moved down, the bones scaled along their length
        for (int k = 0; k < s.strips; ++k)
            for (int l = 0; l < s.levels; ++l) {
                Frame &e = pose[static_cast<std::size_t>(s.bone(k, l))];
                e.t.y = 1.0F - 2.0F * s.link * static_cast<float>(l);
                e.s = {2.0F, 1.0F, 1.0F};
            }
        in.seconds = static_cast<double>(f) / 60.0;
        a.update(in);
        out = pose;
        written.assign(pose.size(), 0);
        a.output(pose, out, written);
        if (f >= 60)
            for (int k = 0; k < s.strips; ++k) {
                const auto b = static_cast<std::size_t>(s.bone(k, s.levels - 1));
                off = maxf(off, length(apply(out[b], {s.link, 0, 0}) - apply(pose[b], {s.link, 0, 0})));
            }
    }
    if (off >= 0.03F)
        std::fprintf(stderr, "guided stretch: hem %.1f cm from the animation\n", static_cast<double>(off * 100.0F));
    CHECK(off < 0.03F); // held at rest lengths the hem would stay 0.6 m short
}

// `cutscene_sim`: about places wider and shorter than the rest shape (a game model's coat), with gravity and a
// collider through them its group does not collide with: never inside the shell over the places, its links at
// the places' lengths, untouched by that collider; its rows (at the places' lengths) stiff at the top and free at
// the hem, as kh2_coat's
void cutscene_sim_shell()
{
    Skirt s = make_skirt(8, 3);
    Diagnostics d;
    auto rig =
        cooked(skirt_config(s,
                            "collider core capsule hips length 0.6 radius 0.3 tags hips\n"
                            "collider shin capsule hips length 0.05 radius 0.01 tags calves\n",
                            "  collide calves\n  cutscene_sim\n"
                            "  row_compliance 0.00002 hem 0.05 curve 1.5\n  row_max_strain 0.04 hem 4 curve 1.5\n"),
               s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    const auto particle_of = [&](int bone) {
        for (std::size_t i = 0; i < rig->particles.size(); ++i)
            if (rig->particles[i].dynamic && rig->particles[i].bone == bone)
                return static_cast<int>(i);
        return -1;
    };
    // the places: flared out like a hem (gravity pulls the strips in against them), 0.75 of the rest spacing down
    constexpr float kOut = 1.3F, kFlare = 0.6F, kDown = 0.15F;
    const auto out_at = [&](int l) { return s.radius * kOut * (1.0F + kFlare * static_cast<float>(l)); };
    std::vector<Vec3> places(rig->particles.size());
    std::vector<float> weight(rig->particles.size(), 0.0F);
    std::vector<int> level(rig->particles.size(), 0);
    std::vector<std::vector<int>> columns;
    for (int k = 0; k < s.strips; ++k) {
        const float a = 2.0F * kPi * static_cast<float>(k) / static_cast<float>(s.strips);
        std::vector<int> col;
        for (int l = 0; l < s.levels; ++l) {
            const int i = particle_of(s.bone(k, l));
            CHECK(i >= 0);
            if (i < 0)
                return;
            if (l == 0)
                col.push_back(rig->particles[static_cast<std::size_t>(i)].anchor);
            const auto ui = static_cast<std::size_t>(i);
            places[ui] = {out_at(l) * std::cos(a), 1.0F - kDown * static_cast<float>(l + 1), out_at(l) * std::sin(a)};
            weight[ui] = 1.0F;
            level[ui] = l;
            col.push_back(i);
        }
        columns.push_back(col);
    }
    std::vector<std::array<int, 3>> shell;
    for (std::size_t c = 0; c < columns.size(); ++c) {
        const auto &l = columns[c], &r = columns[(c + 1) % columns.size()];
        for (std::size_t q = 0; q + 1 < l.size(); ++q) {
            shell.push_back({l[q], l[q + 1], r[q]});
            shell.push_back({r[q], l[q + 1], r[q + 1]});
        }
    }
    // wound outward
    const auto at = [&](int p) {
        const auto i = static_cast<std::size_t>(p);
        return weight[i] > 0.0F
                   ? places[i]
                   : point(s.sk.rest[static_cast<std::size_t>(rig->particles[i].bone)], rig->particles[i].offset);
    };
    const Vec3 n0 = cross(at(shell[0][1]) - at(shell[0][0]), at(shell[0][2]) - at(shell[0][0]));
    const Vec3 c0 = (at(shell[0][0]) + at(shell[0][1]) + at(shell[0][2])) * (1.0F / 3.0F);
    if (dot(n0, Vec3{c0.x, 0.0F, c0.z}) < 0.0F)
        for (auto &f : shell)
            std::swap(f[1], f[2]);
    Simulation sim(*rig);
    TickSettings ts;
    const auto pose = pose_of(s.sk, {});
    sim.set_game_targets(places, places, weight);
    sim.set_game_shell(shell, {});
    sim.initialize(pose, ts, 10);
    float inside = 0.0F, off = 0.0F, link = 0.0F;
    for (int t = 0; t < 180; ++t) {
        sim.tick(pose, pose, ts);
        for (std::size_t i = 0; i < places.size(); ++i) {
            if (weight[i] <= 0.0F)
                continue;
            const Vec3 x = sim.positions()[i];
            inside = maxf(inside, out_at(level[i]) - std::sqrt(x.x * x.x + x.z * x.z));
            off = maxf(off, length(x - places[i]));
        }
    }
    for (int k = 0; k < s.strips; ++k)
        for (int l = 0; l + 1 < s.levels; ++l) {
            const auto a = static_cast<std::size_t>(particle_of(s.bone(k, l))),
                       b = static_cast<std::size_t>(particle_of(s.bone(k, l + 1)));
            link =
                maxf(link, std::fabs(length(sim.positions()[b] - sim.positions()[a]) - length(places[b] - places[a])));
        }
    std::printf("cutscene_sim: %.4f m inside the shell, %.3f m from its places, links %.4f m off theirs\n", inside, off,
                link);
    CHECK(inside < 0.01F); // held out on the flare (a tangent plane per point: its corners' circle cut a little)
    CHECK(off < 0.04F);    // hanging about its places, the collider (radius 0.3) not pushing it out
    CHECK(link < 0.02F);   // the places' spacing, not the rest shape's 0.2
    // spun about its hips (the places turning with them): swung out off the shell, never in
    float out = 0.0F;
    inside = 0.0F;
    std::vector<Vec3> was = places, now = places;
    auto from = pose;
    for (int t = 1; t <= 60; ++t) {
        const float yaw = 0.12F * static_cast<float>(t);
        const auto to = pose_of(s.sk, {}, yaw);
        for (std::size_t i = 0; i < places.size(); ++i)
            now[i] = rotate(axis_angle({0, 1, 0}, yaw), places[i]);
        sim.set_game_targets(was, now, weight);
        sim.tick(from, to, ts);
        for (std::size_t i = 0; i < places.size(); ++i) {
            if (weight[i] <= 0.0F)
                continue;
            const Vec3 x = sim.positions()[i];
            const float r = std::sqrt(x.x * x.x + x.z * x.z);
            inside = maxf(inside, out_at(level[i]) - r);
            out = maxf(out, r - out_at(level[i]));
        }
        was = now;
        from = to;
    }
    std::printf("cutscene_sim spun: %.3f m out off the shell, %.4f m inside it\n", out, inside);
    CHECK(out > 0.015F);   // flung out (its rows held: 2 cm; free, 3)
    CHECK(inside < 0.01F); // never in
}

// `shell_push`: a shin standing through the shell's places, the coat about them pushed out of it (the places, not
// the cloth: nothing touches the cloth), not when the group does not name it
void shell_push_shin()
{
    for (const bool push : {false, true}) {
        Skirt s = make_skirt(8, 3);
        s.sk.names.push_back("shin");
        s.sk.parent.push_back(1);
        s.sk.rest.push_back(down_frame({0.2F, 1.0F, 0.0F}, 0.0F)); // down through strip 0's places
        Diagnostics d;
        auto rig = cooked(skirt_config(s, "collider shin capsule shin length 0.5 radius 0.06 tags calves\n",
                                       std::string("  collide none\n  cutscene_sim\n  thickness 0.01\n") +
                                           (push ? "  shell_push calves\n" : "")),
                          s.sk, s.mesh, d);
        CHECK(rig != nullptr);
        if (!rig)
            return;
        std::vector<Vec3> places(rig->particles.size());
        std::vector<float> weight(rig->particles.size(), 0.0F);
        std::vector<std::vector<int>> columns;
        for (int k = 0; k < s.strips; ++k) {
            const float a = 2.0F * kPi * static_cast<float>(k) / static_cast<float>(s.strips);
            std::vector<int> col;
            for (int l = 0; l < s.levels; ++l) {
                int i = -1;
                for (std::size_t j = 0; j < rig->particles.size(); ++j)
                    if (rig->particles[j].dynamic && rig->particles[j].bone == s.bone(k, l))
                        i = static_cast<int>(j);
                CHECK(i >= 0);
                if (i < 0)
                    return;
                if (l == 0)
                    col.push_back(rig->particles[static_cast<std::size_t>(i)].anchor);
                places[static_cast<std::size_t>(i)] = {0.2F * std::cos(a), 0.85F - 0.15F * static_cast<float>(l),
                                                       0.2F * std::sin(a)};
                weight[static_cast<std::size_t>(i)] = 1.0F;
                col.push_back(i);
            }
            columns.push_back(col);
        }
        std::vector<std::array<int, 3>> shell;
        for (std::size_t c = 0; c < columns.size(); ++c) {
            const auto &l = columns[c], &r = columns[(c + 1) % columns.size()];
            for (std::size_t q = 0; q + 1 < l.size(); ++q) {
                shell.push_back({l[q], r[q], l[q + 1]}); // wound outward (a ring turning +x to +z, rows down)
                shell.push_back({r[q], r[q + 1], l[q + 1]});
            }
        }
        Simulation sim(*rig);
        TickSettings ts;
        const auto pose = pose_of(s.sk, {});
        sim.set_game_targets(places, places, weight);
        sim.set_game_shell(shell, {});
        sim.initialize(pose, ts, 10);
        for (int t = 0; t < 120; ++t)
            sim.tick(pose, pose, ts);
        // the nearest a place-held point comes to the shin's core
        float nearest = 1e9F;
        for (std::size_t i = 0; i < places.size(); ++i) {
            if (weight[i] <= 0.0F)
                continue;
            const Vec3 x = sim.positions()[i];
            nearest = minf(nearest, std::sqrt((x.x - 0.2F) * (x.x - 0.2F) + x.z * x.z));
        }
        std::printf("shell_push %s: the coat %.3f m from the shin's core (radius 0.06)\n", push ? "on" : "off",
                    nearest);
        CHECK(push ? nearest > 0.06F : nearest < 0.03F);
    }
}

// `cutscene_sim`: a leg kicking one strip's places out and back with the hips still: the shell pushes the cloth
// (contact, as a leg pushes the gameplay coat) without throwing it, and its neighbours come along a little
void cutscene_sim_kick()
{
    Skirt s = make_skirt(8, 3);
    Diagnostics d;
    auto rig =
        cooked(skirt_config(s, "",
                            "  collide none\n  cutscene_sim\n  follow_frequency 3 hem 1.2 curve 1.5\n"
                            "  follow_damping 0.75\n  damping 3\n  inertia 0.8\n"
                            "  row_compliance 0.00002 hem 0.05 curve 1.5\n  row_max_strain 0.04 hem 4 curve 1.5\n"),
               s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    std::vector<Vec3> places(rig->particles.size());
    std::vector<float> weight(rig->particles.size(), 0.0F);
    std::vector<int> strip(rig->particles.size(), -1), level(rig->particles.size(), 0);
    std::vector<std::vector<int>> columns;
    for (int k = 0; k < s.strips; ++k) {
        const float a = 2.0F * kPi * static_cast<float>(k) / static_cast<float>(s.strips);
        std::vector<int> col;
        for (int l = 0; l < s.levels; ++l) {
            int i = -1;
            for (std::size_t j = 0; j < rig->particles.size(); ++j)
                if (rig->particles[j].dynamic && rig->particles[j].bone == s.bone(k, l))
                    i = static_cast<int>(j);
            CHECK(i >= 0);
            if (i < 0)
                return;
            if (l == 0)
                col.push_back(rig->particles[static_cast<std::size_t>(i)].anchor);
            const auto ui = static_cast<std::size_t>(i);
            places[ui] = {0.2F * std::cos(a), 0.85F - 0.15F * static_cast<float>(l), 0.2F * std::sin(a)};
            weight[ui] = 1.0F;
            strip[ui] = k;
            level[ui] = l;
            col.push_back(i);
        }
        columns.push_back(col);
    }
    std::vector<std::array<int, 3>> shell;
    for (std::size_t c = 0; c < columns.size(); ++c) {
        const auto &l = columns[c], &r = columns[(c + 1) % columns.size()];
        for (std::size_t q = 0; q + 1 < l.size(); ++q) {
            shell.push_back({l[q], r[q], l[q + 1]});
            shell.push_back({r[q], r[q + 1], l[q + 1]});
        }
    }
    Simulation sim(*rig);
    TickSettings ts;
    const auto pose = pose_of(s.sk, {});
    sim.set_game_targets(places, places, weight);
    sim.set_game_shell(shell, {});
    sim.initialize(pose, ts, 10);
    for (int t = 0; t < 60; ++t)
        sim.tick(pose, pose, ts);
    // strip 0 kicked out along +x, 0.2 m at its hem, in 6 ticks and back in 6
    const auto kicked = [&](float amount) {
        std::vector<Vec3> now = places;
        for (std::size_t i = 0; i < now.size(); ++i)
            if (strip[i] == 0)
                now[i].x += amount * static_cast<float>(level[i] + 1) / static_cast<float>(s.levels);
        return now;
    };
    std::vector<Vec3> was = places;
    float thrown = 0.0F, neighbour = 0.0F;
    for (int t = 1; t <= 72; ++t) {
        const float k = t <= 6 ? static_cast<float>(t) / 6.0F : t <= 12 ? static_cast<float>(12 - t) / 6.0F : 0.0F;
        const std::vector<Vec3> now = kicked(0.2F * k);
        sim.set_game_targets(was, now, weight);
        sim.tick(pose, pose, ts);
        for (std::size_t i = 0; i < now.size(); ++i) {
            if (level[i] != s.levels - 1)
                continue;
            const Vec3 x = sim.positions()[i];
            if (strip[i] == 0 && t > 12)
                thrown = maxf(thrown, x.x - now[i].x);
            if ((strip[i] == 1 || strip[i] == s.strips - 1) && t <= 12)
                neighbour = maxf(neighbour, x.x - now[i].x);
        }
        was = now;
    }
    std::printf("cutscene_sim kick: hem thrown %.3f m past its place after the kick, neighbours carried %.3f m\n",
                thrown, neighbour);
    CHECK(thrown < 0.035F);   // in the shell's frame and free rows: 4.9 cm
    CHECK(neighbour > 0.02F); // free rows: 0.7 cm
}

// A `clip_world` volume over the particles' places `rest`: closed (each edge on two faces, once each way) and wound
// outward (its signed volume positive); that volume (m^3, model units cubed)
float clip_volume_check(const ClipVolume &cv, const std::vector<Vec3> &rest)
{
    CHECK(!cv.faces.empty());
    CHECK(cv.open_edges == 0U);
    std::vector<Vec3> at;
    for (const int p : cv.particles)
        at.push_back(rest[static_cast<std::size_t>(p)]);
    for (const auto *ring : {&cv.top, &cv.bottom}) {
        Vec3 middle{};
        for (const int k : *ring)
            middle += at[static_cast<std::size_t>(k)] * (1.0F / static_cast<float>(ring->size()));
        at.push_back(middle);
    }
    float volume = 0.0F;
    for (const auto &f : cv.faces)
        volume += dot(at[static_cast<std::size_t>(f[0])],
                      cross(at[static_cast<std::size_t>(f[1])], at[static_cast<std::size_t>(f[2])])) /
                  6.0F;
    CHECK(volume > 0.0F);
    return volume;
}

// `clip_world`: a skirt's volume, its slit (no faces between two strips) closed across, wound outward
void clip_world_volume()
{
    Skirt s = make_skirt(8, 3, 3);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, "", "  clip_world inset 0.02\n"), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    std::vector<Vec3> rest;
    for (const auto &p : rig->particles)
        rest.push_back(point(s.sk.rest[static_cast<std::size_t>(p.bone)], p.offset));
    const auto volumes = clip_volumes_of(*rig, rest);
    CHECK(volumes.size() == 1U);
    if (volumes.size() != 1U)
        return;
    const float v = clip_volume_check(volumes[0], rest);
    // a cylinder of the skirt's radius from its fixed points to its hem, give or take its facets
    std::printf("clip_world: %zu columns, %zu faces, %.4f m^3, inset %.3f m\n", volumes[0].columns,
                volumes[0].faces.size(), v, volumes[0].inset);
    CHECK(volumes[0].columns == 8U);
    CHECK(std::fabs(volumes[0].inset - 0.02F) < 1e-6F);
}

// `turn`: a strip turned out at rest, by an angle or just clear of a collider; the drawn rest follows it
void strip_turn()
{
    const Skirt s = make_skirt(8, 3);
    const std::string post = "collider post sphere hips offset 0.15 -0.3 0 radius 0.03\n";
    Diagnostics d;
    auto rig = cooked(skirt_config(s, post, "  turn s0_0 clear post\n"), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    // the turned strip's segments (its head through each control) clear the post; the next strip as it was
    const Vec3 centre{0.15F, 0.7F, 0.0F};
    Vec3 from = point(s.sk.rest[static_cast<std::size_t>(s.bone(0, 0))], {0, 0, 0});
    float least = 1.0F;
    for (int l = 0; l < 3; ++l) {
        const auto b = static_cast<std::size_t>(s.bone(0, l));
        const Vec3 x = point(rig->skeleton.rest[b], {s.link, 0, 0});
        float u, v;
        closest_segments(from, x, centre, centre, u, v);
        least = std::fmin(least, length(lerp(from, x, u) - centre));
        from = x;
    }
    const float turned = angle_between(frame_of(rig->skeleton.rest[static_cast<std::size_t>(s.bone(0, 0))]).q,
                                       frame_of(s.sk.rest[static_cast<std::size_t>(s.bone(0, 0))]).q);
    std::printf("turn: %.2f degrees, %.4f m from the post's centre\n", turned / kDegrees, least);
    CHECK(turned > 1.0F * kDegrees && turned < 30.0F * kDegrees);
    CHECK(least >= 0.03F + rig->particles[0].thickness);
    CHECK(from.x > s.radius); // out, away from the hips
    CHECK(rig->skeleton.rest[static_cast<std::size_t>(s.bone(1, 2))] ==
          s.sk.rest[static_cast<std::size_t>(s.bone(1, 2))]);
    // rebuilt at rest, the strip's bones are its turned rest
    Simulation sim(*rig);
    const auto pose = pose_of(s.sk, {});
    TickSettings ts;
    ts.gravity = {};
    sim.initialize(pose, ts, 0);
    std::vector<Frame> out(pose);
    std::vector<std::uint8_t> written(pose.size(), 0);
    sim.reconstruct(pose, out, written);
    const auto b = static_cast<std::size_t>(s.bone(0, 1));
    CHECK(written[b] && angle_between(out[b].q, frame_of(rig->skeleton.rest[b]).q) < 1e-3F);
    // by an angle; a bone outside the group's strips refused
    d.clear();
    rig = cooked(skirt_config(s, "", "  turn s0_0 20\n"), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (rig)
        CHECK(std::fabs(angle_between(frame_of(rig->skeleton.rest[static_cast<std::size_t>(s.bone(0, 0))]).q,
                                      frame_of(s.sk.rest[static_cast<std::size_t>(s.bone(0, 0))]).q) -
                        20.0F * kDegrees) < 1e-3F);
    d.clear();
    CHECK(cooked(skirt_config(s, "", "  turn hips 20\n"), s.sk, s.mesh, d) == nullptr);
    CHECK(has_text(d, "not in a strip"));
}

void outward_axis()
{
    // a post through strip 0's wall at rest: `outward axis` pushes that strip out alone (no links between
    // its strips), the strip opposite it as without the post
    const Skirt s = make_skirt(8, 3);
    const std::string post = "collider post sphere hips offset 0.14 -0.3 0 radius 0.03\n";
    Diagnostics d;
    const auto rig = cooked(skirt_config(s, post, "  outward axis\n"), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    const auto bare = cooked(skirt_config(s, "", "  outward axis\n"), s.sk, s.mesh, d); // the post left out
    CHECK(bare != nullptr);
    if (!bare)
        return;
    const auto moved = [&](int k, int l) {
        for (std::size_t i = 0; i < rig->particles.size(); ++i)
            if (rig->particles[i].dynamic && rig->particles[i].bone == s.bone(k, l))
                return length(rig->particles[i].reference - bare->particles[i].reference);
        return -1.0F;
    };
    std::printf("outward axis: strip 0 out %.4f m, opposite %.6f m\n", moved(0, 1),
                std::fmax(moved(4, 0), std::fmax(moved(4, 1), moved(4, 2))));
    CHECK(moved(0, 1) > 0.01F);
    CHECK(moved(4, 0) >= 0.0F && moved(4, 0) < 1e-5F && moved(4, 1) < 1e-5F && moved(4, 2) < 1e-5F);
    d.clear();
    CHECK(
        cooked("secondary_motion 1\nunits m\ngroup tail chain\n  attach hips\n  chain s0_0 s0_1\n  outward axis\nend\n",
               s.sk, s.mesh, d) == nullptr);
    CHECK(has_text(d, "outward axis"));
}

void floor_over_body()
{
    // a platform's underside 5 cm over the skirt's hips (its top row 25 cm under it, within reach of a floor's push
    // from below): not the floor the body stands on, so the skirt stays under it; pushed up through it, it stayed on
    // its top, stretched up from the hips
    const Skirt s = make_skirt(8, 3);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, "", "  thickness 0.01\n"), s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    std::vector<Triangle> room;
    const Vec3 a{-5, 1.05F, -5}, b{5, 1.05F, -5}, c{5, 1.05F, 5}, e{-5, 1.05F, 5};
    room.push_back({a, c, b, {0, 1, 0}, Surface::ground});
    room.push_back({a, e, c, {0, 1, 0}, Surface::ground});
    StaticEnvironment env(room);
    Actor actor(rig, 13);
    std::vector<Frame> pose;
    ActorInput in;
    in.pose = &pose;
    in.environment = &env;
    float highest = -1.0F;
    for (int f = 0; f < 60; ++f) {
        in.seconds = f / 60.0;
        pose = pose_of(s.sk, {});
        actor.update(in);
        for (std::size_t i = 0; i < actor.simulation().positions().size(); ++i)
            if (rig->particles[i].dynamic)
                highest = maxf(highest, actor.simulation().positions()[i].y);
    }
    std::printf("floor over the body: the skirt's highest control at %.3f m (the floor at 1.05)%s",
                static_cast<double>(highest), "\n");
    CHECK(highest < 1.0F);
}

void contacts()
{
    // a skirt over a fast capsule sweeping through its middle, a floor below it, a wall beside it
    Skirt s = make_skirt(8, 3);
    s.sk.names.push_back("bat");
    s.sk.parent.push_back(0);
    s.sk.rest.push_back(kIdentity4);
    Diagnostics d;
    auto rig = cooked(
        skirt_config(s, "collider bat capsule bat length 0.4 radius 0.05\n", "  collide body\n  thickness 0.01\n"),
        s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    std::vector<Triangle> room;
    const Vec3 a{-5, 0.55F, -5}, b{5, 0.55F, -5}, c{5, 0.55F, 5}, e{-5, 0.55F, 5};
    room.push_back({a, c, b, {0, 1, 0}, Surface::ground}); // a floor above the hem's rest (initial overlap)
    room.push_back({a, e, c, {0, 1, 0}, Surface::ground});
    box_triangles({0.2F, 0.0F, -2.0F}, {1.0F, 2.0F, 2.0F}, Surface::wall, room); // a wall cutting the skirt's side
    StaticEnvironment env(room);
    Actor actor(rig, 11);
    std::vector<Frame> pose;
    ActorInput in;
    in.pose = &pose;
    in.environment = &env;
    float below = 0.0F, inside_wall = 0.0F, through = 0.0F;
    for (int f = 0; f < 120; ++f) {
        in.seconds = f / 60.0;
        pose = pose_of(s.sk, {});
        // the first second the room alone; then the bat, horizontal along x at 0.8 m, sweeping through the
        // skirt along z at 6 m/s (a body part pushing cloth into a wall wins over the wall: measured apart)
        const bool batting = f >= 60;
        Frame &bat = pose.back();
        bat.q = {};
        bat.t =
            batting ? Vec3{-0.3F, 0.8F, -0.6F + 6.0F * static_cast<float>(f % 20) / 60.0F} : Vec3{-0.3F, 5.0F, 0.0F};
        actor.update(in);
        for (std::size_t i = 0; i < actor.simulation().positions().size(); ++i) {
            if (!rig->particles[i].dynamic)
                continue;
            const Vec3 x = actor.simulation().positions()[i];
            below = maxf(below, 0.55F - x.y);
            if (!batting && x.x > 0.2F && x.y < 2.0F && std::fabs(x.z) < 2.0F)
                inside_wall = maxf(inside_wall, x.x - 0.2F);
        }
        if (!batting)
            continue;
        // the faces' centres stay out of the bat (the interior between controls, not only the controls)
        for (const auto &face : rig->faces) {
            const auto &x = actor.simulation().positions();
            const Vec3 centre = (x[static_cast<std::size_t>(face.a)] + x[static_cast<std::size_t>(face.b)] +
                                 x[static_cast<std::size_t>(face.c)]) *
                                (1.0F / 3.0F);
            const auto &sh = actor.simulation().colliders().front();
            float u, v;
            closest_segments(centre, centre, sh.a, sh.b, u, v);
            through = maxf(through, sh.ra - length(centre - lerp(sh.a, sh.b, v)));
        }
    }
    CHECK(below < 0.012F); // floor (and the hem that started under it) kept above it
    std::printf("contacts: %.3f below the floor, %.3f into the wall, %.3f bat in a face\n", static_cast<double>(below),
                static_cast<double>(inside_wall), static_cast<double>(through));
    CHECK(inside_wall < 0.012F); // the wall's face
    CHECK(through < 0.03F);      // the bat's core never inside a face
    // a platform rising under the skirt pushes it up
    StaticEnvironment lift(std::vector<Triangle>{});
    std::vector<Triangle> plat;
    box_triangles({-1.0F, -1.0F, -1.0F}, {1.0F, 0.0F, 1.0F}, Surface::object, plat);
    StaticEnvironment platform(plat);
    Actor up(rig, 12);
    in.environment = &platform;
    float worst = 0.0F;
    for (int f = 0; f < 90; ++f) {
        in.seconds = f / 60.0;
        pose = pose_of(s.sk, {});
        if (f > 10)
            platform.translate({0.0F, 0.006F, 0.0F});
        const float top = f > 10 ? 0.006F * static_cast<float>(f - 10) : 0.0F;
        up.update(in);
        for (std::size_t i = 0; i < up.simulation().positions().size(); ++i)
            if (rig->particles[i].dynamic)
                worst = maxf(worst, top - up.simulation().positions()[i].y);
    }
    CHECK(worst < 0.015F);
}

void hair_and_wind()
{
    // a head with three single-bone tufts; guided, swing-limited, under a gusting wind window
    SkeletonDesc sk;
    MeshDesc mesh;
    sk.names = {"root", "head"};
    sk.parent = {-1, 0};
    sk.rest = {kIdentity4, kIdentity4};
    sk.rest[1][13] = 1.6F;
    for (int k = 0; k < 3; ++k) {
        const float a = static_cast<float>(k) * 2.0F;
        Mat4 m = down_frame({0.08F * std::cos(a), 1.7F, 0.08F * std::sin(a)}, a);
        // pointing up and out
        m[0] = 0.0F;
        m[1] = 1.0F;
        m[2] = 0.0F;
        m[4] = std::cos(a);
        m[5] = 0.0F;
        m[6] = std::sin(a);
        const Vec3 z = cross({0, 1, 0}, {std::cos(a), 0, std::sin(a)});
        m[8] = z.x;
        m[9] = z.y;
        m[10] = z.z;
        sk.names.push_back("tuft" + std::to_string(k));
        sk.parent.push_back(1);
        sk.rest.push_back(m);
        MeshDesc::Vertex v;
        v.position = point(m, {0.08F, 0, 0});
        v.bones = {2 + k, -1, -1, -1};
        v.weights = {1, 0, 0, 0};
        mesh.vertices.push_back(v);
    }
    const std::string text = "secondary_motion 1\ngroup hair hair\n  attach head\n  tufts tuft0 tuft1 tuft2\n"
                             "  mode guided\n  follow_frequency 7\n  swing_limit 10\n  damping 6\nend\n"
                             "control breeze\n  animation idle\n  time 1 2 seconds\n  blend 0.2 0.2\n  groups hair\n"
                             "  wind world 1 0 0 speed 4 gust 0.5\nend\n";
    Diagnostics d;
    auto rig = cooked(text, sk, mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    Actor a(rig, 21);
    std::vector<Frame> pose;
    ActorInput in;
    in.pose = &pose;
    float worst = 0.0F, wind_out = 0.0F, wind_in = 0.0F;
    for (int f = 0; f < 180; ++f) {
        const float t = static_cast<float>(f) / 60.0F;
        pose.assign(sk.rest.size(), {});
        Frame head;
        head.q = axis_angle({0, 0, 1}, 0.6F * std::sin(t * 9.0F)); // a sharp head shake
        head.t = {0.0F, 0.0F, 0.0F};
        for (std::size_t b = 0; b < pose.size(); ++b)
            pose[b] = b == 0 ? frame_of(sk.rest[0]) : compose(head, frame_of(sk.rest[b]));
        in.seconds = t;
        in.animations = {{"idle", -1, -1.0F, t, 0.0F, 1.0F}};
        a.update(in);
        // each tuft's tip within its swing limit (and a little) of the animated direction
        const auto &sim = a.simulation();
        for (std::size_t i = 0; i < rig->particles.size(); ++i) {
            const Particle &p = rig->particles[i];
            if (!p.dynamic)
                continue;
            const Vec3 head_at = sim.positions()[static_cast<std::size_t>(p.anchor)];
            const Vec3 want = apply(pose[static_cast<std::size_t>(p.bone)], p.offset) - head_at;
            const Vec3 got = sim.positions()[i] - head_at;
            const float off = std::acos(clampf(dot(normalized(want), normalized(got)), -1.0F, 1.0F));
            if (off > worst && off > 11.0F * kDegrees)
                std::printf("hair f%d particle %zu: %.1f degrees, |want| %.3f |got| %.3f\n", f, i,
                            static_cast<double>(off / kDegrees), static_cast<double>(length(want)),
                            static_cast<double>(length(got)));
            worst = maxf(worst, off);
        }
        const float speed = a.control_weights().empty() ? 0.0F : a.control_weights()[0];
        if (t < 0.9F || t > 2.4F)
            wind_out = maxf(wind_out, speed);
        if (t > 1.3F && t < 1.7F)
            wind_in = maxf(wind_in, speed);
    }
    std::printf("hair: worst %.2f degrees from the animated direction\n", static_cast<double>(worst / kDegrees));
    CHECK(worst < 11.0F * kDegrees); // the silhouette kept: within the limit of the animated direction
    CHECK(wind_out == 0.0F);         // the wind's window only
    CHECK(wind_in > 0.99F);
}

// A first pose with the legs already apart, wider than the skirt hangs at rest: the skirt starts around
// them (the colliders sweep in from their rest places), not squeezed between them.
void spread_start()
{
    Skirt s = make_skirt(8, 3);
    s.radius = 0.15F;
    for (const char *leg : {"leg_l", "leg_r"}) {
        s.sk.names.push_back(leg);
        s.sk.parent.push_back(1);
        Mat4 m = kIdentity4;
        m[12] = leg[4] == 'l' ? 0.08F : -0.08F; // at rest inside the skirt
        m[13] = 1.0F;
        s.sk.rest.push_back(m);
    }
    Diagnostics d;
    auto rig = cooked(skirt_config(s,
                                   "collider leg_l capsule leg_l length 0.6 radius 0.06\n"
                                   "collider leg_r capsule leg_r length 0.6 radius 0.06\n",
                                   "  thickness 0.01\n"),
                      s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    auto pose = pose_of(s.sk, {});
    // the legs pointing down (x along -y) and spread to 0.3 m either side
    for (std::size_t b = pose.size() - 2; b < pose.size(); ++b) {
        const float side = b == pose.size() - 2 ? 1.0F : -1.0F;
        pose[b].q = axis_angle({0, 0, 1}, -kPi * 0.5F + side * 0.5F);
        pose[b].t = {side * 0.08F, 1.0F, 0.0F};
    }
    Actor a(rig, 4);
    ActorInput in;
    in.pose = &pose;
    a.update(in);
    // every control outside both legs, and the side controls out beyond them
    float inside = 0.0F, widest = 0.0F;
    for (std::size_t i = 0; i < rig->particles.size(); ++i) {
        if (!rig->particles[i].dynamic)
            continue;
        const Vec3 x = a.simulation().positions()[i];
        for (const auto &c : a.simulation().colliders()) {
            float u, v;
            closest_segments(x, x, c.a, c.b, u, v);
            inside = maxf(inside, c.ra - length(x - lerp(c.a, c.b, v)));
        }
        widest = maxf(widest, std::fabs(x.x));
    }
    std::printf("spread start: deepest %.3f m inside a leg, widest %.3f m out\n", static_cast<double>(inside),
                static_cast<double>(widest));
    CHECK(inside < 0.005F);
    CHECK(widest > 0.3F); // around the legs, whose feet are 0.3 m and more out
}

void pose_cut_restart()
{
    // a pose cut (a new motion with no fade) jumping the legs starts the cloth afresh, draped round them; one
    // leaving the legs where they were, or a leg jump with no cut, carries the cloth on
    Skirt s = make_skirt(8, 3);
    s.radius = 0.15F;
    for (const char *leg : {"leg_l", "leg_r"}) {
        s.sk.names.push_back(leg);
        s.sk.parent.push_back(1);
        Mat4 m = kIdentity4;
        m[12] = leg[4] == 'l' ? 0.08F : -0.08F;
        m[13] = 1.0F;
        s.sk.rest.push_back(m);
    }
    Diagnostics d;
    auto rig = cooked(skirt_config(s,
                                   "collider leg_l capsule leg_l length 0.6 radius 0.06\n"
                                   "collider leg_r capsule leg_r length 0.6 radius 0.06\n",
                                   "  thickness 0.01\n"),
                      s.sk, s.mesh, d);
    CHECK(rig != nullptr);
    if (!rig)
        return;
    // the legs down, then spread `spread` radians either side
    const auto legs = [&](float spread) {
        auto pose = pose_of(s.sk, {});
        for (std::size_t b = pose.size() - 2; b < pose.size(); ++b) {
            const float side = b == pose.size() - 2 ? 1.0F : -1.0F;
            pose[b].q = axis_angle({0, 0, 1}, -kPi * 0.5F + side * spread);
            pose[b].t = {side * 0.08F, 1.0F, 0.0F};
        }
        return pose;
    };
    Actor a(rig, 4);
    std::vector<Frame> pose = legs(0.0F);
    ActorInput in;
    in.pose = &pose;
    int f = 0;
    const auto draw = [&](float spread, bool cut) {
        pose = legs(spread);
        in.seconds = f++ / 60.0;
        in.pose_cut = cut;
        a.update(in);
    };
    for (int k = 0; k < 30; ++k)
        draw(0.0F, false);
    const auto generation = a.generation();
    draw(0.0F, true); // a cut to the same pose of the legs: carried on
    CHECK(a.generation() == generation);
    draw(0.05F, false); // the legs moving 3 cm with no cut: carried on
    CHECK(a.generation() == generation);
    draw(0.6F, true); // their feet jumping 0.3 m out across a cut: started afresh
    CHECK(a.generation() == generation + 1);
    float inside = 0.0F;
    for (std::size_t i = 0; i < rig->particles.size(); ++i) {
        if (!rig->particles[i].dynamic)
            continue;
        const Vec3 x = a.simulation().positions()[i];
        for (const auto &c : a.simulation().colliders()) {
            float u, v;
            closest_segments(x, x, c.a, c.b, u, v);
            inside = maxf(inside, c.ra - length(x - lerp(c.a, c.b, v)));
        }
    }
    std::printf("pose cut: deepest %.3f m inside a leg after the restart\n", static_cast<double>(inside));
    CHECK(inside < 0.005F);
}

void stress()
{
    const Skirt s = make_skirt(8, 4);
    Diagnostics d;
    auto rig = cooked(skirt_config(s, "", "  max_speed 50\n"), s.sk, s.mesh, d);
    if (!rig) {
        CHECK(false);
        return;
    }
    Actor a(rig, 99);
    std::vector<Frame> pose;
    ActorInput in;
    in.pose = &pose;
    std::uint32_t seed = 12345;
    const auto rnd = [&]() {
        seed = seed * 1664525U + 1013904223U;
        return static_cast<float>(seed >> 8) / 16777216.0F - 0.5F;
    };
    bool finite_all = true;
    float far = 0.0F;
    for (int f = 0; f < 600; ++f) {
        in.seconds = f / 60.0;
        // violent shaking below the teleport threshold, and odd draw gaps
        pose = pose_of(s.sk, {rnd() * 1.4F, rnd() * 1.4F, rnd() * 1.4F}, rnd() * 3.0F);
        a.update(in);
        for (std::size_t i = 0; i < a.simulation().positions().size(); ++i) {
            finite_all = finite_all && finite(a.simulation().positions()[i]);
            if (rig->particles[i].dynamic)
                far = maxf(far, length(a.simulation().positions()[i] - a.simulation().targets()[i]));
        }
    }
    CHECK(finite_all);
    CHECK(far < 4.0F); // bounded: anything further is recovered
}

// N_EX830's configuration (docs/examples/secondary) on the real model, when it is at hand.
void n_ex830()
{
    const char *folder = std::getenv("KHDAYS_SECONDARY_ASSETS");
    if (folder == nullptr) {
        std::printf("N_EX830: KHDAYS_SECONDARY_ASSETS not set; skipped\n");
        return;
    }
    const std::filesystem::path dir(folder);
    const auto model = khdays::assets::kh2::import_mdlx(dir / "N_EX830.mdlx");
    SkeletonDesc sk;
    MeshDesc mesh;
    describe(model, sk, mesh);
    CHECK(sk.names.size() == 308U);
    Diagnostics d;
    const Config c = read_config(dir / "N_EX830.secondary.txt", d);
    Rig rig = cook(c, sk, mesh, d);
    for (const auto &x : d)
        if (x.error)
            std::fprintf(stderr, "%s\n", format(x).c_str());
    CHECK(!has_errors(d));
    if (has_errors(d))
        return;
    prepare_references(rig);
    int coat = 0;
    for (const auto &p : rig.particles)
        coat += p.dynamic && rig.groups[static_cast<std::size_t>(p.group)].name == "coat" ? 1 : 0;
    CHECK(coat == 43); // 44 listed, 85 shared by both front strips
    // `clip_world` on the coat: nine columns (the forked front's two share its top), closed across the slit
    {
        Rig clipped = rig;
        std::vector<Vec3> places;
        for (auto &g : clipped.groups)
            g.clip_world = g.name == "coat";
        for (const auto &p : clipped.particles)
            places.push_back(point(sk.rest[static_cast<std::size_t>(p.bone)], p.offset));
        const auto volumes = clip_volumes_of(clipped, places);
        CHECK(volumes.size() == 1U);
        if (volumes.size() == 1U) {
            const float v = clip_volume_check(volumes[0], places);
            // its quads split as the coat's mesh splits them: each edge between two controls one of the mesh's
            // faces has, but those across the slit (closed there)
            std::set<std::uint64_t> drawn;
            for (const Face &f : clipped.faces)
                for (const auto &[a, b] : {std::pair{f.a, f.b}, std::pair{f.b, f.c}, std::pair{f.c, f.a}})
                    drawn.insert(pair_key(a, b));
            std::size_t across = 0;
            const auto &cv = volumes[0];
            for (const auto &f : cv.faces)
                for (int k = 0; k < 3; ++k) {
                    const int a = f[static_cast<std::size_t>(k)], b = f[static_cast<std::size_t>((k + 1) % 3)];
                    if (static_cast<std::size_t>(a) >= cv.particles.size() ||
                        static_cast<std::size_t>(b) >= cv.particles.size())
                        continue;
                    const int pa = cv.particles[static_cast<std::size_t>(a)],
                              pb = cv.particles[static_cast<std::size_t>(b)];
                    if (clipped.particles[static_cast<std::size_t>(pa)].dynamic &&
                        clipped.particles[static_cast<std::size_t>(pb)].dynamic && drawn.count(pair_key(pa, pb)) == 0)
                        ++across;
                }
            std::printf("N_EX830 clip_world: %zu columns, %zu faces, %.4f model units^3, %zu edges not the mesh's\n",
                        volumes[0].columns, volumes[0].faces.size(), v, across / 2);
            CHECK(volumes[0].columns == 9U);
            CHECK(across / 2 == 5U); // across the slit: its three rows (88-92, 89-93, 90-94), two diagonals
        }
    }
    // the front slit: the two lower front panels are not joined
    for (const auto &l : rig.distances) {
        const std::string
            a = rig.skeleton.names[static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(l.a)].bone)],
            b = rig.skeleton.names[static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(l.b)].bone)];
        CHECK(!((a == "kh2_88" && b == "kh2_92") || (a == "kh2_92" && b == "kh2_88")));
    }
    // the drawstrings hang: their ends below their chest helpers once settled
    std::vector<Frame> rest(sk.rest.size());
    for (std::size_t b = 0; b < rest.size(); ++b)
        rest[b] = frame_of(sk.rest[b]);
    for (const auto &p : rig.particles) {
        const std::string &n = rig.skeleton.names[static_cast<std::size_t>(p.bone)];
        if (n == "kh2_106" || n == "kh2_223") {
            const auto &g = rig.groups[static_cast<std::size_t>(p.group)];
            const Vec3 end = apply(rest[static_cast<std::size_t>(g.reference_bone)], p.reference);
            const Vec3 helper = rest[n == "kh2_106" ? 102U : 219U].t;
            CHECK(end.y < helper.y - 0.12F);
        }
    }
}

} // namespace

int main()
{
    configuration();
    reconstruction_at_rest();
    slit_and_helpers();
    clock_independence();
    pause_hold_and_rate();
    spawn_teleport_identity();
    partial_jump();
    floor_over_body();
    authored_and_release();
    guided_stretch();
    cutscene_sim_shell();
    shell_push_shin();
    cutscene_sim_kick();
    clip_world_volume();
    strip_turn();
    outward_axis();
    contacts();
    spread_start();
    pose_cut_restart();
    hair_and_wind();
    stress();
    n_ex830();
    if (failures != 0) {
        std::fprintf(stderr, "secondary motion: %d checks failed\n", failures);
        return 1;
    }
    std::printf("secondary motion: all checks passed\n");
    return 0;
}
