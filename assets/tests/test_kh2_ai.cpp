#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/kh2_ai.h"

// KH2's AI scripts made here, word by word, run by the port's machine: every operation, threads and yields, events,
// child threads, calls handled and not, the faults that stop a thread, memory misses, no fixed caps, the PC's
// 60-frames constants.

namespace {

namespace ai = khdays::assets::kh2::ai;
using Words = std::vector<std::uint16_t>;

void expect(bool ok, const std::string &what)
{
    if (!ok)
        throw std::runtime_error(what);
}

// ---- instructions
Words push_int(std::int32_t v) { return {0x0000, std::uint16_t(v & 0xffff), std::uint16_t((std::uint32_t(v) >> 16) & 0xffff)}; }
Words push_float(float f)
{
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return {0x0010, std::uint16_t(b & 0xffff), std::uint16_t(b >> 16)};
}
Words push_addr(unsigned base, unsigned off) { return {std::uint16_t(0x20 | (base << 6)), std::uint16_t(off)}; }
Words push_val(unsigned base, unsigned off) { return {std::uint16_t(0x30 | (base << 6)), std::uint16_t(off)}; }
Words pop_to(unsigned base, unsigned off) { return {std::uint16_t(0x1 | (base << 6)), std::uint16_t(off)}; }
Words copy(unsigned base, unsigned n, unsigned off) { return {std::uint16_t(0x2 | (base << 6)), std::uint16_t(n), std::uint16_t(off)}; }
Words load(unsigned off) { return {0x3, std::uint16_t(off)}; }
Words store(unsigned n) { return {std::uint16_t(0x4 | (n << 6))}; }
Words unary(unsigned sub, unsigned a) { return {std::uint16_t(0x5 | (sub << 4) | (a << 6))}; }
Words binary(unsigned sub, unsigned a) { return {std::uint16_t(0x6 | (sub << 4) | (a << 6))}; }
Words jump(unsigned kind, std::int16_t by) { return {std::uint16_t(0x7 | (kind << 6)), std::uint16_t(by)}; }
Words call(unsigned slots, std::int16_t by) { return {std::uint16_t(0x8 | (slots << 6)), std::uint16_t(by)}; }
Words far_call(unsigned slots, std::int32_t by) { return {std::uint16_t(0xb | (slots << 6)), std::uint16_t(by & 0xffff), std::uint16_t((std::uint32_t(by) >> 16) & 0xffff)}; }
Words misc(unsigned a) { return {std::uint16_t(0x9 | (a << 6))}; }
Words sys(unsigned kind, unsigned n) { return {std::uint16_t(0xa | (kind << 6)), std::uint16_t(n)}; }
Words operator+(Words a, const Words &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

constexpr unsigned kRecord = 0, kCount = 1, kGive = 2, kNoFn = 3, kEndSelf = 4; // the test's calls (kind 0)

// A script: its name, sizes, triggers {key, offset in `code`}, the code. Word 0 is at +0x10, so the code starts after
// the header and the triggers; first() says at which word.
std::size_t first(std::size_t triggers) { return (0x1c + 8 * (triggers + 1) - 0x10) / 2; }

std::shared_ptr<const ai::Script> script(const std::vector<std::pair<int, std::size_t>> &triggers, const Words &code,
                                         const char *name = "made_up", std::int32_t stack = 256)
{
    std::vector<std::uint8_t> b(16, 0);
    std::memcpy(b.data(), name, std::strlen(name));
    const auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(std::uint8_t(v >> (8 * i)));
    };
    u32(64);    // work area
    u32(256);   // frame
    u32(std::uint32_t(stack));
    const std::size_t at = first(triggers.size());
    for (const auto &[key, offset] : triggers) {
        u32(std::uint32_t(key));
        u32(std::uint32_t(at + offset));
    }
    u32(0);
    u32(0);
    for (const auto w : code) {
        b.push_back(std::uint8_t(w));
        b.push_back(std::uint8_t(w >> 8));
    }
    return std::make_shared<const ai::Script>(ai::read_script(b.data(), b.size()));
}

struct Rig {
    ai::Memory memory;
    ai::Calls calls;
    std::vector<ai::Value> recorded;
    std::vector<std::string> reports;
    int count = 0;
    Rig()
    {
        memory.report = [this](const std::string &s) { reports.push_back(s); };
        calls.set_shape(0, kRecord, 1, false);
        calls.set(0, kRecord, [this](ai::Machine &, std::span<ai::Value> a) { recorded.push_back(a[0]); });
        calls.set_shape(0, kCount, 0, false);
        calls.set(0, kCount, [this](ai::Machine &, std::span<ai::Value>) { ++count; });
        calls.set_shape(0, kGive, 1, true);
        calls.set(0, kGive, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(a[0].i() * 10); });
        calls.set_shape(0, kNoFn, 2, true); // KH2 has it, the port does not handle it
        calls.set_shape(0, kEndSelf, 0, false); // the running thread ended by its own call (a state it leaves)
        calls.set(0, kEndSelf, [](ai::Machine &m, std::span<ai::Value>) { m.stop(m.current()); });
    }
};

Words rec() { return sys(0, kRecord); }

void test_operations()
{
    Rig rig;
    // the routine called below: pushes 11, returns
    const Words routine = push_int(11) + misc(2);
    Words c;
    c = c + push_int(7) + push_int(5) + binary(0, 1) + rec();                 // 2
    c = c + push_float(1.5F) + push_float(0.5F) + binary(1, 0) + rec();       // 2.0
    c = c + push_int(7) + push_int(0) + binary(0, 3) + rec();                 // 7: by zero, the left kept
    c = c + push_float(3.0F) + push_float(0.0F) + binary(1, 3) + rec();       // 3.0
    c = c + push_int(-8) + push_int(1) + binary(0, 9) + rec();                // -4
    c = c + push_int(5) + unary(0, 2) + rec();                                // -5
    c = c + push_float(-2.5F) + unary(1, 1) + rec();                          // -2
    c = c + push_float(-1.57079637F) + misc(7) + rec();                       // cos(-pi/2): exactly 0
    c = c + push_float(180.0F) + misc(8) + rec();                             // -pi (180 degrees brought into [-pi, pi))
    c = c + push_int(42) + pop_to(1, 0x10) + push_val(1, 0x10) + rec();      // 42, tagged Int
    c = c + push_addr(1, 0) + push_int(0x10) + binary(0, 0) + load(0) + rec(); // an address + 16, loaded: 42
    c = c + push_addr(1, 0x20) + push_int(9) + store(0) + push_val(1, 0x20) + rec(); // 9
    c = c + push_addr(1, 0x10) + copy(1, 4, 0x24) + push_val(1, 0x24) + rec();      // 42 copied, its tag lost
    c = c + push_int(3) + misc(5) + binary(0, 2) + rec();                     // 9 (dup, mul)
    c = c + push_int(0) + jump(1, 5) + push_int(99) + rec() + push_int(1) + rec(); // jz taken: 1 only
    c = c + push_int(4) + sys(0, kGive) + rec();                              // 40
    c = c + push_int(1) + push_int(2) + sys(0, kNoFn) + rec();                // 0: not handled
    const std::size_t call_at = c.size();
    c = c + call(4, 0) + rec();                                               // 11, from the routine
    c = c + far_call(4, 0) + rec();                                           // 11 again
    c = c + misc(1);                                                          // exit
    // patch the calls' distances: the routine right after the code
    const std::size_t routine_at = c.size();
    c[call_at + 1] = std::uint16_t(std::int16_t(routine_at - (call_at + 2)));
    const std::size_t far_at = call_at + 4; // after the call and its record
    const std::int32_t far_by = std::int32_t(routine_at) - std::int32_t(far_at + 3);
    c[far_at + 1] = std::uint16_t(far_by & 0xffff);
    c[far_at + 2] = std::uint16_t((std::uint32_t(far_by) >> 16) & 0xffff);
    c = c + routine;

    ai::Machine m(script({{0, 0}}, c), rig.memory, rig.calls);
    expect(m.raise(0), "trigger 0 runs");
    const auto &r = rig.recorded;
    expect(r.size() == 19, "every result recorded: " + std::to_string(r.size()));
    expect(r[0].i() == 2 && r[0].tag == ai::Tag::Int, "sub");
    expect(r[1].f() == 2.0F && r[1].tag == ai::Tag::Float, "fadd");
    expect(r[2].i() == 7, "div by zero keeps the left");
    expect(r[3].f() == 3.0F, "fdiv by zero keeps the left");
    expect(r[4].i() == -4, "arithmetic shift right");
    expect(r[5].i() == -5, "neg");
    expect(r[6].i() == -2, "ftoi truncates");
    expect(r[7].f() == 0.0F, "cos(-pi/2) exactly 0");
    expect(std::fabs(r[8].f() + 3.14159274F) < 1e-6F, "180 degrees: -pi");
    expect(r[9].i() == 42 && r[9].tag == ai::Tag::Int, "the work area keeps the tag");
    expect(r[10].i() == 42, "an address plus an integer, loaded");
    expect(r[11].i() == 9, "store");
    expect(r[12].i() == 42 && r[12].tag == ai::Tag::Unset, "copy: the value, its tag unset");
    expect(r[13].i() == 9, "dup");
    expect(r[14].i() == 1, "jz taken");
    expect(r[15].i() == 40, "a handled call gives back");
    expect(r[16].i() == 0, "an unhandled call gives back 0");
    expect(r[17].i() == 11 && r[18].i() == 11, "call and far call, and their returns");
    expect(rig.reports.size() == 1 && rig.reports[0].find("0:3, not handled") != std::string::npos,
           "the unhandled call reported once");
    expect(rig.memory.regions() == 2, "the event's thread dropped (the code and the work area left)");
}

void test_threads()
{
    Rig rig;
    // trigger 1 (main): count, yield, again; trigger 6 (an event): count, yield, count (never reached)
    const Words main = sys(0, kCount) + misc(0) + jump(0, -5);
    const Words event = sys(0, kCount) + misc(0) + sys(0, kCount) + misc(1);
    const Words child = sys(0, kCount) + misc(1);
    ai::Machine m(script({{1, 0}, {6, main.size()}}, main + event + child), rig.memory, rig.calls);
    expect(m.start_main(1) != nullptr, "main started");
    m.tick();
    m.tick();
    m.tick();
    expect(rig.count == 3, "main ran once a tick");
    expect(m.raise(6) && rig.count == 4, "the event ran to its first yield, at once");
    expect(!m.raise(13), "no trigger 13: nothing");
    const std::uint32_t child_at = std::uint32_t(first(2) + main.size() + event.size());
    expect(m.start_routine(nullptr, child_at) != nullptr, "a child");
    m.tick();
    expect(rig.count == 6, "main and its child in one tick");
    m.tick();
    expect(rig.count == 7, "the child ended and was dropped");
    expect(m.main_thread()->children.empty(), "no child left");

    // a loose thread (2:20's): not run by tick(), run by tick_loose(), dropped once ended
    expect(m.start_loose(child_at) != nullptr && m.has_loose(), "a loose thread");
    m.tick();
    expect(rig.count == 8, "tick() runs main alone");
    m.tick_loose();
    expect(rig.count == 9 && !m.has_loose(), "tick_loose() runs it; ended, it is dropped");

    // a thread ended by a call of its own runs on to its yield, then no more (KH2: FUN_1403e1910 only lists it)
    Rig own;
    const Words ends = sys(0, kEndSelf) + sys(0, kCount) + misc(0) + sys(0, kCount) + misc(1);
    ai::Machine e(script({{1, 0}}, ends), own.memory, own.calls);
    e.start_main(1);
    e.tick();
    expect(own.count == 1 && e.main_thread()->status == ai::Status::Stopped, "ended, it ran on to its yield");
    e.tick();
    expect(own.count == 1, "and no further");
}

void test_faults()
{
    {
        Rig rig;
        const Words loop = jump(0, -2); // never yields
        ai::Machine m(script({{1, 0}}, loop), rig.memory, rig.calls);
        m.budget = 1000;
        m.start_main(1);
        m.tick();
        expect(m.main_thread()->status == ai::Status::Stopped, "a runaway thread stopped");
        expect(!rig.reports.empty() && rig.reports[0].find("without yielding") != std::string::npos, "and told");
    }
    {
        Rig rig;
        const Words c = push_int(1) + push_int(2) + push_int(3) + misc(1);
        ai::Machine m(script({{1, 0}}, c, "small", 16), rig.memory, rig.calls); // room for two values
        m.start_main(1);
        m.tick();
        expect(m.main_thread()->status == ai::Status::Stopped, "a stack overrun stopped");
    }
    {
        Rig rig;
        const Words c = sys(5, 77) + misc(1); // a call with no known shape
        ai::Machine m(script({{1, 0}}, c), rig.memory, rig.calls);
        m.start_main(1);
        m.tick();
        expect(m.main_thread()->status == ai::Status::Stopped, "an unknown call stops the thread");
    }
    {
        Rig rig;
        const Words c = push_int(0x10) + load(0) + rec() + push_int(0x10) + load(0) + rec() + misc(1);
        ai::Machine m(script({{0, 0}}, c), rig.memory, rig.calls);
        m.raise(0);
        expect(rig.recorded.size() == 2 && rig.recorded[0].i() == 0, "a read outside every region: 0");
        expect(rig.reports.size() == 1, "told once");
    }
}

void test_no_caps()
{
    Rig rig;
    const Words main = misc(0) + jump(0, -3);
    const Words child = misc(0) + jump(0, -3);
    ai::Machine m(script({{1, 0}}, main + child), rig.memory, rig.calls);
    m.start_main(1);
    for (int i = 0; i < 1000; ++i)
        expect(m.start_routine(nullptr, std::uint32_t(first(1) + main.size())) != nullptr, "a child more");
    m.tick();
    expect(m.main_thread()->children.size() == 1000, "1000 children");
    ai::Memory memory;
    std::vector<std::uint32_t> bases;
    for (int i = 0; i < 10000; ++i)
        bases.push_back(memory.add(64, "made"));
    for (const auto b : bases)
        memory.remove(b);
    expect(memory.regions() == 0, "10000 regions made and given back");
}

void test_sixty()
{
    for (const float tick : {1.0F, 2.0F}) {
        Rig rig;
        // a jump to word 0xc06 (byte offset 0x180c), where 20.0 is pushed
        const std::size_t start = first(1);
        Words c = jump(0, 0);
        const std::size_t target = 0xc06;
        c[1] = std::uint16_t(std::int16_t(target - (start + 2)));
        c.resize(target - start, 0x9 | (1 << 6)); // exits, never reached
        c = c + push_float(20.0F) + rec() + misc(1);
        ai::Machine m(script({{0, 0}}, c), rig.memory, rig.calls);
        m.tick_length = tick;
        m.raise(0);
        expect(rig.recorded.size() == 1, "the patched push recorded");
        expect(rig.recorded[0].f() == (tick < 2.0F ? 18.0F : 20.0F), "20.0 at 0x180c is 18.0 at 60 a second");
    }
}

void test_reading()
{
    bool refused = false;
    try {
        const std::uint8_t tiny[4] = {};
        ai::read_script(tiny, sizeof tiny);
    } catch (const std::runtime_error &) {
        refused = true;
    }
    expect(refused, "a short script refused");
    // a BAR with a model and a script
    const Words c = misc(1);
    auto s = script({{0, 0}}, c, "bar_ai");
    std::vector<std::uint8_t> bar = {'B', 'A', 'R', 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto entry = [&](unsigned kind, const char *name, std::uint32_t at, std::uint32_t size) {
        bar.push_back(std::uint8_t(kind));
        bar.push_back(0);
        bar.push_back(0);
        bar.push_back(0);
        for (int i = 0; i < 4; ++i)
            bar.push_back(std::uint8_t(name[i]));
        for (const auto v : {at, size})
            for (int i = 0; i < 4; ++i)
                bar.push_back(std::uint8_t(v >> (8 * i)));
    };
    const std::uint32_t data_at = 16 + 32;
    entry(4, "mdl_", data_at, 4);
    entry(3, "m_ex", data_at + 4, std::uint32_t(s->data.size()));
    bar.insert(bar.end(), {'m', 'o', 'd', 'l'});
    bar.insert(bar.end(), s->data.begin(), s->data.end());
    const auto found = ai::scripts_in(bar.data(), bar.size());
    expect(found.size() == 1 && found[0].first == "m_ex" && found[0].second.name == "bar_ai", "the BAR's script");
}

} // namespace

int main()
{
    try {
        test_operations();
        test_threads();
        test_faults();
        test_no_caps();
        test_sixty();
        test_reading();
    } catch (const std::exception &e) {
        std::cerr << "kh2 ai: " << e.what() << "\n";
        return 1;
    }
    std::cout << "kh2 ai: all checks passed\n";
    return 0;
}
