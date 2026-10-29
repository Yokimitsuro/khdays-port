/* One pixel of a screen's 2D, composed from its frame description (frame.h) the way the DS composes
 * what the game asked for. Shared C/HLSL: the CPU (compose_cpu.c) and the GPU (shaders/compose2d.hlsl)
 * run the same code; the includer defines KD_FN, KD_VOID, KD_DESC, the memories, palettes, 3D and
 * replacements' accessors (KD_*), and the types `uint`, `int` and `float` (docs/RENDERING.md). */

#define KD_OPAQUE 0x8000u

KD_FN int kd_s16(uint v)
{
    return (int)((v & 0xffffu) ^ 0x8000u) - 0x8000;
}

KD_FN uint kd_bg_mask(KD_VOID)
{
    return (KD_DESC(KD_FLAGS) & KDF_ENGINE_B) ? 0x1ffffu : 0x7ffffu;
}

KD_FN uint kd_obj_mask(KD_VOID)
{
    return (KD_DESC(KD_FLAGS) & KDF_ENGINE_B) ? 0x1ffffu : 0x3ffffu;
}

KD_FN uint kd_bg8(uint a)
{
    return KD_BG8(a & kd_bg_mask());
}
KD_FN uint kd_bg16(uint a)
{
    return KD_BG16(a & kd_bg_mask() & ~1u);
}
KD_FN uint kd_obj8(uint a)
{
    return KD_OBJ8(a & kd_obj_mask());
}
KD_FN uint kd_obj16(uint a)
{
    return KD_OBJ16(a & kd_obj_mask() & ~1u);
}

/* --- Tiles drawn instead (HD 2D from mods; not the DS) ----------------------------------
 * A layer's or a sprite's colour is KD_OPAQUE | BGR555, or, from a replaced
 * tile in its own colours, KD_HD | RGB888 (red in bits 0-7). */
#define KD_HD 0x80000000u
#define KD_HD_INDEX 0x40000000u /* from kd_hd: a palette index whose colour is still to be looked up */

KD_FN uint kd_shown(uint c)
{
    return (c & (KD_OPAQUE | KD_HD)) != 0u ? 1u : 0u;
}
KD_FN uint kd_keep(uint c)
{
    return (c & KD_HD) != 0u ? c : (c & 0x7fffu);
}

KD_FN float kd_minf(float a, float b)
{
    return a < b ? a : b;
}
KD_FN float kd_maxf(float a, float b)
{
    return a > b ? a : b;
}

/* A replaced tile in the game's palette: KD_HD_INDEX | the index at (u, v), or 0 where it is clear. */
KD_FN uint kd_hd_index(uint id, uint tile, float u, float v)
{
    const uint info = KD_HD_INFO(id);
    const uint scale = (info >> 8) & 0xffu, columns = info >> 16, side = 8u * scale;
    uint x = (uint)(u * (float)scale), y = (uint)(v * (float)scale), texel;
    x = (x < side ? x : side - 1u) + (tile % columns) * side;
    y = (y < side ? y : side - 1u) + (tile / columns) * side;
    if (x >= KD_HD_WIDTH(id) || y >= KD_HD_HEIGHT(id)) {
        return 0u;
    }
    texel = KD_HD_TEXEL(KD_HD_FIRST(id) + y * KD_HD_WIDTH(id) + x);
    return (texel & 0xffu) != 0u ? KD_HD_INDEX | (texel & 0xffu) : 0u;
}

KD_FN float kd_floorf(float x)
{
    const float t = (float)(int)x;
    return t > x ? t - 1.0f : t;
}

KD_FN int kd_clampi(int x, int lo, int hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

/* Catmull-Rom's weight for a texel `d` texels from where it is read (0 from 2 on). */
KD_FN float kd_cubic(float d)
{
    const float x = d < 0.0f ? -d : d;
    return x < 1.0f ? (1.5f * x - 2.5f) * x * x + 1.0f : x < 2.0f ? ((-0.5f * x + 2.5f) * x - 4.0f) * x + 2.0f : 0.0f;
}

/* A replaced tile's colour under an output pixel `ru` x `rv` DS pixels wide at (u, v): the texels it
 * covers averaged, or, when it is smaller than a texel, a smooth Catmull-Rom blend of the 4 x 4 around.
 * KD_HD | the colour, how much shows through in bits 24-30 (KD_SEE_THROUGH); 0 where clear. */
KD_FN uint kd_hd_colour(uint id, uint tile, float u, float v, float ru, float rv)
{
    const uint info = KD_HD_INFO(id);
    const uint scale = (info >> 8) & 0xffu, columns = info >> 16;
    uint side = 8u * scale; /* a tile's texels across, in the copy read */
    uint width = KD_HD_WIDTH(id), height = KD_HD_HEIGHT(id), first = KD_HD_FIRST(id);
    float texels = kd_maxf(ru, rv) * (float)scale; /* texels under the pixel, across */
    float k, x0, x1, y0, y1, r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f, area = 0.0f;
    uint x, y, ox, oy, seen;
    if ((info & 8u) != 0u) {
        /* the next smaller copy while its texels still come from one tile each */
        while (texels >= 2.0f && (side & 1u) == 0u) {
            first += width * height;
            width = width > 1u ? width >> 1 : 1u;
            height = height > 1u ? height >> 1 : 1u;
            side >>= 1;
            texels *= 0.5f;
        }
    }
    k = (float)side / 8.0f; /* texels in a DS pixel */
    ox = (tile % columns) * side;
    oy = (tile / columns) * side;
    if (texels < 1.0f) {
        /* drawn larger than its texels: the 4 x 4 round the pixel's centre, those past the tile's edge
         * read at its edge (the tile beside it in the sheet is not the one beside it on the screen) */
        const float fx = u * k - 0.5f, fy = v * k - 0.5f;
        const float bx = kd_floorf(fx), by = kd_floorf(fy);
        const int last = (int)side - 1;
        for (int j = -1; j <= 2; ++j) {
            const float wy = kd_cubic(fy - (by + (float)j));
            const uint ty = (uint)kd_clampi((int)by + j, 0, last);
            for (int i = -1; i <= 2; ++i) {
                const float w = wy * kd_cubic(fx - (bx + (float)i));
                const uint tx = (uint)kd_clampi((int)bx + i, 0, last);
                uint texel = 0u; /* past the picture: clear */
                float ta;
                area += w;
                if (ox + tx < width && oy + ty < height) {
                    texel = KD_HD_TEXEL(first + (oy + ty) * width + ox + tx);
                }
                ta = w * (float)(texel >> 24);
                r += ta * (float)(texel & 255u);
                g += ta * (float)((texel >> 8) & 255u);
                b += ta * (float)((texel >> 16) & 255u);
                a += ta;
            }
        }
        x1 = x0 = 0.0f; /* the area's loop below reads none */
        y1 = y0 = 0.0f;
    } else {
        x0 = kd_maxf((u - 0.5f * ru) * k, 0.0f);
        x1 = kd_minf((u + 0.5f * ru) * k, (float)side);
        y0 = kd_maxf((v - 0.5f * rv) * k, 0.0f);
        y1 = kd_minf((v + 0.5f * rv) * k, (float)side);
    }
    for (y = (uint)y0; (float)y < y1; ++y) {
        const float wy = kd_minf((float)(y + 1u), y1) - kd_maxf((float)y, y0);
        for (x = (uint)x0; (float)x < x1; ++x) {
            const float w = wy * (kd_minf((float)(x + 1u), x1) - kd_maxf((float)x, x0));
            uint texel;
            float ta;
            area += w;
            if (ox + x >= width || oy + y >= height) {
                continue; /* past the picture: clear */
            }
            texel = KD_HD_TEXEL(first + (oy + y) * width + ox + x);
            ta = w * (float)(texel >> 24);
            r += ta * (float)(texel & 255u);
            g += ta * (float)((texel >> 8) & 255u);
            b += ta * (float)((texel >> 16) & 255u);
            a += ta;
        }
    }
    if (a <= 0.0f || area <= 0.0f) {
        return 0u;
    }
    /* (Catmull-Rom's weights go below 0 and the sum above: kept within what a texel can be) */
    seen = (uint)((1.0f - kd_minf(a / (area * 255.0f), 1.0f)) * 127.0f + 0.5f);
    if (seen >= 127u) {
        return 0u;
    }
    return KD_HD | (seen << 24) | (uint)(kd_minf(kd_maxf(r / a, 0.0f), 255.0f) + 0.5f) |
           ((uint)(kd_minf(kd_maxf(g / a, 0.0f), 255.0f) + 0.5f) << 8) |
           ((uint)(kd_minf(kd_maxf(b / a, 0.0f), 255.0f) + 0.5f) << 16);
}

/* Replaced tile `entry` (a KD_HD_BG or KD_HD_OBJ word) at (u, v) inside its 8x8 tile, in DS pixels,
 * for an output pixel `ru` x `rv` DS pixels wide. Bitmaps are read by kd_hd_bitmap. */
KD_FN uint kd_hd(uint entry, float u, float v, float ru, float rv)
{
    const uint id = (entry >> 20) - 1u;
    const uint tile = entry & 0xfffffu;
    const uint info = KD_HD_INFO(id);
    if ((info & 4u) != 0u || (info >> 16) == 0u || ((info >> 8) & 0xffu) == 0u || u < 0.0f || v < 0.0f) {
        return 0u;
    }
    return (info & 1u) != 0u ? kd_hd_index(id, tile, u, v) : kd_hd_colour(id, tile, u, v, ru, rv);
}

/* The texel of a replaced bitmap (KHDAYS_REPLACEMENT_BITMAP: a movie's frame) for unit `entry`, at the
 * bitmap's byte `at`, with the output pixel at (fu, fv) inside its DS pixel: KD_HD | its colour, or 0.
 * Its info holds the 32-byte units per row in bits 8-15 and the rows in bits 16-31. */
KD_FN uint kd_hd_bitmap(uint entry, uint at, float fu, float fv)
{
    const uint id = (entry >> 20) - 1u;
    const uint unit = entry & 0xfffffu;
    const uint info = KD_HD_INFO(id);
    const uint units = (info >> 8) & 0xffu, rows = info >> 16;
    const uint width = KD_HD_WIDTH(id), height = KD_HD_HEIGHT(id);
    uint x, y, texel;
    if ((info & 4u) == 0u || units == 0u || rows == 0u) {
        return 0u;
    }
    x = (uint)(((float)((unit % units) * 16u + ((at & 31u) >> 1)) + fu) * (float)width / (float)(units * 16u));
    y = (uint)(((float)(unit / units) + fv) * (float)height / (float)rows);
    if (x >= width || y >= height) {
        return 0u;
    }
    texel = KD_HD_TEXEL(KD_HD_FIRST(id) + y * width + x);
    return (texel >> 24) >= 128u ? KD_HD | (texel & 0xffffffu) : 0u;
}

/* Where inside its DS texel the output pixel falls: the fraction of an
 * affine coordinate (8 fractional bits) moved to the output pixel's place. */
KD_FN float kd_affine_sub(int at, int along_x, int along_y)
{
    const float f = ((float)at + (float)along_x * KD_SUB_X + (float)along_y * KD_SUB_Y) / 256.0f;
    return f - (float)floor(f);
}

/* How many DS pixels an output pixel spans along an affine picture's axis (steps in 8 fractional bits). */
KD_FN float kd_affine_reach(int along_x, int along_y)
{
    return (float)((along_x < 0 ? -along_x : along_x) + (along_y < 0 ? -along_y : along_y)) / 256.0f * KD_PIXEL;
}

/* --- A layer's colour at (x, row): KD_OPAQUE | BGR555, or 0 ------------------- */

KD_FN uint kd_ext_color(uint slot, uint pal, uint index)
{
    return KD_BGEXT16((slot * 0x2000u + pal * 512u + index * 2u) & 0x7ffeu) & 0x7fffu;
}

KD_FN uint kd_text(uint base, int x, int row)
{
    const uint kind = KD_DESC(base + KDL_KIND);
    const uint w = KD_DESC(base + KDL_WIDTH), h = KD_DESC(base + KDL_HEIGHT);
    const uint y = ((uint)row + KD_DESC(base + KDL_VOFS)) & (h - 1u);
    const uint sx = ((uint)x + KD_DESC(base + KDL_HOFS)) & (w - 1u);
    /* the map is laid out in 256x256 blocks: across first, then down */
    const uint screen = KD_DESC(base + KDL_MAP) + (y >> 8) * (w > 256u ? 2u : 1u) * 0x800u + ((y >> 3) & 31u) * 64u;
    const uint entry = kd_bg16(screen + (sx >> 8) * 0x800u + ((sx >> 3) & 31u) * 2u);
    const uint tile = entry & 0x3ffu;
    const uint px = (entry & 0x400u) ? 7u - (sx & 7u) : (sx & 7u);
    const uint py = (entry & 0x800u) ? 7u - (y & 7u) : (y & 7u);
    const uint chars = KD_DESC(base + KDL_CHARS);
    const uint slot = (kind >> KDL_EXT_SLOT_SHIFT) & 7u;
    const uint at = chars + tile * ((kind & KDL_256COLOURS) ? 64u : 32u);
    const uint hd = KD_HD_BG((at & kd_bg_mask()) >> 5);
    uint index;
    if (KD_LEFT_OUT_CELL((base - KD_LAYERS) / KD_LAYER_WORDS * KE_CELLS + (y >> 3) * 64u + (sx >> 3)) != 0u) {
        return 0u; /* left out: an element stands for it */
    }
    if (hd != 0u) {
        const float fu = (entry & 0x400u) ? 1.0f - KD_SUB_X : KD_SUB_X;
        const float fv = (entry & 0x800u) ? 1.0f - KD_SUB_Y : KD_SUB_Y;
        const uint r = kd_hd(hd, (float)px + fu, (float)py + fv, KD_PIXEL, KD_PIXEL);
        if ((r & KD_HD) != 0u) {
            return r;
        }
        index = r & 0xffu;
    } else if (kind & KDL_256COLOURS) {
        index = kd_bg8(at + py * 8u + px);
    } else {
        const uint pair = kd_bg8(at + py * 4u + px / 2u);
        index = (px & 1u) ? pair >> 4 : pair & 15u;
    }
    if (index == 0u) {
        return 0u;
    }
    if (kind & KDL_256COLOURS) {
        return KD_OPAQUE | (slot != 0u ? kd_ext_color(slot - 1u, entry >> 12, index) : (KD_PAL16(index) & 0x7fffu));
    }
    return KD_OPAQUE | (KD_PAL16((entry >> 12) * 16u + (index & 15u)) & 0x7fffu);
}

KD_FN uint kd_affine(uint base, int x, int row)
{
    const uint kind = KD_DESC(base + KDL_KIND);
    const uint k = kind & 0xffu;
    const int w = (int)KD_DESC(base + KDL_WIDTH), h = (int)KD_DESC(base + KDL_HEIGHT);
    const uint papb = KD_DESC(base + KDL_PAPB), pcpd = KD_DESC(base + KDL_PCPD);
    const int pa = kd_s16(papb), pb = kd_s16(papb >> 16), pc = kd_s16(pcpd), pd = kd_s16(pcpd >> 16);
    /* the reference point for this row, then the step along it */
    const int refx = (int)KD_DESC(base + KDL_REFX) + pb * row;
    const int refy = (int)KD_DESC(base + KDL_REFY) + pd * row;
    int px = (refx + pa * x) >> 8;
    int py = (refy + pc * x) >> 8;
    const uint chars = KD_DESC(base + KDL_CHARS), screen = KD_DESC(base + KDL_MAP);
    const uint slot = (kind >> KDL_EXT_SLOT_SHIFT) & 7u;
    if (kind & KDL_WRAP) {
        px = px & (w - 1);
        py = py & (h - 1);
    } else if (px < 0 || px >= w || py < 0 || py >= h) {
        return 0u;
    }
    if (k == KDK_AFFINE || k == KDK_AFFINE_EXT) {
        const uint entry = k == KDK_AFFINE ? kd_bg8(screen + (uint)((py >> 3) * (w >> 3) + (px >> 3)))
                                           : kd_bg16(screen + (uint)((py >> 3) * (w >> 3) + (px >> 3)) * 2u);
        const uint tx = (entry & 0x400u) && k == KDK_AFFINE_EXT ? 7u - (uint)(px & 7) : (uint)(px & 7);
        const uint ty = (entry & 0x800u) && k == KDK_AFFINE_EXT ? 7u - (uint)(py & 7) : (uint)(py & 7);
        const uint at = chars + (k == KDK_AFFINE ? entry : entry & 0x3ffu) * 64u;
        const uint hd = KD_HD_BG((at & kd_bg_mask()) >> 5);
        uint index;
        if (hd != 0u) {
            const float su = kd_affine_sub(refx + pa * x, pa, pb), sv = kd_affine_sub(refy + pc * x, pc, pd);
            const uint r = kd_hd(hd, (float)tx + ((entry & 0x400u) && k == KDK_AFFINE_EXT ? 1.0f - su : su),
                                 (float)ty + ((entry & 0x800u) && k == KDK_AFFINE_EXT ? 1.0f - sv : sv),
                                 kd_affine_reach(pa, pb), kd_affine_reach(pc, pd));
            if ((r & KD_HD) != 0u) {
                return r;
            }
            index = r & 0xffu;
        } else {
            index = kd_bg8(at + ty * 8u + tx);
        }
        if (index == 0u) {
            return 0u;
        }
        if (k == KDK_AFFINE) {
            return KD_OPAQUE | (KD_PAL16(index) & 0x7fffu);
        }
        return KD_OPAQUE | (slot != 0u ? kd_ext_color(slot - 1u, entry >> 12, index) : (KD_PAL16(index) & 0x7fffu));
    }
    if (k == KDK_BITMAP256 || k == KDK_LARGE_BITMAP) {
        const uint index = kd_bg8(KD_DESC(base + KDL_BITMAP) + (uint)(py * w + px));
        return index != 0u ? KD_OPAQUE | (KD_PAL16(index) & 0x7fffu) : 0u;
    }
    {
        const uint at = KD_DESC(base + KDL_BITMAP) + (uint)(py * w + px) * 2u;
        const uint hd = KD_HD_BG((at & kd_bg_mask()) >> 5);
        uint color;
        if (hd != 0u) {
            /* a movie's frame: the port's own picture of it */
            const uint r = kd_hd_bitmap(hd, at & kd_bg_mask(), kd_affine_sub(refx + pa * x, pa, pb),
                                        kd_affine_sub(refy + pc * x, pc, pd));
            if (r != 0u) {
                return r;
            }
        }
        color = kd_bg16(at);
        return (color & 0x8000u) ? color : 0u; /* bit 15 is its alpha */
    }
}

/* Layer b's colour at (x, row); with mosaic, the colour at the mosaic block's corner. */
KD_FN uint kd_layer(int b, int x, int row)
{
    const uint base = KD_LAYERS + (uint)b * KD_LAYER_WORDS;
    const uint kind = KD_DESC(base + KDL_KIND);
    const uint k = kind & 0xffu;
    if (k == KDK_NONE) {
        return 0u;
    }
    if (k == KDK_3D) {
        const uint sx = ((uint)x + KD_DESC(KD_HOFS3D)) & 511u;
        uint p;
        if (sx >= 256u) {
            return 0u;
        }
        p = KD_3D(sx, row);
        if (((p >> 24) & 31u) == 0u) {
            return 0u;
        }
        return KD_OPAQUE | ((p >> 1) & 31u) | (((p >> 9) & 31u) << 5) | (((p >> 17) & 31u) << 10);
    }
    if (kind & KDL_MOSAIC) {
        const uint mosaic = KD_DESC(KD_MOSAIC);
        const int mw = (int)(mosaic & 31u), mh = (int)((mosaic >> 8) & 31u);
        row = row - row % mh;
        x = x - x % mw;
    }
    return k == KDK_TEXT ? kd_text(base, x, row) : kd_affine(base, x, row);
}

KD_FN uint kd_alpha3d(int x, int row)
{
    const uint sx = ((uint)x + KD_DESC(KD_HOFS3D)) & 511u;
    return sx >= 256u ? 31u : ((KD_3D(sx, row) >> 24) & 31u);
}

/* Whether the 3D at (x, row) is a polygon the one-screen picture moved over
 * its 2D (the game over's orb, KHDAYS_G3D_OVER_2D: bit 29 of the 3D drawn).
 * On the DS it is somewhere else, so it is not that DS pixel's 3D. */
KD_FN uint kd_moved3d(int x, int row)
{
    const uint sx = ((uint)x + KD_DESC(KD_HOFS3D)) & 511u;
    return sx >= 256u ? 0u : ((KD_3D(sx, row) >> 29) & 1u);
}

/* --- The sprites at (x, row) --------------------------------------------------- */

typedef struct KdSprite {
    uint color; /* KD_OPAQUE | BGR555 of the topmost, or 0 */
    uint prio;
    uint mode;
    uint window; /* 1: a sprite-window sprite covers the pixel */
} KdSprite;

KD_FN uint kd_sprite_texel(uint flags, uint tile_word, uint w, uint tx, uint ty, float fu, float fv, float ru, float rv)
{
    const uint obj = KD_DESC(KD_OBJ);
    const uint mode = (flags >> KDS_MODE_SHIFT) & 3u;
    const uint tile = tile_word & 0x3ffu;
    const uint pal = (tile_word >> 12) & 15u;
    if (mode == 3u) {
        uint address, color, hd;
        if (obj & KDO_BITMAP_1D) {
            address = tile * ((obj & KDO_BITMAP_STEP256) ? 256u : 128u) + (ty * w + tx) * 2u;
        } else {
            const uint mask = (obj & KDO_BITMAP_WIDE) ? 0x1fu : 0x0fu;
            const uint width = (obj & KDO_BITMAP_WIDE) ? 256u : 128u;
            address = (tile & mask) * 0x10u + (tile & ~mask) * 0x80u + (ty * width + tx) * 2u;
        }
        hd = KD_HD_OBJ((address & kd_obj_mask()) >> 5);
        if (hd != 0u) {
            /* a movie's frame: the port's own picture of it */
            const uint r = kd_hd_bitmap(hd, address & kd_obj_mask(), fu, fv);
            if (r != 0u) {
                return r;
            }
        }
        color = kd_obj16(address);
        return (color & 0x8000u) ? color : 0u;
    }
    if (flags & KDS_256COLOURS) {
        uint address, index, hd;
        if (obj & KDO_TILE_1D) {
            address = tile * (32u << ((obj >> KDO_TILE_STEP_SHIFT) & 3u)) + ((ty >> 3) * (w >> 3) + (tx >> 3)) * 64u;
        } else {
            address = (tile & ~1u) * 32u + (ty >> 3) * 1024u + (tx >> 3) * 64u;
        }
        hd = KD_HD_OBJ((address & kd_obj_mask()) >> 5);
        if (hd != 0u) {
            const uint r = kd_hd(hd, (float)(tx & 7u) + fu, (float)(ty & 7u) + fv, ru, rv);
            if ((r & KD_HD) != 0u) {
                return r;
            }
            index = r & 0xffu;
        } else {
            index = kd_obj8(address + (ty & 7u) * 8u + (tx & 7u));
        }
        if (index == 0u) {
            return 0u;
        }
        if (obj & KDO_OBJ_EXT_PAL) {
            return KD_OPAQUE | (KD_OBJEXT16((pal * 512u + index * 2u) & 0x1ffeu) & 0x7fffu);
        }
        return KD_OPAQUE | (KD_PAL16(256u + index) & 0x7fffu);
    } else {
        uint address, pair, index, hd;
        if (obj & KDO_TILE_1D) {
            address = tile * (32u << ((obj >> KDO_TILE_STEP_SHIFT) & 3u)) + ((ty >> 3) * (w >> 3) + (tx >> 3)) * 32u;
        } else {
            address = tile * 32u + (ty >> 3) * 1024u + (tx >> 3) * 32u;
        }
        hd = KD_HD_OBJ((address & kd_obj_mask()) >> 5);
        if (hd != 0u) {
            const uint r = kd_hd(hd, (float)(tx & 7u) + fu, (float)(ty & 7u) + fv, ru, rv);
            if ((r & KD_HD) != 0u) {
                return r;
            }
            index = r & 15u;
        } else {
            pair = kd_obj8(address + (ty & 7u) * 4u + (tx & 7u) / 2u);
            index = (tx & 1u) ? pair >> 4 : pair & 15u;
        }
        if (index == 0u) {
            return 0u;
        }
        return KD_OPAQUE | (KD_PAL16(256u + pal * 16u + index) & 0x7fffu);
    }
}

KD_FN KdSprite kd_sprites(int x, int row)
{
    KdSprite acc;
    int i;
    acc.color = 0u;
    acc.prio = 0u;
    acc.mode = 0u;
    acc.window = 0u;
    for (i = 0; i < 128; ++i) {
        const uint base = KD_SPRITES + (uint)i * KD_SPRITE_WORDS;
        const uint size = KD_DESC(base + KDS_SIZE);
        uint box, dy, bw, bh, w, h, tx, ty, color, mode, prio;
        float fu, fv, ru = KD_PIXEL, rv = KD_PIXEL;
        int dx;
        if (!(size & KDS_SHOWN) || KD_LEFT_OUT_OBJ((uint)i) != 0u) {
            continue;
        }
        box = KD_DESC(base + KDS_BOX);
        bw = (box >> 8) & 0xffu;
        bh = (box >> 16) & 0xffu;
        dy = ((uint)row - (box & 0xffu)) & 0xffu;
        dx = x - (int)KD_DESC(base + KDS_X);
        if (dy >= bh || dx < 0 || dx >= (int)bw) {
            continue;
        }
        w = size & 0xffu;
        h = (size >> 8) & 0xffu;
        if (size & KDS_AFFINE) {
            const uint papb = KD_DESC(base + KDS_PAPB), pcpd = KD_DESC(base + KDS_PCPD);
            const int rx = dx - (int)(bw / 2u), ry = (int)dy - (int)(bh / 2u);
            const int ax = ((kd_s16(papb) * rx + kd_s16(papb >> 16) * ry) >> 8) + (int)(w / 2u);
            const int ay = ((kd_s16(pcpd) * rx + kd_s16(pcpd >> 16) * ry) >> 8) + (int)(h / 2u);
            if (ax < 0 || ax >= (int)w || ay < 0 || ay >= (int)h) {
                continue;
            }
            tx = (uint)ax;
            ty = (uint)ay;
            fu = kd_affine_sub(kd_s16(papb) * rx + kd_s16(papb >> 16) * ry, kd_s16(papb), kd_s16(papb >> 16));
            fv = kd_affine_sub(kd_s16(pcpd) * rx + kd_s16(pcpd >> 16) * ry, kd_s16(pcpd), kd_s16(pcpd >> 16));
            ru = kd_affine_reach(kd_s16(papb), kd_s16(papb >> 16));
            rv = kd_affine_reach(kd_s16(pcpd), kd_s16(pcpd >> 16));
        } else {
            tx = (size & KDS_HFLIP) ? w - 1u - (uint)dx : (uint)dx;
            ty = (size & KDS_VFLIP) ? h - 1u - dy : dy;
            fu = (size & KDS_HFLIP) ? 1.0f - KD_SUB_X : KD_SUB_X;
            fv = (size & KDS_VFLIP) ? 1.0f - KD_SUB_Y : KD_SUB_Y;
        }
        color = kd_sprite_texel(size, KD_DESC(base + KDS_TILE), w, tx, ty, fu, fv, ru, rv);
        if (color == 0u) {
            continue;
        }
        mode = (size >> KDS_MODE_SHIFT) & 3u;
        if (mode == 2u) {
            acc.window = 1u;
            continue;
        }
        prio = (size >> KDS_PRIO_SHIFT) & 3u;
        /* the lowest priority value wins; on a tie, the first sprite in OAM order */
        if (kd_shown(acc.color) == 0u || prio < acc.prio) {
            acc.color = color;
            acc.prio = prio;
            acc.mode = mode;
        }
    }
    return acc;
}

/* --- Windows, blending, brightness ---------------------------------------------- */

KD_FN uint kd_window_x(uint win, int x)
{
    uint x1, x2;
    if (win & KDW_NONE) {
        return 0u;
    }
    x1 = win & 0x1ffu;
    x2 = (win >> 16) & 0x1ffu;
    if (win & KDW_WRAPS) {
        return ((uint)x >= x1 || (uint)x < x2) ? 1u : 0u;
    }
    return ((uint)x >= x1 && (uint)x < x2) ? 1u : 0u;
}

KD_FN uint kd_window_y(uint window, int row)
{
    return (KD_DESC(KD_WINY + window * 6u + (uint)row / 32u) >> ((uint)row & 31u)) & 1u;
}

KD_FN uint kd_channel(uint c, uint shift)
{
    return (c >> shift) & 31u;
}

KD_FN uint kd_blend(uint a, uint b, uint eva, uint evb)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 15u; shift += 5u) {
        const uint v = (kd_channel(a, shift) * eva + kd_channel(b, shift) * evb) >> 4;
        acc |= (v > 31u ? 31u : v) << shift;
    }
    return acc;
}

KD_FN uint kd_brighten(uint a, uint evy)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 15u; shift += 5u) {
        const uint v = kd_channel(a, shift);
        acc |= (v + (((31u - v) * evy) >> 4)) << shift;
    }
    return acc;
}

KD_FN uint kd_darken(uint a, uint evy)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 15u; shift += 5u) {
        const uint v = kd_channel(a, shift);
        acc |= (v - ((v * evy) >> 4)) << shift;
    }
    return acc;
}

/* The master brightness on the 6-bit intensities: R in bits 0-5, G 8-13, B 16-21. */
KD_FN uint kd_bright(uint color, uint master)
{
    const uint factor = master & 31u;
    const uint mode = (master >> 14) & 3u;
    uint acc = 0u;
    uint n;
    for (n = 0u; n < 3u; ++n) {
        const uint c5 = kd_channel(color, 5u * n);
        uint c = c5 != 0u ? c5 * 2u + 1u : 0u;
        if (mode == 1u) {
            c += (63u - c) * factor / 16u;
        } else if (mode == 2u) {
            c -= c * factor / 16u;
        }
        acc |= c << (8u * n);
    }
    return acc;
}

/* --- In 8 bits per channel (KDF_FINE: when HD textures are drawn; not the DS) -------
 * RGB888 with red in bits 0-7, green 8-15, blue 16-23: the same effects on
 * wider channels. */

KD_FN uint kd_fine(uint bgr555)
{
    uint acc = 0u;
    uint n;
    for (n = 0u; n < 3u; ++n) {
        const uint c = kd_channel(bgr555, 5u * n);
        acc |= ((c << 3) | (c >> 2)) << (8u * n);
    }
    return acc;
}

KD_FN uint kd_blend8(uint a, uint b, uint eva, uint evb)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 24u; shift += 8u) {
        const uint v = (((a >> shift) & 255u) * eva + ((b >> shift) & 255u) * evb) >> 4;
        acc |= (v > 255u ? 255u : v) << shift;
    }
    return acc;
}

KD_FN uint kd_brighten8(uint a, uint evy)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 24u; shift += 8u) {
        const uint v = (a >> shift) & 255u;
        acc |= (v + (((255u - v) * evy) >> 4)) << shift;
    }
    return acc;
}

KD_FN uint kd_darken8(uint a, uint evy)
{
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 24u; shift += 8u) {
        const uint v = (a >> shift) & 255u;
        acc |= (v - ((v * evy) >> 4)) << shift;
    }
    return acc;
}

KD_FN uint kd_bright8(uint color, uint master)
{
    const uint factor = master & 31u;
    const uint mode = (master >> 14) & 3u;
    uint acc = 0u;
    uint shift;
    for (shift = 0u; shift < 24u; shift += 8u) {
        uint c = (color >> shift) & 255u;
        if (mode == 1u) {
            c += (255u - c) * factor / 16u;
        } else if (mode == 2u) {
            c -= c * factor / 16u;
        }
        acc |= c << shift;
    }
    return acc;
}

#define KD_LAYER_SPRITE 4u
#define KD_LAYER_BACKDROP 5u

/* What the windows let show at (x, row): layers 0-3 in bits 0-3, the sprites
 * in bit 4, and the colour effects in bit 5. */
KD_FN uint kd_mask(int x, int row, uint sprite_window)
{
    const uint windows = (KD_DESC(KD_FLAGS) >> KDF_WIN_ON_SHIFT) & 7u;
    uint mask = 0x3fu;
    if (windows != 0u) {
        const uint winin = KD_DESC(KD_WININ), winout = KD_DESC(KD_WINOUT);
        mask = winout & 0x3fu;
        if ((windows & 4u) && sprite_window != 0u) {
            mask = (winout >> 8) & 0x3fu;
        }
        if ((windows & 2u) && kd_window_y(1u, row) != 0u && kd_window_x(KD_DESC(KD_WIN1X), x) != 0u) {
            mask = (winin >> 8) & 0x3fu;
        }
        if ((windows & 1u) && kd_window_y(0u, row) != 0u && kd_window_x(KD_DESC(KD_WIN0X), x) != 0u) {
            mask = winin & 0x3fu;
        }
    }
    return mask;
}

/* Composes the pixel from the layers' colours, the topmost sprite, the window mask and the 3D's alpha:
 * the first two things that show in priority order, with the effects applied. BGR555 before the
 * master brightness; with KDF_FINE RGB888, the 3D layer's own 8-bit colour `fine3d`. */
KD_FN uint kd_compose(uint color[4], KdSprite spr, uint mask, uint alpha3d, uint fine3d)
{
    const uint flags = KD_DESC(KD_FLAGS);
    const uint blendw = KD_DESC(KD_BLEND), coef = KD_DESC(KD_COEF);
    const uint eva = coef & 31u, evb = (coef >> 8) & 31u, evy = (coef >> 16) & 31u;
    const uint effect = (blendw >> 16) & 3u;
    uint layer0 = KD_LAYER_BACKDROP, layer1 = KD_LAYER_BACKDROP;
    uint c0 = KD_DESC(KD_BACKDROP), c1 = KD_DESC(KD_BACKDROP);
    uint found = 0u;
    uint result, p;
    int b;
    for (p = 0u; p < 4u && found < 2u; ++p) {
        if (kd_shown(spr.color) != 0u && spr.prio == p && (mask & 0x10u)) {
            if (found == 0u) {
                layer0 = KD_LAYER_SPRITE;
                c0 = kd_keep(spr.color);
            } else {
                layer1 = KD_LAYER_SPRITE;
                c1 = kd_keep(spr.color);
            }
            ++found;
        }
        for (b = 0; b < 4 && found < 2u; ++b) {
            const uint prio = (KD_DESC(KD_LAYERS + (uint)b * KD_LAYER_WORDS + KDL_KIND) >> 8) & 3u;
            if (prio == p && kd_shown(color[b]) != 0u && (mask & (1u << (uint)b))) {
                if (found == 0u) {
                    layer0 = (uint)b;
                    c0 = kd_keep(color[b]);
                } else {
                    layer1 = (uint)b;
                    c1 = kd_keep(color[b]);
                }
                ++found;
            }
        }
    }
    if ((flags & KDF_FINE) != 0u) {
        const uint is3d = (flags & KDF_BG0_IS_3D) != 0u ? 1u : 0u;
        const uint second = (blendw >> 8) & (1u << layer1);
        const uint f0 = is3d != 0u && layer0 == 0u ? fine3d : (c0 & KD_HD) != 0u ? c0 & 0xffffffu : kd_fine(c0);
        const uint f1 = is3d != 0u && layer1 == 0u ? fine3d : (c1 & KD_HD) != 0u ? c1 & 0xffffffu : kd_fine(c1);
        if (is3d != 0u && layer0 == 0u && second != 0u && (alpha3d < 31u || (effect == 1u && (blendw & 1u)))) {
            return alpha3d < 31u ? kd_blend8(f0, f1, alpha3d / 2u, 16u - alpha3d / 2u) : f0;
        }
        if (mask & 0x20u) {
            if (layer0 == KD_LAYER_SPRITE && (spr.mode == 1u || spr.mode == 3u) && second != 0u) {
                return kd_blend8(f0, f1, eva, evb);
            }
            if (blendw & (1u << layer0)) {
                if (effect == 1u && second != 0u)
                    return kd_blend8(f0, f1, eva, evb);
                if (effect == 2u)
                    return kd_brighten8(f0, evy);
                if (effect == 3u)
                    return kd_darken8(f0, evy);
            }
        }
        return f0;
    }
    /* a replaced tile's own colour, cut down to the DS's 5 bits */
    if ((c0 & KD_HD) != 0u) {
        c0 = ((c0 >> 3) & 31u) | (((c0 >> 11) & 31u) << 5) | (((c0 >> 19) & 31u) << 10);
    }
    if ((c1 & KD_HD) != 0u) {
        c1 = ((c1 >> 3) & 31u) | (((c1 >> 11) & 31u) << 5) | (((c1 >> 19) & 31u) << 10);
    }
    {
        const uint is3d = (flags & KDF_BG0_IS_3D) != 0u ? 1u : 0u;
        const uint second = (blendw >> 8) & (1u << layer1);
        if (is3d != 0u && layer0 == 0u && second != 0u && (alpha3d < 31u || (effect == 1u && (blendw & 1u)))) {
            /* the 3D over a second target blends by its own alpha, never
             * BLDALPHA's (GBATEK: probably EVA = A/2, EVB = 16 - A/2): an
             * opaque 3D pixel stays itself */
            result = alpha3d < 31u ? kd_blend(c0, c1, alpha3d / 2u, 16u - alpha3d / 2u) : c0;
        } else {
            result = c0;
            if (mask & 0x20u) {
                if (layer0 == KD_LAYER_SPRITE && (spr.mode == 1u || spr.mode == 3u) && second != 0u) {
                    result = kd_blend(c0, c1, eva, evb);
                } else if (blendw & (1u << layer0)) {
                    if (effect == 1u && second != 0u) {
                        result = kd_blend(c0, c1, eva, evb);
                    } else if (effect == 2u) {
                        result = kd_brighten(c0, evy);
                    } else if (effect == 3u) {
                        result = kd_darken(c0, evy);
                    }
                }
            }
        }
    }
    return result;
}

/* --- The 2D without the 3D (KDF_SPLIT) ------------------------- */

/* The two topmost layers that show at the pixel, as kd_compose picks them. */
typedef struct KdTop {
    uint layer0;
    uint layer1;
} KdTop;

KD_FN KdTop kd_top(uint color[4], KdSprite spr, uint mask)
{
    KdTop t;
    uint found = 0u, p;
    int b;
    t.layer0 = KD_LAYER_BACKDROP;
    t.layer1 = KD_LAYER_BACKDROP;
    for (p = 0u; p < 4u && found < 2u; ++p) {
        if (kd_shown(spr.color) != 0u && spr.prio == p && (mask & 0x10u)) {
            if (found == 0u) {
                t.layer0 = KD_LAYER_SPRITE;
            } else {
                t.layer1 = KD_LAYER_SPRITE;
            }
            ++found;
        }
        for (b = 0; b < 4 && found < 2u; ++b) {
            const uint prio = (KD_DESC(KD_LAYERS + (uint)b * KD_LAYER_WORDS + KDL_KIND) >> 8) & 3u;
            if (prio == p && kd_shown(color[b]) != 0u && (mask & (1u << (uint)b))) {
                if (found == 0u) {
                    t.layer0 = (uint)b;
                } else {
                    t.layer1 = (uint)b;
                }
                ++found;
            }
        }
    }
    return t;
}

/* The pixel with the 3D's colour X left unknown, for the one-screen pass: the screen shows K + F x X,
 * K in bits 0-23 and in bits 24-31 0 where the 2D does not show over the 3D, else 1 + F x 254. Every
 * blend with the 3D is linear in X, so K and F come from composing with the 3D black and white. With
 * KD_3D_KNOWN the 3D counts as drawn at the pixel. No master brightness (docs/RENDERING.md). */
KD_FN uint kd_split(uint color[4], KdSprite spr, uint mask, int x, int row)
{
    const uint flags = KD_DESC(KD_FLAGS);
    const uint is3d = (flags & KDF_BG0_IS_3D) != 0u && (flags & (1u << KDF_BG_ON_SHIFT)) != 0u ? 1u : 0u;
    const uint blendw = KD_DESC(KD_BLEND);
    const uint a3 = is3d != 0u && KD_3D_KNOWN ? (kd_moved3d(x, row) != 0u ? 0u : kd_alpha3d(x, row)) : 31u;
    uint k, w, f;
    KdTop t;
    if (is3d != 0u) {
        color[0] = a3 != 0u ? KD_OPAQUE : 0u;
    }
    t = kd_top(color, spr, mask);
    if (is3d != 0u && t.layer0 == 0u && t.layer1 == KD_LAYER_BACKDROP && a3 < 31u &&
        ((blendw >> 8) & (1u << KD_LAYER_BACKDROP)) != 0u) {
        /* the 3D, translucent, over the backdrop: blended by its alpha as kd_compose does (the
         * backdrop's share, and the 3D's factor) -- drawn whole it would lose its soft edges */
        const uint eva = a3 / 2u, bd = kd_fine(KD_DESC(KD_BACKDROP) & 0x7fffu);
        uint kb = 0u;
        for (uint sh = 0u; sh < 24u; sh += 8u) {
            kb |= (((bd >> sh) & 255u) * (16u - eva) / 16u) << sh;
        }
        return kb | ((1u + (eva * 254u + 8u) / 16u) << 24);
    }
    if (t.layer0 == KD_LAYER_BACKDROP ||
        (is3d != 0u && t.layer0 == 0u && (a3 >= 31u || t.layer1 == KD_LAYER_BACKDROP))) {
        return 0u;
    }
    if (is3d != 0u && t.layer1 == 0u && (mask & 0x20u) && ((blendw >> 8) & 1u) != 0u) {
        const uint semi = t.layer0 == KD_LAYER_SPRITE && (spr.mode == 1u || spr.mode == 3u) ? 1u : 0u;
        const uint alpha = (blendw & (1u << t.layer0)) != 0u && ((blendw >> 16) & 3u) == 1u ? 1u : 0u;
        if ((semi != 0u || alpha != 0u) && (KD_DESC(KD_COEF) & 31u) == 0u && ((KD_DESC(KD_COEF) >> 8) & 31u) >= 16u) {
            return 0u;
        }
    }
    k = kd_compose(color, spr, mask, a3, 0u);
    w = k;
    if (is3d != 0u && a3 != 0u) {
        color[0] = KD_OPAQUE | 0x7fffu;
        w = kd_compose(color, spr, mask, a3, 0xffffffu);
    }
    if ((flags & KDF_FINE) == 0u) {
        k = kd_fine(k & 0x7fffu);
        w = kd_fine(w & 0x7fffu);
    }
    f = ((w >> 8) & 255u) > ((k >> 8) & 255u) ? ((w >> 8) & 255u) - ((k >> 8) & 255u) : 0u;
    return (k & 0xffffffu) | ((1u + (f * 254u + 127u) / 255u) << 24);
}

/* --- The port's elements (gfx/elements.h) -------------------------------- */

/* Element e's texel at the output pixel (x, row), or 0 when the pixel is
 * outside the element or the element is transparent there (a direct
 * element's texel with its alpha, any above 0). */
KD_FN uint kd_element_texel(uint e, int x, int row)
{
    const uint base = e * KE_WORDS;
    if ((KD_DESC(KD_FLAGS) & KDF_SPLIT) != 0u && (KD_ELEMENT(base + KE_SLOT) & KES_PICTURE) != 0u)
        return 0u;
    const float fx = (float)x + KD_SUB_X, fy = (float)row + KD_SUB_Y;
    const float ex = KD_ASFLOAT(KD_ELEMENT(base + KE_X)), ey = KD_ASFLOAT(KD_ELEMENT(base + KE_Y));
    const float ew = KD_ASFLOAT(KD_ELEMENT(base + KE_W)), eh = KD_ASFLOAT(KD_ELEMENT(base + KE_H));
    const uint iw = KD_ELEMENT(base + KE_IW), ih = KD_ELEMENT(base + KE_IH);
    uint u, v, t;
    if (fx < ex || fy < ey || fx >= ex + ew || fy >= ey + eh || iw == 0u || ih == 0u) {
        return 0u;
    }
    u = (uint)((fx - ex) * (float)iw / ew);
    v = (uint)((fy - ey) * (float)ih / eh);
    u = u < iw ? u : iw - 1u;
    v = v < ih ? v : ih - 1u;
    t = KD_EL_TEXEL(KD_ELEMENT(base + KE_IMAGE), v * iw + u);
    if ((KD_ELEMENT(base + KE_SLOT) & KES_DIRECT) != 0u) {
        return (t >> 24) != 0u ? t : 0u;
    }
    return (t & KET_OPAQUE) != 0u ? t : 0u;
}

/* A shown colour (BGR555 with KD_OPAQUE, or KD_HD RGB888) as RGB888. */
KD_FN uint kd_rgb8(uint c)
{
    return (c & KD_HD) != 0u ? c & 0xffffffu : kd_fine(c & 0x7fffu);
}

/* t of RGB888 colour a and 1 - t of b. */
KD_FN uint kd_mix8(uint a, uint b, float t)
{
    uint mixed = 0u, s;
    for (s = 0u; s < 24u; s += 8u) {
        const float v = (float)((a >> s) & 255u) * t + (float)((b >> s) & 255u) * (1.0f - t);
        mixed |= (uint)(v + 0.5f) << s;
    }
    return mixed;
}

/* The same in BGR555. */
KD_FN uint kd_mix5(uint a, uint b, float t)
{
    uint mixed = 0u, s;
    for (s = 0u; s < 15u; s += 5u) {
        const float v = (float)((a >> s) & 31u) * t + (float)((b >> s) & 31u) * (1.0f - t);
        mixed |= (uint)(v + 0.5f) << s;
    }
    return mixed;
}

/* A layer's or a sprite's colour that does not cover all of what is under it (the elements', a replaced
 * tile's soft edge): KD_HD, the colour, and in bits 24-30 how much of what is under it shows (1-127 of
 * 127; 0 covers it, as every other colour). */
#define KD_SEE_THROUGH(c) ((((c) & KD_HD) != 0u) ? (((c) >> 24) & 0x7fu) : 0u)

/* Layer b's colour at (x, row) with the elements that take its slot, the topmost over the ones under
 * by its alpha, over the layer's own colour. Where the layer does not show either, the colour says how
 * much the elements let through (KD_SEE_THROUGH), composed with and without it (kd_through). */
KD_FN uint kd_element_layer(int b, int x, int row, uint color)
{
    const uint count = KD_DESC(KD_ELEMENTS);
    float r = 0.0f, g = 0.0f, bl = 0.0f, through = 1.0f; /* the colour so far, weighed; what still shows under it */
    uint e, under = 0u, c;
    for (e = 0u; e < count && through > 0.0f; ++e) {
        const uint slot = KD_ELEMENT(e * KE_WORDS + KE_SLOT);
        uint t;
        if ((slot & KES_SPRITES) != 0u || (slot & KES_NUMBER_MASK) != (uint)b) {
            continue;
        }
        t = kd_element_texel(e, x, row);
        if (t == 0u) {
            continue;
        }
        if ((slot & KES_DIRECT) != 0u) {
            const float a = (float)(t >> 24) / 255.0f;
            if (a >= 1.0f && through >= 1.0f) {
                return KD_HD | (t & 0xffffffu);
            }
            r += through * a * (float)(t & 255u);
            g += through * a * (float)((t >> 8) & 255u);
            bl += through * a * (float)((t >> 16) & 255u);
            through *= 1.0f - a;
            continue;
        }
        c = KD_OPAQUE | (KD_PAL16(t & 0xffu) & 0x7fffu);
        if ((t & KET_EXTENDED) != 0u) {
            const uint ext = (KD_DESC(KD_LAYERS + (uint)b * KD_LAYER_WORDS + KDL_KIND) >> KDL_EXT_SLOT_SHIFT) & 7u;
            if (ext != 0u) {
                c = KD_OPAQUE | kd_ext_color(ext - 1u, (t >> KET_EXT_PAL_SHIFT) & 15u, t & 0xffu);
            }
        }
        if (through >= 1.0f) {
            return c;
        }
        under = c; /* an indexed texel under the direct ones: it covers the rest */
        break;
    }
    if (through >= 1.0f) {
        return color;
    }
    if (under == 0u && kd_shown(color) != 0u) {
        under = color;
    }
    if (under != 0u || through <= 0.0f) {
        const uint u = under != 0u ? kd_rgb8(under) : 0u;
        r += through * (float)(u & 255u);
        g += through * (float)((u >> 8) & 255u);
        bl += through * (float)((u >> 16) & 255u);
        return KD_HD | (uint)(r + 0.5f) | ((uint)(g + 0.5f) << 8) | ((uint)(bl + 0.5f) << 16);
    }
    {
        /* nothing of the layer under them: their own colour, and how much shows through */
        const float k = 1.0f / (1.0f - through);
        const uint seen = (uint)(through * 127.0f + 0.5f);
        return KD_HD |
               (seen < 1u     ? 0u
                : seen > 127u ? 127u
                              : seen)
                   << 24 |
               (uint)(r * k + 0.5f) | ((uint)(g * k + 0.5f) << 8) | ((uint)(bl * k + 0.5f) << 16);
    }
}

/* The topmost sprite at (x, row) with the elements that take the sprites'
 * slots: an element's opaque texel there wins over the sprites of its
 * priority and below. */
KD_FN KdSprite kd_element_sprites(int x, int row, KdSprite spr)
{
    const uint count = KD_DESC(KD_ELEMENTS);
    uint e;
    for (e = 0u; e < count; ++e) {
        const uint slot = KD_ELEMENT(e * KE_WORDS + KE_SLOT);
        const uint prio = slot & KES_NUMBER_MASK;
        uint t;
        if ((slot & KES_SPRITES) == 0u || (kd_shown(spr.color) != 0u && spr.prio < prio)) {
            continue;
        }
        t = kd_element_texel(e, x, row);
        if ((slot & KES_DIRECT) != 0u && (t >> 24) < 128u) {
            t = 0u; /* a sprite's slot takes no part of what is under it: shown from half its alpha */
        }
        if (t != 0u) {
            spr.color = (slot & KES_DIRECT) != 0u ? (KD_HD | (t & 0xffffffu))
                                                  : (KD_OPAQUE | (KD_PAL16(256u + (t & 0xffu)) & 0x7fffu));
            spr.prio = prio;
            spr.mode = (slot >> KES_SEMI_SHIFT) & 1u;
            return spr;
        }
    }
    return spr;
}

KD_FN KdSprite kd_no_sprite(KD_VOID)
{
    KdSprite none;
    none.color = 0u;
    none.prio = 0u;
    none.mode = 0u;
    none.window = 0u;
    return none;
}

/* The 3D layer's 8-bit colour at (x, row) (KDF_FINE), or 0 outside the 3D. */
KD_FN uint kd_fine3d(int x, int row)
{
    const uint sx = ((uint)x + KD_DESC(KD_HOFS3D)) & 511u;
    return sx >= 256u ? 0u : KD_3D(sx, row) & 0xffffffu;
}

/* The layer whose elements let what is under them show at the pixel (the
 * first of them), or -1. */
KD_FN int kd_through_layer(uint color[4])
{
    int b;
    for (b = 0; b < 4; ++b) {
        if (KD_SEE_THROUGH(color[b]) != 0u) {
            return b;
        }
    }
    return -1;
}

/* How much of the elements' colour shows: 1 - KD_SEE_THROUGH / 127. */
KD_FN float kd_through_alpha(uint c)
{
    return 1.0f - (float)KD_SEE_THROUGH(c) / 127.0f;
}

/* What lets what is under it show at the pixel: the first such layer, or the topmost sprite
 * (KD_LAYER_SPRITE) when no layer does or the sprite is over the pixel's other layers; else -1. */
KD_FN int kd_through(uint color[4], KdSprite spr, uint mask)
{
    const int layer = kd_through_layer(color);
    if (KD_SEE_THROUGH(spr.color) != 0u && (layer < 0 || kd_top(color, spr, mask).layer0 == KD_LAYER_SPRITE)) {
        return (int)KD_LAYER_SPRITE;
    }
    return layer;
}

/* Two pixels of the one-screen pass (kd_split: K in bits 0-23, and 0, or
 * 1 + F x 254 in bits 24-31, for K + F x X) mixed: t of a, 1 - t of b. */
KD_FN uint kd_mix_split(uint a, uint b, float t)
{
    const float fa = (a >> 24) == 0u ? 1.0f : (float)((a >> 24) - 1u) / 254.0f;
    const float fb = (b >> 24) == 0u ? 1.0f : (float)((b >> 24) - 1u) / 254.0f;
    const uint ka = (a >> 24) == 0u ? 0u : a & 0xffffffu, kb = (b >> 24) == 0u ? 0u : b & 0xffffffu;
    const float f = fa * t + fb * (1.0f - t);
    if ((a >> 24) == 0u && (b >> 24) == 0u) {
        return 0u;
    }
    return kd_mix8(ka, kb, t) | ((1u + (uint)(f * 254.0f + 0.5f)) << 24);
}

/* The pixel at (x, row) as the screen shows it, in 6-bit channels as
 * kd_bright gives them (8-bit ones with KDF_FINE). */
KD_FN uint kd_pixel(int x, int row)
{
    const uint flags = KD_DESC(KD_FLAGS);
    const uint display = flags & KDF_DISPLAY_MASK;
    const uint fine = flags & KDF_FINE;
    uint result;
    if (display == 0u || (display == 1u && (flags & KDF_FORCED_BLANK))) {
        if ((flags & KDF_SPLIT) != 0u) {
            return 0x01ffffffu; /* white, shown, with nothing of the 3D */
        }
        return fine != 0u ? 0xffffffu : 0x3f3f3fu; /* white */
    }
    if (display == 2u) {
        result = KD_VRAM16((uint)(row * 256 + x) * 2u) & 0x7fffu;
        if ((flags & KDF_SPLIT) != 0u) {
            return kd_fine(result) | 0x01000000u;
        }
        if (fine != 0u)
            result = kd_fine(result);
    } else if (display == 3u) {
        if ((flags & KDF_SPLIT) != 0u) {
            return 0x01000000u;
        }
        result = 0u; /* the main memory display mode: not modelled, black */
    } else {
        uint color[4];
        KdSprite spr = kd_no_sprite();
        int b;
        for (b = 0; b < 4; ++b) {
            color[b] = (flags & (1u << (KDF_BG_ON_SHIFT + (uint)b))) ? kd_layer(b, x, row) : 0u;
        }
        if (flags & KDF_OBJ_ON) {
            spr = kd_sprites(x, row);
        }
        if (KD_DESC(KD_ELEMENTS) != 0u) {
            for (b = 0; b < 4; ++b) {
                if (flags & (1u << (KDF_BG_ON_SHIFT + (uint)b))) {
                    color[b] = kd_element_layer(b, x, row, color[b]);
                }
            }
            if (flags & KDF_OBJ_ON) {
                spr = kd_element_sprites(x, row, spr);
            }
        }
        {
            const uint mask = kd_mask(x, row, spr.window);
            const int through = kd_through(color, spr, mask);
            const uint alpha3d = (flags & KDF_BG0_IS_3D) != 0u ? kd_alpha3d(x, row) : 31u;
            const uint fine3d = (fine != 0u && (flags & KDF_BG0_IS_3D) != 0u) ? kd_fine3d(x, row) : 0u;
            uint under[4];
            KdSprite spr_under = spr;
            float t = 1.0f;
            for (b = 0; b < 4; ++b) {
                under[b] = color[b];
            }
            if (through >= 0) {
                /* what lets it show through: the pixel with it and without it */
                if (through == (int)KD_LAYER_SPRITE) {
                    t = kd_through_alpha(spr.color);
                    spr_under.color = 0u;
                } else {
                    t = kd_through_alpha(color[through]);
                    under[through] = 0u;
                }
            }
            if ((flags & KDF_SPLIT) != 0u) {
                uint with[4];
                for (b = 0; b < 4; ++b) {
                    with[b] = color[b];
                }
                result = kd_split(with, spr, mask, x, row);
                return through >= 0 ? kd_mix_split(result, kd_split(under, spr_under, mask, x, row), t) : result;
            }
            result = kd_compose(color, spr, mask, alpha3d, fine3d);
            if (through >= 0) {
                const uint below = kd_compose(under, spr_under, mask, alpha3d, fine3d);
                result = fine != 0u ? kd_mix8(result, below, t) : kd_mix5(result, below, t);
            }
        }
    }
    return fine != 0u ? kd_bright8(result, KD_DESC(KD_MASTER)) : kd_bright(result, KD_DESC(KD_MASTER));
}

/* --- Widescreen (as gpu2d.h's KhdaysGpu2dWide had it) ---------------------------- */

/* Where the 8x8 cell under DS column x moved: -1 to the left, 0 nowhere, 1 to the right. */
KD_FN int kd_side(int x, int row)
{
    const uint cell = (uint)(row >> 3) * 32u + (uint)(x >> 3);
    const uint bits = (KD_DESC(KD_SIDES + cell / 16u) >> ((cell & 15u) * 2u)) & 3u;
    return bits == 1u ? -1 : bits == 2u ? 1 : 0;
}

/* Column X of a picture KD_WIDE DS columns wider on each side: the 3D at its own column, the layers
 * 1-3 at the DS column their HUD piece came from, the sprites and windows at the DS column there
 * (docs/RENDERING.md, "Widescreen"). */
KD_FN uint kd_pixel_wide(int column, int row)
{
    const uint flags = KD_DESC(KD_FLAGS);
    const int margin = (int)KD_DESC(KD_WIDE);
    const int x = column - margin;
    const uint hofs = KD_DESC(KD_HOFS3D);
    const int shift = hofs < 256u ? (int)hofs : (int)hofs - 512;
    const int target = column + shift; /* the 3D's column in its wider target */
    uint color[4], mask, alpha3d = 31u, p3, fine3d = 0u;
    KdSprite spr = kd_no_sprite();
    int source = -1, b;
    if (column < 256 && kd_side(column, row) < 0) {
        source = column; /* a piece moved left */
    } else if (x - margin >= 0 && kd_side(x - margin, row) > 0) {
        source = x - margin; /* a piece moved right */
    } else if (x >= 0 && x < 256 && kd_side(x, row) == 0) {
        source = x;
    }
    color[0] = 0u;
    if ((flags & (1u << KDF_BG_ON_SHIFT)) && target >= 0 && target < 256 + 2 * margin) {
        p3 = KD_3D((uint)target, row);
        if (((p3 >> 24) & 31u) != 0u) {
            color[0] = KD_OPAQUE | ((p3 >> 1) & 31u) | (((p3 >> 9) & 31u) << 5) | (((p3 >> 17) & 31u) << 10);
            alpha3d = (p3 >> 24) & 31u;
            fine3d = p3 & 0xffffffu;
        }
    }
    for (b = 1; b < 4; ++b) {
        color[b] = source >= 0 && (flags & (1u << (KDF_BG_ON_SHIFT + (uint)b))) ? kd_layer(b, source, row) : 0u;
    }
    if (x >= 0 && x < 256 && (flags & KDF_OBJ_ON)) {
        spr = kd_sprites(x, row);
    }
    if (KD_DESC(KD_ELEMENTS) != 0u) {
        /* the elements at their place in the wider picture (in DS columns,
         * the DS's 256 starting at 0): placed by the layout, not moved as
         * pieces */
        for (b = 1; b < 4; ++b) {
            if (flags & (1u << (KDF_BG_ON_SHIFT + (uint)b))) {
                color[b] = kd_element_layer(b, x, row, color[b]);
            }
        }
        if (flags & KDF_OBJ_ON) {
            spr = kd_element_sprites(x, row, spr);
        }
    }
    if (source >= 0 && source != x) {
        mask = kd_mask(source, row, (flags & KDF_OBJ_ON) ? kd_sprites(source, row).window : 0u);
    } else if (x >= 0 && x < 256) {
        mask = kd_mask(x, row, spr.window);
    } else {
        mask = ((flags >> KDF_WIN_ON_SHIFT) & 7u) != 0u ? KD_DESC(KD_WINOUT) & 0x3fu : 0x3fu;
    }
    {
        const int through = kd_through(color, spr, mask);
        uint result = kd_compose(color, spr, mask, alpha3d, fine3d);
        if (through >= 0) {
            /* what lets it show through: the pixel with it and without it */
            uint under[4];
            KdSprite spr_under = spr;
            float t;
            for (b = 0; b < 4; ++b) {
                under[b] = color[b];
            }
            if (through == (int)KD_LAYER_SPRITE) {
                t = kd_through_alpha(spr.color);
                spr_under.color = 0u;
            } else {
                t = kd_through_alpha(color[through]);
                under[through] = 0u;
            }
            result = (flags & KDF_FINE) != 0u ? kd_mix8(result, kd_compose(under, spr_under, mask, alpha3d, fine3d), t)
                                              : kd_mix5(result, kd_compose(under, spr_under, mask, alpha3d, 0u), t);
        }
        if ((flags & KDF_FINE) != 0u) {
            return kd_bright8(result, KD_DESC(KD_MASTER));
        }
        return kd_bright(result, KD_DESC(KD_MASTER));
    }
}
