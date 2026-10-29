"""Retain both font layouts without writing the alternate pass to game memory."""

DECLARATIONS = """
extern int khdays_ui_text_layout_begin(void);
extern void khdays_ui_text_layout_actual(int token);
extern void khdays_ui_text_layout_end(int token);
extern int khdays_ui_text_layout_glyph(const void *owner, const void *font, double x, double y, unsigned ch);
extern double khdays_ui_text_layout_x(double x);
extern double khdays_ui_text_layout_y(double y);
"""


def wrap(text: str, signature: str, name: str, args: str, result: str = "void") -> str:
    if signature not in text:
        raise SystemExit(f"font reflow signature not found: {signature}")
    body = name + "_layout"
    text = text.replace(signature, signature + ";\n" + signature.replace(name + "(", body + "("), 1)
    call = f"{body}({args})"
    value = "" if result == "void" else f"{result} value;"
    actual = f"{call};" if result == "void" else f"value = {call};"
    returned = "" if result == "void" else "return value;"
    return (
        text
        + f"""
{signature}
{{
    int token = khdays_ui_text_layout_begin();
    {value}
    if (token) {{ {call}; khdays_ui_text_layout_actual(token); }}
    {actual}
    khdays_ui_text_layout_end(token);
    {returned}
}}
"""
    )


def patch(text: str, rel: str) -> str:
    name = rel.rsplit("/", 1)[-1]
    if name == "Text_DrawGlyph.c":
        needle = "    khdays_ui_text_position(font, x, y);"
        if needle not in text:
            raise SystemExit("font reflow glyph hook not found")
        text = text.replace(
            "    GlyphDraw gd;",
            "    GlyphDraw gd;\n    x = khdays_ui_text_layout_x(x);\n    y = khdays_ui_text_layout_y(y);",
        )
        text = text.replace(
            needle,
            "    if (khdays_ui_text_layout_glyph((void *)owner, font, x, y, ch))\n        return khdays_ui_text_metric_f(font, glyph, 2, *(signed char *)(gd.metrics + 2));\n"
            + needle,
        )
        text = wrap(
            text,
            "double HD_FontDrawGlyph(int owner, int *font, double x, double y, int p5, unsigned short ch)",
            "HD_FontDrawGlyph",
            "owner, font, x, y, p5, ch",
            "double",
        )
    elif name == "Text_DrawLine.c":
        text = wrap(
            text,
            "void HD_FontDrawLine(int *ctx, double x, double y, int p5, int s, int *pNext, TextDir dir)",
            "HD_FontDrawLine",
            "ctx, x, y, p5, s, pNext, dir",
        )
    elif name == "Text_DrawAlignedLines.c":
        text = wrap(
            text,
            "void HD_FontDrawAlignedLines(int *ctx, double x, double y, double width, int p5, unsigned int flags, int str, TextDir dir)",
            "HD_FontDrawAlignedLines",
            "ctx, x, y, width, p5, flags, str, dir",
        )
    elif name == "Text_AlignAnchor.c":
        text = wrap(
            text,
            "void Text_AlignAnchor(int *ctx, int x, int y, int mode, int flags, int p6, AxisOffset off)",
            "Text_AlignAnchor",
            "ctx, x, y, mode, flags, p6, off",
        )
    elif name == "Text_DrawWithShadow.c":
        text = wrap(
            text,
            "void HD_FontFitShadow(void *surface, int x, int y, int depth, void *text, int shadow, int layout)",
            "HD_FontFitShadow",
            "surface, x, y, depth, text, shadow, layout",
        )
    else:
        return text
    return DECLARATIONS + text
