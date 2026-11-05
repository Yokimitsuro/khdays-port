"""The KH2 models in the HD pack: made from the collection's files alone, each setting's file provided."""

import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "installer"))
sys.path.insert(0, str(ROOT / "tools" / "hd"))
import build_installer as installer  # noqa: E402
import kh2_models as models  # noqa: E402
import setup_steps as setup  # noqa: E402

# the settings' lines that name a file beside them
NAMES_A_FILE = re.compile(r"^\s*(?:mset|motions|skeleton|include)\s+(\S+)|^\s*weapon_motions\s+(\S+)\s+(\S+)", re.M)


def collection(root: Path, without: str = "") -> tuple[Path, Path, Path]:
    """The two archives' extractions, each file its own name as its bytes."""
    first, fifth, sixth = root / "kh2_first.hed_out", root / "kh2_fifth.hed_out", root / "kh2_sixth.hed_out"
    places = {models.FIRST: first, models.FIFTH: fifth, models.SIXTH: sixth}
    for name, archive in models.SOURCES.items():
        if name == without:
            continue
        target = places[archive] / "original" / "obj" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(name.encode())
        if name.endswith(".mdlx"):
            pictures = places[archive] / "remastered" / "obj" / name
            pictures.mkdir(parents=True)
            for n in range(2):
                (pictures / f"-{n}.dds").write_bytes(f"{name} {n}".encode())
    return first, fifth, sixth


class Models(unittest.TestCase):
    def test_made_from_the_collection(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            first, fifth, sixth = collection(root)
            mod = root / "mod"
            models.make(first, fifth, sixth, mod)
            place = mod / "graphics/ba/ch/ro/def_hb.p.z/mdl"
            self.assertEqual((place / "roxas_b_h.mdlx").read_bytes(), b"N_EX830.mdlx")
            self.assertEqual((place / "roxas_b_h" / "-1.dds").read_bytes(), b"N_EX830.mdlx 1")
            self.assertEqual((place / "P_EX110.mset").read_bytes(), b"P_EX110.mset")
            self.assertEqual((mod / "graphics/ba/ch/ro/w_.p2#0/mdl/ro_w00600/-0.dds").read_bytes(), b"W_EX010.mdlx 0")
            self.assertEqual((mod / "graphics/ba/ch/r2/def_h.p.z/mdl/roxas_bf_h.mdlx").read_bytes(), b"B_EX390.mdlx")
            for source in (models.SETTINGS / "graphics").rglob("*"):
                if source.is_file():
                    target = mod / source.relative_to(models.SETTINGS)
                    self.assertEqual(target.read_bytes(), source.read_bytes())
            for folder, *_ in models.MODELS:
                self.assertEqual((mod / folder / models.PRESETS.name).read_bytes(), models.PRESETS.read_bytes())

    def test_a_missing_file_is_named(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            first, fifth, sixth = collection(root, without="GAMEOVER.mset")
            with self.assertRaises(SystemExit) as stop:
                models.make(first, fifth, sixth, root / "mod")
            self.assertIn("GAMEOVER.mset", str(stop.exception))

    def test_each_settings_file_is_provided(self):
        for folder, model, _, files in models.MODELS:
            place = models.SETTINGS / folder
            from_folder, from_model = models.SAME_SETTINGS.get((folder, model), (folder, model))
            self.assertTrue(
                (place / f"{model}.bones.txt").is_file()
                or (models.SETTINGS / from_folder / f"{from_model}.bones.txt").is_file(),
                model,
            )
            provided = set(files) | {f"{model}.mdlx"}
            for settings in place.glob("*.txt"):
                for line in NAMES_A_FILE.findall(settings.read_text(encoding="utf-8")):
                    for name in filter(None, line):
                        known = name in provided or name == models.PRESETS.name or (place / name).is_file()
                        self.assertTrue(known, f"{settings.name}: {name}")

    def test_same_settings(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            first, fifth, sixth = collection(root)
            mod = root / "mod"
            models.make(first, fifth, sixth, mod)
            for (folder, model), (from_folder, from_model) in models.SAME_SETTINGS.items():
                for source in (models.SETTINGS / from_folder).glob(f"{from_model}.*.txt"):
                    name = model + source.name[len(from_model) :]
                    own = models.SETTINGS / folder / name
                    expected = own if own.is_file() else source
                    self.assertEqual((mod / folder / name).read_bytes(), expected.read_bytes())
            # the dual wield's keyblades ride its weapon bones; a low poly body plays as its high poly one
            left = mod / "graphics/ba/ch/r2/w_.p2#0/mdl"
            self.assertIn("held kh2_2 rest", (left / "ro_w13600.bones.txt").read_text(encoding="utf-8"))
            self.assertEqual((left / "ro_w13600.mdlx").read_bytes(), b"W_EX010_10.mdlx")
            self.assertTrue((mod / "graphics/ba/ch/ro/def.p.z/mdl/roxas_b.motions.txt").is_file())
            self.assertFalse((mod / "graphics/ba/ch/ro/w_.p2#27/mdl/ro_w9999b.secondary.txt").exists())
            # the cutscene's: the hooded Roxas on B_EX390, its keyblades with chains, F_TT580 without
            self.assertEqual((mod / "graphics/mi/ch/47.z/mdl/e_roh_hb.mdlx").read_bytes(), b"B_EX390.mdlx")
            cutscene = mod / "graphics/mi/ob"
            self.assertEqual((cutscene / "B0.z/mdl/ro_w13600.mdlx").read_bytes(), b"W_EX010_10.mdlx")
            self.assertEqual((cutscene / "17.z/mdl/e_ro_w.mdlx").read_bytes(), b"W_EX010.mdlx")
            self.assertTrue((cutscene / "17.z/mdl/e_ro_w.secondary.txt").is_file())
            self.assertTrue((cutscene / "B1.z/mdl/ro_w14600.secondary.txt").is_file())
            self.assertEqual((cutscene / "67.z/mdl/ro_w9999b.mdlx").read_bytes(), b"F_TT580.mdlx")
            self.assertFalse((cutscene / "67.z/mdl/ro_w9999b.secondary.txt").exists())

    def test_every_source_is_used(self):
        used = {kh2 for _, _, kh2, _ in models.MODELS} | {n for *_, files in models.MODELS for n in files}
        self.assertEqual(used, set(models.SOURCES))

    def test_nothing_of_the_game_in_the_repository(self):
        files = [p for p in models.SETTINGS.rglob("*") if p.is_file()]
        self.assertTrue(files)
        for path in files:
            self.assertEqual(path.suffix, ".txt", path)
            self.assertTrue(path.read_bytes().isascii(), path)

    def test_each_model_follows_its_switch(self):
        # Settings, Enhancements, KH2 models (hud_settings.h kh2_models): every model of the pack, off together
        bones = sorted(models.SETTINGS.rglob("*.bones.txt"))
        self.assertTrue(bones)
        for path in bones:
            lines = [line.split("#")[0].split() for line in path.read_text(encoding="utf-8").splitlines()]
            self.assertIn(["switch", "kh2_models"], lines, path)

    def test_wiring(self):
        self.assertIn(("kh2_models.py", ["{kh2}", "{kh2_gauge}", "{kh2_sixth}"]), setup.HD_MENU_TOOLS)
        self.assertIn("tools/hd/kh2_models.py", installer.TOOLS)
        self.assertEqual(
            set(setup.model_settings()),
            {p.relative_to(ROOT).as_posix() for p in models.SETTINGS.rglob("*") if p.is_file()},
        )
        inputs = setup.hd_inputs()
        for name, archive in models.SOURCES.items():
            self.assertIn(f"obj/{name}", inputs[f"kh2_{archive}"])


if __name__ == "__main__":
    unittest.main()
