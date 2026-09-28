#!/usr/bin/env python3
"""Build the native (32-bit Windows) executable from the decompilation.

  python native/build.py [--config Debug|Release] [--jobs N]

Runs prepare.py, then configures native/CMakeLists.txt for x86 with Ninja inside
the Visual Studio x86 environment, then builds. Output: build/native/obj/khdays-native.exe
(Release: build/native/obj-release/, so the two configurations do not rebuild
each other).
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "build" / "native" / "gen"
OBJ = ROOT / "build" / "native" / "obj"


def vcvarsall() -> Path:
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
        "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    out = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        capture_output=True, text=True, check=True).stdout.strip()
    if not out:
        raise SystemExit("no Visual Studio with the C++ x86/x64 tools found")
    return Path(out) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"


def msvc_env() -> dict[str, str]:
    out = subprocess.run(f'"{vcvarsall()}" x86 >nul && set', shell=True,
                         capture_output=True, text=True, check=True)
    env = dict(line.split("=", 1) for line in out.stdout.splitlines() if "=" in line)
    env["VSLANG"] = "1033"  # English diagnostics
    return env


def run(cmd: list[str], env: dict[str, str]) -> None:
    exe = shutil.which(cmd[0], path=env.get("Path") or env.get("PATH"))
    if exe is None:
        raise SystemExit(f"{cmd[0]} not found")
    subprocess.run([exe] + cmd[1:], env=env, check=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    ap.add_argument("--jobs", type=int, default=0)
    args = ap.parse_args()

    subprocess.run([sys.executable, str(ROOT / "native" / "tools" / "prepare.py"),
                    "--out", str(GEN)], check=True)
    env = msvc_env()
    obj = OBJ if args.config == "Debug" else OBJ.with_name("obj-release")
    configure = ["cmake", "-S", str(ROOT / "native"), "-B", str(obj), "-G", "Ninja",
                 f"-DCMAKE_BUILD_TYPE={args.config}", f"-DKHDAYS_GEN={GEN}",
                 "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl"]
    sdl = ROOT / "build" / "_deps" / "sdl3-src"
    if (sdl / "CMakeLists.txt").exists():  # the port's download of the same release
        configure.append(f"-DFETCHCONTENT_SOURCE_DIR_SDL3={sdl}")
    run(configure, env)
    build = ["cmake", "--build", str(obj)]
    if args.jobs:
        build += ["--parallel", str(args.jobs)]
    run(build + ["--", "-k", "0"], env)  # report every failing file at once
    return 0


if __name__ == "__main__":
    sys.exit(main())
