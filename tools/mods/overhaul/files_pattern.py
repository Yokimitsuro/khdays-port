"""The Files pages' backdrop tile (layouts/files/files_pattern.png, 512 x 512: the screen wraps it), also the
pattern behind the story's menu and the save page: one of each Kingdom Hearts symbol of the user's pattern picture
(art/files/pattern.png: the Nobody, the crown, the heart, Mickey, the shuriken) cut from it and set again on an
exact lattice, so that the tile repeats with no seam (the picture has one in its middle), dark grey on the camp's
ground; then its heart and its crown redrawn from the user's cleaner pictures of them (art/files/heart_kh.webp,
crown_kh.png). Each step works on the previous one's 8-bit picture, as the tile was made and remade."""

from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image

# The symbols in the pattern picture: their middle (y, x) and half their width and height
SPRITES = {
    "nobody": (248, 330, 26, 32),
    "crown": (242, 484, 28, 22),
    "heart": (360, 332, 28, 28),
    "mickey": (364, 218, 30, 28),
    "shuriken": (302, 410, 24, 24),
}
# The lattice (the picture's own spacing made regular and a power of two): rows of Nobody-crown, shurikens,
# heart-Mickey, shurikens; each symbol's middle at (x, y) of a 256 x 256 tile
PLACES = [
    ("nobody", 0, 0),
    ("crown", 128, 0),
    ("shuriken", 64, 64),
    ("shuriken", 192, 64),
    ("heart", 0, 128),
    ("mickey", 128, 128),
    ("shuriken", 64, 192),
    ("shuriken", 192, 192),
]
TILE = (256, 256)
S = 2  # the tile made at twice the lattice's size (its edges made crisp again: see sprite)
GROUND = np.array([8, 8, 14], np.float32)  # the camp's ground
INK = np.array([34, 34, 42], np.float32)  # the pattern's grey
CLEAR = 4  # a redrawn symbol: cleared this far round the old one's ink
# The redrawn symbols: the user's picture, the old ink's middle on the tile (x, y: the heart's on the tile's left
# edge, the crown's on its top one; the tile wraps), its width and height, how the picture holds the shape (blue:
# its blue; dark: its dark on white) and whether the new shape fills the old box exactly (or keeps its own aspect
# at the same area)
SYMBOLS = (
    ("heart_kh.webp", (0.0, 255.5), (81.0, 88.0), "blue", True),
    ("crown_kh.png", (256.5, 0.5), (84.0, 56.0), "dark", False),
)


def painted(cov: np.ndarray) -> np.ndarray:
    """The pattern's grey over its ground as much as `cov` (0 to 1) covers each texel, 8 bits."""
    return (GROUND + (INK - GROUND) * cov[..., None]).round().astype(np.uint8)


def lattice_tile(picture: Image.Image) -> np.ndarray:
    """The symbols cut from the pattern picture and set on the lattice (RGB, 8 bits). Each one's coverage is the
    picture's grey over its ground, upscaled 8 times (bicubic), sharpened (a smoothstep round the middle grey) and
    scaled to the tile's size (Lanczos); its middle is its ink's."""
    a = np.asarray(picture.convert("L")).astype(np.float32)
    bg = float(np.median(a[a < 60]))

    def sprite(cy, cx, hw, hh):
        c = a[cy - hh : cy + hh, cx - hw : cx + hw]
        im = Image.fromarray((np.clip((c - bg) / (250 - bg), 0, 1) * 255).astype(np.uint8))
        big = np.asarray(im.resize((im.width * 8, im.height * 8), Image.BICUBIC)).astype(np.float32) / 255
        t = np.clip((big - 0.5) / 0.18 + 0.5, 0, 1)
        t = t * t * (3 - 2 * t)
        small = Image.fromarray((t * 255).astype(np.uint8)).resize((im.width * S, im.height * S), Image.LANCZOS)
        o = np.asarray(small).astype(np.float32) / 255
        ys, xs = np.nonzero(o > 0.02)
        return o, (ys.min() + ys.max()) / 2, (xs.min() + xs.max()) / 2

    cut = {k: sprite(*v) for k, v in SPRITES.items()}
    w, h = TILE[0] * S, TILE[1] * S
    cov = np.zeros((h, w), np.float32)
    for name, x, y in PLACES:
        o, cy, cx = cut[name]
        sh, sw = o.shape
        x0, y0 = int(round(x * S - cx)), int(round(y * S - cy))
        for dy in (-h, 0, h):  # a symbol on an edge is whole across it
            for dx in (-w, 0, w):
                X0, Y0 = x0 + dx, y0 + dy
                xa, ya, xb, yb = max(X0, 0), max(Y0, 0), min(X0 + sw, w), min(Y0 + sh, h)
                if xa < xb and ya < yb:
                    cov[ya:yb, xa:xb] = np.maximum(cov[ya:yb, xa:xb], o[ya - Y0 : yb - Y0, xa - X0 : xb - X0])
    return painted(cov)


def shape_of(picture: Image.Image, mode: str) -> np.ndarray:
    """A symbol's shape (0 to 1) in the user's picture, cropped to where it is over half: its blue (`blue`) or
    its dark on white (`dark`)."""
    px = np.asarray(picture.convert("RGBA")).astype(np.float32) / 255
    if mode == "blue":
        shape = px[..., 3] * np.clip((px[..., 2] - np.maximum(px[..., 0], px[..., 1])) * 1.4, 0, 1)
    else:
        lum = px[..., :3].mean(-1)
        ink = np.median(lum[lum < 0.75])
        shape = px[..., 3] * np.clip((1.0 - lum) / (1.0 - ink), 0, 1)
    ys, xs = np.nonzero(shape > 0.5)
    return shape[ys.min() : ys.max() + 1, xs.min() : xs.max() + 1]


def redrawn(tile: np.ndarray, picture: Image.Image, centre, size, mode: str, fill: bool) -> np.ndarray:
    """The tile with a symbol redrawn: the old one's ink (its middle `centre`, `size` wide and high) cleared and
    the picture's shape put in its place, centred where the old ink was, as large (its own aspect at the same
    area, or the old box exactly with `fill`), scaled down in two steps (to 8 times the size by Lanczos, then by
    area) so that its edges are smooth. The tile is rolled to have the symbol whole in its middle meanwhile."""
    cx, cy = centre
    w, h = size
    tile = tile.astype(np.float32)
    th, tw = tile.shape[:2]
    cov = np.clip((tile[..., 0] - GROUND[0]) / (INK[0] - GROUND[0]), 0, 1)
    sx, sy = int(round(tw / 2 - cx)), int(round(th / 2 - cy))
    cov = np.roll(np.roll(cov, sx, axis=1), sy, axis=0)
    mx, my = cx + sx, cy + sy
    x0, y0 = int(round(mx - w / 2)), int(round(my - h / 2))
    cov[y0 - CLEAR : y0 + int(h) + CLEAR, x0 - CLEAR : x0 + int(w) + CLEAR] = 0

    shape = shape_of(picture, mode)
    if fill:
        nw, nh = int(round(w)), int(round(h))
    else:
        aspect = shape.shape[1] / shape.shape[0]
        nh = int(round((w * h / aspect) ** 0.5))
        nw = int(round(nh * aspect))
    big = Image.fromarray((shape * 255).astype(np.uint8)).resize((nw * 8, nh * 8), Image.LANCZOS)
    small = np.asarray(big.resize((nw, nh), Image.BOX)).astype(np.float32) / 255
    nx0, ny0 = int(round(mx - nw / 2)), int(round(my - nh / 2))
    region = cov[ny0 : ny0 + nh, nx0 : nx0 + nw]
    cov[ny0 : ny0 + nh, nx0 : nx0 + nw] = np.maximum(region, small)

    cov = np.roll(np.roll(cov, -sy, axis=0), -sx, axis=1)
    return painted(cov)


def pattern_tile(art: Path) -> Image.Image:
    """The tile: the lattice of the pattern picture's symbols, then the heart and the crown redrawn."""
    tile = lattice_tile(Image.open(art / "pattern.png"))
    for name, centre, size, mode, fill in SYMBOLS:
        tile = redrawn(tile, Image.open(art / name), centre, size, mode, fill)
    return Image.fromarray(tile, "RGB")
