"""Where the Xbox app (the Microsoft Store, PC Game Pass) keeps the KINGDOM HEARTS collections on this computer:
each drive's game library, the folder its `.GamingRoot` file at the drive's root names (`XboxGames` unless the
player chose another), a game in `<library>/<game>/Content`. A collection is the folder that holds its archives
in `Image/<language>`, as Steam's and the Epic Games Store's copies do; the Xbox app's keeps Days's videos in
`MSSTORE/Mare/MOVIE/Days/<language>` (seen in a player's copy: `MSSTORE` holds `dt`, `juefigs`, `Mare` and
`Theater` beside `Image`). Read only."""

from __future__ import annotations

import os
import string
from pathlib import Path

from epic_library import holds

DEFAULT_LIBRARY = "XboxGames"
GAMING_ROOT = ".GamingRoot"


def drives() -> list[Path]:
    """The computer's drives' roots."""
    listed = getattr(os, "listdrives", None)  # Windows, Python 3.12 on
    if listed is not None:
        try:
            return [Path(d) for d in listed()]
        except OSError:
            pass
    return [Path(f"{letter}:\\") for letter in string.ascii_uppercase if Path(f"{letter}:\\").is_dir()]


def gaming_root_names(data: bytes) -> list[str]:
    """The library folders a `.GamingRoot` file names: "RGBX", a 32-bit count, then the folders' names in
    UTF-16, each ended by a 0 (what is not read so gives none)."""
    if data[:4] != b"RGBX" or len(data) < 8:
        return []
    count = int.from_bytes(data[4:8], "little")
    names = [n for n in data[8:].decode("utf-16-le", errors="ignore").split("\0") if n.strip()]
    return names[:count] if count else names


def library_folders(roots: list[Path] | None = None) -> list[Path]:
    """Every game library of the Xbox app that is there: the folders each drive's `.GamingRoot` names,
    and each drive's `XboxGames`."""
    found: list[Path] = []
    for root in roots if roots is not None else drives():
        try:
            names = gaming_root_names((root / GAMING_ROOT).read_bytes())
        except OSError:
            names = []
        for name in [*names, DEFAULT_LIBRARY]:
            folder = root / name.lstrip("\\/")
            try:
                there = folder.is_dir()
            except OSError:
                there = False
            if there and folder not in found:
                found.append(folder)
    return found


def collection_folder(marker: str, libraries: list[Path] | None = None) -> Path | None:
    """The Xbox app's folder of the collection whose archive is `marker` (a game's `Content`), or None."""
    for library in libraries if libraries is not None else library_folders():
        try:
            games = sorted(g for g in library.iterdir() if g.is_dir())
        except OSError:
            continue
        for game in games:
            for folder in (game / "Content", game):
                if holds(folder, marker):
                    return folder
    return None
