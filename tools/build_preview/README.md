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
| `--only 3d,ui,fonts,text,audio` | Comma-separated list; only those categories (merges — see below) |
| `--limit N` | At most N items per category (quick testing; merges — see below) |
| `--jobs N` | Parallel processes |
| `--force` | Redo what already exists |
| `--clean` | Wipe `data/preview` before starting |
| `--bg-per-screen N` | Background compositions kept per screen (default 4) |
| `--max-bg-per-sub N` | Cap on backgrounds per sub-file |
| `--no-viewers` | Do not write the in-browser 3D viewers (see below) |
| `--no-anims` | Keep the viewers, but leave the animation data out of them |

`--bg-per-screen` and `--max-bg-per-sub` exist because `--dump-ui` has no limit of its own: one pack (`UI/cm/cm.p2` sub85) wrote **1.25 million BMPs** before it was bounded.

A full pass takes ~2 min; re-running it without `--force` takes ~20 s.

## Partial runs merge; they do not truncate

The gallery is built up across runs, so `--only` and `--limit` **add to the existing index rather than replace it**. `--only 3d` rebuilds the 3D cards and leaves every ui/fonts/text/audio card in place.

This used to be a trap, and it bit two people twice each: the index was written from whatever a single run happened to process, so using `--only`/`--limit` for exactly what they are for silently dropped every other category from the gallery — 856 KB down to 10 KB — while all of the files sat untouched on disk. Nothing warned you.

Each run now writes its payload to `data/preview/items.json` beside `index.html`, and reads the previous one back on the next run. Every item records the *unit* it came from (one model, one P2 sub-file, one sequence), so a run replaces only the units it actually processed. A previous item is dropped only when this run is entitled to say so: its unit was reprocessed, or its source is no longer on disk (the collectors always scan everything, so a `--limit` run still knows the full inventory), or its file has gone from `data/preview/`. A partial run says so in the page header.

`--clean` still wipes the lot — that is its job, and after it there is nothing left to preserve.

If `items.json` is missing or damaged while an `index.html` exists — an index built before this mechanism, say — a partial run has nothing to merge into, so it **refuses and changes nothing** rather than quietly truncating the gallery. Do one full run to rebuild the sidecar, or pass `--clean` if discarding really is the intent.

## What it produces

| Type | Result |
|---|---|
| **3D** | Textures as PNG, models as `.obj` (from BMD0/BTX0), and an in-browser viewer per model |
| **2D UI** | Backgrounds, sprite cells and tile sheets as PNG |
| **Fonts** | A glyph sample per NFTR |
| **Text** | String tables and message databases as `.txt` |
| **Audio** | Sequences, streams and SWAVs as `.wav`, with an in-page player, named from the SDAT's SYMB table |

Clicking any image opens it full-size. The cards are 88px tall, which is smaller than some of these textures actually are and far too small to read a 16×32 one, so the overlay scales by a **whole number** with nearest-neighbour — a texel stays a square instead of being smeared into its neighbours — and reports the real size and the zoom it used (`ax_hair_b · 16×32 · 35× zoom`). Its checkerboard sits on a solid mid-grey, so alpha reads on either theme. Arrow keys step through the group the click came from, since the neighbours are what you usually want to compare against; `Esc`, the scrim or the `×` closes.

## The 3D viewer

Clicking a model card in the gallery opens `viewer.html`, which draws the model with WebGL: drag to orbit, wheel to zoom, and a button toggles **Textured / Untextured**. Untextured is flat-shaded grey, so the topology is readable rather than a silhouette.

A model that has animations also gets a picker, play/pause and a frame scrubber. See [Animation](#animation) — including why the frame rate is a question mark.

There is no three.js and no CDN. The viewer is a few hundred lines of hand-written WebGL in `tools/build_preview/viewer.html`, copied verbatim into `data/preview/`. One copy is shared by every model.

### How the data reaches the page

The gallery is opened from `file://`, where `fetch()` and `XHR` cannot read a sibling file. So each model gets a `3d/<rel>/model.js` next to its `model.obj`, and `viewer.html?m=<path>` injects that path as a `<script src>`; the file calls back into `KH_MODEL({...})`. Script tags are not subject to the `file://` origin restriction — that is the whole reason for the indirection.

Textures are inlined into that `.js` as `data:` URIs instead of being referenced as the sibling `.png`. On a `file://` page Chrome treats a `file://` image as cross-origin, so uploading it into WebGL taints the texture and throws. A `data:` URI does not.

### Cost, and why it is on by default

Measured over a full run of 533 models: **505 `model.js`, 118.5 MB total** (avg 240 KB, largest 5.1 MB, 27 over 1 MB), plus a 16 KB `viewer.html`. The 28 models without one decode to no geometry at all; their card stays a plain `.obj` link.

Almost all of that is the animation payload, and it is not small: the same run with `--no-anims` writes **13.1 MB**. The baked palettes are a `mat4` per palette entry per frame, so a model with several long animations dominates its own file — Axel's is 40 KB static and 581 KB with his six. Over the whole gallery that is `3d/` at 138 MB instead of 33 MB, and the preview at ~373 MB instead of ~262 MB (the rest is mostly audio). It costs a few seconds of extra work, not minutes.

It is on by default because a gallery whose models only open in Blender is the problem this tool exists to solve, and the disk is git-ignored and regenerable. Two flags trade it back: `--no-anims` keeps the viewers but drops the animation data, and `--no-viewers` drops the payloads entirely.

### Where the textures come from

`--export-obj` writes a `usemtl` per mesh, naming the **texture** its material binds; a mesh that binds none has no `usemtl`. So the tool reads the binding back out of the OBJ, and the CLI stays the only thing that decodes anything here.

The material is named by its texture because that is the part a viewer has to bind, and because material names are not texture names anyway (`robe00a` and `robe00b` both bind `ax_robe00`).

### Transparency, and the byte that used to eat it

DS textures are translucent, not just cut out: `A3I5` and `A5I3` carry 3- and 5-bit alpha (8 and 32 levels), and the game uses them for effects and for character hair — Axel's `ax_hair_b` is A3I5 with 6 distinct alpha levels.

None of it reached the gallery until 2026-07-17. `to_bmp` wrote a 32-bit BMP with the bare 40-byte `BITMAPINFOHEADER` and `BI_RGB`, a combination in which **the fourth byte of a pixel is undefined by the format**. The alpha was in the file, and readers were entitled to ignore it — Pillow opened them as `mode=RGB` and did, so every texture converted to an opaque PNG. All 3513 of them, silently. `to_bmp` now writes a `BITMAPV4HEADER` with explicit channel masks, which declares the alpha instead of hoping for it; `load_bmp` reads `BI_BITFIELDS` back and refuses masks that are not the BGRA layout it decodes, rather than quietly swapping channels.

This was never only a viewer problem: a `--dump-textures` BMP is what a modder edits and feeds back through `mods/`, and that round trip lost the alpha the same way.

### Animation

`--export-obj` writes the **rest pose** (`to_wavefront_obj` bakes `posed_position`), so the bones are gone by the time the OBJ exists and no viewer could animate it. `--export-skin` supplies the missing half, which `build_preview.py` folds into the same `model.js`: the **raw** vertex positions with their palette indices and weights, the rest palette, and **one baked matrix palette per animation frame** — produced by the same `sample_animation` → `compute_palette` chain the native renderer runs per displayed frame.

Its vertices come out in the OBJ's exact order, so `v[i]` and `skin.pos[i]` are the same vertex. The viewer re-checks that count and stays static if it ever disagrees, rather than draw a scrambled mesh.

Skinning runs on the **CPU**, in JS, not in the shader. Axel's palette is 35 `mat4` = 140 `vec4` of uniforms and WebGL1 only guarantees 128, so a uniform array would break on real models. A few hundred vertices per frame costs nothing and has no such ceiling.

#### Which animations a model gets

Every model is at `<container>/slot_7/0000.nsbmd` and every animation at `<container>/slot_0/0000.nsbca` — 533/533 and 590/590, so the convention is exact rather than a guess. Two sources, and they are *not* equally trustworthy:

- **The model's own container.** Ownership is structural, so it is accepted whatever its bone count. It may drive fewer bones than the model has — `sample_animation` maps bone-by-index and leaves the rest at their rest matrix — and 8 pairs in the game do exactly that (`mi/ch/4C`: a 27-bone model with a 3-bone animation).
- **Anim-only containers in the same folder.** This is how a character's extra animations ship: `ba/ch/ax` holds Axel's model in `def.p` and six more animations for that skeleton in `it.p`, `li.p` and `ma0-2.p`. But ownership here is *inferred*, and a folder mixes skeletons freely — `ba/ch/ax` also holds 18-bone (`li_ea2`) and 7-bone (`li_e0`) animations for other things. So an exact bone count is required as evidence, and 571 candidates are rejected on it. A container that has its own model is never borrowed from.

An NSBCA whose bones carry no curves is dropped too (8 of them): every frame of it is the rest pose, so it would be a name in the menu that does nothing.

Every rejection is written to `build_preview_log.txt` with its reason. Nothing is dropped silently.

#### Two things that are not settled

- **The frame rate is unknown.** The viewer defaults to 30 and labels it `fps?`, with an input to change it. That 30 is inherited from `gpu_renderer.cpp`, where it is a bare `constexpr` commented "Nintendo DS animations" — nobody has measured it, and this project has already been wrong once by assuming 30 (the game's own loop runs at 60). Treat it as a viewing default, not the DS's cadence.
- **A palette's `w` is not always 1.** 9 of the 533 models ship palettes whose `m[15]` is `1.015625` or `0.996094` (`ba/ch/ze`, `mi/ch/03` and 7 more) — 1 ± a DS fixed-point step. The port does not agree with itself about it: `model.vert` multiplies the full `mat4` and lets the GPU divide by `w`, while `transform_point` (which the OBJ's rest pose goes through) computes three rows and ignores it. For those 9 the OBJ and the native render already disagree by ~1.5% on the affected bones. **The viewer follows the OBJ**, since the OBJ is the geometry it draws. `--export-skin` exports all 16 floats of every matrix rather than discard the evidence.

This used to be a second, independent walk of the MDL0 render commands, written in Python in `mdl0_materials.py`, because the exporter dropped the binding it had already resolved. That duplicated `src/assets/mesh.cpp`, and it drifted exactly as predicted: a missing `case 0x48` in the opcode table had to be fixed in both files at once — one bug in two places, found only because three models failed to export. Emitting the binding from the exporter deleted the duplicate walk, the drift risk, and the mesh-order tripwire that guarded against it. The change was verified against that walk's output first: identical bindings on all 533 models.

## Honest caveats

- **Many `ui/p2` backgrounds are mispaired noise.** `--dump-ui` renders screen × tileset × palette combinations because it *cannot know* which belong together, so correct art sits beside garbage. Sprite cells and 3D textures are clean. The game stores the real pairing in tables (for example `data_ov000_0205a9d4` for `ttl.p2`); reading those is the correct fix, and it is not done yet.
- **Three of the 533 models never produce a `model.obj`**, so they appear in the gallery as textures only: `ba/ch/de/li_e0.p` (`ef_de_Limit01`), `ba/ch/r2/li_e1.p` (`ef_ro2_Limit2`) and `mi/ob/08` (`E110_xx_000`), all `slot_7/0000.nsbmd`. `--export-obj` rejects each with `unknown render command opcode 72`. That is **not** a fault of this tool: `sbc_param_bytes()` in `src/assets/mesh.cpp` has no `case 0x48`, so the SBC walk throws. 0x48 is the Y-billboard command carrying its extra parameter, and it takes 2 parameter bytes exactly like 0x47 — which the table already has — so these three are the only models in the ROM that use it. `--model-info` reads them fine, because it never walks the render command stream.
- **No decoder**: the 46 MobiClip videos (`mv/*.mods`) and the code in `.bin`.
- **163 of the 533 models never animate**, and the gallery cannot tell you whether that is right. 141 have no candidate animation anywhere near them, and the rest lose theirs to the bone-count rule. An animation that lives outside its model's folder is not looked for at all — nothing has traced how the game actually pairs the two, so the container and the folder are the only evidence used here.
- **A played animation is only as right as that pairing.** For the model's own container it is structural and safe. For a folder-mate it rests on an equal bone count, which is evidence, not proof: two different 26-bone skeletons would pair happily.
- **The viewer is not the DS.** It draws the diffuse texture with a headlight and nothing else: no vertex colours (`--export-obj` drops those too), no per-material polygon attributes, and everything is drawn double-sided because DS meshes are authored that way. It is for looking at assets, not for judging how they will render in the port.
- **Translucent texels are composited in mesh order, and that is deliberate.** They are drawn in a second blended pass with depth writes off, so they no longer read as solid. Overlapping translucent surfaces then resolve in **mesh order**, which is the order the game submits geometry: `decode_model_geometry` appends meshes as the MDL0's render command stream executes them, and nothing in the path sorts. That order matters — reversing it visibly changes `mi/ob/1B`'s light beam (the spiral rings brighten, a magenta core appears) — so it is worth saying why it is left alone rather than depth-sorted.

  The game's own `main` writes **`SWAP_BUFFERS` (0x04000540) = 1 every frame** when it presents (`func_02000bcc.c`, the decomp). Per the DS register documentation, bit 0 selects translucent-polygon Y-sorting and **1 means manual** — the hardware does *not* reorder translucent polygons; they composite in submission order. (The boot-time `func_ov001_0204cbb4` writes 2, i.e. auto-sort + W-buffer, but `main` overrides it at every swap.) So back-to-front sorting here would **diverge** from the DS, not correct it. The register write is read out of the game; the meaning of its bits is hardware documentation, not something measured in this repo — a savestate would settle it beyond doubt.
- 26 models bind no texture at all, so their toggle does nothing. Models are still written as `.obj` plus their textures, so Blender remains the option for anything the viewer does not cover.
