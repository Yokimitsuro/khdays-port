"""tools/mods/kh2_enemies.py with no collection: a made-up KH2 object (a BAR holding an AI script that makes two calls),
its motions and effects files, its HD pages, the calls' shapes given in place of an executable's and the port's
handled calls given in place of khdays-native's: the mod's files, its list of kinds and which are covered."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

MODS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MODS))
import kh2_bdx  # noqa: E402
import kh2_enemies  # noqa: E402


def script(code: list[int]) -> bytes:
    """A script with one trigger (0), its code from the first word after the triggers."""
    first = (0x1C + 8 * 2 - 0x10) // 2
    head = b"made_up".ljust(16, b"\0") + struct.pack("<3i", 16, 64, 32) + struct.pack("<2i", 0, first)
    return head + struct.pack("<2i", 0, 0) + b"".join(struct.pack("<H", w) for w in code)


def bar(entries: list[tuple[int, bytes, bytes]]) -> bytes:
    out = b"BAR\x01" + struct.pack("<I", len(entries)) + bytes(8)
    at = 16 + 16 * len(entries)
    body = b""
    for kind, name, data in entries:
        out += struct.pack("<HH4sII", kind, 0, name, at + len(body), len(data))
        body += data
    return out + body


def syscall(kind: int, number: int) -> list[int]:
    return [0x000A | (kind << 6), number]


class Kh2EnemiesTest(unittest.TestCase):
    def collection(self, root: Path, name: str, with_pages: bool) -> None:
        obj = root / "original" / "obj"
        obj.mkdir(parents=True, exist_ok=True)
        # its script: 1:6, then 2:7, then exit
        mdlx = bar([(4, b"mdl", b"model"), (3, b"m_ex", script(syscall(1, 6) + syscall(2, 7) + [0x0049]))])
        (obj / f"{name}.mdlx").write_bytes(mdlx)
        (obj / f"{name}.mset").write_bytes(b"motions")
        (obj / f"{name}.a.us").write_bytes(b"effects")
        if with_pages:
            pages = root / "remastered" / "obj" / f"{name}.mdlx"
            pages.mkdir(parents=True)
            (pages / "-0.dds").write_bytes(b"page 0")
            effects = root / "remastered" / "obj" / f"{name}.a.us"
            effects.mkdir(parents=True)
            (effects / "-0.dds").write_bytes(b"effects' page 0")

    def test_the_mod(self):
        shapes = [(1, 6, 12, False), (2, 7, 1, True)]
        with tempfile.TemporaryDirectory() as d, mock.patch.object(kh2_bdx, "exe_calls", return_value=shapes):
            kh2 = Path(d) / "kh2.hed_out"
            self.collection(kh2, "M_EX010", with_pages=True)
            self.collection(kh2, "M_EX020", with_pages=False)
            (kh2 / "original" / "effect").mkdir(parents=True)
            (kh2 / "original" / "effect" / "texcommon.dpd").write_bytes(b"resident")
            (kh2 / "remastered" / "effect" / "texcommon.dpd").mkdir(parents=True)
            (kh2 / "remastered" / "effect" / "texcommon.dpd" / "-0.dds").write_bytes(b"resident page 0")
            (Path(d) / "kh2.exe").write_bytes(b"MZ")
            mod = Path(d) / "mods" / "Days - KH2 Enemies"
            asked = []

            def run(line):
                asked.append(line)
                return "kh2 enemies: the calls the port answers\n1:6\n2:7\n"

            left = kh2_enemies.make(kh2, Path(d) / "kh2.exe", mod, Path(d) / "khdays-native.exe", run)
            self.assertEqual(asked, [[str(Path(d) / "khdays-native.exe"), "--kh2-ai-calls"]])
            calls = (mod / "kh2" / "calls.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(calls[1:], ["1:6 12 0", "2:7 1 1"])
            for ext in (".mdlx", ".mset", ".a.us"):
                self.assertTrue((mod / "kh2" / "obj" / f"M_EX010{ext}").is_file(), ext)
            self.assertEqual((mod / "kh2" / "obj" / "M_EX010" / "-0.dds").read_bytes(), b"page 0")
            self.assertEqual((mod / "kh2" / "fx" / "M_EX010" / "-0.dds").read_bytes(), b"effects' page 0")
            self.assertEqual((mod / "kh2" / "effect" / "texcommon.dpd").read_bytes(), b"resident")
            self.assertEqual((mod / "kh2" / "fx" / "texcommon" / "-0.dds").read_bytes(), b"resident page 0")
            kinds = (mod / "kh2_enemies.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(kinds[1], "M_EX010\tSoldier\tyes\t2\t-")
            self.assertEqual(kinds[2], "M_EX020\tShadow\tyes\t2\t-")
            self.assertEqual(left, ["M_EX020 (Shadow): its model's HD pictures (its own PS2 pictures used)"])

    def test_missing_and_uncovered(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(kh2_bdx, "exe_calls", return_value=[]):
            kh2 = Path(d) / "kh2.hed_out"
            self.collection(kh2, "M_EX010", with_pages=True)
            (Path(d) / "kh2.exe").write_bytes(b"MZ")
            mod = Path(d) / "mod"
            left = kh2_enemies.make(kh2, Path(d) / "kh2.exe", mod, Path(d) / "x.exe", lambda line: "1:6\n")
            kinds = (mod / "kh2_enemies.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(kinds[1:], ["M_EX010\tSoldier\tno\t2\t2:7"])
            self.assertIn("M_EX020 (Shadow): KH2's M_EX020.mdlx, M_EX020.mset, M_EX020.a.us not there", left)
            self.assertTrue(any("texcommon" in line for line in left))
            # no port given: nothing covered
            left = kh2_enemies.make(kh2, Path(d) / "kh2.exe", mod)
            self.assertIn("M_EX010\tSoldier\tno\t2\t1:6 2:7", (mod / "kh2_enemies.txt").read_text(encoding="utf-8"))
            self.assertTrue(any("coverage" in line for line in left))


if __name__ == "__main__":
    unittest.main()
