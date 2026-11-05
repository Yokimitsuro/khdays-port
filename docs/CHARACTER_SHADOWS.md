# Enhanced character shadows

Settings → Enhancements → **Enhanced character shadows** enables animated,
soft overhead silhouettes. It defaults to off and is saved as
`character_shadows = on` in `[enhancements]`. Changes apply on resuming play.
There is no scene light or directional lighting calculation.

The current character/object pose is used, including the geometry and textures
emitted by the existing mod model renderer. There is no second model loader,
animation player, or sub-shadow model requirement. Linked weapon models and the
player's separately submitted equipped weapons are included. Independently
drawn effects are not casters.

## Integration

The hooks in `port/tools/prepare.py` associate `Scene_DrawNode` model draws
with the original shadow calls in `Render_DrawViewLists`, `Entity_Tick`, and
`Ov022_DropGroundMark`. The original calls still execute and retain the game's
visibility decisions, color, alpha, height fade, and volume height range.
Neither the decompilation nor generated adapted sources are edited.

NPC models can instead contain flat shadow geometry using the `ef_htd` texture.
The shared enemy framework draws a separate `kage` object using texture
`kage00`. Port hooks associate that object with its actor's model-node tree at
creation and remove the association at destruction. Material draw scopes
identify these native shadows without treating them as part of the caster.
Recognition follows the native texture-to-material pairing dictionary, since
native material names differ from texture names (the graphics dump's glTF
material names use texture names). The game's visibility decisions remain
authoritative: an actor with no visible native shadow does not acquire one
through this enhancement.

For these flat shadows, the receiver height band surrounds the original plane,
with a margin derived from its width. The silhouette uses the original
material's central texture color and alpha, including texture replacements.
The original flat geometry is suppressed only after a replacement receiver
and mask have been generated successfully. When a model mod replaces the NPC,
the native shadow draw is retained for this decision; an imported legacy
shadow plane is excluded once that native draw has been captured.

`port/gfx/character_shadow.c` captures pre-clipping geometry. It recovers world
positions using the camera and projection saved by the existing NitroSystem
flush hooks. This must use the saved projection: `NNS_G3dGlbFlushP` multiplies
camera and base transforms into the geometry engine's projection stack.

At SwapBuffers each caster's X/Z bounds define a vertical orthographic
projector. Nearby opaque floor polygons are clipped to those bounds and the
original volume's Y range. Walls and other registered casters are excluded.
The resulting receiver geometry reuses the floor's clip positions and depth;
it follows slopes and stops at rendered floor edges. Its material retains the
original shadow color and opacity. The original volume remains the fallback
when no matching pose or usable receiver is available, or when the graphics card
could not make the masks (their pipelines, or memory for one more mask). A list
has as many enhanced shadows as it has casters: the masks and their tile index
grow with them.

Source and receiver triangles travel in the existing geometry lists, including
frame copies, recordings, and interpolation. New flags distinguish the hidden
mask geometry, projected receivers, and original volumes replaced on the GPU;
a polygon's `shadow` field names its caster's mask.
The reference DS rasterizer continues drawing the original volume and ignores
the added floating-point polygons. Enhanced pictures intentionally differ from
that reference; comparisons should leave this enhancement off.

The GPU makes a 128×128 alpha mask for each active caster, taking the union of
its triangles with material, vertex, and texture alpha. An overhead tile index
limits the work per texel. A second compute pass softens the mask with twelve
equally weighted bilinear samples. The scene shader projects it onto the
receiver before ordinary translucent geometry and selection markers, checks
receiver depth and polygon identity, and blends without changing floor depth.
Duplicate receiver triangles do not compound the shadow alpha. Shader sources
are compiled by `port/tools/build_shaders.py` into all three existing formats.

## Provenance and limits

Days integration was researched against `khdays-decomp` commit
`a2b0478b87e3102732094c34f6ca89595f98ab93`:

- `Scene_DrawNode` (`main@0202aa9c`): animated render object draw.
- `SceneNode_AttachToModelJoint` and `SceneNode_Detach`: a scene node's parent at
  +0xbc, the path from a weapon's node to its owner. The decompilation's
  `Scene_DrawNode` calls the same offset `displayList`, which these two contradict.
- `Node_BaseInit` and `Node_UnlinkAndClearRefs`: a node's owner at +0. Assumed, not
  traced in the game: that this link leads from an enemy's attachments to its
  model tree (`khdays_character_object_begin`); only synthetic memory tests it.
- `Render_DrawViewLists` (`main@0202bc90`) and `Entity_Tick`
  (`main@0202c624`): entity shadow at +0x168, scene node at +0x10.
- `Ov022_DropGroundMark` (`ov022@0209c870`): floor raycast and alpha selection.
- `ShadowVolume_Draw` (`main@0202df34`): native mask ID 0 and shadow ID 63.
- `NNS_G3dGlbFlushP`: camera/base transforms in the projection stack.
- `Obj_RenderModel` (`main@0203bc78`): ModelObj draw and parent association.
- `func_ov107_020c6624` (`ov107@020c6624`): enemy model tree at +0x9c and
  separate flat shadow object at +0x1a8; `Ov107_DestroyObject`
  (`ov107@020c68ec`) releases the association before shadow destruction.
- `NNSi_G3dFuncSbc_SHP` (`itcm@01ffc0d0`): current material at SBC state
  +0xad and material resource at +0xd8 identify native flat shadow draws via
  their texture-to-material pairing dictionary, also decoded by
  `assets/src/mesh.cpp::read_materials`.
- `Ov002_SubmitActorDraw` (`ov002@02051cbc`) and `func_ov022_0209c9fc`
  (`ov022@0209c9fc`): equipped weapon draws inherit the player body owner.

The KH2 comparison used the user's local DWARF and IDA exports. Its observed
approach is an animated silhouette target, twelve-sample softening, and floor
projection. Its default projector is effectively overhead. This implementation
uses that rendering approach with Days' existing receiver range and appearance;
it does not claim identical KH2 floor selection, resolution, or filtering at
every distance. No extracted shaders, binaries, or assets are included.

If the mask's buffers cannot be made for a frame, that frame keeps the native
shadows and draws no receivers (`prepare_shadows` in `gpu_raster3d.c`).

The receivers are rendered opaque surfaces, not collision meshes. Very steep
surfaces, translucent floors, models with no existing shadow draw, and poses
not emitted in the same frame retain native behavior. A nearby horizontal
surface inside the original volume's height range can receive the silhouette;
this does not implement directional lighting, self-shadowing, or shadow maps
for the whole scene.

## Validation

`khdays-character-shadow-test` exercises native and replacement-style geometry,
receiver generation, retained alpha/ID, rebuilding, and missing-pose/floor and
disabled fallbacks, flat shadow exclusion, and independently submitted weapons.
`khdays-character-shadow-hooks-test` checks parent ownership, enemy association
and cleanup, material selection, and disabled behavior using synthetic game
memory. `khdays-kh2-gpu-test` additionally checks silhouette coverage, opacity,
duplicate receivers, foreground IDs, unchanged depth, native and replacement
sprite tint/alpha, and 1×/4× output. The input tests cover settings persistence.

For in-game review, enable the setting and compare standing, running, attacks,
jumps, and camera rotation. Check stairs, slopes, ledges, overlapping actors,
and object shadows. Repeat with a visibly different replacement model and its
alpha-textured parts, then disable the setting and confirm native shadows
return. Check both ordinary and interpolated frames and watch GPU frame time
in a crowded encounter.
Check the Moogle and lobby NPCs, ordinary enemies and bosses, both equipped
weapon slots, and actors that normally have no shadow. Repeat with model and
texture replacements. These gameplay checks are still required to verify the
full range of content; the automated fixtures use synthetic geometry.
