# Asset gallery

Decodes **everything** extracted from your ROM into a browsable gallery at `data/preview/index.html`: images, models, audio and text.

The tool neither downloads nor distributes game content: it works on the data you extracted yourself, and its output lives under `data/`, which Git ignores. The generator is what ships; the assets stay on your machine.

## Prerequisites

You need the data extracted from your own copy of the game. The full pipeline:

```powershell
py .\tools\verify_rom\verify_rom.py "E:\path\game.nds"
py .\tools\extract_data\extract_data.py "E:\path\game.nds"
py .\tools\decompress_data\decompress_data.py
py .\tools\unpack_containers\unpack_containers.py
```

And the built executable, which is what actually decodes each format:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Generate

```powershell
python .\tools\build_preview\build_preview.py
```

Open `data/preview/index.html` in a browser. It works from `file://`, no server needed.

> **Use `python`, not `python3`.** On Windows only the former has PIL, which is required to convert to PNG.

## Options

| Flag | Purpose |
|---|---|
| `--only 3d,ui,fonts,text,audio` | Comma-separated list; only those categories |
| `--limit N` | At most N items per category (quick testing) |
| `--jobs N` | Parallel processes |
| `--force` | Redo what already exists |
| `--clean` | Wipe `data/preview` before starting |
| `--bg-per-screen N` | Background compositions kept per screen (default 4) |
| `--max-bg-per-sub N` | Cap on backgrounds per sub-file |

Those last two exist because `--dump-ui` has no limit of its own: one pack (`UI/cm/cm.p2` sub85) wrote **1.25 million BMPs** before it was bounded.

A full pass takes ~2 min; re-running it without `--force` takes ~20 s.

## What it produces

| Type | Result |
|---|---|
| **3D** | Textures as PNG and models as `.obj` (from BMD0/BTX0) |
| **2D UI** | Backgrounds, sprite cells and tile sheets as PNG |
| **Fonts** | A glyph sample per NFTR |
| **Text** | String tables and message databases as `.txt` |
| **Audio** | Sequences, streams and SWAVs as `.wav`, with an in-page player, named from the SDAT's SYMB table |

## Honest caveats

- **Many `ui/p2` backgrounds are mispaired noise.** `--dump-ui` renders screen × tileset × palette combinations because it *cannot know* which belong together, so correct art sits beside garbage. Sprite cells and 3D textures are clean. The game stores the real pairing in tables (for example `data_ov000_0205a9d4` for `ttl.p2`); reading those is the correct fix, and it is not done yet.
- **No decoder**: the 46 MobiClip videos (`mv/*.mods`) and the code in `.bin`. 3D animations are listed but not rendered — they cannot be drawn on their own, without a skeleton and a scene.
- Models are exported as `.obj` plus their textures: open them in Blender. **The gallery does not show them in 3D** in the browser yet.
