"""The camp menu's pieces as the Menu Overhaul changes them (mods/<mod>/layouts/camp/<lang>/; host/screens/
overhaul_camp_screen.c and camp_screen.c read them): the HD PACK's own (tools/hd/ddd_camp.py's, made from Dream Drop
Distance's camp) with these changes:

  camp.txt                the money's and the heart points' digits lower, the heart points' word as a picture
                          (place hearts_label), the digits closer (digit_overlap), the story's Roxas (piece
                          face12_story) and Mission Mode's model framed as the story's (mission_model_source,
                          mission_model_view); in an HD PACK whose digits came four to a piece, each digit's own
  camp_atlas.png          the HD PACK's, grown under it by the story's Roxas: the user's portrait (art/camp)
  camp_page_backdrop.png  the Preferences page's panel remade from the status panel's own texels
  hearts_label.png        the heart points' word in KH Gummi, as DDD draws MUNNY over the money's digits
  camp_backdrop.png, camp_font.png   the HD PACK's, unchanged (they go beside the changed camp.txt)

From the HD PACK's layouts/camp, the game's UI/cm/str/status_<lang>.s (the heart points' word), Televo's KHGummi and
the user's portrait.
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "hd"))  # tools/hd: ddd_camp (the game's string tables)
import ddd_camp  # noqa: E402
import files_font  # noqa: E402
from common import ART, LANGS, Inputs  # noqa: E402
from raqm_text import ShapedFont  # noqa: E402

# The camp.txt lines changed, by their first words: the money's and the heart points' digits lower, the word's
# picture between them, the digits closer together
PLACES = {
    "place munny": ["place munny 410 910 0 32"],
    "place hearts": ["place hearts 410 994 0 32", "place hearts_label 50 968 0 18"],
    "digit_overlap": ["digit_overlap 0.04"],
}
PIECE_NOTE = (
    "#                              character N), sigil, crown, digit<N>, settings_esc, settings_gear, glove,",
    "#                              character N; face<N>_story, if there, in the story alone), sigil, crown,"
    " digit<N>, settings_esc, settings_gear, glove,",
)
# Mission Mode's model in the story's look (its own camera is farther than the story's), after the story's lines'
# note
STORY_NOTE = "#   story_model_source, story_model_view   the story's camp's model (as model_*)"
MISSION_MODEL = [
    "#   mission_model_source, mission_model_view   Mission Mode's model in the story's look (its camera is farther",
    "#                              than the story's, its character some 48 dots high: framed to fill the plate as"
    " Roxas)",
    "mission_model_source 117 22 134 116",
    "mission_model_view 111 32.3 84 84",
]
# The story's Roxas: the user's portrait, its piece after the game's faces
PORTRAIT = "face12_story"
LAST_FACE = "face18"

# An HD PACK made from DDD's remastered digits at half their size holds them four to a piece (ddd_camp.py's cells of
# 128 x 90 then took four digits each): pieces digit0, digit1 and digit4 hold 0 1 4 5, 2 3 6 7 and 8 9 (over two of
# the sheet's other signs). Each digit is then its own rectangle in those (left, top from the piece's, width,
# height), as they were measured on that HD PACK's atlas. An HD PACK made from the digits at their size holds one a
# piece already.
CLUMPS = {"digit0": 4, "digit1": 4, "digit4": 4}
DIGIT_CELLS = {
    0: ("digit0", -1, -3, 51, 34),
    1: ("digit0", 73, -3, 29, 34),
    2: ("digit1", -1, -3, 51, 34),
    3: ("digit1", 59, -3, 51, 34),
    4: ("digit0", 5, 45, 45, 34),
    5: ("digit0", 59, 45, 51, 34),
    6: ("digit1", -1, 45, 51, 34),
    7: ("digit1", 64, 45, 46, 34),
    8: ("digit4", -1, -1, 51, 34),
    9: ("digit4", 63, -1, 47, 34),
}

# The Preferences page's panel: the status panel (camp.txt's panel) grown left to the model's box (its frame), made
# of the status panel's own texels -- its left edge (highlight and bevel), one column of its middle (its shades run
# down, the same across), its right edge -- so that the port grows the one into the other (camp_screen.c)
PAGE_LEFT = 24  # the left edge's texels
PAGE_MIDDLE = 22  # the middle's column, from the panel's left (before the table's lines)
PAGE_RIGHT = 20  # the right edge's texels

# The heart points' word: the game's (status_<lang>.s), in capitals, in KH Gummi as DDD's MUNNY over the money's
# digits -- as high (the place's 18), spaced alike, light grey -- drawn four times larger and brought down. The picture
# is twice the place's size, with room for accents above the capitals (0.45 of them) and 0.15 below (as
# overhaul_camp_screen.c draws it).
HEARTS_STRINGS = "UI/cm/str/status_{}.s"
HEARTS_STRING = 11
GUMMI = "KHGummi.otf"
GUMMI_CAP = 0.8  # KH Gummi's capitals: 800 of its 1000 units
WORD_CAP = 36  # the picture's capitals
SUPERSAMPLE = 4
ABOVE, BELOW = 0.45, 0.15
TRACKING = 0.16  # the room between letters, of the capitals' height (MUNNY's)
WORD_GREY = 232


def read_pieces(lines: list[str]) -> dict[str, tuple[int, int, int, int]]:
    return {w[1]: tuple(int(v) for v in w[2:6]) for w in (line.split() for line in lines) if w[:1] == ["piece"]}


def glyphs(alpha: np.ndarray) -> int:
    """How many separate drawings (8 ways touching dots) a piece's alpha holds."""
    seen = np.zeros(alpha.shape, bool)
    count = 0
    h, w = alpha.shape
    for y, x in zip(*np.nonzero(alpha)):
        if seen[y, x]:
            continue
        count += 1
        seen[y, x] = True
        stack = [(y, x)]
        while stack:
            cy, cx = stack.pop()
            for ny in range(max(0, cy - 1), min(h, cy + 2)):
                for nx in range(max(0, cx - 1), min(w, cx + 2)):
                    if alpha[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
    return count


def own_digits(lines: list[str], atlas: np.ndarray) -> list[str]:
    """The digit pieces each a digit alone: unchanged when they are already, else their rectangles in the pieces
    that hold four (DIGIT_CELLS)."""
    pieces = read_pieces(lines)
    for name, count in CLUMPS.items():
        u, v, w, h = pieces[name]
        if glyphs(atlas[v : v + h, u : u + w, 3] > 0) != count:
            return lines
    out = []
    for line in lines:
        words = line.split()
        if words[:1] == ["piece"] and words[1].startswith("digit"):
            clump, dx, dy, w, h = DIGIT_CELLS[int(words[1][5:])]
            u, v = pieces[clump][:2]
            line = f"piece {words[1]} {u + dx} {v + dy} {w} {h}"
        out.append(line)
    return out


def camp_txt(lines: list[str], atlas: np.ndarray, portrait_v: int) -> list[str]:
    """The HD PACK's camp.txt with the Menu Overhaul's lines (see the top)."""
    out = []
    for line in own_digits(lines, atlas):
        key = " ".join(line.split()[:2]) if line.startswith("place ") else line.split(" ")[0]
        if key in PLACES:
            out.extend(PLACES[key])
            continue
        out.append(PIECE_NOTE[1] if line == PIECE_NOTE[0] else line)
        if line == STORY_NOTE:
            out.extend(MISSION_MODEL)
        elif line.startswith(f"piece {LAST_FACE} "):
            out.append(f"piece {PORTRAIT} 1 {portrait_v} 256 256")
    return out


def with_portrait(atlas: np.ndarray, portrait: np.ndarray) -> tuple[np.ndarray, int]:
    """The atlas grown under it by the portrait at its left, with a texel of its edge around it (as the atlas's
    pieces have) and a clear row under; the portrait's top."""
    h, w = atlas.shape[:2]
    ph, pw = portrait.shape[:2]
    out = np.zeros((h + ph + 3, w, 4), np.uint8)
    out[:h] = atlas
    out[h : h + ph + 2, : pw + 2] = np.pad(portrait, ((1, 1), (1, 1), (0, 0)), mode="edge")
    return out, h + 1


def page_backdrop(lines: list[str], menu: np.ndarray, page: np.ndarray) -> np.ndarray:
    """The Preferences page's backdrop with its panel made of the status panel's texels (see PAGE_*)."""
    values = {
        w[0]: [int(v) for v in w[1:]] for w in (line.split() for line in lines) if w[:1] in (["panel"], ["frame"])
    }
    px, py, pw, ph = values["panel"]
    left = values["frame"][0]
    right = px + pw
    rows = slice(py, py + ph)
    out = page.copy()
    out[rows, left : left + PAGE_LEFT] = menu[rows, px : px + PAGE_LEFT]
    out[rows, left + PAGE_LEFT : right - PAGE_RIGHT] = menu[rows, px + PAGE_MIDDLE : px + PAGE_MIDDLE + 1]
    out[rows, right - PAGE_RIGHT : right] = menu[rows, right - PAGE_RIGHT : right]
    return out


def hearts_label(font: ShapedFont, word: str) -> Image.Image:
    """The word's picture: its letters one by one (HarfBuzz's advances, TRACKING apart) drawn big, the capitals'
    top ABOVE of their height from the top, cut to the letters across and brought down; light grey, its alpha the
    letters'."""
    cap = WORD_CAP * SUPERSAMPLE
    frame = int((1 + ABOVE + BELOW) * cap)
    pad = cap
    width = int(sum(font.advance(c) for c in word) + len(word) * TRACKING * cap) + 2 * pad
    canvas = Image.new("L", (width, frame))
    draw = ImageDraw.Draw(canvas)
    y = round(ABOVE * cap) - files_font.bbox(font, "H")[1]
    x = float(pad)
    for c in word:
        draw.text((x, y), c, font=font.font, fill=255)
        x += font.advance(c) + TRACKING * cap
    left, _, right, _ = canvas.getbbox()
    canvas = canvas.crop((left, 0, right, frame))
    alpha = canvas.resize((round(canvas.width / SUPERSAMPLE), round(frame / SUPERSAMPLE)), Image.LANCZOS)
    out = Image.new("RGBA", alpha.size, (WORD_GREY, WORD_GREY, WORD_GREY, 0))
    out.putalpha(alpha)
    return out


def make(inputs: Inputs, mod: Path) -> None:
    portrait = np.asarray(Image.open(ART / "camp" / f"{PORTRAIT}.png").convert("RGBA"))
    font = ShapedFont(inputs.font(GUMMI), round(WORD_CAP * SUPERSAMPLE / GUMMI_CAP))
    for lang in LANGS:
        hd = inputs.hd / "layouts" / "camp" / lang
        out = mod / "layouts" / "camp" / lang
        out.mkdir(parents=True, exist_ok=True)
        lines = (hd / "camp.txt").read_text("utf-8").splitlines()
        atlas = np.asarray(Image.open(hd / "camp_atlas.png").convert("RGBA"))
        grown, portrait_v = with_portrait(atlas, portrait)
        Image.fromarray(grown, "RGBA").save(out / "camp_atlas.png")
        (out / "camp.txt").write_bytes(("\n".join(camp_txt(lines, atlas, portrait_v)) + "\n").encode("utf-8"))
        menu = np.asarray(Image.open(hd / "camp_backdrop.png").convert("RGB"))
        page = np.asarray(Image.open(hd / "camp_page_backdrop.png").convert("RGB"))
        Image.fromarray(page_backdrop(lines, menu, page), "RGB").save(out / "camp_page_backdrop.png")
        for name in ("camp_backdrop.png", "camp_font.png"):
            shutil.copyfile(hd / name, out / name)
        word = ddd_camp.strings(inputs.data, HEARTS_STRINGS.format(lang))[HEARTS_STRING].upper()
        hearts_label(font, word).save(out / "hearts_label.png")
