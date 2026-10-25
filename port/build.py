#!/usr/bin/env python3
"""Build the native Windows (x64) executable: prepare.py into build/port/gen-x64, then
port/CMakeLists.txt with Ninja in Visual Studio's x64 environment, into build/port/obj-x64 (Debug),
obj-release-x64 (Release) or obj-asan-x64 (--asan: AddressSanitizer on the port's own code).
  python port/build.py [--config Debug|Release] [--asan] [--jobs N]
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "build" / "port" / "gen-x64"
OBJ = ROOT / "build" / "port" / "obj-x64"


def vcvarsall() -> Path:
    portable = os.environ.get("KHDAYS_VCVARSALL")
    if portable:
        path = Path(portable).resolve()
        if not path.is_file():
            raise SystemExit(f"KHDAYS_VCVARSALL does not exist: {path}")
        return path
    vswhere = (
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    out = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    if not out:
        raise SystemExit("no Visual Studio with the C++ x64 build tools found")
    return Path(out) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"


def msvc_env(arch: str = "x64") -> dict[str, str]:
    """The Visual Studio environment that builds for `arch` (vcvarsall's name)."""
    out = subprocess.run(f'"{vcvarsall()}" {arch} >nul && set', shell=True, capture_output=True, text=True, check=True)
    env = dict(line.split("=", 1) for line in out.stdout.splitlines() if "=" in line)
    env["VSLANG"] = "1033"  # English diagnostics
    return env


def oem_console() -> int:
    """Make the console's output code page the system's OEM one; return the old one (0: none).
    Ninja learns each file's headers from cl's localized /showIncludes lines, matched byte for byte
    against the prefix found at configure time and written in the console's code page: a tree
    built in another code page than it was configured in rebuilt nothing on a header change."""
    if os.name != "nt":
        return 0
    import ctypes

    kernel32 = ctypes.windll.kernel32
    old = kernel32.GetConsoleOutputCP()
    if old and old != kernel32.GetOEMCP():
        kernel32.SetConsoleOutputCP(kernel32.GetOEMCP())
    return old


def check_header_tracking(obj: Path, env: dict[str, str]) -> None:
    """Warn when ninja recorded no header for one of the port's own files."""
    target = "CMakeFiles/khdays-native.dir/runtime/display/display.c.obj"
    ninja = shutil.which("ninja", path=env.get("Path") or env.get("PATH"))
    if ninja is None:
        return
    out = subprocess.run([ninja, "-C", str(obj), "-t", "deps", target], env=env, capture_output=True).stdout
    if b"#deps 0" in out:
        print(
            "build.py: ninja recorded no header of runtime/display/display.c: a change to a header alone will not "
            "rebuild what includes it. Configure and build the tree in the same console code page "
            "(see oem_console).",
            file=sys.stderr,
        )


def run(cmd: list[str], env: dict[str, str]) -> None:
    exe = shutil.which(cmd[0], path=env.get("Path") or env.get("PATH"))
    if exe is None:
        raise SystemExit(f"{cmd[0]} not found")
    subprocess.run([exe] + cmd[1:], env=env, check=True)


def generated(config: str, asan: bool = False) -> tuple[Path, Path]:
    """prepare.py's output directory and the build directory."""
    if asan:
        return GEN, OBJ.with_name("obj-asan-x64")
    return GEN, OBJ if config == "Debug" else OBJ.with_name("obj-release-x64")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    ap.add_argument(
        "--asan",
        action="store_true",
        help="AddressSanitizer on the port's own code (Release flags; build/port/obj-asan-x64)",
    )
    ap.add_argument("--jobs", type=int, default=0)
    args = ap.parse_args()
    if args.asan:
        args.config = "Release"

    gen, obj = generated(args.config, args.asan)
    console = oem_console()
    try:
        return build_tree(args, gen, obj)
    finally:
        if console:
            import ctypes

            ctypes.windll.kernel32.SetConsoleOutputCP(console)


def build_tree(args: argparse.Namespace, gen: Path, obj: Path) -> int:
    """Prepares the decomp, configures the tree and builds it."""
    subprocess.run([sys.executable, str(ROOT / "port" / "tools" / "prepare.py"), "--out", str(gen)], check=True)
    env = msvc_env()
    configure = [
        "cmake",
        "-S",
        str(ROOT / "port"),
        "-B",
        str(obj),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={args.config}",
        f"-DKHDAYS_GEN={gen}",
        "-DCMAKE_C_COMPILER=cl",
        "-DCMAKE_CXX_COMPILER=cl",
        # SDL3 is fetched into each build directory (port/CMakeLists.txt
        # pins the release); drop a source directory an older cache kept.
        "-UFETCHCONTENT_SOURCE_DIR_SDL3",
        f"-DKHDAYS_ASAN={'ON' if args.asan else 'OFF'}",
    ]
    run(configure, env)
    build = ["cmake", "--build", str(obj)]
    if args.jobs:
        build += ["--parallel", str(args.jobs)]
    run(build + ["--", "-k", "0"], env)  # report every failing file at once
    check_header_tracking(obj, env)
    return 0


if __name__ == "__main__":
    sys.exit(main())
