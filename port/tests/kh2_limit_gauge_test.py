"""Synthetic preservation and reference checks for the limit-gauge resource additions."""

from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from kh2_add_limit_gauge import STRIDES, YELLOW_RGBA, add_limit_gauge, tables


class LimitGaugeTest(unittest.TestCase):
    def test_copies_graph_without_changing_originals(self):
        rows = [[], [], [bytes(4) for _ in range(97)], [], [bytes(36) for _ in range(130)]]
        rows[0] = [bytes(range(44)), bytes(reversed(range(44)))]
        rows[1] = [struct.pack("<4iI", -45, -45, 0, 0, 0) for _ in range(3)]
        rows[1].append(struct.pack("<4iI", -100, 45, 0, 0, 1))
        rows[2][95], rows[2][96] = struct.pack("<HH", 0, 3), struct.pack("<HH", 3, 1)
        for sprite, clip in ((93, 128), (94, 129)):
            record = bytearray(range(144))
            struct.pack_into("<I", record, 4, sprite)
            first = len(rows[3])
            rows[3].append(bytes(record))
            row = bytearray(range(36))
            struct.pack_into("<HH", row, 0, first, 1)
            rows[4][clip] = bytes(row)
        sequence = bytearray(48)
        sequence[:8] = b"SEQD\x60\x01\0\0"
        for i, row in enumerate(rows):
            struct.pack_into("<II", sequence, 8 + i * 8, len(row), len(sequence))
            sequence.extend(b"".join(row))
        data = bytearray(32)
        struct.pack_into("<4sI", data, 0, b"BAR\x01", 1)
        struct.pack_into("<I4sII", data, 16, 25, b"gaug", 32, len(sequence))
        data.extend(sequence)
        data.extend(b"OTHER RESOURCE BYTES")
        result, indices = add_limit_gauge(bytes(data))
        self.assertEqual(indices, (130, 131))
        self.assertEqual(result[:24], data[:24])
        self.assertEqual(result[32 : len(data)], data[32:])
        _, changed = tables(result)
        for old, new, stride in zip(rows, changed, STRIDES):
            self.assertEqual(new[: len(old)], old)
            self.assertTrue(all(len(row) == stride for row in new))
        self.assertEqual([len(r) for r in changed], [4, 8, 99, 4, 132])
        for source, new in zip(rows[0], changed[0][2:]):
            self.assertEqual(source[:28], new[:28])
            self.assertEqual(new[28:44], YELLOW_RGBA * 4)
        for i in range(2):
            first, count = struct.unpack("<HH", changed[2][97 + i])
            self.assertEqual((first, count), ((4, 3) if i == 0 else (7, 1)))
            self.assertTrue(all(struct.unpack_from("<I", r, 16)[0] == 2 + i for r in changed[1][first : first + count]))
            self.assertEqual(struct.unpack_from("<I", changed[3][2 + i], 4)[0], 97 + i)
            self.assertEqual(changed[3][2 + i][:4], rows[3][i][:4])
            self.assertEqual(changed[3][2 + i][8:], rows[3][i][8:])
            self.assertEqual(changed[4][130 + i][4:], rows[4][128 + i][4:])


if __name__ == "__main__":
    unittest.main()
