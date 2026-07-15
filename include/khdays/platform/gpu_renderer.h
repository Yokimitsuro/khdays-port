#pragma once

#include <filesystem>

namespace khdays::platform {

// Open a window and render an MDL0 model in 3D through SDL's GPU API
// (depth-tested, shaded). Returns an exit code. Blocks until the window closes.
int render_model(const std::filesystem::path& model_path);

}  // namespace khdays::platform
