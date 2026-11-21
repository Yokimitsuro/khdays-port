"""The collections' archive reader (collection_archive.py) on synthetic archives: every kind of entry
-- tiny, stored, encrypted only, compressed and encrypted, with remastered assets of each kind --
written as the collections write them and read back byte for byte. The table here is a made-up
permutation (the game's own is never in the repository); the encryption is its own inverse (XOR)."""

import contextlib
import hashlib
import io
import os
import random
import struct
import sys
import tempfile
import unittest
import zlib
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import collection_archive as ca  # noqa: E402

TABLE = bytes(random.Random(358).sample(range(256), 256))


def rounded(n):
    return (n + 15) & ~15


def piece(cipher, header, data, kind):
    """A piece as stored, its stored length: 'stored', 'encrypted' or 'compressed'."""
    if len(data) <= 16 or kind == "stored":
        return data + b"\0" * (rounded(len(data)) - len(data)), ca.STORED
    if kind == "encrypted":
        return cipher.decrypt(data, header) + b"\0" * (rounded(len(data)) - len(data)), ca.ENCRYPTED
    body = zlib.compress(data + b"\0" * (rounded(len(data)) - len(data)))  # padded, as the games pack it
    return cipher.decrypt(body, header), len(body)


def write_archive(base: Path, entries, cipher):
    """entries: path -> (data, kind, [(asset name, data, kind)])."""
    hed, pkg = bytearray(), bytearray()
    for path, (data, kind, assets) in entries.items():
        date = 0x665F4DB9
        # the header holds the stored length, which depends on the header (its key): settle it
        stored_len = ca.STORED
        for _ in range(4):
            header = struct.pack("<iiii", len(data), len(assets), stored_len, date)
            body, new_len = piece(cipher, header, data, kind)
            if new_len == stored_len:
                break
            stored_len = new_len
        header = struct.pack("<iiii", len(data), len(assets), stored_len, date)
        records, bodies = bytearray(), bytearray()
        for name, adata, akind in assets:
            abody, astored = piece(cipher, header, adata, akind)
            records += struct.pack("<32siiii", name.encode(), 0, 0, len(adata), astored)
            bodies += abody
        offset = len(pkg)
        pkg += header + records + body + bodies
        hed += struct.pack("<16sqii", hashlib.md5(path.encode()).digest(), offset, len(pkg) - offset, len(data))
    base.with_name(base.name + ".hed").write_bytes(bytes(hed))
    base.with_name(base.name + ".pkg").write_bytes(bytes(pkg))


class Reader(unittest.TestCase):
    def test_every_kind(self):
        rng = random.Random(1)
        cipher = ca.Cipher(TABLE)

        def blob(n):
            return bytes(rng.randrange(256) for _ in range(n))

        entries = {
            "tiny/eight.bin": (blob(8), "compressed", []),
            "stored/plain.bin": (blob(1000), "stored", []),
            "crypt/only.bin": (blob(777), "encrypted", []),
            "zip/big.bin": (bytes(5000) + blob(300), "compressed", []),
            "menu/fm/title.2ld": (
                blob(2049),
                "compressed",
                [
                    ("UK_title_2ld0.png", blob(4097), "encrypted"),
                    ("se/obj/nested.dds", bytes(3000) + blob(13), "compressed"),
                    ("tiny.bin", blob(5), "stored"),
                ],
            ),
        }
        with tempfile.TemporaryDirectory() as d:
            base = Path(d) / "test_first"
            write_archive(base, entries, cipher)
            with ca.Archive(base, cipher) as archive:
                for path, (data, _kind, assets) in entries.items():
                    entry = archive.read(path)
                    self.assertEqual(entry.data, data, path)
                    self.assertEqual([(n, a) for n, a in entry.assets], [(n, a) for n, a, _ in assets], path)
                out = Path(d) / "out"
                written = ca.materialise(archive, ["menu/fm/title.2ld"], out)
                self.assertEqual(written, 4)
                self.assertEqual(
                    (out / "remastered/menu/fm/title.2ld/se/obj/nested.dds").read_bytes(), assets_of(entries)[1]
                )
                with self.assertRaises(ca.ArchiveError):
                    archive.read("not/there.bin")

    def test_hash_names(self):
        key = hashlib.md5(b"some/path.bin").digest()
        self.assertEqual(ca.path_hash("some/path.bin"), key)
        self.assertEqual(ca.path_hash(key.hex().upper() + ".dat"), key)

    def test_table_found_by_its_digest(self):
        with tempfile.TemporaryDirectory() as d:
            exe = Path(d) / "game.exe"
            exe.write_bytes(bytes(48) + TABLE + bytes(32))
            saved = ca.TABLE_SHA256
            try:
                ca.TABLE_SHA256 = hashlib.sha256(TABLE).hexdigest()
                self.assertEqual(ca.load_table(exe), TABLE)
                exe.write_bytes(bytes(48 + 256 + 32))
                with self.assertRaises(ca.ArchiveError):
                    ca.load_table(exe)
            finally:
                ca.TABLE_SHA256 = saved

    def test_table_from_the_players_file_when_the_executable_is_not_read(self):
        """The Xbox app's copy: its executable is there and nothing reads it."""
        with tempfile.TemporaryDirectory() as d:
            kept = Path(d) / "table.bin"
            kept.write_bytes(TABLE)
            unread = Path(d) / "absent.exe"
            without = Path(d) / "game.exe"
            without.write_bytes(bytes(512))
            digest = mock.patch.object(ca, "TABLE_SHA256", hashlib.sha256(TABLE).hexdigest())
            with digest, mock.patch.dict(os.environ, {ca.TABLE_FILE_VARIABLE: str(kept)}):
                self.assertEqual(ca.load_table(unread), TABLE)
                self.assertEqual(ca.load_table(without), TABLE)
                kept.write_bytes(bytes(256))  # not the table: the executable's failure stands
                with self.assertRaises(OSError):
                    ca.load_table(unread)
                with self.assertRaises(ca.ArchiveError):
                    ca.load_table(without)
            with digest, mock.patch.dict(os.environ):
                os.environ.pop(ca.TABLE_FILE_VARIABLE, None)
                with self.assertRaises(OSError):
                    ca.load_table(unread)


class CommandLine(unittest.TestCase):
    def test_paths_held_or_named(self):
        cipher = ca.Cipher(TABLE)
        entries = {"map/tt02.map": (bytes(range(200)), "compressed", [("-0.dds", bytes(300), "stored")])}
        with tempfile.TemporaryDirectory() as d:
            base = Path(d) / "kh2_fifth"
            write_archive(base, entries, cipher)
            exe = Path(d) / "game.exe"
            exe.write_bytes(bytes(16) + TABLE)
            saved = ca.TABLE_SHA256
            try:
                ca.TABLE_SHA256 = hashlib.sha256(TABLE).hexdigest()
                out = Path(d) / "out"
                said = io.StringIO()
                with contextlib.redirect_stdout(said):
                    status = ca.main(["x", str(base), str(out), str(exe), "map/tt02.map", "map/tt99.map"])
                self.assertEqual(status, 0)
                self.assertIn("map/tt99.map is not in", said.getvalue())
                self.assertEqual((out / "original/map/tt02.map").read_bytes(), bytes(range(200)))
                self.assertTrue((out / "remastered/map/tt02.map/-0.dds").is_file())
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(ca.main(["x", str(base), str(out), str(exe), "map/tt99.map"]), 3)
                said = io.StringIO()
                with contextlib.redirect_stdout(said):
                    self.assertEqual(ca.main(["x", "--has", str(base), "map/tt01.map", "map/tt02.map"]), 0)
                self.assertEqual(said.getvalue().split(), ["map/tt02.map"])
            finally:
                ca.TABLE_SHA256 = saved


def assets_of(entries):
    return [a for _, a, _ in entries["menu/fm/title.2ld"][2]]


if __name__ == "__main__":
    unittest.main()
