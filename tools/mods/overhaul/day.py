"""The day's card (layouts/day/<lang>/day.txt and day_atlas.png): the 358/2 Days theater's card for a chapter's day
(days_theatre.l2d's layout 100: DAY, the day's digits rolling in, the line and its ends opening, the heart falling
as it turns, the day's title fading in), drawn by host/screens/day_screen.c over the game's calendar card (ov004,
runtime/menus/daycard.c) for every day the game shows.

Made from the theater's Mare (the layout and its texture, the menu font, the chapters and their names in
ct00100.ctd) and the game's own data (the titled days of the calendar overlay, their titles in UI/cal/ttl_<lang>).
Each node's keys are written from its own control's start, frame by frame at 60 a second, in the window's 1920 x
1080; the port starts the controls when the movie does (runtime/menus/daycard.h, measured frame by frame on a
1080p60 capture of the collection's movie). day.txt's lines, as day_screen.c reads them:
    piece NAME X Y W H                  a piece in the atlas: day, line, centre, end, digit0-9, heart0-7, title_d<day>
    length N                            the controls' last frame
    element I NAME                      a drawn element (heart takes its frame's heart<K>)
    key I F X Y W H ALPHA FX FY K       element I at frame F: its rect, alpha, turned over (x, y), sprite frame
    slot S X Y W H                      a digit's place at rest (0 the ones, 1 the tens, 2 the hundreds)
    roll C F DY ALPHA                   a digit's roll by the layout's control C (1, 2, 4, 5) at frame F
    title_at X Y TOP                    the title's ink's middle, its capitals' top, and their top in its pieces
    title_alpha F A                     the title's alpha at frame F
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "hd"))  # tools/hd: the theater's readers
from common import LANGS, Inputs  # noqa: E402
import theater_cine as tc  # noqa: E402
import theater_title as tt  # noqa: E402
from newgame_dialog import ctd, plain  # noqa: E402

LAYOUT = 100
LENGTH = 144  # the controls' max frame (once: the last one held)
LINE_NODE, HEART_NODE, TITLE_NODE, DAY_NODE = 120, 130, 140, 150
DIGIT_NODES = (151, 152, 153)  # the ones', the tens', the hundreds'
# A digit's controls: 1 a new one rising from below, 2 the old one rising away, 4 the old one falling away, 5 a new
# one falling in from above (their keys move from frame 8 to 50).
ROLLS = (1, 2, 4, 5)
ROLL_LENGTH = 50
DIGIT_SPRITE, HEART_SPRITE = 84, 85  # animated sprites: frame k is the part moved by k parts across, 8 a row
# The drawn elements: (piece, node, its sprite animation's index in the node's control). The line's two ends are
# the same sprite, the second turned half round.
ELEMENTS = (
    ("line", LINE_NODE, 0),
    ("centre", LINE_NODE, 1),
    ("end", LINE_NODE, 2),
    ("end", LINE_NODE, 3),
    ("heart", HEART_NODE, 0),
    ("day", DAY_NODE, 0),
)
TITLES = 68  # the DS's titled days (UI/cal/ttl_<lang>, data_ov004_020510cc)
# The theater's chapter for a DS title whose words differ (the DS's own numerals): its index -> the chapter's day.
TITLE_CHAPTER_DAYS = {1: 7}
CALENDAR_OVERLAY = "_unnamed/file_00004.bin"  # ov004 (the calendar), BLZ-compressed as the ROM keeps it
PROLOGUE = 400  # the prologue's day in the DS's list (its card shows 255)
# Measured on a capture of the theater's day 10 (1920 x 1080, "Incomplete"): the layout's unit in the picture's
# texels and its middle (DAY, the digits and the line fall on the capture's to the texel); the title, in the menu
# font, its capitals 22 texels high, its letters 0.7 closer than the font's advances, its ink centred 1 texel left
# of the title node's place and its capitals' top 9 below it.
UNIT = 2.0
MIDDLE = (960.0, 540.0)
TITLE_CAP = 22.0
TITLE_TRACKING = -0.7
TITLE_DX = -1.0
TITLE_CAP_BELOW = 9.0
TITLE_PAD = 4  # the title's picture's margin around its glyphs' cells, before it is cut to its ink


# --- The layout's animation, evaluated -------------------------------------------------------------------------------


def eased(t: float, data: bytes) -> float:
    """How far a key's value has come `t` of the way to it. A spline key (its data's first byte 0x40-0x42, OpenKh's
    TypeSpline) arrives at rest, the segment leaving the key before at its data's second byte's speed (in percent of
    the cubic ease-out's): 0 a smoothstep (the line's ends, the digits), 100 1 - (1 - t)^3 (the heart's fall), as
    the movie shows them to a texel; other keys come straight."""
    if data[0] & 0xC0 != 0x40:
        return t
    m = 3.0 * min(data[1], 100) / 100.0
    return m * (t**3 - 2 * t * t + t) + 3 * t * t - 2 * t**3


def key_at(keys, kind, frame):
    """A kind's value at `frame` (theater_title.key_at's, the spline keys eased by the arriving key's data): a status
    key holds from its own frame on, a point key (0xc0) holds until the next."""

    def value(k):
        if kind == "color":
            c = k[1] & 0xFFFFFFFF
            return np.array([(c >> s) & 255 for s in (0, 8, 16, 24)], dtype=np.float64)
        if kind == "status":
            return k[2]
        return tt.f32(k[1])

    if frame <= keys[0][0]:
        return value(keys[0])
    for a, c in zip(keys, keys[1:]):
        if a[0] <= frame <= c[0]:
            if kind == "status":
                return value(c if frame == c[0] else a)
            if c[0] == a[0] or (a[2][0] & 0xC0) == 0xC0:
                return value(a)
            t = eased((frame - a[0]) / (c[0] - a[0]), c[2])
            return value(a) + (value(c) - value(a)) * t
    return value(keys[-1])


class Card:
    """Layout 100 of a language's days_theatre.l2d: its nodes' places, sprites and keys at a frame."""

    def __init__(self, l2d: Path):
        self.layouts = tt.Layouts(l2d)
        self.block = self.layouts.blocks[0]
        self.nodes = self.layouts.layout_nodes(LAYOUT)

    def node(self, nid):
        return next(n for n in self.nodes if n[12] == nid)

    @staticmethod
    def values(anim, f):
        """An animation's values at frame f. A sprite turned half round is drawn turned over both ways, its offset
        turned with it (the line's second end opens from the middle as the first)."""
        v = dict(tt.DEFAULT)
        v["color"] = np.array([128.0, 128.0, 128.0, 255.0])
        v["status"] = None
        for kind, keys in anim["keys"].items():
            v[kind] = key_at(keys, kind, f)
        if abs(v["rotate"] - math.pi) < 1e-3:
            v["scale_x"], v["scale_y"], v["rotate"] = -v["scale_x"], -v["scale_y"], 0.0
            v["offset_x"], v["offset_y"] = -v["offset_x"], -v["offset_y"]
        elif abs(v["rotate"]) > 1e-3:
            raise ValueError("a turned sprite in the day's card: the port draws its pieces upright or turned over")
        return v

    def control(self, nid, control):
        c = self.layouts.control(self.node(nid), control)
        return c, self.block.anims[c[2] : c[2] + c[4]]

    @staticmethod
    def frame(c, f):
        cmax, cret, _, once, _ = c
        return f if cmax <= 0 or f <= cmax else cmax if once else cret + (f - cret) % max(1, cmax - cret)

    def place(self, nid, f, controls=None):
        """The node's place in layout units (its parents' moves added), its alpha and its animations at frame f."""
        controls = controls or {}
        chain, n = [], self.node(nid)
        while True:
            chain.append(n)
            if n[9] < 0:
                break
            n = self.nodes[n[9]]
        x = y = 0.0
        alpha = 1.0
        for n in reversed(chain):
            x, y = x + n[10], y + n[11]
            c, anims = self.control(n[12], controls.get(n[12], 0))
            fr = self.frame(c, f)
            for a in anims:
                if a["sprite"] < 0 and a["kind"] == 0:
                    v = self.values(a, fr)
                    x += v["base_x"] + v["offset_x"]
                    y += v["base_y"] + v["offset_y"]
                    alpha *= v["color"][3] / 255.0
        c, anims = self.control(nid, controls.get(nid, 0))
        return x, y, alpha, anims, self.frame(c, f)

    def part(self, sprite, frame=(0, 0)):
        """A sprite's one part: its box in layout units and its texels (the frame's: moved by whole parts across,
        then down)."""
        gc, gi = self.block.sprites[sprite]
        if gc != 1:
            raise ValueError(f"sprite {sprite}: {gc} parts (one is drawn)")
        x0, y0, x1, y1, p, _ = self.block.groups[gi]
        u0, v0, u1, v1, *_ = self.block.parts[p]
        w, h = abs(u1 - u0), abs(v1 - v0)
        ua, va = min(u0, u1) + frame[0] * w, min(v0, v1) + frame[1] * h
        texels = self.block.texture[va : va + max(h, 1), ua : ua + max(w, 1)]
        return (x0, y0, x1, y1), texels

    def piece(self, nid, sprite_index, f):
        """One sprite of a node at frame f: its rect in the picture, alpha, turned over (x, y), sprite frame."""
        x, y, alpha, anims, fr = self.place(nid, f)
        a = [a for a in anims if a["sprite"] >= 0][sprite_index]
        v = self.values(a, fr)
        st = v["status"]
        k = (st[1] + 8 * st[3]) if st is not None and st[0] == 1 else 0
        (x0, y0, x1, y1), _ = self.part(a["sprite"])
        sx, sy = v["scale_x"], v["scale_y"]
        qx0, qx1 = sorted((x0 * sx, x1 * sx))
        qy0, qy1 = sorted((y0 * sy, y1 * sy))
        tx, ty = x + v["base_x"] + v["offset_x"], y + v["base_y"] + v["offset_y"]
        c = v["color"]
        return (
            MIDDLE[0] + (tx + qx0) * UNIT,
            MIDDLE[1] + (ty + qy0) * UNIT,
            (qx1 - qx0) * UNIT,
            (qy1 - qy0) * UNIT,
            alpha * c[3] / 255.0,
            int(sx < 0),
            int(sy < 0),
            k,
        )

    def colour_alpha(self, nid, f):
        """A node's own colour animation's alpha (kind 2: what the program draws there)."""
        _, _, alpha, anims, fr = self.place(nid, f)
        for a in anims:
            if a["sprite"] < 0 and a["kind"] == 2 and "color" in a["keys"]:
                return alpha * self.values(a, fr)["color"][3] / 255.0
        return alpha


# --- The titles ------------------------------------------------------------------------------------------------------


def ds_titles(inputs: Inputs, lang: str) -> list[str]:
    """UI/cal/ttl_<lang>: u32 its header's size, u32 the count, then each record's u32 length (these 4 bytes in it)
    and its UTF-16LE text."""
    b = inputs.ds(f"UI/cal/ttl_{lang}.z")
    at, n = struct.unpack_from("<II", b, 0)
    out = []
    for _ in range(n):
        length = struct.unpack_from("<I", b, at)[0]
        out.append(b[at + 4 : at + length].decode("utf-16le").split("\0")[0])
        at += length
    return out


def blz(data: bytes) -> bytes:
    """An overlay as the DS's loader unpacks it (the backward LZ of its footer: the header's and the packed part's
    lengths, then how much it grows)."""
    top, grow = struct.unpack_from("<II", data, len(data) - 8)
    header, packed = top >> 24, top & 0xFFFFFF
    out = bytearray(data) + bytearray(grow)
    src, dst, end = len(data) - header, len(out), len(data) - packed
    while src > end:
        src -= 1
        flags = data[src]
        for _ in range(8):
            if src <= end:
                break
            if flags & 0x80:
                src -= 2
                v = data[src] | data[src + 1] << 8
                for _ in range((v >> 12) + 3):
                    dst -= 1
                    out[dst] = out[dst + (v & 0xFFF) + 3]
            else:
                src -= 1
                dst -= 1
                out[dst] = data[src]
            flags = (flags << 1) & 0xFF
    return bytes(out)


def ds_title_days(inputs: Inputs) -> list[int]:
    """The days the DS's card has a title for, in UI/cal/ttl's order (data_ov004_020510cc: 68 ints rising from 0 to
    the prologue's 400, found in the unpacked overlay)."""
    b = blz(inputs.ds(CALENDAR_OVERLAY))
    for at in range(0, len(b) - 4 * TITLES + 1, 4):
        v = struct.unpack_from(f"<{TITLES}i", b, at)
        if v[0] == 0 and v[-1] == PROLOGUE and all(a < c for a, c in zip(v, v[1:])):
            return list(v)
    raise ValueError(f"{CALENDAR_OVERLAY}: the calendar's titled days not found")


def titles(inputs: Inputs, lang: str, days: list[int], english: list[str]) -> dict[int, str]:
    """The card's title by day (the day the game's card names: 400 the prologue's) in `lang`, as the theater has
    them: the DS's titled days (`days`, their English titles `english`), each in the theater's words for the chapter
    with the same English title (or TITLE_CHAPTER_DAYS's), else the game's own; a theater chapter's title moved to
    the chapter's day when the theater places it on a day the DS names none for (the DS's day 28 "Sleep" is the
    theater's day 49), and a day the theater names otherwise (193) takes the theater's."""
    mare = inputs.mare
    folder = tt.LANGUAGES[lang][0]
    own = ds_titles(inputs, lang)
    uk_table = ctd(mare / "uk" / "ctd" / "ct00100.ctd")
    table = ctd(mare / folder / "ctd" / "ct00100.ctd")
    chapters = [c for c in tc.theater_chapters(mare) if c["title"] in uk_table]
    out, keys = {}, {}
    for i in range(min(TITLES, len(own), len(days))):
        key = None
        if i in TITLE_CHAPTER_DAYS:
            key = next((c["title"] for c in chapters if c["day"] == TITLE_CHAPTER_DAYS[i]), None)
        elif i < len(english):
            key = next((c["title"] for c in chapters if plain(uk_table[c["title"]]) == english[i]), None)
        text = plain(table[key]) if key is not None and key in table else own[i]
        out[days[i]] = " ".join(text.split())
        keys[days[i]] = key
    used = {k for k in keys.values() if k is not None}
    for c in chapters:
        day, key = c["day"], c["title"]
        if key not in table or day == 0:
            continue
        text = " ".join(plain(table[key]).split())
        if key in used:
            ds_day = next(d for d, k in keys.items() if k == key)
            if ds_day != day and day not in out and ds_day != PROLOGUE:
                out[day] = out.pop(ds_day)  # the theater's place for it
                keys[day] = keys.pop(ds_day)
        elif day in out and keys.get(day) is None:
            out[day] = text  # the theater's own name for the DS's day
            keys[day] = key
        elif day not in out:
            out[day] = text
            keys[day] = key
    return out


def title_line(font: tt.Font, text: str, missing: set) -> tuple[np.ndarray, float]:
    """The title in the theater's menu font, white, its capitals TITLE_CAP high: RGBA cut to its ink (two columns
    either side), and its capitals' top in it. Letters the font has not are left out (into `missing`)."""
    k = TITLE_CAP / font.cap_height
    cell = int(math.ceil(32 * k))

    def advance(c):
        return (tt.SPACE_PER_CAP * TITLE_CAP if c == " " else font.glyphs[c][2] * k) + TITLE_TRACKING

    missing.update(c for c in text if c != " " and c not in font.glyphs)
    text = "".join(c for c in text if c == " " or c in font.glyphs)
    width = int(math.ceil(sum(advance(c) for c in text))) + 2 * TITLE_PAD + 8
    mask = np.zeros((cell + 2 * TITLE_PAD, width), np.float32)
    x = float(TITLE_PAD)
    for c in text:
        if c != " ":
            gx, gy, _ = font.glyphs[c]
            ink = font.ink[c]
            glyph = Image.fromarray(font.picture[gy : gy + 32, gx : gx + ink, 3])
            a = np.asarray(glyph.resize((max(1, round(ink * k)), cell), Image.LANCZOS)).astype(np.float32)
            at = int(math.floor(x))
            frac = x - at  # placed between texels: its columns shared by the two
            h, w = a.shape
            moved = np.zeros((h, w + 1), np.float32)
            moved[:, :w] += a * (1 - frac)
            moved[:, 1:] += a * frac
            region = mask[TITLE_PAD : TITLE_PAD + h, at : at + w + 1]
            np.maximum(region, moved[:, : region.shape[1]], out=region)
        x += advance(c)
    a = np.clip(mask / 255.0, 0, 1)
    cols = np.nonzero(a.max(0) > 0.02)[0]
    if len(cols) == 0:
        return np.zeros((1, 1, 4), np.uint8), 0.0
    a = a[:, max(0, cols[0] - 2) : cols[-1] + 3]
    out = np.zeros(a.shape + (4,), np.uint8)
    out[..., :3] = 255
    out[..., 3] = (a * 255).astype(np.uint8)
    return out, TITLE_PAD + font.cap_top * k


# --- Making the files ------------------------------------------------------------------------------------------------


def make_language(inputs: Inputs, lang: str, days: list[int], english: list[str], out: Path) -> None:
    """One language's day.txt and day_atlas.png into `out`."""
    folder = tt.LANGUAGES[lang][0]
    card = Card(inputs.mare / folder / "l2d" / "days_theatre.l2d")
    font = tt.Font(inputs.mare / folder / "font")
    atlas = tt.Atlas()
    names = {}

    def add(name, texels):
        region = ("day", name)
        atlas.add(region, np.ascontiguousarray(texels))
        names[name] = region

    # the pieces: DAY, the line, its middle and ends, the digits, the heart's frames, the titles
    line_anims = [a for a in card.control(LINE_NODE, 0)[1] if a["sprite"] >= 0]
    for name, sprite in (
        ("day", card.control(DAY_NODE, 0)[1][1]["sprite"]),
        ("line", line_anims[0]["sprite"]),
        ("centre", line_anims[1]["sprite"]),
        ("end", line_anims[2]["sprite"]),
    ):
        add(name, card.part(sprite)[1])
    for d in range(10):
        add(f"digit{d}", card.part(DIGIT_SPRITE, (d % 8, d // 8))[1])
    for k in range(8):
        add(f"heart{k}", card.part(HEART_SPRITE, (k, 0))[1])
    missing: set = set()
    title_top = 0.0
    for day, text in sorted(titles(inputs, lang, days, english).items()):
        if text:
            picture, title_top = title_line(font, text, missing)
            add(f"title_d{day}", picture)
    if missing:
        print(f"day: {lang}: the menu font has no {''.join(sorted(missing))!r} (left out)")
    pixels = atlas.pack()

    lines = [
        f"# The day's card as the 358/2 Days theater draws a chapter's day (layout {LAYOUT} of",
        f"# {folder}/l2d/days_theatre.l2d), made by tools/mods/overhaul/day.py from the player's own collection.",
        "atlas day_atlas.png",
    ]
    for name, region in names.items():
        x, y = atlas.at[region]
        h, w = atlas.pixels[region].shape[:2]
        lines.append(f"piece {name} {x} {y} {w} {h}")
    lines.append(f"length {LENGTH}")
    # each drawn element at each frame of its node's control
    for index, (name, nid, sprite_index) in enumerate(ELEMENTS):
        lines.append(f"element {index} {name}")
        for f in range(LENGTH + 1):
            x, y, w, h, a, fx, fy, k = card.piece(nid, sprite_index, f)
            lines.append(f"key {index} {f} {x:.2f} {y:.2f} {w:.2f} {h:.2f} {a:.3f} {fx} {fy} {k}")
    # the digits' places (the ones' first) and their rolls
    (x0, y0, x1, y1), _ = card.part(DIGIT_SPRITE)
    for slot, nid in enumerate(DIGIT_NODES):
        x, y, _, _, _ = card.place(nid, 0, {nid: 0})
        lines.append(
            f"slot {slot} {MIDDLE[0] + (x + x0) * UNIT:.2f} {MIDDLE[1] + (y + y0) * UNIT:.2f} "
            f"{(x1 - x0) * UNIT:.2f} {(y1 - y0) * UNIT:.2f}"
        )
    nid = DIGIT_NODES[0]
    _, rest_y, _, _, _ = card.place(nid, 0, {nid: 0})
    for control in ROLLS:
        for f in range(ROLL_LENGTH + 1):
            _, y, a, _, _ = card.place(nid, f, {nid: control})
            lines.append(f"roll {control} {f} {(y - rest_y) * UNIT:.2f} {a:.3f}")
    # the title: its ink's middle and its capitals' top (from the title node's place), where its capitals' top is
    # in its pieces, and its alpha
    tx, ty, _, _, _ = card.place(TITLE_NODE, 0)
    lines.append(
        f"title_at {MIDDLE[0] + tx * UNIT + TITLE_DX:.2f} {MIDDLE[1] + ty * UNIT + TITLE_CAP_BELOW:.2f} {title_top:.2f}"
    )
    for f in range(LENGTH + 1):
        lines.append(f"title_alpha {f} {card.colour_alpha(TITLE_NODE, f):.3f}")

    out.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels, "RGBA").save(out / "day_atlas.png")
    (out / "day.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\r\n")  # CRLF, as shipped


def make(inputs: Inputs, mod: Path) -> None:
    """The day's card in the game's five languages: mod/layouts/day/<lang>/."""
    days = ds_title_days(inputs)
    english = ds_titles(inputs, "en")
    for lang in LANGS:
        make_language(inputs, lang, days, english, mod / "layouts" / "day" / lang)
