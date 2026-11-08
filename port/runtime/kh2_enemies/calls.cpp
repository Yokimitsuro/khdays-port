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
}

// ------------------------------------------------------------------------------------------------- states

void add_states(ai::Calls &calls)
{
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
    // 1:57 the object's class's byte + 0x0c bit 0 (a kind that moves by its speed, not its motions); 1:124 its entry's
    // flag (FUN_1403ba700); 1:56 FUN_1403ba4a0's value; 1:110 its battle record's byte + 0x1c (FUN_1403b50d0: -1
    // without one): none of these is Days's, so the plain answers -- 0, and -1 for no record
    calls.set(1, 57, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    calls.set(1, 124, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    calls.set(1, 56, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
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
    // 2:8 whether an object's kind (+ 0x26) is in the groups past 1 (FUN_1403ce980): trigger 6's sender, never one
    // of Days's
    calls.set(2, 8, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // the world's flags: 1:23 DAT_140717008 (the script tests 10), 1:34 DAT_142a11478 (FUN_1403ac220), 1:301
    // FUN_1403bbea0, 1:171 / 1:206 DAT_142a11400's bits 2 / 7 -- KH2's field busy (an event, a pause), which Days's
    // field never is for its enemies: 0
    for (const unsigned n : {23u, 34u, 301u, 171u, 206u})
        calls.set(1, n, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
}

// ------------------------------------------------------------------------------------------------- motions

void add_motions(ai::Calls &calls)
{
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
    // 1:55, 1:168 a jump (FUN_1403cbb00, FUN_1404233f0): a vector (its x, z the way, its y the height -- negative up
    // -- its w the speed along), then a motion; the flag sets the two after it (+ 0x118 its fall, + 0x11c its landing)
    const auto jump = [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        x->jump.on = true;
        x->jump.up = v.v[1] < 0.0F ? -std::sqrt(-v.v[1] * 2.0F / 0.408163F) : 0.0F;
        const float speed = v.v[3] != 0.0F ? v.v[3] : 1.0F;
        x->jump.along = {v.v[0] * speed, 0.0F, v.v[2] * speed};
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
    // 1:4 face a direction (FUN_1403b65c0: yaw = atan2(x, z)); 1:79 the same, its facing too (FUN_1403b6420)
    const auto face = [](ai::Machine &m, std::span<ai::Value> a) {
        const Vec v = read_vec(m, a[1].bits);
        if (v.v[0] != 0.0F || v.v[2] != 0.0F)
            object_of(m, a[0])->set_yaw(std::atan2(v.v[0], v.v[2]));
    };
    calls.set(1, 4, face);
    calls.set(1, 79, face);
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
    // 1:89 a move this tick: a direction made unit and a speed (FUN_1403b9a70: + 0xa58, + 0xa64); 1:111 the same way
    // asked through + 0x10 / + 0x1c (FUN_1403b9da0), read as a move as well
    const auto move = [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        if (n == 0.0F)
            return;
        x->move = {v.v[0] / n, v.v[1] / n, v.v[2] / n};
        x->move_speed = a[2].f();
    };
    calls.set(1, 89, move);
    calls.set(1, 111, move);
    // 1:119 a way toward a point at a height (FUN_1403b7f10): 0, the way clear -- Days's rooms have no KH2 paths;
    // the room's own collision keeps the actor out of the walls
    calls.set(1, 119, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:92 a point of the ground ahead (FUN_1403b6170: a ray, then FUN_1403dc170 with the number): nothing of Days's
    calls.set(1, 92, [](ai::Machine &, std::span<ai::Value>) {});
}

// ------------------------------------------------------------------------------------------------- targets, effects

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
    calls.set(2, 9, [](ai::Machine &, std::span<ai::Value>) {});
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
