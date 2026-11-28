"""Export Xion rest-pose models from a locally supplied Days ROM, read-only.

See README.md for formats, provenance, dependencies and private output paths.
Usage: python tools/research/xion/xion_extract.py --rom PATH [--out DIR] [--only NAME]"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np
from PIL import Image

import nitro3d as n3
import render
import tex as texmod
from nds import NdsRom, lz_decompress

DEFAULT_OUT = Path(__file__).resolve().parents[3] / "private" / "xion"

# Xion-related model names and texture users found in the European ROM census.
# Descriptions identify the overlay or container used to classify each model.
XION_MODELS = [
    # label, NitroFS path, model index, what it is
    ("xion", "ba/ch/xo/def.p.z", 0, "field/battle model (ba/ch/xo/def.p.z)"),
    ("xion_h", "ba/ch/xo/def_h.p.z", 0, "high-detail model, event overlay ov023's party actors"),
    ("xion_bh_h", "ba/ch/xo/def_hh.p.z", 0, "hooded high-detail, player_xion overlays (nameGroup 1, alternate)"),
    ("xion_bh", "ba/ch/xo/def_ho.p.z", 0, "hooded field model, player_xion overlays (nameGroup 1)"),
    ("e_xio_hb", "mi/ch/0D.z", 0, "event (cutscene) Xion model, mi/ch/*.z"),
    ("e_xih_hb", "mi/ch/43.z", 0, "event Xion model variant (mi/ch/43.z)"),
    ("e_roh_hb", "mi/ch/47.z", 0, "event model using Xion's head/robe textures (mi/ch/47.z)"),
    ("r_xio_lb", "mi/ch/55.z", 0, "mission model using Xion's field textures (mi/ch/55.z)"),
    ("r_foodxio_lb", "mi/ch/56.z", 0, "mission hooded model (mi/ch/56.z)"),
    ("ms017", "Ms/72.p", 0, "model 017 of Ms/72.p (Xion's head/robe textures)"),
    # event objects and world objects that carry Xion's textures / name (not her body)
    ("e_xio_jew", "mi/ob/29.z", 0, "event object e_xio_jew"),
    ("e_xio_ice", "mi/ob/2F.z", 0, "event object e_xio_ice"),
    ("ob_xion", "mi/ob/E6.z", 0, "event object model named xion (mi/ob/E6.z)"),
    ("zz_46xion", "mi/wd/wd_eh", "zz_46xion", "world model zz_46xion (mi/wd/wd_eh, a room's static mesh)"),
    ("ef_050a_004", "mi/ob/E1.z", 0, "effect model ef_050a_004 (Xion textures)"),
    ("ef_050B_001", "mi/ob/E2.z", 0, "effect model ef_050B_001 (Xion textures)"),
]


def _all_bmd0(info: dict, blob: bytes) -> list[tuple[int, bytes, str]]:
    """(offset in the decompressed file, BMD0 bytes, where) for every model resource."""
    if blob[:4] == b"BMD0":
        return [(0, blob, "file")]
    if blob[:4] == b"KAPH":
        bmd = n3.kaph_block(blob, 7)
        if bmd and bmd[:4] == b"BMD0":
            return [(n3.kaph_sections(blob)[7][0], bmd, "KAPH section 7")]
        return []
    if blob[:2] == b"P2":
        out = []
        for at, entry in p2_entries(blob):
            if entry[:4] == b"KAPH":
                bmd = n3.kaph_block(entry, 7)
                if bmd and bmd[:4] == b"BMD0":
                    off = n3.kaph_sections(entry)[7][0]
                    out.append((at + off, bmd, f"P2 entry at 0x{at:x} > KAPH section 7"))
            elif entry[:4] == b"BMD0":
                out.append((at, entry, f"P2 entry at 0x{at:x}"))
        return out
    return []


def load_model(rom: NdsRom, path: str, which: int | str = 0):
    """Read one NitroFS file, unpack LZ/containers, return the chosen model (by
    index in the file, or by its MDL0 model name) and where it was found."""
    f = next((x for x in rom.files if x.path == path), None)
    if f is None:
        raise SystemExit(f"not in the ROM: {path}")
    blob = rom.read(f)
    info = {
        "file": path,
        "rom_offset": f.start,
        "file_size": f.size,
        "lz": False,
        "kaph_offset": None,
        "bmd0_offset": None,
        "mdl0_offset": None,
        "model_offset": None,
    }
    if blob[:1] in (b"\x10", b"\x11"):
        blob = lz_decompress(blob)
        info["lz"] = True
        info["decompressed_size"] = len(blob)
    seen = 0
    if isinstance(which, int):
        wanted_name = None
    else:
        wanted_name = which
    for off, bmd, where in _all_bmd0(info, blob):
        try:
            count = len(n3.parse_dict(bmd, n3.bmd0_sections(bmd)["MDL0"][0] + 8).names)
        except Exception:
            continue
        for index in range(count):
            try:
                model = n3.decode_model(bmd, index)
            except Exception:
                continue
            if wanted_name is not None and model.name != wanted_name:
                continue
            if wanted_name is None and seen != which:
                seen += 1
                continue
            info.update(
                {
                    "bmd0_offset": off,
                    "mdl0_offset": model.mdl0_offset,
                    "model_offset": model.model_offset,
                    "bmd0_bytes": len(bmd),
                    "where": where,
                    "bmd0": bmd,
                    "model": model,
                }
            )
            return info
    raise SystemExit(f"{path}: model {which!r} not found")


def p2_entries(blob: bytes) -> list[tuple[int, bytes]]:
    if blob[:2] != b"P2":
        return []
    count = n3.u16(blob, 2) & 0x7FFF
    start = n3.u32(blob, 0x0C)
    sizes_off = 0x10 + (count + 1) // 2 * 4
    out = []
    for i in range(count):
        word = n3.u32(blob, sizes_off + 4 * i)
        size = word & 0x7FFFFFFF
        at = start + n3.u16(blob, 0x10 + 2 * i) * 0x200
        if size == 0:
            continue
        entry = blob[at : at + size]
        if word & 0x80000000:
            entry = lz_decompress(entry)
        out.append((at, entry))
    return out


def model_textures(info: dict) -> dict[str, texmod.Texture]:
    """Decode every (texture, palette) the model's materials bind."""
    bindings = {}
    for material in info["model"].materials:
        if not material.texture:
            continue
        # shortcut: one palette per texture; add paired material resources before exporting palette variants.
        if material.texture in bindings and bindings[material.texture] != material.palette:
            raise ValueError(f"texture {material.texture!r} uses multiple palettes")
        bindings[material.texture] = material.palette
    bmd = info["bmd0"]
    secs = n3.bmd0_sections(bmd)
    if "TEX0" not in secs:
        return {}
    off, size = secs["TEX0"]
    tex0 = bmd[off : off + size]
    layout = texmod.tex0_layout(tex0)
    out: dict[str, texmod.Texture] = {}
    for mat in info["model"].materials:
        if not mat.texture or mat.texture in out:
            continue
        if mat.texture not in layout["tex_dict"].names:
            continue
        pal = (
            mat.palette
            if mat.palette in layout["pltt_dict"].names
            else (layout["pltt_dict"].names[0] if layout["pltt_dict"].names else "")
        )
        out[mat.texture] = texmod.decode_texture(tex0, layout, mat.texture, pal)
    return out


def bone_world(model) -> list[np.ndarray]:
    """Each bone's world matrix from the object hierarchy (parent * local)."""
    world: list[np.ndarray | None] = [None] * len(model.bones)
    by_name = {i: b for i, b in enumerate(model.bones)}

    def get(i: int, depth: int = 0) -> np.ndarray:
        if world[i] is not None:
            return world[i]
        b = by_name[i]
        local = np.array(b.matrix, dtype=np.float64).reshape(4, 4)
        if b.parent >= 0 and b.parent < len(model.bones) and depth < 64:
            # our numpy matrices are the transpose of the true ones, so the true
            # child world = parent world * local reads local.T @ parent.T here
            w = local @ get(b.parent, depth + 1)
        else:
            w = local
        world[i] = w
        return w

    return [get(i) for i in range(len(model.bones))]


def _inv4(m: np.ndarray) -> np.ndarray:
    try:
        return np.linalg.inv(m)
    except np.linalg.LinAlgError:
        return np.eye(4)


def _gltf_matrix(m: np.ndarray) -> list[float]:
    """The decoder's row-vector arrays already hold glTF's column-major storage."""
    return [float(x) for x in m.reshape(-1)]


def export_gltf(name: str, info: dict, textures: dict, folder: Path) -> dict:
    model = info["model"]
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "textures").mkdir(exist_ok=True)

    worlds = bone_world(model)
    n_bones = len(model.bones)
    inv_binds = [_inv4(w) for w in worlds]

    images, tex_defs = [], []
    tex_index: dict[str, int] = {}
    for tex_name, t in textures.items():
        png = folder / "textures" / f"{_safe(tex_name)}.png"
        Image.frombytes("RGBA", (t.width, t.height), t.rgba).save(png)
        images.append({"uri": f"textures/{png.name}"})
        tex_defs.append({"source": len(images) - 1, "sampler": 0})
        tex_index[tex_name] = len(tex_defs) - 1

    materials, mat_index = [], {}
    for mat in model.materials:
        if mat.name in mat_index:
            continue
        entry = {
            "name": mat.name,
            "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.9},
            "doubleSided": bool(mat.double_sided),
        }
        if mat.alpha < 0.999:
            entry["alphaMode"] = "BLEND"
            entry["pbrMetallicRoughness"]["baseColorFactor"] = [1.0, 1.0, 1.0, max(mat.alpha, 0.0)]
        texture = textures.get(mat.texture)
        if texture is not None and any(alpha < 255 for alpha in texture.rgba[3::4]):
            entry["alphaMode"] = "BLEND"
        if mat.texture in tex_index:
            entry["pbrMetallicRoughness"]["baseColorTexture"] = {"index": tex_index[mat.texture]}
        mat_index[mat.name] = len(materials)
        materials.append(entry)

    bin_path = folder / f"{name}.bin"
    accessors, views, blob = [], [], bytearray()
    primitives = []

    def add_view(data: bytes, target: int | None = None) -> int:
        while len(blob) % 4:
            blob.append(0)
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        blob.extend(data)
        return len(views) - 1

    def add_accessor(view: int, count: int, ctype: int, atype: str, mn=None, mx=None) -> int:
        acc = {"bufferView": view, "componentType": ctype, "count": count, "type": atype}
        if mn is not None:
            acc["min"] = mn
            acc["max"] = mx
        accessors.append(acc)
        return len(accessors) - 1

    pal = np.array(model.palette, dtype=np.float64).reshape(-1, 4, 4)
    # A DS palette entry is a vertex's exact transform. glTF reproduces one only when
    # it is the weighted sum of its bones' worlds x inverse binds with at most four
    # bones; any other entry (a fifth bone, a piece's own scale in the stream) becomes
    # its own joint: its node's global matrix is the palette matrix itself and its
    # inverse bind the identity, so the skin reproduces the vertex exactly.
    blend_entries = []
    use_weights: dict[int, bool] = {}
    for entry in sorted({v.matrix for mesh in model.meshes for v in mesh.vertices}):
        bones = model.palette_bones[entry]
        ok = bool(bones) and len(bones) <= 4
        if ok:
            pred = np.zeros((4, 4))
            for b, w in bones:
                pred += w * (worlds[b] @ inv_binds[b])
            ok = float(np.abs(pred - pal[entry]).max()) < 1e-6
        use_weights[entry] = ok
        if not ok:
            blend_entries.append(entry)
    blend_joint = {entry: n_bones + i for i, entry in enumerate(blend_entries)}
    for mesh in model.meshes:
        if not mesh.vertices:
            continue
        pos = np.array([v.position for v in mesh.vertices], dtype=np.float64)
        uv = np.array([v.texcoord for v in mesh.vertices], dtype=np.float64)
        t = textures.get(mesh.texture)
        if t is not None and t.width and t.height:
            uv = uv / np.array([t.width, t.height])
        else:
            uv = np.zeros_like(uv)
        midx = np.array([v.matrix for v in mesh.vertices], dtype=np.int64)
        m = pal[midx]
        posed = np.einsum("nji,nj->ni", m[:, :3, :3], pos) + m[:, 3, :3]

        joints = np.zeros((len(mesh.vertices), 4), dtype=np.uint8)
        weights = np.zeros((len(mesh.vertices), 4), dtype=np.float64)
        for i, v in enumerate(mesh.vertices):
            if not use_weights[v.matrix]:
                joints[i, 0] = blend_joint[v.matrix]
                weights[i, 0] = 1.0
                continue
            bones = model.palette_bones[v.matrix] if v.matrix < len(model.palette_bones) else []
            total = sum(w for _b, w in bones) or 1.0
            for j, (b, w) in enumerate(bones[:4]):
                joints[i, j] = b
                weights[i, j] = w / total
            if not bones:
                weights[i, 0] = 1.0
        idx = np.array(mesh.indices, dtype=np.uint32)
        if idx.size == 0:
            continue
        a_pos = add_accessor(
            add_view(posed.astype("<f4").tobytes(), 34962),
            len(posed),
            5126,
            "VEC3",
            [float(x) for x in posed.min(0)],
            [float(x) for x in posed.max(0)],
        )
        a_uv = add_accessor(add_view(uv.astype("<f4").tobytes(), 34962), len(uv), 5126, "VEC2")
        a_j = add_accessor(add_view(joints.tobytes(), 34962), len(joints), 5121, "VEC4")
        a_w = add_accessor(add_view(weights.astype("<f4").tobytes(), 34962), len(weights), 5126, "VEC4")
        a_i = add_accessor(add_view(idx.astype("<u2").tobytes(), 34963), int(idx.size), 5123, "SCALAR")
        prim = {
            "attributes": {"POSITION": a_pos, "TEXCOORD_0": a_uv, "JOINTS_0": a_j, "WEIGHTS_0": a_w},
            "indices": a_i,
            "mode": 4,
        }
        if mesh.material in mat_index:
            prim["material"] = mat_index[mesh.material]
        primitives.append(prim)

    nodes = []
    children = [[] for _ in model.bones]
    roots = []
    for i, b in enumerate(model.bones):
        if 0 <= b.parent < len(model.bones):
            children[b.parent].append(i)
        else:
            roots.append(i)
    for i, b in enumerate(model.bones):
        w = worlds[i]
        local = w @ _inv4(worlds[b.parent]) if 0 <= b.parent < len(model.bones) else w
        node = {"name": b.name, "matrix": _gltf_matrix(local)}
        if children[i]:
            node["children"] = children[i]
        nodes.append(node)

    for entry in blend_entries:
        nodes.append(
            {
                "name": f"blend_{entry}",
                "matrix": _gltf_matrix(pal[entry]),
                "extras": {
                    "bones": [b for b, _w in model.palette_bones[entry]],
                    "weights": [w for _b, w in model.palette_bones[entry]],
                },
            }
        )

    # A blend joint's global is its palette matrix, so its inverse bind is that
    # matrix's inverse: global x inverse bind is the identity and the vertex stays put.
    inv_flat = np.array(
        [x for m in inv_binds for x in _gltf_matrix(m)]
        + [x for e in blend_entries for x in _gltf_matrix(_inv4(pal[e]))],
        dtype="<f4",
    )
    inv_acc = add_accessor(add_view(inv_flat.tobytes()), n_bones + len(blend_entries), 5126, "MAT4")

    nodes.append({"name": f"{name}_mesh", "mesh": 0, "skin": 0})
    mesh_node = len(nodes) - 1
    gltf = {
        "asset": {"version": "2.0", "generator": "days-xion-model tools/xion_extract.py"},
        "scene": 0,
        "scenes": [{"name": name, "nodes": roots + [n_bones + i for i in range(len(blend_entries))] + [mesh_node]}],
        "nodes": nodes,
        "meshes": [{"name": f"{name}_mesh", "primitives": primitives}],
        "materials": materials,
        "textures": tex_defs,
        "images": images,
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 10497, "wrapT": 10497}],
        "skins": [
            {
                "name": f"{name}_skin",
                "joints": list(range(n_bones + len(blend_entries))),
                "inverseBindMatrices": inv_acc,
                "skeleton": roots[0] if roots else 0,
            }
        ],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"uri": bin_path.name, "byteLength": len(blob)}],
        "extras": {
            "source": {
                "file": info["file"],
                "rom_offset": info["rom_offset"],
                "file_size": info["file_size"],
                "lz_compressed": info["lz"],
                "decompressed_size": info.get("decompressed_size"),
                "bmd0_offset_in_decompressed": info["bmd0_offset"],
                "mdl0_offset_in_bmd0": info["mdl0_offset"],
                "model_offset_in_mdl0": info["model_offset"],
                "bmd0_bytes": info.get("bmd0_bytes"),
                "model_name": model.name,
            },
            "geometry": {
                "vertices": int(sum(len(x.vertices) for x in model.meshes)),
                "triangles": int(sum(len(x.indices) // 3 for x in model.meshes)),
                "bones": n_bones,
                "meshes": len(model.meshes),
            },
            "textures": {k: {"width": v.width, "height": v.height, "format": v.format} for k, v in textures.items()},
        },
    }
    bin_path.write_bytes(bytes(blob))
    (folder / f"{name}.gltf").write_text(json.dumps(gltf, indent=1))
    return gltf


def export_obj(name: str, info: dict, textures: dict, folder: Path) -> None:
    model = info["model"]
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "textures").mkdir(exist_ok=True)
    png_names = {}
    for tex_name, t in textures.items():
        png = folder / "textures" / f"{_safe(tex_name)}.png"
        Image.frombytes("RGBA", (t.width, t.height), t.rgba).save(png)
        png_names[tex_name] = f"textures/{png.name}"
    mtl = ["# Xion model from 358/2 Days -- generated by tools/xion_extract.py", ""]
    for mat in model.materials:
        mtl += [f"newmtl {_safe(mat.name)}", "Kd 0.8 0.8 0.8"]
        if not mat.double_sided:
            mtl.append("illum 2")
        if mat.texture in png_names:
            mtl.append(f"map_Kd {png_names[mat.texture]}")
        if mat.alpha < 0.999:
            mtl.append(f"d {mat.alpha:.3f}")
        mtl.append("")
    (folder / f"{name}.mtl").write_text("\n".join(mtl))
    lines = [f"# {model.name} from {info['file']} (358/2 Days, read-only extraction)", f"mtllib {name}.mtl"]
    base = 1
    pal = np.array(model.palette, dtype=np.float64).reshape(-1, 4, 4)
    for mesh in model.meshes:
        if not mesh.vertices:
            continue
        t = textures.get(mesh.texture)
        lines.append(f"o {_safe(mesh.name)}")
        lines.append(f"usemtl {_safe(mesh.material)}")
        for v in mesh.vertices:
            m = pal[v.matrix]
            p = m[:3, :3].T @ np.array(v.position) + m[3, :3]
            lines.append(f"v {p[0]:.5f} {p[1]:.5f} {p[2]:.5f}")
        for v in mesh.vertices:
            u, w = v.texcoord
            if t is not None and t.width:
                u, w = u / t.width, 1.0 - w / t.height
            lines.append(f"vt {u:.5f} {w:.5f}")
        for i in range(0, len(mesh.indices), 3):
            a, b, c = (mesh.indices[i] + base, mesh.indices[i + 1] + base, mesh.indices[i + 2] + base)
            lines.append(f"f {a}/{a} {b}/{b} {c}/{c}")
        base += len(mesh.vertices)
    (folder / f"{name}.obj").write_text("\n".join(lines) + "\n")


def _safe(name: str) -> str:
    return "".join(c if c.isalnum() or c in "_-." else "_" for c in name) or "unnamed"


def render_previews(name: str, info: dict, textures: dict, folder: Path, size: int = 512) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    model = info["model"]
    render.render(model, textures, 0, size).save(folder / f"{name}_front.png")
    render.render(model, textures, 90, size).save(folder / f"{name}_side.png")
    sheet = render.turntable(model, textures, size // 2)
    sheet.save(folder / f"{name}_turntable.png")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True, help="your locally supplied Days ROM dump")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--only", default=None)
    ap.add_argument("--size", type=int, default=512)
    args = ap.parse_args()
    out = Path(args.out)
    (out / "gltf").mkdir(parents=True, exist_ok=True)
    (out / "obj").mkdir(parents=True, exist_ok=True)
    (out / "preview").mkdir(parents=True, exist_ok=True)

    rom = NdsRom(args.rom)
    report = []
    for label, path, index, note in XION_MODELS:
        if args.only and args.only != label:
            continue
        info = load_model(rom, path, index)
        model = info["model"]
        textures = model_textures(info)
        verts = sum(len(m.vertices) for m in model.meshes)
        tris = sum(len(m.indices) // 3 for m in model.meshes)
        if verts != model.header_vertex_count:
            print(f"WARNING {label}: decoded {verts} vertices, header says {model.header_vertex_count}")
        export_gltf(label, info, textures, out / "gltf" / label)
        export_obj(label, info, textures, out / "obj" / label)
        render_previews(label, info, textures, out / "preview", args.size)
        entry = {
            "label": label,
            "file": path,
            "note": note,
            "model_name": model.name,
            "rom_offset": info["rom_offset"],
            "file_size": info["file_size"],
            "lz": info["lz"],
            "decompressed_size": info.get("decompressed_size"),
            "bmd0_offset": info["bmd0_offset"],
            "mdl0_offset": info["mdl0_offset"],
            "model_offset": info["model_offset"],
            "bmd0_bytes": info.get("bmd0_bytes"),
            "vertices": verts,
            "triangles": tris,
            "bones": len(model.bones),
            "meshes": len(model.meshes),
            "bone_names": [b.name for b in model.bones],
            "materials": [
                {
                    "name": m.name,
                    "texture": m.texture,
                    "palette": m.palette,
                    "alpha": round(m.alpha, 3),
                    "double_sided": m.double_sided,
                }
                for m in model.materials
            ],
            "textures": {t: {"width": v.width, "height": v.height, "format": v.format} for t, v in textures.items()},
        }
        report.append(entry)
        print(
            f"{label:14s} {path:20s} model={model.name:12s} verts={verts:5d} tris={tris:4d} "
            f"bones={len(model.bones):2d} textures={len(textures)}"
        )
    report_path = out / (f"report-{args.only}.json" if args.only else "report.json")
    report_path.write_text(json.dumps(report, indent=1))
    rom.close()
    print(f"report -> {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
