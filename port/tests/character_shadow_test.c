/* Synthetic native and replacement poses projected onto a sloping receiver. */
#include "../gfx/character_shadow.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failed;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                                 \
            ++failed;                                                                                                  \
        }                                                                                                              \
    } while (0)
static const int32_t projection[16] = {4096, 0, 0, 0, 0, 2048, -819, 0, 0, 2048, 819, 0, 0, 0, 0, 4096};
static const int32_t camera[12] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096, 0, 0, 0};

static void triangle(const float xyz[9], uint32_t attr, int native)
{
    KhdaysG3dClipVertex v[3] = {0};
    for (int i = 0; i < 3; ++i) {
        const float x = xyz[i * 3], y = xyz[i * 3 + 1], z = xyz[i * 3 + 2];
        v[i].x = x;
        v[i].y = (y + z) * 0.5f;
        v[i].z = (z - y) * 819.0f / 4096.0f;
        v[i].w = 1;
        v[i].r = 12;
        v[i].g = 8;
        v[i].b = 4;
        v[i].a = 1;
    }
    if (!native) {
        khdays_g3d_mod_polygon(v, 3, attr, 0, -1, ((attr >> 16) & 31) < 31, 0);
        return;
    }
    uint32_t p = attr;
    khdays_g3d_command(0x29, &p);
    p = 0;
    khdays_g3d_command(0x40, &p);
    for (int i = 0; i < 3; ++i) {
        uint32_t words[2] = {(uint16_t)(int16_t)(v[i].x * 4096) | ((uint32_t)(uint16_t)(int16_t)(v[i].y * 4096) << 16),
                             (uint16_t)(int16_t)(v[i].z * 4096)};
        khdays_g3d_command(0x23, words);
    }
    khdays_g3d_command(0x41, NULL);
}

static void run(int on, int native, int floor_present, int caster_present)
{
    const float floor[9] = {-.9f, 0, -.9f, .9f, 0, -.9f, 0, 0, .9f};
    const float volume[9] = {-.5f, -.1f, -.5f, .5f, .1f, -.5f, 0, .1f, .5f};
    const float pose[9] = {-.2f, .2f, -.2f, .2f, .3f, -.2f, 0, .5f, .2f};
    khdays_g3d_reset();
    khdays_g3d_frame_camera(projection, camera);
    khdays_shadow_capture_begin(9, on);
    if (floor_present)
        triangle(floor, 0xc0u | ((floor_present == 2 ? 30u : 31u) << 16) | (2u << 24), native);
    khdays_shadow_capture_end();
    khdays_shadow_volume_begin(1, on);
    triangle(volume, 0xf0u | (10u << 16) | (63u << 24), native);
    khdays_shadow_volume_end();
    khdays_shadow_capture_begin(1, on);
    if (caster_present)
        triangle(pose, 0xc0u | (31u << 16) | (1u << 24), native);
    khdays_shadow_capture_end();
    uint32_t zero = 0;
    khdays_g3d_command(0x50, &zero);
    khdays_g3d_vblank();
    const KhdaysG3dList *list = khdays_g3d_shown();
    int sources = 0, receivers = 0, replaced = 0;
    for (int i = 0; i < list->npre; ++i) {
        const KhdaysG3dPolygon *p = &list->pre[i];
        sources += !!(p->flags & KHDAYS_G3D_SHADOW_SOURCE);
        receivers += !!(p->flags & KHDAYS_G3D_SHADOW_RECEIVER);
        replaced += !!(p->flags & KHDAYS_G3D_SHADOW_REPLACED);
        if (p->flags & KHDAYS_G3D_SHADOW_RECEIVER) {
            CHECK(((p->attr >> 16) & 31) == 10 && p->pltt == 2);
            CHECK((p->attr & 0x30) == 0);
            for (int k = 0; k < p->count; ++k) {
                KhdaysG3dClipVertex v;
                memcpy(&v, &list->pre_verts[p->first + k], sizeof(v));
                const float world_z = v.y + v.z * (4096.0f / (2 * 819.0f));
                CHECK(fabsf(v.s / 16 - (v.x + .218f) / .436f * 128) < .5f);
                CHECK(fabsf(v.t / 16 - (world_z + .218f) / .436f * 128) < .5f);
            }
        }
    }
    const int enhanced = on && floor_present == 1 && caster_present;
    CHECK((sources > 0) == enhanced);
    CHECK((receivers > 0) == enhanced);
    CHECK((replaced > 0) == enhanced);
    for (int i = 0; i < list->nverts; ++i)
        CHECK(isfinite(list->verts[i].x) && isfinite(list->verts[i].y));
    KhdaysG3dList rebuilt = {0};
    CHECK(khdays_g3d_finish(list, NULL, &rebuilt));
    CHECK(rebuilt.npolys == list->npolys);
    khdays_g3d_list_free(&rebuilt);
}

static void sprite_and_weapon(int on, int floor_present, int sprite_present, int native)
{
    const float floor[9] = {-.9f, 0, -.9f, .9f, 0, -.9f, 0, 0, .9f};
    const float pose[9] = {-.2f, .2f, -.2f, .2f, .3f, -.2f, 0, .5f, .2f};
    const float weapon[9] = {.3f, .3f, -.2f, .55f, .4f, 0, .3f, .3f, .2f};
    const float flat[9] = {-.7f, .05f, -.7f, .7f, .05f, -.7f, 0, .05f, .7f};
    khdays_g3d_reset();
    khdays_g3d_frame_camera(projection, camera);
    khdays_shadow_capture_begin(9, on);
    if (floor_present)
        triangle(floor, 0xc0u | (31u << 16) | (2u << 24), native);
    khdays_shadow_capture_end();
    khdays_shadow_capture_begin(1, on);
    triangle(pose, 0xc0u | (31u << 16) | (1u << 24), native);
    if (sprite_present) {
        if (on)
            khdays_shadow_sprite_begin();
        CHECK(khdays_shadow_sprite_active() == on);
        triangle(flat, 0xc0u | (12u << 16) | (1u << 24), native);
        if (on)
            khdays_shadow_volume_end();
        CHECK(khdays_shadow_has_sprite() == on);
    }
    khdays_shadow_capture_end();
    // An independently drawn attachment is attributed to its owner, as the runtime hooks do.
    khdays_shadow_capture_begin(1, on);
    triangle(weapon, 0xc0u | (31u << 16) | (1u << 24), !native);
    khdays_shadow_capture_end();
    uint32_t zero = 0;
    khdays_g3d_command(0x50, &zero);
    khdays_g3d_vblank();
    const KhdaysG3dList *list = khdays_g3d_shown();
    int sources = 0, receivers = 0, replaced = 0;
    const int enhanced = on && floor_present && sprite_present;
    for (int i = 0; i < list->npre; ++i) {
        const KhdaysG3dPolygon *p = &list->pre[i];
        sources += !!(p->flags & KHDAYS_G3D_SHADOW_SOURCE);
        replaced += !!(p->flags & KHDAYS_G3D_SHADOW_REPLACED);
        if (!(p->flags & KHDAYS_G3D_SHADOW_RECEIVER))
            continue;
        ++receivers;
        CHECK(p->flags & KHDAYS_G3D_SHADOW_SPRITE);
        CHECK(((p->flags >> 18) & 63) == 2);
        CHECK(((p->attr >> 16) & 31) == 12);
        for (int k = 0; k < p->count; ++k) {
            KhdaysG3dClipVertex v;
            memcpy(&v, &list->pre_verts[p->first + k], sizeof(v));
            // The flat oval must not inflate the footprint; the held weapon must extend it.
            CHECK(v.x < .59f && v.x > -.24f);
        }
    }
    CHECK(sources == (enhanced ? 2 : 0));
    CHECK((receivers > 0) == enhanced);
    CHECK(replaced == enhanced);
}

/* More casters than the 32 masks there once were: each one its own mask and its own receiver, none left native. */
static void many(int native)
{
    enum { CASTERS = 40 };
    const float floor[9] = {-.9f, 0, -.9f, .9f, 0, -.9f, 0, 0, .9f};
    const float volume[9] = {-.5f, -.1f, -.5f, .5f, .1f, -.5f, 0, .1f, .5f};
    const float pose[9] = {-.2f, .2f, -.2f, .2f, .3f, -.2f, 0, .5f, .2f};
    khdays_g3d_reset();
    khdays_g3d_frame_camera(projection, camera);
    khdays_shadow_capture_begin(9, 1);
    triangle(floor, 0xc0u | (31u << 16) | (2u << 24), native);
    khdays_shadow_capture_end();
    for (int c = 0; c < CASTERS; ++c) {
        khdays_shadow_volume_begin(100 + (uintptr_t)c, 1);
        triangle(volume, 0xf0u | (10u << 16) | (63u << 24), native);
        khdays_shadow_volume_end();
        khdays_shadow_capture_begin(100 + (uintptr_t)c, 1);
        triangle(pose, 0xc0u | (31u << 16) | (1u << 24), native);
        khdays_shadow_capture_end();
    }
    uint32_t zero = 0;
    khdays_g3d_command(0x50, &zero);
    khdays_g3d_vblank();
    const KhdaysG3dList *list = khdays_g3d_shown();
    int source[CASTERS] = {0}, receiver[CASTERS] = {0}, replaced = 0;
    for (int i = 0; i < list->npre; ++i) {
        const KhdaysG3dPolygon *p = &list->pre[i];
        replaced += !!(p->flags & KHDAYS_G3D_SHADOW_REPLACED);
        if (!(p->flags & (KHDAYS_G3D_SHADOW_SOURCE | KHDAYS_G3D_SHADOW_RECEIVER)))
            continue;
        CHECK(p->shadow >= 0 && p->shadow < CASTERS);
        if (p->shadow < 0 || p->shadow >= CASTERS)
            continue;
        if (p->flags & KHDAYS_G3D_SHADOW_SOURCE)
            ++source[p->shadow];
        else
            ++receiver[p->shadow];
    }
    for (int c = 0; c < CASTERS; ++c)
        CHECK(source[c] > 0 && receiver[c] > 0);
    CHECK(replaced == CASTERS);
}

/* The game's flat shadow drawn at its caster's height in the air (ov107's when its floor ray finds no plain floor):
 * the floor beneath, within 4 units, takes the shadow; none further down */
static void sprite_over_floor(float floor_y, int expected)
{
    const float floor[9] = {-.9f, floor_y, -.9f, .9f, floor_y, -.9f, 0, floor_y, .9f};
    const float pose[9] = {-.2f, .2f, -.2f, .2f, .3f, -.2f, 0, .5f, .2f};
    const float flat[9] = {-.7f, .05f, -.7f, .7f, .05f, -.7f, 0, .05f, .7f};
    khdays_g3d_reset();
    khdays_g3d_frame_camera(projection, camera);
    khdays_shadow_capture_begin(9, 1);
    triangle(floor, 0xc0u | (31u << 16) | (2u << 24), 0);
    khdays_shadow_capture_end();
    khdays_shadow_capture_begin(1, 1);
    triangle(pose, 0xc0u | (31u << 16) | (1u << 24), 0);
    khdays_shadow_sprite_begin();
    triangle(flat, 0xc0u | (12u << 16) | (1u << 24), 0);
    khdays_shadow_volume_end();
    khdays_shadow_capture_end();
    uint32_t zero = 0;
    khdays_g3d_command(0x50, &zero);
    khdays_g3d_vblank();
    const KhdaysG3dList *list = khdays_g3d_shown();
    int receivers = 0, replaced = 0;
    for (int i = 0; i < list->npre; ++i) {
        const KhdaysG3dPolygon *p = &list->pre[i];
        replaced += !!(p->flags & KHDAYS_G3D_SHADOW_REPLACED);
        if (!(p->flags & KHDAYS_G3D_SHADOW_RECEIVER))
            continue;
        ++receivers;
        for (int k = 0; k < p->count; ++k) { /* on the floor: its world y (y - z, as triangle() sends it) */
            KhdaysG3dClipVertex v;
            memcpy(&v, &list->pre_verts[p->first + k], sizeof(v));
            const float world_z = v.y + v.z * (4096.0f / (2 * 819.0f)), world_y = 2 * v.y - world_z;
            CHECK(fabsf(world_y - floor_y) < .01f);
        }
    }
    CHECK((receivers > 0) == expected);
    CHECK((replaced > 0) == expected);
}

int main(void)
{
    many(0);
    many(1);
    sprite_over_floor(-.9f, 1); /* 0.95 under the shadow, past its band (0.7): the floor beneath takes it */
    sprite_over_floor(0.0f, 1); /* in its band, as before */
    for (int native = 0; native <= 1; ++native) {
        run(0, native, 1, 1);
        run(1, native, 1, 1);
        run(1, native, 0, 1);
        run(1, native, 1, 0);
        run(1, native, 2, 1);
        sprite_and_weapon(1, 1, 1, native);
        sprite_and_weapon(0, 1, 1, native);
        sprite_and_weapon(1, 0, 1, native);
        sprite_and_weapon(1, 1, 0, native);
    }
    return failed ? 1 : 0;
}
