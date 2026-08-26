#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "khdays/assets/mesh.h"
#include "khdays/assets/tex0.h"

// Loading a stage room out of a `mi/wd/wd_<code>` world archive, through the
// resource layer, into neutral forms the engine can draw.
//
// See docs/MISSION_WORLD_DATA.md for the archive format and
// docs/GAMEPLAY_RUNTIME.md for what the game does with it.
namespace khdays::resource {

// One drawable piece of a room: a KAPH sub-file's slot-7 model with the
// textures it embeds.
struct RoomModel final {
    std::string name;
    khdays::assets::NeutralModel model;
    std::map<std::string, khdays::assets::DecodedTexture> textures;
};

struct LoadedRoom final {
    // The room's data blob sub-file -- the collision world the game installs
    // for this room. Its index comes from the room table entry's byte +0x03,
    // which is established; the blob's record layout is not decoded yet, so
    // this is reported rather than parsed.
    std::size_t data_subfile = 0;

    // The geometry. See the note on `geometry_is_exact`.
    std::vector<RoomModel> models;

    // False whenever the models were chosen by the port's heuristic rather than
    // by the game's own rule -- which is currently always, because that rule is
    // unknown. The heuristic takes the run of KAPH sub-files immediately after
    // the data blob, stopping at the next non-KAPH. It matches what the archives
    // look like in the common case (Traverse Town's District 3 is its data blob
    // at 2 plus KAPH at 3 and 4) but is demonstrably not the rule: wd_bb room 1
    // declares sub_count 8 with a single KAPH before the next room's data, and
    // wd_al rooms 15..17 point at data blobs with no adjacent KAPH at all.
    bool geometry_is_exact = false;
};

// Load room `room_index` of the world named by its two-character code (e.g.
// "tw"). Returns nothing when the archive, the room, or its geometry cannot be
// read.
std::optional<LoadedRoom> load_world_room(
    const std::string& world_code, std::size_t room_index);

// How many rooms the world's room table declares. Zero when the archive is
// missing or unreadable.
std::size_t world_room_count(const std::string& world_code);

}  // namespace khdays::resource
