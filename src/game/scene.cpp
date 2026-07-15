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
    ended_ = false;
    if (observer_) {
        observer_(id, arg);
    }
    current_->on_enter(*this);
}

void SceneManager::start(SceneId first, int arg) {
    pending_.reset();
    enter(first, arg);
}

void SceneManager::request_scene(SceneId id, int arg) {
    pending_ = std::make_pair(id, arg);
}

void SceneManager::change_scene(SceneId id, int arg) {
    request_scene(id, arg);
    ended_ = true;
}

void SceneManager::step() {
    ++frame_;
    if (current_) {
        current_->update(*this);
    }

    // A scene that ended with nowhere to go tears down: the flow has no scene
    // (the DS Game_PollSceneAlive-returns-0 path).
    if (current_ && ended_ && !pending_) {
        current_->on_exit(*this);
        current_.reset();
        current_id_ = kSceneNone;
        ended_ = false;
        return;
    }

    // Apply a pending transition only once the current scene has ended (or there
    // is none) — the dispatcher's teardown-then-load gate.
    if (pending_ && (!current_ || ended_)) {
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
