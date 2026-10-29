"""hd_inputs.txt (the collection files the HD tools read, traced by `setup_steps.py trace-inputs`) is
not stale: it was traced with the HD tools as they are now, and every line names a known archive."""

import ast
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import setup_steps as s  # noqa: E402

ARCHIVES = {"Mare", "SettingMenu", "kh2_first", "kh2_fifth", "kh3d_first", "movies"}


class Inputs(unittest.TestCase):
    def test_traced_with_these_tools(self):
        digest = s.tools_digest(HERE.parent.parent)
        lines = s.INPUTS_FILE.read_text(encoding="utf-8").splitlines()
        recorded = [line.split()[-1] for line in lines if line.startswith("# tools ")]
        self.assertEqual(
            recorded,
            [digest],
            "an HD tool or the scene map changed since hd_inputs.txt was traced: run setup_steps.py trace-inputs",
        )

    def test_lines(self):
        entries = [
            line for line in s.INPUTS_FILE.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#")
        ]
        self.assertTrue(entries)
        for line in entries:
            archive, _, path = line.partition(" ")
            self.assertIn(archive, ARCHIVES, line)
            self.assertTrue(path and not path.startswith("/"), line)
        # the glove's sheet newgame_dialog takes from KH2
        self.assertIn(f"kh2_first {s.HAND}", entries)
        self.assertIn("kh2_first msg/us/fontinfo.bar", entries)
        self.assertIn("kh2_first msg/us/fontimage.bar", entries)
        self.assertIn("kh2_first radar.tm2", entries)
        for region in ("us", "sp", "fr", "gr", "it"):
            self.assertIn(f"kh2_fifth field2d/{region}/zz0field.2dd", entries)

    def test_minimap_generator_wiring(self):
        self.assertIn(("minimap_icons.py", ["--settings", "{minimap_settings}", "--kh2", "{kh2}"]), s.HD_MENU_TOOLS)
        self.assertIn("minimap_icons.py", s.DATA_TOOLS)
        source = ast.parse((HERE.parent / "build_installer.py").read_text(encoding="utf-8"))
        tools = next(
            node.value
            for node in source.body
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "TOOLS" for t in node.targets)
        )
        self.assertIn("tools/hd/minimap_icons.py", ast.literal_eval(tools))

    def test_font_generator_wiring(self):
        self.assertIn(("kh2_fonts.py", ["{kh2}"]), s.HD_MENU_TOOLS)
        self.assertIn("kh2_fonts.py", s.DATA_TOOLS)
        self.assertEqual(s.tool_arguments(["{kh2}"], {"kh2": Path("extracted-kh2")}), ["extracted-kh2"])
        source = ast.parse((HERE.parent / "build_installer.py").read_text(encoding="utf-8"))
        tools = next(
            node.value
            for node in source.body
            if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "TOOLS" for t in node.targets)
        )
        self.assertIn("tools/hd/kh2_fonts.py", ast.literal_eval(tools))


if __name__ == "__main__":
    unittest.main()
