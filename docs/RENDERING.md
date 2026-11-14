# How the port draws the game

The game asks for the DS's 2D and 3D as ever; the port describes each frame and
draws it on the graphics card at any resolution and aspect ratio. Software
engines of the DS's own (`port/gpu`) are the reference the card's picture is
checked against.

## The 2D: a frame's description

At VBlank (`port/gfx/describe.c`) the port describes a frame of each screen's
2D (`port/gfx/frame.h`): the layers, sprites, windows and effects the game asked
for, already decoded -- no registers, OAM entries or bank windows left -- and the
memory its layers and sprites read their tiles, maps and palettes from. The
description is words, so the GPU reads it as it is (`KD_*` are their offsets).

`port/gfx/compose.h` composes one pixel from it the way the DS composes what the
game asked for: each layer's colour there, the topmost sprite, the windows'
masks, the two topmost targets blended, the 3D layer's own alpha, brightening
and darkening, and the master brightness. At 1x it gives the same pixels as the
reference renderer (`port/gpu/gpu2d.c`). It is written in the part of C that
HLSL shares, so the CPU (`compose_cpu.c`: the check and the tests) and the GPU
(`port/shaders/compose2d.hlsl`) run the same code. The includer defines, before
including it, the types `uint` and `int` (32 bits) and `float`, and:

| Macro | What it gives |
|---|---|
| `KD_FN` | how a function is declared (C: `static inline`; HLSL: nothing) |
| `KD_VOID` | an empty parameter list (C: `void`; HLSL: nothing) |
| `KD_DESC(i)` | word i of the screen's description |
| `KD_BG8(a)`, `KD_BG16(a)` | the layers' memory (`a` masked to its size here) |
| `KD_OBJ8(a)`, `KD_OBJ16(a)` | the sprites' memory |
| `KD_BGEXT16(a)`, `KD_OBJEXT16(a)` | the extended palettes |
| `KD_PAL16(i)` | palette entry i (0-255 the layers', 256-511 the sprites') |
| `KD_VRAM16(a)` | the bank a VRAM display shows |
| `KD_3D(x, row)` | the 3D layer's pixel (6-bit channels in bits 0-5, 8-13, 16-21; alpha 0-31 in 24-28; 0 transparent) |
| `KD_3D_KNOWN` | 1 when `KD_3D` holds the 3D drawn (0: not given, every pixel 0) |
| `KD_HD_BG(u)`, `KD_HD_OBJ(u)` | the tiles drawn instead (`runtime/mods/tiles2d.h`): the word of the layers' or the sprites' 32 bytes number u, 0 none |
| `KD_HD_FIRST(id)`, `KD_HD_WIDTH(id)`, `KD_HD_HEIGHT(id)`, `KD_HD_INFO(id)` | replacement id's first texel, size and info (`textures.h`) |
| `KD_HD_TEXEL(i)` | texel i of the replacements |
| `KD_SUB_X`, `KD_SUB_Y` | where the output pixel lies inside its DS pixel (0-1, float) |
| `KD_PIXEL` | the output pixel's size in DS pixels (1 / N, float) |
| `KD_ELEMENT(i)` | word i of the screen's elements (`gfx/elements.h`) |
| `KD_EL_TEXEL(image, i)` | texel i of element image `image` (`KE_IMAGE`) |
| `KD_ASFLOAT(u)` | the float whose bits `u` holds |
| `KD_LEFT_OUT_CELL(c)`, `KD_LEFT_OUT_OBJ(i)` | 1 when cell c of the text layers' maps (`KE_CELLS`), or sprite i, is left out (what a part stands for) |

A replaced bitmap (`KHDAYS_REPLACEMENT_BITMAP`, info bit 4: a movie's frame,
`runtime/movies/video.h`) is read at the picture's own size; its info holds the
bitmap's 32-byte units per row in bits 8-15 and its rows in bits 16-31.

### Elements

Elements are things the port draws itself (`port/gfx/elements.h`): each takes
one slot of the DS's composition (a BG layer, or the sprites at a priority),
with an image the port drew at the 2D's scale (`khdays_ui_scale`: the window's,
below) and a place in DS pixels. The description's `KD_ELEMENTS` word says how many follow it (on the
card after the description and its palettes; on the CPU in their own array),
`KE_WORDS` words each, the topmost of a slot first. A texel is 0 transparent;
indexed, `KET_OPAQUE` | the palette index (the slot's palette), with
`KET_EXTENDED` an 8-bit character's index in the extended palette its bits
12-15 name; or direct colour, RGBA with red in bits 0-7. Where a layer does not
show, the elements' colour says how much they let through (`KD_SEE_THROUGH`)
and the pixel is composed with and without it.

## Widescreen

The upper picture is `KD_WIDE` DS columns wider on each side, the DS's 256 in
the middle. The 3D uses its own column there. The HUD's pieces are the groups of
8x8 cells the 2D layers 1-3 cover that touch one another: a piece wholly in the
left half moves to the left edge, keeping its distance to it, one wholly in the
right half to the right edge, and one across the middle (a dialog, a bar from
side to side) stays. The sprites and the windows use the DS column at the pixel:
beside the DS's columns there are none, and the pixel is outside every window.
Only for a screen showing the 3D (`khdays_gfx_describe_wide`; the software
engine's `KhdaysGpu2dWide` does the same, with a DS column whose own pixel stays
taking the DS's record, and a column with no 2D and no sprite its window mask's
record alone).

## Two screens side by side

With two screens, `[video] two_screens = side-by-side` puts the upper screen on the left and the lower on the right
(`host/render/two_screens.c`): a picture two DS screens wide and one tall, each screen its DS's 256 columns (a screen
drawn wider shows its middle 256), the engine A's 3D drawn on the card again over its own screen's place. The aspect
ratio widens stacked screens only: side by side the picture is the DS's columns (`columns_now()`). Stacked, the
default, is the picture as it was, the two screens one above the other. Only the presentation changes: the game and
its frames are the same.

## One screen

The one-screen pass (`port/gfx/onescreen.h`) places shapes -- rectangles of
either screen as the port composed it, with their effects -- in a single picture
of C x 192 DS dots. For each texel the shapes are tried in order; the first whose
destination holds the texel and whose source texel is visible gives it, else the
3D or the backdrop; then the brightness of the rule. A shape that shrinks its
source averages the texels it covers (a box filter), so one-texel lines of the
game's text keep their share of the colour; one that keeps or enlarges its
source takes the texel there, sharp. The layouts are in `docs/COMPOSITIONS.md`.

The 3D's colour is not known when the upper screen's 2D is composed, so
`kd_split` composes the pixel with the 3D's colour X left unknown. The screen
shows K + F x X: the result holds K in 8-bit channels (bits 0-23) and, in bits
24-31, either 0 where the 2D does not show over the 3D, or 1 + F x 254. Every
blend the DS makes with the 3D is linear in X, so K and F come from composing
once with the 3D black and once with it white. The 2D does not show over the 3D
when the top layer is the 3D or the backdrop, or when a layer is blended over
the 3D with its own weight (EVA) 0 and the 3D's (EVB) 16: the game holds a layer
so while it is not to be seen (the day's card's sprites, the mission's
information between its fades) and passes weights 1-3 only for a frame or two
of a fade. When the 3D drawn is given (`KD_3D_KNOWN`), it counts as it is at the
pixel: where it is transparent the layers under it show, as on the DS (the game
over's menu lies under its 3D), and its own alpha blends. A polygon the
one-screen picture moved (the game over's orb) is not at its new place on the
DS, so there it counts as none. A 3D the DS shows over all its 2D (the orb,
`KHDAYS_G3D_OVER_2D`) goes over the shape's texel with its alpha. The DS draws
one 3D, engine A's, whichever screen engine A shows; the picture has it under
everything either way.

The shapes' effects: a key colour's texels are not shown, a see-through colour's
are mixed with what is under the shape at their opacity (both matched in the
DS's 5 bits a channel, so a piece's colour is matched exactly); `dim` takes the
colours toward black; `opacity` and `fade` mix the shape with what is under it,
`fade` from nothing at an edge to the whole shape over its length; `mirror`
turns the source over, `repeat` repeats it, `force` shows texels the 2D would
leave invisible over the 3D, `round` leaves out its corners outside a quarter
circle.

The pass is shared C/HLSL like `compose.h`. With `KO_STEPS` the includer defines
`KO_FN`, `KO_SHAPE(i, w)` (word w of shape i), `KO_KEY(i)` (colour i of the
keys' buffer, BGR555), `KO_ASFLOAT(u)`, `KO_FLOOR(f)`, `KO_ABS(i)`, and
`KO_SOURCE(lower, x, y)` (the source screen's texel at DS dots: the upper's as
`kd_split` gives it, the lower's colour with alpha 0 on the backdrop).

## The 3D

`port/gfx/g3d.h` is the port's 3D geometry: it answers what NitroSystem and the
game ask of the DS's geometry engine. Its state -- the four matrix stacks, the
current vertex's attributes, the lights, the material, the viewport -- is the
DS's, in the DS's fixed point, so positions, colours and whatever the game reads
back are the same as on the DS. The commands are the DS's (GBATEK, "DS 3D
Geometry Commands"), taken as they come (one command with its parameters,
packed words as GXFIFO takes them, or a store to a command port) and run at
once: there is no FIFO, the geometry is never busy, and a SwapBuffers closes the
list being built, which the next VBlank shows. The arithmetic is GBATEK's;
where it leaves the rounding open the choice is marked in `g3d_geometry.c`. The
DS's polygon and vertex RAM no longer limits a list.

A list keeps each polygon as the game sent it, before clipping (`pre`), with the
key that finds the same polygon in the next list: the model draw it came from
(its resource and how many draws of it came before) and its place in that draw.
For frame rates above the game's (`port/gfx/g3d_interp.h`) the polygons the
newer list shares with the older one -- the same key, as many vertices, drawn
with the same texture, palette and attributes but alpha -- get their vertices
placed between the two lists' at `alpha`, linearly in clip space, nothing
extrapolated; then the list is clipped and put through the viewport like the
game's (`khdays_g3d_finish`), so at `alpha` = 1 it is exactly the newer list.
The match does not depend on `alpha`: the picture names the two lists by their game frames' times
(`khdays_g3d_interp_pair`), and the refreshes between the same two take the match the first made and only
place and finish (each newer polygon's older one kept, `older_of`).
Their places, colours and alpha go between; their texture coordinates are the
newer's (a texture scrolled round, a door's barrier or a Dark Corridor, ran back
between the two). A model drawn more than once in a list (the field's prizes,
the enemies) is matched to its nearest drawing in the older list, nearest pairs
first, not to the one of its place in the order, which changes as one comes or
goes (the prizes flew between each other). What moved more than an eighth of
the view -- a model's drawing, or a polygon outside a model (an effect's piece
taken for another of its material) -- and the selection markers (the reticle
going from one target to the next) are drawn where the newer list has them. A
drawing's place is the middle of its polygons' first vertices before projection,
projected: the average of their places on the view ran to the hundreds for a room
whose walls stand beside the eye, so the field's room (some 500 polygons) passed
for one that jumped in a third of the game's frames and was drawn at the game's 30
a second under Roxas moving smoothly (seen 2026-11-04 at 165 Hz, the play walk:
322 polygons a picture left out where any were, 0.08% of all after).
`tests/g3d_interp_test.c` checks each.

A mod's model leaves out what lies wholly past a side of the view, twice as far
out as the sides it is clipped at (`KHDAYS_G3D_CULL_MARGIN`), so that a picture
between two lists still finds what the camera turns to: first each mesh whose
vertices all follow one joint (a room's pieces), by the box round its vertices
kept at loading (`model_load.cpp` `box_meshes`), before any of its vertices is
placed; then each triangle whose three corners lie past the same side. A polygon
left out still takes its place in the draw (`khdays_g3d_mod_skip`), so the later
ones keep their keys, and a drawing's place counts a mod's polygons only within
those sides, the same with or without what was left out. It happens only in a
model draw (outside one a polygon's place is its material's count). Nothing the
game reads comes of it: the box test and the counts of polygons and vertices are
its own polygons' alone, so in multiplayer each machine leaves out what its own
camera does not see and the game runs the same. `[video] culling = off` (the
settings' Graphics, "Skip what's out of view"; `KHDAYS_HOST_NO_CULLING`) draws
every piece, at once, for comparing. Seen 2026-11-12 with a map of
260,000 triangles in 393 meshes (a glTF in place of Twilight Town's room 0, the
play walk, in a window): the game's thread ran 720 ms a second at 28 frames a
second, 510 with the meshes left out (200-238 of them), 340 at 56-60 with the
triangles too; the step pictures' map was the same as without (only Roxas, his
arrival at another pace in the slow run, differed).

The graphics card draws the lists (`host/render/gpu_raster3d.c`); the software
rasterizer (`port/gpu/gpu3d.c`, GBATEK's "DS 3D Video") draws them as the
reference, close to the hardware where GBATEK leaves the arithmetic open (the
depth value, the interpolation, which pixels a polygon covers), not bit-exact.

Selection reticles have a port-only `KHDAYS_G3D_MARKER` flag. The selection draw
hooks mark both KH2 replacements and native PNG/DS fallback draws, and clear the
scope before returning to unrelated rendering. These polygons follow the world
in submission order, bypass world depth in both Z and W modes, and leave world
depth unchanged. Their translucent IDs start separately from the world's, so an
enemy using the same ID cannot erase them. This prevents translucent enemies
from covering reticles through the normal Y sort. GPU and software paths apply
the same rule, including lists rebuilt for interpolation. This does not set
`KHDAYS_G3D_OVER_2D` or change HUD placement: the existing BG0 and one-screen UI
composition still decide which 2D elements cover the reticle.

### Shadow volumes

The game draws a shadow in two passes (`ShadowVolume_Draw`): the volume's rear
faces as the mask (polygon ID 0, front faces culled), then the whole volume as
the shadow (ID 0x3f, nothing culled). A surface is in shadow where it lies
between the volume's front and rear faces. The mask marks the stencil where its
rear face fails the depth test (the surface is in front of it); the shadow draws
where a mark is and its own depth test passes (the surface is behind its front
face), so its rear faces, which pass only where no mark is, draw nothing.
GBATEK's wording -- marks where the mask passes, the shadow where none is,
clearing them -- would let the second pass's rear faces shade everything behind
the volume: the whole volume showed. As GBATEK says, a shadow is not drawn over
a pixel of its own ID. A mask reaching a pixel after any other polygon starts
from no mark there, so one volume's marks where its shadow did not draw shade
nothing of the next.

### Clip volumes

A mod model's `clip_world` group (SECONDARY_MOTION.md) sends a closed volume, the
coat's, as triangles flagged `KHDAYS_G3D_CLIP`, both sides, first in the drawing
order, and the model's own polygons flagged `KHDAYS_G3D_CLIP_OWNER`
(`khdays_g3d_set_clip_owner` around its meshes). The volumes are not drawn as
polygons: as `raster3d.hlsl` goes through a pixel's polygons it keeps the
nearest depth of the volumes' triangles covering it, and where two or more do,
any later polygon that is not an owner's, the HUD's, a sequence or a marker is
left out behind that depth -- inside the volume, or behind it, where the coat's
far wall would hide it. Where the volume's nearest depth lies before the rear
plane, the pixel starts as the coat's inside: black, under everything drawn after
the volumes, so a translucent effect in front blends over it as over any surface
(painted after the polygons, it covered the effects, and the coat under them
wherever an effect had blended last). Its depth stays the rear plane's for the
coat to draw on; if no polygon writes a depth there, the inside is the surface:
the volume's nearest depth, with the last owner polygon's ID and fog (leaving out
only what lay inside the volume showed whatever was behind it: the rear plane, or
the room under a bed through the crack along its quad's diagonal). A volume's triangles cover a target
pixel more on each side than a drawn polygon does: two of them sharing an edge
could leave a pixel to neither, and a pixel that misses the volume's front shows
the room through it (a dashed line across the coat's inside); a pixel covered
twice changes nothing. So the room inside a coat is not
drawn and the coat's inside shows there, while a wall in front of the coat lies
before the nearest depth and still covers it; the order of the list does not
matter. Two volumes a pixel shows act as one from the nearest (the room behind
the nearer model is left out, as it is behind its coat). The software rasterizer (`gpu3d.c`) draws no mod model and
so no volume. The cost is a volume's triangles (about a hundred) in the tiles it
covers.

The optional **Enhanced character shadows** setting replaces that footprint on
the GPU with a softened overhead silhouette of the current model pose. It uses
the original shadow's color, opacity and receiver height range, including mod
model geometry and equipped weapons. NPC and enemy flat shadow materials also
use this path, retaining native visibility, tint and opacity. See
[CHARACTER_SHADOWS.md](CHARACTER_SHADOWS.md) for the hooks,
projection and mask passes, fallback behavior, provenance and validation.

## On the graphics card

The GPU device is made with every shader format the build carries
(`port/host/render/generated`: DXIL, SPIR-V, MSL; `port/tools/build_shaders.py`
makes them from `port/shaders`) and SDL picks the backend; the window's
SDL_Renderer is made on it, so the settings menu draws into the same frame. A
graphics card is required: without Direct3D 12 or Vulkan the port says why and
does not start (`host/render/gpu_present.c`).

A frame: both screens' 2D are composed by `gfx_render.c` from the package's
descriptions (`shaders/compose2d.hlsl`: an output pixel takes the 2D of the DS
pixel it lies in, so the pixel art stays sharp, and the 3D of its own place),
and engine A's 3D is drawn at the internal resolution by `gpu_raster3d.c`.
The 2D is composed at the window's scale whatever the internal resolution
(`scale2d_now`, gpu_present.c: smooth scaling the whole multiple at least as
large, sharp the largest that fits; the 3D's when that is finer, an
internal resolution above the window's), so the HUD, the
menus and the text stay as sharp as the window when the 3D is drawn coarser to
save the card's time. Where the 2D is finer than the 3D, each output pixel reads
the 3D at its middle (`finished3d_at`, finish3d.hlsli): the nearest texel with
sharp scaling; with smooth scaling the four nearest, weighed by place and by
their alpha, so a texel without 3D gives no colour. The HUD's own 3D (the
polygons the field's panel and the game over screen send, `KHDAYS_G3D_HUD`) is
then drawn apart at the 2D's scale (`khdays_raster3d_rasterize_hud`: those
polygons alone, the world's not uploaded or walked and no character shadow prepared for that pass,
over no clear colour's alpha or fog and no rear plane; each size
keeps its targets, gpu_native3d.c and gpu_raster3d.c holding the other aside),
the rest at the internal resolution without them, and the composition puts the
HUD's over the rest as the DS draws a polygon over what is there (`over3d`). The
game draws its own pictures (HD text, the KH2 HUD's) at the 2D's scale too
(`khdays_host_frame_scale`), with no upper bound. Seen (2026-11-04, the HD PACK,
internal resolution 1 on a 2736x1536 window, the 2D at 8x): the command menu's
HD words and the panels as sharp as at the window's resolution, the 3D at the
DS's size, smoothed or square by the scaling. Each
frame uploads the package's polygons, the composition records and the texture
pages that changed; the card's own rasterizer draws the 3D (below, "The native
3D"). `KHDAYS_3D=compute` draws it the way before it: with an index of which
polygons may cover each 8x8 tile (in drawing order), `raster3d.hlsl` runs
`gpu3d.c`'s rasterizer for each pixel -- each thread owns
one pixel and goes through the polygons that may cover it in drawing order,
keeping its state as `gpu3d.c`'s `Pixel` does, so translucent IDs, the shadow
mask and blending over alpha 0 are exactly the reference's; where `gpu3d.c`
computes in double (the depth) it computes the same integer in float and
integer steps, its coverage and interpolation kept from being fused
(`precise`). `finish3d.hlsli` then does the edge marking and the fog, which
need the finished neighbours, and `compose.hlsl` composes it with the 2D as
`gpu2d.c` does from the composition records, the master brightness last. A
picture made larger than it is shown is shrunk by `shrink.hlsl` (each texel the
area-weighted average under it): the window shows the picture at its own pixels.

The antialiasing setting (FXAA, `shaders/fxaa3d.hlsl`) smooths the 3D once it is
drawn, before anything reads it: a texel whose neighbours' brightness changes
enough (an eighth of the brightest, and at least 0.03) lies on an edge; the edge
runs across or down by which way the change is larger, its run is followed both
ways (steps of 1 to 8 texels) between the texel's row and the steeper side's until
the brightness leaves the edge's, and the texel is blended with the one across
the edge by how near the run's nearer end it lies (a texel finer than one, a lone
point or line, by how far it stands from its neighbours). Brightness and blending
count each texel by its alpha, so an outline over no 3D becomes partial alpha and
a texel without 3D gives no colour; the texel keeps its flags (over the 2D,
translucent), and the edge marking and the fog come after from the other target.
The result is copied back over the colour, so every reader (the composition, the
one-screen picture) gets it; the HUD's 3D drawn apart is left sharp. It replaced
the antialiasing that drew the picture 2x to 4x larger and shrunk it (four to
sixteen times the 3D's work; settings files holding 2x, 3x or 4x read as FXAA).

In widescreen the 3D target is that many columns wide (`origin_x` the DS
column of its first, negative), the list clipped wide enough to fill it
(`khdays_g3d_set_widen`), and the HUD's polygons move to the sides as the 2D
pieces do: touching polygons are grouped and moved by the half they lie in.
A picture the game draws in screen space over the whole DS view has nothing
beside it, so screen-space polygons that together cover the view (Sora's
memories in a dialog, a fade drawn as a quad) are stretched to the target's
width; smaller ones keep their shape (the title's logo, the day's card).

Texture smoothing is the port's option, not the DS's. Whether a pixel shows is
the DS's (the nearest texel's alpha), so a cut-out keeps its shape; its colour
is the four texels around blended by distance, each weighted by its alpha, and
only texels the polygon maps (an atlas's neighbours stay out). A polygon that
shows its texture at its own size or smaller in screen space (an orthographic
projection: the 2D drawn with the 3D engine, the HUD's pieces, the day cards,
the noise over Sora's memories) is left sharp, as the 2D layers are; a model
under an orthographic camera with its texture magnified (the title's logo) is
still smoothed. A replacement texture is read at the place of the original's
mapping at its own resolution, a smaller copy where a pixel spans more than one
of its texels.

The replacement graphics (`runtime/mods/textures.h`: mods' 3D textures, 2D tile
sheets, models' textures) share one buffer the 3D pass and the 2D composition
both read (SDL's GPU API binds at most 8 storage buffers to a pass): a table of
four words per replacement (its first texel's place, width, height, what it is),
then all the texels, one word each; a still picture's and a 2D sheet in its own
colours are followed by smaller copies, each half the one before (info bit 0x8).
The buffer grows as new pictures come (`host/render/gpu_hd.c`): a larger one is
made and what the card had is copied over on the card, its places moved past the
larger table and the whole table uploaded again, so a growth uploads only the new
pictures (it uploaded everything again, a stall of the whole buffer's size, and
its sizes were 32-bit sums that wrapped past 2 GB of texels). The most one buffer
holds is 4 GB, the shaders' byte addresses being 32 bits (`hd.Load`) as SDL's
buffer sizes are, so the card keeps the pictures as a cache. The 3D's pictures
(`KHDAYS_REPLACEMENT_ON_DEMAND`: a mod's texture for one of the game's, a mod's
model's or room's, a texture bank's -- drawn by the 3D's polygons only) are
uploaded when a list's polygons first draw them (`khdays_gpu_hd_sync_drawn`, before
each 3D pass); every other picture (2D sheets, the HUD's atlases, movies, the
pictures the card itself draws) is uploaded as it is added and stays. Below the
most nothing leaves. At the most, room for a picture is found in the stretches
pictures let go, and else by letting go of the 3D's pictures not drawn in this
picture (`khdays_gpu_hd_next_frame`, once a presented picture), the least lately
drawn first; they come back when drawn again. The table reaching the most takes
room for four times the pictures; a larger table there takes room from the top
of the texels, letting the 3D's pictures there go. A picture with no room yet
reads the first texel, white -- the game's colours untextured -- until it fits,
and the KH2 HUD's pass (`kh2_sequence_gpu.c`) does not draw into a picture not on
the card (it wrote through the white texel's entry over the pictures after it: a
model drawn with it vanished). Each growth, and the most reached, is in the
session log (`hd: the card's replacement memory ...`). Checked by
`tests/gpu_hd_test.c` on the card (growth keeping every picture, pictures coming
and going, a table at the most). Seen (2026-11-04, the maintainer's HD PACK and
two mods: 3.5 GB of pictures with their smaller copies on disk): 55-66 MB on the
card in the mission and camp walks; with the most lowered to 40 MB, Roxas's model
let go in the field and drawn white (the 2D's pictures, which stay, filled the
rest), the walk's scenes the same.

With the 3D on both screens the game moves engine A to the other screen at
every VBlank (POWCNT1 bit 15), drawing each screen's view in turn while the
display capture keeps the other's; the one-screen picture is the upper
screen's, so it is made on the VBlanks engine A shows the upper screen.

The screen blends (a cut between an event's camera shots) capture engine A's
picture blended with a VRAM bank back into it while engine A shows the bank.
The window takes each capture again from what it shows, at its resolution and
width (`capture.hlsl`: the blend, `gfx_render.c` `take_capture`), and shows its
copy of the bank instead of the DS's 256x192 one (RUNTIME_MAP, "The screen
blends at the port's resolution").

Testing knobs: `KHDAYS_SHOTS_HD=DIR` (the `KHDAYS_SHOTS` frames as presented,
at the resolution drawn; each at the first frame presented from it on, named
by that frame); `KHDAYS_GFX=reference` (the 2D engines' pictures);
`KHDAYS_GFX=check` (the port's way compared with the 2D engines at 1x, or the
old way at N x, every 300 frames); `KHDAYS_PROFILE` (frames never drawn, and
the time preparing the 3D took; and once a second a `phases:` line, `runtime/core/phases.h`: the milliseconds
and runs of a replacement model's load, posing, cloth and skinning, the HD pictures' decoding, the character
shadows' capture and build, the 3D lists' finish, the interpolation (with its finish), and the renderer's
preparation and its upload and submission); `KHDAYS_PROFILE_GPU` (every 300 pictures, the
3D's and the composition's time on the card, each picture waited for);
`KHDAYS_3D=compute` (the 3D by `raster3d.hlsl` alone).

### The native 3D

`gpu_native3d.c` draws the list with the card's own rasterizer, so a picture
costs what its pixels cost and not, as `raster3d.hlsl`'s, every pixel times
the polygons near it. Each polygon is a fan of triangles in the list's order,
its corners a vertex and its polygon (`native3d.hlsl`'s vertex shader reads the
vertex from the list); the clip space's w is the DS's, so the card interpolates
colours and texture coordinates as the DS does, and the Z buffer's depth is the
DS's over 2^24 (the W buffer's, and the depth test "equal", from the shader:
`SV_Depth`). The fragment shader colours a pixel with `gpu3d.c`'s rules, shared
with `raster3d.hlsl` (`g3d_vram.hlsli`: texels of every format, the rear plane;
`g3d_polygon.hlsli`: the polygon's alpha and colour, the replacements).

What a fragment shader cannot do the pipelines do. The stencil holds the last
polygon ID drawn (bits 0-5), whether it was translucent (6) and the shadow
masks' bit (7): a translucent polygon is not drawn over a translucent pixel of
its own ID; a run of shadow masks first clears bit 7 over the whole target,
each mask sets it where its depth test fails, and a shadow is drawn where it is
set, after its polygon ID has cleared it from the pixels of that ID; the first
selection marker clears bit 6. The DS blends a translucent pixel over what is
there, but gives an empty pixel (alpha 0) its colour whole: target 0 keeps the
colour blended (rgb) and how much of the first translucent colour drawn over an
empty pixel still shows (alpha, multiplied down by each later blend), target 1
that first colour, and `native3d_pack.hlsl` adds it back (the DS's result,
however many layers). The alpha is the largest drawn (blend MAX); the fog bit
and the opaque polygon's ID and edge are written where the shader's alpha says.
A polygon whose texture or replacement may give both opaque and translucent
pixels is drawn twice, its opaque pixels first (writing the depth, as the DS
does) and then its translucent ones; runs of such polygons drawn alike are
drawn as two draws. The rest of the polygons drawn alike (pipeline, stencil
reference) are one draw.

A picture writes only the targets what finishes it reads: the colour, alpha
and flags always; the first translucent colour when the rear plane can be empty;
the fog bit with fog on, the polygon IDs and edges with edge marking on, the
depth for either (`finish3d.hlsli`). Without the clear image the rear plane is
the targets' clear values. The depth target is a float one holding the DS's
24-bit depth as a whole number (exact in a float): a render pass clears with
float values, which Direct3D 12 converts for an integer format and Vulkan hands
over as their bits, so an `R32_UINT` cleared to the rear plane's depth held about
half of it on Vulkan, and `raster3d.hlsl`, resuming from it in a scene with KH2
Roxas's coat (its clip volume comes first in the list), left out the whole room
(a black screen under the dialog boxes, fixed 2026-11-06).
`native3d_pack.hlsl` then writes `raster3d.hlsl`'s
two targets, which `compose.hlsl`, the one-screen picture and the capture read
as before. The native sequences (a mod's KH2-style HUD) blend with the scene in
ways the card's blending cannot, so the list from the first of them on is drawn
by `raster3d.hlsl`, starting from what the targets hold. The pictures are
`gpu3d.c`'s but where the card's interpolation moves a texel or an edge by a
pixel (the render test's limits, docs/BUILD.md).

### KH2's effects

A KH2 effect's triangles (`runtime/mods/kh2_corridors.cpp`, the Dark Corridor) are polygons of their own
(`KHDAYS_G3D_EFFECT`, `khdays_g3d_effect_polygon`): drawn after the translucent polygons and before the selection
markers, in the order sent (KH2's: its layers', a model's primitives far to near), each with the PS2's blend its GS
ALPHA byte names (`pltt`) and its depth rules: tested nearer unless `_NO_DEPTH_TEST`, written with `_DEPTH_WRITE`,
the depth alone with `_DEPTH_ONLY` (the corridor's mask, which hides its bowl outside its opening). The colour is the
picture's times the vertex colour (x 2 at most, as the GS's 0x80 is 1); without a picture it is the vertex colour
itself (0x80 half of full, as the GS draws with textures off). Its alpha is the source alpha of the blend.
`gpu_native3d.c` draws them into the colour target alone, the blend by the card's (0x44 mixed, 0x48 added, 0x42
`Cd - Cs x As` reversed subtraction, 0x46 `Cd x (1 - As)`, 0x88 `Cs x As`; another byte as 0x44, said once), the
stencil untouched; `raster3d.hlsl` the same formula per pixel (`g3d_polygon.hlsli` `gs_blend`). `gpu3d.c`, as for
mods' models, leaves them out.

## Mods' graphics

What a mod may replace and how it is named is in `README.md` and
`RUNTIME_MAP.md`; this is how the port finds and draws it.

**Names.** A file's data is read from the ROM as the game's loads read it
(`runtime/mods/names.cpp`): a packed entry of a "P2" archive (the header
`Archive_OpenSubfileByHandle` reads: u16 count at +2, the data's start at +0xc,
u16 offsets in 0x200-byte units at +0x10, then the u32 sizes, bit 31 when the
entry is LZ-compressed), or a whole file, LZ-compressed when its path ends in
`.z`. The NitroSystem resources inside it (BMD0, BTX0, wherever a KAPH or D2KP
container put them) are found by their headers, and their TEX0 dictionaries
give each texture's and palette's name and place. A 2D tile sheet is an NCGR's
tiles, laid out `columns` to a row (the NCGR's own width when it holds them
all, else 16, or fewer).

**3D textures** (`runtime/mods/textures.c`). A polygon names its texture by
where it sits in the texture slots (TEXIMAGE_PARAM) and its palette by where
that sits in the palette slots (PLTT_BASE); the slots' pages say which bank
holds them, `origin.c` which file and offset that bank's bytes came from, and
`names.cpp` the names there. A texture is looked up once for each slot place,
format and palette, and again only after its bank changed. A replacement the
runtime changes as the game runs (a movie's frame) is written between
`khdays_textures_change_begin` and `_end`, which moves its version; a reader
on another thread copies it between `khdays_textures_read_begin` and `_end`.

**2D tiles** (`runtime/mods/tiles2d.c`). A tile the layers or sprites draw is
named by where its bytes came from: its file's tile sheet
(`<file>[#<entry>]/chr/<n>`) and its number there. A mod's PNG of that name
(any whole multiple of the original's size) is drawn instead at its own
resolution -- indexed, with the game's palettes (its fades and palette effects
kept), or in colour -- only while the tile's bytes are still the sheet's: tiles
the game draws over itself (its text) stay its own. An indexed sheet's pixel is
its nearest texel's. In colour, every texel under the output pixel counts by how
much of it the pixel covers (its colour weighted by its alpha), read from the
smaller copy with one to two texels to the pixel while a copy's texel still
comes from one tile: a sheet larger than shown keeps its lines, and its soft
edges show what is under them by their alpha (`KD_SEE_THROUGH`), the pixel
composed with the tile and without it, as the elements are. A sheet smaller than
shown (an output pixel inside one texel: a 6x sheet at a 4K window's 12x) is read
through Catmull-Rom over the 4 x 4 texels round the pixel, those past the tile's
edge taken at its edge (the tile beside it in the sheet is not the one beside it
on the screen), so its edges stay smooth instead of showing the texels' steps --
but a sheet at the DS's own size (1x, a texel to a DS pixel) is read at the texel
under the pixel, sharp, as the game's own pixels are (smoothed, a mod's 1x
sprite in colour came out blurred in the camp's menus, a tester, 2026-11-07). A
frame of 2D is composed at the 2D's scale (above) even without 3D: at the DS's own
size, which a frame of 2D alone kept before (now only the 2D check,
`KHDAYS_GFX=check`, compares there), the sheet was averaged down to the DS's pixels
and stretched (the Journal's pages, seen blurred at 4K). The map of replaced tiles
holds, where a replaced sheet's tile starts, the replacement's id + 1 in bits
20-31 and the tile's number in bits 0-19; a page is mapped again when its
memory, its bank or its bytes' origin changed.

**Models** (`runtime/mods/models.cpp`). The game draws its model as always; the
port takes each node's matrix as it is sent, leaves out the model's own shapes,
and at the draw's end draws the mod's meshes with those matrices, skinned on
their own skeleton and lit as the game's material is. Matrices are column-major
for column vectors (`m[col * 4 + row]`), as the neutral formats and glTF have
them (and, read as they lie, the DS's row-major arrays for row vectors). For
each node G of the game's model the port knows its rest matrix W_G and, during a
draw, the matrix M_G the game sends; D_G = M_G x inverse(W_G) takes a point at
rest to where it is drawn now. A joint j that follows G is drawn with
K_j = T(where its head is now) x linear(D_G) x C_j x T(-head_j): its rest turned
by C_j (the rest pose correction, for a T-pose unlike the game's), then as the
game turns G; its head is where its parent's K puts it, so the mod keeps its
own proportions. A joint at G's place with no correction is drawn with D_G
itself; one that follows no node moves with its parent. C_j turns a joint at
rest when its way to its one following child points more than 20 degrees from
the game's; `pose_degrees <n>` in `<model>.bones.txt` sets that threshold for
the model (KH2's Roxas: legs hanging straight against Days' 10-11 degree splay
came out with the ankles crossed under 20). `ik <upper> <lower> <end>` bends a
two-bone limb of paired joints after that, so the end reaches as the game's
nodes reach: the target is the game's end node seen from its upper node, scaled
by the two limbs' lengths, bent in the plane of the game's middle node (the law
of cosines about an up vector, as KH2's `Motion::calcIKJoint`); the upper and
lower joints are swung to it, keeping their nodes' twist. `ik ... exact [<x> <y>
<z>]` aims the end at the game's end node itself instead (plus an offset, in
centimetres on the rest axes, turned as that node turns), so what the game's
animation puts in contact stays there: a hand on a bed, a foot on the floor. The
scaled limb missed it: KH2 Roxas's shoulders stand 8.5-10 cm over Days' and his
arms are 7-9 % longer, so even at rest his hands hung 5-8 cm from the game's. For
the feet the offset keeps the soles together: KH2's ankle stands 15.1 cm over its
sole and its heel reaches 8.6 cm behind it, Days' 9.3-10.0 and 10.2-10.6, so
`exact 0 5.0 1.6` (roxas_b_h) and `0 5.7 2.0` (e_rox_hb). It trades the bend for
the place: over the 161 cutscene animations of `mi/mo/ro.p2` on e_rox_hb (scale
0.93), where the game bends more than 10 degrees, the exact hand lands 0.7 cm
from the game's (p90 3.8) with the elbow 18 degrees straighter (p10 40), the
scaled one 5.2 cm (p90 11) with the game's bend; the feet 0.0 cm with the knee
11 degrees straighter, scaled 3.3 cm (p90 9). KH2's shoulders follow his own
torso, 2.4 cm over the game's (+-4.7 by pose), which put the exact hand out of
reach in 54 % of arm frames (the elbow locked straight) and a hand brought to
the chest inside his; so the arms keep the scaled reach and the legs `exact`. `align <joint> foot|hand`
turns an end whole onto the game's at rest by its shape, the vertices weighted
to the joint and its unpaired children against those of its node: a foot about
the vertical by the way to its front fifth, a hand by the way to its far fifth
(the fingers) and its flattest axis. The rest correction above only turns a bone
toward its one paired child, never about itself, and hands, feet and the head
have none, so each copied the game's turn from rest onto a rest that differed:
Days' feet stand 11.7-12.5 degrees toed out where KH2's corrected 4.5, and the
flat of the hand was 27-31 degrees off (fingers 11-12), in every pose. The joints
then carry what the game attaches to its nodes: `NNS_G3dGetResultMtx`, which
`Obj_RenderModel` and `Scene_DrawNode` read after the draw to place a model's
attached objects (a held prop), returns for a replaced model each node's frame
where the joint of that node, or of its nearest ancestor with one, carries it
(the joint's frame turned to the node's as at rest, then the node's rest place
under it; a `weapon` line's turn for its node); the game's own nodes were
otherwise followed (`models.cpp` `khdays_models_result_matrix`). `scale <n>` sizes
the model and, through the root node it is placed on, its KH2 motions (their
`offset`s too) and its secondary motion (which simulates in the model's own
units); an `ik exact` offset scales with it, and what the game attaches (a
`weapon` line, the result matrices) keeps the game's size. The cutscene Roxas
(`e_rox_hb`) is 0.93: game/KH2 height 0.95, shoulders 0.93, arms 0.93, head
joint 0.91, legs 0.89, hip joint 0.83 (one scale cannot match his short legs; the
upper body, where cutscenes touch things, does). `twist <joint> <by joint> <axis
from> <axis to> [<share>]` turns the joint and those under it about the way from one
joint to the other (at rest, as the joint's parent carries it) by a share (default 1)
of the other joint's twist about it against that parent (past 135 degrees eased back to none at half a
turn, where +180 and -180 degrees are one pose: passing it flipped the cuff a whole turn in a frame), after the pose and before
the secondary motion (`models.cpp` draw, `kh2::twist_about` / `twist_turn`): N_EX830's
cuffs ride the wrist (113: the sleeve's last ring on 115 and the cuff on 116), and
P_EX110's motions and Days' animations turn the hand alone, so `twist kh2_114 kh2_133
kh2_112 kh2_113` (and 231 250 229 230) turns them with it. A KH2 motion that turns the
joint itself has authored that twist already, so its share of the pose is left out: the model's own mset's
motions turn every bone, a retargeted one (`skeleton`) only the bones a `bone` line or `carry` gives it, and an
idle fade's held pose counts as it was shown (`kh2_own_share`, `kh2_retargeted_share`). Twisted again on top,
saix_h's cuffs in his KH2 idle stood up to 14.6 cm off KH2's (replay `--days`); riku_h's and roxas_b_h's cuffs,
which P_EH000 and P_EX110 do not key, keep the whole twist in KH2 motions too. `swing` takes the same share. `mesh_scale <joint>
<x> <y> <z>` scales that joint's own vertices in its own rest axes, applied last
(skin x B x S x inverse(B), B its rest frame): its bone, its children, the
weapons and the secondary motion's colliders keep their sizes (the secondary motion
runs on the unscaled skin; its colliders are sized in its own file). roxas_b_h's
pelvis, kh2_2, is 0.9 along its y, which points backward, to keep the butt under
the coat. A 0 is drawn as a
thousandth, so the normals stay finite.
`hide <joint>... from <top> to <bottom> [fade <length>]` (the model's own
centimetres at rest) leaves out what those joints carry most between the two
heights and fades it back in below over `fade`: a triangle with a hidden corner is
dropped (but at the bottom edge, within `fade` of it, where it fades from clear: a
faded top edge showed the hips through the coat), one in the fade goes to a blended mesh of its own (the same picture, its vertex
alpha the fade's), the rest stay as they were (the whole body blended would be
drawn translucent, its parts covering each other wrongly). e_rox_hb: `hide kh2_2
kh2_5 kh2_6 kh2_14 kh2_15 from 99 to 35 fade 8`, its legs from below the coat's
waist ring down to halfway down the calves, fading into the coat's dark inside
(206 triangles dropped, 28 fading).
`place <joint> [<x> <y> <z>] [ride [<bone>]]` (`ride`, for a weapon bone: the paired bone
named, or the nearest above, is left as it is, and the joint moves by how far that bone
is from its own node, so it stands from the KH2 hand as the node from the game's) puts a paired joint on its game node's own place
(plus the offset, the game's centimetres in the node's rest axes) instead of where
its parent carries it, and the bone above it, when that is its only paired child,
swings and stretches along itself to reach it, with the unpaired joints riding it
(a twist bone); a limb with a placed joint is not reached (`ik`). e_rox_hb places
its elbows and hands, so a hand the cutscene brings to the chest lands where the
game's does; its legs keep `ik exact`. Placing its shoulders tore them from the
chest (the clavicle swung out to the game's shoulder, the chest and hood stayed on
KH2's torso). A bone with several paired children (the pelvis) is not stretched.
`spine <base> <end> [hips <joint>] [onto]` makes the torso follow the game's: the base
goes where the game node of the chain's first paired joint carries it (the whole
body with it), and the chain, as the game's turns pose it, is turned about the base
onto the end's place and every bone lengthened or shortened alike to reach it: the
game's curve kept. Bending it further instead (FABRIK) folded it over in close
reaches, a turn of 5-23 degrees past the game's own in 59 of 5933 stored frames;
lengthened, 17, all inside the game's own fast turns of 45-66 degrees. `hips`: the
pelvis (that joint's nearest paired joint above) turned as the base's node, then
swung and stretched so the joint is where the pelvis's own node carries it. The
joints off the chain are posed again from their parents, so the shoulders and
head keep the game's turn from wherever the bent spine puts them
(`model_parts.h` `bend_spine`). e_rox_hb: `spine kh2_96 kh2_166 hips kh2_3`. With `onto` the end goes on the
game node's own place (as `place`), so the chain takes the game's torso length, not the
model's: for a model built otherwise than the game's (xaldin_h: hips to neck 76.1 cm to
the game's 66.9), with its limbs `place`d. `hips` still swings the pelvis where its node
carries the model's own hip joint: swung toward the node's own place of a larger game
skeleton (`game_scale`) it squashed the pelvis under the coat's waist.

`game_scale <k>` takes the game's skeleton k times larger about the actor's origin before
the model is posed on it: each node's D(p) becomes O + k (D(p / k) - O), its turn kept, and
the nodes' rest places are scaled at load, so `place`, `spine ... onto` and the `ik` limbs
reach a skeleton of the model's size while the game's turns stay as they are. xaldin_h:
1.041, the game's neck where his own stands; his legs' knees and ankles then land about
on his own. `align <joint> rest` gives a joint no rest correction at all, so the turns its
parents take at rest (a shin turned onto the game's) do not tilt it: a foot's sole stays as
built (xaldin_h's toes stood 5.5-6.2 cm up in the game's idle). `lower_hips <share>`
lowers the whole body in the game's own animations (along the root's up, after the
spine and before the limbs reach) by the most any `exact` leg needs for its hip to stand
from its foot, over the leg's length, as far as the game's thigh node from its foot over
the game's leg: the height of moving the hip toward the foot by what it lacks (never
raising it; `model_stance.cpp` `lower_hips`), so a hanging leg is matched and one trailing
behind asks little. The feet stay on the game's, so the knees bend as the game's do.
Solving for the drop that reaches the wanted distance instead dropped the body to the
foot's height wherever no drop could reach it, and back: with the pelvis turned under
(KH2_MODELS.md, `ds_pose`), its hips forward, marluxia_h's run bobbed 39 cm, 20 in a
frame; now 12, at most 2.5 a frame. KH2 legs that are shorter for
their hips locked straight where the game's bent: marluxia_h's right knee 175 degrees in
`b_ma_Ca005`, the game's 166, larxene_h's 175 to 141 (`Ca006`), riku_h's 175 to 159
(`Ca006`); with it 166, 133 and 158, his body 3.5 cm lower there, 5 (up to 9) in the run. A
`place`d leg already follows the game's knee and gains nothing; where the legs reach out
level with the hips (xemnas_h's `Ca006`) moving down cannot bend them. KH2 motions keep
their own hips. An `exact` leg bends the way the game's knee stands off the game's own
hip-to-foot line (seen from KH2's hip, which stands a few centimetres apart, a straight
game leg seemed bent aside), and toward the model's front (KH2's +z, as the hips turn)
where the game's knee is within a tenth of its thigh of that line: in the game's glide
hover (`b_ma_Ca000`) marluxia_h's right leg is straight (179 degrees, a degree backward),
and lowered by the other leg's need, his knee bent 6 cm aside and back; now 6.5 cm
forward (165 degrees). `ik_side kh2_hip` keeps the bend as it was before (toward the game's knee as seen
from the model's own hip): demyx_h, set up as before all of this (user). Over the
161 cutscene animations its neck was 3.7 cm from the game's (p90 13.5, max 36) and
its shoulders 4.9 (max 36); bent, the neck 0.0 (max 0.4), the shoulders 2.5 (max
6), the chest 0.3; the spine 0.94-1.28 its length (1st-99th percentile, max 1.36). On the
game's pelvis the waist needed up to 1.9; on its lower spine (`Bip01_Spine`,
81 cm, where the game bends, below KH2's waist at 99) 0.88-1.05, and the pelvis
turned as that node puts the hip joint within 0.6 cm (median, p90 5) of the
game's pelvis's carry, the swing a median 2 degrees (max 21; turned as the
game's pelvis it would be 10, max 110). The legs cannot match: at 0.93 KH2's
hip joint stands at 82.3 cm and its knee at 51.2, the game's at 73.1 and 44.8
(KH2's ankle stands 5 cm higher in its boot, its thigh is 31.1 cm to 28.7, its shin
37.6 to 37.2), so with the soles on the game's (`ik exact`) KH2's knee is 7.0 cm
from the game's (median, max 15.5), outside the game's coat. So e_rox_hb places
its hips, knees and ankles on the game's leg nodes (`place kh2_5`, `kh2_6`, `kh2_9
0 5.3 1.86`: the ankle's offset keeps KH2's sole on the game's), each thigh and
shin stretched or shrunk to reach; its coat is locked on the game's coat
(SECONDARY_MOTION.md, `game_shape`), which then always covers its legs. A mesh
is drawn both
sides or front only as its file says (glTF `doubleSided`; FBX both), unless
`<model>.bones.txt` says `cull back` or `cull none`. The game's own shapes are
left unsent but for those `keep <node> ...` names: a shape whose vertices all
hang from those nodes (through the palette's bones) is known by its display
list's place from the model's start, the place the game's SHP command sends it
from, and sent as the game sends it (a chest's light rays under a KH2 chest).
`switch <setting>` ties the model to an HD replacement of the settings
(`hud_settings.h`'s keys): while it is off, the game's model is drawn. A blended
mesh's vertex
alpha goes across each polygon, pixel by pixel, times the polygon's (the list's
vertices carry it: `KhdaysG3dVertex` `a`, 1 for the game's, whose polygons have
one alpha as the DS's): taken whole per triangle, KH2's sun glow came out a disk
with a hard edge and its cloud layers in patches. Its polygons are cut by
the clip volume but for the game's far plane, which neither cuts nor hides them
(the DS's far-plane rule hid a polygon reaching past it whole: a KH2 room's
floor, a few large triangles, went at once); what lies past it is drawn at its
depth (`gfx/g3d_geometry.c` `finish_mod_polygon`). A room of a mission world
whose world the same mod replaces is drawn with HD graphics off too: its new
ground is played either way. A KH2 model (`.mdlx`) is imported into the same
neutral form, its bones as the joints (`KH2_MODELS.md`).

**A model that moves by itself** (`runtime/mods/room_animation.cpp`; the
studio's KH2 rooms are made so). Its glTF's own animations move its nodes: a
joint that follows no game node is drawn with its node's matrix now times its
inverse bind, so a piece skinned to its own bones moves as the file says (a
mesh on an animated node with no skin is made still on loading, and the log
says so). The port's glTF extras say the rest: `khdays_sky` on a node (its
meshes drawn as KH2 draws its skies: around the camera, turned with the room but
not moved, and behind everything else, painted in turn with no depth among
them -- the opaque and cut-out meshes in the file's order, then the blended
ones, each layer a step nearer in a band of depth at the far end, set once the
near plane has cut them at their true depth. Placed at the room's origin, a
sky's inner ring of hills and trees was seen edge on as zigzags; with one depth
for all, or their distance's, its layers covered one another in wedges; set
before the cut, a sky triangle reaching behind the camera came out near it and
hid the room), on a material `khdays_scroll` `[u, v]` (texture coordinates, 0..1 over
the picture, added each game frame) and `khdays_frames` `{"pictures": [...],
"frames": [[picture, game frames], ...], "loop": true, "loop_from": i}`
(pictures in turn), on an animation `khdays_loop` and `khdays_loop_from`
(seconds). Its clock is the game's frames (60 a second) from its first draw,
standing while the game is paused (gPauseMode); the studio's room preview
moves it by its own clock.

**The colours of runtime-built sheets** (`runtime/mods/sheet_colours.h`). For
the studio's sheets whose layout the game builds at run time (the field HUD's),
while the graphics dump is on the palette row each tile of a named sheet is
drawn with, and that row's colours, go to `<dump>/<name>.colours`:
`row <r> <colours>` (BGR555 in hex, 16 or 256 for 8-bit tiles) and
`<first>-<last> <r>` (these tiles most often drawn with row r's colours).
Colours are kept per tile; other colours a row had get a row of their own from
17 on. What the file holds already is kept where this session saw nothing, so
plays add up.
