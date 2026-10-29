"""The pause's pictures for the Menu Overhaul (layouts/pause; ui/pause.cpp draws them, and kh2pause.py cuts its
words out of them): its title, its "Are you sure?" and each line plain and lit, in the game's five languages, and
the hand.

They are made of the collection's own pieces, as the HD PACK already holds them (tools/hd/theater_settings.py and
theater_mission.py, from the player's 1.5+2.5 collection):

- the capsules: the settings menu's row (layouts/settings/<lang>/settings.txt, parts `row shown` -- black with a
  grey rim -- and `row selected` -- red with a white rim and its glow), its left, middle and right put together
  and drawn 462 x 72;
- the words: the theater's menu font (settings_font.png: menufont's coverage, 32-texel cells; settings.txt's
  `glyph` lines), each word's glyphs cut to what shows and drawn 27 dots high (the title 80), light grey (240)
  over a soft black edge (the letters grown 3 dots and blurred); kh2pause.py draws the game over's words with it;
- the hand: the theater's glove (layouts/rooms/en: its `glove` piece).

Each picture keeps hd_pause.py's canvas: a line 528 x 96 (the DS's 11 x 2 cells, 6 dots a DS dot) with its capsule
at (54, 12) and its words centred on x = 285; the question 672 x 96 and the title 576 x 144, their words centred.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageFilter

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from common import LANGS, Inputs  # noqa: E402

# The pause's words in each language, by the port's keys
WORDS = {
    "en": {
        "title": "PAUSE",
        "question": "Are you sure?",
        "continue": "Continue",
        "skip": "Skip",
        "withdraw": "Withdraw",
        "retry": "Retry",
        "yes": "Yes",
        "no": "No",
    },
    "fr": {
        "title": "PAUSE",
        "question": "En êtes-vous sûr ?",
        "continue": "Reprendre",
        "skip": "Passer",
        "withdraw": "Abandonner",
        "retry": "Réessayer",
        "yes": "Oui",
        "no": "Non",
    },
    "de": {
        "title": "PAUSE",
        "question": "Sicher?",
        "continue": "Fortfahren",
        "skip": "Überspringen",
        "withdraw": "Abbrechen",
        "retry": "Wiederholen",
        "yes": "Ja",
        "no": "Nein",
    },
    "it": {
        "title": "PAUSA",
        "question": "Sei sicuro?",
        "continue": "Continua",
        "skip": "Salta",
        "withdraw": "Ritirati",
        "retry": "Riprova",
        "yes": "Sì",
        "no": "No",
    },
    "es": {
        "title": "PAUSA",
        "question": "¿Seguro?",
        "continue": "Continuar",
        "skip": "Omitir",
        "withdraw": "Retirarse",
        "retry": "Reintentar",
        "yes": "Sí",
        "no": "No",
    },
}
LINES = ("continue", "skip", "withdraw", "retry", "yes", "no")

# The pictures: a line's (its capsule's place and size, its words' middle), the question's and the title's
LINE_SIZE, CAPSULE_AT, CAPSULE_SIZE, LINE_MIDDLE = (528, 96), (54, 12), (462, 72), 285
QUESTION_SIZE, TITLE_SIZE = (672, 96), (576, 144)
# The words: their glyphs' height (the title's, the others'), how much of the picture's width they may take
# (all of it but 100 dots), their grey, their black edge (the letters grown by a 7-dot square, blurred 1 dot)
WORDS_HIGH, TITLE_HIGH, WORDS_ROOM = 27, 80, 100
GREY, EDGE_GROWN, EDGE_BLUR, EDGE_ROOM = 240, 7, 1, 8
# The font's cells; the strip a word's glyphs are first laid on (its height, the cells' top in it); what a character
# the font lacks counts in that strip's width
CELL, STRIP_HIGH, STRIP_TOP, LACKING = 32, 40, 4, 12


def read_layout(folder: Path, atlas: bool = True) -> tuple[list[list[str]], Image.Image | None]:
    """A layout of the HD PACK's (layouts/<name>/<lang>): its file's lines, split into words, and its atlas as
    RGBA."""
    name = folder.parent.name
    lines = [line.split() for line in (folder / f"{name}.txt").read_text(encoding="utf-8").splitlines()]
    return lines, Image.open(folder / f"{name}_atlas.png").convert("RGBA") if atlas else None


class Font:
    """The theater's menu font as the HD PACK's settings menu holds it (layouts/settings/<lang>: settings.txt's
    `glyph` lines and settings_font.png): white glyphs whose alpha is the picture's coverage, each its place and
    advance by its character's code."""

    def __init__(self, folder: Path):
        lines, _ = read_layout(folder, atlas=False)
        coverage = Image.open(folder / "settings_font.png").convert("L")
        self.picture = Image.new("RGBA", coverage.size, (255, 255, 255, 0))
        self.picture.putalpha(coverage)
        self.glyphs = {int(w[1]): tuple(map(int, w[2:5])) for w in lines if w and w[0] == "glyph"}

    def line(self, text: str, whole: bool = False) -> Image.Image:
        """The text's glyph cells side by side, STRIP_TOP dots down a strip STRIP_HIGH high, each advancing its
        width; a character the font lacks (its space) advances as its "?". The strip is as wide as the advances and
        8 dots more but, unless `whole`, counts such a character LACKING dots, less than it advances: the pause's
        words with spaces lose the right of their last glyph past the strip's end, as the shipped pictures' "?" do."""
        lacking = None if whole else (0, 0, LACKING)
        width = sum(self.glyphs.get(ord(c), lacking or self.glyphs[ord("?")])[2] for c in text) + 8
        strip = Image.new("RGBA", (width, STRIP_HIGH))
        x = 0
        for c in text:
            u, v, advance = self.glyphs.get(ord(c), self.glyphs[ord("?")])
            if c != " ":
                strip.alpha_composite(self.picture.crop((u, v, u + CELL, v + CELL)), (x, STRIP_TOP))
            x += advance
        return strip

    def strip(self, text: str, rows: tuple[int, int] | None = None, whole: bool = False) -> Image.Image:
        """The text's line (whole or not) cut to what shows: across, and down too, or to the cells' `rows`
        [top, bottom)."""
        strip = self.line(text, whole)
        box = strip.getbbox()
        if rows is not None:
            box = (box[0], STRIP_TOP + rows[0], box[2], STRIP_TOP + rows[1])
        return strip.crop(box)


def settings(inputs: Inputs, lang: str) -> Path:
    """The HD PACK's settings menu in that language: its pieces and the theater's menu font."""
    return inputs.hd / "layouts" / "settings" / lang


def word(font: Font, text: str, high: int) -> Image.Image:
    """The text `high` dots high (its glyphs' box scaled to that), grey over its soft black edge, EDGE_ROOM dots of
    room around."""
    glyphs = font.strip(text)
    glyphs = glyphs.resize((max(1, round(glyphs.width * high / glyphs.height)), high), Image.Resampling.LANCZOS)
    alpha = glyphs.getchannel("A")
    letters = Image.new("RGBA", glyphs.size, (GREY, GREY, GREY))
    letters.putalpha(alpha)
    out = Image.new("RGBA", (glyphs.width + 2 * EDGE_ROOM, high + 2 * EDGE_ROOM))
    edge = Image.new("RGBA", out.size, (0, 0, 0, 255))
    grown = Image.new("L", out.size)
    grown.paste(alpha, (EDGE_ROOM, EDGE_ROOM))
    edge.putalpha(grown.filter(ImageFilter.MaxFilter(EDGE_GROWN)).filter(ImageFilter.GaussianBlur(EDGE_BLUR)))
    out.alpha_composite(edge)
    out.alpha_composite(letters, (EDGE_ROOM, EDGE_ROOM))
    return out


def capsule(lines: list[list[str]], atlas: Image.Image, state: str) -> Image.Image:
    """The settings menu's row in `state` (its left, middle and right parts side by side), CAPSULE_SIZE."""
    order = ("left", "middle", "right")
    parts = {}
    for w in lines:
        if len(w) == 10 and w[:3] == ["part", "row", state]:
            width, height, u, v = map(int, w[6:10])
            parts[w[3]] = atlas.crop((u, v, u + width, v + height))
    if sorted(parts) != sorted(order):
        raise ValueError(f"settings.txt has no whole `row {state}`")
    row = Image.new("RGBA", (sum(p.width for p in parts.values()), max(p.height for p in parts.values())))
    x = 0
    for name in order:
        row.alpha_composite(parts[name], (x, 0))
        x += parts[name].width
    return row.resize(CAPSULE_SIZE, Image.Resampling.LANCZOS)


def picture(size: tuple[int, int], words: Image.Image, middle: int, under: Image.Image | None = None) -> Image.Image:
    """The words centred on x = `middle` and on the picture's height, over the capsule `under` if there is one;
    words wider than the picture less WORDS_ROOM are shrunk to that first."""
    out = Image.new("RGBA", size)
    if under is not None:
        out.alpha_composite(under, CAPSULE_AT)
    if words.width > size[0] - WORDS_ROOM:
        words.thumbnail((size[0] - WORDS_ROOM, size[1]), Image.Resampling.LANCZOS)
    out.alpha_composite(words, (middle - words.width // 2, (size[1] - words.height) // 2))
    return out


def hand(inputs: Inputs) -> Image.Image:
    """The theater's glove, as the rooms' layout holds it."""
    lines, atlas = read_layout(inputs.hd / "layouts" / "rooms" / "en")
    for w in lines:
        if w[:2] == ["piece", "glove"]:
            u, v, width, height = map(int, w[2:6])
            return atlas.crop((u, v, u + width, v + height))
    raise ValueError("rooms.txt has no glove")


def make(inputs: Inputs, mod: Path) -> None:
    out = mod / "layouts" / "pause"
    for lang in LANGS:
        folder = settings(inputs, lang)
        lines, atlas = read_layout(folder)
        font = Font(folder)
        words = WORDS[lang]
        target = out / lang
        target.mkdir(parents=True, exist_ok=True)
        title = picture(TITLE_SIZE, word(font, words["title"], TITLE_HIGH), TITLE_SIZE[0] // 2)
        title.save(target / "title.png")
        question = picture(QUESTION_SIZE, word(font, words["question"], WORDS_HIGH), QUESTION_SIZE[0] // 2)
        question.save(target / "question.png")
        for key in LINES:
            for state, name in (("shown", key), ("selected", f"{key}_lit")):
                line = word(font, words[key], WORDS_HIGH)
                picture(LINE_SIZE, line, LINE_MIDDLE, capsule(lines, atlas, state)).save(target / f"{name}.png")
    hand(inputs).save(out / "hand.png")
