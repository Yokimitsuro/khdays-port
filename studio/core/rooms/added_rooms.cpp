#include "core/rooms/added_rooms.h"

#include "core/files/file_write.h"
#include "core/files/paths.h"
#include "core/rooms/room_collision.h"
#include "core/rooms/room_map.h"
#include "core/rooms/worlds.h"

#include <fstream>
#include <set>
#include <sstream>

namespace studio {

namespace fs = std::filesystem;

namespace {

bool fail_with(std::string* why, const std::string& text) {
    if (why != nullptr)
        *why = text;
    return false;
}

fs::path list_file(const fs::path& mod_dir) {
    return mod_dir / "rooms" / "rooms.txt";
}

}  // namespace

bool added_room_key(const std::string& key) {
    if (key.empty())
        return false;
    for (const char c : key) {
        const bool fine = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                          c == '-';
        if (!fine)
            return false;
    }
    return true;
}

fs::path added_room_folder(const fs::path& mod_dir, const std::string& world, const std::string& key) {
    return mod_dir / "rooms" / path_u8(world) / path_u8(key);
}

std::vector<AddedRoom> read_added_rooms(const fs::path& mod_dir, std::vector<std::string>* notes) {
    std::vector<AddedRoom> out;
    std::ifstream in(list_file(mod_dir), std::ios::binary);
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream words(line.substr(0, line.find('#')));
        AddedRoom r;
        if (!(words >> r.key))
            continue;
        if (!(words >> r.world) || r.world.size() != 2 || !added_room_key(r.key)) {
            if (notes != nullptr)
                notes->push_back("rooms/rooms.txt line " + std::to_string(number) + ": not <key> <world>");
            continue;
        }
        out.push_back(r);
    }
    return out;
}

bool write_added_rooms(const fs::path& mod_dir, const std::vector<AddedRoom>& rooms, std::string* why) {
    std::string text = "# The rooms this mod adds to the game's worlds: <key> <world>; their files in rooms/<world>/<key>/\n";
    for (const auto& r : rooms)
        text += r.key + " " + r.world + "\n";
    const auto result = write_into_mod(
        mod_dir, list_file(mod_dir),
        [&](const fs::path& temporary) -> std::string {
            std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
            f << text;
            return f ? std::string() : std::string("could not be written");
        },
        [](const fs::path&) { return std::string(); });
    return result.ok || fail_with(why, "rooms/rooms.txt: " + result.why);
}

bool added_room_place(const khdays::assets::ImportedModel& floors, float units, RoomPlacement* place) {
    // a ground at height 0 under the origin, no markers: lined up as another game's room is on a room's ground
    RoomCollision ground;
    RoomFace f;
    f.corners = 4;
    const std::int32_t unit = 4096;
    f.v = {{{-unit, 0, -unit}, {-unit, 0, unit}, {unit, 0, unit}, {unit, 0, -unit}}};
    f.bounds = {-unit, -unit, unit, unit};
    ground.faces[kGround] = {f};
    RoomAlignment a = kh2_alignment({}, ground, floors, units);
    if (a.verdict != RoomAlignment::kh2_units)
        return false;
    const auto [on, of] = onto_floors(floors, ground, &a);
    *place = a.place;
    return on == of;
}

bool add_room(const fs::path& nitrofs, const fs::path& mod_dir, const std::string& world, const std::string& key,
              const fs::path& model, const RoomPlacement& place, const fs::path& collision_model,
              std::vector<std::string>* lines, std::string* why) {
    if (!added_room_key(key))
        return fail_with(why, "a room's key is letters, digits, _ and -: " + key);
    World w;
    if (!load_world(nitrofs, world, &w, why))
        return false;
    if (w.rooms.empty())
        return fail_with(why, world_path(world) + " has no room to take records from");
    const auto& data = w.archive.entries[static_cast<std::size_t>(w.rooms[0].collision_member)].data;
    RoomCollision original;
    if (data.empty() || !parse_collision(data, &original, why))
        return fail_with(why, world_path(world) + ": room 0 has no collision to take records from");
    // the floors and walls: from the collision model when given, else the model's visible triangles
    khdays::assets::ImportedModel placed;
    if (!placed_model(collision_model.empty() ? model : collision_model, place, &placed, why))
        return false;
    const CollisionSource source = collision_source(placed, collision_model.empty());
    RoomCollision built;
    CollisionReport report;
    if (!build_collision(source, original, CollisionRules{}, &built, &report, why))
        return false;
    const std::string problem = check_collision(built, original);
    if (!problem.empty())
        return fail_with(why, "the collision made is not one the game reads: " + problem);
    const fs::path folder = added_room_folder(mod_dir, world, key);
    const auto bytes = write_collision(built);
    const auto saved = write_into_mod(
        mod_dir, folder / "collision.bin",
        [&](const fs::path& temporary) -> std::string {
            std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return f ? std::string() : std::string("could not be written");
        },
        [&](const fs::path& temporary) -> std::string {
            std::ifstream f(temporary, std::ios::binary);
            const std::vector<std::uint8_t> back((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            RoomCollision again;
            std::string trouble;
            if (!parse_collision(back, &again, &trouble))
                return "it does not read back: " + trouble;
            return check_collision(again, original);
        });
    if (!saved.ok)
        return fail_with(why, "collision.bin: " + saved.why);
    // its map on the lower screen, from its floors
    const RoomMap map = room_map(source, CollisionRules{}.ground_limit);
    if (!write_room_map(map, folder, key, room_map_palette(nitrofs), why))
        return false;
    // the model drawn
    khdays::assets::ImportedModel drawn;
    if (!placed_model(model, place, &drawn, why))
        return false;
    std::set<std::string> written;
    if (!write_model_as(mod_dir, folder / "room.gltf", "room",
                        "rooms/" + world + "/" + key + "/", drawn, &written, why)) {
        return false;
    }
    // listed once
    std::vector<std::string> notes;
    auto rooms = read_added_rooms(mod_dir, &notes);
    bool listed = false;
    for (const auto& r : rooms)
        listed = listed || (r.key == key && r.world == world);
    if (!listed) {
        rooms.push_back({key, world});
        if (!write_added_rooms(mod_dir, rooms, why))
            return false;
    }
    if (lines != nullptr) {
        lines->push_back(world + " room " + key + ": " + std::to_string(report.faces[kGround]) + " grounds, " +
                         std::to_string(report.faces[kWall]) + " walls, " + std::to_string(report.faces[kCeiling]) +
                         " ceilings, " + std::to_string(report.bytes) + " bytes");
        for (const auto& warning : report.warnings)
            lines->push_back(world + " room " + key + ": " + warning_text(warning));
        lines->push_back(world + " room " + key + ": its map at " + std::to_string(map.cell) + " a pixel, " +
                         std::to_string(map.width) + " x " + std::to_string(map.height) + " tiles");
        lines->push_back(u8(folder) + " written");
    }
    return true;
}

bool remove_added_room(const fs::path& mod_dir, const std::string& world, const std::string& key, std::string* why) {
    std::vector<std::string> notes;
    auto rooms = read_added_rooms(mod_dir, &notes);
    std::vector<AddedRoom> kept;
    for (const auto& r : rooms) {
        if (!(r.key == key && r.world == world))
            kept.push_back(r);
    }
    if (kept.size() == rooms.size())
        return fail_with(why, "the mod adds no room " + key + " to " + world);
    std::error_code ec;
    fs::remove_all(added_room_folder(mod_dir, world, key), ec);
    return write_added_rooms(mod_dir, kept, why);
}

}  // namespace studio
