#include "khdays/assets/ddd_map.h"

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "khdays/assets/dds.h"

namespace khdays::assets::ddd {

namespace {

// What a PMP and its PMOs hold (docs/FORMATS.md, "Dream Drop Distance's rooms"):
// PMP: "PMP\0", objects at +0x10 (u16), textures at +0x1a (u16) listed at +0x1c; an object of 0x30 bytes from +0x20:
// position, rotation (radians), scale (f32 x3 each), its PMO's offset (u32, 0 none).
// PMO: "PMO\0", textures at +8 (u8) listed from +0xa0 (0x20 each, the name at +4), its section at +0x10 (else at
// +0x1c, a model with see-through parts only; offsets from the PMO).
// A section: two groups of records, pairs of u32 (the first group's, then the second's): the records' offset at +0 / +4,
// their vertex count at +8 / +0xc, the records' count at +0x10 / +0x14; then the vertices' offset at +0x18 and their
// bytes at +0x1c, the first group's vertices then the second's. A record, 24 bytes: u16 vertex count, u8 texture (in
// the PMO's list), u8 vertex size, u32 flags (primitive in the top four bits: 3 triangles, 4 a strip stitched by
// degenerate triangles), u16 at +0xa its drawing (0x20 blended, 0x40 added, 0x60 taken away), its colour at +0xc.
// A vertex of 20 bytes: u16 u, v (0x800: 1), its colour (R G B A), position f32 x3 (x 32768).
constexpr std::size_t kObjectSize = 0x30;
constexpr std::size_t kTextureInfoSize = 0x20;
constexpr std::size_t kPmoTextures = 0xa0;
constexpr std::size_t kRecordSize = 24;
constexpr std::size_t kVertexSize = 20;
constexpr float kUvUnit = 2048.0F;
constexpr float kPositionUnit = 32768.0F;
constexpr unsigned kTriangles = 3, kStrip = 4;
constexpr std::uint16_t kBlended = 0x60;  // blended, added or taken away: drawn see-through
// A BCD: "@bcd", its version (0x0a), its parts' count and offset at +8 / +0xc. A part, 0x30 bytes: its box (min, max:
// f32 x3 each), u16 vertex and face counts at +0x18 / +0x1a, the vertices' offset at +0x1c and the faces' at +0x20
// (from the file), then what speeds up the game's look-ups (a tree, its lists). A vertex: f32 x4 (w 1). A face, 48
// bytes: its plane (normal f32 x3, distance), its corners (i16 x4, the fourth -1 for a triangle; their order not
// always the normal's), then what it is (a floor, a wall).
constexpr std::size_t kBcdPartSize = 0x30;
constexpr std::size_t kBcdVertexSize = 16;
constexpr std::size_t kBcdFaceSize = 48;

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : d_(data), n_(size) {}
    template <typename T> T get(std::size_t at) const {
        if (at + sizeof(T) > n_ || at + sizeof(T) < at)
            throw std::runtime_error("past the file's end");
        T v;
        std::memcpy(&v, d_ + at, sizeof(T));
        return v;
    }
    std::string name(std::size_t at, std::size_t length) const {
        std::string out;
        for (std::size_t i = 0; i < length && at + i < n_ && d_[at + i] != 0; ++i)
            out += static_cast<char>(d_[at + i]);
        return out;
    }
    std::size_t size() const { return n_; }

private:
    const std::uint8_t* d_;
    std::size_t n_;
};

struct Record {
    std::uint16_t count = 0;
    std::uint8_t texture = 0;
    std::uint8_t vertex_size = 0;
    unsigned primitive = 0;
    bool blended = false;
};

// A section's records, its first group's then its second's.
std::vector<Record> records(const Reader& r, std::size_t pmo, std::size_t section) {
    std::vector<Record> out;
    for (std::size_t group = 0; group < 2; ++group) {
        const std::size_t at = pmo + r.get<std::uint32_t>(section + 4 * group);
        const auto count = r.get<std::uint32_t>(section + 0x10 + 4 * group);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t rec = at + i * kRecordSize;
            out.push_back({r.get<std::uint16_t>(rec), r.get<std::uint8_t>(rec + 2), r.get<std::uint8_t>(rec + 3),
                           r.get<std::uint32_t>(rec + 4) >> 28, (r.get<std::uint16_t>(rec + 10) & kBlended) != 0});
        }
    }
    return out;
}

std::array<float, 3> turned(const std::array<float, 3>& p, const std::array<float, 3>& turn) {
    // about x, then y, then z
    auto [x, y, z] = p;
    const float cx = std::cos(turn[0]), sx = std::sin(turn[0]);
    const float cy = std::cos(turn[1]), sy = std::sin(turn[1]);
    const float cz = std::cos(turn[2]), sz = std::sin(turn[2]);
    float y1 = y * cx - z * sx, z1 = y * sx + z * cx;
    float x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy;
    float x3 = x2 * cz - y1 * sz, y3 = x2 * sz + y1 * cz;
    return {x3, y3, z2};
}

}  // namespace

std::vector<std::string> texture_names(const std::uint8_t* data, std::size_t size) {
    const Reader r(data, size);
    if (size < 0x20 || std::memcmp(data, "PMP\0", 4) != 0)
        throw std::runtime_error("not a PMP");
    std::vector<std::string> out;
    const auto count = r.get<std::uint16_t>(0x1a);
    const auto list = r.get<std::uint32_t>(0x1c);
    for (std::size_t i = 0; i < count; ++i)
        out.push_back(r.name(list + i * kTextureInfoSize + 4, 12));
    return out;
}

Room room_model(const std::uint8_t* data, std::size_t size, const std::string& name, const PictureFinder& find,
                const PictureSink& write_picture) {
    const Reader r(data, size);
    if (size < 0x20 || std::memcmp(data, "PMP\0", 4) != 0)
        throw std::runtime_error("not a PMP");
    Room room;
    room.scene.name = name;
    room.scene.nodes.push_back({name, -1});
    // a material for each texture, blended or not: its picture written once
    std::map<std::pair<std::string, bool>, int> material_of;
    std::map<std::string, bool> picture_alpha;  // written pictures: whether they hold see-through texels
    std::map<int, SceneMesh> meshes;            // by material
    auto material = [&](const std::string& texture, bool blended) {
        const auto key = std::make_pair(texture, blended);
        if (auto found = material_of.find(key); found != material_of.end())
            return found->second;
        SceneMaterial m;
        m.name = texture + (blended ? "_blend" : "");
        auto written = picture_alpha.find(texture);
        if (written == picture_alpha.end() && texture.empty()) {
            room.report.push_back("a mesh with no texture: in its colours");
        } else if (written == picture_alpha.end()) {
            bool alpha = false;
            const std::filesystem::path hd = find(texture);
            if (!hd.empty()) {
                try {
                    const DecodedTexture picture = read_dds(hd);
                    for (std::size_t i = 3; i < picture.rgba.size() && !alpha; i += 4)
                        alpha = picture.rgba[i] < 250;
                    write_picture(texture + ".png", picture);
                    written = picture_alpha.emplace(texture, alpha).first;
                } catch (const std::exception& e) {
                    room.report.push_back(texture + ": " + e.what());
                }
            } else {
                room.report.push_back(texture + ": no HD picture in the copy; its surfaces in their colours");
            }
        }
        if (written != picture_alpha.end()) {
            m.texture = texture + ".png";
            m.alpha_mode = blended           ? NeutralMesh::AlphaMode::blend
                           : written->second ? NeutralMesh::AlphaMode::mask
                                             : NeutralMesh::AlphaMode::opaque;
        } else {
            m.alpha_mode = blended ? NeutralMesh::AlphaMode::blend : NeutralMesh::AlphaMode::opaque;
        }
        m.double_sided = true;  // (which faces the game culls is not read yet)
        const int index = static_cast<int>(room.scene.materials.size());
        room.scene.materials.push_back(m);
        material_of.emplace(key, index);
        return index;
    };

    const auto objects = r.get<std::uint16_t>(0x10);
    for (std::size_t i = 0; i < objects; ++i) {
        const std::size_t o = 0x20 + i * kObjectSize;
        const std::array<float, 3> move{r.get<float>(o), r.get<float>(o + 4), r.get<float>(o + 8)};
        const std::array<float, 3> turn{r.get<float>(o + 12), r.get<float>(o + 16), r.get<float>(o + 20)};
        const std::array<float, 3> scale{r.get<float>(o + 24), r.get<float>(o + 28), r.get<float>(o + 32)};
        const auto pmo = r.get<std::uint32_t>(o + 36);
        if (pmo == 0)
            continue;
        if (pmo + 0xa0 > size || std::memcmp(data + pmo, "PMO\0", 4) != 0) {
            room.report.push_back("object " + std::to_string(i) + ": no PMO at its offset");
            continue;
        }
        std::vector<std::string> textures;
        for (std::size_t t = 0; t < r.get<std::uint8_t>(pmo + 8); ++t)
            textures.push_back(r.name(pmo + kPmoTextures + t * kTextureInfoSize + 4, 12));
        {
            // its section (+0x1c, when +0x10 has none, is a model's only one; else it points within the vertices)
            std::uint32_t at = r.get<std::uint32_t>(pmo + 0x10);
            if (at == 0)
                at = r.get<std::uint32_t>(pmo + 0x1c);
            if (at == 0)
                continue;
            const std::size_t s = pmo + at;
            const auto vertices_at = r.get<std::uint32_t>(s + 0x18);
            const auto bytes = r.get<std::uint32_t>(s + 0x1c);
            const std::vector<Record> recs = records(r, pmo, s);
            std::size_t held = 0;
            bool readable = true;
            for (const Record& rec : recs) {
                held += std::size_t{rec.count} * rec.vertex_size;
                readable = readable && rec.vertex_size == kVertexSize &&
                           (rec.primitive == kTriangles || rec.primitive == kStrip);
            }
            if (!readable || held != bytes) {
                room.report.push_back("object " + std::to_string(i) + ": its meshes not read (" +
                                      std::to_string(recs.size()) + " records, " + std::to_string(held) + " of " +
                                      std::to_string(bytes) + " vertex bytes, or a vertex of another size)");
                continue;
            }
            const std::size_t v0 = pmo + vertices_at;
            std::size_t first = 0;
            for (const Record& rec : recs) {
                const std::string texture = rec.texture < textures.size() ? textures[rec.texture] : std::string();
                const int m = material(texture, rec.blended);
                SceneMesh& mesh = meshes[m];
                if (mesh.primitives.empty()) {
                    mesh.name = room.scene.materials[static_cast<std::size_t>(m)].name;
                    mesh.primitives.push_back({m});
                }
                ScenePrimitive& prim = mesh.primitives.front();
                const auto base = static_cast<std::uint32_t>(prim.positions.size());
                std::vector<std::array<float, 3>> placed;
                for (std::size_t k = 0; k < rec.count; ++k) {
                    const std::size_t v = v0 + (first + k) * kVertexSize;
                    prim.uvs.push_back({r.get<std::uint16_t>(v) / kUvUnit, r.get<std::uint16_t>(v + 2) / kUvUnit});
                    prim.colors.push_back({r.get<std::uint8_t>(v + 4), r.get<std::uint8_t>(v + 5),
                                           r.get<std::uint8_t>(v + 6), r.get<std::uint8_t>(v + 7)});
                    std::array<float, 3> p{r.get<float>(v + 8) / kPositionUnit * scale[0],
                                           r.get<float>(v + 12) / kPositionUnit * scale[1],
                                           r.get<float>(v + 16) / kPositionUnit * scale[2]};
                    p = turned(p, turn);
                    p = {p[0] + move[0], p[1] + move[1], p[2] + move[2]};
                    prim.positions.push_back(p);
                    placed.push_back(p);
                }
                if (rec.primitive == kTriangles) {
                    for (std::uint32_t t = 0; t + 2 < rec.count; t += 3)
                        prim.indices.insert(prim.indices.end(), {base + t, base + t + 1, base + t + 2});
                } else {
                    for (std::uint32_t t = 0; t + 2 < rec.count; ++t) {
                        const auto& a = placed[t];
                        const auto& b = placed[t + 1];
                        const auto& c = placed[t + 2];
                        if (a == b || b == c || a == c)
                            continue;  // a stitch between two strips
                        if (t % 2 == 0)
                            prim.indices.insert(prim.indices.end(), {base + t, base + t + 1, base + t + 2});
                        else
                            prim.indices.insert(prim.indices.end(), {base + t + 1, base + t, base + t + 2});
                    }
                }
                first += rec.count;
            }
        }
    }
    for (auto& [m, mesh] : meshes)
        room.scene.meshes.push_back(std::move(mesh));
    return room;
}

Scene collision_scene(const std::uint8_t* data, std::size_t size, const std::string& name) {
    const Reader r(data, size);
    if (size < 0x10 || std::memcmp(data, "@bcd", 4) != 0)
        throw std::runtime_error("not a BCD");
    Scene scene;
    scene.name = name + " collision";
    scene.nodes.push_back({"room"});
    SceneMaterial mat;
    mat.name = "ddd collision";
    mat.double_sided = true;  // a wall stops from both sides
    scene.materials.push_back(mat);
    ScenePrimitive prim;
    auto corner = [&](const std::array<float, 3>& p) {
        prim.indices.push_back(static_cast<std::uint32_t>(prim.positions.size()));
        prim.positions.push_back(p);
        prim.uvs.push_back({0.0F, 0.0F});
        prim.colors.push_back({255, 255, 255, 255});
    };
    const auto parts = r.get<std::uint32_t>(8);
    const auto parts_at = r.get<std::uint32_t>(0xc);
    for (std::size_t part = 0; part < parts; ++part) {
        const std::size_t p = parts_at + part * kBcdPartSize;
        const auto vertices = r.get<std::uint16_t>(p + 0x18);
        const auto faces = r.get<std::uint16_t>(p + 0x1a);
        const auto vertices_at = r.get<std::uint32_t>(p + 0x1c);
        const auto faces_at = r.get<std::uint32_t>(p + 0x20);
        auto vertex = [&](std::int16_t i) {
            if (i < 0 || i >= vertices)
                throw std::runtime_error("a face's corner past the vertices");
            const std::size_t v = vertices_at + static_cast<std::size_t>(i) * kBcdVertexSize;
            return std::array<float, 3>{r.get<float>(v), r.get<float>(v + 4), r.get<float>(v + 8)};
        };
        // a triangle turned to face where its face's plane does (a floor up, a wall out): the corners' order varies
        auto triangle = [&](const std::array<float, 3>& a, std::array<float, 3> b, std::array<float, 3> c,
                            const std::array<float, 3>& facing) {
            const std::array<float, 3> e0{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const std::array<float, 3> e1{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const std::array<float, 3> n{e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                                         e0[0] * e1[1] - e0[1] * e1[0]};
            if (n[0] * facing[0] + n[1] * facing[1] + n[2] * facing[2] < 0)
                std::swap(b, c);
            corner(a);
            corner(b);
            corner(c);
        };
        for (std::size_t f = 0; f < faces; ++f) {
            const std::size_t face = faces_at + f * kBcdFaceSize;
            const std::array<float, 3> facing{r.get<float>(face), r.get<float>(face + 4), r.get<float>(face + 8)};
            const std::size_t at = face + 0x10;
            const std::array<std::int16_t, 4> c{r.get<std::int16_t>(at), r.get<std::int16_t>(at + 2),
                                                r.get<std::int16_t>(at + 4), r.get<std::int16_t>(at + 6)};
            triangle(vertex(c[0]), vertex(c[1]), vertex(c[2]), facing);
            if (c[3] >= 0)  // a four-sided face, as two triangles
                triangle(vertex(c[0]), vertex(c[2]), vertex(c[3]), facing);
        }
    }
    if (prim.positions.empty())
        throw std::runtime_error("a BCD with no faces");
    scene.meshes.push_back({"collision", 0, {}, {std::move(prim)}});
    return scene;
}

}  // namespace khdays::assets::ddd
