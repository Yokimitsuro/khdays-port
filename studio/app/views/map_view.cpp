// The map editor: a room of a mission world as the game has it -- its models, collision, markers and
// exits, any face or marker picked to read what it is -- and replaced by the modder's model, its
// collision made and shown as the game will read it (docs/STUDIO.md, "Rooms").
#include "../app.h"
#include "../save_dialog.h"

#include "core/models/bones.h"
#include "core/models/game_model.h"
#include "core/models/model_files.h"
#include "core/files/paths.h"
#include "core/rooms/added_rooms.h"
#include "core/rooms/room_music.h"
#include "core/rooms/collection_source.h"
#include "core/rooms/kh2_source.h"
#include "core/rooms/room_change.h"
#include "mods/room_animation.h"  // port/runtime: as the port moves a room
#include "core/rooms/room_export.h"
#include "core/mods/settings.h"
#include "core/rooms/worlds.h"
#include "../preview3d.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <SDL3/SDL.h>

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace studio {

namespace {

namespace assets = khdays::assets;

constexpr float kFx = 1.0f / 4096.0f;  // the game's 20.12 to units

struct Texture {
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
};

struct RoomModel {
    int member = 0;
    std::string key;      // the dump's name, empty when not found
    std::string problem;  // why it is not drawn
    GameModel game;
    std::vector<Mat> palette;
};

// A replacement being made (or the mod's, opened): what the modder gave,
// placed, and the collision made from it.
struct Replacing {
    bool active = false;
    bool from_mod = false;  // the mod holds a change for this room
    RoomReplacement r;
    fs::path model_source, collision_source;   // the files as given (or the mod's copies)
    assets::ImportedModel raw, raw_collision;  // as read
    assets::ImportedModel placed;              // placed: what is drawn
    RoomPlacement placed_with;                 // where `placed` was put (drawn moved to the place now while dragged)
    SurfaceMap surfaces;                       // its floors and roofs from above, as placed_with puts them
    bool dirty = false, built_ok = false;
    RoomCollision built;
    CollisionReport report;
    std::vector<RoomWarning> warnings;
    std::string error, message;
    int view = 0;  // 0 yours, 1 the game's, 2 both
    bool next_is_collision = false;
    bool other_collision = false;  // the collision from another model, chosen (its file perhaps not yet)
    bool kh2_collision = false;    // that model the map's own floors and walls (KH2's, or another game's)
    std::string placed_text;       // how the model was placed (lined up, as it is, or to place by hand)
    bool placed_ok = true;
};

struct MapView {
    int item = -1;
    int mod = -1;  // the mod chosen when the room was opened
    std::string error;
    World world;
    const WorldRoom* room = nullptr;
    bool has_collision = false;
    RoomCollision collision;
    std::vector<Marker> markers;
    std::vector<RoomModel> models;
    std::map<std::string, Texture> textures;

    // as the game shows it first; the collision laid over it when asked
    bool show_models = true, show_ground = false, show_walls = false, show_ceilings = false, show_markers = true;
    bool show_sky = false;    // a sky around the room hides it from outside: drawn only when asked
    float view_share = 0.5f;  // the picture's share of the panel's height (dragged below it)
    float yaw = 30.0f, pitch = 35.0f, distance = 40.0f;
    std::array<float, 3> target{0, 0, 0};
    int picked_list = -1, picked_face = -1, picked_marker = -1;
    std::unique_ptr<Preview> preview;
    Replacing rep;
    bool open_replace = false;  // the mod's replacement just opened: its panel shown open
};

MapView m;

// KH2's rooms of the player's collection, listed and made on a thread of their own (the archives are read
// by the installer's reader in Python), what it made taken by the window once done. Kept across rooms.
struct Kh2Picker {
    bool shown = false;
    int world = -1;  // in kKh2Worlds
    std::vector<std::string> maps;
    std::map<std::string, bool> out;  // a map taken out of the copy already
    std::map<std::string, assets::DecodedTexture> radars;
    std::string typed, error;
    std::vector<Kh2Pair> pairs;  // the studio's, then the mod's
    int pairs_for = -2;          // the mod they were read for
    fs::path collection;
    std::string collection_for = "\x01";  // the setting `collection` was found for
    std::thread worker;
    std::atomic<bool> busy{false};
    std::string working;  // the map being made; empty while listing
    // left by the worker
    std::mutex mutex;
    bool listed = false, made = false;
    std::vector<std::string> found;
    std::string done_map, failed;
    fs::path model;
    Kh2Made outcome;
    ~Kh2Picker() {
        if (worker.joinable())
            worker.join();
    }
};
Kh2Picker kh2;

// The rooms of the other games in the player's collections (KH1, Re:Chain of Memories, Dream Drop Distance), listed and
// made on a thread of their own as KH2's are, what it made taken by the window once done. Kept across rooms.
struct OtherPicker {
    bool shown = false;
    int game = 0;          // in other_games()
    int world = 0;         // in `worlds`
    int listed_game = -1;  // the game `maps` are of
    std::vector<CollectionMap> maps;
    std::vector<std::string> worlds;
    std::map<std::string, bool> out;  // a map taken out of the copy already
    std::string error;
    fs::path copy;
    std::string copy_for = "\x01";  // the game and setting `copy` was found for
    std::thread worker;
    std::atomic<bool> busy{false};
    std::string working;  // the map being made; empty while listing
    // left by the worker
    std::mutex mutex;
    bool listed = false, made = false;
    std::vector<CollectionMap> found;
    std::string done_source, failed;
    fs::path model;
    CollectionMade outcome;
    ~OtherPicker() {
        if (worker.joinable())
            worker.join();
    }
};
OtherPicker other;

std::vector<const CollectionGameInfo*> other_games() {
    std::vector<const CollectionGameInfo*> out;
    for (const auto& g : collection_games()) {
        if (g.game != CollectionGame::kh2)
            out.push_back(&g);
    }
    return out;
}

// The replaced room's other game and its map's name (ddd, eh_01), when it is made from one.
const CollectionGameInfo* source_game(const RoomReplacement& r, std::string* map) {
    const auto colon = r.source.find(':');
    if (colon == std::string::npos)
        return nullptr;
    *map = r.source.substr(colon + 1);
    return collection_game(r.source.substr(0, colon));
}

// The collision drawn and picked: the one made when the modder's is shown
// (the game's when it is kept).
const RoomCollision& shown() {
    return m.rep.active && m.rep.view != 1 && m.rep.built_ok ? m.rep.built : m.collision;
}

bool showing_built() {
    return m.rep.active && m.rep.view != 1 && m.rep.built_ok;
}

// The names a face or material may be given: the room's own, then the surface tags.
std::vector<std::string> name_choices() {
    std::vector<std::string> out;
    for (const auto& n : m.collision.named)
        out.push_back(n.text());
    for (const char* t : {"nohit", "nocatch", "sea", "nocam", "slide", "sea2", "sicon"}) {
        if (std::find(out.begin(), out.end(), t) == out.end())
            out.push_back(t);
    }
    return out;
}

// The materials of the model the collision is made from, as the builder names them.
std::vector<std::string> collision_materials() {
    const assets::ImportedModel& model = m.rep.collision_source.empty() ? m.rep.raw : m.rep.raw_collision;
    std::vector<std::string> out;
    for (const auto& mesh : model.model.meshes) {
        const std::string name = !mesh.material_name.empty() ? mesh.material_name : mesh.texture_name;
        if (std::find(out.begin(), out.end(), name) == out.end())
            out.push_back(name);
    }
    return out;
}

fs::path nitrofs() {
    return khdays::vfs::data_root() / "nitrofs";
}

const Texture* texture(const GameModel& g, const std::string& name) {
    const std::string key = g.label + "|" + name;
    const auto found = m.textures.find(key);
    if (found != m.textures.end())
        return found->second.rgba.empty() ? nullptr : &found->second;
    Texture t;
    const auto file = g.textures.find(name);
    if (file != g.textures.end()) {
        const char* error = nullptr;
        std::uint8_t* px = khdays_png_read(u8(file->second).c_str(), &t.width, &t.height, &error);
        if (px != nullptr) {
            t.rgba.assign(px, px + static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4u);
            khdays_png_free(px);
        }
    }
    const auto it = m.textures.emplace(key, std::move(t)).first;
    return it->second.rgba.empty() ? nullptr : &it->second;
}

// The kind a named record is, by its name.
S named_kind(const std::string& name) {
    static const char* const tags[] = {"nohit", "nocatch", "sea", "nocam", "slide", "sea2", "sicon"};
    for (const char* t : tags) {
        if (name == t)
            return S::map_kind_tag;
    }
    if (name.rfind("gate", 0) == 0)
        return S::map_kind_gate;
    if (name.rfind("col_wall", 0) == 0)
        return S::map_kind_wall;
    return S::map_kind_other;
}

// What reads a surface tag, as found in the game's code (function names: not translated).
const char* tag_readers(const std::string& name) {
    if (name == "nohit")
        return "Ov022_SettlePointOnGround, Ov022_CastMove, Ov022_MayActOnHit, Ov022_StepChargeSequence";
    if (name == "nocatch")
        return "Ov022_FindClimbTarget";
    if (name == "sea" || name == "sea2")
        return "Ov022_StepGlideState";
    if (name == "slide")
        return "Ov022_StepGlideState, Ov022_StepJumpState";
    return nullptr;
}

std::string names_of(int list, const RoomFace& f) {
    std::string out;
    for (std::size_t p = 0; p < 4; ++p) {
        const auto s = f.slots[p];
        if (s == 0xff)
            continue;
        if (!slot_names(list, p)) {
            out += (out.empty() ? "" : ", ") + std::string("kind ") + std::to_string(s);
            continue;
        }
        if (s >= shown().named.size())
            continue;
        out += (out.empty() ? "" : ", ") + shown().named[s].text();
    }
    return out;
}

void frame_camera() {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& list : shown().faces) {
        for (const auto& f : list) {
            for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
                for (std::size_t c = 0; c < 3; ++c) {
                    lo[c] = std::min(lo[c], static_cast<float>(f.v[v][c]) * kFx);
                    hi[c] = std::max(hi[c], static_cast<float>(f.v[v][c]) * kFx);
                }
            }
        }
    }
    if (lo[0] > hi[0]) {  // no collision: around the models' nodes
        for (const auto& rm : m.models) {
            for (const auto& w : rm.game.rest) {
                for (std::size_t c = 0; c < 3; ++c) {
                    lo[c] = std::min(lo[c], w[12 + c]);
                    hi[c] = std::max(hi[c], w[12 + c]);
                }
            }
        }
    }
    if (lo[0] > hi[0])
        return;
    m.target = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    m.distance = std::max({hi[0] - lo[0], hi[2] - lo[2], 2.0f}) * 1.3f;
}

void open(App& app) {
    const Item& item = app.catalog.items[static_cast<std::size_t>(app.selected)];
    MapView fresh;
    fresh.item = app.selected;
    fresh.mod = app.mod;
    fresh.preview = std::move(m.preview);
    m = std::move(fresh);
    // `mi/wd/wd_<code>/room/<index>`
    const auto at = item.key.rfind("/room/");
    const std::string file = item.group;
    const std::string code = file.size() > 9 ? file.substr(9) : std::string();
    if (at == std::string::npos || !load_world(nitrofs(), code, &m.world, &m.error)) {
        if (m.error.empty())
            m.error = "not a room: " + item.key;
        return;
    }
    const int index = std::atoi(item.key.c_str() + at + 6);
    if (index < 0 || static_cast<std::size_t>(index) >= m.world.rooms.size()) {
        m.error = "no room " + std::to_string(index);
        return;
    }
    m.room = &m.world.rooms[static_cast<std::size_t>(index)];
    const auto& data = m.world.archive.entries[static_cast<std::size_t>(m.room->collision_member)].data;
    if (!data.empty()) {
        std::string why;
        m.has_collision = parse_collision(data, &m.collision, &why);
        if (!m.has_collision)
            m.error = why;
        else
            m.markers = markers(m.collision);
    }
    // the room's models: its objects, as the graphics dump names them
    const fs::path dump = resolve(app.settings, app.settings.graphics_dump);
    for (const auto& o : m.room->objects) {
        RoomModel rm;
        rm.member = o.member;
        if (o.member >= m.world.archive.entries.size() || m.world.archive.entries[o.member].data.empty()) {
            rm.problem = fmt(app.language, S::map_model_empty, {std::to_string(o.member)});
            m.models.push_back(std::move(rm));
            continue;
        }
        const std::string prefix = file + "#" + std::to_string(o.member) + "/mdl/";
        for (const auto& it : app.catalog.items) {
            if (it.kind == Kind::model && it.key.rfind(prefix, 0) == 0) {
                rm.key = it.key;
                break;
            }
        }
        std::string why;
        if (rm.key.empty()) {
            rm.problem = fmt(app.language, S::map_model_missing, {prefix});
        } else if (!load_game_model(nitrofs(), dump, rm.key, &rm.game, &why)) {
            rm.problem = rm.key + ": " + why;
        } else {
            std::vector<Mat> node_world;
            pose_game_model(rm.game, nullptr, 0.0f, &node_world, &rm.palette);
        }
        m.models.push_back(std::move(rm));
    }
    frame_camera();
    // the mod's replacement of this room, opened to change it
    if (const Mod* mod = app.current_mod()) {
        const fs::path change = change_file(mod->dir, m.world.code, index);
        std::error_code ec;
        if (fs::exists(change, ec)) {
            Replacing& p = m.rep;
            std::string why;
            if (!read_change(change, &p.r, &why)) {
                p.error = why;
                return;
            }
            p.active = p.from_mod = true;
            m.open_replace = true;
            const fs::path folder = change_models(mod->dir, m.world.code, index);
            p.model_source = folder / path_u8(p.r.model);
            if (!import_model(p.model_source, &p.raw, &why))
                p.error = why;
            if (!p.r.collision.empty()) {
                p.collision_source = folder / path_u8(p.r.collision);
                if (!import_model(p.collision_source, &p.raw_collision, &why))
                    p.error = why;
                std::string map = p.r.kh2;
                if (map.empty())
                    (void)source_game(p.r, &map);
                p.kh2_collision = !map.empty() && p.r.collision == map + "_collision.gltf";
            }
            p.dirty = p.error.empty();
        }
    }
}

// The modder's model placed again and its collision made, as the save will.
void rebuild() {
    Replacing& p = m.rep;
    p.dirty = false;
    p.built_ok = false;
    p.error.clear();
    p.warnings.clear();
    if (p.model_source.empty())
        return;
    p.placed = p.raw;
    place_model(&p.placed, p.r.place);
    p.placed_with = p.r.place;
    if (!m.has_collision) {
        p.error = "this room has no collision in the game's data to replace";
        return;
    }
    // only what lies around the room, as the save keeps it
    const RoomZone zone = p.r.zone >= 0 ? room_zone(m.collision, p.r.zone) : RoomZone{};
    if (p.r.zone >= 0)
        cut_to_zone(&p.placed, zone);
    p.surfaces = surface_map(p.placed);
    assets::ImportedModel collision = p.collision_source.empty() ? p.placed : p.raw_collision;
    if (!p.collision_source.empty()) {
        place_model(&collision, p.r.place);
        if (p.r.zone >= 0)
            cut_to_zone(&collision, zone);
    }
    if (p.r.keep_collision) {
        // only the look replaced: the game's own floor and walls are the ones shown and saved
        p.built = m.collision;
        p.report = CollisionReport{};
        p.built_ok = true;
        p.r.world = m.world.code;
        p.r.room = m.room->index;
        for (auto& more : sharing_warnings(m.world, p.r)) {
            if (more.kind != RoomWarning::collision_shared)
                p.warnings.push_back(std::move(more));
        }
        return;
    }
    CollisionRules rules;
    rules.ground_limit = p.r.ground_limit;
    rules.names_by_material = p.r.names_by_material;
    rules.names_by_triangle = p.r.names_by_triangle;
    rules.drop_original = p.r.drop_original;
    rules.markers = p.r.markers;
    if (!build_collision(collision_source(collision, p.collision_source.empty()), m.collision, rules, &p.built,
                         &p.report, &p.error)) {
        return;
    }
    const std::string problem = check_collision(p.built, m.collision);
    if (!problem.empty()) {
        p.error = "the collision made is not one the game reads: " + problem;
        return;
    }
    p.built_ok = true;
    p.warnings = p.report.warnings;
    p.r.world = m.world.code;
    p.r.room = m.room->index;
    for (auto& more : sharing_warnings(m.world, p.r))
        p.warnings.push_back(std::move(more));
}

std::array<float, 3> corner(const RoomFace& f, std::size_t v) {
    return {static_cast<float>(f.v[v][0]) * kFx, static_cast<float>(f.v[v][1]) * kFx,
            static_cast<float>(f.v[v][2]) * kFx};
}

std::array<float, 3> face_tint(int list, const RoomFace& f) {
    bool gate = false, wall = false, tag = false;
    for (std::size_t p = 0; p < 4; ++p) {
        const auto s = f.slots[p];
        if (s == 0xff || s >= shown().named.size() || !slot_names(list, p))
            continue;
        const S kind = named_kind(shown().named[s].text());
        gate = gate || kind == S::map_kind_gate;
        wall = wall || kind == S::map_kind_wall;
        tag = tag || kind == S::map_kind_tag;
    }
    if (gate)
        return {0.95f, 0.85f, 0.2f};
    if (wall)
        return {0.9f, 0.3f, 0.9f};
    if (tag)
        return {0.3f, 0.9f, 0.9f};
    return list == kGround ? std::array<float, 3>{0.35f, 0.85f, 0.35f}
           : list == kWall ? std::array<float, 3>{0.95f, 0.55f, 0.25f}
                           : std::array<float, 3>{0.4f, 0.55f, 1.0f};
}

// Where a marker stands on the modder's room: on one of its floors, over it with nothing near above, or under
// one of its surfaces (in it); `to` the point of that floor or surface over or under it.
enum class Footing { none, on, floating, under };

Footing footing(const std::array<float, 3>& q, std::array<float, 3>* to) {
    const Replacing& p = m.rep;
    // the marker in the room as it was placed (the model may be dragged since)
    const auto b = placed_point(p.placed_with, unplaced_point(p.r.place, q));
    float below = -1e30f, above = 1e30f;
    for (const auto& h : p.surfaces.heights_at(b[0], b[2])) {
        if (h.up && h.y <= b[1] + 0.05f)
            below = std::max(below, h.y);
        if (h.y > b[1] + 0.05f)
            above = std::min(above, h.y);
    }
    const bool has_below = below > -1e29f, has_above = above < 1e29f;
    auto live = [&](float y) { return placed_point(p.r.place, unplaced_point(p.placed_with, {b[0], y, b[2]})); };
    if (has_below && b[1] - below <= 0.35f) {
        *to = live(below);
        return Footing::on;
    }
    if (has_above && (!has_below || above - b[1] < 2.0f)) {
        *to = live(above);
        return Footing::under;
    }
    if (has_below) {
        *to = live(below);
        return Footing::floating;
    }
    return Footing::none;
}

// Where vertex v is drawn when its joints are moved as `moved` says (palette entry: matrix); as it is
// when none of its joints moves.
std::array<float, 3> skinned(const std::map<std::uint32_t, Mat>& moved, const assets::NeutralVertex& v) {
    std::array<float, 3> pos{0, 0, 0};
    float total = 0.0f;
    for (std::size_t k = 0; k < 4; ++k) {
        const auto found = moved.find(v.joints[k]);
        if (v.weights[k] <= 0.0f || found == moved.end())
            continue;
        const auto q = mat_point(found->second, v.position);
        for (std::size_t c = 0; c < 3; ++c)
            pos[c] += v.weights[k] * q[c];
        total += v.weights[k];
    }
    if (total <= 0.0f)
        return v.position;
    for (auto& c : pos)
        c /= total;
    return pos;
}

void draw_room(App& app, ImVec2 size) {
    if (!m.preview)
        m.preview = std::make_unique<Preview>(app.device);
    Preview& p = *m.preview;
    p.collect();
    const int w = std::max(64, static_cast<int>(size.x)), h = std::max(64, static_cast<int>(size.y));
    const Mat vp =
        orbit_view_projection(m.target, m.yaw, m.pitch, m.distance, static_cast<float>(w) / static_cast<float>(h));
    p.begin(w, h, vp, orbit_eye_direction(m.yaw, m.pitch));

    const bool yours = m.rep.active && m.rep.view != 1 && !m.rep.model_source.empty();
    const bool games = !yours || m.rep.view == 2;
    if (m.show_models) {
        const auto uses = m.rep.active ? object_uses(*m.room, m.rep.r) : std::vector<std::pair<int, ObjectUse>>{};
        for (const auto& rm : m.models) {
            if (!rm.problem.empty())
                continue;
            bool kept = games;
            for (const auto& [member, use] : uses)
                kept = kept || (member == rm.member && use == ObjectUse::keep);
            if (!kept)
                continue;
            for (const auto& mesh : rm.game.model.meshes) {
                if (mesh.material_alpha <= 0.0f)
                    continue;  // the DS draws nothing of it
                const Texture* t = mesh.texture_name.empty() ? nullptr : texture(rm.game, mesh.texture_name);
                PreviewImage image;
                if (t != nullptr)
                    image = {t->rgba.data(), t->width, t->height, mesh.repeat, mesh.mirror};
                const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
                const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
                auto vertex = [&](const assets::NeutralVertex& v) {
                    PreviewVertex o;
                    std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
                    for (int k = 0; k < 4; ++k) {
                        const float wgt = v.weights[static_cast<std::size_t>(k)];
                        const std::uint32_t jt = v.joints[static_cast<std::size_t>(k)];
                        if (wgt <= 0.0f || jt >= rm.palette.size())
                            continue;
                        const auto q = mat_point(rm.palette[jt], v.position);
                        const auto n = mat_vec(rm.palette[jt], v.normal);
                        for (std::size_t c = 0; c < 3; ++c)
                            pos[c] += wgt * q[c], nrm[c] += wgt * n[c];
                    }
                    o.position = pos;
                    o.normal = nrm;
                    o.uv = {v.texcoord[0] / tw, v.texcoord[1] / th};
                    // the vertex colour the room is lit with, its material's alpha
                    o.colour = {v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f,
                                v.color[3] / 255.0f * mesh.material_alpha};
                    return o;
                };
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                               vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
                }
            }
        }
        if (yours) {
            // the modder's, placed: as the port draws it (its vertex colours, its textures), moving by itself
            // as the port moves it, by the studio's clock at 60 frames a second
            const assets::ImportedModel& placed = m.rep.placed;
            // while its scale, turn or move is dragged: shown where the place now puts it
            const bool moving = !same_place(m.rep.placed_with, m.rep.r.place);
            const auto frames = static_cast<std::uint64_t>(SDL_GetTicks() * 60 / 1000);
            std::map<std::uint32_t, Mat> moved;  // a joint's palette entry: its node now times its inverse bind
            if (!placed.joints.empty()) {
                const auto worlds =
                    khdays::room::node_worlds(placed.nodes, placed.animations, static_cast<double>(frames) / 60.0);
                for (const auto& j : placed.joints) {
                    const bool on_node = j.node >= 0 && static_cast<std::size_t>(j.node) < worlds.size();
                    moved[j.palette_index] =
                        mat_mul(on_node ? worlds[static_cast<std::size_t>(j.node)] : j.world, j.inverse_bind);
                }
            }
            // where a vertex is drawn as the room is placed (its sky put about the viewer below)
            const auto drawn_at = [&](const assets::NeutralVertex& v) {
                auto at = skinned(moved, v);
                return moving ? placed_point(m.rep.r.place, unplaced_point(m.rep.placed_with, at)) : at;
            };
            std::vector<std::array<float, 3>> sky_points;
            for (const auto& mesh : placed.model.meshes)
                for (const auto& v : mesh.vertices)
                    if (mesh.sky && m.show_sky)
                        sky_points.push_back(drawn_at(v));
            const Sky sky(sky_points);
            const auto eye_dir = orbit_eye_direction(m.yaw, m.pitch);
            const std::array<float, 3> eye{m.target[0] + eye_dir[0] * m.distance, m.target[1] + eye_dir[1] * m.distance,
                                           m.target[2] + eye_dir[2] * m.distance};
            for (const auto& mesh : placed.model.meshes) {
                if (mesh.sky && !m.show_sky)
                    continue;  // from outside, its sky hides it
                std::string shown = mesh.texture_name;
                if (!mesh.texture_frames.empty()) {
                    shown = mesh.texture_frames[khdays::room::frame_at(mesh.texture_frames, mesh.texture_frames_loop,
                                                                       mesh.texture_frames_loop_from, frames)]
                                .first;
                }
                const assets::DecodedTexture* t = nullptr;
                for (const auto& [name, decoded] : placed.textures) {
                    if (name == shown && !decoded.rgba.empty())
                        t = &decoded;
                }
                PreviewImage image;
                if (t != nullptr)
                    image = {t->rgba.data(), t->width, t->height};
                const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
                const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
                const auto offset = khdays::room::scroll_offset(mesh.scroll, frames, tw, th);
                auto vertex = [&](const assets::NeutralVertex& v) {
                    PreviewVertex o;
                    o.position = drawn_at(v);
                    if (mesh.sky)
                        o.position = sky.at(o.position, eye, m.distance);  // as the game draws it
                    o.normal = v.normal;
                    o.uv = {(v.texcoord[0] + offset[0]) / tw, (v.texcoord[1] + offset[1]) / th};
                    o.colour = {v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f, v.color[3] / 255.0f};
                    return o;
                };
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                               vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
                }
            }
        }
    }
    const bool show[3] = {m.show_ground, m.show_walls, m.show_ceilings};
    // over the models: see-through, brought a little towards the viewer so
    // the surface it follows does not show through it in stripes
    const float alpha = m.show_models ? 0.5f : 1.0f;
    const auto eye_dir = orbit_eye_direction(m.yaw, m.pitch);
    const std::array<float, 3> eye{m.target[0] + eye_dir[0] * m.distance, m.target[1] + eye_dir[1] * m.distance,
                                   m.target[2] + eye_dir[2] * m.distance};
    const float lift = m.show_models ? m.distance * 0.002f : 0.0f;
    for (int l = 0; l < 3; ++l) {
        if (!show[l])
            continue;
        const auto& faces = shown().faces[static_cast<std::size_t>(l)];
        for (std::size_t j = 0; j < faces.size(); ++j) {
            const RoomFace& f = faces[j];
            if (f.corners < 3)
                continue;
            std::array<float, 3> tint = face_tint(l, f);
            if (l == m.picked_list && static_cast<int>(j) == m.picked_face)
                tint = {1, 1, 1};
            // the face's normal from its corners, for the light
            const auto a = corner(f, 0), b = corner(f, 1), c = corner(f, 2);
            const std::array<float, 3> e0{b[0] - a[0], b[1] - a[1], b[2] - a[2]},
                e1{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const std::array<float, 3> n{e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                                         e0[0] * e1[1] - e0[1] * e1[0]};
            auto vertex = [&](std::size_t v) {
                PreviewVertex o;
                o.position = corner(f, v);
                const std::array<float, 3> to{eye[0] - o.position[0], eye[1] - o.position[1], eye[2] - o.position[2]};
                const float d = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
                if (d > 0.0f) {
                    for (std::size_t k = 0; k < 3; ++k)
                        o.position[k] += to[k] / d * lift;
                }
                o.normal = n;
                o.colour[3] = alpha;
                return o;
            };
            // a face's corners turn so that (v1-v0)x(v2-v0) points to the side the
            // player stands on (4119 of the 4124 grounds up, every ceiling down):
            // seen from behind, it is left out, as a level editor shows a room
            p.triangle(vertex(0), vertex(1), vertex(2), PreviewImage{}, true, tint);
            if (f.corners == 4)
                p.triangle(vertex(0), vertex(2), vertex(3), PreviewImage{}, true, tint);
        }
    }

    SDL_GPUTexture* tex = p.finish();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (tex != nullptr) {
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(tex))),
                     ImVec2(static_cast<float>(w), static_cast<float>(h)));
    } else {
        ImGui::Dummy(ImVec2(static_cast<float>(w), static_cast<float>(h)));
    }
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto project = [&](const std::array<float, 3>& q, float* x, float* y) {
        if (!p.project(q, x, y))
            return false;
        *x += at.x;
        *y += at.y;
        return true;
    };

    // the markers: a dot and the way it faces; over the modder's room, coloured by where each stands on it
    // (green on its floor, yellow over it, red under or in it) with a line to that floor or surface
    int near_marker = -1;
    float best = 9.0f * ImGui::GetStyle().FontScaleDpi;
    const std::vector<Marker> shown_markers = showing_built() ? markers(m.rep.built) : m.markers;
    const bool footings = yours && !m.rep.surfaces.triangles.empty();
    const ImU32 footing_colour[4] = {IM_COL32(170, 170, 170, 255), IM_COL32(80, 220, 100, 255),
                                     IM_COL32(255, 210, 60, 255), IM_COL32(255, 60, 60, 255)};
    std::array<int, 4> counted{0, 0, 0, 0};
    if (m.show_markers) {
        for (std::size_t k = 0; k < shown_markers.size(); ++k) {
            const Marker& mk = shown_markers[k];
            const std::array<float, 3> q{static_cast<float>(mk.place[0]) * kFx, static_cast<float>(mk.place[1]) * kFx,
                                         static_cast<float>(mk.place[2]) * kFx};
            ImU32 colour = IM_COL32(255, 60, 60, 255);
            std::array<float, 3> to{};
            Footing stands = Footing::none;
            if (footings) {
                stands = footing(q, &to);
                colour = footing_colour[static_cast<int>(stands)];
                ++counted[static_cast<std::size_t>(stands)];
            }
            float x, y;
            if (!project(q, &x, &y))
                continue;
            float tx, ty;
            if ((stands == Footing::floating || stands == Footing::under) && project(to, &tx, &ty)) {
                list->AddLine(ImVec2(x, y), ImVec2(tx, ty), colour, 2.0f);
                list->AddCircle(ImVec2(tx, ty), 3.0f, colour, 0, 1.5f);
            }
            const float turn = static_cast<float>(mk.extra) / 65536.0f * 6.2831853f;
            float fx, fy;
            if (project({q[0] + std::sin(turn), q[1], q[2] + std::cos(turn)}, &fx, &fy)) {
                list->AddLine(ImVec2(x, y), ImVec2(fx, fy), (colour & 0x00ffffffu) | 0xdc000000u, 2.0f);
            }
            const bool picked = static_cast<int>(k) == m.picked_marker;
            list->AddCircleFilled(ImVec2(x, y), picked ? 6.0f : 4.0f, picked ? IM_COL32(255, 255, 255, 255) : colour);
            const float d = std::hypot(x - mouse.x, y - mouse.y);
            if (hovered && d < best)
                best = d, near_marker = static_cast<int>(k);
        }
    }
    if (footings && m.show_markers && !shown_markers.empty()) {
        // how many stand where, in the picture's corner
        const S says[3] = {S::map_markers_on, S::map_markers_over, S::map_markers_under};
        ImVec2 pen(at.x + 8.0f, at.y + 6.0f);
        for (int s = 0; s < 3; ++s) {
            const std::string text =
                fmt(app.language, says[s], {std::to_string(counted[static_cast<std::size_t>(s + 1)])});
            list->AddCircleFilled(ImVec2(pen.x + 5.0f, pen.y + ImGui::GetFontSize() * 0.5f), 5.0f,
                                  footing_colour[s + 1]);
            list->AddText(ImVec2(pen.x + 14.0f, pen.y), IM_COL32(235, 235, 235, 255), text.c_str());
            pen.x += 14.0f + ImGui::CalcTextSize(text.c_str()).x + 16.0f;
        }
    }
    if (near_marker >= 0)
        ImGui::SetTooltip("%s", shown_markers[static_cast<std::size_t>(near_marker)].name.c_str());
    // a mission's points in this room: spawners green, the players' starts blue, chests gold, emblems
    // pink, labels on hover
    const MissionOverlay& mo = mission_overlay();
    if (mo.world == m.world.code && mo.room == m.room->index) {
        for (const auto& pt : mo.points) {
            float x, y;
            if (!project({static_cast<float>(pt.at[0]) * kFx, static_cast<float>(pt.at[1]) * kFx,
                          static_cast<float>(pt.at[2]) * kFx},
                         &x, &y))
                continue;
            static const ImU32 colors[] = {IM_COL32(80, 255, 120, 255), IM_COL32(90, 170, 255, 255),
                                           IM_COL32(255, 200, 60, 255), IM_COL32(255, 110, 210, 255)};
            list->AddCircle(ImVec2(x, y), 7.0f, colors[pt.kind], 0, 2.5f);
            if (hovered && std::hypot(x - mouse.x, y - mouse.y) < 9.0f)
                ImGui::SetTooltip("%s", pt.label.c_str());
        }
    }
    // the exits' models (the world's gate models of this room's links), where the change puts them
    if (m.show_markers) {
        for (const auto& link : m.room->links) {
            for (const auto& g : m.world.gate_models) {
                if (g.key != static_cast<std::int8_t>(link.key))
                    continue;
                for (std::size_t part = 0; part < 5; ++part) {
                    if (g.parts[part].member == 0)
                        continue;
                    auto point = g.parts[part].position;
                    const auto moved = m.rep.r.gate_points.find({g.key, static_cast<int>(part)});
                    if (m.rep.active && moved != m.rep.r.gate_points.end())
                        point = moved->second;
                    float x, y;
                    if (!project({static_cast<float>(point[0]) * kFx, static_cast<float>(point[1]) * kFx,
                                  static_cast<float>(point[2]) * kFx},
                                 &x, &y)) {
                        continue;
                    }
                    const float r = 5.0f;
                    list->AddQuadFilled(ImVec2(x, y - r), ImVec2(x + r, y), ImVec2(x, y + r), ImVec2(x - r, y),
                                        IM_COL32(255, 210, 40, 230));
                }
            }
        }
    }

    // a click picks the nearest marker, else the nearest face under the pointer
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f) {
        m.picked_marker = near_marker;
        m.picked_list = m.picked_face = -1;
        if (near_marker < 0) {
            float nearest = 1e30f;
            for (int l = 0; l < 3; ++l) {
                if (!show[l])
                    continue;
                const auto& faces = shown().faces[static_cast<std::size_t>(l)];
                for (std::size_t j = 0; j < faces.size(); ++j) {
                    const RoomFace& f = faces[j];
                    float xs[4], ys[4];
                    bool ok = true;
                    for (std::size_t v = 0; v < f.corners && v < 4 && ok; ++v)
                        ok = project(corner(f, v), &xs[v], &ys[v]);
                    if (!ok || f.corners < 3)
                        continue;
                    // not seen from behind (as drawn)
                    const auto a = corner(f, 0), b = corner(f, 1), c2 = corner(f, 2);
                    const std::array<float, 3> e0{b[0] - a[0], b[1] - a[1], b[2] - a[2]},
                        e1{c2[0] - a[0], c2[1] - a[1], c2[2] - a[2]};
                    const std::array<float, 3> n{e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                                                 e0[0] * e1[1] - e0[1] * e1[0]};
                    if (n[0] * (eye[0] - a[0]) + n[1] * (eye[1] - a[1]) + n[2] * (eye[2] - a[2]) <= 0.0f)
                        continue;
                    // inside the projected polygon (either winding)
                    int sign = 0;
                    bool inside = true;
                    for (std::size_t v = 0; v < f.corners && inside; ++v) {
                        const std::size_t u = (v + 1) % f.corners;
                        const float cross = (xs[u] - xs[v]) * (mouse.y - ys[v]) - (ys[u] - ys[v]) * (mouse.x - xs[v]);
                        const int s = cross > 0 ? 1 : cross < 0 ? -1 : 0;
                        if (s != 0 && sign != 0 && s != sign)
                            inside = false;
                        if (s != 0)
                            sign = s;
                    }
                    if (!inside)
                        continue;
                    std::array<float, 3> centre{0, 0, 0};
                    for (std::size_t v = 0; v < f.corners; ++v) {
                        const auto q = corner(f, v);
                        for (std::size_t c = 0; c < 3; ++c)
                            centre[c] += q[c] / static_cast<float>(f.corners);
                    }
                    const float d = std::hypot(centre[0] - eye[0], centre[1] - eye[1], centre[2] - eye[2]);
                    if (d < nearest)
                        nearest = d, m.picked_list = l, m.picked_face = static_cast<int>(j);
                }
            }
        }
    } else if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        m.yaw -= d.x * 0.4f;
        m.pitch = std::clamp(m.pitch + d.y * 0.4f, -85.0f, 85.0f);
    }
    orbit_move(m.target, m.yaw, m.pitch, m.distance, static_cast<float>(h), hovered);
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        m.distance *= std::pow(0.9f, ImGui::GetIO().MouseWheel);
}

void draw_details(App& app) {
    const auto lang = app.language;
    const std::vector<Marker> shown_markers = showing_built() ? markers(m.rep.built) : m.markers;
    if (m.picked_marker >= static_cast<int>(shown_markers.size()))
        m.picked_marker = -1;
    if (m.picked_marker >= 0 && showing_built()) {
        // moved on the new room: where the change puts it
        Replacing& p = m.rep;
        const Marker& mk = shown_markers[static_cast<std::size_t>(m.picked_marker)];
        float at[3] = {static_cast<float>(mk.place[0]) * kFx, static_cast<float>(mk.place[1]) * kFx,
                       static_cast<float>(mk.place[2]) * kFx};
        float facing = static_cast<float>(mk.extra) * 360.0f / 65536.0f;
        bool moved = false;
        ImGui::TextUnformatted(mk.name.c_str());
        if (mk.name.rfind("pent", 0) == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(lang, S::map_marker_entry));
        }
        ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
        ImGui::DragFloat3(tr(lang, S::map_marker_place), at, 0.02f, -100000.0f, 100000.0f, "%.2f");
        moved |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
        ImGui::DragFloat(tr(lang, S::map_marker_facing), &facing, 1.0f, -360.0f, 360.0f, "%.0f");
        moved |= ImGui::IsItemDeactivatedAfterEdit();
        same_line_if_room(button_width(tr(lang, S::map_to_ground)));
        if (ImGui::SmallButton(tr(lang, S::map_to_ground))) {
            double y = 0;
            if (ground_under(p.built, at[0] * 4096.0, at[2] * 4096.0, at[1] * 4096.0 + 4096.0, &y) ||
                ground_under(p.built, at[0] * 4096.0, at[2] * 4096.0, 1e18, &y)) {
                at[1] = static_cast<float>(y / 4096.0 + 0.1);  // as the game's sit, a tenth above
                moved = true;
            }
        }
        if (moved) {
            p.r.markers[mk.name] = {
                static_cast<std::int32_t>(std::lround(at[0] * 4096.0)),
                static_cast<std::int32_t>(std::lround(at[1] * 4096.0)),
                static_cast<std::int32_t>(std::lround(at[2] * 4096.0)),
                static_cast<std::int32_t>(std::lround(std::fmod(facing + 360.0f, 360.0f) * 65536.0f / 360.0f))};
            p.dirty = true;
        }
    } else if (m.picked_marker >= 0) {
        const Marker& mk = m.markers[static_cast<std::size_t>(m.picked_marker)];
        ImGui::TextUnformatted(fmt(lang, S::map_marker_picked,
                                   {mk.name, std::to_string(static_cast<float>(mk.place[0]) * kFx),
                                    std::to_string(static_cast<float>(mk.place[1]) * kFx),
                                    std::to_string(static_cast<float>(mk.place[2]) * kFx),
                                    std::to_string(static_cast<double>(mk.extra) * 360.0 / 65536.0)})
                                   .c_str());
        if (mk.name.rfind("pent", 0) == 0)
            ImGui::TextDisabled("%s", tr(lang, S::map_marker_entry));
    } else if (m.picked_face >= 0) {
        const RoomFace& f =
            shown().faces[static_cast<std::size_t>(m.picked_list)][static_cast<std::size_t>(m.picked_face)];
        const S list = m.picked_list == kGround ? S::map_grounds
                       : m.picked_list == kWall ? S::map_walls
                                                : S::map_ceilings;
        const std::string names = names_of(m.picked_list, f);
        ImGui::TextUnformatted(fmt(lang, S::map_face_picked,
                                   {tr(lang, list), std::to_string(m.picked_face), std::to_string(f.corners),
                                    names.empty() ? std::string("-") : names})
                                   .c_str());
        if (showing_built()) {
            // the face of the new room: its triangles take a name
            Replacing& p = m.rep;
            const auto& lists = p.report.face_triangles[static_cast<std::size_t>(m.picked_list)];
            const std::vector<std::size_t> triangles = static_cast<std::size_t>(m.picked_face) < lists.size()
                                                           ? lists[static_cast<std::size_t>(m.picked_face)]
                                                           : std::vector<std::size_t>{};
            if (triangles.empty()) {
                ImGui::TextDisabled("%s", tr(lang, S::map_face_kept));
            } else {
                ImGui::TextDisabled("%s", fmt(lang, S::map_face_made, {std::to_string(triangles.size())}).c_str());
                const auto choices = name_choices();
                ImGui::SetNextItemWidth(10 * ImGui::GetFontSize());
                if (ImGui::BeginCombo(tr(lang, S::map_give_name), tr(lang, S::map_add_name))) {
                    for (const auto& c : choices) {
                        if (ImGui::Selectable(c.c_str())) {
                            for (const auto t : triangles) {
                                auto& given = p.r.names_by_triangle[t];
                                if (std::find(given.begin(), given.end(), c) == given.end())
                                    given.push_back(c);
                            }
                            p.dirty = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                same_line_if_room(button_width(tr(lang, S::map_clear_names)));
                if (ImGui::SmallButton(tr(lang, S::map_clear_names))) {
                    for (const auto t : triangles)
                        p.r.names_by_triangle.erase(t);
                    p.dirty = true;
                }
            }
        }
    } else {
        ImGui::TextDisabled("%s", tr(lang, S::map_pick_hint));
    }
}

void draw_lists(App& app) {
    const auto lang = app.language;
    if (ImGui::CollapsingHeader(fmt(lang, S::map_named_list, {std::to_string(shown().named.size())}).c_str())) {
        for (std::size_t k = 0; k < shown().named.size(); ++k) {
            const std::string name = shown().named[k].text();
            int on[3] = {0, 0, 0};
            for (int l = 0; l < 3; ++l) {
                for (const auto& f : shown().faces[static_cast<std::size_t>(l)]) {
                    for (std::size_t p = 0; p < 4; ++p)
                        on[l] += f.slots[p] == k && slot_names(l, p) ? 1 : 0;
                }
            }
            ImGui::BulletText("%s", fmt(lang, S::map_named_row,
                                        {name, tr(lang, named_kind(name)), std::to_string(on[0]), std::to_string(on[1]),
                                         std::to_string(on[2])})
                                        .c_str());
            if (const char* readers = tag_readers(name)) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", fmt(lang, S::map_read_by, {readers}).c_str());
            } else if (named_kind(name) == S::map_kind_tag || named_kind(name) == S::map_kind_other) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", tr(lang, S::map_not_read));
            }
        }
    }
    if (ImGui::CollapsingHeader(fmt(lang, S::map_markers_list, {std::to_string(m.markers.size())}).c_str())) {
        for (std::size_t k = 0; k < m.markers.size(); ++k) {
            const Marker& mk = m.markers[k];
            if (ImGui::Selectable((mk.name + "##mk" + std::to_string(k)).c_str(),
                                  static_cast<int>(k) == m.picked_marker)) {
                m.picked_marker = static_cast<int>(k);
                m.picked_face = m.picked_list = -1;
            }
        }
    }
    if (ImGui::CollapsingHeader(fmt(lang, S::map_exits_list, {std::to_string(m.room->links.size())}).c_str())) {
        // the row's target as the data holds it: which room it names is not read yet
        for (const auto& l : m.room->links) {
            ImGui::BulletText("%s",
                              fmt(lang, S::map_exit_row, {std::to_string(l.key), std::to_string(l.target)}).c_str());
        }
    }
}

// The names (the original's exits and walls that open, the materials' names) and the exits' models.
void draw_names_and_exits(App& app) {
    const auto lang = app.language;
    Replacing& p = m.rep;
    // names: the original's exits and walls that open, the materials' names
    if (ImGui::TreeNode(tr(lang, S::map_names_title))) {
        ImGui::TextWrapped("%s", tr(lang, S::map_names_hint));
        for (std::size_t k = 0; k < m.collision.named.size(); ++k) {
            const std::string name = m.collision.named[k].text();
            const S kind = named_kind(name);
            if (kind != S::map_kind_gate && kind != S::map_kind_wall)
                continue;
            bool yours = false;
            for (const auto& [material, names] : p.r.names_by_material)
                yours = yours || std::find(names.begin(), names.end(), name) != names.end();
            for (const auto& [t, names] : p.r.names_by_triangle)
                yours = yours || std::find(names.begin(), names.end(), name) != names.end();
            if (yours) {
                ImGui::BulletText("%s", fmt(lang, S::map_on_your_faces, {name}).c_str());
                continue;
            }
            bool keep = p.r.drop_original.count(name) == 0;
            if (ImGui::Checkbox(fmt(lang, S::map_keep_original, {name}).c_str(), &keep)) {
                if (keep)
                    p.r.drop_original.erase(name);
                else
                    p.r.drop_original.insert(name);
                p.dirty = true;
            }
        }
        const auto choices = name_choices();
        for (const auto& material : collision_materials()) {
            ImGui::PushID(material.c_str());
            auto& names = p.r.names_by_material[material];
            std::string list;
            for (const auto& n : names)
                list += (list.empty() ? "" : ", ") + n;
            ImGui::TextUnformatted(fmt(lang, S::map_material_names,
                                       {material.empty() ? std::string("-") : material,
                                        list.empty() ? std::string(tr(lang, S::map_none)) : list})
                                       .c_str());
            same_line_if_room(9 * ImGui::GetFontSize());
            ImGui::SetNextItemWidth(9 * ImGui::GetFontSize());
            if (ImGui::BeginCombo("##add", tr(lang, S::map_add_name))) {
                for (const auto& c : choices) {
                    if (ImGui::Selectable(c.c_str()) && std::find(names.begin(), names.end(), c) == names.end()) {
                        names.push_back(c);
                        p.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (!names.empty()) {
                same_line_if_room(button_width(tr(lang, S::map_clear_names)));
                if (ImGui::SmallButton(tr(lang, S::map_clear_names))) {
                    names.clear();
                    p.dirty = true;
                }
            }
            if (names.empty())
                p.r.names_by_material.erase(material);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // the exits' models of this room: where they stand
    if (ImGui::TreeNode(tr(lang, S::map_gate_models))) {
        for (const auto& link : m.room->links) {
            for (const auto& g : m.world.gate_models) {
                if (g.key != static_cast<std::int8_t>(link.key))
                    continue;
                for (std::size_t part = 0; part < 5; ++part) {
                    if (g.parts[part].member == 0)
                        continue;
                    const std::pair<int, int> id{g.key, static_cast<int>(part)};
                    const auto moved = p.r.gate_points.find(id);
                    const auto at = moved != p.r.gate_points.end() ? moved->second : g.parts[part].position;
                    float v[3] = {static_cast<float>(at[0]) * kFx, static_cast<float>(at[1]) * kFx,
                                  static_cast<float>(at[2]) * kFx};
                    ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
                    if (ImGui::DragFloat3(
                            fmt(lang, S::map_gate_part,
                                {std::to_string(g.key), std::to_string(part), std::to_string(g.parts[part].member)})
                                .c_str(),
                            v, 0.02f, -100000.0f, 100000.0f, "%.2f")) {
                        p.r.gate_points[id] = {static_cast<std::int32_t>(std::lround(v[0] * 4096.0)),
                                               static_cast<std::int32_t>(std::lround(v[1] * 4096.0)),
                                               static_cast<std::int32_t>(std::lround(v[2] * 4096.0))};
                    }
                }
            }
        }
        ImGui::TreePop();
    }
}

// The size of what a model spans, words for a person: "12.4 wide, 3.1 high, 9.0 deep".
std::string span_text(App& app, const std::array<float, 3>& lo, const std::array<float, 3>& hi) {
    char w[32], h[32], d[32];
    std::snprintf(w, sizeof(w), "%.1f", hi[0] - lo[0]);
    std::snprintf(h, sizeof(h), "%.1f", hi[1] - lo[1]);
    std::snprintf(d, sizeof(d), "%.1f", hi[2] - lo[2]);
    return fmt(app.language, S::map_span, {w, h, d});
}

// The room's own points (its models at rest, as drawn): what a model made from it is lined up with.
std::vector<std::array<float, 3>> game_points() {
    std::vector<std::array<float, 3>> out;
    for (const auto& rm : m.models) {
        if (!rm.problem.empty())
            continue;
        for (const auto& mesh : rm.game.model.meshes) {
            for (const auto& v : mesh.vertices) {
                std::array<float, 3> pos{0, 0, 0};
                for (int k = 0; k < 4; ++k) {
                    const float wgt = v.weights[static_cast<std::size_t>(k)];
                    const std::uint32_t jt = v.joints[static_cast<std::size_t>(k)];
                    if (wgt <= 0.0f || jt >= rm.palette.size())
                        continue;
                    const auto qp = mat_point(rm.palette[jt], v.position);
                    for (std::size_t c = 0; c < 3; ++c)
                        pos[c] += wgt * qp[c];
                }
                out.push_back(pos);
            }
        }
    }
    return out;
}

// The KH2 map this room was made from (the studio's pairs, then the mod's), or null.
const Kh2Pair* room_pair(App& app) {
    if (kh2.pairs_for != app.mod) {
        const Mod* mod = app.current_mod();
        std::vector<std::string> notes;
        kh2.pairs = kh2_pairs(mod != nullptr ? mod->dir : fs::path(), &notes);
        kh2.pairs_for = app.mod;
        for (const auto& note : notes)
            app.tell(note, false);
    }
    return m.room != nullptr ? kh2_pair_of(kh2.pairs, m.world.code, m.room->index) : nullptr;
}

// The modder's model lined up with the room when it is made from it, and the person told what was done.
void line_up(App& app) {
    Replacing& p = m.rep;
    if (p.raw.model.meshes.empty())
        return;
    // the KH2 map this room was made from, where Days put it
    const Kh2Pair* pair = !p.r.kh2.empty() ? room_pair(app) : nullptr;
    if (pair != nullptr && pair->placed && pair->map == p.r.kh2) {
        p.r.place = RoomPlacement{};
        p.r.place.scale = 0.01f;
        p.r.place.turn = {0, pair->turn_y, 0};
        p.r.place.move = pair->move;
        char turn[32], move[96];
        std::snprintf(turn, sizeof(turn), "%g", pair->turn_y);
        std::snprintf(move, sizeof(move), "%.2f %.2f %.2f", pair->move[0], pair->move[1], pair->move[2]);
        p.placed_text = fmt(app.language, S::map_align_pair, {turn, move});
        p.placed_ok = true;
        p.dirty = true;
        return;
    }
    // another game's room: in its game's units, by its own floors when it has them
    std::string map;
    const CollectionGameInfo* game = source_game(p.r, &map);
    RoomAlignment a;
    std::pair<std::size_t, std::size_t> on_floors{0, 0};
    if (game != nullptr && game->units > 0 && m.has_collision) {
        assets::ImportedModel floors;
        std::string why;
        std::error_code ec;
        const fs::path own = collection_made_collision(app.settings, *game, map);
        const bool own_floors = fs::is_regular_file(own, ec) && import_model(own, &floors, &why);
        a = kh2_alignment(game_points(), m.collision, own_floors ? floors : p.raw, game->units);
        if (a.verdict == RoomAlignment::kh2_units)
            on_floors = onto_floors(own_floors ? floors : p.raw, m.collision, &a);
    } else {
        a = !p.r.kh2.empty() && m.has_collision ? kh2_alignment(game_points(), m.collision, p.raw)
                                                : align_with_room(game_points(), p.raw);
    }
    char share[16];
    std::snprintf(share, sizeof(share), "%.0f", a.share * 100.0);
    if (a.verdict == RoomAlignment::kh2_units && game != nullptr) {
        p.r.place = a.place;
        char scale[32];
        std::snprintf(scale, sizeof(scale), "%g", a.place.scale);
        p.placed_text = fmt(app.language, S::map_align_units,
                            {game->name, scale, std::to_string(on_floors.first), std::to_string(on_floors.second)});
        p.placed_ok = on_floors.first * 2 >= on_floors.second;
    } else if (a.verdict == RoomAlignment::kh2_units) {
        p.r.place = a.place;
        char scale[32];
        std::snprintf(scale, sizeof(scale), "%g", a.place.scale);
        p.placed_text = fmt(app.language, S::map_align_kh2, {scale});
        p.placed_ok = true;
    } else if (a.verdict == RoomAlignment::turned) {
        p.r.place = a.place;
        char turn[64], scale[32];
        std::snprintf(turn, sizeof(turn), "%.0f, %.0f, %.0f", a.place.turn[0], a.place.turn[1], a.place.turn[2]);
        std::snprintf(scale, sizeof(scale), "%g", a.place.scale);
        p.placed_text = fmt(app.language, S::map_align_turned, {turn, scale, share});
        p.placed_ok = true;
    } else if (a.verdict == RoomAlignment::in_place) {
        p.r.place = RoomPlacement{};
        p.placed_text = fmt(app.language, S::map_align_in_place, {share});
        p.placed_ok = true;
    } else {
        // the sizes, to place it by hand
        std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f}, rlo = lo, rhi = hi;
        assets::ImportedModel base = p.raw;
        place_model(&base, RoomPlacement{});
        for (const auto& mesh : base.model.meshes) {
            for (const auto& v : mesh.vertices) {
                for (std::size_t c = 0; c < 3; ++c)
                    lo[c] = std::min(lo[c], v.position[c]), hi[c] = std::max(hi[c], v.position[c]);
            }
        }
        for (const auto& list : m.collision.faces) {
            for (const auto& f : list) {
                for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
                    for (std::size_t c = 0; c < 3; ++c) {
                        rlo[c] = std::min(rlo[c], static_cast<float>(f.v[v][c]) * kFx);
                        rhi[c] = std::max(rhi[c], static_cast<float>(f.v[v][c]) * kFx);
                    }
                }
            }
        }
        p.placed_text = fmt(app.language, S::map_align_unrelated, {span_text(app, lo, hi), span_text(app, rlo, rhi)});
        p.placed_ok = false;
    }
    p.dirty = true;
}

// The map's own floors and walls (KH2's, or another game's made with its model) as the room's collision model; false
// (the reason in the room's error) when there are none.
bool load_own_collision(App& app) {
    Replacing& p = m.rep;
    fs::path file;
    std::string why, map;
    bool found = false;
    if (const CollectionGameInfo* game = source_game(p.r, &map)) {
        std::error_code ec;
        file = collection_made_collision(app.settings, *game, map);
        found = fs::is_regular_file(file, ec);
        if (!found)
            why = p.r.source + ": no floors and walls of its own";
    } else {
        found = make_kh2_collision(app.settings, p.r.kh2, &file, &why);
    }
    if (found && import_model(file, &p.raw_collision, &why)) {
        p.collision_source = file;
        return true;
    }
    p.error = why;
    return false;
}

// The worker's results taken: its list of maps, or the model it made (offered as the room's model).
void kh2_take(App& app, const Item& item) {
    bool listed = false, made = false;
    std::string failed, map;
    fs::path model;
    Kh2Made outcome;
    {
        std::lock_guard lock(kh2.mutex);
        std::swap(listed, kh2.listed);
        std::swap(made, kh2.made);
        failed = kh2.failed;
        map = kh2.done_map;
        model = kh2.model;
        outcome = kh2.outcome;
        if (listed)
            kh2.maps = kh2.found;
    }
    if (kh2.worker.joinable() && !kh2.busy)
        kh2.worker.join();
    if (listed) {
        kh2.error = failed;
        // this room's own KH2 room first
        const Kh2Pair* pair = m.room != nullptr ? kh2_pair_of(kh2.pairs, m.world.code, m.room->index) : nullptr;
        if (pair != nullptr) {
            const auto at = std::find(kh2.maps.begin(), kh2.maps.end(), pair->map);
            if (at != kh2.maps.end())
                std::rotate(kh2.maps.begin(), at, at + 1);
        }
        kh2.out.clear();
        for (const auto& name : kh2.maps)
            kh2.out[name] = kh2_map_taken_out(app.settings, name);
    }
    if (!made)
        return;
    if (!failed.empty()) {
        kh2.error = failed;
        app.tell(fmt(app.language, S::save_failed, {failed}), false);
        return;
    }
    kh2.error.clear();
    kh2.out[map] = true;
    kh2.radars.erase(map);
    m.rep.next_is_collision = false;
    m.rep.r.kh2 = map;
    m.rep.r.source.clear();
    // the game's floor and walls kept, unless KH2's were chosen
    if (!m.rep.kh2_collision) {
        m.rep.r.keep_collision = true;
        m.rep.other_collision = false;
        m.rep.collision_source.clear();
        m.rep.raw_collision = {};
        m.rep.r.collision.clear();
    }
    room_offer(app, item, model);
    if (m.rep.kh2_collision) {
        load_own_collision(app);  // KH2's own floors and walls: the new map's
        m.rep.dirty = true;
    }
    app.tell(fmt(app.language, S::map_kh2_made,
                 {map, std::to_string(outcome.triangles), std::to_string(outcome.pictures),
                  std::to_string(outcome.pieces), std::to_string(outcome.animations)}),
             true);
    for (const auto& line : outcome.report)
        app.tell(map + ": " + line, false);
}

void kh2_start(App& app, const std::string& map, const std::string& world) {
    if (kh2.busy)
        return;
    if (kh2.worker.joinable())
        kh2.worker.join();
    kh2.busy = true;
    kh2.working = map;
    kh2.error.clear();
    const Settings settings = app.settings;
    kh2.worker = std::thread([settings, map, world] {
        std::vector<std::string> found;
        std::string why;
        Kh2Made outcome;
        const fs::path model = map.empty() ? fs::path() : kh2_made_model(settings, map);
        bool ok = false;
        if (map.empty()) {
            ok = kh2_world_maps(settings, world, &found, &why);
        } else {
            // the model made already when this studio made it (Take out every map)
            CollectionMap one;
            one.name = map;
            CollectionMade made;
            ok = collection_room(settings, *collection_game("kh2"), one, &made, &why);
            outcome.triangles = made.triangles;
            outcome.pictures = made.pictures;
            outcome.pieces = made.pieces;
            outcome.animations = made.animations;
            outcome.report = made.report;
        }
        std::lock_guard lock(kh2.mutex);
        kh2.failed = ok ? std::string() : why;
        if (map.empty()) {
            kh2.found = std::move(found);
            kh2.listed = true;
        } else {
            kh2.done_map = map;
            kh2.model = model;
            kh2.outcome = std::move(outcome);
            kh2.made = true;
        }
        kh2.busy = false;
    });
}

// The KH2 rooms of the player's copy to take as the room's model: a world's, or one by its name.
void draw_kh2_picker(App& app) {
    const auto lang = app.language;
    const ImVec4 warn(0.95f, 0.8f, 0.35f, 1);
    ImGui::Indent();
    ImGui::TextWrapped("%s", tr(lang, S::map_kh2_hint));
    if (kh2.collection_for != app.settings.collection_1525) {
        kh2.collection = kh2_collection(app.settings);
        kh2.collection_for = app.settings.collection_1525;
    }
    if (kh2.collection.empty()) {
        ImGui::TextColored(warn, "%s", tr(lang, S::map_kh2_no_collection));
        ImGui::Unindent();
        return;
    }
    ImGui::TextDisabled("%s", u8(kh2.collection).c_str());
    // the KH2 room this room was made from, when it is known
    const Kh2Pair* pair = room_pair(app);
    if (pair != nullptr) {
        ImGui::TextWrapped("%s", fmt(lang, S::map_kh2_pair, {pair->map, pair->evidence}).c_str());
        ImGui::BeginDisabled(kh2.busy);
        if (ImGui::Button((std::string(tr(lang, S::map_kh2_use)) + " " + pair->map + "##pair").c_str()))
            kh2_start(app, pair->map, {});
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("%s", tr(lang, S::map_kh2_no_pair));
    }
    if (kh2.world < 0) {
        kh2.world = 0;
        for (int w = 0; w < kKh2WorldCount; ++w) {
            if (m.world.code == kKh2Worlds[w])
                kh2.world = w;
        }
    }
    ImGui::BeginDisabled(kh2.busy);
    static const std::vector<std::string> kh2_labels = [] {
        std::vector<std::string> out;
        for (const char* w : kKh2Worlds)
            out.push_back(collection_world_label(CollectionGame::kh2, w));
        return out;
    }();
    std::vector<const char*> kh2_worlds;
    for (const auto& w : kh2_labels)
        kh2_worlds.push_back(w.c_str());
    ImGui::SetNextItemWidth(combo_width(kh2_labels));
    if (ImGui::Combo(tr(lang, S::map_kh2_world), &kh2.world, kh2_worlds.data(), kKh2WorldCount))
        kh2.maps.clear();
    same_line_if_room(button_width(tr(lang, S::map_kh2_list)));
    if (ImGui::Button(tr(lang, S::map_kh2_list)))
        kh2_start(app, {}, kKh2Worlds[kh2.world]);
    same_line_if_room(button_width(tr(lang, S::take_out_this_game)));
    if (ImGui::Button(tr(lang, S::take_out_this_game))) {
        app.take_out_asked = true;
        app.take_out_game = "kh2";
    }
    ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
    ImGui::InputText(tr(lang, S::map_kh2_by_name), &kh2.typed);
    ImGui::SameLine();
    if (ImGui::Button((std::string(tr(lang, S::map_kh2_use)) + "##typed").c_str()) && !kh2.typed.empty())
        kh2_start(app, kh2.typed, {});
    ImGui::EndDisabled();
    if (kh2.busy) {
        ImGui::TextDisabled("%s", kh2.working.empty() ? tr(lang, S::map_kh2_list)
                                                      : fmt(lang, S::map_kh2_working, {kh2.working}).c_str());
    }
    if (!kh2.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1));
        ImGui::TextWrapped("%s", kh2.error.c_str());
        ImGui::PopStyleColor();
    }
    const float side = 4 * ImGui::GetFontSize();
    for (const auto& name : kh2.maps) {
        ImGui::PushID(name.c_str());
        auto radar = kh2.radars.find(name);
        if (radar == kh2.radars.end() && kh2.out[name]) {
            assets::DecodedTexture picture;
            kh2_radar(app.settings, name, &picture);
            radar = kh2.radars.emplace(name, std::move(picture)).first;
        }
        if (radar != kh2.radars.end() && !radar->second.rgba.empty()) {
            const GpuImage& image = app.images->pixels("kh2-radar/" + name, 1, radar->second.rgba, radar->second.width,
                                                       radar->second.height);
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(image.texture))),
                         ImVec2(side, side));
        } else {
            ImGui::Dummy(ImVec2(side, side));
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (pair != nullptr && pair->map == name)
            ImGui::Text("%s  (%s)", name.c_str(), tr(lang, S::map_kh2_pair_mark));
        else
            ImGui::TextUnformatted(name.c_str());
        if (!kh2.out[name])
            ImGui::TextDisabled("%s", tr(lang, S::map_kh2_not_out));
        ImGui::BeginDisabled(kh2.busy || room_adder_busy());
        if (ImGui::SmallButton(tr(lang, S::map_kh2_use)))
            kh2_start(app, name, {});
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(lang, S::map_add_room))) {
            CollectionMap map;
            map.name = name;
            room_adder_open_map("map", collection_game("kh2"), map, m.world.code);
        }
        ImGui::EndDisabled();
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::Unindent();
}

// The other game's worker's results taken: its list of maps, or the room it made (offered as the room's model, its
// own floors and walls as its collision when it has them).
void other_take(App& app, const Item& item) {
    bool listed = false, made = false;
    std::string failed, source;
    fs::path model;
    CollectionMade outcome;
    {
        std::lock_guard lock(other.mutex);
        std::swap(listed, other.listed);
        std::swap(made, other.made);
        failed = other.failed;
        source = other.done_source;
        model = other.model;
        outcome = other.outcome;
        if (listed)
            other.maps = std::move(other.found);
    }
    if (other.worker.joinable() && !other.busy)
        other.worker.join();
    if (listed) {
        other.error = failed;
        other.listed_game = other.game;
        other.world = 0;
        other.worlds.clear();
        other.out.clear();
        const CollectionGameInfo* game = other_games()[static_cast<std::size_t>(other.game)];
        for (const auto& mp : other.maps) {
            if (other.worlds.empty() || other.worlds.back() != mp.world)
                other.worlds.push_back(mp.world);
            other.out[mp.name] = collection_map_taken_out(app.settings, *game, mp);
        }
    }
    if (!made)
        return;
    if (!failed.empty()) {
        other.error = failed;
        app.tell(fmt(app.language, S::save_failed, {failed}), false);
        return;
    }
    other.error.clear();
    other.out[source.substr(source.find(':') + 1)] = true;
    Replacing& p = m.rep;
    p.next_is_collision = false;
    p.r.kh2.clear();
    p.r.source = source;
    // its own floors and walls when it has them (the game room's would not fit it), else made from the model
    p.r.keep_collision = false;
    p.kh2_collision = p.other_collision = outcome.collision;
    p.collision_source.clear();
    p.raw_collision = {};
    p.r.collision.clear();
    room_offer(app, item, model);
    if (p.kh2_collision && !load_own_collision(app))
        p.kh2_collision = p.other_collision = false;
    p.dirty = true;
    app.tell(fmt(app.language, S::map_other_made,
                 {source, std::to_string(outcome.triangles), std::to_string(outcome.pictures)}),
             true);
    for (const auto& line : outcome.report)
        app.tell(source + ": " + line, false);
}

// `map` of `game` made into a room on the worker; without one, the game's maps listed.
void other_start(App& app, const CollectionGameInfo* game, const CollectionMap* map) {
    if (other.busy)
        return;
    if (other.worker.joinable())
        other.worker.join();
    other.busy = true;
    other.working = map != nullptr ? map->name : std::string();
    other.error.clear();
    const Settings settings = app.settings;
    const bool make = map != nullptr;
    const CollectionMap chosen = make ? *map : CollectionMap{};
    other.worker = std::thread([settings, game, chosen, make] {
        std::vector<CollectionMap> found;
        std::string why;
        CollectionMade outcome;
        const bool ok = make ? collection_room(settings, *game, chosen, &outcome, &why)
                             : collection_maps(settings, *game, &found, &why);
        std::lock_guard lock(other.mutex);
        other.failed = ok ? std::string() : why;
        if (make) {
            other.done_source = std::string(game->code) + ":" + chosen.name;
            other.model = collection_made_model(settings, *game, chosen.name);
            other.outcome = std::move(outcome);
            other.made = true;
        } else {
            other.found = std::move(found);
            other.listed = true;
        }
        other.busy = false;
    });
}

// The other games' rooms of the player's copies to take as the room's model: a game, its worlds, their maps.
void draw_other_picker(App& app) {
    const auto lang = app.language;
    const ImVec4 warn(0.95f, 0.8f, 0.35f, 1);
    ImGui::Indent();
    ImGui::TextWrapped("%s", tr(lang, S::map_other_hint));
    const auto games = other_games();
    std::vector<const char*> names;
    for (const auto* g : games)
        names.push_back(g->name);
    ImGui::BeginDisabled(other.busy);
    ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
    if (ImGui::Combo(tr(lang, S::map_other_game), &other.game, names.data(), static_cast<int>(names.size()))) {
        other.maps.clear();
        other.worlds.clear();
        other.listed_game = -1;
    }
    ImGui::EndDisabled();
    const CollectionGameInfo* game = games[static_cast<std::size_t>(other.game)];
    const std::string copy_for =
        std::to_string(other.game) + "|" + app.settings.collection_1525 + "|" + app.settings.collection_28;
    if (other.copy_for != copy_for) {
        other.copy = collection_copy(app.settings, *game);
        other.copy_for = copy_for;
    }
    if (other.copy.empty()) {
        ImGui::TextColored(warn, "%s", fmt(lang, S::map_other_no_copy, {game->name}).c_str());
        ImGui::Unindent();
        return;
    }
    ImGui::TextDisabled("%s", u8(other.copy).c_str());
    ImGui::BeginDisabled(other.busy);
    if (ImGui::Button(tr(lang, S::map_kh2_list)))
        other_start(app, game, nullptr);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr(lang, S::take_out_this_game))) {
        app.take_out_asked = true;
        app.take_out_game = game->code;
    }
    if (other.busy) {
        ImGui::TextDisabled("%s", other.working.empty() ? tr(lang, S::map_kh2_list)
                                                        : fmt(lang, S::map_kh2_working, {other.working}).c_str());
    }
    if (!other.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1));
        ImGui::TextWrapped("%s", other.error.c_str());
        ImGui::PopStyleColor();
    }
    if (other.listed_game != other.game || other.worlds.empty()) {
        ImGui::Unindent();
        return;
    }
    // each world by its name, its code after it
    std::vector<std::string> labels;
    for (const auto& w : other.worlds)
        labels.push_back(collection_world_label(game->game, w));
    std::vector<const char*> worlds;
    for (const auto& w : labels)
        worlds.push_back(w.c_str());
    ImGui::SetNextItemWidth(combo_width(labels));
    ImGui::Combo(tr(lang, S::map_other_world), &other.world, worlds.data(), static_cast<int>(worlds.size()));
    const std::string& world = other.worlds[static_cast<std::size_t>(other.world)];
    for (const auto& mp : other.maps) {
        if (mp.world != world)
            continue;
        ImGui::PushID(mp.name.c_str());
        ImGui::TextUnformatted(mp.name.c_str());
        if (collection_map_falling(*game, mp)) {
            ImGui::SameLine();
            ImGui::TextColored(warn, "%s", tr(lang, S::map_other_falling));
        }
        if (!other.out[mp.name]) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", tr(lang, S::map_kh2_not_out));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(other.busy || room_adder_busy());
        if (ImGui::SmallButton(tr(lang, S::map_kh2_use)))
            other_start(app, game, &mp);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr(lang, S::map_add_room)))
            room_adder_open_map("map", game, mp, m.world.code);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::Unindent();
}

// The modder's room raised so that most of the room's markers stand on one of its floors, and the person
// told how many do.
void raise_to_room_markers(App& app) {
    Replacing& p = m.rep;
    if (p.dirty)
        rebuild();
    std::vector<std::array<float, 3>> feet;
    for (const auto& mk : showing_built() ? markers(p.built) : m.markers) {
        feet.push_back({static_cast<float>(mk.place[0]) * kFx, static_cast<float>(mk.place[1]) * kFx,
                        static_cast<float>(mk.place[2]) * kFx});
    }
    std::size_t on = 0;
    const float raise = raise_to_markers(p.surfaces, feet, &on);
    char by[32];
    std::snprintf(by, sizeof(by), "%+.2f", raise);
    if (on > 0) {
        p.r.place.move[1] += raise;
        p.placed_text = fmt(app.language, S::map_raised, {by, std::to_string(on), std::to_string(feet.size())});
        p.placed_ok = on * 2 >= feet.size();
        p.dirty = true;
    } else {
        p.placed_text = tr(app.language, S::map_raised_none);
        p.placed_ok = false;
    }
}

void choose_room_export(App& app, const Item& item) {
    choose_save_file(app, export_name(item), "glTF", "gltf", app.dialog_room_exports);
}

// Above the picture while the room is being replaced: which room it shows,
// and why it is not yours when yours could not be built.
void draw_shown_room(App& app) {
    const auto lang = app.language;
    const Replacing& p = m.rep;
    if (!p.active)
        return;
    const char* views[3] = {tr(lang, S::map_show_new), tr(lang, S::map_show_game), tr(lang, S::map_show_both)};
    ImGui::SetNextItemWidth(12 * ImGui::GetFontSize());
    ImGui::Combo(tr(lang, S::map_show), &m.rep.view, views, 3);
    if (!p.built_ok && !p.dirty) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1));
        ImGui::TextWrapped("%s",
                           fmt(lang, S::map_yours_not_shown, {p.error.empty() ? std::string("-") : p.error}).c_str());
        ImGui::PopStyleColor();
    }
}

void draw_replace(App& app, const Item& item) {
    const auto lang = app.language;
    Replacing& p = m.rep;
    const ImVec4 warn(0.95f, 0.8f, 0.35f, 1), good(0.55f, 0.9f, 0.55f, 1);
    if (m.open_replace) {
        ImGui::SetNextItemOpen(true);
        m.open_replace = false;
    }
    if (!ImGui::CollapsingHeader(tr(lang, S::map_replace), p.active ? ImGuiTreeNodeFlags_DefaultOpen : 0))
        return;
    const Mod* mod = app.current_mod();

    // 1. the model: the person's, or the game's room exported to start from
    ImGui::SeparatorText(tr(lang, S::map_step_model));
    ImGui::TextWrapped("%s", tr(lang, S::map_replace_hint));
    kh2_take(app, item);
    other_take(app, item);
    {
        std::string added_world, added_key;
        draw_room_adder(app, "map", &added_world, &added_key);
    }
    if (ImGui::Button(tr(lang, S::choose_file))) {
        p.next_is_collision = false;
        p.r.kh2.clear();  // the modder's own model, not a KH2 room nor another game's
        p.r.source.clear();
        choose_file(app, item);
    }
    // or, none of the game's replaced, as a room of the mod's own
    same_line_if_room(button_width(tr(lang, S::map_add_model_room)));
    ImGui::BeginDisabled(room_adder_busy());
    if (ImGui::Button(tr(lang, S::map_add_model_room)))
        room_adder_choose_model(app, "map", m.world.code);
    ImGui::EndDisabled();
    same_line_if_room(button_width(tr(lang, S::map_export)));
    if (ImGui::Button(tr(lang, S::map_export)))
        choose_room_export(app, item);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr(lang, S::map_export_hint));
    same_line_if_room(button_width(tr(lang, S::map_kh2_source)));
    if (ImGui::Button(tr(lang, S::map_kh2_source)))
        kh2.shown = !kh2.shown;
    // each list in its own ids: both show buttons of the same words (its rooms, take out this game's maps), and two
    // buttons of one id answer neither click but the first's
    if (kh2.shown) {
        ImGui::PushID("kh2");
        draw_kh2_picker(app);
        ImGui::PopID();
    }
    same_line_if_room(button_width(tr(lang, S::map_other_source)));
    if (ImGui::Button(tr(lang, S::map_other_source)))
        other.shown = !other.shown;
    if (other.shown) {
        ImGui::PushID("other");
        draw_other_picker(app);
        ImGui::PopID();
    }
    if (mod == nullptr) {
        ImGui::TextColored(warn, "%s", tr(lang, S::choose_mod_first));
        return;
    }
    if (!p.active)
        return;
    if (!p.r.kh2.empty() || !p.r.source.empty())
        ImGui::TextColored(warn, "%s %s", (p.r.kh2.empty() ? p.r.source : p.r.kh2).c_str(), tr(lang, S::map_kh2_note));
    ImGui::TextUnformatted(
        fmt(lang, S::map_model_file, {p.model_source.empty() ? std::string("-") : u8(p.model_source.filename())})
            .c_str());

    // 2. where it sits: lined up by the tool when it can, the person's hand otherwise
    ImGui::SeparatorText(tr(lang, S::map_step_place));
    if (!p.placed_text.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.placed_ok ? good : warn);
        ImGui::TextWrapped("%s", p.placed_text.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::Button(tr(lang, S::map_align_button)))
        line_up(app);
    // the height that puts most of the room's markers on one of its floors
    same_line_if_room(button_width(tr(lang, S::map_raise_to_markers)));
    if (ImGui::Button(tr(lang, S::map_raise_to_markers)) && p.built_ok)
        raise_to_room_markers(app);
    bool changed = false;
    ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
    ImGui::DragFloat(tr(lang, S::map_scale), &p.r.place.scale, 0.01f, 0.001f, 1000.0f, "%.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
    ImGui::DragFloat3(tr(lang, S::map_turn), p.r.place.turn.data(), 0.5f, -360.0f, 360.0f, "%.1f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(20 * ImGui::GetFontSize());
    ImGui::DragFloat3(tr(lang, S::map_move), p.r.place.move.data(), 0.05f, -100000.0f, 100000.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (p.r.place.scale <= 0.0f)
        p.r.place.scale = 0.001f;
    // only what lies around the room: a KH2 map is a whole place, the room a part of it
    bool cut = p.r.zone >= 0;
    if (ImGui::Checkbox(tr(lang, S::map_zone), &cut)) {
        p.r.zone = cut ? kZoneMargin : -1.0f;
        changed = true;
    }
    if (cut) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(6 * ImGui::GetFontSize());
        ImGui::DragFloat(tr(lang, S::map_zone_margin), &p.r.zone, 0.25f, 0.0f, 1000.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (p.r.zone < 0)
            p.r.zone = 0;
    }

    // 3. the floor and walls: the game's kept, made from the model, or from a simpler one
    ImGui::SeparatorText(tr(lang, S::map_step_collision));
    int source = p.r.keep_collision                                 ? 0
                 : p.kh2_collision                                  ? 3
                 : p.other_collision || !p.collision_source.empty() ? 2
                                                                    : 1;
    const int before = source;
    ImGui::RadioButton(tr(lang, S::map_collision_keep), &source, 0);
    ImGui::RadioButton(tr(lang, S::map_collision_mine), &source, 1);
    ImGui::RadioButton(tr(lang, S::map_collision_other), &source, 2);
    if (!p.r.kh2.empty())
        ImGui::RadioButton(tr(lang, S::map_collision_kh2), &source, 3);
    else if (!p.r.source.empty())
        ImGui::RadioButton(tr(lang, S::map_collision_own), &source, 3);
    if (source == 2) {
        ImGui::SameLine();
        ImGui::TextUnformatted(p.collision_source.empty() ? "-" : u8(p.collision_source.filename()).c_str());
        same_line_if_room(button_width(tr(lang, S::map_choose_collision)));
        if (ImGui::SmallButton(tr(lang, S::map_choose_collision))) {
            p.next_is_collision = true;
            choose_file(app, item);
        }
    }
    if (source != before) {
        p.r.keep_collision = source == 0;
        p.other_collision = source == 2 || source == 3;
        p.kh2_collision = source == 3;
        if (source != 2) {
            p.collision_source.clear();
            p.raw_collision = {};
            p.r.collision.clear();
        }
        if (source == 3 && !load_own_collision(app))
            p.kh2_collision = p.other_collision = false;
        p.dirty = true;
    }

    // advanced: the steepest ground, the room's other models, names, the exits' models
    if (ImGui::TreeNode(tr(lang, S::map_advanced))) {
        float degrees = std::acos(std::clamp(p.r.ground_limit, -1.0f, 1.0f)) * 57.2957795f;
        ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
        if (ImGui::DragFloat(tr(lang, S::map_ground_limit), &degrees, 0.1f, 1.0f, 89.0f, "%.1f")) {
            p.r.ground_limit = std::cos(std::clamp(degrees, 1.0f, 89.0f) / 57.2957795f);
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        const char* uses[3] = {tr(lang, S::map_use_model), tr(lang, S::map_use_hide), tr(lang, S::map_use_keep)};
        for (const auto& [member, use] : object_uses(*m.room, p.r)) {
            std::string name = std::to_string(member);
            for (const auto& rm : m.models) {
                if (rm.member == member && !rm.key.empty())
                    name = rm.key.substr(rm.key.rfind('/') + 1);
            }
            int choice = use == ObjectUse::model ? 0 : use == ObjectUse::hide ? 1 : 2;
            ImGui::SetNextItemWidth(10 * ImGui::GetFontSize());
            if (ImGui::Combo(fmt(lang, S::map_object, {std::to_string(member), name}).c_str(), &choice, uses, 3)) {
                p.r.objects[member] = choice == 0 ? ObjectUse::model : choice == 1 ? ObjectUse::hide : ObjectUse::keep;
                p.dirty = true;
            }
        }
        draw_names_and_exits(app);
        ImGui::TreePop();
    }
    if (changed)
        p.dirty = true;
    if (p.dirty)
        rebuild();

    if (p.built_ok && !p.r.keep_collision) {
        ImGui::TextDisabled("%s", fmt(lang, S::map_built,
                                      {std::to_string(p.report.faces[kGround]), std::to_string(p.report.faces[kWall]),
                                       std::to_string(p.report.faces[kCeiling]), std::to_string(p.report.nodes),
                                       std::to_string(p.report.bytes)})
                                      .c_str());
    } else if (p.built_ok) {
        ImGui::TextDisabled("%s", tr(lang, S::map_collision_kept_note));
    }
    for (const auto& w : p.warnings) {
        // in RoomWarning::Kind's order
        static const S words[] = {S::map_warn_dropped,         S::map_warn_too_big,          S::map_warn_no_ground,
                                  S::map_warn_gate_unplaced,   S::map_warn_wall_unplaced,    S::map_warn_name_kept,
                                  S::map_warn_marker_floating, S::map_warn_collision_shared, S::map_warn_model_shared,
                                  S::map_warn_passable,        S::map_warn_marker_in_wall};
        static_assert(sizeof(words) / sizeof(words[0]) == RoomWarning::marker_in_wall + 1, "a word per warning");
        std::string text = tr(lang, words[w.kind]);
        for (std::size_t k = 0; k < w.args.size(); ++k) {
            const std::string key = "{" + std::to_string(k) + "}";
            for (auto at = text.find(key); at != std::string::npos; at = text.find(key))
                text.replace(at, key.size(), w.args[k]);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, warn);
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    }
    if (!p.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1));
        ImGui::TextWrapped("%s", p.error.c_str());
        ImGui::PopStyleColor();
    }

    // 4. saved into the mod (or the room given back to the game)
    ImGui::SeparatorText(tr(lang, S::map_step_save));
    const fs::path dump = resolve(app.settings, app.settings.graphics_dump);
    ImGui::BeginDisabled(!p.built_ok);
    if (ImGui::Button(tr(lang, S::map_save))) {
        WorldOutcome o;
        std::string why;
        p.r.world = m.world.code;
        p.r.room = m.room->index;
        if (save_change(nitrofs(), dump, mod->dir, p.r, p.model_source, p.collision_source, &o, &why)) {
            // from now on, the mod's copies
            const fs::path folder = change_models(mod->dir, m.world.code, m.room->index);
            p.model_source = folder / p.model_source.filename();
            if (!p.collision_source.empty())
                p.collision_source = folder / p.collision_source.filename();
            p.from_mod = true;
            p.message = fmt(lang, S::map_saved, {mod->name});
            app.tell(p.message + "  " + tr(lang, S::restart_note), true);
        } else {
            p.error = why;
            app.tell(fmt(lang, S::save_failed, {why}), false);
        }
    }
    ImGui::EndDisabled();
    if (p.from_mod) {
        same_line_if_room(button_width(tr(lang, S::map_revert)));
        if (ImGui::Button(tr(lang, S::map_revert))) {
            WorldOutcome o;
            std::string why;
            if (revert_change(nitrofs(), dump, mod->dir, m.world.code, m.room->index, &o, &why)) {
                m.rep = Replacing{};
                m.rep.message = fmt(lang, S::map_reverted, {mod->name});
                app.tell(m.rep.message, true);
            } else {
                p.error = why;
                app.tell(fmt(lang, S::save_failed, {why}), false);
            }
        }
    }
    if (!m.rep.message.empty())
        ImGui::TextDisabled("%s", m.rep.message.c_str());
}

}  // namespace

void room_offer(App& app, const Item& item, const fs::path& file) {
    (void)item;
    if (m.item != app.selected || m.mod != app.mod)
        open(app);
    if (m.room == nullptr)
        return;
    Replacing& p = m.rep;
    const bool collision = p.next_is_collision;
    p.next_is_collision = false;
    assets::ImportedModel model;
    std::string why;
    if (!import_model(file, &model, &why)) {
        p.error = u8(file.filename()) + ": " + why;
        return;
    }
    if (collision && p.active) {
        p.collision_source = file;
        p.raw_collision = std::move(model);
        p.other_collision = true;
    } else {
        p.model_source = file;
        p.raw = std::move(model);
    }
    if (!p.active) {
        p.active = true;
        p.r.world = m.world.code;
        p.r.room = m.room->index;
    }
    p.message.clear();
    p.dirty = true;
    if (!collision)
        line_up(app);
}

void room_export_to(App& app, const fs::path& file) {
    if (m.room == nullptr)
        return;
    std::vector<std::string> notes;
    std::string why;
    fs::path out = file;
    if (lower(u8(out.extension())) != ".gltf")
        out += ".gltf";
    if (!export_room(nitrofs(), resolve(app.settings, app.settings.graphics_dump), m.world, m.room->index, out, &notes,
                     &why)) {
        app.tell(fmt(app.language, S::save_failed, {why}), false);
        return;
    }
    app.tell(fmt(app.language, S::map_exported, {u8(out)}), true);
    for (const auto& n : notes)
        app.tell(n, false);
}

void draw_map_view(App& app, const Item& item) {
    (void)item;
    if (m.item != app.selected || m.mod != app.mod)
        open(app);
    const auto lang = app.language;
    if (!m.error.empty() && m.room == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", m.error.c_str());
        return;
    }
    if (m.rep.dirty)
        rebuild();  // the modder's room made before it is counted and drawn
    ImGui::TextUnformatted(
        fmt(lang, S::map_room, {std::to_string(m.room->index), std::to_string(m.room->id), world_path(m.world.code)})
            .c_str());
    draw_shown_room(app);
    if (m.has_collision) {
        ImGui::TextDisabled(
            "%s", fmt(lang, S::map_counts,
                      {std::to_string(shown().faces[kGround].size()), std::to_string(shown().faces[kWall].size()),
                       std::to_string(shown().faces[kCeiling].size()), std::to_string(m.markers.size()),
                       std::to_string(shown().named.size())})
                      .c_str());
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1), "%s",
                           m.error.empty() ? tr(lang, S::map_no_collision) : m.error.c_str());
    }
    for (const auto& rm : m.models) {
        if (!rm.problem.empty())
            ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.35f, 1), "%s", rm.problem.c_str());
    }
    ImGui::Checkbox(tr(lang, S::map_models), &m.show_models);
    same_line_if_room(check_width(tr(lang, S::map_grounds)));
    ImGui::Checkbox(tr(lang, S::map_grounds), &m.show_ground);
    same_line_if_room(check_width(tr(lang, S::map_walls)));
    ImGui::Checkbox(tr(lang, S::map_walls), &m.show_walls);
    same_line_if_room(check_width(tr(lang, S::map_ceilings)));
    ImGui::Checkbox(tr(lang, S::map_ceilings), &m.show_ceilings);
    same_line_if_room(check_width(tr(lang, S::map_markers)));
    ImGui::Checkbox(tr(lang, S::map_markers), &m.show_markers);
    bool sky = false;
    for (const auto& mesh : m.rep.placed.model.meshes)
        sky = sky || mesh.sky;
    if (sky && m.rep.active && m.rep.view != 1) {
        same_line_if_room(check_width(tr(lang, S::map_sky)));
        ImGui::Checkbox(tr(lang, S::map_sky), &m.show_sky);
    }
    same_line_if_room(button_width(tr(lang, S::map_frame)));
    if (ImGui::Button(tr(lang, S::map_frame)))
        frame_camera();
    // the picture, as tall as the person drags its lower edge
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    draw_room(app, ImVec2(avail.x, std::max(200.0f, avail.y * m.view_share)));
    ImGui::InvisibleButton("##viewsplit", ImVec2(avail.x, 0.5f * ImGui::GetFontSize()));
    if (ImGui::IsItemActive() && avail.y > 0)
        m.view_share = std::clamp(m.view_share + ImGui::GetIO().MouseDelta.y / avail.y, 0.2f, 0.95f);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        m.view_share = m.view_share < 0.7f ? 0.9f : 0.5f;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ImGui::SetTooltip("%s", tr(lang, S::map_view_size));
    }
    {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const float y = (a.y + b.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, y), ImVec2(b.x, y),
                                            ImGui::GetColorU32(ImGui::IsItemHovered() || ImGui::IsItemActive()
                                                                   ? ImGuiCol_SeparatorHovered
                                                                   : ImGuiCol_Separator),
                                            2.0f);
    }
    ImGui::TextDisabled("%s", tr(lang, S::map_controls));
    ImGui::BeginChild("##room_controls", ImVec2(0, 0), ImGuiChildFlags_None);
    draw_replace(app, item);
    if (ImGui::CollapsingHeader(tr(lang, S::room_music))) {  // the music the mod plays in this room
        if (const Mod* mod = app.current_mod())
            draw_room_music(app, game_room_music(mod->dir, m.world.code, m.room->index), "map_music");
        else
            ImGui::TextDisabled("%s", tr(lang, S::choose_mod_first));
    }
    draw_details(app);
    draw_lists(app);
    ImGui::EndChild();
}

}  // namespace studio
