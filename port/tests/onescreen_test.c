/* The one-screen picture on the CPU: the compositions' places by the port's rule, the port's own file,
 * the pass's steps on fixed texels, and kd_split giving today's pixel when nothing moves.
 *   ctest --test-dir build/port/obj-x64 -R khdays-onescreen --output-on-failure */
#include "../gfx/describe.h"
#include "../ui/composition.h"
#include "../ui/hud_options.h"
#include "../host/render/png_io.h"
#include "../host/render/onescreen_images.h"
#include "../runtime/display/frame_package.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int outside_pieces, inside_pieces; /* test_inside's */
static int overlaps;                      /* test_apart's */

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                                    \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* --- What composition.c needs of the runtime, stood in for -------------------------- */

/* the compositions compiled in, here the test's own: a shape at each kind of place (test_places) */
const unsigned char khdays_compositions_port[] =
    "units port\n"
    "[field]\n"
    "screens upper\n"
    "shape upper 0 0 100 50 at top-left margin 4 6 0 0\n"
    "shape upper 0 0 100 50 at bottom-right margin 0 0 5 7 scale 0.5\n"
    "shape lower 10 20 64 32 at centre scale fixed(2)\n"
    "shape lower 0 0 40 20 at top margin 0 2+fixed(3) 0 0 scale 1+fixed(0.5)\n"
    "shape upper at right margin 0 0 fixed(4) 0 scale cover\n"
    "shape upper at bottom scale 1 at-most fixed(1)\n"
    "shape3d hud 0 0 128 64 at top-left margin 3 2 0 0 scale 0.5\n"
    "shape3d all 0 0 256 192 at centre scale fixed(1)\n"
    "[pause]\n"
    "menu\n"
    "[title if wide]\n"
    "screens side-by-side\n"
    "[title if narrow]\n"
    "screens stacked\n"
    "[title]\n"
    "screens lower\n"
    "bogus line\n";
const unsigned int khdays_compositions_port_size = sizeof(khdays_compositions_port) - 1;

/* The port's own file (CMake's KHDAYS_PORT_COMPOSITIONS_FILE), given as a mod's once a test asks
 * for it; the lines the reader ignores, counted. */
static const char *mods_file;
static int ignored_lines, read_ahead_lines;
/* the title's pictures a mod gives (test_title_backdrop): its atlas or picture (a .png), its
 * root level's animated picture, its story level's and its mission level's; the last animated
 * picture asked for */
static const char *atlas_file, *root_file, *backdrop_file, *mission_file;
static char asked[256];

int khdays_mods_find(const char *relative, char *path, int size)
{
    const char *file = mods_file;
    if (strncmp(relative, "layouts/title/", 14) == 0) {
        const size_t n = strlen(relative);
        if (n > 4 && strcmp(relative + n - 4, ".png") == 0) {
            file = atlas_file;
        } else {
            snprintf(asked, sizeof(asked), "%s", relative);
            file = strstr(relative, "/root_") != NULL      ? root_file
                   : strstr(relative, "/story_") != NULL   ? backdrop_file
                   : strstr(relative, "/mission_") != NULL ? mission_file
                                                           : NULL;
        }
    }
    if (file == NULL || (int)strlen(file) >= size) {
        return 0;
    }
    memcpy(path, file, strlen(file) + 1);
    return 1;
}

long khdays_host_read_file(const char *path, void *data, size_t size)
{
    FILE *f = fopen(path, "rb");
    long got;
    if (f == NULL) {
        return -1;
    }
    got = (long)fread(data, 1, size, f);
    fclose(f);
    return got;
}

void khdays_log(const char *format, ...)
{
    ignored_lines += strstr(format, "is ignored") != NULL;
    read_ahead_lines += strstr(format, "(read ahead)") != NULL;
}

/* --- The places, by the port's rule ------------------------------------------------- */

static const char *test_file(const char *suffix, const char *text);

static float word_float(const uint32_t *w, int at)
{
    float f;
    memcpy(&f, &w[at], sizeof(f));
    return f;
}

/* A shape's rectangle in the C x 192 picture and its scale (a DS dot to picture dots). */
static void check_place(const KhdaysShape *s, float x1, float y1, float w, float h, float scale)
{
    const float px1 = word_float(s->words, KO_DST_X1), py1 = word_float(s->words, KO_DST_Y1);
    const float px2 = word_float(s->words, KO_DST_X2), py2 = word_float(s->words, KO_DST_Y2);
    const int near = fabsf(px1 - x1) < 0.001f && fabsf(py1 - y1) < 0.001f && fabsf(px2 - (x1 + w)) < 0.001f &&
                     fabsf(py2 - (y1 + h)) < 0.001f && fabsf(word_float(s->words, KO_SCALE_X) - scale) < 0.0001f;
    if (!near) {
        fprintf(stderr, "  %.3f,%.3f-%.3f,%.3f x%.4f, expected %.3f,%.3f-%.3f,%.3f x%.4f\n", px1, py1, px2, py2,
                word_float(s->words, KO_SCALE_X), x1, y1, x1 + w, y1 + h, scale);
    }
    CHECK(near);
}

/* The test's compositions at 4:3, 16:10 and 16:9 and HUD sizes 1, 4 and 8: lengths, `fixed`, scales,
 * corners and margins, `cover`, `at-most`, the 3D shapes, `menu` and the common rule, `wide` and
 * `narrow`. */
static void test_places(void)
{
    static const int columns[] = {256, 308, 342};
    static const int sizes[] = {1, 4, 8};
    static const float factors[] = {0.6f, 1.0f, 1.75f};
    KhdaysSituation field = {KHDAYS_SIT_FIELD, 0}, pause = {KHDAYS_SIT_PAUSE, 0}, title = {KHDAYS_SIT_TITLE, 0};
    KhdaysComposition comp;
    mods_file = NULL;
    ignored_lines = 0;
    khdays_composition_load();
    CHECK(ignored_lines == 1); /* the bogus line */
    for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); ++z) {
        const float f = factors[z];
        khdays_composition_set_hud_size(sizes[z]);
        CHECK(fabsf(khdays_composition_hud_factor() - f) < 0.0001f);
        for (size_t n = 0; n < sizeof(columns) / sizeof(columns[0]); ++n) {
            const float c = (float)columns[n];
            khdays_composition_for(&field, columns[n], &comp);
            CHECK(comp.screens == KHDAYS_SCREENS_UPPER);
            CHECK(comp.shape_count == 6 && comp.shape3d_count == 2);
            if (comp.shape_count != 6 || comp.shape3d_count != 2) {
                continue;
            }
            check_place(&comp.shapes[0], 4.0f * f, 6.0f * f, 100.0f * f, 50.0f * f, f);
            check_place(&comp.shapes[1], c - 50.0f * f - 5.0f * f, 192.0f - 25.0f * f - 7.0f * f, 50.0f * f, 25.0f * f,
                        0.5f * f);
            check_place(&comp.shapes[2], (c - 128.0f) / 2.0f, 64.0f, 128.0f, 64.0f, 2.0f);
            check_place(&comp.shapes[3], (c - 40.0f * (f + 0.5f)) / 2.0f, 2.0f * f + 3.0f, 40.0f * (f + 0.5f),
                        20.0f * (f + 0.5f), f + 0.5f);
            check_place(&comp.shapes[4], -4.0f, (192.0f - 192.0f * c / 256.0f) / 2.0f, c, 192.0f * c / 256.0f,
                        c / 256.0f);
            {
                /* `at-most fixed(1)`: the HUD's size up to the DS's, at the bottom */
                const float s = f < 1.0f ? f : 1.0f;
                check_place(&comp.shapes[5], (c - 256.0f * s) / 2.0f, 192.0f - 192.0f * s, 256.0f * s, 192.0f * s, s);
            }
            CHECK((comp.shapes[2].words[KO_FLAGS] & KOF_LOWER) != 0u &&
                  (comp.shapes[0].words[KO_FLAGS] & KOF_LOWER) == 0u);
            CHECK(word_float(comp.shapes[2].words, KO_SRC_X) == 10.0f &&
                  word_float(comp.shapes[2].words, KO_SRC_H) == 32.0f);
            CHECK(comp.shapes3d[0].group == KHDAYS_SHAPE3D_HUD && fabsf(comp.shapes3d[0].scale - 0.5f * f) < 0.0001f);
            CHECK(fabsf(comp.shapes3d[0].x - 3.0f * f) < 0.001f && fabsf(comp.shapes3d[0].y - 2.0f * f) < 0.001f);
            CHECK(comp.shapes3d[1].group == KHDAYS_SHAPE3D_ALL && comp.shapes3d[1].scale == 1.0f);
            CHECK(fabsf(comp.shapes3d[1].x - (c - 256.0f) / 2.0f) < 0.001f && comp.shapes3d[1].y == 0.0f);
            /* the common rule: the upper screen alone (content 1, or nothing seen), both side by side */
            for (uint32_t content = 0; content < 4; ++content) {
                pause.content = content;
                khdays_composition_for(&pause, columns[n], &comp);
                if (content == 3u) {
                    CHECK(comp.screens == KHDAYS_SCREENS_SIDE_BY_SIDE && comp.shape_count == 0);
                    continue;
                }
                CHECK(comp.screens == (content == 2u ? KHDAYS_SCREENS_LOWER : KHDAYS_SCREENS_UPPER));
                CHECK(comp.shape_count == 1);
                if (comp.shape_count == 1) {
                    check_place(&comp.shapes[0], (c - 256.0f) / 2.0f, 0.0f, 256.0f, 192.0f, 1.0f);
                    CHECK(((comp.shapes[0].words[KO_FLAGS] & KOF_LOWER) != 0u) == (content == 2u));
                }
            }
            /* a situation no section names: the same rule */
            {
                KhdaysSituation shop = {KHDAYS_SIT_SHOP, 0};
                shop.content = 2u;
                khdays_composition_for(&shop, columns[n], &comp);
                CHECK(comp.screens == KHDAYS_SCREENS_LOWER && comp.shape_count == 1);
            }
            khdays_composition_for(&title, columns[n], &comp);
            CHECK(comp.screens == (columns[n] == 342   ? KHDAYS_SCREENS_SIDE_BY_SIDE
                                   : columns[n] == 256 ? KHDAYS_SCREENS_STACKED
                                                       : KHDAYS_SCREENS_LOWER));
        }
    }
    khdays_composition_set_hud_size(4);
    /* a mod's file not in the port's units: refused, the port's own read */
    mods_file = test_file("old.txt", "# a file\n[field]\nscreens lower\n");
    khdays_composition_load();
    khdays_composition_for(&field, 342, &comp);
    CHECK(comp.screens == KHDAYS_SCREENS_UPPER && comp.shape_count == 6);
    remove(mods_file);
    mods_file = test_file("new.txt", "  # a file\n\nunits port   # its units\n[field]\nscreens lower\n");
    khdays_composition_load();
    khdays_composition_for(&field, 342, &comp);
    CHECK(comp.screens == KHDAYS_SCREENS_LOWER && comp.shape_count == 0);
    remove(mods_file);
    mods_file = NULL;
    khdays_composition_load();
}

/* The port's own compositions: every line read; the game over's orb with its lines; the credits;
 * the countdown and the lock-on at the top in the middle whether the mission's information shows
 * or not; the party's rows -- at every HUD size. */
static void test_real_file(void)
{
    static const int sizes[] = {1, 4, 8};
    const uint32_t hud = 1u << KHDAYS_ST_HUD, info = 1u << KHDAYS_ST_INFO_LOWER;
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    ignored_lines = 0;
    khdays_composition_load();
    CHECK(ignored_lines == 0);
    for (size_t z = 0; z < sizeof(sizes) / sizeof(sizes[0]); ++z) {
        float with = -1.0f, without = -1.0f, factor;
        khdays_composition_set_hud_size(sizes[z]);
        {
            /* the game over's orb (its 3D shape) goes with its lines (the whole upper screen at the
             * DS's size): the same place and scale */
            KhdaysSituation over = {KHDAYS_SIT_GAME_OVER, 0};
            KhdaysComposition comp;
            khdays_composition_for(&over, 342, &comp);
            CHECK(comp.shape_count == 1 && comp.shape3d_count == 1);
            if (comp.shape_count == 1 && comp.shape3d_count == 1) {
                CHECK(fabsf(comp.shapes3d[0].scale - word_float(comp.shapes[0].words, KO_SCALE_X)) < 0.001f);
                CHECK(fabsf(comp.shapes3d[0].x - word_float(comp.shapes[0].words, KO_DST_X1)) < 0.01f);
                CHECK(fabsf(comp.shapes3d[0].y - word_float(comp.shapes[0].words, KO_DST_Y1)) < 0.01f);
            }
        }
        {
            /* the pause: its menu, the lower screen's INFORMATION, map and panel -- but in an
             * event's pause the menu alone */
            KhdaysSituation pause = {KHDAYS_SIT_PAUSE, 0}, event = {KHDAYS_SIT_PAUSE, 1u << KHDAYS_ST_EVENT};
            KhdaysComposition comp;
            khdays_composition_for(&pause, 342, &comp);
            CHECK(comp.shape_count == 4);
            khdays_composition_for(&event, 342, &comp);
            CHECK(comp.shape_count == 1);
        }
        {
            /* the credits: the two screens side by side, each with its brightness */
            KhdaysSituation end = {KHDAYS_SIT_THE_END, 0};
            KhdaysComposition comp;
            khdays_composition_for(&end, 342, &comp);
            CHECK(comp.screens == KHDAYS_SCREENS_SIDE_BY_SIDE && comp.brightness == (int)KOB_HALVES &&
                  comp.shape_count == 0);
        }
        {
            /* Sora's memories: in an event the lower screen alone in the picture's place; in a battle the
             * two screens as one, joined in the middle -- the upper's world enlarged to the picture's
             * width as its screen, the lower's right half last (under the HUD's shapes) on the right */
            static const int widths[] = {256, 308, 342, 512};
            const uint32_t shown = 1u << KHDAYS_ST_LOWER_SHOWN;
            KhdaysSituation event = {KHDAYS_SIT_FIELD_3D_BOTH, shown};
            KhdaysComposition comp;
            khdays_composition_for(&event, 342, &comp);
            CHECK(comp.screens == KHDAYS_SCREENS_LOWER && comp.hide3d_all && comp.shape_count == 1);
            for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
                KhdaysSituation battle = {KHDAYS_SIT_FIELD_3D_BOTH, shown | hud};
                const float c = (float)widths[w], k = c < 342.0f ? c / 256.0f : 342.0f / 256.0f;
                const float top = (192.0f - 192.0f * k) / 2.0f;
                khdays_composition_for(&battle, widths[w], &comp);
                CHECK(comp.screens == KHDAYS_SCREENS_UPPER && comp.brightness == (int)KOB_HALVES);
                CHECK(comp.shape_count >= 2 && comp.shape3d_count >= 1);
                if (comp.shape_count >= 2 && comp.shape3d_count >= 1) {
                    const uint32_t *last = comp.shapes[comp.shape_count - 1].words;
                    const KhdaysShape3d *moved = &comp.shapes3d[comp.shape3d_count - 1];
                    CHECK(fabsf(word_float(last, KO_DST_X1) - c / 2.0f) < 0.01f);
                    CHECK(fabsf(word_float(last, KO_DST_X2) - (c / 2.0f + 128.0f * k)) < 0.01f);
                    CHECK(fabsf(word_float(last, KO_DST_Y1) - top) < 0.01f);
                    CHECK(word_float(last, KO_SRC_X) == 128.0f && word_float(last, KO_SRC_W) == 128.0f);
                    CHECK(moved->world && moved->whole && fabsf(moved->scale - k) < 0.001f);
                    CHECK(moved->src_x == 0.0f && fabsf(moved->x - (c - 256.0f * k) / 2.0f) < 0.01f &&
                          fabsf(moved->y - top) < 0.01f);
                }
            }
        }
        for (int pass = 0; pass < 2; ++pass) {
            KhdaysSituation field = {KHDAYS_SIT_FIELD, pass == 0 ? hud | info : hud};
            KhdaysComposition comp;
            khdays_composition_for(&field, 342, &comp);
            for (int s = 0; s < comp.shape_count; ++s) {
                if (word_float(comp.shapes[s].words, KO_SRC_X) == 93.0f &&
                    word_float(comp.shapes[s].words, KO_SRC_W) == 70.0f) {
                    *(pass == 0 ? &with : &without) = word_float(comp.shapes[s].words, KO_DST_Y1);
                }
            }
        }
        factor = khdays_composition_hud_factor();
        CHECK(fabsf(without - 5.0f * factor) < 0.01f);
        CHECK(fabsf(with - 5.0f * factor) < 0.01f);
        {
            /* the party's rows: from y 84 with up to three members, from
             * y 64 with four, the shape's bottom where it was -- in the field and the field with the
             * 3D on both screens */
            static const int scenes[] = {KHDAYS_SIT_FIELD, KHDAYS_SIT_FIELD_3D_BOTH};
            for (size_t n = 0; n < sizeof(scenes) / sizeof(scenes[0]); ++n) {
                float bottom[2] = {-1.0f, -1.0f}, top[2] = {-1.0f, -1.0f}, src_y[2] = {-1.0f, -1.0f};
                for (int four = 0; four < 2; ++four) {
                    KhdaysSituation field = {scenes[n], four ? hud | (1u << KHDAYS_ST_PARTY_4) : hud};
                    KhdaysComposition comp;
                    int found = 0;
                    khdays_composition_for(&field, 342, &comp);
                    for (int s = 0; s < comp.shape_count; ++s) {
                        const uint32_t *w = comp.shapes[s].words;
                        if (word_float(w, KO_SRC_X) == 128.0f && word_float(w, KO_SRC_W) == 128.0f &&
                            (word_float(w, KO_SRC_Y) == 84.0f || word_float(w, KO_SRC_Y) == 64.0f)) {
                            ++found;
                            src_y[four] = word_float(w, KO_SRC_Y);
                            top[four] = word_float(w, KO_DST_Y1);
                            bottom[four] = word_float(w, KO_DST_Y2);
                            CHECK(word_float(w, KO_SRC_H) == (four ? 127.0f : 107.0f));
                        }
                    }
                    CHECK(found == 1);
                }
                CHECK(src_y[0] == 84.0f && src_y[1] == 64.0f);
                CHECK(fabsf(bottom[0] - bottom[1]) < 0.01f);
                CHECK(top[1] < top[0]);
            }
        }
    }
    khdays_composition_set_hud_size(4);
    mods_file = NULL; /* the real files are only read */
}

/* The field's HUD at the default HUD size as the port's own file lays it out, digested: for each
 * of its states and the 16:9, 16:10 and 4:3 pictures, every shape's rectangles, scale, effects
 * and colours, every 3D shape's place, each length rounded to 1/64 of a dot: a change of the file
 * changes it (the last, the INFORMATION banners as the game draws them, fading out where cut). */
#define FIELD_HUD_REFERENCE 0xf556c443eacec027ull

static uint64_t digest_word(uint64_t d, uint32_t v)
{
    for (int b = 0; b < 4; ++b) {
        d = (d ^ ((v >> (8 * b)) & 0xffu)) * 1099511628211ull;
    }
    return d;
}

static uint64_t digest_length(uint64_t d, float f)
{
    return digest_word(d, (uint32_t)(int32_t)lroundf(f * 64.0f));
}

static uint64_t field_hud_digest(void)
{
    static const int columns[] = {342, 308, 256};
    const uint32_t hud = 1u << KHDAYS_ST_HUD;
    const uint32_t states[] = {
        hud,
        hud | (1u << KHDAYS_ST_INFO_LOWER),
        hud | (1u << KHDAYS_ST_INFO_NEW) | (1u << KHDAYS_ST_INFO_LOWER),
        hud | (1u << KHDAYS_ST_MISSION_PANEL),
        hud | (1u << KHDAYS_ST_MISSION_PANEL) | (1u << KHDAYS_ST_INFO_UPPER),
        hud | (1u << KHDAYS_ST_PARTY_4),
        0,
    };
    static const int scenes[] = {KHDAYS_SIT_FIELD, KHDAYS_SIT_FIELD_3D_BOTH};
    /* a shape's lengths: its source, its place, its fades, its opacity and its scale */
    static const int lengths[] = {KO_SRC_X,    KO_SRC_Y,    KO_SRC_W,   KO_SRC_H,   KO_DST_X1,
                                  KO_DST_Y1,   KO_DST_X2,   KO_DST_Y2,  KO_FADE,    KO_FADE + 1,
                                  KO_FADE + 2, KO_FADE + 3, KO_OPACITY, KO_SCALE_X, KO_SCALE_Y};
    uint64_t d = 1469598103934665603ull;
    khdays_composition_set_hud_size(4);
    for (size_t c = 0; c < sizeof(columns) / sizeof(columns[0]); ++c) {
        for (size_t n = 0; n < sizeof(scenes) / sizeof(scenes[0]); ++n) {
            for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); ++s) {
                KhdaysSituation situation = {scenes[n], states[s]};
                KhdaysComposition comp;
                khdays_composition_for(&situation, columns[c], &comp);
                d = digest_word(d, (uint32_t)comp.screens);
                d = digest_word(d, (uint32_t)comp.brightness);
                d = digest_word(d, (uint32_t)comp.shape_count);
                for (int i = 0; i < comp.shape_count; ++i) {
                    const uint32_t *w = comp.shapes[i].words;
                    if (getenv("KHDAYS_PRINT_SHAPES") != NULL) {
                        printf(
                            "c%d s%d st%x #%d fl%x see%x keys%u src %g %g %g %g dst %g %g %g %g sc %g %g fade %g %g %g %g\n",
                            columns[c], scenes[n], states[s], i, w[KO_FLAGS], w[KO_SEE_THROUGH], w[KO_KEY_COUNT],
                            word_float(w, KO_SRC_X), word_float(w, KO_SRC_Y), word_float(w, KO_SRC_W),
                            word_float(w, KO_SRC_H), word_float(w, KO_DST_X1), word_float(w, KO_DST_Y1),
                            word_float(w, KO_DST_X2), word_float(w, KO_DST_Y2), word_float(w, KO_SCALE_X),
                            word_float(w, KO_SCALE_Y), word_float(w, KO_FADE), word_float(w, KO_FADE + 1),
                            word_float(w, KO_FADE + 2), word_float(w, KO_FADE + 3));
                    }
                    d = digest_word(d, w[KO_FLAGS]);
                    for (size_t k = 0; k < sizeof(lengths) / sizeof(lengths[0]); ++k) {
                        d = digest_length(d, word_float(w, lengths[k]));
                    }
                    d = digest_word(d, w[KO_SEE_THROUGH]);
                    d = digest_word(d, w[KO_KEY_COUNT]);
                    for (uint32_t k = 0; k < w[KO_KEY_COUNT] && w[KO_KEYS] + k < (uint32_t)comp.key_count; ++k) {
                        d = digest_word(d, comp.keys[w[KO_KEYS] + k]);
                    }
                }
                d = digest_word(d, (uint32_t)comp.shape3d_count);
                for (int i = 0; i < comp.shape3d_count; ++i) {
                    const KhdaysShape3d *t = &comp.shapes3d[i];
                    if (getenv("KHDAYS_PRINT_SHAPES") != NULL) {
                        printf("c%d s%d st%x 3d#%d g%d h%d w%d %g %g %g src %g %g %g %g\n", columns[c], scenes[n],
                               states[s], i, t->group, t->hide, t->whole, t->x, t->y, t->scale, t->src_x, t->src_y,
                               t->src_w, t->src_h);
                    }
                    d = digest_word(d, (uint32_t)t->group);
                    d = digest_word(d, (uint32_t)t->hide);
                    d = digest_word(d, (uint32_t)t->whole);
                    if (t->hide) {
                        continue; /* a hidden group's place shows nothing */
                    }
                    d = digest_length(d, t->x);
                    d = digest_length(d, t->y);
                    d = digest_length(d, t->scale);
                    d = digest_length(d, t->src_x);
                    d = digest_length(d, t->src_y);
                    d = digest_length(d, t->src_w);
                    d = digest_length(d, t->src_h);
                }
            }
        }
    }
    return d;
}

/* Every layout of the port's file, at 4:3, 16:10 and 16:9 and at the eight HUD sizes, for each of
 * the states its sections name: each piece inside the picture -- but one that covers the whole
 * picture (`scale cover`) or repeats along it (`repeat`) -- within a hundredth of a dot. */
static void test_inside(void)
{
    static const int columns[] = {256, 308, 342};
    const uint32_t hud = 1u << KHDAYS_ST_HUD;
    const uint32_t states[] = {
        0,
        hud,
        hud | (1u << KHDAYS_ST_PARTY_4),
        hud | (1u << KHDAYS_ST_INFO_NEW) | (1u << KHDAYS_ST_INFO_LOWER),
        hud | (1u << KHDAYS_ST_MISSION_PANEL),
        hud | (1u << KHDAYS_ST_MISSION_PANEL) | (1u << KHDAYS_ST_CHALLENGE),
        (1u << KHDAYS_ST_INFO_UPPER) | (1u << KHDAYS_ST_MISSION_PANEL),
        1u << KHDAYS_ST_DIALOG,
        1u << KHDAYS_ST_TITLE_LIT,
    };
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    for (int size = 1; size <= 8; ++size) {
        khdays_composition_set_hud_size(size);
        for (size_t c = 0; c < sizeof(columns) / sizeof(columns[0]); ++c) {
            const float w = (float)columns[c];
            for (int scene = 0; scene < KHDAYS_SITUATIONS; ++scene) {
                for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); ++s) {
                    KhdaysSituation situation = {scene, states[s]};
                    KhdaysComposition comp;
                    situation.content = 3u;
                    khdays_composition_for(&situation, columns[c], &comp);
                    for (int i = 0; i < comp.shape_count; ++i) {
                        const uint32_t *wd = comp.shapes[i].words;
                        const float x1 = word_float(wd, KO_DST_X1), y1 = word_float(wd, KO_DST_Y1);
                        const float x2 = word_float(wd, KO_DST_X2), y2 = word_float(wd, KO_DST_Y2);
                        const int covers = x1 <= 0.01f && y1 <= 0.01f && x2 >= w - 0.01f && y2 >= 191.99f;
                        const int repeats = (wd[KO_FLAGS] & (KOF_REPEAT_X | KOF_REPEAT_Y)) != 0u;
                        const int inside = x1 >= -0.01f && y1 >= -0.01f && x2 <= w + 0.01f && y2 <= 192.01f;
                        inside_pieces += inside;
                        if (!covers && !repeats && !inside) {
                            fprintf(stderr, "  %s states %x, HUD %d, %d columns: piece %d at %.2f,%.2f-%.2f,%.2f\n",
                                    khdays_situation_name(scene), states[s], size, columns[c], i, x1, y1, x2, y2);
                            ++outside_pieces;
                        }
                    }
                }
            }
        }
    }
    khdays_composition_set_hud_size(4);
    mods_file = NULL;
    khdays_composition_load();
}

/* The field's HUD pieces the layout keeps apart, by their source on the DS's screens. */
typedef struct HudPiece {
    const char *name;
    int lower;
    float x, y, w, h;
} HudPiece;

static const HudPiece hud_pieces[] = {
    {"panel", 1, 4, 152, 248, 40},   {"map", 1, 112, 64, 104, 64},    {"health", 0, 163, 0, 93, 22},
    {"countdown", 0, 93, 0, 70, 20}, {"hearts", 0, 0, 0, 108, 86},    {"sigils", 0, 163, 25, 93, 30},
    {"deck", 0, 0, 84, 112, 108},    {"party", 0, 128, 84, 128, 107},
};
#define HUD_PIECES ((int)(sizeof(hud_pieces) / sizeof(hud_pieces[0])))

/* the pairs that must not overlap: each corner's pieces against the next corner's and the middle */
static const int apart[][2] = {{0, 3}, {3, 2}, {0, 2}, {0, 1}, {4, 6}, {5, 7}, {6, 7}, {1, 7}, {0, 6}};

/* The field's HUD: every piece at every HUD size and aspect; the pieces of each pair above apart
 * where the layout keeps them so -- HUD sizes 1-4 at 16:10 and 16:9, 1-2 at 4:3 (at 4:3 the
 * panel's right end meets the countdown from size 3; from size 5 the corners' pieces meet in
 * every aspect). */
static void test_apart(void)
{
    static const int columns[] = {256, 308, 342};
    const uint32_t states[] = {1u << KHDAYS_ST_HUD | 1u << KHDAYS_ST_MISSION_PANEL,
                               1u << KHDAYS_ST_HUD | 1u << KHDAYS_ST_MISSION_PANEL | 1u << KHDAYS_ST_PARTY_4};
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    for (int size = 1; size <= 8; ++size) {
        khdays_composition_set_hud_size(size);
        for (size_t c = 0; c < sizeof(columns) / sizeof(columns[0]); ++c) {
            for (size_t s = 0; s < sizeof(states) / sizeof(states[0]); ++s) {
                KhdaysSituation field = {KHDAYS_SIT_FIELD, states[s]};
                KhdaysComposition comp;
                float r[HUD_PIECES][4];
                int found[HUD_PIECES] = {0};
                khdays_composition_for(&field, columns[c], &comp);
                for (int i = 0; i < comp.shape_count; ++i) {
                    const uint32_t *w = comp.shapes[i].words;
                    for (int k = 0; k < HUD_PIECES; ++k) {
                        const HudPiece *h = &hud_pieces[k];
                        const float sy = word_float(w, KO_SRC_Y), sh = word_float(w, KO_SRC_H);
                        if (((w[KO_FLAGS] & KOF_LOWER) != 0u) == (h->lower != 0) && word_float(w, KO_SRC_X) == h->x &&
                            sy <= h->y && sy + sh >= h->y + h->h - 20.0f && word_float(w, KO_SRC_W) == h->w &&
                            !found[k]) {
                            found[k] = 1;
                            r[k][0] = word_float(w, KO_DST_X1);
                            r[k][1] = word_float(w, KO_DST_Y1);
                            r[k][2] = word_float(w, KO_DST_X2);
                            r[k][3] = word_float(w, KO_DST_Y2);
                        }
                    }
                }
                for (int k = 0; k < HUD_PIECES; ++k) {
                    if (!found[k]) {
                        fprintf(stderr, "  HUD %d, %d columns: no %s\n", size, columns[c], hud_pieces[k].name);
                        ++overlaps;
                    }
                }
                for (size_t q = 0; q < sizeof(apart) / sizeof(apart[0]); ++q) {
                    const int a = apart[q][0], b = apart[q][1];
                    if (size <= (columns[c] == 256 ? 2 : 4) && found[a] && found[b] && r[a][0] < r[b][2] - 0.01f &&
                        r[b][0] < r[a][2] - 0.01f && r[a][1] < r[b][3] - 0.01f && r[b][1] < r[a][3] - 0.01f) {
                        fprintf(stderr,
                                "  HUD %d, %d columns, states %x: %s %.1f,%.1f-%.1f,%.1f over %s %.1f,%.1f-%.1f,%.1f\n",
                                size, columns[c], states[s], hud_pieces[a].name, r[a][0], r[a][1], r[a][2], r[a][3],
                                hud_pieces[b].name, r[b][0], r[b][1], r[b][2], r[b][3]);
                        ++overlaps;
                    }
                }
            }
        }
    }
    khdays_composition_set_hud_size(4);
    mods_file = NULL;
    khdays_composition_load();
}

static void test_field_hud_reference(void)
{
    uint64_t d;
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    ignored_lines = 0;
    khdays_composition_load();
    CHECK(ignored_lines == 0);
    d = field_hud_digest();
    if (getenv("KHDAYS_PRINT_REFERENCE") != NULL) {
        printf("field HUD digest 0x%016llxull\n", (unsigned long long)d);
    }
    CHECK(d == FIELD_HUD_REFERENCE);
    mods_file = NULL;
}

/* A file of the test's, in the build's directory. */
static void test_hud_edge_alignment(void)
{
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    KhdaysSituation field = {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_HUD};
    for (int size = 1; size <= 8; ++size) {
        khdays_composition_set_hud_size(size);
        for (int width = 256; width <= 342; width += 43) {
            KhdaysComposition c;
            khdays_composition_set_hud_options(KHDAYS_HUD_ALIGN_EDGES);
            khdays_composition_for(&field, width, &c);
            int command = 0, health = 0;
            for (int i = 0; i < c.shape_count; ++i) {
                const uint32_t *w = c.shapes[i].words;
                if (word_float(w, KO_SRC_X) == 0 && word_float(w, KO_SRC_Y) == 84 && word_float(w, KO_SRC_W) == 112) {
                    CHECK(fabsf(word_float(w, KO_DST_Y2) - 192) < .001f);
                    CHECK(fabsf(word_float(w, KO_DST_X1)) < .001f);
                    ++command;
                }
                if (word_float(w, KO_SRC_X) == 128 && word_float(w, KO_SRC_Y) == 84) {
                    CHECK(fabsf(word_float(w, KO_DST_Y2) - 192) < .001f);
                    CHECK(fabsf(word_float(w, KO_DST_X2) - width) < .001f);
                    ++health;
                }
            }
            CHECK(command == 1 && health == 1);
            khdays_composition_set_hud_options(KHDAYS_HUD_ALIGN_EDGES | KHDAYS_HUD_PANEL_HIDDEN);
            khdays_composition_for(&field, width, &c);
            for (int i = 0; i < c.shape_count; ++i) {
                const uint32_t *w = c.shapes[i].words;
                if (word_float(w, KO_SRC_W) == 108 && word_float(w, KO_SRC_H) == 86)
                    CHECK(fabsf(word_float(w, KO_DST_Y1)) < .001f);
            }
        }
    }
    khdays_composition_set_hud_options(0);
    khdays_composition_set_hud_size(3);
    mods_file = NULL;
}

static void test_pause_target_placement(void)
{
    KhdaysFramePackage *p = calloc(1, sizeof(*p));
    CHECK(p != NULL);
    if (!p)
        return;
    uint32_t record[KE_WORDS] = {0};
    record[KE_SLOT] = KES_PICTURE | KES_PAUSE_LAYOUT | KES_DIRECT | 2;
    record[KE_IMAGE] = 333;
    record[KE_IW] = 64;
    record[KE_IH] = 96;
    const float x = 107.2f, y = 54, w = 41.6f, h = 62.4f;
    memcpy(record + KE_X, &x, 4);
    memcpy(record + KE_Y, &y, 4);
    memcpy(record + KE_W, &w, 4);
    memcpy(record + KE_H, &h, 4);
    p->elements[0].records = record;
    p->elements[0].count = 1;
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    KhdaysSituation s = {KHDAYS_SIT_PAUSE, 0};
    KhdaysComposition c;
    khdays_composition_for(&s, 342, &c);
    khdays_onescreen_images(p, &s, &c);
    int found = 0;
    const float factor = khdays_composition_hud_factor();
    for (int i = 0; i < c.shape_count; ++i) {
        const uint32_t *shape = c.shapes[i].words;
        if ((shape[KO_FLAGS] & KOF_IMAGE) && shape[KO_KEYS] == 333) {
            CHECK(fabsf(word_float(shape, KO_DST_X1) - (171 + (x - 128) * factor)) < .001f);
            CHECK(fabsf(word_float(shape, KO_DST_Y2) - word_float(shape, KO_DST_Y1) - h * factor) < .001f);
            ++found;
        }
    }
    CHECK(found == 1);
    for (int i = 0; i < c.shape_count; ++i) {
        const uint32_t *shape = c.shapes[i].words;
        CHECK((shape[KO_FLAGS] & (KOF_IMAGE | KOF_LOWER)) || word_float(shape, KO_SRC_X) != 72 ||
              word_float(shape, KO_SRC_Y) != 64);
    }
    s.states = 1u << KHDAYS_ST_EVENT;
    khdays_composition_for(&s, 342, &c);
    khdays_onescreen_images(p, &s, &c);
    for (int i = 0; i < c.shape_count; ++i)
        CHECK(!(c.shapes[i].words[KO_FLAGS] & KOF_IMAGE));
    free(p);
    mods_file = NULL;
}

static const char *test_file(const char *suffix, const char *text)
{
    static char paths[4][512];
    static int next;
    char *path = paths[next++ % 4];
    FILE *f;
    snprintf(path, sizeof(paths[0]), "%s/onescreen_test.%s", KHDAYS_TEST_DIR, suffix);
    f = fopen(path, "wb");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
    return path;
}

/* The title's animated picture (port_days.txt's [title]): its name from the menu's state; with
 * no mod giving it, the port's two screens side by side; with one, its pieces at each frame
 * -- the clip that names the group before (none after no picture), then the loop at the frame it
 * reached, the loop going on at its frame after the same group -- the 3D not shown. A plain
 * picture as the backdrop: its texels, no pieces; one read ahead (`preload`), taken when shown. */
static void test_title_backdrop(void)
{
    static const uint8_t rgba[2 * 4 * 4] = {255, 0, 0, 255};
    KhdaysSituation title = {KHDAYS_SIT_TITLE, 0, KHDAYS_TITLE_STORY, 2, 1, 5};
    KhdaysComposition comp;
    char picture[512];
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    atlas_file = root_file = backdrop_file = NULL;
    khdays_composition_load();
    khdays_composition_for(&title, 342, &comp);
    CHECK(strcmp(asked, "layouts/title/es/story_2_1.txt") == 0);
    CHECK(comp.backdrop == NULL && comp.screens == KHDAYS_SCREENS_SIDE_BY_SIDE && !comp.hide3d_all);

    snprintf(picture, sizeof(picture), "%s/onescreen_test.title.png", KHDAYS_TEST_DIR);
    CHECK(khdays_png_write(picture, rgba, 4, 2) == NULL);
    atlas_file = picture;
    backdrop_file = test_file("story.txt", "picture 100 50\n"
                                           "ground 1 1 1\n"
                                           "atlas atlas.png\n"
                                           "group story\n"
                                           "clip 10 after none intro\n"
                                           "piece 0 0 4 2 over 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
                                           "key 0 0 0 10 10 1 1 1 0\n"
                                           "key 10 0 0 10 10 1 1 1 1\n"
                                           "loop 20 # the light\n"
                                           "piece 4 0 -4 2 add 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
                                           "key 0 0 0 10 10 1 1 1 1\n"
                                           "key 20 20 0 30 10 1 1 1 1\n");
    root_file = test_file("root.txt", "picture 100 50\n"
                                      "atlas atlas.png\n"
                                      "group root\n"
                                      "clip 5 after story mission\n"
                                      "piece 0 0 4 2 over 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
                                      "key 0 1 1 2 2 1 1 1 1\n"
                                      "loop 7\n"
                                      "piece 0 0 4 2 over 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
                                      "key 0 3 3 4 4 1 1 1 1\n");
    khdays_composition_load();
    title.frame = 100; /* after no picture: the clip after none, at its first frame (nothing seen) */
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.backdrop != NULL && comp.backdrop_w == 4 && comp.backdrop_h == 2);
    CHECK(comp.animated && comp.picture_w == 100 && comp.picture_h == 50 && comp.ground == 0xffffffu);
    CHECK(comp.screens == KHDAYS_SCREENS_UPPER && comp.hide3d_all && comp.shape_count == 0);
    CHECK(comp.piece_count == 0);
    title.frame = 105; /* halfway through its fade */
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.piece_count == 1);
    if (comp.piece_count == 1) {
        CHECK(fabsf(comp.pieces[0].corners[3] - 0.5f) < 0.001f && comp.pieces[0].corners[0] == 1.0f);
        CHECK(comp.pieces[0].rect[2] == 10.0f && comp.pieces[0].blend == KHDAYS_PIECE_OVER);
    }
    title.frame = 112; /* the clip over: the loop at its frame 12 */
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.piece_count == 1);
    if (comp.piece_count == 1) {
        CHECK(fabsf(comp.pieces[0].rect[0] - 12.0f) < 0.001f && comp.pieces[0].blend == KHDAYS_PIECE_ADD);
        CHECK(comp.pieces[0].texels[0] == 4.0f && comp.pieces[0].texels[2] == -4.0f);
    }
    title.title_row = 0; /* another row, the same group: the loop goes on, at its frame 13 */
    title.frame = 113;
    khdays_composition_for(&title, 342, &comp);
    CHECK(strcmp(asked, "layouts/title/es/story_2_0.txt") == 0);
    CHECK(comp.piece_count == 1 && fabsf(comp.pieces[0].rect[0] - 13.0f) < 0.001f);
    title.title_menu = KHDAYS_TITLE_ROOT; /* after the story level: root's clip after story */
    title.frame = 120;
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.piece_count == 1 && comp.pieces[0].rect[0] == 1.0f && comp.ground == 0u);
    title.frame = 126; /* its 5 frames over: the loop at its frame 6 */
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.piece_count == 1 && comp.pieces[0].rect[0] == 3.0f);
    title.title_menu = KHDAYS_TITLE_NONE; /* the menu gone (the screens after it): no picture */
    title.frame = 127;
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.backdrop == NULL && comp.screens == KHDAYS_SCREENS_SIDE_BY_SIDE);
    title.title_menu = KHDAYS_TITLE_STORY; /* back: after none again */
    title.frame = 200;
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.piece_count == 0);

    /* a plain picture */
    remove(backdrop_file);
    remove(root_file);
    mods_file =
        test_file("plain.txt", "units port\n[title]\nbackdrop title/{lang}/plain.png at top-left\nscreens upper\n");
    khdays_composition_load();
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.backdrop != NULL && comp.backdrop[0] == 255 && comp.backdrop[1] == 0 && comp.backdrop[3] == 255);
    CHECK(!comp.animated && comp.picture_w == 4 && comp.picture_h == 2 && comp.piece_count == 0 &&
          comp.backdrop_ax == 0.0f);

    /* a picture read ahead: shown later, the thread's */
    remove(mods_file);
    mods_file = test_file("preload.txt",
                          "units port\n[title]\nbackdrop title/{lang}/{menu}.png\npreload title/{lang}/root.png\n"
                          "screens upper\n");
    khdays_composition_load();
    read_ahead_lines = 0;
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.backdrop != NULL && read_ahead_lines == 0);
    title.title_menu = KHDAYS_TITLE_ROOT;
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.backdrop != NULL && comp.backdrop[0] == 255 && comp.picture_w == 4 && read_ahead_lines == 1);
    title.title_menu = KHDAYS_TITLE_STORY;

    atlas_file = root_file = backdrop_file = NULL;
    remove(picture);
    remove(mods_file);
    mods_file = NULL;
    khdays_composition_load();
}

/* An animated picture's settings key (`prompt`, `icon`, `button`): with the keyboard its own
 * texels; with a gamepad's inputs the icon of the first that has one, at the piece's height, as
 * wide as the icon's shape, its right edge kept; the button's rectangle given out. */
static void test_title_prompt(void)
{
    static const uint8_t rgba[2 * 4 * 4] = {255, 0, 0, 255};
    static const char *const pad[] = {"xbox:guide", "xbox:leftstick"};
    KhdaysSituation title = {KHDAYS_SIT_TITLE, 0, KHDAYS_TITLE_MISSION, 2, 0, 5};
    KhdaysComposition comp;
    char picture[512];
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    snprintf(picture, sizeof(picture), "%s/onescreen_test.prompt.png", KHDAYS_TEST_DIR);
    CHECK(khdays_png_write(picture, rgba, 4, 2) == NULL);
    atlas_file = picture;
    mission_file = test_file("mission.txt", "picture 100 50\n"
                                            "atlas atlas.png\n"
                                            "group mission\n"
                                            "icon xbox:leftstick 2 0 2 1\n"
                                            "button open_settings 10 20 30 40\n"
                                            "loop 1\n"
                                            "piece 0 0 2 2 over 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1\n"
                                            "prompt open_settings\n"
                                            "key 0 10 20 30 40 1 1 1 1\n");
    khdays_composition_load();
    title.frame = 10;
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.button_count == 1);
    if (comp.button_count == 1) {
        CHECK(strcmp(comp.buttons[0].action, "open_settings") == 0 && comp.buttons[0].rect[0] == 10.0f &&
              comp.buttons[0].rect[3] == 40.0f);
    }
    CHECK(comp.piece_count == 1 && comp.pieces[0].texels[0] == 0.0f && comp.pieces[0].rect[0] == 10.0f);
    khdays_composition_set_prompt("open_settings", pad, 2); /* no icon for the guide: the stick's */
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.piece_count == 1);
    if (comp.piece_count == 1) {
        CHECK(comp.pieces[0].texels[0] == 2.0f && comp.pieces[0].texels[2] == 2.0f && comp.pieces[0].texels[3] == 1.0f);
        CHECK(fabsf(comp.pieces[0].rect[0] + 10.0f) < 0.001f && comp.pieces[0].rect[2] == 30.0f);
    }
    khdays_composition_set_prompt("open_settings", NULL, 0);
    khdays_composition_for(&title, 342, &comp);
    CHECK(comp.piece_count == 1 && comp.pieces[0].texels[0] == 0.0f);
    atlas_file = mission_file = NULL;
    remove(picture);
    remove(test_file("mission.txt", ""));
    mods_file = NULL;
    khdays_composition_load();
}

/* --- The pass's steps on the CPU ------------------------------------------------------ */

typedef uint32_t uint;

static uint32_t shape_words[32][KO_WORDS];
static uint32_t key_words[4];
static uint32_t upper_src[192][256], lower_src[192][256]; /* as KO_SOURCE gives them */
static uint32_t image_colour;
static uint32_t image_colours[16];
static int use_image_colours;
static float last_image_x, last_image_y;
static uint32_t image_sample(uint32_t image, float x, float y)
{
    last_image_x = x;
    last_image_y = y;
    return use_image_colours ? image_colours[image] : image_colour;
}

static uint32_t source(uint32_t lower, float x, float y)
{
    const int tx = x < 0 ? 0 : x > 255 ? 255 : (int)x, ty = y < 0 ? 0 : y > 191 ? 191 : (int)y;
    return lower ? lower_src[ty][tx] : upper_src[ty][tx];
}

static float as_float(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

#define KO_STEPS
#define KO_FN static inline
#define KO_SHAPE(i, w) shape_words[(i)][(w)]
#define KO_KEY(i) key_words[(i)]
#define KO_ASFLOAT(u) as_float((u))
#define KO_FLOOR(f) floorf(f)
#define KO_ABS(i) abs(i)
#define KO_SOURCE(lower, x, y) source((lower), (x), (y))
#define KO_IMAGE(image, x, y) image_sample((image), (x), (y))
#include "../gfx/onescreen.h"

static void put_float(uint32_t *w, int at, float f)
{
    memcpy(&w[at], &f, sizeof(f));
}

static void shape(int i, uint32_t flags, float sx, float sy, float sw, float sh, float x1, float y1, float scale)
{
    memset(shape_words[i], 0, sizeof(shape_words[i]));
    shape_words[i][KO_FLAGS] = flags;
    put_float(shape_words[i], KO_SRC_X, sx);
    put_float(shape_words[i], KO_SRC_Y, sy);
    put_float(shape_words[i], KO_SRC_W, sw);
    put_float(shape_words[i], KO_SRC_H, sh);
    put_float(shape_words[i], KO_DST_X1, x1);
    put_float(shape_words[i], KO_DST_Y1, y1);
    put_float(shape_words[i], KO_DST_X2, x1 + sw * scale);
    put_float(shape_words[i], KO_DST_Y2, y1 + sh * scale);
    put_float(shape_words[i], KO_SCALE_X, scale);
    put_float(shape_words[i], KO_SCALE_Y, scale);
    put_float(shape_words[i], KO_OPACITY, 1.0f);
}

static void test_steps(void)
{
    const uint32_t x3d = 0x00204060u | 0x1000000u; /* the 3D's colour there, shown */
    const uint32_t masters = 0;
    /* the upper source: a red piece over the 3D (K, F 0) at (10, 10), elsewhere nothing shown */
    memset(upper_src, 0, sizeof(upper_src));
    memset(lower_src, 0, sizeof(lower_src));
    upper_src[10][10] = 0x000000ffu | (1u << 24);
    /* half blended: K = half of green, F = 0.5 (1 + 127) */
    upper_src[20][20] = 0x00008000u | (128u << 24);
    lower_src[5][5] = 0x00ff0000u | (1u << 24); /* blue */
    lower_src[6][6] = 0x00080808u | (1u << 24); /* (8, 8, 8) in 8 bits: BGR555 (1, 1, 1) */
    /* nothing claims it: the 3D */
    CHECK(ko_pixel(50.5f, 50.5f, 1.0f, x3d, 0, KOB_DEFAULT, masters, 0, 171.0f, 1) == (x3d & 0xffffffu));
    /* an upper shape moved: its texel where it lands, the 3D through where it is not shown */
    shape(0, 0, 0, 0, 32, 32, 100, 100, 1.0f);
    CHECK(ko_pixel(110.5f, 110.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x000000ffu);
    CHECK(ko_pixel(111.5f, 110.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == (x3d & 0xffffffu));
    /* a 3D the DS shows over its 2D (the game over's orb): over the piece, with its alpha */
    CHECK(ko_pixel(110.5f, 110.5f, 1.0f, x3d | 0x2000000u | (31u << 26), 1, KOB_DEFAULT, masters, 0, 171.0f, 1) ==
          (x3d & 0xffffffu));
    CHECK(ko_pixel(110.5f, 110.5f, 1.0f, x3d | 0x2000000u | (0u << 26), 1, KOB_DEFAULT, masters, 0, 171.0f, 1) ==
          0x000000ffu);
    {
        /* K + F x X at the destination: green 0x80 + half the 3D */
        const uint32_t c = ko_pixel(120.5f, 120.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1);
        CHECK(ko_channel(c, 0) == 0x30u && ko_channel(c, 1) == 0xa0u && ko_channel(c, 2) == 0x10u);
    }
    /* a lower shape first claims it (the composition's order), then the upper one under it */
    shape(1, KOF_LOWER, 0, 0, 32, 32, 100, 100, 1.0f);
    lower_src[10][10] = 0x0000ff00u | (1u << 24);
    memcpy(shape_words[2], shape_words[0], sizeof(shape_words[0]));
    memcpy(shape_words[0], shape_words[1], sizeof(shape_words[1]));
    memcpy(shape_words[1], shape_words[2], sizeof(shape_words[2]));
    CHECK(ko_pixel(110.5f, 110.5f, 1.0f, x3d, 2, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x0000ff00u);
    /* its key colour skipped: the next shape's */
    lower_src[10][10] = 0x00080808u | (1u << 24);
    key_words[0] = 0x0421u; /* BGR555 (1, 1, 1) */
    shape_words[0][KO_KEYS] = 0;
    shape_words[0][KO_KEY_COUNT] = 1;
    CHECK(ko_pixel(110.5f, 110.5f, 1.0f, x3d, 2, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x000000ffu);
    /* mirrored in x: the source's column 31 - 10 lands at 100 + 10 */
    shape(0, KOF_LOWER | KOF_MIRROR_X, 0, 0, 32, 32, 100, 100, 1.0f);
    lower_src[10][21] = 0x00ff00ffu | (1u << 24);
    CHECK(ko_pixel(110.5f, 110.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x00ff00ffu);
    /* at half the size (the port's): an output texel covers 2 x 2 of the
     * source's texels and takes their average, so a line one texel thick keeps half
     * its colour; the nearest texel alone would have been the black row under it */
    for (int x = 0; x < 32; ++x) {
        lower_src[60][x] = 0x00ffffffu | (1u << 24);
        lower_src[61][x] = 0x00000000u | (1u << 24);
    }
    shape(0, KOF_LOWER, 0, 60, 32, 2, 100, 100, 0.5f);
    CHECK(ko_pixel(104.5f, 100.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x00808080u);
    /* where only some of them show, the others count as the 3D under the shape */
    for (int x = 0; x < 32; ++x) {
        lower_src[61][x] = 0;
    }
    CHECK(ko_pixel(104.5f, 100.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x0090a0b0u);
    /* none of them shown: the next shape's, here the 3D itself */
    for (int x = 0; x < 32; ++x) {
        lower_src[60][x] = 0;
    }
    CHECK(ko_pixel(104.5f, 100.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == (x3d & 0xffffffu));
    /* at twice the size: the source's (5, 5) covers (110-112, 110-112) */
    shape(0, KOF_LOWER, 0, 0, 32, 32, 100, 100, 2.0f);
    CHECK(ko_pixel(111.9f, 111.9f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x00ff0000u);
    /* opacity: half the lower texel, half the 3D under it */
    shape_words[0][KO_FLAGS] |= KOF_WEIGHT;
    put_float(shape_words[0], KO_OPACITY, 0.5f);
    {
        const uint32_t c = ko_pixel(111.9f, 111.9f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1);
        CHECK(ko_channel(c, 0) == 0x30u && ko_channel(c, 1) == 0x20u && ko_channel(c, 2) == 0x90u);
    }
    /* seen through: the grey (8, 8, 8) at half over the 3D (0x60, 0x40, 0x20); the blue,
     * another colour, not */
    shape(0, KOF_LOWER | KOF_SEE_THROUGH, 0, 0, 32, 32, 100, 100, 1.0f);
    shape_words[0][KO_SEE_THROUGH] = (1u | 1u << 5 | 1u << 10) | 128u << 16;
    {
        const uint32_t c = ko_pixel(106.5f, 106.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1);
        CHECK(ko_channel(c, 0) == 0x34u && ko_channel(c, 1) == 0x24u && ko_channel(c, 2) == 0x14u);
        CHECK(ko_pixel(105.5f, 105.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x00ff0000u);
    }
    /* forced: a texel the 2D leaves invisible shows its colour (black here) */
    shape(0, KOF_LOWER | KOF_FORCE, 0, 0, 32, 32, 100, 100, 1.0f);
    CHECK(ko_pixel(100.5f, 100.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0u);
    /* rounded corners of radius 6: the corner's texel outside the quarter circle, what is under it;
     * one inside it, the shape's (forced: black) */
    shape(0, KOF_LOWER | KOF_ROUND | KOF_FORCE, 0, 0, 32, 32, 100, 100, 1.0f);
    put_float(shape_words[0], KO_ROUND, 6.0f);
    CHECK(ko_pixel(100.5f, 100.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == (x3d & 0xffffffu));
    CHECK(ko_pixel(104.5f, 104.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0u);
    CHECK(ko_pixel(131.5f, 131.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == (x3d & 0xffffffu));
    /* dimmed: half toward black */
    shape(0, KOF_LOWER | KOF_DIM, 0, 0, 32, 32, 100, 100, 1.0f);
    put_float(shape_words[0], KO_OPACITY, 0.5f);
    CHECK(ko_pixel(105.5f, 105.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x00800000u);
    /* repeated along its rows: past its right edge the source again, its rows kept */
    shape(0, KOF_LOWER | KOF_REPEAT_X, 0, 0, 32, 32, 100, 100, 1.0f);
    CHECK(ko_pixel(137.5f, 105.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == 0x00ff0000u);
    CHECK(ko_pixel(137.5f, 140.5f, 1.0f, x3d, 1, KOB_DEFAULT, masters, 0, 171.0f, 1) == (x3d & 0xffffffu));
    /* the brightness: down by 8 of 16 on the upper screen halves the 3D */
    {
        const uint32_t c = ko_pixel(50.5f, 50.5f, 1.0f, x3d, 0, KOB_UPPER, (2u << 14) | 8u, 0, 171.0f, 1);
        CHECK(c == 0x00102030u);
    }
    /* the default: the lower screen's while it is dark */
    {
        const uint32_t c = ko_pixel(50.5f, 50.5f, 1.0f, x3d, 0, KOB_DEFAULT, ((2u << 14) | 16u) << 16, 0, 171.0f, 1);
        CHECK(c == 0u);
    }
}

static int image_shapes(const KhdaysComposition *c);
static void test_obtained_image(void)
{
    KhdaysFramePackage *p = calloc(1, sizeof(*p));
    CHECK(p != NULL);
    if (!p)
        return;
    uint32_t record[KE_WORDS] = {0};
    record[KE_SLOT] = KES_DIRECT | KES_PICTURE | KES_HUD | KES_NATIVE_FADE | 1;
    record[KE_IMAGE] = 77;
    record[KE_IW] = 712;
    record[KE_IH] = 280;
    put_float(record, KE_X, -2);
    put_float(record, KE_Y, 54);
    put_float(record, KE_W, 178);
    put_float(record, KE_H, 70);
    p->elements[0].records = record;
    p->elements[0].count = 1;
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    for (int notice = 0; notice < 4; ++notice) {
        memset(&p->obtained_image, 0, sizeof(p->obtained_image));
        memset(p->notification_images, 0, sizeof(p->notification_images));
        unsigned *words = notice ? p->notification_images[notice - 1].words : p->obtained_image.words;
        words[KSI_OUTPUT] = 77;
        words[KSI_ANCHORED] = 1;
        put_float(words, KSI_ANCHOR_X, 0);
        put_float(words, KSI_ANCHOR_Y, 56);
        for (int size = 1; size <= 8; size *= 2) {
            khdays_composition_set_hud_size(size);
            KhdaysSituation s = {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_HUD};
            KhdaysComposition comp;
            khdays_composition_for(&s, 342, &comp);
            float root_x = 0, root_y = 0;
            int found = 0;
            for (int i = 0; i < comp.shape_count; ++i) {
                const uint32_t *w = comp.shapes[i].words;
                if (w[KO_FLAGS] & (KOF_LOWER | KOF_3D | KOF_IMAGE))
                    continue;
                const float sx = word_float(w, KO_SRC_X), sy = word_float(w, KO_SRC_Y);
                if (sx <= 0 && sx + word_float(w, KO_SRC_W) > 0 && sy <= 56 && sy + word_float(w, KO_SRC_H) > 56) {
                    root_x = word_float(w, KO_DST_X1) - sx * word_float(w, KO_SCALE_X);
                    root_y = word_float(w, KO_DST_Y1) + (56 - sy) * word_float(w, KO_SCALE_Y);
                    found = 1;
                    break;
                }
            }
            CHECK(found);
            const float scale = .766667f * khdays_composition_hud_factor();
            khdays_onescreen_images(p, &s, &comp);
            CHECK(image_shapes(&comp) == 1);
            for (int i = 0; i < comp.shape_count; ++i) {
                const uint32_t *w = comp.shapes[i].words;
                if ((w[KO_FLAGS] & KOF_IMAGE) && w[KO_KEYS] == 77) {
                    CHECK(fabsf(word_float(w, KO_DST_X1) - (root_x - 2 * scale)) < .001f);
                    CHECK(fabsf(word_float(w, KO_DST_Y1) - (root_y - 2 * scale)) < .001f);
                    CHECK(fabsf(word_float(w, KO_DST_X2) - word_float(w, KO_DST_X1) - 178 * scale) < .001f);
                }
            }
        }
    }
    free(p);
}
static void test_independent_image(void)
{
    /* Outside the screen's crop, an image still covers its own rectangle; its alpha composes
     * against the next shape, and transparent texels leave that shape alone. */
    memset(upper_src, 0, sizeof(upper_src));
    upper_src[10][10] = 0x010000ffu;
    shape(0, KOF_IMAGE, 0, 0, 128, 64, 50, 50, 1);
    shape(1, 0, 0, 0, 32, 32, 50, 50, 1);
    image_colour = 0x8000ff00u;
    CHECK(ko_pixel(60.5f, 60.5f, 1, 0, 2, KOB_DEFAULT, 0, 0, 128, 1) == 0x00807fu);
    CHECK(ko_pixel(150.5f, 60.5f, 1, 0, 2, KOB_DEFAULT, 0, 0, 128, 1) == 0x008000u);
    image_colour = 0;
    CHECK(ko_pixel(60.5f, 60.5f, 1, 0, 2, KOB_DEFAULT, 0, 0, 128, 1) == 0xffu);
    image_colour = 0xff00ff00u;
    CHECK(ko_pixel(60.5f, 60.5f, 1, 0, 2, KOB_DEFAULT, 0x8010, 0, 128, 1) == 0);
}

static int image_shapes(const KhdaysComposition *comp)
{
    int count = 0;
    for (int i = 0; i < comp->shape_count; ++i)
        count += (comp->shapes[i].words[KO_FLAGS] & KOF_IMAGE) != 0;
    return count;
}

static int information_crops(const KhdaysComposition *comp)
{
    int count = 0;
    for (int i = 0; i < comp->shape_count; ++i) {
        const uint32_t *w = comp->shapes[i].words;
        const float width = word_float(w, KO_SRC_W);
        if (!(w[KO_FLAGS] & (KOF_3D | KOF_IMAGE)) && word_float(w, KO_SRC_X) == 0 && word_float(w, KO_SRC_Y) == 0 &&
            word_float(w, KO_SRC_H) == 24 && (width == 224 || width == 232 || width == 256))
            ++count;
    }
    return count;
}

static void test_information_image(void)
{
    KhdaysFramePackage *p = calloc(1, sizeof(*p));
    CHECK(p != NULL);
    if (!p)
        return;
    uint32_t record[KE_WORDS] = {0};
    record[KE_SLOT] = KES_DIRECT | KES_FINE | KES_PICTURE | KES_NATIVE_FADE | 1;
    record[KE_IMAGE] = 25;
    record[KE_IW] = 1200;
    record[KE_IH] = 120;
    put_float(record, KE_X, -70);
    put_float(record, KE_Y, -6);
    put_float(record, KE_W, 300);
    put_float(record, KE_H, 30);
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    for (int source = 0; source < 2; ++source) {
        p->elements[source].records = record;
        p->elements[source].count = 1;
        p->information_images[source].words[KSI_OUTPUT] = 25;
        p->information_images[source].words[KSI_ANCHORED] = 1;
        // Neither a moving native text anchor nor an invisible offscreen BG may move/hide the banner.
        put_float(p->information_images[source].words, KSI_ANCHOR_X, 999);
        put_float(p->information_images[source].words, KSI_ANCHOR_Y, 999);
        p->desc2d[source][KD_BLEND] = 2u | (0x21u << 8) | (1u << 16);
        p->desc2d[source][KD_COEF] = 16u << 8;
        for (int size = 1; size <= 8; size += size == 1 ? 3 : 4) {
            khdays_composition_set_hud_size(size);
            const float factor = khdays_composition_hud_factor();
            const float gauge_scale = .766667f * factor;
            for (int upper = 0; upper < 2; ++upper) {
                p->upper = (uint8_t)upper;
                p->information_state = KHDAYS_INFORMATION_AVAILABLE | (0x101u << source);
                KhdaysSituation s = {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_HUD};
                KhdaysComposition comp;
                CHECK(khdays_onescreen_information(p, &s));
                CHECK(s.states & (1u << KHDAYS_ST_INFORMATION_MOD));
                khdays_composition_for(&s, 342, &comp);
                CHECK(information_crops(&comp) == 0);
                float player_scale = 0;
                for (int n = 0; n < comp.shape_count; ++n) {
                    const uint32_t *native = comp.shapes[n].words;
                    if (!(native[KO_FLAGS] & (KOF_IMAGE | KOF_3D | KOF_LOWER)) && word_float(native, KO_SRC_X) == 128 &&
                        word_float(native, KO_SRC_W) == 128)
                        player_scale = word_float(native, KO_SCALE_X);
                }
                khdays_onescreen_images(p, &s, &comp);
                CHECK(image_shapes(&comp) == 1);
                const uint32_t *image = comp.shapes[0].words;
                CHECK(image[KO_FLAGS] & KOF_IMAGE);
                CHECK(!(image[KO_FLAGS] & KOF_NATIVE_FADE));
                CHECK(fabsf(word_float(image, KO_DST_X1) - -70 * gauge_scale) < .001f);
                CHECK(fabsf(word_float(image, KO_DST_Y1) - (5 * factor - 6 * gauge_scale)) < .001f);
                CHECK(fabsf(word_float(image, KO_DST_X2) - 230 * gauge_scale) < .001f);
                const float image_scale = word_float(image, KO_SCALE_X) * record[KE_IW] / 300;
                CHECK(fabsf(image_scale - gauge_scale) < .001f);
                CHECK(player_scale > 0 && fabsf(image_scale - player_scale) < .001f);
                // Rendering does not require any source-screen crop, including a custom composition.
                comp.shapes = NULL;
                comp.shape_count = 0;
                khdays_onescreen_images(p, &s, &comp);
                CHECK(image_shapes(&comp) == 1);

                // End of exit: no replacement image and no one-frame return to the DS crop.
                p->information_state &= ~0x300u;
                khdays_onescreen_information(p, &s);
                khdays_composition_for(&s, 342, &comp);
                khdays_onescreen_images(p, &s, &comp);
                CHECK(image_shapes(&comp) == 0 && information_crops(&comp) == 0);
            }
        }
        p->elements[source].count = 0;
        p->information_images[source] = (KhdaysSequenceImage){0};
    }
    khdays_composition_set_hud_size(4);
    p->elements[1].records = record;
    p->elements[1].count = 1;
    p->information_images[1].words[KSI_OUTPUT] = 25;
    p->information_images[1].words[KSI_ANCHORED] = 1;
    p->information_state = KHDAYS_INFORMATION_AVAILABLE | 0x202u;
    p->upper = 0;
    // One visual window during an upper/lower handoff, with the native upper image on top.
    uint32_t upper_record[KE_WORDS];
    memcpy(upper_record, record, sizeof(upper_record));
    upper_record[KE_IMAGE] = 26;
    p->elements[0].records = upper_record;
    p->elements[0].count = 1;
    p->information_images[0].words[KSI_OUTPUT] = 26;
    p->information_images[0].words[KSI_ANCHORED] = 1;
    p->information_state |= 0x101u;
    KhdaysSituation handoff = {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_HUD};
    KhdaysComposition comp;
    khdays_onescreen_information(p, &handoff);
    khdays_composition_for(&handoff, 342, &comp);
    khdays_onescreen_images(p, &handoff, &comp);
    CHECK(image_shapes(&comp) == 1 && comp.shapes[0].words[KO_KEYS] == 26);
    CHECK(!(handoff.states & (1u << KHDAYS_ST_INFO_UPPER)));
    CHECK(comp.shape3d_count > 0); // The heart counter must retain its composition transform during the exit.
    // The native game can still be showing its upper panel after the sequence's last frame.
    KhdaysSituation native_upper = {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_INFO_UPPER};
    p->information_state &= ~0x100u;
    khdays_onescreen_information(p, &native_upper);
    CHECK(native_upper.states & (1u << KHDAYS_ST_INFO_UPPER));
    CHECK(!(native_upper.states & (1u << KHDAYS_ST_HUD)));
    khdays_composition_for(&native_upper, 342, &comp);
    CHECK(comp.shape3d_count == 1 && comp.shapes3d[0].hide);
    p->hud_options = KHDAYS_HUD_KEEP_INFORMATION;
    p->situation.event_status = 1; // A mission script can show INFORMATION without being a cutscene/dialog.
    p->situation.updates[KHDAYS_UPD_Ov023_SceneRun / 64] |= 1ull << (KHDAYS_UPD_Ov023_SceneRun % 64);
    khdays_composition_set_hud_options(p->hud_options);
    khdays_onescreen_information(p, &native_upper);
    CHECK(native_upper.states & (1u << KHDAYS_ST_HUD));
    khdays_composition_for(&native_upper, 342, &comp);
    CHECK(comp.shape3d_count > 0 && !comp.shapes3d[0].hide);
    native_upper.states &= ~(1u << KHDAYS_ST_HUD);
    p->situation.event_status = 3;
    khdays_onescreen_information(p, &native_upper);
    CHECK(native_upper.states & (1u << KHDAYS_ST_HUD));
    native_upper.states &= ~(1u << KHDAYS_ST_HUD);
    native_upper.states |= 1u << KHDAYS_ST_DIALOG;
    khdays_onescreen_information(p, &native_upper);
    CHECK(!(native_upper.states & (1u << KHDAYS_ST_HUD)));
    native_upper.states &= ~(1u << KHDAYS_ST_DIALOG);
    p->situation.event_status = -1;
    p->hud_options = 0;
    khdays_composition_set_hud_options(0);
    // Pause uses the retained lower caption even if the upper exit was still running.
    p->information_state |= 0x100u;
    KhdaysSituation paused_handoff = {KHDAYS_SIT_PAUSE, 0};
    khdays_onescreen_information(p, &paused_handoff);
    khdays_composition_for(&paused_handoff, 342, &comp);
    khdays_onescreen_images(p, &paused_handoff, &comp);
    CHECK(image_shapes(&comp) == 1 && comp.shapes[0].words[KO_KEYS] == 25);
    p->elements[0].count = 0;
    p->information_images[0] = (KhdaysSequenceImage){0};
    p->information_state = KHDAYS_INFORMATION_AVAILABLE | 0x202u;
    KhdaysSituation dialog = {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_DIALOG};
    khdays_onescreen_information(p, &dialog);
    khdays_composition_for(&dialog, 342, &comp);
    khdays_onescreen_images(p, &dialog, &comp);
    CHECK(image_shapes(&comp) == 0);
    p->upper = 0;
    KhdaysSituation pause = {KHDAYS_SIT_PAUSE, 0};
    khdays_onescreen_information(p, &pause);
    khdays_composition_for(&pause, 342, &comp);
    CHECK(information_crops(&comp) == 0);
    khdays_onescreen_images(p, &pause, &comp);
    CHECK(image_shapes(&comp) == 1);
    pause.states |= 1u << KHDAYS_ST_EVENT;
    khdays_composition_for(&pause, 342, &comp);
    khdays_onescreen_images(p, &pause, &comp);
    CHECK(image_shapes(&comp) == 0);
    const int hidden[] = {KHDAYS_SIT_CAMP_MENU, KHDAYS_SIT_MOVIE, KHDAYS_SIT_TUTORIAL, KHDAYS_SIT_SHOP};
    for (size_t i = 0; i < sizeof(hidden) / sizeof(hidden[0]); ++i) {
        KhdaysSituation s = {hidden[i], 0};
        khdays_onescreen_information(p, &s);
        khdays_composition_for(&s, 342, &comp);
        khdays_onescreen_images(p, &s, &comp);
        CHECK(image_shapes(&comp) == 0);
    }
    // Availability remains set even before the first caption or after resetting its context.
    p->information_state = KHDAYS_INFORMATION_AVAILABLE;
    pause.states = 0;
    CHECK(khdays_onescreen_information(p, &pause));
    khdays_composition_for(&pause, 342, &comp);
    CHECK(information_crops(&comp) == 0);
    // No mod: preserve the original crop and the native timer's ownership.
    p->information_state = 0;
    p->elements[1].count = 0;
    CHECK(!khdays_onescreen_information(p, &pause));
    khdays_composition_for(&pause, 342, &comp);
    CHECK(information_crops(&comp) == 1);
    KhdaysSituation fallback = {KHDAYS_SIT_FIELD, (1u << KHDAYS_ST_HUD) | (1u << KHDAYS_ST_INFO_NEW)};
    CHECK(!khdays_onescreen_information(p, &fallback));
    khdays_composition_for(&fallback, 342, &comp);
    CHECK(information_crops(&comp) == 1);
    free(p);
}
static void test_native_hud_fade(void)
{
    memset(upper_src, 0, sizeof(upper_src));
    memset(lower_src, 0, sizeof(lower_src));
    use_image_colours = 1;
    image_colours[1] = 0xff0000ffu; /* opaque portrait over the green gauge */
    image_colours[2] = 0xff00ff00u;
    for (int i = 0; i < 2; ++i) {
        shape(i, KOF_IMAGE | KOF_NATIVE_FADE, 0, 0, 32, 32, 50, 50, 1);
        shape_words[i][KO_KEYS] = (uint32_t)i + 1u;
        shape_words[i][KO_IMAGE_GROUP] = 4;
    }
    for (int level = 0; level <= 16; ++level) {
        const float alpha = (float)level / 16;
        put_float(shape_words[0], KO_IMAGE_ALPHA, alpha);
        put_float(shape_words[1], KO_IMAGE_ALPHA, alpha);
        const uint32_t expected = (uint32_t)(255 * alpha + 0.5f) | ((uint32_t)(255 * (1 - alpha) + 0.5f) << 16);
        CHECK(ko_pixel(60.5f, 60.5f, 1, 0x01ff0000u, 2, KOB_DEFAULT, 0, 0, 128, 1) == expected);
    }
    put_float(shape_words[0], KO_IMAGE_ALPHA, 0.5f);
    put_float(shape_words[1], KO_IMAGE_ALPHA, 0.5f);
    image_colours[1] = 0x800000ffu;
    const uint32_t mixed = ko_pixel(60.5f, 60.5f, 1, 0x01ff0000u, 2, KOB_DEFAULT, 0, 0, 128, 1);
    CHECK(abs((int)(mixed & 255u) - 64) <= 1 && abs((int)((mixed >> 8) & 255u) - 64) <= 1 &&
          ((mixed >> 16) & 255u) == 128u);
    /* An independent foreground image does not inherit the HUD group's fade. */
    memcpy(shape_words[2], shape_words[1], sizeof(shape_words[2]));
    shape_words[1][KO_KEYS] = 1;
    shape_words[0][KO_FLAGS] = KOF_IMAGE;
    shape_words[0][KO_KEYS] = 3;
    image_colours[3] = 0xffffffffu;
    CHECK(ko_pixel(60.5f, 60.5f, 1, 0x01ff0000u, 3, KOB_DEFAULT, 0, 0, 128, 1) == 0xffffffu);
    use_image_colours = 0;
}

static void test_hud_image_layers(void)
{
    KhdaysFramePackage *p = calloc(1, sizeof(*p));
    uint32_t records[6][KE_WORDS] = {0};
    /* Number label, portrait, player gauge, party gauge, LIMIT, enemy gauge. */
    const float rects[6][4] = {{180, 180, 16, 8},  {200, 130, 54, 62}, {90, 120, 166, 72},
                               {200, 100, 54, 32}, {146, 174, 40, 8},  {120, 40, 136, 16}};
    CHECK(p != NULL);
    if (!p)
        return;
    p->elements[0].records = records[0];
    p->elements[0].count = 6;
    for (int i = 0; i < 6; ++i) {
        records[i][KE_SLOT] = KES_DIRECT | KES_PICTURE | KES_HUD | 3;
        records[i][KE_IMAGE] = (uint32_t)i + 1u;
        for (int a = 0; a < 4; ++a)
            put_float(records[i], KE_X + a, rects[i][a]);
        records[i][KE_IW] = (uint32_t)rects[i][2] * 4u;
        records[i][KE_IH] = (uint32_t)rects[i][3] * 4u;
    }
    p->enemy_image.words[KSI_OUTPUT] = 6;
    p->enemy_image.words[KSI_ANCHORED] = 1;
    put_float(p->enemy_image.words, KSI_ANCHOR_X, 254);
    put_float(p->enemy_image.words, KSI_ANCHOR_Y, 12);
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    for (int width = 256; width <= 512; width += 128) {
        for (int size = 1; size <= 8; size += size == 1 ? 3 : 4) {
            KhdaysComposition comp;
            KhdaysSituation s = {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_HUD};
            khdays_composition_set_hud_size(size);
            khdays_composition_for(&s, width, &comp);
            const int original = comp.shape_count;
            khdays_onescreen_images(p, &s, &comp);
            CHECK(image_shapes(&comp) == 6 && comp.shape_count == original + 6);
            for (int i = 0, last = 0; i < comp.shape_count; ++i) {
                const uint32_t *w = comp.shapes[i].words;
                if ((w[KO_FLAGS] & KOF_IMAGE) && w[KO_KEYS] <= 5) {
                    CHECK((int)w[KO_KEYS] > last); /* keep labels above their gauges */
                    last = (int)w[KO_KEYS];
                    if (w[KO_KEYS] == 3) {
                        const float scale = word_float(w, KO_SCALE_X);
                        CHECK(word_float(w, KO_DST_X2) - word_float(w, KO_DST_X1) > 128 * 4 * scale);
                    }
                }
            }
        }
    }
    khdays_composition_set_hud_size(4);
    for (int level = 0; level <= 16; ++level) {
        KhdaysSituation s = {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_HUD};
        KhdaysComposition comp;
        p->desc2d[0][KD_BLEND] = 8u | (0x21u << 8) | (1u << 16);
        p->desc2d[0][KD_COEF] = (uint32_t)level | ((uint32_t)(16 - level) << 8);
        khdays_composition_for(&s, 342, &comp);
        khdays_onescreen_images(p, &s, &comp);
        CHECK(image_shapes(&comp) == 6);
        for (int i = 0; i < comp.shape_count; ++i) {
            const uint32_t *w = comp.shapes[i].words;
            if (w[KO_FLAGS] & KOF_IMAGE) {
                CHECK(w[KO_FLAGS] & KOF_NATIVE_FADE);
                CHECK(w[KO_IMAGE_GROUP] == 4);
                CHECK(word_float(w, KO_IMAGE_ALPHA) == (float)level / 16);
            }
        }
    }
    {
        KhdaysSituation s = {KHDAYS_SIT_TUTORIAL, 1u << KHDAYS_ST_HUD};
        KhdaysComposition comp;
        khdays_composition_for(&s, 342, &comp);
        khdays_onescreen_images(p, &s, &comp);
        CHECK(image_shapes(&comp) == 6 && comp.shape_count == 9);
        CHECK(comp.shapes[0].words[KO_FLAGS] == KOF_LOWER); /* tutorial page stays above every HUD image */
        for (int i = 1; i <= 6; ++i) {
            const uint32_t *w = comp.shapes[i].words;
            CHECK((w[KO_FLAGS] & (KOF_IMAGE | KOF_DIM)) == (KOF_IMAGE | KOF_DIM));
            CHECK(fabsf(word_float(w, KO_OPACITY) - 0.35f) < 0.001f);
        }
        CHECK(comp.shape_count <= 32);
        memcpy(shape_words, comp.shapes, (size_t)comp.shape_count * sizeof(*comp.shapes));
        memset(upper_src, 0, sizeof(upper_src));
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x)
                lower_src[y][x] = 0x010000ffu;
        image_colour = 0xff00ff00u;
        const uint32_t *gauge = comp.shapes[3].words;
        const float px = floorf((word_float(gauge, KO_DST_X1) + word_float(gauge, KO_DST_X2)) * 0.5f) + 0.5f;
        const float py = floorf((word_float(gauge, KO_DST_Y1) + word_float(gauge, KO_DST_Y2)) * 0.5f) + 0.5f;
        CHECK(ko_pixel(px, py, 1, 0, (uint32_t)comp.shape_count, KOB_OWN, 0, 0, 171, 1) == 0xffu);
        const int x = (int)(px - word_float(comp.shapes[0].words, KO_DST_X1));
        const int y = (int)(py - word_float(comp.shapes[0].words, KO_DST_Y1));
        CHECK(x >= 0 && x < 256 && y >= 0 && y < 192);
        if (x >= 0 && x < 256 && y >= 0 && y < 192)
            lower_src[y][x] = 0;
        CHECK(ko_pixel(px, py, 1, 0, (uint32_t)comp.shape_count, KOB_OWN, 0, 0, 171, 1) == 0x5900u);
    }
    {
        const KhdaysSituation hidden[] = {{KHDAYS_SIT_PAUSE, 0},
                                          {KHDAYS_SIT_GAME_OVER, 0},
                                          {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_DIALOG},
                                          {KHDAYS_SIT_FIELD, 1u << KHDAYS_ST_INFO_UPPER},
                                          {KHDAYS_SIT_FIELD, 0},
                                          {KHDAYS_SIT_TUTORIAL, 0}};
        for (size_t i = 0; i < sizeof(hidden) / sizeof(hidden[0]); ++i) {
            KhdaysComposition comp;
            khdays_composition_for(&hidden[i], 342, &comp);
            const int original = comp.shape_count;
            khdays_onescreen_images(p, &hidden[i], &comp);
            CHECK(image_shapes(&comp) == 0 && comp.shape_count == original);
        }
    }
    /* HUD visibility does not suppress independent images belonging to a menu. */
    {
        KhdaysSituation s = {KHDAYS_SIT_PAUSE, 0};
        KhdaysComposition comp;
        p->elements[0].count = 1;
        records[0][KE_SLOT] &= ~KES_HUD;
        put_float(records[0], KE_X, 100);
        put_float(records[0], KE_Y, 80);
        khdays_composition_for(&s, 342, &comp);
        khdays_onescreen_images(p, &s, &comp);
        CHECK(image_shapes(&comp) == 1);
    }
    free(p);
}

/* --- The upper screen's 2D apart from its 3D ------------------------------------------- */

static uint8_t io[0x2000];
static uint8_t oam[0x800];
static uint8_t palette[0x800];
static uint8_t bg_a[0x80000];
static uint32_t layer3d[256 * 192];
static KhdaysVramPages pages;

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

/* 8-bit channels from kd_pixel's 6-bit ones, as compose_cpu.c's to_rgb888 */
static uint32_t widen6(uint32_t c6)
{
    uint32_t acc = 0;
    for (int n = 0; n < 3; ++n) {
        const uint32_t c = (c6 >> (8 * n)) & 63u;
        acc |= ((c << 2) | (c >> 4)) << (8 * n);
    }
    return acc;
}

static void test_split(void)
{
    uint32_t desc[KHDAYS_DESC_WORDS], split[KHDAYS_DESC_WORDS];
    KhdaysGfxLatch latch;
    KhdaysGfxMemory m;
    /* engine A: BG0 the 3D, BG1 a red tile at (2, 3) above it blended at EVA 10, EVB 6 */
    put32(io, 0x00010000u | 0x8u | 0x100u | 0x200u);
    put16(io + 0x08, 1u);                             /* BG0CNT: priority 1 (the 3D's), under BG1 */
    put16(io + 0x0a, 0u | (1u << 2) | (31u << 8));    /* BG1CNT: priority 0, chars 0x4000, map 0xf800 */
    put16(io + 0x50, 0x2u | (1u << 6) | (0x1u << 8)); /* BLDCNT: BG1 over BG0, alpha */
    put16(io + 0x52, 10u | (6u << 8));                /* BLDALPHA */
    put16(palette + 2, 0x001f);
    memset(bg_a + 0x4000 + 32, 0x11, 32);
    put16(bg_a + 0xf800 + (3 * 32 + 2) * 2, 1);
    for (int i = 0; i < 32; ++i) {
        pages.page[KHDAYS_AREA_ABG][i] = bg_a + i * KHDAYS_VRAM_PAGE;
    }
    for (int i = 0; i < 256 * 192; ++i) {
        layer3d[i] = 20u | (40u << 8) | (60u << 16) | (31u << 24); /* the 3D, 6-bit, opaque */
    }
    memset(&latch, 0, sizeof(latch));
    khdays_gfx_describe(io, palette, oam, 0, &latch, desc);
    memset(&m, 0, sizeof(m));
    m.vram = &pages;
    m.palette = palette;
    m.layer3d = layer3d;
    memcpy(split, desc, sizeof(split));
    split[KD_FLAGS] |= KDF_SPLIT | KDF_FINE;
    {
        /* where the tile is: K + F x X is today's blend (within the 5-bit composition's rounding) */
        const uint32_t today = widen6(khdays_gfx_pixel_cpu(desc, &m, 20, 28));
        const uint32_t k = khdays_gfx_pixel_cpu(split, &m, 20, 28);
        const uint32_t x = widen6(layer3d[28 * 256 + 20]);
        const float f = (float)((k >> 24) - 1u) / 254.0f;
        CHECK((k >> 24) != 0u);
        CHECK(fabsf(f - 6.0f / 16.0f) < 0.01f);
        for (int n = 0; n < 3; ++n) {
            const float again = (float)((k >> (8 * n)) & 255u) + f * (float)((x >> (8 * n)) & 255u);
            CHECK(fabsf(again - (float)((today >> (8 * n)) & 255u)) <= 12.0f);
        }
        /* beside it the 3D alone: not shown over the 3D */
        CHECK((khdays_gfx_pixel_cpu(split, &m, 60, 60) >> 24) == 0u);
    }
}

/* A layer under the 3D (the game over's menu: ov027's BG0 priority 0, BG1 2)
 * shows where the 3D drawn is transparent, as on the DS; under an opaque 3D,
 * or with the 3D not given, it does not. */
static void test_split_under_3d(void)
{
    uint32_t desc[KHDAYS_DESC_WORDS], split[KHDAYS_DESC_WORDS];
    KhdaysGfxLatch latch;
    KhdaysGfxMemory m;
    put32(io, 0x00010000u | 0x8u | 0x100u | 0x200u);
    put16(io + 0x08, 0u);                          /* BG0CNT: priority 0, over BG1 */
    put16(io + 0x0a, 2u | (1u << 2) | (31u << 8)); /* BG1CNT: priority 2, the red tile at (2, 3) */
    put16(io + 0x50, 0u);
    put16(io + 0x52, 0u);
    for (int i = 0; i < 256 * 192; ++i) {
        layer3d[i] = 20u | (40u << 8) | (60u << 16) | (31u << 24); /* opaque */
    }
    layer3d[28 * 256 + 20] = 0u; /* transparent where the tile is */
    memset(&latch, 0, sizeof(latch));
    khdays_gfx_describe(io, palette, oam, 0, &latch, desc);
    memset(&m, 0, sizeof(m));
    m.vram = &pages;
    m.palette = palette;
    m.layer3d = layer3d;
    memcpy(split, desc, sizeof(split));
    split[KD_FLAGS] |= KDF_SPLIT | KDF_FINE;
    {
        const uint32_t shown = khdays_gfx_pixel_cpu(split, &m, 20, 28);
        CHECK((shown >> 24) == 1u);                                   /* shown, nothing of the 3D */
        CHECK((shown & 0xffffffu) == 0xffu);                          /* the tile's red */
        CHECK((khdays_gfx_pixel_cpu(split, &m, 21, 28) >> 24) == 0u); /* the tile under the opaque 3D */
        m.layer3d = NULL;
        CHECK((khdays_gfx_pixel_cpu(split, &m, 20, 28) >> 24) == 0u); /* no 3D given: as an opaque one */
    }
}

/* The 3D on both screens with a swap skipped now and then (the game's frame late as a dialog opens):
 * the layout must stay (it used to flicker for two VBlanks after each skip). Then 8 VBlanks with no
 * swap and engine A below give the lower screen's 3D alone. */
static void test_swap_skips(void)
{
    static const char screens[] = "101010010100100101010"; /* engine A: 1 the upper screen, 0 the lower */
    KhdaysSituationInputs in;
    memset(&in, 0, sizeof(in));
    in.scene = 2; /* the field */
    in.camp_screen = -2;
    in.mission_start = -1;
    in.event_status = -1;
    uint32_t history = 0;
    int upper = 1;
    const KhdaysSituationFrame frame = {&in, 0, 0, 0, NULL, NULL};
    KhdaysSituation s;
    for (int i = 0; screens[i] != '\0'; ++i) {
        const int now = screens[i] == '1';
        in.engine_swaps = khdays_situation_swaps(&history, now != upper);
        in.powcnt1 = now ? 0x8000u : 0u;
        upper = now;
        khdays_situation_decide(&frame, &s);
        if (i >= 2) {
            CHECK(s.scene == KHDAYS_SIT_FIELD_3D_BOTH);
            CHECK((s.states & (1u << KHDAYS_ST_LOWER_3D)) == 0u);
        }
    }
    in.powcnt1 = 0;
    for (int i = 0; i < 8; ++i) {
        in.engine_swaps = khdays_situation_swaps(&history, 0);
        khdays_situation_decide(&frame, &s);
        CHECK(s.scene == (i < 7 ? KHDAYS_SIT_FIELD_3D_BOTH : KHDAYS_SIT_FIELD));
    }
    CHECK((s.states & (1u << KHDAYS_ST_LOWER_3D)) != 0u);
}

static void test_weighted_image(void)
{
    const uint32_t background = 0x01000000u | 0x00ffffffu;
    memset(shape_words, 0, sizeof(shape_words));
    shape(0, KOF_IMAGE | KOF_WEIGHT, 0, 0, 20, 20, 0, 0, 1);
    image_colour = 0x80000000u;
    use_image_colours = 0;
    put_float(shape_words[0], KO_FADE, 5);
    uint32_t edge = ko_pixel(0, 10, 1, background, 1, KOB_DEFAULT, 0, 0, 171, 1);
    uint32_t middle = ko_pixel(10, 10, 1, background, 1, KOB_DEFAULT, 0, 0, 171, 1);
    uint32_t ramp = ko_pixel(2.5f, 10, 1, background, 1, KOB_DEFAULT, 0, 0, 171, 1);
    CHECK((edge & 255u) == 255u && (middle & 255u) == 127u && (ramp & 255u) == 191u);
}

static void test_mission_panel_image(void)
{
    KhdaysFramePackage *p = calloc(1, sizeof(*p));
    CHECK(p != NULL);
    if (!p)
        return;
    uint32_t records[KE_WORDS * 2] = {0};
    for (int i = 0; i < 2; ++i) {
        uint32_t *r = records + i * KE_WORDS;
        r[KE_SLOT] = KES_DIRECT | KES_FINE | KES_PICTURE | 2;
        r[KE_IMAGE] = 71 + i;
        r[KE_IW] = 400;
        r[KE_IH] = 100;
        put_float(r, KE_X, -2);
        put_float(r, KE_Y, -3);
        put_float(r, KE_W, 200);
        put_float(r, KE_H, 50);
    }
    put_float(records, KE_Y, 152 - 3);
    p->upper = 0;
    p->elements[1].records = records;
    p->elements[1].count = 2;
    p->mission_panel_state = 3;
    p->mission_panel_backdrop = 73;
    p->mission_panel_image.words[KSI_OUTPUT] = 71;
    p->mission_panel_image.words[KSI_ANCHORED] = 1;
    put_float(p->mission_panel_image.words, KSI_ANCHOR_Y, 152);
    p->information_images[1].words[KSI_OUTPUT] = 72;
    p->information_images[1].words[KSI_ANCHORED] = 1;
    p->information_state = KHDAYS_INFORMATION_AVAILABLE | 0x202u;
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    khdays_composition_set_hud_size(4);
    KhdaysSituation s = {KHDAYS_SIT_FIELD,
                         (1u << KHDAYS_ST_HUD) | (1u << KHDAYS_ST_MISSION_PANEL) | (1u << KHDAYS_ST_MISSION_PANEL_MOD)};
    khdays_onescreen_information(p, &s);
    KhdaysComposition c;
    khdays_composition_for(&s, 342, &c);
    for (int i = 0; i < c.shape_count; ++i) {
        const uint32_t *shape = c.shapes[i].words;
        CHECK(!(word_float(shape, KO_SRC_Y) == 152 && word_float(shape, KO_SRC_X) == 4));
    }
    khdays_onescreen_images(p, &s, &c);
    CHECK(c.shapes[0].words[KO_KEYS] == 72 && c.shapes[1].words[KO_KEYS] == 71);
    const float factor = khdays_composition_hud_factor();
    CHECK(fabsf(word_float(c.shapes[1].words, KO_DST_Y1) - (5 - 3 * .766667f) * factor) < .001f);
    int backdrop = 0;
    for (int i = 0; i < c.shape_count; ++i)
        if ((c.shapes[i].words[KO_FLAGS] & KOF_IMAGE) && c.shapes[i].words[KO_KEYS] == 73)
            ++backdrop;
    CHECK(backdrop == 1);
    // Anchors follow the viewport after HUD scaling, using resting artwork rather than canvas extents.
    p->elements[1].count = 1;
    const int columns[] = {256, 308, 342, 460, 768};
    for (int size = 1; size <= 8; ++size) {
        khdays_composition_set_hud_size(size);
        const float scale = .766667f * khdays_composition_hud_factor();
        for (unsigned a = 1; a <= 9; ++a) {
            const float ax = (float)((a - 1) % 3) * .5f, ay = (float)((a - 1) / 3) * .5f;
            p->mission_panel_image.words[KSI_PANEL_ANCHOR] = a;
            put_float(records, KE_X, 256 * ax - (20 + 100 * ax) + 7);
            put_float(records, KE_Y, 192 * ay - (10 + 20 * ay) - 9);
            for (unsigned col = 0; col < sizeof(columns) / sizeof(columns[0]); ++col)
                for (int aligned = 0; aligned < 2; ++aligned) {
                    p->hud_options = aligned ? KHDAYS_HUD_ALIGN_EDGES : 0;
                    khdays_composition_for(&s, columns[col], &c);
                    khdays_onescreen_images(p, &s, &c);
                    int found = 0;
                    for (int j = 0; j < c.shape_count; ++j) {
                        const uint32_t *shape = c.shapes[j].words;
                        if (!(shape[KO_FLAGS] & KOF_IMAGE) || shape[KO_KEYS] != 71)
                            continue;
                        ++found;
                        CHECK(fabsf(word_float(shape, KO_DST_X1) + (20 + 100 * ax) * scale -
                                    (columns[col] * ax + 7 * scale)) < .001f);
                        CHECK(fabsf(word_float(shape, KO_DST_Y1) + (10 + 20 * ay) * scale - (192 * ay - 9 * scale)) <
                              .001f);
                        CHECK(fabsf(word_float(shape, KO_DST_X2) - word_float(shape, KO_DST_X1) - 200 * scale) < .001f);
                    }
                    CHECK(found == 1);
                }
        }
    }
    khdays_composition_set_hud_size(4);
    p->hud_options = 0;
    p->mission_panel_image.words[KSI_PANEL_ANCHOR] = 0;
    put_float(records, KE_X, -2);
    put_float(records, KE_Y, 152 - 3);
    // With native INFORMATION, its crop still covers the independent mission panel.
    p->information_state = 0;
    p->elements[1].count = 1;
    s.states &= ~(1u << KHDAYS_ST_INFORMATION_MOD);
    s.states |= 1u << KHDAYS_ST_INFO_UPPER;
    khdays_composition_for(&s, 342, &c);
    khdays_onescreen_images(p, &s, &c);
    CHECK(!(c.shapes[0].words[KO_FLAGS] & KOF_IMAGE));
    CHECK(word_float(c.shapes[0].words, KO_SRC_Y) == 0 && word_float(c.shapes[0].words, KO_SRC_H) == 24);
    // No replacement restores the existing cropped panel.
    p->mission_panel_state = 0;
    s.states &= ~(1u << KHDAYS_ST_MISSION_PANEL_MOD);
    khdays_composition_for(&s, 342, &c);
    int native = 0;
    for (int i = 0; i < c.shape_count; ++i)
        if (word_float(c.shapes[i].words, KO_SRC_Y) == 152 && word_float(c.shapes[i].words, KO_SRC_X) == 4)
            ++native;
    CHECK(native == 2);
    free(p);
}

static void test_minimap_image(void)
{
    KhdaysFramePackage *p = calloc(1, sizeof(*p));
    CHECK(p != NULL);
    if (!p)
        return;
    uint32_t record[KE_WORDS] = {0};
    record[KE_SLOT] = KES_DIRECT | KES_FINE | KES_PICTURE | KES_NATIVE_FADE | 2;
    record[KE_IMAGE] = 90;
    record[KE_IW] = 240;
    record[KE_IH] = 160;
    put_float(record, KE_X, 104);
    put_float(record, KE_Y, 56);
    put_float(record, KE_W, 120);
    put_float(record, KE_H, 80);
    p->elements[1].records = record;
    p->elements[1].count = 1;
    p->minimap_images[0].words[KSI_OUTPUT] = 90;
    p->minimap_images[0].words[KSI_ANCHORED] = 1;
    put_float(p->minimap_images[0].words, KSI_ANCHOR_X, 164);
    put_float(p->minimap_images[0].words, KSI_ANCHOR_Y, 96);
    mods_file = KHDAYS_PORT_COMPOSITIONS_FILE;
    khdays_composition_load();
    KhdaysSituation s = {KHDAYS_SIT_FIELD, (1u << KHDAYS_ST_HUD) | (1u << KHDAYS_ST_MISSION_PANEL)};
    for (int size = 1; size <= 8; size *= 2) {
        khdays_composition_set_hud_size(size);
        KhdaysComposition c;
        khdays_composition_for(&s, 342, &c);
        KhdaysShape map = {0};
        for (int i = 0; i < c.shape_count; ++i)
            if ((c.shapes[i].words[KO_FLAGS] & KOF_LOWER) && word_float(c.shapes[i].words, KO_SRC_X) == 112 &&
                word_float(c.shapes[i].words, KO_SRC_Y) == 64)
                map = c.shapes[i];
        CHECK(map.words[KO_KEY_COUNT] != 0);
        khdays_onescreen_images(p, &s, &c);
        CHECK(image_shapes(&c) == 1);
        for (int i = 0; i < c.shape_count; ++i) {
            const uint32_t *w = c.shapes[i].words;
            if (!(w[KO_FLAGS] & KOF_IMAGE) || w[KO_KEYS] != 90)
                continue;
            CHECK(word_float(w, KO_SRC_X) == 16 && word_float(w, KO_SRC_Y) == 16);
            CHECK(word_float(w, KO_SRC_W) == 208 && word_float(w, KO_SRC_H) == 128);
            CHECK(word_float(w, KO_DST_X1) == word_float(map.words, KO_DST_X1));
            CHECK(word_float(w, KO_DST_Y1) == word_float(map.words, KO_DST_Y1));
            CHECK(!(w[KO_FLAGS] & KOF_SEE_THROUGH));
            CHECK(w[KO_FLAGS] & KOF_WEIGHT);
            CHECK(word_float(w, KO_FADE) == word_float(map.words, KO_FADE));
            CHECK(word_float(w, KO_IMAGE_ALPHA) == 1);
            memset(shape_words, 0, sizeof(shape_words));
            memcpy(shape_words[0], w, KO_WORDS * sizeof(*w));
            use_image_colours = 0;
            float x = (word_float(w, KO_DST_X1) + word_float(w, KO_DST_X2)) / 2;
            float y = (word_float(w, KO_DST_Y1) + word_float(w, KO_DST_Y2)) / 2;
            for (int colour = 0; colour < 3; ++colour) {
                image_colour = colour == 0 ? 0xffffffffu : colour == 1 ? 0xffefefefu : 0xff0000ffu;
                CHECK(ko_pixel(x, y, 1, 0x01000000u, 1, KOB_DEFAULT, 0, 0, 171, 1) == (image_colour & 0xffffffu));
                CHECK(fabsf(last_image_x - 120) < .001f && fabsf(last_image_y - 80) < .001f);
            }
        }
        p->radar_image.words[KSI_CLEAR] = 1;
        p->radar_image.words[KSI_RADAR_MASK] = 99;
        p->radar_image.words[KSI_OUTPUT] = 91;
        p->radar_image.words[KSI_WIDTH] = p->radar_image.words[KSI_HEIGHT] = 192;
        put_float(p->radar_image.words, KSI_RADAR_SCALE, .65f);
        khdays_composition_for(&s, 342, &c);
        khdays_onescreen_images(p, &s, &c);
        CHECK(image_shapes(&c) == 1);
        int found = 0;
        for (int i = 0; i < c.shape_count; ++i) {
            const uint32_t *w = c.shapes[i].words;
            if ((w[KO_FLAGS] & KOF_IMAGE) && w[KO_KEYS] == 91) {
                float side = 96 * .65f * .766667f * khdays_composition_hud_factor();
                CHECK(fabsf(word_float(w, KO_DST_X2) - word_float(map.words, KO_DST_X2)) < .001f);
                CHECK(fabsf(word_float(w, KO_DST_X1) - (word_float(map.words, KO_DST_X2) - side)) < .001f);
                CHECK(fabsf(word_float(w, KO_DST_Y2) - word_float(w, KO_DST_Y1) - side) < .001f);
                CHECK(word_float(w, KO_SRC_X) == 0 && word_float(w, KO_SRC_W) == 192);
                CHECK(!(w[KO_FLAGS] & (KOF_WEIGHT | KOF_SEE_THROUGH)) && word_float(w, KO_FADE) == 0);
                ++found;
            }
            CHECK((w[KO_FLAGS] & KOF_IMAGE) || word_float(w, KO_SRC_X) != 112 || word_float(w, KO_SRC_Y) != 64);
            CHECK((w[KO_FLAGS] & KOF_IMAGE) || word_float(w, KO_SRC_X) != 186 || word_float(w, KO_SRC_Y) != 156);
        }
        CHECK(found == 1);
        memset(&p->radar_image, 0, sizeof(p->radar_image));
    }
    khdays_composition_set_hud_options(KHDAYS_HUD_ALIGN_EDGES);
    for (int radar = 0; radar < 2; ++radar) {
        p->radar_image.words[KSI_CLEAR] = radar;
        p->radar_image.words[KSI_RADAR_MASK] = radar ? 99 : 0;
        p->radar_image.words[KSI_OUTPUT] = 91;
        p->radar_image.words[KSI_WIDTH] = p->radar_image.words[KSI_HEIGHT] = 192;
        put_float(p->radar_image.words, KSI_RADAR_SCALE, .65f);
        float gameplay[4] = {0};
        for (int paused = 0; paused < 2; ++paused) {
            KhdaysSituation scene = {paused ? KHDAYS_SIT_PAUSE : KHDAYS_SIT_FIELD, s.states};
            KhdaysComposition c;
            khdays_composition_for(&scene, 342, &c);
            khdays_onescreen_images(p, &scene, &c);
            int found = 0;
            for (int i = 0; i < c.shape_count; ++i) {
                const uint32_t *w = c.shapes[i].words;
                if (!(w[KO_FLAGS] & KOF_IMAGE) || w[KO_KEYS] != (radar ? 91u : 90u))
                    continue;
                for (int coordinate = 0; coordinate < 4; ++coordinate) {
                    const float value = word_float(w, KO_DST_X1 + coordinate);
                    if (!paused)
                        gameplay[coordinate] = value;
                    else
                        CHECK(fabsf(value - gameplay[coordinate]) < .001f);
                }
                ++found;
            }
            CHECK(found == 1);
        }
    }
    khdays_composition_set_hud_options(0);
    free(p);
}

int main(void)
{
    test_places();
    test_real_file();
    test_field_hud_reference();
    test_hud_edge_alignment();
    test_pause_target_placement();
    test_inside();
    CHECK(outside_pieces == 0 && inside_pieces > 1000);
    test_apart();
    CHECK(overlaps == 0);
    if (getenv("KHDAYS_PRINT_SHAPES") != NULL) {
        printf("pieces inside: %d\n", inside_pieces);
    }
    test_title_backdrop();
    test_title_prompt();
    test_steps();
    test_independent_image();
    test_obtained_image();
    test_native_hud_fade();
    test_information_image();
    test_mission_panel_image();
    test_minimap_image();
    test_weighted_image();
    test_hud_image_layers();
    test_split();
    test_split_under_3d();
    test_swap_skips();
    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    printf("one screen: all checks passed\n");
    return 0;
}
