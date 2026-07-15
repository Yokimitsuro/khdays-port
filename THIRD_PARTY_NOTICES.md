# Third-party notices

This file records code and components that are not covered solely by this repository's MIT license.

## `khdays-decomp`

Repository: <https://github.com/Yokimitsuro/khdays-decomp>

The decompilation and tooling contributed to that repository are dedicated to the public domain under **CC0 1.0 Universal**, subject to the scope and disclaimers stated by that project.

Files copied or adapted from `khdays-decomp` should retain a clear source note containing:

- the original file or symbol;
- the source repository;
- the pinned source commit;
- substantial port-specific modifications.

## SDL3

Project: Simple DirectMedia Layer

Upstream repository: <https://github.com/libsdl-org/SDL>

Pinned release: **3.4.12** (`release-3.4.12`)

Usage: downloaded by CMake with `FetchContent`, compiled as a static library, and linked into the native runtime.

License: **zlib License**.

SDL3 is used for the native window, renderer, events, timing, and future platform abstractions.

## apicula (format reference and validation tool)

Project: apicula — Nintendo DS model tooling

Upstream repository: <https://github.com/scurest/apicula>

License: **0BSD** (BSD Zero Clause License).

Usage: **not linked or embedded**. apicula is used as an external command-line
tool to convert user-provided NSBMD models to glTF for cross-checking the native
mesh decoder, and its public source was read as a reference for the Nintendo DS
MDL0 binary layout (bones, inverse-bind matrices, render commands, and GPU
vertex commands). The decoder in `src/assets/mesh.cpp` is an independent
reimplementation; no apicula code was copied.

## Original game

No license in this repository applies to the original game or to copyrighted material extracted from it.

This repository must not distribute:

- Nintendo DS ROM images;
- extracted graphics, models, animations, maps, scripts, text, audio, or video;
- original game binaries or assembly dumps;
- proprietary Nintendo SDK files;
- proprietary Metrowerks/CodeWarrior files.

All original game content, trademarks, names, and characters remain the property of their respective owners.

## Future dependencies

Every added dependency must be recorded here with:

- project name;
- upstream repository or website;
- exact version or commit;
- license;
- whether it is linked, embedded, modified, or used only as a build tool.

Do not copy code from a repository merely because it is publicly visible. Confirm license compatibility before importing or adapting it.
