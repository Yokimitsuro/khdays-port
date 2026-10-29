"""The title's New Game dialogs as the Menu Overhaul changes them (mods/<mod>/layouts/title/<lang>/; ui/composition.c
draws them): the HD PACK's own newgame*.txt and newgame_atlas.png (tools/hd/newgame_dialog.py's, made from Dream Drop
Distance's dialogs) with

- the glove swaying as the port's other gloves do: a sway line after its piece, in every clip and loop;
- Kingdom Hearts II's firefly at the right end of the button the glove points at, as KH2 draws it beside its
  capsules (added as light, wandering 8 of KH2's dots each way: a wander line), shown and faded in with the glove;
  its picture (kh2_firefly.firefly: KH2's pause.2ld sprites) in a free corner of the atlas.

The title's other files are the HD PACK's, unchanged. From the HD PACK's layouts/title and KH2's menu/us/pause.2ld.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "hd"))  # tools/hd: newgame_dialog (the glove's size)
import kh2_firefly  # noqa: E402
import newgame_dialog  # noqa: E402
from common import LANGS, Inputs  # noqa: E402

FILES = "newgame*.txt"
ATLAS = newgame_dialog.ATLAS
SWAY = "sway 10 60"  # composition.c: the glove's sway (its swing and frames)
# The glove's piece: newgame_dialog.py's KH2 glove at its size
_GX0, _GY0, _GX1, _GY1 = newgame_dialog.HAND_RECT
GLOVE = (round((_GX1 - _GX0) * newgame_dialog.HAND_SCALE), round((_GY1 - _GY0) * newgame_dialog.HAND_SCALE))

# The firefly: the difficulty dialog's buttons are 92 units high where KH2's capsules are 30 of its dots, so a dot of
# KH2's is 92 / 30 units (the firefly as large in every dialog); KH2's firefly is 20 of its dots across and wanders 8
# each way, 26 and 34 swings in 1500 frames (pause.2ld's animation group 8)
KH2_DOT = 92 / 30
FIREFLY_DOTS = 20
WANDER = f"wander {8 * KH2_DOT:.2f} 26 34 1500"
FIREFLY_ADD = " ".join(["1"] * 16)  # its corners' colours: as it is
# Where its picture goes: the free square nearest the atlas's bottom right corner (2 texels clear around it), sought
# in steps of 4
FREE_ROOM = 2
FREE_STEP = 4


def free_square(alpha: np.ndarray, size: int) -> tuple[int, int]:
    """The free square's top left: no drawing within FREE_ROOM of it."""
    h, w = alpha.shape
    drawn = np.zeros((h + 1, w + 1), np.int64)
    drawn[1:, 1:] = np.cumsum(np.cumsum(alpha > 0, 0), 1)
    r = FREE_ROOM
    for y in range(h - size - r, r - 1, -FREE_STEP):
        for x in range(w - size - r, r - 1, -FREE_STEP):
            y0, y1, x0, x1 = y - r, y + size + r, x - r, x + size + r
            if drawn[y1, x1] - drawn[y0, x1] - drawn[y1, x0] + drawn[y0, x0] == 0:
                return x, y
    raise ValueError("the atlas has no room for the firefly")


def sections(lines: list[str]) -> list[list[tuple[int, list[str], list[list[str]]]]]:
    """The file's clips and loop, each its pieces: the piece line's index, its words and its keys' words."""
    out = []
    for i, line in enumerate(lines):
        words = line.split()
        if words[:1] in (["clip"], ["loop"]):
            out.append([])
        elif words[:1] == ["piece"] and out:
            out[-1].append((i, words, []))
        elif words[:1] == ["key"] and out and out[-1]:
            out[-1][-1][2].append(words)
    return out


def rect(key: list[str]) -> tuple[float, float, float, float]:
    return tuple(float(v) for v in key[2:6])


def button_end(pieces, glove) -> tuple[float, float]:
    """The right end and the middle of the button the glove points at (where the pieces are at their first key): its
    left end is under the glove at the glove's middle (the shortest piece there), and it runs on through the pieces
    joined to it, edge to edge, in its rows."""
    gx0, gy0, gx1, gy1 = rect(glove[2][0])
    middle = (gy0 + gy1) / 2
    rects = [rect(keys[0]) for _, _, keys in pieces if keys and keys is not glove[2]]
    _, top, bottom, right = min(
        (y1 - y0, y0, y1, max(x0, x1)) for x0, y0, x1, y1 in rects if y0 <= middle <= y1 and gx0 <= min(x0, x1) <= gx1
    )
    while True:
        joined = [x1 for x0, y0, x1, y1 in rects if (y0, y1) == (top, bottom) and abs(x0 - right) < 0.01]
        if not joined or max(joined) <= right:
            return right, (top + bottom) / 2
        right = max(joined)


def dialog(lines: list[str], at: tuple[int, int], size: int) -> list[str]:
    """The file with the glove's sway and the firefly after each section's glove: its right end at the button's, on
    the button's middle, at each of the glove's keys with the glove's colour."""
    after = {}  # a line's index: the lines that go after it
    across = FIREFLY_DOTS * KH2_DOT
    for pieces in sections(lines):
        glove = [p for p in pieces if (int(p[1][3]), int(p[1][4])) == GLOVE and p[1][5] == "over"][-1]
        right, middle = button_end(pieces, glove)
        box = " ".join(f"{v:.2f}" for v in (right - across, middle - across / 2, right, middle + across / 2))
        block = [f"piece {at[0]} {at[1]} {size} {size} add {FIREFLY_ADD}", WANDER]
        block += [f"key {key[1]} {box} " + " ".join(key[6:]) for key in glove[2]]
        own = glove[0]  # the sway after the piece's own lines (a prompt), the firefly after its keys
        while lines[own + 1].split()[:1] not in (["key"], ["piece"], ["clip"], ["loop"]):
            own += 1
        after[own] = [SWAY]
        after[own + len(glove[2])] = block
    out = []
    for i, line in enumerate(lines):
        out.append(line)
        out.extend(after.get(i, []))
    return out


def make(inputs: Inputs, mod: Path) -> None:
    picture = np.asarray(kh2_firefly.firefly(inputs).convert("RGBA"))
    size = picture.shape[0]
    for lang in LANGS:
        hd = inputs.hd / "layouts" / "title" / lang
        out = mod / "layouts" / "title" / lang
        out.mkdir(parents=True, exist_ok=True)
        atlas = np.asarray(Image.open(hd / ATLAS).convert("RGBA")).copy()
        x, y = free_square(atlas[..., 3], size)
        atlas[y : y + size, x : x + size] = picture
        Image.fromarray(atlas, "RGBA").save(out / ATLAS)
        for path in sorted(hd.glob(FILES)):
            lines = path.read_bytes().decode("utf-8").split("\n")
            (out / path.name).write_bytes("\n".join(dialog(lines, (x, y), size)).encode("utf-8"))
