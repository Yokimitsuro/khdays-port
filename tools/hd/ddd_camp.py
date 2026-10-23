#!/usr/bin/env python3
"""Mission Mode's camp menu on one screen, in Dream Drop Distance HD's camp pieces in Days's tones, from
your own copies (DDD, MARE, SETTINGS: docs/HD_SCREENS.md, "Making them"), into mods/NAME/layouts/camp/.
  python tools/hd/ddd_camp.py DDD MARE SETTINGS [--mod NAME] [--lang es|en|fr|de|it ...] [--data DIR]
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import newgame_dialog as ng  # noqa: E402
import save_screen as ss  # noqa: E402
import theater_cine as tc  # noqa: E402
import theater_mission as tmi  # noqa: E402
import theater_settings as ts  # noqa: E402
import theater_title as tt  # noqa: E402

LAYOUTS = "original/menu/{lang}/camp/comn/comn/bin/camp00_n.l2d"
# The HD textures' language prefixes, and the textures of the file's seven blocks by their CTRT
# sizes and what their nodes draw: 0 the bars, bands, keys (1024 x 512), 1 the words (512 x 512:
# layout 1 node 11 cuts MENU from it, layout 14 nodes 401 and 421 the money's and DDD's drops'
# words), 2 and 6 the digits (256 x 256, not used here), 3 the window titles, 4 the panels and the
# frame's checks (1024 x 512), 5 the panels' gradient (16 x 16).
PREFIX = {"es": "SP", "en": "US", "fr": "FR", "de": "GR", "it": "IT"}
BLOCKS = ("camp00", "pt_font", "cnt_00", "camp_wtitle", "pt_status", "pt_status_gra", "cnt_01")
# Mission Mode's options, top to bottom, as the game's pictures write them (see above).
OPTIONS = {
    "es": ["Holomisiones", "Paneles", "Tienda", "Personaje", "Preferencias", "Guardar", "Principal"],
    "en": ["Holo-Missions", "Panels", "Shop", "Character", "Mission Config", "Save", "Title Screen"],
    "fr": ["Holo-missions", "Panneaux", "Magasin", "Personnage", "Configurer mission", "Sauvegarder", "Écran-titre"],
    "de": ["Holo-Missionen", "Tafeln", "Mogry-Laden", "Charakter", "Missionskonfig.", "Speichern", "Titelbildschirm"],
    "it": ["Olo-missioni", "Pannelli", "Negozio", "Personaggio", "Config. missione", "Salva", "Titolo"],
}
# The story's camp menu's options (ov025's), top to bottom, from the same pictures (cmo_&.p2 entry 1,
# cells 15, 14, 16, 25, 17, 18 and 19).
STORY_OPTIONS = {
    "es": ["Paneles", "Holomisiones", "Archivos", "Tutoriales", "Preferencias", "Guardar", "Principal"],
    "en": ["Panels", "Holo-Missions", "Files", "Tutorials", "Config", "Save", "Title Screen"],
    "fr": ["Panneaux", "Holo-missions", "Fichiers", "Didacticiels", "Configuration", "Sauvegarder", "Écran-titre"],
    "de": ["Tafeln", "Holo-Missionen", "Berichte", "Tutorien", "Konfig.", "Speichern", "Titelbildschirm"],
    "it": ["Pannelli", "Olo-missioni", "Rapporti", "Tutorial", "Configura", "Salva", "Titolo"],
}
# The two options of Holo-Missions and of Files (A on them: the game's list shows them in its place),
# from the same pictures (cells 26, 27, 29 and 30).
STORY_SUBS = {
    "es": ["Mis. normales", "Desafíos", "Diario de Roxas", "Perfs. enemigos"],
    "en": ["Normal Missions", "Challenges", "Roxas's Diary", "Enemy Profiles"],
    "fr": ["Missions normales", "Épreuves", "Journal de Roxas", "Profils ennemis"],
    "de": ["Normale Missionen", "Herausforderungen", "Roxas' Tagebuch", "Gegnerprofile"],
    "it": ["Missioni normali", "Sfide", "Diario di Roxas", "Profili nemici"],
}
# The game mode the DS's status panel shows (UI/cm/cmo_&.p2 entry 2: cell 20 its label, 21-23 the
# story's three difficulties, 24 Mission Mode's), pictures only; the port's panel's
# last row.
MODE_LABEL = {"es": "Modo juego", "en": "Game Mode", "fr": "Mode de jeu", "de": "Spielmodus", "it": "Mod. gioco"}
DIFFICULTIES = {
    "es": ["Principiante", "Normal", "Experto"],
    "en": ["Beginner", "Standard", "Proud"],
    "fr": ["Facile", "Normal", "Expert"],
    "de": ["Anfänger", "Standard", "Profi"],
    "it": ["Principiante", "Standard", "Eroico"],
}
# Title Screen's question as the port asks it (host/screens/ui_text.c TXT_CAMP_BACK_TO_MENU): its letters
# in the font.
BACK_TO_MENU = {
    "es": "¿Volver al menú principal?",
    "en": "Return to the main menu?",
    "fr": "Revenir au menu principal ?",
    "de": "Zum Hauptmenü zurückkehren?",
    "it": "Tornare al menu principale?",
}
MISSION_MODE = {
    "es": "Modo misión",
    "en": "Mission Mode",
    "fr": "Mode Missions",
    "de": "Missionsmodus",
    "it": "Missioni",
}
# Where a member is when it is on the menu: the screen's title word (DDD's, layout 1 node 11, in
# each language's word texture: MENÚ, MENU, MENU, MENÜ, MENU) in the font of the other places
# (the places all in the same letters).
MENU_PLACE = {"es": "Menú", "en": "Menu", "fr": "Menu", "de": "Menü", "it": "Menu"}
SELECTED_FRAME = 30  # control 4's (the bar selected): its glow lit, its slide done
SIGIL = ("srslt/res.p2", 2, 45)
# the crown: DDD's save screen's (save_screen.py's: layout 4 node 14), the DS's own (UI/cm/cmo_&.p2
# entry 3 cell 39) is a picture of 16 x 12 dots (an HD one)
CROWN = (4, 14)
# the status panel: layout 17's node 10 (its nodes 93 and 94 are a scroll bar: the panel holds no
# more rows than it shows)
PANEL_NODE = 10
DIGITS = (512 // 4, 90, 16)  # cnt_00's digits: cells of 128 x 90 from row 16, four a row
SETTINGS_SPLIT = 48  # the settings key's part: the key's columns, then the gear's
CHECKS = (860, 1016)  # the portrait frame's checks: block 4's columns
# the model's box's edge: DDD's pink made the red of the title's pill (days_tones's red), as the DS's
# frame of the model is red
EDGE_RED = (0.75, 0.08, 0.1)
BOX_EDGE = 14  # the model's box's edge and its round corners: the window inside
PANEL_EDGE = 60  # the status panel's edges (its texels), left out of its body (its
# top third too: its band)
W, H = tt.PICTURE
GROUND = (8, 8, 14)
WHITE, YELLOW = (255, 255, 255), (255, 230, 60)
# the heart points' word and digits in the DS's red (its HEART POINTS)
HEARTS_RED = (235, 45, 60)
# Where things go, the port's (the mock-up the maintainer chose), 1920 x 1080:
BARS = (80, 210, 560, 86)  # the first bar's left, top and width; the bars' pitch
FRAME = (660, 192, 560)  # the portrait frame: its left, top and size
TRIALS_Y = 762  # the trials' bar, under the frame, as wide
PANEL = (1240, 190, 640, 650)  # the status panel
STAT_ROWS = 10  # its rows (the lines under them): nine of numbers and the game mode
# A text's style: its left (or anchor), its cells' top, the cells' height, its alignment and its
# colour; a row's or the panel's from their own top left (the host adds them).
TEXTS = {
    "option": (40, None, 40, "left", WHITE),  # in a bar: its capitals' middle on the bar's
    "description": (520, 900, 40, "left", WHITE),
    "prompt": (1580, 1016, 32, "right", WHITE),  # right of the band (before its heart), clear of
    # descriptions of three lines
    "notice": (1700, 46, 32, "right", WHITE),  # Mission Mode's line over the menu, where the DS has
    # it: the top band's right, before the settings key
    "name": (230, 26, 56, "left", WHITE),
    "tag": (0, None, 30, "centre", WHITE),
    "level_label": (230, 104, 30, "left", (200, 200, 200)),
    "level_value": (400, 104, 30, "right", YELLOW),
    "stat_label": (30, 214, 34, "left", WHITE),
    "stat_value": (610, 214, 34, "right", YELLOW),
    "trials_label": (0, 0, 34, "left", WHITE),  # the trials' row, centred in its bar
    "trials_value": (12, 0, 34, "left", YELLOW),  # after its symbol: x the gap
    "hearts_label": (50, 952, 24, "left", HEARTS_RED),
    "day": (0, None, 30, "right", WHITE),  # the story's day on its tag: its right, its middle
    # the Preferences page: its sections' words (a tab's middle), a setting's name and value (from
    # the row's left, the row's middle), the value's help's lines under the setting's (the
    # description's)
    "page_tab": (0, None, 34, "centre", (165, 165, 165)),
    "page_tab_on": (0, None, 34, "centre", WHITE),
    "setting": (30, None, 34, "left", WHITE),
    "setting_value": (0, None, 34, "left", WHITE),
    "detail": (520, 948, 34, "left", YELLOW),
    "button": (0, None, 34, "centre", WHITE),  # a question's answer: its button's middle
    # a member's plate's (from its top left)
    "member_tag": (0, None, 26, "centre", WHITE),
    "member_name": (124, 6, 26, "left", WHITE),
    "member_place": (124, 34, 24, "left", (200, 200, 200)),
    "member_ready": (124, 34, 24, "left", YELLOW),
}
PLACES = {
    "face": (29, 29, 172, 172),  # the panel's
    "face_ring": (26, 26, 178, 178),
    "tag": (583, 57, 27, 27),  # the place's circle: its middle and radius
    "level": (0, 0, 190, 46),  # the level's numbers: a column's, a row's pitch
    "stats": (0, 0, 0, 42),  # the rows' pitch (ten rows in the panel)
    "stat_line": (30, 256, 580, 2),  # the first row's line
    "trial": (0, 0, 34, 44),  # the trials' row: the gap before a symbol, its height
    "munny": (410, 906, 0, 32),  # the money's digits: their right, top, height
    "hearts": (410, 982, 0, 32),
    "settings": (1880, 34, 0, 70),  # the settings key: its right, top, height
    # the group's members along the top band, one plate a place (where the DS shows each one's face
    # and where it is)
    "member": (420, 104, 330, 64),  # the first place's plate
    "member_circle": (32, 32, 22, 22),  # its circle: middle and radius
    "member_face": (62, 6, 52, 52),
    # the hand at the selected option (the theater's glove, the room list's): from the bar's drawing's
    # left and middle, its size
    "glove": (-66, -40, 106, 79),
    # the story's day: its tag on the model's box's top right (where the DS's frame has its DAY):
    # its right, top, the tag's height and the room either side of the words
    "day_tag": (1192, 206, 22, 50),
    # Title Screen's question's answers (yes, no): DDD's save screen's buttons, side by side under
    # the question (the game's, in the description's place), their size four fifths of DDD's, room
    # between them for the hand
    "button_yes": (560, 966, 288, 74),
    "button_no": (948, 966, 288, 74),
    # the Preferences page's: L and R either side of the tabs (the left's right, the right's left
    # end: x, the tabs' middle, the icons' height), the hand at the chosen setting (from its row's
    # left and middle), a value's left arrow (from the row's left: x, its height), the gap either
    # side of the value's words, the scroll bar's track (right of the rows: DDD's save screen's,
    # its thumb's place in it from page_track_k)
    "page_l": (770, 239, 0, 30),
    "page_r": (1770, 239, 0, 30),
    "page_glove": (-98, -40, 106, 79),
    "page_arrow": (560, 0, 0, 26),
    "page_value_gap": (12, 0, 0, 0),
    "page_track": (1818, 286, 0, 532),
}
# The Preferences page's panel (the model's box's, the trials' and the status panel's room) and its
# tabs and rows: their drawing's edges (the first's), the next one's pitch; how many rows show.
PAGE_PANEL = (660, 190, 1220, 650)
PAGE_TABS = (800, 216, 440, 46, 490)
PAGE_ROWS = (756, 290, 1050, 38, 54, 10)
LINES = {
    "description_pitch": "50",
    "digit_overlap": "0.18",  # DDD's digits lean: each overlaps the next
    "members": "345",  # the places' plates' pitch
    "munny_colour": f"{YELLOW[0]} {YELLOW[1]} {YELLOW[2]}",
    "hearts_colour": " ".join(str(c) for c in HEARTS_RED),
    "day_colour": " ".join(str(round(255 * c)) for c in EDGE_RED),  # the day's tag, the box's red
    "detail_pitch": "40",  # the value's help's lines
    "page_backdrop": "camp_page_backdrop.png",
    "page_tabs": " ".join(str(v) for v in PAGE_TABS),
    "page_rows": " ".join(str(v) for v in PAGE_ROWS),
    # the DS's frame of the model (its upper screen's dots: every polygon in it is the model's), and
    # the part of it the window shows: the frame less some of its empty top, right and bottom (the
    # model larger; Xaldin's head, the tallest seen, at dot 43)
    "model_source": "117 29 87 101",
    "model_view": "117 38 80 89",
    # the story's camp's (ov025's): its frame of the model larger (115-253 x 20-140 dots with its
    # edge), the model nearer; its inside the window's view
    "story_model_source": "117 22 134 116",
    # the view as large as Mission Mode's shows its model: Roxas as wide and in the same place
    # (the maintainer wants the two menus alike;
    # Mission Mode's Roxas 737-1046 x from 422 in the picture, the story's at view 98 -36 169 169
    # 742-1037 from 444)
    "story_model_view": "100.5 -26 161.3 161.3",
}


# --- DDD's camp ---------------------------------------------------------------------------------


def camp_layouts(ddd: Path, lang: str) -> tt.Layouts:
    """The language's camp menu with its HD textures, the parts' texels scaled from the CTRTs'."""
    path = ddd / LAYOUTS.format(lang=lang)
    b = path.read_bytes()
    saved = tt.tim2_picture
    tt.tim2_picture = lambda data, at: np.zeros((1, 1, 4), dtype=np.uint8)  # a CTRT, not a TIM2
    try:
        layouts = tt.Layouts(path)
    finally:
        tt.tim2_picture = saved
    n, o = struct.unpack_from("<2i", b, 0x20)
    if n != len(BLOCKS):
        raise ValueError(f"{path}: {n} blocks, not {len(BLOCKS)}")
    hd_dir = ddd / LAYOUTS.format(lang=lang).replace("original", "remastered")
    for i, blk in enumerate(layouts.blocks):
        base = o + struct.unpack_from("<I", b, o + 4 * i)[0]
        ctrt = base + struct.unpack_from("<3I", b, base + 0x10)[2]
        if b[ctrt : ctrt + 4] != b"CTRT":
            raise ValueError(f"{path}: block {i} has no CTRT texture")
        width = struct.unpack_from("<H", b, ctrt + 0x20)[0]
        hd = np.asarray(Image.open(hd_dir / f"{PREFIX[lang]}_{BLOCKS[i]}.png").convert("RGBA"))
        k = hd.shape[1] / width
        blk.texture = hd
        blk.parts = [(int(u0 * k), int(v0 * k), int(u1 * k), int(v1 * k), *c) for (u0, v0, u1, v1, *c) in blk.parts]
    return layouts


def layer(layouts, lid, shown, leave_out=None, frame=None) -> Image.Image:
    """The layout's nodes in `shown` ({node: control}, their parents shown still) as RGBA, 1920 x
    1080, at `frame` (by default most of the way through the layout): drawn on black and on white,
    the alpha what the two differ by."""
    nodes = layouts.layout_nodes(lid)
    ids = [n[12] for n in nodes]
    controls = {i: None for i in ids}
    for n in nodes:
        if n[12] in shown:
            controls[n[12]] = shown[n[12]]
            p = n[9]
            while p >= 0:
                if controls[ids[p]] is None:
                    controls[ids[p]] = 0
                p = nodes[p][9]
    if frame is None:
        frame = max(1, layouts.layouts[layouts.ids.index(lid)][0]) * 0.9
    atlas, entries = tt.Atlas(), []
    for p in layouts.pieces(lid, float(frame), controls, {}):
        if p["region"][0] != "texture" or p["node"] not in shown:
            continue
        _, blk, ua, va, ub, vb = p["region"]
        if leave_out is not None and leave_out(blk, ua):
            continue
        atlas.add(p["region"], layouts.blocks[blk].texture[va:vb, ua:ub])
        entries.append({"piece": p, "keys": [(0.0, tuple(p["rect"]) + tuple(p["colour"]))]})
    px = atlas.pack()
    out = []
    saved = tt.GROUND
    try:
        for ground in ((0.0, 0.0, 0.0), (1.0, 1.0, 1.0)):
            tt.GROUND = ground
            out.append(tt.render(px, [atlas.texels(e["piece"]) for e in entries], entries, 0.0, (W, H)))
    finally:
        tt.GROUND = saved
    black, white = out
    alpha = np.clip(1.0 - (white - black).mean(axis=2), 0, 1)
    rgb = np.where(alpha[..., None] > 1e-3, black / np.maximum(alpha[..., None], 1e-3), 0)
    return Image.fromarray((np.dstack([np.clip(rgb, 0, 1), alpha]) * 255).astype(np.uint8), "RGBA")


def days_tones(im: Image.Image, red=False) -> Image.Image:
    """DDD's blues as Days's: greys (their lightness), or a red."""
    a = np.asarray(im).astype(np.float32)
    v = a[..., :3].max(axis=2)
    blue = (a[..., 2] > a[..., 0] + 20) & (a[..., 2] > a[..., 1])
    out = a.copy()
    if red:
        out[..., 0] = np.where(blue, v * 0.75, a[..., 0])
        out[..., 1] = np.where(blue, v * 0.08, a[..., 1])
        out[..., 2] = np.where(blue, v * 0.1, a[..., 2])
    else:
        for c in range(3):
            out[..., c] = np.where(blue, v * 0.55, a[..., c])
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def cut(im: Image.Image):
    """The picture cut to its drawing, and where that was."""
    box = im.getbbox()
    return im.crop(box), box


def tinted(im: Image.Image, colour) -> Image.Image:
    """A grey word or digit in a colour: its light the colour's (DDD's grey brought up to white:
    its light over that of most of its drawing)."""
    a = np.asarray(im).astype(np.float32)
    light = a[..., :3].max(axis=2)
    solid = light[a[..., 3] > 128]
    top = float(np.percentile(solid, 90)) if solid.size else float(light.max())
    shade = np.clip(light / max(1.0, top), 0, 1)
    for i in range(3):
        a[..., i] = shade * colour[i]
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def word(layouts, lid, node, height, colour) -> Image.Image:
    """A word of the language's word texture, as the node cuts it (its largest part: the node's
    others are its underline), `height` texels high."""
    regions = [
        p["region"]
        for p in layouts.pieces(lid, 0.0, {n[12]: 0 for n in layouts.layout_nodes(lid)}, {})
        if p["node"] == node and p["region"][0] == "texture"
    ]
    if not regions:
        raise ValueError(f"layout {lid} has no node {node} with a texture")
    _, blk, ua, va, ub, vb = max(regions, key=lambda r: (r[4] - r[2]) * (r[5] - r[3]))
    im, _ = cut(Image.fromarray(np.ascontiguousarray(layouts.blocks[blk].texture[va:vb, ua:ub]), "RGBA"))
    k = height / im.size[1]
    return tinted(im.resize((max(1, round(im.size[0] * k)), height), Image.LANCZOS), colour)


def digits(ddd: Path, lang: str):
    """cnt_00's ten big digits, each cut to its drawing, in grey (the port tints them)."""
    sheet = Image.open(
        ddd / LAYOUTS.format(lang=lang).replace("original", "remastered") / f"{PREFIX[lang]}_cnt_00.png"
    ).convert("RGBA")
    w, h, top = DIGITS
    out = []
    for d in range(10):
        cx, cy = d % 4, d // 4
        im, _ = cut(sheet.crop((cx * w, top + cy * h, (cx + 1) * w, top + (cy + 1) * h)))
        out.append(np.asarray(tinted(im, WHITE)))
    return out


def node_rect(layouts, lid, node, control, frame) -> tuple:
    """A node's parts' rectangle at a frame of its control (its parents still), whole texels."""
    nodes = layouts.layout_nodes(lid)
    ids = [n[12] for n in nodes]
    controls = {i: None for i in ids}
    controls[node] = control
    p = nodes[ids.index(node)][9]
    while p >= 0:
        controls[ids[p]] = 0
        p = nodes[p][9]
    rects = [q["rect"] for q in layouts.pieces(lid, float(frame), controls, {}) if q["node"] == node]
    return (
        int(min(r[0] for r in rects)),
        int(min(r[1] for r in rects)),
        int(np.ceil(max(r[2] for r in rects))),
        int(np.ceil(max(r[3] for r in rects))),
    )


def model_box(layouts, body: Image.Image, size: int) -> Image.Image:
    """The model's box, `size` texels square: the inner panel of layout 23's portrait frame (node
    16 without its checks, block 4's u 860-1016) as large as the whole frame -- its rounded
    square, its pink edge made red (the title pill's) -- filled with the status panel's body."""
    im = layer(layouts, 23, {16: 0, 116: 0}, leave_out=lambda blk, u: blk == 4 and CHECKS[0] <= u <= CHECKS[1])
    inner = np.asarray(cut(im)[0].resize((size, size), Image.LANCZOS)).astype(np.float32)
    fill = np.asarray(body.resize((size, size), Image.LANCZOS)).astype(np.float32)
    pink = (inner[..., 0] > 150) & (inner[..., 0] > inner[..., 1] + 80)
    out = fill.copy()
    light = inner[..., :3].max(axis=2)
    for c in range(3):
        out[..., c] = np.where(pink, light * EDGE_RED[c], fill[..., c])
    # solid inside its edge (the panel's own fill starts under its top edge)
    drawn = inner[..., 3] > 8
    solid = np.zeros_like(drawn)
    rows = np.nonzero(drawn.any(axis=1))[0]
    for y in range(rows[0], rows[-1] + 1):
        xs = np.nonzero(drawn[y])[0]
        if xs.size:
            solid[y, xs[0] : xs[-1] + 1] = True
    out[..., 3] = np.where(solid, 255, 0)
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def ds_cell(data: Path, rel: str, entry: int, index: int) -> np.ndarray:
    p2 = data / "nitrofs" / "UI" / rel
    pack = ss.d2kp(ss.p2_entry(p2, entry))
    colours, per = ss.palette(pack[b"RLCN"][0])
    chars, bpp = ss.tiles(pack[b"RGCN"][0])
    im = Image.fromarray(ss.cell(pack[b"RECN"][0], index, chars, bpp, colours, per), "RGBA")
    return np.asarray(cut(im)[0])


# --- The game's words ---------------------------------------------------------------------------


def strings(data: Path, rel: str):
    """A string table of the game's (u32 its header's size, u32 the count, then each record's u32
    length -- these 4 bytes in it -- and its UTF-16LE text)."""
    for root in ("decompressed", "nitrofs"):
        p = data / root / rel
        if p.exists():
            b = p.read_bytes()
            break
    else:
        raise FileNotFoundError(rel)
    head, count = struct.unpack_from("<II", b, 0)
    out, at = [], head
    for _ in range(count):
        n = struct.unpack_from("<I", b, at)[0]
        out.append(b[at + 4 : at + n].decode("utf-16-le", "replace").rstrip("\0"))
        at += n
    return out


def characters(data: Path, lang: str) -> set:
    """Every character the screen may write: the tables it reads, its options, digits."""
    chars = (
        set("".join(OPTIONS[lang] + STORY_OPTIONS[lang] + STORY_SUBS[lang] + DIFFICULTIES[lang]))
        | set(MENU_PLACE[lang] + MODE_LABEL[lang] + MISSION_MODE[lang] + BACK_TO_MENU[lang])
        | set("0123456789/%- .")
    )
    for rel in (
        f"UI/cm/str/status_{lang}.s",
        f"UI/cm/str/root_{lang}.s",
        f"UI/cm/str/select_{lang}.s",
        f"UI/mlt/mlt_{lang}.s",
        f"UI/btl/{lang}/info.s",
        f"UI/cm/str/sav_{lang}.s",
        f"UI/cm/str/cfg_{lang}.s",
    ):
        chars |= set("".join(strings(data, rel)))
    return {c for c in chars if ord(c) >= 32 and not 0x3300 <= ord(c) <= 0x33FF}  # the DS's buttons: icons


# --- The Preferences page's pieces --------------------------------------------------------------


def settings_piece(layouts, name: str, layout: int, node: int, control: int, w: float, h: float):
    """A piece of the settings menu's in a state, its shown drawing's edges w x h, drawn as the
    settings menu lays a piece in a rectangle (settings_skin.cpp lay(): the ends scaled with the
    height, the middle stretched): its pixels, and their place from those edges' top left."""

    def coloured(part_name, role, px, selected):
        if (part_name, role) in ts.KEPT:
            return px
        return ts.red_and_silver(px) if selected else ts.greys(px)

    def parts(ctrl):
        out = []
        for role, p in ts.roles(name, ts.parts_of(layouts, layout, node, ctrl)):
            px = coloured(name, role, ts.part_pixels(layouts, p), ctrl != ts.SHOWN)
            out.append((role, p["rect"][0], p["rect"][1], px))
        return out

    ref, mine = parts(ts.SHOWN), parts(control)
    bx0 = min(r[1] for r in ref)
    by0 = min(r[2] for r in ref)
    bx1 = max(r[1] + r[3].shape[1] for r in ref)
    by1 = max(r[2] + r[3].shape[0] for r in ref)
    shown = Image.new("RGBA", (int(np.ceil(bx1 - bx0)), int(np.ceil(by1 - by0))))
    for _, x, y, px in ref:
        shown.alpha_composite(Image.fromarray(px, "RGBA"), (int(round(x - bx0)), int(round(y - by0))))
    seen = np.asarray(shown)[..., 3] > 40
    ys, xs = np.nonzero(seen.any(axis=1))[0], np.nonzero(seen.any(axis=0))[0]
    mx0, my0 = xs.min(), ys.min()
    mx1, my1 = shown.size[0] - 1 - xs.max(), shown.size[1] - 1 - ys.max()
    along = any(r[0] in ("left", "right") for r in ref)
    ky = h / (shown.size[1] - my0 - my1)
    kx = ky if along else w / (shown.size[0] - mx0 - mx1)
    x0, y0, x1, y1 = -mx0 * kx, -my0 * ky, w + mx1 * kx, h + my1 * ky
    k = ky if along else 0
    lo, hi, laid = x0, x1, []
    for role, px_, py_, px in mine:
        pw, ph = px.shape[1], px.shape[0]
        if along:
            ry0 = y0 + (py_ - by0) * k
            if role == "right":
                rx0 = x1 - (bx1 - px_) * k
                hi = min(hi, rx0)
            else:
                rx0 = x0 + (px_ - bx0) * k
                if role == "left":
                    lo = max(lo, rx0 + pw * k)
            laid.append([role, rx0, ry0, rx0 + pw * k, ry0 + ph * k, px])
        else:
            rx0, ry0 = x0 + (px_ - bx0) * (x1 - x0) / (bx1 - bx0), y0 + (py_ - by0) * (y1 - y0) / (by1 - by0)
            laid.append([role, rx0, ry0, rx0 + pw * (x1 - x0) / (bx1 - bx0), ry0 + ph * (y1 - y0) / (by1 - by0), px])
    for item in laid:
        if item[0] == "middle" and along:
            item[1], item[3] = lo, hi
    cx0, cy0 = min(i[1] for i in laid), min(i[2] for i in laid)
    cx1, cy1 = max(i[3] for i in laid), max(i[4] for i in laid)
    canvas = Image.new("RGBA", (int(np.ceil(cx1 - cx0)), int(np.ceil(cy1 - cy0))))
    for _, rx0, ry0, rx1, ry1, px in laid:
        # each edge rounded once, so that a part ends where the next begins (a column of nothing
        # showed between the middle and the right end, rounded apart)
        ix0, iy0 = int(round(rx0 - cx0)), int(round(ry0 - cy0))
        ix1, iy1 = int(round(rx1 - cx0)), int(round(ry1 - cy0))
        canvas.alpha_composite(
            Image.fromarray(px, "RGBA").resize((max(1, ix1 - ix0), max(1, iy1 - iy0)), Image.LANCZOS), (ix0, iy0)
        )
    return np.asarray(canvas), (cx0, cy0, canvas.size[0], canvas.size[1])


def page_panel(panel: Image.Image, w: int, h: int) -> Image.Image:
    """The status panel as one panel of w x h: its ends kept (scaled with the height), its middle
    stretched."""
    k = h / panel.size[1]
    edge = PANEL_EDGE
    end_w = int(round(edge * k))
    left = panel.crop((0, 0, edge, panel.size[1])).resize((end_w, h), Image.LANCZOS)
    right = panel.crop((panel.size[0] - edge, 0, panel.size[0], panel.size[1])).resize((end_w, h), Image.LANCZOS)
    middle = panel.crop((edge, 0, panel.size[0] - edge, panel.size[1])).resize((w - 2 * end_w, h), Image.LANCZOS)
    out = Image.new("RGBA", (w, h))
    out.alpha_composite(left, (0, 0))
    out.alpha_composite(middle, (end_w, 0))
    out.alpha_composite(right, (w - end_w, 0))
    return out


# --- The file -----------------------------------------------------------------------------------


def font_glyphs(font: ng.Font, chars):
    """Each character's cell (its grey the ink), its bearing and advance, in the HD sheet's texels."""
    k, per = font.k, font.cols * font.rows
    cw, ch = int(round(font.cw * k)), int(round(font.ch * k))
    out = {}
    for c in sorted(chars):
        idx = font.map.get(ord(c))
        if idx is None:
            continue
        sheet, rest = divmod(idx, per)
        row, col = divmod(rest, font.cols)
        x0, y0 = int(round(col * (font.cw + 1) * k)), int(round(row * (font.ch + 1) * k))
        cell = font.sheets[sheet][y0 : y0 + ch, x0 : x0 + cw, 3]
        left, _, advance = font.widths.get(idx, font.default)
        out[c] = (cell, left * k, advance * k)
    return out, ch


def capitals(glyphs) -> tuple:
    """Where the capitals ink their cells: H's first and last rows."""
    rows = np.nonzero(glyphs["H"][0].max(axis=1) > 96)[0]
    return int(rows[0]), int(rows[-1]) + 1


def write(
    out_dir: Path,
    lang: str,
    pieces: dict,
    places: dict,
    lines: dict,
    extra,
    font: ng.Font,
    chars,
    icons: dict,
    texts: dict,
) -> str:
    atlas = tt.Atlas()
    for name, px in pieces.items():
        atlas.add((name,), np.ascontiguousarray(px))
    for name, px in icons.items():
        atlas.add(("icon", name), px)
    pixels = atlas.pack()
    glyphs, cell = font_glyphs(font, chars)
    top, bottom = capitals(glyphs)
    text = [
        "# Mission Mode's camp menu on one screen, in Dream Drop Distance HD's camp pieces in Days's",
        "# tones, made by tools/hd/ddd_camp.py from the player's own copies (host/screens/camp_screen.c reads",
        "# it; camp_backdrop.png is the picture behind the game's 3D model).",
        "#   piece NAME U V W H         a picture in the atlas: bar, bar_selected, face<N> (the game's",
        "#                              character N), sigil, crown, digit<N>, settings_esc, settings_gear, glove,",
        "#                              button_yes, button_no, button_selected_yes, button_selected_no,",
        "#                              the Preferences page's page_tab[_selected], page_row[_selected],",
        "#                              page_arrow_left, page_arrow_right, page_track, page_thumb (its",
        "#                              place from the track's: x, the room above and below it, size)",
        "#   place NAME X Y W H         where something goes (1920 x 1080), or from its row's or the",
        "#                              panel's top left",
        "#   text NAME X Y SIZE ALIGN GREY [R G B]   a line (Y its capitals' top), its size, its colour",
        "#   option N WORDS             the option N's name (the game's pictures', transcribed)",
        "#   story_option N WORDS       the story's camp's option N's name (the same pictures')",
        "#   story_sub N WORDS          Holo-Missions' two options (0, 1), Files' (2, 3)",
        "#   story_model_source, story_model_view   the story's camp's model (as model_*)",
        "#   menu_place WORDS           where a member is on the menu (DDD's title word)",
        "#   mode_label WORDS, difficulty N WORDS, mission_mode WORDS   the game mode row's",
        "#   bars X Y W PITCH, frame X Y SIZE, model X Y W H, panel X Y W H   the backdrop's pieces",
        "#   model_source X Y W H       the DS's frame of the model, in its upper screen's dots",
        "#   model_view X Y W H         the part of it the window shows",
        "#   page_backdrop FILE         the Preferences page's picture (one panel for the model's box,",
        "#                              the trials and the status panel)",
        "#   page_tabs X Y W H PITCH    its sections' tabs: the first's drawing's edges, the pitch",
        "#                              (places page_tab, page_tab_selected: the pieces from there)",
        "#   page_rows X Y W H PITCH SHOWN   its settings' rows, as the tabs (page_row, page_row_selected),",
        "#                              how many show",
        "#   detail_pitch N             the value's help's lines",
        "#   icon NAME U V W H, font FILE CELL TOP BOTTOM, glyph CODE X Y W LEFT ADVANCE",
        f"picture {W} {H}",
        "atlas camp_atlas.png",
        "backdrop camp_backdrop.png",
    ]
    for name in pieces:
        u, v = atlas.at[(name,)]
        h, w = atlas.pixels[(name,)].shape[:2]
        text.append(f"piece {name} {u} {v} {w} {h}")
    for name, (x, y, w, h) in places.items():
        text.append(f"place {name} {round(x, 1)} {round(y, 1)} {round(w, 1)} {round(h, 1)}")
    for name, value in lines.items():
        text.append(f"{name} {value}")
    text.extend(extra)
    for name, (x, cells_top, height, align, colour) in texts.items():
        size = height / cell
        # the port's text styles: Y the capitals' top, or their middle for a centred line (and a
        # row's line, whose middle goes on the row's)
        y = 0.0 if cells_top is None else cells_top + top * size
        shade = colour[0] if colour[0] == colour[1] == colour[2] else f"255 {colour[0]} {colour[1]} {colour[2]}"
        text.append(f"text {name} {x} {y:.1f} {size:.4f} {align} {shade}")
    for name, px in icons.items():
        u, v = atlas.at[("icon", name)]
        text.append(f"icon {name} {u} {v} {px.shape[1]} {px.shape[0]}")
    cols = 32
    codes = sorted(glyphs, key=ord)
    cell_w = max(g[0].shape[1] for g in glyphs.values())
    sheet = np.zeros((((len(codes) + cols - 1) // cols) * cell, cols * cell_w, 4), np.uint8)
    sheet[..., 3] = 255
    text.append(f"font camp_font.png {cell} {top} {bottom}")
    for n, c in enumerate(codes):
        ink, left, advance = glyphs[c]
        gx, gy = (n % cols) * cell_w, (n // cols) * cell
        for i in range(3):
            sheet[gy : gy + ink.shape[0], gx : gx + ink.shape[1], i] = ink
        text.append(f"glyph {ord(c)} {gx} {gy} {ink.shape[1]} {left:.2f} {advance:.2f}")
    out_dir.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels, "RGBA").save(out_dir / "camp_atlas.png")
    Image.fromarray(sheet, "RGBA").save(out_dir / "camp_font.png")
    (out_dir / "camp.txt").write_bytes(("\n".join(text) + "\n").encode("utf-8"))
    return f"{len(pieces)} pieces, {len(icons)} icons, {len(codes)} glyphs, atlas {pixels.shape[1]} x {pixels.shape[0]}"


def make(ddd: Path, mare: Path, settings: Path, data: Path, lang: str, mod: Path, font: ng.Font) -> str:
    L = camp_layouts(ddd, lang)
    out_dir = mod / "camp" / lang
    back = Image.new("RGBA", (W, H), GROUND + (255,))
    back.alpha_composite(days_tones(layer(L, 3, {1: 0, 2: 0})))  # the sides' shade
    back.alpha_composite(days_tones(layer(L, 1, {1: 0, 43: 0})))  # the top band and its line
    back.alpha_composite(days_tones(layer(L, 14, {2: 0})))  # the bottom band, the heart
    back.alpha_composite(days_tones(layer(L, 1, {10: 0}), red=True))  # the title's pill
    # MENU as DDD draws it, white with its shadow in its node's place (layout 1 node 11): each
    # language's word fits the pill there (at one height the longer ones ran out of it)
    back.alpha_composite(layer(L, 1, {11: 0}))
    back.alpha_composite(word(L, 14, 401, 18, (235, 235, 235)), (50, 884))  # the money's word
    page_back = back.copy()  # the Preferences page's: its panel instead of what follows

    # the options' bars: control 0, and control 4 for the one selected, each cut by its node's
    # rectangle (control 4's is control 0's slid 40 texels right: put back where the plain one is,
    # the two the same size -- a gap showed round the selected one); both pieces: the port draws each row's, the selected one's alone
    plain_rect, lit_rect = node_rect(L, 11, 100, 0, 0.0), node_rect(L, 11, 100, 4, SELECTED_FRAME)
    bar = layer(L, 11, {100: 0}, frame=0.0).crop(plain_rect)
    lit = layer(L, 11, {100: 4}, frame=SELECTED_FRAME).crop(lit_rect)
    seen = bar.getbbox()  # the plain bar's drawing in its rectangle
    bx, by, bw, pitch = BARS
    k = bw / (seen[2] - seen[0])  # its drawing as wide as BARS says, from BARS's corner
    size_k = (round(bar.size[0] * k), round(bar.size[1] * k))
    bar, lit = bar.resize(size_k, Image.LANCZOS), lit.resize(size_k, Image.LANCZOS)
    bx_k, by_k = bx - seen[0] * k, by - seen[1] * k
    lit_place = (0, 0, size_k[0], size_k[1])
    bar_body = (seen[0] * k, seen[1] * k, (seen[2] - seen[0]) * k, (seen[3] - seen[1]) * k)
    texts = {**TEXTS, "option": (TEXTS["option"][0] + seen[0] * k, *TEXTS["option"][1:])}

    # the model's box where the portrait frame goes, as large (the grey
    # box filling the whole frame), dark as the DS's own panel behind the model: the status
    # panel's middle, its edges left out; the model's window inside its edge; the trials' bar
    fx, fy, size = FRAME
    panel, _ = cut(days_tones(layer(L, 17, {PANEL_NODE: 0})))
    body = panel.crop((PANEL_EDGE, panel.size[1] // 3, panel.size[0] - PANEL_EDGE, panel.size[1] - PANEL_EDGE))
    back.alpha_composite(model_box(L, body, size), (fx, fy))
    model = (fx + BOX_EDGE, fy + BOX_EDGE, size - 2 * BOX_EDGE, size - 2 * BOX_EDGE)
    row, _ = cut(days_tones(layer(L, 11, {700: 0})))
    row = row.resize((size, round(row.size[1] * size / row.size[0])), Image.LANCZOS)
    back.alpha_composite(row, (fx, TRIALS_Y))
    trials_h = row.size[1]

    # the status panel and its rows' lines
    px0, py0, pw, ph = PANEL
    back.alpha_composite(panel.resize((pw, ph), Image.LANCZOS), (px0, py0))
    lines_px = np.asarray(back).copy()
    lx, ly, lw, lh = PLACES["stat_line"]
    for i in range(STAT_ROWS):
        y = py0 + ly + i * PLACES["stats"][3]
        region = lines_px[y : y + lh, px0 + lx : px0 + lx + lw, :3].astype(np.float32)
        lines_px[y : y + lh, px0 + lx : px0 + lx + lw, :3] = (region * (1 - 50 / 255) + 255 * 50 / 255).astype(np.uint8)
    back = Image.fromarray(lines_px, "RGBA")
    out_dir.mkdir(parents=True, exist_ok=True)
    back.convert("RGB").save(out_dir / "camp_backdrop.png")
    gx, gy, gw, gh = PAGE_PANEL
    page_back.alpha_composite(page_panel(panel, gw, gh), (gx, gy))
    page_back.convert("RGB").save(out_dir / "camp_page_backdrop.png")

    pieces = {"bar": np.asarray(bar), "bar_selected": np.asarray(lit)}
    for n, name in enumerate(tmi.FACES):
        pieces[f"face{n}"] = tmi.face(mare, name)
    pieces["sigil"] = ds_cell(data, SIGIL[0], SIGIL[1], SIGIL[2])
    save = ss.ddd_layouts(ddd, lang)
    crown = ss.together(save, CROWN[0], CROWN[1], ss.SHOWN, lambda p, px: px)[0]
    pieces["crown"] = np.asarray(cut(Image.fromarray(crown, "RGBA"))[0])
    # a question's buttons: the save list's (DDD's save screen's layout 7, nodes 2 and 3)
    for selected, name in ((False, "button"), (True, "button_selected")):
        for node, side in ((2, "yes"), (3, "no")):
            pieces[f"{name}_{side}"] = ss.together(
                save, 7, node, ss.SELECTED if selected else ss.SHOWN, ss.button_colour(selected)
            )[0]
    for d, px in enumerate(digits(ddd, lang)):
        pieces[f"digit{d}"] = px
    folder = mare / tt.LANGUAGES[lang][0]
    theatre = tt.Layouts(folder / "l2d" / "days_theatre.l2d")
    launcher = tt.Layouts(folder / "l2d" / "launcher.l2d")
    pieces["glove"] = {name: px for name, px, _ in tc.list_pieces(theatre, launcher, lang)[0]}["glove"]
    key, _ = cut(layer(L, 1, {2: 0}))
    keys = np.asarray(key)
    pieces["settings_esc"] = keys[:, :SETTINGS_SPLIT]
    pieces["settings_gear"] = keys[:, SETTINGS_SPLIT:]
    places = {**PLACES, "bar_selected": lit_place, "bar_body": bar_body}
    lines = {
        **LINES,
        "bars": f"{bx_k:.1f} {by_k:.1f} {size_k[0]} {pitch} {size_k[1]}",
        "frame": f"{fx} {fy} {size}",
        "panel": f"{px0} {py0} {pw} {ph}",
        "trials": f"{fx} {TRIALS_Y} {size} {trials_h}",
        "model": " ".join(f"{v:.1f}" for v in model),
    }
    # the Preferences page's pieces: the settings menu's, each at its size
    sl = tt.Layouts(settings / ts.LANGUAGES[lang] / ts.LAYOUTS)
    _, _, tab_w, tab_h, _ = PAGE_TABS
    _, _, row_w, row_h, _, _ = PAGE_ROWS
    for piece, kind, layout, node, control, w, h in (
        ("page_tab", "section", 700, 701, ts.SHOWN, tab_w, tab_h),
        ("page_tab_selected", "section", 700, 701, ts.CHOSEN, tab_w, tab_h),
        ("page_row", "row", 750, 751, ts.SHOWN, row_w, row_h),
        ("page_row_selected", "row", 750, 751, ts.SELECTED, row_w, row_h),
    ):
        px, (ox, oy, pw, ph) = settings_piece(sl, kind, layout, node, control, w, h)
        pieces[piece] = px
        places[piece] = (round(ox, 1), round(oy, 1), pw, ph)
    arrow_h = PLACES["page_arrow"][3]
    for piece, node in (("page_arrow_left", 1027), ("page_arrow_right", 1028)):
        part = ts.parts_of(sl, 1020, node, ts.SHOWN)[0]
        px = ts.greys(ts.part_pixels(sl, part))
        size = (max(1, round(px.shape[1] * arrow_h / px.shape[0])), arrow_h)
        pieces[piece] = np.asarray(Image.fromarray(px, "RGBA").resize(size, Image.LANCZOS))
    # the scroll bar: DDD's save screen's track and thumb (layout 5 nodes 5555 and 5556), both
    # scaled alike to the track's height here, the thumb's place in the track kept (its arrows'
    # room above and below)
    track, (_, ty, tw, th) = ss.together(save, 5, 5555, ss.SHOWN, lambda p, px: ss.greys(px))
    thumb, (_, hy, hw, hh) = ss.together(save, 5, 5556, ss.SHOWN, lambda p, px: ss.greys(px))
    x, y, _, h = PLACES["page_track"]
    k = h / th
    pieces["page_track"] = np.asarray(Image.fromarray(track, "RGBA").resize((round(tw * k), h), Image.LANCZOS))
    pieces["page_thumb"] = np.asarray(
        Image.fromarray(thumb, "RGBA").resize((round(hw * k), round(hh * k)), Image.LANCZOS)
    )
    places["page_track"] = (x, y, round(tw * k), h)
    places["page_thumb"] = (0, round((hy - ty) * k, 1), round(hw * k), round(hh * k))
    extra = (
        [f"option {i} {name}" for i, name in enumerate(OPTIONS[lang])]
        + [f"story_option {i} {name}" for i, name in enumerate(STORY_OPTIONS[lang])]
        + [f"story_sub {i} {name}" for i, name in enumerate(STORY_SUBS[lang])]
        + [f"menu_place {MENU_PLACE[lang]}", f"mode_label {MODE_LABEL[lang]}", f"mission_mode {MISSION_MODE[lang]}"]
        + [f"difficulty {i} {name}" for i, name in enumerate(DIFFICULTIES[lang])]
    )
    icons = {**tt.icon_pixels(folder / "font"), **tc.key_icons(folder)}
    return write(out_dir, lang, pieces, places, lines, extra, font, characters(data, lang), icons, texts)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("ddd", type=Path, help="Dream Drop Distance's files: <2.8>/Image/dt/kh3d_first.hed_out")
    ap.add_argument("mare", type=Path, help="the theater extracted: <1.5+2.5>/Image/Mare.hed_out/original")
    ap.add_argument(
        "settings", type=Path, help="the collection's settings menu: <1.5+2.5>/Image/SettingMenu.hed_out/original"
    )
    ap.add_argument("--mod", default="Theater title")
    ap.add_argument("--lang", nargs="*", default=list(OPTIONS), choices=list(OPTIONS))
    ap.add_argument("--data", type=Path, help="the port's extracted game data (data/extracted/<hash>)")
    args = ap.parse_args()
    data = args.data
    if data is None:
        found = sorted((tt.ROOT / "data" / "extracted").glob("*/nitrofs"))
        if not found:
            ap.error("no extracted game data in data/extracted: give --data")
        data = found[0].parent
    font = ng.Font(args.ddd / "original" / ng.FONT, sorted((args.ddd / "remastered" / ng.FONT).glob("US_*.png")))
    mod = tt.ROOT / "mods" / args.mod / "layouts"
    for lang in tt.skip_missing_languages(data, args.lang):
        print(f"{lang}: camp {make(args.ddd, args.mare, args.settings, data, lang, mod, font)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
