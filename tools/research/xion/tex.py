"""Decode Days TEX0 formats 1-4 and 6-7 into 8-bit RGBA texels.

Follows khdays-port port/gpu/texdecode.h at 0b65fa4f, under its MIT license.
Format 5 (4x4 compression) is not supported; see README.md."""

from __future__ import annotations

from dataclasses import dataclass

from nitro3d import parse_dict, u16, u32


def expand5(c: int) -> int:
    c &= 0x1F
    return (c << 3) | (c >> 2)


@dataclass
class Texture:
    name: str
    width: int
    height: int
    format: int
    rgba: bytes  # width*height*4, rows top to bottom


def tex0_layout(tex0: bytes) -> dict:
    """The TEX0 section's dictionaries and data blocks (offsets from TEX0's start)."""
    return {
        "tex_dict": parse_dict(tex0, u16(tex0, 0x0E)),
        "pltt_dict": parse_dict(tex0, u16(tex0, 0x34)),
        "tex_data": u32(tex0, 0x14),
        "tex4x4_data": u32(tex0, 0x24),
        "pltt_data": u32(tex0, 0x38),
        "pltt_size": u16(tex0, 0x30) * 8,
    }


def palette_bytes(tex0: bytes, layout: dict, name: str) -> tuple[int, int]:
    """(offset, length) of a palette's colour data inside TEX0."""
    starts = [u16(e, 0) * 8 for e in layout["pltt_dict"].entries]
    names = layout["pltt_dict"].names
    index = names.index(name)
    start = starts[index]
    end = layout["pltt_size"]
    for other in starts:
        if other > start and other < end:
            end = other
    return layout["pltt_data"] + start, max(0, end - start)


def decode_texture(tex0: bytes, layout: dict, tex_name: str, pltt_name: str) -> Texture:
    i = layout["tex_dict"].names.index(tex_name)
    param = u32(layout["tex_dict"].entries[i], 0)
    width = 8 << ((param >> 20) & 7)
    height = 8 << ((param >> 23) & 7)
    fmt = (param >> 26) & 7
    base = layout["tex_data"] + (param & 0xFFFF) * 8
    pal_off, pal_len = palette_bytes(tex0, layout, pltt_name) if pltt_name else (0, 0)

    def pal_color(index: int) -> tuple[int, int, int, int]:
        o = pal_off + index * 2
        if o + 2 > len(tex0):
            return (255, 0, 255, 255)
        v = u16(tex0, o)
        alpha = 0 if fmt in (2, 3, 4) and param & (1 << 29) and index == 0 else 255
        return (expand5(v), expand5(v >> 5), expand5(v >> 10), alpha)

    out = bytearray(width * height * 4)
    for t in range(height):
        for s in range(width):
            o = (t * width + s) * 4
            if fmt == 1:  # A3I5
                v = tex0[base + t * width + s]
                r, g, b, _ = pal_color(v & 31)
                a = expand5((v >> 5) * 4 + (v >> 5) // 2)
                out[o : o + 4] = bytes((r, g, b, a))
            elif fmt == 2:  # 4-colour, 2bpp
                v = tex0[base + (t * width + s) // 4]
                idx = (v >> (2 * (s & 3))) & 3
                r, g, b, a = pal_color(idx)
                out[o : o + 4] = bytes((r, g, b, a))
            elif fmt == 3:  # 16-colour, 4bpp
                v = tex0[base + (t * width + s) // 2]
                idx = (v >> (4 * (s & 1))) & 15
                r, g, b, a = pal_color(idx)
                out[o : o + 4] = bytes((r, g, b, a))
            elif fmt == 4:  # 256-colour, 8bpp
                idx = tex0[base + t * width + s]
                r, g, b, a = pal_color(idx)
                out[o : o + 4] = bytes((r, g, b, a))
            elif fmt == 6:  # A5I3
                v = tex0[base + t * width + s]
                r, g, b, _ = pal_color(v & 7)
                a = ((v >> 3) * 255 + 15) // 31
                out[o : o + 4] = bytes((r, g, b, a))
            elif fmt == 7:  # direct RGBA5551
                v = u16(tex0, base + (t * width + s) * 2)
                out[o : o + 4] = bytes((expand5(v), expand5(v >> 5), expand5(v >> 10), 255 if v & 0x8000 else 0))
            else:
                out[o : o + 4] = bytes((255, 0, 255, 255))
    return Texture(tex_name, width, height, fmt, bytes(out))
