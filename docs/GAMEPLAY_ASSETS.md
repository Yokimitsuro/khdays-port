# Gameplay assets — what is on disk, and what is still unidentified

Groundwork for a future gameplay slice. Everything here is **measured** from the
extracted data with the port's own `--model-info`, not inferred. The gameplay
*logic* (player movement, collision, camera, combat) is a separate matter: it is
not decompiled, so this file deliberately stops at "which assets exist and load"
and does not describe behaviour. See `ROADMAP.md` Phase 4.

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
- `mi/mo/`, `mi/se/` — not yet inspected here.

## The gap: the walkable field / stage mesh is not cleanly identified

There is **no folder that is obviously "the mission N field mesh you walk on."**
`ba/ma` is effects; `mi/ob` is objects; `mi/ch` is characters. A mission's
playable environment is most likely **assembled from `mi/ob` objects placed by
mission data**, or loaded through the runtime-built archive handles that static
analysis cannot name (the same opaque-handle problem noted in the project brief).

Identifying it should use the project's proven method — read it out of the
running game from a DeSmuME savestate of an in-mission frame (which archive
handles are resident, what geometry is in VRAM) — **not** guessed from folder
names. Until then, a gameplay slice has real characters and animations but no
confirmed ground to stand them on.

## What the engine can already do with these

`--render-model FILE [--anim FILE]` renders any of the above in 3D with skinning
and NSBCA playback (GPU path). This proves the renderer and animation systems
work on real gameplay assets. It is a **CLI path**: the in-game frame loop draws
through a 2D-only `Renderer` (`include/khdays/game/renderer.h`), so a gameplay
scene that shows a 3D character would first need the 3D renderer wired into the
game loop (or the model rendered offscreen to RGBA and blitted).
