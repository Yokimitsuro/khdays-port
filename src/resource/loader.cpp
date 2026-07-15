#include "khdays/resource/loader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

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

#ifdef KHDAYS_HAS_GLTF
// Turn glTF geometry into a model driven by a DS skeleton: match the glTF skin's
// joints to DS bones by name, then build a retargeting skinning program so DS
// NSBCA animations pose the (higher-poly) glTF geometry.
LoadedModel retarget_gltf_onto_ds(
    khdays::assets::GltfModel gltf,
    khdays::assets::NeutralModel ds) {
    LoadedModel out;
    out.model = std::move(gltf.model);  // geometry + rest palette

    std::unordered_map<std::string, int> bone_index;
    for (std::size_t i = 0; i < ds.object_names.size(); ++i) {
        bone_index.emplace(ds.object_names[i], static_cast<int>(i));
    }

    constexpr std::array<float, 16> identity{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<khdays::assets::RetargetJoint> joints;
    joints.reserve(gltf.joints.size());
    std::size_t matched = 0;
    for (const auto& j : gltf.joints) {
        khdays::assets::RetargetJoint rj;
        rj.palette_index = j.palette_index;
        rj.inverse_bind = j.inverse_bind;
        rj.rest = j.palette_index < out.model.palette.size()
            ? out.model.palette[j.palette_index]
            : identity;
        const auto it = bone_index.find(j.name);
        rj.ds_bone = it != bone_index.end() ? it->second : -1;
        if (rj.ds_bone >= 0) {
            ++matched;
        }
        joints.push_back(rj);
    }

    out.model.object_matrices = ds.object_matrices;
    out.model.object_names = ds.object_names;
    if (ds.skinning != nullptr) {
        out.model.skinning = khdays::assets::make_retarget_program(
            *ds.skinning, std::move(joints), out.model.palette.size());
    }

    for (auto& [name, texture] : gltf.textures) {
        const int w = texture.width;
        const int h = texture.height;
        out.textures.emplace(name, LoadedTexture{std::move(texture), w, h});
    }

    std::cout << "override: model '" << ds.name << "' <- glTF geometry ("
              << matched << "/" << gltf.joints.size()
              << " joints matched to DS bones)" << std::endl;
    return out;
}
#endif

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

    auto ds = khdays::assets::decode_model_geometry(path);

#ifdef KHDAYS_HAS_GLTF
    // Model override: a rigged glTF at mods/<mod>/models/<ds_name>.gltf replaces
    // the DS geometry and is animated by the DS skeleton (higher-poly mods).
    if (ds.skinning != nullptr && !ds.object_names.empty()) {
        for (const char* ext : {".gltf", ".glb"}) {
            const auto override_path = find_override("models", ds.name + ext);
            if (!override_path) {
                continue;
            }
            try {
                auto gltf = khdays::assets::import_gltf(*override_path);
                if (!gltf.joints.empty()) {
                    return retarget_gltf_onto_ds(std::move(gltf), std::move(ds));
                }
                std::cerr << "model override '" << override_path->string()
                          << "' has no skin; ignoring (needs a rig to animate)\n";
            } catch (const std::exception& error) {
                std::cerr << "model override '" << override_path->string()
                          << "': " << error.what() << '\n';
            }
            break;
        }
    }
#endif

    return LoadedModel{std::move(ds), {}};
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
