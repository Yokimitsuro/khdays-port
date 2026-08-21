# Gameplay runtime (ov002) — ground contact, collision naming, camera

The first named gameplay behaviour to come out of `khdays-decomp`. Until the
`11cdd35f0` bump, ov002's functions were decompiled but unnamed, which is why
Phase 5 had nothing to build on. This document records what the named ones say,
and — the point of it — how they connect to the world data in
[MISSION_WORLD_DATA.md](MISSION_WORLD_DATA.md).

Every constant below is read from the matched C or from the ROM through Ghidra.

## Ground contact — the roster slot ground ray

`func_ov002_02069a7c` (Ghidra: `Ov002_TestRosterSlotGroundRay`) answers "does
this party slot have ground under it?":

- the **collision handle** is the signed halfword at `entry+0x66`; **negative
  means the entry has no collision**, and the answer is no without casting;
- otherwise the ray starts at the entry's position **raised by `0x1000`** (1.0 in
  20.12) and points **straight down by `0x32000`** (50.0) — `dir = (0, -0x32000, 0)`;
- the word at `entry+0x20` is passed as the cast's `pExtra` — **an exclusion key,
  not a surface mask**; see the collision-world section;
- the answer is simply whether the cast hit.

`func_ov002_020692a8` calls it per slot and only refills that slot's defaults
(`func_ov002_02069b14`) when it answers yes.

### The cast itself

`func_0202c268(handle, from, dir, key)` fills a `CollCastParams`
(`wDirIsUnit = 0`, `wFlagE = 0`, origin, dir, and the key as `pExtra`) and calls
`Collision_RunRayCast(ctx->aViewSlots + handle * 8, params)`.

So a **collision handle is an index into an array of 8-byte "view slots"**, not a
pointer and not a name.

## The `col_*` / `gate*` names are resolved at runtime — two different tables

[MISSION_WORLD_DATA.md](MISSION_WORLD_DATA.md) established that a room's data
blob carries named entities (`col_wall00`…, `col_hole`, `col_btl`, `gate00`…)
and surface tags. This is the other half: what reads them.

`FUN_arm9_ov002__020715c4` builds **both** names from the same `"%s%02d"`
literal at `arm9_ov002::0207f0d4` — `col_wall` (`0207f0dc`) and `gate`
(`0207f0e8`) — and then sends them down **two different paths**:

### `gate%02d` → the named collision table

`FUN_0202bfe8(scene, name)` walks the scene's objects (count `u16` at `base+2`,
pointers at `base+4`) and, per object, a table of **`0x14`-byte records** at
`obj+0xac` with the count at `obj+0x82`. It matches with
**`strncmp(record, name, 8)`** — so the **first 8 bytes of a record are its
name**.

The caller then writes `record[0xc] = 0` or `1`, which is how a gate is opened or
closed, and on a change calls `FUN_0202c13c(scene, name, data, 4)` with a 4-byte
payload built from the caller's own bytes.

Note the 8-byte compare: `gate05` fits, but a 10-character `col_wall05` would not
be distinguishable from `col_wall12` through this path — consistent with
`col_wall*` **not** using it.

### `col_wall%02d` → named scene entries

`FUN_arm9_ov002__02072aa0(name, len, visible, scene)` instead loops
`Ov002_FindNamedEntryFrom(owner, name, len, &index)` — matching on an **explicit
length**, and iterating so one name can hit several entries — and toggles each
through `FUN_02028e4c(owner, channel, entry, …, visible)` on **channels 2 and 1**.

So the two families of names in the room data are read by two different
subsystems: `gate*` are collision records toggled by a byte, `col_wall*` are
named scene entries toggled on two channels. Both reach them through the **same**
view-slot index the ground ray casts against — see the collision-world section
below.

## Camera distance

Two functions, both directly portable.

### Base distance — `func_ov002_02050a54`

Indexed by a **selector**, from a 12-byte record table (below):

- the base is word 2 of the selector's record;
- **selector 10 alone is interpolated**: when the active actor's depth
  (`entry+0x494`) is below `-0x10000`, the base is pulled toward record 0's word 2
  by `FX_Inv(-0x10000 - depth, 0x8000)`;
- a fixed offset is then added, keyed on the field the decomp calls the actor's
  **world** (`entry+0xc`): `+0x4cd` for worlds 0, 5, 0xb, 0xe, 0x12–0x15;
  `+0xccd` for 1–4, 6–0xa, 0xc, 0xd, 0xf; `-0x400` for 0x10 and 0x11;
- **only selectors 0 and 10 get that offset** — every other selector returns the
  bare base.

That world field spans 0..0x15 (22 values), which is more than the 12 entries in
ov002's `wd_` world-code table, so the two indices are **not** obviously the same
thing. Do not assume they are.

### Per-frame smoothing and occlusion — `func_ov002_02050b90`

With `cam+0x38 & 0x8000000` clear nothing moves. Otherwise, with the aim
direction (`cam+0x14` − `cam+0x20`) and the direction to the tracked point
(`cam+0x8c` − `cam+0x20`), both normalised:

- **view clear** (`dot <= 0xa00` **or** distance `>= 0x50000`): the held distance
  (`cam+0x98`) is pulled in by **`0x80` per frame**, never below the selector's
  minimum;
- **possibly occluded** (`dot > 0xa00` **and** distance `< 0x50000`): up to
  **20 probes** (`func_ov002_0204e2e0` at offsets 0/0 then 0x14/0x14); each failed
  round pushes the wanted distance out by **`0x200`**;
- the held distance then rises toward the wanted one by at most **`0x100` per
  frame**, again clamped at the minimum;
- the function returns the held distance **plus `0xc00`**.

### The selector table (`arm9_ov002::0207e764`, stride 0xc)

Word 0 is the minimum the smoother clamps to; word 2 is the base the lookup
reads (its label `data_ov002_0207e76c` is just this table + 8). Word 1 is not
touched by either function.

| sel | w0 (min) | w1 | w2 (base) |
|---:|---:|---:|---:|
| 0 | `0x5000` | `0x1a00` | `0x1000` |
| 1 | `0x5000` | `-0x800` | `0x1a00` |
| 2 | `0x5000` | `0x1600` | `0x1000` |
| 3 | `0x4000` | `0x1800` | `0x1000` |
| 4 | `0x5000` | `0` | `0x14cc` |
| 5 | `0x5000` | `-0xc80` | `0x1000` |
| 6 | `0x2800` | `0x1a00` | `0x1000` |
| 7 | `0x0100` | `0` | `0x1000` |
| 8 | `0x3400` | `0x300` | `0x14cc` |
| 9 | `0x5000` | `-0x400` | `0x1c00` |
| 10 | `0x6800` | `-0x1800` | `0x2800` |
| 11 | `0xa000` | `-0x2800` | `0x2800` |
| 12 | `0x2400` | `-0x800` | `0x1000` |
| 13 | `0x2800` | `-0x800` | `0x1000` |
| 14 | `0x3000` | `-0x800` | `0x1000` |
| 15 | `0x3800` | `-0x1000` | `0x1000` |
| 16 | `0x3800` | `0x1000` | `0x1000` |

**The table is exactly 17 records.** It spans `0x0207e764`..`0x0207e82f` (0xcc
bytes = 17 x 0xc), and the word right after it is `0x000f003f` followed by a
pointer pair — the pattern breaks cleanly there. So there are **17 camera
selectors, 0..16**.

### Word 1 — who reads it

`FUN_arm9_ov002__02050b68(selector)` returns `*(u32 *)(0x0207e768 + selector * 0xc)`,
and `0x0207e768` is this table + 4, i.e. **word 1**. So all three words are
per-selector camera values, each fetched by its own function.

### The selector itself

`Ov002_SetValueAndDerive(owner, value)` is what sets it. With
`cam = *(owner + 0x20)` — the same camera pointer `func_ov002_02050b90` reads —
it pushes the current selector to `cam+0x48` and stores the new one at
**`cam+0x44`**, then derives three values from it twice over:

```
cam+0x54 / cam+0x7c  = Ov002_UpdateCameraDistance(sel)   // func_ov002_02050b90
cam+0x5c / cam+0x84  = FUN_arm9_ov002__02050b68(sel)     // word 1
cam+0x60 / cam+0x88  = Ov002_GetCameraDistance(sel)      // func_ov002_02050a54
```

What *picks* a selector is still unread: `Ov002_SetValueAndDerive` has no direct
cross-references, so it is reached through a dispatch table.

## The collision world — what a handle actually is

The "name to handle" step this document previously listed as unread turned out
to be a **malformed question**: the two are the same index space.

- `func_0202c268` casts against `(*ctx)->aViewSlots + handle * 8`.
- `GetTrackEntryBase(index)` — which `FUN_0202bfe8` (the `gate*` name lookup) and
  `FUN_arm9_ov002__02072aa0` (the `col_wall*` lookup) both call — returns
  **`(*ctx)->aViewSlots + index * 8`**.

Both read `PTR_DAT_0202bfb4` and `PTR_DAT_0202c2a8`, and both hold the same
address, `0x0204c208`. So the halfword at `entry+0x66` does not name a single
collision object; it selects **which collision world** the entity tests against,
and a name then selects a record *inside* the objects of that world.

### View slot (8 bytes)

```
+0x00 u16  ?
+0x02 u16  object count
+0x04 ptr  -> array of `count` object pointers
```

`Collision_RunRayCast` takes `**(u32**)(slot + 4)` — the first object — while the
name lookups walk all `count` of them. Same record, read two ways.

### Collision object

```
+0x82 u16  named-record count
+0x84 int  bounding centre A
+0x88 int  bounding centre B
+0x8c int  bounding radius
+0x9c ptr  geometry tree root  (the narrow-phase walks this)
+0xa0 ptr  model data          (state->pModelDataA)
+0xac ptr  -> array of 0x14-byte named records; first 8 bytes are the name
```

### Broad phase, subdivision and the vertical fast path

`CollCast_TestModelRay` rejects broad-phase with a bounding-sphere-vs-query-bounds
test (centre `+0x84`/`+0x88`, radius `+0x8c`, against the four query bounds at
`state+0x34..+0x40`). On acceptance it fills an **eight-entry ladder** (stride
`0x10`) whose extent field **halves at each level**, seeded from the model's
radius — a spatial subdivision, eight levels deep.

Then it branches on the ray direction: when **`dir.x == 0` and `dir.z == 0`** it
takes a separate, simpler traversal (`FUN_01ffdb54`) instead of the general one
(`FUN_01ffd4a4`). The roster ground ray is `(0, -0x32000, 0)`, so **ground
contact always takes that vertical fast path**.

There is a parallel sphere chain (`Collision_CastSphere` ->
`Collision_RunSphereCast` -> `CollCast_TestModelSphere`) identical in shape except
that it carries a radius at `state+0x74` and offsets all four query bounds by it
before the broad phase — a swept cast.

### Narrow phase — an eight-level quadtree

`FUN_01ffdb54` (the vertical path) walks a **quadtree**. Each node has four
children at `node + 8 + quadrant * 2`; the quadrant is picked by comparing the
query point against the node centre on **two axes** (`state+0x34` vs `node+4`,
`state+0x38` vs `node+8`), giving 0 = −−, 1 = +−, 2 = −+, 3 = ++. The child
centre is offset by **a quarter of the node extent**, so the extent halves per
level — which is exactly the eight-entry ladder `CollCast_TestModelRay` seeds.

Per node it tests a run of **`0x88`-byte face records** based at `state+4`
(`pModelDataA`), indexed by the node's second halfword when that is non-negative.
Each record's flag halfword at `+0x10` carries **bit `0x4000` = skip this face**
and **bit `0x8000` = end of run**.

### `pExtra` is an exclusion key, and the real mask is unused here

`CollCastState.pExtra` sits at **`+0x84`** (confirmed from the struct layout,
size 140), and the traversal reads it as `state[0x21]`. A node also carries a
linked list of dynamic objects (head at `node+4`, chained through `+4`), and an
object is considered only when **both**:

```
obj+0x28 != state->pExtra            // exclusion by key
(obj+0x22 & state->wFlag88) == 0     // bitmask reject
```

So the word ov002 passes at `entry+0x20` is the **first** test — it excludes the
object whose key matches, which for the roster ground ray means the caster does
not collide with itself. It is **not** a surface mask.

The genuine bitmask is `wFlag88` (`state+0x88`), which `CollCast_InitRayState`
copies from `CollCastParams.wFlagE` — and `func_0202c268` **hard-codes that to
0**. With a zero mask the reject can never fire, so nothing is filtered out
through this wrapper. Whatever selects `nohit` / `nocam` / `slide` / `nocatch`,
it is not this path.

## Unknown

1. What selects the `nohit` / `nocam` / `slide` / `nocatch` surface tags. The ray
   mask was the obvious candidate and has been **ruled out** (above), so this is
   now an open search rather than a half-answered one.
2. What picks a camera selector. There are 17 of them and `Ov002_SetValueAndDerive`
   stores one at `cam+0x44`, but it is called through a dispatch table, so the
   callers are not reachable by cross-reference.
3. What word 1 of the selector table *means*. Its reader and its destinations
   (`cam+0x5c`, `cam+0x84`) are known; the quantity is not.
4. Whether `entry+0xc`'s "world" is the `wd_` world id, a room id, or something
   else. ov022 owns these entries and is still almost entirely unnamed.
