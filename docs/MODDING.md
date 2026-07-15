# Modding

khdays-port decodes Nintendo DS assets into neutral, open formats (a skinned
neutral mesh, RGBA textures) before anything reaches the engine. Because of that,
replacing content is a first-class feature rather than byte hacking: you drop
open-format files into a `mods/` folder and the resource layer
(`khdays::resource`, see [ARCHITECTURE.md](ARCHITECTURE.md)) resolves them ahead
of the original DS asset. The engine only ever sees neutral formats, so mods
never contain or distribute copyrighted game data — only your own edits.

## Folder layout

Each mod is a folder under `mods/`, with one subfolder per asset category. Mods
are applied in sorted order; the first match for a given asset wins.

```
mods/
  MyMod/
    textures/           # any subtree; files are matched by name
      vexen/ve_00.png
    models/
      e_vex_hb.gltf     # matched to the DS model's internal name
      e_vex_hb.bin      # glTF buffers / textures sit next to the .gltf
      ve_00.png
```

`mods/` is git-ignored: keep your mods local.

## Texture overrides (including HD)

Drop a `PNG` (preferred) or `BMP` named after the DS texture anywhere under a
mod's `textures/` folder:

```
mods/MyMod/textures/vexen/ve_00.png
```

UVs are always normalized by the **original DS texture size**, so an override of
any resolution samples correctly — a 512×512 repaint of a 64×64 DS texture shows
up at full detail without distorting the mapping. The override is not downscaled.

## Model overrides (higher-poly, animated)

A rigged **glTF** at `mods/<Mod>/models/<ds_name>.gltf` replaces a DS model's
geometry. `<ds_name>` is the model's internal name (e.g. `e_vex_hb` for Vexen),
the same name the `--model-info`/export tools report.

The replacement is **animated by the DS skeleton**: the loader matches the
glTF skin's joints to DS bones *by name*, then drives the glTF each frame from
the DS bone-world matrices (`jointGlobal = DS bone world`), so every existing DS
`NSBCA` animation plays on your geometry. No animation authoring required.

### Making one

1. Export the DS model to glTF (with skeleton + skin) — e.g. with
   [apicula](https://github.com/scurest/apicula). The joints come out named after
   the DS bones (`Bip01`, `Bip01_Spine`, …).
2. Edit in Blender: subdivide, sculpt, or replace the mesh. **Keep the same
   armature and bone names, and keep the vertices skinned to it** (weights up to
   4 bones per vertex).
3. Export back to glTF, place it at `mods/<Mod>/models/<ds_name>.gltf` with its
   `.bin` and texture files alongside.

Joints whose names don't match a DS bone stay in their authored rest pose, so
partial rigs degrade gracefully. A glTF with no skin is ignored for animated
slots (it cannot be posed by the skeleton).

### How it's validated

Retargeting is exact: for an apicula-exported Vexen, the DS model and the
retargeted glTF produce the same posed geometry across animation frames (the
bone-world matrices agree with the glTF's joint transforms to fixed-point
rounding). A modder's higher-poly rebind to the same skeleton therefore animates
identically to the original.

## Trying it

With the DS data extracted under `data/` and a mod prepared as above:

```
khdays-port --render-model data/.../mi/ch/03/slot_7/0000.nsbmd
```

The viewer auto-detects the model's sibling `NSBCA`; the console prints the
override that was applied, e.g.:

```
override: model 'e_vex_hb' <- glTF geometry (24/24 joints matched to DS bones)
```

Pass `--anim <file.nsbca>` to play a specific animation instead of the
auto-detected one.
