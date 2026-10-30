"""tools/mods/nitro3d.py on synthetic bytes and rotations: a P2 archive (an LZ10 entry among them), a model's
render commands giving its nodes' parents, a basis rotation, and the quaternion helpers the clips are made with
(a matrix and back, a slerp's ends and middle, the mean of rotations). No game data is read."""

import struct
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import nitro3d as n3  # noqa: E402


def lz10_literal(data: bytes) -> bytes:
    """An LZ10 stream of literals only (a flag byte of 0 before each 8 bytes)."""
    out = bytearray(b"\x10" + len(data).to_bytes(3, "little"))
    for at in range(0, len(data), 8):
        out.append(0)
        out += data[at : at + 8]
    return bytes(out)


def p2(entries: list[tuple[bytes, bool]]) -> bytes:
    """A P2 archive: (data, compressed?) each, entries at 0x200-byte sectors after the header."""
    count = len(entries)
    sizes_at = 0x10 + (count + 1) // 2 * 4
    header = bytearray(sizes_at + 4 * count)
    header[:2] = b"P2"
    struct.pack_into("<H", header, 2, count)
    start = (len(header) + 0x1FF) & ~0x1FF
    struct.pack_into("<I", header, 0xC, start)
    body = bytearray()
    for i, (data, compressed) in enumerate(entries):
        stored = lz10_literal(data) if compressed else data
        struct.pack_into("<H", header, 0x10 + 2 * i, len(body) // 0x200)
        struct.pack_into("<I", header, sizes_at + 4 * i, len(stored) | (0x80000000 if compressed else 0))
        body += stored + bytes(-len(stored) % 0x200)
    return bytes(header) + bytes(start - len(header)) + bytes(body)


def turn(axis, degrees):
    """A column-vector rotation by `degrees` about `axis`."""
    k = np.asarray(axis, float) / np.linalg.norm(axis)
    kx = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    a = np.radians(degrees)
    return np.eye(3) + np.sin(a) * kx + (1 - np.cos(a)) * kx @ kx


class Nitro3dTest(unittest.TestCase):
    def test_p2_entries(self):
        archive = p2([(b"plain entry", False), (b"", False), (b"squeezed " * 9, True)])
        self.assertEqual(n3.p2_entries(archive), [b"plain entry", None, b"squeezed " * 9])

    def test_node_parents(self):
        # a model whose render commands describe node 0 as a root, 1 under 0 (storing its matrix), 2 under 1
        sbc = bytes([0x06, 0, 0, 0, 0x26, 1, 0, 0, 5, 0x03, 1, 0x06, 2, 1, 0, 0x02, 2, 1, 0x01])
        model = bytearray(0x20) + sbc
        struct.pack_into("<II", model, 4, 0x20, len(model))
        self.assertEqual(n3.node_parents(bytes(model), 0, 3), [-1, 0, 1])

    def test_basis(self):
        # 45 degrees about z: first row (c, s, 0), second (-s, c, 0) in 13 bits (4096 a unit), shifted up 3 bits;
        # the second row's z (0) in the low bits; the third row their cross product
        c = round(4096 * np.sqrt(0.5))
        words = tuple((v << 3) & 0xFFFF for v in (c, c, 0, -c, c))
        want = np.array([[0.5**0.5, 0.5**0.5, 0], [-(0.5**0.5), 0.5**0.5, 0], [0, 0, 1]])
        self.assertTrue(np.allclose(n3.basis(words), want, atol=1e-3))

    def test_quaternions(self):
        m = turn([1, 2, 3], 70)
        self.assertTrue(np.allclose(n3.matrix_of(n3.quat_of(m)), m))
        a, b = turn([0, 0, 1], 10), turn([0, 0, 1], 50)
        self.assertTrue(np.allclose(n3.slerp_matrices(a, b, 0), a))
        self.assertTrue(np.allclose(n3.slerp_matrices(a, b, 1), b))
        self.assertTrue(np.allclose(n3.slerp_matrices(a, b, 0.5), turn([0, 0, 1], 30)))
        self.assertTrue(np.allclose(n3.mean_rotation([turn([0, 1, 0], d) for d in (-20, 0, 20)]), np.eye(3)))


if __name__ == "__main__":
    unittest.main()
