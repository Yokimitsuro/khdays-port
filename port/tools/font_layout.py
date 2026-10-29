"""Port-only scalar font metrics; retain NFTR bitmap records and glyph identity."""

import font_fractional
import font_numbers
import font_fit
import font_reflow

DRAW = "libs/nns/g2d/calls/Text_DrawGlyph.c"
LINE = "libs/nns/g2d/calls/Text_DrawLine.c"
MEASURE = "libs/nns/g2d/calls/NNSi_G2dFontGetStringWidth.c"
RECT = "libs/nns/g2d/calls/NNSi_G2dFontGetTextRect.c"
ALIGN = "libs/nns/g2d/calls/Text_DrawAlignedLines.c"
CRAWL = "src/overlays/scenes/ov002_field/Ov002_StepCrawlChar.c"

PATCHES = {
    DRAW: [
        ("x -= G[0];", "x -= khdays_ui_text_metric(font, glyph, 5, G[0]);"),
        ("y -= G[1];", "y -= khdays_ui_text_metric(font, glyph, 6, G[1]);"),
        ("x += *(signed char *)gd.metrics;", "x += khdays_ui_text_metric(font, glyph, 0, *(signed char *)gd.metrics);"),
        ("y += *(signed char *)gd.metrics;", "y += khdays_ui_text_metric(font, glyph, 0, *(signed char *)gd.metrics);"),
        (
            "x -= *(signed char *)gd.metrics + *(unsigned char *)(gd.metrics + 1);",
            "x -= khdays_ui_text_metric(font, glyph, 0, *(signed char *)gd.metrics) + "
            "khdays_ui_text_metric(font, glyph, 1, *(unsigned char *)(gd.metrics + 1));",
        ),
        (
            "y -= *(signed char *)gd.metrics + G[1];",
            "y -= khdays_ui_text_metric(font, glyph, 0, *(signed char *)gd.metrics) + khdays_ui_text_metric(font, glyph, 6, G[1]);",
        ),
        (
            "return *(signed char *)(gd.metrics + 2);",
            "return khdays_ui_text_metric(font, glyph, 2, *(signed char *)(gd.metrics + 2));",
        ),
    ],
    LINE: [
        ("int c = s;", "int c = s;\n    int previous = 0;"),
        (
            "adv = Text_DrawGlyph(ctx[0], font, x, y, p5, ch);",
            "w = khdays_ui_text_kerning(font, previous, ch);\n"
            "            x += w * dir.dx;\n            y += w * dir.dy;\n"
            "            adv = Text_DrawGlyph(ctx[0], font, x, y, p5, ch);",
        ),
        ("            ch = getNext(&c);", "            previous = ch;\n            ch = getNext(&c);"),
    ],
    MEASURE: [
        (
            "pWidths = NNS_G2dFontGetCharWidths(pFont, c);\n    return pWidths->charWidth;",
            "u16 index = NNS_G2dFontGetGlyphIndex(pFont, c);\n"
            "    pWidths = NNS_G2dFontGetCharWidthsFromIndex(pFont, index);\n"
            "    return khdays_ui_text_metric(pFont, index, 2, pWidths->charWidth);",
        ),
        (
            "    u16 c;",
            "    u16 c;\n    int previous = 0;\n    int right = (-2147483647 - 1);\n"
            "    int low = 2147483647;\n    int bounds_changed = khdays_ui_text_metric(pFont, -1, 4, 0);",
        ),
        (
            "width += NNS_G2dFontGetCharWidth(pFont, c) + hSpace;",
            "u16 index = NNS_G2dFontGetGlyphIndex(pFont, c);\n"
            "        const NNSG2dCharWidths *metrics = NNS_G2dFontGetCharWidthsFromIndex(pFont, index);\n"
            "        int advance = NNS_G2dFontGetCharWidth(pFont, c);\n"
            "        int extent = khdays_ui_text_extent(pFont, index, metrics->left, "
            "metrics->glyphWidth, metrics->charWidth);\n"
            "        width += khdays_ui_text_kerning(pFont, previous, c);\n"
            "        int bearing = khdays_ui_text_metric(pFont, index, 1, metrics->glyphWidth) ? "
            "khdays_ui_text_metric(pFont, index, 0, metrics->left) : 0;\n"
            "        if (width + bearing < low) low = width + bearing;\n"
            "        if (width + extent > right) right = width + extent;\n"
            "        width += advance + hSpace;\n        previous = c;",
        ),
        (
            "    return width;",
            "    if (leftOut) *leftOut = bounds_changed && previous ? low : 0;\n"
            "    return bounds_changed && previous ? right - low : width;",
        ),
    ],
    RECT: [
        ("inline s8 NNS_G2dFontGetLineFeed", "inline int NNS_G2dFontGetLineFeed"),
        ("return pFont->pRes->linefeed;", "return khdays_ui_text_metric(pFont, -1, 3, pFont->pRes->linefeed);"),
        (
            "    return rect;",
            "    if (lines > 1 && khdays_ui_text_metric(pFont, -1, 4, 0))\n"
            "        rect.height += khdays_ui_text_metric(pFont, -1, 6, NNS_G2dFontGetLineFeed(pFont)) "
            "- NNS_G2dFontGetLineFeed(pFont);\n    return rect;",
        ),
    ],
    ALIGN: [
        (
            "int step = ctx[3] + *(signed char *)(*(int *)ctx[1] + 1);",
            "int step = ctx[3] + khdays_ui_text_metric((const void *)ctx[1], -1, 3, "
            "*(signed char *)(*(int *)ctx[1] + 1));",
        ),
        (
            "            Text_DrawLine(ctx, px, py, p5, s, &s, dir);",
            "            if (flags & (0x10 | 0x20 | 0x400 | 0x800)) {\n"
            "                int low = HD_FontLineLeft(ctx[1], ctx[2], s);\n"
            "                px -= low * dir.dx;\n                py -= low * dir.dy;\n            }\n"
            "            Text_DrawLine(ctx, px, py, p5, s, &s, dir);",
        ),
    ],
    CRAWL: [
        (
            "nLine = pFont->pHeader->pMetrics[1];",
            "nLine = khdays_ui_text_metric(pFont, -1, 3, pFont->pHeader->pMetrics[1]);",
        ),
        (
            "            Obj_ForwardInnerPayload(&ctx->pFont, ctx->nPenX, ctx->nPenY, 8, wChar);",
            "            khdays_ui_text_crawl_font(pFont);\n"
            "            khdays_ui_text_crawl_begin(ctx, pText, ctx->nIndex, ctx->nPenX, ctx->nPenY, "
            "khdays_ui_text_kerning_f(pFont, ctx->nIndex ? pText[ctx->nIndex - 1] : 0, wChar));\n"
            "            Obj_ForwardInnerPayload(&ctx->pFont, ctx->nPenX, ctx->nPenY, 8, wChar);",
        ),
        (
            "ctx->nPenX += ((signed char *)NNS_G2dFontGetCharWidthsFromIndex(pFont, nGlyph))[2];",
            "ctx->nPenX += khdays_ui_text_crawl_advance(khdays_ui_text_metric_f(pFont, nGlyph, 2, "
            "((signed char *)NNS_G2dFontGetCharWidthsFromIndex(pFont, nGlyph))[2]));",
        ),
    ],
}


def patch(text: str, rel: str) -> str:
    text = font_fit.patch(text, rel)
    text = font_numbers.patch(text, rel)
    if rel == "libs/nns/g2d/calls/Text_AlignAnchor.c":
        return font_reflow.patch(font_fractional.patch(text, rel), rel)
    if rel not in PATCHES:
        return font_reflow.patch(text, rel)
    for before, after in PATCHES[rel]:
        if before not in text:
            raise SystemExit(f"font layout hook not found in {rel}: {before}")
        text = text.replace(before, after)
    if rel == MEASURE:
        signature = (
            "int NNSi_G2dFontGetStringWidth (const NNSG2dFont * pFont, int hSpace, "
            "const void * str, const void ** pPos)"
        )
        if signature not in text:
            raise SystemExit("font measurement signature changed")
        text = text.replace(
            signature,
            "static int HD_MeasureLine (const NNSG2dFont * pFont, int hSpace, "
            "const void * str, const void ** pPos, int *leftOut)",
        )
        text += (
            "\n" + signature + " { return HD_MeasureLine(pFont, hSpace, str, pPos, (int *)0); }\n"
            "int HD_FontLineLeft(const NNSG2dFont *font, int spacing, const void *str) {\n"
            "    int low; HD_MeasureLine(font, spacing, str, (const void **)0, &low); return low;\n}\n"
        )
    if rel == ALIGN:
        text = "extern int HD_FontLineLeft(int font, int spacing, int str);\n" + text
    if rel == CRAWL:
        text = (
            font_fractional.COMMON
            + "extern void khdays_ui_text_crawl_font(const void *font);\nextern void khdays_ui_text_crawl_begin(const void *ctx, const void *text, int index, int x, int y, double kerning);\nextern int khdays_ui_text_crawl_advance(double advance);\n"
            + text
        )
    return font_reflow.patch(
        font_fractional.patch(
            (
                "extern int khdays_ui_text_metric(const void *font, int glyph, int field, int fallback);\n"
                "extern int khdays_ui_text_kerning(const void *font, int left, int right);\n"
                "extern int khdays_ui_text_extent(const void *font, int glyph, int left, int width, int advance);\n"
                + text
            ),
            rel,
        ),
        rel,
    )
