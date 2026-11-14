// A character with its own skeleton (`skeleton = own`) posed by one of its clips as the port poses it
// (port/runtime/mods/models.cpp own_pose): the clip's tracks on the model's nodes of the same names, sampled at a
// time; each skin's palette from the nodes' places. For the studio's preview.
#pragma once

#include "core/models/bones.h"

#include "khdays/assets/model_import.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace studio {

struct CharacterClip {
    khdays::assets::ImportedAnimation animation;  // its tracks' nodes made the model's (tracks on no node left out)
    std::size_t tracks = 0, matched = 0;          // the file's tracks, and those on one of the model's nodes
};

// The clip file's animation named `animation` (empty: its first), its tracks matched to `model`'s nodes by name.
// False with the reason (no such animation, none of its tracks on the model's nodes).
bool load_character_clip(const std::filesystem::path& file, const khdays::assets::ImportedModel& model,
                         CharacterClip* out, std::string* why, const std::string& animation = {});

// The names of the clip file's animations, in its order; empty when it cannot be read or has none.
std::vector<std::string> clip_animation_names(const std::filesystem::path& file);

// A clip's reference split: its file and the animation a "#<name>" after it names (empty: the file's first).
std::pair<std::string, std::string> clip_reference(const std::string& clip);

// An [actions] line split as the port reads it (port/runtime/characters/registry.c read_action): its clip, whose
// path and animation's name may have spaces, and its options' words, which begin at the word after the clip's file
// name (.gltf, .glb, .fbx) or at an option's word (blend, root, time, loop) followed by a value it takes.
struct ActionLine {
    std::string clip;
    std::vector<std::string> options;
};
ActionLine split_action_line(const std::string& value);

// Each of the model's nodes' world matrix `seconds` into `clip` (looped over its length), or at rest without one.
std::vector<Mat> character_pose(const khdays::assets::ImportedModel& model, const CharacterClip* clip, double seconds);

// Each palette entry's matrix (its joint's node's world times its inverse bind) for the nodes' places; an entry of
// no joint stays identity.
std::vector<Mat> character_palette(const khdays::assets::ImportedModel& model, const std::vector<Mat>& node_worlds);

// The model's span at rest, posed (its lowest and highest point): for its height against the game's.
void character_span(const khdays::assets::ImportedModel& model, std::array<float, 3>* lo, std::array<float, 3>* hi);

// The model's node named `name`, -1 when it has none (the port's look for a weapon's bone).
int character_node(const khdays::assets::ImportedModel& model, const std::string& name);

// Where the port holds a hand's weapon on a character of its own skeleton (port/runtime/mods/model_load.cpp, its
// weapons; models.cpp khdays_models_node_placed): the weapon model's space to the character's as drawn (its model
// at `scale`), its bone `bone` at `node_worlds` now and at `rest_worlds` at rest. `turn`: its weapon_turn's degrees
// (x, then y, then z, on the bone's own axes); null, held as the base holds it: turned from the bone at rest to the
// base's hand node at rest, `base_hand_rest`. `weapon_scale`: its [model] weapon_scale.
Mat held_weapon(const std::vector<Mat>& node_worlds, const std::vector<Mat>& rest_worlds, int bone, float scale,
                const std::array<float, 3>* turn, const Mat& base_hand_rest, float weapon_scale);

}  // namespace studio
