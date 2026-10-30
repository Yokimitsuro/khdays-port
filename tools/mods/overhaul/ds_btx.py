"""Nitro textures (BTX0's TEX0, as the DS's 3D engine reads them) decoded to RGBA, and a KAPH pack's members
(UI/pnl/3d_<lang>.pak: the Panels page's panels, one BTX0 each).
"""

import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import decompress_data as dd  # noqa: E402


def kaph(data: bytes):
    """A KAPH pack's members (eight section words from +4, the last the members' table: their count and
    offsets)."""
    assert data[:4] == b"KAPH", data[:4]
    section = struct.unpack_from("<I", data, 0x24)[0]
    n = struct.unpack_from("<I", data, section)[0]
    offsets = struct.unpack_from(f"<{n}I", data, section + 4)
    out = []
    for i, o in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < n else len(data)
        out.append(data[o:end])
    return out


def bgr555(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))


def info_block(b: bytes, at: int, item: int):
    """A 3D info block: a byte, its count, its size; an unknown block (its header, a word, 4 bytes an item);
    the data's header, then `item` bytes an item; then 16-byte names."""
    count = b[at + 1]
    data_at = at + 12 + 4 * count + 4
    items = [b[data_at + i * item : data_at + (i + 1) * item] for i in range(count)]
    names_at = data_at + count * item
    names = [b[names_at + 16 * i : names_at + 16 * (i + 1)].split(b"\0")[0].decode("latin1") for i in range(count)]
    return list(zip(names, items))


def palette(btx: bytes, n: int = 16):
    """The first palette of a BTX0: its first `n` colours (RGB)."""
    tex0 = struct.unpack_from("<I", btx, 0x10)[0]
    t = btx[tex0:]
    pal_info_at = struct.unpack_from("<I", t, 0x34)[0]
    pal_data_at = struct.unpack_from("<I", t, 0x38)[0]
    pals = info_block(t, pal_info_at, 4)
    at = pal_data_at + ((struct.unpack_from("<H", pals[0][1], 0)[0] << 3) if pals else 0)
    return [
        bgr555(struct.unpack_from("<H", t, at + 2 * k)[0]) if at + 2 * k + 2 <= len(t) else (0, 0, 0) for k in range(n)
    ]


def textures(btx: bytes, override=None):
    """A BTX0's textures, each with its palette (by order: the pack's are one of each), as RGBA arrays;
    `override` {index: RGB} puts other colours in the palette's places (Stop's clock in the magics' purple)."""
    assert btx[:4] == b"BTX0", btx[:4]
    tex0 = struct.unpack_from("<I", btx, 0x10)[0]
    t = btx[tex0:]
    assert t[:4] == b"TEX0"
    info_at = struct.unpack_from("<H", t, 0x0E)[0]
    data_at = struct.unpack_from("<I", t, 0x14)[0]
    pal_info_at = struct.unpack_from("<I", t, 0x34)[0]
    pal_data_at = struct.unpack_from("<I", t, 0x38)[0]
    texs = info_block(t, info_at, 8)
    pals = info_block(t, pal_info_at, 4)
    out = []
    for i, (name, raw) in enumerate(texs):
        params = struct.unpack_from("<I", raw, 0)[0]
        offset = (params & 0xFFFF) << 3
        w, h = 8 << ((params >> 20) & 7), 8 << ((params >> 23) & 7)
        fmt = (params >> 26) & 7
        zero_clear = (params >> 29) & 1
        pal = pals[min(i, len(pals) - 1)][1] if pals else b"\0\0\0\0"
        pal_off = struct.unpack_from("<H", pal, 0)[0] << (4 if fmt == 2 else 3)
        px = np.zeros((h, w, 4), np.uint8)
        src = t[data_at + offset :]

        def colours(n):
            # (a palette may hold fewer colours than its format can index: the rest unused)
            at = pal_data_at + pal_off
            cs = [
                bgr555(struct.unpack_from("<H", t, at + 2 * k)[0]) if at + 2 * k + 2 <= len(t) else (0, 0, 0)
                for k in range(n)
            ]
            for k, c in (override or {}).items():
                if k < len(cs):
                    cs[k] = c
            return cs

        if fmt in (2, 3, 4):  # 4, 16, 256 colours
            bits = {2: 2, 3: 4, 4: 8}[fmt]
            cs = colours(1 << bits)
            per = 8 // bits
            for y in range(h):
                for x in range(w):
                    k = y * w + x
                    v = (src[k // per] >> ((k % per) * bits)) & ((1 << bits) - 1)
                    if v == 0 and zero_clear:
                        continue
                    px[y, x, :3] = cs[v]
                    px[y, x, 3] = 255
        elif fmt in (1, 6):  # A3I5, A5I3
            ib = 5 if fmt == 1 else 3
            cs = colours(1 << ib)
            for y in range(h):
                for x in range(w):
                    v = src[y * w + x]
                    i, a = v & ((1 << ib) - 1), v >> ib
                    amax = (1 << (8 - ib)) - 1
                    px[y, x, :3] = cs[i]
                    px[y, x, 3] = a * 255 // amax
        elif fmt == 7:  # direct
            for y in range(h):
                for x in range(w):
                    c = struct.unpack_from("<H", src, 2 * (y * w + x))[0]
                    px[y, x, :3] = bgr555(c)
                    px[y, x, 3] = 255 if c & 0x8000 else 0
        else:
            raise ValueError(f"texture format {fmt} (4x4) not read")
        out.append((name, fmt, px))
    return out


def pack_members(pak: Path):
    """A KAPH pack's members, raw."""
    return kaph(dd.decompress_nitro(pak.read_bytes())[1])


def pack_textures(pak: Path):
    out = []
    for member in pack_members(pak):
        out.append(textures(member)[0] if member[:4] == b"BTX0" else (None, None, None))
    return out
