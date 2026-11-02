// The rooms a mod adds to a world (spec 040): mods/<Mod>/rooms/rooms.txt lists them, a line "<key> <world>" each, their
// files in rooms/<world>/<key>/ -- room.gltf and its pictures (the model drawn), collision.bin (its floors and walls
// as the game reads them). The port puts them after the world's own rooms (port/runtime/rooms/added.cpp); a new
// mission names one as its room (`room = <key>`).
#pragma once

#include "core/rooms/room_change.h"

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct AddedRoom {
    std::string key, world;
};

// The mod's added rooms, in their order; `notes` the lines not read.
std::vector<AddedRoom> read_added_rooms(const std::filesystem::path& mod_dir, std::vector<std::string>* notes);
// Writes the list (rooms/rooms.txt); false with the reason.
bool write_added_rooms(const std::filesystem::path& mod_dir, const std::vector<AddedRoom>& rooms, std::string* why);
// rooms/<world>/<key>/ under the mod.
std::filesystem::path added_room_folder(const std::filesystem::path& mod_dir, const std::string& world,
                                        const std::string& key);
// Whether `key` can name a room (letters, digits, _ and -).
bool added_room_key(const std::string& key);

// An added room's first place: in its game's `units`, its lowest main floor at height 0 and one of its floors under
// the origin (where a new mission's `start = 0 0 0` puts the player); false when it has no floor.
bool added_room_place(const khdays::assets::ImportedModel& floors, float units, RoomPlacement* place);

// Adds room `key` to `world` (or makes it again): `model` placed by `place` written as its room.gltf with its
// pictures, its floors and walls built from `collision_model` (empty: the model's own visible triangles) as a replaced
// room's are, on room 0's records and markers, written as collision.bin; the room listed. `lines` what was done.
bool add_room(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir, const std::string& world,
              const std::string& key, const std::filesystem::path& model, const RoomPlacement& place,
              const std::filesystem::path& collision_model, std::vector<std::string>* lines, std::string* why);

// Removes room `key` of `world` from the mod: its folder and its line.
bool remove_added_room(const std::filesystem::path& mod_dir, const std::string& world, const std::string& key,
                       std::string* why);

}  // namespace studio
