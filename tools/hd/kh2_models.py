"""KH2's models over Days' Roxas: the models, motions and HD pictures from the player's own copy of KINGDOM
HEARTS HD 1.5+2.5 ReMIX, with the port's settings for them (bone mappings, motions, cloth) from kh2_models/
beside this tool. Nothing of KH2 is in the repository; each file is copied as the collection holds it.
docs/KH2_MODELS.md, "In the HD pack".

    python tools/hd/kh2_models.py <kh2_first.hed_out> <kh2_fifth.hed_out> <kh2_sixth.hed_out> [--mod "HD PACK"]
"""

import argparse
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SETTINGS = HERE / "kh2_models"  # the mod's tree: only the port's own text files
PRESETS = SETTINGS / "kh2_secondary_presets.txt"  # one here, beside each model in the mod (`include` reads it there)

FIRST, FIFTH, SIXTH = "first", "fifth", "sixth"
# Which archive holds each file, under obj/. Verified on the Steam copy: each the same, byte for byte, as the
# files the settings were tuned with.
SOURCES = {
    "B_EX110.mdlx": FIFTH,
    "B_EX110.mset": FIFTH,
    "B_EX120_HB.mdlx": FIFTH,
    "B_EX120_HB.mset": FIFTH,
    "B_EX130.mdlx": FIFTH,
    "B_EX130.mset": FIFTH,
    "B_EX140.mdlx": FIFTH,
    "B_EX140.mset": FIFTH,
    "B_EX150.mdlx": FIFTH,
    "B_EX150.mset": FIFTH,
    "B_EX160.mdlx": FIFTH,
    "B_EX160.mset": FIFTH,
    "B_EX160_2.mset": FIFTH,
    "B_EX170.mdlx": FIFTH,
    "B_EX170.mset": FIFTH,
    "B_EX170_LAST.mset": FIFTH,
    "B_EX390.mdlx": FIFTH,
    "B_EX370.mdlx": FIFTH,
    "B_EX370.mset": FIFTH,
    "B_EX390.mset": FIFTH,
    "B_EX400.mdlx": FIFTH,
    "B_EX400.mset": FIFTH,
    "F_TT150.mdlx": FIFTH,
    "F_TT160.mdlx": FIFTH,
    "F_TT580.mdlx": FIRST,
    "GAMEOVER.mset": FIFTH,
    "N_CM000_BTL.mdlx": FIFTH,
    "N_CM000_BTL.mset": FIFTH,
    "N_CM020_BTL.mdlx": FIFTH,
    "N_CM020_BTL.mset": FIFTH,
    "N_CM040.mdlx": FIFTH,
    "N_CM040_BTL.mset": FIFTH,
    "N_EX830.mdlx": FIFTH,
    "N_EX950.mdlx": FIFTH,
    "P_EH000.mdlx": SIXTH,
    "P_EH000.mset": SIXTH,
    "P_EX020.mdlx": SIXTH,
    "P_EX020.mset": SIXTH,
    "P_EX030.mdlx": SIXTH,
    "P_EX030.mset": SIXTH,
    "P_EX200.mdlx": SIXTH,
    "P_EX200.mset": SIXTH,
    "P_EX110.mdlx": FIRST,
    "P_EX110.mset": FIRST,
    "W_EH000.mdlx": SIXTH,
    "W_EH000.mset": SIXTH,
    "W_EX010.mdlx": FIRST,
    "W_EX010_10.mdlx": SIXTH,
    "W_EX010_20.mdlx": SIXTH,
    "W_EX010_30.mdlx": SIXTH,
    "W_EX010_P0.mdlx": SIXTH,
    "W_EX010_RX.mset": FIRST,
    "W_EX010_W0.mdlx": FIRST,
    "W_EX020.mdlx": SIXTH,
    "W_EX020.mset": SIXTH,
    "W_EX020_B0.mdlx": SIXTH,
    "W_EX030.mdlx": SIXTH,
    "W_EX030.mset": SIXTH,
    "W_EX030_B0.mdlx": SIXTH,
    "W_EX200.mdlx": SIXTH,
    "W_EX200.mset": SIXTH,
}
# Each Days model replaced: its place in the mod, its name there, the KH2 model it is (its HD pictures in a folder
# of that name beside it, where import_mdlx reads them) and the other files its motions.txt names.
PLAYER_FILES = ("P_EX110.mset", "P_EX110.mdlx", "GAMEOVER.mset", "W_EX010.mdlx", "W_EX010_RX.mset")
DUAL_FILES = ("B_EX390.mset", "P_EX110.mset", "P_EX110.mdlx", "GAMEOVER.mset")
RIKU_FILES = ("P_EH000.mset", "P_EH000.mdlx", "W_EH000.mset", "W_EH000.mdlx")
MICKEY_FILES = ("P_EX200.mset", "W_EX200.mset", "W_EX200.mdlx")
DONALD_FILES = ("P_EX020.mset", "W_EX020.mset", "W_EX020.mdlx")
GOOFY_FILES = ("P_EX030.mset", "W_EX030.mset", "W_EX030.mdlx")
MODELS = (
    ("graphics/ba/ch/ro/def_hb.p.z/mdl", "roxas_b_h", "N_EX830.mdlx", PLAYER_FILES),
    ("graphics/ba/ch/ro/def.p.z/mdl", "roxas_b", "N_EX830.mdlx", PLAYER_FILES),
    ("graphics/ba/ch/ro/def_hho.p.z/mdl", "roxas_ho_h", "B_EX390.mdlx", PLAYER_FILES),
    ("graphics/ba/ch/r2/def_h.p.z/mdl", "roxas_bf_h", "B_EX390.mdlx", DUAL_FILES),
    ("graphics/ba/ch/r2/def.p.z/mdl", "roxas_bf", "B_EX390.mdlx", DUAL_FILES),
    ("graphics/mi/ch/0C.z/mdl", "e_rox_hb", "N_EX830.mdlx", ("P_EX110.mset", "P_EX110.mdlx")),
    ("graphics/mi/ch/47.z/mdl", "e_roh_hb", "B_EX390.mdlx", PLAYER_FILES),
    ("graphics/ba/ch/ax/def_h.p.z/mdl", "axel_h", "B_EX110.mdlx", ("B_EX110.mset",)),
    ("graphics/ba/ch/ax/def.p.z/mdl", "axel", "B_EX110.mdlx", ("B_EX110.mset",)),
    ("graphics/ba/ch/ma/def_h.p.z/mdl", "marluxia_h", "N_CM000_BTL.mdlx", ("N_CM000_BTL.mset",)),
    ("graphics/ba/ch/ma/def.p.z/mdl", "marluxia", "N_CM000_BTL.mdlx", ("N_CM000_BTL.mset",)),
    ("graphics/ba/ch/la/def_h.p.z/mdl", "larxene_h", "B_EX400.mdlx", ("B_EX400.mset",)),
    ("graphics/ba/ch/la/def.p.z/mdl", "larxene", "B_EX400.mdlx", ("B_EX400.mset",)),
    ("graphics/ba/ch/ze/def_h.p.z/mdl", "zexion_h", "B_EX370.mdlx", ("B_EX370.mset",)),
    ("graphics/ba/ch/ze/def.p.z/mdl", "zexion", "B_EX370.mdlx", ("B_EX370.mset",)),
    ("graphics/ba/ch/ve/def_h.p.z/mdl", "vexen_h", "N_CM040.mdlx", ("N_CM040_BTL.mset",)),
    ("graphics/ba/ch/ve/def.p.z/mdl", "vexen", "N_CM040.mdlx", ("N_CM040_BTL.mset",)),
    ("graphics/ba/ch/le/def_h.p.z/mdl", "lexaeus_h", "N_CM020_BTL.mdlx", ("N_CM020_BTL.mset",)),
    ("graphics/ba/ch/le/def.p.z/mdl", "lexaeus", "N_CM020_BTL.mdlx", ("N_CM020_BTL.mset",)),
    ("graphics/ba/ch/de/def_h.p.z/mdl", "demyx_h", "B_EX120_HB.mdlx", ("B_EX120_HB.mset",)),
    ("graphics/ba/ch/de/def.p.z/mdl", "demyx", "B_EX120_HB.mdlx", ("B_EX120_HB.mset",)),
    ("graphics/ba/ch/xa/def_h.p.z/mdl", "xaldin_h", "B_EX130.mdlx", ("B_EX130.mset",)),
    ("graphics/ba/ch/xa/def.p.z/mdl", "xaldin", "B_EX130.mdlx", ("B_EX130.mset",)),
    ("graphics/ba/ch/xi/def_h.p.z/mdl", "xigbar_h", "B_EX140.mdlx", ("B_EX140.mset",)),
    ("graphics/ba/ch/xi/def.p.z/mdl", "xigbar", "B_EX140.mdlx", ("B_EX140.mset",)),
    ("graphics/ba/ch/lu/def_h.p.z/mdl", "luxord_h", "B_EX150.mdlx", ("B_EX150.mset",)),
    ("graphics/ba/ch/lu/def.p.z/mdl", "luxord", "B_EX150.mdlx", ("B_EX150.mset",)),
    ("graphics/ba/ch/sa/def_h.p.z/mdl", "saix_h", "B_EX160.mdlx", ("B_EX160.mset",)),
    ("graphics/ba/ch/sa/def.p.z/mdl", "saix", "B_EX160.mdlx", ("B_EX160.mset",)),
    ("graphics/ba/ch/xe/def_h.p.z/mdl", "xemnas_h", "B_EX170.mdlx", ("B_EX170.mset", "B_EX170_LAST.mset")),
    ("graphics/ba/ch/xe/def.p.z/mdl", "xemnas", "B_EX170.mdlx", ("B_EX170.mset", "B_EX170_LAST.mset")),
    # Riku (Mission Mode): the coat Riku with the blindfold on the playable Riku's motions, as roxas_b_h
    ("graphics/ba/ch/ri/def_h.p.z/mdl", "riku_h", "N_EX950.mdlx", RIKU_FILES),
    ("graphics/ba/ch/ri/def.p.z/mdl", "riku", "N_EX950.mdlx", RIKU_FILES),
    # Mickey (Mission Mode; one model, no def_h): KH2's Mickey on his own motions
    ("graphics/ba/ch/mi/def.p.z/mdl", "mickey", "P_EX200.mdlx", MICKEY_FILES),
    # Donald (Mission Mode; one model, no def_h): KH2's Donald on his own motions
    ("graphics/ba/ch/do/def.p.z/mdl", "donald", "P_EX020.mdlx", DONALD_FILES),
    # Goofy (Mission Mode; one model, no def_h): KH2's Goofy on his own motions
    ("graphics/ba/ch/go/def.p.z/mdl", "goofy", "P_EX030.mdlx", GOOFY_FILES),
    # the boss Saix (Ms/53.p): his body, 016 (Mission Mode's A_016), and his claymore object's two models, W00 and the
    # berserk W01 (A_W00, A_W01), each drawn as B_EX160's own claymore meshes
    ("graphics/Ms/53.p#0/mdl", "016", "B_EX160.mdlx", ("B_EX160.mset", "B_EX160_2.mset")),
    ("graphics/Ms/53.p#1/mdl", "A_016", "B_EX160.mdlx", ("B_EX160.mset", "B_EX160_2.mset")),
    ("graphics/Ms/53.p#60/mdl", "W00", "B_EX160.mdlx", ()),
    ("graphics/Ms/53.p#61/mdl", "A_W00", "B_EX160.mdlx", ()),
    ("graphics/Ms/53.p#62/mdl", "W01", "B_EX160.mdlx", ()),
    ("graphics/Ms/53.p#63/mdl", "A_W01", "B_EX160.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#0/mdl", "ro_w00600", "W_EX010.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#21/mdl", "ro_w13600", "W_EX010_10.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#22/mdl", "ro_w15600", "W_EX010_W0.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#26/mdl", "ro_w9999a", "W_EX010_P0.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#27/mdl", "ro_w9999b", "F_TT580.mdlx", ()),
    ("graphics/ba/ch/r2/w_.p2#0/mdl", "ro_w13600", "W_EX010_10.mdlx", ()),
    # Riku's keyblades: Way to the Dawn, and W_EX010_20 for his ri_w14600, both posed by his W_EH000.mset
    ("graphics/ba/ch/ri/w_.p2#1/mdl", "ri_w01600", "W_EH000.mdlx", ()),
    ("graphics/ba/ch/ri/w_.p2#2/mdl", "ri_w14600", "W_EX010_20.mdlx", ()),
    # Mickey's keyblades, both posed by his W_EX200.mset
    ("graphics/ba/ch/mi/w_.p2#0/mdl", "mi_w00600", "W_EX200.mdlx", ()),
    ("graphics/ba/ch/mi/w_.p2#1/mdl", "mi_w01600", "W_EX010_30.mdlx", ()),
    # Donald's staves, all posed by his W_EX020.mset
    ("graphics/ba/ch/do/w_.p2#0/mdl", "do_w00600", "W_EX020.mdlx", ()),
    ("graphics/ba/ch/do/w_.p2#1/mdl", "do_w01600", "F_TT160.mdlx", ()),
    ("graphics/ba/ch/do/w_.p2#2/mdl", "do_w02600", "W_EX020_B0.mdlx", ()),
    # Goofy's shields, all posed by his W_EX030.mset
    ("graphics/ba/ch/go/w_.p2#0/mdl", "go_w00600", "W_EX030.mdlx", ()),
    ("graphics/ba/ch/go/w_.p2#1/mdl", "go_w01600", "F_TT150.mdlx", ()),
    ("graphics/ba/ch/go/w_.p2#2/mdl", "go_w02600", "W_EX030_B0.mdlx", ()),
    ("graphics/ba/ch/r2/w_.p2#80/mdl", "ro_w14600", "W_EX010_20.mdlx", ()),
    ("graphics/mi/ob/17.z/mdl", "e_ro_w", "W_EX010.mdlx", ()),
    ("graphics/mi/ob/67.z/mdl", "ro_w9999b", "F_TT580.mdlx", ()),
    ("graphics/mi/ob/B7.z/mdl", "ro_w00600", "W_EX010.mdlx", ()),
    ("graphics/mi/ob/B0.z/mdl", "ro_w13600", "W_EX010_10.mdlx", ()),
    ("graphics/mi/ob/B1.z/mdl", "ro_w14600", "W_EX010_20.mdlx", ()),
)
# A model's settings kept under another's, each file named for it: what it has of its own in kh2_models/ stays, and
# what the other has from a third (its own entry here) comes along.
KEYBLADE = ("graphics/ba/ch/ro/w_.p2#0/mdl", "ro_w00600")
SAME_SETTINGS = {
    ("graphics/ba/ch/ro/def.p.z/mdl", "roxas_b"): ("graphics/ba/ch/ro/def_hb.p.z/mdl", "roxas_b_h"),
    ("graphics/ba/ch/r2/def.p.z/mdl", "roxas_bf"): ("graphics/ba/ch/r2/def_h.p.z/mdl", "roxas_bf_h"),
    ("graphics/mi/ch/47.z/mdl", "e_roh_hb"): ("graphics/ba/ch/ro/def_hho.p.z/mdl", "roxas_ho_h"),
    ("graphics/ba/ch/ax/def.p.z/mdl", "axel"): ("graphics/ba/ch/ax/def_h.p.z/mdl", "axel_h"),
    ("graphics/ba/ch/ma/def.p.z/mdl", "marluxia"): ("graphics/ba/ch/ma/def_h.p.z/mdl", "marluxia_h"),
    ("graphics/ba/ch/la/def.p.z/mdl", "larxene"): ("graphics/ba/ch/la/def_h.p.z/mdl", "larxene_h"),
    ("graphics/ba/ch/ze/def.p.z/mdl", "zexion"): ("graphics/ba/ch/ze/def_h.p.z/mdl", "zexion_h"),
    ("graphics/ba/ch/ve/def.p.z/mdl", "vexen"): ("graphics/ba/ch/ve/def_h.p.z/mdl", "vexen_h"),
    ("graphics/ba/ch/le/def.p.z/mdl", "lexaeus"): ("graphics/ba/ch/le/def_h.p.z/mdl", "lexaeus_h"),
    ("graphics/ba/ch/de/def.p.z/mdl", "demyx"): ("graphics/ba/ch/de/def_h.p.z/mdl", "demyx_h"),
    ("graphics/ba/ch/xa/def.p.z/mdl", "xaldin"): ("graphics/ba/ch/xa/def_h.p.z/mdl", "xaldin_h"),
    ("graphics/ba/ch/xi/def.p.z/mdl", "xigbar"): ("graphics/ba/ch/xi/def_h.p.z/mdl", "xigbar_h"),
    ("graphics/ba/ch/lu/def.p.z/mdl", "luxord"): ("graphics/ba/ch/lu/def_h.p.z/mdl", "luxord_h"),
    ("graphics/ba/ch/sa/def.p.z/mdl", "saix"): ("graphics/ba/ch/sa/def_h.p.z/mdl", "saix_h"),
    ("graphics/ba/ch/xe/def.p.z/mdl", "xemnas"): ("graphics/ba/ch/xe/def_h.p.z/mdl", "xemnas_h"),
    ("graphics/ba/ch/ri/def.p.z/mdl", "riku"): ("graphics/ba/ch/ri/def_h.p.z/mdl", "riku_h"),
    ("graphics/Ms/53.p#0/mdl", "016"): ("graphics/ba/ch/sa/def_h.p.z/mdl", "saix_h"),
    ("graphics/Ms/53.p#1/mdl", "A_016"): ("graphics/Ms/53.p#0/mdl", "016"),
    ("graphics/Ms/53.p#61/mdl", "A_W00"): ("graphics/Ms/53.p#60/mdl", "W00"),
    ("graphics/Ms/53.p#63/mdl", "A_W01"): ("graphics/Ms/53.p#62/mdl", "W01"),
    ("graphics/ba/ch/ro/w_.p2#21/mdl", "ro_w13600"): KEYBLADE,
    ("graphics/ba/ch/ro/w_.p2#26/mdl", "ro_w9999a"): KEYBLADE,
    ("graphics/ba/ch/r2/w_.p2#0/mdl", "ro_w13600"): KEYBLADE,
    ("graphics/ba/ch/ri/w_.p2#2/mdl", "ri_w14600"): ("graphics/ba/ch/ri/w_.p2#1/mdl", "ri_w01600"),
    ("graphics/ba/ch/mi/w_.p2#0/mdl", "mi_w00600"): KEYBLADE,
    ("graphics/ba/ch/mi/w_.p2#1/mdl", "mi_w01600"): KEYBLADE,
    ("graphics/ba/ch/do/w_.p2#2/mdl", "do_w02600"): ("graphics/ba/ch/do/w_.p2#0/mdl", "do_w00600"),
    ("graphics/ba/ch/go/w_.p2#2/mdl", "go_w02600"): ("graphics/ba/ch/go/w_.p2#0/mdl", "go_w00600"),
    ("graphics/ba/ch/r2/w_.p2#80/mdl", "ro_w14600"): KEYBLADE,
    ("graphics/mi/ob/17.z/mdl", "e_ro_w"): KEYBLADE,
    ("graphics/mi/ob/B7.z/mdl", "ro_w00600"): KEYBLADE,
    ("graphics/mi/ob/B0.z/mdl", "ro_w13600"): KEYBLADE,
    ("graphics/mi/ob/B1.z/mdl", "ro_w14600"): KEYBLADE,
}


def put(source: Path, target: Path) -> None:
    if not source.is_file():
        sys.exit(f"kh2_models: {source} is missing: extract it from your KINGDOM HEARTS HD 1.5+2.5 ReMIX")
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


def make(first: Path, fifth: Path, sixth: Path, mod: Path) -> None:
    archives = {FIRST: first, FIFTH: fifth, SIXTH: sixth}

    def original(name: str) -> Path:
        return archives[SOURCES[name]] / "original" / "obj" / name

    for folder, model, kh2_model, files in MODELS:
        place = mod / folder
        put(original(kh2_model), place / f"{model}.mdlx")
        for name in files:
            put(original(name), place / name)
        pictures = archives[SOURCES[kh2_model]] / "remastered" / "obj" / kh2_model
        found = sorted(pictures.glob("-*.dds")) if pictures.is_dir() else []
        if not found:
            sys.exit(f"kh2_models: {pictures} has no HD pictures: extract them from your 1.5+2.5 ReMIX")
        for picture in found:
            put(picture, place / model / picture.name)
        put(PRESETS, place / PRESETS.name)
    for source in sorted((SETTINGS / "graphics").rglob("*")):
        if source.is_file():
            put(source, mod / source.relative_to(SETTINGS))
    for (folder, model), taken in SAME_SETTINGS.items():
        have = {p.name for p in (SETTINGS / folder).glob(f"{model}.*.txt")}
        while taken is not None:
            from_folder, from_model = taken
            for source in sorted((SETTINGS / from_folder).glob(f"{from_model}.*.txt")):
                name = model + source.name[len(from_model) :]
                if name not in have:
                    have.add(name)
                    put(source, mod / folder / name)
            taken = SAME_SETTINGS.get(taken)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("first", type=Path, help="kh2_first.hed_out (its original/ and remastered/)")
    p.add_argument("fifth", type=Path, help="kh2_fifth.hed_out")
    p.add_argument("sixth", type=Path, help="kh2_sixth.hed_out")
    p.add_argument("--mod", default="HD PACK")
    a = p.parse_args()
    make(a.first, a.fifth, a.sixth, HERE.parents[1] / "mods" / a.mod)


if __name__ == "__main__":
    main()
