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
- 2D/UI graphics; *(done — NCLR palettes, NCGR tile graphics, and NSCR tilemaps
  decode and compose to RGBA (`khdays::assets::decode_nclr`/`decode_ncgr`/
  `decode_nscr`/`compose_background`), auto-decompressing `.z` blobs;
  `--render-tiles` / `--render-bg` export BMP. Verified against real battle-UI
  graphics)*
- fonts; *(done — NFTR bitmap fonts decode to glyphs with proportional widths
  and a character map (`khdays::assets::decode_nftr`), and `render_text` draws a
  UTF-16 string to RGBA; `--render-text`. Verified rendering real game text)*
- models and skeletons; *(done — MDL0 decode with bones and skinning to a
  neutral, animation-ready mesh, validated against apicula)*
- a native 3D renderer that draws decoded models with their textures;
  *(done — SDL3 GPU renderer, depth-tested, per-material TEX0 textures, an orbit
  camera, and a --anim override for playing any NSBCA, incl. battle animations)*
- animations (NSBCA), re-posing the palette per frame;
  *(done — NSBCA decode + per-frame CPU skinning; the viewer auto-detects a
  model's sibling animation)*
- maps;
  *(done for individual environment models — maps are NSBMD, e.g. mi/ob/*
  "tt_map"/"TTL_town", and render with the existing renderer; assembling a full
  mission scene from placed objects needs layout data and is a later step)*
- message data; *(done — the game's text in `db/db_<lang>.p2` (a "P2" container
  of LZ11-packed UTF-16LE sub-files) is fully decoded by
  `khdays::assets::load_p2_archive`: 3480 strings across 23 sub-databases in each
  of `db_en`/`db_es`, via `--message-info` / `--dump-messages`. Format confirmed
  from the decompiled loader and byte-checked — see
  [docs/MESSAGE_DATA_P2.md](MESSAGE_DATA_P2.md). UI strings in `UI/*/str/*.s.z`
  are still to be decoded)*
- audio metadata *(done — SDAT inventory: sequences, banks, wave archives,
  streams, by name via --audio-info)*; and **waveform playback** *(done — SWAR
  wave archives decode to PCM (PCM8/PCM16/IMA-ADPCM) via
  `khdays::assets::open_sdat`/`decode_swav`, play through an SDL3 audio backend
  (`--play-sound`), and export to WAV (`--extract-wav`); byte-verified against an
  independent decoder)*; and **sequenced music** *(done — a software synthesizer
  (`khdays::assets::render_sequence`) runs an SSEQ's multi-track bytecode,
  resolves notes to SBNK instrument regions (ADSR + pan), and mixes pitched
  voices with cubic resampling to stereo PCM; `--render-sequence` /
  `--play-sequence`. First-version quality — the residual grain is the source
  ADPCM's 4-bit fidelity; the exact DS envelope curves are approximated.)*

**Exit condition:** a real model loads from user-generated data and renders,
textured and animated, in the native window. *(met)*

## Modding & content pipeline (decomp-independent)

A core reason to build a native port is first-class modding: because the runtime
already decodes assets to neutral, open forms (neutral mesh with a skinning
palette, RGBA textures), replacing content can be a supported feature instead of
byte hacking. These tools operate only on the user's own data and user-created
mods — they never distribute copyrighted assets.

- **Open-format export** (extract to edit): OBJ *(done)* → glTF with skeleton and
  skinning, plus PNG texture dumps.
- **Asset-override loader:** the runtime resolves each asset through a search
  path (`mods/…` before the extracted DS asset). This is the foundation.
  *(done for textures, models, text, sound, 2D graphics, and fonts —
  `khdays::resource` (see `docs/ARCHITECTURE.md`) resolves
  `mods/<Mod>/textures/**/<name>.{png,bmp}` before the DS TEX0,
  `mods/<Mod>/models/<ds_name>.gltf` before the DS model,
  `mods/<Mod>/text/<name>.txt` string overrides,
  `mods/<Mod>/sounds/<wave_archive>_<swav>.wav` waveform replacements,
  `mods/<Mod>/graphics/<screen>.{png,bmp}` UI-background redraws, and
  `mods/<Mod>/fonts/<name>.nftr` font swaps; the engine only sees neutral
  formats.)*
- **Import:** load glTF / PNG into the neutral mesh and textures at runtime.
  *(done — rigged glTF import with per-skin palettes; PNG/BMP textures.)*
- **Higher-poly model mods:** a rigged glTF whose joints are named like the DS
  bones replaces the DS geometry and is **animated by the DS skeleton** — the
  DS NSBCA drives the glTF via name-matched bone-world retargeting, so a modder
  can subdivide/sculpt a model in Blender (keeping the skeleton and weights) and
  it animates natively. *(done — validated against an apicula-exported Vexen:
  DS and retargeted-glTF posed geometry match to fixed-point rounding across
  animation frames.)*
- **Hot-reload:** reload overridden assets on file change for fast iteration.
- **In-app asset browser:** list, preview, and swap assets from a debug overlay.
- **Mod packaging and load order** (later): a manifest plus multi-mod handling.
- **Enhancements as mods:** HD textures, widescreen, and other quality-of-life
  options enabled by the clean runtime.

**Exit condition:** a user can drop a replacement asset into a mods folder and
see it in the running native game without touching Nintendo DS formats.

## Phase 4 — Core game flow

The DS backbone (`main` @0x02000bcc → frame loop → scene/task framework) is
traced and named in khdays-decomp; the port reproduces the *flow* natively (DS
hardware init, VBlank sync, and overlay loading are the platform layer's job,
not ported). Port or replace the minimum required systems for:

- startup; *(skeleton — `khdays::game::Game::boot` mirrors BootTask_Construct
  (fresh boot → the logo scene), then runs the frame loop in a native SDL window
  that maps input and draws each scene through a neutral `Renderer`; `--game`
  (windowed) / `--game-demo` (headless).)*
- memory arenas;
- archives and filesystem; *(started — `khdays::vfs` resolves a NitroFS game
  path (e.g. `/db/db_en.p2`, `/mi/ch/03/slot_7/0000.nsbmd`) to the extracted
  data, searching mod overrides (`mods/<Mod>/files/`) then the unpacked,
  decompressed, and raw NitroFS views; `--vfs-resolve`. Container-by-index
  access is a later refinement.)*
- overlay/module registration;
- task scheduling; *(done — `khdays::game::Object`/`ObjectList` model the DS
  per-frame object update (`func_02023adc`): each object runs a state-machine
  coroutine whose initial state is its constructor's result and which returns
  the next state, walked once per frame.)*
- scene transitions; *(done — the DS dispatcher (`func_0202099c`) is matched: a
  pending scene id indexes `g_SceneTable` ({overlay, class} per id), tears down
  the old scene once it reports ended, then loads the new one.
  `khdays::game::SceneManager` reproduces this natively — an id→scene table, a
  pending-id latch, and teardown-gated transitions. Real per-scene logic is
  filled in as each scene's constructor is decompiled, starting with the boot
  logo (`func_ov000_0204d630`, ov000).)*
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
