// The rooms a mod adds (spec 040): their list read and written back, their keys, and an added room's first place --
// its lowest main floor at height 0, one of its floors under the origin -- and its map from its floors. Synthetic only.
#include "test.h"

#include "core/rooms/added_rooms.h"
#include "core/rooms/room_map.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace studio;

namespace {

namespace fs = std::filesystem;

khdays::assets::NeutralMesh floor(float x0, float z0, float x1, float z1, float y) {
    khdays::assets::NeutralMesh mesh;
    for (const auto& p : {std::array<float, 3>{x0, y, z0}, std::array<float, 3>{x0, y, z1},
                          std::array<float, 3>{x1, y, z1}, std::array<float, 3>{x1, y, z0}}) {
        khdays::assets::NeutralVertex v;
        v.position = p;
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

}  // namespace

TEST(an_added_rooms_list_written_and_read_back) {
    const fs::path mod = fs::temp_directory_path() / "khdays_added_rooms_test";
    std::error_code ec;
    fs::remove_all(mod, ec);
    fs::create_directories(mod);
    std::string why;
    CHECK(write_added_rooms(mod, {{"plaza", "eh"}, {"street-2", "tt"}}, &why));
    std::vector<std::string> notes;
    const auto rooms = read_added_rooms(mod, &notes);
    CHECK(notes.empty() && rooms.size() == 2 && rooms[0].key == "plaza" && rooms[0].world == "eh" &&
          rooms[1].key == "street-2" && rooms[1].world == "tt");
    // a line not read is told, the others kept
    {
        std::ofstream add(mod / "rooms" / "rooms.txt", std::ios::app);
        add << "bad key! eh\nlonely\n";
    }
    CHECK(read_added_rooms(mod, &notes).size() == 2 && notes.size() == 2);
    CHECK(added_room_folder(mod, "eh", "plaza") == mod / "rooms" / "eh" / "plaza");
    CHECK(remove_added_room(mod, "eh", "plaza", &why) && read_added_rooms(mod, nullptr).size() == 1);
    CHECK(!remove_added_room(mod, "eh", "plaza", &why));
    fs::remove_all(mod, ec);
}

TEST(an_added_rooms_key) {
    CHECK(added_room_key("plaza_2") && added_room_key("Street-1"));
    CHECK(!added_room_key("") && !added_room_key("two words") && !added_room_key("plaza/2"));
}

TEST(an_added_room_placed_on_its_lowest_main_floor_at_the_origin) {
    // another game's room in metres: its street at 5 (x 100 to 120, z 0 to 10), a wider level 50 up
    khdays::assets::ImportedModel room;
    room.model.meshes = {floor(100, 0, 120, 10, 5), floor(100, 20, 130, 40, 55)};
    RoomPlacement place;
    CHECK(added_room_place(room, 1.0f, &place));
    CHECK(place.scale == 1.0f && std::abs(place.move[1] + 5) < 1e-3f);
    // the origin over the street
    CHECK(-place.move[0] >= 100 && -place.move[0] <= 120 && -place.move[2] >= 0 && -place.move[2] <= 10);
}

namespace {

// A floor quad from (x0, z0) to (x1, z1) at height y, turned up (counter-clockwise from above, y up).
void floor_quad(CollisionSource* s, float x0, float z0, float x1, float z1, float y) {
    s->triangles.push_back({{{x0, y, z0}, {x0, y, z1}, {x1, y, z1}}});
    s->triangles.push_back({{{x0, y, z0}, {x1, y, z1}, {x1, y, z0}}});
}

}  // namespace

TEST(an_added_rooms_map_drawn_from_its_floors) {
    CollisionSource s;
    floor_quad(&s, -10, -6, 10, 6, 0);                                // the main floor
    floor_quad(&s, 2, 2, 6, 4, 3);                                    // a ledge above it
    s.triangles.push_back({{{-10, 0, 6}, {-10, 5, 6}, {10, 5, 6}}});  // a wall: not drawn
    const RoomMap map = room_map(s, CollisionRules{}.ground_limit);
    CHECK(std::fabs(map.cell - 0.4F) < 1e-6F);   // the game's finest scale: 20 x 12 units fit
    CHECK(map.width == 15 && map.height == 10);  // the panel's part at least
    CHECK(map.origin_x == 60 && map.origin_y == 40);
    const auto at = [&](float x, float z) {
        const int px = map.origin_x + static_cast<int>(std::floor(x / map.cell));
        const int py = map.origin_y + static_cast<int>(std::floor(z / map.cell));
        return map.pixels[static_cast<std::size_t>(py) * RoomMap::kWidth + static_cast<std::size_t>(px)];
    };
    CHECK(at(-5, -3) == kMapFloor);
    CHECK(at(4, 3) == kMapAbove);
    CHECK(at(2.1F, 3) == kMapEdge);   // the ledge's edge over the floor
    CHECK(at(-9.9F, 0) == kMapEdge);  // the floor's edge
    CHECK(at(-12, 0) == kMapNone);
    // a room too wide for the finest scale: the first of the game's that holds it
    CollisionSource wide;
    floor_quad(&wide, -100, -20, 100, 20, 0);
    const RoomMap far = room_map(wide, CollisionRules{}.ground_limit);
    CHECK(std::fabs(far.cell - 1.2F) < 1e-5F && far.width == 22);
    CHECK(
        far.pixels[static_cast<std::size_t>(far.origin_y) * RoomMap::kWidth + static_cast<std::size_t>(far.origin_x)] ==
        kMapFloor);
    // written: its picture's colours as the palette's, its numbers
    const fs::path folder = fs::temp_directory_path() / "khdays_room_map_test";
    std::string why;
    CHECK(write_room_map(map, folder, "plaza", {}, &why));
    std::ifstream text(folder / "map.txt");
    const std::string all((std::istreambuf_iterator<char>(text)), std::istreambuf_iterator<char>());
    CHECK(all.find("origin 60 40\n") != std::string::npos && all.find("size 15 10\n") != std::string::npos &&
          all.find("cell 0.4\n") != std::string::npos && all.find("name plaza\n") != std::string::npos);
    CHECK(fs::file_size(folder / "map.png") > 0);
    std::error_code ec;
    fs::remove_all(folder, ec);
}
