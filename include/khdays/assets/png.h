#pragma once

#include <filesystem>

#include "khdays/assets/tex0.h"  // DecodedTexture

namespace khdays::assets {

// Decode a PNG into a DecodedTexture with top-down RGBA pixels (via stb_image).
// Available only when the build has PNG support (KHDAYS_HAS_PNG). Throws
// std::runtime_error on failure.
DecodedTexture load_png(const std::filesystem::path& input_path);

}  // namespace khdays::assets
