// The DS's 3D rendering engine at N times its resolution: port/gpu/gpu3d.c's rasterizer run for each
// pixel, one thread a pixel going through the polygons that may cover it in drawing order, so its
// per-pixel rules are exactly gpu3d.c's (docs/RENDERING.md, "On the graphics card"). In widescreen
// `origin_x` is the DS column of the target's first (negative).

static const uint G3D_CLIP = 0x20000000u;       // KHDAYS_G3D_CLIP: a clip volume's, first in the list, not drawn
static const uint G3D_CLIP_OWNER = 0x40000000u; // KHDAYS_G3D_CLIP_OWNER: a clip volume's model's, drawn inside
static const uint G3D_UNCLIPPED = 0x61u | G3D_CLIP_OWNER; // HUD, sequence, marker and owners: never left out

// Every storage buffer is read by its bytes: SDL's Direct3D 12 views them raw (no stride), and AMD's
// cards (vkd3d too) read a StructuredBuffer by the view's stride, so all its elements were the first.
ByteAddressBuffer polys : register(t0, space0);
ByteAddressBuffer verts : register(t1, space0);
ByteAddressBuffer tiles : register(t2, space0);   // tiles_x*tiles_y+1 starts, then polygon indices
ByteAddressBuffer vram : register(t3, space0);         // the texture slots (512 KB), then the palette slots (96 KB)
ByteAddressBuffer hd : register(t4, space0); // the replacements (host/render/gpu_hd.h): 4 words each, then their texels (RGBA8, red low)
ByteAddressBuffer sequences : register(t5, space0); // retained native sequences in this 3D list
ByteAddressBuffer shadow_mask : register(t6, space0); // declared in register order: MSL numbers them as declared
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
    uint resume;      // 1: start from what the targets hold (resumed), not the rear plane
    uint unused0, unused1, unused2;
    uint4 toon[4];    // TOON_TABLE, two entries per uint
};

#include "g3d_vram.hlsli"
#include "g3d_polygon.hlsli"

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

// One pixel of a KH2 effect's polygon: its depth test and writes, its colour blended as its GS byte says.
void plot_effect(Polygon p, inout Pixel px, Attrs at, uint depth)
{
    const uint flags = (uint)p.flags;
    if ((flags & G3D_EFFECT_NO_DEPTH_TEST) == 0u && depth >= px.depth) {
        return; // nearer only (ZTST GREATER)
    }
    if ((flags & G3D_EFFECT_DEPTH_ONLY) == 0u) {
        const float w = 1.0f / at.iw;
        const bool textured = ((p.tex >> 26) & 7u) != 0u && p.replacement >= 0;
        const int4 texels = (smooth & 1u) != 0u && textured ? polygon_texels(p) : int4(0, 0, 0, 0);
        const float4 c = effect_color(p, float3(at.r, at.g, at.b) * w, at.a * w, at.s * w / 16.0f, at.t * w / 16.0f,
                                      textured ? texel_footprint(p, at) : 0.0f, texels);
        if (c.a <= 0.0f) {
            return; // ATST GREATER, AREF 0
        }
        const float cmax = fine != 0u ? 255.0f : 63.0f;
        const float3 cd = float3((float)px.r, (float)px.g, (float)px.b) / cmax;
        const int3 rgb = (int3)round(gs_blend(p.pltt & 0xffu, c.rgb, c.a, cd, (float)px.a / 31.0f) * cmax);
        px.r = rgb.r;
        px.g = rgb.g;
        px.b = rgb.b;
        px.over2d = (flags >> 3) & 1u;
    }
    if ((flags & G3D_EFFECT_DEPTH_WRITE) != 0u) {
        px.depth = depth;
    }
}

// One pixel of polygon `p` (gpu3d.c plot).
void plot(Polygon p, inout Pixel px, Attrs at, uint edge, uint wbuffer)
{
    precise float w = 1.0f / at.iw;
    const uint depth = depth_of(at.z, w, wbuffer);
    const bool marker=((uint)p.flags&0x40u)!=0u;
    if (((uint)p.flags & G3D_EFFECT) != 0u) {
        plot_effect(p, px, at, depth);
        return;
    }
    if (((uint)p.flags & 0x20u)!=0u) {
        if (marker || depth<px.depth) plot_sequence(p,px,at,depth);
        return;
    }
    if (((uint)p.flags & 0x10000u) != 0u) {
        const int diff = (int)depth - (int)px.depth;
        const bool sprite = ((uint)p.flags & 0x100u) != 0u;
        const uint receiver = sprite ? (((uint)p.flags >> 18)&63u) : p.pltt;
        if (abs(diff) > 0x200 || px.trans_id == 63u || px.opaque_id != receiver || px.translucent != 0u) return;
        const float2 uv = float2(at.s, at.t)*w/16.0f-0.5f;
        const int2 q = (int2)floor(uv);
        const float2 f = frac(uv);
        const uint base = p.shadow*128u*128u;
        float coverage = 0;
        for (int k=0;k<4;++k) {
            const int2 a=q+int2(k&1,k>>1);
            if (any(a<0) || any(a>=128)) continue;
            coverage += (float)shadow_mask.Load((base+(uint)a.y*128u+(uint)a.x)*4u)*
                        ((k&1)!=0?f.x:1-f.x)*((k>>1)!=0?f.y:1-f.y)/255.0f;
        }
        float alpha=coverage*(float)((p.attr>>16)&31u)/31.0f;
        const float vscale=fine!=0u?255.0f/63.0f:1.0f;
        float3 shade=float3(at.r,at.g,at.b)*w*vscale;
        if (sprite && ((p.tex >> 26)&7u)!=0u) {
            int4 tint;
            if (p.replacement>=0) {
                const uint4 e=hd.Load4((uint)p.replacement*16u);
                tint=hd_at(e,(int)(e.y/2u),(int)(e.z/2u));
            } else {
                const int ss=8<<((p.tex>>20)&7u), ts=8<<((p.tex>>23)&7u);
                const uint tex=texel(p.tex,p.pltt,ss/2,ts/2);
                tint=int4(channel5((int)(tex&31u)),channel5((int)((tex>>5)&31u)),
                          channel5((int)((tex>>10)&31u)),(int)((tex>>16)&31u));
            }
            alpha*=(float)tint.a/31.0f;
            shade=floor(((shade+1.0f)*(float3(tint.rgb)+1.0f)-1.0f)/(fine!=0u?256.0f:64.0f));
        }
        if (alpha<=0.0f) return;
        const int3 color=(int3)round(lerp(float3(px.r,px.g,px.b),shade,alpha));
        px.r=color.r; px.g=color.g; px.b=color.b;
        px.trans_id=63u;
        return;
    }
    const uint mode = (p.attr >> 4) & 3u;
    const uint id = (p.attr >> 24) & 63u;
    int r, gg, b, a, alpha;
    bool pass;
    if (mode == 3u && id == 0u) {  // a run of shadow masks starts from a clear stencil
        if (px.mask_run == 0u) px.stencil = 0u;
        px.mask_run = 1u;
    } else {
        px.mask_run = 0u;
    }
    alpha = polygon_alpha(p, at.a * w, edge);
    if (alpha == 0) return;

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
        const bool textured = ((p.tex >> 26) & 7u) != 0u && (disp3dcnt & 1u) != 0u;
        const bool smoothing = (smooth & 1u) != 0u && textured;
        const bool art = smoothing && p.replacement < 0 ? pixel_art(p) : true;
        const int4 texels = smoothing && (p.replacement >= 0 || !art) ? polygon_texels(p) : int4(0, 0, 0, 0);
        precise float3 shade = float3(at.r, at.g, at.b) * w;
        precise float sf = at.s * w / 16.0f;
        precise float tf = at.t * w / 16.0f;
        const int4 c = polygon_color(p, alpha, shade, sf, tf,
                                     textured && p.replacement >= 0 ? texel_footprint(p, at) : 0.0f, art, texels);
        r = c.x;
        gg = c.y;
        b = c.z;
        a = c.w;
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

// Whether polygon `p` covers pixel (x, y) of the target, gpu3d.c's draw_polygon on the target's
// grid (the vertices scaled by N), and if so its attributes there and whether it is an edge pixel.
// It covers what it covers at N x, no more (more bloats a mod's many small triangles). `grow` target
// pixels more on each side: a clip volume's, whose triangles' shared edge can leave a pixel to neither,
// and the volume then lets the room through there.
bool cover(Polygon p, int x, int y, out Attrs pa, out uint edge, float grow)
{
    const float n = (float)scale;
    const int rows = (int)height, columns = (int)width;
    precise float ymin = p.ymin * n;
    precise float ymax = p.ymax * n;
    int y0 = (int)ceil(ymin - 0.5f - grow);
    int y1 = (int)ceil(ymax - 0.5f + grow) - 1;
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
        if (grow > 0.0f) {
            cy = clamp(cy, ymin, ymax); // a grown row: the polygon's nearest
        }
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
        const int x0 = (int)ceil(xl - 0.5f - grow);
        int x1 = (int)ceil(xr - 0.5f + grow) - 1;
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

// The rear plane (gpu3d.c rear_plane): the clear colour, depth and polygon ID, or the clear image from
// texture slots 2 and 3, nearest at N x.
Pixel rear_plane(int x, int y)
{
    const RearPlane r = rear_plane_at(x / (int)scale + origin_x, (uint)y / scale);
    Pixel px;
    px.r = r.color.x;
    px.g = r.color.y;
    px.b = r.color.z;
    px.a = r.color.w;
    px.depth = r.depth;
    px.fog = r.fog;
    px.opaque_id = (clear_color >> 24) & 63u;
    px.trans_id = 0xffu;
    px.edge = 0u;
    px.stencil = 0u;
    px.translucent = 0u;
    px.mask_run = 0u;
    px.over2d = 0u;
    return px;
}

// A pixel as an earlier pass left it in the targets (`resume`: the native renderer drew the list's
// polygons before this pass's): what the targets keep of it, no translucent ID nor shadow mask.
Pixel resumed(uint2 at)
{
    const uint c = out_color[at], att = out_attr[at];
    const uint cmask = fine != 0u ? 255u : 63u;
    Pixel px;
    px.r = (int)(c & cmask);
    px.g = (int)((c >> 8) & cmask);
    px.b = (int)((c >> 16) & cmask);
    px.a = (int)((c >> 24) & 31u);
    px.over2d = (c >> 29) & 1u;
    px.translucent = c >> 31;
    px.depth = att & 0xffffffu;
    px.opaque_id = (att >> 24) & 63u;
    px.edge = (att >> 30) & 1u;
    px.fog = att >> 31;
    px.trans_id = 0xffu;
    px.stencil = 0u;
    px.mask_run = 0u;
    return px;
}

// A clip volume showing here: the coat's inside (black, as N_EX830's lining), not what lies behind it (the rear
// plane, the room seen through a crack in its floor). The depth stays the rear plane's, for the coat to draw on
void lay_clip_inside(inout Pixel px, uint clips, uint clip_near)
{
    if (clips >= 2u && px.depth > clip_near) {
        px.r = 0;
        px.g = 0;
        px.b = 0;
        px.a = 31;
        px.fog = 1u; // what blends on it takes this down, cs_main the owner's at the end
    }
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
    if (resume != 0u) {
        px = resumed(id.xy);
    } else {
        px = rear_plane(x, y);
    }
    tile = min(id.y / scale / 8u, tiles_y - 1u) * tiles_x + min(id.x / scale / 8u, tiles_x - 1u);
    first = tiles.Load(tile * 4u);
    last = min(tiles.Load((tile + 1u) * 4u), entries);
    bool marker_started=false;
    // the clip volumes this pixel shows (KHDAYS_G3D_CLIP, docs/RENDERING.md): their nearest depth, how many of
    // their triangles cover it, whether the coat's inside is laid yet and whether a surface has been drawn on it,
    // with the last owner's polygon ID and fog
    uint clip_near = 0xffffffffu, clips = 0u, owner_id = px.opaque_id, owner_fog = px.fog;
    bool inside_laid = false, surface = false;
    for (uint k = first; k < last; ++k) {
        const uint index = tiles.Load(k * 4u);
        if (index >= npolys) {
            continue;
        }
        const Polygon p = polygon_at(index);
        Attrs at;
        uint edge;
        // the volumes come first: past them, the inside goes under all that is drawn, so an effect blends over it
        if (((uint)p.flags & G3D_CLIP) == 0u && !inside_laid) {
            inside_laid = true;
            lay_clip_inside(px, clips, clip_near);
        }
        if (cover(p, x, y, at, edge, ((uint)p.flags & G3D_CLIP) != 0u ? 1.0f : 0.0f)) {
            if (((uint)p.flags & G3D_CLIP) != 0u) {
                if (((uint)p.flags & 1u) == 0u) {
                    clip_near = min(clip_near, depth_of(at.z, 1.0f / at.iw, wbuffer));
                    ++clips;
                }
                continue;
            }
            const bool unclipped = ((uint)p.flags & G3D_UNCLIPPED) != 0u;
            if (clips >= 2u && !unclipped && depth_of(at.z, 1.0f / at.iw, wbuffer) > clip_near) {
                continue; // inside a clip volume or behind it (its far wall, the coat's inside, covers it)
            }
            if (((uint)p.flags&0x40u)!=0u && !marker_started) {
                px.trans_id=0xffu; // The first marker must not share a translucent ID with the world.
                marker_started=true;
            }
            {
                const uint was_depth = px.depth;
                const int3 was = int3(px.r, px.g, px.b);
                plot(p, px, at, edge, wbuffer);
                surface = surface || px.depth != was_depth;
                if (((uint)p.flags & G3D_CLIP_OWNER) != 0u &&
                    (px.depth != was_depth || any(int3(px.r, px.g, px.b) != was))) {
                    owner_id = (p.attr >> 24) & 63u;
                    owner_fog = (p.attr >> 15) & 1u;
                }
            }
        }
    }
    if (!inside_laid) {
        lay_clip_inside(px, clips, clip_near);
    }
    // no surface drawn on the inside (blending writes none): it is the surface here, at the volume's near depth
    if (clips >= 2u && !surface && px.depth > clip_near) {
        px.depth = clip_near;
        px.opaque_id = owner_id;
        px.fog &= owner_fog;
    }
    out_color[id.xy] = (uint)px.r | ((uint)px.g << 8) | ((uint)px.b << 16) | ((uint)px.a << 24) |
                       (px.over2d << 29) | (px.translucent << 31);
    out_attr[id.xy] = px.depth | (px.opaque_id << 24) | (px.edge << 30) | (px.fog << 31);
}
