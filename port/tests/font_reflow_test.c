/* Run the real SDK layout and retained canvas together with synthetic glyphs. */
#include "../runtime/core/window.h"
#undef KHDAYS_W
void *khdays_reflow_host(unsigned reference);
#define KHDAYS_W(reference) khdays_reflow_host((unsigned)(reference))
#define main canvas_fixture_main
#include "hd_text_test.c"
#undef main

void *khdays_reflow_host(unsigned reference)
{
    if (reference >= 0xffff0000u && reference < 0xffff0040u)
        return (void *)native_hostrefs[reference - 0xffff0000u];
    return khdays_window + reference;
}
static int sdk_next(KhdaysRef cursor)
{
    unsigned *stream = KHDAYS_W(cursor);
    const int ch = *(const unsigned short *)KHDAYS_W(*stream);
    *stream += 2;
    return ch;
}
static int sdk_draw(int owner, KhdaysRef f, int x, int y, int colour, KhdaysRef glyph_ref)
{
    DsCanvas *ds = KHDAYS_W((unsigned)owner);
    const void *gd = KHDAYS_W(glyph_ref);
    khdays_ui_text_glyph_before(ds, KHDAYS_W(f), x, y, colour, gd);
    Canvas shape = {.bpp = ds->dst_bpp, .param = (int)ds->param};
    for (int py = y; py < y + 8; ++py)
        for (int px = x; px < x + 4; ++px)
            if (px >= 0 && py >= 0 && px < ds->area_width * 8 && py < ds->area_height * 8)
                put_pixel(&shape, KHDAYS_W(ds->char_base), px, py, (unsigned)colour & 15u);
    khdays_ui_text_glyph(ds, KHDAYS_W(f), x, y, colour, gd);
    return 0;
}
khdays_function_pointer khdays_function(KhdaysRef reference)
{
    union {
        int (*next)(KhdaysRef);
        int (*draw)(int, KhdaysRef, int, int, int, KhdaysRef);
        khdays_function_pointer function;
    } result;
    if (reference == 1)
        result.next = sdk_next;
    else if (reference == 2)
        result.draw = sdk_draw;
    else
        abort();
    return result.function;
}
typedef struct {
    signed char dx, dy;
} ReflowDir;
extern void Text_AlignAnchor(KhdaysRef ctx, int x, int y, int colour, int flags, int text, ReflowDir dir);

static void sdk_setup(int enabled, int flags)
{
    int preferences[KHDAYS_OPT_COUNT];
    khdays_hud_settings_default(preferences);
    preferences[KHDAYS_OPT_FONT] = enabled;
    khdays_hud_settings_publish(preferences);
    setup(4);
    cc->area_width = cc->param = 16;
    cc->area_height = 6;
    cc->vtable = 888;
    *(unsigned *)(ram + 888) = 2;
    font[1] = 1;
    ram[512] = 1;
    ram[513] = 4;
    ram[514] = 6;
    ram[515] = 2;
    ram[516] = 4;
    ram[517] = 7;
    available_glyphs = 3;
    c = canvas_of(cc, "SDK reflow");
    metric_override = 1;
    unsigned *ctx = (unsigned *)(ram + 900);
    ctx[0] = KHDAYS_REF(cc);
    ctx[1] = KHDAYS_REF(font);
    ctx[2] = 1;
    ctx[3] = 2;
    unsigned short *str = (unsigned short *)(ram + 8192);
    const unsigned short content[] = {'A', 'V', 'A', 10, 'V', 'A', 0};
    memcpy(str, content, sizeof(content));
    Text_AlignAnchor(900, 64, 16, 1, flags, 8192, (ReflowDir){1, 0});
    upload(0, c->bytes);
}
extern void HD_FontFitShadow(KhdaysRef surface, int x, int y, int depth, KhdaysRef text, int shadow, int layout);
static void sdk_fit_setup(int hd)
{
    sdk_setup(hd, 0);
    memset(ram + 1024, 0, c->bytes);
    khdays_ui_text_clear(cc, 0);
    /* The synthetic surface's context is separate from its character buffer. */
    unsigned *ctx = (unsigned *)(ram + 12316);
    ctx[0] = KHDAYS_REF(cc);
    ctx[1] = KHDAYS_REF(font);
    ctx[2] = ctx[3] = 0;
    const unsigned short content[] = {'A', 'V', 'A', 0};
    memcpy(ram + 8192, content, sizeof(content));
    fit_widths[0] = 15;
    HD_FontFitShadow(12288, 32, 3, 2, 8192, 1, 0);
    upload(0, c->bytes);
    fit_widths[0] = 0;
}
static void fitted_reflow(void)
{
    GlyphLayout expected[2][6];
    for (int hd = 0; hd < 2; ++hd) {
        sdk_fit_setup(hd);
        CHECK(c->display_count == 6);
        for (int i = 0; i < 6; ++i)
            expected[hd][i] = c->display_ops[i].layouts[hd];
    }
    for (int initial = 0; initial < 2; ++initial) {
        sdk_fit_setup(initial);
        for (int hd = 0; hd < 2; ++hd) {
            int preferences[KHDAYS_OPT_COUNT];
            khdays_hud_settings_default(preferences);
            preferences[KHDAYS_OPT_FONT] = hd;
            khdays_hud_settings_publish(preferences);
            khdays_ui_text_settings_changed();
            CHECK(c->display_count == 6);
            for (int i = 0; i < 6; ++i) {
                CHECK(c->display_ops[i].x == expected[hd][i].x);
                CHECK(c->display_ops[i].x_scale == expected[hd][i].x_scale);
            }
        }
    }
}

extern int Text_DrawGlyph(int owner, KhdaysRef font_ref, int x, int y, int colour, unsigned short ch);

static void crawl_reflow(void)
{
    for (int initial = 0; initial < 2; ++initial) {
        sdk_setup(initial, 0);
        memset(ram + 1024, 0, c->bytes);
        khdays_ui_text_clear(cc, 0);
        const unsigned short *str = (const unsigned short *)(ram + 8192);
        int cursor = 8;
        khdays_ui_text_crawl_font(font);
        for (int i = 0; i < 2; ++i) {
            khdays_ui_text_crawl_begin(cc, str, i, cursor, 3,
                                       khdays_ui_text_kerning_f(font, i ? str[i - 1] : 0, str[i]));
            const int advance = Text_DrawGlyph((int)KHDAYS_REF(cc), KHDAYS_REF(font), cursor, 3, 1, str[i]);
            cursor += khdays_ui_text_crawl_advance(advance);
        }
        upload(0, c->bytes);
        CHECK(c->display_count == 2);
        int preferences[KHDAYS_OPT_COUNT];
        khdays_hud_settings_default(preferences);
        for (int hd = 0; hd < 2; ++hd) {
            preferences[KHDAYS_OPT_FONT] = hd;
            khdays_hud_settings_publish(preferences);
            khdays_ui_text_settings_changed();
            CHECK(c->display_ops[0].x == (hd ? 6 : 9));
            CHECK(c->display_ops[1].x == (hd ? 19 : 16));
        }
        /* Continue the same crawl after switching; its old integer cursor must not retain the old spacing. */
        khdays_ui_text_crawl_begin(cc, str, 2, cursor, 3, khdays_ui_text_kerning_f(font, str[1], str[2]));
        const int advance = Text_DrawGlyph((int)KHDAYS_REF(cc), KHDAYS_REF(font), cursor, 3, 1, str[2]);
        cursor += khdays_ui_text_crawl_advance(advance);
        CHECK(c->op_count == 3 && c->ops[2].x == 22 && cursor == 35);
        khdays_ui_text_crawl_font(NULL);
    }
}

int main(void)
{
    for (int flags = 0; flags <= 0x20; flags += 0x10) {
        GlyphLayout expected[2][5];
        for (int hd = 0; hd < 2; ++hd) {
            sdk_setup(hd, flags);
            CHECK(c->display_count == 5);
            for (int i = 0; i < 5; ++i)
                expected[hd][i] = c->display_ops[i].layouts[hd];
        }
        for (int initial = 0; initial < 2; ++initial) {
            sdk_setup(initial, flags);
            CHECK(layout_depth == 0 && !layout_probe);
            for (int pass = 0; pass < 4; ++pass) {
                const int hd = pass & 1;
                int preferences[KHDAYS_OPT_COUNT];
                khdays_hud_settings_default(preferences);
                preferences[KHDAYS_OPT_FONT] = hd;
                khdays_hud_settings_publish(preferences);
                khdays_ui_text_settings_changed();
                CHECK(c->display_count == 5);
                for (int i = 0; i < 5; ++i) {
                    const GlyphOp *op = &c->display_ops[i];
                    CHECK(op->x == expected[hd][i].x && op->y == expected[hd][i].y);
                    CHECK(op->w == expected[hd][i].w && op->h == expected[hd][i].h);
                    CHECK(op->paint_w == expected[hd][i].paint_w);
                }
            }
        }
    }
    fitted_reflow();
    crawl_reflow();
    printf("SDK cached font reflow: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
