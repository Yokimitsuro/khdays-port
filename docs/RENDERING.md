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
with an image the port drew at the internal resolution and a place in DS
pixels. The description's `KD_ELEMENTS` word says how many follow it (on the
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
and engine A's 3D is drawn at the internal resolution by `gpu_raster3d.c`. Each
frame uploads the package's polygons, an index of which may cover each 8x8 tile
(in drawing order), the composition records and the texture pages that changed.
`raster3d.hlsl` runs `gpu3d.c`'s rasterizer for each pixel -- each thread owns
one pixel and goes through the polygons that may cover it in drawing order,
keeping its state as `gpu3d.c`'s `Pixel` does, so translucent IDs, the shadow
mask and blending over alpha 0 are exactly the reference's; where `gpu3d.c`
computes in double (the depth) it computes the same integer in float and
integer steps, its coverage and interpolation kept from being fused
(`precise`). `finish3d.hlsli` then does the edge marking and the fog, which
need the finished neighbours, and `compose.hlsl` composes it with the 2D as
`gpu2d.c` does from the composition records, the master brightness last. A
picture made larger than it is shown is shrunk by `shrink.hlsl` (each texel the
area-weighted average under it): the 3D's edges come out smooth (the
antialiasing setting) and the window shows the picture at its own pixels.

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
at the resolution drawn); `KHDAYS_GFX=reference` (the 2D engines' pictures);
`KHDAYS_GFX=check` (the port's way compared with the 2D engines at 1x, or the
old way at N x, every 300 frames); `KHDAYS_PROFILE` (frames never drawn, and
the time preparing the 3D took).

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
on the screen), so its edges stay smooth instead of showing the texels' steps. A
frame whose 2D memory holds a replaced tile is composed at the internal resolution
even without 3D (`has_hd_tiles`, gpu_present.c): at the DS's own size, which a
frame of 2D alone keeps otherwise, the sheet was averaged down to the DS's pixels
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
itself; one that follows no node moves with its parent. A mesh is drawn both
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
ground is played either way.

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
