/* Builds the frame description from what the game asked for (see
 * describe.h): its display control, layers, sprites, windows and effects,
 * each decoded once a frame as the DS defines it (GBATEK, "DS Video", "LCD
 * I/O"). */
#include "describe.h"
#include "../runtime/core/bytes.h"

#include <string.h>

/* A coefficient: 0-31 as written, 16 and above the whole. */
static uint32_t coefficient(uint32_t v)
{
    v &= 31;
    return v > 16 ? 16 : v;
}

/* The windows' vertical flags on line `line`. */
static void window_y_step(KhdaysGfxLatch *latch, const uint8_t *io, int line)
{
    for (int w = 0; w < 2; ++w) {
        const uint32_t winv = le16(io + 0x44 + 2 * w);
        if (((uint32_t)line & 0xff) == (winv & 0xff)) {
            latch->win_y[w] = 0;
        } else if (((uint32_t)line & 0xff) == (winv >> 8)) {
            latch->win_y[w] = 1;
        }
    }
}

void khdays_gfx_describe_skip(const uint8_t *io, int engine, KhdaysGfxLatch *latch)
{
    const uint8_t *e = io + (engine ? 0x1000 : 0);
    for (int line = 0; line < 263; ++line) {
        window_y_step(latch, e, line);
    }
}

/* A window's columns: X2 = 0 is 256, X1 = X2 = 0 shows none, X1 > X2 wraps. */
static uint32_t window_columns(uint32_t winh)
{
    const uint32_t x1 = winh >> 8;
    uint32_t x2 = winh & 0xff;
    if (x1 == 0 && x2 == 0) {
        return KDW_NONE;
    }
    if (x2 == 0) {
        x2 = 256;
    }
    return (x1 > x2 ? KDW_WRAPS : 0) | x1 | x2 << 16;
}

/* Each mode's layers: 0 text, 1 affine, 2 extended, 3 large bitmap, -1 none. */
static int layer_family(uint32_t dispcnt, int engine, int bg)
{
    static const int8_t table[8][4] = {
        {0, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 1, 1},   {0, 0, 0, 2},
        {0, 0, 1, 2}, {0, 0, 2, 2}, {0, -1, 3, -1}, {-1, -1, -1, -1},
    };
    const uint32_t mode = dispcnt & 7;
    if (engine == 1 && mode >= 6) {
        return -1; /* reserved on engine B */
    }
    return table[mode][bg];
}

/* The extended palette slot a layer's 256-colour tiles take, + 1; 0 the
 * standard palette. */
static uint32_t ext_slot(uint32_t dispcnt, int bg, uint32_t cnt)
{
    if (!(dispcnt & (1u << 30))) {
        return 0;
    }
    return (uint32_t)((bg < 2 && (cnt & 0x2000)) ? bg + 2 : bg) + 1;
}

static void describe_layer(const uint8_t *e, uint32_t dispcnt, int engine, int bg, uint32_t *layer, int *shown)
{
    static const uint32_t bitmap_w[4] = {128, 256, 512, 512};
    static const uint32_t bitmap_h[4] = {128, 256, 256, 512};
    const uint32_t cnt = le16(e + 0x08 + 2 * bg);
    const uint32_t size = cnt >> 14;
    const int family = layer_family(dispcnt, engine, bg);
    const uint32_t char_base = (engine == 0 ? ((dispcnt >> 24) & 7) * 0x10000u : 0) + ((cnt >> 2) & 15) * 0x4000u;
    const uint32_t map_base = (engine == 0 ? ((dispcnt >> 27) & 7) * 0x10000u : 0) + ((cnt >> 8) & 31) * 0x800u;
    uint32_t kind = KDK_NONE;
    memset(layer, 0, KD_LAYER_WORDS * sizeof(uint32_t));
    *shown = (dispcnt & (0x100u << bg)) && family >= 0;
    layer[KDL_CHARS] = char_base;
    layer[KDL_MAP] = map_base;
    layer[KDL_HOFS] = le16(e + 0x10 + 4 * bg) & 0x1ff;
    layer[KDL_VOFS] = le16(e + 0x12 + 4 * bg) & 0x1ff;
    if (family < 0) {
        layer[KDL_KIND] = KDK_NONE | (cnt & 3) << 8;
        return;
    }
    if (bg == 0 && engine == 0 && (dispcnt & 0x08)) {
        layer[KDL_KIND] = KDK_3D | (cnt & 3) << 8;
        return;
    }
    if (family == 0) {
        kind = KDK_TEXT;
        layer[KDL_WIDTH] = (size & 1) ? 512 : 256;
        layer[KDL_HEIGHT] = (size & 2) ? 512 : 256;
        if (cnt & 0x80) {
            kind |= KDL_256COLOURS | ext_slot(dispcnt, bg, cnt) << KDL_EXT_SLOT_SHIFT;
        }
    } else {
        const int n = bg - 2;
        const uint8_t *regs = e + 0x20 + 0x10 * n;
        if (family == 1) {
            kind = KDK_AFFINE;
            layer[KDL_WIDTH] = layer[KDL_HEIGHT] = 128u << size;
        } else if (family == 2) {
            if (!(cnt & 0x80)) {
                kind = KDK_AFFINE_EXT | ext_slot(dispcnt, bg, cnt) << KDL_EXT_SLOT_SHIFT;
                layer[KDL_WIDTH] = layer[KDL_HEIGHT] = 128u << size;
            } else {
                kind = (cnt & 0x04) ? KDK_BITMAP_DIRECT : KDK_BITMAP256;
                layer[KDL_WIDTH] = bitmap_w[size];
                layer[KDL_HEIGHT] = bitmap_h[size];
                layer[KDL_BITMAP] = ((cnt >> 8) & 31) * 0x4000u; /* without DISPCNT's 64 KB steps */
            }
        } else {
            kind = KDK_LARGE_BITMAP;
            layer[KDL_WIDTH] = (size & 1) ? 1024 : 512;
            layer[KDL_HEIGHT] = (size & 1) ? 512 : 1024;
            layer[KDL_BITMAP] = 0;
        }
        if (cnt & 0x2000) {
            kind |= KDL_WRAP;
        }
        layer[KDL_PAPB] = le16(regs + 0) | le16(regs + 2) << 16;
        layer[KDL_PCPD] = le16(regs + 4) | le16(regs + 6) << 16;
        /* 28-bit signed, 8 fractional bits */
        layer[KDL_REFX] = (uint32_t)((int32_t)(le32(regs + 8) << 4) >> 4);
        layer[KDL_REFY] = (uint32_t)((int32_t)(le32(regs + 12) << 4) >> 4);
    }
    if (cnt & 0x40) {
        kind |= KDL_MOSAIC;
    }
    layer[KDL_KIND] = kind | (cnt & 3) << 8;
}

static void describe_sprites(const uint8_t *oam, uint32_t *desc)
{
    static const uint8_t obj_w[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const uint8_t obj_h[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    for (int i = 0; i < 128; ++i) {
        const uint8_t *entry = oam + 8 * i;
        uint32_t *s = desc + KD_SPRITES + i * KD_SPRITE_WORDS;
        const uint32_t a0 = le16(entry), a1 = le16(entry + 2), a2 = le16(entry + 4);
        const uint32_t affine = (a0 & 0x100) != 0;
        const uint32_t shape = a0 >> 14, size = a1 >> 14;
        uint32_t w, h, bw, bh, flags;
        int32_t x;
        memset(s, 0, KD_SPRITE_WORDS * sizeof(uint32_t));
        if ((!affine && (a0 & 0x200)) || shape == 3) {
            continue; /* hidden, or the shape the DS does not have */
        }
        w = obj_w[shape][size];
        h = obj_h[shape][size];
        bw = w;
        bh = h;
        if (affine && (a0 & 0x200)) {
            bw *= 2; /* double size */
            bh *= 2;
        }
        x = (int32_t)(a1 & 0x1ff);
        if (x >= 256) {
            x -= 512;
        }
        flags = KDS_SHOWN | ((a0 >> 10) & 3) << KDS_MODE_SHIFT | ((a2 >> 10) & 3) << KDS_PRIO_SHIFT;
        if (a0 & 0x2000) {
            flags |= KDS_256COLOURS;
        }
        if (affine) {
            const uint8_t *group = oam + 32 * ((a1 >> 9) & 31);
            flags |= KDS_AFFINE;
            s[KDS_PAPB] = le16(group + 6) | le16(group + 14) << 16;
            s[KDS_PCPD] = le16(group + 22) | le16(group + 30) << 16;
        } else {
            flags |= (a1 & 0x1000 ? KDS_HFLIP : 0) | (a1 & 0x2000 ? KDS_VFLIP : 0);
        }
        s[KDS_X] = (uint32_t)x;
        s[KDS_BOX] = (a0 & 0xff) | bw << 8 | bh << 16;
        s[KDS_SIZE] = w | h << 8 | flags;
        s[KDS_TILE] = a2 & 0xf3ff;
    }
}

void khdays_gfx_describe(const uint8_t *io, const uint8_t *palette, const uint8_t *oam, int engine,
                         KhdaysGfxLatch *latch, uint32_t desc[KHDAYS_DESC_WORDS])
{
    const uint8_t *e = io + (engine ? 0x1000 : 0);
    const uint32_t dispcnt = le32(e);
    uint32_t display = (dispcnt >> 16) & 3, flags;
    const uint32_t bldcnt = le16(e + 0x50), bldalpha = le16(e + 0x52), master = le16(e + 0x6c);
    const uint32_t mosaic = le16(e + 0x4c);
    memset(desc, 0, KHDAYS_DESC_WORDS * sizeof(uint32_t));
    if (engine == 1) {
        display &= 1;
    }
    flags = display | ((dispcnt >> 13) & 7) << KDF_WIN_ON_SHIFT;
    if (dispcnt & 0x80) {
        flags |= KDF_FORCED_BLANK;
    }
    if (engine == 0 && (dispcnt & 0x08)) {
        flags |= KDF_BG0_IS_3D;
    }
    if (dispcnt & 0x1000) {
        flags |= KDF_OBJ_ON;
    }
    if (engine == 1) {
        flags |= KDF_ENGINE_B;
    }
    for (int bg = 0; bg < 4; ++bg) {
        int shown;
        describe_layer(e, dispcnt, engine, bg, desc + KD_LAYERS + bg * KD_LAYER_WORDS, &shown);
        if (shown) {
            flags |= 1u << (KDF_BG_ON_SHIFT + bg);
        }
    }
    desc[KD_FLAGS] = flags;
    desc[KD_OBJ] = ((dispcnt & 0x10) ? KDO_TILE_1D : 0) | ((dispcnt >> 20) & 3) << KDO_TILE_STEP_SHIFT |
                   ((dispcnt & 0x40) ? KDO_BITMAP_1D : 0) |
                   ((engine == 0 && (dispcnt & (1u << 22))) ? KDO_BITMAP_STEP256 : 0) |
                   ((dispcnt & 0x20) ? KDO_BITMAP_WIDE : 0) | ((dispcnt & (1u << 31)) ? KDO_OBJ_EXT_PAL : 0) |
                   ((dispcnt & (1u << 30)) ? KDO_BG_EXT_PAL : 0);
    desc[KD_BACKDROP] = le16(palette + (engine ? 0x400 : 0)) & 0x7fff;
    desc[KD_MASTER] = coefficient(master) | (master & 0xc000);
    desc[KD_BLEND] = (bldcnt & 0x3f) | (bldcnt & 0x3f00) | ((bldcnt >> 6) & 3) << 16;
    desc[KD_COEF] = coefficient(bldalpha) | coefficient(bldalpha >> 8) << 8 | coefficient(le16(e + 0x54)) << 16;
    desc[KD_WIN0X] = window_columns(le16(e + 0x40));
    desc[KD_WIN1X] = window_columns(le16(e + 0x42));
    desc[KD_WININ] = le16(e + 0x48) & 0x3f3f;
    desc[KD_WINOUT] = le16(e + 0x4a) & 0x3f3f;
    desc[KD_MOSAIC] = ((mosaic & 15) + 1) | (((mosaic >> 4) & 15) + 1) << 8;
    desc[KD_HOFS3D] = le16(e + 0x10) & 0x1ff;
    desc[KD_VRAM] = (dispcnt >> 18) & 3;
    describe_sprites(oam + (engine ? 0x400 : 0), desc);
    for (int line = 0; line < 263; ++line) {
        window_y_step(latch, e, line);
        if (line < 192) {
            for (int w = 0; w < 2; ++w) {
                if (latch->win_y[w]) {
                    desc[KD_WINY + w * 6 + line / 32] |= 1u << (line & 31);
                }
            }
        }
    }
}
