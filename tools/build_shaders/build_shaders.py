#!/usr/bin/env python3
"""Compile the GLSL shaders into embedded C arrays (SPIR-V).

Re-run this after editing any file under `shaders/`. The resulting `.inc`
files are committed so that the build and CI do not need the Vulkan SDK.

Requires `glslc` (part of the Vulkan SDK). It is looked up on the PATH and,
failing that, in `$VULKAN_SDK/Bin`.

Run with no arguments to compile every stage; `--help` describes the rest, and
`--dry-run` reports what would be compiled without touching the tree.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SHADERS = REPO / "shaders"
OUT = REPO / "platform" / "pc" / "generated"

# (source file, output .inc name)
STAGES = [
    ("model.vert", "model.vert.spv.inc"),
    ("model.frag", "model.frag.spv.inc"),
]


def find_glslc() -> str:
    found = shutil.which("glslc")
    if found:
        return found
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidate = Path(sdk) / "Bin" / ("glslc.exe" if os.name == "nt" else "glslc")
        if candidate.exists():
            return str(candidate)
    print(
        "ERROR: 'glslc' not found. Install the Vulkan SDK or add glslc to the PATH.",
        file=sys.stderr,
    )
    sys.exit(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compile shaders/*.vert|frag into committed SPIR-V C arrays.",
        epilog=(
            "With no arguments it compiles every stage, overwriting "
            "platform/pc/generated/*.inc. Those files are committed, so a run "
            "shows up in `git diff` whenever the SPIR-V changes. Requires glslc "
            "(Vulkan SDK), found on the PATH or under $VULKAN_SDK/Bin."
        ),
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Report the stages that would be compiled and exit without "
             "writing anything.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.dry_run:
        print(f"glslc : {find_glslc()}")
        for source, output in STAGES:
            print(f"would compile {source} -> "
                  f"{(OUT / output).relative_to(REPO)}")
        return 0

    glslc = find_glslc()
    OUT.mkdir(parents=True, exist_ok=True)
    for source, output in STAGES:
        src = SHADERS / source
        dst = OUT / output
        subprocess.run(
            [glslc, "-O", str(src), "-mfmt=c", "-o", str(dst)],
            check=True,
        )
        print(f"compiled {source} -> {dst.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
