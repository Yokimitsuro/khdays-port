// Secondary motion (runtime/mods/secondary, docs/SECONDARY_MOTION.md): configuration, cooking, the
// solver, its clock and its contacts on small synthetic rigs; and the N_EX830 setup when
// KHDAYS_SECONDARY_ASSETS names the folder holding N_EX830.mdlx and its .secondary.txt.

#include "actor.h"

#include "khdays/assets/kh2_mdlx.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
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
    authored_and_release();
    guided_stretch();
    contacts();
    spread_start();
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
