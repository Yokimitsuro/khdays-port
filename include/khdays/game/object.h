#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

// The per-frame object model — the native form of the DS object/state machine
// the game actually uses (traced in khdays-decomp). Each InstantiateClass object
// holds a state function (obj+0x14); the object walker func_02023adc calls it
// once per frame; the constructor's return value is the object's initial state;
// a state switches by returning the next one (or 0 to stay). Here a state is a
// callable run each frame that transitions the object via go_to() and ends it
// via finish() — a per-object coroutine.
namespace khdays::game {

class Object final {
public:
    using State = std::function<void(Object&)>;

    explicit Object(State initial) : state_(std::move(initial)) {}

    // Adopt a new state (used next frame onward) — mirrors returning a new
    // state function from obj+0x14.
    void go_to(State next) { state_ = std::move(next); }

    // End the object; it leaves the list on the next update (the dead sentinel).
    void finish() { alive_ = false; }
    bool alive() const { return alive_; }

    // Run the current state once.
    void update() {
        if (alive_ && state_) {
            state_(*this);
        }
    }

    // Opaque context a state can carry (the DS aux storage). The owner keeps it
    // alive; the object does not.
    void* user = nullptr;

private:
    State state_;
    bool alive_ = true;
};

// The object list — the native func_02023adc walk. Spawn objects with their
// initial state (like InstantiateClass), and update() ticks them all and drops
// the finished ones. Objects spawned during update() first run next frame.
class ObjectList final {
public:
    Object& spawn(Object::State initial);
    void update();

    std::size_t size() const { return objects_.size(); }
    bool empty() const { return objects_.empty(); }

private:
    std::vector<std::unique_ptr<Object>> objects_;
    std::vector<std::unique_ptr<Object>> incoming_;
    bool updating_ = false;
};

}  // namespace khdays::game
