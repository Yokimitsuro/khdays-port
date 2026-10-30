"""The camp's save page and the title's load list (host/screens/overhaul_camp_screen.c, "The save page"): their
pieces and places, layouts/campsave/ (campsave.txt, campsave_atlas.png, files_font.png).

- KH2's save list (the HD collection's menu/us/save.2ld, its sequences drawn by kh2_seq_draw.py, green made grey):
  the list's frame, a file's row (and the chosen one's, red), the ring of the chosen one's number, the scroll bar's
  track and thumb, the crown and the MUNNY word; the other languages' (menu/sp, fr, gr, it): their MUNNY and their
  titles' words (SAVE, LOAD) for the pill;
- Days's own worlds, the Holo-Missions map's models (UI/cm/cm.p2: campsave_worlds.py), and their titles, the
  worlds' logos (mi/ob) and KH2's for The World That Never Was (menu/jm_top_eh.2ld: campsave_titles.py);
- the files' faces as the port's save list has them (the HD menus' layouts/save/en/save_atlas.png), sharp, larger;
- the menu's lit bar without its yellow glow, for the option chosen beside the pill (the camp piece's
  layouts/camp/en, made before this one);
- the top band the player made (art/campsave/band.png, its SAVE pill) and the load list's, LOAD written over SAVE
  in KHGummi; the other languages' bands, KH2's own words over SAVE;
- the Files pages' font (the files piece's layouts/files, made before this one).
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageEnhance

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import campsave_titles  # noqa: E402
import campsave_worlds  # noqa: E402
import kh2_seq_draw as seq_draw  # noqa: E402
import raqm_text  # noqa: E402
from common import ART, LANGS, Inputs  # noqa: E402
from kh2_sq import Layout  # noqa: E402

SAVE = "menu/{}/save.2ld"  # KH2's save list, in a language's folder
# the port's languages' folders of KH2's menus (English's band is the player's own, its SAVE written there)
KH2_LANGS = {"en": "us", "fr": "fr", "de": "gr", "it": "it", "es": "sp"}
SAVE_WORD, LOAD_WORD, MUNNY_WORD = 21, 22, 25  # the save list's sprites: its titles' words and MUNNY
WORLDS = ["castle", "tt", "al", "bb", "he", "aw", "nm", "pp"]  # the worlds' numbers (saveworld.h's)

# ---- The page's geometry (the layout's units: 1920 x 1080) ----
FRAME = (279, 197, 1368, 644)  # KH2's list frame (its sequence's g47-49: 492 x 254 dots)
FX, FY = FRAME[2] / 492.0, FRAME[3] / 254.0
ROW_W, ROW_H = 1300, 148  # a row's panel (KH2's g53: 458 x 64 dots)
RX, RY = ROW_W / 458.0, ROW_H / 64.0
ROWS = (303, 214, ROW_W, ROW_H, 156, 4)  # first row's left, top; size; pitch; how many show
PAD = 24  # room round a drawn piece (its shadow)
# The scroll bar as KH2 builds it (save.2ld's groups 33-38: 18 dots wide), drawn whole at its size (twice the
# layout's units), nothing stretched when the page draws it. The track: its top cap (sprite 41, 24 dots, above the
# body's top), the body (42, one texel high, as long as the list), the bottom cap (43, 24 dots, under it). The
# thumb: its middle (45, one texel high) from its place t, its top (44, 9 dots) just above t, its bottom (46, 9
# dots) just under its middle; t runs from the body's top (the thumb's top over the top cap's dark end: they meet)
# to its bottom less the middle's length.
SCROLL = (1610, 214, 34, 608)  # the bar: middle x, top, width, height (the layout's units)
THUMB = 60  # the thumb's middle's length (the layout's units)
SK = 2  # the bar's pictures: this many dots a unit
DOT = SCROLL[2] / 18.0  # the layout's units a KH2 dot
CAP = 24 * DOT
BODY_LEN = SCROLL[3] - 2 * CAP
CROWN_W = 52.0  # the crown (the story cleared) on top of the number's ring, as wide as KH2 makes it beside a row
FACE_K = 8  # the faces' pixels made squares this large (the page draws them a little smaller)
ATLAS_W = 2048
# The band's pill: its SAVE (the band's own pixels, 2000 wide) is wiped and another word written there
PILL = (190, 52, 470, 112)

# campsave.txt's comment lines (the port skips them)
HEADER = [
    "# The story camp's save page on one screen: KH2's save list (its rows, frame, digits, scroll bar)",
    "# made grey, Days's own worlds (the Holo-Missions map's models), the player's top band; made by",
    '# tools/mods/overhaul/campsave.py, read by host/screens/overhaul_camp_screen.c ("The save page").',
    "#   rows X Y W H PITCH SHOWN   the rows' panels: the first's left, top, size, pitch, how many show",
    "#   place NAME X Y W H         from a row's panel's top left (row_piece, ring, number, world,",
    "#                              face, day, level, difficulty, munny, time, crown, empty),",
    "#                              or the picture's (band, frame, scroll, button)",
    "#   words empty|load_empty|guest LANG WORDS   an empty file's words (save, load), the guest's row's",
    "# A piece NAME_LANG (band_es, band_load_es, munny_es) stands for NAME in that language when there.",
]


def canvas(w: int, h: int) -> np.ndarray:
    return np.zeros((h, w, 4))


def grey(colour, anim, group):
    return seq_draw.green_to_grey(colour)


# ---- KH2's save list ----------------------------------------------------------------------------------------


class SaveList:
    """KH2's save list's layout (its first sequence) and HD texture 0, in a language's folder (KH2_LANGS')."""

    def __init__(self, kh2: Path, lang: str = "us"):
        self.tex = Image.open(kh2 / "remastered" / SAVE.format(lang) / f"{lang.upper()}_save_2ld0.png").convert("RGBA")
        self.seq = Layout(str(kh2 / "original" / SAVE.format(lang)), [self.tex]).seqs("save")[0]

    def group(self, w: int, h: int, gi: int, frame: int, origin, kx: float, ky: float, **kw) -> Image.Image:
        """Animation group `gi` at `frame` drawn on a w x h picture."""
        c = canvas(w, h)
        seq_draw.draw_group(c, self.seq, self.tex, gi, frame, origin, kx, ky, **kw)
        return seq_draw.to_image(c)

    def sprite(self, i: int, k: int = 2, bright: float = 1.0) -> Image.Image:
        """Sprite i as its texels (HD: k per dot), brightened (the PS2's 0x80 is 1: its tints double)."""
        _, u0, v0, u1, v1 = self.seq.sprites[i][:5]
        ua, ub = min(u0, u1), max(u0, u1)
        va, vb = min(v0, v1), max(v0, v1)
        im = self.tex.crop((ua * k, va * k, max(ub * k, ua * k + 2), max(vb * k, va * k + 2)))
        if bright != 1.0:
            a = np.asarray(im, float)
            a[..., :3] = np.clip(a[..., :3] * bright, 0, 255)
            im = Image.fromarray(a.astype(np.uint8), "RGBA")
        return im

    def ink(self, i: int) -> Image.Image:
        """Sprite i's ink (its alpha), cut to it."""
        a = self.sprite(i).getchannel("A")
        return a.crop(a.point(lambda v: 255 if v > 24 else 0).getbbox())

    def stack(self, parts, w: int) -> Image.Image:
        """[(sprite, length in the layout's units or None: its own)] one under another, `w` wide (SK x)."""
        ims = []
        for sp, length in parts:
            im = self.sprite(sp)
            if length is None:
                ims.append(im.resize((w, round(im.height / 2.0 * DOT * SK)), Image.LANCZOS))
            else:
                row = im.crop((0, im.height // 2, im.width, im.height // 2 + 1)).resize((w, 1), Image.LANCZOS)
                ims.append(row.resize((w, round(length * SK)), Image.NEAREST))
        out = Image.new("RGBA", (w, sum(p.height for p in ims)), (0, 0, 0, 0))
        y = 0
        for p in ims:
            out.alpha_composite(p, (0, y))
            y += p.height
        return out


def ring(kh2: SaveList, middle_height: int) -> Image.Image:
    """The chosen row's number's ring (g54) drawn again at one scale (twice as large), made a circle a little
    larger than KH2's (the two digits well inside) and light grey."""
    big = kh2.group(800, 800, 85, 7, (400 - 20 * RY * 2, 400 - 77 * RY * 2), RY * 2, RY * 2, only=(54,))
    big = big.crop(big.getbbox())
    d = middle_height + 12
    circle = np.asarray(big.resize((d, d), Image.LANCZOS), float)
    lum = circle[..., :3] @ np.array([0.299, 0.587, 0.114])
    top = np.percentile(lum[circle[..., 3] > 128], 90) if (circle[..., 3] > 128).any() else 255.0
    light = np.clip(lum / max(top, 1.0) * 236.0, 0, 255)
    circle[..., 0] = circle[..., 1] = circle[..., 2] = light
    return Image.fromarray(circle.astype(np.uint8), "RGBA")


# ---- The rest's pieces ----------------------------------------------------------------------------------------


def menu_bar(camp: Path) -> Image.Image:
    """The menu's lit bar (the camp's bar_selected) without its yellow glow, cut to what is left."""
    box = [line.split()[2:6] for line in (camp / "camp.txt").read_text(encoding="utf-8").splitlines(True)
           if line.startswith("piece bar_selected ")][0]  # fmt: skip
    u, v, w, h = map(int, box)
    bar = np.asarray(Image.open(camp / "camp_atlas.png").convert("RGBA").crop((u, v, u + w, v + h)), float).copy()
    glow = (bar[..., 1] > 40) & (np.abs(bar[..., 0] - bar[..., 1]) < 70) & (bar[..., 2] < 0.7 * bar[..., 1])
    bar[glow, 3] = 0
    im = Image.fromarray(bar.astype(np.uint8), "RGBA")
    return im.crop(im.getbbox())


def faces(save: Path) -> list[Image.Image]:
    """The files' faces (the port's save list's strips: its save.txt's pieces face0-2 in its atlas), the DS's plate
    round them cut off (its dark top line, its bevel, its cut corner), more of their colour, their pixels kept sharp
    (FACE_K times as large, each pixel a square), faded out at the right end (the panel's round corners)."""
    atlas = Image.open(save / "save_atlas.png").convert("RGBA")
    places = {}
    for line in (save / "save.txt").read_text(encoding="utf-8").splitlines():
        words = line.split()
        if len(words) == 6 and words[0] == "piece":
            places[words[1]] = tuple(int(v) for v in words[2:])
    out = []
    for i in range(3):
        u, v, w, h = places[f"face{i}"]
        f = atlas.crop((u, v, u + w, v + h)).crop((0, 4, 104, 31))
        rgb = ImageEnhance.Brightness(ImageEnhance.Color(f.convert("RGB")).enhance(1.9)).enhance(1.15).convert("RGBA")
        rgb.putalpha(f.getchannel("A"))
        big = np.asarray(rgb.resize((f.width * FACE_K, f.height * FACE_K), Image.NEAREST), float)
        x = (np.arange(big.shape[1]) + 0.5) / big.shape[1]
        big[..., 3] *= np.clip((1 - x) / 0.04, 0, 1)[None, :]
        out.append(Image.fromarray(big.astype(np.uint8), "RGBA"))
    return out


def wiped_pill(band: Image.Image) -> tuple[np.ndarray, tuple[int, int, int, int]]:
    """The band's pixels with the pill's SAVE wiped (each row the pill's own grey beside it), and SAVE's box."""
    a = np.asarray(band).astype(float)
    x0, y0, x1, y1 = PILL
    ys, xs = np.where(a[y0:y1, x0:x1, :3].mean(-1) < 60)
    tb = (xs.min() + x0, ys.min() + y0, xs.max() + x0 + 1, ys.max() + y0 + 1)
    m = 4
    for y in range(tb[1] - m, tb[3] + m):
        a[y, tb[0] - m : tb[2] + m, :3] = a[y, tb[0] - m - 6, :3]
    return a, tb


def written(a: np.ndarray, tb, ink: Image.Image, top: int) -> Image.Image:
    """The wiped band with a word's ink written dark at `top`, its middle SAVE's."""
    out = Image.fromarray(a.astype(np.uint8), "RGBA")
    text = Image.new("RGBA", ink.size, (12, 12, 12, 255))
    text.putalpha(ink)
    out.alpha_composite(text, (round((tb[0] + tb[2]) / 2 - ink.width / 2), top))
    return out


def load_band(band: Image.Image, font: Path) -> Image.Image:
    """The load list's band from the save page's: LOAD written in KHGummi over the pill's SAVE, as high as SAVE (the
    same font), laid out as raqm_text.ink lays it."""
    a, tb = wiped_pill(band)
    size = 200 * (tb[3] - tb[1]) / raqm_text.ink(font, "SAVE", 200).height
    return written(a, tb, raqm_text.ink(font, "LOAD", size), tb[1])


def worded_band(band: Image.Image, word: Image.Image, save: Image.Image) -> Image.Image:
    """The band with KH2's `word` of a language (its ink) written over the pill's SAVE: as high as SAVE (that
    language's KH2 SAVE, `save`, made as high as the band's), smaller when the pill's straight part is narrower."""
    a, tb = wiped_pill(band)
    middle = ((tb[0] + tb[2]) // 2, (tb[1] + tb[3]) // 2)
    light = a[..., :3].mean(-1) > 120  # the pill's grey (SAVE wiped)
    left = right = middle[0]
    while left > 0 and light[middle[1], left - 1]:
        left -= 1
    while right < light.shape[1] - 1 and light[middle[1], right + 1]:
        right += 1
    top = bottom = middle[1]
    while top > 0 and light[top - 1, middle[0]]:
        top -= 1
    while bottom < light.shape[0] - 1 and light[bottom + 1, middle[0]]:
        bottom += 1
    room = right - left + 1 - (bottom - top + 1)  # less its round ends
    k = min((tb[3] - tb[1]) / save.height, room / word.width)
    ink = word.resize((round(word.width * k), round(word.height * k)), Image.LANCZOS)
    return written(a, tb, ink, round(middle[1] - ink.height / 2))


def full_width(band: Image.Image) -> Image.Image:
    """A band as wide as the picture."""
    return band.resize((1920, round(band.height * 1920 / band.width)), Image.LANCZOS)


def shelves(images: dict[str, Image.Image], order: list[str], top: int) -> tuple[dict[str, tuple[int, int]], int]:
    """The pictures in `order` on shelves of the atlas from `top` (a dot of room round each): their places and the
    shelves' end."""
    x, y, shelf = 0, top, 0
    at = {}
    for n in order:
        im = images[n]
        if x + im.width + 2 > ATLAS_W:
            x, y, shelf = 0, y + shelf + 2, 0
        at[n] = (x + 1, y + 1)
        x += im.width + 2
        shelf = max(shelf, im.height)
    return at, y + shelf + 2


# ---- The page -------------------------------------------------------------------------------------------------


def make(inputs: Inputs, mod: Path) -> None:
    out = mod / "layouts" / "campsave"
    out.mkdir(parents=True, exist_ok=True)
    kh2 = SaveList(inputs.kh2)
    pieces: dict[str, Image.Image] = {}
    places = []

    # the frame: drawn at its size, with room for its shadow
    fw, fh = FRAME[2] + 2 * PAD, FRAME[3] + 2 * PAD
    pieces["frame"] = kh2.group(fw, fh, 83, 8, (PAD - 14 * FX, PAD - 72 * FY), FX, FY, tint=grey)
    places.append("place frame %d %d %d %d" % (FRAME[0] - PAD, FRAME[1] - PAD, fw, fh))

    # the rows: the panel at (PAD, PAD) of its piece, without the number's ring
    rw, rh = ROW_W + 2 * PAD, ROW_H + 2 * PAD
    row_origin = (PAD - 20 * RX, PAD - 77 * RY)
    for name, gi, fr in (("row", 87, 8), ("row_selected", 85, 7)):
        pieces[name] = kh2.group(rw, rh, gi, fr, row_origin, RX, RY, tint=grey, skip=(54,))
    places.append("place row_piece %d %d %d %d" % (-PAD, -PAD, rw, rh))
    # the ring where the row draws it (scaling x and y apart, it comes out flattened): the number's place
    bb = kh2.group(rw, rh, 85, 7, row_origin, RX, RY, only=(54,)).getbbox()
    ring_middle = ((bb[0] + bb[2]) / 2 - PAD, (bb[1] + bb[3]) / 2 - PAD)
    d = bb[3] - bb[1] + 12
    pieces["ring"] = ring(kh2, bb[3] - bb[1])
    places.append("place ring %.1f %.1f %d %d" % (ring_middle[0] - d / 2, ring_middle[1] - d / 2, d, d))
    row_alpha = np.asarray(pieces["row"])[:, :, 3]
    body_right = int(np.nonzero(row_alpha[PAD + ROW_H // 2] > 128)[0].max()) + 1 - PAD  # the panel's right end
    # the panel's own drawing within its piece: KH2's g53 is its body 4/140 to 126/140 of its height (a shadow under)
    body = (round(ROW_H * 4 / 140.0) + 2, round(ROW_H * 122 / 140.0) - 2)

    pieces["munny"] = kh2.sprite(MUNNY_WORD, bright=2.0)  # KH2's MUNNY word (white: tinted when drawn)
    pieces["scroll_bar"] = kh2.stack([(41, None), (42, BODY_LEN), (43, None)], SCROLL[2] * SK)
    pieces["scroll_thumb"] = kh2.stack([(44, None), (45, THUMB), (46, None)], SCROLL[2] * SK)
    # where the thumb's picture goes (from the bar's top): from A (t = 0) to B (t at its lowest), its height
    groove = (CAP - 9 * DOT, CAP - 9 * DOT + BODY_LEN - THUMB, pieces["scroll_thumb"].height / SK)
    pieces["crown"] = kh2.sprite(108)

    pieces["menu_bar"] = menu_bar(mod / "layouts" / "camp" / "en")
    menu_bar_h = round(440 * pieces["menu_bar"].height / pieces["menu_bar"].width)
    crown_h = CROWN_W * pieces["crown"].height / pieces["crown"].width
    crown = (ring_middle[0] - CROWN_W / 2, ring_middle[1] - d / 2 - crown_h * 0.62, CROWN_W, crown_h)

    for i, im in enumerate(campsave_worlds.icons(inputs.ds("UI/cm/cm.p2"), WORLDS).values()):
        pieces["world%d" % i] = im
    for i, im in enumerate(faces(inputs.hd / "layouts" / "save" / "en")):
        pieces["face%d" % i] = im

    band = Image.open(ART / "campsave" / "band.png").convert("RGBA")
    pieces["band"] = full_width(band)
    places.append("place band 0 0 1920 %d" % pieces["band"].height)
    pieces["band_load"] = full_width(load_band(band, inputs.font("KHGummi.otf")))
    # the other languages': KH2's words of each (its SAVE and LOAD in the pill, its MUNNY); the page draws its
    # language's pieces (band_es, band_load_es, munny_es) when it has them
    for lang, folder in KH2_LANGS.items():
        if lang != "en":
            words = SaveList(inputs.kh2, folder)
            save = words.ink(SAVE_WORD)
            pieces[f"band_{lang}"] = full_width(worded_band(band, save, save))
            pieces[f"band_load_{lang}"] = full_width(worded_band(band, words.ink(LOAD_WORD), save))
            pieces[f"munny_{lang}"] = words.sprite(MUNNY_WORD, bright=2.0)

    # the atlas: the pieces on shelves, tallest first; the worlds' titles on shelves under them
    at, end = shelves(pieces, sorted(pieces, key=lambda n: -pieces[n].height), 0)
    titles = {"title%d" % i: im for i, im in enumerate(campsave_titles.titles(inputs, WORLDS).values())}
    title_at, height = shelves(titles, sorted(titles, key=lambda n: -titles[n].height), end)
    atlas = Image.new("RGBA", (ATLAS_W, height), (0, 0, 0, 0))
    for images, spots in ((pieces, at), (titles, title_at)):
        for n, (px, py) in spots.items():
            atlas.paste(images[n], (px, py))
    atlas.save(out / "campsave_atlas.png")

    lines = HEADER + ["atlas campsave_atlas.png"]
    lines += ["piece %s %d %d %d %d" % (n, px, py, pieces[n].width, pieces[n].height) for n, (px, py) in at.items()]
    lines += ["piece %s %d %d %d %d" % (n, *title_at[n], titles[n].width, titles[n].height) for n in titles]
    lines += places
    lines += [
        "rows %d %d %d %d %d %d" % ROWS,
        # in a row (from its panel's top left): the number in the ring (middle x, middle y, -, capitals' height)
        "place number %d %d 0 26" % (round(ring_middle[0]), round(ring_middle[1])),
        "place world 141 6 168 146",  # the world's picture (its box)
        "place title 120 66 210 80",  # its title over its lower part (fit in, middle and bottom the box's)
        "place face %d %d 640 %d" % (body_right - 4 - 640, body[0], body[1]),  # the face: its right the panel's
        "place day 352 27 0 27",  # x, capitals' top, -, their height
        "place level 352 97 0 22",
        "place difficulty 20 97 0 22",  # after the level's words: this gap, its top, -, its height
        "place munny 610 21 0 28",  # MUNNY's left, the digits' top (the time's), -, their height (the word 0.8)
        "place time 1235 21 0 28",  # the time's right, top, -, height
        "place crown %.1f %.1f %d %d" % crown,  # on the ring, its middle the number's
        "place empty 650 70 0 22",  # middle x, middle y, -, capitals' height
        "place glove_at -80 0 0 0",
        "place scroll %d %d %d %d" % SCROLL,  # the bar: middle x, top, width, height
        # the thumb's picture's top (from the bar's top) at the list's top and end, its height
        "place scroll_groove %.2f %.2f %.2f 0" % groove,
        "place button 425 104 440 %d" % menu_bar_h,  # the option chosen on the menu (menu_bar), beside SAVE's pill
        "place menu_button 284 84 440 %d" % menu_bar_h,  # the same on the camp's other pages, beside MENU's pill
        # a question's answers on the page (the menu's buttons, smaller, under the question), the question this
        # much higher then
        "place answer_yes 520 1004 250 62",
        "place answer_no 830 1004 250 62",
        "place ask 0 -40 0 0",
        "colour munny_word 255 255 255",
        "colour munny_digits 250 165 26",
        "face_alpha 0.5",
        "words empty en Save new file.",
        "words empty fr Nouvelle sauvegarde.",
        "words empty de Neuer Spielstand.",
        "words empty it Nuovo salvataggio.",
        "words empty es Nueva partida.",
    ]
    # the load list's words (the port's save list's, layouts/save/<lang>/save.txt): an empty file's, the guest's row's
    for lang in LANGS:
        path = inputs.hd / "layouts" / "save" / lang / "save.txt"
        if path.exists():
            for line in path.read_text(encoding="utf-8").splitlines():
                if line.startswith("words empty "):
                    lines.append("words load_empty %s %s" % (lang, line[12:]))
                elif line.startswith("words guest "):
                    lines.append("words guest %s %s" % (lang, line[12:]))
    # the Files pages' font
    files = mod / "layouts" / "files"
    lines += [line for line in (files / "files.txt").read_text(encoding="utf-8").splitlines()
              if line.startswith("font ") or line.startswith("glyph ")]  # fmt: skip
    Image.open(files / "files_font.png").save(out / "files_font.png")
    (out / "campsave.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
