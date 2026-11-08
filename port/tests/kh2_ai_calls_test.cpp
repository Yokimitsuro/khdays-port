// KH2 enemies' calls as the port answers them (runtime/kh2_enemies/calls.h) and their state machine (actor.h), on
// made-up scripts and a made-up actor: the vector and chance calls, states registered by name, entered, queued and
// left, a locked state, the kinds list; motions played to their end and looped with their root's travel turned by
// the yaw, turning toward a direction, the target slot and the player, fading, a jump and its landing.
#include "../runtime/kh2_enemies/actor.h"
#include "../runtime/kh2_enemies/calls.h"
#include "../runtime/kh2_enemies/kinds.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace ai = khdays::assets::kh2::ai;
namespace kh = khdays::kh2_enemies;

namespace {

int failures = 0;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            std::printf("FAILED %s:%d %s\n", __FILE__, __LINE__, #x);                                                  \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

using Words = std::vector<std::uint16_t>;
Words operator+(Words a, const Words &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
Words push_int(std::int32_t v) { return {0x0000, std::uint16_t(v & 0xffff), std::uint16_t((std::uint32_t(v) >> 16) & 0xffff)}; }
Words push_float(float f)
{
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return {0x0010, std::uint16_t(b & 0xffff), std::uint16_t(b >> 16)};
}
Words push_addr(unsigned base, unsigned off) { return {std::uint16_t(0x20 | (base << 6)), std::uint16_t(off)}; }
Words pop_to(unsigned base, unsigned off) { return {std::uint16_t(0x1 | (base << 6)), std::uint16_t(off)}; }
Words load(unsigned off) { return {0x3, std::uint16_t(off)}; }
Words sys(unsigned kind, unsigned n) { return {std::uint16_t(0xa | (kind << 6)), std::uint16_t(n)}; }
Words misc(unsigned a) { return {std::uint16_t(0x9 | (a << 6))}; }
Words text(const char *s) // a name in the code: its bytes, two a word, NUL-ended
{
    Words w;
    const std::size_t n = std::strlen(s) + 1;
    for (std::size_t i = 0; i < n; i += 2)
        w.push_back(std::uint16_t(std::uint8_t(s[i]) | (i + 1 < n ? std::uint8_t(s[i + 1]) << 8 : 0)));
    return w;
}

std::size_t first(std::size_t triggers) { return (0x1c + 8 * (triggers + 1) - 0x10) / 2; }

std::shared_ptr<const ai::Script> script(const std::vector<std::pair<int, std::size_t>> &triggers, const Words &code)
{
    std::vector<std::uint8_t> b(16, 0);
    std::memcpy(b.data(), "made_up", 7);
    const auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(std::uint8_t(v >> (8 * i)));
    };
    u32(4224); // the work area: a state table at + 0x74 fits (128 x 32)
    u32(512);
    u32(512);
    for (const auto &[key, offset] : triggers) {
        u32(std::uint32_t(key));
        u32(std::uint32_t(first(triggers.size()) + offset));
    }
    u32(0);
    u32(0);
    for (const auto w : code) {
        b.push_back(std::uint8_t(w));
        b.push_back(std::uint8_t(w >> 8));
    }
    return std::make_shared<const ai::Script>(ai::read_script(b.data(), b.size()));
}

constexpr unsigned kRecord = 200; // the test's own call (kind 0)

struct Rig {
    ai::Memory memory;
    ai::Calls calls;
    std::vector<ai::Value> recorded;
    Rig()
    {
        kh::add_all(calls);
        // the shapes the test's calls use, as KH2's tables give them
        kh::read_shapes(calls, "0:3 0 1\n0:4 2 1\n0:5 2 1\n0:6 1 1\n0:7 1 1\n0:16 1 1\n0:17 1 1\n0:18 2 1\n0:36 2 1\n"
                               "1:5 1 0\n1:6 12 0\n1:7 2 0\n1:8 2 0\n1:9 2 0\n1:10 1 1\n1:127 1 0\n# a comment\n"
                               "1:3 0 1\n1:11 3 0\n1:12 3 0\n1:14 1 1\n1:19 2 0\n1:20 2 0\n1:28 1 0\n1:55 5 0\n"
                               "1:60 1 1\n1:61 4 0\n1:67 2 1\n1:79 2 0\n1:80 3 1\n1:120 1 1\n1:121 3 0\n2:23 1 1\n");
        calls.set_shape(0, kRecord, 1, false);
        calls.set(0, kRecord, [this](ai::Machine &, std::span<ai::Value> a) { recorded.push_back(a[0]); });
    }
};

Words rec() { return sys(0, kRecord); }

void test_vectors_and_chance()
{
    Rig rig;
    // work area: +0x10 a = (1, 2, 2, 0), +0x20 b = (3, 0, 0, 0)
    Words c;
    const auto put = [&](unsigned at, float x) { c = c + push_float(x) + pop_to(1, at); };
    put(0x10, 1.0F);
    put(0x14, 2.0F);
    put(0x18, 2.0F);
    put(0x20, 3.0F);
    c = c + push_addr(1, 0x10) + sys(0, 6) + rec();                                 // |a| = 3
    c = c + push_addr(1, 0x10) + push_addr(1, 0x20) + sys(0, 4) + load(0) + rec(); // (a + b).x = 4
    c = c + push_addr(1, 0x10) + push_addr(1, 0x20) + sys(0, 5) + load(4) + rec(); // (a - b).y = 2
    c = c + push_addr(1, 0x10) + push_float(2.0F) + sys(0, 36) + load(8) + rec();  // (a x 2).z = 4
    c = c + push_addr(1, 0x10) + sys(0, 7) + rec() + push_addr(1, 0x10) + load(0) + rec(); // 3, then a.x = 1/3
    c = c + push_int(5) + sys(0, 16) + rec();                                      // below 5
    c = c + push_float(10.0F) + push_float(20.0F) + sys(0, 18) + rec();            // between 10 and 20
    c = c + sys(0, 3) + rec();                                                     // the tick: 1.0
    c = c + misc(1);
    ai::Machine m(script({{0, 0}}, c), rig.memory, rig.calls);
    m.raise(0);
    const auto &r = rig.recorded;
    CHECK(r.size() == 9);
    if (r.size() == 9) {
        CHECK(r[0].f() == 3.0F);
        CHECK(r[1].f() == 4.0F);
        CHECK(r[2].f() == 2.0F);
        CHECK(r[3].f() == 4.0F);
        CHECK(r[4].f() == 3.0F && std::fabs(r[5].f() - 1.0F / 3.0F) < 1e-6F);
        CHECK(r[6].i() >= 0 && r[6].i() < 5);
        CHECK(r[7].f() >= 10.0F && r[7].f() < 20.0F);
        CHECK(r[8].f() == 1.0F);
    }
}

// States: trigger 0 registers "a" (its routine records 1, queues "b", ends) and "b" (records 2, ends); main enters
// "a", then each tick records whether a state is pending.
void test_states()
{
    Rig rig;
    const Words name_a = text("a"), name_b = text("b");
    // layout: [names][trigger 0][main][routine a][routine b]
    Words names = name_a + name_b;
    const unsigned at_a = unsigned(first(2)), at_b = at_a + unsigned(name_a.size());
    const auto reg = [&](unsigned name_at, std::uint32_t flags, std::uint32_t routine) {
        Words w = push_addr(1, 0x74) + push_addr(3, name_at) + push_int(std::int32_t(flags)) + push_int(std::int32_t(routine));
        for (int i = 0; i < 8; ++i)
            w = w + push_int(0);
        return w + sys(1, 6);
    };
    // the routines' places are known once the trigger's and main's sizes are: two passes
    Words start, main, ra, rb;
    std::uint32_t routine_a = 0, routine_b = 0;
    for (int pass = 0; pass < 2; ++pass) {
        start = push_addr(1, 0x74) + sys(1, 5) + reg(at_a, 100, routine_a) + reg(at_b, 100, routine_b) +
                push_addr(1, 8) + push_addr(1, 0x74) + sys(1, 7) + misc(1);
        main = push_addr(1, 8) + push_addr(3, at_a) + sys(1, 8) + push_addr(1, 8) + sys(1, 10) + rec() + misc(0) +
               push_addr(1, 8) + sys(1, 10) + rec() + misc(0) + push_addr(1, 8) + sys(1, 10) + rec() + misc(0) +
               misc(0) + misc(0) + misc(1);
        ra = push_int(1) + rec() + push_addr(1, 8) + push_addr(3, at_b) + sys(1, 9) + misc(1);
        rb = push_int(2) + rec() + misc(1);
        routine_a = std::uint32_t(first(2) + names.size() + start.size() + main.size());
        routine_b = routine_a + std::uint32_t(ra.size());
    }
    const Words code = names + start + main + ra + rb;
    ai::Memory &mem = rig.memory;
    kh::Actor actor(script({{0, names.size()}, {1, names.size() + start.size()}}, code), mem, rig.calls, "made_up");
    std::vector<std::string> entered;
    actor.states().entered = [&](const std::string &n) { entered.push_back(n); };
    // the script keeps its actor at its handle's + 4 (wp + 0xc), as KH2's scripts do with trigger 0's argument
    mem.set_u32(actor.machine().work_area() + 0xc, actor.address());
    actor.start();
    for (int i = 0; i < 6; ++i)
        actor.tick();
    CHECK(entered.size() == 2 && entered[0] == "a" && entered[1] == "b");
    const auto &r = rig.recorded;
    // tick 1: main enters a (its routine runs after main, in the same tick): main records pending 1, a records 1;
    // tick 2: a's thread is over, b entered: main records pending 1, b records 2; tick 3: b over: pending 0
    CHECK(r.size() >= 5);
    if (r.size() >= 5) {
        CHECK(r[0].i() == 1 && r[1].i() == 1);
        CHECK(r[2].i() == 1 && r[3].i() == 2);
        CHECK(r[4].i() == 0);
    }
}

void test_kinds()
{
    const auto kinds = kh::read_kinds("# name\ttitle\tcovered\tcalls\tmissing\n"
                                      "M_EX010\tSoldier\tyes\t62\t-\n"
                                      "m_ex020\tShadow\tno\t71\t1:39 1:62\r\n"
                                      "broken line\n");
    CHECK(kinds.size() == 2);
    const kh::Kind *s = kh::find_kind(kinds, "m_ex010");
    CHECK(s != nullptr && s->covered && s->calls == 62 && s->title == "Soldier");
    const kh::Kind *sh = kh::find_kind(kinds, "M_EX020");
    CHECK(sh != nullptr && !sh->covered && sh->missing == "1:39 1:62");
    CHECK(kh::find_kind(kinds, "M_EX999") == nullptr);
}

} // namespace

// A made-up motion set: 0 loops over 60 ticks, its root 1 cm forward a tick; 44 and 7 hold at their end (30 ticks)
struct MadeUpMotions final : kh::Motions {
    bool has(int id) const override
    {
        return id == 0 || id == 44 || id == 7;
    }
    float length(int id) const override
    {
        return id == 0 ? 60.0F : 30.0F;
    }
    float loop_from(int id) const override
    {
        return id == 0 ? 0.0F : 30.0F;
    }
    kh::Vec3 root(int id, float t) const override
    {
        return id == 0 ? kh::Vec3{0.0F, 0.0F, t} : kh::Vec3{};
    }
};

struct MadeUpWorld final : kh::World {
    kh::Object *standing = nullptr;
    float floor = 0.0F;      // the ground's height, KH2's y
    std::string sent;        // the last state sent to every object (1:90)
    float sent_delay = -1.0F;
    kh::Object *player(kh::Actor &) override
    {
        return standing;
    }
    float ground(kh::Actor &) override
    {
        return floor;
    }
    void broadcast(kh::Actor &, const std::string &name, float delay) override
    {
        sent = name;
        sent_delay = delay;
    }
};

// a call made as a script makes it: its handler with these arguments, args[0] given back
ai::Value call(Rig &rig, kh::Actor &a, unsigned kind, unsigned n, std::vector<ai::Value> args)
{
    const ai::Calls::Entry *e = rig.calls.find(kind, n);
    if (e == nullptr || !e->fn) {
        std::printf("FAILED no handler for %u:%u\n", kind, n);
        ++failures;
        return {};
    }
    if (args.empty())
        args.push_back({});
    e->fn(a.machine(), args);
    return args[0];
}

std::uint32_t vec(ai::Memory &m, float x, float y, float z, float w = 0.0F)
{
    const std::uint32_t at = m.add(16, "a test's vector");
    const float v[4] = {x, y, z, w};
    m.write(at, v, sizeof v);
    return at;
}

void test_motions_and_moving()
{
    Rig rig;
    MadeUpMotions motions;
    MadeUpWorld world;
    kh::Actor actor(script({{0, 0}}, misc(1)), rig.memory, rig.calls, "made_up");
    actor.motions = &motions;
    actor.world = &world;
    const ai::Value self = ai::Value::of_address(actor.address());
    const auto f = ai::Value::of_float;
    const auto i = ai::Value::of_int;
    const auto adr = ai::Value::of_address;

    // APPEAR (44) to its end: the ended bit only then (1:14)
    call(rig, actor, 1, 11, {self, i(44), f(0.0F)});
    for (int t = 0; t < 29; ++t)
        actor.step(1.0F);
    CHECK(call(rig, actor, 1, 14, {self}).i() == 0);
    actor.step(1.0F);
    CHECK(call(rig, actor, 1, 14, {self}).i() == 1);
    CHECK(actor.motion.time == 30.0F);
    CHECK(call(rig, actor, 1, 15, {self}).i() == 44); // the motion playing's number, which the scripts wait on
    CHECK(call(rig, actor, 1, 67, {self, i(0)}).f() == 60.0F);
    CHECK(call(rig, actor, 1, 68, {self, i(-1)}).f() == 30.0F); // -1: the motion playing
    CHECK(!actor.play(3, 0.0F, 0.0F));                          // not in the set: nothing

    // the loop from frame 10, facing +x (1:79): its root's travel along x, on through its loop
    call(rig, actor, 1, 61, {self, i(0), f(10.0F), f(0.0F)});
    CHECK(actor.motion.time == 10.0F && call(rig, actor, 1, 14, {self}).i() == 0);
    call(rig, actor, 1, 79, {self, adr(vec(rig.memory, 1.0F, 0.0F, 0.0F))});
    kh::Vec3 t = actor.step(10.0F);
    CHECK(std::fabs(t[0] - 10.0F) < 1e-3F && std::fabs(t[2]) < 1e-3F);
    t = actor.step(55.0F); // 20 -> 75: 40 to its end, then 15 from its loop point
    CHECK(std::fabs(t[0] - 55.0F) < 1e-3F && std::fabs(actor.motion.time - 15.0F) < 1e-3F);
    call(rig, actor, 1, 12, {self, i(0), f(0.0F)}); // already playing: left as it is
    CHECK(std::fabs(actor.motion.time - 15.0F) < 1e-3F);

    // 1:80 toward yaw 0 by 2 x 0.25 a call: three steps, then there
    const std::uint32_t ahead = vec(rig.memory, 0.0F, 0.0F, 1.0F);
    int turning = 0;
    for (int k = 0; k < 3; ++k)
        turning += call(rig, actor, 1, 80, {self, adr(ahead), f(0.25F)}).i();
    CHECK(turning == 3 && std::fabs(actor.yaw() - (1.5707963F - 1.5F)) < 1e-4F);
    CHECK(call(rig, actor, 1, 80, {self, adr(ahead), f(0.25F)}).i() == 0 && actor.yaw() == 0.0F);

    // the target: none standing, then a player at x 100 (1:3; the slot 2:23 chosen by 1:121, its place 1:120)
    CHECK(call(rig, actor, 1, 3, {}).bits == 0);
    kh::Object player(rig.memory, "a player");
    player.set_place({100.0F, 0.0F, 0.0F});
    world.standing = &player;
    CHECK(call(rig, actor, 1, 3, {}).bits == player.address());
    const ai::Value slot = call(rig, actor, 2, 23, {self});
    call(rig, actor, 1, 121, {slot, self, i(0)});
    float at[4] = {};
    rig.memory.read(call(rig, actor, 1, 120, {slot}).bits, at, sizeof at);
    CHECK(at[0] == 100.0F);
    call(rig, actor, 1, 121, {slot, self, i(2)}); // itself
    rig.memory.read(call(rig, actor, 1, 120, {slot}).bits, at, sizeof at);
    CHECK(at[0] == 0.0F);

    // fading: gone over 10 ticks (1:19), held at once by 1:28 (1:20 then does nothing)
    call(rig, actor, 1, 19, {self, f(10.0F)});
    actor.step(5.0F);
    CHECK(std::fabs(actor.fade - 0.5F) < 1e-4F);
    call(rig, actor, 1, 28, {self});
    actor.step(1.0F);
    CHECK(actor.fade == 0.0F);
    call(rig, actor, 1, 20, {self, f(10.0F)});
    actor.step(5.0F);
    CHECK(actor.fade == 0.0F);

    // a jump 100 up with its next motions (1:55's flag): up first, in the air (1:60), then landed
    CHECK(call(rig, actor, 1, 60, {self}).i() == 0);
    call(rig, actor, 1, 55, {self, adr(vec(rig.memory, 0.0F, -100.0F, 1.0F, 2.0F)), i(7), f(0.0F), i(1)});
    CHECK(actor.motion.id == 7 && actor.motion.after == 8 && actor.motion.landing == 9);
    t = actor.step(1.0F);
    CHECK(t[1] < 0.0F && std::fabs(t[2] - 1.9F) < 1e-4F); // its speed along, slowed to 0.95 by its first evaluation
    CHECK(call(rig, actor, 1, 60, {self}).i() == 1);
    float top = t[1], y = t[1];
    for (int k = 0; k < 40; ++k) {
        y += actor.step(1.0F)[1];
        top = std::min(top, y);
    }
    CHECK(top < -95.0F && top > -105.0F); // the height asked
    actor.land();
    CHECK(!actor.jump.on && call(rig, actor, 1, 60, {self}).i() == 0);

    // the way made one long, then times its w; a positive y rises as well (the Fiery Globe's hops: the way to the
    // player 230 off, 70 high, 7 a tick)
    call(rig, actor, 1, 55, {self, adr(vec(rig.memory, 0.0F, 70.0F, 230.0F, 7.0F)), i(7), f(0.0F), i(0)});
    t = actor.step(1.0F);
    CHECK(t[1] < 0.0F && std::fabs(t[2] - 6.65F) < 1e-3F); // 7 along, slowed to 0.95 by its first evaluation
    top = t[1], y = t[1];
    for (int k = 0; k < 40; ++k) {
        y += actor.step(1.0F)[1];
        top = std::min(top, y);
    }
    CHECK(top < -65.0F && top > -75.0F);
    actor.land();
}

// The calls the other enemies need (US2): two ways' turn apart, a vector scaled, KH2's running number, a jump's
// speed for a height, off the ground (1:57) through a jump, a jump of the actor's own (1:156) to its top and back,
// a motion at its end, the actor's way bytes
void test_more_enemies_calls()
{
    Rig rig;
    MadeUpMotions motions;
    MadeUpWorld world;
    kh::Actor actor(script({{0, 0}}, misc(1)), rig.memory, rig.calls, "made_up");
    actor.motions = &motions;
    actor.world = &world;
    const ai::Value self = ai::Value::of_address(actor.address());
    const auto f = ai::Value::of_float;
    const auto i = ai::Value::of_int;
    const auto adr = ai::Value::of_address;
    const float pi = 3.14159265F;

    // 0:21: from +z to +x is a quarter turn; from +x to -z (through the back) wraps into (-pi, pi]
    const std::uint32_t z = vec(rig.memory, 0.0F, 0.0F, 1.0F), x = vec(rig.memory, 1.0F, 0.0F, 0.0F);
    CHECK(std::fabs(call(rig, actor, 0, 21, {adr(z), adr(x)}).f() - pi / 2.0F) < 1e-5F);
    const std::uint32_t back = vec(rig.memory, -0.01F, 0.0F, -1.0F);
    const float wrapped = call(rig, actor, 0, 21, {adr(x), adr(back)}).f();
    CHECK(wrapped > -pi && wrapped <= pi && std::fabs(std::fabs(wrapped) - (pi / 2.0F + 0.01F)) < 1e-3F);
    // 0:35: scaled where it lies, all four
    const std::uint32_t v = vec(rig.memory, 1.0F, -2.0F, 3.0F, 4.0F);
    call(rig, actor, 0, 35, {adr(v), f(0.5F)});
    float got[4];
    rig.memory.read(v, got, sizeof got);
    CHECK(got[0] == 0.5F && got[1] == -1.0F && got[2] == 1.5F && got[3] == 2.0F);
    // 2:67: one more each time
    const int n1 = call(rig, actor, 2, 67, {}).i(), n2 = call(rig, actor, 2, 67, {}).i();
    CHECK(n2 == n1 + 1);
    // 1:100: the speed to rise 100 under KH2's pull, that rise's own
    const float speed = call(rig, actor, 1, 100, {f(-100.0F)}).f();
    CHECK(std::fabs(speed - std::sqrt(200.0F / 0.408163F)) < 1e-3F);
    // 1:57 on the ground, then in a jump until it lands
    CHECK(call(rig, actor, 1, 57, {self}).i() == 0);
    call(rig, actor, 1, 55, {self, adr(vec(rig.memory, 0.0F, -100.0F, 1.0F, 2.0F)), i(7), f(0.0F), i(0)});
    CHECK(call(rig, actor, 1, 57, {self}).i() == 1);
    CHECK(actor.motion.after == -1 && actor.motion.landing == 5);
    actor.land();
    CHECK(call(rig, actor, 1, 57, {self}).i() == 0);
    // 1:156: up 150 (its y) in 20 ticks, along +x at 3 a tick (its w), motion 7 then 8 and 9 after
    call(rig, actor, 1, 156,
         {self, adr(vec(rig.memory, 4.0F, 150.0F, 0.0F, 3.0F)), f(20.0F), f(0.95F), i(7), f(0.0F), i(1)});
    CHECK(actor.jump.on && actor.motion.id == 7 && actor.motion.after == 8 && actor.motion.landing == 9);
    CHECK(call(rig, actor, 1, 57, {self}).i() == 1);
    float y = 0.0F, top = 0.0F, along = 0.0F;
    for (int k = 0; k < 20; ++k) {
        const kh::Vec3 t = actor.step(1.0F);
        y += t[1];
        along += t[0];
        top = std::min(top, y);
    }
    CHECK(top < -145.0F && top > -155.0F); // its top 150 up (KH2's y down) after its 20 ticks
    // 3 along, slowed to 0.95 every two ticks (FUN_1403b7ad0 with no steering): 2 x 3 x (0.95 + ... + 0.95^10)
    CHECK(std::fabs(along - 45.75F) < 0.5F);
    // 1:111 steers: on the ground its speed a tick, kept; in the air the velocity approaches it a tenth at a time
    actor.land();
    call(rig, actor, 1, 111, {self, adr(vec(rig.memory, 0.0F, 0.0F, 2.0F)), f(5.0F)});
    float ground = 0.0F;
    for (int k = 0; k < 4; ++k)
        ground += actor.step(1.0F)[2];
    CHECK(std::fabs(ground - 20.0F) < 1e-3F); // 5 a tick, still 5 after the call's tick
    call(rig, actor, 1, 111, {self, adr(vec(rig.memory, 0.0F, 0.0F, 1.0F)), f(0.0F)});
    float still = 0.0F;
    for (int k = 0; k < 4; ++k)
        still += std::fabs(actor.step(1.0F)[2]);
    CHECK(still < 1e-3F);
    // in 1:156's jump (class 9) the steering is approached by its own value's rest: 0.95 keeps 95 % (the Wight
    // Knight's tear steered at 100: 5 a tick after the first evaluation, not 10 as class 1's)
    call(rig, actor, 1, 156, {self, adr(vec(rig.memory, 0.0F, 150.0F, 0.0F, 0.0F)), f(20.0F), f(0.95F), i(7), f(0.0F),
                              i(0)});
    call(rig, actor, 1, 111, {self, adr(vec(rig.memory, 0.0F, 0.0F, 1.0F)), f(100.0F)});
    CHECK(std::fabs(actor.step(1.0F)[2] - 5.0F) < 1e-3F);
    call(rig, actor, 1, 111, {self, adr(vec(rig.memory, 0.0F, 0.0F, 1.0F)), f(0.0F)});
    actor.land();
    // 1:63: a motion of 30 ticks (APPEAR, played once) at its end, no blend left
    call(rig, actor, 1, 11, {self, i(44), f(0.0F)});
    CHECK(call(rig, actor, 1, 63, {self}).i() == 0);
    for (int k = 0; k < 40; ++k)
        actor.step(1.0F);
    CHECK(call(rig, actor, 1, 63, {self}).i() == 1);
    // 1:150 / 1:151: bit n - 1 of the bytes + 0x2ac / + 0x2ad
    CHECK(call(rig, actor, 1, 150, {self, i(1)}).i() == 0);
    actor.set_u32(0x2ac, 0x0401);
    CHECK(call(rig, actor, 1, 150, {self, i(1)}).i() == 1 && call(rig, actor, 1, 150, {self, i(2)}).i() == 0);
    CHECK(call(rig, actor, 1, 151, {self, i(3)}).i() == 1);
}

// A state whose flag bit 0 is clear begins only once its actor is no longer held by a reaction (FUN_1403ca550), one
// with the bit at once; Days's players are KH2's player to 1:309 (+ 0x6c8 bit 7)
void test_state_waits()
{
    for (const bool at_once : {false, true}) {
        Rig rig;
        const Words names = text("a");
        const unsigned at_a = unsigned(first(2));
        Words start, main, ra;
        std::uint32_t routine_a = 0;
        const std::int32_t flags = at_once ? 0x10064 : 100; // + 4 its priority, + 6 bit 0
        for (int pass = 0; pass < 2; ++pass) {
            Words reg = push_addr(1, 0x74) + push_addr(3, at_a) + push_int(flags) + push_int(std::int32_t(routine_a));
            for (int i = 0; i < 8; ++i)
                reg = reg + push_int(0);
            start = push_addr(1, 0x74) + sys(1, 5) + reg + sys(1, 6) + push_addr(1, 8) + push_addr(1, 0x74) +
                    sys(1, 7) + misc(1);
            main = push_addr(1, 8) + push_addr(3, at_a) + sys(1, 8) + misc(0) + misc(0) + misc(0) + misc(0) + misc(0) +
                   misc(1);
            ra = push_int(1) + rec() + misc(1);
            routine_a = std::uint32_t(first(2) + names.size() + start.size() + main.size());
        }
        ai::Memory &mem = rig.memory;
        kh::Actor actor(script({{0, names.size()}, {1, names.size() + start.size()}}, names + start + main + ra), mem,
                        rig.calls, "made_up");
        mem.set_u32(actor.machine().work_area() + 0xc, actor.address());
        actor.set_bits(kh::field::kStatus, kh::bit::kHeld, true);
        actor.start();
        for (int i = 0; i < 3; ++i)
            actor.tick();
        CHECK(rig.recorded.size() == (at_once ? 1u : 0u)); // held: only the state with the bit has begun
        actor.set_bits(kh::field::kStatus, kh::bit::kHeld, false);
        actor.tick();
        CHECK(rig.recorded.size() == 1); // let go: it begins
    }
    Rig rig;
    const Words code = push_addr(1, 0x40) + load(0) + sys(1, 309) + rec() + misc(1);
    rig.calls.set_shape(1, 309, 1, true);
    kh::Actor actor(script({{0, 0}}, code), rig.memory, rig.calls, "made_up");
    kh::Object player(rig.memory, "a player");
    player.set_bits(kh::field::kFlags6c8, kh::bit::kPlayer, true);
    rig.memory.set_u32(actor.machine().work_area() + 0x40, player.address());
    actor.machine().raise(0);
    CHECK(rig.recorded.size() == 1 && rig.recorded[0].i() == 1);
    // 2:8 a hit's reaction past 1: a small one (2) is, a weak one (1, 12) is not
    rig.calls.set_shape(2, 8, 1, true);
    const Words asks = push_addr(1, 0x40) + load(0) + sys(2, 8) + rec() + misc(1);
    kh::Actor asker(script({{0, 0}}, asks), rig.memory, rig.calls, "made_up");
    for (const std::uint32_t kind : {2u, 12u}) {
        player.set_u32(0x24, kind << 16);
        rig.memory.set_u32(asker.machine().work_area() + 0x40, player.address());
        asker.machine().raise(0);
    }
    CHECK(rig.recorded.size() == 3 && rig.recorded[1].i() == 1 && rig.recorded[2].i() == 0);
}

// The second batch's calls: a value's size, a table's ratio, shared memory, class 5's own velocity, steering to a
// point, the ground and the height over it, a state sent to every object, + 0xd10's bits, an attached thread
void test_batch_two()
{
    Rig rig;
    kh::read_shapes(rig.calls, "0:23 1 1\n1:230 2 1\n2:21 2 1\n1:58 3 0\n1:74 1 0\n1:2 3 1\n1:98 1 1\n1:99 1 1\n"
                               "1:90 3 0\n1:211 2 0\n1:212 2 0\n1:112 3 0\n");
    MadeUpWorld world;
    // its main thread a yield over and over (word 0), a routine the same (word 2)
    const Words loop = misc(0) + Words{0x0007, 0xfffd} + misc(0) + Words{0x0007, 0xfffd};
    kh::Actor actor(script({{1, 0}}, loop), rig.memory, rig.calls, "made_up");
    actor.world = &world;
    actor.machine().start_main(1);
    const ai::Value self = ai::Value::of_address(actor.address());
    const auto f = ai::Value::of_float;
    const auto i = ai::Value::of_int;
    const auto adr = ai::Value::of_address;

    // 0:23: the size of -2.5, marked a whole number as KH2 marks it
    const ai::Value size = call(rig, actor, 0, 23, {f(-2.5F)});
    CHECK(size.f() == 2.5F && size.tag == ai::Tag::Int);
    // 1:230: record 1 of a table of {6, 3}s: 2
    const std::uint32_t table = rig.memory.add(24, "a test's table");
    const std::int32_t records[6] = {1, 1, 0, 6, 3, 0};
    rig.memory.write(table, records, sizeof records);
    CHECK(call(rig, actor, 1, 230, {adr(table), i(1)}).f() == 2.0F);
    // 2:21: the same memory for the same number
    const std::uint32_t shared = call(rig, actor, 2, 21, {i(7), i(10)}).bits;
    CHECK(shared != 0 && call(rig, actor, 2, 21, {i(7), i(10)}).bits == shared);
    CHECK(call(rig, actor, 2, 21, {i(8), i(10)}).bits != shared);

    // 1:58: 4 a tick along +z, keeping half of it an evaluation (2 the first tick's travel), 1:74 its end
    call(rig, actor, 1, 58, {self, adr(vec(rig.memory, 0.0F, 0.0F, 3.0F, 4.0F)), f(0.5F)});
    CHECK(actor.dashing && std::fabs(actor.step(1.0F)[2] - 2.0F) < 1e-4F);
    call(rig, actor, 1, 74, {self});
    CHECK(!actor.dashing);
    actor.step(2.0F);
    CHECK(std::fabs(actor.step(1.0F)[2]) < 1e-4F);

    // 1:2: steered toward (300, 0, 400) at 6, 500 away
    CHECK(call(rig, actor, 1, 2, {self, adr(vec(rig.memory, 300.0F, 0.0F, 400.0F)), f(6.0F)}).f() == 500.0F);
    CHECK(std::fabs(actor.steer[0] - 0.6F) < 1e-5F && std::fabs(actor.steer[2] - 0.8F) < 1e-5F &&
          actor.steer_speed == 6.0F);
    actor.steer_speed = 0.0F;

    // 1:98 / 1:99: 50 above the ground at 0 (KH2's y down): its foot on it, -50
    actor.set_place({10.0F, -50.0F, 20.0F});
    const std::uint32_t foot = call(rig, actor, 1, 98, {self}).bits;
    float ground[4] = {};
    rig.memory.read(foot, ground, sizeof ground);
    CHECK(ground[0] == 10.0F && ground[1] == 0.0F && ground[2] == 20.0F);
    CHECK(call(rig, actor, 1, 99, {self}).f() == -50.0F);
    actor.set_place({10.0F, 5.0F, 20.0F}); // under it: 0
    CHECK(call(rig, actor, 1, 99, {self}).f() == 0.0F);

    // 1:90: the state's name to every object, with its time
    const std::uint32_t name = rig.memory.add(8, "a test's name");
    rig.memory.write(name, "flee", 5);
    call(rig, actor, 1, 90, {self, adr(name), f(30.0F)});
    CHECK(world.sent == "flee" && world.sent_delay == 30.0F);

    // 1:211 / 1:212: + 0xd10's bit 3
    call(rig, actor, 1, 211, {self, i(3)});
    CHECK(actor.u32(0xd10) == 8u);
    call(rig, actor, 1, 212, {self, i(3)});
    CHECK(actor.u32(0xd10) == 0u);

    // 1:112: the routine (word 3) attached to the actor; attached again, the one before ends
    call(rig, actor, 1, 112, {self, i(3), i(0)});
    const std::uint32_t first = actor.side_thread;
    CHECK(first != 0);
    call(rig, actor, 1, 112, {self, i(3), i(0)});
    CHECK(actor.side_thread != first);
    bool first_ended = true;
    for (const auto &c : actor.machine().main_thread()->children)
        if (c->id == first && c->status != ai::Status::Stopped)
            first_ended = false;
    CHECK(first_ended);
}

int main()
{
    test_vectors_and_chance();
    test_states();
    test_kinds();
    test_motions_and_moving();
    test_more_enemies_calls();
    test_state_waits();
    test_batch_two();
    if (failures == 0)
        std::printf("kh2 ai calls: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
