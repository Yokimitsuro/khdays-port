#include "core/models/game_model.h"

#include "core/models/bca0.h"
#include "core/files/paths.h"

#include "khdays/assets/message.h"  // lz_decompress

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace studio {

namespace {

namespace assets = khdays::assets;

std::uint32_t le16(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 2 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8) : 0;
}
std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 4 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                           static_cast<std::uint32_t>(d[at + 3]) << 24)
                              : 0;
}

std::vector<std::uint8_t> read_all(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::vector<std::uint8_t> decompress(const std::vector<std::uint8_t>& bytes) {
    try {
        return assets::lz_decompress(bytes);
    } catch (const std::exception&) {
        return {};
    }
}

bool ends_with_z(const std::string& path) {
    return path.size() > 2 && path.compare(path.size() - 2, 2, ".z") == 0;
}

// The BMD0/BCA0 sections of some data: names.cpp's scan, every 4 bytes, the
// magic and a size that fits.
std::vector<std::size_t> sections(const std::vector<std::uint8_t>& d, const char* magic) {
    std::vector<std::size_t> out;
    for (std::size_t at = 0; at + 0x18 <= d.size(); at += 4) {
        if (std::memcmp(&d[at], magic, 4) == 0 && le32(d, at + 8) <= d.size() - at)
            out.push_back(at);
    }
    return out;
}

}  // namespace

std::vector<GameData> game_file_data(const fs::path& nitrofs, const std::string& game_path) {
    std::vector<GameData> out;
    const std::vector<std::uint8_t> whole = read_all(nitrofs / path_u8(game_path));
    if (whole.size() >= 0x10 && whole[0] == 'P' && whole[1] == '2') {
        const std::uint32_t n = le16(whole, 2) & 0x1ff;
        const std::uint32_t base = le32(whole, 0xc);
        const std::uint32_t sizes = 0x10 + ((n + 1) / 2) * 4;
        for (std::uint32_t e = 0; e < n; ++e) {
            const std::size_t at = base + (static_cast<std::size_t>(le16(whole, 0x10 + 2 * e)) << 9);
            const std::uint32_t raw = le32(whole, sizes + 4 * e);
            const std::size_t size = raw & 0x7fffffffu;
            if (at >= whole.size() || size > whole.size() - at)
                continue;
            std::vector<std::uint8_t> bytes(whole.begin() + static_cast<std::ptrdiff_t>(at),
                                            whole.begin() + static_cast<std::ptrdiff_t>(at + size));
            out.push_back({game_path + "#" + std::to_string(e), (raw & 0x80000000u) ? decompress(bytes) : bytes});
        }
        return out;
    }
    out.push_back({game_path, ends_with_z(game_path) ? decompress(whole) : whole});
    return out;
}

std::vector<std::uint8_t> game_label_data(const fs::path& nitrofs, const std::string& label) {
    const auto hash = label.rfind('#');
    const std::string path = hash == std::string::npos ? label : label.substr(0, hash);
    for (auto& d : game_file_data(nitrofs, path)) {
        if (d.label == label)
            return std::move(d.bytes);
    }
    return {};
}

bool load_game_model(const fs::path& nitrofs, const fs::path& graphics_dump, const std::string& key, GameModel* out,
                     std::string* why) {
    auto fail = [&](const std::string& reason) {
        if (why != nullptr)
            *why = reason;
        return false;
    };
    const auto mdl = key.rfind("/mdl/");
    if (mdl == std::string::npos)
        return fail("not a model's name: " + key);
    GameModel g;
    g.label = key.substr(0, mdl);
    g.name = key.substr(mdl + 5);

    const std::vector<std::uint8_t> d = game_label_data(nitrofs, g.label);
    if (d.empty())
        return fail("no data for " + g.label);
    bool found = false;
    for (const std::size_t at : sections(d, "BMD0")) {
        const std::size_t size = le32(d, at + 8);
        for (std::size_t m = 0;; ++m) {
            assets::NeutralModel model;
            try {
                model = assets::decode_model_geometry(&d[at], size, m);
            } catch (const std::exception&) {
                break;  // past its models
            }
            if (model.name == g.name) {
                g.model = std::move(model);
                found = true;
                break;
            }
        }
        if (found)
            break;
    }
    if (!found)
        return fail("no model " + g.name + " in " + g.label);

    // the port's W_G and parents (models.cpp lines 246-251)
    g.rest = g.model.skinning != nullptr
                 ? assets::compute_bone_world_matrices(*g.model.skinning, g.model.object_matrices)
                 : g.model.object_matrices;
    g.parents = g.model.object_parents;
    g.parents.resize(g.rest.size(), -1);

    // its textures, as the dump wrote them: <label>/tex/<texture>[@<palette>].png
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(graphics_dump / path_u8(g.label) / "tex", ec)) {
        const std::string file = u8(e.path().filename());
        if (lower(u8(e.path().extension())) != ".png")
            continue;
        const std::string stem = file.substr(0, file.size() - 4);
        const std::string texture = stem.substr(0, stem.find('@'));
        g.textures.emplace(texture, e.path());
    }

    // the animations beside it: every BCA0 in the files of its folder, for a
    // model of mi/ch the motions of mi/mo (mi/mo/<name>.z), and for a
    // character's of ba/ch/<code> its own in the shared files of a room's
    // mission review (mrslt/data: the BCA0 whose first is <code>_result00)
    // and game over (gameover/data: <code>_mission00)
    const std::string path = g.label.substr(0, g.label.rfind('#'));
    const auto slash = path.find_last_of('/');
    const std::string folder = slash == std::string::npos ? std::string() : path.substr(0, slash);
    std::map<std::string, std::string> shared;  // file -> its BCA0's first animation for this character
    if (folder.rfind("ba/ch/", 0) == 0) {
        shared["mrslt/data"] = folder.substr(6) + "_result00";
        shared["gameover/data"] = folder.substr(6) + "_mission00";
    }
    std::vector<std::string> files;
    const auto add_folder = [&](const std::string& dir) {
        std::vector<std::string> found;
        for (const auto& e : fs::directory_iterator(nitrofs / path_u8(dir), ec)) {
            std::error_code type_ec;
            if (e.is_regular_file(type_ec))
                found.push_back((dir.empty() ? "" : dir + "/") + u8(e.path().filename()));
        }
        std::sort(found.begin(), found.end());
        files.insert(files.end(), found.begin(), found.end());
    };
    add_folder(folder);
    if (folder == "mi/ch")
        add_folder("mi/mo");
    for (const auto& [file, first] : shared)
        files.push_back(file);
    for (const auto& file : files) {
        const auto own = shared.find(file);
        for (const auto& data : game_file_data(nitrofs, file)) {
            for (const std::size_t at : sections(data.bytes, "BCA0")) {
                const std::size_t size = le32(data.bytes, at + 8);
                const auto named = bca0_animations(&data.bytes[at], size);  // their names in the dictionary
                if (own != shared.end() && (named.empty() || named[0].name != own->second))
                    continue;
                for (std::size_t i = 0;; ++i) {
                    assets::SkeletalAnimation a;
                    try {
                        a = assets::load_nsbca(&data.bytes[at], size, i);
                    } catch (const std::exception&) {
                        break;
                    }
                    // (the shared files' may have more, which the game leaves out: Sora's thrones' 25 for his
                    // 24 nodes)
                    if (a.bones.size() != g.model.object_names.size() &&
                        !(own != shared.end() && a.bones.size() > g.model.object_names.size()))
                        continue;
                    g.animations.push_back({data.label, at, size, i, i < named.size() ? named[i].name : a.name,
                                            a.frame_count, fits_skeleton(g.model, a)});
                }
            }
        }
    }
    // those made for it first, its own file's before the others
    std::stable_sort(g.animations.begin(), g.animations.end(), [&](const GameAnimation& x, const GameAnimation& y) {
        if (x.fits != y.fits)
            return x.fits;
        const bool xs = x.label.rfind(path, 0) == 0, ys = y.label.rfind(path, 0) == 0;
        return xs && !ys;
    });
    *out = std::move(g);
    return true;
}

bool fits_skeleton(const assets::NeutralModel& model, const assets::SkeletalAnimation& animation) {
    std::vector<Mat> posed;
    try {
        posed = assets::sample_animation(animation, 0.0f, model.object_matrices);
    } catch (const std::exception&) {
        return false;
    }
    int compared = 0, kept = 0;
    for (std::size_t i = 0; i < posed.size() && i < model.object_matrices.size(); ++i) {
        const Mat& r = model.object_matrices[i];
        const Mat& p = posed[i];
        const float rest = std::sqrt(r[12] * r[12] + r[13] * r[13] + r[14] * r[14]);
        const float now = std::sqrt(p[12] * p[12] + p[13] * p[13] + p[14] * p[14]);
        if (rest < 1e-3f && now < 1e-3f)
            continue;  // a bone at its parent's place, either way
        ++compared;
        if (std::fabs(rest - now) <= 0.1f * std::max(rest, now) + 1e-3f)
            ++kept;
    }
    return compared == 0 || kept * 10 >= compared * 8;
}

bool load_game_animation(const fs::path& nitrofs, const GameAnimation& a, assets::SkeletalAnimation* out,
                         std::string* why) {
    const std::vector<std::uint8_t> d = game_label_data(nitrofs, a.label);
    if (a.offset + a.size > d.size()) {
        if (why != nullptr)
            *why = "no data for " + a.label;
        return false;
    }
    try {
        *out = assets::load_nsbca(&d[a.offset], a.size, a.index);
    } catch (const std::exception& e) {
        if (why != nullptr)
            *why = e.what();
        return false;
    }
    return true;
}

void pose_game_model(const GameModel& g, const assets::SkeletalAnimation* animation, float frame,
                     std::vector<Mat>* node_world, std::vector<Mat>* palette) {
    const std::vector<Mat> objects = animation != nullptr
                                         ? assets::sample_animation(*animation, frame, g.model.object_matrices)
                                         : g.model.object_matrices;
    if (node_world != nullptr) {
        *node_world =
            g.model.skinning != nullptr ? assets::compute_bone_world_matrices(*g.model.skinning, objects) : objects;
    }
    if (palette != nullptr) {
        *palette = g.model.skinning != nullptr ? assets::compute_palette(*g.model.skinning, objects) : g.model.palette;
    }
}

}  // namespace studio
