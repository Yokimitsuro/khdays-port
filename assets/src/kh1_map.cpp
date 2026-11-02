#include "khdays/assets/kh1_map.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "khdays/assets/dds.h"

namespace khdays::assets::kh1 {

namespace {

// What a room's files hold (docs/FORMATS.md, "KINGDOM HEARTS FINAL MIX's rooms"):
// .bin: u32 entry count, then from +0x10 each entry's offset and size (u32 each); the room's model is an entry whose
// remastered asset is an .mvbl (named by the entry's offset).
// .img: its texture pages from the u32 at +0x18, their bytes at +0x1c, 64 KB each. Their HD pictures are named
// <that + page x 0x10000 + n>, n the texture's place among those of its page, the pages counted as the remaster
// numbers them: the plain ones from the lowest address, sixteen at most (0x2600 to 0x3500, a megabyte of the PS2's
// video memory), then those in the picture buffer's spare bits. A plain page past the sixteen (dc00_01's 0x3600) has
// the .img's one HD picture without a number, "_NoName".
// .mvbl: u32 0x9696, its mesh table's length (u32 at +4) and its textures' (u32 at +8); the textures from +0x10, 16
// bytes each (the PS2's TEX0 and CLAMP words: the page at TEX0's bits 0-13, its kind at 20-25, 0x1b one in the
// picture buffer's spare bits); each mesh's box (8 corners, f32 x4 each); the marks 0x11111111 ... 0x44444444; the
// mesh table, 16 bytes a mesh (u32 0x30000000 for a mesh, its offset). A mesh: u32 0x01234567, u32 1, 8 bytes, u32,
// u32 its vertex count at +0x14, 8 bytes, its texture's 16 bytes at +0x20, then its vertices, 32 bytes each: position
// f32 x3, colour (R G B A, 0x80 is 1), u, v f32 (0..1 over its texture, past it repeated), u32 what it ends (0x1000
// nothing: a strip starts; 1 or 2 a triangle, the strip's parity; 0 a triangle), u32 0.
constexpr std::uint32_t kMvbl = 0x9696;
constexpr std::uint32_t kMesh = 0x30000000;
constexpr std::uint32_t kMeshMagic = 0x01234567;
constexpr std::size_t kMeshHeader = 0x30;
constexpr std::size_t kVertexSize = 32;
constexpr std::size_t kTextureSize = 16;
constexpr std::size_t kBoxSize = 0x80;
constexpr std::uint32_t kStripStart = 0x1000, kOdd = 2;
constexpr std::uint32_t kPageStride = 0x10000;
constexpr unsigned kHalfPicture = 0x1b;  // PSMT8H: a page kept in the picture buffer's spare bits, given after the rest
constexpr const char* kNoName = "_NoName";
constexpr std::uint32_t kPlainPagesNumbered = 16;
// A .bin's collision entry: u32 0x48 (its header's length), then each section's offset and length (u32 each, from
// the entry), each section after the one before: the second its vertices (f32 x3), the fourth its faces, 20 bytes
// each: corners (i16 x4, the fourth -1 for a triangle), u32 its normal's (in a table of its own), f32 its plane's
// distance, u32 what it is.
constexpr std::uint32_t kCollisionHeader = 0x48;
constexpr std::size_t kCollisionVertex = 12;
constexpr std::size_t kCollisionFace = 20;
// A .gvbl (a sky): u32 0x4989, its group count at +0x14, the groups from +0x20, each after the one before: u32 0x25c,
// u32 its vertex count, 8 bytes, u32 its picture's offset in the .bin (the remaster's asset named by it), u32, u32 0
// or 5, u32, then its vertices as an .mvbl mesh's.
constexpr std::uint32_t kGvbl = 0x4989;
constexpr std::uint32_t kSkyGroup = 0x25c;
constexpr std::size_t kSkyHeader = 0x20;
constexpr std::size_t kSkyGroupHeader = 0x20;

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

private:
    const std::uint8_t* d_;
    std::size_t n_;
};

// A PS2 colour (0x80 = 1.0) as glTF's (255 = 1.0); brighter than 1.0 is kept at 1.0.
std::array<std::uint8_t, 4> gltf_color(const std::array<std::uint8_t, 4>& c) {
    std::array<std::uint8_t, 4> out{};
    for (std::size_t i = 0; i < 4; ++i)
        out[i] = static_cast<std::uint8_t>(std::min(255, c[i] * 2));
    return out;
}

// `count` 32-byte vertices from `at` (an .mvbl mesh's, a .gvbl group's) added to `prim`, their strips as triangles.
void add_strips(const Reader& r, std::size_t at, std::size_t count, ScenePrimitive* prim) {
    const auto base = static_cast<std::uint32_t>(prim->positions.size());
    for (std::size_t k = 0; k < count; ++k) {
        const std::size_t v = at + k * kVertexSize;
        // KH1's axes, y down, turned upright about x
        prim->positions.push_back({r.get<float>(v), -r.get<float>(v + 4), -r.get<float>(v + 8)});
        prim->colors.push_back(gltf_color({r.get<std::uint8_t>(v + 12), r.get<std::uint8_t>(v + 13),
                                           r.get<std::uint8_t>(v + 14), r.get<std::uint8_t>(v + 15)}));
        prim->uvs.push_back({r.get<float>(v + 16), r.get<float>(v + 20)});
        const auto ends = r.get<std::uint32_t>(v + 24);
        if (k < 2 || ends == kStripStart)
            continue;
        const auto c = base + static_cast<std::uint32_t>(k);
        if (ends == kOdd)
            prim->indices.insert(prim->indices.end(), {c - 1, c - 2, c});
        else
            prim->indices.insert(prim->indices.end(), {c - 2, c - 1, c});
    }
}

}  // namespace

std::vector<Entry> bin_entries(const std::uint8_t* data, std::size_t size) {
    const Reader r(data, size);
    const auto count = r.get<std::uint32_t>(0);
    std::vector<Entry> out;
    for (std::size_t i = 0; i < count; ++i) {
        const Entry e{r.get<std::uint32_t>(0x10 + 8 * i), r.get<std::uint32_t>(0x14 + 8 * i)};
        if (e.offset + e.size > size || e.offset + e.size < e.offset)
            throw std::runtime_error("a .bin entry past the file's end");
        out.push_back(e);
    }
    return out;
}

ImgPages img_pages(const std::uint8_t* data, std::size_t size) {
    const Reader r(data, size);
    return {r.get<std::uint32_t>(0x18), r.get<std::uint32_t>(0x1c) / kPageStride};
}

Scene collision_scene(const std::uint8_t* data, std::size_t size, const std::string& name) {
    const Reader r(data, size);
    for (const Entry& e : bin_entries(data, size)) {
        if (e.size < kCollisionHeader || r.get<std::uint32_t>(e.offset) != kCollisionHeader)
            continue;
        // the sections chained: each one's offset its last's end
        std::array<std::uint32_t, 8> at{}, length{};
        bool chained = true;
        for (std::size_t s = 0; s < 8 && chained; ++s) {
            at[s] = r.get<std::uint32_t>(e.offset + 8 * s);
            length[s] = r.get<std::uint32_t>(e.offset + 8 * s + 4);
            chained = (s == 0 || at[s] == at[s - 1] + length[s - 1]) && at[s] + length[s] <= e.size;
        }
        if (!chained)
            continue;
        const std::size_t vertices = length[1] / kCollisionVertex, faces = length[3] / kCollisionFace;
        Scene scene;
        scene.name = name + " collision";
        scene.nodes.push_back({"room"});
        SceneMaterial mat;
        mat.name = "kh1 collision";
        mat.double_sided = true;  // a wall stops from both sides
        scene.materials.push_back(mat);
        ScenePrimitive prim;
        auto vertex = [&](std::int16_t i) {
            const std::size_t v = e.offset + at[1] + static_cast<std::size_t>(i) * kCollisionVertex;
            return std::array<float, 3>{r.get<float>(v), -r.get<float>(v + 4), -r.get<float>(v + 8)};
        };
        auto corner = [&](const std::array<float, 3>& p) {
            prim.indices.push_back(static_cast<std::uint32_t>(prim.positions.size()));
            prim.positions.push_back(p);
            prim.uvs.push_back({0.0F, 0.0F});
            prim.colors.push_back({255, 255, 255, 255});
        };
        for (std::size_t f = 0; f < faces; ++f) {
            const std::size_t face = e.offset + at[3] + f * kCollisionFace;
            const std::array<std::int16_t, 4> c{r.get<std::int16_t>(face), r.get<std::int16_t>(face + 2),
                                                r.get<std::int16_t>(face + 4), r.get<std::int16_t>(face + 6)};
            auto within = [&](std::int16_t i) { return i >= 0 && static_cast<std::size_t>(i) < vertices; };
            if (!within(c[0]) || !within(c[1]) || !within(c[2]) || (c[3] >= 0 && !within(c[3])))
                continue;  // (none seen in the rooms read)
            corner(vertex(c[0]));
            corner(vertex(c[1]));
            corner(vertex(c[2]));
            if (c[3] >= 0) {  // a four-sided face, as two triangles
                corner(vertex(c[0]));
                corner(vertex(c[2]));
                corner(vertex(c[3]));
            }
        }
        if (prim.positions.empty())
            throw std::runtime_error("a collision with no faces");
        scene.meshes.push_back({"collision", 0, {}, {std::move(prim)}});
        return scene;
    }
    throw std::runtime_error("no collision entry in the .bin");
}

Room room_model(const std::uint8_t* data, std::size_t size, const std::string& name, const ImgPages& img,
                const PictureFinder& find, const PictureSink& write_picture) {
    const Reader r(data, size);
    if (size < 0x10 || r.get<std::uint32_t>(0) != kMvbl)
        throw std::runtime_error("not an .mvbl");
    const auto meshes = r.get<std::uint32_t>(4);
    const auto textures = r.get<std::uint32_t>(8);

    // each texture's HD picture: its page's place (the plain pages by their address, then those in the picture
    // buffer's spare bits), then its own among its page's
    struct Texture {
        std::array<std::uint8_t, kTextureSize> words{};
        std::string picture;  // its HD picture's name
        bool repeat_u = false, repeat_v = false;
    };
    std::vector<Texture> list(textures);
    std::map<std::pair<bool, std::uint32_t>, std::vector<std::size_t>> pages;
    for (std::size_t i = 0; i < textures; ++i) {
        const std::size_t at = 0x10 + i * kTextureSize;
        std::memcpy(list[i].words.data(), data + at, kTextureSize);
        const auto tex0 = r.get<std::uint64_t>(at);
        const auto clamp = r.get<std::uint64_t>(at + 8);
        list[i].repeat_u = (clamp & 3) == 3;  // REGION_REPEAT
        list[i].repeat_v = ((clamp >> 2) & 3) == 3;
        pages[{((tex0 >> 20) & 0x3f) == kHalfPicture, static_cast<std::uint32_t>(tex0 & 0x3fff)}].push_back(i);
    }
    std::uint32_t page = 0, plain = 0;
    for (const auto& [key, members] : pages) {
        const bool held = key.first || plain++ < kPlainPagesNumbered;
        for (std::size_t n = 0; n < members.size(); ++n) {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%x", img.at + page * kPageStride + static_cast<std::uint32_t>(n));
            list[members[n]].picture = held ? hex : kNoName;
        }
        page += held ? 1 : 0;
    }

    Room room;
    room.scene.name = name;
    room.scene.nodes.push_back({name, -1});
    std::map<std::size_t, int> material_of;  // by texture
    std::map<std::string, bool> picture_alpha;  // written pictures (textures may share one): whether they cut out
    std::map<int, SceneMesh> by_material;
    auto material = [&](std::size_t texture) {
        if (auto found = material_of.find(texture); found != material_of.end())
            return found->second;
        const Texture& t = list[texture];
        SceneMaterial m;
        m.name = name + "_" + t.picture + "_" + std::to_string(texture);
        m.double_sided = true;
        m.repeat = {t.repeat_u, t.repeat_v};
        auto written = picture_alpha.find(t.picture);
        if (written == picture_alpha.end()) {
            const std::filesystem::path hd = find(t.picture);
            if (!hd.empty()) {
                try {
                    const DecodedTexture picture = read_dds(hd);
                    bool alpha = false;
                    for (std::size_t i = 3; i < picture.rgba.size() && !alpha; i += 4)
                        alpha = picture.rgba[i] < 250;
                    write_picture(t.picture + ".png", picture);
                    written = picture_alpha.emplace(t.picture, alpha).first;
                } catch (const std::exception& e) {
                    room.report.push_back(t.picture + ": " + e.what());
                }
            } else {
                room.report.push_back(t.picture + ": no HD picture in the copy; its surfaces in their colours");
            }
        }
        if (written != picture_alpha.end()) {
            m.texture = t.picture + ".png";
            m.alpha_mode = written->second ? NeutralMesh::AlphaMode::mask : NeutralMesh::AlphaMode::opaque;
        }
        const int index = static_cast<int>(room.scene.materials.size());
        room.scene.materials.push_back(m);
        material_of.emplace(texture, index);
        return index;
    };

    // the mesh table follows the textures, each mesh's box and the marks
    const std::size_t marks = 0x10 + textures * kTextureSize + meshes * kBoxSize;
    if (r.get<std::uint32_t>(marks) != 0x11111111 || r.get<std::uint32_t>(marks + 12) != 0x44444444)
        throw std::runtime_error("an .mvbl whose mesh table is not where its counts put it");
    const std::size_t table = marks + 0x10;
    for (std::size_t i = 0; i < meshes; ++i) {
        if (r.get<std::uint32_t>(table + 16 * i) != kMesh)
            continue;
        const std::size_t at = r.get<std::uint32_t>(table + 16 * i + 4);
        if (r.get<std::uint32_t>(at) != kMeshMagic) {
            room.report.push_back("mesh " + std::to_string(i) + ": not read (no mesh at its offset)");
            continue;
        }
        const auto count = r.get<std::uint32_t>(at + 0x14);
        std::array<std::uint8_t, kTextureSize> words{};
        std::memcpy(words.data(), data + at + 0x20, kTextureSize);
        const auto texture = std::find_if(list.begin(), list.end(), [&](const Texture& t) { return t.words == words; });
        if (texture == list.end()) {
            room.report.push_back("mesh " + std::to_string(i) + ": its texture not among the room's");
            continue;
        }
        const int m = material(static_cast<std::size_t>(texture - list.begin()));
        SceneMesh& mesh = by_material[m];
        if (mesh.primitives.empty()) {
            mesh.name = room.scene.materials[static_cast<std::size_t>(m)].name;
            mesh.primitives.push_back({m});
        }
        add_strips(r, at + kMeshHeader, count, &mesh.primitives.front());
    }
    for (auto& [m, mesh] : by_material)
        room.scene.meshes.push_back(std::move(mesh));
    return room;
}

void add_sky(Room* room, const std::uint8_t* data, std::size_t size, const PictureFinder& find,
             const PictureSink& write_picture) {
    const Reader r(data, size);
    if (size < kSkyHeader || r.get<std::uint32_t>(0) != kGvbl)
        throw std::runtime_error("not a .gvbl");
    Scene& scene = room->scene;
    if (scene.nodes.empty())
        scene.nodes.push_back({scene.name, -1});
    // the room's sky node, under its root: made by the first sky
    int node = -1;
    for (std::size_t n = 0; n < scene.nodes.size() && node < 0; ++n)
        node = scene.nodes[n].sky ? static_cast<int>(n) : -1;
    if (node < 0) {
        SceneNode sky;
        sky.name = "sky";
        sky.parent = 0;
        sky.sky = true;
        scene.nodes.push_back(sky);
        node = static_cast<int>(scene.nodes.size() - 1);
    }
    const auto groups = r.get<std::uint32_t>(0x14);
    std::size_t at = kSkyHeader;
    for (std::size_t g = 0; g < groups; ++g) {
        if (r.get<std::uint32_t>(at) != kSkyGroup)
            throw std::runtime_error("a .gvbl group not where the one before it ends");
        const auto count = r.get<std::uint32_t>(at + 4);
        char hex[16];
        std::snprintf(hex, sizeof(hex), "%x", r.get<std::uint32_t>(at + 0x10));
        // its material: blended (the skies' layers fade by their vertices' alpha), one a picture
        const std::string file = std::string("sky_") + hex + ".png";
        int m = -1;
        for (std::size_t i = 0; i < scene.materials.size() && m < 0; ++i)
            m = scene.materials[i].texture == file ? static_cast<int>(i) : -1;
        if (m < 0) {
            SceneMaterial mat;
            mat.name = scene.name + "_sky_" + hex;
            mat.double_sided = true;
            mat.alpha_mode = NeutralMesh::AlphaMode::blend;
            const std::filesystem::path hd = find(hex);
            if (!hd.empty()) {
                try {
                    write_picture(file, read_dds(hd));
                    mat.texture = file;
                } catch (const std::exception& e) {
                    room->report.push_back(std::string("sky ") + hex + ": " + e.what());
                }
            } else {
                room->report.push_back(std::string("sky ") + hex + ": no HD picture in the copy; in its colours");
            }
            m = static_cast<int>(scene.materials.size());
            scene.materials.push_back(mat);
        }
        SceneMesh mesh;
        mesh.name = scene.name + "_sky_" + std::to_string(g) + "_" + hex;
        mesh.node = node;
        mesh.primitives.push_back({m});
        add_strips(r, at + kSkyGroupHeader, count, &mesh.primitives.front());
        scene.meshes.push_back(std::move(mesh));
        at += kSkyGroupHeader + std::size_t{count} * kVertexSize;
    }
}

}  // namespace khdays::assets::kh1
