"""KINGDOM HEARTS II's prize effect (obj/PRIZE.mdlx: a BAR entry of type 18 holding a PAX), read as its own bytes
lay it out (docs/FORMATS.md, "KH2's PAX"): its pictures (8-bit, placed in 128 x 64 pages, each with its palette) and
its animations (frames of one or two layers: a square or four corners, a part of a page, a colour, a blend).

    pax = read_pax(Path("PRIZE.mdlx").read_bytes())
    rgba = page_rgba(pax, tbp, cbp)  # a page coloured with a palette
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

PAGE_W, PAGE_H = 128, 64  # a page as the pictures' places address it
BLOCK = 0x180  # the data block, from the PAX's start
ALPHA, ADD = 0x44, 0x48  # a layer's blend bytes (the PS2 GS ALPHA register's mixes)


class PaxError(ValueError):
    """The file is not a PAX as read here (truncated, another layout)."""


@dataclass
class Picture:
    tbp: int  # the page it is in
    cbp: int  # its palette's place
    x: int
    y: int
    w: int
    h: int
    indices: bytes  # w x h palette indices, rows top to bottom
    palette: list[tuple[int, int, int, int]]  # 256 RGBA in index order (alpha 0x80 opaque)


@dataclass
class Layer:
    corners: list[tuple[int, int]]  # top-left, top-right, bottom-left, bottom-right (y down)
    rect: tuple[float, float, float, float]  # its part of the page: left, top, width, height (fractions)
    colour: tuple[int, int, int, int]  # RGBA multiplying the picture, 0x80 = 1
    blend: str  # "alpha" or "add"


@dataclass
class Frame:
    tbp: int  # its page
    cbp: int  # its palette
    layers: list[Layer]


@dataclass
class Animation:
    number: int
    frames: list[Frame] = field(default_factory=list)


@dataclass
class Pax:
    pictures: list[Picture]
    animations: list[Animation]


def _unswap(i: int) -> int:
    """A stored palette entry's index: bits 3 and 4 swapped (the PS2's CSM1 order); its own inverse."""
    return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)


def _pax_of(data: bytes) -> bytes:
    """The PAX: a BAR's entry of type 18, or the bytes themselves."""
    if data[:4] == b"BAR\x01":
        count = struct.unpack_from("<I", data, 4)[0]
        for i in range(count):
            kind, _, _, offset, size = struct.unpack_from("<HH4sII", data, 16 + 16 * i)
            if kind == 18:
                return data[offset : offset + size]
        raise PaxError("the BAR holds no entry of type 18")
    return data


def read_pax(data: bytes) -> Pax:
    pax = _pax_of(data)
    try:
        if pax[:4] != b"PAX_":
            raise PaxError("not a PAX (no 'PAX_')")
        if struct.unpack_from("<I", pax, BLOCK)[0] != 0x96:
            raise PaxError("its data block is not where it is looked for")
        at = BLOCK + 12
        n = struct.unpack_from("<I", pax, at)[0]
        picture_offsets = struct.unpack_from(f"<{n}I", pax, at + 4)
        at += 4 + 4 * n
        m = struct.unpack_from("<I", pax, at)[0]
        animation_offsets = struct.unpack_from(f"<{m}I", pax, at + 4)
        pictures = [_picture(pax, BLOCK + o) for o in picture_offsets]
        animations = [_animation(pax, BLOCK + o) for o in animation_offsets]
    except struct.error as e:
        raise PaxError(f"truncated ({e})") from None
    return Pax(pictures, animations)


def _picture(pax: bytes, at: int) -> Picture:
    tbp, cbp, _tbw, psm, x, y, w, h = struct.unpack_from("<8H", pax, at)
    if psm != 0x13:
        raise PaxError(f"a picture of format {psm:#x} (only 8-bit pictures are read)")
    indices = pax[at + 32 : at + 32 + w * h]
    stored = pax[at + 32 + w * h : at + 32 + w * h + 1024]
    if len(indices) != w * h or len(stored) != 1024:
        raise PaxError("a picture runs past the end")
    palette = [tuple(stored[4 * _unswap(i) : 4 * _unswap(i) + 4]) for i in range(256)]
    return Picture(tbp, cbp, x, y, w, h, indices, palette)


def _animation(pax: bytes, at: int) -> Animation:
    number = struct.unpack_from("<I", pax, at)[0]
    animation = Animation(number)
    o = at + 0x20
    while o + 8 <= len(pax) and struct.unpack_from("<I", pax, o)[0] >> 24 == 2:
        offset = struct.unpack_from("<I", pax, o)[0] & 0xFFFFFF
        animation.frames.append(_frame(pax, at + 0x10 + offset))
        o += 8
    if not animation.frames:
        raise PaxError(f"animation {number} has no frame")
    return animation


def _frame(pax: bytes, at: int) -> Frame:
    kind, n, _size = struct.unpack_from("<HHI", pax, at)
    tex0 = struct.unpack_from("<Q", pax, at + 0x10)[0]
    o = at + 0x20
    shapes = []
    for _ in range(n):
        if kind & 8:  # four corners
            xy = struct.unpack_from("<8h", pax, o)
            corners = [(xy[0], xy[1]), (xy[2], xy[3]), (xy[4], xy[5]), (xy[6], xy[7])]
            uv = struct.unpack_from("<4H", pax, o + 16)
            o += 24
        else:  # a square: x0 y0 x1 y1
            x0, y0, x1, y1 = struct.unpack_from("<4h", pax, o)
            corners = [(x0, y0), (x1, y0), (x0, y1), (x1, y1)]
            uv = struct.unpack_from("<4H", pax, o + 8)
            o += 16
        shapes.append((corners, tuple(v / 4096 for v in uv)))
    layers = []
    for i, (corners, rect) in enumerate(shapes):
        colour = tuple(pax[o + 8 * i : o + 8 * i + 4])
        blend = "add" if pax[o + 8 * i + 4] == ADD else "alpha"
        layers.append(Layer(corners, rect, colour, blend))
    return Frame(tex0 & 0x3FFF, (tex0 >> 37) & 0x3FFF, layers)


def page_rgba(pax: Pax, tbp: int, cbp: int) -> bytes:
    """Page `tbp`'s pictures coloured with palette `cbp` (the picture that brings it), as PAGE_W x PAGE_H RGBA with
    alpha 0..255; what no picture covers is clear."""
    palette = next((p.palette for p in pax.pictures if p.cbp == cbp), None)
    if palette is None:
        raise PaxError(f"no palette at {cbp:#x}")
    out = bytearray(PAGE_W * PAGE_H * 4)
    for p in pax.pictures:
        if p.tbp != tbp:
            continue
        for row in range(p.h):
            for col in range(p.w):
                r, g, b, a = palette[p.indices[row * p.w + col]]
                at = ((p.y + row) * PAGE_W + p.x + col) * 4
                out[at : at + 4] = bytes((r, g, b, min(255, a * 2)))
    return bytes(out)
