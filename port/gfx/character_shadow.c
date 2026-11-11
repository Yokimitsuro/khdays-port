/* Capture the current pose once, then project its silhouette onto nearby floor polygons. */
#include "character_shadow.h"
#include "../host/app/hud_settings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct Point {
    float world[3];
    KhdaysG3dClipVertex clip;
} Point;
typedef struct Capture {
    uintptr_t owner;
    int pre, volume, sprite;
    Point points[4];
} Capture;
typedef struct Shadow {
    uintptr_t owner;
    int sprite;
} Shadow;
static Capture *captures;
static int count, capacity, depth, enabled, volume = -1;
static uintptr_t caster;
static Shadow *shadows;
static int shadow_count, shadow_capacity;
static KhdaysShadowOutcome outcomes[64]; /* by owner, the oldest replaced (khdays_shadow_outcome) */
static unsigned lists_finished;
static int receives(const Capture *r, const KhdaysG3dPolygon *p);

unsigned khdays_shadow_lists_finished(void)
{
    return lists_finished;
}

const KhdaysShadowOutcome *khdays_shadow_outcome(uintptr_t owner)
{
    for (int i = 0; i < 64; ++i)
        if (outcomes[i].owner == owner && owner != 0)
            return &outcomes[i];
    return NULL;
}

static KhdaysShadowOutcome *outcome_for(uintptr_t owner)
{
    KhdaysShadowOutcome *oldest = &outcomes[0];
    for (int i = 0; i < 64; ++i) {
        if (outcomes[i].owner == owner)
            return &outcomes[i];
        if (outcomes[i].list < oldest->list)
            oldest = &outcomes[i];
    }
    memset(oldest, 0, sizeof(*oldest));
    oldest->owner = owner;
    return oldest;
}

/* The polygons under the ground plan lo..hi (any height) that are not `owner`'s, by why they do not receive */
static void tally_under(const KhdaysG3dList *list, uintptr_t owner, const float lo[3], const float hi[3],
                        KhdaysShadowOutcome *o)
{
    o->under_lo = 1e30f;
    o->under_hi = -1e30f;
    for (int i = 0; i < count; ++i) {
        const Capture *r = &captures[i];
        const KhdaysG3dPolygon *p = &list->pre[r->pre];
        if (r->volume != -1 || r->owner == owner)
            continue;
        float plo[3] = {1e30f, 1e30f, 1e30f}, phi[3] = {-1e30f, -1e30f, -1e30f};
        for (int v = 0; v < p->count; ++v)
            for (int k = 0; k < 3; ++k) {
                plo[k] = fminf(plo[k], r->points[v].world[k]);
                phi[k] = fmaxf(phi[k], r->points[v].world[k]);
            }
        if (phi[0] < lo[0] || plo[0] > hi[0] || phi[2] < lo[2] || plo[2] > hi[2])
            continue;
        ++o->under;
        o->under_lo = fminf(o->under_lo, plo[1]);
        o->under_hi = fmaxf(o->under_hi, phi[1]);
        int caster_of = 0;
        for (int j = 0; j < shadow_count; ++j)
            caster_of |= r->owner == shadows[j].owner;
        if (caster_of)
            ++o->casters;
        else if (((p->attr >> 4) & 3) == 3)
            ++o->shadow_mode;
        else if (((p->attr >> 16) & 31) != 31) {
            ++o->translucent;
            o->translucent_alpha = (int)((p->attr >> 16) & 31);
        } else if (!receives(r, p))
            ++o->steep;
    }
}
static int sprite;
static float camera[16], inverse[16];
static int32_t last_projection[16];
static int inverse_valid;

static int invert(const float *m, float *out)
{
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c)
            a[r][c] = c < 4 ? m[c * 4 + r] : (c - 4 == r);
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (fabs(a[r][c]) > fabs(a[pivot][c]))
                pivot = r;
        if (fabs(a[pivot][c]) < 1e-12)
            return 0;
        for (int k = 0; k < 8; ++k) {
            double t = a[pivot][k];
            a[pivot][k] = a[c][k];
            a[c][k] = t;
        }
        double divisor = a[c][c];
        for (int k = 0; k < 8; ++k)
            a[c][k] /= divisor;
        for (int r = 0; r < 4; ++r) {
            if (r == c)
                continue;
            double factor = a[r][c];
            for (int k = 0; k < 8; ++k)
                a[r][k] -= factor * a[c][k];
        }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[c * 4 + r] = (float)a[r][c + 4];
    return 1;
}

void khdays_shadow_reset(void)
{
    count = depth = enabled = shadow_count = 0;
    volume = -1;
    sprite = 0;
    caster = 0;
    inverse_valid = 0;
    memset(camera, 0, sizeof(camera));
    camera[0] = camera[5] = camera[10] = camera[15] = 1;
}

void khdays_shadow_camera(const int32_t *projection, const int32_t *m)
{
    enabled |= khdays_character_shadows_enabled();
    memset(camera, 0, sizeof(camera));
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 3; ++r)
            camera[c * 4 + r] = (float)m[c * 3 + r] / 4096;
    camera[15] = 1;
    memcpy(last_projection, projection, sizeof(last_projection));
    inverse_valid = 0;
}

void khdays_shadow_capture_begin(uintptr_t owner, int on)
{
    enabled |= on;
    if (depth++ == 0)
        caster = on ? owner : 0;
}

void khdays_shadow_capture_end(void)
{
    if (depth > 0 && --depth == 0)
        caster = 0;
}

/* A new shadow's place in the list, grown as more casters come; -1 out of memory (that one has none) */
static int new_shadow(void)
{
    if (shadow_count == shadow_capacity) {
        const int n = shadow_capacity ? shadow_capacity * 2 : 32;
        Shadow *grown = realloc(shadows, (size_t)n * sizeof(*grown));
        if (!grown)
            return -1;
        shadows = grown;
        shadow_capacity = n;
    }
    return shadow_count++;
}

void khdays_shadow_volume_begin(uintptr_t owner, int on)
{
    enabled |= on;
    volume = -1;
    if (!on || !owner)
        return;
    for (int i = 0; i < shadow_count; ++i)
        if (shadows[i].owner == owner) {
            volume = i;
            break;
        }
    if (volume < 0 && (volume = new_shadow()) < 0)
        return;
    shadows[volume].owner = owner;
    shadows[volume].sprite = 0;
}

void khdays_shadow_volume_end(void)
{
    volume = -1;
    sprite = 0;
}

void khdays_shadow_sprite_begin(void)
{
    volume = -2; /* preserve an unmatched sprite as the fallback, never use it as a caster */
    sprite = 1;
    if (!enabled || !caster)
        return;
    for (int i = 0; i < shadow_count; ++i)
        if (shadows[i].owner == caster) {
            volume = i;
            return;
        }
    const int added = new_shadow();
    if (added < 0)
        return;
    volume = added;
    memset(&shadows[volume], 0, sizeof(shadows[volume]));
    shadows[volume].owner = caster;
    shadows[volume].sprite = 1;
}

int khdays_shadow_sprite_active(void)
{
    return sprite;
}

int khdays_shadow_has_sprite(void)
{
    if (!enabled || !caster)
        return 0;
    for (int i = count - 1; i >= 0; --i)
        if (captures[i].owner == caster && captures[i].sprite)
            return 1;
    return 0;
}

void khdays_shadow_record(const KhdaysG3dList *list)
{
    if (!enabled || list->npre == 0)
        return;
    const KhdaysG3dPolygon *p = &list->pre[list->npre - 1];
    if (p->count < 3 || p->count > 4 ||
        (p->flags & (KHDAYS_G3D_HUD | KHDAYS_G3D_SKY | KHDAYS_G3D_MARKER | KHDAYS_G3D_CLIP)))
        return;
    if (!inverse_valid) {
        float combined[16];
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) {
                float sum = 0;
                for (int k = 0; k < 4; ++k)
                    sum += (float)last_projection[k * 4 + r] / 4096 * camera[c * 4 + k];
                combined[c * 4 + r] = sum;
            }
        inverse_valid = invert(combined, inverse);
        if (!inverse_valid)
            return;
    }
    if (count == capacity) {
        int n = capacity ? capacity * 2 : 1024;
        Capture *grown = realloc(captures, (size_t)n * sizeof(*grown));
        if (!grown)
            return;
        captures = grown;
        capacity = n;
    }
    Capture *record = &captures[count];
    record->owner = caster;
    record->pre = list->npre - 1;
    record->volume = volume;
    record->sprite = sprite;
    for (int i = 0; i < p->count; ++i) {
        const KhdaysG3dPreVertex *v = &list->pre_verts[p->first + i];
        KhdaysG3dClipVertex *clip = &record->points[i].clip;
        if (p->flags & KHDAYS_G3D_MOD)
            memcpy(clip, v, sizeof(*clip));
        else {
            float fields[10];
            for (int k = 0; k < 10; ++k)
                fields[k] = (float)v->word[k];
            for (int k = 0; k < 4; ++k)
                fields[k] /= 4096;
            memcpy(clip, fields, sizeof(*clip));
            clip->a = 1;
        }
        float q[4] = {clip->x, clip->y, clip->z, clip->w}, world[4] = {0};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                world[r] += inverse[c * 4 + r] * q[c];
        if (!isfinite(world[3]) || fabsf(world[3]) < 1e-8f)
            return;
        for (int k = 0; k < 3; ++k) {
            record->points[i].world[k] = world[k] / world[3];
            if (!isfinite(record->points[i].world[k]))
                return;
        }
    }
    ++count;
}

static int clip_plane(const Point *in, int n, Point *out, int axis, float bound, int sign)
{
    int used = 0;
    Point a = in[n - 1];
    float da = sign * (a.world[axis] - bound);
    for (int i = 0; i < n; ++i) {
        Point b = in[i];
        float db = sign * (b.world[axis] - bound);
        if ((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            Point *v = &out[used++];
            for (int k = 0; k < 3; ++k)
                v->world[k] = a.world[k] + t * (b.world[k] - a.world[k]);
            float d[10], x[10], y[10];
            memcpy(x, &a.clip, sizeof(x));
            memcpy(y, &b.clip, sizeof(y));
            for (int k = 0; k < 10; ++k)
                d[k] = x[k] + t * (y[k] - x[k]);
            memcpy(&v->clip, d, sizeof(d));
        }
        if (db >= 0)
            out[used++] = b;
        a = b;
        da = db;
    }
    return used;
}

/* A capture another shadow's polygons may fall on: opaque (alpha 31), not a shadow's own, of no caster, not steep */
static int receives(const Capture *r, const KhdaysG3dPolygon *p)
{
    if (r->volume != -1 || ((p->attr >> 4) & 3) == 3 || ((p->attr >> 16) & 31) != 31)
        return 0;
    for (int j = 0; j < shadow_count; ++j)
        if (r->owner == shadows[j].owner)
            return 0;
    const float *a = r->points[0].world, *b = r->points[1].world, *c = r->points[2].world;
    float nx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
    float ny = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
    float nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    return ny * ny >= 0.04f * (nx * nx + ny * ny + nz * nz);
}

/* Shadow `s`'s receiving polygons: every receiver's part inside the box lo..hi, appended; their count */
static int add_receivers(KhdaysG3dList *list, int s, const float lo[3], const float hi[3],
                         const KhdaysG3dPolygon *shade, const float shade_rgb[3], int *failed)
{
    int receivers = 0;
    for (int i = 0; i < count; ++i) {
        const Capture *r = &captures[i];
        KhdaysG3dPolygon p = list->pre[r->pre];
        if (!receives(r, &p))
            continue;
        Point work[2][16];
        memcpy(work[0], r->points, (size_t)p.count * sizeof(Point));
        int n = p.count, buffer = 0;
        for (int k = 0; k < 3 && n; ++k) {
            n = clip_plane(work[buffer], n, work[1 - buffer], k, lo[k], 1);
            buffer = 1 - buffer;
            if (n) {
                n = clip_plane(work[buffer], n, work[1 - buffer], k, hi[k], -1);
                buffer = 1 - buffer;
            }
        }
        if (n < 3)
            continue;
        KhdaysG3dPreInfo key = list->pre_info[r->pre];
        key.draw ^= (uint64_t)shadows[s].owner << 32;
        key.occurrence |= 0x80000000u | (((unsigned)s & 0x3ffu) << 20); /* the draw's owner tells shadows apart */
        p.pltt = (p.attr >> 24) & 63;
        p.attr = (shade->attr & ~0x8f0u) | 0x4000u | 0xc0u;
        p.tex = 0;
        p.replacement = -1;
        p.translucent = 1;
        p.flags = KHDAYS_G3D_MOD | KHDAYS_G3D_SHADOW_RECEIVER;
        p.shadow = s;
        if (shadows[s].sprite) {
            p.flags |= KHDAYS_G3D_SHADOW_SPRITE | (p.pltt << 18);
            p.tex = shade->tex;
            p.pltt = shade->pltt;
            p.replacement = shade->replacement;
        }
        for (int t = 1; t < n - 1; ++t) {
            KhdaysG3dClipVertex tri[3];
            int at[3] = {0, t, t + 1};
            for (int v = 0; v < 3; ++v) {
                const Point *q = &work[buffer][at[v]];
                tri[v] = q->clip;
                tri[v].s = (q->world[0] - lo[0]) / (hi[0] - lo[0]) * KHDAYS_SHADOW_SIZE * 16;
                tri[v].t = (q->world[2] - lo[2]) / (hi[2] - lo[2]) * KHDAYS_SHADOW_SIZE * 16;
                tri[v].r = shade_rgb[0];
                tri[v].g = shade_rgb[1];
                tri[v].b = shade_rgb[2];
                tri[v].a = 1;
            }
            key.ordinal = list->pre_info[r->pre].ordinal * 16u + (unsigned)t;
            if (!khdays_g3d_shadow_polygon(tri, 3, &p, &key))
                *failed = 1;
            ++receivers;
        }
    }
    return receivers;
}

/* The highest receiver under the box lo..hi's ground plan, at most `reach` below its bottom (lo[1]); -1e30 none */
static float floor_below(const KhdaysG3dList *list, const float lo[3], const float hi[3], float reach)
{
    float top = -1e30f;
    for (int i = 0; i < count; ++i) {
        const Capture *r = &captures[i];
        const KhdaysG3dPolygon *p = &list->pre[r->pre];
        if (!receives(r, p))
            continue;
        float plo[3] = {1e30f, 1e30f, 1e30f}, phi[3] = {-1e30f, -1e30f, -1e30f};
        for (int v = 0; v < p->count; ++v)
            for (int k = 0; k < 3; ++k) {
                plo[k] = fminf(plo[k], r->points[v].world[k]);
                phi[k] = fmaxf(phi[k], r->points[v].world[k]);
            }
        if (phi[0] < lo[0] || plo[0] > hi[0] || phi[2] < lo[2] || plo[2] > hi[2])
            continue;
        if (plo[1] < lo[1] && phi[1] >= lo[1] - reach) /* its lowest under the box: the floor beneath, not a ledge */
            top = fmaxf(top, fminf(phi[1], lo[1]));
    }
    return top;
}

int khdays_shadow_finish(KhdaysG3dList *list)
{
    int changed = 0;
    enabled = 0; /* appending projected geometry must not enter the capture */
    ++lists_finished;
    for (int s = 0; s < shadow_count; ++s) {
        KhdaysShadowOutcome *o = outcome_for(shadows[s].owner);
        memset(o, 0, sizeof(*o));
        o->owner = shadows[s].owner;
        o->list = lists_finished;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        float floor_lo = 1e30f, floor_hi = -1e30f;
        KhdaysG3dPolygon shade = {0};
        float shade_rgb[3] = {0};
        int sources = 0;
        float sprite_width = 0;
        for (int i = 0; i < count; ++i) {
            const Capture *r = &captures[i];
            const KhdaysG3dPolygon *p = &list->pre[r->pre];
            if (r->volume == s) {
                if (r->sprite != shadows[s].sprite)
                    continue;
                if (shadows[s].sprite || ((p->attr >> 24) & 63) != 0) {
                    shade = *p;
                    shade_rgb[0] = r->points[0].clip.r;
                    shade_rgb[1] = r->points[0].clip.g;
                    shade_rgb[2] = r->points[0].clip.b;
                }
                for (int v = 0; v < p->count; ++v) {
                    floor_lo = fminf(floor_lo, r->points[v].world[1]);
                    floor_hi = fmaxf(floor_hi, r->points[v].world[1]);
                    for (int w = 0; w < v; ++w) {
                        sprite_width = fmaxf(sprite_width, fabsf(r->points[v].world[0] - r->points[w].world[0]));
                        sprite_width = fmaxf(sprite_width, fabsf(r->points[v].world[2] - r->points[w].world[2]));
                    }
                }
            } else if (r->owner == shadows[s].owner && r->volume == -1 && ((p->attr >> 16) & 31)) {
                for (int v = 0; v < p->count; ++v)
                    for (int k = 0; k < 3; ++k) {
                        lo[k] = fminf(lo[k], r->points[v].world[k]);
                        hi[k] = fmaxf(hi[k], r->points[v].world[k]);
                    }
                ++sources;
            }
        }
        if (shadows[s].sprite && floor_hi >= floor_lo) {
            float margin = fmaxf(.25f, sprite_width * .5f);
            floor_lo -= margin;
            floor_hi += margin;
        }
        o->sources = sources;
        o->own_lo = lo[1];
        o->own_hi = hi[1];
        o->band_lo = floor_lo;
        o->band_hi = floor_hi;
        o->outcome = !sources               ? KHDAYS_SHADOW_NO_SOURCES
                     : !shade.attr          ? KHDAYS_SHADOW_NO_SHADE
                     : floor_hi <= floor_lo ? KHDAYS_SHADOW_NO_BAND
                                            : KHDAYS_SHADOW_REPLACED_IT;
        if (!sources || !shade.attr || floor_hi <= floor_lo)
            continue;
        float pad = fmaxf(hi[0] - lo[0], hi[2] - lo[2]) * 0.04f + 0.002f;
        lo[0] -= pad;
        hi[0] += pad;
        lo[2] -= pad;
        hi[2] += pad;
        lo[1] = floor_lo;
        hi[1] = floor_hi;
        int first = list->npre, first_vertex = list->npre_verts, failed = 0;
        int receivers = add_receivers(list, s, lo, hi, &shade, shade_rgb, &failed);
        /* the game's flat shadow off any floor: ov107 puts it at its actor's own height when the floor ray finds no
         * plain floor (Ov107_AiState_ResolveContacts: a hit with a type byte, or none), the boss Saix in the air
         * among them. The floor beneath it, within 4 units, takes the shadow instead of the circle */
        if (!receivers && !failed && shadows[s].sprite) {
            const float band = hi[1] - lo[1], top = floor_below(list, lo, hi, 4.0f);
            if (top > -1e29f) {
                lo[1] = top - band * .5f;
                hi[1] = top + band * .5f;
                receivers = add_receivers(list, s, lo, hi, &shade, shade_rgb, &failed);
                o->moved_below = 1;
                o->band_lo = lo[1];
                o->band_hi = hi[1];
            }
        }
        o->receivers = receivers;
        if (!receivers) {
            o->outcome = KHDAYS_SHADOW_NO_RECEIVERS;
            tally_under(list, shadows[s].owner, lo, hi, o);
            continue;
        }
        for (int i = 0; i < count; ++i) {
            const Capture *r = &captures[i];
            KhdaysG3dPolygon p = list->pre[r->pre];
            if (r->owner != shadows[s].owner || r->volume != -1 || !((p.attr >> 16) & 31))
                continue;
            KhdaysG3dClipVertex v[4];
            for (int k = 0; k < p.count; ++k) {
                v[k] = r->points[k].clip;
                v[k].x = 2 * (r->points[k].world[0] - lo[0]) / (hi[0] - lo[0]) - 1;
                v[k].y = 1 - 2 * (r->points[k].world[2] - lo[2]) / (hi[2] - lo[2]);
                v[k].z = 0;
                v[k].w = 1;
            }
            p.attr = (p.attr & ~0xc0u) | 0xc0u;
            p.flags =
                KHDAYS_G3D_MOD | KHDAYS_G3D_SHADOW_SOURCE | ((p.flags & KHDAYS_G3D_MOD) ? 0 : KHDAYS_G3D_SHADOW_NATIVE);
            p.shadow = s;
            KhdaysG3dPreInfo key = list->pre_info[r->pre];
            key.draw ^= (uint64_t)shadows[s].owner << 32;
            key.occurrence |= 0x40000000u | (((unsigned)s & 0x3ffu) << 20);
            key.viewport[0] = 0;
            key.viewport[1] = 192 - KHDAYS_SHADOW_SIZE;
            key.viewport[2] = KHDAYS_SHADOW_SIZE - 1;
            key.viewport[3] = 191;
            key.widen = 4096;
            if (!khdays_g3d_shadow_polygon(v, p.count, &p, &key))
                failed = 1;
        }
        if (failed) {
            list->npre = first;
            list->npre_verts = first_vertex;
            o->outcome = KHDAYS_SHADOW_FAILED;
            continue;
        }
        if (list->npre > first) {
            for (int i = 0; i < count; ++i)
                if (captures[i].volume == s)
                    list->pre[captures[i].pre].flags |= KHDAYS_G3D_SHADOW_REPLACED;
            changed = 1;
        }
    }
    khdays_shadow_reset();
    return changed;
}
