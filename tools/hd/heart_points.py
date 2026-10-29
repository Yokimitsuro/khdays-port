"""Export Days' heart-counter art and editable SEQD clips from the user's extracted ROM."""

from __future__ import annotations

import argparse
import json
import struct
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from save_screen import p2_entry  # noqa: E402

NAMES = [
    *(f"digit_{i}" for i in range(10)),
    "heart",
    "glow",
    "flash",
    "ring",
    "line",
    "fire",
    "particle",
    *(f"small_digit_{i}" for i in range(10)),
    "minus",
    "plus",
    "bonus",
    "chain",
    "chain_frame",
]
SOURCES = [("arc", 0, str(i)) for i in range(10)] + [
    ("arc", 1, "heart"),
    ("arc", 1, "light02"),
    ("arc", 2, "light"),
    ("arc", 2, "ring"),
    ("arc", 3, "line"),
    ("arc", 4, "fire"),
    ("arc", 5, "particle"),
    *(("arc", 6, f"s_{i}") for i in range(10)),
    ("arc", 6, "s_m"),
    ("arc", 6, "s_p"),
    ("arc_{lang}", 0, "bonus"),
    ("arc_{lang}", 1, "chain"),
    ("arc_{lang}", 2, "ef_hp_mp"),
]
LANGUAGES = ("en", "es", "fr", "de", "it")


def u16(b, at):
    return struct.unpack_from("<H", b, at)[0]


def u32(b, at):
    return struct.unpack_from("<I", b, at)[0]


def dictionary(b, at):
    header = at + u16(b, at + 6)
    unit, names = struct.unpack_from("<HH", b, header)
    return {
        b[header + names + i * 16 : header + names + (i + 1) * 16].split(b"\0")[0].decode("ascii"): header
        + 4
        + unit * i
        for i in range(b[at + 1])
    }


def textures(b):
    """KAPH section 7's BMD0/TEX0; counter materials bind <texture>_pl palettes."""
    section = u32(b, 8 + 7 * 4)
    if b[:4] != b"KAPH" or u32(b, section) != 1:
        raise ValueError("expected one heart-counter model in KAPH section 7")
    model = u32(b, section + 4)
    if b[model : model + 4] != b"BMD0":
        raise ValueError("missing counter BMD0")
    tex = next(
        (
            model + u32(b, model + 16 + 4 * i)
            for i in range(u16(b, model + 14))
            if b[model + u32(b, model + 16 + 4 * i) :][:4] == b"TEX0"
        ),
        None,
    )
    if tex is None:
        raise ValueError("missing counter TEX0")
    palettes = dictionary(b, tex + u16(b, tex + 52))
    result = {}
    for name, item in dictionary(b, tex + u16(b, tex + 14)).items():
        param = u32(b, item)
        fmt = (param >> 26) & 7
        if fmt not in (1, 6):
            raise ValueError(f"unexpected counter texture format {fmt}")
        w, h = 8 << ((param >> 20) & 7), 8 << ((param >> 23) & 7)
        pixels = tex + u32(b, tex + 20) + (param & 65535) * 8
        palette = tex + u32(b, tex + 56) + u16(b, palettes[name + "_pl"]) * 8
        image = Image.new("RGBA", (w, h))
        rgba = []
        for value in b[pixels : pixels + w * h]:
            index = value & (31 if fmt == 1 else 7)
            alpha = value >> (5 if fmt == 1 else 3)
            if fmt == 1:
                alpha = alpha * 4 + alpha // 2
            colour = u16(b, palette + index * 2)
            rgba.append(
                tuple(((((colour >> shift) & 31) << 3) | (((colour >> shift) & 31) >> 2)) for shift in (0, 5, 10))
                + (round(alpha * 255 / 31),)
            )
        image.putdata(rgba)
        result[name] = image
    return result


# Logical units use the existing HUD's nominal 0.65 scale.
SCALE = 0.65
PX = 256 / (2 * 0x4D9A / 4096)
PY = 192 / (2 * 0x3B33 / 4096)
CLIPS = [
    "heart_idle",
    "heart_active",
    "pickup",
    "background",
    "streak",
    "particles_0",
    "particles_1",
    "particles_2",
    "chain_intro",
    "chain_loop",
    "chain_outro",
    "bonus",
    *(f"digit_{i}" for i in range(10)),
    *(f"small_digit_{i}" for i in range(10)),
    "minus",
    "plus",
    "gain",
    "bonus_gain",
    "total_gain",
    "total_loss",
    "total_gain_medium",
    "total_gain_large",
    "heart_grow",
    "total_gain_final",
    "chain_warning",
]


def native_executable(explicit=None):
    root = Path(__file__).resolve().parents[2]
    for candidate in (
        [explicit] if explicit else [root / "khdays-native.exe", root / "build/port/obj-release-x64/khdays-native.exe"]
    ):
        if candidate and candidate.is_file():
            return candidate.resolve()
    raise ValueError("build khdays-native first, or provide --native EXE for heart model conversion")


def model(data, executable):
    with tempfile.TemporaryDirectory(prefix="khdays-heart-") as temp:
        source, output = Path(temp) / "model.kaph", Path(temp) / "model.json"
        source.write_bytes(data)
        subprocess.run(
            [str(executable), "--export-heart-model", str(source), str(output)],
            check=True,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        return json.loads(output.read_text())


def alpha(data, material, frame, default):
    section = u32(data, 16)  # KAPH material colour animation, slot 2
    if section in (0, 0xFFFFFFFF) or not u32(data, section):
        return default
    bma = u32(data, section + 4)
    mat = bma + u32(data, bma + 16)
    entries = dictionary(data, mat + 8)
    anim = mat + u32(data, next(iter(entries.values())))
    records = dictionary(data, anim + 8)
    if material not in records:
        return default
    info = u32(data, records[material] + 16)
    if info & 0x20000000:
        return (info & 65535) / 31
    step = 2 if info & 0x40000000 else 4 if info & 0x80000000 else 1
    head = anim + (info & 65535)
    last = (info >> 16) & 8191
    if step == 1 or frame % step == 0:
        value = data[head + frame // step]
    elif frame > last:
        value = data[head + last // step + frame % step]
    else:
        f = frame % step
        value = (data[head + frame // step] * (step - f) + data[head + frame // step + 1] * f) // step
    return value / 31


def overlay(data):
    """The supported ROM's ov002: derive its table address and file id from the overlay table."""
    root = data.parent if data.name == "nitrofs" else data
    record = (root / "system/arm9_overlay_table.bin").read_bytes()[64:96]
    index, address, size, _, _, _, file_id, flags = struct.unpack("<8I", record)
    if index != 2 or address != 0x0204CAC0:
        raise ValueError("unsupported Days field overlay for heart-counter conversion")
    manifest = json.loads((root / "manifest.json").read_text())
    path = next(f["path"] for f in manifest["files"] if f["file_id"] == file_id)
    b = (root / path).read_bytes()
    if flags >> 24 & 1:
        header, extra = struct.unpack_from("<II", b, len(b) - 8)
        out = bytearray(b) + bytearray(extra)
        src, end, dst = len(b) - (header >> 24), len(b) - (header & 0xFFFFFF), len(out)
        while src > end:
            src -= 1
            flag = out[src]
            for bit in range(8):
                if src <= end:
                    break
                if flag & (128 >> bit):
                    src -= 1
                    high = out[src]
                    src -= 1
                    distance = ((high & 15) << 8 | out[src]) + 3
                    for _ in range((high >> 4) + 3):
                        dst -= 1
                        out[dst] = out[dst + distance]
                else:
                    src -= 1
                    dst -= 1
                    out[dst] = out[src]
        b = bytes(out)
    if len(b) < size:
        raise ValueError("truncated Days field overlay")
    return b, address


def material_uv(raw, material, width, height):
    """Apply the counter's 3ds Max texture matrix (texmtxCalc_flagR__2), before repeat/mirror."""
    section = u32(raw, 8 + 7 * 4)
    bmd = u32(raw, section + 4)
    mdl = bmd + u32(raw, bmd + 16)
    model = mdl + u32(raw, next(iter(dictionary(raw, mdl + 8).values())))
    if raw[model + 22] != 2:
        raise ValueError("unsupported counter texture matrix mode")
    materials = model + u32(raw, model + 8)
    record = materials + u32(raw, dictionary(raw, materials + 4)[material])
    flags, param = u16(raw, record + 30), u32(raw, record + 20)
    cursor = record + 44
    sx = sy = 1
    tx = ty = 0
    if not flags & 2:
        sx, sy = (v / 4096 for v in struct.unpack_from("<2i", raw, cursor))
        cursor += 8
    if not flags & 4:
        raise ValueError("counter texture rotation requires additional conversion")
    if not flags & 8:
        tx, ty = (v / 4096 for v in struct.unpack_from("<2i", raw, cursor))
    x = width / 2 - sx * width * (0.5 + tx)
    y = height / 2 + sy * height * (-0.5 + ty)
    return x, y, x + width * sx, y + height * sy, param


def texture_spans(start, end, size, repeat, mirror):
    """Split a model's UV interval into explicit cropped/flipped atlas regions."""
    if end <= start:
        raise ValueError("counter texture matrix must have positive scale")
    edges = [start, end]
    if repeat:
        edges += [i * size for i in range(math.floor(start / size) + 1, math.ceil(end / size))]
    else:
        edges += [v for v in (0, size) if start < v < end]
    edges = sorted(set(edges))
    result = []
    for a, b in zip(edges, edges[1:]):
        tile = math.floor((a + b) / (2 * size)) if repeat else 0
        u, v = a - tile * size, b - tile * size
        if not repeat:
            u, v = max(0, min(size, u)), max(0, min(size, v))
        elif mirror and tile & 1:
            u, v = size - u, size - v
        result.append(((a - start) / (end - start), (b - start) / (end - start), round(u), round(v)))
    return result


def tween(start, end, elapsed, duration, curve=0):
    """The four curves in Days' Fx_Tween, sampled into editable SEQD keys."""
    t = max(0, min(1, elapsed / duration))
    if curve == 1:
        t *= t
    elif curve == 2:
        t = 1 - (1 - t) ** 2
    elif curve == 3:
        t = (1 - (1 - 2 * t) ** 2) / 2 if t < 0.5 else (1 + (1 - 2 * t) ** 2) / 2
    return start + (end - start) * t


def effect_keys(level, slide, vertical=None):
    """Ov002's phase scheduler updates after drawing, with independently completing tweens."""
    tracks = [level, slide, vertical or [(1, 1, 1)] * 3]
    values = [0, 0, 1]
    running = [None, None, None]
    finished = [True] * 3
    step = 1
    result = [tuple(values)]  # The blink event comes after this frame's effect update.
    for frame in range(1, 256):
        result.append(tuple(values))
        phase = step - 1
        if phase >= 3:
            step = 0
        now = frame * 1000 / 30
        if step:
            for i in range(3):
                if finished[i]:
                    running[i] = (*tracks[i][phase], now)
                    finished[i] = False
                    if i == 0:
                        step += 1
        for i, track in enumerate(running):
            if track:
                start, end, duration, stamp = track
                elapsed = now - stamp
                finished[i] = elapsed + 1e-7 >= duration
                values[i] = tween(start, end, duration if finished[i] else elapsed, duration)
        if step == 0 and finished[0]:
            result.append(tuple(values))
            return result
    raise ValueError("counter effect exceeded its native phase bounds")


def key_values(animation, end=False):
    delta = 4 if end else 0
    values = [struct.unpack_from("<i", animation, at + delta)[0] for at in range(16, 48, 8)]
    values += [struct.unpack_from("<f", animation, at + delta)[0] for at in range(48, 96, 8)]
    colour = u32(animation, 136 + delta)
    return values + [(colour >> shift) & 255 for shift in range(0, 32, 8)]


def key_sample(animation, frame):
    start, end = struct.unpack_from("<2i", animation, 8)
    t = (frame - start) / (end - start) if start != end else 1
    if not u32(animation, 0) & 1:
        c1, c3 = struct.unpack_from("<f", animation, 100)[0], struct.unpack_from("<f", animation, 108)[0]
        t = 3 * (1 - t) ** 2 * t * c1 + 3 * (1 - t) * t * t * c3 + t**3
    result = []
    for i, (a, b) in enumerate(zip(key_values(animation), key_values(animation, True), strict=True)):
        result.append(a + int((b - a) * t) if i < 4 or i >= 10 else a + (b - a) * t)
    return result


def compact_keys(keys, rectangles=()):
    """Fit shared SEQD time curves, retaining integer positions and subpixel scale accuracy."""
    keys = list(keys)
    terminal = bytearray(keys[-1])
    start, end = struct.unpack_from("<2i", terminal, 8)
    if end == start + 1 and key_values(terminal) == key_values(terminal, True):
        struct.pack_into("<i", terminal, 12, start)
        struct.pack_into("<I", terminal, 0, u32(terminal, 0) & ~(1 << 15))
        keys[-1] = bytes(terminal)
    result, first = [], 0
    while first < len(keys):
        best, following = keys[first], first + 1
        for last in range(len(keys) - 1, first, -1):
            start = struct.unpack_from("<i", keys[first], 8)[0]
            end = struct.unpack_from("<i", keys[last], 12)[0]
            samples = [key_values(k) for k in keys[first : last + 1]]
            a, b = samples[0], key_values(keys[last], True)
            curves = [(1 / 3, 2 / 3), (0, 1), (0, 1 / 3), (2 / 3, 1)]
            # Fit the dominant scale/rotation channel first; alpha is quantized in the source.
            channel = next((i for i in [8, 9, 7, 6, 5, 4, 0, 1, 13] if abs(b[i] - a[i]) > 1e-5), None)
            if channel is not None:
                aa = ab = bb = ay = by = 0
                for i, sample in enumerate(samples[1:], 1):
                    t = i / (end - start)
                    x, y = 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t
                    value = (sample[channel] - a[channel]) / (b[channel] - a[channel]) - t**3
                    aa += x * x
                    ab += x * y
                    bb += y * y
                    ay += x * value
                    by += y * value
                det = aa * bb - ab * ab
                if det > 1e-12:
                    c1, c3 = (ay * bb - by * ab) / det, (by * aa - ay * ab) / det
                    if -1e-4 <= c1 <= 1.0001 and -1e-4 <= c3 <= 1.0001:
                        curves.append((max(0, min(1, c1)), max(0, min(1, c3))))
            for c1, c3 in curves:
                merged = bytearray(keys[first])
                struct.pack_into("<i", merged, 12, end)
                struct.pack_into("<I", merged, 0, (u32(merged, 0) & ~(1 << 15)) | (u32(keys[last], 0) & (1 << 15)))
                for at in range(16, 96, 8):
                    merged[at + 4 : at + 8] = keys[last][at + 4 : at + 8]
                merged[140:144] = keys[last][140:144]
                if (c1, c3) != curves[0]:
                    struct.pack_into("<I", merged, 0, u32(merged, 0) & ~1)
                    struct.pack_into("<4f", merged, 96, 0, c1, 0, c3)
                valid = True
                for frame, expected in enumerate(samples, start):
                    actual = key_sample(merged, frame)
                    if any(
                        abs(x - y) > (0 if i < 4 or i >= 10 else 1e-4)
                        for i, (x, y) in enumerate(zip(actual, expected, strict=True))
                    ):
                        valid = False
                        break
                    # SEQD transforms quantize vertices to sixteenths of a logical unit.
                    for rect in rectangles:
                        for axis in range(2):
                            for coordinate in (rect[axis], rect[axis + 2]):
                                positions = []
                                for sample in (actual, expected):
                                    scale = sample[7] * sample[8 + axis]
                                    value = (coordinate + sample[2 + axis]) * 16 * scale
                                    positions.append(sample[axis] + int(value) / 16)
                                if positions[0] != positions[1]:
                                    valid = False
                    if not valid:
                        break
                if valid:
                    best, following = merged, last + 1
                    break
            if following == last + 1:
                break
        best = bytearray(best)
        start, end = struct.unpack_from("<2i", best, 8)
        if start == end:
            struct.pack_into("<i", best, 12, start + 1)
            struct.pack_into("<I", best, 0, u32(best, 0) | (1 << 15))
        result.append(bytes(best))
        first = following
    return result


class Layout:
    def __init__(self, rectangles):
        self.rows = [[] for _ in range(5)]
        for x, y, w, h in rectangles:
            self.rows[0].append(struct.pack("<I4i2f4I", 0, x, y, x + w, y + h, 0, 0, *([0x80808080] * 4)))
        self.rectangles = rectangles
        self.names = []

    def animation(self, sprite, start, end, x=0, y=0, sx=1, sy=1, rotation=0, opacity=1, dummy=False):
        a = bytearray(144)
        # Linear interpolation, nearest texels, no sway. A dummy exports an active text/child anchor.
        struct.pack_into("<II2i", a, 0, 1 | 4 | 16 | (2 if dummy else 0), sprite, start, end)
        struct.pack_into("<2i", a, 16, round(x), round(x))
        struct.pack_into("<2i", a, 24, round(y), round(y))
        struct.pack_into("<2f", a, 64, rotation, rotation)
        struct.pack_into("<6f", a, 72, 1, 1, sx, sx, sy, sy)
        colour = 0x808080 | (max(0, min(128, round(opacity * 128))) << 24)
        struct.pack_into("<2I", a, 136, colour, colour)
        return a

    def sprite(self, part, w, h, transform=None):
        first = len(self.rows[1])
        u0, v0, u1, v1, param = transform or (0, 0, w, h, 0)
        xs = texture_spans(u0, u1, w, param & (1 << 16), param & (1 << 18))
        ys = texture_spans(v0, v1, h, param & (1 << 17), param & (1 << 19))
        width, height = u1 - u0, v1 - v0
        atlas_x, atlas_y, _, _ = self.rectangles[part]
        for a, b, left, right in xs:
            for c, d, top, bottom in ys:
                private_part = bytearray(self.rows[0][part])
                struct.pack_into(
                    "<4i", private_part, 4, atlas_x + left, atlas_y + top, atlas_x + right, atlas_y + bottom
                )
                part_id = len(self.rows[0])
                self.rows[0].append(bytes(private_part))
                self.rows[1].append(
                    struct.pack(
                        "<4iI",
                        round((a - 0.5) * width),
                        round((c - 0.5) * height),
                        round((b - 0.5) * width),
                        round((d - 0.5) * height),
                        part_id,
                    )
                )
        self.rows[2].append(struct.pack("<HH", first, len(self.rows[1]) - first))
        return len(self.rows[2]) - 1

    def clip(self, name, animations, loop=1):
        first = len(self.rows[3])
        # One interval per native frame, with interpolated endpoints and no double-drawn boundaries.
        keyed = {(u32(a, 4), struct.unpack_from("<i", a, 8)[0]): a for a in animations}
        for source in animations:
            a = bytearray(source)
            start, end = struct.unpack_from("<2i", a, 8)
            if start == end:
                struct.pack_into("<I", a, 0, u32(a, 0) | (1 << 15))
                struct.pack_into("<i", a, 12, start + 1)
                following = keyed.get((u32(a, 4), start + 1), source)
                for at in range(16, 96, 8):
                    a[at + 4 : at + 8] = following[at : at + 4]
                a[140:144] = following[136:140]
            self.rows[3].append(bytes(a))
        self.rows[4].append(struct.pack("<4H7I", first, len(animations), loop, 0, *([0] * 7)))
        self.names.append(name)

    def mesh_clip(self, name, converted, raw, parts, scale, position, loop=1, static=None, fade=None, frame_offset=0):
        animations = []
        count = converted["frames"] if static is None else 1
        for mesh in converted["meshes"]:
            part = parts[mesh["texture"]]
            _, _, w, h = self.rectangles[part]
            uv = material_uv(raw, mesh["material"], w, h)
            sprite = self.sprite(part, w, h, uv)
            for frame in range(count):
                source_frame = (frame + frame_offset) % count if static is None else static
                vertices = mesh["poses"][source_frame]
                tl, bl, br, tr = vertices  # native quads, verified by their UVs
                vx, vy = tr[0] - tl[0], -(tr[1] - tl[1])
                ux, uy = bl[0] - tl[0], -(bl[1] - tl[1])
                # Counter model quads remain planar rectangles under their BCA transforms.
                if abs(vx * ux + vy * uy) > 1e-3:
                    raise ValueError("counter model needs a non-rectangular SEQD transform")
                sx = math.hypot(vx, vy) * PX * scale / ((uv[2] - uv[0]) * SCALE)
                sy = math.hypot(ux, uy) * PY * scale / ((uv[3] - uv[1]) * SCALE)
                x = (tl[0] + br[0]) / 2 * PX * scale + position[0]
                y = -(tl[1] + br[1]) / 2 * PY * scale + position[1]
                opacity = alpha(raw, mesh["material"], source_frame, mesh["alpha"])
                if fade:
                    opacity *= fade(frame)
                animations.append(
                    self.animation(sprite, frame, frame, x / SCALE, y / SCALE, sx, sy, -math.atan2(vy, vx), opacity)
                )
        self.clip(name, animations, loop)

    def dummy(self, name, length, y0=0, y1=0, alpha0=1, alpha1=1, scale0=1, scale1=1):
        sprite = len(self.rows[2])
        self.rows[2].append(struct.pack("<HH", 0, 0))
        a = self.animation(sprite, 0, length - 1, sy=scale0, sx=scale0, opacity=alpha0, dummy=True)
        struct.pack_into("<2i", a, 24, round(y0), round(y1))
        struct.pack_into("<4f", a, 80, scale0, scale1, scale0, scale1)
        struct.pack_into("<2I", a, 136, 0x808080 | round(alpha0 * 128) << 24, 0x808080 | round(alpha1 * 128) << 24)
        self.clip(name, [a])

    def heart_growth(self):
        sprite = len(self.rows[2])
        self.rows[2].append(struct.pack("<HH", 0, 0))
        animations = []
        for frame in range(6):
            # The native finished flag restores the resting scale after the 150 ms tween.
            size = tween(0xD71, 0x119A, frame * 1000 / 30, 150, 3) / 0xD71 if frame < 5 else 1
            animations.append(self.animation(sprite, frame, frame, sx=size, sy=size, dummy=True))
        self.clip("heart_grow", animations)

    def gain(self, name, bonus=False):
        sprite = len(self.rows[2])
        self.rows[2].append(struct.pack("<HH", 0, 0))
        keys = []
        for frame in range(31):
            elapsed = frame * 1000 // 30
            slide = int(0xBB8000 * elapsed / 1000) >> 12
            level = (0x1F000 - int(0x1F000 * elapsed / 1000)) >> 12
            key = self.animation(sprite, frame, frame, y=-slide / 4096 * PY / SCALE, opacity=level / 31, dummy=True)
            if bonus:
                struct.pack_into("<2I", key, 136, *([0x807C00 | round(level * 128 / 31) << 24] * 2))
            keys.append(key)
        self.clip(name, keys)

    def total_pulse(self, name, peak, final=False):
        sprite = len(self.rows[2])
        self.rows[2].append(struct.pack("<HH", 0, 0))
        keys = []
        for frame in range(5):
            elapsed = min(100, frame * 1000 // 30)
            scale = tween(peak, 1516, elapsed, 100) / 1516
            level = 31 if final or frame == 4 else (0x1F000 - int(0xF000 * elapsed / 100)) >> 12
            keys.append(self.animation(sprite, frame, frame, sx=scale, sy=scale, opacity=level / 31, dummy=True))
        self.clip(name, keys)

    def optimize(self):
        animations, clips = [], []
        for clip in self.rows[4]:
            first, count = struct.unpack_from("<HH", clip)
            records = self.rows[3][first : first + count]
            compact, at = [], 0
            while at < len(records):
                tail = at + 1
                while tail < len(records):
                    previous, current = records[tail - 1], records[tail]
                    start, end = struct.unpack_from("<2i", current, 8)
                    if (
                        u32(current, 4) != u32(records[at], 4)
                        or end != start + 1
                        or start != struct.unpack_from("<i", previous, 12)[0]
                        or current[:8] != previous[:8]
                        or current[96:136] != previous[96:136]
                    ):
                        break
                    tail += 1
                start, end = struct.unpack_from("<2i", records[at], 8)
                sprite = u32(records[at], 4)
                group, length = struct.unpack("<HH", self.rows[2][sprite])
                rectangles = [struct.unpack_from("<4i", g) for g in self.rows[1][group : group + length]]
                compact.extend(compact_keys(records[at:tail], rectangles) if end == start + 1 else records[at:tail])
                at = tail
            updated = bytearray(clip)
            struct.pack_into("<HH", updated, 0, len(animations), len(compact))
            animations.extend(compact)
            clips.append(bytes(updated))

        parts, groups, sprites = [], [], []
        part_ids, sprite_ids, remap = {}, {}, {}
        for old, sprite in enumerate(self.rows[2]):
            first, count = struct.unpack("<HH", sprite)
            converted = []
            for group in self.rows[1][first : first + count]:
                part = self.rows[0][u32(group, 16)]
                if part not in part_ids:
                    part_ids[part] = len(parts)
                    parts.append(part)
                converted.append(group[:16] + struct.pack("<I", part_ids[part]))
            key = tuple(converted)
            if key not in sprite_ids:
                sprite_ids[key] = len(sprites)
                sprites.append(struct.pack("<HH", len(groups) if key else 0, len(key)))
                groups.extend(converted)
            remap[old] = sprite_ids[key]
        for i, animation in enumerate(animations):
            updated = bytearray(animation)
            struct.pack_into("<I", updated, 4, remap[u32(updated, 4)])
            animations[i] = bytes(updated)
        self.rows = [parts, groups, sprites, animations, clips]

    def archive(self, atlas):
        seq = bytearray(b"SEQD\x60\x01\0\0" + bytes(40))
        for i, row in enumerate(self.rows):
            struct.pack_into("<II", seq, 8 + 8 * i, len(row), len(seq))
            seq.extend(b"".join(row))
        # Unswizzled PSMCT32 IMGD, with a real bitmap for layout editors as well as the external PNG.
        img = bytearray(64)
        rgba = bytearray(atlas.tobytes())
        rgba[3::4] = bytes(round(v * 128 / 255) for v in rgba[3::4])
        struct.pack_into(
            "<4s7I", img, 0, b"IMGD", 0x100, 64, len(rgba), 64 + len(rgba), 0, 0, atlas.width | atlas.height << 16
        )
        struct.pack_into("<I", img, 24, 0xFFFFFFFF)
        struct.pack_into("<3H", img, 32, 8, 7, 4)
        struct.pack_into("<I", img, 40, 0xFFFFFFFF)
        struct.pack_into("<4I", img, 44, 0x00100010, 1, 0x00030005, 0)
        struct.pack_into("<H", img, 38, 0)  # PSMCT32
        struct.pack_into("<I", img, 60, 3)  # linear pixels
        img.extend(rgba)
        out = bytearray(struct.pack("<4s3I", b"BAR\x01", 2, 0, 0))
        out.extend(struct.pack("<I4sII", 0x18, b"hart", 48, len(img)))
        out.extend(struct.pack("<I4sII", 0x19, b"hart", 48 + len(img), len(seq)))
        return bytes(out + img + seq)


def paste_texture(atlas, image, x, y):
    """Pad source edges for mirrored seams, including editor half-texel sampling."""
    w, h = image.size
    atlas.paste(image, (x, y))
    atlas.paste(image.crop((0, 0, 1, h)), (x - 1, y))
    atlas.paste(image.crop((w - 1, 0, w, h)), (x + w, y))
    atlas.paste(atlas.crop((x - 1, y, x + w + 1, y + 1)), (x - 1, y - 1))
    atlas.paste(atlas.crop((x - 1, y + h - 1, x + w + 1, y + h)), (x - 1, y + h))


def make(data, output, languages=LANGUAGES, executable=None, settings=None):
    executable = native_executable(executable)
    nitro = data / "nitrofs" if (data / "nitrofs").is_dir() else data
    field, field_base = overlay(nitro)
    shared = {i: p2_entry(nitro / "UI/hcnt/arc", i) for i in range(7)}
    models = {i: model(b, executable) for i, b in shared.items()}

    def screen(x, y):
        return 128 + x / 4096 * PX, 96 - y / 4096 * PY

    def steps(address, row):
        return [struct.unpack_from("<iih", field, address - field_base + row * 36 + phase * 12) for phase in range(3)]

    def effect(layout, name, entry, row, level, slide, size):
        converted = models[entry]
        rect = parts[("arc", entry, converted["meshes"][0]["texture"])]
        _, _, w, h = rectangles[rect]
        uv = material_uv(shared[entry], converted["meshes"][0]["material"], w, h)
        sprite = layout.sprite(rect, w, h, uv)
        tl, bl, br, tr = converted["meshes"][0]["poses"][0]
        width = math.hypot(tr[0] - tl[0], tr[1] - tl[1])
        height = math.hypot(bl[0] - tl[0], bl[1] - tl[1])
        animations = []
        keys = effect_keys(steps(level, row), steps(slide, row), steps(0x0207E1CC, 0) if entry == 4 else None)
        for f, (level_value, slide_value, vertical) in enumerate(keys):
            x, y = screen(int(slide_value) >> 12, 0x3520)
            opacity = (int(level_value) >> 12) / 31
            scale_x, scale_y = size
            if entry == 4:
                scale_y = vertical / 4096
            sx = width * PX * scale_x / ((uv[2] - uv[0]) * SCALE)
            sy = height * PY * scale_y / ((uv[3] - uv[1]) * SCALE)
            x += (tl[0] + br[0]) / 2 * PX * scale_x
            y -= (tl[1] + br[1]) / 2 * PY * scale_y
            animations.append(layout.animation(sprite, f, f, x / SCALE, y / SCALE, sx, sy, opacity=opacity))
        layout.clip(name, animations)

    for language in languages:
        raws = shared | {f"en{i}": p2_entry(nitro / f"UI/hcnt/arc_{language}", i) for i in (0, 1, 2)}
        local_models = {i: model(raws[f"en{i}"], executable) for i in (0, 1, 2)}
        atlas = Image.new("RGBA", (256, 128))
        rectangles, parts, x, y, row_h = [], {}, 2, 2, 0
        for archive, entry, texture in SOURCES:
            raw = shared[entry] if archive == "arc" else raws[f"en{entry}"]
            images = textures(raw)
            # The German HP/MP sheet has its own texture name; use the model's binding.
            actual_texture = local_models[entry]["meshes"][0]["texture"] if archive != "arc" and entry == 2 else texture
            image = images[actual_texture]
            w, h = image.size
            if x + w + 2 > atlas.width:
                x, y, row_h = 2, y + row_h + 4, 0
            if y + h + 2 > atlas.height:
                raise ValueError("heart atlas overflow")
            parts[(archive, entry, texture)] = len(rectangles)
            parts[(archive, entry, actual_texture)] = len(rectangles)
            paste_texture(atlas, image, x, y)
            rectangles.append((x, y, w, h))
            x += w + 4
            row_h = max(row_h, h)
        layout = Layout(rectangles)

        def clip(name, entry, scale, position, loop=1, static=None, frame_offset=0):
            local = isinstance(entry, str)
            n = int(entry[-1]) if local else entry
            raw = raws[entry]
            converted = local_models[n] if local else models[n]
            mapping = {
                m["texture"]: parts[("arc_{lang}" if local else "arc", n, m["texture"])] for m in converted["meshes"]
            }
            layout.mesh_clip(name, converted, raw, mapping, scale, position, loop, static, frame_offset=frame_offset)

        # RewindWidget(0) sets node flag 2, which holds Sequence_UpdateTracks; (1) clears it.
        clip("heart_idle", 1, 0xD71 / 4096, screen(-0x4718, 0x3520), 0, 0)
        clip("heart_active", 1, 0xD71 / 4096, screen(-0x4718, 0x3520), 0)
        # Pickup updates before drawing; the heart itself draws before updating.
        clip("pickup", 2, 0xCCD / 4096, screen(-0x4718, 0x3520), loop=0, frame_offset=1)
        clip("background", 3, 0x5EC / 4096, screen(-0x3A98, 0x2EE0), 0)
        # Native background polygon alpha is 22/31.
        start = len(layout.rows[3]) - 1
        a = bytearray(layout.rows[3][start])
        struct.pack_into("<2I", a, 136, *([0x5B808080] * 2))
        layout.rows[3][start] = bytes(a)
        effect(
            layout,
            "streak",
            4,
            0,
            0x0207E1A8,
            0x0207E1F0,
            (0xE66 / 4096, 0),
        )
        for i in range(3):
            scale = (0x5EC if i == 0 else 0x75C) / 4096
            effect(
                layout,
                f"particles_{i}",
                5,
                i,
                0x0207E214,
                0x0207E2EC,
                (scale, scale),
            )
        clip("chain_intro", "en1", 0x614 / 4096, screen(-0x3E80, 0x2A94))
        clip("chain_loop", "en1", 0x614 / 4096, screen(-0x3E80, 0x2A94), 0)
        # Warning flashes use the backdrop fade; final removal has no extra native fade.
        clip("chain_outro", "en1", 0x614 / 4096, screen(-0x3E80, 0x2A94))
        template = layout.rows[3].pop()
        layout.rows[4].pop()
        layout.names.pop()
        keys = []
        for frame in range(10):
            a = bytearray(template)
            struct.pack_into("<2i", a, 8, frame, frame)
            elapsed = min(300, frame * 1000 // 30)
            level = (0x1F000 - int(0x1F000 * elapsed / 300)) >> 12
            colour = 0x808080 | round(level * 128 / 31) << 24
            struct.pack_into("<2I", a, 136, colour, colour)
            keys.append(a)
        warning_keys = keys
        layout.dummy("chain_outro", 1, alpha0=0, alpha1=0)
        clip("bonus", "en0", 0x614 / 4096, screen(-0x5DC, 0x1770))
        template = layout.rows[3].pop()
        layout.rows[4].pop()
        layout.names.pop()
        bonus_frames = []
        for frame in range(31):
            a = bytearray(template)
            size = (0x5EC - (0x5EC - 0x2E1) * max(0, 1 - frame / 6) ** 2) / 0x614
            struct.pack_into("<2i", a, 8, frame, frame)
            for at in (80, 88):
                original = struct.unpack_from("<f", template, at)[0]
                struct.pack_into("<2f", a, at, original * size, original * size)
            colour = 0x808080 | round(128 * int(31 * (1 - (frame / 30) ** 2)) / 31) << 24
            struct.pack_into("<2I", a, 136, colour, colour)
            bonus_frames.append(a)
        layout.clip("bonus", bonus_frames)
        for i in range(10):
            mapping = {models[0]["meshes"][0]["texture"]: parts[("arc", 0, str(i))]}
            layout.mesh_clip(f"digit_{i}", models[0], shared[0], mapping, 0x5EC / 4096, (0, 0), 0)
        for i, texture in enumerate([*(f"s_{n}" for n in range(10)), "s_m", "s_p"]):
            mapping = {models[6]["meshes"][0]["texture"]: parts[("arc", 6, texture)]}
            layout.mesh_clip(
                f"small_digit_{i}" if i < 10 else "minus" if i == 10 else "plus",
                models[6],
                shared[6],
                mapping,
                0x5EC / 4096,
                (0, 0),
                0,
            )
        layout.gain("gain")
        layout.gain("bonus_gain", bonus=True)
        for name, peak in (
            ("total_gain", 2253),
            ("total_loss", 1516),
            ("total_gain_medium", 2376),
            ("total_gain_large", 2499),
        ):
            if name == "total_loss":
                layout.dummy(name, 1)
            else:
                layout.total_pulse(name, peak)
        layout.heart_growth()
        layout.total_pulse("total_gain_final", 2253, final=True)
        layout.clip("chain_warning", warning_keys)
        if layout.names != CLIPS:
            raise ValueError("heart clip order mismatch")
        before = [len(row) for row in layout.rows]
        layout.optimize()
        print(f"{language}: compact tables {before} -> {[len(row) for row in layout.rows]}")
        folder = output / language
        folder.mkdir(parents=True, exist_ok=True)
        if settings:
            original = settings / language
            config_file = original / "gauge.json"
            previous = (
                json.loads(config_file.read_text(encoding="utf-8")).get("heart_points", {})
                if config_file.exists()
                else {}
            )
            names = {
                "hearts.png",
                "hearts.2dd",
                previous.get("archive", "hearts.2dd"),
                previous.get("texture", "hearts.png"),
            }
            for name in names:
                if Path(name).name != name or name in ("", ".", ".."):
                    raise ValueError("heart counter resources must be local file names")
                if (original / name).is_file() and (original / name).resolve() != (folder / name).resolve():
                    shutil.copy2(original / name, folder / name)
        if not (folder / "hearts.png").exists():
            atlas.save(folder / "hearts.png")
        if not (folder / "hearts.2dd").exists():
            (folder / "hearts.2dd").write_bytes(layout.archive(atlas))
        (folder / "hearts-reference.json").write_text(
            json.dumps(
                {
                    "animations": dict(zip(CLIPS, range(len(CLIPS)), strict=True)),
                    "animation_sprite_groups": {
                        name: sorted({u32(a, 4) for a in layout.rows[3][u16(clip, 0) : u16(clip, 0) + u16(clip, 2)]})
                        for name, clip in zip(CLIPS, layout.rows[4], strict=True)
                    },
                    "sprites": {name: list(r) for name, r in zip(NAMES, rectangles, strict=True)},
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        config_path = folder / "gauge.json"
        config = json.loads(config_path.read_text(encoding="utf-8")) if config_path.exists() else {}
        if "heart_points" not in config:
            config["heart_points"] = dict(
                enabled=True,
                archive="hearts.2dd",
                texture="hearts.png",
                entry="hart",
                scale=0.65,
                offset_x=0,
                offset_y=0,
                source_origin_x=0,
                source_origin_y=0,
                frames_per_tick=1,
                total_x=screen(-0x4394, 0x348A)[0],
                total_y=screen(-0x4394, 0x348A)[1],
                digit_spacing=0x6A4 / 4096 * PX,
                rows_x=screen(-0x1F40, 0x2D50)[0],
                rows_y=screen(-0x1F40, 0x2D50)[1],
                row_spacing=0x44C / 4096 * PX,
                animations=dict(zip(CLIPS, range(len(CLIPS)), strict=True)),
            )
            config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        print(f"{language}: heart counter: {len(CLIPS)} editable clips, {len(rectangles)} source sprites")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data", type=Path, required=True)
    p.add_argument("--output", type=Path)
    p.add_argument("--mod", default="HD PACK")
    p.add_argument("--lang", nargs="+", choices=LANGUAGES, default=list(LANGUAGES))
    p.add_argument("--native", type=Path)
    p.add_argument("--settings", type=Path)
    a = p.parse_args()
    make(
        a.data,
        a.output or Path(__file__).resolve().parents[2] / "mods" / a.mod / "layouts/gauge",
        a.lang,
        a.native,
        a.settings,
    )


if __name__ == "__main__":
    main()
