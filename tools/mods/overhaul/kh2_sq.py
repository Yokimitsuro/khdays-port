"""KH2's 2D layouts (2LD: LAYD + SEQD sequences + IMGZ textures): an animation group drawn at a frame,
from the HD collection's textures (their size a multiple of the PS2's)."""

import struct
import numpy as np
from PIL import Image


def bar(path):
    b = open(path, "rb").read()
    n = struct.unpack_from("<I", b, 4)[0]
    out = []
    for i in range(n):
        t, name, off, size = struct.unpack_from("<I4sII", b, 16 + 16 * i)
        out.append((t, name.rstrip(b"\0").decode("latin1"), b[off : off + size]))
    return out


class Seq:
    def __init__(self, b, o):
        assert b[o : o + 4] == b"SEQD"
        h = struct.unpack_from("<12I", b, o)
        self.sprites = [struct.unpack_from("<5i2f4I", b, o + h[3] + 44 * i) for i in range(h[2])]
        self.parts = [struct.unpack_from("<5i", b, o + h[5] + 20 * i) for i in range(h[4])]
        self.groups = [struct.unpack_from("<2h", b, o + h[7] + 4 * i) for i in range(h[6])]
        self.anims = []
        for i in range(h[8]):
            a = o + h[9] + 144 * i
            iv = struct.unpack_from("<36i", b, a)
            fv = struct.unpack_from("<36f", b, a)
            self.anims.append(
                dict(
                    flags=iv[0],
                    group=iv[1],
                    start=iv[2],
                    end=iv[3],
                    x=(iv[4], iv[5]),
                    y=(iv[6], iv[7]),
                    px=(iv[8], iv[9]),
                    py=(iv[10], iv[11]),
                    rot=(fv[12], fv[13], fv[14]),
                    scale=(fv[18], fv[19]),
                    sx=(fv[20], fv[21]),
                    sy=(fv[22], fv[23]),
                    curve=fv[24:28],
                    bx=(fv[28], fv[29]),
                    by=(fv[30], fv[31]),
                    bcount=iv[32],
                    blend=iv[33],
                    col=(iv[34] & 0xFFFFFFFF, iv[35] & 0xFFFFFFFF),
                )
            )
        self.agroups = [struct.unpack_from("<4h7i", b, o + h[11] + 36 * i) for i in range(h[10])]


class Layout:
    def __init__(self, path, textures):
        self.entries = bar(path)
        self.textures = textures  # HD RGBA images, in the IMGZ's order
        self.layd = {}
        for t, name, data in self.entries:
            if data[:4] == b"LAYD":
                self.layd[name] = data

    def seqs(self, name):
        L = self.layd[name]
        n, o = struct.unpack_from("<2I", L, 0x18)
        return [Seq(L, so) for so in struct.unpack_from("<%dI" % n, L, o)]

    def props(self, name):
        L = self.layd[name]
        n1, o1, n2, o2 = struct.unpack_from("<4I", L, 8)
        return [struct.unpack_from("<6i", L, o1 + 24 * i) for i in range(n1)], [
            struct.unpack_from("<5i", L, o2 + 20 * i) for i in range(n2)
        ]


def rgba(c):
    return np.array([c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, (c >> 24) & 0xFF], float) / 128.0


def lerp(a, t):
    return a[0] + (a[1] - a[0]) * t


def draw_anim(canvas, seq, tex, k, a, frame, origin, hd, recolour=None, info=None):
    """One animation of a group at `frame` (its own frames) onto `canvas` (RGBA float array), its origin
    at `origin` (PS2 pixels) times `hd`."""
    span = max(a["end"] - a["start"], 1)
    t = min(max((frame - a["start"]) / span, 0.0), 1.0)
    x, y = lerp(a["x"], t), lerp(a["y"], t)
    px, py = lerp(a["px"], t), lerp(a["py"], t)
    sc = lerp(a["scale"], t)
    sx, sy = lerp(a["sx"], t) * sc, lerp(a["sy"], t) * sc
    col = rgba(a["col"][0]) * (1 - t) + rgba(a["col"][1]) * t
    g0, gn = seq.groups[a["group"]]
    H, W = canvas.shape[:2]
    for pi in range(g0, g0 + gn):
        l, tp, r, bt, si = seq.parts[pi]
        sp = seq.sprites[si]
        _, u0, v0, u1, v1, du, dv, c0, c1, c2, c3 = sp
        # (a 0-wide sprite: one texel, stretched; sampled at its middle)
        ua, ub = (min(u0, u1), max(u0, u1)) if u1 != u0 else (u0 + 0.25, u0 + 0.75)
        va, vb = (min(v0, v1), max(v0, v1)) if v1 != v0 else (v0 + 0.25, v0 + 0.75)
        src = tex.crop(
            (
                round(ua * hd),
                round(va * hd),
                max(round(ub * hd), round(ua * hd) + 1),
                max(round(vb * hd), round(va * hd) + 1),
            )
        )
        if u1 < u0:
            src = src.transpose(Image.FLIP_LEFT_RIGHT)
        if v1 < v0:
            src = src.transpose(Image.FLIP_TOP_BOTTOM)
        w, h = abs(r - l), abs(bt - tp)
        if w == 0 or h == 0:
            continue
        img = np.asarray(src.resize((max(1, round(w * hd)), max(1, round(h * hd))), Image.BILINEAR), float) / 255.0
        if r < l:
            img = img[:, ::-1]
        if bt < tp:
            img = img[::-1]
        # the sprite's corner colours (top left, top right, bottom left, bottom right), bilinear
        cs = [rgba(c) for c in (c0, c1, c2, c3)]
        hh, ww = img.shape[:2]
        fy = np.linspace(0, 1, hh)[:, None, None]
        fx = np.linspace(0, 1, ww)[None, :, None]
        corner = cs[0] * (1 - fx) * (1 - fy) + cs[1] * fx * (1 - fy) + cs[2] * (1 - fx) * fy + cs[3] * fx * fy
        img = img * corner * col
        if recolour is not None:
            img = recolour(img, k)
        img = np.clip(img, 0, 1)
        # place: the part's corners about the pivot, scaled (no rotation in these menus' parts)
        X0 = origin[0] + x + (min(l, r) + px) * sx
        Y0 = origin[1] + y + (min(tp, bt) + py) * sy
        dw, dh = round(w * abs(sx) * hd), round(h * abs(sy) * hd)
        if dw <= 0 or dh <= 0:
            continue
        if (dw, dh) != (ww, hh):
            img = (
                np.asarray(
                    Image.fromarray((img * 255).astype(np.uint8), "RGBA").resize((dw, dh), Image.BILINEAR), float
                )
                / 255
            )
        X, Y = round(X0 * hd), round(Y0 * hd)
        xa, ya = max(X, 0), max(Y, 0)
        xb, yb = min(X + dw, W), min(Y + dh, H)
        if xb <= xa or yb <= ya:
            continue
        piece = img[ya - Y : yb - Y, xa - X : xb - X]
        dst = canvas[ya:yb, xa:xb]
        al = piece[..., 3:4]
        if a["blend"] == 1:  # additive
            dst[..., :3] += piece[..., :3] * al
            dst[..., 3:4] = np.maximum(dst[..., 3:4], al)
        else:
            dst[..., :3] = piece[..., :3] * al + dst[..., :3] * (1 - al)
            dst[..., 3:4] = al + dst[..., 3:4] * (1 - al)
        if info is not None:
            info.append((k, pi, si, X0, Y0, w * sx, h * sy))


def draw_group(canvas, seq, tex, gi, frame, origin, hd=2, recolour=None, info=None):
    ai, n = seq.agroups[gi][0], seq.agroups[gi][1]
    for k in range(ai, ai + n):
        a = seq.anims[k]
        f = frame
        if f < a["start"] or f > a["end"]:
            continue
        draw_anim(canvas, seq, tex, k, a, f, origin, hd, recolour, info)


def draw_sprite_group(canvas, seq, tex, g, origin, hd=2, colour=0x80808080):
    a = dict(
        flags=0,
        group=g,
        start=0,
        end=0,
        x=(0, 0),
        y=(0, 0),
        px=(0, 0),
        py=(0, 0),
        rot=(0, 0, 0),
        scale=(1, 1),
        sx=(1, 1),
        sy=(1, 1),
        curve=(0, 0, 0, 0),
        bx=(0, 0),
        by=(0, 0),
        bcount=0,
        blend=0,
        col=(colour, colour),
    )
    draw_anim(canvas, seq, tex, -1, a, 0, origin, hd)
