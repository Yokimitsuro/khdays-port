// The DS's texture memory and the 3D engine's registers as the 3D renderers read them (raster3d.hlsl,
// native3d*.hlsl): texels of every format, the rear plane, the depth, the toon table. The includer declares
// `vram` (the texture slots, 512 KB, then the palette slots, 96 KB, read by their bytes) and, in its
// constant buffer, disp3dcnt, clear_color, clear_depth, fine and toon.

#include "g3d_texels.hlsli"

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

// The rear plane at DS pixel (sx, sy) (gpu3d.c rear_plane): the clear colour, alpha, depth and fog, or
// the clear image's from texture slots 2 and 3. RGB at the pass's precision and alpha 0-31 in `color`.
struct RearPlane {
    int4 color;
    uint depth, fog;
};

RearPlane rear_plane_at(int sx, uint sy)
{
    RearPlane r;
    if ((disp3dcnt & 0x4000u) != 0u) {
        const uint offset = clear_depth >> 16;
        const uint i = ((((sy + (offset >> 8)) & 255u) * 256u + ((uint)(sx + (int)offset) & 255u)) * 2u);
        const uint c = tex16(0x40000u + i), d = tex16(0x60000u + i);
        const uint dd = d & 0x7fffu;
        r.color = int4(channel5((int)(c & 31u)), channel5((int)((c >> 5) & 31u)), channel5((int)((c >> 10) & 31u)),
                       (c & 0x8000u) != 0u ? 31 : 0);
        r.depth = dd * 0x200u + ((dd + 1u) / 0x8000u) * 0x1ffu;
        r.fog = d >> 15;
        return r;
    }
    {
        const uint cd = clear_depth & 0x7fffu;
        r.color = int4(channel5((int)(clear_color & 31u)), channel5((int)((clear_color >> 5) & 31u)),
                       channel5((int)((clear_color >> 10) & 31u)), (int)((clear_color >> 16) & 31u));
        r.depth = cd * 0x200u + ((cd + 1u) / 0x8000u) * 0x1ffu;
        r.fog = (clear_color >> 15) & 1u;
        return r;
    }
}
