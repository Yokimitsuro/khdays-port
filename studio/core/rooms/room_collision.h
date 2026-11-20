// A room's collision quadtree: what the game's walk relies on, built from faces, and compared
// (docs/FORMATS.md, "Mission worlds").
#pragma once

#include "core/rooms/worlds.h"

#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace studio {

// Empty when `c`'s tree keeps every rule above, else the first broken.
std::string check_tree(const RoomCollision& c);

// A new tree over `c`'s faces (each list reordered by node): each face in the
// deepest node whose square holds it, a node split while it holds more than
// eight faces and is larger than `smallest`; the root and smallest size set in
// the header. Every face's flags keep their 0x4000 (skipped) bit. `order`,
// when given, receives per list each face's index before.
void build_tree(RoomCollision* c, std::int32_t centre_x, std::int32_t centre_z, std::int32_t size,
                std::int32_t smallest, std::array<std::vector<std::size_t>, 3>* order = nullptr);

// The deepest the game's walk goes: `CollCast_TestModel` keeps eight frames
// (`data_027e06e4`), one per level from the root's; a ninth would be written
// over the hit record after them.
constexpr int kTreeLevels = 8;

// Empty when, for a grid of boxes over the root square and for each list,
// the walk over `a` and over `b` finds the same faces (by their corners and
// bounds) and each finds every face whose bounds meet the box; else the first
// difference.
std::string compare_walks(const RoomCollision& a, const RoomCollision& b);

// The face's plane into `f->planes` as the game's data holds it: its normal
// from (v1 - v0) x (v2 - v0) normalised to 4096 = 1, its distance at v0
// (`RoomPoly_BuildPlanes`); the edges' planes zero, as on disk -- the game
// makes them when it loads the room (`Coll_ResolveModelBlobPointers`).
void compute_planes(int list, RoomFace* f);

// A collision model given as triangles in the game's units (1 = 4096 in
// 20.12), each turned so that (b - a) x (c - a) points to the side a
// character stands on (glTF's front, counter-clockwise), with its material.
struct CollisionSource {
    std::vector<std::array<std::array<float, 3>, 3>> triangles;
    std::vector<std::string> materials;  // one per triangle (may be empty)
    // one per triangle (may be empty: none): left out of the collision, nothing stands on it or
    // stops at it -- a translucent material's of the visible model (glows, glass, a fog layer)
    std::vector<char> passable;
};

struct CollisionRules {
    // a face whose unit normal's y is above this is a ground, below its
    // negative a ceiling, else a wall; 0x333 / 4096 (78.5 degrees from flat)
    // is where `Actor_ResolveMove` steps onto a surface
    float ground_limit = 0x333 / 4096.0f;
    // names (surface tags `nohit` ..., `gate%02d`, `col_wall%02d` ...) the
    // faces made of a material's triangles, or of one triangle, take
    std::map<std::string, std::vector<std::string>> names_by_material;
    std::map<std::size_t, std::vector<std::string>> names_by_triangle;
    // the original's gates and walls that open which no new face takes are
    // kept where the game had them (their faces copied), but these
    std::set<std::string> drop_original;
    // placement markers moved: by name, x, y, z (20.12) and the facing word
    std::map<std::string, std::array<std::int32_t, 4>> markers;
};

// What a made room should tell the modder (the window says it in their
// language, the command line in English: warning_text).
struct RoomWarning {
    enum Kind {
        dropped,           // {0} triangles of no area left out
        too_big,           // {0} bytes, {2} times the game's largest room ({1})
        no_ground,         // nothing to stand on
        gate_unplaced,     // gate {0} on no face: its exit cannot be taken
        wall_unplaced,     // col_wall {0} on no face
        name_kept,         // {0} kept where the game had it (its faces copied)
        marker_floating,   // marker {0} has no ground under it
        collision_shared,  // room {0} uses the same collision
        model_shared,      // room {0} draws model {1} too
        passable,          // {0} triangles of translucent materials left out
        marker_in_wall,    // marker {0} stands in a wall ({1} {2} {3})
    } kind;
    std::vector<std::string> args;
};
std::string warning_text(const RoomWarning& w);

struct CollisionReport {
    std::array<std::size_t, 3> faces{};  // grounds, walls, ceilings made
    std::size_t nodes = 0;
    std::size_t bytes = 0;     // the room's collision as written
    std::size_t dropped = 0;   // triangles of no area at the game's precision
    std::size_t passable = 0;  // triangles left out as passable (CollisionSource::passable)
    std::vector<RoomWarning> warnings;
    // per list and face (as written), the source triangles it was made of;
    // none for a face kept from the original
    std::array<std::vector<std::vector<std::size_t>>, 3> face_triangles;
};

// The game's largest room collision (`mi/wd/wd_*`, 77,624 bytes). A room's
// collision comes from the game's sub heap, which the port no longer bounds by
// the DS's 4 MB (1.25 GB of address space, memory given as used);
// a larger collision is said -- a simpler collision model is lighter -- not
// refused.
constexpr std::size_t kLargestShippedCollision = 77624;

// A room's collision made from `source` over the original room's: its header, markers and named
// records kept, the faces new (coplanar neighbours joined into convex quads, classified, named by
// `rules` and by their material's own name -- one named as a room's export names its collision,
// `ground`, `wall` or `ceiling` then names, gives the surface tags among them), the tree built over
// them. False with the reason (docs/STUDIO.md, "Rooms").
bool build_collision(const CollisionSource& source, const RoomCollision& original, const CollisionRules& rules,
                     RoomCollision* out, CollisionReport* report, std::string* why);

// The height of the highest ground of `c` under (x, z) no higher than `top`
// (20.12); false when no ground is under it.
bool ground_under(const RoomCollision& c, double x, double z, double top, double* y);

// Empty when `built` is what the game can read in place of `original` (read back the same, faces
// convex with 3-4 corners and buildable planes, slots naming records, the tree as the walk wants, every
// name kept); else the first problem.
std::string check_collision(const RoomCollision& built, const RoomCollision& original);

}  // namespace studio
