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
    / "inspect_containers"
    / "inspect_containers.py"
)
SPEC = importlib.util.spec_from_file_location("inspect_containers", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
inspect_containers = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = inspect_containers
SPEC.loader.exec_module(inspect_containers)


class InspectContainersTests(unittest.TestCase):
    def test_signature_scanning_and_report_generation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            decompressed = root / "decompressed"
            decompressed.mkdir()

            data = bytearray(0xC0)
            data[0:4] = b"KAPH"
            struct.pack_into("<I", data, 0x08, 0x28)
            struct.pack_into("<I", data, 0x0C, 0x80)
            struct.pack_into("<I", data, 0x10, 0xFFFFFFFF)
            data[0x28:0x2C] = b"BMD0"
            data[0x80:0x84] = b"BTX0"

            sample = decompressed / "mi" / "ch" / "03"
            sample.parent.mkdir(parents=True)
            sample.write_bytes(data)

            manifest = {
                "schema_version": 1,
                "files": [
                    {
                        "source_path": "nitrofs/mi/ch/03.z",
                        "output_path": "mi/ch/03",
                        "compression": "LZ11",
                        "compressed_size": 100,
                        "decompressed_size": len(data),
                        "sha256": "0" * 64,
                        "inner_magic_ascii": "KAPH",
                        "inner_magic_hex": data[:16].hex().upper(),
                    }
                ],
                "failures": [],
            }
            (decompressed / "decompressed_manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )

            output = inspect_containers.inspect_all(
                decompressed_root=decompressed,
                output_dir=None,
                path_filters=[],
            )

            report = json.loads(
                (output / "container_report.json").read_text(encoding="utf-8")
            )
            container = report["containers"][0]

            self.assertEqual(container["container_magic"], "KAPH")
            self.assertEqual(container["embedded_format_counts"]["NSBMD"], 1)
            self.assertEqual(container["embedded_format_counts"]["NSBTX"], 1)

            word_at_08 = next(
                word
                for word in container["header_words"]
                if word["offset"] == 0x08
            )
            self.assertEqual(word_at_08["kind"], "offset_to_known_signature")
            self.assertTrue((output / "container_inventory.csv").is_file())
            self.assertTrue((output / "report.md").is_file())


if __name__ == "__main__":
    unittest.main()
