#pragma once

// KINGDOM HEARTS Dream Drop Distance's rooms (the 2.8 collection's map/<w>/<w>_<nn>/bin/<w>_<nn>.pmp), read from the
// player's own copy: a PMP places PMO models (version 4, read from the data itself: docs/FORMATS.md, "Dream Drop
// Distance's rooms"), their textures named and found as HD pictures beside the room, those its .txa animates shown
// frame by frame.

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

// A texture of a room shown frame by frame (its .txa, "TXA" version 2): each frame cut from a sheet that holds them all.
struct TextureAnimation final {
    std::string texture;  // the room's texture it shows on
    // the sheet's size and a frame's, in the original's pixels
    int sheet_width = 0, sheet_height = 0;
    int frame_width = 0, frame_height = 0;
    struct Frame final {
        int x = 0, y = 0;  // where it is cut from the sheet
        int length = 0;    // in frames of 60 a second
    };
    std::vector<Frame> frames;
    int loop_from = 0;  // the frame it plays again from after the last
};

// `data` a room's .txa: each texture's animation (its first). Throws std::runtime_error when it is no TXA.
std::vector<TextureAnimation> texture_animations(const std::uint8_t* data, std::size_t size);

// The room's materials on each animation's texture shown frame by frame: the frames cut from its sheet's HD picture
// (among `sheets`, the names of the .txa's own HD pictures, the one named as the texture with one character more or
// less -- fa03_01's fa03_01a, di_05_thu1a's di_05_thu1 -- found by `find`) at the picture's scale, written by
// `write_picture` as <texture>_<n>.png. An animation on a texture the room has not is left out; one whose sheet the copy
// has not is said in the room's report.
void animate_textures(Room* room, const std::vector<TextureAnimation>& animations,
                      const std::vector<std::string>& sheets, const PictureFinder& find,
                      const PictureSink& write_picture);

// `data` a room's collision (its .bcd, "@bcd" version 0x0a): every face of its floors and walls, in its model's units
// and axes, as KH2's collision scene is made. Throws std::runtime_error when it is no BCD.
Scene collision_scene(const std::uint8_t* data, std::size_t size, const std::string& name);

}  // namespace khdays::assets::ddd
