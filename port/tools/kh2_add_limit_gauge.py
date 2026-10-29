"""Add private yellow limit sprites and clips to a PC gaug archive, without changing the originals."""

import argparse
import json
from pathlib import Path
import struct

STRIDES = (44, 20, 4, 144, 36)
YELLOW_RGBA = bytes.fromhex("FF E6 00 80")


def tables(data: bytes) -> tuple[int, list[list[bytes]]]:
    if len(data) < 16 or data[:4] != b"BAR\x01":
        raise ValueError("invalid PC BAR")
    count = struct.unpack_from("<I", data, 4)[0]
    if count > (len(data) - 16) // 16:
        raise ValueError("invalid BAR table")
    entries = [16 + i * 16 for i in range(count)]
    matches = [p for p in entries if struct.unpack_from("<I", data, p)[0] == 25 and data[p + 4 : p + 8] == b"gaug"]
    if len(matches) != 1:
        raise ValueError("expected one gaug SEQD")
    entry = matches[0]
    start, size = struct.unpack_from("<II", data, entry + 8)
    if size < 48 or start > len(data) - size or data[start : start + 8] != b"SEQD\x60\x01\0\0":
        raise ValueError("invalid SEQD 0x160")
    result = []
    for i, stride in enumerate(STRIDES):
        length, offset = struct.unpack_from("<II", data, start + 8 + i * 8)
        if offset < 48 or offset > size or length > (size - offset) // stride:
            raise ValueError("invalid SEQD table")
        result.append([data[start + offset + j * stride : start + offset + (j + 1) * stride] for j in range(length)])
    return entry, result


def replace_sequence(data: bytes, entry: int, rows: list[list[bytes]]) -> bytes:
    start = struct.unpack_from("<I", data, entry + 8)[0]
    sequence = bytearray(data[start : start + 48])
    for i, row in enumerate(rows):
        struct.pack_into("<II", sequence, 8 + i * 8, len(row), len(sequence))
        sequence.extend(b"".join(row))
    # Append so other BAR resources retain their original offsets and bytes.
    out = bytearray(data)
    out.extend(b"\0" * (-len(out) % 16))
    struct.pack_into("<II", out, entry + 8, len(out), len(sequence))
    out.extend(sequence)
    return bytes(out)


def add_limit_gauge(data: bytes) -> tuple[bytes, tuple[int, int]]:
    entry, rows = tables(data)
    parts, groups, sprites, animations, clips = rows
    if len(sprites) <= 96 or len(clips) <= 129:
        raise ValueError("gaug does not contain the required sprites/clips")
    new_clips = (len(clips), len(clips) + 1)
    for source_sprite, source_clip in ((95, 128), (96, 129)):
        first_group, group_count = struct.unpack("<HH", sprites[source_sprite])
        if first_group + group_count > len(groups) or not group_count:
            raise ValueError("invalid source sprite groups")
        first_new_group = len(groups)
        copied_parts = {}
        for g in groups[first_group : first_group + group_count]:
            group = bytearray(g)
            source_part = struct.unpack_from("<I", group, 16)[0]
            if source_part >= len(parts):
                raise ValueError("invalid source sprite part")
            if source_part not in copied_parts:
                copied_parts[source_part] = len(parts)
                part = bytearray(parts[source_part])
                part[28:44] = YELLOW_RGBA * 4
                parts.append(bytes(part))
            struct.pack_into("<I", group, 16, copied_parts[source_part])
            groups.append(bytes(group))
        sprite_id = len(sprites)
        if len(groups) > 65535 or sprite_id > 65535:
            raise ValueError("new sprite references exceed u16")
        sprites.append(struct.pack("<HH", first_new_group, group_count))
        clip = bytearray(clips[source_clip])
        first_animation, animation_count = struct.unpack_from("<HH", clip)
        if first_animation + animation_count > len(animations) or not animation_count:
            raise ValueError("invalid source animation range")
        first_new_animation = len(animations)
        for a in animations[first_animation : first_animation + animation_count]:
            animation = bytearray(a)
            struct.pack_into("<I", animation, 4, sprite_id)
            animations.append(bytes(animation))
        if len(animations) > 65535:
            raise ValueError("new animation references exceed u16")
        struct.pack_into("<HH", clip, 0, first_new_animation, animation_count)
        clips.append(bytes(clip))
    return replace_sequence(data, entry, rows), new_clips


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--config", type=Path, help="update only the two limit clip assignments")
    args = parser.parse_args()
    path = args.archive.resolve(strict=True)
    backup = path.with_name(path.name + ".before-limit.bak")
    current = path.read_bytes()
    original = backup.read_bytes() if backup.exists() else current
    result, indices = add_limit_gauge(original)
    if current not in (original, result):
        raise ValueError("archive changed since this conversion; preserving your edits")
    if current != result:
        if not backup.exists():
            with backup.open("xb") as file:
                file.write(original)
        path.write_bytes(result)
    if args.config:
        config = json.loads(args.config.read_text(encoding="utf-8"))
        config["limit_circle"], config["limit_bar"] = indices
        args.config.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    print(f"Limit clips {indices[0]}/{indices[1]}, stored sprite RGBA FFE60080: {path}")


if __name__ == "__main__":
    main()
