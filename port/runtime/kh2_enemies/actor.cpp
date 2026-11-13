// A KINGDOM HEARTS II enemy as its script sees it (actor.h): its memory at KH2's offsets, its state machine as
// FUN_1403cae60, FUN_1403cab00, FUN_1403cad00, FUN_1403caec0 and FUN_1403ca2c0 run it, its start as FUN_1403b5f30, its
// motions and travel as the actor's update (FUN_1403b8ae3) adds them up.
#include "actor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace khdays::kh2_enemies {
namespace {

// every object by (its memory, its address): a call names another object by its address
std::map<std::pair<const ai::Memory *, std::uint32_t>, Object *> &objects()
{
    static std::map<std::pair<const ai::Memory *, std::uint32_t>, Object *> all;
    return all;
}

constexpr std::uint32_t kStates = 128;      // a state table's entries (FUN_1403ca0e0, FUN_1403ca700)
constexpr std::uint32_t kStateSize = 0x20;
constexpr std::size_t kQueue = 15;          // the ring of 16 keeps one free (FUN_1403cab00)

ai::Thread *find_thread(ai::Thread *t, std::uint32_t id)
{
    if (t == nullptr)
        return nullptr;
    if (t->id == id)
        return t;
    for (auto &c : t->children)
        if (ai::Thread *f = find_thread(c.get(), id))
            return f;
    return nullptr;
}

Vec3 minus(const Vec3 &a, const Vec3 &b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

} // namespace

std::string name_at(ai::Memory &memory, std::uint32_t address)
{
    std::string s;
    for (std::uint32_t i = 0;; ++i) {
        char c = 0;
        if (!memory.read(address + i, &c, 1) || c == 0)
            break;
        s.push_back(c);
    }
    return s;
}

bool is_player(Object *o)
{
    return o != nullptr && o->actor() == nullptr && (o->u32(field::kFlags6c8) & bit::kPlayer) != 0;
}

Vec3 facing_of(float yaw)
{
    return {std::sin(yaw), 0.0F, std::cos(yaw)};
}

Vec3 turned(const Vec3 &v, float radians)
{
    const float c = std::cos(radians), s = std::sin(radians);
    return {v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c};
}

// ------------------------------------------------------------------------------------------------- states

bool StateMachine::find(Actor &a, const std::string &name, StateEntry &out) const
{
    if (table_ == 0)
        return false;
    ai::Memory &m = a.machine().memory();
    for (std::uint32_t i = 0; i < kStates; ++i) {
        const std::uint32_t at = table_ + kStateSize * i;
        const std::uint32_t id = m.u32(at);
        if (id == 0 || name_at(m, id) != name)
            continue;
        std::uint8_t b[kStateSize] = {};
        m.read(at, b, sizeof b);
        const auto u16 = [&](std::size_t o) { return std::uint16_t(b[o] | (b[o + 1] << 8)); };
        out = {};
        out.at = at;
        out.name = name;
        out.priority = u16(4);
        out.flags = u16(6);
        out.routine = u16(8);
        out.on_leave = u16(0xa);
        out.on_air = u16(0xc);
        out.on_land = u16(0xe);
        out.on_shout = u16(0x14);
        out.on_enter = u16(0x16);
        return true;
    }
    return false;
}

void StateMachine::event(Actor &a, int n)
{
    if (!current_ || waiting_)
        return;
    const std::uint16_t routine = n == 1 ? current_->on_air : n == 2 ? current_->on_land : 0;
    if (routine == 0)
        return;
    const ai::Value handle = ai::Value::of_address(handle_);
    a.machine().run_now(routine, {&handle, 1});
}

bool StateMachine::shout(Actor &a, const std::string &name, std::uint32_t from)
{
    StateEntry e;
    if (table_ == 0 || !find(a, name, e) || e.on_shout == 0)
        return false;
    const ai::Value values[2] = {ai::Value::of_address(handle_), ai::Value::of_address(from)};
    a.machine().run_now(e.on_shout, values);
    return true;
}

// FUN_1403caec0: the current state's leave routine run at once, its thread ended
// FUN_1403caec0: the state's leave routine (+ 0xa) run, whether its routine still runs or has ended (KH2 keeps the
// routine's thread at + 0x120 until it leaves; the machine's tick leaves a state whose routine ended so,
// FUN_1403ca550), and its routine stopped. The state is let go first: a leave routine entering another does not leave
// this one again.
void StateMachine::leave(Actor &a)
{
    if (!current_)
        return;
    ai::Machine &m = a.machine();
    const ai::Value handle = ai::Value::of_address(handle_);
    const std::uint16_t on_leave = current_->on_leave;
    ai::Thread *routine = find_thread(m.main_thread(), thread_id_);
    current_.reset();
    thread_id_ = 0;
    locked_ = false;
    waiting_ = false;
    if (on_leave != 0)
        m.run_now(on_leave, {&handle, 1});
    if (routine != nullptr)
        m.stop(routine);
}

// FUN_1403ca550: a state whose flag bit 0 is clear begins only once the actor is not held by a reaction (+ 0x18c bit
// 9) and is on the ground (FUN_1403ba9a0: its class's bit 0, + 0x18c & 0x1000020, + 0x120 bit 23), unless it is
// carried (FUN_1403da520: + 0x6a0); once begun it runs on whatever happens (the bit, + 300's bit 2, cleared). Its
// routine's thread is made at once and held until then.
void StateMachine::wait_or_begin(Actor &a)
{
    if (waiting_) {
        const bool off_ground = a.jump.on || (a.u32(field::kFlags) & bit::kInAir) != 0 ||
                                (a.u32(field::kStatus) & 0x1000020u) != 0;
        const bool carried = a.u32(field::kCarrier) != 0;
        waiting_ = (a.u32(field::kStatus) & bit::kHeld) != 0 || (off_ground && !carried);
    }
    if (ai::Thread *t = find_thread(a.machine().main_thread(), thread_id_))
        t->held = waiting_;
}

// FUN_1403cad00: the current left, the state's routine a child of the object's thread, its enter routine run at once
void StateMachine::go(Actor &a, const StateEntry &e)
{
    ai::Machine &m = a.machine();
    ai::Thread *main = m.main_thread();
    if (main == nullptr || main->status == ai::Status::Done || main->status == ai::Status::Stopped)
        return;
    leave(a);
    const ai::Value handle = ai::Value::of_address(handle_);
    ai::Thread *t = m.start_routine(main, e.routine, {&handle, 1});
    thread_id_ = t != nullptr ? t->id : 0;
    current_ = e;
    locked_ = (e.flags & 2) != 0;
    waiting_ = (e.flags & 1) == 0;
    wait_or_begin(a);
    if (entered)
        entered(e.name);
    if (e.on_enter != 0)
        m.run_now(e.on_enter, {&handle, 1});
}

bool StateMachine::enter(Actor &a, const std::string &name)
{
    StateEntry e;
    if (locked_ || !find(a, name, e))
        return false;
    queued_.clear();
    go(a, e);
    return true;
}

bool StateMachine::enter_over(Actor &a, const std::string &name)
{
    StateEntry e;
    if (locked_ || !find(a, name, e) || (current_ && current_->priority > e.priority))
        return false;
    queued_.clear();
    go(a, e);
    return true;
}

bool StateMachine::queue(Actor &a, const std::string &name)
{
    StateEntry e;
    if (locked_ || !find(a, name, e) || queued_.size() >= kQueue)
        return false;
    queued_.push_back(name);
    return true;
}

bool StateMachine::pending() const
{
    return !queued_.empty() || current_.has_value();
}

void StateMachine::clear(Actor &a)
{
    if (locked_)
        return;
    leave(a);
    queued_.clear();
}

// The current state's thread over: it is no longer the state (whether KH2 runs its leave routine then is not known;
// it does not here), and the next queued one is entered (FUN_1403ca2c0).
void StateMachine::tick(Actor &a)
{
    if (current_) {
        // its routine ended: the state left as KH2 leaves it then (FUN_1403ca550 -> FUN_1403caec0), its leave routine
        // run -- the Sorcerer's cubes clear their "busy" so after a shield (until 2026-11-11 the port skipped it, and
        // they never took another order)
        ai::Thread *t = find_thread(a.machine().main_thread(), thread_id_);
        if (t == nullptr || t->status == ai::Status::Done || t->status == ai::Status::Stopped)
            leave(a);
    }
    if (current_ && waiting_)
        wait_or_begin(a);
    while (!current_ && !queued_.empty()) {
        const std::string name = queued_.front();
        queued_.erase(queued_.begin());
        StateEntry e;
        if (find(a, name, e))
            go(a, e);
    }
}

void Actor::attach_thread(ai::Machine &m, std::uint32_t id)
{
    if (side_machine != nullptr && side_thread != 0)
        if (ai::Thread *old = find_thread(side_machine->main_thread(), side_thread))
            side_machine->stop(old);
    side_machine = &m;
    side_thread = id;
}

float World::ground(Actor &a)
{
    return a.place()[1];
}

// ------------------------------------------------------------------------------------------------- objects

Object::Object(ai::Memory &memory, std::string name) : name_(std::move(name)), memory_(memory), bytes_(field::kSize, 0)
{
    address_ = memory_.add_view(*this, field::kSize, name_ + "'s actor");
    objects()[{&memory_, address_}] = this;
    // its own handle at + 4, as KH2's actors keep it (the calls take an AI handle's + 4 as the object, and a script
    // copies an actor's first bytes to use them as one: Cerberus's 1:3, then 1:112 on the copy)
    set_u32(field::kSelf, address_);
}

Object::~Object()
{
    objects().erase({&memory_, address_});
    memory_.remove(address_);
}

void Object::read(std::uint32_t offset, void *out, std::size_t n)
{
    std::memcpy(out, bytes_.data() + offset, n);
    if (watch.on)
        ++watch.reads[offset];
}

void Object::write(std::uint32_t offset, const void *in, std::size_t n)
{
    std::memcpy(bytes_.data() + offset, in, n);
    if (watch.on)
        ++watch.writes[offset];
}

float Object::f(std::uint32_t offset) const
{
    float v = 0;
    if (offset + 4 <= bytes_.size())
        std::memcpy(&v, bytes_.data() + offset, 4);
    return v;
}

std::uint32_t Object::u32(std::uint32_t offset) const
{
    std::uint32_t v = 0;
    if (offset + 4 <= bytes_.size())
        std::memcpy(&v, bytes_.data() + offset, 4);
    return v;
}

void Object::set_f(std::uint32_t offset, float v)
{
    if (offset + 4 <= bytes_.size())
        std::memcpy(bytes_.data() + offset, &v, 4);
}

void Object::set_u32(std::uint32_t offset, std::uint32_t v)
{
    if (offset + 4 <= bytes_.size())
        std::memcpy(bytes_.data() + offset, &v, 4);
}

void Object::set_bits(std::uint32_t offset, std::uint32_t bits, bool on)
{
    set_u32(offset, on ? (u32(offset) | bits) : (u32(offset) & ~bits));
}

void Object::set_place(const Vec3 &p)
{
    for (std::uint32_t i = 0; i < 3; ++i)
        set_f(field::kPlace + 4 * i, p[i]);
    set_f(field::kPlace + 12, 1.0F);
}

// FUN_1403b6420's second way: the yaw at + 0x7a8, the facing at + 0x680 a direction with the yaw in its w
void Object::set_yaw(float yaw)
{
    const Vec3 d = facing_of(yaw);
    set_f(field::kYaw, yaw);
    for (std::uint32_t i = 0; i < 3; ++i)
        set_f(field::kFacing + 4 * i, d[i]);
    set_f(field::kFacing + 12, yaw);
}

Object *Object::at(ai::Memory &memory, std::uint32_t address)
{
    const auto it = objects().find({&memory, address});
    return it == objects().end() ? nullptr : it->second;
}

// ------------------------------------------------------------------------------------------------- the actor

Actor::Actor(std::shared_ptr<const ai::Script> script, ai::Memory &memory, const ai::Calls &calls, std::string kind)
    : Object(memory, std::move(kind))
{
    machine_ = std::make_unique<ai::Machine>(std::move(script), memory_, calls, this);
}

Actor::~Actor()
{
    machine_.reset();
}

// FUN_1403b5f30: trigger 0 at once with the object's address, then trigger 1 as its own thread
void Actor::start()
{
    const ai::Value self = ai::Value::of_address(address_);
    machine_->start_main(1);
    machine_->raise(0, {&self, 1});
}

void Actor::tick()
{
    states_.tick(*this);
    machine_->tick();
}

bool Actor::event(std::int32_t trigger, std::span<const ai::Value> args)
{
    return machine_->raise(trigger, args);
}

Actor *Actor::of_handle(ai::Machine &m, std::uint32_t handle)
{
    Object *o = at(m.memory(), m.memory().u32(handle + 4));
    return o != nullptr ? o->actor() : nullptr;
}

// FUN_1403c86a0 (YS::MOTION::start_motion): the motion set going from `start`; a blend above 2 ticks
// (DAT_140623b18), once a pose has been shown (+ 0x168 bit 0, the first pose made sets it), blends into its start
// frame from the pose shown (start_blend, FUN_1403c87c0: the count from 2); else its start frame's triggers play at
// once (exec_trigger(start, -1)). The ended bit cleared.
bool Actor::play(int id, float blend, float start, bool restart)
{
    if (motions == nullptr || !motions->has(id))
        return false;
    const bool loops = motions->loop_from(id) < motions->length(id);
    if (!restart && motion.id == id && (loops || !motion.ended))
        return true; // still playing (a loop plays on past its end)
    constexpr float kBlendFrom = 2.0F; // the least blend there is, and the count's first value
    const bool shown = motion.id >= 0;
    motion.id = id;
    motion.time = std::clamp(start, 0.0F, motions->length(id));
    if (shown && blend > kBlendFrom) {
        motion.blend = blend;
        motion.blend_count = kBlendFrom;
        motion.blend_keep = 1.0F - kBlendFrom / blend; // this tick's pose: count/length toward the start frame
    } else {
        motion.blend = motion.blend_count = motion.blend_keep = 0.0F;
    }
    motion.fresh = true;
    motion.cued = motion.time;
    motion.cue_start = true;
    motion.looped = false;
    motion.queued = -1;
    ++motion.started;
    motion.ended = false;
    set_bits(field::kStatus, bit::kMotionEnded, false);
    set_u32(field::kMotion, std::uint32_t(id));
    set_u32(field::kMotionNow, std::uint32_t(id));
    return true;
}

// FUN_1403b8ae3's sum for one tick: the motion's root travel turned by the yaw (the model's y up is KH2's -y), the
// script's move, the jump under KH2's gravity; then FUN_1403b8e4d clears the move
Vec3 Actor::step(float ticks)
{
    Vec3 travel{};
    if (motions != nullptr && motion.id >= 0 && motions->has(motion.id)) {
        const int id = motion.id;
        const float length = motions->length(id), loop = motions->loop_from(id);
        const float t0 = motion.time;
        Vec3 model{}; // no travel while it stands or blends (get_trans_direct gives none during a blend)
        // the ended bit is this tick's (add_time clears + 0x34 bit 1 first, then sets it when the time reaches the
        // motion's end: each tick one stands at its end, a loop's tick it goes round -- Scar counts the player's
        // pinned motion's rounds so)
        bool at_end = false;
        if (motion.fresh) {
            // started this tick: at its start frame till the next
        } else if (motion.blend > 0.0F) {
            motion.blend_count += ticks;
            if (motion.blend_count < motion.blend) {
                motion.blend_keep *= 1.0F - motion.blend_count / motion.blend;
            } else {
                // the blend over: on from its start by what the count went past its length (add_time: no end looked
                // at that tick)
                motion.time = std::min(t0 + motion.blend_count - motion.blend, length);
                motion.blend = motion.blend_count = motion.blend_keep = 0.0F;
                model = minus(motions->root(id, motion.time), motions->root(id, t0));
            }
        } else {
            float t1 = t0 + ticks;
            model = minus(motions->root(id, std::min(t1, length)), motions->root(id, t0));
            if (t1 >= length) {
                motion.ended = true;
                at_end = true;
                if (loop < length && length > 0.0F) { // a loop: on from its loop point
                    const float over = std::fmod(t1 - length, length - loop);
                    const Vec3 more = minus(motions->root(id, loop + over), motions->root(id, loop));
                    for (int i = 0; i < 3; ++i)
                        model[i] += more[i];
                    t1 = loop + over;
                    motion.looped = true;
                } else {
                    t1 = length;
                }
            }
            motion.time = t1;
        }
        set_bits(field::kStatus, bit::kMotionEnded, at_end);
        const Vec3 along = turned({model[0], 0.0F, model[2]}, yaw());
        travel = {along[0], -model[1], along[2]};
        if (jump.on && motion.after >= 0 && jump.up >= 0.0F && motion.id != motion.after) {
            const int after = motion.after;
            motion.after = -1;
            play(after, 4.0F, 0.0F); // the jump's top: its falling motion
        }
    }
    for (int i = 0; i < 3; ++i)
        travel[i] += move[i] * move_speed * ticks;
    move = {};
    move_speed = 0.0F;
    // a hit's slide (FUN_1403d4570): over below 1, else its way times its speed, the speed 0.9 of itself a tick
    if (slide_speed > 0.0F) {
        if (slide_speed < 1.0F) {
            slide_speed = 0.0F;
        } else {
            for (int i = 0; i < 3; ++i)
                travel[i] += slide[std::size_t(i)] * slide_speed * ticks;
            slide_speed *= std::pow(0.9F, ticks);
        }
    }
    // its class's movement, evaluated for two ticks at a time (every tick when steered past 100), its travel a tick
    // kept between evaluations: on the ground the steering, in the air the velocity steered or slowed (the jump's arc
    // its height)
    move_clock -= ticks;
    if (steer_speed > 100.0F)
        move_clock = -2.0F;
    if (move_clock < 0.0F) {
        const bool air = jump.on || (u32(field::kFlags) & bit::kInAir) != 0;
        if (dashing && !air) {
            // class 5: its own velocity kept, eased toward the steering when steered (FUN_140421c80)
            for (int i = 0; i < 3; ++i) {
                float &v = velocity[std::size_t(i)];
                v = steer_speed > 0.0F ? (1.0F - dash_keep) * steer[std::size_t(i)] * steer_speed + dash_keep * v
                                       : dash_keep * v;
            }
        } else if (!air) {
            for (int i = 0; i < 3; ++i)
                velocity[std::size_t(i)] = steer_speed > 0.0F ? steer[std::size_t(i)] * steer_speed : 0.0F;
        } else {
            // along only: both jumping classes give their height of the jump's own arc (FUN_1403cb360, FUN_1403cbdd0)
            const float keep = jump.on ? jump.keep : 0.9F, slow = jump.on ? jump.slow : 0.95F;
            for (int i = 0; i < 3; i += 2) {
                float &v = velocity[std::size_t(i)];
                v = steer_speed > 0.0F ? (1.0F - keep) * steer[std::size_t(i)] * steer_speed + keep * v : slow * v;
            }
            velocity[1] = 0.0F;
        }
        steered = velocity; // the travel a tick (the class gives two ticks' travel, halved)
        move_clock += 2.0F;
    }
    for (int i = 0; i < 3; ++i)
        travel[i] += steered[std::size_t(i)] * ticks;
    if (jump.on && jump.arc) {
        jump.time += ticks;
        constexpr float kHalfPi = 1.5707963F;
        const float h = jump.time >= jump.apex
                            ? jump.rise + jump.base + kKh2Pull * (jump.time - jump.apex) * (jump.time - jump.apex) * 0.5F
                            : std::sin(std::sin(jump.time / jump.apex * kHalfPi) * kHalfPi) * jump.rise + jump.base;
        const float dy = h - jump.height;
        jump.height = h;
        travel[1] += dy;
        jump.up = ticks > 0.0F ? dy / ticks : 0.0F;
        set_bits(field::kFlags, bit::kInAir, true);
    } else if (jump.on) {
        travel[1] += jump.up * ticks + 0.5F * jump.gravity * ticks * ticks;
        jump.up += jump.gravity * ticks;
        set_bits(field::kFlags, bit::kInAir, true);
    }
    // the travels KH2 keeps (+ 0xa28 this tick's, + 0xa38 the one before), a tick's worth
    for (std::uint32_t i = 0; i < 4; ++i)
        set_f(field::kTravelBefore + 4 * i, f(field::kTravel + 4 * i));
    for (std::uint32_t i = 0; i < 3; ++i)
        set_f(field::kTravel + 4 * i, ticks > 0.0F ? travel[i] / ticks : 0.0F);
    for (PartColour &c : part_colours)
        if (c.step != 0.0F) {
            c.strength = std::clamp(c.strength + c.step * ticks, 0.0F, 1.0F);
            if (c.strength <= 0.0F || c.strength >= 1.0F)
                c.step = 0.0F;
        }
    if (fade_step != 0.0F) {
        fade = std::clamp(fade + fade_step * ticks, 0.0F, 1.0F);
        if (fade <= 0.0F || fade >= 1.0F)
            fade_step = 0.0F;
    }
    return travel;
}

// A jump's end on the ground (FUN_1403cbb00's follow-on): its landing motion, the air bit cleared
void Actor::touch_down()
{
    const int landing = motion.landing;
    motion.after = -1;
    motion.landing = 5;
    if (landing >= 0)
        play(landing, 2.0F, 0.0F);
}

void Actor::land()
{
    if (!jump.on)
        return;
    jump = {};
    set_bits(field::kFlags, bit::kInAir, false);
    const int landing = motion.landing;
    motion.after = -1;
    motion.landing = 5;
    if (landing >= 0)
        play(landing, 2.0F, 0.0F);
}

} // namespace khdays::kh2_enemies
