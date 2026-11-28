"""Read-only decoder for the KAPH/BMD0/MDL0 resources used by Days.

Adapted from khdays-port assets/src/mesh.cpp at 0b65fa4f, under its MIT license.
Format and source notes are in README.md."""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# BMD0 / KAPH containers
# ---------------------------------------------------------------------------


def u8(b: bytes, o: int) -> int:
    return b[o]


def u16(b: bytes, o: int) -> int:
    return struct.unpack_from("<H", b, o)[0]


def u32(b: bytes, o: int) -> int:
    return struct.unpack_from("<I", b, o)[0]


def s16(b: bytes, o: int) -> int:
    return struct.unpack_from("<h", b, o)[0]


def s32(b: bytes, o: int) -> int:
    return struct.unpack_from("<i", b, o)[0]


def kaph_sections(blob: bytes) -> list[tuple[int, int] | None]:
    """A KAPH package's eight section tables: (offset, size) or None."""
    if blob[:4] != b"KAPH":
        raise ValueError("not a KAPH package")
    out: list[tuple[int, int] | None] = []
    for i in range(8):
        table = u32(blob, 8 + 4 * i)
        if table in (0, 0xFFFFFFFF):
            out.append(None)
            continue
        count = u32(blob, table)
        offs = [u32(blob, table + 4 + 4 * k) for k in range(count)]
        sizes = [u32(blob, table + 4 + 4 * count + 4 * k) for k in range(count)]
        out.append((offs[0], sizes[0]) if count == 1 else None)
    return out


def kaph_block(blob: bytes, section: int) -> bytes | None:
    entry = kaph_sections(blob)[section]
    if entry is None:
        return None
    off, size = entry
    return blob[off : off + size]


def bmd0_sections(data: bytes) -> dict[str, tuple[int, int]]:
    """Section magic -> (offset, size) for a BMD0/BTX0 resource."""
    if data[:4] not in (b"BMD0", b"BTX0", b"BCA0"):
        raise ValueError(f"not a Nitro 3D resource: {data[:4]!r}")
    count = u16(data, 0x0E)
    out: dict[str, tuple[int, int]] = {}
    for i in range(count):
        off = u32(data, 0x10 + 4 * i)
        if off + 8 > len(data):
            continue
        size = u32(data, off + 4)
        if size < 8 or off + size > len(data):
            continue
        magic = data[off : off + 4].decode("latin1")
        out.setdefault(magic, (off, size))
    return out


@dataclass
class NitroDict:
    names: list[str]
    entries: list[bytes]
    entry_offsets: list[int]  # absolute offset of each entry in the section


def parse_dict(data: bytes, off: int) -> NitroDict:
    count = data[off + 1]
    if count == 0:
        return NitroDict([], [], [])
    header_rel = u16(data, off + 6)
    header = off + header_rel
    unit = u16(data, header)
    names_rel = u16(data, header + 2)
    entries_start = header + 4
    names_start = header + names_rel
    out = NitroDict([], [], [])
    for i in range(count):
        p = entries_start + i * unit
        out.entries.append(data[p : p + unit])
        out.entry_offsets.append(p)
        np_ = names_start + i * 16
        out.names.append(data[np_ : np_ + 16].split(b"\x00", 1)[0].decode("latin1"))
    return out


# ---------------------------------------------------------------------------
# Node (bone) transforms -- after mesh.cpp's read_object_matrix / pivot_matrix
# ---------------------------------------------------------------------------


def _fix(v: int) -> float:
    return v / 4096.0


def _sign_extend(value: int, bits: int) -> int:
    mask = (1 << bits) - 1
    v = value & mask
    if v & (1 << (bits - 1)):
        return v - (1 << bits)
    return v


def pivot_matrix(select: int, neg: int, a: float, b: float) -> list[float]:
    o = 1.0 if not (neg & 1) else -1.0
    c = b if not (neg & 2) else -b
    d = a if not (neg & 4) else -a
    table = {
        0: [o, 0, 0, 0, a, b, 0, c, d],
        1: [0, o, 0, a, 0, b, c, 0, d],
        2: [0, 0, o, a, b, 0, c, d, 0],
        3: [0, a, b, o, 0, 0, 0, c, d],
        4: [a, 0, b, 0, o, 0, c, 0, d],
        5: [a, b, 0, 0, 0, o, c, d, 0],
        6: [0, a, b, 0, c, d, o, 0, 0],
        7: [a, 0, b, c, 0, d, 0, o, 0],
        8: [a, b, 0, c, d, 0, 0, 0, o],
    }
    r = table.get(select, [-a, 0, 0, 0, 0, 0, 0, 0, 0])
    m = identity()
    for col in range(3):
        for row in range(3):
            m[col * 4 + row] = r[col * 3 + row]
    return m


def identity() -> list[float]:
    return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def mat_mul(a: list[float], b: list[float]) -> list[float]:
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            out[col * 4 + row] = sum(a[k * 4 + row] * b[col * 4 + k] for k in range(4))
    return out


def mat_scale(x: float, y: float, z: float) -> list[float]:
    m = identity()
    m[0], m[5], m[10] = x, y, z
    return m


def read_object_matrix(data: bytes, off: int) -> list[float]:
    flags = u16(data, off)
    no_trans = bool(flags & 0x01)
    no_rot = bool(flags & 0x02)
    no_scale = bool(flags & 0x04)
    has_pivot = bool(flags & 0x08)
    select = (flags >> 4) & 0x0F
    neg = (flags >> 8) & 0x0F
    m0 = u16(data, off + 2)
    cursor = off + 4
    translation = None
    rotation = None
    scale = None
    if not no_trans:
        translation = (_fix(s32(data, cursor)), _fix(s32(data, cursor + 4)), _fix(s32(data, cursor + 8)))
        cursor += 12
    if has_pivot:
        a = _fix(s16(data, cursor))
        b = _fix(s16(data, cursor + 2))
        cursor += 4
        rotation = pivot_matrix(select, neg, a, b)
    elif not no_rot:
        m = [_fix(_sign_extend(m0, 16))] + [_fix(s16(data, cursor + i * 2)) for i in range(8)]
        cursor += 16
        rot = identity()
        for col in range(3):
            for row in range(3):
                rot[col * 4 + row] = m[col * 3 + row]
        rotation = rot
    if not no_scale:
        scale = (_fix(s32(data, cursor)), _fix(s32(data, cursor + 4)), _fix(s32(data, cursor + 8)))
        cursor += 12
    matrix = identity()
    if scale is not None:
        matrix = mat_scale(*scale)
    if rotation is not None:
        matrix = mat_mul(rotation, matrix)
    if translation is not None:
        t = identity()
        t[12], t[13], t[14] = translation
        matrix = mat_mul(t, matrix)
    return matrix


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------


@dataclass
class Material:
    name: str
    texture: str = ""
    palette: str = ""
    alpha: float = 1.0
    poly_attr: int = 0
    tex_image_param: int = 0
    double_sided: bool = False


def read_materials(mdl0: bytes, materials_off: int, count: int) -> list[Material]:
    """NNSG3dResMat: two u16 pairing offsets, then the material dictionary."""
    mats = [Material(name="") for _ in range(count)]
    tex_pair_off = u16(mdl0, materials_off)
    pltt_pair_off = u16(mdl0, materials_off + 2)
    mdict = parse_dict(mdl0, materials_off + 4)
    for i, ent in enumerate(mdict.entries[:count]):
        mats[i].name = mdict.names[i]
        rec = materials_off + u32(ent, 0)
        poly = u32(mdl0, rec + 0x0C)
        mats[i].alpha = ((poly >> 16) & 0x1F) / 31.0
        mats[i].poly_attr = poly
        mats[i].tex_image_param = u32(mdl0, rec + 0x14)
        mats[i].double_sided = (poly & 0xC0) == 0xC0
    for which, attr in ((tex_pair_off, "texture"), (pltt_pair_off, "palette")):
        if which == 0:
            continue
        pairing = parse_dict(mdl0, materials_off + which)
        for i, ent in enumerate(pairing.entries):
            if len(ent) < 3:
                continue
            list_off = materials_off + u16(ent, 0)
            n = ent[2]
            for j in range(n):
                mid = mdl0[list_off + j]
                if mid < len(mats):
                    setattr(mats[mid], attr, pairing.names[i])
    return mats


# ---------------------------------------------------------------------------
# Render command stream (SBC)
# ---------------------------------------------------------------------------
SBC_FIXED = {
    0x00: 0,
    0x01: 0,
    0x0B: 0,
    0x2B: 0,
    0x40: 0,
    0x80: 0,
    0x03: 1,
    0x04: 1,
    0x05: 1,
    0x07: 1,
    0x08: 1,
    0x24: 1,
    0x44: 1,
    0x02: 2,
    0x0C: 2,
    0x0D: 2,
    0x47: 2,
    0x48: 2,
    0x06: 3,
    0x26: 4,
    0x46: 4,
    0x66: 5,
}


@dataclass
class Op:
    kind: str
    index: int = 0
    terms: list[tuple[float, int, int]] = field(default_factory=list)  # (weight, stack, invbind)
    parent: int = 0
    scale: tuple[float, float, float] = (1.0, 1.0, 1.0)


def parse_render_commands(mdl0: bytes, off: int, limit: int) -> list[Op]:
    ops: list[Op] = []
    cursor = off
    end = off + limit
    while cursor < end:
        opcode = mdl0[cursor]
        cursor += 1
        if opcode == 0x01:
            break
        if opcode == 0x09:
            params_len = 2 + 3 * mdl0[cursor + 1]
        else:
            params_len = SBC_FIXED[opcode]
        params = cursor
        cursor += params_len
        if opcode in (0x00, 0x02):
            continue
        if opcode == 0x03:
            ops.append(Op("load", mdl0[params]))
        elif opcode in (0x04, 0x24, 0x44):
            ops.append(Op("material", mdl0[params]))
        elif opcode == 0x05:
            ops.append(Op("draw", mdl0[params]))
        elif opcode in (0x06, 0x26, 0x46, 0x66):
            store = load = None
            if opcode == 0x26:
                store = mdl0[params + 3]
            elif opcode == 0x46:
                load = mdl0[params + 3]
            elif opcode == 0x66:
                store = mdl0[params + 3]
                load = mdl0[params + 4]
            if load is not None:
                ops.append(Op("load", load))
            ops.append(Op("object", mdl0[params], parent=mdl0[params + 1]))
            if store is not None:
                ops.append(Op("store", store))
        elif opcode == 0x09:
            store = mdl0[params]
            n = mdl0[params + 1]
            terms = []
            for i in range(n):
                base = params + 2 + i * 3
                terms.append((mdl0[base + 2] / 256.0, mdl0[base], mdl0[base + 1]))
            ops.append(Op("skin", store, terms=terms))
            ops.append(Op("store", store))
        elif opcode == 0x0B:
            ops.append(Op("scale_up"))
        elif opcode == 0x2B:
            ops.append(Op("scale_down"))
    return ops


# ---------------------------------------------------------------------------
# GPU command stream -> triangles
# ---------------------------------------------------------------------------
GPU_WORDS = {
    0x00: 0,
    0x11: 0,
    0x15: 0,
    0x41: 0,
    0x10: 1,
    0x12: 1,
    0x13: 1,
    0x14: 1,
    0x20: 1,
    0x21: 1,
    0x22: 1,
    0x24: 1,
    0x25: 1,
    0x26: 1,
    0x27: 1,
    0x28: 1,
    0x29: 1,
    0x2A: 1,
    0x2B: 1,
    0x30: 1,
    0x31: 1,
    0x32: 1,
    0x33: 1,
    0x40: 1,
    0x50: 1,
    0x60: 1,
    0x72: 1,
    0x23: 2,
    0x71: 2,
    0x1B: 3,
    0x1C: 3,
    0x70: 3,
    0x1A: 9,
    0x17: 12,
    0x19: 12,
    0x16: 16,
    0x18: 16,
    0x34: 32,
}


@dataclass
class Vertex:
    position: tuple[float, float, float]
    texcoord: tuple[float, float]
    color: tuple[int, int, int, int]
    normal: tuple[float, float, float]
    matrix: int


@dataclass
class Mesh:
    name: str
    material: str
    texture: str
    palette: str
    alpha: float
    double_sided: bool
    vertices: list[Vertex] = field(default_factory=list)
    indices: list[int] = field(default_factory=list)


def _flush(mode: int, verts: list[int], out: list[int]) -> None:
    n = len(verts)
    if mode == 0:  # triangle list
        for i in range(0, n - 2, 3):
            out += [verts[i], verts[i + 1], verts[i + 2]]
    elif mode == 1:  # quad list
        for i in range(0, n - 3, 4):
            out += [verts[i], verts[i + 1], verts[i + 2], verts[i], verts[i + 2], verts[i + 3]]
    elif mode == 2:  # triangle strip
        for i in range(2, n):
            if i % 2 == 0:
                out += [verts[i - 2], verts[i - 1], verts[i]]
            else:
                out += [verts[i - 1], verts[i - 2], verts[i]]
    elif mode == 3:  # quad strip
        for i in range(0, n - 3, 2):
            out += [verts[i], verts[i + 1], verts[i + 3], verts[i], verts[i + 3], verts[i + 2]]


class MatrixState:
    """The matrix stack and palette a model's render stream and pieces share
    (mesh.cpp's Builder: stack[32], current, palette)."""

    def __init__(self) -> None:
        self.palette: list[list[float]] = [identity()]
        self.palette_bones: list[list[tuple[int, float]]] = [[]]
        self.stack: list[int | None] = [None] * 32
        self.current = 0

    def fetch(self, slot: int) -> int:
        if self.stack[slot] is None:
            self.stack[slot] = self.current
        return self.stack[slot]

    def add(self, matrix: list[float], bones: list[tuple[int, float]]) -> int:
        self.palette.append(matrix)
        self.palette_bones.append(bones)
        return len(self.palette) - 1


def decode_mesh(mdl0: bytes, off: int, length: int, name: str, material: Material | None, st: "MatrixState") -> Mesh:
    mesh = Mesh(
        name=name,
        material=material.name if material else "",
        texture=material.texture if material else "",
        palette=material.palette if material else "",
        alpha=material.alpha if material else 1.0,
        double_sided=material.double_sided if material else False,
    )
    pos = [0.0, 0.0, 0.0]
    nrm = [0.0, 0.0, 0.0]
    tex = [0.0, 0.0]
    col = [255, 255, 255, 255]
    group: list[int] = []
    mode = -1

    def emit() -> None:
        idx = len(mesh.vertices)
        mesh.vertices.append(Vertex(tuple(pos), tuple(tex), tuple(col), tuple(nrm), st.current))
        group.append(idx)

    def expand(c: int) -> int:
        c &= 0x1F
        return (c << 3) | (c >> 2)

    cursor = off
    end = off + length
    while cursor + 4 <= end:
        word = u32(mdl0, cursor)
        cursor += 4
        for opcode in (word & 0xFF, (word >> 8) & 0xFF, (word >> 16) & 0xFF, word >> 24):
            words = GPU_WORDS[opcode]
            p = [u32(mdl0, cursor + 4 * k) for k in range(words)]
            if opcode == 0x20:
                v = p[0]
                col = [expand(v), expand(v >> 5), expand(v >> 10), 255]
            elif opcode == 0x21:
                v = p[0]
                nrm = [
                    _sign_extend(v & 0x3FF, 10) / 512.0,
                    _sign_extend((v >> 10) & 0x3FF, 10) / 512.0,
                    _sign_extend((v >> 20) & 0x3FF, 10) / 512.0,
                ]
            elif opcode == 0x22:
                v = p[0]
                tex = [_sign_extend(v & 0xFFFF, 16) / 16.0, _sign_extend((v >> 16) & 0xFFFF, 16) / 16.0]
            elif opcode == 0x23:
                pos = [
                    _fix(_sign_extend(p[0] & 0xFFFF, 16)),
                    _fix(_sign_extend((p[0] >> 16) & 0xFFFF, 16)),
                    _fix(_sign_extend(p[1] & 0xFFFF, 16)),
                ]
                emit()
            elif opcode == 0x24:
                v = p[0]
                pos = [_sign_extend(v, 10) / 64.0, _sign_extend(v >> 10, 10) / 64.0, _sign_extend(v >> 20, 10) / 64.0]
                emit()
            elif opcode == 0x25:
                pos = [_fix(_sign_extend(p[0] & 0xFFFF, 16)), _fix(_sign_extend((p[0] >> 16) & 0xFFFF, 16)), pos[2]]
                emit()
            elif opcode == 0x26:
                pos = [_fix(_sign_extend(p[0] & 0xFFFF, 16)), pos[1], _fix(_sign_extend((p[0] >> 16) & 0xFFFF, 16))]
                emit()
            elif opcode == 0x27:
                pos = [pos[0], _fix(_sign_extend(p[0] & 0xFFFF, 16)), _fix(_sign_extend((p[0] >> 16) & 0xFFFF, 16))]
                emit()
            elif opcode == 0x28:
                v = p[0]
                pos = [
                    pos[0] + _sign_extend(v, 10) / 4096.0,
                    pos[1] + _sign_extend(v >> 10, 10) / 4096.0,
                    pos[2] + _sign_extend(v >> 20, 10) / 4096.0,
                ]
                emit()
            elif opcode == 0x14:  # MTX_RESTORE: back to the matrix a stack slot holds
                st.current = st.fetch(p[0] & 0x1F)
            elif opcode == 0x1B:  # MTX_SCALE: the current matrix scaled
                sc = [_fix(s32(mdl0, cursor + 4 * k)) for k in range(3)]
                st.current = st.add(mat_mul(st.palette[st.current], mat_scale(*sc)), st.palette_bones[st.current])
            elif opcode == 0x40:
                if group:
                    _flush(mode, group, mesh.indices)
                group = []
                mode = p[0] & 3
            elif opcode == 0x41:
                if group:
                    _flush(mode, group, mesh.indices)
                group = []
                mode = -1
            cursor += 4 * words
    if group:
        _flush(mode, group, mesh.indices)
    return mesh


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------


@dataclass
class Bone:
    name: str
    parent: int
    matrix: list[float]


@dataclass
class Model:
    name: str
    bones: list[Bone]
    palette: list[list[float]]
    palette_bones: list[list[tuple[int, float]]]
    meshes: list[Mesh]
    materials: list[Material]
    header_vertex_count: int
    model_offset: int = 0
    mdl0_offset: int = 0


def decode_model(bmd0: bytes, model_index: int = 0) -> Model:
    sections = bmd0_sections(bmd0)
    mdl0_off, mdl0_size = sections["MDL0"]
    mdl0 = bmd0[mdl0_off : mdl0_off + mdl0_size]
    mdict = parse_dict(mdl0, 8)
    model_off = u32(mdict.entries[model_index], 0)
    render_off = u32(mdl0, model_off + 0x04)
    materials_off = u32(mdl0, model_off + 0x08)
    pieces_off = u32(mdl0, model_off + 0x0C)
    inv_off = u32(mdl0, model_off + 0x10)
    num_objects = mdl0[model_off + 0x17]
    num_materials = mdl0[model_off + 0x18]
    up_scale = _fix(s32(mdl0, model_off + 0x1C))
    down_scale = _fix(s32(mdl0, model_off + 0x20))
    header_vertex_count = u16(mdl0, model_off + 0x24)

    odict = parse_dict(mdl0, model_off + 0x40)
    bone_matrices = [read_object_matrix(mdl0, model_off + 0x40 + u32(e, 0)) for e in odict.entries]

    inv_binds = []
    cursor = model_off + inv_off
    # Some event-object models have no envelopes at all: their inverse-bind
    # block is absent (the header's offset points at the model's end). mesh.cpp
    # silently reads none there; the count never exceeds what the section holds.
    inv_count = num_objects
    if not inv_off or inv_off >= len(mdl0):
        inv_count = 0
    else:
        inv_count = min(inv_count, (len(mdl0) - cursor) // 84)
    for _ in range(inv_count):
        m = [_fix(s32(mdl0, cursor + 4 * i)) for i in range(12)]
        mat = identity()
        mat[0], mat[1], mat[2] = m[0], m[1], m[2]
        mat[4], mat[5], mat[6] = m[3], m[4], m[5]
        mat[8], mat[9], mat[10] = m[6], m[7], m[8]
        mat[12], mat[13], mat[14] = m[9], m[10], m[11]
        inv_binds.append(mat)
        cursor += 84

    materials = read_materials(mdl0, model_off + materials_off, num_materials)

    piece_dict = parse_dict(mdl0, model_off + pieces_off)
    pieces = []
    for i, ent in enumerate(piece_dict.entries):
        mesh_off = model_off + pieces_off + u32(ent, 0)
        cmd_rel = u32(mdl0, mesh_off + 8)
        cmd_len = u32(mdl0, mesh_off + 12)
        pieces.append((piece_dict.names[i], mesh_off + cmd_rel, cmd_len))

    ops = parse_render_commands(mdl0, model_off + render_off, materials_off - render_off)

    st = MatrixState()
    parents: dict[int, int] = {}
    meshes: list[Mesh] = []

    cur_material = -1
    for op in ops:
        if op.kind == "load":
            st.current = st.fetch(op.index)
        elif op.kind == "store":
            st.stack[op.index] = st.current
        elif op.kind == "object":
            if op.index < len(bone_matrices):
                st.current = st.add(mat_mul(st.palette[st.current], bone_matrices[op.index]), [(op.index, 1.0)])
                parents[op.index] = -1 if op.parent == op.index else op.parent
        elif op.kind == "skin":
            acc = [0.0] * 16
            for weight, slot, ib in op.terms:
                slot_index = st.fetch(slot)
                prod = mat_mul(st.palette[slot_index], inv_binds[ib])
                for k in range(16):
                    acc[k] += prod[k] * weight
            st.current = st.add(acc, [(ib, w) for w, _slot, ib in op.terms])
        elif op.kind in ("scale_up", "scale_down"):
            sc = up_scale if op.kind == "scale_up" else down_scale
            st.current = st.add(mat_mul(st.palette[st.current], mat_scale(sc, sc, sc)), st.palette_bones[st.current])
        elif op.kind == "material":
            cur_material = op.index
        elif op.kind == "draw":
            if op.index < len(pieces):
                name, off, length = pieces[op.index]
                mat = materials[cur_material] if 0 <= cur_material < len(materials) else None
                meshes.append(decode_mesh(mdl0, off, length, name, mat, st))

    bones = [
        Bone(odict.names[i] if i < len(odict.names) else f"bone{i}", parents.get(i, -1), bone_matrices[i])
        for i in range(len(bone_matrices))
    ]
    return Model(
        mdict.names[model_index],
        bones,
        st.palette,
        st.palette_bones,
        meshes,
        materials,
        header_vertex_count,
        model_off,
        mdl0_off,
    )
