from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / "tools"
    / "decompress_data"
    / "decompress_data.py"
)
SPEC = importlib.util.spec_from_file_location("decompress_data", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
decompress_data = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = decompress_data
SPEC.loader.exec_module(decompress_data)


def literal_lz10(payload: bytes) -> bytes:
    header = bytes([0x10]) + len(payload).to_bytes(3, "little")
    body = bytearray()
    for offset in range(0, len(payload), 8):
        chunk = payload[offset:offset + 8]
        body.append(0x00)
        body.extend(chunk)
    return header + bytes(body)


class DecompressDataTests(unittest.TestCase):
    def test_lz10_literal_stream(self) -> None:
        payload = b"BMD0example-data"
        compressed = literal_lz10(payload)
        self.assertEqual(decompress_data.decompress_lz10(compressed), payload)

    def test_extraction_pipeline(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            extraction = root / "extraction"
            source = extraction / "nitrofs" / "asset.bin.z"
            source.parent.mkdir(parents=True)

            payload = b"BTX0texture"
            source.write_bytes(literal_lz10(payload))

            manifest = {
                "schema_version": 1,
                "files": [
                    {
                        "file_id": 0,
                        "path": "nitrofs/asset.bin.z",
                        "size": source.stat().st_size,
                        "sha256": "0" * 64,
                    }
                ],
            }
            (extraction / "manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )

            output, records, failures = decompress_data.decompress_extraction(
                extraction_root=extraction,
                output_root=None,
                force=False,
            )

            self.assertFalse(failures)
            self.assertEqual(len(records), 1)
            self.assertEqual((output / "asset.bin").read_bytes(), payload)

            generated = json.loads(
                (output / "decompressed_manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(generated["files"][0]["inner_magic_ascii"][:4], "BTX0")


if __name__ == "__main__":
    unittest.main()
