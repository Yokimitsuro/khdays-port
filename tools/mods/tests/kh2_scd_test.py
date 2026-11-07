"""KINGDOM HEARTS II's sound banks (tools/mods/kh2_scd.py): the recordings' table, Microsoft ADPCM decoded by hand
(the predictor's division toward zero), refused banks and recordings. Made-up banks: every byte written by the test,
nothing of KH2's."""

import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import kh2_scd  # noqa: E402

# Microsoft ADPCM's seven predictor pairs
PAIRS = [(256, 0), (512, -256), (0, 0), (192, 64), (240, 0), (460, -208), (392, -232)]


def adpcm_format(channels: int, per_block: int, align: int, rate: int = 48000) -> bytes:
    """A WAVEFORMATEX of Microsoft ADPCM with its seven pairs."""
    head = struct.pack("<HHIIHHH", 2, channels, rate, rate * align // per_block, align, 4, 4 + 4 * len(PAIRS))
    return head + struct.pack("<HH", per_block, len(PAIRS)) + b"".join(struct.pack("<hh", *p) for p in PAIRS)


def made_up_bank(recordings: list[tuple[int, int, int, bytes, bytes] | None]) -> bytes:
    """A bank: its 0x30-byte header, the tables' header, the recordings' table and each recording (channels, rate,
    codec, extra, data), None an empty place."""
    table = 0x50
    out = bytearray(table + 4 * len(recordings))
    out[:8] = kh2_scd.MAGIC
    struct.pack_into("<4H", out, 0x30, 0, 0, len(recordings), 0)
    struct.pack_into("<6I", out, 0x38, 0, table, 0, 0, 0, 0)
    for n, r in enumerate(recordings):
        if r is None:
            continue
        channels, rate, codec, extra, data = r
        struct.pack_into("<I", out, table + 4 * n, len(out))
        out += struct.pack("<8I", len(data), channels, rate, codec, 0, 0, len(extra), 0) + extra + data
    return bytes(out)


def block(predictors: list[int], steps: list[int], s1: list[int], s2: list[int], nibbles: list[int]) -> bytes:
    """One block: per channel predictor, then steps, samples 1, samples 2; then the nibbles, high first."""
    out = bytes(predictors)
    for values in (steps, s1, s2):
        out += struct.pack(f"<{len(values)}h", *values)
    packed = [(nibbles[i] << 4) | (nibbles[i + 1] if i + 1 < len(nibbles) else 0) for i in range(0, len(nibbles), 2)]
    return out + bytes(packed)


class Bank(unittest.TestCase):
    def test_reads_the_recordings(self):
        mono = adpcm_format(1, 4, 8)
        data = block([0], [16], [100], [50], [1, 2])
        bank = made_up_bank([None, (1, 48001, kh2_scd.MS_ADPCM, mono, data), (2, 44100, 6, b"\x01\x02", b"ogg")])
        recordings = kh2_scd.recordings(bank)
        self.assertEqual(len(recordings), 3)
        self.assertIsNone(recordings[0])
        r = recordings[1]
        self.assertEqual((r.number, r.channels, r.rate, r.codec, r.extra, r.data), (1, 1, 48001, 12, mono, data))
        self.assertEqual(recordings[2].codec, 6)

    def test_decodes_microsoft_adpcm(self):
        # predictor (256, 0): each sample the last plus the nibble times the step (16: 230/256 of it stays 16)
        r = kh2_scd.recordings(
            made_up_bank([(1, 48000, 12, adpcm_format(1, 4, 8), block([0], [16], [100], [50], [1, 2]))])
        )
        self.assertEqual(kh2_scd.decode(r[0]).tolist(), [[50], [100], [116], [148]])
        # predictor (192, 64), sample 1 -3: -576 / 256 is -2.25, -2 toward zero (not -3); the step 32 then 28
        # (230/256 of it); nibble 8 is -8: (-2 192 - 3 64) / 256 = -2.25 -> -2, -2 - 8 * 28 = -226
        r = kh2_scd.recordings(
            made_up_bank([(1, 48000, 12, adpcm_format(1, 4, 8), block([3], [32], [-3], [0], [0, 8]))])
        )
        self.assertEqual(kh2_scd.decode(r[0]).tolist(), [[0], [-3], [-2], [-226]])

    def test_stereo_interleaved(self):
        data = block([0, 0], [16, 16], [10, -10], [5, -5], [1, 15, 0, 0])
        r = kh2_scd.recordings(made_up_bank([(2, 48000, 12, adpcm_format(2, 4, 16), data)]))
        # left 1 then right -1 (15), then both 0
        self.assertEqual(kh2_scd.decode(r[0]).tolist(), [[5, -5], [10, -10], [26, -26], [26, -26]])

    def test_writes_a_wav(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "a" / "b.wav"
            kh2_scd.write_wav(path, np.array([[1, -1], [2, -2]], np.int16), 48001)
            with wave.open(str(path)) as w:
                self.assertEqual((w.getnchannels(), w.getsampwidth(), w.getframerate()), (2, 2, 48001))
                self.assertEqual(np.frombuffer(w.readframes(2), "<i2").tolist(), [1, -1, 2, -2])

    def test_refused(self):
        with self.assertRaises(kh2_scd.ScdError):
            kh2_scd.recordings(b"SEDBSSCX" + bytes(0x60))
        with self.assertRaises(kh2_scd.ScdError):
            kh2_scd.recordings(kh2_scd.MAGIC + bytes(0x10))  # cut short
        bank = bytearray(made_up_bank([(1, 48000, 12, adpcm_format(1, 4, 8), block([0], [16], [1], [1], [0, 0]))]))
        with self.assertRaises(kh2_scd.ScdError):
            kh2_scd.recordings(bytes(bank[:-3]))  # its data past the end
        vorbis = kh2_scd.recordings(made_up_bank([(2, 44100, 6, b"", b"ogg")]))[0]
        with self.assertRaises(kh2_scd.ScdError):
            kh2_scd.decode(vorbis)
        pcm = kh2_scd.recordings(made_up_bank([(1, 48000, 12, b"\x01\x00" + bytes(48), b"")]))[0]
        with self.assertRaises(kh2_scd.ScdError):
            kh2_scd.decode(pcm)  # a WAVEFORMATEX that is not ADPCM's


if __name__ == "__main__":
    unittest.main()
