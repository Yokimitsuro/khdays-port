/* Runs every frame: optimized even in Debug builds, whose /RTC checks
 * cannot be combined with optimization. */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "gpu2d.h"

#include <stdio.h>
#include <string.h>

/* Features the renderer does not model yet: said once, on first use, so a
 * frame that looks wrong points at its cause. */
enum {
    MISSING_MAIN_MEMORY_DISPLAY = 1u << 0,
    MISSING_CAPTURE = 1u << 1,
    MISSING_OBJ_MOSAIC = 1u << 2,
    MISSING_BITMAP_OBJ_ALPHA = 1u << 3,
    MISSING_WINDOW_X1_AFTER_X2 = 1u << 4,
};

static void missing(unsigned what, const char *text)
{
    static unsigned said;
    if (!(said & what)) {
        said |= what;
        fprintf(stderr, "gpu2d: not modelled: %s\n", text);
        fflush(stderr);
    }
}

/* --- Memory ---------------------------------------------------------------- */

typedef struct Engine {
    const KhdaysGpuInput *in;
    int id;                  /* 0 = A, 1 = B */
    const uint8_t *io;       /* this engine's registers */
    const uint8_t *bg_pal;
    const uint8_t *obj_pal;
    const uint8_t *oam;
    int bg_area, obj_area, bg_ext_area, obj_ext_area;
    uint32_t dispcnt;
    uint32_t char_base;      /* engine A's 64 KB steps from DISPCNT */
    uint32_t screen_base;
} Engine;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint8_t vram8(const Engine *e, int area, uint32_t address)
{
    const uint8_t *page;
    address &= khdays_vram_area_size[area] - 1;  /* every 2D area is a power of two */
    page = e->in->vram->page[area][address / KHDAYS_VRAM_PAGE];
    return page != NULL ? page[address % KHDAYS_VRAM_PAGE] : 0;
}

static uint16_t vram16(const Engine *e, int area, uint32_t address)
{
    const uint8_t *page;
    address &= (khdays_vram_area_size[area] - 1) & ~1u;
    page = e->in->vram->page[area][address / KHDAYS_VRAM_PAGE];
    return page != NULL ? rd16(page + address % KHDAYS_VRAM_PAGE) : 0;
}

static uint16_t palette(const uint8_t *pal, unsigned index)
{
    return rd16(pal + 2 * index) & 0x7fff;
}

/* --- Backgrounds ----------------------------------------------------------- */

#define OPAQUE 0x8000u

static uint32_t bg_char_base(const Engine *e, uint16_t cnt)
{
    return e->char_base + ((cnt >> 2) & 15) * 0x4000u;
}

static uint32_t bg_screen_base(const Engine *e, uint16_t cnt)
{
    return e->screen_base + ((cnt >> 8) & 31) * 0x800u;
}

/* The extended palette slot a BG's 256-colour tiles use, or -1 for the
 * standard palette. */
static int ext_slot(const Engine *e, int bg, uint16_t cnt)
{
    if (!(e->dispcnt & (1u << 30))) {
        return -1;
    }
    if (bg < 2 && (cnt & 0x2000)) {
        return bg + 2;
    }
    return bg;
}

static uint16_t ext_color(const Engine *e, int slot, unsigned pal, unsigned index)
{
    return vram16(e, e->bg_ext_area, slot * 0x2000u + pal * 512u + index * 2u) & 0x7fff;
}

static void text_bg(const Engine *e, int bg, int line, uint16_t *out)
{
    const uint16_t cnt = rd16(e->io + 0x08 + 2 * bg);
    const uint32_t hofs = rd16(e->io + 0x10 + 4 * bg) & 0x1ff;
    const uint32_t vofs = rd16(e->io + 0x12 + 4 * bg) & 0x1ff;
    const unsigned size = cnt >> 14;
    const uint32_t wmask = (size & 1) ? 511 : 255;
    const uint32_t hmask = (size & 2) ? 511 : 255;
    const uint32_t y = ((uint32_t)line + vofs) & hmask;
    const uint32_t screen = bg_screen_base(e, cnt) + (y >> 8) * ((size & 1) ? 2u : 1u) * 0x800u +
                            ((y >> 3) & 31) * 64u;
    const uint32_t chars = bg_char_base(e, cnt);
    const int slot = (cnt & 0x80) ? ext_slot(e, bg, cnt) : -1;
    for (int x = 0; x < KHDAYS_SCREEN_W; ++x) {
        const uint32_t sx = ((uint32_t)x + hofs) & wmask;
        const uint16_t entry = vram16(e, e->bg_area, screen + (sx >> 8) * 0x800u + ((sx >> 3) & 31) * 2u);
        const uint32_t tile = entry & 0x3ff;
        const uint32_t px = (entry & 0x400) ? 7 - (sx & 7) : (sx & 7);
        const uint32_t py = (entry & 0x800) ? 7 - (y & 7) : (y & 7);
        unsigned index;
        if (cnt & 0x80) {
            index = vram8(e, e->bg_area, chars + tile * 64u + py * 8u + px);
            if (index != 0) {
                out[x] = (uint16_t)(OPAQUE | (slot >= 0 ? ext_color(e, slot, entry >> 12, index)
                                                        : palette(e->bg_pal, index)));
            }
        } else {
            const uint8_t pair = vram8(e, e->bg_area, chars + tile * 32u + py * 4u + px / 2);
            index = (px & 1) ? pair >> 4 : pair & 15;
            if (index != 0) {
                out[x] = (uint16_t)(OPAQUE | palette(e->bg_pal, (entry >> 12) * 16u + index));
            }
        }
    }
}

enum { AFFINE, AFFINE_EXT, BITMAP256, BITMAP_DIRECT, LARGE_BITMAP, TEXT_BG };

/* The rotation/scaling family. (refx, refy) is the internal reference point
 * for this line. */
static void affine_bg(const Engine *e, int bg, int kind, int32_t refx, int32_t refy, uint16_t *out)
{
    static const uint16_t bitmap_w[4] = {128, 256, 512, 512};
    static const uint16_t bitmap_h[4] = {128, 256, 256, 512};
    const uint16_t cnt = rd16(e->io + 0x08 + 2 * bg);
    const uint8_t *regs = e->io + 0x20 + 0x10 * (bg - 2);
    const int32_t pa = (int16_t)rd16(regs + 0);
    const int32_t pc = (int16_t)rd16(regs + 4);
    const unsigned size = cnt >> 14;
    const int wrap = (cnt & 0x2000) != 0;
    int32_t w, h;
    const uint32_t chars = bg_char_base(e, cnt);
    const uint32_t screen = bg_screen_base(e, cnt);
    const uint32_t bitmap = ((cnt >> 8) & 31) * 0x4000u;  /* without DISPCNT's 64 KB steps */
    const int slot = kind == AFFINE_EXT ? ext_slot(e, bg, cnt) : -1;
    if (kind == AFFINE || kind == AFFINE_EXT) {
        w = h = 128 << size;
    } else if (kind == LARGE_BITMAP) {
        w = (size & 1) ? 1024 : 512;
        h = (size & 1) ? 512 : 1024;
    } else {
        w = bitmap_w[size];
        h = bitmap_h[size];
    }
    for (int x = 0; x < KHDAYS_SCREEN_W; ++x) {
        int32_t px = (refx + pa * x) >> 8;
        int32_t py = (refy + pc * x) >> 8;
        if (wrap) {
            px &= w - 1;
            py &= h - 1;
        } else if (px < 0 || px >= w || py < 0 || py >= h) {
            continue;
        }
        switch (kind) {
        case AFFINE: {
            const uint8_t tile = vram8(e, e->bg_area, screen + (uint32_t)((py >> 3) * (w >> 3) + (px >> 3)));
            const uint8_t index = vram8(e, e->bg_area, chars + tile * 64u + (py & 7) * 8u + (px & 7));
            if (index != 0) {
                out[x] = (uint16_t)(OPAQUE | palette(e->bg_pal, index));
            }
            break;
        }
        case AFFINE_EXT: {
            const uint16_t entry = vram16(e, e->bg_area,
                                          screen + (uint32_t)((py >> 3) * (w >> 3) + (px >> 3)) * 2u);
            const uint32_t tx = (entry & 0x400) ? 7 - (px & 7) : (px & 7);
            const uint32_t ty = (entry & 0x800) ? 7 - (py & 7) : (py & 7);
            const uint8_t index = vram8(e, e->bg_area, chars + (entry & 0x3ffu) * 64u + ty * 8u + tx);
            if (index != 0) {
                out[x] = (uint16_t)(OPAQUE | (slot >= 0 ? ext_color(e, slot, entry >> 12, index)
                                                        : palette(e->bg_pal, index)));
            }
            break;
        }
        case BITMAP256:
        case LARGE_BITMAP: {
            const uint8_t index = vram8(e, e->bg_area,
                                        (kind == BITMAP256 ? bitmap : 0) + (uint32_t)(py * w + px));
            if (index != 0) {
                out[x] = (uint16_t)(OPAQUE | palette(e->bg_pal, index));
            }
            break;
        }
        default: {
            const uint16_t color = vram16(e, e->bg_area, bitmap + (uint32_t)(py * w + px) * 2u);
            if (color & 0x8000) {  /* bit 15 is the alpha flag */
                out[x] = color;
            }
            break;
        }
        }
    }
}

/* Which renderer each BG uses in each mode; -1: none. */
static int bg_kind(const Engine *e, int bg)
{
    const unsigned mode = e->dispcnt & 7;
    const uint16_t cnt = rd16(e->io + 0x08 + 2 * bg);
    static const int8_t table[8][4] = {
        {0, 0, 0, 0},    /* 0: text x4 */
        {0, 0, 0, 1},    /* 1: BG3 affine */
        {0, 0, 1, 1},
        {0, 0, 0, 2},    /* 3: BG3 extended */
        {0, 0, 1, 2},
        {0, 0, 2, 2},
        {0, -1, 3, -1},  /* 6: BG2 large bitmap (engine A) */
        {-1, -1, -1, -1},
    };
    int kind = table[mode][bg];
    if (e->id == 1 && mode >= 6) {
        return -1;  /* reserved on engine B */
    }
    if (kind == 0) {
        return TEXT_BG;
    }
    if (kind == 1) {
        return AFFINE;
    }
    if (kind == 2) {
        if (!(cnt & 0x80)) {
            return AFFINE_EXT;
        }
        return (cnt & 0x04) ? BITMAP_DIRECT : BITMAP256;
    }
    if (kind == 3) {
        return LARGE_BITMAP;
    }
    return -1;
}


/* --- Objects --------------------------------------------------------------- */

enum { OBJ_NORMAL = 0, OBJ_SEMI = 1, OBJ_WINDOW = 2, OBJ_BITMAP = 3 };

typedef struct ObjLine {
    uint16_t color[KHDAYS_SCREEN_W];  /* OPAQUE | BGR555 */
    uint8_t prio[KHDAYS_SCREEN_W];
    uint8_t mode[KHDAYS_SCREEN_W];
    uint8_t alpha[KHDAYS_SCREEN_W];   /* bitmap OBJs: attribute 2 bits 12-15 */
    uint8_t window[KHDAYS_SCREEN_W];
} ObjLine;

static const uint8_t obj_w[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
static const uint8_t obj_h[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};

/* One OBJ texel at (tx, ty) inside it: OPAQUE | BGR555, or 0. */
static uint16_t obj_texel(const Engine *e, uint16_t a0, uint16_t a2, int w, int tx, int ty)
{
    const unsigned mode = (a0 >> 10) & 3;
    const uint32_t tile = a2 & 0x3ff;
    if (mode == OBJ_BITMAP) {
        uint32_t address;
        uint16_t color;
        if (e->dispcnt & 0x40) {  /* 1D; the 256-byte boundary is engine A's only */
            const uint32_t boundary = (e->id == 0 && (e->dispcnt & (1u << 22))) ? 256 : 128;
            address = tile * boundary + (uint32_t)(ty * w + tx) * 2u;
        } else {
            const uint32_t mask = (e->dispcnt & 0x20) ? 0x1f : 0x0f;
            const uint32_t width = (e->dispcnt & 0x20) ? 256 : 128;
            address = (tile & mask) * 0x10u + (tile & ~mask) * 0x80u + ((uint32_t)ty * width + (uint32_t)tx) * 2u;
        }
        color = vram16(e, e->obj_area, address);
        return (color & 0x8000) ? color : 0;
    }
    if (a0 & 0x2000) {  /* 256 colours */
        uint32_t address;
        uint8_t index;
        if (e->dispcnt & 0x10) {
            address = tile * (32u << ((e->dispcnt >> 20) & 3)) +
                      (uint32_t)((ty >> 3) * (w >> 3) + (tx >> 3)) * 64u;
        } else {
            address = (tile & ~1u) * 32u + (uint32_t)(ty >> 3) * 1024u + (uint32_t)(tx >> 3) * 64u;
        }
        index = vram8(e, e->obj_area, address + (uint32_t)(ty & 7) * 8u + (uint32_t)(tx & 7));
        if (index == 0) {
            return 0;
        }
        if (e->dispcnt & (1u << 31)) {
            return (uint16_t)(OPAQUE | (vram16(e, e->obj_ext_area, (a2 >> 12) * 512u + index * 2u) & 0x7fff));
        }
        return (uint16_t)(OPAQUE | palette(e->obj_pal, index));
    } else {
        uint32_t address;
        uint8_t pair, index;
        if (e->dispcnt & 0x10) {
            address = tile * (32u << ((e->dispcnt >> 20) & 3)) +
                      (uint32_t)((ty >> 3) * (w >> 3) + (tx >> 3)) * 32u;
        } else {
            address = tile * 32u + (uint32_t)(ty >> 3) * 1024u + (uint32_t)(tx >> 3) * 32u;
        }
        pair = vram8(e, e->obj_area, address + (uint32_t)(ty & 7) * 4u + (uint32_t)(tx & 7) / 2);
        index = (tx & 1) ? pair >> 4 : pair & 15;
        if (index == 0) {
            return 0;
        }
        return (uint16_t)(OPAQUE | palette(e->obj_pal, (a2 >> 12) * 16u + index));
    }
}

static void objects(const Engine *e, int line, ObjLine *out)
{
    for (int i = 0; i < 128; ++i) {
        const uint8_t *entry = e->oam + 8 * i;
        const uint16_t a0 = rd16(entry), a1 = rd16(entry + 2), a2 = rd16(entry + 4);
        const int affine = (a0 & 0x100) != 0;
        const unsigned mode = (a0 >> 10) & 3;
        const unsigned shape = a0 >> 14, size = a1 >> 14;
        const unsigned prio = (a2 >> 10) & 3;
        int w, h, bw, bh, x, dy;
        int32_t pa = 256, pb = 0, pc = 0, pd = 256;
        if ((!affine && (a0 & 0x200)) || shape == 3) {
            continue;  /* disabled, or the prohibited shape */
        }
        w = obj_w[shape][size];
        h = obj_h[shape][size];
        bw = w;
        bh = h;
        if (affine && (a0 & 0x200)) {
            bw *= 2;
            bh *= 2;
        }
        dy = (line - (a0 & 0xff)) & 0xff;
        if (dy >= bh) {
            continue;
        }
        if (a0 & 0x1000) {
            missing(MISSING_OBJ_MOSAIC, "OBJ mosaic (drawn without it)");
        }
        if (mode == OBJ_BITMAP && (a2 >> 12) != 15) {
            missing(MISSING_BITMAP_OBJ_ALPHA,
                    "a bitmap OBJ's alpha (GBATEK does not give the formula; drawn opaque)");
        }
        x = a1 & 0x1ff;
        if (x >= 256) {
            x -= 512;
        }
        if (affine) {
            const uint8_t *group = e->oam + 32 * ((a1 >> 9) & 31);
            pa = (int16_t)rd16(group + 6);
            pb = (int16_t)rd16(group + 14);
            pc = (int16_t)rd16(group + 22);
            pd = (int16_t)rd16(group + 30);
        }
        for (int dx = 0; dx < bw; ++dx) {
            const int sx = x + dx;
            int tx, ty;
            uint16_t color;
            if (sx < 0 || sx >= KHDAYS_SCREEN_W) {
                continue;
            }
            if (affine) {
                const int32_t rx = dx - bw / 2, ry = dy - bh / 2;
                tx = ((pa * rx + pb * ry) >> 8) + w / 2;
                ty = ((pc * rx + pd * ry) >> 8) + h / 2;
                if (tx < 0 || tx >= w || ty < 0 || ty >= h) {
                    continue;
                }
            } else {
                tx = (a1 & 0x1000) ? w - 1 - dx : dx;
                ty = (a1 & 0x2000) ? h - 1 - dy : dy;
            }
            color = obj_texel(e, a0, a2, w, tx, ty);
            if (color == 0) {
                continue;
            }
            if (mode == OBJ_WINDOW) {
                out->window[sx] = 1;
                continue;
            }
            /* DS priority: attribute 2's priority first, then the OAM index */
            if (!(out->color[sx] & OPAQUE) || prio < out->prio[sx]) {
                out->color[sx] = color;
                out->prio[sx] = (uint8_t)prio;
                out->mode[sx] = (uint8_t)mode;
                out->alpha[sx] = (uint8_t)(a2 >> 12);
            }
        }
    }
}

/* --- Windows --------------------------------------------------------------- */

/* Horizontal extent: X2 = 0 means 256; X1 = X2 = 0 shows no window (GBATEK,
 * "DS Window Glitches"). */
static int window_x(uint16_t winh, int x)
{
    const int x1 = winh >> 8;
    int x2 = winh & 0xff;
    if (x1 == 0 && x2 == 0) {
        return 0;
    }
    if (x2 == 0) {
        x2 = 256;
    }
    if (x1 > x2) {
        missing(MISSING_WINDOW_X1_AFTER_X2,
                "a window with X1 > X2 (not documented for the DS; wrapped around)");
        return x >= x1 || x < x2;
    }
    return x >= x1 && x < x2;
}

/* The vertical flag: set on the line matching Y1, cleared on the line matching
 * Y2, comparing the lower 8 bits of the line counter (0-262). */
static void window_y_step(KhdaysGpu2dState *state, const uint8_t *io, int line)
{
    for (int w = 0; w < 2; ++w) {
        const uint16_t winv = rd16(io + 0x44 + 2 * w);
        if ((line & 0xff) == (winv & 0xff)) {
            state->win_y[w] = 0;
        } else if ((line & 0xff) == (winv >> 8)) {
            state->win_y[w] = 1;
        }
    }
}

/* --- Composition ----------------------------------------------------------- */

static unsigned channel(uint16_t c, int shift)
{
    return (c >> shift) & 31u;
}

static uint16_t blend(uint16_t a, uint16_t b, unsigned eva, unsigned evb)
{
    uint16_t out = 0;
    for (int shift = 0; shift < 15; shift += 5) {
        unsigned v = (channel(a, shift) * eva + channel(b, shift) * evb) >> 4;
        out |= (uint16_t)((v > 31 ? 31 : v) << shift);
    }
    return out;
}

static uint16_t brighten(uint16_t a, unsigned evy)
{
    uint16_t out = 0;
    for (int shift = 0; shift < 15; shift += 5) {
        const unsigned v = channel(a, shift);
        out |= (uint16_t)((v + (((31 - v) * evy) >> 4)) << shift);
    }
    return out;
}

static uint16_t darken(uint16_t a, unsigned evy)
{
    uint16_t out = 0;
    for (int shift = 0; shift < 15; shift += 5) {
        const unsigned v = channel(a, shift);
        out |= (uint16_t)((v - ((v * evy) >> 4)) << shift);
    }
    return out;
}

static unsigned coefficient(unsigned v)
{
    v &= 31;
    return v > 16 ? 16 : v;
}

enum { LAYER_OBJ = 4, LAYER_BACKDROP = 5 };

/* `alpha3d`: when BG0 is the 3D layer, its per-pixel alpha (0-31). */
static void compose_line(const Engine *e, const KhdaysGpu2dState *state,
                         uint16_t bg[4][KHDAYS_SCREEN_W], const int bg_on[4], const ObjLine *obj,
                         const uint8_t *alpha3d, uint16_t *out)
{
    const uint16_t bldcnt = rd16(e->io + 0x50);
    const uint16_t bldalpha = rd16(e->io + 0x52);
    const unsigned eva = coefficient(bldalpha), evb = coefficient(bldalpha >> 8);
    const unsigned evy = coefficient(rd16(e->io + 0x54));
    const unsigned effect = (bldcnt >> 6) & 3;
    const uint16_t winin = rd16(e->io + 0x48), winout = rd16(e->io + 0x4a);
    const int windows = (e->dispcnt >> 13) & 7;
    const uint16_t backdrop = palette(e->bg_pal, 0);
    unsigned bg_prio[4];
    for (int b = 0; b < 4; ++b) {
        bg_prio[b] = rd16(e->io + 0x08 + 2 * b) & 3;
    }
    for (int x = 0; x < KHDAYS_SCREEN_W; ++x) {
        unsigned mask = 0x3f;
        int layer[2] = {LAYER_BACKDROP, LAYER_BACKDROP};
        uint16_t color[2] = {backdrop, backdrop};
        int found = 0;
        uint16_t result;
        if (windows) {
            mask = winout & 0x3f;
            if ((windows & 4) && obj->window[x]) {
                mask = (winout >> 8) & 0x3f;
            }
            if ((windows & 2) && state->win_y[1] && window_x(rd16(e->io + 0x42), x)) {
                mask = (winin >> 8) & 0x3f;
            }
            if ((windows & 1) && state->win_y[0] && window_x(rd16(e->io + 0x40), x)) {
                mask = winin & 0x3f;
            }
        }
        for (unsigned p = 0; p < 4 && found < 2; ++p) {
            if ((obj->color[x] & OPAQUE) && obj->prio[x] == p && (mask & 0x10)) {
                layer[found] = LAYER_OBJ;
                color[found++] = obj->color[x] & 0x7fff;
            }
            for (int b = 0; b < 4 && found < 2; ++b) {
                if (bg_on[b] && bg_prio[b] == p && (bg[b][x] & OPAQUE) && (mask & (1u << b))) {
                    layer[found] = b;
                    color[found++] = bg[b][x] & 0x7fff;
                }
            }
        }
        result = color[0];
        if (layer[0] == 0 && alpha3d != NULL && alpha3d[x] < 31 && ((bldcnt >> 8) & (1u << layer[1]))) {
            /* the 3D layer over a 2nd target blends by its own alpha; GBATEK:
             * "probably EVA=A/2, EVB=16-A/2", regardless of BLDALPHA and, it
             * says with doubt, of the window's effect flag */
            result = blend(color[0], color[1], alpha3d[x] / 2u, 16u - alpha3d[x] / 2u);
        } else if (mask & 0x20) {
            const int second = (bldcnt >> 8) & (1u << layer[1]);
            if (layer[0] == LAYER_OBJ && (obj->mode[x] == OBJ_SEMI || obj->mode[x] == OBJ_BITMAP) &&
                second) {
                result = blend(color[0], color[1], eva, evb);
            } else if (bldcnt & (1u << layer[0])) {
                if (effect == 1 && second) {
                    result = blend(color[0], color[1], eva, evb);
                } else if (effect == 2) {
                    result = brighten(color[0], evy);
                } else if (effect == 3) {
                    result = darken(color[0], evy);
                }
            }
        }
        out[x] = result;
    }
}

/* --- The frame --------------------------------------------------------------- */

static uint32_t expand(unsigned c5)
{
    /* 5 to 6 bits as GBATEK gives it for the 3D engine: X*2+1, zero stays zero */
    return c5 ? c5 * 2 + 1 : 0;
}

/* The master brightness, on the 6-bit intensities (GBATEK, MASTER_BRIGHT).
 * Returns the three 6-bit channels, R in bits 0-5, G 8-13, B 16-21. */
static uint32_t bright(uint16_t color, uint16_t master)
{
    const unsigned factor = coefficient(master);
    const unsigned mode = master >> 14;
    uint32_t out = 0;
    for (int n = 0; n < 3; ++n) {
        uint32_t c = expand(channel(color, 5 * n));
        if (mode == 1) {
            c += (63 - c) * factor / 16;
        } else if (mode == 2) {
            c -= c * factor / 16;
        }
        out |= c << (8 * n);
    }
    return out;
}

static uint32_t to_rgb888(uint32_t c6)
{
    const uint32_t r = c6 & 63, g = (c6 >> 8) & 63, b = (c6 >> 16) & 63;
    return ((r << 2) | (r >> 4)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 2) | (b >> 4));
}

static uint16_t to_555(uint32_t c6)
{
    return (uint16_t)(((c6 >> 1) & 31) | ((c6 >> 9) & 31) << 5 | ((c6 >> 17) & 31) << 10);
}

static int32_t reference(const uint8_t *p)
{
    return (int32_t)(rd32(p) << 4) >> 4;  /* 28-bit signed, 8 fractional bits */
}

void khdays_gpu2d_frame(const KhdaysGpuInput *in, KhdaysGpu2dState *state, int engine,
                        uint32_t *out, uint16_t *out555)
{
    Engine e;
    uint16_t master;
    unsigned display;
    int32_t ref[2][2];
    const uint16_t mosaic = rd16(in->io + 0x4c + (engine ? 0x1000 : 0));
    const int mosaic_w = (mosaic & 15) + 1, mosaic_h = ((mosaic >> 4) & 15) + 1;

    memset(&e, 0, sizeof(e));
    e.in = in;
    e.id = engine;
    e.io = in->io + (engine ? 0x1000 : 0);
    e.bg_pal = in->palette + (engine ? 0x400 : 0);
    e.obj_pal = e.bg_pal + 0x200;
    e.oam = in->oam + (engine ? 0x400 : 0);
    e.bg_area = engine ? KHDAYS_AREA_BBG : KHDAYS_AREA_ABG;
    e.obj_area = engine ? KHDAYS_AREA_BOBJ : KHDAYS_AREA_AOBJ;
    e.bg_ext_area = engine ? KHDAYS_AREA_BBGEXT : KHDAYS_AREA_ABGEXT;
    e.obj_ext_area = engine ? KHDAYS_AREA_BOBJEXT : KHDAYS_AREA_AOBJEXT;
    e.dispcnt = rd32(e.io);
    if (engine == 0) {
        e.char_base = ((e.dispcnt >> 24) & 7) * 0x10000u;
        e.screen_base = ((e.dispcnt >> 27) & 7) * 0x10000u;
    }
    master = rd16(e.io + 0x6c);
    display = (e.dispcnt >> 16) & 3;
    if (engine == 1) {
        display &= 1;
    }
    if (engine == 0 && (rd32(in->io + 0x64) & 0x80000000u)) {
        missing(MISSING_CAPTURE, "display capture");
    }
    for (int bg = 0; bg < 2; ++bg) {
        ref[bg][0] = reference(e.io + 0x28 + 0x10 * bg);
        ref[bg][1] = reference(e.io + 0x2c + 0x10 * bg);
    }

    for (int line = 0; line < KHDAYS_SCREEN_H; ++line) {
        uint16_t color[KHDAYS_SCREEN_W];
        window_y_step(state, e.io, line);
        if (display == 0 || (display == 1 && (e.dispcnt & 0x80))) {
            /* display off, or forced blank: white */
            for (int x = 0; x < KHDAYS_SCREEN_W; ++x) {
                if (out555) out555[line * KHDAYS_SCREEN_W + x] = 0x7fff;
                out[line * KHDAYS_SCREEN_W + x] = 0xffffff;
            }
            continue;
        }
        if (display == 2) {
            const uint8_t *bank = in->bank[(e.dispcnt >> 18) & 3];
            for (int x = 0; x < KHDAYS_SCREEN_W; ++x) {
                color[x] = rd16(bank + 2 * (line * KHDAYS_SCREEN_W + x)) & 0x7fff;
            }
        } else if (display == 3) {
            missing(MISSING_MAIN_MEMORY_DISPLAY, "the main memory display mode (drawn black)");
            memset(color, 0, sizeof(color));
        } else {
            static uint16_t bg[4][KHDAYS_SCREEN_W];
            static ObjLine obj;
            static uint8_t alpha3d[KHDAYS_SCREEN_W];
            const int is3d = engine == 0 && (e.dispcnt & 0x08);
            int bg_on[4];
            memset(bg, 0, sizeof(bg));
            memset(alpha3d, 31, sizeof(alpha3d));
            memset(&obj, 0, sizeof(obj));
            for (int b = 0; b < 4; ++b) {
                const uint16_t cnt = rd16(e.io + 0x08 + 2 * b);
                const int kind = bg_kind(&e, b);
                const int mosaic_on = (cnt & 0x40) != 0;
                const int src_line = mosaic_on ? line - line % mosaic_h : line;
                bg_on[b] = (e.dispcnt & (0x100u << b)) && kind >= 0;
                if (!bg_on[b]) {
                    continue;
                }
                if (b == 0 && engine == 0 && (e.dispcnt & 0x08)) {
                    /* the 3D layer: scrolled horizontally over 512 dots, the
                     * second 256 transparent */
                    const uint32_t hofs = rd16(e.io + 0x10) & 0x1ff;
                    if (in->layer3d != NULL) {
                        for (int x = 0; x < KHDAYS_SCREEN_W; ++x) {
                            const uint32_t sx = ((uint32_t)x + hofs) & 511;
                            uint32_t p;
                            if (sx >= 256) continue;
                            p = in->layer3d[line * KHDAYS_SCREEN_W + sx];
                            if ((p >> 24) & 31) {
                                bg[0][x] = (uint16_t)(OPAQUE | ((p >> 1) & 31) | (((p >> 9) & 31) << 5) |
                                                      (((p >> 17) & 31) << 10));
                                alpha3d[x] = (uint8_t)((p >> 24) & 31);
                            }
                        }
                    }
                    continue;
                }
                if (kind == TEXT_BG) {
                    text_bg(&e, b, src_line, bg[b]);
                } else {
                    const int n = b - 2;
                    const int32_t pb = (int16_t)rd16(e.io + 0x22 + 0x10 * n);
                    const int32_t pd = (int16_t)rd16(e.io + 0x26 + 0x10 * n);
                    affine_bg(&e, b, kind, ref[n][0] + pb * src_line, ref[n][1] + pd * src_line, bg[b]);
                }
                if (mosaic_on && mosaic_w > 1) {
                    for (int x = 0; x < KHDAYS_SCREEN_W; ++x) {
                        bg[b][x] = bg[b][x - x % mosaic_w];
                    }
                }
            }
            if (e.dispcnt & 0x1000) {
                objects(&e, line, &obj);
            }
            compose_line(&e, state, bg, bg_on, &obj, is3d ? alpha3d : NULL, color);
        }
        for (int x = 0; x < KHDAYS_SCREEN_W; ++x) {
            const uint32_t c6 = bright(color[x], master);
            if (out555) out555[line * KHDAYS_SCREEN_W + x] = to_555(c6);
            out[line * KHDAYS_SCREEN_W + x] = to_rgb888(c6);
        }
    }
    /* the rest of the frame, for the windows' vertical flags */
    for (int line = KHDAYS_SCREEN_H; line < 263; ++line) {
        window_y_step(state, e.io, line);
    }
}
