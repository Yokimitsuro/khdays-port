#include "khdays/resource/loader.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <system_error>
#include <vector>

#include <cctype>

#include "khdays/assets/bmp.h"
#ifdef KHDAYS_HAS_PNG
#include "khdays/assets/png.h"
#endif
#ifdef KHDAYS_HAS_GLTF
#include "khdays/assets/gltf.h"
#endif

namespace khdays::resource {

namespace {

std::filesystem::path g_mods_root{"mods"};

// Search every mod folder's category tree for a file named `file_name`.
// mods/<ModName>/<category>/**/<file_name>. Mods are visited in sorted order;
// the first match wins.
std::optional<std::filesystem::path> find_override(
    const std::string& category,
    const std::string& file_name) {
    std::error_code ec;
    if (!std::filesystem::is_directory(g_mods_root, ec)) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> mods;
    for (const auto& entry :
         std::filesystem::directory_iterator(g_mods_root, ec)) {
        if (entry.is_directory(ec)) {
            mods.push_back(entry.path());
        }
    }
    std::sort(mods.begin(), mods.end());

    for (const auto& mod : mods) {
        const auto category_dir = mod / category;
        if (!std::filesystem::is_directory(category_dir, ec)) {
            continue;
        }
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(category_dir, ec)) {
            if (entry.is_regular_file(ec)
                && entry.path().filename().string() == file_name) {
                return entry.path();
            }
        }
    }
    return std::nullopt;
}

}  // namespace

void set_mods_root(const std::filesystem::path& root) {
    g_mods_root = root;
}

LoadedModel load_model(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == ".gltf" || ext == ".glb") {
#ifdef KHDAYS_HAS_GLTF
        auto imported = khdays::assets::import_gltf(path);
        LoadedModel loaded;
        loaded.model = std::move(imported.model);
        for (auto& [name, texture] : imported.textures) {
            const int w = texture.width;
            const int h = texture.height;
            loaded.textures.emplace(
                name, LoadedTexture{std::move(texture), w, h});
        }
        return loaded;
#else
        throw std::runtime_error(
            "glTF support was not built (enable KHDAYS_ENABLE_GLTF)");
#endif
    }

    return LoadedModel{khdays::assets::decode_model_geometry(path), {}};
}

khdays::assets::SkeletalAnimation load_animation(
    const std::filesystem::path& ds_path) {
    return khdays::assets::load_nsbca(ds_path);
}

LoadedTexture load_texture(
    const std::string& name,
    const std::filesystem::path& ds_source) {
    // The DS texture always provides the reference size for UV mapping, so an
    // override of any resolution samples correctly.
    const auto ds_texture = khdays::assets::load_tex0_texture(ds_source, name);

    const auto use_override = [&](const std::filesystem::path& path,
                                  khdays::assets::DecodedTexture image) {
        std::cout << "override: texture '" << name << "' <- " << path.string()
                  << std::endl;
        image.name = name;
        return LoadedTexture{
            std::move(image), ds_texture.width, ds_texture.height};
    };

#ifdef KHDAYS_HAS_PNG
    if (const auto png_path = find_override("textures", name + ".png")) {
        return use_override(*png_path, khdays::assets::load_png(*png_path));
    }
#endif
    if (const auto bmp_path = find_override("textures", name + ".bmp")) {
        return use_override(*bmp_path, khdays::assets::load_bmp(*bmp_path));
    }

    return LoadedTexture{ds_texture, ds_texture.width, ds_texture.height};
}

}  // namespace khdays::resource
