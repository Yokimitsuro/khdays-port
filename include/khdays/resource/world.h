#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "khdays/assets/mesh.h"
#include "khdays/assets/tex0.h"

// Reading a `mi/wd/wd_<code>` world archive through the resource layer, into
// neutral forms the engine can draw.
//
// This layer only exposes what is **established** about the format. In
// particular there is no "load room N's geometry" call, because which KAPH
// sub-files make up a room is not decoded: the room table binds a room to its
// data blob and nothing read so far binds it to geometry. A caller that wants
// models must name the sub-files itself.
//
// See docs/MISSION_WORLD_DATA.md for the format and docs/GAMEPLAY_RUNTIME.md
// for what the game does with it.
namespace khdays::resource {

// One drawable piece: a KAPH sub-file's slot-7 model with the textures it
// embeds.
struct RoomModel final {
    std::string name;
    std::size_t subfile = 0;
    khdays::assets::NeutralModel model;
    std::map<std::string, khdays::assets::DecodedTexture> textures;
};

// How many rooms the world's room table declares. Zero when the archive is
// missing or unreadable.
std::size_t world_room_count(const std::string& world_code);

// The sub-file index of a room's data blob -- the collision world the game
// installs for it. This is the room table entry's byte +0x03, verified to land
// on an untyped data sub-file for every room of all ten shipped archives.
std::optional<std::size_t> room_data_subfile(
    const std::string& world_code, std::size_t room_index);

// Decode the named sub-files as models. Each must be a KAPH holding an NSBMD in
// slot 7; sub-files that are not are skipped. The caller chooses them, so this
// makes no claim about which belong together.
std::vector<RoomModel> load_world_models(
    const std::string& world_code, const std::vector<std::size_t>& subfiles);

// Every KAPH sub-file in the archive, by index -- an inventory for a caller
// that wants to look at what is there. Says nothing about room membership.
std::vector<std::size_t> world_model_subfiles(const std::string& world_code);

}  // namespace khdays::resource
