"""tools/hd/theater_title.py's cart languages on synthetic folders: a European cart's five, a North
American cart's English and French."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import theater_title as tt  # noqa: E402


def cart(root: Path, langs) -> Path:
    thr = root / "nitrofs" / "UI" / "thr"
    thr.mkdir(parents=True)
    for lang in langs:
        (thr / f"thr_{lang}.s.z").write_bytes(b"")
    return root


class CartLanguages(unittest.TestCase):
    def test_european_cart_keeps_all(self):
        with tempfile.TemporaryDirectory() as d:
            data = cart(Path(d), tt.LANGUAGES)
            self.assertEqual(tt.cart_languages(data, list(tt.LANGUAGES)), list(tt.LANGUAGES))

    def test_north_american_cart_keeps_english_and_french(self):
        with tempfile.TemporaryDirectory() as d:
            data = cart(Path(d), ("en", "fr"))
            self.assertEqual(tt.skip_missing_languages(data, list(tt.LANGUAGES)), ["en", "fr"])


if __name__ == "__main__":
    unittest.main()
