/* Engine A's screen at N x, drawn by the graphics card (gpu_raster3d.h): each frame uploads the
 * polygons, a per-tile index of them, the composition records and the changed texture pages;
 * raster3d.hlsl draws the 3D, compose.hlsl makes the screen. In widescreen the HUD's polygons move to
 * the sides as the 2D's pieces do (docs/RENDERING.md, "On the graphics card"). */
#include "gpu_raster3d.h"
#include "gpu_hd.h"
#include "../../runtime/display/frame_package.h"
#include "../../runtime/mods/textures.h"
#include "../../ui/composition.h"
#include "generated/compose_cs.h"
#include "generated/raster3d_cs.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TILE 8                           /* a tile's side, in DS pixels */
#define MAX_COLUMNS (3 * KHDAYS_FRAME_W) /* the widest picture (see gpu_present.c) */
#define MAX_TILES_X (MAX_COLUMNS / TILE)
#define TILES_Y (KHDAYS_FRAME_H / TILE)
#define MAX_TILES (MAX_TILES_X * TILES_Y)
#define TEX_PAGES (KHDAYS_FRAME_TEX_BYTES / KHDAYS_FRAME_VRAM_PAGE)
#define TEXPAL_PAGES (KHDAYS_FRAME_TEXPAL_BYTES / KHDAYS_FRAME_VRAM_PAGE)
#define VRAM_PAGES (TEX_PAGES + TEXPAL_PAGES)
#define VRAM_BYTES (KHDAYS_FRAME_TEX_BYTES + KHDAYS_FRAME_TEXPAL_BYTES)
#define RECORD_BYTES (KHDAYS_GPU2D_MAX_COLUMNS * KHDAYS_FRAME_H * sizeof(KhdaysGpu2dComposition))

/* The shaders' constant buffers, register for register. */
typedef struct RasterParams {
    uint32_t scale, width, height, disp3dcnt, swap_params, tiles_x, tiles_y, alpha_ref;
    uint32_t clear_color, clear_depth, smooth;
    int32_t origin_x;
    uint32_t fine, npolys, nverts, entries; /* how much was uploaded: the shader stays within it */
    uint32_t toon[16];
} RasterParams;

typedef struct ComposeParams {
    uint32_t scale, width, height, disp3dcnt, swap_params, clear_color, clear_depth, fog_color;
    uint32_t fog_offset, eva, evb, evy, master_bright, bg0_hofs;
    int32_t origin_x;
    uint32_t backdrop;       /* the backdrop colour, with BLDCNT in the top 16 bits */
    int32_t record_origin;   /* the DS column where the records start */
    uint32_t record_columns; /* how many columns the records have */
    uint32_t unused0, unused1;
    uint32_t edge_colors[4];
    uint32_t fog_table[8];
} ComposeParams;

static SDL_GPUDevice *device;
static SDL_GPUComputePipeline *raster, *compose;
static SDL_GPUBuffer *polys, *verts, *tiles, *vram, *records, *sequences;
static uint32_t polys_capacity, verts_capacity; /* entries in `polys` and `verts`: the most any list has had */
static int fine;                                /* draw the 3D in 8 bits per channel (khdays_raster3d_set_fine) */
static uint32_t tiles_capacity;                 /* entries in `tiles` */
static SDL_GPUTransferBuffer *upload;
static uint32_t upload_size;
static uint32_t vram_version[VRAM_PAGES]; /* the change counts of the card's copy */
static int vram_valid;
static SDL_GPUTexture *color3d, *attr3d, *screen;
static SDL_GPUTextureUsageFlags target_usage;
static int target_scale, target_columns;
static SDL_Texture *wrapped;
static SDL_Renderer *wrapped_for;
static uint32_t *tile_list;
static uint32_t tile_list_capacity;
/* Work space that grows with the lists: the vertices with the HUD moved, the
 * boxes and groups of the HUD's polygons, and each polygon's tiles. */
static KhdaysG3dVertex *moved;
static int *hud_index, *hud_group;
static float *hud_x0, *hud_x1, *hud_y0, *hud_y1;
static uint8_t (*box)[4];
static int32_t scratch_polys, scratch_verts;

static int scratch_for(const KhdaysG3dList *list)
{
    if (list->npolys > scratch_polys) {
        const int32_t n = list->npolys * 2;
        int *a = (int *)SDL_realloc(hud_index, sizeof(int) * (size_t)n);
        int *b = a != NULL ? (int *)SDL_realloc(hud_group, sizeof(int) * (size_t)n) : NULL;
        float *c = b != NULL ? (float *)SDL_realloc(hud_x0, sizeof(float) * (size_t)n * 4) : NULL;
        uint8_t(*d)[4] = c != NULL ? (uint8_t(*)[4])SDL_realloc(box, sizeof(box[0]) * (size_t)n) : NULL;
        if (a != NULL)
            hud_index = a;
        if (b != NULL)
            hud_group = b;
        if (c != NULL)
            hud_x0 = c;
        if (d != NULL)
            box = d;
        if (d == NULL) {
            return 0;
        }
        hud_x1 = hud_x0 + n;
        hud_y0 = hud_x0 + 2 * n;
        hud_y1 = hud_x0 + 3 * n;
        scratch_polys = n;
    }
    if (list->nverts > scratch_verts) {
        const int32_t n = list->nverts * 2;
        KhdaysG3dVertex *v = (KhdaysG3dVertex *)SDL_realloc(moved, sizeof(KhdaysG3dVertex) * (size_t)n);
        if (v == NULL) {
            return 0;
        }
        moved = v;
        scratch_verts = n;
    }
    return 1;
}

static SDL_GPUComputePipeline *make_pipeline(const unsigned char *dxil, unsigned dxil_size, const unsigned char *spirv,
                                             unsigned spirv_size, const unsigned char *msl, unsigned msl_size,
                                             const char *entry, Uint32 ro_textures, Uint32 ro_buffers,
                                             Uint32 rw_textures)
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
    info.num_readonly_storage_textures = ro_textures;
    info.num_readonly_storage_buffers = ro_buffers;
    info.num_readwrite_storage_textures = rw_textures;
    info.num_uniform_buffers = 1;
    info.threadcount_x = 8;
    info.threadcount_y = 8;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

static SDL_GPUBuffer *make_buffer(Uint32 size)
{
    SDL_GPUBufferCreateInfo info;
    SDL_zero(info);
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    info.size = size;
    return SDL_CreateGPUBuffer(device, &info);
}

int khdays_raster3d_open(SDL_GPUDevice *d)
{
    device = d;
    raster = make_pipeline(raster3d_cs_dxil, raster3d_cs_dxil_size, raster3d_cs_spirv, raster3d_cs_spirv_size,
                           raster3d_cs_msl, raster3d_cs_msl_size, RASTER3D_CS_ENTRY, 0, 6, 2);
    compose = make_pipeline(compose_cs_dxil, compose_cs_dxil_size, compose_cs_spirv, compose_cs_spirv_size,
                            compose_cs_msl, compose_cs_msl_size, COMPOSE_CS_ENTRY, 0, 1, 3);
    polys_capacity = KHDAYS_G3D_DS_POLYS;
    verts_capacity = KHDAYS_G3D_DS_VERTS;
    polys = make_buffer(polys_capacity * (Uint32)sizeof(KhdaysG3dPolygon));
    verts = make_buffer(verts_capacity * (Uint32)sizeof(KhdaysG3dVertex));
    vram = make_buffer(VRAM_BYTES);
    records = make_buffer((Uint32)RECORD_BYTES);
    sequences = make_buffer((Uint32)sizeof(((KhdaysFramePackage *)0)->sequence3d_images));
    tiles_capacity = 1u << 18;
    tiles = make_buffer(tiles_capacity * 4u);
    vram_valid = 0;
    target_usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_R32_UINT, SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE)) {
        target_usage |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;
    }
    if (raster == NULL || compose == NULL || polys == NULL || verts == NULL || vram == NULL || records == NULL ||
        tiles == NULL || sequences == NULL || !khdays_gpu_hd_open(device)) {
        char why[256];
        SDL_strlcpy(why, SDL_GetError(), sizeof(why));
        khdays_raster3d_close();
        SDL_SetError("%s", why);
        return 0;
    }
    return 1;
}

static void drop_targets(void)
{
    if (wrapped != NULL) {
        SDL_DestroyTexture(wrapped);
        wrapped = NULL;
    }
    wrapped_for = NULL;
    if (color3d != NULL)
        SDL_ReleaseGPUTexture(device, color3d);
    if (attr3d != NULL)
        SDL_ReleaseGPUTexture(device, attr3d);
    if (screen != NULL)
        SDL_ReleaseGPUTexture(device, screen);
    color3d = attr3d = screen = NULL;
    target_scale = target_columns = 0;
}

void khdays_raster3d_close(void)
{
    if (device == NULL) {
        return;
    }
    drop_targets();
    if (raster != NULL)
        SDL_ReleaseGPUComputePipeline(device, raster);
    if (compose != NULL)
        SDL_ReleaseGPUComputePipeline(device, compose);
    if (polys != NULL)
        SDL_ReleaseGPUBuffer(device, polys);
    if (verts != NULL)
        SDL_ReleaseGPUBuffer(device, verts);
    khdays_gpu_hd_close();
    if (tiles != NULL)
        SDL_ReleaseGPUBuffer(device, tiles);
    if (vram != NULL)
        SDL_ReleaseGPUBuffer(device, vram);
    if (records != NULL)
        SDL_ReleaseGPUBuffer(device, records);
    if (sequences != NULL)
        SDL_ReleaseGPUBuffer(device, sequences);
    if (upload != NULL)
        SDL_ReleaseGPUTransferBuffer(device, upload);
    raster = compose = NULL;
    polys = verts = tiles = vram = records = sequences = NULL;
    upload = NULL;
    upload_size = 0;
    SDL_free(tile_list);
    tile_list = NULL;
    tile_list_capacity = 0;
    SDL_free(moved);
    SDL_free(hud_index);
    SDL_free(hud_group);
    SDL_free(hud_x0);
    SDL_free(box);
    moved = NULL;
    hud_index = hud_group = NULL;
    hud_x0 = hud_x1 = hud_y0 = hud_y1 = NULL;
    box = NULL;
    scratch_polys = scratch_verts = 0;
    polys_capacity = verts_capacity = 0;
    device = NULL;
}

static SDL_GPUTexture *make_target(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int w, int h)
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

/* Makes sure the targets exist for `scale`, `columns` DS pixels wide. */
static int targets(int scale, int columns)
{
    const int w = columns * scale, h = KHDAYS_FRAME_H * scale;
    if (scale != target_scale || columns != target_columns) {
        drop_targets();
        color3d = make_target(SDL_GPU_TEXTUREFORMAT_R32_UINT, target_usage, w, h);
        attr3d = make_target(SDL_GPU_TEXTUREFORMAT_R32_UINT, target_usage, w, h);
        screen = make_target(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                             SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, w, h);
        if (color3d == NULL || attr3d == NULL || screen == NULL) {
            char why[256];
            SDL_strlcpy(why, SDL_GetError(), sizeof(why));
            drop_targets();
            SDL_SetError("%s", why);
            return 0;
        }
        target_scale = scale;
        target_columns = columns;
    }
    return 1;
}

SDL_Texture *khdays_raster3d_texture(SDL_Renderer *renderer)
{
    const int w = target_columns * target_scale, h = KHDAYS_FRAME_H * target_scale;
    if (screen == NULL) {
        return NULL;
    }
    if (wrapped == NULL || wrapped_for != renderer) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        if (wrapped != NULL) {
            SDL_DestroyTexture(wrapped);
        }
        SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, screen);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ABGR8888);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
        wrapped = SDL_CreateTextureWithProperties(renderer, props);
        SDL_DestroyProperties(props);
        if (wrapped == NULL) {
            return NULL;
        }
        SDL_SetTextureBlendMode(wrapped, SDL_BLENDMODE_NONE);
        wrapped_for = renderer;
    }
    return wrapped;
}

/* Finds the HUD's pieces. Each HUD polygon gets its list index in hud_index
 * and its box in hud_x0..hud_y1; polygons whose boxes touch (within a pixel)
 * share a group in hud_group -- the digits of a number, a gauge and its
 * frame. With `over_2d`, the polygons shown over the 2D count too. Returns
 * how many polygons. */
static const KhdaysSequenceImage *hud_programs;

static int hud_pieces(const KhdaysG3dList *list, int over_2d)
{
    int *const hud = hud_index, *const group = hud_group;
    float *const x0 = hud_x0, *const x1 = hud_x1, *const y0 = hud_y0, *const y1 = hud_y1;
    int n = 0;
    for (int i = 0; i < list->npolys; ++i) {
        const KhdaysG3dPolygon *p = &list->polys[i];
        if (!(p->flags & KHDAYS_G3D_HUD) || (!over_2d && (p->flags & KHDAYS_G3D_OVER_2D)) || p->count <= 0 ||
            p->first < 0 || p->first + p->count > list->nverts) {
            continue;
        }
        x0[n] = 1e9f;
        x1[n] = -1e9f;
        for (int k = 0; k < p->count; ++k) {
            const float x = list->verts[p->first + k].x;
            x0[n] = x < x0[n] ? x : x0[n];
            x1[n] = x > x1[n] ? x : x1[n];
        }
        y0[n] = p->ymin;
        y1[n] = p->ymax;
        if ((p->flags & KHDAYS_G3D_SEQUENCE) && hud_programs != NULL && p->pltt / 4u < KSI_3D_PROGRAMS) {
            const unsigned *words = hud_programs[p->pltt / 4u].words;
            if (words[KSI_OUTPUT] == p->pltt && words[KSI_ANCHORED]) {
                memcpy(&x0[n], words + KSI_ANCHOR_X, 4);
                memcpy(&y0[n], words + KSI_ANCHOR_Y, 4);
                x1[n] = x0[n];
                y1[n] = y0[n];
            }
        }
        hud[n] = i;
        group[n] = n;
        ++n;
    }
    /* join the groups of polygons whose boxes touch (within a pixel), until
     * no two groups touch any more */
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (group[j] != group[i] && x0[i] <= x1[j] + 1 && x0[j] <= x1[i] + 1 && y0[i] <= y1[j] + 1 &&
                    y0[j] <= y1[i] + 1) {
                    const int from = group[j] > group[i] ? group[j] : group[i];
                    const int to = group[j] < group[i] ? group[j] : group[i];
                    for (int k = 0; k < n; ++k) {
                        group[k] = group[k] == from ? to : group[k];
                    }
                    changed = 1;
                }
            }
        }
    }
    return n;
}

/* Moves the HUD's polygons to the sides of a picture `margin` DS columns
 * wider on each side, writing the moved vertices into `moved`. Returns
 * `moved`, or the list's own vertices when nothing moves. (A polygon shown
 * over the 2D stays in place, like a piece across the middle.) */
static const KhdaysG3dVertex *move_hud(const KhdaysG3dList *list, int margin)
{
    int *const hud = hud_index, *const group = hud_group;
    float *const x0 = hud_x0, *const x1 = hud_x1;
    int n;
    if (margin <= 0) {
        return list->verts;
    }
    n = hud_pieces(list, 0);
    if (n == 0) {
        return list->verts;
    }
    memcpy(moved, list->verts, sizeof(KhdaysG3dVertex) * (size_t)list->nverts);
    for (int g = 0; g < n; ++g) {
        /* one group: find its box and its side, then move its polygons */
        float gx0 = 1e9f, gx1 = -1e9f, shift;
        int members = 0;
        for (int i = 0; i < n; ++i) {
            if (group[i] == g) {
                gx0 = x0[i] < gx0 ? x0[i] : gx0;
                gx1 = x1[i] > gx1 ? x1[i] : gx1;
                ++members;
            }
        }
        if (members == 0) {
            continue;
        }
        shift = gx1 < KHDAYS_FRAME_W / 2 ? (float)-margin : gx0 >= KHDAYS_FRAME_W / 2 ? (float)margin : 0.0f;
        for (int i = 0; i < n; ++i) {
            const KhdaysG3dPolygon *p = &list->polys[hud[i]];
            if (group[i] != g || shift == 0.0f) {
                continue;
            }
            for (int k = 0; k < p->count; ++k) {
                moved[p->first + k].x += shift;
            }
        }
    }
    return moved;
}

/* Whether a polygon is drawn in screen space (an orthographic projection:
 * every w is 1) and is not the HUD's. */
static int overlay_polygon(const KhdaysG3dPolygon *p, const KhdaysG3dVertex *v)
{
    if ((p->flags & KHDAYS_G3D_HUD) || p->count <= 0) {
        return 0;
    }
    for (int k = 0; k < p->count; ++k) {
        if (v[p->first + k].w != 4096.0f) {
            return 0;
        }
    }
    return 1;
}

/* Stretches a picture drawn over the whole DS view to the target's width: screen-space polygons (not
 * the HUD's) that together cover the view (Sora's memories, a fade drawn as a quad) have nothing
 * beside it, so they are stretched from 256 columns to `columns`; smaller ones keep their shape.
 * Returns `moved`, or `in` when nothing moves. */
static int stretched; /* stretch_overlay stretched this list's screen-space polygons */

static const KhdaysG3dVertex *stretch_overlay(const KhdaysG3dList *list, const KhdaysG3dVertex *in, int columns)
{
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    const float k = (float)columns / (float)KHDAYS_FRAME_W, mid = (float)KHDAYS_FRAME_W / 2.0f;
    int any = 0;
    stretched = 0;
    if (columns <= KHDAYS_FRAME_W) {
        return in;
    }
    for (int i = 0; i < list->npolys; ++i) {
        const KhdaysG3dPolygon *p = &list->polys[i];
        if (!overlay_polygon(p, in)) {
            continue;
        }
        for (int j = 0; j < p->count; ++j) {
            const float x = in[p->first + j].x;
            x0 = x < x0 ? x : x0;
            x1 = x > x1 ? x : x1;
        }
        y0 = p->ymin < y0 ? p->ymin : y0;
        y1 = p->ymax > y1 ? p->ymax : y1;
        any = 1;
    }
    if (!any || x0 > 0.5f || x1 < (float)KHDAYS_FRAME_W - 0.5f || y0 > 0.5f || y1 < (float)KHDAYS_FRAME_H - 0.5f) {
        return in;
    }
    if (in != moved) {
        memcpy(moved, in, sizeof(KhdaysG3dVertex) * (size_t)list->nverts);
    }
    for (int i = 0; i < list->npolys; ++i) {
        const KhdaysG3dPolygon *p = &list->polys[i];
        if (!overlay_polygon(p, in)) {
            continue;
        }
        for (int j = 0; j < p->count; ++j) {
            moved[p->first + j].x = mid + (in[p->first + j].x - mid) * k;
        }
    }
    stretched = 1;
    return moved;
}

/* Builds the index of which polygons may cover each tile, in drawing order, for a target `tiles_x`
 * tiles wide whose first column is DS column `origin_x`: tiles_x * TILES_Y + 1 start offsets, then the
 * polygon indices. A box is one DS pixel wider each side than its vertices (at N x a polygon covers
 * pixels within half a target pixel of its edges). Returns the number of entries. */
/* What the last draws sent to the graphics card, for a report when it stops
 * answering (khdays_raster3d_recent). A ring: the oldest is overwritten. */
typedef struct Recent {
    uint32_t frame;
    int scale, width, height, npolys, nverts, between;
    uint32_t entries, busiest; /* tile index entries, and the most polygons in one tile */
} Recent;
static Recent recent[8];
static unsigned recent_next;
static uint32_t busiest_tile;

/* Whether a polygon is unsafe for the shader: its vertices point outside the
 * list's. (Reported once; such a polygon is left out of every tile.) */
static int unusable(const KhdaysG3dList *list, const KhdaysG3dPolygon *p)
{
    static int said;
    if (p->count >= 1 && p->first >= 0 && p->first <= list->nverts - p->count) {
        return 0;
    }
    if (!said) {
        said = 1;
        fprintf(stderr, "host: a 3D polygon with vertices %d..%d of a list of %d left out (said once)\n", p->first,
                p->first + p->count - 1, list->nverts);
    }
    return 1;
}

static uint32_t build_tiles(const KhdaysG3dList *list, const KhdaysG3dVertex *vertices, int tiles_x, int origin_x)
{
    static uint32_t count[MAX_TILES], cursor[MAX_TILES];
    const int tile_count = tiles_x * TILES_Y;
    uint32_t total = 0, need;
    memset(count, 0, sizeof(count));
    busiest_tile = 0;
    for (int i = 0; i < list->npolys; ++i) {
        const KhdaysG3dPolygon *p = &list->polys[i];
        float xmin = 1e9f, xmax = -1e9f;
        int tx0, tx1, ty0, ty1;
        if (unusable(list, p)) {
            box[i][0] = 1;
            box[i][1] = 0; /* empty */
            continue;
        }
        for (int k = 0; k < p->count; ++k) {
            const float x = vertices[p->first + k].x - (float)origin_x;
            xmin = x < xmin ? x : xmin;
            xmax = x > xmax ? x : xmax;
        }
        tx0 = (int)floorf((xmin - 1.0f) / TILE);
        tx1 = (int)floorf((xmax + 1.0f) / TILE);
        ty0 = (int)floorf((p->ymin - 1.0f) / TILE);
        ty1 = (int)floorf((p->ymax + 1.0f) / TILE);
        tx0 = tx0 < 0 ? 0 : tx0;
        ty0 = ty0 < 0 ? 0 : ty0;
        tx1 = tx1 > tiles_x - 1 ? tiles_x - 1 : tx1;
        ty1 = ty1 > TILES_Y - 1 ? TILES_Y - 1 : ty1;
        if (p->count <= 0 || tx1 < tx0 || ty1 < ty0) {
            box[i][0] = 1;
            box[i][1] = 0; /* empty */
            continue;
        }
        box[i][0] = (uint8_t)tx0;
        box[i][1] = (uint8_t)tx1;
        box[i][2] = (uint8_t)ty0;
        box[i][3] = (uint8_t)ty1;
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                ++count[ty * tiles_x + tx];
            }
        }
    }
    for (int t = 0; t < tile_count; ++t) {
        total += count[t];
        busiest_tile = count[t] > busiest_tile ? count[t] : busiest_tile;
    }
    need = (uint32_t)tile_count + 1 + total;
    if (need > tile_list_capacity) {
        uint32_t *grown = (uint32_t *)SDL_realloc(tile_list, need * sizeof(uint32_t) * 2);
        if (grown == NULL) {
            return 0;
        }
        tile_list = grown;
        tile_list_capacity = need * 2;
    }
    {
        uint32_t at = (uint32_t)tile_count + 1;
        for (int t = 0; t < tile_count; ++t) {
            tile_list[t] = at;
            cursor[t] = at;
            at += count[t];
        }
        tile_list[tile_count] = at;
    }
    for (int i = 0; i < list->npolys; ++i) {
        if (box[i][0] > box[i][1]) {
            continue;
        }
        for (int ty = box[i][2]; ty <= box[i][3]; ++ty) {
            for (int tx = box[i][0]; tx <= box[i][1]; ++tx) {
                tile_list[cursor[ty * tiles_x + tx]++] = (uint32_t)i;
            }
        }
    }
    return need;
}

static uint32_t io32(const uint8_t *io3d, unsigned offset)
{
    return (uint32_t)io3d[offset] | (uint32_t)io3d[offset + 1] << 8 | (uint32_t)io3d[offset + 2] << 16 |
           (uint32_t)io3d[offset + 3] << 24;
}

static const KhdaysG3dList *list_instead; /* a list to draw instead of the package's (khdays_raster3d_use_list) */
static int one_screen; /* khdays_raster3d_set_one_screen: do not move the HUD's polygons to the sides */
static const KhdaysShape3d *hud_shapes; /* where the one-screen layout puts the HUD (khdays_raster3d_set_hud_shapes) */
static int hud_shape_count;
static KhdaysG3dPolygon *placed_polys; /* the list's polygons with the HUD's placed (grows as needed) */
static int32_t placed_capacity;

void khdays_raster3d_set_one_screen(int on)
{
    one_screen = on;
}

void khdays_raster3d_set_hud_shapes(const KhdaysShape3d *shapes, int count)
{
    hud_shapes = shapes;
    hud_shape_count = count;
}

/* The shapes of group "all": every polygon whose box's middle is in a shape's rectangle is placed as
 * the shape says (a model in a menu's frame, drawn larger; each vertex moved once), every other one
 * hidden; a `whole` shape places them all, and a `world` one leaves the HUD's where place_hud put them.
 * Works on placed_polys and moved, which place_hud filled; whether anything moved. */
static uint8_t *vertex_moved; /* a flag per vertex (grows as needed) */
static int32_t vertex_moved_room;

static int place_all(const KhdaysG3dList *list, int origin_x)
{
    int any = 0;
    if (list->nverts > vertex_moved_room) {
        uint8_t *grown = (uint8_t *)SDL_realloc(vertex_moved, (size_t)list->nverts * 2);
        if (grown == NULL) {
            return 0;
        }
        vertex_moved = grown;
        vertex_moved_room = list->nverts * 2;
    }
    for (int s = 0; s < hud_shape_count; ++s) {
        const KhdaysShape3d *h = &hud_shapes[s];
        if (h->group != KHDAYS_SHAPE3D_ALL) {
            continue;
        }
        memset(vertex_moved, 0, (size_t)list->nverts);
        for (int i = 0; i < list->npolys; ++i) {
            KhdaysG3dPolygon *p = &placed_polys[i];
            float x0 = 1e9f, x1 = -1e9f, mx, my;
            if (p->count <= 0 || p->first < 0 || p->first + p->count > list->nverts ||
                (h->world && (p->flags & KHDAYS_G3D_HUD))) {
                continue;
            }
            for (int k = 0; k < p->count; ++k) {
                const float x = list->verts[p->first + k].x;
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
            }
            mx = (x0 + x1) / 2.0f;
            my = (list->polys[i].ymin + list->polys[i].ymax) / 2.0f;
            if (!h->whole && (mx < h->src_x || mx > h->src_x + h->src_w || my < h->src_y || my > h->src_y + h->src_h)) {
                p->ymin = p->ymax = -100000.0f;
                any = 1;
                continue;
            }
            for (int k = 0; k < p->count; ++k) {
                const int v = p->first + k;
#pragma warning(                                                                                                       \
    suppress : 6385) /* v is below list->nverts (checked above) and the flags are as many: grown at the top */
                if (!vertex_moved[v]) {
                    moved[v].x = (list->verts[v].x - h->src_x) * h->scale + h->x + (float)origin_x;
                    moved[v].y = (list->verts[v].y - h->src_y) * h->scale + h->y;
                    vertex_moved[v] = 1;
                }
            }
            p->ymin = (list->polys[i].ymin - h->src_y) * h->scale + h->y;
            p->ymax = (list->polys[i].ymax - h->src_y) * h->scale + h->y;
            any = 1;
        }
    }
    return any;
}

/* Places the HUD on the one-screen picture: each piece of the HUD lying wholly inside a 3D shape's
 * rectangle (the first that holds it) is placed as the shape says, or hidden; `placed` gets the list.
 * By pieces, not polygons, so a number crossing a shape's edge stays whole where the game draws it (the
 * portal countdown's hundreds digit was lost to the hearts counter's shape). Returns 0 when no shape
 * moves anything. */
static int place_hud(const KhdaysG3dList *list, int origin_x, KhdaysG3dList *placed)
{
    int *const hud = hud_index, *const group = hud_group;
    float *const x0 = hud_x0, *const x1 = hud_x1, *const y0 = hud_y0, *const y1 = hud_y1;
    int any = 0, n;
    if (hud_shape_count == 0 || list->npolys <= 0) {
        return 0;
    }
    if (list->npolys > placed_capacity) {
        const int32_t grown_to = list->npolys * 2;
        KhdaysG3dPolygon *grown =
            (KhdaysG3dPolygon *)SDL_realloc(placed_polys, sizeof(KhdaysG3dPolygon) * (size_t)grown_to);
        if (grown == NULL) {
            return 0;
        }
        placed_polys = grown;
        placed_capacity = grown_to;
    }
    memcpy(placed_polys, list->polys, sizeof(KhdaysG3dPolygon) * (size_t)list->npolys);
    memcpy(moved, list->verts, sizeof(KhdaysG3dVertex) * (size_t)list->nverts);
    n = hud_pieces(list, 1);
    for (int g = 0; g < n; ++g) {
        /* one piece: find its box and the first shape that holds it whole, then place its polygons */
        float gx0 = 1e9f, gx1 = -1e9f, gy0 = 1e9f, gy1 = -1e9f;
        int members = 0;
        const KhdaysShape3d *h = NULL;
        for (int i = 0; i < n; ++i) {
            if (group[i] == g) {
                gx0 = x0[i] < gx0 ? x0[i] : gx0;
                gx1 = x1[i] > gx1 ? x1[i] : gx1;
                gy0 = y0[i] < gy0 ? y0[i] : gy0;
                gy1 = y1[i] > gy1 ? y1[i] : gy1;
                ++members;
            }
        }
        if (members == 0) {
            continue;
        }
        for (int s = 0; s < hud_shape_count && h == NULL; ++s) {
            const KhdaysShape3d *c = &hud_shapes[s];
            if (c->group == KHDAYS_SHAPE3D_HUD && gx0 >= c->src_x && gx1 <= c->src_x + c->src_w && gy0 >= c->src_y &&
                gy1 <= c->src_y + c->src_h) {
                h = c;
            }
        }
        if (h == NULL) {
            continue;
        }
        for (int i = 0; i < n; ++i) {
            KhdaysG3dPolygon *p = &placed_polys[hud[i]];
            if (group[i] != g) {
                continue;
            }
            for (int k = 0; k < p->count; ++k) {
                KhdaysG3dVertex *v = &moved[p->first + k];
                if (h->hide) {
                    v->x = v->y = -100000.0f;
                } else {
                    /* the picture's columns are the 3D target's: the vertex's own column is that minus origin_x */
                    v->x = (v->x - h->src_x) * h->scale + h->x + (float)origin_x;
                    v->y = (v->y - h->src_y) * h->scale + h->y;
                }
            }
            if (h->hide) {
                p->ymin = p->ymax = -100000.0f;
            } else {
                p->ymin = (p->ymin - h->src_y) * h->scale + h->y;
                p->ymax = (p->ymax - h->src_y) * h->scale + h->y;
            }
        }
        any = 1;
    }
    any |= place_all(list, origin_x);
    *placed = *list;
    placed->polys = placed_polys;
    placed->verts = moved;
    return any;
}

void khdays_raster3d_use_list(const KhdaysG3dList *list)
{
    list_instead = list;
}

static int draw(const KhdaysFramePackage *p, int scale, int texture_smoothing, int compose_too)
{
    const KhdaysG3dList *list = list_instead != NULL ? list_instead : &p->list3d;
    /* the DS's width, or the extra columns the list was made to cover */
    const int wide = (int)SDL_lroundf(list->widen * KHDAYS_FRAME_W);
    const int columns = wide < KHDAYS_FRAME_W ? KHDAYS_FRAME_W : wide > MAX_COLUMNS ? MAX_COLUMNS : wide;
    const int origin_x = -(columns - KHDAYS_FRAME_W) / 2;
    const int tiles_x = (columns + TILE - 1) / TILE;
    const int w = columns * scale, h = KHDAYS_FRAME_H * scale;
    const int scratch_ok = scratch_for(list);
    hud_programs = p->sequence3d_images;
    KhdaysG3dList placed;
    /* In widescreen the HUD's polygons go to the sides; on one screen where the composition's 3D shapes
     * place them, or else where the game draws them. */
    if (scratch_ok && one_screen && place_hud(list, origin_x, &placed)) {
        list = &placed;
    }
    const KhdaysG3dVertex *vertices =
        scratch_ok ? stretch_overlay(list, one_screen ? list->verts : move_hud(list, -origin_x), columns) : NULL;
    const uint32_t poly_bytes = (uint32_t)(list->npolys * (int)sizeof(KhdaysG3dPolygon));
    const uint32_t vert_bytes = (uint32_t)(list->nverts * (int)sizeof(KhdaysG3dVertex));
    /* The 2D's records cover the wide picture when the package has them for
     * this width; otherwise only the DS's 256 columns (and the sides compose
     * the 3D over the backdrop). */
    const int record_columns = p->wide_columns == columns ? columns : KHDAYS_FRAME_W;
    const uint32_t record_bytes =
        compose_too ? (uint32_t)(record_columns * KHDAYS_FRAME_H) * (uint32_t)sizeof(KhdaysGpu2dComposition) : 0;
    const uint8_t *io3d = p->io3d;
    uint32_t entries, tile_bytes, size, at;
    int changed[VRAM_PAGES], nchanged = 0;
    SDL_GPUCommandBuffer *cmd;
    uint8_t *mapped;
    if (device == NULL || !scratch_ok || !targets(scale, columns)) {
        return 0;
    }
    /* the polygon and vertex buffers grow with the lists: no limit but memory */
    if ((uint32_t)list->npolys > polys_capacity) {
        SDL_ReleaseGPUBuffer(device, polys);
        polys_capacity = (uint32_t)list->npolys * 2u;
        polys = make_buffer(polys_capacity * (Uint32)sizeof(KhdaysG3dPolygon));
        if (polys == NULL) {
            polys_capacity = 0;
            return 0;
        }
    }
    if ((uint32_t)list->nverts > verts_capacity) {
        SDL_ReleaseGPUBuffer(device, verts);
        verts_capacity = (uint32_t)list->nverts * 2u;
        verts = make_buffer(verts_capacity * (Uint32)sizeof(KhdaysG3dVertex));
        if (verts == NULL) {
            verts_capacity = 0;
            return 0;
        }
    }
    entries = build_tiles(list, vertices, tiles_x, origin_x);
    if (entries == 0) {
        SDL_SetError("out of memory for the tiles");
        return 0;
    }
    tile_bytes = entries * 4u;
    if (entries > tiles_capacity) {
        SDL_ReleaseGPUBuffer(device, tiles);
        tiles_capacity = entries * 2u;
        tiles = make_buffer(tiles_capacity * 4u);
        if (tiles == NULL) {
            tiles_capacity = 0;
            return 0;
        }
    }
    for (int n = 0; n < VRAM_PAGES; ++n) {
        const uint32_t v = n < TEX_PAGES ? p->tex_version[n] : p->texpal_version[n - TEX_PAGES];
        if (!vram_valid || vram_version[n] != v) {
            changed[nchanged++] = n;
            vram_version[n] = v;
        }
    }
    vram_valid = 1;
    const uint32_t sequence_bytes = (uint32_t)sizeof(p->sequence3d_images);
    size = poly_bytes + vert_bytes + tile_bytes + record_bytes + sequence_bytes +
           (uint32_t)nchanged * KHDAYS_FRAME_VRAM_PAGE;
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
            vram_valid = 0;
            return 0;
        }
    }
    mapped = (uint8_t *)SDL_MapGPUTransferBuffer(device, upload, true);
    if (mapped == NULL) {
        vram_valid = 0;
        return 0;
    }
    at = 0;
    memcpy(mapped + at, list->polys, poly_bytes);
    at += poly_bytes;
    memcpy(mapped + at, vertices, vert_bytes);
    at += vert_bytes;
    memcpy(mapped + at, tile_list, tile_bytes);
    at += tile_bytes;
    if (record_bytes != 0) {
        memcpy(mapped + at, record_columns > KHDAYS_FRAME_W ? (const void *)p->wide : (const void *)p->composition,
               record_bytes);
    }
    at += record_bytes;
    memcpy(mapped + at, p->sequence3d_images, sequence_bytes);
    at += sequence_bytes;
    for (int k = 0; k < nchanged; ++k) {
        const int n = changed[k];
        const uint8_t *page = n < TEX_PAGES ? p->tex + (size_t)n * KHDAYS_FRAME_VRAM_PAGE
                                            : p->texpal + (size_t)(n - TEX_PAGES) * KHDAYS_FRAME_VRAM_PAGE;
        memcpy(mapped + at + (uint32_t)k * KHDAYS_FRAME_VRAM_PAGE, page, KHDAYS_FRAME_VRAM_PAGE);
    }
    SDL_UnmapGPUTransferBuffer(device, upload);

    cmd = SDL_AcquireGPUCommandBuffer(device);
    if (cmd == NULL) {
        vram_valid = 0;
        return 0;
    }
    if (!khdays_gpu_hd_sync(cmd)) { /* upload the HD replacements the card does not have yet */
        SDL_CancelGPUCommandBuffer(cmd);
        vram_valid = 0;
        return 0;
    }
    {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;
        src.transfer_buffer = upload;
        at = 0;
#define UPLOAD(target, bytes, cycle)                                                                                   \
    if ((bytes) > 0) {                                                                                                 \
        src.offset = at;                                                                                               \
        dst.buffer = (target);                                                                                         \
        dst.offset = 0;                                                                                                \
        dst.size = (bytes);                                                                                            \
        SDL_UploadToGPUBuffer(copy, &src, &dst, (cycle));                                                              \
        at += (bytes);                                                                                                 \
    }
        UPLOAD(polys, poly_bytes, true)
        UPLOAD(verts, vert_bytes, true)
        UPLOAD(tiles, tile_bytes, true)
        UPLOAD(records, record_bytes, true)
        UPLOAD(sequences, sequence_bytes, true)
#undef UPLOAD
        for (int k = 0; k < nchanged; ++k) {
            /* the pages that changed, into the card's copy of the memory */
            src.offset = at + (uint32_t)k * KHDAYS_FRAME_VRAM_PAGE;
            dst.buffer = vram;
            dst.offset = (uint32_t)changed[k] * KHDAYS_FRAME_VRAM_PAGE;
            dst.size = KHDAYS_FRAME_VRAM_PAGE;
            SDL_UploadToGPUBuffer(copy, &src, &dst, false);
        }
        at += (uint32_t)nchanged * KHDAYS_FRAME_VRAM_PAGE;
        SDL_EndGPUCopyPass(copy);
    }
    {
        RasterParams rp;
        SDL_GPUStorageTextureReadWriteBinding out[2];
        SDL_GPUBuffer *in[6] = {polys, verts, tiles, vram, khdays_gpu_hd_buffer(), sequences};
        SDL_GPUComputePass *pass;
        SDL_zero(rp);
        rp.scale = (uint32_t)scale;
        rp.width = (uint32_t)w;
        rp.height = (uint32_t)h;
        rp.disp3dcnt = p->disp3dcnt;
        rp.swap_params = list->swap_params;
        rp.tiles_x = (uint32_t)tiles_x;
        rp.tiles_y = TILES_Y;
        rp.alpha_ref = io3d[0x40];
        rp.clear_color = io32(io3d, 0x50);
        rp.clear_depth = io32(io3d, 0x54);
        rp.smooth = (uint32_t)(texture_smoothing != 0) | (stretched ? 2u : 0u); /* the shader's pixel_art flags */
        rp.origin_x = origin_x;
        rp.fine = (uint32_t)fine;
        rp.npolys = (uint32_t)list->npolys;
        rp.nverts = (uint32_t)list->nverts;
        rp.entries = entries;
        {
            Recent *r = &recent[recent_next++ % SDL_arraysize(recent)];
            r->frame = p->frame;
            r->scale = scale;
            r->width = w;
            r->height = h;
            r->npolys = list->npolys;
            r->nverts = list->nverts;
            r->between = list_instead != NULL;
            r->entries = entries;
            r->busiest = busiest_tile;
        }
        for (int i = 0; i < 16; ++i) {
            rp.toon[i] = io32(io3d, 0x80 + 4u * (unsigned)i);
        }
        SDL_zeroa(out);
        out[0].texture = color3d;
        out[0].cycle = true;
        out[1].texture = attr3d;
        out[1].cycle = true;
        pass = SDL_BeginGPUComputePass(cmd, out, 2, NULL, 0);
        SDL_BindGPUComputePipeline(pass, raster);
        SDL_BindGPUComputeStorageBuffers(pass, 0, in, 6);
        SDL_PushGPUComputeUniformData(cmd, 0, &rp, sizeof(rp));
        SDL_DispatchGPUCompute(pass, (Uint32)(w + 7) / 8, (Uint32)(h + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
    }
    if (compose_too) {
        ComposeParams cp;
        SDL_GPUStorageTextureReadWriteBinding io[3];
        SDL_GPUComputePass *pass;
        const KhdaysGpu2dInfo *info = &p->info[0];
        SDL_zero(cp);
        cp.scale = (uint32_t)scale;
        cp.width = (uint32_t)w;
        cp.height = (uint32_t)h;
        cp.disp3dcnt = p->disp3dcnt;
        cp.swap_params = list->swap_params;
        cp.clear_color = io32(io3d, 0x50);
        cp.clear_depth = io32(io3d, 0x54);
        cp.fog_color = io32(io3d, 0x58);
        cp.fog_offset = io32(io3d, 0x5c);
        cp.eva = info->eva;
        cp.evb = info->evb;
        cp.evy = info->evy;
        cp.master_bright = info->master_bright;
        cp.bg0_hofs = info->bg0_hofs;
        cp.origin_x = origin_x;
        cp.backdrop = info->backdrop | (uint32_t)info->bldcnt << 16;
        cp.record_origin = record_columns > KHDAYS_FRAME_W ? origin_x : 0;
        cp.record_columns = (uint32_t)record_columns;
        for (int i = 0; i < 4; ++i) {
            cp.edge_colors[i] = io32(io3d, 0x30 + 4u * (unsigned)i);
        }
        for (int i = 0; i < 8; ++i) {
            cp.fog_table[i] = io32(io3d, 0x60 + 4u * (unsigned)i);
        }
        SDL_zeroa(io);
        io[0].texture = color3d; /* read only: the 3D pass's output, not cycled */
        io[1].texture = attr3d;
        io[2].texture = screen;
        io[2].cycle = true;
        pass = SDL_BeginGPUComputePass(cmd, io, 3, NULL, 0);
        SDL_BindGPUComputePipeline(pass, compose);
        SDL_BindGPUComputeStorageBuffers(pass, 0, &records, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &cp, sizeof(cp));
        SDL_DispatchGPUCompute(pass, (Uint32)(w + 7) / 8, (Uint32)(h + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
    }
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        vram_valid = 0;
        return 0;
    }
    return 1;
}

void khdays_raster3d_set_fine(int on)
{
    fine = on;
}

int khdays_raster3d_fine(void)
{
    return fine;
}

void khdays_raster3d_recent(char *out, size_t size)
{
    size_t at = 0;
    out[0] = '\0';
    for (unsigned i = 0; i < SDL_arraysize(recent); ++i) {
        const Recent *r = &recent[(recent_next + i) % SDL_arraysize(recent)];
        if (r->width == 0 || at >= size) {
            continue;
        }
        at += (size_t)SDL_snprintf(out + at, size - at,
                                   "  frame %u: %dx (%dx%d), %d polygons, %d vertices, %u tile entries, at most %u "
                                   "in a tile%s\n",
                                   r->frame, r->scale, r->width, r->height, r->npolys, r->nverts, r->entries,
                                   r->busiest, r->between ? ", a picture between two logic frames" : "");
    }
}

int khdays_raster3d_draw(const KhdaysFramePackage *p, int scale, int texture_smoothing)
{
    return draw(p, scale, texture_smoothing, 1);
}

int khdays_raster3d_rasterize(const KhdaysFramePackage *p, int scale, int texture_smoothing)
{
    return draw(p, scale, texture_smoothing, 0);
}

int khdays_raster3d_targets(SDL_GPUTexture **color, SDL_GPUTexture **attr, int *columns)
{
    if (color3d == NULL || attr3d == NULL) {
        return 0;
    }
    *color = color3d;
    *attr = attr3d;
    *columns = target_columns;
    return 1;
}

uint32_t *khdays_raster3d_read(int *width, int *height)
{
    const int w = target_columns * target_scale, h = KHDAYS_FRAME_H * target_scale;
    const Uint32 bytes = (Uint32)(w * h * 4);
    SDL_GPUTransferBufferCreateInfo info;
    SDL_GPUTransferBuffer *download;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUFence *fence;
    uint32_t *pixels = NULL;
    if (screen == NULL) {
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
        region.texture = screen;
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
