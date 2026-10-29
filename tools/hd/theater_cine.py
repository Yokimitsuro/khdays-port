#!/usr/bin/env python3
"""MODO CINE's list as the 358/2 Days theater shows its chapters, from your own copy of the HD 1.5+2.5
collection (MARE: docs/HD_SCREENS.md, "Making them"), into mods/NAME/layouts/cine/.
  python tools/hd/theater_cine.py MARE [--mod NAME] [--lang es|en|fr|de|it ...] [--data DIR] [--preview DIR]
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import save_screen as ss  # noqa: E402
import theater_movies as tm  # noqa: E402
import theater_title as tt  # noqa: E402
import decompress_data as dd  # noqa: E402
from newgame_dialog import ctd, plain  # noqa: E402

LAYOUT = 118
ROWS = (903, 904)  # the theater's first two rows (groups): the rows' place and pitch
BAR, LINE, DAY, DIGITS, REPORT = 110, 116, 111, (112, 113, 114, 115), 1190  # the first row's nodes
HEADING, HELP_BAR, SCROLL, ARTWORK, VEIL = 102, 104, 800, 106, 700
NORMAL, SELECTED = 1, 5  # a row's controls: 5 Days's red bar (4 Re:coded's blue, 0 a lit square)
SHOWN = 10  # the game's page (Ov000_DrawListScene)
SQUARE_RIGHT = 340  # the bar's dark square ends here (its own part)
# The digits: the theater's strip of them (0-9, then ~) in its texture, 64 texels apart.
DIGIT_STRIP = (600, 1070, 1320, 1125)  # its place in block 0 (x0, y0, x1, y1)
LAUNCHER_GLOVE = (1972, 484, 2036, 532)  # launcher.l2d block 0, its part 65
MEASURED = {
    # on the capture (1920 x 1080, the client area's pixels):
    "name": (696, 17, 26),  # a row's name: its left, its capitals' top below the row's, their height
    "name_tracking": ("Apertura", 159),  # a name with no space: its ink's width
    "name_space": ("Los que nunca existieron", 449),
    "help": (315, 1007, 21),  # the help: its left, its capitals' top, their height
    "digits": (530.2, 19, 31, 25.6),  # the day: the last digit's middle, its top below the row's,
    # height, the digits' pitch (centred in it, rightmost last)
    "digit_colour": (243, 234, 39),
    "glove": (557.5, -1.7, 1.9),  # the glove's part's left and top (from the row's), its scale
    "thumb": 130,  # the scroll bar's thumb's length
    "grey": 255,
}
LENGTH_RIGHT_MARGIN = 14  # a length's right edge: this far inside the report icon's (the port's)
# The keyboard's keys in the theater's icons (<lang dir>/font/Icon.tm2): FontIcon.inf's boxes
# (u32 the count at 4, then 16 bytes each from 0x10: u16 its code, x0, y0, x1, y1), the rows of
# keys in the order the sheet shows them, by their boxes' top -- as SDL scancodes.
KEY_ROWS = {
    525: list(range(4, 20)),  # A-P
    591: list(range(20, 30)) + [225, 229, 224, 228, 226, 230],  # Q-Z, the shifts, ctrls, alts
    657: list(range(30, 40)) + [82, 81, 80, 79, 40],  # 1-0, up, down, left, right, enter
    723: list(range(89, 99)) + [84, 85, 86, 87, 42, 44],  # the keypad's 1-0 / * - +, backspace, space
    789: list(range(58, 70)) + [99, 41, 73, 76],  # F1-F12, the keypad's ., esc, insert, delete
}


# --- The theater's data -------------------------------------------------------------------------


def strings_at(b: bytes, base: int, offset: int) -> str:
    return b[base + offset : b.index(b"\0", base + offset)].decode("ascii")


def theater_scenes(mare: Path):
    b = (mare / "data" / "DaysTheater.thd").read_bytes()
    if b[:4] != b"THD@":
        raise ValueError("DaysTheater.thd: not a THD")
    n = struct.unpack_from("<H", b, 6)[0]
    base = 0x10 + 28 * n
    out = []
    for i in range(n):
        r = struct.unpack_from("<14H", b, 0x10 + 28 * i)
        out.append({"event": strings_at(b, base, r[4]), "video": strings_at(b, base, r[6])})
    return out


def theater_chapters(mare: Path):
    b = (mare / "data" / "DaysTheater.chp").read_bytes()
    if b[:4] != b"CHP@":
        raise ValueError("DaysTheater.chp: not a CHP")
    n = struct.unpack_from("<H", b, 6)[0]
    base = 8 + 96 * n
    out = []
    for i in range(n):
        r = struct.unpack_from("<48H", b, 8 + 96 * i)
        out.append(
            {
                "day": r[1],
                "art": strings_at(b, base, r[4]),
                "scenes": list(r[18 : 18 + r[16]]),
                "title": (r[13], r[12]),  # its name: <lang dir>/ctd/ct00100.ctd's (group, id)
            }
        )
    return out


# The collection's names of the game's opening and ending: the port's videos are hd802 and hd843
# (theater_movies.py); Re:coded's ending is not Days's.
VIDEO_STEMS = {"DOP.mp4": "hd802", "DED.mp4": "hd843"}
NOT_DAYS = {"CED.mp4"}


def chapter_stems(chapter, scenes):
    """The chapter's videos in order, as the port names them (its scenes without a video left out)."""
    out = []
    for s in chapter["scenes"]:
        video = scenes[s]["video"]
        if video and video not in NOT_DAYS:
            out.append(VIDEO_STEMS.get(video, Path(video).stem))
    return out


def chapter_titles(folder: Path):
    """The chapters' names in a language: its ct00100.ctd, without its control codes."""
    table = ctd(folder / "ctd" / "ct00100.ctd")
    return {key: " ".join(plain(text).split()) for key, text in table.items()}


def ds_movies(data: Path):
    """The game's list's movies (Ov000_FillListRows: thr.p2's entry 7, a u16 each)."""
    table = ss.p2_entry(data / "nitrofs" / "UI" / "thr" / "thr.p2", 7)
    return list(struct.unpack_from(f"<{len(table) // 2}H", table))


def scene_chapters(movies, scenes, chapters):
    """Each movie's chapter (its index), as the module's notes say."""
    ending = next(i for i, c in enumerate(chapters) if any(scenes[s]["video"] == "DED.mp4" for s in c["scenes"]))
    out, last = [], 0
    for movie in movies:
        mine = {i for i, s in enumerate(scenes) if s["event"].startswith(f"hd{movie}/")}
        held = [i for i, c in enumerate(chapters) if mine & set(c["scenes"])]
        later = [i for i in held if i >= last]
        chapter = later[0] if later else held[0] if held else ending
        out.append(chapter)
        last = chapter
    return out


def game_strings(data: Path, lang: str):
    """thr_<lang>.s's records (the game's own words, for the preview)."""
    raw = (data / "nitrofs" / "UI" / "thr" / f"thr_{lang}.s.z").read_bytes()
    b = dd.decompress_nitro(raw)[1]
    at, count = struct.unpack_from("<II", b, 0)
    out = []
    for _ in range(count):
        n = struct.unpack_from("<I", b, at)[0]
        out.append(b[at + 4 : at + n].decode("utf-16-le").rstrip("\0"))
        at += n
    return out


# --- Pictures of the layout ---------------------------------------------------------------------


def nodes_under(layouts, nid):
    nodes = layouts.layout_nodes(LAYOUT)
    ids = [n[12] for n in nodes]
    keep, changed = {nid}, True
    while changed:
        changed = False
        for n in nodes:
            if n[9] >= 0 and ids[n[9]] in keep and n[12] not in keep:
                keep.add(n[12])
                changed = True
    return keep


def parts(layouts, shown: dict, frame=0.0):
    """The layout's parts of the nodes `shown` gives controls to (with their parents, placed and
    faded as in the layout), in the order drawn."""
    nodes = layouts.layout_nodes(LAYOUT)
    ids = [n[12] for n in nodes]
    controls = {i: None for i in ids}
    for k, n in enumerate(nodes):
        if n[12] in shown:
            controls[n[12]] = shown[n[12]]
            p = n[9]
            while p >= 0:
                if controls[ids[p]] is None:
                    controls[ids[p]] = tt.STILL  # (the root's control 1 is the screen's appearing)
                p = nodes[p][9]
    return [
        p for p in layouts.pieces(LAYOUT, frame, controls, {}) if p["node"] in shown and p["region"][0] == "texture"
    ]


def picture(layouts, pieces):
    """The parts drawn as the port draws them (theater_title.render), on black and on white: their
    colour and alpha, cut to where they ink; and that place in the picture."""
    if not pieces:
        raise ValueError("nothing to draw")
    atlas = tt.Atlas()
    for p in pieces:
        _, base, ua, va, ub, vb = p["region"]
        atlas.add(p["region"], layouts.blocks[base].texture[va:vb, ua:ub])
    sheet = atlas.pack()
    entries = [{"piece": p, "keys": [(0.0, list(p["rect"]) + list(p["colour"]))]} for p in pieces]
    texels = [atlas.texels(p) for p in pieces]
    grounds = []
    for ground in ((0.0, 0.0, 0.0), (1.0, 1.0, 1.0)):
        tt.GROUND = ground
        grounds.append(tt.render(sheet, texels, entries, 0.0, tt.PICTURE))
    on_black, on_white = grounds
    alpha = np.clip(1.0 - (on_white - on_black).mean(axis=2), 0, 1)
    colour = np.where(alpha[..., None] > 1e-3, on_black / np.maximum(alpha[..., None], 1e-3), 0)
    rgba = np.dstack([np.clip(colour, 0, 1), alpha])
    ys, xs = np.nonzero(alpha > 1 / 255)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return (rgba[y0:y1, x0:x1] * 255 + 0.5).astype(np.uint8), (int(x0), int(y0), int(x1 - x0), int(y1 - y0))


def key_icons(folder: Path):
    """The theater's keyboard icons (KEY_ROWS) by 'key:<scancode>'; none without its files."""
    sheet_path, boxes_path = folder / "font" / "Icon.tm2", folder / "font" / "FontIcon.inf"
    if not sheet_path.exists() or not boxes_path.exists():
        return {}
    sheet = tt.tim2_picture(sheet_path.read_bytes(), 0)
    b = boxes_path.read_bytes()
    rows = {}
    for i in range(struct.unpack_from("<I", b, 4)[0]):
        _, x0, y0, x1, y1 = struct.unpack_from("<5H", b, 0x10 + 16 * i)
        rows.setdefault(y0, []).append((x0, x1, y1))
    out = {}
    for top, codes in KEY_ROWS.items():
        boxes = sorted(rows.get(top, []))
        for code, (x0, x1, y1) in zip(codes, boxes):
            out[f"key:{code}"] = sheet[top:y1, x0:x1]
    return out


def texels(layouts, region, white=False):
    _, base, ua, va, ub, vb = region
    px = layouts.blocks[base].texture[va:vb, ua:ub].copy()
    if white:
        px[..., :3] = 255
    return px


# --- The menu font ------------------------------------------------------------------------------


def list_font(folder: Path):
    """The font the theater writes its chapters' names in: its event font (theater_movies.py's,
    its subtitles'; the capture's names are its glyphs), white outlined in black. Each glyph's
    texels (RGBA, the PS2's grey doubled), its white's left and right in its cell (the capture was
    measured on the white); the capitals' white's top and height (its E's)."""
    font = tm.EventFont(folder / "font")
    glyphs = {}
    for c, (x, y, w) in font.glyphs.items():
        px = font.picture[y : y + 32, x : x + font.ink[c]].copy()  # its whole outline; it advances w
        if px.shape[:2] != (32, font.ink[c]):
            continue  # a glyph whose cell is not in the picture
        px[..., :3] = np.minimum(255, px[..., :3].astype(np.int32) * 2).astype(np.uint8)
        white = (px[..., :3].min(axis=2) > 170) & (px[..., 3] > 128)
        cols = np.nonzero(white.any(axis=0))[0]
        glyphs[c] = (px, int(cols.min()) if len(cols) else 0, int(cols.max()) + 1 if len(cols) else 0, w)
    e = glyphs["E"][0]
    rows = np.nonzero(((e[..., :3].min(axis=2) > 170) & (e[..., 3] > 128)).any(axis=1))[0]
    return glyphs, int(rows.min()), int(rows.max() - rows.min() + 1)


def ink_width(glyphs, text, size, tracking, space):
    """A line's ink's width: each glyph advancing its width (times size) and the tracking."""
    x, left, right = 0.0, None, 0.0
    for c in text:
        if c == " ":
            x += space * size + tracking
            continue
        _, l, r, w = glyphs[c]
        left = x + l * size if left is None else left
        right = x + r * size
        x += w * size + tracking
    return right - (left or 0.0)


def fit_spacing(glyphs, size):
    """The tracking and the space's width that give the measured names their widths."""
    word, width = MEASURED["name_tracking"]
    lo, hi = -10.0, 20.0
    for _ in range(40):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if ink_width(glyphs, word, size, mid, 0) < width else (lo, mid)
    tracking = (lo + hi) / 2
    words, width = MEASURED["name_space"]
    lo, hi = 0.0, 40.0
    for _ in range(40):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if ink_width(glyphs, words, size, tracking, mid) < width else (lo, mid)
    return tracking, (lo + hi) / 2


# --- The files ----------------------------------------------------------------------------------


def list_pieces(layouts, launcher, lang: str):
    """The theater's chapter list's pieces, in the order they go to the atlas: (name, pixels, place
    or None) -- the veil, heading, help bar, a row normal and selected, its square and DIA, the
    digits, the scroll bar, the launcher's glove -- and the rows' first top and pitch, a length's
    right edge, the digits' height. The places of a row's pieces are from its top."""
    found = []

    def piece(name, px, place=None):
        found.append((name, px, place))

    first = parts(layouts, {BAR: NORMAL})
    top = min(p["rect"][1] for p in first)
    pitch = min(p["rect"][1] for p in parts(layouts, {BAR + 10: NORMAL})) - top
    row_top = top - pitch  # the play-all bar's row: the first scene's

    def row_place(rect):
        x, y, w, h = rect
        return (x, y - top, w, h)

    veil, rect = picture(layouts, parts(layouts, {VEIL: tt.STILL}))
    # a smooth gradient (one part's corners): kept small, the port stretches it
    small = np.asarray(
        Image.fromarray(veil, "RGBA").resize((max(2, rect[2] // 8), max(2, rect[3] // 8)), Image.BILINEAR)
    )
    piece("veil", small, rect)
    for name, node in (("heading", HEADING), ("help_bar", HELP_BAR)):
        px, rect = picture(layouts, parts(layouts, {node: tt.STILL}))
        piece(name, px, rect)
    for name, control in (("row", NORMAL), ("row_selected", SELECTED)):
        bar = [p for p in parts(layouts, {BAR: control, LINE: control}) if p["rect"][0] >= SQUARE_RIGHT]
        px, rect = picture(layouts, bar)
        piece(name, px, row_place(rect))
    square = [p for p in parts(layouts, {BAR: NORMAL}) if p["rect"][2] <= SQUARE_RIGHT]
    px, rect = picture(layouts, square)
    piece("square", px, row_place(rect))
    px, rect = picture(layouts, parts(layouts, {DAY: NORMAL}))
    piece("day", px, row_place(rect))
    report = parts(layouts, {REPORT: NORMAL})
    length_right = max(p["rect"][2] for p in report) - LENGTH_RIGHT_MARGIN

    # the digits: each one's ink in the strip, white (tinted when drawn)
    sx0, sy0, sx1, sy1 = DIGIT_STRIP
    strip = layouts.blocks[0].texture[sy0:sy1, sx0:sx1]
    ink = strip[..., 3] > 30
    cols = ink.any(axis=0)
    runs, on = [], None
    for x, c in enumerate(cols):
        if c and on is None:
            on = x
        if not c and on is not None:
            runs.append((on, x))
            on = None
    rows = np.nonzero(ink.any(axis=1))[0]
    if len(runs) < 10:
        raise ValueError(f"{lang}: the digits' strip has {len(runs)} glyphs")
    for d in range(10):
        x0, x1 = runs[d]
        px = strip[rows.min() : rows.max() + 1, x0:x1].copy()
        px[..., :3] = 255
        piece(f"digit{d}", px)
    digit_h = int(rows.max() - rows.min() + 1)

    # the scroll bar: its trough over the list, its thumb (the upper piece, stretched), its end
    bar_parts = parts(layouts, {SCROLL: tt.STILL})
    trough = max(bar_parts, key=lambda p: p["rect"][3] - p["rect"][1])
    ends = sorted((p for p in bar_parts if p is not trough), key=lambda p: p["rect"][1])
    tx0, _, tx1, ty1 = trough["rect"]
    piece("trough", texels(layouts, trough["region"]), (tx0, row_top, tx1 - tx0, ty1 - row_top))
    upper, lower = ends[0], ends[-1]
    piece(
        "thumb",
        texels(layouts, upper["region"]),
        (upper["rect"][0], row_top, upper["rect"][2] - upper["rect"][0], MEASURED["thumb"]),
    )
    piece(
        "end",
        texels(layouts, lower["region"]),
        (lower["rect"][0], lower["rect"][1], lower["rect"][2] - lower["rect"][0], lower["rect"][3] - lower["rect"][1]),
    )

    gx, gy, k = MEASURED["glove"]
    gu0, gv0, gu1, gv1 = LAUNCHER_GLOVE
    glove = launcher.blocks[0].texture[gv0:gv1, gu0:gu1]
    piece("glove", glove, (gx, gy, (gu1 - gu0) * k, (gv1 - gv0) * k))
    return found, row_top, pitch, length_right, digit_h


def make(mare: Path, data: Path, lang: str, mod: Path, preview: Path | None):
    folder_name = tt.LANGUAGES[lang][0]
    folder = mare / folder_name
    layouts = tt.Layouts(folder / "l2d" / "days_theatre.l2d")
    launcher = tt.Layouts(folder / "l2d" / "launcher.l2d")
    scenes, chapters = theater_scenes(mare), theater_chapters(mare)
    movies = ds_movies(data)
    chapter_of = scene_chapters(movies, scenes, chapters)
    atlas = tt.Atlas()
    pieces, places = [], []

    def piece(name, px, place=None):
        atlas.add((name,), px)
        pieces.append(name)
        if place is not None:
            places.append((name, place))

    found, row_top, pitch, length_right, digit_h = list_pieces(layouts, launcher, lang)
    for name, px, place in found:
        piece(name, px, place)

    icons = {**tt.icon_pixels(folder / "font"), **key_icons(folder)}
    for name, px in icons.items():
        atlas.add(("icon", name), px)

    # A chapter without a name of its own goes on the named one before it (seen: always the same
    # day); the opening's has none: the game's name for it.
    titles = chapter_titles(folder)
    opening = game_strings(data, lang)[1]
    named = []
    for c in chapters:
        name = titles.get(c["title"], "")
        if name or not named:
            named.append({"day": c["day"], "art": c["art"], "stems": [], "name": name or opening})
        named[-1]["stems"] += chapter_stems(c, scenes)
    named = [c for c in named if c["stems"]]

    # the artworks the list shows (its movies' chapters', the named chapters'), once for every language
    art_dir = mod / "art"
    art_dir.mkdir(parents=True, exist_ok=True)
    for name in sorted({chapters[i]["art"] for i in chapter_of} | {c["art"] for c in named}):
        out = art_dir / (Path(name).stem + ".png")
        if not out.exists():
            px = tt.tim2_picture((mare / "bg" / "artwork" / name).read_bytes(), 0)
            Image.fromarray(px).convert("RGB").save(out)

    glyphs, cap_top, cap_h = list_font(folder)
    name_x, name_y, name_cap = MEASURED["name"]
    name_size = name_cap / cap_h
    tracking, space = fit_spacing(glyphs, name_size)
    help_x, help_y, help_cap = MEASURED["help"]

    pixels = atlas.pack()
    lines = [
        "# MODO CINE's list as the 358/2 Days theater shows its chapters, made by",
        "# tools/hd/theater_cine.py from the player's own copy (host/screens/cine_screen.c reads it).",
        "#   piece NAME U V W H         a picture in the atlas",
        "#   place NAME X Y W H         where it goes: in the picture (1920 x 1080), or from a row's",
        "#                              top for a row's (row, row_selected, square, day, glove)",
        "#   rows Y PITCH SHOWN          the first row's top, the rows' pitch, how many show",
        "#   text NAME X Y SIZE ALIGN GREY   a line's place (from a row's top for name and length;",
        "#                              Y its capitals' top), size (the font's scale), alignment, grey",
        "#   digits X Y H PITCH R G B   the day: the last digit's middle, its top from the row's,",
        "#                              its height, the digits' pitch, their colour",
        "#   scene MOVIE DAY ART        each of the game's movies: its chapter's day, its artwork",
        "#                              (layouts/cine/art/ART)",
        "#   chapter DAY ART VIDEO... | NAME   each of the theater's named chapters with videos, in its",
        "#                              order: its day, its artwork, its videos and those of the",
        "#                              unnamed chapters after it, in a row, its name",
        "#   icon NAME U V W H          an input's icon: xbox:<input>, playstation:<input>, pad:<input>,",
        "#                              key:<SDL scancode>",
        "#   font FILE CELL TOP BOTTOM rgba   the font's picture (its own colours: white outlined",
        "#                              in black), its cells' height, its capitals' white",
        "#   glyph CODE X Y W LEFT ADVANCE   the font's glyphs (place, width, bearing, advance)",
        f"picture {tt.PICTURE[0]} {tt.PICTURE[1]}",
        "atlas cine_atlas.png",
    ]
    for name in pieces:
        u, v = atlas.at[(name,)]
        h, w = atlas.pixels[(name,)].shape[:2]
        lines.append(f"piece {name} {u} {v} {w} {h}")
    for name, (x, y, w, h) in places:
        lines.append(f"place {name} {x:.1f} {y:.1f} {w:.1f} {h:.1f}")
    lines.append(f"rows {row_top:.1f} {pitch:.1f} {SHOWN}")
    grey = MEASURED["grey"]
    lines.append(f"text name {name_x:.1f} {name_y:.1f} {name_size:.4f} left {grey}")
    lines.append(f"text length {length_right:.1f} {name_y:.1f} {name_size:.4f} right {grey}")
    lines.append(f"text help {help_x:.1f} {help_y:.1f} {help_cap / cap_h:.4f} left {grey}")
    dx, dy, dh, dp = MEASURED["digits"]
    r, g, b = MEASURED["digit_colour"]
    lines.append(f"digits {dx:.1f} {dy:.1f} {dh} {dp:.1f} {r} {g} {b}")
    for movie, chapter in zip(movies, chapter_of):
        c = chapters[chapter]
        lines.append(f"scene {movie} {c['day']} {Path(c['art']).stem}.png")
    for c in named:
        lines.append(f"chapter {c['day']} {Path(c['art']).stem}.png {' '.join(c['stems'])} | {c['name']}")
    for name, px in icons.items():
        u, v = atlas.at[("icon", name)]
        lines.append(f"icon {name} {u} {v} {px.shape[1]} {px.shape[0]}")

    # the font: its glyphs in cells, each advancing its width and the tracking (in its texels)
    cols_n = 32
    cell_w = max(gl[0].shape[1] for gl in glyphs.values())
    codes = sorted(glyphs, key=ord)
    font_px = np.zeros((((len(codes) + cols_n - 1) // cols_n) * 32, cols_n * cell_w, 4), np.uint8)
    lines.append(f"font cine_font.png 32 {cap_top} {cap_top + cap_h} rgba")
    if " " not in glyphs:
        lines.append(f"glyph 32 0 0 0 0 {space + tracking / name_size:.2f}")
    for n, c in enumerate(codes):
        glyph_px, _, _, w = glyphs[c]
        gx_, gy_ = (n % cols_n) * cell_w, (n // cols_n) * 32
        font_px[gy_ : gy_ + 32, gx_ : gx_ + glyph_px.shape[1]] = glyph_px
        advance = (space if c == " " else w) + tracking / name_size
        lines.append(f"glyph {ord(c)} {gx_} {gy_} {glyph_px.shape[1]} 0 {advance:.2f}")

    out_dir = mod / lang
    out_dir.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels, "RGBA").save(out_dir / "cine_atlas.png")
    Image.fromarray(font_px, "RGBA").save(out_dir / "cine_font.png")
    (out_dir / "cine.txt").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    print(
        f"{lang}: {len(pieces)} pieces, {len(icons)} icons, {len(codes)} glyphs, {len(movies)} scenes, "
        f"{len(named)} chapters with videos; tracking {tracking:.2f}, space {space:.2f}"
    )
    if preview is not None:
        preview.mkdir(parents=True, exist_ok=True)
        draw_preview(
            pixels,
            atlas,
            dict(places),
            lines,
            mod,
            game_strings(data, lang),
            movies,
            chapter_of,
            chapters,
            glyphs,
            name_size,
            tracking,
            space,
            cap_top,
            digit_h,
            preview / f"{lang}.png",
        )


def draw_preview(
    pixels,
    atlas,
    place,
    lines,
    mod,
    words,
    movies,
    chapter_of,
    chapters,
    glyphs,
    size,
    tracking,
    space,
    cap_top,
    digit_h,
    path: Path,
):
    """The list's first page with the cursor on its fifth scene, as the port lays it out."""

    def px(name):
        u, v = atlas.at[(name,)]
        h, w = atlas.pixels[(name,)].shape[:2]
        return pixels[v : v + h, u : u + w]

    def put(name, x, y, w=None, h=None, tint=None):
        x0, y0, pw, ph = place.get(name, (0, 0, 0, 0))
        p = px(name)
        if tint is not None:
            p = p.copy()
            p[..., :3] = tint
        ss.paste(canvas, p, x, y, x + (w if w is not None else pw), y + (h if h is not None else ph))

    def text(t, x, y, right=False):
        width = ink_width(glyphs, t, size, tracking, space) + 4
        x = x - width if right else x
        for c in t:
            if c == " " or c not in glyphs:
                x += space * size + tracking
                continue
            glyph_px, _, _, w = glyphs[c]
            im = Image.fromarray(glyph_px, "RGBA").resize(
                (max(1, int(round(glyph_px.shape[1] * size))), max(1, int(round(32 * size)))), Image.LANCZOS
            )
            canvas.alpha_composite(im, (int(round(x)), int(round(y - cap_top * size))))
            x += w * size + tracking

    rows = next(ln for ln in lines if ln.startswith("rows ")).split()
    row_top, pitch = float(rows[1]), float(rows[2])
    cursor = 4
    art = chapters[chapter_of[cursor]]["art"]
    ground = Image.open(mod / "art" / (Path(art).stem + ".png")).convert("RGBA").resize(tt.PICTURE)
    canvas = ground
    put("veil", *place["veil"][:2])
    put("heading", *place["heading"][:2])
    put("help_bar", *place["help_bar"][:2])
    days = [chapters[c]["day"] for c in chapter_of]
    for i in range(SHOWN):
        top = row_top + i * pitch
        put("row_selected" if i == cursor else "row", place["row"][0], top + place["row"][1])
        if i == 0 or days[i] != days[i - 1]:
            put("square", place["square"][0], top + place["square"][1])
            if days[i] > 0:
                put("day", place["day"][0], top + place["day"][1])
                dx, dy, dh, dp = MEASURED["digits"]
                k = dh / digit_h
                for n, d in enumerate(reversed(str(days[i]))):
                    p = px(f"digit{d}")
                    w = p.shape[1] * k
                    put(f"digit{d}", dx - n * dp - w / 2, top + dy, w, dh, MEASURED["digit_colour"])
        if i == cursor:
            put("glove", place["glove"][0], top + place["glove"][1])
        nx, ny, _ = MEASURED["name"]
        text(words[1 + i], nx, top + ny)
        text(
            words[29 + i],
            float(next(ln for ln in lines if ln.startswith("text length")).split()[2]),
            top + ny,
            right=True,
        )
    put("trough", *place["trough"][:2])
    put("thumb", *place["thumb"][:2])
    put("end", *place["end"][:2])
    hx, hy, _ = MEASURED["help"]
    text(words[0][1:].strip(), hx + 60, hy)
    canvas.convert("RGB").save(path)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("mare", type=Path, help="the theater extracted: <collection>/Image/Mare.hed_out/original")
    ap.add_argument("--mod", default="Theater title")
    ap.add_argument("--lang", nargs="*", default=list(tt.LANGUAGES), choices=list(tt.LANGUAGES))
    ap.add_argument("--data", type=Path, help="the port's extracted game data (data/extracted/<hash>)")
    ap.add_argument("--preview", type=Path, help="also draw each language's first page into DIR/<lang>.png")
    args = ap.parse_args()
    data = args.data
    if data is None:
        found = sorted((tt.ROOT / "data" / "extracted").glob("*/nitrofs"))
        if not found:
            ap.error("no extracted game data in data/extracted: give --data")
        data = found[0].parent
    for lang in tt.skip_missing_languages(data, args.lang):
        make(args.mare, data, lang, tt.ROOT / "mods" / args.mod / "layouts" / "cine", args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
