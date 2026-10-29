// The DS's 3D rendering engine at N times its resolution: port/gpu/gpu3d.c's rasterizer run for each
// pixel, one thread a pixel going through the polygons that may cover it in drawing order, so its
// per-pixel rules are exactly gpu3d.c's (docs/RENDERING.md, "On the graphics card"). In widescreen
// `origin_x` is the DS column of the target's first (negative).

struct Polygon {
    uint attr, tex, pltt;
    int first, count;
    float ymin, ymax;
    int translucent;
    int flags; // gfx/g3d.h KHDAYS_G3D_*: OVER_2D marks its pixels (bit 29 of the colour), the rest the host reads
    int replacement; // the replacement texture drawn instead (host/render/gpu_hd.h's entry), or -1
};

struct Vertex {
    float x, y, z, w, r, g, b, s, t, a;
};

static const uint G3D_MOD = 4u; // gfx/g3d.h KHDAYS_G3D_MOD: a mod's model's polygon

// Every storage buffer is read by its bytes: SDL's Direct3D 12 views them raw (no stride), and AMD's
// cards (vkd3d too) read a StructuredBuffer by the view's stride, so all its elements were the first.
ByteAddressBuffer polys : register(t0, space0);
ByteAddressBuffer verts : register(t1, space0);
ByteAddressBuffer tiles : register(t2, space0);   // tiles_x*tiles_y+1 starts, then polygon indices
ByteAddressBuffer vram : register(t3, space0);         // the texture slots (512 KB), then the palette slots (96 KB)
ByteAddressBuffer hd : register(t4, space0); // the replacements (host/render/gpu_hd.h): 4 words each, then their texels (RGBA8, red low)
ByteAddressBuffer sequences : register(t5, space0); // retained native sequences in this 3D list
#define KSI_SHADER
#include "../gfx/sequence_image.h"
uint seq_word(uint program, uint at) { return sequences.Load((program*KSI_WORDS+at)*4u); }
uint seq_hd_word(uint at) { return hd.Load(at); }
#include "kh2_sequence_sample.hlsli"

[[vk::image_format("r32ui")]] RWTexture2D<uint> out_color : register(u0, space1);
[[vk::image_format("r32ui")]] RWTexture2D<uint> out_attr : register(u1, space1);

cbuffer Params : register(b0, space2) {
    uint scale;       // N
    uint width;       // the target, pixels
    uint height;
    uint disp3dcnt;
    uint swap_params;
    uint tiles_x;     // tiles of 8x8 DS pixels
    uint tiles_y;
    uint alpha_ref;   // ALPHA_TEST_REF
    uint clear_color; // CLEAR_COLOR
    uint clear_depth; // CLEAR_DEPTH, CLRIMAGE_OFFSET << 16
    uint smooth;      // bit 0: texture smoothing (not the DS's); bit 1: screen space stretched to the target
    int origin_x;     // the DS column of the target's first column (0 at 4:3)
    uint fine;        // 1: colours in 8 bits a channel (HD textures drawn), not the DS's 6
    uint npolys;      // what the host uploaded: no polygon, vertex or tile entry past them is read
    uint nverts;
    uint entries;
    uint4 toon[4];    // TOON_TABLE, two entries per uint
};

#define TEX_PAL 0x80000u

// gfx/g3d.h's KhdaysG3dPolygon (40 bytes) and KhdaysG3dVertex (40 bytes), word by word
Polygon polygon_at(uint i)
{
    const uint4 a = polys.Load4(i * 40u), b = polys.Load4(i * 40u + 16u);
    const uint2 c = polys.Load2(i * 40u + 32u);
    Polygon p;
    p.attr = a.x;
    p.tex = a.y;
    p.pltt = a.z;
    p.first = (int)a.w;
    p.count = (int)b.x;
    p.ymin = asfloat(b.y);
    p.ymax = asfloat(b.z);
    p.translucent = (int)b.w;
    p.flags = (int)c.x;
    p.replacement = (int)c.y;
    return p;
}

Vertex vertex_at(int i)
{
    const uint at = (uint)i * 40u;
    const float4 a = asfloat(verts.Load4(at)), b = asfloat(verts.Load4(at + 16u));
    Vertex v;
    v.x = a.x;
    v.y = a.y;
    v.z = a.z;
    v.w = a.w;
    v.r = b.x;
    v.g = b.y;
    v.b = b.z;
    v.s = b.w;
    const uint2 c = verts.Load2(at + 32u);
    v.t = asfloat(c.x);
    v.a = asfloat(c.y);
    return v;
}

// ---- VRAM (texdecode.h) ----

uint tex8(uint address)
{
    address &= 0x7ffffu;
    return (vram.Load(address & ~3u) >> ((address & 3u) * 8u)) & 0xffu;
}

uint tex16(uint address)
{
    return tex8(address) | (tex8(address + 1u) << 8);
}

// A palette address is always even: both bytes are in one word.
uint pal16(uint address)
{
    address = TEX_PAL + address % 0x18000u;
    return (vram.Load(address & ~3u) >> ((address & 2u) * 8u)) & 0xffffu;
}

int wrap(int c, int size, uint repeat, uint flip)
{
    if (repeat == 0u) {
        return c < 0 ? 0 : c >= size ? size - 1 : c;
    }
    if (flip != 0u) {
        const int period = c & (2 * size - 1);
        return period >= size ? 2 * size - 1 - period : period;
    }
    return c & (size - 1);
}

// A texel: RGB 5 bits each in bits 0-14, alpha 0-31 in bits 16-20.
uint texel(uint param, uint pltt, int s, int t)
{
    const uint base = (param & 0xffffu) * 8u;
    const int ss = 8 << ((param >> 20) & 7u), ts = 8 << ((param >> 23) & 7u);
    const uint format = (param >> 26) & 7u;
    const uint zero_transparent = (param >> 29) & 1u;
    const uint pal = pltt * (format == 2u ? 8u : 16u);
    uint index;
    s = wrap(s, ss, (param >> 16) & 1u, (param >> 18) & 1u);
    t = wrap(t, ts, (param >> 17) & 1u, (param >> 19) & 1u);
    switch (format) {
    case 1u: {
        const uint v = tex8(base + (uint)(t * ss + s));
        const uint a3 = v >> 5;
        return (pal16(pal + (v & 31u) * 2u) & 0x7fffu) | ((a3 * 4u + a3 / 2u) << 16);
    }
    case 2u:
        index = (tex8(base + (uint)(t * ss + s) / 4u) >> (2u * ((uint)s & 3u))) & 3u;
        break;
    case 3u:
        index = (tex8(base + (uint)(t * ss + s) / 2u) >> (4u * ((uint)s & 1u))) & 15u;
        break;
    case 4u:
        index = tex8(base + (uint)(t * ss + s));
        break;
    case 5u: {
        const uint block = base + (uint)((t / 4) * (ss / 4) + s / 4) * 4u;
        const uint bits = (tex8(block + ((uint)t & 3u)) >> (2u * ((uint)s & 3u))) & 3u;
        const uint slot1 = 0x20000u + ((block & 0x1ffffu) / 2u) + ((block >= 0x40000u) ? 0x10000u : 0u);
        const uint info = tex16(slot1);
        const uint pa = pltt * 16u + (info & 0x3fffu) * 4u;
        const uint mode = info >> 14;
        if (bits == 3u && (mode == 0u || mode == 1u)) {
            return 0u;
        }
        if (bits < 2u || mode == 0u || mode == 2u) {
            return (pal16(pa + bits * 2u) & 0x7fffu) | (31u << 16);
        }
        {
            const uint c0 = pal16(pa), c1 = pal16(pa + 2u);
            uint o = 0u;
            for (uint sh = 0u; sh < 15u; sh += 5u) {
                const uint a = (c0 >> sh) & 31u, b = (c1 >> sh) & 31u;
                uint v;
                if (mode == 1u) v = (a + b) / 2u;
                else if (bits == 2u) v = (a * 5u + b * 3u) / 8u;
                else v = (a * 3u + b * 5u) / 8u;
                o |= v << sh;
            }
            return o | (31u << 16);
        }
    }
    case 6u: {
        const uint v = tex8(base + (uint)(t * ss + s));
        return (pal16(pal + (v & 7u) * 2u) & 0x7fffu) | ((v >> 3) << 16);
    }
    case 7u: {
        const uint v = tex16(base + (uint)(t * ss + s) * 2u);
        return (v & 0x7fffu) | ((v & 0x8000u) != 0u ? (31u << 16) : 0u);
    }
    default:
        return 0x7fffu | (31u << 16);
    }
    if (index == 0u && zero_transparent != 0u) {
        return 0u;
    }
    return (pal16(pal + index * 2u) & 0x7fffu) | (31u << 16);
}

// The texels polygon `p` maps: its vertices' coordinates bound what any of
// its pixels reads (an interpolation stays between them). x, y the first
// texel in s and t, z, w the last.
int4 polygon_texels(Polygon p)
{
    float s0 = 1e30f, s1 = -1e30f, t0 = 1e30f, t1 = -1e30f;
    for (int i = 0; i < p.count; ++i) {
        const Vertex v = vertex_at(p.first + i);
        s0 = min(s0, v.s);
        s1 = max(s1, v.s);
        t0 = min(t0, v.t);
        t1 = max(t1, v.t);
    }
    {
        const int a = (int)floor(s0 / 16.0f), b = (int)floor(t0 / 16.0f);
        return int4(a, b, max(a, (int)ceil(s1 / 16.0f) - 1), max(b, (int)ceil(t1 / 16.0f) - 1));
    }
}

// Whether polygon p shows its texture at its own size or smaller in screen space (an orthographic
// projection): the game's 2D drawn with the 3D engine, the HUD, the day cards, the noise over Sora's
// memories. Smoothing leaves those sharp, as the 2D layers are; a magnified texture (the title's
// logo) is still smoothed. With screen space stretched this frame, in the DS's own pixels.
bool pixel_art(Polygon p)
{
    float screen = 0.0f, texels = 0.0f;
    for (int i = 0; i < p.count; ++i) {
        const Vertex a = vertex_at(p.first + i), b = vertex_at(p.first + (i + 1) % p.count);
        if (a.w != 4096.0f) {
            return false;
        }
        screen += a.x * b.y - b.x * a.y;
        texels += (a.s * b.t - b.s * a.t) / 256.0f;
    }
    {
        const float stretch = (smooth & 2u) != 0u ? (float)width / (float)scale / 256.0f : 1.0f;
        return p.count > 0 && abs(screen) <= 1.25f * stretch * abs(texels);
    }
}

// Texture smoothing (the port's option): whether a pixel shows is the DS's (the nearest texel's alpha),
// so cut-outs keep their shape; its colour the four texels around blended by distance and alpha, only
// texels the polygon maps (`bounds`). The same format as texel.
uint smoothed_texel(uint param, uint pltt, float s, float t, int4 bounds)
{
    const uint nearest = texel(param, pltt, (int)floor(s), (int)floor(t));
    const float fs = s - 0.5f, ft = t - 0.5f;
    const int s0 = (int)floor(fs), t0 = (int)floor(ft);
    const float ds = fs - (float)s0, dt = ft - (float)t0;
    float3 color = float3(0.0f, 0.0f, 0.0f);
    float alpha = 0.0f;
    if (((nearest >> 16) & 31u) == 0u) {
        return nearest;
    }
    for (int k = 0; k < 4; ++k) {
        const int ks = k & 1, kt = k >> 1;
        const uint v = texel(param, pltt, clamp(s0 + ks, bounds.x, bounds.z), clamp(t0 + kt, bounds.y, bounds.w));
        const float weight = (ks != 0 ? ds : 1.0f - ds) * (kt != 0 ? dt : 1.0f - dt);
        const float a = (float)((v >> 16) & 31u) * weight;
        color += a * float3((float)(v & 31u), (float)((v >> 5) & 31u), (float)((v >> 10) & 31u));
        alpha += a;
    }
    if (alpha <= 0.0f) {
        return nearest;
    }
    {
        const uint3 c = (uint3)min(round(color / alpha), 31.0f);
        return c.x | (c.y << 5) | (c.z << 10) | (nearest & (31u << 16));
    }
}

// ---- Replacement textures (runtime/mods/textures.h): HD from mods ----

// A coordinate wrapped as `wrap` wraps a texel's, kept fractional: in [0, size).
float wrapf(float c, float size, uint repeat, uint flip)
{
    if (repeat == 0u) {
        return clamp(c, 0.0f, size - 0.001f);
    }
    if (flip != 0u) {
        const float period = c - floor(c / (2.0f * size)) * 2.0f * size;
        return min(period >= size ? 2.0f * size - period : period, size - 0.001f);
    }
    return c - floor(c / size) * size;
}

// One of the replacement's texels: 6-bit RGB in xyz, alpha 0-31 in w.
int4 hd_at(uint4 e, int x, int y)
{
    const uint c = hd.Load((e.x + (uint)y * e.y + (uint)x) * 4u);
    const uint cut = fine != 0u ? 0u : 2u;
    return int4((int)((c & 255u) >> cut), (int)(((c >> 8) & 255u) >> cut), (int)(((c >> 16) & 255u) >> cut),
                (int)((c >> 24) >> 3));
}

// A still picture's smaller copies follow its texels, each half the one before
// (host/render/gpu_hd.c SMALLER): level `level`'s first texel, width and height, with
// the entry's info, as an entry.
static const uint HD_SMALLER = 8u;

uint4 hd_level(uint4 e, int level)
{
    uint first = e.x, w = e.y, h = e.z;
    for (int k = 0; k < level && (w > 1u || h > 1u); ++k) {
        first += w * h;
        w = max(w >> 1, 1u);
        h = max(h >> 1, 1u);
    }
    return uint4(first, w, h, e.w);
}

// The last level: a texel.
int hd_levels(uint4 e)
{
    if ((e.w & HD_SMALLER) == 0u) {
        return 0;
    }
    return (int)firstbithigh(max(e.y, e.z));
}

// Level `l` (an entry from hd_level) of polygon `p`'s replacement at (u, v),
// its own texels: the four around it, alpha-weighted (as smoothed_texel), only
// the polygon's own texels (polygon_texels) when they lie within one period of
// the texture. Colour 0-255 or 0-63 (fine) in xyz, alpha 0-31 in w.
float4 hd_blend(Polygon p, uint4 l, float u, float v, float ss, float ts)
{
    const uint rs = (p.tex >> 16) & 1u, rt = (p.tex >> 17) & 1u, fs = (p.tex >> 18) & 1u, ft = (p.tex >> 19) & 1u;
    const float fu = u - 0.5f, fv = v - 0.5f;
    const int x0 = (int)floor(fu), y0 = (int)floor(fv);
    const float du = fu - (float)x0, dv = fv - (float)y0;
    const int4 b = polygon_texels(p);
    const bool inside = b.x >= 0 && b.y >= 0 && b.z < (int)ss && b.w < (int)ts;
    const int ulo = inside ? (int)floor((float)b.x * (float)l.y / ss) : -100000;
    const int uhi = inside ? (int)ceil((float)(b.z + 1) * (float)l.y / ss) - 1 : 100000;
    const int vlo = inside ? (int)floor((float)b.y * (float)l.z / ts) : -100000;
    const int vhi = inside ? (int)ceil((float)(b.w + 1) * (float)l.z / ts) - 1 : 100000;
    float3 color = float3(0.0f, 0.0f, 0.0f);
    float alpha = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const int kx = k & 1, ky = k >> 1;
        const int ux = clamp(x0 + kx, ulo, max(ulo, uhi)), vy = clamp(y0 + ky, vlo, max(vlo, vhi));
        const int4 c = hd_at(l, wrap(ux, (int)l.y, rs, fs), wrap(vy, (int)l.z, rt, ft));
        const float a = (float)c.w * (kx != 0 ? du : 1.0f - du) * (ky != 0 ? dv : 1.0f - dv);
        color += a * float3((float)c.x, (float)c.y, (float)c.z);
        alpha += a;
    }
    return float4(alpha > 0.0f ? color / alpha : float3(0.0f, 0.0f, 0.0f), alpha);
}

// Polygon `p`'s replacement at (s, t), the original's texels: the same place of the original's
// mapping at the replacement's resolution, nearest or smoothed; a smaller copy where a pixel spans more
// than one texel (`footprint`). Colours in 8 bits when `fine`, else cut to the DS's 6.
int4 hd_texel(Polygon p, float s, float t, float footprint)
{
    const uint r4 = (uint)p.replacement * 4u;
    const uint4 e = hd.Load4(r4 * 4u);
    const float ss = (float)(8 << ((p.tex >> 20) & 7u)), ts = (float)(8 << ((p.tex >> 23) & 7u));
    const uint rs = (p.tex >> 16) & 1u, rt = (p.tex >> 17) & 1u, fs = (p.tex >> 18) & 1u, ft = (p.tex >> 19) & 1u;
    const float ws = wrapf(s, ss, rs, fs), wt = wrapf(t, ts, rt, ft);
    // the level: log2 of the replacement's texels to a pixel
    const int last = hd_levels(e);
    const float texels = footprint * max((float)e.y / ss, (float)e.z / ts);
    const float lod = last > 0 && texels > 1.0f ? min(log2(texels), (float)last) : 0.0f;
    if ((smooth & 1u) == 0u) {
        const uint4 l = hd_level(e, (int)round(lod));
        const float u = ws * (float)l.y / ss, v = wt * (float)l.z / ts;
        return hd_at(l, clamp((int)floor(u), 0, (int)l.y - 1), clamp((int)floor(v), 0, (int)l.z - 1));
    }
    {
        const int l0 = (int)floor(lod);
        const float f = lod - (float)l0;
        const uint4 a = hd_level(e, l0);
        float4 c = hd_blend(p, a, ws * (float)a.y / ss, wt * (float)a.z / ts, ss, ts);
        if (f > 0.0f && l0 < last) {
            const uint4 b = hd_level(e, l0 + 1);
            const float4 d = hd_blend(p, b, ws * (float)b.y / ss, wt * (float)b.z / ts, ss, ts);
            const float wa = c.w * (1.0f - f), wb = d.w * f;
            c = float4(wa + wb > 0.0f ? (c.xyz * wa + d.xyz * wb) / (wa + wb) : c.xyz, wa + wb);
        }
        if (c.w <= 0.0f) {
            return int4(0, 0, 0, 0);
        }
        return int4((int3)min(round(c.xyz), fine != 0u ? 255.0f : 63.0f), (int)min(round(c.w), 31.0f));
    }
}


int expand5(int c)
{
    return c != 0 ? c * 2 + 1 : 0;
}

int expand_alpha(int a5)
{
    return a5 != 0 ? a5 * 2 + 1 : 0;
}

// A 5-bit colour channel at the pass's precision: the DS's 6 bits, or 8 when fine.
int channel5(int c)
{
    return fine != 0u ? (c << 3) | (c >> 2) : expand5(c);
}

// ---- The pixel (gpu3d.c Pixel) ----

struct Pixel {
    int r, g, b, a;
    uint depth;
    uint opaque_id, trans_id, fog, edge, stencil, translucent;
    uint mask_run; // the last polygon drawn here was a shadow mask
    uint over2d;   // the last polygon drawn here is shown over the 2D (KHDAYS_G3D_OVER_2D)
};

struct Attrs {
    float iw, r, g, b, s, t, z, a;
};

Attrs attrs_of(Vertex v)
{
    Attrs a;
    precise float iw = 1.0f / v.w;
    a.iw = iw;
    a.r = v.r * iw;
    a.g = v.g * iw;
    a.b = v.b * iw;
    a.s = v.s * iw;
    a.t = v.t * iw;
    a.z = v.z;
    a.a = v.a * iw;
    return a;
}

Attrs lerp_attrs(Attrs a, Attrs b, float f)
{
    Attrs o;
    precise float iw = a.iw + (b.iw - a.iw) * f;
    precise float r = a.r + (b.r - a.r) * f;
    precise float g = a.g + (b.g - a.g) * f;
    precise float bb = a.b + (b.b - a.b) * f;
    precise float s = a.s + (b.s - a.s) * f;
    precise float t = a.t + (b.t - a.t) * f;
    precise float z = a.z + (b.z - a.z) * f;
    precise float va = a.a + (b.a - a.a) * f;
    o.iw = iw;
    o.r = r;
    o.g = g;
    o.b = bb;
    o.s = s;
    o.t = t;
    o.z = z;
    o.a = va;
    return o;
}

// depth_of: in Z mode the clip volume's z/w over the 15-bit range, widened to
// 24 bits: floor(z * 2^23) + 16383 * 512, which is gpu3d.c's double result.
uint depth_of(float z, float w, uint wbuffer)
{
    if (wbuffer != 0u) {
        return w < 0.0f ? 0u : w > 16777215.0f ? 0xffffffu : (uint)w;
    }
    {
        const float scaled = floor(z * 8388608.0f); // exact: a power of two, then floor
        if (scaled < -8388096.0f) return 0u;
        if (scaled > 8389119.0f) return 0xffffffu;   // 0xffffff - 8388096
        return (uint)((int)scaled + 8388096);
    }
}

uint toon_entry(uint index)
{
    const uint word = toon[index >> 3][(index >> 1) & 3u];
    return (index & 1u) != 0u ? word >> 16 : word & 0xffffu;
}

// How many of the original texture's texels one pixel of the target spans
// at `at` on polygon p, its longer way: s/w, t/w and 1/w are planes over the
// screen (given by the polygon's first three corners), so their changes from
// a pixel to the next are the planes' slopes. 0 when the corners are in a line.
float texel_footprint(Polygon p, Attrs at)
{
    const float n = (float)scale;
    float2 q[3];
    float3 a[3];
    if (p.count < 3) {
        return 0.0f;
    }
    for (int i = 0; i < 3; ++i) {
        const Vertex v = vertex_at(p.first + i);
        const Attrs va = attrs_of(v);
        q[i] = float2((v.x - (float)origin_x) * n, v.y * n);
        a[i] = float3(va.s, va.t, va.iw);
    }
    {
        const float2 d1 = q[1] - q[0], d2 = q[2] - q[0];
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (abs(det) < 1e-6f || at.iw == 0.0f) {
            return 0.0f;
        }
        {
            const float3 e1 = a[1] - a[0], e2 = a[2] - a[0];
            const float3 gx = (e1 * d2.y - e2 * d1.y) / det;
            const float3 gy = (e2 * d1.x - e1 * d2.x) / det;
            // s = (s/w) / (1/w): its change is (d(s/w) - s d(1/w)) / (1/w); s in 16ths of a texel
            const float s = at.s / at.iw, t = at.t / at.iw;
            const float2 along_x = float2(gx.x - s * gx.z, gx.y - t * gx.z) / (at.iw * 16.0f);
            const float2 along_y = float2(gy.x - s * gy.z, gy.y - t * gy.z) / (at.iw * 16.0f);
            return max(length(along_x), length(along_y));
        }
    }
}

// Blend native sequence elements with the scene, before any DS alpha quantization or alpha test.
void plot_sequence(Polygon p, inout Pixel px, Attrs at, uint depth) {
    const uint program=p.pltt/4u;
    if (program>=KSI_3D_PROGRAMS || seq_word(program,KSI_OUTPUT)!=p.pltt || seq_word(program,KSI_COUNT)>KSI_MAX_QUADS) return;
    const float w=1.0f/at.iw;
    const float opacity=(float)((p.attr>>16)&31u)/31.0f*saturate(at.a*w);
    const float2 sample_at=float2(at.s,at.t)*w/128.0f*
                       float2(seq_word(program,KSI_WIDTH),seq_word(program,KSI_HEIGHT));
    const float to8=fine!=0u ? 1.0f : 255.0f/63.0f;
    float4 colour=float4(float3(px.r,px.g,px.b)*to8,(float)px.a*255.0f/31.0f);
    uint mask=0u; bool touched=false;
    for (uint q=0u; q<seq_word(program,KSI_COUNT); ++q) {
        float4 src; uint blend;
        if (!seq_sample(program,q,sample_at,mask,src,blend)) continue;
        src.a*=opacity;
        if (src.a<=0.0f || (blend!=0u && all(src.rgb<=0.0f))) continue;
        colour=seq_blend(colour,src,blend);
        touched=true;
    }
    if (!touched) return;
    const int3 rgb=(int3)round(colour.rgb/to8);
    px.r=rgb.r; px.g=rgb.g; px.b=rgb.b;
    px.a=(int)round(colour.a*31.0f/255.0f);
    if ((p.attr&0x800u)!=0u && ((uint)p.flags&0x40u)==0u) px.depth=depth;
    px.trans_id=(p.attr>>24)&63u;
    px.translucent=1u;
    px.fog&=(p.attr>>15)&1u;
    px.over2d=((uint)p.flags>>3)&1u;
}

// One pixel of polygon `p` (gpu3d.c plot).
void plot(Polygon p, inout Pixel px, Attrs at, uint edge, uint wbuffer)
{
    precise float w = 1.0f / at.iw;
    const uint depth = depth_of(at.z, w, wbuffer);
    const bool marker=((uint)p.flags&0x40u)!=0u;
    if (((uint)p.flags & 0x20u)!=0u) {
        if (marker || depth<px.depth) plot_sequence(p,px,at,depth);
        return;
    }
    const uint mode = (p.attr >> 4) & 3u;
    const uint id = (p.attr >> 24) & 63u;
    int alpha = (int)((p.attr >> 16) & 31u);
    // the vertex colour, 6 bits as the DS's (or scaled to 8 when fine)
    const float vscale = fine != 0u ? 255.0f / 63.0f : 1.0f;
    const int cmax = fine != 0u ? 255 : 63;
    precise float rf = at.r * w * vscale + 0.5f;
    precise float gf = at.g * w * vscale + 0.5f;
    precise float bf = at.b * w * vscale + 0.5f;
    int rv = (int)rf, gv = (int)gf, bv = (int)bf;
    int r, gg, b, a;
    bool pass;
    if (mode == 3u && id == 0u) {  // a run of shadow masks starts from a clear stencil
        if (px.mask_run == 0u) px.stencil = 0u;
        px.mask_run = 1u;
    } else {
        px.mask_run = 0u;
    }
    if (alpha == 0) {  // wire-frame: only the edges, solid
        if (edge == 0u) return;
        alpha = 31;
    } else if (((uint)p.flags & G3D_MOD) != 0u) {
        // a mod's vertex alpha, blended across the polygon
        alpha = (int)((float)alpha * saturate(at.a * w) + 0.5f);
        if (alpha == 0) return;
    }
    rv = clamp(rv, 0, cmax);
    gv = clamp(gv, 0, cmax);
    bv = clamp(bv, 0, cmax);

    if (marker) {
        pass = true;
    } else if ((p.attr & 0x4000u) != 0u) {
        const int diff = (int)depth - (int)px.depth;
        pass = diff >= -0x200 && diff <= 0x200;
    } else {
        pass = depth < px.depth;
    }
    if (mode == 3u) {  // shadow polygons: the game's two passes, as gpu3d.c's plot explains
        if (id == 0u) {
            if (!pass) px.stencil = 1u;
            return;
        }
        if (px.stencil == 0u || !pass) {
            return;
        }
        if (px.translucent != 0u ? px.trans_id == id : px.opaque_id == id) {
            return;
        }
    } else if (!pass) {
        return;
    }

    {
        const uint format = (p.tex >> 26) & 7u;
        int rt = cmax, gt = cmax, bt = cmax, at6 = 63, at5 = 31;
        const int unit = fine != 0u ? 256 : 64; // the modulation's divisor
        const int av6 = expand_alpha(alpha);
        const bool textured = format != 0u && (disp3dcnt & 1u) != 0u;
        if (textured) {
            precise float sf = at.s * w / 16.0f;
            precise float tf = at.t * w / 16.0f;
            if (p.replacement >= 0) {
                const int4 c = hd_texel(p, sf, tf, texel_footprint(p, at));
                rt = c.x;
                gt = c.y;
                bt = c.z;
                at5 = c.w;
            } else {
                const uint tx = (smooth & 1u) != 0u && !pixel_art(p) ? smoothed_texel(p.tex, p.pltt, sf, tf, polygon_texels(p))
                                             : texel(p.tex, p.pltt, (int)floor(sf), (int)floor(tf));
                at5 = (int)((tx >> 16) & 31u);
                rt = channel5((int)(tx & 31u));
                gt = channel5((int)((tx >> 5) & 31u));
                bt = channel5((int)((tx >> 10) & 31u));
            }
            at6 = expand_alpha(at5);
        }
        if (mode == 1u && textured) {  // decal
            if (at5 == 0) { r = rv; gg = gv; b = bv; }
            else if (at5 == 31) { r = rt; gg = gt; b = bt; }
            else if (fine != 0u) {
                r = (rt * at5 + rv * (31 - at5)) / 31;
                gg = (gt * at5 + gv * (31 - at5)) / 31;
                b = (bt * at5 + bv * (31 - at5)) / 31;
            } else {
                r = (rt * at5 + rv * (63 - at5)) / 64;
                gg = (gt * at5 + gv * (63 - at5)) / 64;
                b = (bt * at5 + bv * (63 - at5)) / 64;
            }
            a = av6;
        } else if (mode == 2u) {  // toon / highlight: the red component indexes the table
            const uint t16 = toon_entry((uint)(rv >> 1));
            const int rs = channel5((int)(t16 & 31u)), gs = channel5((int)((t16 >> 5) & 31u)),
                      bs = channel5((int)((t16 >> 10) & 31u));
            r = ((rt + 1) * (rs + 1) - 1) / unit;
            gg = ((gt + 1) * (gs + 1) - 1) / unit;
            b = ((bt + 1) * (bs + 1) - 1) / unit;
            if ((disp3dcnt & 2u) != 0u) {
                r = min(r + rs, cmax);
                gg = min(gg + gs, cmax);
                b = min(b + bs, cmax);
            }
            a = ((at6 + 1) * (av6 + 1) - 1) / 64;
        } else {  // modulation (and shadow)
            r = ((rt + 1) * (rv + 1) - 1) / unit;
            gg = ((gt + 1) * (gv + 1) - 1) / unit;
            b = ((bt + 1) * (bv + 1) - 1) / unit;
            a = ((at6 + 1) * (av6 + 1) - 1) / 64;
        }
        a >>= 1;  // back to 5 bits
    }
    // alpha test
    if ((disp3dcnt & 4u) != 0u ? a <= (int)(alpha_ref & 31u) : a == 0) {
        return;
    }
    if (a < 31 || mode == 3u) {
        // no double blending within one translucent polygon ID -- the DS's
        // rule, not a mod's: its translucent pieces (glows, glass, mist) take
        // the game material's ID, and overlapping ones all blend, as in the
        // tool that made them
        if (px.translucent != 0u && px.trans_id == id && mode != 3u && ((uint)p.flags & G3D_MOD) == 0u) {
            return;
        }
        if ((disp3dcnt & 8u) != 0u && px.a > 0) {
            px.r = (r * (a + 1) + px.r * (31 - a)) / 32;
            px.g = (gg * (a + 1) + px.g * (31 - a)) / 32;
            px.b = (b * (a + 1) + px.b * (31 - a)) / 32;
            if (a > px.a) px.a = a;
        } else {
            px.r = r;
            px.g = gg;
            px.b = b;
            px.a = a;
        }
        if ((p.attr & 0x800u) != 0u && !marker) {
            px.depth = depth;
        }
        px.trans_id = id;
        px.translucent = 1u;
        px.fog &= (p.attr >> 15) & 1u;
        px.over2d = ((uint)p.flags >> 3) & 1u;
    } else {
        px.r = r;
        px.g = gg;
        px.b = b;
        px.a = 31;
        if (!marker) px.depth = depth;
        px.opaque_id = id;
        px.translucent = 0u;
        px.fog = (p.attr >> 15) & 1u;
        px.edge = edge;
        px.over2d = ((uint)p.flags >> 3) & 1u;
    }
}

// Whether polygon `p` covers pixel (x, y) of the target, gpu3d.c's
// draw_polygon on the target's grid (the vertices scaled by N), and if so
// its attributes there and whether it is an edge pixel. Each polygon covers
// what it covers at N x, no more (a quarter of a DS pixel more on each side
// bloated every model, a mod's many small triangles most).
bool cover(Polygon p, int x, int y, out Attrs pa, out uint edge)
{
    const float n = (float)scale;
    const int rows = (int)height, columns = (int)width;
    precise float ymin = p.ymin * n;
    precise float ymax = p.ymax * n;
    int y0 = (int)ceil(ymin - 0.5f);
    int y1 = (int)ceil(ymax - 0.5f) - 1;
    float xl = 1e9f, xr = -1e9f;
    Attrs al = (Attrs)0, ar = (Attrs)0;
    bool found = false;
    pa = (Attrs)0;
    edge = 0u;
    if (p.count < 1 || p.first < 0 || (uint)p.first + (uint)p.count > nverts) {
        return false;  // the host leaves such a polygon out (gpu_raster3d.c unusable): never read past the list
    }
    if (y1 < y0) {  // flatter than a pixel row: one row
        y0 = y1 = (int)floor(ymin);
    }
    if (y0 < 0) y0 = 0;
    if (y1 > rows - 1) y1 = rows - 1;
    if (y < y0 || y > y1) {
        return false;
    }
    {
        precise float cy = (float)y + 0.5f;
        for (int i = 0; i < p.count; ++i) {
            const Vertex va = vertex_at(p.first + i);
            const Vertex vb = vertex_at(p.first + (i + 1) % p.count);
            precise float ay = va.y * n;
            precise float by = vb.y * n;
            precise float ax = (va.x - (float)origin_x) * n;
            precise float bx = (vb.x - (float)origin_x) * n;
            const float lo = min(ay, by), hi = max(ay, by);
            if (hi == lo) {
                if (y0 != y1 || (int)floor(lo) != y) continue;
                // a horizontal edge on a single-row polygon: both ends
                if (ax < xl) { xl = ax; al = attrs_of(va); }
                if (ax > xr) { xr = ax; ar = attrs_of(va); }
                if (bx < xl) { xl = bx; al = attrs_of(vb); }
                if (bx > xr) { xr = bx; ar = attrs_of(vb); }
                found = true;
                continue;
            }
            if (cy < lo || cy > hi) continue;
            {
                precise float f = (cy - ay) / (by - ay);
                precise float ex = ax + (bx - ax) * f;
                const Attrs e = lerp_attrs(attrs_of(va), attrs_of(vb), f);
                if (ex < xl) { xl = ex; al = e; }
                if (ex > xr) { xr = ex; ar = e; }
                found = true;
            }
        }
    }
    if (!found) {
        return false;
    }
    {
        // the pixels covered
        const int x0 = (int)ceil(xl - 0.5f);
        int x1 = (int)ceil(xr - 0.5f) - 1;
        precise float span = xr - xl;
        if (x1 < x0) x1 = x0;  // at least a pixel: lines and dots
        if (x < x0 || x > x1 || x >= columns) {
            return false;
        }
        {
            precise float f = span > 0.0f ? (((float)x + 0.5f) - xl) / span : 0.0f;
            pa = lerp_attrs(al, ar, f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f);
        }
        edge = (x == x0 || x == x1 || y == y0 || y == y1) ? 1u : 0u;
    }
    return true;
}

// The rear plane (gpu3d.c rear_plane): the clear colour, depth and polygon
// ID, or the clear image from texture slots 2 and 3, nearest at N x.
Pixel rear_plane(int x, int y)
{
    Pixel px;
    const uint cd = clear_depth & 0x7fffu;
    px.opaque_id = (clear_color >> 24) & 63u;
    px.trans_id = 0xffu;
    px.edge = 0u;
    px.stencil = 0u;
    px.translucent = 0u;
    px.mask_run = 0u;
    px.over2d = 0u;
    if ((disp3dcnt & 0x4000u) != 0u) {
        const uint offset = clear_depth >> 16;
        const int sx = x / (int)scale + origin_x;
        const uint sy = (uint)y / scale;
        const uint i = ((((sy + (offset >> 8)) & 255u) * 256u + ((uint)(sx + (int)offset) & 255u)) * 2u);
        const uint c = tex16(0x40000u + i), d = tex16(0x60000u + i);
        const uint dd = d & 0x7fffu;
        px.r = channel5((int)(c & 31u));
        px.g = channel5((int)((c >> 5) & 31u));
        px.b = channel5((int)((c >> 10) & 31u));
        px.a = (c & 0x8000u) != 0u ? 31 : 0;
        px.depth = dd * 0x200u + ((dd + 1u) / 0x8000u) * 0x1ffu;
        px.fog = d >> 15;
        return px;
    }
    px.r = channel5((int)(clear_color & 31u));
    px.g = channel5((int)((clear_color >> 5) & 31u));
    px.b = channel5((int)((clear_color >> 10) & 31u));
    px.a = (int)((clear_color >> 16) & 31u);
    px.depth = cd * 0x200u + ((cd + 1u) / 0x8000u) * 0x1ffu;
    px.fog = (clear_color >> 15) & 1u;
    return px;
}

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    const int x = (int)id.x, y = (int)id.y;
    const uint wbuffer = (swap_params >> 1) & 1u;
    Pixel px;
    uint tile, first, last;
    if (id.x >= width || id.y >= height) {
        return;
    }
    px = rear_plane(x, y);
    tile = min(id.y / scale / 8u, tiles_y - 1u) * tiles_x + min(id.x / scale / 8u, tiles_x - 1u);
    first = tiles.Load(tile * 4u);
    last = min(tiles.Load((tile + 1u) * 4u), entries);
    bool marker_started=false;
    for (uint k = first; k < last; ++k) {
        const uint index = tiles.Load(k * 4u);
        if (index >= npolys) {
            continue;
        }
        const Polygon p = polygon_at(index);
        Attrs at;
        uint edge;
        if (cover(p, x, y, at, edge)) {
            if (((uint)p.flags&0x40u)!=0u && !marker_started) {
                px.trans_id=0xffu; // The first marker must not share a translucent ID with the world.
                marker_started=true;
            }
            plot(p, px, at, edge, wbuffer);
        }
    }
    out_color[id.xy] = (uint)px.r | ((uint)px.g << 8) | ((uint)px.b << 16) | ((uint)px.a << 24) |
                       (px.over2d << 29) | (px.translucent << 31);
    out_attr[id.xy] = px.depth | (px.opaque_id << 24) | (px.edge << 30) | (px.fog << 31);
}
