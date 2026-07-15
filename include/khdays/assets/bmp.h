#pragma once

#include <filesystem>

#include "khdays/assets/tex0.h"  // DecodedTexture

namespace khdays::assets {

// Decode a Windows BMP (24- or 32-bit, uncompressed) into a DecodedTexture with
// top-down RGBA pixels. Used for user-provided texture overrides; no dependency
// beyond the standard library. Throws std::runtime_error on malformed data.
DecodedTexture load_bmp(const std::filesystem::path& input_path);

}  // namespace khdays::assets
