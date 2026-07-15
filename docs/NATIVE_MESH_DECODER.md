# Native MDL0 mesh decoder milestone

The runtime decodes an MDL0 model into a neutral, engine-independent mesh by
executing the model's render command stream (SBC) the way the Nintendo DS does:
it drives a matrix-stack virtual machine that builds a palette of rest-pose
matrices from the model's bones, inverse-bind matrices, and skinning equations,
then draws each piece's packed GPU command stream.

## What it adds

- A bone (object) parser: translation / rotation (pivot or full 3x3) / scale.
- Inverse-bind matrix parsing.
- A render-command (SBC) virtual machine: `LoadMatrix`, `StoreMatrix`,
  `MulObject`, weighted `Skin` (NODEMIX), `ScaleUp`/`ScaleDown`, `BindMaterial`,
  and `Draw`.
- A GPU command interpreter covering `MTX_RESTORE`, `COLOR`, `TEXCOORD`,
  `TEXIMAGE_PARAM`, all vertex commands (`VTX_16/10/XY/XZ/YZ/DIFF`) and the four
  `BEGIN_VTXS` primitive modes (triangles, quads, triangle/quad strips).
- `khdays::assets::NeutralModel` / `NeutralMesh` / `NeutralVertex` types.
- A rest-pose matrix palette on the model; each vertex keeps its raw local
  position plus a palette index, so the runtime can re-pose the model for
  animation instead of relying on a baked-in rest pose.
- Wavefront OBJ export (rest-pose positions) and a `--export-obj` option.

## Animation-ready design

Vertices are stored in their raw local space, not baked into the rest pose.
The final position is `palette[matrix_index] * position`. To animate, the same
render command stream is re-executed with animated bone matrices to rebuild the
palette — exactly how the Nintendo DS produces each frame.

## Validation

Cross-checked against [apicula](https://github.com/scurest/apicula) 0.1.1-dev
(NSBMD -> glTF) on two user-extracted models. Vertex and triangle counts match
the MDL0 header and apicula, and the rest-pose bounding boxes match apicula to
within floating-point rounding:

| Model | Vertices | Triangles | Rest-pose bbox vs apicula |
|---|---|---|---|
| `e_vex_hb` (Vexen) | 984 | 420 | matches (~1e-3) |
| `02start_Arm` | 232 | 88 | matches (~1e-3) |

The Nintendo DS binary layout (bone TRS, pivot rotations, inverse binds, SBC
opcodes, GPU vertex commands) follows the public understanding captured by
apicula; `src/assets/mesh.cpp` is an independent reimplementation. See
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

## Export a model

```powershell
.\build\Debug\khdays-port.exe `
  --export-obj ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked\mi\ch\03\slot_7\0000.nsbmd"
```

Expected output:

```text
Decoded model 'e_vex_hb' with 7 meshes, 984 vertices (984 expected), 420 triangles
```

The resulting `.obj` opens as a correctly posed model in any 3D viewer.

## Run tests

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Expected native tests:

```text
khdays-port-version
khdays-tex0-parser
khdays-mdl0-parser
khdays-mesh-decoder
```

## Next milestone

Render the neutral mesh with textures through SDL3's GPU API, then load NSBCA
animation data and re-pose the palette per frame.
