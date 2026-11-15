"""tools/mods/kh2_enemies.py with no collection: a made-up KH2 object (a BAR holding an AI script that makes two calls),
its motions and effects files, its HD pages, the calls' shapes given in place of an executable's and the port's
handled calls given in place of khdays-native's: the mod's files, its list of kinds and which are covered; the sounds
its motions and effects play, decoded from made-up banks."""

import math
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
            # what the reaction commands read: the player's skeleton, his keyblade and its effects' HD page, the
            # commands, their names in one language
            (kh2 / "original" / "obj" / "P_EX100.mdlx").write_bytes(b"the player")
            (kh2 / "original" / "obj" / "W_EX010.mdlx").write_bytes(b"the keyblade")
            (kh2 / "original" / "obj" / "W_EX010.a.us").write_bytes(b"its trail")
            (kh2 / "remastered" / "obj" / "W_EX010.a.us").mkdir(parents=True)
            (kh2 / "remastered" / "obj" / "W_EX010.a.us" / "-0.dds").write_bytes(b"trail's page 0")
            (kh2 / "original" / "03system.bin").write_bytes(b"commands")
            (kh2 / "original" / "00battle.bin").write_bytes(b"attacks")
            (kh2 / "original" / "00objentry.bin").write_bytes(b"entries")
            (kh2 / "original" / "msg" / "us").mkdir(parents=True)
            (kh2 / "original" / "msg" / "us" / "sys.bar").write_bytes(b"names")
            # the journal (no character of a kind here) and its messages
            (kh2 / "original" / "msg" / "us" / "jm.bar").write_bytes(b"journal names")
            (kh2 / "original" / "menu" / "us").mkdir(parents=True)
            (kh2 / "original" / "menu" / "us" / "jiminy.bar").write_bytes(b"journal")
            # the general command menu and its HD picture in one language
            (kh2 / "original" / "field2d" / "us").mkdir(parents=True)
            (kh2 / "original" / "field2d" / "us" / "zz0command.2dd").write_bytes(b"menu")
            (kh2 / "remastered" / "field2d" / "us" / "zz0command.2dd").mkdir(parents=True)
            (kh2 / "remastered" / "field2d" / "us" / "zz0command.2dd" / "US_zz0command_2dd0.png").write_bytes(b"hd")
            (kh2 / "original" / "field2d" / "us" / "zz0field.2dd").write_bytes(b"marks")
            (kh2 / "remastered" / "field2d" / "us" / "zz0field.2dd").mkdir(parents=True)
            (kh2 / "remastered" / "field2d" / "us" / "zz0field.2dd" / "US_zz0field_2dd0.png").write_bytes(b"marks hd")
            # KH2's font: its widths and HD picture
            (kh2 / "original" / "msg" / "us" / "fontinfo.bar").write_bytes(b"widths")
            (kh2 / "remastered" / "msg" / "us" / "fontimage.bar").mkdir(parents=True)
            (kh2 / "remastered" / "msg" / "us" / "fontimage.bar" / "US_fontimage_bar0.png").write_bytes(b"glyphs")
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
            self.assertEqual((mod / "kh2" / "obj" / "P_EX100.mdlx").read_bytes(), b"the player")
            self.assertEqual((mod / "kh2" / "obj" / "W_EX010.mdlx").read_bytes(), b"the keyblade")
            self.assertEqual((mod / "kh2" / "obj" / "W_EX010.a.us").read_bytes(), b"its trail")
            self.assertEqual((mod / "kh2" / "fx" / "W_EX010" / "-0.dds").read_bytes(), b"trail's page 0")
            self.assertEqual((mod / "kh2" / "03system.bin").read_bytes(), b"commands")
            self.assertEqual((mod / "kh2" / "00battle.bin").read_bytes(), b"attacks")
            self.assertEqual((mod / "kh2" / "00objentry.bin").read_bytes(), b"entries")
            self.assertEqual((mod / "kh2" / "msg" / "us" / "sys.bar").read_bytes(), b"names")
            self.assertEqual((mod / "kh2" / "msg" / "us" / "jm.bar").read_bytes(), b"journal names")
            self.assertEqual((mod / "kh2" / "field2d" / "us" / "zz0command.2dd").read_bytes(), b"menu")
            self.assertEqual((mod / "kh2" / "field2d" / "us" / "US_zz0command_2dd0.png").read_bytes(), b"hd")
            self.assertEqual((mod / "kh2" / "field2d" / "us" / "zz0field.2dd").read_bytes(), b"marks")
            self.assertEqual((mod / "kh2" / "field2d" / "us" / "US_zz0field_2dd0.png").read_bytes(), b"marks hd")
            self.assertEqual((mod / "kh2" / "msg" / "us" / "fontinfo.bar").read_bytes(), b"widths")
            self.assertEqual((mod / "kh2" / "msg" / "us" / "US_fontimage_bar0.png").read_bytes(), b"glyphs")
            kinds = (mod / "kh2_enemies.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(kinds[1], "M_EX010\tSoldier\tyes\t2\t-\t-\t-\t-\t-\t-\t-\t-\t0")
            self.assertEqual(kinds[2], "M_EX020\tShadow\tyes\t2\t-\t-\t-\t-\t-\t-\t-\t-\t0")
            ours = [line for line in left if line.startswith(("M_EX010", "M_EX020"))]
            self.assertEqual(ours, ["M_EX020 (Shadow): its model's HD pictures (its own PS2 pictures used)"])
            # the other kinds the mod holds, not in this made-up collection: each said
            others = [n for n in kh2_enemies.KINDS if n not in ("M_EX010", "M_EX020")]
            # and KH2's areas (no boss's companions without them)
            self.assertTrue(any(line.startswith("original/ard/us/*.ard") for line in left))
            self.assertEqual(len(left), 2 + len(others))
            self.assertTrue(all(any(line.startswith(n) and "not there" in line for line in left) for n in others))

    def test_the_sounds(self):
        # a motion whose trigger 8 plays (200, 21), an effect's element playing 105 (bank 0), another none (-1)
        attributes = struct.pack("<BBH", 0, 1, 0) + struct.pack("<HBB4h", 30, 8, 4, 200, 21, 0, 0)
        mset = bar([(17, b"00A", bar([(9, b"00A", b"motion"), (0x10, b"00A", attributes)]))])
        self.assertEqual(kh2_enemies.motion_sounds(mset), {200021})
        # a slot of the player's motions (type 0x14): his body's part plays (200, 24), his weapon's the keyblade's (500,
        # 49); a part of no size repeats another's
        body = struct.pack("<BBH", 0, 1, 0) + struct.pack("<HBB4h", 16, 8, 4, 200, 24, 0, 0)
        weapon = struct.pack("<BBH", 0, 1, 0) + struct.pack("<HBB4h", 70, 8, 4, 500, 49, 0, 0)
        parts = bar([(0x11, b"0010", bar([(9, b"AA01", b"motion"), (0x10, b"AA01", body)])),
                     (0x11, b"0011", bar([(9, b"AA01", b"motion"), (0x10, b"AA01", weapon)])),
                     (0x11, b"0012", b"")])
        mset = bar([(17, b"00A", bar([(9, b"00A", b"motion"), (0x10, b"00A", attributes)])), (0x14, b"AA01", parts)])
        elements = [struct.pack("<HHI", 0, 0, 0x40000000).ljust(0x18, b"\0") + struct.pack("<i", 105)]
        elements.append(struct.pack("<HHI", 1, 1, 0x40000000).ljust(0x18, b"\0") + struct.pack("<i", -1))
        records = b"".join(e.ljust(0x50, b"\0") for e in elements)
        pax = b"PAX_" + struct.pack("<3I", 0, len(elements), 0x10 + len(records)) + records
        self.assertEqual(kh2_enemies.motion_sounds(mset), {200021, 200024, 500049})
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
            # a bank its own .a.us lacks, another object's carries (the cubes' 272, the Sorcerer's)
            left = kh2_enemies.make_sounds(kh2, "M_EX870", {200021}, to)
            self.assertEqual(left, [])

    def test_entries_motions_and_voices(self):
        # 00objentry.bin: Hades of the Coliseum's cups moves by B_HE030.mset; a model no entry names, its own
        def entry(number: int, model: bytes, motions: bytes) -> bytes:
            return struct.pack("<IBBBB", number, 3, 0, 0, 0) + model.ljust(32, b"\0") + motions.ljust(32, b"\0")

        records = [entry(0x15E, b"B_HE030", b"B_HE030.mset"), entry(0x90E, b"B_HE030_CLSM", b"B_HE030.mset")]
        objentry = struct.pack("<II", 3, len(records)) + b"".join(r.ljust(96, b"\0") for r in records)
        self.assertEqual(kh2_enemies.motion_set_of(objentry, "B_HE030_CLSM"), "B_HE030.mset")
        self.assertEqual(kh2_enemies.motion_set_of(objentry, "M_EX010"), "M_EX010.mset")
        self.assertEqual(kh2_enemies.motion_set_of(b"", "M_EX010"), "M_EX010.mset")
        # its voice bank's sounds by number (an empty place left out); no bank, nothing; a bank not there said
        mono = kh2_scd_test.adpcm_format(1, 4, 8)
        recording = (1, 48000, 12, mono, kh2_scd_test.block([0], [16], [100], [50], [1, 2]))
        with tempfile.TemporaryDirectory() as d:
            kh2 = Path(d) / "kh2.hed_out"
            bank = kh2 / "original" / "voice" / "us" / "battle" / "he_hades.win32.scd"
            bank.parent.mkdir(parents=True)
            pages = b"OggS header"
            extra = struct.pack("<HB", 0x2002, 7).ljust(0x10, b"\0") + struct.pack("<II", 0, len(pages))
            ogg = (1, 48000, 6, extra.ljust(0x20, b"\0") + bytes(b ^ 7 for b in pages), b"OggS audio")
            bank.write_bytes(
                kh2_scd_test.made_up_sounds_bank([None, (1.0, [(0, 0)]), (0.5, [(0, 1)])], [recording, ogg])
            )
            to = Path(d) / "voice" / "B_HE030_CLSM"
            self.assertEqual(kh2_enemies.make_voices(kh2, "B_HE030_CLSM", "he_hades", to), [])
            self.assertTrue((to / "1.wav").is_file())
            self.assertFalse((to / "0.wav").exists())
            # the battle banks' Ogg Vorbis as it is, its volume beside it
            self.assertEqual((to / "2.ogg").read_bytes(), b"OggS headerOggS audio")
            self.assertEqual((to / "volumes.txt").read_text(encoding="utf-8"), "2 0.5\n")
            self.assertEqual(kh2_enemies.make_voices(kh2, "M_EX010", None, Path(d) / "none"), [])
            left = kh2_enemies.make_voices(kh2, "B_EX130", "n_dragoon", Path(d) / "x")
            self.assertTrue(len(left) == 1 and "n_dragoon.win32.scd" in left[0], left)

    def test_no_model(self):
        # an object with no model and no motions of its own (the Possessor): its .mdlx and effects alone
        with tempfile.TemporaryDirectory() as d, mock.patch.object(kh2_bdx, "exe_calls", return_value=[]):
            kh2 = Path(d) / "kh2.hed_out"
            obj = kh2 / "original" / "obj"
            obj.mkdir(parents=True)
            (obj / "B_BB130.mdlx").write_bytes(bar([(3, b"b_bb", script(syscall(1, 6) + [0x0049]))]))
            (obj / "B_BB130.a.us").write_bytes(b"effects")
            (Path(d) / "kh2.exe").write_bytes(b"MZ")
            mod = Path(d) / "mod"
            left = kh2_enemies.make(kh2, Path(d) / "kh2.exe", mod, Path(d) / "x.exe", lambda line: "1:6\n")
            self.assertTrue((mod / "kh2" / "obj" / "B_BB130.mdlx").is_file())
            self.assertTrue((mod / "kh2" / "obj" / "B_BB130.a.us").is_file())
            self.assertFalse((mod / "kh2" / "obj" / "B_BB130.mset").exists())
            self.assertFalse(any(line.startswith("B_BB130") for line in left), left)
            kinds = (mod / "kh2_enemies.txt").read_text(encoding="utf-8").splitlines()
            self.assertIn("B_BB130\tPossessor\tyes\t1\t-\t-\t-\t-\t-\t-\t-\t-\t0", kinds)

    def test_only_what_is_asked(self):
        # a mod of one's own: the objects asked for alone (any of KH2's, in KINDS or not), the shared files with them,
        # what one makes and was not asked for told
        with (
            tempfile.TemporaryDirectory() as d,
            mock.patch.object(kh2_bdx, "exe_calls", return_value=[]),
            mock.patch.object(kh2_enemies, "entry_kinds", return_value={5: ("W_TEST", 0)}),
            mock.patch.object(kh2_enemies, "made_numbers", return_value={5}),
        ):
            kh2 = Path(d) / "kh2.hed_out"
            for name in ("M_EX010", "M_EX020", "X_NEW"):
                self.collection(kh2, name, with_pages=False)
            (Path(d) / "kh2.exe").write_bytes(b"MZ")
            mod = Path(d) / "mod"
            left = kh2_enemies.make(
                kh2, Path(d) / "kh2.exe", mod, Path(d) / "x.exe", lambda line: "1:6\n", only={"m_ex010", "X_NEW"}
            )
            kinds = (mod / "kh2_enemies.txt").read_text(encoding="utf-8").splitlines()[1:]
            self.assertEqual([k.split("\t")[:2] for k in kinds], [["M_EX010", "Soldier"], ["X_NEW", "X_NEW"]])
            self.assertTrue((mod / "kh2" / "obj" / "X_NEW.mdlx").is_file())
            self.assertFalse((mod / "kh2" / "obj" / "M_EX020.mdlx").exists())
            self.assertTrue((mod / "kh2" / "calls.txt").is_file())
            self.assertIn("M_EX010 makes or brings W_TEST, not asked for (--only): its script finds none", left)

    def test_missing_and_uncovered(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(kh2_bdx, "exe_calls", return_value=[]):
            kh2 = Path(d) / "kh2.hed_out"
            self.collection(kh2, "M_EX010", with_pages=True)
            (Path(d) / "kh2.exe").write_bytes(b"MZ")
            mod = Path(d) / "mod"
            left = kh2_enemies.make(kh2, Path(d) / "kh2.exe", mod, Path(d) / "x.exe", lambda line: "1:6\n")
            kinds = (mod / "kh2_enemies.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(kinds[1:], ["M_EX010\tSoldier\tno\t2\t2:7\t-\t-\t-\t-\t-\t-\t-\t0"])
            self.assertIn("M_EX020 (Shadow): KH2's M_EX020.mdlx not there", left)
            self.assertTrue(any("texcommon" in line for line in left))
            self.assertTrue(any("zz0command" in line for line in left))
            self.assertTrue(any("zz0field" in line for line in left))
            self.assertTrue(any("fontinfo" in line for line in left))
            # no port given: nothing covered
            left = kh2_enemies.make(kh2, Path(d) / "kh2.exe", mod)
            self.assertIn("M_EX010\tSoldier\tno\t2\t1:6 2:7\t-", (mod / "kh2_enemies.txt").read_text(encoding="utf-8"))
            # a script that plays the player's motions (1:29): "player"
            line = kh2_enemies.kinds_line("B_HE020", "Cerberus", {(1, 29)}, {(1, 29)})
            self.assertTrue(line.endswith("\tyes\t1\t-\tplayer\t-\t-\t-\t-\t-\t-\t0"))
            self.assertTrue(any("coverage" in line for line in left))

    def test_journal_names(self):
        # 00objentry.bin's numbers and models; jiminy.bar's JMCH: 36-byte records, + 6 the title, + 0xc the object
        def entry(number: int, model: bytes) -> bytes:
            return struct.pack("<IBBBB", number, 3, 0, 0, 0) + model.ljust(32, b"\0")

        records = [entry(301, b"M_EX010"), entry(2273, b"B_HE030_GM"), entry(2265, b"B_AL100_FIRE_GM"),
                   entry(119, b"M_EX200_NM"), entry(95, b"P_EX110")]
        objentry = struct.pack("<II", 3, len(records)) + b"".join(r.ljust(96, b"\0") for r in records)

        def character(title: int, number: int) -> bytes:
            return bytes(4) + struct.pack("<HHHHH", 0, title, 0, 0, number).ljust(32, b"\0")

        people = [character(11932, 301), character(11648, 2273), character(12022, 2265), character(1, 301),
                  character(11984, 119), character(11700, 95)]
        jmch = b"JMCH" + struct.pack("<III", 1, len(people), 0) + b"".join(people)
        jiminy = bar([(0, b"char", b"JMWO" + bytes(12)), (0, b"char", jmch)])
        titles = kh2_enemies.journal_titles(jiminy, objentry)
        # each model's first; its own, its journal's (_GM), its family's, one named on from it
        self.assertEqual(titles["M_EX010"], 11932)
        self.assertEqual(kh2_enemies.journal_title(titles, "M_EX010"), 11932)
        self.assertEqual(kh2_enemies.journal_title(titles, "B_AL100_FIRE"), 12022)
        self.assertEqual(kh2_enemies.journal_title(titles, "B_HE030_CLSM"), 11648)
        self.assertEqual(kh2_enemies.journal_title(titles, "M_EX200"), 11984)
        self.assertIsNone(kh2_enemies.journal_title(titles, "B_LK110"))
        # one with no entry of its own named by its character's other object (FINAL MIX's hooded Roxas by the
        # prologue's)
        self.assertEqual(kh2_enemies.journal_title(titles, "B_EX390"), 11700)
        self.assertEqual(kh2_enemies.journal_titles(b"", objentry), {})
        # its line's last column: the title's number, "-" none
        self.assertTrue(
            kh2_enemies.kinds_line("M_EX010", "Soldier", {(1, 6)}, {(1, 6)}, 11932).endswith("\t-\t11932\t-\t-\t-\t-\t-\t0")
        )

    def test_mission_pages(self):
        # a mission's HD pictures (one for each of its IMGDs) beside its file, its other pages not
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "remastered" / "msn" / "us" / "HE17_MS105.bar"
            source.mkdir(parents=True)
            for name in ("US_HE17_MS105_bar0.png", "US_HE17_MS105_bar1.png", "-0.dds"):
                (source / name).write_bytes(name.encode())
            to = Path(folder) / "mod" / "kh2" / "msn" / "HE17_MS105"
            self.assertEqual(kh2_enemies.copy_pages(source, to, "*_bar*.png"), 2)
            self.assertEqual(sorted(p.name for p in to.iterdir()), ["US_HE17_MS105_bar0.png", "US_HE17_MS105_bar1.png"])
            self.assertEqual(kh2_enemies.copy_pages(source / "none", to, "*_bar*.png"), 0)

    def test_effect_pages_dds_and_png(self):
        # an .a.us's HD pages are DDS, or PNG for some (Zexion's B_EX370.a.us: his effects' and his command menu's)
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "remastered" / "obj" / "B_EX370.a.us"
            (source / "se").mkdir(parents=True)
            for name in ("-0.png", "-1.png", "-2.dds", "notes.txt"):
                (source / name).write_bytes(name.encode())
            to = Path(folder) / "mod" / "kh2" / "fx" / "B_EX370"
            self.assertEqual(kh2_enemies.copy_pages(source, to), 3)
            self.assertEqual(sorted(p.name for p in to.iterdir()), ["-0.png", "-1.png", "-2.dds"])

    def test_made_numbers(self):
        # trigger 0 gives 956 to a routine that makes an object by its parameter (1:0: Oogie Boogie's gift boxes so),
        # then makes 7 itself; the routine's parameter traced to its caller's push
        def push_int(n: int) -> list[int]:
            return [0x0000, n & 0xFFFF, n >> 16]

        main = push_int(956) + [0x0008, 6] + push_int(7) + syscall(1, 0) + [0x0049]
        maker = [0x0001, 0, 0x0030, 0] + syscall(1, 0) + [0x0089]
        mdlx = bar([(3, b"b_nm", script(main + maker))])
        self.assertEqual(kh2_enemies.made_numbers(mdlx), {956, 7})
        self.assertEqual(kh2_enemies.made_numbers(bar([(3, b"b_nm", script(syscall(1, 6) + [0x0049]))])), set())

    def test_form_numbers(self):
        # the forms a script turns the player into (1:242's argument: Luxord's card 0x601 and die 0x602), apart from
        # the objects it makes (1:0)
        def push_int(n: int) -> list[int]:
            return [0x0000, n & 0xFFFF, n >> 16]

        main = push_int(0x601) + syscall(1, 242) + push_int(0x602) + syscall(1, 242) + push_int(5) + syscall(1, 0)
        mdlx = bar([(3, b"b_ex", script(main + [0x0049]))])
        # (1:242's shape, one argument and nothing back, as KH2's executable gives it: the walk reads past it)
        with mock.patch.dict(kh2_bdx.SHAPES, {(1, 242): (1, 0), (1, 0): (1, 1)}):
            self.assertEqual(kh2_enemies.form_numbers(mdlx), {0x601, 0x602})
            self.assertEqual(kh2_enemies.made_numbers(mdlx), {5})

    def test_companions(self):
        # 00objentry.bin: the Hydra (a large enemy, 0xc), the player (0), Phil and Megara (friends, 9), a Soldier (4)
        def entry(number: int, kind: int, model: bytes) -> bytes:
            return struct.pack("<IBBBB", number, kind, 0, 0, 0) + model.ljust(32, b"\0")

        records = [entry(352, 0xC, b"B_HE100"), entry(566, 0, b"PLAYER"), entry(364, 9, b"N_HE020_BTL"),
                   entry(365, 9, b"N_HE030_BTL"), entry(301, 4, b"M_EX010")]
        objentry = struct.pack("<II", 3, len(records)) + b"".join(r.ljust(96, b"\0") for r in records)
        entries = kh2_enemies.entry_kinds(objentry)
        self.assertEqual(entries[352], ("B_HE100", 0xC))

        # a spawn file (OpenKH's ARD page): set 0 of type 2 with the Hydra, the player, Phil and a Soldier, a walk path
        # of one point and a signal; set 1 of type 5 (its scripts' to make) with Megara
        def placed(number: int, x: float, y: float, z: float, yaw: float) -> bytes:
            return struct.pack("<I6f", number, x, y, z, 0.0, yaw, 0.0).ljust(0x40, b"\0")

        def descriptor(kind: int, objects: int, paths: int, signals: int) -> bytes:
            return struct.pack("<BBh4hI", kind, 0, 1, objects, 0, paths, 0, signals).ljust(0x2C, b"\0")

        first = descriptor(2, 4, 1, 1) + placed(352, 0, 0, 0, 0) + placed(566, 0, 0, -1000, 3.14)
        first += placed(364, 100, -500, 300, 0.5) + placed(301, 50, 0, 50, 0)
        first += struct.pack("<hhBBh", 0, 1, 0, 0, 0) + struct.pack("<3f", 1, 2, 3) + bytes(8)
        second = descriptor(5, 1, 0, 0) + placed(365, 10, 0, 10, 0)
        spawn = struct.pack("<II", 2, 2) + first + second
        ard = bar([(0x0D, b"btl\0", bytes(4)), (0x0C, b"b_40", spawn)])
        self.assertEqual([(n, [k for k, _ in s]) for n, s in kh2_enemies.spawn_sets(ard)], [("b_40", [2, 5])])
        kinds = {"B_HE100", "N_HE020_BTL", "N_HE030_BTL", "M_EX010"}
        # Phil alone (the Soldier an enemy, Megara her script's), turned so that the player is + z: half a turn
        ((name, x, y, z, yaw),) = kh2_enemies.companions([ard], entries, "B_HE100", kinds)
        self.assertEqual(name, "N_HE020_BTL")
        self.assertAlmostEqual(x, -100.0, places=3)
        self.assertEqual(y, -500.0)
        self.assertAlmostEqual(z, -300.0, places=3)
        self.assertAlmostEqual(yaw, 0.5 - math.pi, places=3)
        # its battle set's walk path (number 0, its point (1, 2, 3)) in the same frame: half a turn round the Hydra
        ((number, points),) = kh2_enemies.walk_paths([ard], entries, "B_HE100")
        self.assertEqual(number, 0)
        ((px, py, pz),) = points
        self.assertAlmostEqual(px, -1.0, places=3)
        self.assertEqual(py, 2.0)
        self.assertAlmostEqual(pz, -3.0, places=3)
        self.assertEqual(kh2_enemies.walk_paths([ard], entries, "M_EX010"), [])
        line = kh2_enemies.kinds_line("B_HE100", "Hydra", {(1, 6)}, {(1, 6)}, paths=[(0, points)])
        self.assertTrue(line.endswith("\t-\t0 -1.0 2.0 -3.0\t-\t-\t0"))
        # its room's objects: the F_ objects its script finds by number (1:114) where its area places them, KH2's
        # world; a boss whose script finds none is bound to no room
        floor = struct.pack("<IBBBB", 700, 3, 0, 0, 0) + b"F_HE999".ljust(32, b"\0")
        entries_more = kh2_enemies.entry_kinds(
            struct.pack("<II", 3, len(records) + 1) + b"".join(r.ljust(96, b"\0") for r in [*records, floor])
        )
        piece = placed(700, 10, 20, 30, 1.5)
        piece = piece[:0x20] + struct.pack("<i", 7) + piece[0x24:]
        room_spawn = struct.pack("<II", 2, 1) + descriptor(1, 1, 0, 0) + piece
        room_ard = bar([(0x0D, b"btl\0", bytes(4)), (0x0C, b"b_40", spawn), (0x0C, b"m_50", room_spawn)])
        area, pieces = kh2_enemies.room_objects([("he99", room_ard)], entries_more, "B_HE100", {700, 364})
        self.assertEqual(area, "he99")
        self.assertEqual(pieces, [("F_HE999", 10.0, 20.0, 30.0, 1.5, 7)])
        self.assertEqual(kh2_enemies.unit_argument([room_ard], entries_more, "B_HE100"), 0)
        self.assertEqual(kh2_enemies.unit_argument([room_ard], entries_more, "M_EX010"), 0)
        self.assertEqual(kh2_enemies.room_objects([("he99", room_ard)], entries_more, "B_HE100", {364}), (None, []))
        self.assertEqual(kh2_enemies.room_objects([("he99", room_ard)], entries_more, "M_EX010", {700}), (None, []))
        line = kh2_enemies.kinds_line("B_HE100", "Hydra", set(), set(), home=area, room=pieces, argument=3)
        self.assertTrue(line.endswith("he99" + "\t" + "F_HE999 10.0 20.0 30.0 1.500 7" + "\t" + "3"), line)
        # an enemy brings none; nor a boss the mod has no companion kind for
        self.assertEqual(kh2_enemies.companions([ard], entries, "M_EX010", kinds), [])
        self.assertEqual(kh2_enemies.companions([ard], entries, "B_HE100", {"B_HE100"}), [])
        # the mission the area's battle program runs with that spawn (operation 0x15: the id, the msn's name)
        program = struct.pack("<hh", 0, 1) + b"b_40" + struct.pack("<hh", 0x15, 9) + struct.pack("<I", 75)
        program += b"HE17_MS105".ljust(32, b"\0")
        script = struct.pack("<hh", 123, 4 + len(program)) + program
        with_btl = bar([(0x0D, b"btl\0", script), (0x0C, b"b_40", spawn)])
        self.assertEqual(kh2_enemies.mission_of([with_btl], entries, "B_HE100"), "HE17_MS105")
        self.assertIsNone(kh2_enemies.mission_of([ard], entries, "B_HE100"))
        self.assertIsNone(kh2_enemies.mission_of([with_btl], entries, "M_EX010"))
        line = kh2_enemies.kinds_line("B_HE100", "Hydra", {(1, 6)}, {(1, 6)}, None, [(name, x, y, z, yaw)])
        self.assertTrue(line.endswith("\t-\tN_HE020_BTL -100.0 -500.0 -300.0 -2.642\t-\t-\t-\t-\t0"), line)


if __name__ == "__main__":
    unittest.main()
