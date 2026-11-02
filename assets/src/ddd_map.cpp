#include "khdays/assets/ddd_map.h"

#include <algorithm>
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
// A vertex of 20 bytes: u16 u, v (0x800: 1), its colour (R G B A: 0x80 is a colour's 1, brighter above, as
// the PSP's; the alpha's 1 is 0xff), position f32 x3 (x 32768).
constexpr std::size_t kObjectSize = 0x30;
constexpr std::size_t kTextureInfoSize = 0x20;
constexpr std::size_t kPmoTextures = 0xa0;
constexpr std::size_t kRecordSize = 24;
constexpr std::size_t kVertexSize = 20;
constexpr float kUvUnit = 2048.0F;
constexpr float kPositionUnit = 32768.0F;
constexpr unsigned kTriangles = 3, kStrip = 4;
constexpr std::uint16_t kBlended = 0x60;  // blended, added or taken away: drawn see-through

// A vertex's colour as glTF's (255 = 1.0): its red, green and blue doubled (0x80 is 1; brighter than 1.0 kept at
// 1.0: the rooms lit by them, many never above 0x80, were drawn at half their light), its alpha as it is.
std::array<std::uint8_t, 4> gltf_color(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const auto lit = [](std::uint8_t c) { return static_cast<std::uint8_t>(std::min(255, c * 2)); };
    return {lit(r), lit(g), lit(b), a};
}
// A BCD: "@bcd", its version (0x0a), its parts' count and offset at +8 / +0xc. A part, 0x30 bytes: its box (min, max:
// f32 x3 each), u16 vertex and face counts at +0x18 / +0x1a, the vertices' offset at +0x1c and the faces' at +0x20
// (from the file), then what speeds up the game's look-ups (a tree, its lists). A vertex: f32 x4 (w 1). A face, 48
// bytes: its plane (normal f32 x3, distance), its corners (i16 x4, the fourth -1 for a triangle; their order not
// always the normal's), then what it is (a floor, a wall).
constexpr std::size_t kBcdPartSize = 0x30;
constexpr std::size_t kBcdVertexSize = 16;
constexpr std::size_t kBcdFaceSize = 48;
// A TXA: "TXA\0", u16 its version (2), u16 its entries' count, the entries from +0x10, 0x38 bytes each: its name (16
// bytes), the texture it shows on (16), 8 bytes, u16 the sheet's width and height and a frame's, u32 its animations'
// count and the first's offset. An animation: its name (16 bytes), s16 where it plays again from, back from its end, u16
// its frames' count, u32 their offset; a frame, 12 bytes: u16 x, y (in the sheet), u16 its shortest and longest
// length (0: the shortest), 4 bytes. Then the sheets' own pixels (the HD pictures are used instead).
constexpr std::size_t kTxaEntry = 0x38;
constexpr std::size_t kTxaFrame = 12;

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

// `from`'s part at (x, y), `width` x `height`, kept within it.
DecodedTexture cut(const DecodedTexture& from, int x, int y, int width, int height) {
    x = std::clamp(x, 0, from.width);
    y = std::clamp(y, 0, from.height);
    width = std::clamp(width, 1, std::max(1, from.width - x));
    height = std::clamp(height, 1, std::max(1, from.height - y));
    DecodedTexture out;
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    for (int row = 0; row < height && y + row < from.height; ++row) {
        const auto src = from.rgba.begin() + (static_cast<std::ptrdiff_t>(y + row) * from.width + x) * 4;
        const auto n = static_cast<std::ptrdiff_t>(std::min(width, from.width - x)) * 4;
        std::copy(src, src + n, out.rgba.begin() + static_cast<std::ptrdiff_t>(row) * width * 4);
    }
    return out;
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
                    prim.colors.push_back(gltf_color(r.get<std::uint8_t>(v + 4), r.get<std::uint8_t>(v + 5),
                                                     r.get<std::uint8_t>(v + 6), r.get<std::uint8_t>(v + 7)));
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

std::vector<TextureAnimation> texture_animations(const std::uint8_t* data, std::size_t size) {
    const Reader r(data, size);
    if (size < 0x10 || std::memcmp(data, "TXA\0", 4) != 0)
        throw std::runtime_error("not a TXA");
    std::vector<TextureAnimation> out;
    const auto entries = r.get<std::uint16_t>(6);
    for (std::size_t e = 0; e < entries; ++e) {
        const std::size_t at = 0x10 + e * kTxaEntry;
        if (r.get<std::uint32_t>(at + 0x30) == 0)
            continue;  // (none seen without one)
        TextureAnimation a;
        a.texture = r.name(at + 0x10, 16);
        a.sheet_width = r.get<std::uint16_t>(at + 0x28);
        a.sheet_height = r.get<std::uint16_t>(at + 0x2a);
        a.frame_width = r.get<std::uint16_t>(at + 0x2c);
        a.frame_height = r.get<std::uint16_t>(at + 0x2e);
        const std::size_t animation = r.get<std::uint32_t>(at + 0x34);
        const auto back = r.get<std::int16_t>(animation + 0x10);
        const auto count = r.get<std::uint16_t>(animation + 0x12);
        const std::size_t frames = r.get<std::uint32_t>(animation + 0x14);
        const int target = back < 0 ? std::max(0, count + back) : 0;  // (back by its count, its start, in all read)
        int again = target;
        for (std::size_t f = 0; f < count; ++f) {
            const std::size_t fr = frames + f * kTxaFrame;
            const int shortest = r.get<std::uint16_t>(fr + 4), longest = r.get<std::uint16_t>(fr + 6);
            const int length = longest > shortest ? (shortest + longest + 1) / 2 : shortest;
            if (length == 0) {  // shown for no time: left out
                again -= static_cast<int>(f) < target ? 1 : 0;
                continue;
            }
            a.frames.push_back({r.get<std::uint16_t>(fr), r.get<std::uint16_t>(fr + 2), length});
        }
        a.loop_from = std::clamp(again, 0, std::max(0, static_cast<int>(a.frames.size()) - 1));
        if (!a.frames.empty() && a.sheet_width > 0 && a.sheet_height > 0)
            out.push_back(std::move(a));
    }
    return out;
}

void animate_textures(Room* room, const std::vector<TextureAnimation>& animations,
                      const std::vector<std::string>& sheets, const PictureFinder& find,
                      const PictureSink& write_picture) {
    for (const TextureAnimation& a : animations) {
        const std::string file = a.texture + ".png";
        std::vector<SceneMaterial*> on;
        for (auto& m : room->scene.materials)
            if (m.texture == file)
                on.push_back(&m);
        if (on.empty())
            continue;  // a texture of the world's pieces, not the room's
        // its sheet: the texture's name with one character more or less
        const auto sheet_of = std::find_if(sheets.begin(), sheets.end(), [&](const std::string& s) {
            const std::string& longer = s.size() > a.texture.size() ? s : a.texture;
            const std::string& shorter = s.size() > a.texture.size() ? a.texture : s;
            return longer.size() == shorter.size() + 1 && longer.compare(0, shorter.size(), shorter) == 0;
        });
        const std::filesystem::path hd = sheet_of == sheets.end() ? std::filesystem::path() : find(*sheet_of);
        if (hd.empty()) {
            room->report.push_back(a.texture + ": no HD sheet of its animation in the copy; shown still");
            continue;
        }
        DecodedTexture sheet;
        try {
            sheet = read_dds(hd);
        } catch (const std::exception& e) {
            room->report.push_back(a.texture + "'s animation: " + e.what() + "; shown still");
            continue;
        }
        // each place cut once, at the HD sheet's scale
        const float sx = static_cast<float>(sheet.width) / static_cast<float>(a.sheet_width);
        const float sy = static_cast<float>(sheet.height) / static_cast<float>(a.sheet_height);
        std::vector<std::string> pictures;
        std::map<std::pair<int, int>, int> picture_of;
        std::vector<std::array<int, 2>> frames;
        for (const auto& f : a.frames) {
            auto found = picture_of.find({f.x, f.y});
            if (found == picture_of.end()) {
                const std::string name = a.texture + "_" + std::to_string(pictures.size()) + ".png";
                write_picture(name, cut(sheet, static_cast<int>(std::lround(static_cast<float>(f.x) * sx)),
                                        static_cast<int>(std::lround(static_cast<float>(f.y) * sy)),
                                        static_cast<int>(std::lround(static_cast<float>(a.frame_width) * sx)),
                                        static_cast<int>(std::lround(static_cast<float>(a.frame_height) * sy))));
                found = picture_of.emplace(std::make_pair(f.x, f.y), static_cast<int>(pictures.size())).first;
                pictures.push_back(name);
            }
            frames.push_back({found->second, f.length});
        }
        for (SceneMaterial* m : on) {
            m->frame_pictures = pictures;
            m->frames = frames;
            m->frames_loop = true;
            m->frames_loop_from = a.loop_from;
        }
    }
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
