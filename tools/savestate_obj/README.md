# Savestate OBJ renderer

Development/analysis tool that renders the OBJ (sprite) layer of a **DeSmuME
`.dst` savestate** to a PNG. It reads a screen's real sprite layout out of the
running game — the same runtime-measurement method used to lay out the character
select, the save screen and the difficulty selector, instead of guessing.

It reads only a savestate the user supplies. It does **not** modify, copy or
distribute the ROM or the game.

## Usage

From the `khdays-port` root (run with `py`/`python` 3.13 — it needs Pillow):

```powershell
py .\tools\savestate_obj\savestate_obj.py in.dst out.png
```

Options:

- `--screen top|bottom` — which engine (default `bottom`; Engine A = top, B = bottom).
- `--bank A..I` — VRAM bank holding the OBJ tiles (default `D`, the Days sub-OBJ bank).
- `--bpp 4|8` — colour depth (default `8`; read OAM attr0 bit13 to confirm per screen).
- `--unit N` — bytes per attr2 tile-number step (default `128`).
- `--scale N` — nearest-neighbour upscale (default `3`).
- `--dump-oam` — also print the raw sprite table (positions/sizes/tiles).

Example — the difficulty selector's sprite table plus a 3× render:

```powershell
py .\tools\savestate_obj\savestate_obj.py difficult.dst difficulty.png --dump-oam
```

## How it works

The savestate is `zlib`-decompressed from offset `0x20`; the decompressed stream
is a list of `tag[4] ver[4] size[4] data` chunks. The tool locates these by tag:

| chunk  | size      | contents |
|--------|-----------|----------|
| `WRAM` | 0x400000  | main RAM (`0x02000000`) |
| `LCDM` | 0xa4000   | all VRAM banks A..I concatenated |
| `VMEM` | 0x800     | palette RAM (OBJ at +0x200 top / +0x600 bottom) |
| `OAMS` | 0x800     | OAM (Engine A at +0x000, Engine B at +0x400) |

VRAM bank offsets inside `LCDM`: A/B/C/D = `0x20000` each, E = `0x10000`,
F/G = `0x4000`, H = `0x8000`, I = `0x4000`.

The confirmed pipeline for the Days front-end bottom screen is **bank D, 8bpp,
byte offset = `tile * 128`, 256-colour palette at `VMEM+0x600`** (palette index 0
transparent).

### Caveat: VRAMCNT is not readable from the savestate

DeSmuME does not persist usable `VRAMCNT`/`DISPCNT` bytes in its register chunk
(it keeps the bank mapping in a derived structure), so the OBJ bank and colour
depth **cannot** be read back from registers. They are tool options here; the
defaults match the Days front-end. For a different screen, confirm the colour
depth from OAM attr0 bit13 and, if the render looks wrong, try other banks
(`--bank`) and units (`--unit`).
