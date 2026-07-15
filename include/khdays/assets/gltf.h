#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "khdays/assets/mesh.h"
#include "khdays/assets/tex0.h"

namespace khdays::assets {

// A model imported from glTF: neutral geometry plus the textures it ships with,
// keyed by the name each mesh references. Static import for now (no skinning):
// every vertex uses the identity palette entry. Texture coordinates are in
// texels relative to each texture, matching the DS convention, so the renderer
// normalizes both the same way.
struct GltfModel final {
    NeutralModel model;
    std::vector<std::pair<std::string, DecodedTexture>> textures;
};

// Import a .gltf (or .glb) file. Throws std::runtime_error on failure.
GltfModel import_gltf(const std::filesystem::path& input_path);

}  // namespace khdays::assets
