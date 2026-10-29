"""Synthetic extraction tests: assembled glyphs, native palette/masks, and preservation of edits."""

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import minimap_icons as icons  # noqa: E402


class MinimapAtlas(unittest.TestCase):
    def test_remastered_radar_alpha_and_preservation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            file = root / "source/remastered/radar.tm2/-0.dds"
            file.parent.mkdir(parents=True)
            picture = Image.new("RGBA", (128, 32), (120, 80, 40, 96))
            picture.putpixel((0, 0), (0, 0, 0, 0))
            picture.save(file)
            output = root / "mod"
            icons.make_radar(root / "source", output)
            self.assertEqual((output / "radar.dds").read_bytes(), file.read_bytes())
            with Image.open(output / "radar.dds") as copied:
                self.assertEqual(copied.getpixel((0, 0))[3], 0)
                self.assertEqual(copied.getpixel((1, 1))[3], 96)
            (output / "radar.dds").write_bytes(b"custom mask")
            icons.make_radar(root / "source", output)
            self.assertEqual((output / "radar.dds").read_bytes(), b"custom mask")
            Image.new("RGBA", (64, 64)).save(file)
            with self.assertRaises(ValueError):
                icons.make_radar(root / "source", root / "bad")

    def test_preserves_standalone_player_and_character_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output = root / "existing", root / "staged"
            source.mkdir()
            config = {
                "texture": "minimap.png",
                "players": {"1": {"sprite": "key.png"}},
                "characters": {"Axel": {"sprite": "axel.DDS"}},
            }
            (source / "minimap.json").write_text(json.dumps(config))
            for name in ("minimap.png", "key.png", "axel.DDS", "radar.dds"):
                (source / name).write_bytes(name.encode())
            icons.preserve(source, output)
            for name in ("minimap.json", "minimap.png", "key.png", "axel.DDS", "radar.dds"):
                self.assertEqual((source / name).read_bytes(), (output / name).read_bytes())
            config["players"]["1"]["sprite"] = "../bad.png"
            (source / "minimap.json").write_text(json.dumps(config))
            with self.assertRaises(ValueError):
                icons.preserve(source, output)

    def test_palette_masks_assembly_and_preservation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = root / "data/nitrofs/UI/btl"
            data.mkdir(parents=True)
            raw = bytearray(0x430)
            struct.pack_into("<II", raw, 0x28, 1024, 0x18)
            raw[0x30:] = bytes([0x77]) * 1024
            for tile, value in enumerate((0x11, 0x22, 0x44, 0x77)):
                raw[0x30 + 0x2E0 + tile * 32 : 0x30 + 0x300 + tile * 32] = bytes([value]) * 32
            (data / "bm_lo_bg002.NCGR.z").write_bytes(raw)
            (data / "bm_lo_bg000.NCLR.z").write_bytes(b"synthetic palette")
            palette = [(i, i // 2, 255 - i) for i in range(256)]
            output = root / "mod"
            with (
                patch.object(icons.ds.dd, "decompress_nitro", side_effect=lambda data: ("raw", data)),
                patch.object(icons.ds, "palette", return_value=(palette, 16)),
            ):
                config = icons.make(root / "data", output)
                self.assertTrue(config["kh2map"])
                self.assertEqual(config["map_icon_scale"], 1)
                with Image.open(output / "minimap.png") as sheet:
                    self.assertEqual(sheet.size, (256, 128))
                    for player in range(4):
                        r = config["sprites"][f"player{player + 1}_0"]
                        self.assertEqual(sheet.getpixel((r["x"], r["y"])), (*palette[13 * 16 + player + 2], 255))
                    r = config["sprites"]["emblem2"]
                    x, y = r["x"], r["y"]
                    self.assertEqual(sheet.getpixel((x, y)), (*palette[13 * 16 + 1], 255))
                    self.assertEqual(sheet.getpixel((x + 8, y)), (*palette[13 * 16 + 2], 255))
                    self.assertEqual(sheet.getpixel((x, y + 8))[3], 0)
                    self.assertEqual(sheet.getpixel((x + 8, y + 8)), (*palette[13 * 16 + 3], 255))
                self.assertFalse(config["players"]["1"]["smooth_rotation"])
                self.assertFalse(config["characters"]["Axel"]["enabled"])
                self.assertTrue(
                    all(f"marker_{offset:03x}" in config["sprites"] for offset in (0x120, 0x280, 0x360, 0x380, 0x3A0))
                )
                (output / "minimap.json").write_text(json.dumps({"custom": True}))
                (output / "minimap.png").write_bytes(b"custom art")
                icons.make(root / "data", output)
                self.assertEqual((output / "minimap.png").read_bytes(), b"custom art")
                self.assertEqual(json.loads((output / "minimap.json").read_text()), {"custom": True})


if __name__ == "__main__":
    unittest.main()
