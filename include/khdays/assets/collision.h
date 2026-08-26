#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The game's collision model, and the vertical ground query it runs against it.
//
// A room's data sub-file inside a `mi/wd/wd_<code>` world archive is not a
// container: it *is* this struct, stored with every pointer as a file-relative
// offset. See docs/MISSION_WORLD_DATA.md for the layout and how a room binds to
// it, and docs/GAMEPLAY_RUNTIME.md for what the game does with the result.
//
// Everything here mirrors the decompiled originals rather than reinterpreting
// them: `func_02028bb4` (the parser), `func_01ffd824` (the ray-face test) and
// `FUN_01ffdb54` (the vertical traversal). Values are the DS's 20.12 fixed
// point, kept as integers so the arithmetic matches bit for bit.
namespace khdays::assets {

// 20.12: one unit is 4096.
inline constexpr std::int32_t kFxOne = 0x1000;

// The plane of a face. `distance` is compared against the dot product of the
// normal with a point, both in 20.12.
struct CollisionPlaneFx final {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
    std::int32_t distance = 0;
};

// An edge plane. Only x and z: the inside test is two-dimensional.
struct CollisionEdgeFx final {
    std::int16_t x = 0;
    std::int16_t z = 0;
    std::int32_t distance = 0;
};

// One collision face: a convex polygon of three or four corners.
struct CollisionFace final {
    // minX, minZ, maxX, maxZ -- the broad-phase bound.
    std::array<std::int32_t, 4> bounds{0, 0, 0, 0};
    std::uint16_t flags = 0;
    std::uint16_t vertex_count = 0;
    CollisionPlaneFx plane;
    std::array<CollisionEdgeFx, 4> edges{};
    std::array<std::array<std::int32_t, 3>, 4> vertices{};
};

// Face flag bits, from the traversal and the ray test.
inline constexpr std::uint16_t kFaceFlagVerticalPlane = 0x0002;  // ray test
inline constexpr std::uint16_t kFaceFlagSkip = 0x4000;           // traversal
inline constexpr std::uint16_t kFaceFlagLastOfRun = 0x8000;      // traversal

// A named record: `name` is a 0xc-byte field, `code` the surface type the game
// writes at runtime (0 on disk; 0/1 for a gate's state, 2..8 for a surface
// type -- see GAMEPLAY_RUNTIME.md).
struct CollisionNamedRecord final {
    std::string name;
    std::uint32_t code = 0;
    std::uint32_t payload = 0;
};

struct CollisionModel final {
    bool valid = false;
    std::uint16_t flags = 0;
    std::uint16_t node_count = 0;
    // The run of 0x88-byte faces the ray test walks. The two runs of 0x84-byte
    // faces the parser also relocates are counted but NOT decoded: nothing read
    // so far shows what queries them.
    std::vector<CollisionFace> faces;
    std::uint16_t face84_count_a = 0;
    std::uint16_t face84_count_b = 0;
    std::vector<CollisionNamedRecord> named;
};

// Decode a room's data blob. Returns an invalid model when the header's own
// section offsets do not chain consistently, rather than guessing past it.
CollisionModel decode_collision_model(
    const std::uint8_t* data, std::size_t size);

// The result of a downward query at (x, z).
struct GroundHit final {
    bool hit = false;
    std::int32_t y = 0;             // surface height, 20.12
    std::size_t face_index = 0;
    std::int32_t fraction = 0;      // 0..0x08000000 along the segment
};

// Cast straight down from `from_y` to `to_y` at (x, z) and return the nearest
// surface, exactly as `func_01ffd824` decides it: the face's 2D bound must
// contain the point, the segment must cross the plane downward within the
// game's 0x80 epsilon, and the point must be inside every edge plane.
//
// The game reaches faces through the model's quadtree, which is a spatial
// index: this walks the whole face run instead, which returns the same nearest
// hit while the tree is well formed. The tree is decoded (node count and layout)
// but not walked here.
GroundHit ground_at(
    const CollisionModel& model,
    std::int32_t x,
    std::int32_t z,
    std::int32_t from_y,
    std::int32_t to_y);

}  // namespace khdays::assets
