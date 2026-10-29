/* The port's 3D geometry (g3d.h): GBATEK's arithmetic, the choice marked where it leaves the rounding
 * open; the DS's polygon and vertex RAM no longer limits a list. It runs for every command the game
 * sends, so it is optimized even in Debug builds (whose /RTC checks cannot be combined with it). */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "g3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Not modelled: said once ------------------------------------------------ */

enum {
    MISSING_TEXGEN_NORMAL = 1u << 0,
    MISSING_TEXGEN_VERTEX = 1u << 1,
    MISSING_BEYOND_DS = 1u << 2,
    MISSING_THIRD_LIST = 1u << 3,
    MISSING_MEMORY = 1u << 4,
};

static void missing(unsigned what, const char *text)
{
    static unsigned said;
    if (!(said & what)) {
        said |= what;
        fprintf(stderr, "g3d: %s\n", text);
        fflush(stderr);
    }
}

/* --- Fixed point ------------------------------------------------------------ */

typedef struct Mtx {
    int32_t m[16]; /* row-major, 20.12; vectors are rows: v' = v * M */
} Mtx;

static const Mtx identity = {{0x1000, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0x1000}};

/* a * b */
static Mtx mul(const Mtx *a, const Mtx *b)
{
    Mtx r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            int64_t sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += (int64_t)a->m[i * 4 + k] * b->m[k * 4 + j];
            }
            r.m[i * 4 + j] = (int32_t)(sum >> 12);
        }
    }
    return r;
}

static int32_t sext(uint32_t v, int bits)
{
    return (int32_t)(v << (32 - bits)) >> (32 - bits);
}

/* 5-bit to 6-bit, as GBATEK gives it: X*2+1, zero stays zero. */
static int expand5(int c)
{
    return c ? c * 2 + 1 : 0;
}

/* --- State -------------------------------------------------------------------- */

typedef KhdaysG3dVertex Vertex;
typedef KhdaysG3dPolygon Polygon;
typedef KhdaysG3dList List;

/* The horizontal clip factor, in 1/4096: polygons are clipped against
 * x = +-widen * w instead of +-w (khdays_g3d_set_widen). */
#define WIDEN_ONE 4096

/* The lists: the one being built, those a SwapBuffers closed that no VBlank
 * has shown yet (the DS would hold a second one behind the first), and the
 * one shown. */
static List lists[4];
static List *building = &lists[0];
static List *closed[2];
static int nclosed;
static List *shown = &lists[1];

/* A vertex in clip space, before clipping. */
typedef struct Clip {
    int32_t x, y, z, w;
    int r, g, b;    /* 6 bits */
    int32_t s, t;   /* 12.4 */
    int32_t unused; /* a mod's vertex has its alpha here; the game's polygon has one */
} Clip;
_Static_assert(sizeof(Clip) == sizeof(KhdaysG3dPreVertex), "a vertex before clipping is ten words");
_Static_assert(sizeof(KhdaysG3dClipVertex) == sizeof(KhdaysG3dPreVertex), "a mod's vertex is ten words");

static struct {
    int mode;
    Mtx proj, pos, vec, tex;
    Mtx proj_stack[1], pos_stack[32], vec_stack[32], tex_stack[1];
    int proj_sp, pos_sp, tex_sp;
    int stack_error;
    Mtx clip;
    int clip_dirty;

    uint32_t attr_pending, attr; /* POLYGON_ATTR: set / latched at BEGIN_VTXS */
    uint32_t teximage, pltt;
    int color[3]; /* 5 bits each */
    int32_t raw_s, raw_t, s, t;
    int32_t vx, vy, vz; /* the last vertex (for the partial VTX forms) */
    int prim;           /* BEGIN_VTXS type, -1 outside a list */
    Clip strip[4];
    int strip_count, poly_index;

    int32_t light_vec[4][3], half_vec[4][3];
    int light_color[4][3];
    int diffuse[3], ambient[3], specular[3], emission[3];
    int shininess_on;
    uint8_t shininess[128];

    uint8_t viewport[4];       /* x1, y1, x2, y2 */
    int32_t widen, widen_next; /* the list being built's horizontal clip, the next list's */
    int hud;                   /* the polygons sent now are the HUD's */
    int marker;                /* selection art in the 3D layer, after the world's polygons */
    uintptr_t draw;            /* the model draw the polygons come from (khdays_g3d_draw_begin) */
    uint32_t occurrence, draw_ordinal;
    /* the polygons outside model draws, numbered per material (stream_key):
     * open addressing, keys + 1 (0 free), cleared with each list */
    uint32_t *stream_keys, *stream_counts;
    int32_t stream_capacity, stream_used;
    uintptr_t *drawn; /* the resources drawn in the list being built, one per draw */
    int32_t ndrawn, drawn_capacity;
    uint32_t swap_params; /* of the last SwapBuffers: the list being built uses them */

    int box_result;
    int32_t pos_result[4];
    int16_t vec_result[3];
    uint32_t irq_mode; /* GXSTAT bits 30-31 */
} g;

/* Parameter words per command; 0 for one that takes none. */
static const int8_t param_count[0x80] = {
    [0x10] = 1,  [0x11] = 0,  [0x12] = 1, [0x13] = 1, [0x14] = 1, [0x15] = 0,  [0x16] = 16, [0x17] = 12,
    [0x18] = 16, [0x19] = 12, [0x1a] = 9, [0x1b] = 3, [0x1c] = 3, [0x20] = 1,  [0x21] = 1,  [0x22] = 1,
    [0x23] = 2,  [0x24] = 1,  [0x25] = 1, [0x26] = 1, [0x27] = 1, [0x28] = 1,  [0x29] = 1,  [0x2a] = 1,
    [0x2b] = 1,  [0x30] = 1,  [0x31] = 1, [0x32] = 1, [0x33] = 1, [0x34] = 32, [0x40] = 1,  [0x41] = 0,
    [0x50] = 1,  [0x60] = 1,  [0x70] = 3, [0x71] = 2, [0x72] = 1,
};

static int valid_command(unsigned c)
{
    return (c >= 0x10 && c <= 0x1c) || (c >= 0x20 && c <= 0x2b) || (c >= 0x30 && c <= 0x34) || c == 0x40 || c == 0x41 ||
           c == 0x50 || c == 0x60 || (c >= 0x70 && c <= 0x72);
}

int khdays_g3d_param_count(unsigned command)
{
    return command < 0x80 && valid_command(command) ? param_count[command] : -1;
}

/* A command being gathered from the ports or from packed words. */
static unsigned cur_cmd;
static int cur_have;
static uint32_t cur_params[32];

/* GXFIFO's packed-command unpacking. */
static uint8_t fifo_cmds[4];
static int fifo_ncmds, fifo_index, fifo_left;

/* --- Lists: memory of their own ------------------------------------------------ */

int khdays_g3d_list_reserve(KhdaysG3dList *l, int32_t polys, int32_t verts)
{
    if (polys > l->poly_capacity) {
        int32_t n = l->poly_capacity > 0 ? l->poly_capacity : KHDAYS_G3D_DS_POLYS;
        Polygon *grown;
        while (n < polys)
            n *= 2;
        grown = (Polygon *)realloc(l->polys, sizeof(Polygon) * (size_t)n);
        if (grown == NULL)
            return 0;
        l->polys = grown;
        l->poly_capacity = n;
    }
    if (verts > l->vert_capacity) {
        int32_t n = l->vert_capacity > 0 ? l->vert_capacity : KHDAYS_G3D_DS_VERTS;
        Vertex *grown;
        while (n < verts)
            n *= 2;
        grown = (Vertex *)realloc(l->verts, sizeof(Vertex) * (size_t)n);
        if (grown == NULL)
            return 0;
        l->verts = grown;
        l->vert_capacity = n;
    }
    return 1;
}

/* Room in `l` for `polys` polygons and `verts` vertices before clipping. */
static int pre_reserve(KhdaysG3dList *l, int32_t polys, int32_t verts)
{
    if (polys > l->pre_capacity) {
        int32_t n = l->pre_capacity > 0 ? l->pre_capacity : KHDAYS_G3D_DS_POLYS;
        Polygon *grown;
        KhdaysG3dPreInfo *grown_info;
        while (n < polys)
            n *= 2;
        grown = (Polygon *)realloc(l->pre, sizeof(Polygon) * (size_t)n);
        if (grown == NULL)
            return 0;
        l->pre = grown;
        grown_info = (KhdaysG3dPreInfo *)realloc(l->pre_info, sizeof(KhdaysG3dPreInfo) * (size_t)n);
        if (grown_info == NULL)
            return 0;
        l->pre_info = grown_info;
        l->pre_capacity = n;
    }
    if (verts > l->pre_vert_capacity) {
        int32_t n = l->pre_vert_capacity > 0 ? l->pre_vert_capacity : KHDAYS_G3D_DS_VERTS;
        KhdaysG3dPreVertex *grown;
        while (n < verts)
            n *= 2;
        grown = (KhdaysG3dPreVertex *)realloc(l->pre_verts, sizeof(KhdaysG3dPreVertex) * (size_t)n);
        if (grown == NULL)
            return 0;
        l->pre_verts = grown;
        l->pre_vert_capacity = n;
    }
    return 1;
}

int khdays_g3d_list_copy(KhdaysG3dList *to, const KhdaysG3dList *from)
{
    if (!khdays_g3d_list_reserve(to, from->npolys, from->nverts) || !pre_reserve(to, from->npre, from->npre_verts)) {
        to->npolys = to->nverts = to->beyond_ds = 0;
        to->npre = to->npre_verts = 0;
        return 0;
    }
    to->flushes = from->flushes;
    memcpy(to->camera, from->camera, sizeof(to->camera));
    memcpy(to->projection, from->projection, sizeof(to->projection));
    to->npre = from->npre;
    to->npre_verts = from->npre_verts;
    if (from->npre > 0) {
        memcpy(to->pre, from->pre, sizeof(Polygon) * (size_t)from->npre);
        memcpy(to->pre_info, from->pre_info, sizeof(KhdaysG3dPreInfo) * (size_t)from->npre);
    }
    if (from->npre_verts > 0) {
        memcpy(to->pre_verts, from->pre_verts, sizeof(KhdaysG3dPreVertex) * (size_t)from->npre_verts);
    }
    to->npolys = from->npolys;
    to->nverts = from->nverts;
    to->swap_params = from->swap_params;
    to->widen = from->widen;
    to->beyond_ds = from->beyond_ds;
    if (from->npolys > 0) {
        memcpy(to->polys, from->polys, sizeof(Polygon) * (size_t)from->npolys);
    }
    if (from->nverts > 0) {
        memcpy(to->verts, from->verts, sizeof(Vertex) * (size_t)from->nverts);
    }
    return 1;
}

void khdays_g3d_list_free(KhdaysG3dList *l)
{
    free(l->polys);
    free(l->verts);
    free(l->pre);
    free(l->pre_info);
    free(l->pre_verts);
    memset(l, 0, sizeof(*l));
}

/* What the DS's polygon and vertex RAM would hold of the list being built. */
static int32_t ds_polys, ds_verts;

static void clear_list(List *l)
{
    l->flushes = 0;
    l->npolys = l->nverts = 0;
    l->npre = l->npre_verts = 0;
    l->beyond_ds = 0;
    l->swap_params = 0;
    l->widen = 1.0f;
}

void khdays_g3d_reset(void)
{
    uintptr_t *drawn = g.drawn;
    const int32_t drawn_capacity = g.drawn_capacity;
    memset(&g, 0, sizeof(g));
    g.drawn = drawn;
    g.drawn_capacity = drawn_capacity;
    g.proj = g.pos = g.vec = g.tex = identity;
    g.clip = identity;
    g.prim = -1;
    g.viewport[2] = 255;
    g.viewport[3] = 191;
    g.widen = g.widen_next = WIDEN_ONE;
    for (int i = 0; i < 4; ++i) {
        clear_list(&lists[i]);
    }
    building = &lists[0];
    shown = &lists[1];
    nclosed = 0;
    ds_polys = ds_verts = 0;
    cur_cmd = 0;
    cur_have = 0;
    fifo_ncmds = fifo_index = fifo_left = 0;
}

/* --- Matrices --------------------------------------------------------------- */

static Mtx *current(void)
{
    switch (g.mode) {
    case 0: return &g.proj;
    case 3: return &g.tex;
    default: return &g.pos;
    }
}

static void set_current(const Mtx *m)
{
    if (g.mode == 0) {
        g.proj = *m;
    } else if (g.mode == 3) {
        g.tex = *m;
    } else {
        g.pos = *m;
    }
    g.clip_dirty = 1;
}

/* C = M * C, on the current matrix (and in mode 2 the vector matrix too,
 * except for MTX_SCALE). */
static void multiply(const Mtx *m, int is_scale)
{
    Mtx r = mul(m, current());
    set_current(&r);
    if (g.mode == 2 && !is_scale) {
        g.vec = mul(m, &g.vec);
    }
}

static void load(const Mtx *m)
{
    set_current(m);
    if (g.mode == 2) {
        g.vec = *m;
    }
}

static const Mtx *clip_matrix(void)
{
    if (g.clip_dirty) {
        g.clip = mul(&g.pos, &g.proj);
        g.clip_dirty = 0;
    }
    return &g.clip;
}

static void stack_push(void)
{
    if (g.mode == 0) {
        if (g.proj_sp > 0)
            g.stack_error = 1;
        g.proj_stack[0] = g.proj;
        g.proj_sp = (g.proj_sp + 1) & 1;
    } else if (g.mode == 3) {
        g.tex_stack[0] = g.tex;
        g.tex_sp = (g.tex_sp + 1) & 1;
    } else {
        if (g.pos_sp >= 31)
            g.stack_error = 1;
        g.pos_stack[g.pos_sp & 31] = g.pos;
        g.vec_stack[g.pos_sp & 31] = g.vec;
        g.pos_sp = (g.pos_sp + 1) & 63;
    }
}

static void stack_pop(uint32_t param)
{
    if (g.mode == 0) {
        g.proj_sp = (g.proj_sp - 1) & 1;
        g.proj = g.proj_stack[0];
    } else if (g.mode == 3) {
        g.tex_sp = (g.tex_sp - 1) & 1;
        g.tex = g.tex_stack[0];
    } else {
        g.pos_sp = (g.pos_sp - sext(param, 6)) & 63;
        if (g.pos_sp >= 31)
            g.stack_error = 1;
        g.pos = g.pos_stack[g.pos_sp & 31];
        g.vec = g.vec_stack[g.pos_sp & 31];
    }
    g.clip_dirty = 1;
}

static void stack_store(uint32_t param)
{
    const int n = param & 31;
    if (g.mode == 0) {
        g.proj_stack[0] = g.proj;
    } else if (g.mode == 3) {
        g.tex_stack[0] = g.tex;
    } else {
        if (n == 31)
            g.stack_error = 1;
        g.pos_stack[n] = g.pos;
        g.vec_stack[n] = g.vec;
    }
}

static void stack_restore(uint32_t param)
{
    const int n = param & 31;
    if (g.mode == 0) {
        g.proj = g.proj_stack[0];
    } else if (g.mode == 3) {
        g.tex = g.tex_stack[0];
    } else {
        if (n == 31)
            g.stack_error = 1;
        g.pos = g.pos_stack[n];
        g.vec = g.vec_stack[n];
    }
    g.clip_dirty = 1;
}

/* --- Lighting --------------------------------------------------------------- */

/* A 10-bit vector (1 sign + 9 fractional bits each) as 12 fractional bits,
 * times the vector matrix's upper-left 3x3. */
static void direction(uint32_t packed, int32_t out[3])
{
    const int64_t v[3] = {(int64_t)sext(packed, 10) << 3, (int64_t)sext(packed >> 10, 10) << 3,
                          (int64_t)sext(packed >> 20, 10) << 3};
    for (int j = 0; j < 3; ++j) {
        out[j] = (int32_t)((v[0] * g.vec.m[0 * 4 + j] + v[1] * g.vec.m[1 * 4 + j] + v[2] * g.vec.m[2 * 4 + j]) >> 12);
    }
}

static int32_t dot(const int32_t a[3], const int32_t b[3])
{
    return (int32_t)(((int64_t)a[0] * b[0] + (int64_t)a[1] * b[1] + (int64_t)a[2] * b[2]) >> 12);
}

/* GBATEK's "Internal Operation on Normal Command"; the rounding of each step
 * is not documented and is chosen here. */
static void normal(uint32_t packed)
{
    int32_t n[3];
    int color[3];
    direction(packed, n);
    if ((g.teximage >> 30) == 2) {
        missing(MISSING_TEXGEN_NORMAL,
                "texture coordinates from normals (GBATEK's shift; not verified against hardware)");
        g.s = (int32_t)((((int64_t)sext(packed, 10) * g.tex.m[0] + (int64_t)sext(packed >> 10, 10) * g.tex.m[4] +
                          (int64_t)sext(packed >> 20, 10) * g.tex.m[8]) >>
                         17) +
                        g.raw_s);
        g.t = (int32_t)((((int64_t)sext(packed, 10) * g.tex.m[1] + (int64_t)sext(packed >> 10, 10) * g.tex.m[5] +
                          (int64_t)sext(packed >> 20, 10) * g.tex.m[9]) >>
                         17) +
                        g.raw_t);
    }
    for (int c = 0; c < 3; ++c) {
        color[c] = g.emission[c] << 12;
    }
    for (int i = 0; i < 4; ++i) {
        int32_t diffuse, shine;
        if (!(g.attr & (1u << i))) {
            continue;
        }
        diffuse = -dot(g.light_vec[i], n);
        if (diffuse < 0)
            diffuse = 0;
        shine = -dot(g.half_vec[i], n);
        if (shine < 0)
            shine = 0;
        shine = (int32_t)(((int64_t)shine * shine) >> 12);
        if (shine > 0x1000)
            shine = 0x1000;
        if (g.shininess_on) {
            shine = g.shininess[shine >> 5 > 127 ? 127 : shine >> 5] << 4;
        }
        for (int c = 0; c < 3; ++c) {
            const int lc = g.light_color[i][c];
            color[c] += (g.specular[c] * lc * shine) >> 5;
            color[c] += (g.diffuse[c] * lc * diffuse) >> 5;
            color[c] += (g.ambient[c] * lc) << 7; /* (a * lc / 32) with 12 fractional bits */
        }
    }
    for (int c = 0; c < 3; ++c) {
        int v = color[c] >> 12;
        g.color[c] = v > 31 ? 31 : v;
    }
}

/* --- Polygons --------------------------------------------------------------- */

/* The polygon `in` clipped against one plane of the clip volume: `axis`
 * (0 x, 1 y, 2 z) against +w (`sign` 1) or -w (-1), the x planes at `widen`
 * times w (WIDEN_ONE: the DS's). */
static int clip_plane(const Clip *in, int n, Clip *out, int axis, int sign, int32_t widen)
{
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const Clip *a = &in[i], *b = &in[(i + 1) % n];
        const int32_t *pa = &a->x, *pb = &b->x;
        const int64_t wa = axis == 0 ? ((int64_t)a->w * widen) >> 12 : a->w;
        const int64_t wb = axis == 0 ? ((int64_t)b->w * widen) >> 12 : b->w;
        const int64_t da = wa - (int64_t)sign * pa[axis]; /* >= 0 inside */
        const int64_t db = wb - (int64_t)sign * pb[axis];
        if (da >= 0) {
            out[m++] = *a;
        }
        if ((da >= 0) != (db >= 0)) {
            const double f = (double)da / (double)(da - db);
            Clip c;
            c.x = a->x + (int32_t)llround((b->x - (double)a->x) * f);
            c.y = a->y + (int32_t)llround((b->y - (double)a->y) * f);
            c.z = a->z + (int32_t)llround((b->z - (double)a->z) * f);
            c.w = a->w + (int32_t)llround((b->w - (double)a->w) * f);
            c.r = a->r + (int)lround((b->r - a->r) * f);
            c.g = a->g + (int)lround((b->g - a->g) * f);
            c.b = a->b + (int)lround((b->b - a->b) * f);
            c.s = a->s + (int32_t)llround((b->s - (double)a->s) * f);
            c.t = a->t + (int32_t)llround((b->t - (double)a->t) * f);
            c.unused = 0;
            out[m++] = c;
        }
    }
    return m;
}

static int translucent(uint32_t attr, uint32_t tex)
{
    const unsigned alpha = (attr >> 16) & 31;
    const unsigned format = (tex >> 26) & 7;
    return (alpha > 0 && alpha < 31) || format == 1 || format == 6;
}

static KhdaysG3dPolygonHook polygon_hook;

void khdays_g3d_set_polygon_hook(KhdaysG3dPolygonHook hook)
{
    polygon_hook = hook;
}

/* The polygon recorded before clipping in the list being built, with its
 * key; what it was drawn with returned (in `spare` when the list's memory is
 * short and it is not recorded, said once). */
/* A loose polygon's material: its texture, palette and attributes but its
 * alpha (a fade), and whether it is a mod's; never 0. */
static uint32_t stream_key(const Polygon *what)
{
    uint32_t h = what->tex * 2654435761u;
    h ^= (what->pltt + 0x9e3779b9u) * 0x85ebca6bu;
    h ^= (what->attr & ~(0x1fu << 16)) * 0xc2b2ae35u;
    h ^= (uint32_t)(what->flags & KHDAYS_G3D_MOD) * 0x27d4eb2fu;
    h ^= h >> 15;
    return h != 0 ? h : 1u;
}

/* The next place among the loose polygons of material `key` in the list being built. */
static uint32_t stream_ordinal(uint32_t key)
{
    if (g.stream_used * 2 >= g.stream_capacity) {
        const int32_t n = g.stream_capacity ? g.stream_capacity * 2 : 64;
        uint32_t *keys = (uint32_t *)calloc((size_t)n, sizeof(uint32_t));
        uint32_t *counts = (uint32_t *)calloc((size_t)n, sizeof(uint32_t));
        if (keys == NULL || counts == NULL) {
            free(keys);
            free(counts);
            return 0;
        }
        for (int32_t i = 0; i < g.stream_capacity; ++i) {
            if (g.stream_keys[i] == 0)
                continue;
            uint32_t at = g.stream_keys[i] & (uint32_t)(n - 1);
            while (keys[at] != 0)
                at = (at + 1) & (uint32_t)(n - 1);
            keys[at] = g.stream_keys[i];
            counts[at] = g.stream_counts[i];
        }
        free(g.stream_keys);
        free(g.stream_counts);
        g.stream_keys = keys;
        g.stream_counts = counts;
        g.stream_capacity = n;
    }
    uint32_t at = key & (uint32_t)(g.stream_capacity - 1);
    while (g.stream_keys[at] != 0 && g.stream_keys[at] != key)
        at = (at + 1) & (uint32_t)(g.stream_capacity - 1);
    if (g.stream_keys[at] == 0) {
        g.stream_keys[at] = key;
        g.stream_counts[at] = 0;
        ++g.stream_used;
    }
    return g.stream_counts[at]++;
}

static const KhdaysG3dPreInfo *record_pre(const void *vertices, int n, const Polygon *what, KhdaysG3dPreInfo *spare)
{
    Polygon *p;
    KhdaysG3dPreInfo *info;
    if (!pre_reserve(building, building->npre + 1, building->npre_verts + n)) {
        missing(MISSING_MEMORY, "no memory for a list's polygons (the ones that do not fit are left out)");
        memset(spare, 0, sizeof(*spare));
        memcpy(spare->viewport, g.viewport, 4);
        spare->widen = g.widen;
        return spare;
    }
    p = &building->pre[building->npre];
    info = &building->pre_info[building->npre];
    ++building->npre;
    *p = *what;
    p->first = building->npre_verts;
    p->count = n;
    info->draw = g.draw;
    if (g.draw != 0) {
        info->occurrence = g.occurrence;
        info->ordinal = g.draw_ordinal++;
    } else {
        /* outside model draws: its material for `occurrence`, its place among
         * that material's (an effect whose count changes moves only its own) */
        info->occurrence = stream_key(what);
        info->ordinal = stream_ordinal(info->occurrence);
    }
    memcpy(info->viewport, g.viewport, 4);
    info->widen = g.widen;
    memcpy(&building->pre_verts[building->npre_verts], vertices, sizeof(KhdaysG3dPreVertex) * (size_t)n);
    building->npre_verts += n;
    return info;
}

/* A game polygon `v` (n vertices, clip space) into `out`: culled, clipped
 * and through the viewport, with what `in` and `info` say it was drawn with;
 * `ds` the DS's polygon and vertex RAM so far for the list. `report`: the
 * game's own list (said once, the trace hook) -- not the host's pictures. */
static void finish_polygon(List *out, int32_t ds[2], const Clip *v, int n, const Polygon *in,
                           const KhdaysG3dPreInfo *info, int report)
{
    Clip a[10], b[10];
    int count = n;
    Polygon *p;
    double det;
    int front;
    const uint32_t attr = in->attr;
    /* facing: the winding of the first three vertices on screen (y up) */
    det = (double)v[0].x * ((double)v[1].y * v[2].w - (double)v[2].y * v[1].w) -
          (double)v[1].x * ((double)v[0].y * v[2].w - (double)v[2].y * v[0].w) +
          (double)v[2].x * ((double)v[0].y * v[1].w - (double)v[1].y * v[0].w);
    front = det > 0;
    if (det != 0 && ((front && !(attr & 0x80)) || (!front && !(attr & 0x40)))) {
        return;
    }
    if (!(attr & 0x1000)) { /* far-plane intersecting polygons hidden */
        for (int i = 0; i < n; ++i) {
            if (v[i].z > v[i].w) {
                return;
            }
        }
    }
    memcpy(a, v, sizeof(Clip) * n);
    for (int axis = 0; axis < 3 && count > 0; ++axis) {
        count = clip_plane(a, count, b, axis, 1, info->widen);
        count = clip_plane(b, count, a, axis, -1, info->widen);
    }
    if (count < 3 && !(count > 0 && n >= 3)) {
        return;
    }
    if (count == 0) {
        return;
    }
    if (!khdays_g3d_list_reserve(out, out->npolys + 1, out->nverts + count)) {
        if (report) {
            missing(MISSING_MEMORY, "no memory for a list's polygons (the ones that do not fit are left out)");
        }
        return;
    }
    p = &out->polys[out->npolys++];
    p->flags = in->flags & (KHDAYS_G3D_HUD | KHDAYS_G3D_OVER_2D | KHDAYS_G3D_MARKER);
    p->replacement = in->replacement;
    /* the DS keeps a polygon while its RAM has room for it, and drops the rest */
    if (ds[0] < KHDAYS_G3D_DS_POLYS && ds[1] + count <= KHDAYS_G3D_DS_VERTS) {
        ++ds[0];
        ds[1] += count;
    } else {
        p->flags |= KHDAYS_G3D_BEYOND_DS;
        ++out->beyond_ds;
        if (report) {
            missing(MISSING_BEYOND_DS, "a list holds more than the DS's polygon and vertex RAM: the port draws them "
                                       "all, the reference only what the DS would");
        }
    }
    p->attr = attr;
    p->tex = in->tex;
    p->pltt = in->pltt;
    p->first = out->nverts;
    p->count = count;
    p->translucent = translucent(attr, in->tex);
    p->ymin = 1e9f;
    p->ymax = -1e9f;
    {
        const float x1 = info->viewport[0], y1 = info->viewport[1];
        const float vw = (float)(info->viewport[2] - info->viewport[0] + 1);
        const float vh = (float)(info->viewport[3] - info->viewport[1] + 1);
        for (int i = 0; i < count; ++i) {
            Vertex *o = &out->verts[out->nverts++];
            const float w = a[i].w != 0 ? (float)a[i].w : 1.0f;
            o->x = ((float)a[i].x + w) * vw / (2 * w) + x1;
            o->y = 192.0f - (((float)a[i].y + w) * vh / (2 * w) + y1);
            o->z = (float)a[i].z / w;
            o->w = w;
            o->r = (float)a[i].r;
            o->g = (float)a[i].g;
            o->b = (float)a[i].b;
            o->s = (float)a[i].s;
            o->t = (float)a[i].t;
            o->a = 1.0f;
            if (o->y < p->ymin)
                p->ymin = o->y;
            if (o->y > p->ymax)
                p->ymax = o->y;
        }
    }
    if (report && polygon_hook != NULL) {
        polygon_hook(p, &out->verts[p->first]);
    }
}

static void emit_polygon(const Clip *v, int n)
{
    Polygon what;
    int32_t ds[2] = {ds_polys, ds_verts};
    memset(&what, 0, sizeof(what));
    what.attr = g.attr;
    what.tex = g.teximage;
    what.pltt = g.pltt;
    what.flags =
        (g.hud ? KHDAYS_G3D_HUD | (g.hud == 2 ? KHDAYS_G3D_OVER_2D : 0) : 0) | (g.marker ? KHDAYS_G3D_MARKER : 0);
    what.replacement = -1;
    KhdaysG3dPreInfo spare;
    finish_polygon(building, ds, v, n, &what, record_pre(v, n, &what, &spare), 1);
    ds_polys = ds[0];
    ds_verts = ds[1];
}

static void vertex(int32_t x, int32_t y, int32_t z)
{
    const Mtx *m = clip_matrix();
    Clip c;
    g.vx = x;
    g.vy = y;
    g.vz = z;
    if (g.prim < 0) {
        return; /* outside BEGIN_VTXS */
    }
    if ((g.teximage >> 30) == 3) {
        missing(MISSING_TEXGEN_VERTEX,
                "texture coordinates from vertices (GBATEK's shift; not verified against hardware)");
        g.s =
            (int32_t)((((int64_t)x * g.tex.m[0] + (int64_t)y * g.tex.m[4] + (int64_t)z * g.tex.m[8]) >> 20) + g.raw_s);
        g.t =
            (int32_t)((((int64_t)x * g.tex.m[1] + (int64_t)y * g.tex.m[5] + (int64_t)z * g.tex.m[9]) >> 20) + g.raw_t);
    }
    c.x = (int32_t)(((int64_t)x * m->m[0] + (int64_t)y * m->m[4] + (int64_t)z * m->m[8] + ((int64_t)m->m[12] << 12)) >>
                    12);
    c.y = (int32_t)(((int64_t)x * m->m[1] + (int64_t)y * m->m[5] + (int64_t)z * m->m[9] + ((int64_t)m->m[13] << 12)) >>
                    12);
    c.z = (int32_t)(((int64_t)x * m->m[2] + (int64_t)y * m->m[6] + (int64_t)z * m->m[10] + ((int64_t)m->m[14] << 12)) >>
                    12);
    c.w = (int32_t)(((int64_t)x * m->m[3] + (int64_t)y * m->m[7] + (int64_t)z * m->m[11] + ((int64_t)m->m[15] << 12)) >>
                    12);
    c.r = expand5(g.color[0]);
    c.g = expand5(g.color[1]);
    c.b = expand5(g.color[2]);
    c.s = (int16_t)g.s;
    c.t = (int16_t)g.t;
    c.unused = 0;

    g.strip[g.strip_count++] = c;
    switch (g.prim) {
    case 0: /* separate triangles */
        if (g.strip_count == 3) {
            emit_polygon(g.strip, 3);
            g.strip_count = 0;
        }
        break;
    case 1: /* separate quads */
        if (g.strip_count == 4) {
            emit_polygon(g.strip, 4);
            g.strip_count = 0;
        }
        break;
    case 2: /* triangle strip: every second triangle is wound the other way */
        if (g.strip_count == 3) {
            Clip t[3];
            if (g.poly_index & 1) {
                t[0] = g.strip[1];
                t[1] = g.strip[0];
            } else {
                t[0] = g.strip[0];
                t[1] = g.strip[1];
            }
            t[2] = g.strip[2];
            emit_polygon(t, 3);
            ++g.poly_index;
            g.strip[0] = g.strip[1];
            g.strip[1] = g.strip[2];
            g.strip_count = 2;
        }
        break;
    default: /* quad strip: v0 v1 v3 v2 */
        if (g.strip_count == 4) {
            Clip q[4] = {g.strip[0], g.strip[1], g.strip[3], g.strip[2]};
            emit_polygon(q, 4);
            g.strip[0] = g.strip[2];
            g.strip[1] = g.strip[3];
            g.strip_count = 2;
        }
        break;
    }
}

/* --- A mod's model ---------------------------------------------------------- */

void khdays_g3d_matrices(float position[16], float vector[16], float projection[16])
{
    for (int i = 0; i < 16; ++i) {
        position[i] = (float)g.pos.m[i] / 4096.0f;
        vector[i] = (float)g.vec.m[i] / 4096.0f;
        projection[i] = (float)g.proj.m[i] / 4096.0f;
    }
}

void khdays_g3d_material(KhdaysG3dMaterial *out)
{
    out->attr = g.attr_pending;
    out->tex = g.teximage;
    out->pltt = g.pltt;
    for (int c = 0; c < 3; ++c) {
        out->color[c] = g.color[c];
        out->diffuse[c] = g.diffuse[c];
        out->ambient[c] = g.ambient[c];
        out->specular[c] = g.specular[c];
        out->emission[c] = g.emission[c];
    }
}

/* normal()'s steps, in floats: the lights' vectors and colours, the
 * material's colours and the shininess table as they are now. */
void khdays_g3d_light(const KhdaysG3dMaterial *m, const float n[3], float rgb[3])
{
    float color[3];
    if ((m->attr & 0xf) == 0) {
        for (int c = 0; c < 3; ++c) {
            rgb[c] = (float)m->color[c] * 63.0f / 31.0f;
        }
        return;
    }
    for (int c = 0; c < 3; ++c) {
        color[c] = (float)m->emission[c];
    }
    for (int i = 0; i < 4; ++i) {
        float diffuse, shine;
        if (!(m->attr & (1u << i))) {
            continue;
        }
        diffuse =
            -((float)g.light_vec[i][0] * n[0] + (float)g.light_vec[i][1] * n[1] + (float)g.light_vec[i][2] * n[2]) /
            4096.0f;
        if (diffuse < 0)
            diffuse = 0;
        shine = -((float)g.half_vec[i][0] * n[0] + (float)g.half_vec[i][1] * n[1] + (float)g.half_vec[i][2] * n[2]) /
                4096.0f;
        if (shine < 0)
            shine = 0;
        shine *= shine;
        if (shine > 1)
            shine = 1;
        if (g.shininess_on) {
            const int k = (int)(shine * 4096.0f) >> 5;
            shine = (float)g.shininess[k > 127 ? 127 : k] / 256.0f;
        }
        for (int c = 0; c < 3; ++c) {
            const float lc = (float)g.light_color[i][c];
            color[c] +=
                ((float)m->specular[c] * shine + (float)m->diffuse[c] * diffuse + (float)m->ambient[c]) * lc / 32.0f;
        }
    }
    for (int c = 0; c < 3; ++c) {
        rgb[c] = (color[c] > 31.0f ? 31.0f : color[c]) * 63.0f / 31.0f;
    }
}

/* clip_plane in floats. */
static int clip_plane_f(const KhdaysG3dClipVertex *in, int n, KhdaysG3dClipVertex *out, int axis, int sign, float widen)
{
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const KhdaysG3dClipVertex *a = &in[i], *b = &in[(i + 1) % n];
        const float *pa = &a->x, *pb = &b->x;
        const float wa = axis == 0 ? a->w * widen : a->w;
        const float wb = axis == 0 ? b->w * widen : b->w;
        const float da = wa - (float)sign * pa[axis];
        const float db = wb - (float)sign * pb[axis];
        if (da >= 0) {
            out[m++] = *a;
        }
        if ((da >= 0) != (db >= 0)) {
            const float f = da / (da - db);
            KhdaysG3dClipVertex c;
            c.x = a->x + (b->x - a->x) * f;
            c.y = a->y + (b->y - a->y) * f;
            c.z = a->z + (b->z - a->z) * f;
            c.w = a->w + (b->w - a->w) * f;
            c.r = a->r + (b->r - a->r) * f;
            c.g = a->g + (b->g - a->g) * f;
            c.b = a->b + (b->b - a->b) * f;
            c.s = a->s + (b->s - a->s) * f;
            c.t = a->t + (b->t - a->t) * f;
            c.a = a->a + (b->a - a->a) * f;
            out[m++] = c;
        }
    }
    return m;
}

/* A mod's depths (z / w): a room's farthest, then the sky's band behind it, short of 1. Each sky
 * layer is a step nearer than the one before, as KH2 paints its skies in turn. */
#define MOD_FAR 0.9999f
#define MOD_SKY_END 0.99995f
#define MOD_SKY_LAYERS 32

static void finish_mod_polygon(List *out, const KhdaysG3dClipVertex *v, int n, const Polygon *in,
                               const KhdaysG3dPreInfo *info, int report)
{
    KhdaysG3dClipVertex a[10], b[10];
    int count = n;
    Polygon *p;
    const uint32_t attr = in->attr;
    const float widen = (float)info->widen / 4096.0f;
    const double det = (double)v[0].x * ((double)v[1].y * v[2].w - (double)v[2].y * v[1].w) -
                       (double)v[1].x * ((double)v[0].y * v[2].w - (double)v[2].y * v[0].w) +
                       (double)v[2].x * ((double)v[0].y * v[1].w - (double)v[1].y * v[0].w);
    const int front = det > 0;
    if (n < 3 || n > 4) {
        return;
    }
    if (det != 0 && ((front && !(attr & 0x80)) || (!front && !(attr & 0x40)))) {
        return;
    }
    /* clipped on every side but the far one: a big floor triangle reaching past it must not vanish */
    memcpy(a, v, sizeof(*v) * n);
    for (int axis = 0; axis < 3 && count > 0; ++axis) {
        /* Sequence HUD bounds can extend beyond the DS view before one-screen placement. */
        if (axis < 2 &&
            (in->flags & (KHDAYS_G3D_SEQUENCE | KHDAYS_G3D_HUD)) == (KHDAYS_G3D_SEQUENCE | KHDAYS_G3D_HUD)) {
            continue;
        }
        if (axis == 2) {
            memcpy(b, a, sizeof(*a) * (size_t)count);
        } else {
            count = clip_plane_f(a, count, b, axis, 1, widen);
        }
        count = clip_plane_f(b, count, a, axis, -1, widen);
    }
    if (count < 3) {
        return;
    }
    /* the depth, only after clipping with the true one: past the far plane, at it; a sky, its layer */
    {
        const int layer =
            KHDAYS_G3D_SKY_LAYER(in->flags) < MOD_SKY_LAYERS ? KHDAYS_G3D_SKY_LAYER(in->flags) : MOD_SKY_LAYERS;
        const float sky = MOD_SKY_END - (float)(layer - 1) * ((MOD_SKY_END - MOD_FAR) / (float)MOD_SKY_LAYERS);
        for (int i = 0; i < count; ++i) {
            if (layer > 0) {
                a[i].z = a[i].w * sky;
            } else if (a[i].z > a[i].w * MOD_FAR) {
                a[i].z = a[i].w * MOD_FAR;
            }
        }
    }
    if (!khdays_g3d_list_reserve(out, out->npolys + 1, out->nverts + count)) {
        if (report) {
            missing(MISSING_MEMORY, "no memory for a list's polygons (the ones that do not fit are left out)");
        }
        return;
    }
    p = &out->polys[out->npolys++];
    p->flags = in->flags;
    p->replacement = in->replacement;
    p->attr = attr;
    p->tex = in->tex;
    p->pltt = in->pltt;
    p->first = out->nverts;
    p->count = count;
    p->translucent = in->translucent;
    p->ymin = 1e9f;
    p->ymax = -1e9f;
    {
        const float x1 = info->viewport[0], y1 = info->viewport[1];
        const float vw = (float)(info->viewport[2] - info->viewport[0] + 1);
        const float vh = (float)(info->viewport[3] - info->viewport[1] + 1);
        for (int i = 0; i < count; ++i) {
            Vertex *o = &out->verts[out->nverts++];
            const float w = a[i].w != 0 ? a[i].w : 1.0f / 4096.0f;
            o->x = (a[i].x + w) * vw / (2 * w) + x1;
            o->y = 192.0f - ((a[i].y + w) * vh / (2 * w) + y1);
            o->z = a[i].z / w;
            o->w = w * 4096.0f;
            o->r = a[i].r;
            o->g = a[i].g;
            o->b = a[i].b;
            o->s = a[i].s;
            o->t = a[i].t;
            o->a = a[i].a;
            if (o->y < p->ymin)
                p->ymin = o->y;
            if (o->y > p->ymax)
                p->ymax = o->y;
        }
    }
    if (report && polygon_hook != NULL) {
        polygon_hook(p, &out->verts[p->first]);
    }
}

static void mod_polygon(const KhdaysG3dClipVertex *v, int n, uint32_t attr, uint32_t tex, uint32_t pltt,
                        int32_t replacement, int translucent_, int sky, int flags)
{
    Polygon what;
    if (n < 3 || n > 4) {
        return;
    }
    memset(&what, 0, sizeof(what));
    what.attr = attr;
    what.tex = replacement >= 0 ? tex : 0;
    what.pltt = pltt;
    what.flags = KHDAYS_G3D_MOD | flags | (g.marker ? KHDAYS_G3D_MARKER : 0) |
                 (sky > 0 ? KHDAYS_G3D_SKY | (((sky < 255 ? sky : 255) & 0xff) << 8) : 0) |
                 (g.hud ? KHDAYS_G3D_HUD | (g.hud == 2 ? KHDAYS_G3D_OVER_2D : 0) : 0);
    what.replacement = replacement;
    what.translucent = translucent_ || translucent(attr, 0);
    KhdaysG3dPreInfo spare;
    finish_mod_polygon(building, v, n, &what, record_pre(v, n, &what, &spare), 1);
}

void khdays_g3d_mod_polygon(const KhdaysG3dClipVertex *v, int n, uint32_t attr, uint32_t tex, int32_t replacement,
                            int translucent_, int sky)
{
    mod_polygon(v, n, attr, tex, 0, replacement, translucent_, sky, 0);
}

void khdays_g3d_sequence_polygon(const KhdaysG3dClipVertex *v, int n, uint32_t attr, int32_t atlas, uint32_t program)
{
    mod_polygon(v, n, attr, 7u << 26, program, atlas, 1, 0, KHDAYS_G3D_SEQUENCE);
}

/* --- Tests ------------------------------------------------------------------ */

static void box_test(const uint32_t *p)
{
    const int32_t x = sext(p[0], 16), y = sext(p[0] >> 16, 16), z = sext(p[1], 16);
    const int32_t w = sext(p[1] >> 16, 16), h = sext(p[2], 16), d = sext(p[2] >> 16, 16);
    const Mtx *m = clip_matrix();
    Clip corner[8];
    static const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (int i = 0; i < 8; ++i) {
        const int64_t cx = x + ((i & 1) ? w : 0), cy = y + ((i & 2) ? h : 0), cz = z + ((i & 4) ? d : 0);
        memset(&corner[i], 0, sizeof(Clip));
        corner[i].x = (int32_t)((cx * m->m[0] + cy * m->m[4] + cz * m->m[8] + ((int64_t)m->m[12] << 12)) >> 12);
        corner[i].y = (int32_t)((cx * m->m[1] + cy * m->m[5] + cz * m->m[9] + ((int64_t)m->m[13] << 12)) >> 12);
        corner[i].z = (int32_t)((cx * m->m[2] + cy * m->m[6] + cz * m->m[10] + ((int64_t)m->m[14] << 12)) >> 12);
        corner[i].w = (int32_t)((cx * m->m[3] + cy * m->m[7] + cz * m->m[11] + ((int64_t)m->m[15] << 12)) >> 12);
    }
    g.box_result = 0;
    for (int f = 0; f < 6 && !g.box_result; ++f) {
        Clip a[10], b[10];
        int count = 4;
        for (int i = 0; i < 4; ++i) {
            a[i] = corner[faces[f][i]];
        }
        for (int axis = 0; axis < 3 && count > 0; ++axis) {
            /* the DS's volume, widened or not: the game reads the result */
            count = clip_plane(a, count, b, axis, 1, WIDEN_ONE);
            count = clip_plane(b, count, a, axis, -1, WIDEN_ONE);
        }
        g.box_result = count > 0;
    }
}

/* --- The lists ------------------------------------------------------------------ */

static int compare_translucent(const void *a, const void *b)
{
    const Polygon *pa = (const Polygon *)a, *pb = (const Polygon *)b;
    if (pa->ymin != pb->ymin)
        return pa->ymin < pb->ymin ? -1 : 1;
    if (pa->ymax != pb->ymax)
        return pa->ymax < pb->ymax ? -1 : 1;
    return pa->first < pb->first ? -1 : pa->first > pb->first; /* as submitted */
}

/* Opaque, then translucent (Y-sorted unless manual), then selection markers in submission order.
 * Markers remain inside BG0's 3D layer. Vertices stay where they are. */
static void into_drawing_order_with(List *l, Polygon **order_, int32_t *order_capacity, int report)
{
    Polygon *order;
    int n = 0, first_translucent;
    if (l->npolys == 0) {
        return;
    }
    if (l->npolys > *order_capacity) {
        Polygon *grown = (Polygon *)realloc(*order_, sizeof(Polygon) * (size_t)l->npolys);
        if (grown == NULL) {
            if (report) {
                missing(MISSING_MEMORY, "no memory for a list's polygons (the ones that do not fit are left out)");
            }
            return; /* left in the order they came */
        }
        *order_ = grown;
        *order_capacity = l->npolys;
    }
    order = *order_;
    for (int i = 0; i < l->npolys && n < l->npolys; ++i) {
        if (!l->polys[i].translucent && !(l->polys[i].flags & KHDAYS_G3D_MARKER))
            order[n++] = l->polys[i];
    }
    first_translucent = n;
    for (int i = 0; i < l->npolys && n < l->npolys; ++i) {
        if (l->polys[i].translucent && !(l->polys[i].flags & KHDAYS_G3D_MARKER))
            order[n++] = l->polys[i];
    }
    if (!(l->swap_params & 1)) { /* auto-sort by Y */
        qsort(order + first_translucent, (size_t)(n - first_translucent), sizeof(order[0]), compare_translucent);
    }
    for (int i = 0; i < l->npolys && n < l->npolys; ++i) {
        if (l->polys[i].flags & KHDAYS_G3D_MARKER)
            order[n++] = l->polys[i];
    }
    memcpy(l->polys, order, sizeof(Polygon) * (size_t)n);
}

static void into_drawing_order(List *l)
{
    static Polygon *order;
    static int32_t order_capacity;
    into_drawing_order_with(l, &order, &order_capacity, 1);
}

int khdays_g3d_finish(const KhdaysG3dList *from, const KhdaysG3dPreVertex *vertices, KhdaysG3dList *out)
{
    const KhdaysG3dPreVertex *source = vertices != NULL ? vertices : from->pre_verts;
    int32_t ds[2] = {0, 0};
    Polygon *order = NULL;
    int32_t order_capacity = 0;
    out->npolys = out->nverts = out->beyond_ds = 0;
    out->npre = out->npre_verts = 0;
    out->swap_params = from->swap_params;
    out->widen = from->widen;
    for (int32_t i = 0; i < from->npre; ++i) {
        const Polygon *in = &from->pre[i];
        if (in->flags & KHDAYS_G3D_MOD) {
            KhdaysG3dClipVertex v[4];
            memcpy(v, &source[in->first], sizeof(KhdaysG3dPreVertex) * (size_t)in->count);
            finish_mod_polygon(out, v, in->count, in, &from->pre_info[i], 0);
        } else {
            Clip v[4];
            memcpy(v, &source[in->first], sizeof(KhdaysG3dPreVertex) * (size_t)in->count);
            finish_polygon(out, ds, v, in->count, in, &from->pre_info[i], 0);
        }
    }
    into_drawing_order_with(out, &order, &order_capacity, 0);
    free(order);
    return out->npolys == 0 || out->polys != NULL;
}

/* --- The model draws' keys ------------------------------------------------------- */

void khdays_g3d_draw_begin(const void *resource)
{
    const uintptr_t r = (uintptr_t)resource;
    uint32_t occurrence = 0;
    for (int32_t i = 0; i < g.ndrawn; ++i) {
        occurrence += g.drawn[i] == r;
    }
    if (g.ndrawn == g.drawn_capacity) {
        const int32_t n = g.drawn_capacity ? g.drawn_capacity * 2 : 64;
        uintptr_t *grown = (uintptr_t *)realloc(g.drawn, sizeof(uintptr_t) * (size_t)n);
        if (grown != NULL) {
            g.drawn = grown;
            g.drawn_capacity = n;
        }
    }
    if (g.ndrawn < g.drawn_capacity) {
        g.drawn[g.ndrawn++] = r;
    }
    g.draw = r;
    g.occurrence = occurrence;
    g.draw_ordinal = 0;
}

void khdays_g3d_draw_end(void)
{
    g.draw = 0;
}

void khdays_g3d_frame_camera(const int32_t projection[16], const int32_t camera[12])
{
    if (building->flushes++ == 0) {
        memcpy(building->camera, camera, sizeof(building->camera));
        memcpy(building->projection, projection, sizeof(building->projection));
    }
}

static List *free_list(void)
{
    for (int i = 0; i < 4; ++i) {
        List *l = &lists[i];
        if (l != building && l != shown && (nclosed < 1 || l != closed[0]) && (nclosed < 2 || l != closed[1])) {
            return l;
        }
    }
    return NULL; /* cannot happen: four lists, at most three in use besides */
}

/* SwapBuffers: the list being built is closed, with the parameters of the
 * SwapBuffers before (they apply to the list after them), and the next one
 * starts. The DS holds whatever follows a SwapBuffers until the VBlank; here
 * it goes into the next list at once, which comes to the same picture. */
static uint32_t swaps;

static void swap_buffers(uint32_t params)
{
    List *l = building;
    ++swaps;
    l->swap_params = g.swap_params;
    l->widen = (float)g.widen / WIDEN_ONE;
    into_drawing_order(l);
    if (nclosed == 2) {
        missing(MISSING_THIRD_LIST, "a third SwapBuffers before a VBlank showed the first: the oldest is dropped");
        closed[0] = closed[1];
        nclosed = 1;
    }
    closed[nclosed++] = l;
    building = free_list();
    clear_list(building);
    ds_polys = ds_verts = 0;
    g.ndrawn = 0;
    if (g.stream_capacity > 0) {
        memset(g.stream_keys, 0, sizeof(uint32_t) * (size_t)g.stream_capacity);
        g.stream_used = 0;
    }
    g.swap_params = params & 3;
    g.widen = g.widen_next;
}

int khdays_g3d_swap_pending(void)
{
    return nclosed > 0;
}

uint32_t khdays_g3d_swap_count(void)
{
    return swaps;
}

int khdays_g3d_build_slot(void)
{
    return (int)(building - lists);
}

int khdays_g3d_vblank(void)
{
    if (nclosed == 0) {
        return 0;
    }
    shown = closed[0];
    closed[0] = closed[1];
    --nclosed;
    return 1;
}

const KhdaysG3dList *khdays_g3d_shown(void)
{
    return shown;
}

void khdays_g3d_set_hud(int on)
{
    g.hud = on;
}

void khdays_g3d_set_marker(int on)
{
    g.marker = on != 0;
}

void khdays_g3d_set_widen(float k)
{
    g.widen_next = (int32_t)lroundf(k * WIDEN_ONE);
}

/* --- Commands ----------------------------------------------------------------- */

void khdays_g3d_command(unsigned cmd, const uint32_t *p)
{
    Mtx m;
    switch (cmd) {
    case 0x10: g.mode = p[0] & 3; break;
    case 0x11: stack_push(); break;
    case 0x12: stack_pop(p[0]); break;
    case 0x13: stack_store(p[0]); break;
    case 0x14: stack_restore(p[0]); break;
    case 0x15: load(&identity); break;
    case 0x16:
        for (int i = 0; i < 16; ++i)
            m.m[i] = (int32_t)p[i];
        load(&m);
        break;
    case 0x17:
    case 0x19:
    case 0x1a:
        m = identity;
        for (int r = 0; r < (cmd == 0x1a ? 3 : 4); ++r) {
            for (int c = 0; c < 3; ++c) {
                m.m[r * 4 + c] = (int32_t)p[r * 3 + c];
            }
        }
        if (cmd == 0x17)
            load(&m);
        else
            multiply(&m, 0);
        break;
    case 0x18:
        for (int i = 0; i < 16; ++i)
            m.m[i] = (int32_t)p[i];
        multiply(&m, 0);
        break;
    case 0x1b:
        m = identity;
        m.m[0] = (int32_t)p[0];
        m.m[5] = (int32_t)p[1];
        m.m[10] = (int32_t)p[2];
        multiply(&m, 1);
        break;
    case 0x1c:
        m = identity;
        m.m[12] = (int32_t)p[0];
        m.m[13] = (int32_t)p[1];
        m.m[14] = (int32_t)p[2];
        multiply(&m, 0);
        break;
    case 0x20:
        g.color[0] = p[0] & 31;
        g.color[1] = (p[0] >> 5) & 31;
        g.color[2] = (p[0] >> 10) & 31;
        break;
    case 0x21: normal(p[0]); break;
    case 0x22:
        g.raw_s = sext(p[0], 16);
        g.raw_t = sext(p[0] >> 16, 16);
        if ((g.teximage >> 30) == 1) {
            g.s =
                (int32_t)(((int64_t)g.raw_s * g.tex.m[0] + (int64_t)g.raw_t * g.tex.m[4] + g.tex.m[8] + g.tex.m[12]) >>
                          12);
            g.t =
                (int32_t)(((int64_t)g.raw_s * g.tex.m[1] + (int64_t)g.raw_t * g.tex.m[5] + g.tex.m[9] + g.tex.m[13]) >>
                          12);
        } else {
            g.s = g.raw_s;
            g.t = g.raw_t;
        }
        break;
    case 0x23: vertex(sext(p[0], 16), sext(p[0] >> 16, 16), sext(p[1], 16)); break;
    case 0x24: vertex(sext(p[0], 10) << 6, sext(p[0] >> 10, 10) << 6, sext(p[0] >> 20, 10) << 6); break;
    case 0x25: vertex(sext(p[0], 16), sext(p[0] >> 16, 16), g.vz); break;
    case 0x26: vertex(sext(p[0], 16), g.vy, sext(p[0] >> 16, 16)); break;
    case 0x27: vertex(g.vx, sext(p[0], 16), sext(p[0] >> 16, 16)); break;
    case 0x28:
        vertex((int16_t)(g.vx + sext(p[0], 10)), (int16_t)(g.vy + sext(p[0] >> 10, 10)),
               (int16_t)(g.vz + sext(p[0] >> 20, 10)));
        break;
    case 0x29: g.attr_pending = p[0]; break;
    case 0x2a: g.teximage = p[0]; break;
    case 0x2b: g.pltt = p[0] & 0x1fff; break;
    case 0x30:
        g.diffuse[0] = p[0] & 31;
        g.diffuse[1] = (p[0] >> 5) & 31;
        g.diffuse[2] = (p[0] >> 10) & 31;
        g.ambient[0] = (p[0] >> 16) & 31;
        g.ambient[1] = (p[0] >> 21) & 31;
        g.ambient[2] = (p[0] >> 26) & 31;
        if (p[0] & 0x8000) {
            memcpy(g.color, g.diffuse, sizeof(g.color));
        }
        break;
    case 0x31:
        g.specular[0] = p[0] & 31;
        g.specular[1] = (p[0] >> 5) & 31;
        g.specular[2] = (p[0] >> 10) & 31;
        g.shininess_on = (p[0] >> 15) & 1;
        g.emission[0] = (p[0] >> 16) & 31;
        g.emission[1] = (p[0] >> 21) & 31;
        g.emission[2] = (p[0] >> 26) & 31;
        break;
    case 0x32: {
        const int i = p[0] >> 30;
        direction(p[0], g.light_vec[i]);
        g.half_vec[i][0] = g.light_vec[i][0] / 2;
        g.half_vec[i][1] = g.light_vec[i][1] / 2;
        g.half_vec[i][2] = (g.light_vec[i][2] - 0x1000) / 2;
        break;
    }
    case 0x33: {
        const int i = p[0] >> 30;
        g.light_color[i][0] = p[0] & 31;
        g.light_color[i][1] = (p[0] >> 5) & 31;
        g.light_color[i][2] = (p[0] >> 10) & 31;
        break;
    }
    case 0x34:
        for (int i = 0; i < 32; ++i) {
            for (int b = 0; b < 4; ++b) {
                g.shininess[i * 4 + b] = (uint8_t)(p[i] >> (8 * b));
            }
        }
        break;
    case 0x40:
        g.attr = g.attr_pending;
        g.prim = p[0] & 3;
        g.strip_count = 0;
        g.poly_index = 0;
        break;
    case 0x41: break; /* END_VTXS does nothing on the hardware */
    case 0x50: swap_buffers(p[0]); break;
    case 0x60:
        g.viewport[0] = (uint8_t)p[0];
        g.viewport[1] = (uint8_t)(p[0] >> 8);
        g.viewport[2] = (uint8_t)(p[0] >> 16);
        g.viewport[3] = (uint8_t)(p[0] >> 24);
        break;
    case 0x70: box_test(p); break;
    case 0x71: {
        const Mtx *c = clip_matrix();
        const int64_t x = sext(p[0], 16), y = sext(p[0] >> 16, 16), z = sext(p[1], 16);
        g.vx = (int32_t)x;
        g.vy = (int32_t)y;
        g.vz = (int32_t)z;
        for (int j = 0; j < 4; ++j) {
            g.pos_result[j] =
                (int32_t)((x * c->m[j] + y * c->m[4 + j] + z * c->m[8 + j] + ((int64_t)c->m[12 + j] << 12)) >> 12);
        }
        break;
    }
    case 0x72: {
        int32_t v[3];
        direction(p[0], v);
        for (int j = 0; j < 3; ++j) {
            g.vec_result[j] = (int16_t)sext((uint32_t)v[j], 13); /* 4-bit sign, 12 fractional */
        }
        break;
    }
    default: break;
    }
}

/* One parameter word for the command `cmd` (or the command itself, for one
 * that takes none). */
static void entry(unsigned cmd, uint32_t param)
{
    if (!valid_command(cmd)) {
        return; /* invalid commands fetch no parameters (GBATEK) */
    }
    if (param_count[cmd] == 0) {
        khdays_g3d_command(cmd, NULL);
        return;
    }
    if (cur_have == 0) {
        cur_cmd = cmd;
    }
    cur_params[cur_have++] = param;
    if (cur_have == param_count[cur_cmd]) {
        cur_have = 0;
        khdays_g3d_command(cur_cmd, cur_params);
    }
}

/* Moves the packed-command state past commands that take no parameters. */
static void fifo_advance(void)
{
    while (fifo_index < fifo_ncmds) {
        const unsigned cmd = fifo_cmds[fifo_index];
        const int n = valid_command(cmd) ? param_count[cmd] : 0;
        if (n > 0) {
            fifo_left = n;
            return;
        }
        if (cmd != 0) {
            entry(cmd, 0);
        }
        ++fifo_index;
    }
    fifo_ncmds = 0;
}

static void fifo_word(uint32_t value)
{
    if (fifo_ncmds == 0) {
        for (int i = 0; i < 4; ++i) {
            fifo_cmds[i] = (uint8_t)(value >> (8 * i));
        }
        fifo_ncmds = 4;
        while (fifo_ncmds > 1 && fifo_cmds[fifo_ncmds - 1] == 0) {
            --fifo_ncmds;
        }
        fifo_index = 0;
        fifo_advance();
        return;
    }
    entry(fifo_cmds[fifo_index], value);
    if (--fifo_left == 0) {
        ++fifo_index;
        fifo_advance();
    }
}

void khdays_g3d_words(const uint32_t *words, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        fifo_word(words[i]);
    }
}

void khdays_g3d_port(uint32_t offset, uint32_t value)
{
    entry((offset - 0x400) / 4, value);
}

/* --- What the game reads back ----------------------------------------------------- */

void khdays_g3d_status_written(uint32_t value)
{
    if (value & 0x8000) {
        g.stack_error = 0;
        g.proj_sp = 0;
        g.tex_sp = 0;
    }
    g.irq_mode = value & 0xc0000000u;
}

uint32_t khdays_g3d_read(uint32_t offset)
{
    if (offset == 0x600) {
        /* GXSTAT: no test or matrix stack busy (bits 0, 14), the box test's
         * result (1), the stack levels (8-12, 13) and error (15), the FIFO
         * empty (16-24 its level, 25 less than half full, 26 empty), the
         * geometry not busy (27), the IRQ mode (30-31) */
        return (uint32_t)g.box_result << 1 | (uint32_t)(g.pos_sp & 31) << 8 | (uint32_t)(g.proj_sp & 1) << 13 |
               (uint32_t)g.stack_error << 15 | 1u << 25 | 1u << 26 | g.irq_mode;
    }
    if (offset == 0x604) {
        return (uint32_t)ds_polys | (uint32_t)ds_verts << 16;
    }
    if (offset >= 0x620 && offset < 0x630) {
        return (uint32_t)g.pos_result[(offset - 0x620) / 4];
    }
    if (offset >= 0x630 && offset < 0x636) {
        const int i = (offset - 0x630) / 2;
        return (uint16_t)g.vec_result[i] | (i < 2 ? (uint32_t)(uint16_t)g.vec_result[i + 1] << 16 : 0);
    }
    if (offset >= 0x640 && offset < 0x680) {
        return (uint32_t)clip_matrix()->m[(offset - 0x640) / 4];
    }
    if (offset >= 0x680 && offset < 0x6a4) {
        const int i = (offset - 0x680) / 4; /* 3x3: rows of the 4x4 */
        return (uint32_t)g.vec.m[(i / 3) * 4 + i % 3];
    }
    return 0;
}

int khdays_g3d_fifo_irq(void)
{
    const uint32_t mode = g.irq_mode >> 30;
    return mode == 1 || mode == 2; /* less than half full; empty */
}
