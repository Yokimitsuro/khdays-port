# The game's data formats

What the port and the studio read and write of the game's files, as the game's
own code reads them (each with the decompilation's function that does). The
graphics, model and sound formats decoded by `assets/` are NitroSystem's
standard ones (NCLR, NCGR, NSCR, NCER, NANR, NSBMD, NSBCA, NFTR, SDAT); this is
the game's own, and what is known of its data.

## Containers

### P2

Most of the game's files are "P2" containers (`Msg_OpenContainerAndReadHeader`,
`Archive_LoadFile`, `Archive_OpenSubfileByHandle`):

| Offset | Type | What it is |
|---|---|---|
| 0x00 | u16 | the magic `P2` (`50 32`) |
| 0x02 | u16 | the entry count (`& 0x1ff`); bit 0x8000 a "wide" flag |
| 0x04, 0x08 | u32 | header words (0x08 holds the card handle in memory) |
| 0x0c | u32 | `base`, where the entries' region starts |
| 0x10 | u16[] | each entry's start sector, in 0x200-byte units from `base` (`((count + 1) / 2) * 4` bytes) |
| then | u32[] | each entry's descriptor: bit 31 LZ-compressed, bits 0-30 its stored size |

Entry k is at `base + start[k] * 0x200`, `desc[k] & 0x7fffffff` bytes long
(the low bits are the size, not an offset: the ROM's `add r2, r2, r1, lsl #9`).
In the game's files the bytes between the tables and `base` are kept as they
are, the gaps between entries are zeros, and a file ends on a sector. A path
holding `&` is expanded with the current language's two letters
(`Msg_BuildLangPath`: `/db/db_&.p2` is `/db/db_en.p2`).

### KAPH and D2KP

The 3D archives (`ba/ch/**`, `mi/**`, the rooms inside the world archives) are
KAPH; the UI packs D2KP. Both: the magic, a zero word, eight section offsets
(0xffffffff or 0: none), each section a count, that many block offsets and that
many block sizes; the blocks start on 0x20 bytes, zeros between them, the file
ending on 0x20 bytes (checked over the game's 313 KAPH). By the game's
convention a model archive has its NSBMD in section 7 and its NSBCA in section
0 (1-6 the material, texture and visibility animations). A D2KP's sections hold
NCLR, NCGR, NSCR, NCER and NANR resources, classified by their own magic.

### LZ

LZ10 (type 0x10) and LZ11 (0x11): the type byte, a 24-bit size (or three zeros
and a 32-bit one). Both occur: of the 1,114 `.z` files 947 are 0x11, 166 0x10
and one neither; of the P2 containers' compressed entries 5,164 are 0x11 and
2,921 0x10. A rebuilt file keeps its original's type (whether the game reads
the other type in its place is not known); Nintendo's own compressor's bytes
are not matched, only what they decode to.

## Text

- **Message databases** (`db/db_<lang>.p2`; `db_en`, `db_es`, `db_fr`, `db_de`,
  `db_it`, `db_ja`): a P2 container whose entries are LZ11-packed lists
  (`P2_ExtractString`): `u32 count; u32 ends[count]; u16 utf16le[]`, `ends[k]`
  the byte end of string k (string 0 from 0, the data from `(count + 1) * 4`).
  `db_en` holds 23 lists, 3,480 strings.
- **String tables** (`UI/**/*.s`, usually `.s.z`): `u32 header_size (8); u32
  count;` then per string `u32 length` (these 4 bytes included) and its UTF-16LE
  units with a NUL. The 136 shipped tables hold 5,007 strings.
- **The story's scenes** (`ev/EV_S.p2` the shared ones, `EV_TT`, `EV_NM`, `EV_BB`,
  `EV_AL`, `EV_AW`, `EV_HE`, `EV_PP`, `EV_DP` each world's): a P2 container whose
  entries, stored plain, are scripts of the game's script machine (see Missions).
  A line is a step's mode-0x40 operand: a table in the script's data of seven
  words, the offsets of the Japanese, English, French, German, Italian and Spanish
  texts (UTF-8, NUL-ended) and a closing -1; -1 for a language the card lacks
  (every Japanese one). Step 1.29 shows most of them, 2.64
  (`Ov002_ScriptOpenChoicePanel`) a choice's; two steps may share a table, and the
  languages may share a text ("..."). The archives hold 4,012 lines (`EV_DP`
  none). The studio writes a changed line past its script's data and points that
  language's word at it, so the other languages keep theirs.

Strings keep the game's control codes; in them the DS's buttons are characters
of the game's font (U+3349 its A, U+3314 its B, U+3322 its X, U+3336 U+3351
U+3357 its START).

## Missions

A mission file `mi/mi/<number>` is a P2 container whose entries are script
archives (CAKP), small tables, or empty.

- **A script archive** is laid out as KAPH: section 0's member 0 names the
  scripts (u16 count, u16 offsets, NUL-terminated names), section 1 holds them
  (`Archive_FindNameIndex`, `Archive_GetMember`, `Script_Start`).
- **A script** is a word, the size of its code in bytes (the word included),
  then its steps, then its data: the rest of its block (the steps run from
  block + 4, the data is at block + size). A step: byte 0 the command group,
  byte 1 the command, a u16 whose bits 0-10 are the step's length in words
  (these four bytes included) and bits 11-15 a slot field
  (`Game_RunActionScript`), then the step's words.
- **Operands**: most of a step's words are 8-byte slots, `s16 mode, s16 sel,
  s32 value` (`ScriptVm_ResolveOperand`): mode 1 an integer, 0x10 a 20.12
  number, 4 a field of the game's state, 8 a variable of the running context,
  the 0x80 bit an entry of the context's table, 2 a place in the script's data
  (data + value), 0x40 a table there of a text per language, 0 nothing (its
  value kept as written). Three commands of group 0 read their words as they
  are.
- **The commands**: the interpreter finds a step's handler at
  `data_020425ec[group] + command * 8`, a `{handler, continuation}` pair of the
  table that group holds while the field runs (`studio/tools/command_names.py`
  reads the tables from the decompilation). A name says which function runs;
  what each handler does with its words is read one command at a time, never
  guessed.
- **The enemy table** (`em0`, `em0m`; `Ov002_LoadObjectRecordsAndDrops`): u8
  row count, u8 group count, a pad, the rows' offset, each group's offset. A row
  (16 bytes): an enemy kind with its id (`bId`, the row of `eid.z`'s drop table),
  a 4-bit kind and 4-bit field, a parameter, its drop key and chance (-1: from
  `eid.z`). A group (`Ov002RecordEntry`): its count of parts, a state byte, two
  bytes the code does not read (the parts' limits summed, or -1, in all 2,850 of
  the game's), then parts `{row, limit, count, last player}`: a spawner made for
  the group adds `count` enemies of the row's class, stopping at `limit` unless
  it is 0xff. An enemy's id names it: row `id` of `data_ov107_020cb6a4` (six
  bytes, 0xff none) holds in its first column `b`, and `Ov107_LoadEnemyOverlay`
  starts overlay 113 + b + 1, which the decompilation names after its enemy.
- **The mission database** beside the mission files: `mdb.z` (LZ: a count, then
  60-byte records each starting with its number; 111 in the game's), `eid.z`
  (LZ: a count, then 24-byte records; 118), and `evi` and `trbox` (the code
  spells `trBox`; a count, that many offsets, then the records: 76 and 89).
  The field reads `trbox` and `evi` as a set of rows per mission: u16 the
  mission's number (the key `Ov002_FindHandlerByKey` matches), s8 its count, a
  byte, then rows of 0x14 bytes (s8, s8, s16, s16, s16, a point of 3 x 20.12).
  - `trbox`, the chests (`Ov002_ScriptSpawnTimedSet`): b0 the chest's slot (its
    state field 0x20c0 + 2 x slot), b1 its bucket, h2 its contents' key, h4 its
    flag (game state flag h4 + 9: opened, the chest left out), h6 its heading in
    degrees.
  - `evi`, the emblems (`Ov002_SpawnStateSpots`): b0 its bit in the save's field
    0x92b + 4 x the mission's index (set: found, left out), b1 its kind, h4 a
    value kept per emblem (+0x8d4c); h2 and h6 not read there.

## Mission worlds

A world's archive `mi/wd/wd_<code>` is a P2 container: entry 0 the room table,
entry 1 the gate models, then each room's collision and its KAPH models.

- **A room** (`Ov002_LoadWorldLinkResources`, `Ov002_LoadPeerIntoSlot`): its id
  (the one missions search; ids repeat within a world, so the studio names a
  room by its index), its link rows (8 bytes: key, target slot, pad, gate state,
  flags, radius), its objects (u32: flags, the archive member of a KAPH model it
  draws), the member of its collision.
- **A gate model** (`Ov002_BuildModelSlot`, five per gate): its kind (1, 2, 4
  start their own animation), the archive entry it loads (0: none built; 255 in
  the game's model 0s), a turn (65536ths), its position (three 20.12 words,
  `Render_SubmitNode`) and its scale (copied to the actor's +0xc0; equal on the
  three axes in the game's).
- **A room's collision** (`Coll_ResolveModelBlobPointers`): a 0xb0 header, the
  placement markers its `+0x76` counts (0x18 bytes each), a quadtree, three face
  lists -- grounds (0x88, read by ray casts), walls and ceilings (0x84, read by
  sphere casts) -- and named records (0x14) the faces point at.
- **A face**: 3 or 4 corners in 20.12, the corners' bounds on x and z, its flags
  (0x4000 skipped, 0x8000 the last of its node's run), its plane and four edge
  planes (stored computed; the game computes them again at load,
  `RoomPoly_BuildPlanes`), four slots and, for a ground, one more word not read.
  Slots 0-2 name records (0xff: none) by a convention the game's data keeps: 0
  gates, 1 and 2 surface attributes and walls that open (the camera's cast reads
  1 and 2, `Ov002_CollCastResolvePassThrough`); a ground's slot 3 is a small
  number (0-14, often past the room's records), a ground kind not read.
- **The quadtree**: the root square is the header's centre and size; child c of
  a node sits at its centre -+ size / 4 (c = 0: -x -z, 1: +x -z, 2: -x +z,
  3: +x +z, as `Coll_WalkTreeForHit` steps), half its size; no node of the
  smallest size has children. Each face is in one node's run, inside that
  node's square; a run's faces are consecutive in their list, the last flagged
  0x8000; a node's flags hold per list (bits 0-3 grounds, 4-7 walls, 8-11
  ceilings) bit c when child c's subtree holds faces of that list, bits 12-15
  zero (set at run time).

What is not read is kept byte for byte and written back as it was.

## Animations (BCA0)

A BCA0 (NSBCA), as `assets/src/animation.cpp` reads it (after apicula): a JNT0
section whose Nitro dictionary names the animations and gives each one's J0AC
block; a J0AC holds the frame count, the node count, a word kept as it is, the
pivot and basis rotation tables, and per node its flags, two bytes kept as they
are, and its translation, rotation and scale curves. A J0AC's tables and curve
data are not inside it: the game's BCA0s share them between their animations,
after the J0ACs, at offsets from each J0AC. The game loops or stops an animation
at the frame count its clip gives (for example `Ov259_ModelAnimTick`).

## KH2's PAX (the prize effect)

Not the game's: KINGDOM HEARTS II's prize effect, `obj/PRIZE.mdlx` in the player's 1.5+2.5 copy, as
`tools/mods/kh2_pax.py` reads it to make the KH2 prizes mod (docs/KH2_SEQUENCES.md, "Field prizes"). Only what that
needs is read.

- The file is a BAR; its entry of type 18 holds the PAX (`PAX_`). The PAX's data block is at +0x180: a word 0x96,
  two words (1, 0x60), the pictures' count and their offsets, the animations' count and their offsets, each offset
  from the block.
- A picture: a 32-byte header (u16 page TBP, palette CBP, buffer width, format 0x13 = 8 bits, x, y, width, height;
  then the GS TEX0 word), the width x height indices, then 256 RGBA palette entries stored in the PS2's CSM1 order
  (index bits 3 and 4 swapped), alpha 0x80 opaque. Pictures sit at (x, y) in 128 x 64 pages; the remaster's HD
  pages (`remastered/obj/PRIZE.mdlx/-0.dds`, `-1.dds`, 256 x 128) are those pages in the order of their TBPs.
- An animation: a 0x20-byte header (its number first: 0 munny, 1 the HP orb, 2 the MP orb, 3 the drive orb; its
  frame count, u16, at +0x14), then its frame list, 8 bytes a frame: u16 the frame's offset from the animation's
  +0x10, u16 how long it shows in KH2's frames (60 a second) times 256, a byte 0x80 on the last, 3 bytes 0. Across
  the 7957 animations of the player's KH2 every wait was whole frames (1, 2, 3, 4, 6, 40, 60); the prizes' are 2,
  a 30th of a second, one of Days's field frames.
- A frame: u16 kind (0 squares, 8 four corners), u16 layer count, u32 size, the GS TEX0 word at +0x10 (its page and
  palette), the layers at +0x20: a square (four s16: x0 y0 x1 y1) or four corners (eight s16), each with its part of
  the page as four u16 (u, v, w, h) in 1/4096 of the page; then, a layer each, 8 bytes: RGBA (0x80 = 1) and the GS
  blend byte (0x44 mixed by alpha, 0x48 added).

## KH2's objects (a chest, a prize box)

Not the game's: KINGDOM HEARTS II's objects (`obj/<name>.mdlx` in the player's 1.5+2.5 copy), as
`assets/src/kh2_map.cpp` reads them for the KH2 prizes mod's chests (`khdays-native --kh2-object`) and the studio's
KH2 rooms' pieces. An `.mdlx` is a BAR: its model (type 4), its textures (type 7, a texture container as a map's),
and a small entry of type 23. The model, 0x90 bytes after its entry's start: u32 kind (3 an object), u16 bones at
+0x10, the bones' table at +0x14 (0x40 bytes a bone: index, parent, then scale, rotation in radians and translation,
four floats each), u16 groups at +0x1c, the groups from +0x20 (0x20 bytes each: u32 flags, u32 texture, the DMA chain
at +0x10). A group's flag 0x400000 is light KH2 adds over the model (The World That Never Was's chest's glowing
lines). A chest's rest pose has its lid open: its motion set (`obj/<name>.mset`, a BAR of motions, each a BAR
holding the motion, type 9) has it closed in `00A`'s first frame; the lid is its last bone (`j3`), 100 degrees from
closed when open.

## Dream Drop Distance's rooms

Not the game's: KINGDOM HEARTS Dream Drop Distance's rooms, in the player's KINGDOM HEARTS HD 2.8 Final Chapter
Prologue, as `assets/src/ddd_map.cpp` reads them for the studio (docs/STUDIO.md, "Rooms from the collections"). Read
from the player's files; the header of a PMO as Birth by Sleep's public notes give it, the rest our own reading.

- **Where**: `map/<w>/<w>_<nn>/bin/` in kh3d_first or kh3d_third (`Image/dt`): `<w>_<nn>.pmp` the room, `.pvd`,
  `.mss`, `<w>_<nn>a.bcd` its floors and walls, `_<k>.mcv`, `.txa`. Its textures by name, `<name>.ctt` beside it;
  their HD pictures (`.dds`) the `.ctt`'s remastered asset in kh3d_third, the `.pmp`'s own in kh3d_first. Each
  world's `_60` to `_62` maps hold no `.bcd`: the stages the player falls through (Dive).
- **PMP**: "PMP\0"; its objects (u16 at +0x10), 0x30 bytes each from +0x20: position, rotation (radians, about x then
  y then z), scale (f32 x3 each), its PMO's offset (u32, 0 none); its textures (u16 at +0x1a, listed at +0x1c, 0x20
  bytes each, the name at +4).
- **PMO** (version 4): "PMO\0", its textures (u8 at +8) listed from +0xa0 as the PMP's, its section's offset at
  +0x10 (else at +0x1c, a model of see-through parts only; with +0x10 set, +0x1c points within the vertices).
- **A section**: two groups of records, pairs of u32 (the first group's, then the second's): the records' offset at
  +0 / +4, their vertex count at +8 / +0xc, their count at +0x10 / +0x14; then the vertices' offset at +0x18 and their
  bytes at +0x1c, the first group's vertices then the second's (offsets from the PMO). A record, 24 bytes: u16 vertex
  count, u8 texture (in the PMO's list), u8 vertex size (20), u32 flags (the primitive in the top four bits: 3
  triangles, 4 a strip stitched by degenerate triangles), u16 at +0xa its drawing (0x20 blended, 0x40 added, 0x60
  taken away), its colour at +0xc. A vertex: u16 u, v (0x800 is 1), its colour (R G B A), position f32 x3 (32768 is
  1). Read this way, every section of the rooms read holds its records' vertices exactly.
- **BCD** ("@bcd", version 0x0a): its parts (u32 at +8, their offset at +0xc), 0x30 bytes each: a box (min, max),
  u16 vertex and face counts at +0x18 / +0x1a, the vertices' and faces' offsets at +0x1c / +0x20 (from the file),
  then a tree the game's look-ups use (16-byte nodes: a split, its axis, two offsets) and its u16 face lists. A
  vertex: f32 x4 (w 1). A face, 48 bytes: its plane (normal, distance), its corners (i16 x4, the fourth -1 for a
  triangle), then what it is (u32 at +0x20 1 on floors; the low four bits of +0x24, 1 a floor, 2 a wall).
- **Units**: metres, as Days's: the stairs rise 0.17 to 0.25 (eh_01, nd_02); the floors and walls lie in the model's
  space (eh_01: 47,075 of its 48,852 vertices inside the BCD's box).

## KINGDOM HEARTS FINAL MIX's rooms

Not the game's: KINGDOM HEARTS FINAL MIX's rooms, in the player's KINGDOM HEARTS HD 1.5+2.5 ReMIX (kh1_first to
kh1_third), as `assets/src/kh1_map.cpp` reads them for the studio. No public documentation of them was found: all of it
our own reading of the player's files. The room's model is the remaster's own (its `.mvbl` asset), not the PS2's.

- **Where**: `<w>00_<nn>.bin` and `.img` (`<w>.wdt` the world's). The `.bin`: u32 entry count, then from +0x10 each
  entry's offset and size. In the rooms read its first entry is the floors and walls, its second the PS2's room model
  (the remaster's `.mvbl` asset in its place, named by its offset), its third and fourth skies (`.gvbl` assets).
- **The .img**: its palettes from +0x100, its texture pages from the u32 at +0x18 (their bytes at +0x1c), 64 KB each.
  Its HD pictures are named `<that + page x 0x10000 + n>` in hexadecimal (`-f900.dds`, `-1f903.dds`), n a texture's
  place among its page's, the pages counted as the remaster numbers them: the plain ones from the lowest address,
  sixteen at most (0x2600 to 0x3500), then those kept in the picture buffer's spare bits (PSMT8H at 0x0, 0x400...). A
  plain page past the sixteen (dc00_01's 0x3600) has the one HD picture without a number, `-_NoName.dds`. Checked on
  22 rooms of the 15 worlds.
- **The .mvbl**: u32 0x9696, its mesh table's length and its textures' count; the textures from +0x10, 16 bytes each
  (the PS2's TEX0 and CLAMP words: the page in TEX0's bits 0-13, its kind in bits 20-25; CLAMP 3 repeats); each mesh's
  box (0x80 bytes); the marks 0x11111111 to 0x44444444; the mesh table (16 bytes a mesh: u32 0x30000000, its offset).
  A mesh: u32 0x01234567, u32 1, the vertex count at +0x14, its texture's 16 bytes at +0x20, then 32-byte vertices:
  position f32 x3, colour (0x80 is 1), u, v f32 over its own texture, u32 0x1000 where a strip starts, else a
  triangle ends there (2 on the strip's odd ones).
- **The .gvbl** (a sky): u32 0x4989, `DDDD` at +0x10, its group count at +0x14, the groups from +0x20, each after the
  one before (the last ends the file in the 44 skies of the 22 rooms read): u32 0x25c, u32 its vertex count, 8 bytes,
  u32 its picture's offset in the `.bin` (the remaster's `.dds` asset named by it), u32, u32 0 or 5 (not read), u32,
  then its vertices as an `.mvbl` mesh's. A dome or a ring of clouds about the room's middle (KH1's centimetres, 20 to
  1,300 metres across), a cap, a moon; their vertices' alpha fades the clouds at the horizon. Drawn around the camera,
  blended, in the file's order (the third entry's first).
- **Floors and walls** (the `.bin`'s first entry): u32 0x48, then eight sections' offsets and lengths, each after the
  last; the second its vertices (f32 x3), the fourth its faces, 20 bytes each: corners (i16 x4, the fourth -1 for a
  triangle), u32 its normal's place in a table of its own, f32 its plane's distance, u32 what it is. Each face's plane
  agrees with its corners (95 to 100% of the faces of the 22 rooms; the others thin or flat).
- **Units and axes**: centimetres (stairs rise 20), y down: turned upright as (x, -y, -z).

## Re:Chain of Memories' rooms

Not the game's: KINGDOM HEARTS Re:Chain of Memories' rooms, in the player's KINGDOM HEARTS HD 1.5+2.5 ReMIX (the
`Recom` package), as `assets/src/recom_map.cpp` reads them for the studio. No public documentation of them was found:
all of it our own reading of the player's files.

- **Where**: `MAP<mm>/<nnnn>/MP<mm><nnnn>.AZF` (the room), `.VTM` and `.RTM` (its pictures; the remaster's HD ones are
  the `.RTM`'s, `<name>_tm2.dds`), `.mcd` (its floors and walls, below), `.ALF`, `.AFF`. 627
  rooms in 18 groups (MAP00 to MAP30).
- **.VTM, .RTM**: 32-byte entries, as many as the first one's offset says: u32 the picture's offset, its name
  (`gai_a_00.tm2`, 24 bytes), u32; each picture a TIM2.
- **.AZF**: an offset table (its first offset its length; 0 none). Entry 0 the layout: u16 its places' count, then from
  +0x20 a place each 64 bytes: u32 the piece (an entry of the table), s16 its parent (-1 none), u16, s16 x3 its turn
  in tenths of a degree, u16, f32 x4 its move, f32 x4 its scale. The other entries pieces: u32 x8 (the fourth its
  textures' count, the fifth their names' offset, the sixth its drawing's), a ROOT bone (its matrix at +0x50), the
  textures' names (32 bytes each), then the PS2's DMA and VIF stream. Each UNPACK V4-32 holds a header qword (u16 at
  +4 its vertex count), the GIF tag, then 64 bytes a vertex: u32 what it ends (0xffff nothing: a strip starts; 0x20
  an odd triangle; 0 an even one), position f32 x3 at +0x10, colour u32 x4 at +0x20 (0x80 is 1), s, t, q f32 at
  +0x30 and its texture (u32, among the piece's) at +0x3c.
- **.mcd** ("@MCD", version 3): u16 its pieces' count at +8, then a grid of the room (an origin and a cell size, f32 x3
  each); the pieces 0x10 past the u32 at +0x4c, 0xc0 bytes each: u32 0, u32 1, u32 its number, u32 its mesh's offset
  (from the end of the pieces), its move, turn and scale (f32 x4 each), its matrix (f32 x16, by columns: its move in
  the fourth) and that matrix's inverse. A mesh: a grid of its own (0x20 bytes), its sections' offsets (u32 x8, from
  the mesh, the first 0x40): its cells (u16 first face, count), its planes (f32 x4: normal, distance; a floor's normal
  -y), its vertices (f32 x4), two empty in the rooms read, its faces, 24 bytes each -- u16 0, u16 what it is (0x80),
  u16 its plane, u16 x4 its edges', u16 x4 its corners (one repeated: a triangle), u16 --, and a seventh. Every face's
  corners lie on its plane (1,369 faces of three rooms); 98% of the floors lie on the model's.
- **Units and axes**: metres (stairs rise 0.25 and 0.27), y down: turned upright as (x, -y, -z).
