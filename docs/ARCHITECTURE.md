# Architecture

The port is layered so the engine never depends on Nintendo DS resource formats
directly. Original resources are imported into the port's own neutral formats,
and everything above the resource layer works only with those. This is what lets
you replace an original resource with a modern one without touching engine code.

```text
Nintendo DS resources        NSBMD / TEX0 / NSBCA / SDAT / ...
        |
        v   importers                khdays::assets  (src/assets/*)
Neutral formats              NeutralModel / NeutralMesh
                             DecodedTexture (RGBA)
                             SkeletalAnimation
        |
        v   resource layer            khdays::resource  (src/resource/*)
Resource resolution          load_model / load_texture / load_animation
                             (a user override in mods/ wins; else the DS import)
        |
        v
Engine                       renderer (platform/pc), and later
                             collisions, animation, gameplay
```

## Rules

- **The engine depends only on the neutral formats and the resource layer.** It
  must never include a DS-format header (`khdays/assets/mdl0.h`, `tex0.h`,
  `animation.h` for the decoder functions) to *load* content. It may use the
  neutral *types* (`NeutralModel`, `DecodedTexture`, `SkeletalAnimation`).
- **Importers are one-way.** `khdays::assets` turns a DS format into a neutral
  format. Nothing in `khdays::assets` knows about the renderer or the GPU.
- **The neutral formats are the port's own**, not a 1:1 mirror of a DS struct.
  A future glTF or PNG importer produces the same neutral formats, so the engine
  cannot tell where a resource came from.
- **Overrides go through the resource layer.** `khdays::resource::load_*` checks
  the mods directory first, then falls back to the DS resource. Adding a new
  override kind (e.g. glTF models) is a new branch there, not an engine change.

## Modding, concretely

Today the resource layer resolves **texture overrides**: dropping
`mods/textures/<name>.bmp` next to the executable replaces the DS texture of
that name in the renderer, with no code change. Model (glTF) and animation
overrides plug into the same layer as their importers are added.

The command-line inspectors (`--model-info`, `--anim-info`, `--audio-info`,
`--export-obj`) are tools, not the engine, and may call `khdays::assets`
directly.
