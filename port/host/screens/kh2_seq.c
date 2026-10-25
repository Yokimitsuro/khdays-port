/* Kingdom Hearts II's 2D sequences played over the window (kh2_seq.h). */
#include "kh2_seq.h"
#include "../render/png_io.h"
#include "../../runtime/core/rom.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    SEQ_LINEAR = 0x1,
    SEQ_NO_BOUNCE = 0x10,
    SEQ_NO_ROTATION = 0x20,
    SEQ_NO_SCALING = 0x40,
    SEQ_COLOR_START = 0x80,
    SEQ_NO_COLOR = 0x400,
    SEQ_TRANSLATION_START = 0x1000,
    SEQ_NO_PIVOT = 0x4000
};

static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static int16_t rd16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | (uint16_t)(p[1] << 8));
}

static float f32(const uint8_t *p)
{
    const int32_t i = rd32(p);
    float f;
    memcpy(&f, &i, sizeof(f));
    return f;
}

static uint32_t as_is(uint32_t c)
{
    return c;
}

static int section(const uint8_t *data, size_t size, int at, int item, int *count, const uint8_t **items)
{
    const int n = rd32(data + at), offset = rd32(data + at + 4);
    if (n < 0 || n > 8192 || offset < 0 || (size_t)offset + (size_t)n * (size_t)item > size) {
        return 0;
    }
    *count = n;
    *items = data + offset;
    return 1;
}

void khdays_kh2_seq_free(KhdaysKh2Seq *seq)
{
    free(seq->sprites);
    free(seq->parts);
    free(seq->groups);
    free(seq->anims);
    free(seq->agroups);
    memset(seq, 0, sizeof(*seq));
}

static int parse(KhdaysKh2Seq *seq, const uint8_t *data, size_t size, uint32_t (*recolour)(uint32_t))
{
    const uint8_t *sp, *pt, *gr, *an, *ag;
    if (size < 48 || memcmp(data, "SEQD", 4) != 0 || !section(data, size, 8, 44, &seq->sprite_count, &sp) ||
        !section(data, size, 16, 20, &seq->part_count, &pt) || !section(data, size, 24, 4, &seq->group_count, &gr) ||
        !section(data, size, 32, 144, &seq->anim_count, &an) || !section(data, size, 40, 36, &seq->agroup_count, &ag)) {
        return 0;
    }
    seq->sprites = calloc((size_t)seq->sprite_count + 1, sizeof(KhdaysKh2Sprite));
    seq->parts = calloc((size_t)seq->part_count + 1, sizeof(KhdaysKh2Part));
    seq->groups = calloc((size_t)seq->group_count + 1, sizeof(KhdaysKh2Group));
    seq->anims = calloc((size_t)seq->anim_count + 1, sizeof(KhdaysKh2Anim));
    seq->agroups = calloc((size_t)seq->agroup_count + 1, sizeof(KhdaysKh2AnimGroup));
    if (!seq->sprites || !seq->parts || !seq->groups || !seq->anims || !seq->agroups) {
        return 0;
    }
    for (int i = 0; i < seq->sprite_count; ++i) {
        const uint8_t *p = sp + i * 44;
        KhdaysKh2Sprite *s = &seq->sprites[i];
        s->u0 = rd32(p + 4);
        s->v0 = rd32(p + 8);
        s->u1 = rd32(p + 12);
        s->v1 = rd32(p + 16);
        for (int c = 0; c < 4; ++c)
            s->color[c] = recolour((uint32_t)rd32(p + 28 + 4 * c));
    }
    for (int i = 0; i < seq->part_count; ++i) {
        const uint8_t *p = pt + i * 20;
        KhdaysKh2Part *q = &seq->parts[i];
        q->left = rd32(p);
        q->top = rd32(p + 4);
        q->right = rd32(p + 8);
        q->bottom = rd32(p + 12);
        q->sprite = rd32(p + 16);
        if (q->sprite < 0 || q->sprite >= seq->sprite_count)
            return 0;
    }
    for (int i = 0; i < seq->group_count; ++i) {
        KhdaysKh2Group *g = &seq->groups[i];
        g->first = rd16(gr + 4 * i);
        g->count = rd16(gr + 4 * i + 2);
        if (g->first < 0 || g->count < 0 || g->first + g->count > seq->part_count)
            return 0;
    }
    for (int i = 0; i < seq->anim_count; ++i) {
        const uint8_t *p = an + i * 144;
        KhdaysKh2Anim *a = &seq->anims[i];
        a->flags = rd32(p);
        a->group = rd32(p + 4);
        a->frame_start = rd32(p + 8);
        a->frame_end = rd32(p + 12);
        for (int k = 0; k < 2; ++k) {
            a->tx[k] = rd32(p + 16 + 4 * k);
            a->ty[k] = rd32(p + 24 + 4 * k);
            a->px[k] = rd32(p + 32 + 4 * k);
            a->py[k] = rd32(p + 40 + 4 * k);
            a->rx[k] = f32(p + 48 + 4 * k);
            a->ry[k] = f32(p + 56 + 4 * k);
            a->rz[k] = f32(p + 64 + 4 * k);
            a->s[k] = f32(p + 72 + 4 * k);
            a->sx[k] = f32(p + 80 + 4 * k);
            a->sy[k] = f32(p + 88 + 4 * k);
            /* (96..111: the curve, unused by the drawing) */
            a->bx[k] = f32(p + 112 + 4 * k);
            a->by[k] = f32(p + 120 + 4 * k);
            a->color[k] = recolour((uint32_t)rd32(p + 136 + 4 * k));
        }
        a->bx_count = rd16(p + 128);
        a->by_count = rd16(p + 130);
        a->blend = rd32(p + 132);
        if (a->group < 0 || a->group >= seq->group_count)
            return 0;
    }
    for (int i = 0; i < seq->agroup_count; ++i) {
        const uint8_t *p = ag + i * 36;
        KhdaysKh2AnimGroup *g = &seq->agroups[i];
        g->first = rd16(p);
        g->count = rd16(p + 2);
        g->no_loop = rd16(p + 4);
        g->loop_start = rd32(p + 8);
        g->loop_end = rd32(p + 12);
        if (g->first < 0 || g->count < 0 || g->first + g->count > seq->anim_count)
            return 0;
    }
    return 1;
}

int khdays_kh2_seq_load(KhdaysKh2Seq *seq, const char *relative, float sheet_units, uint32_t (*recolour)(uint32_t))
{
    char path[1024];
    size_t size = 0;
    void *data;
    int ok = 0;
    memset(seq, 0, sizeof(*seq));
    if (!khdays_mods_find(relative, path, sizeof(path)) || (data = SDL_LoadFile(path, &size)) == NULL) {
        return 0;
    }
    ok = parse(seq, (const uint8_t *)data, size, recolour != NULL ? recolour : as_is);
    SDL_free(data);
    if (!ok) {
        SDL_Log("%s: not a sequence the port reads", path);
        khdays_kh2_seq_free(seq);
        return 0;
    }
    seq->sheet_units = sheet_units;
    return 1;
}

SDL_Texture *khdays_kh2_picture(SDL_Renderer *renderer, const char *relative, int bright)
{
    char path[1024];
    int w, h;
    const char *error;
    SDL_Texture *result = NULL;
    if (!khdays_mods_find(relative, path, sizeof(path)))
        return NULL;
    uint8_t *px = khdays_png_read(path, &w, &h, &error);
    if (px == NULL)
        return NULL;
    if (bright) {
        for (size_t i = 0; i < (size_t)w * (size_t)h * 4u; ++i) {
            if (i % 4 != 3)
                px[i] = (uint8_t)(px[i] > 127 ? 255 : px[i] * 2);
        }
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    if (surface != NULL) {
        result = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (result)
            SDL_SetTextureScaleMode(result, SDL_SCALEMODE_LINEAR);
    }
    khdays_png_free(px);
    return result;
}

void khdays_kh2_stage(KhdaysKh2Stage *st, SDL_Renderer *renderer, SDL_Texture *sheet, SDL_Texture *bright, float scale,
                      float left, float top)
{
    st->renderer = renderer;
    st->sheet = sheet;
    st->bright = bright;
    st->scale = scale;
    st->left = left;
    st->top = top;
    st->stretch = 1.0f;
    st->widen = st->widen_from = 0.0f;
    st->alpha = 1.0f;
}

/* An animation's state at a frame: the pivot, the scale, the rotations' factors, the place, the tint. */
typedef struct {
    float pivot_x, pivot_y, scale_x, scale_y, cos_x, cos_y, cos_z, sin_z, x, y;
    float tint[4];
    int blend;
} Pose;

static float lerpf(float t, float a, float b)
{
    return a * (1.0f - t) + b * t;
}

static void color4(uint32_t c, float out[4])
{
    for (int k = 0; k < 4; ++k)
        out[k] = (float)(c >> (8 * k) & 0xFFu) / 128.0f;
}

static int loop_frame(int min, int max, int value)
{
    if (value < max)
        return value;
    if (max <= min)
        return min;
    return min + (value - min) % (max - min);
}

static void draw_part(const KhdaysKh2Stage *st, const KhdaysKh2Seq *seq, const KhdaysKh2Part *part, const Pose *pose)
{
    /* both triangles turning the same way whatever the rectangle's flips (KH2 mirrors a sprite by
     * swapping its edges; some renderers draw only one turning, or draw the other wrongly) */
    static const int order[2][6] = {{0, 1, 2, 1, 3, 2}, {0, 2, 1, 1, 2, 3}};
    const KhdaysKh2Sprite *s = &seq->sprites[part->sprite];
    /* a rectangle of no size is its one texel (the sheet's flat white) */
    const int flat_u = s->u0 == s->u1, flat_v = s->v0 == s->v1;
    float l = (float)part->left, r = (float)part->right, t = (float)part->top, b = (float)part->bottom;
    float u0 = (float)s->u0 + (flat_u ? 0.5f : 0.0f), u1 = flat_u ? u0 : (float)s->u1;
    float v0 = (float)s->v0 + (flat_v ? 0.5f : 0.0f), v1 = flat_v ? v0 : (float)s->v1;
    int ci[4] = {0, 1, 2, 3}; /* the sprite's corner colours at the corners drawn */
    if (st->widen != 0.0f) {
        l += l > st->widen_from ? st->widen : 0.0f;
        r += r > st->widen_from ? st->widen : 0.0f;
    }
    /* KH2 mirrors a sprite by swapping its edges: drawn here as the same rectangle the right way round
     * with its texture and colours mirrored (a renderer may draw a mirrored one wrongly) */
    if (l > r) {
        float x = l;
        l = r;
        r = x;
        x = u0;
        u0 = u1;
        u1 = x;
        int c = ci[0];
        ci[0] = ci[1];
        ci[1] = c;
        c = ci[2];
        ci[2] = ci[3];
        ci[3] = c;
    }
    if (t > b) {
        float y = t;
        t = b;
        b = y;
        y = v0;
        v0 = v1;
        v1 = y;
        int c = ci[0];
        ci[0] = ci[2];
        ci[2] = c;
        c = ci[1];
        ci[1] = ci[3];
        ci[3] = c;
    }
    const float corner[4][2] = {{l, t}, {r, t}, {l, b}, {r, b}};
    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
    SDL_Vertex v[4];
    float rgba[4][4];
    int bright = 0;
    for (int i = 0; i < 4; ++i) {
        color4(s->color[ci[i]], rgba[i]);
        for (int k = 0; k < 4; ++k)
            rgba[i][k] *= pose->tint[k];
        for (int k = 0; k < 3; ++k)
            bright |= rgba[i][k] > 1.0f;
    }
    for (int i = 0; i < 4; ++i) {
        /* pivot, scale, the turns about X and Y (seen edge on), about Z, then the place */
        float x = (corner[i][0] + pose->pivot_x) * pose->scale_x * pose->cos_y;
        float y = (corner[i][1] + pose->pivot_y) * pose->scale_y * pose->cos_x;
        const float rx = x * pose->cos_z - y * pose->sin_z, ry = x * pose->sin_z + y * pose->cos_z;
        v[i].position.x = st->left + (rx + pose->x) * st->scale;
        v[i].position.y = st->top + (ry * st->stretch + pose->y) * st->scale;
        v[i].tex_coord.x = uv[i][0] / seq->sheet_units;
        v[i].tex_coord.y = uv[i][1] / seq->sheet_units;
        v[i].color.r = SDL_min(bright ? rgba[i][0] / 2.0f : rgba[i][0], 1.0f);
        v[i].color.g = SDL_min(bright ? rgba[i][1] / 2.0f : rgba[i][1], 1.0f);
        v[i].color.b = SDL_min(bright ? rgba[i][2] / 2.0f : rgba[i][2], 1.0f);
        v[i].color.a = SDL_min(rgba[i][3], 1.0f) * st->alpha;
    }
    SDL_Texture *sheet = bright && st->bright != NULL ? st->bright : st->sheet;
    SDL_SetTextureBlendMode(sheet, pose->blend == 1 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    const float turn = (v[1].position.x - v[0].position.x) * (v[2].position.y - v[0].position.y) -
                       (v[1].position.y - v[0].position.y) * (v[2].position.x - v[0].position.x);
    SDL_RenderGeometry(st->renderer, sheet, v, 4, order[turn < 0.0f], 6);
}

void khdays_kh2_seq_draw(const KhdaysKh2Stage *st, const KhdaysKh2Seq *seq, int index, int frame, float x, float y)
{
    if (index < 0 || index >= seq->agroup_count || st->sheet == NULL || st->alpha <= 0.002f)
        return;
    const KhdaysKh2AnimGroup *g = &seq->agroups[index];
    int f = frame;
    if (!g->no_loop) {
        int end = g->loop_end;
        if (end == 0) {
            for (int i = 0; i < g->count; ++i)
                end = SDL_max(end, seq->anims[g->first + i].frame_end);
        }
        f = loop_frame(g->loop_start, end, frame);
    }
    for (int i = 0; i < g->count; ++i) {
        const KhdaysKh2Anim *a = &seq->anims[g->first + i];
        if (f < a->frame_start || f > a->frame_end)
            continue;
        const float d = a->frame_end != a->frame_start
                            ? (float)(f - a->frame_start) / (float)(a->frame_end - a->frame_start)
                            : 0.0f;
        const float t = (a->flags & SEQ_LINEAR) ? d : (sinf(d * 3.14159265f - 1.57079633f) + 1.0f) / 2.0f;
        Pose p;
        float rx = 0.0f, ry = 0.0f, rz = 0.0f;
        p.x = x;
        p.y = y;
        if (a->flags & SEQ_TRANSLATION_START) {
            p.x += (float)a->tx[0];
            p.y += (float)a->ty[0];
        } else {
            p.x += lerpf(t, (float)a->tx[0], (float)a->tx[1]);
            p.y += lerpf(t, (float)a->ty[0], (float)a->ty[1]);
        }
        if (a->flags & SEQ_NO_SCALING) {
            p.scale_x = p.scale_y = 1.0f;
        } else {
            const float s = lerpf(t, a->s[0], a->s[1]);
            p.scale_x = s * lerpf(t, a->sx[0], a->sx[1]);
            p.scale_y = s * lerpf(t, a->sy[0], a->sy[1]);
        }
        p.tint[0] = p.tint[1] = p.tint[2] = p.tint[3] = 1.0f;
        if (!(a->flags & SEQ_NO_COLOR)) {
            float c0[4], c1[4];
            color4(a->color[0], c0);
            color4(a->color[1], c1);
            for (int k = 0; k < 4; ++k)
                p.tint[k] = (a->flags & SEQ_COLOR_START) ? c0[k] : lerpf(t, c0[k], c1[k]);
        }
        if (!(a->flags & SEQ_NO_ROTATION)) {
            rx = lerpf(t, a->rx[0], a->rx[1]);
            ry = lerpf(t, a->ry[0], a->ry[1]);
            rz = lerpf(t, a->rz[0], a->rz[1]);
        }
        p.pivot_x = p.pivot_y = 0.0f;
        if (!(a->flags & SEQ_NO_PIVOT)) {
            p.pivot_x = lerpf(t, (float)a->px[0], (float)a->px[1]);
            p.pivot_y = lerpf(t, (float)a->py[0], (float)a->py[1]);
        }
        if (!(a->flags & SEQ_NO_BOUNCE)) {
            p.x += sinf(d * (float)a->bx_count * 3.14159265f) * lerpf(t, a->bx[0], a->bx[1]);
            p.y += sinf(d * (float)a->by_count * 3.14159265f) * lerpf(t, a->by[0], a->by[1]);
        }
        /* OpenKh turns by the negated angles */
        p.cos_x = cosf(-rx);
        p.cos_y = cosf(-ry);
        p.cos_z = cosf(-rz);
        p.sin_z = sinf(-rz);
        p.blend = a->blend;
        const KhdaysKh2Group *sg = &seq->groups[a->group];
        for (int k = 0; k < sg->count; ++k)
            draw_part(st, seq, &seq->parts[sg->first + k], &p);
    }
}
