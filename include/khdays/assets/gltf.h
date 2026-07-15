#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "khdays/assets/mesh.h"
#include "khdays/assets/tex0.h"

namespace khdays::assets {

// One joint of an imported skin: the palette entry it fills, its bone name (for
// matching a DS skeleton), and its glTF inverse-bind matrix. Used to retarget
// DS animations onto the glTF geometry.
struct GltfSkinJoint final {
    std::uint32_t palette_index = 0U;
    std::string name;
    std::array<float, 16> inverse_bind{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// A model imported from glTF: neutral geometry plus the textures it ships with,
// keyed by the name each mesh references. Rigged meshes are supported: each
// skin contributes a block of palette matrices (jointGlobal * inverseBind) and
// its vertices carry up to four bone indices and weights, so the model renders
// in its authored pose and can be re-posed. Static meshes bake their node
// transform and use the identity palette entry. Texture coordinates are in
// texels relative to each texture, matching the DS convention, so the renderer
// normalizes both the same way.
struct GltfModel final {
    NeutralModel model;
    std::vector<std::pair<std::string, DecodedTexture>> textures;
    // Skin joints across all skins, aligned with the palette blocks built for
    // them. Empty for a fully static model.
    std::vector<GltfSkinJoint> joints;
};

// Import a .gltf (or .glb) file. Throws std::runtime_error on failure.
GltfModel import_gltf(const std::filesystem::path& input_path);

}  // namespace khdays::assets
