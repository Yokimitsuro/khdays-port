"""The DS's 3D models as the save page's pictures draw them (campsave_worlds.py, campsave_titles.py): a KAPH
package's sections, a BMD0's models and their geometry in a pose (every vertex of every shape placed by the model's
render commands: its nodes' matrices, its envelopes and its posScale), and a BCA0's joint animations with their
scale tracks, a node's local matrix sampled at a frame.

Matrices are the NNS layout (tools/mods/nitro3d.py's): row vectors (v @ M), a node's local before its parent's.
Only numpy: the installer's Python has no more.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # tools/mods: nitro3d.py
from nitro3d import basis, fx, parse_dict, pivot  # noqa: E402

# The geometry commands' parameter words (a display list packs four commands, then their parameters)
PARAMS = {0x00: 0, 0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 0, 0x16: 16, 0x17: 12, 0x18: 16,
          0x19: 12, 0x1a: 9, 0x1b: 3, 0x1c: 3, 0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2, 0x24: 1, 0x25: 1,
          0x26: 1, 0x27: 1, 0x28: 1, 0x29: 1, 0x2a: 1, 0x2b: 1, 0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1,
          0x34: 32, 0x40: 1, 0x41: 0, 0x50: 1, 0x60: 1, 0x70: 3, 0x71: 2, 0x72: 1}  # fmt: skip


def kaph_sections(blob: bytes) -> list[list[int]]:
    """A KAPH package's eight sections (their words from +8, negative for none), each its members' offsets:
    section 0 holds the BCA0s, section 7 the BMD0."""
    out = []
    for s in range(8):
        at = struct.unpack_from("<i", blob, 8 + 4 * s)[0]
        if at < 0:
            out.append([])
            continue
        count = struct.unpack_from("<I", blob, at)[0]
        out.append([struct.unpack_from("<I", blob, at + 4 + 4 * i)[0] for i in range(count)])
    return out


def tex0_offset(bmd: bytes) -> int | None:
    """The BMD0's texture block (TEX0), if it has one."""
    blocks = struct.unpack_from("<H", bmd, 14)[0]
    found = [o for o in struct.unpack_from(f"<{blocks}I", bmd, 16) if bmd[o : o + 4] == b"TEX0"]
    return found[0] if found else None


def bmd_models(bmd: bytes) -> list[tuple[str, int]]:
    """A BMD0's models: (name, the model's offset)."""
    assert bmd[:4] == b"BMD0", bmd[:4]
    blocks = struct.unpack_from("<H", bmd, 14)[0]
    out = []
    for o in struct.unpack_from(f"<{blocks}I", bmd, 16):
        if bmd[o : o + 4] != b"MDL0":
            continue
        for name, at in parse_dict(bmd, o + 8):
            out.append((name, o + at))
    return out


# ---- Geometry -------------------------------------------------------------------------------------------------


def s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def signed(v: int, bits: int) -> int:
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def node_matrix(b: bytes, p: int) -> np.ndarray:
    """A node's rest local (scale, rotation, translation) as a 4x4."""
    flags, m00 = struct.unpack_from("<Hh", b, p)
    p += 4
    t = np.zeros(3)
    r = np.eye(3)
    s = np.ones(3)
    if not flags & 1:
        t = np.array(struct.unpack_from("<3i", b, p)) / 4096
        p += 12
    if not flags & 2:
        if flags & 8:
            a, c = struct.unpack_from("<2h", b, p)
            p += 4
            r = pivot((flags >> 4) & 0xF | (((flags >> 8) & 0xF) << 4), a / 4096, c / 4096)
        else:
            v = struct.unpack_from("<8h", b, p)
            p += 16
            r = np.array([m00] + list(v)).reshape(3, 3) / 4096
    if not flags & 4:
        s = np.array(struct.unpack_from("<3i", b, p)) / 4096
        p += 24
    m = np.eye(4)
    m[:3, :3] = np.diag(s) @ r
    m[3, :3] = t
    return m


def pos_scale(b: bytes, m: int) -> float:
    """The model's posScale (its vertices' unit: the model info's, at +0x14, its word at +8)."""
    return struct.unpack_from("<i", b, m + 0x14 + 8)[0] / 4096


def shapes(b: bytes, m: int) -> list[tuple[str, int, int]]:
    """The model's shapes: (name, display list's offset, its size)."""
    so = m + struct.unpack_from("<I", b, m + 0xC)[0]
    out = []
    for name, off in parse_dict(b, so):
        sh = so + off
        _, _, _, ofs_dl, size_dl = struct.unpack_from("<HHIII", b, sh)
        out.append((name, sh + ofs_dl, size_dl))
    return out


def run_dl(b, start, size, stack, scale, tris, start_mtx, uvs, cols):
    """A display list's vertices (each by the stack matrix restored last), its triangles (indices into its own
    vertices) into `tris`, each vertex's texture coordinates and colour (-1: none) into `uvs` and `cols`."""
    pts = []
    p, end = start, start + size
    cur = start_mtx
    v = np.zeros(3)
    uv = (0.0, 0.0)
    col = -1
    prim = None
    strip = []

    def emit(vv):
        w = np.append(vv * scale, 1.0) @ cur
        pts.append(w[:3])
        uvs.append(uv)
        cols.append(col)
        strip.append(len(pts) - 1)
        if prim == 0 and len(strip) == 3:
            tris.append(tuple(strip))
            strip.clear()
        elif prim == 1 and len(strip) == 4:
            tris.append((strip[0], strip[1], strip[2]))
            tris.append((strip[0], strip[2], strip[3]))
            strip.clear()
        elif prim == 2 and len(strip) >= 3:
            tris.append((strip[-3], strip[-2], strip[-1]))
        elif prim == 3 and len(strip) >= 4 and len(strip) % 2 == 0:
            a, bb, c, d = strip[-4:]
            tris.append((a, bb, d))
            tris.append((a, d, c))

    while p + 4 <= end:
        commands = b[p : p + 4]
        p += 4
        for c in commands:
            n = PARAMS.get(c, 0)
            par = struct.unpack_from("<%dI" % n, b, p) if n else ()
            p += 4 * n
            if c == 0x14:  # MTX_RESTORE
                cur = stack.get(par[0] & 0x1F, np.eye(4))
            elif c == 0x20:  # COLOR
                col = par[0] & 0x7FFF
            elif c == 0x22:  # TEXCOORD
                uv = (s16(par[0] & 0xFFFF) / 16, s16(par[0] >> 16) / 16)
            elif c == 0x40:  # BEGIN_VTXS
                prim = par[0] & 3
                strip = []
            elif c == 0x23:  # VTX_16
                v = np.array([s16(par[0] & 0xFFFF), s16(par[0] >> 16), s16(par[1] & 0xFFFF)]) / 4096
                emit(v)
            elif c == 0x24:  # VTX_10
                v = np.array([signed(par[0] & 0x3FF, 10), signed((par[0] >> 10) & 0x3FF, 10),
                              signed((par[0] >> 20) & 0x3FF, 10)]) / 64  # fmt: skip
                emit(v)
            elif c == 0x25:  # VTX_XY
                v = np.array([s16(par[0] & 0xFFFF) / 4096, s16(par[0] >> 16) / 4096, v[2]])
                emit(v)
            elif c == 0x26:  # VTX_XZ
                v = np.array([s16(par[0] & 0xFFFF) / 4096, v[1], s16(par[0] >> 16) / 4096])
                emit(v)
            elif c == 0x27:  # VTX_YZ
                v = np.array([v[0], s16(par[0] & 0xFFFF) / 4096, s16(par[0] >> 16) / 4096])
                emit(v)
            elif c == 0x28:  # VTX_DIFF
                d = np.array([signed(par[0] & 0x3FF, 10), signed((par[0] >> 10) & 0x3FF, 10),
                              signed((par[0] >> 20) & 0x3FF, 10)])  # fmt: skip
                v = v + d / 4096 / 8
                emit(v)
    return pts


def inverse_binds(b: bytes, m: int, n: int) -> list[np.ndarray]:
    """The nodes' inverse bind matrices (the envelopes')."""
    o = m + struct.unpack_from("<I", b, m + 0x10)[0]
    out = []
    for i in range(n):
        mx = np.eye(4)
        if o + 0x54 * i + 48 <= len(b):
            v = np.array(struct.unpack_from("<12i", b, o + 0x54 * i)) / 4096
            mx[:4, :3] = v.reshape(4, 3)
        out.append(mx)
    return out


def model_geometry(b: bytes, m: int, pose=None):
    """(points N x 3, triangles, info) of the model at `m` (an MDL0 model): info's 'uvs' (N x 2), 'cols' (N, -1
    for none) and 'tri_mat' (each triangle's material). `pose(node, rest local)` gives a node's local matrix (its
    animation's), else the rest pose. The SBC's NODE commands hide or show what follows."""
    scale = pos_scale(b, m)
    nd = m + 0x40
    local = [node_matrix(b, nd + off) for _, off in parse_dict(b, nd)]
    if pose is not None:
        local = [pose(i, mx) for i, mx in enumerate(local)]
    binds = inverse_binds(b, m, len(local))
    sbc = m + struct.unpack_from("<I", b, m + 4)[0]
    mat_o = m + struct.unpack_from("<I", b, m + 8)[0]
    shp = shapes(b, m)
    stack = {}
    cur = np.eye(4)
    pts, tris = [], []
    uvs, tri_mat, cols = [], [], []
    mat = -1
    visible = 1
    p = sbc
    while p < mat_o:
        c = b[p]
        op = c & 0x1F
        var = c >> 5
        if op == 0x00:  # NOP
            p += 1
        elif op == 0x01:  # RET
            break
        elif op == 0x02:  # NODE: visibility
            visible = b[p + 2] & 1
            p += 3
        elif op == 0x03:  # MTX: a stack matrix
            cur = stack.get(b[p + 1], np.eye(4))
            p += 2
        elif op == 0x04:  # MAT
            mat = b[p + 1]
            p += 2
        elif op == 0x05:  # SHP
            sid = b[p + 1]
            if sid < len(shp) and visible:
                _, dl, size = shp[sid]
                base = len(pts)
                t = []
                pts += run_dl(b, dl, size, stack, scale, t, cur, uvs, cols)
                tris += [(a + base, bb + base, cc + base) for a, bb, cc in t]
                tri_mat += [mat] * len(t)
            p += 2
        elif op == 0x06:  # NODEDESC: a node's world, from a stack matrix, stored
            node = b[p + 1]
            q = p + 4
            dst = src = None
            if var in (1, 3):
                dst = b[q]
                q += 1
            if var in (2, 3):
                src = b[q]
                q += 1
            if src is not None:
                cur = stack.get(src, np.eye(4))
            if node < len(local):
                cur = local[node] @ cur
            if dst is not None:
                stack[dst] = cur
            p = q
        elif op == 0x07 or op == 0x08:  # BB, BBY
            p += 2 + (1 if var in (1, 2) else 2 if var == 3 else 0)
        elif op == 0x09:  # NODEMIX: an envelope
            dst, count = b[p + 1], b[p + 2]
            mx = np.zeros((4, 4))
            for k in range(count):
                src, node, w = b[p + 3 + 3 * k], b[p + 4 + 3 * k], b[p + 5 + 3 * k]
                mx += (binds[node] @ stack.get(src, np.eye(4))) * (w / 256)
            stack[dst] = mx
            p += 3 + 3 * count
        elif op == 0x0A:  # CALLDL
            p += 9
        elif op == 0x0B:  # POSSCALE
            p += 1
        elif op in (0x0C, 0x0D):  # ENVMAP, PRJMAP
            p += 3
        else:
            p += 1
    info = {"uvs": np.array(uvs).reshape(-1, 2), "tri_mat": tri_mat, "cols": np.array(cols, int)}
    return np.array(pts).reshape(-1, 3), tris, info


# ---- Joint animations -----------------------------------------------------------------------------------------


class Animation:
    """A BCA0 animation: its name, frame count and nodes (each its flags and tracks) in its file `b`."""

    name: str
    frames: int
    nodes: list[dict]
    b: bytes
    o3: int  # the pivot rotations' table
    o5: int  # the basis rotations' table


def parse_bca(b: bytes, base: int = 0) -> list[Animation]:
    """A BCA0's joint animations: each node's translation, rotation and scale tracks ('c', constant) or ('v',
    info, data's offset)."""
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
            s = None
            if not flags & 0x200 and not flags & 0x400:
                s = []
                for bit in (0x800, 0x1000, 0x2000):
                    if flags & bit:  # (the scale and its inverse: the scale kept)
                        s.append(("c", struct.unpack_from("<i", b, p)[0]))
                        p += 8
                    else:
                        info, at = struct.unpack_from("<II", b, p)
                        s.append(("v", info, a + at))
                        p += 8
            node.update(T=t, R=r, S=s)
            nodes.append(node)
        an = Animation()
        an.name, an.frames, an.nodes, an.b, an.o3, an.o5 = name, frames, nodes, b, a + o3, a + o5
        anims.append(an)
    return anims


def sample(info: int, frame: float, value, lerp):
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


def blend_rotations(a: np.ndarray, b: np.ndarray, t: float) -> np.ndarray:
    """Between two rotations: their blend made orthonormal again (its nearest rotation)."""
    u, _, vt = np.linalg.svd(np.asarray(a) * (1 - t) + np.asarray(b) * t)
    return u @ vt


def translation(b: bytes, part, frame: float) -> float:
    if part[0] == "c":
        return part[1]
    _, info, off = part
    if (info >> 29) & 1:
        value = lambda i: struct.unpack_from("<h", b, off + 2 * i)[0]  # noqa: E731
    else:
        value = lambda i: struct.unpack_from("<i", b, off + 4 * i)[0]  # noqa: E731
    return sample(info, frame, value, lambda a, c, t: a * (1 - t) + c * t)


def scale_track(b: bytes, part, frame: float) -> float:
    """A scale track (each sample the scale and its inverse: the scale)."""
    if part[0] == "c":
        return part[1]
    _, info, off = part
    if (info >> 29) & 1:
        value = lambda i: struct.unpack_from("<h", b, off + 4 * i)[0]  # noqa: E731
    else:
        value = lambda i: struct.unpack_from("<i", b, off + 8 * i)[0]  # noqa: E731
    return sample(info, frame, value, lambda a, c, t: a * (1 - t) + c * t)


def rotation_at(an: Animation, index: int) -> np.ndarray:
    if index & 0x8000:
        info, a, b = struct.unpack_from("<Hhh", an.b, an.o3 + 6 * (index & 0x7FFF))
        return pivot(info, fx(a), fx(b))
    return basis(struct.unpack_from("<5H", an.b, an.o5 + 10 * (index & 0x7FFF)))


def rotation(an: Animation, part, frame: float) -> np.ndarray:
    if part[0] == "c":
        return rotation_at(an, part[1] & 0xFFFF)
    _, info, off = part
    return sample(info, frame, lambda i: rotation_at(an, struct.unpack_from("<H", an.b, off + 2 * i)[0]),
                  blend_rotations)  # fmt: skip


def pose(an: Animation, frame: float):
    """The animation at `frame` as model_geometry's `pose`: node i's local (scale, rotation, translation) from the
    animation's node i, what it leaves alone the rest pose's."""
    nodes = dict(enumerate(an.nodes))

    def local(i: int, rest: np.ndarray) -> np.ndarray:
        node = nodes.get(i)
        if node is None:
            return rest
        flags = node["flags"]
        if flags & 1:
            return np.eye(4)
        rs = rest[:3, :3]
        rest_scale = np.linalg.norm(rs, axis=1)  # (the rest's scale: its rows')
        rest_rotation = rs / np.where(rest_scale > 1e-9, rest_scale, 1)[:, None]
        if flags & 0x40:
            r = np.eye(3)
        elif flags & 0x80:
            r = rest_rotation
        else:
            r = rotation(an, node["R"], frame)
        if flags & 2:
            t = np.zeros(3)
        elif flags & 4:
            t = rest[3, :3]
        else:
            t = np.array([fx(translation(an.b, c, frame)) for c in node["T"]])
        if flags & 0x200:
            s = np.ones(3)
        elif flags & 0x400:
            s = rest_scale
        else:
            s = np.array([fx(scale_track(an.b, c, frame)) for c in node["S"]])
        mx = np.eye(4)
        mx[:3, :3] = np.diag(s) @ r
        mx[3, :3] = t
        return mx

    return local
