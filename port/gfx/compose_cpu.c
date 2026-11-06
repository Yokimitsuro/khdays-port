/* The 2D composition on the CPU (see describe.h): compose.h run over the
 * game's memory as the game maps its banks. It is used for the check against
 * the reference renderer and for the tests; the GPU runs the same code
 * (port/shaders/compose2d.hlsl). */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "describe.h"

#include <math.h>
#include <string.h>

typedef uint32_t uint;

/* Each thread's own: the game's thread checks the parts, the
 * host's composes the pixels the one-screen picture tells the situation from. */
#if defined(_MSC_VER)
#define KD_THREAD __declspec(thread)
#else
#define KD_THREAD _Thread_local
#endif
static KD_THREAD const uint32_t *kd_desc;
static KD_THREAD const KhdaysGfxMemory *kd_memory;
static KD_THREAD int kd_bg_area, kd_obj_area, kd_bgext_area, kd_objext_area;

static uint kd_read8(int area, uint address)
{
    const uint8_t *page = kd_memory->vram->page[area][address / KHDAYS_VRAM_PAGE];
    return page != NULL ? page[address % KHDAYS_VRAM_PAGE] : 0;
}

static uint kd_read16(int area, uint address)
{
    const uint8_t *page = kd_memory->vram->page[area][address / KHDAYS_VRAM_PAGE];
    return page != NULL ? (uint)(page[address % KHDAYS_VRAM_PAGE] | page[address % KHDAYS_VRAM_PAGE + 1] << 8) : 0;
}

static uint kd_pal16(uint index)
{
    const uint8_t *p = kd_memory->palette + ((kd_desc[KD_FLAGS] & KDF_ENGINE_B) ? 0x400 : 0) + 2 * index;
    return (uint)(p[0] | p[1] << 8);
}

static uint kd_vram16(uint address)
{
    const uint8_t *bank = kd_memory->bank[kd_desc[KD_VRAM] & 3];
    return bank != NULL ? (uint)(bank[address] | bank[address + 1] << 8) : 0;
}

static uint kd_3d(uint x, int line)
{
    return kd_memory->layer3d != NULL ? kd_memory->layer3d[line * 256 + (int)x] : 0;
}

/* The port's elements: their records, their images' texels and
 * what is left out, from the memory the caller gives. */
static uint kd_element(uint i)
{
    return kd_memory->elements != NULL ? kd_memory->elements[i] : 0;
}

static uint kd_el_texel(uint image, uint i)
{
    const uint32_t *texels = kd_memory->element_texels != NULL ? kd_memory->element_texels(image) : NULL;
    return texels != NULL ? texels[i] : 0;
}

static float kd_asfloat(uint u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static uint kd_left_out_cell(uint cell)
{
    return kd_memory->left_out_cells != NULL ? (kd_memory->left_out_cells[cell >> 3] >> (cell & 7)) & 1u : 0u;
}

static uint kd_left_out_obj(uint i)
{
    return kd_memory->left_out_obj != NULL ? (kd_memory->left_out_obj[i >> 3] >> (i & 7)) & 1u : 0u;
}

#define KD_FN static inline
#define KD_VOID void
#define KD_DESC(i) (kd_desc[(i)])
#define KD_BG8(a) kd_read8(kd_bg_area, (a))
#define KD_BG16(a) kd_read16(kd_bg_area, (a))
#define KD_OBJ8(a) kd_read8(kd_obj_area, (a))
#define KD_OBJ16(a) kd_read16(kd_obj_area, (a))
#define KD_BGEXT16(a) kd_read16(kd_bgext_area, (a))
#define KD_OBJEXT16(a) kd_read16(kd_objext_area, (a))
#define KD_PAL16(i) kd_pal16((i))
#define KD_VRAM16(a) kd_vram16((a))
#define KD_3D(x, row) kd_3d((x), (row))
#define KD_3D_KNOWN (kd_memory->layer3d != NULL)
/* no tiles drawn instead: the DS's picture (kd_hd_none is 0, not a constant
 * the compiler sees through) */
#define KD_HD_BG(u) (kd_hd_none + 0u * (u))
#define KD_HD_OBJ(u) (kd_hd_none + 0u * (u))
#define KD_HD_FIRST(id) (0u * (id))
#define KD_HD_WIDTH(id) (0u * (id))
#define KD_HD_HEIGHT(id) (0u * (id))
#define KD_HD_INFO(id) (0u * (id))
#define KD_HD_TEXEL(i) (0u * (i))
#define KD_SUB_X 0.0f
#define KD_SUB_Y 0.0f
#define KD_PIXEL 1.0f
#define KD_ELEMENT(i) kd_element((i))
#define KD_EL_TEXEL(image, i) kd_el_texel((image), (i))
#define KD_ASFLOAT(u) kd_asfloat((u))
#define KD_LEFT_OUT_CELL(c) kd_left_out_cell((c))
#define KD_LEFT_OUT_OBJ(i) kd_left_out_obj((i))
uint32_t kd_hd_none;

#include "compose.h"

static uint32_t to_rgb888(uint32_t c6)
{
    const uint32_t r = c6 & 63, g = (c6 >> 8) & 63, b = (c6 >> 16) & 63;
    return ((r << 2) | (r >> 4)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 2) | (b >> 4));
}

static void use(const uint32_t *desc, const KhdaysGfxMemory *memory)
{
    const int b = (desc[KD_FLAGS] & KDF_ENGINE_B) != 0;
    kd_memory = memory;
    kd_bg_area = b ? KHDAYS_AREA_BBG : KHDAYS_AREA_ABG;
    kd_obj_area = b ? KHDAYS_AREA_BOBJ : KHDAYS_AREA_AOBJ;
    kd_bgext_area = b ? KHDAYS_AREA_BBGEXT : KHDAYS_AREA_ABGEXT;
    kd_objext_area = b ? KHDAYS_AREA_BOBJEXT : KHDAYS_AREA_AOBJEXT;
    kd_desc = desc;
}

/* How many descriptions a cell is remembered as part of a piece across the middle after it last was: a
 * piece that blinks part of itself keeps its place while it does. LOCKED ON!'s words are off 20 to 24
 * descriptions at a time, its icon staying left of the middle; without this the icon went to the left
 * edge each time. A piece that only shrinks to one side follows it this much later. */
#define MIDDLE_KEPT 40

void khdays_gfx_describe_wide(uint32_t desc[KHDAYS_DESC_WORDS], const KhdaysGfxMemory *memory, int columns)
{
    static uint8_t covered[24][32], seen[24][32];
    static int8_t side[24][32];
    static int stack[24 * 32];
    /* the descriptions since each cell was last in a piece across the middle (MIDDLE_KEPT or more: not
     * lately), and this description's pieces across it */
    static uint8_t since_middle[24][32], across[24][32];
    static int middle_started;
    const uint32_t flags = desc[KD_FLAGS];
    desc[KD_WIDE] = 0;
    memset(desc + KD_SIDES, 0, 48 * sizeof(uint32_t));
    if (columns <= 256 || (flags & KDF_ENGINE_B) || (flags & KDF_DISPLAY_MASK) != 1 || (flags & KDF_FORCED_BLANK) ||
        !(flags & KDF_BG0_IS_3D) || !(flags & (1u << KDF_BG_ON_SHIFT))) {
        middle_started = 0; /* the pieces seen before are forgotten */
        return;
    }
    use(desc, memory);
    if (!middle_started) {
        memset(since_middle, MIDDLE_KEPT, sizeof(since_middle));
        middle_started = 1;
    }
    memset(covered, 0, sizeof(covered));
    memset(seen, 0, sizeof(seen));
    memset(side, 0, sizeof(side));
    memset(across, 0, sizeof(across));
    /* the cells layers 1-3 cover */
    for (int cy = 0; cy < 24; ++cy) {
        for (int cx = 0; cx < 32; ++cx) {
            for (int i = 0; i < 64 && !covered[cy][cx]; ++i) {
                const int x = cx * 8 + (i & 7), y = cy * 8 + (i >> 3);
                for (int b = 1; b < 4; ++b) {
                    if ((flags & (1u << (KDF_BG_ON_SHIFT + b))) && (kd_layer(b, x, y) & KD_OPAQUE)) {
                        covered[cy][cx] = 1;
                        break;
                    }
                }
            }
        }
    }
    /* the pieces: each group of covered cells that touch, and its side */
    for (int cy = 0; cy < 24; ++cy) {
        for (int cx = 0; cx < 32; ++cx) {
            int top = 0, x0 = cx, x1 = cx;
            int8_t s;
            if (!covered[cy][cx] || seen[cy][cx]) {
                continue;
            }
            seen[cy][cx] = 1;
            stack[top++] = cy * 32 + cx;
            for (int i = 0; i < top; ++i) {
                static const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                const int px = stack[i] % 32, py = stack[i] / 32;
                x0 = px < x0 ? px : x0;
                x1 = px > x1 ? px : x1;
                for (int k = 0; k < 4; ++k) {
                    const int nx = px + dx[k], ny = py + dy[k];
                    if (nx >= 0 && nx < 32 && ny >= 0 && ny < 24 && covered[ny][nx] && !seen[ny][nx]) {
                        seen[ny][nx] = 1;
                        stack[top++] = ny * 32 + nx;
                    }
                }
            }
            s = x1 < 16 ? -1 : x0 >= 16 ? 1 : 0;
            for (int i = 0; i < top; ++i) {
                across[stack[i] / 32][stack[i] % 32] = s == 0;
            }
            /* a piece on one side with cells in a piece across the middle a moment ago is taken for what is
             * left of that piece (the rest of it blinked off): it stays in the middle rather than jump to the
             * edge. (Only a piece across the middle itself renews that, so a piece kept here is not kept for
             * good.) */
            for (int i = 0; i < top && s != 0; ++i) {
                if (since_middle[stack[i] / 32][stack[i] % 32] < MIDDLE_KEPT) {
                    s = 0;
                }
            }
            for (int i = 0; i < top; ++i) {
                side[stack[i] / 32][stack[i] % 32] = s;
            }
        }
    }
    for (int cell = 0; cell < 24 * 32; ++cell) {
        uint8_t *since = &since_middle[cell / 32][cell % 32];
        if (across[cell / 32][cell % 32]) {
            *since = 0;
        } else if (*since < MIDDLE_KEPT) {
            ++*since;
        }
    }
    for (int cell = 0; cell < 24 * 32; ++cell) {
        const int8_t s = side[cell / 32][cell % 32];
        if (s != 0) {
            desc[KD_SIDES + cell / 16] |= (uint32_t)(s < 0 ? 1 : 2) << ((cell & 15) * 2);
        }
    }
    desc[KD_WIDE] = (uint32_t)(columns - 256) / 2;
}

void khdays_gfx_compose_cpu(const uint32_t desc[KHDAYS_DESC_WORDS], const KhdaysGfxMemory *memory, uint32_t *out)
{
    khdays_gfx_compose_cpu_rect(desc, memory, out, 0, 0, 256, 192);
}

void khdays_gfx_compose_cpu_rect(const uint32_t desc[KHDAYS_DESC_WORDS], const KhdaysGfxMemory *memory, uint32_t *out,
                                 int x0, int y0, int x1, int y1)
{
    static KD_THREAD uint32_t line_desc[KHDAYS_DESC_WORDS];
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > 256 ? 256 : x1;
    y1 = y1 > 192 ? 192 : y1;
    memcpy(line_desc, desc, sizeof(line_desc));
    use(line_desc, memory);
    for (int line = y0; line < y1; ++line) {
        /* only the sprites on this line: the same pixels, fewer to look at */
        for (int i = 0; i < 128; ++i) {
            const uint32_t *s = desc + KD_SPRITES + i * KD_SPRITE_WORDS;
            const uint32_t box = s[KDS_BOX];
            const uint32_t dy = ((uint32_t)line - (box & 0xff)) & 0xff;
            line_desc[KD_SPRITES + i * KD_SPRITE_WORDS + KDS_SIZE] =
                dy < ((box >> 16) & 0xff) ? s[KDS_SIZE] : s[KDS_SIZE] & ~KDS_SHOWN;
        }
        for (int x = x0; x < x1; ++x) {
            out[line * 256 + x] = to_rgb888(kd_pixel(x, line));
        }
    }
}

uint32_t khdays_gfx_pixel_cpu(const uint32_t desc[KHDAYS_DESC_WORDS], const KhdaysGfxMemory *memory, int x, int y)
{
    if (x < 0 || y < 0 || x >= 256 || y >= 192) {
        return 0;
    }
    use(desc, memory);
    return kd_pixel(x, y);
}
