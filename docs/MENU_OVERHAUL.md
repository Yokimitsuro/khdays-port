# The Menu Overhaul

A mod that redraws Days's menus in the look of the Kingdom Hearts HD collections, on one screen.
The port carries its code; the mod's folder carries its pieces, made from the player's own copies of
the collections, so none of them is in this repository. Without the folder nothing of it runs: the
port's screens, pauses, sounds and timings stay as they are.

## Turning it on

A folder of `mods/` holding `days-mods/menu-overhaul.txt` (any contents) turns it on
(`port/runtime/mods/installed.h`, looked for once at the start). The folder's name decides which
mod's files win, as for every mod: one named before `HD PACK` (`Days - Menu Overhaul`) overrides its
pieces. Every feature below also needs the screens on one picture (`screens = one`).

## What it changes

| Part | Code | Pieces |
|---|---|---|
| The camp menu, the story's and Mission Mode's: KH2's command buttons, Birth by Sleep's plates, the character's model and numbers, the panels' grid; Mission Mode's own options, its players (place, character, name, where in the menu) and its notice in the top band | `host/screens/overhaul_camp_screen.c` (in `camp_screen.c`'s place) | `layouts/campstory/` |
| The Panels page, the story's and Mission Mode's (the same grid menu, ov025's and ov008's): the DS's lower screen as a card, the status card X swaps in; a panel held, the stats it would give in that card (the value it would become and the change beside it, the game's own preview: `Ov025/Ov008_PreviewDropSummary`), left off the DS's upper screen | the same; `runtime/menus/camp.c` `read_panels`, `khdays_camp_field` | `layouts/campstory/` |
| Files: Roxas's Diary and Enemy Profiles, with a 3D model viewer (the enemies the game draws as several models assembled as their overlays assemble them: Darkside's tentacles, the bosses' wings and swords, the helpers' weapons in the hands) | the same; `runtime/menus/enemy_viewer.c`, `enemy_viewer_anims.h` | `layouts/files/` |
| The camp's save page in KH2's save list, with each save's world; the title's LOAD list in its look | the same; `runtime/menus/saveworld.c` | `layouts/campsave/` |
| Mission Mode's camp menu without the story's pieces (`layouts/campstory/`): DDD's camp look, the menu's touches and its Preferences sliding in without black | the same | `layouts/camp/` |
| The Moogle Shop on one screen | `host/screens/shop_screen.c`, `runtime/menus/shop.c` | `layouts/shop/` |
| The day's card, as the theater's between chapters, its pace measured on the collection's movie | `host/screens/day_screen.c`, `runtime/menus/daycard.c` | `layouts/day/` |
| A pause over the HD movies, the scenes' videos and the days' monologues (Continue; Skip, at once), the game's pause and game over in KH2's look | `host/screens/modern_pause.c`, `host/screens/kh2_seq.cpp` (the KH2 HUD's SEQD reader), `ui/pause.cpp`, `runtime/movies/movie_hd.c` | `layouts/kh2pause/` (else `layouts/pause/`) |
| A day's monologue in the theater's voice with its subtitles, its words left out | `runtime/movies/scene_trace.c`, `host/app/window.c` | the HD PACK's subtitles |
| The menus' sounds in the port's own screens (the game's SE_SYS) | `host/settings/settings_sample.cpp` | `menu_sounds/se_sys_*.wav` (optional) |
| KH2's glove sway and firefly, the buttons' icons one size on every screen | `host/screens/layout_screen.c` | `layouts/kh2pause/firefly.png` |
| The camps' cursors kept between visits, a save's list going back to its slots after saving | `runtime/menus/camp.c`, `runtime/menus/savelist.c` | -- |

The DS's screens the shop, the Files page and the Panels page float are composed again on the
graphics card with the HD tiles and fonts (`host/render/gfx_render.h` `khdays_gfx_render_shop`), the
CPU's composition kept for finding the shop's stripes and as the fallback. The game's functions it
needs to hear from are hooked in `port/tools/prepare.py` (the Menu Overhaul's `HOOKS`); each port
function they call answers as the game would without the mod.
