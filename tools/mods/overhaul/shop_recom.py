"""Re:Chain of Memories' Moogle Shop bands and pills, made the shop block's brown, for the Moogle Shop on one screen.

From the collection's Recom archive (the installer's extraction, inputs.recom): FORM/0109/FO0109.SPR's remastered
<LANG>_m_shop.lad (the shop's LAYD: SEQD sequences) and FORM/0109/FO0109.RTM's remastered <LANG>_m_shop.imz (its
HD textures: an IMGZ of 32-bit IMGDs). Sequence 1 holds the shop's frame (animation group 26): animation 98 the top
band, 99 the bottom band, 100 the MOOGLE SHOP pill, 101 its words (each language's), 102 the message pill. They are
drawn at RECOM_K window pixels a PS2 dot by kh2_seq_draw (bicubic), their reds made brown (recolour).

The textures' alpha is kept as stored, on the PS2's scale (128 the most), and read as 8-bit: the pieces come out
half see-through, as Re:CoM lays them over its scene, and the bands are then laid over BAND_GROUND, opaque.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kh2_seq_draw as seq_draw  # noqa: E402
from kh2_sq import Seq  # noqa: E402

RECOM_LANGS = ("UK", "FR", "GR", "IT", "SP")  # Re:CoM's languages, the port's en, fr, de, it, es
RECOM_K = 2.6  # window pixels a PS2 dot
BROWN = np.array([1.0, 0.53, 0.06])  # the shop block's brown (89, 47, 5) as a ratio of its red
GOLD_LINE = np.array([1.0, 0.70, 0.22])  # the bands' bright red lines: gold
BAND_GROUND = (26, 14, 3)  # under the bands: they are opaque on the screen
BAND_WIDTH = 1920  # the bands stretched across the 1920-wide layout
BAND_TOP_H = 112  # the top band's lower rows kept (its top is above the screen)
BAND_BOTTOM_H = 120  # the bottom band's upper rows kept
SPR = "remastered/FORM/0109/FO0109.SPR/{lang}_m_shop.lad"
RTM = "remastered/FORM/0109/FO0109.RTM/{lang}_m_shop.imz"


def sequences(recom: Path, lang: str) -> list[Seq]:
    """The shop's LAYD sequences in one language."""
    data = (recom / SPR.format(lang=lang)).read_bytes()
    count, at = struct.unpack_from("<2I", data, 0x18)
    return [Seq(data, o) for o in struct.unpack_from("<%dI" % count, data, at)]


def textures(recom: Path, lang: str) -> list[Image.Image]:
    """The shop's HD textures in one language, in the IMGZ's order (their alpha as stored: see above)."""
    data = (recom / RTM.format(lang=lang)).read_bytes()
    if data[:4] != b"IMGZ":
        raise ValueError(f"{RTM.format(lang=lang)} is not an IMGZ")
    count = struct.unpack_from("<I", data, 0xC)[0]
    out = []
    for i in range(count):
        at = struct.unpack_from("<I", data, 0x10 + 8 * i)[0]
        _, _, pixels, size, _, _, _, w, h = struct.unpack_from("<4sIIIIIiHH", data, at)
        if size != w * h * 4:
            raise ValueError(f"{RTM.format(lang=lang)}: texture {i} is not 32-bit")
        out.append(Image.fromarray(np.frombuffer(data, np.uint8, size, at + pixels).reshape(h, w, 4).copy(), "RGBA"))
    return out


def recolour(c, k, g):
    """A tint's red made the block's brown (its brightest, the lines, gold), a little lighter; other tints as
    they are."""
    r, gg, b, a = c
    if r > 1.3 * gg + 0.01 and r > 1.3 * b + 0.01:
        v = max(r, gg, b)
        ratio = GOLD_LINE if v > 0.9 else BROWN
        lift = 1.25 if v > 0.9 else 1.15
        return np.array([*(v * ratio * lift), a])
    return c


def anim_image(seq: Seq, tex: Image.Image, k: int, size: tuple[int, int], origin: tuple[float, float]) -> Image.Image:
    """Animation `k` of the sequence at its first frame, recoloured, on a clear canvas of `size`."""
    canvas = np.zeros((size[1], size[0], 4))
    seq_draw.draw_anim(canvas, seq, tex, k, seq.anims[k], 0, origin, RECOM_K, RECOM_K, 1.0, recolour)
    return seq_draw.to_image(canvas)


def opaque_band(band: Image.Image) -> Image.Image:
    """A band over BAND_GROUND, stretched to BAND_WIDTH (Lanczos)."""
    out = Image.new("RGBA", band.size, BAND_GROUND + (255,))
    out.alpha_composite(band)
    return out.resize((BAND_WIDTH, band.height), Image.LANCZOS)


def bands(recom: Path) -> dict[str, Image.Image]:
    """The English shop's bands and pills: band_top, band_bottom (opaque, BAND_WIDTH wide), pill (the MOOGLE
    SHOP pill without its words) and message_pill, each cut to its own box."""
    seq = sequences(recom, "UK")[1]
    tex = textures(recom, "UK")[1]
    k = RECOM_K
    top = anim_image(seq, tex, 98, (1800, 300), (89 * k + 2, 40 * k))
    pill = anim_image(seq, tex, 100, (900, 200), (40 * k, 40 * k))
    bottom = anim_image(seq, tex, 99, (1800, 300), (89 * k + 2, -300 * k))
    message = anim_image(seq, tex, 102, (1400, 200), (20 * k, -330 * k))
    top = top.crop(top.getbbox())
    bottom = bottom.crop(bottom.getbbox())
    return {
        "band_top": opaque_band(top.crop((0, top.height - BAND_TOP_H, top.width, top.height))),
        "band_bottom": opaque_band(bottom.crop((0, 0, bottom.width, BAND_BOTTOM_H))),
        "pill": pill.crop(pill.getbbox()),
        "message_pill": message.crop(message.getbbox()),
    }


def title(recom: Path, lang: str) -> Image.Image:
    """The MOOGLE SHOP pill in one of Re:CoM's languages (animation 100, the pill, and 101, its words), cut to the
    pill's box. Re:CoM draws the words light grey at 2/3 opacity over the pill (grey on the brown): here they are
    opaque and nearly white; their black shadow as it is."""
    seq = sequences(recom, lang)[1]
    tex = textures(recom, lang)[1]
    k = RECOM_K
    pill, words = (anim_image(seq, tex, n, (900, 200), (40 * k, 40 * k)) for n in (100, 101))
    t = np.asarray(words).astype(float).copy()
    light = t[..., :3].max(axis=2) > 100
    t[..., 3] = np.where(light, np.clip(t[..., 3] * 255.0 / 169.0, 0, 255), t[..., 3])
    t[..., :3] = np.where(light[..., None], 255.0 - (255.0 - t[..., :3]) * 0.2, t[..., :3])
    out = pill.copy()
    out.alpha_composite(Image.fromarray(t.astype(np.uint8), "RGBA"))
    return out.crop(pill.getbbox())
