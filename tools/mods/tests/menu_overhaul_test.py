"""tools/mods/menu_overhaul.py's pieces and helpers, with no game data and no collection: every piece a module of
overhaul/ with make(inputs, mod); the inputs the command line fills; art/ only pictures; raqm_text.py's advances
(HarfBuzz's, unhinted, in 26.6) and its hidden characters."""

import importlib
import inspect
import sys
import unittest
from pathlib import Path

from PIL import Image

MODS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MODS))
sys.path.insert(0, str(MODS / "overhaul"))
import menu_overhaul  # noqa: E402
import raqm_text  # noqa: E402
from common import ART, Inputs  # noqa: E402


class Pieces(unittest.TestCase):
    def test_each_piece_makes(self):
        for name in menu_overhaul.PIECES:
            make = getattr(importlib.import_module(name), "make", None)
            self.assertTrue(callable(make), name)
            self.assertEqual(list(inspect.signature(make).parameters), ["inputs", "mod"], name)

    def test_order(self):
        # a later piece reads an earlier one's files (docs in menu_overhaul.py)
        order = menu_overhaul.PIECES
        for first, then in (("files", "campstory"), ("files", "campsave"), ("camp", "campsave"), ("pause", "kh2pause")):
            self.assertLess(order.index(first), order.index(then), f"{first} before {then}")

    def test_inputs_are_the_command_line(self):
        source = inspect.getsource(menu_overhaul.main)
        for field in Inputs.__dataclass_fields__:
            self.assertIn(f'"--{field}"', source, field)
        self.assertNotIn("--ddd", source)  # 2.8 is the HD menus' alone


class Art(unittest.TestCase):
    def test_only_pictures(self):
        files = [p for p in ART.rglob("*") if p.is_file()]
        self.assertTrue(files)
        for path in files:
            with Image.open(path) as im:
                im.verify()


class FakeTables:
    """A font's tables as raqm_text reads them: 1000 units an em, A 500 units wide, B 613."""

    upem = 1000
    cmap = {ord("A"): 1, ord("B"): 2}
    marks: set = set()
    kern: list = []

    def width(self, glyph: int) -> int:
        return (0, 500, 613)[glyph]


def shaped(size: int) -> raqm_text.ShapedFont:
    font = raqm_text.ShapedFont.__new__(raqm_text.ShapedFont)
    font.sfnt = FakeTables()
    font.ft_scale = (size * 64 * 65536 + 500) // 1000
    return font


class Advances(unittest.TestCase):
    def test_unhinted_widths(self):
        self.assertEqual(shaped(64).advance("A"), 32.0)  # 500 units of 1000 at 64 pixels
        self.assertEqual(shaped(64).advance("B"), 39.234375)  # 39.232 to the nearest 64th
        self.assertEqual(shaped(33).advance("B"), 20.234375)  # 20.229 to the nearest 64th, from 16.16

    def test_hidden(self):
        self.assertEqual(shaped(64).advance("­"), 0.0)  # the soft hyphen: no room
        laid, end = shaped(64).layout("A­A")
        self.assertEqual([shown for _, shown, _, _ in laid], [True, False, True])
        self.assertEqual(end, 2 * 32 * 64)


if __name__ == "__main__":
    unittest.main()
