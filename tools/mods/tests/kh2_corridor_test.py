"""KINGDOM HEARTS II's Dark Corridor copied into the KH2 Prizes mod (tools/mods/kh2_prizes.py make_corridor): the
effect and the resident pictures as they are, with their HD pages, where the port reads them
(port/runtime/mods/kh2_corridors.cpp); refused with the reason when a file is missing or not an effect. Its sounds
(make_corridor_sounds) decoded into the mod's sounds/ with their switch. Made-up files: every byte written by the
test, nothing of KH2's."""

import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import kh2_pax  # noqa: E402
import kh2_prizes  # noqa: E402
import kh2_scd_test  # noqa: E402

EFFECT, RESIDENT = "event/effect/tt_event_904.pax", "effect/texcommon.dpd"


def made_up_pax(entries: int = 1) -> bytes:
    """A PAX's frame: its header (the element table ending at 0x10), the 0x82 directory with `entries` 32-byte
    entries and one effect listed, the 0x96 block after them (five empty lists)."""
    directory = 0x10
    blocks = directory + 0x10 + 32 * entries
    block = blocks + 8
    out = bytearray(block + 4 + 5 * 4)
    out[:4] = b"PAX_"
    struct.pack_into("<I", out, 0xC, directory)
    struct.pack_into("<4I", out, directory, 0x82, 0, 0, entries)
    struct.pack_into("<2I", out, blocks, 1, block - directory)
    struct.pack_into("<I", out, block, 0x96)
    return bytes(out)


def collection(root: Path, pax: bytes, hd: int) -> Path:
    """KH2's entries as the installer extracts them: original/<entry>, remastered/<entry>/-<n>.dds."""
    kh2 = root / "kh2.hed_out"
    for entry, data in ((EFFECT, pax), (RESIDENT, struct.pack("<6I", 0x96, 0, 0, 0, 0, 0))):
        (kh2 / "original" / entry).parent.mkdir(parents=True, exist_ok=True)
        (kh2 / "original" / entry).write_bytes(data)
        for n in range(hd):
            page = kh2 / "remastered" / entry / f"-{n}.dds"
            page.parent.mkdir(parents=True, exist_ok=True)
            page.write_bytes(b"DDS " + bytes([n]) * 8)
    return kh2


class Corridor(unittest.TestCase):
    def test_reads_the_directory(self):
        pax = made_up_pax(entries=3)
        self.assertEqual(kh2_pax.data_block(pax), 0x10 + 0x10 + 32 * 3 + 8)
        with self.assertRaises(kh2_pax.PaxError):
            kh2_pax.data_block(b"PAX_" + bytes(12))  # no directory where the table ends
        with self.assertRaises(kh2_pax.PaxError):
            kh2_pax.data_block(b"BAR" + pax[3:])

    def test_copied_with_its_hd_pages(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            kh2 = collection(root, made_up_pax(), hd=12)
            mod = root / "mods" / "Days - KH2 Prizes"
            made, left_out = kh2_prizes.make_corridor(kh2, mod)
            self.assertEqual(left_out, [])
            self.assertEqual(made, ["the Dark Corridor: KH2's, 24 HD pages"])
            fx = mod / "kh2fx"
            self.assertEqual((fx / "tt_event_904.pax").read_bytes(), (kh2 / "original" / EFFECT).read_bytes())
            self.assertEqual((fx / "texcommon.dpd").read_bytes(), (kh2 / "original" / RESIDENT).read_bytes())
            # -10 and -11 kept as they are (the port names a page by its place in the order)
            self.assertEqual((fx / "texcommon" / "-11.dds").read_bytes(), b"DDS " + bytes([11]) * 8)
            self.assertEqual(len(list((fx / "tt_event_904").glob("-*.dds"))), 12)

    def test_without_hd_pages(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            made, left_out = kh2_prizes.make_corridor(collection(root, made_up_pax(), hd=0), root / "mod")
            self.assertEqual(made, ["the Dark Corridor: KH2's, 0 HD pages"])
            self.assertEqual(left_out, ["the Dark Corridor's HD pages: none there"])

    def test_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            kh2 = collection(root, b"PAX_" + bytes(12), hd=0)
            made, left_out = kh2_prizes.make_corridor(kh2, root / "mod")
            self.assertEqual(made, [])
            self.assertIn("not an effect the port reads", left_out[0])
            self.assertFalse((root / "mod" / "kh2fx").exists())
            (kh2 / "original" / RESIDENT).unlink()
            made, left_out = kh2_prizes.make_corridor(kh2, root / "mod")
            self.assertEqual(left_out, [f"the Dark Corridor: KH2's {RESIDENT} is not there"])


class CorridorSounds(unittest.TestCase):
    """Its sounds: the bank's recordings 9 (sound 10, the opening) and 15 (sound 16, the closing) as Days's oval's
    loop and disappear, its appear silent, all tied to the KH2 Dark Corridors switch."""

    def bank(self, root: Path, count: int) -> Path:
        kh2 = root / "kh2.hed_out"
        mono = kh2_scd_test.adpcm_format(1, 4, 8, rate=48001)
        recordings = [(1, 48001, 12, mono, kh2_scd_test.block([0], [16], [n], [0], [1, 2])) for n in range(count)]
        path = kh2 / "original" / kh2_prizes.CORRIDOR_BANK
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(kh2_scd_test.made_up_bank(recordings))
        return kh2

    def test_made(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            mod = root / "mod"
            made, left_out = kh2_prizes.make_corridor_sounds(self.bank(root, 16), mod)
            self.assertEqual(left_out, [])
            self.assertEqual(made, ["the Dark Corridor's sounds: KH2's, from Twilight Town's scene 904"])
            sounds = mod / "sounds" / "seqarc"
            for name, first in (("EV_DARK_APPEAR/se_ev_dark_loop", 9), ("EV_DARK_DISAPPEAR/se_ev_dark_disappear", 15)):
                with wave.open(str(sounds / f"{name}.wav")) as w:
                    self.assertEqual((w.getnchannels(), w.getframerate(), w.getnframes()), (1, 48001, 4))
                    self.assertEqual(struct.unpack("<4h", w.readframes(4)), (0, first, first + 16, first + 48))
            with wave.open(str(sounds / "EV_DARK_APPEAR" / "se_ev_dark_appear.wav")) as w:
                self.assertEqual(w.readframes(w.getnframes()), bytes(4))  # one silent frame
            switches = (mod / "sounds" / "switches.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(
                switches[1:], ["seqarc/EV_DARK_APPEAR kh2_corridors", "seqarc/EV_DARK_DISAPPEAR kh2_corridors"]
            )

    def test_left_out(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            made, left_out = kh2_prizes.make_corridor_sounds(root / "kh2.hed_out", root / "mod")
            self.assertEqual(left_out, [f"the Dark Corridor's sounds: KH2's {kh2_prizes.CORRIDOR_BANK} is not there"])
            made, left_out = kh2_prizes.make_corridor_sounds(self.bank(root, 12), root / "mod")
            self.assertEqual(made, [])
            self.assertIn("it has no recording 15", left_out[0])
            self.assertFalse((root / "mod" / "sounds").exists())


if __name__ == "__main__":
    unittest.main()
