#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "khdays/game/audio.h"
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

// Stable scene identifiers, mirroring the DS `g_SceneTable` (@0x02042548,
// {overlayId, classDesc} per id) that the scene dispatcher (func_0202099c)
// indexes. The port's scene factory is the native form of a table entry; the DS
// overlay a scene lived in is noted for reference:
//   1 → ov000 (boot/logo)   2 → ov02    3 → ov03    5 → ov04    6 → ov05
//   7 → ov06   8 → ov11   9 → ov09   10 → ov07   11 → ov12   12 → ov10   19 → ov08
// Semantic names are filled in as each scene is decompiled.
using SceneId = int;
inline constexpr SceneId kSceneNone = 0;
inline constexpr SceneId kSceneBootLogo = 1;   // fresh boot → the intro/logo scene (ov000)
inline constexpr SceneId kSceneTitle = 7;      // the title screen (ov06); the intro requests it
inline constexpr SceneId kSceneContinue = 12;  // continue/other boot path (ov10)
inline constexpr SceneId kSceneMainMenu = 19;  // Mission Mode main menu (ov08); title requests it on Start

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

    // Request scene `id` and end the current one now — the common "switch to
    // scene X" (the DS request-then-teardown, collapsed). The switch applies at
    // the end of the frame.
    void change_scene(SceneId id, int arg = 0);

    // Latch a pending scene without ending the current one: it keeps running
    // (e.g. playing an exit fade) until it calls end_scene(); only then does the
    // dispatcher tear it down and load the pending id (mirrors func_0202099c
    // gating teardown on func_02023bbc "scene ended").
    void request_scene(SceneId id, int arg = 0);
    void end_scene() { ended_ = true; }

    // Advance the current scene's logic one frame, then apply any pending
    // transition (no drawing — used headless and in tests).
    void step();

    // Draw the current scene through the platform renderer.
    void render(Renderer& renderer);

    // The current frame's input, updated by the platform before step().
    void set_input(const Input& input) { input_ = input; }
    const Input& input() const { return input_; }

    // Optional music service (set by the platform). Scenes request a track with
    // `if (auto* m = manager.music()) m->play_music(...)`; it is null when
    // running headless or without audio, so scenes must null-check.
    void set_music_player(MusicPlayer* music) { music_ = music; }
    MusicPlayer* music() const { return music_; }

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
    bool ended_ = false;
    std::uint64_t frame_ = 0;
    Input input_;
    MusicPlayer* music_ = nullptr;
    std::function<void(SceneId, int)> observer_;
};

}  // namespace khdays::game
