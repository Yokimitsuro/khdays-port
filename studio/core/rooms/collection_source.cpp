#include "core/rooms/collection_source.h"

#include "core/files/md5.h"
#include "core/files/paths.h"
#include "core/mods/process.h"
#include "core/rooms/kh2_source.h"

#include "khdays/assets/ddd_map.h"
#include "khdays/assets/dds.h"
#include "khdays/assets/kh1_map.h"
#include "khdays/assets/kh2_map.h"
#include "khdays/assets/recom_map.h"
#include "khdays/assets/gltf_export.h"

extern "C" {
#include "render/png_io.h"
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <tuple>

namespace fs = std::filesystem;

namespace studio {

namespace {

std::vector<std::uint8_t> read_all(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The JSON string starting at text[at] (its opening quote), its escapes read.
std::string json_string_at(const std::string& text, std::size_t at) {
    std::string out;
    for (std::size_t i = at + 1; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            switch (text[i]) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'u': i += 4; break;  // not in a folder's name the installer writes
            default: out += text[i]; break;
            }
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string key_of(const std::string& path) {
    const auto digest = md5(path);
    return std::string(digest.begin(), digest.end());
}

std::string printed(const char* format, int a, int b = 0, int c = 0, int d = 0) {
    char text[96];
    std::snprintf(text, sizeof(text), format, a, b, c, d);
    return text;
}

// A package's base path (without .hed/.pkg) in the copy: Image/dt (Steam's), else Image/en (the Epic Games Store's).
fs::path package_base(const fs::path& copy, const std::string& package) {
    std::error_code ec;
    for (const char* language : {"dt", "en"}) {
        const fs::path base = copy / "Image" / language / path_u8(package);
        if (fs::is_regular_file(fs::path(base).concat(".hed"), ec))
            return base;
    }
    return {};
}

}  // namespace

// The space every map of a game took out of Steam's copy and made (2026-11-11, --collection-take-all): KINGDOM HEARTS
// FINAL MIX's 130 (4 not made: the copy holds no HD model of Atlantica's lm00_14 to 17), KH2's 278 (tr02 and tr07 not
// read), Re:CoM's 627, Dream Drop Distance's 160, Birth by Sleep's 202.
const std::vector<CollectionGameInfo>& collection_games() {
    static const std::vector<CollectionGameInfo> games = {
        {CollectionGame::kh1,
         "kh1",
         "KINGDOM HEARTS FINAL MIX",
         "1.5+2.5",
         "KINGDOM HEARTS FINAL MIX.exe",
         {"kh1_first", "kh1_second", "kh1_third"},
         0.01F,
         7328898367,
         130},
        {CollectionGame::kh2,
         "kh2",
         "KINGDOM HEARTS II FINAL MIX",
         "1.5+2.5",
         "KINGDOM HEARTS II FINAL MIX.exe",
         {"kh2_first", "kh2_fifth", "kh2_sixth"},
         0.01F,
         19485876278,
         278},
        {CollectionGame::recom,
         "recom",
         "KINGDOM HEARTS Re:Chain of Memories",
         "1.5+2.5",
         "KINGDOM HEARTS Re_Chain of Memories.exe",
         {"Recom"},
         1.0F,
         4297158446,
         627},
        {CollectionGame::ddd,
         "ddd",
         "KINGDOM HEARTS Dream Drop Distance",
         "2.8",
         "KINGDOM HEARTS Dream Drop Distance.exe",
         {"kh3d_first", "kh3d_third"},
         1.0F,
         20289182091,
         160},
        {CollectionGame::bbs,
         "bbs",
         "KINGDOM HEARTS Birth by Sleep FINAL MIX",
         "1.5+2.5",
         "KINGDOM HEARTS Birth by Sleep FINAL MIX.exe",
         {"bbs_first", "bbs_second", "bbs_third", "bbs_fourth"},
         1.0F,
         4779309745,
         202},
    };
    return games;
}

const CollectionGameInfo* collection_game(const std::string& code) {
    for (const auto& g : collection_games())
        if (code == g.code)
            return &g;
    return nullptr;
}

std::string collection_world_name(CollectionGame game, const std::string& world) {
    struct Named {
        CollectionGame game;
        const char* code;
        const char* name;
    };
    using G = CollectionGame;
    static const Named names[] = {
        {G::kh1, "al", "Agrabah"},
        {G::kh1, "aw", "Wonderland"},
        {G::kh1, "dc", "Disney Castle"},
        {G::kh1, "dh", "Dive to the Heart"},
        {G::kh1, "di", "Destiny Islands"},
        {G::kh1, "ew", "End of the World"},
        {G::kh1, "he", "Olympus Coliseum"},
        {G::kh1, "lm", "Atlantica"},
        {G::kh1, "nm", "Halloween Town"},
        {G::kh1, "pc", "Hollow Bastion"},
        {G::kh1, "pi", "Monstro"},
        {G::kh1, "po", "100 Acre Wood"},
        {G::kh1, "pp", "Neverland"},
        {G::kh1, "tw", "Traverse Town"},
        {G::kh1, "tz", "Deep Jungle"},
        {G::recom, "MAP00", "Castle Oblivion Hallways & Castle Oblivion Boss Arenas"},
        {G::recom, "MAP01", "Traverse Town"},
        {G::recom, "MAP02", "Agrabah"},
        {G::recom, "MAP03", "Olympus Coliseum"},
        {G::recom, "MAP04", "Wonderland"},
        {G::recom, "MAP05", "Monstro"},
        {G::recom, "MAP06", "Halloween Town"},
        {G::recom, "MAP07", "Atlantica"},
        {G::recom, "MAP08", "Neverland"},
        {G::recom, "MAP09", "Hollow Bastion"},
        {G::recom, "MAP10", "100 Acre Wood"},
        {G::recom, "MAP11", "Twilight Town"},
        {G::recom, "MAP12", "Destiny Islands"},
        {G::recom, "MAP13", "Castle Oblivion"},
        {G::recom, "MAP20", "Battle Arenas & Boss Arenas"},
        {G::recom, "MAP21", "Test Rooms"},
        {G::recom, "MAP22", "Test Rooms"},
        {G::recom, "MAP30", "Test Rooms"},
        {G::kh2, "al", "Agrabah"},
        {G::kh2, "bb", "Beast's Castle"},
        {G::kh2, "ca", "Port Royal"},
        {G::kh2, "dc", "Disney Castle"},
        {G::kh2, "di", "Destiny Islands"},
        {G::kh2, "eh", "The World That Never Was"},
        {G::kh2, "es", "Realm of Darkness"},
        {G::kh2, "hb", "Radiant Garden"},
        {G::kh2, "he", "Olympus Coliseum"},
        {G::kh2, "lk", "The Pride Lands"},
        {G::kh2, "lm", "Atlantica"},
        {G::kh2, "mu", "The Land of Dragons"},
        {G::kh2, "nm", "Halloween Town"},
        {G::kh2, "po", "100 Acre Wood"},
        {G::kh2, "tr", "Space Paranoids"},
        {G::kh2, "tt", "Twilight Town"},
        {G::kh2, "wi", "Timeless River"},
        {G::kh2, "wm", "World Map"},
        {G::bbs, "bd", "Command Board"},
        {G::bbs, "cd", "Castle of Dreams"},
        {G::bbs, "dc", "Disney Town"},
        {G::bbs, "di", "Destiny Islands"},
        {G::bbs, "dp", "Land of Departure"},
        {G::bbs, "he", "Olympus Coliseum"},
        {G::bbs, "jb", "Realm of Darkness"},
        {G::bbs, "kg", "Keyblade Graveyard"},
        {G::bbs, "ls", "Deep Space"},
        {G::bbs, "pp", "Neverland"},
        {G::bbs, "rg", "Radiant Garden"},
        {G::bbs, "sb", "Enchanted Dominion"},
        {G::bbs, "sw", "Dwarf Woodlands"},
        {G::bbs, "vs", "Mirage Arena"},
        {G::bbs, "wm", "World Map"},
        {G::bbs, "yt", "Mysterious Tower"},
        {G::ddd, "de", "Flick Rush"},
        {G::ddd, "di", "Destiny Islands"},
        {G::ddd, "eh", "The World That Never Was"},
        {G::ddd, "fa", "Symphony of Sorcery"},
        {G::ddd, "nd", "La Cité des Cloches"},
        {G::ddd, "pi", "Prankster's Paradise"},
        {G::ddd, "rg", "Radiant Garden"},
        {G::ddd, "tl", "The Grid"},
        {G::ddd, "tm", "Country of the Musketeers"},
        {G::ddd, "tw", "Traverse Town"},
        {G::ddd, "wm", "World Map"},
        {G::ddd, "yt", "Mysterious Tower"},
    };
    for (const auto& n : names)
        if (n.game == game && world == n.code)
            return n.name;
    return {};
}

std::string collection_world_label(CollectionGame game, const std::string& world) {
    const std::string name = collection_world_name(game, world);
    return name.empty() ? world : name + " (" + world + ")";
}

std::unordered_set<std::string> package_keys(const std::vector<std::uint8_t>& hed) {
    std::unordered_set<std::string> keys;
    for (std::size_t at = 0; at + 32 <= hed.size(); at += 32)
        keys.emplace(reinterpret_cast<const char*>(hed.data() + at), 16);
    return keys;
}

bool package_holds(const std::unordered_set<std::string>& keys, const std::string& path) {
    return keys.count(key_of(path)) != 0;
}

std::vector<CollectionMap> maps_in_package(const CollectionGameInfo& game, const std::string& package,
                                           const std::unordered_set<std::string>& keys) {
    std::vector<CollectionMap> out;
    auto add = [&](const std::string& world, const std::string& name, const std::vector<std::string>& paths) {
        CollectionMap m{world, name, package, {}};
        for (const auto& p : paths)
            if (package_holds(keys, p))
                m.paths.push_back(p);
        out.push_back(std::move(m));
    };
    if (game.game == CollectionGame::recom) {
        // MAP<mm>/<nnnn>/MP<mm><nnnn>.AZF and its siblings
        for (int world = 0; world <= 99; ++world)
            for (int n = 0; n <= 9999; ++n) {
                const std::string base = printed("MAP%02d/%04d/MP%02d%04d", world, n, world, n);
                if (!package_holds(keys, base + ".AZF"))
                    continue;
                std::vector<std::string> paths;
                for (const char* ext : {".AZF", ".VTM", ".RTM", ".mcd", ".ALF", ".AFF"})
                    paths.push_back(base + ext);
                add(printed("MAP%02d", world), printed("MP%02d%04d", world, n), paths);
            }
    } else {
        for (char a = 'a'; a <= 'z'; ++a)
            for (char b = 'a'; b <= 'z'; ++b) {
                const std::string w{a, b};
                for (int n = 0; n <= 99; ++n) {
                    const std::string number = printed("%02d", n);
                    std::vector<std::string> paths;
                    std::string name;
                    if (game.game == CollectionGame::kh1) {  // <w>00_<nn>.bin, its textures .img
                        name = w + "00_" + number;
                        paths = {name + ".bin", name + ".img", w + ".wdt"};
                    } else if (game.game == CollectionGame::kh2) {  // map/<w><nn>.map
                        name = w + number;
                        paths = {"map/" + name + ".map"};
                    } else if (game.game == CollectionGame::bbs) {  // arc/map/<w><nn>.arc: the room's files in one
                        name = w + number;
                        paths = {"arc/map/" + name + ".arc"};
                    } else {  // map/<w>/<w>_<nn>/bin/<w>_<nn>.pmp and its siblings
                        name = w + "_" + number;
                        const std::string dir = "map/" + w + "/" + name + "/bin/";
                        paths = {dir + name + ".pmp",  dir + name + ".pvd", dir + name + ".mss",
                                 dir + name + "a.bcd", dir + name + ".bcd", dir + name + ".txa"};
                        for (int k = 0; k <= 99; ++k)
                            paths.push_back(dir + name + printed("_%02d.mcv", k));
                    }
                    if (package_holds(keys, paths.front()))
                        add(w, name, paths);
                }
            }
    }
    std::sort(out.begin(), out.end(), [](const CollectionMap& x, const CollectionMap& y) {
        return std::tie(x.world, x.name) < std::tie(y.world, y.name);
    });
    return out;
}

std::vector<fs::path> recorded_collections(const fs::path& root, const std::string& key) {
    std::ifstream in(root / "setup" / "state.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<fs::path> out;
    const std::string quoted = "\"" + key + "\"";
    for (auto at = text.find(quoted); at != std::string::npos; at = text.find(quoted, at + quoted.size())) {
        auto quote = text.find_first_not_of(" \t\r\n:", at + quoted.size());
        if (quote == std::string::npos || text[quote] != '"')
            continue;
        const std::string folder = json_string_at(text, quote);
        if (!folder.empty())
            out.push_back(path_u8(folder));
    }
    return out;
}

fs::path collection_root(const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_directory(folder / "Image", ec) && fs::is_directory(folder / "Content" / "Image", ec))
        return folder / "Content";
    return folder;
}

fs::path collection_copy(const Settings& settings, const CollectionGameInfo& game) {
    std::error_code ec;
    const std::string& set = std::string(game.collection) == "2.8" ? settings.collection_28 : settings.collection_1525;
    if (!set.empty()) {
        const fs::path folder = collection_root(resolve(settings, set));
        return fs::is_directory(folder, ec) ? folder : fs::path();
    }
    for (const auto& folder : recorded_collections(settings.root, game.collection))
        if (fs::is_regular_file(folder / path_u8(game.executable), ec))
            return folder;
    return {};
}

fs::path collections_folder(const Settings& settings) {
    return resolve(settings, settings.collections.empty() ? std::string("saves/collection") : settings.collections);
}

fs::path collection_folder(const Settings& settings, const CollectionGameInfo& game) {
    return collections_folder(settings) / path_u8(game.code);
}

bool collection_maps(const Settings& settings, const CollectionGameInfo& game, std::vector<CollectionMap>* maps,
                     std::string* why) {
    const fs::path copy = collection_copy(settings, game);
    if (copy.empty()) {
        *why = std::string("no ") +
               (std::string(game.collection) == "2.8" ? "KINGDOM HEARTS HD 2.8 Final Chapter Prologue"
                                                      : "KINGDOM HEARTS HD 1.5+2.5 ReMIX") +
               " folder with " + game.executable + ": set it in the settings";
        return false;
    }
    maps->clear();
    bool any = false;
    for (const char* package : game.packages) {
        const fs::path base = package_base(copy, package);
        if (base.empty())
            continue;
        any = true;
        const auto found = maps_in_package(game, package, package_keys(read_all(fs::path(base).concat(".hed"))));
        maps->insert(maps->end(), found.begin(), found.end());
    }
    if (!any) {
        *why = std::string("no package of ") + game.name + " in " + u8(copy / "Image");
        return false;
    }
    std::sort(maps->begin(), maps->end(), [](const CollectionMap& x, const CollectionMap& y) {
        return std::tie(x.world, x.name) < std::tie(y.world, y.name);
    });
    return true;
}

bool collection_map_taken_out(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map) {
    std::error_code ec;
    return !map.paths.empty() &&
           fs::is_regular_file(collection_folder(settings, game) / "original" / path_u8(map.paths.front()), ec);
}

bool take_out_collection_map(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                             std::string* why, const std::vector<std::string>& extra) {
    if (extra.empty() && collection_map_taken_out(settings, game, map))
        return true;
    const fs::path copy = collection_copy(settings, game);
    std::error_code ec;
    if (copy.empty() || !fs::is_regular_file(copy / path_u8(game.executable), ec)) {
        *why =
            std::string("no ") + game.executable + " in the collection's folder (its packages are read with its table)";
        return false;
    }
    const fs::path base = package_base(copy, map.package);
    if (base.empty()) {
        *why = "no package " + map.package + " in " + u8(copy / "Image");
        return false;
    }
    std::vector<std::string> args{u8(base), u8(collection_folder(settings, game)), u8(copy / path_u8(game.executable))};
    args.insert(args.end(), map.paths.begin(), map.paths.end());
    args.insert(args.end(), extra.begin(), extra.end());
    std::vector<std::string> lines;
    const int status = run_collection_reader(settings.root, args, &lines, why);
    if (status == 0 || (status == 3 && !extra.empty()))
        return true;
    if (why->empty())
        *why = lines.empty() ? "the collection's reader failed" : lines.back();
    return false;
}

bool collection_map_falling(const CollectionGameInfo& game, const CollectionMap& map) {
    if (game.game != CollectionGame::ddd)
        return false;
    for (const auto& p : map.paths)
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".bcd") == 0)
            return false;
    return true;
}

fs::path collection_made_model(const Settings& settings, const CollectionGameInfo& game, const std::string& map) {
    return collection_folder(settings, game) / "made" / path_u8(map) / path_u8(map + ".gltf");
}

fs::path collection_made_collision(const Settings& settings, const CollectionGameInfo& game, const std::string& map) {
    return collection_folder(settings, game) / "made" / path_u8(map) / path_u8(map + "_collision.gltf");
}

namespace {

// A room read from a PMP written as collection_made_model(), its triangles counted and its report kept; its floors and
// walls from `bcd` (its `bcd_name`; none: made from the model when it replaces a room) as collection_made_collision().
bool write_pmp_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                    const fs::path& out, khdays::assets::ddd::Room room, const std::uint8_t* bcd, std::size_t bcd_size,
                    const std::string& bcd_name, CollectionMade* made, std::string* why) {
    std::ofstream gltf(out, std::ios::binary);
    gltf << khdays::assets::scene_to_gltf(room.scene);
    if (!gltf) {
        *why = "cannot write " + u8(out);
        return false;
    }
    for (const auto& mesh : room.scene.meshes)
        for (const auto& p : mesh.primitives)
            made->triangles += p.indices.size() / 3;
    made->report = std::move(room.report);
    if (bcd_size > 0) {
        try {
            const auto scene = khdays::assets::ddd::collision_scene(bcd, bcd_size, map.name);
            std::ofstream file(collection_made_collision(settings, game, map.name), std::ios::binary);
            file << khdays::assets::scene_to_gltf(scene);
            made->collision = static_cast<bool>(file);
            if (!made->collision)
                made->report.push_back("its collision cannot be written");
        } catch (const std::exception& e) {
            made->report.push_back(bcd_name + ": " + e.what() + "; its collision made from the model instead");
        }
    }
    if (!made->collision)
        made->report.push_back(collection_map_falling(game, map)
                                   ? "a stage the player falls through (Dive): no floor to walk on"
                                   : "no collision of its own: made from the model when it replaces a room");
    return true;
}

// A Dream Drop Distance room: its textures taken out by the names its PMP lists (kh3d_third's rooms keep each as a
// .ctt beside the room, its HD picture its asset; kh3d_first's keep them as the .pmp's own assets), then made; those
// its .txa animates shown frame by frame.
bool make_ddd_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                   const fs::path& out, CollectionMade* made, std::string* why) {
    const fs::path folder = collection_folder(settings, game);
    const std::string pmp_path = map.paths.front();
    const auto data = read_all(folder / "original" / path_u8(pmp_path));
    if (data.empty()) {
        *why = "no " + pmp_path + " taken out of the collection";
        return false;
    }
    const std::string dir = pmp_path.substr(0, pmp_path.rfind('/') + 1);
    std::vector<std::string> names;
    try {
        names = khdays::assets::ddd::texture_names(data.data(), data.size());
    } catch (const std::exception& e) {
        *why = pmp_path + ": " + e.what();
        return false;
    }
    auto hd_of = [&](const std::string& name) {
        std::error_code ec;
        const fs::path own = folder / "remastered" / path_u8(pmp_path) / path_u8(name + ".dds");
        if (fs::is_regular_file(own, ec))
            return own;
        const fs::path beside = folder / "remastered" / path_u8(dir + name + ".ctt") / path_u8(name + ".dds");
        return fs::is_regular_file(beside, ec) ? beside : fs::path();
    };
    std::vector<std::string> missing;
    for (const auto& n : names)
        if (hd_of(n).empty())
            missing.push_back(dir + n + ".ctt");
    if (!missing.empty() && !take_out_collection_map(settings, game, map, why, missing))
        return false;
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    std::string failed;
    const auto write_picture = [&](const std::string& file, const khdays::assets::DecodedTexture& picture) {
        const char* e = khdays_png_write(u8(out.parent_path() / path_u8(file)).c_str(), picture.rgba.data(),
                                         picture.width, picture.height);
        if (e != nullptr && failed.empty())
            failed = file + ": " + e;
        ++made->pictures;
    };
    khdays::assets::ddd::Room room;
    try {
        room = khdays::assets::ddd::room_model(data.data(), data.size(), map.name, hd_of, write_picture);
    } catch (const std::exception& e) {
        *why = pmp_path + ": " + e.what();
        return false;
    }
    // its textures shown frame by frame: <map>.txa, its sheets its own HD pictures
    const std::string txa_path = dir + map.name + ".txa";
    const auto txa = read_all(folder / "original" / path_u8(txa_path));
    if (!txa.empty()) {
        const fs::path sheets_folder = folder / "remastered" / path_u8(txa_path);
        std::vector<std::string> sheets;
        for (const auto& e : fs::directory_iterator(sheets_folder, ec))
            if (e.path().extension() == ".dds")
                sheets.push_back(u8(e.path().stem()));
        std::sort(sheets.begin(), sheets.end());
        try {
            khdays::assets::ddd::animate_textures(
                &room, khdays::assets::ddd::texture_animations(txa.data(), txa.size()), sheets,
                [&](const std::string& name) { return sheets_folder / path_u8(name + ".dds"); }, write_picture);
        } catch (const std::exception& e) {
            room.report.push_back(txa_path + ": " + e.what() + "; its textures shown still");
        }
    }
    if (!failed.empty()) {
        *why = "a picture cannot be written: " + failed;
        return false;
    }
    // its floors and walls: <map>a.bcd (kh3d_third's rooms) or <map>.bcd
    std::vector<std::uint8_t> bcd;
    std::string bcd_name;
    for (const char* suffix : {"a.bcd", ".bcd"}) {
        bcd = read_all(folder / "original" / path_u8(dir + map.name + suffix));
        bcd_name = map.name + suffix;
        if (!bcd.empty())
            break;
    }
    return write_pmp_room(settings, game, map, out, std::move(room), bcd.data(), bcd.size(), bcd_name, made, why);
}

// A Birth by Sleep room: its .arc (arc/map/<w><nn>.arc) holds the room's PMP and its floors and walls (<w>_<nn>a.bcd),
// read as Dream Drop Distance's (the same engine's; its models are version 3); its textures the remaster's HD pictures,
// the archive's own assets (<...>_arc<n>.dds: the PMP's n-th texture).
bool make_bbs_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                   const fs::path& out, CollectionMade* made, std::string* why) {
    const fs::path folder = collection_folder(settings, game);
    std::string arc_path = map.paths.front();
    auto arc = read_all(folder / "original" / path_u8(arc_path));
    if (arc.empty()) {
        *why = "no " + arc_path + " taken out of the collection";
        return false;
    }
    std::vector<khdays::assets::ddd::ArcMember> members;
    std::vector<std::string> names;
    const khdays::assets::ddd::ArcMember* pmp = nullptr;
    const khdays::assets::ddd::ArcMember* bcd = nullptr;
    try {
        members = khdays::assets::ddd::arc_members(arc.data(), arc.size());
        // a room another archive holds (sb20 to sb37 use sb05's, vs15 vs03's): that map's taken out and read
        for (const auto& m : members) {
            if (m.kind != khdays::assets::ddd::kArcMapLink || m.size != 0 || members.size() > 4 ||
                !std::none_of(members.begin(), members.end(), [](const auto& o) {
                    return o.name.size() > 4 && o.name.compare(o.name.size() - 4, 4, ".pmp") == 0;
                }))
                continue;
            CollectionMap linked = map;
            linked.name = m.name;
            linked.paths = {"arc/map/" + m.name + ".arc"};
            bool taken = false;
            for (const char* package : game.packages) {
                linked.package = package;
                std::string not_here;
                taken = taken || take_out_collection_map(settings, game, linked, &not_here);
            }
            arc_path = linked.paths.front();
            arc = read_all(folder / "original" / path_u8(arc_path));
            if (!taken || arc.empty()) {
                *why = map.name + " uses " + m.name + "'s room, not found in the copy";
                return false;
            }
            made->report.push_back("the room of " + m.name + " (" + map.name + " uses it)");
            members = khdays::assets::ddd::arc_members(arc.data(), arc.size());
            break;
        }
        const auto ends = [](const std::string& s, const std::string& end) {
            return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
        };
        for (const auto& m : members) {
            if (ends(m.name, ".pmp"))
                pmp = &m;
            if (ends(m.name, "a.bcd") || (bcd == nullptr && ends(m.name, ".bcd")))
                bcd = &m;
        }
        if (pmp == nullptr) {
            *why = arc_path + ": no room model (.pmp) in it";
            return false;
        }
        names = khdays::assets::ddd::texture_names(arc.data() + pmp->offset, pmp->size);
    } catch (const std::exception& e) {
        *why = arc_path + ": " + e.what();
        return false;
    }
    // the HD pictures: <...>_arc<n>.dds, the PMP's n-th texture's
    std::map<std::size_t, fs::path> pictures;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder / "remastered" / path_u8(arc_path), ec)) {
        const std::string stem = u8(e.path().stem());
        const auto at = stem.rfind("_arc");
        if (e.path().extension() == ".dds" && at != std::string::npos)
            pictures[std::strtoul(stem.c_str() + at + 4, nullptr, 10)] = e.path();
    }
    const auto hd_of = [&](const std::string& name) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] != name)
                continue;
            const auto found = pictures.find(i);
            return found != pictures.end() ? found->second : fs::path();
        }
        return fs::path();
    };
    fs::create_directories(out.parent_path(), ec);
    std::string failed;
    const auto write_picture = [&](const std::string& file, const khdays::assets::DecodedTexture& picture) {
        const char* e = khdays_png_write(u8(out.parent_path() / path_u8(file)).c_str(), picture.rgba.data(),
                                         picture.width, picture.height);
        if (e != nullptr && failed.empty())
            failed = file + ": " + e;
        ++made->pictures;
    };
    khdays::assets::ddd::Room room;
    try {
        room = khdays::assets::ddd::room_model(arc.data() + pmp->offset, pmp->size, map.name, hd_of, write_picture);
    } catch (const std::exception& e) {
        *why = arc_path + ": " + e.what();
        return false;
    }
    if (!failed.empty()) {
        *why = "a picture cannot be written: " + failed;
        return false;
    }
    auto said = std::move(made->report);
    const bool ok =
        write_pmp_room(settings, game, map, out, std::move(room), bcd != nullptr ? arc.data() + bcd->offset : nullptr,
                       bcd != nullptr ? bcd->size : 0, bcd != nullptr ? bcd->name : std::string(), made, why);
    made->report.insert(made->report.begin(), said.begin(), said.end());
    return ok;
}

// A KINGDOM HEARTS FINAL MIX room: the remaster's model of it (the .bin entry's .mvbl asset), its textures the
// .img's HD pictures, its skies the .bin's .gvbl assets.
bool make_kh1_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                   const fs::path& out, CollectionMade* made, std::string* why) {
    const fs::path folder = collection_folder(settings, game);
    const std::string bin = map.name + ".bin", img = map.name + ".img";
    const auto img_data = read_all(folder / "original" / path_u8(img));
    if (img_data.empty()) {
        *why = "no " + img + " taken out of the collection";
        return false;
    }
    // the room's model: the .bin's one .mvbl asset
    fs::path mvbl;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder / "remastered" / path_u8(bin), ec)) {
        if (e.path().extension() == ".mvbl")
            mvbl = e.path();
    }
    const auto data = mvbl.empty() ? std::vector<std::uint8_t>() : read_all(mvbl);
    if (data.empty()) {
        *why = "no HD model (.mvbl) of " + bin + " taken out of the collection";
        return false;
    }
    fs::create_directories(out.parent_path(), ec);
    std::string failed;
    const auto write_picture = [&](const std::string& file, const khdays::assets::DecodedTexture& picture) {
        const char* e = khdays_png_write(u8(out.parent_path() / path_u8(file)).c_str(), picture.rgba.data(),
                                         picture.width, picture.height);
        if (e != nullptr && failed.empty())
            failed = file + ": " + e;
        ++made->pictures;
    };
    // a picture among the .img's or the .bin's remastered assets, by its name
    const auto asset = [&](const std::string& file) {
        return [&folder, file](const std::string& name) {
            std::error_code exists;
            const fs::path hd = folder / "remastered" / path_u8(file) / path_u8("-" + name + ".dds");
            return fs::is_regular_file(hd, exists) ? hd : fs::path();
        };
    };
    khdays::assets::kh1::Room room;
    try {
        room = khdays::assets::kh1::room_model(data.data(), data.size(), map.name,
                                               khdays::assets::kh1::img_pages(img_data.data(), img_data.size()),
                                               asset(img), write_picture);
    } catch (const std::exception& e) {
        *why = u8(mvbl.filename()) + ": " + e.what();
        return false;
    }
    // its skies: the .bin's .gvbl assets (named by their offsets), in the .bin's order
    std::map<unsigned long, fs::path> skies;
    for (const auto& e : fs::directory_iterator(folder / "remastered" / path_u8(bin), ec)) {
        const std::string stem = u8(e.path().stem());
        if (e.path().extension() == ".gvbl" && stem.size() > 1)
            skies.emplace(std::strtoul(stem.c_str() + 1, nullptr, 16), e.path());
    }
    for (const auto& [offset, path] : skies) {
        const auto sky = read_all(path);
        try {
            khdays::assets::kh1::add_sky(&room, sky.data(), sky.size(), asset(bin), write_picture);
        } catch (const std::exception& e) {
            room.report.push_back(u8(path.filename()) + ": " + e.what() + "; that sky left out");
        }
    }
    if (!failed.empty()) {
        *why = "a picture cannot be written: " + failed;
        return false;
    }
    std::ofstream gltf(out, std::ios::binary);
    gltf << khdays::assets::scene_to_gltf(room.scene);
    if (!gltf) {
        *why = "cannot write " + u8(out);
        return false;
    }
    for (const auto& mesh : room.scene.meshes)
        for (const auto& p : mesh.primitives)
            made->triangles += p.indices.size() / 3;
    made->report = std::move(room.report);
    // its floors and walls: the .bin's collision entry
    const auto bin_data = read_all(folder / "original" / path_u8(bin));
    try {
        const auto scene = khdays::assets::kh1::collision_scene(bin_data.data(), bin_data.size(), map.name);
        std::ofstream file(collection_made_collision(settings, game, map.name), std::ios::binary);
        file << khdays::assets::scene_to_gltf(scene);
        made->collision = static_cast<bool>(file);
        if (!made->collision)
            made->report.push_back("its collision cannot be written");
    } catch (const std::exception& e) {
        made->report.push_back(bin + ": " + e.what() + "; its collision made from the model instead");
    }
    return true;
}

// A Re:Chain of Memories room: its pieces placed by its layout (.AZF), its textures the .VTM's and .RTM's TIM2
// pictures, the .RTM's HD ones where the copy has them (<name>_tm2.dds, its remastered assets).
bool make_recom_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                     const fs::path& out, CollectionMade* made, std::string* why) {
    const fs::path folder = collection_folder(settings, game);
    const std::string base = map.paths.front().substr(0, map.paths.front().size() - 4);  // MAP01/0001/MP010001
    const auto azf = read_all(folder / "original" / path_u8(base + ".AZF"));
    if (azf.empty()) {
        *why = "no " + base + ".AZF taken out of the collection";
        return false;
    }
    const auto vtm = read_all(folder / "original" / path_u8(base + ".VTM"));
    const auto rtm = read_all(folder / "original" / path_u8(base + ".RTM"));
    std::vector<khdays::assets::recom::PictureEntry> vtm_list, rtm_list;
    try {
        if (!vtm.empty())
            vtm_list = khdays::assets::recom::picture_list(vtm.data(), vtm.size());
        if (!rtm.empty())
            rtm_list = khdays::assets::recom::picture_list(rtm.data(), rtm.size());
    } catch (const std::exception& e) {
        *why = base + ": its pictures' list: " + e.what();
        return false;
    }
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    std::vector<std::string> notes;
    // a texture's picture: its HD one, else its TIM2 in the .VTM or the .RTM
    auto picture = [&](const std::string& name) -> std::optional<khdays::assets::DecodedTexture> {
        std::error_code exists;
        const fs::path hd = folder / "remastered" / path_u8(base + ".RTM") / path_u8(name + "_tm2.dds");
        if (fs::is_regular_file(hd, exists)) {
            try {
                return khdays::assets::read_dds(hd);
            } catch (const std::exception& e) {
                notes.push_back(name + ": its HD picture: " + e.what());
            }
        }
        for (const auto* list : {&vtm_list, &rtm_list}) {
            const auto& data = list == &vtm_list ? vtm : rtm;
            for (const auto& entry : *list) {
                if (entry.name != name)
                    continue;
                try {
                    auto pictures = khdays::assets::kh2::read_tim2(data.data(), data.size(), entry.offset);
                    if (!pictures.empty())
                        return std::move(pictures.front());
                } catch (const std::exception& e) {
                    notes.push_back(name + ": " + e.what());
                }
            }
        }
        return std::nullopt;
    };
    std::string failed;
    khdays::assets::recom::Room room;
    try {
        room = khdays::assets::recom::room_model(azf.data(), azf.size(), map.name, picture,
                                                 [&](const std::string& file, const khdays::assets::DecodedTexture& p) {
                                                     const char* e =
                                                         khdays_png_write(u8(out.parent_path() / path_u8(file)).c_str(),
                                                                          p.rgba.data(), p.width, p.height);
                                                     if (e != nullptr && failed.empty())
                                                         failed = file + ": " + e;
                                                     ++made->pictures;
                                                 });
    } catch (const std::exception& e) {
        *why = base + ".AZF: " + e.what();
        return false;
    }
    if (!failed.empty()) {
        *why = "a picture cannot be written: " + failed;
        return false;
    }
    std::ofstream gltf(out, std::ios::binary);
    gltf << khdays::assets::scene_to_gltf(room.scene);
    if (!gltf) {
        *why = "cannot write " + u8(out);
        return false;
    }
    for (const auto& mesh : room.scene.meshes)
        for (const auto& p : mesh.primitives)
            made->triangles += p.indices.size() / 3;
    made->report = std::move(room.report);
    made->report.insert(made->report.end(), notes.begin(), notes.end());
    // its floors and walls: the .mcd
    const auto mcd = read_all(folder / "original" / path_u8(base + ".mcd"));
    try {
        if (mcd.empty())
            throw std::runtime_error("not taken out");
        const auto scene = khdays::assets::recom::collision_scene(mcd.data(), mcd.size(), map.name);
        std::ofstream file(collection_made_collision(settings, game, map.name), std::ios::binary);
        file << khdays::assets::scene_to_gltf(scene);
        made->collision = static_cast<bool>(file);
        if (!made->collision)
            made->report.push_back("its collision cannot be written");
    } catch (const std::exception& e) {
        made->report.push_back(base + ".mcd: " + e.what() + "; its collision made from the model instead");
    }
    return true;
}

}  // namespace

namespace {

bool make_room_now(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                   CollectionMade* made, std::string* why) {
    if (game.game == CollectionGame::kh2) {
        if (!take_out_kh2_map(settings, map.name, why))
            return false;
        Kh2Made kh2;
        if (!make_kh2_room(kh2_folder(settings), map.name, kh2_made_model(settings, map.name), &kh2, why))
            return false;
        made->triangles = kh2.triangles;
        made->pictures = kh2.pictures;
        made->pieces = kh2.pieces;
        made->animations = kh2.animations;
        made->report = kh2.report;
        fs::path collision;
        std::string no_collision;
        made->collision = make_kh2_collision(settings, map.name, &collision, &no_collision);
        if (!made->collision)
            made->report.push_back(no_collision);
        return true;
    }
    if (!take_out_collection_map(settings, game, map, why))
        return false;
    const fs::path out = collection_made_model(settings, game, map.name);
    switch (game.game) {
    case CollectionGame::ddd: return make_ddd_room(settings, game, map, out, made, why);
    case CollectionGame::kh1: return make_kh1_room(settings, game, map, out, made, why);
    case CollectionGame::recom: return make_recom_room(settings, game, map, out, made, why);
    case CollectionGame::bbs: return make_bbs_room(settings, game, map, out, made, why);
    case CollectionGame::kh2: break;  // (made above)
    }
    *why = std::string(game.name) + ": no reader of its rooms";
    return false;
}

// What making a map's model did, kept beside it.
fs::path made_record(const Settings& settings, const CollectionGameInfo& game, const std::string& map) {
    return collection_made_model(settings, game, map).parent_path() / path_u8(map + ".made.txt");
}

bool write_made_record(const fs::path& file, const CollectionMade& made) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "# what khdays-studio made of this map\n"
        << "triangles = " << made.triangles << "\n"
        << "pictures = " << made.pictures << "\n"
        << "pieces = " << made.pieces << "\n"
        << "animations = " << made.animations << "\n"
        << "collision = " << (made.collision ? 1 : 0) << "\n";
    for (const auto& line : made.report) {
        std::string one = line;
        std::replace(one.begin(), one.end(), '\n', ' ');
        out << "report = " << one << "\n";
    }
    return static_cast<bool>(out);
}

bool read_made_record(const fs::path& file, CollectionMade* made) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    *made = {};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find(" = ");
        if (line.empty() || line[0] == '#' || eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 3);
        const auto count = [&] { return static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10)); };
        if (key == "triangles")
            made->triangles = count();
        else if (key == "pictures")
            made->pictures = count();
        else if (key == "pieces")
            made->pieces = count();
        else if (key == "animations")
            made->animations = count();
        else if (key == "collision")
            made->collision = value == "1";
        else if (key == "report")
            made->report.push_back(value);
    }
    return true;
}

// When the studio's executable was written: a model made before it is made again (none known: every model is).
fs::file_time_type studio_written() {
    static const fs::file_time_type at = [] {
        const char* base = SDL_GetBasePath();
#ifdef _WIN32
        const char* exe = "khdays-studio.exe";
#else
        const char* exe = "khdays-studio";
#endif
        std::error_code ec;
        const fs::file_time_type written =
            base != nullptr ? fs::last_write_time(path_u8(base) / exe, ec) : fs::file_time_type::max();
        return base == nullptr || ec ? fs::file_time_type::max() : written;
    }();
    return at;
}

// The bytes the files under `folder` take.
std::uintmax_t folder_bytes(const fs::path& folder) {
    std::uintmax_t bytes = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code size_ec;
        if (it->is_regular_file(size_ec)) {
            const auto size = it->file_size(size_ec);
            bytes += size_ec ? 0 : size;
        }
    }
    return bytes;
}

// How many maps a run of the reader takes out: few enough for a stop asked to be heard soon, and for the command line.
constexpr std::size_t kMapsPerRun = 8;

}  // namespace

bool make_collection_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                          CollectionMade* made, std::string* why) {
    // the record of an earlier making gone first: a model left half made is never taken as ready
    std::error_code ec;
    const fs::path record = made_record(settings, game, map.name);
    fs::remove(record, ec);
    if (!make_room_now(settings, game, map, made, why))
        return false;
    write_made_record(record, *made);  // (none written: the map made again when next used)
    return true;
}

bool collection_room_ready(const Settings& settings, const CollectionGameInfo& game, const std::string& map) {
    std::error_code ec, model_ec;
    const fs::file_time_type record = fs::last_write_time(made_record(settings, game, map), ec);
    const fs::file_time_type model = fs::last_write_time(collection_made_model(settings, game, map), model_ec);
    const fs::file_time_type studio = studio_written();
    return !ec && !model_ec && studio != fs::file_time_type::max() && record >= studio && model >= studio;
}

bool collection_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                     CollectionMade* made, std::string* why) {
    if (collection_room_ready(settings, game, map.name) &&
        read_made_record(made_record(settings, game, map.name), made))
        return true;
    return make_collection_room(settings, game, map, made, why);
}

std::string space_text(std::uintmax_t bytes) {
    char text[32];
    if (bytes >= (std::uintmax_t{1} << 30))
        std::snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / (1 << 30));
    else
        std::snprintf(text, sizeof(text), "%.0f MB", static_cast<double>(bytes) / (1 << 20));
    return text;
}

CollectionPlan collection_plan(const Settings& settings, const CollectionGameInfo& game) {
    CollectionPlan plan;
    plan.game = &game;
    if (!collection_maps(settings, game, &plan.maps, &plan.why))
        plan.maps.clear();
    for (const auto& m : plan.maps)
        plan.ready += collection_room_ready(settings, game, m.name) ? 1 : 0;
    plan.used = folder_bytes(collection_folder(settings, game));
    const std::uintmax_t all = game.all_maps > 0 ? game.all_bytes / game.all_maps * plan.maps.size() : 0;
    plan.more = all > plan.used ? all - plan.used : 0;
    return plan;
}

namespace {

// The installer's reader of a collection's packages, in the port's folder.
fs::path collection_reader(const fs::path& root) {
    return root / "installer" / "collection_archive.py";
}

}  // namespace

bool take_out_every_map(const Settings& settings, const CollectionPlan& plan, const CollectionStep& step,
                        std::vector<std::string>* failed, std::string* why) {
    const CollectionGameInfo& game = *plan.game;
    std::vector<const CollectionMap*> todo;
    for (const auto& m : plan.maps)
        if (!collection_room_ready(settings, game, m.name))
            todo.push_back(&m);
    if (todo.empty())
        return true;
    const fs::path copy = collection_copy(settings, game);
    std::error_code ec;
    if (copy.empty() || !fs::is_regular_file(copy / path_u8(game.executable), ec)) {
        *why =
            std::string("no ") + game.executable + " in the collection's folder (its packages are read with its table)";
        return false;
    }
    // without the reader, or Python, no map can be: said once, not for each
    if (!fs::is_regular_file(collection_reader(settings.root), ec)) {
        *why = "the installer's reader of the collection is not in " + u8(settings.root / "installer");
        return false;
    }
    // each package's maps not taken out yet, a few a run of the reader (one that fails leaves its maps to be taken out
    // one by one as they are made, and named there when that fails too)
    std::map<std::string, std::vector<const CollectionMap*>> by_package;
    for (const CollectionMap* m : todo)
        if (!collection_map_taken_out(settings, game, *m))
            by_package[m->package].push_back(m);
    std::size_t runs = 0;
    for (const auto& [package, maps] : by_package)
        runs += (maps.size() + kMapsPerRun - 1) / kMapsPerRun;
    const std::size_t total = runs + todo.size();
    std::size_t done = 0;
    for (const auto& [package, maps] : by_package) {
        const fs::path base = package_base(copy, package);
        for (std::size_t first = 0; first < maps.size(); first += kMapsPerRun, ++done) {
            const std::size_t last = std::min(maps.size(), first + kMapsPerRun);
            if (!step(package + ": " + maps[first]->name + (last - first > 1 ? " - " + maps[last - 1]->name : ""), done,
                      total)) {
                *why = "stopped";
                return false;
            }
            if (base.empty())
                continue;
            std::vector<std::string> args{u8(base), u8(collection_folder(settings, game)),
                                          u8(copy / path_u8(game.executable))};
            for (std::size_t k = first; k < last; ++k)
                args.insert(args.end(), maps[k]->paths.begin(), maps[k]->paths.end());
            std::vector<std::string> lines;
            std::string not_read;
            if (run_collection_reader(settings.root, args, &lines, &not_read) < 0) {
                *why = not_read;
                return false;
            }
        }
    }
    // each made, what its model names taken out then
    for (const CollectionMap* m : todo) {
        if (!step(m->name, done, total)) {
            *why = "stopped";
            return false;
        }
        CollectionMade made;
        std::string not_made;
        if (!make_collection_room(settings, game, *m, &made, &not_made))
            failed->push_back(m->name + ": " + not_made);
        ++done;
    }
    step({}, total, total);
    return true;
}

int run_collection_reader(const fs::path& root, const std::vector<std::string>& args, std::vector<std::string>* lines,
                          std::string* why) {
    const fs::path own = root / "setup" / "python" / "python.exe";
    const fs::path script = collection_reader(root);
    std::error_code ec;
    if (!fs::is_regular_file(script, ec)) {
        *why = "the installer's reader of the collection is not in " + u8(root / "installer");
        return -1;
    }
    std::vector<std::string> command{fs::is_regular_file(own, ec) ? u8(own) : std::string("python"), "-I", u8(script)};
    command.insert(command.end(), args.begin(), args.end());
    Job job(command, root);
    while (!job.done())
        SDL_Delay(10);
    *lines = job.lines();
    if (job.exit_code() < 0)
        *why = "Python could not be started (" + job.error() + ")";
    return job.exit_code();
}

}  // namespace studio
