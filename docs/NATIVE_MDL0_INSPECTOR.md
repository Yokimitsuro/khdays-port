# Native MDL0 inspection milestone

The runtime can already decode and display embedded TEX0 textures. This milestone
adds structural parsing of the corresponding MDL0 model data before attempting
to render geometry.

## Added parser coverage

- BMD0 section table;
- MDL0 model dictionary;
- model header and statistics;
- material names;
- mesh names and headers;
- Nintendo DS GPU command packets;
- opcode and vertex-command counts.

This milestone intentionally does not yet execute the MDL0 software render
command list, skinning matrices, or convert GPU vertex commands into a PC mesh.

## Install

Extract the ZIP over the repository root, keeping the previous TEX0 and SDL3
files already present.

Regenerate and compile:

```powershell
cd "E:\KH 3582\khdays-port"

Remove-Item .\build -Recurse -Force -ErrorAction SilentlyContinue

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## Inspect the Vexen model

```powershell
.\build\Debug\khdays-port.exe `
  --model-info ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked\mi\ch\03\slot_7\0000.nsbmd"
```

Expected high-level values:

```text
Model 0: e_vex_hb
Bones/matrices: 26
Materials: 7
Meshes: 7
Vertices: 984
Polygons: 282
```

The seven meshes should report a combined 984 vertex commands.

## Inspect the small arm model

```powershell
.\build\Debug\khdays-port.exe `
  --model-info ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked\mi\ch\2A\slot_7\0000.nsbmd"
```

Expected high-level values:

```text
Model 0: 02start_Arm
Bones/matrices: 3
Materials: 1
Meshes: 2
Vertices: 232
Polygons: 72
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
```

## Commit

```powershell
git add CMakeLists.txt include/khdays/assets/mdl0.h src/assets/mdl0.cpp `
  src/main.cpp include/khdays/port.h tests/cpp/test_mdl0.cpp `
  docs/NATIVE_MDL0_INSPECTOR.md

git commit -m "assets: inspect MDL0 models and GPU command streams"
git push
```

## Next milestone

The next change will decode the GPU vertex commands into a neutral CPU mesh:

```text
positions + UV coordinates + vertex colors + primitive indices
```

That neutral mesh can then be rendered through SDL's GPU API or exported to OBJ
for validation.
