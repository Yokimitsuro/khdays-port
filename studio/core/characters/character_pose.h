// A character with its own skeleton (`skeleton = own`) posed by one of its clips as the port poses it
// (port/runtime/mods/models.cpp own_pose): the clip's tracks on the model's nodes of the same names, sampled at a
// time; each skin's palette from the nodes' places. For the studio's preview.
#pragma once

#include "core/models/bones.h"

#include "khdays/assets/model_import.h"

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct CharacterClip {
    khdays::assets::ImportedAnimation animation;  // its tracks' nodes made the model's (tracks on no node left out)
    std::size_t tracks = 0, matched = 0;          // the file's tracks, and those on one of the model's nodes
};

// The clip file's first animation, its tracks matched to `model`'s nodes by name. False with the reason (no
// animation, none of its tracks on the model's nodes).
bool load_character_clip(const std::filesystem::path& file, const khdays::assets::ImportedModel& model,
                         CharacterClip* out, std::string* why);

// Each of the model's nodes' world matrix `seconds` into `clip` (looped over its length), or at rest without one.
std::vector<Mat> character_pose(const khdays::assets::ImportedModel& model, const CharacterClip* clip, double seconds);

// Each palette entry's matrix (its joint's node's world times its inverse bind) for the nodes' places; an entry of
// no joint stays identity.
std::vector<Mat> character_palette(const khdays::assets::ImportedModel& model, const std::vector<Mat>& node_worlds);

// The model's span at rest, posed (its lowest and highest point): for its height against the game's.
void character_span(const khdays::assets::ImportedModel& model, std::array<float, 3>* lo, std::array<float, 3>* hi);

}  // namespace studio
