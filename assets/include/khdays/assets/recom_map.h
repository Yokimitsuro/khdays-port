#pragma once

// KINGDOM HEARTS Re:Chain of Memories' rooms (the 1.5+2.5 collection's MAP<mm>/<nnnn>/MP<mm><nnnn>.AZF, .VTM and
// .RTM), read from the player's own copy: a room's pieces placed by its layout, their textures the .VTM's and .RTM's
// TIM2 pictures, the .RTM's from their HD pictures when the copy has them (docs/FORMATS.md, "Re:Chain of Memories'
// rooms").

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "khdays/assets/gltf_export.h"  // Scene
#include "khdays/assets/texture.h"      // DecodedTexture

namespace khdays::assets::recom {

struct Room final {
    Scene scene;
    std::vector<std::string> report;  // what was left out, for the player
};

// A .VTM's or .RTM's pictures: each one's name (gai_a_00, without its .tm2) and where its TIM2 starts.
struct PictureEntry final {
    std::string name;
    std::size_t offset = 0;
};
std::vector<PictureEntry> picture_list(const std::uint8_t* data, std::size_t size);

// A texture's picture by its name, or none when the room's files hold none.
using PictureSource = std::function<std::optional<DecodedTexture>(const std::string& name)>;
// A picture the scene names, handed over as it is made (file name beside the .gltf, picture).
using PictureSink = std::function<void(const std::string& file, const DecodedTexture& picture)>;

// `data` a room's .AZF, `name` the room's (MP010001). Its pieces placed by its layout, in Re:CoM's units (metres)
// turned upright (x, -y, -z), drawn both sides; each texture's picture from `picture`, written by `write_picture`, cut
// out by its alpha. Throws std::runtime_error when it is no .AZF.
Room room_model(const std::uint8_t* data, std::size_t size, const std::string& name, const PictureSource& picture,
                const PictureSink& write_picture);

}  // namespace khdays::assets::recom
