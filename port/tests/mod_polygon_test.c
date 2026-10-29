/* A mod's polygons into a list (khdays_g3d_mod_polygon): clipped at the near plane with their true
 * depth, sky layers behind everything in painting order, nothing lost at the far plane, vertex alpha
 * kept. The clip space is Days's field's: z = w - 0.2 (near plane at w 0.1, no far plane).
 *   ctest --test-dir build/port/obj-x64 -R khdays-mod-polygon --output-on-failure */
#include "../gfx/g3d.h"

#include <stdio.h>

static int failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* A point of the field's clip space: x and y, and its distance w. */
static KhdaysG3dClipVertex at(float x, float y, float w)
{
    const KhdaysG3dClipVertex v = {x, y, w - 0.2f, w, 31, 31, 31, 0, 0, 1};
    return v;
}

static const uint32_t BOTH_SIDES = (31u << 16) | 0xc0u;

static void sequence_hud_bounds(void)
{
    const KhdaysG3dClipVertex box[4] = {{-0.9f, 1.2f, -0.95f, 1, 63, 63, 63, 0, 0, 1},
                                        {-0.5f, 1.2f, -0.95f, 1, 63, 63, 63, 128, 0, 1},
                                        {-0.5f, 0.7f, -0.95f, 1, 63, 63, 63, 128, 128, 1},
                                        {-0.9f, 0.7f, -0.95f, 1, 63, 63, 63, 0, 128, 1}};
    for (int kind = 0; kind < 3; ++kind) {
        khdays_g3d_reset();
        khdays_g3d_set_hud(kind != 0);
        if (kind == 2)
            khdays_g3d_mod_polygon(box, 4, BOTH_SIDES, 0, 7, 1, 0);
        else
            khdays_g3d_sequence_polygon(box, 4, BOTH_SIDES, 7, 20);
        const uint32_t swap = 0;
        khdays_g3d_command(0x50, &swap);
        khdays_g3d_vblank();
        const KhdaysG3dList *list = khdays_g3d_shown();
        CHECK(list->npolys == 1);
        if (list->npolys == 1)
            CHECK(kind == 1 ? list->polys[0].ymin < -19 : list->polys[0].ymin >= 0);
        KhdaysG3dList replay = {0};
        CHECK(khdays_g3d_finish(list, NULL, &replay));
        CHECK(replay.npolys == 1);
        if (replay.npolys == 1)
            CHECK(kind == 1 ? replay.polys[0].ymin < -19 : replay.polys[0].ymin >= 0);
        khdays_g3d_list_free(&replay);
    }
}

static void native_triangle(unsigned alpha, unsigned id)
{
    const uint32_t attr = (alpha << 16) | (id << 24) | 0xc0u, begin = 0;
    const uint32_t vertices[3][2] = {{0xf800f800u, 0}, {0xf8000800u, 0}, {0x08000000u, 0}};
    khdays_g3d_command(0x29, &attr);
    khdays_g3d_command(0x40, &begin);
    for (int i = 0; i < 3; ++i)
        khdays_g3d_command(0x23, vertices[i]);
    khdays_g3d_command(0x41, NULL);
}

static void marker_order(void)
{
    khdays_g3d_reset();
    khdays_g3d_set_marker(1);
    native_triangle(31, 1);
    khdays_g3d_set_marker(0);
    native_triangle(16, 2);
    khdays_g3d_set_marker(1);
    const KhdaysG3dClipVertex v[3] = {at(-5, -5, 10), at(5, -5, 10), at(0, 5, 10)};
    khdays_g3d_sequence_polygon(v, 3, BOTH_SIDES | (5u << 24), 7, 19);
    khdays_g3d_set_marker(0);
    native_triangle(31, 3);
    const uint32_t swap = 0;
    khdays_g3d_command(0x50, &swap);
    khdays_g3d_vblank();
    const KhdaysG3dList *shown = khdays_g3d_shown();
    CHECK(shown->npolys == 4);
    for (unsigned sort = 0; sort < 2; ++sort) {
        KhdaysG3dList seed = *shown, replay = {0};
        seed.swap_params = sort;
        CHECK(khdays_g3d_finish(&seed, NULL, &replay));
        CHECK(replay.npolys == 4);
        for (int i = 0; i < replay.npolys; ++i) {
            const KhdaysG3dPolygon *p = &replay.polys[i];
            CHECK(((p->flags & KHDAYS_G3D_MARKER) != 0) == (i >= 2));
            CHECK(!(p->flags & (KHDAYS_G3D_HUD | KHDAYS_G3D_OVER_2D)));
        }
        CHECK(((replay.polys[2].attr >> 24) & 63u) == 1);
        CHECK(replay.polys[3].pltt == 19);
        khdays_g3d_list_free(&replay);
    }
}

/* The polygon `n` of the list shown: its nearest and farthest depth (z / w). */
static int depths(const KhdaysG3dList *l, int n, float *nearest, float *farthest)
{
    if (n >= l->npolys) {
        return 0;
    }
    const KhdaysG3dPolygon *p = &l->polys[n];
    *nearest = 2, *farthest = -2;
    for (int i = 0; i < p->count; ++i) {
        const float z = l->verts[p->first + i].z;
        *nearest = z < *nearest ? z : *nearest;
        *farthest = z > *farthest ? z : *farthest;
    }
    return 1;
}

int main(void)
{
    khdays_g3d_reset();
    /* 0: a sky's triangle from in front of the camera to behind it (KH2's dome around the room): set
     * behind before the cut, its cut corner came out at the near plane and hid the room */
    const KhdaysG3dClipVertex around[3] = {at(-30, -10, 50), at(30, -10, 50), at(0, -10, -60)};
    khdays_g3d_mod_polygon(around, 3, BOTH_SIDES, 0, -1, 0, 1);
    /* 1 and 2: the dome, then the clouds painted after it though farther: the clouds stay in front */
    const KhdaysG3dClipVertex dome[3] = {at(-100, 50, 300), at(100, 50, 300), at(0, 150, 300)};
    const KhdaysG3dClipVertex clouds[3] = {at(-200, 100, 600), at(200, 100, 600), at(0, 300, 600)};
    khdays_g3d_mod_polygon(dome, 3, BOTH_SIDES, 0, -1, 0, 1);
    khdays_g3d_mod_polygon(clouds, 3, BOTH_SIDES, 0, -1, 0, 2);
    /* 3: a room's wall far away, beyond the sky's distance: in front of the sky all the same */
    const KhdaysG3dClipVertex wall[3] = {at(-500, -500, 1000), at(500, -500, 1000), at(0, 500, 1000)};
    khdays_g3d_mod_polygon(wall, 3, BOTH_SIDES, 0, -1, 0, 0);
    /* 4: a room's floor reaching past a nearer far plane (z > w at one corner): kept, that corner at the
     * far plane's depth -- the DS's rule had hidden it whole */
    const KhdaysG3dClipVertex floor_[3] = {
        {-5, -5, 3, 5, 31, 31, 31, 0, 0, 1}, {5, -5, 3, 5, 31, 31, 31, 0, 0, 1}, {0, -5, 40, 30, 31, 31, 31, 0, 0, 1}};
    khdays_g3d_mod_polygon(floor_, 3, BOTH_SIDES, 0, -1, 0, 0);
    /* 5: a glow fading to its edge (KH2's sun): each vertex's alpha kept, and a corner cut by the left of
     * the view takes the alpha between its two (1 to 0, cut at four tenths: 0.6) */
    KhdaysG3dClipVertex glow[3] = {at(-20, 0, 10), at(5, 0, 10), at(5, 5, 10)};
    glow[1].a = glow[2].a = 0;
    khdays_g3d_mod_polygon(glow, 3, BOTH_SIDES, 0, -1, 1, 0);
    const uint32_t swap = 0;
    khdays_g3d_command(0x50, &swap);
    khdays_g3d_vblank();
    const KhdaysG3dList *l = khdays_g3d_shown();
    CHECK(l != NULL && l->npolys == 6);
    if (l == NULL) {
        return 1;
    }
    float near0 = 0, far0 = 0, near1 = 0, far1 = 0, near2 = 0, far2 = 0, near3 = 0, far3 = 0, near4 = 0, far4 = 0;
    CHECK(depths(l, 0, &near0, &far0) && near0 >= 0.99989f && far0 <= 0.99996f);
    CHECK(depths(l, 1, &near1, &far1) && depths(l, 2, &near2, &far2) && far2 < near1);
    CHECK(near1 >= 0.99989f && far1 <= 0.99996f && near2 >= 0.99989f);
    CHECK(depths(l, 3, &near3, &far3) && far3 < 0.9999f && far3 < near2);
    CHECK(depths(l, 4, &near4, &far4) && far4 <= 0.99991f && near4 < 0.7f);
    if (l->npolys == 6) {
        const KhdaysG3dPolygon *p = &l->polys[5]; /* translucent: drawn last */
        int cut = 0, clear = 0, whole = 0;
        for (int i = 0; i < p->count; ++i) {
            const float a = l->verts[p->first + i].a;
            cut += a > 0.59f && a < 0.61f;
            clear += a == 0.0f;
            whole += a == 1.0f;
        }
        CHECK(cut == 2 && clear == 2 && whole == 0);
    }
    khdays_g3d_reset();
    khdays_g3d_sequence_polygon(glow, 3, BOTH_SIDES, 7, 19);
    khdays_g3d_command(0x50, &swap);
    khdays_g3d_vblank();
    l = khdays_g3d_shown();
    CHECK(l->npolys == 1 && (l->polys[0].flags & KHDAYS_G3D_SEQUENCE));
    CHECK(l->polys[0].pltt == 19 && l->polys[0].replacement == 7);
    KhdaysG3dList replay = {0};
    CHECK(khdays_g3d_finish(l, NULL, &replay));
    CHECK(replay.npolys == 1 && replay.polys[0].pltt == 19 && (replay.polys[0].flags & KHDAYS_G3D_SEQUENCE));
    khdays_g3d_list_free(&replay);
    marker_order();
    sequence_hud_bounds();
    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    printf("mod polygons: all checks passed\n");
    return 0;
}
