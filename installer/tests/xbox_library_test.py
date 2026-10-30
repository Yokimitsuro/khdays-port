"""The Xbox app's collections (xbox_library.py) on synthetic drives: a library named by a drive's .GamingRoot and
the default XboxGames one, a collection told by its archive in a game's Content, the 1.5+2.5's archives and videos
found in its layout (MSSTORE), and find-collections taking the Xbox app's copy when neither Steam nor the Epic Games
Store has one. No real store is read (the collections' own files are never in the repository)."""

import argparse
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import epic_library  # noqa: E402
import setup_steps as s  # noqa: E402
import steam_library  # noqa: E402
import xbox_library as xl  # noqa: E402


def touch(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")


def gaming_root(*names: str) -> bytes:
    return b"RGBX" + len(names).to_bytes(4, "little") + b"".join((n + "\0").encode("utf-16-le") for n in names)


class XboxLibrary(unittest.TestCase):
    def test_gaming_root(self):
        self.assertEqual(xl.gaming_root_names(gaming_root("Games")), ["Games"])
        self.assertEqual(xl.gaming_root_names(gaming_root("A", "B\\C")), ["A", "B\\C"])
        self.assertEqual(xl.gaming_root_names(b"RGBX\x00\x00\x00\x00" + "X\0".encode("utf-16-le")), ["X"])
        self.assertEqual(xl.gaming_root_names(b"none"), [])

    def test_libraries_and_collections(self):
        with tempfile.TemporaryDirectory() as t:
            c, d = Path(t) / "c", Path(t) / "d"
            kh = c / "XboxGames" / "KINGDOM HEARTS -HD 1.5+2.5 ReMIX" / "Content"
            dl = d / "Games" / "KINGDOM HEARTS HD 2.8" / "Content"
            touch(kh / "Image" / "dt" / "kh2_first.hed")
            touch(dl / "Image" / "dt" / "kh3d_first.hed")
            touch(c / "XboxGames" / "Another" / "Content" / "Image" / "dt" / "other.hed")
            (d / xl.GAMING_ROOT).write_bytes(gaming_root("Games"))
            self.assertEqual(xl.library_folders([c, d, Path(t) / "e"]), [c / "XboxGames", d / "Games"])
            libraries = xl.library_folders([c, d])
            self.assertEqual(xl.collection_folder(epic_library.MARKER_1525, libraries), kh)
            self.assertEqual(xl.collection_folder(epic_library.MARKER_28, libraries), dl)
            self.assertIsNone(xl.collection_folder("kh9_first", libraries))

    def test_layout(self):
        movies = s.hd_inputs().get("movies", [])
        self.assertTrue(movies)
        with tempfile.TemporaryDirectory() as t:
            kh = Path(t) / "Content"
            touch(kh / "Image" / "dt" / "kh2_first.hed")
            touch(kh / "MSSTORE" / "Mare" / "MOVIE" / "Days" / "dt" / movies[0])
            self.assertEqual(s.archive_base(kh, "1.5+2.5", "kh2_first"), kh / "Image" / "dt" / "kh2_first")
            self.assertEqual(s.movies_folder(kh), kh / "MSSTORE" / "Mare" / "MOVIE" / "Days" / "dt")


class FindCollections(unittest.TestCase):
    def setUp(self):
        self.saved = (steam_library.steam_folder, epic_library.collection_folder, xl.drives, s.check_folder)
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        steam_library.steam_folder = lambda: None
        epic_library.collection_folder = lambda marker, *a: None
        xl.drives = lambda: [self.root]
        s.check_folder = lambda kind, folder: []

    def tearDown(self):
        steam_library.steam_folder, epic_library.collection_folder, xl.drives, s.check_folder = self.saved
        self.temp.cleanup()

    def test_the_xbox_apps_copy(self):
        kh = self.root / "XboxGames" / "KH" / "Content"
        touch(kh / "Image" / "dt" / "kh2_first.hed")
        found = s.find_collections(argparse.Namespace())
        self.assertEqual(found["folder_1525"], str(kh))
        self.assertIsNone(found["folder_28"])

    def test_a_whole_copy_before_a_partial_one(self):
        epic = self.root / "Epic" / "KH_1.5_2.5"
        xbox = self.root / "XboxGames" / "KH" / "Content"
        for folder in (epic, xbox):
            touch(folder / "Image" / "dt" / "kh2_first.hed")
        epic_library.collection_folder = lambda marker, *a: epic if marker == epic_library.MARKER_1525 else None
        s.check_folder = lambda kind, folder: ["Image/Mare.hed/.pkg"] if folder == epic else []
        self.assertEqual(s.find_collections(argparse.Namespace())["folder_1525"], str(xbox))
        s.check_folder = lambda kind, folder: ["Image/Mare.hed/.pkg"]
        found = s.find_collections(argparse.Namespace())["collections"][0]
        self.assertEqual((found["folder"], found["valid"]), (str(epic), False))


if __name__ == "__main__":
    unittest.main()
