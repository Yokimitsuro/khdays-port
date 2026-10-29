"""Writes the made-up test character's files (characters/block/): a figure of three boxes on three bones named as
Roxas's model's (Bip01_Pelvis, Bip01_Spine1, Bip01_Head: its joints follow his by name), its four-colour texture
and its face. Nothing of the game: run it again to remake them."""

import json
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent / "characters" / "block"

# (name, parent, head's height)
BONES = [("Bip01_Pelvis", -1, 0.81), ("Bip01_Spine1", 0, 1.05), ("Bip01_Head", 1, 1.30)]
# (bone, low corner, high corner, texel)
BOXES = [
    (0, (-0.15, 0.00, -0.10), (0.15, 0.80, 0.10), 1),
    (1, (-0.20, 0.80, -0.12), (0.20, 1.25, 0.12), 0),
    (2, (-0.14, 1.25, -0.14), (0.14, 1.55, 0.14), 2),
]
COLOURS = [(230, 120, 30), (40, 70, 200), (240, 200, 160), (20, 20, 20)]  # the texture's four texels


def png(width: int, height: int, rgb: list[tuple[int, int, int]]) -> bytes:
    rows = b"".join(b"\0" + bytes(c for p in rgb[y * width : (y + 1) * width] for c in p) for y in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def box(lo, hi):
    """24 corners (four a face, each face its own normal) and 36 indices."""
    faces = [
        ((0, 1), (1, 0, 0)),
        ((0, 0), (-1, 0, 0)),
        ((1, 1), (0, 1, 0)),
        ((1, 0), (0, -1, 0)),
        ((2, 1), (0, 0, 1)),
        ((2, 0), (0, 0, -1)),
    ]
    positions, normals, indices = [], [], []
    for (axis, side), normal in faces:
        others = [a for a in range(3) if a != axis]
        base = len(positions)
        for u, v in ((0, 0), (1, 0), (1, 1), (0, 1)):
            p = [0.0, 0.0, 0.0]
            p[axis] = hi[axis] if side else lo[axis]
            p[others[0]] = hi[others[0]] if u else lo[others[0]]
            p[others[1]] = hi[others[1]] if v else lo[others[1]]
            positions.append(p)
            normals.append(normal)
        # counter-clockwise seen from outside
        quad = [0, 1, 2, 0, 2, 3]
        flip = (axis == 1) ^ (side == 0)
        indices += [base + i for i in (quad if not flip else [0, 2, 1, 0, 3, 2])]
    return positions, normals, indices


def main() -> None:
    HERE.mkdir(parents=True, exist_ok=True)
    (HERE / "model").mkdir(exist_ok=True)
    positions, normals, uvs, joints, weights, indices = [], [], [], [], [], []
    for bone, lo, hi, texel in BOXES:
        p, n, i = box(lo, hi)
        base = len(positions)
        positions += p
        normals += n
        uvs += [((texel + 0.5) / 4, 0.5)] * len(p)
        joints += [(bone, 0, 0, 0)] * len(p)
        weights += [(1.0, 0.0, 0.0, 0.0)] * len(p)
        indices += [base + k for k in i]
    blob = b""
    views, accessors = [], []

    def add(data: bytes, count: int, kind: str, component: int, target=None, bounds=None):
        nonlocal blob
        while len(blob) % 4:
            blob += b"\0"
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        views.append(view)
        blob += data
        accessor = {"bufferView": len(views) - 1, "componentType": component, "count": count, "type": kind}
        if bounds is not None:
            accessor["min"], accessor["max"] = bounds
        accessors.append(accessor)
        return len(accessors) - 1

    lows = [min(p[k] for p in positions) for k in range(3)]
    highs = [max(p[k] for p in positions) for k in range(3)]
    a_pos = add(b"".join(struct.pack("<3f", *p) for p in positions), len(positions), "VEC3", 5126, 34962, (lows, highs))
    a_nrm = add(b"".join(struct.pack("<3f", *n) for n in normals), len(normals), "VEC3", 5126, 34962)
    a_uv = add(b"".join(struct.pack("<2f", *t) for t in uvs), len(uvs), "VEC2", 5126, 34962)
    a_jnt = add(b"".join(struct.pack("<4B", *j) for j in joints), len(joints), "VEC4", 5121, 34962)
    a_wgt = add(b"".join(struct.pack("<4f", *w) for w in weights), len(weights), "VEC4", 5126, 34962)
    a_idx = add(b"".join(struct.pack("<H", i) for i in indices), len(indices), "SCALAR", 5123, 34963)
    # the inverse bind matrices: each bone's head moved back to the origin (column-major)
    ibm = b""
    for _, _, height in BONES:
        ibm += struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -height, 0, 1)
    a_ibm = add(ibm, len(BONES), "MAT4", 5126)

    nodes = [{"name": "block", "mesh": 0, "skin": 0}]
    for k, (name, parent, height) in enumerate(BONES):
        below = BONES[parent][2] if parent >= 0 else 0.0
        node = {"name": name, "translation": [0.0, height - below, 0.0]}
        children = [1 + c for c, b in enumerate(BONES) if b[1] == k]
        if children:
            node["children"] = children
        nodes.append(node)
    gltf = {
        "asset": {"version": "2.0", "generator": "khdays-port tests: make_files.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": nodes,
        "skins": [{"joints": [1, 2, 3], "inverseBindMatrices": a_ibm, "skeleton": 1}],
        "meshes": [
            {
                "name": "block",
                "primitives": [
                    {
                        "attributes": {
                            "POSITION": a_pos,
                            "NORMAL": a_nrm,
                            "TEXCOORD_0": a_uv,
                            "JOINTS_0": a_jnt,
                            "WEIGHTS_0": a_wgt,
                        },
                        "indices": a_idx,
                        "material": 0,
                    }
                ],
            }
        ],
        "materials": [{"name": "block", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728}],
        "images": [{"uri": "block.png"}],
        "buffers": [{"uri": "block.bin", "byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    (HERE / "model" / "block.gltf").write_text(json.dumps(gltf, indent=1) + "\n", encoding="utf-8", newline="\n")
    (HERE / "model" / "block.bin").write_bytes(blob)
    (HERE / "model" / "block.png").write_bytes(png(4, 1, COLOURS))
    # the face: an orange square with a blue band, 64 x 64
    face = [COLOURS[1] if 24 <= y < 40 else COLOURS[0] for y in range(64) for _ in range(64)]
    (HERE / "face.png").write_bytes(png(64, 64, face))


if __name__ == "__main__":
    main()
