"""The room change's irises in high definition (graphics/ba/tr/<world>.z/tex/<world>_wipe@<world>_wipe_pl.png):
the textures the port puts in place of each world's wipe (runtime/mods/textures.h), 2048 x 1024, from KH2 HD's
own irises and, for the two worlds KH2 has none of, vector drawings (wipes_shapes.py).

On a room change in the field (ov002's Ov002_UpdateSceneFrame) the game draws a quad 128t x 96t DS dots (t the
wipe's size, 0 to 10) with the world's texture, ba/tr/<world>.z: 64 x 32, A5I3, black where it covers, the
world's symbol a hole in it. Days took those symbols from KH2, whose field2d/us/<world>0field.2dd holds the
same iris per world (its HD texture 2: 512 x 512, a black cover with the symbol cut out). Twilight Town's
keyhole, Agrabah's lamp, Beast's Castle's rose, Olympus's thundercloud, Halloween Town's pumpkin and the
Castle That Never Was's heart are KH2's, put where Days's symbol is; Wonderland's spade and Neverland's
feather are drawn.

KH2's iris is placed in the quad's proportions (a texel of Days's texture is 2 x 3 units): its symbol, its
aspect kept, as large as Days's symbol's box lets it be, centred on it. Enlarged from KH2's 512 dots, its edge
is drawn again sharp (bicubic, then cut at its middle about one dot wide), so the shape is KH2's exactly,
without a blur.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ds_tex  # noqa: E402
import wipes_shapes  # noqa: E402
from common import Inputs  # noqa: E402

WORLDS = ("tt", "al", "bb", "he", "aw", "nm", "pp", "eh")  # ba/tr's, in the game's order
TEX = (2048, 1024)  # the replacement's size: 32 times the DS's 64 x 32
SS = 32  # the placing canvas's dots a unit of the quad (128 x 96 units)


def days_shape(inputs: Inputs, world: str) -> np.ndarray:
    """Days's wipe texture, ba/tr/<world>.z (a BTX0: its TEX0 at the offset the header gives), as RGBA
    (32 x 64)."""
    b = inputs.ds(f"ba/tr/{world}.z")
    t = ds_tex.parse_tex0(b, struct.unpack_from("<I", b, 16)[0])
    ((param, _),) = t["texs"].values()
    pal = list(t["pals"].values())[0]
    return ds_tex.decode(b, t, param, pal)


def days_box(inputs: Inputs, world: str) -> tuple[float, float, float, float]:
    """Days's hole's box in the quad's units (x0, y0, x1, y1): a texel 2 x 3 units."""
    a = days_shape(inputs, world)[..., 3].astype(float) / 255
    ys, xs = np.nonzero(a < 0.5)
    return xs.min() * 2.0, ys.min() * 3.0, (xs.max() + 1) * 2.0, (ys.max() + 1) * 3.0


def kh2_hole(inputs: Inputs, world: str) -> np.ndarray:
    """KH2 HD's iris for `world` (field2d/us/<world>0field.2dd, its texture 2) as the hole: 1 where the symbol
    is, 0 the cover."""
    path = inputs.kh2 / "remastered" / "field2d" / "us" / f"{world}0field.2dd" / f"US_{world}0field_2dd2.png"
    im = Image.open(path).convert("RGBA")
    return 1.0 - np.asarray(im, float)[..., 3] / 255


def cut(f: np.ndarray) -> np.ndarray:
    """`f` cut at 0.5, its edge about one dot wide (from how fast it changes there)."""
    grad = np.hypot(*np.gradient(f))
    return np.clip(0.5 + (f - 0.5) / np.maximum(grad * 1.2, 1e-4), 0, 1)


def kh2_iris(inputs: Inputs, world: str) -> Image.Image:
    """KH2's iris in Days's place, at the texture's size (L: 255 the hole, 0 the cover)."""
    hole = kh2_hole(inputs, world)
    ys, xs = np.nonzero(hole > 0.5)
    kx0, ky0, kx1, ky1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    dx0, dy0, dx1, dy1 = days_box(inputs, world)
    k = min((dx1 - dx0) / (kx1 - kx0), (dy1 - dy0) / (ky1 - ky0))  # units a KH2 dot
    cx, cy = (dx0 + dx1) / 2, (dy0 + dy1) / 2
    # KH2's whole texture on the canvas: its dots k units, its symbol's middle on Days's
    h, w = hole.shape
    left = (cx - ((kx0 + kx1) / 2) * k) * SS
    top = (cy - ((ky0 + ky1) / 2) * k) * SS
    big = Image.fromarray((hole * 255).astype(np.uint8), "L")
    big = big.resize((round(w * k * SS), round(h * k * SS)), Image.Resampling.BICUBIC)
    sharp = Image.fromarray((cut(np.asarray(big, float) / 255) * 255).astype(np.uint8), "L")
    canvas = Image.new("L", (128 * SS, 96 * SS), 0)
    canvas.paste(sharp, (round(left), round(top)))
    # each texel 2 x 3 units: 2 x 3 canvas dots averaged to one
    return canvas.resize(TEX, Image.Resampling.BOX)


def make(inputs: Inputs, mod: Path) -> None:
    for world in WORLDS:
        if world in wipes_shapes.SHAPES:
            hole = wipes_shapes.hole(world, TEX)
        else:
            hole = kh2_iris(inputs, world)
        # black, the cover opaque, the hole clear
        rgba = np.zeros((TEX[1], TEX[0], 4), np.uint8)
        rgba[..., 3] = 255 - np.asarray(hole)
        out = mod / "graphics" / "ba" / "tr" / f"{world}.z" / "tex" / f"{world}_wipe@{world}_wipe_pl.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(rgba, "RGBA").save(out, optimize=True)
