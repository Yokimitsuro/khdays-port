from __future__ import annotations

import importlib.util
import os
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "build_preview" / "preview_cache.py"
SPEC = importlib.util.spec_from_file_location("preview_cache", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
preview_cache = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(preview_cache)


class PreviewCacheTests(unittest.TestCase):
    def test_output_must_be_newer_than_source_and_decoder(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "scene.mods"
            decoder = root / "khdays-port.exe"
            output = root / "scene.mp4"
            for path in (source, decoder, output):
                path.write_bytes(b"x")

            os.utime(source, ns=(100, 100))
            os.utime(decoder, ns=(200, 200))
            os.utime(output, ns=(300, 300))
            self.assertTrue(
                preview_cache.output_is_current(output, source, decoder)
            )

            os.utime(decoder, ns=(400, 400))
            self.assertFalse(
                preview_cache.output_is_current(output, source, decoder)
            )

    def test_empty_output_is_never_current(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "scene.mods"
            output = root / "scene.mp4"
            source.write_bytes(b"x")
            output.write_bytes(b"")
            self.assertFalse(preview_cache.output_is_current(output, source))


if __name__ == "__main__":
    unittest.main()
