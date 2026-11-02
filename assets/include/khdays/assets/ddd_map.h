#pragma once

// KINGDOM HEARTS Dream Drop Distance's rooms (the 2.8 collection's map/<w>/<w>_<nn>/bin/<w>_<nn>.pmp), read from the
// player's own copy: a PMP places PMO models (version 4, read from the data itself: docs/FORMATS.md, "Dream Drop
// Distance's rooms"), their textures named and found as HD pictures beside the room.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "khdays/assets/gltf_export.h"  // Scene
#include "khdays/assets/texture.h"      // DecodedTexture

namespace khdays::assets::ddd {

struct Room final {
    Scene scene;
    std::vector<std::string> report;  // what was left out, for the player
};

// A texture's HD picture by its name (eh1_alp25e), or an empty path when the copy has none.
using PictureFinder = std::function<std::filesystem::path(const std::string& name)>;
// A picture the scene names, handed over as it is made (file name beside the .gltf, picture).
using PictureSink = std::function<void(const std::string& file, const DecodedTexture& picture)>;

// The names of the textures a room's PMP lists (its own and those its models use).
std::vector<std::string> texture_names(const std::uint8_t* data, std::size_t size);

// `data` a room's .pmp, `name` the room's (eh_01). Its models placed, in DDD's units and axes; each mesh's material
// its texture (found by `find`, written by `write_picture`), cut-out in the first pass, blended in the second. Throws
// std::runtime_error when it is no PMP.
Room room_model(const std::uint8_t* data, std::size_t size, const std::string& name, const PictureFinder& find,
                const PictureSink& write_picture);

// `data` a room's collision (its .bcd, "@bcd" version 0x0a): every face of its floors and walls, in its model's units
// and axes, as KH2's collision scene is made. Throws std::runtime_error when it is no BCD.
Scene collision_scene(const std::uint8_t* data, std::size_t size, const std::string& name);

}  // namespace khdays::assets::ddd
