/* The port's own 2D, composed on the graphics card (gfx_render.h). Each frame uploads both screens'
 * descriptions and palettes, the 3D at 1x and the 2D memory pages that changed; compose2d.hlsl makes
 * each screen at N x, wrapped as a texture of the window's renderer; on one screen onescreen.hlsl
 * then makes the single picture from both. */
#include "gfx_render.h"
#include "gpu_hd.h"
#include "kh2_sequence_gpu.h"
#include "gpu_raster3d.h"
#include "../../runtime/display/frame_package.h"
#include "generated/capture_cs.h"
#include "generated/compose2d_cs.h"
#include "generated/onescreen_cs.h"
#include "generated/shrink_cs.h"
#include "../../gfx/onescreen.h"

#include <stdio.h>
#include <string.h>

#define PAGE KHDAYS_FRAME_VRAM_PAGE

/* compose2d.hlsl's constant buffer, register for register. */
typedef struct Compose2dParams {
    uint32_t scale, width, height, source3d, disp3dcnt, swap_params, clear_color, clear_depth;
    uint32_t fog_color, fog_offset, fine, offset3d;
    uint32_t edge_colors[4];
    uint32_t fog_table[8];
} Compose2dParams;

/* onescreen.hlsl's constant buffer, register for register. */
typedef struct OneScreenParams {
    uint32_t scale, width, height, has3d, disp3dcnt, swap_params, clear_color, clear_depth;
    uint32_t fog_color, fog_offset, fine, shape_count;
    uint32_t edge_colors[4];
    uint32_t fog_table[8];
    uint32_t frame, rule, masters, backdrop;
    uint32_t a_upper, backdrop_w, backdrop_h, hide3d;
    float backdrop_scale, backdrop_x, backdrop_y; /* picture texels per its texel, its top-left */
    uint32_t piece_count;                         /* an animated picture's pieces this frame */
    uint32_t animated, unused[3];                 /* 1: an animated picture (`backdrop` its ground) */
} OneScreenParams;

/* The areas of the package's 2D memory, each in a buffer of its own. */
enum { ABG, AOBJ, BBG, BOBJ, ABGEXT, AOBJEXT, BBGEXT, BOBJEXT, VRAM, AREAS };
static const struct {
    int first, pages;
} area[AREAS] = {
    {KHDAYS_2D_ABG, 32},    {KHDAYS_2D_AOBJ, 16},  {KHDAYS_2D_BBG, 8},     {KHDAYS_2D_BOBJ, 8}, {KHDAYS_2D_ABGEXT, 2},
    {KHDAYS_2D_AOBJEXT, 1}, {KHDAYS_2D_BBGEXT, 2}, {KHDAYS_2D_BOBJEXT, 1}, {KHDAYS_2D_VRAM, 8},
};

static SDL_GPUDevice *device;
static SDL_GPUComputePipeline *compose;
/* Eight buffers per pass, the most SDL's GPU API binds. The layers' and sprites' memory buffers also
 * hold the map of their replaced (HD) tiles; each description buffer also holds its palettes. */
static SDL_GPUBuffer *memory[AREAS], *desc[2], *layer3d;
static uint32_t hd2d_version[KHDAYS_FRAME_HD2D_PAGES]; /* the change counts of the card's copy */
#define HD2D_PAGE (KHDAYS_FRAME_HD2D_PAGE_WORDS * 4u)
#define DESC_BYTES (KHDAYS_DESC_WORDS * 4u + 0x400u) /* the description, then the palettes */
/* ... then the screen's elements (gfx/elements.h); each buffer grows to the most any frame needed */
static uint32_t desc_bytes[2];

static uint32_t element_bytes(const KhdaysFramePackage *p, int e)
{
    /* ... and the parts left out (KD_LEFT_OUT) */
    const uint32_t left = p->desc2d[e][KD_LEFT_OUT] != 0 ? (KE_LEFT_CELL_WORDS + KE_LEFT_OBJ_WORDS) * 4u : 0u;
    return p->elements[e].count * KE_WORDS * 4u + left;
}

/* The size of area `a`'s buffer: its pages and, for the layers' and the
 * sprites' memory, a 2 KB map page for each page. */
static Uint32 area_bytes(int a)
{
    return (Uint32)area[a].pages * (PAGE + (a <= BOBJ ? HD2D_PAGE : 0u));
}
static SDL_GPUTexture *dummy3d[2]; /* bound in place of the 3D targets at 1x */
static SDL_GPUTexture *screen[2];
static int screen_scale, screen_columns[2]; /* their scale, and each one's width in DS columns */
static SDL_Texture *wrapped[2];
static SDL_Renderer *wrapped_for[2];
static SDL_GPUTransferBuffer *upload;
static uint32_t upload_size;
/* The one-screen picture: its shader, its shapes and keys (each buffer grows to the most any frame
 * needed), and the picture. */
static SDL_GPUComputePipeline *onescreen;
static SDL_GPUBuffer *shape_buffer, *key_buffer, *piece_buffer;
static uint32_t shape_buffer_bytes, key_buffer_bytes, piece_buffer_bytes;
#define PIECE_BYTES 128u /* ui/composition.h KhdaysPiece: KHDAYS_PIECE_WORDS words */
static SDL_GPUTexture *picture;
static int picture_w, picture_h, picture_columns;
static SDL_Texture *picture_wrapped;
static SDL_Renderer *picture_for;
/* The composition's picture behind everything (`backdrop`), uploaded once, and a texel bound in
 * its place when there is none. */
static SDL_GPUTexture *backdrop_texture, *no_backdrop;
static const uint8_t *backdrop_of;
static int backdrop_texture_w, backdrop_texture_h;
/* The 3D on both screens (Gfx_SetupSubScreenCapture): each game frame the DS captures engine A's
 * picture, and the next one engine B shows it on the other screen. The port's capture is at the DS's
 * size (display.c), so for that screen this keeps engine A's picture of the previous package when the
 * capture was in use, with the screen it was on, and remembers the last package drawn. */
static SDL_GPUTexture *held;
static int held_w, held_h, held_columns, held_screen, held_ok;
static SDL_Texture *held_wrapped;
static SDL_Renderer *held_for;
static uint32_t drawn_frame;
static int drawn_any, drawn_capture, drawn_upper;
/* The screen blends (a cut between an event's camera shots, Ov023_CmdStartScreenBlend): engine A shows a
 * VRAM bank while its picture, blended with that bank, is captured back into it. The port's capture is at
 * the DS's size, so each one is taken again here from what the window shows, and the bank shown from
 * that copy while the game's bank still holds the same capture (display.c package_capture). */
typedef struct HdBank {
    SDL_GPUTexture *texture;
    int w, h;
    uint32_t captures; /* the game's count of captures into the bank when this copy was taken */
    int ok;
} HdBank;
static SDL_GPUComputePipeline *capturer;
static HdBank hd_bank[4], hd_scratch, hd_source;
/* What the window shows: the screens and the picture made larger than shown (the antialiasing, or the
 * internal resolution rounded up) are shrunk on the card (shrink.hlsl) to `shown` window pixels a DS
 * pixel, into these. The 3D's edges come out smooth, and the window shows the picture at its own
 * pixels rather than a smaller one stretched. */
typedef struct Shrunk {
    SDL_GPUTexture *texture;
    int w, h;
    SDL_Texture *wrapped;
    SDL_Renderer *wrapped_for;
} Shrunk;
static SDL_GPUComputePipeline *shrinker;
static float shown; /* the window's pixels a DS pixel; 0: as made */
static Shrunk shrunk_screen[2], shrunk_picture;
static int screens_shrunk, picture_shrunk;      /* the last draw's were */
static uint32_t version[KHDAYS_FRAME_2D_PAGES]; /* the change counts of the card's copy */
static int valid;

static SDL_GPUBuffer *make_buffer(Uint32 size)
{
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    info.size = size;
    return SDL_CreateGPUBuffer(device, &info);
}

static SDL_GPUTexture *make_texture(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int w, int h)
{
    SDL_GPUTextureCreateInfo info;
    SDL_zero(info);
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = (Uint32)w;
    info.height = (Uint32)h;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(device, &info);
}

static SDL_GPUComputePipeline *make_pipeline_of(const unsigned char *dxil, unsigned dxil_size,
                                                const unsigned char *spirv, unsigned spirv_size,
                                                const unsigned char *msl, unsigned msl_size, const char *entry,
                                                int buffers, int textures)
{
    SDL_GPUComputePipelineCreateInfo info;
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    SDL_zero(info);
    if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.code = dxil;
        info.code_size = dxil_size;
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.code = spirv;
        info.code_size = spirv_size;
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = msl;
        info.code_size = msl_size - 1; /* the text without its final zero */
        info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else {
        SDL_SetError("the device takes none of the shader formats this build carries");
        return NULL;
    }
    info.entrypoint = entry;
    info.num_readonly_storage_buffers = (Uint32)buffers;
    info.num_readwrite_storage_textures = (Uint32)textures;
    info.num_uniform_buffers = 1;
    info.threadcount_x = 8;
    info.threadcount_y = 8;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

static SDL_GPUComputePipeline *make_pipeline(void)
{
    return make_pipeline_of(compose2d_cs_dxil, compose2d_cs_dxil_size, compose2d_cs_spirv, compose2d_cs_spirv_size,
                            compose2d_cs_msl, compose2d_cs_msl_size, COMPOSE2D_CS_ENTRY, 8, 3);
}

static void drop_shrunk(Shrunk *s)
{
    if (s->wrapped != NULL)
        SDL_DestroyTexture(s->wrapped);
    if (s->texture != NULL)
        SDL_ReleaseGPUTexture(device, s->texture);
    SDL_zerop(s);
}

static void drop_hd_bank(HdBank *b)
{
    if (b->texture != NULL)
        SDL_ReleaseGPUTexture(device, b->texture);
    SDL_zerop(b);
}

/* Makes sure `b` has a texture of w x h (its contents are not kept when it is made again). */
static int ensure_hd_bank(HdBank *b, int w, int h)
{
    if (b->texture != NULL && b->w == w && b->h == h) {
        return 1;
    }
    drop_hd_bank(b);
    b->texture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                              SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, w, h);
    b->w = b->texture != NULL ? w : 0;
    b->h = b->texture != NULL ? h : 0;
    return b->texture != NULL;
}

static void copy_texture(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *from, SDL_GPUTexture *to, int w, int h)
{
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureLocation a, b;
    SDL_zero(a);
    SDL_zero(b);
    a.texture = from;
    b.texture = to;
    SDL_CopyGPUTextureToTexture(copy, &a, &b, (Uint32)w, (Uint32)h, 1, false);
    SDL_EndGPUCopyPass(copy);
}

/* What a package asks of the screen blends: the bank engine A shows from its copy (-1 none), that copy
 * made now from this frame (`seed`), the capture taken again into bank `capture` (-1 none), reading bank
 * `source_b` (-1 none), with EVA and EVB. */
typedef struct CapturePlan {
    int show, seed, capture, source_b;
    uint32_t eva, evb;
} CapturePlan;

static void plan_capture(const KhdaysFramePackage *p, CapturePlan *plan)
{
    const uint32_t flags = p->desc2d[0][KD_FLAGS], cnt = p->dispcapcnt, display = flags & KDF_DISPLAY_MASK;
    const int bank = (int)(p->desc2d[0][KD_VRAM] & 3u); /* engine A's VRAM block: shown, and source B */
    const HdBank *copy = &hd_bank[bank];
    const int intact = (p->bank_intact >> bank) & 1u, plain = p->bank_plain[bank] != 0u;
    int current, seed;
    plan->show = plan->capture = plan->source_b = -1;
    plan->seed = 0;
    plan->eva = plan->evb = 0;
    /* engine A shows its layers or a bank (the VRAM display mode), its 3D in BG0 for the capture to
     * take; a blend's first capture comes while it still shows its layers */
    if (capturer == NULL || (display != 1u && display != 2u) || (flags & KDF_FORCED_BLANK) ||
        !(flags & KDF_BG0_IS_3D) || !(flags & (1u << KDF_BG_ON_SHIFT))) {
        return;
    }
    /* the copy is of this blend: taken at its first capture or after, though the window may have missed
     * some since (a package it never drew); else, when the bank holds that first capture alone (engine A's
     * picture a VBlank before), it is made again from this frame */
    current = intact && plain && copy->ok && copy->captures >= p->bank_plain[bank] &&
              copy->captures <= p->bank_captures[bank];
    seed = !current && intact && plain && p->bank_plain[bank] == p->bank_captures[bank];
    if (display == 2u && (current || seed)) {
        plan->show = bank;
        plan->seed = seed;
    }
    /* a whole screen (256 x 192) at offset 0, of engine A's layers: not its 3D alone, nor the main
     * memory display's FIFO */
    if ((cnt & 0x80000000u) && ((cnt >> 20) & 3u) == 3u && ((cnt >> 18) & 3u) == 0u && ((cnt >> 26) & 3u) == 0u &&
        !(cnt & (3u << 24))) {
        const uint32_t mode = (cnt >> 29) & 3u; /* 0 source A, 1 source B, 2-3 both */
        const uint32_t eva = (cnt & 31u) > 16u ? 16u : cnt & 31u,
                       evb = ((cnt >> 8) & 31u) > 16u ? 16u : (cnt >> 8) & 31u;
        plan->eva = mode == 0 ? 16u : mode == 1 ? 0u : eva;
        plan->evb = mode == 0 ? 0u : mode == 1 ? 16u : evb;
        if (plan->evb == 0u || plan->show == bank || current) {
            plan->capture = (int)((cnt >> 16) & 3u);
            plan->source_b = plan->evb != 0u ? bank : -1;
        }
    }
}

/* Whether engine A is composed as its layers and 3D though it shows a bank: the capture takes them,
 * or the bank's copy covers them. */
static int plan_layers(const KhdaysFramePackage *p, const CapturePlan *plan)
{
    return (p->desc2d[0][KD_FLAGS] & KDF_DISPLAY_MASK) == 2u && (plan->show >= 0 || plan->capture >= 0);
}

int khdays_gfx_render_capture_frame(const KhdaysFramePackage *p)
{
    CapturePlan plan;
    plan_capture(p, &plan);
    return plan_layers(p, &plan);
}

/* KHDAYS_TRACE_CAPTURE: what each screen blend's frame shows and takes. */
static int trace_capture(void)
{
    static int on = -1;
    if (on < 0) {
        on = SDL_getenv("KHDAYS_TRACE_CAPTURE") != NULL;
    }
    return on;
}

/* After engine A was composed as its layers into `target` (w x h): the bank it shows over it from the
 * copy, and the capture taken again from what was composed. */
static void take_capture(SDL_GPUCommandBuffer *cmd, const KhdaysFramePackage *p, const CapturePlan *plan,
                         SDL_GPUTexture *target, int w, int h)
{
    int show = plan->show;
    SDL_GPUTexture *a = target;
    HdBank *dest = plan->capture >= 0 ? &hd_bank[plan->capture] : NULL;
    if (show >= 0 && (plan->seed || hd_bank[show].w != w || hd_bank[show].h != h)) {
        /* the copy made from this frame (what was composed already shows it): the bank holds engine A's
         * picture of a VBlank before, or its copy is of another size */
        HdBank *made = &hd_bank[show];
        made->ok = ensure_hd_bank(made, w, h);
        if (made->ok) {
            copy_texture(cmd, target, made->texture, w, h);
            made->captures = p->bank_captures[show];
        }
        show = -1;
    }
    if (dest != NULL && plan->source_b >= 0 &&
        (!hd_bank[plan->source_b].ok || hd_bank[plan->source_b].w != w || hd_bank[plan->source_b].h != h)) {
        dest->ok = 0; /* source B has no copy of this size */
        dest = NULL;
    }
    if (dest != NULL && show >= 0) {
        /* source A, before the bank shown covers it */
        if (ensure_hd_bank(&hd_source, w, h)) {
            copy_texture(cmd, target, hd_source.texture, w, h);
            a = hd_source.texture;
        } else {
            dest->ok = 0;
            dest = NULL;
        }
    }
    if (show >= 0) {
        copy_texture(cmd, hd_bank[show].texture, target, w, h);
    }
    if (trace_capture()) {
        fprintf(stderr, "capture: frame %u, %dx%d, bank %d %s, takes into %d (EVA %u, EVB %u, source B %d)\n", p->frame,
                w, h, plan->show,
                show >= 0         ? "shown from its copy"
                : plan->show >= 0 ? "shown, its copy made from this frame"
                                  : "not shown",
                dest != NULL ? plan->capture : -1, plan->eva, plan->evb, plan->source_b);
    }
    if (dest != NULL && ensure_hd_bank(&hd_scratch, w, h)) {
        struct {
            Uint32 width, height, eva, evb;
        } params = {(Uint32)w, (Uint32)h, plan->eva, plan->evb};
        SDL_GPUStorageTextureReadWriteBinding io[3];
        SDL_GPUComputePass *pass;
        HdBank done;
        SDL_zeroa(io);
        io[0].texture = a;                                                                   /* read only */
        io[1].texture = plan->source_b >= 0 ? hd_bank[plan->source_b].texture : no_backdrop; /* read only */
        io[2].texture = hd_scratch.texture;
        pass = SDL_BeginGPUComputePass(cmd, io, 3, NULL, 0);
        SDL_BindGPUComputePipeline(pass, capturer);
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
        SDL_DispatchGPUCompute(pass, (Uint32)(w + 7) / 8, (Uint32)(h + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
        /* the blend becomes the bank's copy; its old texture the next scratch */
        done = hd_scratch;
        hd_scratch = *dest;
        *dest = done;
        dest->captures = p->bank_captures[plan->capture] + 1u;
        dest->ok = 1;
    } else if (dest != NULL) {
        dest->ok = 0;
    }
}

/* `big` (w x h, `columns` DS columns wide) shrunk to the size shown into `s`, made again when its
 * size changes; in `cmd`. 0 when it is not shown smaller than made, or the texture could not be
 * made. */
static int shrink(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *big, int w, int h, int columns, Shrunk *s)
{
    const int sw = SDL_min((int)SDL_lroundf((float)columns * shown), w);
    const int sh = SDL_min((int)SDL_lroundf((float)KHDAYS_FRAME_H * shown), h);
    struct {
        Uint32 width, height, big_width, big_height;
    } params = {(Uint32)sw, (Uint32)sh, (Uint32)w, (Uint32)h};
    if (sw < 1 || sh < 1 || (sw == w && sh == h)) {
        return 0;
    }
    SDL_GPUStorageTextureReadWriteBinding io[2];
    SDL_GPUComputePass *pass;
    if (s->texture == NULL || s->w != sw || s->h != sh) {
        drop_shrunk(s);
        s->texture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                  SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, sw, sh);
        if (s->texture == NULL) {
            return 0;
        }
        s->w = sw;
        s->h = sh;
    }
    SDL_zeroa(io);
    io[0].texture = big; /* read only */
    io[1].texture = s->texture;
    io[1].cycle = true;
    pass = SDL_BeginGPUComputePass(cmd, io, 2, NULL, 0);
    SDL_BindGPUComputePipeline(pass, shrinker);
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
    SDL_DispatchGPUCompute(pass, (Uint32)(sw + 7) / 8, (Uint32)(sh + 7) / 8, 1);
    SDL_EndGPUComputePass(pass);
    return 1;
}

/* A card texture (w x h) as a texture of `renderer`, made once for it (in `made`, `made_for`). */
static SDL_Texture *wrap(SDL_Renderer *renderer, SDL_GPUTexture *texture, int w, int h, SDL_Texture **made,
                         SDL_Renderer **made_for)
{
    if (*made == NULL || *made_for != renderer) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        if (*made != NULL) {
            SDL_DestroyTexture(*made);
        }
        SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, texture);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ABGR8888);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
        *made = SDL_CreateTextureWithProperties(renderer, props);
        SDL_DestroyProperties(props);
        if (*made == NULL) {
            return NULL;
        }
        SDL_SetTextureBlendMode(*made, SDL_BLENDMODE_NONE);
        *made_for = renderer;
    }
    return *made;
}

void khdays_gfx_render_set_shown(float pixels_per_ds_pixel)
{
    shown = pixels_per_ds_pixel > 0.0f ? pixels_per_ds_pixel : 0.0f;
}

int khdays_gfx_render_open(SDL_GPUDevice *d)
{
    int ok;
    device = d;
    compose = make_pipeline();
    shrinker = make_pipeline_of(shrink_cs_dxil, shrink_cs_dxil_size, shrink_cs_spirv, shrink_cs_spirv_size,
                                shrink_cs_msl, shrink_cs_msl_size, SHRINK_CS_ENTRY, 0, 2);
    onescreen = make_pipeline_of(onescreen_cs_dxil, onescreen_cs_dxil_size, onescreen_cs_spirv, onescreen_cs_spirv_size,
                                 onescreen_cs_msl, onescreen_cs_msl_size, ONESCREEN_CS_ENTRY, 4, 6);
    /* without it the screen blends show the DS's bank, as before */
    capturer = make_pipeline_of(capture_cs_dxil, capture_cs_dxil_size, capture_cs_spirv, capture_cs_spirv_size,
                                capture_cs_msl, capture_cs_msl_size, CAPTURE_CS_ENTRY, 0, 3);
    shape_buffer_bytes = KO_WORDS * 4u * 64u;
    key_buffer_bytes = 256u;
    piece_buffer_bytes = PIECE_BYTES * 64u;
    shape_buffer = make_buffer(shape_buffer_bytes);
    key_buffer = make_buffer(key_buffer_bytes);
    piece_buffer = make_buffer(piece_buffer_bytes);
    no_backdrop = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                               SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, 1, 1);
    ok = compose != NULL && onescreen != NULL && shape_buffer != NULL && key_buffer != NULL && piece_buffer != NULL &&
         no_backdrop != NULL;
    for (int a = 0; a < AREAS; ++a) {
        memory[a] = make_buffer(area_bytes(a));
        ok = ok && memory[a] != NULL;
    }
    for (int e = 0; e < 2; ++e) {
        desc[e] = make_buffer(DESC_BYTES);
        desc_bytes[e] = DESC_BYTES;

        dummy3d[e] = make_texture(SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, 1, 1);
        ok = ok && desc[e] != NULL && dummy3d[e] != NULL;
    }
    layer3d = make_buffer(KHDAYS_FRAME_PIXELS * 4);
    ok = ok && layer3d != NULL && khdays_gpu_hd_open(d);
    ok = ok && khdays_sequence_gpu_open(d);
    valid = 0;
    if (!ok) {
        char why[256];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        khdays_gfx_render_close();
        SDL_SetError("%s", why);
        return 0;
    }
    return 1;
}

static void drop_screens(void)
{
    for (int e = 0; e < 2; ++e) {
        if (wrapped[e] != NULL) {
            SDL_DestroyTexture(wrapped[e]);
            wrapped[e] = NULL;
        }
        wrapped_for[e] = NULL;
        if (screen[e] != NULL) {
            SDL_ReleaseGPUTexture(device, screen[e]);
            screen[e] = NULL;
        }
    }
    screen_scale = 0;
    screen_columns[0] = screen_columns[1] = 0;
}

void khdays_gfx_render_close(void)
{
    khdays_sequence_gpu_close();
    if (device == NULL) {
        return;
    }
    drop_screens();
    drop_shrunk(&shrunk_screen[0]);
    drop_shrunk(&shrunk_screen[1]);
    drop_shrunk(&shrunk_picture);
    screens_shrunk = picture_shrunk = 0;
    if (shrinker != NULL)
        SDL_ReleaseGPUComputePipeline(device, shrinker);
    shrinker = NULL;
    if (held_wrapped != NULL)
        SDL_DestroyTexture(held_wrapped);
    if (held != NULL)
        SDL_ReleaseGPUTexture(device, held);
    held_wrapped = NULL;
    held_for = NULL;
    held = NULL;
    held_w = held_h = held_columns = held_ok = 0;
    drawn_any = 0;
    for (int b = 0; b < 4; ++b) {
        drop_hd_bank(&hd_bank[b]);
    }
    drop_hd_bank(&hd_scratch);
    drop_hd_bank(&hd_source);
    if (capturer != NULL)
        SDL_ReleaseGPUComputePipeline(device, capturer);
    capturer = NULL;
    if (picture_wrapped != NULL)
        SDL_DestroyTexture(picture_wrapped);
    if (picture != NULL)
        SDL_ReleaseGPUTexture(device, picture);
    picture_wrapped = NULL;
    picture_for = NULL;
    picture = NULL;
    picture_w = picture_h = picture_columns = 0;
    if (onescreen != NULL)
        SDL_ReleaseGPUComputePipeline(device, onescreen);
    if (backdrop_texture != NULL)
        SDL_ReleaseGPUTexture(device, backdrop_texture);
    if (no_backdrop != NULL)
        SDL_ReleaseGPUTexture(device, no_backdrop);
    backdrop_texture = no_backdrop = NULL;
    backdrop_of = NULL;
    if (shape_buffer != NULL)
        SDL_ReleaseGPUBuffer(device, shape_buffer);
    if (key_buffer != NULL)
        SDL_ReleaseGPUBuffer(device, key_buffer);
    if (piece_buffer != NULL)
        SDL_ReleaseGPUBuffer(device, piece_buffer);
    piece_buffer = NULL;
    onescreen = NULL;
    shape_buffer = key_buffer = NULL;
    if (compose != NULL)
        SDL_ReleaseGPUComputePipeline(device, compose);
    for (int a = 0; a < AREAS; ++a) {
        if (memory[a] != NULL)
            SDL_ReleaseGPUBuffer(device, memory[a]);
        memory[a] = NULL;
    }
    for (int e = 0; e < 2; ++e) {
        if (desc[e] != NULL)
            SDL_ReleaseGPUBuffer(device, desc[e]);
        if (dummy3d[e] != NULL)
            SDL_ReleaseGPUTexture(device, dummy3d[e]);
        desc[e] = NULL;
        dummy3d[e] = NULL;
    }
    if (layer3d != NULL)
        SDL_ReleaseGPUBuffer(device, layer3d);
    if (upload != NULL)
        SDL_ReleaseGPUTransferBuffer(device, upload);
    compose = NULL;
    layer3d = NULL;
    upload = NULL;
    upload_size = 0;
    device = NULL;
}

static int screens(int scale, int columns_a)
{
    if (scale != screen_scale || columns_a != screen_columns[0]) {
        drop_screens();
        for (int e = 0; e < 2; ++e) {
            const int columns = e == 0 ? columns_a : KHDAYS_FRAME_W;
            screen[e] = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                                     columns * scale, KHDAYS_FRAME_H * scale);
            screen_columns[e] = columns;
            if (screen[e] == NULL) {
                char why[256];
                SDL_strlcpy(why, SDL_GetError(), sizeof(why));
                drop_screens();
                SDL_SetError("%s", why);
                return 0;
            }
        }
        screen_scale = scale;
    }
    return 1;
}

static uint32_t io32(const uint8_t *io3d, unsigned offset)
{
    return (uint32_t)io3d[offset] | (uint32_t)io3d[offset + 1] << 8 | (uint32_t)io3d[offset + 2] << 16 |
           (uint32_t)io3d[offset + 3] << 24;
}

/* Which area a page is in, and its offset there. */
static int area_of(int page, uint32_t *offset)
{
    for (int a = 0; a < AREAS; ++a) {
        if (page >= area[a].first && page < area[a].first + area[a].pages) {
            *offset = (uint32_t)(page - area[a].first) * PAGE;
            return a;
        }
    }
    return -1;
}

/* Makes sure the composition's backdrop is on the card (uploaded the first time it is seen);
 * returns its texture, or the stand-in texel when there is none or it cannot be (said). */
static SDL_GPUTexture *backdrop_on_card(SDL_GPUCommandBuffer *cmd, const KhdaysOneScreenDraw *one)
{
    SDL_GPUTransferBufferCreateInfo info;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dst;
    SDL_GPUCopyPass *copy;
    void *mapped;
    const Uint32 bytes = (Uint32)one->backdrop_w * (Uint32)one->backdrop_h * 4u;
    if (one->backdrop == NULL || one->backdrop_w <= 0 || one->backdrop_h <= 0) {
        return no_backdrop;
    }
    if (one->backdrop == backdrop_of && backdrop_texture_w == one->backdrop_w &&
        backdrop_texture_h == one->backdrop_h) {
        return backdrop_texture != NULL ? backdrop_texture : no_backdrop;
    }
    if (backdrop_texture != NULL) {
        SDL_ReleaseGPUTexture(device, backdrop_texture);
    }
    backdrop_of = one->backdrop;
    backdrop_texture_w = one->backdrop_w;
    backdrop_texture_h = one->backdrop_h;
    backdrop_texture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                    SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE,
                                    one->backdrop_w, one->backdrop_h);
    SDL_zero(info);
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = bytes;
    transfer = backdrop_texture != NULL ? SDL_CreateGPUTransferBuffer(device, &info) : NULL;
    mapped = transfer != NULL ? SDL_MapGPUTransferBuffer(device, transfer, false) : NULL;
    if (mapped == NULL) {
        fprintf(stderr, "one screen: the backdrop (%dx%d) cannot go to the graphics card: %s\n", one->backdrop_w,
                one->backdrop_h, SDL_GetError());
        if (transfer != NULL)
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        if (backdrop_texture != NULL)
            SDL_ReleaseGPUTexture(device, backdrop_texture);
        backdrop_texture = NULL;
        return no_backdrop;
    }
    SDL_memcpy(mapped, one->backdrop, bytes);
    SDL_UnmapGPUTransferBuffer(device, transfer);
    copy = SDL_BeginGPUCopyPass(cmd);
    SDL_zero(src);
    src.transfer_buffer = transfer;
    src.pixels_per_row = (Uint32)one->backdrop_w;
    src.rows_per_layer = (Uint32)one->backdrop_h;
    SDL_zero(dst);
    dst.texture = backdrop_texture;
    dst.w = (Uint32)one->backdrop_w;
    dst.h = (Uint32)one->backdrop_h;
    dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return backdrop_texture;
}

/* Makes sure the one-screen picture's texture exists, (C x N) by (192 x N). */
static int ensure_picture(int columns, int scale)
{
    if (picture != NULL && picture_w == columns * scale && picture_h == KHDAYS_FRAME_H * scale) {
        return 1;
    }
    if (picture_wrapped != NULL) {
        SDL_DestroyTexture(picture_wrapped);
        picture_wrapped = NULL;
        picture_for = NULL;
    }
    if (picture != NULL) {
        SDL_ReleaseGPUTexture(device, picture);
    }
    picture = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                           SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, columns * scale,
                           KHDAYS_FRAME_H * scale);
    picture_w = picture != NULL ? columns * scale : 0;
    picture_h = picture != NULL ? KHDAYS_FRAME_H * scale : 0;
    picture_columns = picture != NULL ? columns : 0;
    return picture != NULL;
}

/* Makes sure a buffer holds at least `need` bytes (doubling; its contents are not kept). */
static int ensure_buffer(SDL_GPUBuffer **buffer, uint32_t *bytes, uint32_t need)
{
    if (need <= *bytes) {
        return 1;
    }
    while (*bytes < need) {
        *bytes *= 2;
    }
    SDL_ReleaseGPUBuffer(device, *buffer);
    *buffer = make_buffer(*bytes);
    return *buffer != NULL;
}

/* When a new package comes (not the same one drawn again between two
 * pictures): keep engine A's picture of the previous one, if it was captured
 * (see above). */
static void hold_capture(const KhdaysFramePackage *p)
{
    if (drawn_any && p->frame == drawn_frame) {
        return;
    }
    held_ok = 0;
    if (drawn_any && drawn_capture && screen[0] != NULL) {
        /* as it was shown: shrunk to the size shown */
        SDL_GPUTexture *const as_shown = screens_shrunk ? shrunk_screen[0].texture : screen[0];
        const int w = screens_shrunk ? shrunk_screen[0].w : screen_columns[0] * screen_scale;
        const int h = screens_shrunk ? shrunk_screen[0].h : KHDAYS_FRAME_H * screen_scale;
        if (held == NULL || held_w != w || held_h != h) {
            if (held_wrapped != NULL)
                SDL_DestroyTexture(held_wrapped);
            if (held != NULL)
                SDL_ReleaseGPUTexture(device, held);
            held_wrapped = NULL;
            held_for = NULL;
            held = make_texture(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h);
            held_w = held != NULL ? w : 0;
            held_h = held != NULL ? h : 0;
        }
        if (held != NULL) {
            SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
            if (cmd != NULL) {
                SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
                SDL_GPUTextureLocation from, to;
                SDL_zero(from);
                SDL_zero(to);
                from.texture = as_shown;
                to.texture = held;
                SDL_CopyGPUTextureToTexture(copy, &from, &to, (Uint32)w, (Uint32)h, 1, false);
                SDL_EndGPUCopyPass(copy);
                held_ok = SDL_SubmitGPUCommandBuffer(cmd);
                held_columns = screen_columns[0];
                held_screen = drawn_upper == 0 ? 0 : 1;
            }
        }
    }
    drawn_frame = p->frame;
    drawn_capture = p->capture;
    drawn_upper = p->upper;
    drawn_any = 1;
}

static int draw(const KhdaysFramePackage *p, int scale, int raster, const KhdaysOneScreenDraw *one)
{
    int changed[KHDAYS_FRAME_2D_PAGES], nchanged = 0;
    int hd_changed[KHDAYS_FRAME_HD2D_PAGES], nhd = 0;
    const int has3d = (p->wants & KHDAYS_WANT_3D) != 0;
    const uint32_t fixed = 2u * KHDAYS_DESC_WORDS * 4u + 2u * 0x400u + (has3d ? KHDAYS_FRAME_PIXELS * 4u : 0u) +
                           element_bytes(p, 0) + element_bytes(p, 1) +
                           (one != NULL ? (uint32_t)one->shape_count * KO_WORDS * 4u + (uint32_t)one->key_count * 4u +
                                              (uint32_t)one->piece_count * PIECE_BYTES
                                        : 0u);
    uint32_t size, at;
    uint8_t *mapped;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *color3d = NULL, *attr3d = NULL;
    int raster_columns = 0;
    /* Engine A is wider than the DS's only when its 3D is drawn as wide. For the one-screen picture its 2D
     * stays at the DS's 256 columns, apart from the 3D drawn as wide as the picture. */
    uint32_t wide = raster && one == NULL ? p->desc2d[0][KD_WIDE] : 0;
    int columns_a;
    const uint32_t shape_bytes = one != NULL ? (uint32_t)one->shape_count * KO_WORDS * 4u : 0u;
    const uint32_t key_bytes = one != NULL ? (uint32_t)one->key_count * 4u : 0u;
    const uint32_t piece_bytes = one != NULL ? (uint32_t)one->piece_count * PIECE_BYTES : 0u;
    /* a screen blend (take_capture); while engine A shows a bank, it is composed as its layers and 3D,
     * as the capture takes it */
    CapturePlan plan;
    int blend, layers;
    plan_capture(p, &plan);
    blend = raster && (plan.show >= 0 || plan.capture >= 0);
    layers = raster && plan_layers(p, &plan);
    if (trace_capture() && (p->desc2d[0][KD_FLAGS] & KDF_DISPLAY_MASK) == 2u && !blend) {
        fprintf(stderr, "capture: frame %u, engine A shows bank %u at the DS's size (%s)\n", p->frame,
                (unsigned)(p->desc2d[0][KD_VRAM] & 3u), raster ? "no current copy of it" : "no 3D drawn");
    }
    if (device != NULL && (p->wants & KHDAYS_WANT_2D)) {
        hold_capture(p); /* before this package's pictures replace the previous ones */
    }
    if (raster && !khdays_raster3d_targets(&color3d, &attr3d, &raster_columns)) {
        SDL_SetError("the 3D at that resolution is not drawn");
        return 0;
    }
    if (layers && one == NULL && raster_columns > KHDAYS_FRAME_W) {
        wide = (uint32_t)(raster_columns - KHDAYS_FRAME_W) / 2u; /* as wide as its 3D, no HUD piece moved */
    }
    columns_a = KHDAYS_FRAME_W + 2 * (int)wide;
    if (raster && raster_columns != (one != NULL ? one->columns : columns_a)) {
        SDL_SetError("the 3D is not as wide as the 2D's description");
        return 0;
    }
    if (device == NULL || !(p->wants & KHDAYS_WANT_2D) || !screens(scale, columns_a)) {
        if (device != NULL && !(p->wants & KHDAYS_WANT_2D)) {
            SDL_SetError("the package has no 2D descriptions");
        }
        return 0;
    }
    if (one != NULL &&
        (!ensure_buffer(&shape_buffer, &shape_buffer_bytes, shape_bytes) ||
         !ensure_buffer(&key_buffer, &key_buffer_bytes, key_bytes) ||
         !ensure_buffer(&piece_buffer, &piece_buffer_bytes, piece_bytes) || !ensure_picture(one->columns, scale))) {
        valid = 0;
        return 0;
    }
    for (int e = 0; e < 2; ++e) {
        /* make room for this frame's elements after the description and its palettes */
        const uint32_t need = DESC_BYTES + element_bytes(p, e);
        if (need > desc_bytes[e]) {
            uint32_t bytes = desc_bytes[e];
            while (bytes < need)
                bytes *= 2;
            SDL_ReleaseGPUBuffer(device, desc[e]);
            desc[e] = make_buffer(bytes);
            desc_bytes[e] = desc[e] != NULL ? bytes : 0;
            if (desc[e] == NULL) {
                valid = 0;
                return 0;
            }
        }
    }
    for (int n = 0; n < KHDAYS_FRAME_2D_PAGES; ++n) {
        if (!valid || version[n] != p->mem2d_version[n]) {
            changed[nchanged++] = n;
            version[n] = p->mem2d_version[n];
        }
    }
    for (int n = 0; n < KHDAYS_FRAME_HD2D_PAGES; ++n) {
        if (!valid || hd2d_version[n] != p->hd2d_version[n]) {
            hd_changed[nhd++] = n;
            hd2d_version[n] = p->hd2d_version[n];
        }
    }
    valid = 1;
    size = fixed + (uint32_t)nchanged * PAGE + (uint32_t)nhd * HD2D_PAGE;
    if (size > upload_size) {
        SDL_GPUTransferBufferCreateInfo info;
        if (upload != NULL) {
            SDL_ReleaseGPUTransferBuffer(device, upload);
        }
        SDL_zero(info);
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = size + size / 2;
        upload = SDL_CreateGPUTransferBuffer(device, &info);
        upload_size = upload != NULL ? info.size : 0;
        if (upload == NULL) {
            valid = 0;
            return 0;
        }
    }
    mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, upload, true);
    if (mapped == NULL) {
        valid = 0;
        return 0;
    }
    at = 0;
    for (int e = 0; e < 2; ++e) {
        memcpy(mapped + at, p->desc2d[e], KHDAYS_DESC_WORDS * 4);
        if (e == 0) {
            ((uint32_t *)(mapped + at))[KD_WIDE] = wide; /* 0: no wider 3D this frame, the DS's columns */
        }
        if (e == 0 && layers) {
            uint32_t *flags = &((uint32_t *)(mapped + at))[KD_FLAGS];
            *flags = (*flags & ~KDF_DISPLAY_MASK) | 1u;
        }
        if ((khdays_raster3d_fine() || (p->wants & KHDAYS_WANT_MOVIE)) &&
            (e == 1 || raster || !(((const uint32_t *)(mapped + at))[KD_FLAGS] & KDF_BG0_IS_3D))) {
            /* compose in 8 bits per channel (HD graphics, a movie's picture): for engine
             * A only when its 3D is drawn in 8 bits too, or it has no 3D */
            ((uint32_t *)(mapped + at))[KD_FLAGS] |= KDF_FINE;
        }
        if (one != NULL) {
            /* the one-screen picture's sources: the 2D apart from the 3D, in 8 bits, no master brightness */
            ((uint32_t *)(mapped + at))[KD_FLAGS] |= KDF_SPLIT | KDF_FINE;
        }
        at += KHDAYS_DESC_WORDS * 4;
        memcpy(mapped + at, p->palette2d + 0x400 * e, 0x400);
        at += 0x400;
        if (p->elements[e].count != 0) {
            memcpy(mapped + at, p->elements[e].records, p->elements[e].count * KE_WORDS * 4u);
            at += p->elements[e].count * KE_WORDS * 4u;
        }
        if (p->desc2d[e][KD_LEFT_OUT] != 0) {
            memcpy(mapped + at, p->left_out_cells[e], KE_LEFT_CELL_WORDS * 4u);
            at += KE_LEFT_CELL_WORDS * 4u;
            memcpy(mapped + at, p->left_out_obj[e], KE_LEFT_OBJ_WORDS * 4u);
            at += KE_LEFT_OBJ_WORDS * 4u;
        }
    }
    if (has3d) {
        memcpy(mapped + at, p->layer3d, KHDAYS_FRAME_PIXELS * 4);
        at += KHDAYS_FRAME_PIXELS * 4;
    }
    if (one != NULL) {
        memcpy(mapped + at, one->shapes, shape_bytes);
        at += shape_bytes;
        memcpy(mapped + at, one->keys, key_bytes);
        at += key_bytes;
        if (piece_bytes != 0) {
            memcpy(mapped + at, one->pieces, piece_bytes);
        }
        at += piece_bytes;
    }
    for (int k = 0; k < nchanged; ++k) {
        memcpy(mapped + at + (uint32_t)k * PAGE, p->mem2d + (size_t)changed[k] * PAGE, PAGE);
    }
    for (int k = 0; k < nhd; ++k) {
        memcpy(mapped + at + (uint32_t)nchanged * PAGE + (uint32_t)k * HD2D_PAGE,
               p->hd2d + (size_t)hd_changed[k] * KHDAYS_FRAME_HD2D_PAGE_WORDS, HD2D_PAGE);
    }
    SDL_UnmapGPUTransferBuffer(device, upload);

    cmd = SDL_AcquireGPUCommandBuffer(device);
    if (cmd == NULL) {
        valid = 0;
        return 0;
    }
    if (!khdays_gpu_hd_sync(cmd)) { /* upload the HD replacements the card does not have yet */
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    if (!khdays_sequence_gpu_draw(device, cmd, &p->sequence_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    if (!khdays_sequence_gpu_draw(device, cmd, &p->enemy_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    for (int i = 0; i < 2; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->information_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->minimap_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    for (int i = 0; i < 3; ++i)
        if (!khdays_sequence_gpu_draw(device, cmd, &p->notification_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    if (one && !khdays_sequence_gpu_draw(device, cmd, &p->radar_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    if (!khdays_sequence_gpu_draw(device, cmd, &p->mission_panel_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    if (!khdays_sequence_gpu_draw(device, cmd, &p->obtained_image)) {
        SDL_CancelGPUCommandBuffer(cmd);
        valid = 0;
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->party_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->portrait_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (!khdays_sequence_gpu_draw(device, cmd, &p->label_images[i])) {
            SDL_CancelGPUCommandBuffer(cmd);
            valid = 0;
            return 0;
        }
    }
    {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;
        src.transfer_buffer = upload;
        at = 0;
        for (int e = 0; e < 2; ++e) {
            src.offset = at;
            dst.buffer = desc[e];
            dst.offset = 0;
            dst.size = DESC_BYTES + element_bytes(p, e); /* the description, its palettes and its elements */
            SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            at += DESC_BYTES + element_bytes(p, e);
        }
        if (has3d) {
            src.offset = at;
            dst.buffer = layer3d;
            dst.offset = 0;
            dst.size = KHDAYS_FRAME_PIXELS * 4;
            SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            at += KHDAYS_FRAME_PIXELS * 4;
        }
        if (one != NULL) {
            if (shape_bytes != 0) {
                src.offset = at;
                dst.buffer = shape_buffer;
                dst.offset = 0;
                dst.size = shape_bytes;
                SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            }
            at += shape_bytes;
            if (key_bytes != 0) {
                src.offset = at;
                dst.buffer = key_buffer;
                dst.offset = 0;
                dst.size = key_bytes;
                SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            }
            at += key_bytes;
            if (piece_bytes != 0) {
                src.offset = at;
                dst.buffer = piece_buffer;
                dst.offset = 0;
                dst.size = piece_bytes;
                SDL_UploadToGPUBuffer(copy, &src, &dst, true);
            }
            at += piece_bytes;
        }
        for (int k = 0; k < nchanged; ++k) {
            /* the pages that changed, into the card's copy of the memory */
            uint32_t offset;
            const int a = area_of(changed[k], &offset);
            if (a < 0) {
                continue;
            }
            src.offset = at + (uint32_t)k * PAGE;
            dst.buffer = memory[a];
            dst.offset = offset;
            dst.size = PAGE;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        }
        for (int k = 0; k < nhd; ++k) {
            /* the map pages that changed, after their area's memory */
            uint32_t offset;
            const int a = area_of(hd_changed[k], &offset);
            if (a < 0 || a > BOBJ) {
                continue;
            }
            src.offset = at + (uint32_t)nchanged * PAGE + (uint32_t)k * HD2D_PAGE;
            dst.buffer = memory[a];
            dst.offset = (uint32_t)area[a].pages * PAGE + offset / PAGE * HD2D_PAGE;
            dst.size = HD2D_PAGE;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        }
        SDL_EndGPUCopyPass(copy);
    }
    for (int e = 0; e < 2; ++e) {
        Compose2dParams cp;
        SDL_GPUStorageTextureReadWriteBinding io[3];
        SDL_GPUBuffer *in[8];
        SDL_GPUComputePass *pass;
        const int b = e == 1;
        SDL_zero(cp);
        cp.scale = (uint32_t)scale;
        cp.width = (uint32_t)(screen_columns[e] * scale);
        cp.height = (uint32_t)(KHDAYS_FRAME_H * scale);
        /* The one-screen picture's 2D (composed apart from its 3D) reads the 3D
         * drawn as wide as the picture, with the DS's 256 columns in its middle,
         * to know where the game's 3D is transparent (kd_split). */
        cp.source3d = e != 0 ? 0u : raster ? 2u : one != NULL ? 0u : has3d ? 1u : 0u;
        cp.offset3d = e == 0 && raster && one != NULL ? (uint32_t)(raster_columns - KHDAYS_FRAME_W) / 2u : 0u;
        if (e == 0 && raster) {
            const uint8_t *io3d = p->io3d;
            cp.disp3dcnt = p->disp3dcnt;
            cp.swap_params = p->list3d.swap_params;
            cp.fine = (uint32_t)khdays_raster3d_fine();
            cp.clear_color = io32(io3d, 0x50);
            cp.clear_depth = io32(io3d, 0x54);
            cp.fog_color = io32(io3d, 0x58);
            cp.fog_offset = io32(io3d, 0x5c);
            for (int i = 0; i < 4; ++i) {
                cp.edge_colors[i] = io32(io3d, 0x30 + 4u * (unsigned)i);
            }
            for (int i = 0; i < 8; ++i) {
                cp.fog_table[i] = io32(io3d, 0x60 + 4u * (unsigned)i);
            }
        }
        in[0] = desc[e];
        in[1] = memory[b ? BBG : ABG];
        in[2] = memory[b ? BOBJ : AOBJ];
        in[3] = memory[b ? BBGEXT : ABGEXT];
        in[4] = memory[b ? BOBJEXT : AOBJEXT];
        in[5] = memory[VRAM];
        in[6] = layer3d;
        in[7] = khdays_gpu_hd_buffer();
        SDL_zeroa(io);
        io[0].texture = e == 0 && raster ? color3d : dummy3d[0]; /* read only: the 3D pass's output, not cycled */
        io[1].texture = e == 0 && raster ? attr3d : dummy3d[1];
        io[2].texture = screen[e];
        io[2].cycle = true;
        pass = SDL_BeginGPUComputePass(cmd, io, 3, NULL, 0);
        SDL_BindGPUComputePipeline(pass, compose);
        SDL_BindGPUComputeStorageBuffers(pass, 0, in, 8);
        SDL_PushGPUComputeUniformData(cmd, 0, &cp, sizeof(cp));
        SDL_DispatchGPUCompute(pass, (cp.width + 7) / 8, (cp.height + 7) / 8,
                               1); /* the shader skips pixels past the edge */
        SDL_EndGPUComputePass(pass);
    }
    if (one != NULL) {
        /* the one-screen picture, from both screens and the 3D (onescreen.hlsl) */
        OneScreenParams op;
        SDL_GPUStorageTextureReadWriteBinding io[6];
        SDL_GPUBuffer *in[4];
        SDL_GPUTexture *backdrop = backdrop_on_card(cmd, one);
        SDL_GPUComputePass *pass;
        const int upper = p->upper; /* the engine on the upper screen */
        const uint32_t *desc_upper = p->desc2d[upper], *desc_lower = p->desc2d[!upper];
        const uint32_t bd = one->frame == (int)KOS_UPPER ? desc_upper[KD_BACKDROP] : desc_lower[KD_BACKDROP];
        SDL_zero(op);
        op.scale = (uint32_t)scale;
        op.width = (uint32_t)(one->columns * scale);
        op.height = (uint32_t)(KHDAYS_FRAME_H * scale);
        op.has3d = (uint32_t)(raster != 0);
        if (raster) {
            const uint8_t *io3d = p->io3d;
            op.disp3dcnt = p->disp3dcnt;
            op.swap_params = p->list3d.swap_params;
            op.fine = (uint32_t)khdays_raster3d_fine();
            op.clear_color = io32(io3d, 0x50);
            op.clear_depth = io32(io3d, 0x54);
            op.fog_color = io32(io3d, 0x58);
            op.fog_offset = io32(io3d, 0x5c);
            for (int i = 0; i < 4; ++i) {
                op.edge_colors[i] = io32(io3d, 0x30 + 4u * (unsigned)i);
            }
            for (int i = 0; i < 8; ++i) {
                op.fog_table[i] = io32(io3d, 0x60 + 4u * (unsigned)i);
            }
        }
        op.shape_count = (uint32_t)one->shape_count;
        op.frame = (uint32_t)one->frame;
        op.rule = (uint32_t)one->rule;
        op.masters = (desc_upper[KD_MASTER] & 0xffffu) | (desc_lower[KD_MASTER] & 0xffffu) << 16;
        {
            /* the backdrop colour (BGR555) widened to 8-bit channels, as kd_fine does */
            const uint32_t r = bd & 31u, g = (bd >> 5) & 31u, b = (bd >> 10) & 31u;
            op.backdrop = ((r << 3) | (r >> 2)) | ((g << 3) | (g >> 2)) << 8 | ((b << 3) | (b >> 2)) << 16;
        }
        op.a_upper = upper == 0 ? 1u : 0u;
        op.hide3d = one->hide3d_all ? 1u : 0u;
        if (backdrop != no_backdrop) {
            /* covering the picture (or inside it: backdrop_fit), its aspect kept, held at its
             * anchor; an animated picture's pieces on its ground, from the atlas */
            const int pw = one->picture_w > 0 ? one->picture_w : one->backdrop_w;
            const int ph = one->picture_h > 0 ? one->picture_h : one->backdrop_h;
            const float sw = (float)op.width / (float)pw, sh = (float)op.height / (float)ph;
            const float s = (sw > sh) != (one->backdrop_fit != 0) ? sw : sh;
            op.backdrop_w = (uint32_t)one->backdrop_w;
            op.backdrop_h = (uint32_t)one->backdrop_h;
            op.backdrop_scale = s;
            op.backdrop_x = ((float)op.width - (float)pw * s) * one->backdrop_ax;
            op.backdrop_y = ((float)op.height - (float)ph * s) * one->backdrop_ay;
            if (one->animated) {
                op.animated = 1u;
                op.piece_count = (uint32_t)one->piece_count;
                op.backdrop = one->ground & 0xffffffu;
            }
        }
        in[0] = shape_buffer;
        in[1] = key_buffer;
        in[2] = piece_buffer;
        in[3] = khdays_gpu_hd_buffer();
        SDL_zeroa(io);
        io[0].texture = raster ? color3d : dummy3d[0];
        io[1].texture = raster ? attr3d : dummy3d[1];
        io[2].texture = screen[upper];
        io[3].texture = screen[!upper];
        io[4].texture = picture;
        io[4].cycle = true;
        io[5].texture = backdrop; /* read only */
        pass = SDL_BeginGPUComputePass(cmd, io, 6, NULL, 0);
        SDL_BindGPUComputePipeline(pass, onescreen);
        SDL_BindGPUComputeStorageBuffers(pass, 0, in, 4);
        SDL_PushGPUComputeUniformData(cmd, 0, &op, sizeof(op));
        SDL_DispatchGPUCompute(pass, (op.width + 7) / 8, (op.height + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
    }
    if (blend) {
        if (one != NULL) {
            take_capture(cmd, p, &plan, picture, picture_w, picture_h);
        } else {
            take_capture(cmd, p, &plan, screen[0], screen_columns[0] * scale, KHDAYS_FRAME_H * scale);
        }
    }
    /* what is shown, shrunk to the size shown (the picture on one screen, else the screens) */
    picture_shrunk = screens_shrunk = 0;
    if (shown > 0.0f && shrinker != NULL) {
        if (one != NULL) {
            picture_shrunk = shrink(cmd, picture, picture_w, picture_h, picture_columns, &shrunk_picture);
        } else {
            screens_shrunk = 1;
            for (int e = 0; e < 2; ++e) {
                screens_shrunk = screens_shrunk && shrink(cmd, screen[e], screen_columns[e] * scale,
                                                          KHDAYS_FRAME_H * scale, screen_columns[e], &shrunk_screen[e]);
            }
        }
    }
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        valid = 0;
        return 0;
    }
    return 1;
}

int khdays_gfx_render_draw(const KhdaysFramePackage *p, int scale, int raster)
{
    return draw(p, scale, raster, NULL);
}

int khdays_gfx_render_draw_one(const KhdaysFramePackage *p, int scale, int raster, const KhdaysOneScreenDraw *one)
{
    return draw(p, scale, raster, one);
}

SDL_Texture *khdays_gfx_render_picture(SDL_Renderer *renderer, int *columns)
{
    *columns = picture_columns;
    if (picture == NULL) {
        return NULL;
    }
    if (picture_shrunk) {
        return wrap(renderer, shrunk_picture.texture, shrunk_picture.w, shrunk_picture.h, &shrunk_picture.wrapped,
                    &shrunk_picture.wrapped_for);
    }
    return wrap(renderer, picture, picture_w, picture_h, &picture_wrapped, &picture_for);
}

SDL_Texture *khdays_gfx_render_texture(SDL_Renderer *renderer, int engine)
{
    if (screen[engine] == NULL) {
        return NULL;
    }
    if (screens_shrunk) {
        Shrunk *s = &shrunk_screen[engine];
        return wrap(renderer, s->texture, s->w, s->h, &s->wrapped, &s->wrapped_for);
    }
    return wrap(renderer, screen[engine], screen_columns[engine] * screen_scale, KHDAYS_FRAME_H * screen_scale,
                &wrapped[engine], &wrapped_for[engine]);
}

int khdays_gfx_render_columns(int engine)
{
    return screen_columns[engine];
}

int khdays_gfx_render_captured_shown(void)
{
    /* engine B is on the screen engine A was on in the previous package (the upper when A is on the lower) */
    return held_ok && held != NULL && drawn_any && held_screen == (drawn_upper == 0 ? 1 : 0);
}

SDL_Texture *khdays_gfx_render_captured(SDL_Renderer *renderer, int *columns)
{
    if (!khdays_gfx_render_captured_shown()) {
        return NULL;
    }
    *columns = held_columns;
    return wrap(renderer, held, held_w, held_h, &held_wrapped, &held_for);
}

uint32_t *khdays_gfx_render_read(int engine, int *width, int *height)
{
    /* as shown: shrunk to the size shown */
    SDL_GPUTexture *texture;
    int w, h;
    if (engine == 3) {
        texture = held;
        w = held_w;
        h = held_h;
    } else if (engine == 2) {
        texture = picture_shrunk ? shrunk_picture.texture : picture;
        w = picture_shrunk ? shrunk_picture.w : picture_w;
        h = picture_shrunk ? shrunk_picture.h : picture_h;
    } else {
        const int e = engine == 1 ? 1 : 0;
        texture = screens_shrunk ? shrunk_screen[e].texture : screen[e];
        w = screens_shrunk ? shrunk_screen[e].w : screen_columns[e] * screen_scale;
        h = screens_shrunk ? shrunk_screen[e].h : KHDAYS_FRAME_H * screen_scale;
    }
    const Uint32 bytes = (Uint32)(w * h * 4);
    SDL_GPUTransferBufferCreateInfo info;
    SDL_GPUTransferBuffer *download;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUFence *fence;
    uint32_t *pixels = NULL;
    if (texture == NULL) {
        SDL_SetError("nothing drawn yet");
        return NULL;
    }
    SDL_zero(info);
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = bytes;
    download = SDL_CreateGPUTransferBuffer(device, &info);
    cmd = download != NULL ? SDL_AcquireGPUCommandBuffer(device) : NULL;
    if (cmd == NULL) {
        if (download != NULL)
            SDL_ReleaseGPUTransferBuffer(device, download);
        return NULL;
    }
    {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region;
        SDL_GPUTextureTransferInfo to;
        SDL_zero(region);
        SDL_zero(to);
        region.texture = texture;
        region.w = (Uint32)w;
        region.h = (Uint32)h;
        region.d = 1;
        to.transfer_buffer = download;
        SDL_DownloadFromGPUTexture(copy, &region, &to);
        SDL_EndGPUCopyPass(copy);
    }
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence != NULL && SDL_WaitForGPUFences(device, true, &fence, 1)) {
        const uint8_t *rgba = (const uint8_t *)SDL_MapGPUTransferBuffer(device, download, false);
        pixels = rgba != NULL ? (uint32_t *)SDL_malloc(bytes) : NULL;
        if (pixels != NULL) {
            for (int i = 0; i < w * h; ++i) {
                pixels[i] = (uint32_t)rgba[4 * i] << 16 | (uint32_t)rgba[4 * i + 1] << 8 | rgba[4 * i + 2];
            }
        }
        if (rgba != NULL) {
            SDL_UnmapGPUTransferBuffer(device, download);
        }
    }
    if (fence != NULL) {
        SDL_ReleaseGPUFence(device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(device, download);
    *width = w;
    *height = h;
    return pixels;
}
