"""Days's own worlds for the save page's rows: the Holo-Missions world map's models (UI/cm/cm.p2), each drawn once
as a picture (300 x 260): posed by its animation's first frame, seen from the front, orthographic, its textures
four times larger (premultiplied bicubic), drawn three times larger than the picture and reduced (premultiplied
Lanczos)."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))  # tools/mods: nitro3d.py
import campsave_nsbmd as nsbmd  # noqa: E402
from ds_tex import materials  # noqa: E402
from nitro3d import p2_entries  # noqa: E402

# The worlds' models: cm.p2's entries (each a KAPH with the model and its animation)
ENTRIES = {"castle": 13, "tt": 62, "al": 0, "bb": 1, "he": 25, "aw": 63, "nm": 14, "pp": 65}
W, H = 300, 260  # the picture
SS = 3  # drawn this many times larger, then reduced
TEXTURE_UP = 4  # the textures this many times larger


# ---- The renderer: orthographic, z-buffered, textured (texels at the nearest), alpha-tested, shaded -----------


def texel_index(u, n, repeat, flip):
    u = np.floor(u).astype(int)
    if repeat:
        if flip:
            period = 2 * n
            u = np.mod(u, period)
            return np.where(u >= n, period - 1 - u, u)
        return np.mod(u, n)
    return np.clip(u, 0, n - 1)


def texels(mt, uv):
    """RGBA at texel coordinates `uv` (N x 2) of material `mt` (its texture's repeat and flip)."""
    img = mt.get("img") if mt else None
    if img is None:
        out = np.full((len(uv), 4), 255, np.uint8)
        out[:, :3] = 170
        return out
    h, w = img.shape[:2]
    ti = mt.get("teximg", 0)
    ss, st = mt.get("scale", (1.0, 1.0))
    s = texel_index(uv[:, 0] * ss, w, ti >> 16 & 1, ti >> 18 & 1)
    t = texel_index(uv[:, 1] * st, h, ti >> 17 & 1, ti >> 19 & 1)
    return img[t, s]


def bgr(v):
    v = np.asarray(v, int)
    return np.stack([(v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31],
                    -1).astype(float)  # fmt: skip


def vertex_colours(cols, tri_mat, tris, mats, n):
    """Each vertex's colour (RGB float): its COLOR, else its material's diffuse."""
    out = np.full((n, 3), 255.0)
    have = np.zeros(n, bool)
    if len(cols) == n:
        k = cols >= 0
        out[k] = bgr(cols[k])
        have = k
    tt = np.array(tris, int).reshape(-1, 3)
    for t, (a, b, c) in enumerate(tt):
        m = tri_mat[t] if t < len(tri_mat) else -1
        if 0 <= m < len(mats):
            d = bgr(mats[m].get("diffuse", 0x7FFF))
            for v in (a, b, c):
                if v < n and not have[v]:
                    out[v] = d
    return out


def shade_texels(mt, tex, vcol):
    """(RGB, alpha) of texels `tex` (N x 4) over the vertex colour `vcol` (N x 3) by the material's mode."""
    if mt is None or mt.get("img") is None:
        return vcol, np.full(len(vcol), 255.0)
    a = tex[:, 3].astype(float)
    t = tex[:, :3].astype(float)
    if mt.get("mode", 0) == 1:  # decal: the texture over the vertex colour, the polygon whole
        w = (a / 255)[:, None]
        return t * w + vcol * (1 - w), np.full(len(vcol), 255.0)
    return t * vcol / 255, a


def view_matrix(yaw, pitch=0.0):
    cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    ry = np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]])
    rx = np.array([[1, 0, 0], [0, cp, sp], [0, -sp, cp]])
    return ry @ rx


def render(P, tris, uvs, tri_mat, mats, w, h, scale, centre, light, cols):
    """The model seen from the front (orthographic, its centre `centre` at the picture's), `scale` dots a unit,
    z-buffered, each face shaded by its normal against `light`. Returns an RGBA array."""
    img = np.zeros((h, w, 4), np.uint8)
    zbuf = np.full((h, w), -np.inf)
    origin = (w / 2, h / 2)
    R = view_matrix(0.0, 0.0)
    Q = (P - centre) @ R
    X = origin[0] + Q[:, 0] * scale
    Y = origin[1] - Q[:, 1] * scale
    Z = Q[:, 2]
    L = np.array(light) / np.linalg.norm(light)
    T = np.array(tris, int).reshape(-1, 3)
    have_uv = len(uvs) == len(P)
    VC = vertex_colours(np.asarray(cols, int), tri_mat, tris, mats, len(P))
    for k, (a, b, c) in enumerate(T):
        if max(a, b, c) >= len(P):
            continue
        xs = np.array([X[a], X[b], X[c]])
        ys = np.array([Y[a], Y[b], Y[c]])
        x0, x1 = int(max(np.floor(xs.min()), 0)), int(min(np.ceil(xs.max()), w - 1))
        y0, y1 = int(max(np.floor(ys.min()), 0)), int(min(np.ceil(ys.max()), h - 1))
        if x1 < x0 or y1 < y0:
            continue
        den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(den) < 1e-12:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
        l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not inside.any():
            continue
        z = l0 * Z[a] + l1 * Z[b] + l2 * Z[c]
        sub = zbuf[y0 : y1 + 1, x0 : x1 + 1]
        front = inside & (z > sub)
        if not front.any():
            continue
        m = tri_mat[k] if k < len(tri_mat) else -1
        mt = mats[m] if 0 <= m < len(mats) else None
        if have_uv:
            UV = l0[front][:, None] * uvs[a] + l1[front][:, None] * uvs[b] + l2[front][:, None] * uvs[c]
        else:
            UV = np.zeros((front.sum(), 2))
        vc = l0[front][:, None] * VC[a] + l1[front][:, None] * VC[b] + l2[front][:, None] * VC[c]
        rgb_f, alpha = shade_texels(mt, texels(mt, UV), vc)
        ok = alpha >= 128
        if mt is not None and mt.get("alpha", 31) == 0:
            ok[:] = False
        n = np.cross((P[b] - P[a]) @ R, (P[c] - P[a]) @ R)
        nl = np.linalg.norm(n)
        shade = 0.45 + 0.55 * abs(n @ L) / nl if nl > 0 else 1.0
        ys_i, xs_i = np.nonzero(front)
        ys_i, xs_i = ys_i[ok], xs_i[ok]
        if len(ys_i) == 0:
            continue
        rgb = np.clip(rgb_f[ok] * shade, 0, 255).astype(np.uint8)
        img[y0 + ys_i, x0 + xs_i, :3] = rgb
        img[y0 + ys_i, x0 + xs_i, 3] = 255
        sub[ys_i, xs_i] = z[front][ok]
    return img


# ---- The icons --------------------------------------------------------------------------------------------------


def resized_premultiplied(a: np.ndarray, size: tuple[int, int], resample) -> np.ndarray:
    """A straight-alpha picture (floats 0-1) resized with its colours premultiplied (no dark fringes), each
    channel as 8 bits, then straight again."""
    a = a.copy()
    a[..., :3] *= a[..., 3:4]
    big = np.stack([np.asarray(Image.fromarray((a[..., c] * 255).astype(np.uint8), "L").resize(size, resample),
                               float) / 255 for c in range(4)], -1)  # fmt: skip
    al = big[..., 3:4]
    rgb = np.where(al > 1e-3, big[..., :3] / np.maximum(al, 1e-3), 0)
    return np.clip(np.concatenate([rgb, al], -1), 0, 1)


def icon(entry: bytes) -> Image.Image:
    """A world map model (a KAPH: its BMD0, its BCA0) drawn as a W x H picture."""
    sections = nsbmd.kaph_sections(entry)
    bmd = entry[sections[7][0] :]
    _, m = nsbmd.bmd_models(bmd)[0]
    mats = materials(bmd, m, nsbmd.tex0_offset(bmd))
    for mt in mats:
        if mt.get("img") is not None:
            im = Image.fromarray(mt["img"], "RGBA")
            up = resized_premultiplied(np.asarray(im, float) / 255, (im.width * TEXTURE_UP, im.height * TEXTURE_UP),
                                       Image.BICUBIC)  # fmt: skip
            mt["img"] = (up * 255).astype(np.uint8)
            sx, sy = mt.get("scale", (1.0, 1.0))
            mt["scale"] = (sx * TEXTURE_UP, sy * TEXTURE_UP)
    pose = nsbmd.pose(nsbmd.parse_bca(entry, sections[0][0])[0], 0) if sections[0] else None
    P, T, info = nsbmd.model_geometry(bmd, m, pose=pose)
    lo, hi = P.min(0), P.max(0)
    centre = (lo + hi) / 2
    k = min(W * SS * 0.94 / max(hi[0] - lo[0], 1e-3), H * SS * 0.94 / max(hi[1] - lo[1], 1e-3))
    img = render(P, T, info["uvs"], info["tri_mat"], mats, W * SS, H * SS, k, centre, (0, 0, 1), info["cols"])
    small = resized_premultiplied(np.asarray(Image.fromarray(img, "RGBA"), float) / 255, (W, H), Image.LANCZOS)
    return Image.fromarray((small * 255).astype(np.uint8), "RGBA")


def icons(cm_p2: bytes, worlds: list[str]) -> dict[str, Image.Image]:
    """The worlds' pictures (their codes: saveworld.h's), from UI/cm/cm.p2."""
    entries = p2_entries(cm_p2)
    return {w: icon(entries[ENTRIES[w]]) for w in worlds}
