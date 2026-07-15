#pragma once

#include <filesystem>
#include <optional>

namespace khdays::platform {

// Open a window and render an MDL0 model in 3D through SDL's GPU API
// (depth-tested, shaded, animated). If animation_path is set it plays that
// NSBCA; otherwise the model's sibling animation is auto-detected. Returns an
// exit code. Blocks until the window closes.
int render_model(
    const std::filesystem::path& model_path,
    const std::optional<std::filesystem::path>& animation_path = std::nullopt);

}  // namespace khdays::platform
