# KINGDOM HEARTS II's enemies in custom missions

Not the game's: how the port can let a custom mission place KINGDOM HEARTS II's enemies, fighting with their own AI
scripts from the player's copy (KINGDOM HEARTS HD 1.5+2.5 ReMIX on PC). Our own reading of the player's
`KINGDOM HEARTS II FINAL MIX.exe` (KH2FM.exe) and files, and OpenKH's public documentation pages for some layouts; the
script machine and the objects' files are in `docs/FORMATS.md` ("KH2's objects and their AI").

This document is the authors' and players' guide first ("Playing KH2 enemies"), then the research it rests on: what is
read of KH2, what the port does with it, and what is not known yet.

## Playing KH2 enemies

- **The mod**: "Days - KH2 Enemies", made from the player's own KINGDOM HEARTS HD 1.5+2.5 ReMIX by the installer (its
  port mods, inside the HD PACK) or by hand:

      python tools/mods/kh2_enemies.py --kh2 <kh2.hed_out> --exe khdays-native.exe \
          --kh2-exe "<1.5+2.5>/KINGDOM HEARTS II FINAL MIX.exe"

  It copies each kind's `obj/<name>.mdlx` (model, script, collision), `.mset` (motions) and `.a.us` (effects,
  sounds), their HD pictures and KH2's resident effect pictures (`effect/texcommon.dpd`), reads the shapes of KH2's
  calls from KH2's executable (`kh2/calls.txt`) and lists the kinds with whether the port answers every call their
  scripts make (`kh2_enemies.txt`: the Soldier `M_EX010` and the Shadow `M_EX020` both covered). Nothing of the games
  is in the repository or the installer.
- **The setting**: "KH2 enemies" (Settings, the `[gameplay]` `kh2_enemies`, on by default). Off, without the mod or
  without HD graphics (the port's own polygons are drawn only with them), every KH2 enemy plays its stand-in.
- **In a mission** (`missions.txt`, docs/STUDIO.md): `wave <n> = kh2:M_EX010/4x3 at <x> <y> <z>` puts three KH2
  Soldiers in Days Soldiers' places (class 4 standing in); `kh2:M_EX020/0x2` two KH2 Shadows in Days Shadows'. Without
  a class (`kh2:M_EX010x3`) the stand-in is Days's Soldier, and the mission needs KH2's enemies: it is left out without
  them. The stand-in's class counts for the mission's goals (`target = <class>`, its hearts), its HP, level, drops and
  rewards.
- **What plays**: KH2's own script drives each one -- appearing with its effect, closing in, its claw, kick and jump
  attacks, rolls, steps back, turning to face its target -- with its KH2 model (HD pictures), motions and effects; its
  attacks hit Days's players as Days's enemies' do (their damage the stand-in's attack power); Days's hits make it
  flinch (KH2's small damage motions, from the front or the back) and its end is KH2's ("dead": its fading), then
  Days's (the heart, the rewards). Its sounds are its stand-in's for now (below).

## The first enemy: the Soldier

The Soldier (`obj/M_EX010.mdlx`) is the basic ground Heartless whose script makes the fewest calls: 62 different
calls, 27 states (its triggers 11, 6, 13, 1 and 0). The Shadow (`M_EX020`) shares 59 of them and makes 12 more.
Every enemy script with fewer calls is a special variant (the Dusk "RTN" 44, the white Magnum Loader 47, the
Sorcerer's cubes 51). Days has its own Soldier (`ov120_enemy_soldier`, class 0x04) and Shadow (`ov114`), each the
natural stand-in for KH2's.

## The script machine as KH2 runs it

`FUN_14041b400` (the interpreter), read whole:

- **A thread** holds its script, its program counter (a code word number), its frame pointer, its value stack (8-byte
  items: the value, then a tag `@INT` / `@FLT` / `@ADR` / `@???`) and its work area. The work area and the frame keep
  a tag for each 4-byte slot in a shadow area just after them (the work area's size, the frame's size apart):
  a value popped into them takes its tag along, one pushed from them brings its tag; any other memory gives `@???`.
- **Push** (op 0): sub 0 a 32-bit integer, sub 1 a float (two code words, low first); sub 2 the address of a base +
  the next word; sub 3 the value there. Bases: 0 the frame, 1 the work area, 2 the address held in the frame's first
  word, 3 the script's own code (sub 2: the address of code word `n`, sub 3: the 32-bit value at code words n, n+1).
- **Pop** (op 1) into a base + offset (base 3 throws the value away). **Copy** (op 2): pops a source address, copies
  the next word's bytes to base + the word after (the destination's tags set `@???`). **Load** (op 3): pops an
  address, pushes the value at it + the next word. **Store** (op 4): pops a value, then an address; argument 0 stores
  the value (and its tag), argument n copies n bytes from the address the value holds.
- **One value** (op 5), integers (sub 0): to float, negate, not, == 0 (arguments 4 and 8), abs, < 0, <= 0, != 0,
  >= 0, > 0; floats (sub 1): to int (truncated), negate, abs, < 0, <= 0, == 0, != 0, >= 0, > 0. (The decompiled code
  shows an integer's "== 0" and "!= 0" as float compares of its bits, which would count 0x80000000 as zero: **not
  confirmed** against the instructions; the port compares integers.)
- **Two values** (op 6): integers -- add (an address plus an integer stays an address), sub, mul, div (by zero: the
  left value kept), mod, and, or, xor, shift left, arithmetic shift right (by the low 5 bits), logical and, or;
  floats -- add, sub, mul, div (by zero: the left kept), `fmodf`.
- **Jumps** (op 7) by a signed word from the instruction after: always, if zero, if not zero (the popped value's
  32 bits). **Call** (op 8) by a signed word, far call (op 11) by a 32-bit one: the frame grows by the argument's
  4-byte slots; its last two slots keep the return word and the slot count (tags `@???`). **Return** (misc 2) goes back
  and shrinks the frame; returning to word 0 ends the thread.
- **Misc** (op 9): 0 yield (the thread waits for the next tick), 1 exit, 2 return, 3 drop, 5 duplicate, 6 sin and 7
  cos of the angle brought into [-pi, pi) (`fmodf(x + pi, 2 pi) - pi`; cos of pi / 2 exactly 0), 8 degrees to radians
  (brought into [-pi, pi) too), 9 radians to degrees.
- **Calls into the game** (op 10): the argument a table, the next word an entry; its argument count popped into an
  array (the first pushed first), the function run, its value pushed when the entry says so.
- An unknown operation ends the thread with an error (state 5).
- **Ticks** (`FUN_1403e1c80`): a thread runs until it yields (it stays) or ends; then its child threads run, each
  once, after it in the same tick. **Events** (`FUN_1403e1840`): a temporary thread runs the trigger's code at once,
  until its first yield or its end, and is thrown away.
- **The PC's 60 frames a second**: while a tick lasts less than 2.0 (KH2 on PC: 1.0), a float pushed with a known value
  at a known code offset is changed (for instance 0.83 at byte offset 0x2e8 by `FUN_1403b4d30`, 2.4 at 0x262a halved,
  20.0 at 0x180c made 18.0, 30.0 at 0x1a8c doubled in `b_ex240`); and in the script `fire`, 44.0 popped at 0x26a is
  lowered by 2.0. These apply to the scripts whose code has those values at those places. The port runs the KH2
  enemies at 60 ticks a second with a tick of 1.0, two for each of Days's field steps, as KH2 on PC.

## States

Most of a script's behaviour is its states (`docs/FORMATS.md`). A state's id is the address of a **name** in the
script's code, and a state is found by its name (`FUN_1403ca700` compares the strings), so the game asks for states by
name too. The Soldier registers 27 at its start with `1:6` (`appear`, `mode_battle`, `btl_attack`, `atk_craw`,
`atk_kick`, `atk_jump`, `move_roll`, `move_back`, `move_step`, `move_target`, `freeze`, `revenge`, `dead`, `leave`,
`event`, `idle`, `footwork` ...): each name, flags, the routine run as a child thread while it is the state (+8), a
routine run at once when it is left (+0xa, `FUN_1403caec0`), one run at once once it is entered (+0x16,
`FUN_1403cad00`), a priority (+4). Flag 2 of the high half locks a state while it runs (`1:8` and `1:9` refused). Its
main thread enters `appear`, queues `mode_battle`, and every tick enters `mode_battle` again when nothing is queued or
running (`1:10`); the battle states pick the attacks and moves.

The game asks for states by name: `dead` (`FUN_140410af0`, the defeat), `leave` (`FUN_1403de080`), `reraise`,
`dead_crash`, `after_talk`, `before_check`; a hit asks for the reaction state its attack names (below).

## The Soldier's and the Shadow's calls

Grouped by what they touch; `P(x)` turns a script address into the game's pointer (`FUN_1404ad270`); "the actor" is
`P(P(handle) + 4)`. What the port answers is in `port/runtime/kh2_enemies/calls.cpp`; where KH2 reaches something
Days's field does not have (KH2's maps' paths, its events, its battle records), the port gives the plain answer the
scripts take as "nothing there", named in the table.

| Call | Read | The port |
| --- | --- | --- |
| 0:3 | the tick's length (1.0 at 60 a second) | 1.0 |
| 0:4, 0:5, 0:36 | a + b, a - b, a x f into the call's own vector, NaN components made 0 | as KH2 |
| 0:13 | a vector turned about the up by an angle (`FUN_1403b7680`) | as KH2 |
| 0:6, 0:7 | a vector's length; made unit in place, its length given back | as KH2 |
| 0:16, 0:17, 0:18 | random: below n, times f, between a and b (`x = x * 0x10dcd`, 32 bits) | as KH2 |
| 0:47 | memory of n bytes (`FUN_140152430`: the game's allocator, `DAT_1409ba920`'s +8) | the scripts' memory |
| 0:79 | memory given back four calls later (a ring of four, `FUN_1403f63a0`) | as KH2 |
| 1:3 | the player's actor (`DAT_142a105d0`) | the nearest of Days's players |
| 1:4, 1:79 | face a direction (yaw = `atan2(x, z)` at + 0x7a8; 1:79 also the facing at + 0x680, its w the yaw) | as KH2 |
| 1:80 | turn toward a direction by a step at most (the step x 2.0, `DAT_1405b18ec`; 0 the actor's + 0x134): 1 while it turns, 0 once it faces it (`FUN_1403b9f70`) | as KH2 |
| 1:5, 1:6, 1:7 | a state table cleared, a state registered, the table and handle given to the state machine (+ 0x390) | as KH2 |
| 1:8, 1:9, 1:10, 1:127 | enter a state, queue one (15 at most), whether one is queued or running, clear the queue | as KH2 |
| 1:11, 1:61 | play motion `id` blended over `blend` frames, 1:61 from a frame (`FUN_1403c86a0` on the motion controller at + 0x158) | the motion from the mod's .mset |
| 1:12 | the same, left as it is when it already plays (`FUN_1403c6dc0`): the scripts call it every tick while they walk | as KH2 |
| 1:14, 1:15 | + 0x18c bit 1, the motion played to its end; + 0x180, the number of the motion playing: the scripts wait on both | as KH2 |
| 1:67, 1:68 | a motion's length in ticks (the motion controller's virtuals + 8 and + 0x30; -1 the one playing) | as KH2 |
| 1:55, 1:168 | a jump (`FUN_1403cbb00`, `FUN_1404233f0`): a vector -- its x, z the way, its y the height (negative up: the speed up `sqrt(-y x 2 / 0.408163)`), its w the speed along -- then a motion; a flag sets the motions after it (+ 0x118 its fall, + 0x11c its landing, 5 `LAND` by default) | as KH2, under KH2's gravity (2.45 a tick a tick) |
| 1:19, 1:20, 1:28 | its fading (+ 0xa08 and its change a tick, `FUN_1403b7810`; a time of 2 or less is 1): from 1 to 0, from 0 to 1; 1:28 to 0 at once and held (+ 0x9b8 bit 4) | drawn as the model's alpha |
| 1:78, 2:5 | + 0x18c bit 9: a reaction holds it (no moving) | set while a Days hit's reaction plays |
| 1:60 | off the ground or held (`FUN_1403ba9a0`: the class's bit 0, + 0x18c & 0x1000020, + 0x120 bit 23; then + 0x18c bit 6) | + 0x120 bit 23 while it jumps or falls |
| 1:65, 1:70, 1:71 | flag 0x4000000 at + 0x120 set; any bit there set; cleared | as KH2 |
| 1:89, 1:111 | a move this tick: a direction made unit and a speed (+ 0xa58 / + 0xa64, cleared after each tick by `FUN_1403b8e4d`; 1:111 through + 0x10 / + 0x1c) | the travel added that tick |
| 1:147, 1:201 | its place (+ 0x670) and its facing as a direction | as KH2 |
| 1:62, 1:64 | the travel of the tick before (+ 0xa38) and of this one (+ 0xa28) | as KH2 |
| 2:23, 1:121, 1:120, 1:131 | its target slot (+ 0xbf8): made "itself" (2) while empty; a target chosen for an owner by a mode (`FUN_1403be2b0`: 2 the owner, else the player); the target's place; the slot as KH2's pool keeps it | the nearest player |
| 2:88 | the last to hit it (+ 0xd38), the player's actor when none | the player nearest at the hit |
| 1:87 | an effect on the actor (`FUN_1402c6b30` on + 0x80): a group and a number | **not known** which effects the groups name: none |
| 1:57 | the class's byte + 0x0c bit 0 (a kind that moves by its speed: the Soldier walks by its motions) | 0 |
| 1:124, 1:56 | its entry's flag; `FUN_1403ba4a0`'s value | 0 |
| 1:110 | its battle record's byte + 0x1c (`FUN_1403b50d0`), -1 without one | -1 |
| 1:39 | its battle number n (+ 0x9f4 + 4 n; the Shadow tests 99) | 0 |
| 1:104, 1:105, 1:309 | + 0x8c8 bit 6 set and cleared; + 0x6c8 bit 7 | as KH2 |
| 1:23, 1:34, 1:301, 1:171, 1:206 | KH2's field busy: `DAT_140717008` (tested == 10), `DAT_142a11478`, `FUN_1403bbea0`, `DAT_142a11400`'s bits 2 and 7 | 0: Days's field is never busy for its enemies |
| 1:119 | a way toward a point at a height (`FUN_1403b7f10`): 1 or 2 a jump on the way, 3 none | 0: Days's rooms have no KH2 paths; the room's collision keeps the actor out of the walls |
| 1:92 | a point of the ground ahead (a ray, then `FUN_1403dc170` with a number) | nothing |
| 2:8 | whether an object's kind (+ 0x26) is in the groups past 1 (trigger 6's sender) | 0 |
| 1:128, 1:133, 1:134 | the object's class's virtuals + 0x20, + 0x80, + 0x88 (its native code) | nothing |
| 2:9 | the actor's + 0x5c0 made from a number (`FUN_140411350`) | nothing |
| 2:20 | a routine of the script started as a thread of its own (`FUN_1403c5ec0`) | as KH2 |

## Triggers

| Trigger | Read |
| --- | --- |
| 0, 1 | start (once, with the object's address) and main (its own thread, every tick) |
| 6 | sent to every one of 8 listening threads (`FUN_1403f47b0`, reached by tail jumps from `FUN_1403d2ba0`): **not known** what event |
| 11 | a message from another object: raised by the call `FUN_14042c0d0` (one script to another object: its sender, a number, a target) and two native callers |
| 13, 14 | `FUN_1403b7920`, `FUN_1403b79d0`: no direct reference in the executable (called through something built at run time). 13 clears the actor's flag 0x4000000, leaves the current state, runs trigger 13, then the class's virtual + 0x28 and the object's + 0x50. The Shadow's 13 plays motion 4 (`FALL` in KH2's motion list) and sets 0x4000000 again: most likely the actor leaving the ground. **Not confirmed** |
| 10, 15 | `FUN_1403b3030`, `FUN_1403d5500` (pointers in `.rdata` tables): **not known** |
| 16, 17 | a motion's events (`FUN_14040dd70`) |
| 19 | **a hit** (`FUN_140410700`, `FUN_140411060`): the attacker kept at the actor's + 0xd38 (what `2:88` gives back), trigger 19 run with the attack and the attacker, then the attack's reaction applied (`FUN_1403d4650`), the queue cleared and the reaction state the attack's data names entered by name |

## Motions and their events

A motion set (`obj/<name>.mset`) is a BAR whose Nth entry is motion slot N (type 17; empty `DUMM` slots); a motion
number maps to its slot in order for enemies (OpenKH's MSET page: 0 IDLE, 1 WALK, 2 RUN, 3 JUMP, 4 FALL, 5 LAND, 10-24
damage, 44 APPEAR, 45 LEAVE, 76 DEAD_LAND, 77 DEAD_AIR, 96+ the object's own). The Soldier's set has 254 slots,
the Shadow's 204. Each motion (an ANB BAR) holds its motion data and its triggers (OpenKH's ANB page): ranges of
frames (state flags; 4 / 5 a collision group on or off; 10 an attack -- its `atkp` entry and collision group; 27
"can't be hit") and single frames (1 an effect caster, 8 a sound -- package and number, 23 / 24 fades, 15 turn to the
target). The port reads the motion data (`assets/src/kh2_motion.cpp`) and its attributes (`MotionAttributes`). Read in the
Soldier's and the Shadow's: an attack range (10, and 33 with one more number) is `(atkp entry, collision group)` --
the claw `201` frames 58-70 `(5, 1)`, its long form `202` `(1313, 1)`, the kick `203` frames 4-45 `(405, 7)` and 45-67
`(7, 3)`; a trigger 1 is an effect, its one number a PAX element of the object's `.a.us` (the appearing `44` element
0 at frame 0); a trigger 8 a sound, `(bank, number, 0, 0)`: the Soldier's `(200, 21)` and `(200, 22)`, the Shadow's
`(201, 20)`, `(201, 21)`. A motion's frames there are the motion's ticks (60 a second). The motions' numbers are their
slots in order (an enemy's set): the Soldier's appearing is `44` (`80A`), its idle `0`, walk `1`, run `2`, fall `4`,
landing `5`, its damage `10`-`21`, its own from `200`.

## Collision

An object's `.mdlx` holds its collision shapes in its BAR entry of type 0x17: a 64-byte header (its count first) and
20-byte records. Read from the Soldier's 17 and the Shadow's 10: a group (byte 0), a kind word (+4: 0x300 the body's
cylinder, 0x302 a sphere on a bone, 0x306 an attack's sphere, 5 a large box), a bone (+6; 0x4000 none), an offset x, y,
z, then sizes (height, radius). The Soldier's body is group 0 (a cylinder 115 high, 90 round; spheres on bones 2, 28,
31); its attacks are groups 1, 2, 3, 4 and 7 (spheres of radius 40 to 110 on bones 88, 63, 2); group 6 is five boxes
700 by 300 round it (**not known**: likely the area its attacks are tried in). A motion's trigger 10 names the group an
attack uses: the port tests the group's attack spheres (kind 0x306) on their bones, in the pose the motion has
that tick, against Days's players' collision spheres. The record's other bytes are **not known**.

## Effects

An object's effects are its `.a.us`'s PAX (its BAR entry 0x12; docs/FORMATS.md, "KH2's effects"). The PAX's elements
(its 0x82 directory's 32-byte entries) are `{u32 its effect's offset from the directory, u32 its emitter, u32 its
number}`: the Soldier's 9 elements over 4 effects, the Shadow's 3 over 3. A motion's trigger 1 names an element by its
number. The port runs each element started on the enemy at the enemy's place (`runtime/mods/kh2_fx_draw.h`, the Dark
Corridor's way: the effect in KH2's axes and centimetres, its view the enemy's); **not known**: whether KH2 puts some
of them on a bone, and which effects `1:87`'s groups name (the "dead" state's `1:87(2, 1)`, the leaving's `(1, 1)`),
so those are not played.

## Sounds

An `.a.us` holds a PS2 wave bank (`wave`, `WD`), a small `se20` entry `ORIGIN` naming its bank (`0xc8` = 200 for the
Soldier, 201 the Shadow), how many sounds it has (25, 22) and the PC's bank (`se/obj/se200`: the remaster's
`remastered/obj/<name>.a.us/se/obj/se200`, an SCD of 13 recordings for the Soldier, docs/FORMATS.md "KH2's sound
banks"). **Not known**: how a sound's number (a motion's trigger 8: 21, 22) leads to a recording; until it is, a KH2
enemy sounds as its stand-in (Days's hit and defeat sounds play as Days plays them).

## Days's side: the shell

A Days enemy is an ov107 actor (docs/RUNTIME_MAP.md sections 22 and 37): its class registered with
`Ov107_RegisterHandler` (the Soldier: class 0x04, `Ov120_CreateNamedEntity`), its collision sphere at + 0x74, its
model's place and turn at + 0xa0 (an srt: a quaternion, then + 0xb0 the translation), its HP at + 0x218 / + 0x21a.
The port (`port/runtime/kh2_enemies/shell.h`) makes it a KH2 enemy's shell:

- **Made**: `Ov002_CreateActorFromMarker` makes it from the mission's enemy table's row; a custom mission's builder
  gives each class and KH2 enemy in its place a row of its own (`studio::enemy_rows`), so the row says which KH2 enemy
  its actors are (`khdays_missions_kh2_enemy`).
- **Set going** (its + 0x60's 0x100, this game's own: + 0x50 1): KH2's script starts on a KH2 actor at its place,
  facing the nearest player.
- **Each frame** (`Ov107_ProcessObjectTick`, joined): its Days tasks are held as the Stop magic holds a frozen enemy
  (given no time), never skipped by its distance from the players; the KH2 actor runs two of KH2's ticks (the field's
  step is 0x1000 a 30th of a second) -- its script, its motion, its root's travel, its move, KH2's gravity off the
  ground -- and its place and turn are written into the srt (`Srt_SetTranslation`, `Srt_SetRotationQuat`,
  `Ov107_UpdateCollisionSphere`); the room's collision (`Ov107_AiState_ResolveContacts`, later in the same tick) then
  keeps it out of the walls and on the ground. Its attacks' spheres hit the players they reach once a range
  (`Ov107_InvokeHitCallback` with its stand-in's first attack slot with a power, as `Ov120_SweepAttack_Step` does).
- **Hit** (`Ov107_AiState_ApplyHit`, joined after its class's handler took the hit): its HP down, KH2's small
  reaction plays (the queue cleared, + 0x18c bit 9 set, its script waiting) unless a locked state runs; its HP gone,
  the state `dead` is entered (unlocked: the game's own entry). Days defeats it at once and reuses it for the next
  wave: the KH2 enemy's end goes on where it was, drawn as the 3D list closes, until it has faded out.
- **Drawn**: Days's draw of its model (`khdays_models_draw_begin`) is taken when it is its class's own model (in
  `Ms/<class>.p`) drawn within 25 cm of the shell's place, the nearest shell of that class -- no link from the actor
  to its render object was found: its node's + 0x24 (a battle actor's) is not one; the Shadow's model lies 6 cm from
  its actor's place -- its shapes left out and the KH2 model drawn at the shell's place in its pose
  (`khdays_models_draw_posed`), turned by its yaw. The pose has its root's travel taken out (the place carries it:
  left in, the model ran ahead of its body, its attacks' spheres and Days's lock-on mark with the body). Days's
  enemies' draws carry their place in the world in the position matrix, the camera folded into the projection.

## The scripts run in the port

The port's machine (`assets/src/kh2_ai.cpp`) and the KH2 actor with its state machine
(`port/runtime/kh2_enemies/actor.cpp`) run the Soldier's and the Shadow's scripts headless on a made-up actor, the
calls' shapes read from the player's executable (`tools/research/kh2_ai.py --exe-calls`: 869 calls in the tables),
the calls the port answered then handled and the others giving back 0 (the research's runs, before US1):

    khdays-native --kh2-ai-run obj/M_EX010.mdlx calls.txt 600

- The Soldier: `appear`, then `mode_battle` -> `btl_attack` -> `atk_craw`, `move_roll` or `move_back`, again and
  again for 600 ticks; 43 different calls made; no thread stopped (no runaway, no stack overrun).
- The Shadow: `appear_shadow` and its battle states the same way, 38 calls.
- **Neither script reads or writes its actor itself**: every offset of the actor they use goes through a call. The
  concern that scripts read KH2's actor table directly falls, for these two, entirely on the calls the port answers.
- `--coverage` with the port's list (`khdays-native --kh2-ai-calls`): the Soldier 25 of 62 handled, the Shadow 25 of
  71 at the research; with US1 the port answers 74 calls, every one of both scripts'.
- **In play** (the `kh2_enemies` walk, `port/tools/regress.py`, `KHDAYS_TRACE_KH2_ENEMIES=1`): the Soldier appears
  with its effect, enters `mode_battle` -> `btl_attack` / `btl_short` -> `atk_craw`, `move_roll`, `move_back` as in
  KH2, its claw (`202` frame 58) hits Roxas, it flinches and is defeated; the Shadow beside a second Soldier the same.
  Its attacks waited on `1:15` (the motion playing's number at + 0x180): answered 0 at first, every attack ended the
  tick after it started.

## Go / no-go (the research's report, kept as it was given)

**Go**, for the Soldier and then the Shadow, with these risks named:

- The machine is complete and exact to KH2's interpreter (every operation, threads, events, the PC's 60-frames
  constants), and both scripts run in it without a fault. The port runs KH2 enemies at 60 ticks a second (two for
  each of Days's field steps), as KH2 on PC.
- Every behaviour is in the scripts' states, entered and left by name, and the game's events reach them as KH2's do:
  a hit by trigger 19 and a reaction state, the defeat by the state `dead`, a departure by `leave`. Days's own events
  for the shell (`Ov107_AiState_ApplyHit`, `Ov107_AiState_OnDefeat`, the spawner's send-away) map onto those one to
  one.
- The shell's AI can be held where the Stop magic already holds an enemy (`Ov107_ProcessObjectTick`).
- 37 of the Soldier's calls remain, all read to their KH2 functions; those that need Days's world (the player, the
  ground, a target) have their counterparts in the field.
- **Not known yet, first tasks of US1**: the objects' native classes (`1:128` and trigger 13's virtuals: whether the
  Soldier depends on class code; its script makes `1:128` in one place); the layout of a motion's event list in
  the bytes (OpenKH's ANB page gives the triggers; `FUN_14040dd70` not read); how the shell's model is hidden while
  KH2's is drawn; the KH2 / Days damage scale (measured with both Soldiers' numbers); the sounds' banks.
- **Deferred to US4's start**: whether Days's multiplayer messages carry every enemy's place and facing (traced with
  two copies).
- **Names**: no mapping from an object to its enemy name was found in KH2's own data; the kinds' titles are their
  object names unless the author gives one.
