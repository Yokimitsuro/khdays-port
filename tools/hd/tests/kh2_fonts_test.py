"""KH2 font generation on synthetic BAR, NFTR and PNG inputs; no game data is bundled."""

import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import kh2_fonts as k  # noqa: E402


def bar() -> bytes:
    # Reverse the directory order and move both tables away from the real file's offsets.
    data = bytearray(1024)
    struct.pack_into("<4sI", data, 0, b"BAR\x01", 3)
    for i, (name, offset) in enumerate((("evt", 96), ("unused", 64), ("sys", 512))):
        if name == "unused":
            struct.pack_into("<I4sII", data, 16 + i * 16, 25, b"skip", offset, 16)
            continue
        size = k.FONTS[name][5]
        struct.pack_into("<I4sII", data, 16 + i * 16, 2, name.encode(), offset, size)
        data[offset : offset + size] = bytes((i + j % 23 for j in range(size)))
    return bytes(data)


def nftr() -> bytes:
    # Five glyphs, CGLP padding, then chained direct, table-with-hole and scan maps.
    data = bytearray(44)
    data[:6] = b"RTFN\xff\xfe"
    struct.pack_into("<4sI", data, 16, b"FNIF", 28)
    data[31] = 1
    struct.pack_into("<III", data, 32, 52, 0, 96)
    data += struct.pack("<4sIBBHbBBB", b"PLGC", 44, 7, 5, 5, 4, 7, 1, 0) + bytes(28)
    sections = (
        (0x41, 0x42, 0, 120, struct.pack("<H", 0)),
        (0x43, 0x45, 1, 148, struct.pack("<3H", 2, 0xFFFF, 3)),
        (0x20, 0xE000, 2, 0, struct.pack("<H6H", 3, 0x20, 4, 0x2161, 3, 0xE000, 2)),
    )
    for first, last, method, nxt, body in sections:
        size = (20 + len(body) + 3) & ~3
        data += struct.pack("<4sIHHHHI", b"PAMC", size, first, last, method, 0, nxt)
        data += body + bytes(size - 20 - len(body))
    struct.pack_into("<IHH", data, 8, len(data), 16, 5)
    return bytes(data)


def lz10_literal(data: bytes) -> bytes:
    return (
        bytes([0x10])
        + len(data).to_bytes(3, "little")
        + b"".join(b"\0" + data[i : i + 8] for i in range(0, len(data), 8))
    )


class Reading(unittest.TestCase):
    def test_bar_offsets_and_native_advances(self):
        tables = k.advances(bar())
        self.assertEqual(tables["sys"], [2 + i % 23 for i in range(280)])
        self.assertEqual(tables["evt"], [i % 23 for i in range(336)])

    def test_invalid_bar(self):
        for at, fmt, value in (
            (0, "<I", 0),
            (4, "<I", 1000),
            (16, "<I", 25),
            (24, "<I", 0),
            (28, "<I", 335),
            (48, "<I", 25),
        ):
            data = bytearray(bar())
            struct.pack_into(fmt, data, at, value)
            with self.subTest(at=at), self.assertRaises(ValueError):
                k.advances(bytes(data))
        with self.assertRaises(ValueError):
            k.advances(bar()[:700])

    def test_nftr_all_methods_compression_and_padding(self):
        expected = {0x41: 0, 0x42: 1, 0x43: 2, 0x45: 3, 0x20: 4, 0x2161: 3, 0xE000: 2}
        self.assertEqual(k.nftr_map(nftr()), expected)
        self.assertEqual(k.nftr_map(lz10_literal(nftr())), expected)

    def test_invalid_nftr(self):
        for at, fmt, value in (
            (31, "<B", 2),
            (32, "<I", 0),
            (40, "<I", 999999),
            (104, "<H", 9),
            (108, "<I", 96),
            (108, "<I", 0xFFFFFFF0),
            (98, "<H", 0),
            (164, "<H", 999),
        ):
            data = bytearray(nftr())
            struct.pack_into(fmt, data, at, value)
            with self.subTest(at=at), self.assertRaises(ValueError):
                k.nftr_map(bytes(data))
        with self.assertRaises(ValueError):
            k.nftr_map(nftr()[:-2])


class Generation(unittest.TestCase):
    def test_metrics_geometry_and_requested_settings(self):
        for name, (font, scale, offset) in k.REPLACEMENTS.items():
            w, h, aw, ah, cols, count, space = k.FONTS[font]
            tables = k.advances(bar())
            doc = k.definition(name, tables[font], k.nftr_map(nftr()))
            self.assertEqual((doc["scale"], doc["baseline_offset"]), (scale, offset))
            self.assertEqual((doc["cell_width"], doc["cell_height"], doc["baseline"]), (w, h, h))
            self.assertEqual(doc["source"], f"text/{name}.nftr")
            self.assertEqual(doc["kerning"], [])
            self.assertEqual([g["index"] for g in doc["glyphs"]], list(range(count)))
            for i, g in enumerate(doc["glyphs"]):
                self.assertEqual((g["x"], g["y"], g["w"], g["h"]), (i % cols * w, i // cols * h, w, h))
                self.assertLessEqual(g["x"] + w, aw)
                self.assertLessEqual(g["y"] + h, ah)
                self.assertEqual(g["advance"], tables[font][i] if i < count - 1 else space)
                self.assertEqual((g["left"], g["width"]), (0, w))
            self.assertEqual(doc["glyphs"][14]["advance"] * scale, tables[font][14] * scale)

    def test_special_characters_and_unmapped_holes(self):
        chars = {ord(c): i for i, c in enumerate("ABCD E$*\ue000ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩⅪⅫＩ０~｡")}
        doc = k.definition("font_eu_10", list(range(256)) + [7] * 80, chars)
        mapping = {m["codepoint"]: m["glyph"] for m in doc["charmap"]}
        self.assertEqual([mapping[ord(c)] for c in "ABCDE"], [14, 15, 16, 17, 18])
        self.assertTrue(all(ord(c) not in mapping for c in "$*\ue000"))
        self.assertEqual([mapping[ord(c)] for c in "ⅠⅡⅢⅣⅤⅥⅦⅧⅨⅩⅪⅫ"], [84, 85, 86, 87, 88, 88, 90, 93, 92, 93, 100, 101])
        self.assertEqual([mapping[ord(c)] for c in "Ｉ０~｡"], [22, 112, 54, 46])
        self.assertEqual(mapping[0x20], 335)
        # Do not interpret KH2 control bytes, Roman-numeral strings or unused slots as text.
        self.assertNotIn(0x01, k.international_map())
        self.assertNotIn(0x216C, k.international_map())
        self.assertNotIn(56, mapping.values())

    def test_french_ligatures(self):
        # KH2's pictures at bytes 0xB4 and 0xB5 are Œ and œ; Days's Æ and æ have none of their own
        chars = {ord(c): i for i, c in enumerate("cœurŒÆæ")}
        doc = k.definition("font_eu_10", list(range(256)) + [7] * 80, chars)
        mapping = {m["codepoint"]: m["glyph"] for m in doc["charmap"]}
        self.assertEqual((mapping[ord("Œ")], mapping[ord("œ")]), (0xB4 - 0x20, 0xB5 - 0x20))
        self.assertTrue(all(ord(c) not in mapping for c in "Ææ"))

    def test_resource_specific_symbol_exclusions_and_numerals(self):
        characters = {code: i for i, code in enumerate((0x41, 0xFF2C, 0xFF42, 0x2165, 0x2167))}
        for name, (font, _, _) in k.REPLACEMENTS.items():
            doc = k.definition(name, [12] * k.FONTS[font][5], characters)
            mapped = {m["codepoint"]: m["glyph"] for m in doc["charmap"]}
            excluded = [] if name == "font_eu_10all" else [0xFF2C]
            if font == "sys" and name != "font_eu_10all":
                excluded.append(0xFF42)
            self.assertEqual(doc["original_codepoints"], excluded)
            self.assertTrue(all(code not in mapped for code in excluded))
            self.assertEqual(mapped[0x41], 14)
            self.assertEqual((mapped[0x2165], mapped[0x2167]), (88, 93))
            if name == "font_eu_10all":
                self.assertEqual((mapped[0xFF2C], mapped[0xFF42]), (25, 123))

    def test_fractional_evt_spacing_is_not_baked_into_json(self):
        text = "pipsqueak."
        tables = k.advances(bar())
        doc = k.definition("font_eu_10", tables["evt"], {ord(c): i for i, c in enumerate(text)})
        doc = json.loads(json.dumps(doc))
        mapping = {m["codepoint"]: m["glyph"] for m in doc["charmap"]}
        native = [doc["glyphs"][mapping[ord(c)]]["advance"] for c in text]
        self.assertEqual(native, [tables["evt"][k.international_map()[ord(c)]] for c in text])
        exact = sum(native) * 0.35
        self.assertNotAlmostEqual(exact, sum(round(n * 0.35) for n in native))
        self.assertAlmostEqual(sum(n * doc["scale"] for n in native), exact)

    def test_override_defaults(self):
        config = k.overrides()
        self.assertEqual(config["version"], 1)
        scopes = config["scopes"]
        self.assertEqual(len(scopes), 17)
        self.assertEqual(scopes["field.dialogue.speaker"]["text/font_eu_10.nftr"], "text/font_eu_10all.nftr")
        self.assertNotIn("field.dialogue", scopes)
        self.assertNotIn("field.command", scopes)
        self.assertEqual(scopes["shop"]["text/font_eu_08.nftr"], "text/font_eu_10all.nftr")
        self.assertEqual(scopes["field.obtained"]["text/font_eu_08s.nftr"], "text/font_eu_10all.nftr")
        for scope, routes in scopes.items():
            expected = (
                "text/font_eu_10.nftr"
                if ".mission_select." in scope or ".screen.7." in scope
                else "text/font_eu_10all.nftr"
            )
            self.assertTrue(all(target == expected for target in routes.values()))
            self.assertTrue(
                all(target.removeprefix("text/").removesuffix(".nftr") in k.REPLACEMENTS for target in routes.values())
            )

    def test_conversion_and_png_resolution_independence(self):
        for font in k.FONTS:
            w, h, aw, ah, cols, count, _ = k.FONTS[font]
            source = Image.new("RGBA", (aw, ah), (124, 124, 124, 255))
            samples = [(0, 255), (54, 255), (89, 255), (124, 128), (255, 0)]
            for i, (gray, alpha) in enumerate(samples):
                source.putpixel((i, 0), (gray, gray, gray, alpha))
            converted = k.convert(source, font)
            for i, (gray, alpha) in enumerate(samples):
                coverage = round(255 * min(1, max(0, (gray - 54) / 70)) ** 1.42) * alpha // 255
                self.assertEqual(converted.getpixel((i, 0)), (255, 255, 255, coverage))
            self.assertEqual(converted.getpixel(((count - 1) % cols * w, (count - 1) // cols * h)), (255, 255, 255, 0))
            for factor in (2, 4):
                larger = k.convert(source.resize((aw * factor, ah * factor), Image.Resampling.NEAREST), font)
                self.assertEqual(larger.tobytes(), converted.resize(larger.size, Image.Resampling.NEAREST).tobytes())
            with self.assertRaises(ValueError):
                k.convert(source.resize((aw * 2, ah)), font)

    def test_complete_generation_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            kh2, data, out = root / "kh2", root / "data", root / "mods" / "HD PACK" / "fonts" / "text"
            info = kh2 / "original" / k.FONTINFO
            info.parent.mkdir(parents=True)
            info.write_bytes(bar())
            sheets = kh2 / "remastered" / k.FONTIMAGE
            sheets.mkdir(parents=True)
            for i, font in enumerate(k.FONTS):
                _, _, aw, ah, *_ = k.FONTS[font]
                Image.new("RGBA", (aw * 2, ah * 2), (124, 124, 124, 255)).save(sheets / f"US_fontimage_bar{i}.png")
            (data / "nitrofs" / "text").mkdir(parents=True)
            for name in k.REPLACEMENTS:
                (data / "nitrofs" / "text" / f"{name}.nftr").write_bytes(nftr())
            k.make(kh2, data, out)
            first = {p.name: p.read_bytes() for p in out.iterdir()}
            self.assertEqual(len(first), 10)
            self.assertEqual(json.loads((out.parent / "mappings.json").read_text()), k.overrides())
            self.assertEqual(json.loads((out.parent / "layouts.json").read_text()), k.layouts())
            self.assertEqual(
                k.layouts()["scopes"],
                {
                    "camp.panels.inventory.counter": "font_metrics",
                    "title.load.play_time": "font_metrics",
                    "camp.save.play_time": "font_metrics",
                    "field.command.items.name": {"mode": "fit", "max_width": 61},
                    "field.obtained.name": {"mode": "fit", "max_width": 64},
                },
            )
            for name in k.REPLACEMENTS:
                doc = json.loads(first[f"{name}.nftr.json"])
                self.assertEqual(doc["source"], f"text/{name}.nftr")
                self.assertNotIn(0xE000, {m["codepoint"] for m in doc["charmap"]})
            k.make(kh2, data, out)
            self.assertEqual(first, {p.name: p.read_bytes() for p in out.iterdir()})
            (data / "nitrofs" / "text" / "font_eu_10s.nftr").write_bytes(b"broken")
            with self.assertRaises(ValueError):
                k.make(kh2, data, root / "failed")
            self.assertFalse((root / "failed").exists())

    def test_cli_missing_inputs_are_actionable(self):
        with tempfile.TemporaryDirectory() as temp:
            result = subprocess.run([sys.executable, k.__file__, temp, "--data", temp], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("Re-extract msg/us/fontinfo.bar", result.stderr)
            self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
