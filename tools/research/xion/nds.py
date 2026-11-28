"""Read-only NDS ROM / NitroFS listing helpers.

The ROM header (offsets) and FNT/FAT layout are the Nintendo DS cartridge
format: header 0x40 fnt offset, 0x44 fnt size, 0x48 fat offset, 0x4c fat size;
FAT is pairs of u32 (start, end) relative to the ROM start; FNT directories are
`u32 subtable offset, u16 first file id` then entries `u8 len (bit 7 = dir)`.
Nothing here writes to the ROM.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class NdsFile:
    path: str
    file_id: int
    start: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.start


class NdsRom:
    def __init__(self, rom_path: str | Path):
        self.path = Path(rom_path)
        self.fh = open(self.path, "rb")
        head = self.fh.read(0x200)
        if len(head) != 0x200:
            raise ValueError("ROM header truncated")
        self.title = head[0:12].decode("ascii", "replace").strip("\x00 ")
        self.game_code = head[12:16].decode("ascii", "replace")
        self.capacity = 0x20000 << head[0x14]
        self.fnt_off, self.fnt_size, self.fat_off, self.fat_size = struct.unpack_from("<IIII", head, 0x40)
        self.files: list[NdsFile] = []
        self._read_fat()
        self._read_fnt()

    def _u32(self, off: int) -> int:
        self.fh.seek(off)
        return struct.unpack("<I", self.fh.read(4))[0]

    def _read_fat(self) -> None:
        self.fh.seek(self.fat_off)
        raw = self.fh.read(self.fat_size)
        self.fat = [struct.unpack_from("<II", raw, i * 8) for i in range(len(raw) // 8)]

    def _read_fnt(self) -> None:
        base = self.fnt_off
        self.fh.seek(base)
        self.fnt = self.fh.read(self.fnt_size)
        self._walk_dir(0xF000, "")

    def _walk_dir(self, dir_id: int, prefix: str) -> None:
        off = (dir_id & 0x0FFF) * 8
        sub_off, first_id = struct.unpack_from("<IH", self.fnt, off)
        p = sub_off  # offsets are from the FNT start, not from the record
        fid = first_id
        while p < len(self.fnt):
            length = self.fnt[p]
            p += 1
            if length == 0:
                return
            is_dir = bool(length & 0x80)
            length &= 0x7F
            name = self.fnt[p : p + length].decode("ascii", "replace")
            p += length
            if is_dir:
                child = struct.unpack_from("<H", self.fnt, p)[0]
                p += 2
                self._walk_dir(child, prefix + name + "/")
            else:
                start, end = self.fat[fid]
                self.files.append(NdsFile(prefix + name, fid, start, end))
                fid += 1

    def find(self, needle: str) -> list[NdsFile]:
        return [f for f in self.files if needle in f.path]

    def read(self, f: NdsFile) -> bytes:
        self.fh.seek(f.start)
        return self.fh.read(f.size)

    def close(self) -> None:
        self.fh.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def lz_decompress(data: bytes) -> bytes:
    """Nintendo DS LZ10 (0x10) / LZ11 (0x11) decompression."""
    if len(data) < 4:
        raise ValueError("LZ stream smaller than its header")
    kind = data[0]
    if kind not in (0x10, 0x11):
        raise ValueError(f"not an LZ10/LZ11 stream: type 0x{kind:02x}")
    size = int.from_bytes(data[1:4], "little")
    p = 4
    if size == 0:
        size = int.from_bytes(data[4:8], "little")
        p = 8
    out = bytearray()
    if kind == 0x10:
        while len(out) < size:
            flags = data[p]
            p += 1
            for bit in range(8):
                if len(out) >= size:
                    break
                if flags & (0x80 >> bit):
                    b = data[p]
                    p += 1
                    n = (b >> 4) + 3
                    dist = (((b & 0x0F) << 8) | data[p]) + 1
                    p += 1
                    for _ in range(n):
                        if len(out) >= size:
                            break
                        out.append(out[-dist])
                else:
                    out.append(data[p])
                    p += 1
    else:
        while len(out) < size:
            flags = data[p]
            p += 1
            for bit in range(8):
                if len(out) >= size:
                    break
                if flags & (0x80 >> bit):
                    b = data[p]
                    p += 1
                    nib = b >> 4
                    if nib == 0:
                        # extended length: two more bytes after the first
                        h = b & 0x0F
                        lo = data[p]
                        hi = data[p + 1]
                        p += 2
                        n = ((h << 4) | (lo >> 4)) + 0x11
                        dist = (((lo & 0x0F) << 8) | hi) + 1
                    elif nib == 1:
                        n = (((b & 0x0F) << 12) | (data[p] << 4) | (data[p + 1] >> 4)) + 0x111
                        dist = (((data[p + 1] & 0x0F) << 8) | data[p + 2]) + 1
                        p += 3
                    else:
                        n = nib + 1
                        dist = (((b & 0x0F) << 8) | data[p]) + 1
                        p += 1
                    for _ in range(n):
                        if len(out) >= size:
                            break
                        out.append(out[-dist])
                else:
                    out.append(data[p])
                    p += 1
    if len(out) != size:
        raise ValueError(f"LZ output {len(out)} != header size {size}")
    return bytes(out)
