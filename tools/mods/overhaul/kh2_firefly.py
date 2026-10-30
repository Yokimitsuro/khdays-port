"""Kingdom Hearts II's firefly for the Menu Overhaul (layouts/kh2pause/firefly.png; host/screens/layout_screen.c adds
it to the screen as light): the sequence of KH2's menu/us/pause.2ld draws it as sprite group 1, sprites 19 and 20,
each with its own colour and added as light; animation group 8 holds it at its full size, 1.25.

The picture is 128 dots across (6.4 dots a PS2 dot, 2 dots an HD texel), drawn from the English HD sheet
(remastered/menu/us/pause.2ld, texture 0). Each channel of the sheet is a float plane (the texels over 255, in single
precision) sampled linearly at the dots' centres, as the PS2 filters: each sample the bilinear mix of the four texels
around it, rounded once to single precision, the texels around a sprite's box taking part. Each sprite's samples are
multiplied by its colour (0x80 = 1.0) and cut at 1, their colour times their alpha added as light, and the light kept
as a picture the port adds: its brightest channel the alpha (cut at 1), the colour the light over that alpha.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kh2_sq  # noqa: E402
from common import Inputs  # noqa: E402

PAUSE = "menu/us/pause.2ld"
SHEET = "US_pause_2ld0"  # its HD texture 0, 256 x 256
TEX_K = 4  # the HD sheet's texels a PS2 one (256 / 64)
GROUP = 8  # the animation group that holds the firefly at its full size
SIZE = 128  # the picture's dots across


def sequence(inputs: Inputs) -> bytes:
    """pause.2ld's sequence: its LAYD's one SEQD, from where the layout puts it to the layout's end."""
    for _, _, data in kh2_sq.bar(inputs.kh2 / "original" / PAUSE):
        if data[:4] == b"LAYD":
            count, table = struct.unpack_from("<2I", data, 0x18)
            assert count == 1, "pause.2ld's layout has one sequence"
            (start,) = struct.unpack_from("<I", data, table)
            return data[start:]
    raise ValueError("pause.2ld has no LAYD")


def sheet(inputs: Inputs) -> Image.Image:
    """The English HD sheet as the collection holds it (a DDS or a PNG), as RGBA."""
    (path,) = sorted((inputs.kh2 / "remastered" / PAUSE).glob(SHEET + ".*"))
    return Image.open(path).convert("RGBA")


def sampled(planes: list[Image.Image], box: tuple[int, ...], size: int) -> np.ndarray:
    """The sheet's texels in `box` (HD texels) drawn `size` dots square, each channel on its own (the colour not
    weighed by the alpha): each dot the bilinear mix of the four texels around its centre, rounded once."""
    return np.stack([np.asarray(p.transform((size, size), Image.EXTENT, box, Image.BILINEAR)) for p in planes], -1)


def draw(seq_bytes: bytes, sheet_rgba: Image.Image) -> Image.Image:
    """The firefly from the pause sequence and its HD sheet: each sprite's samples times its colour, cut at 1,
    added as light; then that light kept as alpha (its brightest channel) and colour (the light over it)."""
    seq = kh2_sq.Seq(seq_bytes, 0)
    anim = seq.anims[seq.agroups[GROUP][0]]
    scale = anim["scale"][0]
    tint = kh2_sq.rgba(anim["col"][0])
    first, count = seq.groups[anim["group"]]
    parts = [seq.parts[i] for i in range(first, first + count)]
    across = (max(max(p[0], p[2]) for p in parts) - min(min(p[0], p[2]) for p in parts)) * scale
    k = SIZE / across  # dots a PS2 dot
    middle = SIZE / 2
    texels = np.asarray(sheet_rgba, np.float32) / np.float32(255)
    planes = [Image.fromarray(np.ascontiguousarray(texels[..., i]), "F") for i in range(4)]
    light = np.zeros((SIZE, SIZE, 3))
    for left, top, right, bottom, index in parts:
        _, u0, v0, u1, v1, _, _, *colours = seq.sprites[index]
        x0 = round(middle + min(left, right) * scale * k)
        y0 = round(middle + min(top, bottom) * scale * k)
        size = round(abs(right - left) * scale * k)
        samples = sampled(planes, (u0 * TEX_K, v0 * TEX_K, u1 * TEX_K, v1 * TEX_K), size).astype(np.float64)
        lit = np.clip(samples * kh2_sq.rgba(colours[0]) * tint, 0, 1)
        light[y0 : y0 + size, x0 : x0 + size] += lit[..., :3] * lit[..., 3:4]
    alpha = np.clip(light.max(-1), 0, 1)
    colour = np.where(alpha[..., None] > 0, light / np.where(alpha > 0, alpha, 1)[..., None], 0)
    out = np.concatenate([np.clip(colour, 0, 1), alpha[..., None]], -1)
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA")


def firefly(inputs: Inputs) -> Image.Image:
    """layouts/kh2pause/firefly.png from the player's KH2 (menu/us/pause.2ld and its HD sheet)."""
    return draw(sequence(inputs), sheet(inputs))
