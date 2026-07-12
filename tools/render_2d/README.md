# Nitro 2D renderer — atlas fix

This version fixes two issues found with the real KH Days files:

1. NCLR/NCGR payload pointers are relative to the section body, so the real
   payload starts eight bytes later than the stored pointer value.
2. Some D2KP resources contain a trivial NSCR map while the NCGR itself is a
   complete sequential tile atlas. In `auto` mode, the renderer detects this
   case and exports the NCGR atlas directly.

## KH Days Game Over resource

```powershell
py .\tools\render_2d\render_2d.py `
  --container ".\data\extracted\1ecf5e7a41a2ae48\decompressed\unpacked\gameover\gameoverbg_es.pbg" `
  --output ".\data\preview\gameoverbg_es_atlas.png"
```

Expected result for the supplied Spanish files:

```text
Mode:      atlas
Size:      256x160
Color:     4bpp, 256 colors, bank 1
Tiles:     640
Map cells: 1024 (1 unique)
Missing:   0
```

The generated image is a sprite/UI atlas, not a composed 256×256 screen.

## Force behavior

Force atlas rendering:

```powershell
--mode atlas
```

Force NSCR rendering:

```powershell
--mode screen
```

Force a specific 4bpp palette bank:

```powershell
--palette-bank 1
```
