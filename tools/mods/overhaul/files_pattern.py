"""The Files pages' backdrop tile (layouts/files/files_pattern.png, 512 x 512: the screen wraps it), also the
pattern behind the story's menu and the save page: the five Kingdom Hearts symbols the user drew (art/files/
symbols.png: the heart, the Nobody, Mickey, the shuriken, the crown, black on clear) set on a lattice that repeats
with no seam, dark grey on the camp's ground."""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image

PICTURE = "symbols.png"
# Each symbol's box in the user's picture (x0, y0, x1, y1)
BOXES = {
    "heart": (105, 0, 371, 289),
    "nobody": (502, 282, 792, 649),
    "mickey": (76, 362, 408, 628),
    "shuriken": (509, 715, 776, 986),
    "crown": (98, 722, 427, 954),
}
# The lattice: rows of Nobody-crown, shurikens, heart-Mickey, shurikens; each symbol's middle (x, y) on the tile
PLACES = [
    ("nobody", 0, 0),
    ("crown", 256, 0),
    ("shuriken", 128, 128),
    ("shuriken", 384, 128),
    ("heart", 0, 256),
    ("mickey", 256, 256),
    ("shuriken", 128, 384),
    ("shuriken", 384, 384),
]
# How large each symbol is on the tile: the box (w, h) it had in the pattern before; each takes that box's area
# in its own aspect
SIZES = {"nobody": (73, 97), "crown": (80, 58), "shuriken": (75, 72), "heart": (81, 87), "mickey": (99, 88)}
TILE = 512
GROUND = np.array([8, 8, 14], np.float32)  # the camp's ground
INK = np.array([34, 34, 42], np.float32)  # the pattern's grey


def painted(cov: np.ndarray) -> np.ndarray:
    """The pattern's grey over its ground as much as `cov` (0 to 1) covers each texel, 8 bits."""
    return (GROUND + (INK - GROUND) * cov[..., None]).round().astype(np.uint8)


def symbol(picture: np.ndarray, name: str) -> np.ndarray:
    """A symbol's coverage on the tile (0 to 1): its shape in the user's picture (its opaque dark), cut to where it
    is over half, scaled to its size in two steps (to 8 times by Lanczos, then by area) so that its edges are
    smooth."""
    x0, y0, x1, y1 = BOXES[name]
    px = picture[y0:y1, x0:x1]
    shape = px[..., 3] * np.clip(1.0 - px[..., :3].mean(-1), 0, 1)
    ys, xs = np.nonzero(shape > 0.5)
    shape = shape[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1]
    w, h = SIZES[name]
    aspect = shape.shape[1] / shape.shape[0]
    nh = int(round((w * h / aspect) ** 0.5))
    nw = int(round(nh * aspect))
    big = Image.fromarray((shape * 255).astype(np.uint8)).resize((nw * 8, nh * 8), Image.LANCZOS)
    return np.asarray(big.resize((nw, nh), Image.BOX)).astype(np.float32) / 255


def pattern_tile(art: Path) -> Image.Image:
    """The tile: each symbol at its places, a symbol on an edge whole across it (the tile wraps)."""
    picture = np.asarray(Image.open(art / PICTURE).convert("RGBA")).astype(np.float32) / 255
    shapes = {name: symbol(picture, name) for name in BOXES}
    cov = np.zeros((TILE, TILE), np.float32)
    for name, x, y in PLACES:
        s = shapes[name]
        h, w = s.shape
        x0, y0 = int(round(x - w / 2)), int(round(y - h / 2))
        rows, cols = np.arange(y0, y0 + h) % TILE, np.arange(x0, x0 + w) % TILE
        cov[np.ix_(rows, cols)] = np.maximum(cov[np.ix_(rows, cols)], s)
    return Image.fromarray(painted(cov), "RGB")
