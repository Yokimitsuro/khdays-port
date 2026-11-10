// The port's answers to KINGDOM HEARTS II's calls (calls.h): each names the function of KH2FM.exe it follows. A
// call's arguments are in the order the script pushed them; "the actor" is the object an AI handle's + 4 names.
#include "calls.h"

#include "actor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace khdays::kh2_enemies {
namespace {

std::uint32_t chance_state = 1; // DAT_1407535c0

std::uint32_t next_chance()
{
    chance_state *= 0x10dcdu;
    return chance_state;
}

constexpr float kPi = 3.14159265358979F;

// FUN_1403b6f20: an angle into (-pi, pi]
float wrapped(float a)
{
    while (a > kPi)
        a -= 2.0F * kPi;
    while (a <= -kPi)
        a += 2.0F * kPi;
    return a;
}

// a vector at a script address, its NaN components made 0 where it lies (the calls do so first)
struct Vec {
    float v[4] = {};
};
Vec read_vec(ai::Machine &m, std::uint32_t at)
{
    Vec r;
    m.memory().read(at, r.v, sizeof r.v);
    bool fixed = false;
    for (float &f : r.v)
        if (std::isnan(f)) {
            f = 0.0F;
            fixed = true;
        }
    if (fixed)
        m.memory().write(at, r.v, sizeof r.v);
    return r;
}

// Each vector call keeps its own result buffer (KH2: a static each, its address given back); one region per call
// and memory, made on first use.
std::uint32_t result_buffer(ai::Machine &m, const char *call)
{
    static std::map<std::pair<const ai::Memory *, std::string>, std::uint32_t> buffers;
    auto &at = buffers[{&m.memory(), call}];
    if (at == 0)
        at = m.memory().add(16, std::string("the vector of ") + call);
    return at;
}

// `shared`: the one vector several actor calls give back (DAT_140756b48: 1:120, 1:147, 1:201), which one of the
// PC's constants reads too (the machine's shared_vector)
ai::Value give_vec(ai::Machine &m, const char *call, const Vec &v)
{
    const std::uint32_t at = result_buffer(m, call);
    if (std::strcmp(call, "shared") == 0)
        m.shared_vector = at;
    m.memory().write(at, v.v, sizeof v.v);
    return ai::Value::of_address(at);
}

Actor *running(ai::Machine &m)
{
    return static_cast<Actor *>(m.user());
}

// The object a call's first argument names: some calls take the object itself (1:11, 1:14, 1:15 ...), most the AI
// handle whose + 4 is the object (1:7, 1:8 ...); the script's own actor when neither.
Object *object_of(ai::Machine &m, const ai::Value &arg)
{
    if (Object *o = Object::at(m.memory(), arg.bits))
        return o;
    std::uint32_t inner = 0;
    if (m.memory().read(arg.bits + 4, &inner, 4))
        if (Object *o = Object::at(m.memory(), inner))
            return o;
    return running(m);
}

Actor *self_of(ai::Machine &m, const ai::Value &arg)
{
    Object *o = object_of(m, arg);
    Actor *a = o != nullptr ? o->actor() : nullptr;
    return a != nullptr ? a : running(m);
}

Vec3 xyz(const Vec &v)
{
    return {v.v[0], v.v[1], v.v[2]};
}

// A target slot (actor + 0xbf8; FUN_1403be2b0 fills one): how its target is chosen (2 the owner itself, else the
// player), its owner, 16 bytes in the scripts' memory
constexpr std::uint32_t kSlotMode = 0, kSlotOwner = 4, kSlotSet = 8;

std::uint32_t slot_of(Actor &a)
{
    if (a.target_slot == 0)
        a.target_slot = a.memory().add(16, a.name() + "'s target slot");
    return a.target_slot;
}

Object *target_in(ai::Machine &m, std::uint32_t slot)
{
    ai::Memory &mem = m.memory();
    std::uint32_t mode = 0, owner = 0, set = 0;
    if (!mem.read(slot + kSlotMode, &mode, 4) || !mem.read(slot + kSlotOwner, &owner, 4) ||
        !mem.read(slot + kSlotSet, &set, 4) || set == 0)
        return nullptr;
    Object *o = Object::at(mem, owner);
    Actor *a = o != nullptr ? o->actor() : nullptr;
    if (mode == 2 || a == nullptr || a->world == nullptr)
        return o;
    return a->world->player(*a);
}

} // namespace

void seed_chance(std::uint32_t seed)
{
    chance_state = seed != 0 ? seed : 1;
}

int read_shapes(ai::Calls &calls, const std::string &text)
{
    std::istringstream in(text);
    std::string line;
    int read = 0;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        unsigned kind = 0, number = 0, args = 0, back = 0;
        char colon = 0;
        std::istringstream l(line);
        if (!(l >> kind >> colon >> number >> args >> back) || colon != ':')
            continue;
        calls.set_shape(kind, number, args, back != 0);
        ++read;
    }
    return read;
}

// ------------------------------------------------------------------------------------------------- kind 0

void add_math(ai::Calls &calls)
{
    // 0:21 how far the second vector's way along the ground is turned from the first's: atan2(x, z) of each, the
    // difference into (-pi, pi] (FUN_14041d1b0, FUN_1403b6f20)
    calls.set(0, 21, [](ai::Machine &m, std::span<ai::Value> a) {
        const Vec from = read_vec(m, a[0].bits), to = read_vec(m, a[1].bits);
        a[0] = ai::Value::of_float(wrapped(std::atan2(to.v[0], to.v[2]) - std::atan2(from.v[0], from.v[2])));
    });
    // 0:35 a vector scaled where it lies, its four components (FUN_14041d220 -> FUN_14013fcc0)
    calls.set(0, 35, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v = read_vec(m, a[0].bits);
        for (float &f : v.v)
            f *= a[1].f();
        m.memory().write(a[0].bits, v.v, sizeof v.v);
    });
    // 2:67 KH2's next running number (FUN_1403cf490: DAT_142a24afc, from 0 a call before, 0x100 past 0x10000000)
    calls.set(2, 67, [](ai::Machine &, std::span<ai::Value> a) {
        static std::uint32_t last = 0;
        last = last + 1 > 0x10000000u ? 0x100u : last + 1;
        a[0] = ai::Value::of_int(std::int32_t(last));
    });
    // 0:3 the tick's length (DAT_140717480)
    calls.set(0, 3, [](ai::Machine &m, std::span<ai::Value> a) { a[0] = ai::Value::of_float(m.tick_length); });
    // 0:4, 0:5 a + b, a - b into the call's own vector (FUN_14013fce0, FUN_14013fd20)
    calls.set(0, 4, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec x = read_vec(m, a[0].bits), y = read_vec(m, a[1].bits);
        for (int i = 0; i < 4; ++i)
            x.v[i] += y.v[i];
        a[0] = give_vec(m, "0:4", x);
    });
    calls.set(0, 5, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec x = read_vec(m, a[0].bits), y = read_vec(m, a[1].bits);
        for (int i = 0; i < 4; ++i)
            x.v[i] -= y.v[i];
        a[0] = give_vec(m, "0:5", x);
    });
    // 0:6 a vector's length (FUN_140140f50: x, y, z)
    calls.set(0, 6, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        m.memory().read(a[0].bits, v.v, sizeof v.v);
        a[0] = ai::Value::of_float(std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]));
    });
    // 0:7 made unit in place, its length given back (FUN_140142080: left as it is when 0)
    calls.set(0, 7, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        m.memory().read(a[0].bits, v.v, sizeof v.v);
        const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        if (n != 0.0F) {
            const float k = 1.0F / n;
            for (int i = 0; i < 3; ++i)
                v.v[i] *= k;
            m.memory().write(a[0].bits, v.v, 12);
        }
        a[0] = ai::Value::of_float(n);
    });
    // 0:13 a vector turned about the up by an angle into the call's own vector (FUN_1403b7680: FUN_1403b6f20's
    // angle, a turn matrix)
    calls.set(0, 13, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec x = read_vec(m, a[0].bits);
        const Vec3 t = turned(xyz(x), wrapped(a[1].f()));
        x.v[0] = t[0], x.v[1] = t[1], x.v[2] = t[2];
        a[0] = give_vec(m, "0:13", x);
    });
    // 0:42 the way a vector points along the ground: atan2(x, z) (FUN_14041d250)
    calls.set(0, 42, [](ai::Machine &m, std::span<ai::Value> a) {
        const Vec v = read_vec(m, a[0].bits);
        a[0] = ai::Value::of_float(std::atan2(v.v[0], v.v[2]));
    });
    // 0:68 a vector turned about the x by an angle into the call's own vector (FUN_14041d100 -> FUN_1403b75f0: the
    // angle made one of (-pi, pi], FUN_1401411b0's turn, the vector times it -- y cos - z sin, y sin + z cos: the
    // Armored Knight's launch, ahead turned 80 degrees up, then about the up by 0:13)
    calls.set(0, 68, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec x = read_vec(m, a[0].bits);
        const float r = wrapped(a[1].f()), c = std::cos(r), s = std::sin(r), y = x.v[1], z = x.v[2];
        x.v[1] = y * c - z * s;
        x.v[2] = y * s + z * c;
        a[0] = give_vec(m, "0:68", x);
    });
    // 0:16 below n: x / ceil(2^32 / n) (FUN_14041dbf0)
    calls.set(0, 16, [](ai::Machine &, std::span<ai::Value> a) {
        const std::uint64_t n = a[0].bits;
        const std::uint32_t x = next_chance();
        a[0] = ai::Value::of_int(n == 0 ? 0 : std::int32_t(std::uint64_t(x) / ((n - 1 + 0x100000000ull) / n)));
    });
    // 0:17 f x a draw in [0, 1); 0:18 a + (b - a) x a draw (FUN_14041dc40, FUN_14041dc80; 2^-32, DAT_1405b4d30)
    calls.set(0, 17, [](ai::Machine &, std::span<ai::Value> a) {
        const float draw = float(next_chance()) * 2.32830644e-10F;
        a[0] = ai::Value::of_float(draw * a[0].f());
    });
    calls.set(0, 18, [](ai::Machine &, std::span<ai::Value> a) {
        const float x = float(next_chance()); // KH2's order: (b - a) x the draw x 2^-32 + a
        a[0] = ai::Value::of_float((a[1].f() - a[0].f()) * x * 2.32830644e-10F + a[0].f());
    });
    // 0:36 a vector times f into the call's own vector (FUN_14013fcc0)
    calls.set(0, 36, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec x = read_vec(m, a[0].bits);
        for (float &f : x.v)
            f *= a[1].f();
        a[0] = give_vec(m, "0:36", x);
    });
    // 0:47 memory of n bytes (FUN_140152430: the game's allocator); 0:79 given back four calls later (FUN_1403f63a0)
    calls.set(0, 47, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_address(m.memory().add(a[0].bits, "0:47's memory"));
    });
    calls.set(0, 79, [](ai::Machine &m, std::span<ai::Value> a) {
        static std::map<const ai::Memory *, std::vector<std::uint32_t>> rings;
        auto &ring = rings[&m.memory()];
        if (a[0].bits == 0)
            return; // a handle of none (an effect not made)
        ring.push_back(a[0].bits);
        if (ring.size() > 4) {
            m.memory().remove(ring.front());
            ring.erase(ring.begin());
        }
    });
    // 0:0, 0:2 nothing in the PC game (FUN_14041c9f0, FUN_14041ca10: empty; a debugging print once): 0:2's text
    // told where the memory tells its misses (the shell's trace: "avoid start", "high_attack_min" ...); 0:85 an
    // effect's handle let go (FUN_14041e1c0 -> FUN_1402be8f0): the port's effects end of their own
    calls.set(0, 0, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(0, 1, [](ai::Machine &, std::span<ai::Value>) {}); // FUN_14041ca00: empty too
    calls.set(0, 2, [](ai::Machine &m, std::span<ai::Value> a) {
        if (m.memory().report)
            m.memory().report("its script prints \"" + name_at(m.memory(), a[0].bits) + "\"");
    });
    // 0:32 an effect of the screen or of time for n frames (FUN_14041ddb0 -> FUN_140156c90 on FUN_140156dc0's
    // manager; KH2's hit starts it for 10 frames): what it shows is not known -- nothing here
    calls.set(0, 32, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(0, 85, [](ai::Machine &, std::span<ai::Value>) {});
    // 0:15 a bit of KH2's story (FUN_14039a980: DAT_1409ab540's bits, 0xffff none): none of them in Days's field
    calls.set(0, 15, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 0:23 a value without its sign (FUN_14041ca90; given back marked a whole number, as KH2 marks it)
    calls.set(0, 23, [](ai::Machine &, std::span<ai::Value> a) {
        a[0] = ai::Value{a[0].bits & 0x7fffffffu, ai::Tag::Int};
    });
    // 1:228 / 1:229 a table's record of 12 bytes: its first, its second word (FUN_140431030, FUN_140431070); 2:25 a
    // record's + 0x18 (FUN_140432b00)
    calls.set(1, 228, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_int(std::int32_t(m.memory().u32(a[0].bits + 12u * a[1].bits)));
    });
    calls.set(1, 229, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_int(std::int32_t(m.memory().u32(a[0].bits + 12u * a[1].bits + 4u)));
    });
    calls.set(2, 25, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_int(std::int32_t(m.memory().u32(a[0].bits + 0x18u)));
    });
    // 1:230 a table's record of 12 bytes (its first two words whole numbers): the first over the second
    // (FUN_1404310f0; marked a whole number)
    calls.set(1, 230, [](ai::Machine &m, std::span<ai::Value> a) {
        const std::uint32_t at = a[0].bits + 12u * a[1].bits;
        const float ratio = float(std::int32_t(m.memory().u32(at))) / float(std::int32_t(m.memory().u32(at + 4)));
        std::uint32_t bits = 0;
        std::memcpy(&bits, &ratio, 4);
        a[0] = ai::Value{bits, ai::Tag::Int};
    });
    // 2:21 memory several objects share by a number (FUN_1403f6df0: n + 1 bytes, zeroed, the same for the same
    // number)
    calls.set(2, 21, [](ai::Machine &m, std::span<ai::Value> a) {
        static std::map<std::pair<const ai::Memory *, std::uint32_t>, std::uint32_t> shared;
        std::uint32_t &at = shared[{&m.memory(), a[0].bits}];
        if (at == 0)
            at = m.memory().add(a[1].bits + 1, "2:21's shared memory");
        a[0] = ai::Value::of_address(at);
    });
}

// ------------------------------------------------------------------------------------------------- states

void add_states(ai::Calls &calls)
{
    // 1:90 a state every object enters, by its name, at once or each after a time at random below the value
    // (FUN_1403cacd0 lists it, FUN_1403c9d40 sends it to every object): the world's
    calls.set(1, 90, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        if (x->world != nullptr)
            x->world->broadcast(*x, name_at(m.memory(), a[1].bits), a[2].f());
    });
    // 1:112 a routine of the script as the actor's attached thread (FUN_14042ca20: FUN_1403e1970, FUN_1403d76d0 on
    // + 0xd48, the one before ended), the actor its first value, the third its second when it is an object
    calls.set(1, 112, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        Actor *x = self_of(m, a[0]);
        Object *on = is_player(o) ? o : x; // one of Days's players: the player's thread (Cerberus's command)
        std::uint32_t entry = a[1].bits;
        if (a[1].tag == ai::Tag::Address && entry >= m.code())
            entry = (entry - m.code()) / 2;
        const ai::Value values[2] = {ai::Value::of_address(on->address()), a[2]};
        // the second when it names an object (itself, or an AI handle whose + 4 is one)
        std::uint32_t inner = 0;
        const bool second = Object::at(m.memory(), a[2].bits) != nullptr ||
                            (m.memory().read(a[2].bits + 4, &inner, 4) && Object::at(m.memory(), inner) != nullptr);
        ai::Thread *t = m.start_routine(nullptr, entry, std::span<const ai::Value>(values, second ? 2u : 1u));
        if (t == nullptr)
            return;
        if (on == x)
            x->attach_thread(m, t->id);
        else if (running(m)->world != nullptr)
            running(m)->world->player_thread(*on, m, t->id);
    });
    // 1:5 a state table emptied: its 128 entries' ids 0 (FUN_1403ca790)
    calls.set(1, 5, [](ai::Machine &m, std::span<ai::Value> a) {
        for (std::uint32_t i = 0; i < 128; ++i)
            m.memory().set_u32(a[0].bits + 0x20 * i, 0);
    });
    // 1:6 a state registered: the entry with the same id, else the first free (FUN_1403ca0e0)
    calls.set(1, 6, [](ai::Machine &m, std::span<ai::Value> a) {
        const std::uint32_t table = a[0].bits;
        const std::string name = name_at(m.memory(), a[1].bits);
        std::uint32_t at = 0;
        for (std::uint32_t i = 0; i < 128 && at == 0; ++i) {
            const std::uint32_t id = m.memory().u32(table + 0x20 * i);
            if (id != 0 && name_at(m.memory(), id) == name)
                at = table + 0x20 * i;
        }
        for (std::uint32_t i = 0; i < 128 && at == 0; ++i)
            if (m.memory().u32(table + 0x20 * i) == 0)
                at = table + 0x20 * i;
        if (at == 0)
            return;
        std::uint8_t e[0x20] = {};
        std::memcpy(e, &a[1].bits, 4);
        const auto u16 = [&](std::size_t o, std::uint32_t v) {
            e[o] = std::uint8_t(v);
            e[o + 1] = std::uint8_t(v >> 8);
        };
        u16(4, a[2].bits);
        u16(6, a[2].bits >> 16);
        u16(8, a[3].bits);
        for (std::size_t k = 0; k < 8; ++k) // +0xa .. +0x18: the 8 values after
            u16(0xa + 2 * k, a[4 + k].bits);
        m.memory().write(at, e, sizeof e);
    });
    // 1:7 the table and handle given to the state machine (actor + 0x498, + 0x4a0)
    calls.set(1, 7, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]))
            x->states().set_table(a[1].bits, a[0].bits);
    });
    // 1:8 enter, 1:9 queue, 1:10 pending, 1:127 clear (FUN_1403cae60, FUN_1403cab00, FUN_1403ca440)
    calls.set(1, 8, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]))
            x->states().enter(*x, name_at(m.memory(), a[1].bits));
    });
    calls.set(1, 9, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]))
            x->states().queue(*x, name_at(m.memory(), a[1].bits));
    });
    calls.set(1, 10, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x != nullptr && x->states().pending() ? 1 : 0);
    });
    calls.set(1, 127, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]))
            x->states().clear(*x);
    });
}

// ------------------------------------------------------------------------------------------------- the actor's fields

void add_actor_fields(ai::Calls &calls)
{
    // 1:65 flag 0x4000000 at + 0x120; 1:70 / 1:71 any bit there set / cleared (FUN_14042f220, FUN_14042f350,
    // FUN_14042f390)
    calls.set(1, 65, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]))
            x->set_bits(field::kFlags, 0x4000000u, true);
    });
    calls.set(1, 70, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0])) {
            const std::uint32_t bit = a[1].bits;
            x->set_bits(field::kFlags + 4 * (bit >> 5), 1u << (bit & 31), true);
        }
    });
    calls.set(1, 71, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0])) {
            const std::uint32_t bit = a[1].bits;
            x->set_bits(field::kFlags + 4 * (bit >> 5), 1u << (bit & 31), false);
        }
    });
    // 1:14, 1:78, 2:5 status bits at + 0x18c (1, 9, 9); 1:15 the motion playing's number at + 0x180 (FUN_14042b440,
    // FUN_14042f500, FUN_140431d10, FUN_14042b480)
    calls.set(1, 14, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x ? std::int32_t((x->u32(field::kStatus) >> 1) & 1) : 0);
    });
    const auto bit9 = [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x ? std::int32_t((x->u32(field::kStatus) >> 9) & 1) : 0);
    };
    calls.set(1, 78, bit9);
    calls.set(2, 5, bit9);
    calls.set(1, 15, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x ? std::int32_t(x->u32(0x180)) : 0);
    });
    // 1:147 its place (+ 0x670) given back as a vector (FUN_14042b7b0)
    calls.set(1, 147, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        if (Object *x = object_of(m, a[0]))
            for (int i = 0; i < 4; ++i)
                v.v[i] = x->f(field::kPlace + 4 * i);
        a[0] = give_vec(m, "shared", v);
    });
    // 1:39 its battle number n (FUN_1403b5df0: + 0x9f4 + 4 n); none of Days's: 0
    calls.set(1, 39, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x ? std::int32_t(x->u32(field::kParams + 4 * (a[1].bits & 0xff))) : 0);
    });
    // 1:64 this tick's travel (+ 0xa28), 1:62 the tick's before (+ 0xa38), each into the call's own vector
    // (FUN_14042f160, FUN_14042f060)
    const auto travel = [](std::uint32_t at, const char *call) {
        return [at, call](ai::Machine &m, std::span<ai::Value> a) {
            Vec v;
            if (Object *x = object_of(m, a[0]))
                for (std::uint32_t i = 0; i < 4; ++i)
                    v.v[i] = x->f(at + 4 * i);
            a[0] = give_vec(m, call, v);
        };
    };
    calls.set(1, 64, travel(field::kTravel, "1:64"));
    calls.set(1, 62, travel(field::kTravelBefore, "1:62"));
    // 1:104 / 1:105 bit 6 at + 0x8c8 set / cleared (FUN_14042fbf0, FUN_14042fc10)
    calls.set(1, 104, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]))
            x->set_bits(field::kFlags8c8, 0x40u, true);
    });
    calls.set(1, 105, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]))
            x->set_bits(field::kFlags8c8, 0x40u, false);
    });
    // 1:309 + 0x6c8 bit 7 (FUN_14042b260)
    calls.set(1, 309, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x ? std::int32_t((x->u32(field::kFlags6c8) >> 7) & 1) : 0);
    });
    // 1:66 + 0x120's 0x8000000 set (FUN_14042f250; the landing sets it too); 1:69 the float at + 0x19c; 1:84 the
    // object at + 0x5c0 (2:9 makes it; none here); 1:97 where + 0x128 lies; 1:179 + 0x118, the motion of its fall
    calls.set(1, 66, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->set_bits(field::kFlags, 0x8000000u, true);
    });
    calls.set(1, 69, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_float(self_of(m, a[0])->f(0x19c));
    });
    calls.set(1, 84, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_address(self_of(m, a[0])->u32(0x5c0));
    });
    calls.set(1, 97, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_address(self_of(m, a[0])->address() + 0x128);
    });
    calls.set(1, 179, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->motion.after = std::int32_t(a[1].bits);
    });
    // 1:211 / 1:212 bit n of + 0xd10 set / cleared (FUN_14042ba50, FUN_14042ba80)
    calls.set(1, 211, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->set_bits(0xd10, 1u << (a[1].bits & 31u), true);
    });
    calls.set(1, 212, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->set_bits(0xd10, 1u << (a[1].bits & 31u), false);
    });
    // 1:140 whether a target record's object (its + 8) is there (FUN_1403bde30; its other test, the player's lock,
    // not here); 1:143 an actor found by its placing record's number (FUN_1403b41e0, 0x70 for 0): none of Days's,
    // the slot left empty; 1:162 the actor put on one of KH2's lists (FUN_1403bd410): nothing here
    // ... or its + 0's an actor in play with a shape of kind 3 in its groups on (what 6:5 finds)
    calls.set(1, 140, [](ai::Machine &m, std::span<ai::Value> a) {
        bool there = Object::at(m.memory(), m.memory().u32(a[0].bits + 8)) != nullptr;
        if (Object *o = there ? nullptr : Object::at(m.memory(), m.memory().u32(a[0].bits)))
            if (Actor *x = o->actor();
                x != nullptr && x->world != nullptr && (x->u32(field::kFlags) & 0x10080000u) == 0)
                there = !x->world->shapes(*x, 3).empty();
        a[0] = ai::Value::of_int(there ? 1 : 0);
    });
    calls.set(1, 143, [](ai::Machine &m, std::span<ai::Value> a) { m.memory().set_u32(a[0].bits + 4, 0); });
    calls.set(1, 162, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:107 + 0x2a8 set (FUN_14042fce0); 1:130 two objects told apart by their places in KH2's list (FUN_14042b5d0:
    // their + 4s' difference, 0 when either is none); 1:213 its character's number (FUN_1403b5b20: + 8's record's
    // + 0x4c, 00objentry.bin's); 1:225 whether its class lets hits through now (+ 0x6c8 bit 1, then its class's
    // virtual + 0xb8): the port's let them, 0
    calls.set(1, 107, [](ai::Machine &m, std::span<ai::Value> a) { self_of(m, a[0])->set_u32(0x2a8, a[1].bits); });
    calls.set(1, 130, [](ai::Machine &m, std::span<ai::Value> a) {
        const std::uint32_t one = a[0].bits != 0 ? m.memory().u32(a[0].bits + 4) : 0;
        const std::uint32_t two = a[1].bits != 0 ? m.memory().u32(a[1].bits + 4) : 0;
        a[0] = ai::Value::of_int(one != 0 && two != 0 ? std::int32_t(one - two) : 0);
    });
    // Days's players answer KH2's player's number (+ 0x4c of P_EX100's record: Sora's 1; Roxas's is 14, Donald's 2):
    // they stand for KH2's player, whose motions and commands the enemies' sets carry (Cerberus's reaction command
    // begins on 1)
    calls.set(1, 213, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_int(is_player(object_of(m, a[0])) ? 1 : 0);
    });
    calls.set(1, 225, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:157 an event sequence started by a name of up to four letters from the actor's .a.us entries of type 0x16
    // (FUN_1403db2f0), its camera placed from the third argument when given (FUN_1403ad270): a reaction command's
    // (Cerberus's "00" as it takes the player, "10" to "30" as each step begins)
    calls.set(1, 157, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        if (x != nullptr && x->world != nullptr)
            x->world->event(*x, name_at(m.memory(), a[1].bits), object_of(m, a[2]));
    });
    // 1:133 / 1:134 the object's class's virtuals + 0x80 / + 0x88 (its native code): nothing here
    calls.set(1, 133, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(1, 134, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:201 its facing as a direction (FUN_1403b56e0)
    calls.set(1, 201, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        if (Object *x = object_of(m, a[0])) {
            const Vec3 d = facing_of(x->yaw());
            v.v[0] = d[0], v.v[1] = d[1], v.v[2] = d[2];
        }
        a[0] = give_vec(m, "shared", v);
    });
    // 1:57 bit 0 of the actor's class's byte + 0x0c (its class at + 0x0c, FUN_1403b4da0; the classes at 0x140750d30):
    // class 1, off the ground (a jump: FUN_140409ad0 is FUN_1403cbb00 then class 1; a fall, FUN_14053b920), has it,
    // class 0, on the ground (the landing's FUN_1403cb940), not -- the scripts wait for it after a jump (`while
    // !1:57 yield`) and act on it in the air: its jump or its fall
    calls.set(1, 57, [](ai::Machine &m, std::span<ai::Value> a) {
        const Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x != nullptr && (x->u32(field::kFlags) & bit::kInAir) != 0 ? 1 : 0);
    });
    // 1:124 its entry's flag (FUN_1403ba700); 1:56 FUN_1403ba4a0's value; 1:110 its battle record's byte + 0x1c
    // (FUN_1403b50d0: -1 without one): none of these is Days's, so the plain answers -- 0, and -1 for no record
    calls.set(1, 124, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:56 whether it is on the screen (the enemies leave sooner off it: their "leave" distance off the screen is the
    // shorter): the world's answer
    calls.set(1, 56, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x != nullptr && x->world != nullptr && x->world->on_screen(*x) ? 1 : 0);
    });
    calls.set(1, 110, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(-1); });
    // 1:60 off the ground or held (FUN_14042efc0: FUN_1403ba9a0's class bit 0, + 0x18c & 0x1000020, + 0x120 bit 23;
    // then + 0x18c bit 6)
    calls.set(1, 60, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        const bool off =
            x != nullptr && ((x->u32(field::kStatus) & 0x1000020u) != 0 || (x->u32(field::kFlags) & bit::kInAir) != 0 ||
                             (x->u32(field::kStatus) & 0x40u) != 0);
        a[0] = ai::Value::of_int(off ? 1 : 0);
    });
    // 1:150, 1:151 bit n - 1 of the actor's bytes + 0x2ac and + 0x2ad (FUN_14042d1e0, FUN_14042d230): its motion's
    // ranges 16 to 19 on now, its triggers 9 to 12 passed this tick (field::kMotionMarks; the Strafer's and the Magnum
    // Loader's ways of attacking)
    for (const std::uint32_t n : {150u, 151u}) {
        const std::uint32_t at = n == 150u ? field::kMotionMarks : field::kMotionMarks + 1u;
        calls.set(1, n, [at](ai::Machine &m, std::span<ai::Value> a) {
            Object *x = object_of(m, a[0]);
            const int bit = std::int32_t(a[1].bits) - 1;
            const std::uint32_t byte = x != nullptr ? (x->u32(at & ~3u) >> (8 * (at & 3u))) & 0xffu : 0u;
            a[0] = ai::Value::of_int(bit >= 0 && bit < 8 && (byte & (1u << bit)) != 0 ? 1 : 0);
        });
    }
    // 2:45 an object's entry number (its + 0x20 record's u16 at + 2; FUN_1404320a0): the Wight Knight asks it of a
    // message's sender, which in Days's field is none of KH2's objects: 0
    calls.set(2, 45, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 2:8 whether a hit's reaction kind (+ 0x26) is past 1 (FUN_140431f70, FUN_1403ce980: 1 and 12 are 1; 3, 6, 8,
    // 10, 13, 16, 18 are 3; 4, 7, 9, 11, 15, 17 are 4; the others themselves): trigger 6's hit, a Days hit a small
    // reaction (2)
    calls.set(2, 8, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint8_t kind = 0;
        m.memory().read(a[0].bits + 0x26, &kind, 1);
        const unsigned group = kind == 1 || kind == 12 ? 1u : kind;
        a[0] = ai::Value::of_int(group > 1 ? 1 : 0);
    });
    // the world's flags: 1:23 DAT_140717008 (the script tests 10), 1:24, 1:34 DAT_142a11478 (FUN_1403ac220), 1:301
    // FUN_1403bbea0, 1:171 / 1:206 DAT_142a11400's bits 2 / 7 -- KH2's field busy (an event, a pause), which Days's
    // field never is for its enemies: 0
    // 1:24 DAT_140717009, the byte after 1:23's (the Wight Knight tests 9) -- the same
    for (const unsigned n : {23u, 24u, 34u, 301u, 171u, 206u})
        calls.set(1, n, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
}

// ------------------------------------------------------------------------------------------------- motions

void add_motions(ai::Calls &calls)
{
    // 1:29 another object plays a motion of this one's set (FUN_1403c6cc0: its motion set made this one's, + 0x2b8,
    // its motion number + 0x180), from a frame; the motion given back, what its controller then plays (+ 0x184, 1:238:
    // the Armored Knight's flight runs while the two are the same)
    calls.set(1, 29, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        Object *o = object_of(m, a[1]);
        Actor *other = o != nullptr ? o->actor() : nullptr;
        bool played = other != nullptr;
        if (other != nullptr) {
            other->motions = x->motions;
            other->play(std::int32_t(a[2].bits), 0.0F, a[3].f());
        } else if (is_player(o) && x->world != nullptr) { // one of Days's players: the shell plays it
            played = x->world->player_motion(*o, *x, std::int32_t(a[2].bits), a[3].f());
        }
        a[0] = ai::Value::of_int(played ? std::int32_t(a[2].bits) : 0);
    });
    // 1:94 whether an object is there and in play (FUN_14042b0e0: FUN_1403ba720, none of + 0x120's 0x10080000):
    // the Magnum Loader's thread on the player asks it of the loader; 1:101 whether the object an AI handle names
    // plays another's motion (FUN_14042d360: + 0x18c bit 14), what that thread runs while
    calls.set(1, 94, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = Object::at(m.memory(), a[0].bits);
        a[0] = ai::Value::of_int(o != nullptr && (o->u32(field::kFlags) & 0x10080000u) == 0 ? 1 : 0);
    });
    calls.set(1, 101, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        a[0] = ai::Value::of_int(o != nullptr && (o->u32(field::kStatus) & bit::kOtherMotion) != 0 ? 1 : 0);
    });
    // 1:13 a motion asked of the object's controller (FUN_1403c8310: its queue of four, taken at its next update):
    // played at once here; a player's own (Cerberus's 0, its idle, at the end of its reaction command) gives the
    // player back to Days
    calls.set(1, 13, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (Actor *x = o != nullptr ? o->actor() : nullptr)
            x->play(std::int32_t(a[1].bits), a[2].f(), 0.0F);
        else if (is_player(o) && running(m)->world != nullptr)
            running(m)->world->player_own_motion(*o, std::int32_t(a[1].bits));
    });
    // 1:238 the motion its controller plays (+ 0x184, FUN_14042d3b0): Cerberus's reaction command waits for the
    // player's to change
    calls.set(1, 238, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        a[0] = ai::Value::of_int(o != nullptr ? std::int32_t(o->u32(field::kMotionNow)) : 0);
    });
    // 1:11 a motion from its start, blended (FUN_1403c86a0 on actor + 0x158); 1:61 from a frame; 1:12 one that is not
    // already playing (FUN_1403c6dc0); 1:67 a motion's length (FUN_1403c78f0's motion)
    calls.set(1, 11, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->play(std::int32_t(a[1].bits), a[2].f(), 0.0F);
    });
    calls.set(1, 61, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->play(std::int32_t(a[1].bits), a[3].f(), a[2].f());
    });
    calls.set(1, 12, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->play(std::int32_t(a[1].bits), a[2].f(), 0.0F, false);
    });
    const auto length = [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const int id = std::int32_t(a[1].bits) < 0 ? x->motion.id : std::int32_t(a[1].bits);
        a[0] = ai::Value::of_float(x->motions != nullptr && x->motions->has(id) ? x->motions->length(id) : 0.0F);
    };
    calls.set(1, 67, length);
    // 1:68 the same through its other virtual (FUN_1403c7950); -1 the motion playing
    calls.set(1, 68, length);
    // 1:55, 1:168 a jump (FUN_1403cbb00, FUN_1404233f0): a vector -- its x, z the way (made one long, then times its
    // w, the speed along: + 0xe0), its y the height it rises, either sign (negative: + 0xf0 the speed up
    // sqrt(|2 y / 0.408163|), + 0xf8 y; positive: + 0xf4 the same speed, FUN_1403b73f0, + 0xfc -y) -- then a motion;
    // the flag sets the two after it (+ 0x118 its fall, + 0x11c its landing)
    const auto jump = [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        x->jump.on = true;
        x->dashing = false;
        x->jump.gravity = Jump{}.gravity;
        x->jump.keep = Jump{}.keep; // class 1's steering in the air (FUN_1403cb360)
        x->jump.slow = Jump{}.slow;
        x->jump.up = -std::sqrt(std::fabs(2.0F * v.v[1] / 0.408163F));
        const float flat = std::sqrt(v.v[0] * v.v[0] + v.v[2] * v.v[2]);
        x->jump.along = flat > 0.0F ? Vec3{v.v[0] / flat * v.v[3], 0.0F, v.v[2] / flat * v.v[3]} : Vec3{};
        x->velocity = x->jump.along;
        x->move_clock = 0.0F;
        x->steered = x->jump.along;
        x->set_bits(field::kFlags, bit::kInAir, true);
        x->play(std::int32_t(a[2].bits), a[3].f(), 0.0F);
        x->motion.after = -1;
        x->motion.landing = 5;
        if (a[4].bits != 0) {
            x->motion.after = std::int32_t(a[2].bits) + 1;
            x->motion.landing = std::int32_t(a[2].bits) + 2;
        }
    };
    calls.set(1, 55, jump);
    calls.set(1, 168, jump);
    // 1:156 a jump of the actor's own (FUN_14042ce60 -> FUN_1403cbec0): a vector -- its x, z the way along the
    // ground (made one long, then times its w), its y how high it rises -- the ticks to its top (its pull + 0x10c
    // 2 y / t^2, its rise the pull times t), a value kept at + 0x108 (class 9's steering in the air keeps that much of
    // its velocity: FUN_1403cbdd0); then a motion (FUN_1403c86a0 on + 0x158) and, the flag set, the two after it as
    // 1:55's. KH2 leaves the ground at the motion's take-off event (FUN_14040dd70, + 0x648 2: class 9); the port at once
    calls.set(1, 156, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        const float t = a[2].f();
        const float flat = std::sqrt(v.v[0] * v.v[0] + v.v[2] * v.v[2]);
        x->jump.on = true;
        x->dashing = false;
        x->jump.gravity = t != 0.0F ? 2.0F * v.v[1] / (t * t) : 0.0F;
        x->jump.up = -(x->jump.gravity * t);
        x->jump.along = flat > 0.0F ? Vec3{v.v[0] / flat * v.v[3], 0.0F, v.v[2] / flat * v.v[3]} : Vec3{};
        x->velocity = x->jump.along;
        x->move_clock = 0.0F;
        x->steered = x->jump.along;
        x->set_u32(0x108, a[3].bits);
        x->jump.keep = x->jump.slow = a[3].f(); // class 9's steering in the air (FUN_1403cbdd0)
        x->set_bits(field::kFlags, bit::kInAir, true);
        x->play(std::int32_t(a[4].bits), a[5].f(), 0.0F);
        x->motion.after = -1; // FUN_1403cbd00: + 0x118 -1, + 0x11c 5, the move's way + 0x648 2
        x->motion.landing = 5;
        if (a[6].bits != 0) {
            x->motion.after = std::int32_t(a[4].bits) + 1;
            x->motion.landing = std::int32_t(a[4].bits) + 2;
        }
    });
    // 1:129 a throw of the actor's own (FUN_14042b550 -> FUN_1403d3e50): a vector (its NaNs made 0) -- its x, z the way
    // along the ground (made one long, then times its w), its y the speed up (+ 0x24 -y) -- and class 4, thrown
    // (FUN_140409d30; the classes at 0x140750d30): no steering in the air (FUN_140422780 keeps the speed along), on the
    // ground again the motion it was playing then, plus one (FUN_140422700: + 0x114 the motion at the throw) -- the
    // Rapid Thruster's rise in motion 18, landing in 19. KH2's slowing of the rise and its fastest fall are values the
    // game sets running (DAT_142ae5760 + 0x18 .. + 0x20, + 0x138): the port's pull is the jumps' own
    calls.set(1, 129, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        Vec v = read_vec(m, a[1].bits);
        for (float &c : v.v)
            if (std::isnan(c))
                c = 0.0F;
        const float flat = std::sqrt(v.v[0] * v.v[0] + v.v[2] * v.v[2]);
        x->jump = Jump{};
        x->jump.on = true;
        x->jump.keep = x->jump.slow = 1.0F;
        x->jump.up = -v.v[1];
        x->jump.along = flat > 0.0F ? Vec3{v.v[0] / flat * v.v[3], 0.0F, v.v[2] / flat * v.v[3]} : Vec3{};
        x->dashing = false;
        x->velocity = x->steered = x->jump.along;
        x->move_clock = 0.0F;
        x->set_bits(field::kFlags, bit::kInAir, true);
        x->motion.after = -1;
        x->motion.landing = x->motion.id >= 0 ? x->motion.id + 1 : 5;
    });
    // 2:6 whether an object is in one of its reaction motions, 10 to 39 (FUN_140431d50 -> FUN_1403d4880: a motion
    // controller at + 0x158, + 0x180 less 10 under 30): Crimson Jazz asks it of itself before it acts
    calls.set(2, 6, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        Actor *x = o != nullptr ? o->actor() : nullptr;
        const bool reacting = x != nullptr && x->motions != nullptr && x->u32(field::kMotion) - 10u < 30u;
        a[0] = ai::Value::of_int(reacting ? 1 : 0);
    });
    // 1:100 the speed a jump needs to rise `h` under KH2's pull: sqrt(|2 h / 0.408163|) (FUN_1403b73f0)
    calls.set(1, 100, [](ai::Machine &, std::span<ai::Value> a) {
        a[0] = ai::Value::of_float(std::sqrt(std::fabs(2.0F * a[0].f() / 0.408163F)));
    });
    // 1:63 whether its motion has reached its end with no blend left (FUN_14042f120 -> FUN_1403c7e70 on + 0x158:
    // its time + 0x44 against its motion's length, its blend + 0x48 run out)
    calls.set(1, 63, [](ai::Machine &m, std::span<ai::Value> a) {
        const Actor *x = self_of(m, a[0]);
        const bool at_end = x->motions != nullptr && x->motion.id >= 0 && x->motions->has(x->motion.id) &&
                            x->motion.blend_left <= 0.0F && x->motion.time >= x->motions->length(x->motion.id);
        a[0] = ai::Value::of_int(at_end ? 1 : 0);
    });
    // 1:19, 1:20 a fade to gone / to seen over a time, unless held (FUN_1403b7810 on + 0xa08: a time of 2 or less is
    // 1); 1:28 gone in a tick and held (+ 0x9b8 bit 4)
    const auto fade = [](Actor &x, float from, float to, float time) {
        if ((x.u32(field::kFade) & bit::kFadeHeld) != 0)
            return;
        if (time <= 2.0F)
            time = 1.0F;
        x.fade = from;
        x.fade_step = (to - from) / time;
    };
    calls.set(1, 19, [fade](ai::Machine &m, std::span<ai::Value> a) { fade(*self_of(m, a[0]), 1.0F, 0.0F, a[1].f()); });
    calls.set(1, 20, [fade](ai::Machine &m, std::span<ai::Value> a) { fade(*self_of(m, a[0]), 0.0F, 1.0F, a[1].f()); });
    calls.set(1, 28, [fade](ai::Machine &m, std::span<ai::Value> a) {
        Actor &x = *self_of(m, a[0]);
        fade(x, 1.0F, 0.0F, 0.0F);
        x.set_bits(field::kFade, bit::kFadeHeld, true);
    });
}

// ------------------------------------------------------------------------------------------------- moving and facing

void add_moving(ai::Calls &calls)
{
    // 1:2 steered toward a point along the ground at a speed (FUN_1403b9c60: the way there, its height 0, made one
    // long at + 0x10, the speed at + 0x1c), the way's length given back; its first value the actor itself
    // 1:148 the actor put at a point (FUN_1403b64d0: + 0x670, its w 1); 1:220 a step ahead up to a length, the room's
    // collision stopping it (FUN_1403b9ac0: Days's collision puts it back out of the walls after)
    calls.set(1, 148, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v = read_vec(m, a[1].bits);
        for (float &c : v.v)
            if (std::isnan(c))
                c = 0.0F;
        if (Object *o = object_of(m, a[0])) {
            o->set_place(xyz(v));
            if (is_player(o) && running(m)->world != nullptr)
                running(m)->world->player_place(*o, xyz(v));
        }
    });
    calls.set(1, 220, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec3 at = x->place(), f = facing_of(x->yaw());
        x->set_place({at[0] + f[0] * a[1].f(), at[1], at[2] + f[2] * a[1].f()});
    });
    calls.set(1, 2, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec to = read_vec(m, a[1].bits);
        const Vec3 at = x->place();
        const float dx = to.v[0] - at[0], dz = to.v[2] - at[2], n = std::sqrt(dx * dx + dz * dz);
        x->steer = n > 0.0F ? Vec3{dx / n, 0.0F, dz / n} : Vec3{};
        x->steer_speed = a[2].f();
        a[0] = ai::Value::of_float(n);
    });
    // 1:58 along the ground at a velocity of its own (FUN_140421ce0: + 0x20 the vector made one long times its w,
    // + 0x108 the value, class 5), NaN components 0
    calls.set(1, 58, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        Vec v = read_vec(m, a[1].bits);
        for (float &c : v.v)
            if (std::isnan(c))
                c = 0.0F;
        const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        x->velocity = n > 0.0F ? Vec3{v.v[0] / n * v.v[3], v.v[1] / n * v.v[3], v.v[2] / n * v.v[3]} : Vec3{};
        x->steered = x->velocity;
        x->move_clock = 0.0F;
        x->dash_keep = a[2].f();
        x->dashing = true;
        x->jump.on = false;
    });
    // 1:83 a point given on a bone (0x4000 the actor itself) in the world (FUN_1403b50f0 the bone's matrix, then the
    // point through it; NaN components 0): the shared vector
    calls.set(1, 83, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        Vec v = read_vec(m, a[2].bits);
        for (float &c : v.v)
            if (std::isnan(c))
                c = 0.0F;
        Vec3 out = x->place();
        if (x->world == nullptr || !x->world->bone_point(*x, std::int32_t(a[1].bits), xyz(v), out))
            out = x->place();
        Vec r;
        r.v[0] = out[0], r.v[1] = out[1], r.v[2] = out[2], r.v[3] = 1.0F;
        a[0] = give_vec(m, "1:83", r);
    });
    // 1:98 its place on the ground under it (FUN_1403b5b40, its y FUN_1403b87e0's); 1:99 how far above it it is,
    // KH2's way (negative up; 0 on it or below)
    calls.set(1, 98, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec3 at = x->place();
        Vec r;
        r.v[0] = at[0], r.v[1] = x->world != nullptr ? x->world->ground(*x) : at[1], r.v[2] = at[2], r.v[3] = 1.0F;
        a[0] = give_vec(m, "1:98", r);
    });
    calls.set(1, 99, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const float above = (x->world != nullptr ? x->world->ground(*x) : x->place()[1]) - x->place()[1];
        a[0] = ai::Value::of_float(above <= 0.0F ? -0.0F : -above);
    });
    // 1:4 face a direction (FUN_1403b65c0: yaw = atan2(x, z)); 1:79 the same, its facing too (FUN_1403b6420)
    const auto turn = [](ai::Machine &m, Object *o, float yaw) {
        o->set_yaw(yaw);
        if (is_player(o) && running(m)->world != nullptr)
            running(m)->world->player_yaw(*o, yaw);
    };
    const auto face = [turn](ai::Machine &m, std::span<ai::Value> a) {
        const Vec v = read_vec(m, a[1].bits);
        if (v.v[0] != 0.0F || v.v[2] != 0.0F)
            turn(m, object_of(m, a[0]), std::atan2(v.v[0], v.v[2]));
    };
    calls.set(1, 4, face);
    calls.set(1, 79, face);
    // 1:1 turned to a yaw (FUN_14042bf60 -> FUN_1403b6560)
    calls.set(1, 1, [turn](ai::Machine &m, std::span<ai::Value> a) { turn(m, object_of(m, a[0]), a[1].f()); });
    // 1:80 turned toward a direction by a step at most (FUN_1403b9f70: the step times DAT_1405b18ec, 2.0; 0 the
    // actor's own turn speed, + 0x134): 1 while it still turns, 0 once it faces it
    calls.set(1, 80, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        float step = a[2].f();
        if (step == 0.0F)
            step = x->f(0x134);
        step *= 2.0F;
        const float want = std::atan2(v.v[0], v.v[2]), now = x->yaw();
        const float d = wrapped(want - now);
        int turning = 1;
        float yaw = want;
        if (d > step)
            yaw = wrapped(now + step);
        else if (d < -step)
            yaw = wrapped(now - step);
        else
            turning = 0;
        x->set_yaw(yaw);
        a[0] = ai::Value::of_int(turning);
    });
    // 1:89 a move this tick: a direction made unit and a speed (FUN_1403b9a70: + 0xa58, + 0xa64, cleared after the
    // tick); 1:111 its steering, kept (FUN_1403b9da0: + 0x10, + 0x1c; Actor::steer)
    calls.set(1, 89, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        Actor *x = self_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        if (n == 0.0F)
            return;
        if (is_player(o)) { // one of Days's players: the world moves it
            if (running(m)->world != nullptr)
                running(m)->world->player_move(*o, {v.v[0] / n, v.v[1] / n, v.v[2] / n}, a[2].f());
            return;
        }
        x->move = {v.v[0] / n, v.v[1] / n, v.v[2] / n};
        x->move_speed = a[2].f();
    });
    calls.set(1, 111, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        Actor *x = self_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        if (is_player(o)) { // one of Days's players: the world steers it (the Armored Knight's Rising Sun flies him)
            if (running(m)->world != nullptr)
                running(m)->world->player_steer(*o, n != 0.0F ? Vec3{v.v[0] / n, v.v[1] / n, v.v[2] / n} : Vec3{},
                                                a[2].f());
            return;
        }
        x->steer = n != 0.0F ? Vec3{v.v[0] / n, v.v[1] / n, v.v[2] / n} : Vec3{v.v[0], v.v[1], v.v[2]};
        x->steer_speed = a[2].f();
    });
    // 1:119 a way toward a point at a height (FUN_1403b7f10): 0, the way clear -- Days's rooms have no KH2 paths;
    // the room's own collision keeps the actor out of the walls
    calls.set(1, 119, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:92 a point of the ground ahead (FUN_1403b6170: a ray, then FUN_1403dc170 with the number): nothing of Days's
    calls.set(1, 92, [](ai::Machine &, std::span<ai::Value>) {});
}

// ------------------------------------------------------------------------------------------------- targets, effects

// KH2's search for an aim (FUN_14053fb60 makes one, FUN_140540080 lists an object's aims in it, FUN_14053fb80 looks):
// its capacity n (+ 0), a byte the list's next place (+ 4) and one the aims looked at (+ 5), the list's length (+ 8),
// which actors (+ 0xc: 0 those of + 0x6c8 bit 4, 1 and 2 bit 2: KH2's enemies), the reach from (+ 0x10) to (+ 0x14),
// a cone's half (+ 0x18: pi none), then n listed aims, an object and its group each (+ 0x1c, + 0x20); a record found is
// KH2's (FUN_1403bee60: the object, its group, none, -1)
constexpr std::uint32_t kSearchList = 0x1c;

bool listed(ai::Memory &mem, std::uint32_t s, std::uint32_t object, int group)
{
    const std::int32_t n = std::int32_t(mem.u32(s + 8));
    for (std::int32_t k = 0; k < n; ++k)
        if (mem.u32(s + kSearchList + 8 * std::uint32_t(k)) == object &&
            std::int32_t(mem.u32(s + kSearchList + 8 * std::uint32_t(k) + 4)) == group)
            return true;
    return false;
}

void list_aim(ai::Memory &mem, std::uint32_t s, std::uint32_t object, int group)
{
    const std::int32_t cap = std::int32_t(mem.u32(s));
    if (cap <= 0 || listed(mem, s, object, group))
        return;
    std::uint32_t head = mem.u32(s + 4);
    const std::uint32_t at = head & 0xffu;
    mem.set_u32(s + kSearchList + 8 * at, object);
    mem.set_u32(s + kSearchList + 8 * at + 4, std::uint32_t(group));
    if (std::int32_t(mem.u32(s + 8)) < cap)
        mem.set_u32(s + 8, mem.u32(s + 8) + 1);
    head = (head & ~0xffu) | ((at + 1) % std::uint32_t(cap));
    mem.set_u32(s + 4, head);
}

float f32_at(ai::Memory &mem, std::uint32_t at)
{
    float f = 0.0F;
    mem.read(at, &f, 4);
    return f;
}

void add_search(ai::Calls &calls)
{
    // 6:3 a search made: its capacity, nothing listed, its reach 0 to 1000, no cone (FUN_14053dce0 -> FUN_14053fb60)
    calls.set(6, 3, [](ai::Machine &m, std::span<ai::Value> a) {
        ai::Memory &mem = m.memory();
        const std::uint32_t s = a[0].bits;
        const float far = 1000.0F, half = kPi;
        mem.set_u32(s, a[1].bits);
        for (std::uint32_t o = 4; o <= 0x10; o += 4)
            mem.set_u32(s + o, 0);
        mem.write(s + 0x14, &far, 4);
        mem.write(s + 0x18, &half, 4);
    });
    // 6:8 an object's aims (its shapes of kind 3 in its groups on) listed in a search, those it had left (FUN_14053e240
    // -> FUN_140540080): the Armored Knight lists its own
    calls.set(6, 8, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[1]);
        Actor *x = o != nullptr ? o->actor() : nullptr;
        if (x == nullptr || x->world == nullptr || (x->u32(field::kFlags) & 0x10080000u) != 0)
            return;
        for (const auto &sh : x->world->shapes(*x, 3))
            list_aim(m.memory(), a[0].bits, x->address(), sh.group);
    });
    // 6:5 the nearest aim (or the farthest) of KH2's actors from a point within the search's reach and cone, into a
    // record (FUN_14053dd20 -> FUN_14053fb80): along the ground only when asked; with the last flag those listed are
    // passed over and the one found is listed
    calls.set(6, 5, [](ai::Machine &m, std::span<ai::Value> a) {
        ai::Memory &mem = m.memory();
        const std::uint32_t s = a[0].bits, out = a[1].bits;
        const Vec from = read_vec(m, a[2].bits), way = read_vec(m, a[3].bits);
        const bool nearest = a[4].bits != 0, flat = a[5].bits != 0, keep = a[6].bits != 0;
        mem.set_u32(out, 0);
        mem.set_u32(out + 4, 0);
        mem.set_u32(out + 8, 0);
        mem.set_u32(out + 12, 0xffffffffu);
        Actor *me = running(m);
        if (me->world == nullptr)
            return;
        const float lo = f32_at(mem, s + 0x10), hi = f32_at(mem, s + 0x14), cone = f32_at(mem, s + 0x18);
        float best = 0.0F;
        Actor *found = nullptr;
        int found_group = 0;
        std::uint32_t looked = 0;
        for (Actor *x : me->world->actors())
            for (const auto &sh : me->world->shapes(*x, 3)) {
                ++looked;
                if (keep && listed(mem, s, x->address(), sh.group))
                    continue;
                Vec3 d = {sh.at[0] - from.v[0], flat ? 0.0F : sh.at[1] - from.v[1], sh.at[2] - from.v[2]};
                const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (dist < lo || dist > hi)
                    continue;
                if (cone < kPi && std::fabs(wrapped(std::atan2(way.v[0], way.v[2]) - std::atan2(d[0], d[2]))) >= cone)
                    continue;
                const float score = nearest ? dist : -dist;
                if (found == nullptr || score < best)
                    found = x, found_group = sh.group, best = score;
            }
        mem.set_u32(s + 4, (mem.u32(s + 4) & ~0xff00u) | (std::min(looked, 0xffu) << 8));
        if (found == nullptr)
            return;
        mem.set_u32(out, found->address());
        mem.set_u32(out + 4, std::uint32_t(found_group));
        if (keep)
            list_aim(mem, s, found->address(), found_group);
    });
}

// KH2's attacks of their own (YS::ATTACK_MAGIC; the world keeps them: World::attack_made): the calls that make, size,
// place, grow, attach, hit with and end one
void add_attacks(ai::Calls &calls)
{
    // 2:10 one made (FUN_140432250 -> FUN_1404371b0, FUN_1403cf830): for the object an AI handle names (its owner),
    // an atkp entry (FUN_1403eafc0 looks it up with the sub-number), a combo group (-1 a running number), the
    // sub-number; its address given back
    calls.set(2, 10, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = running(m);
        const std::uint32_t at = m.memory().add(0x138, x->name() + "'s attack (2:10)");
        if (x->world != nullptr)
            x->world->attack_made(at, *x, object_of(m, a[0]), std::int32_t(a[1].bits), std::int32_t(a[2].bits),
                                  std::int32_t(a[3].bits));
        a[0] = ai::Value::of_address(at);
    });
    const auto world = [](ai::Machine &m) { return running(m)->world; };
    // 2:11 its size: a radius, a height (0 or less: the radius) (FUN_1404322f0 -> FUN_140437c00, + 0x104, + 0x108)
    calls.set(2, 11, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->attack_size(a[0].bits, a[1].f(), a[2].f() > 0.0F ? a[2].f() : a[1].f());
    });
    // 2:12 its place (FUN_140432330 -> FUN_140437a80: + 0xe4, the one before kept at + 0xf4 -- swept between)
    calls.set(2, 12, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->attack_place(a[0].bits, xyz(read_vec(m, a[1].bits)));
    });
    // 2:13 ended (FUN_1404323b0 -> FUN_1403d0eb0: + 8's 0x800)
    calls.set(2, 13, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->attack_end(a[0].bits);
    });
    // 2:17 a hit made with it on an object at once (FUN_1404328e0 -> FUN_1403d23c0, the object's class's + 0xc0): the
    // Neoshadow's strike, KH2's player's attack on itself
    calls.set(2, 17, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->attack_strike(a[0].bits, object_of(m, a[1]));
    });
    // 2:22 whose effects its hits show (FUN_140432a90: + 0x28 an object's + 0x80): the port shows Days's
    calls.set(2, 22, [](ai::Machine &, std::span<ai::Value>) {});
    // 2:37 its team (FUN_140432540 -> FUN_1403d2390: + 0x38, 3 made 4; + 0x39 every team but it and 0 its targets)
    calls.set(2, 37, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->attack_team(a[0].bits, std::int32_t(a[1].bits));
    });
    // 2:61 the effect it follows (FUN_140432840 -> FUN_140437a00, + 0xc8: its place the effect's each tick, ended with
    // it -- the Hammer Frame's shockwave on its effect 8)
    calls.set(2, 61, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->attack_follow(a[0].bits, a[1].bits);
    });
    // 2:62 its growth (FUN_140432890 -> FUN_140437c30: over n ticks, + 0xd0, from a radius and a height -- 0 or less
    // its own -- + 0xd4, + 0xd8, to its size, eased by sin(t / n x pi / 2); ended once grown)
    calls.set(2, 62, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->attack_grow(a[0].bits, float(std::int32_t(a[1].bits)), a[2].f(), a[3].f());
    });
}

void add_world(ai::Calls &calls)
{
    // 1:3 the player's actor (FUN_1403b3e80, DAT_142a105d0): the Days player this actor goes for
    calls.set(1, 3, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = running(m);
        Object *p = x->world != nullptr ? x->world->player(*x) : nullptr;
        a[0] = ai::Value::of_address(p != nullptr ? p->address() : 0);
    });
    // 2:88 the last to hit it (+ 0xd38, FUN_140431e60), the player's actor when none is (or it no longer acts)
    calls.set(2, 88, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        std::uint32_t who = x->u32(field::kAttacker);
        if (who == 0 || Object::at(m.memory(), who) == nullptr) {
            Object *p = x->world != nullptr ? x->world->player(*x) : nullptr;
            who = p != nullptr ? p->address() : 0;
        }
        a[0] = ai::Value::of_address(who);
    });
    // 2:23 the actor's target slot (+ 0xbf8), made "itself" (2) while it is empty (FUN_1404319d0)
    calls.set(2, 23, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const std::uint32_t slot = slot_of(*x);
        if (m.memory().u32(slot + kSlotSet) == 0) {
            m.memory().set_u32(slot + kSlotMode, 2);
            m.memory().set_u32(slot + kSlotOwner, x->address());
            m.memory().set_u32(slot + kSlotSet, 1);
        }
        a[0] = ai::Value::of_address(slot);
    });
    // 1:121 a slot's target chosen for an owner by a mode (FUN_1403be2b0: 2 the owner itself, else the player)
    calls.set(1, 121, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *owner = object_of(m, a[1]);
        m.memory().set_u32(a[0].bits + kSlotMode, a[2].bits);
        m.memory().set_u32(a[0].bits + kSlotOwner, owner != nullptr ? owner->address() : 0);
        m.memory().set_u32(a[0].bits + kSlotSet, 1);
    });
    // 1:120 a slot's target's place (FUN_1403bdde0, FUN_1403be0f0), the shared vector; 1:131 the slot as KH2's pool
    // keeps it (FUN_1403bda80): the same slot
    calls.set(1, 120, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        if (Object *t = target_in(m, a[0].bits))
            for (int i = 0; i < 4; ++i)
                v.v[i] = t->f(field::kPlace + 4 * i);
        a[0] = give_vec(m, "shared", v);
    });
    calls.set(1, 131, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:132 a record given back to that pool (FUN_140430420): the port keeps none
    calls.set(1, 132, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:191 the record at an object's + 0xdc8's + 4 (FUN_14042b9c0): the player's aim (his lock-on), which the Armored
    // Knight's Rising Sun goes for when it names one -- empty in Days's field (no lock of Days's is passed on): a record
    // of the port's for each object, made on first use
    calls.set(1, 191, [](ai::Machine &m, std::span<ai::Value> a) {
        static std::map<std::pair<const ai::Memory *, std::uint32_t>, std::uint32_t> records;
        Object *o = object_of(m, a[0]);
        if (o == nullptr) {
            a[0] = ai::Value::of_address(0);
            return;
        }
        auto &at = records[{&m.memory(), o->address()}];
        if (at == 0) {
            at = m.memory().add(16, o->name() + "'s aim record (+ 0xdc8's + 4)");
            m.memory().set_u32(at + 12, 0xffffffffu); // FUN_1403bda50's empty record
        }
        a[0] = ai::Value::of_address(at);
    });
    // 1:221 whether the player can take a command now (FUN_14042bbf0 -> FUN_1403b6950, FUN_1403ba070: his state
    // against the command's flags): yes -- Days's players take what is offered
    calls.set(1, 221, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(1); });
    // 1:165 a reaction command offered KH2's player this tick (FUN_140430010 -> FUN_1403b2850 on his + 0xdd0): its slot,
    // the command, its source (an AI handle: the press goes to it), a value of its record
    calls.set(1, 165, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = running(m);
        if (x->world != nullptr)
            x->world->offer(*x, std::int32_t(a[1].bits), object_of(m, a[2]));
    });
    // 1:149 an effect on the actor at another object (FUN_14042d5c0 -> FUN_1403b6670: its element's number, two values,
    // the object it is on), its handle; 0:44 one stopped at once (FUN_14041e190 -> FUN_1402be8e0)
    calls.set(1, 149, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        Object *on = object_of(m, a[2]);
        const std::uint32_t h =
            x->world != nullptr
                ? x->world->effect_on(*x, std::int32_t(a[1].bits), std::int32_t(a[3].bits), std::int32_t(a[4].bits), on)
                : 0;
        a[0] = ai::Value::of_address(h);
    });
    calls.set(0, 44, [](ai::Machine &m, std::span<ai::Value> a) {
        if (running(m)->world != nullptr && a[0].bits != 0)
            running(m)->world->stop_effect(a[0].bits);
    });
    add_search(calls);
    add_attacks(calls);
    // 1:87 an effect on the actor (FUN_1403b6670 -> FUN_1402c6b30 on + 0x80): its element's number, two values more
    calls.set(1, 87, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const std::uint32_t h = x->world != nullptr ? x->world->effect(*x, std::int32_t(a[1].bits),
                                                                       std::int32_t(a[2].bits), std::int32_t(a[3].bits))
                                                    : 0;
        a[0] = ai::Value::of_address(h);
    });
    // 1:128 the object's class's virtual + 0x20 with a value (its native code; trigger 13 calls it with 0); 2:9 the
    // actor's + 0x5c0 made from a number (FUN_140411350): neither is the script's own behaviour, nothing here
    calls.set(1, 128, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:209 the class's virtual + 0xb0 unless + 0x9b8 bit 2 (FUN_1403d3f10); 1:74 the class made class 0 (the old
    // one's virtual + 0x10, the new one's + 8; FUN_1403b4da0) and + 0xba8 cleared: native code, not the script's
    calls.set(1, 209, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(1, 74, [](ai::Machine &m, std::span<ai::Value> a) {
        if (is_player(object_of(m, a[0])))
            return; // a Days player's class is Days's
        Actor *x = self_of(m, a[0]);
        x->set_u32(0xba8, 0);
        x->dashing = false; // class 0
    });
    // 1:18 an actor put on another object, which then carries it (FUN_14042c550 -> FUN_1403d9fe0): the first's actor
    // on the second's, at a bone (0x4000 none: its place) with flags (bit 1 keeps its facing); its fifth and sixth a
    // routine to run when it is taken off (none in the scripts read: 0). 1:102 taken off (FUN_1403da250: put where
    // it was carried, its facing its own again)
    calls.set(1, 18, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *carried = self_of(m, a[0]);
        Object *by = object_of(m, a[1]);
        if (by == nullptr || by == carried)
            return;
        carried->set_u32(field::kCarrier, by->address());
        if (carried->world != nullptr)
            carried->world->carry(*carried, *by, std::int32_t(a[2].bits & 0xffff), std::int32_t(a[3].bits & 0xffff));
    });
    calls.set(1, 102, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *carried = self_of(m, a[0]);
        if (carried->u32(field::kCarrier) == 0)
            return;
        carried->set_u32(field::kCarrier, 0);
        if (carried->world != nullptr)
            carried->world->uncarry(*carried);
    });
    // 1:278 n things made at the actor (FUN_14042a980: n times FUN_1403dc950 on DAT_142af8b48 with DAT_1405d5cc0):
    // **likely its prizes** -- Days gives its stand-in's own
    calls.set(1, 278, [](ai::Machine &, std::span<ai::Value>) {});
    // 2:9 its health's record made from a battle entry (FUN_140411350 -> FUN_1403c0280: + 0x5c0; 12 bytes a value,
    // the first its health now and at most -- what 1:84, 1:228 and 1:229 read, the scripts' "hp_rate"; + 0x268 the
    // actor): Days's health in it, kept by the shell each frame (World::health)
    calls.set(2, 9, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        if (x->u32(0x5c0) == 0) {
            const std::uint32_t record = m.memory().add(0x270, x->name() + "'s health");
            m.memory().set_u32(record + 0x268, x->address());
            x->set_u32(0x5c0, record);
        }
        if (x->world != nullptr)
            x->world->health(*x);
    });
    // 2:20 a routine of the script started as a thread of its own (FUN_1403c5ec0), with one value; a handle back
    calls.set(2, 20, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint32_t entry = a[0].bits;
        if (a[0].tag == ai::Tag::Address && entry >= m.code())
            entry = (entry - m.code()) / 2;
        const ai::Value arg = a[1];
        m.start_routine(nullptr, entry, {&arg, 1});
        static std::map<const ai::Memory *, std::uint32_t> handles;
        auto &h = handles[&m.memory()];
        if (h == 0)
            h = m.memory().add(16, "2:20's threads");
        a[0] = ai::Value::of_address(h);
    });
}

} // namespace khdays::kh2_enemies
