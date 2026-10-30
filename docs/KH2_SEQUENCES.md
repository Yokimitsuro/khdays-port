# KH2 sequences

Animated player and party portrait replacements are documented in
[KH2 portraits](KH2_PORTRAITS.md).

The native KH2 implementation includes an independent sequence viewer, reference
renderer, GPU sequence images, and a mod-provided Days HP gauge replacement. It reads
local PC BAR/IMGD/SEQD resources and a PNG or supported DDS atlas. General layout
composition and LAD remain subsequent stages. Neither
a Days installation nor generated Days sources are needed to run the viewer.

## In-game HP gauge

The installer's existing **HD menus and HUD** import now makes this gauge from
the player's installed collections. `installer/setup_steps.py build-hd --menus`
reads the five `field2d/<region>/zz0field.2dd` entries from KH2's `kh2_fifth`
package using the existing archive reader. `tools/hd/kh2_gauge.py` copies the
corresponding remastered `*_zz0field_2dd1.png` and reticle atlas `*_zz0field_2dd0.png`, recenters clips 132–138, adds the
yellow limit clips and white divider, and writes `gauge.json` with the resulting
indices (157/158/159 for the verified PC resources). The output goes into
`mods/HD PACK/layouts/gauge/<language>/` for en/us, es/sp, fr/fr, de/gr and it/it.
Source archives and the game installation are read only; private development
extractions and backups are not inputs to the installer.

The JSON starts with `docs/examples/kh2-gauge.json`. The HD pack places the
player and party gauges at Y offset -4. Obtained text uses Y offset 2 and scale
1.25. Its presentation defaults enable HUD edge alignment, retained HUD/control
during INFORMATION and the pause-menu target, subject to the saved menu settings.
On rebuilding an existing pack, custom animation assignments, positions, scale
and enable/disable settings are retained. Resource filenames, entry and source origin follow the freshly
generated files. The converters and JSON template are included in the installer's
tool fingerprint, so changing them invalidates an old completed HD import. The
installer stages the necessary Python files and template with the other HD tools;
the player needs no additional Python installation or manual conversion command.

Launch the updated executable normally. An active mod providing
`layouts/gauge/<language>/gauge.json` enables the gauge automatically, with an
English fallback. JSON selection uses the existing mod priority; its archive
and texture are resolved beside that JSON so different mods' resources are not
mixed. No external KH2 directory or launch environment is required. The HUD part
must be `on`; `hud=off` or `hud=check` retains the normal Days gauge. A bad config
or asset is logged once and retains the normal gauge. Set `enabled` to false in
the JSON to disable it. `KHDAYS_KH2_HUD=0` remains a diagnostic override.

Runtime animation errors also disable the affected replacement and clear its GPU
commands. A failed party slot leaves other slots working. Registered atlas pixels
remain owned for the texture registry's lifetime, including failed optional labels.
The importer keeps enemy, reticle and information settings at the root and removes
accidental copies of those sections from older generated party settings on rebuild.

Copy [the default config](examples/kh2-gauge.json) beside `zz0field.2dd` and
`US_zz0field_2dd1.png` in the mod's language folder. Restart after editing.

| Settings | Meaning |
|---|---|
| `archive`, `texture`, `entry` | Local filenames (PNG or supported DDS), BAR entry name |
| `offset_x`, `offset_y` | Move the complete widget, including the portrait, in DS dots |
| `scale` | Gauge size in DS dots per KH2 unit; portrait keeps its existing size |
| `geometry_scale` | Intrinsic mask/extension ratio: 1 for player art, 23/45 for small party art |
| `portrait_offset_x/y` | Move the face relative to the animated center, in DS dots |
| `center/circle/bar/cap_offset_x/y` | Move each gauge element in KH2 logical units; circle masks follow the ring |
| `center_normal/alert/dead`, `circle_normal/alert/dead`, `bar_normal/alert/dead` | State-to-clip assignments |
| `center_damage_small/medium/large`, `circle_damage`, `bar_damage` | Damage-reaction assignments |
| `circle_fill`, `bar_fill`, `cap` | Current HP and endpoint clips |
| `limit_circle`, `limit_bar` | Mod-added yellow clips (157/158 in the supplied conversion); both -1 disables them |
| `limit_marker` | White limit divider clip (159 after conversion); -1 disables it |
| `limit_marker_offset_x/y` | Divider offset in KH2 logical units |
| `center_override` | -1 selects states normally; a clip index forces that center animation |
| `play_intro`, `center_intro` | Optionally play the creation clip once on entering the field |
| `center_fade` | Fade clip for the evaluator's explicit exit state; the Days adapter currently hides immediately on leaving the field |
| `canvas_padding` | Extra DS dots around the image, including room for alert effects; default 16 |
| `source_origin_x/y` | 0 for the centered mod archive; 455/355 for an untouched KH2 archive |

Player gauge settings remain at the root; party and label settings use the
objects described below. Unknown keys, duplicate keys, invalid types and
out-of-range values are errors rather than silently ignored misspellings.
Decimal JSON values use a period regardless of the process locale. Sequence
evaluation rejects overflowing active attachments and caps each evaluated frame
at 65,536 quads to bound repeated sprite-range expansion; the GPU image limit
remains 128 quads. These are port safety limits, not claims about KH2's original limits.

The visible center quads are drawn behind the HP ring and Days portrait. Original
`GAUGE_PLAYER::create` creates clip 136; `setDamage` and the three damage wrappers
select 135/134/133, then return to the status table's 137/132/137. Clip 138's
resource track fades its alpha to zero; its original caller has not been verified
in the provided dump. It is available through the override and explicit exit
state, rather than inventing a KH2 trigger for it. The default keeps the existing
normal/status/damage selection, with the intro optional.

The Days portrait and player tag follow the center's active X/Y translation,
including sway and the configured center offset. Portrait offsets remain local
to that moving center; portrait size is unchanged.

`port/tools/kh2_center_gauge.py ARCHIVE` subtracts 455 and 355 from both X/Y
translation endpoints of only clips 132 through 138, preserving pivots, sway,
timing, curves, UVs and all other resources. It refuses shared tracks affecting
other clips, keeps an `.original.bak`, and recognizes its own converted output
on subsequent runs. This local conversion can be used by the eventual installer;
runtime playback does not rewrite assets. No private archive or backup belongs
in version control.

`port/tools/kh2_add_limit_gauge.py ARCHIVE --config GAUGE_JSON` then adds the
limit resources to that centered archive. It deep-copies sprite records 95/96
(their four group records and two referenced texture parts), writes RGBA bytes
`FF E6 00 80` into all four copied corner colors, and copies clips 128/129 with
their animation tracks redirected to the new sprite records. This is RGBA hex
`FFE60080`; its little-endian word is `0x8000E6FF`. The copied animations retain
their neutral modulation colors, so there is no runtime tint override.
Original resources are preserved. The replacement SEQD is appended and only its
BAR entry's offset/size change; other resources keep their bytes and offsets.
The tool keeps `.before-limit.bak` and updates only the JSON's two limit indices.

`port/tools/kh2_add_limit_marker.py ARCHIVE --config GAUGE_JSON` adds the divider
after the center and yellow conversions. It copies clip 123's neutral endpoint
transform, creates a separate sprite/group/texture part, and samples the bottom
logical pixel row of sprite 95's gray ring. Its radial strip is two KH2 units
thick and writes RGBA `FF FF FF 80` to all four corner colors (little-endian word
`0x80FFFFFF`). Rotation and extension placement use the same endpoint transform
as the HP cap. No texture edit or runtime tint is needed. The tool preserves all
existing records, keeps `.before-marker.bak`, and updates only `limit_marker` in
the JSON, preserving other settings. Run conversions in center/limit/marker order;
earlier tools deliberately refuse later edits instead of overwriting them.

The overlay uses `khdays_link_player_limit_hp`, which calls the original
`Ov022_ComputeChipGauge` on the local actor's block at +0x22f8, as
`Ov022_UpdateSubsystems` does. At decompilation commit
`a2b0478b87e3102732094c34f6ca89595f98ab93`, that computes a base of `0x4cd`,
increased by ability 0x52's value times `(0x800 - 0x4cd)`, reduced by the
block's use count at +0x335 times `0x19a`, multiplied by max HP, clamped to at
least 4096 and rounded up after division by 4096. Battle-mode settings can
disable it or replace it with half maximum HP. This is distinct from the
quarter-HP low-health warning predicate.

The vanilla path passes that threshold to `Ov002_SetScrollPosition(1, threshold)`;
`Ov002_DrawGaugeTweenCell` selects the alternate color for cells up to that point.
The port keeps the threshold in HP units, avoiding the DS bar's 77-cell rounding.
The new circle is clipped by both the live HP mask and its threshold mask, and
the new extension uses `min(current HP, threshold, maximum HP)`. Yellow therefore
shrinks with HP, covers all remaining HP below the threshold, and vanishes at
zero HP or when the original calculation disables it. The existing warning,
damage reaction and limit gameplay rules are not modified.

The divider follows the original Days HUD context (`data_ov002_0207f618`), rather
than an independently timed SEQD animation. `Ov002_RepaintPanelRows` selects
the latched halfword at +0x182 while +0x178 is active; otherwise it selects the
shown HP count at +0x112 while the ready flag at +0x2c is set. Both flags clear
hides it. `Ov002_DrawPromptMarker` clamps positions to 76 on the 77-cell bar.
Those rounded pixel positions must not be mapped back into HP: that offsets the
divider from exact HP and prevents it reaching a full-length KH2 gauge. The
adapter uses current HP directly while ready. While active, `game_link` reads
the local actor's block at +0x22f8: elapsed counter +0x32c, entry HP +0x336, and
outward budget +0x338. The HUD's +0x184 supplies the total budget and +0x17c the
double mode. The same native fractions interpolate from exact entry HP to max
HP, then to exact current HP on the return phase. Values clamp to their actual
endpoints. The first native update crossing the double-mode turn lands at max
HP even if that fixed-point boundary falls between updates. No independent
timer is introduced; pause, message-driven counter updates and visibility during
wind-down still follow the game. The marker follows its circle/bar offset plus
its own local offset and is
drawn after the HP layers without their depth masks, so it can travel above the
current HP; it remains behind the portrait like the rest of the gauge.

At the decompilation commit above, `func_ov022_02094678(start_hp, max_hp)` returns
`150 * (4096 - FX_Div(start_hp << 12, max_hp << 12))` in normal frame modes.
`Ov022_BeginScene` sets the battle step to 4096 (6144 in frame mode 1), and
`Ov022_StepRun` advances its timer with that step. At 30 battle updates per second,
the outward sweep is approximately `5 * (1 - start_hp / max_hp)` seconds: at 30%
HP the fixed-point budget is 430050, about 3.499756 seconds (105 updates to cross).
Equal HP fractions have equal timing regardless of gauge length. The native
alternate frame mode uses a budget multiplier of 100; the adapter does not
substitute a wall-clock duration or change those original rules.

`func_ov022_020946e4` adds another budget based on current HP when ability 0x58
selects the double mode. `Ov002_UpdateScrollThumb` moves from starting shown HP
to full over the first budget, then back to current shown HP over that additional
budget. Without double mode it only moves outward. `Ov022_IsActionOver` checks
timer budget and interruption conditions independently; state 7 can wait for a
queued message before `Ov002_ClosePrompt` hides the marker. Reaching the yellow
range is therefore not a renderer-side signal to end a limit or hide the divider.
The native timing test compiles and exercises the unchanged duration and divider
sources directly; gameplay and generated sources are not edited.

The circle is centered at 230,160 DS dots around the existing face, at 0.65 of
KH2's logical size, and the bar extends left. The existing face and player tag
are composed over it. No portrait texture is edited: the border line embedded
in that artwork remains for a later texture edit. The separately drawn HP fill,
frame and HP label are suppressed. Party replacements are configured separately
within the same JSON, as described below.

The game adapter reads the local battle actor through `game_link`, including the
game's low-HP predicate (`func_ov022_020ad7b0`, quarter HP with ability 0x42's
exception). A decrease restarts the previous-HP segment and the small reaction;
this is an explicit Days policy, not a reconstruction of KH2 hit-event severity.
Healing updates HP immediately. Maximum-HP changes reseed the gauge. The timeline
advances once per VBlank and freezes during the game's pause; the standalone
viewer's configurable clock remains the reference for further timing comparison.

The GPU renders the gauge's normal-alpha clips and masks into an existing UI
image, then places the face over it. A separate layout box preserves
the portrait's layout and widescreen anchor. On one screen the image is placed
directly from that box's composition transform, so the circle and extension can
extend outside the party's source crop. Its image allocation is bounded to a
1024-dot view; the HP-to-geometry mapping is uncapped, with offscreen geometry
clipped instead of allocating an image proportional to HP. The image includes
16 dots beyond the portrait's right edge so the circle is not cut at x 256;
its one-screen anchor stays inside the original screen and its center stays
at 230,160. Mask rotations snap at quarter turns as in `Ft4Base::calcXYZ`,
avoiding a fractional crack between the rotated and rectangular masks.

The original fill is identified from `Ov002_InitGauges`' offsets and lengths,
`Ov002_TickScene`'s queue 7 upload, and the transfer table at `data_02041934`
(queue 7 is `GX_LoadBG3Char`). The local slot is
`Ov002_GetPanelWord0220Idx(Session_GetLocalPlayerIndex())`. The static frame/label
occupy the portrait's strip from y 183 down. The challenge's OBJ path is matched
against the live gauge tiles; that path still needs an in-game challenge capture.

Validation includes synthetic adapter tests for HP changes, pause, leaving the
field, missing party slots, selective tile/sprite suppression, face occlusion,
and bounded allocation for maximum HP. Compositor tests cover independent image
alpha and omission from one-screen source crops. A copied-save `play` walk has
also reached the field with both the normal and replacement HUDs, with screenshots
kept only in the ignored build directory. The GPU path has run through the field
with one-screen layout, HD graphics, and the local mod directory. Challenge and
multiplayer behavior still need manual validation.

## Runtime cost and GPU validation

The live path evaluates and serializes quads on the game thread, then includes
the commands in the frame package. It does not rasterize or upload the gauge's
pixels on the CPU. A compute pass writes the output region in the existing GPU
replacement-image store before the normal 2D and one-screen composition passes.
The atlas is registered once without unused mipmaps; the pipeline is created at
startup. The face's palette conversion reuses the existing versioned image cache.
Buffers are reused and command uploads cycle normally; the live pass performs no
GPU readback or explicit fence wait.

The quad edge calculation uses HLSL `precise` for its view transform and
barycentric coordinates, preventing fused arithmetic from selecting a different
side of a shared diagonal mask edge than the CPU reference. Texture sampling and
color calculations retain their existing optimization. The white divider adds
one quad and no additional image allocation or GPU pass. With that quad enabled,
the local 8x private-asset test measured about 0.29 ms median and 0.59 ms at the
95th percentile (100 draws, including the test-only submission wait).

The CPU renderer remains the reference for headless checks, recording and
reference screenshots. Those diagnostic paths still rasterize on the CPU and
are not a live performance measurement.

`khdays-kh2-gpu-test` checks the compute output against the CPU renderer for
filtering, corner colors, all three blend modes, depth-only masks, face occlusion,
HD face transparency and a synthetic 4096x2048 atlas. With the two private asset
paths as arguments, it also compares real
gaug compositions at several HP values and benchmarks repeated draws at 1x, 4x
and 8x. Its timings include submission and an explicit wait used only by the test.
On the local Direct3D 12 device, the 1024x512 atlas at 8x measured about 0.21 ms
median and 1.4 ms at the 95th percentile over 100 draws. These are gauge-pass
measurements, not a guarantee for other hardware or the rest of the game.
The synthetic HUD adapter test separately verifies that repeated live composition
at 8x performs no CPU pixel reads or pixel-image updates.

Matching 145-second local `play` walks with HD2, one-screen layout, HD graphics
and 2x antialiasing reached the field with the replacement both enabled and
disabled. In the steady field interval at VBlanks 6600 through 8400, each run
reported 1800 presented frames and zero missed frames. Mean reported 2D time was
about 2.10 ms with the gauge and 2.08 ms without it; this small difference is
within the variation of these runs. Logs are local build artifacts, not fixtures.

## Build and run

Use the existing x64 MSVC environment and build configuration:

```text
cmake --build build\port\obj-x64 --target khdays-kh2-sequence-viewer khdays-kh2-sequence-test khdays-native
build\port\obj-x64\khdays-kh2-sequence-test.exe
build\port\obj-x64\khdays-kh2-sequence-viewer.exe --root "C:\KH2-extracted" --sequence 121
```

`--root` (or `KHDAYS_KH2_ASSET_DIR`) supplies the KH2 data directory. The defaults
within it are `field2d/us/zz0field.2dd` and
`remastered/field2d/us/zz0field.2dd/US_zz0field_2dd1.png`. `--archive`, `--texture`,
and `--entry` override that selection. No extraction is performed.

Space plays or pauses; Left/Right step one source frame; Up/Down select a sequence;
Home restarts; +/- change zoom. The view is centered once at the sequence's initial
frame so subsequent movement remains visible. `--frame` selects a starting frame,
`--list` prints the clips, and `--output FILE.png` renders one frame without opening
a window. `--fps` sets source timeline units per second (default 60); it is an
inspection control, not a verified claim about the original Vsync clock.
`--play` starts playback immediately; `--run-frames N` exits after N presentations
for automated viewer smoke tests.

The separate HP composition can be inspected with:

```text
build\port\obj-x64\khdays-kh2-sequence-viewer.exe --root "C:\KH2-extracted" --hp 248 --max-hp 372 --previous-hp 372 --damage-frame 30 --scale 1.5
```

`--reaction small|medium|large` selects the original parent damage reaction.
`--status normal|alert|dead` selects the status explicitly. In HP mode Up/Down
change current HP by 31 Days units; Shift+Up/Down change maximum HP. A decrease
restarts the damage segment and the small reaction for inspection; it does not
infer a KH2 battle-event severity from Days damage.
`--damage-frame 100` is the default: the previous-HP clips are then blank, as at
their original restart frame. HP mode advances an absolute timeline rather than
looping the viewer's selected standalone clip. Long bars may extend beyond the
viewport; zoom out to inspect them. Their length is not capped.

For additional private-asset checks, supply the archive and texture as the two
arguments to `khdays-kh2-sequence-test.exe`. This checks the expected gaug counts,
evaluates all 157 clips at their beginning/middle/end, and exercises HP compositions
through the full unsigned-16-bit Days HP range. The ordinary test needs only
synthetic data. Images and extracted resources belong outside version control;
the viewer's output can go under the ignored `build/` directory.
The private check also renders mask coverage across the circular thresholds and
checks that the previous-HP segment disappears at its blank restart frame.

## Small party gauges

The importer also appends small versions of the player gauge clips and writes
a `party` object in `layouts/gauge/<lang>/gauge.json`. This independently enables all three party
slots; `enabled: false` restores their native gauges. The player configuration
and portrait replacement settings remain independent. Both JSON files expose the
same animation, placement and display-scale settings; the generated party file
disables limit clips because Days does not display party limit gauges.
The [combined defaults](examples/kh2-gauge.json) include shared party settings and
`party.slots.1`, `.2`, and `.3` overrides. These are HUD slots, ordered upward
from the player, rather than character IDs. Each override inherits shared party
settings; its `offset_x/y` adds to the shared value for that slot. Icon offsets
also add to the shared icon offsets; other settings replace their shared values.

The first party gauge is raised eight DS pixels from its native portrait center.
This is part of the default placement: JSON `offset_y` remains zero. Subsequent
gauges are stacked using their artwork radii, with the same edge-to-edge gap as
the player and first party gauge. Independent offsets apply after that placement,
so adjusting one slot does not push its neighbors. Portraits and player-number
labels follow the corresponding animated center.

Rebuilding migrates an old `party.json` into `gauge.json` and removes the separate
file. The old experimental -8 Y offset becomes zero because the default now
includes that lift; other custom settings survive. Runtime reads only the combined
file. Installer conversion validates all languages before publishing outputs.

The smaller atlas quarters have radius 23 rather than 45 sequence units. Their
`geometry_scale` is therefore `23 / 45`, while the display `scale` stays `0.65`.
186 Days HP still fills the three-quarter circle. Extension lengths and mask
geometry scale by the same ratio, so equal HP produces proportionally smaller
party bars. Changing display scale alone does not change the HP mapping.

Generated groups use parts 101 (background), 103 (green), and 105 (damage,
tinted with the player damage color). Two mirrored copies of half-circle 102
form the backing at the origin; the player backing's thin outer ring is omitted.
A one-unit bottom strip of part 101, tinted `00 00 00 80`, supplies the end cap.
The copies of clips 121–129 and 132–138 preserve timing, rotations, scale and colors,
and scale positions and sway by `23 / 45`. Mirroring reverses UVs and corner colors
while keeping rectangle bounds ordered, so adjacent quarters share a continuous
pixel edge. Existing player clips and groups remain unchanged.
Axis-aligned masks use ordered bounds too, preventing stray lines at exact
quarter-turn HP values.

`port/ui/kh2_party_hud` reads each party slot through the existing read-only
portrait query, tracks independent HP and damage trails, and uses the existing
sequence GPU renderer with cached images and atlas. It finds portrait positions
from the native BG3 tile ranges rather than fixed screen coordinates. Native and
replacement portraits follow their slot's animated center; the existing UI
element layout handles widescreen positioning. Only the matching native HP tiles
and their OBJ copies are suppressed. No game behavior or generated source changes.

The headless viewer accepts `--config PATH/gauge.json --party 1 --hp 248 --max-hp 372
--previous-hp 310 --damage-frame 30 --output FILE.png` to inspect these resources.
Focused tests cover the generated geometry, centered backing, unchanged original
records, animation timing, proportional HP mapping, independent live slots,
damage, pause, and reuse without repeated pixel uploads. Private asset checks
compare CPU/GPU output at circular thresholds and along the extended bar. Manual
game validation should include all occupied slots, native/HD/KH2 portraits,
damage and defeat/revival, mission mode, widescreen, and disabling each replacement.

## Player-number icons and limit text

`player_icon` at the root configures the player's P1–P4 label; `party.player_icon`
configures party labels, with overrides under each party slot. `limit_text` at
the root configures the native LIMIT-available indicator. These objects expose
`enabled` (false), `animation` (-1), `archive`, `texture`, `entry`, `scale`,
`source_origin_x/y`, `frames_per_tick`, and `offset_x/y`. Resource names resolve
beside the combined JSON and default to the gauge's archive and atlas. PNG and
supported DDS textures use the existing decoders. A label sequence's origin is
placed at the native label's center; source-origin settings preserve relative
animation motion when recentering an existing resource.

Compact native badges have a separate upper border (main.p2 entry 0's first
NSCR, tiles 140/297 above 172 and the number tile). A successful replacement
suppresses that exact adjacent pair without changing the replacement's anchor;
native fallback retains it. The source pixels are in entry 4's NCGR.

Player icons also expose `p1`, `p2`, `p3`, and `p4` clip assignments (-1). A
nonnegative number-specific assignment overrides `animation`, selected by the
number actually drawn by Days, including when the local multiplayer player is
not P1. Enabling a replacement requires both `enabled: true` and an assigned
clip. Missing assets or invalid clips log once and keep the native icon. Offsets
move native artwork too, so a replacement is not required for positioning.

The native BG3 player-number and LIMIT tiles were identified from the original
Days `UI/btl/main.p2` NSCR/NCGR/NCLR resources, independently of face tiles and HP
fill uploads. Only those tiles are regrouped. LIMIT replacement visibility initially follows
the native text's tiles. Playback then follows the native availability flag
(Ov002_SetPanelMode_2, gauge context +0x2c, tag 0x1a), so blank phases of the
DS artwork cannot restart the custom clip. SEQD's loop count and restart frame
control its animation; availability is not inferred from HP. Slot geometry and origins retain the checks
against the native HUD resource. Sequence labels use cached GPU images and
atlases; playback pauses with the game. These replacements are disabled in the
generated JSON until artwork is supplied.

Number rows are assigned once in native HUD order rather than searched separately
below each portrait. Party numbers attach to the bar's reduced 37-unit baseline,
with their configured offsets applied afterward. Native label images are claimed
before gauge composition and emitted after gauges and portraits, just like their
sequence replacements, so neither the number badges nor LIMIT text is covered.

## Enemy gauge

`enemy` in the same `layouts/gauge/<lang>/gauge.json` independently enables the
enemy replacement. Its default display scale is `0.65`, with zero X/Y offsets.
An older JSON without this section keeps the native enemy gauge. The importer
adds the defaults when rebuilding the HD pack and preserves customized enemy
settings. The existing archive already contains the enemy clips; no enemy sprite
conversion or additional extraction inputs are required.

Days retains its original capacity: **250 HP per bar in Story Mode and 750 in
Mission Mode**. The adapter reads `Ov002_InitGaugeScene`'s live unit multiplier,
not a new gameplay setting. KH2 uses 200 HP per bar; its artwork therefore draws
`200 * remainder / days_capacity` sequence units. At exactly one full bar there
is no extra tank. A 1.5-bar maximum shows the dim full-length preview behind the
shorter initial bar; after rollover the main bar becomes full length. Green and
red lengths are independently anchored at the right, as in `updateHP`.

The settings select `background` (38), `preview` (42), `bar_background` (39),
`cap` (40), `damage` (41), `fill` (43), `tank_full` (44), `tank_empty` (45), and
`tank_damage` (46). They also expose `archive`, `texture`, `entry`, `scale`,
`offset_x/y`, `frames_per_tick`, `source_origin_x/y` (498,31), and independent
`bar_offset_x/y` and `tank_offset_x/y`. The original tank spacing comes from
clip 45's extended parameter. Damage restarts the red clips; their out-of-range
restart frame makes the faded bar and depleted tanks disappear. Healing clears
the trail and restores green tanks. Playback pauses with Days.

`port/ui/kh2_enemy_hud` reads the field gauge context at `0x0207f618`, including
its current/maximum HP and native tween reset state. It does not write game
memory. Only main.p2's enemy bar, HP label, and tank cells (47–59) are hidden,
after a replacement image has been prepared successfully. Days' separate enemy
name surface remains unchanged. Missing resources, hidden native HUD, or
unsupported command/canvas sizes retain the native gauge.

The normal path uses the existing sequence GPU pass, cached atlas, and output
image. The canvas encloses all evaluated geometry instead of using Days' narrow
bar rectangle. A separate native composition anchor lets the wider image and
JSON offsets follow the top-right HUD placement without being clipped to the
original one-screen crop. The CPU rasterizer remains a reference path.

The headless viewer accepts `--enemy --bar-capacity 250 --hp 250 --max-hp 375
--previous-hp 375 --damage-frame 30 --config PATH/gauge.json --output FILE.png`.
Synthetic tests cover both capacities, right anchoring, partial bars, rollover,
red disappearance, healing, target reset, pause, selective cell suppression,
legacy/missing-resource fallback, extended canvas bounds, and image reuse without
CPU uploads. The optional GPU test mode `--enemy ARCHIVE TEXTURE` compares the
local HD resource with the CPU reference at 1x, 4x, and 8x and reports warm draw
timings. In-game validation should include both modes, target changes, damage
across multiple bars, healing, hiding the HUD, HUD-size settings, and widescreen.

KH2 provenance: `GAUGE_ENEMY::create` (`0x9DD1C`), `start` (`0x9E06C`),
`updateHP` (`0x9D890`), `damage` (`0x9E208`), and `cure` (`0x9DC64`), checked
against the supplied PS3 debug pseudocode/disassembly and little-endian PC gaug.
Days provenance: overlay 002's `Ov002_InitGaugeScene`, `Ov002_RestartRequestQueue`,
`Ov002_UpdateGaugeUnits`, `Ov002_RepaintSubPanelRows`, `Ov002_TickScene`, and
`Ov002_BuildPanelLabels` at the decompilation revision recorded by this repository's
submodule. Target replacement is observed through native tween resets, without
adding a gameplay hook; original KH2 transition timing still needs capture comparison.

## Boundaries

In the one-screen picture, independent field HUD images carry `KES_HUD` and
follow the situation's HUD visibility. `host/render/onescreen_images` inserts
them immediately above their source screen shape, preserving all earlier
foreground shapes. A tutorial's page therefore covers the HUD; exposed HUD
receives its source shape's dimming. Dialogs, mission information, pause, game
over, and the HUD-off setting do not promote hidden HUD images. The images keep
their full canvas outside the native crop, and labels retain their order above
gauges and portraits. The shared C/HLSL one-screen pass applies image dimming.
It also reads the native layer's complementary EVA/EVB fade from the frame's
blend registers. Images sharing that layer compose together before the fade is
applied, so an opaque portrait does not reveal its gauge through a fade or darken
twice. Native number/LIMIT label fallbacks join the same image group through the
existing cached palette conversion. Two-screen composition retains its normal
native blending path.

Native gauge suppression follows tile identity outside the usual portrait or
enemy region while the HUD transitions. Temporary 8/16/32/64-pixel OBJ strips
are matched against the live gauge data; outside their original region the OBJ
source must also belong to main.p2. Enemy teardown retains ownership until the
native bar disappears, including a boss reaching zero HP. Fade coefficients do
not require changing image pixels or uploading textures every frame. The native
blend source is `Ov002_CommitDisplayState` and `Ov002_UpdatePanelBlend` in overlay
002; their BG3 targets and complementary coefficients are checked by regressions
at all 17 hardware fade levels.

- `assets/kh2_bar` is the BAR reader factored from `kh2_map`, shared by both callers.
- `assets/kh2_sequence` owns endian-safe parsing, playback, transforms, colors, and
  ordered quad evaluation. It has no filesystem, HP, SDL, or Days dependencies.
- `port/host/render/kh2_sequence_asset` resolves a local archive/image pair and
  uses the existing image/DDS decoders. The installer supplies these same local
  inputs from the player's collection through `tools/hd/kh2_gauge.py`.
- `port/gfx/kh2_sequence` rasterizes the ordered quads against an actual destination
  with normal/add/subtract blending and depth-only masks, and encodes GPU commands.
  It remains the CPU reference. `port/host/render/kh2_sequence_gpu` and
  `port/shaders/kh2_sequence.hlsl` render those commands into a UI image. Blend
  modes operate against that image's accumulated pixels. The 3D reticle path
  instead blends its ordered elements directly with the scene (see below).
- `port/ui/kh2_hp_gauge` selects the gauge clips and builds mask geometry. The
  reusable sequence code has no knowledge of gauge thresholds or Days HP units.

## Obtained items

The `obtained` object in the same `gauge.json` replaces Days' item pickup banner
with `gaug` clip 76. It defaults to scale 0.65. `animation`, `offset_x/y`,
`source_origin_x/y`, `frames_per_tick`, `text_offset_x/y`, and `text_scale` can be
customized along with the archive, atlas and entry. An absent object, disabled
object, missing file or invalid animation keeps the existing Days notification.

The installed-game importer recenters every track of clip 76 relative to its
first active track's base position, preserving both endpoints, sprite offsets,
rotation, scale, colors, timing and flags. The verified western PC resources use
199,340 in English and 256,340 in Spanish, French, German and Italian. This is a
conversion of freshly extracted data, not an in-place patch of an existing mod.
It leaves other clips untouched and retains existing JSON customizations.

The replacement anchors to the native upper-screen pickup area beside Days'
item-name tiles at map column 1, row 8 on BG1. Its complete animation can extend
beyond the original crop. In single-screen mode it keeps the native notification
position and the player/party gauges' composition scale. Original artwork and
captured text in the pickup rectangle are suppressed only while the replacement
owns the announcement; other copies of the cached item name remain available.
The native layout's pickup entry (tag 0x4A in `UI/btl/main.p2` entry 0) covers
13 by 3 tiles at column 0, row 7; suppression includes that complete rectangle
on BG3 for the artwork and BG1 for the item name. `Ov002_LoadElemsFromLayout`
maps its kind 3 through `data_ov002_0207db70[3]` to destination 11 (main BG3).
Entrance sway counts represent half-turns, as confirmed by `UpdateActive`'s
0x40490FDB constant at 0xC2D18 and its multiplication at 0xC32E4.

Days still selects the item name, sound and gameplay behavior. The existing
`Ov002_PanelDrawSlotCursor` private capture records both native glyphs and font
replacements in `field.obtained` scope. Glyphs are uploaded only when their
content, palette or resolution changes; animation, blending and composition run
on the GPU. The text follows the active track and its alpha, with no marquee.
The western PC timeline is 107 frames, advanced in the same game-frame units as
the other sequences. Repeated announcements restart it, pause freezes it, and
menu, cutscene and scene changes cancel it. Its exit can finish after the native
tilemap stops showing the pickup. Saves and inventory state are not changed.

The original debug executable confirms the setup inlined into
`dk::TREASURE_INFO::update()` (0x7FCDC): the prize queue creates `SpriteMessage`
with clip 76, group 13, font size 16, no 16:9 offset, and centered alignment when
the clip's Ex parameter is one. `dk::SpriteMessage::drawMessage()` (0x7B5C8)
attaches text to active XY plus clip parameters and applies active alpha.
`yasui/libys/messagedraw.h` identifies alignment 2 as `ALIGN_CENTER`. The port
uses the visible Days glyph extent for that centering and preserves Days' font
metrics rather than substituting KH2's font. The Days source is the same pinned
decompilation commit documented below; its cached item tiles and private capture
are described in [HD fonts](HD_FONTS.md).

`khdays-kh2-obtained-test` checks one-shot playback, repeated items, text alpha and
translation, cached uploads, pause, context changes and fallback with synthetic
resources. `--reference ARCHIVE` verifies every frame of the locally converted
PC resource. `khdays-kh2-gpu-test --obtained ARCHIVE TEXTURE` compares entrance,
hold and exit frames against the CPU reference at 1x, 4x and 8x. Font-canvas and
single-screen tests cover native-font capture and position/scale independently.

## Information window

`gauge.json` also accepts an independent `information` object. The HD importer enables
it and uses the localized gaug archive and `*_zz0field_2dd1.png` atlas. Missing settings,
disabled HD graphics, unavailable resources or unsupported clips retain Days' banner.
No additional asset patch is required. Restart after editing settings.

Settings are `enabled`, `archive`, `texture`, `entry`, `scale` (0.65), `offset_x/y`,
`source_origin_x/y` (0,15), `frames_per_tick`, `intro` (0), `loop` (1), `reveal` (2),
`outro` (3), `text_offset_x/y`, `text_scale` (1), `clip_width/height` (0,64 KH2
units; width 0 reads the PC sequence's Ex parameter), `hold_frames` (300 VBlanks), `scroll_wait_frames` (180 sequence frames),
and `scroll_speed` (1 KH2 unit per sequence frame; 0 disables scrolling).
Text offsets use DS screen units before
composition scaling. `text_scale=1` keeps Days' glyph size at gauge scale 0.65;
changing the gauge scale scales the text proportionally. Native font mappings,
language, glyph metrics, control icons, palette and typewriter updates remain
owned by Days. The replacement retains offscreen glyphs and supplies its own
marquee only when the line exceeds the readable area. The original PC gaug window
has Ex=360; the independent PC image uses that width directly. PS3's debug update
subtracts 170 before its platform scissor call; applying that adjustment to this
image incorrectly clipped the text halfway across the window. The final reveal
quads supply the right fade's clear boundary (335.5 in the original data), so the
last character stops before the fade instead of disappearing under it. Positive
`clip_width` values still override the viewport for custom sequences.
Two-screen mode retains
the message until Days closes it; one-screen mode holds short lines for five
seconds before playing the complete exit. Long lines get enough time to scroll
to their end and remain readable. `hold_frames` changes the minimum one-screen
hold, measured after the intro before any reading/scroll delay can expire.
Pause freezes playback and shows the retained caption without the reveal
cover (animation 2). If paused during the intro, the displayed background uses the
open looping window with visible text; the animation clock stays frozen. Returning
to play starts a fresh display period for the retained lower caption.

Generation hooks observe accepted `Ov002_RequestPanelScreen` requests before any
glyphs arrive, lower `Ov002_SetCaptionText` updates, `Ov002_StartPanelFadeIn` and
the confirmed upper `Ov002_BeginTextCrawl`, and `Ov002_HoldPanelScreen` pause/resume.
`Ov002_OpenPanelScreen` and `Ov002_ClosePanelScreen` reset both captions and advance
a context generation. Captured and copied glyph canvases retain that generation;
old map canvases cannot supply a new caption, even if the message is identical.
Existing
`field.hud.information` glyph capture identifies the
visible text canvases. The lower caption uses sub BG0 (`Ov002_GetItemResource(0x18)`
returns `G2S_GetBG0ScrPtr`, and `Ov002_UploadCaptionChars` uses `GXS_LoadBG0Char`);
the upper crawl uses main BG1. `ui/text.c` uses its original/native or replacement-font
painter to cache a transparent glyph-only plane; it does not sample the old border
or recolor a flattened screenshot. Rasterization and glyph-plane uploads occur only
when commands, palette or resolution change. Full RAM canvas copies retain the
information tag as well as glyph commands, including ordinary native-font glyphs.
Wrapped tilemap positions are normalized before selecting a source; when several
staging canvases share a layer, the one with the greatest visible glyph coverage wins.
The GPU sequence pass
inserts that plane between the background and the independent reveal quads, with
animated positioning, clipping, scale and sequence alpha. Closing follows the
original `INFORMATION_WINDOW::close`: it clears the message and hides the reveal
overlay together, then plays the background's exit animation. Retained glyph
pixels are kept for pause/restore, but never shown during the exit.

The adapter removes observed information text and `bl000.NCGR` artwork in
the original top rows. The upper crawl instead uses BG3 artwork from `main.p2`.
During panel states 1–8 the adapter records those cells' map entries, location and
tile bytes. Identical retired artwork stays hidden after the panel state ends;
a changed map, entry or tile releases its suppression immediately, preventing a
final flash without hiding newly published HUD artwork.

In single-screen mode the original port crops the lower banner (232x24 DS dots,
224x24 during pause) or the upper banner (256x24) and places that crop at the top
left. An available information replacement disables those built-in crops with the
`information-mod` composition state, even before the first message or after an exit.
The replacement instead receives its own full-image shape at the top left, with
the port's 5-dot top margin and the player/party gauge's 0.766667 composition scale,
both following HUD size. A JSON scale of 0.65 therefore has the same ratio to
player/party gauge art; it is not shrunk again by the old native banner's 0.5 scale.
The same root and scale apply to either native engine, independently of crop
shapes, mission-panel pixel detection and native text-layer scroll or alpha.
JSON offsets remain in DS units before that composition scale. Sequence intro,
reveal and exit control visibility; the game's master screen fades still apply.
The upper image has priority over the lower copy during a mission introduction;
pause prefers the retained lower caption. The replacement does not change
`info-upper`: that state also controls whether the native HUD's 3D is hidden or
positioned. Its lifetime remains Days' even while a sequence is entering or leaving,
so the heart counter cannot lose its layout transform for a transition frame.
Dialogs, tutorials and other screens cannot receive this shape. Without a loaded,
enabled replacement, the existing crops and native-pixel timer remain unchanged.

The native `Ov002_RequestPanelScreen` function is observed rather than invoked
for lower-screen hints: it controls panel modes, world fades, sound and game flow
as well as rendering. Both native message paths feed the same replacement renderer.
The game-thread situation snapshot gates replacement drawing and native suppression
in both screen modes: camp/Panel menus, shops, game over, tutorials, movies,
running event scenes, dialog panels and held loading screens cannot retain a
field information image. Returning to the field restores the current caption;
changing between one and two screens also restores its appropriate lifetime.

KH2 provenance: maintainer-supplied PS3 debug disassembly for
`INFORMATION::openInformationWindow` (0xA6BC0), `INFORMATION_WINDOW::create`
(0xA8A30), `init` (0xA8924), `update` (0xA8710), `setMessage` (0xA8B4C), `close`
(0xA8690), `SpriteMessage::drawMessage` (0x7B5C8), and `ScisSpriteMessage::draw`
(0x6A9634), checked against DWARF field offsets and the local little-endian PC
SEQD. Window and mask begin on 0, then independently select 1 and 2. Updating an
open message keeps 1 and restarts 2; closing selects 3 and hides the mask. Text
position adds sequence active XY, sprite offsets/position, clip X/Y parameters,
and the message offset. The original clip parameters are (6,-12,0,45,360), and
init separately sets the message offset to (6,0). PS3's -85 widescreen adjustment
and INFO_BASE stacking are replaced by Days' composition and JSON offsets.
The marquee uses KH2's 180-frame initial wait and one-unit-per-frame speed by
default. Intentional differences: fitting text remains still, scrolling stops
at the final readable position outside the edge fade, and Days retains control of message lifetime
except for the port's temporary one-screen display. System sound is omitted.

Tests cover independent intro/reveal/exit, message replacement, retained glyphs,
pause, warm-frame allocation/upload counts, resolution changes, timeout without
reopening, and fallback. Font-canvas tests exercise native and replacement glyph
planes and their cache. GPU tests cover text insertion, scaling, scissor, alpha,
mask order and exact shared-edge coverage at non-power-of-two scales. Local-only
`khdays-kh2-gpu-test --information ARCHIVE TEXTURE` compares original clips 0–3
against the reference rasterizer at 1x/4x/8x. In-game verification should include
boss hints, mission-start information, changing/empty messages, long scrolling
lines, font replacements, one/two screens, dialogs, pause, and scene fades.
`khdays-kh2-information-test --reference ARCHIVE` also checks the runtime's text
viewport, final scroll position and text removal at closing against a private
original PC gaug sequence without launching the game.

## Target and interaction reticles

The same `gauge.json` has a `reticles` object. Its shared `archive`, `texture`,
`entry`, `scale`, `offset_x/y`, `source_origin_x/y` and `frames_per_tick` apply to
the five objects below; each object can override those fields independently.
`enabled: false` on `reticles` disables them all. The gauge's root `enabled`
does not disable reticles. Restart after changing the file.

| Object | Start and loop | Exit | Default |
| --- | --- | --- | --- |
| `soft_lock` | 5 | 4 | On |
| `hard_lock` | 27 | 26 | On |
| `interact` | 50 | 49 | On |
| `chain` | -1 | -1 | Off |
| `error` | -1 | -1 | Off |

The fields are `animation` and `end_animation`. A negative start animation keeps
the Days draw; a negative exit animation removes the replacement immediately.
The HD pack defaults use `fiel` and `US_zz0field_2dd0.png` at scale 0.35. PNG and supported
DDS atlases use the existing loader. Offsets are DS screen dots relative to the
projected target; source origin is in KH2 logical units. The original `fiel`
coordinates are already centered. No binary recentering is needed. Its IMGD is
256x256 and the supplied remaster is 1024x1024; the existing logical atlas
conversion supplies the factor of four.

Days already projects targets to screen coordinates before drawing its 3D sprite
quads. The port observes those draws, preserving selection, hard-lock input,
target eligibility, offscreen checks and the eight-dot vertical lift. Only the
chosen marker draws are suppressed. The actual archive members are `rockon2t`
and `rockon02` for the target pair, `rockon03` for the four interaction corners,
`rockon04` for its usable center, `rockon05@rockon05_pl` for its unavailable
center, and `t_02@t_02_pl` for the chain timer. Error replacement includes its
corner set; with it disabled, that entire native set is retained.

Unavailable or invalid resources fail independently for each object. Its native
draw continues, so the existing `graphics/ba/ef/ta.p.z/...png` replacement path
still applies, followed by the DS texture. Other effects in this archive are
unaffected. New targets restart the intro; a lost target or change of marker kind
plays the prior exit once at its last world position, reprojected through the
current camera each update. The position is copied rather than retaining a
target pointer, so removed targets cannot leave dangling references. Before
native markers switch to an orthographic camera, `Ov022_UpdateSelectionMarker`
projects retained anchors through `Ov022_ProjectToScreen`, including its
8-pixel lift, viewport and behind-camera checks. Off-screen exits keep advancing
without drawing; they can reappear only while their exit timeline is unfinished.
Native input/UI
suppression cancels the replacement. A long gap without marker updates restarts
cleanly. Loop points and timing come from SEQD (`5`: 11–59, `27`: 12–69,
`50`: 71–189), with one source frame per VBlank by default. Pause freezes playback.

Reticles submit a sequence polygon into the existing 3D layer, with the native
polygon ID, alpha, orthographic depth and W scale. `raster3d.hlsl` evaluates its
ordered elements against the scene at that pixel: normal source-alpha, additive
and subtractive blending preserve atlas alpha at 8-bit precision. The DS's
5-bit texture-alpha conversion and alpha-test threshold do not apply to these
sequence elements. Blend/filter/mask sampling is shared with the sequence-image
shader through `kh2_sequence_sample.hlsli`; unrelated DS polygons keep their
original rules. No intermediate transparent image is allocated or alpha-blended
over the world. This avoids dark glow disks, double attenuation, low-alpha
cutoffs and 5-bit alpha bands. The existing HD rendering mode retains 8-bit RGB.

Programs are retained separately for the four geometry-list slots, so preparing
a later frame does not overwrite the shown list's commands. The polygon's
`KHDAYS_G3D_SEQUENCE` flag selects this path; its palette word identifies the
retained program. Atlases are cached, and normal frames upload only commands.
The screen's existing fade and UI composition still apply. The software DS
renderer keeps the native markers. Both styles (including PNG fallback) receive
`KHDAYS_G3D_MARKER` only around the existing selection draws. They draw after
world geometry, independently of Z/W depth or translucent-enemy sort order,
without writing world depth. This does not promote them over 2D UI. The marker
scope is cleared even when replacements are unavailable or a draw is cancelled.

Days provenance: `khdays-decomp` `a2b0478b87e3102732094c34f6ca89595f98ab93`,
`Ov022_ReadSelectionInput` (`0x02085380`), `Ov022_UpdateSelectionMarker`,
`Ov022_ProjectToScreen` (`0x02085ec4`), `func_0201653c` (`0x0201653c`),
`Ov022_DrawSelectionMarkers` (`0x020863d0`), `Ov022_UpdateUiSelectionMarker`
(`0x02086138`), `Ov022_InitUiSubsystem`, `Ov022_LoadMarkerSprites`, and
`Ov002_DrawSpriteQuad` (`0x02050f50`). Hooks live in `port/tools/prepare.py`;
no gameplay state or generated/decomp source is edited. KH2 clip assignments
come from the maintainer; the supplied little-endian PC resource confirms the
timelines and loop points. Exact original-game reticle size, glow and timing still
need capture comparison.

Automated checks cover transitions, one-shot exits, restart on target change,
per-object settings, copied world anchors, reprojected exits, hidden/paused exit
timelines, retained list images, warm-frame allocations, HD-off and
missing-file fallback. `khdays-kh2-gpu-test --reticles ARCHIVE TEXTURE` compares
the actual six clips against the reference at 1x, 4x and 8x, both as images and
over a colored 3D background. Synthetic scene tests additionally cover all three
blend modes, their order, a full alpha ramp with the native alpha cutoff enabled,
black additive texels, masks, world depth, and the selection-only foreground
override. Native/PNG tests exercise Z/W buffering, matching translucent world
IDs, and opaque 2D elements above the marker. In-game validation:
approach/leave an NPC, soft-target enemies, toggle hard lock, switch targets,
move a target beyond the camera, pause, open a tutorial, and test widescreen and
two-screen modes. Also disable each object/remove its file and check HD PNG and
DS fallback; chain/error should stay native until explicitly configured.

## Provenance and supported behavior

The authority is the KH2 HD PS3 debug executable's symbols/disassembly, its DWARF
offsets, and the supplied little-endian PC resource bytes. The proprietary dump
is kept locally; none of it is a repository test fixture. Relevant original symbols:

| Behavior | Original symbol/address |
|---|---|
| Table layout | `YI::SPRITE::Init` `0xC6A28`, `YI::SEQUENCE::Init` `0xC23D8` |
| Frame bounds and looping | `GetAllFrame` `0xC2674`, `ChangeNumber` `0xC27A8`, `Control` `0xC3AC8` |
| Evaluation and parent attachment | `Calc` `0xC387C`, `Draw` `0xC3C58`, `UpdateActive` `0xC2D20` |
| Geometry and color | `SPRITE::SetRotate` `0xC67E0`, `_Draw2` `0xC8D80`, `MultiRGBA` `0xCA508`, `RateRGBA` `0xCA5E0` |
| Gauge construction | `GAUGE_PLAYER::createHP` `0xA36E4`, `createCircle` `0xA286C`, `createBar` `0xA2728` |
| Mask geometry | `CIRCLE_MASK::create` `0x64230`, `Ft4Base::calcXYZ` `0x9CEE8` |

The PC SEQD version is `0x160`; the header is 48 bytes and the record strides are
44/20/4/144/36 bytes. Unknown bits and clip parameters are retained. Sequence flags
are not blindly interpreted as runtime disables: the original Calc derives its
optimization state from the animation records. Palette selection and scrolling
currently produce an explicit unsupported-feature error when encountered in a
drawn part. The gaug fixture uses neither.

Playback evaluates whole source frames, includes animation endpoints except
`last_cut`, and discards overshoot on a loop transition. Restart frames outside
the visible animation interval are allowed. Tags are returned as evaluation data;
the caller controls callback dispatch, so extra draws do not trigger side effects.
The active attachment is optional; a future wrapper must decide how to retain a
previous active value when no active track is present.

The renderer supports mirrored and degenerate UV rectangles, four corner colors,
the projected X/Y/Z rotation matrix, scale, sway, and cubic easing using floats at
animation offsets 0x64 and 0x6C. Coordinates are quantized to the original 1/16
units. Packed modulation colors use 128 as unity; the replacement PNG uses ordinary
RGBA8. The logical 512x256 gaug atlas addresses the 1024x512 replacement without
changing geometry size.

## Remaining fidelity validation

This implementation has not yet been matched pixel-for-pixel against original
KH2 captures. In particular, `_adjustSPRT`'s resolution-dependent edge adjustments
are not emulated: the reference currently uses the sprite path's half-texel UV
and position convention. Filtering selected from the format and HD blend/clamp
rounding still need comparison against the original backend. The source clock
unit remains configurable until Vsync timing is verified. Sway uses full cycles
per eased interval; the disassembly establishes the multiplication by the sway
count, but the referenced period constant still needs independent confirmation.

The HP composition uses the original mask thresholds and depth ordering, and the
provided `CIRCLE_RAD = 0.078539819`. The supplied PS3 status tables decode as
normal/alert/dead circle clips 121/122/121, bars 124/125/124, and parent clips
137/132/137. Status and reaction are explicit inputs; the Days adapter supplies
them with the policy described above.

Days HP maps to KH2 HP as `days_hp * 10 / 31`: 186 is the 60-HP circular capacity,
372 is KH2's 120-HP extent, and the extension continues by `3 * (kh2_hp - 60)`
logical units without a gameplay cap. The parent position is normalized from the
asset's 455,355 center. The Days adapter places its own existing portrait artwork
over this gauge and advances before the display's unchanged-frame shortcut.

## Optional authored mission panel

The `mission_panel` object in `layouts/gauge/<language>/gauge.json` is disabled
by default. It is for a mod-authored BAR/SEQD and PNG or DDS atlas, not an asset
extracted from KH2. Put `mission.2dd` and `mission.png` next to that JSON, set
`enabled` to true, and restart. `archive`, `texture`, and the BAR `entry` (`miss`
by default) are configurable local names. The atlas uses the BAR image entry's
logical size, independently of its replacement resolution. The HD importer
preserves this section and never patches or replaces the custom mission files.

The example JSON lists every setting. The default animation assignments are:

| Setting | Clip | Use |
|---|---:|---|
| `reference` | 0 | Canvas extent only; authoring/debug artwork is never drawn |
| `intro`, `outro` | 1, 2 | Background, active parent, and shared fade/translation |
| `track` | 3 | Empty progress track |
| `fill`, `ready_fill`, `full_fill` | 4, 5, 6 | Below minimum, minimum reached, full progress |
| `heading`, `complete_heading` | 7, 8 | Mission gauge / mission complete label |
| `full_marker`, `minimum_marker` | 9, 10 | End marker and calculated minimum marker |
| `bead_empty`, `bead_full` | 11, 12 | Available/completed missions on a day |
| `unity`, `rank`, `chest` | 13, 14, 15 | Separate counter icons |

The background clips need an active track. Its resting location is the last
frame of `intro`; other components follow its relative translation and alpha.
`intro` honors its sequence loop/restart, whereas `outro` plays once. Pausing
freezes animation. Leaving the field, opening menus, cutscenes, and map changes
cancel the previous panel. The native panel's tilemap controls when it is shown;
rendering does not alter progress, collection flags, or other gameplay state.
No dummy tracks or extra sprite groups are required in the authored file.

Positions below are KH2 logical units before `scale` (0.65), and are relative
to the file's origin. `source_origin_x/y` defaults to 0,15. `offset_x/y` moves
the whole result in Days screen units. On one screen it uses the information
bar's top-left placement and HUD composition scale, bypassing the original
mission-panel crop and its scale. Both native and replacement information bars
cover it. On two screens it stays in Days' native lower-screen mission-panel area at Y=152.

| Anchors | Meaning |
|---|---|
| `description_x/y` | Left edge/top of the objective or localized DAY text |
| `tally_x/y` | **Right** edge/top of localized Missions Left text |
| `counters_right`, `counters_y` | Right edge of the counter row and icon origin Y |
| `icon_gap`, `counter_gap` | Icon-to-text gap and gap between complete blocks |
| `counter_text_offset_y` | Extra vertical adjustment of number text relative to the icons |
| `beads_x/y`, `bead_spacing` | First bead position and horizontal step |
| `bar_start_x`, `bar_end_x`, `bar_y` | Track endpoints and origin Y |
| `heading_x/y` | Pre-rendered gauge/complete heading origin |
| `full_marker_x/y`, `minimum_marker_y` | End-marker origin and minimum-marker Y |

Counter blocks pack leftwards in chest, unity, rank order from the right edge.
Zero-total blocks are omitted. Each block reserves its icon's bounds over the
whole animation, measured font width including bearings/shadow, and both gaps.
Description text fits horizontally around counter blocks only when their vertical
bounds overlap its row. Moving the counters to another row restores the full
description width. Replacement font cell heights, including shadows, are retained. Numbers use Days'
8-dot font, yellow with a white slash and black shadow, with no one-digit cap;
the label uses the 10-dot font. Both honor `field.hud.status` HD font mappings.
`text_scale` adjusts label text and `counter_scale` adjusts only counter text.
`counters_scale` (default 1, range 0.1 to 4) scales the entire chest/badge row,
including icons, text, gaps, and relative text offsets, around `counters_right`
and `counters_y`. It multiplies the existing `counter_scale`; it does not scale
the description, Missions Left text, or mission beads. Text pixels are
cached until their content or resolution changes; sequences render on the GPU.

The track and fill span the configured endpoints; fill is clipped with matching
UVs rather than stretching its texture as progress changes. It follows Days'
existing displayed 224-step progress, preserving catch-up timing. The minimum
marker uses `floor(goal * 224 / total)`, clamped to 223, mapped onto those same
endpoints; it is omitted when goal equals total. The complete label and green
fill wait for the displayed progress to reach the minimum; full displayed
progress selects the orange fill.

When disabled, missing, or invalid, the original panel remains. When active, its
BG2 artwork, BG0 text, panel sprites, and port-added badge overlay are suppressed
only in the panel area. The minimap backdrop remains independent of those
suppressed pixels. Edit settings and restart to retry a rejected asset.

Verified Days sources at decomp commit
`a2b0478b87e3102732094c34f6ca89595f98ab93`: `Ov002_DrawLoadedPortrait`,
`Ov002_DrawLoadedTally`, `Ov002_DrawNoticeGauge`, `Ov002_DrawMessageCaption`,
`Ov002_DrawMissionGaugeTrack`, `Ov002_StepProgressBar`, and
`Ov002_UpdatePageStamp`. Collection flags use the port's existing
`mission_list.c` and `badges.c` data sources. The configurable anchor positions
are mod policy, not inferred KH2 game behavior.

`khdays-kh2-mission-panel-test` uses synthetic art/state to check parent fades,
packing, multi-digit counters, fill geometry, cached text, pause, context loss,
JSON validation, and disabled fallback. Its optional `--asset GAUGE_JSON` mode
checks locally authored art without launching the game. Text-canvas and
single-screen tests also verify native suppression, independent scaling,
information priority, and restoration of the original crop. In-game validation
should cover a normal mission, a day list, challenge/Mission Mode, simultaneous
chest/unity/rank counters, pause, and entering/exiting a map, with the mod both
on and off.

## Optional field notices

`backpack_full`, `breakthrough`, and `locked_on` in `gauge.json` use the same
renderer and settings as `obtained`: `archive`, `texture`, `entry`, `animation`,
`scale` (0.65), `offset_x/y`, `source_origin_x/y`, `frames_per_tick`,
`text_offset_x/y`, and `text_scale`. All three default to `enabled: false` and
`animation: -1`; the importer adds these settings without enabling them or
inventing artwork. Select a clip in your own mod archive before enabling one.
Text-bearing clips need an active/dummy track. Its position, color alpha and
clip parameters attach Days' text, including font replacements, to the artwork.
`locked_on` replaces both the baked label and its animated icon, so that clip
should include both. It does not require a text anchor.

`loop` defaults to false for the backpack and breakthrough announcements and
true for the locked-on caption. A looping clip returns to its authored restart
frame and ends when Days dismisses the native notice. Non-looping clips use the
same full one-shot lifetime as obtained. Pause freezes playback, and scene,
menu and native-panel recreation cancel it. Temporary HUD hiding during movies,
events or tutorials preserves pending requests and ongoing playback, without
drawing over those screens. Missing files or invalid clips
retain the original drawing. Single-screen placement uses the same HUD scale
as obtained; the native anchors also work in dual-screen mode.

The adapter observes `Ov002_BlitTileRegion` without changing its writes. The
pinned Days `UI/btl/main.p2` entry 0 identifies the backpack panel as tags 78/93,
the breakthrough rectangle as 72, and the alternating locked-on frames as
60/61. The complete live rectangle is suppressed on BG3, with the announcement
text on BG1 also suppressed. Other layers and neighboring cells remain intact.
`Ov002_BuildTextItem` draws localized `UI/btl/<language>/text.s.z` record 8,
"New breakthrough!"; its glyph operations are retained separately from its
background. Backpack item names use the existing private pickup capture and
its shifted cache placement. No inventory, mission, or targeting logic changes.

The synthetic obtained tests also exercise these notices, their native layout
bounds, dismissal, scene reset, text attachment, and missing/disabled fallback.
They also check notifications requested while the HUD is hidden and notifications
hidden mid-animation, including the obtained, backpack, recon and locked-on boxes.

Pending notifications claim their native box immediately, before text is ready.
The obtained/backpack sequences use their private captured glyphs independently
of the original item-name tilemap. This prevents DS artwork appearing while the
native upper INFORMATION banner restores the HUD layers in separate uploads.

Notification suppression outlives the native close request: the tilemap clear is
uploaded later. All four replacements retain the exact visible native map
entries and character bytes independently on BG1/BG3 until those cells change.
This hides the last resident DS picture and glyphs without delaying the KH2
animation or masking later content that reuses the same area. Scene/context
resets and disabled or invalid replacements release this coverage. Synthetic
tests delay clears across multiple frames, clear art/text independently, and
check both map-entry changes and in-place character clearing.

## Heart point counter

`heart_points` in `gauge.json` replaces Ov002's heart-counter nodes with GPU SEQD
commands in the original 3D HUD layer. Missing/disabled/invalid replacements and
the reference-only renderer keep the native draws. Exact node matching leaves
the shared-sheet countdown, world markers and HP/MP follower untouched. The
existing HUD composition places the counter in one-screen mode; animation bounds
use a stable composition anchor, so expanding effects do not change its placement.

The pack builder runs `tools/hd/heart_points.py` against the user's extracted
Days data. It writes `hearts.png` (256x128), `hearts.2dd` (BAR `hart` image/sequence
entries, types 0x18/0x19), and `hearts-reference.json`. The atlas includes 32 source
textures, assembled with their own palettes. Material texture transforms and
repeating/mirrored coordinates become explicit cropped/flipped parts in sprite
groups. The unused minus and HP/MP frame art are included for editing; exporting
those textures does not replace their other native uses. No game art is tracked.

The model conversion is headless: `khdays-native --export-heart-model INPUT OUTPUT`
uses the existing BMD0/BCA0 decoders. It does not run the game. Source provenance:
`UI/hcnt/arc` entries 0-6 and localized `UI/hcnt/arc_<language>` entries 0-2;
`Ov002_SceneResetPanelWidgets`, `Ov002_SceneDrawPanelWidgets`,
`Ov002_SceneStepPanelCounters`, `Ov002_SceneDrawPanelRowCounts`,
`Ov002_SceneStepPanelBackdrop`, `Ov002_SceneStepPanelFlash`,
`Ov002_SceneStepPanelRowFx`, and `Ov002_SceneStepPanelBlink` in the pinned
`external/khdays-decomp` revision. Native fields named tint/depth in several of
these functions are model scale vectors; they are not RGB colors.

Days supplies the displayed total, visibility and pickup/row/bonus triggers;
replacement motion, alpha, scale, loop flags and durations come from the 2DD.
The original total remains capped at 9999 (`Ov002_MaxValueForDigits(4)`), and the
native queue has ten floating rows. Both reward row types select the plus sign:
white ordinary rows and cyan bonus rows. The counter's two original joint tracks
have 18 and 12 frames; their material alpha tracks are converted with them.
The remaining effects are generated from the native code/overlay tween tables.
The generated timing uses the normal Days update rate of 30 frames per second.

The 43 default animation assignments are:

| Clips | Setting(s) | Purpose |
| --- | --- | --- |
| 0-1 | `heart_idle`, `heart_active` | Held first pose and free-running heart, respectively |
| 2 | `pickup` | Pickup light/ring cycle; native count-up holds it open |
| 3 | `background` | Counter underline/background |
| 4-7 | `streak`, `particles_0` through `particles_2` | Bonus effects |
| 8-10 | `chain_intro`, `chain_loop`, `chain_outro` | Localized CHAIN label |
| 11 | `bonus` | Localized BONUS flash |
| 12-21 | `digit_0` through `digit_9` | Total's digits |
| 22-31 | `small_digit_0` through `small_digit_9` | Floating reward digits |
| 32-33 | `minus`, `plus` | Symbols; minus is available but unused by default |
| 34-35 | `gain`, `bonus_gain` | Floating row's active anchor and color/alpha |
| 36-39 | `total_gain`, `total_loss`, `total_gain_medium`, `total_gain_large` | Total-digit active anchors |
| 40 | `heart_grow` | Heart's growth anchor; returns to resting size after 150 ms |
| 41 | `total_gain_final` | Final increment's scale pulse, with opaque digits |
| 42 | `chain_warning` | Repeating 300 ms native warning fade |

Edit `heart_points.animations` to reassign clips; `-1` omits a component. Common
settings are `enabled`, `archive`, `texture` (PNG or DDS), `entry`, `scale`,
`offset_x/y`, `source_origin_x/y` and `frames_per_tick`. Defaults use scale 0.65
and retain Days placement. `total_x/y`, `digit_spacing`, `rows_x/y` and
`row_spacing` expose numeric placement in native screen units; the root scale
then scales the counter proportionally. Gain and total-motion clips use an empty
sprite as an active anchor, like text anchors: their translation, scale and RGBA
apply to the dynamically selected number sprites. Digit clips can animate too.

Automated checks cover independent replacement lifetimes, pause, native fallback,
shared-node exclusion, mirrored groups, fractional-frame interpolation, and
GPU/reference output including W-buffer depth. Manual comparison is still needed
for transient visual fidelity: steady totals, pickups of different magnitudes,
white/cyan rows, CHAIN/BONUS refresh, pause, scene changes, and both screen modes.
The code-generated total-digit pulse uses an editable 100 ms clip; it approximates
the original gap-dependent 30-100 ms tween rather than overriding a mod's timeline
from native tween completion. `total_loss` is neutral by default: native snap/reset
updates do not start the increment pulse.

The state audit follows `Sequence_UpdateTracks` and the actual node flag writes:
`RewindWidget(0)` sets flag 2 and holds the first pose; `(1)` clears it and advances.
The helper names and their earlier source comments imply the opposite. Pickup
updates before drawing, so its exported twelve-frame cycle starts at source frame
1; the active heart draws before updating and starts at frame 0. Both geometry
and material alpha follow that order. The background and 22 digit/symbol clips
use their static native models. Floating rows retain the native white/cyan tint,
1000 ms movement/fade, and plus-sign placement.

The flash and three particle clips sample their own native tween tables with the
native phase scheduler: completed phases start on the next update, and drawings
use the preceding update's sample. Their geometry comes from the source models,
not assumed texture dimensions. A repeated bonus cannot interrupt an active
effect; native phase state can queue its next cycle. BONUS uses native quadratic
scale/alpha curves and includes its transparent 1000 ms endpoint. CHAIN's warning
lasts 300 ms by default and follows each native fade-tween restart. The final
removal uses a separate empty one-frame outro by default, so it does not extend
the native warning past the label's disappearance. Older JSON files without
`chain_warning` retain their `chain_outro` assignment for warnings. Growth uses native curve 3 and
restores the base scale when finished. Every default clip is checked on the GPU
at every integer frame, against the independent reference rasterizer.

Only native sequence HUD polygons retain offscreen X/Y bounds before single-screen
placement. This preserves enlarged art above the DS viewport until its final HUD
position is known; original DS polygons and world reticles keep their normal clipping.

The pickup is a held cycle, not a single shot: `SceneDrawPanelWidgets` keeps
+0x48 armed across animation wraps while +0xd58 says the count-up is moving.
The replacement's default clip 2 therefore loops until the native hold ends,
then completes its current custom cycle. Its rewind request arrives after the
native widget draw, so it is applied on the next update. Animation 1 retains
its separate +0xe4 state; growth while collecting is not sufficient to select it.

CHAIN warning flashes repeatedly restart the backdrop tween at +0xfd0 even
while the fading flag +0xd0 remains set. The controller follows the 64-bit
start stamp at +0xfe0, including the native transparent boundary frame between
warning cycles and immediate visibility when an active chain is refreshed.

Total-digit opacity runs from 31 down to 16 and returns to 31 when the tween
finishes; floating-row and CHAIN fades retain their native 5-bit alpha steps.
The last increment starts a scale tween without restarting the opacity tween.
New packs expose `total_gain_final` at clip 41 for that distinction. It defaults
to -1 in older JSON files, which retain their existing normal-gain assignment.
Snap/reset updates do not synthesize an animated count-up.

The exporter compacts sampled conversion data before writing SEQD: identical
parts, compound sprite groups, and empty anchors are shared; linear and cubic
segments replace redundant frame records. Every accepted segment retains sampled
integer translations, colors, and quantized quad positions. Curves with different
scale/alpha progress retain the necessary separate keys because SEQD uses one
time curve per element. The native evaluator compares every frame of compact
and uncompacted output, including UVs, colors, durations and active anchors
(anchor scales use a 0.0001 tolerance). `hearts-reference.json` also lists the
sprite-group references of each animation so editor indices can be checked.

Single-frame holds use positive half-open intervals, so editors never display
zero-duration layers. The current 43-clip export reduces 315 animation records
to 171, with 35 parts and 32 shared sprite groups.


## Optional HUD presentation and pause target

`gauge.json` now accepts a root `presentation` object. All switches default to
false; existing layouts and dual-screen HUD positioning retain their previous
behavior. Restart the game after changing these settings.

```json
"presentation": {
  "keep_hud_during_information": false,
  "align_hud_to_edges": false,
  "pause_mission_target": false
}
```

`keep_hud_during_information` retains the HUD during a native upper INFORMATION
banner when its KH2 replacement is available. The banner-specific HUD fades, map suspension, original banner-art callback and HUD scroll
write are bypassed; its message, snapshot lifetime, state transitions and timing remain
native. Pause, tutorial and cutscene hiding retain their normal paths.
Command navigation (including the port's D-pad enhancement), shortcuts and selected
command types also remain available. Actor-state, pause and scripted input locks
are unchanged. Pickup, interaction and panel scheduling gates stay native to avoid
starting conflicting requests on the active INFORMATION controller.
The command hook adapts ov022 `Ov022_UpdateCommandInput` (0x020a2978), from
`khdays-decomp` a2b0478b87e3102732094c34f6ca89595f98ab93. Its local panel-open
queries, including the port control-sampling hook, use a command-only predicate;
`Ov002_Hud_IsPanelOpen` itself is unchanged. Synthetic actor tests execute the
generated dispatcher across panel states 0..11, both command-list modes and both
control schemes, checking navigation, shortcuts, command arguments and pause,
scripted-control and zero-HP suppression, with the option enabled and disabled.

`align_hud_to_edges` removes the deliberate outer field HUD inset in single-screen
mode. The command menu touches the bottom and left edges and the HP gauge reaches
the bottom and right edges. Internal spacing, including the heart counter beneath
a visible mission panel and the padding around the minimap, is preserved. It does
not change dual-screen positioning or the selected HUD size.

The existing `mission_panel` object also accepts `pause_only` (false),
`pause_offset_x` (0) and `pause_offset_y` (0). The pause offsets add to its ordinary
position, in the same coordinate units as `offset_x/y`. With `pause_only`, native
panel tiles remain suppressed during gameplay and the heart counter returns to
its usual top-left position. The panel is drawn while paused without freezing on
an invisible entrance frame. A missing/disabled replacement retains native behavior.

Optional `mission_panel.anchor` and `pause_anchor` accept `top-left`, `top`,
`top-right`, `middle-left`, `middle`, `middle-right`, `bottom-left`, `bottom`,
and `bottom-right`. `pause_anchor` inherits `anchor` when omitted. An omitted
`anchor` (or `native`) retains the existing placement. Use `pause_anchor: "bottom"`
with zero pause offsets to center the paused panel against the bottom edge.
Offsets add to the selected position: positive X moves right and positive Y moves
down; negative Y adds space above the bottom edge. Normal offsets still apply
while paused, with pause offsets added. Explicit anchors align the resting
background artwork, excluding transparent canvas padding, and preserve animation
motion. Single-screen placement uses the current viewport and HUD size; on two
screens it uses the native lower screen. Anchoring overrides the implicit top
inset, independently of `align_hud_to_edges`. No anchor preserves legacy layout.

`pause_mission_target` adds the native TARGET artwork between the PAUSE header and
the buttons in an ordinary gameplay pause. It uses a 0.65 scale and moves the menu
pieces to make room. A world-only panel, cutscene pause, or unavailable resource
keeps the normal pause. The target is decoded from its native D2KP palette, tile
and map resources, rather than sampled from the composed lower-screen framebuffer.
Existing pause PNG replacements are retained; native tile artwork is the fallback.

Provenance: `external/khdays-decomp` a2b0478b87e3102732094c34f6ca89595f98ab93,
overlay 002 `Ov002_RequestPanelScreen`, `Ov002_FinishPanelFadeIn`,
`Ov002_QueuePanelGraphics`, and `Ov002_RecolourGaugeBlock`. The field context
referenced at 0x0207f630 stores the target selector at +0xac: negative means the
world panel, zero selects locale map.p2 entry 11, and 1..61 select mapchr.p2
entries selector+0x2f. Native text and mission logic are not changed.

Regression coverage checks unchanged default composition output, command/HP edge
placement across HUD sizes/aspect ratios, hidden-panel counter placement,
configuration parsing, paused panel offsets and lifetime, and target tile palette,
flip and bounds handling. In-game comparison still needs a mission target and a
world-only mission, ordinary/cutscene pauses, top INFORMATION and tutorials, in
both screen modes with the switches on and off.


The upper INFORMATION fade option checks configured resource availability rather
than this frame's drawing eligibility: a mission script can request the banner
while the replacement renderer is temporarily suspended. That must not start a
native HUD fade when keeping the HUD is enabled. Single-screen composition also
allows the HUD during that explicit upper-information state regardless of script
activity, while ordinary dialog, movie, tutorial and pause contexts remain hidden.

The native pause fallback keeps the title's original 12-by-2 tile proportions.
Retry and Withdraw reuse the existing cursor-specific cached native line states;
they must not both inherit the currently lit Give Up row. Once the target menu
has been fully prepared, it owns the original pause BG layer, including tiles
outside the piece descriptors. The old single-screen pause crop is omitted as
well. Synthetic native-tile tests cover header proportions and each Retry cursor
state; composition tests check that no original pause crop remains behind the
independent target menu.


INFORMATION normally snapshots and clears BG1/BG2/BG3 through ResetFaders.
With keep_hud_during_information, all three maps remain live: BG1 contains command
labels, enemy names and notification text as well as the caption. The native
snapshot node still has a balanced push/pop lifetime, but has no saved HUD maps
to overwrite live updates on exit. Only the caption scratch rows at map offsets
0x700..0x7ff are saved/restored separately. Other snapshot callers are unaffected.
The original banner-art callback is skipped for this option.

BG1 and BG3 retain their mandatory native scroll callbacks. The BG3 callback
suppresses its register write; BG1 records the caption scroll without moving the
physical text layer. Only INFORMATION canvas discovery and compositing use that
virtual scroll, for original and replacement-font glyphs. All other text uses
the physical layer coordinates. The main-screen resting scroll offsets are also
preserved through the transition, preventing the brief return-position snap.
Native-code tests cover all three maps, live text/art updates, caption scratch
restoration, nested snapshots and both option states. Font tests cover virtual
caption discovery and command/obtained/backpack/recon text while INFORMATION is
active. Map composition tests compare gameplay/pause rectangles for both the
rectangular and KH2 minimap when edge alignment is enabled.
The PAUSE title ends six native layout units above the target, for either native
art or a replacement PNG, using its actual displayed height.


The BG3 scroll callback at panel +0x234 is mandatory: UpdatePanelScroll calls it
without a null check while the offset tween is active. Keeping the HUD must leave
that callback installed. The port instead suppresses the write inside its target,
Ov002_SetBgOfs_main001c, when the option is enabled. The native-code regression
runs UpdatePanelScroll through that callback with an active tween and verifies
that disabled settings still write the original scroll value.


## Field prizes

A mod can give any of the field's prize pictures (`mo/prize.pak.z`: `ef_pp`, `prize_01` the HP orb, `prize_03`
munny, `prize_05`-`prize_07` the item spheres, `prize_08`, `prize_10`, `prize_11`, `he_prize`, `born`) an animated
effect drawn instead of it, on the same native sequence route as the reticles. The game still places, moves, fades
and hands out its prizes: only the picture changes (docs/RUNTIME_MAP.md section 33).

```text
mods/<Mod>/graphics/mo/prize.pak.z/fx/
├── <picture>.txt     # the effect: prize_01.txt, prize_03.txt ...
└── <page>.png        # its pages, any size (RGBA)
```

The file is text, one statement a line, `#` a comment:

```text
effect 1                         # the format (1); first
page <n> <file>                  # page n, a PNG beside the file
body <layer>                     # the first frame's layer made as wide as Days's picture (default 0)
frame_time <frames>              # game frames each frame shows (default 1; the field runs 30 a second)
frame                            # a new frame; its layers follow, drawn in order
layer page <n> rect <u> <v> <w> <h> corners <x0> <y0> <x1> <y1> <x2> <y2> <x3> <y3> colour <r> <g> <b> <a> blend <how>
```

`rect` is the layer's part of its page in fractions (left, top, width, height; w and h above 0); `corners` its
top-left, top-right, bottom-left and bottom-right in the effect's own units (x right, y down, 0 the prize's middle);
`colour` multiplies the page (0..2, 1 leaves it); `blend` is `alpha` or `add`. The frames loop and stand still while
the game is paused; each prize plays them from the first when it shows, as KH2's prizes do (an effect of more than
128 layers in all, too long for one sequence program, shows the same frame on all its prizes). The node's fade
multiplies them. A file that
breaks a rule (another version, a page not declared or not readable, no frame, a frame with no layer) is named once
in the log and the picture keeps Days's look. Mods are taken in the player's order; there are no limits on frames,
layers or pages.

`tools/mods/kh2_prizes.py --kh2 <kh2.hed_out>` writes KINGDOM HEARTS II's: the HP orb (`obj/PRIZE.mdlx`'s
animation 1) for `prize_01` and munny (animation 0) for `prize_03`, with the remaster's HD pages
(`remastered/obj/PRIZE.mdlx/-N.dds`) when there, from the player's own 1.5+2.5 copy. The installer makes it with
the HD menus (`build-mods --kh2-prizes`) into the HD PACK. The settings' HD replacements have "KH2 prizes and
chests" (`kh2_prizes`, on by default); off, Days's pictures are drawn exactly as before. The reference renderer
draws Days's pictures. `KHDAYS_TRACE_PRIZES=1` names the pictures as they load, each effect found or refused, each
prize taken and the player's HP and munny.

## Live Enhancements preferences

The Enhancements menu stores HUD choices only in the usual settings.ini under
[enhancements]. It never writes gauge.json, minimap.json or font metadata.
Replacement switches default on and act as additional gates: disabled JSON entries,
missing resources and invalid assets still retain their normal native fallback.
Player/party HP includes HUD portraits, player-number labels and LIMIT text.
Notification boxes groups Obtained, Backpack Full and Recon Breakthrough; LOCKED ON
has its own switch. The KH2 minimap preference changes only single-screen map
presentation; map textures and icon replacements remain independent.

UI padding is saved as ui_padding and defaults to off. For an active HD
presentation in single-screen mode, off removes the existing outer inset and on
restores it. This menu choice takes precedence over presentation.align_hud_to_edges
in memory without changing the JSON file. It applies immediately and leaves
HUD size and dual-screen placement unchanged.

Mission visibility and both nine-position anchors are saved menu preferences,
taking precedence over their JSON counterparts in memory. Defaults are Always,
Top left in gameplay and Bottom while paused. JSON scale and offsets still apply.
The file stores visibility as 0 (Always) or 1 (Pause only), and anchors as 1..9 in
row-major order from top-left through bottom-right.

Preferences are published atomically and applied without restarting. Cached text
is invalidated on a preference change, including an active obtained-item canvas.
Resources remain cached across off/on changes. Notifications continue observing
native requests while disabled so enabling a still-visible box works. Font
character mappings remain bound while disabled so newly loaded fonts can later
use HD replacements without reinitialization.

The INFORMATION replacement and Allow HUD/control choices are latched for native
upper-panel states 1..8. An already-open message completes its balanced map
snapshot/scroll transition before adopting a changed preference. Lower-screen
messages and other replacements switch when the game resumes. Pause, dialogue,
cutscene and scripted control restrictions are preserved. Allow HUD/control also
requires presentation.keep_hud_during_information and a valid INFORMATION asset.
The pause-target preference likewise gates presentation.pause_mission_target.

The saved keys in `[enhancements]` are:

| Key | Default | Scope |
|---|---|---|
| `hd_font` | on | Original/HD text artwork and layout, including cached captions |
| `kh2_minimap` | off | Single-screen radar presentation; icon replacements remain independent |
| `party_hp_sequence` | off | Player/party gauges, HUD portraits, player labels and LIMIT |
| `enemy_hp_sequence` | off | Enemy bar and tanks; native enemy names remain |
| `mission_panel_sequence` | off | Mod-authored mission panel |
| `information_sequence` | off | INFORMATION background, reveal and closing animation |
| `target_sequence` | off | Soft/hard lock, interact and optional chain/error reticles |
| `locked_on_sequence` | off | Top-screen LOCKED ON notice |
| `notification_sequences` | off | Obtained, Backpack Full and Recon Breakthrough |
| `heart_points_sequence` | off | Heart total, effects and chain counter |
| `pause_target` | off | Mission target in ordinary pause menus |
| `information_hud` | off | Configured HUD/control retention during upper INFORMATION |
| `ui_padding` | off | Off removes the single-screen HD presentation inset |
| `mission_panel_visibility` | 0 | 0: Always; 1: Pause only |
| `mission_panel_anchor` | 1 | Gameplay anchor, 1..9 |
| `mission_panel_pause_anchor` | 8 | Pause anchor, 1..9 |

The main HD graphics switch also gates gauges, portraits and labels. Turning it
off releases their native tiles; turning it back on reuses the loaded resources.
Mission-panel font routes are bound even when the HD font preference starts off,
so enabling fonts later can select the configured scoped artwork.

Regression coverage includes settings-file round trips, missing-setting defaults,
menu rows and localized choices, live renderer gates, JSON precedence, retained
map icons, font mapping/cache reuse and INFORMATION transition latching.

## Pull request validation

Build the existing release tree, then run its complete CTest suite. The HUD tests
use synthetic layouts, palettes, text and game state; the GPU suite compares
rendered output with the CPU reference. `port/tools/analyze.py` checks the port's
own native sources. Run Ruff on changed Python files and clang-format on changed
C/C++ files; do not format generated shader headers or decompilation sources.

The generated shaders are checked in for DXIL, SPIR-V and MSL. A Windows run
validates the available graphics backend, not every platform. Before merging,
compare the following in game with HD graphics on/off and each replacement on/off:

- Single and dual screens, narrow and ultrawide viewports, and multiple HUD sizes.
- All party slots, enemy bar rollover/death, LIMIT and label animations.
- Native upper/lower INFORMATION, pause, tutorials, cutscenes and map changes.
- Notifications collected while the HUD is hidden, including full inventory/recon.
- Mission counters, long descriptions, paused anchors and target/no-target pauses.
- Minimap terrain replacements, smooth icons, character overrides and edge fades.
- Font toggles on an already-visible caption and command menu, and a mission panel
  first loaded with font replacement off.

Only local generated assets are used for optional visual tests. Extracted game
files, authored mod packs, screenshots and disassembly stay outside the commit.
