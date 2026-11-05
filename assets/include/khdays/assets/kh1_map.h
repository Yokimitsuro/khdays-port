#pragma once

// KINGDOM HEARTS FINAL MIX's rooms (the 1.5+2.5 collection's <w>00_<nn>.bin and .img), read from the player's own
// copy: the remaster's model of a room (the .bin's room entry's .mvbl asset), its textures the .img's HD pictures, its
// skies the .bin's .gvbl assets (docs/FORMATS.md, "KINGDOM HEARTS FINAL MIX's rooms").

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "khdays/assets/gltf_export.h"  // Scene
#include "khdays/assets/texture.h"      // DecodedTexture

namespace khdays::assets::kh1 {

struct Room final {
    Scene scene;
    std::vector<std::string> report;  // what was left out, for the player
};

// A texture's HD picture by its name among the .img's remastered assets (f900, 1f903: hexadecimal), or an empty path
// when the copy has none.
using PictureFinder = std::function<std::filesystem::path(const std::string& name)>;
// A picture the scene names, handed over as it is made (file name beside the .gltf, picture).
using PictureSink = std::function<void(const std::string& file, const DecodedTexture& picture)>;

// The entries of a room's .bin: offset and size each.
struct Entry final {
    std::uint32_t offset = 0, size = 0;
};
std::vector<Entry> bin_entries(const std::uint8_t* data, std::size_t size);

// Where a room's .img holds its texture pages and how many (its header's u32 at +0x18 and its pages' bytes at +0x1c,
// 64 KB a page): the HD pictures are named from them.
struct ImgPages final {
    std::uint32_t at = 0, count = 0;
};
ImgPages img_pages(const std::uint8_t* data, std::size_t size);

// `data` a room's .mvbl, `name` the room's (tw00_02), `pages` its .img's. Its meshes in KH1's units (centimetres)
// turned upright (x, -y, -z), drawn both sides; each mesh's texture its HD picture (found by `find`, written by
// `write_picture`), cut out by its alpha. Throws std::runtime_error when it is no .mvbl.
Room room_model(const std::uint8_t* data, std::size_t size, const std::string& name, const ImgPages& pages,
                const PictureFinder& find, const PictureSink& write_picture);

// `data` one of a room's .gvbl (its skies: the remaster's assets of the .bin's third and fourth entries): its meshes
// added to `room` under a sky node (drawn around the camera, behind everything), blended by their vertices' and
// pictures' alpha, in its order; each one's HD picture the .bin's asset named by the offset the mesh gives (found by
// `find` from that offset in hexadecimal, written by `write_picture` as sky_<offset>.png). Throws std::runtime_error
// when it is no .gvbl.
void add_sky(Room* room, const std::uint8_t* data, std::size_t size, const PictureFinder& find,
             const PictureSink& write_picture);

// `data` a room's .bin: its floors and walls (its first entry of eight chained sections: vertices in the second,
// faces in the fourth), turned upright as the model is, as KH2's collision scene is made. Throws std::runtime_error
// when the .bin holds none.
Scene collision_scene(const std::uint8_t* data, std::size_t size, const std::string& name);

}  // namespace khdays::assets::kh1
