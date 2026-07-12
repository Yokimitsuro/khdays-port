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
    / "inspect_nsbmd"
    / "inspect_nsbmd.py"
)
SPEC = importlib.util.spec_from_file_location("inspect_nsbmd", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
inspect_nsbmd = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = inspect_nsbmd
SPEC.loader.exec_module(inspect_nsbmd)


def make_section(magic: bytes, size: int) -> bytes:
    data = bytearray(size)
    data[:4] = magic
    struct.pack_into("<I", data, 4, size)
    return bytes(data)


def make_bmd0(with_tex0: bool) -> bytes:
    sections = [make_section(b"MDL0", 0x40)]
    if with_tex0:
        sections.append(make_section(b"TEX0", 0x30))

    section_count = len(sections)

    # Real Nitro BMD0 files commonly keep header_size at 0x10.
    # The section-offset table follows immediately after that fixed header.
    header_size = 0x10
    offset_table_end = 0x10 + section_count * 4
    cursor = offset_table_end

    offsets = []
    for section in sections:
        offsets.append(cursor)
        cursor += len(section)

    data = bytearray(cursor)
    data[:4] = b"BMD0"
    struct.pack_into("<H", data, 4, 0xFEFF)
    struct.pack_into("<H", data, 6, 0x0100)
    struct.pack_into("<I", data, 8, len(data))
    struct.pack_into("<H", data, 0x0C, header_size)
    struct.pack_into("<H", data, 0x0E, section_count)

    for index, offset in enumerate(offsets):
        struct.pack_into("<I", data, 0x10 + index * 4, offset)

    for offset, section in zip(offsets, sections):
        data[offset:offset + len(section)] = section

    return bytes(data)


class InspectNsbmdTests(unittest.TestCase):
    def test_accepts_offset_table_after_fixed_header(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "unpacked"
            root.mkdir()

            plain = root / "plain.nsbmd"
            textured = root / "textured.nsbmd"
            plain.write_bytes(make_bmd0(False))
            textured.write_bytes(make_bmd0(True))

            output = inspect_nsbmd.analyze(root, None)
            summary = json.loads(
                (output / "summary.json").read_text(encoding="utf-8")
            )

            self.assertEqual(summary["totals"]["files_found"], 2)
            self.assertEqual(summary["totals"]["files_parsed"], 2)
            self.assertEqual(summary["totals"]["failures"], 0)
            self.assertEqual(summary["totals"]["with_mdl0"], 2)
            self.assertEqual(summary["totals"]["with_embedded_tex0"], 1)
            self.assertEqual(
                summary["best_candidates"][0]["path"],
                "textured.nsbmd",
            )


if __name__ == "__main__":
    unittest.main()
