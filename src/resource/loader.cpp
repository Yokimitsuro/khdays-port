#include "khdays/resource/loader.h"

#include <iostream>

#include "khdays/assets/bmp.h"

namespace khdays::resource {

namespace {
std::filesystem::path g_mods_root{"mods"};
}

void set_mods_root(const std::filesystem::path& root) {
    g_mods_root = root;
}

khdays::assets::NeutralModel load_model(
    const std::filesystem::path& ds_path) {
    // Model overrides (glTF from mods/) are a future importer; for now the DS
    // model is the only source, but the engine already goes through this layer.
    return khdays::assets::decode_model_geometry(ds_path);
}

khdays::assets::SkeletalAnimation load_animation(
    const std::filesystem::path& ds_path) {
    return khdays::assets::load_nsbca(ds_path);
}

khdays::assets::DecodedTexture load_texture(
    const std::string& name,
    const std::filesystem::path& ds_source) {
    const auto override_path =
        g_mods_root / "textures" / (name + ".bmp");
    if (std::filesystem::exists(override_path)) {
        std::cout << "override: texture '" << name << "' <- "
                  << override_path.string() << std::endl;
        auto texture = khdays::assets::load_bmp(override_path);
        texture.name = name;
        return texture;
    }
    return khdays::assets::load_tex0_texture(ds_source, name);
}

}  // namespace khdays::resource
