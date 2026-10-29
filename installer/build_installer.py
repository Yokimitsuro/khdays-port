"""Builds the port's installer: the Release port and server, the studio (with the KH Mod SDK and its examples), the
Visual C++ runtime, the tools the installer runs and the Python they run with, packed by Inno Setup. The install has the
repository's layout (tools/, installer/ beside data/, mods/, saves/); nothing of the game or the
collections is packed. Output: build/installer/KH358-2-Days-Port-Setup-<version>.exe
  python installer/build_installer.py [--no-build] [--iscc PATH]"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = ROOT / "build" / "installer"
CACHE = OUT / "cache"
STAGE = OUT / "stage"
RELEASE = ROOT / "build" / "port" / "obj-release-x64"
STUDIO_RELEASE = ROOT / "build" / "studio" / "release"  # khdays-studio, the optional mod tool
SDK_RELEASE = ROOT / "build" / "sdk"  # the KH Mod SDK built alone: its examples' plugins

sys.path.insert(0, str(ROOT / "port"))
import build  # noqa: E402

# The repository's files the installer carries, by their place in the repository (the same place
# in the install).
TOOLS = [
    "tools/verify_rom.py",
    "tools/supported_roms.json",
    "tools/extract_data.py",
    "tools/decompress_data.py",
    # the HD tools (the maintainer's mods/HD PACK and the theater's videos)
    "tools/hd/theater_title.py",
    "tools/hd/newgame_dialog.py",
    "tools/hd/theater_settings.py",
    "tools/hd/theater_mission.py",
    "tools/hd/theater_cine.py",
    "tools/hd/theater_movies.py",
    "tools/hd/save_screen.py",
    "tools/hd/ddd_camp.py",
    "tools/hd/kh2_fonts.py",
    "tools/hd/kh2_gauge.py",
    "tools/hd/minimap_icons.py",
    "tools/hd/heart_points.py",
    "port/tools/heart_model_export.cpp",
    "port/tools/kh2_center_gauge.py",
    "port/tools/kh2_add_limit_gauge.py",
    "port/tools/kh2_add_limit_marker.py",
    "port/tools/kh2_add_party_gauges.py",
    "docs/examples/kh2-gauge.json",
    "port/movies/hd_scenes.txt",  # theater_movies.py's scene map: the scene videos it makes
]
# the installer's own (its tests and this script stay in the repository)
INSTALLER_KEEP = ("build_installer.py", "pinned.json")
NEVER = ("data", "saves", "mods")  # the game's and the player's: never packed


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def version() -> str:
    """The repository's VERSION ("0.1.0") when the commit is its release tag (v0.1.0) and nothing is
    changed; otherwise that version with the commit after it, and "-modified" when files changed."""
    number = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    commit = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.strip()
    dirty = subprocess.run(
        ["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.strip()
    tags = subprocess.run(
        ["git", "tag", "--points-at", "HEAD"], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout.split()
    if f"v{number}" in tags and not dirty:
        return number
    return f"{number}+{commit}" + ("-modified" if dirty else "")


def pe_imports(exe: Path) -> list[str]:
    """The DLLs `exe` imports by name (its import directory)."""
    data = exe.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    directories = opt + (112 if magic == 0x20B else 96)
    import_rva = struct.unpack_from("<I", data, directories + 8)[0]
    table = [struct.unpack_from("<IIIII", data, opt + optional + 40 * i + 8) for i in range(sections)]

    def offset(rva: int) -> int:
        for size, address, _raw_size, raw, _ in table:
            if address <= rva < address + max(size, _raw_size):
                return rva - address + raw
        raise ValueError(f"rva 0x{rva:x} in no section")

    names, at = [], offset(import_rva)
    while True:
        name_rva = struct.unpack_from("<I", data, at + 12)[0]
        if name_rva == 0:
            return names
        o = offset(name_rva)
        names.append(data[o : data.index(b"\0", o)].decode("ascii"))
        at += 20


def check_packable(source: Path) -> None:
    try:
        rel = source.resolve().relative_to(ROOT)
    except ValueError:
        return  # outside the repository (the Visual C++ runtime)
    if rel.parts and rel.parts[0] in NEVER:
        sys.exit(f"build_installer: refusing to pack {rel}: nothing under {'/'.join(NEVER)} goes in the installer")


def copy(source: Path, to: Path, packed: list[str]) -> None:
    check_packable(source)
    to.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, to)
    packed.append(str(to.relative_to(STAGE)))


def runtime_dlls(exe: Path, env: dict[str, str]) -> list[Path]:
    """The Visual C++ runtime DLLs the port imports, from Visual Studio's redistributable folder
    (Microsoft allows them beside the program)."""
    redist = Path(env["VCToolsRedistDir"]) / "x64"
    crt = sorted(redist.glob("Microsoft.VC*.CRT"))
    if not crt:
        sys.exit(f"build_installer: no Microsoft.VC*.CRT under {redist}")
    found = []
    for name in pe_imports(exe):
        candidate = crt[-1] / name.lower()
        if candidate.is_file():
            found.append(candidate)
    return found


def stage_python(packed: list[str]) -> None:
    """The pinned Python, unpacked, with its two libraries beside it on its path."""
    pinned = json.loads((HERE / "pinned.json").read_text(encoding="utf-8"))["files"]
    python = STAGE / "setup" / "python"
    site = python / "Lib" / "site-packages"
    for item in pinned:
        source = CACHE / item["name"]
        if not source.is_file():
            sys.exit(f"build_installer: {source} is missing: fetch it (installer/pinned.json gives its URL)")
        got = sha256(source)
        if got != item["sha256"]:
            sys.exit(f"build_installer: {source.name} has SHA-256 {got}, pinned {item['sha256']}")
        target = python if item["name"].endswith(".zip") else site
        with zipfile.ZipFile(source) as z:
            z.extractall(target)
        packed.append(f"setup/python <- {item['name']}")
    # the embeddable Python's path file: its own zip and folder, and the libraries' folder
    pth = sorted(python.glob("python*._pth"))
    if len(pth) != 1:
        sys.exit("build_installer: the embeddable Python has no single ._pth file")
    lines = pth[0].read_text(encoding="utf-8").splitlines()
    if "Lib\\site-packages" not in lines:
        lines.insert(2, "Lib\\site-packages")
    pth[0].write_text("\n".join(lines) + "\n", encoding="utf-8")


def manifest_value(ini: Path, key: str) -> str | None:
    """A mod.ini's `key = value` (its comment after " #" left out)."""
    for line in ini.read_text(encoding="utf-8").splitlines():
        name, equals, value = line.partition("=")
        if equals and name.strip() == key:
            return value.split(" #")[0].strip()
    return None


def tracked(folder: str) -> list[Path]:
    """The repository's files under `folder`, as git has them."""
    out = subprocess.run(["git", "ls-files", folder], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    return [ROOT / line for line in out.splitlines() if line]


def stage_sdk(packed: list[str]) -> None:
    """The KH Mod SDK for those who install the studio (in studio/: its task): the SDK as the repository has it
    (sdk/), and each example as a mod ready to copy into mods/, its plugin built (mods-examples/<example>)."""
    for source in tracked("sdk"):
        copy(source, STAGE / "studio" / source.relative_to(ROOT), packed)
    examples = ROOT / "sdk" / "examples"
    for example in sorted(e for e in examples.iterdir() if (e / "mod.ini").is_file()):
        library = manifest_value(example / "mod.ini", "native.windows-x64")
        built = SDK_RELEASE / "examples" / example.name / Path(library or "").name
        if library is None or not built.is_file():
            sys.exit(f"build_installer: {example.name}'s plugin is not built ({built}; cmake -S sdk -B build/sdk)")
        to = STAGE / "studio" / "mods-examples" / example.name
        for source in tracked(f"sdk/examples/{example.name}"):
            if source.suffix not in (".c", ".cpp", ".h", ".py") and source.name != "CMakeLists.txt":
                copy(source, to / source.relative_to(example), packed)  # the mod's own files, not its sources
        copy(built, to / library, packed)


def find_iscc(given: str | None) -> Path:
    candidates = [
        given,
        os.environ.get("KHDAYS_ISCC"),
        os.path.expandvars(r"%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"),
        os.path.expandvars(r"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"),
        os.path.expandvars(r"%ProgramFiles%\Inno Setup 6\ISCC.exe"),
    ]
    for c in candidates:
        if c and Path(c).is_file():
            return Path(c)
    sys.exit("build_installer: Inno Setup 6's ISCC.exe not found (install it, or name it with --iscc or KHDAYS_ISCC)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--no-build", action="store_true", help="use the Release port already built")
    ap.add_argument("--iscc", help="Inno Setup's command-line compiler")
    args = ap.parse_args()
    iscc = find_iscc(args.iscc)
    if not args.no_build:
        subprocess.run([sys.executable, str(ROOT / "port" / "build.py"), "--config", "Release"], cwd=ROOT, check=True)
        subprocess.run([sys.executable, str(ROOT / "studio" / "build.py"), "--config", "Release"], cwd=ROOT, check=True)
        sdk_env = build.msvc_env()
        build.run(
            ["cmake", "-S", str(ROOT / "sdk"), "-B", str(SDK_RELEASE), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"],
            sdk_env,
        )
        build.run(["cmake", "--build", str(SDK_RELEASE)], sdk_env)
    exe = RELEASE / "khdays-native.exe"
    if not exe.is_file():
        sys.exit(f"build_installer: {exe} is not built")
    studio_exe = STUDIO_RELEASE / "khdays-studio.exe"
    if not studio_exe.is_file():
        sys.exit(f"build_installer: {studio_exe} is not built (studio/build.py --config Release)")
    env = build.msvc_env()
    ver = version()

    if STAGE.exists():
        shutil.rmtree(STAGE)
    STAGE.mkdir(parents=True)
    packed: list[str] = []
    copy(exe, STAGE / exe.name, packed)
    for dll in runtime_dlls(exe, env):
        copy(dll, STAGE / dll.name, packed)
    # the multiplayer server, always, beside the game (for a player who hosts; the game's default
    # server is the maintainer's)
    server_exe = RELEASE / "khdays-server.exe"
    if not server_exe.is_file():
        sys.exit(f"build_installer: {server_exe} is not built")
    copy(server_exe, STAGE / server_exe.name, packed)
    for dll in runtime_dlls(server_exe, env):
        if not (STAGE / dll.name).exists():
            copy(dll, STAGE / dll.name, packed)
    # the mod studio and its runtime, in studio/ so the installer can make it an optional task
    copy(studio_exe, STAGE / "studio" / studio_exe.name, packed)
    for dll in runtime_dlls(studio_exe, env):
        copy(dll, STAGE / "studio" / dll.name, packed)
    stage_sdk(packed)
    for rel in TOOLS:
        copy(ROOT / rel, STAGE / rel, packed)
    # the port's license and the third parties' notices, at the install's root
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        copy(ROOT / name, STAGE / name, packed)
    # the example gameplay mods and their interface, outside mods/ so none is on until copied there
    for source in sorted((ROOT / "port" / "mods_examples").rglob("*")):
        if source.is_file():
            copy(source, STAGE / "mods-examples" / source.relative_to(ROOT / "port" / "mods_examples"), packed)
    copy(ROOT / "docs" / "SCRIPTING.md", STAGE / "mods-examples" / "SCRIPTING.md", packed)
    for source in sorted(HERE.iterdir()):
        if source.is_file() and source.suffix in (".py", ".json", ".txt", ".ini") and source.name not in INSTALLER_KEEP:
            copy(source, STAGE / "installer" / source.name, packed)
    stage_python(packed)
    (STAGE / "setup").mkdir(exist_ok=True)
    (STAGE / "setup" / "version.txt").write_text(ver + "\n", encoding="utf-8")

    subprocess.run(
        [str(iscc), "/Qp", f"/DVersion={ver}", f"/DStage={STAGE}", f"/O{OUT}", str(HERE / "khdays.iss")], check=True
    )
    setup = OUT / f"KH358-2-Days-Port-Setup-{ver}.exe"
    print("packed:")
    for p in packed:
        print("  " + p)
    print(
        f"nothing from {', '.join(n + '/' for n in NEVER)}; installer: {setup} ({setup.stat().st_size:,} bytes, "
        f"SHA-256 {sha256(setup)})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
