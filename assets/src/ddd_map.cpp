#include "khdays/assets/ddd_map.h"

#include <algorithm>
#include <array>
#include <cctype>
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
// PMO: "PMO\0", textures at +8 (u8) listed from +0xa0 (0x20 each, the name at +4, then f32 x2 how far its texture
// coordinates move each 60th of a second: water, waves, clouds), its section at +0x10 (else at +0x1c, a model with
// see-through parts only; offsets from the PMO).
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
// A BCD: "@bcd", its version (0x0a Dream Drop Distance's, 2 Birth by Sleep's), its parts' count and offset at +8 /
// +0xc. A part, 0x30 bytes (version 2's 0x40): its box (min, max: f32 x3 each), u16 vertex and face counts at +0x18 /
// +0x1a (version 2: +0x24 / +0x26), the vertices' offset at +0x1c and the faces' at +0x20 (+0x28 / +0x2c; from the
// file), then what speeds up the game's look-ups (a tree, its lists). A vertex: f32 x4 (w 1). A face, 48 bytes: its
// plane (normal f32 x3, distance), its corners (i16 x4, the fourth -1 for a triangle; their order not always the
// normal's), then what it is (a floor, a wall).
// Version 2's face: at +0x24 a u32 of what it is -- bits 0-3 its kind (1 a floor, 2 a wall), 4-7 its material (9
// water), bits 9-13 whom it stops (0x200 and 0x400 every floor and wall that does stop the characters; a sea's
// surface 0x2000 alone: the characters go through it).
constexpr std::size_t kBcdPartSize = 0x30, kBcd2PartSize = 0x40;
constexpr std::uint32_t kBcd2Stops = 0x3E00, kBcd2StopsCharacters = 0x0600;
// Under a water's surface the floors deeper than this get walls at that depth: the game keeps its characters out of
// deep water (Days has no swimming); the depth a character wades to.
constexpr float kWadingDepth = 0.6F;
// The walls made there: from this far under that depth to this far above it.
constexpr float kWaterWallBelow = 1.0F, kWaterWallAbove = 3.0F;
// The floors under a water's surface: within this far of its faces' box.
constexpr float kWaterReach = 5.0F;
// A floor's open edge: a step this far across it finds no floor there, at any height.
constexpr float kStepOut = 0.3F;
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

// A model's mesh as read, before it is placed: its texture (in its PMO's list), whether blended, triangles or strips,
// the strips' lengths (none: one strip, its parts stitched by degenerate triangles), its vertices in the model's own
// units.
struct ReadVertex {
    std::array<float, 2> uv{};
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    std::array<float, 3> position{};
};

struct ReadMesh {
    std::uint8_t texture = 0;
    bool blended = false;
    unsigned primitive = 0;
    std::vector<std::uint16_t> strips;
    std::vector<ReadVertex> vertices;
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

// A version 4 model's meshes (Dream Drop Distance's): its section's records, its vertices of 20 bytes after them;
// `problem` says why none were read.
std::vector<ReadMesh> pmo4_meshes(const Reader& r, std::size_t pmo, std::string* problem) {
    // its section (+0x1c, when +0x10 has none, is a model's only one; else it points within the vertices)
    std::uint32_t at = r.get<std::uint32_t>(pmo + 0x10);
    if (at == 0)
        at = r.get<std::uint32_t>(pmo + 0x1c);
    if (at == 0)
        return {};
    const std::size_t s = pmo + at;
    const auto vertices_at = r.get<std::uint32_t>(s + 0x18);
    const auto bytes = r.get<std::uint32_t>(s + 0x1c);
    const std::vector<Record> recs = records(r, pmo, s);
    std::size_t held = 0;
    bool readable = true;
    for (const Record& rec : recs) {
        held += std::size_t{rec.count} * rec.vertex_size;
        readable =
            readable && rec.vertex_size == kVertexSize && (rec.primitive == kTriangles || rec.primitive == kStrip);
    }
    if (!readable || held != bytes) {
        *problem = std::to_string(recs.size()) + " records, " + std::to_string(held) + " of " + std::to_string(bytes) +
                   " vertex bytes, or a vertex of another size";
        return {};
    }
    std::vector<ReadMesh> out;
    std::size_t v = pmo + vertices_at;
    for (const Record& rec : recs) {
        ReadMesh m;
        m.texture = rec.texture;
        m.blended = rec.blended;
        m.primitive = rec.primitive;
        for (std::size_t k = 0; k < rec.count; ++k, v += kVertexSize) {
            ReadVertex rv;
            rv.uv = {r.get<std::uint16_t>(v) / kUvUnit, r.get<std::uint16_t>(v + 2) / kUvUnit};
            rv.color = gltf_color(r.get<std::uint8_t>(v + 4), r.get<std::uint8_t>(v + 5), r.get<std::uint8_t>(v + 6),
                                  r.get<std::uint8_t>(v + 7));
            rv.position = {r.get<float>(v + 8) / kPositionUnit, r.get<float>(v + 12) / kPositionUnit,
                           r.get<float>(v + 16) / kPositionUnit};
            m.vertices.push_back(rv);
        }
        out.push_back(std::move(m));
    }
    return out;
}

// A version 3 model's meshes (Birth by Sleep's): each of its two passes (+0x10 the first, +0x1c the second, drawn
// see-through) a run of meshes, each its header -- u16 its vertex count (0 ends the run), u8 its texture, u8 its
// vertex's size, u32 its vertex form (the PSP's: texture coordinates in bits 0-1, colour 2-4, normal 5-6, position
// 7-8, weights 9-10 and their count 14-16; one colour of the mesh's after the header with bit 24; the primitive in
// the top four bits), u8, u8 its strips' count, u16 its drawing (as version 4's) -- then its colour, its strips'
// lengths (u16 each), and from the next 4 bytes its vertices; the next mesh after them, from the next 4 bytes.
// Positions times the model's scale (f32 at +0x18). `problem` says why none were read.
std::vector<ReadMesh> pmo3_meshes(const Reader& r, std::size_t pmo, std::string* problem) {
    static constexpr std::size_t kTexCoord[] = {0, 1, 2, 4}, kColour[] = {0, 0, 0, 0, 2, 2, 2, 4},
                                 kNormal[] = {0, 1, 2, 4}, kPosition[] = {0, 1, 2, 4}, kWeight[] = {0, 1, 2, 4};
    const float model_scale = r.get<float>(pmo + 0x18);
    // a value of `size` bytes (signed but texture coordinates and weights) as a fraction: the PSP's 8 and 16 bits
    // are 1 at 128 and 32768
    const auto fraction = [&](std::size_t at, std::size_t size, bool is_signed) {
        switch (size) {
        case 1: return (is_signed ? r.get<std::int8_t>(at) : r.get<std::uint8_t>(at)) / 128.0F;
        case 2: return (is_signed ? r.get<std::int16_t>(at) : r.get<std::uint16_t>(at)) / 32768.0F;
        default: return r.get<float>(at);
        }
    };
    const auto colour = [&](std::size_t at, unsigned form) -> std::array<std::uint8_t, 4> {
        if (form == 7)  // 8888
            return gltf_color(r.get<std::uint8_t>(at), r.get<std::uint8_t>(at + 1), r.get<std::uint8_t>(at + 2),
                              r.get<std::uint8_t>(at + 3));
        const auto c = r.get<std::uint16_t>(at);
        const auto wide = [](unsigned v, unsigned bits) {
            return static_cast<std::uint8_t>(v * 255 / ((1U << bits) - 1));
        };
        if (form == 4)  // 5650
            return {wide(c & 31, 5), wide((c >> 5) & 63, 6), wide(c >> 11, 5), 255};
        if (form == 5)  // 5551
            return {wide(c & 31, 5), wide((c >> 5) & 31, 5), wide((c >> 10) & 31, 5),
                    static_cast<std::uint8_t>(c >> 15 ? 255 : 0)};
        return {wide(c & 15, 4), wide((c >> 4) & 15, 4), wide((c >> 8) & 15, 4), wide(c >> 12, 4)};  // 4444
    };
    std::vector<ReadMesh> out;
    for (const std::size_t pass : {0x10, 0x1c}) {
        const auto first = r.get<std::uint32_t>(pmo + pass);
        if (first == 0)
            continue;
        std::size_t at = pmo + first;
        for (;;) {
            const auto count = r.get<std::uint16_t>(at);
            if (count == 0)
                break;
            const auto size = r.get<std::uint8_t>(at + 3);
            const auto form = r.get<std::uint32_t>(at + 4);
            const auto strips = r.get<std::uint8_t>(at + 9);
            ReadMesh m;
            m.texture = r.get<std::uint8_t>(at + 2);
            m.primitive = form >> 28;
            m.blended = pass == 0x1c || (r.get<std::uint16_t>(at + 10) & kBlended) != 0;
            std::size_t h = at + 12;
            std::array<std::uint8_t, 4> mesh_colour{255, 255, 255, 255};
            if (form & 0x01000000U) {
                mesh_colour = colour(h, 7);
                h += 4;
            }
            for (std::size_t s = 0; s < strips; ++s, h += 2)
                m.strips.push_back(r.get<std::uint16_t>(h));
            h = (h + 3) & ~std::size_t{3};
            // the vertex's parts in the PSP's order, each on its own size
            const std::size_t uv = kTexCoord[form & 3], col = kColour[(form >> 2) & 7], nrm = kNormal[(form >> 5) & 3],
                              pos = kPosition[(form >> 7) & 3], wgt = kWeight[(form >> 9) & 3];
            const std::size_t weights = wgt != 0 ? ((form >> 14) & 7) + 1 : 0;
            std::size_t offset = 0, widest = 1;
            const auto place = [&](std::size_t n, std::size_t part) {
                if (part == 0)
                    return std::size_t{0};
                offset = (offset + part - 1) / part * part;
                const std::size_t here = offset;
                offset += n * part;
                widest = std::max(widest, part);
                return here;
            };
            place(weights, wgt);
            const std::size_t uv_at = place(2, uv), col_at = place(1, col);
            place(3, nrm);
            const std::size_t pos_at = place(3, pos);
            if (pos == 0 || (offset + widest - 1) / widest * widest != size ||
                (m.primitive != kTriangles && m.primitive != kStrip)) {
                *problem = "a mesh of vertex form " + std::to_string(form) + " not read";
                return {};
            }
            for (std::size_t k = 0; k < count; ++k) {
                const std::size_t v = h + k * size;
                ReadVertex rv;
                if (uv != 0)
                    rv.uv = {fraction(v + uv_at, uv, false), fraction(v + uv_at + uv, uv, false)};
                rv.color = col != 0 ? colour(v + col_at, (form >> 2) & 7)
                                    : gltf_color(mesh_colour[0], mesh_colour[1], mesh_colour[2], mesh_colour[3]);
                for (std::size_t c = 0; c < 3; ++c)
                    rv.position[c] = fraction(v + pos_at + c * pos, pos, true) * model_scale;
                m.vertices.push_back(rv);
            }
            out.push_back(std::move(m));
            at = (h + std::size_t{count} * size + 3) & ~std::size_t{3};
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

std::vector<ArcMember> arc_members(const std::uint8_t* data, std::size_t size) {
    const Reader r(data, size);
    if (size < 0x10 || std::memcmp(data, "ARC", 4) != 0)
        throw std::runtime_error("not an ARC");
    std::vector<ArcMember> out;
    const auto count = r.get<std::uint16_t>(6);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = 0x10 + i * 0x20;
        ArcMember m;
        m.kind = r.get<std::uint32_t>(at);
        m.offset = r.get<std::uint32_t>(at + 4);
        m.size = r.get<std::uint32_t>(at + 8);
        m.name = r.name(at + 16, 16);
        if (m.offset > size || m.size > size - m.offset)
            throw std::runtime_error("an ARC member past its end: " + m.name);
        out.push_back(std::move(m));
    }
    return out;
}

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
    // a material for each texture, blended or not, and its scroll: its picture written once
    std::map<std::tuple<std::string, bool, std::array<float, 2>>, int> material_of;
    std::map<std::string, bool> picture_alpha;  // written pictures: whether they hold see-through texels
    std::map<std::pair<int, bool>, SceneMesh> meshes;  // by material, and whether a sky's
    int sky_node = -1;
    auto material = [&](const std::string& texture, bool blended, const std::array<float, 2>& scroll) {
        const auto key = std::make_tuple(texture, blended, scroll);
        if (auto found = material_of.find(key); found != material_of.end())
            return found->second;
        SceneMaterial m;
        const bool scrolls = scroll[0] != 0.0F || scroll[1] != 0.0F;
        m.name = texture + (blended ? "_blend" : "") + (scrolls ? "_scroll" : "");
        m.scroll = scroll;
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
        std::vector<std::array<float, 2>> scrolls;
        for (std::size_t t = 0; t < r.get<std::uint8_t>(pmo + 8); ++t) {
            const std::size_t info = pmo + kPmoTextures + t * kTextureInfoSize;
            textures.push_back(r.name(info + 4, 12));
            std::array<float, 2> s{r.get<float>(info + 16), r.get<float>(info + 20)};
            for (float& c : s)
                c = std::isfinite(c) && std::fabs(c) < 1.0F ? c : 0.0F;  // (a texture's width a frame at most)
            scrolls.push_back(s);
        }
        // a sky: the object's bit 0 at +0x2c (a sphere of a metre or two about the origin, drawn around the camera:
        // di_01's lp_sky2, nd_02's sora01, Birth by Sleep's lp_he01_11), or its textures named a sky or a far
        // backdrop (tm_01's dome of 200 metres, tm01_sky01, and its tm01_fore01 cylinders have no bit): drawn
        // behind everything, they would hide the room from outside
        const bool sky =
            (r.get<std::uint8_t>(o + 0x2c) & 1) != 0 ||
            std::any_of(textures.begin(), textures.end(), [](const std::string& t) {
                const auto fore = t.find("fore");
                return t.find("sky") != std::string::npos || (fore != std::string::npos && fore + 4 < t.size() &&
                                                              std::isdigit(static_cast<unsigned char>(t[fore + 4])));
            });
        if (sky && sky_node < 0) {
            SceneNode node;
            node.name = "sky";
            node.parent = 0;
            node.sky = true;
            room.scene.nodes.push_back(node);
            sky_node = static_cast<int>(room.scene.nodes.size() - 1);
        }
        // its meshes, as its version keeps them
        std::string problem;
        const std::vector<ReadMesh> read =
            r.get<std::uint8_t>(pmo + 6) == 3 ? pmo3_meshes(r, pmo, &problem) : pmo4_meshes(r, pmo, &problem);
        if (!problem.empty()) {
            room.report.push_back("object " + std::to_string(i) + ": its meshes not read (" + problem + ")");
            continue;
        }
        for (const ReadMesh& rm : read) {
            const std::string texture = rm.texture < textures.size() ? textures[rm.texture] : std::string();
            const int m = material(texture, rm.blended,
                                   rm.texture < scrolls.size() ? scrolls[rm.texture] : std::array<float, 2>{});
            SceneMesh& mesh = meshes[{m, sky}];
            if (mesh.primitives.empty()) {
                mesh.name = room.scene.materials[static_cast<std::size_t>(m)].name + (sky ? "_sky" : "");
                mesh.node = sky ? sky_node : 0;
                mesh.primitives.push_back({m});
            }
            ScenePrimitive& prim = mesh.primitives.front();
            const auto base = static_cast<std::uint32_t>(prim.positions.size());
            std::vector<std::array<float, 3>> placed;
            for (const ReadVertex& rv : rm.vertices) {
                prim.uvs.push_back(rv.uv);
                prim.colors.push_back(rv.color);
                std::array<float, 3> q{rv.position[0] * scale[0], rv.position[1] * scale[1], rv.position[2] * scale[2]};
                q = turned(q, turn);
                q = {q[0] + move[0], q[1] + move[1], q[2] + move[2]};
                prim.positions.push_back(q);
                placed.push_back(q);
            }
            const auto count = static_cast<std::uint32_t>(rm.vertices.size());
            if (rm.primitive == kTriangles) {
                for (std::uint32_t t = 0; t + 2 < count; t += 3)
                    prim.indices.insert(prim.indices.end(), {base + t, base + t + 1, base + t + 2});
                continue;
            }
            // strips: each its own (version 3 gives their lengths), else one stitched by degenerate triangles
            std::vector<std::uint32_t> lengths(rm.strips.begin(), rm.strips.end());
            if (lengths.empty())
                lengths.push_back(count);
            std::uint32_t from = 0;
            for (const std::uint32_t length : lengths) {
                for (std::uint32_t t = 0; t + 2 < length && from + t + 2 < count; ++t) {
                    const std::uint32_t k = from + t;
                    const auto& a = placed[k];
                    const auto& b = placed[k + 1];
                    const auto& c = placed[k + 2];
                    if (a == b || b == c || a == c)
                        continue;  // a stitch between two strips
                    if (t % 2 == 0)
                        prim.indices.insert(prim.indices.end(), {base + k, base + k + 1, base + k + 2});
                    else
                        prim.indices.insert(prim.indices.end(), {base + k + 1, base + k, base + k + 2});
                }
                from += length;
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
    const bool v2 = r.get<std::uint32_t>(4) == 2;
    const std::size_t counts = v2 ? 0x24 : 0x18;  // then the faces', the vertices' offset, the faces'
    const auto parts = r.get<std::uint32_t>(8);
    const auto parts_at = r.get<std::uint32_t>(0xc);
    // version 2: the water's surfaces (left out) and the floors kept, for the walls at the wading depth
    struct Water {
        float height;
        std::array<float, 4> box;  // x, z least and most
    };
    std::vector<Water> waters;
    struct Floor {
        std::array<std::array<float, 3>, 3> corners;
        std::array<float, 3> facing;
    };
    std::vector<Floor> floors;  // (put in last, but those wholly in deep water)
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
    for (std::size_t part = 0; part < parts; ++part) {
        const std::size_t p = parts_at + part * (v2 ? kBcd2PartSize : kBcdPartSize);
        const auto vertices = r.get<std::uint16_t>(p + counts);
        const auto faces = r.get<std::uint16_t>(p + counts + 2);
        const auto vertices_at = r.get<std::uint32_t>(p + counts + 4);
        const auto faces_at = r.get<std::uint32_t>(p + counts + 8);
        auto vertex = [&](std::int16_t i) {
            if (i < 0 || i >= vertices)
                throw std::runtime_error("a face's corner past the vertices");
            const std::size_t v = vertices_at + static_cast<std::size_t>(i) * kBcdVertexSize;
            return std::array<float, 3>{r.get<float>(v), r.get<float>(v + 4), r.get<float>(v + 8)};
        };
        for (std::size_t f = 0; f < faces; ++f) {
            const std::size_t face = faces_at + f * kBcdFaceSize;
            const std::array<float, 3> facing{r.get<float>(face), r.get<float>(face + 4), r.get<float>(face + 8)};
            const std::size_t at = face + 0x10;
            const std::array<std::int16_t, 4> c{r.get<std::int16_t>(at), r.get<std::int16_t>(at + 2),
                                                r.get<std::int16_t>(at + 4), r.get<std::int16_t>(at + 6)};
            if (v2) {
                const auto what = r.get<std::uint32_t>(face + 0x24);
                if ((what & kBcd2Stops) != 0 && (what & kBcd2StopsCharacters) == 0) {
                    // a face that stops none of the characters: left out; a water's surface kept apart
                    if ((what & 0xF0U) == 0x90U && (what & 0x0FU) == 1U && facing[1] > 0.5F) {
                        Water w{0.0F, {1e30F, 1e30F, -1e30F, -1e30F}};
                        float sum = 0.0F;
                        int n = 0;
                        for (const std::int16_t i : c) {
                            if (i < 0)
                                continue;
                            const auto v = vertex(i);
                            sum += v[1];
                            ++n;
                            w.box = {std::min(w.box[0], v[0]), std::min(w.box[1], v[2]), std::max(w.box[2], v[0]),
                                     std::max(w.box[3], v[2])};
                        }
                        w.height = sum / static_cast<float>(n);
                        waters.push_back(w);
                    }
                    continue;
                }
            }
            if (facing[1] > 0.5F) {
                floors.push_back({{vertex(c[0]), vertex(c[1]), vertex(c[2])}, facing});
                if (c[3] >= 0)
                    floors.push_back({{vertex(c[0]), vertex(c[2]), vertex(c[3])}, facing});
                continue;
            }
            triangle(vertex(c[0]), vertex(c[1]), vertex(c[2]), facing);
            if (c[3] >= 0)  // a four-sided face, as two triangles
                triangle(vertex(c[0]), vertex(c[2]), vertex(c[3]), facing);
        }
    }
    // walls where a floor under a water's surface goes deeper than a character wades: along the line the floor
    // crosses that depth, each floor triangle crossing it giving one wall; a floor wholly deeper is left out (no one
    // stands there, and a room is placed by its floors)
    std::vector<bool> deep(floors.size(), false);
    for (const Water& w : waters) {
        const float depth = w.height - kWadingDepth;
        for (std::size_t i = 0; i < floors.size(); ++i) {
            const auto& t = floors[i].corners;
            const float mx = (t[0][0] + t[1][0] + t[2][0]) / 3.0F, mz = (t[0][2] + t[1][2] + t[2][2]) / 3.0F;
            if (mx < w.box[0] - kWaterReach || mx > w.box[2] + kWaterReach || mz < w.box[1] - kWaterReach ||
                mz > w.box[3] + kWaterReach)
                continue;
            if (t[0][1] < depth && t[1][1] < depth && t[2][1] < depth) {
                deep[i] = true;
                continue;
            }
            std::vector<std::array<float, 3>> crossing;
            for (int e = 0; e < 3; ++e) {
                const auto& a = t[static_cast<std::size_t>(e)];
                const auto& b = t[static_cast<std::size_t>((e + 1) % 3)];
                if ((a[1] - depth) * (b[1] - depth) >= 0.0F)
                    continue;  // (an edge touching the depth at a corner crosses nothing)
                const float k = (depth - a[1]) / (b[1] - a[1]);
                crossing.push_back({a[0] + (b[0] - a[0]) * k, depth, a[2] + (b[2] - a[2]) * k});
            }
            if (crossing.size() != 2)
                continue;
            const auto& p = crossing[0];
            const auto& q = crossing[1];
            const std::array<float, 3> p0{p[0], depth - kWaterWallBelow, p[2]}, p1{p[0], depth + kWaterWallAbove, p[2]};
            const std::array<float, 3> q0{q[0], depth - kWaterWallBelow, q[2]}, q1{q[0], depth + kWaterWallAbove, q[2]};
            const std::array<float, 3> out{q[2] - p[2], 0.0F, p[0] - q[0]};  // (either side: the wall stops both)
            triangle(p0, q0, q1, out);
            triangle(p0, q1, p1, out);
        }
    }
    // walls on the floors' open edges: an edge no other floor shares, past which a step finds no floor under it at
    // all -- where the game goes on into the next room (an exit of its own, not Days's) or the floor just ends; a
    // character would walk off and fall out of the room. An edge with a floor past it, lower (a ledge) or higher, stays
    // open.
    std::map<std::array<long long, 6>, int> shared;
    const auto key = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        std::array<long long, 3> p{std::llround(a[0] * 1000.0F), std::llround(a[1] * 1000.0F),
                                   std::llround(a[2] * 1000.0F)};
        std::array<long long, 3> q{std::llround(b[0] * 1000.0F), std::llround(b[1] * 1000.0F),
                                   std::llround(b[2] * 1000.0F)};
        if (q < p)
            std::swap(p, q);
        return std::array<long long, 6>{p[0], p[1], p[2], q[0], q[1], q[2]};
    };
    for (std::size_t i = 0; i < floors.size(); ++i) {
        if (deep[i])
            continue;
        for (std::size_t e = 0; e < 3; ++e)
            ++shared[key(floors[i].corners[e], floors[i].corners[(e + 1) % 3])];
    }
    // whether a floor (not deep) is at (x, z), at any height: a ledge's lower floor, a higher one to jump onto
    const auto floor_at = [&](float x, float z) {
        for (std::size_t i = 0; i < floors.size(); ++i) {
            if (deep[i])
                continue;
            const auto& t = floors[i].corners;
            const float d = (t[1][0] - t[0][0]) * (t[2][2] - t[0][2]) - (t[2][0] - t[0][0]) * (t[1][2] - t[0][2]);
            if (std::fabs(d) < 1e-9F)
                continue;
            const float u = ((x - t[0][0]) * (t[2][2] - t[0][2]) - (t[2][0] - t[0][0]) * (z - t[0][2])) / d;
            const float v = ((t[1][0] - t[0][0]) * (z - t[0][2]) - (x - t[0][0]) * (t[1][2] - t[0][2])) / d;
            if (u >= 0.0F && v >= 0.0F && u + v <= 1.0F)
                return true;
        }
        return false;
    };
    for (std::size_t i = 0; i < floors.size(); ++i) {
        if (deep[i])
            continue;
        const auto& t = floors[i].corners;
        for (std::size_t e = 0; e < 3; ++e) {
            const auto& a = t[e];
            const auto& b = t[(e + 1) % 3];
            const auto& other = t[(e + 2) % 3];
            if (shared[key(a, b)] != 1)
                continue;
            // a step out across the edge's middle, away from the triangle's third corner
            float ox = b[2] - a[2], oz = a[0] - b[0];
            const float length = std::sqrt(ox * ox + oz * oz);
            if (length < 1e-4F)
                continue;
            ox /= length;
            oz /= length;
            const float mx = (a[0] + b[0]) / 2.0F, mz = (a[2] + b[2]) / 2.0F;
            if ((other[0] - mx) * ox + (other[2] - mz) * oz > 0.0F) {
                ox = -ox;
                oz = -oz;
            }
            if (floor_at(mx + ox * kStepOut, mz + oz * kStepOut))
                continue;
            const float low = std::min(a[1], b[1]) - kWaterWallBelow, high = std::max(a[1], b[1]) + kWaterWallAbove;
            const std::array<float, 3> a0{a[0], low, a[2]}, a1{a[0], high, a[2]}, b0{b[0], low, b[2]},
                b1{b[0], high, b[2]};
            const std::array<float, 3> out{ox, 0.0F, oz};
            triangle(a0, b0, b1, out);
            triangle(a0, b1, a1, out);
        }
    }
    for (std::size_t i = 0; i < floors.size(); ++i) {
        if (!deep[i])
            triangle(floors[i].corners[0], floors[i].corners[1], floors[i].corners[2], floors[i].facing);
    }
    if (prim.positions.empty())
        throw std::runtime_error("a BCD with no faces");
    scene.meshes.push_back({"collision", 0, {}, {std::move(prim)}});
    return scene;
}

}  // namespace khdays::assets::ddd
