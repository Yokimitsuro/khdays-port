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


def settings_of(folder: str, model: str) -> dict[str, Path]:
    """A model's settings files as make() gives them: its own, then each taken along its SAME_SETTINGS chain."""
    found = {p.name: p for p in (models.SETTINGS / folder).glob(f"{model}.*.txt")}
    taken = models.SAME_SETTINGS.get((folder, model))
    while taken is not None:
        from_folder, from_model = taken
        for source in sorted((models.SETTINGS / from_folder).glob(f"{from_model}.*.txt")):
            found.setdefault(model + source.name[len(from_model) :], source)
        taken = models.SAME_SETTINGS.get(taken)
    return found


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
            settings = settings_of(folder, model)
            # its own bones.txt, or one through the models whose settings it takes (as make() follows them)
            self.assertIn(f"{model}.bones.txt", settings, model)
            provided = set(files) | {f"{model}.mdlx"}
            # every file its settings name, inherited ones too, is copied beside it
            for name_in_mod, source in settings.items():
                for line in NAMES_A_FILE.findall(source.read_text(encoding="utf-8")):
                    for name in filter(None, line):
                        known = name in provided or name == models.PRESETS.name or (place / name).is_file()
                        self.assertTrue(known, f"{folder}/{name_in_mod} (from {source}): {name}")

    def test_same_settings(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            first, fifth, sixth = collection(root)
            mod = root / "mod"
            models.make(first, fifth, sixth, mod)
            # each settings file the nearest model of its chain has, all the way along
            for folder, model in models.SAME_SETTINGS:
                for name, source in settings_of(folder, model).items():
                    self.assertEqual((mod / folder / name).read_bytes(), source.read_bytes(), f"{folder}/{name}")
            # the dual wield's keyblades ride its weapon bones; a low poly body plays as its high poly one
            left = mod / "graphics/ba/ch/r2/w_.p2#0/mdl"
            self.assertIn("held kh2_2 rest", (left / "ro_w13600.bones.txt").read_text(encoding="utf-8"))
            self.assertEqual((left / "ro_w13600.mdlx").read_bytes(), b"W_EX010_10.mdlx")
            self.assertTrue((mod / "graphics/ba/ch/ro/def.p.z/mdl/roxas_b.motions.txt").is_file())
            self.assertFalse((mod / "graphics/ba/ch/ro/w_.p2#27/mdl/ro_w9999b.secondary.txt").exists())
            # the cutscene's: the hooded Roxas on B_EX390, its keyblades with chains, F_TT580 without
            self.assertEqual((mod / "graphics/mi/ch/47.z/mdl/e_roh_hb.mdlx").read_bytes(), b"B_EX390.mdlx")
            axel = mod / "graphics/ba/ch/ax"
            self.assertEqual((axel / "def_h.p.z/mdl/axel_h.mdlx").read_bytes(), b"B_EX110.mdlx")
            self.assertEqual((axel / "def.p.z/mdl/axel.mdlx").read_bytes(), b"B_EX110.mdlx")
            self.assertTrue((axel / "def.p.z/mdl/axel.secondary.txt").is_file())
            marluxia = mod / "graphics/ba/ch/ma"
            self.assertEqual((marluxia / "def_h.p.z/mdl/marluxia_h.mdlx").read_bytes(), b"N_CM000_BTL.mdlx")
            self.assertEqual((marluxia / "def.p.z/mdl/marluxia.mdlx").read_bytes(), b"N_CM000_BTL.mdlx")
            self.assertEqual((marluxia / "def.p.z/mdl/N_CM000_BTL.mset").read_bytes(), b"N_CM000_BTL.mset")
            self.assertTrue((marluxia / "def.p.z/mdl/marluxia.bones.txt").is_file())
            larxene = mod / "graphics/ba/ch/la"
            self.assertEqual((larxene / "def_h.p.z/mdl/larxene_h.mdlx").read_bytes(), b"B_EX400.mdlx")
            self.assertEqual((larxene / "def.p.z/mdl/larxene.mdlx").read_bytes(), b"B_EX400.mdlx")
            self.assertTrue((larxene / "def.p.z/mdl/larxene.motions.txt").is_file())
            zexion = mod / "graphics/ba/ch/ze"
            self.assertEqual((zexion / "def_h.p.z/mdl/zexion_h.mdlx").read_bytes(), b"B_EX370.mdlx")
            self.assertEqual((zexion / "def.p.z/mdl/zexion.mdlx").read_bytes(), b"B_EX370.mdlx")
            self.assertTrue((zexion / "def.p.z/mdl/zexion.secondary.txt").is_file())
            vexen = mod / "graphics/ba/ch/ve"
            self.assertEqual((vexen / "def_h.p.z/mdl/vexen_h.mdlx").read_bytes(), b"N_CM040.mdlx")
            self.assertEqual((vexen / "def.p.z/mdl/N_CM040_BTL.mset").read_bytes(), b"N_CM040_BTL.mset")
            self.assertTrue((vexen / "def.p.z/mdl/vexen.bones.txt").is_file())
            lexaeus = mod / "graphics/ba/ch/le"
            self.assertEqual((lexaeus / "def_h.p.z/mdl/lexaeus_h.mdlx").read_bytes(), b"N_CM020_BTL.mdlx")
            self.assertEqual((lexaeus / "def.p.z/mdl/lexaeus.mdlx").read_bytes(), b"N_CM020_BTL.mdlx")
            self.assertTrue((lexaeus / "def.p.z/mdl/lexaeus.motions.txt").is_file())
            demyx = mod / "graphics/ba/ch/de"
            self.assertEqual((demyx / "def_h.p.z/mdl/demyx_h.mdlx").read_bytes(), b"B_EX120_HB.mdlx")
            self.assertEqual((demyx / "def.p.z/mdl/B_EX120_HB.mset").read_bytes(), b"B_EX120_HB.mset")
            self.assertTrue((demyx / "def.p.z/mdl/demyx.secondary.txt").is_file())
            xaldin = mod / "graphics/ba/ch/xa"
            self.assertEqual((xaldin / "def_h.p.z/mdl/xaldin_h.mdlx").read_bytes(), b"B_EX130.mdlx")
            self.assertEqual((xaldin / "def.p.z/mdl/xaldin.mdlx").read_bytes(), b"B_EX130.mdlx")
            self.assertTrue((xaldin / "def.p.z/mdl/xaldin.motions.txt").is_file())
            xigbar = mod / "graphics/ba/ch/xi"
            self.assertEqual((xigbar / "def_h.p.z/mdl/xigbar_h.mdlx").read_bytes(), b"B_EX140.mdlx")
            self.assertEqual((xigbar / "def.p.z/mdl/B_EX140.mset").read_bytes(), b"B_EX140.mset")
            self.assertTrue((xigbar / "def.p.z/mdl/xigbar.motions.txt").is_file())
            luxord = mod / "graphics/ba/ch/lu"
            self.assertEqual((luxord / "def_h.p.z/mdl/luxord_h.mdlx").read_bytes(), b"B_EX150.mdlx")
            self.assertEqual((luxord / "def.p.z/mdl/luxord.mdlx").read_bytes(), b"B_EX150.mdlx")
            self.assertTrue((luxord / "def.p.z/mdl/luxord.bones.txt").is_file())
            saix = mod / "graphics/ba/ch/sa"
            self.assertEqual((saix / "def_h.p.z/mdl/saix_h.mdlx").read_bytes(), b"B_EX160.mdlx")
            self.assertEqual((saix / "def.p.z/mdl/B_EX160.mset").read_bytes(), b"B_EX160.mset")
            self.assertTrue((saix / "def.p.z/mdl/saix.secondary.txt").is_file())
            xemnas = mod / "graphics/ba/ch/xe"
            self.assertEqual((xemnas / "def_h.p.z/mdl/xemnas_h.mdlx").read_bytes(), b"B_EX170.mdlx")
            self.assertEqual((xemnas / "def.p.z/mdl/B_EX170_LAST.mset").read_bytes(), b"B_EX170_LAST.mset")
            self.assertTrue((xemnas / "def.p.z/mdl/xemnas.motions.txt").is_file())
            # the boss Saix: his body with the player's cloth (through 016 for Mission Mode's A_016) and his claymores
            boss = mod / "graphics/Ms"
            self.assertEqual((boss / "53.p#0/mdl/016.mdlx").read_bytes(), b"B_EX160.mdlx")
            self.assertEqual((boss / "53.p#0/mdl/B_EX160_2.mset").read_bytes(), b"B_EX160_2.mset")
            self.assertTrue((boss / "53.p#0/mdl/016.secondary.txt").is_file())
            self.assertTrue((boss / "53.p#1/mdl/A_016.secondary.txt").is_file())
            self.assertTrue((boss / "53.p#1/mdl/A_016.motions.txt").is_file())
            self.assertEqual((boss / "53.p#62/mdl/W01.mdlx").read_bytes(), b"B_EX160.mdlx")
            self.assertTrue((boss / "53.p#63/mdl/A_W01.bones.txt").is_file())
            self.assertFalse((boss / "53.p#60/mdl/W00.secondary.txt").exists())
            # the boss Riku: riku_h's bones and cloth (through B13_01body for A_B13_01body), his own motions, and his
            # sword object as W_EX010_20 with ri_w01600's keychain
            self.assertEqual((boss / "54.p#0/mdl/B13_01body.mdlx").read_bytes(), b"N_EX950.mdlx")
            self.assertEqual((boss / "54.p#0/mdl/W_EH000.mset").read_bytes(), b"W_EH000.mset")
            self.assertTrue((boss / "54.p#0/mdl/B13_01body.bones.txt").is_file())
            self.assertTrue((boss / "54.p#1/mdl/A_B13_01body.secondary.txt").is_file())
            self.assertTrue((boss / "54.p#1/mdl/A_B13_01body.motions.txt").is_file())
            self.assertEqual((boss / "54.p#33/mdl/A_B13_02w.mdlx").read_bytes(), b"W_EX010_20.mdlx")
            self.assertTrue((boss / "54.p#32/mdl/B13_02w.secondary.txt").is_file())
            self.assertIn("held kh2_2 body", (boss / "54.p#33/mdl/A_B13_02w.bones.txt").read_text(encoding="utf-8"))
            # Riku: the coat Riku on the playable Riku's motions and weapon motions, the low poly model through riku_h
            riku = mod / "graphics/ba/ch/ri"
            self.assertEqual((riku / "def_h.p.z/mdl/riku_h.mdlx").read_bytes(), b"N_EX950.mdlx")
            self.assertEqual((riku / "def_h.p.z/mdl/P_EH000.mset").read_bytes(), b"P_EH000.mset")
            self.assertEqual((riku / "def.p.z/mdl/W_EH000.mdlx").read_bytes(), b"W_EH000.mdlx")
            self.assertTrue((riku / "def.p.z/mdl/riku.bones.txt").is_file())
            self.assertTrue((riku / "def.p.z/mdl/riku.secondary.txt").is_file())
            self.assertEqual((riku / "w_.p2#1/mdl/ri_w01600.mdlx").read_bytes(), b"W_EH000.mdlx")
            self.assertEqual((riku / "w_.p2#2/mdl/ri_w14600.mdlx").read_bytes(), b"W_EX010_20.mdlx")
            self.assertTrue((riku / "w_.p2#2/mdl/ri_w14600.secondary.txt").is_file())
            # Mickey: his own motions, both keyblades with the Kingdom Key's settings
            mickey = mod / "graphics/ba/ch/mi"
            self.assertEqual((mickey / "def.p.z/mdl/mickey.mdlx").read_bytes(), b"P_EX200.mdlx")
            self.assertEqual((mickey / "def.p.z/mdl/W_EX200.mset").read_bytes(), b"W_EX200.mset")
            self.assertEqual((mickey / "w_.p2#0/mdl/mi_w00600.mdlx").read_bytes(), b"W_EX200.mdlx")
            self.assertEqual((mickey / "w_.p2#1/mdl/mi_w01600.mdlx").read_bytes(), b"W_EX010_30.mdlx")
            self.assertTrue((mickey / "w_.p2#1/mdl/mi_w01600.secondary.txt").is_file())
            # Donald: his own motions, his three staves posed by W_EX020.mset, do_w02600 with do_w00600's settings
            donald = mod / "graphics/ba/ch/do"
            self.assertEqual((donald / "def.p.z/mdl/donald.mdlx").read_bytes(), b"P_EX020.mdlx")
            self.assertEqual((donald / "def.p.z/mdl/W_EX020.mset").read_bytes(), b"W_EX020.mset")
            self.assertTrue((donald / "def.p.z/mdl/donald.secondary.txt").is_file())
            self.assertEqual((donald / "w_.p2#0/mdl/do_w00600.mdlx").read_bytes(), b"W_EX020.mdlx")
            self.assertEqual((donald / "w_.p2#1/mdl/do_w01600.mdlx").read_bytes(), b"F_TT160.mdlx")
            self.assertEqual((donald / "w_.p2#2/mdl/do_w02600.mdlx").read_bytes(), b"W_EX020_B0.mdlx")
            self.assertTrue((donald / "w_.p2#2/mdl/do_w02600.bones.txt").is_file())
            # Goofy: his own motions, his three shields posed by W_EX030.mset, go_w02600 with go_w00600's settings
            goofy = mod / "graphics/ba/ch/go"
            self.assertEqual((goofy / "def.p.z/mdl/goofy.mdlx").read_bytes(), b"P_EX030.mdlx")
            self.assertEqual((goofy / "def.p.z/mdl/W_EX030.mset").read_bytes(), b"W_EX030.mset")
            self.assertTrue((goofy / "def.p.z/mdl/goofy.secondary.txt").is_file())
            self.assertEqual((goofy / "w_.p2#0/mdl/go_w00600.mdlx").read_bytes(), b"W_EX030.mdlx")
            self.assertEqual((goofy / "w_.p2#1/mdl/go_w01600.mdlx").read_bytes(), b"F_TT150.mdlx")
            self.assertEqual((goofy / "w_.p2#2/mdl/go_w02600.mdlx").read_bytes(), b"W_EX030_B0.mdlx")
            self.assertTrue((goofy / "w_.p2#2/mdl/go_w02600.bones.txt").is_file())
            # the cutscene Axels are not in the pack
            self.assertFalse((mod / "graphics/mi/ch/07.z").exists())
            self.assertFalse((mod / "graphics/mi/ch/50.z").exists())
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
