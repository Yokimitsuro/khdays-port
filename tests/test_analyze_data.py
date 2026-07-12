from __future__ import annotations

import csv
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / "tools"
    / "analyze_data"
    / "analyze_data.py"
)
SPEC = importlib.util.spec_from_file_location("analyze_data", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
analyze_data = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = analyze_data
SPEC.loader.exec_module(analyze_data)


class AnalyzeDataTests(unittest.TestCase):
    def test_detects_nitro_formats_and_writes_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            extraction = root / "extraction"
            nitrofs = extraction / "nitrofs"
            nitrofs.mkdir(parents=True)

            model = nitrofs / "sample.nsbmd"
            texture = nitrofs / "sample.nsbtx"
            compressed = nitrofs / "compressed.bin"

            model.write_bytes(b"BMD0" + bytes(128))
            texture.write_bytes(b"BTX0" + bytes(64))
            compressed.write_bytes(bytes([0x10, 0x20, 0x00, 0x00]) + bytes(32))

            manifest = {
                "schema_version": 1,
                "system": {},
                "files": [
                    {
                        "file_id": 0,
                        "path": "nitrofs/sample.nsbmd",
                        "size": model.stat().st_size,
                        "sha256": "a" * 64,
                    },
                    {
                        "file_id": 1,
                        "path": "nitrofs/sample.nsbtx",
                        "size": texture.stat().st_size,
                        "sha256": "b" * 64,
                    },
                    {
                        "file_id": 2,
                        "path": "nitrofs/compressed.bin",
                        "size": compressed.stat().st_size,
                        "sha256": "c" * 64,
                    },
                ],
            }
            (extraction / "manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )

            output = analyze_data.analyze(extraction, None)

            summary = json.loads(
                (output / "summary.json").read_text(encoding="utf-8")
            )
            self.assertEqual(summary["by_format"]["NSBMD"], 1)
            self.assertEqual(summary["by_format"]["NSBTX"], 1)
            self.assertEqual(summary["by_format"]["LZ77"], 1)

            with (output / "asset_inventory.csv").open(
                newline="",
                encoding="utf-8-sig",
            ) as stream:
                rows = list(csv.DictReader(stream))

            self.assertEqual(len(rows), 3)
            self.assertTrue((output / "report.md").is_file())


if __name__ == "__main__":
    unittest.main()
