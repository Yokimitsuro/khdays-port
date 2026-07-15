# khdays-port

Experimental native PC runtime and source port for **Kingdom Hearts 358/2 Days**, developed alongside [`khdays-decomp`](https://github.com/Yokimitsuro/khdays-decomp).

> [!WARNING]
> This project is in early development. The native asset pipeline (textures, models, animation, text) works, but it is **not playable** and does not yet boot the game.

> [!IMPORTANT]
> This repository does not contain a ROM, game assets, audio, video, text dumps, proprietary SDK files, or copyrighted game binaries. Users will be required to provide their own legally obtained copy of the game.

## Relationship to `khdays-decomp`

The two repositories have different purposes:

- **`khdays-decomp`** reconstructs Nintendo DS code that compiles byte-for-byte to the original game.
- **`khdays-port`** adapts understood game code and behavior to a portable runtime for modern systems.

Matching and reverse-engineering work belongs in `khdays-decomp`. Platform abstractions, native rendering, input, audio, filesystem work, quality-of-life features, and other PC-specific changes belong here.

The port should consume a pinned revision of the decompilation rather than turning the matching repository into a collection of platform-specific `#ifdef` blocks.

## Current status

**Phase 0 — Repository bootstrap**

- [x] Project structure
- [x] Portable CMake build
- [x] Legal and contribution policy
- [x] Pin `khdays-decomp` as a submodule
- [x] Add automated builds
- [x] Verify and identify a supported ROM
- [x] Extract required data locally
- [x] Open a window and initialize the platform runtime
- [x] Load the first game-owned resource from user-provided data
- [x] Port and test the first self-contained game subsystem

**What works now (native asset pipeline)**

- [x] TEX0 textures decoded to RGBA
- [x] MDL0 models decoded with bones and skinning to a neutral, animation-ready mesh
- [x] NSBCA skeletal animation, GPU-skinned per frame
- [x] SDL3 GPU renderer (depth-tested, textured, orbit camera) for models and environment maps
- [x] Modding: texture and rigged-model overrides via a `mods/` folder (see [Modding](#modding))
- [x] Message text extracted from the `db_<lang>.p2` container (`--message-info` / `--dump-messages`)
- [x] SDAT audio inventory (sequences, banks, wave archives) — *metadata only; no playback yet*

Still ahead: audio playback, game boot flow, overlays, and gameplay. See
[`docs/ROADMAP.md`](docs/ROADMAP.md) for the full milestone plan.

## Documentation

- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestones and phase plan.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the neutral-format layering (importers → resource layer → engine).
- [`docs/MODDING.md`](docs/MODDING.md) — replacing textures and models via the `mods/` folder.
- [`docs/NATIVE_MESH_DECODER.md`](docs/NATIVE_MESH_DECODER.md) — MDL0 geometry/skinning decode to the neutral mesh.
- [`docs/NATIVE_TEX0_RUNTIME.md`](docs/NATIVE_TEX0_RUNTIME.md) — TEX0 texture decoding.
- [`docs/NATIVE_MDL0_INSPECTOR.md`](docs/NATIVE_MDL0_INSPECTOR.md) — the `--model-info` inspector.
- [`docs/PLATFORM_RUNTIME.md`](docs/PLATFORM_RUNTIME.md) — the SDL3 platform runtime.
- [`docs/MESSAGE_DATA_P2.md`](docs/MESSAGE_DATA_P2.md) — the `db_<lang>.p2` message-container format.

## Goals

- Build a native, portable runtime for the game.
- Preserve the behavior of the original game where practical.
- Keep PC-specific code separate from the matching Nintendo DS decompilation.
- Require user-provided game data instead of distributing copyrighted content.
- Support modern input, rendering, audio, display resolutions, and debugging tools over time.
- Document formats and behavior discovered during reverse engineering.

## Non-goals

- Distributing the game, a pre-patched ROM, extracted assets, or proprietary development tools.
- Replacing or weakening the byte-matching goals of `khdays-decomp`.
- Claiming affiliation with or endorsement by the original rights holders.
- Treating guessed or unverified code as completed reverse engineering.

## Proposed architecture

```text
khdays-port/
├── CMakeLists.txt
├── include/
│   └── khdays/
│       └── port.h
├── src/
│   └── main.cpp
├── external/
│   └── khdays-decomp/       # pinned Git submodule
├── platform/
│   ├── common/
│   └── pc/
├── game/
│   └── ported/              # adapted, understood game code
├── tools/
│   ├── verify_rom/
│   └── extract_data/
├── docs/
│   └── ROADMAP.md
└── data/                    # local generated data; never committed
```

The directories beyond the current bootstrap files should be added when they contain real code. Avoid committing empty architecture for systems that have not been designed yet.

## Building the bootstrap executable

Requirements:

- CMake 4.2 or newer
- A C++20 compiler:
  - Visual Studio 2026 on Windows
  - Clang or GCC on Linux
  - Apple Clang on macOS

Configure and build:

```bash
cmake -S . -B build
cmake --build build --config Release
```

Run on Windows with a multi-config generator:

```powershell
.\build\Release\khdays-port.exe
```

Run on Linux or macOS with a single-config generator:

```bash
./build/khdays-port
```

## Adding the decompilation as a submodule

Run this from the repository root:

```bash
git submodule add https://github.com/Yokimitsuro/khdays-decomp external/khdays-decomp
git submodule update --init --recursive
git add .gitmodules external/khdays-decomp
git commit -m "build: pin khdays-decomp as a submodule"
```

Do not automatically track the latest decompilation commit during normal builds. Update the pinned revision deliberately, test the port, and record the decomp commit in the port commit message or pull request.

## First technical milestone

The first useful milestone is not “boot the entire game.” It is:

1. Accept a local `.nds` path.
2. Calculate and display its cryptographic hash.
3. Reject unsupported revisions cleanly.
4. Extract required files into ignored local storage.
5. Open a native window.
6. Load and visualize one texture, model, or map resource.
7. Port one self-contained function or subsystem and compare its behavior against the original game.

This creates a legal, testable pipeline before attempting the boot flow, overlays, graphics emulation, audio, or gameplay.

## Modding

Because the runtime decodes DS assets into neutral, open formats before anything
reaches the engine, replacing content is a supported feature rather than byte
hacking. Drop open-format files into a `mods/` folder and the resource layer
resolves them ahead of the original DS asset:

- **Textures** — a PNG/BMP under `mods/<Mod>/textures/**/<name>.png` overrides a
  DS texture. UVs are normalized by the original DS size, so higher-resolution
  (HD) replacements map correctly and are not downscaled.
- **Models** — a rigged glTF at `mods/<Mod>/models/<ds_name>.gltf` replaces a DS
  model's geometry and is **animated by the DS skeleton**: joints are matched to
  DS bones by name, so existing DS animations play on higher-poly geometry with
  no animation authoring.

Mods only ever contain your own edits — never copyrighted game data — and
`mods/` is git-ignored. See [`docs/MODDING.md`](docs/MODDING.md) for the folder
layout and the export/edit workflow.

## Contributing

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) before opening a pull request.

Contributions must not include copyrighted game data, ROMs, proprietary Nintendo/Metrowerks tools, or code copied from projects with incompatible licenses.

## License

Original port code in this repository is licensed under the [MIT License](LICENSE).

Code imported or adapted from `khdays-decomp` remains subject to that project's CC0 1.0 dedication. Third-party components retain their own licenses. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

The MIT license applies only to the contributors' original work. It does not grant rights to the original game, its assets, names, characters, trademarks, or other copyrighted material.

## Disclaimer

This is an unofficial fan-made reverse-engineering and preservation project. It is not affiliated with, sponsored by, or endorsed by Square Enix, Disney, Nintendo, h.a.n.d., or any other rights holder. All trademarks and original game content belong to their respective owners.
