"""Synthetic preservation checks for the local gauge archive conversion."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from kh2_center_gauge import recenter_gauge


class CenterGaugeTest(unittest.TestCase):
    def test_preserves_relative_motion_and_other_bytes(self):
        start, animations = 32, 48
        table = animations + 8 * 144
        size = table + 139 * 36
        data = bytearray(start + size)
        struct.pack_into("<4sI", data, 0, b"BAR\x01", 1)
        struct.pack_into("<I4sII", data, 16, 25, b"gaug", start, size)
        struct.pack_into("<4sI", data, start, b"SEQD", 0x160)
        struct.pack_into("<4I", data, start + 32, 8, animations, 139, table)
        selected = set()
        for clip in range(139):
            index = clip - 131 if clip >= 132 else 0
            struct.pack_into("<HH", data, start + table + clip * 36, index, 1)
            if clip >= 132:
                at = start + animations + index * 144
                struct.pack_into("<4i", data, at + 16, 455, 462, 355, 349)
                struct.pack_into("<4f", data, at + 112, -4, 0, -10, 3)
                selected.update(range(at + 16, at + 32))
        result = recenter_gauge(bytes(data))
        for index in range(1, 8):
            at = start + animations + index * 144
            self.assertEqual(struct.unpack_from("<4i", result, at + 16), (0, 7, 0, -6))
        self.assertEqual(len(data), len(result))
        self.assertTrue(all(data[i] == result[i] for i in range(len(data)) if i not in selected))


if __name__ == "__main__":
    unittest.main()
