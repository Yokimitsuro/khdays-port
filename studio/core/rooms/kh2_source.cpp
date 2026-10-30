#include "core/rooms/kh2_source.h"

#include "core/files/paths.h"
#include "core/mods/process.h"

#include "kh2_rooms_data.h"

#include "khdays/assets/gltf_export.h"
#include "khdays/assets/kh2_map.h"
#include "khdays/assets/kh2_room.h"

extern "C" {
#include "render/png_io.h"
}

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace studio {

namespace {

constexpr const char* kKh2Exe = "KINGDOM HEARTS II FINAL MIX.exe";

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

// The folders the install's record names as its "1.5+2.5" input, in its order.
std::vector<fs::path> recorded_collections(const fs::path& root) {
    std::ifstream in(root / "setup" / "state.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<fs::path> out;
    const std::string key = "\"1.5+2.5\"";
    for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + key.size())) {
        auto quote = text.find_first_not_of(" \t\r\n:", at + key.size());
        if (quote == std::string::npos || text[quote] != '"')
            continue;
        const std::string folder = json_string_at(text, quote);
        if (!folder.empty())
            out.push_back(path_u8(folder));
    }
    return out;
}

// The installer's reader of the collections' archives run with `args` after its script: the install's
// own Python (setup/python), else the system's (the repository's tree). Its exit status (-1: it did
// not start) and its output.
int run_reader(const fs::path& root, const std::vector<std::string>& args, std::vector<std::string>* lines,
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

// A map taken out into `folder` (kh2_folder()): its data, and its remastered pictures' folder.
fs::path original_map(const fs::path& folder, const std::string& map) {
    return folder / "original" / "map" / path_u8(map + ".map");
}
fs::path remastered_map(const fs::path& folder, const std::string& map) {
    return folder / "remastered" / "map" / path_u8(map + ".map");
}

// The map's own floors and walls written as `out`; false with the reason.
bool write_collision(const std::vector<std::uint8_t>& data, const std::string& map, const fs::path& out,
                     std::string* why) {
    try {
        const auto scene = khdays::assets::kh2::collision_scene(data.data(), data.size(), map);
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        std::ofstream file(out, std::ios::binary);
        file << khdays::assets::scene_to_gltf(scene);
        if (!file) {
            *why = "cannot write " + u8(out);
            return false;
        }
    } catch (const std::exception& e) {
        *why = map + ": " + e.what();
        return false;
    }
    return true;
}

}  // namespace

fs::path kh2_folder(const fs::path& root) {
    return root / "saves" / "collection" / "kh2";
}

fs::path kh2_made_model(const fs::path& root, const std::string& map) {
    return kh2_folder(root) / "made" / path_u8(map) / path_u8(map + ".gltf");
}

fs::path kh2_collection(const Settings& settings) {
    std::error_code ec;
    if (!settings.collection_1525.empty()) {
        const fs::path set = resolve(settings, settings.collection_1525);
        return fs::is_directory(set, ec) ? set : fs::path();
    }
    for (const auto& folder : recorded_collections(settings.root)) {
        if (fs::is_regular_file(folder / kKh2Exe, ec))
            return folder;
    }
    return {};
}

namespace {

// The collection's archives that hold KH2's maps, in the order they are asked: kh2_first, kh2_fifth,
// kh2_sixth, in its language folder (Image/dt: Steam's, Image/en: the Epic Games Store's).
std::vector<fs::path> kh2_archives(const fs::path& collection) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const char* archive : {"kh2_first", "kh2_fifth", "kh2_sixth"}) {
        for (const char* language : {"dt", "en"}) {
            const fs::path base = collection / "Image" / language / archive;
            if (fs::is_regular_file(fs::path(base).concat(".hed"), ec)) {
                out.push_back(base);
                break;
            }
        }
    }
    return out;
}

// The player's collection and its KH2 archives; false with the reason when either is missing.
bool collection_archives(const Settings& settings, fs::path* collection, std::vector<fs::path>* archives,
                         std::string* why) {
    *collection = kh2_collection(settings);
    if (collection->empty()) {
        *why = "no KINGDOM HEARTS HD 1.5+2.5 ReMIX folder: set it in the settings";
        return false;
    }
    *archives = kh2_archives(*collection);
    if (archives->empty()) {
        *why = "no KH2 archive in " + u8(*collection / "Image");
        return false;
    }
    return true;
}

}  // namespace

bool kh2_world_maps(const Settings& settings, const std::string& world, std::vector<std::string>* maps,
                    std::string* why) {
    fs::path collection;
    std::vector<fs::path> archives;
    if (!collection_archives(settings, &collection, &archives, why))
        return false;
    maps->clear();
    for (const auto& archive : archives) {
        std::vector<std::string> args{"--has", u8(archive)};
        for (int n = 0; n < 100; ++n) {
            char name[32];
            std::snprintf(name, sizeof(name), "map/%s%02d.map", world.c_str(), n);
            args.emplace_back(name);
        }
        std::vector<std::string> lines;
        if (run_reader(settings.root, args, &lines, why) != 0) {
            if (why->empty())
                *why = lines.empty() ? "the collection's reader failed" : lines.back();
            return false;
        }
        for (const auto& line : lines) {
            // map/<name>.map
            if (line.rfind("map/", 0) == 0 && line.size() > 8)
                maps->push_back(line.substr(4, line.size() - 8));
        }
    }
    std::sort(maps->begin(), maps->end());
    return true;
}

bool kh2_map_taken_out(const fs::path& root, const std::string& map) {
    std::error_code ec;
    return fs::is_regular_file(original_map(kh2_folder(root), map), ec) &&
           fs::is_directory(remastered_map(kh2_folder(root), map), ec);
}

bool take_out_kh2_map(const Settings& settings, const std::string& map, std::string* why) {
    if (kh2_map_taken_out(settings.root, map))
        return true;
    fs::path collection;
    std::vector<fs::path> archives;
    if (!collection_archives(settings, &collection, &archives, why))
        return false;
    std::error_code ec;
    if (!fs::is_regular_file(collection / kKh2Exe, ec)) {
        *why = std::string("no ") + kKh2Exe + " in " + u8(collection) + " (its archives are read with its table)";
        return false;
    }
    for (const auto& archive : archives) {
        std::vector<std::string> lines;
        const int status = run_reader(
            settings.root,
            {u8(archive), u8(kh2_folder(settings.root)), u8(collection / kKh2Exe), "map/" + map + ".map"}, &lines, why);
        if (status == 0 && kh2_map_taken_out(settings.root, map))
            return true;
        if (status != 3) {  // 3: not in this archive, the next one asked
            if (why->empty())
                *why = lines.empty() ? "the collection's reader failed" : lines.back();
            return false;
        }
    }
    *why = "no map " + map + " in this copy's KH2 archives";
    return false;
}

bool take_out_entries(const Settings& settings, const std::string& archive, const std::vector<std::string>& entries,
                      const fs::path& out, std::string* why) {
    const fs::path collection = kh2_collection(settings);
    if (collection.empty()) {
        *why = "no KINGDOM HEARTS HD 1.5+2.5 ReMIX folder: set it in the settings";
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(collection / kKh2Exe, ec)) {
        *why = std::string("no ") + kKh2Exe + " in " + u8(collection) + " (its archives are read with its table)";
        return false;
    }
    fs::path base;
    for (const char* language : {"dt", "en"}) {
        const fs::path at = collection / "Image" / language / path_u8(archive);
        if (fs::is_regular_file(fs::path(at).concat(".hed"), ec)) {
            base = at;
            break;
        }
    }
    if (base.empty()) {
        *why = "no archive " + archive + " in " + u8(collection / "Image");
        return false;
    }
    std::vector<std::string> args{u8(base), u8(out), u8(collection / kKh2Exe)};
    args.insert(args.end(), entries.begin(), entries.end());
    std::vector<std::string> lines;
    const int status = run_reader(settings.root, args, &lines, why);
    if (status == 0 || status == 3)  // 3: none of them in this archive
        return true;
    if (why->empty())
        *why = lines.empty() ? "the collection's reader failed" : lines.back();
    return false;
}

bool make_kh2_collision(const fs::path& root, const std::string& map, fs::path* out, std::string* why) {
    const auto data = read_all(original_map(kh2_folder(root), map));
    if (data.empty()) {
        *why = "no " + map + " taken out of the collection";
        return false;
    }
    *out = kh2_made_model(root, map).parent_path() / path_u8(map + "_collision.gltf");
    return write_collision(data, map, *out, why);
}

void read_kh2_pairs(const std::string& text, const std::string& source, std::vector<Kh2Pair>* pairs,
                    std::vector<std::string>* notes) {
    std::istringstream lines(text);
    std::string line;
    int number = 0;
    while (std::getline(lines, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto hash = line.find('#');
        std::string body = line.substr(0, hash);
        std::string evidence;
        const auto semicolon = body.find(';');
        if (semicolon != std::string::npos) {
            evidence = body.substr(semicolon + 1);
            body.erase(semicolon);
            const auto first = evidence.find_first_not_of(" \t");
            evidence = first == std::string::npos ? std::string() : evidence.substr(first);
        }
        std::istringstream words(body);
        Kh2Pair p;
        std::string room, rest;
        if (!(words >> p.world)) {
            continue;  // an empty line, a comment
        }
        const auto bad = [&] {
            if (notes != nullptr)
                notes->push_back(source + " line " + std::to_string(number) +
                                 ": not <world> <room> <map> [turn <degrees>] [move <x> <y> <z>] ; <evidence>");
        };
        if (!(words >> room >> p.map) || p.world.size() != 2) {
            bad();
            continue;
        }
        // where Days put the map, when given
        bool fine = true;
        while (fine && (words >> rest)) {
            if (rest == "turn")
                fine = static_cast<bool>(words >> p.turn_y);
            else if (rest == "move")
                fine = static_cast<bool>(words >> p.move[0] >> p.move[1] >> p.move[2]);
            else
                fine = false;
            p.placed = true;
        }
        if (!fine) {
            bad();
            continue;
        }
        char* end = nullptr;
        const long n = std::strtol(room.c_str(), &end, 10);
        if (end == room.c_str() || *end != '\0' || n < 0) {
            bad();
            continue;
        }
        p.room = static_cast<int>(n);
        p.evidence = evidence;
        bool replaced = false;
        for (auto& q : *pairs) {
            if (q.world == p.world && q.room == p.room) {
                q = p;
                replaced = true;
            }
        }
        if (!replaced)
            pairs->push_back(std::move(p));
    }
}

std::vector<Kh2Pair> kh2_pairs(const fs::path& mod_dir, std::vector<std::string>* notes) {
    std::vector<Kh2Pair> pairs;
    read_kh2_pairs(kKh2RoomsText, "the studio's kh2_rooms.txt", &pairs, notes);
    if (!mod_dir.empty()) {
        const auto data = read_all(mod_dir / "studio" / "kh2_rooms.txt");
        if (!data.empty())
            read_kh2_pairs(std::string(data.begin(), data.end()), u8(mod_dir / "studio" / "kh2_rooms.txt"), &pairs,
                           notes);
    }
    return pairs;
}

const Kh2Pair* kh2_pair_of(const std::vector<Kh2Pair>& pairs, const std::string& world, int room) {
    for (const auto& p : pairs) {
        if (p.world == world && p.room == room)
            return &p;
    }
    return nullptr;
}

bool kh2_radar(const fs::path& root, const std::string& map, khdays::assets::DecodedTexture* out) {
    const auto data = read_all(original_map(kh2_folder(root), map));
    if (data.empty())
        return false;
    try {
        const auto m = khdays::assets::kh2::read_map(data.data(), data.size());
        if (m.radar.empty())
            return false;
        auto pictures = khdays::assets::kh2::read_tim2(data.data(), data.size(), m.entries[m.radar.front()].offset);
        if (pictures.empty())
            return false;
        *out = std::move(pictures.front());
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool make_kh2_room(const fs::path& folder, const std::string& map, const fs::path& out, Kh2Made* made,
                   std::string* why) {
    const fs::path file = original_map(folder, map);
    const fs::path hd = remastered_map(folder, map);
    const std::vector<std::uint8_t> data = read_all(file);
    if (data.empty()) {
        *why = "no " + u8(file) + " (the map is taken out of the collection first)";
        return false;
    }
    std::error_code ec;
    if (!fs::is_directory(hd, ec))
        made->report.push_back("no remastered pictures in " + u8(hd) + ": surfaces in their vertex colours");
    fs::create_directories(out.parent_path(), ec);
    std::string failed;
    khdays::assets::kh2::Room room;
    try {
        room = khdays::assets::kh2::room_model(
            data.data(), data.size(), hd, map,
            [&](const std::string& name, const khdays::assets::DecodedTexture& picture) {
                const char* e = khdays_png_write(u8(out.parent_path() / path_u8(name)).c_str(), picture.rgba.data(),
                                                 picture.width, picture.height);
                if (e != nullptr && failed.empty())
                    failed = name + ": " + e;
                ++made->pictures;
            });
    } catch (const std::exception& e) {
        *why = u8(file) + ": " + e.what();
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
    for (const auto& mesh : room.scene.meshes) {
        for (const auto& p : mesh.primitives)
            made->triangles += p.indices.size() / 3;
        made->pieces += mesh.joints.empty() ? 0 : 1;
    }
    made->animations = room.scene.animations.size();
    for (const auto& m : room.scene.materials) {
        made->scrolling += m.scroll[0] != 0.0F || m.scroll[1] != 0.0F ? 1 : 0;
        made->framed += m.frames.empty() ? 0 : 1;
    }
    made->report.insert(made->report.end(), room.report.begin(), room.report.end());
    // and the map's own floors and walls beside it (<map>_collision.gltf), for a room change to take
    std::string no_walls;
    if (!write_collision(data, map, out.parent_path() / path_u8(map + "_collision.gltf"), &no_walls))
        made->report.push_back("no floors and walls of its own: " + no_walls);
    return true;
}

}  // namespace studio
