// The port's own 2D: one screen at N times the DS's resolution, each pixel composed from its frame
// description by port/gfx/compose.h (the code the CPU runs too): the 2D of the DS pixel it lies in,
// the 3D of its own place. In widescreen engine A's screen is KD_WIDE columns wider each side.

#include "../gfx/frame.h"

// Eight buffers, the most SDL's GPU API binds to a pass, read by their bytes (raster3d.hlsl says why):
ByteAddressBuffer desc : register(t0, space0);       // KHDAYS_DESC_WORDS, then the palette: the layers' 256 colours, the sprites'
ByteAddressBuffer bg_mem : register(t1, space0);     // the layers' memory, then its map of tiles drawn instead (runtime/mods/tiles2d.h)
ByteAddressBuffer obj_mem : register(t2, space0);    // the sprites', then theirs
ByteAddressBuffer bgext_mem : register(t3, space0);  // the extended palettes
ByteAddressBuffer objext_mem : register(t4, space0);
ByteAddressBuffer vram_mem : register(t5, space0);   // the bank a VRAM display shows
ByteAddressBuffer layer3d_mem : register(t6, space0); // the 3D at 1x (256x192), for source3d 1
ByteAddressBuffer hd : register(t7, space0);         // the replacements (host/render/gpu_hd.h)

[[vk::image_format("r32ui")]] RWTexture2D<uint> color3d : register(u0, space1); // raster3d's, for source3d 2
[[vk::image_format("r32ui")]] RWTexture2D<uint> attr3d : register(u1, space1);
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> out_screen : register(u2, space1);
// the HUD's 3D drawn apart at N (gpu_raster3d.h), when the 3D is drawn coarser than the 2D
[[vk::image_format("r32ui")]] RWTexture2D<uint> hud_color3d : register(u3, space1);
[[vk::image_format("r32ui")]] RWTexture2D<uint> hud_attr3d : register(u4, space1);
// the pass at the DS's size's verdicts (PRE_*): for DS dot (x, y), at (2x, y) its verdict, at (2x + 1, y) its colour
[[vk::image_format("r32ui")]] RWTexture2D<uint> pre : register(u5, space1);

cbuffer Params : register(b0, space2) {
    uint scale;         // N
    uint width;         // the screen and raster3d's targets, pixels
    uint height;
    uint source3d;      // 0 none, 1 layer3d_mem at 1x, 2 raster3d's targets at N x
    uint disp3dcnt;
    uint swap_params;
    uint clear_color;   // CLEAR_COLOR
    uint clear_depth;   // CLEAR_DEPTH
    uint fog_color;     // FOG_COLOR
    uint fog_offset;    // FOG_OFFSET
    uint fine;          // 1: the 3D's channels have 8 bits, and the screen is composed in 8 (KDF_FINE)
    uint offset3d;      // source3d 2: the 3D's columns left of the DS's (the one picture's, as wide as it)
    uint4 edge_colors;  // EDGE_COLOR, two entries per uint
    uint4 fog_table[2]; // FOG_TABLE, four entries per uint
    uint scale3d;       // source3d 2: the 3D's N (`scale` but when the 2D is drawn finer than the 3D)
    uint width3d;       // its targets, texels
    uint height3d;
    uint smooth3d;      // 1: the 3D read between its texels (smooth scaling)
    uint hud3d;         // 1: the HUD's 3D apart, over the rest
    uint hud_width;     // its targets, texels (as wide as the 3D's at N)
    uint hud_height;
    uint part;          // the DS dot the dispatch starts at, in eighths (x in bits 0-11, y in 12-23), and
                        // the pass in bits 24-25 (PASS_*)
};

#define FINISH3D_SCALED
#include "finish3d.hlsli"

// `part`'s pass. The screen at the DS's size first, a thread a DS dot composed at its first place, which
// leaves each dot a verdict; then a thread an output pixel, composed only where the verdict says its place
// can matter. docs/RENDERING.md, "The 2D once a DS dot".
#define PASS_EACH 0u // every output pixel composed, no verdicts
#define PASS_DOTS 1u // the pass at the DS's size, writing `pre`
#define PASS_READ 2u // the output pixels, by `pre`
#define PRE_EACH 0u    // composed at each output pixel: a replacement's or element's texel, or the 3D, is in it
#define PRE_SAME 1u    // the same at every place in the DS dot: the colour composed once
#define PRE_NOTHING 2u // the one-screen picture's source with only the 3D there: none of the 2D shows

static uint g_fx, g_fy; // the output pixel's place inside its DS pixel
static bool g_place_used;  // KD_PLACE_USED said: the place inside the DS pixel can decide the colour
static bool g_3d_used;     // the 3D was asked for
static bool g_only_3d;     // KD_STACK_SEEN: no layer but the first, and no sprite, lay at the DS pixel
static uint g_3d_x, g_3d_value; // the 3D read once before the composition (read3d_once): its column, its value
static int g_3d_row;

// The 3D at (x, row) as its source holds it. Called once in the shader (read3d_once): it is inlined wherever it
// is called, several finished texels each time, and compose.h asks for the 3D in five places.
uint layer3d_read(uint x, int row)
{
    if (source3d == 1u) {
        return layer3d_mem.Load(((uint)row * 256u + x) * 4u);
    }
    if (source3d == 2u && scale3d == scale) {
        return finished3d((int)((x + offset3d) * scale + g_fx), (int)((uint)row * scale + g_fy));
    }
    if (source3d == 2u) {
        // the 2D finer than the 3D: the 3D at this output pixel's middle, the HUD's drawn apart over it
        const uint world =
            finished3d_at(((float)(x + offset3d) + ((float)g_fx + 0.5f) / (float)scale) * (float)scale3d,
                          ((float)row + ((float)g_fy + 0.5f) / (float)scale) * (float)scale3d);
        if (hud3d == 0u) {
            return world;
        }
        return over3d(finish3d_texel(hud_color3d, hud_attr3d, (int)hud_width, (int)hud_height,
                                     (int)((x + offset3d) * scale + g_fx), (int)((uint)row * scale + g_fy)),
                      world);
    }
    return 0u;
}

// compose.h's KD_3D: the value read3d_once read, the only place the composition asks for (the DS pixel's 3D)
uint layer3d_at(uint x, int row)
{
    g_3d_used = true;
    return x == g_3d_x && row == g_3d_row ? g_3d_value : 0u;
}

#define KD_FN
#define KD_VOID
#define KD_PLACE_USED() (g_place_used = true)
#define KD_STACK_SEEN(color, spr)                                                                    \
    (g_only_3d = (color)[1] == 0u && (color)[2] == 0u && (color)[3] == 0u && (spr).color == 0u && \
                 (spr).window == 0u)
#define KD_DESC(i) desc.Load((i) * 4u)
#define KD_BG8(a) ((bg_mem.Load(((a) >> 2) * 4u) >> (((a) & 3u) * 8u)) & 0xffu)
#define KD_BG16(a) ((bg_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_OBJ8(a) ((obj_mem.Load(((a) >> 2) * 4u) >> (((a) & 3u) * 8u)) & 0xffu)
#define KD_OBJ16(a) ((obj_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_BGEXT16(a) ((bgext_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_OBJEXT16(a) ((objext_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_PAL16(i) ((desc.Load((KHDAYS_DESC_WORDS + ((i) >> 1)) * 4u) >> (((i) & 1u) * 16u)) & 0xffffu)
#define KD_VRAM16(a) ((vram_mem.Load(((a) >> 2) * 4u) >> (((a) & 2u) * 8u)) & 0xffffu)
#define KD_3D(x, row) layer3d_at((x), (row))
#define KD_3D_KNOWN (source3d != 0u)
// the maps after the memories: engine A's layers' 512 KB and sprites' 256 KB, engine B's 128 KB each
#define KD_HD_BG(u) bg_mem.Load((((KD_DESC(KD_FLAGS) & KDF_ENGINE_B) != 0u ? 0x8000u : 0x20000u) + (u)) * 4u)
#define KD_HD_OBJ(u) obj_mem.Load((((KD_DESC(KD_FLAGS) & KDF_ENGINE_B) != 0u ? 0x8000u : 0x10000u) + (u)) * 4u)
#define KD_HD_FIRST(id) hd.Load((id) * 16u)
#define KD_HD_WIDTH(id) hd.Load(((id) * 4u + 1u) * 4u)
#define KD_HD_HEIGHT(id) hd.Load(((id) * 4u + 2u) * 4u)
#define KD_HD_INFO(id) hd.Load(((id) * 4u + 3u) * 4u)
#define KD_HD_TEXEL(i) hd.Load((i) * 4u)
#define KD_SUB_X (((float)g_fx + 0.5f) / (float)scale)
#define KD_SUB_Y (((float)g_fy + 0.5f) / (float)scale)
#define KD_PIXEL (1.0f / (float)scale) // an output pixel's size, in DS pixels
// the port's elements (gfx/elements.h): after the description and its palettes; their images are
// replacements (host/render/gpu_hd.h), their texels where the replacement's table puts them
#define KD_ELEMENT(i) desc.Load((KHDAYS_DESC_WORDS + 256u + (i)) * 4u)
#define KD_EL_TEXEL(image, i) hd.Load((hd.Load((image) * 16u) + (i)) * 4u)
#define KD_ASFLOAT(u) asfloat(u)
#define KD_LEFT_AT (KHDAYS_DESC_WORDS + 256u + KD_DESC(KD_ELEMENTS) * KE_WORDS)
#define KD_LEFT_OUT_CELL(c) (KD_DESC(KD_LEFT_OUT) != 0u ? (desc.Load((KD_LEFT_AT + ((c) >> 5)) * 4u) >> ((c) & 31u)) & 1u : 0u)
#define KD_LEFT_OUT_OBJ(i) (KD_DESC(KD_LEFT_OUT) != 0u ? (desc.Load((KD_LEFT_AT + KE_LEFT_CELL_WORDS + ((i) >> 5)) * 4u) >> ((i) & 31u)) & 1u : 0u)

#include "../gfx/compose.h"

float channel8(uint c6)
{
    return (float)((c6 << 2) | (c6 >> 4)) / 255.0f; // as to_rgb888
}

// The 3D the composition of (x, row) asks for, read before it: the column kd_layer, kd_alpha3d, kd_fine3d and
// kd_moved3d ask for, or kd_pixel_wide's in its wider target; 0 outside the 3D's columns. Read even when the
// composition then has no use for it (a forced blank): one read, wherever it is asked for.
void read3d_once(int x, int row)
{
    uint column;
    bool inside;
    if (KD_DESC(KD_WIDE) != 0u) {
        const uint hofs = KD_DESC(KD_HOFS3D);
        const int target = x + (hofs < 256u ? (int)hofs : (int)hofs - 512);
        column = (uint)target;
        inside = target >= 0 && target < 256 + 2 * (int)KD_DESC(KD_WIDE);
    } else {
        column = ((uint)x + KD_DESC(KD_HOFS3D)) & 511u;
        inside = column < 256u;
    }
    g_3d_x = column;
    g_3d_row = row;
    g_3d_value = inside && source3d != 0u ? layer3d_read(column, row) : 0u;
}

// Whether a DS dot of the one-screen picture's source (kd_split) composed with only the 3D at it gives 0 at
// every place: the 3D or the backdrop is on top wherever it is looked at, neither of which the source shows.
// Not where the backdrop is a blend's second target: a translucent 3D is then blended over it by its alpha.
bool nothing_shown()
{
    const uint flags = KD_DESC(KD_FLAGS);
    return g_only_3d && !g_place_used && (flags & KDF_SPLIT) != 0u && (flags & KDF_DISPLAY_MASK) == 1u &&
           (flags & KDF_FORCED_BLANK) == 0u && (flags & KDF_BG0_IS_3D) != 0u &&
           (flags & (1u << KDF_BG_ON_SHIFT)) != 0u && KD_DESC(KD_WIDE) == 0u &&
           ((KD_DESC(KD_BLEND) >> 8) & (1u << KD_LAYER_BACKDROP)) == 0u;
}

// The composition is called in one place, whatever the pass: each call is the whole of compose.h inlined.
[numthreads(8, 8, 1)]
void cs_main(uint3 dispatched : SV_DispatchThreadID)
{
    const uint pass = (part >> 24) & 3u;
    // the dispatch covers the screen from this DS dot (only a part of it composed: gfx_render.c one_reads)
    const uint2 origin = uint2(part & 0xfffu, (part >> 12) & 0xfffu) * 8u;
    const uint2 id = dispatched.xy + (pass == PASS_DOTS ? origin : origin * scale);
    uint c6, verdict = PRE_EACH;
    int x, row;
    if (pass == PASS_DOTS) {
        if (id.x >= width / scale || id.y >= height / scale) {
            return;
        }
        g_fx = 0u;
        g_fy = 0u;
        x = (int)id.x;
        row = (int)id.y;
    } else {
        if (id.x >= width || id.y >= height) {
            return;
        }
        g_fx = id.x % scale;
        g_fy = id.y % scale;
        x = (int)(id.x / scale);
        row = (int)(id.y / scale);
        if (pass == PASS_READ) {
            verdict = pre[uint2((uint)x * 2u, (uint)row)];
            if (verdict == PRE_NOTHING) {
                out_screen[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
                return;
            }
        }
    }
    g_place_used = false;
    g_3d_used = false;
    g_only_3d = false;
    if (verdict == PRE_SAME) {
        c6 = pre[uint2((uint)x * 2u + 1u, (uint)row)];
    } else {
        read3d_once(x, row);
        c6 = KD_DESC(KD_WIDE) != 0u ? kd_pixel_wide(x, row) : kd_pixel(x, row);
    }
    if (pass == PASS_DOTS) {
        pre[uint2(id.x * 2u, id.y)] = !g_place_used && !g_3d_used ? PRE_SAME : nothing_shown() ? PRE_NOTHING : PRE_EACH;
        pre[uint2(id.x * 2u + 1u, id.y)] = c6;
        return;
    }
    if ((KD_DESC(KD_FLAGS) & KDF_SPLIT) != 0u) {
        // the one-screen picture's source: K in 8 bits, its alpha 0 (not shown over
        // the 3D) or 1 + the 3D's factor x 254 (kd_split)
        out_screen[id] = float4((float)(c6 & 255u) / 255.0f, (float)((c6 >> 8) & 255u) / 255.0f,
                                   (float)((c6 >> 16) & 255u) / 255.0f, (float)(c6 >> 24) / 255.0f);
        return;
    }
    if ((KD_DESC(KD_FLAGS) & KDF_FINE) != 0u) {
        out_screen[id] = float4((float)(c6 & 255u) / 255.0f, (float)((c6 >> 8) & 255u) / 255.0f,
                                   (float)((c6 >> 16) & 255u) / 255.0f, 1.0f);
        return;
    }
    out_screen[id] = float4(channel8(c6 & 63u), channel8((c6 >> 8) & 63u), channel8((c6 >> 16) & 63u), 1.0f);
}
