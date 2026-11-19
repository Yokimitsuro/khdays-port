// The port's answers to KINGDOM HEARTS II's calls (calls.h): each names the function of KH2FM.exe it follows. A
// call's arguments are in the order the script pushed them; "the actor" is the object an AI handle's + 4 names.
#include "calls.h"

#include "actor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
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

// The segments BGHITs were cast along (6:51), by their records: 6:52 casts beside them for the surface's normal
std::map<std::uint32_t, std::pair<Vec3, Vec3>> &bghit_segments()
{
    static std::map<std::uint32_t, std::pair<Vec3, Vec3>> segments;
    return segments;
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

// A target slot (actor + 0xbf8; FUN_1403be2b0 fills one): KH2's YS::TARGET, 20 bytes in the scripts' memory (often
// in a script's own work, Final Xemnas's &wp + 0x430) -- + 0 the target object (Obj), + 4 its group, + 8 its attack,
// + 0xc how it is chosen (Type), + 0x10 its list's link: scripts read the object off its + 0 (Final Xemnas's grab asks
// 1:309 of it; until 2026-11-16 the port kept the mode there, and his grab never took the player). Its owner, which
// the choice starts from, kept aside (slot_owners). KH2's modes: 2 the player (DAT_142a105d0, what 1:114 finds for
// 0x236); 0 and 1 the player's lock or a friend's target, else the player; 3 to 6 searches from the owner
// (FUN_1403be7f0, FUN_1403bea30, FUN_1403be4b0), not read: the player for each -- an enemy's target is Days's player.
// (Mode 2 was read as the owner itself: Demyx, whose target is 2, then stood at no distance from it and never summoned
// his water clones.)
constexpr std::uint32_t kSlotObject = 0, kSlotType = 0xc, kSlotSize = 20;

std::map<std::pair<const ai::Memory *, std::uint32_t>, std::uint32_t> &slot_owners()
{
    static std::map<std::pair<const ai::Memory *, std::uint32_t>, std::uint32_t> owners;
    return owners;
}

std::uint32_t slot_of(Actor &a)
{
    if (a.target_slot == 0)
        a.target_slot = a.memory().add(kSlotSize, a.name() + "'s target slot");
    return a.target_slot;
}

// the slot's target now (its owner's player), put at its + 0 as KH2 keeps it; none when the slot was never chosen
Object *target_in(ai::Machine &m, std::uint32_t slot)
{
    ai::Memory &mem = m.memory();
    const auto it = slot_owners().find({&mem, slot});
    if (it == slot_owners().end())
        return nullptr;
    Object *o = Object::at(mem, it->second);
    Actor *a = o != nullptr ? o->actor() : nullptr;
    Object *t = a == nullptr || a->world == nullptr ? o : a->world->player(*a);
    mem.set_u32(slot + kSlotObject, t != nullptr ? t->address() : 0u);
    return t;
}

// FUN_1403be2b0: a slot's target chosen for an owner by a mode
void choose_target(ai::Machine &m, std::uint32_t slot, Object *owner, std::uint32_t mode)
{
    m.memory().set_u32(slot + kSlotType, mode);
    slot_owners()[{&m.memory(), slot}] = owner != nullptr ? owner->address() : 0u;
    target_in(m, slot);
}

// 2:21's blocks by their memory and number
std::map<std::pair<const ai::Memory *, std::uint32_t>, std::uint32_t> &teamwork()
{
    static std::map<std::pair<const ai::Memory *, std::uint32_t>, std::uint32_t> blocks;
    return blocks;
}

} // namespace

void seed_chance(std::uint32_t seed)
{
    chance_state = seed != 0 ? seed : 1;
}

std::uint32_t kh2_chance()
{
    return next_chance();
}

void forget_teamwork()
{
    teamwork().clear();
    slot_owners().clear();
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
    // 0:7 made unit in place, its length given back and kept as its w (FUN_14041cfa0 -> FUN_140142080: left as it is
    // when 0; + 0xc the length)
    // 0:8 a vector printed (trap_vector_dump; FUN_14041d1a0 is empty in the PC's build): nothing
    calls.set(0, 8, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(0, 7, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        m.memory().read(a[0].bits, v.v, sizeof v.v);
        const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        if (n != 0.0F) {
            const float k = 1.0F / n;
            for (int i = 0; i < 3; ++i)
                v.v[i] *= k;
        }
        v.v[3] = n;
        m.memory().write(a[0].bits, v.v, sizeof v.v);
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
    // told where the memory tells its misses (the shell's trace: "avoid start", "high_attack_min" ...)
    calls.set(0, 0, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(0, 1, [](ai::Machine &, std::span<ai::Value>) {}); // FUN_14041ca00: empty too
    calls.set(0, 2, [](ai::Machine &m, std::span<ai::Value> a) {
        if (m.memory().report)
            m.memory().report("its script prints \"" + name_at(m.memory(), a[0].bits) + "\"");
    });
    // 0:31 the screen faded to white over n frames, 0:32 back from white (func_screen_whiteout, _whitein in the PS3
    // build: FUN_14041dd70, FUN_14041ddb0 -> FUN_140156ce0, FUN_140156c90 on FUN_140156dc0's fader, white), 0:88 back
    // from black (trap_screen_fadein, FUN_14041de70; KH2's hit asks 0:32 for 10 frames): Days's screen fades are its
    // own, so none is shown (Jafar's whiteout)
    calls.set(0, 31, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(0, 32, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(0, 88, [](ai::Machine &, std::span<ai::Value>) {});
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
    // 1:231 / 1:232 a record's third word, the health it does not go under (YS::SHEET's HP: Hp, MaxHp, MinHp;
    // trap_sheet_set_min_hp, trap_sheet_min_hp: FUN_140431190, FUN_1404310b0) -- the shell keeps Days's hits above it;
    // 1:233 its health set, kept between the two (trap_sheet_set_hp; FUN_140431140), and given to Days's enemy
    calls.set(1, 231, [](ai::Machine &m, std::span<ai::Value> a) {
        m.memory().set_u32(a[0].bits + 12u * a[2].bits + 8u, a[1].bits);
    });
    calls.set(1, 232, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_int(std::int32_t(m.memory().u32(a[0].bits + 12u * a[1].bits + 8u)));
    });
    calls.set(1, 233, [](ai::Machine &m, std::span<ai::Value> a) {
        const std::uint32_t at = a[0].bits + 12u * a[2].bits;
        const std::int32_t least = std::int32_t(m.memory().u32(at + 8)), most = std::int32_t(m.memory().u32(at + 4));
        const std::int32_t hp = std::max(least, std::min(std::int32_t(a[1].bits), most));
        m.memory().set_u32(at, std::uint32_t(hp));
        const std::uint32_t owner = m.memory().u32(a[0].bits + 0x268);
        Object *o = a[2].bits == 0 ? Object::at(m.memory(), owner) : nullptr;
        if (Actor *x = o != nullptr ? o->actor() : nullptr; x != nullptr && x->world != nullptr)
            x->world->set_health(*x, hp);
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
    // 2:21 memory several objects share by a number (trap_teamwork_alloc, FUN_1403f6df0: n + 1 bytes, zeroed, made by
    // the first to ask; to each later one the same, its first byte made 1 -- the Sorcerer's cubes start their shots'
    // turn-giving thread once so, the first to ask; without it each started its own and none had a turn to shoot)
    calls.set(2, 21, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint32_t &at = teamwork()[{&m.memory(), a[0].bits}];
        if (at == 0) {
            at = m.memory().add(a[1].bits + 1, "2:21's shared memory");
        } else {
            const std::uint8_t made = 1;
            m.memory().write(at, &made, 1);
        }
        a[0] = ai::Value::of_address(at);
    });
    // 0:78 between two vectors (trap_vector_inter, FUN_14041d490 -> FUN_140140230): the first times 1 - t and the
    // second times t, all four parts
    calls.set(0, 78, [](ai::Machine &m, std::span<ai::Value> a) {
        const Vec from = read_vec(m, a[0].bits), to = read_vec(m, a[1].bits);
        const float t = a[2].f();
        Vec v;
        for (int i = 0; i < 4; ++i)
            v.v[i] = from.v[i] * (1.0F - t) + to.v[i] * t;
        a[0] = give_vec(m, "0:78", v);
    });
    // 0:83 a vector turned about an axis by an angle (trap_vector_rot, FUN_14041d650 -> FUN_1403b7560: the axis made
    // one long, FUN_1403b6fb0's turn -- cos + k k (1 - cos) + [k] sin -- the vector times it); w kept
    calls.set(0, 83, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v = read_vec(m, a[0].bits);
        const Vec k = read_vec(m, a[1].bits);
        const float n = std::sqrt(k.v[0] * k.v[0] + k.v[1] * k.v[1] + k.v[2] * k.v[2]);
        if (n > 0.0F) {
            const float x = k.v[0] / n, y = k.v[1] / n, z = k.v[2] / n;
            const float r = wrapped(a[2].f()), c = std::cos(r), s = std::sin(r);
            const float dot = (v.v[0] * x + v.v[1] * y + v.v[2] * z) * (1.0F - c);
            const float cross[3] = {y * v.v[2] - z * v.v[1], z * v.v[0] - x * v.v[2], x * v.v[1] - y * v.v[0]};
            const float axis[3] = {x, y, z};
            float out[3];
            for (int i = 0; i < 3; ++i)
                out[i] = v.v[i] * c + cross[i] * s + axis[i] * dot;
            for (int i = 0; i < 3; ++i)
                v.v[i] = out[i];
        }
        a[0] = give_vec(m, "0:83", v);
    });
    // 0:82 two vectors' outer product, the first's times the second's (trap_vector_outer_product, FUN_14041d570 ->
    // FUN_140142120; w 0) and 0:91 their inner product once each is made one long (trap_vector_inner_prodcut,
    // FUN_14041d8b0: FUN_140142080 on copies, FUN_140140200 of x, y, z): the NaNs of both made 0 where they are first,
    // as KH2 does. Barbossa's and the Illuminator's aims
    const auto fixed = [](ai::Machine &m, std::uint32_t at) {
        Vec v = read_vec(m, at);
        bool any = false;
        for (float &f : v.v)
            if (std::isnan(f)) {
                f = 0.0F;
                any = true;
            }
        if (any)
            m.memory().write(at, v.v, sizeof v.v);
        return v;
    };
    calls.set(0, 82, [fixed](ai::Machine &m, std::span<ai::Value> a) {
        const Vec y = fixed(m, a[1].bits), x = fixed(m, a[0].bits);
        Vec v;
        v.v[0] = x.v[1] * y.v[2] - x.v[2] * y.v[1];
        v.v[1] = x.v[2] * y.v[0] - x.v[0] * y.v[2];
        v.v[2] = x.v[0] * y.v[1] - x.v[1] * y.v[0];
        v.v[3] = 0.0F;
        a[0] = give_vec(m, "0:82", v);
    });
    calls.set(0, 91, [fixed](ai::Machine &m, std::span<ai::Value> a) {
        const auto unit = [](Vec v) {
            const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
            if (n != 0.0F)
                for (int i = 0; i < 3; ++i)
                    v.v[i] /= n;
            return v;
        };
        const Vec x = unit(fixed(m, a[0].bits)), y = unit(fixed(m, a[1].bits));
        a[0] = ai::Value::of_float(x.v[0] * y.v[0] + x.v[1] * y.v[1] + x.v[2] * y.v[2]);
    });
}

// ------------------------------------------------------------------------------------------------- states

void add_states(ai::Calls &calls)
{
    // 1:90 a state's name shouted to every object, at once or each after a time at random below the value
    // (trap_obj_act_shout: FUN_1403cacd0 lists it, FUN_1403c9d40 sends the list to every object once a frame, each
    // running its own state of that name's shout routine, FUN_1403ca950): the world's
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
        // the second whenever it is given (YS::trap_obj_hook: VM::dup with 2 values when it is not 0, 1 when it is) --
        // until 2026-11-16 only when it named an object, and Final Xemnas's Reflect thread, given a record of his work,
        // popped a value it was never given and stopped
        const bool second = a[2].bits != 0;
        ai::Thread *t = m.start_routine(nullptr, entry, std::span<const ai::Value>(values, second ? 2u : 1u));
        if (t == nullptr)
            return;
        if (on == x)
            x->attach_thread(m, t->id);
        else if (running(m)->world != nullptr)
            running(m)->world->player_thread(*on, m, t->id);
    });
    // 1:214 the thread attached to the actor stopped (trap_obj_hook_stop; FUN_14042cab0 -> FUN_1403d76d0 with none)
    calls.set(1, 214, [](ai::Machine &m, std::span<ai::Value> a) { self_of(m, a[0])->attach_thread(m, 0); });
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
    // 1:167 entered as 1:8 when its priority is at least the current state's (trap_obj_act_start_pri, FUN_14042c7d0 ->
    // FUN_1403caa90): the Dancer's
    calls.set(1, 167, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]))
            x->states().enter_over(*x, name_at(m.memory(), a[1].bits));
    });
    // 0:60 an assertion (trap_assert, FUN_14041e710: empty in the PC's build): the Rapid Thruster's and the Armored
    // Knight's events' routines make it
    calls.set(0, 60, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(1, 10, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x != nullptr && x->states().pending() ? 1 : 0);
    });
    calls.set(1, 127, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]))
            x->states().clear(*x);
    });
}

void add_final_xemnas(ai::Calls &calls);
void add_friend_calls(ai::Calls &calls);

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
    // 1:39 its unit's argument n (trap_obj_unit_arg; FUN_1403b5df0: + 0x9f4 + 4 n): what the room's spawn gives each
    // object, or 1:218 sets; Days's waves give none: 0
    calls.set(1, 39, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x ? std::int32_t(x->u32(field::kUnitArgs + 4 * (a[1].bits & 0xff))) : 0);
    });
    // 1:218 one set (trap_obj_set_unit_arg; FUN_14042db40 -> FUN_1403b65b0)
    calls.set(1, 218, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]))
            x->set_u32(field::kUnitArgs + 4 * a[1].bits, a[2].bits);
    });
    // 1:180 the motion its jump or fall lands in (trap_obj_set_land_motion; FUN_14042ce30: + 0x11c)
    calls.set(1, 180, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->motion.landing = std::int32_t(a[1].bits);
    });
    // 1:59 whether it flies (trap_obj_is_fly; FUN_14042ef50: its class's flag bit 1 -- class 5, 1:58's
    // trap_obj_fly -- or + 0x18c bit 6); 1:224 whether its class is one of the air's (trap_obj_is_action_air;
    // FUN_14042d120: its flags' bits 0 or 1 -- off the ground, or flying)
    calls.set(1, 59, [](ai::Machine &m, std::span<ai::Value> a) {
        const Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x->dashing || (x->u32(field::kStatus) & 0x40u) != 0 ? 1 : 0);
    });
    calls.set(1, 224, [](ai::Machine &m, std::span<ai::Value> a) {
        const Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x->dashing || (x->u32(field::kFlags) & bit::kInAir) != 0 ? 1 : 0);
    });
    // 1:36 a part of KH2's room hidden (trap_bg_hide; FUN_14042e690): Days's room is not KH2's -- nothing
    calls.set(1, 36, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:203 a spawn unit of KH2's room let go on (trap_unit_enable; FUN_140431000: its flags' bit 0 cleared): Days's
    // waves have no KH2 units -- nothing (the enemies a boss's unit would bring do not come)
    calls.set(1, 203, [](ai::Machine &, std::span<ai::Value>) {});
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
    // 1:52 / 1:53 / 1:54 a collision group turned on, off, or back to what the motion and the shapes say (FUN_14042edb0,
    // FUN_14042ede0, FUN_14042ee10 on + 0x5a8: FUN_1403f1fa0 the on word's bit, FUN_1403f1f80 the off word's,
    // FUN_1403f2000 both cleared)
    const auto collision = [](int how) {
        return [how](ai::Machine &m, std::span<ai::Value> a) {
            Object *x = object_of(m, a[0]);
            if (x == nullptr)
                return;
            const std::uint32_t group = a[1].bits, at = field::kCollisionFlags + (group >> 5) * 4;
            const std::uint32_t bit = 1u << (group & 31);
            if (how > 0) {
                x->set_bits(at, bit, true);
            } else if (how < 0) {
                x->set_bits(at + 4, bit, true);
            } else {
                x->set_bits(at, bit, false);
                x->set_bits(at + 4, bit, false);
            }
        };
    };
    calls.set(1, 52, collision(1));
    calls.set(1, 53, collision(-1));
    calls.set(1, 54, collision(0));
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
    // 1:348 a sheet's hitback addition (trap_sheet_set_hitback_addition, FUN_140431260: its + 0x1c8), which its
    // object's hits' slides add to their speed (FUN_1403d4960; 2:3): Saïx's claymore's 200 on its owner's sheet while
    // it is wielded. A Days player has none (1:84 gives 0): nothing then
    calls.set(1, 348, [](ai::Machine &m, std::span<ai::Value> a) {
        if (a[0].bits != 0)
            m.memory().set_u32(a[0].bits + 0x1c8, a[1].bits);
    });
    // 1:353 an object's MP recharge started for a time (trap_obj_start_mpdrive -> YS::BTLOBJ::start_mpdrive): Sephiroth's
    // Heartless Angel leaves the player 1 HP (1:155) and no MP. Days's players have no MP gauge (their magic goes by
    // its deck's charges): nothing
    calls.set(1, 353, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:75 a slot of its motion set hooked (trap_obj_motion_hook, FUN_14042f460 -> FUN_1403c7c10: the slot's data at
    // the motion controller's + 0x160 when there is one), 1:76 none (FUN_1403c8f10), 1:77 whether one is (+ 0x160 not
    // 0): Motions::hook
    calls.set(1, 75, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]); x->motions != nullptr)
            x->motions->hook(std::int32_t(a[1].bits));
    });
    calls.set(1, 76, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]); x->motions != nullptr)
            x->motions->unhook();
    });
    calls.set(1, 77, [](ai::Machine &m, std::span<ai::Value> a) {
        const Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x->motions != nullptr && x->motions->hooked() ? 1 : 0);
    });
    calls.set(1, 97, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_address(self_of(m, a[0])->address() + 0x128);
    });
    calls.set(1, 179, [](ai::Machine &m, std::span<ai::Value> a) {
        if (is_player(object_of(m, a[0])))
            return; // (one of Days's players: his own fall is Days's)
        self_of(m, a[0])->motion.after = std::int32_t(a[1].bits);
    });
    // 1:211 / 1:212 bit n of + 0xd10 set / cleared (FUN_14042ba50, FUN_14042ba80)
    calls.set(1, 211, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->set_bits(0xd10, 1u << (a[1].bits & 31u), true);
    });
    calls.set(1, 212, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->set_bits(0xd10, 1u << (a[1].bits & 31u), false);
    });
    // 1:324 the same bit cleared (trap_obj_pattern_reset, FUN_14042bab0): the Hydra's
    calls.set(1, 324, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->set_bits(0xd10, 1u << (a[1].bits & 31u), false);
    });
    // 1:140 whether a target record's object (its + 8) is there (FUN_1403bde30; its other test, the player's lock,
    // not here); 1:143 an actor found by its placing record's number (FUN_1403b41e0, 0x70 for 0): none of Days's,
    // the slot left empty; 1:162 the actor put on one of KH2's lists (FUN_1403bd410): nothing here
    // ... or its + 0's an actor in play with a shape of kind 3 in its groups on (what 6:5 finds; the PS3 build's
    // YS::TARGET::is_exist: OBJ::is_exist, is_enable_collision, a collision of TYPE 3) -- a player's always (KH2's
    // Sora has his): until 2026-11-19 a target record on the player read as gone, and Vexen's disc, homing on him
    // through one, stopped and flew off in a straight line
    calls.set(1, 140, [](ai::Machine &m, std::span<ai::Value> a) {
        bool there = Object::at(m.memory(), m.memory().u32(a[0].bits + 8)) != nullptr;
        Object *o = there ? nullptr : Object::at(m.memory(), m.memory().u32(a[0].bits));
        if (o != nullptr && is_player(o))
            there = true;
        else if (o != nullptr)
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
    // begins on 1); any other object its own entry's (YS::trap_obj_part: Luxord's 4005, which his mission's damage
    // trigger asks; his die's and card's 1, KH2's player's), 0 without one
    calls.set(1, 213, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        std::uint16_t part = 0;
        if (is_player(o))
            part = 1;
        else if (const std::uint32_t entry = o != nullptr ? o->u32(field::kEntry) : 0u; entry != 0)
            m.memory().read(entry + 0x4c, &part, 2);
        a[0] = ai::Value::of_int(part);
    });
    // 2:46 a hit's damage (YS::trap_damage_damage: YS::DAMAGE + 0x28, the PS3 build's) and 2:72 whether it cures
    // (trap_damage_is_cure: its attack's record, + 0x20, of type 5 or 6 at + 4) -- what a mission's damage trigger asks
    calls.set(2, 46, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_int(a[0].bits != 0 ? std::int32_t(m.memory().u32(a[0].bits + 0x28)) : 0);
    });
    calls.set(2, 72, [](ai::Machine &m, std::span<ai::Value> a) {
        const std::uint32_t params = a[0].bits != 0 ? m.memory().u32(a[0].bits + 0x20) : 0u;
        std::uint8_t type = 0;
        if (params != 0)
            m.memory().read(params + 4, &type, 1);
        a[0] = ai::Value::of_int(type == 5 || type == 6 ? 1 : 0);
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
    // 1:133 / 1:134 the object hidden and shown (YS::trap_obj_hide, trap_obj_show: its class's VTABLE hide, its flags'
    // FLAG_HIDE_MODEL and FLAG_HIDE_SHADOW set, and show, FLAG_HIDE_MODEL cleared): a KH2 actor's model and the effects
    // that fade with it not drawn meanwhile (Actor::hidden); a player's, World::player_hide. Until 2026-11-19 only a
    // player's was
    calls.set(1, 133, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (o != nullptr && is_player(o) && running(m)->world != nullptr)
            running(m)->world->player_hide(*o, true);
        else if (Actor *x = o != nullptr ? o->actor() : nullptr)
            x->hidden = true;
    });
    calls.set(1, 134, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (o != nullptr && is_player(o) && running(m)->world != nullptr)
            running(m)->world->player_hide(*o, false);
        else if (Actor *x = o != nullptr ? o->actor() : nullptr)
            x->hidden = false;
    });
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
    // 1:56 whether it is culled, off the screen (method_obj_is_culling in the PS3 build, FUN_14042ee40 ->
    // FUN_1403ba4a0: with + 0x9b8 bit 10 its box against the view, YS::BG::IsCulling; 0 without the bit): the world's
    // answer, not drawn this frame nor the one before. The enemies leave sooner off the screen (their "leave" distance
    // there the shorter); Undead Pirate C circles until it is seen, then shoots
    calls.set(1, 56, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x != nullptr && x->world != nullptr && !x->world->on_screen(*x) ? 1 : 0);
    });
    calls.set(1, 110, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(-1); });
    // 1:40 whether it is a pirate in its shade (trap_obj_is_pirate_shade, FUN_14042bd60 -> FUN_1403baa90; the PS3
    // build's YS::OBJ::is_pirate_shade): its entry's + 0x48 bit 5 (a pirate's) and its OBJ::PIRATE Place 1. KH2 makes
    // a pirate at Place 1, and its move on the ground (OBJ::move_at_bg) sets Place from the floor's polygon -- 0 on one
    // marked 1, 1 on one marked 0: Port Royal's moonlight. Days's rooms have no such floors; the port's pirates stand
    // out of it, at Place 0, in no shade: they take hits (the Undead Pirates take none in it, their element 6 rate 0)
    // and show their parts of that (Actor::faded_parts)
    calls.set(1, 40, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:347 whether a line between two objects is clear (trap_obj_can_see, FUN_14042c330: each one's aim point, 1:88's
    // FUN_1403b5cb0, then FUN_1403f3c20 -- the room's collision cast from the first's to the second's, FUN_14040c2b0 --
    // seen when it meets none): the room's, Days's (World::ray). Barbossa's aim
    calls.set(1, 347, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        Object *from = object_of(m, a[0]), *to = object_of(m, a[1]);
        Vec3 p, q, hit;
        const bool seen = w != nullptr && from != nullptr && to != nullptr && w->aim_point(*from, p) &&
                          w->aim_point(*to, q) && !w->ray(*running(m), p, q, hit);
        a[0] = ai::Value::of_int(seen ? 1 : 0);
    });
    // 1:250 the player's lock-on reach times a rate (trap_status_set_lockon_ratio, FUN_1404308d0: DAT_142a1141c and
    // the battle constants' + 0x50 times it, KH2's own lock-on): Days's lock-on is its own, so nothing
    calls.set(1, 250, [](ai::Machine &, std::span<ai::Value>) {});
    // 6:63 whether its motion is one of its hits' (trap_obj_is_damage_motion, FUN_14053ee50 -> FUN_1403d4880: a motion
    // set, and its motion's number, + 0x180, from 10 to 39). Jafar's and Axel's
    calls.set(6, 63, [](ai::Machine &m, std::span<ai::Value> a) {
        const Object *x = object_of(m, a[0]);
        const std::uint32_t motion = x != nullptr ? x->u32(field::kMotion) : 0u;
        a[0] = ai::Value::of_int(x != nullptr && motion - 10u < 30u ? 1 : 0);
    });
    // 2:70 the part a hit struck (trap_damage_parts, FUN_140432100: YS::DAMAGE's byte + 0x25): Days's hits strike no
    // part, 0
    calls.set(2, 70, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint8_t part = 0;
        m.memory().read(a[0].bits + 0x25, &part, 1);
        a[0] = ai::Value::of_int(part);
    });
    // 6:62 Jafar's carpet's idle (trap_obj_carpet_obj_idle, FUN_14053ee20: its class made 0x13, FUN_1403b4da0); that
    // class's native movement is not read: the port's actor moves as class 0 then, by its motions and steering. 6:45
    // its shadow hidden (trap_obj_hide_shadow, FUN_14053e960: + 0x124 bit 9): KH2's shadow is not drawn for the port's
    // enemies, the bit only kept
    calls.set(6, 62, [](ai::Machine &m, std::span<ai::Value> a) {
        if (!is_player(object_of(m, a[0])))
            self_of(m, a[0])->dashing = false;
    });
    calls.set(6, 45, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]))
            x->set_bits(0x124, 1u << 9, true);
    });
    // 1:85 / 1:86 a texture animation of its model started and stopped by number (trap_obj_texanm_start, _stop:
    // FUN_1403cd8a0, FUN_1403cd8b0 on + 0x7b0's model, kn::ModelObj's texture animations of that number): the port draws
    // no KH2 enemy's texture animations, so nothing. 1:164 an event sequence of a name, as 1:157 starts one but with no
    // object for its camera (trap_obj_camera_start_global, FUN_14042e0d0 -> FUN_1403db2f0 with none): Axel's
    calls.set(1, 85, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(1, 86, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(1, 164, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        if (x != nullptr && x->world != nullptr)
            x->world->event(*x, name_at(m.memory(), a[1].bits), nullptr);
    });
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
    // 2:45 the number of a hit's attack (trap_damage_attack_param_id; FUN_1404320a0: YS::DAMAGE's AttackParam, + 0x20,
    // its u16 at + 2): the Wight Knight asks it of its hits -- a Days hit names no KH2 attack: 0
    calls.set(2, 45, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:0 an object made by its entry's number (trap_sysobj_appear; FUN_14042b130 -> FUN_1403df930: at the world's
    // origin facing pi (DAT_140623df0, DAT_140623b78), its class by its entry's kind, its script started), its AI
    // handle given back, 0 none: the Sorcerer's cubes make the Sorcerer (315) so, then put it where they are (1:148,
    // 1:79). The field makes it (World::appear)
    calls.set(1, 0, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = running(m);
        const std::uint32_t made = me->world != nullptr ? me->world->appear(*me, a[0].bits) : 0u;
        a[0] = ai::Value::of_address(made);
    });
    // 1:328 the object counted as defeated, once (trap_obj_dead_mark; FUN_14042dd50 -> FUN_1403d4a40: unless + 0x9b8
    // bit 2 is set, its group told (+ 0x9e8, FUN_1403fed10), the bit set): the Sorcerer's cubes mark themselves so in
    // their "leave" (far from the player), then fade out. Its script goes on; the field counts it (World::dead_mark)
    calls.set(1, 328, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        if ((x->u32(field::kFade) & bit::kDeadMark) != 0)
            return;
        x->set_bits(field::kFade, bit::kDeadMark, true);
        if (x->world != nullptr)
            x->world->dead_mark(*x);
    });
    // 1:222 its object entry's number (trap_obj_get_entry_id; FUN_14042bb10 -> FUN_1403b5770: + 8's record, + 0)
    calls.set(1, 222, [](ai::Machine &m, std::span<ai::Value> a) {
        const Object *x = object_of(m, a[0]);
        const std::uint32_t record = x != nullptr ? x->u32(field::kEntry) : 0u;
        a[0] = ai::Value::of_int(record != 0 ? std::int32_t(m.memory().u32(record)) : 0);
    });
    // 1:114 the object of an entry's number into an AI handle's + 4 (trap_obj_search_by_entry; FUN_14042ddb0 ->
    // FUN_1403b4110: 0x236 the player, else the field's first object whose entry it is), 0 when none is
    calls.set(1, 114, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = running(m);
        std::uint32_t found = 0;
        if (me->world != nullptr && a[1].bits == 0x236u) {
            if (Object *p = me->world->player(*me))
                found = p->address();
        } else if (me->world != nullptr) {
            for (Actor *x : me->world->objects())
                if (const std::uint32_t record = x->u32(field::kEntry);
                    record != 0 && m.memory().u32(record) == a[1].bits) {
                    found = x->address();
                    break;
                }
        }
        m.memory().set_u32(a[0].bits + 4, found);
    });
    // 1:155 health added to a bar of its record (trap_obj_add_hp: its object's add_hp, YS::BTLOBJ::_OVR_add_hp ->
    // YS::SHEET::_add_hp: kept between the bar's least and most; the bar's health given back): the first bar's given to
    // Days's enemy (at least 1)
    calls.set(1, 155, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const std::uint32_t sheet = x->u32(0x5c0);
        if (sheet == 0) {
            a[0] = ai::Value::of_int(0);
            return;
        }
        const std::uint32_t at = sheet + 12u * a[2].bits;
        const std::int32_t least = std::int32_t(m.memory().u32(at + 8)), most = std::int32_t(m.memory().u32(at + 4));
        const std::int32_t hp =
            std::max(least, std::min(std::int32_t(m.memory().u32(at)) + std::int32_t(a[1].bits), most));
        m.memory().set_u32(at, std::uint32_t(hp));
        if (a[2].bits == 0 && x->world != nullptr)
            x->world->set_health(*x, hp);
        a[0] = ai::Value::of_int(hp);
    });
    // 1:194 / 1:195 parts of its model put in stealth and back (trap_obj_set_stealth, trap_obj_reset_stealth;
    // FUN_14042cbd0, FUN_14042cc00 -> FUN_1401a8990, FUN_1401a85e0 on + 0x7b0's model): the Shadow Stalker's
    calls.set(1, 194, [](ai::Machine &m, std::span<ai::Value> a) { self_of(m, a[0])->stealth |= a[1].bits; });
    // 1:108 / 1:109 a part of its model shown and hidden (trap_obj_show_part, trap_obj_hide_part: FUN_1403cd810 ->
    // FUN_1401a8a90 and FUN_1403cc9b0 -> FUN_1401a7cf0 on + 0x7b0's model; the PS3 build's kn::ModelSKL::showPart sets
    // bits 0-1 of each subpart's flag to 1 whose part, its attribute word's bits 6-10, is the one named, -1 every one,
    // hidePart clears them): the Prison Keeper's faces, the Hydra's heads
    const auto parts = [](bool hide) {
        return [hide](ai::Machine &m, std::span<ai::Value> a) {
            Actor *x = self_of(m, a[0]);
            const std::int32_t part = a[1].i();
            const std::uint32_t mask = part == -1 ? ~0u : part >= 0 && part < 32 ? 1u << part : 0u;
            x->hidden_parts = hide ? x->hidden_parts | mask : x->hidden_parts & ~mask;
        };
    };
    calls.set(1, 108, parts(false));
    calls.set(1, 109, parts(true));
    calls.set(1, 195, [](ai::Machine &m, std::span<ai::Value> a) { self_of(m, a[0])->stealth &= ~a[1].bits; });
    // 1:185 a colour put on parts of its model, faded in over ticks (trap_obj_set_parts_color, FUN_14042cb40 ->
    // FUN_1403cd670: the entry of that part mask among its eight at + 0xac4, or a free one; its RGBA bytes, its strength
    // run from none to full, FUN_1403b7810); 1:186 faded out from where it is (trap_obj_reset_parts_color,
    // FUN_1403cd4c0). Actor::part_colours: Oogie Boogie's machinery's
    const auto colour_of = [](Actor &x, std::uint32_t mask) -> Actor::PartColour & {
        for (Actor::PartColour &c : x.part_colours)
            if (c.mask == mask)
                return c;
        Actor::PartColour made;
        made.mask = mask;
        x.part_colours.push_back(made);
        return x.part_colours.back();
    };
    const auto run = [](Actor::PartColour &c, float from, float to, float ticks) {
        if (ticks <= 2.0F)
            ticks = 1.0F;
        c.strength = from;
        c.step = (to - from) / ticks;
    };
    calls.set(1, 185, [colour_of, run](ai::Machine &m, std::span<ai::Value> a) {
        Actor::PartColour &c = colour_of(*self_of(m, a[0]), a[1].bits);
        for (int k = 0; k < 4; ++k)
            c.rgba[k] = std::uint8_t(a[2].bits >> (8 * k));
        run(c, 0.0F, 1.0F, a[3].f());
    });
    calls.set(1, 186, [colour_of, run](ai::Machine &m, std::span<ai::Value> a) {
        Actor::PartColour &c = colour_of(*self_of(m, a[0]), a[1].bits);
        run(c, c.strength, 0.0F, a[2].f());
    });
    // 2:36 nothing in the PC game (trap_enemy_add_damage, FUN_140431d90 is empty). 1:158 and 1:159 the room's belt
    // conveyor and its pictures' scroll speed (trap_bg_set_belt_conveyor, trap_bg_set_uvscroll_speed: a task of the
    // map's, FUN_1401506b0): the room is a Days room drawn from the map's model, its scrolls its own -- not driven
    calls.set(2, 36, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(1, 158, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(1, 159, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:125 its pictures' fade (trap_obj_tex_fade_start; FUN_1404305d0 -> FUN_1403b7810 on + 0xaa4, YS::OBJ's TexFade):
    // from a value to another over a time (2 or less: 1), kept -- the port does not draw it yet
    calls.set(1, 125, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const float from = a[1].f(), to = a[2].f(), time = a[3].f() <= 2.0F ? 1.0F : a[3].f();
        x->set_f(0xaa4, from);
        x->set_f(0xaa8, (to - from) / time);
    });
    // 2:8 whether a hit's reaction kind (+ 0x26) is past 1 (FUN_140431f70, FUN_1403ce980: 1 and 12 are 1; 3, 6, 8,
    // 10, 13, 16, 18 are 3; 4, 7, 9, 11, 15, 17 are 4; the others themselves): trigger 6's hit, a Days hit a small
    // reaction (3) unless the enemy's damage patterns make it none
    calls.set(2, 8, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint8_t kind = 0;
        m.memory().read(a[0].bits + 0x26, &kind, 1);
        const unsigned group = kind == 1 || kind == 12 ? 1u : kind;
        a[0] = ai::Value::of_int(group > 1 ? 1 : 0);
    });
    // 2:7 a hit's reaction kind itself (YS::DAMAGE's Reaction, + 0x26; FUN_140431f10)
    calls.set(2, 7, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint8_t kind = 0;
        m.memory().read(a[0].bits + 0x26, &kind, 1);
        a[0] = ai::Value::of_int(kind);
    });
    // 2:15 a hit's reaction to be carried out (trap_damage_exec_reaction; FUN_140432050: its Flag, + 0x18, bit 0) --
    // 2:1 sets it too; the port plays a Days hit's reaction before trigger 6, so nothing waits on it
    calls.set(2, 15, [](ai::Machine &m, std::span<ai::Value> a) {
        m.memory().set_u32(a[0].bits + 0x18, m.memory().u32(a[0].bits + 0x18) | 1u);
    });
    // 2:92 whether a hit is a finishing one: its first reaction kind (OrigReaction, + 0x27) 6, 7, 10 or 11
    // (FUN_1404321f0 -> FUN_1403cedc0). A Days hit names none: no
    calls.set(2, 92, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint8_t kind = 0;
        m.memory().read(a[0].bits + 0x27, &kind, 1);
        a[0] = ai::Value::of_int((kind >= 6 && kind < 8) || (kind >= 10 && kind < 12) ? 1 : 0);
    });
    // 1:161 whether another object carries the actor (+ 0x6a0, YS::OBJ's Attach; FUN_14042b980 -> FUN_1403da520): none
    // of Days's do
    calls.set(1, 161, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x != nullptr && x->u32(field::kCarrier) != 0 ? 1 : 0);
    });
    // 2:76 its karma's limit (YS::ENEMY's KarmaLimit, + 0xd4c; FUN_140431e30), kept: the port's revenge does not read
    // it yet
    calls.set(2, 76, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]))
            x->set_u32(field::kKarmaLimit, a[1].bits);
    });
    // the world's flags: 1:23 DAT_140717008 (the script tests 10), 1:24, 1:301 FUN_1403bbea0, 1:171 / 1:206
    // DAT_142a11400's bits 2 / 7 -- KH2's field busy (a pause), which Days's field never is for its enemies: 0
    // 1:24 DAT_140717009, the byte after 1:23's (the Wight Knight tests 9) -- the same
    for (const unsigned n : {23u, 24u, 301u, 171u})
        calls.set(1, n, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:34 whether an event runs (YS::trap_event_is_exec -> YS::EVENT::IsExec: YS::Current set; DAT_142a11478,
    // FUN_1403ac220): an object's camera sequence started by 1:164 is one (YS::STDOBJ::start_camera makes a YS::EVENT of
    // kind 0xb and starts it) -- World::event_playing. Final Xemnas's laser dome fires (8:4) once his "60" is over
    calls.set(1, 34, [](ai::Machine &m, std::span<ai::Value> a) {
        const World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && w->event_playing() ? 1 : 0);
    });
    // 1:204 / 1:205 / 1:206 the field's "force leave" set, cleared, read (trap_status_force_leave_start, _end,
    // trap_status_is_force_leave: FUN_140430860, FUN_140430870, DAT_142a11400 bit 7)
    calls.set(1, 204, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->force_leave = true;
    });
    calls.set(1, 205, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->force_leave = false;
    });
    calls.set(1, 206, [](ai::Machine &m, std::span<ai::Value> a) {
        const World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && w->force_leave ? 1 : 0);
    });
    // 1:323 how many of the field's objects are of an entry's number (trap_obj_count_entry, FUN_1403b3e20: its objects'
    // list, each one's + 8 record's + 0): Oogie Boogie counts his Heartless so
    calls.set(1, 323, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = running(m);
        std::int32_t count = 0;
        if (me->world != nullptr) {
            for (Actor *x : me->world->objects())
                if (const std::uint32_t record = x->u32(field::kEntry);
                    record != 0 && m.memory().u32(record) == a[0].bits)
                    ++count;
            if (a[0].bits == 0x236u && me->world->player(*me) != nullptr)
                ++count; // the player's entry (1:114's)
        }
        a[0] = ai::Value::of_int(count);
    });
    // 1:145 the zone of the room an object stands in (trap_obj_touch_zone, FUN_14042b6f0: + 0xcf4, KH2's area's event
    // zones -- Oogie Boogie's 122 to 124, the platforms the player is on): Days's rooms have none, 0
    calls.set(1, 145, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 0:24 a value printed for KH2's debugging (trap_stputi, FUN_14041ca20: empty in the game); 0:25 a float so
    // (trap_stputf: Sephiroth's)
    calls.set(0, 24, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(0, 25, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:37 a part of KH2's map shown (trap_bg_show, FUN_14042e6a0: DAT_140abcbd8's bit, the map's and its effects'
    // parts): the room is Days's, which has no such parts -- nothing (the Hydra's)
    calls.set(1, 37, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:335 / 1:336 its bones' occluders off and on (trap_obj_disable_occ_bone, _enable_: dk::ObjOcc's records at
    // + 0x9c0 of that bone, + 0x10 bit 1 -- KH2's occlusion culling, which hides what a big body covers): the port
    // culls nothing so -- nothing (the Hydra's)
    calls.set(1, 335, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(1, 336, [](ai::Machine &, std::span<ai::Value>) {});
    // 6:1 a prize made at a point (Ts::trap_prize_appear, FUN_14053dba0 -> FUN_1403dc170: its kind's prizes, as many
    // as the party's needs make): Days gives its stand-in's own -- nothing (the Hydra's heads)
    calls.set(6, 1, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:32 a message window opened (trap_message_open, FUN_14042e5c0: a message object, FUN_1403bb270, of a text and
    // its speaker, its handle given back), 1:48 whether it is over (method_message_is_end, FUN_14042e640: its + 0xa0
    // 3), 1:33 closed (trap_message_close, FUN_14042e670: its class's + 0x38): Days shows no KH2 message window -- a
    // handle of none, over at once, nothing closed (Phil's talk)
    calls.set(1, 32, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_address(0); });
    calls.set(1, 48, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(1); });
    calls.set(1, 33, [](ai::Machine &, std::span<ai::Value>) {});
    // 4:0, 4:1 around a talk's message (Phil's): KH2FM.exe's table has no routine at them (the PS3 build's are unnamed)
    // -- nothing. 4:4 the mission's information shown (trap_mission_information, FUN_1404334b0: when a mission runs,
    // FUN_14017b710): Days's mission shows its own -- nothing
    calls.set(4, 0, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(4, 1, [](ai::Machine &, std::span<ai::Value>) {});
    calls.set(4, 4, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:170 its spawn unit's group (trap_obj_unit_group, FUN_14042dac0: + 0x9e8's record's + 8's u16 at + 2): Days's
    // waves give none -- 0
    calls.set(1, 170, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:41 a signal sent (trap_signal_call, FUN_14042ec90: the reserved ones of that id let go, the signal queued,
    // the PS3 build's YS::SIGNAL::Call): every script of the field and its mission told (trigger 10; World::signal)
    calls.set(1, 41, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *me = running(m); me->world != nullptr)
            me->world->signal(a[0].i(), a[1].i());
    });
    // 1:200 its collision with the room's kind (trap_obj_set_bg_collision_type, FUN_14042da80: + 0x740 bits 3 to 6):
    // kept there as KH2 keeps it; Days's collision is its own (Phil's urn)
    calls.set(1, 200, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        x->set_u32(0x740, (x->u32(0x740) & ~0x78u) | ((a[1].bits & 0xfu) << 3));
    });
    // 1:17 it made to leave at once (trap_obj_leave_force, FUN_14042c870: its class's + 0x38 on it): the field lets it
    // go (World::leave: an ally goes; an enemy's Days enemy stays, its script going on)
    calls.set(1, 17, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        if (x->world != nullptr)
            x->world->leave(*x);
    });
}

// ------------------------------------------------------------------------------------------------- motions

// Thrown (FUN_140409d30's class 4): along the ground (x, z made one long, times `speed`), `up` its first vertical
// speed (KH2's y: negative up), no steering in the air, landing in the motion after the one playing (+ 0x114 the motion
// at the throw; FUN_140422700). Its movement (FUN_140422780) adds KH2's pull (0.408163) to the speed each evaluation
// after slowing the rise and the speed along by values the game sets running (DAT_142ae5760 + 0x20, + 0x18; its
// fastest fall + 0x138), not read yet: the port pulls 1 / 0.408163 a tick a tick, a throw's height and length near
// what those make (15 up rises 46 cm)
// A jump just asked kept for its motion's take-off when the motion has one ahead (+ 0x648: the class its trigger 0
// gives, FUN_14040dd70's event 0), the actor on the ground meanwhile, moving as it moved before the call; at once
// otherwise
void wait_for_take_off(Actor &x, const Vec3 &velocity, const Vec3 &steered)
{
    x.take_off.reset();
    if (x.world == nullptr || !x.world->takes_off_later(x))
        return;
    x.take_off = x.jump;
    x.jump = Jump{};
    x.velocity = velocity;
    x.steered = steered;
    x.set_bits(field::kFlags, bit::kInAir, false);
}

void thrown(Actor &x, float way_x, float way_z, float speed, float up)
{
    way_x = std::isnan(way_x) ? 0.0F : way_x;
    way_z = std::isnan(way_z) ? 0.0F : way_z;
    speed = std::isnan(speed) ? 0.0F : speed;
    const float flat = std::sqrt(way_x * way_x + way_z * way_z);
    x.take_off.reset();
    x.jump = Jump{};
    x.jump.on = true;
    x.jump.gravity = 1.0F / kKh2Pull;
    x.jump.keep = x.jump.slow = 1.0F;
    x.jump.up = std::isnan(up) ? 0.0F : up;
    x.jump.along = flat > 0.0F ? Vec3{way_x / flat * speed, 0.0F, way_z / flat * speed} : Vec3{};
    x.dashing = false;
    x.velocity = x.steered = x.jump.along;
    x.move_clock = 0.0F;
    x.set_bits(field::kFlags, bit::kInAir, true);
    x.jump.blow = true;
    x.motion.after = -1;
    x.motion.landing = x.motion.id >= 0 ? x.motion.id + 1 : 5;
}

void add_motions(ai::Calls &calls)
{
    // 1:29 another object plays a motion of this one's set (FUN_1403c6cc0: its motion set made this one's, + 0x2b8,
    // its motion number + 0x180), from its first frame over a blend (FUN_1403c65e0 starts it with the call's value as
    // the blend and 0 as the frame, as the PS3 build's YS::MOTION::_capture does); the motion given back, what its
    // controller then plays (+ 0x184, 1:238: the Armored Knight's flight runs while the two are the same)
    calls.set(1, 29, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        Object *o = object_of(m, a[1]);
        Actor *other = o != nullptr ? o->actor() : nullptr;
        bool played = other != nullptr;
        if (other != nullptr && x->world != nullptr && x->world->capture_motion(*other, *x, a[2].i(), a[3].f())) {
            // (its own part of the slot: Final Xemnas's air fight carries Riku in his AQ10, "0190")
        } else if (other != nullptr) {
            other->motions = x->motions;
            other->play(std::int32_t(a[2].bits), a[3].f(), 0.0F);
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
    // 1:13 a motion asked of the object's controller to follow the one it plays (trap_sysobj_motion_push ->
    // YS::MOTION::push_queue: its ring of motions, which add_time takes from as the motion reaches its end): queued
    // (Playing::queued), played at once when none plays. Until 2026-11-14 it was played at once: Zexion's illusion,
    // asking 201 and then 202, skipped 201 and never called him on to his book ("start_illusion" at 201's trigger 10).
    // A player's own (Cerberus's 0, its idle, at the end of its reaction command) gives the player back to Days
    calls.set(1, 13, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (Actor *x = o != nullptr ? o->actor() : nullptr) {
            if (x->motion.id < 0)
                x->play(std::int32_t(a[1].bits), a[2].f(), 0.0F);
            else
                x->motion.queued.push_back({std::int32_t(a[1].bits), a[2].f()});
        } else if (is_player(o) && running(m)->world != nullptr)
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
    // on a player (no actor of his own: Days's) his own motion started at once -- before, the call fell to the running
    // actor, which played the player's idle itself (Zexion's illusion ending kept the player in its hands for good)
    const auto own_motion_now = [](ai::Machine &m, const ai::Value &record, int id) {
        Object *o = object_of(m, record);
        if (o == nullptr || o->actor() != nullptr || !is_player(o))
            return false;
        if (World *w = running(m)->world)
            w->player_own_motion_now(*o, id);
        return true;
    };
    calls.set(1, 11, [own_motion_now](ai::Machine &m, std::span<ai::Value> a) {
        if (!own_motion_now(m, a[0], std::int32_t(a[1].bits)))
            self_of(m, a[0])->play(std::int32_t(a[1].bits), a[2].f(), 0.0F);
    });
    calls.set(1, 61, [own_motion_now](ai::Machine &m, std::span<ai::Value> a) {
        if (!own_motion_now(m, a[0], std::int32_t(a[1].bits)))
            self_of(m, a[0])->play(std::int32_t(a[1].bits), a[3].f(), a[2].f());
    });
    calls.set(1, 12, [own_motion_now](ai::Machine &m, std::span<ai::Value> a) {
        if (!own_motion_now(m, a[0], std::int32_t(a[1].bits)))
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
    // 1:55, 1:168 a jump (method_obj_jump, trap_obj_flyjump; FUN_1403cbb00, FUN_1404233f0): a vector -- its x, z the
    // way (made one long, then times its w, the speed along: + 0xe0), its y how it rises (a positive y: up that far, in
    // the ticks KH2's pull takes to fall it, sqrt(2 y / 0.408163) -- + 0xf4 FUN_1403b73f0, 1:100's -- eased, + 0xfc
    // -y; a negative y: no rise, its time begun at those ticks, + 0xf0, from + 0xf8 y, so down from where it is) --
    // then a motion; the flag sets the two after it (+ 0x118 its fall, + 0x11c its landing); its class (1) waits in
    // + 0x648 for the motion's take-off (trigger 0, FUN_14040dd70's event 0). Class 1's movement (FUN_1403cb360) gives
    // the height by the jump's time (Jump::arc)
    const auto begin_jump = [](Actor &x, const Vec &v) {
        x.jump.on = true;
        x.dashing = false;
        x.jump.gravity = Jump{}.gravity;
        x.jump.keep = Jump{}.keep; // class 1's steering in the air (FUN_1403cb360)
        x.jump.slow = Jump{}.slow;
        const float y = v.v[1], ticks = std::sqrt(std::fabs(2.0F * y / kKh2Pull));
        x.jump.arc = true;
        x.jump.up = 0.0F;
        x.jump.height = 0.0F;
        x.jump.time = y < 0.0F ? ticks : 0.0F;
        x.jump.apex = y < 0.0F ? 0.0F : ticks;
        x.jump.base = y < 0.0F ? y : 0.0F;
        x.jump.rise = y < 0.0F ? 0.0F : -y;
        const float flat = std::sqrt(v.v[0] * v.v[0] + v.v[2] * v.v[2]);
        x.jump.along = flat > 0.0F ? Vec3{v.v[0] / flat * v.v[3], 0.0F, v.v[2] / flat * v.v[3]} : Vec3{};
        x.velocity = x.jump.along;
        x.move_clock = 0.0F;
        x.steered = x.jump.along;
        x.set_bits(field::kFlags, bit::kInAir, true);
    };
    const auto jump = [begin_jump](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        const Vec3 velocity = x->velocity, steered = x->steered;
        begin_jump(*x, v);
        x->play(std::int32_t(a[2].bits), a[3].f(), 0.0F);
        x->motion.after = -1;
        x->motion.landing = 5;
        if (a[4].bits != 0) {
            x->motion.after = std::int32_t(a[2].bits) + 1;
            x->motion.landing = std::int32_t(a[2].bits) + 2;
        }
        wait_for_take_off(*x, velocity, steered);
    };
    calls.set(1, 55, jump);
    calls.set(1, 168, jump);
    // 1:227 a jump at once (trap_obj_jump_direct, FUN_14042cd40: its class's (+ 0xc) virtual + 0x18, the PS3 build's
    // YS::ACTION::jump -- YS::OBJ::prepare_jump with the vector as 1:55's, then change_action(1)): class 1 on the
    // spot, its motion as it is, no take-off waited for
    calls.set(1, 227, [begin_jump](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *o = object_of(m, a[0]); is_player(o)) { // one of Days's players: his own control back
            if (running(m)->world != nullptr)
                running(m)->world->player_let_go(*o, "his jump at once (1:227)");
            return;
        }
        Actor *x = self_of(m, a[0]);
        x->take_off.reset();
        begin_jump(*x, read_vec(m, a[1].bits));
    });
    // 1:156 a jump of the actor's own (FUN_14042ce60 -> FUN_1403cbec0): a vector -- its x, z the way along the
    // ground (made one long, then times its w), its y how high it rises -- the ticks to its top (its pull + 0x10c
    // 2 y / t^2, its rise the pull times t), a value kept at + 0x108 (class 9's steering in the air keeps that much of
    // its velocity: FUN_1403cbdd0); then a motion (FUN_1403c86a0 on + 0x158) and, the flag set, the two after it as
    // 1:55's. KH2 leaves the ground at the motion's take-off event (FUN_14040dd70, + 0x648 2: class 9), as the port does
    // when the motion has one ahead (wait_for_take_off)
    calls.set(1, 156, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec v = read_vec(m, a[1].bits);
        const Vec3 velocity = x->velocity, steered = x->steered;
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
        wait_for_take_off(*x, velocity, steered);
    });
    // 1:129 a throw of the actor's own (FUN_14042b550 -> FUN_1403d3e50): a vector (its NaNs made 0) -- its x, z the way
    // along the ground (made one long, then times its w), its y the speed up (+ 0x24 -y) -- and class 4, thrown
    // (FUN_140409d30; the classes at 0x140750d30; FUN_140422780 keeps the speed along): the Rapid Thruster's rise in
    // motion 18, landing in 19
    calls.set(1, 129, [](ai::Machine &m, std::span<ai::Value> a) {
        const Vec v = read_vec(m, a[1].bits);
        thrown(*self_of(m, a[0]), v.v[0], v.v[2], v.v[3], -v.v[1]);
    });
    // 2:1 a blow taken (trap_enemy_exec_damage_blow; FUN_140431b60 -> FUN_1403d3f50): from the front (the hit's way,
    // YS::DAMAGE's Dir at + 0, against its facing, FUN_1403b56e0) it turns to face the hit and plays the first motion
    // given, from behind it turns its back to it and plays the second (FUN_1403b6420, FUN_1403c86a0), then it is thrown
    // as 1:129 throws (FUN_140409d30): along the way's ground part the last value fast, the one before up -- landing in
    // the motion after; + 0x124 bit 17 a finishing hit's (2:92), the hit's Flag (+ 0x18) bit 0 set. The Mole Driller's
    // (16 and 18, 15 up, 20 along)
    calls.set(2, 1, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const std::uint32_t hit = a[1].bits;
        const Vec way = read_vec(m, hit);
        const Vec3 f = facing_of(x->yaw());
        const bool front = way.v[0] * f[0] + way.v[1] * f[1] + way.v[2] * f[2] < 0.0F;
        const float sign = front ? -1.0F : 1.0F;
        if (way.v[0] != 0.0F || way.v[2] != 0.0F)
            x->set_yaw(std::atan2(way.v[0] * sign, way.v[2] * sign));
        x->play(std::int32_t(front ? a[2].bits : a[3].bits), 0.0F, 0.0F);
        std::uint8_t first = 0;
        m.memory().read(hit + 0x27, &first, 1);
        x->set_bits(0x124, 1u << 17, (first >= 6 && first < 8) || (first >= 10 && first < 12));
        thrown(*x, way.v[0], way.v[2], a[5].f(), -a[4].f());
        m.memory().set_u32(hit + 0x18, m.memory().u32(hit + 0x18) | 1u);
    });
    // 2:3 a hit's slide (trap_enemy_exec_damage_hitback, FUN_140431c90 -> FUN_1403d4120 -> FUN_1403d4960): unless its
    // + 0x120 bit 7 holds it, the hit's way (Dir, + 0; turned back for a speed below 0) made one long, its speed a
    // tenth of the one given (its sheet's hitback addition added: + 0x1c8, 1:348), slid along as KH2's actor's
    // update does (Actor::step); the hit's Flag bit 0 set. 2:87 a large hit (trap_enemy_exec_damage_large,
    // FUN_1403d4140): from the front (the way against its facing) it faces the hit and plays motion 23, from behind it
    // turns its back to it and plays 24 (DAMAGE_L_FRONT, DAMAGE_L_BACK), its idle asked after (FUN_1403c8310: the port's
    // actor takes it at the motion's end); then the slide. The Volcanic Lord's Blizzard reaction (2:3, 0) and Hades's
    // (2:87, 0) slide by none.
    const auto slide = [](Actor &x, ai::Machine &m, std::uint32_t hit, float speed) {
        if ((x.u32(field::kFlags) & (1u << 7)) == 0) {
            Vec way = read_vec(m, hit);
            if (speed < 0.0F) {
                speed = -speed;
                for (float &f : way.v)
                    f = -f;
            }
            const float n = std::sqrt(way.v[0] * way.v[0] + way.v[1] * way.v[1] + way.v[2] * way.v[2]);
            x.slide = n > 0.0F ? Vec3{way.v[0] / n, way.v[1] / n, way.v[2] / n} : Vec3{};
            if (const std::uint32_t sheet = x.u32(0x5c0); sheet != 0) { // its sheet's hitback addition (1:348)
                const std::uint32_t bits = m.memory().u32(sheet + 0x1c8);
                float more = 0.0F;
                std::memcpy(&more, &bits, 4);
                speed += more;
            }
            x.slide_speed = speed * 0.1F;
        }
        m.memory().set_u32(hit + 0x18, m.memory().u32(hit + 0x18) | 1u);
    };
    calls.set(2, 3, [slide](ai::Machine &m, std::span<ai::Value> a) {
        slide(*self_of(m, a[0]), m, a[1].bits, a[2].f());
    });
    calls.set(2, 87, [slide](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec way = read_vec(m, a[1].bits);
        const Vec3 f = facing_of(x->yaw());
        const bool front = way.v[0] * f[0] + way.v[1] * f[1] + way.v[2] * f[2] < 0.0F;
        const float sign = front ? -1.0F : 1.0F;
        if (way.v[0] != 0.0F || way.v[2] != 0.0F)
            x->set_yaw(std::atan2(way.v[0] * sign, way.v[2] * sign));
        x->play(front ? 23 : 24, 0.0F, 0.0F);
        x->motion.queued.push_back({0, 0.0F});
        slide(*x, m, a[1].bits, a[2].f());
    });
    // 2:2 a small hit (trap_enemy_exec_damage_small, FUN_140431c40 -> FUN_1403d4260): off the ground (FUN_1403ba9a0,
    // 1:60's test) it turns as 2:87 does and plays 14 from the front, 15 from behind (FUN_1403d3840; the table at
    // 0x1405c63f8), its idle after -- its air class's push (FUN_1403d4800) the port's classes do not have; on the
    // ground it plays, unturned, 10 from the front and 12 from behind, 11 and 13 for a hit whose kind (+ 0x24) is not
    // 0 nor 2 to 5 (FUN_1403d43b0; the table at 0x14074c390), its idle after, then slides as 2:3 does. The other of a
    // pair when its set lacks the one asked; the hit's Flag bit 0 set. The Undead Pirates' hits
    calls.set(2, 2, [slide](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const std::uint32_t hit = a[1].bits;
        const Vec way = read_vec(m, hit);
        const Vec3 f = facing_of(x->yaw());
        const bool front = way.v[0] * f[0] + way.v[1] * f[1] + way.v[2] * f[2] < 0.0F;
        const auto own = [x](int motion) {
            return x->motions != nullptr && !x->motions->has(motion) ? motion ^ 1 : motion;
        };
        const bool air = (x->u32(field::kStatus) & 0x1000020u) != 0 || (x->u32(field::kFlags) & bit::kInAir) != 0;
        if (air) {
            const float sign = front ? -1.0F : 1.0F;
            if (way.v[0] != 0.0F || way.v[2] != 0.0F)
                x->set_yaw(std::atan2(way.v[0] * sign, way.v[2] * sign));
            x->play(own(front ? 14 : 15), 0.0F, 0.0F);
            x->motion.queued.push_back({0, 0.0F});
            m.memory().set_u32(hit + 0x18, m.memory().u32(hit + 0x18) | 1u);
            return;
        }
        std::uint8_t kind = 0;
        m.memory().read(hit + 0x24, &kind, 1);
        const bool first = kind == 0 || (kind >= 2 && kind <= 5);
        x->play(own((front ? 10 : 12) + (first ? 0 : 1)), 0.0F, 0.0F);
        x->motion.queued.push_back({0, 0.0F});
        slide(*x, m, hit, a[2].f());
    });
    // 2:86 a hit's push back (trap_damage_get_hitback, FUN_1404321a0 -> FUN_1403ceab0; YS::DAMAGE::get_hitback in the
    // PS3 build): by its first reaction's kind (+ 0x27, as 2:91 sorts it) KH2's battle constants' (03system.bin's
    // pref "sstm", World::system_value) + 0x30 -- + 0x34 for kind 3 -- then its attack's own (atkp's + 0xc, a short)
    // unless that is 0x7fff; half that for a reaction (+ 0x26) of 12 or 13. Kinds 4 and 5 (a blow's and a pull's
    // length) are none of the port's hits: a Days hit's first reaction is 3. The Undead Pirates slide by it (2:3)
    calls.set(2, 86, [](ai::Machine &m, std::span<ai::Value> a) {
        const std::uint32_t hit = a[0].bits;
        std::uint8_t first = 0, reaction = 0;
        m.memory().read(hit + 0x27, &first, 1);
        m.memory().read(hit + 0x26, &reaction, 1);
        const bool third = first == 3 || first == 6 || first == 8 || first == 10 || first == 13 || first == 16 ||
                           first == 18;
        World *w = running(m)->world;
        float back = w != nullptr ? w->system_value(third ? 0x34u : 0x30u, 200.0F) : 200.0F;
        if (const std::uint32_t record = m.memory().u32(hit + 0x20); record != 0) {
            std::uint8_t two[2] = {};
            m.memory().read(record + 0xc, two, 2);
            const std::int16_t own = std::int16_t(two[0] | (two[1] << 8));
            if (own != 0x7fff)
                back = float(own);
        }
        if (reaction == 12 || reaction == 13)
            back *= 0.5F;
        a[0] = ai::Value::of_float(back);
    });
    // 2:64 a hit's first reaction (trap_damage_orig_reaction, FUN_140431f40: YS::DAMAGE's OrigReaction, + 0x27);
    // 2:91 a reaction kind's kind (trap_damage_get_reaction_type, FUN_1404321d0 -> FUN_1403ce980, of the value given:
    // 1 and 12 are 1; 3, 6, 8, 10, 13, 16, 18 are 3; 4, 7, 9, 11, 15, 17 are 4; the others themselves)
    calls.set(2, 64, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint8_t first = 0;
        m.memory().read(a[0].bits + 0x27, &first, 1);
        a[0] = ai::Value::of_int(first);
    });
    calls.set(2, 91, [](ai::Machine &, std::span<ai::Value> a) {
        const int k = std::int32_t(a[0].bits);
        int kind = k;
        if (k == 1 || k == 12)
            kind = 1;
        else if (k == 3 || k == 6 || k == 8 || k == 10 || k == 13 || k == 16 || k == 18)
            kind = 3;
        else if (k == 4 || k == 7 || k == 9 || k == 11 || k == 15 || k == 17)
            kind = 4;
        a[0] = ai::Value::of_int(kind);
    });
    // 2:85 a hit's element (trap_damage_element, FUN_140432170: its attack's record, + 0x20, its byte + 9 -- atkp's
    // element: 0 physical, 1 fire, 2 blizzard, 3 thunder ...): the Volcanic Lord slides and changes state on Blizzard
    calls.set(2, 85, [](ai::Machine &m, std::span<ai::Value> a) {
        const std::uint32_t record = m.memory().u32(a[0].bits + 0x20);
        std::uint8_t element = 0;
        if (record != 0)
            m.memory().read(record + 9, &element, 1);
        a[0] = ai::Value::of_int(element);
    });
    // 1:311 its rate for an element (trap_sheet_set_element_rate, FUN_140431200 -> FUN_1403c2650: + 0x1a8 + element
    // its sheet's weakness to it times the rate, in percent): the share of Days's hits of that element it takes (the
    // Volcanic Lord's fire 0)
    calls.set(1, 311, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const std::uint32_t element = a[1].bits;
        if (element < x->element_rate.size())
            x->element_rate[element] = std::max(0, std::int32_t(a[2].bits));
    });
    // 2:6 whether an object is in one of its reaction motions, 10 to 39 (FUN_140431d50 -> FUN_1403d4880: a motion
    // controller at + 0x158, + 0x180 less 10 under 30): Crimson Jazz asks it of itself before it acts
    calls.set(2, 6, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        Actor *x = o != nullptr ? o->actor() : nullptr;
        const bool reacting = x != nullptr && x->motions != nullptr && x->u32(field::kMotion) - 10u < 30u;
        a[0] = ai::Value::of_int(reacting ? 1 : 0);
    });
    // 1:100 the ticks a jump takes to rise `h` (trap_obj_jump_height_to_uptime; FUN_1403b73f0): sqrt(|2 h / 0.408163|),
    // KH2's pull's
    calls.set(1, 100, [](ai::Machine &, std::span<ai::Value> a) {
        a[0] = ai::Value::of_float(std::sqrt(std::fabs(2.0F * a[0].f() / 0.408163F)));
    });
    // 1:63 whether its motion has reached its loop with no blend left (trap_obj_is_motion_in_loop, FUN_14042f120 ->
    // FUN_1403c7e70 on + 0x158: its time + 0x44 against its motion's loop point, its blend + 0x48 run out) -- a motion
    // that does not loop its end. Data Roxas's slide waits for its 162's loop (frame 6 of 8); read as its end, a loop
    // never reached it
    calls.set(1, 63, [](ai::Machine &m, std::span<ai::Value> a) {
        const Actor *x = self_of(m, a[0]);
        const bool in_loop = x->motions != nullptr && x->motion.id >= 0 && x->motions->has(x->motion.id) &&
                             x->motion.blend <= 0.0F && x->motion.time >= x->motions->loop_from(x->motion.id);
        a[0] = ai::Value::of_int(in_loop ? 1 : 0);
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
    // collision stopping it (FUN_1403b9ac0: Days's collision puts it back out of the walls after).
    // KH2's arenas are larger than Days's rooms: a point a script finds far off lies beyond the room's walls (Final
    // Xemnas's warp, `mode_bomb`: 18 m behind the player -- he stood outside the coliseum, behind its gate). A KH2
    // actor put so stops short of the first wall between the player it goes for and the point, swept at the higher of
    // the two (so a leap onto a rock still lands on it), as a step does
    const auto put = [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v = read_vec(m, a[1].bits);
        for (float &c : v.v)
            if (std::isnan(c))
                c = 0.0F;
        if (Object *o = object_of(m, a[0])) {
            Vec3 to = xyz(v);
            Actor *x = dynamic_cast<Actor *>(o);
            World *w = running(m)->world;
            if (x != nullptr && w != nullptr && !is_player(o)) {
                const Object *p = w->player(*x);
                const Vec3 from = p != nullptr ? p->place() : x->place();
                Vec3 way = {to[0] - from[0], 0.0F, to[2] - from[2]};
                // (KH2's y down: the higher one the less)
                if (w->wall_sweep(*x, {from[0], std::min(from[1], to[1]), from[2]}, 0.0F, way, false)) {
                    to[0] = from[0] + way[0];
                    to[2] = from[2] + way[2];
                }
            }
            o->set_place(to);
            if (is_player(o) && w != nullptr)
                w->player_place(*o, to);
        }
    };
    calls.set(1, 148, put);
    // 1:153 the same, its place before too (trap_obj_reset_pos; FUN_14042b880 -> FUN_1403b6100): no travel between
    calls.set(1, 153, put);
    // 1:217 the same, + 0x9b8 bit 12 set after (trap_obj_set_pos_trans, FUN_14042b900 -> FUN_1403b6510: 1:148's put
    // marked moved at once -- Scar's leaps across the rock)
    calls.set(1, 217, [put](ai::Machine &m, std::span<ai::Value> a) {
        put(m, a);
        if (Object *o = object_of(m, a[0]))
            o->set_bits(field::kFade, 1u << 12, true);
    });
    // KH2 sweeps its collision along the step and stops it at a wall (FUN_1403b9ac0); the port sweeps its body's sphere
    // (its Days sphere's) along it against the room's walls and stops it short of the first: Days's collision pushes
    // back only one standing in a wall, so a step through one left it outside (Roxas's 2.5 m before his hold, 1:18, out
    // of Days's arena)
    const auto step_to_space = [](ai::Machine &m, Actor *x, float length) {
        const Vec3 at = x->place(), f = facing_of(x->yaw());
        if (!(length > 0.0F))
            return at;
        Vec3 way = {f[0] * length, 0.0F, f[2] * length};
        if (World *w = running(m)->world)
            w->wall_sweep(*x, at, 0.0F, way, false);
        return Vec3{at[0] + way[0], at[1], at[2] + way[2]};
    };
    calls.set(1, 220, [step_to_space](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        x->set_place(step_to_space(m, x, a[1].f()));
    });
    // 1:367 where that step would put it, given back (trap_obj_get_move_to_space_pos, FUN_14042bb40 -> FUN_1403b8510:
    // the record's object, the length, a flag): the Lingering Will's 12 m dash's end
    calls.set(1, 367, [step_to_space](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec3 p = step_to_space(m, x, a[1].f());
        Vec v;
        v.v[0] = p[0];
        v.v[1] = p[1];
        v.v[2] = p[2];
        v.v[3] = 1.0F;
        a[0] = give_vec(m, "shared", v);
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
    // 1:58 flying (trap_obj_fly) at a velocity of its own (FUN_140421ce0: + 0x20 the vector made one long times its w,
    // + 0x108 the value, class 5), NaN components 0. 1:73 hovering (trap_obj_hover, FUN_14042d010 -> FUN_140422950:
    // the same, class 6 -- YS::ACTION_HOVER moves by ACTION_FLY's calc_movement, its other virtuals ACTION's as
    // FLY's are): Saïx's claymore
    const auto fly = [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v = read_vec(m, a[1].bits);
        for (float &c : v.v)
            if (std::isnan(c))
                c = 0.0F;
        if (Object *o = object_of(m, a[0]); is_player(o)) { // one of Days's players: the world steers him (Final
            // Xemnas's throw holds the player still so)
            const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
            if (running(m)->world != nullptr)
                running(m)->world->player_steer(*o, n > 0.0F ? Vec3{v.v[0] / n, v.v[1] / n, v.v[2] / n} : Vec3{},
                                                n > 0.0F ? v.v[3] : 0.0F);
            return;
        }
        Actor *x = self_of(m, a[0]);
        const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        x->velocity = n > 0.0F ? Vec3{v.v[0] / n * v.v[3], v.v[1] / n * v.v[3], v.v[2] / n * v.v[3]} : Vec3{};
        x->steered = x->velocity;
        x->move_clock = 0.0F;
        x->dash_keep = a[2].f();
        x->dashing = true;
        x->jump.on = false;
    };
    calls.set(1, 58, fly);
    calls.set(1, 73, fly);
    // 1:91 an object's stars (trap_obj_star, FUN_14042f9c0 -> FUN_1403d7e40: its + 0xd70; the PS3 build's
    // YS::PARTY::star, a party member's dizziness): Saïx's claymore leaves its wielder 30 so. A Days player has none
    calls.set(1, 91, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (o != nullptr && !is_player(o))
            o->set_u32(0xd70, a[1].bits);
    });
    // 1:83 a point given on a bone (0x4000 the actor itself) in the world (FUN_1403b50f0 the bone's matrix, then the
    // point through it; NaN components 0): the shared vector. On a player (Xigbar's scope aims at the player's bone
    // 140), whose KH2 pose the port has not -- Days's player is no KH2 actor --, his aim point (his middle): it was the
    // running actor's own bone before, Xigbar's scope aiming at his own chest
    calls.set(1, 83, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *o = object_of(m, a[0]); o != nullptr && o->actor() == nullptr) {
            Vec3 out = o->place();
            if (World *w = running(m)->world; w != nullptr && std::int32_t(a[1].bits) != 0x4000)
                w->aim_point(*o, out);
            Vec r;
            r.v[0] = out[0], r.v[1] = out[1], r.v[2] = out[2], r.v[3] = 1.0F;
            a[0] = give_vec(m, "1:83", r);
            return;
        }
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
    // 1:177 a walk path of its battle by number, -1 the first (trap_obj_set_path, FUN_14042d8e0 -> FUN_1403b9de0:
    // its unit's paths, + 0x9e8, FUN_1403ff460; a path follower made at + 0x630), 1 when it has it, 0 when not: the
    // kind's paths (the mod's list: its battle set's in KH2's area, placed as its companions, World::walk_path); KH2's
    // player's own paths, none here. 5:14 its path's point n, n past the last wrapping round (Ti::trap_get_path_point_pos,
    // FUN_140557dc0 -> FUN_140550240: n mod the count; w 1); without a path KH2 gives back nothing of its own, the
    // port its place. Shenzi, Banzai and Ed run round theirs
    calls.set(1, 177, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        x->path.clear();
        const bool found = !is_player(object_of(m, a[0])) && x->world != nullptr &&
                           x->world->walk_path(*x, std::int32_t(a[1].bits), x->path);
        a[0] = ai::Value::of_int(found ? 1 : 0);
    });
    calls.set(5, 14, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        const Actor *x = o != nullptr ? o->actor() : nullptr;
        Vec v;
        if (x != nullptr && !x->path.empty()) {
            const std::int64_t n = std::int64_t(x->path.size());
            const Vec3 &p = x->path[std::size_t(((std::int64_t(std::int32_t(a[1].bits)) % n) + n) % n)];
            v.v[0] = p[0], v.v[1] = p[1], v.v[2] = p[2];
        } else if (o != nullptr) {
            const Vec3 p = o->place();
            v.v[0] = p[0], v.v[1] = p[1], v.v[2] = p[2];
        }
        v.v[3] = 1.0F;
        a[0] = give_vec(m, "5:14", v);
    });
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
    // 6:4 a search's list emptied (Ts::TARGET_SEARCHER's Counter, + 8, and its ring's next, + 4's low byte;
    // FUN_14053dd00)
    calls.set(6, 4, [](ai::Machine &m, std::span<ai::Value> a) {
        ai::Memory &mem = m.memory();
        const std::uint32_t s = a[0].bits;
        mem.set_u32(s + 8, 0);
        mem.set_u32(s + 4, mem.u32(s + 4) & ~0xffu);
    });
    // 6:57 a target record's group (trap_target_get_group; FUN_14053e5b0: its + 4)
    calls.set(6, 57, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_int(std::int32_t(m.memory().u32(a[0].bits + 4)));
    });
    // 2:4 the enemy after one, the first after none (trap_enemy_each; FUN_140431ce0 -> FUN_1403db9d0): KH2's actors
    // of the field in the world's order, 0 past the last
    calls.set(2, 4, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = running(m);
        std::uint32_t next = 0;
        if (me->world != nullptr) {
            const std::vector<Actor *> all = me->world->actors();
            auto it = all.begin();
            if (a[0].bits != 0) {
                it = std::find_if(all.begin(), all.end(), [&](Actor *x) { return x->address() == a[0].bits; });
                if (it != all.end())
                    ++it;
            }
            if (it != all.end())
                next = (*it)->address();
        }
        a[0] = ai::Value::of_address(next);
    });
    // 1:292 the actor whose unit argument n is a value, into an AI handle's + 4 (trap_obj_search_by_unit_arg;
    // FUN_14042de70: the field's objects, FUN_1403bf4f0, by FUN_1403b5df0), 0 when none is
    calls.set(1, 292, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = running(m);
        std::uint32_t found = 0;
        if (me->world != nullptr)
            for (Actor *x : me->world->objects())
                if (x->u32(field::kUnitArgs + 4 * a[1].bits) == a[2].bits) {
                    found = x->address();
                    break;
                }
        m.memory().set_u32(a[0].bits + 4, found);
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
    // 6:11 a target record made the aim of one object nearest a point, to its surface (Ts::trap_target_searcher_search_
    // obj, FUN_14053e460 -> FUN_14053ff10: the object an AI handle's + 4 names, its shapes of kind 3 in its groups on,
    // the point's distance less the shape's radius; the record emptied first, FUN_1403bda50, and a NaN of the point
    // taken as 0): the Lingering Will's B_EX430
    calls.set(6, 11, [](ai::Machine &m, std::span<ai::Value> a) {
        ai::Memory &mem = m.memory();
        const std::uint32_t out = a[0].bits;
        Vec from = read_vec(m, a[2].bits);
        for (float &f : from.v)
            if (std::isnan(f))
                f = 0.0F;
        mem.set_u32(out, 0);
        mem.set_u32(out + 4, 0);
        mem.set_u32(out + 8, 0);
        mem.set_u32(out + 12, 0xffffffffu);
        const std::uint32_t named = a[1].bits != 0 ? mem.u32(a[1].bits + 4) : 0;
        Object *o = named != 0 ? Object::at(mem, named) : nullptr;
        Actor *x = o != nullptr ? o->actor() : nullptr;
        if (x == nullptr || x->world == nullptr)
            return;
        bool found = false;
        float best = 0.0F;
        for (const auto &sh : x->world->shapes(*x, 3)) {
            const float d[3] = {sh.at[0] - from.v[0], sh.at[1] - from.v[1], sh.at[2] - from.v[2]};
            const float gap = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - sh.radius;
            if (!found || gap < best) {
                found = true;
                best = gap;
                mem.set_u32(out, x->address());
                mem.set_u32(out + 4, std::uint32_t(sh.group));
            }
        }
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

// The scripts' shots: effects they move themselves, their attacks' state, the vectors they move them by
void add_shots(ai::Calls &calls)
{
    const auto world = [](ai::Machine &m) { return running(m)->world; };
    const auto unit = [](Vec v) {
        const float n = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        if (n > 0.0F)
            for (int i = 0; i < 3; ++i)
                v.v[i] /= n;
        return v;
    };
    // 0:26 nothing in the PC game (FUN_14041ca40 is empty)
    calls.set(0, 26, [](ai::Machine &, std::span<ai::Value>) {});
    // 0:43 an angle made one of (-pi, pi] (FUN_14041e5e0 -> FUN_1403b6f20; not finite: 0)
    calls.set(0, 43, [](ai::Machine &, std::span<ai::Value> a) {
        const float v = a[0].f();
        a[0] = ai::Value::of_float(std::isfinite(v) ? wrapped(v) : 0.0F);
    });
    // 0:45 a point moved along a way (made one long) by a speed, a tick's worth (FUN_14041d2d0: x DAT_140717480)
    calls.set(0, 45, [unit](ai::Machine &m, std::span<ai::Value> a) {
        Vec p = read_vec(m, a[0].bits);
        const Vec d = unit(read_vec(m, a[1].bits));
        for (int i = 0; i < 3; ++i)
            p.v[i] += d.v[i] * a[2].f() * m.tick_length;
        m.memory().write(a[0].bits, p.v, sizeof p.v);
    });
    // 0:46 a way turned toward another by at most an angle a tick, made one long (FUN_14041d3d0 -> FUN_1403b7200)
    calls.set(0, 46, [unit](ai::Machine &m, std::span<ai::Value> a) {
        const Vec from = unit(read_vec(m, a[0].bits)), to = unit(read_vec(m, a[1].bits));
        const float cosine = std::clamp(from.v[0] * to.v[0] + from.v[1] * to.v[1] + from.v[2] * to.v[2], -1.0F, 1.0F);
        const float angle = std::acos(cosine), step = a[2].f() * m.tick_length;
        Vec out = to;
        if (angle > step && angle > 1e-6F) {
            const float f = step / angle, s = std::sin(angle);
            const float w0 = std::sin((1.0F - f) * angle) / s, w1 = std::sin(f * angle) / s;
            for (int i = 0; i < 3; ++i)
                out.v[i] = from.v[i] * w0 + to.v[i] * w1;
        }
        out.v[3] = 1.0F;
        m.memory().write(a[0].bits, out.v, sizeof out.v);
    });
    // 0:48 0:47's memory let go (FUN_14041e660 -> FUN_140152570)
    calls.set(0, 48, [](ai::Machine &m, std::span<ai::Value> a) { m.memory().remove(a[0].bits); });
    // 0:50 whether an effect still plays; 0:51 one ended at once; 0:52 ended over n ticks (0 or less at once); 0:53
    // its place; 0:38 its place set; 0:41 its turn set from a way (FUN_1403b7710: about the x atan2(-y, along the
    // ground), about the y atan2(x, z))
    calls.set(0, 50, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        a[0] = ai::Value::of_int(w != nullptr && w->effect_alive(a[0].bits) ? 1 : 0);
    });
    calls.set(0, 51, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->stop_effect(a[0].bits);
    });
    calls.set(0, 52, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m)) {
            if (a[1].f() > 0.0F)
                w->effect_fade(a[0].bits, a[1].f());
            else
                w->stop_effect(a[0].bits);
        }
    });
    calls.set(0, 53, [world](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        Vec3 at{};
        if (World *w = world(m); w != nullptr && w->effect_where(a[0].bits, at))
            v.v[0] = at[0], v.v[1] = at[1], v.v[2] = at[2];
        v.v[3] = 1.0F;
        a[0] = give_vec(m, "0:53", v);
    });
    calls.set(0, 38, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->effect_move(a[0].bits, xyz(read_vec(m, a[1].bits)));
    });
    calls.set(0, 41, [world](ai::Machine &m, std::span<ai::Value> a) {
        const Vec d = read_vec(m, a[1].bits);
        const float along = std::sqrt(d.v[0] * d.v[0] + d.v[2] * d.v[2]);
        if (World *w = world(m))
            w->effect_turn(a[0].bits, {std::atan2(-d.v[1], along), std::atan2(d.v[0], d.v[2]), 0.0F});
    });
    // 0:40 its turn set itself (trap_effect_set_rot, FUN_14041e080: + 0x10 of its transform, FUN_1402be1b0: angles
    // about x, y and z -- the Blizzard Lord's ice spikes each a turn at random); 0:39 its size (trap_effect_set_scale,
    // FUN_14041e000: + 0x20); 0:67 its colour (trap_effect_set_color, FUN_14041e520: + 0x30, 1 each as it is -- Dark
    // Thorn's fades out by its alpha)
    calls.set(0, 40, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->effect_turn(a[0].bits, xyz(read_vec(m, a[1].bits)));
    });
    calls.set(0, 39, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->effect_scale(a[0].bits, xyz(read_vec(m, a[1].bits)));
    });
    calls.set(0, 67, [world](ai::Machine &m, std::span<ai::Value> a) {
        const Vec c = read_vec(m, a[1].bits);
        if (World *w = world(m))
            w->effect_colour(a[0].bits, {c.v[0], c.v[1], c.v[2], c.v[3]});
    });
    // 0:49 whether an effect is still alive (trap_effect_is_alive, FUN_14041e1f0 -> FUN_1402be790: its + 0xe bit 0,
    // its end, clear) -- 0:50 asks whether its instance is there (+ 0x30); both are whether it still plays here
    calls.set(0, 49, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        a[0] = ai::Value::of_int(w != nullptr && w->effect_alive(a[0].bits) ? 1 : 0);
    });
    // 1:21 an effect on the actor, its handle (FUN_14042d440 -> FUN_1403b6630: FUN_1402c6b30, as 1:87)
    calls.set(1, 21, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_address(
            x != nullptr && x->world != nullptr
                ? x->world->effect(*x, std::int32_t(a[1].bits), std::int32_t(a[2].bits), std::int32_t(a[3].bits))
                : 0);
    });
    // 2:57 a limit's effect on an object (trap_limit_effect_start, FUN_140432fb0 -> FUN_1403d9b60: FUN_1402c6b30 on the
    // object's + 0x18, its element and value, 1, 30000), its handle: as 1:21 on that object when it is a KH2 one, else
    // none (Data Roxas's shots ask it of the object a thread was given, else 1:21 on himself)
    calls.set(2, 57, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        Actor *x = o != nullptr ? o->actor() : nullptr;
        a[0] = ai::Value::of_address(x != nullptr && x->world != nullptr
                                         ? x->world->effect(*x, std::int32_t(a[1].bits), std::int32_t(a[2].bits), 1)
                                         : 0);
    });
    // 10:52 whether it plays a reflecting motion (trap_btlobj_is_reflect_motion, FUN_1405370d0 -> FUN_1403d48d0: a
    // motion controller, its motion 140 to 150; the PS3 build's BTLOBJ::is_reflect_motion)
    calls.set(10, 52, [](ai::Machine &m, std::span<ai::Value> a) {
        const Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x->motions != nullptr && x->motion.id >= 140 && x->motion.id <= 150 ? 1 : 0);
    });
    // 6:69 the others of its part hidden (Ts::trap_larxene_dead, FUN_14053f180: every object whose entry's part number,
    // + 0x4c, is its own but itself, YS::OBJ's VTABLE hide): their models not drawn
    calls.set(6, 69, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = self_of(m, a[0]);
        const std::uint32_t mine = me->u32(field::kEntry);
        if (me->world == nullptr || mine == 0)
            return;
        const std::uint32_t part = m.memory().u32(mine + 0x4c) & 0xffffu;
        for (Actor *x : me->world->objects())
            if (const std::uint32_t record = x->u32(field::kEntry);
                x != me && record != 0 && (m.memory().u32(record + 0x4c) & 0xffffu) == part)
                x->hidden_parts = ~0u;
    });
    // 10:22 whether an object is blown away (kn::trap_sysobj_is_blow, FUN_140536970: there, its class's + 0xc bit 6 --
    // KH2's blow class, 1:129's and 2:1's): a KH2 one in such a throw; a Days player never (Days's hits are Days's)
    calls.set(10, 22, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        const Actor *x = o != nullptr ? o->actor() : nullptr;
        a[0] = ai::Value::of_int(x != nullptr && x->jump.on && x->jump.blow ? 1 : 0);
    });
    // 1:169 an effect let go of its object (trap_obj_effect_unbind, FUN_14042d650: the object's bound effect of that
    // handle, + 0x98's list, its flag cleared and its binding gone): World::effect_unbind. Roxas's keyblades leave their
    // trails where they were
    calls.set(1, 169, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]); x->world != nullptr)
            x->world->effect_unbind(a[1].bits);
    });
    // 8:2 an object turned about all three axes (ryj::trap_obj_set_xyzrot, FUN_140537e70 -> FUN_1403b65f0: the turns,
    // NaNs 0, at + 0x680, their y its facing's w, + 0x9b8 bit 3): Roxas's keyblades
    calls.set(8, 2, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        Vec v = read_vec(m, a[1].bits);
        for (float &c : v.v)
            if (std::isnan(c))
                c = 0.0F;
        x->set_yaw(v.v[1]);
        for (std::uint32_t i = 0; i < 4; ++i)
            x->set_f(field::kFacing + 4 * i, v.v[i]);
        x->set_f(field::kFacing + 12, v.v[1]);
        x->set_bits(0x9b8, 8u, true);
        x->turned_all = true;
        x->turn_all = {v.v[0], v.v[1], v.v[2]};
    });
    // 1:22 one at a place of the world, its handle (FUN_14042d4b0 -> FUN_1403b66c0: its place then the one given)
    calls.set(1, 22, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_address(
            x != nullptr && x->world != nullptr
                ? x->world->effect_in_world(*x, std::int32_t(a[1].bits), xyz(read_vec(m, a[2].bits)))
                : 0);
    });
    // 1:88 an object's aim point (FUN_14042f880: its record's point, the shared vector)
    calls.set(1, 88, [world](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        Vec3 at{};
        Object *o = object_of(m, a[0]);
        if (World *w = world(m); w != nullptr && o != nullptr && w->aim_point(*o, at))
            v.v[0] = at[0], v.v[1] = at[1], v.v[2] = at[2];
        else if (o != nullptr) {
            const Vec3 p = o->place();
            v.v[0] = p[0], v.v[1] = p[1], v.v[2] = p[2];
        }
        v.v[3] = 1.0F;
        a[0] = give_vec(m, "shared", v);
    });
    // 1:93 a member of the party: 0 KH2's player (DAT_142a105d0, 1:3's), n the n-th partner (FUN_1403c3270): Days's
    // field has none
    calls.set(1, 93, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        Object *p = a[0].bits == 0 && w != nullptr ? w->player(*running(m)) : nullptr;
        a[0] = ai::Value::of_address(p != nullptr ? p->address() : 0);
    });
    // 1:160 a target record made of an object (FUN_140430470 -> FUN_1403bee60: the object, its group, none, -1)
    calls.set(1, 160, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[1]);
        const std::uint32_t record[4] = {o != nullptr ? o->address() : 0u, 0u, 0u, 0xffffffffu};
        m.memory().write(a[0].bits, record, sizeof record);
    });
    // 2:14, 2:29, 2:39, 2:44, 2:43, 2:18, 2:19: an attack's state (World::attack_finished and on)
    calls.set(2, 14, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        a[0] = ai::Value::of_int(w == nullptr || w->attack_finished(a[0].bits) ? 1 : 0);
    });
    calls.set(2, 29, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        a[0] = ai::Value::of_int(w == nullptr || w->attack_expired(a[0].bits) ? 1 : 0);
    });
    calls.set(2, 39, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        a[0] = ai::Value::of_int(w != nullptr && w->attack_landed(a[0].bits) ? 1 : 0);
    });
    // 2:41 whether it met the floor (trap_attack_is_hit_floor, FUN_140432620: + 8 bit 4): Hades's fire shots end on it
    calls.set(2, 41, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        a[0] = ai::Value::of_int(w != nullptr && w->attack_on_floor(a[0].bits) ? 1 : 0);
    });
    // 2:42 where it met the room (trap_attack_hit_bg_pos, FUN_140432650: its + 0xb4's address, a point the world writes
    // when it meets the floor): the Prison Keeper's shots
    calls.set(2, 42, [](ai::Machine &, std::span<ai::Value> a) {
        a[0] = ai::Value::of_address(a[0].bits != 0 ? a[0].bits + 0xb4 : 0);
    });
    // 2:51 a reaction command put on one of its attacks (trap_attack_set_rc, FUN_140432790 -> FUN_140437b70; the PS3
    // build's YS::ATTACK_MAGIC::set_reaction_command(void *, int, float, float)): + 0x114 what it is given (its
    // receiver, 2:52's: the Hydra's urn, a block of Xigbar's shot's thread), + 0x118 the attack's owner (the one the
    // command goes to: 7:34 sets another), + 0x11c the command, + 0x120 and + 0x124 two sizes (the second, 0 or less,
    // the first): kept as KH2 keeps them; offered as the PS3 build's RC_SEARCHER::check_magic offers it (shell.cpp)
    calls.set(2, 51, [](ai::Machine &m, std::span<ai::Value> a) {
        const std::uint32_t at = a[0].bits;
        if (at == 0)
            return;
        m.memory().set_u32(at + 0x114, a[1].bits);
        m.memory().set_u32(at + 0x118, running(m)->address());
        m.memory().set_u32(at + 0x11c, a[2].bits);
        m.memory().set_u32(at + 0x120, a[3].bits);
        m.memory().set_u32(at + 0x124, a[4].f() <= 0.0F ? a[3].bits : a[4].bits);
    });
    // 2:52 what an attack's reaction command was given (trap_attack_rc_receiver, FUN_140432800: its + 0x114); 7:34 the
    // object a command on an attack goes to (Ts::trap_attack_set_rc_owner, FUN_14053fac0: the AI handle's object at
    // + 0x118); 1:166 the attack a command's target record names (trap_target_attack, FUN_140430510: the record's + 8,
    // TARGET::set_attack's). Xigbar's shots: the command taken sends the shot back to him
    calls.set(2, 52, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_address(a[0].bits != 0 ? m.memory().u32(a[0].bits + 0x114) : 0u);
    });
    calls.set(7, 34, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[1]);
        if (a[0].bits != 0 && o != nullptr)
            m.memory().set_u32(a[0].bits + 0x118, o->address());
    });
    calls.set(1, 166, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_address(a[0].bits != 0 ? m.memory().u32(a[0].bits + 8) : 0u);
    });
    // 2:24 an attack's owner (trap_attack_get_owner, FUN_140432ad0: its + 0x10's handle)
    calls.set(2, 24, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        Object *owner = w != nullptr ? w->attack_owner(a[0].bits) : nullptr;
        a[0] = ai::Value::of_address(owner != nullptr ? owner->address() : 0);
    });
    calls.set(2, 44, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        a[0] = ai::Value::of_address(w != nullptr ? w->attack_struck(a[0].bits) : 0);
    });
    calls.set(2, 43, [world](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        Vec3 way{};
        if (World *w = world(m); w != nullptr && w->attack_way(a[0].bits, way))
            v.v[0] = way[0], v.v[1] = way[1], v.v[2] = way[2];
        v.v[3] = 1.0F;
        a[0] = give_vec(m, "2:43", v);
    });
    calls.set(2, 18, [world](ai::Machine &m, std::span<ai::Value> a) {
        World *w = world(m);
        a[0] = ai::Value::of_int(w != nullptr && w->attack_can_hit(a[0].bits, object_of(m, a[1])) ? 1 : 0);
    });
    calls.set(2, 19, [world](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = world(m))
            w->attack_line(a[0].bits, xyz(read_vec(m, a[1].bits)), xyz(read_vec(m, a[2].bits)));
    });
    // 1:141 the ground under a point: a cast from it down a depth (FUN_14042e8b0 -> FUN_1403f3dd0, flags 4), the
    // point it meets written (else the point itself), whether it meets one
    calls.set(1, 141, [world](ai::Machine &m, std::span<ai::Value> a) {
        const Vec3 from = xyz(read_vec(m, a[0].bits));
        Vec3 hit = from;
        World *w = world(m);
        const bool met = w != nullptr && w->ray(*running(m), from, {from[0], from[1] + a[2].f(), from[2]}, hit);
        Vec out;
        out.v[0] = met ? hit[0] : from[0], out.v[1] = met ? hit[1] : from[1], out.v[2] = met ? hit[2] : from[2];
        out.v[3] = 1.0F;
        m.memory().write(a[1].bits, out.v, sizeof out.v);
        a[0] = ai::Value::of_int(met ? 1 : 0);
    });
    // 6:51 whether a segment meets the room (FUN_14053eb70 -> FUN_14040c2b0: a YS::BGHIT, from, to, flags): the point
    // it meets at the record's + 0 (CrossPos), its polygon at + 0x14 (-1 none; Days's rooms number none KH2 knows: 0),
    // the object whose collision it is at + 0x18 (0: the room)
    calls.set(6, 51, [world](ai::Machine &m, std::span<ai::Value> a) {
        const Vec3 from = xyz(read_vec(m, a[1].bits)), to = xyz(read_vec(m, a[2].bits));
        Vec3 hit{};
        World *w = world(m);
        const bool met = w != nullptr && w->ray(*running(m), from, to, hit);
        const std::uint32_t record = a[0].bits;
        bghit_segments()[record] = {from, to}; // (6:52's)
        m.memory().set_u32(record + 0x14, met ? 0u : 0xffffffffu);
        if (met) {
            const float p[4] = {hit[0], hit[1], hit[2], 1.0F};
            m.memory().write(record, p, sizeof p);
            m.memory().set_u32(record + 0x18, 0);
            m.memory().set_u32(record + 0x1c, 0);
        }
        a[0] = ai::Value::of_int(met ? 1 : 0);
    });
    // 6:52 the normal of the polygon a BGHIT met (trap_bghit_get_normal, FUN_14053ec80 -> FUN_14040c3c0: the room's or
    // an object's polygon's): Days's rooms give the port no polygon, so two more segments beside the first, a few
    // centimetres apart across its way, meet the same surface and the three points give it, turned against the way
    // (the way back when they miss). Data Roxas reads it of what his shots meet
    calls.set(6, 52, [world](ai::Machine &m, std::span<ai::Value> a) {
        Vec out;
        World *w = world(m);
        const auto seg = bghit_segments().find(a[0].bits);
        if (w != nullptr && seg != bghit_segments().end()) {
            const Vec3 from = seg->second.first, to = seg->second.second;
            Vec3 way = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
            const float n = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
            if (n > 0.0F) {
                for (float &c : way)
                    c /= n;
                // two ways across it
                const Vec3 up = std::fabs(way[1]) < 0.9F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
                const auto cross = [](const Vec3 &p, const Vec3 &q) {
                    return Vec3{p[1] * q[2] - p[2] * q[1], p[2] * q[0] - p[0] * q[2], p[0] * q[1] - p[1] * q[0]};
                };
                const auto unit = [](Vec3 v) {
                    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    if (l > 0.0F)
                        for (float &c : v)
                            c /= l;
                    return v;
                };
                const Vec3 across = unit(cross(way, up)), other = unit(cross(way, across));
                Vec3 p0{}, p1{}, p2{};
                const auto cast = [&](const Vec3 &side, Vec3 &hit) {
                    constexpr float kApart = 5.0F; // cm
                    const Vec3 f = {from[0] + side[0] * kApart, from[1] + side[1] * kApart, from[2] + side[2] * kApart};
                    const Vec3 t = {to[0] + side[0] * kApart, to[1] + side[1] * kApart, to[2] + side[2] * kApart};
                    return w->ray(*running(m), f, t, hit);
                };
                Vec3 normal = {-way[0], -way[1], -way[2]};
                if (cast({0.0F, 0.0F, 0.0F}, p0) && cast(across, p1) && cast(other, p2)) {
                    const Vec3 got = unit(cross({p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]},
                                                {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]}));
                    if (got[0] != 0.0F || got[1] != 0.0F || got[2] != 0.0F) {
                        const bool facing = got[0] * way[0] + got[1] * way[1] + got[2] * way[2] < 0.0F;
                        normal = facing ? got : Vec3{-got[0], -got[1], -got[2]};
                    }
                }
                out.v[0] = normal[0], out.v[1] = normal[1], out.v[2] = normal[2];
            }
        }
        a[0] = give_vec(m, "6:52", out);
    });
    // 6:53 whether a BGHIT met something (its polygon, + 0x14, not -1; FUN_14053ecd0)
    calls.set(6, 53, [](ai::Machine &m, std::span<ai::Value> a) {
        a[0] = ai::Value::of_int(m.memory().u32(a[0].bits + 0x14) != 0xffffffffu ? 1 : 0);
    });
    // 6:55 the kind of the polygon a BGHIT met (FUN_14053ed40 -> FUN_14040c380: its attributes' low four bits, the
    // room's or an object's; dk::OctCollFile::getAttr): Days's rooms carry no KH2 attributes, 0 (M_EX540 asks for 1)
    calls.set(6, 55, [](ai::Machine &, std::span<ai::Value> a) { a[0] = ai::Value::of_int(0); });
    // 1:247 a point at random round another, between two distances, on the ground (trap_bg_get_random_pos;
    // FUN_14042ead0 -> FUN_1403f3fa0: the distance at random, the way one of eight sectors in a running order --
    // FUN_1403f3fa0's counter, its bits turned -- and at random within it, KH2's chance twice; FUN_1403f4180's search
    // of the way not read: the way's room stops it, the ground under it found): the shared vector. The way out to it
    // is swept with the actor's body against the room's walls, the point its body short of the first: the Lingering
    // Will's warp beside the player (atk_bazu_warp_near: 4 to 6 m off him) put him outside the coliseum's ring
    calls.set(1, 247, [world](ai::Machine &m, std::span<ai::Value> a) {
        static std::uint32_t sector = 0; // DAT_142ae5d78
        const Vec3 at = xyz(read_vec(m, a[0].bits));
        const float r = float(next_chance()) * 2.32830644e-10F * (a[2].f() - a[1].f()) + a[1].f();
        const std::uint32_t turned = ((sector >> 2) & 1u) | (sector * 2u);
        const float turn = (float(turned) + float(next_chance()) * 2.32830644e-10F) * 0.785398163F;
        sector = (sector + 1u) & 7u;
        Vec3 way = {std::sin(turn) * r, 0.0F, std::cos(turn) * r};
        World *w = world(m);
        if (w != nullptr)
            w->wall_sweep(*running(m), at, 0.0F, way, false);
        Vec3 p = {at[0] + way[0], at[1], at[2] + way[2]}, hit{};
        if (w != nullptr && w->ray(*running(m), {p[0], p[1] - 200.0F, p[2]}, {p[0], p[1] + 1000.0F, p[2]}, hit))
            p = hit;
        Vec v;
        v.v[0] = p[0], v.v[1] = p[1], v.v[2] = p[2], v.v[3] = 1.0F;
        a[0] = give_vec(m, "shared", v);
    });
    // 1:248 a point at random round another, between two distances, in the air (trap_bg_get_random_pos_air;
    // FUN_14042eb90 -> FUN_1403f4080: a turn at random, a distance at random between them), put on the ground under it
    // when one is found within the two heights given (KH2's own search of its eight ways not read): the shared vector
    calls.set(1, 248, [world](ai::Machine &m, std::span<ai::Value> a) {
        const Vec3 at = xyz(read_vec(m, a[0].bits));
        const float turn = float(next_chance()) * 2.32830644e-10F * 2.0F * kPi;
        const float r = (a[2].f() - a[1].f()) * float(next_chance()) * 2.32830644e-10F + a[1].f();
        Vec3 p = {at[0] + std::sin(turn) * r, at[1], at[2] + std::cos(turn) * r};
        Vec3 ground{};
        World *w = world(m);
        const float up = std::max(std::fabs(a[3].f()), std::fabs(a[4].f()));
        if (w != nullptr && up > 0.0F && w->ray(*running(m), {p[0], p[1] - up, p[2]}, {p[0], p[1] + up, p[2]}, ground))
            p = ground;
        Vec v;
        v.v[0] = p[0], v.v[1] = p[1], v.v[2] = p[2], v.v[3] = 1.0F;
        a[0] = give_vec(m, "shared", v);
    });
    // 6:9 a point's distance to what a target record names (FUN_14053e3e0 -> FUN_1403bdd90), a float
    calls.set(6, 9, [world](ai::Machine &m, std::span<ai::Value> a) {
        float d = 0.0F;
        if (World *w = world(m))
            w->record_distance(a[0].bits, xyz(read_vec(m, a[1].bits)), d);
        a[0] = ai::Value::of_float(d);
    });
    // 2:38 a routine of the script run when the attack hits, with a value (FUN_140432560: + 0x98 the machine, + 0x9c
    // the routine, + 0xa0 the value)
    calls.set(2, 38, [world](ai::Machine &m, std::span<ai::Value> a) {
        std::uint32_t entry = a[1].bits;
        if (a[1].tag == ai::Tag::Address && entry >= m.code())
            entry = (entry - m.code()) / 2;
        if (World *w = world(m))
            w->attack_on_hit(a[0].bits, *running(m), entry, a[2].bits);
    });
    // 2:74 a routine of the script run when the attack is reflected, with a value (trap_attack_set_reflect_callback,
    // FUN_140432c40: + 0xa4 the machine, + 0xa8 the routine, + 0xac the value): the Lingering Will's B_EX430
    calls.set(2, 74, [world](ai::Machine &m, std::span<ai::Value> a) {
        std::uint32_t entry = a[1].bits;
        if (a[1].tag == ai::Tag::Address && entry >= m.code())
            entry = (entry - m.code()) / 2;
        if (World *w = world(m))
            w->attack_on_reflect(a[0].bits, *running(m), entry, a[2].bits);
    });
    // 6:54 a vector copied into one of its own (FUN_14053ed00: DAT_1407883d8)
    calls.set(6, 54,
              [](ai::Machine &m, std::span<ai::Value> a) { a[0] = give_vec(m, "6:54", read_vec(m, a[0].bits)); });
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
    // 2:23 the actor's target slot (+ 0xbf8), made the player's (2) while it is empty (FUN_1404319d0)
    calls.set(2, 23, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const std::uint32_t slot = slot_of(*x);
        if (slot_owners().count({&m.memory(), slot}) == 0)
            choose_target(m, slot, x, 2);
        else
            target_in(m, slot); // (its + 0 the player now)
        a[0] = ai::Value::of_address(slot);
    });
    // 1:121 a slot's target chosen for an owner by a mode (FUN_1403be2b0: the player; target_in above)
    calls.set(1, 121, [](ai::Machine &m, std::span<ai::Value> a) {
        choose_target(m, a[0].bits, object_of(m, a[1]), a[2].bits);
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
    // 1:237 the four top commands of the player's menu in place of his own this tick (trap_command_override_top;
    // FUN_1404300a0 -> FUN_1403b2910: the menu's submenus closed, each of its four slots given its command, all sent to
    // the object of the target record given last; renewed each tick, as 1:165's): Roxas's duel turns the four into "?"
    // (104) and, while the player's motion has its range 18 (19 in the data battle), one at random into "The End" (672)
    calls.set(1, 237, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = running(m);
        if (x->world != nullptr)
            x->world->override_top(*x,
                                   {std::int32_t(a[0].bits), std::int32_t(a[1].bits), std::int32_t(a[2].bits),
                                    std::int32_t(a[3].bits)},
                                   object_of(m, a[4]));
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
    // 0:44 an effect's loops closed (trap_effect_loop_end), 0:85 the same and the effect freed once it has ended
    // (trap_effect_loop_end_kill; FUN_14041e1c0 -> FUN_1402be8f0): it ends once its objects have. Data Roxas closes
    // the 13 glows 1:87 put on him as he rolls or waits for his duel this way; never closed, each roll left them going
    // and the screen went white
    const auto loop_end = [](ai::Machine &m, std::span<ai::Value> a) {
        if (running(m)->world != nullptr && a[0].bits != 0)
            running(m)->world->effect_loop_end(a[0].bits);
    };
    calls.set(0, 44, loop_end);
    calls.set(0, 85, loop_end);
    add_search(calls);
    add_attacks(calls);
    add_shots(calls);
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
        if (Object *o = object_of(m, a[0]); o != nullptr && is_player(o)) { // a player: World::player_carry
            if (running(m)->world != nullptr)
                running(m)->world->player_carry(*o, object_of(m, a[1]));
            return;
        }
        Actor *carried = self_of(m, a[0]);
        Object *by = object_of(m, a[1]);
        if (by == nullptr || by == carried)
            return;
        carried->set_u32(field::kCarrier, by->address());
        if (carried->world != nullptr)
            carried->world->carry(*carried, *by, std::int32_t(a[2].bits & 0xffff), std::int32_t(a[3].bits & 0xffff));
    });
    calls.set(1, 102, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *o = object_of(m, a[0]); o != nullptr && is_player(o)) {
            if (running(m)->world != nullptr)
                running(m)->world->player_carry(*o, nullptr);
            return;
        }
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
    // actor): its health -- its battle entry's (the second value: 00battle.bin's enmp, the Soldier's 33, Hades's 19 or
    // 199) in Days's scale (World::battle_entry) -- in it, kept by the shell each frame (World::health)
    // 2:95 a sheet of its own made anew from a battle entry (trap_btlobj_dup_sheet, FUN_140431aa0 -> FUN_1404112b0:
    // the old one let go, FUN_1403c06b0's new one at + 0x5c0, its + 0x268 the actor): Larxene's 216 as she begins
    calls.set(2, 95, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const std::uint32_t record = m.memory().add(0x270, x->name() + "'s health");
        m.memory().set_u32(record + 0x268, x->address());
        x->set_u32(0x5c0, record);
        if (x->world != nullptr) {
            x->world->battle_entry(*x, a[1].i());
            x->world->health(*x);
        }
    });
    calls.set(2, 9, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        if (x->u32(0x5c0) == 0) {
            const std::uint32_t record = m.memory().add(0x270, x->name() + "'s health");
            m.memory().set_u32(record + 0x268, x->address());
            x->set_u32(0x5c0, record);
        }
        if (x->world != nullptr) {
            x->world->battle_entry(*x, a[1].i());
            x->world->health(*x);
        }
    });
    // 2:20 a routine of the script started as a thread of its own, with one value; a handle back
    // (trap_magic_start_thread, FUN_1403c5ec0: a machine of its own on the script, FUN_1403e1970, in the game's list
    // of them, FUN_1403c4e90 -- run apart from its object's ticks: the shell runs every field's loose threads, then
    // tells the shouts; the Sorcerer's turn-giving thread moves its turn on so before its cubes hear the order)
    calls.set(2, 20, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint32_t entry = a[0].bits;
        if (a[0].tag == ai::Tag::Address && entry >= m.code())
            entry = (entry - m.code()) / 2;
        const ai::Value arg = a[1];
        m.start_loose(entry, {&arg, 1});
        static std::map<const ai::Memory *, std::uint32_t> handles;
        auto &h = handles[&m.memory()];
        if (h == 0)
            h = m.memory().add(16, "2:20's threads");
        a[0] = ai::Value::of_address(h);
    });
    // 0:9 a routine of the script started as a thread beside the one running, with one value; a handle back
    // (trap_thread_start, FUN_14041da20 -> FUN_1403e1e30: a machine of its own on the script, in the list of threads
    // of the one running's parent -- the main's own when the main runs -- which FUN_1403e1c80 runs after it; tagged
    // with the first value, + 0x3c, those of its list with that tag ended first, FUN_1403e1f00; the third value at
    // + 0x40): Xemnas's
    calls.set(0, 9, [](ai::Machine &m, std::span<ai::Value> a) {
        std::uint32_t entry = a[1].bits;
        if (a[1].tag == ai::Tag::Address && entry >= m.code())
            entry = (entry - m.code()) / 2;
        ai::Thread *running = m.current();
        ai::Thread *parent = running != nullptr ? running->parent : nullptr;
        if (running != nullptr && parent == nullptr && running != m.main_thread())
            parent = running; // a loose thread's (2:20: a machine of its own): its own list
        const std::uint32_t tag = a[0].bits;
        if (ai::Thread *list = parent != nullptr ? parent : m.main_thread(); list != nullptr && tag != 0)
            for (const auto &c : list->children)
                if ((c->tag & tag) == tag)
                    m.stop(c.get());
        const ai::Value arg = a[3];
        if (ai::Thread *t = m.start_routine(parent, entry, {&arg, 1}))
            t->tag = tag;
        static std::map<const ai::Memory *, std::uint32_t> handles;
        auto &h = handles[&m.memory()];
        if (h == 0)
            h = m.memory().add(16, "0:9's threads");
        a[0] = ai::Value::of_address(h);
    });
    // 10:19 whether the object is a limit's (trap_sysobj_is_limit, FUN_140536890: + 0x126 bit 0): none of Days's
    calls.set(10, 19, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        a[0] = ai::Value::of_int(o != nullptr && ((o->u32(0x124) >> 16) & 1u) != 0 ? 1 : 0);
    });
    // 6:16 a target record emptied (Ts::trap_target_clear, FUN_14053e540 -> FUN_1403bda50: none, 0, none, -1)
    calls.set(6, 16, [](ai::Machine &m, std::span<ai::Value> a) {
        m.memory().set_u32(a[0].bits, 0);
        m.memory().set_u32(a[0].bits + 4, 0);
        m.memory().set_u32(a[0].bits + 8, 0);
        m.memory().set_u32(a[0].bits + 12, 0xffffffffu);
    });
    // 6:46 a stop put on every object of the field but two (Ts::trap_obj_rc_stop_all, FUN_14053ea60: each object's
    // stop record, + 0x64c, given the number and a value, FUN_14040a920 -- four slots of a number and a value, their
    // kinds' flags gathered at + 0, FUN_1403eed10's table, not read); 6:47 that number taken off every object
    // (trap_obj_stop_end_all, FUN_14053eb10 -> FUN_14040a770): the field holds them (World::stop_all) -- Demyx's 14
    calls.set(6, 46, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = running(m);
        if (me->world != nullptr)
            me->world->stop_all(std::int32_t(a[1].bits), object_of(m, a[0]), object_of(m, a[3]));
    });
    calls.set(6, 47, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = running(m);
        if (me->world != nullptr)
            me->world->stop_end_all(std::int32_t(a[0].bits));
    });
    // 2:77 a vacuum made at a point with a strength, its handle back (trap_vacuum_create, FUN_140433200 ->
    // FUN_1403f8f20, in the game's list of them: 0x34 bytes -- + 0 the point, + 0x10 the kinds of object it lets be, a
    // bit a kind, + 0x14 its reach, 0 none, + 0x18 its speed, + 0x1c its least, + 0x20 its most, the three the
    // strength, + 0x24 its turning speed 0, + 0x28 its near range 100, + 0x2c its rate 0.01); the field pulls by it
    // (World::vacuum): Demyx's water clones' (20)
    calls.set(2, 77, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec at;
        m.memory().read(a[0].bits, at.v, sizeof at.v);
        for (float &f : at.v)
            if (std::isnan(f))
                f = 0.0F;
        const std::uint32_t v = m.memory().add(0x34, "a vacuum");
        m.memory().write(v, at.v, sizeof at.v);
        const float strength = a[1].f(), near = 100.0F, rate = 0.01F;
        for (const std::uint32_t off : {0x18u, 0x1cu, 0x20u})
            m.memory().write(v + off, &strength, 4);
        m.memory().write(v + 0x28, &near, 4);
        m.memory().write(v + 0x2c, &rate, 4);
        if (Actor *me = running(m); me->world != nullptr)
            me->world->vacuum(v, true);
        a[0] = ai::Value::of_address(v);
    });
    // 2:78 a vacuum let go (trap_vacuum_destroy, FUN_1404332c0); 2:79 a kind of object it lets be (+ 0x10's bit); 2:80
    // its point (+ 0, NaN made 0)
    calls.set(2, 78, [](ai::Machine &m, std::span<ai::Value> a) {
        if (a[0].bits == 0)
            return;
        if (Actor *me = running(m); me->world != nullptr)
            me->world->vacuum(a[0].bits, false);
        m.memory().remove(a[0].bits);
    });
    calls.set(2, 79, [](ai::Machine &m, std::span<ai::Value> a) {
        m.memory().set_u32(a[0].bits + 0x10, m.memory().u32(a[0].bits + 0x10) | (1u << (a[1].bits & 31u)));
    });
    calls.set(2, 80, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec at;
        m.memory().read(a[1].bits, at.v, sizeof at.v);
        for (float &f : at.v)
            if (std::isnan(f))
                f = 0.0F;
        m.memory().write(a[0].bits, at.v, sizeof at.v);
    });
    // 2:82 its turning speed (+ 0x24, trap_vacuum_set_rot_speed: FUN_1404333f0), 2:83 its near range (+ 0x28,
    // FUN_140433420), 2:84 its rate (+ 0x2c, FUN_140433450), 7:32 its reach (+ 0x14, Ts::trap_vacuum_set_effective_range:
    // FUN_14053fa60): the Lingering Will's vacuum
    for (const auto &[kind, number, at] : {std::tuple{2u, 82u, 0x24u}, std::tuple{2u, 83u, 0x28u},
                                           std::tuple{2u, 84u, 0x2cu}, std::tuple{7u, 32u, 0x14u}})
        calls.set(kind, number, [at](ai::Machine &m, std::span<ai::Value> a) {
            if (a[0].bits != 0)
                m.memory().set_u32(a[0].bits + at, a[1].bits);
        });
    // 2:96 whether an attack it made still stands (trap_attack_is_valid, FUN_140432ca0: its + 8 bit 12): one not ended
    // nor run out (World::attack_finished)
    calls.set(2, 96, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && a[0].bits != 0 && !w->attack_finished(a[0].bits) ? 1 : 0);
    });
    // 0:81 whether a button of the pad was pressed this frame (trap_pad_is_trigger, FUN_14041dae0: bit n of the pad's
    // pressed mask, + 8 of FUN_14039b780(0)), KH2's bits the PS2 pad's: this game's own player's Days buttons
    // (World::pad_pressed): the Lingering Will waits on 10, L1
    calls.set(0, 81, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && w->pad_pressed(std::int32_t(a[0].bits)) ? 1 : 0);
    });
    // 1:365 a group of the player's commands turned off (trap_command_disable_group, FUN_140430230 -> FUN_1403f2770 on
    // his command manager; a command's group its 03system.bin record's + 46): the Lingering Will turns off 1 and 2.
    // Days's deck keeps its commands: none here
    calls.set(1, 365, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:210 the first object of the field whose entry's + 0x4c is a number, into an AI handle's + 4
    // (trap_obj_search_by_part, FUN_14042de30 -> FUN_1403b4180): Demyx's water clones find him so (4002). Days's
    // players are KH2's player, whose part is 1 (1:213): Zexion's illusion, told its target's part (1:218 of 1:213),
    // finds the player so -- before, it found no one, faded out and left as it was made
    calls.set(1, 210, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *me = running(m);
        std::uint32_t found = 0;
        if (me->world != nullptr && (a[1].bits & 0xffffu) == 1u) {
            if (Object *p = me->world->player(*me))
                found = p->address();
        } else if (me->world != nullptr)
            for (Actor *x : me->world->objects())
                if (const std::uint32_t record = x->u32(field::kEntry);
                    record != 0 && (m.memory().u32(record + 0x4c) & 0xffffu) == (a[1].bits & 0xffffu)) {
                    found = x->address();
                    break;
                }
        m.memory().set_u32(a[0].bits + 4, found);
    });
    // the field's KH2 mission (World::mission): 4:3 complete, 4:5 a counter's count set (the count, then its index:
    // YS::MISSION::SetCount(n, index)), 4:6 one more (its index), 4:11 failed, 4:14 its pieces shown, 4:15 hidden,
    // 4:16 its boss dead, 4:20 one less (its index), 4:21 whether its pieces are up, 4:22 left, 4:29 a counter's count
    // (its index), 4:41 so many more (how many, then its index: AddCount(n, index) -- until 2026-11-19 read the other
    // way round, Vexen's full gauges never counted), 4:54 locked, 4:55 whether it is (YS::MISSION, MISSION_COUNT,
    // MISSION2D; the PS3 build's names trap_mission_set_count ...)
    for (const unsigned n : {3u, 5u, 6u, 11u, 14u, 15u, 16u, 20u, 21u, 22u, 29u, 41u, 54u, 55u})
        calls.set(4, n, [n](ai::Machine &m, std::span<ai::Value> a) {
            Actor *me = running(m);
            const int second = a.size() > 1 ? a[1].i() : 0;
            a[0] = ai::Value::of_int(me->world != nullptr ? me->world->mission(n, a[0].i(), second) : 0);
        });
    // 4:8 a gauge's level set, 4:9 so much added (a float, the gauge's index), 4:12 its level over its most
    // (trap_mission_set_gauge, _add_gauge, _get_gauge_ratio: FUN_140433740, FUN_140433750, FUN_140433770): World::
    // mission_gauge
    for (const unsigned n : {8u, 9u})
        calls.set(4, n, [n](ai::Machine &m, std::span<ai::Value> a) {
            if (World *w = running(m)->world)
                w->mission_gauge(n, a[1].i(), a[0].f());
        });
    calls.set(4, 12, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_float(w != nullptr ? w->mission_gauge(12, a[0].i(), 0.0F) : 0.0F);
    });
    // 4:57 a gauge's warning on or off (trap_mission_warning_gauge -> YS::MISSION::WarningGauge(on, its index): Vexen's
    // once his count of full gauges reaches 5); 4:50 and 4:58 a counter's on and off (trap_mission_warning_count,
    // trap_mission_reset_warning_count -> MISSION::WarningCount(its index, on)): World::mission_warning
    calls.set(4, 57, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->mission_warning(6, a[1].i(), a[0].bits != 0);
    });
    calls.set(4, 50, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->mission_warning(5, a[0].i(), true);
    });
    calls.set(4, 58, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->mission_warning(5, a[0].i(), false);
    });
    // 4:48 a digit of a counter set (trap_mission_set_count_figure_num, FUN_140433710: its place n -- 0 the ones --,
    // the digit, the counter's index; its count + (digit - (count / 10^n) % 10) x 10^n): as 4:29 then 4:5 (the
    // hyenas' counter, by their unit's argument, which Days's waves leave 0: not reached there)
    calls.set(4, 48, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        if (w == nullptr)
            return;
        const int place = a[0].i(), digit = a[1].i(), index = a[2].i();
        std::int64_t power = 1;
        for (int k = 0; k < place; ++k)
            power *= 10;
        const std::int64_t count = w->mission(29, index, 0);
        w->mission(5, int(count + (digit - (count / power) % 10) * power), index);
    });
    // 1:262 a line of its voice (trap_obj_voice, FUN_14042bde0: the object's virtual + 0xa0, YS::STDOBJ's voice ->
    // YS::VOICE::request): its number in the object's voice bank and a priority (Hades's 13, 1)
    calls.set(1, 262, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        if (x->world != nullptr)
            x->world->voice(*x, std::int32_t(a[1].bits), std::int32_t(a[2].bits));
    });
    // 1:318 a sound of the object's (trap_obj_play_se, FUN_14042c060 -> FUN_1403dadf0: KH2's sound id, bank x 1000 +
    // number; 0x60708 none -- the PC build leaves that one out), heard where it is: Zexion's book's pages
    calls.set(1, 318, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        World *w = running(m)->world;
        if (o != nullptr && w != nullptr && a[1].bits != 0x60708u)
            w->sound(*o, std::int32_t(a[1].bits), false);
    });
    // 6:70 the same, looping until faded (Ts::trap_obj_play_se_loop, FUN_14053f210): its handle back (the sound's
    // + 0x14 in KH2's list, which 6:71 looks for)
    calls.set(6, 70, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(o != nullptr && w != nullptr ? w->sound(*o, std::int32_t(a[1].bits), true) : 0);
    });
    // 6:71 a sound 6:70 gave faded out over a time (Ts::trap_obj_fadeout_se, FUN_14053f290 -> FUN_1401de320; 0 at
    // once: Zexion's the moment his loop's state ends)
    calls.set(6, 71, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world; w != nullptr && std::int32_t(a[0].bits) > 0)
            w->sound_fade(std::int32_t(a[0].bits), float(std::int32_t(a[1].bits)));
    });
    // 0:104 the area's background sounds on (> 0) or quiet (trap_sound_set_bgse_volume, FUN_14041e840: bit 3 of the
    // sounds of kind 2, + 0x35, in KH2's list): Days's rooms play none of KH2's, so nothing
    calls.set(0, 104, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:42, 1:43 the control of every object of a type off and on (func_obj_control_off / _on -> YS::OBJ::ControlOff,
    // ControlOn: YS::ControlFlag's bit, type 0 the player, 1 his party, under 25): Zexion takes both away as he dies
    calls.set(1, 42, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->control(std::int32_t(a[0].bits), true);
    });
    calls.set(1, 43, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->control(std::int32_t(a[0].bits), false);
    });
    // 1:187, 1:188 the prizes drained and no longer (trap_status_prize_drain_start / _end: DAT_142a11400 bit 3, which
    // YS::STATUS::IsPrizeDrain reads in each prize's check_get: taken by the player where it lies); Zexion's last
    // thread sets it for one tick
    calls.set(1, 187, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->prize_drain(true);
    });
    calls.set(1, 188, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->prize_drain(false);
    });
    // 1:272 whether the object is counted defeated (trap_obj_is_dead, FUN_14042dd10: + 0x9b8 bit 2, 1:328's dead mark,
    // which its end sets too): Zexion asks it of his books
    calls.set(1, 272, [](ai::Machine &m, std::span<ai::Value> a) {
        const Object *o = object_of(m, a[0]);
        a[0] = ai::Value::of_int(o != nullptr && (o->u32(field::kFade) & bit::kDeadMark) != 0 ? 1 : 0);
    });
    // 1:305, 1:306 the camera's eye and the point it looks at (trap_camera_eye, trap_camera_at: FUN_14019ce40(1)'s
    // FUN_14019cfb0 / FUN_14019cff0): Days's camera's last; Zexion puts his books where the player looks
    calls.set(1, 305, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec3 eye{}, at{};
        World *w = running(m)->world;
        if (w == nullptr || !w->camera_view(eye, at))
            eye = running(m)->place();
        Vec v;
        v.v[0] = eye[0], v.v[1] = eye[1], v.v[2] = eye[2], v.v[3] = 1.0F;
        a[0] = give_vec(m, "1:305", v);
    });
    calls.set(1, 306, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec3 eye{}, at{};
        World *w = running(m)->world;
        if (w == nullptr || !w->camera_view(eye, at))
            at = running(m)->place();
        Vec v;
        v.v[0] = at[0], v.v[1] = at[1], v.v[2] = at[2], v.v[3] = 1.0F;
        a[0] = give_vec(m, "1:306", v);
    });
    // 1:281 KH2's scope camera made on the object, its eye's point the height given above its place (trap_camera_
    // begin_scope -> dk::ScopeCamera::create: ObjCamera's mode 7, its zoom 0.5, close-up distance 10 and target (0, 0,
    // 0) to start); 1:283 gone (trap_camera_end_scope -> ScopeCamera::destroy: the mode before back while it is 7);
    // 1:312, 1:313, 1:314 its zoom, close-up distance and target (trap_camera_set_scope_zoom, _closeup_distance,
    // _target_pos). Xigbar's sniping: the camera from his head, toward the player as its target moves to him
    calls.set(1, 281, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (World *w = running(m)->world; w != nullptr && o != nullptr)
            w->scope_begin(*o, a[1].f());
    });
    calls.set(1, 283, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->scope_end();
    });
    calls.set(1, 312, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->scope_zoom(a[0].f());
    });
    calls.set(1, 313, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->scope_closeup(a[0].f());
    });
    calls.set(1, 314, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->scope_target(xyz(read_vec(m, a[0].bits)));
    });
    // 1:316 where a point shows on KH2's screen (trap_camera_get_projection_pos: kn::RotTransPers through kn::
    // getProjection and getCamera, then 1792 + 256 off x and 1840 + 208 off y: KH2's dots from the screen's middle,
    // its depth after, w 0); Xigbar's scope locks on when the player shows within 50 across and 30 up or down
    calls.set(1, 316, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        Vec3 out{};
        World *w = running(m)->world;
        if (w != nullptr && w->projection(xyz(read_vec(m, a[0].bits)), out))
            v.v[0] = out[0], v.v[1] = out[1], v.v[2] = out[2];
        else
            v.v[0] = v.v[1] = 1.0e6F; // no camera: nowhere on the screen
        v.v[3] = 0.0F;
        a[0] = give_vec(m, "1:316", v);
    });
    // 1:285 one of the object's pictures shown (trap_obj_show_picture -> YS::STDOBJ::show_picture: its archive's first
    // entry of type 0x2e -- a BAR of a LAYD and an IMGZ -- as a dk::Layout of that layout number), a handle for it;
    // 1:290 another layout of it (trap_picture_change -> Tz::Layout::setNum); 1:315 it moved by x, y (trap_picture_
    // set_pos -> dk::Layout::setPos: YI::LAYOUT::SetOffset, whole dots). Xigbar's scope's sights
    calls.set(1, 285, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        World *w = running(m)->world;
        a[0] = ai::Value::of_address(w != nullptr && o != nullptr ? w->show_picture(*o, std::int32_t(a[1].bits)) : 0u);
    });
    calls.set(1, 290, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->picture_change(a[0].bits, std::int32_t(a[1].bits));
    });
    calls.set(1, 315, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->picture_place(a[0].bits, std::trunc(a[1].f()), std::trunc(a[2].f()));
    });
    // 6:0 the screen shaken (Ts::trap_camera_shake -> ryj::SHAKE::Start: life, the x and y waves' periods, their sizes
    // and their sizes' change a tick); Xigbar's scope when a shot of his own comes back to him
    calls.set(6, 0, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->screen_shake(a[0].f(), a[1].f(), a[2].f(), a[3].f(), a[4].f(), a[5].f(), a[6].f());
    });
    // 1:135 where a segment meets the room (trap_bg_cross_pos -> YS::BG::GetCrossPos(from, to, out, flags)): the point
    // at the third argument, the polygon's number back (below 0 none); 1:137 that polygon's normal (trap_bg_get_normal
    // -> dk::Octree::getNormal). Xigbar's shots glance off the room's walls so
    calls.set(1, 135, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        Vec3 hit{};
        const int polygon =
            w != nullptr ? w->bg_cross(*running(m), xyz(read_vec(m, a[0].bits)), xyz(read_vec(m, a[1].bits)), hit) : -1;
        if (polygon >= 0) {
            const float p[4] = {hit[0], hit[1], hit[2], 1.0F};
            m.memory().write(a[2].bits, p, sizeof p);
        }
        a[0] = ai::Value::of_int(polygon);
    });
    calls.set(1, 137, [](ai::Machine &m, std::span<ai::Value> a) {
        Vec v;
        Vec3 n{};
        if (World *w = running(m)->world; w != nullptr && w->bg_normal(std::int32_t(a[0].bits), n))
            v.v[0] = n[0], v.v[1] = n[1], v.v[2] = n[2];
        v.v[3] = 1.0F;
        a[0] = give_vec(m, "1:137", v);
    });
    // 1:359, 1:360 the player's command menu drawn in the object's own pictures and back (trap_obj_hook_command_image
    // -> YS::STDOBJ::hook_command_image: its archive's -- its .a.us's -- entries of types 0x18 and 0x19 named "comd",
    // dk::COMMAND_DRAW::setCommandImage; trap_obj_reset_command_image): Zexion's book while his Lexicon stands, drawn
    // over Days's deck (ui/reaction_command.cpp)
    calls.set(1, 359, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (World *w = running(m)->world; w != nullptr && o != nullptr)
            w->command_image(*o, true);
    });
    calls.set(1, 360, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (World *w = running(m)->world; w != nullptr && o != nullptr)
            w->command_image(*o, false);
    });
    // 10:56 a friend's warp point given up (kn::trap_friend_invalidate_warp_point, FUN_1405371f0 -> FUN_1401b4f00: a
    // point and a radius): Days's field has none of KH2's party, so nothing
    calls.set(10, 56, [](ai::Machine &, std::span<ai::Value>) {});
    // 6:24 what the room's collision pushed it by this tick (trap_obj_get_bg_press, FUN_14053e080: + 0xa78, which
    // KH2's actor's update writes, FUN_1403b9420; the shell writes Days's push there): the Volcanic Lord's charge ends
    // on a wall so
    calls.set(6, 24, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        Vec v;
        for (int i = 0; i < 4; ++i)
            v.v[i] = x->f(field::kPressed + 4 * std::uint32_t(i));
        a[0] = give_vec(m, "6:24", v);
    });
    // 10:31 whether the object is a summon (trap_sysobj_is_summon, FUN_140536c30: + 0x6c8 bit 22): none in Days's field
    calls.set(10, 31, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        a[0] = ai::Value::of_int(o != nullptr && (o->u32(field::kFlags6c8) & (1u << 22)) != 0 ? 1 : 0);
    });
    // 1:270, 1:271 an effect its motions' triggers start kept from starting, let start again
    // (trap_obj_motion_disable_anmatr_effect, _enable_: FUN_1403c7280, FUN_1403c72c0 on its motion controller's list of
    // 16 at + 0x1b8, which FUN_14040dd70 asks, FUN_1403c7db0, before a trigger 1's element starts): Scar's
    calls.set(1, 270, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->muted_effects.insert(std::int32_t(a[1].bits));
    });
    calls.set(1, 271, [](ai::Machine &m, std::span<ai::Value> a) {
        self_of(m, a[0])->muted_effects.erase(std::int32_t(a[1].bits));
    });
    // 1:319 whether an object is Sora (trap_sysobj_is_sora, FUN_14042b2a0: + 0x6cb bit 0): Days's players stand in for
    // KH2's player, so they are; 6:2 the player's form (trap_player_get_form: KH2's player's + 0xde0, 0 his own): 12
    // while a script turned him into a card or a die (1:242), else his own
    calls.set(1, 319, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        a[0] = ai::Value::of_int(o != nullptr && (is_player(o) || (o->u32(field::kFlags6c8) & (1u << 24)) != 0) ? 1 : 0);
    });
    calls.set(6, 2, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = running(m);
        a[0] = ai::Value::of_int(x->world != nullptr ? x->world->player_form(*x) : 0);
    });
    // 1:242 KH2's player turned into an object entry's form (YS::trap_player_capture_form -> YS::CHANGEFORM::Capture
    // on SORA::Get(): a CHANGEFORM of form 12 and that entry; Luxord's card 0x601 and die 0x602, M_EX950_CARD and
    // M_EX950_DICE) and back (1:243, trap_player_capture_form_end -> CHANGEFORM::CaptureEnd: the form before)
    calls.set(1, 242, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = running(m);
        if (x->world != nullptr)
            x->world->capture_form(*x, a[0].i());
    });
    calls.set(1, 243, [](ai::Machine &m, std::span<ai::Value>) {
        Actor *x = running(m);
        if (x->world != nullptr)
            x->world->capture_end(*x);
    });
    // 6:27 / 6:29 the turned player's action (Ts::trap_player_dice / trap_player_card: YS::OBJ::change_action of
    // SORA::Get() to 0x2b ACTION_DICE / 0x2e ACTION_CARD); 6:28 / 6:30 their numbers (Ts::trap_dice_set_spec /
    // trap_card_set_spec: Ts::ACTION_DICE::SetSpec(f1..f4) / Ts::ACTION_CARD::SetSpec(f1..f7), kept for every one after)
    calls.set(6, 27, [](ai::Machine &m, std::span<ai::Value>) {
        Actor *x = running(m);
        if (x->world != nullptr)
            x->world->form_action(*x, 0x2b);
    });
    calls.set(6, 29, [](ai::Machine &m, std::span<ai::Value>) {
        Actor *x = running(m);
        if (x->world != nullptr)
            x->world->form_action(*x, 0x2e);
    });
    calls.set(6, 28, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->form_spec(0x2b, {a[0].f(), a[1].f(), a[2].f(), a[3].f()});
    });
    calls.set(6, 30, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->form_spec(0x2e, {a[0].f(), a[1].f(), a[2].f(), a[3].f(), a[4].f(), a[5].f(), a[6].f()});
    });
    // 4:56 the control left off after the event that runs (trap_event_continue_control_off -> YS::EVENT::
    // ContinueControlOff): scripts call it only while one runs (1:34), which no KH2 event of the shell's is -- nothing
    calls.set(4, 56, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:122 its state printed for KH2's debugging (trap_obj_dump, FUN_140430540: empty in the game)
    calls.set(1, 122, [](ai::Machine &, std::span<ai::Value>) {});
    // 1:207 KH2's reaction camera on an object (trap_camera_watch, FUN_140430b80: a target record of the object's actor,
    // FUN_1403bee60, to FUN_140176310 -- the PS3 build's TARGET::set_obj, then dk::ReactionCamera::create: the camera's
    // mode 3, the target kept); 1:295 the camera back home (trap_camera_reset, FUN_140430b60 -> FUN_140165a40:
    // ObjCamera::setHome). Dark Thorn's reaction command watches it so
    calls.set(1, 207, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        if (running(m)->world != nullptr && o != nullptr)
            running(m)->world->camera_watch(*o);
    });
    calls.set(1, 295, [](ai::Machine &m, std::span<ai::Value>) {
        if (running(m)->world != nullptr)
            running(m)->world->camera_home();
    });
    // 1:223 the camera given back (trap_camera_cancel, FUN_140430be0: KH2's camera mode back to 4, the field's, its
    // value kept at DAT_140b64fa0 -- not read further): the reaction camera let go, as 1:295. Data Roxas cancels it
    // as the player's motion 246 ends his reaction command
    calls.set(1, 223, [](ai::Machine &m, std::span<ai::Value>) {
        if (running(m)->world != nullptr)
            running(m)->world->camera_home();
    });
    // 1:193 the camera put at its place at once (trap_camera_warp, FUN_140430b40 -> FUN_140165bc0: ObjCamera::setWarp,
    // its + 0x38 set: the next update takes no easing). Xaldin's Jump warps it as his reaction command begins
    calls.set(1, 193, [](ai::Machine &m, std::span<ai::Value>) {
        if (running(m)->world != nullptr)
            running(m)->world->camera_warp();
    });
    // KH2's lights (lights.h): 1:181 one made of a kind (FUN_140430950 -> FUN_1403f6f80), its record's address back;
    // 1:182 a flag set (FUN_140430980: bit n of its + 0x10); 1:183 its colour, bytes, and the ticks it fades in over
    // (FUN_1404309c0 -> FUN_1403f76e0); 1:184 faded out over ticks (FUN_140430a20 -> FUN_1403f76a0)
    calls.set(1, 181, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_address(w != nullptr ? w->light_create(a[0].i()) : 0);
    });
    calls.set(1, 182, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world; w != nullptr && a[0].bits != 0)
            w->light_flag(a[0].bits, a[1].i());
    });
    calls.set(1, 183, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world; w != nullptr && a[0].bits != 0)
            w->light_colour(a[0].bits, a[1].i() & 0xff, a[2].i() & 0xff, a[3].i() & 0xff, a[4].f());
    });
    calls.set(1, 184, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world; w != nullptr && a[0].bits != 0)
            w->light_fade_out(a[0].bits, a[1].f());
    });
    // 1:251 faded in over ticks (trap_light_fadein, FUN_140430b10 -> FUN_1403f7680: its strength run from none to full,
    // FUN_1403b7810), its colour kept. 1:257 to 1:260 its fog's near, far, least and most (trap_light_set_fog_near ...
    // _max: its record's + 0x18 to + 0x24), which a room's light gives the map (FUN_1401626f0): Days's field draws no
    // KH2 fog, so they are kept by nothing. The Illuminator's dark
    calls.set(1, 251, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world; w != nullptr && a[0].bits != 0)
            w->light_fade_in(a[0].bits, a[1].f());
    });
    for (const unsigned n : {257u, 258u, 259u, 260u})
        calls.set(1, n, [](ai::Machine &, std::span<ai::Value>) {});
    add_final_xemnas(calls);
    add_friend_calls(calls);
}

// ------------------------------------------------------------------------------------------------- a party member's

void add_friend_calls(ai::Calls &calls)
{
    // 1:244 whether the field fights (YS::trap_status_is_battle -> YS::STATUS::IsBattleMode)
    calls.set(1, 244, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && w->in_battle() ? 1 : 0);
    });
    // 1:276 its magic points spent by its use (YS::trap_obj_use_mp -> YS::PARTY::use_mp: 10:43's)
    calls.set(1, 276, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]); x != nullptr && running(m)->world != nullptr)
            running(m)->world->magic_use(*x);
    });
    // 10:43 that use set (kn::trap_obj_set_use_mp: YS::PARTY's UseMp)
    calls.set(10, 43, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]); x != nullptr && running(m)->world != nullptr)
            running(m)->world->magic_use_set(*x, a[1].i());
    });
    // 10:50 its magic points (kn::trap_sheet_get_mp: YS::SHEET's Mp)
    calls.set(10, 50, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(x != nullptr && w != nullptr ? std::int32_t(w->magic_points(*x)) : 0);
    });
    // 2:56 the target of the player's lock-on into the record given (YS::trap_btlobj_lockon_target: its object, none 0)
    calls.set(2, 56, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        World *w = running(m)->world;
        Object *t = x != nullptr && w != nullptr ? w->lockon_target(*x) : nullptr;
        a[0] = ai::Value::of_address(t != nullptr ? t->address() : 0u);
    });
    // 6:61 whether a script's thread holds an object (Ts::trap_obj_is_hook -> YS::OBJ::is_hook: 1:112's)
    calls.set(6, 61, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(x != nullptr && w != nullptr && w->hooked(*x) ? 1 : 0);
    });
    // 7:24 / 7:25 a party member's own turning on and off (Ts::trap_friend_enable_system_wishdir, _disable_)
    calls.set(7, 24, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->friend_turning(*self_of(m, a[0]), true);
    });
    calls.set(7, 25, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->friend_turning(*self_of(m, a[0]), false);
    });
    // 10:14 its target set (kn::trap_friend_set_target -> Friend::setTarget(mode, target))
    calls.set(10, 14, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->friend_target(*self_of(m, a[0]), a[1].i(), a[2].bits != 0 ? object_of(m, a[2]) : nullptr);
    });
    // 10:17 / 10:18 whether an object is an enemy, a boss (kn::trap_sysobj_is_zako, _is_boss: its entry's kind 4;
    // 3 or 0xc)
    calls.set(10, 17, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = a[0].bits != 0 ? object_of(m, a[0]) : nullptr;
        const std::uint32_t record = x != nullptr ? x->u32(field::kEntry) : 0u;
        a[0] = ai::Value::of_int(record != 0 && (m.memory().u32(record + 4) & 0xff) == 4 ? 1 : 0);
    });
    calls.set(10, 18, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = a[0].bits != 0 ? object_of(m, a[0]) : nullptr;
        const std::uint32_t record = x != nullptr ? x->u32(field::kEntry) : 0u;
        const std::uint32_t kind = record != 0 ? m.memory().u32(record + 4) & 0xff : 0u;
        a[0] = ai::Value::of_int(kind == 3 || kind == 0xc ? 1 : 0);
    });
    // 10:24 whether it has an ability (kn::tarp_friend_is_equiped_ability -> Friend::equipedAbility)
    calls.set(10, 24, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && w->friend_ability(*self_of(m, a[0]), a[1].i()) ? 1 : 0);
    });
    // 10:29 an item used on a target (kn::trap_friend_use_item -> Friend::setCommandItem): Days's players have no KH2
    // items -- nothing used
    calls.set(10, 29, [](ai::Machine &, std::span<ai::Value>) {});
    // 10:35 one of its actions' parameters (kn::trap_friend_get_action_param -> FriendPersonality::getActionParam)
    calls.set(10, 35, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_address(w != nullptr ? w->friend_action_param(*self_of(m, a[0]), a[1].i()) : 0u);
    });
    // 10:38 whether its attack is over (kn::trap_attack_is_finish: its motion no longer an attack's -- ended or back to
    // its idle's range)
    calls.set(10, 38, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        a[0] = ai::Value::of_int(x->motion.ended || x->motion.id <= 2 ? 1 : 0);
    });
    // 10:39 its action dropped (kn::trap_friend_action_clear)
    calls.set(10, 39, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->friend_action_clear(*self_of(m, a[0]));
    });
    // 10:53 / 10:54 an effect it watches and whether it plays (kn::trap_friend_add_watch_effect, _is_effect_exist)
    calls.set(10, 53, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->friend_watch(*self_of(m, a[0]), a[1].bits);
    });
    calls.set(10, 54, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && w->friend_watching(*self_of(m, a[0]), a[1].bits) ? 1 : 0);
    });
    // 10:58 a magic linked to it (kn::trap_friend_link_magic -> Friend::linkMagic: the magic's object kept, its end
    // told to it): its darkness's circle, Riku's -- kept by nothing more here
    calls.set(10, 58, [](ai::Machine &, std::span<ai::Value>) {});
}

// ------------------------------------------------------------------------------------------------- Final Xemnas

void add_final_xemnas(ai::Calls &calls)
{
    // 0:66 an effect held or let go on (YS::trap_effect_pause -> ryj::EFFECT::stop(bool): its flag 0x200000)
    calls.set(0, 66, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world; w != nullptr && a[0].bits != 0)
            w->effect_pause(a[0].bits, a[1].bits != 0);
    });
    // 0:80 whether a button of KH2's field pad is held (YS::trap_pad_is_button: bit n of YS::SOFTPAD::Get(0)'s
    // Button): Final Xemnas's air fight lifts the player while he holds 8, the jump
    calls.set(0, 80, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && w->pad_held(std::int32_t(a[0].bits & 0x7fu)) ? 1 : 0);
    });
    // 1:95 a flight turned into a jump (YS::trap_obj_fly_to_jump -> YS::OBJ::fly_to_jump: ACTION 1, the jump, its
    // rise and height from the flight's speed -- + 0xd0 its vertical speed over YS::SPEC::GRAVITY, + 0xd8 where it
    // tops -- its speed the flight's, + 0x20 = + 0x860): it goes on as it flew, falling under KH2's pull
    calls.set(1, 95, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        const Vec3 v = x->velocity;
        x->dashing = false;
        x->jump.on = true;
        x->jump.arc = false;
        x->jump.blow = false;
        x->jump.gravity = kKh2Pull;
        x->jump.keep = Jump{}.keep;
        x->jump.slow = Jump{}.slow;
        x->jump.up = v[1];
        x->jump.along = {v[0], 0.0F, v[2]};
        x->velocity = x->jump.along;
        x->steered = x->jump.along;
        x->move_clock = 0.0F;
        x->set_bits(field::kFlags, bit::kInAir, true);
    });
    // 1:113 the way the player's pad moves him (YS::trap_player_get_movement -> YS::PLAYER::get_movement: its w how far
    // the stick is pushed): the vector of 1:113. Final Xemnas's air fight steers the player by it (1:111)
    calls.set(1, 113, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *o = object_of(m, a[0]);
        World *w = running(m)->world;
        Vec v;
        Vec3 way{};
        float push = 0.0F;
        if (w != nullptr && o != nullptr && w->player_movement(*o, way, push))
            v.v[0] = way[0], v.v[1] = way[1], v.v[2] = way[2], v.v[3] = push;
        a[0] = give_vec(m, "1:113", v);
    });
    // 1:115 the motions a jump goes on to (YS::trap_obj_set_jump_motion: + 0x100 n + 1 its fall, + 0x104 n + 2 its
    // landing, as 1:55's flag sets them)
    calls.set(1, 115, [](ai::Machine &m, std::span<ai::Value> a) {
        Actor *x = self_of(m, a[0]);
        x->motion.after = std::int32_t(a[1].bits) + 1;
        x->motion.landing = std::int32_t(a[1].bits) + 2;
    });
    // 1:192 whether its motion may be cut now (YS::trap_obj_is_motion_cancel: YS::MOTION's Flag bit 4, FLAG_CANCEL --
    // the PC's + 0x18c, its motion's range 4)
    calls.set(1, 192, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_int(x != nullptr && (x->u32(field::kStatus) & 0x10u) != 0 ? 1 : 0);
    });
    // 1:234 a party member's weapon object (YS::trap_party_get_weapon: YS::PARTY's + 0xaf8 + 4 n): Days's players
    // hold no KH2 weapon object -- none (Final Xemnas's air fight then leaves the player's keyblade as it is, 6:65)
    calls.set(1, 234, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        a[0] = ai::Value::of_address(x != nullptr && !is_player(x) ? x->u32(0xaf8u + 4u * (a[1].bits & 3u)) : 0u);
    });
    // 1:354 / 1:356 KH2's 2D layers off and on (YS::trap_event_layer_off: YS::EVENT::LayerOff -- its common, mission,
    // map and other layers -- and YS::INDICATION::Disable; trap_event_layer_on the two back): World::layers
    calls.set(1, 354, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->layers(false);
    });
    calls.set(1, 356, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->layers(true);
    });
    // 6:49 whether an object is stopped (Ts::trap_obj_is_stop -> YS::OBJ::is_stop): World::is_stopped
    calls.set(6, 49, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = object_of(m, a[0]);
        World *w = running(m)->world;
        a[0] = ai::Value::of_int(w != nullptr && x != nullptr && w->is_stopped(*x) ? 1 : 0);
    });
    // 6:58 a state queued for Final Xemnas's other one (Ts::trap_obj_act_child_push -> YS::ACT::child_push): his
    // blades' dance (StateMachine::queue_child)
    calls.set(6, 58, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Actor *x = self_of(m, a[0]))
            x->states().queue_child(*x, name_at(m.memory(), a[1].bits));
    });
    // 6:59 one of Final Xemnas's own (Ts::trap_xemnas_get_obj -> Ts::XEMNAS::Get(n): 0 the one whose first argument
    // is 1, 1 the other; World::xemnas), 0 none: given back, his script puts it in an AI handle's + 4 itself (its
    // 0x3b79 / 0x3b85, then 1:8 on the handle: his other one's own states -- atk_normal_laser3_child)
    calls.set(6, 59, [](ai::Machine &m, std::span<ai::Value> a) {
        World *w = running(m)->world;
        Actor *x = w != nullptr ? w->xemnas(a[0].i()) : nullptr;
        a[0] = ai::Value::of_address(x != nullptr ? x->address() : 0u);
    });
    // 6:65 an object's model cut to the screen's rectangle or not (Ts::trap_obj_set_scissoring: its draw list's flag
    // 0x20, + 0x768): kept; the port's draw does not read it
    calls.set(6, 65, [](ai::Machine &m, std::span<ai::Value> a) {
        Object *x = Object::at(m.memory(), a[0].bits);
        std::uint32_t inner = 0;
        if (x == nullptr && a[0].bits != 0 && m.memory().read(a[0].bits + 4, &inner, 4))
            x = Object::at(m.memory(), inner);
        if (x != nullptr)
            x->set_bits(0x768, 0x20u, a[1].bits != 0);
    });
    // 7:14 where on an attack its reaction command's sign stands (Ts::trap_attack_set_reaction_offset: + 0x130)
    calls.set(7, 14, [](ai::Machine &m, std::span<ai::Value> a) {
        if (a[0].bits == 0)
            return;
        const Vec v = read_vec(m, a[1].bits);
        m.memory().write(a[0].bits + 0x130, v.v, sizeof v.v);
    });
    // 8:3 to 8:7 Final Xemnas's laser dome (ryj::trap_special_last_xemnus_laser_start on its caster, _attack, _end,
    // _optimize, _optimize_end: ryj::LAST_XEMNUS_LASER, World::laser_start ...)
    calls.set(8, 3, [](ai::Machine &m, std::span<ai::Value> a) {
        if (World *w = running(m)->world)
            w->laser_start(*self_of(m, a[0]));
    });
    calls.set(8, 4, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->laser_attack();
    });
    calls.set(8, 5, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->laser_end();
    });
    calls.set(8, 6, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->laser_optimize(true);
    });
    calls.set(8, 7, [](ai::Machine &m, std::span<ai::Value>) {
        if (World *w = running(m)->world)
            w->laser_optimize(false);
    });
    // 10:6 / 10:7 a special command of an object's put in the player's command menu and taken off
    // (kn::trap_obj_set_special_command -> YS::STDOBJ::set_special_command(n), _reset_special_command): World::
    // special_command
    calls.set(10, 6, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]); x != nullptr && running(m)->world != nullptr)
            running(m)->world->special_command(*x, std::int32_t(a[1].bits));
    });
    calls.set(10, 7, [](ai::Machine &m, std::span<ai::Value> a) {
        if (Object *x = object_of(m, a[0]); x != nullptr && running(m)->world != nullptr)
            running(m)->world->special_command_off(*x);
    });
}

} // namespace khdays::kh2_enemies
