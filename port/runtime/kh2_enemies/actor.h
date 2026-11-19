// A KINGDOM HEARTS II enemy as its script sees it (docs/KH2_ENEMIES.md): KH2's actor laid out at KH2's offsets (the
// calls read and change it there, and a script may read it directly), its state machine, its motions, and the script
// machine that drives it. Nothing here touches Days's game: the shell (shell.cpp) links an actor to a Days enemy and
// gives it its world (World).
#pragma once

#include "khdays/assets/kh2_ai.h"

#include <array>
#include <cstdint>
#include <deque>
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
constexpr std::uint32_t kEntry = 0x8;       // its object entry's record (YS::OBJ's Entry: + 0 its number; 1:222)
constexpr std::uint32_t kFlags = 0x120;     // bit words: 1:65, 1:70, 1:71; bit 23 in the air (1:60)
constexpr std::uint32_t kMotion = 0x180;    // the motion playing's number (1:15; the scripts wait on it)
constexpr std::uint32_t kMotionNow = 0x184; // the motion its controller plays (1:238: a reaction command's wait)
constexpr std::uint32_t kStatus = 0x18c;    // bit 1 the motion ended (1:14), bit 9 held by a reaction (1:78, 2:5)
// its motion controller's (+ 0x158) bytes + 0x154 and + 0x155: the "AI" ranges 16 to 19 on now, bits 0 to 3 (FUN_14040d810,
// cleared each tick by FUN_14040e5c0; 1:150), and the triggers 9 to 12 its motion passed this tick, bits 0 to 3
// (FUN_14040dd70, cleared each tick by FUN_1403c6740; 1:151: the Magnum Loader's charge waits for its 52A's frame 80)
constexpr std::uint32_t kMotionMarks = 0x2ac;
// the script's collision groups (1:52 to 1:54; YS::COLLISION_FLAG): a word of groups turned on, the next of those
// turned off (a pair for each 32), looked at before the motion's (+ 0x5a0, ranges 4 and 5; FUN_1403ba550)
constexpr std::uint32_t kCollisionFlags = 0x5a8;
constexpr std::uint32_t kPlace = 0x670;     // vec4: 1:147
constexpr std::uint32_t kFlags6c8 = 0x6c8;  // bit 7 the player (KH2's player's maker, FUN_1403a7a40), read by 1:309
constexpr std::uint32_t kCarrier = 0x6a0;   // another object carrying it (FUN_1403da520): its states begin anyway
constexpr std::uint32_t kFacing = 0x680;    // vec4, its w the yaw: 1:79, 1:201
constexpr std::uint32_t kYaw = 0x7a8;       // float: 1:4, 1:79
constexpr std::uint32_t kFlags8c8 = 0x8c8;  // bit 6 set and cleared by 1:104, 1:105
constexpr std::uint32_t kFade = 0x9b8;      // bit 4: the fade held (1:19, 1:20, 1:28); bit 2 the dead mark (1:328)
constexpr std::uint32_t kUnitArgs = 0x9f4;  // YS::OBJ's UnitArg: its spawn's values, a word each (1:39, 1:218)
constexpr std::uint32_t kTravel = 0xa28;    // vec4: this tick's travel (FUN_1403b8ae3; 1:64)
constexpr std::uint32_t kTravelBefore = 0xa38; // vec4: the tick's before (1:62)
// vec4: what the room's collision pushed it off its way this tick (FUN_1403b9420: the place it was put at less the one
// asked, a tick's worth); 6:24 gives it, its steering stops on it (FUN_140421450)
constexpr std::uint32_t kPressed = 0xa78;
constexpr std::uint32_t kTarget = 0xbf8;       // its target slot (2:23)
constexpr std::uint32_t kAttacker = 0xd38;  // the last to hit it (FUN_140410700), 2:88
constexpr std::uint32_t kKarmaLimit = 0xd4c; // YS::ENEMY's KarmaLimit (2:76), after its Karma
constexpr std::uint32_t kSize = 0xe00;      // past every offset read so far
} // namespace field

// Status and flag bits the port's own code sets as KH2's does
namespace bit {
constexpr std::uint32_t kMotionEnded = 1u << 1; // kStatus: the motion played to its end (1:14 waits on it)
constexpr std::uint32_t kHeld = 1u << 9;        // kStatus: a reaction holds it (1:78: no moving)
// kStatus: a player plays another object's motion (1:29: its enemy's set), which then offers that object's commands
// (FUN_14040d810's range 20: the command's source + 0x2b0); 1:101 waits on it
constexpr std::uint32_t kOtherMotion = 1u << 14;
// kStatus: its motion's range 13 on (FUN_14040d810): what it carries with a carry's flag bit 0 stays on it while it is
// (the actor's update, FUN_1403b4a12: Scar on the player he pins)
constexpr std::uint32_t kKeepsCarried = 1u << 15;
constexpr std::uint32_t kInAir = 1u << 23;      // kFlags: off the ground (FUN_1403ba9a0, 1:60)
// kFlags: YS::OBJ's Flag (1:70 sets one, 1:71 clears it; the PS3 build's names): no touching objects, no being hit
constexpr std::uint32_t kNoHitObject = 1u << 1, kNoHitAttack = 1u << 3;
constexpr std::uint32_t kPlayer = 1u << 7;       // kFlags6c8: the player (1:309: the enemies' scripts answer it)
constexpr std::uint32_t kFadeHeld = 1u << 4;    // kFade: 1:28 ended its fading
constexpr std::uint32_t kDeadMark = 1u << 2;    // kFade: counted as defeated (1:328, FUN_1403d4a40)
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
    // + 0xa + 2 n: its routine for the game's event n (FUN_1403ca1d0): 1 the actor left the ground, 2 it landed
    std::uint16_t on_air = 0, on_land = 0; // + 0xc, + 0xe
    // + 0x10 (event 3): run when the actor is struck (FUN_1403ca470, from FUN_140410d60 before trigger 6), given its
    // handle and the hit's damage
    std::uint16_t on_struck = 0;
    std::uint16_t on_shout = 0;            // + 0x14 (event 5): run when its name is shouted (1:90; FUN_1403ca950)
    std::uint16_t on_enter = 0;  // +0x16: run at once after it is entered (FUN_1403cad00)
    // + 0x18 (event 7): run when the actor is hit (FUN_1403caba0, from KH2's hit FUN_140411060), given its handle,
    // the hit's damage and its attack (Saïx's asks the attack's owner, 2:24)
    std::uint16_t on_hit = 0;
};

class Actor;

// The state machine at KH2's actor + 0x390 (FUN_1403cae60 enter, FUN_1403cab00 queue, FUN_1403cad00, FUN_1403caec0,
// FUN_1403ca2c0 the queue's next). States are found by name (FUN_1403ca700), so the game asks for them by name too
// ("dead", "leave").
class StateMachine final {
public:
    void set_table(std::uint32_t table, std::uint32_t handle) { table_ = table, handle_ = handle; }
    bool enter(Actor &a, const std::string &name);  // 1:8: at once, the queue emptied (not while locked)
    // 1:167: as 1:8 when its priority (+ 4) is at least the current state's (FUN_1403caa90: + 0x128, which entering
    // sets, FUN_1403cad00; 0 with none)
    bool enter_over(Actor &a, const std::string &name);
    bool queue(Actor &a, const std::string &name);  // 1:9: after the current one (15 at most, KH2's ring)
    // 6:58 (Ts::trap_obj_act_child_push -> YS::ACT::child_push): queued as 1:9, its element's flag bit 0 set -- when
    // its turn comes (YS::ACT::check_start) it is entered on Final Xemnas's other one (Ts::XEMNAS::Get(1), World::
    // xemnas) and the queue goes on to the next: his blades' dance, his own moves queued between his other one's
    bool queue_child(Actor &a, const std::string &name);
    bool pending() const;                           // 1:10: one running or queued
    void clear(Actor &a);                           // 1:127: the current left, the queue emptied
    void unlock()
    {
        locked_ = false;
    } // the game's own entries ("dead") go in over a locked one
    void tick(Actor &a);                            // the current one ended: the next queued entered
    // the game's event n told to the current state (FUN_1403ca1d0): its routine for it run at once, the actor's handle
    // given (then `more`), unless the state has not begun (+ 300's bit 2): 1 off the ground, 2 landed, 3 struck (the
    // damage after the handle: FUN_1403ca470), 7 hit (the damage and the attack: FUN_1403caba0)
    void event(Actor &a, int n, std::span<const ai::Value> more = {});
    // a shout of a state's name heard (FUN_1403ca950): that state's shout routine (+ 0x14) run at once, given this
    // machine's handle and the shouter's, whatever the state is now; whether it has one
    bool shout(Actor &a, const std::string &name, std::uint32_t from);
    const StateEntry *current() const { return current_ ? &*current_ : nullptr; }
    // YS::ACT::can_control: a state runs (its routine begun) and is not held by a damage motion -- one whose priority
    // is under 300 waits each tick a damage motion plays (YS::ACT::exec: is_damage_motion), unless its flag 1 lets it
    // run on (+ 0x9c bit 0)
    bool can_control(const Actor &a) const;
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
    struct Queued final {
        std::string name;
        bool child = false; // 6:58's: entered on Final Xemnas's other one
    };
    std::vector<Queued> queued_;
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
    // 1:75: a slot of the set made its motions' first place to look (YS::MOTION::hook: a slot of type 0x14 whose
    // entries stand for the set's own by number, the set's own where one has none -- Saïx's berserk, his slot 161);
    // whether the set has such a slot there. 1:76 none again (unhook), 1:77 whether one is (is_hook). The actor's
    // motion controller's state, kept by its set as the actor sees it (const: the actor's motions are)
    virtual bool hook(int /*id*/) const
    {
        return false;
    }
    virtual void unhook() const {}
    virtual bool hooked() const
    {
        return false;
    }
};

// The motion an actor plays (its motion controller, actor + 0x158, the PS3 build's YS::MOTION; FUN_1403c86a0 plays
// one): its number and its time (KH2's frames as the script gives them, read as ticks).
//
// A motion started over a blend above 2 ticks, with a pose shown before (start_motion -> start_blend, FUN_1403c87c0)
// stands at its start frame while the blend runs: the controller's time (+ 0x44, YS::MOTION::Time) counts the blend
// from 2, a tick a tick (add_time), up to its length (+ 0x48, BlendTime); each tick's pose is the one shown the tick
// before taken count/length of the way to the motion's start frame (MOTION::exec keeps the pose shown and the start
// frame's, Context1 and Context2; MOTION::calc blends them by Time / BlendTime). At the blend's end the motion goes on
// from its start (+ 0x4c, BlendStart) by what the count went past the length, and plays its start frame's triggers.
struct Playing final {
    int id = -1;
    float time = 0.0F;
    // the blend: its length (0 none, or over), its count (from 2), how much of the pose shown at the motion's start is
    // left in the pose now (the ticks' blends one after another: each count/length toward a start frame that stands)
    float blend = 0.0F, blend_count = 0.0F, blend_keep = 0.0F;
    // KH2 moves its motions on (add_time) before its objects' scripts run, so one started in a tick stands at its
    // start frame that tick: the port's script runs first, and step leaves a motion started this tick where it is
    bool fresh = false;
    // its triggers played up to `cued` (KH2's window: those after it, up to the time now); `cue_start` the start
    // frame's own still to play (exec_trigger(start, -1): the frame equal to the start); `looped` this tick's step
    // went round its loop (those up to its end, then those after its loop point: add_time's two calls)
    float cued = 0.0F;
    bool cue_start = false, looped = false;
    unsigned started = 0; // a new number each motion started: the pose shown then is the blend's from
    // the motions asked to follow this one, each with its blend (YS::MOTION::push_queue: a ring at + 0x44, its ends
    // + 0x3c and + 0x40 -- 1:13, 2:87's idle): the first taken when this one blends back (its range 2) or reaches its
    // end (add_time takes it there, else the motion goes round its loop); a motion started empties it
    // (FUN_1403c86a0's + 0x50)
    std::deque<std::pair<int, float>> queued;
    bool ended = false;
    int after = -1, landing = 5; // + 0x118 / + 0x11c: a jump's next motions (1:55's flag), -1 none
};

// KH2's pull on a jump, centimetres a tick a tick (DAT_1405ce184)
constexpr float kKh2Pull = 0.408163F;

class Actor;
// A throw (1:129, 2:1's blow; calls.cpp): along the ground way (way_x, way_z) `speed` cm a tick, `up` its vertical
// speed (KH2's y: negative up), landing in the motion after the one playing
void thrown(Actor &x, float way_x, float way_z, float speed, float up);

// A jump (1:55, 1:168, 1:156, a throw): its vertical speed (KH2's y points down: up is negative) and along the ground.
struct Jump final {
    bool on = false;
    float up = 0.0F; // this tick's vertical speed, KH2's y (negative up)
    Vec3 along{};    // the speed along the ground a tick
    // its pull a tick a tick (1:156's own, a throw's KH2's)
    float gravity = kKh2Pull;
    bool blow = false; // a blow's (1:129, 2:1: KH2's blow class, its flags' bit 6 -- what 10:22 asks)
    // class 1's arc by its time (1:55, 1:168; FUN_1403cbb00, its movement FUN_1403cb360): `time` (+ 0xf0) on a tick a
    // tick; before `apex` (+ 0xf4) its height eased up to `rise` (+ 0xfc: sin(sin(time / apex x pi/2) x pi/2) of it),
    // then from `rise` + `base` (+ 0xf8) down under KH2's pull; `height` where it is, from where it began
    bool arc = false;
    float time = 0.0F, apex = 0.0F, rise = 0.0F, base = 0.0F, height = 0.0F;
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
    // KH2's field's "force leave" (DAT_142a11400 bit 7: 1:204 sets it, 1:205 clears it, 1:206 reads it -- Oogie
    // Boogie's script, while his Heartless are to leave); false at an area's start
    bool force_leave = false;
    // The object the actor goes for (1:3 the player's actor, FUN_1403b3e80; 1:121's choice): one of Days's players
    // as KH2 sees it, null when none stands.
    virtual Object *player(Actor &a) = 0;
    // An effect on the actor (1:87, FUN_1402c6b30 on actor + 0x80): the element of that number in the object's
    // effects, and two values FUN_1402c6b30 passes on to the effect; a handle the script gives back with 0:79, 0 none.
    virtual std::uint32_t effect(Actor &, int /*number*/, int, int)
    {
        return 0;
    }
    // 1:149: the same on `target` (FUN_1403b6670's last: the Armored Knight's element 17 on the player it launches), its
    // handle for 0:44
    virtual std::uint32_t effect_on(Actor &a, int number, int x, int y, Object * /*target*/)
    {
        return effect(a, number, x, y);
    }
    // 0:51 (trap_effect_kill): an effect ended at once, by its handle
    virtual void stop_effect(std::uint32_t /*handle*/) {}
    // 0:44 and 0:85 (trap_effect_loop_end, trap_effect_loop_end_kill: FUN_1402be8e0, FUN_1402be8f0, the PS3 build's
    // ryj::EFFECT::loop_end and loop_end_kill, both pppLoopEnd; the kill's flag 1 frees it once it has ended): an
    // effect's loops closed, by its handle -- it ends once its objects have (the port lets every ended effect go)
    virtual void effect_loop_end(std::uint32_t /*handle*/) {}
    // KH2's attacks of their own (YS::ATTACK_MAGIC, 0x138 bytes: 2:10 makes one, FUN_1404371b0), each at the address
    // `at` of the scripts' memory the calls name it by: made by `by`'s script for `owner` (an enemy, or KH2's player:
    // his blow then) with an atkp entry, a combo group and the entry's sub-number; its size (2:11: a radius and a
    // height, a sphere when they match), its place (2:12), its growth (2:62: from a size over n ticks), the effect it
    // follows (2:61), its team (2:37), made to hit an object at once (2:17), ended (2:13)
    virtual void attack_made(std::uint32_t /*at*/, Actor & /*by*/, Object * /*owner*/, int /*atkp*/, int /*group*/,
                             int /*sub*/)
    {
    }
    virtual void attack_size(std::uint32_t /*at*/, float /*radius*/, float /*height*/) {}
    virtual void attack_place(std::uint32_t /*at*/, const Vec3 & /*place*/) {}
    virtual void attack_grow(std::uint32_t /*at*/, float /*ticks*/, float /*radius*/, float /*height*/) {}
    virtual void attack_follow(std::uint32_t /*at*/, std::uint32_t /*effect*/) {}
    virtual void attack_team(std::uint32_t /*at*/, int /*team*/) {}
    virtual void attack_strike(std::uint32_t /*at*/, Object * /*target*/) {}
    virtual void attack_end(std::uint32_t /*at*/) {}
    // ... and what the scripts ask of one: ended (2:14: grown, its effect gone, or it hit), grown or its effect gone
    // (2:29: + 8's 0x18), it hit (2:39: + 8's bit 9), the object it hit (2:44: + 0x90), its way from its owner (2:43,
    // FUN_140437530), whether it can hit an object (2:18, FUN_1403d2060: its team's targets); its place and the place
    // before set at once (2:19, FUN_140437a40: a line swept)
    virtual bool attack_finished(std::uint32_t /*at*/)
    {
        return true;
    }
    virtual bool attack_expired(std::uint32_t /*at*/)
    {
        return true;
    }
    virtual bool attack_landed(std::uint32_t /*at*/)
    {
        return false;
    }
    // ... whether it met the room's floor (2:41: + 8 bit 4)
    virtual bool attack_on_floor(std::uint32_t /*at*/)
    {
        return false;
    }
    virtual std::uint32_t attack_struck(std::uint32_t /*at*/)
    {
        return 0;
    }
    virtual bool attack_way(std::uint32_t /*at*/, Vec3 & /*out*/)
    {
        return false;
    }
    virtual bool attack_can_hit(std::uint32_t /*at*/, Object * /*target*/)
    {
        return false;
    }
    virtual void attack_line(std::uint32_t /*at*/, const Vec3 & /*now*/, const Vec3 & /*before*/) {}
    // Effects the scripts move themselves (their shots): one of an actor's at a place of the world (1:22,
    // FUN_1403b66c0), whether one still plays (0:50, FUN_1402be780: its instance at + 0x30), its place (0:53,
    // FUN_1402be970), set (0:38), its turn set (0:41), ended over n ticks (0:52; 0:51 and 0:44 at once)
    virtual std::uint32_t effect_in_world(Actor & /*a*/, int /*number*/, const Vec3 & /*place*/)
    {
        return 0;
    }
    virtual bool effect_alive(std::uint32_t /*handle*/)
    {
        return false;
    }
    virtual bool effect_where(std::uint32_t /*handle*/, Vec3 & /*out*/)
    {
        return false;
    }
    virtual void effect_move(std::uint32_t /*handle*/, const Vec3 & /*place*/) {}
    virtual void effect_turn(std::uint32_t /*handle*/, const Vec3 & /*angles*/) {}
    virtual void effect_fade(std::uint32_t /*handle*/, float /*ticks*/) {}
    // ... its size (0:39: the effect's scale, + 0x20 of its transform, FUN_1402bea80) and its colour (0:67: + 0x30,
    // FUN_1402be1e0; 1 each as it is, the scripts fade one out by its alpha), set
    virtual void effect_scale(std::uint32_t /*handle*/, const Vec3 & /*scale*/) {}
    virtual void effect_colour(std::uint32_t /*handle*/, const std::array<float, 4> & /*colour*/) {}
    // 1:207: KH2's reaction camera made to watch `o` (dk::ReactionCamera: while a KH2 enemy holds the player, the target
    // camera on it); 1:295: the camera back home (ObjCamera::setHome)
    virtual void camera_watch(Object & /*o*/) {}
    virtual void camera_home() {}
    // 1:193: the camera to its place at once, not eased there (trap_camera_warp: ObjCamera::setWarp)
    virtual void camera_warp() {}
    // KH2's lights (lights.h): 1:181 one made of a kind, its handle (0 none); 1:182 a flag of it set; 1:183 its colour
    // faded in over ticks; 1:184 it faded out over ticks; 1:251 it faded in over ticks, its colour kept
    virtual std::uint32_t light_create(int /*kind*/)
    {
        return 0;
    }
    virtual void light_flag(std::uint32_t /*handle*/, int /*bit*/) {}
    virtual void light_colour(std::uint32_t /*handle*/, int /*r*/, int /*g*/, int /*b*/, float /*ticks*/) {}
    virtual void light_fade_out(std::uint32_t /*handle*/, float /*ticks*/) {}
    virtual void light_fade_in(std::uint32_t /*handle*/, float /*ticks*/) {}
    // 1:262: a line of the actor's voice (YS::OBJ's voice -> YS::STDOBJ::_OVR_voice -> YS::VOICE::request: the sound
    // `id` of its voice bank, one at a time, a new one taking the place of one asked with no higher a priority)
    virtual void voice(Actor &, int /*id*/, int /*priority*/) {}
    // 1:318, 6:70: KH2's sound `id` heard at the object (its kind's: bank x 1000 + number), looping until faded when
    // `loop`; a number for it back (6:71's), 0 none
    virtual int sound(Object &, int /*id*/, bool /*loop*/)
    {
        return 0;
    }
    // 6:71: a sound `sound` gave faded out over `ticks` (60 a second), ended at once for 0
    virtual void sound_fade(int /*handle*/, float /*ticks*/) {}
    // 1:42, 1:43: the control of every object of a type taken away or given back (YS::OBJ::ControlOff / ControlOn:
    // YS::ControlFlag's bit of the type, which YS::OBJ::_OVR_is_control tests against the object's get_type: 0 the
    // player, 1 his party)
    virtual void control(int /*type*/, bool /*off*/) {}
    // 1:187: the field's prizes all taken by the player where they lie (YS::STATUS::IsPrizeDrain, which each prize's
    // check_get reads: PRIZEEFFECT::get on the player); 1:188 ends it
    virtual void prize_drain(bool /*on*/) {}
    // 1:359: the player's command menu drawn in the object's own pictures (YS::STDOBJ::hook_command_image: its
    // archive's IMGD and SEQD named "comd", dk::COMMAND_DRAW::setCommandImage); 1:360 (reset_command_image) KH2's
    // own again
    virtual void command_image(Object & /*of*/, bool /*on*/) {}
    // 1:305, 1:306: the camera's eye and the point it looks at (KH2's units); whether there is a camera
    virtual bool camera_view(Vec3 & /*eye*/, Vec3 & /*at*/)
    {
        return false;
    }
    // A segment of KH2's world against the room's collision (Days's, under the actor): where it first meets it
    // (FUN_14040c2b0's cast: 6:51, 1:141's ground under a point, 1:248's)
    virtual bool ray(Actor & /*a*/, const Vec3 & /*from*/, const Vec3 & /*to*/, Vec3 & /*hit*/)
    {
        return false;
    }
    // 1:135: the same segment and the polygon it meets (trap_bg_cross_pos -> YS::BG::GetCrossPos), where in `hit`; a
    // number for that polygon (1:137's), below 0 none
    virtual int bg_cross(Actor & /*a*/, const Vec3 & /*from*/, const Vec3 & /*to*/, Vec3 & /*hit*/)
    {
        return -1;
    }
    // 1:137: that polygon's normal, KH2's axes (trap_bg_get_normal -> dk::Octree::getNormal); whether it has one
    virtual bool bg_normal(int /*polygon*/, Vec3 & /*normal*/)
    {
        return false;
    }
    // KH2's scope camera (dk::ScopeCamera, ObjCamera's mode 7; Xigbar's sniping): 1:281 made on the object, its eye's
    // point `height` above the object's place (its head), its zoom 0.5, close-up distance 10 and target (0, 0, 0) to
    // start; 1:283 gone, the camera's mode before back; 1:312 its zoom, 1:313 its close-up distance, 1:314 the point
    // it looks at
    virtual void scope_begin(Object & /*o*/, float /*height*/) {}
    virtual void scope_end() {}
    virtual void scope_zoom(float /*zoom*/) {}
    virtual void scope_closeup(float /*distance*/) {}
    virtual void scope_target(const Vec3 & /*target*/) {}
    // 1:316: where a point of KH2's world shows on KH2's screen through the camera (trap_camera_get_projection_pos:
    // kn::RotTransPers, the GS's 2048 off): KH2's dots from the screen's middle, y down, in x and y; its depth in z;
    // whether a camera is known
    virtual bool projection(const Vec3 & /*point*/, Vec3 & /*out*/)
    {
        return false;
    }
    // KH2's object pictures (YS::STDOBJ::show_picture: its archive's first entry of type 0x2e, a BAR of a LAYD and an
    // IMGZ, as dk::Layout): 1:285 its layout `number` shown, a handle for it (0 none); 1:290 another layout of it
    // (Tz::Layout::setNum); 1:315 it moved by x, y KH2 dots (dk::Layout::setPos -> YI::LAYOUT::SetOffset)
    virtual std::uint32_t show_picture(Object & /*o*/, int /*number*/)
    {
        return 0;
    }
    virtual void picture_change(std::uint32_t /*handle*/, int /*number*/) {}
    virtual void picture_place(std::uint32_t /*handle*/, float /*x*/, float /*y*/) {}
    // 6:0: the screen shaken (Ts::trap_camera_shake -> ryj::SHAKE::Start: the shown frame moved by dk::DispFrame::
    // setShakeXYoffset): its life in ticks, the x and y waves' periods, their sizes in KH2's dots and how much those
    // change a tick
    virtual void screen_shake(float /*life*/, float /*period_x*/, float /*period_y*/, float /*size_x*/,
                              float /*size_y*/, float /*speed_x*/, float /*speed_y*/)
    {
    }
    // 0:80: whether this game's own player holds the button of KH2's field pad bit `bit` now (trap_pad_is_button: bit
    // n of YS::SOFTPAD::Get(0)'s held mask; the field's table, YS::SoftPadTableField, makes them from the pad: 8 the
    // jump's CANCEL, 9 square, 10 triangle, 11 R1, 14 to 16 and 28 the D-pad, 22 Select, 23 L1, 24 to 27 circle,
    // triangle, square, cross)
    virtual bool pad_held(int /*bit*/)
    {
        return false;
    }
    // 1:113: the way the player's pad moves him (YS::PLAYER::get_movement -> PadToMovement: the camera's way, KH2's
    // world) and how far the stick is pushed (its w: 0 none, 1 all the way); whether `o` is a player whose pad is known
    virtual bool player_movement(Object & /*o*/, Vec3 &way, float &push)
    {
        way = {};
        push = 0.0F;
        return false;
    }
    // 1:354 / 1:356: KH2's 2D layers off and on (YS::EVENT::LayerOff: its common, mission, map and other layers, and
    // YS::INDICATION::Disable: the reaction command's sign), Final Xemnas's while his laser dome's event plays
    virtual void layers(bool /*on*/) {}
    // 6:49: whether an object is stopped (Ts::trap_obj_is_stop -> YS::OBJ::is_stop: its STOP's -- Stop magic, a
    // stop put on it, 6:46 --, a summon's mode)
    virtual bool is_stopped(Object & /*o*/)
    {
        return false;
    }
    // 0:66: an effect held where it is or let go on (trap_effect_pause -> ryj::EFFECT::stop(bool): its flag 0x200000)
    virtual void effect_pause(std::uint32_t /*handle*/, bool /*paused*/) {}
    // 6:59: Final Xemnas's own (Ts::XEMNAS::Get: the field's objects whose entry's part is 4007, a B_EX170's): n 0
    // the one whose first argument (UnitArg[1]) is 1, else one whose is not; null none
    virtual Actor *xemnas(int /*n*/)
    {
        return nullptr;
    }
    // Final Xemnas's laser dome (ryj::LAST_XEMNUS_LASER: 8:3 started on its caster, 8:4 its lasers fired, 8:5 ended,
    // 8:6 / 8:7 the room's map group 0, sky and glow hidden and shown again while it plays)
    virtual void laser_start(Actor & /*caster*/) {}
    virtual void laser_attack() {}
    virtual void laser_end() {}
    virtual void laser_optimize(bool /*on*/) {}
    // 10:6 / 10:7: a special command put in the player's command menu by an object (YS::STDOBJ::set_special_command:
    // PLAYER_COMMAND::set_special_command(n, its archive's 'comd' pictures, the object)) and taken off
    virtual void special_command(Object & /*o*/, int /*number*/) {}
    virtual void special_command_off(Object & /*o*/) {}
    // KH2's party member's AI (kn::Friend; the shell's Friend) as its script reaches it: 1:244 whether the field
    // fights (YS::STATUS::IsBattleMode); 10:14 its target set (Friend::setTarget: a mode, an object); 7:24 / 7:25 its
    // own turning on and off (Friend::enable / disableSystemWishDir); 10:39 its action dropped (Friend::actionClear);
    // 10:35 one of its actions' parameters (kn::FriendActionParam, 0x80 bytes) in the scripts' memory, 0 none; 10:24
    // whether it has an ability (Friend::equipedAbility); its magic points (10:50 YS::SHEET's, 1:276 its use's spent,
    // 10:43 that use set); 10:53 / 10:54 an effect it watches and whether it still plays; 2:56 the player's lock-on's
    // target (YS::BTLOBJ::lockon_target); 6:61 whether a script's thread holds an object (YS::OBJ::is_hook)
    virtual bool in_battle()
    {
        return false;
    }
    virtual void friend_target(Actor & /*a*/, int /*mode*/, Object * /*target*/) {}
    virtual void friend_turning(Actor & /*a*/, bool /*own*/) {}
    virtual void friend_action_clear(Actor & /*a*/) {}
    virtual std::uint32_t friend_action_param(Actor & /*a*/, int /*id*/)
    {
        return 0;
    }
    virtual bool friend_ability(Actor & /*a*/, int /*ability*/)
    {
        return false;
    }
    virtual float magic_points(Object & /*o*/)
    {
        return 0.0F;
    }
    virtual void magic_use(Object & /*o*/) {}
    virtual void magic_use_set(Object & /*o*/, int /*points*/) {}
    virtual void friend_watch(Actor & /*a*/, std::uint32_t /*effect*/) {}
    virtual bool friend_watching(Actor & /*a*/, std::uint32_t /*effect*/)
    {
        return false;
    }
    virtual Object *lockon_target(Object & /*o*/)
    {
        return nullptr;
    }
    virtual bool hooked(Object & /*o*/)
    {
        return false;
    }
    // A way along the ground (`way`, KH2's cm) from `place` swept by a body's sphere of `radius` (KH2's cm; 0 the actor's
    // own) against the room's walls (Days's: its wall list, which its ray never meets): up to the first wall, the rest
    // slid along it when `slide`; the way made back in `way`, whether a wall stopped it
    virtual bool wall_sweep(Actor & /*a*/, const Vec3 & /*place*/, float /*radius*/, Vec3 & /*way*/, bool /*slide*/)
    {
        return false;
    }
    // A point's distance to what a target record names (6:9, FUN_1403bdd90: its shape -- an attack's sphere, an
    // actor's aim shape -- FUN_1403c99f0)
    virtual bool record_distance(std::uint32_t /*record*/, const Vec3 & /*point*/, float & /*out*/)
    {
        return false;
    }
    // An attack's routine run when it hits (2:38: + 0x98 the machine, + 0x9c the routine, + 0xa0 its value)
    virtual void attack_on_hit(std::uint32_t /*at*/, Actor & /*by*/, std::uint32_t /*routine*/, std::uint32_t /*value*/)
    {
    }
    // An attack's routine run when it is reflected (2:74: + 0xa4 the machine, + 0xa8 the routine, + 0xac its value)
    virtual void attack_on_reflect(std::uint32_t /*at*/, Actor & /*by*/, std::uint32_t /*routine*/,
                                   std::uint32_t /*value*/)
    {
    }
    // An object's aim point in the world (1:88: its record's point, FUN_1403b5cb0 -> FUN_1403bdde0): a KH2 enemy's
    // aim shape, a player's middle
    virtual bool aim_point(Object & /*o*/, Vec3 & /*out*/)
    {
        return false;
    }
    // 1:165: a reaction command offered KH2's player this tick by `by`'s script (FUN_1403b2850 on his + 0xdd0), from
    // `source`: what a press sends it (trigger 11)
    virtual void offer(Actor & /*by*/, int /*command*/, Object * /*source*/) {}
    // 0:81: whether this game's own player pressed the button of KH2's pad bit `bit` this frame (the PS2 pad's bits:
    // 4 to 7 the D-pad, 10 L1, 11 R1, 12 triangle, 13 circle, 14 cross, 15 square, 3 Start, 0 Select)
    virtual bool pad_pressed(int /*bit*/)
    {
        return false;
    }
    // 1:237: the four top commands of KH2's player's command menu (Attack, Magic, Items, Drive) this tick, given by
    // `by`'s script (FUN_1403b2910 on his + 0xdd0), each sent to `target` when chosen
    virtual void override_top(Actor & /*by*/, const std::array<int, 4> & /*commands*/, Object * /*target*/) {}
    // KH2's actors of the field (6:5's search goes through them, FUN_1403bf4f0)
    virtual std::vector<Actor *> actors()
    {
        return {};
    }
    // and the objects that are no enemies with them (the Hydra's Phil, Pegasus, Megara): what a search by entry or by
    // unit argument finds (1:114, 1:292, 1:323)
    virtual std::vector<Actor *> objects()
    {
        return actors();
    }
    // An actor's shapes of a kind in its collision groups on now (FUN_1403b4f10), each its group and its point in KH2's
    // world (FUN_1403c9c00): 1:140, 6:5 and 6:8 look for kind 3, what is aimed at
    struct ShapeAt final {
        int group = 0;
        Vec3 at{};
        float radius = 0.0F;
    };
    virtual std::vector<ShapeAt> shapes(Actor &, int /*kind*/)
    {
        return {};
    }
    // Whether the motion it plays has its take-off still ahead (a trigger 0 at or past its time): a jump then waits
    // for it (FUN_14040dd70's event 0)
    virtual bool takes_off_later(Actor &)
    {
        return false;
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
    // A state's name shouted to every object (1:90: FUN_1403cacd0's list, which FUN_1403c9d40 sends once a frame to
    // every object, at once or each after a time at random below `delay`): each runs its own state of that name's
    // shout routine (StateMachine::shout)
    virtual void broadcast(Actor &, const std::string & /*name*/, float /*delay*/) {}
    // Its health record (2:9's, at + 0x5c0) given its health now and at most
    virtual void health(Actor &) {}
    // The battle entry its script names for it (2:9's second value: 00battle.bin's enmp): its KH2 health, which the
    // shell gives it in Days's scale
    virtual void battle_entry(Actor &, int /*entry*/) {}
    // Its health made `hp` by its script (1:233, kept within its record's least and most)
    virtual void set_health(Actor &, int /*hp*/) {}

    // One of Days's players in a script's hands (KH2's player is an actor as its enemies are; Days's are the shell's):
    // what the calls do to one goes to the world. 1:29: `player` plays motion `slot` of `by`'s set from its first frame
    // over `blend` (FUN_1403c6cc0: the set made the player's, + 0x2b8): false when the world cannot
    virtual bool player_motion(Object & /*player*/, Actor & /*by*/, int /*slot*/, float /*blend*/)
    {
        return false;
    }
    // 1:29 on one of KH2's actors: its own part of `by`'s slot (the slot's variant named for its character, its object
    // entry's + 7: "0190" Riku's body, "0191" his weapon) played on it, its own set kept for its own motions: false
    // when the slot has no part of its (the call then plays `by`'s set on it as before)
    virtual bool capture_motion(Actor & /*target*/, Actor & /*by*/, int /*slot*/, float /*blend*/)
    {
        return false;
    }
    // 1:13 on a player: a motion of its own asked for (FUN_1403c8310), 0 its idle -- the script gives it back
    virtual void player_own_motion(Object & /*player*/, int /*id*/) {}
    // 1:11, 1:12, 1:61 on a player: a motion of its own started at once (start_motion, not the queue): given back now
    // (Zexion's illusion, ending, starts the player's idle so)
    virtual void player_own_motion_now(Object &player, int id)
    {
        player_own_motion(player, id);
    }
    // 1:148 / 1:1, 1:4, 1:79 on a player: put at a place, turned to a yaw (KH2's)
    virtual void player_place(Object & /*player*/, const Vec3 & /*place*/) {}
    virtual void player_yaw(Object & /*player*/, float /*yaw*/) {}
    // 1:89 on a player: this tick's move along a way (made one long) at a speed (FUN_1403b9a70: KH2's cm a tick; the
    // Magnum Loader's Quick Blade dashes the player to it so)
    virtual void player_move(Object & /*player*/, const Vec3 & /*way*/, float /*speed*/) {}
    // 1:111 on a player: his steering along a way at a speed, each tick till another (the Armored Knight's Rising Sun)
    virtual void player_steer(Object & /*player*/, const Vec3 & /*way*/, float /*speed*/) {}
    // 1:18 / 1:102 on a player: carried by one of KH2's actors (`by`, none: let down) -- he stands where it does, the
    // camera with him (Final Xemnas's grab puts Sora on Riku, whom the pad then moves); 1:133 / 1:134 on a player his
    // model hidden and shown (YS::OBJ::hide, show)
    virtual void player_carry(Object & /*player*/, Object * /*by*/) {}
    virtual void player_hide(Object & /*player*/, bool /*hidden*/) {}
    // 1:227 on a player: KH2's jump at once (YS::ACTION::jump: his own action, the jump, taken back) -- an enemy that
    // holds him lets him go to his own control then (Final Xemnas's throw's thread ends so)
    virtual void player_let_go(Object & /*player*/, const char * /*why*/) {}
    // 1:112 on a player: a script's routine as the player's thread (the one before ended)
    virtual void player_thread(Object & /*player*/, ai::Machine & /*machine*/, std::uint32_t /*thread*/) {}
    // 1:242: KH2's player turned into the form of the object entry `entry` (YS::trap_player_capture_form ->
    // YS::CHANGEFORM::Capture: his form 12; Luxord's card 0x601 and die 0x602), the player `by`'s script plays against;
    // 1:243 turned back (CHANGEFORM::CaptureEnd); 6:2 his form (Ts::trap_player_get_form: 12 while turned, else 0)
    virtual void capture_form(Actor & /*by*/, int /*entry*/) {}
    virtual void capture_end(Actor & /*by*/) {}
    virtual int player_form(Actor & /*by*/)
    {
        return 0;
    }
    // 6:27 and 6:29: the turned player's action (YS::OBJ::change_action: 0x2b ACTION_DICE, 0x2e ACTION_CARD); 6:28
    // and 6:30 their numbers (Ts::ACTION_DICE::SetSpec's four, Ts::ACTION_CARD::SetSpec's seven), as the script gives them
    virtual void form_action(Actor & /*by*/, int /*action*/) {}
    virtual void form_spec(int /*action*/, const std::vector<float> & /*numbers*/) {}
    // 1:18: `carried` put on `by` (FUN_1403d9fe0: + 0x6a0 its carrier, + 0x6a4 the bone -- 0x4000 its place, not a
    // bone -- + 0x6a6 flags, its own place on it made 0, class 3); 1:102 taken off again (FUN_1403da250)
    virtual void carry(Actor & /*carried*/, Object & /*by*/, int /*bone*/, int /*flags*/) {}
    virtual void uncarry(Actor & /*carried*/) {}
    // 1:157: the event sequence of that name in the actor's .a.us (its entries of type 0x16, FUN_1403db2f0), played by
    // KH2's one event player (FUN_1402cc4b0, the one before ended), its camera placed from `target` (FUN_1403ad270)
    virtual void event(Actor & /*actor*/, const std::string & /*name*/, Object * /*target*/) {}
    // whether one of KH2's events plays now (1:34: an object's camera sequence, 1:164's)
    virtual bool event_playing() const
    {
        return false;
    }
    // 1:17: it made to leave the field at once (its class's leave)
    virtual void leave(Actor & /*actor*/) {}
    // 6:46: a stop (its number) put on every object of the field but two (none when null); 6:47: that stop taken off
    // every object
    virtual void stop_all(int /*stop*/, Object * /*but*/, Object * /*nor*/) {}
    virtual void stop_end_all(int /*stop*/) {}
    // 1:41 and the game's own (an enemy's end: 75): a signal to every script of the field and its mission, their
    // trigger 10 (id, argument), as KH2's YS::SIGNAL::Call queues it
    virtual void signal(int /*id*/, int /*argument*/) {}
    // a call of kind 4 on the field's KH2 mission (its counts, its bar, its end), its arguments as the script gives
    // them (4:6, 4:20, 4:29 the counter's index; 4:5 and 4:41 the value, then the index): what it gives back
    virtual int mission(unsigned /*number*/, int /*first*/, int /*second*/)
    {
        return 0;
    }
    // 2:77, 2:78: a vacuum's record (2:77's, in the scripts' memory) made, let go: the field pulls its objects by it
    virtual void vacuum(std::uint32_t /*record*/, bool /*alive*/) {}
    // 1:0: an object made by its entry's number (00objentry.bin's + 0) and set going, its script started; its address
    // (an AI handle's + 4 names it), 0 when none can be made
    virtual std::uint32_t appear(Actor & /*by*/, std::uint32_t /*entry*/)
    {
        return 0;
    }
    // 1:328: the actor counted as defeated by what made it (its wave), its script going on (FUN_1403d4a40)
    virtual void dead_mark(Actor & /*a*/) {}
    // 1:177: the walk path of number `number` (-1 the first) of the actor's battle, its points in KH2's world into
    // `out`; whether it has one
    virtual bool walk_path(Actor & /*a*/, int /*number*/, std::vector<Vec3> & /*out*/)
    {
        return false;
    }
    // a float of KH2's battle constants (03system.bin's "pref" BAR, its "sstm": from its record's + 4 on; 2:86 reads
    // + 0x30 and + 0x34), `fallback` when the mod has none
    virtual float system_value(std::uint32_t /*offset*/, float fallback)
    {
        return fallback;
    }
    // the field's mission's gauge `index` (YS::MISSION's SetGauge, AddGauge, GetGaugeRatio): 8 set to `value`, 9
    // `value` added, 12 its level over its most given back
    virtual float mission_gauge(unsigned /*number*/, int /*index*/, float /*value*/)
    {
        return 0.0F;
    }
    // the field's mission's warning on a piece of `kind` (5 a counter, 6 a gauge) `index` turned on or off
    // (YS::MISSION::WarningCount, WarningGauge: its set_warning, reset_warning)
    virtual void mission_warning(int /*kind*/, int /*index*/, bool /*on*/) {}
    // 1:169: an effect let go of the object it follows (its bound flag cleared, FUN_1402be1c0): it stays where it is
    virtual void effect_unbind(std::uint32_t /*handle*/) {}
    // 2:24: the object that owns an attack (its + 0x10: what 2:10 was given), null none
    virtual Object *attack_owner(std::uint32_t /*at*/)
    {
        return nullptr;
    }
    // 1:225 (trap_obj_is_star): whether an object is invincible now -- a player held in a motion whose range 27 runs
    // (YS::MOTION's FLAG_STAR)
    virtual bool star(const Object & /*o*/)
    {
        return false;
    }
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
    // a jump waiting for its motion's take-off (1:55, 1:168, 1:156 keep their class in + 0x648, which a motion's
    // trigger 0 gives the actor: FUN_14040dd70's event 0): on the ground until then, the motion's root moving it
    std::optional<Jump> take_off;
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
    // class 5, flying (1:58, trap_obj_fly: FUN_140421ce0; its movement FUN_140421c80): at a velocity of its own,
    // keeping `dash_keep` of it an evaluation (eased toward the steering when steered), until its class changes
    bool dashing = false;
    float dash_keep = 0.0F;
    // a hit's slide (2:3, 2:87: FUN_1403d4960 -- + 0xd24 its way, made one long, + 0xd30 its speed a tick, a tenth of
    // what the script gives; FUN_1403d4570 each tick): the way times the speed, the speed then 0.9 of itself, over
    // once below 1 (KH2's cm) or when a wall stops it (FUN_1403b9420)
    Vec3 slide{};
    float slide_speed = 0.0F;
    // its sheet's rates by KH2's elements (1:311, FUN_1403c2650: + 0x1a8 + element, its weakness times a rate, in
    // percent): the share of a Days hit of that element it takes; 100 each at first
    std::array<int, 8> element_rate{100, 100, 100, 100, 100, 100, 100, 100};
    // the elements its motions' triggers 1 are kept from starting (1:270, 1:271: its motion controller's + 0x1b8)
    std::set<int> muted_effects;
    // the thread a script attached to it (1:112, FUN_1403d76d0: + 0xd48, the one before ended), in the machine that
    // made it
    ai::Machine *side_machine = nullptr;
    std::uint32_t side_thread = 0;
    void attach_thread(ai::Machine &m, std::uint32_t id);
    // whether that thread still runs (YS::OBJ::is_hook: + 0xd48 set; 6:61): a party member's AI stands aside then
    bool hooked() const;
    // a damage motion of its plays (YS::ACT::is_damage_motion: a hit's reaction motion, 10 to 39): its states under
    // priority 300 wait each tick it does (StateMachine::tick)
    bool damage_motion = false;
    // its fading (+ 0xa08, FUN_1403b7810: a value and its change a tick; 1:19, 1:20, 1:28): 1 seen, 0 gone
    float fade = 1.0F, fade_step = 0.0F;
    // its model's parts in stealth (1:194, 1:195: kn::ModelObj::setStealth, resetStealth on + 0x7b0's model, a bit a
    // part group; KH2 draws them as a refraction): the port draws the whole model faint while any is
    std::uint32_t stealth = 0;
    // hidden by its script (1:133: YS::OBJ's VTABLE hide, its flags' FLAG_HIDE_MODEL and FLAG_HIDE_SHADOW, + 0x10c
    // 0x300) till shown (1:134: FLAG_HIDE_MODEL cleared): its model not drawn, nor its effects that fade with it
    // (ryj::PAX::MANAGER::set_obj_fade: YS::OBJ::is_hide)
    bool hidden = false;
    // its model's parts hidden (1:109 hides one, 1:108 shows it: kn::ModelSKL::hidePart and showPart on + 0x7b0's
    // model, each subpart of that part -- its attribute word's bits 6-10 -- drawn or not; -1 every part): bit n part n
    std::uint32_t hidden_parts = 0;
    // its model's parts KH2's own rules fade out, drawn as hidden (bit n part n): a pirate's out of the moonlight,
    // parts 10 to 19 (YS::OBJ::exec_pirate at Place 0: PARTS_COLOR of 0xffc00 faded to none, of 0x3ff00000 drawn; 1:40)
    std::uint32_t faded_parts = 0;
    // its parts' colours (1:185 set, faded in over ticks; 1:186 faded out: KH2 keeps eight, + 0xac4, a part mask, its
    // RGBA bytes -- 128 as it is -- and a strength run as the fade is, FUN_1403b7810): the port draws no part's tint,
    // only a part whose colour's alpha times its strength falls below half as hidden (coloured_out)
    struct PartColour final {
        std::uint32_t mask = 0;
        std::uint8_t rgba[4] = {128, 128, 128, 128};
        float strength = 0.0F, step = 0.0F;
    };
    std::vector<PartColour> part_colours;
    std::uint32_t coloured_out() const
    {
        std::uint32_t out = 0;
        for (const PartColour &c : part_colours)
            if (float(c.rgba[3]) * c.strength + 128.0F * (1.0F - c.strength) < 64.0F)
                out |= c.mask;
        return out;
    }
    std::uint32_t drawn_hidden() const { return hidden_parts | faded_parts | coloured_out(); }
    std::vector<Vec3> path;        // the walk path it is on (1:177; KH2's follower at + 0x630), KH2's world; 5:14
    // turned about all three axes (8:2: + 0x680 the turns, + 0x9b8 bit 3), x then y then z as an effect's element is,
    // KH2's world; its model drawn so (Roxas's keyblades spinning), else by its yaw alone
    bool turned_all = false;
    Vec3 turn_all{};
    std::uint32_t target_slot = 0; // its slot's address in the scripts' memory (2:23), made on first use

    // A motion played (FUN_1403c86a0): `restart` false leaves one already playing as it is (1:12). Whether the set
    // has it (none: the call does nothing, as KH2's controller does for an empty slot).
    bool play(int id, float blend, float start, bool restart = true);
    // A tick begins (before its script runs): a motion started in the tick before moves on in this one's step.
    void begin_tick() { motion.fresh = false; }
    // The motion and the jump moved on by `ticks`: the ended bit set at a motion's end, a loop taken, a blend counted;
    // the travel this made in KH2's world (the root's travel turned by the yaw, the script's move, the jump) given
    // back. The shell puts it into Days's place and gives the place back (set_place).
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
