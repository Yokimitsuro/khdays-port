"""The Files pages' text: KHMenu's glyphs in a sheet (layouts/files/files_font.png) with their advances as the pages
were first laid out (raqm_text.py: HarfBuzz's, unhinted, the soft hyphen hidden), every glyph drawn by Pillow's
basic layout, which inks them as the first layout did."""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from raqm_text import HIDDEN, ShapedFont

FONT_PX = 64  # the sheet's glyphs: KHMenu at this size (its capitals 45 texels high)
FONT_PAD = 3  # the sheet: clear texels around a cell's highest and lowest ink
FONT_GAP = 6  # and black between the cells
COLUMNS = 24
CHARS = [chr(c) for c in range(32, 127)] + [chr(c) for c in range(160, 256)] + list("’‘“”…–—œŒ€")


def bbox(font: ShapedFont, text: str) -> tuple[int, int, int, int]:
    """The text's box as the basic layout gives it (its hinted advance in it); none for a hidden character."""
    return (0, 0, 0, 0) if len(text) == 1 and ord(text) in HIDDEN else font.font.getbbox(text)


def font_sheet(path: Path):
    """KHMenu's glyphs, white on black (the port takes the grey as the ink): every glyph whole in its cell -- the
    cell as high as the highest accent (Å) to the lowest descent (¸), FONT_PAD clear above and below -- and FONT_GAP
    of black between the cells, so that no glyph's ink reaches another's cell, even sampled smooth at its edge.
    Returns the sheet, the cell's height, the capitals' top and bottom in the cell (from H) and each glyph's
    (code, x, y, ink width, left, advance)."""
    f = ShapedFont(path, FONT_PX)
    boxes = {c: bbox(f, c) for c in CHARS}
    inked = [b for b in boxes.values() if b[2] > b[0]]
    lift = FONT_PAD - min(b[1] for b in inked)  # a glyph drawn this far down its cell
    cell = max(b[3] for b in inked) + lift + FONT_PAD
    widest = max(max(b[2] - min(0, b[0]) for b in inked), int(np.ceil(max(f.advance(c) for c in CHARS))))
    cw = widest + 2 * FONT_PAD
    pitch_x, pitch_y = cw + FONT_GAP, cell + FONT_GAP
    rows = (len(CHARS) + COLUMNS - 1) // COLUMNS
    sheet = Image.new("L", (COLUMNS * pitch_x, rows * pitch_y))
    draw = ImageDraw.Draw(sheet)
    glyphs = []
    for n, c in enumerate(CHARS):
        gx, gy = (n % COLUMNS) * pitch_x, (n // COLUMNS) * pitch_y
        box = boxes[c]
        left = min(0, box[0])
        if ord(c) not in HIDDEN:
            draw.text((gx + FONT_PAD - left, gy + lift), c, font=f.font, fill=255)
        ink_w = (box[2] - left) + 2 * FONT_PAD if box[2] > box[0] else 0
        glyphs.append((ord(c), gx, gy, ink_w, left - FONT_PAD, f.advance(c)))
    a = np.asarray(sheet)
    for n in range(len(CHARS)):  # nothing outside its cell: the black between them checked
        gx, gy = (n % COLUMNS) * pitch_x, (n // COLUMNS) * pitch_y
        if a[gy + cell : gy + pitch_y, gx : gx + pitch_x].any() or a[gy : gy + pitch_y, gx + cw : gx + pitch_x].any():
            raise ValueError(f"the glyph {CHARS[n]!r} reaches out of its cell")
    h = bbox(f, "H")
    return sheet, cell, h[1] + lift, h[3] + lift, glyphs
