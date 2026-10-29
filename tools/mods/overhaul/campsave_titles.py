"""The worlds' titles for the save page's rows (over each world's picture), each on a soft dark shadow:

- Days's own: the logos the game shows when Roxas first reaches a world (mission objects mi/ob/<id>: models whose
  animation assembles the title), at the animation's last frame, only the title's materials (its words: the
  glows, flashes, particles and pictures left out), every node shown and every material opaque;
- The World That Never Was's (the Castle's rows): the DS has no logo for it, so KH2's own (the HD collection's
  Jiminy's Journal page, menu/jm_top_eh.2ld, texture 0), its silver letters kept and the city behind them left out.

The logos' renderer: the model posed by its BCA0, orthographic and facing the camera, every triangle alpha-blended
back to front, textures sampled bilinearly, drawn SS times larger and reduced (premultiplied)."""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageFilter

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import campsave_nsbmd as nsbmd  # noqa: E402
from common import Inputs  # noqa: E402
from ds_tex import materials  # noqa: E402

# The logos: world code (saveworld.h's) -> the mission objects drawn together
LOGOS = {"tt": ["72"], "al": ["93"], "bb": ["C4", "4F"], "he": ["BB"], "aw": ["BE", "BD"], "nm": ["BA"],
         "pp": ["C0"]}  # fmt: skip
# Each logo's title materials (its words)
TITLE = {
    "tt": r"TTL_(t_1|wo)",  # the two T and "wilight own"
    "al": r"ALL_title01",  # AGRABAH
    "bb": r"alphabet0\d|castle05",  # "Beast's" and the pink "Castle"
    "he": r"title_0[12]",  # OLYMPUS, COLISEUM
    "aw": r"[ADELNRWo]|D02|N02|trump\d\d",  # the cards' soldiers holding WONDER LAND
    "nm": r"title_\d\d",  # the letters (and the pumpkin's O)
    "pp": r"title",  # NEVER LAND
}
TITLE_W, TITLE_H = 720, 240  # the most a title is drawn (dots; the page shows it about 3 times smaller)
SS = 4  # drawn this many times larger, then reduced
SHADOW = 10  # the soft dark shadow's reach
JOURNAL = "menu/jm_top_eh.2ld"  # KH2's Jiminy's Journal page (its texture 0: The World That Never Was's logo)


# ---- Days's logos -----------------------------------------------------------------------------------------------


def load(blob: bytes) -> dict:
    """A mission object (a KAPH): its model, materials and animations."""
    sections = nsbmd.kaph_sections(blob)
    bmd = blob[sections[7][0] :]
    _, m = nsbmd.bmd_models(bmd)[0]
    mats = materials(bmd, m, nsbmd.tex0_offset(bmd))
    anims = nsbmd.parse_bca(blob, sections[0][0]) if sections[0] else []
    return {"bmd": bmd, "m": m, "mats": mats, "anims": anims}


def all_visible(bmd: bytes, m: int) -> bytes:
    """The BMD0 with every node's visibility (the SBC's NODE, op 0x02) on: the logos hide their pieces at rest and
    their BVA0 shows them."""
    b = bytearray(bmd)
    sbc = m + struct.unpack_from("<I", b, m + 4)[0]
    end = m + struct.unpack_from("<I", b, m + 8)[0]
    sizes = {0x00: 1, 0x02: 3, 0x03: 2, 0x04: 2, 0x05: 2, 0x0A: 9, 0x0B: 1, 0x0C: 3, 0x0D: 3}
    p = sbc
    while p < end:
        op, var = b[p] & 0x1F, b[p] >> 5
        if op == 0x01:
            break
        if op == 0x02:
            b[p + 2] |= 1
        if op == 0x06:
            p += 4 + (1 if var in (1, 2) else 2 if var == 3 else 0)
        elif op in (0x07, 0x08):
            p += 2 + (1 if var in (1, 2) else 2 if var == 3 else 0)
        elif op == 0x09:
            p += 3 + 3 * b[p + 2]
        else:
            p += sizes.get(op, 1)
    return bytes(b)


def last_parts(objects: list[bytes]) -> list:
    """The logo's objects at their animation's last frame: [(points, triangles, info, materials)]."""
    parts = []
    for blob in objects:
        obj = load(blob)
        frames = obj["anims"][0].frames if obj["anims"] else 1
        pose = nsbmd.pose(obj["anims"][0], frames - 1) if obj["anims"] else None
        P, T, info = nsbmd.model_geometry(all_visible(obj["bmd"], obj["m"]), obj["m"], pose=pose)
        parts.append((P, np.array(T, int).reshape(-1, 3), info, obj["mats"]))
    return parts


def bilinear(mt: dict, uv: np.ndarray) -> np.ndarray:
    """RGBA (float, premultiplied) of material `mt` at texel coordinates `uv` (N x 2), bilinear."""
    img = mt["fimg"]
    h, w = img.shape[:2]
    ti = mt.get("teximg", 0)
    ss, st = mt.get("scale", (1.0, 1.0))
    u = uv[:, 0] * ss - 0.5
    v = uv[:, 1] * st - 0.5

    def wrap(i, n, rep, flip):
        if rep:
            if flip:
                i = np.mod(i, 2 * n)
                return np.where(i >= n, 2 * n - 1 - i, i)
            return np.mod(i, n)
        return np.clip(i, 0, n - 1)

    u0 = np.floor(u).astype(int)
    v0 = np.floor(v).astype(int)
    fu = (u - u0)[:, None]
    fv = (v - v0)[:, None]
    rs, rt, fs, ft = ti >> 16 & 1, ti >> 17 & 1, ti >> 18 & 1, ti >> 19 & 1
    a0, a1 = wrap(u0, w, rs, fs), wrap(u0 + 1, w, rs, fs)
    b0, b1 = wrap(v0, h, rt, ft), wrap(v0 + 1, h, rt, ft)
    return (img[b0, a0] * (1 - fu) * (1 - fv) + img[b0, a1] * fu * (1 - fv) + img[b1, a0] * (1 - fu) * fv
            + img[b1, a1] * fu * fv)  # fmt: skip


def prepare(mats: list[dict]) -> None:
    """Each material's texture as premultiplied floats ('fimg')."""
    for mt in mats:
        if mt.get("img") is not None and "fimg" not in mt:
            a = mt["img"].astype(float) / 255
            a[..., :3] *= a[..., 3:4]
            mt["fimg"] = a


def render(parts, w: int, h: int, k: float, centre, keep) -> Image.Image:
    """The parts' triangles of the materials `keep` takes, orthographic (looking down -Z, `k` dots a unit, `centre`
    at the picture's middle), in the painter's order, premultiplied; reduced SS x SS, then straight alpha."""
    acc = np.zeros((h * SS, w * SS, 4))
    tris = []
    for pi, (P, T, info, mats) in enumerate(parts):
        prepare(mats)
        for t, (a, b, c) in enumerate(T):
            m = info["tri_mat"][t] if t < len(info["tri_mat"]) else -1
            mt = mats[m] if 0 <= m < len(mats) else None
            if mt is None or not keep(mt["name"]):
                continue
            z = (P[a, 2] + P[b, 2] + P[c, 2]) / 3
            tris.append((z, pi, a, b, c, mt))
    tris.sort(key=lambda x: x[0])
    for z, pi, a, b, c, mt in tris:
        P, T, info, mats = parts[pi]
        uvs = info["uvs"]
        cols = info["cols"]
        xs = (w / 2 + (P[[a, b, c], 0] - centre[0]) * k) * SS
        ys = (h / 2 - (P[[a, b, c], 1] - centre[1]) * k) * SS
        x0, x1 = int(max(np.floor(xs.min()), 0)), int(min(np.ceil(xs.max()), w * SS - 1))
        y0, y1 = int(max(np.floor(ys.min()), 0)), int(min(np.ceil(ys.max()), h * SS - 1))
        if x1 < x0 or y1 < y0:
            continue
        den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / den
        l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not inside.any():
            continue
        L0, L1, L2 = l0[inside][:, None], l1[inside][:, None], l2[inside][:, None]
        if mt.get("fimg") is not None and len(uvs) == len(P):
            col = bilinear(mt, L0 * uvs[a] + L1 * uvs[b] + L2 * uvs[c])
        else:
            col = np.ones((len(L0), 4))
        # the vertex colour (or the material's diffuse) modulates; a decal ignores it
        if len(cols) == len(P) and min(cols[a], cols[b], cols[c]) >= 0:
            vc = np.array([[(v & 31) / 31, (v >> 5 & 31) / 31, (v >> 10 & 31) / 31] for v in cols[[a, b, c]]])
        else:
            d = mt.get("diffuse", 0x7FFF)
            vc = np.tile([(d & 31) / 31, (d >> 5 & 31) / 31, (d >> 10 & 31) / 31], (3, 1))
        if mt.get("mode", 0) != 1:
            col[:, :3] *= L0 * vc[0] + L1 * vc[1] + L2 * vc[2]
        # (the materials' own alpha, 0 at rest and faded in by their BMA0, is left out: they are opaque here)
        ys_i, xs_i = np.nonzero(inside)
        dst = acc[y0 + ys_i, x0 + xs_i]
        acc[y0 + ys_i, x0 + xs_i] = col + dst * (1 - col[:, 3:4])
    small = acc.reshape(h, SS, w, SS, 4).mean((1, 3))
    al = small[..., 3:4]
    rgb = np.where(al > 1e-4, small[..., :3] / np.maximum(al, 1e-4), 0)
    return Image.fromarray((np.clip(np.concatenate([rgb, al], -1), 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")


def bounds(parts, keep):
    """The kept triangles' corners' least and most."""
    pts = []
    for P, T, info, mats in parts:
        for t, (a, b, c) in enumerate(T):
            m = info["tri_mat"][t]
            if 0 <= m < len(mats) and keep(mats[m]["name"]):
                pts.append(P[[a, b, c]])
    if not pts:
        return np.zeros(3), np.ones(3)
    pts = np.concatenate(pts)
    return pts.min(0), pts.max(0)


def clean(im: Image.Image) -> Image.Image:
    """Without the hairlines a quad's edge leaves (its texture's border texels): what an opening of the shape (5
    dots) loses and its grown self does not cover again; cut to what is left."""
    a = im.getchannel("A")
    solid = a.point(lambda v: 255 if v > 32 else 0)
    opened = solid.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.MaxFilter(5))
    near = opened.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(1))
    im = im.copy()
    im.putalpha(ImageChops.multiply(a, near))
    return im.crop(im.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox())


def shadowed(im: Image.Image) -> Image.Image:
    """The title over a soft dark shadow (it sits on the rows' grey and red)."""
    pad = SHADOW * 2
    size = (im.width + 2 * pad, im.height + 2 * pad)
    a = Image.new("L", size, 0)
    a.paste(im.getchannel("A"), (pad, pad + SHADOW // 3))
    a = a.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(SHADOW / 2))
    a = a.point(lambda v: int(v * 0.75))
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    out.putalpha(a)
    out.alpha_composite(im, (pad, pad))
    return out.crop(out.getbbox())


def logo_title(objects: list[bytes], pattern: str) -> Image.Image:
    """A logo's title (the materials `pattern` names) drawn as large as TITLE_W x TITLE_H lets it be."""
    p = re.compile(r"(%s)$" % pattern)
    keep = lambda n: p.match(n) is not None  # noqa: E731
    parts = last_parts(objects)
    lo, hi = bounds(parts, keep)
    centre = (lo + hi) / 2
    k = min(TITLE_W / max(hi[0] - lo[0], 1e-3), TITLE_H / max(hi[1] - lo[1], 1e-3))
    im = render(parts, TITLE_W + 8, TITLE_H + 8, k, centre, keep)
    return shadowed(clean(im))


# ---- KH2's The World That Never Was -------------------------------------------------------------------------------


def journal_title(texture: Image.Image) -> Image.Image:
    """KH2's logo of The World That Never Was (the Journal page's texture): the letters are silver (unsaturated,
    light) in a dark outline; what is light and grey, grown to take in its outline, stays. About as large as the
    DS's titles (240 high)."""
    a = np.asarray(texture.convert("RGBA"), float)
    rgb, al = a[..., :3], a[..., 3]
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = (mx - mn) / np.maximum(mx, 1)
    silver = (sat < 0.22) & (mx > 150) & (al > 200)
    m = Image.fromarray((silver * 255).astype(np.uint8), "L")
    m = m.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))  # no lone specks
    m = m.filter(ImageFilter.MaxFilter(9))  # their dark outline
    m = m.filter(ImageFilter.GaussianBlur(1.2))
    keep = np.asarray(m, float) / 255
    out = a.copy()
    out[..., 3] = al * keep
    title = clean(Image.fromarray(out.astype(np.uint8), "RGBA"))
    k = TITLE_H / title.height
    title = title.resize((round(title.width * k), TITLE_H), Image.LANCZOS)
    return shadowed(title)


def titles(inputs: Inputs, worlds: list[str]) -> dict[str, Image.Image]:
    """The worlds' titles (their codes: saveworld.h's; the Castle's KH2's)."""
    out = {}
    for w in worlds:
        if w == "castle":
            out[w] = journal_title(Image.open(inputs.kh2 / "remastered" / JOURNAL / "-0.dds"))
        else:
            out[w] = logo_title([inputs.ds(f"mi/ob/{o}.z") for o in LOGOS[w]], TITLE[w])
    return out
