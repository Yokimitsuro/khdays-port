#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace khdays::platform {

struct ApplicationOptions final {
    std::optional<std::filesystem::path> resource_path;
    std::optional<std::string> texture_name;

    // When set, the runtime renders this MDL0 model in 3D instead of showing
    // the placeholder screens.
    std::optional<std::filesystem::path> model_path;
};

int run_application(const ApplicationOptions& options);

}  // namespace khdays::platform
