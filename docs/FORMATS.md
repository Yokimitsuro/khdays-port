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

## Room maps

A room's map on the lower screen (`Ov002_QueuePanelGraphics`, `Ov002_ShowSlotSummary`, `Ov002_SwapOwnerBlock`,
`Ov002_ScreenToCell`), read from the decompilation and the game's files. Two P2 archives hold them:
`UI/btl/<language>/map.p2` (a world's rooms' chain, names in that language) and `UI/btl/mapchr.p2` (the pictures).

- **A world's chain**: `map.p2`'s member by world (`data_ov002_0207ee08`: tt to nm 0-5, pp 6, bb 7; hb, zz, tw and pi
  none), looked up by the field's room id (`Ov002_FindChainEntryById`; the room table's id byte: tt's thirteen entries
  are thirteen of its seventeen rooms' ids). An entry: u32 its length (-1 ends the chain), u16 where its name starts
  (0x18), u16 the room's id, u16 x2 the pixel of the picture the room's x = z = 0 falls on, u16 x2 the part of the
  picture the map holds in 8-pixel tiles (15 x 10, the panel's 120 x 80 pixels, for most), u32 the game units a pixel
  spans (20.12: 0x666 = 0.4 and 0xccd = 0.8 in tt), u32 its picture's member of `mapchr.p2`, its name (UTF-16, ended
  by 0), the entry padded to 4 bytes. A position on the map: x / that + the origin's pixel, z the same down.
- **A picture**: an NCGR, 24 x 20 tiles of 8 x 8 pixels at 4 bits (192 x 160), its pixels the map's colours: 15 none
  (see-through), 14 a floor, 13 a floor of another level, 10 an edge (black), 11 and 12 greys (12 a stair's lines),
  1 to 4 marks at the room's exits. They show in `UI/btl/bm_lo_bg000.NCLR.z`'s sub-palette 13 (white floors with black
  edges, as on the game's lower screen); `mapchr.p2`'s other members are the panel's own pictures (D2KP packs).
- A room a mod adds has one when its folder holds `map.png` (the colours above, 192 x 160) and `map.txt` (`origin x y`,
  `size w h` in tiles, `cell` the game units a pixel spans, `name` and `name.<language>`): the studio draws both from
  the room's floors; the port puts its picture after `mapchr.p2`'s and its entry in its world's chain in each
  language's `map.p2` (docs/RUNTIME_MAP.md, "A world's rooms").

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

## KH2's effects

Not the game's: KINGDOM HEARTS II's effects (PAX), as `assets/src/kh2_effect.cpp` reads and plays them for the
Dark Corridor (docs/RUNTIME_MAP.md, "The Dark Corridors") -- our own reading of the files and of the player's
`KINGDOM HEARTS II FINAL MIX.exe`, whose loader and effect interpreter it follows. The prize effect above is the
same layout's first effect, read only as far as its pictures and shapes.

- **The file**: `PAX_`, at +0xC where its element table ends: a directory `{0x82, 0, 0, n}`, `n` entries of 32 bytes
  (the elements placing an effect in a scene), then a list -- a count and offsets from the directory -- of effects.
  An effect is a block `{0x96}` and five lists, each a count and offsets from the block: emitters, pictures, shapes,
  models and vertex lists (`{u32 id, u32 model, u32 count, u16 vertex numbers}`). `effect/texcommon.dpd` is a bare
  block of pictures alone: those KH2 keeps resident, which the effects' shapes may cut.
- **An emitter**: its offsets count from its +0x110: there its length (frames x 4096), at +0x114 a word whose bit 0
  makes it repeat until told to stop, at +0x11c the list of its layers. A layer: +0x10 when it starts, +0x14 its life,
  +0x18/+0x1c a loop's start and end (0x70000000: never; a layer that never starts is born by another), +0x20 where
  its commands' cursors sit in a running layer, +0x24 its drawing order, +0x26 the command count, the commands from
  +0x28: 16 bytes `{u32 code, u16 record size, u16 record count, u32 records, u32 slots}`. A command's records each
  start with their frame (x 4096), the last followed by `0xfffff000`; its slots list where in a running layer's
  memory its values are kept.
- **The commands**: a code indexes the executable's table of 0x50-byte descriptors (at 0x140779a00), the first word
  KH2's name for it, the others its functions: word 1 a step, 3 a draw (run every tick, drawn or not: some keep
  their state there), 5 a hook on the instance, 7 the reset on a birth, 8 on a loop, 9 the release. The 130 codes
  KH2's 3166 PAX use are in the table below ("KH2's effect commands"); a command the reader does not know is
  refused with its code, as a primitive kind it does not read. The names' middle letters are their authors'
  (`Ke`, `Ryj`, `Sno`, `Ei`, `Vt`).
- **The run**, at KH2's 60 ticks a second: a layer is born when its start comes; each tick every running layer's
  commands without a draw step, then those with one step and draw, in the layers' order; a layer whose loop's end
  comes goes back to its loop's start (the frames between run without their spawners); one whose life is over goes.
  A repeating emitter runs until told to stop, then its layers stop looping and it ends when none is left -- KH2's
  own opening, hold and closing.
- **A model**: its header at +0x10: u8 flags (bit 0 clear: depth written), offsets of its primitives, vertices and
  normals, the counts, at +0x16 its GS ALPHA byte and at +0x17 its FIX. Vertices are three s16; primitives come in
  packets `{u8, u8 kind, s16 count, 4 bytes, TEX0}`, kinds 0-2 triangles and 4-7 quads (a quad a strip: 0-1-2,
  1-3-2): RGBA per corner first, then the vertex numbers (+0xc a triangle's, +0x10 a quad's); 2, 6 and 7 are
  textured, u16 UV pairs in 1/4096 of the page (+0x14 a triangle's, +0x18 a quad's); 1, 5 and 7 add a normal's
  number per corner (+0x12, +0x18, +0x28: the scene's lighting, which the port does not draw). Their sizes are
  0x14, 0x1c, 0x20, -, 0x18, 0x20, 0x28 and 0x30; kind 3 (0x28) is in none of KH2's files. A kind 0xff ends them. A textured corner's colour multiplies the picture (0x80 leaves it as it is); an untextured
  one's is the colour drawn (0x80 half of full). Alpha 0x80 is a blend's 1 either way.
- **A shape**: frames `{wait in 1/256 of a frame, a loop flag, TEX0}` of layers, each four corners in 12.4 fixed
  point (x 1/16), its part of the page, its colour and its own GS ALPHA byte.
- **Drawing**: the GS blend `(A - B) x C / 128 + D` by the ALPHA byte: 0x44 mixed by alpha, 0x48 added, 0x42
  `Cd - Cs x As` (dark smoke), 0x46 `Cd x (1 - As)`, 0x88 `Cs x As`. Drawn nearer only (`TEST 0x7000d`); a
  `pppDrawMdl3` whose record has +0xd (and not +0xe) set writes the depth alone, no colour (`TEST 0x72001`: the
  corridor's mask), +0xe drawn without the depth test. A model's primitives far to near, the layers in their order.
- **Pictures and pages**: a picture is `{u16 tbp, cbp, tbw, psm, x, y, w, h}`, its indices and palette as the prize
  effect's; a draw names a page by its TEX0 (tbp, width, height, palette): its view of the pictures placed on the
  128 x 64 pages from that tbp. The remaster's HD pictures are `remastered/<file>/-<n>.dds`, one a page, in the order
  of the pictures' tbps.

### KH2's effect commands

Each command's slots are offsets in its object's memory (from the object's +0xa0); "a key" is a keyed record on
its frame. The handlers named are KINGDOM HEARTS II FINAL MIX's (the PC executable, our own reading).

| Codes | Commands | What they do |
| --- | --- | --- |
| 0x02-0x11 | `pppKeThRes`, `8`, `16`, `24`, `24x4`, `32`, `40`, `48`, `48x4` | At a birth: a lightning bolt's points (one or four sets of 3 to 48, 48 bytes each) |
| 0x1c-0x1f, 0x24-0x27 | `pppAccele`, `AngAccele`, `SclAccele`, `ColAccele`, `Move`, `AngMove`, `SclMove`, `ColMove` | A value and its rate: a key adds to the rate, the rate adds to the value each tick |
| 0x28-0x2b | `pppPoint`, `pppAngle`, `pppScale`, `pppColor` | A key adds its values (the colour's four shorts / 128 the bytes at +8) |
| 0xd2-0xd7 | `pppMoveLoop`, `AngMoveLoop`, `SclMoveLoop`, `PointLoop`, `AngleLoop`, `ScaleLoop` | The same for a looping layer (a loop's fast-forward leaves the rate out; the first key taken off again) |
| 0x33-0x35, 0x3f-0x44, 0x48-0x4d, 0x51, 0x52 | `pppRand...`, `pppSRand...` | A random draw times the key's base added to a target: both ways (base x draw - base), `Up` (0 to base), `Down` (-base to 0); floats drawn at the start and kept, ints (`IV`) and four-short colours (`HCV`) drawn on each key; `S` each component its own draw |
| 0x2d | `pppVertexAttend` | A vertex of a list through the parent's matrix (where a `pppVertexApAt` child stays) |
| 0x2f | `pppKeMvYpEff` | The angles that face its way (z about the way's x and y, x its rise) |
| 0x30 | `pppKeDrct` | A direction set on its key |
| 0x31 | `pppVtMime` | The object's vertices a blend of two models, t moving with its rate; drawn and spawned at in the model's place |
| 0x54, 0x55 | `pppSMatrix`, `pppKeMatSN` | A shared matrix; a turn by how much angles changed since the last tick |
| 0x57-0x5c, 0xd8, 0xe6-0xeb | `pppMatrixXYZ`, `XZY`, `YZX`, `YXZ`, `ZXY`, `ZYX`, `pppMatrixLoop`, `pppMatrixLoopXYZ`... | The object's matrix from place, angles and scale; the letters the order the turns apply (row vectors) |
| 0x5d, 0x5e, 0x60, 0x63 | `pppMatrixLoc`, `pppMatrixScl`, `pppRyjMatrixNoRot`, `pppRyjMatrixWorldXYZ` | No turn; the scale on the diagonal; the view's turn undone (facing the camera); kept where the world was at the birth |
| 0x69, 0x6a | `pppParMatrix`, `pppKeParMatR` | Placed through the parent's matrix; turned by its unit rows too |
| 0x70-0x75, 0xd9 | `pppDrawMatrix`, `Front`, `NoRot`, `Wood`, `pppKeDMat`, `pppKeDMatFr`, `pppDrawMatrixLoop` | The matrix drawn with: through world, view and projection; facing the view; no view turn; upright; kept in a slot |
| 0x78, 0x79, 0x7b | `pppRyjDrawMatrixWorld`, `Front`, `Wood` | The same at the world's place and size at the birth |
| 0x7c, 0x7d | `pppKeZCrct`, `pppKeZCrctShp` | Depth order corrections (KH2's sorting alone) |
| 0x7e, 0x80, 0x81 | `pppKeThTp`, `pppKeThSft`, `pppKeTh` | Lightning: a bolt grown from the object toward a target point, segment by segment with random kinks and branches, drawn as a glowing ribbon (FUN_1404be900, FUN_1404dd8e0, FUN_1404de5c0); read, not played yet |
| 0x83, 0x85, 0x86, 0x89, 0x8a, 0x8b, 0xda, 0xdb | `pppDrawMdl`, `MdlTs`, `Mdl2`, `Mdl3`, `MdlSemi3`, `MdlTs3`, `MdlLoop`, `MdlLoopZ` | A model through the object's matrix and colour; each its depth rules (8 written, 0x80 not, 4 two record bytes: the depth alone / always), the Ts and Loop ones their UV scroll, Semi3 and LoopZ the record's blend |
| 0x8d, 0x8e | `pppDrawShape`, `pppDrawShapeX` | A shape's frames through the matrix |
| 0x91, 0xa0, 0xee | `pppKeMdlDtt`, `pppKeShpDtt`, `pppSnoMdlDttNorm` | The screen behind drawn again through a model or a sprite, moved by a strength (along the normals for the last): given as draws of the screen, the movement not worked out; the port does not draw them yet |
| 0x99-0x9d | `pppKeShpTailX`, `Tail2`, `Tail2X`, `Tail3`, `Tail3X` | Sprites along the object's last places (a ring of 31 or 28), spaced by a length shrinking with their size, coloured head to tail; the 3 ones each sprite's frame, size and turn from a 16-bit LCG |
| 0xac, 0xae, 0xb1, 0xb2, 0xec | `pppPointAp`, `pppVertexAp`, `ApLc`, `ApAt`, `ApLcLoop` | Children born every wait: at its point; at a list's vertices through its matrix, as in the model, or tied to a vertex number |
| 0xb5, 0xb7, 0xb8 | `pppKeBornRnd3`, `5`, `6` | Children born at a list's vertices picked by `rand()`: with their unit way out (sized), or facing out along it |
| 0xbc-0xc1, 0xdd, 0xdf-0xe1 | `pppKeLnsLpSft`, `LnsLp`, `LnsArnd`, `LnsClm`, `LnsCrn`, `LnsFls` and the T ones | A lens flare: a line from the source to the screen's middle and how bright, elements around the source, as columns along the line, a streak, a flash; the T ones keep their key's values (the maps' suns). LpT's test of how much of the source is seen is not made |
| 0xc8 | `pppRyjDrawShipolyBone` | A weapon's swing trail from the game's two points (base, tip): a history bent into the model's quads by a natural cubic spline |
| 0xc9, 0xcb, 0xd0, 0xe5 | `pppRyjMegaBirth`, `Prize`, `Model`, `ModelFilter` | Particle pools: sprites, prizes (crowds here: the game's points), models; Prize and ModelFilter read, not played yet |
| 0xca | `pppRyjMngFlag` | The instance's bytes: its state for the game, frozen, its place following the camera |
| 0xed | `pppSnoScaleAll` | Three sizes and one for all, each with rates; their products |
| 0xef, 0xf0, 0xf2 | `pppRyjMegaBirthUserCtrl`, `2`, `3` | Sprites at the game's points (frame, turn, size and alpha in each point's bytes); 2 and 3 a list of the game's matrices (the gummi game, Xemnas), not played yet |
| 0xf1 | `pppEiDrawShipoly` | A ribbon along the object's last places (two edges a place, or one and edges across the screen): width, drift and a pull that move, a natural cubic spline, thinner and faded to the tail, pieces along and across with UV ranges and scroll |

The game gives some effects points (KH2's list at 0x140b60268): `EffectInstance::set_points`.

## KH2's sound banks and its scenes' sounds

Not the game's: KINGDOM HEARTS II's PC sound banks (`se/se<bank>.win32.scd`), as `tools/mods/kh2_scd.py` reads them
for the Dark Corridor's sounds (docs/RUNTIME_MAP.md, "The Dark Corridors") -- our own reading of the files, checked
sample for sample against ffmpeg's decoding of the same recordings.

- **The bank**: `SEDBSSCF`, a 0x30-byte header, then at 0x30 the tables' header: four u16 counts (the third the
  recordings'), then u32 offsets (the second the recordings' table's). The table: a u32 offset a recording, 0 an
  empty place.
- **A recording**: `{u32 data size, channels, rate, codec, loop start, loop end, extra size, -}`, the codec's extra
  header, then its data. Codec 12 is Microsoft ADPCM, its extra a WAVEFORMATEX (`wFormatTag` 2, the block's bytes,
  `wSamplesPerBlock` 32, the seven predictor pairs); codec 6 is Ogg Vorbis. Rates are the files' own (47,995-48,005
  in bank 2984).
- **Microsoft ADPCM**, as Microsoft's description of the format gives it: a block holds per channel a predictor
  number (u8), then per channel the step, sample 1 and sample 2 (s16 each); sample 2 then sample 1 are the first out;
  then a nibble a sample, high nibble first, the channels interleaved. A sample is `(s1 c1 + s2 c2) / 256` -- the
  division toward zero, as C's (ffmpeg's decoding agrees; rounding down is off by one at negative values) -- plus the
  nibble (signed) times the step, held to 16 bits; the step becomes `max(16, step x adapt[nibble] / 256)`.
- **A scene's sounds**: a KH2 event (an `.ard` area's entry of type 22) is records `{u16 size, u16 op}`, run by the
  executable's event runner (its records' dispatch at `FUN_1402cb5d0`, a jump table at 0x1402cbc2c by `op - 2`). Op
  0x2a names the scene's bank (an s16 at +6); op 0x24 loads a block of sub-records `{op, size}` (0x38 an object, 0x25
  an animation, 0x26 a voice, 0x39 an effect slot and its file's name); op 0x9 runs an effect: +4 its frame, +0xa
  its slot, +0xc the file's element, +0xe 4 bind, 0 start, 1 or 2 stop (`FUN_1402d9340` starts the element); op
  0x2b a sound: +4 its slot, +6 0 start or 1 stop, +8 a u32 sound -- below 1001 the scene's bank's, its recording
  `n - 1` -- and +0xc its frame (`FUN_1402d8be0` plays it). A PAX element's own sound (its +0x18, -1 none) plays
  with it; the Dark Corridor's names none.

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
  taken away), its colour at +0xc. A vertex: u16 u, v (0x800 is 1), its colour (R G B A: red, green and blue 0x80
  is 1 and brighter above it, as the PSP's -- many rooms never pass 0x80, the sunny ones do; the alpha's 1 is 0xff),
  position f32 x3 (32768 is 1). Read this way, every section of the rooms read holds its records' vertices exactly.
- **BCD** ("@bcd", version 0x0a): its parts (u32 at +8, their offset at +0xc), 0x30 bytes each: a box (min, max),
  u16 vertex and face counts at +0x18 / +0x1a, the vertices' and faces' offsets at +0x1c / +0x20 (from the file),
  then a tree the game's look-ups use (16-byte nodes: a split, its axis, two offsets) and its u16 face lists. A
  vertex: f32 x4 (w 1). A face, 48 bytes: its plane (normal, distance), its corners (i16 x4, the fourth -1 for a
  triangle), then what it is (u32 at +0x20 1 on floors; the low four bits of +0x24, 1 a floor, 2 a wall). Made into a
  room's collision, a floor's open edge -- one no other floor shares, a step of 0.3 m past it finding no floor at any
  height -- gets a wall from 1 m under it to 3 m above: where the game goes on into the next room by an exit of its
  own (not Days's), or the floor just ends, a character walked off and fell out of the room (eh_01 and Birth by
  Sleep's di01 walled so, a ledge over a lower floor left open).
- **TXA** (a room's textures shown frame by frame): "TXA\0", u16 its version (2), u16 its entries' count, the
  entries from +0x10, 0x38 bytes each: its name (16 bytes), the texture it shows on (16), 8 bytes, u16 the sheet's
  width and height and a frame's (the original's pixels: a texture is one frame, the sheet holds them all), u32 its
  animations' count (1 in all read) and the first's offset. An animation: its name (16 bytes), s16 where it plays
  again from, back from its end (minus its frame count, its start, in all read), u16 its frames' count, u32 their
  offset; a frame, 12 bytes: u16 x, y in the sheet, u16 its shortest and longest length (0: the shortest; taken as
  60ths of a second, a random one at its middle; 0 long, left out), 4 bytes (one set on a thunder's flash: its sound,
  not read). Then the sheets' own pixels. The remaster's HD sheets are the `.txa`'s assets, each named as its texture
  with one character more or less (`fa03_01a`, `di_05_thu1` for `di_05_thu1a`, `tw04_fir01_`), at their own scale
  (rg_08's twice the original, its texture four times). Read on the five rooms of the eight taken out that have one
  (di_05, fa_03, nd_03, rg_08, tm_04); a texture the room's model has not (tm_04's, fa_03's `g_` ones) is left out.
- **Units**: metres, as Days's: the stairs rise 0.17 to 0.25 (eh_01, nd_02); the floors and walls lie in the model's
  space (eh_01: 47,075 of its 48,852 vertices inside the BCD's box).
- **Skies**: an object with bit 0 of its byte at +0x2c set is drawn around the camera: a sphere of a metre or two
  about the origin (di_01's `lp_sky2`, nd_02's `sora01`, di_05's sky, sea and black; Birth by Sleep's the same:
  vs02's `lp_he01_11`, dp01's `sora02`). One whose textures are named a sky (`tm01_sky01`, `pi_sky_c`) or a far
  backdrop (`tm01_fore01`: "fore" and a digit) is one too: tm_01's dome of 200 metres and its backdrops, cylinders of
  120 and 135 about the room's middle, have no bit.

## Birth by Sleep's rooms

Not the game's: KINGDOM HEARTS Birth by Sleep FINAL MIX's rooms, in the player's KINGDOM HEARTS HD 1.5+2.5 ReMIX, as
`assets/src/ddd_map.cpp` reads them for the studio: its engine is Dream Drop Distance's (above), its models of an
older version. Read from the player's files.

- **Where**: `arc/map/<w><nn>.arc` in bbs_first to bbs_fourth (`Image/dt`; 202 rooms in 16 worlds, among them `jb`'s
  eight, a world never finished -- The Jungle Book's, few of its pictures). The HD pictures are the archive's own
  remastered assets, `<...>_arc<n>.dds` the PMP's n-th texture (di03: its 61 textures' shapes those of its 61
  pictures).
- **The archive** ("ARC\0"): u16 its version (1), u16 its members' count at +6; from +0x10 a member's 0x20 bytes:
  u32, u32 its offset, u32 its size, u32, its name (16 bytes). A room's members: `<w>_<nn>.pmp`, `.pvd`, `.mss`,
  `<w>_<nn>a.bcd`, and some a `.env`, a `.tm2` and an `.nmd`.
- **PMP**: as Dream Drop Distance's; its textures are TIM2 pictures within it.
- **PMO** (version 3, the byte at +6): its textures as version 4's; its scale (f32 at +0x18), its first pass at
  +0x10 and its second (drawn see-through) at +0x1c, each a run of meshes: a mesh's header -- u16 its vertex count (0
  ends the run), u8 its texture, u8 its vertex's size, u32 its vertex form (the PSP's own: texture coordinates in bits
  0-1, colour 2-4, normal 5-6, position 7-8, weights 9-10 and their count less one in 14-16; with bit 24 one colour
  for the whole mesh after the header; the primitive in the top four bits, 3 triangles, 4 strips), u8, u8 its strips'
  count, u16 its drawing (as version 4's) -- then that colour (R G B A as version 4's), each strip's length (u16),
  and from the next 4 bytes its vertices; the next mesh from the next 4 bytes after them. A vertex holds its parts in
  the PSP's order (weights, texture coordinates, colour, normal, position), each on a multiple of its own size and the
  vertex on its widest: 8-bit values 1 at 128, 16-bit at 32768 (positions signed), floats as they are, times the
  model's scale. Read this way the forms of di03's 212 meshes (8-bit or float coordinates, colours 8888 or the
  mesh's, float positions) each give the size their header says.
- **PMO's textures**: after a texture's name, two f32 at +0x10 / +0x14 of its 0x20 bytes, how far its texture
  coordinates move each frame (di01: the sea's foam `lplm06009a` 0 / 0.002, `lp_di1mt05` 0.001 / 0.001, the clouds
  `lp_sky2` 0.0002 / 0); the room's material scrolls by them each 60th of a second (not compared with the game's
  speed). Nothing else in a room moves: no member of its archive holds an animation, and none of di01's 96 objects
  has a mesh with weights.
- **BCD** (version 2): as version 0x0a, a part 0x40 bytes, its counts at +0x24 / +0x26 and its vertices' and faces'
  offsets at +0x28 / +0x2c (jb_20a: 2,328 vertices ending where its 2,703 faces of 48 bytes start). A face's u32 at
  +0x24 says what it is: bits 0-3 its kind (1 a floor, 2 a wall, whatever its plane), 4-7 its material (9 water), 9-13
  whom it stops -- 0x200 and 0x400 in every floor and wall of the 202 rooms that does stop the characters (0x3e00 and
  0x3600 the most common), a sea's surface 0x2000 alone (di01-03's at y -0.96, 30 faces each, over a sea floor down to
  -2.8 with no wall round it). Read so: a face with bits 9-13 but neither 0x200 nor 0x400 is left out, and under a
  water's surface (material 9, a floor, so left out) walls stand where the floors go deeper than 0.6 m below it, from
  1 m under that depth to 3 m above, along the line each floor triangle crosses it (within 5 m of the surface's
  faces), and the floors wholly deeper are left out: the game keeps its characters out of deep water and Days has no
  swimming (and the studio places a room by its lowest main floor: di01's sea floor would have been it). Faces with none of bits 9-13
  (14,046 floors marked 0x2xxxx) are kept, their use not read.
- **Units**: metres, as Dream Drop Distance's.

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
  one before (the last ends the file in the 44 skies of the 22 rooms read): u32 its kind (0x25c; 0x21c in Deep
  Jungle's tz00_08, laid out the same), u32 its vertex count, 8 bytes,
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
