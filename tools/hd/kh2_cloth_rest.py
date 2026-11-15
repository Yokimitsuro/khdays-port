"""The HD pack's cloth rest shapes: khdays-secondary-rest run on every installed model with secondary motion, its
`rest` lines written into the settings file in kh2_models/ the model takes (its own, or the one SAME_SETTINGS gives
it), and that file installed again. A file several models share is written from its own model and checked against
the others (docs/SECONDARY_MOTION.md, "Rest shape")."""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from kh2_models import MODELS, SAME_SETTINGS, SETTINGS

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "port" / "obj-release-x64"
SUFFIX = ".secondary.txt"


def source_of(folder: str, model: str) -> Path | None:
    """The kh2_models/ file a model's .secondary.txt is installed from, as kh2_models.make picks it."""
    at: tuple[str, str] | None = (folder, model)
    while at is not None:
        f = SETTINGS / at[0] / f"{at[1]}{SUFFIX}"
        if f.is_file():
            return f
        at = SAME_SETTINGS.get(at)
    return None


def run(exe: Path, mdlx: Path, settings: Path, write: Path | None = None) -> dict[tuple[str, str], tuple[float, ...]]:
    cmd = [str(exe), str(mdlx), str(settings)] + (["--write", str(write)] if write else [])
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"kh2_cloth_rest: {mdlx.name}: {r.stderr.strip()}")
    print("  " + r.stderr.strip().replace("\n", "\n  "))
    lines = {}
    for line in r.stdout.splitlines():
        group, _, bone, *xyz = line.split()
        lines[(group, bone)] = tuple(float(v) for v in xyz)
    return lines


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mod", type=Path, default=BUILD / "mods" / "HD PACK", help="the installed HD pack")
    p.add_argument("--exe", type=Path, default=BUILD / "khdays-secondary-rest.exe")
    p.add_argument("--backup", type=Path, help="a folder to keep the installed files it replaces in")
    a = p.parse_args()
    if not a.exe.is_file():
        sys.exit(f"kh2_cloth_rest: build khdays-secondary-rest first ({a.exe} is missing)")
    users: dict[Path, list[tuple[str, str]]] = {}
    for folder, model, *_ in MODELS:
        source = source_of(folder, model)
        if source is not None and (a.mod / folder / f"{model}.mdlx").is_file():
            users.setdefault(source, []).append((folder, model))
    for source, models in sorted(users.items()):
        own = source.relative_to(SETTINGS).parent.as_posix(), source.name[: -len(SUFFIX)]
        models.sort(key=lambda m: m != own)  # its own model first, when installed
        print(source.relative_to(SETTINGS).as_posix())
        written = None
        for folder, model in models:
            installed = a.mod / folder / f"{model}{SUFFIX}"
            if a.backup and installed.is_file():
                keep = a.backup / folder / installed.name
                keep.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(installed, keep)
            shutil.copyfile(source, installed)  # the source as it is now, its presets beside it
            mdlx = a.mod / folder / f"{model}.mdlx"
            if written is None:
                print(f" {folder}/{model} (written)")
                written = run(a.exe, mdlx, installed, source)
                shutil.copyfile(source, installed)
                continue
            print(f" {folder}/{model}")
            lines = run(a.exe, mdlx, installed)
            shutil.copyfile(source, installed)
            apart = max(
                (max(abs(x - y) for x, y in zip(v, written[k])) for k, v in lines.items() if k in written), default=0.0
            )
            if lines.keys() != written.keys() or apart > 0.5:
                print(f"  ! its own hang differs from the written one by up to {apart:.2f} degrees")


if __name__ == "__main__":
    main()
