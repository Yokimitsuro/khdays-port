// A character posed by its own clips (character_pose.h).
#include "core/characters/character_pose.h"

#include "core/files/paths.h"

#include "khdays/assets/fbx.h"
#include "khdays/assets/gltf.h"
#include "mods/room_animation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace fs = std::filesystem;
namespace assets = khdays::assets;

namespace studio {

namespace {

constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

}  // namespace

bool load_character_clip(const fs::path& file, const assets::ImportedModel& model, CharacterClip* out,
                         std::string* why) {
    std::string extension = u8(file.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    assets::ImportedModel clip_file;
    try {
        clip_file = extension == ".fbx" ? assets::import_fbx(file) : assets::import_gltf(file);
    } catch (const std::exception& e) {
        *why = e.what();
        return false;
    }
    if (clip_file.animations.empty()) {
        *why = "no animation in it";
        return false;
    }
    // the model's nodes by name: what the port matches the clip's tracks with
    std::map<std::string, int> node_of;
    for (std::size_t n = 0; n < model.nodes.size(); ++n)
        node_of.emplace(model.nodes[n].name, static_cast<int>(n));
    CharacterClip c;
    assets::ImportedAnimation& a = clip_file.animations.front();
    c.animation.name = a.name;
    c.animation.duration = a.duration;
    c.animation.loop = a.loop;
    c.animation.loop_from = a.loop_from;
    c.tracks = a.tracks.size();
    for (auto& t : a.tracks) {
        if (t.node < 0 || static_cast<std::size_t>(t.node) >= clip_file.nodes.size())
            continue;
        const auto found = node_of.find(clip_file.nodes[static_cast<std::size_t>(t.node)].name);
        if (found == node_of.end())
            continue;
        t.node = found->second;
        c.animation.tracks.push_back(std::move(t));
        ++c.matched;
    }
    if (c.matched == 0) {
        *why = "none of its " + std::to_string(c.tracks) + " tracks moves a bone of the model (by name)";
        return false;
    }
    *out = std::move(c);
    return true;
}

std::vector<Mat> character_pose(const assets::ImportedModel& model, const CharacterClip* clip, double seconds) {
    if (clip == nullptr || clip->animation.duration <= 0.0f)
        return khdays::room::node_worlds(model.nodes, {}, 0.0);
    assets::ImportedAnimation looped = clip->animation;
    looped.loop = true;  // the preview plays it again and again
    looped.loop_from = 0.0f;
    return khdays::room::node_worlds(model.nodes, {looped}, std::fmod(seconds, static_cast<double>(looped.duration)));
}

std::vector<Mat> character_palette(const assets::ImportedModel& model, const std::vector<Mat>& node_worlds) {
    std::size_t entries = 0;
    for (const auto& j : model.joints)
        entries = std::max<std::size_t>(entries, j.palette_index + 1u);
    std::vector<Mat> palette(entries, kIdentity);
    for (const auto& j : model.joints) {
        if (j.node < 0 || static_cast<std::size_t>(j.node) >= node_worlds.size())
            continue;
        palette[j.palette_index] = mat_mul(node_worlds[static_cast<std::size_t>(j.node)], j.inverse_bind);
    }
    return palette;
}

void character_span(const assets::ImportedModel& model, std::array<float, 3>* lo, std::array<float, 3>* hi) {
    *lo = {1e30f, 1e30f, 1e30f};
    *hi = {-1e30f, -1e30f, -1e30f};
    for (const auto& mesh : model.model.meshes) {
        for (const auto& v : mesh.vertices) {
            for (std::size_t c = 0; c < 3; ++c) {
                (*lo)[c] = std::min((*lo)[c], v.position[c]);
                (*hi)[c] = std::max((*hi)[c], v.position[c]);
            }
        }
    }
}

}  // namespace studio
