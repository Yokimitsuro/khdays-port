# Roadmap

This roadmap describes technical milestones, not release dates.

## Dependency structure

Not everything waits on the decompilation. Work splits into two tracks that
progress in parallel:

- **Decomp-independent (do now, as far as possible):** the platform runtime and
  the whole asset/format/rendering pipeline — textures, models, skeletons,
  animations, maps, 2D/UI graphics, fonts, text, and audio. These are Nintendo
  DS *file formats* and *platform services*; understanding them needs no
  decompiled game logic. Phases 2 and 3 live almost entirely here, and the port
  can run well ahead of the decompilation on this track.
- **Decomp-gated (incremental):** game logic — boot flow, memory arenas, overlay
  registration, task scheduling, scene transitions, saves, AI, combat. The port
  reimplements *understood behavior* (it does not byte-match), so this track
  advances **subsystem by subsystem as `khdays-decomp` understands each one** —
  it is never blocked on the decompilation reaching 100%.

Practically: push the decomp-independent track hard now to reach a rich asset
viewer / renderer, and fold in game subsystems as their behavior becomes clear.
The format understanding produced here also feeds back into the decompilation.

## Phase 0 — Bootstrap

- Establish README, license, notices, and contribution rules.
- Build a dependency-free executable with CMake.
- Pin a known `khdays-decomp` revision.
- Define generated-data and proprietary-file exclusions.
- Add Windows and Linux build verification.

**Exit condition:** a clean checkout builds without any game data.

## Phase 1 — User-provided data pipeline

- Accept a local Nintendo DS ROM path.
- Calculate SHA-256 and identify supported revisions.
- Parse the Nintendo DS header and filesystem.
- Extract only the files required by the current milestone.
- Store generated data outside source-controlled directories.
- Produce actionable errors for missing or unsupported data.

**Exit condition:** a supported user-provided ROM can generate a deterministic local data directory, while an unsupported ROM is rejected safely.

## Phase 2 — Platform runtime

Create small interfaces for:

- logging;
- timing;
- filesystem access;
- memory allocation;
- input;
- window lifecycle;
- audio output;
- rendering.

Do not mirror every NitroSDK function one-to-one unless behavior requires it. Separate hardware emulation from semantic replacements.

**Exit condition:** a native window runs a deterministic update loop and can read generated game data.

## Phase 3 — Resource viewers

Implement isolated tools or debug modes for:

- textures and palettes; *(done — native TEX0 decoding)*
- models and skeletons; *(done — MDL0 decode with bones and skinning to a
  neutral, animation-ready mesh, validated against apicula)*
- a native 3D renderer that draws decoded models with their textures;
- animations (NSBCA), re-posing the palette per frame;
- maps;
- message data;
- audio metadata.

**Exit condition:** a real model loads from user-generated data and renders,
textured and animated, in the native window.

## Phase 4 — Core game flow

Port or replace the minimum required systems for:

- startup;
- memory arenas;
- archives and filesystem;
- overlay/module registration;
- task scheduling;
- scene transitions;
- save data abstraction.

**Exit condition:** the runtime reaches a recognizable game-owned state without executing Nintendo DS binaries directly.

## Phase 5 — Playable vertical slice

Choose one narrow, reproducible target such as:

- a debug room;
- a single mission map;
- player movement and camera;
- one enemy;
- one combat interaction.

**Exit condition:** the selected slice is playable from start to finish in the native runtime.

## Phase 6 — Full-game bring-up

Progressively add:

- missions;
- menus;
- cutscenes;
- AI;
- combat systems;
- effects;
- audio;
- save compatibility;
- controller remapping;
- modern display options.

A full-game port should not be announced until the boot path and substantial gameplay run without relying on an emulator.
