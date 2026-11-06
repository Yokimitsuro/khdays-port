// The 3D list drawn by the graphics card's own rasterizer (docs/RENDERING.md, "The native 3D"): each
// polygon a fan of triangles, its pixels coloured by gpu3d.c's rules (g3d_polygon.hlsli) and kept in six
// targets that native3d_pack.hlsl turns into raster3d.hlsl's two:
//   0 RGBA8     the colour, each translucent pixel blended over what was there (rgb), and how much of the
//               first translucent colour drawn over an empty pixel still shows (a): the DS gives an empty
//               pixel (alpha 0) that colour whole, which the pack adds back from target 1
//   1 RGBA8     that first colour (rgb), and whether there is one (a)
//   2 RGBA8     the fog bit (r), where the pixel takes it (a)
//   3 RGBA8     the opaque polygon ID / 255 (r) and the edge bit (g), where an opaque pixel is drawn (a)
//   4 RGBA8     translucent (r), shown over the 2D (g), the alpha / 31 (a: the largest drawn)
//   5 R32_UINT  the depth, 24 bits, where the depth buffer takes it
// A picture writes 1, 2, 3 and 5 only when what finishes it reads them (gpu_native3d.c OUT_*). The stencil
// holds the ID of the last polygon drawn (bits 0-5), whether it was translucent (6) and the shadow masks'
// bit (7). The host draws the polygons in the list's order (gpu_native3d.c).

#if defined(STAGE_VS)
ByteAddressBuffer polys : register(t0, space0);
ByteAddressBuffer verts : register(t1, space0);
ByteAddressBuffer vram : register(t2, space0);
ByteAddressBuffer hd : register(t3, space0);
#else
ByteAddressBuffer polys : register(t0, space2);
ByteAddressBuffer verts : register(t1, space2);
ByteAddressBuffer vram : register(t2, space2); // the texture slots (512 KB), then the palette slots (96 KB)
ByteAddressBuffer hd : register(t3, space2);   // the replacements (host/render/gpu_hd.h)
#endif

#include "native3d_params.hlsli"
#include "g3d_vram.hlsli"
#include "g3d_polygon.hlsli"

struct Corner {
    float4 position : SV_Position;
    float4 shade : COLOR0;                     // the vertex colour (0-63 a channel), a mod's vertex alpha
    float2 st : TEXCOORD0;                     // the texture coordinates, 16ths of a texel
    float w : TEXCOORD1;                       // the DS's w, perspective-correct as its interpolation
    noperspective float z : TEXCOORD2;         // z over w, linear over the screen
    nointerpolation uint polygon : TEXCOORD3;  // its index in the list, which of its pixels drawn (bits 30-31)
    nointerpolation uint art : TEXCOORD4;      // pixel_art(p): smoothing leaves it sharp
    nointerpolation int4 texels : TEXCOORD5;   // polygon_texels(p)
};

#if defined(STAGE_VS)
// A triangle's corner (`c`: a vertex, its polygon with which of its pixels the draw takes: 0 all, 1 the
// opaque ones, 2 the translucent ones). The DS's coordinates are the target's at N x, from
// column origin_x: the clip space's, w the DS's own, so the card interpolates colours and coordinates as
// the DS does.
Corner vs_main(uint2 c : TEXCOORD0)
{
    const Vertex v = vertex_at((int)c.x);
    const Polygon p = polygon_at(c.y & 0x3fffffffu);
    const bool smoothing = (smooth & 1u) != 0u && ((p.tex >> 26) & 7u) != 0u;
    Corner o;
    // the depth as the DS's Z buffer has it, over 2^24: z * 2^23 + 0x7ffe00 (depth_of), for native3d_z.hlsl
    const float depth = clamp(v.z * 0.5f + 8388096.0f / 16777216.0f, 0.0f, 1.0f);
    o.position = float4((v.x * to_clip.x + to_clip.y) * v.w, (v.y * to_clip.z + to_clip.w) * v.w, depth * v.w, v.w);
    o.shade = float4(v.r, v.g, v.b, v.a);
    o.st = float2(v.s, v.t);
    o.w = v.w;
    o.z = v.z;
    o.polygon = c.y;
    o.art = smoothing && p.replacement < 0 && pixel_art(p) ? 1u : 0u;
    o.texels = smoothing ? polygon_texels(p) : int4(0, 0, 0, 0);
    return o;
}
#endif

// Whether the target's pixel at `at` (its centre) is on polygon p's edge as raster3d.hlsl's cover()
// finds it: the first or last of its row in the polygon, or in the polygon's first or last row.
uint edge_at(Polygon p, float2 at)
{
    const float n = (float)scale;
    const int x = (int)floor(at.x), y = (int)floor(at.y);
    int y0 = (int)ceil(p.ymin * n - 0.5f), y1 = (int)ceil(p.ymax * n - 0.5f) - 1;
    float xl = 1e9f, xr = -1e9f;
    if (y1 < y0) {
        return 1u; // flatter than a pixel row: one row, all edge
    }
    if (y == max(y0, 0) || y == min(y1, (int)height - 1)) {
        return 1u;
    }
    {
        const float cy = (float)y + 0.5f;
        for (int i = 0; i < p.count; ++i) {
            const Vertex va = vertex_at(p.first + i), vb = vertex_at(p.first + (i + 1) % p.count);
            const float ay = va.y * n, by = vb.y * n;
            const float ax = (va.x - (float)origin_x) * n, bx = (vb.x - (float)origin_x) * n;
            if (ay == by || cy < min(ay, by) || cy > max(ay, by)) {
                continue;
            }
            {
                const float ex = ax + (bx - ax) * ((cy - ay) / (by - ay));
                xl = min(xl, ex);
                xr = max(xr, ex);
            }
        }
    }
    {
        const int x0 = (int)ceil(xl - 0.5f);
        const int x1 = max((int)ceil(xr - 0.5f) - 1, x0);
        return x == x0 || x == x1 ? 1u : 0u;
    }
}

struct Targets {
    float4 colour : SV_Target0;
    float4 first : SV_Target1;
    float4 fog : SV_Target2;
    float4 opaque : SV_Target3;
    float4 flags : SV_Target4;
    float depth24 : SV_Target5; // a whole number of 24 bits, exact in a float (gpu_native3d.c formats)
#if !defined(NATIVE3D_RASTER_DEPTH)
    float depth : SV_Depth;
#endif
};

// One pixel of a polygon (raster3d.hlsl plot): its depth (the depth buffer's test is the DS's, the
// stencil's its polygon ID and shadow rules), its colour and what it leaves in each target. The
// blending is the targets' (gpu_raster3d.c polygon_pipeline).
Targets ps_main(Corner i)
{
    const float2 st = i.st / 16.0f;
    const float footprint = max(length(ddx(st)), length(ddy(st))); // texels a pixel spans
    const Polygon p = polygon_at(i.polygon & 0x3fffffffu);
    const uint pixels = i.polygon >> 30;
    const uint mode = (p.attr >> 4) & 3u, id = (p.attr >> 24) & 63u;
    const uint depth = depth_of(i.z, i.w, (swap_params >> 1) & 1u);
    const bool edges = (disp3dcnt & 0x20u) != 0u || ((p.attr >> 16) & 31u) == 0u;
    const uint edge = edges ? edge_at(p, i.position.xy) : 0u;
    const int alpha = polygon_alpha(p, i.shade.w, edge);
    Targets o = (Targets)0;
    if (alpha == 0) {
        discard;
    }
    // the depth test "equal" passes within 0x200 above: tested as less or equal, 0x200 nearer
#if !defined(NATIVE3D_RASTER_DEPTH)
    o.depth = (float)((p.attr & 0x4000u) != 0u ? max((int)depth - 0x200, 0) : (int)depth) / 16777216.0f;
#endif
    o.depth24 = (float)depth;
    if (mode == 3u && id == 0u) {
        return o; // a shadow mask: only its depth test counts (it writes no target)
    }
    {
        const int4 c = polygon_color(p, alpha, i.shade.xyz, st.x, st.y, footprint, i.art != 0u, i.texels);
        const bool opaque = c.w == 31 && mode != 3u;
        const float3 rgb = (float3)c.xyz / (fine != 0u ? 255.0f : 63.0f);
        // the DS's blend: (colour (a + 1) + what was there (31 - a)) / 32; an opaque pixel replaces
        const float s = opaque || (disp3dcnt & 8u) == 0u ? 1.0f : (float)(c.w + 1) / 32.0f;
        const uint fog = (p.attr >> 15) & 1u;
        if ((disp3dcnt & 4u) != 0u ? c.w <= (int)(alpha_ref & 31u) : c.w == 0) {
            discard; // the alpha test
        }
        if ((pixels == 1u && !opaque) || (pixels == 2u && opaque)) {
            discard; // the other draw's
        }
        o.colour = float4(rgb * s, s);
        o.first = float4(rgb, 1.0f);
        // an opaque pixel takes its polygon's fog bit, a translucent one only clears it
        o.fog = float4((float)fog, 0.0f, 0.0f, opaque || fog == 0u ? 1.0f : 0.0f);
        o.opaque = float4((float)id / 255.0f, (float)edge, 0.0f, opaque ? 1.0f : 0.0f);
        o.flags = float4(opaque ? 0.0f : 1.0f, (float)(((uint)p.flags >> 3) & 1u), 0.0f, (float)c.w / 31.0f);
    }
    return o;
}
