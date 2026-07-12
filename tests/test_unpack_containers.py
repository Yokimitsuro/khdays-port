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
    / "unpack_containers"
    / "unpack_containers.py"
)
SPEC = importlib.util.spec_from_file_location("unpack_containers", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
unpack_containers = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = unpack_containers
SPEC.loader.exec_module(unpack_containers)


def make_nitro_file(magic: bytes, size: int) -> bytes:
    data = bytearray(size)
    data[:4] = magic
    struct.pack_into("<H", data, 4, 0xFEFF)
    struct.pack_into("<H", data, 6, 0x0100)
    struct.pack_into("<I", data, 8, size)
    return bytes(data)


def build_container(
    magic: bytes,
    slots: dict[int, list[bytes]],
) -> bytes:
    header = bytearray(0x28)
    header[:4] = magic
    for slot in range(8):
        struct.pack_into("<I", header, 0x08 + slot * 4, 0xFFFFFFFF)

    table_offsets: dict[int, int] = {}
    cursor = 0x28

    for slot in sorted(slots):
        table_offsets[slot] = cursor
        count = len(slots[slot])
        cursor += 4 + count * 8

    cursor = (cursor + 0x1F) & ~0x1F
    payload_offsets: dict[tuple[int, int], int] = {}

    for slot in sorted(slots):
        for index, payload in enumerate(slots[slot]):
            payload_offsets[(slot, index)] = cursor
            cursor += len(payload)
            cursor = (cursor + 0x1F) & ~0x1F

    data = bytearray(cursor)

    for slot, table_offset in table_offsets.items():
        struct.pack_into("<I", data, 0x08 + slot * 4, table_offset)
        entries = slots[slot]
        struct.pack_into("<I", data, table_offset, len(entries))

        offsets_start = table_offset + 4
        sizes_start = offsets_start + len(entries) * 4

        for index, payload in enumerate(entries):
            payload_offset = payload_offsets[(slot, index)]
            struct.pack_into("<I", data, offsets_start + index * 4, payload_offset)
            struct.pack_into("<I", data, sizes_start + index * 4, len(payload))
            data[payload_offset:payload_offset + len(payload)] = payload

    data[:4] = magic
    return bytes(data)


class UnpackContainersTests(unittest.TestCase):
    def test_parses_kaph_and_d2kp_slot_tables(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            decompressed = root / "decompressed"
            decompressed.mkdir()

            kaph_data = build_container(
                b"KAPH",
                {
                    7: [
                        make_nitro_file(b"BMD0", 0x80),
                        make_nitro_file(b"BTX0", 0x60),
                    ]
                },
            )
            d2kp_data = build_container(
                b"D2KP",
                {
                    0: [make_nitro_file(b"RLCN", 0x40)],
                    1: [make_nitro_file(b"RGCN", 0x80)],
                    6: [make_nitro_file(b"RCSN", 0x50)],
                },
            )

            kaph_path = decompressed / "mi" / "ch" / "03"
            d2kp_path = decompressed / "gameover" / "sample.pbg"
            kaph_path.parent.mkdir(parents=True)
            d2kp_path.parent.mkdir(parents=True)
            kaph_path.write_bytes(kaph_data)
            d2kp_path.write_bytes(d2kp_data)

            manifest = {
                "schema_version": 1,
                "files": [
                    {
                        "output_path": "mi/ch/03",
                        "inner_magic_ascii": "KAPH",
                    },
                    {
                        "output_path": "gameover/sample.pbg",
                        "inner_magic_ascii": "D2KP",
                    },
                ],
            }
            (decompressed / "decompressed_manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )

            output, containers, failures = unpack_containers.unpack_all(
                decompressed_root=decompressed,
                output_root=None,
                filters=[],
                force=False,
            )

            self.assertFalse(failures)
            self.assertEqual(len(containers), 2)
            self.assertEqual(
                sum(container.extracted_entry_count for container in containers),
                5,
            )

            self.assertTrue(
                (output / "mi" / "ch" / "03" / "slot_7" / "0000.nsbmd").is_file()
            )
            self.assertTrue(
                (output / "mi" / "ch" / "03" / "slot_7" / "0001.nsbtx").is_file()
            )
            self.assertTrue(
                (
                    output
                    / "gameover"
                    / "sample.pbg"
                    / "slot_0"
                    / "0000.nclr"
                ).is_file()
            )
            self.assertTrue(
                (
                    output
                    / "gameover"
                    / "sample.pbg"
                    / "slot_1"
                    / "0000.ncgr"
                ).is_file()
            )
            self.assertTrue(
                (
                    output
                    / "gameover"
                    / "sample.pbg"
                    / "slot_6"
                    / "0000.nscr"
                ).is_file()
            )

            global_manifest = json.loads(
                (output / "unpacked_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(global_manifest["entry_count"], 5)
            self.assertEqual(global_manifest["failure_count"], 0)


if __name__ == "__main__":
    unittest.main()
