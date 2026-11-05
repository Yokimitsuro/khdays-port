// The DS's texture memory decoded as the shaders read it (g3d_vram.hlsli, shadow_mask.hlsl): a texel of any
// format. The includer declares `vram` (the texture slots, 512 KB, then the palette slots, 96 KB), read by
// its bytes.

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
