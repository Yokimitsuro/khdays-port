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

- ~~`TitleScene::kPagePitch = 256.0F`~~ **Resolved (2026-09-25): the title has
  no page scroll.** `func_ov000_02050ec4` and the selection pulse
  `func_ov000_0205157c` both run on `data_ov000_0205ac24`, the *load-page*
  (save-file) context -- `02050ec4` is the only caller of `0205157c` -- not on
  the title. A title level change is `func_ov000_0204e9a4`: an 8-frame OBJ/BG1
  cross-fade (weight 16 - 4n for n = 1..4, then 4n - 16), swapping the level on
  frame 4.
- ~~the pulsing selection level~~ **Corrected (2026-09-25).** The title cursor
  is object 0, created in mode 1 by `func_ov000_0204cac0`, so the object manager
  plays its own NANR animation 0: cells 0,1,2,3,2,1 for 6 frames each -- the
  cell cycle an earlier version had, with the game's durations.
- `kLoadAvailable` is hard-coded `false` (no save system), so CARGAR is never
  reachable.
- ~~`BootLogoScene` skips the logo chain on A or Start~~ **Resolved: no skip.**
  None of `func_ov000_0204dc38` / `0204dd34` / `0204de30` reads the pad and the
  scene tick `func_ov000_0204d354` is empty. The title intro, by contrast, *is*
  skippable (`func_ov000_0204d338`: A or Start).

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
