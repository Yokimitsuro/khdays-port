// The collections' rooms (spec 040): the games' table, MD5 as the packages key their paths, and the maps a package's
// index holds found by each game's pattern. Synthetic only: an index made here of the paths' keys.
#include "test.h"

#include "core/files/md5.h"
#include "core/rooms/collection_source.h"

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
    const CollectionMap walked{"eh", "eh_01", "kh3d_third", {"map/eh/eh_01/bin/eh_01.pmp", "map/eh/eh_01/bin/eh_01a.bcd"}};
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
    const auto recom = package_keys(index_of({"MAP01/0001/MP010001.AZF", "MAP01/0001/MP010001.mcd",
                                              "MAP30/2004/MP302004.AZF", "MAP01/0002/MP010002.VTM"}));
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
