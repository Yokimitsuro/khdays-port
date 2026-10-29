"""Append player-style party gauge sprites/clips using KH2's smaller atlas parts."""

import struct
from copy import deepcopy

from kh2_add_limit_gauge import replace_sequence, tables

RATIO = 23 / 45
SOURCE_CLIPS = (*range(121, 130), *range(132, 139))


def add_party_gauges(data: bytes) -> tuple[bytes, dict[str, int]]:
    entry, rows = tables(data)
    parts, groups, sprites, animations, clips = rows
    if len(parts) <= 128 or len(sprites) <= 117 or len(clips) <= 138:
        raise ValueError("gaug lacks the player and party gauge resources")
    for index, size in ((101, (23, 23)), (102, (14, 27)), (103, (23, 23)), (105, (23, 23))):
        u0, v0, u1, v1 = struct.unpack_from("<4i", parts[index], 4)
        if (u1 - u0, v1 - v0) != size:
            raise ValueError(f"unexpected party atlas part {index}")

    def part(source, uv=None, color=None):
        p = bytearray(parts[source])
        if uv is not None:
            struct.pack_into("<4i", p, 4, *uv)
        if color is not None:
            p[28:44] = color
        result = len(parts)
        parts.append(bytes(p))
        return result

    mirrored_parts = {}

    def sprite(rects):
        first = len(groups)
        for x0, y0, x1, y1, p in rects:
            flip = int(x0 > x1) | (int(y0 > y1) << 1)
            if flip:
                key = (p, flip)
                if key not in mirrored_parts:
                    u0, v0, u1, v1 = struct.unpack_from("<4i", parts[p], 4)
                    colors = struct.unpack_from("<4I", parts[p], 28)
                    mirrored_parts[key] = part(
                        p,
                        (
                            u1 if flip & 1 else u0,
                            v1 if flip & 2 else v0,
                            u0 if flip & 1 else u1,
                            v0 if flip & 2 else v1,
                        ),
                        struct.pack("<4I", *(colors[v ^ flip] for v in range(4))),
                    )
                p = mirrored_parts[key]
            # Keep shared-edge ownership consistent; mirror UVs rather than rectangle winding.
            groups.append(struct.pack("<4iI", min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1), p))
        result = len(sprites)
        sprites.append(struct.pack("<HH", first, len(rects)))
        return result

    def ring(p):
        return [(-23, -23, 0, 0, p), (23, -23, 0, 0, p), (23, 23, 0, 0, p)]

    def edge(source, color=None):
        u0, v0, u1, v1 = struct.unpack_from("<4i", parts[source], 4)
        return part(source, (u1, v0, u1, v1), color)

    red = parts[108][28:44]
    background, green, damage = part(101), part(103), part(105, color=red)
    u0, v0, u1, v1 = struct.unpack_from("<4i", parts[101], 4)
    black = part(101, (u0, v1 - 1, u1, v1), bytes.fromhex("00000080") * 4)
    seam = part(101, (u0, v1 - 1, u1, v1))
    half = part(102)
    # Width 100 is a normalization for the runtime's bar stretch, not the artwork's physical size.
    replacement = {
        91: sprite([(-23, 0, 0, 1, seam), *ring(background)]),
        92: sprite([(-100, 23, 0, 0, edge(101))]),
        93: sprite(ring(green)),
        94: sprite([(-100, 23, 0, 0, edge(103))]),
        95: sprite(ring(damage)),
        96: sprite([(-100, 23, 0, 0, edge(105, red))]),
        97: sprite([(-23, 0, 0, -1, black)]),
        117: sprite([(-14, -14, 0, 14, half), (14, -14, 0, 14, half)]),
    }

    def scaled_sprite(index):
        if index in replacement:
            return replacement[index]
        first, count = struct.unpack("<HH", sprites[index])
        rects = []
        for g in groups[first : first + count]:
            *rect, p = struct.unpack("<4iI", g)
            rects.append((*[round(v * RATIO) for v in rect], p))
        replacement[index] = sprite(rects)
        return replacement[index]

    mapping = {}
    for source in SOURCE_CLIPS:
        clip = bytearray(clips[source])
        first, count = struct.unpack_from("<HH", clip)
        destination = len(animations)
        for a in animations[first : first + count]:
            a = bytearray(a)
            struct.pack_into("<I", a, 4, scaled_sprite(struct.unpack_from("<I", a, 4)[0]))
            for offset in range(16, 48, 4):
                struct.pack_into("<i", a, offset, round(struct.unpack_from("<i", a, offset)[0] * RATIO))
            for offset in range(112, 128, 4):
                struct.pack_into("<f", a, offset, struct.unpack_from("<f", a, offset)[0] * RATIO)
            animations.append(bytes(a))
        if max(len(groups), len(sprites), len(animations)) > 65535:
            raise ValueError("party gauge references exceed u16")
        struct.pack_into("<HH", clip, 0, destination, count)
        mapping[str(source)] = len(clips)
        clips.append(bytes(clip))
    return replace_sequence(data, entry, rows), mapping


def party_config(player: dict, mapping: dict[str, int]) -> dict:
    result = {
        key: deepcopy(value)
        for key, value in player.items()
        if key
        not in (
            "party",
            "slots",
            "limit_text",
            "enemy",
            "reticles",
            "information",
            "obtained",
            "backpack_full",
            "breakthrough",
            "locked_on",
            "mission_panel",
            "heart_points",
            "presentation",
        )
    }
    result.update(
        offset_x=0,
        offset_y=0,
        portrait_offset_x=0,
        portrait_offset_y=0,
        source_origin_x=0,
        source_origin_y=0,
        geometry_scale=RATIO,
        limit_circle=-1,
        limit_bar=-1,
        limit_marker=-1,
    )
    # Resolve the original player defaults, never infer an unknown customized clip's meaning.
    names = {
        "center_normal": 137,
        "center_alert": 132,
        "center_dead": 137,
        "center_damage_small": 135,
        "center_damage_medium": 134,
        "center_damage_large": 133,
        "center_intro": 136,
        "center_fade": 138,
        "circle_normal": 121,
        "circle_alert": 122,
        "circle_dead": 121,
        "bar_normal": 124,
        "bar_alert": 125,
        "bar_dead": 124,
        "circle_damage": 126,
        "bar_damage": 127,
        "circle_fill": 128,
        "bar_fill": 129,
        "cap": 123,
    }
    result.update({key: mapping[str(clip)] for key, clip in names.items()})
    result["center_override"] = -1
    return result
