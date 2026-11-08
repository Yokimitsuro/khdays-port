"""tools/mods/kh2_enemies.py with no collection: a made-up KH2 object (a BAR holding an AI script that makes two calls),
its motions and effects files, its HD pages, the calls' shapes given in place of an executable's and the port's
handled calls given in place of khdays-native's: the mod's files, its list of kinds and which are covered; the sounds
its motions and effects play, decoded from made-up banks."""

import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path
from unittest import mock

import numpy as np

MODS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MODS))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import kh2_bdx  # noqa: E402
import kh2_enemies  # noqa: E402
import kh2_scd_test  # noqa: E402


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

    def test_the_sounds(self):
        # a motion whose trigger 8 plays (200, 21), an effect's element playing 105 (bank 0), another none (-1)
        attributes = struct.pack("<BBH", 0, 1, 0) + struct.pack("<HBB4h", 30, 8, 4, 200, 21, 0, 0)
        mset = bar([(17, b"00A", bar([(9, b"00A", b"motion"), (0x10, b"00A", attributes)]))])
        elements = [struct.pack("<HHI", 0, 0, 0x40000000).ljust(0x18, b"\0") + struct.pack("<i", 105)]
        elements.append(struct.pack("<HHI", 1, 1, 0x40000000).ljust(0x18, b"\0") + struct.pack("<i", -1))
        records = b"".join(e.ljust(0x50, b"\0") for e in elements)
        pax = b"PAX_" + struct.pack("<3I", 0, len(elements), 0x10 + len(records)) + records
        self.assertEqual(kh2_enemies.motion_sounds(mset), {200021})
        self.assertEqual(kh2_enemies.effect_sounds(bar([(0x12, b"m_ex", pax)])), {105})
        mono = kh2_scd_test.adpcm_format(1, 4, 8)
        recording = (1, 48000, 12, mono, kh2_scd_test.block([0], [16], [100], [50], [1, 2]))
        with tempfile.TemporaryDirectory() as d:
            kh2 = Path(d) / "kh2.hed_out"
            own = kh2 / "remastered" / "obj" / "M_EX010.a.us" / "se" / "obj" / "se200"
            own.parent.mkdir(parents=True)
            sounds = [None] * 21 + [(0.5, [(0, 0)])]
            own.write_bytes(kh2_scd_test.made_up_sounds_bank(sounds, [recording]))
            common = kh2 / "original" / kh2_enemies.COMMON_BANK
            common.parent.mkdir(parents=True)
            common.write_bytes(kh2_scd_test.made_up_sounds_bank([None] * 105 + [(1.0, [(0, 0)])], [recording]))
            to = Path(d) / "se"
            left = kh2_enemies.make_sounds(kh2, "M_EX010", {200021, 105, 200022, 7001}, to)
            with wave.open(str(to / "200021.wav")) as w:  # at its volume: half
                self.assertEqual(np.frombuffer(w.readframes(4), "<i2").tolist(), [25, 50, 58, 74])
            with wave.open(str(to / "105.wav")) as w:
                self.assertEqual(np.frombuffer(w.readframes(4), "<i2").tolist(), [50, 100, 116, 148])
            self.assertEqual(len(left), 2)
            self.assertTrue(any("200022" in line and "no recording" in line for line in left), left)
            self.assertTrue(any("7001" in line and "se7" in line for line in left), left)

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
