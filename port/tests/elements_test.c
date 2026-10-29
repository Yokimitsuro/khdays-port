/* The port's screen elements in the composition, on the CPU: fixed screens (a BG layer with a tile, and
 * the elements standing in for it) composed by port/gfx/compose.h as the game's frames are.
 *   ctest --test-dir build/port/obj-x64 -R khdays-elements --output-on-failure */
#include "../gfx/describe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

#define W 256
#define H 192

static uint8_t io[0x2000];
static uint8_t oam[0x800]; /* every sprite's tile 0, which is empty */
static uint8_t palette[0x800];
static uint8_t bg_a[0x80000], obj_a[0x40000], bg_b[0x20000];
static KhdaysVramPages pages;
static uint32_t desc[KHDAYS_DESC_WORDS];
static KhdaysGfxLatch latch;
static uint32_t base_out[W * H], out[W * H];

/* the element images: texels by KE_IMAGE */
static uint32_t *images[8];

static const uint32_t *texels(uint32_t image)
{
    return image < 8 ? images[image] : NULL;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* Engine A: the layers shown, BG1 a text layer at priority 1 (its tiles at
 * 0x4000, its map at 0xf800) with tile 1 -- every pixel colour 1, red -- at
 * cell (2, 3), the DS pixels 16-23 x 24-31; colour 2 green, 3 blue. */
static void scene(void)
{
    memset(io, 0, sizeof(io));
    memset(palette, 0, sizeof(palette));
    memset(bg_a, 0, sizeof(bg_a));
    memset(obj_a, 0, sizeof(obj_a));
    memset(bg_b, 0, sizeof(bg_b));
    put32(io, 0x00010000u | 0x200u | 0x1000u | 0x10u); /* display the layers; BG1 and OBJ on; OBJ 1D */
    put16(io + 0x0a, 1u | (1u << 2) | (31u << 8));     /* BG1CNT: priority 1, chars 0x4000, map 0xf800 */
    put16(palette + 2, 0x001f);                        /* red */
    put16(palette + 4, 0x03e0);                        /* green */
    put16(palette + 6, 0x7c00);                        /* blue */
    put16(palette + 0x200 + 2, 0x7fff);                /* sprite colour 1: white */
    memset(bg_a + 0x4000 + 32, 0x11, 32);              /* tile 1 */
    put16(bg_a + 0xf800 + (3 * 32 + 2) * 2, 1);        /* the map's cell (2, 3) */
    /* engine B: its BG0 the same way */
    put32(io + 0x1000, 0x00010000u | 0x100u);
    put16(io + 0x1008, (1u << 2) | (31u << 8));
    put16(palette + 0x400 + 2, 0x7c00); /* engine B's colour 1: blue */
    memset(bg_b + 0x4000 + 32, 0x11, 32);
    put16(bg_b + 0xf800 + (5 * 32 + 4) * 2, 1); /* cell (4, 5): 32-39 x 40-47 */
    for (int i = 0; i < 32; ++i) {
        pages.page[KHDAYS_AREA_ABG][i] = bg_a + i * KHDAYS_VRAM_PAGE;
    }
    for (int i = 0; i < 16; ++i) {
        pages.page[KHDAYS_AREA_AOBJ][i] = obj_a + i * KHDAYS_VRAM_PAGE;
    }
    for (int i = 0; i < 8; ++i) {
        pages.page[KHDAYS_AREA_BBG][i] = bg_b + i * KHDAYS_VRAM_PAGE;
    }
}

static void memory_of(KhdaysGfxMemory *m, const uint32_t *elements, const uint8_t *left_cells)
{
    memset(m, 0, sizeof(*m));
    m->vram = &pages;
    m->palette = palette;
    m->elements = elements;
    m->element_texels = texels;
    m->left_out_cells = left_cells;
}

static void compose(int engine, const uint32_t *elements, uint32_t count, const uint8_t *left_cells, uint32_t *to)
{
    KhdaysGfxMemory m;
    memset(&latch, 0, sizeof(latch));
    khdays_gfx_describe(io, palette, oam, engine, &latch, desc);
    desc[KD_ELEMENTS] = count;
    memory_of(&m, elements, left_cells);
    khdays_gfx_compose_cpu(desc, &m, to);
}

/* An indexed image of w x h texels, each `index` (0: transparent). */
static uint32_t *image(int n, int w, int h, uint32_t index)
{
    free(images[n]);
    images[n] = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    for (int i = 0; i < w * h; ++i) {
        images[n][i] = index != 0 ? KET_OPAQUE | index : 0;
    }
    return images[n];
}

static void element(uint32_t *r, uint32_t slot, float x, float y, float w, float h, int img, int iw, int ih)
{
    r[KE_SLOT] = slot;
    r[KE_X] = fbits(x);
    r[KE_Y] = fbits(y);
    r[KE_W] = fbits(w);
    r[KE_H] = fbits(h);
    r[KE_IMAGE] = (uint32_t)img;
    r[KE_IW] = (uint32_t)iw;
    r[KE_IH] = (uint32_t)ih;
}

/* the cell showing tile 1, (2, 3) of BG1's map, left out */
static uint8_t left_tile[KE_LEFT_CELL_WORDS * 4];

static int differences_outside(const uint32_t *a, const uint32_t *b, int x0, int y0, int x1, int y1, int x2, int y2,
                               int x3, int y3)
{
    int n = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int in_a = x >= x0 && x < x1 && y >= y0 && y < y1;
            const int in_b = x >= x2 && x < x3 && y >= y2 && y < y3;
            if (!in_a && !in_b && a[y * W + x] != b[y * W + x]) {
                ++n;
            }
        }
    }
    return n;
}

int main(void)
{
    uint32_t r[KE_WORDS * 2];
    memset(left_tile, 0, sizeof(left_tile));
    left_tile[(1 * KE_CELLS + 3 * 64 + 2) >> 3] |= (uint8_t)(1u << ((1 * KE_CELLS + 3 * 64 + 2) & 7));

    /* the DS's layout: the element over what it stands for gives the frame without it */
    scene();
    compose(0, NULL, 0, NULL, base_out);
    CHECK((base_out[24 * W + 16] & 0xff0000u) != 0 && (base_out[24 * W + 16] & 0xffffu) == 0); /* red */
    image(1, 8, 8, 1);
    element(r, 1, 16, 24, 8, 8, 1, 8, 8);
    compose(0, r, 1, left_tile, out);
    CHECK(memcmp(out, base_out, sizeof(out)) == 0);

    /* its image at 4x: the same at 1x */
    image(2, 32, 32, 1);
    element(r, 1, 16, 24, 8, 8, 2, 32, 32);
    compose(0, r, 1, left_tile, out);
    CHECK(memcmp(out, base_out, sizeof(out)) == 0);

    /* moved: only its old place and its new one change */
    element(r, 1, 100, 50, 8, 8, 1, 8, 8);
    compose(0, r, 1, left_tile, out);
    CHECK(differences_outside(out, base_out, 16, 24, 24, 32, 100, 50, 108, 58) == 0);
    CHECK(out[24 * W + 16] == base_out[0]);            /* its old place: the backdrop */
    CHECK(out[50 * W + 100] == base_out[24 * W + 16]); /* its new one: red */

    /* scaled twice over: 16x16 */
    element(r, 1, 100, 50, 16, 16, 1, 8, 8);
    compose(0, r, 1, left_tile, out);
    CHECK(out[65 * W + 115] == base_out[24 * W + 16]);
    CHECK(out[66 * W + 116] == base_out[0]);

    /* a transparent texel shows the layer under it */
    image(3, 8, 8, 0);
    element(r, 1, 16, 24, 8, 8, 3, 8, 8);
    compose(0, r, 1, NULL, out);
    CHECK(memcmp(out, base_out, sizeof(out)) == 0);

    /* the topmost first: green over red */
    image(4, 8, 8, 2);
    element(r, 1, 16, 24, 8, 8, 4, 8, 8);
    element(r + KE_WORDS, 1, 16, 24, 8, 8, 1, 8, 8);
    compose(0, r, 2, left_tile, out);
    CHECK((out[24 * W + 16] & 0x00ff00u) != 0 && (out[24 * W + 16] & 0xff0000u) == 0);

    /* brightened (BLDCNT BG1 first target, EVY 8), faded (master brightness),
     * a window over half the screen: still the frame without the element */
    scene();
    put16(io + 0x50, 0x0002u | (2u << 6));
    put16(io + 0x54, 8);
    put16(io + 0x6c, (1u << 14) | 6u);
    put32(io, 0x00010000u | 0x200u | 0x2000u);
    put16(io + 0x40, (0u << 8) | 20u);  /* WIN0H: 0-19 */
    put16(io + 0x44, (0u << 8) | 192u); /* WIN0V */
    put16(io + 0x48, 0x0002u);          /* WININ: BG1 inside window 0 */
    put16(io + 0x4a, 0x0000u);          /* WINOUT: nothing */
    compose(0, NULL, 0, NULL, base_out);
    image(1, 8, 8, 1);
    element(r, 1, 16, 24, 8, 8, 1, 8, 8);
    compose(0, r, 1, left_tile, out);
    CHECK(memcmp(out, base_out, sizeof(out)) == 0);

    /* in the sprites' slot, priority 0: drawn with the sprites' palette */
    scene();
    compose(0, NULL, 0, NULL, base_out);
    element(r, KES_SPRITES | 0u, 40, 40, 8, 8, 1, 8, 8);
    compose(0, r, 1, NULL, out);
    CHECK(out[40 * W + 40] != base_out[40 * W + 40] && (out[40 * W + 40] & 0xff) != 0 &&
          (out[40 * W + 40] & 0xff00) != 0);
    CHECK(differences_outside(out, base_out, 40, 40, 48, 48, 0, 0, 0, 0) == 0);

    /* the lower screen (engine B) */
    scene();
    compose(1, NULL, 0, NULL, base_out);
    CHECK((base_out[40 * W + 32] & 0xff) != 0);
    {
        uint8_t left_b[KE_LEFT_CELL_WORDS * 4];
        memset(left_b, 0, sizeof(left_b));
        left_b[(5 * 64 + 4) >> 3] |= (uint8_t)(1u << ((5 * 64 + 4) & 7)); /* BG0's cell (4, 5) */
        element(r, 0, 32, 40, 8, 8, 1, 8, 8);
        compose(1, r, 1, left_b, out);
        CHECK(memcmp(out, base_out, sizeof(out)) == 0);
    }

    /* A native mission-panel image remains visible on engine B's BG2 in dual-screen mode. */
    scene();
    put32(io + 0x1000, 0x00010000u | 0x700u);
    put16(io + 0x100a, 1u | (1u << 2) | (30u << 8));
    put16(io + 0x100c, 2u | (1u << 2) | (29u << 8));
    image(1, 8, 8, 0);
    for (int i = 0; i < 64; ++i)
        images[1][i] = 0xff00ff00u;
    element(r, 2 | KES_DIRECT | KES_PICTURE | KES_FINE, 16, 152, 8, 8, 1, 8, 8);
    compose(1, r, 1, NULL, out);
    CHECK(out[152 * W + 16] != out[0]);

    _Static_assert((KES_FINE & KES_PICTURE) == 0u, "image sampling and placement flags must be independent");
    /* Independent images are drawn separately from the one-screen source crop. */
    scene();
    image(1, 8, 8, 1);
    element(r, 1 | KES_PICTURE | KES_HUD, 40, 40, 8, 8, 1, 8, 8);
    compose(0, r, 1, NULL, out);
    CHECK(out[40 * W + 40] != out[0]);
    desc[KD_FLAGS] |= KDF_SPLIT | KDF_FINE;
    {
        KhdaysGfxMemory m;
        memory_of(&m, r, NULL);
        CHECK(khdays_gfx_pixel_cpu(desc, &m, 40, 40) == 0);
        r[KE_SLOT] &= ~KES_PICTURE;
        r[KE_SLOT] |= KES_FINE;
        CHECK(khdays_gfx_pixel_cpu(desc, &m, 40, 40) != 0);
    }
    for (int i = 0; i < 8; ++i)
        free(images[i]);
    if (failures) {
        fprintf(stderr, "%d failed\n", failures);
        return 1;
    }
    printf("elements: all passed\n");
    return 0;
}
