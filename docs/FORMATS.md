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
