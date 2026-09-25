#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "khdays/game/game.h"

namespace khdays::platform {

// Run the game frame loop in a native window: each frame maps input, advances
// the scene/task state machine (khdays::game), and draws the current scene
// through an SDL renderer. The caller registers scenes; `start` then enters the
// first scene (e.g. `game.boot()`). It runs only once the platform services
// exist -- music, video and the saved settings, language included -- because a
// scene's on_enter may request a track or a movie and loads localized assets.
int run_game(khdays::game::Game& game,
             const std::function<void(khdays::game::Game&)>& start);

// Play one NitroFS MODS movie in a native window. Video is decoded by the
// reconstructed MobiClip path; audio support follows separately.
int play_mods_video(std::string_view game_path);

struct ApplicationOptions final {
    std::optional<std::filesystem::path> resource_path;
    std::optional<std::string> texture_name;

    // When set, the runtime renders this MDL0 model in 3D instead of showing
    // the placeholder screens.
    std::optional<std::filesystem::path> model_path;

    // Optional NSBCA animation to play on the model. When unset, the runtime
    // auto-detects the model's sibling animation.
    std::optional<std::filesystem::path> animation_path;
};

int run_application(const ApplicationOptions& options);

}  // namespace khdays::platform
