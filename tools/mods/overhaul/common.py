"""What the Menu Overhaul's pieces are made from (tools/mods/menu_overhaul.py): the player's collections as the
installer extracts them (installer/collection_archive.py: <archive>.hed_out/original/<path> and
remastered/<path>/<asset>), the game's own data (data/extracted/<hash>), the HD menus' pieces the installer made
before (mods/HD PACK), Televo's KH Re:Collection fonts and the repository's own pictures (art/)."""

from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1]))  # tools/: decompress_data.py
import decompress_data as dd  # noqa: E402

LANGS = ("en", "fr", "de", "it", "es")  # the game's five, in the port's order
ART = HERE / "art"  # the pictures made for the Menu Overhaul (no game's)


@dataclass
class Inputs:
    kh2: Path  # KINGDOM HEARTS II FINAL MIX's entries (kh2_first and kh2_fifth): the menus, the fields' irises
    bbs: Path  # Birth by Sleep's (bbs_first): the camp menu
    recom: Path  # Re:Chain of Memories' (Recom): the Moogle Shop
    mare: Path  # the 1.5+2.5 theater's (Mare.hed_out/original)
    hd: Path  # the HD menus' pieces (mods/HD PACK: tools/hd's)
    fonts: Path  # Televo's KH Re:Collection fonts (KHGummi.otf, KHMenu.otf)
    data: Path  # the game's extraction (data/extracted/<hash>: nitrofs/, decompressed/)
    exe: Path | None  # the port (khdays-native.exe): the game's own sounds rendered

    @property
    def nitrofs(self) -> Path:
        return self.data / "nitrofs"

    def ds(self, path: str) -> bytes:
        """A file of the game's as it reads it: an LZ-compressed one (.z) decompressed."""
        data = (self.nitrofs / path).read_bytes()
        if path.endswith(".z") and data[:1] in (b"\x10", b"\x11"):
            return dd.decompress_nitro(data)[1]
        return data

    def font(self, name: str) -> Path:
        return self.fonts / name
