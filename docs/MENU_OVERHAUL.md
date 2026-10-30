# The Menu Overhaul

A mod that redraws Days's menus in the look of the Kingdom Hearts HD collections, on one screen.
The port carries its code; the mod's folder carries its pieces, made by `tools/mods/menu_overhaul.py`
from the player's own copies of the collections and of the game (see "Making the mod"), so none of the
games' is in this repository. Without the folder nothing of it runs: the port's screens, pauses, sounds
and timings stay as they are.

## Turning it on

A folder of `mods/` holding `days-mods/menu-overhaul.txt` (any contents) turns it on
(`port/runtime/mods/installed.h`, looked for once at the start). The installer puts it inside the HD PACK,
its files over the HD menus' it replaces (see "Making the mod"); a folder of its own, as
`tools/mods/menu_overhaul.py` makes it, must be named before `HD PACK` to override its pieces, as for every
mod. Every feature below also needs the screens on one picture (`screens = one`).

## What it changes

| Part | Code | Pieces |
|---|---|---|
| The camp menu, the story's and Mission Mode's: KH2's command buttons, Birth by Sleep's plates, the character's model and numbers, the panels' grid; Mission Mode's own options, its players (place, character, name, where in the menu) and its notice in the top band | `host/screens/overhaul_camp_screen.c` (in `camp_screen.c`'s place) | `layouts/campstory/` |
| The Panels page, the story's and Mission Mode's (the same grid menu, ov025's and ov008's): the DS's lower screen as a card, the status card X swaps in; a panel held, the stats it would give in that card (the value it would become and the change beside it, the game's own preview: `Ov025/Ov008_PreviewDropSummary`), left off the DS's upper screen | the same; `runtime/menus/camp.c` `read_panels`, `khdays_camp_field` | `layouts/campstory/` |
| Files: Roxas's Diary and Enemy Profiles, with a 3D model viewer (the enemies the game draws as several models assembled as their overlays assemble them: Darkside's tentacles, the bosses' wings and swords, the helpers' weapons in the hands) | the same; `runtime/menus/enemy_viewer.c`, `enemy_viewer_anims.h` | `layouts/files/` |
| The camp's save page in KH2's save list, with each save's world; the title's LOAD list in its look; its SAVE, LOAD and MUNNY in KH2's words of the game's language | the same; `runtime/menus/saveworld.c` | `layouts/campsave/` |
| Mission Mode's camp menu without the story's pieces (`layouts/campstory/`): DDD's camp look, the menu's touches and its Preferences sliding in without black | the same | `layouts/camp/` |
| The Moogle Shop on one screen | `host/screens/shop_screen.c`, `runtime/menus/shop.c` | `layouts/shop/` |
| The day's card, as the theater's between chapters, its pace measured on the collection's movie | `host/screens/day_screen.c`, `runtime/menus/daycard.c` | `layouts/day/` |
| A pause over the HD movies, the scenes' videos and the days' monologues (Continue; Skip, at once), the game's pause and game over in KH2's look | `host/screens/modern_pause.c`, `host/screens/kh2_seq.cpp` (the KH2 HUD's SEQD reader), `ui/pause.cpp`, `runtime/movies/movie_hd.c` | `layouts/kh2pause/` (else `layouts/pause/`) |
| A day's monologue in the theater's voice with its subtitles, its words left out | `runtime/movies/scene_trace.c`, `host/app/window.c` | the HD PACK's subtitles |
| The menus' sounds in the port's own screens (the game's SE_SYS) | `host/settings/settings_sample.cpp` | `menu_sounds/se_sys_*.wav` (optional) |
| KH2's glove sway and firefly, the buttons' icons one size on every screen | `host/screens/layout_screen.c` | `layouts/kh2pause/firefly.png` |
| The camps' cursors kept between visits, a save's list going back to its slots after saving | `runtime/menus/camp.c`, `runtime/menus/savelist.c` | -- |
| The room change's iris in HD (KH2's irises, the two worlds KH2 has not drawn) | `runtime/display/field_wipe.c`, `host/render/gpu_present.c` | `graphics/ba/tr/` |

The DS's screens the shop, the Files page and the Panels page float are composed again on the
graphics card with the HD tiles and fonts (`host/render/gfx_render.h` `khdays_gfx_render_shop`), the
CPU's composition kept for finding the shop's stripes and as the fallback. The game's functions it
needs to hear from are hooked in `port/tools/prepare.py` (the Menu Overhaul's `HOOKS`); each port
function they call answers as the game would without the mod.

## Making the mod

The installer makes it with the HD menus, from the 1.5+2.5 collection, and puts it inside the HD PACK with the
Walk (docs/WALK_MOD.md): one mod for the player. Its files go over the HD menus' (the 200 or so of the camp and
the title it retouches), the pieces they replace kept in `setup/hd-originals` and the install's state saying
which; before the HD menus or these mods are made again they are put back, so each is made from the HD tools'
own pieces. A developer runs the installer's helper on an install whose HD menus are made, with KHGummi.otf and
KHMenu.otf in `installer/fonts` (the installer's build takes them out of the package `installer/pinned.json`
pins); the mods not named are taken out of the HD PACK:

```powershell
py .\installer\setup_steps.py build-mods "<install>" --menu-overhaul --walk --1525 "<1.5+2.5>"
```

It extracts the collections' entries `installer/mod_inputs.txt` lists (KH2's menus and fields' irises, Birth by
Sleep's camp, Re:Chain of Memories' Moogle Shop, the theater's day card) as the HD menus' are extracted, and runs
`tools/mods/menu_overhaul.py`, whose pieces (`tools/mods/overhaul/<piece>.py`, each `make(inputs, mod)`) read:

- the extracted collections' entries (none of 2.8's: the camp's and the title's pieces start from the HD menus');
- the game's own data (`data/extracted/<hash>`): its menus' texts and pictures, its world models and logos, its
  panels, its calendar; the menu's cues rendered by the port itself (`khdays-native --dump-sounds DIR NAME...`);
- the HD menus' pieces (`mods/HD PACK`, `tools/hd`'s), for the camp and the title the Menu Overhaul retouches;
- Televo's Kingdom Hearts Re:Collection fonts, which the installer carries (`installer/fonts`, pinned in
  `installer/pinned.json`);
- the pictures made for the mod by its author (`tools/mods/overhaul/art/`: buttons, the shop's block, the
  pattern's tile...), the only pictures in the repository.

`installer/setup_steps.py trace-mod-inputs "<install>" --src <extractions> --1525 "<1.5+2.5>" [--fonts DIR]`
writes `mod_inputs.txt` again from the tools as they are (`installer/tests/mod_inputs_test.py` fails when they
changed since). `tools/mods/tests/menu_overhaul_test.py` checks the pieces' wiring without any game data.

## How it reads the game

What the port reads of the game's own screens to draw the Overhaul's, as the decompilation shows it.

### The enemy viewer

The Enemy Profiles' model viewer: the enemy's model as the game loads it in battle, drawn by the game's own 3D
(NNS G3D, all in the main ARM9 code, loaded whatever the overlay) while its Files page runs -- the camp menu
draws Roxas so (Ov025_ModelActor_Init, Ov025_Menu_RenderScenePanels).

The model: Ms/<class>.p (a P2 archive; the class the enm_&.z record's first byte, as the enemies' overlays
open it: Ov114_CreateNamedEntity's "Ms/%02x.p"), its entry 0 a KAPH pack -- the model (section 7) and, for
most, its joint animations (section 0); for the others each later entry that holds a BCA alone is one
animation. An entry is opened by a handle on the archive's header (Msg_OpenContainerAndReadHeader,
Ov107_PackTextureHandle's packing) through RegisterSeqAndInit (the model instance: textures to VRAM, the
render object, the animations' table at +0xe0) or Snd_RegisterSeqAndBind (an animation's file into a table of
its own); BindAnimTrack plays one, Sequence_UpdateTracks steps it, Scene_DrawNode draws the instance on its
matrix (+0x80, a 4x3) with the camera Camera_CommitMatrices made (a block Projection_LoadDefaults fills),
between main's G3X_ResetMtxStack and its swap.

The page has no texture VRAM (Ov025_Reports_SetupDisplay: banks A-D its BGs and OBJs) and hides the 3D layer.
The port shows neither of the main engine's (the lower screen's) BG and OBJ there, so while the viewer is open
bank B (the main OBJ's) holds the textures and F+G their palettes, as the camp's menu has them
(Ov025_Hub_SetupGraphics), BG0 shows the 3D; on closing, all as it was (bank B's contents too).
host/render/onescreen.c takes the 3D's polygons into the window.

Some enemies are several models the game draws at one place (Darkside's DS01 with DS02 and DS03 on the actor's
matrix, Ov244_TickAndSyncTwoModelXforms; the bosses' wings, swords, arms, tails): their parts
(enemy_viewer_anims.h's VIEWER_PARTS) are instances of their own from the same archive, each with the
animation of the body's number, on the body's matrix.

(`port/runtime/menus/enemy_viewer.c`)

### The Moogle Shop

The story's shop is ov026 (a sub-scene of the field, Ov026_FillSceneHooks), Mission Mode's the same code in ov008
(each function 0x2eb0 lower, the same context). The shop object's update is its step (no mode of its own to
read): ShopTabSelectTick the menu, ShopListInput a list, and so on (the decomp's ov026_shop). Its context
(Ov026_ShopCreate: 0xc608 bytes):

- +0xc250 the list's tab: 0 Buy, 1 Sell, 2 Synthesize, 3 Redeem (copied from the cursor on entering)
- +0xc31c the munny row's change shown (s32), +0xc320 the heart points'
- +0xc3b0 the menu's cursor (0-4)
- +0xc4e4 the quantity dialog's stage (s16): 0 the count, 1 OK/Cancel; +0xc4e7 (u8) 0 OK, 1 Cancel
- +0xc51c the recipe panel up (Synthesize: the moogle not drawn)
- +0xc534 the synthesis question's stage (s16): 0 asked, 1 done, 2 only one allowed; +0xc53c 0 Yes, 1 No
- +0xc5f0 Synthesize offered, +0xc5f4 Redeem offered
- +0xbff0 the moogle's model instance (mi/ch/70.z): +0x00 its flags (0x20: its rotation made again from its
  angles at the next draw), +0x7c its turn about Y (u16, 0x10000 a whole turn)

The money: the game state's +0x8 (munny) and +0xc (heart points), as the camp reads them.

(`port/runtime/menus/shop.c`)

### The day's card

Read while its object runs (Ov004_StepSceneFrame). Its context (data_ov004_02051384, the object's heap,
Ov004_CreateSceneObjects):

- +0xaf8 the phase
- +0x5558 the day it starts on and +0x555c the one it rolls to (0x190 both made 0xff: the prologue's day 255)
- +0x5568 the rolling value (20.12 fixed point), +0x5570 the rolling's step (16.16 of the whole a frame,
  Ov004_FadeInTransition's), +0x5574 the rolling done, +0x5578 the rolling's progress
- +0x5598 the day whose title it draws (Ov004_PrepareTransitionLabel, set before 0x190 became 0xff: 400 the
  prologue's)

The port's card has its titles by that day (tools/hd/theater_day.py). More: docs/RUNTIME_MAP.md, scene 5.

(`port/runtime/menus/daycard.c`)

### The Files pages

Roxas's Diary and Enemy Profiles are the story camp's screen 7, one page in two modes (Ov025_GoToPage7A / 7B: the
context's word 0x9768, 0 the diary, 1 the enemies). Its state (the screen's, page A: Ov025_Reports_InitPage,
0x278 bytes): s16 the first row shown (+0), the row chosen (+2), the rows (+4); A held (+0xc0: the cursor's
row shows its other version); running (+0xc8, 1 from InitPage to Reports_Leave); the entries (+0x1e8: 0x40
bytes each, Reports_LoadEntries -- u16 its id, the diary's day, in bits 0-8 and bits 10-15 set while it is
locked; +2 its title's length; +0xc its other version or 0; +0x10 its title, UTF-16); the entries viewed on
this visit and those seen before (+0x238, +0x248: bits, the first the highest of the first word;
Ov025_QueryItemFlags -- the NEW badge while not seen before, dimmed once viewed). A diary row with another
version is viewed as entry + 0x40 (its other version's). A locked enemy's name is the game's "?" (the
overlay's string 0x020b5270).

(`port/runtime/menus/camp.c`)

### The Panels page

The story camp's screen 2 (Ov025's grid menu: page A its grid's context, Ov008MenuContext -- the grid and the
list on the lower screen; page B the status page on the upper one, Ov025_TickMenuState's). Shown by the port
once page B is up (its gate 0x020b575c, Ov025_TickMenuState's step 1); not while table B shows another page
there (4: the first visit's tutorial). While page B sets up (its step, +0x18, short of 7: its slide's handler,
Ov025_TickPageBTransition, not running yet) its upper screen shows the list as set up (its layers not
scrolled, its sprites where they are), the numbers chosen at step 3 and slid to at once when the handler
starts: the view is the numbers' then, and `panels_early` once its words are drawn (steps 1-2), for the port
to take the list from those pictures (the page opening, before X shows it). Page B's view (+0x44: 1 the
numbers, 0 the list of what the panels give) and its slide between the two (+0x38, in dots:
Ov025_TickPageBTransition's sub BG scroll, a 100 ms tween to 192 or 0 -- Ov025_ChangeMenuSelection). The lower
screen's line (its box: row 0 of the grid's text panel, Ov025_RepaintTextRow -- the cell's panel's
description, the list's row's, a count) as the game wrote it last (prepare.py's hook), copied then: the
description's record goes away with the next.

(`port/runtime/menus/camp.c`)

### The Files pages' NEW badges

An entry is new as its page would badge it (Reports_RefreshRowEntries: an entry's bit
clear in the seen words, which Reports_Teardown keeps in the game state's fields 0x45b (the diary) and 0x3c6a
(the enemies), eight 16-bit words 0x10 apart, the first bit the highest of the first two words' u32 --
BitArray_TestBit). Its rows as Reports_LoadEntries / CountUnlocked make them from the page's table
(UI/cm/str/rpt_&.z, enm_&.z: a u32 count, then 0x14-byte records whose first halfword holds the diary's day in
bits 0-8; an enemy's id is the record's first byte): the diary's entries from the first while each one's day
is before the story's (359 in the last chapter, field 0x44e), the next one's not after it, and the table has
not started again (its second half, the rewritten versions); an entry with its rewritten version linked (the
last chapter's, by its missions: the page's own reckoning) is viewed as entry + 0x40 -- known from the page
once it was open, else counted new only while neither is seen. The enemies: all the table's, new once met (the
game state's byte 0xa11 + id not 0: Ov025_IsFlagRowFree).

(`port/runtime/menus/camp.c`)
