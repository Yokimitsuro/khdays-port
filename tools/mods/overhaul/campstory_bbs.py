"""Birth by Sleep's camp menu as the story camp menu's plates take it: the US_camp entry of bbs_first (an ARC
whose camp.l2d is an L2D like the 358/2 Days theater's, tools/hd/theater_title.py) read with its HD textures,
and its pieces drawn as the port draws them (bilinear, their corners' colours blended across them)."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "hd"))
import theater_title as tt  # noqa: E402

# The US_camp entry (its path's name unknown: the archive names it by its MD5) and its HD textures, one for each
# of camp.l2d's SQ2P blocks in their order: the menu's sheet and two small ones.
ENTRY = "cba7b0396c9efe756ba1e069e6dd603e.dat"
TEXTURES = ("US_camp_arc12", "US_camp_arc7", "US_camp_arc8")

# Birth by Sleep's SQ2 against the Days theater's (theater_title.Block): its sequences 8 bytes (not 16), its
# animations 24 bytes -- +4 the sprite, +6 the first key, +8 eleven key counts (no k8), +19 the kind,
# +20 the blend -- the rest alike.
KINDS = ["status", "base_x", "base_y", "k3", "offset_x", "offset_y", "k6", "rotate", "scale_x", "scale_y", "color"]


def arc_file(entry: bytes, name: str) -> bytes:
    """A file of a Birth by Sleep ARC: its count at +6, then 32-byte records from +0x10 (+4 the offset, +8 the
    size, +16 the name)."""
    n = struct.unpack_from("<H", entry, 6)[0]
    for i in range(n):
        e = 0x10 + 0x20 * i
        _, off, size, _ = struct.unpack_from("<4I", entry, e)
        if entry[e + 16 : e + 32].split(b"\0")[0].decode() == name:
            return entry[off : off + size]
    raise KeyError(name)


def hd_asset(folder: Path, stem: str) -> Path:
    """An entry's HD texture as the collection holds it: a PNG or a DDS, kept raw by the installer."""
    return next(p for p in sorted(folder.iterdir()) if p.stem == stem)


def hd_texture(folder: Path, stem: str) -> Image.Image:
    """An entry's HD texture, as RGBA."""
    with Image.open(hd_asset(folder, stem)) as im:
        im.load()
        return im.convert("RGBA")


class Block(tt.Block):
    """One SQ2P of Birth by Sleep's: the theater's Block's fields, read from BBS's own SQ2. The HD collection
    keeps the TIM2's header alone ("Tm2DataAreaReduction"): its texels are the entry's HD texture (`hd`, None
    for none) scaled to the TIM2's size."""

    def __init__(self, b: bytes, base: int, hd: Image.Image | None):
        sp2_o, sq2_o, tm2_o = struct.unpack_from("<3I", b, base + 0x10)
        sp = base + sp2_o
        pc, po, gc, go, sc, so = struct.unpack_from("<6i", b, sp + 0x10)
        self.parts = [struct.unpack_from("<4h4I", b, sp + po + 24 * i) for i in range(pc)]
        self.groups = [struct.unpack_from("<4hHH", b, sp + go + 12 * i) for i in range(gc)]
        self.sprites = [struct.unpack_from("<HH", b, sp + so + 4 * i) for i in range(sc)]
        sq = base + sq2_o
        seq_n, seq_o, ctl_n, ctl_o, an_n, an_o, key_n, key_o = struct.unpack_from("<8i", b, sq + 0x10)
        self.seqs = [struct.unpack_from("<ihh", b, sq + seq_o + 8 * i) for i in range(seq_n)]
        self.ctls = [struct.unpack_from("<iiihh", b, sq + ctl_o + 16 * i) for i in range(ctl_n)]
        self.anims = []
        for i in range(an_n):
            raw = b[sq + an_o + 24 * i : sq + an_o + 24 * i + 24]
            sprite, kofs = struct.unpack_from("<hH", raw, 4)
            keys, k = {}, kofs
            for idx, n in enumerate(raw[8:19]):
                ks = [struct.unpack_from("<fi4s", b, sq + key_o + 12 * (k + j)) for j in range(n)]
                k += n
                if ks:
                    keys[KINDS[idx]] = ks
            self.anims.append({"sprite": sprite, "keys": keys, "kind": raw[19], "blend": raw[20]})
        w, h = struct.unpack_from("<HH", b, base + tm2_o + 16 + 0x14)
        if hd is not None:
            self.texture = np.asarray(hd.convert("RGBA").resize((w, h), Image.LANCZOS))
        else:
            self.texture = np.zeros((h, w, 4), np.uint8)


class Layouts(tt.Layouts):
    """camp.l2d's layouts, read as the theater's (their pieces by tt.Layouts.pieces) from its bytes, its blocks
    BBS's; BBS names its layouts (16 bytes each) where the theater numbers them: they are taken by their index."""

    def __init__(self, b: bytes, textures):
        if b[:4] != b"L2D@":
            raise ValueError("camp.l2d: not an L2D")
        n, o, ly, _ = struct.unpack_from("<4i", b, 0x20)
        hd = list(textures)
        self.blocks = [
            Block(b, o + struct.unpack_from("<I", b, o + 4 * i)[0], hd.pop(0) if hd else None) for i in range(n)
        ]
        lc, lo, _, _, _, no, _, fo = struct.unpack_from("<8i", b, ly + 0x10)
        self.layouts = [struct.unpack_from("<iihhhh", b, ly + lo + 16 * i) for i in range(lc)]
        self.nodes = [struct.unpack_from("<ihhbbbbiihhhhi", b, ly + no + 32 * i) for i in range((fo - no) // 32)]
        self.ids = list(range(len(self.layouts)))


class Camp:
    """Birth by Sleep's camp menu from the player's bbs_first extraction: its layouts and its sheet (the first
    texture, RGBA in 0..1) the plates are drawn from."""

    def __init__(self, bbs: Path):
        folder = bbs / "remastered" / ENTRY
        textures = [hd_texture(folder, t) for t in TEXTURES]
        self.lay = Layouts(arc_file((bbs / "original" / ENTRY).read_bytes(), "camp.l2d"), textures)
        self.tex = np.asarray(textures[0], np.float32) / 255.0

    def draw(self, canvas: Image.Image, pieces) -> Image.Image:
        """Pieces ((u0, v0, u1, v1) in the sheet, rect, corners 4 x RGBA, flip, blend, key) onto `canvas` (an
        RGBA picture), as the port draws them: each piece's texels resized bilinear to its rectangle, times its
        corners' colours blended across it, laid over (or added, blend 1)."""
        acc = np.asarray(canvas, np.float32) / 255.0
        for (ua, va, ub, vb), (x0, y0, x1, y1), corners, flip, blend, _ in pieces:
            w, h = int(round(x1 - x0)), int(round(y1 - y0))
            if w <= 0 or h <= 0:
                continue
            src = Image.fromarray((self.tex[va:vb, ua:ub] * 255).astype(np.uint8), "RGBA")
            img = np.asarray(src.resize((w, h), Image.BILINEAR), np.float32) / 255.0
            if flip[0]:
                img = img[:, ::-1]
            if flip[1]:
                img = img[::-1]
            cs = [np.array(c, np.float32) for c in corners]
            gy = np.linspace(0, 1, h)[:, None, None]
            gx = np.linspace(0, 1, w)[None, :, None]
            corner = (cs[0] * (1 - gx) + cs[1] * gx) * (1 - gy) + (cs[2] * (1 - gx) + cs[3] * gx) * gy
            img = np.clip(img * corner, 0, 1)
            X, Y = int(round(x0)), int(round(y0))
            xa, ya, xb, yb = max(X, 0), max(Y, 0), min(X + w, acc.shape[1]), min(Y + h, acc.shape[0])
            if xb <= xa or yb <= ya:
                continue
            piece = img[ya - Y : yb - Y, xa - X : xb - X]
            dst = acc[ya:yb, xa:xb]
            al = piece[..., 3:4]
            if blend == 1:
                dst[..., :3] = np.clip(dst[..., :3] + piece[..., :3] * al, 0, 1)
            else:
                dst[..., :3] = piece[..., :3] * al + dst[..., :3] * (1 - al)
                dst[..., 3:4] = al + dst[..., 3:4] * (1 - al)
        return Image.fromarray((np.clip(acc, 0, 1) * 255).astype(np.uint8), "RGBA")


def red(c):
    """A tint (r, g, b, a: 1.0 the texel's own) with BBS's blue made Days's red: (0, 0.18, 1.12) -> (1.12, 0.10,
    0.12)."""
    r, g, b, a = c
    if b > r + 0.2:
        return (b, g * 0.55, r + 0.12, a)
    return c
