"""Check Metal resource layouts against the committed SPIR-V modules."""

import re
import unittest

from build_shaders import metal_order
from fix_msl_order import GENERATED, array


class MetalOrderTests(unittest.TestCase):
    def shader(self, base):
        text = (GENERATED / (base + ".h")).read_text()
        return (array(text, base + "_msl", base)[1][:-1], array(text, base + "_spirv", base)[1])

    def test_missing_resource_is_rejected(self):
        msl, spirv = self.shader("compose_cs")
        incomplete = re.sub(rb"Params \[\[buffer\(\d+\)\]\]", b"Params", msl)
        self.assertNotEqual(incomplete, msl)
        with self.assertRaisesRegex(ValueError, "missing"):
            metal_order("compose_cs", incomplete, spirv)

    def test_duplicate_resource_is_rejected(self):
        msl, spirv = self.shader("compose_cs")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            metal_order("compose_cs", msl + b"\nParams [[buffer(0)]]", spirv)

    def test_optimized_fragment_keeps_unused_storage_slots(self):
        msl, spirv = self.shader("native3d_rear_ps")
        fixed, _ = metal_order("native3d_rear_ps", msl, spirv)
        self.assertIn(b"vram [[buffer(3)]]", fixed)
        self.assertIn(b"Params [[buffer(0)]]", fixed)
        self.assertEqual(metal_order("native3d_rear_ps", fixed, spirv), (fixed, []))


if __name__ == "__main__":
    unittest.main()
