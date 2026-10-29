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

    def test_wiring(self):
        folder, template = s.PORT_MODS["menu-overhaul"]
        self.assertEqual(folder, "Days - Menu Overhaul")
        places = {a[1:-1] for a in template if a.startswith("{")}
        # every place the command line names is one build_mods fills in (setup_steps.mod_places)
        self.assertEqual(places, {"kh2", "bbs", "recom", "mare", "hd", "fonts", "data", "exe"})
        with tempfile.TemporaryDirectory() as temp:
            install = Path(temp)
            (s.extracted_root(install) / "0123456789abcdef" / "nitrofs").mkdir(parents=True)
            self.assertEqual(places, set(s.mod_places(install, install / "src")))


if __name__ == "__main__":
    unittest.main()
