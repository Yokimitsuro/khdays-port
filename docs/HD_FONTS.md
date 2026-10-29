# HD font rendering

PNG fonts replace NFTR artwork through the existing mod loader. Menu-specific
font choices are documented in [Scoped HD font mappings](FONT_MAPPING.md).
No new third-party dependency is required.

## Optional numeric layout corrections

`fonts/layouts.json` opts into corrections for numeric fields originally drawn
in fixed DS columns. It is separate from `fonts/mappings.json`, which chooses
fonts. Missing files, unmatched scopes and `"original"` retain the game's layout;
loading a replacement PNG alone does not enable a correction. The highest-priority
mod supplying this file wins, as with font mappings. Restart after editing it.

```json
{
  "version": 1,
  "scopes": {
    "camp.panels.inventory.counter": "font_metrics",
    "title.load.play_time": "font_metrics",
    "camp.save.play_time": "font_metrics"
  }
}
```

These scopes cover panel inventory counts in Story and Mission Mode, title Load
times, and camp/standalone Save times respectively. Dot-separated numeric parent rules
are inherited; a more specific `"original"` cancels an inherited opt-in. Unknown
scopes are inert, allowing future corrections without changing this format.
Invalid files log a diagnostic and keep original layouts. No active replacement
also keeps the original path, even with `"font_metrics"` configured.

The HD PACK generator writes these three opt-ins for its proportional KH2 fonts.
A faithful HD Days recreation can omit the file or use `"original"`. The corrected
counter treats both numbers and the slash as one right-aligned run, retaining
their palette and shadow differences. Time fields become one `HH:MM:SS` run,
using the game's separator and character spacing, with a common right edge.
Both use fractional advances, bearings and kerning through the existing glyph
renderer. Native fallback still uses native artwork/metrics and pixel positions.
Other fields, including the Stats page's already-correct Slots string, are unchanged.

Configuration is resolved once per session. Runs use bounded stack arrays and
existing glyph capture, dirty tiles, raster caches and canvas transfers; there
are no new frame updates, full-surface redraws or texture allocations.

Provenance: `Obj_ForwardInnerPayload`, `Text_DrawDirectional_2`, the Ov008/Ov025
`DrawRowCountDigits` and `DrawSaveSlotTexts`, Ov000 `DrawSaveSlotText`, and Ov009
`SaveMenu_DrawSummaries`, inspected at decomp commit
`a2b0478b87e3102732094c34f6ca89595f98ab93`. Counter columns step by four while
subtracting each digit's advance; the previous port hook substituted a variable
HD advance into that fixed grid. Time numbers and separators have independent
alignment anchors designed for narrow native glyphs. Opted-in runs intentionally
replace these horizontal layouts, retaining their original surfaces, row origins,
title extra-entry vertical shift, clears and submission calls.

Regression tests cover `30/30` versus `99/99`, three-digit hours, mixed native
separators, both time scope IDs, shadow colors, exact fractional positions,
configuration validation/inheritance and cached lookup. In-game verification:
compare Load, Save and scrolling Panels in Story and Mission Mode, with KH2 opt-ins,
without `layouts.json`, with a faithful Days atlas, and with fonts disabled.

## Optional item-name fitting

Two independent, exact scope entries in `fonts/layouts.json` can shrink names
horizontally when their complete rendered bounds exceed the configured width:

```json
{
  "version": 1,
  "scopes": {
    "field.command.items.name": { "mode": "fit", "max_width": 61 },
    "field.obtained.name": { "mode": "fit", "max_width": 64 }
  }
}
```

Merge these entries with any numeric rules. Missing entries, `"original"`, no
file, disabled HD text or no replacement font retain the existing rendering.
Fit rules do not inherit from parent scopes: configuring Items cannot affect
OBTAINED. HD PACK generation emits these two explicit entries. The Items rule
covers both the Items list and the Use/Discard header; those views reuse the same
cached item-name tiles. OBTAINED keeps its independent capture and font mapping.

Widths are fractional DS pixels, between 0.25 and 64. Optional `"x_offset"` defaults
to 0 and moves a fitted run to the right; the entire configured region must remain
inside the 64-pixel strip. Fitting never enlarges or moves a name that already fits.
HD PACK uses 61 pixels from the existing text origin, with no centering offset.
The button's assembled tilemap repeats an 8-pixel middle tile, extending the
53-pixel reference-texture interior to 61 pixels. The source strip is 64 pixels
wide; the label's indent and selected-row shift are applied by the tilemap.

The complete string is measured before canvas clipping, using cached PNG alpha
bounds, fractional advances/bearings, kerning, game character spacing and shadow.
One horizontal factor transforms every captured glyph and shadow position as well
as its artwork; font size, baseline and vertical geometry stay unchanged. PNG
alpha-bound padding is expressed in reference-atlas units, so doubling an atlas
does not change the fit geometry. The normal raster LRU and dirty-tile compositor
cache the result. There is no per-frame measurement, resampling or image allocation
for an unchanged cache. Alpha bounds are scanned once per used glyph per atlas.

The native write footprint remains unscaled for storage invalidation. Duplicate
tilemap placements cover both that footprint and the fitted artwork, including
tiles that now contain only the hidden DS text tail. Their base plane still
preserves non-text graphics; the PNG's alpha never masks original font pixels.

Mixed native fallback remains supported. Only inside a fitted run, the existing
`DrawGlyphLine` renders each native glyph into a small scratch canvas. Its palette
indices are retained inside the glyph operation's existing artwork-storage field,
then painted with the same horizontal transform and the destination palette.
This uses original DS artwork directly, never as a mask for PNG artwork. Ordinary
fallback is unchanged. The supported menu canvases are 4-bpp with native cells no
larger than 16x16; unsupported native cell sizes or writing directions, multiline
names and strings over 512 characters keep their original layout.

Different Items/OBTAINED fitting rules also create the private pickup capture when
both use the same font, including without `mappings.json`. Equal font and fitting
choices keep the shared cache. Neither configuration causes a per-frame redraw.

Provenance at the decomp revision above: `Ov002_DrawPanelRowText` caches item names
after `Ov002_PickTextFitting64` chooses the existing normal/narrow font. The port
fits only that item-name draw, leaving spell and command labels alone.
The live BG3 button rows repeat middle tile `0x0db` (normal) or `0x11b`
(selected); the assembled label area is one tile wider than the reference art.
`Ov002_PanelRepaintListRow` and `Ov002_PanelRepaintCachedEntry` reuse its tiles via
`Ov002_PanelWriteSlotLabel`. The private OBTAINED surface in
`Ov002_PanelDrawSlotCursor` explicitly selects its separate fit rule. The latter
never changes the command cache's VRAM or captured geometry.

Tests cover `Gear Component C`, unchanged `Aerial Tech+`, independent scope
eligibility, fractional advances and shadow offsets, PNG-resolution-independent
bounds, native glyphs beyond the old clip boundary, partial/full clears and
unchanged-frame image/cache reuse. Verify both menu views and the OBTAINED popup
in-game with long, short and mixed native/HD names, including empty item slots.
Duplicate-placement tests check the final lit pixel at the configured boundary
at 1x through 8x, native-tail suppression, retained graphics and stable frames
after a scale change. `KHDAYS_TRACE_TEXT=1` logs the fitted run's captured ink
bounds and horizontal factor for comparison with its visible placement.

## Generating KH2 fonts

The installer's HD menus step runs `tools/hd/kh2_fonts.py` and puts five generated
PNG/JSON pairs in `mods/HD PACK/fonts/text/`. The installer ships the generator,
not font images, original NFTRs, advance tables, or reference JSONs. It extracts
these entries from the player's own 1.5+2.5 `kh2_first` archive:

- `msg/us/fontinfo.bar`: BAR v1 directory, type-2 entries `sys` and `evt`, one
  unsigned byte per native advance. In the inspected file these are at 0x50
  (280 bytes) and 0x170 (336 bytes); the reader follows the directory instead
  of requiring those offsets.
- `msg/us/fontimage.bar`: remastered `US_fontimage_bar0.png` (SYS) and
  `US_fontimage_bar1.png` (EVT). Other sheets are not used.

Manual generation from an extraction, using the tool's existing repository or
installed layout:

```powershell
python tools/hd/kh2_fonts.py C:/extracted/kh2_first.hed_out --data data/extracted/<hash> --mod "HD PACK"
```

The Days input is the player's extracted `nitrofs/text/*.nftr`, not a prebuilt
JSON. The reader handles FINF pointers, CGLP cell counts including trailing
alignment bytes, and chained direct/table/scan CMAPs. The five European Days
fonts inspected declare UTF-16 encoding (FINF encoding byte 1). The tool refuses
other encodings rather than interpreting their codes as Unicode.

| Days resource | KH2 font | Reference cell | Scale | Baseline offset |
| --- | --- | --- | --- | --- |
| `font_eu_08.nftr`, `font_eu_08s.nftr` | SYS | 18×24 | 0.5 | 2 |
| `font_eu_10.nftr`, `font_eu_10s.nftr` | EVT | 24×32 | 0.35 | 0 |
| `font_eu_10all.nftr` | SYS | 18×24 | 0.41 | 0 |

SYS uses 28 columns, 280 slots and a 512×256 reference image. EVT uses 21 columns,
336 slots and a 512×512 reference image; its row stride and cell height are both
32, not 38. Atlas coordinates follow slot order. The collection's sheets are
twice those reference dimensions. The tool preserves their resolution and
requires a uniform integer resolution multiplier. Resolution never changes
metrics or scale. Bearings are zero, logical drawing width is the full cell,
baseline and default line feed are the native cell height. All real glyph
advances are copied exactly from BAR bytes, without prescaling or rounding.
The runtime applies the one uniform scale during fractional layout.

Space is a KH2 message command, not an ordinary fontinfo character. For
compatibility with the tested font mod, space, nonbreaking space and ideographic
space use an explicitly transparent last slot with native advances 6 (SYS) and
8 (EVT). These are chosen layout defaults, not measurements recovered from BAR.
No pair kerning is invented: fontinfo's type-2 tables contain advances only.

The international character encoding and intentional aliases were independently
expressed from the user's KH2/Days reference tables. No OpenKH implementation or
reference JSON is distributed. First occurrences of duplicate punctuation win;
fullwidth ASCII aliases use the corresponding normal glyph. Unicode Roman
numerals I–XII use individual atlas pictures, preserving ordinary Latin I/V/X.
The Days compatibility mappings use U+2165 → slot 88 and U+2167 → slot 93,
as requested after inspecting Days' numeral artwork; this does not assert how
the original game composes wider numerals. Both SYS and EVT sheets were checked:
slot 88 contains V and slot 93 contains X in each. The small Days `08` fonts
have no Roman-numeral CMAP entries; `10`, `10s` and `10all` use these mappings.
The XIII picture has no single corresponding Unicode numeral and is not
substituted for a Latin sequence. Control codes, unused slots, Japanese extension
tables and unsupported characters are not assigned fabricated mappings.

Output glyph arrays retain every native KH2 slot. The charmap includes only
supported codepoints present in each original Days NFTR. Missing characters use
the existing secondary PNG / original NFTR fallback chain with no index shifts.
Days repurposes U+FF2C as a circled L in `08`/`08s`/`10`/`10s`, and U+FF42 as
an SP symbol in `08`/`08s`. These are omitted from their KH2 charmaps and listed
in `original_codepoints`. In `10all` those codes are ordinary letters and stay
mapped. Source-specific exclusions survive scoped overrides to `10all`.
The generator also writes the approved scoped defaults to
`mods/HD PACK/fonts/mappings.json`: camp, field tutorials, bottom-screen HUD,
mission results, shops and pickup names use SYS through `font_eu_10all`;
diary/profiles and mission summary/objectives/rewards bodies keep EVT through
`font_eu_10`. Command overrides are left unset. These are routing configuration,
not extracted font data. Other mods are not changed. An installer update detects
the tool change and rebuilds HD PACK once.

Validation: `python tools/hd/tests/kh2_fonts_test.py` uses synthetic BAR/NFTR/PNG
files, including relocated tables, all three CMAP methods, compressed NFTRs,
invalid inputs, aliases, consecutive unsupported characters, space policy,
conversion alpha, repeatable output and 1×/2×/4× image resolutions.
Local development additionally compared all 616 native advances and every
supported mapping against the supplied reference using the player's files.
For in-game verification enable HD PACK, disable any higher-priority font mod,
and check dialogue, menus, shadows, button fallback and localized punctuation.

## Files and glyph identity

Keep the original resource path under the mod's `fonts/` directory:

```text
mods/<Mod>/fonts/text/font_eu_10.nftr.png
mods/<Mod>/fonts/text/font_eu_10.nftr.json
```

The graphics dump exports these pairs, including every original glyph index,
its metrics and character map. The PNG and adjacent JSON must belong to the
same mod. Font files and mappings are cached until restart.

A minimal, illustrative source-sized atlas definition is:

```json
{
  "source": "text/font_eu_10.nftr",
  "format": "NFTR",
  "atlas": {"width": 24, "height": 32},
  "cell_width": 24,
  "cell_height": 32,
  "baseline": 25,
  "max_width": 24,
  "line_feed": 32,
  "scale": 0.35,
  "baseline_offset": 0,
  "glyphs": [
    {"index": 0, "x": 0, "y": 0, "w": 24, "h": 32,
     "left": 0, "width": 18, "advance": 20}
  ],
  "charmap": [{"codepoint": 65, "glyph": 0}],
  "kerning": []
}
```

`atlas.width/height` and glyph `x/y/w/h` use reference image coordinates. The
actual PNG may be uniformly larger, for example 48x64 for this 24x32 reference.
That changes sampling quality, not logical size. Atlas layouts are arbitrary;
glyph rectangles must stay inside the reference image. PNG RGB is tinted by the
game's palette color; PNG alpha supplies all replacement coverage. An empty or
transparent glyph remains a valid replacement.

Indices must be unique and contiguous from zero. A nonempty `charmap` binds
original NFTR codepoints to replacement indices, so array order can differ from
the original. Missing codepoints fall back. An empty map preserves legacy
index-based binding; it is unsuitable for routing between different NFTRs.
Aliases of one original glyph use the first original codepoint encountered.
Duplicate replacement codepoints are rejected.

Optional `original_codepoints` lists codepoints that must use original NFTR
artwork and metrics, bypassing both PNG layers. It is useful for game symbols
stored under ordinary Unicode letter codes. Unlike an absent charmap entry,
this explicit native policy follows the requested resource through scoped font
overrides; it does not disable legitimate letters in the shared target font.
Existing JSONs without the field keep their previous behavior.

`source` must match the selected resource, including its original subtree.
Leading NitroFS slashes and slash direction are normalized. A combined font such
as `text/font_eu_10all.nftr` needs its own matching pair; the loader does not infer
that `font_eu_10` supplies its characters.

## Logical metrics and sizing

Without `scale`, metrics remain in Days units and artwork keeps the original
compatibility fit inside the Days cell. Unchanged NFTR metrics preserve the
full-cell artwork, including originally blank pixels. Editing `width` clips its
logical drawing area; it never stretches a character. Baseline adjustments are
inactive in this mode.

An explicit `scale` opts into native source-font units. Supply the source cell,
bearings, widths, advances, line feed and kerning before scaling. One uniform
factor applies to all of them. Artwork fits inside the replacement source cell,
preserving its aspect ratio; the original Days bitmap cell does not limit its
size. For example, an 18x24 cell at scale 0.5 occupies 9x12 logical pixels, with
an advance of 18 becoming 9. A 2x or 4x PNG produces the same geometry.

| Field | Meaning |
| --- | --- |
| `left` | Horizontal bearing from the character origin |
| `width` | Logical drawing width after the bearing; clips, never stretches |
| `advance` | Cursor movement before the next character |
| `cell_width`, `cell_height` | Source cell used for uniform artwork sizing |
| `line_feed` | Multiline step; the primary font controls mixed-font lines |
| `baseline` | Source baseline aligned to the original Days baseline |
| `baseline_offset` | Optional additional vertical displacement in Days units |
| `max_width`, `bpp` | Export metadata; not replacement sizing controls |

For scaled fonts, vertical placement is:

```text
artwork_y = draw_y + days_baseline - baseline * scale + baseline_offset
```

The Days baseline is the signed CGLP baseline. Positive offsets move artwork
down; negative offsets move it up. The offset defaults to zero and is not scaled
again. Horizontal and multiline alignment resolve before this displacement.

Fractional advances, bearings, baseline positions and alignment remain precise
through capture and output-resolution rasterization. Integer game APIs round
only their final result. Measurement uses the clipped drawing extent after the
bearing, with cursor advances used between characters. Compatibility mode keeps
the original advance-based measurement behavior. Intentional game character
spacing and fixed counter-column steps remain in effect.

Optional pair kerning uses codepoints:

```json
"kerning": [{"left": 65, "right": 86, "adjust": -2}]
```

**Do not add `scale` to already scaled Days-unit metrics.** Existing KH2 packs
using Days-unit values can retain compatibility mode. To use native sizing,
regenerate their definitions with native KH2 cell dimensions and advances from
`fontinfo.bar`, keeping bearings and drawing widths in the same source units,
then specify one scale. The renderer does
not change mod definitions. PNG upscaling requires no metric regeneration.

## Per-glyph and secondary fallback

Reserve an internal slot without changing any later mapping:

```json
{"index": 42, "fallback": "original"}
```

This forces the requested Days NFTR glyph, with its native metrics and pixel-grid
rendering. It bypasses all HD artwork, including a secondary atlas. Other fields
may be omitted on that entry. Never remove an internal entry and renumber its
successors to represent a missing glyph.

An optional secondary pair supplies artwork for missing primary characters:

```text
mods/<Mod>/fonts/text/font_eu_10.nftr.fallback.png
mods/<Mod>/fonts/text/font_eu_10.nftr.fallback.json
```

Resolution is primary PNG, secondary PNG, then the originally requested NFTR.
Use `"fallback": "secondary"` to reserve a primary slot for this chain. Missing
primary map/array entries also try the secondary automatically. Blank artwork
and zero widths do not trigger fallback.

The secondary uses the same schema and original `source` without `.fallback`.
It requires a nonempty codepoint map, with its own contiguous indices; it can
contain just icons or unsupported characters. Its default `"metrics": "original"`
fits artwork inside the original Days cell and retains original positioning,
width and advance. Secondary metrics, scale and baseline do not alter this mode.
Use `"metrics": "custom"` to enable its own metrics and optional uniform scale.

A secondary-only pack is supported. Missing or invalid pairs continue through
the chain. Secondary entries may force `"original"` but cannot request another
secondary. Kerning never crosses primary/secondary/DS boundaries; custom secondary
kerning applies between two secondary characters. Native fallback never acquires
HD metrics or HD suppression.

Scoped routes prefer the alternate target's secondary. If absent, they inherit
the requested source's secondary, bound by codepoint. For example, routing
`font_eu_10` to `font_eu_10all` can still use `font_eu_10.nftr.fallback.*`.
There is at most one secondary provider; final DS fallback always uses the
originally requested NFTR.

## Capture, publication and compositing

The traced source is `external/khdays-decomp` commit
`a2b0478b87e3102732094c34f6ca89595f98ab93`. Relevant symbols include
`Font_LoadUTF16`, `Resource_BindByName`, `DrawGlyphLine`, `LetterChar`,
`ClearContinuous`, `ClearAreaLine`, and `include/nnsys/g2d.h`.
All adaptations live in the port; matching sources remain unchanged.

1. Resource binding associates a font handle and FINF identity with its original
   path. RAM provenance also recovers identities after SDK initialization or
   font-object copying. Resource release invalidates bindings.
2. Port generation hooks surround the original glyph draw. Its original body
   always runs. The bitmap address identifies the glyph index; original bitmap
   samples never determine HD visibility or coverage. `NNSG2dGlyph` contains
   two 32-bit references even on x64.
3. Each canvas retains captured commands, a source observation and text-free
   graphics. HD capture leaves preceding graphics intact. Native fallback
   publishes the game's DS pixels, including unchanged-valued pixels.
4. Character transfers publish independent graphics, commands and pixel ownership
   into the displayed VRAM state. Explicit clears and memory-copy/fill hooks
   handle equal-byte writes. Partial publication and overwrites retire only
   affected command regions; full resource copies preserve command provenance.
5. BG tilemaps establish placement, palette, flips and scrolling. Display-layer
   selection checks actual tilemap use, since upload and display BGs can differ.
   Primary images retain the existing canvas/layout anchor. Other placements
   share one viewport-sized supplementary graphics/HD pair that follows it.
6. Dirty graphics and HD tiles update incrementally. Game palette colors tint
   PNG glyphs. The compositor retains logical size, clipping, layer priority and
   host layout transforms.

Text-only canvases retain their clear fill in the graphics plane. Mixed canvases
retain the graphics that preceded text, including backgrounds beneath original
ink. Both use the same explicit separation; neither is classified by its glyph
silhouette or palette. A redesigned PNG can fill originally transparent pixels
or omit originally opaque pixels without exposing DS glyphs.

Scaled artwork bounds can exceed its native DS write footprint while remaining
inside the canvas clip. Clearing and scrolling update captured clips; enlarged
artwork does not claim ownership of neighboring storage. External overwrites
inside the native write footprint conservatively retire the affected draw.

Layout transforms in `font_layout.py` and `font_fractional.py` adapt
`Text_DrawGlyph`, `Text_DrawLine`, `Text_DrawAlignedLines`,
`NNSi_G2dFontGetStringWidth`, `NNSi_G2dFontGetTextRect`,
`Ov002_StepCrawlChar`, and ov008/ov025 `DrawRowCountDigits`.
Shared measurement/draw paths use the same metrics. Crawling text keeps a bounded
fractional residual, reset when its stream or expected cursor changes.
Scope provenance and individual menu draw paths are in [FONT_MAPPING.md](FONT_MAPPING.md).

### Placement regression evidence

At the pinned revision, `Ov002_BuildTileRow` and `Ov002_DrawPanelRowText` draw
command labels into one 8x24-tile 4-bpp BG1 canvas, uploaded at character offset
`0x1a00` and then freed. `Ov002_PanelRefreshRowHeader` and
`Ov002_PanelWriteSlotLabel` reposition its strips through tilemap entries;
selection neither redraws the glyphs nor changes them to OBJ text.

For primary source row 1, destination-minus-source tile origins are:

| Selected | Talk/primary | Magic | Items |
| --- | --- | --- | --- |
| Talk | (2,16) | (1,14) | (1,14) |
| Magic | (1,16) | (2,14) | (1,14) |
| Items | (1,16) | (1,14) | (2,14) |

Each label contributes sixteen cells. A single majority-origin overlay could
reject both Magic and Items when Magic was selected, even though Items did not
move. The supplementary image covers every remaining placement; tests also
cover primary source row 2 and all three labels simultaneously.

Portrait names expose a separate case: copied text enters character memory
through BG1 but is shown on BG2. Provenance-preserving CPU copies and actual
display-layer selection retain that text after the original canvas is freed.
Initial panel names/quantities test another distinction: late graphics writes
inside enlarged PNG overhang must preserve commands whose native DS write
footprint was not overwritten. Both cases are covered without forced redraws.

## Caching, bounds and diagnostics

Canvas graphics honor the ordinary PNG tile mappings, including canvases with
no captured glyphs and tiles overwritten by partial resource uploads. PNG artwork
uses the existing RGBA plane beneath text, preserving indexed palettes, alpha,
flips and supplementary placements. Its native DS samples stay transparent;
if the RGBA image cannot be allocated, those cells remain with the ordinary
renderer. Original-sheet matching and file provenance requirements are unchanged.

The HD-font preference changes retained text layout as well as artwork. The
adapted SDK draw entry points evaluate a second scalar layout pass with the
opposite font mode; that pass never invokes a game blitter. Captured glyphs retain
both sets of positions, bearings, sizes, alignment and fitting, plus native glyph
samples. Switching the preference selects the retained layout and invalidates
primary, supplementary and sequence-text images. INFORMATION text revisions
include the font mode, so its cached texture updates without restarting the
message. Character crawls measure both prefixes, preserving their reveal state.

Unchanged frames reuse images without painting or uploading. Primary tiles skip
glyph-color scans when neither their dirty bit nor palette changes. Palette
hashes are shared per engine during submission. Supplementary updates render
each distinct source tile/palette/flip once, then copy repeated placements.
Palette keys include extended-palette selectors and the displayed BG slot.
Serial submission reuses a fixed scratch tile; canvas membership uses a bitset.
Primary and supplementary DS graphics planes stay at native resolution; the
compositor scales their indexed pixels. Captured font operations and PNG artwork use output-resolution
RGBA images. Matching-size, unflipped RGBA tile copies avoid resampling.
PNG mapping changes dirty only their affected tiles. The mapping cache takes
four bytes per canvas tile (at most 64 KiB) and introduces no additional image
planes. Indexed PNG conversion caches palette colors within each updated tile.

The untinted premultiplied glyph cache is bounded to 32 MiB and 8,192 entries.
Normalized artwork and exact fractional-phase samples share this LRU; integer
translation, clipping and tint reuse samples. No pen positions are rounded for
cache keys. PNG alpha bounds and grayscale/white-mask fast paths reduce sampling
and blending. Each individual raster is also limited to 32 MiB.

A canvas's command fragments, the bound fonts and the canvases copied in RAM
grow as needed; a copy lives as long as its memory. Only an allocation failure
returns a canvas to native rendering; a full clear enables capture again. A
canvas may be any size, its characters in the game's memory. Supplementary images
cover at most 33x25 tiles. The existing dimension-based compositor image pool
has no new global GPU-memory budget or retirement policy.

Metadata may hold any number of glyphs, map entries, kerning pairs, mappings and
scopes; atlas sides go up to 16,384 pixels (the GPU's texture limit) and scale
from 1/64 through 16. Invalid metadata logs its path and reason, then preserves
fallback. JSON numbers are locale independent.

Set these environment variables before launch for diagnostics:

- `KHDAYS_TRACE_TEXT=1`: bindings, canvases, placement and overlay eligibility.
- `KHDAYS_PROFILE_TEXT=1`: slow text-stage timings in the normal session log.
- `KHDAYS_HD_FONT_DISABLE=1`: native-text control run with other mods retained.

Keep them unset for ordinary play. No source regeneration is needed for changing
mod files or these diagnostics. Regenerate only when generation hooks change.

Font and configuration paths use UTF-8 consistently with the mod finder and PNG
reader. A replacement that needs a character map stays on native glyphs and metrics
until binding succeeds; an unbound map cannot use unrelated same-numbered slots.
Secondary `metrics: original` artwork is measured at its actual native-cell size
for fitting, independently of ignored secondary width metrics.

## Verification and remaining limits

Build `khdays-font-tests` in the existing port build, then run
`ctest --test-dir build/port/obj-x64 --output-on-failure -R khdays-font-`.
CTest includes the runtime suites, NFTR decoder and generator-hook checks, with
separate fixtures per build directory. Release tests retain assertion checks.

The same suites run under AddressSanitizer in the `port/build.py --asan` tree.
There are ten synthetic suites: camp scope selection, canvas/VRAM behavior,
layout following, metadata/routing, secondary fallback, rasterization,
raster-cache bounds, NFTR decoding, generated SDK layout and live layout reflow.

Canvas tests also check ordinary PNGs with HD fonts disabled, partial uploads
beside retained text, transparent and indexed art, flips, scrolling and image
allocation failure.
Raster checks compare exact output checksums and exercise cache eviction,
fractional phases, tinted grayscale text and the shared memory/entry limits.

The layout suite uses the existing
`build/port/gen-x64` generated sources; prepare them once for the current hooks.
Run `python port/tests/font_scope_hooks_test.py` for the generator-hook checks.
The assets test target `khdays-font-test` also covers NFTR decoding and malformed input.

Coverage includes native KH2-sized cells over smaller Days cells, positive and
negative bearings, `pipsqueak.` at scale 0.35, multiline/center/right alignment,
fractional drift, doubled PNG resolution, mixed/consecutive DS fallback, initial
panel population, partial/equal-byte clears, copied portraits, overwritten VRAM,
all three command-menu selections, scrolling, palette/flips, detached canvases,
supplementary placement and bounded repeated-tile stress.

Before merging, visually smoke-test command selections (including Items while
Magic is selected), empty inventory slots, the first panel-menu draw, repeated
pause-menu scrolling, portrait names, tutorials, mission selection and HUD/results
in Story and Mission Mode. Compare native fallback and no-mod behavior as controls.
Existing user testing established that remaining transition microstutters also
occurred in the older build; do not attribute all game stalls to HD text.

Covered rendering uses line-layout 4/8-bpp BG character canvases. True OBJ glyph
replacement, movie glyph records, TTF/OTF and mod hot reload are not implemented.
Pre-rendered text graphics require texture mods. A raw, unhooked identical-byte
write cannot be observed from memory snapshots; any new such game path needs an
operation hook. The renderer must not infer it by comparing original glyph ink.
