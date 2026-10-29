"""Recenter selected gaug clips in a private PC BAR archive, preserving motion."""

import argparse
from pathlib import Path
import struct


def _recenter(data: bytes, origins: dict[int, tuple[int, int] | None]) -> bytes:
    def u32(at: int) -> int:
        return struct.unpack_from("<I", data, at)[0]

    if len(data) < 16 or data[:4] != b"BAR\x01" or u32(4) > (len(data) - 16) // 16:
        raise ValueError("invalid PC BAR")
    entries = []
    for i in range(u32(4)):
        at = 16 + i * 16
        if u32(at) == 0x19 and data[at + 4 : at + 8] == b"gaug":
            entries.append((u32(at + 8), u32(at + 12)))
    if len(entries) != 1:
        raise ValueError("expected one gaug SEQD")
    start, size = entries[0]
    if size < 48 or start > len(data) - size or data[start : start + 4] != b"SEQD":
        raise ValueError("invalid gaug SEQD range")
    if u32(start + 4) != 0x160:
        raise ValueError("unsupported SEQD version")
    count, animations, clips, table = (u32(start + x) for x in (32, 36, 40, 44))
    if clips <= max(origins) or animations > size or count > (size - animations) // 144:
        raise ValueError("invalid animation table")
    if table > size or clips > (size - table) // 36:
        raise ValueError("invalid clip table")
    selected, other = {}, set()
    for clip in range(clips):
        first, length = struct.unpack_from("<HH", data, start + table + clip * 36)
        if first + length > count:
            raise ValueError("invalid animation references")
        if clip in origins and origins[clip] is None:
            active = [
                start + animations + index * 144
                for index in range(first, first + length)
                if u32(start + animations + index * 144) & 2
            ]
            if not active:
                raise ValueError("clip has no active text anchor")
            anchor = min(active, key=lambda at: struct.unpack_from("<i", data, at + 8)[0])
            origins[clip] = (
                struct.unpack_from("<i", data, anchor + 16)[0],
                struct.unpack_from("<i", data, anchor + 24)[0],
            )
        for animation in range(first, first + length):
            if clip in origins:
                if animation in selected and selected[animation] != origins[clip]:
                    raise ValueError("shared tracks have different origins")
                selected[animation] = origins[clip]
            else:
                other.add(animation)
    if selected.keys() & other:
        raise ValueError("center tracks are shared with other clips; refusing to move unrelated data")
    out = bytearray(data)
    for index in sorted(selected):
        x, y = selected[index]
        for offset, origin in ((16, x), (20, x), (24, y), (28, y)):
            at = start + animations + index * 144 + offset
            value = struct.unpack_from("<i", data, at)[0] - origin
            if not -(2**31) <= value < 2**31:
                raise ValueError("translated coordinate overflows i32")
            struct.pack_into("<i", out, at, value)
    return bytes(out)


def recenter_gauge(data: bytes) -> bytes:
    return _recenter(data, {clip: (455, 355) for clip in range(132, 139)})


def recenter_obtained(data: bytes) -> bytes:
    # English anchors at 199,340; the other western PC languages anchor at 256,340.
    return _recenter(data, {76: None})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    path = args.archive.resolve(strict=True)
    backup = path.with_name(path.name + ".original.bak")
    current = path.read_bytes()
    original = backup.read_bytes() if backup.exists() else current
    result = recenter_gauge(original)
    if current == result:
        print(f"Already centered: {path}")
        return
    if current != original:
        raise ValueError("archive differs from its backup and the centered output; preserving your changes")
    if not backup.exists():
        with backup.open("xb") as file:
            file.write(original)
    path.write_bytes(result)
    print(f"Centered clips 132-138: {path}\nOriginal preserved: {backup}")


if __name__ == "__main__":
    main()
