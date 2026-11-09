// A KINGDOM HEARTS II enemy as its script sees it (docs/KH2_ENEMIES.md): KH2's actor laid out at KH2's offsets (the
// calls read and change it there, and a script may read it directly), its state machine, its motions, and the script
// machine that drives it. Nothing here touches Days's game: the shell (shell.cpp) links an actor to a Days enemy and
// gives it its world (World).
#pragma once

#include "khdays/assets/kh2_ai.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace khdays::kh2_enemies {

namespace ai = khdays::assets::kh2::ai;
using Vec3 = std::array<float, 3>;

// KH2's actor's offsets the port reads and writes (each named where a call or the game uses it)
namespace field {
constexpr std::uint32_t kSelf = 0x4;        // its own handle (what an AI handle's + 4 holds)
constexpr std::uint32_t kFlags = 0x120;     // bit words: 1:65, 1:70, 1:71; bit 23 in the air (1:60)
constexpr std::uint32_t kMotion = 0x180;    // the motion playing's number (1:15; the scripts wait on it)
constexpr std::uint32_t kMotionNow = 0x184; // the motion its controller plays (1:238: a reaction command's wait)
constexpr std::uint32_t kStatus = 0x18c;    // bit 1 the motion ended (1:14), bit 9 held by a reaction (1:78, 2:5)
constexpr std::uint32_t kPlace = 0x670;     // vec4: 1:147
constexpr std::uint32_t kFlags6c8 = 0x6c8;  // bit 7 the player (KH2's player's maker, FUN_1403a7a40), read by 1:309
constexpr std::uint32_t kCarrier = 0x6a0;   // another object carrying it (FUN_1403da520): its states begin anyway
constexpr std::uint32_t kFacing = 0x680;    // vec4, its w the yaw: 1:79, 1:201
constexpr std::uint32_t kYaw = 0x7a8;       // float: 1:4, 1:79
constexpr std::uint32_t kFlags8c8 = 0x8c8;  // bit 6 set and cleared by 1:104, 1:105
constexpr std::uint32_t kFade = 0x9b8;      // bit 4: the fade held (1:19, 1:20, 1:28)
constexpr std::uint32_t kParams = 0x9f4;    // its battle numbers, a word each (1:39)
constexpr std::uint32_t kTravel = 0xa28;    // vec4: this tick's travel (FUN_1403b8ae3; 1:64)
constexpr std::uint32_t kTravelBefore = 0xa38; // vec4: the tick's before (1:62)
constexpr std::uint32_t kTarget = 0xbf8;       // its target slot (2:23)
constexpr std::uint32_t kAttacker = 0xd38;  // the last to hit it (FUN_140410700), 2:88
constexpr std::uint32_t kSize = 0xe00;      // past every offset read so far
} // namespace field

// Status and flag bits the port's own code sets as KH2's does
namespace bit {
constexpr std::uint32_t kMotionEnded = 1u << 1; // kStatus: the motion played to its end (1:14 waits on it)
constexpr std::uint32_t kHeld = 1u << 9;        // kStatus: a reaction holds it (1:78: no moving)
constexpr std::uint32_t kInAir = 1u << 23;      // kFlags: off the ground (FUN_1403ba9a0, 1:60)
constexpr std::uint32_t kPlayer = 1u << 7;       // kFlags6c8: the player (1:309: the enemies' scripts answer it)
constexpr std::uint32_t kFadeHeld = 1u << 4;    // kFade: 1:28 ended its fading
} // namespace bit

// One state of the table a script fills (1:5, 1:6; FUN_1403ca0e0): 32 bytes in the script's own memory.
struct StateEntry final {
    std::uint32_t at = 0;        // its address
    std::string name;            // its id: a name in the script's code
    std::uint16_t priority = 0;  // +4
    std::uint16_t flags = 0;     // +6: bit 1 locks it while it is the state, bit 0 lets it begin while the actor is
                                 // held or off the ground (FUN_1403cad00, FUN_1403ca550)
    std::uint16_t routine = 0;   // +8: run as a child thread while it is the state
    std::uint16_t on_leave = 0;  // +0xa: run at once when it is left (FUN_1403caec0)
    std::uint16_t on_enter = 0;  // +0x16: run at once after it is entered (FUN_1403cad00)
};

class Actor;

// The state machine at KH2's actor + 0x390 (FUN_1403cae60 enter, FUN_1403cab00 queue, FUN_1403cad00, FUN_1403caec0,
// FUN_1403ca2c0 the queue's next). States are found by name (FUN_1403ca700), so the game asks for them by name too
// ("dead", "leave").
class StateMachine final {
public:
    void set_table(std::uint32_t table, std::uint32_t handle) { table_ = table, handle_ = handle; }
    bool enter(Actor &a, const std::string &name);  // 1:8: at once, the queue emptied (not while locked)
    bool queue(Actor &a, const std::string &name);  // 1:9: after the current one (15 at most, KH2's ring)
    bool pending() const;                           // 1:10: one running or queued
    void clear(Actor &a);                           // 1:127: the current left, the queue emptied
    void unlock()
    {
        locked_ = false;
    } // the game's own entries ("dead") go in over a locked one
    void tick(Actor &a);                            // the current one ended: the next queued entered
    const StateEntry *current() const { return current_ ? &*current_ : nullptr; }
    std::uint32_t handle() const
    {
        return handle_;
    }
    std::function<void(const std::string &)> entered; // told each state entered (the research's runs, the logs)
    bool find(Actor &a, const std::string &name, StateEntry &out) const;

private:
    void leave(Actor &a);
    void go(Actor &a, const StateEntry &e);
    void wait_or_begin(Actor &a);

    std::uint32_t table_ = 0, handle_ = 0;
    std::vector<std::string> queued_;
    std::optional<StateEntry> current_;
    std::uint32_t thread_id_ = 0;
    bool locked_ = false;
    bool waiting_ = false; // its routine not begun: the actor held or off the ground (FUN_1403ca550, + 300 bit 2)
};

// Every access a script makes to an actor's memory directly, kept to learn which offsets it reads (the research's
// watched run) -- off unless asked.
struct Watch final {
    bool on = false;
    std::map<std::uint32_t, unsigned> reads, writes; // offset -> how many
};

// One of KH2's objects in a script's memory: its bytes at KH2's offsets. An enemy (Actor) or one the port stands in
// for -- each of Days's players, as an enemy's script sees it (its place and facing).
class Object : public ai::View {
public:
    Object(ai::Memory &memory, std::string name);
    ~Object() override;
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;

    // ai::View: the bytes at KH2's offsets
    void read(std::uint32_t offset, void *out, std::size_t n) override;
    void write(std::uint32_t offset, const void *in, std::size_t n) override;

    std::uint32_t address() const
    {
        return address_;
    } // where the scripts see it
    const std::string &name() const
    {
        return name_;
    }
    ai::Memory &memory()
    {
        return memory_;
    }
    Watch watch;

    float f(std::uint32_t offset) const;
    std::uint32_t u32(std::uint32_t offset) const;
    void set_f(std::uint32_t offset, float v);
    void set_u32(std::uint32_t offset, std::uint32_t v);
    void set_bits(std::uint32_t offset, std::uint32_t bits, bool on);
    Vec3 place() const
    {
        return {f(field::kPlace), f(field::kPlace + 4), f(field::kPlace + 8)};
    }
    void set_place(const Vec3 &p);
    float yaw() const
    {
        return f(field::kYaw);
    }
    void set_yaw(float yaw); // + 0x7a8, and the facing (+ 0x680, its w the yaw) as KH2 keeps both

    virtual Actor *actor()
    {
        return nullptr;
    }

    // The object a script address names (an AI handle's + 4, or an object's own address), or null.
    static Object *at(ai::Memory &memory, std::uint32_t address);

protected:
    std::string name_;
    ai::Memory &memory_;
    std::vector<std::uint8_t> bytes_;
    std::uint32_t address_ = 0;
};

// What an actor's motions are: the motion numbers its set has (KH2's list: 0 IDLE, 1 WALK ... 44 APPEAR), their
// lengths and loops in ticks, their root motion -- an .mset behind it in the game, a made-up one in the tests.
class Motions {
public:
    virtual ~Motions() = default;
    virtual bool has(int id) const = 0;
    virtual float length(int id) const = 0;           // ticks (60 a second)
    virtual float loop_from(int id) const = 0;        // ticks; at or past the length: it holds at its end
    virtual Vec3 root(int id, float ticks) const = 0; // the root's travel from the motion's start (model space, cm)
};

// The motion an actor plays (its motion controller, actor + 0x158; FUN_1403c86a0 plays one): its number, its time,
// the blend from the motion before (KH2's frames as the script gives them, read as ticks).
struct Playing final {
    int id = -1;
    float time = 0.0F;
    float blend = 0.0F, blend_left = 0.0F; // the blend's length and what is left of it
    int from_id = -1;                      // the motion blended from, at its time then
    float from_time = 0.0F;
    bool ended = false;
    int after = -1, landing = 5; // + 0x118 / + 0x11c: a jump's next motions (1:55's flag), -1 none
};

// A jump (1:55, 1:168; FUN_1403cbb00): the speed up (KH2's y points down: up is negative) and along the ground.
struct Jump final {
    bool on = false;
    float up = 0.0F; // this tick's vertical speed, KH2's y (negative up)
    Vec3 along{};    // the speed along the ground a tick
    // its pull a tick a tick: KH2's (FUN_1403cbb00's sqrt: 2 / 0.408163 is 2 g; DAT_1405ce184), or 1:156's own
    float gravity = 1.0F / 0.408163F;
    // its class's steering in the air (FUN_1403b7ad0's): the velocity kept an evaluation when steered (the rest the
    // steering's), and kept unsteered -- class 1's (1:55) 0.9 and 0.95 (DAT_1405aaa40, DAT_1405b1c74), class 9's
    // (1:156) its own value at + 0x108 for both (FUN_1403cbdd0)
    float keep = 0.9F, slow = 0.95F;
};

// What the scripts reach of the world around an actor (the shell's: Days's players and ground; a made-up one in the
// tests). Each says the KH2 function it stands for.
class World {
public:
    virtual ~World() = default;
    // The object the actor goes for (1:3 the player's actor, FUN_1403b3e80; 1:121's choice): one of Days's players
    // as KH2 sees it, null when none stands.
    virtual Object *player(Actor &a) = 0;
    // An effect on the actor (1:87, FUN_1402c6b30 on actor + 0x80): the element of that number in the object's
    // effects, and two values FUN_1402c6b30 passes on to the effect; a handle the script gives back with 0:79, 0 none.
    virtual std::uint32_t effect(Actor &, int /*number*/, int, int)
    {
        return 0;
    }
    // Whether the actor is on the screen (1:56: FUN_1403ba4a0 tests it against the camera's view, FUN_1403f3ed0)
    virtual bool on_screen(Actor &)
    {
        return false;
    }
    // A point given on one of its bones (0x4000: the actor itself, in its own axes) in KH2's world (1:83,
    // FUN_1403b50f0): false when the world cannot say
    virtual bool bone_point(Actor &, int /*bone*/, const Vec3 & /*local*/, Vec3 & /*out*/)
    {
        return false;
    }
    // The height of the ground under it, KH2's y (1:98, 1:99: FUN_1403b87e0's ray down)
    virtual float ground(Actor &a);
    // A state every object enters, at once or each after a time at random below `delay` (1:90: FUN_1403cacd0's list,
    // FUN_1403c9d40 sends it to every object)
    virtual void broadcast(Actor &, const std::string & /*name*/, float /*delay*/) {}
    // Its health record (2:9's, at + 0x5c0) given its health now and at most
    virtual void health(Actor &) {}

    // One of Days's players in a script's hands (KH2's player is an actor as its enemies are; Days's are the shell's):
    // what the calls do to one goes to the world. 1:29: `player` plays motion `slot` of `by`'s set from `frame`
    // (FUN_1403c6cc0: the set made the player's, + 0x2b8): false when the world cannot
    virtual bool player_motion(Object & /*player*/, Actor & /*by*/, int /*slot*/, float /*frame*/)
    {
        return false;
    }
    // 1:13 on a player: a motion of its own asked for (FUN_1403c8310), 0 its idle -- the script gives it back
    virtual void player_own_motion(Object & /*player*/, int /*id*/) {}
    // 1:148 / 1:1, 1:4, 1:79 on a player: put at a place, turned to a yaw (KH2's)
    virtual void player_place(Object & /*player*/, const Vec3 & /*place*/) {}
    virtual void player_yaw(Object & /*player*/, float /*yaw*/) {}
    // 1:112 on a player: a script's routine as the player's thread (the one before ended)
    virtual void player_thread(Object & /*player*/, ai::Machine & /*machine*/, std::uint32_t /*thread*/) {}
    // 1:18: `carried` put on `by` (FUN_1403d9fe0: + 0x6a0 its carrier, + 0x6a4 the bone -- 0x4000 its place, not a
    // bone -- + 0x6a6 flags, its own place on it made 0, class 3); 1:102 taken off again (FUN_1403da250)
    virtual void carry(Actor & /*carried*/, Object & /*by*/, int /*bone*/, int /*flags*/) {}
    virtual void uncarry(Actor & /*carried*/) {}
};

// Whether an object is one of Days's players as the scripts see them (no actor of the port's: + 0x6c8 bit 7)
bool is_player(Object *o);

class Actor final : public Object {
public:
    Actor(std::shared_ptr<const ai::Script> script, ai::Memory &memory, const ai::Calls &calls, std::string kind);
    ~Actor() override;

    Actor *actor() override
    {
        return this;
    }
    ai::Machine &machine()
    {
        return *machine_;
    }
    StateMachine &states()
    {
        return states_;
    }
    const std::string &kind() const
    {
        return name_;
    }

    // Its start (trigger 0 with its address, then trigger 1 as its main thread; FUN_1403b5f30), a tick (its state
    // machine, then its threads) and the game's events.
    void start();
    void tick();
    bool event(std::int32_t trigger, std::span<const ai::Value> args = {});

    // The world, the motions, what it plays (1:11, 1:12, 1:61): the shell's, set before start()
    World *world = nullptr;
    const Motions *motions = nullptr;
    Playing motion;
    Jump jump;
    // the move a script asks for this tick (1:89): a direction made unit and a speed (KH2's units a tick), cleared
    // after each tick as KH2 clears + 0xa48 .. + 0xa64 (FUN_1403b8e4d)
    Vec3 move{};
    float move_speed = 0.0F;
    // its steering (1:111, FUN_1403b9da0: + 0x10 a direction made unit, + 0x1c a speed), kept until the script
    // changes it, and its velocity (+ 0x20), which its class's movement makes of it every two ticks (FUN_1403b89a0:
    // + 0xba8 the clock, every tick past a speed of 100; + 0xb98 the travel a tick): on the ground the steering
    // itself (FUN_140422350), in the air approached by a tenth an evaluation, or slowed to 0.95 without it
    // (FUN_1403b7ad0), a jump's own speed along first (FUN_1403cbd30 copies + 0xe0)
    Vec3 steer{};
    float steer_speed = 0.0F;
    Vec3 velocity{};
    float move_clock = 0.0F;
    Vec3 steered{}; // the travel a tick of the last evaluation
    // class 5 (1:58, FUN_140421ce0; its movement FUN_140421c80): along the ground at a velocity of its own, keeping
    // `dash_keep` of it an evaluation (eased toward the steering when steered), until its class changes
    bool dashing = false;
    float dash_keep = 0.0F;
    // the thread a script attached to it (1:112, FUN_1403d76d0: + 0xd48, the one before ended), in the machine that
    // made it
    ai::Machine *side_machine = nullptr;
    std::uint32_t side_thread = 0;
    void attach_thread(ai::Machine &m, std::uint32_t id);
    // its fading (+ 0xa08, FUN_1403b7810: a value and its change a tick; 1:19, 1:20, 1:28): 1 seen, 0 gone
    float fade = 1.0F, fade_step = 0.0F;
    std::uint32_t target_slot = 0; // its slot's address in the scripts' memory (2:23), made on first use

    // A motion played (FUN_1403c86a0): `restart` false leaves one already playing as it is (1:12). Whether the set
    // has it (none: the call does nothing, as KH2's controller does for an empty slot).
    bool play(int id, float blend, float start, bool restart = true);
    // The motion and the jump moved on by `ticks`: the ended bit set at a motion's end, a loop taken; the travel this
    // made in KH2's world (the root's travel turned by the yaw, the script's move, the jump) given back. The shell
    // puts it into Days's place and gives the place back (set_place).
    Vec3 step(float ticks);
    // The shell found it on the ground while it jumps: the jump over, its landing motion (+ 0x11c) played.
    void land();
    // A fall (no jump of its own) ended on the ground: its landing motion as KH2's falling class plays it
    // (FUN_1403cb940: + 0x11c, then 5 again for the next)
    void touch_down();

    static Actor *of_handle(ai::Machine &m, std::uint32_t handle);

private:
    std::unique_ptr<ai::Machine> machine_;
    StateMachine states_;
};

// The name a script address holds (a state's id): up to its NUL, as far as the memory there reaches.
std::string name_at(ai::Memory &memory, std::uint32_t address);

// KH2's facing as a direction for a yaw (atan2(x, z): FUN_1403b65c0), and a direction turned about the up by an angle.
Vec3 facing_of(float yaw);
Vec3 turned(const Vec3 &v, float radians);

} // namespace khdays::kh2_enemies
