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
            if (kind == 6 || kind == 4) { // quads: 4 RGBA, 4 vertices (6: 4 UV in 1/4096 of the page)
                e.corners = 4;
                for (int c = 0; c < 4; ++c) {
                    for (int k = 0; k < 4; ++k)
                        e.colour[std::size_t(c)][std::size_t(k)] = r.u8(it + 4 * std::size_t(c) + std::size_t(k));
                    e.vertex[std::size_t(c)] = r.u16(it + 0x10 + 2 * std::size_t(c));
                }
                if (kind == 6) {
                    e.tex0 = tex0;
                    for (int c = 0; c < 4; ++c)
                        e.uv[std::size_t(c)] = {float(r.u16(it + 0x18 + 4 * std::size_t(c))) / 4096.0F,
                                                float(r.u16(it + 0x1a + 4 * std::size_t(c))) / 4096.0F};
                }
            } else if (kind == 0) { // an untextured triangle: 3 RGBA, 3 vertices
                e.corners = 3;
                for (int c = 0; c < 3; ++c) {
                    for (int k = 0; k < 4; ++k)
                        e.colour[std::size_t(c)][std::size_t(k)] = r.u8(it + 4 * std::size_t(c) + std::size_t(k));
                    e.vertex[std::size_t(c)] = r.u16(it + 0xc + 2 * std::size_t(c));
                }
            } else {
                fail("a primitive of kind " + std::to_string(kind) + " (not read yet)");
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

std::array<float, 4> apply(const std::array<float, 4> &v, const M4 &m)
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

struct Obj {
    const EffectLayer *layer = nullptr;
    std::int32_t age = 0;
    bool skip = true, restart = false;
    std::vector<std::uint8_t> mem;     // its memory from +0xa0
    std::vector<std::size_t> cursor;   // each command's key
    std::map<std::int32_t, const std::vector<std::uint8_t> *> first; // the loop commands' first keys
    M4 local = identity(), draw = identity();
    std::shared_ptr<Obj> parent;
    std::vector<std::optional<Particle>> pool;
    int pool_count = 0, vertex_k = 0;
    std::array<float, 3> wind{};
    bool pool_made = false;

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

// which descriptor words a command has (KH2's table at 0x140779a00): the step (1), the draw / particle step (3)
enum : unsigned { W1 = 1, W3 = 2, SPAWNER = 4 };
unsigned words(std::uint32_t code)
{
    switch (code) {
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29:
    case 0x2a: case 0x2b: case 0x33: case 0x3f: case 0x42: case 0x4b: case 0x54: case 0x57: case 0x5e: case 0x69:
    case 0x70: case 0x71: case 0x73: case 0x7c: case 0x7d: case 0xae: case 0xd3: case 0xd4: case 0xd5: case 0xd6:
    case 0xd7: case 0xd8: case 0xd9: case 0xea:
        return W1;
    case 0x89: return W3;
    case 0x8b: case 0x8e: case 0xdb: return W1 | W3;
    case 0xc9: return W1 | W3 | SPAWNER;
    default: return 0;
    }
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
    M4 world = identity(), view = identity(), proj = identity(), wv = identity(), wvp = identity(), wood = identity();
    std::array<float, 3> world_scale{1, 1, 1};

    float rand01() // FUN_1404dc710: 24 random bits x 2^-24
    {
        return float(random() & 0xffffffu) * (1.0F / 16777216.0F);
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
        case 0xae: zero(0, 8); break;
        case 0x8b: zero(2, 24); break;
        case 0x8e: zero(0, 8); break;
        case 0xdb: o.seti(c.slots[2], 0, 0); break;
        default: break;
        }
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
        case 0x8e: std::memset(o.at(c.slots[0], 8), 0, 8); break;
        case 0xdb:
            std::memset(o.at(c.slots[2] + 8, 8), 0, 8);
            std::memset(o.at(c.slots[2] + 0x14, 8), 0, 8);
            break;
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
        case 0x33: case 0x3f: case 0x4b: case 0x42: random_command(o, c, k); break;
        case 0x57: case 0xd8: case 0xea: { // [position, angle, scale] -> the object's matrix
            const std::array<std::int32_t, 3> a{o.i(s(1), 0), o.i(s(1), 1), o.i(s(1), 2)};
            M4 m = c.code == 0xea ? mul(mul(rot_z(a[2]), rot_x(a[0])), rot_y(a[1]))  // FUN_1404e01f0
                                  : mul(mul(rot_x(a[0]), rot_y(a[1])), rot_z(a[2])); // FUN_1404be820
            for (int r = 0; r < 3; ++r)
                for (int j = 0; j < 3; ++j)
                    m[std::size_t(r * 4 + j)] *= o.f(s(2), r);
            for (int j = 0; j < 3; ++j)
                m[std::size_t(12 + j)] = o.f(s(0), j);
            o.local = m;
            break;
        }
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
                const auto p = apply({o.local[12], o.local[13], o.local[14], 1}, o.parent->local);
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
            const auto p = apply({o.local[12], o.local[13], o.local[14], 1}, wv);
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
            const auto p = apply({o.local[12], o.local[13], o.local[14], 1}, wvp);
            for (int j = 0; j < 4; ++j)
                m[std::size_t(12 + j)] = p[std::size_t(j)];
            o.draw = m;
            break;
        }
        case 0x54: case 0x7c: case 0x7d: break; // a shared vector; the depth order's corrections (sorting only)
        case 0xae: vertex_ap(o, c, k); break;
        case 0x8b: { // pppDrawMdlTs3's scroll (FUN_1404c8da0): u (pos, vel, acc) at +0, v at +0xc
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
        case 0xdb: { // pppDrawMdlLoopZ's scroll (FUN_1404c9350): handle, u, du, ddu, v, dv, ddv; wrapped
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
        case 0x8e: shape_step(o, c, k); break;
        case 0xc9: mega_step(o, c, k); break;
        default: fail("command " + std::to_string(c.code) + " not read yet");
        }
    }

    void random_command(Obj &o, const EffectCommand &c, const Key &k)
    {
        const std::int32_t s = c.slots[0];
        const auto draw2 = [&](std::uint8_t sum) { // two draws summed, or one doubled (DAT_140623b18)
            const float x = rand01();
            return sum ? x + rand01() : x * 2.0F;
        };
        if (c.code == 0x42) { // pppRandIV (FUN_1404c2950): a new value on each key
            if (!hit(o, k))
                return;
            const float x = draw2(k.b(0x14));
            o.setf(s, 0, x);
            const std::int32_t t = k.s32(4);
            if (t == -1)
                return;
            for (int j = 0; j < 3; ++j) {
                const std::int32_t b = k.s32(8 + 4 * std::size_t(j));
                o.seti(t, j, o.i(t, j) + std::int32_t(float(b) * x - float(b)));
            }
            return;
        }
        const int n = c.code == 0x33 ? 1 : 3;
        const std::size_t flag = c.code == 0x33 ? 0xc : 0x14;
        if (o.age == 0) { // the value drawn at the start, kept
            for (int j = 0; j < (c.code == 0x4b ? 3 : 1); ++j)
                o.setf(s, j, draw2(k.b(flag)));
        } else if (!hit(o, k)) {
            return;
        }
        const std::int32_t t = k.s32(4);
        if (t == -1)
            return;
        for (int j = 0; j < n; ++j) {
            const float x = o.f(s, c.code == 0x4b ? j : 0), b = k.f32(8 + 4 * std::size_t(j));
            o.setf(t, j, o.f(t, j) + (b * x - b));
        }
    }

    void vertex_ap(Obj &o, const EffectCommand &c, const Key &k) // pppVertexAp (FUN_1404d3b30)
    {
        if (fastforward || k.s16(4) < 0)
            return;
        const std::int32_t s = c.slots[0];
        std::uint16_t wait = rd<std::uint16_t>(o.at(s + 2, 2));
        if (wait == 0) {
            const auto &vl = block->vertex_lists.at(std::size_t(k.s16(4)));
            const auto &vs = block->models.at(vl.model).vertices;
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
        const std::int32_t layer = k.s32(0xc);
        if (layer < 0 || std::size_t(layer) >= emitter->layers.size())
            return;
        auto child = birth(emitter->layers[std::size_t(layer)]);
        child->skip = true;
        child->parent = find(o);
        const auto p = apply({v[0], v[1], v[2], 1}, o.local);
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
    void shape_step(Obj &o, const EffectCommand &c, const Key &k) // pppDrawShapeX (FUN_1404c9b30)
    {
        const std::int32_t shape = k.s32(4);
        if (shape == 0xffff)
            return;
        const auto &frames = block->shapes.at(std::size_t(shape)).frames;
        const std::int32_t s = c.slots[0];
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
            const auto d = apply({base[0], base[1], base[2], 0}, m);
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

    void draw_model(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> &out) // FUN_1404c8660..
    {
        const std::int32_t m = k.s32(4);
        if (m == 0xffff)
            return;
        const auto &model = block->models.at(std::size_t(m));
        const std::uint8_t *colour = o.at(c.slots[0] + 8, 4);
        if (colour[3] == 0)
            return;
        EffectDraw d;
        d.alpha = model.alpha;
        d.depth_write = (model.flags & 1) == 0;
        float su = 0, sv = 0;
        if (c.code == 0x89 && (k.b(8) & 2)) { // FUN_1404dc760: +0xd with +0xe clear the depth alone, +0xe always
            if (k.b(0xe))
                d.depth_test = false;
            else if (k.b(0xd))
                d.depth_only = true;
        }
        if (c.code == 0x8b) {
            if ((k.b(0x20) & 2) && k.b(0x25))
                d.depth_test = false;
            su = o.f(c.slots[2], 0);
            sv = o.f(c.slots[2], 3);
        }
        if (c.code == 0xdb) {
            if (k.b(0x20) != 0xff)
                d.alpha = k.b(0x20); // the record's ALPHA in place of the model's (FUN_1404c9540)
            su = o.f(c.slots[2], 1);
            sv = o.f(c.slots[2], 4);
        }
        std::vector<std::array<float, 4>> clip(model.vertices.size());
        for (std::size_t i = 0; i < clip.size(); ++i)
            clip[i] = apply({model.vertices[i][0], model.vertices[i][1], model.vertices[i][2], 1}, o.draw);
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
                                          float(colour[j]) / 128.0F;
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
                const auto p = apply({l.corners[std::size_t(q)][0], l.corners[std::size_t(q)][1], 0, 1}, m);
                d.triangles.push_back(vertex(p, uv[q][0], uv[q][1], c));
            }
            out.push_back(std::move(d));
        }
    }

    void draw(Obj &o, const EffectCommand &c, const Key &k, std::vector<EffectDraw> &out) // word 3
    {
        switch (c.code) {
        case 0x89: case 0x8b: case 0xdb: draw_model(o, c, k, out); break;
        case 0x8e: { // pppDrawShapeX (FUN_1404c99c0): the shape's frame through the object's matrix
            if (k.s32(4) == 0xffff)
                return;
            const std::uint8_t *col = o.at(c.slots[1] + 8, 4);
            draw_shape(block->shapes.at(std::size_t(k.s32(4))), rd<std::uint16_t>(o.at(c.slots[0] + 2, 2)), o.draw,
                       {col[0] / 128.0F, col[1] / 128.0F, col[2] / 128.0F, col[3] / 128.0F}, out);
            break;
        }
        case 0xc9: { // FUN_1404bbdb0 + FUN_1404bc710: each particle a sprite in view space
            if (k.s32(4) == 0xffff || !o.pool_made)
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
                auto pos = apply({p.pos[0], p.pos[1], p.pos[2], 1}, to_view);
                if (k.b(0xe)) // a view-space offset (toward the camera)
                    for (int j = 0; j < 3; ++j)
                        pos[std::size_t(j)] += k.f32(0x10 + 4 * std::size_t(j));
                for (int j = 0; j < 4; ++j)
                    bill[std::size_t(12 + j)] = pos[std::size_t(j)];
                std::array<float, 4> tint{};
                for (int j = 0; j < 3; ++j)
                    tint[std::size_t(j)] = float(std::clamp(col[j] + p.jitter[std::size_t(j)], 0, 255)) / 128.0F;
                tint[3] = std::clamp(float(col[3] + p.jitter[3]) - p.fade, 0.0F, 127.0F) / 128.0F;
                draw_shape(shape, p.frame, mul(bill, proj), tint, out);
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
                if (draws != nullptr)
                    draw(o, c, k, *draws);
            }
        }
        for (const auto &po : objs) {
            if (po->skip)
                po->skip = false;
            else
                grow_age(*po);
        }
        // lives and loops
        std::int32_t lives = 0;
        std::vector<std::shared_ptr<Obj>> keep;
        for (const auto &po : objs) {
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
                keep.push_back(po);
            } else {
                lives |= L.life;
                if (o.age < L.life)
                    keep.push_back(po);
            }
        }
        objs = std::move(keep);
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
        M4 upright = identity();
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
