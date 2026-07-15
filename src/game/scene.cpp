#include "khdays/game/scene.h"

#include <stdexcept>

namespace khdays::game {

void SceneManager::register_scene(SceneId id, SceneFactory factory) {
    registry_[id] = std::move(factory);
}

std::unique_ptr<Scene> SceneManager::make(SceneId id) const {
    const auto it = registry_.find(id);
    if (it == registry_.end() || !it->second) {
        throw std::runtime_error(
            "no scene registered for id " + std::to_string(id));
    }
    return it->second();
}

void SceneManager::enter(SceneId id, int arg) {
    current_ = make(id);
    current_id_ = id;
    current_arg_ = arg;
    if (observer_) {
        observer_(id, arg);
    }
    current_->on_enter(*this);
}

void SceneManager::start(SceneId first, int arg) {
    pending_.reset();
    enter(first, arg);
}

void SceneManager::change_scene(SceneId id, int arg) {
    pending_ = std::make_pair(id, arg);
}

void SceneManager::step() {
    ++frame_;
    if (current_) {
        current_->update(*this);
    }
    if (pending_) {
        const auto [id, arg] = *pending_;
        pending_.reset();
        if (current_) {
            current_->on_exit(*this);
        }
        enter(id, arg);
    }
}

void SceneManager::render(Renderer& renderer) {
    if (current_) {
        current_->render(*this, renderer);
    }
}

}  // namespace khdays::game
