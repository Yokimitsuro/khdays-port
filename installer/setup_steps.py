"""The installer's helper: each step the installer cannot do by itself, run with its Python. A result
is one JSON object (exit 0 done, 1 a refusal the player can fix, 2 a failure); what a step makes is
whole or absent (`.partial`, setup/state.json) (installer/README.md, "How it works").
  python -I setup_steps.py <command> [arguments] --result <file.json> [--language es|en|fr|de|it]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import traceback
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Callable, Iterator

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # its own modules (run with -I, the script's folder is not on the path)
LANGUAGES = ("en", "es", "fr", "de", "it")

OK, REFUSED, FAILED = 0, 1, 2


class Refusal(Exception):
    """Something the player can put right: its message key and the values it names."""

    def __init__(self, key: str, **values: Any) -> None:
        super().__init__(key)
        self.key = key
        self.values = values


# The helper's own words, in the installer's five languages.
MESSAGES: dict[str, dict[str, str]] = {
    "rom-supported": {
        "en": "This is the ROM the port needs: {name}.",
        "es": "Es la ROM que necesita el port: {name}.",
        "fr": "C'est la ROM dont le portage a besoin : {name}.",
        "de": "Das ist das ROM, das die Portierung braucht: {name}.",
        "it": "È la ROM che serve al port: {name}.",
    },
    "rom-wrong": {
        "en": "This file is not the ROM the port needs. It needs your own dump of {names}.",
        "es": "Este archivo no es la ROM que necesita el port. Necesita tu propio volcado de {names}.",
        "fr": "Ce fichier n'est pas la ROM dont le portage a besoin. Il lui faut votre propre copie de {names}.",
        "de": "Diese Datei ist nicht das ROM, das die Portierung braucht. Sie braucht deine eigene Kopie von {names}.",
        "it": "Questo file non è la ROM che serve al port. Serve la tua copia di {names}.",
    },
    "rom-missing": {
        "en": "There is no file at {path}.",
        "es": "No hay ningún archivo en {path}.",
        "fr": "Il n'y a aucun fichier à {path}.",
        "de": "Unter {path} gibt es keine Datei.",
        "it": "Non c'è nessun file in {path}.",
    },
    "data-ready": {
        "en": "The game's data is ready.",
        "es": "Los datos del juego están listos.",
        "fr": "Les données du jeu sont prêtes.",
        "de": "Die Daten des Spiels sind bereit.",
        "it": "I dati del gioco sono pronti.",
    },
    "preparing-data": {
        "en": "Preparing the game's data from the ROM",
        "es": "Preparando los datos del juego a partir de la ROM",
        "fr": "Préparation des données du jeu à partir de la ROM",
        "de": "Die Daten des Spiels werden aus dem ROM vorbereitet",
        "it": "Preparazione dei dati del gioco dalla ROM",
    },
    "failed": {
        "en": "Something went wrong: {what}",
        "es": "Algo ha fallado: {what}",
        "fr": "Une erreur s'est produite : {what}",
        "de": "Etwas ist schiefgegangen: {what}",
        "it": "Qualcosa è andato storto: {what}",
    },
}


def words(key: str, language: str, **values: Any) -> str:
    texts = MESSAGES.get(key, {})
    text = texts.get(language) or texts.get("en") or key
    return text.format(**values)


def progress(done: int, total: int, what: str) -> None:
    print(f"progress {done} {total} {what}", flush=True)


# ---- Where the tools are -----------------------------------------------------------------------


def tool_folders() -> dict[str, Path]:
    """The repository's tools beside installer/: in the repository and in an install alike."""
    tools = HERE.parent / "tools"
    return {"rom": tools, "hd": tools / "hd", "mods": tools / "mods"}


# ---- Whole or absent ---------------------------------------------------------------------------


def partial_name(final: Path) -> Path:
    return final.with_name(final.name + ".partial")


def remove(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    elif path.exists() or path.is_symlink():
        path.unlink()


@contextmanager
def making(final: Path) -> Iterator[Path]:
    """The place to make `final` in: its `.partial` name, emptied first; renamed to `final` (an
    older one replaced) only when the block ends without an error, else left to be redone."""
    work = partial_name(final)
    remove(work)
    final.parent.mkdir(parents=True, exist_ok=True)
    yield work
    put_in_place(work, final)


def put_in_place(made: Path, final: Path) -> None:
    """`made` put at `final`, an older one there replaced: the whole folder at once, or file by file
    when a window of the File Explorer shows a folder inside the older one (it cannot be renamed then)."""
    final.parent.mkdir(parents=True, exist_ok=True)
    old = final.with_name(final.name + ".old")
    remove(old)
    if not final.exists():
        os.replace(made, final)
        return
    try:
        os.replace(final, old)
    except PermissionError:
        if not (made.is_dir() and final.is_dir()):
            raise
        try:
            move_into(made, final)
        except PermissionError as error:
            raise Refusal("in-use", file=error.filename2 or error.filename) from error
        return
    os.replace(made, final)
    remove(old)


def move_into(made: Path, final: Path) -> None:
    """`final` made the same as `made` by moving its files in one by one."""
    kept = {p.relative_to(made) for p in made.rglob("*")}
    for path in sorted(made.rglob("*")):
        target = final / path.relative_to(made)
        if path.is_dir():
            if target.exists() and not target.is_dir():
                remove(target)
            target.mkdir(exist_ok=True)
        else:
            if target.is_dir():
                remove(target)
            os.replace(path, target)
    # what the older one had and the new one does not, deepest first
    for path in sorted(final.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        if path.relative_to(final) not in kept:
            remove(path)
    remove(made)


MESSAGES["in-use"] = {
    "en": "Another program is using {file}: close the game and the studio.",
    "es": "Otro programa está usando {file}: cierra el juego y el estudio.",
    "fr": "Un autre programme utilise {file} : fermez le jeu et le studio.",
    "de": "Ein anderes Programm verwendet {file}: Schließe das Spiel und das Studio.",
    "it": "Un altro programma sta usando {file}: chiudi il gioco e lo studio.",
}


# ---- The install's state -----------------------------------------------------------------------


def state_path(install: Path) -> Path:
    return install / "setup" / "state.json"


def load_state(install: Path) -> dict[str, Any]:
    path = state_path(install)
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_state(install: Path, state: dict[str, Any]) -> None:
    path = state_path(install)
    path.parent.mkdir(parents=True, exist_ok=True)
    work = partial_name(path)
    work.write_text(json.dumps(state, indent=2, ensure_ascii=False), encoding="utf-8")
    os.replace(work, path)


def step_done(state: dict[str, Any], step: str, inputs: dict[str, Any]) -> bool:
    """Whether `step` is complete for these same inputs: a rerun skips it then, and redoes it when
    it is not complete or its inputs changed (another ROM, another collection folder)."""
    record = state.get("steps", {}).get(step)
    return bool(record) and record.get("complete") is True and record.get("inputs") == inputs


def mark_step(state: dict[str, Any], step: str, inputs: dict[str, Any], **more: Any) -> None:
    state.setdefault("steps", {})[step] = {"complete": True, "inputs": inputs, **more}


def same_folder(a: str, b: str) -> bool:
    """Whether two folder paths name the same folder, as Windows compares them (case, separators)."""
    return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


def hd_made(state: dict[str, Any], step: str, folders: dict[str, str], made: Path) -> bool:
    """Whether an HD step was made from these collection folders by these tools and what it made is
    still there: an update then leaves it as it is."""
    record = state.get("steps", {}).get(step)
    if not record or record.get("complete") is not True or not made.is_dir() or not any(made.iterdir()):
        return False
    if record.get("made_by") != made_by(step):
        return False  # other tools (or a record from before they were kept): made again
    return same_inputs(record, folders)


def same_inputs(record: dict[str, Any], folders: dict[str, str]) -> bool:
    recorded = record.get("inputs", {})
    return set(recorded) == set(folders) and all(same_folder(recorded[k], folders[k]) for k in folders)


def made_by(step: str) -> str:
    """What an HD step's pieces are made by as a whole: for the videos, their tool (with what it imports)
    and the scene map; for the menus, their tools as earlier installers recorded them, before each was
    kept apart (`menu_tools_to_make`). An update whose tools differ makes them again."""
    if step == "hd-menus":
        parts = [repr(HD_MENU_TOOLS).encode()] + [(tool_folders()["hd"] / t).read_bytes() for t, _ in HD_MENU_TOOLS]
        parts += [(HERE.parent / rel).read_bytes() for rel in HD_MENU_SUPPORT]
    else:
        parts = [tool_made_by(t, template).encode() for t, template in HD_VIDEO_TOOLS]
        parts.append((HERE.parent / SCENE_MAP).read_bytes())
    return hashlib.sha256(b"".join(parts)).hexdigest()


IMPORTS = re.compile(r"^\s*(?:import|from)\s+(\w+)", re.MULTILINE)


def tool_made_by(tool: str, template: list[str]) -> str:
    """What one HD tool's pieces are made by: the tool with its arguments and the repository's tools it
    imports (theater_title.py's helpers, decompress_data.py...), as installed beside the installer."""
    folders = tool_folders()
    parts, seen, waiting = [repr((tool, template)).encode()], set(), [tool]
    while waiting:
        name = waiting.pop()
        if name in seen:
            continue
        seen.add(name)
        path = next((f / name for f in (folders["hd"], folders["rom"]) if (f / name).is_file()), None)
        if path is None:
            continue  # not one of the repository's (numpy, PIL...)
        source = path.read_bytes()
        parts.append(source)
        waiting += [module + ".py" for module in IMPORTS.findall(source.decode("utf-8"))]
    if tool in ("kh2_gauge.py", "heart_points.py"):
        parts += [(HERE.parent / rel).read_bytes() for rel in HD_MENU_SUPPORT]
    return hashlib.sha256(b"".join(parts)).hexdigest()


def menu_tools_to_make(state: dict[str, Any], folders: dict[str, str], made: Path) -> list[str]:
    """The HD menu tools to run, in their order: none when each one's files are there, made by the same
    tool from these collections; else those whose tool changed or whose files went missing, with every
    tool that made one of the same files. A record from before each tool was kept: all or none."""
    everything = [tool for tool, _ in HD_MENU_TOOLS]
    record = state.get("steps", {}).get("hd-menus")
    if not record or not made.is_dir() or not any(made.iterdir()) or not same_inputs(record, folders):
        return everything
    tools = record.get("tools")
    if tools is None:
        return [] if hd_made(state, "hd-menus", folders, made) else everything
    again = {
        tool
        for tool, template in HD_MENU_TOOLS
        if tool not in tools
        or tools[tool]["by"] != tool_made_by(tool, template)
        or not all((made / f).is_file() for f in tools[tool]["files"])
    }
    while True:
        files = {f for tool in again if tool in tools for f in tools[tool]["files"]}
        sharing = {t for t in tools if t in everything and t not in again and files & set(tools[t]["files"])}
        if not sharing:
            return [tool for tool in everything if tool in again]
        again |= sharing


# ---- The commands ------------------------------------------------------------------------------

Command = Callable[[argparse.Namespace], dict[str, Any]]
COMMANDS: dict[str, tuple[Command, Callable[[argparse.ArgumentParser], None], str]] = {}


def command(name: str, help_text: str, arguments: Callable[[argparse.ArgumentParser], None]):
    def register(fn: Command) -> Command:
        COMMANDS[name] = (fn, arguments, help_text)
        return fn

    return register


def load_tool(folder: Path, name: str):
    """One of the repository's tools as a module (its functions, not its command line)."""
    import importlib.util

    spec = importlib.util.spec_from_file_location(name, folder / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def rom_check(rom: Path) -> dict[str, Any]:
    """The ROM's SHA-256 against the supported list (tools/verify_rom.py): its entry, or a refusal."""
    folder = tool_folders()["rom"]
    verify = load_tool(folder, "verify_rom")
    roms = verify.load_database(folder / "supported_roms.json")
    if not rom.is_file():
        raise Refusal("rom-missing", path=str(rom))
    try:
        info = verify.read_rom_info(rom)
    except (OSError, ValueError):
        info = None  # too small or unreadable: not the ROM either
    entry = verify.find_supported_rom(info, roms) if info is not None else None
    if entry is None:
        raise Refusal("rom-wrong", names=", ".join(r.get("name", "?") for r in roms))
    return {"name": entry.get("name", ""), "region": info.region, "sha256": info.sha256}


def _rom_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("rom", type=Path)


@command("verify-rom", "whether a file is the ROM the port supports (exit 1 when not)", _rom_arguments)
def verify_rom(args: argparse.Namespace) -> dict[str, Any]:
    found = rom_check(args.rom)
    return {"supported": True, **found, "message": words("rom-supported", args.language, name=found["name"])}


@command(
    "find-rom",
    "a supported ROM already on this computer where KH Melon Mix keeps a player's own",
    lambda p: p.add_argument(
        "folders",
        type=Path,
        nargs="*",
        help="KINGDOM HEARTS HD 1.5+2.5 ReMIX folders (default: Steam's and the Epic Games Store's)",
    ),
)
def find_rom(args: argparse.Namespace) -> dict[str, Any]:
    folders = list(args.folders)
    if not folders:
        import epic_library
        import steam_library

        found = [
            steam_library.app_folder(steam_library.APP_1525),
            epic_library.collection_folder(epic_library.MARKER_1525),
        ]
        folders = [f for f in found if f is not None]
    for folder in folders:
        candidate = folder / "Image" / "melon" / "roms" / "days.nds"
        if candidate.is_file():
            try:
                checked = rom_check(candidate)
            except Refusal:
                continue
            return {"found": str(candidate.resolve()), **checked}
    return {"found": None}


def extracted_root(install: Path) -> Path:
    return install / "data" / "extracted"


@command(
    "prepare-data",
    "the game's data prepared from the ROM (tools/extract_data.py)",
    lambda p: (p.add_argument("rom", type=Path), p.add_argument("install", type=Path)),
)
def prepare_data(args: argparse.Namespace) -> dict[str, Any]:
    install = args.install.resolve()
    checked = rom_check(args.rom)
    name = checked["sha256"][:16]  # the extraction's own folder name (extract_data.py)
    final = extracted_root(install) / name
    state = load_state(install)
    # (the decompressed view too: the HD tools read files there)
    inputs = {"rom": checked["sha256"], "decompressed": True}
    if step_done(state, "data", inputs) and (final / "nitrofs").is_dir():
        return {"data": str(final), "skipped": True, "message": words("data-ready", args.language)}
    progress(0, 1, words("preparing-data", args.language))
    folder = tool_folders()["rom"]
    extract = load_tool(folder, "extract_data")
    with making(final) as work:
        # the tool writes <root>/<name>: its root is the .partial folder, its output moved up after
        out = extract.extract_rom(
            rom_path=args.rom,
            output_root=work,
            database_path=folder / "supported_roms.json",
            allow_unknown=False,
            force=True,
        )
        inner = work.with_name(work.name + ".inner")
        remove(inner)
        os.replace(out, inner)
        remove(work)
        os.replace(inner, work)
        # the files the game keeps LZ-compressed, also as they are read (decompressed/)
        decompress = load_tool(folder, "decompress_data")
        decompress.decompress_extraction(work)
    size = sum(p.stat().st_size for p in final.rglob("*") if p.is_file())
    mark_step(state, "data", inputs, folder=str(final.relative_to(install)))
    save_state(install, state)
    progress(1, 1, words("data-ready", args.language))
    return {"data": str(final), "bytes": size, "message": words("data-ready", args.language)}


@command(
    "remove-user-files",
    "the player's saves and settings moved to the Recycle Bin (on uninstall)",
    lambda p: p.add_argument("install", type=Path),
)
def remove_user_files(args: argparse.Namespace) -> dict[str, Any]:
    saves = args.install.resolve() / "saves"
    moved = []
    if saves.exists():
        to_recycle_bin(saves)
        moved.append(str(saves))
    return {"moved": moved}


def to_recycle_bin(path: Path) -> None:
    """The shell's undoable delete: `path` goes to the Recycle Bin, never deleted outright."""
    import ctypes
    from ctypes import wintypes

    class SHFILEOPSTRUCTW(ctypes.Structure):
        _fields_ = [
            ("hwnd", wintypes.HWND),
            ("wFunc", wintypes.UINT),
            ("pFrom", wintypes.LPCWSTR),
            ("pTo", wintypes.LPCWSTR),
            ("fFlags", ctypes.c_ushort),
            ("fAnyOperationsAborted", wintypes.BOOL),
            ("hNameMappings", ctypes.c_void_p),
            ("lpszProgressTitle", wintypes.LPCWSTR),
        ]

    FO_DELETE, FOF_ALLOWUNDO, FOF_NOCONFIRMATION, FOF_SILENT, FOF_NOERRORUI = 3, 0x40, 0x10, 0x4, 0x400
    op = SHFILEOPSTRUCTW(
        None,
        FO_DELETE,
        str(path) + "\0",
        None,
        FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI,
        False,
        None,
        None,
    )
    if ctypes.windll.shell32.SHFileOperationW(ctypes.byref(op)) != 0 or op.fAnyOperationsAborted:
        raise OSError(f"could not move {path} to the Recycle Bin")


# ---- The HD pieces -----------------------------------------------------------------------------

# The tools that make the maintainer's mods/HD PACK (the HD menus and HUD, both collections) and the
# theater's videos (1.5+2.5), each with its arguments: {mare}, {settings} the 1.5+2.5 theater's and
# settings menu's files (their original/ folder), {ddd} 2.8's kh3d_first (its original/ and
# remastered/), {kh2} KH2's kh2_first extraction, {hand} its title sheet, {movies} the theater's videos.
HD_MENU_TOOLS = [
    ("theater_title.py", ["{mare}"]),
    ("theater_settings.py", ["{settings}"]),
    ("newgame_dialog.py", ["{ddd}", "{mare}", "{hand}"]),
    ("theater_mission.py", ["{mare}"]),
    ("theater_cine.py", ["{mare}"]),
    ("save_screen.py", ["{ddd}", "{mare}"]),
    ("ddd_camp.py", ["{ddd}", "{mare}", "{settings}"]),
    ("kh2_fonts.py", ["{kh2}"]),
    ("kh2_gauge.py", ["{kh2_gauge}", "--settings", "{gauge_settings}"]),
    ("heart_points.py", ["--settings", "{gauge_settings}"]),
    ("minimap_icons.py", ["--settings", "{minimap_settings}", "--kh2", "{kh2}"]),
]
HD_MENU_SUPPORT = (
    "port/tools/kh2_center_gauge.py",
    "port/tools/kh2_add_limit_gauge.py",
    "port/tools/kh2_add_limit_marker.py",
    "port/tools/kh2_add_party_gauges.py",
    "docs/examples/kh2-gauge.json",
    "port/tools/heart_model_export.cpp",
)
HD_VIDEO_TOOLS = [
    ("theater_movies.py", ["{mare}", "--movies", "{movies}", "--videos", "--subtitles", "--music"]),
]
# theater_movies.py's scene map (which of the theater's scene videos the port plays), beside the tools
# in the repository's layout, so the videos it names are made too
SCENE_MAP = "port/movies/hd_scenes.txt"
HD_MOD = "HD PACK"
HD_ERRORS = "hd-errors.log"  # under setup/: what each HD tool that failed said
HAND = "menu/fm/title.2ld"  # kh2_first's entry whose remastered sheet newgame_dialog takes the glove from
HAND_ASSET = "UK_title_2ld0.png"
INPUTS_FILE = HERE / "hd_inputs.txt"

# Run by the trace: the tool under an audit hook writing every file opened and folder listed.
_TRACER = r"""
import os, runpy, sys
log = open(os.environ["KHDAYS_TRACE_LOG"], "a", encoding="utf-8")
def hook(event, args):
    if event == "open" and isinstance(args[0], (str, bytes, os.PathLike)):
        log.write("open\t" + os.fsdecode(args[0]) + "\n")
    elif event in ("os.scandir", "os.listdir") and args and args[0] is not None:
        log.write("list\t" + os.fsdecode(args[0]) + "\n")
sys.addaudithook(hook)
tool = sys.argv[1]
sys.argv = sys.argv[1:]
runpy.run_path(tool, run_name="__main__")
"""


def run_hd_tools(
    runs: list[tuple[str, list[str]]],
    line_of: Callable[[str, list[str]], list[str]],
    cwd: Path,
    log: Path,
    language: str,
    watch: Path | None = None,
) -> list[dict[str, Any]]:
    """Each HD tool run in turn. One that fails leaves its screens with the DS's look and the others
    are made all the same (a copy changed by a mod may lack what one of them reads); what each failed
    one said is kept whole in `log`, which is removed when none fails. Each tool, made or not, and for
    one made the files under `watch` it wrote."""
    import subprocess

    pieces: list[dict[str, Any]] = []
    errors: list[str] = []
    for i, (tool, template) in enumerate(runs):
        progress(i, len(runs), words("hd-making", language, what=tool))
        line = line_of(tool, template)
        before = files_in(watch) if watch else {}
        done = subprocess.run(line, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if done.returncode != 0:
            said = done.stderr.strip()
            last = said.splitlines()[-1] if said else f"exit status {done.returncode}"
            errors.append(f"== {tool} ({' '.join(line[1:])})" + chr(10) + (said or last) + chr(10))
            pieces.append({"tool": tool, "made": False, "error": last})
            continue
        piece = {"tool": tool, "made": True}
        if watch:
            piece["files"] = sorted(f for f, stamp in files_in(watch).items() if before.get(f) != stamp)
        pieces.append(piece)
    log.unlink(missing_ok=True)
    if errors:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_bytes(chr(10).join(errors).encode("utf-8"))
    return pieces


def files_in(folder: Path) -> dict[str, tuple[int, int]]:
    """Each file under `folder` by its path there, with its size and when it was written."""
    if not folder.is_dir():
        return {}
    found = {}
    for path in folder.rglob("*"):
        if path.is_file():
            stat = path.stat()
            found[path.relative_to(folder).as_posix()] = (stat.st_size, stat.st_mtime_ns)
    return found


def link_videos(installed: Path, work: Path) -> None:
    """The videos already installed, linked into the work folder (not copied): theater_movies.py keeps
    a video that is there, so it makes only the missing ones. On a disk without links, all are made."""
    work.mkdir(parents=True, exist_ok=True)
    for video in installed.glob("*.mp4"):
        try:
            os.link(video, work / video.name)
        except OSError:
            return


def tool_arguments(template: list[str], places: dict[str, Path]) -> list[str]:
    return [str(places[a[1:-1]]) if a.startswith("{") and a.endswith("}") else a for a in template]


def tools_digest(repository: Path) -> str:
    """What the trace depends on: the HD tools and the scene map, as they are in `repository`."""
    parts = [(repository / "tools" / "hd" / t).read_bytes() for t, _ in HD_MENU_TOOLS + HD_VIDEO_TOOLS]
    parts += [(repository / rel).read_bytes() for rel in HD_MENU_SUPPORT]
    return hashlib.sha256(b"".join(parts + [(repository / SCENE_MAP).read_bytes()])).hexdigest()


def _inputs_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("--mare", type=Path, required=True, help="<1.5+2.5>/Image/Mare.hed_out/original (an extraction)")
    p.add_argument("--settings", type=Path, required=True, help="<1.5+2.5>/Image/SettingMenu.hed_out/original")
    p.add_argument("--ddd", type=Path, required=True, help="<2.8>/Image/dt (or en)/kh3d_first.hed_out")
    p.add_argument("--kh2", type=Path, required=True, help="<1.5+2.5>/Image/dt (or en)/kh2_first.hed_out")
    p.add_argument("--kh2-gauge", type=Path, help="kh2_fifth.hed_out (default: beside --kh2)")
    p.add_argument("--movies", type=Path, required=True, help="<1.5+2.5>/STEAM/Mare/MOVIE/Days/dt (or EPIC/.../en)")
    p.add_argument("--data", type=Path, default=HERE.parent / "data" / "extracted", help="the game's extracted data")
    p.add_argument(
        "--native",
        type=Path,
        default=HERE.parent / "build" / "port" / "obj-release-x64" / "khdays-native.exe",
        help="a built khdays-native.exe (heart_points.py converts the heart's model with it)",
    )
    p.add_argument("--out", type=Path, default=INPUTS_FILE)


@command(
    "trace-inputs", "developers: which collection files the HD tools read, traced into hd_inputs.txt", _inputs_arguments
)
def trace_inputs(args: argparse.Namespace) -> dict[str, Any]:
    import subprocess
    import tempfile

    repository = HERE.parent
    hand = args.kh2 / "remastered" / HAND / HAND_ASSET
    places = {
        "mare": args.mare,
        "settings": args.settings,
        "ddd": args.ddd,
        "kh2": args.kh2,
        "hand": hand,
        "movies": args.movies,
    }
    kh2_gauge = args.kh2_gauge or args.kh2.with_name("kh2_fifth.hed_out")
    places["kh2_gauge"] = kh2_gauge
    places["gauge_settings"] = Path("no-existing-gauge-settings")
    places["minimap_settings"] = Path("no-existing-minimap-settings")
    # the archives the sources are extractions of, and how a file there names its entry
    sources = [
        ("Mare", args.mare, "original"),
        ("SettingMenu", args.settings, "original"),
        ("kh3d_first", args.ddd / "original", "original"),
        ("kh3d_first", args.ddd / "remastered", "remastered"),
        ("kh2_first", args.kh2 / "original", "original"),
        ("kh2_first", args.kh2 / "remastered", "remastered"),
        ("kh2_fifth", kh2_gauge / "original", "original"),
        ("kh2_fifth", kh2_gauge / "remastered", "remastered"),
        ("movies", args.movies, "plain"),
    ]
    entries: dict[str, set[str]] = {}
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        # the tools in the repository's layout, the game's data beside them (read only: a junction to
        # data/extracted alone, so nothing they write reaches the repository's data or mods)
        copied = [f"tools/hd/{t}" for t, _ in HD_MENU_TOOLS + HD_VIDEO_TOOLS] + [
            "tools/decompress_data.py",
            SCENE_MAP,
            *HD_MENU_SUPPORT,
        ]
        for rel in copied:
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(repository / rel, root / rel)
        (root / "data").mkdir()
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(root / "data" / "extracted"), str(args.data.resolve())],
            check=True,
            capture_output=True,
        )
        log = root / "trace.log"
        try:
            for i, (tool, template) in enumerate(HD_MENU_TOOLS + HD_VIDEO_TOOLS):
                progress(i, len(HD_MENU_TOOLS) + len(HD_VIDEO_TOOLS), tool)
                command_line = [
                    sys.executable,
                    "-c",
                    _TRACER,
                    str(root / "tools" / "hd" / tool),
                    *tool_arguments(template, places),
                ]
                if (tool, template) in HD_MENU_TOOLS:
                    command_line += ["--mod", "trace"]
                # as build-hd runs them: the extraction, and the executable the heart's model is converted with
                if tool in DATA_TOOLS:
                    command_line += ["--data", str(data_folder(root))]
                if tool == "heart_points.py":
                    command_line += ["--native", str(args.native.resolve())]
                subprocess.run(command_line, cwd=root, check=True, env={**os.environ, "KHDAYS_TRACE_LOG": str(log)})
            for line in log.read_text(encoding="utf-8").splitlines():
                kind, _, raw = line.partition("\t")
                path = Path(raw).resolve()
                for archive, folder, layout in sources:
                    folder = folder.resolve()
                    if path != folder and folder not in path.parents:
                        continue
                    files = (
                        [path]
                        if kind == "open"
                        else [p for p in path.rglob("*") if p.is_file()]
                        if path.is_dir()
                        else []
                    )
                    for f in files:
                        rel = f.relative_to(folder).as_posix()
                        if layout == "remastered":
                            rel = rel.rsplit("/", 1)[0]  # the asset's entry (its name follows it)
                            while rel and not (folder.parent / "original" / rel).is_file():
                                rel = rel.rsplit("/", 1)[0] if "/" in rel else ""
                            if not rel:
                                continue
                        entries.setdefault(archive, set()).add(rel)
                    break
        finally:
            subprocess.run(["cmd", "/c", "rmdir", str(root / "data" / "extracted")], capture_output=True)
    lines = [
        "# The collection files the HD tools read (installer/setup_steps.py trace-inputs):",
        "# '<archive> <entry>' (its data and remastered assets) or 'movies <file>'.",
        f"# tools {tools_digest(repository)}",
    ]
    for archive in sorted(entries):
        lines += [f"{archive} {rel}" for rel in sorted(entries[archive])]
    args.out.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    return {"inputs": sum(len(v) for v in entries.values()), "out": str(args.out)}


# The collections' places and what each HD choice needs. Steam's copies keep their language's archives
# in Image/dt and Days's videos in STEAM/Mare/MOVIE/Days/dt; the Epic Games Store's keep the archives in
# Image/en and the videos in EPIC/Mare/MOVIE/Days/en (seen in a copy of its own); the
# videos are found where the collection has them.
KH2_EXE = "KINGDOM HEARTS II FINAL MIX.exe"
COLLECTION_ARCHIVES = {
    "1.5+2.5": {
        "Mare": "Image/Mare",
        "SettingMenu": "Image/SettingMenu",
        "kh2_first": "Image/dt/kh2_first",
        "kh2_fifth": "Image/dt/kh2_fifth",
        "kh2_sixth": "Image/dt/kh2_sixth",
        # (the port's own mods': mod_inputs.txt)
        "bbs_first": "Image/dt/bbs_first",
        "bbs_second": "Image/dt/bbs_second",
        "Recom": "Image/dt/Recom",
    },
    "2.8": {"kh3d_first": "Image/dt/kh3d_first"},
}
MOVIES = "STEAM/Mare/MOVIE/Days/dt"
LANGUAGE_FOLDERS = ("dt", "en")


def archive_base(folder: Path, kind: str, archive: str) -> Path:
    """Where `archive` of collection `kind` is in `folder` (its .hed and .pkg beside this name): its
    language folder the one the collection has."""
    rel = COLLECTION_ARCHIVES[kind][archive]
    for language in LANGUAGE_FOLDERS if "/dt/" in rel else ():
        base = folder / rel.replace("/dt/", f"/{language}/")
        if base.with_name(base.name + ".hed").is_file():
            return base
    return folder / rel


def movies_folder(folder: Path) -> Path:
    """Days's videos in the 1.5+2.5 collection `folder`: Steam's STEAM/Mare/MOVIE/Days/dt, else the
    folder in the same place of the collection that holds them (*/Mare/MOVIE/Days/*)."""
    if (folder / MOVIES).is_dir():
        return folder / MOVIES
    first = hd_inputs().get("movies", [])
    for candidate in sorted(folder.glob("*/Mare/MOVIE/Days/*")):
        if candidate.is_dir() and (not first or (candidate / first[0]).is_file()):
            return candidate
    return folder / MOVIES


DATA_TOOLS = {
    "theater_mission.py",
    "theater_cine.py",
    "save_screen.py",
    "ddd_camp.py",
    "kh2_fonts.py",
    "minimap_icons.py",
    "heart_points.py",
}  # take --data

MESSAGES.update(
    {
        "hd-missing": {
            "en": "This folder lacks files the HD pieces need: {missing}",
            "es": "A esta carpeta le faltan archivos que necesitan las piezas HD: {missing}",
            "fr": "Il manque à ce dossier des fichiers dont les éléments HD ont besoin : {missing}",
            "de": "In diesem Ordner fehlen Dateien, die die HD-Teile brauchen: {missing}",
            "it": "In questa cartella mancano file che servono ai pezzi HD: {missing}",
        },
        "hd-needs-both": {
            "en": "The HD menus and HUD need both collections.",
            "es": "Los menús y el HUD HD necesitan las dos colecciones.",
            "fr": "Les menus et l'interface HD ont besoin des deux collections.",
            "de": "Die HD-Menüs und das HD-HUD brauchen beide Sammlungen.",
            "it": "I menu e l'HUD HD richiedono entrambe le raccolte.",
        },
        "hd-needs-1525": {
            "en": "The HD videos need KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
            "es": "Los vídeos HD necesitan KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
            "fr": "Les vidéos HD ont besoin de KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
            "de": "Die HD-Videos brauchen KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
            "it": "I video HD richiedono KINGDOM HEARTS HD 1.5+2.5 ReMIX.",
        },
        "hd-making": {
            "en": "Making the HD pieces: {what}",
            "es": "Haciendo las piezas HD: {what}",
            "fr": "Création des éléments HD : {what}",
            "de": "Die HD-Teile werden erstellt: {what}",
            "it": "Creazione dei pezzi HD: {what}",
        },
        "hd-ready": {
            "en": "The HD pieces are ready.",
            "es": "Las piezas HD están listas.",
            "fr": "Les éléments HD sont prêts.",
            "de": "Die HD-Teile sind fertig.",
            "it": "I pezzi HD sono pronti.",
        },
        "hd-partial": {
            "en": "The HD pieces are ready, but for those of {tools}: those screens keep the DS's look (your copy "
            "may be changed by a mod). What went wrong is in {log}; please attach it to a report.",
            "es": "Las piezas HD están listas, menos las de {tools}: esas pantallas se quedan con el aspecto de la "
            "DS (puede que un mod haya cambiado tu copia). Lo que falló está en {log}; adjúntalo si lo reportas.",
            "fr": "Les éléments HD sont prêts, sauf ceux de {tools} : ces écrans gardent l'aspect de la DS (un mod a "
            "peut-être changé votre copie). Ce qui a échoué est dans {log} ; joignez-le à un rapport.",
            "de": "Die HD-Teile sind fertig, außer denen von {tools}: Diese Bildschirme behalten das Aussehen der DS "
            "(vielleicht hat ein Mod deine Kopie verändert). Was schiefging, steht in {log}; häng es an einen Bericht.",
            "it": "I pezzi HD sono pronti, tranne quelli di {tools}: quelle schermate restano con l'aspetto del DS "
            "(forse una mod ha cambiato la tua copia). Cosa è andato storto è in {log}; allegalo a una segnalazione.",
        },
    }
)


def hd_inputs() -> dict[str, list[str]]:
    """hd_inputs.txt: archive -> its entries (or movies -> its files)."""
    out: dict[str, list[str]] = {}
    for line in INPUTS_FILE.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            archive, _, path = line.partition(" ")
            out.setdefault(archive, []).append(path)
    return out


def check_folder(kind: str, folder: Path) -> list[str]:
    """What `folder` lacks of what the HD pieces read from collection `kind` (empty: all there)."""
    import collection_archive

    inputs = hd_inputs()
    for archive, paths in mod_inputs().items():  # the port's own mods' too
        inputs.setdefault(archive, []).extend(paths)
    missing = []
    for archive, rel in COLLECTION_ARCHIVES[kind].items():
        base = archive_base(folder, kind, archive)
        if not (base.with_name(base.name + ".hed").is_file() and base.with_name(base.name + ".pkg").is_file()):
            missing.append(rel + ".hed/.pkg")
            continue
        index = collection_archive.Archive(base, None)
        try:
            absent = [p for p in inputs.get(archive, []) if not index.has(p)]
        finally:
            index.close()
        if absent:
            missing.append(f"{rel}: {len(absent)} entries ({absent[0]}...)")
    if kind == "1.5+2.5":
        movies = movies_folder(folder)
        absent = [m for m in inputs.get("movies", []) if not (movies / m).is_file()]
        if absent:
            missing.append(f"{movies.relative_to(folder).as_posix()}: {len(absent)} files ({absent[0]}...)")
        try:
            collection_archive.load_table(folder / KH2_EXE)
        except (OSError, collection_archive.ArchiveError):
            missing.append(KH2_EXE)
    return missing


@command(
    "find-collections",
    "the KINGDOM HEARTS collections Steam or the Epic Games Store has on this computer, each checked",
    lambda p: None,
)
def find_collections(args: argparse.Namespace) -> dict[str, Any]:
    import epic_library
    import steam_library

    steam = steam_library.steam_folder()
    found = []
    for kind, app, marker in (
        ("1.5+2.5", steam_library.APP_1525, epic_library.MARKER_1525),
        ("2.8", steam_library.APP_28, epic_library.MARKER_28),
    ):
        # Steam's copy first, the Epic Games Store's when Steam has none that is whole
        folder = steam_library.app_folder(app, steam) if steam is not None else None
        missing = check_folder(kind, folder) if folder is not None else None
        if folder is None or missing:
            epic = epic_library.collection_folder(marker)
            epic_missing = check_folder(kind, epic) if epic is not None else None
            if epic is not None and (folder is None or not epic_missing):
                folder, missing = epic, epic_missing
        found.append(
            {
                "kind": kind,
                "folder": str(folder) if folder else None,
                "valid": folder is not None and not missing,
                "missing": missing or [],
            }
        )
    flat = {}  # the same, one field each, for the installer's script
    for c, key in zip(found, ("1525", "28")):
        flat[f"folder_{key}"] = c["folder"] if c["valid"] else None
    return {"steam": str(steam) if steam else None, "collections": found, **flat}


@command(
    "check-collection",
    "whether a folder holds what the HD pieces read from that collection (exit 1 when not)",
    lambda p: (p.add_argument("kind", choices=sorted(COLLECTION_ARCHIVES)), p.add_argument("folder", type=Path)),
)
def check_collection(args: argparse.Namespace) -> dict[str, Any]:
    missing = check_folder(args.kind, args.folder)
    if missing:
        raise Refusal("hd-missing", missing="; ".join(missing))
    return {"valid": True, "missing": []}


def data_folder(install: Path) -> Path:
    for folder in sorted(extracted_root(install).glob("*")):
        if folder.suffix != ".partial" and (folder / "nitrofs").is_dir():
            return folder
    raise Refusal("data-missing")


MESSAGES["data-missing"] = {
    "en": "The game's data is not prepared yet.",
    "es": "Los datos del juego aún no están preparados.",
    "fr": "Les données du jeu ne sont pas encore préparées.",
    "de": "Die Daten des Spiels sind noch nicht vorbereitet.",
    "it": "I dati del gioco non sono ancora pronti.",
}


def _build_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("install", type=Path)
    p.add_argument("--menus", action="store_true", help="the HD menus and HUD (mods/HD PACK): both collections")
    p.add_argument("--videos", action="store_true", help="the theater's videos and subtitles: 1.5+2.5")
    p.add_argument("--1525", dest="c1525", type=Path, help="the KINGDOM HEARTS HD 1.5+2.5 ReMIX folder")
    p.add_argument("--28", dest="c28", type=Path, help="the KINGDOM HEARTS HD 2.8 Final Chapter Prologue folder")


@command("build-hd", "the HD pieces made from the player's collections", _build_arguments)
def build_hd(args: argparse.Namespace) -> dict[str, Any]:
    import collection_archive

    install = args.install.resolve()
    if args.menus and (args.c1525 is None or args.c28 is None):
        raise Refusal("hd-needs-both")
    if args.videos and args.c1525 is None:
        raise Refusal("hd-needs-1525")
    # an update leaves the pieces already made from the same collections by the same tools (their folder
    # deleted: made again)
    made_before = load_state(install)
    menu_inputs, video_inputs = {"1.5+2.5": str(args.c1525), "2.8": str(args.c28)}, {"1.5+2.5": str(args.c1525)}
    mod, cutscenes = install / "mods" / HD_MOD, install / "data" / "cutscenes"
    menus = menu_tools_to_make(made_before, menu_inputs, mod) if args.menus else []
    videos = args.videos and not hd_made(made_before, "hd-videos", video_inputs, cutscenes)
    if not menus and not videos:
        return {"pieces": [], "skipped": True, "message": words("hd-ready", args.language)}
    for kind, folder in (("1.5+2.5", args.c1525), ("2.8", args.c28)):
        if folder is not None and (menus or kind == "1.5+2.5"):
            missing = check_folder(kind, folder)
            if missing:
                raise Refusal("hd-missing", missing="; ".join(missing))
    if menus:
        unmerge_port_mods(install)  # the menus kept are copied as their tools made them (build-mods puts them in again)
    data = data_folder(install)
    inputs = hd_inputs()
    work = install / "setup" / "hd-work"
    remove(work)
    root, src = work / "root", work / "src"
    pieces = []
    try:
        # the tools in the repository's layout under a root of their own: their outputs land there
        tools = tool_folders()
        for tool, _ in HD_MENU_TOOLS + HD_VIDEO_TOOLS:
            (root / "tools" / "hd").mkdir(parents=True, exist_ok=True)
            shutil.copy2(tools["hd"] / tool, root / "tools" / "hd" / tool)
        for rel in HD_MENU_SUPPORT:
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(HERE.parent / rel, root / rel)
        shutil.copy2(tools["rom"] / "decompress_data.py", root / "tools")
        (root / SCENE_MAP).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(HERE.parent / SCENE_MAP, root / SCENE_MAP)
        places = {
            "movies": movies_folder(args.c1525) if args.c1525 else Path(),
            "mare": src / "Mare.hed_out" / "original",
            "settings": src / "SettingMenu.hed_out" / "original",
            "ddd": src / "kh3d_first.hed_out",
            "kh2": src / "kh2_first.hed_out",
            "hand": src / "kh2_first.hed_out" / "remastered" / HAND / HAND_ASSET,
            "kh2_gauge": src / "kh2_fifth.hed_out",
            "gauge_settings": install / "mods" / HD_MOD / "layouts" / "gauge",
            "minimap_settings": install / "mods" / HD_MOD / "layouts" / "minimap",
        }
        runs = [(t, template) for t, template in HD_MENU_TOOLS if t in menus] + (HD_VIDEO_TOOLS if videos else [])
        # the collections' files these tools read (their arguments' archives), made from their archives
        read = {places[a[1:-1]] for _, template in runs for a in template if a[1:-1] in places}
        wanted = sorted({p.relative_to(src).parts[0].removesuffix(".hed_out") for p in read if p.is_relative_to(src)})
        cipher = collection_archive.Cipher(collection_archive.load_table(args.c1525 / KH2_EXE)) if menus else None
        for archive in wanted:
            kind = "1.5+2.5" if archive in COLLECTION_ARCHIVES["1.5+2.5"] else "2.8"
            progress(0, 1, words("hd-making", args.language, what=archive))
            folder = args.c1525 if kind == "1.5+2.5" else args.c28
            with collection_archive.Archive(archive_base(folder, kind, archive), cipher) as a:
                collection_archive.materialise(a, inputs.get(archive, []), src / f"{archive}.hed_out")
        # what stays as it was made: the files of the menu tools not run again, copied, and the videos,
        # linked (each the theater's own, unscrambled: the same from the same collection)
        recorded = (made_before.get("steps", {}).get("hd-menus", {}).get("tools") or {}) if menus else {}
        kept = {t: r for t, r in recorded.items() if t not in menus and t in dict(HD_MENU_TOOLS)}
        for record in kept.values():
            for f in record["files"]:
                (root / "mods" / HD_MOD / f).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(mod / f, root / "mods" / HD_MOD / f)
        if videos and same_inputs(made_before.get("steps", {}).get("hd-videos", {}), video_inputs):
            link_videos(cutscenes / "cinematics", root / "data" / "cutscenes" / "cinematics")

        def line_of(tool: str, template: list[str]) -> list[str]:
            # (the installed Python is isolated by its ._pth file; -I would hide a developer's own packages)
            line = [sys.executable, str(root / "tools" / "hd" / tool), *tool_arguments(template, places)]
            if (tool, template) in HD_MENU_TOOLS:
                line += ["--mod", HD_MOD]
            if tool in DATA_TOOLS:
                line += ["--data", str(data)]
            if tool == "heart_points.py":
                # This tool runs below setup/hd-work, away from the installed exporter.
                line += ["--native", str(install / "khdays-native.exe")]
            return line

        log = install / "setup" / HD_ERRORS
        pieces = run_hd_tools(runs, line_of, root, log, args.language, watch=root / "mods" / HD_MOD)
        if not any(p["made"] for p in pieces):
            first = pieces[0]
            raise RuntimeError(f"{first['tool']}: {first['error']} ({log})")
        made = {p["tool"]: p for p in pieces if p["made"]}
        # whole, into their places: the menus when any was made (one that was not keeps the DS's look,
        # unrecorded, so the next run tries it again), the videos when all were
        state = load_state(install)
        if any(tool in made for tool in menus):
            put_in_place(root / "mods" / HD_MOD, mod)
            tools = kept | {
                t: {"by": tool_made_by(t, template), "files": made[t]["files"]}
                for t, template in HD_MENU_TOOLS
                if t in made
            }
            complete = len(tools) == len(HD_MENU_TOOLS)
            state.setdefault("steps", {})["hd-menus"] = {"complete": complete, "inputs": menu_inputs, "tools": tools}
            save_state(install, state)  # kept even when the videos fail next
        if videos and all(tool in made for tool, _ in HD_VIDEO_TOOLS):
            put_in_place(root / "data" / "cutscenes", cutscenes)
            mark_step(state, "hd-videos", video_inputs, made_by=made_by("hd-videos"))
        save_state(install, state)
    finally:
        remove(work)
    progress(1, 1, words("hd-ready", args.language))
    missing = [p["tool"] for p in pieces if not p["made"]]
    if missing:
        log = install / "setup" / HD_ERRORS
        message = words("hd-partial", args.language, tools=", ".join(missing), log=str(log))
        return {"pieces": pieces, "missing": ", ".join(missing), "message": message}
    return {"pieces": pieces, "message": words("hd-ready", args.language)}


@command(
    "compare-hd",
    "developers: two folders of HD pieces compared (pictures by their pixels, the rest by bytes)",
    lambda p: (p.add_argument("made", type=Path), p.add_argument("reference", type=Path)),
)
def compare_hd(args: argparse.Namespace) -> dict[str, Any]:
    from PIL import Image

    made = {p.relative_to(args.made).as_posix(): p for p in args.made.rglob("*") if p.is_file()}
    reference = {p.relative_to(args.reference).as_posix(): p for p in args.reference.rglob("*") if p.is_file()}
    differences = []
    for rel in sorted(set(made) | set(reference)):
        if rel not in made or rel not in reference:
            differences.append({"file": rel, "why": "only in the made pieces" if rel in made else "missing"})
            continue
        a, b = made[rel], reference[rel]
        if a.read_bytes() == b.read_bytes():
            continue
        if a.suffix.lower() == ".png":
            with Image.open(a) as x, Image.open(b) as y:
                if x.size == y.size and x.convert("RGBA").tobytes() == y.convert("RGBA").tobytes():
                    continue
        differences.append({"file": rel, "why": "different"})
    return {"equal": not differences, "files": len(reference), "differences": differences}


# ---- The mods of the port's own features -------------------------------------------------------

# The mods that switch on features of the port's own (port/runtime/mods/installed.h), each made by its tool in
# tools/mods: the folder it makes and the tool's command line, its places filled in ({data} the game's
# extraction; the Menu Overhaul's collections' entries as installer/mod_inputs.txt lists them, extracted as the HD
# pieces' are, the HD menus' pieces made before it, the fonts the installer carries, the port). Both are put inside
# the HD PACK, one mod for the player: their files over the HD menus' (the pieces they replace kept apart in
# setup/hd-originals and put back before the HD menus or these mods are made again, each made from the HD tools'
# own pieces), the rest added; the install's state ("port-mods") says which are which.
PORT_MODS = {
    "walk": ("Days - Walk", ["walk_clips.py", "--data", "{data}"]),
    "kh2-prizes": (
        "Days - KH2 Prizes",
        ["kh2_prizes.py", "--kh2", "{kh2}", "--exe", "{exe}", "--bbs2", "{bbs2}", "--recom", "{recom}"],
    ),
    "menu-overhaul": (
        "Days - Menu Overhaul",
        [
            "menu_overhaul.py",
            *("--kh2", "{kh2}", "--bbs", "{bbs}", "--recom", "{recom}", "--mare", "{mare}"),
            *("--hd", "{hd}", "--fonts", "{fonts}", "--data", "{data}", "--exe", "{exe}"),
        ],
    ),
}
MOD_INPUTS_FILE = HERE / "mod_inputs.txt"
# the mods made from the 1.5+2.5 collection (installer/mod_inputs.txt's entries, extracted for them)
COLLECTION_MODS = ("menu-overhaul", "kh2-prizes")
HD_ORIGINALS = "hd-originals"  # (under setup/) the HD menus' pieces the port's own mods replace in the HD PACK
# where each archive's entries are extracted for the mods' tools (KH2's two archives into one folder)
MOD_SOURCES = {
    "kh2_first": "kh2",
    "kh2_fifth": "kh2",
    "kh2_sixth": "kh2",
    "bbs_first": "bbs_first",
    "bbs_second": "bbs_second",
    "Recom": "Recom",
    "Mare": "Mare",
}
FONTS = HERE / "fonts"  # the fonts the installer takes from its pinned packages (build_installer.py)
MESSAGES.update(
    {
        "mod-making": {
            "en": "Making the mod {what}",
            "es": "Haciendo el mod {what}",
            "fr": "Création du mod {what}",
            "de": "Der Mod {what} wird erstellt",
            "it": "Creazione della mod {what}",
        },
        "mod-ready": {
            "en": "The mods are ready.",
            "es": "Los mods están listos.",
            "fr": "Les mods sont prêts.",
            "de": "Die Mods sind fertig.",
            "it": "Le mod sono pronte.",
        },
        "mod-needs-1525": {
            "en": "KH2's prizes and chests need the 1.5+2.5 collection.",
            "es": "Los premios y cofres de KH2 necesitan la colección 1.5+2.5.",
            "fr": "Les prix et coffres de KH2 ont besoin de la collection 1.5+2.5.",
            "de": "Die Preise und Truhen aus KH2 brauchen die Sammlung 1.5+2.5.",
            "it": "I premi e i forzieri di KH2 richiedono la raccolta 1.5+2.5.",
        },
        "mod-needs-hd": {
            "en": "The Menu Overhaul needs the 1.5+2.5 collection and the HD menus made first.",
            "es": "El Menu Overhaul necesita la colección 1.5+2.5 y los menús HD hechos antes.",
            "fr": "Le Menu Overhaul a besoin de la collection 1.5+2.5 et des menus HD faits avant.",
            "de": "Das Menu Overhaul braucht die Sammlung 1.5+2.5 und zuerst die HD-Menüs.",
            "it": "Il Menu Overhaul richiede la raccolta 1.5+2.5 e i menu HD fatti prima.",
        },
    }
)


def mod_inputs() -> dict[str, list[str]]:
    """mod_inputs.txt: archive -> its entries (the mods' tools', as hd_inputs.txt is the HD tools')."""
    out: dict[str, list[str]] = {}
    for line in MOD_INPUTS_FILE.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            archive, _, path = line.partition(" ")
            out.setdefault(archive, []).append(path)
    return out


def mods_digest(repository: Path) -> str:
    """What mod_inputs.txt's trace depends on: the mods' tools and pictures, as they are in `repository`."""
    folder = repository / "tools" / "mods"
    files = sorted(p for p in folder.rglob("*") if p.is_file() and "tests" not in p.relative_to(folder).parts)
    h = hashlib.sha256()
    for p in files:
        if "__pycache__" not in p.parts:
            h.update(p.relative_to(folder).as_posix().encode() + b"\0" + p.read_bytes())
    return h.hexdigest()


def mod_places(install: Path, src: Path, fonts: Path = FONTS) -> dict[str, str]:
    """The mods' tools' places ({kh2}, {data}...) in an install, the collections' entries extracted under `src`."""
    return {
        "kh2": str(src / "kh2.hed_out"),
        "bbs": str(src / "bbs_first.hed_out"),
        "bbs2": str(src / "bbs_second.hed_out"),
        "recom": str(src / "Recom.hed_out"),
        "mare": str(src / "Mare.hed_out" / "original"),
        "hd": str(install / "mods" / HD_MOD),
        "fonts": str(fonts),
        "data": str(data_folder(install)),
        "exe": str(install / "khdays-native.exe"),
    }


def _mods_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("install", type=Path)
    p.add_argument("--walk", action="store_true", help="the Walk (docs/WALK_MOD.md): the game's data")
    p.add_argument("--menu-overhaul", action="store_true", help="the Menu Overhaul: 1.5+2.5, the HD menus")
    p.add_argument("--kh2-prizes", action="store_true", help="KH2's prizes and chests: 1.5+2.5")
    p.add_argument("--1525", dest="c1525", type=Path, help="the KINGDOM HEARTS HD 1.5+2.5 ReMIX folder")


def unmerge_port_mods(install: Path) -> None:
    """The HD PACK as the HD tools made it: the port's own mods' files taken out of it, the HD pieces they replaced
    put back (none when a merge stopped before moving them: the HD PACK's own are still there)."""
    state = load_state(install)
    record = state.get("steps", {}).get("port-mods")
    if not record:
        return
    mod, originals = install / "mods" / HD_MOD, install / "setup" / HD_ORIGINALS
    for rel in record["added"]:
        remove(mod / rel)
    for rel in record["replaced"]:
        if (originals / rel).is_file():
            (mod / rel).parent.mkdir(parents=True, exist_ok=True)
            os.replace(originals / rel, mod / rel)
    # the folders the added files leave empty
    for folder in sorted({(mod / rel).parent for rel in record["added"]}, key=lambda p: -len(p.parts)):
        while folder != mod and folder.is_dir() and not any(folder.iterdir()):
            folder.rmdir()
            folder = folder.parent
    remove(originals)
    del state["steps"]["port-mods"]
    save_state(install, state)


def merge_port_mods(install: Path, made: list[Path], record: dict[str, Any]) -> None:
    """The port's own mods made (each one's folder) put inside the HD PACK, the HD pieces they replace kept apart;
    `record` kept with which files were added and which replaced (first: a merge stopped half way is taken out
    whole by unmerge_port_mods)."""
    mod, originals = install / "mods" / HD_MOD, install / "setup" / HD_ORIGINALS
    files: dict[str, Path] = {}
    for folder in made:
        for f in sorted(p for p in folder.rglob("*") if p.is_file()):
            files[f.relative_to(folder).as_posix()] = f
    replaced = sorted(rel for rel in files if (mod / rel).is_file())
    state = load_state(install)
    state.setdefault("steps", {})["port-mods"] = {
        **record,
        "replaced": replaced,
        "added": sorted(set(files) - set(replaced)),
    }
    save_state(install, state)
    for rel in replaced:
        (originals / rel).parent.mkdir(parents=True, exist_ok=True)
        os.replace(mod / rel, originals / rel)
    for rel, source in files.items():
        (mod / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, mod / rel)


@command("build-mods", "the port's own mods (docs/WALK_MOD.md, docs/MENU_OVERHAUL.md)", _mods_arguments)
def build_mods(args: argparse.Namespace) -> dict[str, Any]:
    import subprocess

    import collection_archive

    install = args.install.resolve()
    wanted = [m for m in PORT_MODS if getattr(args, m.replace("-", "_"))]
    # made again from the HD tools' own pieces, then put in the HD PACK again (only those chosen now)
    unmerge_port_mods(install)
    hd = install / "mods" / HD_MOD
    if "kh2-prizes" in wanted and args.c1525 is None:
        raise Refusal("mod-needs-1525")
    if "menu-overhaul" in wanted and (args.c1525 is None or not any(p.is_file() for p in hd.rglob("*"))):
        raise Refusal("mod-needs-hd")
    for folder, _ in PORT_MODS.values():
        remove(install / "mods" / folder)  # where an earlier installer put each, apart from the HD PACK
    made: list[str] = []
    failed = None
    work = install / "setup" / "mod-work"
    remove(work)
    try:
        # the tools in the repository's layout under a root of their own (tools/hd's helpers too): the mods land
        # there, then inside the HD PACK
        tools = tool_folders()
        shutil.copytree(tools["mods"], work / "tools" / "mods", ignore=shutil.ignore_patterns("tests"))
        shutil.copytree(tools["hd"], work / "tools" / "hd", ignore=shutil.ignore_patterns("tests"))
        shutil.copy2(tools["rom"] / "decompress_data.py", work / "tools")
        places = mod_places(install, work / "src")
        if set(COLLECTION_MODS) & set(wanted):
            # the collections' entries the tools read, extracted as the HD pieces' are
            cipher = collection_archive.Cipher(collection_archive.load_table(args.c1525 / KH2_EXE))
            for archive, paths in mod_inputs().items():  # (all of 1.5+2.5: the HD menus' are 2.8's)
                with collection_archive.Archive(archive_base(args.c1525, "1.5+2.5", archive), cipher) as a:
                    collection_archive.materialise(a, paths, work / "src" / f"{MOD_SOURCES[archive]}.hed_out")
        for i, mod in enumerate(wanted):
            folder, template = PORT_MODS[mod]
            progress(i, len(wanted), words("mod-making", args.language, what=folder))
            line = [sys.executable, str(work / "tools" / "mods" / template[0])]
            line += [places[a[1:-1]] if a.startswith("{") else a for a in template[1:]] + ["--mod", folder]
            done = subprocess.run(line, cwd=work, capture_output=True, text=True, encoding="utf-8", errors="replace")
            if done.returncode != 0:
                said = done.stderr.strip().splitlines()
                failed = failed or RuntimeError(f"{template[0]}: {said[-1] if said else done.returncode}")
                continue  # the others made still go in
            made.append(mod)
        merge_port_mods(install, [work / "mods" / PORT_MODS[m][0] for m in made], {"mods": made})
    finally:
        remove(work)
    if failed is not None:
        raise failed
    progress(1, 1, words("mod-ready", args.language))
    return {"mods": made, "into": HD_MOD, "message": words("mod-ready", args.language)}


def _mod_trace_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("install", type=Path, help="an install with the game's data, the HD menus and the port")
    p.add_argument("--src", type=Path, required=True, help="the collections' entries extracted (<name>.hed_out)")
    p.add_argument("--1525", dest="c1525", type=Path, required=True)
    p.add_argument(
        "--fonts", type=Path, default=FONTS, help="KHGummi.otf and KHMenu.otf (pinned.json's fonts; the installer's)"
    )
    p.add_argument("--out", type=Path, default=MOD_INPUTS_FILE)


@command(
    "trace-mod-inputs",
    "developers: which collection files the mods' tools read, traced into mod_inputs.txt",
    _mod_trace_arguments,
)
def trace_mod_inputs(args: argparse.Namespace) -> dict[str, Any]:
    import subprocess
    import tempfile

    import collection_archive

    repository = HERE.parent
    install = args.install.resolve()
    places = mod_places(install, args.src.resolve(), args.fonts.resolve())
    with tempfile.TemporaryDirectory() as temp:
        log = Path(temp) / "trace.log"
        for mod in COLLECTION_MODS:  # every tool that reads the collection
            folder, template = PORT_MODS[mod]
            line = [sys.executable, "-c", _TRACER, str(repository / "tools" / "mods" / template[0])]
            line += [places[a[1:-1]] if a.startswith("{") else a for a in template[1:]] + [
                "--mod",
                str(Path(temp) / folder),
            ]
            subprocess.run(line, check=True, env={**os.environ, "KHDAYS_TRACE_LOG": str(log)})
        opened = {
            Path(raw).resolve() for kind, _, raw in (ln.partition("\t") for ln in log.read_text("utf-8").splitlines())
        }
    cipher = collection_archive.Cipher(collection_archive.load_table(args.c1525 / KH2_EXE))
    entries: dict[str, set[str]] = {}
    for archive, source in MOD_SOURCES.items():
        root = (args.src / f"{source}.hed_out").resolve()
        if not root.is_dir():
            continue
        with collection_archive.Archive(archive_base(args.c1525, "1.5+2.5", archive), cipher) as a:
            for path in opened:
                if root not in path.parents:
                    continue
                rel = path.relative_to(root).as_posix()
                layout, _, rel = rel.partition("/")
                if layout == "remastered":
                    rel = rel.rsplit("/", 1)[0]  # the asset's entry
                if layout in ("original", "remastered") and a.has(rel):
                    entries.setdefault(archive, set()).add(rel)
    lines = [
        "# The collection files the port's own mods' tools read (installer/setup_steps.py trace-mod-inputs):",
        "# '<archive> <entry>' (its data and remastered assets).",
        f"# tools {mods_digest(repository)}",
    ]
    for archive in sorted(entries):
        lines += [f"{archive} {rel}" for rel in sorted(entries[archive])]
    args.out.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    return {"inputs": sum(len(v) for v in entries.values()), "out": str(args.out)}


# ---- The installed game's starting settings ----------------------------------------------------

DEFAULT_SETTINGS = HERE / "default_settings.ini"
MESSAGES["settings-seeded"] = {
    "en": "The starting settings are in place.",
    "es": "Los ajustes iniciales están puestos.",
    "fr": "Les réglages de départ sont en place.",
    "de": "Die Anfangseinstellungen sind gesetzt.",
    "it": "Le impostazioni iniziali sono a posto.",
}


@command(
    "seed-settings",
    "the install's starting settings.ini (the maintainer's, without a nickname), when it has none",
    lambda p: p.add_argument("install", type=Path),
)
def seed_settings(args: argparse.Namespace) -> dict[str, Any]:
    target = args.install.resolve() / "saves" / "settings.ini"
    if target.exists():  # a player's own settings are kept (an update or a repair)
        return {"seeded": False, "message": words("settings-seeded", args.language)}
    target.parent.mkdir(parents=True, exist_ok=True)
    with making(target) as work:
        shutil.copyfile(DEFAULT_SETTINGS, work)
    return {"seeded": True, "message": words("settings-seeded", args.language)}


# ---- The port as a non-Steam game --------------------------------------------------------------

MESSAGES["steam-running"] = {
    "en": "Steam is running. It must be closed to change its games, or the change would be lost.",
    "es": "Steam está abierto. Hay que cerrarlo para cambiar sus juegos, o el cambio se perdería.",
    "fr": "Steam est en cours d'exécution. Il faut le fermer pour modifier ses jeux, sinon la modification serait perdue.",
    "de": "Steam läuft. Es muss geschlossen werden, um seine Spiele zu ändern, sonst ginge die Änderung verloren.",
    "it": "Steam è in esecuzione. Va chiuso per modificare i suoi giochi, altrimenti la modifica andrebbe persa.",
}
MESSAGES["steam-missing"] = {
    "en": "Steam was not found on this computer.",
    "es": "No se ha encontrado Steam en este equipo.",
    "fr": "Steam est introuvable sur cet ordinateur.",
    "de": "Steam wurde auf diesem Computer nicht gefunden.",
    "it": "Steam non è stato trovato su questo computer.",
}
MESSAGES["steam-wine"] = {
    "en": "This runs under Proton or Wine (a Steam Deck, Linux): the installer cannot reach this computer's "
    "Steam from here. Add the game to Steam by hand, from Desktop Mode.",
    "es": "Se está ejecutando en Proton o Wine (Steam Deck, Linux): desde aquí el instalador no llega al Steam "
    "de este equipo. Añade el juego a Steam a mano, desde el Modo Escritorio.",
    "fr": "Ceci tourne sous Proton ou Wine (Steam Deck, Linux) : l'installateur ne peut pas atteindre le Steam "
    "de cet ordinateur d'ici. Ajoutez le jeu à Steam à la main, depuis le mode Bureau.",
    "de": "Dies läuft unter Proton oder Wine (Steam Deck, Linux): Von hier erreicht das Installationsprogramm "
    "das Steam dieses Computers nicht. Füge das Spiel im Desktop-Modus von Hand zu Steam hinzu.",
    "it": "Questo gira sotto Proton o Wine (Steam Deck, Linux): da qui l'installazione non raggiunge lo Steam "
    "di questo computer. Aggiungi il gioco a Steam a mano, dalla modalità Desktop.",
}
MESSAGES["steam-added"] = {
    "en": "Added the port to Steam.",
    "es": "Se ha añadido el port a Steam.",
    "fr": "Le portage a été ajouté à Steam.",
    "de": "Die Portierung wurde zu Steam hinzugefügt.",
    "it": "Il port è stato aggiunto a Steam.",
}


def _steam_arguments(p: argparse.ArgumentParser) -> None:
    p.add_argument("--install", type=Path, required=True, help="the installed port's folder")


def _steam_setup(install: Path):
    """Steam, the installed port's executable, and the shortcut module; raises if Steam is absent."""
    import steam_library
    import steam_shortcut

    steam = steam_library.steam_folder()
    if steam is None:
        raise Refusal("steam-missing")
    exe = install.resolve() / steam_shortcut.EXE_NAME
    return steam, exe, steam_library, steam_shortcut


@command("steam-status", "whether Steam is here, is running, and already has the port", _steam_arguments)
def steam_status(args: argparse.Namespace) -> dict[str, Any]:
    import steam_library
    import steam_shortcut

    if steam_shortcut.under_wine():  # a Wine prefix's Steam is not the computer's: nothing to look at
        return {"steam": None, "wine": True, "running": False, "accounts": 0, "present": False}
    steam = steam_library.steam_folder()
    if steam is None:
        return {"steam": None, "wine": False, "running": False, "accounts": 0, "present": False}
    exe = args.install.resolve() / steam_shortcut.EXE_NAME
    present = False
    configs = steam_shortcut.user_configs(steam)
    for config in configs:
        try:
            entries, _ = steam_shortcut.read_shortcuts(config / "shortcuts.vdf")
        except (OSError, ValueError):
            continue
        present = present or steam_shortcut.port_present(entries, exe)
    return {
        "steam": str(steam),
        "steam_exe": str(steam / "steam.exe"),
        "wine": False,
        "running": steam_shortcut.steam_running(),
        "accounts": len(configs),
        "present": present,
    }


@command(
    "add-to-steam",
    "add the port to Steam as a non-Steam game (exit 1 if Steam is open)",
    lambda p: (
        _steam_arguments(p),
        p.add_argument("--proton", help="the Proton tool to map (a Windows game on Linux/Deck)"),
        p.add_argument(
            "--proton-from-collection",
            action="store_true",
            help="map whatever Proton the 1.5+2.5 collection already uses",
        ),
    ),
)
def add_to_steam(args: argparse.Namespace) -> dict[str, Any]:
    import steam_shortcut

    if steam_shortcut.under_wine():  # also for a silent /STEAM=yes: never write into a Wine prefix's Steam
        raise Refusal("steam-wine")
    steam, exe, steam_library, steam_shortcut = _steam_setup(args.install)
    if steam_shortcut.steam_running():
        raise Refusal("steam-running")
    appid = 0
    written = 0
    for config in steam_shortcut.user_configs(steam):
        entries, tail = steam_shortcut.read_shortcuts(config / "shortcuts.vdf")
        appid = steam_shortcut.set_port_shortcut(entries, exe)
        steam_shortcut.write_shortcuts(config / "shortcuts.vdf", entries, tail)
        written += 1

    # The port is a Windows game; on a Steam Deck / Linux it runs through Proton. Reusing the
    # Proton the 1.5+2.5 collection already uses means the same tool with no guessing; when the
    # collection has no mapping (plain Windows), Proton is not in use here and none is set.
    proton = args.proton
    proton_note = False
    if args.proton_from_collection and not proton:
        proton = _collection_proton(steam, steam_library, steam_shortcut)
    if proton:
        config_vdf = steam / "config" / "config.vdf"
        try:
            text = config_vdf.read_text(encoding="utf-8")
            new_text = steam_shortcut.set_compat_tool(text, appid, proton)
            _write_text_atomic(config_vdf, new_text)
        except (OSError, ValueError):
            # an explicit --proton that has no mapping block to go in: the player sets it by hand
            proton, proton_note = None, bool(args.proton)

    result = {
        "written": written,
        "appid": appid,
        "proton": proton,
        "proton_note": proton_note,
        "message": words("steam-added", args.language),
    }
    _record_steam(args.install, appid, written, proton)
    return result


@command("remove-from-steam", "take the port out of Steam (exit 1 if Steam is open)", _steam_arguments)
def remove_from_steam(args: argparse.Namespace) -> dict[str, Any]:
    import steam_shortcut

    if steam_shortcut.under_wine():  # nothing was ever written there
        return {"removed": 0}
    steam, exe, steam_library, steam_shortcut = _steam_setup(args.install)
    appid = steam_shortcut.shortcut_appid(f'"{exe}"', steam_shortcut.APP_NAME)  # deterministic, no state needed
    if not _port_in_steam(steam, exe, appid, steam_shortcut):
        _clear_steam(args.install)
        return {"removed": 0}  # nothing to take out: Steam need not be closed
    if steam_shortcut.steam_running():
        raise Refusal("steam-running")
    removed = 0
    for config in steam_shortcut.user_configs(steam):
        try:
            entries, tail = steam_shortcut.read_shortcuts(config / "shortcuts.vdf")
        except (OSError, ValueError):
            continue
        n = steam_shortcut.remove_port_shortcut(entries, exe)
        if n:
            steam_shortcut.write_shortcuts(config / "shortcuts.vdf", entries, tail)
            removed += n
    if appid:
        config_vdf = steam / "config" / "config.vdf"
        try:
            text = config_vdf.read_text(encoding="utf-8")
            new_text = steam_shortcut.remove_compat_tool(text, appid)
            if new_text != text:
                _write_text_atomic(config_vdf, new_text)
        except OSError:
            pass
    _clear_steam(args.install)
    return {"removed": removed}


def _port_in_steam(steam: Path, exe: Path, appid: int, steam_shortcut) -> bool:
    """Whether any user's shortcuts or config.vdf's Proton mapping still has the port."""
    for config in steam_shortcut.user_configs(steam):
        try:
            entries, _ = steam_shortcut.read_shortcuts(config / "shortcuts.vdf")
        except (OSError, ValueError):
            continue
        if steam_shortcut.port_present(entries, exe):
            return True
    try:
        text = (steam / "config" / "config.vdf").read_text(encoding="utf-8")
    except OSError:
        return False
    return steam_shortcut.read_compat_tool(text, appid) is not None


def _collection_proton(steam: Path, steam_library, steam_shortcut) -> str | None:
    """The Proton tool the 1.5+2.5 collection is already mapped to in config.vdf, to give the port
    the same (so a Windows game runs the same way on the Deck); None when it has none."""
    try:
        text = (steam / "config" / "config.vdf").read_text(encoding="utf-8")
    except OSError:
        return None
    return steam_shortcut.read_compat_tool(text, steam_library.APP_1525)


def _write_text_atomic(path: Path, text: str) -> None:
    """Write `text`, keeping a one-time backup of the first file replaced (config.vdf is Steam's)."""
    backup = path.with_name(path.name + ".khdays-backup")
    if path.exists() and not backup.exists():
        shutil.copy2(path, backup)
    work = partial_name(path)
    work.write_text(text, encoding="utf-8")
    os.replace(work, path)


def _record_steam(install: Path, appid: int, written: int, proton: str | None) -> None:
    state = load_state(install)
    state["steam"] = {"appid": appid, "accounts": written, "proton": proton}
    save_state(install, state)


def _clear_steam(install: Path) -> None:
    state = load_state(install)
    if "steam" in state:
        del state["steam"]
        save_state(install, state)


def write_result(path: Path | None, result: dict[str, Any]) -> None:
    # ASCII only (other characters as \u escapes): what the installer's script reads back
    text = json.dumps(result, indent=2, ensure_ascii=True)
    if path is None:
        print(text)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    work = partial_name(path)
    work.write_text(text, encoding="utf-8")
    os.replace(work, path)


def main(argv: list[str] | None = None) -> int:
    # --result and --language before the command or after its arguments
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--result", type=Path, default=argparse.SUPPRESS, help="the JSON file the result is written to")
    common.add_argument("--language", choices=LANGUAGES, default=argparse.SUPPRESS)
    # (the parent's actions are shared with the commands' parsers: their defaults stay unset)
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], parents=[common])
    sub = parser.add_subparsers(dest="command", required=True)
    for name, (_, arguments, help_text) in COMMANDS.items():
        arguments(sub.add_parser(name, help=help_text, parents=[common]))
    args = parser.parse_args(argv)
    args.result = getattr(args, "result", None)
    args.language = getattr(args, "language", "en")
    fn = COMMANDS[args.command][0]
    try:
        result = fn(args)
        code = OK
    except Refusal as refusal:
        result = {"message": words(refusal.key, args.language, **refusal.values), "refused": refusal.key}
        code = REFUSED
    except Exception as error:  # a failure: said, and its trace kept for a report
        result = {
            "message": words("failed", args.language, what=str(error) or type(error).__name__),
            "trace": traceback.format_exc(),
        }
        code = FAILED
    result.setdefault("ok", code == OK)
    write_result(args.result, result)
    return code


if __name__ == "__main__":
    sys.exit(main())
