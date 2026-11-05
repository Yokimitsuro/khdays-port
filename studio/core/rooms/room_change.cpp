#include "core/rooms/room_change.h"

#include "core/files/file_write.h"
#include "core/models/model_files.h"
#include "core/files/paths.h"

#include "khdays/assets/gltf.h"
#include "khdays/assets/gltf_export.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace studio {

namespace {

namespace assets = khdays::assets;

constexpr const char* kHeading = "khdays-studio room 1";

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr)
        *why = reason;
    return false;
}

std::string use_word(ObjectUse u) {
    return u == ObjectUse::model ? "model" : u == ObjectUse::hide ? "hide" : "keep";
}

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names)
        out += (out.empty() ? "" : ",") + n;
    return out;
}

std::vector<std::string> split_names(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        if (ch == ',') {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

using M4 = std::array<double, 16>;  // column-major

M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            double s = 0;
            for (int k = 0; k < 4; ++k)
                s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    }
    return r;
}

M4 identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// scale, then turned about x, y, z (degrees), then moved
M4 placement_matrix(const RoomPlacement& p) {
    const double k = 3.14159265358979323846 / 180.0;
    const double ax = p.turn[0] * k, ay = p.turn[1] * k, az = p.turn[2] * k;
    const M4 rx{1, 0, 0, 0, 0, std::cos(ax), std::sin(ax), 0, 0, -std::sin(ax), std::cos(ax), 0, 0, 0, 0, 1};
    const M4 ry{std::cos(ay), 0, -std::sin(ay), 0, 0, 1, 0, 0, std::sin(ay), 0, std::cos(ay), 0, 0, 0, 0, 1};
    const M4 rz{std::cos(az), std::sin(az), 0, 0, -std::sin(az), std::cos(az), 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    M4 s = identity();
    s[0] = s[5] = s[10] = p.scale;
    M4 t = identity();
    t[12] = p.move[0];
    t[13] = p.move[1];
    t[14] = p.move[2];
    return mul(t, mul(rz, mul(ry, mul(rx, s))));
}

M4 to_m4(const std::array<float, 16>& f) {
    M4 m{};
    for (std::size_t k = 0; k < 16; ++k)
        m[k] = f[k];
    return m;
}

std::array<float, 16> to_floats(const M4& m) {
    std::array<float, 16> f{};
    for (std::size_t k = 0; k < 16; ++k)
        f[k] = static_cast<float>(m[k]);
    return f;
}

std::array<float, 3> apply(const M4& m, const std::array<float, 3>& v, double w) {
    std::array<float, 3> r{};
    for (int row = 0; row < 3; ++row) {
        r[static_cast<std::size_t>(row)] =
            static_cast<float>(m[static_cast<std::size_t>(row)] * v[0] + m[static_cast<std::size_t>(4 + row)] * v[1] +
                               m[static_cast<std::size_t>(8 + row)] * v[2] + m[static_cast<std::size_t>(12 + row)] * w);
    }
    return r;
}

std::string member_label(const std::string& code, int member) {
    return world_path(code) + "#" + std::to_string(member);
}

// The texture files a written room model uses: `<model>-room-<n>.png`.
std::string texture_file(const std::string& model, std::size_t n) {
    return model + "-room-" + std::to_string(n) + ".png";
}

// What the studio wrote for a world's rooms (paths under the mod, '/'
// separators), so that only those are ever removed -- never a model the
// modder replaced by hand.
std::filesystem::path written_list(const std::filesystem::path& mod_dir, const std::string& code) {
    return rooms_folder(mod_dir, code) / "written.list";
}

std::set<std::string> read_written(const std::filesystem::path& mod_dir, const std::string& code) {
    std::set<std::string> out;
    std::ifstream in(written_list(mod_dir, code), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            out.insert(line);
    }
    return out;
}

// Removes a written file and the folders it leaves empty under the mod.
void remove_written(const std::filesystem::path& mod_dir, const std::string& relative) {
    std::error_code ec;
    const auto file = mod_dir / path_u8(relative);
    if (!inside(mod_dir, file))
        return;
    std::filesystem::remove(file, ec);
    for (auto f = file.parent_path(); f != mod_dir && inside(mod_dir, f); f = f.parent_path()) {
        if (!std::filesystem::is_directory(f, ec) || !std::filesystem::is_empty(f, ec))
            break;
        std::filesystem::remove(f, ec);
    }
}

}  // namespace

bool write_model_as(const std::filesystem::path& mod_dir, const std::filesystem::path& gltf, const std::string& name,
                    const std::string& listed, const assets::ImportedModel& model, std::set<std::string>* written,
                    std::string* why) {
    std::map<std::string, assets::GltfTextureRef> refs;
    std::size_t n = 0;
    for (const auto& [texture, decoded] : model.textures) {
        if (decoded.rgba.empty() || decoded.width <= 0 || decoded.height <= 0)
            continue;
        const std::string file = texture_file(name, n++);
        const auto result = write_into_mod(
            mod_dir, gltf.parent_path() / path_u8(file),
            [&](const std::filesystem::path& temporary) -> std::string {
                const char* e =
                    khdays_png_write(u8(temporary).c_str(), decoded.rgba.data(), decoded.width, decoded.height);
                return e != nullptr ? std::string(e) : std::string();
            },
            [](const std::filesystem::path&) { return std::string(); });
        if (!result.ok)
            return fail_with(why, file + ": " + result.why);
        written->insert(listed + file);
        refs[texture] = {file, decoded.width, decoded.height,
                         assets::texels_translucent(decoded.rgba.data(), decoded.rgba.size() / 4)};
    }
    // a model that moves by itself written as its scene, its skeleton and animation kept
    const std::string text = assets::moves_by_itself(model) ? assets::scene_to_gltf(assets::scene_of(model, refs))
                                                            : assets::to_gltf(model.model, refs);
    const auto result = write_into_mod(
        mod_dir, gltf,
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
            return out ? std::string() : std::string("could not be written");
        },
        [&](const std::filesystem::path& temporary) -> std::string {
            // read as the port reads it; the textures are beside the destination, not the temporary
            try {
                (void)assets::import_gltf(temporary);
            } catch (const std::exception& e) {
                return std::string("the port cannot read it: ") + e.what();
            }
            return {};
        });
    if (!result.ok)
        return fail_with(why, name + ".gltf: " + result.why);
    written->insert(listed + name + ".gltf");
    return true;
}

namespace {

// Writes `model` (placed) as the glTF replacing `key`'s model, its textures beside it.
bool write_room_model(const std::filesystem::path& mod_dir, const std::string& key, const assets::ImportedModel& model,
                      std::set<std::string>* written, std::string* why) {
    return write_model_as(mod_dir, mod_dir / "graphics" / path_u8(key + ".gltf"), key.substr(key.rfind('/') + 1),
                          "graphics/" + key.substr(0, key.rfind('/') + 1), model, written, why);
}

// A model's joints at rest: each palette entry its joint's global times its inverse bind.
std::map<std::uint32_t, M4> rest_palette(const assets::ImportedModel& m) {
    std::map<std::uint32_t, M4> palette;
    for (const auto& j : m.joints)
        palette[j.palette_index] = mul(to_m4(j.world), to_m4(j.inverse_bind));
    return palette;
}

// Where a vertex is drawn: through its joints when the model keeps its skeleton, else as it is.
std::array<float, 3> posed(const std::map<std::uint32_t, M4>& palette, const assets::NeutralVertex& v) {
    if (palette.empty())
        return v.position;
    std::array<float, 3> pos{0, 0, 0};
    float total = 0;
    for (std::size_t k = 0; k < 4; ++k) {
        const auto found = palette.find(v.joints[k]);
        if (v.weights[k] <= 0 || found == palette.end())
            continue;
        const auto q = apply(found->second, v.position, 1);
        for (std::size_t c = 0; c < 3; ++c)
            pos[c] += v.weights[k] * q[c];
        total += v.weights[k];
    }
    if (total <= 0)
        return v.position;
    for (auto& c : pos)
        c /= total;
    return pos;
}

}  // namespace

std::filesystem::path rooms_folder(const std::filesystem::path& mod_dir, const std::string& code) {
    return mod_dir / "studio" / "rooms" / path_u8(code);
}

std::filesystem::path change_file(const std::filesystem::path& mod_dir, const std::string& code, int room) {
    return rooms_folder(mod_dir, code) / (std::to_string(room) + ".txt");
}

std::filesystem::path change_models(const std::filesystem::path& mod_dir, const std::string& code, int room) {
    return rooms_folder(mod_dir, code) / std::to_string(room);
}

std::string change_text(const RoomReplacement& r) {
    std::ostringstream o;
    o << kHeading << "\n";
    o << "world " << r.world << "\n";
    o << "room " << r.room << "\n";
    o << "model " << r.model << "\n";
    if (!r.kh2.empty())
        o << "kh2 " << r.kh2 << "\n";
    if (!r.source.empty())
        o << "source " << r.source << "\n";
    if (!r.collision.empty())
        o << "collision " << r.collision << "\n";
    if (r.keep_collision)
        o << "collision-kept\n";
    o << "scale " << r.place.scale << "\n";
    o << "turn " << r.place.turn[0] << " " << r.place.turn[1] << " " << r.place.turn[2] << "\n";
    o << "move " << r.place.move[0] << " " << r.place.move[1] << " " << r.place.move[2] << "\n";
    if (r.zone >= 0)
        o << "zone " << r.zone << "\n";
    o << "ground-limit " << r.ground_limit << "\n";
    for (const auto& [member, use] : r.objects)
        o << "object " << member << " " << use_word(use) << "\n";
    for (const auto& [material, names] : r.names_by_material)
        o << "material-names " << joined(names) << " " << material << "\n";
    for (const auto& [triangle, names] : r.names_by_triangle)
        o << "triangle-names " << triangle << " " << joined(names) << "\n";
    for (const auto& name : r.drop_original)
        o << "drop-original " << name << "\n";
    for (const auto& [name, at] : r.markers)
        o << "marker " << name << " " << at[0] << " " << at[1] << " " << at[2] << " " << at[3] << "\n";
    for (const auto& [key, at] : r.gate_points) {
        o << "gate-point " << key.first << " " << key.second << " " << at[0] << " " << at[1] << " " << at[2] << "\n";
    }
    return o.str();
}

bool read_change(const std::filesystem::path& file, RoomReplacement* out, std::string* why) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return fail_with(why, "cannot open " + u8(file));
    RoomReplacement r;
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (number == 1) {
            if (line != kHeading)
                return fail_with(why, u8(file) + ": not a room change of this studio");
            continue;
        }
        if (line.empty() || line[0] == '#')
            continue;
        const auto space = line.find(' ');
        const std::string word = line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        std::istringstream v(rest);
        const std::string at = u8(file) + " line " + std::to_string(number);
        if (word == "world")
            r.world = rest;
        else if (word == "room")
            v >> r.room;
        else if (word == "model")
            r.model = rest;
        else if (word == "kh2")
            r.kh2 = rest;
        else if (word == "source")
            r.source = rest;
        else if (word == "collision")
            r.collision = rest;
        else if (word == "collision-kept")
            r.keep_collision = true;
        else if (word == "scale")
            v >> r.place.scale;
        else if (word == "turn")
            v >> r.place.turn[0] >> r.place.turn[1] >> r.place.turn[2];
        else if (word == "move")
            v >> r.place.move[0] >> r.place.move[1] >> r.place.move[2];
        else if (word == "zone")
            v >> r.zone;
        else if (word == "ground-limit")
            v >> r.ground_limit;
        else if (word == "object") {
            int member = -1;
            std::string use;
            v >> member >> use;
            if (use != "model" && use != "hide" && use != "keep")
                return fail_with(why, at + ": an object is model, hide or keep");
            r.objects[member] = use == "model" ? ObjectUse::model : use == "hide" ? ObjectUse::hide : ObjectUse::keep;
        } else if (word == "material-names") {
            const auto gap = rest.find(' ');
            if (gap == std::string::npos)
                return fail_with(why, at + ": names, then the material");
            r.names_by_material[rest.substr(gap + 1)] = split_names(rest.substr(0, gap));
        } else if (word == "triangle-names") {
            std::size_t t = 0;
            std::string names;
            v >> t >> names;
            r.names_by_triangle[t] = split_names(names);
        } else if (word == "drop-original") {
            r.drop_original.insert(rest);
        } else if (word == "marker") {
            std::string name;
            std::array<std::int32_t, 4> place{};
            v >> name >> place[0] >> place[1] >> place[2] >> place[3];
            r.markers[name] = place;
        } else if (word == "gate-point") {
            int key = 0, part = 0;
            std::array<std::int32_t, 3> place{};
            v >> key >> part >> place[0] >> place[1] >> place[2];
            if (part < 0 || part > 4)
                return fail_with(why, at + ": a gate model's point is 0 to 4");
            r.gate_points[{key, part}] = place;
        } else {
            return fail_with(why, at + ": \"" + word + "\" is not known");
        }
        if (v.fail() && word != "world" && word != "model" && word != "collision" && word != "collision-kept" &&
            word != "material-names" && word != "drop-original") {
            return fail_with(why, at + ": not a number where one goes");
        }
    }
    if (r.world.empty() || r.room < 0 || r.model.empty())
        return fail_with(why, u8(file) + ": no world, room or model");
    if (!(r.place.scale > 0))
        return fail_with(why, u8(file) + ": the scale must be above 0");
    *out = std::move(r);
    return true;
}

std::vector<RoomReplacement> changes_of(const std::filesystem::path& mod_dir, const std::string& code,
                                        std::vector<std::string>* problems) {
    std::vector<RoomReplacement> out;
    std::error_code ec;
    const auto folder = rooms_folder(mod_dir, code);
    if (!std::filesystem::is_directory(folder, ec))
        return out;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (!e.is_regular_file() || lower(u8(e.path().extension())) != ".txt")
            continue;
        RoomReplacement r;
        std::string why;
        if (read_change(e.path(), &r, &why)) {
            out.push_back(std::move(r));
        } else if (problems != nullptr) {
            problems->push_back(why);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.room < b.room; });
    return out;
}

std::vector<std::string> changed_worlds(const std::filesystem::path& mod_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    const auto folder = mod_dir / "studio" / "rooms";
    if (!std::filesystem::is_directory(folder, ec))
        return out;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (e.is_directory())
            out.push_back(u8(e.path().filename()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

RoomAlignment align_with_room(const std::vector<std::array<float, 3>>& room_points,
                              const assets::ImportedModel& model) {
    RoomAlignment best;
    if (room_points.empty())
        return best;
    // the model's points as the file places them, a sample of them
    assets::ImportedModel base = model;
    place_model(&base, RoomPlacement{});
    std::vector<std::array<float, 3>> points;
    std::size_t total = 0;
    for (const auto& mesh : base.model.meshes)
        total += mesh.vertices.size();
    const std::size_t step = std::max<std::size_t>(1, total / 4000);
    std::size_t at = 0;
    for (const auto& mesh : base.model.meshes) {
        for (const auto& v : mesh.vertices) {
            if (at++ % step == 0)
                points.push_back(v.position);
        }
    }
    if (points.empty())
        return best;
    // the room's points in cells of a hundredth of a unit (the game's own places: within 1/4096)
    const double cell = 0.01;
    auto key = [&](double x, double y, double z) {
        const auto ix = static_cast<std::int64_t>(std::floor(x / cell)),
                   iy = static_cast<std::int64_t>(std::floor(y / cell)),
                   iz = static_cast<std::int64_t>(std::floor(z / cell));
        return (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791);
    };
    std::set<std::int64_t> cells;
    for (const auto& q : room_points)
        cells.insert(key(q[0], q[1], q[2]));
    auto on_room = [&](const std::array<float, 3>& q) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (cells.count(key(q[0] + dx * cell, q[1] + dy * cell, q[2] + dz * cell)) != 0)
                        return true;
                }
            }
        }
        return false;
    };
    // every quarter-turn orientation once (24), as the turns that make it
    std::vector<std::array<float, 3>> turns;
    std::set<std::array<int, 9>> seen;
    for (int x = 0; x < 360; x += 90) {
        for (int y = 0; y < 360; y += 90) {
            for (int z = 0; z < 360; z += 90) {
                RoomPlacement p;
                p.turn = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                const M4 m = placement_matrix(p);
                std::array<int, 9> r{};
                for (int c = 0; c < 3; ++c) {
                    for (int row = 0; row < 3; ++row)
                        r[static_cast<std::size_t>(c * 3 + row)] =
                            static_cast<int>(std::lround(m[static_cast<std::size_t>(c * 4 + row)]));
                }
                if (seen.insert(r).second)
                    turns.push_back(p.turn);
            }
        }
    }
    for (const double scale : {1.0, 0.01, 100.0, 0.1, 10.0}) {
        for (const auto& turn : turns) {
            RoomPlacement p;
            p.scale = static_cast<float>(scale);
            p.turn = turn;
            const M4 m = placement_matrix(p);
            std::size_t hits = 0;
            for (const auto& q : points)
                hits += on_room(apply(m, q, 1)) ? 1 : 0;
            const double share = static_cast<double>(hits) / static_cast<double>(points.size());
            if (scale == 1.0 && turn[0] == 0 && turn[1] == 0 && turn[2] == 0)
                best.as_is = share;
            if (share > best.share + 1e-9) {
                best.share = share;
                best.place = p;
            }
        }
    }
    const bool moved =
        best.place.scale != 1.0f || best.place.turn[0] != 0 || best.place.turn[1] != 0 || best.place.turn[2] != 0;
    if (moved && best.share >= 0.05 && best.share >= 2 * best.as_is) {
        best.verdict = RoomAlignment::turned;
    } else if (best.as_is >= 0.03) {
        best.verdict = RoomAlignment::in_place;
        best.place = RoomPlacement{};
        best.share = best.as_is;
    } else {
        best.verdict = RoomAlignment::unrelated;
    }
    return best;
}

namespace {

// A KH2 room's middle from above and its floor's height under it, from its still meshes but its skies
// (its pieces may stand far off, waiting); false when it has none.
bool kh2_floor_middle(const assets::ImportedModel& kh2, float* x, float* z, float* floor_y) {
    std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    struct Tri {
        std::array<float, 3> a, b, c;
    };
    std::vector<Tri> ups;
    for (const auto& mesh : kh2.model.meshes) {
        if (mesh.sky)
            continue;
        auto still = [](const assets::NeutralVertex& v) { return v.joints[0] == 0 && v.weights[0] >= 0.999f; };
        for (const auto& v : mesh.vertices) {
            if (!still(v))
                continue;
            for (std::size_t c = 0; c < 3; ++c)
                lo[c] = std::min(lo[c], v.position[c]), hi[c] = std::max(hi[c], v.position[c]);
        }
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const auto& p = mesh.vertices[mesh.indices[i]];
            const auto& q = mesh.vertices[mesh.indices[i + 1]];
            const auto& r = mesh.vertices[mesh.indices[i + 2]];
            if (!still(p) || !still(q) || !still(r))
                continue;
            const float e0[3] = {q.position[0] - p.position[0], q.position[1] - p.position[1],
                                 q.position[2] - p.position[2]};
            const float e1[3] = {r.position[0] - p.position[0], r.position[1] - p.position[1],
                                 r.position[2] - p.position[2]};
            const float n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                                e0[0] * e1[1] - e0[1] * e1[0]};
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0 && n[1] / len > 0.9f)
                ups.push_back({p.position, q.position, r.position});
        }
    }
    if (lo[0] > hi[0])
        return false;
    const float kx = (lo[0] + hi[0]) / 2, kz = (lo[2] + hi[2]) / 2;
    // the lowest upward triangle holding the middle, else the nearest one's middle
    float y = lo[1];
    bool held = false;
    float nearest = 1e30f;
    for (const auto& t : ups) {
        auto side = [&](const std::array<float, 3>& u, const std::array<float, 3>& w) {
            return (w[0] - u[0]) * (kz - u[2]) - (w[2] - u[2]) * (kx - u[0]);
        };
        const float s0 = side(t.a, t.b), s1 = side(t.b, t.c), s2 = side(t.c, t.a);
        const bool inside = (s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0);
        const float cy = (t.a[1] + t.b[1] + t.c[1]) / 3;
        if (inside) {
            if (!held || cy < y)
                y = cy;
            held = true;
        } else if (!held) {
            const float dx = (t.a[0] + t.b[0] + t.c[0]) / 3 - kx, dz = (t.a[2] + t.b[2] + t.c[2]) / 3 - kz;
            if (dx * dx + dz * dz < nearest) {
                nearest = dx * dx + dz * dz;
                y = cy;
            }
        }
    }
    *x = kx;
    *z = kz;
    *floor_y = y;
    return true;
}

// A game room's middle from above and its ground's height under it (the collision's 20.12 units): the
// ground holding the middle, else the nearest ground's middle; false when the room has no faces.
bool room_ground_middle(const RoomCollision& room, double* x, double* z, double* ground) {
    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
    for (const auto& list : room.faces) {
        for (const auto& f : list) {
            for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
                for (std::size_t c = 0; c < 3; ++c)
                    lo[c] = std::min(lo[c], static_cast<double>(f.v[v][c])),
                    hi[c] = std::max(hi[c], static_cast<double>(f.v[v][c]));
            }
        }
    }
    if (lo[0] > hi[0])
        return false;
    const double dx = (lo[0] + hi[0]) / 2, dz = (lo[2] + hi[2]) / 2;
    double y = lo[1];
    if (!ground_under(room, dx, dz, hi[1] + 4096.0, &y)) {
        double best = 1e300;
        for (const auto& g : room.faces[kGround]) {
            double cx = 0, cy = 0, cz = 0;
            for (std::size_t v = 0; v < g.corners && v < 4; ++v)
                cx += g.v[v][0], cy += g.v[v][1], cz += g.v[v][2];
            const double k = g.corners > 0 ? 1.0 / g.corners : 0;
            const double d = (cx * k - dx) * (cx * k - dx) + (cz * k - dz) * (cz * k - dz);
            if (d < best) {
                best = d;
                y = cy * k;
            }
        }
    }
    *x = dx;
    *z = dz;
    *ground = y;
    return true;
}

}  // namespace

RoomAlignment kh2_alignment(const std::vector<std::array<float, 3>>& room_points, const RoomCollision& room,
                            const assets::ImportedModel& kh2, float units) {
    RoomAlignment a = align_with_room(room_points, kh2);
    if (a.verdict != RoomAlignment::unrelated)
        return a;
    a = RoomAlignment{};
    a.verdict = RoomAlignment::kh2_units;
    a.place.scale = units;  // KH2's units are a hundredth of the game's (the Usual Spot, room 0 of Twilight Town)
    float kx = 0, kz = 0, floor_y = 0;
    double dx = 0, dz = 0, ground = 0;
    if (!kh2_floor_middle(kh2, &kx, &kz, &floor_y) || !room_ground_middle(room, &dx, &dz, &ground))
        return a;
    const float s = a.place.scale;
    a.place.move = {static_cast<float>(dx / 4096.0) - s * kx, static_cast<float>(ground / 4096.0) - s * floor_y,
                    static_cast<float>(dz / 4096.0) - s * kz};
    return a;
}

std::array<float, 3> placed_point(const RoomPlacement& place, const std::array<float, 3>& v) {
    return apply(placement_matrix(place), v, 1);
}

std::array<float, 3> unplaced_point(const RoomPlacement& place, const std::array<float, 3>& v) {
    // the matrix's columns are the turned axes times the scale: back along each, over the scale squared
    const M4 m = placement_matrix(place);
    const double d[3] = {v[0] - m[12], v[1] - m[13], v[2] - m[14]};
    const double s2 = static_cast<double>(place.scale) * place.scale;
    std::array<float, 3> out{};
    for (std::size_t c = 0; c < 3; ++c)
        out[c] = static_cast<float>((m[c * 4] * d[0] + m[c * 4 + 1] * d[1] + m[c * 4 + 2] * d[2]) / s2);
    return out;
}

bool same_place(const RoomPlacement& a, const RoomPlacement& b) {
    return a.scale == b.scale && a.turn == b.turn && a.move == b.move;
}

bool placed_model(const std::filesystem::path& model, const RoomPlacement& place, assets::ImportedModel* out,
                  std::string* why) {
    assets::ImportedModel m;
    if (!import_model(model, &m, why))
        return false;
    place_model(&m, place);
    *out = std::move(m);
    return true;
}

void place_model(assets::ImportedModel* model, const RoomPlacement& place) {
    assets::ImportedModel& m = *model;
    if (!m.animations.empty()) {
        // a model with its own animation keeps its skeleton: the place goes on its roots and joints,
        // the meshes loading made still are placed
        const M4 p = placement_matrix(place);
        for (auto& node : m.nodes) {
            if (node.parent < 0)
                node.local = to_floats(mul(p, to_m4(node.local)));
        }
        std::set<std::uint32_t> skinned;
        for (auto& j : m.joints) {
            j.world = to_floats(mul(p, to_m4(j.world)));
            skinned.insert(j.palette_index);
        }
        for (auto& mesh : m.model.meshes) {
            for (auto& v : mesh.vertices) {
                bool on_joint = false;
                for (std::size_t k = 0; k < 4; ++k)
                    on_joint = on_joint || (v.weights[k] > 0 && skinned.count(v.joints[k]) != 0);
                if (!on_joint) {
                    v.position = apply(p, v.position, 1);
                    v.normal = apply(p, v.normal, 0);
                }
            }
        }
        return;
    }
    // the palette as the file poses it: each joint's global times its inverse bind
    std::map<std::uint32_t, M4> palette;
    for (const auto& j : m.joints) {
        M4 world{}, ibm{};
        for (std::size_t k = 0; k < 16; ++k)
            world[k] = j.world[k], ibm[k] = j.inverse_bind[k];
        palette[j.palette_index] = mul(world, ibm);
    }
    const M4 p = placement_matrix(place);
    for (auto& mesh : m.model.meshes) {
        for (auto& v : mesh.vertices) {
            std::array<float, 3> pos{0, 0, 0}, nrm{0, 0, 0};
            float total = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                if (v.weights[k] <= 0)
                    continue;
                const auto found = palette.find(v.joints[k]);
                const M4& s = found != palette.end() ? found->second : identity();
                const auto q = apply(s, v.position, 1), n = apply(s, v.normal, 0);
                for (std::size_t c = 0; c < 3; ++c)
                    pos[c] += v.weights[k] * q[c], nrm[c] += v.weights[k] * n[c];
                total += v.weights[k];
            }
            if (total <= 0) {
                pos = v.position;
                nrm = v.normal;
            } else if (std::fabs(total - 1.0f) > 1e-4f) {
                for (auto& c : pos)
                    c /= total;
            }
            v.position = apply(p, pos, 1);
            v.normal = apply(p, nrm, 0);
            v.joints = {0, 0, 0, 0};
            v.weights = {1, 0, 0, 0};
        }
    }
    m.joints.clear();
    m.model.palette.clear();
    m.model.palette_bones.clear();
    m.model.object_matrices.clear();
    m.model.object_names.clear();
    m.model.object_parents.clear();
    m.model.skinning = nullptr;
}

CollisionSource collision_source(const assets::ImportedModel& placed, bool visible) {
    CollisionSource s;
    // a model that keeps its skeleton (its own animation) at rest: each vertex through its joints
    const auto palette = rest_palette(placed);
    std::vector<assets::NeutralMesh> at_rest;
    if (!palette.empty()) {
        at_rest = placed.model.meshes;
        for (auto& mesh : at_rest) {
            for (auto& v : mesh.vertices)
                v.position = posed(palette, v);
        }
    }
    for (const auto& mesh : palette.empty() ? placed.model.meshes : at_rest) {
        const std::string material = !mesh.material_name.empty() ? mesh.material_name : mesh.texture_name;
        const char passable = visible && mesh.alpha_mode == assets::NeutralMesh::AlphaMode::blend ? 1 : 0;
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const auto& a = mesh.vertices[mesh.indices[i]];
            const auto& b = mesh.vertices[mesh.indices[i + 1]];
            const auto& c = mesh.vertices[mesh.indices[i + 2]];
            s.triangles.push_back({a.position, b.position, c.position});
            s.materials.push_back(material);
            s.passable.push_back(passable);
            // drawn both sides (every FBX mesh is): a steep face stops from both
            // sides too; a floor's other side would be a ceiling under it, so not
            const float e0[3] = {b.position[0] - a.position[0], b.position[1] - a.position[1],
                                 b.position[2] - a.position[2]};
            const float e1[3] = {c.position[0] - a.position[0], c.position[1] - a.position[1],
                                 c.position[2] - a.position[2]};
            const float n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                                e0[0] * e1[1] - e0[1] * e1[0]};
            const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (mesh.double_sided && length > 0 && std::fabs(n[1] / length) < 0.5f) {
                s.triangles.push_back({a.position, c.position, b.position});
                s.materials.push_back(material);
                s.passable.push_back(passable);
            }
        }
    }
    return s;
}

bool RoomZone::holds(float x, float z) const {
    const auto i = static_cast<std::int64_t>(std::floor(x / cell)) - x0;
    const auto k = static_cast<std::int64_t>(std::floor(z / cell)) - z0;
    if (i < 0 || k < 0 || i >= width || k >= depth)
        return false;
    return in[static_cast<std::size_t>(k * width + i)] != 0;
}

namespace {

// The cells of `zone` that a room's faces cover from above: their edges (a wall from above is one) and
// the cells whose middles they hold.
std::vector<char> covered_cells(const RoomCollision& room, const RoomZone& zone) {
    const float c = zone.cell;
    const int w = zone.width, d = zone.depth;
    std::vector<char> covered(static_cast<std::size_t>(w) * static_cast<std::size_t>(d), 0);
    auto mark = [&](double x, double z) {
        const auto i = static_cast<std::int64_t>(std::floor(x / c)) - zone.x0;
        const auto k = static_cast<std::int64_t>(std::floor(z / c)) - zone.z0;
        if (i >= 0 && k >= 0 && i < w && k < d)
            covered[static_cast<std::size_t>(k * w + i)] = 1;
    };
    for (const auto& list : room.faces) {
        for (const auto& f : list) {
            const std::size_t n = std::min<std::size_t>(f.corners, 4);
            if (n < 3)
                continue;
            double px[4], pz[4];
            for (std::size_t v = 0; v < n; ++v)
                px[v] = f.v[v][0] / 4096.0, pz[v] = f.v[v][2] / 4096.0;
            for (std::size_t v = 0; v < n; ++v) {
                const std::size_t u = (v + 1) % n;
                const double length = std::hypot(px[u] - px[v], pz[u] - pz[v]);
                const int steps = static_cast<int>(length / (c / 2)) + 1;
                for (int s = 0; s <= steps; ++s) {
                    const double t = static_cast<double>(s) / steps;
                    mark(px[v] + t * (px[u] - px[v]), pz[v] + t * (pz[u] - pz[v]));
                }
            }
            double flo[2] = {px[0], pz[0]}, fhi[2] = {px[0], pz[0]};
            for (std::size_t v = 1; v < n; ++v) {
                flo[0] = std::min(flo[0], px[v]), fhi[0] = std::max(fhi[0], px[v]);
                flo[1] = std::min(flo[1], pz[v]), fhi[1] = std::max(fhi[1], pz[v]);
            }
            for (auto i = static_cast<std::int64_t>(std::floor(flo[0] / c)); i * c <= fhi[0]; ++i) {
                for (auto k = static_cast<std::int64_t>(std::floor(flo[1] / c)); k * c <= fhi[1]; ++k) {
                    const double x = (static_cast<double>(i) + 0.5) * c, z = (static_cast<double>(k) + 0.5) * c;
                    bool above = false, below = false;
                    for (std::size_t v = 0; v < n; ++v) {
                        const std::size_t u = (v + 1) % n;
                        const double side = (px[u] - px[v]) * (z - pz[v]) - (pz[u] - pz[v]) * (x - px[v]);
                        above = above || side > 0;
                        below = below || side < 0;
                    }
                    if (!(above && below))
                        mark(x, z);
                }
            }
        }
    }
    return covered;
}

// The cells outside what the covered ones close in: those reached from the zone's edges.
std::vector<char> outside_cells(const std::vector<char>& covered, int w, int d) {
    std::vector<char> outside(covered.size(), 0);
    std::vector<std::size_t> stack;
    auto reach = [&](int i, int k) {
        const auto at = static_cast<std::size_t>(k) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i);
        if (covered[at] == 0 && outside[at] == 0) {
            outside[at] = 1;
            stack.push_back(at);
        }
    };
    for (int i = 0; i < w; ++i)
        reach(i, 0), reach(i, d - 1);
    for (int k = 0; k < d; ++k)
        reach(0, k), reach(w - 1, k);
    while (!stack.empty()) {
        const std::size_t at = stack.back();
        stack.pop_back();
        const int i = static_cast<int>(at % static_cast<std::size_t>(w)),
                  k = static_cast<int>(at / static_cast<std::size_t>(w));
        if (i > 0)
            reach(i - 1, k);
        if (i + 1 < w)
            reach(i + 1, k);
        if (k > 0)
            reach(i, k - 1);
        if (k + 1 < d)
            reach(i, k + 1);
    }
    return outside;
}

// Each cell's distance (in cells) to the nearest cell not outside, in two sweeps.
std::vector<float> distance_cells(const std::vector<char>& outside, int w, int d) {
    const float far = 1e30f;
    std::vector<float> dist(outside.size(), far);
    for (std::size_t at = 0; at < outside.size(); ++at) {
        if (outside[at] == 0)
            dist[at] = 0;
    }
    const float diagonal = std::sqrt(2.0f);
    auto relax = [&](int i, int k, int di, int dk, float step) {
        const int ni = i + di, nk = k + dk;
        if (ni < 0 || nk < 0 || ni >= w || nk >= d)
            return;
        float& here = dist[static_cast<std::size_t>(k) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i)];
        here = std::min(
            here,
            dist[static_cast<std::size_t>(nk) * static_cast<std::size_t>(w) + static_cast<std::size_t>(ni)] + step);
    };
    for (int k = 0; k < d; ++k) {
        for (int i = 0; i < w; ++i) {
            relax(i, k, -1, 0, 1), relax(i, k, 0, -1, 1), relax(i, k, -1, -1, diagonal), relax(i, k, 1, -1, diagonal);
        }
    }
    for (int k = d - 1; k >= 0; --k) {
        for (int i = w - 1; i >= 0; --i) {
            relax(i, k, 1, 0, 1), relax(i, k, 0, 1, 1), relax(i, k, 1, 1, diagonal), relax(i, k, -1, 1, diagonal);
        }
    }
    return dist;
}

}  // namespace

RoomZone room_zone(const RoomCollision& room, float margin) {
    RoomZone zone;
    const float c = zone.cell;
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    for (const auto& list : room.faces) {
        for (const auto& f : list) {
            for (std::size_t v = 0; v < f.corners && v < 4; ++v) {
                const double x = f.v[v][0] / 4096.0, z = f.v[v][2] / 4096.0;
                lo[0] = std::min(lo[0], x), hi[0] = std::max(hi[0], x);
                lo[1] = std::min(lo[1], z), hi[1] = std::max(hi[1], z);
            }
        }
    }
    if (lo[0] > hi[0])
        return zone;
    const double pad = std::max(0.0f, margin) + 2 * c;
    zone.x0 = static_cast<std::int64_t>(std::floor((lo[0] - pad) / c));
    zone.z0 = static_cast<std::int64_t>(std::floor((lo[1] - pad) / c));
    zone.width = static_cast<int>(std::floor((hi[0] + pad) / c) - zone.x0 + 1);
    zone.depth = static_cast<int>(std::floor((hi[1] + pad) / c) - zone.z0 + 1);
    // the cells the room covers or closes in, and those within the margin of them
    const auto dist =
        distance_cells(outside_cells(covered_cells(room, zone), zone.width, zone.depth), zone.width, zone.depth);
    zone.in.assign(dist.size(), 0);
    const float reach_cells = std::max(0.0f, margin) / c;
    for (std::size_t at = 0; at < dist.size(); ++at)
        zone.in[at] = dist[at] <= reach_cells ? 1 : 0;
    return zone;
}

namespace {

// The model's drawn triangles (its skies left out), each corner where it is drawn at rest.
std::vector<std::array<std::array<float, 3>, 3>> drawn_triangles(const assets::ImportedModel& placed) {
    const auto palette = rest_palette(placed);
    std::vector<std::array<std::array<float, 3>, 3>> out;
    for (const auto& mesh : placed.model.meshes) {
        if (mesh.sky)
            continue;
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            out.push_back({posed(palette, mesh.vertices[mesh.indices[i]]),
                           posed(palette, mesh.vertices[mesh.indices[i + 1]]),
                           posed(palette, mesh.vertices[mesh.indices[i + 2]])});
        }
    }
    return out;
}

}  // namespace

SurfaceMap surface_map(const assets::ImportedModel& placed) {
    SurfaceMap s;
    for (const auto& t : drawn_triangles(placed)) {
        const float e0[3] = {t[1][0] - t[0][0], t[1][1] - t[0][1], t[1][2] - t[0][2]};
        const float e1[3] = {t[2][0] - t[0][0], t[2][1] - t[0][1], t[2][2] - t[0][2]};
        const float n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                            e0[0] * e1[1] - e0[1] * e1[0]};
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(length > 0) || std::fabs(n[1] / length) <= 0.3f)
            continue;
        const std::size_t at = s.triangles.size();
        s.triangles.push_back(t);
        s.up.push_back(n[1] > 0 ? 1 : 0);
        const float lo_x = std::min({t[0][0], t[1][0], t[2][0]}), hi_x = std::max({t[0][0], t[1][0], t[2][0]});
        const float lo_z = std::min({t[0][2], t[1][2], t[2][2]}), hi_z = std::max({t[0][2], t[1][2], t[2][2]});
        if (hi_x - lo_x > 64 || hi_z - lo_z > 64) {
            s.wide.push_back(at);  // a ground far and wide: asked every time, not cell by cell
            continue;
        }
        for (auto i = static_cast<std::int64_t>(std::floor(lo_x)); i <= static_cast<std::int64_t>(std::floor(hi_x));
             ++i) {
            for (auto k = static_cast<std::int64_t>(std::floor(lo_z)); k <= static_cast<std::int64_t>(std::floor(hi_z));
                 ++k)
                s.cells[{i, k}].push_back(at);
        }
    }
    return s;
}

std::vector<SurfaceMap::Height> SurfaceMap::heights_at(float x, float z) const {
    std::vector<Height> out;
    const auto cell = cells.find({static_cast<std::int64_t>(std::floor(x)), static_cast<std::int64_t>(std::floor(z))});
    std::vector<std::size_t> asked = wide;
    if (cell != cells.end())
        asked.insert(asked.end(), cell->second.begin(), cell->second.end());
    for (const std::size_t i : asked) {
        const auto& t = triangles[i];
        // where (x, z) falls on it from above, by its corners' weights
        const double d = (t[1][2] - t[2][2]) * (t[0][0] - t[2][0]) + (t[2][0] - t[1][0]) * (t[0][2] - t[2][2]);
        if (std::fabs(d) < 1e-12)
            continue;
        const double a = ((t[1][2] - t[2][2]) * (x - t[2][0]) + (t[2][0] - t[1][0]) * (z - t[2][2])) / d;
        const double b = ((t[2][2] - t[0][2]) * (x - t[2][0]) + (t[0][0] - t[2][0]) * (z - t[2][2])) / d;
        const double c = 1 - a - b;
        if (a < -1e-6 || b < -1e-6 || c < -1e-6)
            continue;
        out.push_back({static_cast<float>(a * t[0][1] + b * t[1][1] + c * t[2][1]), up[i] != 0});
    }
    return out;
}

std::pair<std::size_t, std::size_t> onto_floors(const assets::ImportedModel& floors, const RoomCollision& room,
                                                RoomAlignment* a) {
    std::vector<std::array<float, 3>> places{{0, 0, 0}};
    for (const auto& m : markers(room))
        places.push_back({m.place[0] / 4096.0f, m.place[1] / 4096.0f, m.place[2] / 4096.0f});
    assets::ImportedModel placed = floors;
    place_model(&placed, a->place);
    // its lowest main floor (the lowest height, by halves of a unit, holding a quarter of the floors' area the most
    // held one does) at the room's ground: the floor under its middle may be a roof, and from a higher one the player
    // could fall past where the game takes a fall for a fall out of the room
    double x = 0, z = 0, ground = 0;
    if (room_ground_middle(room, &x, &z, &ground)) {
        std::map<long long, double> area_at;
        for (const auto& t : drawn_triangles(placed)) {
            const float e0[3] = {t[1][0] - t[0][0], t[1][1] - t[0][1], t[1][2] - t[0][2]};
            const float e1[3] = {t[2][0] - t[0][0], t[2][1] - t[0][1], t[2][2] - t[0][2]};
            const float n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                                e0[0] * e1[1] - e0[1] * e1[0]};
            const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (!(length > 0) || n[1] / length <= 0.7f)
                continue;  // (n[1]: twice its area seen from above)
            area_at[std::llround((t[0][1] + t[1][1] + t[2][1]) / 3 * 2)] += n[1] / 2;
        }
        const auto most = std::max_element(area_at.begin(), area_at.end(),
                                           [](const auto& p, const auto& q) { return p.second < q.second; });
        auto main = area_at.begin();
        while (main != most && main->second < most->second / 4)
            ++main;
        if (main != area_at.end()) {
            const float raise = static_cast<float>(ground / 4096.0 - main->first / 2.0);
            a->place.move[1] += raise;
            for (auto& mesh : placed.model.meshes)
                for (auto& v : mesh.vertices)
                    v.position[1] += raise;
        }
    }
    const SurfaceMap surfaces = surface_map(placed);
    // a place stands on a floor at most a stair's rise above its feet (the game puts the player there and lets them
    // fall: a floor higher up is passed under) and two units below
    const float above = 0.25f, below = 2.0f;
    auto on = [&](float dx, float dz) {
        std::size_t count = 0;
        for (const auto& q : places) {
            bool stands = false;
            for (const auto& h : surfaces.heights_at(q[0] - dx, q[2] - dz))
                stands = stands || (h.up && h.y <= q[1] + above && h.y >= q[1] - below);
            count += stands ? 1 : 0;
        }
        return count;
    };
    std::size_t most = on(0, 0);
    float best_x = 0, best_z = 0;
    for (std::size_t t = 0; t < surfaces.triangles.size() && most < places.size(); ++t) {
        if (!surfaces.up[t])
            continue;
        const auto& tri = surfaces.triangles[t];
        const float mx = (tri[0][0] + tri[1][0] + tri[2][0]) / 3, mz = (tri[0][2] + tri[1][2] + tri[2][2]) / 3;
        const float my = (tri[0][1] + tri[1][1] + tri[2][1]) / 3;
        for (const auto& q : places) {
            if (my > q[1] + above || my < q[1] - below)
                continue;
            const float dx = q[0] - mx, dz = q[2] - mz;
            const std::size_t count = on(dx, dz);
            if (count > most || (count == most && dx * dx + dz * dz < best_x * best_x + best_z * best_z)) {
                most = count;
                best_x = dx;
                best_z = dz;
            }
        }
    }
    a->place.move[0] += best_x;
    a->place.move[2] += best_z;
    return {most, places.size()};
}

float raise_to_markers(const SurfaceMap& surfaces, const std::vector<std::array<float, 3>>& markers, std::size_t* on) {
    // per marker, each raise that would put a floor under it a tenth below its feet
    std::vector<std::vector<float>> raises;
    for (const auto& q : markers) {
        std::vector<float> r;
        for (const auto& h : surfaces.heights_at(q[0], q[2])) {
            const float d = q[1] - 0.1f - h.y;
            if (h.up && std::fabs(d) <= 50.0f)
                r.push_back(d);
        }
        raises.push_back(std::move(r));
    }
    // the raise the most markers agree on (within 0.15), the smallest of equals, then their mean
    const float near = 0.15f;
    float best = 0;
    std::size_t most = 0;
    for (const auto& r : raises) {
        for (const float d : r) {
            std::size_t count = 0;
            for (const auto& other : raises) {
                bool agrees = false;
                for (const float e : other)
                    agrees = agrees || std::fabs(e - d) <= near;
                count += agrees ? 1 : 0;
            }
            if (count > most || (count == most && std::fabs(d) < std::fabs(best))) {
                most = count;
                best = d;
            }
        }
    }
    if (most > 0) {
        float sum = 0;
        for (const auto& r : raises) {
            float closest = 1e30f;
            for (const float e : r) {
                if (std::fabs(e - best) < std::fabs(closest - best))
                    closest = e;
            }
            if (std::fabs(closest - best) <= near)
                sum += closest;
        }
        best = sum / static_cast<float>(most);
    }
    if (on != nullptr)
        *on = most;
    return best;
}

std::size_t cut_to_zone(assets::ImportedModel* placed, const RoomZone& zone) {
    assets::ImportedModel& m = *placed;
    // a model that keeps its skeleton: each vertex where its joints put it
    const auto palette = rest_palette(m);
    auto where = [&](const assets::NeutralVertex& v) { return posed(palette, v); };
    std::size_t dropped = 0;
    std::vector<assets::NeutralMesh> kept;
    for (auto& mesh : m.model.meshes) {
        if (!mesh.sky) {
            std::vector<std::array<float, 3>> at(mesh.vertices.size());
            for (std::size_t v = 0; v < mesh.vertices.size(); ++v)
                at[v] = where(mesh.vertices[v]);
            std::vector<std::uint32_t> indices;
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const auto& a = at[mesh.indices[i]];
                const auto& b = at[mesh.indices[i + 1]];
                const auto& c = at[mesh.indices[i + 2]];
                const bool over = zone.holds(a[0], a[2]) || zone.holds(b[0], b[2]) || zone.holds(c[0], c[2]) ||
                                  zone.holds((a[0] + b[0] + c[0]) / 3, (a[2] + b[2] + c[2]) / 3);
                if (over)
                    indices.insert(indices.end(), {mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]});
                else
                    ++dropped;
            }
            // only the vertices still used
            std::map<std::uint32_t, std::uint32_t> renumbered;
            std::vector<assets::NeutralVertex> vertices;
            for (auto& index : indices) {
                const auto found = renumbered.emplace(index, static_cast<std::uint32_t>(vertices.size()));
                if (found.second)
                    vertices.push_back(mesh.vertices[index]);
                index = found.first->second;
            }
            mesh.indices = std::move(indices);
            mesh.vertices = std::move(vertices);
        }
        if (!mesh.indices.empty())
            kept.push_back(std::move(mesh));
    }
    m.model.meshes = std::move(kept);
    std::set<std::string> used;
    for (const auto& mesh : m.model.meshes) {
        used.insert(mesh.texture_name);
        for (const auto& frame : mesh.texture_frames)
            used.insert(frame.first);
    }
    m.textures.erase(
        std::remove_if(m.textures.begin(), m.textures.end(), [&](const auto& t) { return used.count(t.first) == 0; }),
        m.textures.end());
    return dropped;
}

std::vector<std::pair<int, ObjectUse>> object_uses(const WorldRoom& room, const RoomReplacement& r) {
    std::vector<std::pair<int, ObjectUse>> out;
    bool first = true;
    for (const auto& o : room.objects) {
        const auto set = r.objects.find(o.member);
        out.push_back({o.member, set != r.objects.end() ? set->second : first ? ObjectUse::model : ObjectUse::hide});
        first = false;
    }
    return out;
}

std::vector<std::string> member_models(const std::filesystem::path& graphics_dump, const std::string& code,
                                       int member) {
    std::vector<std::string> out;
    std::error_code ec;
    const std::string label = member_label(code, member);
    const auto folder = graphics_dump / path_u8(label) / "mdl";
    if (!std::filesystem::is_directory(folder, ec))
        return out;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (lower(u8(e.path().extension())) == ".gltf")
            out.push_back(label + "/mdl/" + u8(e.path().stem()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> rooms_sharing(const World& w, int room, int member, bool collision) {
    std::vector<int> out;
    for (const auto& r : w.rooms) {
        if (r.index == room)
            continue;
        bool uses = collision ? r.collision_member == member : false;
        for (const auto& o : r.objects)
            uses = uses || (!collision && o.member == member);
        if (uses)
            out.push_back(r.index);
    }
    return out;
}

bool build_room(const World& w, const std::filesystem::path& mod_dir, const RoomReplacement& r, BuiltRoom* out,
                std::string* why) {
    if (r.room < 0 || static_cast<std::size_t>(r.room) >= w.rooms.size())
        return fail_with(why, "no room " + std::to_string(r.room));
    const WorldRoom& room = w.rooms[static_cast<std::size_t>(r.room)];
    const auto& data = w.archive.entries[static_cast<std::size_t>(room.collision_member)].data;
    if (data.empty())
        return fail_with(why, "room " + std::to_string(r.room) + " has no collision in the game's data to replace");
    RoomCollision original;
    if (!parse_collision(data, &original, why))
        return false;
    const auto folder = change_models(mod_dir, r.world, r.room);
    assets::ImportedModel placed;
    if (!placed_model(folder / path_u8(r.collision.empty() ? r.model : r.collision), r.place, &placed, why))
        return false;
    if (r.zone >= 0)
        cut_to_zone(&placed, room_zone(original, r.zone));
    CollisionRules rules;
    rules.ground_limit = r.ground_limit;
    rules.names_by_material = r.names_by_material;
    rules.names_by_triangle = r.names_by_triangle;
    rules.drop_original = r.drop_original;
    rules.markers = r.markers;
    BuiltRoom b;
    if (!build_collision(collision_source(placed, r.collision.empty()), original, rules, &b.collision, &b.report,
                         why)) {
        return false;
    }
    const std::string problem = check_collision(b.collision, original);
    if (!problem.empty())
        return fail_with(why, "the collision made is not one the game reads: " + problem);
    b.warnings = b.report.warnings;
    for (auto& more : sharing_warnings(w, r))
        b.warnings.push_back(std::move(more));
    *out = std::move(b);
    return true;
}

void apply_gate_points(World* w, const RoomReplacement& r) {
    for (const auto& [key, at] : r.gate_points) {
        for (auto& g : w->gate_models) {
            if (g.key == key.first)
                g.parts[static_cast<std::size_t>(key.second)].position = at;
        }
    }
}

std::vector<RoomWarning> sharing_warnings(const World& w, const RoomReplacement& r) {
    std::vector<RoomWarning> out;
    if (r.room < 0 || static_cast<std::size_t>(r.room) >= w.rooms.size())
        return out;
    const WorldRoom& room = w.rooms[static_cast<std::size_t>(r.room)];
    for (const int other : rooms_sharing(w, r.room, room.collision_member, true)) {
        out.push_back({RoomWarning::collision_shared, {std::to_string(other)}});
    }
    for (const auto& [member, use] : object_uses(room, r)) {
        if (use == ObjectUse::keep)
            continue;
        for (const int other : rooms_sharing(w, r.room, member, false)) {
            out.push_back({RoomWarning::model_shared, {std::to_string(other), std::to_string(member)}});
        }
    }
    return out;
}

bool write_world_outputs(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                         const std::filesystem::path& mod_dir, const std::string& code, WorldOutcome* out,
                         std::string* why) {
    WorldOutcome o;
    World w;
    if (!load_world(nitrofs, code, &w, why))
        return false;  // the player's copy, never the mod's
    std::vector<std::string> problems;
    const auto changes = changes_of(mod_dir, code, &problems);
    if (!problems.empty())
        return fail_with(why, problems.front());
    std::error_code ec;
    const auto archive = mod_dir / "files" / path_u8(world_path(code));

    const std::set<std::string> before = read_written(mod_dir, code);
    if (changes.empty()) {
        for (const auto& file : before)
            remove_written(mod_dir, file);
        std::filesystem::remove(written_list(mod_dir, code), ec);
        std::filesystem::remove(rooms_folder(mod_dir, code), ec);  // only when empty
        o.removed = true;
        o.lines.push_back(world_path(code) + ": no room replaced; its outputs removed");
        if (out != nullptr)
            *out = std::move(o);
        return true;
    }

    // the collisions
    std::map<int, std::vector<std::uint8_t>> collisions;
    std::map<int, int> collision_of;  // member -> the room that made it
    for (const auto& r : changes) {
        if (r.keep_collision) {
            o.lines.push_back("room " + std::to_string(r.room) + ": the game's own grounds, walls and ceilings kept");
            continue;
        }
        BuiltRoom b;
        std::string problem;
        if (!build_room(w, mod_dir, r, &b, &problem))
            return fail_with(why, "room " + std::to_string(r.room) + ": " + problem);
        const int member = w.rooms[static_cast<std::size_t>(r.room)].collision_member;
        if (collision_of.count(member) != 0) {
            return fail_with(why, "rooms " + std::to_string(collision_of[member]) + " and " + std::to_string(r.room) +
                                      " share one collision: replace only one of them");
        }
        collision_of[member] = r.room;
        collisions[member] = write_collision(b.collision);
        o.lines.push_back("room " + std::to_string(r.room) + ": " + std::to_string(b.report.faces[kGround]) +
                          " grounds, " + std::to_string(b.report.faces[kWall]) + " walls, " +
                          std::to_string(b.report.faces[kCeiling]) + " ceilings, " + std::to_string(b.report.nodes) +
                          " nodes, " + std::to_string(b.report.bytes) + " bytes");
        for (const auto& warning : b.warnings)
            o.lines.push_back("room " + std::to_string(r.room) + ": " + warning_text(warning));
    }

    // the archive, checked before it replaces anything: the gate models' points moved too
    World moved = w;
    for (const auto& r : changes)
        apply_gate_points(&moved, r);
    const auto bytes = write_world(moved, collisions);
    const auto gate_entry = write_gate_models(moved);
    const auto result = write_into_mod(
        mod_dir, archive,
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return f ? std::string() : std::string("could not be written");
        },
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ifstream f(temporary, std::ios::binary);
            const std::vector<std::uint8_t> back((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            World again;
            std::string problem;
            if (!parse_world(back, code, &again, &problem))
                return "it does not read back: " + problem;
            if (again.archive.entries.size() != w.archive.entries.size())
                return "it holds another number of entries";
            for (std::size_t e = 0; e < w.archive.entries.size(); ++e) {
                const auto made = collisions.find(static_cast<int>(e));
                if (e == 1) {
                    if (again.archive.entries[e].data != gate_entry)
                        return "the gate models are not as moved";
                    continue;
                }
                if (made == collisions.end()) {
                    if (again.archive.entries[e].data != w.archive.entries[e].data)
                        return "entry " + std::to_string(e) + " changed";
                    continue;
                }
                RoomCollision c, original;
                if (!parse_collision(again.archive.entries[e].data, &c, &problem))
                    return "entry " + std::to_string(e) + ": " + problem;
                if (!parse_collision(w.archive.entries[e].data, &original, &problem))
                    return problem;
                problem = check_collision(c, original);
                if (!problem.empty())
                    return "entry " + std::to_string(e) + ": " + problem;
            }
            return {};
        });
    if (!result.ok)
        return fail_with(why, world_path(code) + ": " + result.why);
    std::set<std::string> now{"files/" + world_path(code)};
    o.lines.push_back(u8(archive) + " written");

    // the models: the modder's, nothing, or the game's
    std::map<int, std::pair<const RoomReplacement*, ObjectUse>> use_of;
    for (const auto& r : changes) {
        for (const auto& [member, use] : object_uses(w.rooms[static_cast<std::size_t>(r.room)], r))
            use_of[member] = {&r, use};
    }
    std::map<std::string, assets::ImportedModel> placed;  // by change
    for (const auto& [member, change] : use_of) {
        const RoomReplacement& r = *change.first;
        if (change.second == ObjectUse::keep)
            continue;
        const auto keys = member_models(graphics_dump, code, member);
        if (keys.empty()) {
            return fail_with(why, "the graphics dump has no model of " + member_label(code, member) +
                                      ": make the dump first");
        }
        assets::ImportedModel model;
        if (change.second == ObjectUse::model) {
            const std::string id = r.world + "/" + std::to_string(r.room);
            auto it = placed.find(id);
            if (it == placed.end()) {
                assets::ImportedModel m;
                if (!placed_model(change_models(mod_dir, r.world, r.room) / path_u8(r.model), r.place, &m, why))
                    return false;
                if (r.zone >= 0) {
                    // only what lies around the room
                    const WorldRoom& room = w.rooms[static_cast<std::size_t>(r.room)];
                    const auto& data = w.archive.entries[static_cast<std::size_t>(room.collision_member)].data;
                    RoomCollision floor;
                    std::string problem;
                    if (data.empty() || !parse_collision(data, &floor, &problem))
                        return fail_with(why, "room " + std::to_string(r.room) + ": no floor to cut the model to");
                    o.lines.push_back("room " + std::to_string(r.room) + ": " +
                                      std::to_string(cut_to_zone(&m, room_zone(floor, r.zone))) +
                                      " triangles away from the room left out");
                }
                it = placed.emplace(id, std::move(m)).first;
            }
            model = it->second;
        }
        for (const auto& key : keys) {
            if (!write_room_model(mod_dir, key, model, &now, why))
                return false;
            o.lines.push_back(key + ".gltf written" + (change.second == ObjectUse::hide ? " (nothing drawn)" : ""));
        }
    }
    // what an earlier save wrote and this one did not: removed; the list kept
    for (const auto& file : before) {
        if (now.count(file) == 0)
            remove_written(mod_dir, file);
    }
    std::string list;
    for (const auto& file : now)
        list += file + "\n";
    const auto saved = write_into_mod(
        mod_dir, written_list(mod_dir, code),
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
            f << list;
            return f ? std::string() : std::string("could not be written");
        },
        [](const std::filesystem::path&) { return std::string(); });
    if (!saved.ok)
        return fail_with(why, saved.why);
    if (out != nullptr)
        *out = std::move(o);
    return true;
}

bool save_change(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                 const std::filesystem::path& mod_dir, RoomReplacement r, const std::filesystem::path& model_source,
                 const std::filesystem::path& collision_source_file, WorldOutcome* out, std::string* why) {
    std::error_code ec;
    const auto folder = change_models(mod_dir, r.world, r.room);
    // the model files and what they need, beside the change
    auto bring = [&](const std::filesystem::path& source, std::string* name) {
        if (source.empty())
            return true;
        *name = u8(source.filename());
        const auto destination = folder / source.filename();
        if (std::filesystem::equivalent(source, destination, ec))
            return true;
        std::filesystem::create_directories(folder, ec);
        std::string problem = copy_bytes(source, destination);
        if (!problem.empty())
            return fail_with(why, problem);
        std::vector<std::string> missing;
        for (const auto& c : model_companions(source, &missing)) {
            const auto to = folder / path_u8(c.relative);
            std::filesystem::create_directories(to.parent_path(), ec);
            problem = copy_bytes(c.source, to);
            if (!problem.empty())
                return fail_with(why, problem);
        }
        return true;
    };
    if (!bring(model_source, &r.model))
        return false;
    if (collision_source_file.empty())
        r.collision.clear();
    else if (!bring(collision_source_file, &r.collision))
        return false;
    // the change, then everything made from it
    const std::string text = change_text(r);
    const auto result = write_into_mod(
        mod_dir, change_file(mod_dir, r.world, r.room),
        [&](const std::filesystem::path& temporary) -> std::string {
            std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
            f << text;
            return f ? std::string() : std::string("could not be written");
        },
        [&](const std::filesystem::path& temporary) -> std::string {
            RoomReplacement back;
            std::string problem;
            return read_change(temporary, &back, &problem) ? std::string() : problem;
        });
    if (!result.ok)
        return fail_with(why, result.why);
    return write_world_outputs(nitrofs, graphics_dump, mod_dir, r.world, out, why);
}

bool revert_change(const std::filesystem::path& nitrofs, const std::filesystem::path& graphics_dump,
                   const std::filesystem::path& mod_dir, const std::string& code, int room, WorldOutcome* out,
                   std::string* why) {
    std::error_code ec;
    const auto file = change_file(mod_dir, code, room);
    if (!std::filesystem::exists(file, ec))
        return fail_with(why, "room " + std::to_string(room) + " of " + code + " is not replaced in this mod");
    std::filesystem::remove(file, ec);
    std::filesystem::remove_all(change_models(mod_dir, code, room), ec);
    return write_world_outputs(nitrofs, graphics_dump, mod_dir, code, out, why);
}

}  // namespace studio
