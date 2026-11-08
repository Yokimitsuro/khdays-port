"""KINGDOM HEARTS II's PC sound banks (se/se<bank>.win32.scd), as we read them (docs/FORMATS.md, "KH2's sound
banks"): each recording's format and data, and Microsoft ADPCM decoded to 16-bit samples as Microsoft's description
of the format gives it (the KH2 Prizes mod's Dark Corridor sounds, tools/mods/kh2_prizes.py)."""

from __future__ import annotations

import struct
import wave
from dataclasses import dataclass
from pathlib import Path

import numpy as np

MAGIC = b"SEDBSSCF"
TABLES = 0x30  # the tables' header: u16 counts (-, -, recordings, -), then u32 offsets (-, recordings, ...)
MS_ADPCM = 12  # a recording's codec: Microsoft ADPCM (its WAVEFORMATEX in the extra); 6 is Ogg Vorbis
RECORDING_HEADER = 0x20  # u32 data size, channels, rate, codec, loop start, loop end, extra size, -
WAVE_FORMAT_ADPCM = 2
# how each nibble scales the step (Microsoft's ADPCM description)
ADAPT = np.array([230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230], np.int64)


class ScdError(ValueError):
    """Not a sound bank this reads, or a recording it cannot decode."""


@dataclass
class Recording:
    number: int  # its place in the bank's recordings (an event's sound n is recording n - 1)
    channels: int
    rate: int
    codec: int
    loop: tuple[int, int]
    extra: bytes  # the codec's header (a WAVEFORMATEX for MS_ADPCM)
    data: bytes


def recordings(bank: bytes) -> list[Recording | None]:
    """The bank's recordings in order, None for an empty place."""
    if bank[:8] != MAGIC:
        raise ScdError("not a SEDBSSCF sound bank")
    if len(bank) < TABLES + 0x20:
        raise ScdError("its tables' header is cut short")
    count = struct.unpack_from("<H", bank, TABLES + 4)[0]
    table = struct.unpack_from("<I", bank, TABLES + 0xC)[0]
    if table + 4 * count > len(bank):
        raise ScdError("its recordings' table lies past its end")
    out: list[Recording | None] = []
    for n in range(count):
        at = struct.unpack_from("<I", bank, table + 4 * n)[0]
        if at == 0:
            out.append(None)
            continue
        if at + RECORDING_HEADER > len(bank):
            raise ScdError(f"recording {n} lies past its end")
        size, channels, rate, codec, loop_start, loop_end, extra = struct.unpack_from("<7I", bank, at)
        start = at + RECORDING_HEADER
        if start + extra + size > len(bank):
            raise ScdError(f"recording {n}'s data lies past its end")
        out.append(
            Recording(
                n,
                channels,
                rate,
                codec,
                (loop_start, loop_end),
                bank[start : start + extra],
                bank[start + extra : start + extra + size],
            )
        )
    return out


@dataclass
class Sound:
    number: int  # its place in the bank's sounds: KH2's sound id % 1000 (FUN_1401de150: the bank the id / 1000)
    volume: float  # its +8 float
    recordings: list[int]  # the recordings its tracks play


def sounds(bank: bytes) -> list[Sound | None]:
    """The bank's sounds in order, None for an empty place or one with no track. The sounds' table is the first
    after the tables' header (at 0x50, its count the header's first): a u32 offset a sound. A sound: u8 its tracks'
    count, ..., +8 a float (its volume), +0xc its number; its tracks' references last, a u16 track and a u16
    recording each. The sounds lie end to end, each header as long as the bank's others (16 or 64 bytes in the PC's
    banks): a sound's references are the bytes before the next sound, the last one's after the bank's header."""
    if bank[:8] != MAGIC:
        raise ScdError("not a SEDBSSCF sound bank")
    if len(bank) < TABLES + 0x20:
        raise ScdError("its tables' header is cut short")
    count, tracks_count, recordings_count = struct.unpack_from("<3H", bank, TABLES)
    table = TABLES + 0x20
    if table + 4 * count > len(bank):
        raise ScdError("its sounds' table lies past its end")
    places = list(struct.unpack_from(f"<{count}I", bank, table))
    heads: dict[int, int] = {}
    for n, at in enumerate(places[:-1]):
        after = places[n + 1]
        if at and after > at and at < len(bank):
            head = after - at - 4 * bank[at]
            heads[head] = heads.get(head, 0) + 1
    usual = max(heads, key=lambda h: heads[h]) if heads else 0x10
    out: list[Sound | None] = []
    for n, at in enumerate(places):
        if at == 0 or at + 0x10 > len(bank) or bank[at] == 0:
            out.append(None)
            continue
        tracks = bank[at]
        after = places[n + 1] if n + 1 < len(places) and places[n + 1] > at else at + usual + 4 * tracks
        refs = after - 4 * tracks
        if refs < at + 0x10 or after > len(bank):
            raise ScdError(f"sound {n}'s tracks lie outside it")
        volume = struct.unpack_from("<f", bank, at + 8)[0]
        played = []
        for t in range(tracks):
            track, recording = struct.unpack_from("<HH", bank, refs + 4 * t)
            if track >= tracks_count or recording >= recordings_count:
                raise ScdError(f"sound {n}'s track {t} names a track or recording the bank has not")
            played.append(recording)
        out.append(Sound(n, volume, played))
    return out


def decode(recording: Recording) -> np.ndarray:
    """A Microsoft ADPCM recording's samples, (frames, channels) int16. A block: per channel a predictor (u8), then
    per channel the step, sample 1 and sample 2 (s16 each); sample 2 then sample 1 come out first; then a nibble a
    sample, the high one first, the channels interleaved. Each next sample is the predictor's (s1 c1 + s2 c2) / 256,
    the division toward zero, plus the nibble (signed) times the step."""
    if recording.codec != MS_ADPCM:
        raise ScdError(f"recording {recording.number}: codec {recording.codec} is not Microsoft ADPCM")
    extra = recording.extra
    if len(extra) < 22:
        raise ScdError(f"recording {recording.number}: its format is cut short")
    tag, ch, _rate, _bytes, align, _bits, _size, per, coefficients = struct.unpack_from("<HHIIHHHHH", extra, 0)
    if tag != WAVE_FORMAT_ADPCM or ch not in (1, 2) or per < 2 or align < 7 * ch or len(extra) < 22 + 4 * coefficients:
        raise ScdError(f"recording {recording.number}: not a Microsoft ADPCM format this reads")
    if coefficients == 0 or (per - 2) * ch > 2 * (align - 7 * ch):
        raise ScdError(f"recording {recording.number}: its blocks do not hold their samples")
    pairs = np.array(struct.unpack_from(f"<{2 * coefficients}h", extra, 22), np.int64).reshape(coefficients, 2)
    blocks = len(recording.data) // align
    if blocks == 0:
        return np.zeros((0, ch), np.int16)
    b = np.frombuffer(recording.data[: blocks * align], np.uint8).reshape(blocks, align)

    def s16(at: int) -> np.ndarray:
        return b[:, at : at + 2 * ch].copy().view("<i2").astype(np.int64).reshape(blocks, ch)

    predictor = np.clip(b[:, :ch].astype(np.int64), 0, coefficients - 1)
    step, s1, s2 = s16(ch), s16(3 * ch), s16(5 * ch)
    c1, c2 = pairs[predictor, 0], pairs[predictor, 1]
    out = np.zeros((blocks, per, ch), np.int64)
    out[:, 0], out[:, 1] = s2, s1
    packed = b[:, 7 * ch :]
    nibbles = np.stack([packed >> 4, packed & 15], axis=2).reshape(blocks, -1).astype(np.int64)
    nibbles = nibbles[:, : (per - 2) * ch].reshape(blocks, per - 2, ch)
    for t in range(per - 2):
        u = nibbles[:, t]
        q = s1 * c1 + s2 * c2
        v = np.clip(np.where(q < 0, -((-q) // 256), q // 256) + np.where(u >= 8, u - 16, u) * step, -32768, 32767)
        s2, s1 = s1, v
        step = np.maximum((ADAPT[u] * step) // 256, 16)
        out[:, t + 2] = v
    return out.reshape(blocks * per, ch).astype(np.int16)


def write_wav(path: Path, samples: np.ndarray, rate: int) -> None:
    """16-bit PCM WAV of `samples` ((frames, channels) int16)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(samples.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(samples.astype("<i2").tobytes())
