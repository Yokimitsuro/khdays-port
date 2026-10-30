"""KH2's 2D sequences (SEQD in a 2LD's LAYD) drawn at any scale: x and y apart (the PS2's dots are not
square), from the HD collection's textures (tex_k of their texels to a PS2 dot)."""

import numpy as np
from PIL import Image
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from kh2_sq import Layout, rgba, lerp  # noqa: E402, F401


def green_to_grey(c):
    """A tint's colour (r, g, b, a: 1.0 = 128) with its green made grey of the same lightness."""
    r, g, b, a = c
    if g > r * 1.1 and g >= b * 0.95 and g > 0.02:
        y = 0.299 * r + 0.587 * g + 0.114 * b
        return np.array([y, y, y, a])
    return c


def draw_group(canvas, seq, tex, gi, frame, origin, kx, ky, tex_k=2.0, tint=None, skip=(), only=None):
    """Animation group `gi` at `frame` onto `canvas` (H x W x 4 floats, premultiplied off: straight
    alpha), its origin at `origin` (the canvas's pixels); `tint(colour, anim_index, sprite_group)` may
    change a tint (the anim's and the sprites' corner colours alike)."""
    ai, n = seq.agroups[gi][0], seq.agroups[gi][1]
    for k in range(ai, ai + n):
        a = seq.anims[k]
        if frame < a["start"] or frame > a["end"]:
            continue
        if a["group"] in skip or (only is not None and a["group"] not in only):
            continue
        draw_anim(canvas, seq, tex, k, a, frame, origin, kx, ky, tex_k, tint)


def draw_anim(canvas, seq, tex, k, a, frame, origin, kx, ky, tex_k, tint):
    span = max(a["end"] - a["start"], 1)
    t = min(max((frame - a["start"]) / span, 0.0), 1.0)
    x, y = lerp(a["x"], t), lerp(a["y"], t)
    px, py = lerp(a["px"], t), lerp(a["py"], t)
    sc = lerp(a["scale"], t)
    sx, sy = lerp(a["sx"], t) * sc, lerp(a["sy"], t) * sc
    col = rgba(a["col"][0]) * (1 - t) + rgba(a["col"][1]) * t
    if tint is not None:
        col = tint(col, k, a["group"])
    g0, gn = seq.groups[a["group"]]
    H, W = canvas.shape[:2]
    for pi in range(g0, g0 + gn):
        l, tp, r, bt, si = seq.parts[pi]
        _, u0, v0, u1, v1, du, dv, c0, c1, c2, c3 = seq.sprites[si]
        ua, ub = (min(u0, u1), max(u0, u1)) if u1 != u0 else (u0 + 0.25, u0 + 0.75)
        va, vb = (min(v0, v1), max(v0, v1)) if v1 != v0 else (v0 + 0.25, v0 + 0.75)
        box = (
            round(ua * tex_k),
            round(va * tex_k),
            max(round(ub * tex_k), round(ua * tex_k) + 1),
            max(round(vb * tex_k), round(va * tex_k) + 1),
        )
        src = tex.crop(box)
        if u1 < u0:
            src = src.transpose(Image.FLIP_LEFT_RIGHT)
        if v1 < v0:
            src = src.transpose(Image.FLIP_TOP_BOTTOM)
        w, h = abs(r - l), abs(bt - tp)
        if w == 0 or h == 0:
            continue
        X0 = origin[0] + (x + (min(l, r) + px) * sx) * kx
        Y0 = origin[1] + (y + (min(tp, bt) + py) * sy) * ky
        X1 = origin[0] + (x + (max(l, r) + px) * sx) * kx
        Y1 = origin[1] + (y + (max(tp, bt) + py) * sy) * ky
        Xa, Ya, Xb, Yb = round(X0), round(Y0), round(X1), round(Y1)
        dw, dh = Xb - Xa, Yb - Ya
        if dw <= 0 or dh <= 0:
            continue
        img = np.asarray(src.resize((dw, dh), Image.BICUBIC if min(src.size) > 1 else Image.BILINEAR), float) / 255.0
        if r < l:
            img = img[:, ::-1]
        if bt < tp:
            img = img[::-1]
        cs = [rgba(c) for c in (c0, c1, c2, c3)]
        if tint is not None:
            cs = [tint(c, k, a["group"]) for c in cs]
        fy = np.linspace(0, 1, dh)[:, None, None]
        fx = np.linspace(0, 1, dw)[None, :, None]
        corner = cs[0] * (1 - fx) * (1 - fy) + cs[1] * fx * (1 - fy) + cs[2] * (1 - fx) * fy + cs[3] * fx * fy
        img = np.clip(img * corner * col, 0, 1)
        xa, ya = max(Xa, 0), max(Ya, 0)
        xb, yb = min(Xb, W), min(Yb, H)
        if xb <= xa or yb <= ya:
            continue
        piece = img[ya - Ya : yb - Ya, xa - Xa : xb - Xa]
        dst = canvas[ya:yb, xa:xb]
        al = piece[..., 3:4]
        if a["blend"] == 1:
            dst[..., :3] = np.clip(dst[..., :3] * dst[..., 3:4] + piece[..., :3] * al, 0, None)
            na = np.maximum(dst[..., 3:4], al)
            dst[..., :3] /= np.where(na > 1e-6, na, 1)
            dst[..., 3:4] = na
        else:
            na = al + dst[..., 3:4] * (1 - al)
            dst[..., :3] = (piece[..., :3] * al + dst[..., :3] * dst[..., 3:4] * (1 - al)) / np.where(na > 1e-6, na, 1)
            dst[..., 3:4] = na


def to_image(canvas):
    return Image.fromarray((np.clip(canvas, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")
