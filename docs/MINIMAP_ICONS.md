# Minimap icon replacements

The mod's `layouts/minimap/minimap.json` replaces only the dynamic minimap icon
layer. Its `texture` names a PNG or DDS in the same folder. `enabled: false`, a
missing JSON or texture, disabled HD graphics, or invalid settings keep the
current Days icon rendering. Restart after editing settings or images.

The HD asset builder exports a native-resolution `minimap.png` and complete
`minimap.json` from the user's extracted Days data. It preserves an existing
minimap JSON and its named texture when rebuilding a pack. Generate them alone:

```powershell
python tools/hd/minimap_icons.py --data data/extracted/<hash> --output "mods/HD PACK/layouts/minimap"
```

No extracted sheet is committed. [Example settings](examples/minimap.json) use
the exporter's default arrangement, with replacement disabled. The exported
JSON enables the sheet and KH2-style map; smooth rotation and character
overrides remain off.

The HD builder also copies KH2's remastered `radar.tm2/-0.dds` from `kh2_first`
to `layouts/minimap/radar.dds`, preserving its alpha gradients and resolution.
An existing custom `radar.dds` is preserved on pack regeneration. For a standalone
export, add `--kh2 <extracted-kh2_first-root>` to the command above. This asset
supplies the KH2-style map mask. Enable `kh2map` to use it in single-screen mode.

The HD pack builder enables `kh2map` by default; an omitted field in another mod
still defaults to false. When enabled, the room's loaded Days tile image is
rendered from its resident VRAM tiles through KH2's 96x96 radar mask, independently
of the bottom-screen crop. Days' world-to-map conversion and scroll origin are
unchanged.
Existing indexed and RGBA HD tile replacements are sampled through the normal
tile-replacement lookup. Terrain is cached and uploaded only when its pixels,
palette, or replacements change; map scrolling, icons and masking run on the GPU.
Source tracking bounds the resident tile run; the renderer does not retain or
read the NCGR upload buffer that Days frees after uploading it. The temporary
upload-state byte can be zero while the map remains resident and visible.
The resident NSCR supplies tile placement, palette banks, and flips. Its map
artwork is packed 24 tiles across, with repeated padding cells in the surrounding
32x32 BG grid; the texture sheet is not interpreted as a 32-column bitmap.
At the outer map boundaries, terrain sampling clamps to the edge texels across
the radar window. It does not repeat the room artwork or expose a differently
coloured backing when the player approaches the edge of the image.

`map_scale` defaults to 0.65 and follows the same HUD sizing convention as the
other KH2 replacements. `map_offset_x` and `map_offset_y` move the entire map.
`map_opacity` defaults to 0.75. The HD pack builder sets `map_icon_scale` to 1,
keeping an 8-pixel Days icon at 8 units before the common UI scale. An omitted
field in another mod retains the renderer's fallback of 2. The existing
16-pixel center emblem keeps its size. The DDS supplies the 8-pixel edge strips
and mirrored corners around an 80x80 center. Source: `dk::RadarRect::createRect`
(0x73460), `updateRect` (0x732D0), and `Radar::create`/`draw` (0x739C8/0x73FD8).

Dual-screen rendering is unchanged. Missing masks, unavailable terrain, and
more than 127 captured icons retain the existing map presentation. With
`kh2map: false`, no native radar image is composed and the existing map crop
and backing remain in use. This replaces field/pause radar presentation only;
it does not replace map or other menu pages.

## Atlas and sprites

`logical_width` and `logical_height` default to 256 and 128. Rectangles in
`sprites` always use those logical units. A 512x256, 1024x512, or larger equal
integer multiple PNG/DDS needs **no coordinate changes**. Replacing the sheet
changes only the icons, not the map's terrain, outlines, scrolling, or minimap
composition. Extra transparent cells in the default atlas are available for
custom sprites.

Each named sprite accepts `x`, `y`, `width`, `height`, `draw_width`, `draw_height`,
`pivot_x`, `pivot_y`, `scale`, `offset_x`, and `offset_y`. Draw dimensions default
to the rectangle dimensions; the pivot defaults to its center. Offsets are in
native minimap pixels. The game still selects the icon, position, visibility,
blink variant, party eligibility, and draw order. Artwork is rendered on the
GPU, clipped to the native 120x80 icon surface, and follows the original minimap
layer through both single- and dual-screen composition.
In single-screen mode, icons are composed separately from the map's white
transparency key and floor-colour filter, so replacement artwork keeps its own
RGBA colours. They retain the map crop, edge fade, placement, and native fade.

| Sprite names | Native use |
|---|---|
| `player1_0` ... `player4_7` | Four player colors, eight directional bitmaps per player |
| `emblem1` ... `emblem4` | The local player's assembled 16x16 center emblem |
| `entity_0`, `entity_1`, `entity_2`, `entity_4`, `entity_5`, `entity_6` | Every drawn entity-icon kind; kinds 3 and 7 intentionally draw nothing |
| `marker_120` | Spawn/goal-position marker |
| `marker_280`, `marker_360`, `marker_3a0` | Fixed-object marker and its two actionable variants |
| `marker_380` | Link-item marker |
| `source_00` ... `source_31` | All original source glyphs, for reference or custom mapping |

Entity-kind names deliberately retain their source numbers rather than guessing
an object's gameplay identity from its picture. The sheet includes the native
check/portal-style artwork among those entries and marker variants.

## Player and character enhancements

The `players` object uses keys `"1"` through `"4"`, corresponding to the native
player/color IDs, not HUD display order. Each entry accepts `enabled` (default
true), `sprite` (default empty), `smooth_rotation` (default false), `scale`
(default 1), and `rotation_offset` in degrees (default 0).

`sprite` can name an atlas rectangle or a standalone PNG/DDS file in the same
folder, for example `"sprite": "key.png"`. Standalone images preserve their
aspect ratio and fit an 8x8 minimap-pixel area before applying `scale`; their
file resolution does not change the displayed size. Use north-facing artwork.
Files are packed into the GPU atlas once at load time. The HD builder preserves
these referenced files alongside the shared atlas and JSON.

An empty sprite with snapping enabled retains the eight native directional
images. With `smooth_rotation: true`, the north-facing `playerN_0` sprite uses
the actor's full 16-bit heading instead. Supplying `sprite` selects a custom
north-facing sprite, rotated either continuously or in the native 45-degree
steps. Positive rotation offsets follow the native counterclockwise direction.

```json
"players": {
  "1": {"sprite": "player1_0", "smooth_rotation": true, "scale": 1},
  "2": {"sprite": "player2_0", "smooth_rotation": true, "scale": 1}
}
```

`characters` accepts the same character names as the portrait system, including
`Axel`, `Xaldin`, `Roxas`, `Xion_Hood`, and `Roxas_Hood`. Add an atlas rectangle
under `sprites`, then enable its named override:

```json
"characters": {
  "Axel": {"enabled": true, "sprite": "axel_custom", "smooth_rotation": true, "scale": 1}
}
```

An enabled character override with an existing sprite takes priority over its
player entry. An unavailable optional character/player sprite uses the native
player entry in the replacement sheet, retaining continuous rotation if enabled.
Character identity comes from
the HUD's already mapped member-ID/portrait-ID records, preserving Mission Mode
and multiplayer ordering. The separate center emblems remain configurable under
`sprites.emblemN`.

## Implementation and validation

Source: pinned decompilation `a2b0478b87e3102732094c34f6ca89595f98ab93`,
`Ov002_RedrawMapPanel`, `Ov002_PlotMapIcon`, `Ov002_PlotPageGlyph`,
`Ov002_PlotPageEmblem`, `Ov002_StampCellBlock`, `Ov002_ScreenToCell`,
`Ov002_UpdateCursorCell`, and `func_ov022_02088254`. Capture hooks are generated
by `port/tools/prepare.py`; the decompilation is unchanged. Native stamping
continues, allowing fallback without reconstructing the original raster.

The source is `UI/btl/bm_lo_bg002.NCGR.z`, not map terrain in `mapchr.p2`.
Palette bank 13 of `bm_lo_bg000.NCLR.z` is selected by the native icon tilemap
(verified in `main.p2` entry 0). The tile sequence 0x139..0x1CE begins at map
cell 13,7, or 104,56 pixels, with fifteen tiles per row. Only that palette/tile
range in the detected icon layer is suppressed, after successful GPU setup.
The player's original masks are 0x22/0x33/0x44/0x55. Each center emblem is four
tiles assembled in a 2x2 arrangement. The directional formula is
`((heading + 0x1000) / 0x2000) % 8`; continuous rotation uses the same heading
and sign before rounding. Native mode compares against the exported bitmaps;
continuous rotation is an intentional optional enhancement.

`khdays-minimap-test` checks coordinates, 45-degree vs continuous rotation,
character selection, missing optional sprite fallback, native layer isolation,
ordered GPU batches, menu/reset behavior, and overflow fallback. Its optional
`--asset MINIMAP_JSON` validates a local atlas. The Python exporter test uses
synthetic glyphs/palettes to check all player masks, palette selection, emblem
assembly, and preservation of edits. Icons beyond the visible surface are
culled; more than 512 visible captures retains native rendering for that frame.
In-game verification should cover map movement, all four players, portals/check
markers, blinking, pause, both screen modes, and the disabled mod.
