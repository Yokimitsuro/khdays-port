/* The 3D list drawn by the graphics card's own rasterizer (gpu_native3d.h): each polygon a fan of
 * triangles, drawn in the list's order. The pipelines carry the DS's per-pixel rules a fragment shader
 * cannot: the depth test, the polygon IDs and shadow masks in the stencil, the blending into
 * native3d.hlsl's six targets, of which a picture writes only those its finishing reads; consecutive
 * polygons drawn alike are one draw. native3d_pack.hlsl then makes raster3d's two targets from them. */
#include "gpu_native3d.h"
#include "../../gfx/character_shadow.h"
#include "../../gfx/g3d.h"
#include "../../runtime/display/frame_package.h"
#include "generated/native3d_pack_cs.h"
#include "generated/native3d_ps.h"
#include "generated/native3d_rear_ps.h"
#include "generated/native3d_rear_vs.h"
#include "generated/native3d_vs.h"
#include "generated/native3d_z_ps.h"

#include <stdio.h>
#include <string.h>

#define TARGETS 6

/* The shaders' constant buffer (shaders/native3d_params.hlsli), register for register. */
typedef struct Params {
    uint32_t scale, width, height, disp3dcnt, swap_params, alpha_ref, clear_color, clear_depth, smooth;
    int32_t origin_x;
    uint32_t fine, outputs;
    float to_clip[4];
    uint32_t toon[16];
} Params;

/* A polygon pipeline's key: its depth test (DEPTH_EQUAL: the DS's "equal", less or equal 0x200 nearer),
 * whether it writes the depth, its stencil rules, whether the DS blends. */
enum { DEPTH_LESS, DEPTH_EQUAL, DEPTH_ALWAYS };
enum {
    STENCIL_OPAQUE,     /* the polygon ID, not translucent */
    STENCIL_SAME_ID,    /* translucent: not over a translucent pixel of the same ID, which it becomes */
    STENCIL_ANY_ID,     /* translucent, a mod's: over any */
    STENCIL_MASK,       /* a shadow mask: where its depth test fails, the mask's bit */
    STENCIL_SHADOW_PRE, /* before a shadow: the mask's bit cleared on its own polygon ID */
    STENCIL_SHADOW,     /* a shadow: where the mask's bit is, translucent */
    STENCIL_EFFECT      /* a KH2 effect's: the stencil neither tested nor written */
};
/* A KH2 effect's blend (its GS ALPHA byte, gfx/g3d.h KHDAYS_G3D_EFFECT) as the colour target's, the shader's colour
 * premultiplied: (A - B) * As + D for the byte's five the effects use, and its depth alone. 0: not an effect. */
enum { EFFECT_NONE, EFFECT_ALPHA, EFFECT_ADD, EFFECT_SUBTRACT, EFFECT_DARKEN, EFFECT_REPLACE, EFFECT_DEPTH_ONLY };
/* The targets a picture writes besides the colour (0) and the flags (4) (native3d.hlsl says what each
 * holds): the first translucent colour on an empty pixel (1) when the rear plane may be empty, the fog bit
 * (2) with fog on, the opaque polygons' IDs and edges (3) with edge marking on or raster3d.hlsl drawing
 * after, the depth (5) for any of them. */
#define OUT_FIRST 1
#define OUT_FOG 2
#define OUT_IDS 4
#define OUT_DEPTH 8
#define OUTPUTS 16
/* ... and with `z`, the depth the rasterizer interpolates (native3d_z.hlsl): the Z buffer's, not "equal" */
#define KEY(depth, write, stencil, blending, outputs, z)                                                               \
    ((depth) | (write) << 2 | (stencil) << 3 | (blending) << 6 | (outputs) << 7 | (z) << 11)
/* ... and a KH2 effect's blend (EFFECT_*) above */
#define EFFECT_KEY(key, effect) ((key) | (effect) << 12)
#define KEYS (128 * OUTPUTS * 2 * 8)
/* Draws over the whole target: the shadow masks' bit cleared (a run of masks starts), or the
 * translucent bit (the first selection marker blends over the world's pixels whatever their ID). */
#define CLEAR_MASKS (-1)
#define CLEAR_TRANSLUCENT (-2)

/* the depth (a whole number of 24 bits, exact in a float) in a float target: a render pass clears a target with
 * float values, which Direct3D 12 converts for an integer format and Vulkan gives as their bits -- an R32_UINT
 * cleared to the rear plane's depth held another (about half way) on Vulkan, and raster3d.hlsl resuming from it
 * left out all that lay behind that */
static const SDL_GPUTextureFormat formats[TARGETS] = {
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R32_FLOAT};
/* the targets each output needs written */
static const int target_output[TARGETS] = {0, OUT_FIRST, OUT_FOG, OUT_IDS, 0, OUT_DEPTH};
#define TARGET_USAGE (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE)

static SDL_GPUDevice *device;
static SDL_GPUShader *polygon_vs, *polygon_ps, *polygon_ps_z, *rear_vs, *rear_ps;
static SDL_GPUComputePipeline *pack;
static SDL_GPUGraphicsPipeline *pipelines[KEYS], *rears[OUTPUTS], *clear_masks, *clear_translucent;
static SDL_GPUTextureFormat depth_format;
static SDL_GPUTexture *target[TARGETS], *depth;
static int target_w, target_h;
/* A second size's targets kept aside while the 3D is drawn at two sizes a frame (khdays_native3d_keep_two:
 * the HUD apart at the 2D's scale), so that neither is made again each frame. */
static SDL_GPUTexture *spare_target[TARGETS], *spare_depth;
static int spare_w, spare_h, keep_two;
static int outputs; /* the prepared picture's OUT_* */
static SDL_GPUBuffer *corner_buffer;
static uint32_t corner_buffer_size;

/* The prepared picture: the triangles' corners (a vertex, its polygon: two words each, the polygon
 * pipelines' vertex buffer) and the draws. */
typedef struct Draw {
    uint32_t first, count; /* corners */
    int key;               /* the polygon pipeline's, or CLEAR_* */
    uint8_t reference;     /* the stencil's */
    uint8_t alone;         /* no other polygon joins it */
} Draw;
static uint32_t *corners;
static uint32_t corner_count, corner_room;
static Draw *draws;
static int draw_count, draw_room;
/* A run of polygons that may make both opaque and translucent pixels (a texture's alpha, a replacement's, a
 * mod's vertex alpha), drawn alike: their opaque pixels first, which write the depth as the DS's do, then their
 * translucent ones, which write it when the polygon says (flush_run). */
static int *run;
static int run_count, run_room;
static int run_opaque_key, run_translucent_key;
static uint8_t run_opaque_reference, run_translucent_reference;

static SDL_GPUShader *make_shader(const unsigned char *dxil, unsigned dxil_size, const unsigned char *spirv,
                                  unsigned spirv_size, const unsigned char *msl, unsigned msl_size, const char *entry,
                                  SDL_GPUShaderStage stage, Uint32 buffers, Uint32 uniforms)
{
    SDL_GPUShaderCreateInfo info;
    const SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(device);
    SDL_zero(info);
    if (have & SDL_GPU_SHADERFORMAT_DXIL) {
        info.code = dxil;
        info.code_size = dxil_size;
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (have & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.code = spirv;
        info.code_size = spirv_size;
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (have & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = msl;
        info.code_size = msl_size - 1; /* the text without its final zero */
        info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else {
        SDL_SetError("the device takes none of the shader formats this build carries");
        return NULL;
    }
    info.entrypoint = entry;
    info.stage = stage;
    info.num_storage_buffers = buffers;
    info.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(device, &info);
}

static SDL_GPUComputePipeline *make_pack(void)
{
    SDL_GPUComputePipelineCreateInfo info;
    const SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(device);
    SDL_zero(info);
    if (have & SDL_GPU_SHADERFORMAT_DXIL) {
        info.code = native3d_pack_cs_dxil;
        info.code_size = native3d_pack_cs_dxil_size;
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (have & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.code = native3d_pack_cs_spirv;
        info.code_size = native3d_pack_cs_spirv_size;
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (have & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = native3d_pack_cs_msl;
        info.code_size = native3d_pack_cs_msl_size - 1;
        info.format = SDL_GPU_SHADERFORMAT_MSL;
    } else {
        SDL_SetError("the device takes none of the shader formats this build carries");
        return NULL;
    }
    info.entrypoint = NATIVE3D_PACK_CS_ENTRY;
    info.num_readonly_storage_buffers = 1;
    info.num_readwrite_storage_textures = TARGETS + 2;
    info.num_uniform_buffers = 1;
    info.threadcount_x = 8;
    info.threadcount_y = 8;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

static void blend(SDL_GPUColorTargetDescription *t, SDL_GPUBlendFactor src, SDL_GPUBlendFactor dst,
                  SDL_GPUBlendFactor src_alpha, SDL_GPUBlendFactor dst_alpha, SDL_GPUBlendOp alpha_op)
{
    t->blend_state.enable_blend = true;
    t->blend_state.src_color_blendfactor = src;
    t->blend_state.dst_color_blendfactor = dst;
    t->blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    t->blend_state.src_alpha_blendfactor = src_alpha;
    t->blend_state.dst_alpha_blendfactor = dst_alpha;
    t->blend_state.alpha_blend_op = alpha_op;
}

/* The targets as a polygon draws them (native3d.hlsl says what each holds): those of `out` (OUT_*); with
 * `colours` off only the depth and stencil. */
static void polygon_targets(SDL_GPUColorTargetDescription *t, int colours, int depth_written, int blending, int out)
{
    const SDL_GPUColorComponentFlags all =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    memset(t, 0, sizeof(*t) * TARGETS);
    for (int i = 0; i < TARGETS; ++i) {
        t[i].format = formats[i];
        t[i].blend_state.enable_color_write_mask = true;
        t[i].blend_state.color_write_mask = colours && (target_output[i] & ~out) == 0 ? all : 0;
    }
    /* the colour over what was there by the shader's share; how much of the first colour shows, less */
    blend(&t[0], SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ZERO,
          SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_GPU_BLENDOP_ADD);
    /* the first colour on an empty pixel, kept once there */
    blend(&t[1], SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_GPU_BLENDFACTOR_ONE,
          SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDOP_ADD);
    /* the fog bit and the opaque polygon's ID, edge: written where the shader's alpha is 1 */
    for (int i = 2; i <= 3; ++i) {
        blend(&t[i], SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE,
              SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDOP_ADD);
    }
    /* translucent and over the 2D replaced; the alpha the largest, replaced without the DS's blending */
    blend(&t[4], SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ZERO,
          blending ? SDL_GPU_BLENDOP_MAX : SDL_GPU_BLENDOP_ADD);
    t[5].blend_state.color_write_mask = colours && depth_written && (out & OUT_DEPTH) ? SDL_GPU_COLORCOMPONENT_R : 0;
}

/* A KH2 effect's targets: its colour blended into the colour target alone as its GS byte says (the shader's colour
 * premultiplied by its alpha), what the colour target's alpha holds kept; the depth target written with the
 * depth; nothing else -- its pixels keep the world's IDs, fog and flags. */
static void effect_targets(SDL_GPUColorTargetDescription *t, int effect, int depth_written, int out)
{
    const SDL_GPUColorComponentFlags all =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    SDL_GPUBlendFactor src = SDL_GPU_BLENDFACTOR_ONE, dst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp op = SDL_GPU_BLENDOP_ADD;
    memset(t, 0, sizeof(*t) * TARGETS);
    for (int i = 0; i < TARGETS; ++i) {
        t[i].format = formats[i];
        t[i].blend_state.enable_color_write_mask = true;
        t[i].blend_state.color_write_mask = 0;
    }
    switch (effect) {
    case EFFECT_ADD: /* Cs As + Cd */ dst = SDL_GPU_BLENDFACTOR_ONE; break;
    case EFFECT_SUBTRACT: /* Cd - Cs As */
        dst = SDL_GPU_BLENDFACTOR_ONE;
        op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        break;
    case EFFECT_DARKEN: /* Cd (1 - As) */ src = SDL_GPU_BLENDFACTOR_ZERO; break;
    case EFFECT_REPLACE: /* Cs As */ dst = SDL_GPU_BLENDFACTOR_ZERO; break;
    default: /* Cs As + Cd (1 - As) */ break;
    }
    if (effect != EFFECT_DEPTH_ONLY) {
        t[0].blend_state.color_write_mask = all;
        t[0].blend_state.enable_blend = true;
        t[0].blend_state.src_color_blendfactor = src;
        t[0].blend_state.dst_color_blendfactor = dst;
        t[0].blend_state.color_blend_op = op;
        t[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        t[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        t[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }
    t[5].blend_state.color_write_mask = depth_written && (out & OUT_DEPTH) ? SDL_GPU_COLORCOMPONENT_R : 0;
}

/* A KH2 effect polygon's EFFECT_*: its GS ALPHA byte (in `pltt`) as the five the effects use; another byte drawn as
 * the plain blend, said once. */
static int effect_blend(const KhdaysG3dPolygon *p)
{
    static int said;
    if (p->flags & KHDAYS_G3D_EFFECT_DEPTH_ONLY) {
        return EFFECT_DEPTH_ONLY;
    }
    switch (p->pltt & 0xffu) {
    case 0x44: return EFFECT_ALPHA;
    case 0x48: return EFFECT_ADD;
    case 0x42: return EFFECT_SUBTRACT;
    case 0x46: return EFFECT_DARKEN;
    case 0x88: return EFFECT_REPLACE;
    default:
        if (!said) {
            said = 1;
            fprintf(stderr, "native 3D: a KH2 effect's GS blend %02x drawn as 44 (said once)\n",
                    (unsigned)(p->pltt & 0xffu));
        }
        return EFFECT_ALPHA;
    }
}

static SDL_GPUStencilOpState stencil_ops(SDL_GPUCompareOp compare, SDL_GPUStencilOp pass, SDL_GPUStencilOp depth_fail)
{
    SDL_GPUStencilOpState s;
    s.fail_op = SDL_GPU_STENCILOP_KEEP;
    s.pass_op = pass;
    s.depth_fail_op = depth_fail;
    s.compare_op = compare;
    return s;
}

static SDL_GPUGraphicsPipeline *make_graphics(SDL_GPUShader *vs, SDL_GPUShader *ps,
                                              const SDL_GPUColorTargetDescription *targets,
                                              SDL_GPUCompareOp depth_compare, int depth_write,
                                              SDL_GPUStencilOpState stencil, Uint8 compare_mask, Uint8 write_mask)
{
    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_GPUVertexBufferDescription buffer;
    SDL_GPUVertexAttribute corner;
    SDL_zero(info);
    if (vs == polygon_vs) {
        /* each corner: its vertex and its polygon */
        SDL_zero(buffer);
        SDL_zero(corner);
        buffer.slot = 0;
        buffer.pitch = 2 * sizeof(uint32_t);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        corner.location = 0;
        corner.buffer_slot = 0;
        corner.format = SDL_GPU_VERTEXELEMENTFORMAT_UINT2;
        corner.offset = 0;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = &corner;
        info.vertex_input_state.num_vertex_attributes = 1;
    }
    info.vertex_shader = vs;
    info.fragment_shader = ps;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = depth_compare;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = depth_write != 0;
    info.depth_stencil_state.enable_stencil_test = true;
    info.depth_stencil_state.front_stencil_state = stencil;
    info.depth_stencil_state.back_stencil_state = stencil;
    info.depth_stencil_state.compare_mask = compare_mask;
    info.depth_stencil_state.write_mask = write_mask;
    info.target_info.color_target_descriptions = targets;
    info.target_info.num_color_targets = TARGETS;
    info.target_info.depth_stencil_format = depth_format;
    info.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(device, &info);
}

static SDL_GPUGraphicsPipeline *polygon_pipeline(int key)
{
    static const SDL_GPUCompareOp depth_ops[4] = {SDL_GPU_COMPAREOP_LESS, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
                                                  SDL_GPU_COMPAREOP_ALWAYS,
                                                  SDL_GPU_COMPAREOP_ALWAYS}; /* the key's 2 bits */
    if (pipelines[key] == NULL) {
        const int compare = key & 3, write = (key >> 2) & 1, rules = (key >> 3) & 7, blending = (key >> 6) & 1;
        const int out = (key >> 7) & (OUTPUTS - 1), z = (key >> 11) & 1, effect = key >> 12;
        SDL_GPUColorTargetDescription t[TARGETS];
        SDL_GPUStencilOpState s;
        Uint8 compare_mask = 0xff, write_mask = 0x7f;
        switch (rules) {
        case STENCIL_SAME_ID:
            s = stencil_ops(SDL_GPU_COMPAREOP_NOT_EQUAL, SDL_GPU_STENCILOP_REPLACE, SDL_GPU_STENCILOP_KEEP);
            compare_mask = 0x7f;
            break;
        case STENCIL_MASK:
            s = stencil_ops(SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_REPLACE);
            write_mask = 0x80;
            break;
        case STENCIL_SHADOW_PRE:
            s = stencil_ops(SDL_GPU_COMPAREOP_EQUAL, SDL_GPU_STENCILOP_ZERO, SDL_GPU_STENCILOP_KEEP);
            compare_mask = 0xbf;
            write_mask = 0x80;
            break;
        case STENCIL_SHADOW:
            s = stencil_ops(SDL_GPU_COMPAREOP_EQUAL, SDL_GPU_STENCILOP_REPLACE, SDL_GPU_STENCILOP_KEEP);
            compare_mask = 0x80;
            break;
        case STENCIL_EFFECT:
            s = stencil_ops(SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_KEEP);
            write_mask = 0;
            break;
        default: /* STENCIL_OPAQUE, STENCIL_ANY_ID */
            s = stencil_ops(SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_REPLACE, SDL_GPU_STENCILOP_KEEP);
            break;
        }
        if (effect != EFFECT_NONE) {
            effect_targets(t, effect, write, out);
        } else {
            polygon_targets(t, rules != STENCIL_MASK && rules != STENCIL_SHADOW_PRE, write, blending, out);
        }
        pipelines[key] = make_graphics(polygon_vs, z ? polygon_ps_z : polygon_ps, t, depth_ops[compare], write, s,
                                       compare_mask, write_mask);
    }
    return pipelines[key];
}

/* The rear plane's triangle over the whole target (the clear image's: writing the targets of `out`), and the
 * same clearing a stencil bit. */
static SDL_GPUGraphicsPipeline *make_full(int clear_bit, int out)
{
    SDL_GPUColorTargetDescription t[TARGETS];
    memset(t, 0, sizeof(t));
    for (int i = 0; i < TARGETS; ++i) {
        t[i].format = formats[i];
        t[i].blend_state.enable_color_write_mask = clear_bit != 0 || (target_output[i] & ~out) != 0;
    }
    if (clear_bit != 0) {
        return make_graphics(rear_vs, rear_ps, t, SDL_GPU_COMPAREOP_ALWAYS, 0,
                             stencil_ops(SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_ZERO, SDL_GPU_STENCILOP_ZERO),
                             0xff, (Uint8)clear_bit);
    }
    return make_graphics(rear_vs, rear_ps, t, SDL_GPU_COMPAREOP_ALWAYS, 1,
                         stencil_ops(SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_REPLACE, SDL_GPU_STENCILOP_REPLACE),
                         0xff, 0xff);
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

static void drop_targets(void)
{
    for (int i = 0; i < TARGETS; ++i) {
        if (target[i] != NULL) {
            SDL_ReleaseGPUTexture(device, target[i]);
            target[i] = NULL;
        }
    }
    if (depth != NULL) {
        SDL_ReleaseGPUTexture(device, depth);
        depth = NULL;
    }
    target_w = target_h = 0;
}

static void swap_spare(void)
{
    SDL_GPUTexture *t;
    int n;
    for (int i = 0; i < TARGETS; ++i) {
        t = target[i];
        target[i] = spare_target[i];
        spare_target[i] = t;
    }
    t = depth;
    depth = spare_depth;
    spare_depth = t;
    n = target_w;
    target_w = spare_w;
    spare_w = n;
    n = target_h;
    target_h = spare_h;
    spare_h = n;
}

static void drop_spare(void)
{
    swap_spare();
    drop_targets();
    swap_spare();
}

void khdays_native3d_keep_two(int on)
{
    keep_two = on != 0;
    if (!keep_two && device != NULL) {
        drop_spare();
    }
}

static int make_targets(int w, int h)
{
    if (w == target_w && h == target_h && depth != NULL) {
        return 1;
    }
    if (keep_two && w == spare_w && h == spare_h && spare_depth != NULL) {
        swap_spare();
        return 1;
    }
    if (keep_two && depth != NULL) {
        drop_spare(); /* the size drawn last kept aside, the one before it let go */
        swap_spare();
    }
    drop_targets();
    for (int i = 0; i < TARGETS; ++i) {
        target[i] = make_texture(formats[i], TARGET_USAGE, w, h);
        if (target[i] == NULL) {
            drop_targets();
            return 0;
        }
    }
    depth = make_texture(depth_format, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, w, h);
    if (depth == NULL) {
        drop_targets();
        return 0;
    }
    target_w = w;
    target_h = h;
    return 1;
}

static SDL_GPUBuffer *make_corner_buffer(Uint32 size)
{
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = size;
    return SDL_CreateGPUBuffer(device, &info);
}

void khdays_native3d_close(void)
{
    if (device == NULL) {
        return;
    }
    drop_targets();
    drop_spare();
    for (int i = 0; i < KEYS; ++i) {
        if (pipelines[i] != NULL) {
            SDL_ReleaseGPUGraphicsPipeline(device, pipelines[i]);
            pipelines[i] = NULL;
        }
    }
    for (int i = 0; i < OUTPUTS; ++i) {
        if (rears[i] != NULL) {
            SDL_ReleaseGPUGraphicsPipeline(device, rears[i]);
            rears[i] = NULL;
        }
    }
    if (clear_masks != NULL)
        SDL_ReleaseGPUGraphicsPipeline(device, clear_masks);
    if (clear_translucent != NULL)
        SDL_ReleaseGPUGraphicsPipeline(device, clear_translucent);
    if (pack != NULL)
        SDL_ReleaseGPUComputePipeline(device, pack);
    if (polygon_vs != NULL)
        SDL_ReleaseGPUShader(device, polygon_vs);
    if (polygon_ps != NULL)
        SDL_ReleaseGPUShader(device, polygon_ps);
    if (polygon_ps_z != NULL)
        SDL_ReleaseGPUShader(device, polygon_ps_z);
    if (rear_vs != NULL)
        SDL_ReleaseGPUShader(device, rear_vs);
    if (rear_ps != NULL)
        SDL_ReleaseGPUShader(device, rear_ps);
    if (corner_buffer != NULL)
        SDL_ReleaseGPUBuffer(device, corner_buffer);
    clear_masks = clear_translucent = NULL;
    pack = NULL;
    polygon_vs = polygon_ps = polygon_ps_z = rear_vs = rear_ps = NULL;
    corner_buffer = NULL;
    corner_buffer_size = 0;
    SDL_free(corners);
    SDL_free(draws);
    SDL_free(run);
    corners = NULL;
    draws = NULL;
    run = NULL;
    run_count = run_room = 0;
    corner_room = corner_count = 0;
    draw_room = draw_count = 0;
    device = NULL;
}

int khdays_native3d_open(SDL_GPUDevice *d)
{
    device = d;
    polygon_vs =
        make_shader(native3d_vs_dxil, native3d_vs_dxil_size, native3d_vs_spirv, native3d_vs_spirv_size, native3d_vs_msl,
                    native3d_vs_msl_size, NATIVE3D_VS_ENTRY, SDL_GPU_SHADERSTAGE_VERTEX, 4, 1);
    polygon_ps =
        make_shader(native3d_ps_dxil, native3d_ps_dxil_size, native3d_ps_spirv, native3d_ps_spirv_size, native3d_ps_msl,
                    native3d_ps_msl_size, NATIVE3D_PS_ENTRY, SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1);
    polygon_ps_z =
        make_shader(native3d_z_ps_dxil, native3d_z_ps_dxil_size, native3d_z_ps_spirv, native3d_z_ps_spirv_size,
                    native3d_z_ps_msl, native3d_z_ps_msl_size, NATIVE3D_Z_PS_ENTRY, SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1);
    rear_vs = make_shader(native3d_rear_vs_dxil, native3d_rear_vs_dxil_size, native3d_rear_vs_spirv,
                          native3d_rear_vs_spirv_size, native3d_rear_vs_msl, native3d_rear_vs_msl_size,
                          NATIVE3D_REAR_VS_ENTRY, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    rear_ps = make_shader(native3d_rear_ps_dxil, native3d_rear_ps_dxil_size, native3d_rear_ps_spirv,
                          native3d_rear_ps_spirv_size, native3d_rear_ps_msl, native3d_rear_ps_msl_size,
                          NATIVE3D_REAR_PS_ENTRY, SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1);
    pack = make_pack();
    depth_format = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, SDL_GPU_TEXTURETYPE_2D,
                                                SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                       ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT
                       : SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    for (int i = 0; i < TARGETS; ++i) {
        if (!SDL_GPUTextureSupportsFormat(device, formats[i], SDL_GPU_TEXTURETYPE_2D, TARGET_USAGE)) {
            khdays_native3d_close();
            return SDL_SetError("the card cannot draw into and read target %d's format", i);
        }
    }
    if (polygon_vs != NULL && polygon_ps != NULL && polygon_ps_z != NULL && rear_vs != NULL && rear_ps != NULL) {
        clear_masks = make_full(0x80, 0);
        clear_translucent = make_full(0x40, 0);
    }
    corner_buffer_size = 1u << 16;
    corner_buffer = make_corner_buffer(corner_buffer_size);
    if (polygon_vs == NULL || polygon_ps == NULL || polygon_ps_z == NULL || rear_vs == NULL || rear_ps == NULL ||
        pack == NULL || clear_masks == NULL || clear_translucent == NULL || corner_buffer == NULL) {
        char why[256];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        khdays_native3d_close();
        return SDL_SetError("%s", why);
    }
    return 1;
}

int khdays_native3d_count(const KhdaysG3dList *list, int32_t hidden)
{
    const int32_t raster3d_only = KHDAYS_G3D_SEQUENCE | KHDAYS_G3D_CLIP | (KHDAYS_G3D_SHADOW_RECEIVER & ~hidden);
    for (int i = 0; i < list->npolys; ++i) {
        if (list->polys[i].flags & raster3d_only) {
            return i;
        }
    }
    return list->npolys;
}

static int corners_for(uint32_t more)
{
    if (corner_count + more > corner_room) {
        const uint32_t room = (corner_count + more) * 2u;
        uint32_t *grown = (uint32_t *)SDL_realloc(corners, (size_t)room * 2u * sizeof(uint32_t));
        if (grown == NULL) {
            return 0;
        }
        corners = grown;
        corner_room = room;
    }
    return 1;
}

static Draw *new_draw(int key, uint8_t reference, int alone)
{
    Draw *d;
    if (draw_count == draw_room) {
        const int room = draw_room > 0 ? draw_room * 2 : 256;
        Draw *grown = (Draw *)SDL_realloc(draws, (size_t)room * sizeof(Draw));
        if (grown == NULL) {
            return NULL;
        }
        draws = grown;
        draw_room = room;
    }
    d = &draws[draw_count++];
    d->first = corner_count;
    d->count = 0;
    d->key = key;
    d->reference = reference;
    d->alone = (uint8_t)alone;
    return d;
}

/* Which of a polygon's pixels a draw takes (the corners' polygon word, bits 30-31; native3d.hlsl). */
enum { PIXELS_ALL, PIXELS_OPAQUE, PIXELS_TRANSLUCENT };

/* Polygon `index` of the list, a fan of triangles, into the last draw if it is drawn alike. */
static int add_polygon(const KhdaysG3dPolygon *p, int index, int pixels, int key, uint8_t reference, int alone)
{
    const uint32_t n = 3u * (uint32_t)(p->count - 2), word = (uint32_t)index | (uint32_t)pixels << 30;
    Draw *d = draw_count > 0 ? &draws[draw_count - 1] : NULL;
    if (!corners_for(n)) {
        return 0;
    }
    if (d == NULL || d->key != key || d->reference != reference || d->alone || alone) {
        d = new_draw(key, reference, alone);
        if (d == NULL) {
            return 0;
        }
    }
    for (int k = 1; k + 1 < p->count; ++k) {
        uint32_t *c = corners + 2u * corner_count;
        c[0] = (uint32_t)p->first;
        c[1] = word;
        c[2] = (uint32_t)(p->first + k);
        c[3] = word;
        c[4] = (uint32_t)(p->first + k + 1);
        c[5] = word;
        corner_count += 3u;
    }
    d->count += n;
    return 1;
}

static int flush_run(const KhdaysG3dList *list)
{
    for (int k = 0; k < run_count; ++k) {
        if (!add_polygon(&list->polys[run[k]], run[k], PIXELS_OPAQUE, run_opaque_key, run_opaque_reference, 0)) {
            return 0;
        }
    }
    for (int k = 0; k < run_count; ++k) {
        if (!add_polygon(&list->polys[run[k]], run[k], PIXELS_TRANSLUCENT, run_translucent_key,
                         run_translucent_reference, 0)) {
            return 0;
        }
    }
    run_count = 0;
    return 1;
}

static int add_to_run(const KhdaysG3dList *list, int index, int opaque_key, uint8_t opaque_reference,
                      int translucent_key, uint8_t translucent_reference)
{
    if (run_count > 0 &&
        (opaque_key != run_opaque_key || opaque_reference != run_opaque_reference ||
         translucent_key != run_translucent_key || translucent_reference != run_translucent_reference)) {
        if (!flush_run(list)) {
            return 0;
        }
    }
    if (run_count == run_room) {
        const int room = run_room > 0 ? run_room * 2 : 64;
        int *grown = (int *)SDL_realloc(run, (size_t)room * sizeof(int));
        if (grown == NULL) {
            return 0;
        }
        run = grown;
        run_room = room;
    }
    run[run_count++] = index;
    run_opaque_key = opaque_key;
    run_opaque_reference = opaque_reference;
    run_translucent_key = translucent_key;
    run_translucent_reference = translucent_reference;
    return 1;
}

/* The draws of the first `count` polygons, by gpu3d.c plot's rules: which pixels a polygon may make
 * translucent (texture formats with alpha, its own alpha, a mod's vertex alpha, a replacement), its depth
 * test and writes, its stencil rules. */
static int build(const KhdaysG3dList *list, int count, int32_t hidden, uint32_t disp3dcnt, int out)
{
    const int zbuffer = (list->swap_params & 2u) == 0u;
    static int said;
    const int blending = (disp3dcnt & 8u) != 0;
    int shadows = 0, masks = 0, markers = 0, lines = 0;
    draw_count = 0;
    corner_count = 0;
    run_count = 0;
    for (int i = 0; i < count; ++i) {
        const uint32_t attr = list->polys[i].attr;
        shadows |= ((attr >> 4) & 3u) == 3u && ((attr >> 24) & 63u) != 0u;
    }
    for (int i = 0; i < count; ++i) {
        const KhdaysG3dPolygon *p = &list->polys[i];
        const uint32_t attr = p->attr, mode = (attr >> 4) & 3u, id = (attr >> 24) & 63u, alpha = (attr >> 16) & 31u;
        const uint32_t format = (p->tex >> 26) & 7u;
        const int textured = format != 0u && (disp3dcnt & 1u) != 0u;
        const int marker = (p->flags & KHDAYS_G3D_MARKER) != 0;
        const int depth_test = marker ? DEPTH_ALWAYS : (attr & 0x4000u) ? DEPTH_EQUAL : DEPTH_LESS;
        const int z = zbuffer && depth_test != DEPTH_EQUAL;
        /* an opaque pixel writes the depth, a translucent one when the polygon says */
        const int write_opaque = !marker, write_translucent = !marker && (attr & 0x800u) != 0;
        /* the opaque polygon's ID only matters to shadows (not on their own ID): without them all one */
        const uint8_t opaque_reference = (uint8_t)(shadows ? id : 0u);
        const int opaque_key = KEY(depth_test, write_opaque, STENCIL_OPAQUE, blending, out, z);
        const int translucent_key =
            KEY(depth_test, write_translucent, (p->flags & KHDAYS_G3D_MOD) ? STENCIL_ANY_ID : STENCIL_SAME_ID, blending,
                out, z);
        int ok;
        if (p->flags & hidden) {
            continue; /* drawn by no one (gpu_raster3d.c hidden_flags) */
        }
        if (p->count < 1 || p->first < 0 || p->first > list->nverts - p->count || p->ymax < 0.0f ||
            p->ymin >= (float)KHDAYS_FRAME_H) {
            continue; /* outside the list's vertices (raster3d leaves it out too), or above or below the picture */
        }
        if (p->count < 3) {
            ++lines;
            continue;
        }
        if (p->flags & KHDAYS_G3D_EFFECT) { /* a KH2 effect's: its own blend and depth rules, as sent */
            const int effect_depth = (p->flags & KHDAYS_G3D_EFFECT_NO_DEPTH_TEST) ? DEPTH_ALWAYS : DEPTH_LESS;
            const int effect_write = (p->flags & KHDAYS_G3D_EFFECT_DEPTH_WRITE) != 0;
            if (run_count > 0 && !flush_run(list)) {
                return 0;
            }
            if (!add_polygon(p, i, PIXELS_ALL,
                             EFFECT_KEY(KEY(effect_depth, effect_write, STENCIL_EFFECT, blending, out, zbuffer),
                                        effect_blend(p)),
                             0, 0)) {
                return 0;
            }
            continue;
        }
        if (mode != 3u && alpha == 31u &&
            ((p->flags & KHDAYS_G3D_MOD) || (textured && (format == 1u || format == 6u || p->replacement >= 0)))) {
            if (!add_to_run(list, i, opaque_key, opaque_reference, translucent_key, (uint8_t)(0x40u | id))) {
                return 0;
            }
            continue;
        }
        if (run_count > 0 && !flush_run(list)) {
            return 0;
        }
        if (mode == 3u && id == 0u) {
            if (!masks && new_draw(CLEAR_MASKS, 0, 1) == NULL) {
                return 0;
            }
            masks = 1;
            if (!add_polygon(p, i, PIXELS_ALL, KEY(depth_test, 0, STENCIL_MASK, blending, out, z), 0x80, 0)) {
                return 0;
            }
            continue;
        }
        masks = 0;
        if (marker && !markers) {
            if (new_draw(CLEAR_TRANSLUCENT, 0, 1) == NULL) {
                return 0;
            }
            markers = 1;
        }
        if (mode == 3u) {
            ok = add_polygon(p, i, PIXELS_ALL, KEY(DEPTH_ALWAYS, 0, STENCIL_SHADOW_PRE, blending, out, z),
                             (uint8_t)(0x80u | id), 1) &&
                 add_polygon(p, i, PIXELS_ALL, KEY(depth_test, write_translucent, STENCIL_SHADOW, blending, out, z),
                             (uint8_t)(0xc0u | id), 1);
        } else if (alpha > 0u && alpha < 31u) {
            ok = add_polygon(p, i, PIXELS_ALL, translucent_key, (uint8_t)(0x40u | id), 0);
        } else {
            ok = add_polygon(p, i, PIXELS_ALL, opaque_key, opaque_reference, 0);
        }
        if (!ok) {
            return 0;
        }
    }
    if (run_count > 0 && !flush_run(list)) {
        return 0;
    }
    if (lines > 0 && !said) {
        said = 1;
        fprintf(stderr, "native 3D: %d polygons of fewer than three corners not drawn (said once)\n", lines);
    }
    return 1;
}

int khdays_native3d_prepare(const KhdaysG3dList *list, int count, int32_t hidden, const KhdaysNative3dFrame *frame,
                            uint32_t *corner_bytes)
{
    uint32_t bytes;
    if (device == NULL) {
        return SDL_SetError("the native 3D is not open");
    }
    /* the targets the finishing reads (finish3d.hlsli: edge marking, fog) or raster3d.hlsl resumes from */
    outputs = ((frame->clear_color >> 16) & 31u) == 0u || (frame->disp3dcnt & 0x4000u) ? OUT_FIRST : 0;
    outputs |= (frame->disp3dcnt & 0x80u) ? OUT_FOG : 0;
    /* raster3d.hlsl drawing after reads the opaque polygons' IDs too: its shadows fall on their receiver's */
    outputs |= (frame->disp3dcnt & 0x20u) || count < list->npolys ? OUT_IDS : 0;
    outputs |= (outputs & (OUT_FOG | OUT_IDS)) || count < list->npolys ? OUT_DEPTH : 0;
    if (!build(list, count, hidden, frame->disp3dcnt, outputs)) {
        return SDL_SetError("out of memory for the native 3D's draws");
    }
    for (int i = 0; i < draw_count; ++i) {
        if (draws[i].key >= 0 && polygon_pipeline(draws[i].key) == NULL) {
            return 0;
        }
    }
    if ((frame->disp3dcnt & 0x4000u) && rears[outputs] == NULL && (rears[outputs] = make_full(0, outputs)) == NULL) {
        return 0;
    }
    if (!make_targets(frame->width, frame->height)) {
        return 0;
    }
    bytes = corner_count * 2u * (uint32_t)sizeof(uint32_t);
    if (bytes > corner_buffer_size) {
        SDL_ReleaseGPUBuffer(device, corner_buffer);
        corner_buffer_size = bytes * 2u;
        corner_buffer = make_corner_buffer(corner_buffer_size);
        if (corner_buffer == NULL) {
            corner_buffer_size = 0;
            return 0;
        }
    }
    *corner_bytes = bytes;
    return 1;
}

const void *khdays_native3d_corners(void)
{
    return corners;
}

void khdays_native3d_upload(SDL_GPUCopyPass *copy, SDL_GPUTransferBuffer *upload, uint32_t offset, uint32_t bytes)
{
    SDL_GPUTransferBufferLocation src;
    SDL_GPUBufferRegion dst;
    if (bytes == 0) {
        return;
    }
    src.transfer_buffer = upload;
    src.offset = offset;
    dst.buffer = corner_buffer;
    dst.offset = 0;
    dst.size = bytes;
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
}

void khdays_native3d_draw(SDL_GPUCommandBuffer *cmd, const KhdaysNative3dFrame *frame, SDL_GPUBuffer *polys,
                          SDL_GPUBuffer *verts, SDL_GPUBuffer *vram, SDL_GPUBuffer *hd, SDL_GPUTexture *color3d,
                          SDL_GPUTexture *attr3d)
{
    Params pr;
    SDL_GPUColorTargetInfo colours[TARGETS];
    SDL_GPUDepthStencilTargetInfo ds;
    SDL_GPURenderPass *pass;
    SDL_zero(pr);
    pr.scale = (uint32_t)frame->scale;
    pr.width = (uint32_t)frame->width;
    pr.height = (uint32_t)frame->height;
    pr.disp3dcnt = frame->disp3dcnt;
    pr.swap_params = frame->swap_params;
    pr.alpha_ref = frame->alpha_ref;
    pr.clear_color = frame->clear_color;
    pr.clear_depth = frame->clear_depth;
    pr.smooth = frame->smooth;
    pr.origin_x = frame->origin_x;
    pr.fine = frame->fine;
    pr.outputs = (uint32_t)outputs;
    /* a DS pixel's column and row to the clip space's -1..1 (y up) */
    pr.to_clip[0] = 2.0f * (float)frame->scale / (float)frame->width;
    pr.to_clip[1] = -2.0f * (float)frame->origin_x * (float)frame->scale / (float)frame->width - 1.0f;
    pr.to_clip[2] = -2.0f * (float)frame->scale / (float)frame->height;
    pr.to_clip[3] = 1.0f;
    memcpy(pr.toon, frame->toon, sizeof(pr.toon));
    {
        /* the rear plane: the clear colour, depth and polygon ID as the targets' clear values (the clear image
         * drawn instead) */
        const uint32_t cc = frame->clear_color, cd = frame->clear_depth & 0x7fffu;
        const float cmax = frame->fine ? 255.0f : 63.0f;
        const uint32_t alpha = (cc >> 16) & 31u, fog = (cc >> 15) & 1u, id = (cc >> 24) & 63u;
        const uint32_t rear_depth = cd * 0x200u + ((cd + 1u) / 0x8000u) * 0x1ffu;
        float rgb[3];
        for (int c = 0; c < 3; ++c) {
            const uint32_t v = (cc >> (5 * c)) & 31u;
            rgb[c] = (float)(frame->fine ? (v << 3) | (v >> 2) : v != 0u ? v * 2u + 1u : 0u) / cmax;
        }
        SDL_zeroa(colours);
        for (int i = 0; i < TARGETS; ++i) {
            colours[i].texture = target[i];
            const int written = (target_output[i] & ~outputs) == 0;
            colours[i].load_op =
                written && !(frame->disp3dcnt & 0x4000u) ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_DONT_CARE;
            colours[i].store_op = written ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
            colours[i].cycle = true;
        }
        /* an empty pixel (alpha 0) takes the first translucent colour drawn on it whole; a shown one is the
         * colour later ones blend with, as if that first colour were black (native3d_rear.hlsl) */
        if (alpha > 0u) {
            colours[0].clear_color = (SDL_FColor){rgb[0], rgb[1], rgb[2], 0.0f};
            colours[1].clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};
        } else {
            colours[0].clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};
            colours[1].clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 0.0f};
        }
        colours[2].clear_color = (SDL_FColor){(float)fog, 0.0f, 0.0f, 1.0f};
        colours[3].clear_color = (SDL_FColor){(float)id / 255.0f, 0.0f, 0.0f, 1.0f};
        colours[4].clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, (float)alpha / 31.0f};
        colours[5].clear_color = (SDL_FColor){(float)rear_depth, 0.0f, 0.0f, 0.0f};
        SDL_zero(ds);
        ds.texture = depth;
        ds.clear_depth = (float)rear_depth / 16777216.0f;
        ds.load_op = SDL_GPU_LOADOP_CLEAR;
        ds.store_op = SDL_GPU_STOREOP_DONT_CARE;
        ds.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
        ds.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        ds.cycle = true;
        ds.clear_stencil = (Uint8)id;
    }
    SDL_PushGPUVertexUniformData(cmd, 0, &pr, sizeof(pr));
    SDL_PushGPUFragmentUniformData(cmd, 0, &pr, sizeof(pr));
    pass = SDL_BeginGPURenderPass(cmd, colours, TARGETS, &ds);
    if (pass != NULL) {
        SDL_GPUBuffer *buffers[4] = {polys, verts, vram, hd};
        SDL_GPUBufferBinding binding;
        SDL_GPUGraphicsPipeline *bound = NULL;
        int reference = -1; /* the stencil reference set, -1 none: set again only when it or the pipeline changes */
        binding.buffer = corner_buffer;
        binding.offset = 0;
        SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, 4);
        SDL_BindGPUFragmentStorageBuffers(pass, 0, buffers, 4);
        if (frame->disp3dcnt & 0x4000u) {
            bound = rears[outputs];
            SDL_BindGPUGraphicsPipeline(pass, bound);
            SDL_SetGPUStencilReference(pass, (Uint8)((frame->clear_color >> 24) & 63u));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }
        for (int i = 0; i < draw_count; ++i) {
            const Draw *d = &draws[i];
            SDL_GPUGraphicsPipeline *pipeline = d->key >= 0             ? pipelines[d->key]
                                                : d->key == CLEAR_MASKS ? clear_masks
                                                                        : clear_translucent;
            if (pipeline != bound) {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                if (d->key >= 0) {
                    /* after the pipeline: Direct3D 12's takes the corners' stride from the one bound */
                    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
                }
                bound = pipeline;
                reference = -1;
            }
            if (reference != (int)d->reference) {
                SDL_SetGPUStencilReference(pass, d->reference);
                reference = (int)d->reference;
            }
            if (d->key < 0) {
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            } else {
                SDL_DrawGPUPrimitives(pass, d->count, 1, d->first, 0);
            }
        }
        SDL_EndGPURenderPass(pass);
    }
    {
        /* the targets into raster3d's two */
        SDL_GPUStorageTextureReadWriteBinding io[TARGETS + 2];
        SDL_GPUComputePass *compute;
        SDL_zeroa(io);
        for (int i = 0; i < TARGETS; ++i) {
            io[i].texture = target[i];
        }
        io[TARGETS].texture = color3d;
        io[TARGETS].cycle = true;
        io[TARGETS + 1].texture = attr3d;
        io[TARGETS + 1].cycle = true;
        compute = SDL_BeginGPUComputePass(cmd, io, TARGETS + 2, NULL, 0);
        SDL_BindGPUComputePipeline(compute, pack);
        SDL_BindGPUComputeStorageBuffers(compute, 0, &vram, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &pr, sizeof(pr));
        SDL_DispatchGPUCompute(compute, (Uint32)(frame->width + 7) / 8, (Uint32)(frame->height + 7) / 8, 1);
        SDL_EndGPUComputePass(compute);
    }
}
