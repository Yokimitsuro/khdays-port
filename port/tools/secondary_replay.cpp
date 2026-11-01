// khdays-secondary-replay: a model's secondary motion played offline on KH2 motions, for tuning,
// validation and profiling without the game (docs/SECONDARY_MOTION.md, "Replay tool"). It drives the
// solver exactly as the port does (models.cpp): the motion evaluated, retargeted and cross-faded by
// matrices, the root motion taken out and the actor placed by a scripted path (run, turn, stop, jump,
// wall contact, teleport), the actor updated per draw on a game clock; then it skins the model with the
// bones the solver rebuilt, measures clipping, stretch and cost, and draws contact sheets (PNG).
//
//   khdays-secondary-replay <model.mdlx> <model.secondary.txt> <model.motions.txt> [options]
//     --out <dir>            contact sheets and the report go here
//     --scenario <name>      idle, calm, idle_loop, calm_loop, run, attack, jump, guard, air_front, air_back, wall,
//                            spawn, teleport, cut, platform, all (default all)
//     --actors <n>           simulate n actors at once (the benchmark; default 1)
//     --draw-hz <hz>         draws a second (default 60); --repeat <n> draws each n times
//     --reference <B.mdlx> <B.mset> <slot,...>   compare the coat with a baked KH2 coat (same coat bones)
//
// Diagnostics (stderr): KHDAYS_REPLAY_DEBUG=<strain> the worst links once strain passes it, and every leg
// in a coarse face with its side; KHDAYS_REPLAY_LEGS=1 each frame's deepest leg in the coat (cm);
// KHDAYS_REPLAY_DUMP=<frame> the coat's controls and targets in the hips' space; KHDAYS_REPLAY_REFDUMP=<frame>
// the bake's and the simulated coat there (--reference); KHDAYS_REPLAY_JITTER=1 each frame's worst drawn
// control, the legs' largest step and any coat bone turning over 60 deg past its last step;
// KHDAYS_REPLAY_LEGPOSE=<slot> the mset skeleton's legs and this model's after the retarget (knee bends) once; KHDAYS_REPLAY_TRACE=<bone,bone,...>
// those controls each frame (drawn, solved, targets, nearest collider); KHDAYS_REPLAY_CLIP=<name> each frame's
// deepest simulated vertex in the colliders so named (KHDAYS_REPLAY_CLIP_BY=<collider>: of the groups that
// collide with it); KHDAYS_REPLAY_POKE=<bone,...> those bones' body vertices through the coat's drawn surface;
// KHDAYS_REPLAY_NO_SIM=1 draws the animation alone; KHDAYS_REPLAY_SHEET=<frame>,<step>[,<columns>,<size>] the
// contact sheet's frames.

#include "actor.h"

#include "khdays/assets/kh2_mdlx.h"
#include "khdays/assets/kh2_motion.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace kh2 = khdays::assets::kh2;
using namespace khdays::secondary;

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open " + p.string());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// --- The motions: the mset of motions.txt, played on its skeleton and retargeted ---------------------

struct Motions {
    std::unique_ptr<kh2::MotionSet> set;
    std::unique_ptr<kh2::MotionSkeleton> skeleton;
    std::unique_ptr<kh2::Retarget> retarget;
    std::map<std::size_t, std::unique_ptr<kh2::MotionPlayer>> players;
    bool leg_printed = false;

    // the model's bones (KH2 cm, model space) for `slot` at `ticks`, the root motion taken out
    bool pose(std::size_t slot, float ticks, std::vector<kh2::Mat4> &out)
    {
        const kh2::Motion *m = set->motion(slot);
        if (m == nullptr)
            return false;
        auto &p = players[slot];
        if (!p)
            p = std::make_unique<kh2::MotionPlayer>(*skeleton);
        const float end = m->length_ticks(), from = m->loop_ticks();
        if (std::getenv("KHDAYS_REPLAY_JITTER") != nullptr && ticks > end) {
            const float wrapped = from >= 0.0F && from < end ? from + std::fmod(ticks - from, end - from) : end;
            const float before = from >= 0.0F && from < end ? from + std::fmod(ticks - 1.0F - from, end - from) : end;
            if (wrapped < before)
                std::fprintf(stderr, "loop slot %zu at %.1f ticks (length %.1f, loop from %.1f)\n", slot,
                             static_cast<double>(ticks), static_cast<double>(end), static_cast<double>(from));
        }
        if (ticks > end)
            ticks = from >= 0.0F && from < end ? from + std::fmod(ticks - from, end - from) : end;
        const kh2::Mat4 unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        p->evaluate(*m, ticks, unit, nullptr);
        std::vector<kh2::Mat4> g;
        if (retarget)
            retarget->apply(p->joints(), g);
        else
            g = p->joints();
        if (const char *lp = std::getenv("KHDAYS_REPLAY_LEGPOSE"); lp != nullptr && retarget &&
                                                                   static_cast<int>(slot) == std::atoi(lp) &&
                                                                   !leg_printed) { // the source's legs and ours, once
            leg_printed = true;
            const auto at = [](const kh2::Mat4 &x) { return Vec3{x[12], x[13], x[14]}; };
            const auto bend = [](Vec3 a, Vec3 b, Vec3 c) {
                const Vec3 u = normalized(b - a), v = normalized(c - b);
                return std::acos(std::fmax(-1.0F, std::fmin(1.0F, dot(u, v)))) * 57.2958F;
            };
            const auto &sj = p->joints();
            const int src[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16, 31};
            for (const int b : src)
                if (static_cast<std::size_t>(b) < sj.size()) {
                    const Vec3 v = at(sj[static_cast<std::size_t>(b)]);
                    std::fprintf(stderr, "src %d: %.1f %.1f %.1f\n", b, v.x, v.y, v.z);
                }
            for (const int b : {0, 1, 2, 3, 4, 5, 6, 7, 9, 14, 15, 18})
                if (static_cast<std::size_t>(b) < g.size()) {
                    const Vec3 v = at(g[static_cast<std::size_t>(b)]);
                    std::fprintf(stderr, "dst %d: %.1f %.1f %.1f\n", b, v.x, v.y, v.z);
                }
            for (const int b : {5, 6, 7, 8, 9, 10, 11, 14, 15, 16})
                if (static_cast<std::size_t>(b) < g.size()) {
                    const auto &x = g[static_cast<std::size_t>(b)];
                    std::fprintf(stderr, "scale %d: %.3f %.3f %.3f\n", b, std::hypot(x[0], x[1], x[2]),
                                 std::hypot(x[4], x[5], x[6]), std::hypot(x[8], x[9], x[10]));
                }
            if (sj.size() > 11 && g.size() > 9) {
                std::fprintf(stderr, "knee bend: source (7 8 11) %.1f deg, (16 8 11) %.1f deg; ours (5 6 7) %.1f deg\n",
                             bend(at(sj[7]), at(sj[8]), at(sj[11])), bend(at(sj[16]), at(sj[8]), at(sj[11])),
                             bend(at(g[5]), at(g[6]), at(g[7])));
            }
        }
        const Mat4 taken = inverse_affine(m->root_motion(ticks));
        out.resize(g.size());
        for (std::size_t b = 0; b < g.size(); ++b)
            out[b] = mul(taken, g[b]);
        return true;
    }
};

Motions load_motions(const std::filesystem::path &list, const std::filesystem::path &model)
{
    Motions m;
    std::ifstream in(list);
    if (!in)
        throw std::runtime_error("cannot open " + list.string());
    std::string line, mset, skeleton;
    std::vector<std::pair<int, int>> bones;
    std::vector<kh2::Retarget::Limb> limbs;
    const auto span = [](const std::string &s, int &a, int &b) {
        const auto dash = s.find('-');
        a = std::atoi(s.c_str());
        b = dash == std::string::npos ? a : std::atoi(s.c_str() + dash + 1);
    };
    while (std::getline(in, line)) {
        if (const auto h = line.find('#'); h != std::string::npos)
            line.erase(h);
        std::istringstream w(line);
        std::string a, b, c, d, r;
        if (!(w >> a >> b))
            continue;
        if (a == "mset")
            mset = b;
        else if (a == "skeleton")
            skeleton = b;
        else if (a == "bone" && w >> c) {
            int t0, t1, s0, s1;
            span(b, t0, t1);
            span(c, s0, s1);
            for (int i = 0; i <= t1 - t0; ++i)
                bones.emplace_back(t0 + i, s0 + i);
        } else if (a == "ik" && w >> c >> d) {
            int reach = -1;
            bool lengths = false;
            while (w >> r)
                if (r == "lengths")
                    lengths = true;
                else
                    reach = std::atoi(r.c_str());
            limbs.push_back({std::atoi(b.c_str()), std::atoi(c.c_str()), std::atoi(d.c_str()), reach, lengths});
        }
    }
    const auto folder = list.parent_path();
    const auto own = read_file(model);
    const kh2::MotionSkeleton target = kh2::read_motion_skeleton(own.data(), own.size());
    if (skeleton.empty()) {
        m.skeleton = std::make_unique<kh2::MotionSkeleton>(target);
    } else {
        const auto other = read_file(folder / skeleton);
        m.skeleton = std::make_unique<kh2::MotionSkeleton>(kh2::read_motion_skeleton(other.data(), other.size()));
        std::vector<int> source_of(target.joints.size(), -1);
        for (const auto &[t, s] : bones)
            source_of[static_cast<std::size_t>(t)] = s;
        m.retarget = std::make_unique<kh2::Retarget>(*m.skeleton, target, source_of, limbs);
    }
    const auto set = read_file(folder / mset);
    m.set = std::make_unique<kh2::MotionSet>(set.data(), set.size());
    return m;
}

// --- Scenarios: what the actor plays and where it is, second by second ------------------------------

struct Segment {
    float from;         // seconds into the scenario
    std::size_t slot;   // mset slot
    std::string name;   // the game animation's name the controls see
    float fade = 0.15F; // the cross-fade in (by matrices, as the game's)
};

struct Scenario {
    std::string name;
    float seconds;
    std::vector<Segment> segments;
    std::function<Frame(float)> place; // the actor's placement (metres, y up) at a time
    std::vector<Triangle> room;
    std::function<Vec3(float)> room_offset; // a moving object (null: static)
    float cut_at = -1.0F;                   // a camera cut then (seconds; -1 none): ActorInput::cut
};

Frame yaw_at(Vec3 t, float yaw)
{
    Frame f;
    f.q = axis_angle({0.0F, 1.0F, 0.0F}, yaw);
    f.t = t;
    return f;
}

std::vector<Triangle> floor_plane(float size)
{
    std::vector<Triangle> t;
    const Vec3 a{-size, 0, -size}, b{size, 0, -size}, c{size, 0, size}, d{-size, 0, size};
    t.push_back({a, c, b, {0, 1, 0}, Surface::ground});
    t.push_back({a, d, c, {0, 1, 0}, Surface::ground});
    return t;
}

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> s;
    const auto still = [](float) { return yaw_at({}, 0.0F); };
    s.push_back({"idle", 4.0F, {{0.0F, 0, "b_ro_A000"}}, still, floor_plane(20), nullptr});
    // the weaponless idle: standing, the coat should hang close to its authored drape
    s.push_back({"calm", 4.0F, {{0.0F, 2, "b_ro_e000"}}, still, floor_plane(20), nullptr});
    // both idles long enough to loop several times: what a loop's seam does to the cloth
    s.push_back({"idle_loop", 12.0F, {{0.0F, 0, "b_ro_A000"}}, still, floor_plane(20), nullptr});
    s.push_back({"calm_loop", 12.0F, {{0.0F, 2, "b_ro_e000"}}, still, floor_plane(20), nullptr});
    // run forward (+z, the model faces +z), a sharp turn about, run back, stop
    s.push_back({"run",
                 7.0F,
                 {{0.0F, 0, "b_ro_A000"}, {1.0F, 10, "b_ro_A001"}, {5.0F, 0, "b_ro_A000", 0.1F}},
                 [](float t) {
                     constexpr float speed = 5.5F;
                     if (t < 1.0F)
                         return yaw_at({}, 0.0F);
                     if (t < 3.0F)
                         return yaw_at({0, 0, (t - 1.0F) * speed}, 0.0F);
                     if (t < 3.2F) { // turning about over 0.2 s, slowing through the turn
                         const float u = (t - 3.0F) / 0.2F;
                         return yaw_at({0, 0, 2.0F * speed + 0.2F * speed * u * (1.0F - u)}, u * kPi);
                     }
                     if (t < 5.0F)
                         return yaw_at({0, 0, 2.0F * speed - (t - 3.2F) * speed}, kPi);
                     return yaw_at({0, 0, 2.0F * speed - 1.8F * speed}, kPi); // a dead stop
                 },
                 floor_plane(40),
                 nullptr});
    s.push_back({"attack",
                 3.5F,
                 {{0.0F, 0, "b_ro_A000"},
                  {0.5F, 42, "b_ro_B000", 0.08F},
                  {1.7F, 46, "b_ro_B001", 0.08F},
                  {2.9F, 0, "b_ro_A000"}},
                 [](float t) { // a lunge with each swing
                     const float z = std::fmin(1.0F, std::fmax(0.0F, (t - 0.5F) / 0.4F)) * 0.6F +
                                     std::fmin(1.0F, std::fmax(0.0F, (t - 1.7F) / 0.4F)) * 0.6F;
                     return yaw_at({0, 0, z}, 0.0F);
                 },
                 floor_plane(20),
                 nullptr});
    // a jump: crouch, up 1.1 m and down over 0.9 s, landing
    s.push_back({"jump",
                 3.5F,
                 {{0.0F, 0, "b_ro_A000"},
                  {0.5F, 14, "b_ro_A002", 0.05F},
                  {0.7F, 18, "b_ro_A003", 0.1F},
                  {1.6F, 22, "b_ro_A004", 0.05F},
                  {2.1F, 0, "b_ro_A000"}},
                 [](float t) {
                     float y = 0.0F;
                     if (t > 0.7F && t < 1.6F) {
                         const float u = (t - 0.7F) / 0.9F;
                         y = 4.4F * u * (1.0F - u);
                     }
                     return yaw_at({0, y, std::fmax(0.0F, std::fmin(t, 1.6F) - 0.7F) * 2.0F}, 0.0F);
                 },
                 floor_plane(20),
                 nullptr});
    s.push_back(
        {"guard", 3.0F, {{0.0F, 0, "b_ro_A000"}, {0.5F, 644, "b_ro_Ca003", 0.1F}}, still, floor_plane(20), nullptr});
    // launched by a hit (motions.txt `air`): KH2's aerial damage looping up 1.4 m and back down, cut to KH2's
    // landing on touching down (no fade, as the game lands it), its readying, then the idle
    // (KHDAYS_REPLAY_AIR=<seconds>: that long in the air, the landing at another point of the loop)
    const char *air_env = std::getenv("KHDAYS_REPLAY_AIR");
    const float up = air_env != nullptr ? std::fmax(0.2F, static_cast<float>(std::atof(air_env))) : 1.1F;
    const auto launched = [up](float t) {
        float y = 0.0F;
        if (t > 0.5F && t < 0.5F + up) {
            const float u = (t - 0.5F) / up;
            y = 5.6F * u * (1.0F - u);
        }
        return yaw_at({0, y, -std::fmax(0.0F, std::fmin(t, 0.5F + up) - 0.5F) * 1.5F}, 0.0F);
    };
    // (KHDAYS_REPLAY_FROM=<slot>: launched out of that motion, started 0.3 s before, instead of the idle)
    const char *from_env = std::getenv("KHDAYS_REPLAY_FROM");
    const auto before_launch = static_cast<std::size_t>(from_env != nullptr ? std::atoi(from_env) : 0);
    for (const auto &[name, air, land] : {std::tuple{"air_front", 66, 70}, std::tuple{"air_back", 74, 78}})
        s.push_back({name,
                     up + 2.7F,
                     {{0.0F, 0, "b_ro_A000"},
                      {0.2F, before_launch, "b_ro_B000", 0.0F},
                      {0.5F, static_cast<std::size_t>(air), "b_ro_A005", 0.0F},
                      {0.5F + up, static_cast<std::size_t>(land), "b_ro_A004", 0.0F},
                      {1.33F + up, 24, "b_ro_A000", 0.0F},
                      {1.76F + up, 0, "b_ro_A000", 0.27F}},
                     launched,
                     floor_plane(20),
                     nullptr});
    // a wall 30 cm behind; turning in place sweeps the coat across it, then backing into it
    {
        Scenario w{"wall",
                   5.0F,
                   {{0.0F, 0, "b_ro_A000"}},
                   [](float t) {
                       const float yaw = t < 2.5F ? 0.6F * std::sin(t * 2.5F) : 0.0F;
                       const float back = t > 2.5F ? -std::fmin(0.2F, (t - 2.5F) * 0.3F) : 0.0F;
                       return yaw_at({0, 0, back}, yaw);
                   },
                   floor_plane(20),
                   nullptr};
        box_triangles({-3.0F, 0.0F, -1.3F}, {3.0F, 3.0F, -0.30F}, Surface::wall, w.room);
        // and a corner: another wall to the left
        box_triangles({0.55F, 0.0F, -1.3F}, {1.5F, 3.0F, 1.0F}, Surface::wall, w.room);
        s.push_back(std::move(w));
    }
    // started while already running (the first pose mid-motion, moving)
    s.push_back({"spawn",
                 2.0F,
                 {{0.0F, 10, "b_ro_A001"}},
                 [](float t) { return yaw_at({0, 0, 5.5F * t}, 0.0F); },
                 floor_plane(40),
                 nullptr});
    // a teleport mid-idle, and a platform rising under the coat
    s.push_back({"teleport",
                 4.0F,
                 {{0.0F, 0, "b_ro_A000"}},
                 [](float t) { return yaw_at(t < 2.0F ? Vec3{} : Vec3{12.0F, 0, -4.0F}, t < 2.0F ? 0.0F : 1.2F); },
                 floor_plane(40),
                 nullptr});
    // a cutscene's camera cut: a new shot restarts the animation (no fade) and re-stages the actor 45 cm over and
    // 30 degrees turned, under the teleport's limits
    {
        Scenario c{"cut",
                   4.0F,
                   {{0.0F, 0, "b_ro_A000"}, {2.0F, 0, "b_ro_A000", 0.0F}},
                   [](float t) { return yaw_at(t < 2.0F ? Vec3{} : Vec3{0.4F, 0, 0.2F}, t < 2.0F ? 0.0F : 0.52F); },
                   floor_plane(20),
                   nullptr};
        c.cut_at = 2.0F;
        s.push_back(std::move(c));
    }
    {
        Scenario m{"platform", 4.0F, {{0.0F, 0, "b_ro_A000"}}, still, floor_plane(20), nullptr};
        box_triangles({-0.6F, -1.0F, -0.8F}, {0.6F, 0.0F, -0.3F}, Surface::object, m.room); // under the coat's back
        m.room_offset = [](float t) { return Vec3{0, std::fmin(0.45F, std::fmax(0.0F, (t - 1.0F) * 0.3F)), 0}; };
        s.push_back(std::move(m));
    }
    return s;
}

// --- Drawing: a flat-shaded software raster of the skinned model ------------------------------------

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgb;
    std::vector<float> depth;
    Image(int ww, int hh)
        : w(ww), h(hh), rgb(static_cast<std::size_t>(ww * hh * 3), 235), depth(static_cast<std::size_t>(ww * hh), 1e30F)
    {
    }
    void tri(Vec3 a, Vec3 b, Vec3 c, const std::uint8_t col[3], float shade)
    {
        const float minx = std::fmax(0.0F, std::floor(std::fmin(a.x, std::fmin(b.x, c.x))));
        const float maxx = std::fmin(static_cast<float>(w - 1), std::ceil(std::fmax(a.x, std::fmax(b.x, c.x))));
        const float miny = std::fmax(0.0F, std::floor(std::fmin(a.y, std::fmin(b.y, c.y))));
        const float maxy = std::fmin(static_cast<float>(h - 1), std::ceil(std::fmax(a.y, std::fmax(b.y, c.y))));
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-9F)
            return;
        for (float y = miny; y <= maxy; ++y)
            for (float x = minx; x <= maxx; ++x) {
                const float px = x + 0.5F, py = y + 0.5F;
                const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                const float w2 = 1.0F - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                const float z = w0 * a.z + w1 * b.z + w2 * c.z;
                const auto i = static_cast<std::size_t>(y * static_cast<float>(w) + x);
                if (z >= depth[i])
                    continue;
                depth[i] = z;
                for (int k = 0; k < 3; ++k)
                    rgb[i * 3 + static_cast<std::size_t>(k)] =
                        static_cast<std::uint8_t>(std::fmin(255.0F, static_cast<float>(col[k]) * shade));
            }
    }
};

// --- The run ----------------------------------------------------------------------------------------

struct Model {
    khdays::assets::ImportedModel imported;
    SkeletonDesc skeleton;
    MeshDesc mesh;
    std::vector<int> dominant;
    std::vector<std::uint8_t> coat_vertex;  // a vertex of a simulated bone
    std::vector<std::uint32_t> vertex_tags; // the collider tags its bone's group collides with
    std::vector<float> rest_depth;          // per vertex and collider: how far inside it is at rest
};

struct Metrics {
    float max_penetration = 0.0F; // cm: a simulated vertex inside a body collider
    int frames_clipping = 0;      // frames with any simulated vertex over 1 cm inside
    float max_floor = 0.0F;       // cm below the floor / room
    float max_stretch = 0.0F;     // of the coat's strip and anchor links over their rest length
    float max_flare = 0.0F;       // of its row and shear links (allowed by row_max_strain)
    float max_through = 0.0F;     // cm: a body collider's core reaching into or through a coarse face
    int frames_through = 0;       // frames with one over 1 cm
    float max_legs = 0.0F;        // cm: the same for the colliders tagged legs
    double shape_sum = 0.0;       // cm: the coat's controls from the authored drape carried by the hips
    int shape_count = 0;
    float shape_max = 0.0F;
    double push_sum = 0.0; // cm: the coat's controls off their (undraped) targets, what contact had to do
    double lean_sum = 0.0; // deg: the coat's lowest links (depth over 0.7, front half) off straight down
    int lean_count = 0;
    double hem_ratio_sum = 0.0; // the hem's mean radius about the hips' axis over its rest radius
    int hem_count = 0;
    int frames_legs = 0;
    float max_legs_drawn = 0.0F; // cm: the same for the drawn (smoothed) coat
    int frames_legs_drawn = 0;
    int frames = 0, resets = 0, rebases = 0;
    bool finite = true;
    // frame-to-frame acceleration beyond the targets' (cm per frame^2) and the coat bones' angular (deg)
    float jitter_pos_p99 = 0.0F, jitter_pos_max = 0.0F, jitter_rot_p99 = 0.0F, jitter_rot_max = 0.0F;
};

void skin(const Model &m, const std::vector<Frame> &bones, std::vector<Vec3> &out)
{
    std::vector<Mat4> s(bones.size());
    for (std::size_t b = 0; b < bones.size(); ++b)
        s[b] = mul(matrix_of(bones[b]), inverse_affine(m.skeleton.rest[b]));
    out.resize(m.mesh.vertices.size());
    for (std::size_t v = 0; v < m.mesh.vertices.size(); ++v) {
        const auto &x = m.mesh.vertices[v];
        Vec3 p;
        float total = 0.0F;
        for (std::size_t k = 0; k < 4; ++k)
            if (x.bones[k] >= 0) {
                p += point(s[static_cast<std::size_t>(x.bones[k])], x.position) * x.weights[k];
                total += x.weights[k];
            }
        out[v] = total > 0.0F ? p * (1.0F / total) : x.position;
    }
}

// The simulation's proxies, not the model: body colliders (grey), the coarse surfaces (blue), links
// (dark), anchors (red) and the room (green), seen from the front or the side.
void draw(const Simulation &sim, const std::vector<Triangle> &room, Vec3 room_offset, Image &img, int w, int h,
          Vec3 centre, bool side)
{
    const Rig &rig = sim.rig();
    const float scale = static_cast<float>(h) / 2.2F;
    const auto project = [&](Vec3 p) {
        const Vec3 d = p - centre;
        const float sx = side ? d.z : d.x;
        const float depth = side ? -d.x : -d.z;
        return Vec3{static_cast<float>(w) * 0.5F + sx * scale, static_cast<float>(h) * 0.5F - d.y * scale, depth};
    };
    const Vec3 light = normalized(Vec3{0.4F, 0.8F, side ? 0.2F : 0.6F});
    const auto shade = [&](Vec3 a, Vec3 b, Vec3 c) {
        return 0.5F + 0.5F * std::fabs(dot(normalized(cross(b - a, c - a)), light));
    };
    for (const auto &t : room) {
        const std::uint8_t col[3] = {170, 190, 170};
        const Vec3 a = t.a + room_offset, b = t.b + room_offset, c = t.c + room_offset;
        img.tri(project(a), project(b), project(c), col, 0.7F + 0.3F * std::fabs(dot(t.n, light)));
    }
    // a capsule as rings of quads about its core
    const auto capsule = [&](Vec3 a, Vec3 b, float ra, float rb, const std::uint8_t col[3]) {
        const Vec3 axis = length(b - a) > 1e-6F ? normalized(b - a) : Vec3{0, 1, 0};
        const Vec3 u = perpendicular(axis), v = cross(axis, u);
        constexpr int around = 10, along = 6;
        const auto at = [&](int i, int j) { // j: 0..along over the core with half-sphere caps either side
            const float th = 2.0F * kPi * static_cast<float>(i) / around;
            const float s = static_cast<float>(j) / along;
            const float cap = (s < 0.25F) ? (0.25F - s) * 4.0F : (s > 0.75F) ? (s - 0.75F) * 4.0F : 0.0F;
            const float phi = cap * kPi * 0.5F;
            const float core = std::fmin(1.0F, std::fmax(0.0F, (s - 0.25F) * 2.0F));
            const float r = ra + (rb - ra) * core;
            const Vec3 base = a + (b - a) * core +
                              axis * (s < 0.25F   ? -r * std::sin(phi)
                                      : s > 0.75F ? r * std::sin(phi)
                                                  : 0.0F);
            return base + (u * std::cos(th) + v * std::sin(th)) * (r * std::cos(phi));
        };
        for (int j = 0; j < along; ++j)
            for (int i = 0; i < around; ++i) {
                const Vec3 p00 = at(i, j), p10 = at(i + 1, j), p01 = at(i, j + 1), p11 = at(i + 1, j + 1);
                img.tri(project(p00), project(p10), project(p11), col, shade(p00, p10, p11));
                img.tri(project(p00), project(p11), project(p01), col, shade(p00, p11, p01));
            }
    };
    const std::uint8_t grey[3] = {205, 205, 210}, blue[3] = {70, 90, 160}, dark[3] = {30, 30, 40},
                       red[3] = {200, 40, 40};
    for (const auto &c : sim.colliders())
        capsule(c.a, c.b, c.ra, c.rb, grey);
    const auto &x = sim.positions();
    for (const auto &f : rig.faces) {
        const Vec3 a = x[static_cast<std::size_t>(f.a)], b = x[static_cast<std::size_t>(f.b)],
                   c = x[static_cast<std::size_t>(f.c)];
        img.tri(project(a), project(b), project(c), blue, shade(a, b, c));
    }
    for (const auto &d : rig.distances) {
        if (d.kind == Link::bend)
            continue;
        const Vec3 a = project(x[static_cast<std::size_t>(d.a)]), b = project(x[static_cast<std::size_t>(d.b)]);
        const Vec3 n = normalized(Vec3{-(b.y - a.y), b.x - a.x, 0.0F}) * 0.6F;
        const Vec3 lift{0.0F, 0.0F, -0.02F};
        img.tri(a - n + lift, a + n + lift, b + n + lift, dark, 1.0F);
        img.tri(a - n + lift, b + n + lift, b - n + lift, dark, 1.0F);
    }
    for (std::size_t i = 0; i < x.size(); ++i) {
        const Vec3 p = project(x[i]) + Vec3{0.0F, 0.0F, -0.05F};
        const float s = rig.particles[i].dynamic ? 1.0F : 1.6F;
        const std::uint8_t *col = rig.particles[i].dynamic ? dark : red;
        img.tri(p + Vec3{-s, -s, 0}, p + Vec3{s, -s, 0}, p + Vec3{s, s, 0}, col, 1.0F);
        img.tri(p + Vec3{-s, -s, 0}, p + Vec3{s, s, 0}, p + Vec3{-s, s, 0}, col, 1.0F);
    }
}

struct Run {
    Model model;
    std::shared_ptr<Rig> rig;
    Motions motions;
};

Metrics play(Run &run, const Scenario &sc, int actors, float draw_hz, int repeat, const std::filesystem::path &folder,
             Profile &total, bool sheets)
{
    const Rig &rig = *run.rig;
    const std::size_t nb = rig.skeleton.names.size();
    std::vector<std::unique_ptr<Actor>> list;
    for (int a = 0; a < actors; ++a)
        list.push_back(std::make_unique<Actor>(run.rig, 1234U + static_cast<std::uint32_t>(a)));
    StaticEnvironment env(sc.room);
    Vec3 env_at{};
    Metrics m;
    std::vector<kh2::Mat4> now, before;
    std::vector<Frame> pose(nb), shown(nb), out(nb);
    std::vector<std::uint8_t> written(nb);
    std::vector<Vec3> verts;
    // KHDAYS_REPLAY_SHEET=<frame>,<step>[,<columns>,<size>]: its frames, and fewer bigger pictures
    int sheet_cols = 10, cell = 1;
    if (const char *e = std::getenv("KHDAYS_REPLAY_SHEET"); e != nullptr) {
        int a = 0, b = 0, c = 0, d = 0;
        if (std::sscanf(e, "%d,%d,%d,%d", &a, &b, &c, &d) == 4) {
            sheet_cols = std::max(1, c);
            cell = std::clamp(d, 1, 4);
        }
    }
    const int cw = 160 * cell, ch = 240 * cell;
    Image sheet(sheet_cols * cw, 2 * ch);
    int drawn = 0;
    const int frames = static_cast<int>(sc.seconds * draw_hz);
    int every = std::max(1, frames / sheet_cols), first = 0;
    if (const char *e = std::getenv("KHDAYS_REPLAY_SHEET");
        e != nullptr && std::sscanf(e, "%d,%d", &first, &every) == 2)
        every = std::max(1, every);
    std::size_t seg = 0;
    float seg_start_pose_fade = 0.0F;
    std::vector<kh2::Mat4> fade_from;
    // jitter: the coat's controls' and bones' frame-to-frame acceleration (in the hips' space) over its targets'
    std::vector<Vec3> jx1, jx2, jt1, jt2;
    std::vector<Vec3> jw1; // the coat bones' angular steps, hips space
    std::vector<Quat> jq1;
    std::vector<float> jitter_pos, jitter_rot;
    for (int f = 0; f <= frames; ++f) {
        const float t = static_cast<float>(f) / draw_hz;
        // the segment playing, cross-faded by matrices from the pose last shown (as the game's blend); one with no
        // fade a pose cut (as models.cpp's KH2 motions starting so)
        bool pose_cut = false;
        while (seg + 1 < sc.segments.size() && t >= sc.segments[seg + 1].from) {
            pose_cut = sc.segments[seg + 1].fade <= 0.0F;
            fade_from = before;
            seg_start_pose_fade = sc.segments[seg + 1].fade;
            ++seg;
        }
        const Segment &s = sc.segments[seg];
        const float local = t - s.from;
        run.motions.pose(s.slot, local * 60.0F, now);
        if (!fade_from.empty() && local < seg_start_pose_fade) {
            const float u = local / seg_start_pose_fade;
            for (std::size_t b = 0; b < now.size(); ++b)
                for (std::size_t k = 0; k < 16; ++k)
                    now[b][k] = fade_from[b][k] * (1.0F - u) + now[b][k] * u;
        }
        before = now;
        const Frame place = sc.place(t);
        for (std::size_t b = 0; b < nb; ++b) {
            Mat4 g = now[b];
            g[12] *= 0.01F;
            g[13] *= 0.01F;
            g[14] *= 0.01F;
            pose[b] = compose(place, frame_of(g));
        }
        if (sc.room_offset) {
            const Vec3 o = sc.room_offset(t);
            if (length(o - env_at) > 1e-6F) {
                env.translate(o - env_at);
                env_at = o;
            }
        }
        ActorInput in;
        in.seconds = static_cast<double>(t);
        in.pose = &pose;
        in.placement = place;
        in.animations = {{s.name, static_cast<int>(s.slot), -1.0F, local, 0.0F, 1.0F}};
        in.environment = &env;
        in.cut = sc.cut_at >= 0.0F && t >= sc.cut_at && t - 1.0F / draw_hz < sc.cut_at;
        in.pose_cut = pose_cut && std::getenv("KHDAYS_REPLAY_NO_POSE_CUT") == nullptr;
        for (int r = 0; r < repeat; ++r) {
            for (auto &a : list) {
                const auto t0 = std::chrono::steady_clock::now();
                a->update(in);
                std::copy(pose.begin(), pose.end(), out.begin());
                std::fill(written.begin(), written.end(), std::uint8_t{0});
                if (std::getenv("KHDAYS_REPLAY_NO_SIM") == nullptr)
                    a->output(pose, out, written);
                total.add(static_cast<float>(
                    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count()));
            }
        }
        // measure the first actor's drawn model
        shown = out;
        skin(run.model, shown, verts);
        ++m.frames;
        float frame_pen = 0.0F;
        const Simulation &sim = list.front()->simulation();
        // KHDAYS_REPLAY_POKE=<bone,...>: the body's vertices on those bones through the coat's drawn surface,
        // each frame: how far one lies outside the nearest coat triangle (within 6 cm), outward being away
        // from the hips' long axis
        if (const char *pk = std::getenv("KHDAYS_REPLAY_POKE"); pk != nullptr) {
            const std::string want = std::string(",") + pk + ",";
            std::uint32_t legs = 0;
            for (const auto &k : rig.colliders)
                if (k.name == "thigh_l")
                    legs = k.tags;
            std::vector<Triangle> coat;
            const auto &tri = run.model.mesh.triangles;
            for (std::size_t ti = 0; ti + 2 < tri.size(); ti += 3) {
                bool all = true;
                for (std::size_t k = 0; k < 3; ++k)
                    all = all && run.model.coat_vertex[tri[ti + k]] && (run.model.vertex_tags[tri[ti + k]] & legs);
                if (all)
                    coat.push_back({verts[tri[ti]],
                                    verts[tri[ti + 1]],
                                    verts[tri[ti + 2]],
                                    {static_cast<float>(run.model.dominant[tri[ti]]), 0.0F, 0.0F},
                                    Surface::object});
            }
            TriangleBvh bvh;
            bvh.build(std::move(coat));
            std::vector<int> near;
            const Frame &hips = pose[2];
            const Vec3 axis = rotate(hips.q, {1.0F, 0.0F, 0.0F});
            float worst = 0.0F;
            int worst_bone = -1, over = 0, worst_under = -1;
            for (std::size_t v = 0; v < verts.size(); ++v) {
                const int b = run.model.dominant[v];
                if (b < 0 || run.model.coat_vertex[v] || want.find("," + std::to_string(b) + ",") == std::string::npos)
                    continue;
                const Vec3 p = verts[v];
                Aabb box;
                box.add(p);
                box.grow(0.06F);
                near.clear();
                bvh.query(box, near);
                float best = 0.06F;
                Vec3 q{};
                bool found = false;
                int under = -1;
                for (const int ti : near) {
                    const Triangle &c = bvh.triangles()[static_cast<std::size_t>(ti)];
                    const Vec3 x = closest_on_triangle(p, c.a, c.b, c.c);
                    const Vec3 n = normalized(cross(c.b - c.a, c.c - c.a), {0.0F, 0.0F, 0.0F});
                    const Vec3 d = p - x;
                    if (length(d - n * dot(d, n)) > 1e-4F)
                        continue; // beside the triangle, not under it (a shoe below the hem)
                    if (length(p - x) < best) {
                        best = length(p - x);
                        q = x;
                        found = true;
                        under = static_cast<int>(c.n.x);
                    }
                }
                if (!found)
                    continue;
                Vec3 o = p - hips.t;
                o = normalized(o - axis * dot(o, axis), {0.0F, 0.0F, 1.0F});
                const float out_by = dot(p - q, o) * 100.0F;
                if (out_by > 0.3F)
                    ++over;
                if (out_by > worst) {
                    worst = out_by;
                    worst_bone = b;
                    worst_under = under;
                }
            }
            std::fprintf(stderr, "poke %s f%d %.2f cm kh2_%d (%d verts over 0.3) under kh2_%d\n", sc.name.c_str(), f,
                         static_cast<double>(worst), worst_bone, over, worst_under);
        }
        {
            const std::size_t np = rig.particles.size();
            std::vector<Vec3> x(np), tg(np);
            for (std::size_t i = 0; i < np; ++i) { // the drawn points: on the bones as output
                const Particle &pp = rig.particles[i];
                x[i] = apply_inverse(pose[2], pp.dynamic ? apply(out[static_cast<std::size_t>(pp.bone)], pp.offset)
                                                         : sim.positions()[i]);
                tg[i] = apply_inverse(pose[2], sim.raw_targets()[i]);
            }
            if (jx2.size() == np) {
                float worst = 0.0F;
                for (std::size_t i = 0; i < np; ++i) {
                    if (!rig.particles[i].dynamic)
                        continue;
                    const Vec3 ax = x[i] - jx1[i] * 2.0F + jx2[i], at = tg[i] - jt1[i] * 2.0F + jt2[i];
                    worst = std::fmax(worst, length(ax - at) * 100.0F);
                }
                jitter_pos.push_back(worst);
                if (std::getenv("KHDAYS_REPLAY_JITTER") != nullptr) { // the worst control each frame
                    std::size_t wi = 0;
                    float wv = -1.0F;
                    for (std::size_t i = 0; i < np; ++i) {
                        if (!rig.particles[i].dynamic)
                            continue;
                        const float a = length((x[i] - jx1[i] * 2.0F + jx2[i]) - (tg[i] - jt1[i] * 2.0F + jt2[i]));
                        if (a > wv) {
                            wv = a;
                            wi = i;
                        }
                    }
                    // the body colliders' ends: the most any moved since the last frame (hips space)
                    static std::vector<Vec3> last_ends;
                    std::vector<Vec3> ends;
                    for (const auto &c : sim.colliders()) {
                        ends.push_back(apply_inverse(pose[2], c.a));
                        ends.push_back(apply_inverse(pose[2], c.b));
                    }
                    float cmove = 0.0F, lmove = 0.0F; // any collider's ends; the legs'
                    std::size_t cwhich = 0;
                    if (last_ends.size() == ends.size())
                        for (std::size_t e = 0; e < ends.size(); ++e) {
                            const float step = length(ends[e] - last_ends[e]);
                            if (step > cmove) {
                                cmove = step;
                                cwhich = e / 2;
                            }
                            const auto leg = std::find(rig.tag_names.begin(), rig.tag_names.end(), "legs");
                            if (leg != rig.tag_names.end() &&
                                (rig.colliders[e / 2].tags >> (leg - rig.tag_names.begin()) & 1U) != 0U)
                                lmove = std::fmax(lmove, step);
                        }
                    std::fprintf(stderr, "jleg %s f%d %.2f cm\n", sc.name.c_str(), f,
                                 static_cast<double>(lmove * 100.0F));
                    last_ends = ends;
                    std::fprintf(stderr, "jcol %s f%d %.2f cm %s\n", sc.name.c_str(), f,
                                 static_cast<double>(cmove * 100.0F), rig.colliders[cwhich].name.c_str());
                    static std::vector<Vec3> last_solved;
                    const float solved_step =
                        last_solved.size() == np ? length(sim.positions()[wi] - last_solved[wi]) : 0.0F;
                    std::fprintf(stderr, "jsol %s f%d %.2f cm\n", sc.name.c_str(), f,
                                 static_cast<double>(solved_step * 100.0F));
                    last_solved = sim.positions();
                    const Vec3 st = x[wi] - jx1[wi], tt = tg[wi] - jt1[wi];
                    std::fprintf(stderr, "jit %s f%d %.2f cm %s (group %s) step %.2f target step %.2f cm\n",
                                 sc.name.c_str(), f, static_cast<double>(wv * 100.0F),
                                 rig.skeleton.names[static_cast<std::size_t>(rig.particles[wi].bone)].c_str(),
                                 rig.groups[static_cast<std::size_t>(rig.particles[wi].group)].name.c_str(),
                                 static_cast<double>(length(st) * 100.0F), static_cast<double>(length(tt) * 100.0F));
                }
            }
            // KHDAYS_REPLAY_TRACE=<bones>: each listed control per frame in the hips' space (cm): drawn, solved,
            // raw and draped targets, and the nearest body collider (gap from its surface)
            if (const char *tr = std::getenv("KHDAYS_REPLAY_TRACE"); tr != nullptr) {
                const std::string want = std::string(",") + tr + ",";
                for (std::size_t i = 0; i < np; ++i) {
                    const int bone = rig.particles[i].bone;
                    if (want.find("," + std::to_string(bone) + ",") == std::string::npos)
                        continue;
                    const Vec3 p = sim.positions()[i], so = apply_inverse(pose[2], p) * 100.0F, dr = x[i] * 100.0F,
                               rt = tg[i] * 100.0F, dt = apply_inverse(pose[2], sim.targets()[i]) * 100.0F;
                    float gap = 1e9F;
                    std::size_t near = 0;
                    for (std::size_t ci = 0; ci < sim.colliders().size(); ++ci) {
                        const auto &c = sim.colliders()[ci];
                        const Vec3 ab = c.b - c.a;
                        const float ll = dot(ab, ab);
                        const float u = ll > 0.0F ? std::fmin(1.0F, std::fmax(0.0F, dot(p - c.a, ab) / ll)) : 0.0F;
                        const float g = length(p - (c.a + ab * u)) - (c.ra + (c.rb - c.ra) * u);
                        if (g < gap) {
                            gap = g;
                            near = ci;
                        }
                    }
                    std::fprintf(stderr,
                                 "tr f%d b%d drawn %.1f %.1f %.1f solved %.1f %.1f %.1f raw %.1f %.1f %.1f draped %.1f "
                                 "%.1f %.1f gap %.1f %s\n",
                                 f, bone, dr.x, dr.y, dr.z, so.x, so.y, so.z, rt.x, rt.y, rt.z, dt.x, dt.y, dt.z,
                                 gap * 100.0F, rig.colliders[near].name.c_str());
                }
            }
            jx2 = jx1;
            jx1 = x;
            jt2 = jt1;
            jt1 = tg;
            // the rebuilt bones: their turn in the hips' space, step to step
            std::vector<Quat> q(out.size());
            std::vector<Vec3> w(out.size());
            for (std::size_t b = 0; b < out.size(); ++b)
                q[b] = conjugate(out[2].q) * out[b].q;
            if (jq1.size() == out.size()) {
                for (std::size_t b = 0; b < out.size(); ++b) {
                    Quat d = q[b] * conjugate(jq1[b]);
                    if (d.w < 0.0F)
                        d = Quat{-d.x, -d.y, -d.z, -d.w};
                    w[b] = Vec3{d.x, d.y, d.z} * 2.0F; // small-angle rotation vector (rad)
                }
                if (jw1.size() == out.size()) {
                    float worst = 0.0F;
                    std::size_t wb = 0;
                    for (const auto &sb : rig.bones_out) {
                        const auto b = static_cast<std::size_t>(sb.bone);
                        if (rig.groups[static_cast<std::size_t>(sb.group)].name == "coat" &&
                            length(w[b] - jw1[b]) * 57.2958F > worst) {
                            worst = length(w[b] - jw1[b]) * 57.2958F;
                            wb = b;
                        }
                    }
                    jitter_rot.push_back(worst);
                    if (std::getenv("KHDAYS_REPLAY_JITTER") != nullptr && worst > 60.0F)
                        std::fprintf(stderr, "jrot %s f%d %.0f deg %s\n", sc.name.c_str(), f,
                                     static_cast<double>(worst), rig.skeleton.names[wb].c_str());
                }
                jw1 = w;
            }
            jq1 = q;
        }
        // KHDAYS_REPLAY_CLIP=<name>: each frame's deepest simulated vertex past its rest depth in the colliders
        // whose names start so, whatever the vertex collides with (KHDAYS_REPLAY_CLIP_BY=<collider>: only the
        // vertices of groups colliding with that one)
        if (const char *cn = std::getenv("KHDAYS_REPLAY_CLIP"); cn != nullptr) {
            const std::size_t nc = rig.colliders.size();
            std::uint32_t by = ~0U;
            if (const char *bn = std::getenv("KHDAYS_REPLAY_CLIP_BY"); bn != nullptr)
                for (const auto &k : rig.colliders)
                    if (k.name == bn)
                        by = k.tags;
            float clip_pen = 0.0F;
            for (std::size_t ci = 0; ci < nc; ++ci) {
                if (rig.colliders[ci].name.rfind(cn, 0) != 0)
                    continue;
                const auto &c = sim.colliders()[ci];
                const Vec3 ab = c.b - c.a;
                const float ll = dot(ab, ab);
                for (std::size_t v = 0; v < verts.size(); ++v) {
                    if (!run.model.coat_vertex[v] || !(run.model.vertex_tags[v] & by))
                        continue;
                    const float u = ll > 0.0F ? std::fmin(1.0F, std::fmax(0.0F, dot(verts[v] - c.a, ab) / ll)) : 0.0F;
                    const float pen = c.ra + (c.rb - c.ra) * u - length(verts[v] - (c.a + ab * u));
                    clip_pen = std::fmax(clip_pen, pen - std::fmax(0.0F, run.model.rest_depth[v * nc + ci]));
                }
            }
            std::fprintf(stderr, "clip %s f%d %.2f cm\n", sc.name.c_str(), f, static_cast<double>(clip_pen * 100.0F));
        }
        for (std::size_t v = 0; v < verts.size(); ++v) {
            if (!run.model.coat_vertex[v])
                continue;
            if (!finite(verts[v]))
                m.finite = false;
            const std::size_t nc = sim.colliders().size();
            for (std::size_t ci = 0; ci < nc; ++ci) {
                if (!(rig.colliders[ci].tags & run.model.vertex_tags[v]))
                    continue;
                const auto &c = sim.colliders()[ci];
                const Vec3 ab = c.b - c.a;
                const float ll = dot(ab, ab);
                const float u = ll > 0.0F ? std::fmin(1.0F, std::fmax(0.0F, dot(verts[v] - c.a, ab) / ll)) : 0.0F;
                const float pen = c.ra + (c.rb - c.ra) * u - length(verts[v] - (c.a + ab * u));
                // only what goes deeper than it sits at rest (a hair root inside the head is its design)
                frame_pen = std::fmax(frame_pen, pen - std::fmax(0.0F, run.model.rest_depth[v * nc + ci]));
            }
            for (const auto &tri : sc.room) {
                const Vec3 a = tri.a + env_at, b = tri.b + env_at, c = tri.c + env_at;
                const float side = dot(verts[v] - a, tri.n);
                if (side < 0.0F && side > -0.1F) {
                    const Vec3 onplane = verts[v] - tri.n * side;
                    if (length(closest_on_triangle(onplane, a, b, c) - onplane) < 1e-4F &&
                        -side * 100.0F > m.max_floor) {
                        m.max_floor = -side * 100.0F;
                        if (std::getenv("KHDAYS_REPLAY_DEBUG") != nullptr) {
                            const auto &mv = run.model.mesh.vertices[v];
                            std::fprintf(stderr, "below: f%d vertex %zu bone %s %.2f cm\n", f, v,
                                         rig.skeleton.names[static_cast<std::size_t>(mv.bones[0])].c_str(),
                                         static_cast<double>(-side * 100.0F));
                        }
                    }
                }
            }
        }
        m.max_penetration = std::fmax(m.max_penetration, frame_pen * 100.0F);
        // the body through the coat: each collider its faces collide with against each coarse face exactly
        float through = 0.0F, legs = 0.0F, legs_drawn = 0.0F;
        const auto legs_tag = std::find(rig.tag_names.begin(), rig.tag_names.end(), std::string("legs"));
        const std::uint32_t legs_bit =
            legs_tag == rig.tag_names.end() ? 0U : 1U << static_cast<unsigned>(legs_tag - rig.tag_names.begin());
        // the drawn coat: its controls on the bones as output (smoothed), what the player sees
        std::vector<Vec3> coat_drawn(sim.positions());
        for (std::size_t i = 0; i < rig.particles.size(); ++i)
            if (rig.particles[i].dynamic)
                coat_drawn[i] = apply(out[static_cast<std::size_t>(rig.particles[i].bone)], rig.particles[i].offset);
        for (int pass = 0; pass < 2; ++pass)
            for (std::size_t fi = 0; fi < rig.faces.size(); ++fi) {
                const Face &face = rig.faces[fi];
                const auto &x = pass == 0 ? sim.positions() : coat_drawn;
                const std::uint32_t mask = rig.particles[static_cast<std::size_t>(face.a)].collide;
                for (std::size_t ci = 0; ci < sim.colliders().size(); ++ci) {
                    if (!(rig.colliders[ci].tags & mask) ||
                        std::binary_search(rig.face_excluded.begin(), rig.face_excluded.end(),
                                           (static_cast<std::uint64_t>(ci) << 32) | fi))
                        continue;
                    const auto &c = sim.colliders()[ci];
                    Vec3 on_core, on_face;
                    bool crosses = false;
                    const float d = closest_segment_triangle(
                        c.a, c.b, x[static_cast<std::size_t>(face.a)], x[static_cast<std::size_t>(face.b)],
                        x[static_cast<std::size_t>(face.c)], on_core, on_face, crosses);
                    const float ab2 = dot(c.b - c.a, c.b - c.a);
                    const float u =
                        ab2 > 0.0F ? std::fmin(1.0F, std::fmax(0.0F, dot(on_core - c.a, c.b - c.a) / ab2)) : 0.0F;
                    // against the pair's contact size: the collider's, or nearer where the coat hangs nearer as authored
                    const float rk = c.ra + (c.rb - c.ra) * u, k0 = rig.colliders[ci].radius;
                    const float clearance = rig.face_clearance.empty() || k0 <= 0.0F
                                                ? 0.0F
                                                : rig.face_clearance[ci * rig.faces.size() + fi] * (c.ra / k0);
                    const float deep = (crosses ? 2.0F : 1.0F) * pair_reach(rk, clearance, 0.5F * rk) - d;
                    if (pass == 1) {
                        if (rig.colliders[ci].tags & legs_bit)
                            legs_drawn = std::fmax(legs_drawn, deep);
                        continue;
                    }
                    if (std::getenv("KHDAYS_REPLAY_DEBUG") != nullptr && deep > 0.005F &&
                        (rig.colliders[ci].tags & legs_bit)) {
                        // the face's rest side of the collider against where it is now
                        const auto rest_of = [&](int pi) {
                            const Particle &pp = rig.particles[static_cast<std::size_t>(pi)];
                            return apply(frame_of(rig.skeleton.rest[static_cast<std::size_t>(
                                             rig.groups[static_cast<std::size_t>(pp.group)].reference_bone)]),
                                         pp.reference);
                        };
                        const Collider &k = rig.colliders[ci];
                        const Vec3 ra = apply(frame_of(rig.skeleton.rest[static_cast<std::size_t>(k.bone)]), k.offset);
                        const Vec3 rb =
                            apply(frame_of(rig.skeleton.rest[static_cast<std::size_t>(k.end_bone)]), k.end_offset);
                        const Vec3 r0 = rest_of(face.a), r1 = rest_of(face.b), r2 = rest_of(face.c);
                        const Vec3 rc = (r0 + r1 + r2) * (1.0F / 3.0F);
                        float rs, rt;
                        closest_segments(rc, rc, ra, rb, rs, rt);
                        const float rest_sign =
                            dot(rc - lerp(ra, rb, rt), cross(r1 - r0, r2 - r0)) >= 0.0F ? 1.0F : -1.0F;
                        const Vec3 nn =
                            normalized(
                                cross(x[static_cast<std::size_t>(face.b)] - x[static_cast<std::size_t>(face.a)],
                                      x[static_cast<std::size_t>(face.c)] - x[static_cast<std::size_t>(face.a)])) *
                            rest_sign;
                        std::fprintf(
                            stderr,
                            "through f%d: %s into face %s %s %s by %.1f cm%s; ahead %.1f cm, rest gap %.1f cm\n", f,
                            k.name.c_str(),
                            rig.skeleton
                                .names[static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(face.a)].bone)]
                                .c_str(),
                            rig.skeleton
                                .names[static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(face.b)].bone)]
                                .c_str(),
                            rig.skeleton
                                .names[static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(face.c)].bone)]
                                .c_str(),
                            static_cast<double>(deep * 100.0F), crosses ? " (through it)" : "",
                            static_cast<double>(dot(on_face - on_core, nn) * 100.0F),
                            static_cast<double>(length(rc - lerp(ra, rb, rt)) * 100.0F));
                    }
                    through = std::fmax(through, deep);
                    if (rig.colliders[ci].tags & legs_bit)
                        legs = std::fmax(legs, deep);
                }
            }
        m.max_through = std::fmax(m.max_through, through * 100.0F);
        if (through > 0.01F)
            ++m.frames_through;
        if (std::getenv("KHDAYS_REPLAY_DUMP") != nullptr && f == std::atoi(std::getenv("KHDAYS_REPLAY_DUMP"))) {
            // the hips' frame, the right thigh's core and the right-side controls, in the hips' space (cm)
            const Frame &hips = pose[2];
            const bool world_axes = std::getenv("KHDAYS_REPLAY_DUMP_WORLD") != nullptr; // world axes about the hips
            const auto in_hips = [&](Vec3 p) { return (world_axes ? p - hips.t : apply_inverse(hips, p)) * 100.0F; };
            for (std::size_t ci = 0; ci < sim.colliders().size(); ++ci)
                if (rig.colliders[ci].name.rfind("thigh", 0) == 0 || rig.colliders[ci].name.rfind("calf", 0) == 0) {
                    const Vec3 a = in_hips(sim.colliders()[ci].a), b = in_hips(sim.colliders()[ci].b);
                    std::fprintf(stderr, "dump f%d %s: %.1f %.1f %.1f -> %.1f %.1f %.1f r %.1f\n", f,
                                 rig.colliders[ci].name.c_str(), a.x, a.y, a.z, b.x, b.y, b.z,
                                 sim.colliders()[ci].ra * 100.0F);
                }
            for (std::size_t i = 0; i < rig.particles.size(); ++i) {
                const std::string &n = rig.skeleton.names[static_cast<std::size_t>(rig.particles[i].bone)];
                if (!rig.particles[i].dynamic ||
                    rig.groups[static_cast<std::size_t>(rig.particles[i].group)].name != "coat")
                    continue;
                const Vec3 x = in_hips(sim.positions()[i]), aim = in_hips(sim.raw_targets()[i]);
                std::fprintf(stderr, "dump f%d %s: at %.1f %.1f %.1f target %.1f %.1f %.1f\n", f, n.c_str(), x.x, x.y,
                             x.z, aim.x, aim.y, aim.z);
            }
        }
        if (std::getenv("KHDAYS_REPLAY_LEGS") != nullptr)
            std::fprintf(stderr, "legs %s %d %.2f\n", sc.name.c_str(), f, static_cast<double>(legs * 100.0F));
        m.max_legs = std::fmax(m.max_legs, legs * 100.0F);
        m.max_legs_drawn = std::fmax(m.max_legs_drawn, legs_drawn * 100.0F);
        if (legs_drawn > 0.005F)
            ++m.frames_legs_drawn;
        // the coat's shape against its authored drape on the hips (bone 2)
        {
            const Frame rest_hips = frame_of(rig.skeleton.rest[2]);
            double hem_now = 0.0, hem_rest = 0.0;
            for (std::size_t i = 0; i < rig.particles.size(); ++i) {
                const Particle &p = rig.particles[i];
                if (!p.dynamic || rig.groups[static_cast<std::size_t>(p.group)].name != "coat")
                    continue;
                const Vec3 rest_point = apply(frame_of(rig.skeleton.rest[static_cast<std::size_t>(p.bone)]), p.offset);
                const Vec3 drape = apply(pose[2], apply_inverse(rest_hips, rest_point));
                const float e = length(sim.positions()[i] - drape) * 100.0F;
                m.shape_sum += e;
                ++m.shape_count;
                m.push_sum += static_cast<double>(length(sim.positions()[i] - sim.raw_targets()[i]) * 100.0F);
                if (p.depth > 0.7F && p.anchor >= 0 && apply_inverse(rest_hips, rest_point).y < -0.05F) { // front half
                    const Vec3 link = sim.positions()[i] - sim.positions()[static_cast<std::size_t>(p.anchor)];
                    const float l = length(link);
                    if (l > 1e-6F) {
                        m.lean_sum +=
                            static_cast<double>(std::acos(std::fmax(-1.0F, std::fmin(1.0F, -link.y / l))) * 57.2958F);
                        ++m.lean_count;
                    }
                }
                m.shape_max = std::fmax(m.shape_max, e);
                if (p.depth > 0.99F) {
                    Vec3 a = apply_inverse(pose[2], sim.positions()[i]), b = apply_inverse(rest_hips, rest_point);
                    a.x = b.x = 0.0F; // bone 2's x runs down the body: the radius is across it
                    hem_now += length(a);
                    hem_rest += length(b);
                }
            }
            if (hem_rest > 0.0) {
                m.hem_ratio_sum += hem_now / hem_rest;
                ++m.hem_count;
            }
        }
        if (legs > 0.005F)
            ++m.frames_legs;
        if (frame_pen > 0.01F)
            ++m.frames_clipping;
        for (const auto &d : rig.distances) {
            if (d.kind == Link::bend ||
                rig.groups[static_cast<std::size_t>(rig.particles[static_cast<std::size_t>(d.a)].group)].name != "coat")
                continue;
            const float l =
                length(sim.positions()[static_cast<std::size_t>(d.b)] - sim.positions()[static_cast<std::size_t>(d.a)]);
            if (d.kind == Link::row || d.kind == Link::shear)
                m.max_flare = std::fmax(m.max_flare, l / d.rest - 1.0F);
            else
                m.max_stretch = std::fmax(m.max_stretch, l / d.rest - 1.0F);
        }
        static bool told = false;
        if (std::getenv("KHDAYS_REPLAY_DEBUG") != nullptr && !told &&
            m.max_stretch > static_cast<float>(std::atof(std::getenv("KHDAYS_REPLAY_DEBUG")))) {
            told = true;
            std::vector<std::pair<float, std::size_t>> worst;
            for (std::size_t k = 0; k < rig.distances.size(); ++k) {
                const auto &d = rig.distances[k];
                if (d.kind == Link::row || d.kind == Link::shear || d.kind == Link::bend)
                    continue;
                const float l = length(sim.positions()[static_cast<std::size_t>(d.b)] -
                                       sim.positions()[static_cast<std::size_t>(d.a)]);
                worst.push_back({l / d.rest - 1.0F, k});
            }
            std::sort(worst.rbegin(), worst.rend());
            for (std::size_t k = 0; k < 8 && k < worst.size(); ++k) {
                const auto &d = rig.distances[worst[k].second];
                const auto &pa = rig.particles[static_cast<std::size_t>(d.a)],
                           &pb = rig.particles[static_cast<std::size_t>(d.b)];
                std::fprintf(stderr, "f%d link %s(%d)-%s(%d) kind %d rest %.2f strain %.0f%% | ta %.2f tb %.2f\n", f,
                             rig.skeleton.names[static_cast<std::size_t>(pa.bone)].c_str(), pa.dynamic,
                             rig.skeleton.names[static_cast<std::size_t>(pb.bone)].c_str(), pb.dynamic,
                             static_cast<int>(d.kind), static_cast<double>(d.rest * 100),
                             static_cast<double>(worst[k].first * 100),
                             static_cast<double>(length(sim.positions()[static_cast<std::size_t>(d.a)] -
                                                        sim.targets()[static_cast<std::size_t>(d.a)]) *
                                                 100),
                             static_cast<double>(length(sim.positions()[static_cast<std::size_t>(d.b)] -
                                                        sim.targets()[static_cast<std::size_t>(d.b)]) *
                                                 100));
            }
        }
        if (sheets && f >= first && (f - first) % every == 0 && drawn < sheet_cols) {
            const Vec3 centre = place.t + Vec3{0.0F, 0.85F, 0.0F};
            Image front(cw, ch), side(cw, ch);
            draw(sim, sc.room, env_at, front, cw, ch, centre, false);
            draw(sim, sc.room, env_at, side, cw, ch, centre, true);
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x)
                    for (int k = 0; k < 3; ++k) {
                        const auto src = static_cast<std::size_t>((y * cw + x) * 3 + k);
                        sheet.rgb[static_cast<std::size_t>((y * sheet.w + drawn * cw + x) * 3 + k)] = front.rgb[src];
                        sheet.rgb[static_cast<std::size_t>(((y + ch) * sheet.w + drawn * cw + x) * 3 + k)] =
                            side.rgb[src];
                    }
            ++drawn;
        }
    }
    for (const auto &a : list) {
        m.resets += a->simulation().resets;
        m.rebases += static_cast<int>(a->profile().rebases);
        const PhaseTimes &ph = a->profile().phases;
        total.phases.pose += ph.pose;
        total.phases.solve += ph.solve;
        total.phases.model_collision += ph.model_collision;
        total.phases.environment_gather += ph.environment_gather;
        total.phases.environment_query += ph.environment_query;
        total.phases.self_collision += ph.self_collision;
        total.phases.reconstruct += ph.reconstruct;
        total.phases.startup += ph.startup;
        total.ticks += a->profile().ticks;
    }
    if (sheets) {
        const auto file = (folder / (sc.name + ".png")).string();
        stbi_write_png(file.c_str(), sheet.w, sheet.h, 3, sheet.rgb.data(), sheet.w * 3);
    }
    const auto pct = [](std::vector<float> v, float q) {
        if (v.empty())
            return 0.0F;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<float>(v.size())))];
    };
    m.jitter_pos_p99 = pct(jitter_pos, 0.99F);
    m.jitter_pos_max = pct(jitter_pos, 1.0F);
    m.jitter_rot_p99 = pct(jitter_rot, 0.99F);
    m.jitter_rot_max = pct(jitter_rot, 1.0F);
    return m;
}

// The coat against a baked KH2 coat: the reference model's motion drives this model's body (bones
// shared up to `shared`), the coat simulated; compared control by control with the bake.
void compare_reference(Run &run, const std::filesystem::path &mdlx, const std::filesystem::path &mset,
                       const std::vector<std::size_t> &slots, FILE *report)
{
    const auto bytes = read_file(mdlx), set_bytes = read_file(mset);
    const kh2::MotionSkeleton sk = kh2::read_motion_skeleton(bytes.data(), bytes.size());
    const kh2::MotionSet set(set_bytes.data(), set_bytes.size());
    const Rig &rig = *run.rig;
    const std::size_t nb = rig.skeleton.names.size();
    constexpr std::size_t shared = 95; // N_EX830, B_EX390 and P_EX100 share bones 0-94 (body and coat)
    std::vector<Frame> rest(nb);
    for (std::size_t b = 0; b < nb; ++b)
        rest[b] = frame_of(rig.skeleton.rest[b]);
    for (const std::size_t slot : slots) {
        const kh2::Motion *m = set.motion(slot);
        if (m == nullptr)
            continue;
        kh2::MotionPlayer player(sk);
        Actor actor(run.rig, 77U);
        std::vector<Frame> pose(nb), baked(nb), out(nb);
        std::vector<std::uint8_t> written(nb);
        double sum2 = 0.0, bake_amp = 0.0, sim_amp = 0.0, bake_stretch = 0.0, sim_stretch = 0.0;
        int count = 0;
        const int frames = static_cast<int>(m->length_ticks());
        for (int f = 0; f <= frames; ++f) {
            const float ticks = static_cast<float>(f);
            const kh2::Mat4 unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            player.evaluate(*m, ticks, unit, nullptr);
            const Mat4 taken = inverse_affine(m->root_motion(ticks));
            for (std::size_t b = 0; b < nb; ++b) {
                if (b < shared && b < player.joints().size()) {
                    Mat4 g = mul(taken, player.joints()[b]);
                    g[12] *= 0.01F;
                    g[13] *= 0.01F;
                    g[14] *= 0.01F;
                    pose[b] = baked[b] = frame_of(g);
                } else { // this model's own bones at rest under their parents
                    const int p = rig.skeleton.parent[b];
                    pose[b] = p >= 0 ? compose(pose[static_cast<std::size_t>(p)],
                                               relative(rest[static_cast<std::size_t>(p)], rest[b]))
                                     : rest[b];
                    baked[b] = pose[b];
                }
            }
            ActorInput in;
            in.seconds = ticks / 60.0;
            in.pose = &pose;
            actor.update(in);
            std::fill(written.begin(), written.end(), std::uint8_t{0});
            out = pose;
            actor.output(pose, out, written);
            const Simulation &sim = actor.simulation();
            const Frame &hips = baked[2];
            if (std::getenv("KHDAYS_REPLAY_REFDUMP") != nullptr &&
                f == std::atoi(std::getenv("KHDAYS_REPLAY_REFDUMP"))) {
                const auto in_hips = [&](Vec3 p) { return apply_inverse(hips, p) * 100.0F; };
                for (const int lb : {5, 6, 9, 14, 15, 18}) {
                    const Vec3 a = in_hips(baked[static_cast<std::size_t>(lb)].t);
                    std::fprintf(stderr, "ref %zu f%d bone %d: %.1f %.1f %.1f\n", slot, f, lb, a.x, a.y, a.z);
                }
                for (std::size_t i = 0; i < rig.particles.size(); ++i) {
                    const Particle &p = rig.particles[i];
                    if (!p.dynamic || rig.groups[static_cast<std::size_t>(p.group)].name != "coat")
                        continue;
                    const Vec3 bk = in_hips(apply(baked[static_cast<std::size_t>(p.bone)], p.offset));
                    const Vec3 sm = in_hips(sim.positions()[i]);
                    std::fprintf(stderr, "ref %zu f%d %s: bake %.1f %.1f %.1f sim %.1f %.1f %.1f\n", slot, f,
                                 rig.skeleton.names[static_cast<std::size_t>(p.bone)].c_str(), bk.x, bk.y, bk.z, sm.x,
                                 sm.y, sm.z);
                }
            }
            for (std::size_t i = 0; i < rig.particles.size(); ++i) {
                const Particle &p = rig.particles[i];
                if (!p.dynamic || rig.groups[static_cast<std::size_t>(p.group)].name != "coat")
                    continue;
                const Vec3 bake = apply(baked[static_cast<std::size_t>(p.bone)], p.offset);
                const Vec3 mine = sim.positions()[i];
                sum2 += static_cast<double>(dot(bake - mine, bake - mine));
                ++count;
                if (p.depth > 0.99F) { // the hem's swing about its rest place on the hips
                    const Vec3 rest_place =
                        apply(hips, apply_inverse(rest[2], apply(rest[static_cast<std::size_t>(p.bone)], p.offset)));
                    bake_amp = std::max(bake_amp, static_cast<double>(length(bake - rest_place)));
                    sim_amp = std::max(sim_amp, static_cast<double>(length(mine - rest_place)));
                }
            }
            for (const auto &d : rig.distances) {
                const Particle &pa = rig.particles[static_cast<std::size_t>(d.a)],
                               &pb = rig.particles[static_cast<std::size_t>(d.b)];
                if (d.kind == Link::bend || !pa.dynamic || !pb.dynamic ||
                    rig.groups[static_cast<std::size_t>(pa.group)].name != "coat")
                    continue;
                const float lb = length(apply(baked[static_cast<std::size_t>(pb.bone)], pb.offset) -
                                        apply(baked[static_cast<std::size_t>(pa.bone)], pa.offset));
                const float ls = length(sim.positions()[static_cast<std::size_t>(d.b)] -
                                        sim.positions()[static_cast<std::size_t>(d.a)]);
                if (std::getenv("KHDAYS_REPLAY_DEBUG") != nullptr && f == 0 && std::fabs(lb / d.rest - 1.0F) > 0.5F)
                    std::fprintf(stderr, "bake slot %zu: %s-%s rest %.2f bake %.2f cm (scales %.2f %.2f)\n", slot,
                                 rig.skeleton.names[static_cast<std::size_t>(pa.bone)].c_str(),
                                 rig.skeleton.names[static_cast<std::size_t>(pb.bone)].c_str(),
                                 static_cast<double>(d.rest * 100.0F), static_cast<double>(lb * 100.0F),
                                 static_cast<double>(baked[static_cast<std::size_t>(pa.bone)].s.x),
                                 static_cast<double>(baked[static_cast<std::size_t>(pb.bone)].s.x));
                bake_stretch = std::max(bake_stretch, static_cast<double>(std::fabs(lb / d.rest - 1.0F)));
                sim_stretch = std::max(sim_stretch, static_cast<double>(std::fabs(ls / d.rest - 1.0F)));
            }
        }
        std::fprintf(report,
                     "reference slot %zu (%s, %d ticks): coat RMS from the bake %.1f cm; hem swing bake %.1f cm, "
                     "simulated %.1f cm; worst link strain bake %.1f%%, simulated %.1f%%\n",
                     slot, set.name(slot).c_str(), frames, count ? std::sqrt(sum2 / count) * 100.0 : 0.0,
                     bake_amp * 100.0, sim_amp * 100.0, bake_stretch * 100.0, sim_stretch * 100.0);
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: khdays-secondary-replay <model.mdlx> <model.secondary.txt> <model.motions.txt> "
                             "[--out dir] [--scenario name] [--actors n] [--draw-hz hz] [--repeat n] "
                             "[--reference B.mdlx B.mset slot,slot]\n");
        return 2;
    }
    std::filesystem::path out = ".";
    std::string which = "all";
    int actors = 1, repeat = 1;
    float hz = 60.0F;
    std::filesystem::path ref_mdlx, ref_mset;
    std::vector<std::size_t> ref_slots;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc)
            out = argv[++i];
        else if (a == "--scenario" && i + 1 < argc)
            which = argv[++i];
        else if (a == "--actors" && i + 1 < argc)
            actors = std::max(1, std::atoi(argv[++i]));
        else if (a == "--draw-hz" && i + 1 < argc)
            hz = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--repeat" && i + 1 < argc)
            repeat = std::max(1, std::atoi(argv[++i]));
        else if (a == "--reference" && i + 3 < argc) {
            ref_mdlx = argv[++i];
            ref_mset = argv[++i];
            std::istringstream s(argv[++i]);
            std::string n;
            while (std::getline(s, n, ','))
                ref_slots.push_back(static_cast<std::size_t>(std::atoi(n.c_str())));
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    try {
        std::filesystem::create_directories(out);
        Run run;
        const auto t0 = std::chrono::steady_clock::now();
        run.model.imported = kh2::import_mdlx(argv[1]);
        describe(run.model.imported, run.model.skeleton, run.model.mesh);
        Diagnostics diag;
        const Config config = read_config(argv[2], diag);
        Rig rig = cook(config, run.model.skeleton, run.model.mesh, diag);
        for (const auto &d : diag)
            std::fprintf(stderr, "%s\n", format(d).c_str());
        if (has_errors(diag))
            return 1;
        Diagnostics prepared;
        prepare_references(rig, &prepared);
        for (const auto &d : prepared)
            std::fprintf(stderr, "%s\n", format(d).c_str());
        const double cook_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        run.rig = std::make_shared<Rig>(std::move(rig));
        run.motions = load_motions(argv[3], argv[1]);
        // the vertices the simulation moves
        std::vector<char> simulated(run.model.skeleton.names.size(), 0);
        for (const auto &s : run.rig->bones_out)
            simulated[static_cast<std::size_t>(s.bone)] = 1;
        for (const auto &c : run.rig->carried)
            simulated[static_cast<std::size_t>(c.bone)] = 1;
        // each simulated vertex's group's collider tags, and how deep it sits in each collider at rest
        std::vector<std::uint32_t> tags_of(run.model.skeleton.names.size(), 0);
        for (const auto &s : run.rig->bones_out)
            tags_of[static_cast<std::size_t>(s.bone)] =
                run.rig->particles[static_cast<std::size_t>(s.particle)].collide;
        for (const auto &c : run.rig->carried)
            tags_of[static_cast<std::size_t>(c.bone)] =
                run.rig
                    ->particles[static_cast<std::size_t>(run.rig->bones_out[static_cast<std::size_t>(c.by)].particle)]
                    .collide;
        std::vector<Frame> rest(run.model.skeleton.names.size());
        for (std::size_t b = 0; b < rest.size(); ++b)
            rest[b] = frame_of(run.model.skeleton.rest[b]);
        const std::size_t nc = run.rig->colliders.size();
        for (const auto &v : run.model.mesh.vertices) {
            int bone = -1;
            for (std::size_t k = 0; k < 4; ++k)
                if (v.bones[k] >= 0 && v.weights[k] > 0.5F && simulated[static_cast<std::size_t>(v.bones[k])])
                    bone = v.bones[k];
            run.model.coat_vertex.push_back(bone >= 0 ? 1 : 0);
            int heaviest = -1;
            for (std::size_t k = 0; k < 4; ++k)
                if (v.bones[k] >= 0 && (heaviest < 0 || v.weights[k] > v.weights[static_cast<std::size_t>(heaviest)]))
                    heaviest = static_cast<int>(k);
            run.model.dominant.push_back(heaviest >= 0 ? v.bones[static_cast<std::size_t>(heaviest)] : -1);
            run.model.vertex_tags.push_back(bone >= 0 ? tags_of[static_cast<std::size_t>(bone)] : 0U);
            for (std::size_t ci = 0; ci < nc; ++ci) {
                const Collider &k = run.rig->colliders[ci];
                const Vec3 a = apply(rest[static_cast<std::size_t>(k.bone)], k.offset);
                const Vec3 b =
                    k.kind == ShapeKind::capsule ? apply(rest[static_cast<std::size_t>(k.end_bone)], k.end_offset) : a;
                const Vec3 ab = b - a;
                const float ll = dot(ab, ab);
                const float u = ll > 0.0F ? std::fmin(1.0F, std::fmax(0.0F, dot(v.position - a, ab) / ll)) : 0.0F;
                run.model.rest_depth.push_back(k.radius + (k.end_radius - k.radius) * u -
                                               length(v.position - (a + ab * u)));
            }
        }
        FILE *report = nullptr;
        if (fopen_s(&report, (out / "report.txt").string().c_str(), "w") != 0 || report == nullptr)
            report = stdout;
        std::fprintf(report, "%s\ncook and prepare: %.1f ms\n", run.rig->summary.c_str(), cook_ms);
        std::fprintf(report, "settings: %.0f Hz, %d substeps, %d iterations; draws at %.0f Hz x%d; %d actor(s)\n",
                     static_cast<double>(run.rig->config.rate), run.rig->config.substeps, run.rig->config.iterations,
                     static_cast<double>(hz), repeat, actors);
        Profile all;
        for (const auto &sc : scenarios()) {
            if (which != "all" && which != sc.name)
                continue;
            Profile p;
            const Metrics m = play(run, sc, actors, hz, repeat, out, p, true);
            all.totals.insert(all.totals.end(), p.totals.begin(), p.totals.end());
            {
                const PhaseTimes &ph = p.phases;
                const double n = static_cast<double>(std::max<std::uint64_t>(1, p.ticks));
                std::fprintf(report,
                             "          per tick (us): pose %.1f, solve %.1f, model collision %.1f, room gather %.1f, "
                             "room queries %.1f, self collision %.1f, rebuild %.1f; startup %.0f us\n",
                             ph.pose / n, ph.solve / n, ph.model_collision / n, ph.environment_gather / n,
                             ph.environment_query / n, ph.self_collision / n, ph.reconstruct / n, ph.startup);
            }
            std::fprintf(
                report,
                "%-9s %4d frames: shape off the drape %.1f cm mean, %.1f max; off its targets %.1f cm; lower front coat %.0f deg off vertical; hem %.2fx its rest radius; legs in coat %.1f cm (%d frames over 0.5 cm), drawn %.1f cm (%d frames); body in coat %.1f cm (%d over 1 cm); worst penetration %.2f cm (%d frames over 1 cm); below the room %.2f cm; "
                "strips %.1f%%, rows %.0f%%; resets %d, rebases %d%s; per actor update p50 %.0f us, p95 %.0f us, "
                "p99 %.0f us\n",
                sc.name.c_str(), m.frames, m.shape_count ? m.shape_sum / m.shape_count : 0.0,
                static_cast<double>(m.shape_max), m.shape_count ? m.push_sum / m.shape_count : 0.0,
                m.lean_count ? m.lean_sum / m.lean_count : 0.0, m.hem_count ? m.hem_ratio_sum / m.hem_count : 0.0,
                static_cast<double>(m.max_legs), m.frames_legs, static_cast<double>(m.max_legs_drawn),
                m.frames_legs_drawn, static_cast<double>(m.max_through), m.frames_through,
                static_cast<double>(m.max_penetration), m.frames_clipping, static_cast<double>(m.max_floor),
                static_cast<double>(m.max_stretch * 100.0F), static_cast<double>(m.max_flare * 100.0F), m.resets,
                m.rebases, m.finite ? "" : ", NOT FINITE", static_cast<double>(p.percentile(0.5F)),
                static_cast<double>(p.percentile(0.95F)), static_cast<double>(p.percentile(0.99F)));
            std::fprintf(report,
                         "          jitter: controls %.2f cm p99, %.2f max; coat bones %.1f deg p99, %.1f max\n",
                         static_cast<double>(m.jitter_pos_p99), static_cast<double>(m.jitter_pos_max),
                         static_cast<double>(m.jitter_rot_p99), static_cast<double>(m.jitter_rot_max));
            std::fflush(report);
        }
        std::fprintf(report, "all scenarios: per actor update p50 %.0f us, p95 %.0f us, p99 %.0f us, max %.0f us\n",
                     static_cast<double>(all.percentile(0.5F)), static_cast<double>(all.percentile(0.95F)),
                     static_cast<double>(all.percentile(0.99F)), static_cast<double>(all.percentile(1.0F)));
        if (!ref_mdlx.empty())
            compare_reference(run, ref_mdlx, ref_mset, ref_slots, report);
        if (report != stdout)
            std::fclose(report);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "secondary-replay: %s\n", e.what());
        return 1;
    }
    return 0;
}
