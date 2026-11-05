# Scoped HD font mappings

Numeric placement corrections use a separate optional [`fonts/layouts.json`](HD_FONTS.md#optional-numeric-layout-corrections).
Font routing alone does not enable those corrections.

Place `fonts/mappings.json` in an enabled mod, for example
`mods/HD2/fonts/mappings.json`. Restart after edits. The highest-priority enabled
mod providing the file wins; mapping files are not merged. The HD PACK generator
supplies its documented defaults. Other font mods opt in by providing this file;
without one, existing font behavior is unchanged.

```json
{
  "version": 1,
  "scopes": {
    "camp": {
      "text/font_eu_10.nftr": "text/font_eu_10s.nftr"
    },
    "camp.story.screen.2": {
      "text/font_eu_10.nftr": "text/camp_panels.nftr",
      "text/font_eu_10s.nftr": "text/camp_panels.nftr"
    },
    "camp.story.screen.7.mode.0": {
      "text/font_eu_10.nftr": "text/camp_diary.nftr"
    },
    "camp.save": {
      "text/font_eu_10.nftr": "text/font_eu_10.nftr"
    }
  }
}
```

Each key is the original resource requested by the game; each value selects an
HD font. For `text/camp_panels.nftr`, provide
`fonts/text/camp_panels.nftr.png` and `.json`, with JSON
`"source": "text/camp_panels.nftr"`. A binary NFTR for the alternate is unnecessary.
Its artwork, scale, baseline, metrics, and kerning are used unchanged.

This routes **HD selection**, rather than modifying archive requests or globally
rebinding NFTR resources. Original requested Days data remains authoritative for
character identity and native fallback. Dialogue retains its usual font.
The alternate's `charmap` is required: its array indices cannot safely be assumed
to match another NFTR. Missing characters try the secondary PNG atlas described
in [HD fonts](HD_FONTS.md#per-glyph-and-secondary-fallback), then the originally
requested Days artwork and metrics. Invalid/missing targets retain the original
binding, including its ordinary HD replacement if available, and log a diagnostic.

## Scopes

The most specific matching source rule wins, falling back through dot-separated
parents independently for each source. For `camp.story.screen.2`, lookup checks
that scope, `camp.story.screen`, `camp.story`, then `camp`. Mapping a source to
itself cancels an inherited rule. Mappings do not chain (A → B does not apply B → C).

| Scope | Applies to |
| --- | --- |
| `camp` | Camp family, including standalone save |
| `camp.story` | Story camp (ov025) |
| `camp.mission` | Mission Mode camp (ov008) |
| `camp.story.screen.N` | One story camp screen class |
| `camp.mission.screen.N` | One Mission Mode camp screen class |
| `camp.save` | Standalone camp save (ov009), excluding title-screen load |
| `field.tutorial` | Field tutorial text, during initial drawing and page turns |
| `field.dialogue.speaker` | Cutscene portrait nameplate, including measurement and narrow-font choice; dialogue is separate |
| `field.hud.information` | INFORMATION message: initial crawl, skip, restoration and retained line |
| `field.hud.status` | Shared day/status panel, mission objective/progress text and replay caption |
| `field.hud.area` | Area name, including its measurement and narrow-font choice |
| `field.command` | Command labels and their Magic/Items lists, including fitting and shadows |
| `field.obtained` | Item name in the small gameplay OBTAINED banner, independently of the Items list |
| `shop` | Shop names, lists, descriptions, quantities, dialogs and measured detail headings |
| `mission.results` | Mission results: summary/status and reward pages |
| `mission.results.summary` | Result title and status text, including abort/completion |
| `mission.results.rewards` | Reward headings, item names/counts, descriptions and prompts |

| N | Screen handler class / typical page |
| --- | --- |
| 0 | Root/options (`SetupScene`) |
| 1 | Mission list (`WorldMap_StateTick`) |
| 2 | Panels/grid list |
| 3 | Transition |
| 4 | Save (`SaveMenuInitStep`) |
| 5 | Preferences/config |
| 6 | Tutorials |
| 7 | Reports/diary and enemy profiles |

For screen 7, suffix `.mode.0` selects reports/diary, `.mode.1` enemy profiles.
An unset screen index ends in `screen.-1` and still inherits camp defaults.
Auxiliary/detail handlers inherit the current screen's choice when using its
four font handles: `text/font_eu_08.nftr`, `text/font_eu_10.nftr`,
`text/font_eu_08s.nftr`, `text/font_eu_10s.nftr`. Map each desired role explicitly.

Field tutorials use shared field fonts, so they have a separate scope. Add this
entry alongside `camp` and its existing overrides in `scopes`:

```json
"field.tutorial": {
  "text/font_eu_10.nftr": "text/font_eu_10all.nftr",
  "text/font_eu_10s.nftr": "text/font_eu_10all.nftr"
}
```

Camp tutorials keep their existing camp mappings. Field dialogue and other field
text retain their usual fonts, even while a tutorial is open.

`KHDAYS_TRACE_TEXT=1` logs scope/binding changes. Startup diagnostics report
configuration and selected targets. Pre-rendered graphics, the PC menu's host
fonts, separate camp handles outside these four slots, and other field text
are outside this implementation. Field HUD and results text are covered below.

## Mission selection elements

Mission-selection text supports additional suffixes on the current camp screen
scope. For story mode's world/mission selector these are:

| Scope | Text element |
| --- | --- |
| `camp.story.screen.1.mission_select.instruction` | Upper-right instruction, such as Select a mission |
| `camp.story.screen.1.mission_select.world` | Lower-left selected world name |
| `camp.story.screen.1.mission_select.header` | Red mission header: number, name, subtitle, counts |
| `camp.story.screen.1.mission_select.summary` | Summary body, including its MISSION heading |
| `camp.story.screen.1.mission_select.objectives` | Objectives body |
| `camp.story.screen.1.mission_select.rewards` | Rewards body |
| `camp.story.screen.1.mission_select.buttons` | Embark/Back button prompts |

Use `camp.mission` instead of `camp.story` for Mission Mode. Elements inherit
the existing screen, mode, and `camp` rules independently; a header rule never
spills into a nested body draw. Only configure the elements you want to change.
The tab labels themselves and other lettering baked into artwork remain graphics.

To keep a 10all camp default but use the normal 10 replacement for Summary,
add this entry inside `scopes`:

```json
"camp.story.screen.1.mission_select.summary": {
  "text/font_eu_10.nftr": "text/font_eu_10.nftr",
  "text/font_eu_10s.nftr": "text/font_eu_10.nftr"
}
```

Objectives and Rewards have separate original canvases/drawing functions, so
use their separate scope keys when they should change too. Their initial text
is captured separately even before those tabs are selected. Switching tabs only
publishes the corresponding canvas; it does not change another tab's font.

## Field HUD and mission results

HUD overrides apply only while the corresponding draw/measurement operation runs.
They do not change dialogue, tutorials, camp choices, or the shared field fonts
globally. Initial drawing, the INFORMATION crawl and skip, returning from a held
panel, asynchronous area/status graphics loads, and later status updates all use
the same scopes. Captured glyphs retain their chosen font after the draw returns.

The day line and the mission's objective/progress text use the same status-panel
surfaces and are intentionally one `field.hud.status` setting. The area name and
INFORMATION message are independent. Mission completion and abort results share
the same result drawing paths, so a `mission.results` parent rule covers both.
Use its `.summary` and `.rewards` children if those pages need different fonts.

For example, add these entries to `scopes` to use 10all for all these elements:

```json
"field.hud.information": {
  "text/font_eu_10.nftr": "text/font_eu_10all.nftr",
  "text/font_eu_10s.nftr": "text/font_eu_10all.nftr"
},
"field.hud.status": {
  "text/font_eu_10.nftr": "text/font_eu_10all.nftr",
  "text/font_eu_10s.nftr": "text/font_eu_10all.nftr"
},
"field.hud.area": {
  "text/font_eu_10.nftr": "text/font_eu_10all.nftr",
  "text/font_eu_10s.nftr": "text/font_eu_10all.nftr"
},
"mission.results": {
  "text/font_eu_10.nftr": "text/font_eu_10all.nftr",
  "text/font_eu_10s.nftr": "text/font_eu_10all.nftr"
}
```

Artwork containing lettering, such as the static INFORMATION banner, and result
counters/ranks drawn from sprite sequences are graphics, not NFTR glyphs. Font
mappings do not replace those; use the existing texture replacement system.

## Commands, shops and obtained items

The command deck uses `font_eu_08.nftr` and its narrow `08s` alternative. To change
it, map those two resources under `field.command`. It remains unchanged when that
scope is absent. Enemy-name captions and normal dialogue are separate.

The small OBTAINED banner has its own `field.obtained` scope. Its item name uses
the same original 08/08s sources, even though the new target can be 10all. The
game normally aliases the Items list's prepared tiles; the port captures a
private 64x16 text surface when that banner changes. It runs the existing text
layout and NFTR renderer with the popup's scope, retaining independent advances,
fitting, shadows and original-glyph fallback. No native character VRAM is
overwritten. The OBTAINED heading itself is pre-rendered artwork.
Different fitting rules in `fonts/layouts.json` also select the private surface
when the font choices match, including when no mappings file is installed.

Shop routing covers both the standalone shop and Mission Mode's embedded shop.
Its original fonts are 10, 08 and 08s; map all three for a consistent replacement.
Only add these two new entries to use 10all in shops and pickup banners:

```json
"shop": {
  "text/font_eu_10.nftr": "text/font_eu_10all.nftr",
  "text/font_eu_08.nftr": "text/font_eu_10all.nftr",
  "text/font_eu_08s.nftr": "text/font_eu_10all.nftr"
},
"field.obtained": {
  "text/font_eu_08.nftr": "text/font_eu_10all.nftr",
  "text/font_eu_08s.nftr": "text/font_eu_10all.nftr"
}
```

The private popup reuses one native-resolution plane and one bounded RGBA plane
(256 KiB at 8x), with the existing glyph raster cache. Stable frames do not redraw
or upload it. Its tilemap entries, source ownership and VRAM writes determine
eligibility; clearing or overwriting the item tiles removes it. Scrolling and
palette/flip handling use the existing canvas renderer. Allocation failure or a
conflicting ordinary PNG mapping leaves the original rendering in charge.

## Design and provenance

Verified against decompilation commit `a2b0478b87e3102732094c34f6ca89595f98ab93`:

- `Ov008/009/025_PushCannedDescriptors` loads four 12-byte font handles at context
  offsets `0x9680`, `0x968c`, `0x9698`, `0x96a4` through `Resource_BindByName`.
  Pages reuse these handles; font selection cannot be scoped solely at loading.
- `Resource_BindByName` loads the NFTR and initializes its handle. Font getters
  return those same handles. A shared NFTR pointer cannot identify a menu owner.
- `Ov025_UpdateCampaignMenuManager` sets handler A before initial page drawing.
  The port already reads the corresponding screen globals in `camp.c`.
- Camp hook-slot tables define the screen classes. `Ov025_Reports_InitPage` and
  `Ov025_ReportDetail_Setup` read report mode at context `+0x9768`.
- The standalone save context is the second word of `data_ov009_020563e4`.

The port's scope provider checks exact handle identity in loaded camp contexts.
`ui/text.c` selects/binds the route before returning a font identity to existing
measurement, kerning, baseline, and capture APIs. Choices are memoized until scope
changes. Captured glyph operations keep that identity during later scrolling,
partial clears, canvas copying, VRAM publication, and compositing.

Routed views share immutable PNG pixels and raster-cache entries, with separate
CMAP/native-fallback metadata per (original source, alternate target). Views are
reused across scopes, as many as the mappings name. Without mappings,
layout/drawing skips scope lookup. No bitmap comparison or forced redraw is added.
Other contexts can provide scopes through the same selection API in future.

Field tutorial text uses `Ov002_LinkPageRefresh`, called initially from
`Ov002_TakePageIntoSubObject` and on page turns from `Ov002_ClosePageIfAllowed`.
It measures/draws title, footer/page counter, body, and optional extra records.
The port's `prepare.py` around hook enters `field.tutorial` for that synchronous
operation and restores the previous scope afterward, including nested calls.
Checking whether a tutorial is merely open would incorrectly reroute unrelated
field text sharing the same font handles. Captured glyphs retain their route
after the scope ends. One generated-source hook was added; no decompilation changes.

Mission-element hooks cover both ov008 and ov025 variants. The original
`DrawMissionDetail` calls `MissionMenu_DrawMissionText`, `MissionMenu_DrawInfo`,
and `DrawMissionInfoText`, which populate the three canvases at menu `+0xc`
(targets `+0x138`, `+0x13c`, `+0x140`). `SwitchMenuTab` selects among them.
The outer draw's name/rank/count surfaces form the red header; its hint surface
at `+0xfc` is scoped separately for button prompts. `DrawMissionSummaryHeading`
handles upper-right instructions. `WorldMap_UpdateSelectionText` handles the
world caption, including its fitting/measurement. `WorldMap_SetupTextSurfaces`
scopes those same initial instruction/world draws individually. This covers
initial construction, selection changes, and redraws, rather than identifying
strings, screen rectangles, or particular missions.

The operation scope stack is bounded at 16 entries, restores enclosing scopes,
and leaves each captured glyph's final font identity unchanged during later
compositing. Mission scopes extend the active camp screen scope; verified
mission draw functions can also resolve that scope for fonts outside the four
common handles. No extra rasterization or per-frame redraw was introduced.

Field HUD/result hooks at the same decompilation revision:

- `Ov002_BeginTextCrawl` measures the INFORMATION line; `StepTextCrawl` draws
  timed/skipped characters. `HoldPanelScreen` reconstructs the reached prefix
  when returning from a hold. `SetCaptionText` draws/measures the retained line;
  `BuildRunSummary` adds replay summary text on a sibling canvas.
- `Ov002_DrawLoadedCaption` draws the area string cached at caption-context
  `+0xb4`, including a font_eu_10s fitting test at 120 logical pixels.
- `Ov002_DrawLoadedPortrait` fills the status canvas at `+0xfc`, formatting the
  current day in the wide-layout branch or drawing the stored `+0xb8` string.
  `DrawMessageCaption` refreshes its replay variant. `ShowNoticeText` populates
  the notice on that surface; `DrawLoadedTally` writes range/progress text into
  the adjacent `+0x138` canvas. Both use `field.hud.status`.
- ov005 owns two result contexts. `UpdateResultLabels` draws the result status
  and configured title. `RenderTextSurface` handles reward headings, names,
  quantities, descriptions and prompts, including font selection based on text
  width. Both contexts load font_eu_10; the rewards context also loads 10s.
  `DrawResultNumber` instead selects sprite sequences and has no font to map.

These are operation scopes, not per-frame renderer callbacks. They use the
existing scope stack and route cache without changing canvas tracking, VRAM
ownership, compositing, glyph rasterization or font metrics.

Additional paths at the same decompilation revision:

- `Ov002_DrawPanelRowText` prepares the command labels, spells and item names
  using 08/08s. `BuildPanelLabels` instead draws enemy-name captions and is not
  part of this scope.
- `Ov022_ApplyItemEffect` calls `Ov002_AnnounceWithSound`, then
  `AnnounceSelection` and `PanelDrawSlotCursor`. The last writes sixteen cached
  item-name tiles starting at `0x250 + slot * 16` into BG1 map rows 8/9. Its
  nonzero shift selects the pickup banner at column 1; the unshifted item-use
  variant retains command rendering. Slot keys are at command context `+0x4b8`,
  stride 12. The private capture reads that same key/name and fits it with
  `PickTextFitting64` before calling the existing `Text_DrawWithShadow`.
- `Ov008_InitShopSurfaces` and `Ov026_InitShopSurfaces` bind three 12-byte font
  handles at context `+0xc13c`, `+0xc148`, `+0xc154`. Their context globals are
  `data_ov008_02090fac` and `data_ov026_02091368`. Exact loaded-owner checks cover
  every draw and measurement, including the embedded shop. The standalone shop
  also scopes its string, fixed-number, description and detail-layout helpers.

## Verification

`ctest -R khdays-font-` in the port's build (the `port/build.py --asan` tree runs them under
AddressSanitizer). Synthetic tests cover the actual scope provider's four handles,
all screen indices, both camp variants, save/report modes, unrelated or released
handles; inherited/page rules, identity cancellation, dialogue isolation, missing
targets, invalid config, repeated cached lookup, shared artwork with different
source indices, fractional metrics, native fallback, and stable captured choices.
Field tutorial tests cover initial drawing, repeated page turns, nested scope
restoration, reuse of the exact same font handle by normal text, and restoration
of a pre-existing camp scope.
Mission tests cover all seven roles, inherited defaults for non-body elements,
explicit original-font cancellation for the three bodies, nested tutorial/mission
scope restoration, bounded stack overflow recovery, and retained captured fonts.
Run `python port/tests/font_scope_hooks_test.py` for the generator tests.
They check both overlays' twelve element wrappers and initial instruction/world
draws, including parameter comments containing commas or identifier-like words.
The generated wrapper always pops its scope after the game function returns,
including early returns inside that function.
Existing layout, clearing, scrolling, placement, and overlay tests also run.
The `khdays-font-tests` build target and `ctest -R khdays-font-` run these suites
and the generator checks through the standard port test workflow.

In-game verification remains necessary: install distinct alternates and visit
root → Panels → diary/profiles → Save → root → dialogue, including Mission Mode.
Check initial drawing, alignment, wrapping, and scrolling. Scope changes affect
new layout/draw operations; text intentionally retained without redrawing keeps
its captured choice. Editing mappings requires a restart.

HUD/results regression tests also cover each absolute scope on a shared font,
nested tutorial restoration, return to an enclosing camp scope, retained captured
identities, parent-rule inheritance, per-area cancellation, and dialogue
isolation. Generator tests validate all twelve new wrappers against their actual
source definitions, including early-return functions. In-game verification:
visit the hub day/area display, receive and skip INFORMATION messages, hold/resume
the panel via a menu, enter a mission and observe progress changes, then abort
and complete missions and inspect both results and rewards pages.

Command/shop/pickup tests cover independent cached font identities, an absent
command rule, mixed HD/native popup glyphs, stable-frame cache reuse, tilemap
clears, scrolling, ordinary PNG precedence, identical-byte VRAM overwrites,
republishing and invalid slots. Shop-owner tests cover both overlays' three exact
handles, unloaded owners and unrelated handles. In-game checks: change all three
command selections and their sublists, buy/sell/synthesize and open quantity and
detail dialogs, collect different/repeated items, then pause/resume or change
areas while a pickup banner is visible. Check these in Story and Mission Mode.

`field.dialogue.speaker` covers only the portrait-label font binding, width
measurement and two shadow passes in `Ov002_SceneApplyPanelRequest`, at decomp
revision `a2b0478b87e3102732094c34f6ca89595f98ab93`. It ends before
`Ov002_SceneLayoutPanelWindow`; dialogue lines in `Ov002_SceneDrawEntryLines`
remain outside the scope. The label's existing scratch-to-portrait copy retains
the captured font choice. HD PACK maps both `10` and `10s` to `10all` here.
Tests check these hook boundaries and shared-handle restoration. Verify short
and long names in cutscenes, including narrow-font selection and unchanged dialogue.
