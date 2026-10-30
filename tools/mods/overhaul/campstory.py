"""The story camp menu in the Menu Overhaul's look (host/screens/overhaul_camp_screen.c, "The story's menu"): Kingdom
Hearts II's command buttons, Birth by Sleep's plates made red, the panels' grid; mods/<mod>/layouts/campstory/:

  campstory.txt         the places (1920 x 1080) and what the screen reads
  campstory_atlas.png   the one-screen picture's pieces: the Files pages' pattern laid out once (a sheet a tile
                        wider and taller than the picture, its texels shown drifting), Birth by Sleep's two camp
                        plates made red at their places -- Roxas's (chara_plate, its four coloured lines) and his
                        panels' (deck_name_plate, squeezed thinner) -- and the black block behind them
  kh2_camp.seqd         KH2's camp.2ld (menu/us), its LAYD's second sequence: the command buttons, the glove, the
                        firefly
  kh2_camp.png          its sheet, the collection's (remastered/menu/us/camp.2ld: the keyboard's; the four are
                        alike but for the settings key's picture)
  <lang>/panels.png     the Panels page's panels (UI/pnl/3d_<lang>.pak), and panels.txt their records (db/db.p2)
  status_plate.png      the Panels page's block of Roxas's numbers: the old camp menu's status panel, cut out of
                        the HD menus' camp backdrop (layouts/camp/en/camp_backdrop.png)

From the player's own copies: Birth by Sleep's US_camp (bbs_first), KH2's menu/us/camp.2ld (kh2_fifth), the
game's UI/pnl/3d_<lang>.pak.z and db/db.p2, the HD PACK's camp backdrop and the Files pages' pattern, which the
files piece makes before this one (layouts/files/files_pattern.png).
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import campstory_bbs as bbs  # noqa: E402
import campstory_panels as panels  # noqa: E402
import kh2_sq  # noqa: E402
from common import LANGS, Inputs  # noqa: E402

# The plates' boxes (the layout's units), their edges' scale (BBS's texels at unit 2): the plate's frame keeps
# PLATE_MARGIN texels of each edge at EDGE_K and stretches its middle; chara_plate's lines are drawn at LABEL_K.
CHARA = (668, 210, 688, 630)
DECK = (1386, 210, 340, 630)
PLATE_MARGIN = 40
EDGE_K, LABEL_K = 1.7, 1.9
# The model's window: the DS's view of it (story_model_view, 161.3 dots square) at 1.2 times the old window's
# scale (532); his frame on the DS (story_model_source, 117-251 across) then 837-1367, his face (the frame's
# middle) right of his numbers.
MODEL = (771.5, 118.3, 638.4, 638.4)
# The plates' frames: their SP2 groups in camp.l2d's layouts 3 (chara_plate) and 4 (deck_name_plate).
FRAME_KEYS = {525, 526, 527, 528, *range(670, 679), *range(679, 688)}
# chara_plate's four lines (BBS's LV, HP, FP, NEXT LEVEL; Days's LV, HP, EXP, Next Level): their colours.
LINE_COLOURS = {807: (255, 230, 60), 808: (60, 235, 70), 809: (255, 150, 50), 810: (235, 235, 235)}
PATTERN_SCALE, PATTERN_DRIFT = 0.55, (16.0, 16.0)  # the Files pages' (layouts/files/files.txt)
# A block of black behind the two plates, the pattern dimmed there (its corners rounded, its edge soft).
BACKING = (652, 198, 1090, 654)
BACKING_ALPHA, BACKING_RADIUS, BACKING_SOFT = 0.5, 18.0, 2.5
PANEL_CELL = 48  # the grid's cell on the panels' plate (layout units): a DS dot 3 units, as the textures

# The Panels page: the menu's old status panel (camp.txt's `panel` of camp_backdrop.png) in the block left of
# the DS's lower screen, cut out of the backdrop (its ground there blue-black or black, the panel's greys neutral:
# its bevelled corners left clear), each row whole between its first and last dot of the panel.
STATUS_PANEL = (1240, 190, 640, 650)
PANELS_STATUS_AT = (272, 190)
PANELS_LOWER = (1032, 275, 768, 480, 160)  # the lower screen's card: its rows 0-159 at 3 units a dot
PANELS_LIST = (184, 3.3)  # the upper screen's abilities' list and its help: dots 0-183
PANELS_SLOT = (136, 495, 212)  # the card not in the block: its middle in the left room, its width
PANELS_SWAP = (0.42, 60)


def backing_picture() -> Image.Image:
    """The block behind the plates: black at BACKING_ALPHA, its corners rounded, its edge fading in over
    BACKING_SOFT texels."""
    w, h = BACKING[2], BACKING[3]
    y, x = np.mgrid[0:h, 0:w].astype(np.float32) + 0.5
    r = BACKING_RADIUS
    dx = np.maximum(np.maximum(r - x, x - (w - r)), 0)
    dy = np.maximum(np.maximum(r - y, y - (h - r)), 0)
    inside = r - np.sqrt(dx * dx + dy * dy)  # the distance in from the rounded edge
    edge = np.clip(np.minimum(np.minimum(x, w - x), np.minimum(y, h - y)), 0, None)
    d = np.minimum(inside, edge)
    a = np.clip(d / BACKING_SOFT, 0, 1) * BACKING_ALPHA
    px = np.zeros((h, w, 4), np.uint8)
    px[..., 3] = (a * 255 + 0.5).astype(np.uint8)
    return Image.fromarray(px, "RGBA")


def plate_picture(camp: bbs.Camp, layout_id: int, box, lines: bool):
    """A plate's frame stretched to `box` (its edges kept at EDGE_K) and made red, with chara_plate's lines at
    LABEL_K from its top left (`lines`; BBS's own words left out: the screen writes the game's): an RGBA picture
    of the box, and the frame's top left in the layout's texels."""
    ps = [p for p in camp.lay.pieces(layout_id, 60.0, {}, {}) if p["region"][0] == "texture"]
    frame = [p for p in ps if p["key"][-1] in FRAME_KEYS]
    L = min(p["rect"][0] for p in frame)
    T = min(p["rect"][1] for p in frame)
    R = max(p["rect"][2] for p in frame)
    B = max(p["rect"][3] for p in frame)
    x, y, w, h = 0, 0, box[2], box[3]
    m = PLATE_MARGIN

    def map1(v, lo, hi, dlo, dhi):
        if v - lo <= m:
            return dlo + (v - lo) * EDGE_K
        if hi - v <= m:
            return dhi - (hi - v) * EDGE_K
        span, dspan = hi - lo - 2 * m, dhi - dlo - 2 * m * EDGE_K
        return dlo + m * EDGE_K + (v - lo - m) * dspan / span

    pieces = []
    for p in frame:
        x0, y0, x1, y1 = p["rect"]
        rect = (map1(x0, L, R, x, x + w), map1(y0, T, B, y, y + h), map1(x1, L, R, x, x + w), map1(y1, T, B, y, y + h))
        corners = [bbs.red(tuple(np.array(c) * np.array(p["colour"]))) for c in p["corners"]]
        _, _, ua, va, ub, vb = p["region"]
        pieces.append(((ua, va, ub, vb), rect, corners, p["flip"], p["blend"], p["key"]))
    for p in ps:
        if not lines or p["key"][-1] not in LINE_COLOURS:
            continue
        x0, y0, x1, y1 = p["rect"]
        rect = (x + (x0 - L) * LABEL_K, y + (y0 - T) * LABEL_K, x + (x1 - L) * LABEL_K, y + (y1 - T) * LABEL_K)
        colour = tuple(c / 255.0 for c in LINE_COLOURS[p["key"][-1]]) + (1.0,)
        corners = [tuple(np.array(c) * np.array(colour)) for c in p["corners"]]
        _, _, ua, va, ub, vb = p["region"]
        pieces.append(((ua, va, ub, vb), rect, corners, p["flip"], p["blend"], p["key"]))
    canvas = Image.new("RGBA", (box[2], box[3]), (0, 0, 0, 0))
    return camp.draw(canvas, pieces), (L, T)


def pattern_sheet(files_pattern: Path):
    """The Files pages' pattern laid out once over the picture and a tile more each way, at their scale; and the
    tile's size there."""
    tile = Image.open(files_pattern).convert("RGBA")
    tw, th = tile.width * PATTERN_SCALE, tile.height * PATTERN_SCALE
    nx, ny = int(np.ceil((1920 + tw + 2) / tw)), int(np.ceil((1080 + th + 2) / th))
    big = Image.new("RGBA", (tile.width * nx, tile.height * ny))
    for j in range(ny):
        for i in range(nx):
            big.paste(tile, (i * tile.width, j * tile.height))
    sheet = big.resize((round(big.width * PATTERN_SCALE), round(big.height * PATTERN_SCALE)), Image.LANCZOS)
    return sheet, (tw, th)


def status_plate(backdrop: Path) -> Image.Image:
    """The old camp menu's status panel cut out of its backdrop (STATUS_PANEL): the panel's texels those whose
    blue and red are within 2 of each other and not black, each row whole between its first and last; the rest
    clear."""
    x, y, w, h = STATUS_PANEL
    px = np.asarray(Image.open(backdrop).convert("RGBA"))[y : y + h, x : x + w].copy()
    rgb = px[..., :3].astype(int)
    inside = (np.abs(rgb[..., 2] - rgb[..., 0]) <= 2) & (rgb.max(-1) >= 10)
    for r in range(h):
        xs = np.nonzero(inside[r])[0]
        if len(xs):
            inside[r, xs[0] : xs[-1] + 1] = True
    px[..., 3] = np.where(inside, 255, 0).astype(np.uint8)
    return Image.fromarray(px, "RGBA")


def seqd(two_ld: Path, name: str, index: int) -> bytes:
    """A 2LD's LAYD `name`'s sequence `index` (a SEQD), its offsets its own."""
    L = kh2_sq.Layout(str(two_ld), []).layd[name]
    n, o = struct.unpack_from("<2I", L, 0x18)
    starts = list(struct.unpack_from("<%dI" % n, L, o))
    end = starts[index + 1] if index + 1 < n else len(L)
    out = L[starts[index] : end]
    assert out[:4] == b"SEQD"
    return out


def atlas_picture(sheet: Image.Image, plates: dict) -> tuple[Image.Image, dict]:
    """The atlas: the pattern's sheet at the top, the two plates and the block behind them (`plates`: chara_plate,
    deck_plate, backing) side by side under it, two texels apart; and their pieces' rectangles in it."""
    chara, deck, backing = plates["chara_plate"], plates["deck_plate"], plates["backing"]
    cy = sheet.height + 2
    at = {"chara_plate": (0, cy), "deck_plate": (chara.width + 2, cy), "backing": (chara.width + deck.width + 4, cy)}
    aw = max(sheet.width, at["backing"][0] + backing.width)
    ah = cy + max(chara.height, deck.height, backing.height)
    atlas = Image.new("RGBA", (aw, ah), (0, 0, 0, 0))
    atlas.paste(sheet, (0, 0))
    for name, picture in plates.items():
        atlas.paste(picture, at[name])
    return atlas, {name: (*at[name], picture.width, picture.height) for name, picture in plates.items()}


def layout_text(sheet: Image.Image, tile, pieces: dict, chara_top_left, panel_count: int) -> str:
    """campstory.txt: the atlas's pieces, their places and what the screen reads."""
    tw, th = tile
    L, T = chara_top_left
    drift = f"{PATTERN_DRIFT[0]:.1f} {PATTERN_DRIFT[1]:.1f}"

    def piece(name):
        return f"piece {name} " + " ".join(str(v) for v in pieces[name])

    def row_y(line_top):
        """chara_plate's line's top (its texels): where its row's words sit over it."""
        return CHARA[1] + (line_top - T) * LABEL_K

    left, right = CHARA[0] + (790 - L) * LABEL_K, CHARA[0] + (926 - L) * LABEL_K
    lines = [
        "# The story's camp menu: host/screens/overhaul_camp_screen.c reads it (\"The story's menu\"); made by",
        "# tools/mods/overhaul/campstory.py. The layout's units are the picture's (1920 x 1080).",
        "#   pattern_sheet U V W H TW TH DX DY  the pattern laid out once in the atlas, its tile's size there,",
        "#                                      its drift (units a second, down and to the right)",
        "#   piece chara_plate, deck_plate      the plates (the one-screen picture's, under the model), and",
        "#   piece backing                      the black behind both (drawn first, over the pattern)",
        "#   place chara_plate, deck_plate      where they go; place model: the model's window",
        "#   kh2 SEQD SHEET UNITS               KH2's camp.2ld sequence 1, its sheet, the sheet's PS2 size",
        "#   list X Y PITCH SCALE WIDEN FROM    the first button's place (KH2's animations put it at 34, 79 of",
        "#                                      its dots: X, Y there), the rows' pitch (its dots), its dots'",
        "#                                      size, the buttons longer by WIDEN dots (their parts right of",
        "#                                      FROM moved)",
        "#   option_text DX DY CAP              the options' words: from the button's place (its dots), their",
        "#                                      capitals' middle, their height (the camp's font)",
        "#   stat N RECORD R G B X Y RIGHT VY CAP  a row of Roxas's: status_&.s's label RECORD at (X, Y: its",
        "#                                      capitals' top), its number's right at RIGHT (its top at VY, a",
        "#                                      tenth taller), in a colour",
        "#   title X Y CAP, deck_title X Y CAP  Roxas's name, the panels' word (the story's option 0)",
        "#   day RIGHT Y CAP R G B              the day (info.s's DAY %d) in the top band, in the line's orange",
        "#   pages RIGHT Y H                    the panels' pages: Y's icon and the page",
        "#   panel_icons FILE CELL COLUMNS N    <lang>/FILE: the Panels page's panels (UI/pnl/3d_<lang>.pak, the",
        "#                                      game's order), CELL texels each (4 a dot), COLUMNS to a row;",
        "#                                      place panels: the grid of a page (5 x 8 cells) on its plate;",
        "#                                      panels.txt: which panel each item is, the links' shapes;",
        "#   panels_float RISE PERIOD           the grid floating as the DS's screens on the Files pages",
        "#   panels_page_status FILE X Y        the Panels page: the menu's old status panel cut out (camp.txt's",
        "#                                      panel of its camp_backdrop.png), its top left in the page's block",
        "#   panels_page_lower X Y W H ROWS     the DS's lower screen as a card: its rows 0 to ROWS-1 (its grid, its",
        "#                                      list; the description's box left out: the band shows its line)",
        "#   panels_page_list DOTS K            what X shows (the upper screen's abilities' list and its help): its",
        "#                                      dots 0 to DOTS-1 across, K units a dot, in the block when shown",
        "#   panels_page_slot X Y W             the card not in the block: its middle in the left room, its width",
        "#   panels_page_swap SECONDS ARC       the two swapping places on X: how long, how far their paths bow",
        "picture 1920 1080",
        "atlas campstory_atlas.png",
        f"pattern_sheet 0 0 {sheet.width} {sheet.height} {tw:.4f} {th:.4f} {drift}",
        piece("chara_plate"),
        piece("deck_plate"),
        piece("backing"),
        f"place backing {BACKING[0]} {BACKING[1]} {BACKING[2]} {BACKING[3]}",
        f"place chara_plate {CHARA[0]} {CHARA[1]} {CHARA[2]} {CHARA[3]}",
        f"place deck_plate {DECK[0]} {DECK[1]} {DECK[2]} {DECK[3]}",
        f"place model {MODEL[0]} {MODEL[1]} {MODEL[2]} {MODEL[3]}",
        f"panel_icons panels.png {16 * panels.PANEL_K} {panels.PANEL_COLUMNS} {panel_count}",
        f"place panels {DECK[0] + (DECK[2] - 5 * PANEL_CELL) // 2} {DECK[1] + 142} {5 * PANEL_CELL} {8 * PANEL_CELL}",
        "panels_float 4.0 4.8",
        "kh2 kh2_camp.seqd kh2_camp.png 256",
        "list 185 267.5 26 2.9 30 80",
        "option_text 1 8 28",
        f"stat 0 0 255 230 60 {left:.1f} {row_y(416) - 28:.1f} {right:.1f} {row_y(416) - 30:.1f} 22",
        f"stat 1 1 60 235 70 {left:.1f} {row_y(438) - 28:.1f} {right:.1f} {row_y(438) - 30:.1f} 22",
        f"stat 2 9 255 150 50 {left:.1f} {row_y(460) - 28:.1f} {right:.1f} {row_y(460) - 30:.1f} 22",
        f"stat 3 10 235 235 235 {left:.1f} {row_y(472):.1f} {right:.1f} {row_y(498) - 30:.1f} 22",
        f"title {CHARA[0] + 22 * EDGE_K:.1f} {CHARA[1] + 13 * EDGE_K - 13:.1f} 30",
        f"deck_title {DECK[0] + 22 * EDGE_K:.1f} {DECK[1] + 13 * EDGE_K - 13:.1f} 30",
        "day 1708 127 30 255 143 0",
        f"pages {DECK[0] + DECK[2] - 24} 216 40",
        f"panels_page_status status_plate.png {PANELS_STATUS_AT[0]} {PANELS_STATUS_AT[1]}",
        "panels_page_lower %d %d %d %d %d" % PANELS_LOWER,
        "panels_page_list %d %.2f" % PANELS_LIST,
        "panels_page_slot %d %d %d" % PANELS_SLOT,
        "panels_page_swap %.2f %d" % PANELS_SWAP,
    ]
    return "\n".join(lines) + "\n"


def make(inputs: Inputs, mod: Path) -> None:
    """Writes the story camp menu's files into mod/layouts/campstory/ (the files piece's pattern made before)."""
    out = mod / "layouts" / "campstory"
    out.mkdir(parents=True, exist_ok=True)

    camp = bbs.Camp(inputs.bbs)
    chara, chara_top_left = plate_picture(camp, 3, CHARA, True)
    deck, _ = plate_picture(camp, 4, DECK, False)
    sheet, tile = pattern_sheet(mod / "layouts" / "files" / "files_pattern.png")
    plates = {"chara_plate": chara, "deck_plate": deck, "backing": backing_picture()}
    atlas, pieces = atlas_picture(sheet, plates)
    atlas.save(out / "campstory_atlas.png", optimize=True)

    panel_count = 0
    for lang in LANGS:
        icons, panel_count = panels.panel_sheet(inputs.ds(f"UI/pnl/3d_{lang}.pak.z"))
        (out / lang).mkdir(exist_ok=True)
        icons.save(out / lang / "panels.png", optimize=True)
    (out / "panels.txt").write_text(panels.panel_table(inputs.nitrofs / "db" / "db.p2"), "utf-8", newline="\r\n")

    status_plate(inputs.hd / "layouts" / "camp" / "en" / "camp_backdrop.png").save(
        out / "status_plate.png", optimize=True
    )
    (out / "kh2_camp.seqd").write_bytes(seqd(inputs.kh2 / "original" / "menu" / "us" / "camp.2ld", "camp", 1))
    with Image.open(bbs.hd_asset(inputs.kh2 / "remastered" / "menu" / "us" / "camp.2ld", "US_camp_2ld0")) as im:
        im.save(out / "kh2_camp.png")

    text = layout_text(sheet, tile, pieces, chara_top_left, panel_count)
    (out / "campstory.txt").write_text(text, "utf-8", newline="\r\n")
