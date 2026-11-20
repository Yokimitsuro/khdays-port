// A mission world's archive (`mi/wd/wd_<code>`) as the game reads it, and written back: the room table,
// gate models, rooms, their collision, faces and quadtree (docs/FORMATS.md, "Mission worlds"). What is
// not read is kept byte for byte.
#pragma once

#include "core/files/containers.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace studio {

// A face: 3 or 4 corners in 20.12, their bounds on x and z, its flags, its plane and edge planes,
// four slots (0 gates, 1 and 2 surfaces and opening walls; a ground's 3 a kind) and a ground's word
// not read (docs/FORMATS.md, "Mission worlds").
struct RoomFace {
    std::array<std::int32_t, 4> bounds{};  // min x, min z, max x, max z
    std::uint16_t flags = 0;
    std::uint16_t corners = 0;
    std::array<std::uint8_t, 0x3c> planes{};
    std::array<std::array<std::int32_t, 3>, 4> v{};
    std::array<std::uint8_t, 4> slots{0xff, 0xff, 0xff, 0xff};
    std::uint32_t extra = 0;
};

// A quadtree node: its flags (bits 0-3 grounds, 4-7 walls, 8-11 ceilings: bit
// c when child c's subtree holds faces of that list), the first face of its
// run in each list (-1: none), a word set at run time, its parent and children
// (indices, -1: none).
struct RoomNode {
    std::uint16_t flags = 0;
    std::array<std::int16_t, 3> first{-1, -1, -1};
    std::uint32_t objects = 0;
    std::int32_t parent = -1;
    std::array<std::int32_t, 4> children{-1, -1, -1, -1};
};

// A named record: what faces name (surface tags `nohit` ..., gates `gate%02d`,
// switchable walls `col_wall%02d` ...): its name (8 characters), a word not
// read, its tags (the game writes them at load for the surface tags' names),
// a pointer (0 on disk).
struct NamedRecord {
    std::array<char, 8> name{};
    std::uint32_t kept = 0;
    std::array<std::uint8_t, 4> tags{};
    std::uint32_t pointer = 0;
    std::string text() const;
};

// The surface tags' names, in the order of the game's table (`data_ov002_0207e640`: tags 2 to 8).
inline constexpr std::array<const char*, 7> kSurfaceTags{"nohit", "nocatch", "sea", "nocam", "slide", "sea2", "sicon"};

enum FaceList { kGround = 0, kWall = 1, kCeiling = 2 };

// Whether slot `position` of a face of list `list` names a record (see RoomFace).
inline bool slot_names(int list, std::size_t position) {
    return !(list == kGround && position == 3);
}

struct RoomCollision {
    std::vector<std::uint8_t> header;     // 0xb0 bytes as read; counts and offsets set when written
    std::vector<std::uint8_t> records18;  // the header's +0x76 records of 0x18 bytes
    std::vector<RoomNode> nodes;
    std::array<std::vector<RoomFace>, 3> faces;  // grounds, walls, ceilings
    std::vector<NamedRecord> named;

    // the quadtree's root: its centre on x and z, its size and the smallest node's
    std::int32_t root(int word) const;
    void set_root(std::int32_t centre_x, std::int32_t centre_z, std::int32_t size, std::int32_t smallest);
};

// A placement marker (the collision's 0x18-byte records): its name, its place (20.12) and a word whose values read as a
// facing in 65536ths of a turn. `pentKK_i` is the i-th place a character
// entering through gate KK appears (`Ov002_ResolveNamedPlacement`).
struct Marker {
    std::string name;
    std::array<std::int32_t, 3> place{};
    std::int32_t extra = 0;
};
std::vector<Marker> markers(const RoomCollision& c);
// The markers written back into `c` (the same names, in the same order).
void set_markers(RoomCollision* c, const std::vector<Marker>& m);

bool parse_collision(const std::vector<std::uint8_t>& data, RoomCollision* out, std::string* why);
std::vector<std::uint8_t> write_collision(const RoomCollision& c);

struct LinkRow {
    std::uint8_t key = 0, target = 0, pad = 0;
    std::int8_t gate_state = 0;
    std::uint16_t flags = 0;
    std::int16_t radius = 0;
};

struct RoomObject {
    std::uint8_t flags = 0, member = 0;
    std::uint16_t rest = 0;
};

struct WorldRoom {
    int index = 0;
    std::int8_t id = 0;
    int collision_member = 0;
    std::vector<LinkRow> links;
    std::vector<RoomObject> objects;
    std::int32_t budget = 0;
};

// One of a gate's five models (Ov002_BuildModelSlot): its kind, the archive entry it loads, a turn,
// its position (20.12) and its scale.
struct GateModelPart {
    std::int8_t kind = 0;
    std::uint8_t member = 0;
    std::uint16_t turn = 0;
    std::array<std::int32_t, 3> position{};
    std::array<std::int32_t, 3> scale{};
};

struct GateModel {
    std::int8_t key = 0;
    std::array<std::uint8_t, 3> pad{};
    std::array<GateModelPart, 5> parts{};
};

struct World {
    std::string code;
    P2File archive;
    std::vector<WorldRoom> rooms;  // read from entry 0, which is written back as it was
    std::vector<GateModel> gate_models;
    std::vector<std::uint8_t> gate_tail;  // entry 1's bytes past its records (none in the game's)
};

// The game path of world `code`.
std::string world_path(const std::string& code);

// The worlds shipped in the extraction (`mi/wd/wd_*`), by code.
std::vector<std::string> world_codes(const std::filesystem::path& nitrofs);

bool parse_world(const std::vector<std::uint8_t>& file, const std::string& code, World* out, std::string* why);
bool load_world(const std::filesystem::path& nitrofs, const std::string& code, World* out, std::string* why);

std::vector<std::uint8_t> write_gate_models(const World& w);

// The world's file again: entry 1 from its gate models, the collision entries
// in `collisions` (archive member -> data) replaced, every other entry as read.
std::vector<std::uint8_t> write_world(const World& w, const std::map<int, std::vector<std::uint8_t>>& collisions);

// --check-worlds: every world rebuilt with nothing changed and every room's
// collision written back from what was read, each compared with the game's;
// then each room's tree checked and rebuilt (room_collision.h). One line per
// difference and a total; false when anything differs.
bool check_all_worlds(const std::filesystem::path& nitrofs, const std::function<void(const std::string&)>& line);

}  // namespace studio
