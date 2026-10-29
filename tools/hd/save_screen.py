#!/usr/bin/env python3
"""Make the save list's pieces from Dream Drop Distance HD in Days's tones, from your own copies (DDD,
MARE: docs/HD_SCREENS.md, "Making them"), into mods/NAME/layouts/save/<lang>/.
  python tools/hd/save_screen.py DDD MARE [--data DIR] [--mod NAME] [--lang es|en|fr|de|it ...] [--preview DIR]
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
import newgame_dialog as ng  # noqa: E402
import theater_cine as tc  # noqa: E402
import theater_settings as ts  # noqa: E402
import theater_title as tt  # noqa: E402
import decompress_data as dd  # noqa: E402

LANGUAGES = ("es", "en", "fr", "de", "it")
CODES = {"es": "SP", "en": "US", "fr": "FR", "de": "GR", "it": "IT"}  # DDD's textures' prefixes
LAYOUTS = "menu/{lang}/camp/save/bin/save00_n.l2d"
TEXTURES = ("{code}_save00.dds", "{code}_save_gratext_00.dds", "{code}_save_cnt_00.dds", "{code}_save_wtitle.dds")
ICONS = "font/en/bin/iconfont_n.ctt/{code}_iconfont_n.png"
WORDS = "message/{lang}/system/bin/ct00200.ctd"
EMPTY = (0x6E, 0x8100)  # "No data."
# The guest's row's words as its picture writes them (UI/cm/cmo_&.p2's, guest_label), transcribed:
# the load list's help says them with Y.
GUEST_WORDS = {"es": "Juego invitado", "en": "Guest Play", "fr": "Invitation", "de": "Gastspiel", "it": "Gioca ospite"}
# The gratext texture's words (its texels, the same in every language: each word centred in it).
GRATEXT_SIZE = 1024  # the size GRATEXT's places are measured on: DDD's own picture, 1024 x 1024
GRATEXT = {
    "heading_save": (16, 16, 896, 176),
    "heading_load": (16, 192, 896, 352),
    "label_munny": (16, 544, 736, 624),
    "label_time": (16, 640, 736, 720),
}
SHOWN, SELECTED = 0, 4

# A file's row: layout 4's node 2 (954..2254 x 532..704 in DDD's picture), its frame's ends 40
# wide, drawn where the list puts it, its height 150 (DDD's 172 at the list's).
ROW_RECT = (954, 532, 2254, 704)
ROW_END = 40
LIST = (296, 212, 1276, 150)  # the first row: x, y, width, height
ROW_PITCH, ROWS_SHOWN = 155, 4
K = LIST[3] / (ROW_RECT[3] - ROW_RECT[1])
# Days's tones for the row (DDD's tints as they are named in its file).
CIRCLE = (1.7, 1.7, 1.7, 1.0)  # DDD's yellow circle: silver
PINK_RED = (1.45, 0.08, 0.10, 1.0)  # the selected frame's pink
DARK_PINK_RED = (0.55, 0.02, 0.03, 1.0)  # its second pass's
BUTTON_RED_DEPTH, SILVER = 0.75, 1.5

# Days's faces: UI/cm/cmo_&.p2's entry 3 (a D2KP), cells 48-50 (its animation 13's frames 2-4:
# Ov000_RefreshSelectionGroupDraw draws a saved slot with frame 2 + field 0xc4b).
FACES = (48, 49, 50)
FACE_FROM = 86  # the plate's columns from here on hold the face
FACE_FADE = 1.6  # its left edge fades in over 1 / 1.6 of its width
FACE_ALPHA = 0.8
GUEST_AFTER, GUEST_SKIP, GUEST_CHARS = 44, 10, 10  # the guest's words: see guest_label
GUEST_SCALE = 4  # its dots in the picture's texels

# The port's words (the mock-up's): (x, y, size, alignment, grey) -- x, y in the row for a row's,
# in the picture for the rest; size the help font's HD cells' scale.
TEXT = {
    "number": (88 * K, 88 * K, 1.25, "centre", 240),
    "day": (180, 22, 1.55, "left", 255),
    "level": (180, 74, 1.3, "left", 225),
    "munny": (824, 26, 1.3, "right", 255),
    "time": (824, 74, 1.3, "right", 255),
    "difficulty": (864, 26, 1.3, "left", 255),
    "empty": (LIST[2] / 2, 70, 1.35, "centre", 200),
    "help": (1190, 925, 1.4, "centre", 235),
    "help_keys": (1190, 1005, 1.4, "centre", 235),
    "ask": (1190, 880, 1.3, "centre", 235),
    "tell": (1190, 925, 1.4, "centre", 235),
    "button": (0, 0, 1.3, "centre", 255),  # from the button's middle
}
LINE_PITCH = {"ask": 46, "tell": 64}
PLACES = {
    "label_munny": (424, 32, None, 22),  # in the row: x, y, width (from the height), height
    "label_time": (424, 80, None, 22),
    "heading": (150, 44, None, 46),  # in the picture, on the tab
    # the hand: in the row, from its left (before it) and its middle; at a button, the same from
    # the button's left and middle
    "glove": (-100, 0, 122, 91),
}


# --- Days's files -----------------------------------------------------------------------------


def p2_entry(path: Path, entry: int) -> bytes:
    """An entry of a P2 archive (rom.h khdays_rom_file_data's), decompressed."""
    b = path.read_bytes()
    count = struct.unpack_from("<H", b, 2)[0]
    start = struct.unpack_from("<I", b, 0xC)[0]
    offsets = struct.unpack_from(f"<{count}H", b, 0x10)
    sizes = struct.unpack_from(f"<{count}I", b, 0x10 + (count + 1) // 2 * 4)  # 4-aligned, as rom.cpp's
    at, size = offsets[entry] * 0x200 + start, sizes[entry] & 0x7FFFFFFF
    data = b[at : at + size]
    return dd.decompress_nitro(data)[1] if sizes[entry] & 0x80000000 else data


def d2kp(pack: bytes):
    """A D2KP's resources by their magic (graphics2d.cpp parse_pk2d)."""
    out = {}
    for slot in range(8):
        section = struct.unpack_from("<I", pack, 8 + 4 * slot)[0]
        if section in (0, 0xFFFFFFFF) or section + 4 > len(pack):
            continue
        n = struct.unpack_from("<I", pack, section)[0]
        offsets = struct.unpack_from(f"<{n}I", pack, section + 4)
        sizes = struct.unpack_from(f"<{n}I", pack, section + 4 + 4 * n)
        for o, z in zip(offsets, sizes):
            out.setdefault(pack[o : o + 4], []).append(pack[o : o + z])
    return out


def palette(d: bytes):
    """An NCLR's colours (graphics2d.cpp decode_nclr)."""
    size = struct.unpack_from("<I", d, 0x20)[0]
    out = []
    for i in range(size // 2):
        c = struct.unpack_from("<H", d, 0x28 + 2 * i)[0]
        r, g, b = c & 0x1F, (c >> 5) & 0x1F, (c >> 10) & 0x1F
        out.append(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)))
    return out, 256 if struct.unpack_from("<I", d, 0x18)[0] == 4 else 16


def tiles(d: bytes):
    """An NCGR's 8 x 8 tiles of indices (graphics2d.cpp decode_ncgr)."""
    eight = struct.unpack_from("<I", d, 0x1C)[0] == 4
    size = struct.unpack_from("<I", d, 0x28)[0]
    at = 0x18 + struct.unpack_from("<I", d, 0x2C)[0]
    raw = np.frombuffer(d, np.uint8, size, at)
    if eight:
        return raw.reshape(-1, 64), 8
    lo, hi = raw & 0x0F, raw >> 4
    return np.stack([lo, hi], axis=1).reshape(-1, 64), 4


def cell(d: bytes, index: int, chars, bpp: int, colours, per: int):
    """An NCER's cell drawn (cell.cpp decode_ncer, render_cell), RGBA."""
    kb = 0x10
    records = kb + 8 + struct.unpack_from("<I", d, kb + 0xC)[0]
    record = 16 if struct.unpack_from("<H", d, kb + 0xA)[0] == 1 else 8
    boundary = 32 << (struct.unpack_from("<I", d, kb + 0x10)[0] & 7)
    count = struct.unpack_from("<H", d, kb + 8)[0]
    oam_base = records + count * record
    n, offset = (
        struct.unpack_from("<H", d, records + index * record)[0],
        struct.unpack_from("<I", d, records + index * record + 4)[0],
    )
    dims = [
        [(8, 8), (16, 16), (32, 32), (64, 64)],
        [(16, 8), (32, 8), (32, 16), (64, 32)],
        [(8, 16), (8, 32), (16, 32), (32, 64)],
    ]
    pieces = []
    for j in range(n):
        a0, a1, a2 = struct.unpack_from("<3H", d, oam_base + offset + 6 * j)
        w, h = dims[a0 >> 14][(a1 >> 14) & 3]
        y, x = a0 & 0xFF, a1 & 0x1FF
        pieces.append(
            (
                x - 512 if x >= 256 else x,
                y - 256 if y >= 128 else y,
                w,
                h,
                a2 & 0x3FF,
                (a2 >> 12) & 0xF,
                bool(a1 & 0x1000),
                bool(a1 & 0x2000),
            )
        )
    x0 = min(p[0] for p in pieces)
    y0 = min(p[1] for p in pieces)
    x1 = max(p[0] + p[2] for p in pieces)
    y1 = max(p[1] + p[3] for p in pieces)
    out = np.zeros((y1 - y0, x1 - x0, 4), np.uint8)
    unit = max(1, boundary // (64 if bpp == 8 else 32))
    for x, y, w, h, tile, pal, fx, fy in pieces:
        for py in range(h):
            for px in range(w):
                sx, sy = (w - 1 - px) if fx else px, (h - 1 - py) if fy else py
                t = tile * unit + (sy // 8) * (w // 8) + sx // 8
                if t >= len(chars):
                    continue
                i = chars[t][(sy % 8) * 8 + sx % 8]
                if i == 0 or pal * per + i >= len(colours):
                    continue
                out[y - y0 + py, x - x0 + px] = colours[pal * per + i] + (255,)
    return out


def faces(data: Path, lang: str):
    """Days's three faces: the plates' ends, their left edges fading in."""
    pack = d2kp(p2_entry(data / "nitrofs" / "UI" / "cm" / f"cmo_{lang}.p2", 3))
    colours, per = palette(pack[b"RLCN"][0])
    chars, bpp = tiles(pack[b"RGCN"][0])
    out = []
    for index in FACES:
        plate = cell(pack[b"RECN"][0], index, chars, bpp, colours, per)
        face = plate[:, FACE_FROM:].copy()
        ramp = np.clip(np.linspace(0, FACE_FADE, face.shape[1]), 0, 1)
        face[..., 3] = (face[..., 3] * ramp[None, :] * FACE_ALPHA).astype(np.uint8)
        out.append(face)
    return out


def guest_label(data: Path, lang: str):
    """Mission Mode's guest's row's words ("JUEGO INVITADO"): no cell of entry 3 holds them; they
    are its characters right after those of cell 44's ("SIN DATOS", 8 characters), past two of a
    bar's end: 10 characters, 80 x 8 dots, the words centred in them, in every language (seen in
    the characters the game's list loads)."""
    pack = d2kp(p2_entry(data / "nitrofs" / "UI" / "cm" / f"cmo_{lang}.p2", 3))
    colours, per = palette(pack[b"RLCN"][0])
    chars, bpp = tiles(pack[b"RGCN"][0])
    d = pack[b"RECN"][0]
    kb = 0x10
    records = kb + 8 + struct.unpack_from("<I", d, kb + 0xC)[0]
    record = 16 if struct.unpack_from("<H", d, kb + 0xA)[0] == 1 else 8
    count = struct.unpack_from("<H", d, kb + 8)[0]
    oam = records + count * record
    unit = max(1, (32 << (struct.unpack_from("<I", d, kb + 0x10)[0] & 7)) // (64 if bpp == 8 else 32))
    first = struct.unpack_from("<I", d, records + GUEST_AFTER * record + 4)[0]
    a2 = struct.unpack_from("<3H", d, oam + first)[2]
    start, pal = (a2 & 0x3FF) * unit + GUEST_SKIP, a2 >> 12
    out = np.zeros((8, 8 * GUEST_CHARS, 4), np.uint8)
    for k in range(GUEST_CHARS):
        for i, index in enumerate(chars[start + k]):
            if index != 0 and pal * per + index < len(colours):
                out[i // 8, k * 8 + i % 8] = colours[pal * per + index] + (255,)
    ys, xs = np.nonzero(out[..., 3])
    if len(xs) == 0:
        raise ValueError(f"cmo_{lang}.p2: the guest's row's words are not where the European ROM has them")
    return out[:, xs.min() : xs.max() + 1]


# --- DDD's pieces -----------------------------------------------------------------------------


def ddd_layouts(ddd: Path, lang: str) -> tt.Layouts:
    """The save screen's layouts with their HD textures (each block's at its CTRT's scale)."""
    rel = LAYOUTS.format(lang="en")
    path = ddd / "original" / rel
    b = path.read_bytes()
    saved = tt.tim2_picture
    tt.tim2_picture = lambda data, at: np.zeros((1, 1, 4), dtype=np.uint8)  # a CTRT, not a TIM2
    try:
        layouts = tt.Layouts(path)
    finally:
        tt.tim2_picture = saved
    n, o = struct.unpack_from("<2i", b, 0x20)
    for i, blk in enumerate(layouts.blocks):
        base = o + struct.unpack_from("<I", b, o + 4 * i)[0]
        ctrt = base + struct.unpack_from("<3I", b, base + 0x10)[2]
        if b[ctrt : ctrt + 4] != b"CTRT":
            raise ValueError(f"{path}: block {i} has no CTRT texture")
        width = struct.unpack_from("<H", b, ctrt + 0x20)[0]
        name = TEXTURES[i].format(code=CODES[lang])
        hd = np.asarray(Image.open(ddd / "remastered" / LAYOUTS.format(lang=lang) / name).convert("RGBA"))
        k = hd.shape[1] / width
        blk.texture = hd
        blk.parts = [(int(u0 * k), int(v0 * k), int(u1 * k), int(v1 * k), *c) for (u0, v0, u1, v1, *c) in blk.parts]
    return layouts


def tinted(layouts, p, tint=None):
    """A part's texels in its colour (its corners' mean times its tint), turned as it is."""
    _, block, ua, va, ub, vb = p["region"]
    tex = layouts.blocks[block].texture[va:vb, ua:ub].astype(np.float32)
    c = np.array(p["corners"], np.float32).mean(axis=0) * np.array(p["colour"] if tint is None else tint, np.float32)
    out = np.clip(tex * c, 0, 255).astype(np.uint8)
    fx, fy = p["flip"]
    out = out[:, ::-1] if fx else out
    out = out[::-1] if fy else out
    return np.ascontiguousarray(out)


def greys(a):
    out = a.copy()
    v = a[..., :3].max(axis=2)
    for c in range(3):
        out[..., c] = v
    return out


def paste(canvas: Image.Image, px, x0, y0, x1, y1):
    w, h = int(round(x1 - x0)), int(round(y1 - y0))
    if w > 0 and h > 0:
        canvas.alpha_composite(
            Image.fromarray(px, "RGBA").resize((w, h), Image.LANCZOS), (int(round(x0)), int(round(y0)))
        )


def nodes(layouts, canvas, layout, node, control=SHOWN, colour=greys, at=(0, 0)):
    for p in ts.parts_of(layouts, layout, node, control):
        x0, y0, x1, y1 = p["rect"]
        paste(canvas, colour(tinted(layouts, p)), x0 - at[0], y0 - at[1], x1 - at[0], y1 - at[1])


def screen(layouts) -> np.ndarray:
    canvas = Image.new("RGBA", tt.PICTURE, (0, 0, 0, 255))
    nodes(layouts, canvas, 1, 2)  # the pattern, the list's and the help's panels
    nodes(layouts, canvas, 1, 500)  # the heading's tab
    nodes(layouts, canvas, 5, 210)  # the list's window
    nodes(layouts, canvas, 5, 5555)  # the scrollbar's track
    return np.asarray(canvas)


def row(layouts, selected: bool) -> np.ndarray:
    """A file's row in Days's tones, its frame's middle stretched to the list's width."""
    w, h = LIST[2], LIST[3]
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))

    def mx(x):
        if x <= ROW_RECT[0] + ROW_END:
            return (x - ROW_RECT[0]) * K
        if x >= ROW_RECT[2] - ROW_END:
            return w - (ROW_RECT[2] - x) * K
        middle = ROW_RECT[2] - ROW_RECT[0] - 2 * ROW_END
        return ROW_END * K + (x - ROW_RECT[0] - ROW_END) / middle * (w - 2 * ROW_END * K)

    for p in ts.parts_of(layouts, 4, 2, SELECTED if selected else SHOWN):
        x0, y0, x1, y1 = p["rect"]
        c = tuple(float(v) for v in p["colour"])
        if c[0] > 1.5 and c[1] > 1.5:
            tint = CIRCLE
        elif c[0] > 1.5:
            tint = PINK_RED
        elif c[0] > 0.8 and c[1] < 0.1:
            tint = DARK_PINK_RED
        else:
            tint = c
        paste(canvas, tinted(layouts, p, tint), mx(x0), (y0 - ROW_RECT[1]) * K, mx(x1), (y1 - ROW_RECT[1]) * K)
    return np.asarray(canvas)


def together(layouts, layout, node, control, colour):
    """A node's parts drawn into one picture, and its place."""
    parts = ts.parts_of(layouts, layout, node, control)
    x0 = min(p["rect"][0] for p in parts)
    y0 = min(p["rect"][1] for p in parts)
    x1 = max(p["rect"][2] for p in parts)
    y1 = max(p["rect"][3] for p in parts)
    canvas = Image.new("RGBA", (int(np.ceil(x1 - x0)), int(np.ceil(y1 - y0))), (0, 0, 0, 0))
    for p in parts:
        px = colour(p, tinted(layouts, p))
        rx0, ry0, rx1, ry1 = p["rect"]
        paste(canvas, px, rx0 - x0, ry0 - y0, rx1 - x0, ry1 - y0)
    return np.asarray(canvas), (x0, y0, x1 - x0, y1 - y0)


def button_colour(selected: bool):
    """A question's button: the selected one's red darker, its ring (DDD's yellow) silver; the
    other's black and grey."""

    def colour(p, px):
        r, g, b = p["colour"][:3]
        if r > 0.99 and g > 0.99 and b < 0.01:  # the ring
            k = SILVER / max(r, 1e-6)
            px = px.astype(np.float32)
            v = px[..., :3].max(axis=2)
            for c in range(3):
                px[..., c] = np.clip(v * k, 0, 255)
            return px.astype(np.uint8)
        if selected:
            px = px.astype(np.float32)
            px[..., 0] *= BUTTON_RED_DEPTH
            return np.clip(px, 0, 255).astype(np.uint8)
        return greys(px)

    return colour


def gratext(layouts, name):
    """A word of the gratext texture, cut to its ink, in greys: its place on DDD's own picture, scaled to
    the copy's (a mod may have put a smaller one in its place)."""
    texture = layouts.blocks[1].texture
    ky, kx = texture.shape[0] / GRATEXT_SIZE, texture.shape[1] / GRATEXT_SIZE
    u0, v0, u1, v1 = GRATEXT[name]
    px = texture[int(v0 * ky) : int(v1 * ky), int(u0 * kx) : int(u1 * kx)]
    ys, xs = np.nonzero(px[..., 3] > 8)
    if len(xs) == 0:
        raise ValueError(
            f"DDD's save_gratext picture ({texture.shape[1]} x {texture.shape[0]}) has no {name} where DDD's own "
            "has it: is that picture of your 2.8 copy changed by a mod?"
        )
    return greys(px[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1])


def icon_boxes(sheet: np.ndarray):
    """The icon sheet's icons: per band of rows, each icon's box (x0, y0, x1, y1), left to right."""
    ink = sheet[..., 3] > 8

    def runs(v):
        out, start = [], None
        for i, on in enumerate(v):
            if on and start is None:
                start = i
            if not on and start is not None:
                out.append((start, i))
                start = None
        if start is not None:
            out.append((start, len(v)))
        return out

    bands = []
    for y0, y1 in runs(ink.any(axis=1)):
        bands.append([(x0, y0, x1, y1) for x0, x1 in runs(ink[y0:y1].any(axis=0))])
    return bands


# The icons by band and place in it (read off the sheet, the same in every language): an Xbox
# pad's, a PlayStation's -- the port's names of the pads' inputs (host/app/profile.c) -- and the
# keyboard's keys by SDL scancode.
PAD_ICONS = {
    "xbox": {
        "east": (0, 0),
        "south": (0, 1),
        "north": (0, 2),
        "west": (0, 3),
        "leftshoulder": (1, 0),
        "rightshoulder": (1, 1),
        "lefttrigger": (1, 2),
        "righttrigger": (1, 3),
        "leftstick": (2, 0),
        "rightstick": (2, 1),
        "dpup": (4, 0),
        "dpdown": (4, 1),
        "dpleft": (4, 2),
        "dpright": (4, 3),
        "start": (5, 0),
        "back": (5, 1),
    },
    "playstation": {
        "east": (0, 5),
        "south": (0, 6),
        "north": (0, 7),
        "west": (0, 8),
        "leftshoulder": (1, 5),
        "rightshoulder": (1, 6),
        "lefttrigger": (1, 7),
        "righttrigger": (1, 8),
        "leftstick": (2, 5),
        "rightstick": (2, 6),
        "dpup": (4, 5),
        "dpdown": (4, 6),
        "dpleft": (4, 7),
        "dpright": (4, 8),
        "start": (5, 5),
        "touchpad": (5, 6),
    },
}
KEY_ICONS = {}
for i in range(16):
    KEY_ICONS[4 + i] = (6, i)  # A-P
for i in range(10):
    KEY_ICONS[20 + i] = (7, i)  # Q-Z
for code, i in ((225, 10), (229, 11), (224, 12), (228, 13), (226, 14), (230, 15)):
    KEY_ICONS[code] = (7, i)  # the left and the right shift, control, alt
for i in range(10):
    KEY_ICONS[30 + i] = (8, i)  # 1-9, 0
for code, i in ((82, 10), (81, 11), (80, 12), (79, 13), (40, 14)):
    KEY_ICONS[code] = (8, i)  # up, down, left, right, enter
for i in range(10):
    KEY_ICONS[89 + i] = (9, i)  # the keypad's 1-9, 0
for code, i in ((84, 10), (85, 11), (86, 12), (87, 13), (42, 14), (44, 15)):
    KEY_ICONS[code] = (9, i)  # the keypad's / * - +, backspace, space
for i in range(12):
    KEY_ICONS[58 + i] = (10, i)  # F1-F12
for code, i in ((99, 12), (41, 13), (73, 14), (76, 15)):
    KEY_ICONS[code] = (10, i)  # the keypad's ., escape, insert, delete


def font_glyphs(font: ng.Font):
    """Every glyph of the help font (its private codes aside): its cell's grey cut from the HD
    sheets, its left bearing and its advance (in the sheets' texels)."""
    k, per = font.k, font.cols * font.rows
    cw, ch = int(round(font.cw * k)), int(round(font.ch * k))
    out = []
    for code, idx in sorted(font.map.items()):
        if 0xE000 <= code <= 0xF8FF or code < 0x20:
            continue
        sheet, rest = divmod(idx, per)
        r, c = divmod(rest, font.cols)
        x0, y0 = int(round(c * (font.cw + 1) * k)), int(round(r * (font.ch + 1) * k))
        if sheet >= len(font.sheets):
            continue
        grey = font.sheets[sheet][y0 : y0 + ch, x0 : x0 + cw, 3]
        left, _, advance = font.widths.get(idx, font.default)
        out.append((code, grey, left * k, advance * k))
    return out, ch


def make(ddd: Path, mare: Path, data: Path, lang: str, out_dir: Path, preview: Path | None):
    layouts = ddd_layouts(ddd, lang)
    atlas = tt.Atlas()
    lines = [
        "# The save list in Dream Drop Distance HD's pieces, in Days's tones, made by",
        "# tools/hd/save_screen.py from the player's own copies (host/screens/save_screen.c reads it).",
        "#   piece NAME U V W H         a picture in the atlas",
        "#   place NAME X Y W H         where it goes: in the picture (1920 x 1080), or in a row for",
        "#                              a row's (crown, face, label_*, glove: from its left and middle)",
        "#   rows X Y W H PITCH SHOWN   the first row, the rows' pitch, how many show",
        "#   scroll Y0 Y1               the thumb's top at the list's first row and last",
        "#   text NAME X Y SIZE ALIGN GREY   a word's place (in a row for a row's), size (the",
        "#                              font's scale), left|centre|right, grey",
        "#   pitch NAME N               the lines' pitch of a text of several",
        "#   words NAME WORDS...        DDD's words (empty), the guest's row's (guest)",
        "#   icon NAME U V W H          an input's icon: xbox:<input>, playstation:<input>,",
        "#                              key:<SDL scancode>",
        "#   font FILE CELL TOP BOTTOM  the font's picture, its cells' height, where capitals ink them",
        "#   glyph CODE X Y W LEFT ADVANCE   the font's glyphs (place, width, bearing, advance)",
        f"picture {tt.PICTURE[0]} {tt.PICTURE[1]}",
        "atlas save_atlas.png",
    ]
    pieces, places = [], []

    def piece(name, px, place=None):
        atlas.add((name,), px)
        pieces.append(name)
        if place is not None:
            places.append((name, place))

    piece("screen", screen(layouts), (0, 0, tt.PICTURE[0], tt.PICTURE[1]))
    piece("row", row(layouts, False))
    piece("row_selected", row(layouts, True))
    crown, (cx, cy, cw, ch) = together(layouts, 4, 14, SHOWN, lambda p, px: px)
    piece("crown", crown, ((cx - ROW_RECT[0]) * K, (cy - ROW_RECT[1]) * K, cw * K, ch * K))
    thumb, rect = together(layouts, 5, 5556, SHOWN, lambda p, px: greys(px))
    piece("thumb", thumb, rect)
    for name in ("heading_load", "heading_save"):
        px = gratext(layouts, name)
        x, y, _, h = PLACES["heading"]
        piece(
            name,
            (px.astype(np.float32) * [0.55, 0.55, 0.55, 1.0]).astype(np.uint8),
            (x, y, px.shape[1] * h / px.shape[0], h),
        )
    for name in ("label_munny", "label_time"):
        px = gratext(layouts, name)
        x, y, _, h = PLACES[name]
        piece(name, px, (x, y, px.shape[1] * h / px.shape[0], h))
    # the question's buttons: DDD's pair, its middle under the help's (DDD's is the whole screen's)
    pair = [together(layouts, 7, node, SHOWN, button_colour(False))[1] for node in (2, 3)]
    shift = TEXT["ask"][0] - (pair[0][0] + pair[1][0] + pair[1][2]) / 2
    for selected, name in ((False, "button"), (True, "button_selected")):
        for node, side in ((2, "yes"), (3, "no")):
            px, (x, y, w, h) = together(layouts, 7, node, SELECTED if selected else SHOWN, button_colour(selected))
            piece(f"{name}_{side}", px, (x + shift, y, w, h))
    folder = mare / tt.LANGUAGES[lang][0]
    theatre = tt.Layouts(folder / "l2d" / "days_theatre.l2d")
    launcher = tt.Layouts(folder / "l2d" / "launcher.l2d")
    piece("glove", {name: px for name, px, _ in tc.list_pieces(theatre, launcher, lang)[0]}["glove"], PLACES["glove"])
    label = guest_label(data, lang)
    lw, lh = label.shape[1] * GUEST_SCALE, label.shape[0] * GUEST_SCALE
    piece(
        "guest",
        np.asarray(Image.fromarray(label, "RGBA").resize((lw, lh), Image.NEAREST)),
        (TEXT["empty"][0] - lw / 2, TEXT["empty"][1] - lh / 2, lw, lh),
    )
    face_h = (664 - 548) * K
    for i, face in enumerate(faces(data, lang)):
        w = face.shape[1] * face_h / face.shape[0]
        piece(f"face{i}", face, (LIST[2] - ROW_END * K - w, (548 - ROW_RECT[1]) * K, w, face_h))
    sheet = np.asarray(Image.open(ddd / "remastered" / ICONS.format(code=CODES[lang])).convert("RGBA"))
    bands = icon_boxes(sheet)
    icons = []
    for family, table in PAD_ICONS.items():
        for name, (band, i) in table.items():
            icons.append((f"{family}:{name}", bands[band][i]))
    for code, (band, i) in KEY_ICONS.items():
        icons.append((f"key:{code}", bands[band][i]))
    for name, (x0, y0, x1, y1) in icons:
        atlas.add(("icon", name), sheet[y0:y1, x0:x1])
    pixels = atlas.pack()
    for name in pieces:
        u, v = atlas.at[(name,)]
        h, w = atlas.pixels[(name,)].shape[:2]
        lines.append(f"piece {name} {u} {v} {w} {h}")
    for name, (x, y, w, h) in places:
        lines.append(f"place {name} {x:.1f} {y:.1f} {w:.1f} {h:.1f}")
    lines.append(f"rows {LIST[0]} {LIST[1]} {LIST[2]} {LIST[3]} {ROW_PITCH} {ROWS_SHOWN}")
    track = ts.parts_of(layouts, 5, 5555, SHOWN)
    top = rect[1]
    bottom = max(p["rect"][3] for p in track) - (top - min(p["rect"][1] for p in track)) - rect[3]
    lines.append(f"scroll {top:.1f} {bottom:.1f}")
    for name, (x, y, size, align, grey) in TEXT.items():
        lines.append(f"text {name} {x:.1f} {y:.1f} {size} {align} {grey}")
    for name, pitch in LINE_PITCH.items():
        lines.append(f"pitch {name} {pitch}")
    words = ng.ctd(ddd / "original" / WORDS.format(lang=lang))
    lines.append(f"words empty {ng.plain(words[EMPTY])}")
    lines.append(f"words guest {GUEST_WORDS[lang]}")
    for name, (x0, y0, x1, y1) in icons:
        u, v = atlas.at[("icon", name)]
        lines.append(f"icon {name} {u} {v} {x1 - x0} {y1 - y0}")
    font = ng.Font(ddd / "original" / ng.FONT, sorted((ddd / "remastered" / ng.FONT).glob("US_*.png")))
    glyphs, cell_h = font_glyphs(font)
    cols = 32
    cell_w = max(g[1].shape[1] for g in glyphs)
    rows_n = (len(glyphs) + cols - 1) // cols
    font_px = np.zeros((rows_n * cell_h, cols * cell_w), np.uint8)
    # where capitals and digits ink a cell: the words are placed by their ink, as the mock-up did
    ink = [np.nonzero(g[1].max(axis=1) > 16)[0] for g in glyphs if chr(g[0]).isupper() or chr(g[0]).isdigit()]
    ink_top = min(int(i.min()) for i in ink if len(i))
    ink_bottom = max(int(i.max()) + 1 for i in ink if len(i))
    lines.append(f"font save_font.png {cell_h} {ink_top} {ink_bottom}")
    for n, (code, grey, left, advance) in enumerate(glyphs):
        gx, gy = (n % cols) * cell_w, (n // cols) * cell_h
        font_px[gy : gy + grey.shape[0], gx : gx + grey.shape[1]] = grey
        lines.append(f"glyph {code} {gx} {gy} {grey.shape[1]} {left:.2f} {advance:.2f}")
    out_dir.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels, "RGBA").save(out_dir / "save_atlas.png")
    Image.fromarray(font_px, "L").save(out_dir / "save_font.png")
    (out_dir / "save.txt").write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    print(
        f"{lang}: {len(pieces)} pieces, {len(icons)} icons, {len(glyphs)} glyphs; "
        f"atlas {pixels.shape[1]} x {pixels.shape[0]}, font {font_px.shape[1]} x {font_px.shape[0]}"
    )
    if preview is not None:
        preview.mkdir(parents=True, exist_ok=True)
        draw_preview(pixels, atlas, places, font, preview / f"{lang}.png")


def draw_preview(pixels, atlas, places, font, path: Path):
    """The list with three made-up rows, as the port lays it out."""

    def px(name):
        u, v = atlas.at[(name,)]
        h, w = atlas.pixels[(name,)].shape[:2]
        return pixels[v : v + h, u : u + w]

    place = dict(places)
    canvas = Image.new("RGBA", tt.PICTURE, (0, 0, 0, 255))
    paste(canvas, px("screen"), 0, 0, *tt.PICTURE)
    x, y, w, h = place["heading_load"]
    paste(canvas, px("heading_load"), x, y, x + w, y + h)
    x, y, w, h = place["thumb"]
    paste(canvas, px("thumb"), x, y, x + w, y + h)
    for i in range(ROWS_SHOWN):
        top = LIST[1] + i * ROW_PITCH
        paste(canvas, px("row_selected" if i == 0 else "row"), LIST[0], top, LIST[0] + LIST[2], top + LIST[3])
        tx, ty, size, align, grey = TEXT["number"]
        im = font.line(f"{i + 1:02d}", size, (grey,) * 3)
        canvas.alpha_composite(
            Image.fromarray(im, "RGBA"), (int(LIST[0] + tx - im.shape[1] / 2), int(top + ty - im.shape[0] / 2))
        )
        if i < 3:
            for name in (f"face{i}", "label_munny", "label_time"):
                x, y, w, h = place[name]
                paste(canvas, px(name), LIST[0] + x, top + y, LIST[0] + x + w, top + y + h)
        if i == 0:
            x, _, w, h = place["glove"]
            y = LIST[3] / 2 - h / 2
            paste(canvas, px("glove"), LIST[0] + x, top + y, LIST[0] + x + w, top + y + h)
    canvas.convert("RGB").save(path)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("ddd", type=Path, help="Dream Drop Distance extracted: <2.8>/Image/dt/kh3d_first.hed_out")
    ap.add_argument("mare", type=Path, help="the theater extracted: <collection>/Image/Mare.hed_out/original")
    ap.add_argument("--data", type=Path, help="the port's extracted game data (data/extracted/<hash>)")
    ap.add_argument("--mod", default="Theater title")
    ap.add_argument("--lang", nargs="*", default=list(LANGUAGES), choices=list(LANGUAGES))
    ap.add_argument("--preview", type=Path, help="also draw a list with made-up rows into DIR/<lang>.png")
    args = ap.parse_args()
    data = args.data
    if data is None:
        found = sorted((tt.ROOT / "data" / "extracted").glob("*/nitrofs"))
        if not found:
            ap.error("no extracted game data in data/extracted: give --data")
        data = found[0].parent
    for lang in tt.skip_missing_languages(data, args.lang):
        make(args.ddd, args.mare, data, lang, tt.ROOT / "mods" / args.mod / "layouts" / "save" / lang, args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
