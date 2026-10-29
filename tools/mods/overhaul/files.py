"""The story camp's Files pages (Roxas's Diary, Enemy Profiles) on one screen (host/screens/overhaul_camp_screen.c,
"The Files pages"): the list in the user's own rows over the camp's top and bottom bands, the DS's upper screen
beside it, a drifting pattern behind; mods/<mod>/layouts/files/:

  files.txt          the places (1920 x 1080), the atlas's pieces, the digits' advances and the font's glyphs
  files_atlas.png    the list's row (the user's button, and the chosen row's: the same made red), the list's arrow
                     (the user's), Kingdom Hearts II's NEW! badge, and KHGummi's digits in yellow (the days) and red
                     (the heart points), their shadow drawn in
  files_font.png     KHMenu's glyphs (files_font.py)
  files_pattern.png  the backdrop's tile (files_pattern.py)
  files_hint.png     the DS's A mark (files_hint.py)

From the user's pictures (art/files: button.png, arrow.png, symbols.png), KH2's camp
menu sheet (kh2_fifth's menu/us/camp.2ld, its first HD texture: the NEW! badge), the game's UI/cm/cm.p2 and
Televo's KHMenu and KHGummi.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import files_font  # noqa: E402
import files_hint  # noqa: E402
import files_pattern  # noqa: E402
from common import ART, Inputs  # noqa: E402
from raqm_text import ShapedFont  # noqa: E402

# KH2's NEW! badge: its box (left, top, right, bottom) in the camp menu's first HD texture
NEW_SHEET = "remastered/menu/us/camp.2ld/US_camp_2ld0.png"
NEW_BOX = (1, 511, 156, 560)

DIGIT_CAP = 60  # KHGummi's digits drawn this high in the atlas (shown at ~31: sharp at 4K)
DIGIT_PX = 64  # the size their height is measured at
SHADOW = 0.1  # the digits' shadow, down and right, as a share of their height (the text's: 3 at 22)
SHADOW_ALPHA = 0.7
YELLOW_TOP = (255, 236, 92)
YELLOW_BOTTOM = (206, 138, 18)
RED_TOP = (255, 128, 118)  # the heart points' digits (the camp menu's numbers)
RED_BOTTOM = (178, 14, 34)

# The screen's lines (1920 x 1080, the camp's top and bottom bands kept: the list's room is y 192-856)
ROWS = (126, 232, 670, 66, 74, 8)  # the rows: left, first top, width, height, pitch, how many show
LABEL = (22, 22, 22)  # "Day": x, its capitals' top, their height (from the row's top left)
DIGITS = (184, 18, 30)  # the day's digits: their right, top, height
TITLE = (262, 22, 22)  # the entry's title, the enemy's name
NEW = (30, 17, 32)  # the badge in the button: its right this far from the row's right, top, height
ARROWS = (461, 212, 836, 52)  # the arrows' middle x, the up one's middle y, the down one's, width
SCREEN = (976, 236, 768, 576, 6)  # the DS's upper screen: x, y, w, h (3x), its black edge
GLOVE = (-104,)  # the glove's left from the chosen row's (its middle on the row's)
SHIFT = 22  # the chosen row (and the glove) this far to the right
HINT = (221, 23)  # the DS's A mark (files_hint.png's 16 x 18 cells) in its upper screen
MENU_NEW = (28, 0, 34)  # the menu's NEW: its gap after the longest option, from the bars' middle, height
FLOAT = (4.0, 4.8)  # the DS's screen's float: up and down this far, once in this many seconds
PATTERN = (0.55, 16.0, 16.0)  # the tile's texels to the layout's, its drift (units a second: down and right)
LABELS = {
    "en": ("Day", "No."),
    "fr": ("Jour", "N°"),
    "de": ("Tag", "Nr."),
    "it": ("Giorno", "N."),
    "es": ("Día", "N.º"),
}
# files.txt's comment lines (the port skips them)
HEADER = [
    "# The story camp's Files pages (Roxas's Diary, Enemy Profiles) on one screen: the list in the",
    "# player's own rows over the camp's top and bottom bands (its backdrop's), the DS's upper screen",
    "# beside it, a drifting pattern behind. Made by tools/mods/overhaul/files.py; read by",
    '# host/screens/overhaul_camp_screen.c ("The Files pages").',
    "#   rows X Y W H PITCH SHOWN    the rows: the first's left and top, their size, pitch, how many show",
    "#   place NAME X Y W H          label/title: x, capitals' top, their height (from a row's top left);",
    "#                               digits: their right, top, height;",
    "#                               arrows: middle x, the up arrow's middle y, the down one's, width;",
    "#                               screen: the DS's upper screen (x y w h) and its edge; float: its",
    "#                               rise and period",
    "#   pattern FILE SCALE DX DY    the tile behind, its texels to the layout's, its drift a second",
    "#   hint FILE W H               the DS's A mark, its frames W x H (the diary's row, the enemies'): drawn",
    "#                               in the DS's upper screen at place hint (its pixels), as sharp",
    "#   place new R Y 0 H           the NEW, in the row's button: its right R from the row's right, its",
    "#                               top, its height;",
    "#   place select_shift D        the chosen row and the glove: this far to the right;",
    "#   place menu_new GAP DY 0 H   the camp menu's NEW: after its longest option, from its bars' middle",
    "#   label LANG DAY NUMBER       the words before a day's digits and an enemy's number",
    "#   digit N ADVANCE             a digit's advance in its piece's texels; digit_ink H: their ink's height",
]


class Atlas:
    """Pieces packed in rows, left to right, a texel apart from each other and the edge; the sheet as tall as a
    power of two."""

    def __init__(self, width=2048):
        self.width, self.x, self.y, self.row_h = width, 1, 1, 0
        self.pieces = {}
        self.images = []

    def add(self, name, im):
        w, h = im.size
        if self.x + w + 1 > self.width:
            self.x, self.y, self.row_h = 1, self.y + self.row_h + 2, 0
        self.pieces[name] = (self.x, self.y, w, h)
        self.images.append((self.x, self.y, im))
        self.x += w + 2
        self.row_h = max(self.row_h, h)

    def sheet(self) -> Image.Image:
        h = self.y + self.row_h + 1
        out = Image.new("RGBA", (self.width, 1 << (h - 1).bit_length()))
        for x, y, im in self.images:
            out.alpha_composite(im, (x, y))
        return out


def selected_button(button: Image.Image) -> Image.Image:
    """The row chosen: the user's draft's colours -- the dark part a deep red, the blue a crimson -- the gradients,
    the edge's white and the antialiasing kept (the dark part's grey and the blue's amount mixed again into the two
    reds)."""
    px = np.asarray(button.convert("RGBA")).astype(np.float32)
    r, g, b, a = px[..., 0], px[..., 1], px[..., 2], px[..., 3]
    grey = np.minimum(r, g)
    grey = np.where(grey > 17, 17 + (grey - 17) * 0.3, grey)  # the left edge's lighter grey: a darker red
    blue = np.clip(b - r, 0, None)
    dark_to = np.array([120, 10, 9]) / 17.0
    blue_to = np.array([110, 24, 49]) / 87.0
    out = grey[..., None] * dark_to + blue[..., None] * blue_to
    light = np.clip((np.maximum(np.maximum(r, g), b) - 90) / 80, 0, 1)[..., None]
    rgb = out * (1 - light) + px[..., :3] * light
    return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), a]).astype(np.uint8), "RGBA")


def digit_font(path: Path) -> ShapedFont:
    """KHGummi at the size that makes its digits' ink DIGIT_CAP high (measured at DIGIT_PX)."""
    _, top, _, bottom = files_font.bbox(ShapedFont(path, DIGIT_PX), "0123456789")
    return ShapedFont(path, round(DIGIT_PX * (DIGIT_CAP / (bottom - top))))


def gummi_digit(big: ShapedFont, d: str, top_colour, bottom_colour) -> Image.Image:
    """A KHGummi digit in a gradient from `top_colour` to `bottom_colour`, with its shadow: the picture as wide as
    the digit's advance (its left bearing in it) plus the shadow."""
    _, top, _, bottom = files_font.bbox(big, "0123456789")
    shadow = round(DIGIT_CAP * SHADOW)
    w = int(np.ceil(big.advance(d))) + shadow + 4
    h = bottom - top + shadow + 2
    mask = Image.new("L", (w, h))
    ImageDraw.Draw(mask).text((2, -top), d, font=big.font, fill=255)
    grad = np.linspace(0, 1, h)[:, None, None]
    colour = np.array(top_colour)[None, None, :] * (1 - grad) + np.array(bottom_colour)[None, None, :] * grad
    colour = np.broadcast_to(colour, (h, w, 3))
    ink = Image.fromarray(np.dstack([colour, np.asarray(mask)]).astype(np.uint8), "RGBA")
    out = Image.new("RGBA", (w, h))
    shade = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    shade.putalpha(mask.point(lambda v: int(v * SHADOW_ALPHA)))
    out.alpha_composite(shade, (shadow, shadow))
    out.alpha_composite(ink)
    return out


def layout_text(atlas: Atlas, advances: dict, ink: int, cell: int, top: int, bottom: int, glyphs) -> str:
    """files.txt (see HEADER for its lines)."""
    lines = HEADER + ["atlas files_atlas.png", f"font files_font.png {cell} {top} {bottom}"]
    for name, (u, v, w, h) in atlas.pieces.items():
        lines.append(f"piece {name} {u} {v} {w} {h}")
    for d, adv in advances.items():
        lines.append(f"digit {d} {adv:.2f}")
    lines.append(f"digit_ink {ink}")
    lines.append(f"rows {' '.join(str(v) for v in ROWS)}")
    lines.append(f"place label {LABEL[0]} {LABEL[1]} 0 {LABEL[2]}")
    lines.append(f"place title {TITLE[0]} {TITLE[1]} 0 {TITLE[2]}")
    lines.append(f"place digits {DIGITS[0]} {DIGITS[1]} 0 {DIGITS[2]}")
    lines.append(f"place new {NEW[0]} {NEW[1]} 0 {NEW[2]}")
    lines.append(f"place arrows {ARROWS[0]} {ARROWS[1]} {ARROWS[3]} {ARROWS[2]}")
    lines.append(f"place screen {SCREEN[0]} {SCREEN[1]} {SCREEN[2]} {SCREEN[3]}")
    lines.append(f"place screen_edge {SCREEN[4]} 0 0 0")
    lines.append(f"place float {FLOAT[0]} {FLOAT[1]} 0 0")
    lines.append(f"place glove_at {GLOVE[0]} 0 0 0")
    lines.append(f"place select_shift {SHIFT} 0 0 0")
    lines.append(f"place hint {HINT[0]} {HINT[1]} 16 18")
    lines.append(f"place menu_new {MENU_NEW[0]} {MENU_NEW[1]} 0 {MENU_NEW[2]}")
    lines.append("hint files_hint.png 16 18")
    lines.append(f"pattern files_pattern.png {PATTERN[0]} {PATTERN[1]} {PATTERN[2]}")
    for lang, (day, number) in LABELS.items():
        lines.append(f"label {lang} {day} {number}")
    cap = bottom - top
    lines.append(f"text row_label 0 0 {LABEL[2] / cap:.4f} left 255")
    lines.append(f"text row_title 0 0 {TITLE[2] / cap:.4f} left 255")
    for code, gx, gy, w, left, adv in glyphs:
        lines.append(f"glyph {code} {gx} {gy} {w} {left:.2f} {adv:.2f}")
    return "\n".join(lines) + "\n"


def make(inputs: Inputs, mod: Path) -> None:
    art = ART / "files"
    out = mod / "layouts" / "files"
    out.mkdir(parents=True, exist_ok=True)

    atlas = Atlas()
    button = Image.open(art / "button.png").convert("RGBA")
    atlas.add("button", button)
    atlas.add("button_selected", selected_button(button))
    atlas.add("arrow", Image.open(art / "arrow.png").convert("RGBA"))
    atlas.add("new", Image.open(inputs.kh2 / NEW_SHEET).convert("RGBA").crop(NEW_BOX))
    gummi = digit_font(inputs.font("KHGummi.otf"))
    advances = {}
    for d in "0123456789":
        atlas.add(f"gdigit{d}", gummi_digit(gummi, d, YELLOW_TOP, YELLOW_BOTTOM))
        advances[d] = gummi.advance(d)
    for d in "0123456789":
        atlas.add(f"hdigit{d}", gummi_digit(gummi, d, RED_TOP, RED_BOTTOM))
    _, top, _, bottom = files_font.bbox(gummi, "0123456789")
    ink = bottom - top
    atlas.sheet().save(out / "files_atlas.png", optimize=True)

    sheet, cell, top, bottom, glyphs = files_font.font_sheet(inputs.font("KHMenu.otf"))
    sheet.convert("RGB").save(out / "files_font.png", optimize=True)
    files_pattern.pattern_tile(art).save(out / "files_pattern.png", optimize=True)
    files_hint.hint_sheet(inputs.nitrofs / files_hint.ARCHIVE).save(out / "files_hint.png", optimize=True)
    text = layout_text(atlas, advances, ink, cell, top, bottom, glyphs)
    (out / "files.txt").write_text(text, encoding="utf-8", newline="\n")
