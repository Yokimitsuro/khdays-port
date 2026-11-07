// KINGDOM HEARTS II's effects (kh2_effect.h): the PAX read as KH2's loader walks it (FUN_1402bdde0,
// FUN_1402c21e0, FUN_1404b06d0) and played as its interpreter does (FUN_1404b0fd0, FUN_1404b08e0, FUN_1404b09e0,
// FUN_1404b0550 and each command's handlers in the descriptor table at 0x140779a00), our own reading of the
// player's executable (docs/FORMATS.md, "KH2's effects"). Each command's handler below names the function it
// follows.
#include "khdays/assets/kh2_effect.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace khdays::assets::kh2 {
namespace {

constexpr std::int32_t kFrame = 0x1000;
constexpr std::int32_t kNever = 0x70000000;

[[noreturn]] void fail(const std::string &why)
{
    throw std::runtime_error("KH2 effect: " + why);
}

struct Reader {
    const std::uint8_t *data;
    std::size_t size;

    void need(std::size_t at, std::size_t n) const
    {
        if (at > size || n > size - at)
            fail("reads past the end at " + std::to_string(at));
    }
    std::uint8_t u8(std::size_t at) const
    {
        need(at, 1);
        return data[at];
    }
    std::uint16_t u16(std::size_t at) const
    {
        need(at, 2);
        return std::uint16_t(data[at] | data[at + 1] << 8);
    }
    std::int16_t s16(std::size_t at) const { return std::int16_t(u16(at)); }
    std::uint32_t u32(std::size_t at) const
    {
        need(at, 4);
        return std::uint32_t(data[at]) | std::uint32_t(data[at + 1]) << 8 | std::uint32_t(data[at + 2]) << 16 |
               std::uint32_t(data[at + 3]) << 24;
    }
    std::int32_t s32(std::size_t at) const { return std::int32_t(u32(at)); }
    std::uint64_t u64(std::size_t at) const { return std::uint64_t(u32(at)) | std::uint64_t(u32(at + 4)) << 32; }
    float f32(std::size_t at) const
    {
        const std::uint32_t v = u32(at);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    std::vector<std::uint32_t> list(std::size_t at, std::size_t &next) const
    {
        const std::uint32_t n = u32(at);
        need(at + 4, std::size_t(n) * 4);
        std::vector<std::uint32_t> out(n);
        for (std::uint32_t k = 0; k < n; ++k)
            out[k] = u32(at + 4 + 4 * std::size_t(k));
        next = at + 4 + 4 * std::size_t(n);
        return out;
    }
};

// the PS2's 8-bit palette order (CSM1): entries 8-15 and 16-23 of each 32 swapped
int unswap(int i)
{
    return (i & 0xe7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
}

EffectPicture read_picture(const Reader &r, std::size_t at)
{
    EffectPicture p;
    p.tbp = r.u16(at);
    p.cbp = r.u16(at + 2);
    p.tbw = r.u16(at + 4);
    p.psm = r.u16(at + 6);
    p.x = r.u16(at + 8);
    p.y = r.u16(at + 10);
    p.w = r.u16(at + 12);
    p.h = r.u16(at + 14);
    const std::size_t n = std::size_t(p.w) * p.h, pixels = at + 32;
    p.indices.resize(n);
    if (p.psm == 0x13) { // 8-bit, a 256-entry palette after the pixels
        r.need(pixels, n + 1024);
        std::memcpy(p.indices.data(), r.data + pixels, n);
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 4; ++c)
                p.palette[std::size_t(i)][std::size_t(c)] = r.data[pixels + n + 4 * std::size_t(unswap(i)) + c];
    } else if (p.psm == 0x14) { // 4-bit, the low nibble first, a 16-entry palette after
        const std::size_t bytes = (n + 1) / 2;
        r.need(pixels, bytes + 64);
        for (std::size_t k = 0; k < n; ++k) {
            const std::uint8_t b = r.data[pixels + k / 2];
            p.indices[k] = std::uint8_t((k & 1) ? b >> 4 : b & 15);
        }
        for (int i = 0; i < 16; ++i)
            for (int c = 0; c < 4; ++c)
                p.palette[std::size_t(i)][std::size_t(c)] = r.data[pixels + bytes + 4 * std::size_t(i) + c];
    } else {
        fail("a picture of format " + std::to_string(p.psm));
    }
    return p;
}

std::size_t primitive_size(int kind)
{
    switch (kind) {
    case 0: return 0x14;
    case 1: return 0x1c;
    case 2:
    case 5: return 0x20;
    case 3:
    case 6: return 0x28;
    case 4: return 0x18;
    case 7: return 0x30;
    default: fail("a primitive of kind " + std::to_string(kind));
    }
}

EffectModel read_model(const Reader &r, std::size_t at)
{
    EffectModel m;
    const std::size_t h = at + 0x10; // FUN_1404b6860's header
    m.flags = r.u8(h);
    const std::int32_t prims = r.s32(h + 4), verts = r.s32(h + 8);
    const int nv = r.s16(h + 0x12);
    m.alpha = r.u8(h + 0x16);
    m.fix = r.u8(h + 0x17);
    if (prims < 0 || verts < 0 || nv < 0)
        fail("a model's offsets");
    for (int k = 0; k < nv; ++k) {
        const std::size_t v = h + std::size_t(verts) + 6 * std::size_t(k);
        m.vertices.push_back({float(r.s16(v)), float(r.s16(v + 2)), float(r.s16(v + 4))});
    }
    std::size_t p = h + std::size_t(prims);
    while (r.u8(p + 1) != 0xff) {
        const int kind = r.u8(p + 1), n = r.s16(p + 2);
        const std::uint64_t tex0 = r.u64(p + 8);
        const std::size_t size = primitive_size(kind);
        if (n < 0)
            fail("a model's primitive count");
        for (int q = 0; q < n; ++q) {
            const std::size_t it = p + 0x10 + size * std::size_t(q);
            EffectPrimitive e;
            e.kind = std::uint8_t(kind);
            // 0, 1, 2 triangles, 4 to 7 quads: RGBA each corner first, then the vertices (+0xc a triangle's, +0x10
            // a quad's); 1, 5 and 7 add each corner's normal (lighting, not drawn here); 2, 6 and 7 are textured,
            // their UV in 1/4096 of the page (+0x14 a triangle's, +0x18 a quad's). Kind 3 is in none of KH2's.
            if (kind == 3)
                fail("a primitive of kind 3 (none in KINGDOM HEARTS II's files)");
            e.corners = kind >= 4 ? 4 : 3;
            const std::size_t vertices = e.corners == 4 ? 0x10 : 0xc, uvs = e.corners == 4 ? 0x18 : 0x14;
            for (int c = 0; c < e.corners; ++c) {
                for (int k = 0; k < 4; ++k)
                    e.colour[std::size_t(c)][std::size_t(k)] = r.u8(it + 4 * std::size_t(c) + std::size_t(k));
                e.vertex[std::size_t(c)] = r.u16(it + vertices + 2 * std::size_t(c));
            }
            if (kind == 2 || kind == 6 || kind == 7) {
                e.tex0 = tex0;
                for (int c = 0; c < e.corners; ++c)
                    e.uv[std::size_t(c)] = {float(r.u16(it + uvs + 4 * std::size_t(c))) / 4096.0F,
                                            float(r.u16(it + uvs + 2 + 4 * std::size_t(c))) / 4096.0F};
            }
            for (int c = 0; c < e.corners; ++c)
                if (e.vertex[std::size_t(c)] >= m.vertices.size())
                    fail("a primitive's vertex past the model's");
            m.primitives.push_back(e);
        }
        p += 0x10 + size * std::size_t(n);
    }
    return m;
}

EffectShape read_shape(const Reader &r, std::size_t at)
{
    EffectShape s;
    const int count = r.u16(at + 0x14);
    for (int k = 0; k < count; ++k) {
        const std::size_t entry = at + 0x20 + 8 * std::size_t(k);
        EffectShapeFrame f;
        f.wait = r.u16(entry + 2);
        f.loop = (r.u8(entry + 4) & 0x80) != 0;
        const std::size_t fr = at + 0x10 + r.u16(entry);
        const int kind = r.u16(fr), n = r.u16(fr + 2);
        f.tex0 = r.u64(fr + 0x10);
        const std::size_t shape_size = (kind & 8) ? 24 : 16;
        const std::size_t colours = fr + 0x20 + std::size_t(n) * shape_size;
        for (int i = 0; i < n; ++i) {
            const std::size_t o = fr + 0x20 + shape_size * std::size_t(i);
            EffectShapeLayer l;
            if (kind & 8) { // four corners
                for (int c = 0; c < 4; ++c)
                    l.corners[std::size_t(c)] = {float(r.s16(o + 4 * std::size_t(c))) / 16.0F,
                                                 float(r.s16(o + 2 + 4 * std::size_t(c))) / 16.0F};
            } else { // a square x0 y0 x1 y1
                const float x0 = float(r.s16(o)) / 16.0F, y0 = float(r.s16(o + 2)) / 16.0F;
                const float x1 = float(r.s16(o + 4)) / 16.0F, y1 = float(r.s16(o + 6)) / 16.0F;
                l.corners = {{{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}};
            }
            const std::size_t uv = o + shape_size - 8;
            for (int c = 0; c < 4; ++c)
                l.rect[std::size_t(c)] = float(r.u16(uv + 2 * std::size_t(c))) / 4096.0F;
            for (int c = 0; c < 4; ++c)
                l.colour[std::size_t(c)] = r.u8(colours + 8 * std::size_t(i) + std::size_t(c));
            l.alpha = r.u8(colours + 8 * std::size_t(i) + 4);
            f.layers.push_back(l);
        }
        if (f.layers.empty())
            fail("a shape frame with no layer");
        s.frames.push_back(std::move(f));
    }
    if (s.frames.empty())
        fail("a shape with no frame");
    return s;
}

EffectEmitter read_emitter(const Reader &r, std::size_t e)
{
    EffectEmitter out;
    const std::size_t base = e + 0x110; // the file's offsets are from here (FUN_1404b06d0)
    out.length = r.s32(base);
    out.repeat = (r.u32(base + 4) & 1) != 0;
    std::size_t next;
    const auto layers = r.list(base + r.u32(base + 0xc), next);
    for (const std::uint32_t off : layers) {
        const std::size_t la = base + off;
        EffectLayer L;
        L.start = r.s32(la + 0x10);
        L.life = r.s32(la + 0x14);
        L.loop_start = r.s32(la + 0x18);
        L.loop_end = r.s32(la + 0x1c);
        L.order = r.u16(la + 0x24);
        const int count = r.u16(la + 0x26);
        for (int c = 0; c < count; ++c) {
            const std::size_t at = la + 0x28 + 16 * std::size_t(c);
            EffectCommand cmd;
            cmd.code = r.u32(at);
            const std::size_t size = r.u16(at + 4), keys = r.u16(at + 6);
            const std::size_t data = base + r.u32(at + 8), slots = base + r.u32(at + 12);
            r.need(data, size * keys);
            for (std::size_t k = 0; k < keys; ++k)
                cmd.keys.emplace_back(r.data + data + size * k, r.data + data + size * (k + 1));
            for (std::size_t k = 0; k < 4; ++k)
                cmd.slots[k] = slots + 4 * (k + 1) <= r.size ? r.s32(slots + 4 * k) : 0;
            L.commands.push_back(std::move(cmd));
        }
        out.layers.push_back(std::move(L));
    }
    return out;
}

} // namespace

EffectBlock read_effect_block(const std::uint8_t *data, std::size_t size, std::size_t at)
{
    const Reader r{data, size};
    if (r.u32(at) != 0x96)
        fail("no 0x96 block at " + std::to_string(at));
    std::size_t p = at + 4;
    std::vector<std::uint32_t> lists[5];
    for (auto &l : lists)
        l = r.list(p, p);
    EffectBlock b;
    for (const auto o : lists[0])
        b.emitters.push_back(read_emitter(r, at + o));
    for (const auto o : lists[1])
        b.pictures.push_back(read_picture(r, at + o));
    for (const auto o : lists[2])
        b.shapes.push_back(read_shape(r, at + o));
    for (const auto o : lists[3])
        b.models.push_back(read_model(r, at + o));
    for (const auto o : lists[4]) {
        EffectVertexList v;
        v.model = r.u32(at + o + 4);
        const std::uint32_t n = r.u32(at + o + 8);
        r.need(at + o + 12, std::size_t(n) * 2);
        for (std::uint32_t k = 0; k < n; ++k)
            v.vertices.push_back(r.u16(at + o + 12 + 2 * std::size_t(k)));
        if (v.model >= lists[3].size())
            fail("a vertex list of a model not there");
        b.vertex_lists.push_back(std::move(v));
    }
    return b;
}

std::vector<EffectBlock> read_effect(const std::uint8_t *data, std::size_t size)
{
    const Reader r{data, size};
    r.need(0, 16);
    if (std::memcmp(data, "PAX_", 4) != 0)
        fail("not a PAX");
    const std::size_t d = r.u32(0xc); // the element table's end: the 0x82 directory
    if (r.u32(d) != 0x82)
        fail("no 0x82 directory");
    const std::size_t count = r.u32(d + 0xc);
    std::size_t p = d + 0x10 + 32 * count, next;
    std::vector<EffectBlock> out;
    for (const auto o : r.list(p, next))
        out.push_back(read_effect_block(data, size, d + o));
    if (out.empty())
        fail("no effect block");
    return out;
}

EffectPage effect_page(const std::vector<const EffectPicture *> &pictures, std::uint64_t tex0)
{
    const int tbp = int(tex0 & 0x3fff), tbw = int((tex0 >> 14) & 63);
    EffectPage page;
    page.width = 1 << ((tex0 >> 26) & 15);
    page.height = 1 << ((tex0 >> 30) & 15);
    const int cbp = int((tex0 >> 37) & 0x3fff);
    std::vector<std::uint8_t> idx(std::size_t(page.width) * std::size_t(page.height));
    const int per_row = std::max(1, tbw / 2); // PSMT8 pages are 128 wide
    for (const auto *p : pictures) {
        if (p->tbw != tbw || p->tbp < tbp)
            continue;
        const int k = (p->tbp - tbp) / 0x20;
        const int ox = (k % per_row) * 128 + p->x, oy = (k / per_row) * 64 + p->y;
        for (int y = 0; y < p->h && oy + y < page.height; ++y)
            for (int x = 0; x < p->w && ox + x < page.width; ++x)
                idx[std::size_t(oy + y) * std::size_t(page.width) + std::size_t(ox + x)] =
                    p->indices[std::size_t(y) * p->w + std::size_t(x)];
    }
    const EffectPicture *pal = nullptr;
    for (const auto *p : pictures)
        if (p->cbp == cbp)
            pal = p;
    page.rgba.resize(idx.size() * 4);
    for (std::size_t i = 0; i < idx.size(); ++i) {
        const auto c = pal != nullptr ? pal->palette[idx[i]] : std::array<std::uint8_t, 4>{};
        page.rgba[4 * i] = c[0];
        page.rgba[4 * i + 1] = c[1];
        page.rgba[4 * i + 2] = c[2];
        page.rgba[4 * i + 3] = std::uint8_t(std::min(255, c[3] * 2));
    }
    return page;
}

// ------------------------------------------------------------------------------------------------ the player
namespace {

using M4 = EffectMatrix;

M4 identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// a . b (row vectors: a first)
M4 mul(const M4 &a, const M4 &b)
{
    M4 o{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a[std::size_t(i * 4 + k)] * b[std::size_t(k * 4 + j)];
            o[std::size_t(i * 4 + j)] = s;
        }
    return o;
}

std::array<float, 4> transformed(const std::array<float, 4> &v, const M4 &m)
{
    std::array<float, 4> o{};
    for (int j = 0; j < 4; ++j)
        o[std::size_t(j)] = v[0] * m[std::size_t(j)] + v[1] * m[std::size_t(4 + j)] + v[2] * m[std::size_t(8 + j)] +
                            v[3] * m[std::size_t(12 + j)];
    return o;
}

// KH2's sine table (0x14077e640): 4096 entries a turn, an angle of 0x10000 a turn
std::pair<float, float> sin_cos(std::int32_t a)
{
    constexpr double turn = 6.283185307179586;
    const int s = (a >> 4) & 0xfff, c = ((a + 0x4000) >> 4) & 0xfff;
    return {float(std::sin(turn * s / 4096.0)), float(std::sin(turn * c / 4096.0))};
}

M4 rot_x(std::int32_t a) // FUN_1404dc860
{
    const auto [s, c] = sin_cos(a);
    M4 m = identity();
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
    return m;
}

M4 rot_y(std::int32_t a) // FUN_1404dc8e0
{
    const auto [s, c] = sin_cos(a);
    M4 m = identity();
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
    return m;
}

M4 rot_z(std::int32_t a) // FUN_1404dc960
{
    const auto [s, c] = sin_cos(a);
    M4 m = identity();
    m[0] = c;
    m[1] = s;
    m[4] = -s;
    m[5] = c;
    return m;
}

float row_length(const M4 &m, int r)
{
    return std::sqrt(m[std::size_t(r * 4)] * m[std::size_t(r * 4)] + m[std::size_t(r * 4 + 1)] * m[std::size_t(r * 4 + 1)] +
                     m[std::size_t(r * 4 + 2)] * m[std::size_t(r * 4 + 2)]);
}

// the rotation orders (FUN_1404be820, FUN_1404e0010, FUN_1404e0150, FUN_1404e00b0, FUN_1404e01f0,
// FUN_1404e0290): the letters the order the axes turn a vertex in (row vectors: the first letter's matrix first)
enum class Order { XYZ, XZY, YXZ, YZX, ZXY, ZYX };

M4 rotation(Order order, const std::array<std::int32_t, 3> &a)
{
    const M4 x = rot_x(a[0]), y = rot_y(a[1]), z = rot_z(a[2]);
    switch (order) {
    case Order::XYZ: return mul(mul(x, y), z);
    case Order::XZY: return mul(mul(x, z), y);
    case Order::YXZ: return mul(mul(y, x), z);
    case Order::YZX: return mul(mul(y, z), x);
    case Order::ZXY: return mul(mul(z, x), y);
    case Order::ZYX: return mul(mul(z, y), x);
    }
    return identity();
}

// a rigid matrix's inverse (FUN_140478ce0 / FUN_14013f3a0): the rotation transposed, the place turned back
M4 rigid_inverse(const M4 &m)
{
    M4 o = identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            o[std::size_t(i * 4 + j)] = m[std::size_t(j * 4 + i)];
    for (int j = 0; j < 3; ++j)
        o[std::size_t(12 + j)] =
            -(m[12] * o[std::size_t(j)] + m[13] * o[std::size_t(4 + j)] + m[14] * o[std::size_t(8 + j)]);
    return o;
}

void scale_rows(M4 &m, const std::array<float, 3> &s)
{
    for (int r = 0; r < 3; ++r)
        for (int j = 0; j < 3; ++j)
            m[std::size_t(r * 4 + j)] *= s[std::size_t(r)];
}

std::int32_t key_frame(const std::vector<std::uint8_t> &k)
{
    std::int32_t v;
    std::memcpy(&v, k.data(), 4);
    return v;
}

template <class T> T rd(const std::uint8_t *p)
{
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

template <class T> void wr(std::uint8_t *p, T v)
{
    std::memcpy(p, &v, sizeof v);
}

struct Particle {
    std::array<float, 3> pos{}, dir{};
    M4 born{}; // the matrix it keeps (the follow modes)
    int life = 0, age = 0, fade_in = 0, fade_out = 0, scale_in = 0, scale_out = 0;
    std::int32_t rot = 0, rot_v = 0, rot_a = 0;
    float w = 0, h = 0, dw = 0, dh = 0, rw = 0, rh = 0, speed = 0, speed2 = 0, fade = 0, scale = 1;
    std::array<int, 4> jitter{};
    int t = 0, frame = 0, prev = 0;
};

// pppRyjMegaBirthModel's particle (FUN_1404bc9e0's 0xb0-byte record): a matrix (its rows the way it flies and two
// across it, its place), a life, colour jitter, a turn with its rate, three sizes with theirs, its speeds, a UV
// scroll, a fade; and, when the record asks, the world at its birth and a colour of its own that moves
struct ModelParticle {
    M4 m{};
    std::int16_t life = 0;
    std::array<std::int8_t, 4> jitter{};
    std::array<std::int32_t, 3> rot{}, rot_v{}, rot_r{};
    std::array<float, 3> size{}, size_v{}, size_r{};
    float speed = 0, speed2 = 0, u = 0, v = 0, du = 0, dv = 0, fade = 0;
    std::uint8_t age = 0, fade_in = 0, fade_out = 0;
    M4 born{};
    std::array<float, 4> colour{}, colour_v{};
};

struct ModelPool {
    std::vector<ModelParticle> particles;
    std::array<float, 3> wind{};
    std::uint16_t timer = 0, vertex_k = 0;
    bool made = false, born_world = false, own_colour = false;
};

struct Obj {
    const EffectLayer *layer = nullptr;
    std::int32_t age = 0;
    bool skip = true, restart = false, gone = false;
    std::vector<std::uint8_t> mem;     // its memory from +0xa0
    std::vector<std::size_t> cursor;   // each command's key
    std::map<std::int32_t, const std::vector<std::uint8_t> *> first; // the loop commands' first keys
    M4 local = identity(), draw = identity();
    std::shared_ptr<Obj> parent;
    std::vector<std::optional<Particle>> pool;
    int pool_count = 0, vertex_k = 0;
    std::array<float, 3> wind{};
    bool pool_made = false;
    // its own vertices (+0x90: pppVtMime's blend of two models, shorts), drawn and spawned at in the model's place
    std::vector<std::array<float, 3>> morph;
    bool morphed = false;
    std::map<const EffectCommand *, ModelPool> model_pools; // pppRyjMegaBirthModel's, one a command
    std::map<const EffectCommand *, std::vector<std::array<float, 4>>> rings; // pppEiDrawShipoly's places
    // pppRyjDrawShipolyBone's: the points' history (two a sample) and its own copy of the model it bends
    std::map<const EffectCommand *, std::pair<std::vector<std::array<float, 4>>, EffectModel>> swings;

    std::uint8_t *at(std::int32_t slot, std::size_t n)
    {
        if (slot < 0)
            fail("a slot before the object's memory");
        if (std::size_t(slot) + n > mem.size())
            mem.resize(std::size_t(slot) + n + 0x40);
        return mem.data() + slot;
    }
    float f(std::int32_t s, int k) { return rd<float>(at(s + 4 * k, 4)); }
    void setf(std::int32_t s, int k, float v) { wr(at(s + 4 * k, 4), v); }
    std::int32_t i(std::int32_t s, int k) { return rd<std::int32_t>(at(s + 4 * k, 4)); }
    void seti(std::int32_t s, int k, std::int32_t v) { wr(at(s + 4 * k, 4), v); }
    std::int16_t h(std::int32_t s, int k) { return rd<std::int16_t>(at(s + 2 * k, 2)); }
    void seth(std::int32_t s, int k, std::int32_t v) { wr(at(s + 2 * k, 2), std::int16_t(std::uint16_t(v))); }
};

struct Key {
    const std::uint8_t *p = nullptr;
    std::size_t n = 0;
    std::int32_t frame() const { return n >= 4 ? rd<std::int32_t>(p) : -1; }
    std::uint8_t b(std::size_t o) const { return o < n ? p[o] : 0; }
    std::int16_t s16(std::size_t o) const { return o + 2 <= n ? rd<std::int16_t>(p + o) : 0; }
    std::uint16_t u16(std::size_t o) const { return o + 2 <= n ? rd<std::uint16_t>(p + o) : 0; }
    std::int32_t s32(std::size_t o) const { return o + 4 <= n ? rd<std::int32_t>(p + o) : 0; }
    float f32(std::size_t o) const { return o + 4 <= n ? rd<float>(p + o) : 0.0F; }
};

// which descriptor words a command has (KH2's table at 0x140779a00): the step (1), the draw (3: run every tick,
// drawn or not, some keep their state there), a birth alone (7); SPAWNER: the steps the fast-forward of a loop
// leaves out (the list at 0x140779990)
enum : unsigned { W1 = 1, W3 = 2, SPAWNER = 4, BIRTH = 8 };
unsigned words(std::uint32_t code)
{
    switch (code) {
    case 0x02: case 0x03: case 0x06: case 0x09: case 0x0a: case 0x0b: case 0x0e: case 0x10: case 0x11:
        return BIRTH;
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29:
    case 0x2a: case 0x2b: case 0x2d: case 0x2f: case 0x30: case 0x33: case 0x34: case 0x35: case 0x3f: case 0x40:
    case 0x41: case 0x42: case 0x43: case 0x44: case 0x48: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x51:
    case 0x52: case 0x54: case 0x55: case 0x57: case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d:
    case 0x5e: case 0x60: case 0x63: case 0x69: case 0x6a: case 0x70: case 0x71: case 0x72: case 0x73: case 0x74:
    case 0x75: case 0x78: case 0x79: case 0x7b: case 0x7c: case 0x7d: case 0xac: case 0xae: case 0xb1: case 0xb2:
    case 0xb5: case 0xb7: case 0xb8: case 0xca: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6: case 0xd7:
    case 0xd8: case 0xd9: case 0xe6: case 0xe7: case 0xe8: case 0xe9: case 0xea: case 0xeb: case 0xec: case 0xed:
    case 0xbc:
        return W1;
    case 0x83: case 0x86: case 0x89: case 0x8a: case 0x91: case 0xee: case 0xef:
        return W3;
    case 0x31: case 0x85: case 0x8b: case 0x8d: case 0x8e: case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0xa0:
    case 0x9d: case 0xbd: case 0xbe: case 0xbf: case 0xc0: case 0xc1: case 0xc8: case 0xda: case 0xdb: case 0xdd:
    case 0xdf: case 0xe0: case 0xe1:
        return W1 | W3;
    case 0xc9: case 0xd0: case 0xf1: return W1 | W3 | SPAWNER;
    default: return 0;
    }
}

Order order_of(std::uint32_t code)
{
    switch (code) {
    case 0x58: case 0xe7: return Order::XZY;
    case 0x59: case 0xe9: return Order::YZX;
    case 0x5a: case 0xe8: return Order::YXZ;
    case 0x5b: case 0xea: return Order::ZXY;
    case 0x5c: case 0xeb: return Order::ZYX;
    default: return Order::XYZ; // 0x57 pppMatrixXYZ, 0xd8 pppMatrixLoop, 0xe6
    }
}

// KH2's atan2 (FUN_1404c1510): the angle of (x, y), false for (0, 0)
bool angle_of(float x, float y, float &out)
{
    constexpr float half = 1.5707963705062866F, pi = 3.1415927410125732F;
    const float p = x * y;
    if (p > 0) {
        out = float(std::atan(double(y / x)) + double(x <= 0 ? -pi : 0.0F));
    } else if (p < 0) {
        out = float(double(y > 0 ? half : -half) - std::atan(double(x / y)));
    } else if (x == 0) {
        if (y == 0)
            return false;
        out = y > 0 ? half : -half;
    } else {
        out = x <= 0 ? pi : 0.0F;
    }
    return true;
}

// radians to KH2's angles (0x10000 a turn): x 32768 / pi, toward zero
std::int32_t to_angle(float radians)
{
    return std::int32_t((radians * 32768.0F) / 3.1415927410125732F);
}

} // namespace

struct EffectInstance::State {
    std::shared_ptr<const EffectBlock> block;
    const EffectEmitter *emitter = nullptr;
    std::mt19937 random;
    std::int32_t age = 0;
    bool stopping = false, finished = false, fastforward = false;
    std::vector<std::shared_ptr<Obj>> objs;
    std::vector<bool> started;
    // the camera's matrices (FUN_1402c3310): world, view, projection, world.view, world.view.projection, the
    // upright view (eda0) . projection, the world's row lengths (0x142b0f010)
    M4 world = identity(), view = identity(), proj = identity(), wv = identity(), wvp = identity(), wood = identity(),
       upright = identity();
    std::array<float, 3> world_scale{1, 1, 1};
    // the instance's bytes pppRyjMngFlag sets (+0xc0 its state for the game, +0xc1 frozen: no ageing, no keyed
    // steps; +0xc2 its place follows the camera's: FUN_1404b1280 adds the eye, 0x142b0ed20)
    std::uint8_t game_state = 0, frozen = 0, follow = 0;
    std::vector<EffectPoint> points; // the game's (set_points)

    float rand01() // FUN_1404dc710: 24 random bits x 2^-24
    {
        return float(random() & 0xffffffu) * (1.0F / 16777216.0F);
    }

    int crand() // FUN_1404dced0: the C library's rand() & 0x7fff (drawn from the instance's stream here)
    {
        return int(random() & 0x7fffu);
    }

    bool live(const Obj &o) const // what the keyed steps check: not frozen, an even age (odd while paused)
    {
        return frozen == 0 && (o.age & 1) == 0;
    }

    const std::vector<std::array<float, 3>> &vertices_of(const Obj &o, std::size_t model) const
    {
        return o.morphed ? o.morph : block->models.at(model).vertices;
    }

    Key key(Obj &o, std::size_t ci)
    {
        const auto &c = o.layer->commands[ci];
        if (c.keys.empty())
            return {};
        const auto &k = c.keys[o.cursor[ci]];
        return {k.data(), k.size()};
    }

    Key advance(Obj &o, std::size_t ci)
    {
        const auto &c = o.layer->commands[ci];
        const std::size_t k = o.cursor[ci];
        if (k + 1 < c.keys.size() && key_frame(c.keys[k + 1]) == o.age)
            o.cursor[ci] = k + 1;
        return key(o, ci);
    }

    static bool hit(const Obj &o, const Key &k) { return k.n >= 4 && k.frame() == o.age; }

    std::shared_ptr<Obj> birth(const EffectLayer &L)
    {
        auto o = std::make_shared<Obj>();
        o->layer = &L;
        o->mem.assign(0x200, 0);
        o->cursor.assign(L.commands.size(), 0);
        for (const auto &c : L.commands)
            birth_command(*o, c);
        std::size_t at = 0;
        while (at < objs.size() && objs[at]->layer->order < L.order)
            ++at;
        objs.insert(objs.begin() + std::ptrdiff_t(at), o);
        return o;
    }

    void reset(Obj &o) // FUN_1404b0120: the cursors to the first keys, each command's word 8
    {
        for (std::size_t ci = 0; ci < o.layer->commands.size(); ++ci) {
            o.cursor[ci] = 0;
            reset_command(o, o.layer->commands[ci]);
        }
    }

    void step_one(Obj &o) // FUN_1404b0550: every step but the spawners', then the age
    {
        for (std::size_t ci = 0; ci < o.layer->commands.size(); ++ci) {
            const auto &c = o.layer->commands[ci];
            const Key k = advance(o, ci);
            if ((words(c.code) & W1) && !(words(c.code) & SPAWNER))
                step(o, c, k);
        }
        grow_age(o);
    }

    static void grow_age(Obj &o)
    {
        o.age = (o.age + kFrame) & ~0xfff;
        if (o.age > 0x6fffffff)
            o.age = 0x6ffff000;
    }

    // ---------------------------------------------------------------------------------- the commands
    void birth_command(Obj &o, const EffectCommand &c) // word 7
    {
        const auto zero = [&](int slot, std::size_t n) { std::memset(o.at(c.slots[std::size_t(slot)], n), 0, n); };
        switch (c.code) {
        case 0x28: case 0x29: case 0x2a: zero(0, 12); break;
        case 0x1c: case 0x1d: case 0x1e: case 0x24: case 0x25: case 0x26: case 0xd3: case 0xd4: zero(1, 12); break;
        case 0x1f: case 0x27: zero(1, 8); break;
        case 0x2b: zero(0, 8); break;
        case 0xd5: case 0xd6: case 0xd7: o.seti(c.slots[0], 3, 0); break; // the first key forgotten
        case 0xae: case 0xb1: case 0xb2: case 0xec: zero(0, 4); break;
        case 0x85: case 0x8b: zero(2, 24); break;
        case 0x8d: case 0x8e: zero(0, 6); break;
        case 0xda: case 0xdb: o.seti(c.slots[2], 0, 0); break;
        case 0x02: case 0x03: case 0x06: case 0x09: case 0x0a: case 0x0b: case 0x0e: case 0x10: case 0x11:
            trail_resource(o, c);
            break;
        case 0x30: o.setf(c.slots[0], 3, 1.0F); break; // pppKeDrct (FUN_1404c1790): w = 1
        case 0x31: zero(0, 16); o.morphed = false; break; // pppVtMime (FUN_1404c1a00): t, dt, ddt, no buffer
        case 0x55: std::memset(o.at(c.slots[0] + 0x80, 12), 0, 12); break; // pppKeMatSN: the angles last seen
        case 0x63: born_world(o, c.slots[3]); break;
        case 0x78: { // pppRyjDrawMatrixWorld (FUN_1404c6a50): the world's matrix at its birth
            for (int j = 0; j < 16; ++j)
                o.setf(c.slots[1], j, world[std::size_t(j)]);
            break;
        }
        case 0x79: case 0x7b: // FUN_1404c6b90 / FUN_1404c6ed0: the world's place and its rows' sizes at its birth
            for (int j = 0; j < 3; ++j) {
                o.setf(c.slots[1], j, world[std::size_t(12 + j)]);
                o.setf(c.slots[1] + 0x10, j, world_scale[std::size_t(j)]);
            }
            break;
        case 0xac: *o.at(c.slots[1] + 1, 1) = 0; break; // pppPointAp: the wait
        case 0xb5: zero(0, 2); break;
        case 0xb7: case 0xb8: zero(0, 3); break; // the vertex counter, the wait
        case 0xd2: zero(1, 12); break;
        case 0xed: zero(0, 0x3c); break;
        case 0x91: case 0xa0: case 0xee: zero(0, 12); break; // the strength, its rate, the rate's
        case 0xbc: zero(0, 8); break;
        case 0xef: game_state = 3; break; // FUN_1404db5c0: the instance's state 3
        case 0xbd: case 0xdd: // FUN_1404d6cb0 / FUN_1404d6ef0 / FUN_1404e2690: the line made anew (state 2)
            std::memset(o.at(c.slots[0], 0xb0), 0, 0xb0);
            for (int j = 0; j < 16; ++j)
                o.setf(c.slots[0] + 0x40, j, identity()[std::size_t(j)]);
            o.setf(c.slots[0] + 0xac, 0, 1.0F);
            game_state = 2;
            break;
        case 0xbe: case 0xbf: case 0xc0: case 0xc1: case 0xdf: case 0xe0: case 0xe1: // not tied yet, no shape
            std::memset(o.at(c.slots[0], 0x10), 0, 0x10);
            if (c.code == 0xbf || c.code == 0xdf)
                *o.at(c.slots[0] + 8, 1) = 4;
            break;
        case 0xc8: // FUN_1404d1c60: the history's cursor, its size (none yet), still filling
            std::memset(o.at(c.slots[1], 12), 0, 12);
            *o.at(c.slots[1] + 5, 1) = 1;
            o.swings.erase(&c);
            break;
        case 0xf1: // FUN_1404ba790: the ring's cursor, size and count, its width, drift and pull, all none
            std::memset(o.at(c.slots[3], 0xe0), 0, 0xe0);
            *o.at(c.slots[3] + 5, 1) = 1;
            o.rings.erase(&c);
            break;
        case 0x99: case 0x9a: case 0x9b: // FUN_1404cc930 ...: 31 places, the newest first; the shape's frame
            zero(0, 8);
            *o.at(c.slots[0], 1) = 0x1f;
            break;
        case 0x9c: case 0x9d: // FUN_1404ce110 / FUN_1404cee40: the colours, the time, a seed from rand()
            zero(0, 0x30);
            o.seti(c.slots[0] + 0x1f0, 0, 0);
            wr(o.at(c.slots[0] + 0x1f4, 2), std::uint16_t(0));
            wr(o.at(c.slots[0] + 0x1f8, 2), std::uint16_t(crand()));
            *o.at(c.slots[0] + 0x1fa, 1) = 0;
            break;
        default: break;
        }
    }

    // pppKeThRes... (FUN_1404bf300 ...): a trail's points: how many sets (1 or 4), how many points, their bytes
    void trail_resource(Obj &o, const EffectCommand &c)
    {
        int sets = 1, count = 0;
        switch (c.code) {
        case 0x02: count = 3; break;
        case 0x03: count = 8; break;
        case 0x06: count = 16; break;
        case 0x09: count = 24; break;
        case 0x0a: count = 24; sets = 4; break;
        case 0x0b: count = 32; break;
        case 0x0e: count = 40; break;
        case 0x10: count = 48; break;
        default: count = 48; sets = 4; break; // 0x11
        }
        std::uint8_t *p = o.at(c.slots[0], 4);
        p[0] = std::uint8_t(sets);
        p[1] = std::uint8_t(count);
        wr(p + 2, std::uint16_t(count * 48 + 96));
    }

    // FUN_1404c5460: the world's turn (its angles, 0x142b0f000, made a matrix) and place (0x142b0eff0) at the
    // birth, a matrix at the slot: the world's rows without their sizes
    void born_world(Obj &o, std::int32_t slot)
    {
        const M4 m = unsized_world();
        for (int j = 0; j < 16; ++j)
            o.setf(slot, j, m[std::size_t(j)]);
    }

    M4 unsized_world() const // the world's turn and place (FUN_1404b1280's angles and 0x142b0eff0)
    {
        M4 m = world;
        for (int r = 0; r < 3; ++r) {
            const float size = world_scale[std::size_t(r)];
            for (int j = 0; j < 3; ++j)
                m[std::size_t(r * 4 + j)] = size != 0 ? world[std::size_t(r * 4 + j)] / size : 0.0F;
            m[std::size_t(r * 4 + 3)] = 0;
        }
        m[15] = 1;
        return m;
    }

    void reset_command(Obj &o, const EffectCommand &c) // word 8
    {
        switch (c.code) {
        case 0x28: case 0x29: case 0x2a: std::memset(o.at(c.slots[0], 12), 0, 12); break;
        case 0x1c: case 0x1d: case 0x1e: case 0x24: case 0x25: case 0x26: case 0xd3: case 0xd4:
            std::memset(o.at(c.slots[1], 12), 0, 12);
            break;
        case 0x1f: case 0x27: std::memset(o.at(c.slots[1], 8), 0, 8); break;
        case 0x2b: std::memset(o.at(c.slots[0], 8), 0, 8); break;
        case 0xd5: case 0xd6: case 0xd7: { // FUN_1404c0aa0: the first key taken off again
            const auto it = o.first.find(c.slots[0]);
            if (it == o.first.end())
                break;
            const Key k{it->second->data(), it->second->size()};
            for (int j = 0; j < 3; ++j) {
                if (c.code == 0xd6)
                    o.seti(c.slots[0], j, o.i(c.slots[0], j) - k.s32(4 + 4 * std::size_t(j)));
                else
                    o.setf(c.slots[0], j, o.f(c.slots[0], j) - k.f32(4 + 4 * std::size_t(j)));
            }
            break;
        }
        case 0x8d: case 0x8e: std::memset(o.at(c.slots[0], 6), 0, 6); break;
        case 0x85: case 0x8b: std::memset(o.at(c.slots[2], 24), 0, 24); break;
        case 0xda: case 0xdb:
            std::memset(o.at(c.slots[2] + 8, 8), 0, 8);
            std::memset(o.at(c.slots[2] + 0x14, 8), 0, 8);
            break;
        case 0x31: std::memset(o.at(c.slots[0], 12), 0, 12); break;
        case 0xb1: case 0xb2: std::memset(o.at(c.slots[0], 4), 0, 4); break;
        case 0xd2: std::memset(o.at(c.slots[1], 12), 0, 12); break;
        case 0xed: std::memset(o.at(c.slots[0], 0x3c), 0, 0x3c); break;
        case 0x9c: case 0x9d: std::memset(o.at(c.slots[0], 0x30), 0, 0x30); break;
        case 0x91: case 0xa0: case 0xee: std::memset(o.at(c.slots[0], 12), 0, 12); break;
        case 0xbc: std::memset(o.at(c.slots[0], 8), 0, 8); break;
        case 0xbd: case 0xdd: o.setf(c.slots[0] + 0x9c, 0, 0); o.setf(c.slots[0] + 0xa8, 0, 0); break;
        case 0xf1: ribbon_reset(o, c); break;
        default: break;
        }
    }

    void step(Obj &o, const EffectCommand &c, const Key &k) // word 1
    {
        const auto s = [&](int n) { return c.slots[std::size_t(n)]; };
        switch (c.code) {
        case 0x28: case 0x2a: // pppPoint, pppScale (FUN_1404c0900, FUN_1404c0d40)
            if (hit(o, k))
                for (int j = 0; j < 3; ++j)
                    o.setf(s(0), j, o.f(s(0), j) + k.f32(4 + 4 * std::size_t(j)));
            break;
        case 0x29: // pppAngle (FUN_1404c0b20)
            if (hit(o, k))
                for (int j = 0; j < 3; ++j)
                    o.seti(s(0), j, o.i(s(0), j) + k.s32(4 + 4 * std::size_t(j)));
            break;
        case 0x1c: case 0x1e: case 0x24: case 0x26: case 0xd4: { // ...Move / ...Accele: [value, rate]
            if (hit(o, k))
                for (int j = 0; j < 3; ++j)
                    o.setf(s(1), j, o.f(s(1), j) + k.f32(4 + 4 * std::size_t(j)));
            if (c.code != 0xd4 || !fastforward)
                for (int j = 0; j < 3; ++j)
                    o.setf(s(0), j, o.f(s(0), j) + o.f(s(1), j));
            break;
        }
        case 0x1d: case 0x25: case 0xd3: { // the angle ones (integers); 0xd3 not when fast-forwarding
            if (hit(o, k))
                for (int j = 0; j < 3; ++j)
                    o.seti(s(1), j, o.i(s(1), j) + k.s32(4 + 4 * std::size_t(j)));
            if (c.code != 0xd3 || !fastforward)
                for (int j = 0; j < 3; ++j)
                    o.seti(s(0), j, o.i(s(0), j) + o.i(s(1), j));
            break;
        }
        case 0x1f: case 0x27: { // colour rates: four shorts
            if (hit(o, k))
                for (int j = 0; j < 4; ++j)
                    o.seth(s(1), j, o.h(s(1), j) + k.s16(4 + 2 * std::size_t(j)));
            for (int j = 0; j < 4; ++j)
                o.seth(s(0), j, o.h(s(0), j) + o.h(s(1), j));
            break;
        }
        case 0x2b: { // pppColor (FUN_1404c0f60): u16 x the instance's colour, <= 32767, x 1/128 -> the bytes at +8
            if (hit(o, k))
                for (int j = 0; j < 4; ++j)
                    o.seth(s(0), j, o.h(s(0), j) + k.s16(4 + 2 * std::size_t(j)));
            for (int j = 0; j < 4; ++j) {
                const float v = std::min(32767.0F, float(std::uint16_t(o.h(s(0), j))));
                *o.at(s(0) + 8 + j, 1) = std::uint8_t(int(v * (1.0F / 128.0F)));
            }
            break;
        }
        case 0xd5: case 0xd6: case 0xd7: { // the loop ones (FUN_1404c09a0, FUN_1404c0ba0, FUN_1404c0de0)
            if (o.i(s(0), 3) == 0) {
                std::memset(o.at(s(0), 12), 0, 12);
                o.seti(s(0), 3, 1);
                if (k.n)
                    o.first[s(0)] = &c.keys[0];
            }
            if (!hit(o, k))
                break;
            if (c.code == 0xd6) {
                std::array<std::int32_t, 3> v{};
                for (int j = 0; j < 3; ++j)
                    v[std::size_t(j)] = o.i(s(0), j) + k.s32(4 + 4 * std::size_t(j));
                while (v[0] > 0x10000)
                    v[0] -= 0x10000;
                for (int j = 1; j < 3; ++j)
                    if (v[std::size_t(j)] > 0x10000)
                        do
                            v[std::size_t(j)] -= 0x10000;
                        while (v[std::size_t(j)] > 0x10000);
                for (int j = 0; j < 3; ++j)
                    o.seti(s(0), j, v[std::size_t(j)]);
            } else {
                for (int j = 0; j < 3; ++j)
                    o.setf(s(0), j, o.f(s(0), j) + k.f32(4 + 4 * std::size_t(j)));
            }
            break;
        }
        case 0x33: case 0x34: case 0x35: case 0x3f: case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
        case 0x48: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x51: case 0x52:
            random_command(o, c, k);
            break;
        case 0x57: case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0xd8: case 0xe6: case 0xe7:
        case 0xe8: case 0xe9: case 0xea: case 0xeb: { // [position, angle, scale] -> the object's matrix
            M4 m = rotation(order_of(c.code), {o.i(s(1), 0), o.i(s(1), 1), o.i(s(1), 2)});
            scale_rows(m, {o.f(s(2), 0), o.f(s(2), 1), o.f(s(2), 2)});
            for (int j = 0; j < 3; ++j)
                m[std::size_t(12 + j)] = o.f(s(0), j);
            o.local = m;
            break;
        }
        case 0x5d: { // pppMatrixLoc (FUN_1404c4fc0): no turn, the position
            o.local = identity();
            for (int j = 0; j < 3; ++j)
                o.local[std::size_t(12 + j)] = o.f(s(0), j);
            break;
        }
        case 0x60: { // pppRyjMatrixNoRot (FUN_1404c50e0): the view turned back (facing the camera), sized, placed
            M4 m = rigid_inverse(view);
            scale_rows(m, {o.f(s(1), 0), o.f(s(1), 1), o.f(s(1), 2)});
            for (int j = 0; j < 3; ++j)
                m[std::size_t(12 + j)] = o.f(s(0), j);
            o.local = m;
            break;
        }
        case 0x63: { // pppRyjMatrixWorldXYZ (FUN_1404c52e0): where the world was at the birth, the world now undone
            M4 m = rotation(Order::XYZ, {o.i(s(1), 0), o.i(s(1), 1), o.i(s(1), 2)});
            for (int j = 0; j < 3; ++j)
                m[std::size_t(12 + j)] = o.f(s(0), j);
            M4 born;
            for (int j = 0; j < 16; ++j)
                born[std::size_t(j)] = o.f(s(3), j);
            m = mul(mul(born, m), rigid_inverse(unsized_world()));
            scale_rows(m, {o.f(s(2), 0), o.f(s(2), 1), o.f(s(2), 2)});
            o.local = m;
            break;
        }
        case 0x6a: // pppKeParMatR (FUN_1404c60f0): turned by the parent's rows (made unit, FUN_140478d80) and
            if (o.parent) { // placed by its whole matrix
                M4 turn = o.parent->local;
                for (int j = 0; j < 4; ++j)
                    turn[std::size_t(12 + j)] = j == 3 ? 1.0F : 0.0F;
                for (int r = 0; r < 3; ++r) {
                    const float n = row_length(turn, r);
                    for (int j = 0; j < 3; ++j)
                        turn[std::size_t(r * 4 + j)] = n != 0 ? turn[std::size_t(r * 4 + j)] / n : 0.0F;
                    turn[std::size_t(r * 4 + 3)] = 0;
                }
                const auto p = transformed({o.local[12], o.local[13], o.local[14], o.local[15]}, o.parent->local);
                o.local = mul(o.local, turn);
                for (int j = 0; j < 4; ++j)
                    o.local[std::size_t(12 + j)] = p[std::size_t(j)];
            }
            break;
        case 0x72: { // pppDrawMatrixNoRot (FUN_1404c6530): rows and position sized by the world, no view turn
            M4 m = o.local;
            scale_rows(m, world_scale);
            for (int j = 0; j < 3; ++j)
                m[std::size_t(12 + j)] =
                    o.local[std::size_t(12 + j)] * world_scale[std::size_t(j)] + wv[std::size_t(12 + j)];
            m[15] = 1;
            o.draw = mul(m, proj);
            break;
        }
        case 0x74: { // pppKeDMat (FUN_1404c66f0): local . W.V kept in its slot, then . P
            const M4 m = mul(o.local, wv);
            for (int j = 0; j < 16; ++j)
                o.setf(s(1), j, m[std::size_t(j)]);
            o.draw = mul(m, proj);
            break;
        }
        case 0x75: { // pppKeDMatFr (FUN_1404c6760): rows x the world's size, the position . W.V, kept, then . P
            M4 m = o.local;
            scale_rows(m, world_scale);
            const auto p = transformed({o.local[12], o.local[13], o.local[14], o.local[15]}, wv);
            for (int j = 0; j < 4; ++j)
                m[std::size_t(12 + j)] = p[std::size_t(j)];
            for (int j = 0; j < 16; ++j)
                o.setf(s(1), j, m[std::size_t(j)]);
            o.draw = mul(m, proj);
            break;
        }
        case 0x78: { // pppRyjDrawMatrixWorld (FUN_1404c69e0): local . (the birth's world . V . P)
            M4 born;
            for (int j = 0; j < 16; ++j)
                born[std::size_t(j)] = o.f(s(1), j);
            o.draw = mul(o.local, mul(mul(born, view), proj));
            break;
        }
        case 0x79: case 0x7b: { // ...WorldFront (FUN_1404c6a70), ...WorldWood (FUN_1404c6d90): 0x71 / 0x73 at
            M4 at = identity(); // the birth's place and size
            for (int j = 0; j < 3; ++j)
                at[std::size_t(12 + j)] = o.f(s(1), j);
            at = c.code == 0x79 ? mul(at, view) : mul(mul(at, view), proj);
            M4 m = o.local;
            scale_rows(m, {o.f(s(1) + 0x10, 0), o.f(s(1) + 0x10, 1), o.f(s(1) + 0x10, 2)});
            if (c.code == 0x7b)
                m = mul(m, wood);
            const auto p = transformed({o.local[12], o.local[13], o.local[14], o.local[15]}, at);
            for (int j = 0; j < 4; ++j)
                m[std::size_t(12 + j)] = p[std::size_t(j)];
            o.draw = c.code == 0x79 ? mul(m, proj) : m;
            break;
        }
        case 0x2d: vertex_attend(o, c, k); break;
        case 0x2f: { // pppKeMvYpEff (FUN_1404c15f0): turned to face its way (z about the way's xy, x its rise)
            if (!live(o))
                break;
            const float vx = o.f(s(1), 0), vy = o.f(s(1), 1), vz = o.f(s(1), 2);
            if (vx == 0 && vy == 0 && vz == 0)
                break;
            float a;
            if (angle_of(-vy, vx, a))
                o.seti(s(0), 2, to_angle(a));
            if (angle_of(std::sqrt(vx * vx + vy * vy), -vz, a))
                o.seti(s(0), 0, to_angle(a));
            break;
        }
        case 0x30: // pppKeDrct (FUN_1404c1750): a direction set on its key
            if (hit(o, k))
                for (int j = 0; j < 3; ++j)
                    o.seti(s(0), j, k.s32(4 + 4 * std::size_t(j)));
            break;
        case 0x31: { // pppVtMime (FUN_1404c1940): the blend t (with its rate and the rate's), keyed
            if (!live(o))
                break;
            const float ddt = o.f(s(0), 2), dt = ddt + o.f(s(0), 1), t = dt + o.f(s(0), 0);
            o.setf(s(0), 1, dt);
            o.setf(s(0), 0, t);
            if (hit(o, k)) {
                o.setf(s(0), 0, t + k.f32(0xc));
                if (double(k.f32(0x10)) >= -1e38)
                    o.setf(s(0), 1, dt + k.f32(0x10));
                o.setf(s(0), 2, ddt + k.f32(0x14));
            }
            break;
        }
        case 0x55: { // pppKeMatSN (FUN_1404c4380): a turn by how much the angles changed since the last tick
            const std::int32_t at = s(0) + 0x80;
            std::array<std::int32_t, 3> d{};
            for (int j = 0; j < 3; ++j) {
                const std::int32_t now = o.i(s(1), j);
                d[std::size_t(j)] = std::int32_t(std::uint32_t(o.i(at, j)) - std::uint32_t(now));
                o.seti(at, j, now);
            }
            M4 m = rotation(Order::XYZ, d);
            m[12] = m[13] = m[14] = 0;
            m[15] = 1;
            for (int j = 0; j < 16; ++j)
                o.setf(s(0) + 0x40, j, m[std::size_t(j)]);
            break;
        }
        case 0xac: point_ap(o, c, k); break;
        case 0xb1: case 0xb2: case 0xec: vertex_spawner(o, c, k); break;
        case 0xb5: case 0xb7: case 0xb8: born_at_random(o, c, k); break;
        case 0xca: // pppRyjMngFlag (FUN_1404be8c0): the instance's bytes on its key
            if (hit(o, k)) {
                follow = k.b(4);
                frozen = k.b(5);
                game_state = k.b(6);
            }
            break;
        case 0xd2: { // pppMoveLoop (FUN_1404c0510): pppMove's [value, rate], the rate not added when fast-forwarding
            if (!live(o))
                break;
            if (hit(o, k))
                for (int j = 0; j < 3; ++j)
                    o.setf(s(1), j, o.f(s(1), j) + k.f32(4 + 4 * std::size_t(j)));
            if (!fastforward)
                for (int j = 0; j < 3; ++j)
                    o.setf(s(0), j, o.f(s(0), j) + o.f(s(1), j));
            break;
        }
        case 0xed: sno_scale_all(o, c, k); break;
        case 0x5e: { // pppMatrixScl (FUN_1404c5030): the scale on the diagonal, the position
            M4 m = identity();
            for (int j = 0; j < 3; ++j) {
                m[std::size_t(j * 5)] = o.f(s(1), j);
                m[std::size_t(12 + j)] = o.f(s(0), j);
            }
            o.local = m;
            break;
        }
        case 0x69: // pppParMatrix (FUN_1404c60c0): the position through the parent's matrix
            if (o.parent) {
                const auto p = transformed({o.local[12], o.local[13], o.local[14], 1}, o.parent->local);
                o.local[12] = p[0];
                o.local[13] = p[1];
                o.local[14] = p[2];
            }
            break;
        case 0x70: case 0xd9: o.draw = mul(o.local, wvp); break; // local . W.V.P
        case 0x71: { // pppDrawMatrixFront (FUN_1404c6490): rows x world scale, the position . W.V, then . P
            M4 m = o.local;
            for (int r = 0; r < 3; ++r)
                for (int j = 0; j < 3; ++j)
                    m[std::size_t(r * 4 + j)] *= world_scale[std::size_t(r)];
            const auto p = transformed({o.local[12], o.local[13], o.local[14], 1}, wv);
            for (int j = 0; j < 4; ++j)
                m[std::size_t(12 + j)] = p[std::size_t(j)];
            o.draw = mul(m, proj);
            break;
        }
        case 0x73: { // pppDrawMatrixWood (FUN_1404c6630): rows x world scale . (upright view . P); position . W.V.P
            M4 m = o.local;
            for (int r = 0; r < 3; ++r)
                for (int j = 0; j < 3; ++j)
                    m[std::size_t(r * 4 + j)] *= world_scale[std::size_t(r)];
            m = mul(m, wood);
            const auto p = transformed({o.local[12], o.local[13], o.local[14], 1}, wvp);
            for (int j = 0; j < 4; ++j)
                m[std::size_t(12 + j)] = p[std::size_t(j)];
            o.draw = m;
            break;
        }
        case 0x54: case 0x7c: case 0x7d: break; // a shared vector; the depth order's corrections (sorting only)
        case 0xae: vertex_ap(o, c, k); break;
        case 0x85: case 0x8b: { // pppDrawMdlTs3's scroll (FUN_1404c8da0; pppDrawMdlTs's too): u (pos, vel, acc), v
            float u = o.f(s(2), 0), du = o.f(s(2), 1), ddu = o.f(s(2), 2), v = o.f(s(2), 3), dv = o.f(s(2), 4),
                  ddv = o.f(s(2), 5);
            du += ddu;
            u += du;
            dv += ddv;
            v += dv;
            if (hit(o, k)) {
                u += k.f32(8);
                du += k.f32(0xc);
                ddu += k.f32(0x10);
                v += k.f32(0x14);
                dv += k.f32(0x18);
                ddv += k.f32(0x1c);
            }
            const float all[6]{u, du, ddu, v, dv, ddv};
            for (int j = 0; j < 6; ++j)
                o.setf(s(2), j, all[j]);
            break;
        }
        case 0xda: case 0xdb: { // pppDrawMdlLoopZ's scroll (FUN_1404c9350; pppDrawMdlLoop's too): handle, u, du,
                                // ddu, v, dv, ddv; wrapped
            if (o.i(s(2), 0) == 0) {
                o.setf(s(2), 1, k.f32(8));
                o.setf(s(2), 4, k.f32(0x14));
                std::memset(o.at(s(2) + 8, 8), 0, 8);
                std::memset(o.at(s(2) + 0x14, 8), 0, 8);
                o.seti(s(2), 0, 1);
            }
            if (hit(o, k)) {
                o.setf(s(2), 2, o.f(s(2), 2) + k.f32(0xc));
                o.setf(s(2), 3, o.f(s(2), 3) + k.f32(0x10));
                o.setf(s(2), 5, o.f(s(2), 5) + k.f32(0x18));
                o.setf(s(2), 6, o.f(s(2), 6) + k.f32(0x1c));
            }
            if (!fastforward) {
                const float du = o.f(s(2), 3) + o.f(s(2), 2), dv = o.f(s(2), 6) + o.f(s(2), 5);
                o.setf(s(2), 2, du);
                o.setf(s(2), 5, dv);
                o.setf(s(2), 1, std::fmod(du + o.f(s(2), 1), 32768.0F));
                o.setf(s(2), 4, std::fmod(dv + o.f(s(2), 4), 32768.0F));
            }
            break;
        }
        case 0x8d: case 0x8e: shape_step(o, k, c.slots[0]); break;
        case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: tail_step(o, c, k); break;
        case 0xc8: case 0xa0: break; // pppRyjDrawShipolyBone's and pppKeShpDtt's steps: none
        case 0xbc: case 0xbd: case 0xbe: case 0xbf: case 0xc0: case 0xc1: case 0xdd: case 0xdf: case 0xe0: case 0xe1:
            flare_step(o, c, k);
            break;
        case 0xc9: mega_step(o, c, k); break;
        case 0xd0: model_birth_step(o, c, k); break;
        case 0xf1: ribbon_step(o, c, k); break;
        default: fail("command " + std::to_string(c.code) + " not read yet");
        }
    }

    // the random ones (pppRand... FUN_1404c1ae0 ...): a value drawn, kept in the slot, then target += base x value
    // (- base when it runs both ways). Floats are drawn at the start and kept (applied again on each key); ints and
    // the four-short colours are drawn anew on each key. "S" ones draw each component apart.
    void random_command(Obj &o, const EffectCommand &c, const Key &k)
    {
        enum Kind { FLOAT, INT, SHORT };
        enum Range { BOTH, UP, DOWN };
        Kind kind = FLOAT;
        Range range = BOTH;
        int n = 3;
        bool apart = false;
        std::size_t flag = 0x14;
        switch (c.code) {
        case 0x33: n = 1; flag = 0xc; break;
        case 0x34: n = 1; flag = 0xc; range = UP; break;
        case 0x35: n = 1; flag = 0xc; range = DOWN; break;
        case 0x3f: break;
        case 0x40: range = UP; break;
        case 0x41: range = DOWN; break;
        case 0x42: kind = INT; break;
        case 0x43: kind = INT; range = UP; break;
        case 0x44: kind = INT; range = DOWN; break;
        case 0x48: kind = SHORT; n = 4; flag = 0x10; break;
        case 0x4a: kind = SHORT; n = 4; flag = 0x10; range = DOWN; break;
        case 0x4b: apart = true; break;
        case 0x4c: apart = true; range = UP; break;
        case 0x4d: apart = true; range = DOWN; break;
        case 0x51: kind = SHORT; n = 4; flag = 0x10; apart = true; break;
        default: kind = SHORT; n = 4; flag = 0x10; apart = true; range = UP; break; // 0x52
        }
        if (!live(o))
            return;
        const bool sum = k.b(flag) != 0;
        const auto draw = [&]() { // DAT_140623b18 2, DAT_140623a4c 0.5
            const float x = rand01();
            switch (range) {
            case BOTH: return sum ? x + rand01() : x * 2.0F;
            case UP: return sum ? (rand01() + x) * 0.5F : x;
            default: return sum ? (-x - rand01()) * 0.5F : -x;
            }
        };
        const std::int32_t s = c.slots[0];
        if (kind == FLOAT && o.age == 0) {
            for (int j = 0; j < (apart ? n : 1); ++j)
                o.setf(s, j, draw());
        } else if (!hit(o, k)) {
            return;
        } else if (kind != FLOAT) {
            for (int j = 0; j < (apart ? n : 1); ++j)
                o.setf(s, j, draw());
        }
        const std::int32_t t = k.s32(4);
        if (t == -1) // a scratch value in the game's data (0x140b607e8)
            return;
        for (int j = 0; j < n; ++j) {
            const float x = o.f(s, apart ? j : 0);
            if (kind == FLOAT) {
                const float b = k.f32(8 + 4 * std::size_t(j));
                o.setf(t, j, o.f(t, j) + (range == BOTH ? b * x - b : b * x));
            } else if (kind == INT) {
                const float b = float(k.s32(8 + 4 * std::size_t(j)));
                o.seti(t, j, o.i(t, j) + std::int32_t(range == BOTH ? b * x - b : b * x));
            } else {
                const float b = float(k.s16(8 + 2 * std::size_t(j)));
                std::int32_t v = std::int32_t(range == BOTH ? b * x - b : b * x);
                if (c.code == 0x52) // its sum cut to a byte (FUN_1404c3d80)
                    v = std::int8_t(std::uint8_t(v));
                o.seth(t, j, o.h(t, j) + v);
            }
        }
    }

    // pppVertexAttend (FUN_1404c1240): a vertex of a list (its number in the slot, kept in the list's range)
    // through the parent's matrix: where a child of pppVertexApAt stays on its parent's model
    void vertex_attend(Obj &o, const EffectCommand &c, const Key &k)
    {
        if (k.s16(0xc) < 0 || !o.parent)
            return;
        const auto &vl = block->vertex_lists.at(std::size_t(k.s16(0xc)));
        if (vl.vertices.empty())
            return;
        std::uint16_t i = rd<std::uint16_t>(o.at(c.slots[0], 2));
        if (vl.vertices.size() <= i) {
            i = std::uint16_t(i % vl.vertices.size());
            wr(o.at(c.slots[0], 2), i);
        }
        const auto &v = block->models.at(vl.model).vertices.at(vl.vertices[i]);
        const auto p = transformed({v[0], v[1], v[2], 1}, o.parent->local);
        for (int j = 0; j < 3; ++j)
            o.setf(c.slots[1], j, p[std::size_t(j)]);
    }

    // a child of the layer given, its parent this object (FUN_1404b02f0); null when there is no such layer
    std::shared_ptr<Obj> child_of(Obj &o, std::int32_t layer)
    {
        if (layer < 0 || std::size_t(layer) >= emitter->layers.size())
            return nullptr;
        auto child = birth(emitter->layers[std::size_t(layer)]);
        child->skip = true;
        child->parent = find(o);
        return child;
    }

    // pppPointAp (FUN_1404d36e0): every wait, children placed at its own point (slot 0)
    void point_ap(Obj &o, const EffectCommand &c, const Key &k)
    {
        if (!live(o))
            return;
        std::uint8_t *wait = o.at(c.slots[1] + 1, 1);
        if (*wait == 0) {
            for (int n = 0; n < k.b(0xd); ++n)
                if (auto child = child_of(o, k.s32(4)))
                    for (int j = 0; j < 3; ++j)
                        child->setf(k.s32(8), j, o.f(c.slots[0], j));
            *wait = k.b(0xc);
        }
        *wait = std::uint8_t(*wait - 1);
    }

    // pppVertexApLc (FUN_1404d3e00; 0xec the same) and pppVertexApAt (FUN_1404d4080): pppVertexAp's walk of a
    // vertex list, the children given the vertex as it is in the model (Lc, FUN_1404d3d50) or its number (At,
    // FUN_1404d4000)
    void vertex_spawner(Obj &o, const EffectCommand &c, const Key &k)
    {
        if (fastforward || !live(o) || k.s16(4) < 0)
            return;
        const std::int32_t s = c.slots[0];
        std::uint16_t wait = rd<std::uint16_t>(o.at(s + 2, 2));
        if (wait == 0) {
            const auto &vl = block->vertex_lists.at(std::size_t(k.s16(4)));
            const std::size_t count = vl.vertices.size();
            const auto give = [&](std::size_t i) {
                auto child = child_of(o, k.s32(0xc));
                if (!child)
                    return;
                if (c.code == 0xb2) {
                    wr(child->at(k.s32(0x10), 2), std::uint16_t(i));
                    return;
                }
                const auto &v = vertices_of(o, vl.model).at(vl.vertices.at(i));
                for (int j = 0; j < 3; ++j)
                    child->setf(k.s32(0x10), j, v[std::size_t(j)]);
            };
            if (k.b(8) == 0) {
                for (int b = 0; b < k.b(6) && count > 0; ++b) {
                    std::uint16_t i = rd<std::uint16_t>(o.at(s, 2));
                    if (count <= i)
                        i = 0;
                    wr(o.at(s, 2), std::uint16_t(i + 1));
                    give(i);
                }
            } else if (k.b(8) == 1) {
                for (int b = 0; b < k.b(6) && count > 0; ++b)
                    give(std::min(count - 1, std::size_t(float(count) * rand01())));
            }
            wait = k.b(7);
        }
        wr(o.at(s + 2, 2), std::uint16_t(wait - 1));
    }

    // pppKeBornRnd3 / 5 / 6 (FUN_1404d4760, FUN_1404d4ea0, FUN_1404d51a0 through FUN_1404e1780): children born at
    // a vertex list's vertices, walked or picked by rand(); 3 on a chance (its mask against rand()'s high byte),
    // 5 and 6 every wait. Each child gets the vertex through the object's matrix and its unit way from the object's
    // centre (3; 5 that way sized), or the vertex as in the model and the angles facing out along it (6).
    void born_at_random(Obj &o, const EffectCommand &c, const Key &k)
    {
        if (fastforward || !live(o))
            return;
        const bool five = c.code == 0xb7;
        const std::size_t layer_at = five ? 0x10 : 0xc, count_at = five ? 0xc : 6, pick_at = five ? 0xe : 8;
        const std::int32_t s = c.slots[0];
        if (c.code == 0xb5) {
            if (k.b(7) == 0xff)
                return;
            if ((k.b(7) & ((crand() >> 8) & 0xff)) != 0 || k.s32(0xc) == -1 || k.s32(0x14) == -1)
                return;
        } else {
            const std::size_t wait_at = five ? 0xd : 7;
            if (k.b(wait_at) == 0)
                return;
            std::uint8_t *wait = o.at(s + 2, 1);
            if (*wait == 0) {
                *wait = k.b(wait_at);
                if (k.s32(layer_at) == -1 || k.s32(layer_at + 8) == -1)
                    return;
                born_walk(o, c, k, count_at, pick_at);
            }
            *wait = std::uint8_t(*wait - 1);
            return;
        }
        born_walk(o, c, k, count_at, pick_at);
    }

    void born_walk(Obj &o, const EffectCommand &c, const Key &k, std::size_t count_at, std::size_t pick_at)
    {
        if (k.s16(4) < 0)
            return;
        const auto &vl = block->vertex_lists.at(std::size_t(k.s16(4)));
        const int count = int(vl.vertices.size());
        if (count == 0)
            return;
        const auto &vs = vertices_of(o, vl.model);
        const bool pick = k.b(pick_at) != 0;
        std::uint16_t *counter = reinterpret_cast<std::uint16_t *>(o.at(c.slots[0], 2));
        int i = pick ? crand() % count : int(*counter);
        for (int n = k.b(count_at); n > 0; --n) {
            const auto &v = vs.at(vl.vertices.at(std::size_t(std::min(i, count - 1))));
            born_one(o, c, k, v);
            if (pick) {
                i = crand() % count;
            } else if (count <= ++i) {
                i = 0;
            }
        }
        wr(reinterpret_cast<std::uint8_t *>(counter), std::uint16_t(i));
    }

    void born_one(Obj &o, const EffectCommand &c, const Key &k, const std::array<float, 3> &v)
    {
        const bool three = c.code == 0xb5, five = c.code == 0xb7;
        auto child = child_of(o, k.s32(five ? 0x10 : 0xc));
        if (!child)
            return;
        if (three || five) { // FUN_1404d4830 / FUN_1404d4f80
            const auto p = transformed({v[0], v[1], v[2], 1}, o.local);
            const std::int32_t at = k.s32(five ? 0x14 : 0x10), way = k.s32(five ? 0x1c : 0x18);
            const float size = five ? k.f32(8) : 1.0F;
            std::array<float, 3> d{};
            for (int j = 0; j < 3; ++j)
                d[std::size_t(j)] = p[std::size_t(j)] - o.local[std::size_t(12 + j)];
            const float n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); // made unit (FUN_140478d80)
            for (int j = 0; j < 3; ++j) {
                child->setf(at, j, p[std::size_t(j)]);
                child->setf(way, j, (n != 0 ? d[std::size_t(j)] / n : d[std::size_t(j)]) * size);
            }
            return;
        }
        // FUN_1404d5280: the vertex, and the angles of its way out (z about the xy way, x its rise; y none)
        for (int j = 0; j < 3; ++j)
            child->setf(k.s32(0x10), j, v[std::size_t(j)]);
        const std::int32_t a = k.s32(0x18);
        float r;
        if (angle_of(-v[1], v[0], r))
            child->seti(a, 2, to_angle(r));
        if (angle_of(std::sqrt(v[0] * v[0] + v[1] * v[1]), -v[2], r))
            child->seti(a, 0, to_angle(r));
        child->seti(a, 1, 0);
    }

    // pppSnoScaleAll (FUN_1404c4080): three sizes and one size for all, each with its rate and the rate's rate;
    // the slot's first three floats their products
    void sno_scale_all(Obj &o, const EffectCommand &c, const Key &k)
    {
        if (!live(o))
            return;
        const std::int32_t s = c.slots[0];
        // the slot: out[3] 0, rate[3] 0xc, rate's rate[3] 0x18, size[3] 0x24, all 0x30, its rate 0x34, its rate's 0x38
        if (hit(o, k)) {
            for (int j = 0; j < 3; ++j) {
                o.setf(s + 0x18, j, o.f(s + 0x18, j) + k.f32(0x1c + 4 * std::size_t(j)));
                o.setf(s + 0xc, j, o.f(s + 0xc, j) + k.f32(0x10 + 4 * std::size_t(j)));
                o.setf(s + 0x24, j, o.f(s + 0x24, j) + k.f32(4 + 4 * std::size_t(j)));
            }
            for (int j = 0; j < 3; ++j)
                o.setf(s + 0x30, j, o.f(s + 0x30, j) + k.f32(0x28 + 4 * std::size_t(j)));
        }
        const float all_rate = o.f(s + 0x30, 2) + o.f(s + 0x30, 1), all = all_rate + o.f(s + 0x30, 0);
        o.setf(s + 0x30, 1, all_rate);
        o.setf(s + 0x30, 0, all);
        for (int j = 0; j < 3; ++j) {
            const float rate = o.f(s + 0x18, j) + o.f(s + 0xc, j), size = rate + o.f(s + 0x24, j);
            o.setf(s + 0xc, j, rate);
            o.setf(s + 0x24, j, size);
            o.setf(s, j, all * size);
        }
    }

    void vertex_ap(Obj &o, const EffectCommand &c, const Key &k) // pppVertexAp (FUN_1404d3b30)
    {
        if (fastforward || !live(o) || k.s16(4) < 0)
            return;
        const std::int32_t s = c.slots[0];
        std::uint16_t wait = rd<std::uint16_t>(o.at(s + 2, 2));
        if (wait == 0) {
            const auto &vl = block->vertex_lists.at(std::size_t(k.s16(4)));
            const auto &vs = vertices_of(o, vl.model);
            const int n = k.b(6);
            if (k.b(8) == 0) {
                std::uint16_t i = rd<std::uint16_t>(o.at(s, 2));
                for (int b = 0; b < n; ++b) {
                    if (vl.vertices.size() <= i)
                        i = 0;
                    spawn(o, k, vs.at(vl.vertices[i]));
                    ++i;
                }
                wr(o.at(s, 2), i);
            } else if (k.b(8) == 1) {
                for (int b = 0; b < n; ++b)
                    spawn(o, k, vs.at(vl.vertices[std::size_t(float(vl.vertices.size()) * rand01())]));
            }
            wait = k.b(7);
        }
        wr(o.at(s + 2, 2), std::uint16_t(wait - 1));
    }

    void spawn(Obj &o, const Key &k, const std::array<float, 3> &v) // FUN_1404d3a20
    {
        auto child = child_of(o, k.s32(0xc));
        if (!child)
            return;
        const auto p = transformed({v[0], v[1], v[2], 1}, o.local);
        for (int j = 0; j < 3; ++j)
            child->setf(k.s32(0x10), j, p[std::size_t(j)]);
    }

    std::shared_ptr<Obj> find(const Obj &o)
    {
        for (auto &p : objs)
            if (p.get() == &o)
                return p;
        return nullptr;
    }

    // ---------------------------------------------------------------------------------- shapes
    // pppDrawShapeX (FUN_1404c9b30; the tails' FUN_1404cc750 ... the same at their +2): the time, the frame, the
    // frame drawn
    void shape_step(Obj &o, const Key &k, std::int32_t s)
    {
        const std::int32_t shape = k.s32(4);
        if (shape == 0xffff || !live(o))
            return;
        const auto &frames = block->shapes.at(std::size_t(shape)).frames;
        std::uint16_t t = rd<std::uint16_t>(o.at(s, 2)), fr = rd<std::uint16_t>(o.at(s + 2, 2));
        const std::uint16_t prev = fr;
        t = std::uint16_t(t + k.u16(8));
        const auto &f = frames.at(std::min<std::size_t>(prev, frames.size() - 1));
        if (f.wait <= t) {
            t = std::uint16_t(t - f.wait);
            ++fr;
            if (frames.size() <= fr) {
                t = 0;
                fr = f.loop ? 0 : prev;
            }
        }
        wr(o.at(s, 2), t);
        wr(o.at(s + 2, 2), fr);
        wr(o.at(s + 4, 2), prev);
    }


    // ---------------------------------------------------------------------------------- the tails
    // pppKeShpTail... (FUN_1404cc750, FUN_1404cce70, FUN_1404cd550, FUN_1404cdd80, FUN_1404ceab0): a ring of the
    // object's last places (or their world places), the newest first: 31 at +0x10 (byte 0 their count, byte 1 the
    // newest; the shape's frame as pppDrawShapeX's at +2) or, the "3" ones, 28 at +0x30 (the newest at +0x1fa) with
    // two colours that move by themselves (head and tail, each with its rate and its rate's rate, shorts / 128)
    std::size_t tail_world_flag(std::uint32_t code) const
    {
        switch (code) {
        case 0x99: return 0x21;
        case 0x9a: return 0x23;
        case 0x9b: return 0x26;
        case 0x9d: return 0x53;
        default: return 0; // 0x9c: always the object's own place
        }
    }

    void tail_step(Obj &o, const EffectCommand &c, const Key &k)
    {
        if (!live(o))
            return;
        const bool three = c.code == 0x9c || c.code == 0x9d;
        const std::int32_t s = c.slots[0];
        const std::size_t flag = tail_world_flag(c.code);
        std::array<float, 4> here{o.local[12], o.local[13], o.local[14], 1.0F};
        if (flag != 0 && k.b(flag) != 0) {
            here = transformed({o.local[12], o.local[13], o.local[14], o.local[15]}, world);
            here[3] = 1.0F;
        }
        const int count = three ? 28 : *o.at(s, 1);
        const std::int32_t ring = s + (three ? 0x30 : 0x10);
        const std::int32_t newest = three ? s + 0x1fa : s + 1;
        if (o.age == 0) // every place the first one
            for (int i = 0; i < count; ++i)
                for (int j = 0; j < 4; ++j)
                    o.setf(ring + 16 * i, j, here[std::size_t(j)]);
        std::uint8_t at = *o.at(newest, 1);
        at = std::uint8_t((at == 0 ? count : at) - 1);
        *o.at(newest, 1) = at;
        for (int j = 0; j < 4; ++j)
            o.setf(ring + 16 * at, j, here[std::size_t(j)]);
        if (!three) {
            shape_step(o, k, s + 2);
            return;
        }
        // the colours: rates += their rates, values += rates; a key adds to all six
        for (int j = 0; j < 4; ++j) {
            o.seth(s, 8 + j, o.h(s, 8 + j) + o.h(s, 0xc + j));
            o.seth(s, 0x10 + j, o.h(s, 0x10 + j) + o.h(s, 0x14 + j));
            o.seth(s, j, o.h(s, j) + o.h(s, 8 + j));
            o.seth(s, 4 + j, o.h(s, 4 + j) + o.h(s, 0x10 + j));
        }
        if (hit(o, k))
            for (int j = 0; j < 4; ++j) {
                const std::size_t o2 = 2 * std::size_t(j);
                o.seth(s, j, o.h(s, j) + k.s16(0x20 + o2));
                o.seth(s, 4 + j, o.h(s, 4 + j) + k.s16(0x28 + o2));
                o.seth(s, 8 + j, o.h(s, 8 + j) + k.s16(0x30 + o2));
                o.seth(s, 0xc + j, o.h(s, 0xc + j) + k.s16(0x38 + o2));
                o.seth(s, 0x10 + j, o.h(s, 0x10 + j) + k.s16(0x40 + o2));
                o.seth(s, 0x14 + j, o.h(s, 0x14 + j) + k.s16(0x48 + o2));
            }
    }

    std::array<float, 4> ring_at(Obj &o, std::int32_t ring, int i)
    {
        return {o.f(ring + 16 * i, 0), o.f(ring + 16 * i, 1), o.f(ring + 16 * i, 2), o.f(ring + 16 * i, 3)};
    }

    static std::array<float, 4> minus(const std::array<float, 4> &a, const std::array<float, 4> &b)
    {
        return {a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]};
    }

    static float length3(const std::array<float, 4> &v)
    {
        return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    }

    // the walk along the ring every tail shares: the sprites spaced by a length that shrinks as they do, each
    // `place`d where its length along the ring falls (FUN_1404cd760's and FUN_1404ceef0's loops, as written: the
    // first spacing the tail's length, then each the one before less its share)
    template <class Place>
    void tail_walk(Obj &o, std::int32_t ring, int count, int newest, unsigned sprites, bool first, float length,
                   float d_length, Place &&place)
    {
        std::array<float, 4> a = ring_at(o, ring, newest);
        int next = newest == count - 1 ? 0 : newest + 1;
        std::array<float, 4> b = ring_at(o, ring, next), seg = minus(b, a), pos = a;
        float along = length3(seg), seg_length = along, into = length;
        if (first)
            place(pos);
        for (;;) {
            if (--sprites == 0)
                return;
            length -= d_length;
            if (length <= 0)
                return;
            while (along < length) {
                const int further = next == count - 1 ? 0 : next + 1;
                if (further == newest)
                    return;
                a = b;
                b = ring_at(o, ring, further);
                seg = minus(b, a);
                into -= seg_length;
                seg_length = length3(seg);
                along += seg_length;
                next = further;
            }
            for (int j = 0; j < 4; ++j)
                pos[std::size_t(j)] = seg[std::size_t(j)] * (into / seg_length) + a[std::size_t(j)];
            into += length;
            along -= length;
            place(pos);
        }
    }

    static std::uint8_t byte_of(float v) // (undefined1)(longlong)v
    {
        return std::uint8_t(std::int64_t(v));
    }

    // pppKeShpTailX / Tail2 / Tail2X's draw (FUN_1404cc960, FUN_1404cd080, FUN_1404cd760): the shape's current
    // frame at each place, sized and coloured from the head to the tail
    void tail_draw(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        if (k.s32(4) == 0xffff || out == nullptr)
            return;
        const bool x = c.code == 0x99, two_x = c.code == 0x9b;
        const std::int32_t s = c.slots[0];
        const auto &shape = block->shapes.at(std::size_t(k.s32(4)));
        std::array<std::uint8_t, 4> slot_colour{};
        std::memcpy(slot_colour.data(), o.at(c.slots[1] + 8, 4), 4);
        std::array<float, 4> head{}, tail{};
        unsigned sprites;
        float size, d_size, length, d_length;
        if (x) {
            for (int j = 0; j < 4; ++j) {
                head[std::size_t(j)] = float(slot_colour[std::size_t(j)]);
                tail[std::size_t(j)] = float(k.s16(0xc + 2 * std::size_t(j)) >> 7);
            }
            sprites = k.u16(0x1c);
            size = 1.0F;
            d_size = (1.0F - k.f32(0x14)) / float(int(sprites) - 1);
            length = k.f32(0x18);
            d_length = d_size * length;
        } else {
            const float alpha = float(slot_colour[3]) * (1.0F / 128.0F);
            for (int j = 0; j < 4; ++j) {
                head[std::size_t(j)] = float(k.b(0x14 + std::size_t(j)));
                tail[std::size_t(j)] = float(k.b(0x18 + std::size_t(j)));
            }
            head[3] *= alpha;
            tail[3] *= alpha;
            sprites = k.u16(0x20);
            size = k.f32(0xc);
            d_size = (size - k.f32(0x10)) / float(int(sprites) - 1);
            length = k.f32(0x1c);
            d_length = (d_size / size) * length;
        }
        std::array<float, 4> step{};
        for (int j = 0; j < 4; ++j) {
            const float n = float(int(sprites) - 1);
            step[std::size_t(j)] = (head[std::size_t(j)] - tail[std::size_t(j)]) * (1.0F / (n == 0 ? 1e-6F : n));
        }
        if (length == 0)
            sprites = 0;
        const std::size_t flag = tail_world_flag(c.code);
        const M4 &to_view = k.b(flag) != 0 ? view : wv;
        const int frame = rd<std::uint16_t>(o.at(s + 6, 2));
        std::array<float, 4> colour = head;
        float sprite_size = size;
        const bool first = x || k.b(0x22) != 0;
        bool placed = false;
        tail_walk(o, s + 0x10, *o.at(s, 1), *o.at(s + 1, 1), sprites, first, length, d_length,
                  [&](const std::array<float, 4> &pos) {
                      if (placed || !first) {
                          for (int j = 0; j < 4; ++j)
                              colour[std::size_t(j)] -= step[std::size_t(j)];
                          sprite_size -= d_size;
                      }
                      placed = true;
                      M4 m = identity();
                      for (int r = 0; r < (two_x ? 3 : 2); ++r)
                          for (int j = 0; j < 4; ++j)
                              m[std::size_t(r * 4 + j)] =
                                  o.local[std::size_t(r * 4 + j)] * (sprite_size * world_scale[std::size_t(r)]);
                      const auto p = transformed(pos, to_view);
                      for (int j = 0; j < 4; ++j)
                          m[std::size_t(12 + j)] = p[std::size_t(j)];
                      draw_shape(shape, frame, mul(m, proj),
                                 {byte_of(colour[0]) / 128.0F, byte_of(colour[1]) / 128.0F,
                                  byte_of(colour[2]) / 128.0F, byte_of(colour[3]) / 128.0F},
                                 *out);
                  });
    }

    // pppKeShpTail3 / 3X's draw (FUN_1404ce1c0, FUN_1404ceef0): at each place a frame picked by the time and the
    // sprite's own number (a 16-bit LCG from the seed, x 0x80d + 7), its size jittered by that number, turned by it
    // when the record says, the colours from the ring's own
    void tail3_draw(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        if (k.s32(4) == 0xffff)
            return;
        const std::int32_t s = c.slots[0];
        if (out != nullptr) {
            const auto &shape = block->shapes.at(std::size_t(k.s32(4)));
            unsigned sprites = k.b(0x1c);
            const float n = float(int(sprites) - 1), alpha = float(*o.at(c.slots[1] + 0xb, 1)) * (1.0F / 128.0F);
            std::array<float, 4> colour{}, tail{}, step{};
            for (int j = 0; j < 4; ++j) {
                colour[std::size_t(j)] = float(o.h(s, j) >> 7);
                tail[std::size_t(j)] = float(o.h(s, 4 + j) >> 7);
            }
            colour[3] *= alpha;
            tail[3] *= alpha;
            for (int j = 0; j < 4; ++j)
                step[std::size_t(j)] = (colour[std::size_t(j)] - tail[std::size_t(j)]) * (1.0F / (n == 0 ? 1e-6F : n));
            float size = k.f32(0xc);
            const float length = k.f32(0x18), d_size = (size - k.f32(0x10)) / n, d_length = (d_size / size) * length;
            if (length == 0)
                sprites = 0;
            const bool x = c.code == 0x9d;
            const M4 &to_view = k.b(x ? 0x53 : 0x50) != 0 ? view : wv;
            std::array<float, 4> r0{}, r1{};
            for (int j = 0; j < 4; ++j) {
                r0[std::size_t(j)] = o.local[std::size_t(j)] * world_scale[0];
                r1[std::size_t(j)] = o.local[std::size_t(4 + j)] * world_scale[1];
            }
            const std::uint32_t time = std::uint32_t(o.i(s + 0x1f0, 0));
            std::uint32_t seed = rd<std::uint16_t>(o.at(s + 0x1f8, 2));
            const std::uint32_t wait = std::max<std::uint32_t>(1, shape.frames.front().wait);
            const std::uint32_t frames = std::uint32_t(shape.frames.size());
            const bool first = k.b(0x1d) != 0;
            bool placed = false;
            tail_walk(o, s + 0x30, 28, *o.at(s + 0x1fa, 1), sprites, first, length, d_length,
                      [&](const std::array<float, 4> &pos) {
                          if (placed || !first) {
                              for (int j = 0; j < 4; ++j)
                                  colour[std::size_t(j)] -= step[std::size_t(j)];
                              size -= d_size;
                          }
                          placed = true;
                          seed = (seed * 0x80d + 7) & 0xffff;
                          const int frame = int(((time + seed) / wait) % frames);
                          const float sz = (1.0F - (float(seed) / 65535.0F) * k.f32(0x14)) * size;
                          M4 m{};
                          for (int j = 0; j < 4; ++j) {
                              m[std::size_t(j)] = r0[std::size_t(j)] * sz;
                              m[std::size_t(4 + j)] = r1[std::size_t(j)] * sz;
                          }
                          const auto p = transformed({pos[0], pos[1], pos[2], 1.0F}, to_view);
                          for (int j = 0; j < 4; ++j)
                              m[std::size_t(12 + j)] = p[std::size_t(j)];
                          if (k.s16(0x1e) != 0) { // turned by the sprite's number (KH2's sine table)
                              const auto [sn, cs] = sin_cos(std::int32_t(seed));
                              for (int j = 0; j < 4; ++j) {
                                  const float a0 = m[std::size_t(j)], a1 = m[std::size_t(4 + j)];
                                  m[std::size_t(j)] = a0 * cs + a1 * sn;
                                  m[std::size_t(4 + j)] = a1 * cs - a0 * sn;
                              }
                          }
                          draw_shape(shape, frame, mul(m, proj),
                                     {byte_of(colour[0]) / 128.0F, byte_of(colour[1]) / 128.0F,
                                      byte_of(colour[2]) / 128.0F, byte_of(colour[3]) / 128.0F},
                                     *out);
                      });
        }
        if (live(o))
            o.seti(s + 0x1f0, 0, o.i(s + 0x1f0, 0) + k.s32(8));
    }

    // ---------------------------------------------------------------------------------- the model particles
    // pppRyjMegaBirthModel (FUN_1404be1f0, FUN_1404bc9e0, FUN_1404bd910, FUN_1404bdd50, FUN_1404be590): a pool of
    // particles each drawing the record's model, born like pppRyjMegaBirth's (the same kinds: a spray, a box, a
    // vertex list's vertices) from their own record layout, turned, sized and scrolled on their own
    static M4 radians_xyz(float x, float y, float z) // FUN_140140560: the real sine, x then y then z
    {
        const float sx = std::sin(x), cx = std::cos(x), sy = std::sin(y), cy = std::cos(y), sz = std::sin(z),
                    cz = std::cos(z);
        M4 rx = identity(), ry = identity(), rz = identity();
        rx[5] = cx, rx[6] = sx, rx[9] = -sx, rx[10] = cx;
        ry[0] = cy, ry[2] = -sy, ry[8] = sy, ry[10] = cy;
        rz[0] = cz, rz[1] = sz, rz[4] = -sz, rz[5] = cz;
        return mul(mul(rx, ry), rz);
    }

    static void unit3(float *v) // FUN_140478d80: made unit, w 0
    {
        const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n != 0)
            for (int j = 0; j < 3; ++j)
                v[j] /= n;
        v[3] = 0;
    }

    static void cross3(float *out, const float *a, const float *b) // FUN_140142120: a x b
    {
        const float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
        out[0] = x, out[1] = y, out[2] = z, out[3] = 0;
    }

    // the rows across the way it flies (row 1): row 2 = 1 x 0, row 0 = 2 x 1, all made unit
    static void frame_rows(M4 &m)
    {
        unit3(&m[4]);
        cross3(&m[8], &m[4], &m[0]);
        unit3(&m[8]);
        cross3(&m[0], &m[8], &m[4]);
        unit3(&m[0]);
    }

    float spread_draw(int kind, float radius) // the distributions of FUN_1404bc9e0 (b 0x100)
    {
        switch (kind) {
        case 1: rand01(); return rand01() * radius;
        case 2: { const float a = rand01(); return a * radius * rand01(); }
        case 3: { const float a = rand01(), b = rand01(); return radius - radius * a * b * 0.7F; }
        case 4: {
            const float a = rand01(), b = rand01(), c = rand01(), d = rand01();
            return a * radius * b * c * d;
        }
        case 5: {
            const float a = rand01(), b = rand01(), c = rand01();
            return radius - radius * a * b * c * 0.5F;
        }
        default: return radius;
        }
    }

    ModelParticle model_particle_birth(Obj &o, ModelPool &pool, const EffectCommand &c, const Key &k)
    {
        ModelParticle p;
        p.m = identity();
        const int kind = k.b(0x26), dist = k.b(0x100);
        const std::array<float, 3> axes{k.f32(0xf4), k.f32(0xf8), k.f32(0xfc)};
        constexpr float pi = 3.1415927410125732F;
        if (kind < 8) {
            const float spread = float(k.b(0x27));
            std::array<float, 3> a{};
            for (auto &x : a)
                x = ((rand01() * (spread + spread) - spread) * pi) / 180.0F;
            if (kind == 2 || kind == 3)
                a[0] = 0;
            const auto d = transformed({k.f32(0xc4), k.f32(0xc8), k.f32(0xcc), 1.0F}, radians_xyz(a[0], a[1], a[2]));
            for (int j = 0; j < 3; ++j)
                p.m[std::size_t(4 + j)] = d[std::size_t(j)] * axes[std::size_t(j)];
            frame_rows(p.m);
        }
        const float radius = k.f32(0xf0);
        if (kind < 4 || kind > 9) {
            if (radius != 0) {
                const float f = spread_draw(dist, radius);
                for (int j = 0; j < 3; ++j)
                    p.m[std::size_t(12 + j)] = p.m[std::size_t(4 + j)] * f;
                p.m[15] = 1;
            }
        } else if (kind < 6) {
            if (radius != 0) {
                const float half = radius * 0.5F;
                if (dist == 1)
                    rand01();
                for (int j = 0; j < 3; ++j) {
                    float f;
                    switch (dist) {
                    case 2: { const float a = rand01(); f = a * radius * rand01(); break; }
                    case 3: { const float a = rand01(), b = rand01(); f = radius - radius * a * b * 0.7F; break; }
                    case 4: {
                        const float a = rand01(), b = rand01(), cc = rand01(), d = rand01();
                        f = a * radius * b * cc * d;
                        break;
                    }
                    case 5: {
                        const float a = rand01(), b = rand01(), cc = rand01();
                        f = radius - radius * a * b * cc * 0.5F;
                        break;
                    }
                    default: f = rand01() * radius; break;
                    }
                    p.m[std::size_t(12 + j)] = (f - half) * axes[std::size_t(j)];
                }
                p.m[15] = 1;
            }
        } else if (k.s16(0x108) >= 0) { // at a vertex list's vertex
            const auto &vl = block->vertex_lists.at(std::size_t(k.s16(0x108)));
            const auto &vs = vertices_of(o, vl.model);
            const int count = int(vl.vertices.size());
            if (count > 0) {
                int i;
                if (dist >= 1 && dist <= 5) {
                    float f = 1.0F;
                    switch (dist) {
                    case 1: rand01(); f = rand01(); break;
                    case 2: { const float a = rand01(), b = rand01(); f = rand01() * b * a; break; }
                    case 3: { const float a = rand01(), b = rand01(), cc = rand01(); f = 1.0F - cc * b * a; break; }
                    case 4: {
                        const float a = rand01(), b = rand01(), cc = rand01(), d = rand01();
                        f = d * b * a * cc;
                        break;
                    }
                    default: {
                        const float a = rand01(), b = rand01(), cc = rand01(), d = rand01(), e = rand01();
                        f = 1.0F - e * b * a * cc * d;
                        break;
                    }
                    }
                    if (count <= pool.vertex_k)
                        pool.vertex_k = 0;
                    i = std::min(count - 1, int(float(count) * f));
                } else {
                    if (count <= pool.vertex_k)
                        pool.vertex_k = 0;
                    i = pool.vertex_k++;
                }
                const auto &v = vs.at(vl.vertices.at(std::size_t(i)));
                for (int j = 0; j < 3; ++j)
                    p.m[std::size_t(12 + j)] = v[std::size_t(j)] * axes[std::size_t(j)];
                p.m[15] = 1;
                if (kind == 8 || kind == 9) { // flying out from the centre through the vertex
                    for (int j = 0; j < 3; ++j)
                        p.m[std::size_t(4 + j)] = p.m[std::size_t(12 + j)];
                    unit3(&p.m[4]);
                    for (int j = 0; j < 3; ++j)
                        p.m[std::size_t(4 + j)] *= axes[std::size_t(j)];
                    frame_rows(p.m);
                }
            }
        }
        for (int j = 0; j < 4; ++j) { // one draw left out, then the jitter
            const int b = k.b(0x48 + std::size_t(j));
            rand01();
            p.jitter[std::size_t(j)] = std::int8_t(int(rand01() * float(b * 2) - float(b >> 1)));
        }
        if (k.b(0x24) != 0) {
            p.fade = float(*o.at(c.slots[1] + 0xb, 1));
            p.fade_in = k.b(0x24);
        }
        if (k.b(0x25) != 0)
            p.fade_out = k.b(0x25);
        for (int j = 0; j < 3; ++j) {
            p.rot[std::size_t(j)] = k.s32(0x4c + 4 * std::size_t(j));
            p.rot_v[std::size_t(j)] = k.s32(0x58 + 4 * std::size_t(j));
        }
        const std::uint8_t rf = k.b(0x103);
        if (rf != 0) {
            p.rot_r[0] = std::int32_t(float(k.s32(0x70)) * rand01());
            if ((rf & 0x20) == 0) {
                p.rot_r[1] = std::int32_t(float(k.s32(0x74)) * rand01());
                p.rot_r[2] = std::int32_t(float(k.s32(0x78)) * rand01());
                if ((rf & 3) == 3) {
                    for (auto &r : p.rot_r)
                        if (rand01() > 0.5F)
                            r = -r;
                } else if (rf & 2) {
                    for (auto &r : p.rot_r)
                        r = -r;
                }
            } else {
                p.rot_r[1] = p.rot_r[2] = p.rot_r[0];
                if ((rf & 3) == 3) {
                    if (rand01() > 0.5F)
                        for (auto &r : p.rot_r)
                            r = -r;
                } else if (rf & 2) {
                    for (auto &r : p.rot_r)
                        r = -r;
                }
            }
        }
        for (int j = 0; j < 3; ++j) {
            if (rf & 4)
                p.rot[std::size_t(j)] += p.rot_r[std::size_t(j)];
            if (rf & 8)
                p.rot_v[std::size_t(j)] += p.rot_r[std::size_t(j)];
            if (0xffffu < std::uint32_t(p.rot[std::size_t(j)]) + 0x8000u)
                p.rot[std::size_t(j)] %= 0x8000; // C's remainder, as FUN_1404bc9e0's mask
        }
        for (int j = 0; j < 3; ++j) {
            p.size[std::size_t(j)] = k.f32(0x7c + 4 * std::size_t(j));
            p.size_v[std::size_t(j)] = k.f32(0x88 + 4 * std::size_t(j));
        }
        const std::uint8_t sf = k.b(0x102);
        if (sf != 0) {
            p.size_r[0] = k.f32(0xa0) * rand01();
            if ((sf & 0x20) == 0) {
                p.size_r[1] = rand01() * k.f32(0xa4);
                p.size_r[2] = rand01() * k.f32(0xa8);
                if ((sf & 3) == 3) {
                    for (auto &r : p.size_r)
                        if (rand01() > 0.5F)
                            r = -r;
                } else if (sf & 2) {
                    for (auto &r : p.size_r)
                        r = -r;
                }
            } else {
                p.size_r[1] = p.size_r[2] = p.size_r[0];
                if ((sf & 3) == 3) {
                    if (rand01() > 0.5F)
                        for (auto &r : p.size_r)
                            r = -r;
                } else if (sf & 2) {
                    for (auto &r : p.size_r)
                        r = -r;
                }
            }
        }
        for (int j = 0; j < 3; ++j) {
            if (sf & 4)
                p.size[std::size_t(j)] += p.size_r[std::size_t(j)];
            if (sf & 8)
                p.size_v[std::size_t(j)] += p.size_r[std::size_t(j)];
        }
        p.speed = k.f32(0xdc);
        p.speed2 = k.f32(0xe8);
        if (const float j = k.f32(0xe4); j != 0) {
            const float r = rand01();
            p.speed += (j + j) * r - j;
        }
        p.u = k.f32(0xac);
        p.v = k.f32(0xb8);
        p.du = k.f32(0xb0);
        p.dv = k.f32(0xbc);
        switch (k.b(0x104)) {
        case 1: p.u *= rand01(); break;
        case 2: p.v *= rand01(); break;
        case 3: { const float r = rand01(); p.u *= r; p.v *= r; break; }
        case 4: { p.u *= rand01(); p.v *= rand01(); break; }
        default: break;
        }
        const std::int16_t life = k.s16(0x22);
        p.life = life == 0 ? -1 : life;
        if (pool.born_world)
            p.born = world;
        if (pool.own_colour)
            for (int j = 0; j < 4; ++j)
                p.colour_v[std::size_t(j)] = k.f32(0x28 + 4 * std::size_t(j));
        return p;
    }

    void model_particle_step(Obj &o, ModelPool &pool, const EffectCommand &c, const Key &k, ModelParticle &p)
    {
        int base = *o.at(c.slots[1] + 0xb, 1);
        if (pool.own_colour) {
            for (int j = 0; j < 4; ++j) {
                p.colour[std::size_t(j)] += p.colour_v[std::size_t(j)];
                p.colour_v[std::size_t(j)] += k.f32(0x38 + 4 * std::size_t(j));
            }
            base = std::min(0xff, int(p.colour[3]) + base);
        }
        for (int j = 0; j < 3; ++j) {
            p.rot[std::size_t(j)] += p.rot_v[std::size_t(j)];
            p.rot_v[std::size_t(j)] += k.s32(0x64 + 4 * std::size_t(j)) +
                                       ((k.b(0x103) & 0x10) ? p.rot_r[std::size_t(j)] : 0);
            while (p.rot[std::size_t(j)] > 0x7fff)
                p.rot[std::size_t(j)] -= 0x10000;
            p.size[std::size_t(j)] += p.size_v[std::size_t(j)];
            p.size_v[std::size_t(j)] += k.f32(0x94 + 4 * std::size_t(j)) +
                                        ((k.b(0x102) & 0x10) ? p.size_r[std::size_t(j)] : 0.0F);
        }
        float sp = p.speed + k.f32(0xe0);
        p.speed = sp;
        if (k.b(0x10b) == 0) {
            const float a = k.f32(0xdc), b = k.f32(0xe0);
            if ((a > 0 && b < 0 && sp < 0) || (a < 0 && b > 0 && sp > 0))
                p.speed = sp = 0;
        }
        p.speed2 += k.f32(0xec);
        for (int j = 0; j < 3; ++j)
            p.m[std::size_t(12 + j)] += p.m[std::size_t(4 + j)] * sp + pool.wind[std::size_t(j)] * p.speed2;
        p.u += p.du;
        p.v += p.dv;
        p.du += k.f32(0xb4);
        p.dv += k.f32(0xc0);
        if (k.s16(0x22) != 0)
            --p.life;
        ++p.age;
        if (p.fade_in != 0 && p.age <= p.fade_in)
            p.fade -= float(base) / float(p.fade_in);
        if (p.fade_out != 0 && std::uint16_t(p.life) <= p.fade_out)
            p.fade += float(base) / float(std::max<int>(1, k.b(0x25)));
    }

    void model_birth_step(Obj &o, const EffectCommand &c, const Key &k)
    {
        ModelPool &pool = o.model_pools[&c];
        if (!pool.made) {
            pool.particles.assign(k.u16(0x1c), ModelParticle{});
            pool.born_world = k.b(0x106) != 0;
            pool.own_colour = k.b(0x101) != 0;
            std::array<float, 4> w{k.f32(0xd0), k.f32(0xd4), k.f32(0xd8), 0};
            unit3(w.data());
            pool.wind = {w[0], w[1], w[2]};
            pool.made = true;
        }
        if (frozen != 0)
            return;
        ++pool.timer;
        int born = 0;
        for (auto &p : pool.particles) {
            if (p.life == 0) {
                if (k.u16(0x20) <= pool.timer && born < k.u16(0x1e)) {
                    p = model_particle_birth(o, pool, c, k);
                    ++born;
                }
            } else {
                model_particle_step(o, pool, c, k, p);
            }
        }
        if (born > 0)
            pool.timer = 0;
    }

    void model_birth_draw(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        const auto it = o.model_pools.find(&c);
        if (out == nullptr || k.s32(4) == 0xffff || it == o.model_pools.end() || !it->second.made)
            return;
        const ModelPool &pool = it->second;
        const int kind = k.b(0x26);
        const bool facing = kind == 1 || kind == 3 || kind == 5 || kind == 7 || kind == 9;
        const M4 place = (facing || !pool.born_world) ? mul(o.local, wv) : identity();
        const auto &model = block->models.at(std::size_t(k.s32(4)));
        std::array<std::uint8_t, 4> slot{};
        std::memcpy(slot.data(), o.at(c.slots[1] + 8, 4), 4);
        const bool writes = (model.flags & 1) == 0;
        unsigned flags = 0x20;
        if (k.b(8) & 2)
            flags = writes ? 0x2c : 0x24;
        else
            flags |= ((k.b(8) & 1) ? 0x80u : 0u) | (writes ? 8u : 0u);
        for (const auto &p : pool.particles) {
            if (p.life == 0)
                continue;
            // FUN_1404be590: its rows (or none), turned, sized, then through the view as its kind says
            M4 m = identity();
            if (k.b(0x105) != 0)
                m = p.m;
            else
                for (int j = 0; j < 4; ++j)
                    m[std::size_t(12 + j)] = p.m[std::size_t(12 + j)];
            if (p.rot[0] != 0 || p.rot[1] != 0 || p.rot[2] != 0) {
                constexpr float to_rad = 3.1415927410125732F * 3.0517578125e-05F;
                m = mul(radians_xyz(float(p.rot[0]) * to_rad, float(p.rot[1]) * to_rad, float(p.rot[2]) * to_rad), m);
            }
            for (int r = 0; r < 3; ++r)
                for (int j = 0; j < 4; ++j)
                    m[std::size_t(r * 4 + j)] *= p.size[std::size_t(r)];
            const std::array<float, 4> pos{m[12], m[13], m[14], m[15]};
            std::array<float, 4> q;
            if (facing) {
                q = transformed(pos, place);
                m = mul(m, upright);
            } else if (!pool.born_world) {
                q = transformed(pos, place);
                m = mul(m, wv);
            } else {
                const M4 born_view = mul(mul(o.local, p.born), view);
                q = transformed(pos, born_view);
                m = mul(m, wv);
            }
            for (int j = 0; j < 4; ++j)
                m[std::size_t(12 + j)] = q[std::size_t(j)];
            const M4 draw_m = mul(m, proj);
            std::array<float, 4> colour{};
            for (int j = 0; j < 3; ++j) {
                int v = p.jitter[std::size_t(j)] + slot[std::size_t(j)];
                if (pool.own_colour)
                    v += int(p.colour[std::size_t(j)]);
                colour[std::size_t(j)] = float(std::clamp(v, 0, 0xff));
            }
            int a = int((float(p.jitter[3]) + float(slot[3])) - p.fade);
            if (pool.own_colour)
                a += int(p.colour[3]);
            colour[3] = float(std::clamp(a, 0, 0x7f));
            draw_model_with(o.morphed ? &o.morph : nullptr, model, draw_m, colour, flags, k.b(0xc), k.b(0x10a), 0,
                            p.u, p.v, *out);
        }
    }

    // ---------------------------------------------------------------------------------- the ribbon
    // pppEiDrawShipoly (FUN_1404b9c00, FUN_1404b9260, FUN_1404b8380): a ribbon along the object's last places. Its
    // fourth slot keeps: +0 the ring's cursor, +4 its size (4 to 128), +6 how many it holds, +0xc the width with
    // its rate and the rate's (+0x10, +0x14), +0x18 a v the reset takes back, +0x24 / +0x28 the UV scroll, +0x30
    // the object's matrix as last taken, +0x70 a drift with its rate and the rate's (+0x80, +0x90), +0xa0 a point
    // the places are pulled to with its rate and the rate's (+0xb0, +0xc0) and the pull (+0xd0, +0xd4, +0xd8).
    // Record: +9 0 two edges a place (+-0.5 x the width across the object's y), 1 or 2 the place alone and edges
    // made across the screen; +0xa the object's places not taken; +0xb the ring's size; +0x10 thinner toward the
    // tail; +0x11 0 the object's own space, 1 the world's (2: a matrix the game gives, the view here); +0x12 how
    // many pieces along, +0x13 across; +0x14 no depth test; +0x15 the blend; +0x16 / +0x17 faded across / along
    // (+0x18 the share along that fades in); +0x1c each place pushed out from their middle by its width; +0x1d
    // the drift; +0x1e the pull (2: to the point as it is, 1: through the object's matrix; +0x8c the pull's
    // reach); +0x90 the u by the length along; +0x94 / +0x98 the scroll's rates; +0x9c..+0xa1 the u and v ranges.
    static std::array<float, 4> add4(const std::array<float, 4> &a, const std::array<float, 4> &b)
    {
        return {a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]};
    }

    std::array<float, 4> slot4(Obj &o, std::int32_t s)
    {
        return {o.f(s, 0), o.f(s, 1), o.f(s, 2), o.f(s, 3)};
    }

    void set4(Obj &o, std::int32_t s, const std::array<float, 4> &v)
    {
        for (int j = 0; j < 4; ++j)
            o.setf(s, j, v[std::size_t(j)]);
    }

    void ribbon_reset(Obj &o, const EffectCommand &c) // FUN_1404ba8c0: the first key's taken off again
    {
        const std::int32_t s = c.slots[3];
        if (c.keys.empty())
            return;
        const Key k{c.keys[0].data(), c.keys[0].size()};
        for (int j = 0; j < 3; ++j) {
            o.setf(s + 0xa0, j, o.f(s + 0xa0, j) - k.f32(0x5c + 4 * std::size_t(j)));
            o.setf(s + 0x70, j, o.f(s + 0x70, j) - k.f32(0x38 + 4 * std::size_t(j)));
        }
        std::memset(o.at(s + 0xb0, 0x20), 0, 0x20);
        std::memset(o.at(s + 0xd4, 8), 0, 8);
        o.setf(s + 0xd0, 0, o.f(s + 0xd0, 0) - k.f32(0x80));
        std::memset(o.at(s + 0x80, 0x20), 0, 0x20);
        o.setf(s + 0xc, 0, o.f(s + 0xc, 0) - k.f32(0x2c));
        std::memset(o.at(s + 0x10, 8), 0, 8);
        o.setf(s + 0x18, 0, o.f(s + 0x18, 0) - k.f32(0x20));
        std::memset(o.at(s + 0x1c, 8), 0, 8);
    }

    // the ring's i-th place back from the newest (0 the newest), its edge `side` (0 or 1; single: 0)
    static int ring_back(int cursor, int size, int i)
    {
        int at = cursor - 1 - i;
        while (at < 0)
            at += size;
        return at;
    }

    void ribbon_step(Obj &o, const EffectCommand &c, const Key &k)
    {
        if (!live(o))
            return;
        const std::int32_t s = c.slots[3];
        const bool single = k.b(9) != 0, keep = k.b(10) == 0;
        if (keep)
            for (int j = 0; j < 16; ++j)
                o.setf(s + 0x30, j, o.local[std::size_t(j)]);
        auto &ring = o.rings[&c];
        if (ring.empty()) {
            const int size = std::clamp<int>(k.b(0xb), 4, 0x80);
            *o.at(s + 4, 1) = std::uint8_t(size);
            ring.assign(std::size_t(size * (single ? 1 : 2)), std::array<float, 4>{});
            switch (k.b(0x9f)) { // random UV starts
            case 1: o.setf(s + 0x24, 0, rand01()); break;
            case 2: o.setf(s + 0x28, 0, rand01()); break;
            case 3:
                o.setf(s + 0x24, 0, rand01());
                o.setf(s + 0x28, 0, rand01());
                break;
            default: break;
            }
        }
        const int size = *o.at(s + 4, 1);
        int cursor = o.i(s, 0);
        std::uint16_t filled = rd<std::uint16_t>(o.at(s + 6, 2));
        M4 m;
        for (int j = 0; j < 16; ++j)
            m[std::size_t(j)] = o.f(s + 0x30, j);
        const auto to_space = [&](std::array<float, 4> v) {
            if (k.b(0x11) == 1 || k.b(0x11) == 3) // the world's (3: its own matrix after, not known: the world)
                v = transformed(v, world);
            return v;
        };
        if (keep) {
            if (!single) {
                const float w = k.f32(0xc);
                ring[std::size_t(2 * cursor)] = to_space(transformed({0, w * 0.5F, 0, 1}, m));
                ring[std::size_t(2 * cursor + 1)] = to_space(transformed({0, w * -0.5F, 0, 1}, m));
            } else {
                auto v = to_space({m[12], m[13], m[14], m[15]});
                if (k.b(0x1c) != 0)
                    v[3] = o.f(s + 0xc, 0);
                ring[std::size_t(cursor)] = v;
            }
            filled = std::uint16_t(std::min<int>(filled + 1, size));
            wr(o.at(s + 6, 2), filled);
        }
        if (k.b(0x1c) != 0 && single && filled > 0) { // pushed out from their middle
            std::array<float, 4> mid{};
            for (int i = 0; i < filled; ++i)
                mid = add4(mid, ring[std::size_t(ring_back(cursor + 1, size, i))]);
            for (auto &x : mid)
                x /= float(filled);
            for (int i = 1; i < int(filled) - 2; ++i) {
                auto &v = ring[std::size_t(ring_back(cursor + 1, size, i))];
                std::array<float, 4> d = minus(v, mid);
                unit3(d.data());
                const float w = v[3];
                v = add4(v, {d[0] * w, d[1] * w, d[2] * w, 0});
                v[3] = w;
            }
        }
        { // the width
            const float rr = o.f(s + 0x14, 0), r = rr + o.f(s + 0x10, 0), w = r + o.f(s + 0xc, 0);
            o.setf(s + 0x10, 0, r);
            o.setf(s + 0xc, 0, w);
            if (hit(o, k)) {
                o.setf(s + 0xc, 0, w + k.f32(0x2c));
                o.setf(s + 0x10, 0, r + k.f32(0x30));
                o.setf(s + 0x14, 0, rr + k.f32(0x34));
            }
        }
        if (k.b(0x1d) != 0 && single) { // the drift
            const auto drift = slot4(o, s + 0x70);
            for (int i = 1; i < filled; ++i) {
                auto &v = ring[std::size_t(ring_back(cursor + 1, size, i))];
                v = add4(v, drift);
            }
            set4(o, s + 0x80, add4(slot4(o, s + 0x80), slot4(o, s + 0x90)));
            set4(o, s + 0x70, add4(slot4(o, s + 0x70), slot4(o, s + 0x80)));
            if (hit(o, k))
                for (int j = 0; j < 3; ++j) {
                    o.setf(s + 0x70, j, o.f(s + 0x70, j) + k.f32(0x38 + 4 * std::size_t(j)));
                    o.setf(s + 0x80, j, o.f(s + 0x80, j) + k.f32(0x44 + 4 * std::size_t(j)));
                    o.setf(s + 0x90, j, o.f(s + 0x90, j) + k.f32(0x50 + 4 * std::size_t(j)));
                }
        }
        if (k.b(0x1e) != 0 && single) { // the pull
            std::array<float, 4> target = slot4(o, s + 0xa0);
            if (k.b(0x1e) != 2)
                target = transformed(target, m);
            for (int i = 1; i < filled; ++i) {
                auto &v = ring[std::size_t(ring_back(cursor + 1, size, i))];
                std::array<float, 4> d = minus(target, v);
                const float n = length3(d);
                unit3(d.data());
                float f = n / k.f32(0x8c);
                f = f < 0 ? 0.0F : std::min(f, 1.0F);
                const float pull = (1.0F - f) * o.f(s + 0xd0, 0);
                v = add4(v, {d[0] * pull, d[1] * pull, d[2] * pull, 0});
            }
            set4(o, s + 0xb0, add4(slot4(o, s + 0xb0), slot4(o, s + 0xc0)));
            set4(o, s + 0xa0, add4(slot4(o, s + 0xa0), slot4(o, s + 0xb0)));
            const float pr = o.f(s + 0xd4, 0) + o.f(s + 0xd8, 0);
            o.setf(s + 0xd4, 0, pr);
            o.setf(s + 0xd0, 0, pr + o.f(s + 0xd0, 0));
            if (hit(o, k)) {
                for (int j = 0; j < 3; ++j) {
                    o.setf(s + 0xa0, j, o.f(s + 0xa0, j) + k.f32(0x5c + 4 * std::size_t(j)));
                    o.setf(s + 0xb0, j, o.f(s + 0xb0, j) + k.f32(0x68 + 4 * std::size_t(j)));
                    o.setf(s + 0xc0, j, o.f(s + 0xc0, j) + k.f32(0x74 + 4 * std::size_t(j)));
                }
                for (int j = 0; j < 3; ++j)
                    o.setf(s + 0xd0, j, o.f(s + 0xd0, j) + k.f32(0x80 + 4 * std::size_t(j)));
            }
        }
        if (keep) {
            ++cursor;
            o.seti(s, 0, size <= cursor ? 0 : cursor);
        }
    }

    // FUN_1404b8f90 / FUN_1404baa90 (and FUN_1404d1480's for one axis): a natural cubic spline through the points
    // at unit spacing, then its value t of the way along (t from 0 to the count - 1)
    static std::vector<std::array<float, 4>> spline_moments(const std::vector<std::array<float, 4>> &y)
    {
        const std::size_t n = y.size();
        std::vector<std::array<float, 4>> m(n), rhs(n);
        std::vector<float> diag(n, 4.0F);
        if (n < 3)
            return m;
        for (std::size_t i = 1; i + 1 < n; ++i)
            for (int j = 0; j < 4; ++j)
                rhs[i][std::size_t(j)] = (y[i + 1][std::size_t(j)] - y[i][std::size_t(j)]) -
                                         (y[i][std::size_t(j)] - y[i - 1][std::size_t(j)]);
        for (std::size_t i = 2; i + 1 < n; ++i) {
            const float w = 1.0F / diag[i - 1];
            diag[i] = 4.0F - w;
            for (int j = 0; j < 4; ++j)
                rhs[i][std::size_t(j)] -= w * rhs[i - 1][std::size_t(j)];
        }
        for (std::size_t i = n - 2; i >= 1; --i) {
            for (int j = 0; j < 4; ++j)
                m[i][std::size_t(j)] = (rhs[i][std::size_t(j)] - m[i + 1][std::size_t(j)]) / diag[i];
            if (i == 1)
                break;
        }
        return m;
    }

    static std::array<float, 4> spline_at(const std::vector<std::array<float, 4>> &y,
                                          const std::vector<std::array<float, 4>> &m, float t)
    {
        std::size_t i = std::size_t(std::max(0, int(t)));
        if (i + 1 >= y.size())
            i = y.size() >= 2 ? y.size() - 2 : 0;
        const float f = t - float(i);
        std::array<float, 4> out{};
        if (y.size() < 2) {
            if (!y.empty())
                out = y[0];
            return out;
        }
        for (int j = 0; j < 4; ++j) {
            const float y0 = y[i][std::size_t(j)], y1 = y[i + 1][std::size_t(j)], m0 = m[i][std::size_t(j)],
                        m1 = m[i + 1][std::size_t(j)];
            out[std::size_t(j)] = (((y1 - y0) - (m0 + m0 + m1)) + ((m1 - m0) * f + m0 * 3.0F) * f) * f + y0;
        }
        return out;
    }

    void ribbon_draw(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        const std::int32_t s = c.slots[3];
        const auto it = o.rings.find(&c);
        const std::uint16_t filled = rd<std::uint16_t>(o.at(s + 6, 2));
        const int pieces = k.b(0x12);
        if (out != nullptr && it != o.rings.end() && filled > 2 && pieces > 0) {
            const auto &ring = it->second;
            const int size = *o.at(s + 4, 1), cursor = o.i(s, 0);
            const M4 &space = k.b(0x11) == 0 ? wv : view; // 2: the game's matrix (none here), then the view
            std::vector<std::array<float, 4>> a(std::size_t(pieces) + 1), b(std::size_t(pieces) + 1);
            const float step_t = float(int(filled) - 1) / float(pieces);
            if (k.b(9) == 0) {
                for (int side = 0; side < 2; ++side) {
                    std::vector<std::array<float, 4>> pts(filled);
                    for (int i = 0; i < filled; ++i) {
                        auto v = ring[std::size_t(2 * ring_back(cursor, size, i) + side)];
                        v[3] = 1.0F;
                        pts[std::size_t(i)] = transformed(v, space);
                    }
                    const auto m = spline_moments(pts);
                    for (int q = 0; q <= pieces; ++q) {
                        auto v = spline_at(pts, m, float(q) * step_t);
                        v[3] = 1.0F;
                        (side == 0 ? a : b)[std::size_t(q)] = v;
                    }
                }
            } else {
                std::vector<std::array<float, 4>> pts(filled);
                for (int i = 0; i < filled; ++i) {
                    auto v = ring[std::size_t(ring_back(cursor, size, i))];
                    v[3] = 1.0F;
                    pts[std::size_t(i)] = v;
                }
                const auto mo = spline_moments(pts);
                std::vector<std::array<float, 4>> mid(std::size_t(pieces) + 1);
                for (int q = 0; q <= pieces; ++q) {
                    auto v = spline_at(pts, mo, float(q) * step_t);
                    v[3] = 1.0F;
                    mid[std::size_t(q)] = transformed(v, space);
                }
                if (k.f32(0x90) > 0) { // each piece's length, for the u along
                    for (int q = 0; q < pieces; ++q)
                        mid[std::size_t(q)][3] = length3(minus(mid[std::size_t(q)], mid[std::size_t(q) + 1]));
                    mid[std::size_t(pieces)][3] = 0;
                }
                const float half = k.f32(0xc) * o.f(s + 0xc, 0) * world_scale[0];
                std::array<float, 4> last{};
                for (int q = 0; q <= pieces; ++q) {
                    std::array<float, 4> d{};
                    if (k.b(9) == 1) { // the way between this place and the next (the last: the one before)
                        d = q != pieces ? minus(mid[std::size_t(q)], mid[std::size_t(q) + 1])
                                        : minus(mid[std::size_t(q) - 1], mid[std::size_t(q)]);
                        d[3] = 0; // made unit with its depth, the edges then across x and y alone
                        unit3(d.data());
                        for (int j = 0; j < 2; ++j)
                            d[std::size_t(j)] *= half;
                    } else { // the sum of the ways in and out
                        std::array<float, 4> e{};
                        if (q != pieces)
                            e = minus(mid[std::size_t(q)], mid[std::size_t(q) + 1]);
                        e[2] = e[3] = 0;
                        unit3(e.data());
                        for (int j = 0; j < 2; ++j)
                            e[std::size_t(j)] *= half;
                        d = add4(last, e);
                        last = e;
                    }
                    const std::array<float, 4> perp{-d[1], d[0], 0, 0};
                    a[std::size_t(q)] = add4(mid[std::size_t(q)], perp);
                    b[std::size_t(q)] = minus(mid[std::size_t(q)], perp);
                    a[std::size_t(q)][3] = b[std::size_t(q)][3] = mid[std::size_t(q)][3];
                }
            }
            if (k.b(0x10) != 0) { // thinner toward the tail
                float f = 1.0F;
                for (int q = 0; q <= pieces; ++q) {
                    for (int j = 0; j < 3; ++j) {
                        const float mid = (a[std::size_t(q)][std::size_t(j)] + b[std::size_t(q)][std::size_t(j)]) *
                                          0.5F;
                        a[std::size_t(q)][std::size_t(j)] = mid * (1.0F - f) + a[std::size_t(q)][std::size_t(j)] * f;
                        b[std::size_t(q)][std::size_t(j)] = mid * (1.0F - f) + b[std::size_t(q)][std::size_t(j)] * f;
                    }
                    f = std::max(0.0F, f - 1.0F / float(pieces));
                }
            }
            ribbon_strips(o, c, k, a, b, filled, size, *out);
        }
        if (it != o.rings.end() && filled > 2 && frozen == 0) { // the UV scroll, kept between 0 and 1
            float v = k.f32(0x98) + o.f(s + 0x28, 0), u = k.f32(0x94) + o.f(s + 0x24, 0);
            while (v > 1.0F)
                v -= 1.0F;
            while (v < 0.0F)
                v += 1.0F;
            while (u > 1.0F)
                u -= 1.0F;
            while (u < 0.0F)
                u += 1.0F;
            o.setf(s + 0x28, 0, v);
            o.setf(s + 0x24, 0, u);
        }
    }

    // FUN_1404b8380: the pieces along, each a strip across from edge a to edge b, the u along and the v across
    // in the ranges the record gives, the colour slot's colour faded along and across
    void ribbon_strips(Obj &o, const EffectCommand &c, const Key &k, const std::vector<std::array<float, 4>> &a,
                       const std::vector<std::array<float, 4>> &b, int filled, int size, std::vector<EffectDraw> &out)
    {
        const std::int32_t s = c.slots[3];
        const int pieces = k.b(0x12), across = k.b(0x13) + 1;
        const auto range = [](std::uint8_t r) { return r == 1 ? 0.5F : r == 2 ? 0.25F : 1.0F; };
        float u_hi = range(k.b(0x9c)), v_hi = range(k.b(0x9d)), u_lo = 0;
        switch (k.b(0x9e)) { // as FUN_1404b8380 has it
        case 1: u_hi += 0.25F; u_lo = 0.5F; break;
        case 2: u_hi += 0.5F; u_lo = 0.5F; break;
        case 3: u_hi += 0.75F; u_lo = 0.75F; break;
        default: break;
        }
        u_lo += 0.01F;
        u_hi -= 0.01F;
        v_hi -= 0.01F;
        float du = 0, u = 0;
        const float share = k.b(0xa1) != 0 ? float(filled) / float(size) : 1.0F;
        switch (k.b(0xa0)) {
        case 0: case 2: du = share * (u_hi - u_lo) / float(pieces); u = u_lo; break;
        case 1: case 3: du = -(share * (u_hi - u_lo) / float(pieces)); u = u_hi; break;
        default: break;
        }
        float fade = 1.0F, fade_in = 0, fade_out = 0;
        if (k.b(0x17) != 0) {
            const float in = float(int(float(pieces) * k.f32(0x18))), rest = float(pieces) - in;
            if (in > 0) {
                fade = 0;
                fade_in = 1.0F / in;
            }
            if (rest > 0)
                fade_out = 1.0F / rest;
        }
        std::array<std::uint8_t, 4> colour{};
        std::memcpy(colour.data(), o.at(c.slots[2] + 8, 4), 4);
        EffectDraw d;
        if (k.s32(4) != 0xffff) { // the model's first packet's page
            const auto &model = block->models.at(std::size_t(k.s32(4)));
            if (!model.primitives.empty())
                d.tex0 = model.primitives.front().tex0;
        }
        static const std::uint8_t blends[5] = {0x44, 0x48, 0x42, 0x41, 0x46};
        d.alpha = blends[std::min<int>(k.b(0x15), 4)];
        d.depth_test = k.b(0x14) == 0;
        d.depth_write = false;
        bool falling = false;
        const float u_scroll = o.f(s + 0x24, 0), v_scroll = o.f(s + 0x28, 0);
        for (int q = 0; q < pieces; ++q) {
            const float alpha = float(colour[3]);
            float a0 = alpha, a1 = alpha;
            if (k.b(0x17) != 0) { // up to 1 over the share that fades in, then down to the tail
                a0 = alpha * fade;
                if (!falling && 1.0F <= fade)
                    falling = true;
                fade = std::clamp(falling ? fade - fade_out : fade + fade_in, 0.0F, 1.0F);
                a1 = alpha * fade;
            }
            float u1, u_next;
            if (k.f32(0x90) > 0) {
                u1 = u + (a[std::size_t(q)][3] / k.f32(0x90)) * (u_hi - u_lo);
                u_next = u_hi < u1 ? (u1 - u_hi) + u_lo : u1;
            } else {
                u1 = u + du;
                u_next = std::min(u1, u_hi);
            }
            const float s0 = u + u_scroll, s1 = u1 + u_scroll;
            float side = 0, side_step = 0;
            if (k.b(0x16) != 0)
                side_step = 2.0F / float(across - 1);
            std::vector<EffectVertex> strip;
            for (int i = 0; i < across; ++i) {
                const float t = across > 1 ? float(i) / float(across - 1) : 0.0F;
                const float v = (v_hi - 0.01F) * t + 0.01F + v_scroll;
                float e0 = a0, e1 = a1;
                if (k.b(0x16) != 0) {
                    e0 = a0 * side;
                    e1 = a1 * side;
                    const float n = side + side_step;
                    if (n < 1.0F) {
                        side = std::max(0.0F, n);
                    } else {
                        side_step = -side_step;
                        side = 1.0F;
                    }
                }
                std::array<float, 4> p0{}, p1{};
                for (int j = 0; j < 4; ++j) {
                    p0[std::size_t(j)] = a[std::size_t(q)][std::size_t(j)] * (1.0F - t) +
                                         b[std::size_t(q)][std::size_t(j)] * t;
                    p1[std::size_t(j)] = a[std::size_t(q) + 1][std::size_t(j)] * (1.0F - t) +
                                         b[std::size_t(q) + 1][std::size_t(j)] * t;
                }
                p0[3] = p1[3] = 1.0F;
                const bool swap = k.b(0xa0) > 1;
                const std::array<float, 4> rgb0{colour[0] / 128.0F, colour[1] / 128.0F, colour[2] / 128.0F,
                                                e0 / 128.0F};
                const std::array<float, 4> rgb1{colour[0] / 128.0F, colour[1] / 128.0F, colour[2] / 128.0F,
                                                e1 / 128.0F};
                strip.push_back(vertex(transformed(p0, proj), swap ? v : s0, swap ? s0 : v, rgb0));
                strip.push_back(vertex(transformed(p1, proj), swap ? v : s1, swap ? s1 : v, rgb1));
            }
            for (std::size_t i = 0; i + 2 < strip.size(); ++i) { // the strip's triangles
                d.triangles.push_back(strip[i]);
                d.triangles.push_back(strip[i + 1]);
                d.triangles.push_back(strip[i + 2]);
            }
            u = u_next;
        }
        if (!d.triangles.empty())
            out.push_back(std::move(d));
    }

    // ---------------------------------------------------------------------------------- the swing's trail
    // pppRyjDrawShipolyBone (FUN_1404d1960, FUN_1404d11e0, FUN_1404d1d00): the trail of a weapon's swing from the
    // game's first two points (its base and tip). Each tick they join a history of the record's +0xe samples; the
    // record's model (quads, two vertices a sample) is bent along it: every axis of base and tip a natural cubic
    // spline through the history, relative to the base now, the quads strung 2k..2k+3, faded to the tail (and
    // its UV laid along it while the history fills: +0x10 / +0x11 the u and v ranges, +0x12 the u's start), thinner
    // to the tail with +0xf; drawn at the base through the view. +0xd keeps the history from moving on.
    void swing_draw(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        if (k.s32(4) == 0xffff || points.size() < 2)
            return;
        const std::int32_t s = c.slots[1];
        auto &[history, model] = o.swings[&c];
        const auto point = [&](int i) {
            return std::array<float, 4>{points[std::size_t(i)].place[0], points[std::size_t(i)].place[1],
                                        points[std::size_t(i)].place[2], 1.0F};
        };
        if (history.empty()) {
            const int n = k.b(0xe);
            *o.at(s + 4, 1) = std::uint8_t(n);
            for (int i = 0; i < n; ++i) {
                history.push_back(point(0));
                history.push_back(point(1));
            }
            model = block->models.at(std::size_t(k.s32(4)));
            std::uint16_t at = 0;
            for (auto &q : model.primitives) {
                if (q.kind != 4 && q.kind != 6) // FUN_1404d11e0 bends quads alone
                    return;
                for (int c2 = 0; c2 < 4; ++c2) {
                    q.colour[std::size_t(c2)][0] = q.colour[std::size_t(c2)][1] = q.colour[std::size_t(c2)][2] = 0xff;
                    q.vertex[std::size_t(c2)] = std::uint16_t(at + c2);
                }
                at = std::uint16_t(at + 2);
            }
        }
        const int n = *o.at(s + 4, 1), quads = int(model.primitives.size());
        if (n <= 0 || int(model.vertices.size()) != 2 * quads + 2)
            return;
        const int cursor = o.i(s, 0) % n;
        history[std::size_t(2 * cursor)] = point(0);
        history[std::size_t(2 * cursor + 1)] = point(1);
        const auto base = point(0);
        for (int axis = 0; axis < 3; ++axis)
            for (int which = 0; which < 2; ++which) {
                std::vector<std::array<float, 4>> y(static_cast<std::size_t>(n));
                for (int j = 0; j < n; ++j) {
                    int at = cursor - j;
                    if (at < 0)
                        at += n;
                    y[std::size_t(j)][0] =
                        history[std::size_t(2 * at + which)][std::size_t(axis)] - base[std::size_t(axis)];
                }
                const auto m = spline_moments(y);
                for (int i = 0; i <= quads; ++i) {
                    const float t = (float(i) / float(quads)) * float(n - 1);
                    model.vertices[std::size_t(2 * i + which)][std::size_t(axis)] =
                        float(std::int16_t(std::int32_t(spline_at(y, m, t)[0])));
                }
            }
        int trimmed = 0;
        const bool filling = *o.at(s + 5, 1) != 0;
        if (filling) // the samples the history has not yet replaced: the trail as long as it is
            for (int i = quads; i > 0; --i) {
                if (model.vertices[std::size_t(2 * i)] != model.vertices[std::size_t(2 * i - 2)])
                    break;
                ++trimmed;
            }
        if (k.b(0xf) != 0) { // the base drawn toward the tip
            const int left = quads - trimmed;
            const float step = left > 0 ? 1.0F / float(left) : 1.0F;
            float f = 1.0F;
            for (int i = 0; i <= quads; ++i) {
                auto &b0 = model.vertices[std::size_t(2 * i)];
                const auto &tip = model.vertices[std::size_t(2 * i + 1)];
                for (int j = 0; j < 3; ++j)
                    b0[std::size_t(j)] = float(std::int16_t(std::int32_t(tip[std::size_t(j)] * (1.0F - f) +
                                                                          b0[std::size_t(j)] * f)));
                f = std::max(0.0F, f - step);
            }
        }
        if (filling) {
            const auto range = [](std::uint8_t r) { return r == 1 ? 0x800 : r == 2 ? 0x400 : 0x1000; };
            int u_end = range(k.b(0x10)), u = 0;
            const int v_end = range(k.b(0x11));
            switch (k.b(0x12)) {
            case 1: u = 0x400; break;
            case 2: u = 0x800; break;
            case 3: u = 0xc00; break;
            default: break;
            }
            u_end += u;
            u += 0x10;
            u_end -= 0x10;
            const int left = quads - trimmed;
            const int du = left >= 1 ? (u_end - u) / left : 0;
            const float d_alpha = left >= 1 ? 128.0F / float(left) : 0.0F;
            float alpha = left >= 1 ? 128.0F : 0.0F;
            for (int i = 0; i < quads; ++i) {
                auto &q = model.primitives[std::size_t(i)];
                std::uint8_t near_a = 0, far_a = 0;
                if (i < left) {
                    near_a = std::uint8_t(int(alpha));
                    far_a = std::uint8_t(int(float(int(alpha) & 0xff) - d_alpha));
                }
                q.colour[0][3] = q.colour[1][3] = near_a;
                q.colour[2][3] = q.colour[3][3] = far_a;
                const int next = std::min(u + du, u_end);
                if (q.kind == 6) { // the far corners' u not held at the end (FUN_1404d1d00)
                    const float u0 = float(u) / 4096.0F, u1 = float(u + du - 1) / 4096.0F;
                    const float v0 = float(v_end - 0x10) / 4096.0F, v1 = float(0x10) / 4096.0F;
                    q.uv[0] = {u0, v0};
                    q.uv[1] = {u0, v1};
                    q.uv[2] = {u1, v0};
                    q.uv[3] = {u1, v1};
                }
                alpha = std::max(0.0F, alpha - d_alpha);
                u = next;
            }
        }
        if (k.b(0xd) == 0 && frozen == 0) {
            int next = o.i(s, 0) + 1;
            if (n <= next) {
                *o.at(s + 5, 1) = 0;
                next = 0;
            }
            o.seti(s, 0, next);
        }
        if (out == nullptr)
            return;
        std::array<std::uint8_t, 4> colour{};
        std::memcpy(colour.data(), o.at(c.slots[0] + 8, 4), 4);
        if (colour[3] == 0)
            return;
        const bool writes = (model.flags & 1) == 0;
        unsigned flags;
        if (k.b(8) & 2)
            flags = writes ? 0xc : 4;
        else
            flags = ((k.b(8) & 1) ? 0x80u : 0u) | (writes ? 8u : 0u);
        M4 at = identity(); // FUN_1404d1960: no turn, at the base, through the view (0x142b0ef20)
        for (int j = 0; j < 3; ++j)
            at[std::size_t(12 + j)] = base[std::size_t(j)];
        const M4 m = mul(mul(at, view), proj);
        draw_model_with(nullptr, model, m, {float(colour[0]), float(colour[1]), float(colour[2]), float(colour[3])},
                        flags, k.b(0xc), k.b(0x13), 0, 0, 0, *out);
    }

    // ---------------------------------------------------------------------------------- the screen's distortions
    // pppKeMdlDtt (FUN_1404ca9e0), pppKeShpDtt (FUN_1404cfa90), pppSnoMdlDttNorm (FUN_1404cbba0): the screen behind
    // drawn again through a model or a sprite, moved by a strength (slot 0, with its rate and the rate's, keyed):
    // the picture KH2's frame buffer (a TEX0 of its own), the model's corners moved along their normals
    // (FUN_1404cad20) or by the strength alone (FUN_1404c9e90, a sprite's FUN_1404e0330). Given here as draws
    // marked `screen`, each corner's u, v where it lies on the screen; the movement is not worked out here.
    void distortion(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        const std::int32_t s = c.slots[0];
        const bool sprite = c.code == 0xa0;
        const std::size_t at = sprite ? 4 : 8;
        if (c.code == 0xee ? frozen == 0 : live(o)) {
            const float ddt = o.f(s, 2), dt = ddt + o.f(s, 1), t = dt + o.f(s, 0);
            o.setf(s, 1, dt);
            o.setf(s, 0, t);
            if (hit(o, k)) {
                o.setf(s, 0, t + k.f32(at));
                o.setf(s, 1, dt + k.f32(at + 4));
                if (c.code != 0x91 || k.b(0x14) < 4)
                    o.setf(s, 2, ddt + k.f32(at + 8));
            }
        }
        if (out == nullptr)
            return;
        std::array<std::uint8_t, 4> colour{};
        std::memcpy(colour.data(), o.at(c.slots[1] + 8, 4), 4);
        const auto screen_uv = [](const std::array<float, 4> &clip) {
            const float w = clip[3] != 0 ? clip[3] : 1e-6F;
            return std::pair<float, float>{clip[0] / w * 0.5F + 0.5F, 0.5F - clip[1] / w * 0.5F};
        };
        EffectDraw d;
        d.screen = true;
        d.alpha = 0x44;
        if (sprite) { // a quad across the view at the place pppKeDMat kept (or slot 3's x and y)
            std::array<float, 4> centre{};
            if (k.b(0x19) & 2) {
                centre = {o.f(c.slots[3], 0), o.f(c.slots[3], 1), 0, 1};
            } else {
                for (int j = 0; j < 4; ++j)
                    centre[std::size_t(j)] = o.f(c.slots[2] + 0x30, j);
            }
            const float w = world_scale[0] * k.f32(0x10), h = world_scale[1] * k.f32(0x14);
            const std::array<float, 4> tint{colour[0] / 128.0F, colour[1] / 128.0F, colour[2] / 128.0F,
                                            colour[3] / 128.0F};
            std::array<EffectVertex, 4> q;
            const float dx[4] = {-0.5F, 0.5F, -0.5F, 0.5F}, dy[4] = {-0.5F, -0.5F, 0.5F, 0.5F};
            for (int i = 0; i < 4; ++i) {
                const auto clip = transformed({centre[0] + dx[i] * w, centre[1] + dy[i] * h, centre[2], 1}, proj);
                const auto [u, v] = screen_uv(clip);
                q[std::size_t(i)] = vertex(clip, u, v, tint);
            }
            for (const int i : {0, 1, 2, 1, 3, 2})
                d.triangles.push_back(q[std::size_t(i)]);
            out->push_back(std::move(d));
            return;
        }
        if (k.s32(4) == 0xffff || colour[3] == 0)
            return;
        const auto &model = block->models.at(std::size_t(k.s32(4)));
        const auto &vertices = o.morphed ? o.morph : model.vertices;
        const std::array<float, 4> tint{colour[0] / 128.0F, colour[1] / 128.0F, colour[2] / 128.0F,
                                        colour[3] / 128.0F};
        for (const auto &pr : model.primitives) {
            std::array<EffectVertex, 4> q;
            for (int i = 0; i < pr.corners; ++i) {
                const auto &v = vertices.at(pr.vertex[std::size_t(i)]);
                const auto clip = transformed({v[0], v[1], v[2], 1}, o.draw);
                const auto [u, vv] = screen_uv(clip);
                q[std::size_t(i)] = vertex(clip, u, vv, tint);
            }
            if (pr.corners == 4) {
                for (const int i : {0, 1, 2, 1, 3, 2})
                    d.triangles.push_back(q[std::size_t(i)]);
            } else {
                for (const int i : {0, 1, 2})
                    d.triangles.push_back(q[std::size_t(i)]);
            }
        }
        if (!d.triangles.empty())
            out->push_back(std::move(d));
    }

    // ---------------------------------------------------------------------------------- the lens flare
    // pppKeLnsLp (FUN_1404d6c30, FUN_1404d6d10, FUN_1404e26c0): a flare's line, kept in its slot ("the line"): +0
    // a matrix at the source's place in the view, +0x40 its way across the screen (unit, toward the screen's
    // middle) and the rows across it, +0x70 that way times the depth / 512, +0x78 the depth / 512, +0x7c how
    // bright (1 at the middle, 0 at +0xa8 from it; facing ones by their angle too), +0x80 the source's place,
    // +0x8e a near distance, +0x8f 1: the source the object itself, facing (+0x90 the object's z axis' point),
    // +0x9c the facing's reach, +0xa4 the source's distance from the middle on the screen. The elements -- around
    // the source (pppKeLnsArnd, FUN_1404e18a0), columns along the line (pppKeLnsClm, FUN_1404e1b30), a streak to
    // the middle (pppKeLnsCrn, FUN_1404e2010), a flash (pppKeLnsFls, FUN_1404e2370) -- each a shape's frames
    // tied to a line (slot 1 of theirs) on their key; pppKeLnsLpSft moves the reach and the brightness' distance.
    // the T ones (pppKeLnsLpT, ClmT, CrnT, FlsT: FUN_1404d6e10, FUN_1404d61d0, FUN_1404d65a0, FUN_1404d6970) keep
    // their record's values on its key and call the plain ones; pppKeLnsLpT's line also numbers itself and tests,
    // drawing a square at the source and reading it back (FUN_1404d7120), how much of it is seen: not made here,
    // the source always seen
    static std::uint32_t plain_flare(std::uint32_t code)
    {
        switch (code) {
        case 0xdd: return 0xbd;
        case 0xdf: return 0xbf;
        case 0xe0: return 0xc0;
        case 0xe1: return 0xc1;
        default: return code;
        }
    }

    void flare_step(Obj &o, const EffectCommand &c, const Key &k)
    {
        if (!live(o))
            return;
        const std::int32_t s = c.slots[0];
        const std::uint32_t code = plain_flare(c.code);
        if (code == 0xbc) { // FUN_1404d5c20
            if (hit(o, k)) {
                o.setf(s, 0, o.f(s, 0) + k.f32(4));
                o.setf(s, 1, o.f(s, 1) + k.f32(8));
                if (o.age == 0)
                    return;
            }
            o.setf(c.slots[1] + 0x9c, 0, o.f(c.slots[1] + 0x9c, 0) + o.f(s, 0));
            o.setf(c.slots[1] + 0xa8, 0, o.f(s, 1) + o.f(c.slots[1] + 0xa8, 0));
            return;
        }
        if (!hit(o, k))
            return;
        if (code == 0xbd) { // FUN_1404d6c30
            o.setf(s + 0xa8, 0, k.f32(8) + o.f(s + 0xa8, 0));
            *o.at(s + 0x8e, 1) = k.b(0xc);
            o.setf(s + 0x9c, 0, k.f32(4) + o.f(s + 0x9c, 0));
            *o.at(s + 0x8f, 1) = k.b(0xd);
            return;
        }
        // the elements (FUN_1404d5fc0, FUN_1404d63a0, ...): tied to the line, their shape and its first wait
        o.seti(s, 0, 1); // tied
        if (k.s32(4) == 0xffff) {
            o.seti(s, 1, -1);
        } else if (o.i(s, 1) != k.s32(4) + 1 || code != 0xbf) {
            const bool fresh = o.i(s, 1) != k.s32(4) + 1;
            o.seti(s, 1, k.s32(4) + 1);
            const auto &frames = block->shapes.at(std::size_t(k.s32(4))).frames;
            if (code == 0xbf) { // FUN_1404e1f40: each column its time, frame and size (a 16-bit LCG)
                std::uint16_t seed = k.b(0x16);
                const int columns = *o.at(s + 8, 1);
                for (int i = 0; i < columns; ++i) {
                    wr(o.at(s + 0x10 + 8 * i, 2), frames.front().wait);
                    *o.at(s + 0x12 + 8 * i, 1) = 0;
                    seed = std::uint16_t(seed * 0x80d + 7);
                    if ((*o.at(s + 9, 1) & 1) == 0)
                        o.setf(s + 0x14 + 8 * i, 0, float(seed) / 65535.0F);
                }
            } else if (fresh) {
                wr(o.at(s + 8, 2), frames.front().wait);
                *o.at(s + 10, 1) = 0;
            }
        }
        if (code == 0xbf) {
            wr(o.at(s + 10, 2), k.u16(0x12));
            wr(o.at(s + 0xc, 2), std::uint16_t(k.s16(0x10)));
            *o.at(s + 0xe, 1) = k.b(0x17);
        }
    }

    void flare_line(Obj &o, const EffectCommand &c) // FUN_1404d6d10 + FUN_1404e26c0
    {
        const std::int32_t f = c.slots[0];
        const bool facing = *o.at(f + 0x8f, 1) != 0;
        if (facing) {
            const auto z = transformed({0, 0, 1, 1}, o.local);
            for (int j = 0; j < 3; ++j)
                o.setf(f + 0x90, j, z[std::size_t(j)]);
        }
        for (int j = 0; j < 3; ++j)
            o.setf(f + 0x80, j, o.local[std::size_t(12 + j)]);
        const std::array<float, 4> pos{o.local[12], o.local[13], o.local[14], o.local[15]};
        std::array<float, 4> a{}, b{}, e{};
        if (!facing) {
            M4 t = identity();
            for (int j = 0; j < 3; ++j)
                t[std::size_t(12 + j)] = wv[std::size_t(12 + j)] + o.f(f + 0x80, j);
            a = transformed({0, 0, 0, 1}, mul(t, proj));
            for (auto &x : a)
                x /= a[3] != 0 ? a[3] : 1e-6F;
            M4 u = identity();
            for (int j = 0; j < 3; ++j)
                u[std::size_t(12 + j)] = wv[std::size_t(12 + j)];
            b = transformed({0, 0, 0, 1}, mul(u, proj));
            for (auto &x : b)
                x /= b[3] != 0 ? b[3] : 1e-6F;
            e = minus(b, a);
            e[2] = 512.0F;
        } else {
            M4 t = wv;
            t[12] = t[13] = t[14] = 0;
            a = transformed(pos, t);
            b = {-wv[12], -wv[13], -wv[14], -wv[15]};
            e = minus(b, a);
        }
        M4 m = identity();
        const auto p = transformed(pos, wv);
        for (int j = 0; j < 4; ++j)
            m[std::size_t(12 + j)] = p[std::size_t(j)];
        for (int j = 0; j < 16; ++j)
            o.setf(f, j, m[std::size_t(j)]);
        if (e[0] == 0 && e[1] == 0) {
            for (int j = 0; j < 4; ++j)
                o.setf(f + 0x70, j, p[std::size_t(j)]);
        } else {
            const float n = std::sqrt(e[0] * e[0] + e[1] * e[1]);
            const float r0[4] = {e[0] / n, e[1] / n, 0, 0}, r2[4] = {0, 0, 1, 0};
            float r1[4];
            cross3(r1, r0, r2);
            for (int j = 0; j < 4; ++j) {
                o.setf(f + 0x40, j, r0[j]);
                o.setf(f + 0x50, j, r1[j]);
                o.setf(f + 0x60, j, r2[j]);
                o.setf(f + 0x70, j, p[std::size_t(j)]);
            }
        }
        const float depth = (facing ? p[2] : wv[14] + o.f(f + 0x88, 0)) * 0.001953125F;
        o.setf(f + 0x78, 0, depth);
        std::array<float, 4> d = e;
        unit3(d.data());
        o.setf(f + 0x70, 0, d[0] * depth);
        o.setf(f + 0x74, 0, d[1] * depth);
        const std::array<float, 4> b2 = minus(b, a);
        const float dist = length3(b2), screen = std::sqrt(b2[0] * b2[0] + b2[1] * b2[1]);
        o.setf(f + 0xa4, 0, screen);
        std::array<float, 4> way = b2;
        unit3(way.data());
        float bright = 0;
        if (!facing) {
            if (o.f(f + 0xa8, 0) > 0) {
                const float r = screen / o.f(f + 0xa8, 0);
                bright = r <= 1.0F ? 1.0F - r : 0.0F;
            }
        } else {
            const float x = (1.0F - (1.0F - way[2]) * 0.5F) / 0.3F; // as FUN_1404e26c0 writes it
            bright = x <= 1.0F ? 1.0F - x : 0.0F;
            if (o.f(f + 0x9c, 0) >= 0) {
                const float dz = o.f(f + 0x90, 0) * way[0] + o.f(f + 0x90, 1) * way[1] + o.f(f + 0x90, 2) * way[2];
                const float y = ((1.0F - dz) * 0.5F) / o.f(f + 0x9c, 0);
                bright *= y <= 1.0F ? 1.0F - y : 0.0F;
            }
            const float reach = o.f(f + 0xa8, 0) - float(*o.at(f + 0x8e, 1));
            if (reach > 0) {
                const float r = dist / reach;
                bright *= r < 1.0F ? 1.0F - r : 0.0F;
            } else {
                bright = 0;
            }
        }
        o.setf(f + 0x7c, 0, bright);
    }

    // a shape's frame clock (each element's and each column's: +0 the time, +2 the frame), one frame a tick
    void frame_clock(Obj &o, std::int32_t at, const EffectShape &shape)
    {
        std::int16_t t = std::int16_t(rd<std::uint16_t>(o.at(at, 2)) + 0x200);
        std::uint8_t fr = *o.at(at + 2, 1);
        const auto &f = shape.frames.at(std::min<std::size_t>(fr, shape.frames.size() - 1));
        if (std::int16_t(f.wait) <= t) {
            t = std::int16_t(t - std::int16_t(f.wait));
            ++fr;
            if (int(shape.frames.size()) <= fr) {
                t = 0;
                fr = f.loop ? 0 : std::uint8_t(fr - 1);
            }
        }
        wr(o.at(at, 2), std::uint16_t(t));
        *o.at(at + 2, 1) = fr;
    }

    void flare_element(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        const std::uint32_t code = plain_flare(c.code);
        const std::int32_t e = c.slots[0], f = c.slots[1];
        if (o.i(e, 0) == 0 || o.i(e, 1) <= 0)
            return;
        const float bright = o.f(f + 0x7c, 0);
        if (bright == 0 && code != 0xbf)
            return;
        const auto &shape = block->shapes.at(std::size_t(o.i(e, 1) - 1));
        const bool facing = *o.at(f + 0x8f, 1) != 0;
        const float size = k.f32(0xc);
        const std::array<float, 4> colour{float(k.b(8)), float(k.b(9)), float(k.b(10)), float(k.b(0xb))};
        const auto at_source = [&]() {
            M4 m = identity();
            if (!facing) {
                for (int j = 0; j < 3; ++j)
                    m[std::size_t(12 + j)] = wv[std::size_t(12 + j)] + o.f(f + 0x80, j);
            } else {
                for (int j = 0; j < 16; ++j)
                    m[std::size_t(j)] = o.f(f, j);
            }
            return m;
        };
        const auto draw_it = [&](int frame, const M4 &m, float alpha) {
            if (out != nullptr)
                draw_shape(shape, frame, mul(m, proj),
                           {colour[0] / 128.0F, colour[1] / 128.0F, colour[2] / 128.0F,
                            float(std::uint8_t(int(alpha))) / 128.0F},
                           *out);
        };
        if (code == 0xbe) { // around the source
            M4 m = at_source();
            m[0] *= size;
            m[5] *= size;
            scale_rows(m, world_scale);
            frame_clock(o, e + 8, shape);
            draw_it(*o.at(e + 10, 1), m, colour[3] * bright);
        } else if (code == 0xc0) { // a streak toward the middle
            M4 m = identity();
            const float n = (size * o.f(f + 0xa4, 0)) / float(std::max<int>(1, k.u16(0x10)));
            for (int j = 0; j < 4; ++j) {
                m[std::size_t(j)] = o.f(f + 0x40, j) * -n;
                m[std::size_t(4 + j)] = o.f(f + 0x50, j) * n;
                m[std::size_t(8 + j)] = o.f(f + 0x60, j);
                m[std::size_t(12 + j)] = o.f(f + 0x30, j);
            }
            if (!facing)
                for (int j = 0; j < 3; ++j)
                    m[std::size_t(12 + j)] = wv[std::size_t(12 + j)] + o.f(f + 0x80, j);
            scale_rows(m, world_scale);
            const float r = std::min(1.0F, o.f(f + 0xa4, 0) / float(std::max<int>(1, k.u16(0x12))));
            frame_clock(o, e + 8, shape);
            draw_it(*o.at(e + 10, 1), m, r * colour[3] * bright);
        } else if (code == 0xc1) { // a flash, sized by how near the middle the source is
            const float r = o.f(f + 0xa4, 0) / float(std::max<int>(1, k.u16(0x10))), left = r <= 1.0F ? 1.0F - r : 0.0F;
            M4 m = at_source();
            const auto scaled = [&](std::uint8_t mode) {
                return mode == 1 ? size * left : mode == 2 ? (r + 1.0F) * size : size;
            };
            m[0] = k.b(0x12) == 1 || k.b(0x12) == 2 ? scaled(k.b(0x12)) : size;
            m[5] = scaled(k.b(0x13));
            scale_rows(m, world_scale);
            frame_clock(o, e + 8, shape);
            draw_it(*o.at(e + 10, 1), m, colour[3] * bright * left);
        } else { // 0xbf: columns along the line, from the middle by the record's offset, each its own size
            if (bright == 0)
                return;
            M4 m = identity();
            if (facing)
                for (int j = 0; j < 16; ++j)
                    m[std::size_t(j)] = o.f(f, j);
            else
                m[14] = wv[14] + o.f(f + 0x88, 0);
            const float sx = o.f(f + 0x70, 0), sy = o.f(f + 0x74, 0);
            const float offset = float(std::int16_t(rd<std::uint16_t>(o.at(e + 0xc, 2))));
            float x = (facing ? 0.0F : wv[12]) + offset * sx, y = (facing ? 0.0F : wv[13]) + offset * sy;
            const float spacing = float(std::int16_t(rd<std::uint16_t>(o.at(e + 10, 2))));
            float fade = 0;
            const float near = o.f(f + 0xa4, 0) - float(*o.at(f + 0x8e, 1));
            if (near > 0)
                fade = std::min(1.0F, near / (float(k.u16(0x14)) * o.f(f + 0x78, 0)));
            if (*o.at(e + 0xe, 1) != 0)
                fade = 1.0F - fade;
            const int columns = *o.at(e + 8, 1);
            for (int i = 0; i < columns; ++i) {
                frame_clock(o, e + 0x10 + 8 * i, shape);
                const float scale = o.f(e + 0x14 + 8 * i, 0);
                x += scale * spacing * sx;
                y += scale * spacing * sy;
                const float a = float(int((double(scale) * 0.75 + 0.25) * double(fade * colour[3] * bright)));
                M4 cm = m;
                cm[0] = scale * size * m[0] * world_scale[0];
                cm[5] = scale * size * m[5] * world_scale[1];
                cm[12] = x;
                cm[13] = y;
                draw_it(*o.at(e + 0x12 + 8 * i, 1), cm, a);
            }
        }
    }

    // pppRyjMegaBirthUserCtrl (FUN_1404db590): the record's shape at each of the game's points, facing the screen.
    // A point's bytes: its alpha (0: not drawn; the record's +0xb when 0xff is not there), its frame (moved on
    // each tick, back to 0 at the end when the frame loops), a turn (x 256, KH2's angle) and a size (x 0.1); a
    // point whose fourth word is 1.0 drawn plain (frame 0, no turn, size 1). The colour the record's +8..+0xa.
    void user_sprites(const Key &k, std::vector<EffectDraw> *out)
    {
        if (points.empty() || k.s32(4) == 0xffff)
            return;
        const auto &shape = block->shapes.at(std::size_t(k.s32(4)));
        for (auto &pt : points) {
            float w;
            std::memcpy(&w, pt.bytes.data(), 4);
            const bool plain = w == 1.0F;
            int frame = 0;
            float size = 1.0F;
            M4 m = identity();
            std::uint8_t alpha = k.b(0xb);
            if (!plain) {
                if (pt.bytes[0] == 0)
                    continue;
                alpha = pt.bytes[0];
                frame = pt.bytes[1];
                size = float(pt.bytes[3]) * 0.1F;
                if (pt.bytes[2] != 0) {
                    const auto [sn, cs] = sin_cos(std::int32_t(pt.bytes[2]) << 8);
                    m[0] = cs, m[1] = sn, m[4] = -sn, m[5] = cs;
                }
            }
            for (int r = 0; r < 3; ++r)
                for (int j = 0; j < 4; ++j)
                    m[std::size_t(r * 4 + j)] *= size;
            const auto p = transformed({pt.place[0], pt.place[1], pt.place[2], 1}, view);
            for (int j = 0; j < 4; ++j)
                m[std::size_t(12 + j)] = p[std::size_t(j)];
            if (out != nullptr)
                draw_shape(shape, frame, mul(m, proj),
                           {k.b(8) / 128.0F, k.b(9) / 128.0F, k.b(10) / 128.0F, alpha / 128.0F}, *out);
            if (!plain && frozen == 0) { // the frame moved on
                std::uint8_t next = std::uint8_t(pt.bytes[1] + 1);
                if (int(shape.frames.size()) <= next) {
                    const std::size_t was = std::min<std::size_t>(pt.bytes[1], shape.frames.size() - 1);
                    const bool loops = shape.frames.at(was).loop;
                    pt.bytes[1] = 0;
                    if (!loops)
                        pt.bytes[0] = 0;
                    continue;
                }
                pt.bytes[1] = next;
            }
        }
    }

    // ---------------------------------------------------------------------------------- the particle spawner
    void mega_step(Obj &o, const EffectCommand &c, const Key &k) // FUN_1404bc3b0 + FUN_1404bbbe0
    {
        if (!o.pool_made) {
            o.pool.assign(k.u16(0x1c), std::nullopt);
            const std::array<float, 3> w{k.f32(0x98), k.f32(0x9c), k.f32(0xa0)};
            const float n = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
            o.wind = n > 0 ? std::array<float, 3>{w[0] / n, w[1] / n, w[2] / n} : w;
            o.pool_made = true;
        }
        if (k.s32(4) == 0xffff)
            return;
        const int alpha = *o.at(c.slots[1] + 0xb, 1);
        const auto &frames = block->shapes.at(std::size_t(k.s32(4))).frames;
        ++o.pool_count;
        int born = 0;
        for (auto &slot : o.pool) {
            if (!slot || slot->life == 0) {
                if (k.u16(0x20) <= o.pool_count && born < k.u16(0x1e)) {
                    slot = particle_birth(o, k, alpha);
                    ++born;
                } else {
                    slot.reset();
                }
                continue;
            }
            Particle &p = *slot;
            particle_step(o, k, p, alpha);
            p.prev = p.frame;
            p.t += k.u16(8);
            const auto &f = frames.at(std::size_t(std::min<int>(p.prev, int(frames.size()) - 1)));
            if (f.wait <= p.t) {
                p.t -= f.wait;
                ++p.frame;
                if (int(frames.size()) <= p.frame) {
                    p.t = 0;
                    p.frame = f.loop ? 0 : p.prev;
                }
            }
        }
        if (born > 0)
            o.pool_count = 0;
    }

    M4 slot_matrix(const Key &k) const // FUN_1404bc3b0: odd kinds the world's scale alone, else the world
    {
        const int kind = k.b(0x26);
        if (kind == 1 || kind == 3 || kind == 5 || kind == 7 || kind == 9) {
            M4 m = identity();
            for (int j = 0; j < 3; ++j) {
                m[std::size_t(j * 5)] = world_scale[std::size_t(j)];
                m[std::size_t(12 + j)] = world[std::size_t(12 + j)];
            }
            return m;
        }
        return world;
    }

    Particle particle_birth(Obj &o, const Key &k, int alpha) // FUN_1404bac40
    {
        Particle p;
        const int kind = k.b(0x26), dist = k.b(0xc8);
        const float spread = float(k.b(0x27));
        const std::array<float, 3> base{k.f32(0x8c), k.f32(0x90), k.f32(0x94)};
        const std::array<float, 3> axes{k.f32(0xbc), k.f32(0xc0), k.f32(0xc4)};
        constexpr float pi = 3.14159265F;
        if (kind < 8) {
            std::array<float, 3> ang{};
            for (auto &a : ang)
                a = ((rand01() * (spread + spread) - spread) * pi) / 180.0F;
            if (kind == 2 || kind == 3)
                ang[0] = 0;
            const auto to = [](float r) { return std::int32_t(r / (2 * pi) * 65536.0F); };
            const M4 m = mul(mul(rot_x(to(ang[0])), rot_y(to(ang[1]))), rot_z(to(ang[2])));
            const auto d = transformed({base[0], base[1], base[2], 0}, m);
            std::array<float, 3> v{d[0] * axes[0], d[1] * axes[1], d[2] * axes[2]};
            const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            p.dir = n > 0 ? std::array<float, 3>{v[0] / n, v[1] / n, v[2] / n} : v;
        }
        const float radius = k.f32(0xb8);
        const auto pick = [&]() {
            switch (dist) {
            case 1: rand01(); return rand01();
            case 2: return rand01() * rand01();
            case 3: return 1.0F - rand01() * rand01() * 0.7F;
            case 4: return rand01() * rand01() * rand01() * rand01();
            case 5: return 1.0F - rand01() * rand01() * rand01() * 0.5F;
            default: return 1.0F;
            }
        };
        if (kind < 4 || kind > 9) {
            if (radius != 0)
                for (int j = 0; j < 3; ++j)
                    p.pos[std::size_t(j)] = p.dir[std::size_t(j)] * radius * pick();
        } else if (kind < 6) {
            if (radius != 0)
                for (int j = 0; j < 3; ++j)
                    p.pos[std::size_t(j)] = (rand01() * radius - radius * 0.5F) * axes[std::size_t(j)];
        } else if (k.s16(0xd0) >= 0) { // at a vertex list's vertex
            const auto &vl = block->vertex_lists.at(std::size_t(k.s16(0xd0)));
            const auto &vs = block->models.at(vl.model).vertices;
            std::size_t i;
            if (dist == 0) {
                if (int(vl.vertices.size()) <= o.vertex_k)
                    o.vertex_k = 0;
                i = std::size_t(o.vertex_k++);
            } else {
                float f;
                switch (dist) {
                case 1: rand01(); f = rand01(); break;
                case 2: f = rand01() * rand01() * rand01(); break;
                case 3: f = 1.0F - rand01() * rand01() * rand01(); break;
                case 4: f = rand01() * rand01() * rand01() * rand01(); break;
                default: f = 1.0F - rand01() * rand01() * rand01() * rand01() * rand01(); break;
                }
                i = std::min(vl.vertices.size() - 1, std::size_t(float(vl.vertices.size()) * f));
            }
            const auto &v = vs.at(vl.vertices.at(i));
            for (int j = 0; j < 3; ++j)
                p.pos[std::size_t(j)] = v[std::size_t(j)] * axes[std::size_t(j)];
            if (kind >= 8) {
                const float n = std::sqrt(p.pos[0] * p.pos[0] + p.pos[1] * p.pos[1] + p.pos[2] * p.pos[2]);
                if (n > 0)
                    p.dir = {p.pos[0] / n, p.pos[1] / n, p.pos[2] / n};
            }
        }
        for (int j = 0; j < 4; ++j) {
            const int b = k.b(0x48 + std::size_t(j));
            p.jitter[std::size_t(j)] = int(rand01() * float(b * 2) - float(b >> 1));
        }
        p.fade_in = k.b(0x24);
        p.fade_out = k.b(0x25);
        p.fade = p.fade_in ? float(alpha) : 0.0F;
        p.scale_in = k.b(0xd3);
        p.scale_out = k.b(0xd4);
        p.scale = p.scale_in ? 0.0F : 1.0F;
        const auto deg = [](float d) { return std::int32_t(d * 32768.0F / 180.0F); };
        p.rot = deg(k.f32(0x7c));
        p.rot_v = deg(k.f32(0x80));
        const int rf = k.b(0xcb);
        if (rf) {
            const std::int32_t x = deg(rand01() * k.f32(0x88));
            p.rot_a = x;
            if ((rf & 3) == 3) {
                if (rand01() > 0.5F)
                    p.rot_a = -x;
            } else if (rf & 2) {
                p.rot_a = -x;
            }
        }
        if (rf & 4)
            p.rot += p.rot_a;
        if (rf & 8)
            p.rot_v += p.rot_a;
        p.w = k.f32(0x4c);
        p.h = k.f32(0x50);
        p.dw = k.f32(0x58);
        p.dh = k.f32(0x5c);
        const int sf = k.b(0xca);
        if (sf) {
            p.rw = k.f32(0x70) * rand01();
            p.rh = (sf & 0x20) ? p.rw : rand01() * k.f32(0x74);
            if ((sf & 3) == 3) {
                if (rand01() > 0.5F)
                    p.rw = -p.rw;
                if (rand01() > 0.5F)
                    p.rh = -p.rh;
            } else if (sf & 2) {
                p.rw = -p.rw;
                p.rh = -p.rh;
            }
        }
        if (sf & 4) {
            p.w += p.rw;
            p.h += p.rh;
        }
        if (sf & 8) {
            p.dw += p.rw;
            p.dh += p.rh;
        }
        p.speed = k.f32(0xa4);
        p.speed2 = k.f32(0xb0);
        if (const float j = k.f32(0xac); j != 0)
            p.speed += (j + j) * rand01() - j;
        if (const float j = k.f32(0xd8); j != 0)
            p.speed2 += (j + j) * rand01() - j;
        const int life = k.s16(0x22);
        p.life = life == 0 ? -1 : life;
        switch (k.b(0xcc)) { // the matrix kept from the birth
        case 1: p.born = slot_matrix(k); break;
        case 2: p.born = o.local; break;
        case 3: p.born = mul(o.local, slot_matrix(k)); break;
        default: break;
        }
        return p;
    }

    void particle_step(Obj &o, const Key &k, Particle &p, int alpha) // FUN_1404bb8b0
    {
        p.rot = (p.rot + p.rot_v) & 0xffff;
        p.rot_v = std::int32_t(float(p.rot_v) + k.f32(0x84) + float((k.b(0xcb) & 0x10) ? p.rot_a : 0));
        const float width_rate = p.dw;
        p.w += p.dw;
        p.h += p.dh;
        if (k.b(0xca) & 0x10) {
            p.dw = p.rw + k.f32(0x64) + width_rate;
            p.dh = p.dh + p.rh + k.f32(0x68);
        } else {
            p.dw = width_rate + k.f32(0x64);
            p.dh += k.f32(0x68);
        }
        float sp = p.speed + k.f32(0xa8);
        p.speed = sp;
        if (k.b(0xce) == 0) {
            const float a4 = k.f32(0xa4), a8 = k.f32(0xa8);
            if ((a4 > 0 && a8 < 0 && sp < 0) || (a4 < 0 && a8 > 0 && sp > 0))
                p.speed = sp = 0;
        }
        p.speed2 += k.f32(0xb4);
        for (int j = 0; j < 3; ++j)
            p.pos[std::size_t(j)] += p.dir[std::size_t(j)] * sp + o.wind[std::size_t(j)] * p.speed2;
        if (k.s16(0x22) != 0)
            --p.life;
        p.age = (p.age + 1) & 0xff;
        if (p.fade_in && p.age <= p.fade_in)
            p.fade -= float(alpha) / float(p.fade_in);
        if (p.fade_out && p.life <= p.fade_out)
            p.fade += float(alpha) / float(std::max(1, int(k.b(0x25))));
        if (p.scale_in && p.age <= p.scale_in)
            p.scale = float(p.age) / float(p.scale_in);
        if (p.scale_out && p.life <= p.scale_out)
            p.scale = float(p.life) / float(p.scale_out);
    }

    // ---------------------------------------------------------------------------------- drawing
    static EffectVertex vertex(const std::array<float, 4> &clip, float u, float v, const std::array<float, 4> &c)
    {
        EffectVertex e;
        e.x = clip[0];
        e.y = clip[1];
        e.z = clip[2];
        e.w = clip[3];
        e.u = u;
        e.v = v;
        e.r = c[0];
        e.g = c[1];
        e.b = c[2];
        e.a = c[3];
        return e;
    }

    // the model draws (FUN_1404c7940 pppDrawMdl, FUN_1404c7c70 pppDrawMdlTs, FUN_1404c7f80 pppDrawMdl2,
    // FUN_1404c8660 pppDrawMdl3, FUN_1404c88c0 pppDrawMdlSemi3, FUN_1404c8b10 pppDrawMdlTs3, FUN_1404c90f0
    // pppDrawMdlLoop, FUN_1404c9540 pppDrawMdlLoopZ): each fills the renderer's request (FUN_1404dc760 ->
    // FUN_1404b6860) from its record. Its flags: 8 the depth written (the model's flag bit 0 clear), 0x80 not (and
    // the GS's TEST left as it is), 4 the depth rule two record bytes give (the first: the depth alone, 0x72001;
    // the second: always, 0x3000d). The scene's light colours (flags 0x100, 0x200) and the view-space normals'
    // matrix (0x2000) are not drawn here: KH2's lights are not the port's.
    void draw_model(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        const std::int32_t m = k.s32(4);
        if (m == 0xffff || out == nullptr)
            return;
        const auto &model = block->models.at(std::size_t(m));
        const std::uint8_t *colour = o.at(c.slots[0] + 8, 4);
        if (colour[3] == 0)
            return;
        const bool writes = (model.flags & 1) == 0;
        unsigned flags = 0;
        std::uint8_t only = 0, always = 0;
        std::uint16_t alpha = 0; // 0: the model's (FUN_1404b6860), else ALPHA in its low byte, FIX in its high
        float su = 0, sv = 0;
        const auto keyed = [&](std::uint8_t kb) {
            if (kb & 2)
                flags = writes ? 0xc : 4;
            else
                flags = ((kb & 1) ? 0x80u : 0u) | (writes ? 8u : 0u);
        };
        switch (c.code) {
        case 0x83: case 0x85: flags = writes ? 0xc : 0x80; break;
        case 0x86: keyed(k.b(8)); only = k.b(9); always = k.b(0xa); break;
        case 0x89: keyed(k.b(8)); only = k.b(0xd); always = k.b(0xe); break;
        case 0x8a: keyed(k.b(0xa)); always = k.b(0xf); alpha = k.u16(8); break;
        case 0x8b: keyed(k.b(0x20)); always = k.b(0x25); break;
        default: // 0xda, 0xdb
            flags = (k.b(0x22) & 2) ? 4u : ((k.b(0x22) & 1) ? 0x80u : 0u);
            flags |= writes ? 8u : 0u;
            if (k.b(0x20) != 0xff)
                alpha = k.u16(0x20);
            break;
        }
        if (c.code == 0x85 || c.code == 0x8b) {
            su = o.f(c.slots[2], 0);
            sv = o.f(c.slots[2], 3);
        } else if (c.code == 0xda || c.code == 0xdb) {
            su = o.f(c.slots[2], 1);
            sv = o.f(c.slots[2], 4);
        }
        const std::array<float, 4> tint{float(colour[0]), float(colour[1]), float(colour[2]), float(colour[3])};
        draw_model_with(o.morphed ? &o.morph : nullptr, model, o.draw, tint, flags, only, always, alpha, su, sv,
                        *out);
    }

    // a model through `m` (FUN_1404b6860's work): its vertices (`own` when given: pppVtMime's blend), each
    // primitive's corner colour x `tint` (0x80 = 1 both), the request's flags and depth bytes, its ALPHA (0: the
    // model's), the UV scroll added; the primitives far to near, a draw a page
    void draw_model_with(const std::vector<std::array<float, 3>> *own, const EffectModel &model, const M4 &m,
                         const std::array<float, 4> &tint,
                         unsigned flags, std::uint8_t only, std::uint8_t always, std::uint16_t alpha, float su,
                         float sv, std::vector<EffectDraw> &out)
    {
        EffectDraw d;
        d.alpha = alpha != 0 ? std::uint8_t(alpha & 0xff) : model.alpha;
        d.depth_write = (flags & 8) != 0 && (flags & 0x80) == 0;
        if (flags & 4) {
            if (always)
                d.depth_test = false;
            else if (only)
                d.depth_only = true;
        }
        const auto &vertices = own != nullptr ? *own : model.vertices;
        std::vector<std::array<float, 4>> clip(model.vertices.size());
        for (std::size_t i = 0; i < clip.size(); ++i) {
            const auto &v = i < vertices.size() ? vertices[i] : model.vertices[i];
            clip[i] = transformed({v[0], v[1], v[2], 1}, m);
        }
        // a model's primitives far to near (the depth buckets of FUN_1404af7d0)
        std::vector<std::pair<float, const EffectPrimitive *>> order;
        for (const auto &p : model.primitives) {
            float z = 0;
            for (int q = 0; q < p.corners; ++q) {
                const auto &v = clip[p.vertex[std::size_t(q)]];
                z += v[3] != 0 ? v[2] / v[3] : 0;
            }
            order.emplace_back(z / float(p.corners), &p);
        }
        std::stable_sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::uint64_t tex = ~0ull;
        for (const auto &[z, p] : order) {
            if (p->tex0 != tex) {
                if (!d.triangles.empty())
                    out.push_back(d);
                d.triangles.clear();
                d.tex0 = p->tex0;
                tex = p->tex0;
            }
            const auto corner = [&](int q) {
                std::array<float, 4> col{};
                for (int j = 0; j < 4; ++j)
                    col[std::size_t(j)] = float(p->colour[std::size_t(q)][std::size_t(j)]) / 128.0F *
                                          tint[std::size_t(j)] / 128.0F;
                return vertex(clip[p->vertex[std::size_t(q)]], p->uv[std::size_t(q)][0] + su,
                              p->uv[std::size_t(q)][1] + sv, col);
            };
            if (p->corners == 4) {
                for (const int q : {0, 1, 2, 1, 3, 2})
                    d.triangles.push_back(corner(q));
            } else {
                for (const int q : {0, 1, 2})
                    d.triangles.push_back(corner(q));
            }
        }
        if (!d.triangles.empty())
            out.push_back(std::move(d));
    }

    // pppVtMime's draw (FUN_1404c17b0): the object's vertices a blend of two models', t of the way from the
    // first to the second, made shorts; drawn and spawned at in the model's place from then on
    void vt_mime(Obj &o, const EffectCommand &c, const Key &k)
    {
        const std::int32_t a = k.s32(4), b = k.s32(8);
        if (a == 0xffff && b == 0xffff)
            return;
        if (a < 0 || b < 0 || std::size_t(a) >= block->models.size() || std::size_t(b) >= block->models.size())
            return;
        const auto &from = block->models[std::size_t(a)].vertices, &to = block->models[std::size_t(b)].vertices;
        const float t = o.f(c.slots[0], 0);
        o.morph.resize(from.size());
        for (std::size_t i = 0; i < from.size(); ++i)
            for (int j = 0; j < 3; ++j) {
                const float f = from[i][std::size_t(j)], g = i < to.size() ? to[i][std::size_t(j)] : f;
                o.morph[i][std::size_t(j)] = float(std::int16_t(std::int32_t((g - f) * t + f)));
            }
        o.morphed = true;
    }

    void draw_shape(const EffectShape &shape, int frame, const M4 &m, const std::array<float, 4> &colour,
                    std::vector<EffectDraw> &out)
    {
        const auto &f = shape.frames.at(std::size_t(std::clamp(frame, 0, int(shape.frames.size()) - 1)));
        for (const auto &l : f.layers) {
            EffectDraw d;
            d.tex0 = f.tex0;
            d.alpha = l.alpha;
            const float x0 = l.rect[0], y0 = l.rect[1], x1 = x0 + l.rect[2], y1 = y0 + l.rect[3];
            const float uv[4][2]{{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
            std::array<float, 4> c{};
            for (int j = 0; j < 4; ++j)
                c[std::size_t(j)] = float(l.colour[std::size_t(j)]) / 128.0F * colour[std::size_t(j)];
            for (const int q : {0, 1, 2, 1, 3, 2}) {
                const auto p = transformed({l.corners[std::size_t(q)][0], l.corners[std::size_t(q)][1], 0, 1}, m);
                d.triangles.push_back(vertex(p, uv[q][0], uv[q][1], c));
            }
            out.push_back(std::move(d));
        }
    }

    // word 3, every tick (FUN_1404b09e0): `out` null when nothing is drawn, the commands' own state kept all the same
    void draw(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> *out)
    {
        switch (c.code) {
        case 0x83: case 0x85: case 0x86: case 0x89: case 0x8a: case 0x8b: case 0xda: case 0xdb:
            draw_model(o, c, k, out);
            break;
        case 0x31: vt_mime(o, c, k); break;
        case 0x99: case 0x9a: case 0x9b: tail_draw(o, c, k, out); break;
        case 0x9c: case 0x9d: tail3_draw(o, c, k, out); break;
        case 0xd0: model_birth_draw(o, c, k, out); break;
        case 0xc8: swing_draw(o, c, k, out); break;
        case 0xef: user_sprites(k, out); break;
        case 0x91: case 0xa0: case 0xee: distortion(o, c, k, out); break;
        case 0xbd: case 0xdd: flare_line(o, c); break;
        case 0xbe: case 0xbf: case 0xc0: case 0xc1: case 0xdf: case 0xe0: case 0xe1: flare_element(o, c, k, out); break;
        case 0xf1: ribbon_draw(o, c, k, out); break;
        case 0x8d: case 0x8e: { // pppDrawShape(X) (FUN_1404c97c0, FUN_1404c99c0): the shape's frame, the matrix
            if (k.s32(4) == 0xffff || out == nullptr)
                return;
            const std::uint8_t *col = o.at(c.slots[1] + 8, 4);
            draw_shape(block->shapes.at(std::size_t(k.s32(4))), rd<std::uint16_t>(o.at(c.slots[0] + 2, 2)), o.draw,
                       {col[0] / 128.0F, col[1] / 128.0F, col[2] / 128.0F, col[3] / 128.0F}, *out);
            break;
        }
        case 0xc9: { // FUN_1404bbdb0 + FUN_1404bc710: each particle a sprite in view space
            if (k.s32(4) == 0xffff || !o.pool_made || out == nullptr)
                return;
            const auto &shape = block->shapes.at(std::size_t(k.s32(4)));
            const std::uint8_t *col = o.at(c.slots[1] + 8, 4);
            const M4 slot = slot_matrix(k);
            for (const auto &slot_p : o.pool) {
                if (!slot_p || slot_p->life == 0)
                    continue;
                const Particle &p = *slot_p;
                M4 to_view;
                switch (k.b(0xcc)) {
                case 0: to_view = mul(mul(o.local, slot), view); break;
                case 2: to_view = mul(mul(p.born, slot), view); break;
                default: to_view = mul(mul(o.local, p.born), view); break;
                }
                const float sw = world_scale[0] * p.w * p.scale, sh = world_scale[1] * p.h * p.scale;
                const auto [s, cs] = sin_cos(p.rot);
                M4 bill = identity();
                bill[0] = cs * sw;
                bill[1] = s * sw;
                bill[4] = -s * sh;
                bill[5] = cs * sh;
                auto pos = transformed({p.pos[0], p.pos[1], p.pos[2], 1}, to_view);
                if (k.b(0xe)) // a view-space offset (toward the camera)
                    for (int j = 0; j < 3; ++j)
                        pos[std::size_t(j)] += k.f32(0x10 + 4 * std::size_t(j));
                for (int j = 0; j < 4; ++j)
                    bill[std::size_t(12 + j)] = pos[std::size_t(j)];
                std::array<float, 4> tint{};
                for (int j = 0; j < 3; ++j)
                    tint[std::size_t(j)] = float(std::clamp(col[j] + p.jitter[std::size_t(j)], 0, 255)) / 128.0F;
                tint[3] = std::clamp(float(col[3] + p.jitter[3]) - p.fade, 0.0F, 127.0F) / 128.0F;
                draw_shape(shape, p.frame, mul(bill, proj), tint, *out);
            }
            break;
        }
        default: break;
        }
    }

    // ---------------------------------------------------------------------------------- a tick
    void run(std::vector<EffectDraw> *draws)
    {
        // FUN_1404aff20: the layers whose start has come
        for (std::size_t i = 0; i < emitter->layers.size(); ++i)
            if (!started[i] && emitter->layers[i].start <= age) {
                started[i] = true;
                birth(emitter->layers[i])->skip = false;
            }
        // FUN_1404b0fd0: each object's steps of the commands without a draw
        const auto snapshot = objs;
        for (const auto &po : snapshot) {
            Obj &o = *po;
            if (o.skip)
                continue;
            if (o.restart) {
                o.age = 0;
                reset(o);
                o.restart = false;
            }
            for (std::size_t ci = 0; ci < o.layer->commands.size(); ++ci) {
                const auto &c = o.layer->commands[ci];
                if ((words(c.code) & W1) && !(words(c.code) & W3))
                    step(o, c, advance(o, ci));
            }
        }
        // FUN_1404b09e0: the commands with a draw: their step, then the draw
        for (const auto &po : snapshot) {
            Obj &o = *po;
            if (o.skip)
                continue;
            for (std::size_t ci = 0; ci < o.layer->commands.size(); ++ci) {
                const auto &c = o.layer->commands[ci];
                if (!(words(c.code) & W3))
                    continue;
                const Key k = (words(c.code) & W1) ? advance(o, ci) : key(o, ci);
                if (words(c.code) & W1)
                    step(o, c, k);
                draw(o, c, k, draws);
            }
        }
        for (const auto &po : objs) {
            if (po->skip)
                po->skip = false;
            else
                grow_age(*po);
        }
        // lives and loops; a loop's fast-forward may bear children (pppPointAp's steps run then), kept in their place
        std::int32_t lives = 0;
        bool any_gone = false;
        const auto current = objs;
        for (const auto &po : current) {
            Obj &o = *po;
            const EffectLayer &L = *o.layer;
            if (!stopping && L.loop_end <= o.age && L.loop_start < kNever && L.loop_end < kNever) {
                o.age = 0;
                reset(o);
                fastforward = true;
                if (L.loop_start == 0) {
                    step_one(o);
                    o.restart = true;
                } else {
                    while (o.age < L.loop_start)
                        step_one(o);
                }
                fastforward = false;
            } else {
                lives |= L.life;
                o.gone = L.life <= o.age;
                any_gone |= o.gone;
            }
        }
        if (any_gone)
            objs.erase(std::remove_if(objs.begin(), objs.end(), [](const auto &po) { return po->gone; }), objs.end());
        if (emitter->repeat && stopping && lives == kNever)
            objs.clear();
        // FUN_1404b08e0: the instance's age and its end
        age += kFrame;
        if (!emitter->repeat)
            finished = emitter->length <= age;
        else
            finished = stopping && objs.empty();
    }

    void camera(const EffectCamera &c)
    {
        world = c.world;
        view = c.view;
        proj = c.projection;
        wv = mul(world, view);
        wvp = mul(wv, proj);
        for (int r = 0; r < 3; ++r)
            world_scale[std::size_t(r)] = row_length(world, r);
        // FUN_1402c3310's upright view (eda0): the world's Y axis in view space kept upright
        std::array<float, 3> y{wv[4], wv[5], wv[6]};
        float n = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
        if (n > 0)
            y = {y[0] / n, y[1] / n, y[2] / n};
        std::array<float, 3> x{y[1], -y[0], 0};
        n = std::sqrt(x[0] * x[0] + x[1] * x[1]);
        if (n > 0)
            x = {x[0] / n, x[1] / n, 0};
        std::array<float, 3> z{x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
        n = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
        if (n > 0)
            z = {z[0] / n, z[1] / n, z[2] / n};
        upright = identity();
        for (int j = 0; j < 3; ++j) {
            upright[std::size_t(j)] = x[std::size_t(j)];
            upright[std::size_t(4 + j)] = y[std::size_t(j)];
            upright[std::size_t(8 + j)] = z[std::size_t(j)];
        }
        wood = mul(upright, proj);
    }
};

EffectInstance::EffectInstance(std::shared_ptr<const EffectBlock> block, std::size_t emitter, std::uint32_t seed)
    : state(std::make_unique<State>())
{
    if (!block || emitter >= block->emitters.size())
        fail("no such emitter");
    state->block = std::move(block);
    state->emitter = &state->block->emitters[emitter];
    state->random.seed(seed);
    state->started.assign(state->emitter->layers.size(), false);
    for (const auto &L : state->emitter->layers)
        for (const auto &c : L.commands)
            if (words(c.code) == 0)
                fail("command " + std::to_string(c.code) + " not read yet");
}

EffectInstance::~EffectInstance() = default;
EffectInstance::EffectInstance(EffectInstance &&) noexcept = default;
EffectInstance &EffectInstance::operator=(EffectInstance &&) noexcept = default;

void EffectInstance::tick(const EffectCamera &camera, std::vector<EffectDraw> *draws)
{
    if (state->finished)
        return;
    state->camera(camera);
    state->run(draws);
}

void EffectInstance::stop()
{
    state->stopping = true;
}

void EffectInstance::set_points(std::vector<EffectPoint> points)
{
    state->points = std::move(points);
}

bool EffectInstance::done() const
{
    return state->finished;
}

std::size_t EffectInstance::objects() const
{
    return state->objs.size();
}

const EffectBlock &EffectInstance::block() const
{
    return *state->block;
}

} // namespace khdays::assets::kh2
