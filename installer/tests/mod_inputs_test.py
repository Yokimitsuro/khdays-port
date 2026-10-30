"""mod_inputs.txt (the collection files the port's own mods' tools read, traced by `setup_steps.py
trace-mod-inputs`) is not stale: it was traced with the tools as they are now, and every line names an archive the
installer extracts for them."""

import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import setup_steps as s  # noqa: E402


class Inputs(unittest.TestCase):
    def test_traced_with_these_tools(self):
        digest = s.mods_digest(HERE.parent.parent)
        lines = s.MOD_INPUTS_FILE.read_text(encoding="utf-8").splitlines()
        recorded = [line.split()[-1] for line in lines if line.startswith("# tools ")]
        self.assertEqual(
            recorded,
            [digest],
            "a tool or picture of tools/mods changed since mod_inputs.txt was traced: run setup_steps.py "
            "trace-mod-inputs",
        )

    def test_lines(self):
        entries = s.mod_inputs()
        self.assertTrue(entries)
        for archive, paths in entries.items():
            self.assertIn(archive, s.MOD_SOURCES, archive)
            self.assertIn(archive, s.COLLECTION_ARCHIVES["1.5+2.5"], f"{archive}: the mods read no 2.8 archive")
            for path in paths:
                self.assertTrue(path and not path.startswith("/"), f"{archive} {path}")
        # the Moogle Shop's Re:CoM layout and Birth by Sleep's camp (an entry known by its MD5 alone)
        self.assertIn("FORM/0109/FO0109.SPR", entries.get("Recom", []))
        self.assertIn("cba7b0396c9efe756ba1e069e6dd603e.dat", entries.get("bbs_first", []))
        # KH2's prize effect (tools/mods/kh2_prizes.py)
        self.assertIn("obj/PRIZE.mdlx", entries.get("kh2_first", []) + entries.get("kh2_fifth", []))
        # the chests (kh2_prizes.py through khdays-native) and Neverland's Birth by Sleep pictures
        self.assertIn("obj/F_EX040_TT.mdlx", entries.get("kh2_first", []) + entries.get("kh2_fifth", []))
        self.assertIn("ForEventJP/gimmick/g01pp/00/g01pp00.pmo", entries.get("bbs_second", []))

    def test_wiring(self):
        folder, template = s.PORT_MODS["menu-overhaul"]
        self.assertEqual(folder, "Days - Menu Overhaul")
        places = {a[1:-1] for a in template if a.startswith("{")}
        # every place the command line names is one build_mods fills in (setup_steps.mod_places)
        self.assertEqual(places, {"kh2", "bbs", "recom", "mare", "hd", "fonts", "data", "exe"})
        with tempfile.TemporaryDirectory() as temp:
            install = Path(temp)
            (s.extracted_root(install) / "0123456789abcdef" / "nitrofs").mkdir(parents=True)
            filled = set(s.mod_places(install, install / "src"))
            self.assertLessEqual(places, filled)
            # every mod's places are filled in; each collection mod is one the trace runs
            for mod, (_, line) in s.PORT_MODS.items():
                self.assertLessEqual({a[1:-1] for a in line if a.startswith("{")}, filled, mod)
        self.assertEqual(s.PORT_MODS["kh2-prizes"][0], "Days - KH2 Prizes")
        self.assertLessEqual(set(s.COLLECTION_MODS), set(s.PORT_MODS))
        self.assertNotIn("walk", s.COLLECTION_MODS)


if __name__ == "__main__":
    unittest.main()
