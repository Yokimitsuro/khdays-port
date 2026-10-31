#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
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

// One motion (kn::MotionPrototype0RAW) in an .mset: `flags` the joints in
// order (with IK bits), `times` the time table, a root motion moving x by `root_x`.
Bytes mset(int joints, const std::vector<std::uint32_t>& flags, const std::vector<float>& times,
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
    Bytes motion(0x90, 0);
    motion.insert(motion.end(), m.b.begin(), m.b.end());
    return bar({{0x11, bar({{0x09, motion}})}, {0x10011, {}}});
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
    expect(motions.motion(0)->has_root_motion() && near(root[12], 5.0F) && near(root[0], 1.0F),
           "the root motion's translation");
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

}  // namespace

int main() {
    try {
        test_curves();
        test_ik();
        test_retarget();
        test_attributes();
        test_refusals();
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "kh2 motion: ok\n";
    return 0;
}
