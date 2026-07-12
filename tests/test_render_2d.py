from __future__ import annotations

import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / "tools"
    / "render_2d"
    / "render_2d.py"
)
SPEC = importlib.util.spec_from_file_location("render_2d", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
render_2d = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = render_2d
SPEC.loader.exec_module(render_2d)


def nitro_file(file_magic: bytes, section_magic: bytes, section_body: bytes) -> bytes:
    section = section_magic + struct.pack("<I", 8 + len(section_body)) + section_body
    file_size = 0x10 + len(section)
    header = (
        file_magic
        + struct.pack("<H", 0xFEFF)
        + struct.pack("<H", 0x0100)
        + struct.pack("<I", file_size)
        + struct.pack("<H", 0x10)
        + struct.pack("<H", 1)
    )
    return header + section


def make_nclr_4bpp_16_banks() -> bytes:
    colors = bytearray()
    for bank in range(16):
        for color_index in range(16):
            red = (bank + color_index) & 0x1F
            green = (bank * 2 + color_index) & 0x1F
            blue = (bank * 3 + color_index) & 0x1F
            value = red | (green << 5) | (blue << 10)
            colors += struct.pack("<H", value)

    # Stored offset 0x10 is relative to the section body, not the section start.
    body = (
        struct.pack("<I", 3)          # 4bpp depth code
        + struct.pack("<I", 0)
        + struct.pack("<I", len(colors))
        + struct.pack("<I", 0x10)
        + colors
    )
    return nitro_file(b"RLCN", b"TTLP", body)


def make_ncgr_atlas_2x1() -> bytes:
    tile_0 = bytes([0x11] * 32)
    tile_1 = bytes([0x22] * 32)
    tile_data = tile_0 + tile_1

    # Height=1 tile, width=2 tiles. Stored offset 0x18 is relative to body.
    body = (
        struct.pack("<H", 1)
        + struct.pack("<H", 2)
        + struct.pack("<I", 3)        # 4bpp
        + struct.pack("<I", 0)
        + struct.pack("<I", 0)
        + struct.pack("<I", len(tile_data))
        + struct.pack("<I", 0x18)
        + tile_data
    )
    return nitro_file(b"RGCN", b"RAHC", body)


def make_trivial_nscr_2x1() -> bytes:
    # 16x8 pixels = two map cells. Both entries are zero, which triggers atlas mode.
    entries = struct.pack("<HH", 0, 0)

    # NRCS data starts directly at section offset 0x14.
    body = (
        struct.pack("<H", 16)
        + struct.pack("<H", 8)
        + struct.pack("<I", 0)
        + struct.pack("<I", len(entries))
        + entries
    )
    return nitro_file(b"RCSN", b"NRCS", body)


class Render2DTests(unittest.TestCase):
    def test_auto_mode_uses_sequential_ncgr_atlas(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nclr = root / "palette.nclr"
            ncgr = root / "graphics.ncgr"
            nscr = root / "screen.nscr"
            output = root / "preview.png"

            nclr.write_bytes(make_nclr_4bpp_16_banks())
            ncgr.write_bytes(make_ncgr_atlas_2x1())
            nscr.write_bytes(make_trivial_nscr_2x1())

            result = render_2d.render(
                nclr_path=nclr,
                ncgr_path=ncgr,
                nscr_path=nscr,
                output_path=output,
                transparent_zero=False,
                render_mode="auto",
                requested_palette_bank=1,
            )

            self.assertEqual(result.render_mode, "atlas")
            self.assertEqual((result.width, result.height), (16, 8))
            self.assertEqual(result.bpp, 4)
            self.assertEqual(result.palette_colors, 256)
            self.assertEqual(result.palette_bank, 1)
            self.assertEqual(result.tile_count, 2)
            self.assertEqual(result.map_entries, 2)
            self.assertEqual(result.unique_map_entries, 1)
            self.assertEqual(result.missing_tile_references, 0)

            png = output.read_bytes()
            self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
            width, height = struct.unpack(">II", png[16:24])
            self.assertEqual((width, height), (16, 8))

            metadata = json.loads(
                output.with_suffix(".png.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata["render_mode"], "atlas")
            self.assertEqual(metadata["palette_bank"], 1)

    def test_force_screen_mode_still_uses_nscr(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nclr = root / "palette.nclr"
            ncgr = root / "graphics.ncgr"
            nscr = root / "screen.nscr"
            output = root / "screen.png"

            nclr.write_bytes(make_nclr_4bpp_16_banks())
            ncgr.write_bytes(make_ncgr_atlas_2x1())
            nscr.write_bytes(make_trivial_nscr_2x1())

            result = render_2d.render(
                nclr_path=nclr,
                ncgr_path=ncgr,
                nscr_path=nscr,
                output_path=output,
                transparent_zero=False,
                render_mode="screen",
                requested_palette_bank=1,
            )

            self.assertEqual(result.render_mode, "screen")
            self.assertEqual((result.width, result.height), (16, 8))
            self.assertEqual(result.missing_tile_references, 0)


if __name__ == "__main__":
    unittest.main()
