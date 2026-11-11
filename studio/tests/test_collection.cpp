// The collections' rooms: the games' table, MD5 as the packages key their paths, and the maps a package's
// index holds found by each game's pattern. Synthetic only: an index made here of the paths' keys.
#include "test.h"

#include "core/files/md5.h"
#include "core/files/paths.h"
#include "core/rooms/collection_source.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace studio;

namespace {

std::string hex(const std::array<std::uint8_t, 16>& d) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (const auto b : d) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

// A package's index of `paths` (32-byte entries: the key, then offset and sizes, here zero).
std::vector<std::uint8_t> index_of(const std::vector<std::string>& paths) {
    std::vector<std::uint8_t> hed;
    for (const auto& p : paths) {
        const auto key = md5(p);
        hed.insert(hed.end(), key.begin(), key.end());
        hed.insert(hed.end(), 16, 0);
    }
    return hed;
}

std::vector<std::string> names(const std::vector<CollectionMap>& maps) {
    std::vector<std::string> out;
    for (const auto& m : maps)
        out.push_back(m.world + "/" + m.name);
    return out;
}

}  // namespace

TEST(md5_as_rfc_1321_gives_it) {
    CHECK(hex(md5("")) == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(hex(md5("abc")) == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(hex(md5("message digest")) == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(hex(md5(std::string(1000, 'a'))) == "cabe45dcc9ae5b66ba86600cca6b8ba8");  // past one block
}

TEST(the_five_games_and_their_collections) {
    CHECK(collection_games().size() == 5);
    const auto* kh1 = collection_game("kh1");
    const auto* ddd = collection_game("ddd");
    CHECK(kh1 != nullptr && std::string(kh1->collection) == "1.5+2.5" && kh1->packages.size() == 3);
    CHECK(ddd != nullptr && std::string(ddd->collection) == "2.8");
    CHECK(collection_game("recom") != nullptr && collection_game("kh2") != nullptr &&
          collection_game("bbs") != nullptr);
    CHECK(std::string(collection_game("bbs")->collection) == "1.5+2.5" && collection_game("bbs")->packages.size() == 4);
    CHECK(collection_game("bbsfm") == nullptr);
    // their units in Days's: KH2's centimetres, DDD's metres
    CHECK(collection_game("kh2")->units == 0.01f && ddd->units == 1.0f);
    // a DDD map with no collision among its files is a stage fallen through (Dive)
    const CollectionMap dive{"eh", "eh_60", "kh3d_third", {"map/eh/eh_60/bin/eh_60.pmp", "map/eh/eh_60/bin/eh_60.pvd"}};
    const CollectionMap walked{
        "eh", "eh_01", "kh3d_third", {"map/eh/eh_01/bin/eh_01.pmp", "map/eh/eh_01/bin/eh_01a.bcd"}};
    CHECK(collection_map_falling(*ddd, dive) && !collection_map_falling(*ddd, walked));
}

TEST(a_packages_maps_found_by_each_games_pattern) {
    // KH1: <w>00_<nn>.bin with its .img; a .bin alone; another file that is no map
    const auto kh1 = package_keys(index_of({"tw00_02.bin", "tw00_02.img", "tw.wdt", "al00_09.bin", "dc01.ard"}));
    const auto kh1_maps = maps_in_package(*collection_game("kh1"), "kh1_third", kh1);
    CHECK(names(kh1_maps) == std::vector<std::string>({"al/al00_09", "tw/tw00_02"}));
    CHECK(kh1_maps[1].paths == std::vector<std::string>({"tw00_02.bin", "tw00_02.img", "tw.wdt"}));
    CHECK(kh1_maps[0].paths == std::vector<std::string>({"al00_09.bin"}));  // only what the package holds
    // Re:CoM: MAP<mm>/<nnnn>/MP<mm><nnnn>.AZF and its siblings
    const auto recom = package_keys(index_of(
        {"MAP01/0001/MP010001.AZF", "MAP01/0001/MP010001.mcd", "MAP30/2004/MP302004.AZF", "MAP01/0002/MP010002.VTM"}));
    const auto recom_maps = maps_in_package(*collection_game("recom"), "Recom", recom);
    CHECK(names(recom_maps) == std::vector<std::string>({"MAP01/MP010001", "MAP30/MP302004"}));
    CHECK(recom_maps[0].paths == std::vector<std::string>({"MAP01/0001/MP010001.AZF", "MAP01/0001/MP010001.mcd"}));
    // DDD: map/<w>/<w>_<nn>/bin/<w>_<nn>.pmp with its collision and cameras
    const auto ddd = package_keys(index_of({"map/eh/eh_01/bin/eh_01.pmp", "map/eh/eh_01/bin/eh_01a.bcd",
                                            "map/eh/eh_01/bin/eh_01_02.mcv", "map/tw/tw_06/bin/tw_06.pmp"}));
    const auto ddd_maps = maps_in_package(*collection_game("ddd"), "kh3d_third", ddd);
    CHECK(names(ddd_maps) == std::vector<std::string>({"eh/eh_01", "tw/tw_06"}));
    CHECK(ddd_maps[0].paths == std::vector<std::string>({"map/eh/eh_01/bin/eh_01.pmp", "map/eh/eh_01/bin/eh_01a.bcd",
                                                         "map/eh/eh_01/bin/eh_01_02.mcv"}));
    // KH2: map/<w><nn>.map
    const auto kh2 = package_keys(index_of({"map/tt02.map", "map/eh14.map"}));
    CHECK(names(maps_in_package(*collection_game("kh2"), "kh2_first", kh2)) ==
          std::vector<std::string>({"eh/eh14", "tt/tt02"}));
    // Birth by Sleep: arc/map/<w><nn>.arc, the room's files in one
    const auto bbs = package_keys(index_of({"arc/map/jb20.arc", "arc/map/di03.arc", "arc/boss/b60ex.arc"}));
    const auto bbs_maps = maps_in_package(*collection_game("bbs"), "bbs_third", bbs);
    CHECK(names(bbs_maps) == std::vector<std::string>({"di/di03", "jb/jb20"}));
    CHECK(bbs_maps[1].paths == std::vector<std::string>({"arc/map/jb20.arc"}));
}

// The Xbox app's game folder, as a player chooses it, is taken as its Content, where the collection's Image is.
TEST(the_xbox_apps_game_folder_is_taken_as_its_content) {
    namespace fs = std::filesystem;
    const fs::path game = fs::temp_directory_path() / "khdays_collection_root_test";
    std::error_code ec;
    fs::remove_all(game, ec);
    fs::create_directories(game / "Content" / "Image", ec);
    CHECK(collection_root(game) == game / "Content");
    CHECK(collection_root(game / "Content") == game / "Content");
    fs::create_directories(game / "Image", ec);  // its own Image: as given
    CHECK(collection_root(game) == game);
    CHECK(collection_root(game / "nothing") == game / "nothing");
    fs::remove_all(game, ec);
}

// Where the maps are kept: the setting `collections` against the port's folder, saves/collection when it is empty.
TEST(the_collections_folder_is_the_setting_else_saves_collection) {
    namespace fs = std::filesystem;
    Settings s;
    s.root = fs::path("port");
    s.collections.clear();
    CHECK(collections_folder(s) == fs::path("port") / "saves" / "collection");
    s.collections = "elsewhere/maps";
    const CollectionGameInfo& ddd = *collection_game("ddd");
    CHECK(collection_folder(s, ddd) == fs::path("port") / "elsewhere" / "maps" / "ddd");
    CHECK(collection_made_model(s, ddd, "tm_01") ==
          fs::path("port") / "elsewhere" / "maps" / "ddd" / "made" / "tm_01" / "tm_01.gltf");
    CHECK(space_text(std::uintmax_t{3} << 29) == "1.5 GB" && space_text(std::uintmax_t{5} << 20) == "5 MB");
}

// A map's model this build of the studio made is used as it is, what its making did read back from its record; one
// made before the studio was written is made again.
TEST(a_map_made_by_this_studio_is_used_as_it_is) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "khdays_collection_ready_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    Settings s;
    s.root = dir;
    s.collections = (dir / "maps").string();
    const CollectionGameInfo& ddd = *collection_game("ddd");
    CHECK(!collection_room_ready(s, ddd, "xx_01"));
    const fs::path model = collection_made_model(s, ddd, "xx_01");
    fs::create_directories(model.parent_path(), ec);
    std::ofstream(model) << "{}";
    std::ofstream(model.parent_path() / "xx_01.made.txt")
        << "triangles = 12\npictures = 3\npieces = 0\nanimations = 0\ncollision = 1\nreport = one thing left out\n";
    const char* base = SDL_GetBasePath();
    const fs::path studio = fs::path(base != nullptr ? base : "") / "khdays-studio.exe";
    if (!fs::is_regular_file(studio, ec)) {
        CHECK(!collection_room_ready(s, ddd, "xx_01"));  // no studio to compare with: made again
    } else {
        CHECK(collection_room_ready(s, ddd, "xx_01"));
        CollectionMap map;
        map.name = "xx_01";
        CollectionMade made;
        std::string why;
        // no copy of the game: only the record can give it
        CHECK(collection_room(s, ddd, map, &made, &why));
        CHECK(made.triangles == 12 && made.pictures == 3 && made.collision && made.report.size() == 1 &&
              made.report[0] == "one thing left out");
        // made before the studio was written: not ready
        fs::last_write_time(model.parent_path() / "xx_01.made.txt", fs::last_write_time(studio) - std::chrono::hours(1),
                            ec);
        CHECK(!collection_room_ready(s, ddd, "xx_01"));
    }
    fs::remove_all(dir, ec);
}

// Without the installer's reader in the port's folder no map can be taken out: said once for the game, not for each.
TEST(no_reader_is_said_once_for_the_game) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "khdays_collection_reader_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const CollectionGameInfo& ddd = *collection_game("ddd");
    fs::create_directories(dir / "copy", ec);
    std::ofstream(dir / "copy" / path_u8(ddd.executable)) << "";
    Settings s;
    s.root = dir / "port";
    s.collections = (dir / "maps").string();
    s.collection_28 = (dir / "copy").string();
    CollectionPlan plan;
    plan.game = &ddd;
    for (const char* name : {"xx_01", "xx_02", "xx_03"}) {
        CollectionMap m;
        m.world = "xx";
        m.name = name;
        m.package = "kh3d_third";
        plan.maps.push_back(m);
    }
    std::size_t steps = 0;
    std::vector<std::string> failed;
    std::string why;
    CHECK(!take_out_every_map(
        s, plan, [&](const std::string&, std::size_t, std::size_t) { return ++steps > 0; }, &failed, &why));
    CHECK(failed.empty() && steps == 0 && why.find("reader") != std::string::npos);
    fs::remove_all(dir, ec);
}
