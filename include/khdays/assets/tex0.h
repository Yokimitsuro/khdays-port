#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace khdays::assets {

struct DecodedTexture final {
    std::string name;
    std::optional<std::string> palette_name;
    std::string format_name;
    int width = 0;
    int height = 0;
    bool color_zero_transparent = false;
    std::vector<std::uint8_t> rgba;
};

std::vector<std::string> list_tex0_textures(
    const std::filesystem::path& input_path);

DecodedTexture load_tex0_texture(
    const std::filesystem::path& input_path,
    std::optional<std::string_view> requested_name = std::nullopt);

}  // namespace khdays::assets
