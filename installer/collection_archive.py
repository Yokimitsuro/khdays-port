"""The KINGDOM HEARTS collections' archives (a `.hed` index and its `.pkg`), read as the collections'
own code reads them, from our own reverse engineering (installer/README.md, "How it works").
  python collection_archive.py <archive without .hed/.pkg> <out folder> <kh2 exe> <path> [<path> ...]
writes each path's data as `original/<path>` and its assets as `remastered/<path>/<asset>`; a path the
archive does not hold is named and left out (exit status 3 when it holds none of them).
  python collection_archive.py --has <archive without .hed/.pkg> <path> [<path> ...]
prints the paths the archive holds, one a line (the studio's KH2 rooms: which maps a copy has).
"""

from __future__ import annotations

import hashlib
import itertools
import os
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

# The game's S-box, recognised by its digest (its bytes stay in the player's executable).
TABLE_SHA256 = "759b2f22f7e0bfd257063a01f3de0c0835905217aa926df6def134a14313672d"
TABLE_OFFSETS = (0x710940,)  # where Steam's KINGDOM HEARTS II FINAL MIX.exe has it (8,659,216 bytes)
TABLE_FILE_VARIABLE = "KHDAYS_COLLECTION_TABLE"  # a file with the table, for a copy whose executable is not read
XBOX_GAME_CONFIG = "MicrosoftGame.config"  # beside the executables of a game the Xbox app installed
RCON = (0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36)
ENTRY = struct.Struct("<16sqii")
HEADER = struct.Struct("<iiii")
ASSET = struct.Struct("<32siiii")
STORED, ENCRYPTED = -2, -1


class ArchiveError(Exception):
    pass


class UnreadExecutable(ArchiveError):
    """The executable is there and only the running game reads it (the Xbox app's copy), and it does not run."""


def table_in(data: bytes) -> bytes | None:
    """The S-box in `data`: at a known place, else any 16-byte-aligned place whose 256 bytes have its digest."""
    for offset in itertools.chain(TABLE_OFFSETS, range(0, len(data) - 255, 16)):
        window = data[offset : offset + 256]
        if hashlib.sha256(window).hexdigest() == TABLE_SHA256:
            return window
    return None


def load_table(exe: Path) -> bytes:
    """The S-box from the player's executable. The Xbox app (the Microsoft Store's copy) lets nothing but the
    game read its executables: the table is then read from the running game's memory (game_memory.py), and
    `UnreadExecutable` says the game is to be started when it does not run. The file `KHDAYS_COLLECTION_TABLE`
    names (the player's own, holding the 256 bytes) gives it when neither does."""
    import game_memory

    try:
        table = table_in(exe.read_bytes())
        failure: Exception = ArchiveError(f"{exe} does not hold the collections' archive table")
    except PermissionError as error:
        table, failure = None, error
        if (exe.parent / XBOX_GAME_CONFIG).is_file():  # the Xbox app's copy, not a file some other way refused
            image = game_memory.running_image(exe)
            table = table_in(image) if image is not None else None
            failure = UnreadExecutable(f"{exe} is read only while it runs ({error})")
    except OSError as error:
        table, failure = None, error
    if table is None:
        kept = os.environ.get(TABLE_FILE_VARIABLE)
        if kept:
            try:
                table = table_in(Path(kept).read_bytes())
            except OSError:
                table = None
    if table is None:
        raise failure
    return table


class Cipher:
    def __init__(self, table: bytes) -> None:
        self.table = table
        self._masks: dict[bytes, tuple[int, int, int, int]] = {}

    def mask(self, header: bytes) -> tuple[int, int, int, int]:
        """The XOR of the eleven round keys of the entry's header."""
        found = self._masks.get(header)
        if found is not None:
            return found
        key = bytes(b if b else i for i, b in enumerate(header))
        w = list(struct.unpack("<4I", key))
        t_ = self.table
        for i in range(4, 44):
            t = w[i - 1]
            if i % 4 == 0:
                t = ((t >> 8) | (t << 24)) & 0xFFFFFFFF  # RotWord: bytes b1 b2 b3 b0
                t = t_[t & 0xFF] | t_[(t >> 8) & 0xFF] << 8 | t_[(t >> 16) & 0xFF] << 16 | t_[t >> 24] << 24
                t ^= RCON[i // 4]
            w.append(w[i - 4] ^ t)
        m = [0, 0, 0, 0]
        for r in range(11):
            for j in range(4):
                m[j] ^= w[4 * r + j]
        found = (m[0], m[1], m[2], m[3])
        self._masks[header] = found
        return found

    def decrypt(self, data: bytes, header: bytes) -> bytes:
        m = self.mask(header)
        out = bytearray(data)
        for at in range(0, min(0x100, len(out) - len(out) % 16), 16):
            a, b, c, d = struct.unpack_from("<4I", out, at)
            struct.pack_into("<4I", out, at, a ^ m[0], b ^ m[1], c ^ m[2], d ^ m[3])
        return bytes(out)


@dataclass
class Entry:
    data: bytes
    assets: list[tuple[str, bytes]] = field(default_factory=list)


def path_hash(path: str) -> bytes:
    """The `.hed` key of `path`: its MD5 as written; a name that is itself an MD5 (`<32 hex>.dat`,
    an entry whose path is not known) is taken as the key."""
    stem = path.rsplit("/", 1)[-1]
    if len(stem) == 36 and stem.endswith(".dat"):
        try:
            return bytes.fromhex(stem[:32])
        except ValueError:
            pass
    return hashlib.md5(path.encode("utf-8")).digest()


class Archive:
    def __init__(self, base: Path, cipher: Cipher | None) -> None:
        self.base = base
        self.cipher = cipher
        hed = base.with_name(base.name + ".hed").read_bytes()
        self.index = {}
        for at in range(0, len(hed) - len(hed) % ENTRY.size, ENTRY.size):
            key, offset, _stored, _length = ENTRY.unpack_from(hed, at)
            self.index[key] = offset
        self.pkg = base.with_name(base.name + ".pkg").open("rb")

    def close(self) -> None:
        self.pkg.close()

    def __enter__(self) -> "Archive":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def has(self, path: str) -> bool:
        return path_hash(path) in self.index

    def _read(self, size: int) -> bytes:
        data = self.pkg.read(size)
        if len(data) != size:
            raise ArchiveError(f"{self.base}.pkg ends inside an entry")
        return data

    def _payload(self, header: bytes, length: int, stored: int) -> bytes:
        """One piece's bytes as the game reads them: plain when 16 bytes or fewer; stored; encrypted
        only; or compressed and encrypted (inflated, its first `length` bytes kept)."""
        if length <= 16:
            return self._read(length)
        if stored > 0:
            body = self._read(stored)
            if self.cipher is None:
                raise ArchiveError(f"{self.base}: an encrypted entry and no table to read it with")
            out = zlib.decompress(self.cipher.decrypt(body, header))
            if len(out) < length:
                raise ArchiveError(f"{self.base}: an entry inflates to {len(out)} bytes, not {length}")
            return out[:length]
        body = self._read(length)
        if stored == ENCRYPTED:
            if self.cipher is None:
                raise ArchiveError(f"{self.base}: an encrypted entry and no table to read it with")
            body = self.cipher.decrypt(body, header)
        return body

    def read(self, path: str) -> Entry:
        offset = self.index.get(path_hash(path))
        if offset is None:
            raise ArchiveError(f"{path} is not in {self.base}")
        self.pkg.seek(offset)
        header = self._read(HEADER.size)
        length, count, stored, _date = HEADER.unpack(header)
        records = [ASSET.unpack(self._read(ASSET.size)) for _ in range(count)]
        data_at = offset + HEADER.size + count * ASSET.size
        self.pkg.seek(data_at)
        entry = Entry(self._payload(header, length, stored))
        at = data_at + (stored if stored > 0 else (length + 15) & ~15)
        for name, _f20, _f24, alength, astored in records:
            self.pkg.seek(at)
            entry.assets.append(
                (name.split(b"\0", 1)[0].decode("ascii", "replace"), self._payload(header, alength, astored))
            )
            at += astored if astored > 0 else (alength + 15) & ~15
        return entry


def materialise(archive: Archive, paths: list[str], out: Path) -> int:
    """Each path's data as `original/<path>`, its assets as `remastered/<path>/<asset>`, the layout
    the HD tools read. Returns how many files were written."""
    written = 0
    for path in paths:
        entry = archive.read(path)
        target = out / "original" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(entry.data)
        written += 1
        for name, data in entry.assets:
            asset = out / "remastered" / path / name
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(data)
            written += 1
    return written


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[1] == "--has":
        with Archive(Path(argv[2]), None) as archive:
            for path in argv[3:]:
                if archive.has(path):
                    print(path)
        return 0
    if len(argv) < 5:
        print(__doc__)
        return 2
    base, out, exe, paths = Path(argv[1]), Path(argv[2]), Path(argv[3]), argv[4:]
    with Archive(base, Cipher(load_table(exe))) as archive:
        held = [path for path in paths if archive.has(path)]
        for path in paths:
            if path not in held:
                print(f"{path} is not in {base}")
        if not held:
            return 3
        print(f"{materialise(archive, held, out)} files written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
