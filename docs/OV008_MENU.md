# ov008 — the in-game menu overlay

This documents the `ov008` overlay as decompiled in `khdays-decomp` (pin
`5e56c1c` and later). It is **reference material, not yet reproduced in the
port**: ov008 is a gameplay-side subsystem that depends on game state the port
does not model yet (day counter, mission progression, persistent flags). It is
written down here so that, once a `GameState` exists, the menu can be built from
the decomp instead of from OAM measurements.

Do not turn any offset below into port code by eye. Every field cited here comes
from a decompiled function; where a value is a runtime measurement it is called
out as such. Filling the gaps between them with plausible numbers is the mistake
this file exists to prevent.

## What ov008 is

`ov008` is **the in-game menu** — the paged hub you open during play (root menu,
status, panels, mission select, shop, config, save), *not* the boot/title
front-end. The front-end (boot logos → title → menu levels → save-file screen)
is `ov000`; the Mission Mode character select is `ov06`. See `ROADMAP.md`.

Its top-level scene id is **`0x13` (`curId` 19)**, confirmed both by the
decomp (`Ov008_MainMenu_StateTick` names "scene 0x13") and by the runtime
`curId` reading the project had been trying to capture. `Ov008_Menu_*` is a
**generic paged-panel framework** parameterised by a per-scene id — the same
code also serves scene ids `{0,2,4,5,6,9,0xb,0xe,0xf,0x10}` seen in the panel
and archive tables, so those are menu-context ids, not global scene-table ids.

## Scene lifecycle

`Ov008_MainMenu_StateTick` (`func_ov008_0205b230`) is the per-frame state
machine, dispatched through the scene handler table (no direct caller):

- **State 0 (first-time setup):** configure the video hardware
  (`Ov008_MainMenu_SetupDisplay`, `func_ov008_0205a8e0` — VRAM banks, both
  DISPCNTs, 3D clear colour, BG control/priority; platform-layer concern, not
  ported), fill the object-list init params from `data_ov008_0208edd4`
  (`slotCount = 8`, list size 7 or 3), build the object list — directly, or via
  `Ov008_MainMenu_InitObjectListRetry` (`func_ov008_0205b19c`) when the context
  field is set — load the scene-`0x13` layout resource, and pick a layout variant
  (`4`, or `0` once the story counter `GameState_GetField(0,9)` reaches `0x165`).
- **State 1 (second-phase setup):** `Ov008_MainMenu_SetupToolbar`
  (`func_ov008_0205ab5c`), `Ov008_SetupMenuBgCellsAlt`, and
  `Ov008_MainMenu_SetupTextSurfaces` (`func_ov008_0205af54`); stamp the entry
  tick, signal the transition happened this frame.
- **Every frame** ends by drawing the menu panels (`Ov008_DrawMenuPanels`).

## Pages

`Ov008_Menu_ChangePage` (`func_ov008_02063018`) switches the visible page and
fires per-page tag callbacks. Each page carries an 8-entry tag table; the page
being left (`a`) and the page being entered (`b`) are copied from
`data_ov008_0208f2e8` (`struct { u32 a[8]; u32 b[8]; }`). The current page /
state live at ctx `+0x70 / +0x74 / +0x9c`. After the switch it refreshes the
widgets.

## Sub-item grid

`Ov008_Menu_RefreshSubitemGrid` (`func_ov008_02057f58`) rebuilds the 7-entry
sub-item availability grid each tick. The 7 ids come from `data_ov008_0208e958`;
the enable flags are **game progression**:

- all-on when flag `0x200c` is set;
- otherwise derived from the day counter `GameState_GetField(0,9)` and per-item
  flags.

The flags are pushed to entries `0x65..0x6b`, and the selector is repositioned at
the chosen sub-item. This is exactly why the port cannot build this screen yet:
without the day counter and flag store, the grid's contents are unknown, and
they must not be guessed.

## Panels

- `Ov008_Menu_BindScenePanels` (`func_ov008_0205a1fc`): the per-scene parameter
  table entry carries up to three resource names (`name1/2/3`, 0x10 bytes each).
  Each non-empty name is looked up in the scene's resource dictionary
  (`obj+0x5c`, `+0x40` to reach the dict); found index → `obj+0x148/+0x14c`,
  valid flag → `obj+0x140/+0x144`. `name1`/`name3` share panel slot `obj+0x1b8`;
  `name2` uses `obj+0x31c`.
- `Ov008_Menu_InitPanelSubObject` (`func_ov008_02059c88`): registers the panel's
  sprite sequence and loads the layout-archive variant that matches the scene id:
  - scene `0xe`: fixed archive `data_ov008_02090264`;
  - scenes `{0,5,6,0xb,0x10}`: variant 0;
  - scenes `{4,9,0xf}`: variant 0 when slot == 3, else variant 1;
  - scenes `{2,0x13}`: variant 0 when slot == 1, else variant 1;
  - any other scene: nothing (variant stays -1).
- `Ov008_Menu_RenderScenePanels` (`func_ov008_0205a684`): per-frame render and
  gfx submit of the two panel widgets (slot 1 = `obj+0x1b8`/matrix `obj+0x150`,
  slot 2 = `obj+0x31c`/matrix `obj+0x180`), scaling to 100.0 (`0x64000` fx32) in
  scene state `0xd`.
- `Ov008_Menu_LoadSceneText` (`func_ov008_0205a138`): opens the scene's message
  container (message-database unit `0xe`) into `obj+0x1b4`, and loads the
  character weapon model into `obj+0x4d4`.

## Selector and directional prompt

- `Ov008_Menu_PositionSelector` (`func_ov008_02057b7c`): entry `0x15` is the
  selector. Its X is entry 0x15's own layout X, except when shared-context
  `+0x5c6` bit 5 is set it sits `0x30000` (3px, 16.16 fixed) left of the target;
  Y always follows the target entry.
- `Ov008_Menu_UpdateDirectionalPrompt` (`func_ov008_020579a8`): recomputes the
  prompt state `0x16..0x1c` from an input/branch decision tree (also gated on
  flag `0x200c`) and redraws the prompt text on change.
- Navigation is the **same directional walker as ov000** (`func_020552b4`):
  it skips disabled/non-focusable nodes and **does not wrap within a group**.
  The port's front-end scenes already model the no-wrap behaviour.

## Detail panel / sub-scene 8

Three functions drive entering the detail sub-scene:

- `Ov008_Menu_ToggleDetailPanel` (`func_ov008_02057c78`): enter/leave; records
  the panel state in `+0x5c6` bit 5, toggles entries 7/8, hides/shows the
  main-list entries (`0x10/0x11/0x12/0xe/0xf`), snaps/restores the selector.
- `Ov008_Menu_AdvanceIntoPanel` (`func_ov008_02058a28`): one-shot, gated on
  `+0x5c6` bit 7 clear; advances when flag `0x200c` is set, shared-context bit 4
  is clear, and the cursor record's `byte[3] == 8`.
- `Ov008_Menu_CommitEnterSubScene8` (`func_ov008_02058ae0`): the confirm path;
  plays the confirm sound once, latches `+0x5c6` bit 8, sets persistent flag
  `0x200c`, primes sub-scene 8.

## Party/slot panel

`Ov008_Menu_RefreshSlotPanel` (`func_ov008_020574c0`): per-frame refresh of the
4 party/panel slots. Builds a 4-bit active-slots mask, reconciles `+0x5c6`, and
shows/hides three UI entries per slot (ids `slot+0x6f / +0x79 / +0x83`).

## Game-state flags

`Ov008_Menu_ApplyFlagPresets` (`func_ov008_02056ec0`) shows the shape of the
game-state store the port will need: a **packed bit store addressed by
`(bitOffset, bitWidth)`**. Each preset names a 4-bit field via
`entry.id * 4 + 0x92b` and ORs a value in (`GameState_GetField`/`SetField`,
width 4; bits only set, never cleared). The first table (8 entries) is always
applied; the second (14 entries) only once flag `0x200b` is set or the player is
far enough in.

Named flags seen across ov008: `0x200b`, `0x200c` (menu-advanced / detail
committed), `0x200d`, and the day/story counter field `(0, 9)` (story milestone
`0x165`).

## What the port would need before building this

1. A neutral `GameState` (the packed bit store above, plus the day/story
   counter) — this is the true blocker; the grid, toolbar, and flag presets all
   read it.
2. The per-scene parameter table (`SceneParam[]`) that names each menu scene's
   resources and archive variants.
3. The panel/layout archives resolved through the existing `.ui` loader
   (`load_ui_layout`) and sprite-set loader — both already in the port.

Until (1) exists, this screen stays documented, not reproduced.
