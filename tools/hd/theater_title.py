#!/usr/bin/env python3
"""Make the title's animated pictures from the 358/2 Days theater's main menu, from your own copy of the
HD 1.5+2.5 collection (MARE: docs/HD_SCREENS.md, "Making them"), into mods/NAME/layouts/title/<lang>/.
  python tools/hd/theater_title.py MARE [--mod NAME] [--lang es|en|fr|de|it ...] [--preview DIR CLIP FRAME]
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]

# The theater's language folders, and each language's options as the game's own option sprites
# write them (ttl_<lang>.p2 entry 1, English's ttl.p2 entry 2, transcribed): the title's
# first level, its story level, its mission level (ov000's Ov000_RefreshMenuLayout).
LANGUAGES = {
    "es": (
        "sp",
        {
            "root": ["MODO HISTORIA", "MODO MISIÓN", "MODO CINE"],
            "story": ["NUEVA PARTIDA", "CARGAR"],
            "mission": ["UN JUGADOR", "MULTIJUGADOR"],
        },
    ),
    "en": (
        "uk",
        {
            "root": ["STORY MODE", "MISSION MODE", "THEATER MODE"],
            "story": ["NEW GAME", "CONTINUE"],
            "mission": ["SOLO", "MULTIPLAYER"],
        },
    ),
    "fr": (
        "fr",
        {
            "root": ["MODE HISTOIRE", "MODE MISSIONS", "MODE THÉÂTRE"],
            "story": ["NOUVELLE PARTIE", "CONTINUER"],
            "mission": ["SOLO", "MULTIJOUEUR"],
        },
    ),
    "de": (
        "gr",
        {
            "root": ["STORY-MODUS", "MISSIONSMODUS", "KINO-MODUS"],
            "story": ["NEUES SPIEL", "FORTSETZEN"],
            "mission": ["SOLO", "MEHRSPIELER"],
        },
    ),
    "it": (
        "it",
        {
            "root": ["STORIA", "MISSIONI", "TEATRO"],
            "story": ["NUOVA PARTITA", "CONTINUA"],
            "mission": ["UN GIOCATORE", "MULTIGIOCATORE"],
        },
    ),
}


def cart_languages(data: Path, wanted) -> list[str]:
    """The languages of `wanted` the cart has text for: a European cart all five, a North American
    one English and French (each has its theater strings, UI/thr/thr_<lang>.s.z)."""
    return [lang for lang in wanted if (data / "nitrofs" / "UI" / "thr" / f"thr_{lang}.s.z").is_file()]


def skip_missing_languages(data: Path, wanted) -> list[str]:
    """`wanted` less the languages the cart lacks, each one left out said."""
    have = cart_languages(data, wanted)
    for lang in wanted:
        if lang not in have:
            print(f"{lang}: not on this cart, left out")
    return have


# Layout 113, the theater's main menu: its option nodes top to bottom with their word parts, the nodes
# of the entries under its options, and the controls that draw an option selected (0) and not (3); its
# groups appear with control 1 and stand still with 0 (docs/HD_SCREENS.md, "The title").
MENU_LAYOUT = 113
# The theater's boot screens, in its order (seen drawn): Disney, SQUARE ENIX, the
# copyright card; each node 100's control 1 fades its logo over 275 frames, its control 0 holds it.
LOGO_LAYOUTS = (130, 140, 150)
OPTION_NODES = [104, 107, 108, 111]
OPTION_WORD_PARTS = [(110, 111, 112), (113, 114, 115), (116, 117, 118), (119, 120, 121)]
LEFT_OUT_NODES = {105, 106, 109, 110}
# The settings key's node (its bar, its words, its key and gear in one part: the key's columns
# end where the gear's blue begins) and the port's action it stands for (host/app/profile.c).
SETTINGS_NODE, SETTINGS_ACTION = 0, "open_settings"
SELECTED, NOT_SELECTED = 0, 3
STILL, APPEAR = 0, 1
PICTURE = (1920, 1080)  # the layout's 960 x 540 units, two texels each
GROUND = (1.0, 1.0, 1.0)  # white behind the menu, as the theater's
# REPRODUCIR in its word part (632 x 76 texels, two to a layout unit): it starts 79 texels in, its
# capitals 43 high from row 19, its letters 3.2 texels further apart than the font's.
WORD_LEFT, WORD_CAP_TOP, WORD_CAP_HEIGHT, WORD_HEIGHT, WORD_TRACKING = 79, 19, 43, 76, 3.2
WORD_GREYS = {"black": 186, "white": 200}  # the word's grey in each column of the texture
# The font has no space (its U+3000 says a width of 255): a word's space is 0.36 of a capital
# beyond the letters' spacing, as "AJUSTES DEL JUEGO" in the theater's texture has its (12-13
# texels between the words, capitals 35 high).
SPACE_PER_CAP = 12.5 / 35

# The theater's button icons (<lang dir>/font/Icon.tm2, cells of 52 x 52 texels unless a width is
# given; their places read from its FontIcon.inf and each one checked by drawing it with its
# name) by the port's gamepad families and input names (host/app/profile.c pad_names).
# The collection has none for the Guide button.
ICONS = {
    "xbox": {
        "east": (2, 11),
        "south": (56, 11),
        "north": (110, 11),
        "west": (164, 11),
        "leftshoulder": (6, 77),
        "rightshoulder": (74, 77),
        "lefttrigger": (142, 77),
        "righttrigger": (204, 77),
        "leftstick": (6, 143),
        "rightstick": (68, 143),
        "dpup": (6, 275),
        "dpdown": (68, 275),
        "dpleft": (130, 275),
        "dpright": (192, 275),
        "start": (2, 341),
        "back": (70, 341),
    },
    "playstation": {
        "east": (342, 11),
        "south": (396, 11),
        "north": (450, 11),
        "west": (504, 11),
        "leftshoulder": (346, 77),
        "rightshoulder": (414, 77),
        "lefttrigger": (482, 77),
        "righttrigger": (544, 77),
        "leftstick": (346, 143),
        "rightstick": (408, 143),
        "dpup": (346, 275),
        "dpdown": (408, 275),
        "dpleft": (470, 275),
        "dpright": (532, 275),
        "start": (342, 341, 68),
        "touchpad": (417, 341, 58),
    },
    "pad": {
        "east": (682, 11),
        "south": (736, 11),
        "north": (790, 11),
        "west": (844, 11),
        "leftshoulder": (686, 77),
        "rightshoulder": (754, 77),
        "lefttrigger": (822, 77),
        "righttrigger": (884, 77),
        "leftstick": (686, 143),
        "rightstick": (748, 143),
    },
}

KINDS = [
    "status",
    "base_x",
    "base_y",
    "k3",
    "offset_x",
    "offset_y",
    "k6",
    "rotate",
    "k8",
    "scale_x",
    "scale_y",
    "color",
]
DEFAULT = {
    "base_x": 0.0,
    "base_y": 0.0,
    "k3": 0.0,
    "offset_x": 0.0,
    "offset_y": 0.0,
    "k6": 0.0,
    "rotate": 0.0,
    "k8": 0.0,
    "scale_x": 1.0,
    "scale_y": 1.0,
}
BLENDS = {0: "over", 1: "add", 2: "take"}


# --- TIM2 ---------------------------------------------------------------------------------------


def tim2_picture(b: bytes, at: int) -> np.ndarray:
    """The first picture of the TIM2 at `at`, as RGBA (the PS2's alpha, 0x80 = opaque, made 255):
    32-bit texels (image type 3), 24-bit ones (type 2: opaque; some of the theater's artworks), or
    8-bit indices (type 5) into the 32-bit colours after them, in the PS2's CSM1 order (clut type
    bit 7 clear: each 32 colours' second and third eight swapped)."""
    p = at + 16
    total, clut_size, image_size, header_size, clut_colours, fmt, mipmaps, clut_type, image_type, w, h = (
        struct.unpack_from("<IIIHHBBBBHH", b, p)
    )
    image = p + header_size
    if image_type == 3:
        px = np.frombuffer(b, dtype=np.uint8, count=w * h * 4, offset=image).reshape(h, w, 4).copy()
    elif image_type == 2:
        rgb = np.frombuffer(b, dtype=np.uint8, count=w * h * 3, offset=image).reshape(h, w, 3)
        px = np.dstack([rgb, np.full((h, w), 0x80, np.uint8)])
    elif image_type == 5 and clut_type & 0x3F == 3 and clut_colours == 256:
        clut = np.frombuffer(b, dtype=np.uint8, count=256 * 4, offset=image + image_size).reshape(256, 4)
        if not clut_type & 0x80:
            order = np.arange(256)
            clut = clut[(order & 0xE7) | ((order & 0x10) >> 1) | ((order & 0x08) << 1)]
        px = clut[np.frombuffer(b, dtype=np.uint8, count=w * h, offset=image).reshape(h, w)].copy()
    else:
        raise ValueError(
            f"a TIM2 picture of type {image_type}, colours of type {clut_type} (32-bit texels "
            f"and 8-bit indices into 256 32-bit colours are read)"
        )
    px[..., 3] = np.minimum(255, px[..., 3].astype(np.int32) * 2).astype(np.uint8)
    return px


# --- L2D ----------------------------------------------------------------------------------------


def f32(i: int) -> float:
    return struct.unpack("<f", struct.pack("<i", i))[0]


class Block:
    """One SQ2P: its sprites (SP2: parts, groups, sprites), its sequences (SQ2) and its texture."""

    def __init__(self, b: bytes, base: int):
        sp2_o, sq2_o, tm2_o = struct.unpack_from("<3I", b, base + 0x10)
        sp = base + sp2_o
        pc, po, gc, go, sc, so = struct.unpack_from("<6i", b, sp + 0x10)
        self.parts = [struct.unpack_from("<4h4I", b, sp + po + 24 * i) for i in range(pc)]
        self.groups = [struct.unpack_from("<4hHH", b, sp + go + 12 * i) for i in range(gc)]
        self.sprites = [struct.unpack_from("<HH", b, sp + so + 4 * i) for i in range(sc)]
        sq = base + sq2_o
        seq_n, seq_o, ctl_n, ctl_o, an_n, an_o, key_n, key_o = struct.unpack_from("<8i", b, sq + 0x10)
        self.seqs = [struct.unpack_from("<ihh", b, sq + seq_o + 16 * i) for i in range(seq_n)]
        self.ctls = [struct.unpack_from("<iiihh", b, sq + ctl_o + 16 * i) for i in range(ctl_n)]
        self.anims = []
        for i in range(an_n):
            raw = b[sq + an_o + 28 * i : sq + an_o + 28 * i + 28]
            sprite, kofs = struct.unpack_from("<hH", raw, 4)
            keys, k = {}, kofs
            for idx, n in enumerate(raw[8:20]):
                ks = [struct.unpack_from("<fi4s", b, sq + key_o + 12 * (k + j)) for j in range(n)]
                k += n
                if ks:
                    keys[KINDS[idx]] = ks
            self.anims.append({"sprite": sprite, "keys": keys, "kind": raw[20], "blend": raw[21]})
        self.texture = tim2_picture(b, base + tm2_o)


def key_at(keys, kind, frame):
    """A kind's value at `frame`: the keys around it blended (a point key, 0xc0, holds)."""

    def value(k):
        if kind == "color":
            c = k[1] & 0xFFFFFFFF
            return np.array([(c >> s) & 255 for s in (0, 8, 16, 24)], dtype=np.float64)
        if kind == "status":
            return k[2]
        return f32(k[1])

    if frame <= keys[0][0]:
        return value(keys[0])
    for a, c in zip(keys, keys[1:]):
        if a[0] <= frame <= c[0]:
            if c[0] == a[0] or (a[2][0] & 0xC0) == 0xC0 or kind == "status":
                return value(a)
            t = (frame - a[0]) / (c[0] - a[0])
            return value(a) + (value(c) - value(a)) * t
    return value(keys[-1])


class Layouts:
    def __init__(self, path: Path):
        b = path.read_bytes()
        if b[:4] != b"L2D@":
            raise ValueError(f"{path}: not an L2D")
        n, o, ly, _ = struct.unpack_from("<4i", b, 0x20)
        # the SQ2P table's offsets count from the table itself
        self.blocks = [Block(b, o + struct.unpack_from("<I", b, o + 4 * i)[0]) for i in range(n)]
        lc, lo, _, _, _, no, _, fo = struct.unpack_from("<8i", b, ly + 0x10)
        nmo = struct.unpack_from("<i", b, ly + 0x38)[0]
        self.ids = [struct.unpack_from("<H", b, ly + nmo + 2 * i)[0] for i in range(lc)]
        self.layouts = [struct.unpack_from("<iihhhh", b, ly + lo + 16 * i) for i in range(lc)]
        self.nodes = [struct.unpack_from("<ihhbbbbiihhhhi", b, ly + no + 32 * i) for i in range((fo - no) // 32)]

    def layout_nodes(self, layout_id):
        mf, nidx, ncount, _, _, _ = self.layouts[self.ids.index(layout_id)]
        return self.nodes[nidx : nidx + ncount]

    def control(self, node, control):
        """A node's control (max frame, return frame, first animation, once, animations), or None: past
        its max frame one with `once` 0 plays again from its return frame, one with 1 holds its last."""
        base, seq = node[1], node[2]
        if not (0 <= base < len(self.blocks)) or not (0 <= seq < len(self.blocks[base].seqs)):
            return None
        blk = self.blocks[base]
        ci, cn, _ = blk.seqs[seq]
        return blk.ctls[ci + min(control, cn - 1)] if cn > 0 else None

    def groups(self, layout_id):
        """The ids of the layout's groups: nodes whose animations all move their children."""
        out = []
        for node in self.layout_nodes(layout_id):
            c = self.control(node, STILL)
            if c is not None and all(a["sprite"] < 0 for a in self.blocks[node[1]].anims[c[2] : c[2] + c[4]]):
                out.append(node[12])
        return out

    def parent_id(self, layout_id, nid):
        nodes = self.layout_nodes(layout_id)
        parent = next(n[9] for n in nodes if n[12] == nid)
        return nodes[parent][12] if parent >= 0 else None

    def length(self, layout_id, nids, control):
        """The longest of those nodes' `control` (its max frame), at least 1."""
        longest = 1
        for node in self.layout_nodes(layout_id):
            if node[12] in nids:
                c = self.control(node, control)
                longest = max(longest, c[0] if c is not None else 0)
        return longest

    def pieces(self, layout_id, frame, controls, words):
        """The layout's quads at `frame`, in its picture's texels (PICTURE: two to a unit, the
        layout's middle at the picture's), in the order they are drawn: each node with the control
        `controls` gives it by node id (None leaves it out), the parts in `words` (by block 0's
        part index: a name and its picture) drawn from those pictures instead of the texture."""
        mf, nidx, ncount, _, lx, ly = self.layouts[self.ids.index(layout_id)]
        unit = PICTURE[0] / 960.0
        out, place, fade = [], {}, {}
        for k in range(ncount):
            _, base, seq, _, _, _, _, _, _, parent, x, y, nid, _ = self.nodes[nidx + k]
            px, py = place.get(parent, (lx, ly))
            here, alpha = (px + x, py + y), fade.get(parent, 1.0)
            place[k], fade[k] = here, alpha
            control = controls.get(nid, SELECTED)
            c = self.control(self.nodes[nidx + k], control) if control is not None else None
            if c is None:
                continue
            blk = self.blocks[base]
            cmax, cret, aidx, once, an = c
            f = frame if cmax <= 0 or frame <= cmax else cmax if once else cret + (frame - cret) % max(1, cmax - cret)
            ox = oy = 0.0
            for a in blk.anims[aidx : aidx + an]:
                if a["sprite"] < 0 and a["kind"] == 0:  # a parent animation: it moves and fades the children
                    v = self.values(a, f)
                    ox += v["base_x"] + v["offset_x"]
                    oy += v["base_y"] + v["offset_y"]
                    alpha *= v["color"][3] / 255.0
            place[k], fade[k] = (here[0] + ox, here[1] + oy), alpha
            for j, a in enumerate(blk.anims[aidx : aidx + an]):
                if a["sprite"] >= 0 and not (a["kind"] & 0x80):  # kind 0x81: not drawn in this control
                    made = self.sprite(
                        (k, j),
                        blk,
                        base,
                        a,
                        f,
                        PICTURE[0] / 2 + (here[0] + ox) * unit,
                        PICTURE[1] / 2 + (here[1] + oy) * unit,
                        fade.get(parent, 1.0),
                        words,
                        unit,
                    )
                    for q in made:
                        q["node"] = nid
                    if nid == SETTINGS_NODE:
                        made = [q for piece in made for q in self.key_apart(piece)]
                    out += made
        return out

    def key_apart(self, piece):
        """The settings key's part as two pieces: its key (its own texels where they are not
        clear, a prompt for the port's settings) and the rest; other parts as they are."""
        if piece["region"][0] != "texture" or piece["flip"] != (False, False):
            return [piece]
        _, base, ua, va, ub, vb = piece["region"]
        px = self.blocks[base].texture[va:vb, ua:ub].astype(np.int32)
        blue = ((px[..., 2] > px[..., 0] + 40) & (px[..., 3] > 60)).any(axis=0)
        if not blue.any() or np.argmax(blue) < 2:
            return [piece]
        split = int(np.argmax(blue)) - 1
        seen = px[:, :split, 3] > 8
        cols, rows = np.nonzero(seen.any(axis=0))[0], np.nonzero(seen.any(axis=1))[0]
        if len(cols) == 0:
            return [piece]
        kx0, kx1, ky0, ky1 = int(cols[0]), int(cols[-1]) + 1, int(rows[0]), int(rows[-1]) + 1
        x0, y0, x1, y1 = piece["rect"]
        rw, rh = (x1 - x0) / (ub - ua), (y1 - y0) / (vb - va)
        key = dict(
            piece,
            key=piece["key"] + (0,),
            region=("texture", base, ua + kx0, va + ky0, ua + kx1, va + ky1),
            rect=(x0 + kx0 * rw, y0 + ky0 * rh, x0 + kx1 * rw, y0 + ky1 * rh),
            prompt=SETTINGS_ACTION,
        )
        rest = dict(
            piece,
            key=piece["key"] + (1,),
            region=("texture", base, ua + split, va, ub, vb),
            rect=(x0 + split * rw, y0, x1, y1),
        )
        return [key, rest]

    @staticmethod
    def values(anim, f):
        v = dict(DEFAULT)
        v["color"] = np.array([128.0, 128.0, 128.0, 255.0])
        v["status"] = None
        for kind, keys in anim["keys"].items():
            v[kind] = key_at(keys, kind, f)
        return v

    def sprite(self, key, blk, base, anim, f, ox, oy, parent_alpha, words, unit):
        """A sprite's quads: each its part's texels (one column stretched where u0 == u1), its
        corners' colours (R, G, B, A, 0x80 = 1.0) blended across it, the key colour (RGB 0x80 =
        1.0, alpha of 255) over all of it, placed and scaled by the keys."""
        v = self.values(anim, f)
        if abs(v["rotate"]) > 1e-6:
            raise ValueError("a turned sprite: the port draws the pieces upright")
        n = anim["sprite"]
        st = v["status"]
        if st is not None and st[0] == 1:
            n += st[1] + 8 * st[3]  # an animated sprite's frame
        if n >= len(blk.sprites):
            return []
        gcount, gidx = blk.sprites[n]
        sx, sy = v["scale_x"], v["scale_y"]
        tx = ox + (v["base_x"] + v["offset_x"]) * unit
        ty = oy + (v["base_y"] + v["offset_y"]) * unit
        c = v["color"]
        colour = (c[0] / 128.0, c[1] / 128.0, c[2] / 128.0, c[3] / 255.0 * parent_alpha)
        out = []
        for g in range(gidx, gidx + gcount):
            x0, y0, x1, y1, pidx, _ = blk.groups[g]
            u0, v0, u1, v1, *corners = blk.parts[pidx]
            word = words.get(pidx) if base == 0 else None
            if word is not None:
                region = ("word", word[0])
                x1 = x0 + word[1].shape[1] / 2.0  # its own width: two texels to a unit, as the texture
            else:
                ua, ub = sorted((u0, u1))
                va, vb = sorted((v0, v1))
                region = ("texture", base, ua, va, max(ub, ua + 1), max(vb, va + 1))
            qx0, qx1 = sorted((x0 * sx, x1 * sx))
            qy0, qy1 = sorted((y0 * sy, y1 * sy))
            if qx1 - qx0 <= 0 or qy1 - qy0 <= 0:
                continue
            out.append(
                {
                    "key": key + (g,),
                    "region": region,
                    "blend": anim["blend"],
                    # turned over by its corners' order, its texels' or a negative scale
                    "flip": (((x1 < x0) != (u1 < u0)) != (sx < 0), ((y1 < y0) != (v1 < v0)) != (sy < 0)),
                    "rect": (tx + qx0 * unit, ty + qy0 * unit, tx + qx1 * unit, ty + qy1 * unit),
                    "corners": [tuple(((cc >> s) & 255) / 128.0 for s in (0, 8, 16, 24)) for cc in corners],
                    "colour": colour,
                }
            )
        return out


# --- The atlas: every piece's texels in one picture ---------------------------------------------


class Atlas:
    PAD = 1  # each region's edge repeated around it

    def __init__(self):
        self.pixels, self.at = {}, {}

    def add(self, region, pixels):
        self.pixels.setdefault(region, pixels)

    def pack(self) -> np.ndarray:
        """Rows of regions, the tallest first, as wide as the widest or 2048."""
        p = self.PAD
        width = max([2048] + [px.shape[1] + 2 * p for px in self.pixels.values()])
        x = y = row = 0
        for region, px in sorted(self.pixels.items(), key=lambda r: -r[1].shape[0]):
            w, h = px.shape[1] + 2 * p, px.shape[0] + 2 * p
            if x + w > width:
                x, y, row = 0, y + row, 0
            self.at[region] = (x + p, y + p)
            x, row = x + w, max(row, h)
        out = np.zeros((y + row, width, 4), dtype=np.uint8)
        for region, (ax, ay) in self.at.items():
            px = self.pixels[region]
            out[ay - p : ay + px.shape[0] + p, ax - p : ax + px.shape[1] + p] = np.pad(
                px, ((p, p), (p, p), (0, 0)), mode="edge"
            )
        return out

    def texels(self, piece):
        """The piece's texels in the atlas: x, y, width, height, a turned-over side's negative."""
        px = self.pixels[piece["region"]]
        x, y = self.at[piece["region"]]
        w, h = px.shape[1], px.shape[0]
        fx, fy = piece["flip"]
        return (x + w if fx else x, y + h if fy else y, -w if fx else w, -h if fy else h)


# --- A clip: its pieces and their keys ----------------------------------------------------------


def track(samples, eps):
    """The frames whose values a line between the kept ones would not give (to eps): the keys."""
    frames = sorted(samples)
    kept, last = [frames[0]], 0
    for i in range(1, len(frames) - 1):
        a, b = frames[last], frames[i + 1]
        va, vb = np.array(samples[a]), np.array(samples[b])
        for m in range(last + 1, i + 1):
            t = (frames[m] - a) / (b - a)
            if np.any(np.abs(va + (vb - va) * t - np.array(samples[frames[m]])) > eps):
                kept.append(frames[i])
                last = i
                break
    if len(frames) > 1:
        kept.append(frames[-1])
    return kept


def clip(layouts, length, controls, words, layout=MENU_LAYOUT):
    """The clip's pieces over its frames, each its region, blend, corners and keys (frame: x0 y0
    x1 y1 r g b a), in the order drawn; a piece missing from a frame there with no alpha."""
    pieces = {}
    for f in range(length):
        for p in layouts.pieces(layout, float(f), controls, words):
            entry = pieces.setdefault(p["key"], {"piece": p, "samples": {}})
            entry["samples"][f] = tuple(p["rect"]) + tuple(p["colour"])
    out = []
    for key in sorted(pieces):
        samples = pieces[key]["samples"]
        present = sorted(samples)
        for f in range(length):
            if f not in samples:
                near = min(present, key=lambda g: abs(g - f))
                samples[f] = samples[near][:7] + (0.0,)
        if all(s[7] <= 0.0 for s in samples.values()):
            continue
        eps = np.array([0.01] * 4 + [0.002] * 4)
        out.append({"piece": pieces[key]["piece"], "keys": [(f, samples[f]) for f in track(samples, eps)]})
    return out


def keys_at(keys, f):
    """The values at frame f between the keys, a line between them."""
    if f <= keys[0][0]:
        return np.array(keys[0][1])
    for (a, va), (b, vb) in zip(keys, keys[1:]):
        if a <= f <= b:
            return np.array(va) + (np.array(vb) - np.array(va)) * ((f - a) / (b - a) if b > a else 0.0)
    return np.array(keys[-1][1])


def render(atlas, texels, pieces, frame, size):
    """The clip at frame drawn as the port draws it (shaders/onescreen.hlsl backdrop_at): on the
    ground, each piece where its rectangle covers a texel's middle, its atlas texels filtered
    between the four nearest (within its own), times its corners' colours blended across it."""
    sx, sy = size[0] / PICTURE[0], size[1] / PICTURE[1]
    canvas = np.ones((size[1], size[0], 3), dtype=np.float32) * np.array(GROUND, dtype=np.float32)
    tex = atlas.astype(np.float32) / 255.0
    for entry, (u, v, w, h) in zip(pieces, texels):
        p = entry["piece"]
        x0, y0, x1, y1, r, g, b, a = keys_at(entry["keys"], frame)
        if a <= 0.0:
            continue
        ix = np.arange(max(0, int(math.floor(x0 * sx))), min(size[0], int(math.ceil(x1 * sx))))
        iy = np.arange(max(0, int(math.floor(y0 * sy))), min(size[1], int(math.ceil(y1 * sy))))
        if len(ix) == 0 or len(iy) == 0:
            continue
        fx = ((ix + 0.5) / sx - x0) / (x1 - x0)
        fy = ((iy + 0.5) / sy - y0) / (y1 - y0)
        mx, my = (fx >= 0) & (fx < 1), (fy >= 0) & (fy < 1)
        ix, fx, iy, fy = ix[mx], fx[mx], iy[my], fy[my]
        ax = u + fx * w - 0.5
        ay = v + fy * h - 0.5
        lox, hix = min(u, u + w), max(u, u + w) - 1
        loy, hiy = min(v, v + h), max(v, v + h) - 1
        ax0 = np.clip(np.floor(ax), lox, hix).astype(int)
        ay0 = np.clip(np.floor(ay), loy, hiy).astype(int)
        ax1 = np.clip(np.floor(ax) + 1, lox, hix).astype(int)
        ay1 = np.clip(np.floor(ay) + 1, loy, hiy).astype(int)
        tx = (ax - np.floor(ax))[None, :, None]
        ty = (ay - np.floor(ay))[:, None, None]
        t = (tex[ay0][:, ax0] * (1 - tx) + tex[ay0][:, ax1] * tx) * (1 - ty) + (
            tex[ay1][:, ax0] * (1 - tx) + tex[ay1][:, ax1] * tx
        ) * ty
        c = [np.array(k, dtype=np.float32) * np.array((r, g, b, a), dtype=np.float32) for k in p["corners"]]
        gx, gy = fx[None, :, None], fy[:, None, None]
        corner = (c[0] * (1 - gx) + c[1] * gx) * (1 - gy) + (c[2] * (1 - gx) + c[3] * gx) * gy
        col = np.clip(t * corner, 0, 1)
        dst = canvas[iy[0] : iy[-1] + 1, ix[0] : ix[-1] + 1]
        alpha = col[..., 3:4]
        if p["blend"] == 1:
            dst[...] = np.clip(dst + col[..., :3] * alpha, 0, 1)
        elif p["blend"] == 2:
            dst[...] = np.clip(dst - col[..., :3] * alpha, 0, 1)
        else:
            dst[...] = dst * (1 - alpha) + col[..., :3] * alpha
    return canvas


def button_rect(pieces):
    """Where the settings key's pieces are at the clip's first frame (the picture's texels)."""
    rects = [keys_at(e["keys"], 0.0)[:4] for e in pieces if e["piece"].get("node") == SETTINGS_NODE]
    if not rects:
        return None
    return (min(r[0] for r in rects), min(r[1] for r in rects), max(r[2] for r in rects), max(r[3] for r in rects))


def icon_pixels(folder: Path):
    """The theater's button icons (ICONS) as pictures cut to their drawing, by 'family:input';
    none when the font folder has no Icon.tm2."""
    path = folder / "Icon.tm2"
    if not path.exists():
        print(f"{path}: not there; the settings key shows Esc only", file=sys.stderr)
        return {}
    sheet = tim2_picture(path.read_bytes(), 0)
    out = {}
    for family, cells in ICONS.items():
        for name, cell in cells.items():
            x, y = cell[0], cell[1]
            w = cell[2] if len(cell) > 2 else 52
            px = sheet[y : y + 52, x : x + w]
            seen = px[..., 3] > 8
            cols, rows = np.nonzero(seen.any(axis=0))[0], np.nonzero(seen.any(axis=1))[0]
            if len(cols):
                out[f"{family}:{name}"] = px[rows[0] : rows[-1] + 1, cols[0] : cols[-1] + 1].copy()
    return out


MENUS = ("root", "story", "mission")
INTRO = "intro"  # the group of intro_0_0.txt, the menu's fade-in while the lower screen is white


def write_clips(path: Path, group: str, clips, atlas: Atlas, icons=(), button=None, atlas_name="atlas.png"):
    """The animated picture's file: the button icons in the atlas, the settings key's place,
    then each clip (its name, its frames, its pieces) with the groups it follows -- enter after
    none and the intro, arrive after the other levels, a name 'after ...' its own groups -- and
    the loop."""
    lines = [
        "# The title as the 358/2 Days theater of KINGDOM HEARTS HD 1.5+2.5 ReMIX draws its main",
        "# menu, made by tools/hd/theater_title.py from the player's own copy: the port's",
        "# animated picture (docs/COMPOSITIONS.md).",
        f"picture {PICTURE[0]} {PICTURE[1]}",
        "ground " + " ".join(f"{g:g}" for g in GROUND),
        f"atlas {atlas_name}",
        f"group {group}",
    ]
    for name in icons:
        u, v = atlas.at[("icon", name)]
        h, w = atlas.pixels[("icon", name)].shape[:2]
        lines.append(f"icon {name} {u} {v} {w} {h}")
    if button is not None:
        lines.append(f"button {SETTINGS_ACTION} " + " ".join(f"{x:.2f}" for x in button))
    after = {"enter": f"none {INTRO}", "arrive": " ".join(m for m in MENUS if m != group)}
    for name, length, pieces in clips:
        lines.append(
            f"loop {length}"
            if name == "loop"
            else f"clip {length} {name}"
            if name.startswith("after ")
            else f"clip {length} after {after[name]}"
        )
        for entry in pieces:
            p = entry["piece"]
            u, v, w, h = atlas.texels(p)
            corners = " ".join(f"{x:.4g}" for corner in p["corners"] for x in corner)
            lines.append(f"piece {u} {v} {w} {h} {BLENDS.get(p['blend'], 'over')} {corners}")
            if p.get("prompt"):
                lines.append(f"prompt {p['prompt']}")
            for f, vals in entry["keys"]:
                lines.append(
                    f"key {f} " + " ".join(f"{x:.2f}" for x in vals[:4]) + " " + " ".join(f"{x:.4f}" for x in vals[4:])
                )
    path.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))


# --- The menu font ------------------------------------------------------------------------------


def ink_widths(picture: np.ndarray, glyphs: dict, most: int = 4) -> dict:
    """Each glyph's drawn width in a font's picture: its box (x, y, the advance its file gives,
    32 high) and the columns right of it its ink goes on into while no other glyph's box holds them
    -- the theater's event font outlines its letters one or two texels past their advance, and
    cutting them at the advance cut the outlines."""
    alpha = picture[..., 3] > 8
    owned = np.zeros(alpha.shape, bool)
    for x, y, w in glyphs.values():
        owned[y : y + 32, x : x + w] = True
    out = {}
    for c, (x, y, w) in glyphs.items():
        width = w
        for k in range(most):
            col = x + w + k
            if col >= alpha.shape[1] or not (alpha[y : y + 32, col] & ~owned[y : y + 32, col]).any():
                break
            width = w + k + 1
        out[c] = width
    return out


class Font:
    """The theater's menu font: menufont.tm2 (32 x 32 cells) and menufont.cod (8 bytes a glyph:
    its UTF-16 code, its cell's x and y in the picture, a byte, its width)."""

    def __init__(self, folder: Path):
        cod = (folder / "menufont.cod").read_bytes()
        self.glyphs = {}
        for i in range(len(cod) // 8):
            code, x, y, _, w = struct.unpack_from("<HHHBB", cod, 8 * i)
            self.glyphs[chr(code)] = (x, y, w)
        self.picture = tim2_picture((folder / "menufont.tm2").read_bytes(), 0)
        self.ink = ink_widths(self.picture, self.glyphs)
        cap = self.picture[
            self.glyphs["R"][1] : self.glyphs["R"][1] + 32,
            self.glyphs["R"][0] : self.glyphs["R"][0] + self.glyphs["R"][2],
            3,
        ]
        rows = np.where(cap.max(1) > 0)[0]
        self.cap_top, self.cap_height = int(rows.min()), int(rows.max() - rows.min() + 1)

    def word(self, text: str, grey: int) -> np.ndarray:
        """The word as the theater's word parts hold theirs: its grey on a 76-texel row, starting
        79 texels in, capitals 43 high from row 19, the font's letters spaced as REPRODUCIR's."""
        k = WORD_CAP_HEIGHT / self.cap_height
        cell = int(math.ceil(32 * k))
        missing = [c for c in text if c != " " and c not in self.glyphs]
        if missing:
            raise ValueError(f"the menu font has no {missing!r}")
        space = SPACE_PER_CAP * WORD_CAP_HEIGHT

        def advance(c):
            return (space if c == " " else self.glyphs[c][2] * k) + WORD_TRACKING

        width = WORD_LEFT + sum(advance(c) for c in text) + 24
        mask = Image.new("L", (int(math.ceil(width)), WORD_HEIGHT), 0)
        x, top = float(WORD_LEFT), int(round(WORD_CAP_TOP - self.cap_top * k))
        for c in text:
            if c == " ":
                x += advance(c)
                continue
            gx, gy, gw = self.glyphs[c]
            glyph = Image.fromarray(self.picture[gy : gy + 32, gx : gx + self.ink[c], 3])
            glyph = glyph.resize((max(1, int(round(self.ink[c] * k))), cell), Image.LANCZOS)
            layer = Image.new("L", mask.size, 0)
            layer.paste(glyph, (int(round(x)), top))
            mask = Image.fromarray(np.maximum(np.asarray(mask), np.asarray(layer)))
            x += gw * k + WORD_TRACKING
        a = np.asarray(mask).astype(np.float32) / 255.0
        a = np.clip((a - 0.5) * 1.6 + 0.5, 0, 1)  # the enlarged glyphs' edges made crisp again
        out = np.zeros((WORD_HEIGHT, mask.size[0], 4), dtype=np.uint8)
        out[..., :3] = grey
        out[..., 3] = (a * 255).astype(np.uint8)
        return out


# --- The title's states -------------------------------------------------------------------------


def states(options):
    """Each state of the title's menu (Ov000_RefreshMenuLayout): its file's name, its level and
    its options."""
    out = []
    for menu, shown in (
        ("root", [options["root"][:2], options["root"][:3]]),
        ("story", [options["story"][:1], options["story"][:2]]),
        ("mission", [options["mission"][:2]]),
    ):
        for labels in shown:
            for row in range(len(labels)):
                out.append((f"{menu}_{len(labels)}_{row}", menu, labels, row))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("mare", type=Path, help="the theater extracted: <collection>/Image/Mare.hed_out/original")
    ap.add_argument("--mod", default="Theater title")
    ap.add_argument("--lang", nargs="*", default=list(LANGUAGES), choices=list(LANGUAGES))
    ap.add_argument(
        "--preview",
        nargs=3,
        metavar=("DIR", "CLIP", "FRAME"),
        help="also draw each state's clip at that frame into DIR/<lang>/, as the port does",
    )
    args = ap.parse_args()
    for lang in args.lang:
        folder, options = LANGUAGES[lang]
        l2d = args.mare / folder / "l2d" / "days_theatre.l2d"
        if not l2d.exists():
            print(f"{l2d}: not there (is MARE the extracted Mare.hed_out/original?)", file=sys.stderr)
            return 1
        layouts, font = Layouts(l2d), Font(args.mare / folder / "font")
        icons = icon_pixels(args.mare / folder / "font")
        groups = layouts.groups(MENU_LAYOUT)
        bars = {layouts.parent_id(MENU_LAYOUT, nid) for nid in OPTION_NODES}
        words_made, atlas, made = {}, Atlas(), []
        for name, px in icons.items():
            atlas.add(("icon", name), px)
        for name, menu, labels, row in states(options):
            controls = {nid: None for nid in LEFT_OUT_NODES}
            words = {}
            for slot, nid in enumerate(OPTION_NODES):
                if slot >= len(labels):
                    controls[nid] = None  # an option the level does not have: not drawn
                    continue
                controls[nid] = SELECTED if slot == row else NOT_SELECTED
                for part, grey in zip(OPTION_WORD_PARTS[slot], ("black", "white", "black")):
                    if (labels[slot], grey) not in words_made:
                        words_made[labels[slot], grey] = font.word(labels[slot], WORD_GREYS[grey])
                    words[part] = ((labels[slot], grey), words_made[labels[slot], grey])
            clips = []
            for clip_name, appearing in (("enter", groups), ("arrive", bars), ("loop", [])):
                these = {**controls, **{g: APPEAR for g in appearing}}
                length = (
                    layouts.length(MENU_LAYOUT, appearing, APPEAR)
                    if appearing
                    else layouts.length(MENU_LAYOUT, OPTION_NODES, SELECTED)
                )
                clips.append((clip_name, length, clip(layouts, length, these, words)))
            for _, _, pieces in clips:
                for entry in pieces:
                    region = entry["piece"]["region"]
                    if region[0] == "word":
                        atlas.add(region, words_made[region[1]])
                    else:
                        _, base, ua, va, ub, vb = region
                        atlas.add(region, layouts.blocks[base].texture[va:vb, ua:ub])
            made.append((name, menu, clips))
        for n, lid in enumerate(LOGO_LAYOUTS):
            # held (control 0): the game's own fades through white bring it in and out
            pieces = clip(layouts, 1, {}, {}, layout=lid)
            for entry in pieces:
                _, base, ua, va, ub, vb = entry["piece"]["region"]
                atlas.add(entry["piece"]["region"], layouts.blocks[base].texture[va:vb, ua:ub])
            made.append((f"logo_{len(LOGO_LAYOUTS)}_{n}", "logo", [("loop", 1, pieces)]))
        out_dir = ROOT / "mods" / args.mod / "layouts" / "title" / lang
        out_dir.mkdir(parents=True, exist_ok=True)
        pixels = atlas.pack()
        Image.fromarray(pixels, "RGBA").save(out_dir / "atlas.png")
        print(out_dir / "atlas.png", pixels.shape[1], "x", pixels.shape[0])
        write_clips(out_dir / "intro_0_0.txt", INTRO, [("loop", 1, [])], atlas)
        for name, menu, clips in made:
            write_clips(
                out_dir / f"{name}.txt", menu, clips, atlas, icons if menu != "logo" else (), button_rect(clips[-1][2])
            )
            print(out_dir / f"{name}.txt", ", ".join(f"{c} {n} frames, {len(p)} pieces" for c, n, p in clips))
            if args.preview:
                where, which, frame = Path(args.preview[0]) / lang, args.preview[1], float(args.preview[2])
                where.mkdir(parents=True, exist_ok=True)
                pieces = next(p for c, _, p in clips if c == which)
                canvas = render(pixels, [atlas.texels(e["piece"]) for e in pieces], pieces, frame, PICTURE)
                Image.fromarray((canvas * 255 + 0.5).astype(np.uint8), "RGB").save(
                    where / f"{name}_{which}_{int(frame)}.png"
                )
    return 0


if __name__ == "__main__":
    sys.exit(main())
