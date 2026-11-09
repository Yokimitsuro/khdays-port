// The 3D list's polygons and their colour as the 3D renderers draw them (raster3d.hlsl, native3d.hlsl):
// gfx/g3d.h's polygons and vertices, the replacement textures, gpu3d.c plot's colour. After
// g3d_vram.hlsli; the includer declares `polys`, `verts` (read by their bytes), `hd` (the replacements,
// host/render/gpu_hd.h: 4 words each, then their texels, RGBA8, red low) and, in its constant buffer,
// smooth, width and scale besides g3d_vram.hlsli's.

#include "g3d_list.hlsli"

static const uint G3D_MOD = 4u; // gfx/g3d.h KHDAYS_G3D_MOD: a mod's model's polygon
// gfx/g3d.h KHDAYS_G3D_EFFECT*: a KINGDOM HEARTS II effect's triangle, blended as the GS's ALPHA byte in its pltt
static const uint G3D_EFFECT = 0x1000000u;
static const uint G3D_EFFECT_DEPTH_WRITE = 0x2000000u;
static const uint G3D_EFFECT_DEPTH_ONLY = 0x4000000u;
static const uint G3D_EFFECT_NO_DEPTH_TEST = 0x8000000u;

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
// the polygon's own texels (`b`: polygon_texels) when they lie within one period of
// the texture. Colour 0-255 or 0-63 (fine) in xyz, alpha 0-31 in w.
float4 hd_blend(Polygon p, uint4 l, float u, float v, float ss, float ts, int4 b)
{
    const uint rs = (p.tex >> 16) & 1u, rt = (p.tex >> 17) & 1u, fs = (p.tex >> 18) & 1u, ft = (p.tex >> 19) & 1u;
    const float fu = u - 0.5f, fv = v - 0.5f;
    const int x0 = (int)floor(fu), y0 = (int)floor(fv);
    const float du = fu - (float)x0, dv = fv - (float)y0;
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
int4 hd_texel(Polygon p, float s, float t, float footprint, int4 bounds)
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
        float4 c = hd_blend(p, a, ws * (float)a.y / ss, wt * (float)a.z / ts, ss, ts, bounds);
        if (f > 0.0f && l0 < last) {
            const uint4 b = hd_level(e, l0 + 1);
            const float4 d = hd_blend(p, b, ws * (float)b.y / ss, wt * (float)b.z / ts, ss, ts, bounds);
            const float wa = c.w * (1.0f - f), wb = d.w * f;
            c = float4(wa + wb > 0.0f ? (c.xyz * wa + d.xyz * wb) / (wa + wb) : c.xyz, wa + wb);
        }
        if (c.w <= 0.0f) {
            return int4(0, 0, 0, 0);
        }
        return int4((int3)min(round(c.xyz), fine != 0u ? 255.0f : 63.0f), (int)min(round(c.w), 31.0f));
    }
}

// The polygon's alpha at a pixel (gpu3d.c plot): wire-frame polygons (alpha 0) solid on their edges only,
// a mod's model's vertex alpha (`vertex_alpha`, the vertices' there) blended across the polygon. 0: the
// pixel is not drawn.
int polygon_alpha(Polygon p, float vertex_alpha, uint edge)
{
    const int alpha = (int)((p.attr >> 16) & 31u);
    if (alpha == 0) {
        return edge != 0u ? 31 : 0;
    }
    if (((uint)p.flags & G3D_MOD) != 0u) {
        return (int)((float)alpha * saturate(vertex_alpha) + 0.5f);
    }
    return alpha;
}

// The colour polygon `p` gives a pixel, before the alpha test (gpu3d.c plot): its vertex colour there
// (`shade`, 0-63 a channel) and texel at (s, t) (texels) by the polygon's mode. `footprint`: the texels a
// pixel spans (a replacement's smaller copies); `art`: pixel_art(p), `texels`: polygon_texels(p), needed
// only with smoothing. RGB at the pass's precision, alpha 0-31.
int4 polygon_color(Polygon p, int alpha, float3 shade, float s, float t, float footprint, bool art, int4 texels)
{
    const uint mode = (p.attr >> 4) & 3u;
    // the vertex colour, 6 bits as the DS's (or scaled to 8 when fine)
    const float vscale = fine != 0u ? 255.0f / 63.0f : 1.0f;
    const int cmax = fine != 0u ? 255 : 63;
    precise float rf = shade.x * vscale + 0.5f;
    precise float gf = shade.y * vscale + 0.5f;
    precise float bf = shade.z * vscale + 0.5f;
    const int rv = clamp((int)rf, 0, cmax), gv = clamp((int)gf, 0, cmax), bv = clamp((int)bf, 0, cmax);
    int r, gg, b, a;
    {
        const uint format = (p.tex >> 26) & 7u;
        int rt = cmax, gt = cmax, bt = cmax, at6 = 63, at5 = 31;
        const int unit = fine != 0u ? 256 : 64; // the modulation's divisor
        const int av6 = expand_alpha(alpha);
        const bool textured = format != 0u && (disp3dcnt & 1u) != 0u;
        if (textured) {
            if (p.replacement >= 0) {
                const int4 c = hd_texel(p, s, t, footprint, texels);
                rt = c.x;
                gt = c.y;
                bt = c.z;
                at5 = c.w;
            } else {
                const uint tx = (smooth & 1u) != 0u && !art ? smoothed_texel(p.tex, p.pltt, s, t, texels)
                                                            : texel(p.tex, p.pltt, (int)floor(s), (int)floor(t));
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
    return int4(r, gg, b, a);
}

// A KH2 effect's pixel before its blend (the GS's MODULATE): its picture's texel (a replacement) times the vertex
// colour as a factor (`shade`: 63 the GS's 0x80, 1, up to twice that), its alpha the texel's times the vertex's.
// Without a picture (TME off) the vertex colour is the colour itself, 0x80 of 0xff: half the factor's. Colour and
// alpha 0..1.
float4 effect_color(Polygon p, float3 shade, float valpha, float s, float t, float footprint, int4 texels)
{
    float3 rgb = float3(128.0f, 128.0f, 128.0f) / 255.0f;
    float a = 1.0f;
    if (((p.tex >> 26) & 7u) != 0u && p.replacement >= 0) {
        const int4 c = hd_texel(p, s, t, footprint, texels);
        rgb = (float3)c.xyz / (fine != 0u ? 255.0f : 63.0f);
        a = (float)c.w / 31.0f;
    }
    return float4(saturate(rgb * shade / 63.0f), saturate(a * valpha));
}

// The GS's blend of an effect's pixel (`alpha` its ALPHA byte): (A - B) * C + D, A, B and D the source colour,
// the colour there or 0, C the source alpha, the colour there's alpha or FIX (0 here: no effect sets it).
float3 gs_blend(uint alpha, float3 cs, float as_, float3 cd, float ad)
{
    const uint A = alpha & 3u, B = (alpha >> 2) & 3u, C = (alpha >> 4) & 3u, D = (alpha >> 6) & 3u;
    const float3 none = float3(0.0f, 0.0f, 0.0f);
    const float3 a = A == 0u ? cs : (A == 1u ? cd : none);
    const float3 b = B == 0u ? cs : (B == 1u ? cd : none);
    const float3 d = D == 0u ? cs : (D == 1u ? cd : none);
    const float c = C == 0u ? as_ : (C == 1u ? ad : 0.0f);
    return saturate((a - b) * c + d);
}
