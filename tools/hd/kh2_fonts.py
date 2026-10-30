#!/usr/bin/env python3
"""Generate Days PNG font replacements from the player's KH2 extraction and Days NFTRs.
  python tools/hd/kh2_fonts.py KH2_HED_OUT --data DATA_EXTRACTED --mod "HD PACK"
See docs/HD_FONTS.md for the encoding, spacing and conversion policy.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

from PIL import Image, ImageChops

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from decompress_data import decompress_nitro  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
FONTINFO = "msg/us/fontinfo.bar"
FONTIMAGE = "msg/us/fontimage.bar"
# width, height, atlas width, atlas height, columns, glyph count, space advance
FONTS = {"sys": (18, 24, 512, 256, 28, 280, 6), "evt": (24, 32, 512, 512, 21, 336, 8)}
REPLACEMENTS = {
    "font_eu_08": ("sys", 0.5, 2),
    "font_eu_08s": ("sys", 0.5, 2),
    "font_eu_10": ("evt", 0.35, 0),
    "font_eu_10s": ("evt", 0.35, 0),
    "font_eu_10all": ("sys", 0.41, 0),
}


def span(data: bytes, at: int, size: int) -> bytes:
    if at < 0 or size < 0 or at > len(data) - size:
        raise ValueError("font section extends past end of file")
    return data[at : at + size]


def unpack(fmt: str, data: bytes, at: int):
    return struct.unpack(fmt, span(data, at, struct.calcsize(fmt)))


def advances(data: bytes) -> dict[str, list[int]]:
    """BAR type-2 entries named sys/evt; read directory offsets, not fixed file offsets."""
    if span(data, 0, 4) != b"BAR\x01":
        raise ValueError("fontinfo.bar: not a BAR v1 archive")
    count = unpack("<I", data, 4)[0]
    span(data, 16, count * 16)
    result = {}
    for i in range(count):
        kind, name, offset, size = unpack("<I4sII", data, 16 + i * 16)
        name = name.rstrip(b"\0").decode("ascii")
        if name not in FONTS:
            continue
        if name in result or kind != 2 or size != FONTS[name][5] or offset < 16 + count * 16:
            raise ValueError(f"fontinfo.bar: invalid or duplicate {name} advance table")
        result[name] = list(span(data, offset, size))
    if result.keys() != FONTS.keys():
        raise ValueError("fontinfo.bar: missing sys or evt table; use KH2's msg/us/fontinfo.bar")
    return result


def nftr_map(data: bytes) -> dict[int, int]:
    """Read Days' UTF-16 NFTR CMAP chain, retaining its original character/glyph identities."""
    if data[:1] in (b"\x10", b"\x11"):
        _, data = decompress_nitro(data)
    if span(data, 0, 6) != b"RTFN\xff\xfe" or span(data, 16, 4) != b"FNIF":
        raise ValueError("not a little-endian Days NFTR")
    if span(data, 31, 1) != b"\x01":
        raise ValueError("expected a UTF-16 European Days font")
    cglp, _, at = unpack("<III", data, 32)
    if cglp < 8 or span(data, cglp - 8, 4) != b"PLGC":
        raise ValueError("NFTR: invalid CGLP pointer")
    size = unpack("<I", data, cglp - 4)[0]
    span(data, cglp - 8, size)
    cell_size = unpack("<H", data, cglp + 2)[0]
    if size < 16 or not cell_size:
        raise ValueError("NFTR: invalid glyph storage")
    # CGLP may end with alignment bytes after its last complete cell.
    glyphs = (size - 16) // cell_size
    if not 1 <= glyphs <= 65536:
        raise ValueError("NFTR: invalid glyph count")
    result, visited = {}, set()
    while at:
        if at < 8 or at in visited or span(data, at - 8, 4) != b"PAMC":
            raise ValueError("NFTR: invalid or cyclic CMAP chain")
        visited.add(at)
        size = unpack("<I", data, at - 4)[0]
        section = span(data, at - 8, size)
        first, last, method, _, at = unpack("<HHHHI", section, 8)
        if first > last:
            raise ValueError("NFTR: reversed CMAP range")
        if method == 0:
            start = unpack("<H", section, 20)[0]
            pairs = [(code, start + code - first) for code in range(first, last + 1)]
        elif method == 1:
            indices = unpack(f"<{last - first + 1}H", section, 20)
            pairs = [(first + i, glyph) for i, glyph in enumerate(indices) if glyph != 0xFFFF]
        elif method == 2:
            n = unpack("<H", section, 20)[0]
            pairs = [unpack("<HH", section, 22 + i * 4) for i in range(n)]
        else:
            raise ValueError("NFTR: unsupported CMAP method")
        for code, glyph in pairs:
            if not first <= code <= last or glyph >= glyphs or 0xD800 <= code <= 0xDFFF:
                raise ValueError("NFTR: invalid character or glyph index")
            if code in result and result[code] != glyph:
                raise ValueError("NFTR: conflicting character maps")
            result[code] = glyph
    if not result:
        raise ValueError("NFTR: empty character map")
    return result


def international_map() -> dict[int, int]:
    """KH2 international byte-to-Unicode facts; atlas slot is byte minus 0x20."""
    groups = {
        0x20: "⬛０１２３４５６７８９+−ₓABCDEFGHIJKLMNOPQRSTUVWXYZ!?%/※、。.,;:…-–〜'",
        0x5A: "()「」『』“”[]<>-–⤷♩⇾⇽◯✕",
        0x7F: "αβγ⭑⭒",
        0x86: "&#®▴▾▸◂°♪",
        # 0xB4 and 0xB5 are Œ and œ in both sheets (French's cœur), not Æ and æ
        0x90: "0123456789abcdefghijklmnopqrstuvwxyzŒœßàáâäèéêëìíîïñòóôöùúûüº—»«",
        0xD0: "ÀÁÂÄÈÉÊËÌÍÎÏÑÒÓÔÖÙÚÛÜ¡¿Çç‛’`´\"'★☆■□▲△●○♪♫→←↑↓・❤",
    }
    result = {}
    for byte, text in groups.items():
        for i, char in enumerate(text):
            result.setdefault(ord(char), byte + i - 0x20)
    # Roman numerals are individual pictures, not sequences of Latin letters.
    for i, slot in enumerate((84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 100, 101)):
        result[0x2160 + i] = slot
    for code in range(0xFF01, 0xFF5F):
        if code - 0xFEE0 in result:
            result[code] = result[code - 0xFEE0]
    for alias, char in {"~": "〜", "～": "〜", "‐": "-", "｡": "。", "｢": "「", "｣": "」", "､": "、", "･": "・"}.items():
        result[ord(alias)] = result[ord(char)]
    return result


def convert(image: Image.Image, font: str) -> Image.Image:
    """The supplied bright conversion: white RGB, luminance-derived alpha times source alpha."""
    width, height, aw, ah, columns, count, _ = FONTS[font]
    if image.width % aw or image.height % ah or image.width // aw != image.height // ah:
        raise ValueError(f"{font}: PNG must be a uniform integer multiple of {aw}x{ah}")
    image = image.convert("RGBA")
    lut = [round(255 * min(1, max(0, (v - 54) / 70)) ** 1.42) for v in range(256)]
    alpha = ImageChops.multiply(image.convert("L").point(lut), image.getchannel("A"))
    out = Image.new("RGBA", image.size, (255, 255, 255, 0))
    out.putalpha(alpha)
    # Spaces use a reserved blank slot; no dependency on unused source texture padding.
    k = image.width // aw
    x, y = (count - 1) % columns * width * k, (count - 1) // columns * height * k
    out.paste((255, 255, 255, 0), (x, y, x + width * k, y + height * k))
    return out


def definition(name: str, table: list[int], characters: dict[int, int]) -> dict:
    font, scale, offset = REPLACEMENTS[name]
    w, h, aw, ah, columns, count, space = FONTS[font]
    if len(table) != count:
        raise ValueError(f"{font}: wrong number of advance values")
    glyphs = [
        {
            "index": i,
            "x": i % columns * w,
            "y": i // columns * h,
            "w": w,
            "h": h,
            "left": 0,
            "width": w,
            "advance": advance,
        }
        for i, advance in enumerate(table)
    ]
    glyphs[-1]["advance"] = space
    mapping = international_map()
    original = []
    if name != "font_eu_10all":
        original.append(0xFF2C)  # Days' circled L, not a fullwidth Latin letter.
    if name in ("font_eu_08", "font_eu_08s"):
        original.append(0xFF42)  # Days' SP symbol, not a fullwidth lowercase b.
    for code in original:
        mapping.pop(code, None)
    mapping.update({0x2165: 88, 0x2167: 93})
    mapping.update({code: count - 1 for code in (0x20, 0xA0, 0x3000)})
    return {
        "source": f"text/{name}.nftr",
        "format": "NFTR",
        "cell_width": w,
        "cell_height": h,
        "baseline": h,
        "baseline_offset": offset,
        "max_width": w,
        "line_feed": h,
        "scale": scale,
        "atlas": {"width": aw, "height": ah, "columns": columns, "padding": 0},
        "glyphs": glyphs,
        "charmap": [{"codepoint": code, "glyph": mapping[code]} for code in sorted(characters) if code in mapping],
        "original_codepoints": [code for code in original if code in characters],
        "kerning": [],
    }


def overrides() -> dict:
    """The player's approved font-routing defaults, without artwork or extracted font data."""
    system = {f"text/{name}.nftr": "text/font_eu_10all.nftr" for name in ("font_eu_10", "font_eu_10s")}
    event = {source: "text/font_eu_10.nftr" for source in system}
    scopes = {
        scope: system.copy()
        for scope in (
            "camp",
            "field.tutorial",
            "field.dialogue.speaker",
            "field.hud.information",
            "field.hud.status",
            "field.hud.area",
            "mission.results",
        )
    }
    for mode in ("story", "mission"):
        for section in ("summary", "objectives", "rewards"):
            scopes[f"camp.{mode}.screen.1.mission_select.{section}"] = event.copy()
    for mode in (0, 1):
        scopes[f"camp.story.screen.7.mode.{mode}"] = event.copy()
    item = {f"text/{name}.nftr": "text/font_eu_10all.nftr" for name in ("font_eu_08", "font_eu_08s")}
    scopes["shop"] = {"text/font_eu_10.nftr": "text/font_eu_10all.nftr", **item}
    scopes["field.obtained"] = item
    return {"version": 1, "scopes": scopes}


def layouts() -> dict:
    """Explicit numeric corrections and independent item-name fitting for this pack."""
    return {
        "version": 1,
        "scopes": {
            "camp.panels.inventory.counter": "font_metrics",
            "title.load.play_time": "font_metrics",
            "camp.save.play_time": "font_metrics",
            "field.command.items.name": {"mode": "fit", "max_width": 61},
            "field.obtained.name": {"mode": "fit", "max_width": 64},
        },
    }


def make(kh2: Path, data: Path, out: Path) -> None:
    tables = advances((kh2 / "original" / FONTINFO).read_bytes())
    pictures = {}
    for i, font in enumerate(FONTS):
        with Image.open(kh2 / "remastered" / FONTIMAGE / f"US_fontimage_bar{i}.png") as image:
            pictures[font] = convert(image, font)
    documents = {}
    for name, (font, _, _) in REPLACEMENTS.items():
        path = data / "nitrofs" / "text" / f"{name}.nftr"
        documents[name] = definition(name, tables[font], nftr_map(path.read_bytes()))
        if not documents[name]["charmap"]:
            raise ValueError(f"{path}: no supported KH2 characters")
    out.mkdir(parents=True, exist_ok=True)
    for name, doc in documents.items():
        font = REPLACEMENTS[name][0]
        pictures[font].save(out / f"{name}.nftr.png")
        (out / f"{name}.nftr.json").write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
        print(f"{name}: {len(doc['charmap'])} supported characters, {font} scale {doc['scale']}")
    (out.parent / "mappings.json").write_text(json.dumps(overrides(), indent=2) + "\n", encoding="utf-8")
    (out.parent / "layouts.json").write_text(json.dumps(layouts(), indent=2) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("kh2", type=Path, help="KH2's kh2_first.hed_out extraction")
    ap.add_argument("--data", type=Path, help="the port's extracted Days data (data/extracted/<hash>)")
    ap.add_argument("--mod", default="HD PACK")
    args = ap.parse_args()
    data = args.data
    if data is None:
        found = sorted((ROOT / "data" / "extracted").glob("*/nitrofs"))
        if not found:
            ap.error("no extracted Days data: give --data data/extracted/<hash>")
        data = found[0].parent
    try:
        make(args.kh2, data, ROOT / "mods" / args.mod / "fonts" / "text")
    except (OSError, ValueError) as error:
        ap.exit(
            1,
            f"KH2 font generation failed: {error}\nRe-extract msg/us/fontinfo.bar and msg/us/fontimage.bar from your KH2 copy and check --data.\n",
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
