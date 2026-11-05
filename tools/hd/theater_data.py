"""The 358/2 Days theater of KINGDOM HEARTS HD 1.5+2.5 ReMIX read from the player's own copy: its
scenes (data/DaysTheater.thd), its chapters (data/DaysTheater.chp), each scene's lines in a language
(its event's TEXT_MOVIE schedule and the CTD tables), and its videos, scrambled or already plain.

MARE is `<collection>/Image/Mare.hed_out/original` (an extraction of Image/Mare).
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import theater_movies as tm  # noqa: E402

PLAIN_MARK = b"ftyp"  # bytes 4-7 of a plain MP4


def _string(data: bytes, base: int, offset: int) -> str:
    return data[base + offset : data.index(b"\0", base + offset)].decode("ascii")


def scenes(mare: Path) -> list[dict]:
    """DaysTheater.thd ("THD@", the count a u16 at 6): 28-byte records from 0x10, then their strings;
    a record's u16s at 8, 12 and 16 are the offsets of its event file, video and music."""
    data = (mare / "data" / "DaysTheater.thd").read_bytes()
    if data[:4] != b"THD@":
        raise ValueError("DaysTheater.thd: not a THD")
    count = struct.unpack_from("<H", data, 6)[0]
    base = 0x10 + 28 * count
    out = []
    for i in range(count):
        record = struct.unpack_from("<14H", data, 0x10 + 28 * i)
        out.append(
            {
                "event": _string(data, base, record[4]),
                "video": _string(data, base, record[6]),
                "music": _string(data, base, record[8]),
            }
        )
    return out


def chapters(mare: Path) -> list[dict]:
    """DaysTheater.chp ("CHP@", the count a u16 at 6): 96-byte records from 8; a record's u16 at 2 its
    day, at 8 its artwork's name's offset, at 32 its scenes' count and from 36 their indices."""
    data = (mare / "data" / "DaysTheater.chp").read_bytes()
    if data[:4] != b"CHP@":
        raise ValueError("DaysTheater.chp: not a CHP")
    count = struct.unpack_from("<H", data, 6)[0]
    base = 8 + 96 * count
    out = []
    for i in range(count):
        record = struct.unpack_from("<48H", data, 8 + 96 * i)
        out.append(
            {"day": record[1], "art": _string(data, base, record[4]), "scenes": list(record[18 : 18 + record[16]])}
        )
    return out


def story(mare: Path) -> list[dict]:
    """The scenes with a video in the theater's order: chapter, day, event stem, video file."""
    all_scenes = scenes(mare)
    out = []
    for number, chapter in enumerate(chapters(mare)):
        for index in chapter["scenes"]:
            scene = all_scenes[index]
            if scene["video"]:
                stem = scene["event"].split("/")[0] if scene["event"] else Path(scene["video"]).stem
                out.append(
                    {"chapter": number, "day": chapter["day"], "scene": index, "stem": stem, "video": scene["video"]}
                )
    return out


def lines(mare: Path, lang: str) -> dict[str, list[tuple[int, int, str | None]]]:
    """Every event's lines in `lang` (es, en, fr, de, it): (first frame, end frame, text) at 30 a
    second; None for a line whose words the tables lack."""
    folder, name, language = tm.LANGUAGES[lang]
    root = mare / folder
    words = {}
    for table in tm.WORDS:
        words.update(tm.ctd(root / "message" / name / "event" / "hd" / "bin" / table))
    out = {}
    for event in sorted((root / "event" / name / "hd").glob("hd*/hd*.exia2")):
        out[event.stem] = [
            (first, end, None if (t := words.get((text_id >> 16, text_id & 0xFFFF))) is None else t.replace("\r", ""))
            for first, end, text_id, *_fades in tm.schedule(event, language)
        ]
    return out


def video_bytes(path: Path) -> bytes:
    """A theater video as a plain MP4: its first 0x100 bytes unscrambled, or taken as they are when
    the copy already holds it plain (bytes 4-7 "ftyp")."""
    data = bytearray(path.read_bytes())
    if data[4:8] != PLAIN_MARK:
        for i in range(min(tm.SCRAMBLED, len(data))):
            data[i] ^= tm.PATTERN[i % 16]
    if data[4:8] != PLAIN_MARK:
        raise ValueError(f"{path}: no MP4 header, scrambled or not")
    return bytes(data)
