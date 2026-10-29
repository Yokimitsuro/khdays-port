# How the studio works

khdays-studio (`studio/`, using it: `studio/README.md`) is a mod authoring tool
beside the port, built on its own (`studio/build.py`) so nothing of the port's
or the asset library's builds changes. It compiles some of the port's portable
files as they are, by path -- where the port decides something (which mod wins,
how a sound or a PNG is read), the studio asks the same code -- and edits none of
them. The game's formats it reads and writes are in `FORMATS.md`.

## Containers rebuilt

A game file is rebuilt with one piece changed and everything else as it was
(`core/files/containers.h`): a P2 container's tables, the bytes between them and
`base`, zeros between entries, sector ends; a KAPH's sections, 0x20-byte block
starts and zeros between. An LZ file keeps its original's type.

## Models and animations

**A mod's model on the game's skeleton** follows the port's rules
(`port/runtime/mods/models.cpp`; `RENDERING.md`, "Mods' graphics"). That code is
bound to the running game, so `core/models/bones.h` mirrors its rules, each
citing the lines it follows: a change there must be made here too, and the tests
and the in-game comparison check the two agree.

**An animation replaced** (`core/models/anim_replace.h`, `bca0.h`): the game file
holding it is read as the port's dump finds it (a P2 entry, or the whole file,
LZ when its name ends in `.z`), its KAPH's BCA0 block replaced, everything wrapped
again as it was, and the result goes to `mods/<Mod>/files/<game path>`, the
port's whole-file replacement. The new J0AC holds all of its own data and is
appended at the end of JNT0, its dictionary entry pointed at it: the dictionary
and every other animation stay byte for byte, the old J0AC unreferenced. The
nodes the new animation moves are written whole, every frame sampled
(translation 20.12, scale with its inverse 20.12, rotation as the compressed
orthonormal basis of 13-bit 1.12 values); the nodes it leaves alone keep the
original's node blocks byte for byte but for their data offsets, which point
into a copy of the original's shared data, with the new rotations after its
basis table. Frame and node counts are the original's (what else the game times
by them is not known). The BCA0 is found in a copy by its block's order, so a
copy whose earlier blocks grew still finds it, and the kept curves come from the
game's own, so replacing an animation again starts from the original.

A clip comes from a glTF or glb file (`clip_import.h`): its nodes named like the
game's are sampled at as many evenly spaced times as the original has frames,
each node's T x R x S as glTF composes them; the nodes it leaves alone keep the
original's motion. It is meant to be made on the game's own skeleton, the one the
dump writes as glTF. Of a file with several animations (a model exported with
its animations) the one named as the export named the animation replaced is
taken, else the first; a clip that moves none of the game's nodes is refused.
`check_all_animations` rebuilds and reads back every animation of the game: with
every node kept (it must read back exactly), every node replaced by its own
motion, and every file rebuilt half kept, half replaced.

`--export-model <model> <out.gltf> [animations]` writes a model as "Export this
model..." does, its textures beside it (the dump's own glTF's pictures, else its
first picture of each texture) and its animations at 30 frames a second; with
animations, each is read back as "Replace this animation..." reads a clip and
compared with the game's: at frames where a node's rotation is one of its curve's
samples, how far the matrices are apart; between samples, how far the export's
rotation turned from the blend the reader makes.

## Rooms

A room of a mission world replaced by the modder's model
(`core/rooms/room_change.h`):

```
mods/<Mod>/studio/rooms/<code>/<room>.txt   the change (the port ignores studio/)
mods/<Mod>/studio/rooms/<code>/<room>/      the modder's model files beside it
mods/<Mod>/files/mi/wd/wd_<code>            what the port reads: the world's archive,
                                            every replaced room's collision new
mods/<Mod>/graphics/mi/wd/wd_<code>#<member>/mdl/<model>.gltf
                                            each of the room's models drawn as the
                                            modder's (or as nothing)
```

A model made from the game's room (its dump, or `--room-model`'s export, edited
in a 3D program) is lined up with it again: the turn in quarter turns about x, y
and z and the scale (1, or the 100, 10, 0.1, 0.01 a 3D program's units add)
under which most of its points fall on the room's own (`share` 0.25 or more:
the room's model, lined up; the move left at 0). A model that shares nothing
with the room keeps its placement.

The collision is made from the model's triangles, placed (a mesh drawn both
sides gives its steep faces, facing less than 60 degrees from level, a second
turned the other way; made from the visible model, a translucent material's
triangles -- glows, glass, fog, a sky's haze -- are passable). It keeps the
original room's header, placement markers and named records (so every name the
room had is still there); the faces are new -- coplanar neighbours joined into
convex pieces of four corners, each classified by its facing, its names from the
rules in the slots their kind takes (gates slot 0, the others 1 and 2), a
ground's slot 3 and last word from the nearest ground of the original -- and the
tree is built over them (a root square of a power of two holding every face).
It is checked before it is saved: written and read back the same, every face 3-4
corners, convex, its bounds its corners', a plane the game can build, every slot
naming a record or none, the tree as the walk wants and no deeper than it goes,
every name the original had still there.

**A KH2 room** (`core/rooms/kh2_source.h`). A map of KINGDOM HEARTS II FINAL
MIX is taken out of the player's KINGDOM HEARTS HD 1.5+2.5 ReMIX -- its folder
the studio's setting `collection_1525`, else the one the install made its HD
pieces from (`setup/state.json`) -- by the installer's own reader
(`installer/collection_archive.py`, run with the install's Python,
`setup/python`, or the system's in the repository's tree), asking kh2_first,
kh2_fifth and kh2_sixth in the collection's language folder (`Image/dt`,
Steam's; `Image/en`, the Epic Games Store's) into
`saves/collection/kh2/original/map/<map>.map` and
`remastered/map/<map>.map/-N.dds`; `--has` lists the maps a copy holds. The
map is made into `saves/collection/kh2/made/<map>/<map>.gltf` (assets'
`kh2_room`: its background, skies and placed pieces with their motion, its HD
pictures as PNG, docs/RENDERING.md "Mods' graphics" for its extras), and the
change names it (`kh2 <map>`). Its first place: where Days put the map when the
pair says (below), else lined up by its points when it shares them with the room
(the Usual Spot is the same place in both games), else KH2's units as the game's at
0.01, its floor under its middle at the height of the room's ground under the
room's middle, the two middles together. Days rebuilt many rooms with their own
geometry, often at another height: those are lined up by eye -- dragging Scale,
Turn or Move shows the model where it goes as it is dragged (the collision made
again once let go), "Both" shows the game's room with it. "Only what lies around
the room" (`zone <units>` in the change) keeps of the model only what lies within
so many units of the game's grounds and walls, or between them, from above. The
collision made from it names each marker left in a wall (a wall face within
0.15 units of the point half a unit above its feet) or with no ground under it.
What is made is the player's own (Square Enix's work from their copy): the
panel says so. The KH2 map each room was made from is
`studio/data/kh2_rooms.txt` (`<world> <room> <map> [turn <degrees>] [move <x> <y>
<z>] ; <evidence>`, names only, built into the studio; RUNTIME_MAP.md section 4 says
how they were found). Where Days moved a map (ten rooms of Beast's Castle, The World
That Never Was and Twilight Town's underground), the turn and move are the ones most
of the room model's corners agree on with the map's, at KH2's scale; a mod's
`studio/kh2_rooms.txt` adds pairs or replaces them line by line. The panel offers
the room's own pair first, marked, with its evidence. The map's own floors and walls (its `ID_`
collision, `kh2_room`'s `collision_scene`) are a fourth choice for a KH2 room's
collision: written as `<map>_collision.gltf` beside its model and taken like
another model's, placed as the model is (the Usual Spot's: 334 grounds, 847
walls, 155 ceilings, a fifth of the floors made from its visible model).

## Missions

A mission is changed in place (`core/missions/mission_edit.h`): a step's
operands (a point, a number, a time), its texts, the enemy table's groups, the
mission's chests and emblems; written into the mod as the game reads them
(`files/mi/mi/<number>`, and `files/mi/mi/trbox` and `evi` with this mission's
set changed). Nothing moves: a step keeps its length, a table its size, so every
offset the game follows stays where it was; a text that grows is written past
the script's data and its place there pointed at it. A mission made from another
takes `from`'s content (world, rooms, enemies, starts, texts) as mission
`target`, whose place in the Mission Mode list, its name there, its unlock and
saved progress stay the target's (kept by its number); its chests and emblems
are copied when the target's sets have as many rows.

### New missions

A mission of the mod's own (`core/missions/mod_missions.h`) is listed in Mission
Mode after the game's, in its world's category: `missions/missions.txt` holds a
block for each (`[mission <key>]`: `name <lang>`, `objective <lang>`,
`description <lang>`, `category`, `world`, `room`, `level`, `time`, `unlock`,
`template`), and `missions/<key>/mission.p2` its file, a game mission
file; without one the template's file is played as the game has it. The tool
writes both and reads them back with the port's own reader
(`port/runtime/missions/registry.c`) before it says they are saved. In the
window **Missions** (top right; also a mission's tab **The mod's new missions**):
**New mission** (a key, a world, a room of it); and for each mission its **Places
on the map** (below) and its **Words and settings** (each one's words in the five
languages, where it is listed, when it opens; **Save to the mod** writes and reads
back each change). Another mission chosen, or the studio closed, with changes not
saved asks first. A mission's own **New mission from this one** makes one from a
game mission. From the command line:

    khdays-studio --new-mission <Mod> <key> <game mission> <world> <room>
    khdays-studio --mission-set <Mod> <key> <setting> [<language>] <value...>
    khdays-studio --check-mod-missions <Mod>

`--check-mod-missions` reads every new mission as the port does, its world and
room, its file, and the rows the port adds to the list in each language; it exits
1 with a line for each problem.

**Waves, chests and emblems.** A mission with waves needs no file of its own:
the port builds it at its start (`core/missions/mission_build.h`, compiled into
the port as it is) from its block's lines, out of the game's own steps (1354's
area, start and ending, 0004's "all defeated" check), with its world, room, start,
time limit, notice and objective:

    start = <x> <y> <z> <facing>               # where the players start, degrees
    wave <n> = <class>x<count> ... at <x> <y> <z>   # a spawner of wave n (1-255)
    chest = <contents> at <x> <y> <z> <heading>     # a chest (the game's contents number)
    emblem = <kind 0-2> at <x> <y> <z>          # one of each kind at most

Wave 1 comes out at the start, each next wave when every enemy of the last is
defeated, and the mission is cleared when the last wave is (or failed by its
time). An area takes four enemy classes at most (the game's); a class must be
one a game mission has (the window lists them by name). The chests and emblems
are added to the game's `trbox` and `evi` as they load and put out in Mission
Mode for new missions; a chest's opened flag is kept in the port's save part (an
emblem's pickup is made at its place, but it is not drawn nor picked up yet).

**Places on the map** (`app/views/mission_map.cpp`) shows the mission's room as
the game has it -- its models and, over them, its floor -- or as the mod replaces
it (the mod's world file and its model of the room), with the players' start (its
facing drawn), each spawner (its wave and how many enemies), each chest and each
emblem on it. A place is dragged over the floor (it stands on the floor under the
pointer, the collision's grounds); one is added by choosing what (**Players'
start**, **Enemies**, **Chest**, **Emblem**) and clicking the floor; the one
clicked is changed beside the picture: where it is, onto the floor, its facing, a
spawner's wave and enemies (by name, from the classes the game's missions have,
with how many of each), a chest's contents, an emblem's badge, or it is removed. A
place over no floor is ringed red. **Save the places** writes the start, waves,
chests and emblems at once where the first of them was in the mission's block
(`set_mod_mission_spots`), when the mission reads as the port reads it and builds
as the port builds it; otherwise the file is left as it was and the reason told.
From the command line (each change checked by building the mission):

    khdays-studio --mission-wave <Mod> <key> add <n> <class>x<count> ... at <x> <y> <z> | clear [<n>]
    khdays-studio --mission-chest <Mod> <key> add <contents> at <x> <y> <z> <heading> | clear
    khdays-studio --mission-emblem <Mod> <key> add <kind> at <x> <y> <z> | clear

## 2D sheets

A 2D tile sheet is seen assembled and edited as a picture
(`core/sheets/sheets.h`): the sheet as the port names and replaces it, the
screens, cell banks and palettes in the same data, a picture assembled from one
of them (which goes with which is the game's code, not its data, so it is the
modder's choice), and an edited picture taken back into the sheet the port reads
(an indexed PNG is the game's palette indices, any other its own colours, a
whole multiple of the sheet's size).

- **Candidates**: the screens of the sheet's depth whose tiles are all in it, the
  cell banks whose pieces' tiles are all in it, then the sheet as it is. Their
  palette: in data holding as many palettes as sheets, the sheet's own number's
  (they come in threes there); else the first of the sheet's depth; with none of
  its own, the first nearby one that gives the sheet's indices at least half the
  colours the liveliest nearby one does (an all-black or mask palette is passed
  over).
- **Ranking**: those that join up (border jump 0-2, drawn with each one's palette,
  drawing some of the sheet) by the share of the sheet they show over their
  jump; not a screen laying the tiles in order at another width (it assembles
  nothing); one laying them as the sheet does and showing at least half of it
  stands for the sheet as it is, in the palette row it gives them. In data
  holding as many screens (or cell banks) as sheets the sheet's own number goes
  first unless it scores under 0.8 of the best. Shown together at first: the
  first, then each one whose tiles are at least three in four not shown yet;
  the sheet as it is instead when together they show under half the sheet and it
  joins up better. After them the others, those that draw nothing, the sheet as
  it is.
- **A sheet with no screens or cells of its own** borrows its twin's: of the
  sheets of the same depth, tiles and columns, in data with screens or cells in
  its file or the file without its language, the one with the most drawn tiles
  byte for byte its own, at least one in eight (`UI/shop/res_i18n.p2#9` shares
  151 of its 352 tiles with `UI/shop/res.p2#0` sheet 1), its palettes first; or,
  for a file of its own, the screen and cell files of its folder, the name most
  like its own first.
- **The colours seen in play**: the port writes `<key>.colours` while the game is
  played with the graphics dump on (`RENDERING.md`); made a palette of the sheet
  (the rows seen as the game had them, greys for rows never seen), it colours
  the sheet as it was drawn, tiles never seen in greys. A row holds 16 colours,
  or 256 for 8-bit tiles; rows from 17 on are the port's, for colours a row had
  for other tiles (the HUD's slots take each portrait's palette into one row).
- **Export**: every sheet of the game written as `<key>.png`, as the window
  first shows it, with the colours seen in play.

## Movies

A movie of the game is seen and heard as each video it can be -- the game's own
(its preview converted to MP4 in `data/preview`), the player's HD video, each
mod's -- and a video saved into the mod in its place. The port takes two: an
MPEG-1 `movies/<id>.mpg`, played in the game's movie wherever it shows
(`port/runtime/movies/video_mod.c`), and an MP4 `movies/hd<id>.mp4`, played
instead with the movies set to HD. A mod holds one of them per movie. The videos
play through the port's own player (Media Foundation reads both).

## Previews

The 3D previews (models, rooms) gather triangles on the processor and draw them
on the graphics card (`studio/shaders/preview.hlsl`, built once into
`studio/app/generated/` by `studio/tools/build_shaders.py`, the port's steps
reused) into a picture twice the shown size: depth-tested, textured as the DS
samples (nearest, each axis repeated, mirrored or clamped as the material says),
times the vertex colour, a light from the viewer's side on vertices with a
normal, translucent ones blended after the rest.

## The mods' console

Console (beside Themes) shows the running game's mods' log (`saves/logs/mods.log` of the port's
folder, `docs/SCRIPTING.md`): each line with its mod, kind, file and line, the mods' last states,
filtered by mod or to errors. Reload writes the mod's id (or `*`) in `saves/mods/reload.txt`; the
game reads it within a second and loads that mod again. The tool never writes the log.

## The generated tables

`studio/tools/command_names.py` writes `core/missions/command_names.cpp` from the
pinned decompilation's command tables (who installs which table in each group:
its `StoreGlobalArrayEntry` calls), and `studio/tools/enemy_names.py` writes
`core/missions/enemy_names.cpp` (each enemy id's overlay's name, its variant
number left out). Both are committed; a table the decompilation does not have is
left out.
