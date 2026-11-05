// A room's map on the lower screen, made as the game's rooms have theirs (UI/btl/mapchr.p2's pictures, placed by the
// world's chain in UI/btl/<language>/map.p2: docs/FORMATS.md, "Room maps"): its floors seen from above in the map's
// colours, for a room a mod adds (map.png and map.txt beside its collision.bin).
#pragma once

#include "core/rooms/room_collision.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace studio {

// The map's colours (indices into the lower screen's map palette): what the game's own maps hold.
enum RoomMapColour : std::uint8_t {
    kMapEdge = 10,   // a floor's edge
    kMapAbove = 13,  // a floor above the room's main one
    kMapFloor = 14,  // a floor
    kMapNone = 15,   // nothing: see-through
};

struct RoomMap final {
    static constexpr int kWidth = 192, kHeight = 160;  // the game's map pictures' size
    std::vector<std::uint8_t> pixels;                  // kWidth x kHeight colours, rows from the top
    int origin_x = 0, origin_y = 0;                    // the pixel the room's x = z = 0 falls on
    int width = 0, height = 0;                         // the part of the picture the room's map holds, in 8-pixel tiles
    float cell = 0;                                    // the game units a pixel spans
};

// `source`'s floors (a triangle whose unit normal's y is above `ground_limit`) from above, x to the right and z down
// as the game's maps: the floors more than `step` above the main one (the height most of the floor is at) in the
// colour above (one of a few pixels, a prop on the floor, drawn as the floor), a pixel between floors filled, edges
// where a floor meets nothing or a lower one. The map's scale the finest of the game's steps (0.4 game units a pixel
// and its multiples) that fits the floors and the room's origin; centred in the part it holds (at least the panel's
// 15 x 10 tiles). No floor: an empty map at the game's finest scale.
RoomMap room_map(const CollisionSource& source, float ground_limit, float step = 1.0F);

// map.png (its colours as `palette`'s, 16 RGB entries: the lower screen's map palette, else greys) and map.txt (where
// the room's origin falls, the part held, the scale, its name) into `folder`. False with the reason.
bool write_room_map(const RoomMap& map, const std::filesystem::path& folder, const std::string& name,
                    const std::vector<std::uint8_t>& palette, std::string* why);

// The lower screen's map palette from the player's game (UI/btl/bm_lo_bg000.NCLR.z's sub-palette 13, which the game's
// maps are drawn in) as 16 RGB entries; empty when the game's files are not there.
std::vector<std::uint8_t> room_map_palette(const std::filesystem::path& nitrofs);

}  // namespace studio
