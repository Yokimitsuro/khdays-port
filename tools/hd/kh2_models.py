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
    "B_EX390.mdlx": FIFTH,
    "B_EX390.mset": FIFTH,
    "F_TT580.mdlx": FIRST,
    "GAMEOVER.mset": FIFTH,
    "N_EX830.mdlx": FIFTH,
    "P_EX110.mdlx": FIRST,
    "P_EX110.mset": FIRST,
    "W_EX010.mdlx": FIRST,
    "W_EX010_10.mdlx": SIXTH,
    "W_EX010_20.mdlx": SIXTH,
    "W_EX010_P0.mdlx": SIXTH,
    "W_EX010_RX.mset": FIRST,
    "W_EX010_W0.mdlx": FIRST,
}
# Each Days model replaced: its place in the mod, its name there, the KH2 model it is (its HD pictures in a folder
# of that name beside it, where import_mdlx reads them) and the other files its motions.txt names.
PLAYER_FILES = ("P_EX110.mset", "P_EX110.mdlx", "GAMEOVER.mset", "W_EX010.mdlx", "W_EX010_RX.mset")
DUAL_FILES = ("B_EX390.mset", "P_EX110.mset", "P_EX110.mdlx", "GAMEOVER.mset")
MODELS = (
    ("graphics/ba/ch/ro/def_hb.p.z/mdl", "roxas_b_h", "N_EX830.mdlx", PLAYER_FILES),
    ("graphics/ba/ch/ro/def.p.z/mdl", "roxas_b", "N_EX830.mdlx", PLAYER_FILES),
    ("graphics/ba/ch/ro/def_hho.p.z/mdl", "roxas_ho_h", "B_EX390.mdlx", PLAYER_FILES),
    ("graphics/ba/ch/r2/def_h.p.z/mdl", "roxas_bf_h", "B_EX390.mdlx", DUAL_FILES),
    ("graphics/ba/ch/r2/def.p.z/mdl", "roxas_bf", "B_EX390.mdlx", DUAL_FILES),
    ("graphics/mi/ch/0C.z/mdl", "e_rox_hb", "N_EX830.mdlx", ("P_EX110.mset", "P_EX110.mdlx")),
    ("graphics/mi/ch/47.z/mdl", "e_roh_hb", "B_EX390.mdlx", PLAYER_FILES),
    ("graphics/ba/ch/ro/w_.p2#0/mdl", "ro_w00600", "W_EX010.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#21/mdl", "ro_w13600", "W_EX010_10.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#22/mdl", "ro_w15600", "W_EX010_W0.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#26/mdl", "ro_w9999a", "W_EX010_P0.mdlx", ()),
    ("graphics/ba/ch/ro/w_.p2#27/mdl", "ro_w9999b", "F_TT580.mdlx", ()),
    ("graphics/ba/ch/r2/w_.p2#0/mdl", "ro_w13600", "W_EX010_10.mdlx", ()),
    ("graphics/ba/ch/r2/w_.p2#80/mdl", "ro_w14600", "W_EX010_20.mdlx", ()),
    ("graphics/mi/ob/17.z/mdl", "e_ro_w", "W_EX010.mdlx", ()),
    ("graphics/mi/ob/67.z/mdl", "ro_w9999b", "F_TT580.mdlx", ()),
    ("graphics/mi/ob/B7.z/mdl", "ro_w00600", "W_EX010.mdlx", ()),
    ("graphics/mi/ob/B0.z/mdl", "ro_w13600", "W_EX010_10.mdlx", ()),
    ("graphics/mi/ob/B1.z/mdl", "ro_w14600", "W_EX010_20.mdlx", ()),
)
# A model's settings kept under another's, each file named for it: what it has of its own in kh2_models/ stays.
KEYBLADE = ("graphics/ba/ch/ro/w_.p2#0/mdl", "ro_w00600")
SAME_SETTINGS = {
    ("graphics/ba/ch/ro/def.p.z/mdl", "roxas_b"): ("graphics/ba/ch/ro/def_hb.p.z/mdl", "roxas_b_h"),
    ("graphics/ba/ch/r2/def.p.z/mdl", "roxas_bf"): ("graphics/ba/ch/r2/def_h.p.z/mdl", "roxas_bf_h"),
    ("graphics/mi/ch/47.z/mdl", "e_roh_hb"): ("graphics/ba/ch/ro/def_hho.p.z/mdl", "roxas_ho_h"),
    ("graphics/ba/ch/ro/w_.p2#21/mdl", "ro_w13600"): KEYBLADE,
    ("graphics/ba/ch/ro/w_.p2#26/mdl", "ro_w9999a"): KEYBLADE,
    ("graphics/ba/ch/r2/w_.p2#0/mdl", "ro_w13600"): KEYBLADE,
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
    for (folder, model), (from_folder, from_model) in SAME_SETTINGS.items():
        for source in sorted((SETTINGS / from_folder).glob(f"{from_model}.*.txt")):
            name = model + source.name[len(from_model) :]
            if not (SETTINGS / folder / name).is_file():
                put(source, mod / folder / name)


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
