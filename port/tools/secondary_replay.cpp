// khdays-secondary-replay: a model's secondary motion played offline on KH2 motions, or (`--days`) on the game's own
// animations posed onto the model by the port's models.cpp itself, for tuning, validation and profiling without the
// game. It drives the solver as the port does (models.cpp), skins the model with the bones the solver rebuilt, measures
// clipping, stretch and cost and draws contact sheets. Its options and diagnostics: docs/SECONDARY_MOTION.md, "Replay
// tool".

#include "actor.h"
#include "replay_game_stubs.h"

#include "khdays/assets/animation.h"
#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/kh2_mdlx.h"
#include "khdays/assets/kh2_motion.h"
#include "khdays/assets/mesh.h"

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
#include <set>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

// port/runtime/mods/models.cpp: one draw of a mod's model posed by the game's node matrices, up to the cloth
bool khdays_models_offline_pose(const char *file, unsigned offset, const std::vector<std::array<float, 16>> &nodes,
                                const std::vector<std::tuple<std::string, float, float, float>> &animations,
                                std::vector<std::array<float, 16>> &joints, std::vector<int> &kh2_bones);

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
    std::unique_ptr<kh2::MotionSkeleton> skeleton, own_skeleton;
    std::unique_ptr<kh2::Retarget> retarget;
    std::vector<std::pair<std::size_t, std::size_t>> own_slots; // `own` msets' slots, [from, to): not retargeted
    std::map<std::size_t, std::unique_ptr<kh2::MotionPlayer>> players;
    bool leg_printed = false;
    // motions.txt's `rotate_motions`, as models.cpp: every pose turned by it (root motion with it)
    kh2::Mat4 turn{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    bool own(std::size_t slot) const
    {
        if (!retarget)
            return true;
        for (const auto &[from, to] : own_slots)
            if (slot >= from && slot < to)
                return true;
        return false;
    }

    // the model's bones (KH2 cm, model space) for `slot` at `ticks`, the root motion taken out
    bool pose(std::size_t slot, float ticks, std::vector<kh2::Mat4> &out)
    {
        const kh2::Motion *m = set->motion(slot);
        if (m == nullptr)
            return false;
        auto &p = players[slot];
        if (!p)
            p = std::make_unique<kh2::MotionPlayer>(own(slot) ? *own_skeleton : *skeleton);
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
        if (!own(slot))
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
        const Mat4 taken = mul(turn, inverse_affine(m->root_motion(ticks))); // R T^-1 R^-1 times R g
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
    bool mset_own = false;
    std::vector<std::pair<int, int>> bones;
    std::vector<kh2::Retarget::Limb> limbs;
    struct Extra { // `motions <file> [<entry>] at <slot> [own]`
        std::string file, entry;
        std::size_t at = 0;
        bool own = false;
    };
    std::vector<Extra> extras;
    std::vector<std::array<int, 3>> carries; // `carry <bone> <by bone> at <slot>`
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
        if (a == "mset") {
            mset = b;
            mset_own = static_cast<bool>(w >> c) && c == "own";
        } else if (a == "motions") {
            Extra x{b, {}, 0, false};
            if (w >> c && c != "at") {
                x.entry = c;
                w >> c;
            }
            if (c == "at" && w >> d) {
                x.at = static_cast<std::size_t>(std::atoi(d.c_str()));
                x.own = static_cast<bool>(w >> r) && r == "own";
                extras.push_back(x);
            }
        } else if (a == "carry" && w >> c >> d >> r && d == "at") {
            carries.push_back({std::atoi(b.c_str()), std::atoi(c.c_str()), std::atoi(r.c_str())});
        } else if (a == "rotate_motions" && w >> c >> d) { // <x> <y> <z> [<height>], as models.cpp's turn_of
            const float degrees[3] = {std::strtof(b.c_str(), nullptr), std::strtof(c.c_str(), nullptr),
                                      std::strtof(d.c_str(), nullptr)};
            const float height = w >> r ? std::strtof(r.c_str(), nullptr) : 0.0F;
            kh2::Mat4 turn{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const float rad = degrees[axis] * 0.0174532925F, co = std::cos(rad), si = std::sin(rad);
                const std::size_t u = (axis + 1) % 3, v = (axis + 2) % 3;
                kh2::Mat4 one{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                one[u * 4 + u] = co;
                one[v * 4 + v] = co;
                one[u * 4 + v] = si;
                one[v * 4 + u] = -si;
                turn = mul(one, turn);
            }
            // about the point `height` cm over the root
            turn[12] = turn[4] * -height;
            turn[13] = turn[5] * -height + height;
            turn[14] = turn[6] * -height;
            m.turn = turn;
        } else if (a == "skeleton")
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
    m.own_skeleton = std::make_unique<kh2::MotionSkeleton>(target);
    const auto set = read_file(folder / mset);
    m.set = std::make_unique<kh2::MotionSet>(set.data(), set.size());
    if (mset_own)
        m.own_slots.emplace_back(0, m.set->slots());
    for (const Extra &x : extras) { // as models.cpp's load_kh2_motions
        const auto bytes = read_file(folder / x.file);
        if (x.entry.empty()) {
            m.set->add(x.at, kh2::MotionSet(bytes.data(), bytes.size()));
        } else {
            for (const kh2::BarEntry &e : kh2::bar_entries(bytes.data(), bytes.size()))
                if (e.name == x.entry && kh2::is_bar(bytes.data(), bytes.size(), e.offset)) {
                    m.set->add(x.at, kh2::MotionSet(bytes.data() + e.offset, e.size));
                    break;
                }
        }
        if (x.own && m.set->slots() > x.at)
            m.own_slots.emplace_back(x.at, m.set->slots());
    }
    if (skeleton.empty()) {
        m.skeleton = std::make_unique<kh2::MotionSkeleton>(target);
    } else {
        const auto other = read_file(folder / skeleton);
        m.skeleton = std::make_unique<kh2::MotionSkeleton>(kh2::read_motion_skeleton(other.data(), other.size()));
        std::vector<int> source_of(target.joints.size(), -1);
        for (const auto &[t, s] : bones)
            source_of[static_cast<std::size_t>(t)] = s;
        m.retarget = std::make_unique<kh2::Retarget>(*m.skeleton, target, source_of, limbs);
        for (const auto &[bone, by, at] : carries) { // the bone on the other where the own slot's first frame has it
            const kh2::Motion *motion = m.set->motion(static_cast<std::size_t>(at));
            if (motion == nullptr || !m.own(static_cast<std::size_t>(at)))
                throw std::runtime_error("carry " + std::to_string(bone) + " " + std::to_string(by) +
                                         ": no own motion in slot " + std::to_string(at));
            kh2::MotionPlayer p(target);
            const kh2::Mat4 unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            p.evaluate(*motion, 0.0F, unit, nullptr);
            const auto unscaled = [](Mat4 x) {
                for (std::size_t c = 0; c < 3; ++c) {
                    const float l = std::hypot(x[c * 4], x[c * 4 + 1], x[c * 4 + 2]);
                    for (std::size_t r = 0; r < 3 && l > 0.0F; ++r)
                        x[c * 4 + r] /= l;
                }
                return x;
            };
            m.retarget->carry(bone, by,
                              mul(inverse_affine(unscaled(p.joints()[static_cast<std::size_t>(by)])),
                                  unscaled(p.joints()[static_cast<std::size_t>(bone)])));
        }
    }
    return m;
}

// --- Scenarios: what the actor plays and where it is, second by second ------------------------------

struct Segment {
    float from;         // seconds into the scenario
    std::size_t slot;   // mset slot
    std::string name;   // the game animation's name the controls see
    float fade = 0.15F; // the cross-fade in (by matrices, as the game's)
    int days = -1;      // `--days`: the game's animation it plays instead (Days::anims), posed by models.cpp
    float speed = 0.0F; // `--days-scenario`: metres a second it moves forward (+z) meanwhile
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
    // running round a circle, turning all the while (the stick held to one side while the camera turns the
    // other way); KHDAYS_REPLAY_TURN=<degrees a second> (default 120), negative turning the other way
    static const float turn = [] {
        const char *v = std::getenv("KHDAYS_REPLAY_TURN");
        return (v != nullptr ? static_cast<float>(std::atof(v)) : 120.0F) * kPi / 180.0F;
    }();
    s.push_back({"circle",
                 8.0F,
                 {{0.0F, 0, "b_ro_A000"}, {1.0F, 10, "b_ro_A001"}, {7.0F, 0, "b_ro_A000", 0.1F}},
                 [](float t) {
                     constexpr float speed = 5.5F;
                     const float u = std::fmin(std::fmax(t - 1.0F, 0.0F), 6.0F), h = turn * u;
                     const float r = std::fabs(turn) > 1e-4F ? speed / turn : 0.0F;
                     const Vec3 at = std::fabs(turn) > 1e-4F ? Vec3{r * (1.0F - std::cos(h)), 0, r * std::sin(h)}
                                                             : Vec3{0, 0, speed * u};
                     return yaw_at(at, h);
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
    // `--hair`: its edges through the drawn body's triangles (most in a frame, frames with any, each hair bone and
    // body bone pair's count), and its vertices' frame-to-frame acceleration in its reference bone's frame (cm)
    int hair_cross_max = 0, hair_cross_frames = 0;
    std::map<std::pair<int, int>, int> hair_cross_pairs;
    float twitch_p99 = 0.0F, twitch_max = 0.0F;
    int twitch_bone = -1, twitch_frame = -1;
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

// `--days`: the game model the mod's model replaces (its file as the game's origin table names it, its nodes at rest)
// and the game's animations played on it, each by the name motions.txt and the controls know it
struct Days {
    std::string file;
    khdays::assets::NeutralModel game;
    std::vector<khdays::assets::SkeletalAnimation> anims;
    std::vector<std::string> names;
};

// `--hair`: the groups measured as hair (their vertices), the bones whose triangles are not the body (`--hair-ignore`:
// other hair, hidden weapons), the hair's edges, the body's triangles and the bone whose frame the twitch is read in
struct Hair {
    std::vector<std::uint8_t> vertex;
    std::vector<std::uint32_t> list; // its vertices
    std::vector<std::array<std::uint32_t, 2>> edges;
    std::vector<std::size_t> body; // offsets into the mesh's triangle indices
    int reference = -1;
};

struct Run {
    Model model;
    std::shared_ptr<Rig> rig;
    Motions motions;
    Days days;
    Hair hair;
};

// A `--days` segment's pose: the game's animation at its frame (30 a second, looped) on the game model's nodes, posed
// onto the mod's model by models.cpp, each joint at its KH2 bone (KH2 cm, model space as the motions' are)
// `from`: a game animation fading out (its segment and time) while `s` fades in by `u`, as the game cross-fades: both
// played at once by their shares, the nodes' local matrices mixed by them
void days_pose(Run &run, const Segment &s, float local, std::vector<kh2::Mat4> &now, const Segment *from = nullptr,
               float from_local = 0.0F, float u = 1.0F)
{
    const auto sampled = [&](const Segment &g, float at, float &frame) {
        const auto &anim = run.days.anims[static_cast<std::size_t>(g.days)];
        frame = at * 30.0F;
        if (anim.frame_count > 0)
            frame = std::fmod(frame, static_cast<float>(anim.frame_count));
        return khdays::assets::sample_animation(anim, frame, run.days.game.object_matrices);
    };
    float frame = 0.0F, from_frame = 0.0F;
    auto objects = sampled(s, local, frame);
    // each animation's name, frame, share and length in frames (a `follow` section stretches onto it)
    const auto frames_of = [&](const Segment &g) {
        return static_cast<float>(run.days.anims[static_cast<std::size_t>(g.days)].frame_count);
    };
    std::vector<std::tuple<std::string, float, float, float>> animations;
    if (from != nullptr && from->days >= 0 && u < 1.0F) {
        const auto out = sampled(*from, from_local, from_frame);
        for (std::size_t n = 0; n < objects.size() && n < out.size(); ++n)
            for (std::size_t k = 0; k < 16; ++k)
                objects[n][k] = out[n][k] * (1.0F - u) + objects[n][k] * u;
        animations.emplace_back(run.days.names[static_cast<std::size_t>(from->days)], from_frame, 1.0F - u,
                                frames_of(*from));
    }
    animations.emplace_back(run.days.names[static_cast<std::size_t>(s.days)], frame, animations.empty() ? 1.0F : u,
                            frames_of(s));
    const auto nodes = run.days.game.skinning != nullptr
                           ? khdays::assets::compute_bone_world_matrices(*run.days.game.skinning, objects)
                           : objects;
    replay_game_tick();
    std::vector<std::array<float, 16>> joints;
    std::vector<int> bones;
    if (!khdays_models_offline_pose(run.days.file.c_str(), 0, nodes, animations, joints, bones))
        throw std::runtime_error("models.cpp drew no mod model for " + run.days.file +
                                 ": give the model from its mod's graphics folder and the game model it replaces");
    static std::vector<kh2::Mat4> last; // KHDAYS_REPLAY_DAYS_TRACE: the joint that moved most since the last draw
    now.assign(run.model.skeleton.rest.size(), kh2::Mat4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    for (std::size_t j = 0; j < joints.size(); ++j) {
        const int b = bones[j];
        if (b < 0 || static_cast<std::size_t>(b) >= now.size())
            continue;
        now[static_cast<std::size_t>(b)] = joints[j];
        for (std::size_t c = 12; c < 15; ++c)
            now[static_cast<std::size_t>(b)][c] *= 100.0F;
    }
    // KHDAYS_REPLAY_DAYS_BONES=<bone>,...: each listed KH2 bone's place (cm, model space) at every frame
    if (const char *list = std::getenv("KHDAYS_REPLAY_DAYS_BONES"); list != nullptr) {
        std::string line;
        std::istringstream bones_in(list);
        for (std::string b; std::getline(bones_in, b, ',');) {
            const int bone = std::atoi(b.c_str());
            if (bone < 0 || static_cast<std::size_t>(bone) >= now.size())
                continue;
            const auto &w = now[static_cast<std::size_t>(bone)];
            char one[96];
            std::snprintf(one, sizeof(one), " kh2_%d %.1f %.1f %.1f", bone, static_cast<double>(w[12]),
                          static_cast<double>(w[13]), static_cast<double>(w[14]));
            line += one;
        }
        std::fprintf(stderr, "days bones %s frame %.1f:%s\n", run.days.names[static_cast<std::size_t>(s.days)].c_str(),
                     static_cast<double>(frame), line.c_str());
    }
    if (std::getenv("KHDAYS_REPLAY_DAYS_TRACE") != nullptr) {
        if (last.size() == now.size()) {
            float most = 0.0F;
            std::size_t at = 0;
            for (std::size_t b = 0; b < now.size(); ++b) {
                const float d =
                    std::hypot(now[b][12] - last[b][12], now[b][13] - last[b][13], now[b][14] - last[b][14]);
                if (d > most) {
                    most = d;
                    at = b;
                }
            }
            std::fprintf(stderr, "days %s frame %.1f: kh2_%zu moved %.1f cm\n",
                         run.days.names[static_cast<std::size_t>(s.days)].c_str(), static_cast<double>(frame), at,
                         static_cast<double>(most));
        }
        last = now;
    }
}

// Whether the segment p-q passes through the triangle a b c (Moller-Trumbore, the segment's ends excluded)
bool crosses(Vec3 p, Vec3 q, Vec3 a, Vec3 b, Vec3 c)
{
    const Vec3 d = q - p, e1 = b - a, e2 = c - a, h = cross(d, e2);
    const float det = dot(e1, h);
    if (std::fabs(det) < 1e-12F)
        return false;
    const float inv = 1.0F / det;
    const Vec3 s = p - a;
    const float u = dot(s, h) * inv;
    if (u < 0.0F || u > 1.0F)
        return false;
    const Vec3 qv = cross(s, e1);
    const float v = dot(d, qv) * inv;
    if (v < 0.0F || u + v > 1.0F)
        return false;
    const float t = dot(e2, qv) * inv;
    return t > 1e-4F && t < 1.0F - 1e-4F;
}

// `--hair`'s twitch: its vertices' places in the reference bone's frame the last two frames, each frame's largest
// acceleration
struct TwitchState {
    std::vector<Vec3> x1, x2;
    std::vector<float> values;
};

// `--hair`, one frame of the drawn model: the hair's edges through the body's triangles (a grid of 4 cm cells finds
// the candidates), and how sharply its vertices move in its reference bone's frame (their second difference, cm)
void measure_hair(const Run &run, const std::vector<Frame> &bones, const std::vector<Vec3> &verts, int frame,
                  Metrics &m, TwitchState &st)
{
    const auto &tri = run.model.mesh.triangles;
    constexpr float kCell = 0.04F;
    const auto cell_of = [](float v) { return static_cast<int>(std::floor(v / kCell)); };
    const auto key = [](int x, int y, int z) {
        return (static_cast<std::int64_t>(x) * 73856093LL) ^ (static_cast<std::int64_t>(y) * 19349663LL) ^
               (static_cast<std::int64_t>(z) * 83492791LL);
    };
    const auto lowest = [](Vec3 a, Vec3 b) {
        return Vec3{std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)};
    };
    const auto highest = [](Vec3 a, Vec3 b) {
        return Vec3{std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)};
    };
    std::map<std::int64_t, std::vector<std::size_t>> grid;
    for (const std::size_t t : run.hair.body) {
        const Vec3 a = verts[tri[t]], b = verts[tri[t + 1]], c = verts[tri[t + 2]];
        const Vec3 lo = lowest(lowest(a, b), c), hi = highest(highest(a, b), c);
        for (int x = cell_of(lo.x); x <= cell_of(hi.x); ++x)
            for (int y = cell_of(lo.y); y <= cell_of(hi.y); ++y)
                for (int z = cell_of(lo.z); z <= cell_of(hi.z); ++z)
                    grid[key(x, y, z)].push_back(t);
    }
    int count = 0;
    std::vector<std::size_t> candidates;
    for (const auto &e : run.hair.edges) {
        const Vec3 p = verts[e[0]], q = verts[e[1]];
        const Vec3 lo = lowest(p, q), hi = highest(p, q);
        candidates.clear();
        for (int x = cell_of(lo.x); x <= cell_of(hi.x); ++x)
            for (int y = cell_of(lo.y); y <= cell_of(hi.y); ++y)
                for (int z = cell_of(lo.z); z <= cell_of(hi.z); ++z)
                    if (const auto it = grid.find(key(x, y, z)); it != grid.end())
                        candidates.insert(candidates.end(), it->second.begin(), it->second.end());
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        for (const std::size_t t : candidates)
            if (crosses(p, q, verts[tri[t]], verts[tri[t + 1]], verts[tri[t + 2]])) {
                ++count;
                ++m.hair_cross_pairs[{run.model.dominant[e[0]], run.model.dominant[tri[t]]}];
            }
    }
    m.hair_cross_max = std::max(m.hair_cross_max, count);
    m.hair_cross_frames += count > 0 ? 1 : 0;
    if (run.hair.reference < 0)
        return;
    const Frame &r = bones[static_cast<std::size_t>(run.hair.reference)];
    std::vector<Vec3> x(run.hair.list.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = apply_inverse(r, verts[run.hair.list[i]]);
    if (st.x2.size() == x.size() && frame >= 15) { // the first quarter second: the cloth settling onto its pose
        float worst = 0.0F;
        int bone = -1;
        for (std::size_t i = 0; i < x.size(); ++i) {
            const float a = length(x[i] - st.x1[i] * 2.0F + st.x2[i]) * 100.0F;
            if (a > worst) {
                worst = a;
                bone = run.model.dominant[run.hair.list[i]];
            }
        }
        st.values.push_back(worst);
        if (worst > m.twitch_max) {
            m.twitch_max = worst;
            m.twitch_bone = bone;
            m.twitch_frame = frame;
        }
    }
    st.x2 = st.x1;
    st.x1 = std::move(x);
}

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
    TwitchState twitch_state;
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
        // a fade between two game animations: models.cpp plays both, as in the game (its KH2 motions' own blends too)
        const bool game_fade = s.days >= 0 && seg > 0 && sc.segments[seg - 1].days >= 0 && local < seg_start_pose_fade;
        if (game_fade)
            days_pose(run, s, local, now, &sc.segments[seg - 1], t - sc.segments[seg - 1].from,
                      local / seg_start_pose_fade);
        else if (s.days >= 0)
            days_pose(run, s, local, now);
        else
            run.motions.pose(s.slot, local * 60.0F, now);
        if (!game_fade && !fade_from.empty() && local < seg_start_pose_fade) {
            const float u = local / seg_start_pose_fade;
            for (std::size_t b = 0; b < now.size(); ++b)
                for (std::size_t k = 0; k < 16; ++k)
                    now[b][k] = fade_from[b][k] * (1.0F - u) + now[b][k] * u;
        }
        before = now;
        // KHDAYS_REPLAY_ROLL=<degrees>: the whole actor turned about its x (a weapon held tip down)
        static const float roll = std::getenv("KHDAYS_REPLAY_ROLL") != nullptr
                                      ? static_cast<float>(std::atof(std::getenv("KHDAYS_REPLAY_ROLL"))) * kDegrees
                                      : 0.0F;
        // KHDAYS_REPLAY_LIFT=<m>: raised that far (a weapon, whose origin is its grip, at a hand's height)
        static const float lift = std::getenv("KHDAYS_REPLAY_LIFT") != nullptr
                                      ? static_cast<float>(std::atof(std::getenv("KHDAYS_REPLAY_LIFT")))
                                      : 0.0F;
        Frame place = sc.place(t);
        place.q = normalized(place.q * axis_angle({1.0F, 0.0F, 0.0F}, roll));
        place.t.y += lift;
        // KHDAYS_REPLAY_NOISE=<mm>: shaken by up to that much each draw (the game's fixed-point rounding)
        if (const char *nz = std::getenv("KHDAYS_REPLAY_NOISE"); nz != nullptr) {
            static unsigned seed = 12345U;
            const auto r = [&] {
                seed = seed * 1664525U + 1013904223U;
                return (static_cast<float>(seed >> 8) / 16777216.0F - 0.5F) * 2.0F;
            };
            const float mm = static_cast<float>(std::atof(nz)) * 0.001F;
            place.t = place.t + Vec3{r() * mm, r() * mm, r() * mm};
            place.q = normalized(place.q * axis_angle(normalized(Vec3{r(), r(), r()}, {1, 0, 0}), r() * mm * 0.5F));
        }
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
        in.animations = {{s.name, s.days >= 0 ? -1 : static_cast<int>(s.slot), -1.0F, local, 0.0F, 1.0F}};
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
        if (!run.hair.vertex.empty())
            measure_hair(run, shown, verts, f, m, twitch_state);
        // KHDAYS_REPLAY_SPREAD=<cm>[,<bone>-<bone>...]: each frame whose farthest vertex from the root bone is past that,
        // with its bone (a mesh stretched off the body); the bones after it left out (hidden weapons)
        if (const char *e = std::getenv("KHDAYS_REPLAY_SPREAD"); e != nullptr && !verts.empty()) {
            static const std::vector<std::pair<int, int>> left_out = [e] {
                std::vector<std::pair<int, int>> r;
                for (const char *c = std::strchr(e, ','); c != nullptr; c = std::strchr(c + 1, ',')) {
                    int a = 0, b = 0;
                    if (std::sscanf(c + 1, "%d-%d", &a, &b) == 2)
                        r.emplace_back(a, b);
                    else if (std::sscanf(c + 1, "%d", &a) == 1)
                        r.emplace_back(a, a);
                }
                return r;
            }();
            const Vec3 root = shown[0].t;
            float most = 0.0F;
            std::size_t at = 0;
            for (std::size_t v = 0; v < verts.size(); ++v)
                if (const float d = length(verts[v] - root);
                    d > most && std::none_of(left_out.begin(), left_out.end(), [&](const auto &r) {
                        return run.model.dominant[v] >= r.first && run.model.dominant[v] <= r.second;
                    })) {
                    most = d;
                    at = v;
                }
            if (most * 100.0F > static_cast<float>(std::atof(e)))
                std::fprintf(stderr, "spread %s frame %d (%.2f s, segment %zu): %.0f cm, kh2_%d\n", sc.name.c_str(), f,
                             static_cast<double>(t), seg, static_cast<double>(most * 100.0F), run.model.dominant[at]);
        }
        // KHDAYS_REPLAY_MODEL_DUMP=<frame>: that frame's drawn model, to fit colliders to: "v x y z bone" for each vertex
        // (metres, its heaviest bone) and "b index" with the 16 numbers of each bone's frame (column-major)
        if (const char *dump = std::getenv("KHDAYS_REPLAY_MODEL_DUMP"); dump != nullptr && std::atoi(dump) == f) {
            std::string file_name = "dump_" + sc.name + "_" + std::to_string(f) + ".txt";
            std::replace(file_name.begin(), file_name.end(), ':', '_');
            if (FILE *d = std::fopen((folder / file_name).string().c_str(), "w"); d != nullptr) {
                for (std::size_t v = 0; v < verts.size(); ++v)
                    std::fprintf(d, "v %.5f %.5f %.5f %d\n", static_cast<double>(verts[v].x),
                                 static_cast<double>(verts[v].y), static_cast<double>(verts[v].z),
                                 run.model.dominant[v]);
                for (std::size_t b = 0; b < shown.size(); ++b) {
                    const Mat4 x = matrix_of(shown[b]);
                    std::fprintf(d, "b %zu", b);
                    for (const float e : x)
                        std::fprintf(d, " %.5f", static_cast<double>(e));
                    std::fprintf(d, "\n");
                }
                std::fclose(d);
            }
        }
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
                                 static_cast<double>(cmove * 100.0F),
                                 rig.colliders.empty() ? "-" : rig.colliders[cwhich].name.c_str());
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
                                 gap * 100.0F, rig.colliders.empty() ? "-" : rig.colliders[near].name.c_str());
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
                        static const std::string measured =
                            std::getenv("KHDAYS_REPLAY_GROUP") != nullptr ? std::getenv("KHDAYS_REPLAY_GROUP") : "coat";
                        if (rig.groups[static_cast<std::size_t>(sb.group)].name == measured &&
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
            // KHDAYS_REPLAY_TWIST=1: the measured group's bones' largest roll about their own length since the last
            // frame, and how far their length turned meanwhile (a flip: a large roll, the length nearly still)
            if (std::getenv("KHDAYS_REPLAY_TWIST") != nullptr && jq1.size() == out.size()) {
                const std::string measured =
                    std::getenv("KHDAYS_REPLAY_GROUP") != nullptr ? std::getenv("KHDAYS_REPLAY_GROUP") : "coat";
                float most_roll = 0.0F, swing = 0.0F;
                std::size_t rb = 0;
                for (const auto &sb : rig.bones_out) {
                    const auto b = static_cast<std::size_t>(sb.bone);
                    if (rig.groups[static_cast<std::size_t>(sb.group)].name != measured)
                        continue;
                    const Vec3 was = rotate(jq1[b], Vec3{1, 0, 0}), moved = rotate(q[b], Vec3{1, 0, 0});
                    const Quat rest = normalized(conjugate(arc(was, moved) * jq1[b]) * q[b]); // what is left: the roll
                    const float r = 2.0F * std::acos(std::fmin(1.0F, std::fabs(rest.w))) * 57.2958F;
                    if (r > most_roll) {
                        most_roll = r;
                        rb = b;
                        swing = std::acos(std::fmax(-1.0F, std::fmin(1.0F, dot(was, moved)))) * 57.2958F;
                    }
                }
                std::fprintf(stderr, "jtwist %s f%d %.1f deg %s (its length %.1f deg)\n", sc.name.c_str(), f,
                             static_cast<double>(most_roll), rig.skeleton.names[rb].c_str(),
                             static_cast<double>(swing));
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
                const float deeper = pen - std::fmax(0.0F, run.model.rest_depth[v * nc + ci]);
                // KHDAYS_REPLAY_DEBUG: each new deepest vertex past its rest depth, its bone and collider
                static float pen_worst = 0.0F;
                if (deeper > pen_worst && std::getenv("KHDAYS_REPLAY_DEBUG") != nullptr && (pen_worst = deeper) > 0.0F)
                    std::fprintf(stderr, "penetration: f%d vertex %zu bone %s collider %s %.2f cm\n", f, v,
                                 rig.skeleton.names[static_cast<std::size_t>(run.model.dominant[v])].c_str(),
                                 rig.colliders[ci].name.c_str(), static_cast<double>(deeper * 100.0F));
                frame_pen = std::fmax(frame_pen, deeper);
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
    m.twitch_p99 = pct(twitch_state.values, 0.99F);
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
    // N_EX830, B_EX390 and P_EX100 share bones 0-94 (body and coat); a reference with this model's bones shares all
    const std::size_t shared = sk.joints.size() == nb ? nb : 95;
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
            in.animations = {
                {"reference", static_cast<int>(slot), -1.0F, ticks / 60.0F, m->length_ticks() / 60.0F, 1.0F}};
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
                             "[--slot-offset n] [--reference B.mdlx B.mset slot,slot] [--days game.nsbmd "
                             "[--days-anim name=file.nsbca#index]... [--days-scenario name=anim:seconds[:m/s],...]...] "
                             "[--hair group,group [--hair-ignore bones]]\n");
        return 2;
    }
    std::filesystem::path days_bmd;
    std::vector<std::string> days_anim_specs, days_scenario_specs;
    std::string hair_groups, hair_ignore;
    std::filesystem::path out = ".";
    std::string which = "all";
    int actors = 1, repeat = 1;
    float hz = 60.0F;
    std::filesystem::path ref_mdlx, ref_mset;
    std::vector<std::size_t> ref_slots;
    std::size_t slot_offset = 0;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc)
            out = argv[++i];
        else if (a == "--slot-offset" && i + 1 < argc)
            slot_offset = static_cast<std::size_t>(std::max(0, std::atoi(argv[++i])));
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
        } else if (a == "--days" && i + 1 < argc) {
            days_bmd = argv[++i];
        } else if (a == "--days-anim" && i + 1 < argc) {
            days_anim_specs.push_back(argv[++i]);
        } else if (a == "--days-scenario" && i + 1 < argc) {
            days_scenario_specs.push_back(argv[++i]);
        } else if (a == "--hair" && i + 1 < argc) {
            hair_groups = argv[++i];
        } else if (a == "--hair-ignore" && i + 1 < argc) {
            hair_ignore = argv[++i];
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
        // `--days`: the mod's model as models.cpp finds it (<mods>/<mod>/graphics/<name>.mdlx, the game model's file
        // the name up to its "/mdl/"), the game model's bytes, and the game's animations
        if (!days_bmd.empty()) {
            const std::filesystem::path model = std::filesystem::absolute(argv[1]);
            std::vector<std::filesystem::path> parts(model.begin(), model.end());
            const auto graphics = std::find(parts.begin(), parts.end(), std::filesystem::path("graphics"));
            if (graphics == parts.end() || graphics - parts.begin() < 2)
                throw std::runtime_error("--days: give the model from its mod's folder (<mods>/<mod>/graphics/...)");
            std::filesystem::path mods;
            for (auto p = parts.begin(); p != graphics - 1; ++p)
                mods /= *p;
            std::string name;
            for (auto p = graphics + 1; p != parts.end(); ++p)
                name += (name.empty() ? "" : "/") + p->generic_string();
            name = name.substr(0, name.size() - model.extension().string().size());
            const auto mdl = name.find("/mdl/");
            if (mdl == std::string::npos)
                throw std::runtime_error("--days: the model's path has no /mdl/ folder: " + name);
            run.days.file = name.substr(0, mdl);
            const auto bmd = read_file(days_bmd);
            run.days.game = khdays::assets::decode_model_geometry(bmd.data(), bmd.size(), 0);
            replay_game_set_model(run.days.file, 0, name, bmd, mods);
            for (const std::string &spec : days_anim_specs) {
                const auto eq = spec.find('='), hash = spec.rfind('#');
                if (eq == std::string::npos || hash == std::string::npos || hash < eq)
                    throw std::runtime_error("--days-anim " + spec + ": write name=file.nsbca#index");
                const auto bytes = read_file(spec.substr(eq + 1, hash - eq - 1));
                run.days.anims.push_back(khdays::assets::load_nsbca(
                    bytes.data(), bytes.size(), static_cast<std::size_t>(std::atoi(spec.c_str() + hash + 1))));
                run.days.names.push_back(spec.substr(0, eq));
            }
        }
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
        // `--hair`: those groups' bones' vertices; the body's triangles, none of whose corners is hair or on an
        // ignored bone; the hair's triangles' edges; the first group's reference bone
        if (!hair_groups.empty()) {
            std::vector<std::uint8_t> hair_bone(run.model.skeleton.names.size(), 0), ignored(hair_bone.size(), 0);
            const std::string groups = "," + hair_groups + ",";
            for (const auto &s : run.rig->bones_out)
                if (groups.find("," + run.rig->groups[static_cast<std::size_t>(s.group)].name + ",") !=
                    std::string::npos) {
                    hair_bone[static_cast<std::size_t>(s.bone)] = 1;
                    if (run.hair.reference < 0)
                        run.hair.reference = run.rig->groups[static_cast<std::size_t>(s.group)].reference_bone;
                }
            for (const auto &c : run.rig->carried)
                if (hair_bone[static_cast<std::size_t>(run.rig->bones_out[static_cast<std::size_t>(c.by)].bone)])
                    hair_bone[static_cast<std::size_t>(c.bone)] = 1;
            std::istringstream spans(hair_ignore);
            for (std::string w; std::getline(spans, w, ',');) {
                int from = 0, to = 0;
                if (std::sscanf(w.c_str(), "%d-%d", &from, &to) != 2)
                    to = from = std::atoi(w.c_str());
                for (int b = from; b <= to; ++b)
                    if (b >= 0 && static_cast<std::size_t>(b) < ignored.size())
                        ignored[static_cast<std::size_t>(b)] = 1;
            }
            const std::size_t nv = run.model.mesh.vertices.size();
            run.hair.vertex.assign(nv, 0);
            for (std::size_t v = 0; v < nv; ++v) {
                const int b = run.model.dominant[v];
                if (b >= 0 && hair_bone[static_cast<std::size_t>(b)]) {
                    run.hair.vertex[v] = 1;
                    run.hair.list.push_back(static_cast<std::uint32_t>(v));
                }
            }
            const auto &tri = run.model.mesh.triangles;
            std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
            for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
                bool hair = true, body = true;
                for (std::size_t k = 0; k < 3; ++k) {
                    const std::uint32_t v = tri[t + k];
                    const int b = run.model.dominant[v];
                    hair = hair && run.hair.vertex[v];
                    body = body && !run.hair.vertex[v] && !(b >= 0 && ignored[static_cast<std::size_t>(b)]);
                }
                if (body)
                    run.hair.body.push_back(t);
                if (hair)
                    for (std::size_t k = 0; k < 3; ++k) {
                        const std::uint32_t a = tri[t + k], b = tri[t + (k + 1) % 3];
                        edges.insert({std::min(a, b), std::max(a, b)});
                    }
            }
            for (const auto &[a, b] : edges)
                run.hair.edges.push_back({a, b});
            std::fprintf(stderr, "hair: %zu vertices, %zu edges against %zu body triangles\n", run.hair.list.size(),
                         run.hair.edges.size(), run.hair.body.size());
        }
        FILE *report = nullptr;
        if (fopen_s(&report, (out / "report.txt").string().c_str(), "w") != 0 || report == nullptr)
            report = stdout;
        std::fprintf(report, "%s\ncook and prepare: %.1f ms\n", run.rig->summary.c_str(), cook_ms);
        std::fprintf(report, "settings: %.0f Hz, %d substeps, %d iterations; draws at %.0f Hz x%d; %d actor(s)\n",
                     static_cast<double>(run.rig->config.rate), run.rig->config.substeps, run.rig->config.iterations,
                     static_cast<double>(hz), repeat, actors);
        Profile all;
        // `--days`: each game animation alone (two loops, 4 s at least), and `--days-scenario`s, one after another
        std::vector<Scenario> list = scenarios();
        const auto days_index = [&](const std::string &name) {
            const auto it = std::find(run.days.names.begin(), run.days.names.end(), name);
            if (it == run.days.names.end())
                throw std::runtime_error("--days-scenario: no --days-anim named " + name);
            return static_cast<int>(it - run.days.names.begin());
        };
        for (std::size_t d = 0; d < run.days.anims.size(); ++d) {
            const float seconds = std::max(4.0F, 2.0F * static_cast<float>(run.days.anims[d].frame_count) / 30.0F);
            list.push_back({"days:" + run.days.names[d],
                            seconds,
                            {{0.0F, 0, run.days.names[d], 0.15F, static_cast<int>(d), 0.0F}},
                            [](float) { return yaw_at({}, 0.0F); },
                            floor_plane(20),
                            nullptr});
        }
        for (const std::string &spec : days_scenario_specs) {
            const auto eq = spec.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error("--days-scenario " + spec + ": write name=anim:seconds[:m/s],...");
            Scenario sc{"days:" + spec.substr(0, eq), 0.0F, {}, nullptr, floor_plane(20), nullptr};
            std::istringstream parts(spec.substr(eq + 1));
            for (std::string part; std::getline(parts, part, ',');) {
                std::istringstream fields(part);
                std::string anim, seconds, speed;
                std::getline(fields, anim, ':');
                std::getline(fields, seconds, ':');
                std::getline(fields, speed, ':');
                sc.segments.push_back({sc.seconds, 0, anim, 0.15F, days_index(anim),
                                       speed.empty() ? 0.0F : static_cast<float>(std::atof(speed.c_str()))});
                sc.seconds += static_cast<float>(std::atof(seconds.c_str()));
            }
            const std::vector<Segment> segments = sc.segments;
            sc.place = [segments](float t) { // forward by each segment's speed
                float z = 0.0F;
                for (std::size_t i = 0; i < segments.size(); ++i) {
                    const float end = i + 1 < segments.size() ? segments[i + 1].from : t;
                    z += segments[i].speed * std::fmax(0.0F, std::fmin(t, end) - segments[i].from);
                }
                return yaw_at({0.0F, 0.0F, z}, 0.0F);
            };
            list.push_back(std::move(sc));
        }
        for (auto sc : list) {
            if (which != "all" && which != sc.name && !(which == "days" && sc.name.rfind("days:", 0) == 0))
                continue;
            for (Segment &s : sc.segments) // P_EX110's slots where motions.txt adds its mset from a slot on
                s.slot += slot_offset;
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
            if (!run.hair.vertex.empty()) {
                std::vector<std::pair<int, std::pair<int, int>>> worst;
                for (const auto &[pair, n] : m.hair_cross_pairs)
                    worst.push_back({n, pair});
                std::sort(worst.rbegin(), worst.rend());
                std::string top;
                for (std::size_t i = 0; i < worst.size() && i < 6; ++i)
                    top += (i ? ", " : "") + std::string("kh2_") + std::to_string(worst[i].second.first) +
                           " through kh2_" + std::to_string(worst[i].second.second) + " " +
                           std::to_string(worst[i].first);
                std::fprintf(report,
                             "          hair: through the body %d edges at most in a frame, %d frames with any%s%s; "
                             "twitch %.2f cm p99, %.2f max (kh2_%d, frame %d)\n",
                             m.hair_cross_max, m.hair_cross_frames, top.empty() ? "" : "; most: ", top.c_str(),
                             static_cast<double>(m.twitch_p99), static_cast<double>(m.twitch_max), m.twitch_bone,
                             m.twitch_frame);
            }
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
