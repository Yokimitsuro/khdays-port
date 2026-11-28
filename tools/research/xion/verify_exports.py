"""Check glTF skin transforms, buffer bounds and matching OBJ geometry counts.

Uses NumPy; no external model viewer is required.
Usage: python tools/research/xion/verify_exports.py [--dir DIR]"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np

COMP = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_accessor(gltf: dict, blob: bytes, index: int) -> np.ndarray:
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    fmt, size = COMP[acc["componentType"]]
    n = NCOMP[acc["type"]]
    start = view["byteOffset"] + acc.get("byteOffset", 0)
    count = acc["count"]
    vals = np.frombuffer(blob, dtype=np.dtype("<" + fmt), count=count * n, offset=start)
    return vals.reshape(count, n) if n > 1 else vals


def check_one(folder: Path) -> tuple[str, bool, str]:
    gltf_path = folder / f"{folder.name}.gltf"
    gltf = json.loads(gltf_path.read_text())
    blob = (folder / gltf["buffers"][0]["uri"]).read_bytes()
    problems = []
    if gltf["buffers"][0]["byteLength"] != len(blob):
        problems.append("buffer byteLength != file size")
    # accessors within their views
    for i, acc in enumerate(gltf["accessors"]):
        view = gltf["bufferViews"][acc["bufferView"]]
        _, size = COMP[acc["componentType"]]
        need = acc["count"] * NCOMP[acc["type"]] * size
        if acc.get("byteOffset", 0) + need > view["byteLength"]:
            problems.append(f"accessor {i} overruns its bufferView")
    # node globals
    nodes = gltf["nodes"]
    parents = {}
    for i, node in enumerate(nodes):
        for c in node.get("children", []):
            parents[c] = i

    def global_matrix(i: int) -> np.ndarray:
        node = nodes[i]
        if "matrix" in node:
            m = np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T  # column-major -> row-major
        else:
            m = np.eye(4)
        return global_matrix(parents[i]) @ m if i in parents else m

    skin = gltf["skins"][0]
    joints = skin["joints"]
    inv = read_accessor(gltf, blob, skin["inverseBindMatrices"]).reshape(-1, 4, 4)
    inv = np.transpose(inv, (0, 2, 1))  # column-major storage -> row-major matrices
    jmats = np.array([global_matrix(j) for j in joints])
    skin_mats = jmats @ inv

    verts = 0
    tris = 0
    worst = 0.0
    for prim in gltf["meshes"][0]["primitives"]:
        pos = read_accessor(gltf, blob, prim["attributes"]["POSITION"]).astype(np.float64)
        j = read_accessor(gltf, blob, prim["attributes"]["JOINTS_0"]).astype(np.int64)
        w = read_accessor(gltf, blob, prim["attributes"]["WEIGHTS_0"]).astype(np.float64)
        idx = read_accessor(gltf, blob, prim["indices"]).astype(np.int64)
        if idx.max(initial=-1) >= len(pos):
            problems.append("index out of range")
        if abs(w.sum(1) - 1.0).max(initial=0.0) > 1e-3:
            problems.append("weights do not sum to 1")
        # posed = sum w * skin_matrix[joint] * pos
        posed = np.zeros_like(pos)
        for k in range(4):
            m = skin_mats[j[:, k]]
            posed += w[:, k : k + 1] * (np.einsum("nij,nj->ni", m[:, :3, :3], pos) + m[:, :3, 3])
        if len(pos):
            worst = max(worst, float(np.abs(posed - pos).max()))
        verts += len(pos)
        tris += len(idx) // 3
    if worst > 1e-3:
        problems.append(f"skin re-pose drifts from POSITION by {worst:.4f}")
    # OBJ counts
    obj = folder.parent.parent / "obj" / folder.name / f"{folder.name}.obj"
    obj_counts = "-"
    if obj.exists():
        lines = obj.read_text().splitlines()
        ov = sum(1 for l in lines if l.startswith("v "))
        of = sum(1 for l in lines if l.startswith("f "))
        obj_counts = f"{ov}v/{of}f"
        if ov != verts or of != tris:
            problems.append(f"OBJ {ov}v/{of}f != glTF {verts}v/{tris}f")
    status = "OK" if not problems else "FAIL: " + "; ".join(problems)
    return (
        f"{folder.name:14s} glTF {verts:5d}v {tris:4d}t bones {len(joints):2d} "
        f"skin-drift {worst:.1e} obj {obj_counts}  {status}",
        not problems,
        status,
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", type=Path, default=Path(__file__).resolve().parents[3] / "private" / "xion")
    args = ap.parse_args()
    base = Path(args.dir) / "gltf"
    bad = 0
    for folder in sorted(p for p in base.iterdir() if p.is_dir()):
        line, ok, _ = check_one(folder)
        print(line)
        bad += 0 if ok else 1
    print(f"{'all OK' if not bad else str(bad) + ' failed'}")
    return 0 if not bad else 1


if __name__ == "__main__":
    raise SystemExit(main())
