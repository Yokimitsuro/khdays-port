# Roadmap

This roadmap describes technical milestones, not release dates. Completion depends on the continuing reverse engineering of `khdays-decomp`.

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

- textures and palettes;
- models and skeletons;
- animations;
- maps;
- message data;
- audio metadata.

**Exit condition:** at least one real map or model can be loaded and inspected from user-generated data.

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
