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
| `--no-viewers` | Do not write the in-browser 3D viewers (see below) |

`--bg-per-screen` and `--max-bg-per-sub` exist because `--dump-ui` has no limit of its own: one pack (`UI/cm/cm.p2` sub85) wrote **1.25 million BMPs** before it was bounded.

A full pass takes ~2 min; re-running it without `--force` takes ~20 s.

## What it produces

| Type | Result |
|---|---|
| **3D** | Textures as PNG, models as `.obj` (from BMD0/BTX0), and an in-browser viewer per model |
| **2D UI** | Backgrounds, sprite cells and tile sheets as PNG |
| **Fonts** | A glyph sample per NFTR |
| **Text** | String tables and message databases as `.txt` |
| **Audio** | Sequences, streams and SWAVs as `.wav`, with an in-page player, named from the SDAT's SYMB table |

## The 3D viewer

Clicking a model card in the gallery opens `viewer.html`, which draws the model with WebGL: drag to orbit, wheel to zoom, and a button toggles **Textured / Untextured**. Untextured is flat-shaded grey, so the topology is readable rather than a silhouette.

There is no three.js and no CDN. The viewer is a few hundred lines of hand-written WebGL in `tools/build_preview/viewer.html`, copied verbatim into `data/preview/`. One copy is shared by every model.

### How the data reaches the page

The gallery is opened from `file://`, where `fetch()` and `XHR` cannot read a sibling file. So each model gets a `3d/<rel>/model.js` next to its `model.obj`, and `viewer.html?m=<path>` injects that path as a `<script src>`; the file calls back into `KH_MODEL({...})`. Script tags are not subject to the `file://` origin restriction — that is the whole reason for the indirection.

Textures are inlined into that `.js` as `data:` URIs instead of being referenced as the sibling `.png`. On a `file://` page Chrome treats a `file://` image as cross-origin, so uploading it into WebGL taints the texture and throws. A `data:` URI does not.

### Cost, and why it is on by default

Measured over a full run of 530 models: **502 `model.js`, 13.1 MB total** (avg 25 KB, largest 172 KB), plus a 15 KB `viewer.html`. That is +5% on a ~254 MB preview (which is mostly audio), and about 3 s of extra work. The 28 models without one decode to no geometry at all; their card stays a plain `.obj` link.

That is cheap enough to leave on. `--no-viewers` turns it off, which keeps `3d/` at 18 MB instead of 32 MB.

### Where the textures come from

`--export-obj` writes `o`/`v`/`vt`/`f` and nothing else: no `usemtl`, so the OBJ alone never says which texture belongs to which mesh. The C++ decoder does resolve it (`NeutralMesh::texture_name`), but the OBJ format drops it on the way out, and `--model-info` only prints materials and meshes as two separate declaration-ordered lists that cannot be zipped together.

Guessing does not work either: material names are not texture names (`robe00a` and `robe00b` both bind `ax_robe00`), and although UVs are in texels — which hints at a texture's size — sizes repeat within a model and wrapped UVs overshoot the texture entirely.

So `mdl0_materials.py` reads the binding straight out of the MDL0: the texture-pairing dictionary plus the `BindMaterial`/`Draw` opcodes of the render command stream. **This duplicates logic that lives in `src/assets/mesh.cpp`** and will drift if that file changes. It is checked on every model: the walk must reproduce the exact mesh order the exporter emitted, and if it does not, the textures are dropped and the model is shown untextured rather than painted with confidently wrong ones. Over all 530 models it currently matches every time.

## Honest caveats

- **Many `ui/p2` backgrounds are mispaired noise.** `--dump-ui` renders screen × tileset × palette combinations because it *cannot know* which belong together, so correct art sits beside garbage. Sprite cells and 3D textures are clean. The game stores the real pairing in tables (for example `data_ov000_0205a9d4` for `ttl.p2`); reading those is the correct fix, and it is not done yet.
- **No decoder**: the 46 MobiClip videos (`mv/*.mods`) and the code in `.bin`. 3D animations are listed but not rendered — they cannot be drawn on their own, without a skeleton and a scene.
- **The viewer shows the rest pose, unanimated.** The NSBCA animations are never applied, even for a model that has some.
- **The viewer is not the DS.** It draws the diffuse texture with a headlight and nothing else: no vertex colours (`--export-obj` drops those too), no per-material polygon attributes, no translucency — texels below half alpha are cut out, and everything is drawn double-sided because DS meshes are authored that way. It is for looking at assets, not for judging how they will render in the port.
- 26 models bind no texture at all, so their toggle does nothing. Models are still written as `.obj` plus their textures, so Blender remains the option for anything the viewer does not cover.
