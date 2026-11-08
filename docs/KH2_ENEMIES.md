# KINGDOM HEARTS II's enemies in custom missions

Not the game's: how the port can let a custom mission place KINGDOM HEARTS II's enemies, fighting with their own AI
scripts from the player's copy (KINGDOM HEARTS HD 1.5+2.5 ReMIX on PC). Our own reading of the player's
`KINGDOM HEARTS II FINAL MIX.exe` (KH2FM.exe) and files, and OpenKH's public documentation pages for some layouts; the
script machine and the objects' files are in `docs/FORMATS.md` ("KH2's objects and their AI").

This document is first the research report the work starts from (what is read, what is not known yet), and becomes
the authors' and players' guide once KH2 enemies can be placed.

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

## The Soldier's calls

Grouped by what they touch; `P(x)` turns a script address into the game's pointer (`FUN_1404ad270`); "the actor" is
`P(P(handle) + 4)`. **Not known** marks what is still unread.

| Call | Read |
| --- | --- |
| 0:3 | the tick's length (1.0 at 60 a second) |
| 0:4, 0:5, 0:13, 0:36 | vector operations, NaN components made 0 |
| 0:6, 0:7 | a vector's length; made unit in place, its length given back |
| 0:16, 0:17, 0:18 | random: below n, times f, between a and b (`x = x * 0x10dcd`, 32 bits) |
| 0:47 | memory of n bytes (`FUN_140152430`: the game's allocator, `DAT_1409ba920`'s +8) |
| 0:79 | memory given back four calls later (a ring of four, `FUN_1403f63a0`) |
| 1:3 | the player's actor (`DAT_142a105d0`) |
| 1:4, 1:79 | face a direction (yaw = `atan2(x, z)` at actor + 0x7a8; 1:79 also the direction at + 0x680) |
| 1:5, 1:6, 1:7 | a state table cleared, a state registered, the table and handle given to the state machine (actor + 0x390) |
| 1:8, 1:9, 1:10, 1:127 | enter a state, queue one (16 at most), whether one is queued or running, clear the queue |
| 1:11, 1:61 | play motion `id` blending over `blend` frames (`FUN_1403c86a0` on the motion controller at actor + 0x158) |
| 1:12 | play a motion by another route (`FUN_1403c6dc0`: looked up in the motion set or the object's own BAR): **not known** how it differs |
| 1:55, 1:168 | a jump toward a point (`FUN_1403cbb00`: the speed up to reach its height, a negative y being up), then a motion; 1:168 also marks the actor (+0x648 = 3); a flag sets two follow-on motions (+0x118, +0x11c) |
| 1:67, 1:68 | a motion's frame and length (virtuals +8 and +0x30 of the motion's object): **not known** in which units |
| 1:14, 1:15, 1:78, 2:5 | status bits of the actor (+0x18c bit 1, an int at + 0x180, + 0x18c bit 9) |
| 1:56 | + 0x9b8 bit 10, else (+ 0x9b8 >> 18) << 8: **not known** |
| 1:57 | the object's class byte + 0x0c bit 0 |
| 1:60 | can act: not class bit 0, not + 0x18c bits 0x1000020, not + 0x120 bit 23, and active (`FUN_1403ba720`: + 0x120 without 0x10080000 and the field's `FUN_1403bfb70`) |
| 1:65, 1:70 | set flag 0x4000000 at + 0x120; set any bit there |
| 1:80 | whether a direction is within an angle of the facing |
| 1:87 | an effect started on the actor (+ 0x80, `FUN_1402c6b30`) |
| 1:89, 1:111 | a direction (made unit) and a speed: at + 0xa58 / + 0xa64, and + 0x10 / + 0x1c |
| 1:92 | a ray through the room's collision from the actor's place (`FUN_14040c2b0(..., 6)`) |
| 1:110 | the battle record's byte + 0x1c, -1 without one |
| 1:119 | `FUN_1403b7f10`: a way from the actor's place toward a point at a height: **not known** what it gives back |
| 1:120 | the target's place (`FUN_1403be0f0`) |
| 1:121 | pick a target by mode: 0 the player unless the actor's flag 0x800 at + 0x120 or a lock; 1 among the party: **not known** in full |
| 1:124 | the object's entry flag (+ 8's + 0x48 bit 3) |
| 1:128 | the object class's virtual + 0x20: **not known** (the class's code) |
| 1:131 | a target slot given back (a pool at `DAT_142a163f0`) |
| 1:147, 1:201 | the actor's place (+ 0x670, or + 0x70 while it rides something) and facing as a direction |
| 1:19, 1:20, 1:28 | a value going from 1 to 0, 0 to 1 over a time, or held (+ 0xa08): its fading |
| 1:23 | `DAT_140717008`, written by `FUN_140152be0` alone: **not known** what it holds (the script tests "== 10") |
| 1:34 | `DAT_142a11478 != 0` (written by `FUN_1403ac000` and three more): **not known** |
| 1:301 | `DAT_142a16328` (written by `FUN_1403bb600` and two more): **not known** |
| 1:171, 1:206 | bits 2 and 7 of `DAT_142a11400` (the field's flags; written by several): **not known** |
| 2:8 | whether an object's kind (+ 0x26 byte) falls in the groups 3 or 4 (`FUN_1403ce980`) |
| 2:9 | the actor's + 0x5c0 made from number n (`FUN_140411350` -> `FUN_1403c0280`): **not known** what it is |
| 2:20 | a routine of the running script started as a thread of its own (`FUN_1403c5ec0` on the running thread, `DAT_142a25048`) |
| 2:23 | the actor's target slot (+ 0xbf8) made "this actor's" (type 2) when free |

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
target). The port reads the motion data already (`assets/src/kh2_motion.cpp`); the triggers' reading comes with US1.

## Collision

An object's `.mdlx` holds its collision shapes in its BAR entry of type 0x17: a 64-byte header (its count first) and
20-byte records. Read from the Soldier's 17 and the Shadow's 10: a group (byte 0), a kind word (+4: 0x300 the body's
cylinder, 0x302 a sphere on a bone, 0x306 an attack's sphere, 5 a large box), a bone (+6; 0x4000 none), an offset x, y,
z, then sizes (height, radius). The Soldier's body is group 0 (a cylinder 115 high, 90 round; spheres on bones 2, 28,
31); its attacks are groups 1, 2, 3, 4 and 7 (spheres of radius 40 to 110 on bones 88, 63, 2); group 6 is five boxes
700 by 300 round it (**not known**: likely the area its attacks are tried in). A motion's trigger 10 names the group an
attack uses. The record's other bytes are **not known**.

## Days's side: the shell

Read in the decompilation and `docs/RUNTIME_MAP.md` section 22. A Days enemy is an ov107 actor: its class registered
with `Ov107_RegisterHandler` (the Soldier: class 0x04, `Ov120_CreateNamedEntity`), its callbacks installed at its
start (`Ov120_InitializeActor`: + 0x1c its events, + 0x1d0 a hit, + 0x34 its attached pose, ...), its place at + 0x74,
its HP at + 0x218 / + 0x21a. **Each frame** it runs `Ov107_ProcessObjectTick(self, delta)`, which the port's Stop magic
already joins to hold an enemy as a frozen one (its tasks given no time, no motion): the same join holds a KH2 enemy's
shell while the port moves it. A hit comes through `Ov107_AiState_ApplyHit` (joined by Stop too), the defeat through
`Ov107_AiState_OnDefeat`. **Not known**: how its model is hidden while the KH2 model is drawn in its place (its model
subitem, `CreateSubitemInstance0xB4`, is registered in a list by `RegisterSubscriberSlot`); whether writing its place at
+ 0x74 keeps the room's collision on it.

## The scripts run in the port

The port's machine (`assets/src/kh2_ai.cpp`) and the KH2 actor with its state machine
(`port/runtime/kh2_enemies/actor.cpp`) run the Soldier's and the Shadow's scripts headless on a made-up actor, the
calls' shapes read from the player's executable (`tools/research/kh2_ai.py --exe-calls`: 869 calls in the tables),
the 25 calls the port answers so far handled and the others giving back 0:

    khdays-native --kh2-ai-run obj/M_EX010.mdlx calls.txt 600

- The Soldier: `appear`, then `mode_battle` -> `btl_attack` -> `atk_craw`, `move_roll` or `move_back`, again and
  again for 600 ticks; 43 different calls made; no thread stopped (no runaway, no stack overrun).
- The Shadow: `appear_shadow` and its battle states the same way, 38 calls.
- **Neither script reads or writes its actor itself**: every offset of the actor they use goes through a call. The
  concern that scripts read KH2's actor table directly falls, for these two, entirely on the calls the port answers.
- `--coverage` with the port's list (`khdays-native --kh2-ai-calls`): the Soldier 25 of 62 handled, the Shadow 25 of
  71; the rest are US1's.

## Go / no-go

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
