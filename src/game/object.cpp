#include "khdays/game/object.h"

#include <algorithm>
#include <utility>

namespace khdays::game {

Object& ObjectList::spawn(Object::State initial) {
    auto object = std::make_unique<Object>(std::move(initial));
    Object& reference = *object;
    // Spawning while walking would resize the list mid-iteration; stage it.
    (updating_ ? incoming_ : objects_).push_back(std::move(object));
    return reference;
}

void ObjectList::update() {
    updating_ = true;
    // Index-based: the vector is not resized during the walk (spawns are staged).
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        objects_[i]->update();
    }
    updating_ = false;

    objects_.erase(
        std::remove_if(
            objects_.begin(), objects_.end(),
            [](const std::unique_ptr<Object>& o) { return !o->alive(); }),
        objects_.end());

    for (auto& object : incoming_) {
        objects_.push_back(std::move(object));
    }
    incoming_.clear();
}

}  // namespace khdays::game
