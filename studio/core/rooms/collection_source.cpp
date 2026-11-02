#include "core/rooms/collection_source.h"

#include "core/files/md5.h"
#include "core/files/paths.h"
#include "core/mods/process.h"
#include "core/rooms/kh2_source.h"

#include "khdays/assets/ddd_map.h"
#include "khdays/assets/gltf_export.h"

extern "C" {
#include "render/png_io.h"
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
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

const std::vector<CollectionGameInfo>& collection_games() {
    static const std::vector<CollectionGameInfo> games = {
        {CollectionGame::kh1, "kh1", "KINGDOM HEARTS FINAL MIX", "1.5+2.5", "KINGDOM HEARTS FINAL MIX.exe",
         {"kh1_first", "kh1_second", "kh1_third"}, 0.0F},
        {CollectionGame::kh2, "kh2", "KINGDOM HEARTS II FINAL MIX", "1.5+2.5", "KINGDOM HEARTS II FINAL MIX.exe",
         {"kh2_first", "kh2_fifth", "kh2_sixth"}, 0.01F},
        {CollectionGame::recom, "recom", "KINGDOM HEARTS Re:Chain of Memories", "1.5+2.5",
         "KINGDOM HEARTS Re_Chain of Memories.exe", {"Recom"}, 0.0F},
        {CollectionGame::ddd, "ddd", "KINGDOM HEARTS Dream Drop Distance", "2.8", "KINGDOM HEARTS Dream Drop Distance.exe",
         {"kh3d_first", "kh3d_third"}, 1.0F},
    };
    return games;
}

const CollectionGameInfo* collection_game(const std::string& code) {
    for (const auto& g : collection_games())
        if (code == g.code)
            return &g;
    return nullptr;
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
                    } else {  // map/<w>/<w>_<nn>/bin/<w>_<nn>.pmp and its siblings
                        name = w + "_" + number;
                        const std::string dir = "map/" + w + "/" + name + "/bin/";
                        paths = {dir + name + ".pmp", dir + name + ".pvd", dir + name + ".mss", dir + name + "a.bcd",
                                 dir + name + ".bcd", dir + name + ".txa"};
                        for (int k = 0; k <= 99; ++k)
                            paths.push_back(dir + name + printed("_%02d.mcv", k));
                    }
                    if (package_holds(keys, paths.front()))
                        add(w, name, paths);
                }
            }
    }
    std::sort(out.begin(), out.end(),
              [](const CollectionMap& x, const CollectionMap& y) { return std::tie(x.world, x.name) < std::tie(y.world, y.name); });
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

fs::path collection_copy(const Settings& settings, const CollectionGameInfo& game) {
    std::error_code ec;
    const std::string& set =
        std::string(game.collection) == "2.8" ? settings.collection_28 : settings.collection_1525;
    if (!set.empty()) {
        const fs::path folder = resolve(settings, set);
        return fs::is_directory(folder, ec) ? folder : fs::path();
    }
    for (const auto& folder : recorded_collections(settings.root, game.collection))
        if (fs::is_regular_file(folder / path_u8(game.executable), ec))
            return folder;
    return {};
}

fs::path collection_folder(const fs::path& root, const CollectionGameInfo& game) {
    return root / "saves" / "collection" / path_u8(game.code);
}

bool collection_maps(const Settings& settings, const CollectionGameInfo& game, std::vector<CollectionMap>* maps,
                     std::string* why) {
    const fs::path copy = collection_copy(settings, game);
    if (copy.empty()) {
        *why = std::string("no ") + (std::string(game.collection) == "2.8" ? "KINGDOM HEARTS HD 2.8 Final Chapter Prologue"
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

bool collection_map_taken_out(const fs::path& root, const CollectionGameInfo& game, const CollectionMap& map) {
    std::error_code ec;
    return !map.paths.empty() &&
           fs::is_regular_file(collection_folder(root, game) / "original" / path_u8(map.paths.front()), ec);
}

bool take_out_collection_map(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                             std::string* why, const std::vector<std::string>& extra) {
    if (extra.empty() && collection_map_taken_out(settings.root, game, map))
        return true;
    const fs::path copy = collection_copy(settings, game);
    std::error_code ec;
    if (copy.empty() || !fs::is_regular_file(copy / path_u8(game.executable), ec)) {
        *why = std::string("no ") + game.executable + " in the collection's folder (its packages are read with its table)";
        return false;
    }
    const fs::path base = package_base(copy, map.package);
    if (base.empty()) {
        *why = "no package " + map.package + " in " + u8(copy / "Image");
        return false;
    }
    std::vector<std::string> args{u8(base), u8(collection_folder(settings.root, game)),
                                  u8(copy / path_u8(game.executable))};
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

bool collection_rooms_read(const CollectionGameInfo& game) {
    return game.game == CollectionGame::kh2 || game.game == CollectionGame::ddd;
}

bool collection_map_falling(const CollectionGameInfo& game, const CollectionMap& map) {
    if (game.game != CollectionGame::ddd)
        return false;
    for (const auto& p : map.paths)
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".bcd") == 0)
            return false;
    return true;
}

fs::path collection_made_model(const fs::path& root, const CollectionGameInfo& game, const std::string& map) {
    return collection_folder(root, game) / "made" / path_u8(map) / path_u8(map + ".gltf");
}

fs::path collection_made_collision(const fs::path& root, const CollectionGameInfo& game, const std::string& map) {
    return collection_folder(root, game) / "made" / path_u8(map) / path_u8(map + "_collision.gltf");
}

namespace {

// A Dream Drop Distance room: its textures taken out by the names its PMP lists (kh3d_third's rooms keep each as a
// .ctt beside the room, its HD picture its asset; kh3d_first's keep them as the .pmp's own assets), then made.
bool make_ddd_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                   const fs::path& out, CollectionMade* made, std::string* why) {
    const fs::path folder = collection_folder(settings.root, game);
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
    khdays::assets::ddd::Room room;
    try {
        room = khdays::assets::ddd::room_model(
            data.data(), data.size(), map.name, hd_of,
            [&](const std::string& file, const khdays::assets::DecodedTexture& picture) {
                const char* e = khdays_png_write(u8(out.parent_path() / path_u8(file)).c_str(), picture.rgba.data(),
                                                 picture.width, picture.height);
                if (e != nullptr && failed.empty())
                    failed = file + ": " + e;
                ++made->pictures;
            });
    } catch (const std::exception& e) {
        *why = pmp_path + ": " + e.what();
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
    // its floors and walls: <map>a.bcd (kh3d_third's rooms) or <map>.bcd
    for (const char* suffix : {"a.bcd", ".bcd"}) {
        const auto bcd = read_all(folder / "original" / path_u8(dir + map.name + suffix));
        if (bcd.empty())
            continue;
        try {
            const auto scene = khdays::assets::ddd::collision_scene(bcd.data(), bcd.size(), map.name);
            std::ofstream file(collection_made_collision(settings.root, game, map.name), std::ios::binary);
            file << khdays::assets::scene_to_gltf(scene);
            made->collision = static_cast<bool>(file);
            if (!made->collision)
                made->report.push_back("its collision cannot be written");
        } catch (const std::exception& e) {
            made->report.push_back(map.name + suffix + ": " + e.what() + "; its collision made from the model instead");
        }
        break;
    }
    if (!made->collision)
        made->report.push_back(collection_map_falling(game, map)
                                   ? "a stage the player falls through (Dive): no floor to walk on"
                                   : "no collision of its own: made from the model when it replaces a room");
    return true;
}

}  // namespace

bool make_collection_room(const Settings& settings, const CollectionGameInfo& game, const CollectionMap& map,
                          CollectionMade* made, std::string* why) {
    if (game.game == CollectionGame::kh2) {
        if (!take_out_kh2_map(settings, map.name, why))
            return false;
        Kh2Made kh2;
        if (!make_kh2_room(kh2_folder(settings.root), map.name, kh2_made_model(settings.root, map.name), &kh2, why))
            return false;
        made->triangles = kh2.triangles;
        made->pictures = kh2.pictures;
        made->report = kh2.report;
        fs::path collision;
        std::string no_collision;
        made->collision = make_kh2_collision(settings.root, map.name, &collision, &no_collision);
        if (!made->collision)
            made->report.push_back(no_collision);
        return true;
    }
    if (!take_out_collection_map(settings, game, map, why))
        return false;
    const fs::path out = collection_made_model(settings.root, game, map.name);
    switch (game.game) {
    case CollectionGame::ddd: return make_ddd_room(settings, game, map, out, made, why);
    default: *why = std::string(game.name) + "'s rooms are not read yet"; return false;
    }
}

int run_collection_reader(const fs::path& root, const std::vector<std::string>& args, std::vector<std::string>* lines,
                          std::string* why) {
    const fs::path own = root / "setup" / "python" / "python.exe";
    const fs::path script = root / "installer" / "collection_archive.py";
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
