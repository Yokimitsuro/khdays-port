/* Runs every frame: optimized even in Debug builds, whose /RTC checks
 * cannot be combined with optimization. */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "gpu3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Not modelled: said once ------------------------------------------------ */

enum {
    MISSING_ANTIALIAS = 1u << 0,
    MISSING_TEXGEN_NORMAL = 1u << 1,
    MISSING_TEXGEN_VERTEX = 1u << 2,
    MISSING_1DOT = 1u << 3,
    MISSING_OVERFLOW = 1u << 4,
};

static void missing(unsigned what, const char *text)
{
    static unsigned said;
    if (!(said & what)) {
        said |= what;
        fprintf(stderr, "gpu3d: %s\n", text);
        fflush(stderr);
    }
}

/* --- Fixed point ------------------------------------------------------------ */

typedef struct Mtx {
    int32_t m[16];  /* row-major, 20.12; vectors are rows: v' = v * M */
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

/* --- Geometry state --------------------------------------------------------- */

#define MAX_POLYS 2048
#define MAX_VERTS 6144

typedef struct Vertex {
    float x, y;       /* screen, pixels (y down) */
    float z;          /* z / w, -1..1 */
    float w;          /* clip w, 12 fractional bits as a float */
    float r, g, b;    /* 6 bits each */
    float s, t;       /* texels, 4 fractional bits as a float */
} Vertex;

typedef struct Polygon {
    uint32_t attr, tex, pltt;
    int first, count;  /* in the vertex list */
    float ymin, ymax;
    int translucent;
} Polygon;

typedef struct List {
    Polygon polys[MAX_POLYS];
    Vertex verts[MAX_VERTS];
    int npolys, nverts;
    uint32_t swap_params;  /* the SwapBuffers bits it is drawn with */
} List;

static List lists[2];
static List *geometry = &lists[0];
static List *render = &lists[1];

/* A vertex in clip space, before clipping. */
typedef struct Clip {
    int32_t x, y, z, w;
    int r, g, b;      /* 6 bits */
    int32_t s, t;     /* 12.4 */
} Clip;

static struct {
    int mode;
    Mtx proj, pos, vec, tex;
    Mtx proj_stack[1], pos_stack[32], vec_stack[32], tex_stack[1];
    int proj_sp, pos_sp, tex_sp;
    int stack_error;
    Mtx clip;
    int clip_dirty;

    uint32_t attr_pending, attr;  /* POLYGON_ATTR: set / latched at BEGIN_VTXS */
    uint32_t teximage, pltt;
    int color[3];                 /* 5 bits each */
    int32_t raw_s, raw_t, s, t;
    int32_t vx, vy, vz;           /* the last vertex (for the partial VTX forms) */
    int prim;                     /* BEGIN_VTXS type, -1 outside a list */
    Clip strip[4];
    int strip_count, poly_index;

    int32_t light_vec[4][3], half_vec[4][3];
    int light_color[4][3];
    int diffuse[3], ambient[3], specular[3], emission[3];
    int shininess_on;
    uint8_t shininess[128];

    uint8_t viewport[4];          /* x1, y1, x2, y2 */
    uint32_t swap_params;         /* of the last SwapBuffers: the list being built uses them */
    int swap_pending;
    uint32_t swap_next_params;

    int box_result;
    int32_t pos_result[4];
    int16_t vec_result[3];
    uint32_t gxstat_irq;          /* GXSTAT bits 30-31 */
} g;

/* The command decoder: parameters gathered for the command at the front. */
static const int8_t param_count[0x80] = {
    [0x10] = 1, [0x11] = 0, [0x12] = 1, [0x13] = 1, [0x14] = 1, [0x15] = 0, [0x16] = 16,
    [0x17] = 12, [0x18] = 16, [0x19] = 12, [0x1a] = 9, [0x1b] = 3, [0x1c] = 3,
    [0x20] = 1, [0x21] = 1, [0x22] = 1, [0x23] = 2, [0x24] = 1, [0x25] = 1, [0x26] = 1,
    [0x27] = 1, [0x28] = 1, [0x29] = 1, [0x2a] = 1, [0x2b] = 1,
    [0x30] = 1, [0x31] = 1, [0x32] = 1, [0x33] = 1, [0x34] = 32,
    [0x40] = 1, [0x41] = 0, [0x50] = 1, [0x60] = 1, [0x70] = 3, [0x71] = 2, [0x72] = 1,
};

static int valid_command(unsigned c)
{
    return (c >= 0x10 && c <= 0x1c) || (c >= 0x20 && c <= 0x2b) || (c >= 0x30 && c <= 0x34) ||
           c == 0x40 || c == 0x41 || c == 0x50 || c == 0x60 || (c >= 0x70 && c <= 0x72);
}

/* Entries waiting while a SwapBuffers halts the engine until VBlank. */
typedef struct Entry {
    uint8_t cmd;
    uint32_t param;
} Entry;
static Entry *held;
static int held_count, held_cap;

static unsigned cur_cmd;
static int cur_have;
static uint32_t cur_params[32];

/* GXFIFO's packed-command unpacking. */
static uint8_t fifo_cmds[4];
static int fifo_ncmds, fifo_index, fifo_left;

void khdays_gpu3d_reset(void)
{
    memset(&g, 0, sizeof(g));
    g.proj = g.pos = g.vec = g.tex = identity;
    g.clip = identity;
    g.prim = -1;
    g.viewport[2] = 255;
    g.viewport[3] = 191;
    memset(lists, 0, sizeof(lists));
    geometry = &lists[0];
    render = &lists[1];
    held_count = 0;
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

static void set_current(const Mtx *m, int scale)
{
    if (g.mode == 0) {
        g.proj = *m;
    } else if (g.mode == 3) {
        g.tex = *m;
    } else {
        g.pos = *m;
    }
    g.clip_dirty = 1;
    (void)scale;
}

/* C = M * C, on the current matrix (and in mode 2 the vector matrix too,
 * except for MTX_SCALE). */
static void multiply(const Mtx *m, int is_scale)
{
    Mtx r = mul(m, current());
    set_current(&r, is_scale);
    if (g.mode == 2 && !is_scale) {
        g.vec = mul(m, &g.vec);
    }
}

static void load(const Mtx *m)
{
    set_current(m, 0);
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
        if (g.proj_sp > 0) g.stack_error = 1;
        g.proj_stack[0] = g.proj;
        g.proj_sp = (g.proj_sp + 1) & 1;
    } else if (g.mode == 3) {
        g.tex_stack[0] = g.tex;
        g.tex_sp = (g.tex_sp + 1) & 1;
    } else {
        if (g.pos_sp >= 31) g.stack_error = 1;
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
        if (g.pos_sp >= 31) g.stack_error = 1;
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
        if (n == 31) g.stack_error = 1;
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
        if (n == 31) g.stack_error = 1;
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
        out[j] = (int32_t)((v[0] * g.vec.m[0 * 4 + j] + v[1] * g.vec.m[1 * 4 + j] +
                            v[2] * g.vec.m[2 * 4 + j]) >> 12);
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
                          (int64_t)sext(packed >> 20, 10) * g.tex.m[8]) >> 17) + g.raw_s);
        g.t = (int32_t)((((int64_t)sext(packed, 10) * g.tex.m[1] + (int64_t)sext(packed >> 10, 10) * g.tex.m[5] +
                          (int64_t)sext(packed >> 20, 10) * g.tex.m[9]) >> 17) + g.raw_t);
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
        if (diffuse < 0) diffuse = 0;
        shine = -dot(g.half_vec[i], n);
        if (shine < 0) shine = 0;
        shine = (int32_t)(((int64_t)shine * shine) >> 12);
        if (shine > 0x1000) shine = 0x1000;
        if (g.shininess_on) {
            shine = g.shininess[shine >> 5 > 127 ? 127 : shine >> 5] << 4;
        }
        for (int c = 0; c < 3; ++c) {
            const int lc = g.light_color[i][c];
            color[c] += (g.specular[c] * lc * shine) >> 5;
            color[c] += (g.diffuse[c] * lc * diffuse) >> 5;
            color[c] += (g.ambient[c] * lc) << 7;  /* (a * lc / 32) with 12 fractional bits */
        }
    }
    for (int c = 0; c < 3; ++c) {
        int v = color[c] >> 12;
        g.color[c] = v > 31 ? 31 : v;
    }
}

/* --- Polygons --------------------------------------------------------------- */

static int clip_plane(const Clip *in, int n, Clip *out, int axis, int sign)
{
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const Clip *a = &in[i], *b = &in[(i + 1) % n];
        const int32_t *pa = &a->x, *pb = &b->x;
        const int64_t da = (int64_t)a->w - (int64_t)sign * pa[axis];  /* >= 0 inside */
        const int64_t db = (int64_t)b->w - (int64_t)sign * pb[axis];
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

static void emit_polygon(const Clip *v, int n)
{
    Clip a[10], b[10];
    int count = n;
    Polygon *p;
    double det;
    int front;
    const uint32_t attr = g.attr;
    /* facing: the winding of the first three vertices on screen (y up) */
    det = (double)v[0].x * ((double)v[1].y * v[2].w - (double)v[2].y * v[1].w) -
          (double)v[1].x * ((double)v[0].y * v[2].w - (double)v[2].y * v[0].w) +
          (double)v[2].x * ((double)v[0].y * v[1].w - (double)v[1].y * v[0].w);
    front = det > 0;
    if (det != 0 && ((front && !(attr & 0x80)) || (!front && !(attr & 0x40)))) {
        return;
    }
    if (!(attr & 0x1000)) {  /* far-plane intersecting polygons hidden */
        for (int i = 0; i < n; ++i) {
            if (v[i].z > v[i].w) {
                return;
            }
        }
    }
    memcpy(a, v, sizeof(Clip) * n);
    for (int axis = 0; axis < 3 && count > 0; ++axis) {
        count = clip_plane(a, count, b, axis, 1);
        count = clip_plane(b, count, a, axis, -1);
    }
    if (count < 3 && !(count > 0 && n >= 3)) {
        return;
    }
    if (count == 0) {
        return;
    }
    if (geometry->npolys >= MAX_POLYS || geometry->nverts + count > MAX_VERTS) {
        missing(MISSING_OVERFLOW, "polygon/vertex RAM overflow (further polygons dropped)");
        return;
    }
    p = &geometry->polys[geometry->npolys++];
    p->attr = attr;
    p->tex = g.teximage;
    p->pltt = g.pltt;
    p->first = geometry->nverts;
    p->count = count;
    p->translucent = translucent(attr, g.teximage);
    p->ymin = 1e9f;
    p->ymax = -1e9f;
    {
        const float x1 = g.viewport[0], y1 = g.viewport[1];
        const float vw = (float)(g.viewport[2] - g.viewport[0] + 1);
        const float vh = (float)(g.viewport[3] - g.viewport[1] + 1);
        for (int i = 0; i < count; ++i) {
            Vertex *o = &geometry->verts[geometry->nverts++];
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
            if (o->y < p->ymin) p->ymin = o->y;
            if (o->y > p->ymax) p->ymax = o->y;
        }
    }
}

static void vertex(int32_t x, int32_t y, int32_t z)
{
    const Mtx *m = clip_matrix();
    Clip c;
    g.vx = x;
    g.vy = y;
    g.vz = z;
    if (g.prim < 0) {
        return;  /* outside BEGIN_VTXS */
    }
    if ((g.teximage >> 30) == 3) {
        missing(MISSING_TEXGEN_VERTEX,
                "texture coordinates from vertices (GBATEK's shift; not verified against hardware)");
        g.s = (int32_t)((((int64_t)x * g.tex.m[0] + (int64_t)y * g.tex.m[4] + (int64_t)z * g.tex.m[8]) >> 20) +
                        g.raw_s);
        g.t = (int32_t)((((int64_t)x * g.tex.m[1] + (int64_t)y * g.tex.m[5] + (int64_t)z * g.tex.m[9]) >> 20) +
                        g.raw_t);
    }
    c.x = (int32_t)(((int64_t)x * m->m[0] + (int64_t)y * m->m[4] + (int64_t)z * m->m[8] + ((int64_t)m->m[12] << 12)) >> 12);
    c.y = (int32_t)(((int64_t)x * m->m[1] + (int64_t)y * m->m[5] + (int64_t)z * m->m[9] + ((int64_t)m->m[13] << 12)) >> 12);
    c.z = (int32_t)(((int64_t)x * m->m[2] + (int64_t)y * m->m[6] + (int64_t)z * m->m[10] + ((int64_t)m->m[14] << 12)) >> 12);
    c.w = (int32_t)(((int64_t)x * m->m[3] + (int64_t)y * m->m[7] + (int64_t)z * m->m[11] + ((int64_t)m->m[15] << 12)) >> 12);
    c.r = expand5(g.color[0]);
    c.g = expand5(g.color[1]);
    c.b = expand5(g.color[2]);
    c.s = (int16_t)g.s;
    c.t = (int16_t)g.t;

    g.strip[g.strip_count++] = c;
    switch (g.prim) {
    case 0:  /* separate triangles */
        if (g.strip_count == 3) {
            emit_polygon(g.strip, 3);
            g.strip_count = 0;
        }
        break;
    case 1:  /* separate quads */
        if (g.strip_count == 4) {
            emit_polygon(g.strip, 4);
            g.strip_count = 0;
        }
        break;
    case 2:  /* triangle strip: every second triangle is wound the other way */
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
    default:  /* quad strip: v0 v1 v3 v2 */
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

/* --- Tests ------------------------------------------------------------------ */

static void box_test(const uint32_t *p)
{
    const int32_t x = sext(p[0], 16), y = sext(p[0] >> 16, 16), z = sext(p[1], 16);
    const int32_t w = sext(p[1] >> 16, 16), h = sext(p[2], 16), d = sext(p[2] >> 16, 16);
    const Mtx *m = clip_matrix();
    Clip corner[8];
    static const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4},
                                    {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
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
            count = clip_plane(a, count, b, axis, 1);
            count = clip_plane(b, count, a, axis, -1);
        }
        g.box_result = count > 0;
    }
}

/* --- Commands ----------------------------------------------------------------- */

static void swap_now(void);

static void execute(unsigned cmd, const uint32_t *p)
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
        for (int i = 0; i < 16; ++i) m.m[i] = (int32_t)p[i];
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
        if (cmd == 0x17) load(&m); else multiply(&m, 0);
        break;
    case 0x18:
        for (int i = 0; i < 16; ++i) m.m[i] = (int32_t)p[i];
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
            g.s = (int32_t)(((int64_t)g.raw_s * g.tex.m[0] + (int64_t)g.raw_t * g.tex.m[4] + g.tex.m[8] + g.tex.m[12]) >> 12);
            g.t = (int32_t)(((int64_t)g.raw_s * g.tex.m[1] + (int64_t)g.raw_t * g.tex.m[5] + g.tex.m[9] + g.tex.m[13]) >> 12);
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
    case 0x41: break;  /* END_VTXS does nothing on the hardware */
    case 0x50:
        g.swap_pending = 1;
        g.swap_next_params = p[0] & 3;
        break;
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
            g.pos_result[j] = (int32_t)((x * c->m[j] + y * c->m[4 + j] + z * c->m[8 + j] + ((int64_t)c->m[12 + j] << 12)) >> 12);
        }
        break;
    }
    case 0x72: {
        int32_t v[3];
        direction(p[0], v);
        for (int j = 0; j < 3; ++j) {
            g.vec_result[j] = (int16_t)sext((uint32_t)v[j], 13);  /* 4-bit sign, 12 fractional */
        }
        break;
    }
    default:
        break;
    }
}

/* One FIFO entry (command, parameter) into the engine. */
static void entry(unsigned cmd, uint32_t param)
{
    if (g.swap_pending) {  /* the engine is halted until VBlank */
        if (held_count == held_cap) {
            held_cap = held_cap ? held_cap * 2 : 1024;
            held = (Entry *)realloc(held, held_cap * sizeof(Entry));
        }
        held[held_count].cmd = (uint8_t)cmd;
        held[held_count].param = param;
        ++held_count;
        return;
    }
    if (!valid_command(cmd)) {
        return;  /* invalid commands fetch no parameters (GBATEK) */
    }
    if (param_count[cmd] == 0) {
        execute(cmd, NULL);
        return;
    }
    if (cur_have == 0) {
        cur_cmd = cmd;
    }
    cur_params[cur_have++] = param;
    if (cur_have == param_count[cur_cmd]) {
        cur_have = 0;
        execute(cur_cmd, cur_params);
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

void khdays_gpu3d_fifo_write(uint32_t value)
{
    if (fifo_ncmds == 0) {
        fifo_ncmds = 0;
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

void khdays_gpu3d_port_write(uint32_t offset, uint32_t value)
{
    entry((offset - 0x400) / 4, value);
}

void khdays_gpu3d_gxstat_write(uint32_t value)
{
    if (value & 0x8000) {
        g.stack_error = 0;
        g.proj_sp = 0;
        g.tex_sp = 0;
    }
    g.gxstat_irq = value & 0xc0000000u;
}

static uint32_t fifo_level(void)
{
    return held_count > 256 ? 256 : (uint32_t)held_count;
}

uint32_t khdays_gpu3d_read(uint32_t offset)
{
    if (offset == 0x600) {
        const uint32_t level = fifo_level();
        return (uint32_t)g.box_result << 1 | (uint32_t)(g.pos_sp & 31) << 8 |
               (uint32_t)(g.proj_sp & 1) << 13 | (uint32_t)g.stack_error << 15 | level << 16 |
               (level < 128 ? 1u << 25 : 0) | (level == 0 ? 1u << 26 : 0) |
               (g.swap_pending || held_count ? 1u << 27 : 0) | g.gxstat_irq;
    }
    if (offset == 0x604) {
        return (uint32_t)geometry->npolys | (uint32_t)geometry->nverts << 16;
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
        const int i = (offset - 0x680) / 4;  /* 3x3: rows of the 4x4 */
        return (uint32_t)g.vec.m[(i / 3) * 4 + i % 3];
    }
    return 0;
}

int khdays_gpu3d_irq(void)
{
    const uint32_t mode = g.gxstat_irq >> 30;
    const uint32_t level = fifo_level();
    return (mode == 1 && level < 128) || (mode == 2 && level == 0);
}

static void swap_now(void)
{
    List *t = render;
    render = geometry;
    geometry = t;
    render->swap_params = g.swap_params;
    g.swap_params = g.swap_next_params;
    geometry->npolys = geometry->nverts = 0;
    g.swap_pending = 0;
}

/* --- Rendering ------------------------------------------------------------------ */

#define W 256
#define H 192

typedef struct Pixel {
    uint8_t r, g, b, a;  /* 6/6/6 bits, alpha 5 */
    uint32_t depth;      /* 24 bits */
    uint8_t opaque_id, trans_id, fog, edge, stencil, translucent;
} Pixel;

static Pixel fb[W * H];
static const uint8_t *rio;
static const KhdaysVramPages *rvram;

static uint16_t io16(uint32_t offset)
{
    return (uint16_t)(rio[offset] | rio[offset + 1] << 8);
}

static uint32_t io32(uint32_t offset)
{
    return (uint32_t)io16(offset) | (uint32_t)io16(offset + 2) << 16;
}

static uint8_t tex8(uint32_t address)
{
    const uint8_t *page;
    address &= 0x7ffff;
    page = rvram->page[KHDAYS_AREA_TEX][address / KHDAYS_VRAM_PAGE];
    return page ? page[address % KHDAYS_VRAM_PAGE] : 0;
}

static uint16_t tex16(uint32_t address)
{
    return (uint16_t)(tex8(address) | tex8(address + 1) << 8);
}

static uint16_t pal16(uint32_t address)
{
    const uint8_t *page;
    address %= 0x18000;
    page = rvram->page[KHDAYS_AREA_TEXPAL][address / KHDAYS_VRAM_PAGE];
    return page ? (uint16_t)(page[address % KHDAYS_VRAM_PAGE] | page[address % KHDAYS_VRAM_PAGE + 1] << 8) : 0;
}

static int wrap_coord(int c, int size, int repeat, int flip)
{
    if (!repeat) {
        return c < 0 ? 0 : c >= size ? size - 1 : c;
    }
    if (flip) {
        const int period = c & (2 * size - 1);
        return period >= size ? 2 * size - 1 - period : period;
    }
    return c & (size - 1);
}

/* A texel: RGB 5 bits each in bits 0-14, alpha 0-31 in bits 16-20. */
static uint32_t texel(uint32_t param, uint32_t pltt, int s, int t)
{
    const uint32_t base = (param & 0xffff) * 8;
    const int ss = 8 << ((param >> 20) & 7), ts = 8 << ((param >> 23) & 7);
    const unsigned format = (param >> 26) & 7;
    const int zero_transparent = (param >> 29) & 1;
    const uint32_t pal = pltt * (format == 2 ? 8 : 16);
    uint32_t index;
    s = wrap_coord(s, ss, (param >> 16) & 1, (param >> 18) & 1);
    t = wrap_coord(t, ts, (param >> 17) & 1, (param >> 19) & 1);
    switch (format) {
    case 1: {
        const uint8_t v = tex8(base + (uint32_t)(t * ss + s));
        const unsigned a3 = v >> 5;
        return (pal16(pal + (v & 31) * 2) & 0x7fff) | (a3 * 4 + a3 / 2) << 16;
    }
    case 2:
        index = (tex8(base + (uint32_t)(t * ss + s) / 4) >> (2 * (s & 3))) & 3;
        break;
    case 3:
        index = (tex8(base + (uint32_t)(t * ss + s) / 2) >> (4 * (s & 1))) & 15;
        break;
    case 4:
        index = tex8(base + (uint32_t)(t * ss + s));
        break;
    case 5: {
        const uint32_t block = base + (uint32_t)((t / 4) * (ss / 4) + s / 4) * 4;
        const uint32_t bits = (uint32_t)tex8(block + (t & 3)) >> (2 * (s & 3)) & 3;
        const uint32_t slot1 = 0x20000 + ((block & 0x1ffff) / 2) + ((block >= 0x40000) ? 0x10000 : 0);
        const uint16_t info = tex16(slot1);
        const uint32_t pa = pltt * 16 + (info & 0x3fff) * 4;
        const unsigned mode = info >> 14;
        uint16_t c0, c1;
        if (bits == 3 && (mode == 0 || mode == 1)) {
            return 0;
        }
        if (bits < 2 || mode == 0 || mode == 2) {
            return (pal16(pa + bits * 2) & 0x7fff) | 31u << 16;
        }
        c0 = pal16(pa);
        c1 = pal16(pa + 2);
        {
            uint32_t out = 0;
            for (int sh = 0; sh < 15; sh += 5) {
                const unsigned a = (c0 >> sh) & 31, b = (c1 >> sh) & 31;
                unsigned v;
                if (mode == 1) v = (a + b) / 2;
                else if (bits == 2) v = (a * 5 + b * 3) / 8;
                else v = (a * 3 + b * 5) / 8;
                out |= v << sh;
            }
            return out | 31u << 16;
        }
    }
    case 6: {
        const uint8_t v = tex8(base + (uint32_t)(t * ss + s));
        return (pal16(pal + (v & 7) * 2) & 0x7fff) | (uint32_t)(v >> 3) << 16;
    }
    case 7: {
        const uint16_t v = tex16(base + (uint32_t)(t * ss + s) * 2);
        return (v & 0x7fff) | (v & 0x8000 ? 31u << 16 : 0);
    }
    default:
        return 0x7fff | 31u << 16;
    }
    if (index == 0 && zero_transparent) {
        return 0;
    }
    return (pal16(pal + index * 2) & 0x7fff) | 31u << 16;
}

static uint32_t depth_of(const Vertex *unused, float z, float w, int wbuffer)
{
    double d;
    (void)unused;
    if (wbuffer) {
        d = w;
    } else {
        /* the clip volume's z/w (-1..1) over the 15-bit depth range CLEAR_DEPTH
         * uses, widened to 24 bits the same way (x 0x200) */
        d = ((double)z * 0x4000 + 0x3fff) * 0x200;
    }
    if (d < 0) return 0;
    if (d > 0xffffff) return 0xffffff;
    return (uint32_t)d;
}

typedef struct Attrs {
    float iw;             /* 1/w */
    float r, g, b, s, t;  /* over w */
    float z;
} Attrs;

static void attrs_of(const Vertex *v, Attrs *a)
{
    a->iw = 1.0f / v->w;
    a->r = v->r * a->iw;
    a->g = v->g * a->iw;
    a->b = v->b * a->iw;
    a->s = v->s * a->iw;
    a->t = v->t * a->iw;
    a->z = v->z;
}

static void lerp(const Attrs *a, const Attrs *b, float f, Attrs *o)
{
    o->iw = a->iw + (b->iw - a->iw) * f;
    o->r = a->r + (b->r - a->r) * f;
    o->g = a->g + (b->g - a->g) * f;
    o->b = a->b + (b->b - a->b) * f;
    o->s = a->s + (b->s - a->s) * f;
    o->t = a->t + (b->t - a->t) * f;
    o->z = a->z + (b->z - a->z) * f;
}

static int expand_alpha(int a5)
{
    return a5 ? a5 * 2 + 1 : 0;
}

/* One pixel of polygon `p`. */
static void plot(const Polygon *p, int x, int y, const Attrs *at, int edge, uint32_t disp3dcnt, int wbuffer)
{
    Pixel *px = &fb[y * W + x];
    const float w = 1.0f / at->iw;
    const uint32_t depth = depth_of(NULL, at->z, w, wbuffer);
    const unsigned mode = (p->attr >> 4) & 3;
    const unsigned id = (p->attr >> 24) & 63;
    int alpha = (p->attr >> 16) & 31;
    int rv = (int)(at->r * w + 0.5f), gv = (int)(at->g * w + 0.5f), bv = (int)(at->b * w + 0.5f);
    int r, gg, b, a;
    int pass;
    if (alpha == 0) {  /* wire-frame: only the edges, solid */
        if (!edge) return;
        alpha = 31;
    }
    if (rv > 63) rv = 63;
    if (gv > 63) gv = 63;
    if (bv > 63) bv = 63;
    if (rv < 0) rv = 0;
    if (gv < 0) gv = 0;
    if (bv < 0) bv = 0;

    if (p->attr & 0x4000) {
        const int64_t diff = (int64_t)depth - px->depth;
        pass = diff >= -0x200 && diff <= 0x200;
    } else {
        pass = depth < px->depth;
    }
    if (mode == 3) {  /* shadow polygons */
        if (id == 0) {
            if (pass) px->stencil = 1;
            return;
        }
        if (px->stencil) {
            px->stencil = 0;
            return;
        }
        if (!pass || px->opaque_id == id) {
            return;
        }
    } else if (!pass) {
        return;
    }

    /* texture and vertex colour (GBATEK, "DS 3D Texture Blending") */
    {
        const unsigned format = (p->tex >> 26) & 7;
        int rt = 63, gt = 63, bt = 63, at6 = 63, at5 = 31;
        const int av6 = expand_alpha(alpha);
        int textured = format != 0 && (disp3dcnt & 1);
        if (textured) {
            const int s = (int)floorf(at->s * w / 16.0f), t = (int)floorf(at->t * w / 16.0f);
            const uint32_t tx = texel(p->tex, p->pltt, s, t);
            at5 = (int)((tx >> 16) & 31);
            rt = expand5(tx & 31);
            gt = expand5((tx >> 5) & 31);
            bt = expand5((tx >> 10) & 31);
            at6 = expand_alpha(at5);
        }
        if (mode == 1 && textured) {  /* decal */
            if (at5 == 0) { r = rv; gg = gv; b = bv; }
            else if (at5 == 31) { r = rt; gg = gt; b = bt; }
            else {
                r = (rt * at5 + rv * (63 - at5)) / 64;
                gg = (gt * at5 + gv * (63 - at5)) / 64;
                b = (bt * at5 + bv * (63 - at5)) / 64;
            }
            a = av6;
        } else if (mode == 2) {  /* toon / highlight: the red component indexes the table */
            const uint16_t toon = io16(0x380 + 2 * (rv >> 1));
            const int rs = expand5(toon & 31), gs = expand5((toon >> 5) & 31), bs = expand5((toon >> 10) & 31);
            r = ((rt + 1) * (rs + 1) - 1) / 64;
            gg = ((gt + 1) * (gs + 1) - 1) / 64;
            b = ((bt + 1) * (bs + 1) - 1) / 64;
            if (disp3dcnt & 2) {
                r = r + rs > 63 ? 63 : r + rs;
                gg = gg + gs > 63 ? 63 : gg + gs;
                b = b + bs > 63 ? 63 : b + bs;
            }
            a = ((at6 + 1) * (av6 + 1) - 1) / 64;
        } else {  /* modulation (and shadow) */
            r = ((rt + 1) * (rv + 1) - 1) / 64;
            gg = ((gt + 1) * (gv + 1) - 1) / 64;
            b = ((bt + 1) * (bv + 1) - 1) / 64;
            a = ((at6 + 1) * (av6 + 1) - 1) / 64;
        }
        a >>= 1;  /* back to 5 bits */
    }
    /* alpha test */
    if ((disp3dcnt & 4) ? a <= (int)(io16(0x340) & 31) : a == 0) {
        return;
    }
    if (a < 31 || mode == 3) {
        if (px->translucent && px->trans_id == id && mode != 3) {
            return;  /* no double blending within one translucent polygon ID */
        }
        if ((disp3dcnt & 8) && px->a > 0) {
            px->r = (uint8_t)((r * (a + 1) + px->r * (31 - a)) / 32);
            px->g = (uint8_t)((gg * (a + 1) + px->g * (31 - a)) / 32);
            px->b = (uint8_t)((b * (a + 1) + px->b * (31 - a)) / 32);
            if (a > px->a) px->a = (uint8_t)a;
        } else {
            px->r = (uint8_t)r;
            px->g = (uint8_t)gg;
            px->b = (uint8_t)b;
            px->a = (uint8_t)a;
        }
        if (p->attr & 0x800) {
            px->depth = depth;
        }
        px->trans_id = (uint8_t)id;
        px->translucent = 1;
        px->fog &= (p->attr >> 15) & 1;
    } else {
        px->r = (uint8_t)r;
        px->g = (uint8_t)gg;
        px->b = (uint8_t)b;
        px->a = 31;
        px->depth = depth;
        px->opaque_id = (uint8_t)id;
        px->translucent = 0;
        px->fog = (p->attr >> 15) & 1;
        px->edge = (uint8_t)edge;
    }
}

/* Scan conversion of a convex polygon: pixel centres, attributes
 * interpolated over 1/w (perspective-correct). The DS's own coverage rules
 * are not documented in GBATEK; these are the usual ones. */
static void draw_polygon(const Polygon *p, const Vertex *v, uint32_t disp3dcnt, int wbuffer)
{
    Attrs at[10];
    int y0, y1;
    for (int i = 0; i < p->count; ++i) {
        attrs_of(&v[i], &at[i]);
    }
    y0 = (int)ceilf(p->ymin - 0.5f);
    y1 = (int)ceilf(p->ymax - 0.5f) - 1;
    if (y1 < y0) {  /* flatter than a pixel row: one row */
        y0 = y1 = (int)floorf(p->ymin);
    }
    if (y0 < 0) y0 = 0;
    if (y1 > H - 1) y1 = H - 1;
    for (int y = y0; y <= y1; ++y) {
        const float cy = y + 0.5f;
        float xl = 1e9f, xr = -1e9f;
        Attrs al, ar;
        int found = 0;
        for (int i = 0; i < p->count; ++i) {
            const Vertex *a = &v[i], *b = &v[(i + 1) % p->count];
            float lo = a->y < b->y ? a->y : b->y, hi = a->y < b->y ? b->y : a->y;
            float f, x;
            Attrs e;
            if (hi == lo) {
                if (y0 != y1 || (int)floorf(lo) != y) continue;
                /* a horizontal edge on a single-row polygon: both ends */
                for (int k = 0; k < 2; ++k) {
                    const Vertex *q = k ? b : a;
                    Attrs qa;
                    attrs_of(q, &qa);
                    if (q->x < xl) { xl = q->x; al = qa; }
                    if (q->x > xr) { xr = q->x; ar = qa; }
                    found = 1;
                }
                continue;
            }
            if (cy < lo || cy > hi) continue;
            f = (cy - a->y) / (b->y - a->y);
            x = a->x + (b->x - a->x) * f;
            lerp(&at[i], &at[(i + 1) % p->count], f, &e);
            if (x < xl) { xl = x; al = e; }
            if (x > xr) { xr = x; ar = e; }
            found = 1;
        }
        if (!found) continue;
        {
            int x0 = (int)ceilf(xl - 0.5f), x1 = (int)ceilf(xr - 0.5f) - 1;
            const float span = xr - xl;
            if (x1 < x0) x1 = x0;  /* at least a pixel: lines and dots */
            for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < W; ++x) {
                Attrs pa;
                const float f = span > 0 ? ((x + 0.5f) - xl) / span : 0.0f;
                lerp(&al, &ar, f < 0 ? 0 : f > 1 ? 1 : f, &pa);
                plot(p, x, y, &pa, x == x0 || x == x1 || y == y0 || y == y1, disp3dcnt, wbuffer);
            }
        }
    }
}

static int compare_translucent(const void *a, const void *b)
{
    const Polygon *pa = *(const Polygon *const *)a, *pb = *(const Polygon *const *)b;
    if (pa->ymin != pb->ymin) return pa->ymin < pb->ymin ? -1 : 1;
    if (pa->ymax != pb->ymax) return pa->ymax < pb->ymax ? -1 : 1;
    return pa < pb ? -1 : pa > pb;
}

static void rear_plane(uint32_t disp3dcnt)
{
    const uint32_t clear = io32(0x350);
    const uint16_t cd = io16(0x354) & 0x7fff;
    const uint32_t clear_depth = (uint32_t)cd * 0x200 + ((cd + 1u) / 0x8000u) * 0x1ff;
    if (disp3dcnt & 0x4000) {  /* bitmap: colour in texture slot 2, depth in slot 3 */
        const uint16_t offset = io16(0x356);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const uint32_t i = (uint32_t)((((y + (offset >> 8)) & 255) * 256 + ((x + offset) & 255)) * 2);
                const uint16_t c = tex16(0x40000 + i), d = tex16(0x60000 + i);
                const uint32_t dd = d & 0x7fff;
                Pixel *px = &fb[y * W + x];
                px->r = (uint8_t)expand5(c & 31);
                px->g = (uint8_t)expand5((c >> 5) & 31);
                px->b = (uint8_t)expand5((c >> 10) & 31);
                px->a = (c & 0x8000) ? 31 : 0;
                px->depth = dd * 0x200 + ((dd + 1) / 0x8000) * 0x1ff;
                px->fog = (uint8_t)(d >> 15);
                px->opaque_id = (uint8_t)((clear >> 24) & 63);
                px->trans_id = 0xff;
                px->edge = px->stencil = px->translucent = 0;
            }
        }
        return;
    }
    for (int i = 0; i < W * H; ++i) {
        Pixel *px = &fb[i];
        px->r = (uint8_t)expand5(clear & 31);
        px->g = (uint8_t)expand5((clear >> 5) & 31);
        px->b = (uint8_t)expand5((clear >> 10) & 31);
        px->a = (uint8_t)((clear >> 16) & 31);
        px->depth = clear_depth;
        px->fog = (uint8_t)((clear >> 15) & 1);
        px->opaque_id = (uint8_t)((clear >> 24) & 63);
        px->trans_id = 0xff;
        px->edge = px->stencil = px->translucent = 0;
    }
}

static void edge_marking(void)
{
    const uint32_t clear = io32(0x350);
    const uint16_t cd = io16(0x354) & 0x7fff;
    const uint32_t clear_depth = (uint32_t)cd * 0x200 + ((cd + 1u) / 0x8000u) * 0x1ff;
    const uint8_t clear_id = (uint8_t)((clear >> 24) & 63);
    static uint8_t marked[W * H];
    memset(marked, 0, sizeof(marked));
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const Pixel *px = &fb[y * W + x];
            static const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
            if (!px->edge || px->translucent) continue;
            for (int k = 0; k < 4; ++k) {
                const int nx = x + dx[k], ny = y + dy[k];
                uint8_t nid;
                uint32_t nd;
                if (nx < 0 || nx >= W || ny < 0 || ny >= H) {
                    nid = clear_id;
                    nd = clear_depth;
                } else {
                    nid = fb[ny * W + nx].opaque_id;
                    nd = fb[ny * W + nx].depth;
                }
                if (nid != px->opaque_id && px->depth < nd) {
                    marked[y * W + x] = 1;
                    break;
                }
            }
        }
    }
    for (int i = 0; i < W * H; ++i) {
        if (marked[i]) {
            const uint16_t c = io16(0x330 + 2 * (fb[i].opaque_id >> 3));
            fb[i].r = (uint8_t)expand5(c & 31);
            fb[i].g = (uint8_t)expand5((c >> 5) & 31);
            fb[i].b = (uint8_t)expand5((c >> 10) & 31);
        }
    }
}

static void fog(uint32_t disp3dcnt, int wbuffer)
{
    const uint32_t color = io32(0x358);
    const uint32_t offset = io16(0x35c) & 0x7fff;
    const uint32_t shift = (disp3dcnt >> 8) & 15;
    const uint32_t step = 0x400 >> shift;
    const int fr = expand5(color & 31), fg = expand5((color >> 5) & 31), fbl = expand5((color >> 10) & 31);
    const int fa = (color >> 16) & 31;
    for (int i = 0; i < W * H; ++i) {
        Pixel *px = &fb[i];
        const uint32_t d = wbuffer ? px->depth : px->depth >> 9;  /* the 15-bit scale of FOG_OFFSET */
        int density;
        if (!px->fog) continue;
        if (step == 0 || d <= offset + step) {
            density = rio[0x360] & 0x7f;
        } else if (d >= offset + step * 32) {
            density = rio[0x360 + 31] & 0x7f;
        } else {
            const uint32_t n = (d - offset) / step - 1;
            const uint32_t lo = offset + step * (n + 1);
            const int d0 = rio[0x360 + n] & 0x7f, d1 = rio[0x360 + n + 1] & 0x7f;
            density = d0 + (int)((d1 - d0) * (int)(d - lo) / (int)step);
        }
        if (density == 127) density = 128;
        if (!(disp3dcnt & 0x40)) {
            px->r = (uint8_t)((fr * density + px->r * (128 - density)) / 128);
            px->g = (uint8_t)((fg * density + px->g * (128 - density)) / 128);
            px->b = (uint8_t)((fbl * density + px->b * (128 - density)) / 128);
        }
        px->a = (uint8_t)((fa * density + px->a * (128 - density)) / 128);
    }
}

void khdays_gpu3d_vblank(const uint8_t *io, const KhdaysVramPages *vram, uint32_t *out)
{
    static Polygon *order[MAX_POLYS];
    uint32_t disp3dcnt;
    int n = 0, wbuffer;
    rio = io;
    rvram = vram;
    if (g.swap_pending) {
        swap_now();
        /* the engine resumes: what waited behind SwapBuffers runs now */
        {
            int i = 0;
            while (i < held_count && !g.swap_pending) {
                const Entry e = held[i++];
                entry(e.cmd, e.param);
            }
            memmove(held, held + i, (size_t)(held_count - i) * sizeof(Entry));
            held_count -= i;
        }
    }
    if (out == NULL) {
        return;  /* the swap only: no one sees this frame (display.c) */
    }
    disp3dcnt = io16(0x60);
    wbuffer = (render->swap_params >> 1) & 1;
    if (disp3dcnt & 0x10) {
        missing(MISSING_ANTIALIAS, "anti-aliasing (drawn without it)");
    }
    rear_plane(disp3dcnt);
    for (int i = 0; i < render->npolys; ++i) {
        if (!render->polys[i].translucent) order[n++] = &render->polys[i];
    }
    {
        const int first_translucent = n;
        for (int i = 0; i < render->npolys; ++i) {
            if (render->polys[i].translucent) order[n++] = &render->polys[i];
        }
        if (!(render->swap_params & 1)) {  /* auto-sort by Y */
            qsort(order + first_translucent, (size_t)(n - first_translucent), sizeof(order[0]),
                  compare_translucent);
        }
    }
    for (int i = 0; i < n; ++i) {
        draw_polygon(order[i], &render->verts[order[i]->first], disp3dcnt, wbuffer);
    }
    if (disp3dcnt & 0x20) {
        edge_marking();
    }
    if (disp3dcnt & 0x80) {
        fog(disp3dcnt, wbuffer);
    }
    for (int i = 0; i < W * H; ++i) {
        const Pixel *px = &fb[i];
        out[i] = (uint32_t)px->r | (uint32_t)px->g << 8 | (uint32_t)px->b << 16 | (uint32_t)px->a << 24;
    }
}
