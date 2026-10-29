// The one-screen picture, C x 192 DS dots at N times the DS's resolution: each texel from the first
// shape that gives it (the upper screen's 2D without its 3D over the 3D at its place, or the lower
// screen), else the 3D or the backdrop; then the rule's brightness. The steps are
// port/gfx/onescreen.h's, which the host test runs on the CPU.

#include "../gfx/onescreen.h"

// the buffers read by their bytes (raster3d.hlsl says why)
ByteAddressBuffer shapes : register(t0, space0); // KO_WORDS a shape
ByteAddressBuffer keys : register(t1, space0);   // the colours made transparent (BGR555)
// an animated picture's pieces this frame (ui/composition.h KhdaysPiece, 32 words each): its
// rectangle in the picture, its texels in the atlas, its corners' colours, its blend
ByteAddressBuffer pieces : register(t2, space0);
ByteAddressBuffer hd : register(t3, space0); // the existing changing image store

[[vk::image_format("r32ui")]] RWTexture2D<uint> color3d : register(u0, space1); // raster3d's, C x 192 at N
[[vk::image_format("r32ui")]] RWTexture2D<uint> attr3d : register(u1, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> upper2d : register(u2, space1); // 256 x 192 at N
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> lower2d : register(u3, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> out_picture : register(u4, space1);
// the composition's picture behind everything (`backdrop`), read only; a texel when there is none
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> backdrop_image : register(u5, space1);

cbuffer Params : register(b0, space2) {
    uint scale;         // N
    uint width;         // the picture's texels: C x N
    uint height;
    uint has3d;         // 1: color3d and attr3d hold the 3D, as wide as the picture
    uint disp3dcnt;     // finish3d.hlsli's, as compose2d.hlsl's
    uint swap_params;
    uint clear_color;
    uint clear_depth;
    uint fog_color;
    uint fog_offset;
    uint fine;
    uint shape_count;
    uint4 edge_colors;
    uint4 fog_table[2];
    uint frame;         // KOS_*
    uint rule;          // KOB_*
    uint masters;       // the upper screen's MASTER_BRIGHT, the lower's in bits 16-31
    uint backdrop;      // 8-bit, where nothing is
    uint a_upper;       // 1: engine A (the 3D's) is the upper screen
    uint backdrop_w;    // the backdrop picture's texels (0: none, `backdrop` is the colour)
    uint backdrop_h;
    uint hide3d;        // 1: `hide3d all`, no 3D
    float backdrop_scale; // picture texels per backdrop texel
    float backdrop_x;     // its top-left corner in the picture's texels
    float backdrop_y;
    uint piece_count;     // an animated picture's pieces this frame
    uint is_animated;     // 1: an animated picture: its pieces from the atlas on `backdrop`, its ground
    uint unused2;
    uint unused3;
    uint unused4;
};

#include "finish3d.hlsli"

uint pack(float4 c)
{
    return (uint)(c.r * 255.0f + 0.5f) | ((uint)(c.g * 255.0f + 0.5f) << 8) | ((uint)(c.b * 255.0f + 0.5f) << 16) |
           ((uint)(c.a * 255.0f + 0.5f) << 24);
}

// A source screen's texel at (x, y) DS dots: the nearest of its N x N.
uint source_at(uint lower, float x, float y)
{
    const int tx = (int)floor(x * (float)scale), ty = (int)floor(y * (float)scale);
    const int limit_x = 256 * (int)scale - 1, limit_y = 192 * (int)scale - 1;
    const uint2 at = uint2((uint)clamp(tx, 0, limit_x), (uint)clamp(ty, 0, limit_y));
    return pack(lower != 0u ? lower2d[at] : upper2d[at]);
}

#define KO_STEPS
#define KO_FN
#define KO_SHAPE(i, w) shapes.Load(((i) * KO_WORDS + (w)) * 4u)
#define KO_KEY(i) keys.Load((i) * 4u)
#define KO_ASFLOAT(u) asfloat(u)
#define KO_FLOOR(f) floor(f)
#define KO_ABS(i) abs(i)
#define KO_SOURCE(lower, x, y) source_at((lower), (x), (y))
uint image_at(uint image, float x, float y)
{
    const uint first = hd.Load(image * 16u), w = hd.Load(image * 16u + 4u), h = hd.Load(image * 16u + 8u);
    if (x < 0.0f || y < 0.0f || x >= (float)w || y >= (float)h)
        return 0u;
    return hd.Load((first + (uint)y * w + (uint)x) * 4u);
}
#define KO_IMAGE(image, x, y) image_at((image), (x), (y))
#include "../gfx/onescreen.h"

// The 3D at the texel, 8-bit channels, bit 24 when it shows there; drawn over
// the 2D (KHDAYS_G3D_OVER_2D) bit 25, and its alpha (0-31) in bits 26-30.
uint x3d_at(uint2 id)
{
    uint c, r, g, b, over;
    if (has3d == 0u || hide3d != 0u) {
        return 0u;
    }
    c = finished3d((int)id.x, (int)id.y);
    if (((c >> 24) & 31u) == 0u) {
        return 0u;
    }
    over = (c & 0x20000000u) != 0u ? 0x2000000u | (((c >> 24) & 31u) << 26) : 0u;
    if (fine != 0u) {
        return (c & 0xffffffu) | 0x1000000u | over;
    }
    r = c & 63u;
    g = (c >> 8) & 63u;
    b = (c >> 16) & 63u;
    return ((r << 2) | (r >> 4)) | (((g << 2) | (g >> 4)) << 8) | (((b << 2) | (b >> 4)) << 16) | 0x1000000u | over;
}

// The backdrop picture at (ix, iy) of its texels (their middles at .5), filtered between the four
// nearest within (lo_x, lo_y)-(hi_x, hi_y).
float4 backdrop_texel(float ix, float iy, int lo_x, int lo_y, int hi_x, int hi_y)
{
    const int x0 = (int)floor(ix - 0.5f), y0 = (int)floor(iy - 0.5f);
    const float fx = ix - 0.5f - (float)x0, fy = iy - 0.5f - (float)y0;
    const int ax = clamp(x0, lo_x, hi_x), bx = clamp(x0 + 1, lo_x, hi_x);
    const int ay = clamp(y0, lo_y, hi_y), by = clamp(y0 + 1, lo_y, hi_y);
    return lerp(lerp(backdrop_image[uint2(ax, ay)], backdrop_image[uint2(bx, ay)], fx),
                lerp(backdrop_image[uint2(ax, by)], backdrop_image[uint2(bx, by)], fx), fy);
}

float4 piece_corner(uint at)
{
    return asfloat(pieces.Load4(at * 4u));
}

// An animated picture at (px, py) of its texels: its ground, then each piece over it whose
// rectangle holds the point, its atlas texels times its corners' colours blended across it.
float3 animated_at(float px, float py)
{
    float3 c = float3((float)(backdrop & 255u), (float)((backdrop >> 8) & 255u), (float)((backdrop >> 16) & 255u)) / 255.0f;
    for (uint i = 0u; i < piece_count; ++i) {
        const uint at = i * 32u;
        const float x0 = asfloat(pieces.Load(at * 4u)), y0 = asfloat(pieces.Load((at + 1u) * 4u));
        const float x1 = asfloat(pieces.Load((at + 2u) * 4u)), y1 = asfloat(pieces.Load((at + 3u) * 4u));
        float u, v, w, h, fx, fy;
        float4 s, k;
        if (px < x0 || py < y0 || px >= x1 || py >= y1) {
            continue;
        }
        u = asfloat(pieces.Load((at + 4u) * 4u));
        v = asfloat(pieces.Load((at + 5u) * 4u));
        w = asfloat(pieces.Load((at + 6u) * 4u));
        h = asfloat(pieces.Load((at + 7u) * 4u));
        fx = (px - x0) / (x1 - x0);
        fy = (py - y0) / (y1 - y0);
        s = backdrop_texel(u + fx * w, v + fy * h, (int)min(u, u + w), (int)min(v, v + h),
                           (int)max(u, u + w) - 1, (int)max(v, v + h) - 1);
        k = lerp(lerp(piece_corner(at + 8u), piece_corner(at + 12u), fx),
                 lerp(piece_corner(at + 16u), piece_corner(at + 20u), fx), fy);
        s = saturate(s * k);
        switch (pieces.Load((at + 24u) * 4u)) {
        case 1u: // KHDAYS_PIECE_ADD
            c = saturate(c + s.rgb * s.a);
            break;
        case 2u: // KHDAYS_PIECE_TAKE
            c = saturate(c - s.rgb * s.a);
            break;
        default:
            c = c * (1.0f - s.a) + s.rgb * s.a;
            break;
        }
    }
    return c;
}

// What is behind everything at the texel: an animated picture, the backdrop picture filtered
// between its four nearest texels, or the frame's own colour.
uint backdrop_at(uint2 id)
{
    float px, py;
    if (backdrop_w == 0u) {
        return backdrop;
    }
    px = ((float)id.x + 0.5f - backdrop_x) / backdrop_scale;
    py = ((float)id.y + 0.5f - backdrop_y) / backdrop_scale;
    if (is_animated != 0u) {
        return pack(float4(animated_at(px, py), 1.0f)) & 0xffffffu;
    }
    return pack(backdrop_texel(px, py, 0, 0, (int)backdrop_w - 1, (int)backdrop_h - 1)) & 0xffffffu;
}

[numthreads(8, 8, 1)]
void cs_main(uint3 id : SV_DispatchThreadID)
{
    uint c;
    if (id.x >= width || id.y >= height) {
        return;
    }
    c = ko_pixel(((float)id.x + 0.5f) / (float)scale, ((float)id.y + 0.5f) / (float)scale, 1.0f / (float)scale,
                 x3d_at(id.xy), shape_count,
                 rule, masters, backdrop_at(id.xy), (float)width / (float)scale / 2.0f, a_upper);
    out_picture[id.xy] = float4((float)(c & 255u) / 255.0f, (float)((c >> 8) & 255u) / 255.0f,
                                (float)((c >> 16) & 255u) / 255.0f, 1.0f);
}
