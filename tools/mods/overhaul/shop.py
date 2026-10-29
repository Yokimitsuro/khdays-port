"""The Moogle Shop on one screen (layouts/shop): the pieces and places the port draws it with (host/screens/
shop_screen.c; the picture under the moogle: host/render/onescreen.c).

Writes shop.txt, shop_atlas.png (the window's pieces), shop_font.png (KHMenu's glyphs) and shop_backdrop.png (the
picture under the moogle: the DS's stripes, the pattern's moogle and its sheet, the moogle's shadow, the player's
block). From:
- the player's pictures (art/shop): block.png (the munny and heart points block), button.png (the menu's button),
  moogle_icon.png (the pattern's moogle);
- Re:Chain of Memories' shop bands and pills made brown, and its MOOGLE SHOP pill in each language (shop_recom.py,
  from the Recom archive);
- the camp's glove and settings key (the HD menus' layouts/camp/en: camp_atlas.png's pieces);
- Televo's KHMenu and KHGummi, laid out as libraqm lays them out (raqm_text.py).
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shop_recom  # noqa: E402
from common import ART, Inputs  # noqa: E402
from raqm_text import ShapedFont  # noqa: E402

W, H = 1920, 1080

# ---- the backdrop (the one-screen picture's, under the moogle's 3D) --------------------------------------------
STRIPE_DARK = (123, 82, 0)  # the DS's stripes (UI/shop's background, read off the screen)
STRIPE_LIGHT = (140, 107, 0)
EDGE = (49, 16, 8)  # its edges' brown: the picture's ground beyond it
STRIPE_PERIOD = 16  # layout units: 8 dark, 8 light
ICON_H = 140  # the pattern's moogle, in layout units (its texels twice that: sharp at 4K)
ICON_TEXELS = 2
ICON_ALPHA = 0.26
PATTERN_STEP = (300, 270)  # its grid; every other row half a step over
PATTERN_SPEED = (14.0, 9.0)  # layout units a second: down and to the right
FLOOR = (1296, 870, 300, 60)  # the moogle's shadow on the floor (x y w h)
FLOOR_ALPHA = 0.43

# ---- the moogle (the DS's upper screen's 3D, turned to the left: runtime/menus/shop.c) -------------------------
MODEL_SOURCE = (0, 0, 256, 192)  # every polygon of the upper screen is the moogle's
MODEL_VIEW = (94, 16, 68, 106)  # the part shown: the moogle and its pompom (DS dots)
MODEL = (1226, 336, 440, 548)  # where (layout units)

# ---- the window's pieces (layout units, 1920 x 1080) ----------------------------------------------------------
TITLE = (150, 18)  # the MOOGLE SHOP pill (Re:CoM's, each language's)
SECTION = (1276, 18, 430, 68)  # the right pill: the page's word
SECTION_TEXT = (27, (238, 230, 214))  # its capitals' height, colour
SETTINGS = (1880, 34, 0, 70)  # the settings key (the camp's place)
MESSAGE = (295, 978, 1330, 64)  # the bottom pill: the game's line
MESSAGE_TEXT = (45, 25, (232, 226, 214))  # its left inside the pill, capitals' height, colour
BLOCK = (1186, 150, 520)  # the player's block (its height from its picture)
LABEL_SIZE, VALUE_SIZE, CHANGE_SIZE = 28, 40, 26  # KHGummi's sizes (px of a 1080 picture)
MUNNY_LABEL = (34, 20)  # in the block: the label's picture's top left
HEARTS_LABEL = (34, 134)
VALUE_RIGHT = 44  # the values' pictures' right from the block's
MUNNY_VALUE_TOP, HEARTS_VALUE_TOP = 56, 170
CHANGE_GAP = 16  # a pending price: its right this far left of the value
LINE = (38, 119, 46, 3, (214, 140, 40))  # the line between them: left, y, right inset, thickness, colour
MENU = (360, 528, 460, 112)  # the menu's buttons: left, their middle, width, pitch
MENU_LIT = 24  # the chosen button: this far to the right
MENU_TEXT = (56, 30, (236, 226, 208), (255, 255, 255))  # the word: left in the button, capitals, colours
GLOVE = (-80, -40, 106, 79)  # the glove: from the chosen button's left and middle
PANEL_K = 3.1  # the DS's lower screen, floating: this many layout units a dot
PANEL = (170, 307)  # its top left
HEADER = (170, 140)  # the upper screen's counter (80, 144, 176 x 48: the panel's name, how
HEADER_SOURCE = (80, 144, 176, 48)  # many are owned) over it, the same size
RECIPE_K = 2.6  # Synthesize: the upper screen's recipe (its ingredients) in the
RECIPE = (1196, 470)  # moogle's place
RECIPE_SOURCE = (32, 16, 192, 128)
SCREEN_EDGE = 3  # their black frame
FLOAT = (4.0, 4.8)  # their float: up and down this far, once in this many seconds

LANGS = {"en": "UK", "fr": "FR", "de": "GR", "it": "IT", "es": "SP"}  # the port's languages, Re:CoM's
MUNNY_TOP, MUNNY_BOTTOM = (255, 236, 90), (240, 150, 20)
HEART_TOP, HEART_BOTTOM = (255, 120, 150), (200, 20, 60)
WHITE_TOP, WHITE_BOTTOM = (255, 255, 255), (214, 214, 222)
TEXELS = 2  # KHGummi's pictures: twice their size on a 1080 picture
CAMP_PIECES = ("glove", "settings_esc", "settings_gear")  # the camp's, as its English atlas has them

# ---- KHMenu's glyph sheet (the port's layouts read it: white on black, the grey the ink) -----------------------
FONT_PX = 64  # the sheet's glyphs: KHMenu at this size (its capitals 45 texels high)
FONT_PAD = 3  # the sheet: clear texels around a cell's highest and lowest ink
FONT_GAP = 6  # and black between the cells
FONT_COLUMNS = 24
CHARS = [chr(c) for c in range(32, 127)] + [chr(c) for c in range(160, 256)] + list("’‘“”…–—œŒ€×―")

HEADER_LINES = [
    "# The Moogle Shop on one screen (story and Mission Mode): Re:Chain of Memories' bands made brown, the",
    "# player's block with the munny and heart points (KHGummi), the menu in the player's buttons (KHMenu, the",
    "# camp's glove, KH2's firefly), the DS's lower screen floating for the lists, the moogle turned to the",
    "# left over the DS's stripes with a moogle pattern drifting. Made by tools/mods/overhaul/shop.py;",
    "# read by host/screens/shop_screen.c (the window's pieces) and host/render/onescreen.c (the picture under",
    "# the moogle: backdrop, pattern, floor, model_*).",
    "#   backdrop FILE X Y W H        the picture's atlas and the stripes' texels in it (a 1920 x 1080 picture)",
    "#   ground R G B                 beyond the picture",
    "#   pattern X Y W H HEIGHT ALPHA STEPX STEPY SPEEDX SPEEDY   the moogle's texels in it, its height, its",
    "#                                alpha, the grid's step, its drift (units a second)",
    "#   pattern_sheet X Y W H        the pattern laid out once over the picture and a step more each way",
    "#                                (W + STEPX by H + 2 STEPY, a texel a unit): drawn as one piece",
    "#   floor X Y W H ALPHA          the moogle's shadow's texels in it (drawn at place floor), its alpha",
    "#   block_backdrop X Y W H       the player's block's texels in it (drawn at place block, under the moogle)",
    "#   screens_alone 1|0            the DS's list and counter without the DS's stripes nor a frame, a shadow",
    "#                                under them (0: the whole screens in a thin black frame)",
    "#   model_source / model_view X Y W H   the DS's dots with the moogle, the part shown; model: where",
    "#   digits SET PAD INK SIZE      KHGummi's digits <SET>0-9 (c also cplus, cminus): the pen and the ink's",
    "#                                top PAD texels into each picture, the ink INK texels high; digit SET C ADV",
    "#   colour NAME R G B; place float RISE PERIOD (the DS's screens'); the motion's times are the screen's own",
]


class Atlas:
    """Pieces in rows, each with its edge texels repeated around it (EXTRUDE): sampled smooth at its edge, a piece
    reads its own colour, not the transparent gap (the pills' slices meet seamless)."""

    EXTRUDE = 2

    def __init__(self, width: int = 2048) -> None:
        self.width, self.x, self.y, self.row_h = width, 0, 0, 0
        self.pieces: dict[str, tuple[int, int, int, int]] = {}
        self.images: list[tuple[int, int, Image.Image]] = []

    def add(self, name: str, im: Image.Image) -> None:
        e = self.EXTRUDE
        w, h = im.size
        if self.x + w + 2 * e > self.width:
            self.x, self.y, self.row_h = 0, self.y + self.row_h, 0
        self.pieces[name] = (self.x + e, self.y + e, w, h)
        a = np.pad(np.asarray(im.convert("RGBA")), ((e, e), (e, e), (0, 0)), mode="edge")
        self.images.append((self.x, self.y, Image.fromarray(a, "RGBA")))
        self.x += w + 2 * e
        self.row_h = max(self.row_h, h + 2 * e)

    def save(self, path: Path) -> None:
        h = self.y + self.row_h
        out = Image.new("RGBA", (self.width, 1 << (h - 1).bit_length()))
        for x, y, im in self.images:
            out.paste(im, (x, y))
        out.save(path, optimize=True)


def gradient_ink(mask: np.ndarray, top, bottom, y0: int = 0, y1: int | None = None) -> np.ndarray:
    """The mask's ink coloured top to bottom (between rows y0 and y1)."""
    h, w = mask.shape
    y1 = h if y1 is None else y1
    t = np.clip((np.arange(h) - y0) / max(1, y1 - y0), 0, 1)[:, None, None]
    col = np.array(top, float) * (1 - t) + np.array(bottom, float) * t
    return np.dstack([np.broadcast_to(col, (h, w, 3)), mask]).astype(np.uint8)


def shadowed(ink_rgba: np.ndarray, shadow: int) -> Image.Image:
    """The ink over its own shadow, down and to the right (black, 0.85)."""
    h, w = ink_rgba.shape[:2]
    out = Image.new("RGBA", (w + shadow, h + shadow))
    sh = np.zeros((h, w, 4), np.uint8)
    sh[..., 3] = (ink_rgba[..., 3].astype(float) * 0.85).astype(np.uint8)
    out.alpha_composite(Image.fromarray(sh, "RGBA"), (shadow, shadow))
    out.alpha_composite(Image.fromarray(ink_rgba, "RGBA"), (0, 0))
    return out


def gummi_label(gummi: Path, text: str, size: int, top, bottom) -> Image.Image:
    """KHGummi's `text` (a label), its colour top to bottom and its shadow, at TEXELS times `size`."""
    f = ShapedFont(gummi, size * TEXELS)
    left, t, right, b = f.getbbox(text)
    shadow = max(2, round(size * 0.08)) * TEXELS
    pad = 4 * TEXELS
    mask = Image.new("L", (right - left + 2 * pad, b - t + 2 * pad))
    f.draw(mask, (pad - left, pad - t), text)
    return shadowed(gradient_ink(np.asarray(mask), top, bottom, pad, pad + (b - t)), shadow)


def gummi_digits(gummi: Path, size: int, top, bottom, signs: bool = False):
    """KHGummi's digits (and + and -), each in a picture of its own at TEXELS times `size`: the pen at `pad` from
    its left, the digits' ink top at `pad` from its top. Each picture with its advance; the pad; their ink's
    height (texels)."""
    f = ShapedFont(gummi, size * TEXELS)
    _, t, _, b = f.getbbox("0123456789")
    shadow = max(2, round(size * 0.08)) * TEXELS
    pad = 4 * TEXELS
    out = {}
    for c in list("0123456789") + (["+", "-"] if signs else []):
        adv = f.getlength(c)
        right = f.getbbox(c)[2]
        mask = Image.new("L", (pad + int(np.ceil(max(adv, right))) + pad, pad + (b - t) + pad))
        f.draw(mask, (pad, pad - t), c)
        out[c] = (shadowed(gradient_ink(np.asarray(mask), top, bottom, pad, pad + (b - t)), shadow), adv)
    return out, pad, b - t


def lit_button(im: Image.Image) -> Image.Image:
    """The menu's button lit: its brown made the DS's lit gold, the same light."""
    a = np.asarray(im, float).copy()
    lum = a[..., :3] @ np.array([0.299, 0.587, 0.114])
    g = lum / max(1.0, np.percentile(lum[a[..., 3] > 128], 50))
    a[..., :3] = np.clip(g[..., None] * np.array([188, 158, 8], float), 0, 255)
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def three_slices(im: Image.Image) -> tuple[Image.Image, Image.Image, Image.Image]:
    """A pill's left cap, a middle column and its right cap (the caps as wide as it is high)."""
    h = im.height
    return im.crop((0, 0, h, h)), im.crop((h, 0, h + 4, h)), im.crop((im.width - h, 0, im.width, h))


def backdrop() -> Image.Image:
    """The DS's stripes in their own colours under a smooth light: the edges' brown at the sides (the DS's
    dithered fade made a gradient), a soft light behind the moogle, darker to the top and bottom."""
    y = np.arange(H)
    light = ((y // (STRIPE_PERIOD // 2)) % 2) == 1
    base = np.where(light[:, None, None], np.array(STRIPE_LIGHT, float), np.array(STRIPE_DARK, float))
    base = base * np.ones((1, W, 1))
    x = (np.arange(W) + 0.5) / W
    yy = (np.arange(H) + 0.5) / H
    gx, gy = np.meshgrid(x, yy)
    t = np.clip(np.minimum(x, 1 - x) / 0.17, 0, 1)
    t = t * t * (3 - 2 * t)
    glow = np.exp(-(((gx - 0.66) / 0.40) ** 2 + ((gy - 0.55) / 0.55) ** 2))
    glow2 = np.exp(-(((gx - 0.30) / 0.30) ** 2 + ((gy - 0.50) / 0.45) ** 2))
    vert = 1 - 0.72 * np.abs(gy - 0.5) ** 1.5
    img = base * (0.78 + 0.26 * glow + 0.10 * glow2)[..., None] * vert[..., None]
    img = np.array(EDGE, float) * (1 - t[None, :, None]) + img * t[None, :, None]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def pattern_icon() -> Image.Image:
    """The player's moogle, ICON_TEXELS times ICON_H high, one colour throughout (its own) so that its smooth edge
    never reads a black texel."""
    im = Image.open(ART / "shop" / "moogle_icon.png").convert("RGBA")
    k = ICON_H * ICON_TEXELS / im.height
    im = im.resize((round(im.width * k), ICON_H * ICON_TEXELS), Image.LANCZOS)
    a = np.asarray(im).copy()
    solid = a[..., 3] > 200
    a[..., :3] = np.round(a[solid][:, :3].astype(float).mean(0)).astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def pattern_sheet(icon: Image.Image) -> Image.Image:
    """The pattern laid out once over a picture and one period more each way (W + STEPX by H + 2 STEPY, layout
    units): the one-screen picture draws it as a single piece whose texels slide with the drift (a piece for each
    moogle made the graphics card test some 45 pieces at every texel). Laid out at ICON_TEXELS and halved, each
    texel the mean of its four: the moogles' edges as smooth as before. The sheet's colour is the moogle's
    throughout, its alpha the moogle's (the piece's own: ICON_ALPHA)."""
    sx, sy = PATTERN_STEP
    k = ICON_TEXELS
    cw, ch = (W + sx) * k, (H + 2 * sy) * k
    alpha = np.zeros((ch, cw), np.float32)
    ia = np.asarray(icon)[..., 3].astype(np.float32)
    ih, iw = ia.shape
    for row in range(-1, (H + 2 * sy) // sy + 2):
        for col in range(-1, (W + sx) // sx + 2):
            x = (col * sx + (sx // 2 if row & 1 else 0)) * k
            y = row * sy * k
            x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + iw, cw), min(y + ih, ch)
            if x1 <= x0 or y1 <= y0:
                continue
            region = alpha[y0:y1, x0:x1]
            np.maximum(region, ia[y0 - y : y1 - y, x0 - x : x1 - x], out=region)
    a = alpha.reshape(ch // k, k, cw // k, k).mean(axis=(1, 3))
    rgb = np.asarray(icon)[..., :3].reshape(-1, 3)[0]
    out = np.zeros((ch // k, cw // k, 4), np.uint8)
    out[..., :3] = rgb
    out[..., 3] = np.clip(np.round(a), 0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def floor_shadow() -> Image.Image:
    """The moogle's shadow on the floor: a soft brown ellipse (FLOOR's size, 20 texels of blur room around it)."""
    w, h = FLOOR[2], FLOOR[3]
    m = Image.new("L", (w + 40, h + 40))
    ImageDraw.Draw(m).ellipse((20, 20, 20 + w, 20 + h), fill=255)
    m = m.filter(ImageFilter.GaussianBlur(10))
    a = np.zeros((h + 40, w + 40, 4), np.uint8)
    a[..., :3] = (30, 12, 0)
    a[..., 3] = np.asarray(m)
    return Image.fromarray(a, "RGBA")


def font_sheet(path: Path):
    """KHMenu's glyphs, white on black (the port takes the grey as the ink): every glyph whole in its cell -- the
    cell as high as the highest accent (Å) to the lowest descent (¸), FONT_PAD clear above and below -- and
    FONT_GAP of black between the cells, so that no glyph's ink reaches another's cell, even sampled smooth at its
    edge. The sheet, the cell's height, the capitals' top and bottom in the cell (from H) and each glyph's
    (code, x, y, ink width, left, advance)."""
    f = ShapedFont(path, FONT_PX)
    boxes = {c: f.getbbox(c) for c in CHARS}
    inked = [b for b in boxes.values() if b[2] > b[0]]
    lift = FONT_PAD - min(b[1] for b in inked)  # a glyph drawn this far down its cell
    cell = max(b[3] for b in inked) + lift + FONT_PAD
    widest = max(b[2] - min(0, b[0]) for b in inked)
    cw = max(widest, int(np.ceil(max(f.getlength(c) for c in CHARS)))) + 2 * FONT_PAD
    pitch_x, pitch_y = cw + FONT_GAP, cell + FONT_GAP
    rows = (len(CHARS) + FONT_COLUMNS - 1) // FONT_COLUMNS
    sheet = Image.new("L", (FONT_COLUMNS * pitch_x, rows * pitch_y))
    glyphs = []
    for n, c in enumerate(CHARS):
        gx, gy = (n % FONT_COLUMNS) * pitch_x, (n // FONT_COLUMNS) * pitch_y
        box = boxes[c]
        left = min(0, box[0])
        f.draw(sheet, (gx + FONT_PAD - left, gy + lift), c)
        ink_w = (box[2] - left) + 2 * FONT_PAD if box[2] > box[0] else 0
        glyphs.append((ord(c), gx, gy, ink_w, left - FONT_PAD, f.getlength(c)))
    # nothing outside its cell: the sheet's black between them checked
    a = np.asarray(sheet)
    for n in range(len(CHARS)):
        gx, gy = (n % FONT_COLUMNS) * pitch_x, (n // FONT_COLUMNS) * pitch_y
        if a[gy + cell : gy + pitch_y, gx : gx + pitch_x].any() or a[gy : gy + pitch_y, gx + cw : gx + pitch_x].any():
            raise ValueError(f"KHMenu's {CHARS[n]!r} reaches past its cell")
    h = f.getbbox("H")
    return sheet, cell, h[1] + lift, h[3] + lift, glyphs


def camp_pieces(hd: Path) -> dict[str, Image.Image]:
    """The camp's glove and settings key, cut from its English atlas (the HD menus' layouts/camp/en)."""
    folder = hd / "layouts" / "camp" / "en"
    atlas = Image.open(folder / "camp_atlas.png").convert("RGBA")
    out = {}
    for line in (folder / "camp.txt").read_text(encoding="utf-8").splitlines():
        w = line.split()
        if len(w) == 6 and w[0] == "piece" and w[1] in CAMP_PIECES:
            x, y, pw, ph = map(int, w[2:6])
            out[w[1]] = atlas.crop((x, y, x + pw, y + ph))
    missing = [name for name in CAMP_PIECES if name not in out]
    if missing:
        raise ValueError(f"{folder / 'camp.txt'} has no piece {', '.join(missing)}")
    return out


def make(inputs: Inputs, mod: Path) -> None:
    out = mod / "layouts" / "shop"
    out.mkdir(parents=True, exist_ok=True)
    gummi = inputs.font("KHGummi.otf")
    block = Image.open(ART / "shop" / "block.png").convert("RGBA")
    button = Image.open(ART / "shop" / "button.png").convert("RGBA")

    # the backdrop's atlas: the stripes, the pattern's moogle and its sheet, the floor's shadow, and the player's
    # block (the moogle over it; its words and sums the screen's, over the moogle)
    icon = pattern_icon()
    drift = pattern_sheet(icon)
    drift_at = (0, H + ICON_H * ICON_TEXELS + 4)
    fl = floor_shadow()
    floor_at = (icon.width + 8, H + 2)
    block_at = (floor_at[0] + fl.width + 8, H + 2)
    bd = Image.new("RGBA", (max(W, drift.width), drift_at[1] + drift.height))
    bd.paste(backdrop(), (0, 0))
    bd.paste(icon, (0, H + 2))
    bd.paste(drift, drift_at)
    bd.paste(fl, floor_at)
    bd.paste(block, block_at)
    bd.save(out / "shop_backdrop.png", optimize=True)

    recom = shop_recom.bands(inputs.recom)
    atlas = Atlas()
    atlas.add("band_top", recom["band_top"])
    atlas.add("band_bottom", recom["band_bottom"])
    for code, lang in LANGS.items():
        atlas.add("title_" + code, shop_recom.title(inputs.recom, lang))
    for name, pill in (("pill", recom["pill"]), ("message", recom["message_pill"])):
        left, mid, right = three_slices(pill)
        atlas.add(name + "_left", left)
        atlas.add(name + "_middle", mid)
        atlas.add(name + "_right", right)
    atlas.add("block", block)
    atlas.add("button", button)
    atlas.add("button_lit", lit_button(button))
    camp = camp_pieces(inputs.hd)
    for name in CAMP_PIECES:
        atlas.add(name, camp[name])
    labels = {}
    for name, word in (("munny", "MUNNY"), ("hearts", "HEART POINTS")):
        labels[name] = gummi_label(gummi, word, LABEL_SIZE, WHITE_TOP, WHITE_BOTTOM)
        atlas.add("label_" + name, labels[name])
    sets = {}
    signs_names = {"+": "plus", "-": "minus"}
    for prefix, size, top, bottom, signs in (
        ("m", VALUE_SIZE, MUNNY_TOP, MUNNY_BOTTOM, False),
        ("h", VALUE_SIZE, HEART_TOP, HEART_BOTTOM, False),
        ("c", CHANGE_SIZE, WHITE_TOP, WHITE_BOTTOM, True),
    ):
        digits, pad, ink = gummi_digits(gummi, size, top, bottom, signs)
        for c, (im, _adv) in digits.items():
            atlas.add(prefix + signs_names.get(c, c), im)
        sets[prefix] = (digits, pad, ink, size)
    atlas.save(out / "shop_atlas.png")

    sheet, cell, ftop, fbottom, glyphs = font_sheet(inputs.font("KHMenu.otf"))
    sheet.convert("RGB").save(out / "shop_font.png", optimize=True)

    bx, by, bw = BLOCK
    bh = block.height * bw / block.width
    lines = HEADER_LINES + [
        f"backdrop shop_backdrop.png 0 0 {W} {H}",
        f"ground {EDGE[0]} {EDGE[1]} {EDGE[2]}",
        f"pattern 0 {H + 2} {icon.width} {icon.height} {ICON_H} {ICON_ALPHA} {PATTERN_STEP[0]} {PATTERN_STEP[1]} "
        f"{PATTERN_SPEED[0]} {PATTERN_SPEED[1]}",
        f"pattern_sheet {drift_at[0]} {drift_at[1]} {drift.width} {drift.height}",
        f"floor {floor_at[0]} {floor_at[1]} {fl.width} {fl.height} {FLOOR_ALPHA}",
        f"block_backdrop {block_at[0]} {block_at[1]} {block.width} {block.height}",
        "screens_alone 1",
        "model_source %d %d %d %d" % MODEL_SOURCE,
        "model_view %d %d %d %d" % MODEL_VIEW,
        "model %d %d %d %d" % MODEL,
        "atlas shop_atlas.png",
        f"font shop_font.png {cell} {ftop} {fbottom}",
    ]
    for name, (x, y, w, h) in atlas.pieces.items():
        lines.append(f"piece {name} {x} {y} {w} {h}")
    ext = (fl.width - FLOOR[2]) / 2
    lines += [
        f"place floor {FLOOR[0] - ext:.1f} {FLOOR[1] - ext:.1f} {fl.width} {fl.height}",
        f"place band_top 0 0 {W} 112",
        f"place band_bottom 0 960 {W} 120",
        f"place title {TITLE[0]} {TITLE[1]} 0 68",
        "place section %d %d %d %d" % SECTION,
        f"place section_text {SECTION_TEXT[0]} 0 0 0",
        "place settings %d %d %d %d" % SETTINGS,
        "place message %d %d %d %d" % MESSAGE,
        f"place message_text {MESSAGE_TEXT[0]} {MESSAGE_TEXT[1]} 0 0",
        f"place block {bx} {by} {bw} {bh:.1f}",
    ]
    for name, (lx, ly) in (("munny", MUNNY_LABEL), ("hearts", HEARTS_LABEL)):
        im = labels[name]
        lines.append(f"place label_{name} {bx + lx} {by + ly} {im.width / TEXELS:.1f} {im.height / TEXELS:.1f}")
    for prefix in ("m", "h", "c"):
        digits, pad, ink, size = sets[prefix]
        lines.append(f"digits {prefix} {pad} {ink} {size}")
        for c, (_im, adv) in digits.items():
            lines.append(f"digit {prefix} {c} {adv:.2f}")
    # the values: their pen's end (the right), their ink's top and height (layout units)
    k = 1 / TEXELS
    for name, prefix, top in (("munny", "m", MUNNY_VALUE_TOP), ("hearts", "h", HEARTS_VALUE_TOP)):
        _digits, pad, ink, _size = sets[prefix]
        lines.append(
            f"place {name}_value {bx + bw - VALUE_RIGHT - pad * k:.1f} {by + top + pad * k:.1f} 0 {ink * k:.1f}"
        )
    lines.append(f"place change {CHANGE_GAP} 0 0 {sets['c'][2] / TEXELS:.1f}")
    lines.append(f"place line {bx + LINE[0]} {by + LINE[1] - LINE[3] / 2:.1f} {bw - LINE[0] - LINE[2]} {LINE[3]}")
    lines.append("colour line %d %d %d" % LINE[4])
    lines += [
        "place menu %d %d %d %d" % MENU,
        f"place menu_lit {MENU_LIT} 0 0 0",
        f"place menu_text {MENU_TEXT[0]} {MENU_TEXT[1]} 0 0",
        "colour menu_text %d %d %d" % MENU_TEXT[2],
        "colour menu_text_lit %d %d %d" % MENU_TEXT[3],
        "colour section_text %d %d %d" % SECTION_TEXT[1],
        "colour message_text %d %d %d" % MESSAGE_TEXT[2],
        "place glove %d %d %d %d" % GLOVE,
        f"place panel {PANEL[0]} {PANEL[1]} {256 * PANEL_K:.1f} {192 * PANEL_K:.1f}",
        f"place header {HEADER[0]} {HEADER[1]} {HEADER_SOURCE[2] * PANEL_K:.1f} {HEADER_SOURCE[3] * PANEL_K:.1f}",
        "place header_source %d %d %d %d" % HEADER_SOURCE,
        f"place recipe {RECIPE[0]} {RECIPE[1]} {RECIPE_SOURCE[2] * RECIPE_K:.1f} {RECIPE_SOURCE[3] * RECIPE_K:.1f}",
        "place recipe_source %d %d %d %d" % RECIPE_SOURCE,
        f"place screen_edge {SCREEN_EDGE} 0 0 0",
        f"place float {FLOAT[0]} {FLOAT[1]} 0 0",
    ]
    for code, gx, gy, w, left, adv in glyphs:
        lines.append(f"glyph {code} {gx} {gy} {w} {left:.2f} {adv:.2f}")
    (out / "shop.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
