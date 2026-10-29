"""Writes the Limit example's made-up character (characters/spinner/): a figure of three boxes on a skeleton of its own
(pelvis, spine, head), its face, and its clips -- standing, and its Limit's start, spin and end -- each a glTF file
with its buffers inside. Nothing of the game: run it again to remake them."""

import base64
import json
import math
import struct
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent / "characters" / "spinner"

# (name, parent, head's height)
BONES = [("pelvis", -1, 0.81), ("spine", 0, 1.05), ("head", 1, 1.30)]
# (bone, low corner, high corner, texel)
BOXES = [
    (0, (-0.15, 0.00, -0.10), (0.15, 0.80, 0.10), 1),
    (1, (-0.20, 0.80, -0.12), (0.20, 1.25, 0.12), 0),
    (2, (-0.14, 1.25, -0.14), (0.14, 1.55, 0.14), 2),
]
COLOURS = [(40, 170, 90), (230, 230, 240), (240, 200, 160), (20, 20, 20)]  # the texture's four texels


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
        quad = [0, 1, 2, 0, 2, 3]
        flip = (axis == 1) ^ (side == 0)
        indices += [base + i for i in (quad if not flip else [0, 2, 1, 0, 3, 2])]
    return positions, normals, indices


class Buffer:
    """A glTF buffer, its views and accessors, written inside the file."""

    def __init__(self) -> None:
        self.blob = b""
        self.views: list[dict] = []
        self.accessors: list[dict] = []

    def add(self, data: bytes, count: int, kind: str, component: int, target=None, bounds=None) -> int:
        while len(self.blob) % 4:
            self.blob += b"\0"
        view = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.views.append(view)
        self.blob += data
        accessor = {"bufferView": len(self.views) - 1, "componentType": component, "count": count, "type": kind}
        if bounds is not None:
            accessor["min"], accessor["max"] = bounds
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def into(self, gltf: dict) -> None:
        uri = "data:application/octet-stream;base64," + base64.b64encode(self.blob).decode("ascii")
        gltf["buffers"] = [{"byteLength": len(self.blob), "uri": uri}]
        gltf["bufferViews"] = self.views
        gltf["accessors"] = self.accessors


def skeleton_nodes() -> list[dict]:
    nodes = [{"name": "spinner", "mesh": 0, "skin": 0}]
    for k, (name, parent, height) in enumerate(BONES):
        below = BONES[parent][2] if parent >= 0 else 0.0
        node = {"name": name, "translation": [0.0, height - below, 0.0]}
        children = [1 + c for c, b in enumerate(BONES) if b[1] == k]
        if children:
            node["children"] = children
        nodes.append(node)
    return nodes


def model(buf: Buffer) -> dict:
    """The figure: its mesh, its skin and its texture."""
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
    lows = [min(p[k] for p in positions) for k in range(3)]
    highs = [max(p[k] for p in positions) for k in range(3)]
    a_pos = buf.add(
        b"".join(struct.pack("<3f", *p) for p in positions), len(positions), "VEC3", 5126, 34962, (lows, highs)
    )
    a_nrm = buf.add(b"".join(struct.pack("<3f", *n) for n in normals), len(normals), "VEC3", 5126, 34962)
    a_uv = buf.add(b"".join(struct.pack("<2f", *t) for t in uvs), len(uvs), "VEC2", 5126, 34962)
    a_jnt = buf.add(b"".join(struct.pack("<4B", *j) for j in joints), len(joints), "VEC4", 5121, 34962)
    a_wgt = buf.add(b"".join(struct.pack("<4f", *w) for w in weights), len(weights), "VEC4", 5126, 34962)
    a_idx = buf.add(b"".join(struct.pack("<H", i) for i in indices), len(indices), "SCALAR", 5123, 34963)
    ibm = b"".join(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -h, 0, 1) for _, _, h in BONES)
    a_ibm = buf.add(ibm, len(BONES), "MAT4", 5126)
    texture = png(4, 1, COLOURS)
    texture_uri = "data:image/png;base64," + base64.b64encode(texture).decode("ascii")
    return {
        "asset": {"version": "2.0", "generator": "khdays-port sdk: CustomLimit make_files.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": skeleton_nodes(),
        "skins": [{"joints": [1, 2, 3], "inverseBindMatrices": a_ibm, "skeleton": 1}],
        "meshes": [
            {
                "name": "spinner",
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
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0}}],
        "textures": [{"sampler": 0, "source": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728}],
        "images": [{"uri": texture_uri}],
    }


def turn(axis: tuple[float, float, float], degrees: float) -> tuple[float, float, float, float]:
    half = math.radians(degrees) / 2
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def clip(name: str, keys: dict[str, list[tuple[float, tuple]]]) -> dict:
    """The figure with one animation: per bone and path ("pelvis rotation", "pelvis translation"), its keys."""
    buf = Buffer()
    gltf = model(buf)
    index = {b[0]: 1 + k for k, b in enumerate(BONES)}
    samplers, channels = [], []
    for what, frames in keys.items():
        bone, path = what.split()
        times = [t for t, _ in frames]
        a_t = buf.add(
            b"".join(struct.pack("<f", t) for t in times),
            len(times),
            "SCALAR",
            5126,
            bounds=([min(times)], [max(times)]),
        )
        width = 4 if path == "rotation" else 3
        a_v = buf.add(
            b"".join(struct.pack(f"<{width}f", *v) for _, v in frames),
            len(frames),
            "VEC4" if width == 4 else "VEC3",
            5126,
        )
        samplers.append({"input": a_t, "output": a_v, "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": index[bone], "path": path}})
    gltf["animations"] = [{"name": name, "samplers": samplers, "channels": channels}]
    buf.into(gltf)
    return gltf


UP = (0.0, 1.0, 0.0)
SIDE = (1.0, 0.0, 0.0)
FRONT = (0.0, 0.0, 1.0)
REST = (0.0, 0.81, 0.0)
RAISED = (0.0, 1.11, 0.0)

CLIPS = {
    # standing: the spine sways
    "idle": {"spine rotation": [(0.0, turn(FRONT, -4)), (1.0, turn(FRONT, 4)), (2.0, turn(FRONT, -4))]},
    # the Limit's start: risen, the head thrown back
    "limit_start": {
        "pelvis translation": [(0.0, REST), (0.5, RAISED)],
        "head rotation": [(0.0, turn(SIDE, 0)), (0.5, turn(SIDE, -30))],
    },
    # one turn of its spin, risen
    "limit_spin": {
        "pelvis translation": [(0.0, RAISED), (0.25, RAISED)],
        "pelvis rotation": [(0.25 * k / 4, turn(UP, 90 * k)) for k in range(5)],
        "spine rotation": [(0.0, turn(SIDE, 20)), (0.25, turn(SIDE, 20))],
    },
    # down again
    "limit_end": {
        "pelvis translation": [(0.0, RAISED), (0.5, REST)],
        "spine rotation": [(0.0, turn(SIDE, 20)), (0.5, turn(SIDE, 0))],
    },
}


def main() -> None:
    (HERE / "model").mkdir(parents=True, exist_ok=True)
    (HERE / "clips").mkdir(exist_ok=True)
    buf = Buffer()
    figure = model(buf)
    buf.into(figure)
    (HERE / "model" / "spinner.gltf").write_text(json.dumps(figure, indent=1) + "\n", encoding="utf-8", newline="\n")
    for name, keys in CLIPS.items():
        text = json.dumps(clip(name, keys), indent=1) + "\n"
        (HERE / "clips" / f"{name}.gltf").write_text(text, encoding="utf-8", newline="\n")
    face = [COLOURS[1] if 2 <= x < 6 and 2 <= y < 6 else COLOURS[0] for y in range(8) for x in range(8)]
    (HERE / "face.png").write_bytes(png(8, 8, face))
    print("written:", HERE)


if __name__ == "__main__":
    main()
