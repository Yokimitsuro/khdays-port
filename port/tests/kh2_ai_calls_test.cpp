// KH2 enemies' calls as the port answers them (runtime/kh2_enemies/calls.h) and their state machine (actor.h), on
// made-up scripts and a made-up actor: the vector and chance calls, states registered by name, entered, queued and
// left, a locked state, the kinds list.
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
                               "1:5 1 0\n1:6 12 0\n1:7 2 0\n1:8 2 0\n1:9 2 0\n1:10 1 1\n1:127 1 0\n# a comment\n");
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

int main()
{
    test_vectors_and_chance();
    test_states();
    test_kinds();
    if (failures == 0)
        std::printf("kh2 ai calls: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
