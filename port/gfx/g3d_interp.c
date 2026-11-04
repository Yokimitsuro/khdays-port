/* A picture between two of the game's 3D lists (see g3d_interp.h). */
#include "g3d_interp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t key_hash(const KhdaysG3dPreInfo *info, int32_t flags)
{
    uint32_t h = (uint32_t)info->draw * 2654435761u;
    h ^= info->occurrence * 0x85ebca6bu;
    h ^= info->ordinal * 0xc2b2ae35u;
    h ^= (uint32_t)(flags & KHDAYS_G3D_MOD) * 0x27d4eb2fu;
    return h ^ (h >> 15);
}

static int same_key(const KhdaysG3dPreInfo *a, int32_t fa, const KhdaysG3dPreInfo *b, int32_t fb)
{
    return a->draw == b->draw && a->occurrence == b->occurrence && a->ordinal == b->ordinal &&
           (fa & KHDAYS_G3D_MOD) == (fb & KHDAYS_G3D_MOD);
}

/* The older list's polygons in `work->slots`, found by key. */
static int index_older(KhdaysG3dInterp *work, const KhdaysG3dList *older)
{
    int32_t size = 64;
    while (size < older->npre * 2) {
        size *= 2;
    }
    if (size > work->slot_capacity) {
        int32_t *grown = (int32_t *)realloc(work->slots, sizeof(int32_t) * (size_t)size);
        if (grown == NULL) {
            return 0;
        }
        work->slots = grown;
        work->slot_capacity = size;
    }
    memset(work->slots, 0, sizeof(int32_t) * (size_t)size);
    for (int32_t i = 0; i < older->npre; ++i) {
        uint32_t at = key_hash(&older->pre_info[i], older->pre[i].flags) & (uint32_t)(size - 1);
        while (work->slots[at] != 0) {
            at = (at + 1) & (uint32_t)(size - 1);
        }
        work->slots[at] = i + 1;
    }
    return size;
}

/* The older list's polygon with `p`'s key, drawn as `p` is -- the same
 * texture, palette and attributes but alpha (a fade), the same texture drawn
 * instead --, or -1: a key that names another polygon (geometry that came or
 * went before it) is not taken for this one. */
static int32_t find_older(const KhdaysG3dInterp *work, int32_t size, const KhdaysG3dList *older,
                          const KhdaysG3dPreInfo *info, const KhdaysG3dPolygon *p)
{
    uint32_t at = key_hash(info, p->flags) & (uint32_t)(size - 1);
    while (work->slots[at] != 0) {
        const int32_t i = work->slots[at] - 1;
        const KhdaysG3dPolygon *q = &older->pre[i];
        if (same_key(&older->pre_info[i], q->flags, info, p->flags)) {
            const uint32_t alpha = 0x1fu << 16;
            if (q->tex != p->tex || q->pltt != p->pltt || (q->attr & ~alpha) != (p->attr & ~alpha) ||
                q->replacement != p->replacement) {
                return -1;
            }
            return i;
        }
        at = (at + 1) & (uint32_t)(size - 1);
    }
    return -1;
}

/* A vertex's place on the view, x/w and y/w (-1..1 the view); 0 when it is
 * behind the eye. A mod's words are floats, the game's fixed point. */
static int view_place(const KhdaysG3dPreVertex *v, int32_t flags, double *x, double *y)
{
    double c[4];
    for (int i = 0; i < 4; ++i) {
        if (flags & KHDAYS_G3D_MOD) {
            float f;
            memcpy(&f, &v->word[i], sizeof(f));
            c[i] = f;
        } else {
            c[i] = (double)v->word[i];
        }
    }
    if (c[3] <= 0.0)
        return 0;
    *x = c[0] / c[3];
    *y = c[1] / c[3];
    return 1;
}

/* How far two places on the view are, squared; and the distance past which a model or polygon is not drawn
 * between two lists (an eighth of the view's width, which is 2). */
static double apart(double ax, double ay, double bx, double by)
{
    return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
}
#define FAR (0.25 * 0.25)

/* Whether a polygon's first vertex moved far between two lists. */
static int moved_far(const KhdaysG3dPreVertex *a, const KhdaysG3dPreVertex *b, int32_t flags)
{
    double ax, ay, bx, by;
    if (!view_place(a, flags, &ax, &ay) || !view_place(b, flags, &bx, &by))
        return 0;
    return apart(ax, ay, bx, by) > FAR;
}

static int32_t between_fixed(int32_t a, int32_t b, double t)
{
    return (int32_t)llround((double)a + ((double)b - (double)a) * t); /* t = 1: b exactly */
}

/* A model drawing of one of the lists (a draw and its occurrence): its place on the view (its polygons'
 * first vertices on average), and for a newer one the older drawing it is taken for. */
typedef struct KhdaysG3dInterpGroup {
    uint64_t draw;
    uint32_t occurrence;
    int newer;        /* of the newer list */
    double x, y;      /* the sums, then the average */
    int32_t placed;   /* how many vertices the place was taken from */
    int32_t match;    /* newer: the older drawing it is taken for, -1 none (drawn where the newer has it) */
    int32_t taken;    /* older: a newer drawing took it */
    int32_t polygons; /* newer: of its polygons, how many were found in the older list */
} Group;

typedef struct {
    int32_t newer, older;
    double distance;
} Pair;

static int by_distance(const void *a, const void *b)
{
    const double da = ((const Pair *)a)->distance, db = ((const Pair *)b)->distance;
    return da < db ? -1 : da > db;
}

/* The drawing of `info` in the groups [from, to), else a new one at `*count`; -1 without the memory. */
static int32_t group_for(KhdaysG3dInterp *work, int32_t *count, int32_t from, const KhdaysG3dPreInfo *info, int newer)
{
    for (int32_t g = *count - 1; g >= from; --g) { /* (a drawing's polygons come together: the last first) */
        if (work->groups[g].draw == info->draw && work->groups[g].occurrence == info->occurrence) {
            return g;
        }
    }
    if (*count == work->group_capacity) {
        const int32_t room = work->group_capacity > 0 ? work->group_capacity * 2 : 256;
        Group *grown = (Group *)realloc(work->groups, sizeof(Group) * (size_t)room);
        if (grown == NULL) {
            return -1;
        }
        work->groups = grown;
        work->group_capacity = room;
    }
    {
        Group *g = &work->groups[*count];
        memset(g, 0, sizeof(*g));
        g->draw = info->draw;
        g->occurrence = info->occurrence;
        g->newer = newer;
        g->match = -1;
    }
    return (*count)++;
}

/* The model drawings of both lists (the older's first, from 0 to `*older_count`), each newer one matched
 * to the older drawing of the same model nearest to it, nearest pairs first, none farther than FAR.
 * Returns the number of groups, -1 without the memory. */
static int32_t match_drawings(KhdaysG3dInterp *work, const KhdaysG3dList *older, const KhdaysG3dList *newer,
                              int32_t *older_count)
{
    int32_t count = 0, pairs = 0, pair_room = 0;
    Pair *pair = NULL;
    if (newer->npre > work->group_of_capacity) {
        int32_t *grown = (int32_t *)realloc(work->group_of, sizeof(int32_t) * (size_t)newer->npre);
        if (grown == NULL) {
            return -1;
        }
        work->group_of = grown;
        work->group_of_capacity = newer->npre;
    }
    for (int list = 0; list < 2; ++list) {
        const KhdaysG3dList *l = list == 0 ? older : newer;
        const int32_t from = count;
        for (int32_t i = 0; i < l->npre; ++i) {
            const KhdaysG3dPreInfo *info = &l->pre_info[i];
            int32_t g;
            double x, y;
            if (list == 1) {
                work->group_of[i] = -1;
            }
            if (info->draw == 0) {
                continue;
            }
            g = group_for(work, &count, from, info, list);
            if (g < 0) {
                return -1;
            }
            if (list == 1) {
                work->group_of[i] = g;
            }
            if (view_place(&l->pre_verts[l->pre[i].first], l->pre[i].flags, &x, &y)) {
                work->groups[g].x += x;
                work->groups[g].y += y;
                ++work->groups[g].placed;
            }
        }
        if (list == 0) {
            *older_count = count;
        }
    }
    for (int32_t g = 0; g < count; ++g) {
        if (work->groups[g].placed > 0) {
            work->groups[g].x /= work->groups[g].placed;
            work->groups[g].y /= work->groups[g].placed;
        }
    }
    /* the pairs of drawings of one model near enough, nearest first; a drawing with no place (behind the
     * eye) kept with the one of its own order */
    for (int32_t n = *older_count; n < count; ++n) {
        for (int32_t o = 0; o < *older_count; ++o) {
            const Group *a = &work->groups[n], *b = &work->groups[o];
            double d;
            if (a->draw != b->draw) {
                continue;
            }
            if (a->placed == 0 || b->placed == 0) {
                if (a->occurrence != b->occurrence) {
                    continue;
                }
                d = 0.0;
            } else if ((d = apart(a->x, a->y, b->x, b->y)) > FAR) {
                continue;
            }
            if (pairs == pair_room) {
                const int32_t room = pair_room > 0 ? pair_room * 2 : 64;
                Pair *grown = (Pair *)realloc(pair, sizeof(Pair) * (size_t)room);
                if (grown == NULL) {
                    free(pair);
                    return -1;
                }
                pair = grown;
                pair_room = room;
            }
            {
                pair[pairs].newer = n;
                pair[pairs].older = o;
                pair[pairs].distance = d;
                ++pairs;
            }
        }
    }
    if (pairs > 0) {
        qsort(pair, (size_t)pairs, sizeof(Pair), by_distance);
    }
    for (int32_t k = 0; k < pairs; ++k) {
        Group *a = &work->groups[pair[k].newer], *b = &work->groups[pair[k].older];
        if (a->match < 0 && !b->taken) {
            a->match = pair[k].older;
            b->taken = 1;
        }
    }
    free(pair);
    return count;
}

int khdays_g3d_interpolate(KhdaysG3dInterp *work, const KhdaysG3dList *older, const KhdaysG3dList *newer, float alpha,
                           KhdaysG3dList *out)
{
    int32_t size, groups, older_groups = 0;
    const double t = alpha < 0.0f ? 0.0 : alpha > 1.0f ? 1.0 : (double)alpha;
    work->matched = 0;
    work->jumped = 0;
    work->jumped_mod = 0;
    memset(work->jumped_in, 0, sizeof(work->jumped_in));
    work->polygons = newer->npre;
    if (older == NULL || t >= 1.0 || older->npre == 0) {
        return khdays_g3d_finish(newer, NULL, out);
    }
    if (newer->npre_verts > work->vert_capacity) {
        KhdaysG3dPreVertex *grown =
            (KhdaysG3dPreVertex *)realloc(work->verts, sizeof(KhdaysG3dPreVertex) * (size_t)newer->npre_verts);
        if (grown == NULL) {
            khdays_g3d_finish(newer, NULL, out);
            return 0;
        }
        work->verts = grown;
        work->vert_capacity = newer->npre_verts;
    }
    size = index_older(work, older);
    groups = size != 0 ? match_drawings(work, older, newer, &older_groups) : -1;
    if (groups < 0) {
        khdays_g3d_finish(newer, NULL, out);
        return 0;
    }
    memcpy(work->verts, newer->pre_verts, sizeof(KhdaysG3dPreVertex) * (size_t)newer->npre_verts);
    for (int32_t i = 0; i < newer->npre; ++i) {
        const KhdaysG3dPolygon *p = &newer->pre[i];
        KhdaysG3dPreInfo key = newer->pre_info[i];
        const int32_t g = work->group_of[i];
        int32_t j;
        const KhdaysG3dPolygon *q;
        if (p->flags & KHDAYS_G3D_MARKER) {
            continue; /* a selection marker: where the newer list puts it (it moves from target to target) */
        }
        if (g >= 0) {
            /* a model's: its polygon in the older drawing it is taken for */
            const Group *drawing = &work->groups[g];
            if (drawing->match < 0) {
                if (find_older(work, size, older, &key, p) >= 0) {
                    ++work->jumped; /* the drawing of its own order is too far (or another's) */
                    work->jumped_mod += (p->flags & KHDAYS_G3D_MOD) != 0;
                    for (int d = 0; d < 4; ++d) {
                        if (work->jumped_in[d].count == 0) {
                            work->jumped_in[d].draw = key.draw;
                            work->jumped_in[d].occurrence = key.occurrence;
                        }
                        if (work->jumped_in[d].draw == key.draw && work->jumped_in[d].occurrence == key.occurrence) {
                            ++work->jumped_in[d].count;
                            break;
                        }
                    }
                }
                continue;
            }
            key.occurrence = work->groups[drawing->match].occurrence;
        }
        j = find_older(work, size, older, &key, p);
        if (j < 0 || (q = &older->pre[j])->count != p->count) {
            continue;
        }
        if (g < 0 && moved_far(&older->pre_verts[q->first], &newer->pre_verts[p->first], p->flags)) {
            ++work->jumped; /* outside a model (an effect's piece): another of its material's, or a cut */
            work->jumped_mod += (p->flags & KHDAYS_G3D_MOD) != 0;
            continue;
        }
        ++work->matched;
        for (int32_t k = 0; k < p->count; ++k) {
            const KhdaysG3dPreVertex *a = &older->pre_verts[q->first + k];
            KhdaysG3dPreVertex *b = &work->verts[p->first + k];
            /* place (0-3), colour (4-6) and alpha (9); the texture coordinates (7, 8) are the newer's */
            if (p->flags & KHDAYS_G3D_MOD) {
                float fa[10], fb[10];
                memcpy(fa, a->word, sizeof(fa));
                memcpy(fb, b->word, sizeof(fb));
                for (int w = 0; w < 10; ++w) {
                    if (w != 7 && w != 8) {
                        fb[w] = (float)((double)fa[w] + ((double)fb[w] - (double)fa[w]) * t);
                    }
                }
                memcpy(b->word, fb, sizeof(fb));
            } else {
                for (int w = 0; w < 10; ++w) {
                    if (w != 7 && w != 8) {
                        b->word[w] = between_fixed(a->word[w], b->word[w], t);
                    }
                }
            }
        }
    }
    return khdays_g3d_finish(newer, work->verts, out);
}

void khdays_g3d_interp_free(KhdaysG3dInterp *work)
{
    free(work->verts);
    free(work->slots);
    free(work->groups);
    free(work->group_of);
    memset(work, 0, sizeof(*work));
}
