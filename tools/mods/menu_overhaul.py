"""The Menu Overhaul mod (docs/MENU_OVERHAUL.md) made from your own copies: its screens' pieces from the KH
collections, the game's own data and the repository's pictures, and the switch days-mods/menu-overhaul.txt.

    python tools/mods/menu_overhaul.py --kh2 DIR --bbs DIR --recom DIR --mare DIR
        [--hd "mods/HD PACK"] [--fonts DIR] [--data data/extracted/<hash>] [--exe khdays-native.exe]
        [--mod "Days - Menu Overhaul"] [--pieces NAME ...]

The collections' folders are the installer's extractions (installer/collection_archive.py) of the 1.5+2.5
collection: KH2's entries of kh2_first and kh2_fifth, Birth by Sleep's bbs_first, Re:Chain of Memories' Recom and
the theater's Mare. --hd is the HD menus' pieces the installer made first (tools/hd), --fonts Televo's KH
Re:Collection fonts (the installer carries them). Each piece writes its files under mods/<mod>/; --pieces makes
those named only.
"""

from __future__ import annotations

import argparse
import importlib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "overhaul"))  # the pieces import each other and common.py by name
from common import Inputs  # noqa: E402

# The pieces, each a module of overhaul/ with make(inputs, mod folder), in the order they are made: a later one may
# read an earlier one's files (campstory the files' pattern, campsave the files' font and the camp's bar, kh2pause the
# pause's words)
PIECES = ["files", "wipes", "campstory", "camp", "campsave", "shop", "pause", "kh2pause", "day", "title", "menu_sounds"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--kh2", type=Path, required=True)
    ap.add_argument("--bbs", type=Path, required=True)
    ap.add_argument("--recom", type=Path, required=True)
    ap.add_argument("--mare", type=Path, required=True)
    ap.add_argument("--hd", type=Path, default=ROOT / "mods" / "HD PACK")
    ap.add_argument("--fonts", type=Path, default=ROOT / "installer" / "fonts")
    ap.add_argument("--data", type=Path)
    ap.add_argument("--exe", type=Path)
    ap.add_argument("--mod", default="Days - Menu Overhaul")
    ap.add_argument("--pieces", nargs="*", choices=PIECES, default=PIECES)
    args = ap.parse_args()
    data = args.data
    if data is None:
        found = sorted((ROOT / "data" / "extracted").glob("*/nitrofs"))
        if not found:
            ap.error("no extracted game data in data/extracted: give --data")
        data = found[0].parent
    inputs = Inputs(args.kh2, args.bbs, args.recom, args.mare, args.hd, args.fonts, data, args.exe)
    mod = ROOT / "mods" / args.mod
    for name in args.pieces:
        print(f"menu overhaul: {name}", flush=True)
        importlib.import_module(name).make(inputs, mod)
    switch = mod / "days-mods" / "menu-overhaul.txt"
    switch.parent.mkdir(parents=True, exist_ok=True)
    switch.write_text(mod.name + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
