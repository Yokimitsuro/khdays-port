#!/usr/bin/env python3
"""Another game's room in place of Twilight Town's room 0, played: a scratch mod made by the studio
(--replace-room <Mod> tt 0 <game>:<map>) from the player's own collection, then the regression walk "play" on it
in a window, frames saved as pictures. Nothing of the games is kept outside the player's saves/ and mods/.
  python port/tools/collection_room_walk.py ddd:eh_01 --walk-save OLD.sav [--model-collision] [--shots "4400 4650"]
docs/STUDIO.md, "Rooms from the collections".
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regress  # noqa: E402

ROOT = regress.ROOT
DEFAULT_STUDIO = ROOT / "build" / "studio" / "release" / "khdays-studio.exe"  # studio/build.py --config Release
MOD = "CollectionRoomWalk"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("room", help="the room as <game>:<map> (kh2:tt02, ddd:eh_01)")
    parser.add_argument("--walk-save", type=Path, required=True, help="the save the play walk starts from")
    parser.add_argument("--exe", type=Path, default=regress.DEFAULT_EXE, help="the khdays-native.exe to run")
    parser.add_argument("--studio", type=Path, default=DEFAULT_STUDIO, help="the khdays-studio.exe that makes the mod")
    parser.add_argument(
        "--model-collision", action="store_true", help="floors and walls made from the model, not the map's own"
    )
    parser.add_argument("--shots", default="4400 4650", help="the frames saved as pictures (KHDAYS_SHOTS)")
    parser.add_argument("--out", type=Path, help="where the shots and the log go (default: a new temporary folder)")
    parser.add_argument("--keep", action="store_true", help="keep the scratch mod in mods/ afterwards")
    args = parser.parse_args()

    for exe in (args.exe, args.studio):
        if not exe.exists():
            sys.exit(f"{exe} does not exist")
    out = args.out.resolve() if args.out else Path(tempfile.mkdtemp(prefix="khdays-collection-walk-"))
    shots, logs = out / "shots", out / "logs"
    shots.mkdir(parents=True, exist_ok=True)

    mod = ROOT / "mods" / MOD
    if mod.exists():
        shutil.rmtree(mod)
    mod.mkdir(parents=True)
    (mod / "mod.ini").write_text(
        f"id = test.collection_room_walk\nname = {args.room} in Twilight Town's room 0\nversion = 1\n", encoding="utf-8"
    )
    try:
        collision = [] if args.model_collision or args.room.startswith("kh2:") else ["own-collision"]
        made = subprocess.run(
            [str(args.studio), "--port", str(ROOT), "--replace-room", MOD, "tt", "0", args.room, *collision]
        )
        if made.returncode != 0:
            print(f"the studio could not replace the room with {args.room}")
            return made.returncode

        walk = next(w for w in regress.WALKS if w.name == "play")
        regress.WALKS = [dataclasses.replace(walk, mods=str(mod), window=True)]
        os.environ["KHDAYS_SHOTS"] = args.shots
        os.environ["KHDAYS_SHOTS_HD"] = str(shots)
        sys.argv = [
            "regress.py",
            str(args.exe),
            "--walk-save",
            str(args.walk_save),
            "--only",
            "play",
            "--logs",
            str(logs),
        ]
        status = regress.main()
        print(f"shots: {shots}")
        print(f"log: {logs / 'play.log'}")
        return status
    finally:
        if not args.keep:
            shutil.rmtree(mod, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
