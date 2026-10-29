// A mod's mission's places on its room (docs/STUDIO.md, "New missions"): the room as the game has it (or as the mod
// replaces it), its floor, and the players' start, the enemies' spawners, chests and emblems on it, each moved by
// dragging it over the floor, added by choosing what and clicking the floor, changed beside the picture, and saved
// all at once when the mission reads and builds as the port will.
#include "../app.h"
#include "../preview3d.h"

#include "core/files/paths.h"
#include "core/missions/enemy_names.h"
#include "core/missions/mission_build.h"
#include "core/missions/mod_missions.h"
#include "core/models/bones.h"
#include "core/models/game_model.h"
#include "core/models/model_files.h"
#include "core/rooms/room_change.h"
#include "core/rooms/room_collision.h"
#include "core/rooms/worlds.h"

#include "khdays/vfs/filesystem.h"

#include "imgui.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>

namespace studio {

namespace {

namespace assets = khdays::assets;
using Spot = MissionSpot::Kind;

constexpr float kFx = 1.0f / 4096.0f;  // the game's 20.12 to units
constexpr int kGround = 0;

struct Texture {
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
};

struct RoomModel {
    GameModel game;
    std::vector<Mat> palette;
};

struct MapState {
    std::string mod, key, world = "\x01";  // what is shown
    int room = -1;
    std::string error;
    bool has_collision = false;
    RoomCollision collision;
    std::vector<RoomModel> models;
    std::map<std::string, Texture> textures;
    bool mod_room = false;  // drawn from the mod's own model of the room
    assets::ImportedModel mod_model;

    std::vector<MissionSpot> spots, saved;
    std::string read_error;
    int chosen = -1;   // the place shown beside the picture
    int dragged = -1;  // the place being dragged
    int placing = -1;  // what a click on the floor adds (Kind), -1 nothing
    float yaw = 30.0f, pitch = 60.0f, distance = 40.0f;
    std::array<float, 3> target{0, 0, 0};
    bool show_floor = true, show_room = true;
    std::unique_ptr<Preview> preview;
};

MapState s;

fs::path nitrofs() {
    return khdays::vfs::data_root() / "nitrofs";
}

std::vector<std::uint8_t> read_bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

const std::vector<int>& buildable() {
    static const std::vector<int> classes = buildable_enemy_classes(extracted_game_files(nitrofs()));
    return classes;
}

std::array<float, 3> corner(const RoomFace& f, std::size_t v) {
    return {static_cast<float>(f.v[v][0]) * kFx, static_cast<float>(f.v[v][1]) * kFx,
            static_cast<float>(f.v[v][2]) * kFx};
}

const Texture* texture_of(const GameModel& g, const std::string& name) {
    const std::string key = g.label + "|" + name;
    const auto found = s.textures.find(key);
    if (found != s.textures.end())
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
    const auto it = s.textures.emplace(key, std::move(t)).first;
    return it->second.rgba.empty() ? nullptr : &it->second;
}

// The room as the mission plays it: the mod's own world file (its replaced rooms' floors) when it has one, else the
// game's; its models the mod's model of the room when it replaces it, else the game's.
void load_room(App& app, const Mod& m, const std::string& world, int room) {
    s.world = world;
    s.room = room;
    s.error.clear();
    s.has_collision = false;
    s.collision = {};
    s.models.clear();
    s.textures.clear();
    s.mod_room = false;
    s.mod_model = {};
    World w;
    std::string why;
    const fs::path own = m.dir / "files" / path_u8(world_path(world));
    std::error_code ec;
    const bool loaded = fs::is_regular_file(own, ec) ? parse_world(read_bytes(own), world, &w, &why)
                                                     : load_world(nitrofs(), world, &w, &why);
    if (!loaded || room < 0 || room >= static_cast<int>(w.rooms.size())) {
        s.error = world + " " + std::to_string(room) + ": " + (why.empty() ? "no such room" : why);
        return;
    }
    const WorldRoom& r = w.rooms[static_cast<std::size_t>(room)];
    const auto& data = w.archive.entries[static_cast<std::size_t>(r.collision_member)].data;
    s.has_collision = !data.empty() && parse_collision(data, &s.collision, &why);
    // its models
    const fs::path change = change_file(m.dir, world, room);
    RoomReplacement replaced;
    if (fs::is_regular_file(change, ec) && read_change(change, &replaced, &why) && !replaced.model.empty() &&
        import_model(change_models(m.dir, world, room) / path_u8(replaced.model), &s.mod_model, &why)) {
        place_model(&s.mod_model, replaced.place);
        s.mod_room = true;
    } else {
        const fs::path dump = resolve(app.settings, app.settings.graphics_dump);
        for (const auto& o : r.objects) {
            const std::string prefix = world_path(world) + "#" + std::to_string(o.member) + "/mdl/";
            for (const auto& it : app.catalog.items) {
                if (it.kind != Kind::model || it.key.rfind(prefix, 0) != 0)
                    continue;
                RoomModel rm;
                if (load_game_model(nitrofs(), dump, it.key, &rm.game, &why)) {
                    std::vector<Mat> node_world;
                    pose_game_model(rm.game, nullptr, 0.0f, &node_world, &rm.palette);
                    s.models.push_back(std::move(rm));
                }
                break;
            }
        }
    }
    // the camera over the floor, looking down
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& f : s.collision.faces[kGround]) {
        for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
            const auto c = corner(f, v);
            for (std::size_t k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], c[k]);
                hi[k] = std::max(hi[k], c[k]);
            }
        }
    }
    if (lo[0] <= hi[0]) {
        s.target = {(lo[0] + hi[0]) * 0.5f, lo[1], (lo[2] + hi[2]) * 0.5f};
        s.distance = std::max({hi[0] - lo[0], hi[2] - lo[2], 4.0f}) * 1.4f;
    }
}

// The floor's height under a point (from a little over it), if there is floor there.
bool floor_under(const std::array<double, 3>& at, double* y) {
    if (!s.has_collision)
        return false;
    double found = 0.0;
    if (!ground_under(s.collision, at[0] * 4096.0, at[2] * 4096.0, (at[1] + 4.0) * 4096.0, &found))
        return false;
    *y = found / 4096.0;
    return true;
}

// Where the pointer's ray meets the floor (the nearest ground face), else the level `y`.
bool pointer_on_floor(const std::array<float, 3>& eye, const std::array<float, 3>& dir, float y,
                      std::array<double, 3>* at) {
    float best = 1e30f;
    auto hit = [&](const std::array<float, 3>& a, const std::array<float, 3>& b, const std::array<float, 3>& c) {
        // Moller-Trumbore, both sides
        const std::array<float, 3> e1{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const std::array<float, 3> p{dir[1] * e2[2] - dir[2] * e2[1], dir[2] * e2[0] - dir[0] * e2[2],
                                     dir[0] * e2[1] - dir[1] * e2[0]};
        const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
        if (std::fabs(det) < 1e-9f)
            return;
        const std::array<float, 3> t0{eye[0] - a[0], eye[1] - a[1], eye[2] - a[2]};
        const float u = (t0[0] * p[0] + t0[1] * p[1] + t0[2] * p[2]) / det;
        if (u < 0.0f || u > 1.0f)
            return;
        const std::array<float, 3> q{t0[1] * e1[2] - t0[2] * e1[1], t0[2] * e1[0] - t0[0] * e1[2],
                                     t0[0] * e1[1] - t0[1] * e1[0]};
        const float v = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) / det;
        if (v < 0.0f || u + v > 1.0f)
            return;
        const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
        if (t > 0.0f && t < best)
            best = t;
    };
    for (const auto& f : s.collision.faces[kGround]) {
        if (f.corners < 3)
            continue;
        hit(corner(f, 0), corner(f, 1), corner(f, 2));
        if (f.corners == 4)
            hit(corner(f, 0), corner(f, 2), corner(f, 3));
    }
    if (best >= 1e29f) {
        if (std::fabs(dir[1]) < 1e-6f)
            return false;
        best = (y - eye[1]) / dir[1];
        if (best <= 0.0f)
            return false;
    }
    *at = {eye[0] + dir[0] * best, eye[1] + dir[1] * best, eye[2] + dir[2] * best};
    return true;
}

ImU32 spot_colour(const MissionSpot& p) {
    static const ImU32 waves[] = {IM_COL32(255, 80, 80, 255), IM_COL32(255, 150, 60, 255), IM_COL32(220, 90, 255, 255),
                                  IM_COL32(255, 60, 150, 255)};
    switch (p.kind) {
    case Spot::start: return IM_COL32(90, 170, 255, 255);
    case Spot::spawner: return waves[static_cast<std::size_t>(std::max(0, p.wave - 1)) % 4];
    case Spot::chest: return IM_COL32(255, 205, 60, 255);
    case Spot::emblem: return IM_COL32(120, 255, 200, 255);
    }
    return IM_COL32_WHITE;
}

std::string spot_label(App& app, const MissionSpot& p) {
    switch (p.kind) {
    case Spot::start: return tr(app.language, S::mmap_start);
    case Spot::spawner: {
        int count = 0;
        for (const auto& e : p.enemies)
            count += e.second;
        return fmt(app.language, S::mmap_spot_wave, {std::to_string(p.wave), std::to_string(count)});
    }
    case Spot::chest: return tr(app.language, S::mmap_chest);
    case Spot::emblem: return fmt(app.language, S::mmap_spot_badge, {std::to_string(p.emblem + 1)});
    }
    return {};
}

void draw_room(App& app, ImVec2 size) {
    if (!s.preview)
        s.preview = std::make_unique<Preview>(app.device);
    Preview& p = *s.preview;
    p.collect();
    const int w = std::max(64, static_cast<int>(size.x)), h = std::max(64, static_cast<int>(size.y));
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const Mat vp = orbit_view_projection(s.target, s.yaw, s.pitch, s.distance, aspect);
    p.begin(w, h, vp, orbit_eye_direction(s.yaw, s.pitch));
    if (s.show_room) {
        for (const auto& rm : s.models) {
            for (const auto& mesh : rm.game.model.meshes) {
                if (mesh.material_alpha <= 0.0f)
                    continue;
                const Texture* t = mesh.texture_name.empty() ? nullptr : texture_of(rm.game, mesh.texture_name);
                PreviewImage image;
                if (t != nullptr)
                    image = {t->rgba.data(), t->width, t->height, mesh.repeat, mesh.mirror};
                const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
                const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
                auto vertex = [&](const assets::NeutralVertex& v) {
                    PreviewVertex o;
                    std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
                    for (std::size_t k = 0; k < 4; ++k) {
                        const std::uint32_t jt = v.joints[k];
                        if (v.weights[k] <= 0.0f || jt >= rm.palette.size())
                            continue;
                        const auto q = mat_point(rm.palette[jt], v.position);
                        const auto n = mat_vec(rm.palette[jt], v.normal);
                        for (std::size_t c = 0; c < 3; ++c)
                            pos[c] += v.weights[k] * q[c], nrm[c] += v.weights[k] * n[c];
                    }
                    o.position = pos;
                    o.normal = nrm;
                    o.uv = {v.texcoord[0] / tw, v.texcoord[1] / th};
                    o.colour = {v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f,
                                v.color[3] / 255.0f * mesh.material_alpha};
                    return o;
                };
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                    p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                               vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
            }
        }
        if (s.mod_room) {
            for (const auto& mesh : s.mod_model.model.meshes) {
                const assets::DecodedTexture* t = nullptr;
                for (const auto& [name, decoded] : s.mod_model.textures)
                    if (name == mesh.texture_name && !decoded.rgba.empty())
                        t = &decoded;
                PreviewImage image;
                if (t != nullptr)
                    image = {t->rgba.data(), t->width, t->height};
                const float tw = t != nullptr ? static_cast<float>(t->width) : 1.0f;
                const float th = t != nullptr ? static_cast<float>(t->height) : 1.0f;
                auto vertex = [&](const assets::NeutralVertex& v) {
                    PreviewVertex o;
                    o.position = v.position;
                    o.normal = v.normal;
                    o.uv = {v.texcoord[0] / tw, v.texcoord[1] / th};
                    o.colour = {v.color[0] / 255.0f, v.color[1] / 255.0f, v.color[2] / 255.0f, v.color[3] / 255.0f};
                    return o;
                };
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                    p.triangle(vertex(mesh.vertices[mesh.indices[i]]), vertex(mesh.vertices[mesh.indices[i + 1]]),
                               vertex(mesh.vertices[mesh.indices[i + 2]]), image, !mesh.double_sided, {1, 1, 1});
            }
        }
    }
    // the floor the places stand on, see-through over the room
    if (s.show_floor && s.has_collision) {
        const float alpha = s.show_room ? 0.45f : 1.0f;
        for (const auto& f : s.collision.faces[kGround]) {
            if (f.corners < 3)
                continue;
            auto vertex = [&](std::size_t v) {
                PreviewVertex o;
                o.position = corner(f, v);
                o.position[1] += 0.02f;
                o.normal = {0, 1, 0};
                o.colour[3] = alpha;
                return o;
            };
            p.triangle(vertex(0), vertex(1), vertex(2), PreviewImage{}, false, {0.35f, 0.85f, 0.35f});
            if (f.corners == 4)
                p.triangle(vertex(0), vertex(2), vertex(3), PreviewImage{}, false, {0.35f, 0.85f, 0.35f});
        }
    }
    SDL_GPUTexture* tex = p.finish();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    if (tex != nullptr)
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(tex))),
                     ImVec2(static_cast<float>(w), static_cast<float>(h)));
    else
        ImGui::Dummy(ImVec2(static_cast<float>(w), static_cast<float>(h)));
    // the mouse the picture's own (a drag on it never moves the window)
    ImGui::SetCursorScreenPos(at);
    ImGui::InvisibleButton("##room", ImVec2(static_cast<float>(w), static_cast<float>(h)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

    // the places, over the picture
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->PushClipRect(at, ImVec2(at.x + static_cast<float>(w), at.y + static_cast<float>(h)), true);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int near = -1;
    float best = 14.0f * ImGui::GetStyle().FontScaleDpi;
    std::vector<ImVec2> shown(s.spots.size(), ImVec2(-1e9f, -1e9f));
    for (std::size_t i = 0; i < s.spots.size(); ++i) {
        const MissionSpot& sp = s.spots[i];
        float x, y;
        if (!p.project({static_cast<float>(sp.at[0]), static_cast<float>(sp.at[1]), static_cast<float>(sp.at[2])}, &x,
                       &y))
            continue;
        shown[i] = ImVec2(at.x + x, at.y + y);
        const float d = std::hypot(shown[i].x - mouse.x, shown[i].y - mouse.y);
        if (hovered && d < best) {
            best = d;
            near = static_cast<int>(i);
        }
    }
    const float r = 8.0f * ImGui::GetStyle().FontScaleDpi;
    for (std::size_t i = 0; i < s.spots.size(); ++i) {
        const MissionSpot& sp = s.spots[i];
        const ImVec2 c = shown[i];
        if (c.x < -1e8f)
            continue;
        const ImU32 colour = spot_colour(sp);
        const bool chosen = static_cast<int>(i) == s.chosen;
        double floor_y = 0.0;
        const bool on_floor = floor_under(sp.at, &floor_y) && std::fabs(floor_y - sp.at[1]) < 0.6;
        switch (sp.kind) {
        case Spot::start: {
            list->AddCircleFilled(c, r, colour);
            // which way it faces: 0 degrees along +z, turning to +x
            const double a = sp.heading * 3.14159265358979 / 180.0;
            float x, y;
            if (p.project({static_cast<float>(sp.at[0] + std::sin(a) * 1.0), static_cast<float>(sp.at[1]),
                           static_cast<float>(sp.at[2] + std::cos(a) * 1.0)},
                          &x, &y))
                list->AddLine(c, ImVec2(at.x + x, at.y + y), colour, 3.0f);
            break;
        }
        case Spot::spawner:
            list->AddTriangleFilled(ImVec2(c.x, c.y - r * 1.2f), ImVec2(c.x - r, c.y + r * 0.8f),
                                    ImVec2(c.x + r, c.y + r * 0.8f), colour);
            break;
        case Spot::chest:
            list->AddRectFilled(ImVec2(c.x - r, c.y - r * 0.7f), ImVec2(c.x + r, c.y + r * 0.7f), colour, 2.0f);
            break;
        case Spot::emblem:
            list->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y),
                                colour);
            break;
        }
        if (chosen)
            list->AddCircle(c, r * 1.7f, IM_COL32(255, 255, 255, 255), 0, 2.5f);
        if (!on_floor)
            list->AddCircle(c, r * 1.4f, IM_COL32(255, 40, 40, 255), 0, 2.0f);  // over no floor
        const std::string label = spot_label(app, sp);
        list->AddText(ImVec2(c.x + r * 1.5f, c.y - r), IM_COL32(240, 240, 240, 255), label.c_str());
    }
    list->PopClipRect();

    // the pointer's ray into the room
    const auto eye_dir = orbit_eye_direction(s.yaw, s.pitch);
    const std::array<float, 3> eye{s.target[0] + eye_dir[0] * s.distance, s.target[1] + eye_dir[1] * s.distance,
                                   s.target[2] + eye_dir[2] * s.distance};
    const std::array<float, 3> forward{-eye_dir[0], -eye_dir[1], -eye_dir[2]};
    std::array<float, 3> right{forward[1] * 0 - forward[2] * 1, forward[2] * 0 - forward[0] * 0,
                               forward[0] * 1 - forward[1] * 0};  // forward x up
    const float rl = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    for (auto& c : right)
        c /= rl > 0.0f ? rl : 1.0f;
    const std::array<float, 3> up{right[1] * forward[2] - right[2] * forward[1],
                                  right[2] * forward[0] - right[0] * forward[2],
                                  right[0] * forward[1] - right[1] * forward[0]};
    const float k = 1.0f / std::tan(35.0f * 0.5f * 0.0174532925f);
    const float nx = ((mouse.x - at.x) / static_cast<float>(w)) * 2.0f - 1.0f;
    const float ny = 1.0f - ((mouse.y - at.y) / static_cast<float>(h)) * 2.0f;
    std::array<float, 3> ray{};
    for (std::size_t c = 0; c < 3; ++c)
        ray[c] = forward[c] + right[c] * (nx * aspect / k) + up[c] * (ny / k);

    // choosing, dragging, adding
    if (hovered && s.placing >= 0) {
        ImGui::SetTooltip("%s", tr(app.language, S::mmap_click_floor));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            std::array<double, 3> where;
            if (pointer_on_floor(eye, ray, s.target[1], &where)) {
                MissionSpot added;
                added.kind = static_cast<Spot>(s.placing);
                added.at = where;
                if (added.kind == Spot::spawner) {
                    int last_wave = 1;
                    for (const auto& sp : s.spots)
                        if (sp.kind == Spot::spawner)
                            last_wave = std::max(last_wave, sp.wave);
                    added.wave = last_wave;
                    added.enemies = {{buildable().empty() ? 0 : buildable().front(), 3}};
                }
                if (added.kind == Spot::start) {  // one start: moved there
                    s.spots.erase(std::remove_if(s.spots.begin(), s.spots.end(),
                                                 [](const MissionSpot& sp) { return sp.kind == Spot::start; }),
                                  s.spots.end());
                }
                if (added.kind == Spot::emblem) {  // one of each badge
                    for (int e = 0; e < 3; ++e) {
                        bool taken = false;
                        for (const auto& sp : s.spots)
                            taken = taken || (sp.kind == Spot::emblem && sp.emblem == e);
                        if (!taken) {
                            added.emblem = e;
                            break;
                        }
                    }
                }
                s.spots.push_back(added);
                s.chosen = static_cast<int>(s.spots.size()) - 1;
            }
            s.placing = -1;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            s.placing = -1;
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && near >= 0) {
        s.chosen = near;
        s.dragged = near;
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        s.dragged = -1;
    }
    if (s.dragged >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        MissionSpot& sp = s.spots[static_cast<std::size_t>(s.dragged)];
        std::array<double, 3> where;
        if (pointer_on_floor(eye, ray, static_cast<float>(sp.at[1]), &where))
            sp.at = where;
    } else {
        s.dragged = -1;
        if (hovered && s.placing < 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            s.yaw -= d.x * 0.4f;
            s.pitch = std::clamp(s.pitch + d.y * 0.4f, 5.0f, 89.0f);
        }
    }
    orbit_move(s.target, s.yaw, s.pitch, s.distance, static_cast<float>(h), hovered);
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        s.distance *= std::pow(0.9f, ImGui::GetIO().MouseWheel);
    if (near >= 0 && s.dragged < 0 && s.placing < 0)
        ImGui::SetTooltip("%s", spot_label(app, s.spots[static_cast<std::size_t>(near)]).c_str());
}

// The chosen place, changed beside the picture.
void draw_chosen(App& app) {
    const auto lang = app.language;
    if (s.chosen < 0 || s.chosen >= static_cast<int>(s.spots.size())) {
        ImGui::TextWrapped("%s", tr(lang, S::mmap_choose));
        return;
    }
    MissionSpot& sp = s.spots[static_cast<std::size_t>(s.chosen)];
    ImGui::TextUnformatted(spot_label(app, sp).c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    float at[3] = {static_cast<float>(sp.at[0]), static_cast<float>(sp.at[1]), static_cast<float>(sp.at[2])};
    if (ImGui::DragFloat3("##at", at, 0.05f, 0.0f, 0.0f, "%.2f"))
        sp.at = {at[0], at[1], at[2]};
    double floor_y = 0.0;
    if (floor_under(sp.at, &floor_y)) {
        if (std::fabs(floor_y - sp.at[1]) > 0.01 && ImGui::SmallButton(tr(lang, S::mmap_to_floor)))
            sp.at[1] = floor_y;
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", tr(lang, S::mmap_no_floor));
    }
    switch (sp.kind) {
    case Spot::start:
    case Spot::chest:
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##heading", &sp.heading, -180, 180, (std::string(tr(lang, S::mmap_facing)) + ": %d").c_str());
        if (sp.kind == Spot::chest) {
            ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
            ImGui::InputInt(tr(lang, S::mmap_contents), &sp.contents, 1, 10);
            sp.contents = std::max(sp.contents, 0);
        }
        break;
    case Spot::spawner: {
        ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
        ImGui::InputInt(tr(lang, S::mmap_wave), &sp.wave);
        sp.wave = std::clamp(sp.wave, 1, 255);
        int gone = -1;
        for (std::size_t e = 0; e < sp.enemies.size(); ++e) {
            ImGui::PushID(static_cast<int>(e));
            auto& [enemy_class, count] = sp.enemies[e];
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            if (ImGui::BeginCombo("##class", (std::to_string(enemy_class) + ": " + enemy_name(enemy_class)).c_str())) {
                for (const int c : buildable())
                    if (ImGui::Selectable((std::to_string(c) + ": " + enemy_name(c)).c_str(), c == enemy_class))
                        enemy_class = c;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(5 * ImGui::GetFontSize());
            ImGui::InputInt("##count", &count, 1, 1);
            count = std::clamp(count, 1, 255);
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                gone = static_cast<int>(e);
            ImGui::PopID();
        }
        if (gone >= 0 && sp.enemies.size() > 1)
            sp.enemies.erase(sp.enemies.begin() + gone);
        if (ImGui::SmallButton(tr(lang, S::mmap_add_enemy)))
            sp.enemies.emplace_back(buildable().empty() ? 0 : buildable().front(), 1);
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", tr(lang, S::mmap_waves_note));
        ImGui::PopTextWrapPos();
        break;
    }
    case Spot::emblem: {
        const std::string names = tr(lang, S::mmap_badges);
        int kind = sp.emblem;
        std::string zeros;
        for (const char c : names)
            zeros += c == '|' ? '\0' : c;
        zeros += '\0';
        ImGui::SetNextItemWidth(12 * ImGui::GetFontSize());
        if (ImGui::Combo("##badge", &kind, zeros.c_str()))
            sp.emblem = std::clamp(kind, 0, 2);
        break;
    }
    }
    if (ImGui::Button(tr(lang, S::mmap_remove))) {
        s.spots.erase(s.spots.begin() + s.chosen);
        s.chosen = -1;
    }
}

}  // namespace

bool mission_map_unsaved() {
    if (s.key.empty())
        return false;
    if (s.spots.size() != s.saved.size())
        return true;
    for (std::size_t i = 0; i < s.spots.size(); ++i)
        if (mission_spot_line(s.spots[i]) != mission_spot_line(s.saved[i]))
            return true;
    return false;
}

void draw_mission_map(App& app, const Mod& m, const std::string& key, const std::string& world, int room) {
    const auto lang = app.language;
    // read again for another mission, mod, world or room (the room changed in the settings: its places kept)
    if (s.mod != m.name || s.key != key) {
        s.mod = m.name;
        s.key = key;
        s.saved = mod_mission_spots(m.dir, key, &s.read_error);
        s.spots = s.saved;
        s.chosen = s.dragged = s.placing = -1;
        s.world = "\x01";
    }
    if (s.world != world || s.room != room)
        load_room(app, m, world, room);
    if (!s.error.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1), "%s", s.error.c_str());
        return;
    }
    // what to add, then click the floor
    ImGui::TextUnformatted(tr(lang, S::mmap_add));
    const S adds[] = {S::mmap_start, S::mmap_spawner, S::mmap_chest, S::mmap_emblem};
    for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        const bool on = s.placing == i;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(tr(lang, adds[i])))
            s.placing = on ? -1 : i;
        if (on)
            ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr(lang, S::mmap_show_room), &s.show_room);
    ImGui::SameLine();
    ImGui::Checkbox(tr(lang, S::mmap_show_floor), &s.show_floor);
    // saving: all at once, when it reads and builds
    const bool changed = mission_map_unsaved();
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button(tr(lang, S::mmap_save))) {
        std::string why;
        if (set_mod_mission_spots(nitrofs(), m.dir, key, s.spots, &why)) {
            s.saved = mod_mission_spots(m.dir, key, &s.read_error);
            s.spots = s.saved;
            s.chosen = -1;
            app.tell(fmt(lang, S::mmap_saved, {key}) + "  " + tr(lang, S::restart_note), true);
        } else {
            app.tell(fmt(lang, S::save_failed, {why}), false);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(lang, S::mission_undo))) {
        s.spots = s.saved;
        s.chosen = -1;
    }
    ImGui::EndDisabled();
    // what the row says ends it (an empty word too: what follows goes below)
    ImGui::SameLine();
    if (changed)
        ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.35f, 1), "%s", tr(lang, S::mmap_unsaved));
    else
        ImGui::TextDisabled("%s", s.spots.empty() ? tr(lang, S::mmap_none) : "");
    if (s.mod_room) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tr(lang, S::mmap_mod_room));
    }
    // the picture, the chosen place beside it
    const float side = 18 * ImGui::GetFontSize();
    const ImVec2 room_size = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##mappicture", ImVec2(room_size.x - side, room_size.y));
    const ImVec2 picture = ImGui::GetContentRegionAvail();
    draw_room(app, ImVec2(picture.x, picture.y - 2.2f * ImGui::GetTextLineHeightWithSpacing()));
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr(lang, S::mmap_hint));
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##mapchosen", ImVec2(0, room_size.y), ImGuiChildFlags_Borders);
    {
        WrappedTexts wrap_chosen;
        draw_chosen(app);
    }
    ImGui::EndChild();
}

}  // namespace studio
