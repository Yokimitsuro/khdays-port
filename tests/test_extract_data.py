from __future__ import annotations

import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / "tools"
    / "extract_data"
    / "extract_data.py"
)
SPEC = importlib.util.spec_from_file_location("extract_data", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
extract_data = importlib.util.module_from_spec(SPEC)
import sys
sys.modules[SPEC.name] = extract_data
SPEC.loader.exec_module(extract_data)


class ExtractDataTests(unittest.TestCase):
    def test_minimal_nitrofs_extraction(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            rom = temp / "test.nds"
            output = temp / "output"
            database = temp / "supported_roms.json"

            image = bytearray(0x500)
            image[0x00:0x0C] = b"TESTROM\x00\x00\x00\x00\x00"
            image[0x0C:0x10] = b"TSTP"
            image[0x10:0x12] = b"01"

            fnt_offset = 0x200
            # Root main entry: subtable at 8, first file ID 0, 1 directory.
            fnt = bytearray(struct.pack("<IHH", 8, 0, 1))
            fnt += bytes([8]) + b"test.bin" + bytes([0])

            fat_offset = 0x240
            file_start = 0x300
            file_data = b"hello from NitroFS"
            fat = struct.pack("<II", file_start, file_start + len(file_data))

            struct.pack_into("<II", image, 0x40, fnt_offset, len(fnt))
            struct.pack_into("<II", image, 0x48, fat_offset, len(fat))
            image[fnt_offset:fnt_offset + len(fnt)] = fnt
            image[fat_offset:fat_offset + len(fat)] = fat
            image[file_start:file_start + len(file_data)] = file_data
            rom.write_bytes(image)

            database.write_text(
                json.dumps({"schema_version": 1, "roms": []}),
                encoding="utf-8",
            )

            result = extract_data.extract_rom(
                rom_path=rom,
                output_root=output,
                database_path=database,
                allow_unknown=False,
                force=False,
            )

            extracted = result / "nitrofs" / "test.bin"
            self.assertEqual(extracted.read_bytes(), file_data)

            manifest = json.loads(
                (result / "manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["files"][0]["path"], "nitrofs/test.bin")
            self.assertEqual(manifest["files"][0]["size"], len(file_data))


if __name__ == "__main__":
    unittest.main()
