#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "khdays/assets/kh2_motion.h"

// KH2 motions made here, byte by byte: an .mdlx's skeleton and an .mset of one
// motion, played by MotionPlayer. Times are KH2's ticks (60 a second).

namespace {

namespace kh2 = khdays::assets::kh2;
using Bytes = std::vector<std::uint8_t>;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

bool near(float a, float b, float tolerance = 1e-3F) {
    return std::fabs(a - b) < tolerance;
}

struct Writer final {
    Bytes b;
    std::size_t size() const {
        return b.size();
    }
    void u8(unsigned v) {
        b.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    }
    void u16(unsigned v) {
        u8(v);
        u8(v >> 8U);
    }
    void u32(std::uint32_t v) {
        u16(v & 0xFFFFU);
        u16(v >> 16U);
    }
    void f32(float f) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        u32(bits);
    }
    void zeros(std::size_t n) {
        b.insert(b.end(), n, 0);
    }
    void pad(std::size_t align) {
        while (b.size() % align != 0) {
            u8(0);
        }
    }
    void set_u32(std::size_t at, std::uint32_t v) {
        for (std::size_t i = 0; i < 4; ++i) {
            b.at(at + i) = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
        }
    }
};

Bytes bar(const std::vector<std::pair<std::uint32_t, Bytes>>& entries) {
    Writer w;
    w.u32(0x01524142U);  // "BAR\x01"
    w.u32(static_cast<std::uint32_t>(entries.size()));
    w.zeros(8);
    const std::size_t table = w.size();
    w.zeros(entries.size() * 16U);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        w.pad(16);
        w.set_u32(table + i * 16U, entries[i].first);
        w.set_u32(table + i * 16U + 8, static_cast<std::uint32_t>(w.size()));
        w.set_u32(table + i * 16U + 12, static_cast<std::uint32_t>(entries[i].second.size()));
        w.b.insert(w.b.end(), entries[i].second.begin(), entries[i].second.end());
    }
    return w.b;
}

struct Bone final {
    int parent;
    float length;
    float x, y;  // its translation
};

// An .mdlx whose model is only its bones (kn::ModelHRC, 0x40 bytes each).
Bytes mdlx(const std::vector<Bone>& bones) {
    Writer w;
    w.zeros(0x90);
    w.u32(3);
    w.zeros(12);
    w.u16(static_cast<unsigned>(bones.size()));
    w.u16(0);
    w.u32(0x20);  // bone table
    w.u32(0);
    w.u32(0);
    for (std::size_t i = 0; i < bones.size(); ++i) {
        w.u32(static_cast<std::uint32_t>(i));
        w.u32(static_cast<std::uint32_t>(bones[i].parent));
        w.zeros(8);
        for (const float f :
             {1.0F, 1.0F, 1.0F, bones[i].length, 0.0F, 0.0F, 0.0F, 0.0F, bones[i].x, bones[i].y, 0.0F, 1.0F}) {
            w.f32(f);
        }
    }
    return bar({{0x04, w.b}});
}

struct Curve final {
    int joint;
    int track;
    int post;                                 // 0 hold, 1 slope, 2 repeat
    std::vector<std::pair<int, float>> keys;  // (time index, value), each key's type below
    int type;                                 // 0 constant, 1 linear, 2 Hermite
};

struct Constraint final {
    int type, passive, active;
};

// One motion's bytes (kn::MotionPrototype0RAW): `flags` the joints in order (with IK
// bits), `times` the time table, a root motion moving x by `root_x`.
Bytes motion_body(int joints, const std::vector<std::uint32_t>& flags, const std::vector<float>& times,
                  const std::vector<Curve>& curves, const std::vector<Constraint>& constraints, float root_x) {
    Writer m;
    m.zeros(0xB0);
    const auto here = [&] { return static_cast<std::uint32_t>(m.size()); };
    m.set_u32(0x10, static_cast<std::uint32_t>(joints | (joints << 16)));  // model and calc joints
    m.set_u32(0x18, here());                                               // no helpers
    m.set_u32(0x1C, here());
    for (const std::uint32_t f : flags) {
        m.u32(f);
    }
    m.set_u32(0x20, static_cast<std::uint32_t>(times.size()));
    m.set_u32(0x44, here());
    for (const float t : times) {
        m.f32(t);
    }
    std::vector<float> values;
    m.set_u32(0x40, here());
    for (const Curve& c : curves) {
        for (const auto& [time, value] : c.keys) {
            m.u16(static_cast<unsigned>((time << 2) | c.type));
            m.u16(static_cast<unsigned>(values.size()));
            m.u16(0);
            m.u16(0);
            values.push_back(value);
        }
    }
    m.set_u32(0x30, here());
    m.set_u32(0x34, static_cast<std::uint32_t>(curves.size()));
    int key = 0;
    for (const Curve& c : curves) {
        m.u16(static_cast<unsigned>(c.joint));
        m.u8(static_cast<unsigned>(c.track | (c.post << 6)));
        m.u8(static_cast<unsigned>(c.keys.size()));
        m.u16(static_cast<unsigned>(key));
        key += static_cast<int>(c.keys.size());
    }
    m.pad(4);
    m.set_u32(0x48, here());
    for (const float v : values) {
        m.f32(v);
    }
    m.set_u32(0x4C, here());
    m.f32(0.0F);  // the slopes: one, 0
    m.set_u32(0x50, here());
    m.set_u32(0x54, static_cast<std::uint32_t>(constraints.size()));
    for (const Constraint& c : constraints) {
        m.u8(static_cast<unsigned>(c.type));
        m.u8(1);  // active
        m.u16(static_cast<unsigned>(c.passive));
        m.u16(static_cast<unsigned>(c.active));
        m.u16(0);
        m.u16(0);
        m.u16(0);
    }
    for (const std::size_t field : {0x24, 0x38, 0x58, 0x5C, 0x60, 0x68, 0xA0}) {
        m.set_u32(field, here());  // empty tables
    }
    m.set_u32(0x2C, here());  // PositionInfo: scale 1, not_unit, rotation 0, translation x, no curves
    for (const float f : {1.0F, 1.0F, 1.0F}) {
        m.f32(f);
    }
    m.u32(root_x != 0.0F ? 1U : 0U);
    m.zeros(16);
    for (const float f : {root_x, 0.0F, 0.0F, 1.0F}) {
        m.f32(f);
    }
    for (int i = 0; i < 9; ++i) {
        m.u32(0xFFFFFFFFU);
    }
    const auto set_f = [&](std::size_t at, float f) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        m.set_u32(at, bits);
    };
    set_f(0x94, 30.0F);  // end
    set_f(0x98, 30.0F);  // fps
    return m.b;
}

// A motion's bytes as the only motion of an .mset, and an empty slot after it.
Bytes in_set(const Bytes& body) {
    Bytes motion(0x90, 0);
    motion.insert(motion.end(), body.begin(), body.end());
    return bar({{0x11, bar({{0x09, motion}})}, {0x10011, {}}});
}

Bytes mset(int joints, const std::vector<std::uint32_t>& flags, const std::vector<float>& times,
           const std::vector<Curve>& curves, const std::vector<Constraint>& constraints, float root_x) {
    return in_set(motion_body(joints, flags, times, curves, constraints, root_x));
}

const kh2::Mat4& joint(const kh2::MotionPlayer& p, std::size_t j) {
    return p.joints().at(j);
}

void test_curves() {
    // joint 1 turns about z from 0 to 90 degrees over 60 ticks, linearly, then holds
    const Bytes model = mdlx({{-1, 0.0F, 0.0F, 0.0F}, {0, 10.0F, 10.0F, 0.0F}});
    const Bytes set = mset(2, {0, 1}, {0.0F, 60.0F}, {{1, 5, 0, {{0, 0.0F}, {1, 1.5707964F}}, 1}}, {}, 5.0F);
    const kh2::MotionSet motions(set.data(), set.size());
    expect(motions.slots() == 2 && motions.motion(0) != nullptr && motions.motion(1) == nullptr,
           "the motion in slot 0, none in the empty slot");
    kh2::MotionPlayer player(kh2::read_motion_skeleton(model.data(), model.size()));
    constexpr kh2::Mat4 kWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    player.evaluate(*motions.motion(0), 30.0F, kWorld, nullptr);
    expect(near(joint(player, 1)[0], std::cos(0.7853982F)) && near(joint(player, 1)[1], std::sin(0.7853982F)),
           "halfway: turned 45 degrees");
    expect(near(joint(player, 1)[12], 10.0F) && near(joint(player, 1)[13], 0.0F), "at its place under its parent");
    player.evaluate(*motions.motion(0), 90.0F, kWorld, nullptr);
    expect(near(joint(player, 1)[0], 0.0F) && near(joint(player, 1)[1], 1.0F), "after the last key: held");
    const kh2::Mat4 root = motions.motion(0)->root_motion(0.0F);
    expect(near(root[12], 5.0F) && near(root[0], 1.0F), "the root motion's translation");
}

void test_ik() {
    // thigh (1) and shin (2), 10 long each, bent so the foot (3) reaches the target (4)
    const Bytes model = mdlx({{-1, 0.0F, 0.0F, 0.0F},
                              {0, 10.0F, 0.0F, 0.0F},
                              {1, 10.0F, 10.0F, 0.0F},
                              {2, 0.0F, 10.0F, 0.0F},
                              {0, 0.0F, 12.0F, -5.0F}});
    const std::uint32_t ik2 = 2U << 16U, calculated = 1U << 21U;
    const Bytes set = mset(5, {0, 4, 1 | ik2, 2 | calculated, 3}, {0.0F}, {}, {{0, 3, 4}}, 0.0F);
    const kh2::MotionSet motions(set.data(), set.size());
    kh2::MotionPlayer player(kh2::read_motion_skeleton(model.data(), model.size()));
    constexpr kh2::Mat4 kWorld{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    player.evaluate(*motions.motion(0), 0.0F, kWorld, nullptr);
    const kh2::Mat4& foot = joint(player, 3);
    expect(near(foot[12], 12.0F, 1e-2F) && near(foot[13], -5.0F, 1e-2F) && near(foot[14], 0.0F, 1e-2F),
           "the foot on the target");
    const kh2::Mat4& knee = joint(player, 2);
    const float thigh = std::hypot(knee[12], knee[13]);
    const float shin = std::hypot(foot[12] - knee[12], foot[13] - knee[13]);
    expect(near(thigh, 10.0F, 1e-2F) && near(shin, 10.0F, 1e-2F), "both bones keep their lengths");
}

kh2::Mat4 turned_z(float degrees, float x, float y) {
    const float a = degrees * 0.017453292F, c = std::cos(a), s = std::sin(a);
    return {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, x, y, 0, 1};
}

kh2::MotionSkeleton::Joint bone(int parent, float y) {
    kh2::MotionSkeleton::Joint j;
    j.parent = parent;
    j.translation = {0.0F, y, 0.0F, 1.0F};
    return j;
}

void test_retarget() {
    // a source leg 10 + 10 long down -y; a target leg 12 + 9 long, and an unmapped "coat" bone
    kh2::MotionSkeleton source, target;
    source.joints = {bone(-1, 0.0F), bone(0, 0.0F), bone(1, -10.0F), bone(2, -10.0F)};
    target.joints = {bone(-1, 0.0F), bone(0, 0.0F), bone(1, -12.0F), bone(2, -9.0F), bone(0, 0.0F)};
    target.joints[4].translation = {5.0F, 0.0F, 0.0F, 1.0F};
    const kh2::Retarget retarget(source, target, {0, 1, 2, 3, -1}, {{1, 2, 3}});
    constexpr kh2::Mat4 kUnit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // bent: the upper bone turned 90 degrees about z (pointing +x), the lower another 90 (pointing +y)
    const std::vector<kh2::Mat4> pose{kUnit, turned_z(90.0F, 0.0F, 0.0F), turned_z(180.0F, 10.0F, 0.0F),
                                      turned_z(180.0F, 10.0F, 10.0F)};
    std::vector<kh2::Mat4> out;
    retarget.apply(pose, out);
    // the target end's rest head (1 below the source end's) carried by the source end: 10, 11
    const auto at = [&](std::size_t b) { return std::array<float, 2>{out.at(b)[12], out.at(b)[13]}; };
    expect(near(at(3)[0], 10.0F, 1e-2F) && near(at(3)[1], 11.0F, 1e-2F), "the end where the source carries it");
    expect(near(std::hypot(at(2)[0] - at(1)[0], at(2)[1] - at(1)[1]), 12.0F, 1e-2F) &&
               near(std::hypot(at(3)[0] - at(2)[0], at(3)[1] - at(2)[1]), 9.0F, 1e-2F),
           "the target's own lengths");
    expect(near(at(4)[0], 5.0F) && near(at(4)[1], 0.0F) && near(out.at(4)[0], 1.0F), "the unmapped bone at rest");
    // the end bone turned as the source's end is: 180 degrees
    expect(near(out.at(3)[0], -1.0F, 1e-3F) && near(out.at(3)[5], -1.0F, 1e-3F), "the end turned as its source");
    bool refused = false;
    try {
        const kh2::Retarget bad(source, target, {0, 1, 2, 9, -1}, {});
    } catch (const std::runtime_error&) {
        refused = true;
    }
    expect(refused, "a source bone the source does not have");
    // a limb may reach where another mapped bone is carried, not an unmapped one
    const kh2::Retarget reaching(source, target, {0, 1, 2, 3, -1}, {{1, 2, 3, 3}});
    reaching.apply(pose, out);
    expect(near(out.at(3)[12], 10.0F, 1e-2F) && near(out.at(3)[13], 11.0F, 1e-2F), "reaching with the end itself");
    refused = false;
    try {
        const kh2::Retarget bad(source, target, {0, 1, 2, 3, -1}, {{1, 2, 3, 4}});
    } catch (const std::runtime_error&) {
        refused = true;
    }
    expect(refused, "a limb reaching with an unmapped bone");
    // `carry`: the unmapped bone on the end at a frame 2 along its x, turned 90 degrees, its children with it
    target.joints.push_back(bone(4, 1.0F));
    kh2::Retarget carrying(source, target, {0, 1, 2, 3, -1, -1}, {{1, 2, 3}});
    carrying.carry(4, 3, turned_z(90.0F, 2.0F, 0.0F));
    carrying.apply(pose, out);
    // the end at (10, 11) turned 180: 2 along its x is (8, 11), its turn 270 in all
    expect(near(out.at(4)[12], 8.0F, 1e-2F) && near(out.at(4)[13], 11.0F, 1e-2F) && near(out.at(4)[0], 0.0F, 1e-3F) &&
               near(out.at(4)[1], -1.0F, 1e-3F),
           "the carried bone on its carrier");
    expect(near(out.at(5)[12], 9.0F, 1e-2F) && near(out.at(5)[13], 11.0F, 1e-2F), "its child carried with it");
    for (const auto& [bone_at, by] : std::vector<std::pair<int, int>>{{4, 5}, {4, 4}, {4, 9}}) {
        refused = false;
        try {
            carrying.carry(bone_at, by, kUnit);
        } catch (const std::runtime_error&) {
            refused = true;
        }
        expect(refused, "a carrier below the bone, the bone itself or no bone");
    }
}

void test_attributes() {
    // two ranges holding texture animation slots (KH2's id 23), a trigger setting a default (id 4) at frame 10
    Bytes a{2, 1, 20, 0};
    for (const unsigned v : {0U, 300U}) {
        a.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        a.push_back(static_cast<std::uint8_t>(v >> 8U));
    }
    a.insert(a.end(), {23, 1, 76, 0});
    a.insert(a.end(), {125, 0, 188, 0, 23, 1, 24, 0});
    a.insert(a.end(), {10, 0, 4, 1, 33, 0});
    const Bytes set = bar({{0x11, bar({{0x10, a}})}, {0x10011, {}}});
    const kh2::MotionSet motions(set.data(), set.size());
    const auto* at = motions.attributes(0);
    expect(at != nullptr && motions.attributes(1) == nullptr, "slot 0's attributes, none in the empty slot");
    expect(at->ranges.size() == 2 && at->ranges[0].start == 0 && at->ranges[0].end == 300 && at->ranges[0].id == 23 &&
               at->ranges[0].params == std::vector<std::uint16_t>{76},
           "the first range");
    expect(at->ranges[1].start == 125 && at->ranges[1].end == 188 && at->ranges[1].params[0] == 24, "the second");
    expect(at->triggers.size() == 1 && at->triggers[0].frame == 10 && at->triggers[0].id == 4 &&
               at->triggers[0].params == std::vector<std::uint16_t>{33},
           "the trigger, read from its offset");
}

void test_added() {
    // another set's slots from slot 10: its slot 0 at 10, nothing between; not over the set's own
    const Bytes a = mset(2, {0, 1}, {0.0F, 60.0F}, {{1, 5, 0, {{0, 0.0F}, {1, 1.5707964F}}, 1}}, {}, 5.0F);
    const Bytes b = mset(2, {0, 1}, {0.0F, 60.0F}, {{1, 5, 0, {{0, 0.0F}, {1, 1.0F}}, 1}}, {}, 0.0F);
    kh2::MotionSet set(a.data(), a.size());
    const kh2::MotionSet other(b.data(), b.size());
    set.add(10, other);
    expect(set.slots() == 12 && set.motion(0) != nullptr && set.motion(5) == nullptr && set.motion(10) != nullptr &&
               set.motion(11) == nullptr,
           "added from slot 10, the slots between empty");
    expect(near(set.motion(10)->root_motion(0.0F)[12], 0.0F) && near(set.motion(0)->root_motion(0.0F)[12], 5.0F),
           "each its own motion");
    bool refused = false;
    try {
        set.add(4, other);
    } catch (const std::runtime_error&) {
        refused = true;
    }
    expect(refused, "not over the set's own slots");
}

void test_refusals() {
    const Bytes junk{1, 2, 3, 4};
    bool refused = false;
    try {
        (void)kh2::read_motion_skeleton(junk.data(), junk.size());
    } catch (const std::runtime_error&) {
        refused = true;
    }
    expect(refused, "not an mdlx");
}

constexpr kh2::Mat4 kUnit4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr int kTurnZ = 5, kMoveX = 6;  // an F-curve's or an expression's tracks

Bytes two_bones() {
    return mdlx({{-1, 0.0F, 0.0F, 0.0F}, {0, 0.0F, 0.0F, 0.0F}});
}

// Joint 1's matrix at `ticks`, its `track` on a linear curve with a key per entry of `values`, 60 ticks apart
kh2::Mat4 turned(const std::vector<float>& values, int track, int post, float ticks) {
    std::vector<float> times;
    std::vector<std::pair<int, float>> keys;
    for (std::size_t i = 0; i < values.size(); ++i) {
        times.push_back(static_cast<float>(i) * 60.0F);
        keys.emplace_back(static_cast<int>(i), values[i]);
    }
    const Bytes model = two_bones();
    const Bytes set = mset(2, {0, 1}, times, {{1, track, post, keys, 1}}, {}, 0.0F);
    const kh2::MotionSet motions(set.data(), set.size());
    expect(motions.motion(0) != nullptr, "a motion to play");
    kh2::MotionPlayer player(kh2::read_motion_skeleton(model.data(), model.size()));
    player.evaluate(*motions.motion(0), ticks, kUnit4, nullptr);
    return joint(player, 1);
}

bool finite_matrix(const kh2::Mat4& m) {
    for (const float f : m) {
        if (!std::isfinite(f)) {
            return false;
        }
    }
    return true;
}

// A turn so large that one turn at a time never gets it in range, or infinite: played, not hung; an ordinary one
// wrapped as ever.
void test_huge_angles() {
    const kh2::Mat4 ordinary = turned({7.0F}, kTurnZ, 0, 0.0F);
    expect(near(ordinary[0], std::cos(7.0F)) && near(ordinary[1], std::sin(7.0F)), "7 radians: as 7 radians turn");
    for (const float huge :
         {1e10F, -1e10F, 1e30F, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        expect(finite_matrix(turned({huge}, kTurnZ, 0, 0.0F)), "a huge angle: a pose, in finite time");
    }
}

// An F-curve's repeats: near and far as the periods say, and any time at all in finite time.
void test_repeats() {
    const float quarter = 1.5707964F;
    const auto at = [&](float ticks) { return turned({0.0F, quarter}, kTurnZ, 2, ticks); };
    constexpr float kEighth = 0.70710677F;
    expect(near(at(150.0F)[0], kEighth), "two periods on: as 30 ticks in");
    expect(near(at(300030.0F)[0], kEighth), "5,000 periods on: as 30 ticks in");
    expect(finite_matrix(at(1e30F)), "a time past any period");
    (void)at(std::numeric_limits<float>::infinity());  // it returns; no pose is asked of an infinite time
    const int pre_repeat = kTurnZ | (2 << 4);          // the curve's modes share the track's byte
    const auto before = [&](float ticks) { return turned({0.0F, quarter}, pre_repeat, 0, ticks); };
    expect(near(before(-30.0F)[0], kEighth), "before the first key: as 30 ticks in");
    expect(finite_matrix(before(-1e30F)), "a time before any period");
}

struct Node final {
    int type;
    float value;
    int car, cdr;
};

// `body` with joint 1's `track` set by an expression: its tree, from node 0.
Bytes with_expression(Bytes body, const std::vector<Node>& nodes, int track) {
    Writer w{std::move(body)};
    w.pad(4);
    w.set_u32(0x68, static_cast<std::uint32_t>(w.size()));
    w.set_u32(0x6C, static_cast<std::uint32_t>(nodes.size()));
    for (const Node& n : nodes) {
        w.u32(static_cast<std::uint32_t>(n.type));
        w.f32(n.value);
        w.u16(static_cast<unsigned>(n.car));
        w.u16(static_cast<unsigned>(n.cdr));
    }
    w.set_u32(0x60, static_cast<std::uint32_t>(w.size()));
    w.set_u32(0x64, 1);
    w.u16(1);  // its joint
    w.u16(static_cast<unsigned>(track));
    w.u16(0);
    w.u16(0);  // its tree's node
    return w.b;
}

kh2::Mat4 expressed(const std::vector<Node>& nodes, int track) {
    const Bytes model = two_bones();
    const Bytes set = in_set(with_expression(motion_body(2, {0, 1}, {0.0F}, {}, {}, 0.0F), nodes, track));
    const kh2::MotionSet motions(set.data(), set.size());
    expect(motions.motion(0) != nullptr, "a motion with an expression");
    kh2::MotionPlayer player(kh2::read_motion_skeleton(model.data(), model.size()));
    player.evaluate(*motions.motion(0), 0.0F, kUnit4, nullptr);
    return joint(player, 1);
}

constexpr int kAdd = 18, kAbs = 8, kRemainder = 22, kConstant = 31;

void test_expressions() {
    const Node seven{kConstant, 7.0F, 0, 0}, three{kConstant, 3.0F, 0, 0};
    expect(near(expressed({{kAdd, 0.0F, 1, 2}, seven, three}, kMoveX)[12], 10.0F), "a well-formed tree: 7 + 3");
    expect(near(expressed({{kRemainder, 0.0F, 1, 2}, seven, three}, kMoveX)[12], 1.0F), "7 % 3");
    // the int-less remainders: undefined casts, and INT_MIN % -1, which traps on x64
    for (const float x : {1e30F, -2147483648.0F, std::numeric_limits<float>::infinity()}) {
        const kh2::Mat4 m =
            expressed({{kRemainder, 0.0F, 1, 2}, {kConstant, x, 0, 0}, {kConstant, -1.0F, 0, 0}}, kMoveX);
        expect(near(m[12], 0.0F), "a remainder of what no int holds: 0");
    }
    expect(near(expressed({{kRemainder, 0.0F, 1, 2}, seven, {kConstant, -1.0F, 0, 0}}, kMoveX)[12], 0.0F), "7 % -1");
    // a node that reaches itself: cut short, not a stack overflow
    expect(finite_matrix(expressed({{kAbs, 0.0F, 0, 0}}, kMoveX)), "a node under itself");
    // nodes shared twice over at every level: 2^40 visits if each were followed
    std::vector<Node> doubling;
    for (int i = 0; i < 40; ++i) {
        doubling.push_back({kAdd, 0.0F, i + 1, i + 1});
    }
    doubling.push_back({kConstant, 1.0F, 0, 0});
    expect(finite_matrix(expressed(doubling, kMoveX)), "nodes shared to no end: cut short");
}

// A motion's model with more joints than the skeleton it is played on, and a helper: `flags` in calc order, joint 1
// placed where joint `active` is. Before the helper table, bytes no joint should read.
kh2::Mat4 played_on_a_smaller_skeleton(int active) {
    constexpr int kModelJoints = 3, kJoints = 4;
    const std::vector<std::uint32_t> flags{0, 2, 3, 1};
    Writer w{motion_body(kModelJoints, flags, {0.0F}, {}, {{0, 1, active}}, 0.0F)};
    w.set_u32(0x10, static_cast<std::uint32_t>(kModelJoints | (kJoints << 16)));
    w.pad(4);
    for (int i = 0; i < 16; ++i) {
        w.f32(99.0F);
    }
    w.set_u32(0x18, static_cast<std::uint32_t>(w.size()));
    w.zeros(4);
    w.u32(0);  // parent 0 (i16) at +4
    w.zeros(8);
    for (const float f : {1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 7.0F, 0.0F, 0.0F, 1.0F}) {
        w.f32(f);
    }
    const Bytes set = in_set(w.b);
    const kh2::MotionSet motions(set.data(), set.size());
    expect(motions.motion(0) != nullptr, "a motion of three joints and a helper");
    const Bytes model = two_bones();
    kh2::MotionPlayer player(kh2::read_motion_skeleton(model.data(), model.size()));
    player.evaluate(*motions.motion(0), 0.0F, kUnit4, nullptr);
    expect(player.joints().size() == 2, "the skeleton's joints");
    return joint(player, 1);
}

void test_more_joints_than_the_skeleton() {
    expect(near(played_on_a_smaller_skeleton(3)[12], 7.0F), "the helper as its table has it");
    expect(near(played_on_a_smaller_skeleton(2)[12], 0.0F), "a joint the skeleton lacks: neutral, not read from bytes");
}

// An offset in a motion's header
std::size_t word_at(const Bytes& b, std::size_t at) {
    return static_cast<std::size_t>(b.at(at)) | (static_cast<std::size_t>(b.at(at + 1)) << 8U);
}

// Each table a motion's header points at, past the end: the slot has no motion, rather than throwing while drawn.
void test_bad_tables() {
    const auto refused = [](const Bytes& body) {
        const Bytes set = in_set(body);
        const kh2::MotionSet motions(set.data(), set.size());
        return motions.motion(0) == nullptr;
    };
    const std::vector<Curve> hermite{{1, kTurnZ, 0, {{0, 0.0F}, {1, 1.0F}}, 2}};
    const Bytes good = motion_body(2, {0, 1}, {0.0F, 60.0F}, hermite, {}, 5.0F);
    expect(!refused(good), "the unchanged motion plays");
    const auto far_off = [](Bytes body, std::size_t field) {
        Writer w{std::move(body)};
        w.set_u32(field, static_cast<std::uint32_t>(w.size()) + 16U);
        return w.b;
    };
    for (const std::size_t field : {0x40U, 0x48U, 0x4CU, 0x2CU}) {
        expect(refused(far_off(good, field)), "keys, values, slopes or root motion past the end");
    }
    const Bytes linear = motion_body(2, {0, 1}, {0.0F, 60.0F}, {{1, kTurnZ, 0, {{0, 0.0F}, {1, 1.0F}}, 1}}, {}, 0.0F);
    expect(!refused(far_off(linear, 0x4C)), "slopes no key reads are not looked at");
    expect(refused(motion_body(2, {0, 5}, {0.0F}, {}, {}, 0.0F)), "a joint id past the joints");
    const std::uint32_t ik_external = (1U << 16U) | (1U << 23U);
    const Bytes external = motion_body(2, {0, 1U | ik_external}, {0.0F}, {}, {}, 0.0F);
    expect(!refused(external), "an external effector in its table");
    expect(refused(far_off(external, 0xA0)), "the external effectors past the end");
    const Bytes limited = motion_body(2, {0, 1}, {0.0F}, {}, {{13, 1, 0}}, 0.0F);
    expect(refused(far_off(limited, 0x5C)), "a limiter past the end");
    Writer activated{motion_body(2, {0, 1}, {0.0F}, {}, {{0, 1, 0}}, 0.0F)};
    activated.b.at(word_at(activated.b, 0x50) + 8) = 1;  // one activation entry
    expect(refused(far_off(activated.b, 0x58)), "activations past the end");
    Writer rooted{motion_body(2, {0, 1}, {0.0F}, {}, {}, 5.0F)};
    rooted.set_u32(word_at(rooted.b, 0x2C) + 48, 99);
    expect(refused(rooted.b), "a root motion on an F-curve the motion does not have");
}

}  // namespace

int main() {
    try {
        test_curves();
        test_ik();
        test_retarget();
        test_attributes();
        test_added();
        test_refusals();
        test_huge_angles();
        test_repeats();
        test_expressions();
        test_more_joints_than_the_skeleton();
        test_bad_tables();
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "kh2 motion: ok\n";
    return 0;
}
