# Native TEX0 resource-loading milestone

This overlay moves TEX0 decoding from the Python research tools into the native
C++ runtime.

## What it adds

- Native BMD0/BTX0 section parsing.
- Native TEX0 dictionaries.
- Native palette decoding.
- Native texture decoding for:
  - A3I5;
  - 4-color;
  - 16-color;
  - 256-color;
  - A5I3;
  - direct BGR555.
- SDL3 upload as an RGBA texture.
- Nearest-neighbor rendering inside the first DS-screen placeholder.
- `--resource` and `--texture` command-line options.
- A C++ synthetic TEX0 parser test with no copyrighted data.

Nintendo DS 4×4 compressed textures are reported as unsupported for now.

## Install

Extract over the repository root.

Regenerate the build:

```powershell
cd "E:\KH 3582\khdays-port"

Remove-Item .\build -Recurse -Force -ErrorAction SilentlyContinue

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

## Run with the Vexen resource

```powershell
.\build\Debug\khdays-port.exe `
  --resource ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked\mi\ch\03\slot_7\0000.nsbmd" `
  --texture "ve_00"
```

The first DS screen should display `ve_00`. The console should report:

```text
Loaded texture 've_00' ...
```

Available names in this file include:

```text
ve_00
ve_01
ve_02
ve_hair00
ve_hair01
ve_robe00
ve_robe01
```

## Run all native tests

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Expected tests:

```text
khdays-port-version
khdays-tex0-parser
```

## Commit

```powershell
git add CMakeLists.txt include src platform tests/cpp docs/NATIVE_TEX0_RUNTIME.md
git commit -m "assets: load TEX0 textures in the native runtime"
git push
```

Do not commit the NSBMD, extracted PNG files, `data/`, or build outputs.
