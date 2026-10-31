"""Fit explicitly configured item-name runs before the existing canvas clips them."""

from font_fractional import COMMON, replace

DRAW = "src/engine/Text_DrawWithShadow.c"
ITEMS = "src/overlays/scenes/ov002_field/Ov002_DrawPanelRowText.c"
MISSION_ROWS = (
    "src/overlays/scenes/ov008_camp_menu/Ov008_DrawMissionRow.c",
    "src/overlays/screens/ov025_camp_menu_2/Ov025_DrawMissionRow.c",
)
DECLARATION = (
    "extern void HD_FontFitShadow(void *surface, int x, int y, int depth, void *text, int shadow, int layout);\n"
)
HELPER = """
extern double khdays_ui_text_fit_width(const void *font, int layout, double *offset);
extern int khdays_ui_text_ink(const void *font, int glyph, int w, int h, int ink, double *bounds);
extern void khdays_ui_text_fit_begin(const void *font, double origin, double left, double factor, double offset);
extern void khdays_ui_text_fit_end(void);
extern unsigned short NNS_G2dFontFindGlyphIndex(int *font, unsigned ch);
extern char *NNS_G2dFontGetCharWidthsFromIndex(int *font, unsigned glyph);

void HD_FontFitShadow(void *surface, int x, int y, int depth, void *text, int shadow, int layout)
{
    int *ctx = (int *)((char *)surface + 0x1c);
    int *font = (int *)ctx[1];
    double offset = 0, target = khdays_ui_text_fit_width(font, layout, &offset);
    double cursor = 0, low = 0, high = 0;
    int enabled = 0;
    if (target > 0 && text) {
        int stream = (int)text, previous = 0, count = 0, ch;
        int (*next)(int *) = (int (*)(int *))font[1];
        unsigned char *table = *(unsigned char **)(*font + 8);
        while ((ch = next(&stream)) != 0) {
            unsigned glyph;
            const signed char *metrics;
            double bounds[2], bearing;
            if (ch == 10 || ++count > 512 || (table[7] != 0 && table[7] != 7)) break;
            glyph = NNS_G2dFontFindGlyphIndex(font, ch);
            if (glyph == 0xffff) glyph = *(unsigned short *)(*font + 2);
            metrics = (const signed char *)NNS_G2dFontGetCharWidthsFromIndex(font, glyph);
            if (!khdays_ui_text_ink(font, glyph, table[0], table[1], (unsigned char)metrics[1], bounds) &&
                (table[0] > 16 || table[1] > 16 || (unsigned char)metrics[1] > 16)) break;
            cursor += khdays_ui_text_kerning_f(font, previous, ch);
            bearing = khdays_ui_text_metric_f(font, glyph, 0, metrics[0]);
            if (bounds[1] > bounds[0]) {
                double l = cursor + bearing + bounds[0];
                double r = cursor + bearing + bounds[1] + (shadow ? 1 : 0);
                if (l < low) low = l;
                if (r > high) high = r;
            }
            cursor += khdays_ui_text_metric_f(font, glyph, 2, metrics[2]) + ctx[2];
            previous = ch;
        }
        if (!ch && high - low > target) {
            khdays_ui_text_fit_begin(font, x, low, target / (high - low), offset);
            enabled = 1;
        }
    }
    Text_DrawWithShadow(surface, x, y, depth, text, shadow);
    if (enabled) khdays_ui_text_fit_end();
}
"""


def patch(text: str, rel: str) -> str:
    if rel == DRAW:
        return COMMON + text + HELPER
    if rel == ITEMS:
        before = """                    if (pText == 0) {
                        pText = data_ov002_0207eb08;
                    }
                    Text_DrawWithShadow(pDst + nX, 0, nY + 3, 2, pText, 1);"""
        return DECLARATION + replace(
            text,
            before,
            before.replace("Text_DrawWithShadow(", "HD_FontFitShadow(").replace("pText, 1);", "pText, 1, 0);"),
        )
    if rel in MISSION_ROWS:
        # the list's "found / total", in its row's 16-pixel slot (KHDF_FIT_MISSION_COUNT)
        before = "Text_DrawWithShadow(&pList->aRowDotSurface[nRow], 2, 7, nColour, aCount, 1);"
        after = before.replace("Text_DrawWithShadow(", "HD_FontFitShadow(").replace("aCount, 1);", "aCount, 1, 2);")
        return DECLARATION + replace(text, before, after)
    return text
