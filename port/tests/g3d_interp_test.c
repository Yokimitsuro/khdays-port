/* A picture between two 3D lists (khdays_g3d_interpolate): a model drawn twice is matched to its nearest
 * drawing whatever their order, a selection marker and a polygon that jumped are where the newer list has
 * them, the texture coordinates are the newer's, and a polygon that moved a little is placed between.
 *   ctest --test-dir build/port/obj-x64 -R khdays-g3d-interp --output-on-failure */
#include "../gfx/g3d_interp.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

#define ONE 4096 /* w: the view's -1..1 is x from -4096 to 4096 */
#define POLYGONS 8

typedef struct {
    KhdaysG3dList list;
    KhdaysG3dPolygon pre[POLYGONS];
    KhdaysG3dPreInfo info[POLYGONS];
    KhdaysG3dPreVertex verts[POLYGONS * 4];
} Lists;

/* A square around view x `x` (-1..1), texture coordinate `s`, of draw `draw`'s drawing `occurrence`. */
static void square(Lists *l, double x, int32_t s, uint64_t draw, uint32_t occurrence, uint32_t ordinal, int32_t flags)
{
    const int32_t i = l->list.npre++;
    KhdaysG3dPolygon *p = &l->pre[i];
    KhdaysG3dPreInfo *info = &l->info[i];
    memset(p, 0, sizeof(*p));
    memset(info, 0, sizeof(*info));
    p->attr = (31u << 16) | 0xc0u;
    p->first = l->list.npre_verts;
    p->count = 4;
    p->flags = flags;
    p->replacement = -1;
    info->draw = draw;
    info->occurrence = occurrence;
    info->ordinal = ordinal;
    info->viewport[2] = 255;
    info->viewport[3] = 191;
    info->widen = ONE;
    for (int k = 0; k < 4; ++k) {
        int32_t *w = l->verts[l->list.npre_verts++].word;
        memset(w, 0, sizeof(int32_t) * 10);
        w[0] = (int32_t)(x * ONE) + ((k & 1) ? 200 : -200);
        w[1] = (k & 2) ? 200 : -200;
        w[3] = ONE;
        w[4] = w[5] = w[6] = 31;
        w[7] = s;
    }
}

static void begin(Lists *l)
{
    memset(&l->list, 0, sizeof(l->list));
    l->list.pre = l->pre;
    l->list.pre_info = l->info;
    l->list.pre_verts = l->verts;
    l->list.pre_capacity = POLYGONS;
    l->list.pre_vert_capacity = POLYGONS * 4;
    l->list.widen = 1.0f;
}

/* A polygon of draw `draw` beside the eye (a room's wall the camera stands at): before projection about
 * x `x`, its w only `w` -- its place on the view far out, and far from one list to the next. */
static void beside_eye(Lists *l, int32_t x, int32_t w, uint64_t draw, uint32_t ordinal)
{
    square(l, 0.0, 0, draw, 0, ordinal, 0);
    for (int k = 0; k < 4; ++k) {
        int32_t *v = l->verts[l->pre[l->list.npre - 1].first + k].word;
        v[0] = x + ((k & 1) ? 200 : -200);
        v[3] = w;
    }
}

/* The picture's first vertex of the newer list's polygon `i`: its x on the view and its s. */
static double x_of(const KhdaysG3dInterp *work, const Lists *newer, int i, int32_t *s)
{
    const int32_t *w = work->verts[newer->pre[i].first].word;
    *s = w[7];
    return (double)(w[0] + 200) / ONE;
}

static int near(double a, double b)
{
    return a - b < 0.01 && b - a < 0.01;
}

int main(void)
{
    static Lists older, newer;
    KhdaysG3dInterp work;
    KhdaysG3dList out;
    int32_t s;
    memset(&work, 0, sizeof(work));
    memset(&out, 0, sizeof(out));
    begin(&older);
    begin(&newer);
    /* a model drawn twice (the field's prizes): in the newer list its drawings come the other way round */
    square(&older, -0.5, 0, 0x1000, 0, 0, 0);
    square(&older, 0.5, 0, 0x1000, 1, 0, 0);
    square(&newer, 0.52, 0, 0x1000, 0, 0, 0);
    square(&newer, -0.48, 0, 0x1000, 1, 0, 0);
    /* a selection marker moved to the next target */
    square(&older, -0.3, 0, 0x2000, 0, 0, KHDAYS_G3D_MARKER);
    square(&newer, 0.3, 0, 0x2000, 0, 0, KHDAYS_G3D_MARKER);
    /* a texture scrolled round: 100 texels, then 4 */
    square(&older, 0.0, 100 * 16, 0x3000, 0, 0, 0);
    square(&newer, 0.02, 4 * 16, 0x3000, 0, 0, 0);
    /* an effect's piece outside a model that jumped across the view (another of its material's) */
    square(&older, -0.8, 0, 0, 7, 0, 0);
    square(&newer, 0.8, 0, 0, 7, 0, 0);
    /* a room drawn once, moving a little as the camera turns, one of its walls beside the eye: the wall's
     * place on the view runs from -83 to -250, the room's middle hardly moves -- drawn between */
    square(&older, 0.0, 0, 0x4000, 0, 0, 0);
    beside_eye(&older, -300, 6, 0x4000, 1);
    square(&newer, 0.1, 0, 0x4000, 0, 0, 0);
    beside_eye(&newer, -300, 2, 0x4000, 1);
    CHECK(khdays_g3d_interpolate(&work, &older.list, &newer.list, 0.5f, &out));
    CHECK(near(x_of(&work, &newer, 0, &s), 0.51)); /* between itself, not the other drawing */
    CHECK(near(x_of(&work, &newer, 1, &s), -0.49));
    CHECK(near(x_of(&work, &newer, 2, &s), 0.3));  /* the marker where it is now */
    CHECK(near(x_of(&work, &newer, 3, &s), 0.01)); /* moved a little: between */
    CHECK(s == 4 * 16);                            /* its texture where it is now */
    CHECK(near(x_of(&work, &newer, 4, &s), 0.8));  /* the jump not drawn between */
    CHECK(work.jumped == 1);
    CHECK(near(x_of(&work, &newer, 5, &s), 0.05)); /* the room between, its wall's far place not a jump */
    /* the pair named (the picture's refreshes between two game frames): matched once, then each alpha
     * placed from the match kept, as a call that matches again places it */
    khdays_g3d_interp_pair(&work, 1, 2);
    CHECK(khdays_g3d_interpolate(&work, &older.list, &newer.list, 0.25f, &out));
    CHECK(near(x_of(&work, &newer, 0, &s), 0.505));
    CHECK(khdays_g3d_interpolate(&work, &older.list, &newer.list, 0.75f, &out)); /* from the kept match */
    CHECK(work.cached && work.jumped == 1);
    CHECK(near(x_of(&work, &newer, 0, &s), 0.515));
    CHECK(near(x_of(&work, &newer, 1, &s), -0.485));
    CHECK(near(x_of(&work, &newer, 2, &s), 0.3));
    CHECK(near(x_of(&work, &newer, 4, &s), 0.8));
    khdays_g3d_interp_pair(&work, 2, 3); /* another pair: matched again */
    CHECK(!work.cached);
    khdays_g3d_list_free(&out);
    khdays_g3d_interp_free(&work);
    if (failures == 0) {
        printf("3D interpolation: drawings matched by place, markers, jumps and texture coordinates as the newer\n");
    }
    return failures != 0;
}
