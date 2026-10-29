"""Add a private white limit divider, using the gray ring's edge and clip 123's transform."""

import argparse
import json
from pathlib import Path
import struct

from kh2_add_limit_gauge import replace_sequence, tables

WHITE_RGBA = bytes.fromhex("FF FF FF 80")


def add_limit_marker(data: bytes) -> tuple[bytes, int]:
    entry, rows = tables(data)
    parts, groups, sprites, animations, clips = rows
    if len(sprites) <= 95 or len(clips) <= 123:
        raise ValueError("gaug does not contain the gray ring and endpoint clip")
    first_group, count = struct.unpack("<HH", sprites[95])
    if not count or first_group + count > len(groups):
        raise ValueError("invalid gray ring groups")
    source_part = struct.unpack_from("<I", groups[first_group], 16)[0]
    if source_part >= len(parts):
        raise ValueError("invalid gray ring part")
    part = bytearray(parts[source_part])
    u0, v0, u1, v1 = struct.unpack_from("<4i", part, 4)
    if u1 - u0 != 45 or v1 - v0 != 45:
        raise ValueError("expected the gray ring's 45 by 45 atlas region")
    struct.pack_into("<4i", part, 4, u0, v1 - 1, u1, v1)
    part[28:44] = WHITE_RGBA * 4
    part_id, group_id, sprite_id, animation_id = map(len, rows[:4])
    if max(group_id, sprite_id, animation_id) >= 65535:
        raise ValueError("new marker references exceed u16")
    parts.append(bytes(part))
    groups.append(struct.pack("<4iI", -45, -1, 0, 1, part_id))
    sprites.append(struct.pack("<HH", group_id, 1))
    clip = bytearray(clips[123])
    first, count = struct.unpack_from("<HH", clip)
    if count != 1 or first >= len(animations):
        raise ValueError("expected the endpoint clip's single transform")
    animation = bytearray(animations[first])
    struct.pack_into("<I", animation, 4, sprite_id)
    animations.append(bytes(animation))
    struct.pack_into("<HH", clip, 0, animation_id, 1)
    clip_id = len(clips)
    clips.append(bytes(clip))
    return replace_sequence(data, entry, rows), clip_id


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--config", type=Path, help="update only the limit_marker clip assignment")
    args = parser.parse_args()
    path = args.archive.resolve(strict=True)
    backup = path.with_name(path.name + ".before-marker.bak")
    current = path.read_bytes()
    original = backup.read_bytes() if backup.exists() else current
    result, index = add_limit_marker(original)
    if current not in (original, result):
        raise ValueError("archive changed since this conversion; preserving your edits")
    if current != result:
        if not backup.exists():
            with backup.open("xb") as file:
                file.write(original)
        path.write_bytes(result)
    if args.config:
        config = json.loads(args.config.read_text(encoding="utf-8"))
        config["limit_marker"] = index
        args.config.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    print(f"Limit marker clip {index}, stored sprite RGBA FFFFFF80: {path}")


if __name__ == "__main__":
    main()
