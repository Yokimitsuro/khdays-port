# Front-end fidelity audit — boot to gameplay

The port's front-end reproduces the DS **content** and most of its timings, but it
was built screen by screen: each scene hand-places sprites at positions measured
out of savestate OAM. The DS does not work that way, and the difference shows.

This document is the audit of where the port diverges from the DS between boot
and gameplay, what has been **measured**, and what is still unresolved. It is a
work list, not a design.

## 1. The difficulty-select screen is missing entirely

On the DS, choosing NUEVA PARTIDA leads to a difficulty screen before gameplay.
The port skips it: `TitleScene::confirm` changes straight to `kSceneGameplay`.

**Measured** from the `difficult.dst` savestate (`curId` = 1, so this screen is
ov000 like the rest of the front-end):

- **Top screen: zero active OBJ.** All 128 entries sit at Y=192 with the same
  tile. The top screen is BG only.
- **Bottom screen: 13 active OBJ**, three option rows:

| Row | Y | sprites (x, w×h, tile) |
|---|---:|---|
| 0 | 56 | (72, 32×16, t2) (104, 32×16, t6) (136, 32×16, t52) (168, 16×16, t10) |
| 1 *(selected)* | 72 | (80, 32×16, t22) (112, 32×16, t26) (144, 32×16, t60) (176, 16×16, t30) |
| 2 | 88 | (72, 32×16, t2) (104, 32×16, t6) (136, 32×16, t52) (168, 16×16, t10) |

  plus a **16×16 sprite at (72, 72), tile 0** — the cursor, at the selected row's
  left edge.

Three things follow, and they are facts rather than readings:

1. Rows 0 and 2 use **identical tiles**, so the OBJ layer is *not* the difficulty
   names — it is the plate/frame art (three 32-wide pieces and a 16-wide right
   cap). The selected row uses a different set (22/26/60/30) and is shifted
   **+8 px** in X. The names are on the **BG layer**.
2. Row pitch is **16 px**, not the 28 px the title menu uses.
3. All OBJ are 256-colour, priority 2, palette 0.

**Blocked on:** the BG layer. The names live there, and the port has no reading
of which `UI/cm/cmo_&.p2` screen/tilesheet/palette triple the DS pairs for this
screen. An earlier attempt at this screen was reverted for filling that gap with
eyeballed offsets; do not repeat that. The pairing is game data — see §3.

## 2. Divergences the port already admits in comments

These are flagged in the source as approximations, and each needs a measured
replacement:

- `TitleScene::kPagePitch = 256.0F` — "port rendition" of the page scroll. ov000
  does not use a uniform pitch; see §3.
- ~~`TitleScene::draw_selection_cursor` cycles the highlight through cells 0..3~~
  **Resolved.** `func_ov000_0205157c` pulses a **blend level**, not a cell: a
  Tween ping-pongs Q12 `0x2000` ↔ `0x8000` over 500 ms per leg, restarting
  reversed at each end, and the sample `>> 12` is stored as the DS blend
  coefficient clamped to 0..16 (`func_020327e0`). Mode 0 is linear
  (`FUN_02035da8` = `from + elapsed * (to - from) / duration`), and `nDirection`
  starts 0 so the first leg runs 2 → 8. `RENDER_NODES.md` had it right; the cell
  cycle was an inference from the same 500 ms. Now drawn as a linear 2/16..8/16
  level on the resting cell. *Which* cell the cursor uses is still unverified.
- `has_save_data()` is hard-coded `false`, so CARGAR is never reachable.
- `BootLogoScene` skips the whole logo chain on A or Start. Whether the DS
  accepts a skip there, and on which button, is **not established**.

## 3. The DS lays the front-end out from a data table, not from code

`Ov000_LayoutSelectionPages` (`arm9_ov000::0204fdec`) shows the real mechanism:

- it opens **`UI/cm/cmo_&.p2`** (`Msg_OpenContainerAndReadHeader`, sub-file
  selector `0xe`) for languages 2..5, and binds a **0x24-entry table** into the
  scene's selection object (`Ov000_LoadBlockProcessAndFree(..., 0x24, ...)`);
- widgets are then addressed **by entry id** — `FindEntryById(obj, 0xb)`, `0xd`,
  `0x10`, `0x14`, `0x15`, `0x3c` — and their positions read and written with
  `Ov000_GetEntryPosition` / `Ov000_SetEntryPosition`;
- positions are **20.12 fixed point**: the four page x targets are exactly
  `-0x100000`, `-0x155000`, `-0x1aa000`, `-0x200000` (−256, −341.25, −426.5,
  −512 px) — deliberately **not** a uniform pitch — and the nudges are `±0x8000`
  (±8 px);
- the sub-engine blend is set explicitly: `G2x_SetBlendAlpha_(..., 4, 0x10, 8, 8)`.

This is the same widget-graph the navigation walker `func_ov000_020552b4` walks,
and the same family as ov008's `.ui` records (see `docs/OV008_MENU.md`).

**What this means for the port:** every hand-measured position in
`title_scene.cpp` / `main_menu_scene.cpp` / `save_file_scene.cpp` is a
reconstruction of a number the game already stores. Loading the entry table would
replace the measurements with the game's own values — which is the only way the
front-end becomes exact rather than close.

**Not established:** which container sub-file holds the 0x24-entry table, its
record layout, and whether the title menu's page scroll uses this same table or a
different one. `Ov000_LayoutSelectionPages` is named for the *panel* selection
pages; it has not been shown to drive the title.

## Work list, in dependency order

1. Decode the ov000 entry table (record layout + how ids map to sprites). This is
   the keystone: it unblocks exact positions everywhere in the front-end.
2. Resolve the `cmo_&.p2` screen/tile/palette pairing for the BG layer, the same
   way `data_ov000_0205a9d4` was read for `ttl.p2`.
3. Build the difficulty screen from 1 + 2 — plates and cursor from the table, the
   names from the BG.
4. Re-derive the title's page scroll and selection pulse from the table instead
   of the current hand-written eases, and settle the cell-cycle vs alpha-tween
   contradiction.
5. Save data, so CARGAR and the save-file screen become reachable.
