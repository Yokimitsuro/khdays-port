/* Exercise regenerated SDK drawing and measurement against synthetic NFTR data. */
#include "khdays_references.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    signed char dx, dy;
} TextDir;
typedef struct {
    int width, height;
} TextRect;
extern TextRect NNSi_G2dFontGetTextRect(khdays_ref font, int hspace, int vspace, khdays_ref text);
extern void Text_AlignAnchor(khdays_ref ctx, int x, int y, int color, int flags, int text, TextDir dir);
extern void Text_DrawLine(khdays_ref ctx, int x, int y, int color, int text, khdays_ref next, TextDir dir);
extern void Text_DrawAlignedLines(khdays_ref ctx, int x, int y, int width, int color, unsigned flags, int text,
                                  TextDir dir);
extern int NNSi_G2dFontGetStringWidth(khdays_ref font, int spacing, khdays_ref text, khdays_ref next);
extern double HD_FontLineWidth(int font, int spacing, int text, khdays_ref next);
extern int HD_FontCounter(khdays_ref surface, int row, int placed, int owned);
extern int HD_FontTime(khdays_ref surface, int layout, int x, int y, unsigned hours, unsigned minutes, unsigned seconds,
                       khdays_ref separator);
unsigned char *khdays_window;
extern void HD_FontFitShadow(khdays_ref surface, int x, int y, int depth, khdays_ref text, int shadow, int layout);
/* The scalar layout tests do not capture canvases; compositor reflow is tested separately. */
int khdays_ui_text_layout_begin(void)
{
    return 0;
}
void khdays_ui_text_layout_actual(int token)
{
    (void)token;
}
void khdays_ui_text_layout_end(int token)
{
    (void)token;
}
int khdays_ui_text_layout_glyph(const void *owner, const void *font, double x, double y, unsigned ch)
{
    (void)owner;
    (void)font;
    (void)x;
    (void)y;
    (void)ch;
    return 0;
}
double khdays_ui_text_layout_x(double x)
{
    return x;
}
double khdays_ui_text_layout_y(double y)
{
    return y;
}
static khdays_ref widths;
static int original_second;
static int failures, custom, custom_width = 12, bearing_case, sized_case, count, xs[2048], ys[2048];
static double exact_xs[2048], exact_ys[2048], pending_x, pending_y;
static int fractional_case, fractional_bearing, fractional_kerning;
static double fractional_scale = .35;
static int numeric_case, numeric_enabled, numeric_fallback, colours[2048];
static double fit_targets[2], fit_factor = 1, fit_origin, fit_left, fit_offset;
static int fit_begins;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "line %d: %s\n", __LINE__, #c);                                                            \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

khdays_ref khdays_reference(const void *host)
{
    if (!host)
        return 0;
    uintptr_t offset = (uintptr_t)host - (uintptr_t)khdays_window;
    if (offset > UINT32_MAX)
        abort();
    return (khdays_ref)offset;
}
static int split(khdays_ref cursor)
{
    khdays_ref *at = KHDAYS_W(cursor);
    int c = *(unsigned short *)KHDAYS_W(*at);
    *at += 2;
    return c;
}
static int draw(int owner, khdays_ref font, int x, int y, int color, khdays_ref glyph)
{
    (void)owner;
    (void)font;
    (void)color;
    khdays_ref metrics = *(khdays_ref *)KHDAYS_W(glyph);
    CHECK(((unsigned char *)KHDAYS_W(metrics))[1] == 4);
    CHECK(((unsigned char *)KHDAYS_W(metrics))[2] == 6);
    if (count < 2048) {
        xs[count] = x;
        ys[count] = y;
        exact_xs[count] = pending_x;
        exact_ys[count] = pending_y;
        colours[count] = color;
    }
    ++count;
    return 0;
}
khdays_function_pointer khdays_function(khdays_ref ref)
{
    union {
        int (*splitter)(khdays_ref);
        int (*drawer)(int, khdays_ref, int, int, int, khdays_ref);
        khdays_function_pointer function;
    } result;
    if (ref == 1)
        result.splitter = split;
    else if (ref == 2)
        result.drawer = draw;
    else
        abort();
    return result.function;
}
unsigned short NNS_G2dFontFindGlyphIndex(khdays_ref font, unsigned code)
{
    (void)font;
    if (numeric_case)
        return (unsigned short)code;
    if (fractional_case) {
        const char *chars = "pisqueak.@";
        const char *found = strchr(chars, (int)code);
        return found ? (unsigned short)(found - chars) : 0;
    }
    return code == 'A' ? 0 : 1;
}
khdays_ref NNS_G2dFontGetCharWidthsFromIndex(khdays_ref font, unsigned index)
{
    (void)font;
    return widths + (index % 2) * 3;
}
int khdays_ui_text_metric(const void *font, int index, int field, int fallback)
{
    CHECK(font != NULL);
    if (original_second && index == 1)
        return fallback;
    if (sized_case) {
        if (field == 4)
            return 1;
        if (field == 0)
            return index == 0 ? (sized_case == 2 ? 1 : -1) : 2;
        if (field == 1)
            return 9;
        if (field == 2)
            return index == 0 ? 10 : 6;
        if (field == 3)
            return 12;
        if (field == 6)
            return 16;
    }
    if (!custom)
        return fallback;
    if (bearing_case) {
        if (field == 0)
            return bearing_case == 1 ? 10 : 1;
        if (field == 1)
            return bearing_case == 2 ? 0 : 4;
        if (field == 2)
            return 6;
    }
    if (field == 0)
        return index == 0 ? -2 : 1;
    if (field == 1)
        return index == 0 ? custom_width : 5;
    if (field == 2)
        return index == 0 ? 9 : 7;
    return fallback;
}
int khdays_ui_text_extent(const void *font, int index, int left, int width, int advance)
{
    int a = khdays_ui_text_metric(font, index, 2, advance);
    if (original_second && index == 1)
        return sized_case ? left + width : advance;
    (void)width;
    return sized_case ? khdays_ui_text_metric(font, index, 0, left) + 9 : a;
}
int khdays_ui_text_kerning(const void *font, int left, int right)
{
    CHECK(font != NULL);
    if (original_second && (left != 'A' || right != 'A'))
        return 0;
    return custom && !bearing_case && left == 'A' && right == 'V' ? (sized_case ? -1 : -2) : 0;
}

double khdays_ui_text_metric_f(const void *font, int index, int field, double fallback)
{
    if (numeric_case) {
        if (numeric_fallback && index == ':')
            return fallback;
        if (field == 0)
            return 0;
        if (field == 2)
            return (index == '0' ? 11 : index == ':' ? 3 : 10) * .41;
        return fallback;
    }
    if (!fractional_case)
        return khdays_ui_text_metric(font, index, field, (int)fallback);
    if (index == 9)
        return fallback;
    static const int advances[] = {11, 7, 10, 12, 11, 9, 11, 9, 8};
    if (field == 4)
        return 1;
    if (field == 0)
        return fractional_bearing * fractional_scale;
    if (field == 1 || field == 5)
        return 24 * fractional_scale;
    if (field == 6 || field == 3)
        return 32 * fractional_scale;
    if (field == 2 && index >= 0)
        return advances[index] * fractional_scale;
    return fallback;
}
double khdays_ui_text_kerning_f(const void *font, int left, int right)
{
    if (numeric_case)
        return 0;
    if (fractional_case && fractional_kerning && left == 'p' && right == 'i')
        return -fractional_scale;
    return fractional_case ? 0 : khdays_ui_text_kerning(font, left, right);
}
double khdays_ui_text_extent_f(const void *font, int index, int left, int width, int advance)
{
    if (numeric_case)
        return numeric_fallback && index == ':' ? advance : 18 * .41;
    return fractional_case ? (index == 9 ? left + width : (24 + fractional_bearing) * fractional_scale)
                           : khdays_ui_text_extent(font, index, left, width, advance);
}
int khdays_ui_text_round(double value)
{
    return (int)lround(value);
}
void khdays_ui_text_position(const void *font, double x, double y)
{
    if (font) {
        pending_x = fit_factor < 1 ? fit_origin + (x - fit_origin - fit_left) * fit_factor + fit_offset : x;
        pending_y = y;
    }
}
double khdays_ui_text_pen_x(int x)
{
    return x;
}

int khdays_ui_text_numeric(const void *font, int layout)
{
    CHECK(font != NULL && layout >= 0 && layout < 3);
    return numeric_enabled;
}

double khdays_ui_text_fit_width(const void *font, int layout, double *offset)
{
    CHECK(font != NULL && layout >= 0 && layout < 2);
    *offset = 0;
    return fit_targets[layout];
}
int khdays_ui_text_ink(const void *font, int glyph, int w, int h, int ink, double *bounds)
{
    (void)font;
    (void)glyph;
    (void)w;
    (void)h;
    (void)ink;
    bounds[0] = 0;
    bounds[1] = 3.69;
    return 1;
}
void khdays_ui_text_fit_begin(const void *font, double origin, double left, double factor, double offset)
{
    (void)font;
    fit_origin = origin;
    fit_left = left;
    fit_factor = factor;
    fit_offset = offset;
    ++fit_begins;
}
void khdays_ui_text_fit_end(void)
{
    fit_factor = 1;
}

static void fitted_layout_tests(khdays_ref font, khdays_ref owner)
{
    unsigned int surface[12] = {0};
    unsigned short name[] = {'G', 'e', 'a', 'r', ' ', 'C', 'o', 'm', 'p', 'o', 'n', 'e', 'n', 't', ' ', 'C', 0};
    unsigned short short_name[] = {'A', 'e', 'r', 'i', 'a', 'l', ' ', 'T', 'e', 'c', 'h', '+', 0};
    surface[7] = owner;
    surface[8] = font;
    numeric_case = 1;
    count = 0;
    HD_FontFitShadow(KHDAYS_REF(surface), 0, 3, 2, KHDAYS_REF(name), 1, 0);
    CHECK(count == 32 && fit_begins == 0 && fabs(exact_xs[31] - 61.5) < 1e-9);
    fit_targets[0] = 53;
    count = 0;
    HD_FontFitShadow(KHDAYS_REF(surface), 0, 3, 2, KHDAYS_REF(name), 1, 0);
    const double factor = 53 / (61.5 + 3.69 + 1);
    CHECK(count == 32 && fit_begins == 1 && fit_factor == 1);
    CHECK(fabs(exact_xs[31] - 61.5 * factor) < 1e-9);
    CHECK(fabs(exact_xs[17] - exact_xs[16] - 4.1 * factor) < 1e-9);
    CHECK(fabs(exact_xs[0] - exact_xs[16] - factor) < 1e-9 && exact_ys[0] - exact_ys[16] == 1);
    CHECK(fabs(exact_xs[15] + 3.69 * factor - 53) < 1e-9);
    count = 0;
    HD_FontFitShadow(KHDAYS_REF(surface), 0, 3, 2, KHDAYS_REF(short_name), 1, 0);
    CHECK(count == 24 && fit_begins == 1 && fabs(exact_xs[23] - 45.1) < 1e-9);
    count = 0;
    HD_FontFitShadow(KHDAYS_REF(surface), 0, 3, 2, KHDAYS_REF(name), 1, 1);
    CHECK(count == 32 && fit_begins == 1 && fabs(exact_xs[31] - 61.5) < 1e-9);
    fit_targets[1] = 64;
    count = 0;
    HD_FontFitShadow(KHDAYS_REF(surface), 0, 3, 2, KHDAYS_REF(name), 1, 1);
    CHECK(count == 32 && fit_begins == 2);
    CHECK(fabs(exact_xs[15] + 3.69 * (64 / 66.19) - 64) < 1e-9);
    numeric_case = 0;
}

static void numeric_layout_tests(khdays_ref font, khdays_ref owner)
{
    unsigned int surface[12] = {0};
    unsigned short separator[] = {':', 0};
    surface[0] = font;
    surface[6] = owner - 8;
    surface[7] = owner;
    surface[8] = font;
    numeric_case = 1;
    fractional_case = 0;
    count = 0;
    CHECK(!HD_FontCounter(KHDAYS_REF(surface), 0, 0, 30) && count == 0);
    CHECK(!HD_FontTime(KHDAYS_REF(surface), 1, 10, 20, 71, 52, 8, KHDAYS_REF(separator)) && count == 0);
    numeric_enabled = 1;
    for (int value = 30; value <= 99; value += 69) {
        count = 0;
        CHECK(HD_FontCounter(KHDAYS_REF(surface), 2, 0, value));
        CHECK(count == 10);
        CHECK(fabs(exact_xs[6] - exact_xs[5] - 4.1) < 1e-9);
        CHECK(fabs(exact_xs[9] - exact_xs[8] - 4.1) < 1e-9);
        CHECK(fabs(exact_xs[9] + (value == 30 ? 4.51 : 4.1) - 125) < 1e-9);
        CHECK(exact_ys[5] == 35 && colours[5] == 0xf3 && colours[7] == 0xf3 && colours[8] == 0xf1);
        CHECK(fabs(exact_xs[0] - exact_xs[5] - 1) < 1e-9 && colours[0] == 0xf2);
    }
    for (int layout = 1; layout <= 2; ++layout) {
        for (int native_separator = 0; native_separator < 2; ++native_separator) {
            numeric_fallback = native_separator;
            count = 0;
            CHECK(HD_FontTime(KHDAYS_REF(surface), layout, 10, 20, 71, 52, 8, KHDAYS_REF(separator)));
            CHECK(count == 16);
            CHECK(fabs(exact_xs[9] - exact_xs[8] - 4.1) < 1e-9);
            CHECK(fabs(exact_xs[15] + 4.1 - 203) < 1e-9);
            CHECK(fabs(exact_xs[11] - exact_xs[10] - (native_separator ? 5 : 1.23)) < 1e-9);
            CHECK(exact_ys[8] == 20 && colours[0] == 1 && colours[8] == 2);
            if (native_separator)
                CHECK(xs[10] == khdays_ui_text_round(exact_xs[10]));
            count = 0;
            CHECK(HD_FontTime(KHDAYS_REF(surface), layout, 10, 28, 999, 30, 0, KHDAYS_REF(separator)));
            CHECK(count == 18 && exact_ys[9] == 28);
            CHECK(fabs(exact_xs[17] + 4.51 - 203) < 1e-9);
        }
    }
    count = 0;
    CHECK(HD_FontCounter(KHDAYS_REF(surface), 0, -1, 0) && count == 2);
    numeric_fallback = numeric_case = numeric_enabled = 0;
}

int main(void)
{
    unsigned int ctx[4] = {0}, font[2] = {0}, info[4] = {0}, owner[6] = {0}, vtable = 2;
    unsigned char table[24] = {8, 8, 8, 0, 0, 0, 0, 0};
    unsigned char metrics[6] = {1, 4, 6, 1, 4, 6};
    unsigned short text[] = {'A', 'V', '\n', 'A', 0};
    khdays_ref next = 0;
    khdays_window = (unsigned char *)((uintptr_t)&ctx & ~UINT64_C(0xffffffff));
    widths = KHDAYS_REF(metrics);
    font[0] = KHDAYS_REF(info);
    font[1] = 1;
    info[2] = KHDAYS_REF(table);
    owner[5] = KHDAYS_REF(&vtable);
    ctx[0] = KHDAYS_REF(owner);
    ctx[1] = KHDAYS_REF(font);
    ctx[2] = 1;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(text), KHDAYS_REF(&next), (TextDir){1, 0});
    CHECK(count == 2 && xs[0] == 11 && xs[1] == 18 && ys[0] == 20);
    CHECK(next == KHDAYS_REF(text + 3));
    CHECK(NNSi_G2dFontGetStringWidth(KHDAYS_REF(font), 1, KHDAYS_REF(text), KHDAYS_REF(&next)) == 13);
    CHECK(next == KHDAYS_REF(text + 3));
    custom = 1;
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(text), 0, (TextDir){1, 0});
    CHECK(count == 2 && xs[0] == 8 && xs[1] == 19);
    CHECK(NNSi_G2dFontGetStringWidth(KHDAYS_REF(font), 1, KHDAYS_REF(text), 0) == 15);
    custom_width = 30;
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(text), 0, (TextDir){1, 0});
    CHECK(count == 2 && xs[0] == 8 && xs[1] == 19);
    CHECK(NNSi_G2dFontGetStringWidth(KHDAYS_REF(font), 1, KHDAYS_REF(text), 0) == 15);
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(text), 0, (TextDir){-1, 0});
    CHECK(count == 2 && xs[0] == 8 && xs[1] == 3);
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(text), 0, (TextDir){0, 1});
    CHECK(count == 2 && ys[0] == 20 && ys[1] == 28);
    // Uniform bearings must stay visible with left, center and right alignment.
    text[2] = 0;
    for (int alignment = 0; alignment < 3; ++alignment) {
        unsigned flags = alignment == 1 ? 0x400u : alignment == 2 ? 0x800u : 0u;
        custom = 0;
        count = 0;
        Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, flags, (int)KHDAYS_REF(text), (TextDir){1, 0});
        CHECK(count == 2);
        int old_x0 = xs[0], old_x1 = xs[1];
        custom = 1;
        bearing_case = 1;
        count = 0;
        Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, flags, (int)KHDAYS_REF(text), (TextDir){1, 0});
        CHECK(count == 2 && xs[0] == old_x0 + 9 && xs[1] == old_x1 + 9);
        CHECK(NNSi_G2dFontGetStringWidth(KHDAYS_REF(font), 1, KHDAYS_REF(text), 0) == 13);
        bearing_case = 2;
        count = 0;
        Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, flags, (int)KHDAYS_REF(text), (TextDir){1, 0});
        int shift = 0; // Legacy mode retains advance-based alignment.
        CHECK(count == 2 && xs[0] == old_x0 + shift && xs[1] == old_x1 + shift);
        CHECK(xs[1] - xs[0] == 7);
        CHECK(NNSi_G2dFontGetStringWidth(KHDAYS_REF(font), 1, KHDAYS_REF(text), 0) == 13);
    }
    bearing_case = 0;
    sized_case = 1;
    count = 0;
    Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, 0, (int)KHDAYS_REF(text), (TextDir){1, 0});
    CHECK(count == 2 && xs[0] == 9 && xs[1] == 22);
    unsigned short proportional[] = {'A', 'V', 'A', 0};
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(proportional), 0, (TextDir){1, 0});
    CHECK(count == 3 && xs[0] == 9 && xs[1] == 22 && xs[2] == 26);
    unsigned short split_pair[] = {'A', '\n', 'V', 0};
    count = 0;
    Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, 0, (int)KHDAYS_REF(split_pair), (TextDir){1, 0});
    CHECK(count == 2 && xs[0] == 9 && xs[1] == 12 && ys[1] == 32);
    CHECK(NNSi_G2dFontGetStringWidth(KHDAYS_REF(font), 1, KHDAYS_REF(text), 0) == 22);
    count = 0;
    Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, 0x800, (int)KHDAYS_REF(text), (TextDir){1, 0});
    CHECK(count == 2 && xs[0] == 38 && xs[1] == 51 && xs[1] + 9 == 60);
    count = 0;
    Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, 0x400, (int)KHDAYS_REF(text), (TextDir){1, 0});
    CHECK(count == 2 && xs[0] == 24 && xs[1] == 37);
    sized_case = 2;
    CHECK(NNSi_G2dFontGetStringWidth(KHDAYS_REF(font), 1, KHDAYS_REF(text), 0) == 20);
    count = 0;
    Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, 0x800, (int)KHDAYS_REF(text), (TextDir){1, 0});
    CHECK(count == 2 && xs[0] == 40 && xs[1] == 51);
    text[2] = '\n';
    count = 0;
    Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, 0x800, (int)KHDAYS_REF(text), (TextDir){1, 0});
    CHECK(count == 3 && ys[0] == 20 && ys[2] == 32 && xs[2] + 9 == 60);
    TextRect bounds = NNSi_G2dFontGetTextRect(KHDAYS_REF(font), 1, 0, KHDAYS_REF(text));
    CHECK(bounds.width == 20 && bounds.height == 28);
    count = 0;
    Text_AlignAnchor(KHDAYS_REF(ctx), 100, 100, 8, 0x20 | 4, (int)KHDAYS_REF(text), (TextDir){1, 0});
    CHECK(count == 3 && xs[0] == 80 && xs[1] + 9 == 100 && xs[2] == 80);
    CHECK(ys[0] == 72 && ys[2] == 84 && ys[2] + 16 == 100);
    /* Missing original index 1 between HD index 0: native advance/bearing
       survives both isolated and consecutive fallback glyphs, with no HD kerning. */
    unsigned short mixed[] = {'A', 'V', 'V', 'A', 0};
    original_second = 1;
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(mixed), 0, (TextDir){1, 0});
    CHECK(count == 4 && xs[0] == 11 && xs[1] == 22 && xs[2] == 29 && xs[3] == 36);
    CHECK(NNSi_G2dFontGetStringWidth(KHDAYS_REF(font), 1, KHDAYS_REF(mixed), 0) == 34);
    count = 0;
    Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 50, 8, 0x800, (int)KHDAYS_REF(mixed), (TextDir){1, 0});
    CHECK(count == 4 && xs[3] + 9 == 60 && xs[1] - xs[0] == 11 && xs[2] - xs[1] == 7);
    CHECK(memcmp(metrics, (unsigned char[]){1, 4, 6, 1, 4, 6}, 6) == 0);
    original_second = 0;
    fractional_case = 1;
    unsigned short word[] = {'p', 'i', 'p', 's', 'q', 'u', 'e', 'a', 'k', '.', 0};
    const int advances[] = {11, 7, 11, 10, 12, 11, 9, 11, 9, 8};
    for (int scale_case = 0; scale_case < 3; ++scale_case) {
        fractional_scale = scale_case == 0 ? .35 : scale_case == 1 ? .5 : .4;
        count = 0;
        Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(word), 0, (TextDir){1, 0});
        CHECK(count == 10);
        double pen = 10;
        for (int i = 0; i < 10; ++i) {
            CHECK(fabs(exact_xs[i] - pen) < 1e-9);
            CHECK(xs[i] == khdays_ui_text_round(pen));
            pen += advances[i] * fractional_scale + 1; // Intentional game spacing.
        }
        const double width = pen - 10 - 1 - advances[9] * fractional_scale + 24 * fractional_scale;
        CHECK(fabs(HD_FontLineWidth((int)KHDAYS_REF(font), 1, (int)KHDAYS_REF(word), 0) - width) < 1e-9);
        count = 0;
        Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 80, 8, 0x800, (int)KHDAYS_REF(word), (TextDir){1, 0});
        CHECK(fabs(exact_xs[9] + 24 * fractional_scale - 90) < 1e-9);
    }
    fractional_scale = .35;
    unsigned short long_line[321];
    for (int i = 0; i < 320; ++i)
        long_line[i] = 'p';
    long_line[320] = 0;
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(long_line), 0, (TextDir){1, 0});
    CHECK(count == 320 && fabs(exact_xs[319] - (10 + 319 * 4.85)) < 1e-8);
    unsigned short mixed_fractional[] = {'p', '@', 'p', 0};
    fractional_bearing = -2;
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(mixed_fractional), 0, (TextDir){1, 0});
    CHECK(count == 3 && fabs(exact_xs[0] - 9.3) < 1e-9);
    CHECK(xs[1] == 16); // Native bearing 1, advance 6; native bitmap remains on the grid.
    CHECK(fabs(exact_xs[2] - 21.15) < 1e-9);
    fractional_bearing = 2;
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(word), 0, (TextDir){1, 0});
    CHECK(fabs(exact_xs[0] - 10.7) < 1e-9);
    fractional_bearing = 0;
    unsigned short fractional_lines[] = {'p', 'i', '\n', 'p', 0};
    count = 0;
    Text_DrawAlignedLines(KHDAYS_REF(ctx), 10, 20, 80, 8, 0x400, (int)KHDAYS_REF(fractional_lines), (TextDir){1, 0});
    CHECK(count == 3 && fabs(exact_xs[0] - 43.375) < 1e-9 && fabs(exact_xs[2] - 45.8) < 1e-9);
    CHECK(fabs(exact_ys[2] - 31.2) < 1e-9);
    count = 0;
    Text_AlignAnchor(KHDAYS_REF(ctx), 100, 100, 8, 0x10 | 2, (int)KHDAYS_REF(fractional_lines), (TextDir){1, 0});
    CHECK(count == 3 && fabs(exact_xs[0] - 93.375) < 1e-9 && fabs(exact_ys[0] - 88.8) < 1e-9);
    CHECK(fabs(exact_ys[2] - 100) < 1e-9);
    fractional_kerning = 1;
    count = 0;
    Text_DrawLine(KHDAYS_REF(ctx), 10, 20, 8, (int)KHDAYS_REF(fractional_lines), 0, (TextDir){1, 0});
    CHECK(count == 2 && fabs(exact_xs[1] - 14.5) < 1e-9);
    CHECK(fabs(HD_FontLineWidth((int)KHDAYS_REF(font), 1, (int)KHDAYS_REF(fractional_lines), 0) - 12.9) < 1e-9);
    numeric_layout_tests(KHDAYS_REF(font), KHDAYS_REF(owner));
    fitted_layout_tests(KHDAYS_REF(font), KHDAYS_REF(owner));
    printf("HD font layout: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
