"""Kingdom Hearts II's pause for the Menu Overhaul (layouts/kh2pause; host/screens/modern_pause.c draws it, and
host/screens/layout_screen.c its firefly): KH2's own pause layout, its sheet, its firefly and the pause's words.

- pause.seqd: the sequence of KH2's menu/us/pause.2ld (its LAYD's one SEQD, as the file holds it). The seven
  languages' pause.2ld hold the same layout; only their sheets differ.
- <lang>/sheet.png: that layout's HD sheet (remastered/menu/<KH2's language>/pause.2ld, its texture 0, 256 x 256:
  four texels a PS2 one), as the collection holds it. Italian and Spanish say PAUSA.
- firefly.png: KH2's firefly, drawn by kh2_firefly.py from the sequence and the English sheet.
- <lang>/continue, skip, yes, no, withdraw, retry.png: the words of the pause's lines, cut out of the mod's own
  pause pictures (pause.py's layouts/pause/<lang>/<key>.png: the words over a black capsule): the rim set apart, the
  words' box with 3 dots of room, the capsule's black (6) taken away so that the words keep their light as alpha.
- <lang>/question.png: the mod's "Are you sure?" (layouts/pause/<lang>/question.png) cut to its words.
- <lang>/go_continue, go_easier, go_withdraw.png: the game over's lines (its own words in each language), in the
  theater's menu font as pause.py draws the pause's words and at their size (a word of capitals 27 dots high), but
  all of them cut to the cells' rows any of them has ink in, so that they share one line: the letters light grey
  (249, the pause words' grey as their cut-out pictures keep it) over a soft black edge (grown a dot, blurred, at
  0.55), 3 dots of room around; at most 350 dots across.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kh2_firefly  # noqa: E402
import pause  # noqa: E402
from common import LANGS, Inputs  # noqa: E402

# KH2's menu folder for each of the port's languages (the theater's are theater_title.LANGUAGES)
KH2_LANGS = {"en": "us", "fr": "fr", "de": "gr", "it": "it", "es": "sp"}
PAUSE = "menu/{}/pause.2ld"

# The pause pictures' words: the capsule's black under them, how bright a dot of the words is at least, the
# room left around their box
CAPSULE_BLACK = 6
WORD_BRIGHT = 40
WORD_ROOM = 3
LINES = ("continue", "skip", "yes", "no", "withdraw", "retry")

# The game over's lines, its own words (ov027's menu: Continue, Decrease Challenge, Withdraw), as its pictures
# write them (they are not text in the game's data)
GAME_OVER = {
    "en": {"go_continue": "Continue", "go_easier": "Decrease Challenge", "go_withdraw": "Withdraw"},
    "fr": {"go_continue": "Continuer", "go_easier": "Réduire le niveau", "go_withdraw": "Abandonner"},
    "de": {"go_continue": "Fortsetzen", "go_easier": "Vereinfachen", "go_withdraw": "Rückzug"},
    "it": {"go_continue": "Continua", "go_easier": "Riduci sfida", "go_withdraw": "Ritirati"},
    "es": {"go_continue": "Continuar", "go_easier": "↓dificultad", "go_withdraw": "Retirarse"},
}
# Their look: the letters' grey; their edge (the letters grown by a 3-dot square, blurred 0.8 dots, at 0.55); the
# room around them; the widest a line may be. A capital's glyph (its height is the font's capitals': 19 texels)
GO_GREY, GO_EDGE_GROWN, GO_EDGE_BLUR, GO_EDGE, GO_ROOM, GO_WIDEST = 249, 3, 0.8, 0.55, 3, 350
CAPITAL = "R"


def save(im: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)


def hd_texture(inputs: Inputs, entry: str, stem: str) -> Image.Image:
    """An entry's HD texture as the collection holds it (a DDS or a PNG), as RGBA."""
    (path,) = sorted((inputs.kh2 / "remastered" / entry).glob(stem + ".*"))
    return Image.open(path).convert("RGBA")


def sheet(inputs: Inputs, lang: str) -> Image.Image:
    code = KH2_LANGS[lang]
    return hd_texture(inputs, PAUSE.format(code), f"{code.upper()}_pause_2ld0")


# --- The pause pictures' words ------------------------------------------------------------------------------------


def grown(seed: np.ndarray, within: np.ndarray) -> np.ndarray:
    """`seed` grown through the touching (8 ways) dots of `within`."""
    h, w = seed.shape
    while True:
        p = np.pad(seed, 1)
        more = seed.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                more |= p[1 + dy : 1 + dy + h, 1 + dx : 1 + dx + w]
        more &= within
        if np.array_equal(more, seed):
            return seed
        seed = more


def line_words(picture: Image.Image) -> Image.Image:
    """A pause line's words cut out of its picture (the words over a black capsule with a bright rim): the bright
    dots that are not the rim (the ring touching their box's edges), their box with room around, and the capsule's
    black taken away -- what is left over it is the words' light, kept as alpha, their colour that light's."""
    c = np.asarray(picture.convert("RGBA"))[..., :3].astype(np.float32)
    bright = c.max(-1) > WORD_BRIGHT
    ys, xs = np.nonzero(bright)
    edges = np.zeros_like(bright)
    edges[[ys.min(), ys.max()], :] = True
    edges[:, [xs.min(), xs.max()]] = True
    words = bright & ~grown(bright & edges, bright)
    ys, xs = np.nonzero(words)
    x0, y0 = xs.min() - WORD_ROOM, ys.min() - WORD_ROOM
    x1, y1 = xs.max() + 1 + WORD_ROOM, ys.max() + 1 + WORD_ROOM
    light = c[y0:y1, x0:x1] - CAPSULE_BLACK
    alpha = np.clip(light.max(-1) / (255 - CAPSULE_BLACK), 0, 1)
    a8 = (alpha * 255).astype(np.uint8)
    # the faintest (the capsule's own shading, not the words) keep no colour
    colour = np.where(a8[..., None] >= 3, light / np.where(alpha > 0, alpha, 1)[..., None], 0)
    out = np.concatenate([np.clip(colour, 0, 255).astype(np.uint8), a8[..., None]], -1)
    return Image.fromarray(out, "RGBA")


def question(picture: Image.Image) -> Image.Image:
    """The question's words: its picture cut to what shows."""
    return picture.convert("RGBA").crop(picture.convert("RGBA").getbbox(alpha_only=True))


# --- The game over's words ----------------------------------------------------------------------------------------


def shared_rows(fonts: dict[str, pause.Font]) -> tuple[int, int]:
    """The glyph cells' rows [top, bottom) any of the game over's words has ink in, in any language (the accents'
    tops to the descenders' feet)."""
    boxes = [fonts[lang].line(text, whole=True).getbbox() for lang in LANGS for text in GAME_OVER[lang].values()]
    return min(b[1] for b in boxes) - pause.STRIP_TOP, max(b[3] for b in boxes) - pause.STRIP_TOP


def game_over_word(font: pause.Font, text: str, rows: tuple[int, int]) -> Image.Image:
    """The word's glyphs cut to the shared `rows`, in GO_GREY, scaled as pause.py scales a word of capitals to its
    words' height (or to fit GO_WIDEST with the room), over a soft black edge, GO_ROOM dots of room around."""
    strip = font.strip(text, rows, whole=True)
    k = min(pause.WORDS_HIGH / font.strip(CAPITAL).height, (GO_WIDEST - 2 * GO_ROOM) / strip.width)
    letters = Image.new("RGBA", strip.size, (GO_GREY, GO_GREY, GO_GREY))
    letters.putalpha(strip.getchannel("A"))
    letters = letters.resize((round(strip.width * k), round(strip.height * k)), Image.Resampling.LANCZOS)
    size = (letters.width + 2 * GO_ROOM, letters.height + 2 * GO_ROOM)
    grown = Image.new("L", size)
    grown.paste(letters.getchannel("A"), (GO_ROOM, GO_ROOM))
    grown = grown.filter(ImageFilter.MaxFilter(GO_EDGE_GROWN)).filter(ImageFilter.GaussianBlur(GO_EDGE_BLUR))
    out = Image.new("RGBA", size, (0, 0, 0, 255))
    out.putalpha(grown.point(lambda v: v * GO_EDGE))
    out.alpha_composite(letters, (GO_ROOM, GO_ROOM))
    return out


# --- The piece ----------------------------------------------------------------------------------------------------


def make(inputs: Inputs, mod: Path) -> None:
    """Needs pause.py's pictures in `mod` (layouts/pause) and the HD PACK's settings menu (its menu font)."""
    out = mod / "layouts" / "kh2pause"
    out.mkdir(parents=True, exist_ok=True)
    (out / "pause.seqd").write_bytes(kh2_firefly.sequence(inputs))
    save(kh2_firefly.firefly(inputs), out / "firefly.png")
    pictures = mod / "layouts" / "pause"
    fonts = {lang: pause.Font(pause.settings(inputs, lang)) for lang in LANGS}
    rows = shared_rows(fonts)
    for lang in LANGS:
        save(sheet(inputs, lang), out / lang / "sheet.png")
        for key in LINES:
            save(line_words(Image.open(pictures / lang / f"{key}.png")), out / lang / f"{key}.png")
        save(question(Image.open(pictures / lang / "question.png")), out / lang / "question.png")
        for name, text in GAME_OVER[lang].items():
            save(game_over_word(fonts[lang], text, rows), out / lang / f"{name}.png")
