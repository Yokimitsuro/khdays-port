#include "cli.h"

#include "core/models/anim_replace.h"
#include "core/files/catalog.h"
#include "core/models/game_model.h"
#include "core/missions/command_meanings.h"
#include "core/missions/command_names.h"
#include "core/missions/missions.h"
#include "core/missions/mission_db.h"
#include "core/missions/mod_missions.h"
#include "core/mods/mods.h"
#include "core/mods/plugin_parameters.h"
#include "core/files/paths.h"
#include "core/mods/settings.h"
#include "core/models/clip_import.h"
#include "core/models/texture_export.h"
#include "core/models/model_export.h"
#include "core/models/model_files.h"
#include "core/rooms/kh2_source.h"
#include "core/rooms/room_change.h"
#include "core/rooms/room_collision.h"
#include "core/rooms/room_export.h"
#include "core/sheets/sheets.h"
#include "core/texts/texts.h"
#include "core/rooms/worlds.h"

#include "khdays/assets/kh2_map.h"
#include "khdays/vfs/filesystem.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <unordered_map>

namespace studio {

namespace {

const char* kind_name(Kind k) {
    switch (k) {
    case Kind::texture: return "texture";
    case Kind::sheet: return "sheet";
    case Kind::model: return "model";
    case Kind::sound: return "sound";
    case Kind::text_file: return "text";
    case Kind::movie: return "movie";
    case Kind::room: return "room";
    case Kind::mission: return "mission";
    }
    return "?";
}

Catalog load_catalog(const fs::path& root, const Settings& settings) {
    CatalogSources sources;
    sources.graphics_dump = resolve(settings, settings.graphics_dump);
    sources.sounds_dump = resolve(settings, settings.sounds_dump);
    if (khdays::vfs::autodetect_data_root(root / "data" / "extracted")) {
        sources.nitrofs = khdays::vfs::data_root() / "nitrofs";
    }
    return build_catalog(sources, nullptr, nullptr);
}

// --list <Mod>: every file of the mod, the item it replaces and whether the
// port uses it (another mod's may come first).
int list(const fs::path& root, const std::string& name) {
    const fs::path mods_root = root / "mods";
    const fs::path mod_dir = mods_root / path_u8(name);
    std::error_code ec;
    if (!fs::is_directory(mod_dir, ec)) {
        std::fprintf(stderr, "khdays-studio: no mod %s in %s\n", name.c_str(), u8(mods_root).c_str());
        return 1;
    }
    std::string note;
    const Settings settings = load_settings(root, &note);
    const Catalog catalog = load_catalog(root, settings);
    std::unordered_map<std::string, int> by_path;
    for (int i = 0; i < static_cast<int>(catalog.items.size()); ++i) {
        for (const auto& rel : lookup_paths(catalog.items[static_cast<std::size_t>(i)]))
            by_path.emplace(rel, i);
    }
    int files = 0, unused = 0, unknown = 0;
    for (fs::recursive_directory_iterator it(mod_dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec))
            continue;
        const std::string rel = u8(it->path().lexically_relative(mod_dir));
        if (rel.rfind("studio/", 0) == 0)
            continue;  // the tool's own
        ++files;
        const auto found = by_path.find(rel);
        if (found == by_path.end() && rel.rfind("files/", 0) == 0 &&
            fs::is_regular_file(khdays::vfs::data_root() / "nitrofs" / path_u8(rel.substr(6)), type_ec)) {
            // a game file replaced whole (one with an animation replaced): not in the catalog
            Item whole;
            whole.kind = Kind::text_file;
            whole.key = rel.substr(6);
            const auto h = holders(mods_root, whole);
            const bool used = !h.empty() && h.front().mod == name && fs::equivalent(h.front().file, it->path(), ec);
            if (!used)
                ++unused;
            std::printf("%s  game file  %s\n", rel.c_str(),
                        used
                            ? "used"
                            : ("not used: " + (h.empty() ? std::string("?") : h.front().mod) + " comes first").c_str());
            continue;
        }
        if (found == by_path.end()) {
            const bool companion = rel.find("/mdl/") != std::string::npos;  // a model's .bin, textures, bones.txt
            if (!companion)
                ++unknown;
            std::printf("%s  %s\n", rel.c_str(), companion ? "(beside a model)" : "(not a name the game has)");
            continue;
        }
        const Item& item = catalog.items[static_cast<std::size_t>(found->second)];
        const auto h = holders(mods_root, item);
        const bool used = !h.empty() && h.front().mod == name && fs::equivalent(h.front().file, it->path(), ec);
        if (!used)
            ++unused;
        std::printf("%s  %s  %s\n", rel.c_str(), kind_name(item.kind),
                    used ? "used"
                         : ("not used: " + (h.empty() ? std::string("?") : h.front().mod) + " comes first").c_str());
    }
    std::printf("%d files, %d not used, %d not names the game has\n", files, unused, unknown);
    return 0;
}

fs::path nitrofs_of(const fs::path& root) {
    if (!khdays::vfs::autodetect_data_root(root / "data" / "extracted"))
        return {};
    return khdays::vfs::data_root() / "nitrofs";
}

// --check-animations: every animation of the game rebuilt (every node kept,
// then every node replaced by its own motion) and read back, every file
// holding one rebuilt.
int check_animations(const fs::path& root) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    const bool ok = check_all_animations(nitrofs, [](const std::string& line) { std::printf("%s\n", line.c_str()); });
    return ok ? 0 : 1;
}

// --check-sheets: every 2D sheet of the game assembled with each screen or
// cell bank that fits it, taken back whole and drawn again.
int check_sheets(const fs::path& root) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    const bool ok = check_all_sheets(nitrofs, [](const std::string& line) { std::printf("%s\n", line.c_str()); });
    return ok ? 0 : 1;
}

// --export-sheets <folder> [mod:<Mod>]: every 2D sheet as the window first
// shows it, coloured, written into <folder> as <sheet>.png.
int export_all_sheets(const fs::path& root, const std::vector<std::string>& args) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    fs::path mod_dir;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i].rfind("mod:", 0) != 0) {
            std::fprintf(stderr, "khdays-studio: unknown option %s\n", args[i].c_str());
            return 2;
        }
        mod_dir = root / "mods" / path_u8(args[i].substr(4));
        std::error_code ec;
        if (!fs::is_directory(mod_dir, ec)) {
            std::fprintf(stderr, "khdays-studio: no mod %s in %s\n", args[i].c_str() + 4, u8(root / "mods").c_str());
            return 1;
        }
    }
    std::string note;
    const Settings settings = load_settings(root, &note);
    std::vector<std::string> failed;
    const int written = export_sheets(
        nitrofs, resolve(settings, settings.graphics_dump), mod_dir, path_u8(args[1]),
        [](std::size_t, std::size_t, const std::string& key) {
            if (!key.empty())
                std::printf("%s\n", key.c_str());
            return true;
        },
        &failed);
    for (const auto& f : failed)
        std::fprintf(stderr, "khdays-studio: not written: %s\n", f.c_str());
    std::printf("%d sheets written into %s, %zu not\n", written, args[1].c_str(), failed.size());
    return failed.empty() ? 0 : 1;
}

// --export-textures <folder> [scale:<n>]: every 3D texture the dump has, n times its size.
int export_all_textures(const fs::path& root, const std::vector<std::string>& args) {
    int scale = 1;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i].rfind("scale:", 0) != 0 || (scale = std::atoi(args[i].c_str() + 6)) < 1 || scale > 8) {
            std::fprintf(stderr, "khdays-studio: not an option: %s (scale:1 to scale:8)\n", args[i].c_str());
            return 2;
        }
    }
    std::string note;
    const Settings settings = load_settings(root, &note);
    const Catalog catalog = load_catalog(root, settings);
    std::vector<std::string> failed;
    if (!catalog.graphics_dump_found) {
        std::fprintf(stderr, "khdays-studio: no graphics dump (its 3D textures) at %s\n",
                     u8(resolve(settings, settings.graphics_dump)).c_str());
        return 1;
    }
    const int written = export_textures(
        catalog.items, scale, path_u8(args[1]),
        [](std::size_t, std::size_t, const std::string& key) {
            if (!key.empty())
                std::printf("%s\n", key.c_str());
            return true;
        },
        &failed);
    for (const auto& f : failed)
        std::fprintf(stderr, "khdays-studio: not written: %s\n", f.c_str());
    std::printf("%d textures written into %s, %zu not\n", written, args[1].c_str(), failed.size());
    return failed.empty() ? 0 : 1;
}

// --check-worlds: every mission world rebuilt with nothing changed, every
// room's collision written back and its tree checked and rebuilt.
int check_worlds(const fs::path& root) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    const bool ok = check_all_worlds(nitrofs, [](const std::string& line) { std::printf("%s\n", line.c_str()); });
    return ok ? 0 : 1;
}

// --world-info <code> [<room>]: a world's rooms and gate models; with a room
// (its index in the table), its collision.
int world_info(const fs::path& root, const std::vector<std::string>& args) {
    const fs::path nitrofs = nitrofs_of(root);
    World w;
    std::string why =
        nitrofs.empty() ? "no extracted game files under " + u8(root / "data" / "extracted") : std::string();
    if (!why.empty() || !load_world(nitrofs, args[1], &w, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    auto fx = [](std::int32_t v) { return static_cast<double>(v) / 4096.0; };
    if (args.size() == 2) {
        std::printf("%s: %zu entries, %zu rooms, %zu gate models\n", world_path(w.code).c_str(),
                    w.archive.entries.size(), w.rooms.size(), w.gate_models.size());
        for (const auto& r : w.rooms) {
            std::printf("room %d id %d collision #%d (%zu bytes)", r.index, r.id, r.collision_member,
                        w.archive.entries[static_cast<std::size_t>(r.collision_member)].data.size());
            for (const auto& o : r.objects) {
                std::printf(" object #%d flags %02x%s", o.member, o.flags,
                            w.archive.entries[o.member].data.empty() ? " (empty)" : "");
            }
            for (const auto& l : r.links)
                std::printf(" link key %d -> slot %d flags %04x", l.key, l.target, l.flags);
            std::printf("\n");
        }
        for (const auto& g : w.gate_models) {
            std::printf("gate model key %d:", g.key);
            for (const auto& part : g.parts) {
                std::printf(" [kind %d entry %d turn %.0f at %.2f %.2f %.2f scale %.2f]", part.kind, part.member,
                            part.turn * 360.0 / 65536.0, fx(part.position[0]), fx(part.position[1]),
                            fx(part.position[2]), fx(part.scale[0]));
            }
            std::printf("\n");
        }
        return 0;
    }
    const int index = std::atoi(args[2].c_str());
    if (index < 0 || static_cast<std::size_t>(index) >= w.rooms.size()) {
        std::fprintf(stderr, "khdays-studio: no room %d in %s\n", index, world_path(w.code).c_str());
        return 1;
    }
    const WorldRoom& r = w.rooms[static_cast<std::size_t>(index)];
    const auto& data = w.archive.entries[static_cast<std::size_t>(r.collision_member)].data;
    if (data.empty()) {
        std::printf("room %d (id %d): no collision (an empty entry)\n", r.index, r.id);
        return 0;
    }
    RoomCollision c;
    if (!parse_collision(data, &c, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    std::printf(
        "room %d (id %d): %zu grounds, %zu walls, %zu ceilings, %zu nodes, %zu placement markers; root centre %.2f %.2f size %.2f, "
        "smallest %.2f\n",
        r.index, r.id, c.faces[kGround].size(), c.faces[kWall].size(), c.faces[kCeiling].size(), c.nodes.size(),
        c.records18.size() / 0x18, fx(c.root(0)), fx(c.root(1)), fx(c.root(2)), fx(c.root(3)));
    // the extent of its faces, and its placement markers
    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
    for (const auto& list : c.faces) {
        for (const auto& f : list) {
            for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
                for (std::size_t a = 0; a < 3; ++a)
                    lo[a] = std::min(lo[a], fx(f.v[v][a])), hi[a] = std::max(hi[a], fx(f.v[v][a]));
            }
        }
    }
    std::printf("faces from %.2f %.2f %.2f to %.2f %.2f %.2f\n", lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
    for (const auto& m : markers(c)) {
        std::printf("marker %-8s at %.2f %.2f %.2f facing %.0f degrees\n", m.name.c_str(), fx(m.place[0]),
                    fx(m.place[1]), fx(m.place[2]), m.extra * 360.0 / 65536.0);
    }
    for (std::size_t k = 0; k < c.named.size(); ++k) {
        int on[3] = {0, 0, 0};
        for (int l = 0; l < 3; ++l) {
            for (const auto& f : c.faces[static_cast<std::size_t>(l)]) {
                for (std::size_t p = 0; p < 4; ++p)
                    on[l] += f.slots[p] == k && slot_names(l, p) ? 1 : 0;
            }
        }
        std::printf("named %zu %-8s: %d grounds, %d walls, %d ceilings\n", k, c.named[k].text().c_str(), on[0], on[1],
                    on[2]);
    }
    const std::string tree = check_tree(c);
    std::printf("tree: %s\n", tree.empty() ? "as the walk reads it" : tree.c_str());
    return 0;
}

// --room-model <code> <room> <out.gltf>: the room's models at rest and its
// collision, a mesh per kind of face and set of names, to build on.
int room_model(const fs::path& root, const std::vector<std::string>& args) {
    const fs::path nitrofs = nitrofs_of(root);
    World w;
    std::string why =
        nitrofs.empty() ? "no extracted game files under " + u8(root / "data" / "extracted") : std::string();
    std::string note;
    const Settings settings = load_settings(root, &note);
    std::vector<std::string> notes;
    if (!why.empty() || !load_world(nitrofs, args[1], &w, &why) ||
        !export_room(nitrofs, resolve(settings, settings.graphics_dump), w, std::atoi(args[2].c_str()),
                     path_u8(args[3]), &notes, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    for (const auto& n : notes)
        std::printf("left out: %s\n", n.c_str());
    std::printf("%s written\n", args[3].c_str());
    return 0;
}

// What a KH2 map made into a model holds, and what of it was left out.
void print_kh2_made(const std::string& name, const Kh2Made& made) {
    std::printf("%s: %zu triangles, %zu pictures, %zu pieces (%zu moving), %zu scrolling and %zu changing textures\n",
                name.c_str(), made.triangles, made.pictures, made.pieces, made.animations, made.scrolling, made.framed);
    for (const auto& line : made.report)
        std::printf("note: %s\n", line.c_str());
    std::printf("made from the player's own copy: Square Enix's work, not to be shared\n");
}

// --kh2-room <map> <out.gltf> [<folder>]: a KH2 map (tt02) of the player's collection made into a model
// to replace a room with, its pictures beside it; the folder the collection's archives gave it to
// (saves/collection/kh2 when not given).
int kh2_room(const fs::path& root, const std::vector<std::string>& args) {
    const fs::path folder = args.size() > 3 ? path_u8(args[3]) : kh2_folder(root);
    Kh2Made made;
    std::string why;
    if (!make_kh2_room(folder, args[1], path_u8(args[2]), &made, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    print_kh2_made(args[2], made);
    return 0;
}

// --kh2-maps [<world>]: the KH2 maps of the player's copy (of the worlds Days shares, or one), each with
// the Days rooms made from it and, once taken out, its pieces and moving textures.
int kh2_maps(const fs::path& root, const std::vector<std::string>& args) {
    std::string note, why;
    const Settings settings = load_settings(root, &note);
    std::vector<std::string> notes;
    const auto pairs = kh2_pairs({}, &notes);
    for (const auto& n : notes)
        std::fprintf(stderr, "khdays-studio: %s\n", n.c_str());
    const std::vector<std::string> worlds =
        args.size() > 1 ? std::vector<std::string>{args[1]}
                        : std::vector<std::string>(std::begin(kKh2Worlds), std::end(kKh2Worlds));
    int status = 0;
    for (const auto& world : worlds) {
        std::vector<std::string> maps;
        if (!kh2_world_maps(settings, world, &maps, &why)) {
            std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
            return 1;
        }
        std::printf("%s: %zu maps\n", world.c_str(), maps.size());
        for (const auto& map : maps) {
            std::string rooms;
            for (const auto& pair : pairs) {
                if (pair.map == map)
                    rooms += (rooms.empty() ? " Days " : ", ") + pair.world + " " + std::to_string(pair.room);
            }
            std::string what = "  (not taken out)";
            std::ifstream in(kh2_folder(root) / "original" / "map" / path_u8(map + ".map"), std::ios::binary);
            const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                                 std::istreambuf_iterator<char>());
            if (!data.empty()) {
                namespace kh2 = khdays::assets::kh2;
                try {
                    const auto m = kh2::read_map(data.data(), data.size());
                    std::size_t pieces = 0, moving = 0, scrolls = 0, frames = 0, skies = 0;
                    for (const auto& part : m.parts) {
                        pieces += part.name == "BOB" ? 1 : 0;
                        moving += part.motion_entry ? 1 : 0;
                        skies += part.name.rfind("SK", 0) == 0 ? 1 : 0;
                        const auto t =
                            kh2::read_textures(data.data(), data.size(), m.entries[part.texture_entry].offset);
                        scrolls += t.scrolls.size();
                        frames += t.animations.size();
                    }
                    char text[160];
                    std::snprintf(text, sizeof(text),
                                  "  %zu pieces (%zu moving), %zu skies, %zu scrolls, %zu texture animations", pieces,
                                  moving, skies, scrolls, frames);
                    what = text;
                } catch (const std::exception& e) {
                    what = std::string("  (cannot be read: ") + e.what() + ")";
                    status = 1;
                }
            }
            std::printf("  %s%s%s\n", map.c_str(), rooms.c_str(), what.c_str());
        }
    }
    return status;
}

// The game's own collision of the world's room `room`; false when it has none that can be read.
bool room_collision_of(const World& w, int room, RoomCollision* out) {
    if (room < 0 || static_cast<std::size_t>(room) >= w.rooms.size())
        return false;
    const auto member = static_cast<std::size_t>(w.rooms[static_cast<std::size_t>(room)].collision_member);
    std::string unused;
    return member < w.archive.entries.size() && parse_collision(w.archive.entries[member].data, out, &unused);
}

// The mod a room command works in, and the game files it reads; false (said) when either is missing.
bool room_command_paths(const fs::path& root, const std::string& mod, fs::path* nitrofs, fs::path* dump,
                        fs::path* mod_dir) {
    *nitrofs = nitrofs_of(root);
    if (nitrofs->empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return false;
    }
    *mod_dir = root / "mods" / path_u8(mod);
    std::error_code ec;
    if (!fs::is_directory(*mod_dir, ec)) {
        std::fprintf(stderr, "khdays-studio: no mod %s in %s\n", mod.c_str(), u8(root / "mods").c_str());
        return false;
    }
    std::string note;
    const Settings settings = load_settings(root, &note);
    *dump = resolve(settings, settings.graphics_dump);
    return true;
}

int print_outcome(bool ok, const WorldOutcome& o, const std::string& why) {
    for (const auto& line : o.lines)
        std::printf("%s\n", line.c_str());
    if (!ok) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    return 0;
}

// --replace-room <Mod> <code> <room> [<model> [<collision model>]]: with a
// model, it becomes the room's (copied beside the change); the room made again
// from its change and the world's outputs written.
int replace_room(const fs::path& root, const std::vector<std::string>& args) {
    fs::path nitrofs, dump, mod_dir;
    if (!room_command_paths(root, args[1], &nitrofs, &dump, &mod_dir))
        return 1;
    const std::string code = args[2];
    const int room = std::atoi(args[3].c_str());
    RoomReplacement r;
    std::string why;
    std::error_code ec;
    // kh2:<map>: a KH2 map of the player's collection, taken out and made into the model
    std::vector<std::string> given = args;
    std::string kh2;
    if (args.size() >= 5 && args[4].rfind("kh2:", 0) == 0) {
        kh2 = args[4].substr(4);
        std::string note;
        const Settings settings = load_settings(root, &note);
        Kh2Made made;
        const fs::path model = kh2_made_model(root, kh2);
        if (!take_out_kh2_map(settings, kh2, &why) || !make_kh2_room(kh2_folder(root), kh2, model, &made, &why))
            return print_outcome(false, {}, why);
        print_kh2_made(kh2, made);
        given[4] = u8(model);
        // kh2-collision: the map's own floors and walls, placed like its model
        if (args.size() >= 6 && args[5] == "kh2-collision") {
            fs::path collision;
            if (!make_kh2_collision(root, kh2, &collision, &why))
                return print_outcome(false, {}, why);
            given[5] = u8(collision);
        }
    }
    const fs::path file = change_file(mod_dir, code, room);
    if (fs::exists(file, ec)) {
        if (!read_change(file, &r, &why))
            return print_outcome(false, {}, why);
    } else if (args.size() < 5) {
        return print_outcome(false, {},
                             "room " + args[3] + " of " + code + " has no change in " + args[1] + ": give a model");
    } else {
        r.world = code;
        r.room = room;
        // a new model made from the room (its dump or export, edited): lined up with it again
        World w;
        khdays::assets::ImportedModel model;
        if (load_world(nitrofs, code, &w, &why) && import_model(path_u8(given[4]), &model, &why)) {
            RoomCollision collision;
            const bool has_collision = room_collision_of(w, room, &collision);
            const RoomAlignment a = !kh2.empty() && has_collision
                                        ? kh2_alignment(room_points(nitrofs, dump, w, room), collision, model)
                                        : align_with_room(room_points(nitrofs, dump, w, room), model);
            // the KH2 map this room was made from, where Days put it
            std::vector<std::string> notes;
            const auto pairs = kh2_pairs(mod_dir, &notes);
            const Kh2Pair* pair = kh2.empty() ? nullptr : kh2_pair_of(pairs, code, room);
            if (pair != nullptr && pair->placed && pair->map == kh2) {
                r.place.scale = 0.01f;
                r.place.turn = {0, pair->turn_y, 0};
                r.place.move = pair->move;
                std::printf("placed where Days put %s: turned %g, moved %.2f %.2f %.2f\n", kh2.c_str(), pair->turn_y,
                            pair->move[0], pair->move[1], pair->move[2]);
            } else if (a.verdict == RoomAlignment::kh2_units) {
                r.place = a.place;
                std::printf("placed by KH2's units (scale %g), its floor at the room's, the middles together: move "
                            "%.2f %.2f %.2f\n",
                            a.place.scale, a.place.move[0], a.place.move[1], a.place.move[2]);
            } else if (a.verdict == RoomAlignment::turned) {
                r.place = a.place;
                std::printf(
                    "lined up with the room: turned %.0f %.0f %.0f, scale %g (%.0f%% of its points on the room's)\n",
                    a.place.turn[0], a.place.turn[1], a.place.turn[2], a.place.scale, a.share * 100.0);
            } else if (a.verdict == RoomAlignment::in_place) {
                std::printf("lines up with the room as it is (%.0f%% of its points on the room's)\n", a.share * 100.0);
            } else {
                std::printf(
                    "does not line up with the room: placed as it is (give its scale, turn and move in its change)\n");
            }
        }
        why.clear();
    }
    const bool keep = args.size() >= 6 && args[5] == "game";  // the game's own floor and walls kept
    if (keep)
        r.keep_collision = true;
    if (!kh2.empty())
        r.kh2 = kh2;
    WorldOutcome o;
    const bool ok = args.size() >= 5 ? save_change(nitrofs, dump, mod_dir, r, path_u8(given[4]),
                                                   args.size() >= 6 && !keep ? path_u8(given[5]) : fs::path(), &o, &why)
                                     : write_world_outputs(nitrofs, dump, mod_dir, code, &o, &why);
    return print_outcome(ok, o, why);
}

// --revert-room <Mod> <code> <room>: the room's change and what it wrote removed.
int revert_room(const fs::path& root, const std::vector<std::string>& args) {
    fs::path nitrofs, dump, mod_dir;
    if (!room_command_paths(root, args[1], &nitrofs, &dump, &mod_dir))
        return 1;
    WorldOutcome o;
    std::string why;
    const bool ok = revert_change(nitrofs, dump, mod_dir, args[2], std::atoi(args[3].c_str()), &o, &why);
    return print_outcome(ok, o, why);
}

// --rebuild-worlds <Mod>: every world the mod changes written again from its
// changes over this copy of the game (after a game update, or another player's).
int rebuild_worlds(const fs::path& root, const std::vector<std::string>& args) {
    fs::path nitrofs, dump, mod_dir;
    if (!room_command_paths(root, args[1], &nitrofs, &dump, &mod_dir))
        return 1;
    int failed = 0;
    for (const auto& code : changed_worlds(mod_dir)) {
        WorldOutcome o;
        std::string why;
        failed += print_outcome(write_world_outputs(nitrofs, dump, mod_dir, code, &o, &why), o, why);
    }
    return failed == 0 ? 0 : 1;
}

// --check-missions: every mission file and the mission database's files read
// and written back, every script walked.
int check_missions(const fs::path& root) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    const bool ok = check_all_missions(nitrofs, [](const std::string& line) { std::printf("%s\n", line.c_str()); });
    return ok ? 0 : 1;
}

// The game's files and a mod's folder for the new missions' commands (the mod made when `make`).
bool mission_command_paths(const fs::path& root, const std::string& mod, bool make, fs::path* nitrofs,
                           fs::path* mod_dir) {
    *nitrofs = nitrofs_of(root);
    if (nitrofs->empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return false;
    }
    *mod_dir = root / "mods" / path_u8(mod);
    std::error_code ec;
    std::string why;
    if (!fs::is_directory(*mod_dir, ec) && !(make && create_mod(root / "mods", mod, &why))) {
        std::fprintf(stderr, "khdays-studio: no mod %s in %s%s%s\n", mod.c_str(), u8(root / "mods").c_str(),
                     why.empty() ? "" : ": ", why.c_str());
        return false;
    }
    return true;
}

// --new-mission <Mod> <key> <template> <world> <room>: a new mission made from a game mission's file, in
// Mission Mode's list after the game's.
int new_mission(const fs::path& root, const std::vector<std::string>& args) {
    fs::path nitrofs, mod_dir;
    if (!mission_command_paths(root, args[1], true, &nitrofs, &mod_dir))
        return 1;
    if (args[3] == "blank") {
        std::fprintf(stderr, "khdays-studio: a blank mission comes with the waves' editor; give a game mission's "
                             "number to start from (0010, the tutorial's square)\n");
        return 1;
    }
    std::string why;
    if (!new_mod_mission(nitrofs, mod_dir, args[2], std::atoi(args[3].c_str()), args[4], std::atoi(args[5].c_str()),
                         &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    std::printf("mission %s made in %s (mi/mi/%04d's file, %s room %s); name it with --mission-set\n", args[2].c_str(),
                u8(mod_dir / "missions").c_str(), std::atoi(args[3].c_str()), args[4].c_str(), args[5].c_str());
    return 0;
}

int rebuilt(const fs::path& nitrofs, const fs::path& mod_dir, const std::string& key);

// --mission-set <Mod> <key> <setting> [<language>] <value...>
int mission_set(const fs::path& root, const std::vector<std::string>& args) {
    fs::path nitrofs, mod_dir;
    if (!mission_command_paths(root, args[1], false, &nitrofs, &mod_dir))
        return 1;
    bool language = false;
    for (const MissionSetting& s : mission_settings())
        language = language || (args[3] == s.name && s.language);
    const std::size_t first = language ? 5 : 4;
    if (args.size() <= first) {
        std::fprintf(stderr, "khdays-studio: %s needs %sa value\n", args[3].c_str(),
                     language ? "a language (en, fr, de, it, es) and " : "");
        return 1;
    }
    std::string value;
    for (std::size_t i = first; i < args.size(); ++i)
        value += (i > first ? " " : "") + args[i];
    std::string why;
    if (!set_mod_mission(mod_dir, args[2], args[3], language ? args[4] : std::string(), value, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    std::printf("mission %s: %s%s%s = %s (missions/missions.txt read back)\n", args[2].c_str(), args[3].c_str(),
                language ? " " : "", language ? args[4].c_str() : "", value.c_str());
    return rebuilt(nitrofs, mod_dir, args[2]);
}

// After a change of mission `key`: when it has waves, whether it builds as the port will build it; 1 when not.
int rebuilt(const fs::path& nitrofs, const fs::path& mod_dir, const std::string& key) {
    bool has_waves = false;
    std::string why;
    std::vector<std::string> off_floor;
    const bool builds = check_mod_mission_build(nitrofs, mod_dir, key, &has_waves, &off_floor, &why);
    for (const std::string& w : off_floor)
        std::printf("warning: mission %s: %s\n", key.c_str(), w.c_str());
    if (!builds) {
        std::fprintf(stderr, "khdays-studio: mission %s does not build yet: %s (missions/missions.txt is changed)\n",
                     key.c_str(), why.c_str());
        return 1;
    }
    if (has_waves)
        std::printf("mission %s builds as the port will build it at its start\n", key.c_str());
    return 0;
}

// --mission-wave <Mod> <key> add <n> <class>x<count> ... at <x> <y> <z> | clear [<n>]
// --mission-chest <Mod> <key> add <item> at <x> <y> <z> <heading> | clear
// --mission-emblem <Mod> <key> add <kind> at <x> <y> <z> | clear
int mission_lines(const fs::path& root, const std::vector<std::string>& args) {
    fs::path nitrofs, mod_dir;
    if (!mission_command_paths(root, args[1], false, &nitrofs, &mod_dir))
        return 1;
    const std::string what = args[0] == "--mission-wave" ? "wave" : args[0] == "--mission-chest" ? "chest" : "emblem";
    const std::string& key = args[2];
    std::string why;
    if (args[3] == "clear") {
        const std::string setting = what == "wave" && args.size() > 4 ? "wave " + args[4] : what;
        int removed = 0;
        if (!clear_mod_mission_lines(mod_dir, key, setting, &removed, &why)) {
            std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
            return 1;
        }
        std::printf("mission %s: %d line(s) of %s taken away (missions/missions.txt read back)\n", key.c_str(), removed,
                    setting.c_str());
        return rebuilt(nitrofs, mod_dir, key);
    }
    const std::size_t first = what == "wave" ? 5 : 4;
    if (args[3] != "add" || args.size() <= first) {
        std::fprintf(stderr, "khdays-studio: %s add ... or clear\n", args[0].c_str());
        return 1;
    }
    std::string value;
    for (std::size_t i = first; i < args.size(); ++i)
        value += (i > first ? " " : "") + args[i];
    const std::string setting = what == "wave" ? "wave " + args[4] : what;
    if (!add_mod_mission_line(mod_dir, key, setting, value, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    std::printf("mission %s: %s = %s (missions/missions.txt read back)\n", key.c_str(), setting.c_str(), value.c_str());
    return rebuilt(nitrofs, mod_dir, key);
}

// --check-mod-missions <Mod>: every new mission of the mod read as the port reads it.
int check_mod_missions_command(const fs::path& root, const std::string& mod) {
    fs::path nitrofs, mod_dir;
    if (!mission_command_paths(root, mod, false, &nitrofs, &mod_dir))
        return 1;
    std::vector<std::string> lines;
    const bool ok = check_mod_missions(nitrofs, mod_dir, &lines);
    for (const std::string& line : lines)
        std::printf("%s\n", line.c_str());
    return ok ? 0 : 1;
}

// The folder of mod `mod` under the port's mods/; empty (told) when there is none.
fs::path mod_folder(const fs::path& root, const std::string& mod) {
    const fs::path dir = root / "mods" / path_u8(mod);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        std::fprintf(stderr, "khdays-studio: no mod %s in %s\n", mod.c_str(), u8(root / "mods").c_str());
        return {};
    }
    return dir;
}

void print_plugin_parameters(const PluginParameters& p) {
    for (int i = 0; i < p.list.count; ++i) {
        const KhdaysParameter& q = p.list.list[i];
        static const char* const kinds[] = {"integer", "number", "flag", "choice", "text"};
        char value[512];
        khdays_parameters_value_text(&q, value, sizeof(value));
        std::printf("%s (%s) = %s%s%s\n", q.name, kinds[q.kind], value, q.label != nullptr ? "  -- " : "",
                    q.label != nullptr ? q.label : "");
    }
    for (const std::string& problem : p.problems)
        std::printf("problem: %s\n", problem.c_str());
}

// --plugin-parameters <Mod>: the parameters its plugin's mod describes, each with the value chosen (else its default).
int plugin_parameters_command(const fs::path& root, const std::string& mod) {
    const fs::path dir = mod_folder(root, mod);
    PluginParameters p;
    if (dir.empty())
        return 1;
    if (!load_plugin_parameters(dir, p)) {
        std::printf("%s describes no plugin parameters (%s)\n", mod.c_str(), u8(p.descriptor).c_str());
        return 1;
    }
    print_plugin_parameters(p);
    return p.problems.empty() ? 0 : 1;
}

// --set-plugin-parameter <Mod> <name> <value>: one parameter's value chosen and saved beside its description.
int set_plugin_parameter_command(const fs::path& root, const std::vector<std::string>& args) {
    const fs::path dir = mod_folder(root, args[1]);
    PluginParameters p;
    std::string why;
    if (dir.empty())
        return 1;
    if (!load_plugin_parameters(dir, p)) {
        std::fprintf(stderr, "khdays-studio: %s describes no plugin parameters\n", args[1].c_str());
        return 1;
    }
    if (!set_plugin_parameter(p, args[2], args[3], why) || !save_plugin_parameters(p, why)) {
        std::fprintf(stderr, "khdays-studio: %s = %s: %s\n", args[2].c_str(), args[3].c_str(), why.c_str());
        return 1;
    }
    std::printf("saved: %s\n", u8(p.values).c_str());
    print_plugin_parameters(p);
    return 0;
}

// --check-texts: every text file of the game read and written back the same.
int check_texts(const fs::path& root) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    const bool ok = check_all_texts(nitrofs, [](const std::string& line) { std::printf("%s\n", line.c_str()); });
    return ok ? 0 : 1;
}

// --model-animations: a model's animations with as many nodes, those made for it said apart.
int model_animations(const fs::path& root, const std::string& key) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    std::string note, why;
    const Settings settings = load_settings(root, &note);
    GameModel g;
    if (!load_game_model(nitrofs, resolve(settings, settings.graphics_dump), key, &g, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    int fit = 0;
    for (const auto& a : g.animations) {
        std::printf("%s  %s:%s (%d frames)\n", a.fits ? "fits " : "other", a.label.c_str(), a.name.c_str(), a.frames);
        fit += a.fits ? 1 : 0;
    }
    std::printf("%s: %zu nodes; %d animations made for it, %zu of other models with as many nodes\n", key.c_str(),
                g.model.object_names.size(), fit, g.animations.size() - static_cast<std::size_t>(fit));
    return 0;
}

// --export-model <model> <out.gltf> [animations]: the model as "Export this model..." writes it; with
// its animations, each read back as "Replace this animation..." reads a clip and compared with the
// game's, at its curves' samples and between them (docs/STUDIO.md, "Models and animations").
int export_model(const fs::path& root, const std::vector<std::string>& args) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    const bool animations = args.size() == 4 && args[3] == "animations";
    if (args.size() == 4 && !animations) {
        std::fprintf(stderr, "khdays-studio: the last word is \"animations\" or nothing\n");
        return 1;
    }
    std::string note, why;
    const Settings settings = load_settings(root, &note);
    GameModel g;
    if (!load_game_model(nitrofs, resolve(settings, settings.graphics_dump), args[1], &g, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    const fs::path out = path_u8(args[2]);
    ModelExport done;
    if (!export_game_model(nitrofs, resolve(settings, settings.graphics_dump), g, animations, out, &done, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    std::error_code ec;
    std::printf("%s: %d files, %d animations, the glTF %llu bytes\n", u8(out).c_str(), done.files, done.animations,
                static_cast<unsigned long long>(fs::file_size(out, ec)));
    if (done.animations == 0)
        return 0;
    float worst = 0.0F, worst_turn = 0.0F;
    int failed = 0;
    const std::vector<std::string> names = export_animation_names(g);
    for (std::size_t i = 0; i < g.animations.size(); ++i) {
        if (!g.animations[i].fits)
            continue;
        khdays::assets::SkeletalAnimation a;
        Clip clip;
        if (!load_game_animation(nitrofs, g.animations[i], &a, &why) ||
            !load_clip(out, g.model.object_names, animation_pose(g, a), &clip, &why, names[i])) {
            std::printf("  %s: %s\n", names[i].c_str(), why.c_str());
            ++failed;
            continue;
        }
        if (clip.name != names[i]) {
            std::printf("  %s: read back the file's \"%s\"\n", names[i].c_str(), clip.name.c_str());
            ++failed;
        }
        // whether node n's rotation at frame f is one of its curve's samples (sample_rotation_curve's mapping)
        const auto on_sample = [&a](std::size_t n, std::size_t f) {
            if (n >= a.bones.size() || !a.bones[n].animated)
                return true;
            const auto& r = a.bones[n].rotation;
            if (r.kind != khdays::assets::AnimationRotationCurve::Kind::Samples || r.samples.size() < 2)
                return true;
            if (f <= r.start_frame || r.end_frame <= r.start_frame + 1U || f + 1U >= r.end_frame)
                return true;
            const float at = static_cast<float>(f - r.start_frame) /
                             static_cast<float>(r.end_frame - 1U - r.start_frame) *
                             static_cast<float>(r.samples.size() - 1U);
            return std::fabs(at - std::round(at)) < 1e-4F;
        };
        const AnimationPose original = animation_pose(g, a);
        float far = 0.0F, turn = 0.0F;
        std::size_t far_frame = 0, far_node = 0, far_entry = 0;
        for (std::size_t f = 0; f < original.size() && f < clip.pose.size(); ++f) {
            for (std::size_t n = 0; n < original[f].size() && n < clip.pose[f].size(); ++n) {
                const Mat& o = original[f][n];
                const Mat& c = clip.pose[f][n];
                if (on_sample(n, f)) {
                    for (std::size_t k = 0; k < 16; ++k) {
                        const float d = std::fabs(o[k] - c[k]);
                        if (d > far)
                            far = d, far_frame = f, far_node = n, far_entry = k;
                    }
                    continue;
                }
                for (std::size_t col = 0; col < 3; ++col) {
                    float dot = 0.0F, lo = 0.0F, lc = 0.0F;
                    for (std::size_t r = 0; r < 3; ++r) {
                        dot += o[4 * col + r] * c[4 * col + r];
                        lo += o[4 * col + r] * o[4 * col + r];
                        lc += c[4 * col + r] * c[4 * col + r];
                    }
                    if (lo > 1e-12F && lc > 1e-12F) {
                        const float cosine = std::clamp(dot / std::sqrt(lo * lc), -1.0F, 1.0F);
                        turn = std::max(turn, std::acos(cosine) * 57.29578F);
                    }
                }
            }
        }
        worst = std::max(worst, far);
        worst_turn = std::max(worst_turn, turn);
        if (far > 1e-3F) {
            std::printf(
                "  %s: %d frames, read back up to %g from the game's (frame %zu, node %s, entry %zu: %g, not %g)\n",
                names[i].c_str(), a.frame_count, far, far_frame, g.model.object_names[far_node].c_str(), far_entry,
                clip.pose[far_frame][far_node][far_entry], original[far_frame][far_node][far_entry]);
        }
    }
    std::printf("read back: %d animations, %d failed; on the curves' samples the farthest matrix entry %g from the "
                "game's, between them the axes turned up to %.2f degrees from the blend's\n",
                done.animations, failed, worst, worst_turn);
    return failed == 0 ? 0 : 1;
}

// A step as text: group.command handler(operands).
std::string step_text(const ScriptStep& s, const Script& script) {
    const CommandName* n = command_name(s.group, s.command);
    char head[48];
    std::snprintf(head, sizeof(head), "%d.%d", s.group, s.command);
    std::string name =
        n == nullptr ? "(no table names it)" : (n->handler[0] != '\0' ? n->handler : "(a null handler: skipped)");
    const std::string meaning = operands_meaning(s, &script);
    return std::string(head) + " " + name + "(" + operands_text(s, &script) + ")" +
           (s.field != 0 ? " field " + std::to_string(s.field) : std::string()) +
           (meaning.empty() ? std::string() : "  -- " + meaning);
}

// --mission-info <number> [<script>]: a mission's entries and scripts; with a
// script (its name, or entry:index), its steps.
int mission_info(const fs::path& root, const std::vector<std::string>& args) {
    const fs::path nitrofs = nitrofs_of(root);
    MissionFile m;
    std::string why =
        nitrofs.empty() ? "no extracted game files under " + u8(root / "data" / "extracted") : std::string();
    if (!why.empty() || !load_mission(nitrofs, std::atoi(args[1].c_str()), &m, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    if (args.size() == 2) {
        std::printf("%s: %zu entries\n", mission_path(m.number).c_str(), m.entries.size());
        for (std::size_t e = 0; e < m.entries.size(); ++e) {
            const auto& entry = m.entries[e];
            const std::string label = e < m.entry_names.size() ? m.entry_names[e] : std::string();
            if (entry.kind != MissionEntry::script_archive) {
                std::printf("entry %zu %-6s: %s, %zu bytes\n", e, label.c_str(),
                            entry.kind == MissionEntry::empty ? "empty" : "a table", m.p2.entries[e].data.size());
                EnemyTable t;
                std::string problem;
                if ((label == "em0" || label == "em0m") && parse_enemy_table(m.p2.entries[e].data, &t, &problem)) {
                    for (std::size_t r = 0; r < t.kinds.size(); ++r) {
                        const EnemyKind& k = t.kinds[r];
                        std::printf("  row %zu: class %d (+1 %d/%d, +2 %d, +3 %d, +4 %d, +5 %d, drop %d chance %d)\n",
                                    r, k.id(), k.kind(), k.high(), k.raw[2], k.raw[3],
                                    static_cast<std::int8_t>(k.raw[4]), k.param(), k.drop_key(), k.drop_chance());
                    }
                    for (std::size_t g = 0; g < t.groups.size(); ++g) {
                        std::string parts;
                        for (const auto& part : t.groups[g].parts) {
                            parts += (parts.empty() ? "" : " + ") + std::string("row ") + std::to_string(part.row) +
                                     " x" + std::to_string(part.spawned()) + " (count " + std::to_string(part.count) +
                                     ", limit " +
                                     (part.limit == 0xff ? std::string("none") : std::to_string(part.limit)) + ")";
                        }
                        std::printf("  group %zu: %s\n", g, parts.c_str());
                    }
                }
                continue;
            }
            std::printf("entry %zu %-6s: %zu scripts%s\n", e, label.c_str(), entry.archive.scripts.size(),
                        label.rfind("_s", 0) == 0 ? " (the start: its _i runs when the field is built)" : "");
            for (std::size_t i = 0; i < entry.archive.scripts.size(); ++i) {
                const Script& s = entry.archive.scripts[i];
                std::printf("  %zu:%zu %-16s %zu steps\n", e, i, s.name.c_str(), s.steps.size());
            }
        }
        // the mission's chests and emblems (trbox, evi: mission_db.h)
        for (const char* file : {"trbox", "evi"}) {
            std::vector<PlacedSet> sets;
            std::string problem;
            if (!load_placed_sets(nitrofs, file, &sets, &problem)) {
                std::printf("%s: %s\n", file, problem.c_str());
                continue;
            }
            const PlacedSet* set = placed_set(sets, m.number);
            std::printf("%s: %zu %s\n", file, set != nullptr ? set->rows.size() : 0,
                        std::strcmp(file, "trbox") == 0 ? "chests" : "emblems");
            for (std::size_t r = 0; set != nullptr && r < set->rows.size(); ++r) {
                const PlacedRow& row = set->rows[r];
                if (std::strcmp(file, "trbox") == 0) {
                    std::printf(
                        "  chest %zu: slot %d, bucket %d, contents %d, flag %d, at (%.3f, %.3f, %.3f) facing %d\n", r,
                        row.b0, row.b1, row.h2, row.h4, row.at[0] / 4096.0, row.at[1] / 4096.0, row.at[2] / 4096.0,
                        row.h6);
                } else {
                    std::printf("  emblem %zu: bit %d, kind %d, at (%.3f, %.3f, %.3f)\n", r, row.b0, row.b1,
                                row.at[0] / 4096.0, row.at[1] / 4096.0, row.at[2] / 4096.0);
                }
            }
        }
        return 0;
    }
    // the script: by name, or entry:index
    const Script* found = nullptr;
    const auto colon = args[2].find(':');
    for (std::size_t e = 0; e < m.entries.size() && found == nullptr; ++e) {
        const auto& scripts = m.entries[e].archive.scripts;
        for (std::size_t i = 0; i < scripts.size() && found == nullptr; ++i) {
            const bool by_place = colon != std::string::npos && std::to_string(e) == args[2].substr(0, colon) &&
                                  std::to_string(i) == args[2].substr(colon + 1);
            if (by_place || scripts[i].name == args[2])
                found = &scripts[i];
        }
    }
    if (found == nullptr) {
        std::fprintf(stderr, "khdays-studio: no script %s in %s\n", args[2].c_str(), mission_path(m.number).c_str());
        return 1;
    }
    std::printf("%s %s: %zu steps\n", mission_path(m.number).c_str(), found->name.c_str(), found->steps.size());
    std::size_t at = 4;
    for (const auto& s : found->steps) {
        std::printf("  %05zx  %s\n", at, step_text(s, *found).c_str());
        at += 4 * s.length();
    }
    return 0;
}

// --mission-commands: every (group, command) the game's missions use, with
// its handler, how many steps, and the lengths seen.
int mission_commands(const fs::path& root) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    std::map<std::pair<int, int>, std::pair<std::size_t, std::set<std::size_t>>> seen;
    for (const int number : mission_numbers(nitrofs)) {
        MissionFile m;
        std::string why;
        if (!load_mission(nitrofs, number, &m, &why)) {
            std::fprintf(stderr, "khdays-studio: %s: %s\n", mission_path(number).c_str(), why.c_str());
            return 1;
        }
        for (const auto& entry : m.entries) {
            for (const auto& s : entry.archive.scripts) {
                for (const auto& step : s.steps) {
                    auto& v = seen[{step.group, step.command}];
                    ++v.first;
                    v.second.insert(step.length());
                }
            }
        }
    }
    for (const auto& [gc, v] : seen) {
        const CommandName* n = command_name(gc.first, gc.second);
        std::string lengths;
        for (const auto l : v.second)
            lengths += (lengths.empty() ? "" : ",") + std::to_string(l);
        std::printf("%2d.%-3d x%-6zu words %-12s %s%s%s\n", gc.first, gc.second, v.first, lengths.c_str(),
                    n == nullptr ? "(no table names it)"
                                 : (n->handler[0] != '\0' ? n->handler : "(null handler: skipped)"),
                    n != nullptr && n->continuation[0] != '\0' ? " / " : "", n != nullptr ? n->continuation : "");
    }
    std::printf("%zu commands\n", seen.size());
    return 0;
}

// The options of --assemble and --take-back: `screen:N`, `cells:N` or `sheet` (several:
// one under the other, as the window stacks them), and `palette:N`, `scale:K`,
// `mod:<Mod>`, `colours:own|fit`, `row:N` (the palette row of `sheet`).
struct SheetOptions {
    Arrangement arrangement;        // its palette, and the last one named
    std::vector<Arrangement> list;  // all named, in order
    bool have_arrangement = false, have_palette = false;
    bool best = false;  // the one the window shows first
    bool have_row = false;
    int scale = 0;
    std::string mod;
    Colours colours = Colours::automatic;
    std::string error;
};

SheetOptions sheet_options(const std::vector<std::string>& words) {
    SheetOptions o;
    for (const auto& w : words) {
        const auto colon = w.find(':');
        const std::string name = colon == std::string::npos ? w : w.substr(0, colon);
        const std::string value = colon == std::string::npos ? std::string() : w.substr(colon + 1);
        if (name == "best") {
            o.best = true;
            o.have_arrangement = true;
        } else if (name == "screen" || name == "cells" || name == "sheet") {
            o.arrangement.cells = name == "cells";
            o.arrangement.whole = name == "sheet";
            o.arrangement.resource = name == "sheet" ? 0 : std::atoi(value.c_str());
            o.list.push_back(o.arrangement);
            o.have_arrangement = true;
        } else if (name == "row") {
            o.have_row = true;
            for (auto& a : o.list)
                a.row = std::atoi(value.c_str());
            o.arrangement.row = std::atoi(value.c_str());
        } else if (name == "palette") {
            o.arrangement.palette = value == "none" ? -1 : std::atoi(value.c_str());
            o.have_palette = true;
        } else if (name == "scale") {
            o.scale = std::atoi(value.c_str());
        } else if (name == "mod") {
            o.mod = value;
        } else if (name == "colours") {
            o.colours = value == "own" ? Colours::own : value == "fit" ? Colours::fit : Colours::automatic;
        } else {
            o.error = "unknown option " + w;
        }
    }
    if (!o.have_arrangement && o.error.empty())
        o.error = "say screen:N, cells:N or sheet";
    for (auto& a : o.list)
        a.row = o.arrangement.row;  // row:N wherever it was given
    return o;
}

// The sheet, the arrangements (the palette their fit gives when none is
// named) and the assembly, several one under the other; the sheet as it is
// with the rows the window gives its tiles (the mod's, else its screen's).
bool open_sheet(const fs::path& root, const std::string& key, SheetOptions& o, const fs::path& mod_dir, SheetSource* s,
                Assembly* a) {
    const fs::path nitrofs = nitrofs_of(root);
    std::string why;
    if (nitrofs.empty())
        why = "no extracted game files under " + u8(root / "data" / "extracted");
    else if (!o.error.empty())
        why = o.error;
    else if (load_sheet_source(nitrofs, key, s, &why)) {
        std::string note;
        const Settings settings = load_settings(root, &note);
        take_seen_colours(resolve(settings, settings.graphics_dump), key, s);
        const auto fits = fitting_arrangements(*s);
        const Ranked ranked = rank_arrangements(*s, fits, game_sheet_pixels(*s));
        Arrangement ranked_whole;
        for (const auto& r : ranked.order) {
            if (r.whole)
                ranked_whole = r;
        }
        std::vector<int> tile_rows = read_tile_rows(mod_dir, key, s->tiles);
        if (tile_rows.empty())
            tile_rows = ranked_whole.rows;
        if (o.best) {
            std::vector<Arrangement> together(
                ranked.order.begin(),
                ranked.order.begin() + static_cast<std::ptrdiff_t>(std::min(ranked.together, ranked.order.size())));
            std::string names;
            for (auto& one : together) {
                if (o.have_row)
                    one.row = o.arrangement.row;
                names += std::string(names.empty() ? "" : " ") +
                         (one.whole ? "sheet" : (one.cells ? "cells:" : "screen:") + std::to_string(one.resource));
            }
            o.list.insert(o.list.begin(), together.begin(), together.end());
            std::printf("best: %s\n", names.c_str());
        }
        std::string which;
        for (const auto& f : fits) {
            which += f.whole ? std::string(" sheet")
                             : std::string(" ") + (f.cells ? "cells:" : "screen:") + std::to_string(f.resource);
        }
        bool all_fit = true;
        for (std::size_t i = 0; i < o.list.size(); ++i) {
            bool fits_one = false;
            for (const auto& f : fits) {
                if (f.whole != o.list[i].whole ||
                    (!f.whole && (f.cells != o.list[i].cells || f.resource != o.list[i].resource))) {
                    continue;
                }
                fits_one = true;
                if (!o.have_palette && i == 0)
                    o.arrangement.palette = f.palette;
                if (f.whole && !o.have_row)
                    o.arrangement.row = ranked_whole.row;
            }
            all_fit = all_fit && fits_one;
        }
        for (auto& one : o.list) {
            one.palette = o.arrangement.palette;
            one.row = o.arrangement.row;
            if (one.whole)
                one.rows = tile_rows;
        }
        if (!all_fit) {
            why = "an arrangement named does not fit this sheet (its depth or its tiles); those that do:" +
                  (which.empty() ? std::string(" none") : which);
        } else if (assemble_stack(*s, o.list, a, nullptr, &why)) {
            return true;
        }
    }
    std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
    return false;
}

// --assemble <sheet> <screen:N|cells:N|sheet>... [row:N] [palette:N] [scale:K] [mod:<Mod>] <out.png>:
// the sheet assembled as a picture to edit (the mod's sheet when it has one).
int assemble_sheet(const fs::path& root, const std::vector<std::string>& args) {
    SheetOptions o = sheet_options(std::vector<std::string>(args.begin() + 2, args.end() - 1));
    SheetSource s;
    Assembly a;
    const fs::path mod_dir = o.mod.empty() ? fs::path() : root / "mods" / path_u8(o.mod);
    if (!open_sheet(root, args[1], o, mod_dir, &s, &a))
        return 1;
    fs::path from;
    SheetPixels p = current_sheet(mod_dir, args[1], s, &from);
    if (o.scale > 0)
        p = rescale(p, o.scale);
    const auto palette = arrangement_palette(s, o.arrangement);
    const auto picture = render_assembly(s, a, p, palette);
    const fs::path out = path_u8(args.back());
    if (const char* e = khdays_png_write(u8(out).c_str(), picture.data(), a.width * p.scale, a.height * p.scale)) {
        std::fprintf(stderr, "khdays-studio: %s\n", e);
        return 1;
    }
    std::printf("%s: %dx%d (%d times), from %s, palette %s; %d pixels where cell pieces overlap\n", args.back().c_str(),
                a.width * p.scale, a.height * p.scale, p.scale, from.empty() ? "the game's sheet" : u8(from).c_str(),
                o.arrangement.palette < 0 ? "none (greys)"
                                          : (std::to_string(o.arrangement.palette) +
                                             (palette_from(s, o.arrangement.palette).empty()
                                                  ? std::string()
                                                  : " from " + palette_from(s, o.arrangement.palette)))
                                                .c_str(),
                a.overlaps);
    return 0;
}

// --arrangements <sheet>: every arrangement that fits the sheet in the order
// the window offers them, with what that order is made from.
int list_arrangements(const fs::path& root, const std::string& key) {
    const fs::path nitrofs = nitrofs_of(root);
    SheetSource s;
    std::string why =
        nitrofs.empty() ? "no extracted game files under " + u8(root / "data" / "extracted") : std::string();
    if (!why.empty() || !load_sheet_source(nitrofs, key, &s, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    std::printf(
        "%d tiles of %d bits, %s; sheet %d of %d in the data, %zu screens, %zu cell banks, %zu palettes (%zu nearby)\n",
        s.tiles, s.bpp(),
        s.shaped ? ("its header's " + std::to_string(s.columns) + " columns").c_str() : "no shape in its header",
        s.number, s.sheets_in_data, s.screens.size(), s.cell_banks.size(), s.palettes.size(), s.nearby.size());
    if (!s.borrowed.empty())
        std::printf("screens and cells from %s\n", s.borrowed.c_str());
    std::string note;
    const Settings settings = load_settings(root, &note);
    if (take_seen_colours(resolve(settings, settings.graphics_dump), key, &s)) {
        std::printf("the colours seen in play: palette %d; %d tiles never seen, in its greys (row %d)\n",
                    s.seen_palette, s.unseen_tiles, s.seen_row);
    }
    const SheetPixels pixels = game_sheet_pixels(s);
    const auto fits = fitting_arrangements(s);
    const auto scores = score_arrangements(s, fits, pixels);
    const Ranked ranked = rank_arrangements(s, fits, pixels);
    for (std::size_t i = 0; i < ranked.order.size(); ++i) {
        const Arrangement& a = ranked.order[i];
        for (const auto& sc : scores) {
            const Arrangement& f = sc.arrangement;
            if (f.whole != a.whole || (!f.whole && (f.cells != a.cells || f.resource != a.resource)))
                continue;
            std::printf("%s%s palette %d%s jump %.2f shown %.2f%s\n", i < ranked.together ? "* " : "  ",
                        f.whole ? "sheet" : ((f.cells ? "cells:" : "screen:") + std::to_string(f.resource)).c_str(),
                        f.palette, f.whole ? (" row " + std::to_string(a.row)).c_str() : "", sc.jump, sc.shown,
                        sc.as_sheet   ? " in order, as the sheet"
                        : sc.in_order ? " in order"
                                      : "");
        }
    }
    std::printf("(* shown together to begin with)\n");
    return 0;
}

// --take-back <Mod> <sheet> <screen:N|cells:N|sheet>... [row:N] [palette:N] [colours:own|fit] <edited.png>:
// an edited assembled picture into the mod's sheet.
int take_back_sheet(const fs::path& root, const std::vector<std::string>& args) {
    SheetOptions o = sheet_options(std::vector<std::string>(args.begin() + 3, args.end() - 1));
    const fs::path mod_dir = root / "mods" / path_u8(args[1]);
    std::error_code ec;
    if (!fs::is_directory(mod_dir, ec)) {
        std::fprintf(stderr, "khdays-studio: no mod %s in %s\n", args[1].c_str(), u8(root / "mods").c_str());
        return 1;
    }
    SheetSource s;
    Assembly a;
    if (!open_sheet(root, args[2], o, mod_dir, &s, &a))
        return 1;
    const SheetPixels current = current_sheet(mod_dir, args[2], s, nullptr);
    const auto palette = arrangement_palette(s, o.arrangement);
    const char* error = nullptr;
    int w = 0, h = 0;
    std::uint8_t* px = khdays_png_read(args.back().c_str(), &w, &h, &error);
    if (px == nullptr) {
        std::fprintf(stderr, "khdays-studio: %s\n", error != nullptr ? error : "unreadable");
        return 1;
    }
    const std::vector<std::uint8_t> edited(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    khdays_png_free(px);
    TakeBack t;
    std::string why;
    fs::path written;
    if (!take_back(s, a, current, palette, edited, w, h, o.colours, false, &t, &why) ||
        !save_sheet(mod_dir, args[2], s, t.sheet, display_colours(s, a, palette), &written, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    auto list = [](const std::vector<int>& tiles) {
        std::string out;
        for (const int t : tiles)
            out += " " + std::to_string(t);
        return out;
    };
    std::printf("%s: %s, %d times; %d pixels changed\n", u8(written.lexically_relative(root)).c_str(),
                t.sheet.indexed ? "the game's palettes" : "its own colours", t.scale, t.changed);
    if (t.outside > 0)
        std::printf("not taken: %d changed pixels where no tile is\n", t.outside);
    if (t.off_palette > 0)
        std::printf("%d changed pixels are no colour of their palette\n", t.off_palette);
    if (!t.conflicts.empty())
        std::printf("tiles edited differently in two places (the first kept):%s\n", list(t.conflicts).c_str());
    if (!t.repeated.empty())
        std::printf("changed tiles placed more than once (the change shows in each):%s\n", list(t.repeated).c_str());
    if (!t.flipped.empty())
        std::printf("changed tiles placed flipped somewhere:%s\n", list(t.flipped).c_str());
    if (!t.several_rows.empty())
        std::printf("tiles drawn with several palettes, now one set of colours:%s\n", list(t.several_rows).c_str());
    if (t.unplaced_coloured > 0)
        std::printf("%d tiles not in this picture coloured with the palette's first row\n", t.unplaced_coloured);
    return 0;
}

// --replace-animation <Mod> <model> <animation> <clip>: the model as the
// window names it (`<data>/mdl/<name>`), the animation by its name (or
// `<data>:<name>` when two files name one alike), the clip a glTF/glb file.
int replace_animation(const fs::path& root, const std::vector<std::string>& args) {
    const fs::path nitrofs = nitrofs_of(root);
    if (nitrofs.empty()) {
        std::fprintf(stderr, "khdays-studio: no extracted game files under %s\n",
                     u8(root / "data" / "extracted").c_str());
        return 1;
    }
    const fs::path mod_dir = root / "mods" / path_u8(args[1]);
    std::error_code ec;
    if (!fs::is_directory(mod_dir, ec)) {
        std::fprintf(stderr, "khdays-studio: no mod %s in %s\n", args[1].c_str(), u8(root / "mods").c_str());
        return 1;
    }
    std::string note, why;
    const Settings settings = load_settings(root, &note);
    GameModel g;
    if (!load_game_model(nitrofs, resolve(settings, settings.graphics_dump), args[2], &g, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    std::string wanted = args[3], in_data;
    const auto colon = wanted.rfind(':');
    if (colon != std::string::npos) {
        in_data = wanted.substr(0, colon);
        wanted = wanted.substr(colon + 1);
    }
    std::vector<const GameAnimation*> matches;
    for (const auto& a : g.animations) {
        if (a.name == wanted && (in_data.empty() || a.label == in_data))
            matches.push_back(&a);
    }
    if (matches.size() != 1) {
        std::fprintf(stderr, "khdays-studio: %s animation %s for %s\n", matches.empty() ? "no" : "more than one",
                     args[3].c_str(), args[2].c_str());
        for (const auto* a : matches)
            std::fprintf(stderr, "  %s:%s\n", a->label.c_str(), a->name.c_str());
        return 1;
    }
    AnimationReplacement r;
    if (!replace_animation_in_mod(nitrofs, mod_dir, g, *matches.front(), path_u8(args[4]), &r, &why)) {
        std::fprintf(stderr, "khdays-studio: %s\n", why.c_str());
        return 1;
    }
    std::printf("%s: %s (%d frames) from %s%s\n", u8(r.written.lexically_relative(root)).c_str(), wanted.c_str(),
                matches.front()->frames, args[4].c_str(), r.on_mod_copy ? ", on the mod's copy of the file" : "");
    std::printf("moved by the clip: %zu nodes; keeping the original's motion: %zu", r.animated.size(),
                r.left_alone.size());
    for (const auto& n : r.left_alone)
        std::printf(" %s", n.c_str());
    std::printf("\nread back: largest difference %.5f\n", static_cast<double>(r.difference));
    if (!r.not_plain.empty()) {
        std::printf("note: on these nodes a clip made on the dumped skeleton lands only approximately:");
        for (const auto& n : r.not_plain)
            std::printf(" %s", n.c_str());
        std::printf("\n");
    }
    return 0;
}

}  // namespace

bool take_port_argument(std::vector<std::string>& args, fs::path* root) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--port") {
            if (i + 1 >= args.size())
                return false;
            *root = path_u8(args[i + 1]);
            args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                       args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
            return true;
        }
    }
    return true;
}

int run_command_line(const fs::path& root, const std::vector<std::string>& args) {
    if (args.size() == 2 && args[0] == "--list")
        return list(root, args[1]);
    if (args.size() == 1 && args[0] == "--check-animations")
        return check_animations(root);
    if (args.size() == 1 && args[0] == "--check-sheets")
        return check_sheets(root);
    if (args.size() == 1 && args[0] == "--check-worlds")
        return check_worlds(root);
    if ((args.size() == 2 || args.size() == 3) && args[0] == "--world-info")
        return world_info(root, args);
    if (args.size() == 4 && args[0] == "--room-model")
        return room_model(root, args);
    if ((args.size() == 3 || args.size() == 4) && args[0] == "--kh2-room")
        return kh2_room(root, args);
    if ((args.size() == 1 || args.size() == 2) && args[0] == "--kh2-maps")
        return kh2_maps(root, args);
    if (args.size() == 1 && args[0] == "--check-missions")
        return check_missions(root);
    if (args.size() == 1 && args[0] == "--check-texts")
        return check_texts(root);
    if (args.size() == 2 && args[0] == "--model-animations")
        return model_animations(root, args[1]);
    if ((args.size() == 3 || args.size() == 4) && args[0] == "--export-model")
        return export_model(root, args);
    if ((args.size() == 2 || args.size() == 3) && args[0] == "--mission-info")
        return mission_info(root, args);
    if (args.size() == 1 && args[0] == "--mission-commands")
        return mission_commands(root);
    if (args.size() == 6 && args[0] == "--new-mission")
        return new_mission(root, args);
    if (args.size() >= 5 && args[0] == "--mission-set")
        return mission_set(root, args);
    if (args.size() == 2 && args[0] == "--check-mod-missions")
        return check_mod_missions_command(root, args[1]);
    if (args.size() >= 4 &&
        (args[0] == "--mission-wave" || args[0] == "--mission-chest" || args[0] == "--mission-emblem"))
        return mission_lines(root, args);
    if ((args.size() >= 4 && args.size() <= 6) && args[0] == "--replace-room")
        return replace_room(root, args);
    if (args.size() == 4 && args[0] == "--revert-room")
        return revert_room(root, args);
    if (args.size() == 2 && args[0] == "--rebuild-worlds")
        return rebuild_worlds(root, args);
    if (args.size() >= 4 && args[0] == "--assemble")
        return assemble_sheet(root, args);
    if (args.size() == 2 && args[0] == "--arrangements")
        return list_arrangements(root, args[1]);
    if ((args.size() == 2 || args.size() == 3) && args[0] == "--export-sheets")
        return export_all_sheets(root, args);
    if ((args.size() == 2 || args.size() == 3) && args[0] == "--export-textures")
        return export_all_textures(root, args);
    if (args.size() >= 5 && args[0] == "--take-back")
        return take_back_sheet(root, args);
    if (args.size() == 5 && args[0] == "--replace-animation")
        return replace_animation(root, args);
    if (args.size() == 2 && args[0] == "--plugin-parameters")
        return plugin_parameters_command(root, args[1]);
    if (args.size() == 4 && args[0] == "--set-plugin-parameter")
        return set_plugin_parameter_command(root, args);
    std::fprintf(
        stderr,
        "usage: khdays-studio [--port <dir>]               the window\n"
        "       khdays-studio [--port <dir>] --list <Mod>  a mod's files and whether the port uses them\n"
        "       khdays-studio [--port <dir>] --replace-animation <Mod> <model> <animation> <clip.gltf>\n"
        "                                                  one of a model's animations replaced by a glTF clip\n"
        "       khdays-studio [--port <dir>] --check-animations\n"
        "                                                  every animation of the game rebuilt and read back\n"
        "       khdays-studio [--port <dir>] --check-sheets\n"
        "                                                  every 2D sheet assembled and taken back\n"
        "       khdays-studio [--port <dir>] --check-worlds\n"
        "                                                  every mission world rebuilt, its rooms' collision checked\n"
        "       khdays-studio [--port <dir>] --world-info <code> [<room>]\n"
        "                                                  a mission world's rooms, or one room's collision\n"
        "       khdays-studio [--port <dir>] --room-model <code> <room> <out.gltf>\n"
        "                                                  a room's models and collision, to build on\n"
        "       khdays-studio [--port <dir>] --kh2-room <map> <out.gltf> [<folder>]\n"
        "                                                  a KH2 map of the player's collection as a model\n"
        "       khdays-studio [--port <dir>] --kh2-maps [<world>]\n"
        "                                                  the KH2 maps of the player's copy, the Days rooms made from each\n"
        "       khdays-studio [--port <dir>] --replace-room <Mod> <code> <room> [<model> | kh2:<map> [<collision model> | game | kh2-collision]]\n"
        "                                                  a room made from a model (or again from its change)\n"
        "       khdays-studio [--port <dir>] --revert-room <Mod> <code> <room>\n"
        "                                                  a room's change and what it wrote removed\n"
        "       khdays-studio [--port <dir>] --rebuild-worlds <Mod>\n"
        "                                                  every world the mod changes written again\n"
        "       khdays-studio [--port <dir>] --model-animations <model>\n"
        "                                                  a model's animations, those made for it apart\n"
        "       khdays-studio [--port <dir>] --export-model <model> <out.gltf> [animations]\n"
        "                                                  a model as glTF, with its animations read back\n"
        "       khdays-studio [--port <dir>] --check-texts\n"
        "                                                  every text file read and written back the same\n"
        "       khdays-studio [--port <dir>] --check-missions\n"
        "                                                  every mission read and written back, its scripts walked\n"
        "       khdays-studio [--port <dir>] --mission-info <number> [<script>|<entry>:<index>]\n"
        "                                                  a mission's scripts, or one script's steps\n"
        "       khdays-studio [--port <dir>] --mission-commands\n"
        "                                                  every script command the missions use, by its handler\n"
        "       khdays-studio [--port <dir>] --new-mission <Mod> <key> <game mission> <world> <room>\n"
        "                                                  a new mission in Mission Mode's list, from a game mission\n"
        "       khdays-studio [--port <dir>] --mission-set <Mod> <key> <setting> [<language>] <value...>\n"
        "                                                  a new mission's name, objective, category, unlock...\n"
        "       khdays-studio [--port <dir>] --mission-wave <Mod> <key> add <n> <class>x<count>... at <x> <y> <z> | clear [<n>]\n"
        "                                                  a wave's spawner: the first at the start, each next when all are defeated\n"
        "       khdays-studio [--port <dir>] --mission-chest <Mod> <key> add <item> at <x> <y> <z> <heading> | clear\n"
        "       khdays-studio [--port <dir>] --mission-emblem <Mod> <key> add <kind> at <x> <y> <z> | clear\n"
        "       khdays-studio [--port <dir>] --check-mod-missions <Mod>\n"
        "                                                  a mod's new missions read as the port reads them\n"
        "       khdays-studio [--port <dir>] --assemble <sheet> screen:N|cells:N|sheet... [row:N] [palette:N] [scale:K]\n"
        "                                               [mod:<Mod>] <out.png>\n"
        "                                                  a 2D sheet assembled as a picture to edit\n"
        "       khdays-studio [--port <dir>] --export-sheets <folder> [mod:<Mod>]\n"
        "                                                  every 2D sheet coloured as the window first shows it\n"
        "       khdays-studio [--port <dir>] --export-textures <folder> [scale:<n>]\n"
        "                                                  every 3D texture of the dump, n times its size\n"
        "       khdays-studio [--port <dir>] --arrangements <sheet>\n"
        "                                                  the arrangements that fit a sheet, best first\n"
        "       khdays-studio [--port <dir>] --take-back <Mod> <sheet> screen:N|cells:N|sheet... [row:N] [palette:N]\n"
        "                                                [colours:own|fit] <edited.png>\n"
        "                                                  the edited picture into the mod's sheet\n"
        "       khdays-studio [--port <dir>] --plugin-parameters <Mod>\n"
        "                                                  its native plugin's parameters and the values chosen\n"
        "       khdays-studio [--port <dir>] --set-plugin-parameter <Mod> <name> <value>\n"
        "                                                  one of them chosen and saved\n");
    return 2;
}

}  // namespace studio
