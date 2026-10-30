"""Text laid out as Pillow lays it out with libraqm (HarfBuzz), on a Pillow without it.

The Menu Overhaul's lettering (the Files pages', the Moogle Shop's, the camp's, the load list's band) was first
drawn by a Pillow built with libraqm: its text advances are the font's own unhinted widths (fractions of a pixel,
the GPOS kerning added) and the characters a shaper hides (the soft hyphen) take no room. The installer's Pillow
lays text out with FreeType alone (whole, hinted widths, no GPOS, the missing glyph's box for a soft hyphen). This
module does what the shaper did, from the font's own tables (cmap, hmtx, GPOS's kern pairs), and draws each glyph
alone with the basic layout, whose glyph pictures are the same, at the pixel the shaper's pen put it. The glyphs
are joined as Pillow's renderer joins them (the larger coverage wins), and the boxes are made as Pillow's
`bounding_box_and_anchors` makes them (the pen line and each glyph's pixel box), for horizontal text and the
default anchor ("la").
"""

from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# The code points a shaper makes invisible and zero-wide (Unicode's Default_Ignorable_Code_Point, the ones
# a Latin text may hold).
HIDDEN = {0x00AD, 0x034F, 0x061C, 0x180E, 0x3164, 0xFEFF, 0xFFA0}
HIDDEN.update(range(0x200B, 0x2010), range(0x202A, 0x202F), range(0x2060, 0x2070), range(0xFE00, 0xFE10))


def _pixel(x: int) -> int:
    """A 26.6 position to a pixel, rounded as FreeType's and Pillow's PIXEL() round it."""
    return (x + 32) >> 6


def _record_size(fmt: int) -> int:
    """A GPOS ValueRecord's size in bytes (a 16-bit field for each bit of its format)."""
    return bin(fmt & 0xFF).count("1") * 2


def _value_record(data: bytes, at: int, fmt: int) -> dict[str, int]:
    """A GPOS ValueRecord's x placement and x advance (font units); the y ones and device tables do not move
    horizontal text."""
    out = {"x": 0, "adv": 0}
    for bit in range(8):
        if fmt & (1 << bit):
            v = struct.unpack_from(">h", data, at)[0]
            if bit == 0:
                out["x"] = v
            elif bit == 2:
                out["adv"] = v
            at += 2
    return out


def _coverage(data: bytes, at: int) -> dict[int, int]:
    fmt, count = struct.unpack_from(">HH", data, at)
    out = {}
    if fmt == 1:
        for i, g in enumerate(struct.unpack_from(">%dH" % count, data, at + 4)):
            out[g] = i
    else:
        for i in range(count):
            start, end, index = struct.unpack_from(">HHH", data, at + 4 + 6 * i)
            for g in range(start, end + 1):
                out[g] = index + g - start
    return out


def _class_def(data: bytes, at: int) -> dict[int, int]:
    fmt = struct.unpack_from(">H", data, at)[0]
    out = {}
    if fmt == 1:
        start, count = struct.unpack_from(">HH", data, at + 2)
        for i, c in enumerate(struct.unpack_from(">%dH" % count, data, at + 6)):
            out[start + i] = c
    else:
        count = struct.unpack_from(">H", data, at + 2)[0]
        for i in range(count):
            start, end, c = struct.unpack_from(">HHH", data, at + 4 + 6 * i)
            for g in range(start, end + 1):
                out[g] = c
    return out


class _PairSubtable:
    """A GPOS PairPos subtable (formats 1 and 2): the first glyph's and the second's values for a pair."""

    def __init__(self, data: bytes, at: int) -> None:
        fmt, cov, self.fmt1, self.fmt2 = struct.unpack_from(">HHHH", data, at)
        self.coverage = _coverage(data, at + cov)
        size1, size2 = _record_size(self.fmt1), _record_size(self.fmt2)
        self.pairs: dict[int, dict[int, tuple]] = {}
        self.classes = None
        if fmt == 1:
            for first, index in self.coverage.items():
                ps = at + struct.unpack_from(">H", data, at + 10 + 2 * index)[0]
                rows = {}
                for i in range(struct.unpack_from(">H", data, ps)[0]):
                    rec = ps + 2 + i * (2 + size1 + size2)
                    second = struct.unpack_from(">H", data, rec)[0]
                    rows[second] = (
                        _value_record(data, rec + 2, self.fmt1),
                        _value_record(data, rec + 2 + size1, self.fmt2),
                    )
                self.pairs[first] = rows
        else:
            cd1, cd2, n1, n2 = struct.unpack_from(">HHHH", data, at + 8)
            self.classes = (_class_def(data, at + cd1), _class_def(data, at + cd2), n1, n2)
            self.records = []
            rec = at + 16
            for _ in range(n1):
                row = []
                for _ in range(n2):
                    row.append((_value_record(data, rec, self.fmt1), _value_record(data, rec + size1, self.fmt2)))
                    rec += size1 + size2
                self.records.append(row)

    def values(self, first: int, second: int) -> tuple | None:
        """The pair's two values, or None when this subtable does not apply to it (the next one is tried)."""
        if first not in self.coverage:
            return None
        if self.classes is None:
            return self.pairs[first].get(second)
        cd1, cd2, n1, n2 = self.classes
        k1, k2 = cd1.get(first, 0), cd2.get(second, 0)
        if k1 >= n1 or k2 >= n2:
            return None
        return self.records[k1][k2]


class _Sfnt:
    """The tables of an OpenType font the shaper reads for Latin text: cmap, hmtx, GDEF's classes, GPOS kern."""

    def __init__(self, path: Path) -> None:
        data = path.read_bytes()
        count = struct.unpack_from(">H", data, 4)[0]
        tables = {}
        for i in range(count):
            tag, _sum, at, _length = struct.unpack_from(">4sIII", data, 12 + 16 * i)
            tables[tag.decode("latin-1")] = at
        self.upem = struct.unpack_from(">H", data, tables["head"] + 18)[0]
        n_metrics = struct.unpack_from(">H", data, tables["hhea"] + 34)[0]
        self.widths = [struct.unpack_from(">H", data, tables["hmtx"] + 4 * i)[0] for i in range(n_metrics)]
        self.cmap = self._cmap(data, tables["cmap"])
        self.marks = set()
        if "GDEF" in tables:
            gdef = tables["GDEF"]
            classes = struct.unpack_from(">H", data, gdef + 4)[0]
            if classes:
                self.marks = {g for g, c in _class_def(data, gdef + classes).items() if c == 3}
        self.kern = self._kern(data, tables["GPOS"]) if "GPOS" in tables else []

    def width(self, glyph: int) -> int:
        return self.widths[min(glyph, len(self.widths) - 1)]

    @staticmethod
    def _cmap(data: bytes, at: int) -> dict[int, int]:
        count = struct.unpack_from(">H", data, at + 2)[0]
        found = {}
        for i in range(count):
            pid, eid, off = struct.unpack_from(">HHI", data, at + 4 + 8 * i)
            found[(pid, eid)] = at + off
        for key in ((3, 10), (0, 4), (3, 1), (0, 3)):
            if key in found:
                return _Sfnt._cmap_subtable(data, found[key])
        raise ValueError("no Unicode cmap")

    @staticmethod
    def _cmap_subtable(data: bytes, at: int) -> dict[int, int]:
        fmt = struct.unpack_from(">H", data, at)[0]
        out = {}
        if fmt == 4:
            segs = struct.unpack_from(">H", data, at + 6)[0] // 2
            ends = struct.unpack_from(">%dH" % segs, data, at + 14)
            starts = struct.unpack_from(">%dH" % segs, data, at + 16 + 2 * segs)
            deltas = struct.unpack_from(">%dh" % segs, data, at + 16 + 4 * segs)
            ranges_at = at + 16 + 6 * segs
            ranges = struct.unpack_from(">%dH" % segs, data, ranges_at)
            for s in range(segs):
                for c in range(starts[s], ends[s] + 1):
                    if c == 0xFFFF:
                        continue
                    if ranges[s] == 0:
                        g = (c + deltas[s]) & 0xFFFF
                    else:
                        g = struct.unpack_from(">H", data, ranges_at + 2 * s + ranges[s] + 2 * (c - starts[s]))[0]
                        g = (g + deltas[s]) & 0xFFFF if g else 0
                    if g:
                        out[c] = g
        elif fmt == 12:
            groups = struct.unpack_from(">I", data, at + 12)[0]
            for i in range(groups):
                start, end, glyph = struct.unpack_from(">III", data, at + 16 + 12 * i)
                for c in range(start, end + 1):
                    out[c] = glyph + c - start
        else:
            raise ValueError(f"cmap format {fmt}")
        return out

    @staticmethod
    def _kern(data: bytes, gpos: int) -> list[tuple[int, list[_PairSubtable]]]:
        """The kern feature's pair lookups for the Latin script's default language (else DFLT's), in order."""
        scripts, features, lookups = struct.unpack_from(">HHH", data, gpos + 4)
        scripts, features, lookups = gpos + scripts, gpos + features, gpos + lookups
        by_tag = {}
        for i in range(struct.unpack_from(">H", data, scripts)[0]):
            tag, off = struct.unpack_from(">4sH", data, scripts + 2 + 6 * i)
            by_tag[tag] = scripts + off
        script = by_tag.get(b"latn", by_tag.get(b"DFLT"))
        if script is None:
            return []
        default = struct.unpack_from(">H", data, script)[0]
        if not default:
            return []
        langsys = script + default
        n = struct.unpack_from(">H", data, langsys + 4)[0]
        indices = struct.unpack_from(">%dH" % n, data, langsys + 6)
        wanted = set()
        for fi in indices:
            tag, off = struct.unpack_from(">4sH", data, features + 2 + 6 * fi)
            if tag == b"kern":
                fl = features + off
                m = struct.unpack_from(">H", data, fl + 2)[0]
                wanted.update(struct.unpack_from(">%dH" % m, data, fl + 4))
        out = []
        for li in sorted(wanted):
            lookup = lookups + struct.unpack_from(">H", data, lookups + 2 + 2 * li)[0]
            kind, flag, count = struct.unpack_from(">HHH", data, lookup)
            subtables = []
            for si in range(count):
                at = lookup + struct.unpack_from(">H", data, lookup + 6 + 2 * si)[0]
                if kind == 9:  # an extension: the subtable behind it
                    kind_ext, off32 = struct.unpack_from(">HI", data, at + 2)
                    if kind_ext != 2:
                        continue
                    at += off32
                elif kind != 2:
                    continue
                subtables.append(_PairSubtable(data, at))
            if subtables:
                out.append((flag, subtables))
        return out


class ShapedFont:
    """A font at a pixel size whose text is laid out as libraqm lays it out (see the module's docstring)."""

    def __init__(self, path: Path, size: int) -> None:
        self.font = ImageFont.truetype(str(path), size, layout_engine=ImageFont.Layout.BASIC)
        self.sfnt = _Sfnt(path)
        upem = self.sfnt.upem
        # FreeType's scale for the size (16.16), and HarfBuzz's font scale and its multiplier for GPOS values
        self.ft_scale = (size * 64 * 65536 + upem // 2) // upem
        hb_scale = (self.ft_scale * upem + (1 << 15)) >> 16
        self.hb_mult = (hb_scale << 16) // upem
        self._ink: dict[str, tuple] = {}

    def _advance(self, glyph: int) -> int:
        """A glyph's advance (26.6) as HarfBuzz reads it from FreeType: unhinted, rounded from 16.16."""
        v = (self.sfnt.width(glyph) * self.ft_scale + 32) // 64
        return (v + (1 << 9)) >> 10

    def _scale(self, v: int) -> int:
        """A GPOS value (font units) in 26.6, rounded as HarfBuzz rounds it."""
        return (v * self.hb_mult + 32768) >> 16

    def advance(self, char: str) -> float:
        """A character's own advance in pixels (no kerning): none for a hidden one."""
        return 0.0 if ord(char) in HIDDEN else self._advance(self.sfnt.cmap.get(ord(char), 0)) / 64

    def layout(self, text: str) -> tuple[list[tuple[str, bool, int, int]], int]:
        """Each character, whether it shows (a hidden one is the shaper's zero-wide invisible glyph), its pen
        position and x offset (26.6); and the pen's end."""
        glyphs = [(c, None if ord(c) in HIDDEN else self.sfnt.cmap.get(ord(c), 0)) for c in text]
        adv = [0 if g is None else self._advance(g) for _, g in glyphs]
        offset = [0] * len(glyphs)
        for flag, subtables in self.sfnt.kern:
            skip = {i for i, (_, g) in enumerate(glyphs) if g is None or (flag & 8 and g in self.sfnt.marks)}
            i = 0
            while i < len(glyphs):
                j = i + 1
                while j < len(glyphs) and j in skip:
                    j += 1
                if i in skip or j >= len(glyphs):
                    i += 1
                    continue
                for sub in subtables:
                    found = sub.values(glyphs[i][1], glyphs[j][1])
                    if found is None:
                        continue
                    v1, v2 = found
                    adv[i] += self._scale(v1["adv"])
                    offset[i] += self._scale(v1["x"])
                    adv[j] += self._scale(v2["adv"])
                    offset[j] += self._scale(v2["x"])
                    if sub.fmt2:
                        i = j
                    break
                i += 1
        out, pen = [], 0
        for (c, g), a, o in zip(glyphs, adv, offset):
            out.append((c, g is not None, pen, o))
            pen += a
        return out, pen

    def getlength(self, text: str) -> float:
        return self.layout(text)[1] / 64

    def _glyph_box(self, c: str) -> tuple[int, int, int, int]:
        """The glyph's pixel box (x min, top, x max, bottom), the top and bottom from the anchor's line.
        The basic layout's box holds the glyph's hinted advance too; where the advance reaches past the
        glyph, its right is its coverage's last column."""
        found = self._ink.get(c)
        if found is None:
            left, top, right, bottom = self.font.getbbox(c)
            if right <= _pixel(round(self.font.getlength(c) * 64)):
                pen = 4 - left
                canvas = Image.new("L", (right - left + 8, bottom - top + 8))
                ImageDraw.Draw(canvas).text((pen, 4 - top), c, font=self.font, fill=255)
                ink = np.asarray(canvas).any(axis=0).nonzero()[0]
                right = int(ink[-1]) + 1 - pen if len(ink) else 0
            found = self._ink[c] = (left, top, right, bottom)
        return found

    def getbbox(self, text: str) -> tuple[int, int, int, int]:
        """The text's box as ImageFont.getbbox gives it: from the pen's line (0 to the pen after each glyph)
        and each glyph's pixel box at its pen; an invisible glyph's box is empty, on the baseline."""
        laid, end = self.layout(text)
        baseline = self.font.getbbox(" ")[1]  # the space's box: empty, on the baseline under the anchor
        x_min = x_max = 0
        top = bottom = baseline
        for i, (c, shown, pen, offset) in enumerate(laid):
            after = laid[i + 1][2] if i + 1 < len(laid) else end
            x_max = max(x_max, _pixel(after))
            if not shown:
                continue
            px = _pixel(pen + offset)
            left, t, right, b = self._glyph_box(c)
            x_min, x_max = min(x_min, px + left), max(x_max, px + right)
            top, bottom = min(top, t), max(bottom, b)
        return x_min, top, x_max, bottom

    def draw(self, image: Image.Image, xy: tuple[int, int], text: str, fill: int = 255) -> None:
        """`text` onto an "L" picture at `xy` (the default anchor), as ImageDraw.text draws it: the glyphs' coverage
        over the text's box (the larger one where two meet), `fill` laid through it."""
        left, top, right, bottom = self.getbbox(text)
        w, h = right - left, bottom - top
        if w <= 0 or h <= 0:
            return
        union = np.zeros((h, w), np.uint8)
        for c, shown, pen, offset in self.layout(text)[0]:
            if not shown:
                continue
            one = Image.new("L", (w, h))
            ImageDraw.Draw(one).text((_pixel(pen + offset) - left, -top), c, font=self.font, fill=255)
            np.maximum(union, np.asarray(one), out=union)
        x0, y0 = xy[0] + left, xy[1] + top
        image.paste(fill, (x0, y0, x0 + w, y0 + h), Image.fromarray(union, "L"))


def ink(path: Path, text: str, size: float) -> Image.Image:
    """`text`'s ink (L, cut to it) at a size in fractions of a pixel, each glyph at its pen position as the shaper
    lays it out: the glyphs' own advances unhinted (their widths at the size, in 26.6), each pen rounded to a pixel."""
    upem = _Sfnt(path).upem
    at_em = ImageFont.truetype(str(path), upem)  # a pixel a font unit: its advances the glyphs' widths
    font = ImageFont.truetype(str(path), size)
    im = Image.new("L", (3000, 800), 0)
    draw = ImageDraw.Draw(im)
    pen = 0
    for ch in text:
        draw.text((50 + _pixel(pen), 50), ch, font=font, fill=255)
        pen += round(at_em.getlength(ch) * size * 64 / upem)
    return im.crop(im.getbbox())
