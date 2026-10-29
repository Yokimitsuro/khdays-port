/* The DS's 3D rendering engine in software (gpu3d.h): the reference the
 * port's GPU renderer is checked against.
 *
 * Runs every frame it is asked for: optimized even in Debug builds, whose
 * /RTC checks cannot be combined with optimization. */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma runtime_checks("", off)
#pragma optimize("gt", on)
#endif

#include "gpu3d.h"
#include "texdecode.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Not modelled: said once ------------------------------------------------ */

enum {
    MISSING_ANTIALIAS = 1u << 0,
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

/* 5-bit to 6-bit, as GBATEK gives it: X*2+1, zero stays zero. */
static int expand5(int c)
{
    return c ? c * 2 + 1 : 0;
}

typedef KhdaysG3dVertex Vertex; /* gfx/g3d.h */
typedef KhdaysG3dPolygon Polygon;

/* --- Rendering ------------------------------------------------------------------ */

#define W 256
#define H 192

typedef struct Pixel {
    uint8_t r, g, b, a; /* 6/6/6 bits, alpha 5 */
    uint32_t depth;     /* 24 bits */
    uint8_t opaque_id, trans_id, fog, edge, stencil, translucent;
    uint8_t mask_run; /* the last polygon drawn here was a shadow mask */
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

/* The texture formats: texdecode.h (shared with the GPU renderer's texture
 * cache), read from the VRAM of the frame being drawn. */
static uint32_t texel(uint32_t param, uint32_t pltt, int s, int t)
{
    return khdays_tex_texel(rvram, param, pltt, s, t);
}

static uint16_t tex16(uint32_t address)
{
    return khdays_tex16(rvram, address);
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
    if (d < 0)
        return 0;
    if (d > 0xffffff)
        return 0xffffff;
    return (uint32_t)d;
}

typedef struct Attrs {
    float iw;            /* 1/w */
    float r, g, b, s, t; /* over w */
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
    const int marker = (p->flags & KHDAYS_G3D_MARKER) != 0;
    const unsigned mode = (p->attr >> 4) & 3;
    const unsigned id = (p->attr >> 24) & 63;
    int alpha = (p->attr >> 16) & 31;
    int rv = (int)(at->r * w + 0.5f), gv = (int)(at->g * w + 0.5f), bv = (int)(at->b * w + 0.5f);
    int r, gg, b, a;
    int pass;
    if (mode == 3 && id == 0) { /* a run of shadow masks starts from a clear stencil */
        if (!px->mask_run)
            px->stencil = 0;
        px->mask_run = 1;
    } else {
        px->mask_run = 0;
    }
    if (alpha == 0) { /* wire-frame: only the edges, solid */
        if (!edge)
            return;
        alpha = 31;
    }
    if (rv > 63)
        rv = 63;
    if (gv > 63)
        gv = 63;
    if (bv > 63)
        bv = 63;
    if (rv < 0)
        rv = 0;
    if (gv < 0)
        gv = 0;
    if (bv < 0)
        bv = 0;

    if (marker) {
        pass = 1;
    } else if (p->attr & 0x4000) {
        const int64_t diff = (int64_t)depth - px->depth;
        pass = diff >= -0x200 && diff <= 0x200;
    } else {
        pass = depth < px->depth;
    }
    if (mode == 3) {
        /* Shadow polygons, from the game's two passes (the volume's rear faces as the mask, ID 0;
         * the whole volume as the shadow, ID 0x3f): a surface is in shadow between the volume's
         * front and rear faces, so the mask marks where its rear face fails the depth test and the
         * shadow draws where a mark is and its depth test passes. GBATEK's wording would shade
         * everything behind the volume (docs/RENDERING.md, "Shadow volumes"). */
        if (id == 0) {
            if (!pass)
                px->stencil = 1;
            return;
        }
        if (!px->stencil || !pass) {
            return;
        }
        if (px->translucent ? px->trans_id == id : px->opaque_id == id) {
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
        if (mode == 1 && textured) { /* decal */
            if (at5 == 0) {
                r = rv;
                gg = gv;
                b = bv;
            } else if (at5 == 31) {
                r = rt;
                gg = gt;
                b = bt;
            } else {
                r = (rt * at5 + rv * (63 - at5)) / 64;
                gg = (gt * at5 + gv * (63 - at5)) / 64;
                b = (bt * at5 + bv * (63 - at5)) / 64;
            }
            a = av6;
        } else if (mode == 2) { /* toon / highlight: the red component indexes the table */
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
        } else { /* modulation (and shadow) */
            r = ((rt + 1) * (rv + 1) - 1) / 64;
            gg = ((gt + 1) * (gv + 1) - 1) / 64;
            b = ((bt + 1) * (bv + 1) - 1) / 64;
            a = ((at6 + 1) * (av6 + 1) - 1) / 64;
        }
        a >>= 1; /* back to 5 bits */
    }
    /* alpha test */
    if ((disp3dcnt & 4) ? a <= (int)(io16(0x340) & 31) : a == 0) {
        return;
    }
    if (a < 31 || mode == 3) {
        if (px->translucent && px->trans_id == id && mode != 3) {
            return; /* no double blending within one translucent polygon ID */
        }
        if ((disp3dcnt & 8) && px->a > 0) {
            px->r = (uint8_t)((r * (a + 1) + px->r * (31 - a)) / 32);
            px->g = (uint8_t)((gg * (a + 1) + px->g * (31 - a)) / 32);
            px->b = (uint8_t)((b * (a + 1) + px->b * (31 - a)) / 32);
            if (a > px->a)
                px->a = (uint8_t)a;
        } else {
            px->r = (uint8_t)r;
            px->g = (uint8_t)gg;
            px->b = (uint8_t)b;
            px->a = (uint8_t)a;
        }
        if ((p->attr & 0x800) && !marker) {
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
        if (!marker)
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
    if (y1 < y0) { /* flatter than a pixel row: one row */
        y0 = y1 = (int)floorf(p->ymin);
    }
    if (y0 < 0)
        y0 = 0;
    if (y1 > H - 1)
        y1 = H - 1;
    for (int y = y0; y <= y1; ++y) {
        const float cy = y + 0.5f;
        float xl = 1e9f, xr = -1e9f;
        Attrs al = {0}, ar = {0}; /* an edge at a NaN x (a vertex at w = 0) sets neither: no pixel is drawn */
        int found = 0;
        for (int i = 0; i < p->count; ++i) {
            const Vertex *a = &v[i], *b = &v[(i + 1) % p->count];
            float lo = a->y < b->y ? a->y : b->y, hi = a->y < b->y ? b->y : a->y;
            float f, x;
            Attrs e;
            if (hi == lo) {
                if (y0 != y1 || (int)floorf(lo) != y)
                    continue;
                /* a horizontal edge on a single-row polygon: both ends */
                for (int k = 0; k < 2; ++k) {
                    const Vertex *q = k ? b : a;
                    Attrs qa;
                    attrs_of(q, &qa);
                    if (q->x < xl) {
                        xl = q->x;
                        al = qa;
                    }
                    if (q->x > xr) {
                        xr = q->x;
                        ar = qa;
                    }
                    found = 1;
                }
                continue;
            }
            if (cy < lo || cy > hi)
                continue;
            f = (cy - a->y) / (b->y - a->y);
            x = a->x + (b->x - a->x) * f;
            lerp(&at[i], &at[(i + 1) % p->count], f, &e);
            if (x < xl) {
                xl = x;
                al = e;
            }
            if (x > xr) {
                xr = x;
                ar = e;
            }
            found = 1;
        }
        if (!found)
            continue;
        {
            int x0 = (int)ceilf(xl - 0.5f), x1 = (int)ceilf(xr - 0.5f) - 1;
            const float span = xr - xl;
            if (x1 < x0)
                x1 = x0; /* at least a pixel: lines and dots */
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
    if (pa->ymin != pb->ymin)
        return pa->ymin < pb->ymin ? -1 : 1;
    if (pa->ymax != pb->ymax)
        return pa->ymax < pb->ymax ? -1 : 1;
    return pa < pb ? -1 : pa > pb;
}

static void rear_plane(uint32_t disp3dcnt)
{
    const uint32_t clear = io32(0x350);
    const uint16_t cd = io16(0x354) & 0x7fff;
    const uint32_t clear_depth = (uint32_t)cd * 0x200 + ((cd + 1u) / 0x8000u) * 0x1ff;
    if (disp3dcnt & 0x4000) { /* bitmap: colour in texture slot 2, depth in slot 3 */
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
                px->edge = px->stencil = px->translucent = px->mask_run = 0;
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
        px->edge = px->stencil = px->translucent = px->mask_run = 0;
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
            if (!px->edge || px->translucent)
                continue;
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
        const uint32_t d = wbuffer ? px->depth : px->depth >> 9; /* the 15-bit scale of FOG_OFFSET */
        int density;
        if (!px->fog)
            continue;
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
        if (density == 127)
            density = 128;
        if (!(disp3dcnt & 0x40)) {
            px->r = (uint8_t)((fr * density + px->r * (128 - density)) / 128);
            px->g = (uint8_t)((fg * density + px->g * (128 - density)) / 128);
            px->b = (uint8_t)((fbl * density + px->b * (128 - density)) / 128);
        }
        px->a = (uint8_t)((fa * density + px->a * (128 - density)) / 128);
    }
}

void khdays_gpu3d_draw(const KhdaysG3dList *list, const uint8_t *io, const KhdaysVramPages *vram, uint32_t *out)
{
    uint32_t disp3dcnt;
    int wbuffer;
    rio = io;
    rvram = vram;
    disp3dcnt = io16(0x60);
    wbuffer = (list->swap_params >> 1) & 1;
    if (disp3dcnt & 0x10) {
        missing(MISSING_ANTIALIAS, "anti-aliasing (drawn without it)");
    }
    rear_plane(disp3dcnt);
    int marker_started = 0;
    for (int i = 0; i < list->npolys; ++i) {
        if (list->polys[i].flags & (KHDAYS_G3D_BEYOND_DS | KHDAYS_G3D_MOD)) {
            continue; /* the DS's RAM would not have held it, or a mod's model drew it */
        }
        if ((list->polys[i].flags & KHDAYS_G3D_MARKER) && !marker_started) {
            for (int p = 0; p < W * H; ++p)
                fb[p].trans_id = 0xff;
            marker_started = 1;
        }
        draw_polygon(&list->polys[i], &list->verts[list->polys[i].first], disp3dcnt, wbuffer);
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
