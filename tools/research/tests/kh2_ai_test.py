"""KINGDOM HEARTS II's AI scripts (tools/research/kh2_ai.py): the header and triggers, each operation decoded, the code
walked from the triggers through jumps and calls, refused scripts, a BAR's script and the survey. Made-up scripts:
every byte written by the test, nothing of KH2's."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import kh2_ai  # noqa: E402


def script(triggers: list[tuple[int, int]], code: list[int], name: bytes = b"made_up") -> bytes:
    """A script: its name, sizes (16, 64, 32), triggers, then the code words; word 0 is at +0x10, so the code
    here starts after the triggers (at word `start(len(triggers))`)."""
    head = name.ljust(16, b"\0") + struct.pack("<3i", 16, 64, 32)
    for key, entry in triggers:
        head += struct.pack("<2i", key, entry)
    head += struct.pack("<2i", 0, 0)
    return head + b"".join(struct.pack("<H", w & 0xFFFF) for w in code)


def start(triggers: int) -> int:
    """The first code word's number after `triggers` triggers."""
    return (0x1C + 8 * (triggers + 1) - 0x10) // 2


def bar(entries: list[tuple[int, bytes, bytes]]) -> bytes:
    """A BAR of (type, name, data) entries."""
    out = b"BAR\x01" + struct.pack("<I", len(entries)) + bytes(8)
    at = 16 + 16 * len(entries)
    body = b""
    for kind, name, data in entries:
        out += struct.pack("<HH4sII", kind, 0, name, at + len(body), len(data))
        body += data
    return out + body


def push_int(v: int) -> list[int]:
    return [0x0000, v & 0xFFFF, (v >> 16) & 0xFFFF]


def push_float(v: float) -> list[int]:
    lo, hi = struct.unpack("<2H", struct.pack("<f", v))
    return [0x0010, lo, hi]


class Kh2AiTest(unittest.TestCase):
    def made(self):
        s0 = start(2)
        # trigger 0: push 7; pop fp+4; syscall 1:7; yield; jmp back to its start
        t0 = push_int(7) + [0x0001, 0x0004, 10 | (1 << 6), 7, 0x0009]
        jmp_at = s0 + len(t0)
        t0 += [0x0007, (s0 - (jmp_at + 2)) & 0xFFFF]
        # trigger 1: push 1.5f; jz +2 (over the call); call the routine; exit; the routine: ret
        s1 = s0 + len(t0)
        t1 = push_float(1.5) + [0x0047, 2, (2 << 6) | 8, 1, 0x0049, 0x0089]
        unreached = [0x000F]
        return script([(0, s0), (1, s1)], t0 + t1 + unreached), s0, s1, jmp_at

    def test_header_and_triggers(self):
        data, s0, s1, _ = self.made()
        s = kh2_ai.read_script(data)
        self.assertEqual(s.name, "made_up")
        self.assertEqual((s.work, s.frame, s.stack), (16, 64, 32))
        self.assertEqual(s.triggers, [(0, s0), (1, s1)])

    def test_walk_and_listing(self):
        data, s0, s1, jmp_at = self.made()
        s = kh2_ai.read_script(data)
        code = kh2_ai.walk(s)
        self.assertEqual(code[s0].text, "push 7")
        self.assertEqual(code[s0 + 3].text, "pop fp+0x4")
        self.assertEqual(code[s0 + 5].syscall, (1, 7))
        self.assertEqual(code[s0 + 7].text, "yield")
        self.assertEqual(code[jmp_at].text, f"jmp {s0:#x}")
        self.assertEqual(code[s1].text, "push 1.5f")
        self.assertEqual(code[s1 + 3].text, f"jz {s1 + 7:#x}")
        self.assertEqual(code[s1 + 5].text, f"call {s1 + 8:#x} (frame 2)")
        self.assertEqual(code[s1 + 7].text, "exit")
        self.assertEqual(code[s1 + 8].text, "ret")
        self.assertNotIn(s1 + 9, code, "the word after the routine is not reached")
        self.assertFalse(any(i.bad for i in code.values()))
        text = "\n".join(kh2_ai.listing(s))
        self.assertIn("trigger 0 at", text)
        self.assertIn(": start", text)
        self.assertIn(f"{s1 + 8:#06x}:", text, "the routine has a label")

    def test_operations(self):
        s = kh2_ai.read_script(script([(0, start(1))], [0x0009]))
        # each one decoded alone at word 0
        cases = {
            (0x0020 | (1 << 6), 0x10): "push &wp+0x10",
            (0x0030, 0x8): "push fp+0x8",
            (0x0030 | (3 << 6), 0x4): "push code[0x4]",
            (0x0002 | (1 << 6), 12, 0x20): "copy 12 bytes to wp+0x20",
            (0x0003, 0x14): "load +0x14",
            (0x0004 | (16 << 6),): "store 16 bytes",
            (0x0005 | (2 << 6),): "neg",
            (0x0015 | (1 << 6),): "ftoi",
            (0x0006 | (7 << 6),): "xor",
            (0x0016 | (3 << 6),): "fdiv",
            (0x0009 | (6 << 6),): "sin",
            (0x0009 | (8 << 6),): "deg2rad",
        }
        for words, text in cases.items():
            s.data = s.data[:0x10] + b"".join(struct.pack("<H", w) for w in words) + bytes(8)
            self.assertEqual(kh2_ai.decode(s, 0).text, text)
        for bad in (0x000C, 0x0026, 0x0009 | (4 << 6), 0x0007 | (3 << 6)):
            s.data = s.data[:0x10] + struct.pack("<H", bad) + bytes(8)
            self.assertTrue(kh2_ai.decode(s, 0).bad, hex(bad))
        far = (3 << 6) | 11
        s.data = s.data[:0x10] + struct.pack("<HiH", far, 5, 0)
        self.assertEqual(kh2_ai.decode(s, 0).text, f"call {3 + 5:#x} (frame 3, far)")

    def test_refused(self):
        with self.assertRaises(kh2_ai.AiError):
            kh2_ai.read_script(b"short")
        with self.assertRaises(kh2_ai.AiError):
            kh2_ai.read_script(script([(0, 999)], [0x0009]))
        with self.assertRaises(kh2_ai.AiError):
            kh2_ai.read_script(b"x" * 16 + struct.pack("<3i", 0, 0, 0) + struct.pack("<i", 1))

    def test_bar_and_survey(self):
        data, *_ = self.made()
        mdlx = bar([(4, b"mdl", b"model"), (3, b"m_ex", data)])
        found = kh2_ai.scripts_in(mdlx)
        self.assertEqual([n for n, _ in found], ["m_ex"])
        self.assertEqual(kh2_ai.scripts_in(b"not a bar"), [])
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "obj").mkdir()
            (Path(d) / "obj" / "M_TEST.mdlx").write_bytes(mdlx)
            (Path(d) / "obj" / "B_TEST.mdlx").write_bytes(mdlx)
            scripts, calls = kh2_ai.survey([Path(d)])
            self.assertEqual(kh2_ai.find([Path(d)], "obj/M_TEST.mdlx").name, "M_TEST.mdlx")
        self.assertEqual(scripts, {"M": 1, "B": 1})
        self.assertEqual(calls["M"][(1, 7)], 1)


if __name__ == "__main__":
    unittest.main()
