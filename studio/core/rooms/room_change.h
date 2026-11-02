// A room of a mission world replaced by the modder's model: the change in mods/<Mod>/studio/rooms/
// (which the port ignores), and what the port reads made from it over the player's copy -- the world's
// archive with new collision, and the room's models as glTF (docs/STUDIO.md, "Rooms"). A room is its
// index in the world's table.
#pragma once

#include "core/rooms/room_collision.h"
#include "core/rooms/worlds.h"

#include "khdays/assets/model_import.h"

#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace studio {

// Where the modder's model sits in the room: scaled, turned about x, then y,
// then z (degrees), then moved (the game's units: 1 = 4096 in 20.12, the
// units --room-model exports in).
struct RoomPlacement {
    float scale = 1.0f;
    std::array<float, 3> turn{0, 0, 0};
    std::array<float, 3> move{0, 0, 0};
};

// What a room's model (one of its objects, by archive member) becomes.
enum class ObjectUse { model, hide, keep };

struct RoomReplacement {
    std::string world;      // its code: tt, aw ...
    int room = -1;          // its index in the world's table
    std::string model;      // the visible model's file, in the change's folder
    std::string kh2;        // the KH2 map it was made from (tt02); empty: a model of the modder's
    std::string source;     // another collection's map it was made from (<game>:<map>: ddd:eh_01); empty: none
    std::string collision;  // a separate collision model's, or empty: the visible one
    // the game's own grounds, walls and ceilings kept: only what is seen is
    // replaced (a room remade in the same place needs no collision of its own)
    bool keep_collision = false;
    RoomPlacement place;
    // the model cut to the room: only what lies within this many units (from above) of the game's grounds
    // and walls, or between them, is kept; below 0, all of it
    float zone = -1;
    float ground_limit = 0x333 / 4096.0f;  // the unit normal's y above which a face is a ground
    // per object (archive member): unset, the room's first model takes the
    // modder's and the others are hidden
    std::map<int, ObjectUse> objects;
    std::map<std::string, std::vector<std::string>> names_by_material;
    std::map<std::size_t, std::vector<std::string>> names_by_triangle;
    // the original's gates and walls that open not kept where the game had them
    std::set<std::string> drop_original;
    // placement markers moved (by name: x, y, z in 20.12, the facing word)
    std::map<std::string, std::array<std::int32_t, 4>> markers;
    // gate models moved (by link key and model 0-4: their position x, y, z in 20.12)
    std::map<std::pair<int, int>, std::array<std::int32_t, 3>> gate_points;
};

std::filesystem::path rooms_folder(const std::filesystem::path& mod_dir, const std::string& code);
std::filesystem::path change_file(const std::filesystem::path& mod_dir, const std::string& code, int room);
std::filesystem::path change_models(const std::filesystem::path& mod_dir, const std::string& code, int room);

bool read_change(const std::filesystem::path& file, RoomReplacement* out, std::string* why);
std::string change_text(const RoomReplacement& r);

// The mod's replaced rooms of world `code` (by their files), and the worlds it changes.
std::vector<RoomReplacement> changes_of(const std::filesystem::path& mod_dir, const std::string& code,
                                        std::vector<std::string>* problems);
std::vector<std::string> changed_worlds(const std::filesystem::path& mod_dir);

// A point of the model where `place` puts it, and a placed point back where the model has it.
std::array<float, 3> placed_point(const RoomPlacement& place, const std::array<float, 3>& v);
std::array<float, 3> unplaced_point(const RoomPlacement& place, const std::array<float, 3>& v);
bool same_place(const RoomPlacement& a, const RoomPlacement& b);

// A model (read as the port reads it) with every vertex posed as the file
// places it and then placed; its bones, if any, left (a room's model stands still).
void place_model(khdays::assets::ImportedModel* model, const RoomPlacement& place);

// Where a model made from the game's room lines up with it again: the quarter turns and scale under
// which most of its points fall on the room's own (`share`, 0.25 or more: lined up; the move left at
// 0). A model that shares nothing with the room keeps its placement.
struct RoomAlignment {
    RoomPlacement place;
    double share = 0;  // under `place`
    double as_is = 0;  // with nothing changed
    // what to do: `turned` -- `place` puts it on the room (at least 5% of its
    // points, twice as many as it has as it is); `in_place` -- it lines up as
    // it is (3% or more: a room remade in the same place keeps some of the
    // game's points); `unrelated` -- it shares nothing worth saying
    // `kh2_units` (a KH2 room sharing no points with the room): KH2's units as the game's, its floor under its
    // middle at the height of the room's ground under the room's middle, the two middles together
    enum Verdict { turned, in_place, unrelated, kh2_units } verdict = unrelated;
};
RoomAlignment align_with_room(const std::vector<std::array<float, 3>>& room_points,
                              const khdays::assets::ImportedModel& model);
// A KH2 room's first place over a game room (`room` its collision): lined up by its points when it shares
// them (the Usual Spot is the same place in both games), else by `kh2_units` -- KH2's (0.01), or `units` for another
// game's room.
RoomAlignment kh2_alignment(const std::vector<std::array<float, 3>>& room_points, const RoomCollision& room,
                            const khdays::assets::ImportedModel& kh2, float units = 0.01f);
bool placed_model(const std::filesystem::path& model, const RoomPlacement& place, khdays::assets::ImportedModel* out,
                  std::string* why);
// Writes `model` as `gltf` under the mod, its pictures beside it named after `name`; each file written put in `written`
// as `listed` followed by its name.
bool write_model_as(const std::filesystem::path& mod_dir, const std::filesystem::path& gltf, const std::string& name,
                    const std::string& listed, const khdays::assets::ImportedModel& model, std::set<std::string>* written,
                    std::string* why);

// A room from above, in cells of a quarter unit: those its grounds and walls cover, those they close in,
// and those within `margin` units of either.
struct RoomZone {
    float cell = 0.25f;
    std::int64_t x0 = 0, z0 = 0;  // the first cell's indices
    int width = 0, depth = 0;
    std::vector<char> in;  // width * depth, row by row along x
    bool holds(float x, float z) const;
};
RoomZone room_zone(const RoomCollision& room, float margin);
// how far past the room's floor and walls a cut model is kept, at first (the game's units)
constexpr float kZoneMargin = 6.0f;

// A model's surfaces that are not walls (their normal's y past 0.3 either way), found from above.
struct SurfaceMap {
    std::vector<std::array<std::array<float, 3>, 3>> triangles;
    std::vector<char> up;  // per triangle: it faces up (a floor), else down
    std::map<std::pair<std::int64_t, std::int64_t>, std::vector<std::size_t>> cells;  // a unit from above
    std::vector<std::size_t> wide;  // those over more than 64 by 64 cells, asked every time
    struct Height {
        float y;
        bool up;
    };
    std::vector<Height> heights_at(float x, float z) const;
};
SurfaceMap surface_map(const khdays::assets::ImportedModel& placed);

// Another game's room placed by its units (`a`'s kh2_units, `floors` its floors and walls) moved across so that most
// of the game room's places stand on one of its floors: its markers and its origin (where missions put the player in
// rooms with no gate). Each move that puts a floor's middle under a place is tried; the one with most places on a floor,
// then the shortest, is kept. How many places stand on a floor then, of how many.
std::pair<std::size_t, std::size_t> onto_floors(const khdays::assets::ImportedModel& floors, const RoomCollision& room,
                                                RoomAlignment* a);

// How far to raise the model so that most of `markers` stand on one of its floors, a tenth above it as the
// game's do (at most 50 units either way); 0 when none can. `on`: how many markers then do.
float raise_to_markers(const SurfaceMap& surfaces, const std::vector<std::array<float, 3>>& markers, std::size_t* on);

// `placed` cut to `zone`: each triangle with no corner and no middle over it left out (skies kept), the
// meshes and pictures left unused removed. The triangles left out.
std::size_t cut_to_zone(khdays::assets::ImportedModel* placed, const RoomZone& zone);

// The model's triangles, placed, as the collision builder takes them: a two-sided mesh's steep faces
// doubled the other way; with `visible`, a translucent material's triangles passable.
CollisionSource collision_source(const khdays::assets::ImportedModel& placed, bool visible = false);

// The world with the change's gate model points moved.
void apply_gate_points(World* w, const RoomReplacement& r);

// The use each of the room's objects gets (by member, in the room's order).
std::vector<std::pair<int, ObjectUse>> object_uses(const WorldRoom& room, const RoomReplacement& r);

// The models the graphics dump names in a world's archive member
// (`mi/wd/wd_<code>#<member>/mdl/<name>`).
std::vector<std::string> member_models(const std::filesystem::path& graphics_dump, const std::string& code, int member);

// The rooms other than `room` that draw member `member` (models) or use it
// as their collision.
std::vector<int> rooms_sharing(const World& w, int room, int member, bool collision);

// One replaced room's collision, made as it will be written.
struct BuiltRoom {
    RoomCollision collision;
    CollisionReport report;
    std::vector<RoomWarning> warnings;  // the report's and the sharing ones
};
bool build_room(const World& w, const std::filesystem::path& mod_dir, const RoomReplacement& r, BuiltRoom* out,
                std::string* why);

// The rooms that change with this one (sharing its collision or a model it
// does not keep), as warnings.
std::vector<RoomWarning> sharing_warnings(const World& w, const RoomReplacement& r);

struct WorldOutcome {
    std::vector<std::string> lines;  // per room: counts and each warning
    bool removed = false;            // no room of the world replaced any more: its outputs removed
};

// Writes world `code`'s outputs from the mod's changes:
// the archive (checked before it replaces anything) and the rooms' models;
// with no change left, removes them. False with the reason; what was there stays.
bool write_world_outputs(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                         const std::filesystem::path& mod_dir, const std::string& code, WorldOutcome* out,
                         std::string* why);

// Saves a change: the model files (and their companions) copied beside it,
// the change written, then the world's outputs.
bool save_change(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                 const std::filesystem::path& mod_dir, RoomReplacement r, const std::filesystem::path& model_source,
                 const std::filesystem::path& collision_source_file, WorldOutcome* out, std::string* why);

// Removes a room's change and the models it wrote, then writes its world again.
bool revert_change(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                   const std::filesystem::path& mod_dir, const std::string& code, int room, WorldOutcome* out,
                   std::string* why);

}  // namespace studio
