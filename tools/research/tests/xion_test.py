"""Synthetic glTF matrix checks; no game data is needed."""

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "xion"))
from xion_extract import _gltf_matrix, model_textures, export_gltf
import nitro3d as n3
from tex import Texture, decode_texture
from verify_exports import check_one


class MatrixTests(unittest.TestCase):
    def test_texture_alpha_is_preserved_in_gltf_materials(self):
        identity = np.eye(4).reshape(-1).tolist()
        material = SimpleNamespace(name="material", texture="t", alpha=1.0, double_sided=False)
        vertices = [n3.Vertex(p, (0, 0), (255, 255, 255, 255), (0, 0, 1), 0) for p in ((0, 0, 0), (1, 0, 0), (0, 1, 0))]
        mesh = n3.Mesh("mesh", "material", "t", "p", 1.0, False, vertices, [0, 1, 2])
        model = n3.Model("test", [n3.Bone("root", -1, identity)], [identity], [[(0, 1.0)]], [mesh], [material], 3)
        info = {
            "model": model,
            "file": "synthetic",
            "rom_offset": 0,
            "file_size": 0,
            "lz": False,
            "bmd0_offset": 0,
            "mdl0_offset": 0,
            "model_offset": 0,
        }
        with tempfile.TemporaryDirectory() as temporary:
            for alpha in (0, 128, 255):
                texture = Texture("t", 1, 1, 1, bytes((255, 255, 255, alpha)))
                result = export_gltf("test", info, {"t": texture}, Path(temporary))
                expected = "BLEND" if alpha < 255 else "OPAQUE"
                self.assertEqual(result["materials"][0].get("alphaMode", "OPAQUE"), expected)

    def test_conflicting_palettes_are_rejected_before_export(self):
        materials = [SimpleNamespace(texture="robe", palette=p) for p in ("blue", "red")]
        with self.assertRaisesRegex(ValueError, "multiple palettes"):
            model_textures({"model": SimpleNamespace(materials=materials), "bmd0": b""})

    def test_translation_is_serialized_in_gltf_column_order(self):
        stored = np.eye(4)
        stored[3, :3] = [1, 2, 3]
        actual = _gltf_matrix(stored)
        self.assertEqual(actual[12:15], [1, 2, 3])
        self.assertEqual([actual[i] for i in (3, 7, 11)], [0, 0, 0])

    def test_verifier_composes_parent_before_child(self):
        parent = np.array([[0, -1, 0, 0], [1, 0, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])
        child = np.eye(4)
        child[0, 3] = 1
        world = parent @ child
        np.testing.assert_array_equal(world[:3, 3], [0, 1, 0])
        blob = bytearray()
        views, accessors = [], []

        def add(values, dtype, kind, component):
            while len(blob) % 4:
                blob.append(0)
            array = np.array(values, dtype=dtype)
            views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": array.nbytes})
            accessors.append(
                {"bufferView": len(views) - 1, "componentType": component, "count": len(array), "type": kind}
            )
            blob.extend(array.tobytes())
            return len(accessors) - 1

        positions = add([[2, 3, 4], [3, 3, 4], [2, 4, 4]], "<f4", "VEC3", 5126)
        joints = add([[0, 0, 0, 0]] * 3, "<u2", "VEC4", 5123)
        weights = add([[1, 0, 0, 0]] * 3, "<f4", "VEC4", 5126)
        indices = add([0, 1, 2], "<u2", "SCALAR", 5123)
        inverse = add([np.linalg.inv(world).T.reshape(-1)], "<f4", "MAT4", 5126)
        document = {
            "asset": {"version": "2.0"},
            "buffers": [{"uri": "sample.bin", "byteLength": len(blob)}],
            "bufferViews": views,
            "accessors": accessors,
            "nodes": [
                {"matrix": parent.T.reshape(-1).tolist(), "children": [1]},
                {"matrix": child.T.reshape(-1).tolist()},
            ],
            "skins": [{"joints": [1], "inverseBindMatrices": inverse}],
            "meshes": [
                {
                    "primitives": [
                        {
                            "attributes": {"POSITION": positions, "JOINTS_0": joints, "WEIGHTS_0": weights},
                            "indices": indices,
                        }
                    ]
                }
            ],
        }
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "gltf" / "sample"
            folder.mkdir(parents=True)
            (folder / "sample.gltf").write_text(json.dumps(document))
            (folder / "sample.bin").write_bytes(blob)
            _, ok, message = check_one(folder)
            self.assertTrue(ok, message)


class TextureTests(unittest.TestCase):
    def decode(self, fmt, transparent, pixel):
        data = bytearray(768)
        data[:64] = bytes([pixel]) * 64
        data[256:] = struct.pack("<H", 0x001F) * 256
        param = (fmt << 26) | (int(transparent) << 29)
        layout = {
            "tex_dict": SimpleNamespace(names=["t"], entries=[struct.pack("<I", param)]),
            "pltt_dict": SimpleNamespace(names=["p"], entries=[struct.pack("<H", 0)]),
            "tex_data": 0,
            "pltt_data": 256,
            "pltt_size": 512,
        }
        return decode_texture(bytes(data), layout, "t", "p")

    def test_a3i5_alpha_endpoints(self):
        self.assertEqual(self.decode(1, False, 0).rgba[3], 0)
        self.assertEqual(self.decode(1, False, 7 << 5).rgba[3], 255)

    def test_index_zero_transparency_flag(self):
        for fmt in (2, 3, 4):
            with self.subTest(format=fmt):
                self.assertEqual(self.decode(fmt, True, 0).rgba[3], 0)
                self.assertEqual(self.decode(fmt, False, 0).rgba[3], 255)


if __name__ == "__main__":
    unittest.main()
