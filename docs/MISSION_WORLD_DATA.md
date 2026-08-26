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
| 10 | `tw` | yes |
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
- **sub-file 1** — 4 bytes, all zero, in every archive.
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
+0x03  u8    unknown
+0x04  u32   unknown  x6       // through +0x18
+0x1c  u32   offset            // relocated: entry_base + value
+0x20  u32   offset[sub_count] // relocated: entry_base + value
```

Verified against the data — `wd_pp` sub-file 0 (360 B) declares
`room_count = 7` with offsets `0x20, 0x58, 0x88, 0xb0, 0xd8, 0x108, 0x140`,
and each of those offsets does land on a well-formed entry; `wd_tw` declares 1
room at `0x08` and `wd_zz` 2 rooms at `0x0c` and `0x34`.

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

## Unknown — do not fill these in without measuring

1. **The room-data blob's record layout.** All that is measured is: a 0x70-byte
   zeroed prologue, then what looks like a section table (`wd_tw` sub-file 2 has
   ascending u32s `0xb0, 0xb0, 0xb0, 0xf0, 0x178, 0x40c, 0x40c` against a
   0x420-byte file), then data containing the names above. The record that binds
   a name to geometry, to a surface tag, and to a volume has **not** been
   decoded.
2. **The six u32s at room entry `+0x04`..`+0x18`.** Their magnitudes
   (`0xfffffe96`, `0x2c000`, `0x7000`, `0x23000`, …) are consistent with
   fixed-point coordinates or extents, but nothing has confirmed that.
3. **The values behind the relocated room pointers** — `0x380`, `0x480`,
   `0x580`, `0x1180`, … Low 7 bits are always zero. They are not sub-file indices
   of the same container (`wd_tw` has 7 sub-files but stores `0x380`/`0x480`).
4. **World code → world name.** `tt`, `aw`, `he`, `al`, `eh`, `nm`, `zz`, `pp`,
   `bb`, `tw` are the shipped codes; which is Twilight Town, which is Agrabah,
   etc. is a guess until a room is rendered or `mdb.z` is decoded.
5. **Whether each `col_*` prefix is walk collision, camera collision, or battle
   bounds.** `col_btl` living in ov013 rather than ov002 is a hint, not proof.
6. `mi/mi/eid.z` and `mi/mi/evi` roles; the `CAKP` container format.

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
