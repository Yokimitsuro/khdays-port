"""Synthetic resource graph and preservation checks for the white divider."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from kh2_add_limit_gauge import STRIDES, tables
from kh2_add_limit_marker import WHITE_RGBA, add_limit_marker


class LimitMarkerTest(unittest.TestCase):
    def test_white_edge_with_independent_sprite_and_animation(self):
        rows = [[], [], [bytes(4) for _ in range(96)], [], [bytes(36) for _ in range(124)]]
        part = bytearray(range(44))
        struct.pack_into("<4i", part, 4, 99, 60, 144, 105)
        rows[0].append(bytes(part))
        rows[1].append(struct.pack("<4iI", -45, -45, 0, 0, 0))
        rows[2][95] = struct.pack("<HH", 0, 1)
        animation = bytearray(range(144))
        struct.pack_into("<I", animation, 4, 95)
        rows[3].append(bytes(animation))
        clip = bytearray(range(36))
        struct.pack_into("<HH", clip, 0, 0, 1)
        rows[4][123] = bytes(clip)
        sequence = bytearray(48)
        sequence[:8] = b"SEQD\x60\x01\0\0"
        for i, row in enumerate(rows):
            struct.pack_into("<II", sequence, 8 + i * 8, len(row), len(sequence))
            sequence.extend(b"".join(row))
        data = bytearray(32)
        struct.pack_into("<4sI", data, 0, b"BAR\x01", 1)
        struct.pack_into("<I4sII", data, 16, 25, b"gaug", 32, len(sequence))
        data.extend(sequence)
        data.extend(b"ANOTHER RESOURCE")
        result, index = add_limit_marker(bytes(data))
        self.assertEqual(index, 124)
        self.assertEqual(result[:24], data[:24])
        self.assertEqual(result[32 : len(data)], data[32:])
        _, changed = tables(result)
        for old, new, stride in zip(rows, changed, STRIDES):
            self.assertEqual(new[: len(old)], old)
            self.assertTrue(all(len(row) == stride for row in new))
        self.assertEqual([len(row) for row in changed], [2, 2, 97, 2, 125])
        new_part = changed[0][-1]
        self.assertEqual(struct.unpack_from("<4i", new_part, 4), (99, 104, 144, 105))
        self.assertEqual(new_part[:4], part[:4])
        self.assertEqual(new_part[20:28], part[20:28])
        self.assertEqual(new_part[28:44], WHITE_RGBA * 4)
        self.assertEqual(struct.unpack("<4iI", changed[1][-1]), (-45, -1, 0, 1, 1))
        self.assertEqual(struct.unpack("<HH", changed[2][-1]), (1, 1))
        self.assertEqual(struct.unpack_from("<I", changed[3][-1], 4)[0], 96)
        self.assertEqual(changed[3][-1][:4], animation[:4])
        self.assertEqual(changed[3][-1][8:], animation[8:])
        self.assertEqual(struct.unpack_from("<HH", changed[4][-1]), (1, 1))
        self.assertEqual(changed[4][-1][4:], clip[4:])


if __name__ == "__main__":
    unittest.main()
