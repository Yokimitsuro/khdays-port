#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "khdays/game/input.h"
#include "khdays/game/renderer.h"

// The game-flow state machine — the native equivalent of the DS backbone traced
// in khdays-decomp (main @0x02000bcc → frame loop → scene/task framework). A
// Scene is a game state (boot logo, title, menu, mission); the SceneManager
// holds the current one, applies pending transitions, and drives one frame at a
// time. The DS-specific parts (VBlank sync, overlay loading, hardware init) are
// the platform layer's job and are not modelled here — only the flow.
namespace khdays::game {

class SceneManager;

// Stable scene identifiers, mirroring the DS scene ids (see the decompiled
// BootTask_Construct). More are added as khdays-decomp names each scene.
using SceneId = int;
inline constexpr SceneId kSceneNone = 0;
inline constexpr SceneId kSceneBootLogo = 1;   // fresh boot → the logo scene
inline constexpr SceneId kSceneContinue = 12;  // continue/other boot path

// One game state. Override the hooks that matter; the default is a no-op.
class Scene {
public:
    virtual ~Scene() = default;
    virtual void on_enter(SceneManager&) {}
    virtual void update(SceneManager&) {}                 // per-frame logic
    virtual void render(SceneManager&, Renderer&) {}       // per-frame draw
    virtual void on_exit(SceneManager&) {}
};

using SceneFactory = std::function<std::unique_ptr<Scene>()>;

// Owns the scene registry and the current scene, and applies transitions. A
// scene requests the next state with change_scene(); the switch happens at the
// end of the frame (mirroring the DS "poll scene alive → transition" step).
class SceneManager final {
public:
    // Register a factory for a scene id (idempotent replace).
    void register_scene(SceneId id, SceneFactory factory);

    // Enter the first scene directly (the boot task instantiating scene 1).
    void start(SceneId first, int arg = 0);

    // Request a transition to `id`; applied after the current frame finishes.
    void change_scene(SceneId id, int arg = 0);

    // Advance the current scene's logic one frame, then apply any pending
    // transition (no drawing — used headless and in tests).
    void step();

    // Draw the current scene through the platform renderer.
    void render(Renderer& renderer);

    // The current frame's input, updated by the platform before step().
    void set_input(const Input& input) { input_ = input; }
    const Input& input() const { return input_; }

    SceneId current_id() const { return current_id_; }
    int current_arg() const { return current_arg_; }
    bool has_scene() const { return current_ != nullptr; }
    bool has_pending() const { return pending_.has_value(); }
    std::uint64_t frame() const { return frame_; }

    // Optional observer: called on every scene entry (id, arg). Useful for logs
    // and tests.
    void on_scene_entered(std::function<void(SceneId, int)> observer) {
        observer_ = std::move(observer);
    }

private:
    std::unique_ptr<Scene> make(SceneId id) const;
    void enter(SceneId id, int arg);

    std::unordered_map<SceneId, SceneFactory> registry_;
    std::unique_ptr<Scene> current_;
    SceneId current_id_ = kSceneNone;
    int current_arg_ = 0;
    std::optional<std::pair<SceneId, int>> pending_;
    std::uint64_t frame_ = 0;
    Input input_;
    std::function<void(SceneId, int)> observer_;
};

}  // namespace khdays::game
