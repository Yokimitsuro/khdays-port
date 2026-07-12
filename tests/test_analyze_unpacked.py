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
    / "analyze_unpacked"
    / "analyze_unpacked.py"
)
SPEC = importlib.util.spec_from_file_location("analyze_unpacked", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
analyze_unpacked = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = analyze_unpacked
SPEC.loader.exec_module(analyze_unpacked)


class AnalyzeUnpackedTests(unittest.TestCase):
    def test_ranks_model_texture_pair_first(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            unpacked = Path(temporary) / "unpacked"
            unpacked.mkdir()

            manifest = {
                "schema_version": 1,
                "containers": [
                    {
                        "source_path": "mi/ch/03",
                        "container_magic": "KAPH",
                        "container_size": 180000,
                        "slots": [
                            {
                                "slot": 7,
                                "entries": [
                                    {"detected_format": "NSBMD"},
                                    {"detected_format": "NSBTX"},
                                ],
                            }
                        ],
                    },
                    {
                        "source_path": "UI/sample",
                        "container_magic": "D2KP",
                        "container_size": 20000,
                        "slots": [
                            {
                                "slot": 0,
                                "entries": [
                                    {"detected_format": "NCLR"},
                                    {"detected_format": "NCGR"},
                                ],
                            }
                        ],
                    },
                ],
            }
            (unpacked / "unpacked_manifest.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )

            output = analyze_unpacked.analyze(unpacked, None)
            summary = json.loads(
                (output / "summary.json").read_text(encoding="utf-8")
            )

            self.assertEqual(summary["totals"]["model_texture_pairs"], 1)
            self.assertEqual(
                summary["best_candidates"][0]["source_path"],
                "mi/ch/03",
            )
            self.assertTrue((output / "report.md").is_file())
            self.assertTrue((output / "containers.csv").is_file())


if __name__ == "__main__":
    unittest.main()
