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

  It copies each kind's `obj/<name>.mdlx` (model, script, collision), `.mset` (motions) and `.a.us` (effects), their
  HD pictures and KH2's resident effect pictures (`effect/texcommon.dpd`), decodes the sounds each kind plays into
  `kh2/se/<id>.wav` (below, "Sounds"), reads the shapes of KH2's calls from KH2's executable (`kh2/calls.txt`) and
  lists the kinds with whether the port answers every call their scripts make (`kh2_enemies.txt`: the Soldier
  `M_EX010`, the Shadow `M_EX020`, the Fiery Globe `M_AL020_FIRE`, the Icy Cube `M_AL020_ICEE`, the Rabid Dog
  `M_EX510`, the Strafer `M_EX610`, the Wight Knight `M_EX200`, the Assault Rider `M_EX570`, the Nightwalker
  `M_EX580`, the Magnum Loader `M_EX600`, the Hot Rod `M_EX640`, the Cannon Gun `M_EX650`, the Armored Knight
  `M_EX760`, the Creeper `M_EX880` and the Dusk `M_EX990`, all covered; "Coverage" below). Nothing of the games is
  in the repository or the installer.
- **No setting**: a mission that places a KH2 enemy gets KH2's enemy, as its author wrote it. Only a player without
  the mod (or with one that does not cover the kind) gets the class written after the name in its place; without a
  class the mission is left out for that player.
- **In a mission** (`missions.txt`, docs/STUDIO.md): `wave <n> = kh2:M_EX010/4x3 at <x> <y> <z>` puts three KH2
  Soldiers in Days Soldiers' places (class 4 standing in); `kh2:M_EX020/0x2` two KH2 Shadows in Days Shadows'. Without
  a class (`kh2:M_EX010x3`) the stand-in is Days's Soldier, and the mission needs KH2's enemies: it is left out without
  them. The stand-in's class counts for the mission's goals (`target = <class>`, its hearts), its HP, level, drops and
  rewards.
- **What plays**: KH2's own script drives each one -- appearing with its effect, closing in, its claw, kick and jump
  attacks, rolls, steps back, turning to face its target -- with its KH2 model (HD pictures), motions and effects; its
  attacks hit Days's players as Days's enemies' do (their damage the stand-in's attack power); Days's hits make it
  flinch (KH2's small damage motions, from the front or the back) and its end is KH2's ("dead": its effect and its
  fading), then Days's (the heart, the rewards). Its sounds are KH2's: its motions', its effects' and its end's.

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
`FUN_1403cad00`), a routine run when the actor is hit while it is the state (+0x10, `FUN_1403ca470`), a priority
(+4: 100 the modes and moves, 200 the attacks, 300 `revenge`, `dead`, `leave`, `event`). Flag 2 of the high half locks
a state while it runs (`1:8` and `1:9` refused). A state without flag 1 of the high half begins only once the actor is
not held by a reaction (+ 0x18c bit 9) and is on the ground (`FUN_1403ba9a0`), unless another object carries it
(+ 0x6a0); once begun it runs on (`FUN_1403ca550`: + 300's bit 2 cleared). A state's routine that leaves its own state
(`1:8` from inside it) runs on to its yield or its end: `FUN_1403caec0` only lists its thread to be freed
(`FUN_1403e1910`), so what follows the `1:8` still runs (`mode_revenge` enters `rvg_short` then queues `mode_battle`,
`rvg_short` enters `move_counter` then queues `atk_rush`). Its main thread enters `appear`, queues `mode_battle`, and
every tick enters `mode_battle` again when nothing is queued or running (`1:10`); the battle states pick the attacks
and moves.

The game asks for states by name: `dead` (`FUN_140410af0`, the defeat), `leave` (`FUN_1403de080`), `reraise`,
`dead_crash`, `after_talk`, `before_check`; a hit asks for the reaction state its attack names (below).

**Revenge.** KH2 tells a hit to the actor twice (`FUN_140410d60`): its attack's own reaction (`FUN_140410700`,
`FUN_140411060`: the attacker kept at + 0xd38, trigger 19, the reaction's motion, the queue cleared, then the state
the attack's data names at its + 0x18 entered by name -- a small hit's `revenge`), then the current state's hit
routine and **trigger 6** (its argument the hit), the native reaction (the class's virtual + 0xc8,
`FUN_1403d2fe0`) and 8 listening threads. Every enemy script's trigger 6 asks `1:309` of `2:88` -- + 0x6c8 bit 7 of
the last to hit it, set only by the player's maker (`FUN_1403a7a40`): struck by the player, it enters
`mode_revenge`, otherwise `mode_battle`. `mode_revenge` (a state that waits for the reaction to end) picks its revenge
moves: the Rabid Dog's `rvg_short` (`move_counter`, then `atk_rush`) or `rvg_long` (`atk_dive`) by the player's
distance against its handle's + 0x34 (450). `revenge` itself picks `rst_normal`, `rst_hard` or `rst_super_hard` by
its handle's + 0x18; the Rabid Dog registers none of them, and KH2's lookup (`FUN_1403ca700`) then enters nothing.
The port does the same: Days's hit enters `revenge`, then raises trigger 6; Days's players carry the player's bit.

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
| 1:55, 1:168 | a jump (`FUN_1403cbb00`, `FUN_1404233f0`): a vector -- its x, z the way (made one long, then times its w, the speed along), its y the height it rises, either sign (the speed up `sqrt(abs(2 y / 0.408163))`: + 0xf0 for a negative y, + 0xf4 for a positive one) -- then a motion; a flag sets the motions after it (+ 0x118 its fall, + 0x11c its landing, 5 `LAND` by default). KH2 leaves the ground at the motion's take-off event (`FUN_14040dd70` event 0: + 0x648 1 makes it class 1); in the air class 1's movement (`FUN_1403cb360`) keeps 0.9 of its velocity an evaluation when steered, 0.95 unsteered, its height the jump's arc | as KH2, under KH2's gravity (2.45 a tick a tick), from the call |
| 1:19, 1:20, 1:28 | its fading (+ 0xa08 and its change a tick, `FUN_1403b7810`; a time of 2 or less is 1): from 1 to 0, from 0 to 1; 1:28 to 0 at once and held (+ 0x9b8 bit 4) | drawn as the model's alpha |
| 1:78, 2:5 | + 0x18c bit 9: a reaction holds it (no moving) | set while a Days hit's reaction plays |
| 1:60 | off the ground or held (`FUN_1403ba9a0`: the class's bit 0, + 0x18c & 0x1000020, + 0x120 bit 23; then + 0x18c bit 6) | + 0x120 bit 23 while it jumps or falls |
| 1:65, 1:70, 1:71 | flag 0x4000000 at + 0x120 set; any bit there set; cleared | as KH2 |
| 1:89, 1:111 | 1:89 a move this tick: a direction made unit and a speed (+ 0xa58 / + 0xa64, cleared after each tick by `FUN_1403b8e4d`); 1:111 the steering, kept (`FUN_1403b9da0`: + 0x10 made unit, + 0x1c its speed), which the class's movement makes the velocity (+ 0x20) of every two ticks (every tick past 100): on the ground the steering itself (`FUN_140422350`), in the air approached (`FUN_1403b7ad0`). Speeds are centimetres a 60th of a second | as KH2 |
| 1:147, 1:201 | its place (+ 0x670) and its facing as a direction | as KH2 |
| 1:62, 1:64 | the travel of the tick before (+ 0xa38) and of this one (+ 0xa28) | as KH2 |
| 2:23, 1:121, 1:120, 1:131 | its target slot (+ 0xbf8): made "itself" (2) while empty; a target chosen for an owner by a mode (`FUN_1403be2b0`: 2 the owner, else the player); the target's place; the slot as KH2's pool keeps it | the nearest player |
| 2:88 | the last to hit it (+ 0xd38), the player's actor when none | the player nearest at the hit |
| 1:87 | an effect on the actor (`FUN_1403b6670` -> `FUN_1402c6b30` on + 0x80): the element of that number in its effects, and two values passed on to the effect (**not known**) | that element started: the "dead" state's `1:87(2, 1, 0)` the Soldier's element 2 and the Shadow's (its dying smoke and sound), the leaving's `(1, 1, 0)` element 1 |
| 1:57 | bit 0 of the actor's class's byte + 0x0c (the class at + 0x0c; KH2's classes at 0x140750d30, 16 bytes each: class 1 off the ground -- a jump, `FUN_140409ad0`, or a fall, `FUN_14053b920` -- has it, class 0 on the ground, the landing's `FUN_1403cb940`, not): the scripts wait for it after a jump and act on it in the air (US1 read it as the class's own, always 0: a jump's wait never ended) | whether it is off the ground |
| 1:124, 1:56 | its entry's flag; whether it is on the screen (`FUN_1403ba4a0`: with + 0x9b8 bit 10, `FUN_1403f3ed0` tests it against the camera's view; 0 without the bit): the enemies leave once the player is farther than a distance, the shorter off the screen | 0; whether Days drew its model this frame or the one before |
| 1:110 | its battle record's byte + 0x1c (`FUN_1403b50d0`), -1 without one | -1 |
| 1:39 | its battle number n (+ 0x9f4 + 4 n; the Shadow tests 99) | 0 |
| 1:104, 1:105, 1:309 | + 0x8c8 bit 6 set and cleared; + 0x6c8 bit 7 of an actor: the player (set by its maker, `FUN_1403a7a40`) | as KH2; Days's players have the bit |
| 1:23, 1:34, 1:301, 1:171, 1:206 | KH2's field busy: `DAT_140717008` (tested == 10), `DAT_142a11478`, `FUN_1403bbea0`, `DAT_142a11400`'s bits 2 and 7 | 0: Days's field is never busy for its enemies |
| 1:119 | a way toward a point at a height (`FUN_1403b7f10`): 1 or 2 a jump on the way, 3 none | 0: Days's rooms have no KH2 paths; the room's collision keeps the actor out of the walls |
| 1:92 | a point of the ground ahead (a ray, then `FUN_1403dc170` with a number) | nothing |
| 2:8 | whether a hit's reaction kind (+ 0x26, `FUN_1403ce980`'s groups) is past 1: trigger 6's hit, which the Strafer and the cubes turn to their revenge on | as KH2: Days's hits are small reactions (2) |
| 1:128, 1:133, 1:134 | the object's class's virtuals + 0x20, + 0x80, + 0x88 (its native code) | nothing |
| 2:9 | the actor's + 0x5c0 made from a number (`FUN_140411350`) | nothing |
| 2:20 | a routine of the script started as a thread of its own (`FUN_1403c5ec0`) | as KH2 |

The calls the other enemies need (US2, "Coverage" below), read the same way:

| Call | Read (KH2FM.exe) | The port |
| --- | --- | --- |
| 0:21 | how far one vector's way along the ground is turned from another's: `atan2(x, z)` of each, the difference into (-pi, pi] (`FUN_14041d1b0`, `FUN_1403b6f20`: pi and 2 pi at 0x140623b78, 0x140623ba4) | as KH2 |
| 0:35 | a vector scaled where it lies, its four components (`FUN_14013fcc0`) | as KH2 |
| 2:67 | KH2's next running number (`FUN_1403cf490`: `DAT_142a24afc`, 0 at the start, 0x100 again past 0x10000000) | as KH2 |
| 1:63 | its motion at its end with no blend left (`FUN_1403c7e70` on + 0x158: the time + 0x44 against the motion's length, the blend + 0x48 run out) | as KH2 |
| 1:100 | the speed a jump needs to rise a height: `sqrt(abs(2 h / 0.408163))` (`FUN_1403b73f0`) | as KH2 |
| 1:156 | a jump of its own (`FUN_1403cbec0`): a vector -- its x, z the way (made one long, times its w), its y how high -- the ticks to its top (its pull + 0x10c `2 y / t^2`, its rise the pull times t), a value at + 0x108, a motion and its blend, and with the flag the two after it as 1:55's. It leaves the ground as class 9 (+ 0x648 2) at the motion's take-off; class 9's movement (`FUN_1403cbdd0`) keeps + 0x108 of its velocity an evaluation, steered or not (the Wight Knight's tear: up 5 m, then steered ahead at 100, or 250 when far, keeping 0.95: a lunge of some 15 m) | as KH2 (the jump's own pull and keep), from the call |
| 1:150, 1:151 | bit n - 1 of the actor's bytes + 0x2ac and + 0x2ad: set by the native code that places it (the Strafer's and the Magnum Loader's ways of attacking) | the bytes, 0 in Days's field |
| 2:45 | an object's entry number (its + 0x20 record's u16 at + 2): the Wight Knight asks it of a message's sender (492, 1149, 568) | 0: no KH2 object sends one |
| 1:24 | `DAT_140717009`, the byte after 1:23's (tested against 9) | 0, as 1:23 |
| 1:74 | the actor's class made class 0 (`FUN_1403b4da0`: the old one's virtual + 0x10, the new one's + 8) and + 0xba8 cleared | + 0xba8 cleared: no native classes |
| 1:209 | the class's virtual + 0xb0 unless + 0x9b8 bit 2 (`FUN_1403d3f10`) | nothing (native code) |
| 1:278 | n things made at the actor (`FUN_14042a980`: n times `FUN_1403dc950` on the manager at `DAT_142af8b48`) -- **likely its prizes** | nothing: Days gives the stand-in's |
| 0:0, 0:2 | nothing in the PC game (`FUN_14041c9f0`, `FUN_14041ca10` are empty: debugging prints once) | nothing; 0:2's text in the trace (`KHDAYS_TRACE_KH2_ENEMIES`: "avoid start", "high_attack_min") |
| 0:15 | a bit of KH2's story (`FUN_14039a980`: `DAT_1409ab540`'s bits, 0xffff none) | 0: none in Days's field |
| 0:23 | a value without its sign (`FUN_14041ca90`), marked a whole number | as KH2 |
| 0:85 | an effect's handle let go (`FUN_14041e1c0` -> `FUN_1402be8f0`) | nothing: the port's effects end of their own |
| 1:2 | steered toward a point along the ground at a speed (`FUN_1403b9c60`: + 0x10, + 0x1c), the way's length back; its first value the actor itself | as KH2 |
| 1:29 | another object plays a motion of this one's set from a frame (`FUN_1403c6cc0`: its set made this one's, + 0x2b8; + 0x180) -- the Magnum Loader's rider, on its message 309 | as KH2, on another of the port's actors |
| 1:58 | along the ground at a velocity of its own (`FUN_140421ce0`: + 0x20 the vector made one long times its w, + 0x108 a value, class 5, whose movement `FUN_140421c80` keeps + 0x108 of it an evaluation, eased toward the steering) | as KH2 (class 5 ends with 1:74 or a jump) |
| 1:66, 1:69, 1:84, 1:97, 1:179 | + 0x120's 0x8000000 set; the float at + 0x19c; the object at + 0x5c0 (2:9's); where + 0x128 lies; + 0x118 (its fall's motion) set | as KH2 |
| 1:83 | a point given on a bone (0x4000 the actor itself, in its own axes) in the world (`FUN_1403b50f0`: the bone's matrix, the point through it) | the pose the enemy has now |
| 1:90 | a state every object enters by its name, at once or each after a time at random below a value (`FUN_1403cacd0` lists it, `FUN_1403c9d40` sends it to every object, KH2's chance) | every KH2 enemy of the field |
| 1:98, 1:99 | its place on the ground under it (`FUN_1403b87e0`'s ray down); how far above it it is (negative up, 0 on it or below) | the ground Days last found under it |
| 1:112 | a routine of the script as the actor's attached thread (`FUN_1403e1970`, `FUN_1403d76d0`: + 0xd48, the one before ended), the actor its first value | as KH2 |
| 1:140 | whether a target record's object (its + 8) is there (`FUN_1403bde30`; its other test, the player's lock, not read) | its + 8 only |
| 1:143 | an actor found by its placing record's number (`FUN_1403b41e0`, 0x70 for 0) | none: the slot left empty |
| 1:162 | the actor put on one of KH2's lists (`FUN_1403bd410` -> `FUN_14040c9d0`) | nothing |
| 1:211, 1:212 | bit n of + 0xd10 set, cleared | as KH2 |
| 1:230 | a table's record of 12 bytes: its first word over its second (`FUN_1404310f0`) | as KH2 |
| 2:21 | memory several objects share by a number (`FUN_1403f6df0`: n + 1 bytes, zeroed, the same for the same number) | as KH2 |

## Coverage

`tools/research/kh2_ai.py --coverage` (each kind's script, its state routines walked, against
`khdays-native --kh2-ai-calls`), over the 1.5+2.5's `obj/M_*` (names from OpenKH's object list):

- **Covered**: the Soldier `M_EX010` and the Shadow `M_EX020` (and their `_NM`, `_TR`, `_WI`, `_RAW` variants), the
  Fiery Globe `M_AL020_FIRE`, the Icy Cube `M_AL020_ICEE`, the Rabid Dog `M_EX510`, the Strafer `M_EX610`, the
  Wight Knight `M_EX200` -- the mod's kinds (`tools/mods/kh2_enemies.py`). Played in a test mission of their own
  (the `missions_waves` walk's route, 2026-11-08): each appears with its effect and sounds, runs its script's states
  (the Rabid Dog's howl, the Wight Knight's leap, the Strafer's slides, the flyers' rises and leaving), is hit and
  defeated with its end, and hits Roxas: at level 99 (their HP to last), Roxas only attacking where he stands, in
  200 seconds the Rabid Dog 34 hits, the Wight Knight 11, the Strafer 5, the Fiery Globe 14, the Icy Cube 11. What
  made them reach him, all from KH2FM.exe: their revenge (trigger 6 after a hit, Days's players KH2's player to
  `1:309`, `2:8`'s hit; above, "Revenge"), a state waiting for the reaction to end and a state's routine running on
  after it leaves its own state ("States"), a shape on no bone in the actor's axes ("Collision"), `1:55`'s way made
  one long and either sign of its height rising, `1:156`'s own steering (class 9), the steering stopped by a wall,
  `1:56` on the screen, and an enemy that leaves coming back.
- **Covered, second batch**: the Assault Rider `M_EX570`, the Nightwalker `M_EX580`, the Magnum Loader `M_EX600`,
  the Hot Rod `M_EX640` (`_HB`, `_WI`), the Cannon Gun `M_EX650` (`_TR`), the Armored Knight `M_EX760` (`_NM`), the
  Creeper `M_EX880` and the Dusk `M_EX990` (the calls above from `0:0` on). Played (2026-11-08, level 99, 200
  seconds, the four by four in one wave, the last three alone): hits on Roxas, the Assault Rider 10, the Nightwalker
  23, the Hot Rod 25, the Armored Knight 12, the Creeper 2, the Dusk 3. **Not yet**: the Magnum Loader's and the
  Cannon Gun's shots -- their scripts run, but their attacks are effects that hit (KH2's shots are objects and
  effects with collision), which the port does not make yet. Since the research tool follows the routines a script
  starts as threads (`1:112`, `2:20`; 2026-11-09), the Magnum Loader's threads make 3 calls the port does not answer
  (1:13 answered since, 1:94, 1:101) and the Armored Knight's 14 (0:42, 0:44, 0:68, 1:94, 1:132, 1:149, 1:165, 1:191,
  1:221, 1:238 answered since, 6:3, 6:5, 6:8): uncovered, their stand-ins play once the mod is made anew.
- **Next**: the white Magnum Loader `M_EX600_LC` (1:222 its object's number, 1:268 class 23, 1:269), the Graveyard
  `M_EX790` (1:53), the Mole Driller `M_EX700` (2:1), the Neoshadow `M_EX420` (1:13, 2:10 an object made, 2:17 a hit
  made); then the rest, 8 calls and more each, and the shots (2:10's objects, effects that hit).

`KHDAYS_TRACE_KH2_ENEMIES=1` says what each enemy does (its states, effects, sounds, hits, the stand-in's sounds
kept quiet, each attack's window with its spheres and the nearest player, where it leaves and comes back); `=2`
also, once a second, where each of its script's threads stands (its next code word, as `tools/research/kh2_ai.py`
lists the script), its motion, whether it jumps, its height over the ground and its fall, and each wall that stops
its steering; `=3` also every call its script makes but those made every tick, with its arguments.

## Bosses

The first boss, the maintainer's choice (2026-11-08): **Cerberus**, `B_HE020` (the Olympus Coliseum's; object 351,
kind 0x0c, a large boss, in `00objentry.bin`). The proposals below were approved ("tal cual en kh2 como todos los
enemigos"); what is built is under "Played" at the end of this section.

- **Its files**: `obj/B_HE020.mdlx` (its model, textures, script and collision -- one object, no parts of its own),
  `.mset`, `.a.us` (its effects and sounds). `B_HE020_GM`, `H_HE020` and the `F_HE020_*` objects are the coliseum's
  event pieces, not the fight's.
- **Its script**: 36 states -- `appear`, `battle_start`, `mode_battle` / `mode_battle_boss` (picking `low_attack`,
  `high_attack`, `low_short`, `high_short` or `long` by the player's distance and height), `bite_select`,
  `turn_select`, `back_turn`, `atk_darkbreath`, `atk_bite_press` and its hit, `self_stun`, `move_target`, `jump`,
  `jump_land`, its revenge (`revenge`, `mode_revenge`, `mode_revenge_boss`), six reaction commands' states
  (`rc_avoid`, `rc_avoid1`, `rc_avoid2`, `rc_miss_avoid1`, `rc_miss_avoid2`, `rc_miss_stun`), `dead`, `leave`,
  `event`, `freeze`, `idle`, `idle_time`, `footwork`. It makes 68 calls; the port answers 56. The other 12:

  | Call | Read (KH2FM.exe) |
  | --- | --- |
  | 0:1 | nothing (`FUN_14041ca00`, empty in the PC game) |
  | 0:32 | an effect of the screen or of time for n frames (`FUN_140156c90` on `FUN_140156dc0`'s manager: + 0x100, + 0x104, + 0x108; KH2's hit starts it for 10 frames when the player's 200-kind attacker hits) -- **what it shows is not known** |
  | 1:107 | the actor's + 0x2a8 set |
  | 1:130 | two objects' places in KH2's list told apart (their + 4s' difference) |
  | 1:148 | the actor put at a point (`FUN_1403b64d0`: + 0x670, then `FUN_1403ccb90`) |
  | 1:157 | an event sequence started by a name of up to four letters from the actor's entries of type 0x16 (`FUN_1403db2f0`: `+ 0x928`'s, made an object of kind 0xb on the actor's + 0x80), its camera's target when given ("Event sequences" below) |
  | 1:213 | its character's number (`FUN_1403b5b20`: + 8's record's + 0x4c, 00objentry.bin's: Sora 1, Roxas 14, Donald 2, Goofy 3; enemies 1000 and up) |
  | 1:220 | a step ahead with the room's collision, up to a length (`FUN_1403b9ac0`: its collision swept in 100s, `FUN_1403b6510` puts it where it stops) |
  | 1:225 | whether its class lets hits through now (+ 0x6c8 bit 1, then its class's virtual + 0xb8: the check KH2's hit makes, `FUN_140410d60`) |
  | 1:228, 1:229 | a 12-byte record's first, second word (as 1:230's) |
  | 2:25 | a record's + 0x18 |

- **Its body and heads**: 97 collision shapes. Its body is a cylinder 6 m high and 5.5 m round (kind 0x300), with
  spheres on its legs, necks and heads (0x301, 0x302). Its attacks are groups 1, 2 and 3 (each head's bite), 5, 12,
  13 (its paws) and 7 (`atk_darkbreath`: a sphere of 2.4 m). Groups 9, 10 and 11 are its three heads, each with an
  aim point (0x307, 4 m up and 3 m ahead): KH2 locks on each head apart.
- **Its health**: its script picks its `enmp` entry with `2:9` -- 19 (1000 health), or 199 (1500) when its battle
  number 1 is 1 (the cup's) -- and reads its share left from the record `2:9` makes (`1:84`, `1:228` / `1:229`:
  `hp_rate`, `AngryRate`). Entries with several health values exist (ids 25, 114, 138, 172, 200, 244: 2 to 6).
- **Where KH2 places it**: `ard/us/he07.ard` at (-611, 236, -731) -- the Underworld's entrance, its floor 2.36 m
  under KH2's origin -- and `he09.ard` at (0, 0, -700) (the cup's arena, its floor at the origin). `mode_battle`
  picks `high_attack` when the ground under the player is above 2 m under the origin (KH2's y less than 200), else
  `low_attack`: on a floor at Days's height (KH2's 0), the cup's `high_attack`, as in its arena.
- **Reaction commands, how KH2 starts them**: its trigger 15 is "an attack of mine landed" (`FUN_1403d1730`, a hit
  applied, calls the attacker's class virtual + 0xe0, `FUN_1403d5500`, which raises it with the one hit and the
  attack; the script reads the attack first). Its bite (`atk_bite_press`, motion 221 `60A`, attack range 10 with atkp
  entry 359) landing on the player (`1:130`: the one hit is the player; `1:225` 0; `1:213` 1, KH2's player's
  character number, Sora's) enters `atk_bite_press_hit`, then `rc_avoid`: Cerberus pins the player. The bite's spheres
  stay 2.8 to 3.5 m up through it (frames 64 to 74): it catches a player in the air at its heads.
- **Reaction commands, how they play**: `rc_avoid` plays Cerberus's motion 222 and starts a routine of its script on
  the player (`1:112`, its code word 7435: a thread the research tool lists since it follows `1:112` and `2:20`). The
  routine puts the player at Cerberus's place and facing (`1:147`, `1:148`, `1:201`, `1:1`), hooks Cerberus onto the
  player (`1:18`: `FUN_1403d9fe0`, + 0x6a0 its carrier, bone 0x4002, its class 3), then plays three steps: each makes
  the player play a motion of Cerberus's set made for KH2's player (`1:29`: slots 252-257, entries of type 0x14 whose
  '0010' variant is the player's body, '0011' his keyblade; entries marked as repeats point at the body's data) and
  waits until its work's + 0 is set -- its trigger 11, a message 38, 39 or 41 -- or the player's motion ends or changes
  (`1:14`, `1:238`: + 0x184). The player's motions offer the command (range 20, OpenKH's ANB page: "Reaction Command
  (Self)"): 38 in AM00 from frame 210, 39 in AM01 and AM04 from 160, 41 in AM02 and AM05 from 170, each following the
  one before (03system.bin's `cmd`: 39 requires 38, 41 requires 39; range 26 carries the one followed). Taken in time,
  the step's success (the player AM01 / AM02 / AM03, Cerberus `rc_avoid1`, `rc_avoid2`, then `self_stun`), else its
  miss (AM04 / AM05, `rc_miss_avoid1`, `rc_miss_avoid2`, `rc_miss_stun`). AM03's keyblade ('0011') carries the
  player's blow, attack 598 (power 300). The routine ends with `1:102` (Cerberus off the player) and `1:13` (the
  player's own motion 0, its idle).
- **The commands** (03system.bin's `cmd`, 0x30 bytes each, OpenKH's 03system page): 38 "Evade", 39 "Jump!", 41 "Dog
  Paddle" (their texts 33954, 33953, 33955: sys.bar's messages 1186, 1185, 1187, the top bit cleared). Their flags
  carry "Disable Roxas": KH2 never offers them to its own Roxas. The port offers them to Days's players, who stand
  for KH2's player (`1:213` answers Sora's 1 for them).
- **Cameras**: none in its script. **Its end**: its `dead` state (its effect and fade), as the other enemies.

**Proposals** (for the maintainer; nothing done):

1. **Size**: its stand-in's collision sphere (a Days class's, under a metre) is far smaller than its body. The shell
   would give the Days enemy KH2's body cylinder for its radius, so Roxas cannot walk into it and Days's hits land
   on its body, not only near its centre.
2. **Health and its bar**: the stand-in's HP as now (the mission's class, its level), shown on the KH2-style enemy
   HUD (`port/ui/kh2_enemy_hud`) with Cerberus's name; KH2's several bars only when its `enmp` entry is found.
3. **Reaction commands**: as KH2 plays them (below, "A player in an enemy's hands"). Roxas's KH2 model (the HD
   PACK's) plays Cerberus's motions for KH2's player: Roxas's KH2 bones are Sora's, renumbered.
4. **Lock-on**: Days locks on one enemy; its mark at the middle head's aim point (group 10), not at its feet.
5. **0:32**: nothing (its only use is a reaction command's).
6. **The 12 calls** answered as read above (1:157 starting the named effect through the port's effect player).

**Played** (2026-11-08, `kh2:B_HE020x1` at level 99, Roxas attacking where he stands, 200 seconds): it appears, opens
with `battle_start` and its leap, leaves it at Roxas's first hit and fights -- `mode_battle`, `high_attack`,
`atk_bite_press`, `back_turn`, `bite_select`, `atk_darkbreath` (its effect drawn) -- taking Roxas's hits and giving
18. What it needed, all as KH2 does it:

- **Its 12 calls** (above) and **`2:9`'s health record**, Days's health kept in it each frame (`World::health`).
- **Its size**: every KH2 enemy's Days collision sphere is its largest body shape's radius (kind 1: a cylinder on no
  bone -- Sora's 35 cm, the Soldier's 45 -- or spheres on its bones, Cerberus's torso's 2.4 m), sitting on the ground
  (`khdays_kh2_shell_size`, + 0x70 and + 0x64); a kind without one keeps its stand-in's. Kind 0x300 is not the body
  (90 cm round for every small enemy, 5.5 m Cerberus's; Sora has none): sized by it, Cerberus kept Roxas out of its
  paws' reach.
- **The player as KH2's attacks find him**: a body from his feet to 1.45 m (Sora's shapes: his cylinder 85 cm high,
  spheres on his bones up to his head), his Days radius round -- Days's own sphere (half a metre, on the ground) left
  the attacks KH2 aims at a standing Sora's chest and head above Roxas (Cerberus's bites 2.4 m up and more).
- **Its lock-on mark** at its aim point (its first 0x307 shape, 4 m up over its middle): a join in
  `Ov022_UpdateSelectionMarker` (`khdays_kh2_enemies_lock_point`) for every KH2 enemy.
- **The motions' state ranges** as + 0x18c's bits while the motion's time lies in them and it plays (OpenKH's ANB
  page: range 0 bit 2, 1 bit 3, 3 bit 5, 9 bit 9, 24 bit 6, 30 bit 24, ...; a loop always plays, a motion held on its
  last frame sets none -- a landing's range 9 "to its end" held the Fiery Globe for ever): `1:60`'s "off the ground"
  is bits 5 and 24 (`FUN_1403ba9a0`), which its leap's motion 171 sets from frame 92 on; ranges 3 and 24 are "no
  gravity"; bit 9 (range 9, a jump's start and its landing) is what `2:5` and `1:78` wait on.
- **A fall's landing**: a fall from higher than a step (30 cm: its leap, a ledge) ending on the ground plays its
  landing motion (`FUN_1403cb940`: + 0x11c, 5 `LAND`), as KH2's falling class does; that ends the leap's loop. A
  smaller one -- Days's ground found a few centimetres off -- lands without it (with it, every enemy kept landing).
- **The ground**: never into it (KH2's ground class keeps it there).
- **A hit without a reaction motion of its own** (Cerberus has no 10 or 12): KH2's flow all the same -- the queue
  cleared, `revenge` entered, trigger 6 -- with no motion and no hold (`FUN_140411060`: the controller plays nothing
  for a slot its set lacks). That ends its opening leap: its leap's state never ends of itself (it yields forever
  after landing); KH2's own first hit ends it.
- **Its attacks reach Roxas**: 18 hits in the 200 seconds (its paws and bites), with the two above.
- **Its reaction command** (2026-11-09), played with the bite taken as landing (a test switch, not kept) and the
  commands taken: the whole chain both ways -- the hold, its three steps' successes to `self_stun` and the blow (45 of
  Days's damage, the last hit's 15 x 3), and the misses to `rc_miss_stun` -- the prompt above the deck in the game's
  language ("Eludir", "¡Salta!", "Chapoteo").
- **Its reaction command's cameras**: 1:157's event sequences ("00" as it takes the player, "10" to "30" as each
  step begins) play their cameras ("Event sequences" below).
- **Not yet**: its name on the enemy HUD (the stand-in's shows); Days's camera, close behind the player, inside its
  body (6 m) outside the reaction command; its leap's loop (motion 171, from frame 63) never ends without a hit (KH2's
  way out not read); 0:32 (the routine's screen effect) and the sequences' screen effects and vibration (commands
  0x17, 0x18, 0x27) not played.

## A player in an enemy's hands

KH2's player is an actor its enemies' scripts move and make play their motions; Days's players are Days's. When a
script makes one of them play a motion of its set (`1:29`, Cerberus's reaction command), the shell holds it
(`shell.cpp`, "the players in hand") until the script gives it back (`1:13`, its own motion 0) or the enemy goes:

- **Days's side**: a join in `Ov022_ActorUpdate` (`khdays_kh2_enemies_holding`, next to Stop's hold) stops the
  player's Days control (its state machine, its animation, its commands: the pad's top button is the command's), and
  puts its actor where the script and the motion have it (its place, + 0x48c and its node; its facing, the node's +
  0x80: Days's model looks along (sin, 0, cos) of it, as KH2's yaw).
- **The motion**: its '0010' variant's time on (1 tick a frame), its root's travel turned by the yaw moving the player
  (as an enemy's), held at its end (the ended bit `1:14` waits on). Its ranges: 20 a reaction command offered (when
  the command it follows, `require`, is a range 26's), 27 not to be hit (the enemies' attacks pass it). Its triggers:
  1 the enemy's effect on the player's own bones (its element's bone is KH2's player's: P_EX100's skeleton in the pose
  the motion has then, placed and turned where the player stands from the enemy), 8 the enemy's sound. Its weapon's
  ('0011' with data of its own) attack ranges: the player's blow on the enemy, Days's damage the player's last hit on it
  times the attack's power over 100 (00battle.bin's atkp; Stop's way into Days's hit, `khdays_kh2_enemies_blow`).
- **The script's calls on it**: `1:148`, `1:1`, `1:4`, `1:79` put and turn it; `1:112` its thread (kept from before
  the hold begins); `1:18` / `1:102` an enemy carried by it (at its place and facing); `1:74` nothing (its class is
  Days's); `1:213` 1.
- **The command**: offered, it shows above the command deck (the button's icon, the HD PACK's or Kenney's, and its
  KH2 name from the mod's kh2/msg/<language>/sys.bar) and over the enemy (the icon at its aim point); the pad's top
  button (KH2's triangle, the DS X) takes it: the command's argument sent to the enemy as a message (trigger 11).
- **KH2's text** (sys.bar, our reading of the files; OpenKH's MSG page gives the control codes): 01 a space, 2e-47
  A-Z, 9a-b3 a-z, 90-99 the digits, b7-cb the small accented letters (a grave, acute, circumflex, diaeresis ... u
  diaeresis), d0-e4 the capitals, b5 oe, b6 sharp s, e5 and e6 the opening exclamation and question marks, 48 !, 49
  ?, 4f ., 50 ,, 52 :, 5a (, 5b ), 66 -, 2b +, ec and ee the apostrophe, ed the quotes.
- **Its look**: a KH2 model (the HD PACK's Roxas) plays the motion on its own bones (`model_kh2_motions.cpp`,
  `play_held`): from KH2's player's bones (P_EX100, the mod's kh2/obj/) through the model's `skeleton` by their rest
  places (`kh2::match_by_rest`: Roxas's P_EX110 has Sora's bodies' rest places, renumbered), then its `bone` pairs.
  Days's own model holds its last pose (Days has no such animation).
- **The mod's files** (the mod tool copies them from the player's KH2): obj/P_EX100.mdlx, 03system.bin, 00battle.bin,
  msg/<language>/sys.bar.

## Event sequences

An object's `.a.us` holds its event sequences as BAR entries of type 0x16 (OpenKH's "Animation Loader"), named by up to
four letters (Cerberus's "00", "10", "20", "30"); 1:157 starts one by that name (`FUN_1403db2f0` packs the letters, the
first in the low byte, as the entry's name lies). KH2 has one event player (`FUN_1402cc4b0`): a sequence started ends
the one before. Of KH2's objects, 189 sequences, every one with a camera; 76 set their camera from the player's
matrix (command 0x31), 4 call back their object (0x51), the others -- Cerberus's -- neither. Our reading of KH2FM.exe:

- **Commands**: a u16 size, a u16 number, then its own; size 0 ends them. Read before the sequence starts
  (`FUN_1402cc180`): 0 its name at + 10 ("re_b_he020-00"), 0xf the start, 0x10 + 4 a number (`DAT_140b64fbc`), 8 its
  length in frames at + 4, 0x13 a camera. On their frames (`FUN_1402cb5d0`, each with its frame at + 4): 6 a camera's
  span (+ 4 the camera, + 6 its first frame, + 8 its last), 0x17 and 0x18 a screen effect started and ended
  (`FUN_14015f1e0`'s centre at + 0xc and + 0xe halved), 0x27 a vibration (+ 0xe), 0x31 the camera set from the
  player's matrix (+ 6 1, `FUN_1402d27a0`) or given back to the game (+ 6 0: Cerberus's "30" at its last frame), 0x51 a
  call back to its object, 0x3c an effect element on its object (+ 6).
- **A camera** (0x13, `FUN_1402d2620` keeps it by its number at + 4): eight channels, each a u16 first key and an s16
  count at + 6 + 4n, the keys at + 0x28, 16 bytes each: a word (bits 29-31 the interpolation to the next key -- 0 held,
  1 linear, 2 to 4 Hermite; bits 0-28 the time, signed, in 1/512 s), the value, its slope in and out (a second's
  change). The channels: the eye's x, y, z, the place it looks at, the roll (degrees), the vertical field of view
  (degrees: `FUN_14019dde0`'s last argument 1). Cerberus's four hold the same 74 keys a channel, each sequence's time 0
  where its own step begins (keys before it are negative).
- **Played** (`FUN_1402d37b0` each frame): the frame is the sequence's ticks over 2 (30 a second); the cut whose span
  holds it (from its first frame to one past its last) shows its camera at the frame less its start, held at its
  end. A channel (`FUN_1402d4580`, `FUN_1402d42a0`): the time in 1/512 s as a whole number; before the first key and
  after the last, that key's value carried on by its slope (the line to the next key's for a linear one); at a key, its
  value; between, the key's interpolation (Hermite with the first key's slope out and the second's in, in seconds). The
  eye and the look-at turn to KH2's axes (y and z the other way), the roll to radians; then both go through the
  sequence's matrix (`FUN_14013f900`) to `FUN_1401780d0`, KH2's camera set.
- **Where**: the sequence's matrix is its object's + 0x60 (`FUN_1403ad2d0`), the identity (`FUN_140142280`) until the
  call back (`FUN_1403ac630`, command 0x51 alone) copies the target's into it (`FUN_140406a90`: the target's + 0x808, or
  its + 0x40 with its place followed when the sequence's byte + 9 is 2). Cerberus's sequences have no 0x51: KH2 plays
  them in its arena's own place. Read as Cerberus's model's axes, the first shot is 10 m before its heads, 2 m up,
  looking at its body with the player in its mouth between.
- **The port** (`assets/src/kh2_event.cpp`, `runtime/kh2_enemies/shell.cpp`): Days's rooms are not KH2's arena, so the
  sequence's place and turn are its target's as it starts (the call back's binding; the actor itself without a target),
  its keys turned into Days's world as an actor's own axes are. It moves on with its enemy's KH2 ticks and ends at its
  length or with its enemy. Its camera replaces the frame's (`runtime/camera/kh2_camera_hooks.c`, the end of each
  camera frame and each commit, whichever camera the field has) for the player it is about -- the one the enemy holds,
  or its attack last landed on -- with KH2's event cameras' near plane; the field of view the camera had goes back
  after, and the KH2 camera takes up again as after Days's camera's frames. The roll is not drawn (Cerberus's are 0),
  nor commands 0x17, 0x18 and 0x27, nor 0x31's camera from the player's matrix (told in the trace when a sequence
  starts). Played with Cerberus (2026-11-09): the four shots in turn, the HD PACK's KH2 Roxas in its mouth, in the air
  for "Jump!" and on its heads for the blow; a room smaller than KH2's arena leaves some shots outside its walls (KH2's
  event cameras meet no collision). Days's own Roxas model plays none of KH2's motion: it stays at the hold's place,
  Cerberus's middle, inside its body.

## Triggers

| Trigger | Read |
| --- | --- |
| 0, 1 | start (once, with the object's address) and main (its own thread, every tick) |
| 6 | **a hit taken** (`FUN_140410d60`: after the current state's hit routine, before the native reaction), its argument the hit; also sent to every one of 8 listening threads (`FUN_1403f47b0`). The enemies turn to their revenge mode on it ("Revenge" above) |
| 11 | a message from another object: raised by the call `FUN_14042c0d0` (one script to another object: its sender, a number, a target) and two native callers |
| 13, 14 | `FUN_1403b7920`, `FUN_1403b79d0`: no direct reference in the executable (called through something built at run time). 13 clears the actor's flag 0x4000000, leaves the current state, runs trigger 13, then the class's virtual + 0x28 and the object's + 0x50. The Shadow's 13 plays motion 4 (`FALL` in KH2's motion list) and sets 0x4000000 again: most likely the actor leaving the ground. **Not confirmed** |
| 10 | `FUN_1403b3030` (a pointer in an `.rdata` table): **not known** |
| 15 | **an attack of its own landed** (`FUN_1403d1730` -> the attacker's class virtual + 0xe0, `FUN_1403d5500`): the one hit and the attack, the attack read first (its + 0x18 its atkp entry, `2:25`); Cerberus's pinning bite begins its reaction command on it. The shell raises it as a KH2 attack's hit lands on a Days player |
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
20-byte records. A shape on no bone is in the actor's own axes, KH2's world's (y down): the model's turned half round
its x, its y and z the other way -- the Rabid Dog's rush sphere (kind 6, group 2) at (0, -55, -170) is 55 up and 170
ahead, every kind's aim point (0x307) above it (-50 the Rabid Dog, -80 the Soldier, -140 the Wight Knight). Read from the Soldier's 17 and the Shadow's 10: a group (byte 0), a kind word (+4: 0x300 the body's
cylinder, 0x302 a sphere on a bone, 0x306 an attack's sphere, 5 a large box), a bone (+6; 0x4000 none), an offset x, y,
z, then sizes (height, radius). The Soldier's body is group 0 (a cylinder 115 high, 90 round; spheres on bones 2, 28,
31); its attacks are groups 1, 2, 3, 4 and 7 (spheres of radius 40 to 110 on bones 88, 63, 2); group 6 is five boxes
700 by 300 round it (**not known**: likely the area its attacks are tried in). A motion's trigger 10 names the group an
attack uses: the port tests the group's attack spheres (kind 0x306) on their bones, in the pose the motion has
that tick, against Days's players' collision spheres. The record's other bytes are **not known**.

## Effects

An object's effects are its `.a.us`'s PAX (its BAR entry 0x12; docs/FORMATS.md, "KH2's effects"). Its 0x82
directory's 32-byte entries are `{u32 its effect's offset from the directory, u32 its emitter, u32 its number}` (the
Soldier's 9 over 4 effects, the Shadow's 3 over 3). What a motion's trigger 1 and the script's `1:87` start is an
**element**: the PAX's records before the directory (0x50 bytes each from + 0x10, their count at + 8; the Soldier's
11, the Shadow's 3), as `FUN_1402c6b30` reads them:

| Offset | Read |
| --- | --- |
| + 0 | u16 its effect: the directory's entry of that number |
| + 2 | u16 its number (what trigger 1 and `1:87` name; the Shadow's are 2, 3 and 4 over its effects 0, 1, 2) |
| + 6 | u16 the bone it follows (`FUN_1403b50f0`: 0x4000 none, the actor itself; bit 15 a bone the class gives) |
| + 0x10 | flags (bit 0 it follows its actor every tick, `FUN_1402c5f70`; 0x10000 two bones' more at + 0x40; others **not known**) |
| + 0x14 | when its sound starts, its effect's ticks (`FUN_1402c58f0`) |
| + 0x18 | its sound: KH2's id, played above 0 at the effect's place and moved with it (`FUN_1401de150`) |
| + 0x1c, + 0x28, + 0x34 | its place, turn (radians) and size: KH2's axes (y down), centimetres |

The port runs each element started on the enemy (`runtime/mods/kh2_fx_draw.h`, the Dark Corridor's way: the effect
in KH2's axes and centimetres, its view the enemy's) at its size, turn (x, then y, then z: the order **not known**,
the Soldier's and the Shadow's turn about one axis only) and place, on its bone in the pose of the tick it is drawn
at; its sound starts when its time comes. Their pictures are read between their texels whatever Days's texture
setting: KH2 does not draw its effects with nearest texels, and the DS look's made a spark a block of pixels. **Not
known**: + 8 and + 0xc (the Soldier's elements 3, 4 and 7: 1 and 1, 2, 4), and the two values `1:87` passes on.

## Sounds

An `.a.us` holds a PS2 wave bank (`wave`, `WD`), a small `se20` entry `ORIGIN` naming its bank (`0xc8` = 200 for the
Soldier, 201 the Shadow), how many sounds it has (25, 22) and the PC's bank (`se/obj/se200`: the remaster's
`remastered/obj/<name>.a.us/se/obj/se200`, an SCD of 13 recordings for the Soldier, docs/FORMATS.md "KH2's sound
banks").

- **A sound's id** is `bank x 1000 + number` (`FUN_1401de150` splits it: the bank the id / 1000, looked up among
  those loaded, the number the id % 1000). Bank 0 is KH2's common one (`se/se000.win32.scd`, 314 sounds: the
  Heartless's appearing 105, leaving 106 and dying 109 are there); 200 and 201 the Soldier's and the Shadow's own.
- **The bank's sound** of that number is the sounds' table's entry of that place (the corridor's sound 10 is entry
  10, its recording 9), which names its tracks' recordings and gives its volume (docs/FORMATS.md). The Soldier's 25
  sounds play 13 recordings: 0-3 and 16-24 one each (16 -> 4 ... 24 -> 12), 4-15 empty.
- **What plays them**: a motion's trigger 8 `(bank, number, 0, 0)` -- the Soldier's 200021 in two of its damage motions
  (`31A`, `33A`) and 200022 appearing (`80A`), the Shadow's 201021 and 201020 -- and an effect's element, its own
  (+ 0x18): the Soldier's appearing 105 (element 0), leaving 106 (1) and end 109 (2), its kick's 200020 (`52A`
  starts element 6) and `53A`'s 200017 (element 5), and 200016, 200018, 200019, 200023 (elements no motion of its
  starts: **not known** what does); the Shadow's end 201019 and 201016, 201017.
- **The mod** has each as `kh2/se/<id>.wav` (`tools/mods/kh2_enemies.py`: the first track's recording, at the
  sound's volume); the port hears it among the effects (their volume) where the enemy was last drawn, to the left or
  right of the camera (`runtime/sound/sound_shots.h`).
- **The stand-in's own sounds** (its class's `MON_` archive: its hits' voice, its appearing and defeat, and the
  enemies' common `COMMON_SYS/se_all_enemy_*`) are not heard at a KH2 enemy's place: the game starts them through
  `Slot_Spawn` there, whose join asks the port (docs/RUNTIME_MAP.md section 37); the players' sounds (their weapons'
  hits) and the rest are heard as ever. **Not known**: what a sound's several tracks mean (one of
  se000's has five recordings: likely a choice; the port plays the first), the tracks' own commands (a pitch range,
  1.06 and 0.944, in most of the Soldier's), how KH2 makes a sound quieter far away, and the sounds 0-3 of the
  Soldier's and the Shadow's banks (no motion or element of theirs names them).

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
  reaction plays (the queue cleared, + 0x18c bit 9 set, `revenge` entered, then trigger 6: "Revenge" above; a kind
  without `revenge` has its script wait) unless a locked state runs; its HP gone,
  the state `dead` is entered (unlocked: the game's own entry). Days defeats it at once and reuses it for the next
  wave: the KH2 enemy's end goes on where it was, drawn as the 3D list closes, until it has faded out.
- **Taken by its wave** (`Ov107_Spawner_FindFreeActor`, joined): a wave's spawner takes an idle actor whose class
  is its row's -- rows of one stand-in class share the spawners' kinds entry (`Ov107_AllocSortedEntry`), so Days alone
  took any of the class's actors, and a defeated Rabid Dog's served the Strafer's row. The join takes only an actor
  made for a row with the same KH2 enemy as the slot's (the slot's owner tag, its spawner's place among the field's
  objects << 8 | its group, names the row: `khdays_enemies_slot_row`).
- **Jumps and falls** end on the ground Days last found under it (+ 0x13c, + 0x140): Days's collision lets a fast
  fall through the floor (the Wight Knight's 5 m leap fell on under the room, its script waiting on the ground).
- **Leaving and coming back**: KH2's enemies enter `leave` when the player is far (their `mode_battle` measures it
  against their handle's + 0x3c on the screen, 12 or 18 m, or + 0x38 off it, 20 or 25 m; `1:56` says which): they
  fade out and wait for KH2's spawner to bring them back. Days's field has none, so once faded out the enemy comes
  back where its wave placed it, its script anew, its Days HP as it was (a Wight Knight's lunge can end 12 m past
  the player).
- **Drawn once a frame**: its stand-in's other model in the same file (`Ms/04.p`'s at + 0xca00) is left out, the
  KH2 model and its effects drawn once.
- **Heard** (`Slot_Spawn`, joined): the sounds Days starts for it of its own -- its class's `MON_` archive, the
  enemies' common appearing and defeat -- are not heard within its body's radius of its place; KH2's own sounds
  play instead (above, "Sounds").
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
