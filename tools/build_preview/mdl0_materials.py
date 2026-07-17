#!/usr/bin/env python3
"""Recover the mesh -> texture binding of a BMD0/NSBMD model.

Why this exists
---------------
``build_preview.py`` decodes nothing itself: the CLI (``khdays-port.exe``) does
it. The one thing the CLI does not hand over is *which texture belongs to which
mesh*. ``--export-obj`` writes ``o``/``v``/``vt``/``f`` only - it drops the
``NeutralMesh::texture_name`` the decoder already resolved (see
``src/assets/mesh.cpp``, ``to_wavefront_obj``), and Wavefront ``usemtl`` never
reaches the file. ``--model-info`` prints materials and meshes as two separate
declaration-ordered lists, which cannot be zipped: the binding lives in the
render command stream, not in the ordering.

Nor can the binding be guessed from the OBJ:

* material names are not texture names (``robe00a`` and ``robe00b`` both bind
  the ``ax_robe00`` texture), so name matching is unreliable;
* UVs are in texels, so a mesh's UV extent hints at its texture size, but sizes
  repeat (three 16x32 textures in one model) and wrapped UVs overshoot the
  texture entirely.

So this module reads the binding directly, mirroring ``read_material_textures``
and the ``BindMaterial``/``Draw`` opcodes of ``parse_render_commands`` in
``src/assets/mesh.cpp``. It is a deliberate, narrow duplication of the C++: it
walks only enough of the SBC to track material binds and draws, and skips all
matrix/skinning work. If the MDL0 reader in ``src/`` changes, this must follow.

The returned draw order is the order ``to_wavefront_obj`` emits its ``o``
groups, so the result lines up 1:1 with the OBJ - which is also how the caller
verifies this module did not drift (compare the names).
"""

from __future__ import annotations

import struct
from pathlib import Path

# Parameter length in bytes of each fixed-size SBC command (0x09 is variable).
# Mirrors sbc_param_bytes() in src/assets/mesh.cpp.
_SBC_PARAM_BYTES = {
    0x00: 0, 0x01: 0, 0x0B: 0, 0x2B: 0, 0x40: 0, 0x80: 0,
    0x03: 1, 0x04: 1, 0x05: 1, 0x07: 1, 0x08: 1, 0x24: 1, 0x44: 1,
    0x02: 2, 0x0C: 2, 0x0D: 2, 0x47: 2, 0x48: 2,
    0x06: 3,
    0x26: 4, 0x46: 4,
    0x66: 5,
}

_BIND_MATERIAL = (0x04, 0x24, 0x44)
_DRAW = 0x05


class Mdl0Error(RuntimeError):
    """The file is not a usable BMD0/MDL0, or is malformed."""


def _u16(data: bytes, off: int) -> int:
    if off + 2 > len(data):
        raise Mdl0Error(f"u16 read past end at {off}")
    return struct.unpack_from("<H", data, off)[0]


def _u32(data: bytes, off: int) -> int:
    if off + 4 > len(data):
        raise Mdl0Error(f"u32 read past end at {off}")
    return struct.unpack_from("<I", data, off)[0]


def _byte(data: bytes, off: int) -> int:
    if off >= len(data):
        raise Mdl0Error(f"byte read past end at {off}")
    return data[off]


def _parse_dictionary(data: bytes, off: int) -> tuple[list[str], list[bytes]]:
    """Nitro dictionary: names plus one raw entry each.

    Mirrors parse_dictionary() in src/assets/mesh.cpp.
    """
    if off + 8 > len(data):
        raise Mdl0Error("dictionary offset is invalid")
    count = _byte(data, off + 1)
    if count == 0:
        return [], []
    size = _u16(data, off + 2)
    entry_header = off + _u16(data, off + 6)
    if size < 8 or off + size > len(data):
        raise Mdl0Error("dictionary exceeds its section")
    end = off + size
    if entry_header + 4 > end:
        raise Mdl0Error("entry header exceeds dictionary")

    unit = _u16(data, entry_header)
    names_rel = _u16(data, entry_header + 2)
    if unit == 0:
        raise Mdl0Error("dictionary entry size is zero")

    entries_start = entry_header + 4
    names_start = entry_header + names_rel
    if (entries_start + count * unit > end) or (names_start + count * 16 > end):
        raise Mdl0Error("dictionary entries exceed dictionary")

    names, entries = [], []
    for i in range(count):
        entries.append(data[entries_start + i * unit:entries_start + (i + 1) * unit])
        raw = data[names_start + i * 16:names_start + i * 16 + 16]
        names.append(raw.split(b"\x00", 1)[0].decode("ascii", "replace"))
    return names, entries


def _mdl0_section(data: bytes) -> bytes:
    """Pull the MDL0 section out of a BMD0 container."""
    if len(data) < 0x10 or data[:4] != b"BMD0":
        raise Mdl0Error("not a BMD0/NSBMD file")
    count = _u16(data, 0x0E)
    if count == 0 or count > 64:
        raise Mdl0Error("invalid Nitro section count")
    for i in range(count):
        off = _u32(data, 0x10 + i * 4)
        if off + 8 > len(data):
            raise Mdl0Error("section offset exceeds file")
        if data[off:off + 4] == b"MDL0":
            size = _u32(data, off + 4)
            if size < 8 or off + size > len(data):
                raise Mdl0Error("section size exceeds file")
            return data[off:off + size]
    raise Mdl0Error("BMD0 contains no MDL0 section")


def _material_textures(mdl0: bytes, materials_off: int, count: int) -> list[str]:
    """Texture name bound to each material id ("" when none).

    Mirrors read_material_textures() in src/assets/mesh.cpp: the pairing is an
    info block keyed by *texture* name, whose entries list the material ids
    using that texture - i.e. the reverse of what we want.
    """
    textures = [""] * count
    pairing_off = materials_off + _u16(mdl0, materials_off)
    names, entries = _parse_dictionary(mdl0, pairing_off)
    for name, entry in zip(names, entries):
        if len(entry) < 3:
            continue
        list_off = materials_off + entry[0] + (entry[1] << 8)
        for j in range(entry[2]):
            if list_off + j >= len(mdl0):
                break
            mat_id = mdl0[list_off + j]
            if mat_id < count:
                textures[mat_id] = name
    return textures


def _draw_order(mdl0: bytes, render_off: int, limit: int) -> list[tuple[int, int]]:
    """Walk the SBC, returning (piece_index, material_index) per draw.

    Only BindMaterial/Draw are interpreted; every other opcode is skipped by
    length. Mirrors parse_render_commands() in src/assets/mesh.cpp.
    """
    draws: list[tuple[int, int]] = []
    cursor = render_off
    end = render_off + limit
    material = -1

    while cursor < end:
        opcode = _byte(mdl0, cursor)
        cursor += 1
        if opcode == 0x01:  # end of render commands
            break
        if opcode == 0x09:
            # Variable length: 1 byte + 1 count byte + 3 bytes per term.
            params_len = 1 + 1 + 3 * _byte(mdl0, cursor + 1)
        else:
            if opcode not in _SBC_PARAM_BYTES:
                raise Mdl0Error(f"unknown render command opcode {opcode}")
            params_len = _SBC_PARAM_BYTES[opcode]
        if cursor + params_len > end:
            raise Mdl0Error("render command parameters exceed stream")
        params = cursor
        cursor += params_len

        if opcode in _BIND_MATERIAL:
            material = mdl0[params]
        elif opcode == _DRAW:
            draws.append((mdl0[params], material))
    return draws


def mesh_texture_bindings(path: Path, model_index: int = 0) -> list[tuple[str, str]]:
    """(mesh name, texture name) per draw, in --export-obj's ``o`` group order.

    The texture name is "" when the material binds none. Raises Mdl0Error on
    anything malformed; callers treat that as "no bindings" and fall back to
    untextured rendering.
    """
    mdl0 = _mdl0_section(Path(path).read_bytes())
    if len(mdl0) < 8 or mdl0[:4] != b"MDL0":
        raise Mdl0Error("invalid MDL0 section")

    names, entries = _parse_dictionary(mdl0, 8)
    if model_index >= len(names):
        raise Mdl0Error(f"model index {model_index} out of range ({len(names)})")
    model_off = struct.unpack_from("<I", entries[model_index], 0)[0]
    if model_off + 0x40 > len(mdl0):
        raise Mdl0Error("model header exceeds MDL0")

    render_off = _u32(mdl0, model_off + 0x04)
    materials_off = _u32(mdl0, model_off + 0x08)
    pieces_off = _u32(mdl0, model_off + 0x0C)
    material_count = _byte(mdl0, model_off + 0x18)

    if materials_off < render_off:
        raise Mdl0Error("render command stream has no extent")
    textures = _material_textures(mdl0, model_off + materials_off, material_count)
    piece_names, _ = _parse_dictionary(mdl0, model_off + pieces_off)

    out: list[tuple[str, str]] = []
    for piece, material in _draw_order(
        mdl0, model_off + render_off, materials_off - render_off
    ):
        if piece >= len(piece_names):
            raise Mdl0Error("piece index out of range")
        tex = textures[material] if 0 <= material < len(textures) else ""
        out.append((piece_names[piece], tex))
    return out


if __name__ == "__main__":  # manual spot-check
    import sys

    for mesh, tex in mesh_texture_bindings(Path(sys.argv[1])):
        print(f"{mesh:24s} -> {tex or '(none)'}")
