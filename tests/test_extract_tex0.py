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
    / "extract_tex0"
    / "extract_tex0.py"
)
SPEC = importlib.util.spec_from_file_location("extract_tex0", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
extract_tex0 = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = extract_tex0
SPEC.loader.exec_module(extract_tex0)


def make_dictionary(
    names: list[str],
    entries: list[bytes],
    unit_size: int,
) -> bytes:
    count = len(names)
    nodes_size = (count + 1) * 4
    entry_header_offset = 8 + nodes_size
    names_relative_offset = 4 + count * unit_size
    dictionary_size = (
        entry_header_offset
        + names_relative_offset
        + count * 16
    )

    data = bytearray(dictionary_size)
    data[0] = 0
    data[1] = count
    struct.pack_into("<H", data, 2, dictionary_size)
    struct.pack_into("<H", data, 4, 8)
    struct.pack_into("<H", data, 6, entry_header_offset)

    entry_header = entry_header_offset
    struct.pack_into("<H", data, entry_header, unit_size)
    struct.pack_into("<H", data, entry_header + 2, names_relative_offset)

    entries_start = entry_header + 4
    for index, entry in enumerate(entries):
        data[
            entries_start + index * unit_size:
            entries_start + (index + 1) * unit_size
        ] = entry

    names_start = entry_header + names_relative_offset
    for index, name in enumerate(names):
        encoded = name.encode("ascii")[:15]
        data[
            names_start + index * 16:
            names_start + index * 16 + len(encoded)
        ] = encoded

    return bytes(data)


def make_bmd0_with_one_texture() -> bytes:
    texture_name = "sample"
    palette_name = "sample_pl"

    # 8x8 PLTT256: texture format 4, S/T exponents 0.
    texture_parameter = 4 << 26
    texture_entry = struct.pack("<IHH", texture_parameter, 8, 0x8001)
    palette_entry = struct.pack("<HH", 0, 0)

    texture_dict = make_dictionary(
        [texture_name],
        [texture_entry],
        8,
    )
    palette_dict = make_dictionary(
        [palette_name],
        [palette_entry],
        4,
    )

    texture_dict_offset = 0x3C
    palette_dict_offset = texture_dict_offset + len(texture_dict)
    texture_data_offset = (
        palette_dict_offset + len(palette_dict) + 7
    ) & ~7

    texture_data = bytes(range(64))

    palette_data_offset = texture_data_offset + len(texture_data)
    palette_values = bytearray()
    for index in range(64):
        value = (
            (index & 0x1F)
            | ((index & 0x1F) << 5)
            | ((index & 0x1F) << 10)
        )
        palette_values += struct.pack("<H", value)

    tex0_size = palette_data_offset + len(palette_values)
    tex0 = bytearray(tex0_size)
    tex0[:4] = b"TEX0"
    struct.pack_into("<I", tex0, 4, tex0_size)
    struct.pack_into("<H", tex0, 0x0C, len(texture_data) // 8)
    struct.pack_into("<H", tex0, 0x0E, texture_dict_offset)
    struct.pack_into("<I", tex0, 0x14, texture_data_offset)
    struct.pack_into("<H", tex0, 0x30, len(palette_values) // 8)
    struct.pack_into("<I", tex0, 0x34, palette_dict_offset)
    struct.pack_into("<I", tex0, 0x38, palette_data_offset)

    tex0[
        texture_dict_offset:texture_dict_offset + len(texture_dict)
    ] = texture_dict
    tex0[
        palette_dict_offset:palette_dict_offset + len(palette_dict)
    ] = palette_dict
    tex0[
        texture_data_offset:texture_data_offset + len(texture_data)
    ] = texture_data
    tex0[
        palette_data_offset:palette_data_offset + len(palette_values)
    ] = palette_values

    mdl0 = bytearray(8)
    mdl0[:4] = b"MDL0"
    struct.pack_into("<I", mdl0, 4, len(mdl0))

    section_count = 2
    section_table_end = 0x10 + section_count * 4
    mdl0_offset = section_table_end
    tex0_offset = mdl0_offset + len(mdl0)
    file_size = tex0_offset + len(tex0)

    data = bytearray(file_size)
    data[:4] = b"BMD0"
    struct.pack_into("<H", data, 4, 0xFEFF)
    struct.pack_into("<H", data, 6, 2)
    struct.pack_into("<I", data, 8, file_size)
    struct.pack_into("<H", data, 0x0C, 0x10)
    struct.pack_into("<H", data, 0x0E, section_count)
    struct.pack_into("<I", data, 0x10, mdl0_offset)
    struct.pack_into("<I", data, 0x14, tex0_offset)
    data[mdl0_offset:mdl0_offset + len(mdl0)] = mdl0
    data[tex0_offset:tex0_offset + len(tex0)] = tex0

    return bytes(data)


class ExtractTex0Tests(unittest.TestCase):
    def test_extracts_embedded_texture_to_png(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            input_path = root / "sample.nsbmd"
            output_dir = root / "output"
            input_path.write_bytes(make_bmd0_with_one_texture())

            manifest = extract_tex0.extract_tex0(
                input_path=input_path,
                output_dir=output_dir,
            )

            self.assertEqual(manifest["texture_count"], 1)
            self.assertEqual(manifest["exported_count"], 1)
            self.assertEqual(manifest["failure_count"], 0)

            texture = manifest["textures"][0]
            self.assertEqual(texture["name"], "sample")
            self.assertEqual((texture["width"], texture["height"]), (8, 8))
            self.assertEqual(texture["format_name"], "PLTT256")

            png_path = output_dir / texture["output_path"]
            self.assertEqual(
                png_path.read_bytes()[:8],
                b"\x89PNG\r\n\x1a\n",
            )

            metadata = json.loads(
                (output_dir / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata["exported_count"], 1)


if __name__ == "__main__":
    unittest.main()
