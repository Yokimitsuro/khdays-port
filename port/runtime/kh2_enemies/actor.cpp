// A KINGDOM HEARTS II enemy as its script sees it (actor.h): its memory at KH2's offsets, its state machine as
// FUN_1403cae60, FUN_1403cab00, FUN_1403cad00, FUN_1403caec0 and FUN_1403ca2c0 run it, its start as FUN_1403b5f30.
#include "actor.h"

#include <cstring>

namespace khdays::kh2_enemies {
namespace {

// every actor by (its memory, its address): a call names another actor by its address
std::map<std::pair<const ai::Memory *, std::uint32_t>, Actor *> &actors()
{
    static std::map<std::pair<const ai::Memory *, std::uint32_t>, Actor *> all;
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

} // namespace

std::string name_at(ai::Memory &memory, std::uint32_t address)
{
    std::string s;
    for (std::uint32_t i = 0; i < 64; ++i) {
        char c = 0;
        if (!memory.read(address + i, &c, 1) || c == 0)
            break;
        s.push_back(c);
    }
    return s;
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
        out.on_enter = u16(0x16);
        return true;
    }
    return false;
}

// FUN_1403caec0: the current state's leave routine run at once, its thread ended
void StateMachine::leave(Actor &a)
{
    if (!current_)
        return;
    ai::Machine &m = a.machine();
    const ai::Value handle = ai::Value::of_address(handle_);
    if (find_thread(m.main_thread(), thread_id_) != nullptr) {
        if (current_->on_leave != 0)
            m.run_now(current_->on_leave, {&handle, 1});
        m.stop(find_thread(m.main_thread(), thread_id_));
    }
    current_.reset();
    thread_id_ = 0;
    locked_ = false;
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
        ai::Thread *t = find_thread(a.machine().main_thread(), thread_id_);
        if (t == nullptr || t->status == ai::Status::Done || t->status == ai::Status::Stopped) {
            current_.reset();
            thread_id_ = 0;
            locked_ = false;
        }
    }
    while (!current_ && !queued_.empty()) {
        const std::string name = queued_.front();
        queued_.erase(queued_.begin());
        StateEntry e;
        if (find(a, name, e))
            go(a, e);
    }
}

// ------------------------------------------------------------------------------------------------- the actor

Actor::Actor(std::shared_ptr<const ai::Script> script, ai::Memory &memory, const ai::Calls &calls, std::string kind)
    : kind_(std::move(kind)), memory_(memory), bytes_(field::kSize, 0)
{
    address_ = memory_.add_view(*this, field::kSize, kind_ + "'s actor");
    machine_ = std::make_unique<ai::Machine>(std::move(script), memory_, calls, this);
    actors()[{&memory_, address_}] = this;
}

Actor::~Actor()
{
    actors().erase({&memory_, address_});
    machine_.reset();
    memory_.remove(address_);
}

void Actor::read(std::uint32_t offset, void *out, std::size_t n)
{
    std::memcpy(out, bytes_.data() + offset, n);
    if (watch.on)
        ++watch.reads[offset];
}

void Actor::write(std::uint32_t offset, const void *in, std::size_t n)
{
    std::memcpy(bytes_.data() + offset, in, n);
    if (watch.on)
        ++watch.writes[offset];
}

float Actor::f(std::uint32_t offset) const
{
    float v = 0;
    if (offset + 4 <= bytes_.size())
        std::memcpy(&v, bytes_.data() + offset, 4);
    return v;
}

std::uint32_t Actor::u32(std::uint32_t offset) const
{
    std::uint32_t v = 0;
    if (offset + 4 <= bytes_.size())
        std::memcpy(&v, bytes_.data() + offset, 4);
    return v;
}

void Actor::set_f(std::uint32_t offset, float v)
{
    if (offset + 4 <= bytes_.size())
        std::memcpy(bytes_.data() + offset, &v, 4);
}

void Actor::set_u32(std::uint32_t offset, std::uint32_t v)
{
    if (offset + 4 <= bytes_.size())
        std::memcpy(bytes_.data() + offset, &v, 4);
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

Actor *Actor::at(ai::Memory &memory, std::uint32_t address)
{
    const auto it = actors().find({&memory, address});
    return it == actors().end() ? nullptr : it->second;
}

Actor *Actor::of_handle(ai::Machine &m, std::uint32_t handle)
{
    return at(m.memory(), m.memory().u32(handle + 4));
}

} // namespace khdays::kh2_enemies
