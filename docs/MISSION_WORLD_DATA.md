# Mission and world data (`mi/`) — where a playable stage comes from

Phase 5 (a playable vertical slice) needs one thing the port did not have: the
data that defines a *stage* — its rooms, its geometry, and the surfaces the
player can walk into. This document records what has been **read out of the ROM
and the shipped data**, and marks precisely what is still unknown.

Nothing here is inferred from how other games do it. Every claim below cites
either a ROM address (read through the read-only Ghidra project on `days.nds`)
or a byte offset in the extracted data. Anything not established that way is in
the **Unknown** section, and stays there until it is measured.

## The loader path (ov002)

The gameplay overlay builds its data paths from these format strings:

| Address | String | Role |
|---|---|---|
| `arm9_ov002::0207f0f4` | `mi/wd/wd_%s` | the **world** archive; `%s` is a 2-char world code |
| `arm9_ov002::0207efb0` | `mi/mi/%04d` | a **mission** file, by number |
| `arm9_ov002::0207f000` | `mi/mi/mdb.z` | mission database |
| `arm9_ov002::0207f128` | `mi/mi/eid.z` | *(role not established)* |
| `arm9_ov002::0207efe8` | `mi/mi/trBox` | shipped as `trbox`; also read by ov008 and ov025 *(role not established)* |
| `arm9_ov002::0207eff4` | `mi/mi/evi` | *(role not established)* |

`FUN_arm9_ov002__02071990` is the world loader: it formats `mi/wd/wd_%s`,
opens the file with `Msg_OpenContainerAndReadHeader` (the same "P2" container
reader the message data uses — see [MESSAGE_DATA_P2.md](MESSAGE_DATA_P2.md)),
then `Archive_LoadFile`s **sub-file 0** and relocates it in place.

### World id → code table

`arm9_ov002::0207f0a4` is a 12-entry `char *` table indexed by world id. Read
from the ROM:

| id | code | shipped as `mi/wd/wd_<code>` |
|---:|---|---|
| 0 | `tt` | yes |
| 1 | `aw` | yes |
| 2 | `he` | yes |
| 3 | `al` | yes |
| 4 | `eh` | yes |
| 5 | `nm` | yes |
| 6 | `hb` | **no file** |
| 7 | `zz` | yes |
| 8 | `pp` | yes |
| 9 | `bb` | yes |
| 10 | `tw` | yes — **Traverse Town** |
| 11 | `pi` | **no file** |

Ten of the twelve ids have an archive; `hb` and `pi` have none in the shipped
filesystem. Which real world each code names is **not** established here — see
Unknown.

## World archive layout

`mi/wd/wd_<code>` is a plain P2 container (magic `P2`, sub-file count in the low
9 bits of the u16 at `0x02`, data base at `0x0c`, sector table then descriptor
table at `0x10`; bit 31 of a descriptor = LZ11-compressed). Sizes range from
39 KB (`wd_tw`, 7 sub-files) to 1.48 MB (`wd_tt`, 53 sub-files).

Sub-file roles, as observed across all ten archives:

- **sub-file 0** — the room table (below).
- **sub-file 1** — a **keyed record table**, not padding (see below). An earlier
  version of this document called it "4 bytes, all zero, in every archive";
  that was true only of the two smallest worlds, which have an empty table.
- **the rest** — alternating **room-data blobs** (untyped) and **`KAPH` models**
  (the room geometry; the port already decodes this model family).

### Sub-file 0 — the room table

The loader's relocation loop (`FUN_arm9_ov002__02071990`) defines the layout
exactly; it is reproduced by the shipped bytes.

```
u8   room_count
u8   [3]                      // unknown
u32  room_offset[room_count]   // relative to the table base; relocated to pointers
```

Then each room entry:

```
+0x00  u8    unknown
+0x01  u8    unknown
+0x02  s8    sub_count         // loop bound in the relocation
+0x03  u8    data sub-file index   // the room's data blob -- see below
+0x04  u32   unknown  x6       // through +0x18
+0x1c  u32   offset            // relocated: entry_base + value
+0x20  u32   offset[sub_count] // relocated: entry_base + value
```

Verified against the data — `wd_pp` sub-file 0 (360 B) declares
`room_count = 7` with offsets `0x20, 0x58, 0x88, 0xb0, 0xd8, 0x108, 0x140`,
and each of those offsets does land on a well-formed entry; `wd_tw` declares 1
room at `0x08` and `wd_zz` 2 rooms at `0x0c` and `0x34`.

### `+0x03` binds a room to its data blob — and that blob *is* the collision world

`FUN_arm9_ov002__02071ba4(viewSlot, roomIndex)` indexes the room table with
`roomIndex`, reads **byte `+0x03`** of the entry, and folds it into an archive
handle:

```
handle = ((container + 0x8000) & 0x00fffffc) << 7 | 0x80000000 | entry[0x03]
```

i.e. the low bits of the handle are the **sub-file index**. It hands that to
`FUN_0202b820(viewSlot, handle, …)`, which `Archive_LoadFile`s the sub-file and
installs it with `FUN_0202aff4` into **`ctx->aViewSlots + viewSlot * 8`** — the
very array the ground ray casts against and the `gate*` / `col_wall*` name
lookups walk (see [GAMEPLAY_RUNTIME.md](GAMEPLAY_RUNTIME.md)).

So the chain is closed end to end:

```
room index -> table entry -> entry[0x03] -> data sub-file
           -> aViewSlots[viewSlot] -> the collision world the ground ray uses
           -> whose named records are col_wall* / gate* and the surface codes
```

**Verified exhaustively**: across all ten world archives, every room's `+0x03`
lands on an untyped data sub-file — roughly 130 rooms, zero exceptions,
including `wd_tt` whose layout starts with a `KAPH` at index 2 and whose first
room correctly points at 3.

### Sub-file 1 — a keyed record table

The world loader also loads **sub-file 1** — the handle it builds is
`0x80000001 | (container << 7)` — and parks the result in a global that
`ov002_FindEntryAddrByKey` searches:

```
u32 count
count * { u8 key; ... }        // stride 0x90
```

`ov002_FindEntryAddrByKey(key)` walks the records comparing the **first byte** of
each and returns the record. Verified against the shipped bytes: `4 + count *
0x90` equals the sub-file length **exactly** in every archive checked —
`wd_tt` count 26 → 3748 bytes, `wd_al` count 29 → 4180, and `wd_tw` / `wd_zz`
count 0 → 4 bytes, which is why the small worlds looked like padding.

`FUN_arm9_ov002__020715c4` is the consumer: it looks a record up by key, reads
what are shaped like **two `Vec3` triples** plus a `u16` of flags out of it, and
from there drives the `gate%02d` and `col_wall%02d` toggling and creates
emitters. What the table *is* — placed objects, spawn points, something else —
is **not** established, and the record counts do not match the room counts
(`wd_tt` has 17 rooms against 26 records, `wd_al` 18 against 29), so it is not
per-room.

### A note on sub-file sizes

`khdays::assets::extract_p2_subfile` deliberately spans **start sector to next
start sector** rather than trusting the descriptor's size field, because that
field is encoded differently in the localized containers. Compressed sub-files
are therefore exact (the LZ header carries the true length) while **uncompressed
ones come back padded to the 0x200 sector** — which is why `--world-info` reports
`wd_tw` sub-file 1 as 512 bytes where the directory declares 4. Read the
descriptor directly if an exact uncompressed length matters.

## Collision and triggers live in the room-data blobs

This is the piece that was missing. The untyped room-data sub-files carry
**named entities** in plain ASCII:

- `col_wall`, `col_wall00`…`col_wall99`, `col_walla`…`col_walle`,
  `col_wallbox`, `col_wallin`, `col_wallbl`, `col_hole`, `col_btl`
- `gate`, `gate00`…`gate56`
- the tags `nohit`, `nocam`, `slide`, `sicon`, `nocatch`

The same names exist as literals in the code, which is what ties them to
behaviour rather than to art:

| Address | String |
|---|---|
| `arm9_ov002::0207f0d4` | `%s%02d` — the name builder (`col_wall` + `05` → `col_wall05`) |
| `arm9_ov002::0207f0dc` | `col_wall` |
| `arm9_ov002::0207f0e8` | `gate` |
| `arm9_ov002::0207f07c`… | `nohit`, `nocam`, `slide`, `sicon`, `nocatch` |
| `arm9_ov013::0207fec0` | `col_btl` |
| `arm9_ov020::020800d8`… | `col_wall`, `col_wall09`, `col_wall10` |

`col_wall` is referenced from `FUN_arm9_ov002__02071ba4` and
`FUN_arm9_ov002__020715c4`.

Every world contributes: `wd_tt` sub-file 37 alone names
`col_walla`…`col_walle` plus `gate21`…`gate24`, `nocatch` and `nohit`.

So a stage's walkable/blocking volumes are **named objects carried by the room
data**, resolved by name at load time — not a separate collision file, which is
why searching the filesystem for one never found anything.

**The other half of this is now read**: which code resolves those names, and that
`gate*` and `col_wall*` go to two *different* subsystems — see
[GAMEPLAY_RUNTIME.md](GAMEPLAY_RUNTIME.md), together with the ground-contact ray
and the camera-distance logic.

## Mission files

`mi/mi/%04d` names 114 shipped 4-digit files, plus `10000` and `10001` (which the
`%04d` format cannot produce), and the non-numeric `mdb.z`, `eid.z`, `evi` and
`trbox`. They are P2 containers too, but with the `0x8000` "wide" bit set in the
count word, and their sub-files are `CAKP` packs rather than `KAPH`. `wd_*` uses
`KAPH`; `mi/mi/*` uses `CAKP`. The relationship between the two tags is **not**
established.

The 4-digit names fall into leading-pair groups — `01xx`, `02xx`, `03xx`, `04xx`,
`06xx`, `09xx`, `10xx`, `13xx`, `20xx`, `30xx`, `80xx`, `90xx` — and within a
group the numbers split around 50 (`0101`…`0114`, then `0151`…`0161`). What the
leading pair and the split select is **not** established.

## The room-data blob decoded — it is a collision model

A room's data sub-file is not a container: it **is** the collision model struct,
stored with every pointer as a file-relative offset. `FUN_0202aff4` checks the
first word against `"KAPH"`; a room blob does not match, so it takes the single
-object branch and hands the blob straight to `func_02028bb4`, which relocates
it in place.

```
+0x00  u32 runtime[4]        zeroed on disk, filled at load
+0x10  u8  [0x64]
+0x74  u16 flags             bit 0x8000 = already relocated, bit 0x4000 set on load
+0x76  u16
+0x78  u16 pointerCount
+0x7a  u16 nodeCount         quadtree nodes, 0x20 bytes each
+0x7c  u16 face88Count
+0x7e  u16 face84CountA
+0x80  u16 face84CountB
+0x82  u16 namedRecordCount
+0x84  u8  [0x10]
+0x94  off field94
+0x98  off pointerTable       pointerCount further offsets, all relocated
+0x9c  off treeRoot
+0xa0  off faces88            face88Count records of 0x88
+0xa4  off faces84A           face84CountA records of 0x84
+0xa8  off faces84B           face84CountB records of 0x84
+0xac  off namedRecords       namedRecordCount records of 0x14
```

Field names follow the decomp's own `CollisionModelBlob`
(`external/khdays-decomp/src/calls/func_02028bb4.c`).

**Verified against `wd_tw` sub-file 2, and every section boundary lands on the
next one:**

```
treeRoot     0x0b0 + 2 x 0x20 = 0x0f0 = faces88
faces88      0x0f0 + 1 x 0x88 = 0x178 = faces84A
faces84A     0x178 + 5 x 0x84 = 0x40c = faces84B = namedRecords
namedRecords 0x40c + 1 x 0x14 = 0x420 = the file length
```

### The walkable surface — `CollisionFace88`

```
+0x00  s32 bounds[4]         minX, minZ, maxX, maxZ (20.12)
+0x10  u16 flags             bit 0x4000 = skip, bit 0x8000 = last of the run
+0x12  u16 vertexCount       up to 4
+0x14  {s16 x,y,z; s16 pad; s32 distance}   the face plane
+0x20  {s16 x; s16 pad; s16 z; s16 pad; s32 distance} edges[4]   inside test
+0x50  VecFx32 vertices[4]   20.12
+0x80  u32 [2]
```

So a collision face is a **convex polygon of up to four corners with its own
plane and four edge planes** — plane for the hit point, edge planes for the
inside test, bounds for the broad phase. The `0x84` variant is the same idea
without the plane's `y`.

`wd_tw` sub-file 2's single `face88` decodes as the district's floor: plane
normal `(0, 4096, 0)` — straight up in 20.12 — four corners at `y = 0.075`
spanning `x = -15.29 .. 20.67` and `z = -17.49 .. 15.01`, and `flags = 0x8000`
because it is the last of its run.

### The tree

`nodeCount` nodes of `0x20` bytes at `treeRoot`. Node `+0x0c` and `+0x10..+0x1c`
hold **child indices**, which `func_02028bb4` multiplies by `0x20` and rebases;
`-1` becomes null. Four children per node is the quadtree
`FUN_01ffdb54` walks, choosing a quadrant on two axes and quartering the extent
per level.

## Unknown — do not fill these in without measuring

1. **How the `KAPH` models bind to a room.** `+0x03` binds the *data*; the
   geometry does not follow from it by any simple rule. A room's data blob is
   usually followed by `KAPH` sub-files — `wd_tw` room 0 has its data at 2 and
   its geometry at 3 and 4 (`tw_03_1` + `tw_03_2`), with 5 and 6 being props
   shared with other archives — but that breaks down: `wd_bb` room 1 declares
   `sub_count = 8` with only one `KAPH` before the next room's data, and
   `wd_al` rooms 15..17 point at data blobs with no adjacent `KAPH` at all. The
   `sub_count` entries at `+0x20` (values `0x380`, `0x480`, …) are the obvious
   candidate and are still undecoded.
2. What a **named record's** `+0x10` payload points at, and how a name binds to
   a particular face or region. The blob's own layout is decoded (above); this
   last link is not.
3. **The six u32s at room entry `+0x04`..`+0x18`.** Their magnitudes
   (`0xfffffe96`, `0x2c000`, `0x7000`, `0x23000`, …) are consistent with
   fixed-point coordinates or extents, but nothing has confirmed that.
4. **The values behind the relocated room pointers** — `0x380`, `0x480`,
   `0x580`, `0x1180`, … Low 7 bits are always zero. They are not sub-file indices
   of the same container (`wd_tw` has 7 sub-files but stores `0x380`/`0x480`).
5. **World code → world name**, for nine of the ten. `tw` is settled:
   `wd_tw`'s only room renders as **Traverse Town, District 3** — recognised on
   sight from the rendered geometry, which also explains the model naming
   `tw_03_1` = world `tw`, area `03`, part `1`. The same method settles the rest
   one archive at a time; `mdb.z` would settle them from data.
6. **Whether each `col_*` prefix is walk collision, camera collision, or battle
   bounds.** `col_btl` living in ov013 rather than ov002 is a hint, not proof.
7. `mi/mi/eid.z` and `mi/mi/evi` roles; the `CAKP` container format.

## Reading one with the port

```
khdays-port --world-info    mi/wd/wd_tw
khdays-port --extract-world mi/wd/wd_tw OUTDIR
khdays-port --render-model  OUTDIR/sub3/slot_7/0000.nsbmd
```

`--extract-world` writes each `KAPH` in the `slot_N/0000.ext` layout
`tools/unpack_containers` produces, which is the layout `--render-model` already
reads — so a room renders with no new rendering code, and its sibling animation
in `slot_0` is picked up automatically. `wd_tw` yields `tw_03_1` (1402 vertices),
`tw_03_2`, and the shared props `gate_lock2` and `lightwall`.

Both commands go through `khdays::assets::parse_slot_container`, which handles
the `KAPH` and `D2KP` magics with one implementation — they share a layout:
eight slot pointers at `+0x08`, each addressing
`{u32 count; u32 offsets[count]; u32 sizes[count]}`. **Slot 7 is the NSBMD and
slot 0 the NSBCA**, the same convention `ba/ch/**` uses.

One observation, unexplained: `--world-info` reports sub-file 1 as 512 bytes,
while its directory entry declares 4. The KAPH sub-files are LZ-compressed and
come out at their exact declared sizes, so this does not affect them, but the
uncompressed-entry path is worth a look.

## Next steps this unblocks

- Decode the room-data blob far enough to extract one room's collision volumes.
- Render one room: room table → `KAPH` sub-file → the existing model renderer.
- With geometry plus collision, Phase 5's vertical slice (a room, the player
  model, movement, a camera) stops being blocked on unknown data.
