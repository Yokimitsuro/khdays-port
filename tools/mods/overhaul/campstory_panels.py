"""The story camp menu's panels: the Panels page's panels as the game draws them (UI/pnl/3d_<lang>.pak, a BTX0
each) laid out in a sheet, and the panels' records (db/db.p2) the menu's grid reads; with port 0.1.4's Stop,
Stopra and Stopga, which the game has no panels of its own for."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "hd"))
import ds_btx  # noqa: E402
import save_screen as ss  # noqa: E402

# Port 0.1.4's Stop, Stopra and Stopga (runtime/battle/stop_magic.c): panels 513-515, copies of Blizzard's levels'
# records (96-98) with the clock's picture (0x1b, tag 0x1c) in the magics' purple (Blizzard's 0x25, colours 2-4).
STOP_PANELS, STOP_FROM = (513, 514, 515), (96, 97, 98)
STOP_PICTURE, STOP_COLOURS_FROM, STOP_COLOURS = 0x1B, 0x25, (2, 3, 4)
PANEL_K, PANEL_COLUMNS = 3, 16  # the panels' textures (16 x 16 dots) at 3 texels a dot, 16 to a row
RECORD = 0x64  # a panel's record in db/db.p2's member 0x15


def panel_sheet(pak: bytes) -> tuple[Image.Image, int]:
    """The Panels page's panels (a KAPH pack's BTX0s: the game's own textures, in its order), each dot made
    PANEL_K x PANEL_K texels (nearest), PANEL_COLUMNS to a row; and their count. The Stop panels' picture is the
    game's clock with its edges and ground (colours 2-4) in Blizzard's purple."""
    members = ds_btx.kaph(pak)
    texs = [ds_btx.textures(m)[0] if m[:4] == b"BTX0" else (None, None, None) for m in members]
    if STOP_PICTURE < len(members) and STOP_COLOURS_FROM < len(members):
        purple = ds_btx.palette(members[STOP_COLOURS_FROM])
        texs[STOP_PICTURE] = ds_btx.textures(members[STOP_PICTURE], {k: purple[k] for k in STOP_COLOURS})[0]
    cell = 16 * PANEL_K
    rows = (len(texs) + PANEL_COLUMNS - 1) // PANEL_COLUMNS
    sheet = Image.new("RGBA", (PANEL_COLUMNS * cell, rows * cell), (0, 0, 0, 0))
    for i, (_, _, px) in enumerate(texs):
        if px is None:
            continue
        im = Image.fromarray(px, "RGBA").resize((cell, cell), Image.NEAREST)
        sheet.paste(im, ((i % PANEL_COLUMNS) * cell, (i // PANEL_COLUMNS) * cell))
    return sheet, len(texs)


def panel_table(db: Path) -> str:
    """The panels' records the Panels page reads, as panels.txt's lines: db/db.p2's member 0x15 (msg db 0x15's
    0x64-byte records by item id from 1: +0x04 the 1-based tag of its texture in the 3d pak, +0x08 its link's
    shape (-1 none), +0x09/+0x0a its anchor's column and row in the shape) and member 0x1a (msg db 0x1b's shapes:
    a u64 mask a shape, bit col + 8 * row)."""
    records = ss.p2_entry(db, 0x15)
    n = struct.unpack_from("<I", records, 0)[0]
    lines = [
        "# The panels' records (tools/mods/overhaul/campstory_panels.py, from db/db.p2): panel ID TAG SHAPE COL ROW",
        "# (TAG the 1-based texture of UI/pnl/3d_<lang>.pak; SHAPE -1 a panel alone, else a link's shape, its",
        "# anchor at COL ROW of it); shape N LOW HIGH (its mask's two words: bit col + 8 * row).",
    ]
    for i in range(1, n + 1):
        r = records[4 + (i - 1) * RECORD : 4 + i * RECORD]
        tag = struct.unpack_from("<H", r, 4)[0]
        shape, col, row = struct.unpack_from("<bBB", r, 8)
        if tag > 0:
            lines.append(f"panel {i} {tag} {shape} {col} {row}")
    # Stop's, blank records in the game's table: Blizzard's shapes, the clock's picture
    for stop, blizzard in zip(STOP_PANELS, STOP_FROM):
        r = records[4 + (blizzard - 1) * RECORD : 4 + blizzard * RECORD]
        shape, col, row = struct.unpack_from("<bBB", r, 8)
        if stop <= n and struct.unpack_from("<H", records, 4 + (stop - 1) * RECORD + 4)[0] == 0:
            lines.append(f"panel {stop} {STOP_PICTURE + 1} {shape} {col} {row}")
    shapes = ss.p2_entry(db, 0x1A)
    for k in range(struct.unpack_from("<I", shapes, 0)[0]):
        low, high = struct.unpack_from("<II", shapes, 4 + 8 * k)
        lines.append(f"shape {k} {low} {high}")
    return "\n".join(lines) + "\n"
