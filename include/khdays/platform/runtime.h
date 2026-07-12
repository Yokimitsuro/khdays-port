#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace khdays::platform {

struct ApplicationOptions final {
    std::optional<std::filesystem::path> resource_path;
    std::optional<std::string> texture_name;
};

int run_application(const ApplicationOptions& options);

}  // namespace khdays::platform
