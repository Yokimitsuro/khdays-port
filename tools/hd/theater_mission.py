#!/usr/bin/env python3
"""Mission Mode's screens on one screen -- the rooms, a room, the character select -- in the 358/2 Days
theater's pieces, from your own copy (MARE: docs/HD_SCREENS.md, "Making them"), into mods/NAME/layouts/.
  python tools/hd/theater_mission.py MARE [--mod NAME] [--lang es|en|fr|de|it ...] [--data DIR]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import save_screen as ss  # noqa: E402
import theater_cine as tc  # noqa: E402
import theater_movies as tm  # noqa: E402
import theater_title as tt  # noqa: E402

VEIL = 700
# The game's characters in its order (UI/mlt/mlt_<lang>.s, records 0x0b-0x1d) and each one's face
# in the theater's bg/face: days_npc_01-14 are the Organization's fourteen in their numbers' order,
# 15 Sora, 16 Riku, 20 the king (17-19 Kairi, DiZ and Naminé, not in the game's list); Donald and
# Goofy have faces only among the theater's others (hd_npc_02 and 03).
FACES = [
    "days_npc_01",
    "days_npc_02",
    "days_npc_03",
    "days_npc_04",
    "days_npc_05",
    "days_npc_06",
    "days_npc_07",
    "days_npc_08",
    "days_npc_09",
    "days_npc_10",
    "days_npc_11",
    "days_npc_12",
    "days_npc_13",
    "days_npc_14",
    "days_npc_20",
    "days_npc_15",
    "days_npc_16",
    "hd_npc_02",
    "hd_npc_03",
]
UNKNOWN = "hd_npc_40"  # the theater's hooded figure in the Organization's coat
ART = "tz007a"  # the background: one of the theater's artworks (the port's choice)
GAUGE = (3, 14)  # res.p2's entry and cell of the full gauge
# A line of text is (left or its anchor, the top of the font's cells, the cells' height, its
# alignment, its grey or colour); the ones of a row, a tile, a card or a plate are from its top left;
# a top of None centres the line's capitals on the anchor (a circle's label).
SELECT_PLACES = {
    "rule": (80, 111, 1760, 2),  # the line under the heading
    "face": (80, 150, 420, 420),  # the character this copy is on
    "tile": (900, 150, 176, 176),  # the grid's first tile
    "tile_tag": (26, 26, 20, 20),  # a place's circle on a tile: its middle and radius
    "stat": (80, 606, 760, 44),  # the first gauge's row
    "gauge": (300, 10, 36, 24),  # a row's first bar
    "plate": (80, 960, 430, 84),  # the first place's plate
    "plate_circle": (42, 42, 32, 32),  # its circle: middle and radius
    "plate_face": (88, 9, 66, 66),  # its character's face
}
SELECT_LINES = {
    "grid": "5 12 4",  # columns, the tiles' gap, a page's rows (above the places' plates)
    "stats": "50",  # the gauges' rows' pitch
    "gauge_pitch": "44",  # the bars' pitch
    "plates": "450",  # the plates' pitch
    "frame": "6 2",  # a tile's frame: a place's, the others'
}
SELECT_TEXTS = {
    "heading": (80, 44, 54, "left", 255),
    "help": (1840, 49, 34, "right", 255),
    "name": (540, 160, 84, "left", 255),
    "weapon": (544, 258, 44, "left", 200),
    "stat": (24, 6, 34, "left", 255),
    "tag": (0, None, 25, "centre", 255),
    "plate_tag": (0, None, 40, "centre", 255),
    "plate_player": (170, 8, 36, "left", 255),
    "plate_character": (170, 44, 28, "left", 190),
    "plate_alone": (170, 24, 38, "left", 255),
    "page": (1364, 925, 34, "centre", 255),  # the pages' "2 / 3", under the grid's four rows
}
ROOMS_PIECES = ("veil", "help_bar", "row", "row_selected", "square", "trough", "thumb", "end", "glove")
ROOMS_PLACES = {
    "rule": (80, 111, 1760, 2),
    "divider": (272, 322, 1268, 2),  # between Create a Group and the search, and the rooms
    "glove": (150, -10, 122, 91),  # the selected row's glove: before its square
    "signal": (286, 16, 7, 34),  # a room's signal in its square: the first bar, the tallest
    "trough": (1574, 336, 48, 560),  # the rooms' scroll bar
    "thumb": (1574, 336, 48, 130),
    "end": (1574, 840, 48, 56),
    "card": (80, 200, 420, 620),  # a room's first place
    "card_face": (60, 90, 300, 300),
    "card_tag": (56, 56, 34, 34),
    "card_signal": (140, 550, 7, 34),
    "proceed": (1240, 870, 600, 68),  # the host's Proceed
    "proceed_glove": (1150, 858, 122, 91),
}
ROOMS_LINES = {
    "fixed": "150 82",  # Create a Group and the search: the first's top, the pitch
    "rows": "336 82 7",  # the rooms: the first's top, the pitch, how many show
    "form": "230 82",  # a new room's or a password's rows
    "cards": "446",  # the places' cards' pitch
    "frame": "6 2",  # a card's ring: a player there, nobody
    "signal": "11 10 18 26 34",  # the bars' pitch and heights
    "signal_ms": "60 120 200",  # four bars under the first, three under the second, ...
}
ROOMS_TEXTS = {
    "heading": (80, 44, 54, "left", 255),
    "message": (80, 128, 34, "left", 210),
    "notice": (1840, 49, 34, "right", (240, 200, 90)),  # the list's: a password refused
    "help": (315, 1001, 34, "left", 255),
    "name": (380, 6, 38, "left", 255),  # a room's name; a field's text
    "host": (380, 40, 24, "left", 190),  # its host; a field's label
    "single": (380, 16, 38, "left", 255),  # a row of one line
    "placeholder": (380, 16, 38, "left", 170),
    "locked": (20, 14, 26, "left", (240, 200, 90)),  # after the name
    "players": (1350, 16, 34, "right", 255),
    "ping": (1530, 16, 34, "right", 255),
    "card_tag": (0, None, 42, "centre", 255),
    "card_name": (210, 410, 64, "centre", 255),
    "card_host": (210, 490, 36, "centre", (240, 200, 90)),
    "card_you": (210, 490, 36, "centre", 200),
    "card_empty": (210, 430, 64, "centre", 130),
    "card_ping": (190, 548, 36, "left", 255),
    "proceed": (1340, 886, 40, "left", 255),
}


def face(mare: Path, name: str) -> np.ndarray:
    return tt.tim2_picture((mare / "bg" / "face" / f"{name}.tm2").read_bytes(), 0)


def grey(px: np.ndarray) -> np.ndarray:
    """The face in grey (its own light: the luma), its alpha kept."""
    out = px.copy()
    luma = px[..., 0].astype(np.float32) * 0.299 + px[..., 1] * 0.587 + px[..., 2] * 0.114
    out[..., 0] = out[..., 1] = out[..., 2] = np.clip(luma, 0, 255).astype(np.uint8)
    return out


def gauge_colours(data: Path):
    """The full gauge's bars' colours, left to right (each bar's middle column)."""
    p = data / "nitrofs" / "UI" / "mlt" / "res.p2"
    pack = ss.d2kp(ss.p2_entry(p, GAUGE[0]))
    colours, per = ss.palette(pack[b"RLCN"][0])
    chars, bpp = ss.tiles(pack[b"RGCN"][0])
    px = ss.cell(pack[b"RECN"][0], GAUGE[1], chars, bpp, colours, per)
    row = px[px.shape[0] // 2]
    coloured = [
        (x, tuple(int(v) for v in row[x][:3]))
        for x in range(row.shape[0])
        if row[x][3] and max(row[x][:3]) - min(row[x][:3]) > 60
    ]
    bars, run = [], []
    for x, rgb in coloured:
        if run and (x != run[-1][0] + 1 or rgb != run[-1][1]):
            bars.append(run)
            run = []
        run.append((x, rgb))
    if run:
        bars.append(run)
    return [b[len(b) // 2][1] for b in bars]


def small_veil(layouts):
    """The theater's veil, a smooth gradient: kept small, the port stretches it."""
    veil, rect = tc.picture(layouts, tc.parts(layouts, {VEIL: tt.STILL}))
    return np.asarray(
        Image.fromarray(veil, "RGBA").resize((max(2, rect[2] // 8), max(2, rect[3] // 8)), Image.BILINEAR)
    )


class Screen:
    """One screen's file: its pieces in an atlas, its places, lines and texts, the font, the icons."""

    def __init__(self, folder: Path):
        self.folder = folder
        self.atlas = tt.Atlas()
        self.pieces = []
        self.icons = {**tt.icon_pixels(folder / "font"), **tc.key_icons(folder)}
        for name, px in self.icons.items():
            self.atlas.add(("icon", name), px)

    def piece(self, name, px):
        self.atlas.add((name,), px)
        self.pieces.append(name)

    def write(self, out_dir: Path, stem: str, header, places, lines_, texts, extra=()):
        glyphs, cap_top, cap_h = tc.list_font(self.folder)
        pixels = self.atlas.pack()
        lines = list(header) + [
            f"picture {tt.PICTURE[0]} {tt.PICTURE[1]}",
            f"atlas {stem}_atlas.png",
            "art ../../select/select_art.png",
        ]
        for name in self.pieces:
            u, v = self.atlas.at[(name,)]
            h, w = self.atlas.pixels[(name,)].shape[:2]
            lines.append(f"piece {name} {u} {v} {w} {h}")
        for name, (x, y, w, h) in places.items():
            lines.append(f"place {name} {x} {y} {w} {h}")
        for name, value in lines_.items():
            lines.append(f"{name} {value}")
        lines.extend(extra)
        for name, (x, top, height, align, shade) in texts.items():
            # the port's text styles: Y the capitals' top, or their middle for a centred line
            size = height / 32.0
            colour = shade if isinstance(shade, int) else f"255 {shade[0]} {shade[1]} {shade[2]}"
            y = 0.0 if top is None else top + (cap_top + (cap_h / 2 if align == "centre" else 0)) * size
            lines.append(f"text {name} {x} {y:.1f} {size:.4f} {align} {colour}")
        for name, px in self.icons.items():
            u, v = self.atlas.at[("icon", name)]
            lines.append(f"icon {name} {u} {v} {px.shape[1]} {px.shape[0]}")
        # the font: its glyphs in cells, each advancing its width; a space as the theater's subtitles'
        cols_n = 32
        cell_w = max(gl[0].shape[1] for gl in glyphs.values())
        codes = sorted(glyphs, key=ord)
        font_px = np.zeros((((len(codes) + cols_n - 1) // cols_n) * 32, cols_n * cell_w, 4), np.uint8)
        lines.append(f"font {stem}_font.png 32 {cap_top} {cap_top + cap_h} rgba")
        if " " not in glyphs:
            lines.append(f"glyph 32 0 0 0 0 {tm.SPACE:.2f}")
        for n, c in enumerate(codes):
            glyph_px, _, _, w = glyphs[c]
            gx, gy = (n % cols_n) * cell_w, (n // cols_n) * 32
            font_px[gy : gy + 32, gx : gx + glyph_px.shape[1]] = glyph_px
            lines.append(f"glyph {ord(c)} {gx} {gy} {glyph_px.shape[1]} 0 {tm.SPACE if c == ' ' else w:.2f}")
        out_dir.mkdir(parents=True, exist_ok=True)
        Image.fromarray(pixels, "RGBA").save(out_dir / f"{stem}_atlas.png")
        Image.fromarray(font_px, "RGBA").save(out_dir / f"{stem}_font.png")
        (out_dir / f"{stem}.txt").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
        return (
            f"{len(self.pieces)} pieces, {len(self.icons)} icons, {len(codes)} glyphs, atlas "
            f"{pixels.shape[1]} x {pixels.shape[0]}"
        )


COMMON_HEADER = [
    "#   piece NAME U V W H         a picture in the atlas",
    "#   place NAME X Y W H         where something goes (1920 x 1080), or from its row's, tile's,",
    "#                              card's or plate's top left; a circle's as its middle and radius",
    "#   art FILE                   the background (from this file's folder)",
    "#   text NAME X Y SIZE ALIGN GREY [R G B]   a line (Y its capitals' top), its size (the font's",
    "#                              scale), its grey or colour",
    "#   icon NAME U V W H          an input's icon",
    "#   font FILE CELL TOP BOTTOM rgba   the font (white outlined in black)",
    "#   glyph CODE X Y W LEFT ADVANCE   its glyphs",
]


def make_select(mare: Path, data: Path, lang: str, mod: Path, layouts) -> str:
    sc = Screen(mare / tt.LANGUAGES[lang][0])
    for n, name in enumerate(FACES):
        px = face(mare, name)
        sc.piece(f"face{n}", px)
        sc.piece(f"face{n}_grey", grey(px))
    sc.piece("veil", small_veil(layouts))
    bars = gauge_colours(data)
    if len(bars) != 10:
        raise ValueError(f"the full gauge has {len(bars)} bars, not ten")
    header = [
        "# Mission Mode's character select on one screen, in the 358/2 Days theater's pieces, made by",
        "# tools/hd/theater_mission.py from the player's own copy (host/screens/select_screen.c reads it).",
        *COMMON_HEADER,
        "#   pieces: face<N> (the game's character N), face<N>_grey, veil; places: rule, face, tile",
        "#   (the grid's first), tile_tag, stat (the first gauge's row), gauge, plate, plate_circle,",
        "#   plate_face",
        "#   grid COLUMNS GAP ROWS      the grid, ROWS a page's (more characters: pages, L and R)",
        "#   stats PITCH                the gauges' rows; gauge_pitch PITCH their bars",
        "#   plates PITCH               the places' plates",
        "#   frame PLACE OTHERS         a tile's frame: a place's colour on it, or none",
        "#   gauge_colour R G B         the gauge's bars, left to right",
    ]
    return (
        sc.write(
            mod / "select" / lang,
            "select",
            header,
            SELECT_PLACES,
            SELECT_LINES,
            SELECT_TEXTS,
            [f"gauge_colour {r} {g} {b}" for r, g, b in bars],
        )
        + f"; gauge {bars}"
    )


def make_rooms(mare: Path, lang: str, mod: Path, layouts) -> str:
    folder = mare / tt.LANGUAGES[lang][0]
    sc = Screen(folder)
    launcher = tt.Layouts(folder / "l2d" / "launcher.l2d")
    found = {name: (px, place) for name, px, place in tc.list_pieces(layouts, launcher, lang)[0]}
    places = {}
    for name in ROOMS_PIECES:
        px, place = found[name]
        sc.piece(name, px)
        if name in ("help_bar", "row", "row_selected", "square"):
            places[name] = tuple(round(v, 1) for v in place)
    # the host's Proceed: a part of a row, selected and not
    sc.piece("button_lit", np.ascontiguousarray(found["row_selected"][0][:, 300:900]))
    sc.piece("button", np.ascontiguousarray(found["row"][0][:, 300:900]))
    sc.piece("unknown", face(mare, UNKNOWN))
    places.update(ROOMS_PLACES)
    header = [
        "# Mission Mode's rooms on one screen, in the 358/2 Days theater's pieces, made by",
        "# tools/hd/theater_mission.py from the player's own copy (host/screens/room_screen.c reads it).",
        *COMMON_HEADER,
        "#   pieces: veil, help_bar, row, row_selected, square, trough, thumb, end, glove (the",
        "#   theater's list's), button and button_lit (a row's part), unknown (a player's face)",
        "#   fixed TOP PITCH            Create a Group and the search",
        "#   rows TOP PITCH SHOWN       the rooms",
        "#   form TOP PITCH             a new room's fields, a password's",
        "#   cards PITCH                a room's places",
        "#   frame THERE NOBODY         a card's ring",
        "#   signal PITCH H1 H2 H3 H4   a room's signal bars; signal_ms A B C the pings under",
        "#                              which four, three and two are lit (one above)",
    ]
    return sc.write(mod / "rooms" / lang, "rooms", header, places, ROOMS_LINES, ROOMS_TEXTS)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("mare", type=Path, help="the theater extracted: <collection>/Image/Mare.hed_out/original")
    ap.add_argument("--mod", default="Theater title")
    ap.add_argument("--lang", nargs="*", default=list(tt.LANGUAGES), choices=list(tt.LANGUAGES))
    ap.add_argument("--data", type=Path, help="the port's extracted game data (data/extracted/<hash>)")
    args = ap.parse_args()
    data = args.data
    if data is None:
        found = sorted((tt.ROOT / "data" / "extracted").glob("*/nitrofs"))
        if not found:
            ap.error("no extracted game data in data/extracted: give --data")
        data = found[0].parent
    mod = tt.ROOT / "mods" / args.mod / "layouts"
    art = mod / "select" / "select_art.png"
    if not art.exists():
        art.parent.mkdir(parents=True, exist_ok=True)
        px = tt.tim2_picture((args.mare / "bg" / "artwork" / f"{ART}.tm2").read_bytes(), 0)
        Image.fromarray(px).convert("RGB").save(art)
    for lang in args.lang:
        layouts = tt.Layouts(args.mare / tt.LANGUAGES[lang][0] / "l2d" / "days_theatre.l2d")
        print(f"{lang}: select {make_select(args.mare, data, lang, mod, layouts)}")
        print(f"{lang}: rooms {make_rooms(args.mare, lang, mod, layouts)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
