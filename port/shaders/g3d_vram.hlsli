// The DS's texture memory and the 3D engine's registers as the 3D renderers read them (raster3d.hlsl,
// native3d*.hlsl): texels of every format, the rear plane, the depth, the toon table. The includer declares
// `vram` (the texture slots, 512 KB, then the palette slots, 96 KB, read by their bytes) and, in its
// constant buffer, disp3dcnt, clear_color, clear_depth, fine and toon.

#define TEX_PAL 0x80000u

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
