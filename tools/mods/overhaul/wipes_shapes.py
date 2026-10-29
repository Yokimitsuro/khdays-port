"""The room change's irises of the two worlds KH2 has none for (wipes.py): Wonderland's spade and Neverland's
feather, drawn as vectors in place of the DS's 64 x 32 pictures.

The drawings are in the units of the quad the game stretches the wipe's texture over: 128 x 96 at the wipe's
size 1, y down (a texel of the DS's texture is 2 x 3 units). Each takes the DS's symbol's place and size, so the
wipe opens and closes as it did, with clean geometry: mirrored halves where the symbol is symmetric, smooth
curves (cubic Beziers, centripetal Catmull-Rom splines), lines of a set width. They are filled on a canvas SS
dots a unit (the hole 255, the cover 0) and averaged down to the texture (supersampling).
"""

from __future__ import annotations

import math

import numpy as np
from PIL import Image, ImageDraw

W, H = 128, 96  # the quad's units at the wipe's size 1
SS = 64  # the canvas's dots a unit: 8192 x 6144, a whole number of them to each of the texture's texels


def cubic(p0, p1, p2, p3, n=48):
    """A cubic Bezier's points, n of them, the ends included."""
    t = np.linspace(0, 1, n)[:, None]
    p0, p1, p2, p3 = map(np.asarray, (p0, p1, p2, p3))
    return list(map(tuple, (1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t**2 * p2 + t**3 * p3))


def spline(pts, n=32):
    """A centripetal Catmull-Rom spline through `pts` (open: its ends extended straight), n points a span."""
    p = [np.asarray(q, float) for q in pts]
    p = [2 * p[0] - p[1]] + p + [2 * p[-1] - p[-2]]
    out = []
    for i in range(1, len(p) - 2):
        p0, p1, p2, p3 = p[i - 1], p[i], p[i + 1], p[i + 2]
        t0 = 0.0
        t1 = t0 + max(np.linalg.norm(p1 - p0), 1e-6) ** 0.5
        t2 = t1 + max(np.linalg.norm(p2 - p1), 1e-6) ** 0.5
        t3 = t2 + max(np.linalg.norm(p3 - p2), 1e-6) ** 0.5
        for t in np.linspace(t1, t2, n, endpoint=False):
            a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1
            a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2
            a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3
            b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2
            b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3
            out.append(tuple((t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2))
    out.append(tuple(p[-2]))
    return out


def mirror(pts, cx=64.0):
    """The path mirrored about x = cx, in reverse order (to close a symmetric outline)."""
    return [(2 * cx - x, y) for x, y in reversed(pts)]


def resample(pts, step):
    """Points along the polyline `pts` every `step` units (the ends kept)."""
    p = np.asarray(pts, float)
    d = np.r_[0, np.cumsum(np.linalg.norm(np.diff(p, axis=0), axis=1))]
    if d[-1] == 0:
        return p
    s = np.linspace(0, d[-1], max(2, int(d[-1] / step) + 1))
    return np.c_[np.interp(s, d, p[:, 0]), np.interp(s, d, p[:, 1])]


def taper(w0, w1, ends=0.15, start_cap=0.0, end_cap=0.0):
    """A line's width from w0 to w1 along it (0-1), narrowing to start_cap/end_cap (fractions of the width)
    over its first and last `ends`."""

    def f(u):
        w = w0 + (w1 - w0) * u
        if u < ends:
            w *= start_cap + (1 - start_cap) * math.sin(math.pi / 2 * u / ends)
        if u > 1 - ends:
            w *= end_cap + (1 - end_cap) * math.sin(math.pi / 2 * (1 - u) / ends)
        return w

    return f


class Canvas:
    """The drawing's mask, SS dots a unit: 255 the hole, 0 the cover."""

    def __init__(self):
        self.im = Image.new("L", (W * SS, H * SS), 0)
        self.dr = ImageDraw.Draw(self.im)

    def fill(self, pts, v=255):
        self.dr.polygon([(x * SS, y * SS) for x, y in pts], fill=v)

    def disk(self, cx, cy, r, v=255):
        self.dr.ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], fill=v)

    def stroke(self, pts, width, v=0, step=0.15):
        """A line along `pts`, width(u) units wide at u (0-1) along it: a quad between each two points a
        step apart, a round cap on each."""
        p = resample(pts, step)
        n = len(p)
        r = np.array([width(i / max(n - 1, 1)) / 2 for i in range(n)])
        for i in range(n - 1):
            a, b = p[i], p[i + 1]
            d = b - a
            length = np.hypot(*d)
            if length == 0:
                continue
            nrm = np.array([-d[1], d[0]]) / length
            quad = [tuple(a + nrm * r[i]), tuple(b + nrm * r[i + 1]), tuple(b - nrm * r[i + 1]), tuple(a - nrm * r[i])]
            self.fill(quad, v)
        for i in range(n):
            if r[i] > 0:
                self.disk(p[i][0], p[i][1], r[i], v)

    def reduced(self, size):
        """The mask at `size`, each of its dots the average of the canvas's under it."""
        w, h = self.im.size
        return self.im.reduce((w // size[0], h // size[1]))


def spade(c: Canvas) -> None:
    """Wonderland: the spade, its stem flaring into a foot; the right half, then mirrored."""
    cx = 64.0
    right = cubic((cx, 11.0), (cx + 7.0, 22.0), (cx + 25.5, 30.0), (cx + 26.0, 47.0))
    right += cubic((cx + 26.0, 47.0), (cx + 26.4, 59.0), (cx + 18.0, 66.5), (cx + 10.0, 64.5))[1:]
    right += cubic((cx + 10.0, 64.5), (cx + 6.0, 63.5), (cx + 3.4, 61.0), (cx + 2.6, 58.5))[1:]
    right += cubic((cx + 2.6, 58.5), (cx + 3.0, 70.0), (cx + 4.4, 78.5), (cx + 10.5, 83.0))[1:]
    right += cubic((cx + 10.5, 83.0), (cx + 11.5, 83.7), (cx + 11.0, 84.6), (cx + 9.5, 84.6))[1:]
    right += [(cx, 84.6)]
    c.fill(right + mirror(right))


def feather(c: Canvas) -> None:
    """Neverland: Peter Pan's feather, from its quill at the lower left to its tip at the upper right."""
    base, tip = np.array([33.0, 72.0]), np.array([101.5, 11.5])
    axis = tip - base
    length = np.linalg.norm(axis)
    d = axis / length
    nrm = np.array([d[1], -d[0]])  # to the upper left

    def at(s, off):
        bow = 3.0 * math.sin(math.pi * s)  # the shaft bowed a little to the lower right
        return tuple(base + d * (s * length) + nrm * (off - bow))

    # the vane, wider above the shaft than below it
    upper, lower = [], []
    for s in np.linspace(0, 1, 160):
        wu = 11.5 * math.sin(math.pi * min(s / 0.98, 1)) ** 0.75 * (1 - 0.25 * s)
        wl = 9.0 * math.sin(math.pi * min(s / 0.98, 1)) ** 0.8 * (1 - 0.15 * s)
        upper.append(at(s, wu))
        lower.append(at(s, -wl))
    c.fill(upper + lower[::-1])
    # notches in the vane, as a feather's barbs part
    for s0, side, depth in ((0.22, -1, 6.0), (0.36, 1, 7.0), (0.55, -1, 5.0)):
        p0, p1 = at(s0, side * 13.0), at(s0 + 0.035, side * 13.0)
        q = at(s0 - 0.02, side * (11.5 if side > 0 else 9.0) - side * depth)
        c.fill([p0, p1, q], v=0)
    # the shaft: dark along the vane, the quill below it
    c.stroke([at(s, 0) for s in np.linspace(0.0, 0.86, 80)], lambda u: 1.5 * (1 - u) + 0.3, v=0)
    c.stroke(
        spline([tuple(base), (26.5, 79.0), (20.5, 85.5)], n=40),
        taper(2.6, 1.2, ends=0.15, start_cap=1.0, end_cap=0.6),
        v=255,
    )
    # two loose barbs at the vane's foot
    c.stroke(cubic((30.0, 70.5), (27.0, 68.0), (25.5, 64.5), (25.0, 61.0)), taper(1.8, 0.4, ends=0.3), v=255)
    c.stroke(cubic((33.0, 74.5), (33.5, 77.5), (36.0, 80.0), (39.0, 81.0)), taper(1.8, 0.4, ends=0.3), v=255)


SHAPES = {"aw": spade, "pp": feather}


def hole(world: str, size: tuple[int, int]) -> Image.Image:
    """`world`'s drawing at the texture's `size` (L: 255 the hole, 0 the cover)."""
    c = Canvas()
    SHAPES[world](c)
    return c.reduced(size)
