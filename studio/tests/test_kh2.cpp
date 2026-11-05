// KH2's rooms in a room change: the change's `kh2` line read back, the first place of a KH2 room that
// shares no points with the room (KH2's units, the floors at one height, the middles together), and the
// markers a made collision leaves inside a wall or with no ground under them, named; another game's room placed
// by its units, its lowest main floor at the ground and the room's places on its floors. Synthetic only.
#include "test.h"

#include "core/rooms/kh2_source.h"
#include "core/rooms/room_change.h"
#include "core/rooms/room_collision.h"
#include "core/rooms/worlds.h"

#include <cmath>
#include <cstring>

using namespace studio;

namespace {

constexpr std::int32_t kUnit = 4096;

RoomFace flat(int x0, int z0, int x1, int z1, int y) {
    RoomFace f;
    f.corners = 4;
    f.v = {{{x0 * kUnit, y * kUnit, z0 * kUnit},
            {x0 * kUnit, y * kUnit, z1 * kUnit},
            {x1 * kUnit, y * kUnit, z1 * kUnit},
            {x1 * kUnit, y * kUnit, z0 * kUnit}}};
    f.bounds = {std::min(x0, x1) * kUnit, std::min(z0, z1) * kUnit, std::max(x0, x1) * kUnit, std::max(z0, z1) * kUnit};
    return f;
}

// A record of the 0x18-byte list: a marker named `name` at (x, y, z) units.
void add_marker(RoomCollision* c, const char* name, double x, double y, double z) {
    const std::size_t at = c->records18.size();
    c->records18.resize(at + 0x18, 0);
    std::memcpy(&c->records18[at], name, std::strlen(name));
    const std::int32_t place[3] = {static_cast<std::int32_t>(x * kUnit), static_cast<std::int32_t>(y * kUnit),
                                   static_cast<std::int32_t>(z * kUnit)};
    for (int a = 0; a < 3; ++a) {
        for (int b = 0; b < 4; ++b)
            c->records18[at + 8 + 4 * a + b] = static_cast<std::uint8_t>((place[a] >> (8 * b)) & 0xff);
    }
}

void quad(CollisionSource* s, std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> c,
          std::array<float, 3> d) {
    s->triangles.push_back({a, b, c});
    s->triangles.push_back({a, c, d});
    s->materials.insert(s->materials.end(), 2, "");
}

}  // namespace

TEST(a_room_change_keeps_its_kh2_map) {
    RoomReplacement r;
    r.world = "tt";
    r.room = 0;
    r.model = "tt02.gltf";
    r.kh2 = "tt02";
    r.place.scale = 0.01f;
    const auto file = studio_test::scratch("kh2_change") / "0.txt";
    studio_test::write(file, change_text(r));
    RoomReplacement back;
    std::string why;
    CHECK(read_change(file, &back, &why));
    CHECK(back.kh2 == "tt02" && back.model == "tt02.gltf" && back.place.scale == 0.01f);
    CHECK(change_text(back) == change_text(r));
}

TEST(the_pairs_are_read_and_a_mods_line_replaces_one) {
    std::vector<Kh2Pair> pairs;
    std::vector<std::string> notes;
    read_kh2_pairs("# a comment\n\ntt 0 tt02 ; model tt_02, textures tt02_*\r\nbb 5 bb04\n", "studio", &pairs, &notes);
    CHECK(notes.empty() && pairs.size() == 2);
    const Kh2Pair* p = kh2_pair_of(pairs, "tt", 0);
    CHECK(p != nullptr && p->map == "tt02" && p->evidence == "model tt_02, textures tt02_*");
    CHECK(kh2_pair_of(pairs, "bb", 5) != nullptr && kh2_pair_of(pairs, "bb", 5)->evidence.empty());
    CHECK(kh2_pair_of(pairs, "tt", 1) == nullptr);
    // a mod's file: one room taken to another map, one added, two lines it cannot read named
    read_kh2_pairs("tt 0 tt09 ; mine\nnm 3 nm06\ntt zero tt02\ntt 1 tt03 extra\n", "mod", &pairs, &notes);
    CHECK(pairs.size() == 3 && kh2_pair_of(pairs, "tt", 0)->map == "tt09");
    CHECK(kh2_pair_of(pairs, "nm", 3) != nullptr);
    CHECK(notes.size() == 2 &&
          notes[0] == "mod line 3: not <world> <room> <map> [turn <degrees>] [move <x> <y> <z>] ; <evidence>" &&
          notes[1].rfind("mod line 4:", 0) == 0);
    // where Days put a map: a turn and a move, or a move alone; none, not placed
    notes.clear();
    read_kh2_pairs("eh 16 eh18 turn 180 move 0.15 0.09 30.05 ; corners\nbb 2 bb08 move 14.93 0 0\nbb 3 bb08 move 1\n",
                   "placed", &pairs, &notes);
    const Kh2Pair* turned = kh2_pair_of(pairs, "eh", 16);
    CHECK(turned != nullptr && turned->placed && turned->turn_y == 180.0f && turned->move[2] == 30.05f &&
          turned->evidence == "corners");
    CHECK(kh2_pair_of(pairs, "bb", 2)->placed && kh2_pair_of(pairs, "bb", 2)->move[0] == 14.93f);
    CHECK(!kh2_pair_of(pairs, "tt", 0)->placed);
    CHECK(notes.size() == 1 && notes[0].rfind("placed line 3:", 0) == 0);
    // the studio's own: the Usual Spot is Twilight Town's room 0
    notes.clear();
    const auto own = kh2_pairs({}, &notes);
    CHECK(notes.empty() && kh2_pair_of(own, "tt", 0) != nullptr && kh2_pair_of(own, "tt", 0)->map == "tt02");
}

TEST(a_placed_point_goes_back_where_the_model_has_it) {
    RoomPlacement place;
    place.scale = 0.01f;
    place.turn = {0, 90, 0};
    place.move = {14.93f, -2, 3};
    const std::array<float, 3> v{100, 200, 300};
    const auto at = placed_point(place, v);
    // KH2's units to the game's, turned a quarter about y (x takes z), then moved
    CHECK(std::fabs(at[0] - (3 + 14.93f)) < 1e-4f && std::fabs(at[1] - 0) < 1e-4f &&
          std::fabs(at[2] - (-1 + 3)) < 1e-4f);
    const auto back = unplaced_point(place, at);
    CHECK(std::fabs(back[0] - 100) < 1e-2f && std::fabs(back[1] - 200) < 1e-2f && std::fabs(back[2] - 300) < 1e-2f);
    CHECK(same_place(place, place) && !same_place(place, RoomPlacement{}));
}

TEST(a_kh2_floor_is_raised_under_the_markers) {
    // a floor five units up from -4 to 4, and a roof over it at eight (facing down)
    khdays::assets::ImportedModel model;
    khdays::assets::NeutralMesh mesh;
    for (const auto& p :
         {std::array<float, 3>{-4, 5, -4}, std::array<float, 3>{-4, 5, 4}, std::array<float, 3>{4, 5, 4},
          std::array<float, 3>{4, 5, -4}, std::array<float, 3>{-4, 8, -4}, std::array<float, 3>{-4, 8, 4},
          std::array<float, 3>{4, 8, 4}, std::array<float, 3>{4, 8, -4}}) {
        khdays::assets::NeutralVertex v;
        v.position = p;
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6};
    model.model.meshes = {mesh};
    const SurfaceMap surfaces = surface_map(model);
    CHECK(surfaces.triangles.size() == 4);
    const auto heights = surfaces.heights_at(1.5f, -2.5f);
    CHECK(heights.size() == 2);
    // three markers on the game's ground, a tenth above 0; one far off, over nothing
    std::size_t on = 0;
    const float raise = raise_to_markers(surfaces, {{0, 0.1f, 0}, {1, 0.1f, 1}, {2, 0.1f, -2}, {40, 0.1f, 40}}, &on);
    CHECK(on == 3 && std::fabs(raise + 5.0f) < 1e-4f);
    // no floor under any: nothing raised
    CHECK(raise_to_markers(surfaces, {{40, 0, 40}}, &on) == 0.0f && on == 0);
}

TEST(a_model_cut_to_its_room_keeps_what_lies_around_it) {
    // the room: a ground from -4 to 4, closed in by walls along z = -4 and z = 4 only
    RoomCollision room;
    room.faces[kGround] = {flat(-4, -4, 4, 4, 0)};
    const RoomZone zone = room_zone(room, 2.0f);
    CHECK(zone.holds(0, 0) && zone.holds(5.5f, 0) && !zone.holds(7, 0) && !zone.holds(0, -30));
    // a triangle over it, one 3 units past its edge, one far off, and a sky far off
    khdays::assets::ImportedModel model;
    auto triangle = [](float x, float z) {
        khdays::assets::NeutralMesh mesh;
        for (const auto& p :
             {std::array<float, 3>{x, 0, z}, std::array<float, 3>{x + 1, 0, z}, std::array<float, 3>{x, 0, z + 1}}) {
            khdays::assets::NeutralVertex v;
            v.position = p;
            mesh.vertices.push_back(v);
        }
        mesh.indices = {0, 1, 2};
        return mesh;
    };
    khdays::assets::NeutralMesh near = triangle(0, 0), past = triangle(5, 0), far = triangle(40, 40),
                                sky = triangle(60, 60);
    far.texture_name = "far.png";
    sky.sky = true;
    model.model.meshes = {near, past, far, sky};
    model.textures = {{"far.png", {}}};
    CHECK(cut_to_zone(&model, zone) == 1);
    CHECK(model.model.meshes.size() == 3 && model.model.meshes[2].sky && model.textures.empty());
    // no ground, no zone: nothing is held
    CHECK(!room_zone(RoomCollision{}, 5.0f).holds(0, 0));
}

TEST(a_kh2_room_sharing_no_points_is_placed_by_its_units) {
    // the game's room: a ground two units up, from -4 to 4
    RoomCollision room;
    room.faces[kGround] = {flat(-4, -4, 4, 4, 2)};
    // KH2's: a floor 500 KH2 units up, from x 1000 to 3000 and z -1000 to 1000
    khdays::assets::ImportedModel kh2;
    khdays::assets::NeutralMesh mesh;
    for (const auto& p : {std::array<float, 3>{1000, 500, -1000}, std::array<float, 3>{1000, 500, 1000},
                          std::array<float, 3>{3000, 500, 1000}, std::array<float, 3>{3000, 500, -1000}}) {
        khdays::assets::NeutralVertex v;
        v.position = p;
        mesh.vertices.push_back(v);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    kh2.model.meshes = {mesh};
    // a sky far off does not count for the room's middle
    khdays::assets::NeutralMesh sky = mesh;
    sky.sky = true;
    for (auto& v : sky.vertices)
        v.position[0] += 900000;
    kh2.model.meshes.push_back(sky);
    const RoomAlignment a = kh2_alignment({}, room, kh2);
    CHECK(a.verdict == RoomAlignment::kh2_units);
    CHECK(a.place.scale == 0.01f);
    CHECK(std::abs(a.place.move[0] + 20) < 1e-3f && std::abs(a.place.move[1] + 3) < 1e-3f &&
          std::abs(a.place.move[2]) < 1e-3f);
}

TEST(markers_in_a_wall_or_over_nothing_are_named) {
    RoomCollision original;
    original.header.assign(0xb0, 0);
    original.faces[kGround] = {flat(-4, -4, 4, 4, 0)};
    add_marker(&original, "pent01_0", 0, 0, 0);  // on the floor, in the open
    add_marker(&original, "ent_02", 2, 0, 0);    // where the new wall stands
    add_marker(&original, "btl_03", 20, 0, 20);  // off the new floor
    build_tree(&original, 0, 0, 32 * kUnit, 2 * kUnit);
    // the new floor and a wall across x = 2
    CollisionSource source;
    quad(&source, {-4, 0, -4}, {-4, 0, 4}, {4, 0, 4}, {4, 0, -4});
    quad(&source, {2, 0, -4}, {2, 3, -4}, {2, 3, 4}, {2, 0, 4});
    RoomCollision built;
    CollisionReport report;
    std::string why;
    CHECK(build_collision(source, original, CollisionRules{}, &built, &report, &why));
    bool in_wall = false, floating = false, wrong = false;
    for (const auto& w : report.warnings) {
        if (w.kind == RoomWarning::marker_in_wall)
            (w.args[0] == "ent_02" && w.args[1] == "2.00" ? in_wall : wrong) = true;
        if (w.kind == RoomWarning::marker_floating)
            (w.args[0] == "btl_03" ? floating : wrong) = true;
    }
    CHECK(in_wall && floating && !wrong);
}

TEST(another_games_room_stands_the_rooms_places_on_its_floors) {
    // the game's room: a ground at 0 from -4 to 4, its origin and two markers on it
    RoomCollision room;
    room.faces[kGround] = {flat(-4, -4, 4, 4, 0)};
    add_marker(&room, "pent01_0", 3, 0, 0);
    add_marker(&room, "pent01_1", -3, 0, 0);
    // another game's, in metres: a street at 0 (x 100 to 120, z 0 to 10), a wider level 50 up (x 100 to 130, z 20
    // to 40) and a step's worth of floor 5 down
    auto floor = [](float x0, float z0, float x1, float z1, float y) {
        khdays::assets::NeutralMesh mesh;
        for (const auto& p : {std::array<float, 3>{x0, y, z0}, std::array<float, 3>{x0, y, z1},
                              std::array<float, 3>{x1, y, z1}, std::array<float, 3>{x1, y, z0}}) {
            khdays::assets::NeutralVertex v;
            v.position = p;
            mesh.vertices.push_back(v);
        }
        mesh.indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    };
    khdays::assets::ImportedModel other;
    other.model.meshes = {floor(100, 0, 120, 10, 0), floor(100, 20, 130, 40, 50), floor(90, 0, 91, 1, -5)};
    RoomAlignment a = kh2_alignment({}, room, other, 1.0f);
    CHECK(a.verdict == RoomAlignment::kh2_units && a.place.scale == 1.0f);
    const auto [on, of] = onto_floors(other, room, &a);
    // the street (the lowest floor holding a quarter of the widest's area) at the room's ground, under its places
    CHECK(on == 3 && of == 3);
    CHECK(std::abs(a.place.move[1]) < 1e-3f);
    for (const float x : {-3.0f, 0.0f, 3.0f}) {
        CHECK(x - a.place.move[0] >= 100 && x - a.place.move[0] <= 120);
        CHECK(-a.place.move[2] >= 0 && -a.place.move[2] <= 10);
    }
}
