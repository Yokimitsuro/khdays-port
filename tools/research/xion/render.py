"""A small orthographic z-buffered renderer for decoded DS models (numpy + PIL).

Vertices arrive already in model space (the model's own rest pose); each mesh
carries its material's texture (see tex.py) or only its vertex colours.
"""

from __future__ import annotations

import numpy as np
from PIL import Image, ImageDraw


def posed_positions(model, palette=None) -> list[np.ndarray]:
    """Each mesh's vertices through its palette matrix (stored column-major:
    m[col * 4 + row], so p' = M p reads the flat array as its transpose)."""
    pal = palette if palette is not None else model.palette
    out = []
    for mesh in model.meshes:
        m = np.array([pal[v.matrix] for v in mesh.vertices], dtype=np.float64).reshape(-1, 4, 4)
        p = np.array([v.position for v in mesh.vertices], dtype=np.float64)
        out.append(np.einsum("nji,nj->ni", m[:, :3, :3], p) + m[:, 3, :3])
    return out


def _texture_sample(tex, u: np.ndarray, v: np.ndarray) -> np.ndarray:
    """Nearest-texel RGBA lookup; uv in texels."""
    h, w = tex.height, tex.width
    xs = np.clip(u.astype(np.int64) % w, 0, w - 1)
    ys = np.clip(v.astype(np.int64) % h, 0, h - 1)
    px = np.frombuffer(tex.rgba, dtype=np.uint8).reshape(h, w, 4)
    return px[ys, xs]


def render(
    model, textures: dict, yaw_deg: float, size: int = 512, bg=(255, 255, 255), turn: float = 0.0, zoom: float = 0.92
) -> Image.Image:
    """Render the model turned `yaw_deg` about the up axis. `textures` maps a texture name to tex.Texture."""
    pos = posed_positions(model)
    allp = np.concatenate(pos) if pos else np.zeros((1, 3))
    # The DS's models are Y up in this engine; X to the character's left, Z to its front.
    a = np.deg2rad(yaw_deg)
    rot = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    pts = allp @ rot.T
    lo, hi = pts.min(0), pts.max(0)
    centre = (lo + hi) / 2
    span = max(hi[0] - lo[0], hi[1] - lo[1], 1e-6)
    scale = size * zoom / span
    zbuf = np.full((size, size), -1e9)
    pix = np.zeros((size, size, 3), dtype=np.float64)
    light = np.array([0.35, 0.55, 0.75])
    light = light / np.linalg.norm(light)

    for mesh, mp in zip(model.meshes, pos):
        if not mesh.vertices:
            continue
        p = (mp @ rot.T - centre) * scale
        x = size / 2 + p[:, 0]
        y = size / 2 - p[:, 1]
        z = p[:, 2]
        idx = np.array(mesh.indices, dtype=np.int64).reshape(-1, 3)
        tex = textures.get(mesh.texture)
        uv = np.array([v.texcoord for v in mesh.vertices], dtype=np.float64)
        base = np.array([v.color[:3] for v in mesh.vertices], dtype=np.float64) / 255.0
        for tri in idx:
            xi, yi, zi = x[tri], y[tri], z[tri]
            minx, maxx = int(np.floor(xi.min())), int(np.ceil(xi.max()))
            miny, maxy = int(np.floor(yi.min())), int(np.ceil(yi.max()))
            if maxx < 0 or maxy < 0 or minx >= size or miny >= size or maxx - minx > size or maxy - miny > size:
                continue
            x0 = max(minx, 0)
            x1 = min(maxx, size - 1)
            y0 = max(miny, 0)
            y1 = min(maxy, size - 1)
            if x1 < x0 or y1 < y0:
                continue
            d = (yi[1] - yi[2]) * (xi[0] - xi[2]) + (xi[2] - xi[1]) * (yi[0] - yi[2])
            if abs(d) < 1e-9:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((yi[1] - yi[2]) * (gx - xi[2]) + (xi[2] - xi[1]) * (gy - yi[2])) / d
            w1 = ((yi[2] - yi[0]) * (gx - xi[2]) + (xi[0] - xi[2]) * (gy - yi[2])) / d
            w2 = 1.0 - w0 - w1
            inside = (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
            if not inside.any():
                continue
            zz = w0 * zi[0] + w1 * zi[1] + w2 * zi[2]
            # face normal in view space for shading
            e1 = p[tri[1]] - p[tri[0]]
            e2 = p[tri[2]] - p[tri[0]]
            n = np.cross(e1, e2)
            ln = np.linalg.norm(n)
            if ln > 1e-9:
                n = n / ln
            shade = 0.55 + 0.45 * abs(float(n @ light))
            if tex is not None:
                uu = w0 * uv[tri[0], 0] + w1 * uv[tri[1], 0] + w2 * uv[tri[2], 0]
                vv = w0 * uv[tri[0], 1] + w1 * uv[tri[1], 1] + w2 * uv[tri[2], 1]
                col = _texture_sample(tex, uu, vv)[..., :3].astype(np.float64) / 255.0
            else:
                vc = w0[..., None] * base[tri[0]] + w1[..., None] * base[tri[1]] + w2[..., None] * base[tri[2]]
                col = np.broadcast_to(vc, vc.shape)
            sub = zbuf[y0 : y1 + 1, x0 : x1 + 1]
            mask = inside & (zz > sub)
            if mask.any():
                sub[mask] = zz[mask]
                pix[y0 : y1 + 1, x0 : x1 + 1][mask] = (col * shade)[mask]
    empty = ~(zbuf > -1e8)
    bg_arr = np.array(bg, dtype=np.float64) / 255.0
    pix[empty] = bg_arr
    arr = (np.clip(pix, 0, 1) * 255).astype(np.uint8)
    return Image.fromarray(arr, "RGB")


def turntable(model, textures: dict, size: int = 512, views=(0, 90, 180, 270)) -> Image.Image:
    imgs = [render(model, textures, yaw, size) for yaw in views]
    out = Image.new("RGB", (size * len(imgs), size), (255, 255, 255))
    for i, im in enumerate(imgs):
        out.paste(im, (i * size, 0))
        d = ImageDraw.Draw(out)
        d.text((i * size + 8, 8), f"yaw {views[i]}", fill=(0, 0, 0))
    return out
