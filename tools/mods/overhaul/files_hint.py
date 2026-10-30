"""The Files pages' A mark (layouts/files/files_hint.png): the mark the DS draws on a list row with more to show
(the row's entry 0x65 + row, Ov025_Reports_RefreshRowEntries: the diary's gold coin with an A, the enemies' A and
chest), cut from the game's own sprites for the port to draw over the DS's upper screen (host/screens/
overhaul_camp_screen.c, "The Files pages").

UI/cm/cm.p2's members 74 and 82 (ov025's data_ov025_020b4228: the diary's and the enemies' row sprites) are D2KP
packs holding a Nitro cell bank (NCER), its characters (NCGR, 8 bits a pixel) and palette (NCLR). Their animations
(NANR): sequence 1 the mark (cells 2, 3), sequence 2 the chosen row's lit one (cells 4, 5), 15 frames each.
The sheet: two rows (diary, enemies) of four 16 x 18 cells -- cells 2, 3, 4, 5 -- each cell's origin at (8, 9)
(the diary's cell 3 is a pixel lower: its bob)."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "hd"))
import save_screen as ss  # noqa: E402

ARCHIVE = "UI/cm/cm.p2"
MEMBERS = (74, 82)
CELLS = (2, 3, 4, 5)
CW, CH = 16, 18
ORIGIN = (8, 9)  # a cell's origin in its frame
SIZES = {
    0: [(8, 8), (16, 16), (32, 32), (64, 64)],
    1: [(16, 8), (32, 8), (32, 16), (64, 32)],
    2: [(8, 16), (8, 32), (16, 32), (32, 64)],
}


def nitro(d: bytes, magic: bytes) -> bytes:
    """A Nitro block's data (after its 16-byte header): the first one with this magic in the pack."""
    a = d.index(magic)
    return d[a : a + struct.unpack("<I", d[a + 8 : a + 12])[0]][0x10:]


def cells_of(d: bytes):
    """The cell bank's cells, each its objects (x, y, w, h, first tile); and its characters' mapping."""
    b = nitro(d, b"RECN")
    n, attr = struct.unpack("<HH", b[8:12])
    off, mapping = struct.unpack("<II", b[12:20])
    size = 16 if attr == 1 else 8
    oam = 8 + off + n * size
    out = []
    for i in range(n):
        cnt, _, at = struct.unpack("<HHI", b[8 + off + i * size : 8 + off + i * size + 8])
        objs = []
        for k in range(cnt):
            a0, a1, a2 = struct.unpack("<HHH", b[oam + at + 6 * k : oam + at + 6 * k + 6])
            y, x = a0 & 0xFF, a1 & 0x1FF
            w, h = SIZES[a0 >> 14][a1 >> 14]
            objs.append((x - 512 if x >= 256 else x, y - 256 if y >= 128 else y, w, h, a2 & 0x3FF))
        out.append(objs)
    return out, mapping


def hint_sheet(cm_p2: Path) -> Image.Image:
    """The marks' sheet: a row a member, a frame a cell, each colour (5 bits a channel, widened by 255 / 31) opaque
    where the character's index is not 0."""
    out = Image.new("RGBA", (CW * len(CELLS), CH * len(MEMBERS)))
    for row, n in enumerate(MEMBERS):
        d = ss.p2_entry(cm_p2, n)
        cells, mapping = cells_of(d)
        g = nitro(d, b"RGCN")
        chars = g[8 + struct.unpack("<I", g[28:32])[0] :]
        p = nitro(d, b"RLCN")
        cols = np.frombuffer(p[8 + struct.unpack("<I", p[20:24])[0] :][:512], "<u2")
        pal = (np.stack([cols & 31, cols >> 5 & 31, cols >> 10 & 31], -1) * 255 // 31).astype(np.uint8)
        for k, c in enumerate(CELLS):
            im = np.zeros((CH, CW, 4), np.uint8)
            for x, y, w, h, tile in cells[c]:
                start = tile * (32 << mapping)  # 1D mapping: a tile number steps 32 << mapping bytes
                for ty in range(h // 8):
                    for tx in range(w // 8):
                        t = np.frombuffer(chars[start + (ty * (w // 8) + tx) * 64 :][:64], np.uint8).reshape(8, 8)
                        for yy in range(8):
                            for xx in range(8):
                                if t[yy, xx]:
                                    py, px = y + ORIGIN[1] + ty * 8 + yy, x + ORIGIN[0] + tx * 8 + xx
                                    if 0 <= py < CH and 0 <= px < CW:
                                        im[py, px, :3] = pal[t[yy, xx]]
                                        im[py, px, 3] = 255
            out.alpha_composite(Image.fromarray(im, "RGBA"), (k * CW, row * CH))
    return out
