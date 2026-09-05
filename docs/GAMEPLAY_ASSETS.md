# Gameplay assets — what is on disk, and what is still unidentified

Everything here is **measured** from the extracted data with the port's own
`--model-info`, not inferred. A small native technical harness now exercises the
assets in `wd_tt` room 0. Its movement, camera, and goal are port-side
scaffolding while the corresponding ov002 systems are reconstructed; room
collision, Roxas animation, and the attached weapon come from the game data.
The guessed HUD crop has been removed: reconstructing the retail HUD requires
porting ov002's runtime tile/gauge compositor, not selecting rectangles from an
atlas. See `ROADMAP.md` Phase 5.

Paths below are under the unpacked data root
(`data/extracted/<hash>/decompressed/unpacked/`, git-ignored).

## Archive / slot convention

Model archives are `KAPH` containers (`*.p` / `*.pak`) unpacked to `slot_N/`.
Confirmed slot meaning from the containers and `--model-info`:

| slot | format | content              |
|------|--------|----------------------|
| 0    | NSBCA  | skeletal animation   |
| 1    | NSBVA  | visibility animation |
| 2    | NSBMA  | material animation   |
| 3    | NSBTP  | texture-pattern anim |
| 4    | NSBTA  | texture-SRT anim     |
| 7    | NSBMD  | model (mesh)         |

A given archive fills only the slots it needs (a character has 0 + 7; an
effect may have 0/1/2/4 and no 7).

## Playable / party character models — `ba/ch/<code>/`

Present and loading. Each `<code>` has variant archives (`def.p`, `def_hb.p`,
`def_hho.p`, …) whose `slot_7` is the model and `slot_0` is a skeletal
animation. Roxas (`ro`) is `roxas_b`: 24 bones, 13 materials, 476 vertices — a
real rigged, skinnable model with real NSBCA clips.

Measured `code → model name` (all 21):

| code | model     | code | model      | code | model    |
|------|-----------|------|------------|------|----------|
| ax   | axel      | lu   | luxord     | so   | sora     |
| de   | de_wa01   | ma   | marluxia   | ve   | vexen    |
| do   | donald    | mi   | mickey     | xa   | xaldin   |
| go   | goofy     | r2   | roxas_bf   | xe   | xemnas   |
| la   | larxene   | ri   | riku       | xi   | xigbar   |
| le   | lexaeus   | ro   | roxas_b    | xo   | xion     |
| lu   | luxord    | sa   | saix       | ze   | zexion   |

(`de` reports the model name `de_wa01`; its character identity is not confirmed,
so it is listed as the raw model name, not guessed.) Note `xi = xigbar`, **not**
Xion — Xion is `xo`. Measuring caught an earlier guess that had this wrong.

## Effects / magic — `ba/ef/`, `ba/ma/`

These are **not maps**, despite `ma`. `ba/ma/fi.p` slot_7 is the model
`ef_fire_0` (a fire effect); the `*_mo.p` entries (`a0_mo.p`, `s0_mo.p`, …) are
animation-only (slots 0/1/2/4, no mesh). Treat `ba/ma` and `ba/ef` as the battle
effect/magic set.

## Mission data — `mi/`

- `mi/ch/` — mission/event characters (enemies, story cast). Models use an `e_`
  prefix: `mi/ch/00` = `e_xem_hb` (Xemnas), `11` = `e_diz_hb` (DiZ), `26` =
  `e_jas_hb` (Jasmine). Codes are hex.
- `mi/ob/` — 263 object archives. The largest model, `mi/ob/1B` = `B2END_A01`
  (50 bones, 44 materials, 1112 vertices), looks like a set piece, not a
  character.
- `mi/wd/` — the ten **world archives**, `wd_<code>`; this is the stage. See
  below.
- `mi/mi/` — 114 numbered mission files plus `mdb.z`, `eid.z`, `evi`, `trbox`.
- `mi/mo/`, `mi/se/` — not yet inspected here.

## Resolved: the stage lives in `mi/wd/wd_<code>`

This section used to record the walkable stage mesh as unidentified. It is
identified now, and it was never going to be found by folder name — the answer
is a container nobody had opened.

Each of the ten `mi/wd/wd_<code>` world archives is a P2 whose **sub-file 0 is a
room table** and whose remaining sub-files alternate **room-data blobs** and
**`KAPH` room geometry**. `wd_tw` decodes to `tw_03_1` (1402 vertices, 398
polygons), a second layer `tw_03_2`, and the shared props `gate_lock2` and
`lightwall` — the last of which corroborates the `gate*` collision names carried
in the room data.

`mi/ob` is **not** excluded by this: `mi/ob/F5` is also `tw_03_1` (1406
vertices), so room geometry exists in both places. What `mi/wd` adds is the
*organisation* — which models belong to which room, together with the room data
that names the collision volumes and surface types.

Full format, and the room-to-sub-file mapping that is still **not** decoded:
[MISSION_WORLD_DATA.md](MISSION_WORLD_DATA.md). What consumes it at runtime:
[GAMEPLAY_RUNTIME.md](GAMEPLAY_RUNTIME.md).

## What the engine can already do with these

`--render-model FILE [--anim FILE]` renders any of the above in 3D with skinning
and NSBCA playback (GPU path). For a world archive, `--world-info` lists its
rooms and models and `--extract-world` writes each `KAPH` out in the
`slot_N/0000.ext` layout that viewer already reads. The gameplay scene now uses
that same neutral 3D data in the native frame loop: it renders `wd_tt` room 0,
the real animated Roxas model, its `ro_w01000` Keyblade from `ba/ch/ro/w_.p2`,
and a small goal ring offscreen to RGBA before blitting it through the game
renderer. The room-data collision mesh supplies both the floor query and a
lateral sphere sweep. Run the harness directly with `--playable-demo`.

## HUD reconstruction status

`UI/btl/main.p2` is an asset source, not a ready-made HUD bitmap. Ghidra shows
that ov002 builds the gauges at runtime:

- `Ov002_BuildGaugeRowMap` constructs 48 six-byte row records and publishes a
  0xc0-byte display map;
- `Ov002_DrawGaugeSpan` writes individual 4bpp pixel columns right-to-left,
  using a seven-step shade ramp and the two measured layouts `4/46/0/2` and
  `6/77/1/0`;
- `Ov002_RebuildGaugeFillRows` converts total/current units into 46-cell rows;
- `Ov002_UpdateGaugeRow` pairs rows and selects filled, empty, and terminal
  styles; `Ov002_RedrawGaugeCells` updates only the changed cells.

The previous `HP` / `LOCKED ON` rectangles were therefore unverified atlas
crops and have been removed. The next correct implementation is a neutral 4bpp
gauge compositor driven by these functions and the real player state, followed
by the surrounding panel/cell layout; it must not reintroduce cropped labels as
if they were the complete HUD.
