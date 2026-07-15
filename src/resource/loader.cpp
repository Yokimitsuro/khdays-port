#include "khdays/resource/loader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "khdays/assets/message.h"
#include "khdays/assets/sequence.h"

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

// The asset's base name: its filename up to the first '.', so both
// "db_es.p2" and "magic.s.z" reduce to the key a text override is named after
// ("db_es", "magic").
std::string base_name(const std::filesystem::path& path) {
    const auto filename = path.filename().string();
    const auto dot = filename.find('.');
    return dot == std::string::npos ? filename : filename.substr(0, dot);
}

// Parse a text-override file: '#' comments and blank lines are ignored; each
// "key = value" line maps a key to a decoded UTF-16 string (value may use the
// \n, \t, \\, \xNN, \uNNNN escapes that message_to_utf8 emits).
std::unordered_map<std::string, std::u16string> parse_text_override(
    const std::filesystem::path& file) {
    std::unordered_map<std::string, std::u16string> overrides;
    std::ifstream stream{file};
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') {
            continue;
        }
        const auto equals = line.find('=', start);
        if (equals == std::string::npos) {
            continue;
        }
        auto key = line.substr(start, equals - start);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
            key.pop_back();
        }
        auto value = line.substr(equals + 1U);
        const auto value_start = value.find_first_not_of(" \t");
        value = value_start == std::string::npos ? std::string{}
                                                 : value.substr(value_start);
        overrides[key] = khdays::assets::message_from_utf8(value);
    }
    return overrides;
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

khdays::assets::MessageArchive load_message_archive(
    const std::filesystem::path& path) {
    auto archive = khdays::assets::load_p2_archive(path);

    const auto override_path = find_override("text", base_name(path) + ".txt");
    if (!override_path) {
        return archive;
    }
    const auto overrides = parse_text_override(*override_path);
    std::size_t applied = 0;
    for (const auto& [key, value] : overrides) {
        const auto colon = key.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        try {
            const auto sub =
                static_cast<std::size_t>(std::stoul(key.substr(0, colon)));
            const auto index =
                static_cast<std::size_t>(std::stoul(key.substr(colon + 1U)));
            if (sub < archive.subdbs.size()
                && index < archive.subdbs[sub].size()) {
                archive.subdbs[sub][index] = value;
                ++applied;
            }
        } catch (const std::exception&) {
            // Malformed key (non-numeric): skip it.
        }
    }
    std::cout << "override: text '" << base_name(path) << "' <- "
              << override_path->string() << " (" << applied << " strings)"
              << std::endl;
    return archive;
}

std::vector<std::u16string> load_string_table(
    const std::filesystem::path& path) {
    auto table = khdays::assets::load_string_table(path);

    const auto override_path = find_override("text", base_name(path) + ".txt");
    if (!override_path) {
        return table;
    }
    const auto overrides = parse_text_override(*override_path);
    std::size_t applied = 0;
    for (const auto& [key, value] : overrides) {
        try {
            const auto index = static_cast<std::size_t>(std::stoul(key));
            if (index < table.size()) {
                table[index] = value;
                ++applied;
            }
        } catch (const std::exception&) {
            // Malformed key: skip it.
        }
    }
    std::cout << "override: strings '" << base_name(path) << "' <- "
              << override_path->string() << " (" << applied << " strings)"
              << std::endl;
    return table;
}

khdays::assets::DecodedAudio load_sound(
    const khdays::assets::Sdat& sdat,
    const std::size_t wave_archive_index,
    const std::size_t swav_index) {
    const auto key = std::to_string(wave_archive_index) + "_"
        + std::to_string(swav_index) + ".wav";
    if (const auto override_path = find_override("sounds", key)) {
        std::cout << "override: sound " << wave_archive_index << ':'
                  << swav_index << " <- " << override_path->string()
                  << std::endl;
        return khdays::assets::load_wav(*override_path);
    }
    return khdays::assets::sdat_waveform(sdat, wave_archive_index, swav_index);
}

khdays::assets::DecodedAudio render_music(
    const khdays::assets::Sdat& sdat,
    const std::size_t sequence_index,
    const std::uint32_t sample_rate,
    const double max_seconds) {
    const auto override_sample =
        [](int wave_archive, int swav)
        -> std::optional<khdays::assets::DecodedAudio> {
        const auto key = std::to_string(wave_archive) + "_"
            + std::to_string(swav) + ".wav";
        if (const auto path = find_override("sounds", key)) {
            std::cout << "override: music sample " << wave_archive << ':'
                      << swav << " <- " << path->string() << std::endl;
            return khdays::assets::load_wav(*path);
        }
        return std::nullopt;
    };
    return khdays::assets::render_sequence(
        sdat, sequence_index, sample_rate, max_seconds, override_sample);
}

}  // namespace khdays::resource
