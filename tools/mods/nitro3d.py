"""The DS's 3D files as the mods' tools read them (walk_clips.py, menu_walk.py): the P2 and KAPH archives a
character's files come in, NNS dictionaries, a model's nodes (a BMD0's MDL0: their rest pose and, from its render
commands, their parents) and joint animations (a BCA0's JNT0: each node's tracks, sampled at a frame).

Matrices are the NNS layout: row vectors (v @ M), a node's local transform before its parent's (world = local @
parent's world). Only numpy: the installer's Python has no more.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import decompress_data as dd  # noqa: E402


def fx(v: float) -> float:
    return v / 4096.0


def unpacked(blob: bytes) -> bytes:
    """A file as the game reads it: an LZ10/LZ11 one decompressed."""
    return dd.decompress_nitro(blob)[1] if blob[:1] in (b"\x10", b"\x11") else blob


def read(nitrofs: Path, path: str) -> bytes:
    data = (nitrofs / path).read_bytes()
    return unpacked(data) if path.endswith(".z") else data


def p2_entries(blob: bytes) -> list[bytes | None]:
    """A P2 archive's entries (rom.h khdays_rom_file_data's, tools/hd/ds_scripts.py's), each decompressed; None
    for an empty one. Its entries start at the header's +0xC, at 0x200-byte sectors; their sizes follow the
    sectors' halfwords, 4-aligned (the top bit: LZ-compressed)."""
    count = struct.unpack_from("<H", blob, 2)[0] & 0x7FFF
    start = struct.unpack_from("<I", blob, 0xC)[0]
    sectors = struct.unpack_from(f"<{count}H", blob, 0x10)
    sizes = struct.unpack_from(f"<{count}I", blob, 0x10 + (count + 1) // 2 * 4)
    out: list[bytes | None] = []
    for i in range(count):
        at, size = sectors[i] * 0x200 + start, sizes[i] & 0x7FFFFFFF
        if size == 0:
            out.append(None)
        elif sizes[i] & 0x80000000:
            out.append(dd.decompress_nitro(blob[at : at + size])[1])
        else:
            out.append(blob[at : at + size])
    return out


def kaph_blocks(blob: bytes | None) -> list[tuple[int, int, bytes]]:
    """A KAPH package's blocks: (offset, size, magic)."""
    if blob is None or blob[:4] != b"KAPH":
        return []
    count = struct.unpack_from("<I", blob, 0x28)[0]
    out = []
    for i in range(count):
        at, size = struct.unpack_from("<II", blob, 0x2C + 8 * i)
        out.append((at, size, blob[at : at + 4]))
    return out


def parse_dict(b: bytes, o: int) -> list[tuple[str, int]]:
    """An NNS dictionary at `o`: (name, its entry's first word) each."""
    count = b[o + 1]
    data = o + 8 + 4 * (count + 1)
    entry_size = struct.unpack_from("<H", b, data)[0]
    values = [struct.unpack_from("<I", b, data + 4 + i * entry_size)[0] for i in range(count)]
    names_at = data + 4 + count * entry_size
    names = [b[names_at + 16 * i : names_at + 16 * i + 16].split(b"\0")[0].decode("latin1") for i in range(count)]
    return list(zip(names, values))


def pivot(info: int, a: float, b: float) -> np.ndarray:
    """A rotation given as a pivot: one axis's +-1, the other four entries from A and B."""
    sel, neg = info & 0xF, (info >> 4) & 0xF
    one = -1.0 if neg & 1 else 1.0
    c = -b if neg & 2 else b
    d = -a if neg & 4 else a
    rows = {
        0: [one, 0, 0, 0, a, b, 0, c, d],
        1: [0, one, 0, a, 0, b, c, 0, d],
        2: [0, 0, one, a, b, 0, c, d, 0],
        3: [0, a, b, one, 0, 0, 0, c, d],
        4: [a, 0, b, 0, one, 0, c, 0, d],
        5: [a, b, 0, 0, 0, one, c, d, 0],
        6: [0, a, b, 0, c, d, one, 0, 0],
        7: [a, 0, b, c, 0, d, 0, one, 0],
        8: [a, b, 0, c, d, 0, 0, 0, one],
    }[sel]
    return np.array(rows, float).reshape(3, 3)


def basis(words: tuple[int, ...]) -> np.ndarray:
    """A rotation given as its first two rows in 13 bits each (five halfwords; the second row's z spread over
    their lowest three bits), the third their cross product."""
    s = [struct.unpack("<h", struct.pack("<H", x))[0] for x in words]
    r1 = np.array([fx(s[0] >> 3), fx(s[1] >> 3), fx(s[2] >> 3)])
    z = (((s[4] & 7) << 12) | ((s[0] & 7) << 9) | ((s[1] & 7) << 6) | ((s[2] & 7) << 3) | (s[3] & 7)) & 0x1FFF
    if z & 0x1000:
        z -= 0x2000
    r2 = np.array([fx(s[3] >> 3), fx(s[4] >> 3), fx(z)])
    return np.array([r1, r2, np.cross(r1, r2)])


# ---- Rotations: matrices and quaternions -----------------------------------------------------------------------


def quat_of(m: np.ndarray) -> np.ndarray:
    """The unit quaternion (x, y, z, w) of a rotation matrix in the column-vector convention, from its largest
    diagonal term (Shepperd's way); a matrix not quite orthogonal gives the quaternion nearest it."""
    m = np.asarray(m, float)
    decision = [m[0, 0], m[1, 1], m[2, 2], m[0, 0] + m[1, 1] + m[2, 2]]
    choice = int(np.argmax(decision))
    q = np.empty(4)
    if choice != 3:
        i, j, k = choice, (choice + 1) % 3, (choice + 2) % 3
        q[i] = 1 - decision[3] + 2 * m[i, i]
        q[j] = m[j, i] + m[i, j]
        q[k] = m[k, i] + m[i, k]
        q[3] = m[k, j] - m[j, k]
    else:
        q[0] = m[2, 1] - m[1, 2]
        q[1] = m[0, 2] - m[2, 0]
        q[2] = m[1, 0] - m[0, 1]
        q[3] = 1 + decision[3]
    return q / np.linalg.norm(q)


def matrix_of(q: np.ndarray) -> np.ndarray:
    """The column-vector rotation matrix of a quaternion (x, y, z, w)."""
    x, y, z, w = np.asarray(q, float) / np.linalg.norm(q)
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
    )


def slerp_matrices(a: np.ndarray, b: np.ndarray, t: float) -> np.ndarray:
    """The rotation `t` of the way from `a` to `b` along the shorter arc."""
    qa, qb = quat_of(a), quat_of(b)
    if np.dot(qa, qb) < 0:
        qb = -qb
    d = float(np.clip(np.dot(qa, qb), -1.0, 1.0))
    angle = np.arccos(d)
    if angle < 1e-9:
        q = qa
    else:
        q = (np.sin((1 - t) * angle) * qa + np.sin(t * angle) * qb) / np.sin(angle)
    return matrix_of(q)


def mean_rotation(matrices: list[np.ndarray]) -> np.ndarray:
    """The average of rotations (column-vector matrices): the quaternion that is the leading eigenvector of the
    sum of their quaternions' outer products (the chordal mean)."""
    qs = [quat_of(m) for m in matrices]
    k = sum(np.outer(q, q) for q in qs)
    return matrix_of(np.linalg.eigh(k)[1][:, -1])


# ---- Models -------------------------------------------------------------------------------------------------


def models(b: bytes, base: int = 0) -> list[tuple[str, list[dict], int]]:
    """A BMD0's models: (name, nodes, the model's offset), each node its name and rest local (R 3x3, T)."""
    assert b[base : base + 4] == b"BMD0"
    blocks = struct.unpack_from("<H", b, base + 14)[0]
    out = []
    for o in struct.unpack_from(f"<{blocks}I", b, base + 16):
        block = base + o
        if b[block : block + 4] != b"MDL0":
            continue
        for name, at in parse_dict(b, block + 8):
            m = block + at
            nodes_at = m + 0x40
            nodes = []
            for node_name, node_at in parse_dict(b, nodes_at):
                p = nodes_at + node_at
                flags, m00 = struct.unpack_from("<Hh", b, p)
                p += 4
                t, r = np.zeros(3), np.eye(3)
                if not flags & 1:
                    t = np.array(struct.unpack_from("<3i", b, p)) / 4096
                    p += 12
                if not flags & 2:
                    if flags & 8:
                        a, c = struct.unpack_from("<2h", b, p)
                        r = pivot(((flags >> 4) & 0xF) | (((flags >> 8) & 0xF) << 4), a / 4096, c / 4096)
                    else:
                        r = np.array([m00, *struct.unpack_from("<8h", b, p)]).reshape(3, 3) / 4096
                nodes.append({"name": node_name, "R": r, "T": t})
            out.append((name, nodes, m))
    return out


def node_parents(b: bytes, model: int, count: int) -> list[int]:
    """Each node's parent (-1: a root), from the model's render commands (its SBC's NODEDESC)."""
    sbc = model + struct.unpack_from("<I", b, model + 4)[0]
    end = model + struct.unpack_from("<I", b, model + 8)[0]
    parents = [-1] * count
    extra = {0: 0, 1: 1, 2: 1, 3: 2}  # the variants' extra bytes (a matrix's stack index to store, to load, both)
    p = sbc
    while p < end:
        op, variant = b[p] & 0x1F, b[p] >> 5
        if op == 0x01:  # RET
            break
        if op == 0x06:  # NODEDESC: node, parent, flags
            node, parent = b[p + 1], b[p + 2]
            if node != parent:
                parents[node] = parent
            p += 4 + extra[variant]
        elif op in (0x07, 0x08):  # BB, BBY
            p += 2 + extra[variant]
        elif op == 0x09:  # NODEMIX: a weight list of 3 bytes each
            p += 3 + 3 * b[p + 2]
        elif op == 0x0A:  # CALLDL
            p += 9
        elif op in (0x02, 0x0C, 0x0D):  # NODE, ENVMAP, PRJMAP
            p += 3
        elif op in (0x03, 0x04, 0x05):  # MTX, MAT, SHP
            p += 2
        else:  # NOP, POSSCALE
            p += 1
    return parents


def skeleton(nitrofs: Path, code: str) -> tuple[list[str], list[int], list[np.ndarray]]:
    """A character's field model (ba/ch/<code>/def.p.z, its first model): its nodes' names, parents and rest
    locals (4x4, row vectors)."""
    blob = read(nitrofs, f"ba/ch/{code}/def.p.z")
    _, nodes, model = models(blob, blob.find(b"BMD0"))[0]
    rest = []
    for n in nodes:
        m = np.eye(4)
        m[:3, :3] = n["R"]
        m[3, :3] = n["T"]
        rest.append(m)
    return [n["name"] for n in nodes], node_parents(blob, model, len(nodes)), rest


# ---- Joint animations --------------------------------------------------------------------------------------


class Animation:
    """A BCA0 animation: its name, frame count and nodes (each its flags and tracks) in its file `b`."""

    name: str
    frames: int
    nodes: list[dict]
    b: bytes
    o3: int  # the pivot rotations' table
    o5: int  # the basis rotations' table


def parse_bca(b: bytes, base: int = 0) -> list[Animation]:
    assert b[base : base + 4] == b"BCA0", b[base : base + 4]
    jnt = base + struct.unpack_from("<I", b, base + 16)[0]
    assert b[jnt : jnt + 4] == b"JNT0"
    anims = []
    for name, off in parse_dict(b, jnt + 8):
        a = jnt + off
        assert b[a : a + 4] == b"J\0AC"
        frames, count = struct.unpack_from("<HH", b, a + 4)
        o3, o5 = struct.unpack_from("<II", b, a + 12)
        nodes = []
        for tag in struct.unpack_from(f"<{count}H", b, a + 20):
            p = a + tag
            flags = struct.unpack_from("<I", b, p)[0]
            p += 4
            node: dict = {"flags": flags, "id": flags >> 24}
            if flags & 1:
                nodes.append(node)
                continue
            t = None
            if not flags & 2 and not flags & 4:
                t = []
                for bit in (8, 16, 32):
                    if flags & bit:
                        t.append(("c", struct.unpack_from("<i", b, p)[0]))
                        p += 4
                    else:
                        info, at = struct.unpack_from("<II", b, p)
                        t.append(("v", info, a + at))
                        p += 8
            r = None
            if not flags & 0x40 and not flags & 0x80:
                if flags & 0x100:
                    r = ("c", struct.unpack_from("<I", b, p)[0])
                    p += 4
                else:
                    info, at = struct.unpack_from("<II", b, p)
                    r = ("v", info, a + at)
                    p += 8
            node.update(T=t, R=r)
            nodes.append(node)
        an = Animation()
        an.name, an.frames, an.nodes, an.b, an.o3, an.o5 = name, frames, nodes, b, a + o3, a + o5
        anims.append(an)
    return anims


def animations(blob: bytes | None) -> list[Animation]:
    """The joint animations of a KAPH package's BCA0 blocks."""
    out = []
    for at, _, magic in kaph_blocks(blob):
        if magic == b"BCA0":
            out += parse_bca(blob, at)
    return out


def _sample(info: int, frame: float, value, lerp):
    """A track at `frame`: its samples every `step` frames up to its last, every frame after it."""
    step, last = 1 << ((info >> 30) & 3), (info >> 16) & 0x1FFF
    if frame >= last and step > 1:
        whole, base = int(frame), last // step
        if whole == frame:
            return value(base + (whole - last))
        return lerp(value(base + (whole - last)), value(base + (whole - last) + 1), frame - whole)
    pos = frame / step
    i0 = int(pos)
    t = pos - i0
    if t < 1e-9:
        return value(i0)
    return lerp(value(i0), value(i0 + 1), t)


def _translation(b: bytes, part, frame: float) -> float:
    if part[0] == "c":
        return part[1]
    _, info, off = part
    if (info >> 29) & 1:
        value = lambda i: struct.unpack_from("<h", b, off + 2 * i)[0]  # noqa: E731
    else:
        value = lambda i: struct.unpack_from("<i", b, off + 4 * i)[0]  # noqa: E731
    return _sample(info, frame, value, lambda a, c, t: a * (1 - t) + c * t)


def _rotation_at(an: Animation, index: int) -> np.ndarray:
    if index & 0x8000:
        info, a, b = struct.unpack_from("<Hhh", an.b, an.o3 + 6 * (index & 0x7FFF))
        return pivot(info, fx(a), fx(b))
    return basis(struct.unpack_from("<5H", an.b, an.o5 + 10 * (index & 0x7FFF)))


def _rotation(an: Animation, part, frame: float) -> np.ndarray:
    if part[0] == "c":
        return _rotation_at(an, part[1] & 0xFFFF)
    _, info, off = part

    def value(i):
        return _rotation_at(an, struct.unpack_from("<H", an.b, off + 2 * i)[0])

    # (row-vector matrices slerped as they are: the path of their transposes is the same one, transposed)
    return _sample(info, frame, value, slerp_matrices)


def node_local(an: Animation, node: dict, frame: float):
    """A node's local at `frame`: (R, T), either None where the animation keeps the model's rest one."""
    flags = node["flags"]
    if flags & 1:
        return np.eye(3), np.zeros(3)
    if flags & 0x40:
        r = np.eye(3)
    elif flags & 0x80:
        r = None
    else:
        r = _rotation(an, node["R"], frame)
    if flags & 2:
        t = np.zeros(3)
    elif flags & 4:
        t = None
    else:
        t = np.array([fx(_translation(an.b, part, frame)) for part in node["T"]])
    return r, t
