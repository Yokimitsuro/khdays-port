#include "khdays/assets/recom_map.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

namespace khdays::assets::recom {

namespace {

// What a room's files hold (docs/FORMATS.md, "Re:Chain of Memories' rooms"):
// .VTM / .RTM: entries of 32 bytes, as many as the first entry's offset says: u32 the picture's offset, its name
// (".tm2" ended, 24 bytes), u32; each picture a TIM2.
// .AZF: an offset table (its first offset its length; 0 none). Entry 0 the layout: u16 its places' count, then from
// +0x20 64 bytes a place: u32 the piece (an entry of the table), s16 its parent place (-1 none), u16, s16 x3 its turn
// in tenths of a degree (about x, then y, then z), u16, f32 x4 its move, f32 x4 its scale. The other entries pieces:
// u32 x8 (the fourth its textures' count, the fifth their names' offset, the sixth its drawing's), a ROOT bone (its
// matrix at +0x50), the textures' names (32 bytes each), then the PS2's DMA and VIF stream: each UNPACK V4-32 holds a
// header (u16 at +4 its vertex count), the GIF tag, then 64 bytes a vertex: u32 what it ends (0xffff nothing: a strip
// starts; 0x20 an odd triangle; 0 an even one), its position f32 x3 at +0x10, its colour u32 x4 at +0x20 (0x80 is 1),
// s, t, q f32 at +0x30 and its texture (u32, among the piece's) at +0x3c.
constexpr std::size_t kListEntry = 32;
constexpr std::size_t kPlace = 0x40;
constexpr std::size_t kVertex = 64;
constexpr std::uint32_t kStripStart = 0xffff, kOdd = 0x20;
constexpr float kTenthDegree = 3.14159265358979323846F / 1800.0F;
// .mcd: "@MCD", u32 3, u16 its pieces' count at +8, the pieces' section at the u32 at +0x4c (its pieces 0x10 after),
// 0xc0 bytes a piece: u32 0, u32 1, u32 its number, u32 its mesh's offset (from the end of the pieces), its move,
// turn and scale (f32 x4 each), its matrix (f32 x16, by columns) and its inverse. A mesh: a grid (0x20 bytes), its
// sections' offsets (u32 x8, from the mesh: 0x40 first) -- the grid's cells, its planes (f32 x4: normal, distance),
// its vertices (f32 x4), ..., its faces from the sixth, 24 bytes each: u16 x2 (0, what it is), u16 its plane, u16 x4
// its edges', u16 x4 its corners (a corner repeated: a triangle), u16. A plane's normal points into the solid (a
// floor's down: of the flat faces of five rooms' collisions, 1 % of the area faced up turned to it).
constexpr std::size_t kMcdPiece = 0xc0;
constexpr std::size_t kMcdFace = 24;

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

private:
    const std::uint8_t* d_;
    std::size_t n_;
};

using M4 = std::array<float, 16>;  // column-major

M4 multiply(const M4& a, const M4& b) {
    M4 out{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    return out;
}

// A place's matrix: scaled, turned about x, then y, then z, moved.
M4 place_matrix(const std::array<float, 3>& turn, const std::array<float, 3>& move, const std::array<float, 3>& scale) {
    const float cx = std::cos(turn[0]), sx = std::sin(turn[0]);
    const float cy = std::cos(turn[1]), sy = std::sin(turn[1]);
    const float cz = std::cos(turn[2]), sz = std::sin(turn[2]);
    const M4 rx{1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1};
    const M4 ry{cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
    const M4 rz{cz, sz, 0, 0, -sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const M4 s{scale[0], 0, 0, 0, 0, scale[1], 0, 0, 0, 0, scale[2], 0, 0, 0, 0, 1};
    M4 m = multiply(rz, multiply(ry, multiply(rx, s)));
    m[12] = move[0];
    m[13] = move[1];
    m[14] = move[2];
    return m;
}

std::array<float, 3> apply(const M4& m, const std::array<float, 3>& p) {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}

}  // namespace

std::vector<PictureEntry> picture_list(const std::uint8_t* data, std::size_t size) {
    const Reader r(data, size);
    std::vector<PictureEntry> out;
    const auto first = r.get<std::uint32_t>(0);
    for (std::size_t at = 0; at + kListEntry <= first; at += kListEntry) {
        std::string name = r.name(at + 4, 24);
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tm2") == 0)
            name.resize(name.size() - 4);
        out.push_back({name, r.get<std::uint32_t>(at)});
    }
    return out;
}

Room room_model(const std::uint8_t* data, std::size_t size, const std::string& name, const PictureSource& picture,
                const PictureSink& write_picture) {
    const Reader r(data, size);
    const auto first = size >= 4 ? r.get<std::uint32_t>(0) : 0U;
    if (first < 8 || first % 4 != 0 || first > size)
        throw std::runtime_error("not an .AZF");
    std::vector<std::uint32_t> table;
    for (std::size_t i = 0; i < first / 4; ++i)
        table.push_back(r.get<std::uint32_t>(4 * i));
    std::vector<std::uint32_t> ends(table.begin(), table.end());
    ends.push_back(static_cast<std::uint32_t>(size));
    std::sort(ends.begin(), ends.end());
    auto end_of = [&](std::uint32_t at) { return *std::upper_bound(ends.begin(), ends.end(), at); };

    Room room;
    room.scene.name = name;
    room.scene.nodes.push_back({name, -1});
    std::map<std::string, int> material_of;  // by texture name ("" untextured)
    std::map<int, SceneMesh> by_material;
    auto material = [&](const std::string& texture) {
        if (auto found = material_of.find(texture); found != material_of.end())
            return found->second;
        SceneMaterial m;
        m.name = texture.empty() ? name + "_colours" : texture;
        m.double_sided = true;
        if (!texture.empty()) {
            if (const auto p = picture(texture)) {
                bool alpha = false;
                for (std::size_t i = 3; i < p->rgba.size() && !alpha; i += 4)
                    alpha = p->rgba[i] < 250;
                write_picture(texture + ".png", *p);
                m.texture = texture + ".png";
                m.alpha_mode = alpha ? NeutralMesh::AlphaMode::mask : NeutralMesh::AlphaMode::opaque;
            } else {
                room.report.push_back(texture + ": no picture in the room's files; its surfaces in their colours");
            }
        }
        const int index = static_cast<int>(room.scene.materials.size());
        room.scene.materials.push_back(m);
        material_of.emplace(texture, index);
        return index;
    };

    // the places, each one's matrix its parent's times its own
    const std::size_t layout = table[0];
    const auto places = r.get<std::uint16_t>(layout);
    std::vector<M4> world(places);
    std::vector<bool> done(places, false);
    auto place_of = [&](auto&& self, std::size_t k, int depth) -> const M4& {
        if (done[k])
            return world[k];
        const std::size_t at = layout + 0x20 + k * kPlace;
        const std::array<float, 3> turn{r.get<std::int16_t>(at + 8) * kTenthDegree,
                                        r.get<std::int16_t>(at + 10) * kTenthDegree,
                                        r.get<std::int16_t>(at + 12) * kTenthDegree};
        const std::array<float, 3> move{r.get<float>(at + 16), r.get<float>(at + 20), r.get<float>(at + 24)};
        const std::array<float, 3> scale{r.get<float>(at + 32), r.get<float>(at + 36), r.get<float>(at + 40)};
        world[k] = place_matrix(turn, move, scale);
        const auto parent = r.get<std::int16_t>(at + 4);
        if (parent >= 0 && static_cast<std::size_t>(parent) < places && depth < static_cast<int>(places))
            world[k] = multiply(self(self, static_cast<std::size_t>(parent), depth + 1), world[k]);
        done[k] = true;
        return world[k];
    };

    for (std::size_t k = 0; k < places; ++k) {
        const std::size_t at = layout + 0x20 + k * kPlace;
        const auto piece = r.get<std::uint32_t>(at);
        if (piece == 0 || piece >= table.size() || table[piece] == 0)
            continue;
        const std::size_t o = table[piece];
        const std::size_t end = end_of(table[piece]);
        const auto textures = r.get<std::uint32_t>(o + 12);
        const auto names_at = r.get<std::uint32_t>(o + 16);
        const auto stream_at = r.get<std::uint32_t>(o + 20);
        std::vector<std::string> names;
        for (std::size_t t = 0; t < textures; ++t)
            names.push_back(r.name(o + names_at + 32 * t, 16));
        M4 bone{};
        for (std::size_t i = 0; i < 16; ++i)
            bone[i] = r.get<float>(o + 0x50 + 4 * i);
        const M4 m = multiply(place_of(place_of, k, 0), bone);
        // the stream: each UNPACK V4-32 a strip set
        for (std::size_t a = o + stream_at; a + 4 <= end;) {
            const auto code = r.get<std::uint32_t>(a);
            const unsigned command = (code >> 24) & 0x7F, qwords = (code >> 16) & 0xFF;
            if ((command & 0x6F) != 0x6C) {
                a += 4;
                continue;
            }
            const std::size_t body = a + 4;
            const auto count = r.get<std::uint16_t>(body + 4);
            struct Point {
                std::array<float, 3> position;
                std::array<std::uint8_t, 4> colour;
                std::array<float, 2> uv;
                std::uint32_t texture, kind;
            };
            std::vector<Point> strip;
            for (std::size_t v = 0; v < count; ++v) {
                const std::size_t q = body + 32 + v * kVertex;
                Point pt{};
                const auto p = apply(m, {r.get<float>(q + 16), r.get<float>(q + 20), r.get<float>(q + 24)});
                pt.position = {p[0], -p[1], -p[2]};  // Re:CoM's axes, y down, turned upright
                for (std::size_t i = 0; i < 4; ++i)
                    pt.colour[i] = static_cast<std::uint8_t>(
                        std::min<std::uint32_t>(255, r.get<std::uint32_t>(q + 32 + 4 * i) * 2));
                const float w = r.get<float>(q + 0x38);
                const float div = w != 0 ? w : 1.0F;
                // t from the picture's foot as the pictures are written: glTF's v from the top (on walls the
                // picture's foot was up in 82 to 100% of faces, against 2 to 18% in KH1's, KH2's and BbS's rooms)
                pt.uv = {r.get<float>(q + 0x30) / div, 1.0F - r.get<float>(q + 0x34) / div};
                pt.texture = r.get<std::uint32_t>(q + 0x3c);
                pt.kind = r.get<std::uint32_t>(q);
                strip.push_back(pt);
            }
            // each triangle in the mesh of its last vertex's texture
            for (std::size_t v = 2; v < strip.size(); ++v) {
                if (strip[v].kind == kStripStart)
                    continue;
                const std::string texture = strip[v].texture < names.size() ? names[strip[v].texture] : std::string();
                const int mat = material(texture);
                SceneMesh& mesh = by_material[mat];
                if (mesh.primitives.empty()) {
                    mesh.name = room.scene.materials[static_cast<std::size_t>(mat)].name;
                    mesh.primitives.push_back({mat});
                }
                ScenePrimitive& prim = mesh.primitives.front();
                const std::array<std::size_t, 3> corners = strip[v].kind == kOdd
                                                               ? std::array<std::size_t, 3>{v - 1, v - 2, v}
                                                               : std::array<std::size_t, 3>{v - 2, v - 1, v};
                for (const std::size_t c : corners) {
                    prim.indices.push_back(static_cast<std::uint32_t>(prim.positions.size()));
                    prim.positions.push_back(strip[c].position);
                    prim.colors.push_back(strip[c].colour);
                    prim.uvs.push_back(strip[c].uv);
                }
            }
            a = body + 16 * static_cast<std::size_t>(qwords);
        }
    }
    for (auto& [mat, mesh] : by_material)
        room.scene.meshes.push_back(std::move(mesh));
    return room;
}

Scene collision_scene(const std::uint8_t* data, std::size_t size, const std::string& name) {
    const Reader r(data, size);
    if (size < 0x50 || std::memcmp(data, "@MCD", 4) != 0)
        throw std::runtime_error("not an .mcd");
    const auto pieces = r.get<std::uint16_t>(8);
    const std::size_t first = std::size_t{r.get<std::uint32_t>(0x4c)} + 0x10;
    const std::size_t meshes = first + pieces * kMcdPiece;
    Scene scene;
    scene.name = name + " collision";
    scene.nodes.push_back({"room"});
    SceneMaterial mat;
    mat.name = "recom collision";
    mat.double_sided = true;  // a wall stops from both sides
    scene.materials.push_back(mat);
    ScenePrimitive prim;
    auto corner = [&](const std::array<float, 3>& p) {
        prim.indices.push_back(static_cast<std::uint32_t>(prim.positions.size()));
        prim.positions.push_back({p[0], -p[1], -p[2]});  // Re:CoM's axes, y down, turned upright
        prim.uvs.push_back({0.0F, 0.0F});
        prim.colors.push_back({255, 255, 255, 255});
    };
    for (std::size_t k = 0; k < pieces; ++k) {
        const std::size_t piece = first + k * kMcdPiece;
        M4 matrix{};
        for (std::size_t i = 0; i < 16; ++i)
            matrix[i] = r.get<float>(piece + 0x40 + 4 * i);
        const std::size_t mesh = meshes + r.get<std::uint32_t>(piece + 12);
        std::array<std::uint32_t, 8> at{};
        for (std::size_t i = 0; i < 8; ++i)
            at[i] = r.get<std::uint32_t>(mesh + 0x20 + 4 * i);
        if (at[0] != 0x40)
            throw std::runtime_error("piece " + std::to_string(k) + "'s mesh is not where its offset says");
        const std::size_t planes = (at[2] - at[1]) / 16, vertices = (at[3] - at[2]) / 16;
        const std::size_t faces = (at[6] - at[5]) / kMcdFace;
        auto vertex = [&](std::uint16_t i) {
            const std::size_t v = mesh + at[2] + std::size_t{i} * 16;
            return apply(matrix, {r.get<float>(v), r.get<float>(v + 4), r.get<float>(v + 8)});
        };
        for (std::size_t f = 0; f < faces; ++f) {
            const std::size_t face = mesh + at[5] + f * kMcdFace;
            const auto plane = r.get<std::uint16_t>(face + 4);
            std::vector<std::uint16_t> c;
            for (std::size_t i = 0; i < 4; ++i) {
                const auto v = r.get<std::uint16_t>(face + 14 + 2 * i);
                if (v >= vertices)
                    throw std::runtime_error("a face's corner past its mesh's vertices");
                if (std::find(c.begin(), c.end(), v) == c.end())
                    c.push_back(v);
            }
            if (c.size() < 3 || plane >= planes)
                continue;
            // turned to face away from its plane's normal (placed by the piece's matrix, without its move)
            const std::size_t p = mesh + at[1] + std::size_t{plane} * 16;
            std::array<float, 3> n{r.get<float>(p), r.get<float>(p + 4), r.get<float>(p + 8)};
            n = {matrix[0] * n[0] + matrix[4] * n[1] + matrix[8] * n[2],
                 matrix[1] * n[0] + matrix[5] * n[1] + matrix[9] * n[2],
                 matrix[2] * n[0] + matrix[6] * n[1] + matrix[10] * n[2]};
            auto triangle = [&](std::uint16_t a, std::uint16_t b, std::uint16_t d) {
                const auto pa = vertex(a), pb = vertex(b), pd = vertex(d);
                const std::array<float, 3> e0{pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
                const std::array<float, 3> e1{pd[0] - pa[0], pd[1] - pa[1], pd[2] - pa[2]};
                const std::array<float, 3> m{e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2],
                                             e0[0] * e1[1] - e0[1] * e1[0]};
                const bool turn = m[0] * n[0] + m[1] * n[1] + m[2] * n[2] > 0;
                corner(pa);
                corner(turn ? pd : pb);
                corner(turn ? pb : pd);
            };
            triangle(c[0], c[1], c[2]);
            if (c.size() == 4)
                triangle(c[0], c[2], c[3]);
        }
    }
    if (prim.positions.empty())
        throw std::runtime_error("an .mcd with no faces");
    scene.meshes.push_back({"collision", 0, {}, {std::move(prim)}});
    return scene;
}

}  // namespace khdays::assets::recom
