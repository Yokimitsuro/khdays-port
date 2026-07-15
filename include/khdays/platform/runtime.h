#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "khdays/game/game.h"

namespace khdays::platform {

// Run the game frame loop in a native window: each frame maps input, advances
// the scene/task state machine (khdays::game), and draws the current scene
// through an SDL renderer. The caller registers scenes and boots `game` first.
int run_game(khdays::game::Game& game);

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
