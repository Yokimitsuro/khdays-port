# Native MDL0 mesh decoder milestone

The runtime can already inspect MDL0 structure and count GPU commands. This
milestone interprets the Nintendo DS GPU command stream and reconstructs a
neutral, engine-independent mesh: positions, texture coordinates, vertex colors
and triangle indices.

## What it adds

- A GPU command interpreter (`src/assets/mesh.cpp`) covering:
  - `MTX_RESTORE` (matrix id captured per vertex for future skinning);
  - `COLOR`, `TEXCOORD`, `TEXIMAGE_PARAM` (for UV normalization);
  - all vertex commands: `VTX_16`, `VTX_10`, `VTX_XY`, `VTX_XZ`, `VTX_YZ`,
    `VTX_DIFF`;
  - primitive assembly for `BEGIN_VTXS` modes: triangles, quads, triangle
    strips and quad strips.
- `khdays::assets::NeutralModel` / `NeutralMesh` / `NeutralVertex` types.
- Wavefront OBJ export.
- A `--export-obj FILE [OUTPUT.obj]` command-line option.
- A synthetic mesh-decoder test with no copyrighted data.

## Scope and current limitations

- Positions are decoded in the model's local command space. **Skinning /
  bind-pose matrices are not applied yet**, so on multi-bone models the
  absolute positions differ from a fully posed export. Topology (vertex and
  triangle counts) is already exact.
- Vertices are not deduplicated: each vertex command appends one entry, so the
  decoded vertex count equals the model header's vertex count.

## Validation

Cross-checked against [apicula](https://github.com/scurest/apicula) 0.1.1-dev
(NSBMD → glTF) on two user-extracted models:

| Model | Vertices (decoded / header) | Triangles (khdays-port / apicula) |
|---|---|---|
| `e_vex_hb` (Vexen) | 984 / 984 | 420 / 420 |
| `02start_Arm` | 232 / 232 | 88 / 88 |

The decoded vertex count matches the MDL0 header exactly, and the triangle count
matches apicula's independent decode exactly on both models, confirming the
command-stream walking and primitive assembly.

## Inspect and export

```powershell
.\build\Debug\khdays-port.exe `
  --export-obj ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked\mi\ch\03\slot_7\0000.nsbmd"
```

Expected output:

```text
Decoded model 'e_vex_hb' with 7 meshes, 984 vertices (984 expected), 420 triangles
```

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

Apply the model's bone/bind matrices so decoded positions match a posed export,
then feed the neutral mesh through SDL3's GPU API for on-screen rendering.
