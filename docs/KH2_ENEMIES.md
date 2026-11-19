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
  `M_EX510`, the Strafer `M_EX610`, the Crimson Jazz `M_EX130`, the Wight Knight `M_EX200`, the Neoshadow
  `M_EX420`, the Hammer Frame `M_EX560`, the Assault Rider `M_EX570`, the Nightwalker `M_EX580`, the Magnum Loader
  `M_EX600`, the Hot Rod `M_EX640`, the Cannon Gun `M_EX650`, the Rapid Thruster `M_EX660`, the Armored Knight
  `M_EX760`, the Creeper `M_EX880` and the Dusk `M_EX990`, all covered; "Coverage" below). Nothing of the games is
  in the repository or the installer. An object with no model of its own (its `.mdlx` holds no BAR entry 4: the
  Thresholder's Possessor `B_BB130`, drawn by its effects alone) needs its `.mdlx` only, its `.mset` and `.a.us` taken
  when it has them; a sound whose bank the kind's own `.a.us` lacks is taken from another object's that carries it
  (the Sorcerer's cubes `M_EX870` play bank 272, the Sorcerer's `.a.us`'s).
- **A mod of one's own** (the maintainer, 2026-11-14: mods carry the enemies they use, as room mods carry maps; each
  mod's author answers for what it carries): any mod may hold a `kh2_enemies.txt` and the files of its kinds under its
  own `kh2/`, made with `--only` and the mod's folder:

      python tools/mods/kh2_enemies.py --kh2 <kh2.hed_out> --exe khdays-native.exe \
          --kh2-exe "<1.5+2.5>/KINGDOM HEARTS II FINAL MIX.exe" --mod "My mission" \
          --only B_EX390 W_EX010_ROXAS_LIGHT W_EX010_ROXAS_DARK

  `--only` makes exactly the objects named (any of KH2's `obj/`, in the tool's list or not) and the files every kind
  needs -- the calls' shapes, `00battle.bin`, `00objentry.bin`, `03system.bin`, the names (`msg/`), the command menu
  and marks (`field2d/`), KH2's font, the resident effect pictures, the player's skeleton and keyblade; an object one
  of them makes (1:0) or KH2 places with it that was not named is told, not added (Roxas's keyblades for `B_EX390`).
  The port reads every started mod's list in load order (a kind in two, the first one's; `Mod` in `shell.cpp`), each
  kind's own files (`obj/`, `fx/`, its voices) in its own mod, and the files they share from the first mod that has
  them (`kh2_file`: the VFS's order), the installer's "Days - KH2 Enemies" or another. Roxas with his keyblades so:
  37 MB.
- **No setting**: a mission that places a KH2 enemy gets KH2's enemy, as its author wrote it. Only a player without
  the mod (or with one that does not cover the kind) gets the class written after the name in its place; without a
  class the mission is left out for that player.
- **In a mission** (`missions.txt`, docs/STUDIO.md): `wave <n> = kh2:M_EX010/4x3 at <x> <y> <z>` puts three KH2
  Soldiers in Days Soldiers' places (class 4 standing in); `kh2:M_EX020/0x2` two KH2 Shadows in Days Shadows'. Without
  a class (`kh2:M_EX010x3`) the stand-in is Days's Soldier, and the mission needs KH2's enemies: it is left out without
  them. The stand-in's class counts for the mission's goals (`target = <class>`, its hearts), its level, drops and
  rewards; its HP is the KH2 enemy's own in Days's scale ("Health" below).
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
routine run at once when it is left (+0xa, `FUN_1403caec0`: also when its routine ends by itself -- the state
machine's tick, `FUN_1403ca550`, runs the routine's thread and leaves the state once it is over; until 2026-11-11 the
port left such a state without it, and the Sorcerer's cubes, whose shield's leave routine clears their "busy", never
took another order), one run at once once it is entered (+0x16,
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

**A hit: damage, not reflect.** KH2 has two paths for an attack reaching an enemy (the PS3 build's, read
2026-11-16). Its **damage** (`YS::ENEMY::_OVR_exec_damage` -> `YS::PIERROT::_OVR_exec_damage` ->
`YS::BTLOBJ::_OVR_exec_damage_reaction`): the enemy's **karma** (+ 0xaf0 in the PS3 build) takes the attack's
RevengeDamage (atkp + 0x2a: Sora's small hits 10, his finishers 20, Riku's 0), its health falls (`add_damage`), its
damage patterns change the reaction (`PATTERN::Convert`), the current state's + 0x10 routine runs (`ACT::damage`,
callback 3), trigger 6 fires with the hit, and the reaction's motion starts by its kind (1 hitback, 2 a small one on
the spot, 3 a small one -- ground or air --, 4 a blow, 5 a tornado, 0xe a large one), its idle pushed after it
(`MOTION::push(0)`). Nothing clears the state queue, nothing enters a state. Its **reflect**
(`PIERROT::_OVR_exec_reflect`: the PC's `FUN_140411060`) is when its own attack is turned back: the state's + 0x18
routine (`ACT::reflect`, callback 7), trigger 19, the attack's reflected motion (atkp + 0x15) with `ACT::clear`, then
the state the attack names (atkp + 0x18). Until 2026-11-16 the port ran the reflect path on every Days hit: each
reacting hit cleared the queue and entered `revenge` at once (Final Xemnas, flinching under Riku's blows, never left
his `revenge`).

**Revenge.** A damage motion's trigger 28 (`ANMATR::ExecTriggerCallback`'s 0x1c; Final Xemnas's 10 to 24 at their
frame 20) asks for `revenge` (`ACT::request`: entered at once when the script has it) once the karma has reached its
limit (+ 0xaf4: 100, from `YS::ENEMY`'s constructor, which also sets the + 0x588 bit 4 the trigger tests; 2:76 sets
another -- Final Xemnas 75). The karma falls by the frame time every tick the state can act (`ENEMY::_OVR_pre_exec`,
`ACT::can_control`). While a damage motion plays (`ACT::is_damage_motion`: a motion 10 to 39, or a reflect), a state
under priority 300 skips its ticks (`ACT::exec`) unless its flag 1 lets it run on; `revenge` is 300. So an enemy takes
a run of flinches and then breaks out of them, as KH2's do. Every enemy script's trigger 6 asks `1:309` of `2:88` --
+ 0x6c8 bit 7 of the last to hit it, set only by the player's maker (`FUN_1403a7a40`): struck by the player, it enters
`mode_revenge`, otherwise `mode_battle`. `mode_revenge` (a state that waits for the reaction to end) picks its
revenge moves: the Rabid Dog's `rvg_short` (`move_counter`, then `atk_rush`) or `rvg_long` (`atk_dive`) by the
player's distance against its handle's + 0x34 (450). `revenge` itself picks `rst_normal`, `rst_hard` or
`rst_super_hard` by its handle's + 0x18; the Rabid Dog registers none of them, and KH2's lookup (`FUN_1403ca700`) then
enters nothing. The port: Days's hit adds its karma (10, as KH2's player's small hit; a KH2 blow its atkps'), runs the
state's + 0x10 routine and trigger 6 (the hit's attack, its + 0xc and its owner -- 2:24 -- the player who struck),
plays the reaction motion with the idle after it, holds the state while it plays, and enters `revenge` from trigger
28; Days's players carry the player's bit.

## The Soldier's and the Shadow's calls

Grouped by what they touch; `P(x)` turns a script address into the game's pointer (`FUN_1404ad270`); "the actor" is
`P(P(handle) + 4)`. What the port answers is in `port/runtime/kh2_enemies/calls.cpp`; where KH2 reaches something
Days's field does not have (KH2's maps' paths, its events, its battle records), the port gives the plain answer the
scripts take as "nothing there", named in the table.

The calls' names (`trap_obj_jump`, `method_obj_disable_collision` ...) and the structures' (`YS::OBJ`, `YS::DAMAGE`,
`YS::SHEET`) are those of KH2's PS3 build (HD 2.5 ReMIX), whose executable keeps its symbols and its debugging
information: its eleven tables of calls (an array of them at 0x7febac; kinds 3 and 9 filled while it runs, kind 9's at
0x7fb418), 8 bytes an entry -- a function's descriptor, then its count of arguments with 0x40000000 when it gives a
value back -- agree with KH2FM.exe's entry for entry (828 of 828: the same arguments, the same values back), so a
call's number names the same function in both. Its structures' members come in the PC's order, but the PC's offsets
move where its pointers grew (`YS::OBJ`'s `Pos` is at + 0x540 on the PS3, + 0x670 on the PC); those of records whose
pointers the PC keeps as 32-bit handles (`YS::DAMAGE`) are the same.

| Call | Read | The port |
| --- | --- | --- |
| 0:3 | the tick's length (1.0 at 60 a second) | 1.0 |
| 0:4, 0:5, 0:36 | a + b, a - b, a x f into the call's own vector, NaN components made 0 | as KH2 |
| 0:13 | a vector turned about the up by an angle (`FUN_1403b7680`) | as KH2 |
| 0:6, 0:7 | a vector's length; made unit in place, its length given back and kept as its w (`FUN_14041cfa0`: + 0xc) | as KH2 |
| 0:16, 0:17, 0:18 | random: below n, times f, between a and b (`x = x * 0x10dcd`, 32 bits) | as KH2 |
| 0:47 | memory of n bytes (`FUN_140152430`: the game's allocator, `DAT_1409ba920`'s +8) | the scripts' memory |
| 0:79 | memory given back four calls later (a ring of four, `FUN_1403f63a0`) | as KH2 |
| 1:3 | the player's actor (`DAT_142a105d0`) | the nearest of Days's players |
| 1:4, 1:79 | face a direction (yaw = `atan2(x, z)` at + 0x7a8; 1:79 also the facing at + 0x680, its w the yaw) | as KH2 |
| 1:80 | turn toward a direction by a step at most (the step x 2.0, `DAT_1405b18ec`; 0 the actor's + 0x134): 1 while it turns, 0 once it faces it (`FUN_1403b9f70`) | as KH2 |
| 1:5, 1:6, 1:7 | a state table cleared, a state registered, the table and handle given to the state machine (+ 0x390) | as KH2 |
| 1:8, 1:9, 1:10, 1:127 | enter a state, queue one (15 at most), whether one is queued or running, clear the queue | as KH2 |
| 1:11, 1:61 | play motion `id` blended over `blend` frames, 1:61 from a frame (`FUN_1403c86a0` on the motion controller at + 0x158): above 2 the motion stands at that frame while the blend runs ("When single-frame triggers play", below) | the motion from the mod's .mset, blended as KH2 |
| 1:12 | the same, left as it is when it already plays (`FUN_1403c6dc0`): the scripts call it every tick while they walk | as KH2 |
| 1:14, 1:15 | + 0x18c bit 1, the motion played to its end; + 0x180, the number of the motion playing: the scripts wait on both. The bit is a tick's: `add_time` (`FUN_1403c6740`) clears its controller's + 0x34 bit 1 first, then sets it when the time reaches the end -- each tick a motion stands at its end, the one tick a loop goes round (Scar counts three rounds of the player pinned under him before he throws him) | as KH2, for an enemy's motions and a held player's |
| 1:67, 1:68 | a motion's length in ticks (the motion controller's virtuals + 8 and + 0x30; -1 the one playing) | as KH2 |
| 1:55, 1:168 | a jump (`method_obj_jump`, `trap_obj_flyjump`; `FUN_1403cbb00`, `FUN_1404233f0`): a vector -- its x, z the way (made one long, then times its w, the speed along), its y how it rises -- then a motion; a flag sets the motions after it (+ 0x118 its fall, + 0x11c its landing, 5 `LAND` by default). A positive y rises that far in the ticks KH2's pull takes to fall it (+ 0xf4 `sqrt(2 y / 0.408163)`, 1:100's), eased (`sin(sin(t / top x pi/2) x pi/2)` of it, + 0xfc), then falls under the pull (0.408163 cm a tick a tick, some 14.7 m/s2); a negative y does not rise: its time begins at those ticks (+ 0xf0) from y (+ 0xf8), so it drops. KH2 leaves the ground at the motion's take-off event (`FUN_14040dd70` event 0: + 0x648 1 makes it class 1); in the air class 1's movement (`FUN_1403cb360`) gives the height by the jump's time and keeps 0.9 of its velocity an evaluation when steered, 0.95 unsteered | as KH2, from the motion's trigger 0 when it has one ahead, else from the call. Until 2026-11-10 the port read `sqrt(2 y / 0.408163)` as the speed up, under a pull of 1 / 0.408163: its jumps rose as high in 2.45 times less time, and the Mole Driller's drill (201: off at 14, striking from 39 to 44) landed before it struck |
| 1:19, 1:20, 1:28 | its fading (+ 0xa08 and its change a tick, `FUN_1403b7810`; a time of 2 or less is 1): from 1 to 0, from 0 to 1; 1:28 to 0 at once and held (+ 0x9b8 bit 4) | drawn as the model's alpha |
| 1:78, 2:5 | + 0x18c bit 9: a reaction holds it (no moving) | set while a Days hit's reaction plays |
| 1:60 | off the ground or held (`FUN_1403ba9a0`: the class's bit 0, + 0x18c & 0x1000020, + 0x120 bit 23; then + 0x18c bit 6) | + 0x120 bit 23 while it jumps or falls |
| 1:65, 1:70, 1:71 | `YS::OBJ`'s `Flag` (+ 0x120, a bit each): 1:65 its fall asked (`trap_obj_exec_fall`, bit 26 `FLAG_EXEC_FALL`); a bit set and cleared (`trap_obj_set_flag`, `trap_obj_reset_flag`) -- the PS3 build's names: 0 `NOHIT_BG`, 1 `NOHIT_OBJ` (touches no object), 2 `HEAVY`, 3 `NOHIT_ATTACK` (no attack lands), 5 `NO_TARGET`, 6 `NO_RC`, 7 `NO_HITBACK`, 10 `GHOST`, 19 `LEAVE`, 20 `BATTLE_MODE`, 23 `JUMPED`, 39 `HIDE`, 40 `HIDE_MODEL`, 41 `HIDE_SHADOW` ... | as KH2; 1 and 3 kept by Days's side too: no push, no touch, no hit (joins in `Ov107_AiState_HitShapeOverlap` and `Ov107_AiState_ApplyHit`) -- the Mole Driller sets both as it sinks into the ground and clears them as it comes out |
| 1:89, 1:111 | 1:89 a move this tick: a direction made unit and a speed (+ 0xa58 / + 0xa64, cleared after each tick by `FUN_1403b8e4d`); 1:111 the steering, kept (`FUN_1403b9da0`: + 0x10 made unit, + 0x1c its speed), which the class's movement makes the velocity (+ 0x20) of every two ticks (every tick past 100): on the ground the steering itself (`FUN_140422350`), in the air approached (`FUN_1403b7ad0`). Speeds are centimetres a 60th of a second | as KH2 |
| 1:147, 1:201 | its place (+ 0x670) and its facing as a direction | as KH2 |
| 1:62, 1:64 | the travel of the tick before (+ 0xa38) and of this one (+ 0xa28) | as KH2 |
| 2:23, 1:121, 1:120, 1:131 | its target slot (+ 0xbf8): made the player's (2) while empty (`FUN_1404319d0`); a target chosen for an owner by a mode (`FUN_1403be2b0`: 2 the player, `DAT_142a105d0` -- what 1:114 finds for 0x236 --; 0 and 1 the player's lock or a friend's target, else the player; 3 to 6 searches from the owner); the target's place; the slot as KH2's pool keeps it | the nearest player for every mode (the owner's searches not read; mode 2 had been read as the owner itself: Demyx, his target 2, stood at no distance from it and never summoned his water clones) |
| 2:88 | the last to hit it (+ 0xd38), the player's actor when none | the player nearest at the hit |
| 1:87 | an effect on the actor (`FUN_1403b6670` -> `FUN_1402c6b30` on + 0x80): the element of that number in its effects, and two values passed on to the effect (**not known**) | that element started: the "dead" state's `1:87(2, 1, 0)` the Soldier's element 2 and the Shadow's (its dying smoke and sound), the leaving's `(1, 1, 0)` element 1 |
| 1:57 | bit 0 of the actor's class's byte + 0x0c (the class at + 0x0c; KH2's classes at 0x140750d30, 16 bytes each: class 1 off the ground -- a jump, `FUN_140409ad0`, or a fall, `FUN_14053b920` -- has it, class 0 on the ground, the landing's `FUN_1403cb940`, not): the scripts wait for it after a jump and act on it in the air (US1 read it as the class's own, always 0: a jump's wait never ended) | whether it is off the ground |
| 1:124, 1:56 | its entry's flag; whether it is culled, off the screen (`method_obj_is_culling` in the PS3 build; `FUN_1403ba4a0`: with + 0x9b8 bit 10, `FUN_1403f3ed0` tests it against the camera's view; 0 without the bit): the enemies leave once the player is farther than a distance, the shorter off the screen | 0; whether Days drew its model neither this frame nor the one before (until 2026-11-13 the port answered the other way round: its enemies left at the shorter distance on the screen) |
| 1:110 | its battle record's byte + 0x1c (`FUN_1403b50d0`), -1 without one | -1 |
| 1:39, 1:218 | its unit's argument n (`trap_obj_unit_arg`: `YS::OBJ`'s `UnitArg`, + 0x9f4 + 4 n, what the room's spawn gives each object; the Shadow tests 99); one set (`trap_obj_set_unit_arg`) | Days's waves give none: 0, then what 1:218 set |
| 1:104, 1:105, 1:309 | + 0x8c8 bit 6 set and cleared; + 0x6c8 bit 7 of an actor: the player (set by its maker, `FUN_1403a7a40`) | as KH2; Days's players have the bit |
| 1:23, 1:301, 1:171 | KH2's field busy: `DAT_140717008` (tested == 10), `FUN_1403bbea0`, `DAT_142a11400`'s bit 2 | 0: Days's field is never busy for its enemies |
| 1:34 | whether an event runs (`trap_event_is_exec` -> `YS::EVENT::IsExec`: `YS::Current` set): an object's camera sequence (1:164, `STDOBJ::start_camera`, makes a `YS::EVENT` of kind 0xb) is one | as KH2: an event sequence playing (Final Xemnas's dome fires after his "60") |
| 1:204, 1:205, 1:206 | the field's "force leave" set, cleared, read (`trap_status_force_leave_start`, `_end`, `trap_status_is_force_leave`: `DAT_142a11400` bit 7) | as KH2, the world's, cleared with the area (Oogie Boogie's) |
| 1:119 | a way toward a point at a height (`FUN_1403b7f10`): 1 or 2 a jump on the way, 3 none | 0: Days's rooms have no KH2 paths; the room's collision keeps the actor out of the walls |
| 1:92 | a point of the ground ahead (a ray, then `FUN_1403dc170` with a number) | nothing |
| 2:8 | whether a hit's reaction kind (+ 0x26, `FUN_1403ce980`'s groups) is past 1: trigger 6's hit, which the Strafer and the cubes turn to their revenge on | as KH2: Days's hits are small reactions (2) |
| 1:128 | the object's class's virtual + 0x20 (its native code) | nothing |
| 1:133, 1:134 | the object hidden, shown (`trap_obj_hide`, `trap_obj_show`: its VTABLE hide sets `FLAG_HIDE_MODEL` and `FLAG_HIDE_SHADOW`, show clears the first) | on a Days player his model hidden and shown (Final Xemnas's grab hides Sora on Riku); on a KH2 actor its model and the effects that fade with it (`Actor::hidden`) |
| 2:9 | the actor's + 0x5c0 made from a number (`FUN_140411350`) | nothing |
| 2:20 | a routine of the script started as a thread of its own (`trap_magic_start_thread`, `FUN_1403c5ec0`: a machine of its own on the script, `FUN_1403e1970`, put in the game's list of them, `FUN_1403c4e90`; run apart from its object's ticks) | a loose thread of its object's machine (`Machine::start_loose`); the shell runs the field's loose threads once a KH2 tick after the enemies' own ticks, the frame's shouts told after the first (the Sorcerer's turn-giving thread moves the turn its order gave on before its cubes hear it). Until 2026-11-11 they ran inside their object's tick, before its state's routine: the turn never came in time |

The calls the other enemies need (US2, "Coverage" below), read the same way:

| Call | Read (KH2FM.exe) | The port |
| --- | --- | --- |
| 0:21 | how far one vector's way along the ground is turned from another's: `atan2(x, z)` of each, the difference into (-pi, pi] (`FUN_14041d1b0`, `FUN_1403b6f20`: pi and 2 pi at 0x140623b78, 0x140623ba4) | as KH2 |
| 0:35 | a vector scaled where it lies, its four components (`FUN_14013fcc0`) | as KH2 |
| 2:67 | KH2's next running number (`FUN_1403cf490`: `DAT_142a24afc`, 0 at the start, 0x100 again past 0x10000000) | as KH2 |
| 1:63 | its motion in its loop with no blend left (`trap_obj_is_motion_in_loop`, `FUN_1403c7e70` on + 0x158: the time + 0x44 against the motion's loop point, the blend + 0x48 run out) -- one that does not loop at its end. Until 2026-11-13 the port read it as the end: a script waiting for a loop (Data Roxas's slide, its 162 looping from frame 6 of 8) waited for ever | as KH2 |
| 1:100 | the ticks a jump takes to rise a height (`trap_obj_jump_height_to_uptime`, `FUN_1403b73f0`): `sqrt(abs(2 h / 0.408163))`, the time KH2's pull (0.408163 cm a tick a tick, `DAT_1405ce184`) takes to fall it | as KH2 |
| 1:156 | a jump of its own (`FUN_1403cbec0`): a vector -- its x, z the way (made one long, times its w), its y how high -- the ticks to its top (its pull + 0x10c `2 y / t^2`, its rise the pull times t), a value at + 0x108, a motion and its blend, and with the flag the two after it as 1:55's. It leaves the ground as class 9 (+ 0x648 2) at the motion's take-off; class 9's movement (`FUN_1403cbdd0`) keeps + 0x108 of its velocity an evaluation, steered or not (the Wight Knight's tear: up 5 m, then steered ahead at 100, or 250 when far, keeping 0.95: a lunge of some 15 m) | as KH2 (the jump's own pull and keep), from the motion's trigger 0 as 1:55's |
| 1:150, 1:151 | bit n - 1 of the actor's bytes + 0x2ac and + 0x2ad (`FUN_14042d1e0`, `FUN_14042d230`): its motion controller's (+ 0x158) + 0x154, the ranges 16 to 19 on now (`FUN_14040d810`, cleared each tick by `FUN_14040e5c0`), and + 0x155, the triggers 9 to 12 its motion passed this tick (`FUN_14040dd70`, cleared each tick by `FUN_1403c6740`) -- the Strafer's ways of attacking; the Magnum Loader's charge waits for its 52A's frame 80, its thread on the player for his AB10's frames 12 and 42 | as KH2: an enemy's own motion's and a held player's |
| 1:94 | an object there and in play (`FUN_14042b0e0`: `FUN_1403ba720`, none of + 0x120's 0x10080000) | as KH2 |
| 1:101 | whether the object an AI handle names plays another's motion (`FUN_14042d360`: + 0x18c bit 14; `FUN_14040d810`'s range 20 then offers that other's command) | a player an enemy holds |
| 1:89 | this tick's move along a way at a speed (`FUN_14042f8d0` -> `FUN_1403b9a70`) | as KH2; on a player an enemy holds, his (the Magnum Loader's Quick Blade dashes him to it) |
| 1:111 | steering along a way at a speed, each tick till another | as KH2; on a held player, his (the Armored Knight's Rising Sun flies him) |
| 1:29 | (its value) the motion given (`FUN_1403c6cc0`), what the player's controller then plays (+ 0x184, 1:238): the Armored Knight's flight runs while the two are the same. The capture is a motion of his own (`YS::MOTION::capture`, `_capture`): his damage reaction's motion takes its place (Vexen's freeze ends with the strike of its shattering ice) | the motion's number; a held player struck by an attack with a reaction (atkp + 10 neither 0 nor 255) is let go (until 2026-11-19 kept: Vexen's freeze held for ever) |
| 0:42, 0:68 | the way a vector points along the ground, `atan2(x, z)` (`FUN_14041d250`); a vector turned about the x (`FUN_14041d100` -> `FUN_1403b75f0`, `FUN_1401411b0`'s turn: y cos - z sin, y sin + z cos) -- Rising Sun's launch, ahead turned up, then about the up (0:13) | as KH2 |
| 6:3, 6:8, 6:5 | KH2's search for an aim (`FUN_14053fb60`, `FUN_140540080`, `FUN_14053fb80`): made (its capacity, a reach of 0 to 1000, no cone), an object's aims listed (its shapes of kind 3 in its groups on), the nearest (or farthest) of KH2's actors' aims from a point within the reach and the cone, passed over when listed and listed once found (FUN_1403bee60's record: the object, its group, none, -1) | as KH2, over the field's KH2 enemies (Days's own have no KH2 shapes) |
| 1:140 | a record names something: its + 8 an object, or its + 0 an actor in play with a shape of kind 3 in its groups on (`FUN_1403bde30`; the PS3 build's `YS::TARGET::is_exist`: `OBJ::is_exist`, `is_enable_collision`, a collision of `TYPE` 3) | as KH2, a player always there (KH2's Sora has his shape of kind 3): until 2026-11-19 a record on the player read as gone, and Vexen's disc, homing on him through one, flew off in a straight line |
| 1:131, 1:132 | a record copied into KH2's pool of records and given back (`FUN_1403bda80`, `FUN_140430420`) | the record itself; nothing |
| 1:191 | the record at an object's + 0xdc8's + 4: the player's aim (his lock-on), which Rising Sun goes for when it names one | a record of the port's, empty: no lock of Days's is passed on |
| 1:221 | whether the player can take a command now (`FUN_1403b6950`, `FUN_1403ba070`: his state against the command's flags) | yes |
| 1:165 | a reaction command offered KH2's player this tick (`FUN_140430010` -> `FUN_1403b2850` on his + 0xdd0): a slot, the command, its source | offered while renewed (Rising Sun's 427, each tick of its descent) |
| 1:149, 0:44 | an effect of the actor's on another object, its handle (`FUN_14042d5c0` -> `FUN_1403b6670`); an effect's loops closed (`trap_effect_loop_end`, `FUN_14041e190` -> `FUN_1402be8e0`: `ryj::EFFECT::loop_end`), so it ends once its objects have | as KH2 (on a held player, on his bones) |
| 2:45 | the number of a hit's attack (`trap_damage_attack_param_id`: `YS::DAMAGE`'s `AttackParam`, + 0x20, its u16 at + 2): the Wight Knight asks it of its hits (492, 1149, 568) | 0: a Days hit names no KH2 attack |
| 1:222, 1:114 | its object entry's number (`trap_obj_get_entry_id`: + 8, `YS::OBJ`'s `Entry`, its record's + 0); the field's first object of an entry's number into an AI handle (`trap_obj_search_by_entry`, `FUN_1403b4110`: 0x236 the player) | as KH2: each kind's `00objentry.bin` record (96 bytes: + 0 its number, + 4 its kind -- 4 an enemy, 3 a boss --, + 8 its name, + 0x28 its motion set's) put at its actors' + 8 |
| 1:155 | health added to a bar (`trap_obj_add_hp`: its object's `add_hp`, `YS::BTLOBJ::_OVR_add_hp` -> `YS::SHEET::_add_hp`: kept between the bar's least and most, the bar's health given back) | as KH2; the first bar's given to Days's enemy (at least 1); a player's to Days's player's HP (`Ov022_ActorSetHp`) -- until 2026-11-19 to the running enemy's |
| 1:167 | a state entered as 1:8 does when its priority (its record's + 4) is at least the current state's (`trap_obj_act_start_pri`: `FUN_1403caa90`; + 0x128 the current one's, which entering sets -- `FUN_1403cad00` --, 0 none): the Dancer's | as KH2 |
| 0:60 | an assertion (`trap_assert`: `FUN_14041e710`, empty in the PC's build): in the Rapid Thruster's and the Armored Knight's states' event routines | nothing, as KH2 |
| 1:0 | an object made by its entry's number (`trap_sysobj_appear`: `FUN_14042b130` -> `FUN_1403df930`, its class by its entry's kind, at the world's origin facing pi -- `DAT_140623df0`, `DAT_140623b78` --, its script started), its AI handle given back: the Sorcerer's cubes `M_EX870` make the Sorcerer (315: their entry's + 0x50 names it) and put it where they stand (1:148, 1:79) | an enemy's entry (kinds 3, 4, 0xc): a Days enemy of the maker's class made where the maker stands (`khdays_enemies_make`); another's: an ally there ("Allies"); its kind's script set going at its first tick, as KH2's (`FUN_1403e1240` runs nothing: what its maker gives it next, 1:218, is there as it starts -- the Hydra's heads' necks); nothing (0) for an entry the mod has no kind for |
| 1:328 | the object counted as defeated, once (`trap_obj_dead_mark`: `FUN_1403d4a40`: unless + 0x9b8 bit 2, its group told -- + 0x9e8, `FUN_1403fed10` -- and the bit set): the cubes in their `leave` (far from the player: counted for their wave, then faded out) | its Days enemy defeated by a hit of all its health (seat 0's player its author: its prizes, its wave's count), no "dead" entered: its script ends it |
| 1:125 | its pictures' fade (`trap_obj_tex_fade_start`, `FUN_1403b7810` on + 0xaa4, `YS::OBJ`'s `TexFade`) | kept, not drawn |
| 1:194, 1:195 | parts of its model in stealth and back (`trap_obj_set_stealth`, `_reset_`: `kn::ModelObj::setStealth` on + 0x7b0's model, a bit a part group; KH2 draws them as a refraction) | the whole model drawn at a quarter of its fade while any part is |
| 1:24 | `DAT_140717009`, the byte after 1:23's (tested against 9) | 0, as 1:23 |
| 1:74 | the actor's class made class 0 (`FUN_1403b4da0`: the old one's virtual + 0x10, the new one's + 8) and + 0xba8 cleared | + 0xba8 cleared: no native classes |
| 1:209 | the class's virtual + 0xb0 unless + 0x9b8 bit 2 (`FUN_1403d3f10`; the PS3 build's `trap_obj_dead`: its death) | a player's: Days's player's HP to 0, his own defeat; an enemy's nothing |
| 1:272 on a player | whether he is dead (`trap_obj_is_dead`) | knocked out: Days's HP 0 |
| 1:278 | n things made at the actor (`FUN_14042a980`: n times `FUN_1403dc950` on the manager at `DAT_142af8b48`) -- **likely its prizes** | nothing: Days gives the stand-in's |
| 0:0, 0:2 | nothing in the PC game (`FUN_14041c9f0`, `FUN_14041ca10` are empty: debugging prints once) | nothing; 0:2's text in the trace (`KHDAYS_TRACE_KH2_ENEMIES`: "avoid start", "high_attack_min") |
| 0:15 | a bit of KH2's story (`FUN_14039a980`: `DAT_1409ab540`'s bits, 0xffff none) | 0: none in Days's field |
| 0:23 | a value without its sign (`FUN_14041ca90`), marked a whole number | as KH2 |
| 0:85 | an effect's loops closed and the effect freed once it has ended (`trap_effect_loop_end_kill`, `FUN_14041e1c0` -> `FUN_1402be8f0`: `ryj::EFFECT::loop_end_kill`) | as 0:44 (Data Roxas closes the glows `1:87` put on him as he rolls or waits for his duel: left going, every roll added to them and the screen went white) |
| 1:2 | steered toward a point along the ground at a speed (`FUN_1403b9c60`: + 0x10, + 0x1c), the way's length back; its first value the actor itself | as KH2 |
| 1:29 | another object plays a motion of this one's set from a frame (`FUN_1403c6cc0`: its set made this one's, + 0x2b8; + 0x180) -- the Magnum Loader's rider, on its message 309. A slot of type 0x14 holds a part per character, named by its object entry's + 7 (its weapon joint: "0010" Sora's body, "0011" his keyblade, "0190" Riku's body, "0191" his weapon) | as KH2, on another of the port's actors: its own part when the slot has one (its own set kept for its own motions, its weapon its "...1"; ended as it plays another), else the set given to it (Final Xemnas's air fight carries Riku in his AQ10 so) |
| 1:58 | flying (`trap_obj_fly`) at a velocity of its own (`FUN_140421ce0`: + 0x20 the vector made one long times its w, + 0x108 a value, class 5, whose movement `FUN_140421c80` keeps + 0x108 of it an evaluation, eased toward the steering) | as KH2 (class 5 ends with 1:74 or a jump) |
| 1:66, 1:69, 1:84, 1:97, 1:179 | + 0x120's 0x8000000 set; the float at + 0x19c; the object at + 0x5c0 (2:9's); where + 0x128 lies; + 0x118 (its fall's motion) set | as KH2 |
| 1:84 on a player, 1:361 | a player's sheet (KH2's player's `YS::SHEET`: + 0 his health's bar, + 0x244 his `PartRam`); a sheet's level (`trap_sheet_level`, `SHEET::get_level`: the PartRam's byte + 0xf, 0 without one) | his sheet made of Days's player's (his HP and most; his level, his member record's, `gPartyMembers` + 2, at most KH2's 99) -- until 2026-11-19 1:84 on a player gave the running enemy's own |
| 1:83 | a point given on a bone (0x4000 the actor itself, in its own axes) in the world (`FUN_1403b50f0`: the bone's matrix, the point through it) | the pose the enemy has now |
| 1:90 | a state's name shouted (`trap_obj_act_shout`): `FUN_1403cacd0` lists it with the shouter, `FUN_1403c9d40` sends the list to every object once a frame -- with a delay each its own time at random below it (+ 0x4c0 .. + 0x4d0, KH2's chance), else at once -- and each runs its own state of that name's shout routine (the state record's + 0x14, event 5; `FUN_1403ca950` through `FUN_1403e2000`), given its own handle and the shouter's; no state is entered | every KH2 enemy of the field, at the next frame's first tick, once the field's loose threads ran (2:20). Until 2026-11-10 the port entered the state at once: the Silver Rock, whose `rc_address_shift_appear` shouts its own name, entered it again and again within one tick and the game stood still |
| 1:98, 1:99 | its place on the ground under it (`FUN_1403b87e0`'s ray down); how far above it it is (negative up, 0 on it or below) | the ground Days last found under it |
| 1:112 | a routine of the script as the actor's attached thread (`FUN_1403e1970`, `FUN_1403d76d0`: + 0xd48, the one before ended), the actor its first value, the third its second whenever it is not 0 (`trap_obj_hook`: `VM::dup` with one value or two) | as KH2 (until 2026-11-16 the second only when it named an object: Final Xemnas's Reflect thread, given a record, stopped at once) |
| 1:140 | whether a target record's object (its + 8) is there (`FUN_1403bde30`; its other test, the player's lock, not read) | its + 8 only |
| 1:143 | an actor found by its placing record's number (`FUN_1403b41e0`, 0x70 for 0) | none: the slot left empty |
| 1:162 | the actor put on one of KH2's lists (`FUN_1403bd410` -> `FUN_14040c9d0`) | nothing |
| 1:211, 1:212, 1:324 | bit n of + 0xd10 set, cleared (`trap_obj_pattern_enable`, `_disable`), cleared (`trap_obj_pattern_reset`, `FUN_14042bab0`) | as KH2 |
| 1:323 | how many of the field's objects are of an entry's number (`trap_obj_count_entry`, `FUN_1403b3e20`) | the KH2 enemies of that entry, and the player for 0x236 (Oogie Boogie counts his Heartless) |
| 1:145 | the room's zone an object stands in (`trap_obj_touch_zone`: + 0xcf4, KH2's area's event zones) | 0: Days's rooms have none (Oogie Boogie's platforms, 122 to 124, are never told) |
| 0:24 | a value printed (`trap_stputi`: empty in the game) | nothing |
| 1:37 | a part of KH2's map shown (`trap_bg_show`: `DAT_140abcbd8`'s bit, the map's and its effects' parts) | nothing: the room is Days's (the Hydra's) |
| 1:335, 1:336 | its bones' occluders off, on (`trap_obj_disable_occ_bone`, `_enable_`: `dk::ObjOcc`'s records at + 0x9c0, + 0x10 bit 1 -- KH2's occlusion culling) | nothing: the port culls nothing so (the Hydra's) |
| 6:1 | a prize made at a point (`Ts::trap_prize_appear`, `FUN_1403dc170`) | nothing: Days gives the stand-in's (the Hydra's heads); Megara's cheering prizes are not made (Days has no prize a script makes at a point yet) |
| 1:32, 1:48, 1:33 | a message window opened (`trap_message_open`, `FUN_14042e5c0`: a message object, `FUN_1403bb270`, its handle back), whether it is over (`method_message_is_end`: its + 0xa0 3), closed (`trap_message_close`) | no KH2 message window in Days: a handle of none, over at once, nothing closed (Phil's talk) |
| 4:0, 4:1, 4:4 | around a talk's message (KH2FM.exe's table has no routine at 4:0 and 4:1; the PS3 build's are unnamed); the mission's information shown (`trap_mission_information`, `FUN_1404334b0`) | nothing: Days's mission shows its own |
| 1:170 | its spawn unit's group (`trap_obj_unit_group`: + 0x9e8's record's + 8's u16 at + 2) | 0: Days's waves give none |
| 1:41 | a signal sent (`trap_signal_call`, `FUN_14042ec90`: the reserved ones of that id let go, the signal queued; the PS3 build's `YS::SIGNAL::Call`) | every script of the field and its mission told at the next frame (trigger 10: the id, its argument; "KH2's missions", below) |
| 1:200 | its collision with the room's kind (`trap_obj_set_bg_collision_type`: + 0x740 bits 3 to 6) | kept there; Days's collision is its own (Phil's urn) |
| 1:17 | it made to leave at once (`trap_obj_leave_force`: its class's + 0x38) | an ally goes ("Allies", below); an enemy's Days enemy stays, its script going on |
| 0:9 | a routine started as a thread beside the one running, with a value (`trap_thread_start`, `FUN_14041da20` -> `FUN_1403e1e30`: in the list of the running thread's parent -- the main's own when the main runs; a loose thread's own --, run after it, `FUN_1403e1c80`; tagged with the first value at + 0x3c, its list's threads of that tag ended first, `FUN_1403e1f00`) | as KH2: a child thread of that parent, tagged (`Thread::tag`); a handle back (Xemnas's) |
| 10:19 | whether the object is a limit's (`trap_sysobj_is_limit`, `FUN_140536890`: + 0x126 bit 0) | as KH2: none of Days's is |
| 4:3, 4:5, 4:6, 4:11, 4:14, 4:15, 4:16, 4:20, 4:21, 4:22, 4:29, 4:41, 4:54, 4:55 | the field's mission: completed, a counter's count set, one more, failed, its pieces shown, hidden, its boss dead, one less, whether its pieces are up, left, a counter's count, so many more, locked, whether it is -- 4:6, 4:20 and 4:29 the counter's index; 4:5 and 4:41 the value, then the index (`YS::MISSION::SetCount(n, index)`, `AddCount(n, index)`: read the other way round until 2026-11-19) (`YS::MISSION`, `MISSION_COUNT`, `MISSION2D`) | the boss's KH2 mission ("KH2's missions", below) |
| 6:16 | a target record emptied (`Ts::trap_target_clear`, `FUN_1403bda50`: none, 0, none, -1) | as KH2 |
| 6:46, 6:47 | a stop put on every object of the field but two (`Ts::trap_obj_rc_stop_all`, `FUN_14053ea60`: each object's stop record at + 0x64c given the stop's number and a value, `FUN_14040a920` -- four slots of a number and a value, their kinds' flags gathered at + 0, from a table, `FUN_1403eed10`, not read); that stop taken off every object (`trap_obj_stop_end_all`, `FUN_14040a770`) | the KH2 enemies and allies but those two held whole, script and motions, while a stop is on them (as Days's Stop holds them; the stop kinds' own flags not read): Demyx's 14 |
| 2:77, 2:78, 2:79, 2:80 | a vacuum made at a point with a strength, its handle back (`trap_vacuum_create`, `FUN_1403f8f20`: 0x34 bytes in the game's list, `DAT_142ae7dc0` -- + 0 the point, + 0x10 the kinds of object it lets be, a bit a kind, + 0x14 its reach, 0 none, + 0x18 its speed, + 0x1c its least and + 0x20 its most, the three the strength, + 0x24 its turning, 0, + 0x28 its near range, 100, + 0x2c its rate, 0.01); one let go; a kind it lets be; its point (NaN made 0). Each object's move adds their pull (`FUN_1403b8ca2` -> `FUN_1403f9130`): none on one of no weight (its entry's + 0x50), + 0x120 bit 2 or + 0x18c bit 25 set; each vacuum not letting its kind be (its entry's + 4, 0 for the player) and within reach pulls it along the ground toward its point at weight / 100 x the speed, kept between the least and the most, times (distance - near range) x rate kept between -1 and 1 | as KH2 on the KH2 enemies and allies (`vacuum_pull` in `shell.cpp`); a vacuum's turning not applied (none of the port's kinds turns one), nor any pull on Days's players: Demyx's water clones' let the player be and pull Demyx (weight 500) and each other (10) |
| 1:210 | the first object of the field whose entry's + 0x4c is a number, into an AI handle's + 4 (`trap_obj_search_by_part`, `FUN_1403b4180`) | as KH2 (allies too): Demyx's water clones find him so (4002) |
| 2:51 | a reaction command put on an attack (`trap_attack_set_rc`, `FUN_140437b70`: + 0x114 the object, + 0x118 the owner, + 0x11c the command, + 0x120 its reach, + 0x124 its height -- the reach when not above 0; the PS3 build's `ATTACK_MAGIC::set_reaction_command`, 4 bytes lower) | kept as KH2 keeps them, and offered as KH2's `RC_SEARCHER::check_magic` offers it (the PS3 build's): each tick a magic attack with a command meets the player's collision with one of its reach and height at its place, the command goes to the attack's owner (`TARGET::set_attack`) -- the Hydra's Urninator, 420, on Phil's urn |
| 1:230 | a table's record of 12 bytes: its first word over its second (`FUN_1404310f0`) | as KH2 |
| 2:21 | memory several objects share by a number (`trap_teamwork_alloc`, `FUN_1403f6df0`: n + 1 bytes, zeroed, made by the first to ask; to each later one the same, its first byte made 1 -- "made already"; all let go with the field, `FUN_1403f6ea0`) | as KH2. Until 2026-11-11 the first byte was left 0: each of the Sorcerer's cubes took itself for the first, started its own turn-giving thread and cleared the turns, and none ever shot |
| 2:10 | an attack of its own made (`FUN_140432250` -> `FUN_1404371b0`, YS::ATTACK_MAGIC, 0x138 bytes; `FUN_1403cf830`): for the object an AI handle names (its owner: the enemy, or KH2's player), an `atkp` entry (looked up with the sub-number, `FUN_1403eafc0`), a combo group (-1 a running number), the sub-number; its address back | as KH2 (below, "Attacks of their own") |
| 2:11 | its size: a radius (+ 0x104) and a height (+ 0x108; 0 or less: the radius) (`FUN_140437c00`) | as KH2 |
| 2:12 | its place (+ 0xe4; the one before kept at + 0xf4, swept between) (`FUN_140437a80`) | as KH2 |
| 2:13 | ended (`FUN_1403d0eb0`: + 8's 0x800) | as KH2 |
| 2:17 | a hit made with it on an object at once (`FUN_1404328e0` -> `FUN_1403d23c0`, the object's class's + 0xc0): the attack, the object, a byte at the hit's + 0x24 and a value -- the Neoshadow's strike, KH2's player's attack on itself | the attack and the object (a Days player: its hit; a KH2 enemy and KH2's player's attack: his blow) |
| 2:22 | whose effects its hits show (`FUN_140432a90`: + 0x28 an object's + 0x80) | nothing: Days's own show |
| 2:37 | its team (`FUN_1403d2390`: + 0x38, 3 made 4; + 0x39 its targets, every team but it and 0) | kept (the port's attacks of an enemy hit Days's players, KH2's player's its enemies) |
| 2:61 | the effect it follows (`FUN_140437a00`, + 0xc8): its place the effect's each tick, ended with it -- the Hammer Frame's shockwave on its element 8 | as KH2 |
| 2:14, 2:29, 2:39, 2:44 | an attack ended (`FUN_1403d1f20`: + 8's bits 1, 3, 4 or 9), grown or its effect gone (+ 8 & 0x18), it hit (+ 8's bit 9), the object it hit (+ 0x90) | as KH2 (an attack ends when it hits) |
| 2:18 | whether an attack can hit an object (`FUN_1403d2060`: its team's targets) | an enemy's: Days's players; KH2's player's: KH2 enemies |
| 2:19 | an attack's place and the place before set at once (`FUN_140437a40`: a line swept) | as KH2 |
| 2:43 | its way from its owner (`FUN_140437530`) | from its enemy's place |
| 2:38 | a routine of the script run when it hits, with a value (+ 0x98 the machine, + 0x9c, + 0xa0) | as KH2 |
| 0:26 | nothing (`FUN_14041ca40` is empty) | nothing |
| 0:43 | an angle made one of (-pi, pi] | as KH2 |
| 0:45, 0:46 | a point moved along a way at a speed a tick; a way turned toward another by at most an angle a tick (`FUN_1403b7200`) | as KH2 |
| 0:48 | 0:47's memory let go | as KH2 |
| 0:38, 0:41, 0:50, 0:51, 0:52, 0:53 | an effect's place set, its turn set from a way (`FUN_1403b7710`), whether it plays (+ 0x30), ended at once, ended over n ticks (`FUN_1402be630`), its place (`FUN_1402be970`) | as KH2, but 0:52 ends it after n ticks without KH2's fade |
| 1:21, 1:22 | an effect on the actor, its handle (as 1:87); one at a place of the world (`FUN_1403b66c0`) | as KH2 |
| 1:88 | an object's aim point (its record's point, `FUN_1403bdde0`) | a KH2 enemy's aim shape, a player's middle |
| 1:93 | a member of the party: 0 KH2's player, n a partner | the player; no partners |
| 1:160 | a target record made of an object (`FUN_1403bee60`) | as KH2 |
| 1:141, 6:51 | the ground under a point (a cast down a depth, flags 4, `FUN_1403f3dd0`); whether a segment meets the room (`FUN_14040c2b0`) into a `YS::BGHIT`: the point it meets (`CrossPos`, + 0), its polygon (+ 0x14, -1 first and left so when it meets none), the object whose collision it met (+ 0x18, none for the room's) | Days's room collision (`Collision_CastRay`); the polygon 0 when met (Days's rooms number none KH2 knows) |
| 6:53, 6:55 | whether a BGHIT met something (+ 0x14 not -1); the kind of its polygon (`FUN_14040c380`: its attributes' low four bits, `dk::OctCollFile::getAttr`) | as KH2; the kind 0 (Days's rooms carry no KH2 attributes: `M_EX540` asks for 1) |
| 1:248 | a point at random round another, between two distances, in the air (`trap_bg_get_random_pos_air`, `FUN_1403f4080`) | as KH2, but KH2's search of eight ways (`FUN_1403f44a0`) not read: one turn at random, the ground within the heights given |
| 1:247 | a point at random round another, between two distances, on the ground (`trap_bg_get_random_pos`, `FUN_1403f3fa0`: the distance at random, the way one of eight sectors of 45 degrees in a running order and at random within it) | as KH2, but `FUN_1403f4180`'s search not read: the way's room stops it, then the ground under it |
| 6:9 | a point's distance to what a record names (`FUN_1403bdd90`: an attack's sphere, an actor's aim shape, `FUN_1403c99f0`) | to the attack's sphere, or the aim point (its shape's radius not taken off) |
| 6:54 | a vector copied into one of its own | as KH2 |
| 2:62 | its growth (`FUN_140437c30`): over n ticks (+ 0xd0) from a radius and a height (+ 0xd4, + 0xd8; 0 or less its own) to its size, eased by `sin(t / n x pi / 2)` (`FUN_140141ae0`); ended once grown | as KH2 |
| 1:87 | (its value) the effect's handle | the effect's own: what 2:61 follows |
| 1:129 | a throw of its own (`FUN_14042b550` -> `FUN_1403d3e50`): a vector (its NaNs made 0) -- its x, z the way (made one long, times its w), its y the speed up (+ 0x24 -y) -- and class 4, thrown (`FUN_140409d30`; the classes at 0x140750d30), whose movement (`FUN_140422780`) keeps its speed along and lands with the motion it was playing plus one (`FUN_140422700`: + 0x114) -- the Rapid Thruster's rise in motion 18, landing in 19 | as KH2, but KH2's slowing of the rise and its fastest fall (values the game sets running, `DAT_142ae5760` + 0x18 to + 0x20, + 0x138): the jumps' pull |
| 1:52, 1:53, 1:54 | a collision group turned on, off, or back to what the motion and the shapes say (`method_obj_enable_collision`, `_disable_`, `_reset_`: `YS::COLLISION_FLAG` at + 0x5a8, a word on and a word off) -- below, "Collision" | as KH2: the Graveyard `M_EX790` turns its groups 21 to 25 off, `M_EX630` its 6 |
| 2:7, 2:92, 2:15 | a hit's reaction kind (`YS::DAMAGE`'s `Reaction`, + 0x26); whether it is a finishing one (`OrigReaction`, + 0x27, 6, 7, 10 or 11); its reaction to be carried out (`Flag`, + 0x18, bit 0) | as KH2 on the Days hit: a small reaction (2), no finish; the port plays a Days hit's reaction before trigger 6, so 2:15 changes nothing |
| 2:1 | a blow taken (`trap_enemy_exec_damage_blow`, `FUN_1403d3f50`): from the front (the hit's way, `Dir`, against its facing) it turns to face the hit and plays the first motion given, from behind it turns its back and plays the second, then it is thrown as 1:129 throws -- along the way's ground part the last value fast, the one before up -- landing in the motion after; + 0x124 bit 17 a finish's | as KH2: a Days hit's way is from the player who struck to the enemy along the ground. The Mole Driller `M_EX700`'s (16 and 18, 15 up, 20 along) |
| 1:161 | whether another object carries it (`trap_obj_is_attach`: `YS::OBJ`'s `Attach`, + 0x6a0) | as KH2: none of Days's do |
| 2:76 | its karma's limit (`trap_enemy_set_karma_limit`: `YS::ENEMY`'s `KarmaLimit`, + 0xd4c, after its `Karma`) | as KH2: trigger 28's ("A hit: damage, not reflect", above) |
| 6:4, 6:57 | a search's list emptied (`Ts::TARGET_SEARCHER`'s `Counter`, + 8, and its ring's next, + 4's low byte); a target record's group (+ 4) | as KH2 |
| 2:4, 1:292 | the enemy after one, the first after none (`trap_enemy_each`, `FUN_1403db9d0`); the object whose unit argument n is a value, into an AI handle (`trap_obj_search_by_unit_arg`) | KH2's actors of the field |
| 1:59, 1:224 | whether it flies (`trap_obj_is_fly`: its class's flag bit 1 -- class 5, which 1:58 `trap_obj_fly` gives it -- or + 0x18c bit 6); whether its class is one of the air's (`trap_obj_is_action_air`: the flags' bits 0 or 1) | as KH2 |
| 1:180, 1:153 | the motion its jump or fall lands in (`trap_obj_set_land_motion`, + 0x11c); put at a point with its place before too (`trap_obj_reset_pos`, `FUN_1403b6100`) | as KH2 |
| 1:214 | the thread attached to it stopped (`trap_obj_hook_stop`: `FUN_1403d76d0` with none) | as KH2 |
| 1:231, 1:232, 1:233 | its health record's third word, the least it goes to (`YS::SHEET`'s 32 `HP` of `{Hp, MaxHp, MinHp}`; `trap_sheet_set_min_hp`, `trap_sheet_min_hp`); its health set within the two (`trap_sheet_set_hp`) | as KH2 on the record; Days's damage kept above the least (`khdays_kh2_enemies_damage`, a join in `Ov107_AiState_ApplyHit`: the Phantom's), the health set given to Days's enemy (at least 1: Days's defeat comes through its hits) |
| 0:8, 1:36, 1:203 | a vector printed (empty in the PC's build); a part of KH2's room hidden (`trap_bg_hide`); a spawn unit of the room let go on (`trap_unit_enable`) | nothing: Days's room and waves are not KH2's (the enemies a boss's unit would bring do not come) |
| 2:6 | an object in one of its reaction motions (`FUN_140431d50` -> `FUN_1403d4880`: a motion controller at + 0x158, + 0x180 from 10 to 39) -- Crimson Jazz asks it of itself before it acts | as KH2 |
| 0:78, 0:83 | between two vectors, the first times 1 - t and the second times t (`trap_vector_inter`, `FUN_140140230`); a vector turned about an axis made one long by an angle (`trap_vector_rot`, `FUN_1403b7560`, its turn `FUN_1403b6fb0`: `cos + k k (1 - cos) + [k] sin`) | as KH2 |
| 0:39, 0:40, 0:67, 0:49 | an effect's size, its turn and its colour set (`trap_effect_set_scale`, `_rot`, `_color`: + 0x20, + 0x10 and + 0x30 of its transform, 1 each as it is); whether it is still alive (`trap_effect_is_alive`: its + 0xe bit 0 clear; 0:50 asks whether its instance is there) | its element's size the one given, its draws' colours times the colour (the Blizzard Lord's ice spikes at random sizes and turns, Dark Thorn's fading by its alpha); 0:49 whether it still plays, as 0:50 |
| 1:262 | a line of its voice (`trap_obj_voice` -> the object's virtual + 0xa0, `YS::STDOBJ::_OVR_voice` -> `YS::VOICE::request`): the sound of that number in its voice bank, one at a time, a new one taking the place of one asked with no higher a priority (`exec`: the one before ended) | the mod's `kh2/voice/<kind>/<number>.wav` (below, "Voices"), heard where the enemy was last drawn; Hades's 13 |
| 2:3, 2:87 | a hit's slide (`trap_enemy_exec_damage_hitback`, `FUN_1403d4960`): unless the actor stands firm, the hit's way made one long (turned back for a speed below 0) at a tenth of the speed given (its health record's + 0x1c8 added) a tick, 0.9 of itself each tick after, over below 1 or on a wall (`FUN_1403d4570`, `FUN_1403b9420`); a large hit (`trap_enemy_exec_damage_large`, `FUN_1403d4140`): from the front it faces the hit and plays 23, from behind it turns its back to it and plays 24, its idle asked after (`FUN_1403c8310`'s queue), then the slide | as KH2, its standing firm only + 0x120 bit 7 (the PC's `FUN_1403ba190` reads two flags of the native class's more); the health record's + 0x1c8 is none in Days's field. The bosses read slide by none: the Volcanic Lord's Blizzard reaction (2:3, 0) and Hades's (2:87, 0) |
| 2:64, 2:91, 2:85 | a hit's first reaction (`trap_damage_orig_reaction`, + 0x27); a reaction kind's kind (`trap_damage_get_reaction_type`, `FUN_1403ce980` of the value given: 1 and 12 are 1, 3 6 8 10 13 16 18 are 3, 4 7 9 11 15 17 are 4, the others themselves); its element (`trap_damage_element`: its attack's record, + 0x20, its byte + 9 -- atkp's element, 0 physical, 1 fire, 2 blizzard, 3 thunder) | as KH2 on the Days hit: its element from the hit's kinds (Days's packet's high flags: Fire, Blizzard, Thunder), below ("The second bosses") |
| 1:311 | its rate for an element (`trap_sheet_set_element_rate`, `FUN_1403c2650`: its sheet's + 0x1a8 + element, the weakness times the rate, in percent, rounded up) | the share of Days's damage of that element it takes (`khdays_kh2_enemies_damage`): the Volcanic Lord takes no fire |
| 1:227 | a jump at once (`trap_obj_jump_direct`: its class's virtual + 0x18, the PS3 build's `YS::ACTION::jump` -- `YS::OBJ::prepare_jump` with the vector as 1:55's, then `change_action(1)`) | as KH2: class 1 on the spot, its motion as it is, no take-off waited for |
| 6:24 | what the room's collision pushed it by this tick (`trap_obj_get_bg_press`: + 0xa78, which the actor's update writes, `FUN_1403b9420`) | Days's collision's push along the ground a tick (the place Days put it less the one the shell gave it), written at + 0xa78 each frame; it ends a slide and a steering too |
| 2:41 | whether an attack of its own met the floor (`trap_attack_is_hit_floor`: + 8 bit 4) | a ray from its middle down by its size meets Days's room (Hades's fire shots) |
| 1:217 | put at a point as 1:148, its + 0x9b8 bit 12 set after (`trap_obj_set_pos_trans`, `FUN_1403b6510`) | as KH2 (the bit is not read by the port) |
| 1:270, 1:271 | an element its motions' triggers start kept from starting, let start again (`trap_obj_motion_disable_anmatr_effect`, `_enable_`: a list of 16 at the motion controller's + 0x1b8, which `FUN_14040dd70` asks before a trigger 1 starts its element, `FUN_1403c7db0`) | as KH2, a list with no bound (Scar's) |
| 1:319, 6:2 | whether an object is Sora (`trap_sysobj_is_sora`: + 0x6cb bit 0); the player's form (`trap_player_get_form`: KH2's player's + 0xde0, 0 his own) | Days's players are KH2's player Sora to the scripts, in his own form |
| 10:31, 1:122 | whether the object is a summon (+ 0x6c8 bit 22); its state printed (`trap_obj_dump`, empty in the game) | none is a summon; nothing |
| 1:207, 1:295 | KH2's reaction camera on an object (`trap_camera_watch`, `FUN_140430b80`: a target record of the object's actor, `FUN_1403bee60`, to `FUN_140176310` -- the PS3 build's `TARGET::set_obj` and `dk::ReactionCamera::create`, the camera's mode 3); the camera back home (`trap_camera_reset`: `ObjCamera::setHome`). `ReactionCamera::update` holds only while the player cannot act (`YS::OBJ::can_control`) and the target stands (`TARGET::is_exist`): KH2's target camera on the target's point (`TargetCamera::updateTargetCamera`), else the camera's mode returns | while a KH2 enemy holds this game's player and the watched object stands, KH2's target camera on its aim point in place of the field's camera, whichever it is (`runtime/camera/kh2_camera_hooks.c` `reaction_frame`, as the event sequences' camera: `kh2_camera_lock` and its springs, from where the camera was); not its eye's collision nor the stick. Dark Thorn's reaction command |
| 1:193 | the camera put at its place at once (`trap_camera_warp`, `FUN_140430b40` -> `FUN_140165bc0`: `ObjCamera::setWarp`, its + 0x38 set: the next update takes no easing) | KH2's camera's next settle goes straight to its targets (`kh2_camera_hooks.c`: the field's and the reaction camera's `snap`); Days's own camera has no easing to skip. Xaldin's Jump |
| 1:108, 1:109 | a part of its model shown, hidden (`trap_obj_show_part`, `_hide_part`: `FUN_1403cd810` -> `FUN_1401a8a90`, `FUN_1403cc9b0` -> `FUN_1401a7cf0` on + 0x7b0's model; the PS3 build's `kn::ModelSKL::showPart` sets bits 0-1 of the flag of each subpart whose part -- its attribute word's bits 6-10 -- is the one named, -1 every one, to 1, `hidePart` clears them; a subpart is drawn while they are 1) | the model's subparts of that part left out of its draw (`NeutralMesh::kh2_part`, `khdays_models_draw_posed`'s hidden parts): the Prison Keeper's faces (parts 1-5), the Hydra's heads (1-7, hurt 11-17); every part shown at first |
| 2:42 | where one of its attacks met the room (`trap_attack_hit_bg_pos`, `FUN_140432650`: the attack's + 0xb4) | the point 2:41's ray met the floor at, written there: the Prison Keeper's shots |
| 1:181 to 1:184 | KH2's lights (`trap_light_create`, `_set_flag`, `_set_color`, `_fadeout`): below, "Lights" | as KH2 (`lights.h`), the room's and the models' colours tinted |

## Attacks of their own

KH2's scripts make attacks that are objects of their own (`2:10`; YS::ATTACK_MAGIC, 0x138 bytes, its class's table at
0x1405d7090): an owner, an `atkp` entry and a combo group, then a size (`2:11`), a place (`2:12`) or an effect to follow
(`2:61`), a growth (`2:62`). Each tick (its class's + 0x18, `FUN_1404376b0`) one following an effect takes the
effect's place (`FUN_1402be970`) and ends when the effect does; one growing eases its radius and height from where
they began to its size over its ticks and ends when grown; then it is tested from its place before to its place now,
in steps of its radius -- a sphere when its height is its radius, else a cylinder that high. The port keeps them by
their address in the scripts' memory, its enemy's (the shell): an enemy's hits Days's players, each once (its `atkp`
entry's hit, trigger 15 too); one KH2's player owns (the Neoshadow's strike: `2:10` for the player, `2:17` on itself)
hits KH2 enemies as his blow (Days's damage his last hit's times the entry's power). An attack ends with `2:13`, its
growth, its effect, or its enemy's defeat, and its memory is let go. Its team (`2:37`) is kept; whose effects its hits
show (`2:22`) is Days's.

## Attacks reflected (2026-11-19)

KH2 settles two attacks that meet before either hits (the PS3 build's `ATTACK_WORK_MANAGER::check_reflect`, read
2026-11-19): two attacks of other teams, each still able to hit the other's owner, their collisions touching, each owner
within the other's cone (`ATTACK_WORK::is_reflect_range`), and the table `IsReflectTable` (0x720cbc) allowing their two
types -- 0 ATTACK, 1 S_ATTACK, 2 GUARD, 3 S_GUARD, 4 SPECIAL, 5 CURE, 6 C_CURE; an ATTACK reflects an ATTACK, a GUARD
and an S_GUARD, an S_ATTACK an S_GUARD alone, the rest nothing. Then each is reflected (`ATTACK_WORK::reflect`): neither
hits the other's owner any more (`ATTACK_CHECK::hit`), one that does not reflect itself NULL is marked reflected (+ 4's
0x200, the other's owner at + 0x84: the PC's 2:39 and 2:44), its owner plays its side (`exec_reflect`), and its script's
routine for it runs (2:74).

The atkp entry's fields (OpenKH's names): + 0x13 `RefactSelf` and + 0x14 `RefactOther` (0 REFLECT, 1 GUARD, 2 NULL: what
the attack does once reflected, and what it makes of the attack it reflects), + 0x15 `ReflectMotion`, + 0x16
`ReflectHitBack` (a short, KH2's cm; under 0 50), + 0x18 `ReflectAct` (a state's name, at that offset from the record),
+ 0x20 `ReflectRC` (none of 00battle.bin's sets one), + 0x22 `ReflectRange` (degrees: 0 any way) and + 0x23
`ReflectAngle` (a signed byte, half degrees). The cone: the way from the other's owner to the attack's own owner along
the ground (`ATTACK::get_dir`) against the owner's facing turned by twice + 0x23 (`MATH::VectorRotY`), reflected when
their dot is under -cos(+ 0x22).

- **An enemy's side** (`YS::PIERROT::_OVR_exec_reflect`): the state's + 0x18 routine (`ACT::reflect`, event 7) and
  trigger 19, both given its attack and the other (the other read first); then, unless its attack's + 4 has 0x40 (2:26,
  `trap_attack_exec_reflect`, sets it) or it reflects itself NULL, its `ReflectMotion` (`BTLOBJ::exec_reflect_default`:
  started at once, its idle after; slid back against its facing by `ReflectHitBack` plus its sheet's hitback addition, a
  tenth of it a tick) with `ACT::clear`, its `ReflectAct` entered (`ACT::start`), and 0x40 set. A reflect motion (140 to
  150, `BTLOBJ::is_reflect_motion`) holds its states as a damage motion does (`ACT::is_damage_motion`).
- **The player's side** (`YS::PARTY::_OVR_exec_reflect`): his attack reflecting itself NULL, or the other making NULL of
  it -- a spark (the system effects' 13) and no more; the other making GUARD of it, on the ground -- the spark 12, he
  faces it and is pushed 200 away (`BTLOBJ::set_hitback`), motion 42; else -- the spark 13 and `exec_reflect_default`'s
  50, motion 41 (43 in the air).

In the port, for KH2's enemies alone (Days's own play as they always have):

- **A Days blow** stands for KH2's player's plain attack: an ATTACK reflecting itself REFLECT from any way. Landing on a
  KH2 enemy (`Ov107_AiState_ApplyHit`, its join `khdays_kh2_enemies_reflected`) it meets the enemy's attacks standing
  then -- its motion's ranges 10 and 33 at its time, and its own (2:10) -- that it can still hit him with (each range's
  once), of a type that reflects an ATTACK, within their cone, one of their spheres reaching the line from his middle to
  the enemy's (Days's blow has no shape of KH2's: as thick as his body). Reflected: Days's hit is answered with no
  damage (as an enemy's barrier answers it, + 0x1c4's 0x10), the attack's range has struck him, the enemy plays its
  side, and he his: Days's player has no KH2 motions 41 and 42, so he staggers as a Days hit staggers him
  (`Ov022_ReceiveHit`'s end: his flag 0x10, his flags2's 0x100, the push his drift) with no damage and no hit counted --
  Days's hit would count one (`Ov002_World_AddStat`, a mission panel's count) --, pushed as far as the reflection says
  (half of it his push, as `hitback_push`). The spark is drawn on the attack's sphere toward him (KH2 draws it on the
  blow's).
- **His block** stands for KH2's guard (a GUARD reflecting itself NULL). Days's block (`Ov022_TryBlockHit`) sends the
  attacker its block packet (low flags 0x11) through `Ov107_AiState_ApplyHit`: the KH2 attack he was struck with is
  reflected once Days's hit is over (the enemy's side; Days's block plays his). A guard reflects an ATTACK alone, so the
  port's KH2 hits on a Days player are unblockable (the request's 8) when their attack is an S_ATTACK or a SPECIAL --
  KH2's unguardable attacks: Sephiroth's split flash (1353) and Heartless Angel (1697).
- **The attack level** (1:358, `trap_sheet_set_attack_level`: the sheet's + 0x24c) is the sub-number its motions'
  attacks are made with (`YS::ATTACK_OBJ`'s constructor; `ATTACKPARAM::Get(id, sub)` finds the record of id << 16 | sub
  << 8, the record's + 0 its sub-number times 256): Lexaeus raises his from 0 to 3, his strikes' power from 50 to 600
  and their reaction from small to a blow. Days's player takes his stand-in's damage all the same: the reaction and its
  push change, not the damage.
- Not made: KH2's reflected motion in the air throws the enemy back up (its class's blow) -- the port plays the motion
  alone; an attack's own reflection mark (`ATTACK_WORK::exec_refmark`, its PAX's); a script attack's sub-number (2:10's
  fourth) is not looked up; a Days spell's hit is taken as a blow; a held player's own attack ranges (AQ90's guard, atkp
  1719) against the enemy's attacks. KH2 checks the two attacks' collisions every frame they stand; Days's blow is met
  only as it lands on the enemy's body, so a clash needs the enemy's attack standing at that moment -- rarer than in KH2
  (Lexaeus's ATTACKs stand a few frames: his motion 204's 2 to 20, 201's 61 to 73).
- **A state entered while a damage motion plays** waits for it to end, its first tick too (`ACT::exec` skips a state
  under priority 300 while `ACT::is_damage_motion`): the reflect motion clears the state and the main thread enters the
  next at once -- until 2026-11-19 that one's motion cut the reflect motion in the same tick.
- **Invincible players** (a held motion's range 27: `CheckRangeCallback` sets the motion's `FLAG_STAR`, 0x400000 of +
  0x20; `1:225`, `trap_obj_is_star`, asks it): KH2's attacks land on them all the same -- trigger 15 -- with no damage
  nor reaction; Sephiroth's script asks 1:225 of the player his attacks land on. The port skipped them.

Seen (window runs, 2026-11-19, Lexaeus's data battle in the coliseum): Days's block (Y) of his strike (atkp 1843)
reflected it at 89.79 s -- his motion 140 played (his recoil and counter: its range 10 strikes with 1881 at its frame
160), its reaction command offered ("Megadescarga"), taken at once (708, the player in AR30 and his power count rising),
the player let go at 94.87 s, Lexaeus fighting on at 97.28 s; his S_ATTACKs (1842, 1913, 1965, 1966) landed through the
block. With every blow taken as meeting 1843 (a local test switch): his trigger 19 gives the player's blow his motion
(its owner a player: no 2:26), the spark 12 on his body, his next state held till 140 ended. A plain chase gave one blow
during an ATTACK of Sephiroth's (1144), its sphere 0.14 m off it.
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
  `1:56` off the screen, and an enemy that leaves coming back.
- **Covered, second batch**: the Assault Rider `M_EX570`, the Nightwalker `M_EX580`, the Magnum Loader `M_EX600`,
  the Hot Rod `M_EX640` (`_HB`, `_WI`), the Cannon Gun `M_EX650` (`_TR`), the Armored Knight `M_EX760` (`_NM`), the
  Creeper `M_EX880` and the Dusk `M_EX990` (the calls above from `0:0` on). Played (2026-11-08, level 99, 200
  seconds, the four by four in one wave, the last three alone): hits on Roxas, the Assault Rider 10, the Nightwalker
  23, the Hot Rod 25, the Armored Knight 12, the Creeper 2, the Dusk 3. **Not yet**: the Magnum Loader's and the
  Cannon Gun's shots -- their scripts run, but their attacks are effects that hit (KH2's shots are objects and
  effects with collision), which the port does not make yet. Since the research tool follows the routines a script
  starts as threads (`1:112`, `2:20`; 2026-11-09), the Magnum Loader's threads make 3 calls more, all answered since
  (1:13, 1:94, 1:101: its Quick Blade, below, "Reaction command zones"), and the Armored Knight's 14, all answered
  since too (its Rising Sun). The Magnum Loader's charge (52A) ran for ever before 1:151 was KH2's (it waits for the
  motion's frame 80 to start counting).
- **Covered, third batch**: the Crimson Jazz `M_EX130` (2:6), the Neoshadow `M_EX420` (`_NM`; 2:10, 2:17), the
  Hammer Frame `M_EX560` (`_HB`, `_WI`; 2:10, 2:11, 2:22, 2:61, 2:62: its shockwave) and the Rapid Thruster
  `M_EX660` (`_WI`; 1:129) -- with them 43 of KH2's objects' scripts are answered in full (the bosses' parts
  `B_EX280`, `B_EX290`, `B_EX320` among them). Played (2026-11-10, alone, level 99): the Hammer Frame's shockwave
  made each slam (2:10 on its element 8, grown from 150) and hit Roxas standing near; the Neoshadow leaps 1.8 m by its
  motion alone (range 30), dives with its attack from frame 40 (hits Roxas) and lands on trigger 14 (204), claws,
  zigzags and sinks into its shadow, and its zone offers 331 ("Danza del viento" in Spanish): Roxas plays its motion
  252 and strikes it five times. Before trigger 14 and range 30 without pull it stood for ever in its leap's loop.
  **Not yet seen**: the Neoshadow's strike state (2:10 for the player, 2:17 on itself), the Crimson Jazz and the Rapid
  Thruster in play.
- **Hidden threads** (2026-11-10): with 2:20's routine read from its first argument and routines passed through
  parameters followed, seven kinds' scripts make more calls -- the Soldier 71 (Cyclone), the Dusk 83 (Reversal),
  Cerberus 106 (its fire), the Crimson Jazz 89, the Strafer 96, the Cannon Gun 93, the Assault Rider 73 -- all answered
  now (the calls above from 2:14 on). Played: the Soldier's Cyclone ("Ciclón") and the Dusk's Reversal, offered by
  their zones and taken (a test switch pressing).
- **Covered, fourth batch** (2026-11-10, the calls above named by KH2's PS3 build): the Silver Rock `M_EX110`, the
  Hook Bat `M_EX520` (`_HB`), the Aeroplane `M_EX540` (`_WI`), the Luna Bandit `M_EX630`, the Mole Driller `M_EX700`
  (`_NM`), the Aerial Knocker `M_EX780` (`_HB`), the Graveyard `M_EX790` (and its Christmas and Halloween ones), the
  Dancer `M_EX930` and the Sorcerer `M_EX960` -- 70 of KH2's objects' scripts answered in full, among them parts of
  bosses (Xemnas's armour's weapons `B_EX280` to `B_EX320`, Luxord's card `B_EX210_EH`, Scar's ghost
  `B_LK110_PHANTOM`), not yet a whole boss but Cerberus. Played (level 99, the seven last together, 300 seconds):
  hits on Roxas, the Graveyard 56, the Dancer 27, the Silver Rock 26 (its own attacks, 2:10), the Aeroplane 6, the
  Aerial Knocker 2, the Hook Bat 1; the Mole Driller (alone) drills and strikes from under the ground, and a hit in
  its revenge throws it (2:1). Three things of KH2's came out of them: a jump waits for its motion's take-off and
  rises in KH2's time (1:55, above: the Mole Driller's drill landed before it struck), a state's name shouted runs
  each object's shout routine once a frame (1:90: the Silver Rock's stood the game still), and the collision groups
  the script turns on and off.
- **The Sorcerer** (2026-11-11) is placed as its cubes `M_EX870` (KH2's spawn places the cubes: their entry's + 0x50
  names the Sorcerer, 315), which make it (1:0) and put it where they stand. It moves and shouts its orders
  (`atk_shout_shield`, `atk_shout_shot`: one turn added to the memory they share, 2:21, then the order shouted, 1:90);
  a cube that hears an order and is not busy takes the turn (`funnel_shield`'s and `funnel_shot`'s shout routines)
  and shields or shoots: its shield's blow (motion 201) and its volley (motion 202, groups 4 to 11) struck Roxas 14
  times in 70 seconds (level 99, the coliseum `he00` as an added room). Three things of KH2's made it: the shared
  memory's "made already" byte (2:21), the turn-giving thread run apart from its object (2:20) before the shouts are
  told, and a state left when its routine ends running its leave routine (the cubes' "busy" cleared).
- **Next**, one or two calls each: `M_EX730` (1:113, the player's stick in KH2's world), `M_EX660_WI_RAW`,
  `M_EX760_NM_RAW`, `M_EX770_TR_RAW` (1:274, an event's time left), `M_EX760_RAW` (1:305, 1:306, the camera's eye and
  point), `M_EX550` (1:227, 2:85: a hit's element), `M_EX690` (1:222, an object's entry), `M_EX850` (1:73), `B_EX310` (1:17), `B_EX380`, `B_TR020` (1:114, 1:262); then the bosses (`B_*`: 96 scripts,
  most of them 5 to 60 calls away) and the shots that are effects that hit (the Magnum Loader's, the Cannon Gun's).

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
  | 0:32 | the screen back from white over n frames (`func_screen_whitein` in the PS3 build; `FUN_140156c90` on `FUN_140156dc0`'s fader: + 0x100, + 0x104, + 0x108; KH2's hit starts it for 10 frames when the player's 200-kind attacker hits) -- not shown: Days's screen fades are its own |
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
2. **Health and its bar**: its KH2 health in Days's scale ("Health" below), shown on the KH2-style enemy HUD
   (`port/ui/kh2_enemy_hud`) with Cerberus's name (KH2's journal's: "Names" below); KH2's several bars only when its
   `enmp` entry is found.
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
- **Ledges**: on the ground of its own (no jump, no motion in the air, not falling), its way along the ground
  (its motion's root, its steering, a hit's slide) does not take it off a ledge -- the ground more than a step
  below, or none within 10 m, under where it goes: it stops there as at a wall, its slide and steering ended.
  KH2's rooms wall their edges; Days's arenas and the rooms made from KH2's maps leave some open (the coliseum's:
  Hades slid off by a hit's slide and could not climb back). A held player is kept off them the same way.
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
- **Its name on the enemy HUD** (2026-11-12): KH2's own, in the game's language -- see "Names" below.
- **Not yet**: Days's camera, close behind the player, inside its body (6 m) outside the reaction command; 0:32 (the
  routine's screen effect) and the sequences' depth blur (command 0x18) not played.
- **Its leap's end** (2026-11-10): KH2 tells the state machine as an actor lands (`FUN_1403b79d0` ->
  `FUN_1403ca1d0(+ 0x390, 2)`): the current state's routine for that event, its record's + 0xe (+ 0xa + 2 n: n 0 its
  leave, 1 off the ground, 2 landed), runs at once unless it has not begun -- Cerberus's `jump` lands into
  `jump_land` that way, its loop (171) no longer waiting for a hit. The port runs it before trigger 13 or 14.

### The next bosses (2026-11-10)

With the calls above, three more of KH2's bosses' scripts are answered in full; what each needs of the field:

- **Thresholder** `B_BB100` (a mod kind): played alone (level 99), it fights, shouts its Possessor's orders
  (`atk_shout`, 1:90), whips (`atk_whipslam`), strikes Roxas and is defeated with its end. Its Possessor `B_BB130`
  (whose script is answered too) has no motion set in its object entry (`00objentry.bin` + 0x28 empty) and is not a
  kind yet: without it the Thresholder mostly calls for it, as KH2's would not.
- **Shadow Stalker** `B_BB120`: not a kind. It begins by possessing the ballroom's window (`hyoui_window`): the
  Beast's Castle ballroom's objects it takes (windows, chandelier) are KH2's room's, which Days's rooms do not have --
  it waits there for ever.
- **Hayabusa** `B_MU110`: Shan-Yu's falcon, found by its entry (1:114, 21): not a kind until Shan-Yu `B_MU100` is.

### The second bosses (2026-11-12)

Kinds made by the mod's tool, their scripts answered in full by the calls above (0:39, 0:40, 0:49, 0:67, 0:78, 0:83,
1:122, 1:207, 1:227, 1:262, 1:295, 1:311, 2:3, 2:41, 2:64, 2:85, 2:87, 2:91, 6:24, 10:31):

- **Hades** of the Coliseum's cups, `B_HE030_CLSM` (`00objentry.bin` 0x90e): its object entry names `B_HE030.mset`, the
  motion set the story's Hades moves by -- the mod's tool copies the motion set an entry names and the shell reads it
  so (`motion_set_of`, `motion_set_name`). It speaks (1:262) from KH2's `he_hades` battle bank.
- **Volcanic Lord** `B_AL100_FIRE` and **Blizzard Lord** `B_AL100_ICE`, with their lava (`B_AL110`) and ice spikes
  (`B_AL120`), which their scripts make (1:0). The Volcanic Lord takes no fire (1:311) and answers a Blizzard hit with
  its own reaction (2:85 2: a slide of none, 2:3, and its state); the Blizzard Lord the other way round.
- **Dark Thorn** `B_BB110`.
- **Scar** `B_LK110` (1:217, 1:270, 1:271 too), speaking from KH2's `scar` battle bank, and his ghosts
  `B_LK110_PHANTOM` (`00objentry.bin` 0x833: they move by `B_LK110.mset` too).

A Days hit's element is read from its kinds, the hit packet's high flags (a join in `Ov107_AiState_ApplyHit` passes
them on): `Ov107_AiState_ApplyHit` names four by the statuses they give -- bit 7 burning (Fire; Roxas's Fire seen
`08 00 80 00`, docs/RUNTIME_MAP.md), bit 3 frozen (Blizzard), bit 11 jolted (Thunder), bit 2 tossed (Aero) -- which
are KH2's fire (1), blizzard (2) and thunder (3); Aero, which KH2 has no element for, and his attacks (0x4000) are
physical. Dark Thorn's reaction command watches it with KH2's reaction camera (1:207), as Cerberus's plays its event
sequences' cameras.

### Xaldin (2026-11-12)

**Xaldin** `B_EX130` (a mod kind, speaking from KH2's `n_dragoon` battle bank) with KH2's lights (above, "Lights":
`atk_final`, his last attack, darkens the room) and 1:193 (his Jump puts the camera at its place). His states:
`btl_normal`, `btl_hard`, `btl_super_hard` by his work's + 0x18, their `rvg_*` after a hit (`revenge`), `atk_final`
from those (half the time in `revenge`). His reaction commands: **Learn** (649) offered by his zone (his shapes'
command): its message adds one to his count of Jumps (his teamwork record's + 4, up to 9: `2:21`) and starts his
effects 8 and 9 on the player; **Jump** (647) offered by his script's thread (1:165) while the count is above 0: the
player leaps at him (`B_EX130.mset`'s 252, 254, 253 on the player, `1:29`), the camera put at its place, his blows
150 of power (atkp 581, below "Health": one and a half of Roxas's blows). Seen in a recorded walk: Learn, Jump and
its strike four times, his health 2,520 (60 blows of 42) down by 63 each. A walk's input script presses the reaction
button too (`khdays_input_script_buttons`).

### The Prison Keeper (2026-11-12)

**The Prison Keeper** `B_NM100` with **Lock**, **Shock** and **Barrel** as it fights with them (`N_NM050_BTL_2`,
`N_NM060_BTL_2`, `N_NM070_BTL_2`: `00objentry.bin` 992 to 994, which its `battle_start` finds by 1:114 and makes by
1:0 -- mod kinds of their own, their scripts answered in full). It swallows them (`suck_in`, `suck_in_all`) and spits
them out (`spit_out`), its face the part of the one inside (1:108, 1:109: `btl_lock`, `btl_shock`, `btl_barrel`,
`btl_final`), and makes them speak (1:262 on their handles: 10 to 12, which only their battle banks `lock_boss`,
`shock_boss` and `barrel_boss` hold). Its health stops at a least (below, "Health": 80%, 60%, 40%, then none); a hit
there (trigger 6) spits the one inside out and lowers it. Seen in a recorded walk: `suck_in`, Lock inside (`btl_lock`,
`rvg_lock`, `atk_swing`) down to 80%, `spit_out`, Barrel swallowed (`btl_barrel`, `atk_bite_s`), Shock's `atk_rise`
and `atk_darkfire` in another; one face drawn at a time.

### The Hydra (2026-11-12)

**The Hydra** `B_HE100` (`00objentry.bin` 352, a large enemy: kind 0xc) and its heads `B_HE110` (922, made by its
script), with Phil `N_HE020_BTL`, Pegasus `N_HE040_BTL` and Phil's urn `F_HE030_L`, which KH2's `he18` places with it,
and Megara `N_HE030_BTL`, whom it makes in its third round -- allies (below, "Allies"). Its rounds are its work's
+ 0x11e8: 1 (`level1`: `atk_tail`, `atk_hellfire`, `call_dark_zako`), 2 once a neck is cut (`guillotine`, `call_neck`:
three heads come up out of the ground, the Hydra lies down, `down_wait`), 3 once its heads are reborn
(`all_neck_reborn`). Its health stops at a least (1:231, "Health": 80% of 1050 at first); a hit there sends it to
`guillotine_chance`, which offers **Vanquish** (238) from its zone 9 (6.65 m ahead, 6 m round): taken, the player
plays its motions 252 and 253 (`AM60`, `AM61`, its event sequence "90"), a neck is cut and a head made where it was.
The heads keep their necks alive in a teamwork record the Hydra shares (2:21 22, + 4, + 8, + 0xc), each by the unit
argument the Hydra gives it as it makes it (1:218: 11 to 13); `down_wait` counts the necks dead and makes their heads
again a while after (its + 0x11ec of them, 60 or 120 ticks). Its other commands: **Attack** (342) from zones on its
seven heads' bones (20 to 26, turned on one after another by its motion 61A), **Pegasus Run** (335, zones 8, 18,
27 to 31, by its motion 57A: Pegasus `rc_pegasus_attack`), **Phil One-Two** (346, zone 19, 1 m ahead and 3 m round, on
60 ticks into `down_wait`: Phil and the urn put on the player, 1:18), and **Urninator** (420) on the attack the urn
carries (2:51, above).

- **Its heads' script starts after its maker's**: a head registers its neck as its script starts (trigger 0: the
  unit argument 11, 12 or 13), which the Hydra gives it only after making it. KH2's object's machine runs nothing as
  it is made (`FUN_1403e1240`); its script first runs as the object is updated (`FUN_1403e1c80`). The port started
  a made object's script within 1:0 -- the heads read 0, the necks stayed dead and the Hydra made a head every 19
  seconds; an object a script makes (an enemy or an ally) now starts at its first tick.
- **Its body**: many spheres (kind 1) on its legs, chest and necks; the largest of them (5 m, group 19, on only while
  it lies down) made its Days sphere first, and Roxas stood under it with the camera in its body. A large enemy's
  Days sphere is now how far its body's spheres reach along the ground at a player's height (KH2's player's body: a
  cylinder 85 high), in its pose each frame (`footprint` in `shell.cpp`).
- **Seen** (a recorded walk, Roxas attacking, the HD PACK): its tail, its fire and its dark minions; down to 80% and
  held there; Vanquish taken at `guillotine_chance`, its event sequence, a neck cut, three heads up (`atk_appear`,
  `atk_bite`, `searching`, `move`), the Hydra in `down_wait`; Phil, Pegasus and the urn standing by. **Not seen
  yet** (the walks' inputs do not take Roxas to the heads): the heads defeated and made again, Phil One-Two, Pegasus
  Run, Urninator, Megara and the third round. Megara's cheering prizes (6:1) are not made, and Days's camera, close
  behind its player, shows little of a body this large.

### Xemnas and Demyx (2026-11-12)

**Xemnas** `B_EX170` (speaking from KH2's `n_xemnas` battle bank) and **Demyx** `B_EX120` (`n_dancer`), their scripts
answered in full by the calls above (0:9, 10:19, 4:21, 6:16, 6:46, 6:47; and 1:32, 1:33, 1:48, 1:170, 4:0, 4:1).

- **Xemnas, seen** (a recorded walk, Roxas attacking): he warps near the player (`warp_target_near`) and casts his
  dark attack (`atk_dark`), then asks whether it caught the player (`check_atk_dark_hit`: his work's + 0xfc, which his
  trigger 16 sets as his motion starts its element 26 -- the routine at word 0x4d2 asks 2:23 and 1:101 of the one the
  effect is on). It never does in the port, so he goes on so for ever and never reaches the rest of his fight
  (`rc_invitation_to_dark`, `atk_shield`, `atk_counter`, `atk_combo`): **not known yet** why the check fails.
- **Demyx, seen** (recorded walks, Roxas near him and then keeping away): he speaks, hops (`move_hop`, `atk_hop`, the
  splash where he lands: an attack of 90 by 500 growing over 15 ticks, 2:62), strikes with his sitar (`atk_brush`),
  rushes (`atk_rush`), dances (`atk_dance`), rains (`idle_rain`) and plays his final notes (`atk_final`); Roxas's
  blows take him down. His target is the player (1:121, mode 2: read as himself before, he stood at no distance from
  it and never chose his far moves, `move_target`, `atk_rush`). An attack sized with a height of 0 is a sphere (his
  notes, 150 by 0, never hit before).
- **His water clones** (`M_EX880_DANCER`, a mod kind: `00objentry.bin` 1184, made by 1:0): `move_target` makes them
  near the player (up to three, his work's + 0x6c) only while his round is the hard one (his block's + 0x18 1; his
  mission's thread makes it 2), and his notes' thread makes one where a note lands (2:39, half the time). Their fight
  is KH2's mission ("KH2's missions" above: `HE17_MS105`, ten clones in 60 seconds), which the port runs now: seen,
  Demyx's hard round, his clones made and defeated, counted, the time up and his `atk_final_failed`. Each clone finds
  Demyx by his entry (1:210, 4002) and makes a vacuum where it stands (2:77, 20, letting the player be) that pulls
  Demyx and the other clones (2:77, above). Their motions are baked (every slot of `M_EX880_DANCER.mset` a
  `MotionRAW`: docs/KH2_MODELS.md), which the port did not play: their pose was none and their model left undrawn,
  only their stand-ins' shadows seen. Played now; their bodies drawn as KH2's subparts say -- translucent, two of
  them additive (drawn as light) -- their water's textures scrolling (their UVSC speeds): seen, see-through water
  figures dancing with Demyx's sitar.
- **Wild Dance** (397, a clone's; then 504, its finish): its zone (group 8, 2.5 m round it) is on in its motion 202
  from frame 35 (range 4), and its state 0x1369 offers it each tick (1:165). Its message handler has the player play
  its slot 252 (AD20: the clone carried in his hand, its 1:18) and the clone its 241 (its swing, `atkp` 1326 and 1327,
  the player's team: above); the player's motion offers 504 again (Cerberus's way); at the third press its slot 253
  (AD21) and the clone's 242 (1328, 1329), `rc_dance_finish`. Seen (a recorded walk's press, by chance at frame 6347):
  397 from a clone's zone, 504 twice, the finish, its swings striking Demyx and the clones round it.
- **Dance Counter** (502, Demyx's own): his zone (group 3, 1.5 m round a point 1.65 m from him) and his message
  handler (6:46's stop, the player attached, 1:29 his slot 253) are there, but in this Demyx (`B_EX120`, the Olympus
  cups') nothing turns the zone's group on (no range 4 in his motions, no 1:52 in his script) and his set has no slot
  253 (212 slots): only Hollow Bastion's (`B_EX120_HB`) offers it. Not offered, as in KH2.

### Port Royal and Axel (2026-11-13)

The **Undead Pirates** A, B and C `B_CA020`, `B_CA030`, `B_CA040`, **Captain Barbossa** `B_CA010` (speaking from
`barbossa`), the **Illuminator** `B_CA000`, **Axel** `B_EX110` (`n_axel`: 12 of the 19 lines his script plays are
recorded there, his battle's; `n_axel_tt04` holds others, the prologue's), their scripts answered in full by the
calls above (Jafar's among them, below) (1:40, 2:2, 2:86, 0:82, 0:91, 1:250, 1:347, 1:251, 1:257 to 1:260, 0:31, 0:88,
2:70, 6:45, 6:62, 6:63, 1:85, 1:86, 1:164).

- **The moonlight**: a pirate (its entry's + 0x48 bit 5) is in its *shade* -- its cursed form, which Days's hits do
  not hurt: the Undead Pirates' hit routine takes only its revenge then, and sets its element 6's rate to 0 -- while
  its `OBJ::PIRATE` Place is 1. KH2 makes it at 1 and its move on the ground (`OBJ::move_at_bg`, the PS3 build) sets
  it from the floor's polygon: 0 on one marked 1, 1 on one marked 0. Days's rooms have no such floors, so the port's
  pirates stand out of it: `1:40` is 0, they take hits, and their model's parts 10 to 19 are left undrawn, the rest
  shown, as `OBJ::exec_pirate` fades them at Place 0 (`PARTS_COLOR` of 0xffc00 to none, of 0x3ff00000 drawn).
- **Their hits**: `2:2` is KH2's small hit (`FUN_1403d4260`): on the ground motion 10 from the front, 12 from behind
  (11 and 13 for a hit of another kind, the other of the pair when the set lacks one), slid by the hit's push back;
  in the air turned to it and 14 or 15. `2:86`, the push back, is KH2's battle constants' (03system.bin's `pref`
  BAR, its `sstm`: + 0x30, 200) unless the attack has its own (`atkp` + 0xc, 0x7fff none: Days's hits have none).
- **Seen** (the three together, Roxas attacking where he stands): they appear, dash (`atk_dash`), slash three times
  (`atk_thrice`, `atk_might`), throw bombs (`atk_bomb`), stab (`atk_knife`) and shoot (`atk_shoot_normal`), and offer
  their reaction command (361, "Fancy Dodge"'s state `rc_fancy_dodge`); Roxas's blows take them down one by one and
  the mission is cleared. Undead Pirate C circled for ever before (`1:56` was answered the other way round): it
  circles while it is off the screen and shoots once it is seen.
- **Barbossa and the Illuminator** (KH2's `ca10`, the Illuminator behind the player): they share memory 29 (2:21;
  its + 4 "the dark", + 8 set by the first to make it), and while the Illuminator stands its lights turn the objects
  black (a kind 1 light to 0 0 0 over 40 ticks) and Barbossa fights in the dark (`btl_dark`: back off, roll, wait);
  his mission `CA10_MS107` runs with him. The Illuminator's end clears the dark (its `dead`: + 4 0, then Barbossa's
  `hless_dead`), after which Barbossa fights in the light and calls Illuminators again (`hless_call`, after eight of
  his attacks). **Not yet seen**: in two recorded runs (Roxas striking where he stands, then chasing what he locks
  on) neither was struck -- the Illuminator wanders (`footwork`: + 8 still set) and Barbossa keeps away -- so the
  turn of the fight is not seen yet.
- **Axel, seen** (Roxas chasing what he locks on): he moves about (`move_various`, `move_target`), slashes
  (`atk_slice`) and bursts (`atk_burst`, `atk_burst_fast`), and Roxas's blows put him in his revenge
  (`mode_revenge_boss`, `rvg_normal`); his combos, 30 a hit, took Roxas down to his last hit points, then (after an
  Elixir) to none: game over 72 seconds into the fight. His reaction commands' event sequences are read ("00", "10":
  1:164).
- **Not shown**: the screen's fades (0:31 to white, 0:32 and 0:88 back), a model's texture animations (1:85, 1:86) and
  a light's fog (1:257 to 1:260) -- Days's screen, the port's KH2 models and Days's field have none of them yet.

### Shenzi, Banzai and Ed (2026-11-13)

**Shenzi** `B_LK100_00`, **Banzai** `B_LK100_10` and **Ed** `B_LK100_20` (Ed speaking from `ed`, the line his script
plays, 8, among its nine), one script for the three, answered in full with their walk paths (1:177, 5:14) and a
counter's digit (4:48); their mission `LK02_MS102`.

- **Walk paths**: an area's spawn set carries walk paths (8 bytes, a u16 its number and a u16 its points' count, then
  the points, 12 bytes each; "Spawns" above). 1:177 puts an object on one of its set's by number, -1 the first
  (`trap_obj_set_path`: its unit's paths, + 0x9e8, `FUN_1403ff460`; a follower made at + 0x630) and says whether it
  had one; 5:14 gives its point n, n wrapping round the count (`Ti::trap_get_path_point_pos`, `FUN_140550240`; w 1).
  The mod tool writes a boss's (its kinds list's tenth column: "number x y z ..." each, apart by ";") from the first
  battle whose own set has some -- the hyenas' second fight, `lk05`'s `b_60`, 33 points round the graveyard, the
  player in another of its sets -- in the boss's own frame as its companions; the port places them from where the
  boss was set going, turned as it then faced its player (`World::walk_path`). Without a path 1:177 is 0, and 5:14
  gives the object's own place (KH2 leaves its argument as it was).
- **Their script** runs to the path's points (5:14) while + 0x128 of its work is 1 (set as it begins and after a
  hit's revenge). 4:48 sets digit n of a counter of their
  mission (its count + (digit - (count / 10^n) % 10) x 10^n); they set it by their unit's argument (1:39), 0 in
  their first battle (`lk02`, theirs in the port; "Bosses bound to their rooms"), so it is not reached.
- **Seen** (the three in the coliseum, Roxas standing, then chasing): each runs off to its path's points
  (`escape_run`) -- the graveyard's path lies past the coliseum's floor, so they stop at its edge (kept off the
  ledge) -- and waits there (`idle_wait`: until the player comes within 8 m). **Not yet seen**: their attacks; no
  recorded run went near them. A room as large as KH2's graveyard keeps their path on its floor.
- **Simba** (`N_LK020_BTL`, a friend in battle KH2 places with them in `lk02`) is not one of the mod's kinds yet: his
  script asks 1:126, a motion kept in step with another's (`trap_obj_motion_sync`: `FUN_1403c8e90` chains one motion
  controller after another's, + 0xd8 and + 0xe0), which the port does not play.

### The data battles (2026-11-13)

KINGDOM HEARTS II FINAL MIX's data battles are their bosses' Limit Cut objects (`_LV99`), each with its own script and
battle entry, fought in the areas `eh10` to `eh20`, `tt20`, `bb15`, `hb04` (their missions `EH15_MS104`, `TT20_MS603_RE`
...). The mod makes those whose calls the port answers:

| Kind | Name | Voice | Its battle |
| --- | --- | --- | --- |
| `B_EX110_LV99` | Axel (Data) | `n_axel` | moves (`move_various`, `move_target`), slashes, bursts, his assault |
| `B_EX130_LV99` | Xaldin (Data) | `n_dragoon` | his flight (`fly_walk`), lances (`atk_brush`), aero, wind; its mission `BB15_MS203_RE` |
| `B_EX170_LV99` | Xemnas (Data) | `n_xemnas` | his normal mode: shield and counter (`move_shield`, `atk_counter`), combos, warps; his hard and super hard modes as his health falls (`mode_check`) |
| `B_EX120_HB_LV99`, `M_EX880_DANCER_LV99` | Demyx (Data), his water clones | `n_dancer_hb` | `HB04_MS403`: ten clones in 60 seconds (his unit argument 1), his final attack when time is up |
| `B_EX160_LV99`, `B_EX220_LV99` | Saïx (Data), his claymore | `n_berserker` | `EH15_MS104`: his berserk gauge filling (4:9), at its most (137) his berserk (`change_berserk`: 1:75 his slot 161), its claymores (1:73) |

- **Set going on its floor**: KH2's spawns put their objects on their points on the floor; Days sets a wave's
  stand-in going sunk into its ground and lifts it out at its next frame, so a KH2 enemy began half a metre above its
  floor and fell, its landing's motion (5) taking its first's place. Data Demyx opens with `atk_final`'s 209, whose
  frame 132 (trigger 9, 1:151) calls his clones: he stood for the whole mission. The port now puts a wave's KH2 enemy
  on its floor the first frame Days stands it out of its ground.
- **1:75 / 1:76 / 1:77**: `YS::MOTION::hook` -- a slot of the motion set of type 0x14 (Saïx's 161: 234 entries, his
  berserk motions by number) looked in first for each motion (`get_motion`: the hook's entry when it has one, else the
  set's own), `unhook`, `is_hook`. Saïx hooks it as a motion ends and starts another at once.
- **1:73** hovering (`trap_obj_hover`): class 6, `ACTION_HOVER`, whose movement is `ACTION_FLY`'s (1:58's). **1:91** an
  object's stars (`trap_obj_star`, + 0xd70; the PS3 build's `PARTY::star`): the claymore's wielder's dizziness -- Days's
  players have none. **1:348** a sheet's hitback addition (+ 0x1c8), which its object's slides add to their speed (2:3).
  **2:24** an attack's owner (+ 0x10): what 2:10 was given, a Days hit's the player who struck.
- **Roxas** `B_EX390` (`n_roxas`): his FINAL MIX fight and his data battle are one object and one script, `eh21`'s
  `b_40` and `b_80` -- the data battle's program sets its battle level to 99 (the ARD's operation 0x1e, `BattleLevel`;
  0x1d is `Recov`), which no script of these reads (1:361 unasked): in the port the mission's own level makes the
  difference. His keyblades are objects his script makes (1:0: `W_EX010_ROXAS_LIGHT`, `_DARK`, kinds of their own),
  turned about all three axes (8:2) and leaving their trails where they were (1:169). His calls: 1:223 the camera given
  back (the reaction camera, as 1:295), 2:57 a limit's effect on an object (1:21's on it), 6:52 a BGHIT's polygon's
  normal (Days's rooms give no polygon: two more segments beside the first give it), 10:52 a reflecting motion
  (140 to 150), 1:237 the duel's command menu with "The End" ("A command menu given by a script", below). The journal
  names him by the prologue's Roxas (`P_EX110`). **Seen**: his slides, spins, shield, duel and charge, revenge on
  Roxas's blows; (2026-11-14, the coliseum, a walk pressing X and A by turns) the duel's deck saying "¿?" three times,
  "FIN" in one row from the duel's frame 80, chosen with A, then his second phase -- `atk_spotbeam_roxas`, `_sora`,
  `_wall`, `_core` -- and his grab (`atk_torikaeshi`) offering "Finta", taken: `rc_slideturn`, his motion 247.
  "Reversal"'s message (675, 0x36e) starts a thread (2:20, routine 0x3bb) that plays the player's AR10 or AR22 (slots
  252, 255: the side he stands on) turned 15 degrees, asks his own idle after it (1:13) and moves him round Roxas for
  30 ticks: KH2's player code does nothing of its own for it (`FUN_1403a8b10`: exec 675, no Action). Its range 2 from
  frame 0 had let him go in the tick it began (below: range 2 is FLAG_CANCEL).
- **Larxene** `B_EX400` (`n_larxene`): her Absent Silhouette and her data battle, `hb33`'s `b_83` and `b_81` (battle
  level 32 and 99). 2:95 a sheet of her own (battle entry 216), 6:69 the others of her part number hidden as she ends,
  10:22 whether an object is blown away (KH2's blow class: 1:129, 2:1). **Seen**: her clones (`bunshin`), her claws and
  steps. Her journal names no object of hers: her stand-in's name shows.
- **The Lingering Will** `B_EX420` (no voice bank of his own): every call of his script answered (171). 1:367 where a
  step along his facing would end (his 12 m dash's end, 1:220's wall-stopped step), 2:82 to 2:84 and 7:32 his vacuum's
  turning, near range, rate and reach (the turning applied across the pull), 2:96 whether an attack of his still
  stands, 0:81 a pad button pressed this frame (KH2's PS2 bits as Days's buttons: he waits on L1, Days's L), 1:365
  a group of the player's commands turned off (Days's deck keeps its own: none). The object he makes, `B_EX430`
  (80 calls): 2:74 its attack's routine for when it is reflected (`trap_attack_set_reflect_callback`: kept, not run --
  the port does not see Days's guard send a KH2 attack back), 6:11 a target record made the aim of one object nearest
  a point, measured to the shape's surface (`trap_target_searcher_search_obj`). `--only B_EX420 B_EX430` makes a mod
  of him. **Seen** (2026-11-14, the coliseum, a walk pressing X and A by turns for three minutes): his first mode
  ("SWORD & KNUCKLE") -- `atk_sword`, `atk_knuckle`, `atk_whip`, his high-speed dashes and their combos, his revenge --
  his effects and his 96 sounds, the player struck from 359 to 169; `B_EX430` not yet made by then. Until
  2026-11-14 the mod tool took the objects a kind's script makes only for the bosses bound to their rooms, so the
  installer's mod lacked `B_EX430` and his KH2 mission (`DC07_FM_NAZO`).
- **Zexion** `B_EX370` (his Absent Silhouette and his data battle: hb34's `b_80` and `b_81`; voice bank `n_zexion`),
  with what his script makes (1:0, through a routine given the number: the tool reads the stacks past KH2's calls
  with their shapes from its executable, and from the routines states and threads begin at): his illusion
  `B_EX410` ("Sora (Book)" in KH2's commands: it takes the player away, motion 255, holding him) and his nine books
  `B_EX380` (meteors, storms, tornados, shields). Calls: 1:318 and 6:70 his own sounds (KH2's sound id; the book's
  pages 391053, a loop 391052 faded by 6:71 -- `sound_shots` now loop and fade), 0:104 the area's background sounds
  (none of KH2's in Days's rooms: nothing), 1:42 and 1:43 the control of a type of object taken and given back
  (`YS::OBJ::ControlOff` / `_OVR_is_control`, type 0 the player: Days's players locked as Days's own lock locks them,
  a join in `Ov022_TranslateCommandToAction` -- as he dies), 1:187 and 1:188 the prize drain (`YS::STATUS::IsPrizeDrain`:
  each prize taken where it lies; a join in `Ov002_CollectNearbySpots` takes every one for the frame, homing to the
  player), 1:272 the dead mark (+ 0x9b8 bit 2, which an enemy's end now sets too), 1:305 and 1:306 the camera's eye
  and the point it looks at (Days's camera's last commit), 1:359 and 1:360 the command menu in his own pictures
  (his book in place of Days's deck: "A command menu in an enemy's own pictures" below), 10:56 a friend's warp point
  (none in Days). His .a.us's HD pictures are PNGs (-0 to -34), not DDS as other objects': the mod tool copies both,
  and the effects' pages are read from either (they had been left out: his effects drawn in their PS2 pictures).
  **Seen** (2026-11-14, the
  coliseum, level 99, a walk pressing X and A): his capture (`atk_capture`: the illusion takes the player,
  `success_capture` shouted), his illusion (`start_illusion` at the illusion's trigger 10: the room's light out, his
  voice, his loop, nine books made, the illusion ended and the player given back), his flight (`move_to_fly_height`,
  `fly_target_round`, `fly_target_away`), his lexicon (`atk_dic_start`, then the hit of atkp 2004 opening his book:
  the player's top commands his pages, 714 offered and taken eight times, a page each), the books' meteors. Fixed on
  the way: 1:210 finds Days's players by KH2's player's part (1), 1:13 queues (below), 1:11 on a player gives him
  back, class 5 has no pull, a motion's attack stands for 2:96, a script's own "dead" is counted (dead_mark).
  **His pages** (1:237 each tick while his book is open, four commands that change every 8 ticks: 724 "Horror", 726
  "Deceit", 712 "Soothe", 727 "Doubt", 713 "Banish", 765 "Jackpot", 711 "Unlock", 766 "Heal", 729 "Hide", 767
  "Spirit", 732 "Ruse", 768 "Bounty" ...; 714 "Turn" the reaction command) stay on the menu after his illusion lets
  the player go: the deck showed them only while an enemy held the player (Roxas's duel), so free the player saw
  Attack, Magic and Items and A attacked (2026-11-14); now held or free ("A command menu given by a script").
- **Luxord** `B_EX150_LV99` (his data battle; voice bank `n_gambler`, his KH2 mission `EH14_MS103` and its gauges),
  with his cards `B_EX210` (1:0) and the two forms his games turn the player into: his card `M_EX950_CARD` (object
  entry 0x601) and his die `M_EX950_DICE` (0x602), carried by the mod tool as what his script turns the player into
  (`form_numbers`: 1:242's argument). His last seven calls are the player's forms ("A player turned into a form",
  below): 1:242 and 1:243, 6:27 to 6:30, and 4:56 (`YS::EVENT::ContinueControlOff`, reached only while a KH2 event
  runs: none of the shell's). He turns the player when the card game is lost (`rc_card_failure`: the card when his
  gauge's phase is past the first and the script's `pp + 0x6c` is set, else the die) and turns him back before his
  final attack once he can be hit (0x2f85: 6:2 is 12 and 1:225 is 0). His battle is KH2's "Battle for Time": his and
  the player's time gauges (his mission's, "KH2's missions and signals") drain by the hits each takes and his card
  games, and he stands (his damage pattern 9) till his own is out; then his script takes it off and leaves him 1. His
  cards end in KH2's "leave" (`all_card_vanish` and their own): an object a script made (1:0) has no spawner to bring
  it back, so its Days enemy is sent away then (`khdays_enemies_remove`: no defeat, no reward) -- before, each came
  back as a wave's enemy does, dozens of them lying on the floor (the maintainer's screenshot, 2026-11-15).
- **Xigbar** `B_EX140_LV99` (his data battle, his KH2 mission `EH10_MS102`) (2026-11-15): his sniping
  (`scope_start` to `scope_end_self` / `scope_end_player`) as KH2 plays it -- his place 8 m up and 19 m out, the
  scope camera from his head toward the player, its sights his own pictures, his shots and their reaction commands
  ("The scope, the pictures and the screen's shake", below). His thirteen calls: the scope's 1:281, 1:283, 1:312 to
  1:316, the pictures' 1:285, 1:290, 1:315, the screen's shake 6:0, the room's polygons 1:135 and 1:137 (his shots
  glance off the walls), the shots' commands 2:52, 7:34 and 1:166. `change_space` (KH2's room changing its shape
  under him) only moves him and shows its flash (his layout 4): Days's rooms have no other shape. Seen in a window
  run (the coliseum): the scope's opening event cameras ("00", "10"), twelve rounds of aiming and locking on (the
  violet sights, then the yellow), Roxas moving under it, reaction commands 491 and 598 taken, the shake, the
  sights' closing (layout 3) and "20" after. Not seen: his end.
- **Final Xemnas** `B_EX170_LAST_LV99` and **Riku** `P_EH000_LAST` (2026-11-16): "Final Xemnas and Riku", below.
- **Vexen** `N_CM040_BTL` (his Absent Silhouette and his data battle, one object; voice bank `n_vexen`, his KH2 mission
  `HB32_FM_VEX`) and his **Anti-Sora** `P_EX100_HTLF_BTL` (2026-11-19): his mission's gauge (0 to 100) and count (0 to
  5) at the left. A task of his (`0:47`, `2:20`) lays a disc on the floor (his element 3) that homes on the player
  (its own steering in the script: `1:140` on its target record, `6:9`, `0:46`, `0:45`, the floor found with
  `6:51`); within 110 cm of him its touch shows (element 26) and the gauge fills by 1.67 / k a tick (k 2.37, then 8,
  9, 11, 14 as the count rises), past 160 cm it lets go; a full gauge adds 1 to the count (4:41: `AddCount(1, 0)`)
  and empties, the fifth sets the gauge's warning (4:57). With a count he summons the Anti-Sora (motion 238, its
  entry 2356, `1:218` its count as its level; `lv1_battle` ...), and gathers it back (`collect`: it leaves). His
  freeze (atkp 1816, his task `0x2da6`): the player captured in his motion 252 (`AR70`) inside an ice block, a
  240-tick timer, then the ice shattered (sound 390049, `2:17` with atkp 1816): the strike's reaction frees him.
  Calls: 4:57; with them 1:140 on a player, 4:5 and 4:41's order (the count first), a held player let go when
  struck by an attack with a reaction -- each wrong before, his gauge never filling past his entrance, his count never
  rising, his freeze held for ever. Seen (window runs, 2026-11-19): the count 1 to 5 in 160 s of a chase, the
  Anti-Sora at 116 s, the gauge's warning at 241 s; frozen at 89 s, the ice shattered and the player free at 94 s.
- **Sephiroth** `N_HB630` (FINAL MIX's, Hollow Bastion's Dark Depths; voice bank `sephiroth`, his KH2 mission
  `HB01_MS601`) (2026-11-19): `N_HB590_BTL`, which bears his name too, is a plain battle template of the story's (22
  states, a 5 KB script, no effects of its own) that never attacks; his fight is `N_HB630`'s (a 27 KB script, 16
  effects). Calls: 1:353 (`trap_obj_start_mpdrive` -> `BTLOBJ::start_mpdrive`): his Heartless Angel (atkp 1697)
  leaves the player 1 HP (1:228, 1:155) and no MP, his MP recharging -- Days's players have no MP gauge: nothing.
  His bank se363 holds a sound of eight-byte references (one of seven tracks at random, FORMATS.md): the tool read it
  wrong and took none of his sounds; 30 of his 34 now (the other four the player's bank 500's). Seen (window runs,
  2026-11-19): his spin slash, split flash, warps, upper and air slash; his grab (AQ91, 72 frames) ended by its
  strike (atkp 1354). His reaction command 664 (offered during his split flash, motion 204) is no reflection (his
  script has no trigger 19; the player's guard in it, AQ90's atkp 1719, a GUARD, reflects no S_ATTACK): its trigger 11
  holds the player in AQ90 (range 27 throughout: `FLAG_STAR`, invincible), the split flash (atkp 1353) lands on him
  all the same, and his trigger 15, the RC under way, enters `rc_success`; his thread waits for the player's motion to
  change. The port skipped an invincible player's hits altogether, so the flash never landed and both stood still
  (2026-11-19, "Attacks reflected": it lands now, with no damage). Seen (a window run, 2026-11-19): 664 taken at
  84.21 s, the flash landing at 84.41 s, `rc_success`, his strike (1354), the player let go at 85.75 s. **Open**:
  warped to the coliseum's edge (5.7, 8.5), his split flash's dash was stopped by its wall and never reached the
  player who took 664 -- held in AQ90 till the run's end (KH2's arena, the Dark Depths, has no wall there).
- **Lexaeus** `N_CM020_BTL` (his Absent Silhouette and data battle; voice bank `n_lexaeus`, his KH2 mission
  `HB33_FM_LEX`: the two power counts, SORA and LEXAEUS) (2026-11-19): his last two calls, 1:358 (his attack level,
  0 to 3: his motions' attacks' records, "Attacks reflected") and 2:26 (his trigger 19 on a reflection by a party
  member's attack: no motion of his). Most of his attacks are S_ATTACKs (his script's own, 1842, 1913, 1931, 1965,
  1966, and his motions' 1852 and 1864): no block stops them. His ATTACKs (1843, 1844, 1881, 1912) are reflected by a
  block or a blow; 1843 alone reflects itself REFLECT, into his motion 140 -- his recoil and counter, which offers his
  reaction command (the others NULL: he goes on); seen in window runs ("Attacks reflected").
- **Marluxia** `N_CM000_BTL` (his Absent Silhouette and data battle; voice bank `n_marluxia`, his KH2 mission
  `HB38_FM_MAR`) (2026-11-19): his doom. `battle_start` (code word 0x4209) reads the player's level (1:84 on the
  player, 1:361: `trap_sheet_level`) and, at its motion's mark 2 (1:151), starts a thread (0x43b7) whose count begins
  there: its figures are his effects 28 to 37 (a digit each, 28 + the digit) over the player's head, laid along the
  camera's left (its way, 1:306 less 1:305, flat, turned -90 degrees by 0:13: 35 apart, about its middle). His trigger
  15 takes 1 off it for each attack of his list that lands on a player not invincible (1:225), and at 0 or below calls
  1:209 on the player: his death (Days's: his HP to 0). The player's hits on him add 13, 7, 3 or 1 (its thread
  0x2b7); the player knocked out and up again (1:272), it stands at 1 at least. Three of the port's calls on a
  player gave the running enemy's own (`self_of`): 1:84 his sheet, 1:155 his health -- Sephiroth's Heartless Angel
  read his own health for the player's and left himself 1 -- and 1:209 did nothing; a player has a sheet of his own
  now (Days's HP and most, his level: his member record's, at most KH2's 99 -- the doom's two figures; a Days player
  at 100 began at "00"). Seen (window runs, 2026-11-19): the count from 99 down by his hits, 1 a hit; with the level
  set to 3 (a local test switch) the player fell at 0 (90.11 s). **Open**: its two figures stand the other way round
  (95 shown "59"): the KH2 enemies' world is a mirror of KH2's (the next fix).

### Final Xemnas and Riku (2026-11-16)

Data Final Xemnas (`B_EX170_LAST_LV99`, his mission `EH20_MS113_RE`) and his battle's partner, Riku
(`P_EH000_LAST`), read in his script and the PS3 build and seen in window runs (2026-11-15 and 16):

- **Two Xemnas**: his battle's spawn places two (the second as his companion), the one whose first unit argument is 1
  seen, the other faded out with no collision (his init). His script finds them by part 4007 (6:59,
  `Ts::XEMNAS::Get(n)`: one value in, the object back -- until 2026-11-16 the port took two and wrote at address 5) and
  queues states for the other (6:58, `YS::ACT::child_push`, entered on it in `ACT::check_start`): his blades' dance,
  the `_child` lasers, and his dark mode (`mode_dark_start`, `mode_battle_dark`: `atk_shield`, `atk_bomb`) while Sora
  is held. So in KH2 one Xemnas is seen most of the fight. Both start their blades at their init (1:87 elements 1
  and 2 on their hands, `trap_obj_effect_start_bind`), the double fading out at once (1:19 over 0): the blades' elements
  are bound and fade with their object (their flags + 0x10 bits 0 and 11: `ryj::PAX::MANAGER::set_obj_fade` sets
  the effect's alpha to `YS::OBJ::get_fade_alpha` each tick, 0 while `is_hide`), so the double's are not seen till it
  fades in (`run3`, its dance). Until 2026-11-19 the port drew them all the same: two blades in the air.
- **Riku**, KH2's party member (the area's `W_FRIEND_ONLY` partner, the battle's `FRIEND_1`; the tool makes him a
  companion), moved by **KH2's party AI** (`kn::Friend`: `friend_param_data` -- 0x62d0 bytes, read from
  KINGDOM HEARTS II FINAL MIX.exe by the tool -- his follow and battle ranges, `PREF_PARTY` "prty" for his speeds, his
  9 actions; `battleAction`, `followMove`, `ACTION_IDLE`). A party member's motion set holds four of each motion
  (`YS::MOTION::id_to_num`: N at 4 N plus the first of its mode's order; mode 2 out of battle, 1 in it, turned over
  with his weapon); a slot it is hooked to (1:75) is numbered so too (251 -> entry 1004, whose 197 is 788). His
  **weapon** (`YS::WEAPON`): `W_EH000` with the set `03system.bin`'s wmst names for his weapon part 65
  (`W_EH000_LAST`), on his hand's bone (sklt's character 19: bone 163), its motion his own number and time, its
  attack ranges his blows on Xemnas (the damage patterns' 2 applies to them: the attacker not the player). His AI
  stands aside while a script's thread holds him (`PARTY::exec_hook`) or an event plays.
- **The laser dome** (`atk_crowd_laser`, `rc_crowd_laser`; `ryj::LAST_XEMNUS_LASER`, 8:3 to 8:7): 405 lasers on 15
  rings; it fires (8:4) once his camera "60" is over (1:34), each laser at the player or Riku (`LASER::attack`: Riku
  while the player plays the dome's 270 or 271, the player while Riku does, else KH2's chance, its sign the player),
  bouncing off one in a Reflect motion (650, offered by 1:165 every tick -- "Reflect", Sora's 254 to 256 and Riku's
  in the same slots, not yet in their loop: `LASER::check_obj_hit`'s 2 m sphere; 374065 for the player, 374066 for
  Riku) or striking him (its 50 cm sphere: `LASER::strike`, atkp 1143); it ends 1800 ticks after (a timer his offers'
  thread counts, his player threads' check: `crowd_laser_finish`, 8:5) and he dies (`rc_crowd_laser_finish`, `dead`).
  Its sounds (374040 the hum, 374047, 374065, 374066) are the engine's own: the tool copies them (`ENGINE_SOUNDS`).
  Each laser is a stick (element 14: `pppRyjMegaBirthUserCtrl3`, the PS3 build's `RyjMegaBirthUserCtrDrawShapeLine`):
  8 sprites of its shape over a metre along its way, 12.5 cm apart from half a metre behind its place, each 6 x its
  size byte / 10 (114 cm), the small white core of `-15` with its red halo, tinted (100, 93, 78), added: seen from
  inside the dome, along their ways, round lights; from the side, sticks. The lists are KH2's world from the caster,
  its y down as an effect's place in the world is (until 2026-11-19 the port turned it over: the dome hung under the
  floor, the lines eight metres of separate dots, the halos their cores' colour -- a KH2 effect's picture is now read
  as the GS reads it, `effect_texel`, not alpha-weighted).
  While it plays **the map is hidden** (8:6, `LAST_XEMNUS_LASER::Optimize`: `dk::Map::setHideGroup(0)` -- every one
  of eh20's 450 draw entries is in group 0, its floor too --, `hideSky`, `Glow::off`; 8:7 shows them again): the
  world black but for the characters and the lasers. The port leaves the room's models out from 8:6 to 8:7
  (`khdays_kh2_enemies_map_hidden`, the models' draws: whatever room the battle is in, a mod's too), or till his
  battle is over.
- **His grab** (`atk_dark`, motion 224, its trigger 7 with 200 at frame 100 -> trigger 17): the player within a metre
  in front is bound (`rc_invitation_to_dark2`, his HP drained), Sora's puppet (`P_EX100_LAST`) made, the player hidden
  (1:133) and put on Riku (1:18), and Riku hooked (1:112 1652) -- the pad moves him (1:113 -> 1:111, 0:81 the jump)
  while the player's command menu is the special one (10:6, `STDOBJ::set_special_command` -> menu 14,
  `RcDarkRikuTable`: Jump 454, Dark Aura 653, Attack 652, in Xemnas's own pictures; its commands sent to him as
  reaction commands: 652 Riku's 197, 653 his 198). "Rescue" (654) frees Sora: offered by Xemnas's zone (group 7, on
  all the fight long), given only while the player's motion follows on from its require 657 (the command's flags 0x40:
  `RC_SEARCHER::get_command_elem`) -- his AQ50.
- **His air fight** (`atk_air_fight`, `rc_air_fight`): Riku rides in Xemnas's AQ10 (1:29: his own part, "0190") till
  it ends, then his AI takes him back.
- **Riku hurt** (`kn::Friend::_OVR_exec_damage` -> `YS::FRIEND` -> `YS::PARTY::_OVR_exec_damage` ->
  `BTLOBJ::add_damage`, `_OVR_exec_damage_reaction`): Xemnas's blows and his attacks' objects strike the spheres on
  his bones of collision type 2 (`YS::COLLISION`'s `TYPE_HIT`; the type a shape's kind's low byte), the lasers his
  body of type 1 (`TYPE_OBJ`, a cylinder 85 high, 40 round: `LASER::check_obj_hit`'s `get_collision(1)`). His health is his character's in `00battle.bin`'s plrp (13: 100), full
  (`SHEET::AllocFriend`). The damage (`SHEET::calc_damage` for an attacker with no sheet: the current level's
  `LEVELPARAM`, lvpm, at the attacker's battle entry's level -- enmp + 2, 99): the level's strength less his defence
  (lvup's at that level: 54 at 99), kept between the level's least and most (10 and 30 at 99), x the attack's power
  x his element rates (100) over a million, rounded up -- 30 x the power over 100 at level 99: a laser (1143, 25) 8,
  a blow of 150 45 --, then the difficulty's for the player's side (`SHEET::adjust_game_level`: the save's
  difficulty -- Beginner 50%, Proud and Critical 200%; the port takes Days's story difficulty). At a quarter of his
  health his script is told (signal 9: trigger 10, `OBJ::_OVR_signal_callback`); he targets his attacker. His
  reaction by its kind (`DAMAGE::GetReactionType`): a slide, a small hit (10 front, 12 back; 14, 15 in the air), a blow
  (16 / 18, thrown up by the attack's + 0xc and along by its + 0xe -- `BTLOBJ::blow`: the way times its w, its y
  less the vector's y --, landing in 17 / 19), a large hit (23 / 24); none while a script holds him. At no health he
  is **knocked out** (`FRIEND::_OVR__dead`: his script told, signal 2; `BTLOBJ::_OVR__dead`: + 0x7f8 bit 4) for his
  spec's stun time (`PREF_PARTY` + 0x18: 1800 ticks for weapon part 65) times his ability's (1), 8 times as fast
  outside a battle (sstm + 0x5c), taking no damage (an attack still stops on him), then gets up (`FRIEND::stun_recov`)
  with his most health x sstm + 0x58 x sstm + 0x54 (a quarter: 25); a cure on him meanwhile wakes him with half of it.
  A party member lies in 54 meanwhile and gets up in 55 (`exec_stun`'s `MOTION::change(0x36)`, `stun_recov`'s 0x37),
  but this Riku's set (`P_EH000_LAST.mset`) has neither (entries 216 to 223 are DUMM): `MOTION::change` finds no motion
  and leaves his own playing, and `kn::Friend::normal_exec` acts whenever his motion grants control (the motion's flags,
  + 0x160 bits 2 to 4 and 30) -- so this Riku fights on knocked out, unhurt, and only a member that lies waits. Over
  his head while he is free (not in a reaction nor an attack, on the ground) KH2's system effects' 14 plays, his
  stars (`ryj::PAX::start(YS::SystemPax, 14)`: 00effect.bar's PAX, "syst"), placed each tick on his collision of type
  16 (`TYPE_STUN`: his bone 98, his head) and closed (`EFFECT::loop_end`) once he is not; the tool copies
  00effect.bar. His abilities (plrp's 0x1b4, 0x1b6, 0xbb: ability flags 150, 152 and 46, his cure's) hold none that
  keeps him from falling (flag 127, `PARTY::_OVR_is_withstand`).
- **What the port does not do yet**: his health is shown nowhere -- by choice (2026-11-19): the mission is played
  in multiplayer too, so no party gauge; Sora's level, which KH2 gives his party
  (lvup), is the battle's (at level 99 his defence would change the damage only past 56, which his 54 never reaches:
  the same at any level); his event cameras
  ("50", "60", "61") are placed for KH2's arena and look elsewhere in Days's rooms.

### The scope, the pictures and the screen's shake

Xigbar's sniping, read in his script and the PS3 build (2026-11-15):

- **The scope camera** (1:281 `trap_camera_begin_scope` -> `dk::ScopeCamera::create`: `ObjCamera`'s mode 7): the
  object it looks from and its eye's height over the object's place (Xigbar's 200: his head), its zoom (0.5),
  close-up distance (10; his 1:313 150) and target ((0, 0, 0)) as globals; 1:312, 1:313, 1:314 set them; 1:283 ends it
  (`ObjCamera::returnMode` while the mode is 7). `ScopeCamera::update`: a point the close-up distance from the target
  toward the head, the eye that point x zoom + the head x (1 - zoom) (`Axa::interVector(a, b, t)` = a t + b (1 - t)),
  looking at the target. His script moves the target toward the player's bone 140 (1:83) by up to 15 a tick, eases
  the zoom to 0.4 (to 1 locked on, 0.2 shaken). The port: the KH2 enemies' camera (`khdays_kh2_enemies_event_camera`)
  gives it when no event of the player's plays, the camera's own field of view kept. 1:83 on a player (Days's has no
  KH2 pose) gives his aim point -- before, the running actor's bone: the scope looked at Xigbar's own chest.
- **Where a point shows** (1:316 `trap_camera_get_projection_pos`): `kn::RotTransPers` through the camera, then
  1792 + 256 off x and 1840 + 208 off y: KH2's dots from the screen's middle (y down). He locks on when the player
  shows within 50 across and 30 up or down. The port projects through Days's camera's last eye, look-at and vertical
  field of view onto KH2's 512 x 416 screen shown at 4:3.
- **His pictures** (1:285 `trap_obj_show_picture` -> `YS::STDOBJ::show_picture`: his archive's first entry of type
  0x2e, `YS::BINARC::get_info(0x2e, 0)`, a BAR of a LAYD and an IMGZ, as a `dk::Layout` of a layout number; 1:290
  `Tz::Layout::setNum`; 1:315 `dk::Layout::setPos` -> `YI::LAYOUT::SetOffset`, whole dots). The LAYD
  (`YI::LAYOUT`): its screens (24 bytes: the picture, the sequence unit, the sequence's clip, the frame it starts at,
  x, y), its layouts (20 bytes: a u16 first screen and a u16 count), its sequence units (a SEQD's offset each). A
  layout's time starts at 0 (`SetNumber`: `ChangeNumber`, its time the longest of its sequences', looping if one
  loops); each screen's sequence plays once the time reaches its frame (`Control`); the screens draw at their places
  plus the offset (`Draw`); a layout over (`GetPlayMode` 0) ends the picture (`Tz::Layout::update`: no next number,
  the object ends itself). Xigbar's: five screens at (256, 208), layouts 0 to 4 one each (clips 0, 1, 4, 3, 2):
  0 aiming (violet), 2 locked on (yellow), 1 after a shot, 3 the scope closing, 4 the room's change's flash. The port:
  ui/kh2_pictures.cpp plays them and draws them over the 3D (a sequence polygon of the 3D layer, KH2's 512 x 416 dots
  the 4:3 picture's), from KH2 PC's HD pages after the archive's effects' (`kh2/fx/<name>/-19`, `-20`).
- **The screen's shake** (6:0 `Ts::trap_camera_shake` -> `ryj::SHAKE::Start`: life, the x and y waves' periods, their
  sizes and their change a tick; Xigbar's 90, 6, 4, 10, 12, -1/9, -2/15): `ryj::SHAKE::exec` a tick moves each wave's
  count on (back by its period), its size by its change (0 to 16), the shown frame's offset
  sin(count / period x 2 pi - pi) x size (`dk::DispFrame::setShakeXYoffset`), its life down. The port moves the KH2
  enemies' camera (the scope's, an event's) across its view so what it looks at moves by those dots; Days's own
  camera is not shaken.
- **His shots' reaction commands** (2:51 on the shot, 7:34 its owner, trigger 11 with the command's record): 2:51
  keeps what it is given at + 0x114 (the PS3 build's `ATTACK_MAGIC::set_reaction_command(void *, ...)`: his shot's
  thread's block) -- the port had kept an object there; 2:52 (`trap_attack_rc_receiver`) gives it back, 7:34
  (`trap_attack_set_rc_owner`) puts the object the command goes to at + 0x118, 1:166 (`trap_target_attack`) reads
  the attack at a command's record's + 8 (`TARGET::set_attack`): the port's record of a command an attack offers names
  it there. His 491 and 598 then send the shot back.
- **The room's polygons** (1:135 `trap_bg_cross_pos` -> `YS::BG::GetCrossPos(from, to, out, flags)`: the polygon a
  segment meets, -1 none; 1:137 `trap_bg_get_normal` -> `dk::Octree::getNormal`): the port's polygon is Days's
  collision face's record (the nearest of its three lists'), its normal the face's plane.
- **The control in an event**: `YS::EVENT::start` takes the player's and his party's control away when its event's
  flags say so, `YS::EVENT::phase_end` gives both back (`YS::OBJ::ControlOn(0)`, `(1)`) unless 4:56 kept it off.
  Xigbar's `scope_start` takes it (1:42) and leaves it to his event's end; the port gives it back when an event
  sequence it plays ends (it had kept Roxas from acting for the whole scope).

### A player turned into a form

KH2's `YS::CHANGEFORM::Capture` (1:242, the PS3 build's names) turns Sora into an object entry's form -- his form 12 --
and `CaptureEnd` (1:243) gives his form before back; 6:2 (`Ts::trap_player_get_form`) answers 12 meanwhile. The port
makes the form's object an ally (no Days enemy stands for it) at the player's place, hides his Days model (his node's
flag 0x20, which `Ov022_ActorUpdate` reads before it draws; RUNTIME_MAP.md section 37) and his weapons, and puts him
where the form stands each frame; his Days control
is locked as Days's own lock locks a player (`Ov022_TranslateCommandToAction`, prepare.py), his pad read first for the
form (`khdays_kh2_enemies_captured`: + 0x18 pressed, 1 the command and 0x800 the jump; the D-pad's heading,
`Ov022_HeadingFromHeldDpad` -- the stick's with analog movement -- plus the camera's + 0x478). On the host, as every
player's control: a guest's pad turns his own form.

- **The card** (6:29: `change_action(0x2e)`, `Ts::ACTION_CARD`; 6:30 its seven numbers, Luxord's 4, 8, 0.069813, 0.95,
  0.03, 50, 3): its walk and run speeds and turn a tick (its start; the bit 12 of its damage pattern set -- the wiki's
  "hits from behind deflected", which Days's hits on a player do not read: not done), its velocity eased toward the
  way at its run speed by 0.95 a tick (`YS::OBJ::calc_inertia`: a lerp; none held, kept 0.97), swept against the
  walls, and kept 50 cm over the floor, 3 cm a tick toward it (`Ts::ACTION_LIB::MainteHeightFromFloor`). Its command
  is its spin, motion 201 (its range 10 strikes with atkp 1310).
- **The die** (6:27: `change_action(0x2b)`, `Ts::ACTION_DICE`, an idle; 6:28 its four, Luxord's 0, 0.087266, 40,
  3.5): it turns toward the way and moves on the ground only by its first number's share (0: it moves by jumping
  alone, the wiki's "unable to move normally"); its jump (`ACTION_DICEJUMP`, KH2's jump at 40 cm) goes along the way
  at the form's run speed (prty 55's 8), its command the same jump 3.5 times as far (`party_jump`: Dice Attack).
  Its jump and fall motions (3, 4) strike with atkp 1309. KH2's `prepare_jump` arc is not read: a plain throw to the
  height, its velocity along the ground the jump's through the air (it had only gone up and down: the maintainer
  saw it as Roxas never turned, 2026-11-15). Without a way held it jumps where it stands.
- **Its time** is a form's: `YS::SORA`'s constructor for form 12 starts two of the drive gauge's bars
  (`SHEET::start_drive(2, -1)`, 600 ticks each: 03system.bin's sstm + 0x68), down a tick each tick
  (`PLAYER::_OVR_pre_exec`) and by each landed blow's AddDrive (atkp + 0x29, 10 for both) times sstm + 0x74 (15)
  (`PLAYER::_OVR_attack_callback`). `SORA::_OVR_exec` turns him back once it is out and Revert (command 5) can be
  decided -- not in the air: the wiki's die that keeps jumping stays one.
- **Their blows** are the player's: the forms' attacks are of team 0, their own -- KH2's player -- so the port strikes
  the KH2 enemies with them (`strike_enemies`, the player's blow: power 25 of 100, the wiki's x0.25), never the
  players.
- **Out of the hands that held him** (2026-11-15): the card game holds the player (Luxord's motions 252-254) when it
  turns him; `CHANGEFORM` makes KH2's player anew, so the port lets the hold go as the form is made -- the die's
  script let him go itself (1:11), the card's did not, and he stood held, unable to act, after the card was over.
- **Kept off the enemies, back when he falls, his weapons hidden** (2026-11-15): the form stands out of every KH2
  enemy's Days sphere by the player's own body (`keep_off_enemies`, as Days's collision keeps its players off its
  enemies -- the card game holds him in front of Luxord, and the die was drawn inside him); a player whose HP runs
  out while a form is himself again, so that Days's fall and game over show him; and his two weapon slots, which the
  hidden bit does not reach, are left undrawn (RUNTIME_MAP.md section 37).

### Bosses bound to their rooms

**Left out for now** (the maintainer, 2026-11-13): the mod tool makes none of these bosses (`ROOM_BOSSES` in
`tools/mods/kh2_enemies.py`, False; its log names each one left out), since a room made from their own map still lacks
what KH2's does for them (its conveyors, its objects' own collision). The port keeps all it does for them, below,
for when they come back.

Some bosses fight with their rooms' pieces: their scripts find objects by number (1:114, `trap_obj_search_by_entry`)
that their battle's area places as it loads. The port makes such a boss **only in a room made from its own KH2 map**,
where those pieces are; anywhere else the Days enemy its wave names stands in for it (the log says so once, and the
studio's mission check reports it: `kh2:<name> fights only in a room made from KH2's <map>`).

- **Which**: the mod tool binds a boss to the area of its first battle ("b_..") when its script finds one of that
  area's field objects (F_, but those the mod lists as kinds of their own) -- the kinds list's eleventh column, its
  home map, and its twelfth, its room's objects: those it finds and every field object of an enemy's kind the area's
  spawns place, each "name x y z yaw argument" at KH2's place on the map. The field objects its script makes by
  number (1:0: Oogie Boogie's gift boxes, F_NM100 and F_NM110) join the mod's kinds, bound to the same map.
  **Shadow Stalker** `B_BB120` and **Dark Thorn** `B_BB110` are bound to the ballroom (`bb05`: the pillars F_BB060,
  the floor F_BB080, the chandelier F_BB050, Dark Thorn's F_BB130), **Oogie Boogie** `B_NM000` to his manor (`nm09`:
  his booth F_NM080, its lift F_NM090, the conveyors F_NM140).
- **The room made from the map**: the studio's `--add-room` from KINGDOM HEARTS II's collection writes the added room's
  `room.txt` (`source kh2:<map>`, its `scale`, `turn` and `move`); the port reads it (`khdays_missions_room_source`:
  the mission's mod's room first, then the other mods'), takes a boss in that room as at home, and places its room's
  objects as the room places the map (its model axes x, -y, -z, then move + Rz Ry Rx scale). A room the studio made
  from a model has no `room.txt`.
- **The room's objects** are made with the boss as allies of its battle (KH2's objects that are no enemies, below):
  drawn, their scripts run, the boss's shouts reach them (1:90 to every actor), the pillars and the chandelier take
  his possession; they are not struck (Days's hits find enemies only).
- **Unit arguments**: an area's spawn record holds its object's arguments (+ 0x20, YS::OBJ's `UnitArg[1]`, which
  1:39(self, 1) gives its script). The kinds list's thirteenth column is a boss's own, from its first battle; each
  room object carries its own. Oogie Boogie fights only with 1 (his start sets him to idle for ever otherwise); the
  chandelier has 1; the hyenas' first battle (`lk02`) gives 0, their second (`lk05`) 3, 2 and 1.
- **Carried by an object** (1:18 on a KH2 object, not only a player): Oogie Boogie is put on his booth (F_NM080, its
  place, flag bit 0) and goes where it is, its floor his (Days's rooms hold no KH2 object's own collision). With flag
  bit 0 the carry lasts while the carrier's range 13 is on: read on a motion held at its end too, as KH2 reads ranges
  on the motion's frame (the booth holds its motion 0, range 13 over all of it), and kept while the carrier plays no
  motion yet (one made with the battle, where KH2's had stood since its area was loaded).
- **Seen, Shadow Stalker** (KH2's ballroom, Roxas chasing what he locks on): his whole round -- the floor's swallow
  (`hyoui_floor`, the floor's `atk_swallow`), the chandelier (`hyoui_chandelier`, its `down_shock_hyoui` and
  `atk_fire`), the windows (`hyoui_window`) and the pillars' claws (`hyoui_pole`, `atk_craw`) -- and Roxas's blows
  (`revenge`). The pieces he possesses are not struck: each possession ends as its time runs out (`hyoui_end`).
- **Seen, Oogie Boogie** (KH2's manor): on his booth he throws gift boxes (`atk_present_box`: F_NM100 made, falling
  to the conveyors), rolls dice (`atk_dice`) and swings his arm (`atk_arm`); Roxas's blows put him in his revenge.
  **Not played**: KH2's map moves what lies on its conveyors (1:158, its belt conveyor, a task of the map's), which a
  Days room drawn from the map's model does not run -- the gift boxes stay where they fall -- and the booth's glass is
  its own collision, so Roxas reaches him inside it.
- **Jafar** `B_AL020` (6:62, 6:63, 2:70, 0:31, 0:88 above) is not offered: a giant in his lava, fought from the air.
  KH2's `al05` sets Sora on the carpet (`P_EX100_AL_CARPET`) 35 m above him, beside the carpet's object
  (`N_AL010_BTL`, which his script finds by number and holds in its idle class, 6:62), and his blows and the spots he
  is struck on are at that height. Played (a recorded walk): his body is 12 m round, he spins his blows
  (`atk_spin_blow`) and asks for his junk (`F_AL170`, 1:0), and Roxas, on the ground at his feet, neither strikes him
  nor is struck. Days's players do not fly.

## A player in an enemy's hands

KH2's player is an actor its enemies' scripts move and make play their motions; Days's players are Days's. When a
script makes one of them play a motion of its set (`1:29`, Cerberus's reaction command), the shell holds it
(`shell.cpp`, "the players in hand") until the script gives it back (`1:13`, its own motion 0) or the enemy goes:

- **Days's side**: a join in `Ov022_ActorUpdate` (`khdays_kh2_enemies_holding`, next to Stop's hold) stops the
  player's Days control (its state machine, its animation, its commands: the pad's top button is the command's), and
  puts its actor where the script and the motion have it (its place, + 0x48c and its node; its facing, the node's +
  0x80: Days's model looks along (sin, 0, cos) of it, as KH2's yaw).
- **The motion**: its '0010' variant's time on (1 tick a frame), its root's travel turned by the yaw moving the player
  (as an enemy's), held at its end (the ended bit `1:14` waits on). It always starts at its first frame: 1:29's
  fourth value is its **blend**, not a frame (the PS3 build's `YS::trap_obj_motion_capture` -> `YS::MOTION::capture` ->
  `_capture`, which starts it with `start_motion(binarc, blend, 0)`; the PC's `FUN_1403c65e0` the same). Above 2 ticks
  the motion waits at its first frame for the blend (`add_time` counts the blend's ticks, then plays the first frame's
  triggers, `exec_trigger(start, -1)`, and goes on), the pose blending from the one before; else it starts at once
  with its first frame's triggers. The Armored Knight's Rising Sun gives 8 (AB50, AB51 again): read as a frame to
  start from, AB50's twelve lights on the player's limbs (elements 5 to 16, at its frame 8) never played. Its ranges:
  20 a reaction command offered (when the command it follows, `require`, is a range 26's), 27 not to be hit (the
  enemies' attacks pass it). Its triggers: 1 the enemy's effect on the player's own bones (its element's bone is KH2's
  player's: P_EX100's skeleton in the pose the motion has then, placed and turned where the player stands from the
  enemy, half a turn more about the up as his model is drawn: the motions for him are made in the enemy's frame, so
  without it his effects stood mirrored on him), 8 the enemy's sound (the mod holds these too: its tool now reads the
  sounds of the player's parts). Its weapon's ('0011' with data of its own) attack ranges: the player's blow on the
  enemy, Days's damage the player's last hit on it times the attack's power over 100 (00battle.bin's atkp; Stop's way
  into Days's hit, `khdays_kh2_enemies_blow`).
- **His weapon**: KH2 hangs the player's keyblade (an object of its own, `YS::WEAPON`) on his bone the object entries
  name (00objentry.bin's P_EX100 record's + 7, a character of 03system.bin's `sklt`: P_EX100's primary weapon bone,
  178); the '0011' part is the keyblade's motion in its own axes on that bone (W_EX010's bone 2 within 4 cm of Sora's
  hand in every frame of Quick Blade). The part's triggers play as the body's (8 its sounds: the keyblade's bank 500,
  W_EX010.a.us's; 1 an effect of the enemy's on the keyblade, `YS::MOTION::_Capture` giving both parts the capturer's
  PAX). Its range 12 is the **swing** (OpenKH's "weapon swing", the motion's + 0x20 bit 11): each tick of his weapon,
  `YS::WEAPON::locus` starts element 10 of the keyblade's own effects while it swings and none runs
  (`start_effect_bind(10, 1, 0, 0)`), and closes it (`ryj::EFFECT::loop_end`) once it stops: its swing's trail
  (W_EX010.a.us: the swing trail's command 0xc8 between the element's two points, below). The port draws it on the
  keyblade the player's KH2 model draws (its `weapon` line's joint times the weapon's pose: models.cpp's `held`
  weapons, `held_weapon_drawn`), else KH2's own on P_EX100's 178. Played on the Soldier's Cyclone (AA01, frames 7 to
  31), Cerberus's last blow (AM03, 10 to 26), Quick Blade (AB10, 67 to 83) and the Neoshadow's Wind Dance (AA90, 0 to
  113); the mod's tool copies obj/W_EX010.mdlx and .a.us with its HD page (mods/<mod>/kh2/fx/W_EX010/). The player's
  KH2 model plays the part on its own keyblade (`weapon_motions`' bones), as it does the body.
- **The script's calls on it**: `1:148`, `1:1`, `1:4`, `1:79` put and turn it; `1:112` its thread (kept from before
  the hold begins); `1:18` / `1:102` an enemy carried by it (at its place and facing; put on him before the hold
  begins too, Scar's). A carry with its flag bit 0 ends by itself, as KH2's actor's update ends it (`FUN_1403b4a12`):
  its carrier gone or its + 0x18c bit 15 clear -- the carrier's motion's range 13 -- so Scar comes off the player he
  pins as he throws him (his 254), and his catch, waiting on `1:161`, goes on; `1:74` nothing (its class is Days's);
  `1:213` 1.
- **The command**: offered, it shows above the command deck (below, "The reaction command's bar") and KH2's mark on
  the player in the enemy's hands (below, "The reaction command's mark"); the pad's top button (KH2's triangle, the DS
  X) takes it: the command's argument sent to the enemy as a message (trigger 11).
- **KH2's text** (sys.bar, our reading of the files; OpenKH's MSG page gives the control codes): 01 a space, 2e-47
  A-Z, 9a-b3 a-z, 90-99 the digits, b7-cb the small accented letters (a grave, acute, circumflex, diaeresis ... u
  diaeresis), d0-e4 the capitals, b5 oe, b6 sharp s, e5 and e6 the opening exclamation and question marks, 48 !, 49
  ?, 4f ., 50 ,, 52 :, 5a (, 5b ), 66 -, 2b +, ec and ee the apostrophe, ed the quotes.
- **Its look**: a KH2 model (the HD PACK's Roxas) plays the motion on its own bones (`model_kh2_motions.cpp`,
  `play_held`): from KH2's player's bones (P_EX100, the mod's kh2/obj/) through the model's `skeleton` by their rest
  places (`kh2::match_by_rest`: Roxas's P_EX110 has Sora's bodies' rest places, renumbered), then its `bone` pairs.
  The motion is made in the enemy's frame: while it carries the enemy (1:18) both stand at one place with one facing
  and no root travel (Cerberus's "00" and "10": the player 0 -12 0 from it in its axes, both yaw 0), and the bones
  alone put the player in the jaws. That frame is half a turn about the up from the one Days's node gives the model
  at the same facing, so the pose is turned so (without it Roxas hung behind Cerberus's head). Days's own model holds
  its last pose (Days has no such animation).
- **Drawn as itself**: a model drawn at a shell's place is taken as the enemy's and left out (its stand-in's other
  parts), but a player character's (ba/ch/) never: a player carrying its enemy stands there (it was left out, Roxas
  unseen in Cerberus's jaws).
- **X is the command's**: while an enemy offers the player a command or holds it, a press of X (the DS's, the pad's
  top button) chooses no Days command (`khdays_reaction_talk_pressed`'s join: kind 7), so the deck's cursor and its
  lists do not move (they did: Magic's list opened at a press). KH2's triangle does nothing but reaction commands; the
  setting "Reaction button for reaction commands only" (the profile's `reaction_button_only`, off; Settings, the
  enhancements) makes X so everywhere: no deck cursor, no lists from it (the deck then moves with "Command menu on the
  D-pad"), L with it still the shortcuts, Days's Talk and Examine on the bar still X's.
- **The mod's files** (the mod tool copies them from the player's KH2): obj/P_EX100.mdlx, obj/W_EX010.mdlx and
  .a.us (the keyblade), 03system.bin, 00battle.bin,
  00objentry.bin (the objects' entries: 1:114, 1:222),
  msg/<language>/sys.bar, field2d/<language>/zz0command.2dd and zz0field.2dd with their HD pictures,
  msg/us/fontinfo.bar with fontimage.bar's HD picture.

## Allies: KH2's objects that are no enemies

Some bosses fight with KH2 objects that are no enemies of the player: the Hydra's Megara, Phil and Pegasus (their
object entries' kind, `00objentry.bin` + 4, is 9, a friend in battle) and the urn Phil hands the player (0x17, a
gimmick). An enemy is an entry of kind 3 (a boss), 4 (an enemy) or 0xc (a large enemy: the Hydra and its heads); every
kind the mod held before is one of those.

- **Where they come from**: a script makes one (1:0: the Hydra makes Megara once its heads are reborn, its work's
  + 0x11e8 3), or KH2's area places it with its boss. An area (`ard/us/<area>.ard`, OpenKH's ARD page) holds spawn
  files (BAR entries of type 0xc, "Spawn Point"): sets of objects, each set a 0x2c-byte descriptor (+ 0 its type,
  + 4 its objects' count, + 6 its event activators', + 8 its walk paths', + 0xa its return parameters', + 0xc its
  signals'), its objects (0x40 bytes: + 0 the entry's number, + 4 the place, + 0x10 the rotation), then the rest. A
  battle's spawn (`b_..`) holds the boss and the player in one set; KH2 places the sets of types 1 and 2 as the area
  loads, those of type 5 being what its scripts make (`he18`'s `b_40`: the Hydra, the player and his party, Phil and
  Pegasus in a set of type 2; Phil's urn in another; a head and Megara in one of type 5 -- the Hydra makes both; the
  other type 5 sets, `M_EX800_RAW`, `B_EX350`, are such too). The mod's tool reads them for each boss (kinds 3 and
  0xc) and lists the others that are no enemies, from the boss, turned so the way from it to the player is + z
  (`kh2_enemies.txt`'s eighth column); the port makes them as the boss is set going, turned to where its player stands
  (`shell.cpp`, `start`).
- **What they are in Days**: no Days enemy stands in for them -- nothing of Days's hits them, locks on them, counts
  them or shows their health. The shell runs their scripts at KH2's 60 ticks a second, moves them by their motions and
  their scripts, keeps them on the room's ground (the ground under them, from their battle's Days enemy's room),
  plays their effects and sounds, and draws them as the 3D list closes (`allies_close`, as an enemy's end once its
  Days enemy is gone). Their scripts' calls on Days's field (its players, its ground) are answered as their battle's
  enemy's (`Enemy::anchor`). Searches by entry find them (1:114, 1:292, 1:323: `World::objects`); 6:5's search for
  aims does not. They go with their battle (its enemy gone and its end over) or when made to leave (1:17).

## KH2's missions and signals

A KH2 boss's battle runs with a mission its area names: the area's battle program that places the boss's spawn
names it too (operation 0x15 of the `.ard`'s script, OpenKH's ARD page: a u32, the mission's id, then its name; Demyx's
`he17`: 75, `HE17_MS105`). The mod's tool finds it for each boss and copies `msn/us/<name>.bar` (the ninth column of
`kh2_enemies.txt`); the port runs it as its boss is set going (`start_mission` in `shell.cpp`), one at a time.

- **The file** (OpenKH's MSN page): a BAR; its entry of type 2 a 0x1c-byte header (its id, its flags: a boss's battle,
  no leaving, no prizes ...; texts, sounds) then operations {s16 code, s16 count, count u32 parameters} till 0 -- 1 to 3
  the cameras of its start, its end and its failure; 4 a timer, 5 a counter, 6 a gauge, 7 and 8 KH2's others, each
  the PS3 build's `MISSION2D::TABLE` as `YS::MISSION::init_mission2d` reads it: {its start, its most, its warning, a
  word: its index (bits 24-31), its side (16-23: `INFO_BASE::ALIGN`, 0 left, 1 right, 2 the middle), its picture's BAR
  entry + 1 (0-15; 0 none)}. A timer counts down from its start's seconds (`dk::Timer::setCountDown`), else up to its
  most's; a counter starts at its start, its most's bit 30 showing it "count / most". Then its script (type 3:
  `ms_boss` for most bosses, its own for Demyx's); its event sequences (type 0x16), a PAX and its pieces' pictures
  (IMGD, 8-bit, its pixels as the GS's memory holds them) each followed by its sequence (SEQD: `ti_e` the timer's,
  `ct_h` the counter's). KH2 PC draws the sequences with HD pictures, one for each IMGD in their order
  (`remastered/msn/us/<name>.bar/US_<name>_bar<N>.png`, laid out otherwise than the PS2 ones: the clock and TIME moved
  where the sequences look for them). `HE17_MS105`: a timer of 60 seconds at the left, a count of 10 at the right,
  down to 0.
- **Signals**: the PS3 build's `YS::SIGNAL::Call(id, argument)` queues one; every object's script (trigger 10,
  `YS::OBJ::_OVR_signal_callback`) and the mission's hear it. An enemy's end sends 75 with its serial
  (`YS::ZAKO::_OVR__dead`: entries of kind 4); a counter reaching its most sends 123, reaching 0 14, with its index,
  while the pieces are up (`MISSION_COUNT::set`); the time up 74 (`dk::timerTask`). The counters' calls: 4:6, 4:20
  and 4:29 the counter's index; 4:5 `trap_mission_set_count` the count, then the index, and 4:41 how many, then the
  index (`YS::MISSION::SetCount(n, index)`, `AddCount(n, index)`, the PS3 build's; until 2026-11-19 the port read
  both the other way round, and Vexen's full gauges never counted). The mission's script counts its count down on 75 (4:20),
  locks itself completed on 14 and failed on 74 (4:54), then ends so (4:3, 4:11; its boss dead 4:16, left 4:22).
  Demyx counts the clones on 75 too, while the mission's bar is up (4:21). The port queues signals and tells every
  KH2 enemy, ally and the mission at the next frame; an enemy's end (its health out, an entry of kind 4) sends 75.
- **Gauges** (op 6; Saïx's berserk, `EH15_MS104`: start 100, most 100, warning 50; Luxord's two): `YS::MISSION_GAUGE`
  holds a float level (+ 0x2c), its start as it is made (`activate`); `4:8` sets it, `4:9` adds to it, a float then the
  gauge's index (`SetGauge`, `AddGauge`), `4:12` gives its level over its most (`GetGaugeRatio`). `MISSION_GAUGE::set`
  keeps it between 0 and its most: reaching 0 from above sends 136, its most from below 137, with its index, while
  the pieces are up. KH2 PC at 60 frames a second (its tick under 2, as the port's) halves some missions' steps
  (`FUN_140433750`: `PO01_MS501`, `TT12_WORK_BOX`, `HB32_FM_VEX`; `TR07_MS203` rising, `TR04_MS104` falling; then
  `EH15_MS104`'s of -5 and above), and so does the port. Drawn as `dk::MISSION_GAUGE` builds it (the PS3 build's
  `create` and `updateGauge`, read): its base (3, then 4), its second (9, then 10: Saïx's claymore), its full part
  (12) scaled across by its level over its most, its empty part (13) by the rest, its tip (14) moved that share of the
  bar's length (14's `ex`) along; its column's clip 16, 17 or 15 by its side. Its bar's parts scroll their picture
  (a SEQD part's scroll: `YI::SPRITE::_Draw` moves its UVs by the scroll times the sprite's time -- the frames since it
  was made -- times the image's size, in 16ths of a pixel, the picture repeating within the part's own rectangle, the
  GS's `REGION_REPEAT`); the port's CPU drawing does the same. Its top standing is its second's last frame's (Saïx's
  opens with a flash far above it).
  Luxord's two (`EH14_MS103`, the player's and his: "SORA [TIME GAUGE VS] LUXORD") stand in the right column at -85,
  which KH2 PC's wide screen moves 85 dots out: the port lays a side piece so met by the middle by the middle of the
  screen, both halves alike. The player's half shows the player playing, the maintainer's wish (2026-11-15): his
  portrait by his HP gauge (his mod character's own, else the game's cells), turned to face the middle, in Sora's
  face's place (its part 0), and his character's name in KH2's font over "SORA" (written on its bar, part 1: the
  bar's plain stretch at 20 to 71 laid over 80 to 131, rows 46 to 61 of its IMGD). They move with the mission's
  trigger 6 (KH2's damage, `YS::UNITSCRIPT::DamageCallback`): the player's by 100 x damage / 4000, Luxord's by 100 x
  damage / 3000, and his card games' results (his script's 4:9); his damage pattern 9 keeps him standing till his own
  runs out (RUNTIME_MAP.md section 37).
  Lexaeus's power counts (`HB33_FM_LEX`, counters 0 and 1: "SORA POW" in the left column, "ER LEXAEUS" in the right,
  "POWER" whole between them in the middle of the screen, each half its counter's part 37 or 36 placed at the middle)
  are two pieces met by the middle: both laid out by it, in one row -- the lower of their columns' tops (the left's,
  under Days's hearts and clock); until 2026-11-19 they stood 13 dots apart and "POWER" broke in two. The player's
  half as Luxord's (2026-11-19, the maintainer's wish): his portrait over Sora's face (part 1; its shadow, part 2, the
  same picture black and half clear, taken out) and his name in the middle of "SORA"'s place ("SORA" a part of its
  own, part 0, taken out; its capitals at 60.75 to 109, rows 35.25 to 48.5 of its IMGD).
- **The port's mission**: its script's main thread (trigger 3) runs at KH2's 60 ticks a second, its timer counts
  while its bar is up, its count as `MISSION_COUNT::set` keeps it; its bar is up from its start (as KH2's after its
  start camera; whether trigger 2 is its start or its end is not known: the port runs it as it ends), its cameras and
  event sequences are not played, and its end changes nothing of Days's mission (a failure does not fail it).
- **Its pieces on the screen** (`ui/kh2_mission_2d.cpp`), as KH2's `dk::TIMER` and `dk::COUNTER` build them (the PS3
  build's, read): sprites of the sequence's clips, each playing its clip and at its end the next or gone (`dk::Sprite`)
  -- the base (3 coming, then 4; 5 leaving; 0 and 1 the warning, below), its second (the clock,
  the note: 9, then 10; 11 leaving), the digits (12-21; 22 a counter's dim leading zero; 23 and 24 the timer's
  separators); a child placed at its base's active point plus its offset (the digits a digit's width, the clip's
  `ex`, apart) and faded with the base's active alpha (`getParentSeqAlphaOnly`). The timer shows minutes, seconds and
  hundredths (`dk::Clock::vCountToHMSMS`: frames times 5/3) and its last seconds in large figures in the middle of the
  screen (31 with two seconds left, 30, 29, then 28 at 0; KH2's system sound 0x2b with each is not played). The
  pieces stand in the side their table names, each below the open ones before it in that column by its base's height
  (`INFO_BASE`), half a DS dot a KH2 dot, under Days's own pieces at the top of that side as KH2's are under its
  INFORMATION window (the hearts and clock at the left, the target's name and health at the right): on one screen
  placed as those pieces are. While Days's INFORMATION banner is up at the top they are not drawn.
- **Warnings** (the PS3 build's, read 2026-11-19): a counter's or a gauge's (`MISSION2D` + 0xc bit 3) goes on and off
  as its value is set (`MISSION_COUNT::set`, `MISSION_GAUGE::set` -> `MISSION2D::check_warning`): with a warning above
  0, on once the value falls to it -- or, the piece starting at or below it, once the value reaches it -- else off
  (Saïx's berserk gauge from 50 down); its script sets it whatever the value: 4:57 a gauge's (`trap_mission_warning_
  gauge` -> `YS::MISSION::WarningGauge(on, index)`: Vexen's, `HB32_FM_VEX`), 4:50 and 4:58 a counter's on and off
  (`WarningCount(index, on)`). On, its base plays clip 0 and its second piece 6 (`dk::COUNTER::alert`,
  `dk::MISSION_GAUGE::alert`), a counter's then 1 and 7 (`dk::COUNTER::alertLoop`); off, 4 and 10 again
  (`releaseAlert`). `MISSION2D::set_warning` also plays the piece's warning sound (+ 0x28: `4:46`, `4:53`) when it has
  one -- not played by the port (no mission it runs sets one). A timer's warning (`dk::INFORMATION::alertTimer`, 4:47)
  is not drawn.
- **Seen** (Demyx, `HE17_MS105`, a recorded walk, Roxas keeping away and striking): the mission set going (a count
  of 10, 60 seconds), Demyx in his hard round (`btl_hard`) making his water clones (`move_target`: `appear_mission`,
  `idle_mission`, `atk_water`, `atk_move`), Roxas defeating eight (each 75: the count 10 down to 2), the time up (74),
  the mission failed and Demyx's `atk_final_failed`.

## Reaction command zones

Most enemies' reaction commands are not offered by a motion the player plays (Cerberus's range 20) but by the enemy
itself: shapes of kind 5 in its `.mdlx` (`Collision`, above), each with its command at + 2, in a group its motions'
ranges 4 turn on. Read against each script's message handler (trigger 11 compares the message with that command):
the Magnum Loader's eight zones along its rush (group 5, on in 52A's frames 0 to 75, 4 m round points 3 to 22 m
ahead: 309, Quick Blade), the Armored Knight's (group 4: 300, Rising Sun), the Soldier's (group 6: 34, Cyclone), the
Dusk's (group 5: 69, Reversal).

- **Offered** (`zone_offer` in `shell.cpp`): while this game's own player's place (+ 0x670) lies within a zone's radius
  of its point (the test KH2's own uses of a zone make, `FUN_14054ae70`: the place's distance, `FUN_140140f50`, under
  the shape's + 0x10). The general check, read later, is KH2's player's searcher (`FUN_140407de0` from `FUN_1404081c0`,
  "A command menu given by a script" below: every object's shapes of kinds 5 and 14); the port takes the test above for
  every zone. The deepest zone's command shows (the bar, and KH2's mark at the enemy's aim point), not again in the
  motion one was taken in; while an enemy holds the player only a command he can take then (`command_usable`: its
  require his motion's range 26 -- Roxas's grab, `atk_torikaeshi`, holds him in AR20, range 26 675, and the zone his
  motion 246 turns on, group 14, offers "Reversal", 675; before, no zone offered to a held player). The press sends the
  command's argument to the enemy (trigger 11, as a held player's).
- **The command's record**: trigger 11's third value is the command's target record (the manager's + 0x10:
  `FUN_1403bee60`'s -- the object, its group, none, -1), not the object: the Soldier's and the Dusk's routines take
  the object from its first word (`1:94` on it, then its place); the port sends one (the enemy, group 0). With the
  enemy's own address instead, the Dusk's Reversal turned the player round the world's origin -- not round it.
- **Every command is its enemy's script's**: KH2's player takes a command (`FUN_1403b2340`, his command manager at
  + 0xdd0: the press, `FUN_1403b22d0` -> `FUN_1403d7f30`, keeps it at the manager's + 8) and runs it
  (`FUN_1403a8b10`: its few special `exec` kinds apart, its journal count, `rcct`, and its `Action` -- 1 the ground, 2 a
  jump), then sends it to its target's script (`FUN_1403b5fa0`: trigger 11 with the player, the command and its
  record). The Magnum Loader's and the Armored Knight's handlers make the player play a motion of their set (1:29)
  and run a thread on him (1:112); the Soldier's and the Dusk's start a thread of their own (2:20) whose routine is
  given through a routine's parameter (the Soldier's 34: 90 pushed, passed on, started) -- Cyclone (AA00 aside 3.6 m,
  then AA01 the spin), Reversal (the Dusk's "DUSK" 241 or 242). The research and mod tools follow such routines from
  their callers' stacks (`kh2_bdx.thread_routines`); they had read 2:20's routine from its second argument (it is
  its first: `FUN_140432cd0`), which hid these threads and the ones of other kinds (the Cerberus's fire, the Cannon
  Gun's, the Strafer's, the Crimson Jazz's, the Assault Rider's; "Coverage"). The port offers a zone's command only
  for a kind whose script makes a 1:29 call in its walked code: the mod tool's list says so (kh2_enemies.txt's sixth
  column, "player").
- **A held player on the ground**: while his motion has no range 3, 24 or 30 (the air classes' pull off) KH2's
  ground class moves him: the port takes only the way along the ground of 1:89's move and 1:111's steering, puts him
  on the room's ground under him (Days's collision, `Collision_CastRay` on the enemy's scene's collision: + 0x4's
  + 0x7c), stops him short of a wall he would walk into (a cast at his middle), and keeps him off a ledge more than a
  step down (KH2's rooms wall their edges; Days's eh platform has none). Before, the Soldier's Cyclone sank him
  (its way down to the Soldier) and then stepped him off the platform.
- **The Magnum Loader's Quick Blade** (AB10, 112 ticks): its thread (routine 133) asks for the player's own idle at
  once (1:13: KH2 queues it on his controller, `FUN_1403c8310`, taken once the motion of another's set gives him back,
  below), turns him to the loader, and on his motion's triggers 9
  (frame 12) and 10 (42) -- bits of his + 0x2ad, as an enemy's -- dashes him to it (1:89 on the player: (distance + 2
  m) / 32 a tick, at least 10, slowing after 42) while it waits on his + 0x18c bit 14 (1:101: he plays another's
  motion). His weapon's attacks (657 at 14, 656 at 69) are the blow on it; his motion's end gives him back, or a press
  of his from its range 2 (from 107, FLAG_CANCEL). A blow on an enemy not hit yet takes the player's last Days hit on
  any KH2 enemy, else 1.
- **Seen** (the Magnum Loader alone, Roxas still, the HD PACK; the press by a test switch, not kept): its charge offers
  Quick Blade ("Estocada rápida"), the press dashes Roxas to it and strikes twice (2 and 4: no Days hit before), it
  comes back into sight as its charge ends (1:19 at its start fades it out, 1:20 at its end in, as KH2's), and its
  next charge offers it again.
- **The Armored Knight's Rising Sun** (300 from its zone in its spin and jump attacks, 51A and 52A; then 427, which
  follows 300, offered by its script): its thread (routine 196) has the player play AB50 (from frame 8), finds what to
  fly him at -- his aim if he has one (1:191, empty in Days), else the nearest aim within 30 m (6:3, 6:8 its own
  passed over, 6:5), else the knight itself. AB50 throws him up (80 degrees, 0:68 then 0:13, by 1:89); AB51 dives him
  at it (1:111: 45 degrees down flattening to 5, at 50 a tick, for 18 ticks, while 1:238 is what 1:29 gave); AB52
  sends him up again (20 degrees rising to 80), his own idle asked for (1:13: taken at its end), 427 offered each tick
  (1:165, up to five times: its count at the work area's + 0x94). A press starts routine 1749, which dives him again
  (60, 70, 80 a tick, steeper each time). His weapon's attack in AB51 (644, power 1000) lands on the knight.
- **The player's own motion asked for** (1:13 on a player an enemy holds): KH2 queues it, so a motion still playing keeps
  him till it ends, or till a press of his (A, B, Y: his attack, jump, guard) while its range 2 is on -- the PS3
  build's YS::MOTION flags name the bit range 2 sets (+ 0x34 bit 4) FLAG_CANCEL, the bits of ranges 0 and 1 FLAG_IDLE
  and FLAG_CONTROL; read as "back to idle", range 2 from frame 0 had let the player go as Roxas's Reversal began -- one
  at its end lets him go at once (Cerberus's). A defeated enemy's thread takes him no more.
- **An enemy's own next motion** (1:13 on itself, `trap_sysobj_motion_push` -> `YS::MOTION::push_queue`): KH2 keeps a
  ring of motions with their blends at the controller's + 0x44 (its ends + 0x3c and + 0x40), and `YS::MOTION::add_time`
  takes the first as the motion playing reaches its end (else the motion goes round its loop); a motion started empties
  it. The port queues it (`Playing::queued`) and takes it in the tick the motion ends (its ended bit, a loop's as it
  goes round) or blends back (range 2), as it did 2:87's idle; it played it at once until 2026-11-14 -- Zexion's
  illusion, asking 201 and then 202, skipped 201, whose trigger 10 (frame 130) calls Zexion on to his book.
- **Seen** (the Armored Knight alone, Roxas attacking, the HD PACK; the presses by a test switch): Rising Sun from its
  spin, then 427 four times more, each a flight and a blow of 490 (its last Days hit's 49 times 10), the knight
  defeated in the fifth.

## The reaction command's bar

KH2 shows a reaction command as a bar over its command menu. The world's menus have their own (field2d/<language>/
<world><n>command.2dd, which KH2FM.exe's `FUN_140154070` loads for a world, `zz0command.2dd` otherwise); the port
draws the general one, `zz0command.2dd`: its IMGD and SEQD named `comw`, with the HD picture
remastered/field2d/<language>/zz0command.2dd/<LANGUAGE>_zz0command_2dd0.png. The mod tool copies both to
mods/<mod>/kh2/field2d/<language>/ (us, fr, gr, it, sp), and `port/ui/reaction_command.cpp` plays them through the
port's KH2 sequence player on the GPU (docs/KH2_SEQUENCES.md), as the obtained notices are.

- **comw's sequences** (our reading of the file against KH2's own play of Cerberus's commands): 1 the bar standing (its
  circle's light at 0.157 of its length), 5 the bar coming (a ring closing on the circle, a light along it; then it
  stands), 3 the bar coming with "COMBO!" over it (a command offered after one taken), 4 the button's flash (a
  triangle growing out of the circle), 0 the bar leaving (16 frames), 7 and the rest the menu's own titles and slots.
  Which of them KH2FM.exe plays when was not read in the executable: seen in KH2's play, the first command comes with 5,
  each after a taken one with 3, a press flashes 4, the command taken stays until its enemy lets the player go, and the
  bar leaves with 0.
- **Drawn** as wide as the deck (84 DS dots), its foot a dot above it; over it, as KH2's, the pad's top button's icon
  in the circle (the player's input's: a PlayStation pad's triangle, an Xbox one's Y, a key) and the command's name
  after it in KH2's own "sys" font, white with a dark edge, fading with the bar. The font is the mod's
  kh2/msg/us/fontinfo.bar ("sys": 280 widths) and fontimage.bar's HD picture (US_fontimage_bar0.png: 28 columns of
  18 x 24 dots over 512 x 256, a glyph's slot its KH2 byte - 0x20, the last the space, 6 wide; grey glyphs KH2
  draws white); its cells 0.62 of the bar's height. Without them, Days's 8-dot font (the HD fonts' route for the
  deck's labels, "field.command"). The setting "KH2 reaction commands"
  (`kh2_reaction_commands`, on) and the HD textures switch it; without its pieces the icon and the name show alone.
- **Days's own commands** on the bar (the same setting): Days offers the thing the player aims at (an actor's + 0x4f0 2
  and its object's + 0x17, `Ov022_GetSubKindIfState2`) as the deck's primary command -- 1 Examine, 2 Talk, 3 Trick,
  4 Bone: UI/btl/<language>/cmd.s's record kind + 5 -- in place of Attack (`Ov002_PanelSelectSource`), and A then
  talks instead of attacking (`Ov022_TranslateCommandToAction`, kind 9: the pending action 2 over 1). Joins in
  prepare.py leave the deck its Attack and put the command on the bar: the reaction button chooses kind 9 with
  argument 1, which only talks, examines ..., marked chosen without A as the L shortcuts' commands are (the actor's
  + 0x4e8 bit 1, which the control packet's bit 24 takes to the host's + 0x4e8 bit 2: without it
  `Ov022_TranslateCommandToAction` takes a command only with A held); the deck's own press while the bar has one gets
  argument 2, which never does (the argument goes to the host in the packet's 10 bits, so each player's own setting
  rules their presses). The game keeps its pressed buttons while it does not sample the pad (an examination's start):
  a press is X newly there at a sample. Where Days has no Attack (a town: the panel's + 0x58), the deck's first row
  reads Attack, greyed, in place of Talk (`Ov002_DrawPanelRowText`'s record 7 left for 0; from the next area).
- **Seen** (story Mode, day 357's Twilight Town, the shop window by the station's stairs: Days's command 1): the bar
  "Examinar" over the deck, its Attack greyed; the reaction button made Roxas say "La torre del reloj... ¿Habrá alguien
  ahí arriba?" as Days's A does with the setting off; A with it on made nothing. Cerberus's three commands came with 5,
  3 and 3 ("¡COMBO!"), each press's flash in the circle, and the bar left as the blow ended. The button's icon is the
  player's input's; without one (a keyboard here) the game's X glyph, from the 10-dot font (the 8-dot has none).

## A command menu given by a script

Some commands are no reaction command's bar's but the player's own menu's: **1:237** (`trap_command_override_top`;
`FUN_1404300a0` -> `FUN_1403b2910` on the player's command manager, + 0xdd0) gives the four top commands of his menu
(Attack, Magic, Items, Drive: their slots at the manager's + 0xa28, 0x24 bytes apart) four commands of the script's
for that tick, its submenus closed; each chosen goes to the object of the target record given last (the script's
trigger 11, as a reaction command taken). A script renews it each tick it wants it, as 1:165's offer.

- **Roxas's duel** (`atk_duelbaut_a`, routine 0x7a5b; KH2's "The End", 672): the routine plays the player's AR11
  (B_EX390's slot 253, 1:29) and starts a thread on him (1:112, routine 0x7b2d: its routine pushed with another value
  after it, which the walk reads only knowing the calls' shapes, as the mod tool does with kh2/calls.txt). Each tick
  while he stays in that motion (1:101, 1:238) the thread
  gives the menu four "?" (command 104: no action, flags Cursor and Battle), and while his motion's range 18 is on
  (frames 80 to 120; the data battle's range 19, 90 to 120: `trap_obj_motion_check_range`, 1:150 with 3 and 4) one
  of the four, at random once for the thread (0:16), "The End" (672). AR11's range 26 (672) the whole motion
  (`FLAG_RC_EXEC`: + 0x18c bit 21, + 0x638) is what lets the player choose it held: KH2's `FUN_1403ba070` passes a
  command whose require (672's own) is that number, and no other while his motion gives him no state to act (no range
  0 to 2: "?" cannot be chosen). Chosen, Roxas's trigger 11 takes 672 to `rc_duelbaut` (0x7c9e), which hooks his
  motion slot 161 (1:75: a set of its own, type 0x14) -- his second phase, in which his revenge turns to
  `atk_spotbeam_revenge_*` and, after 20 blows in the data battle (50 in the other), `atk_torikaeshi` with "Reversal".
  The player's blow in "The End" (atkp 1839) gives him no reaction (atkp + 10: 0), so `rc_duelbaut` plays on to its
  hook; the port had given every KH2 player's blow a small one and sent him to "revenge" first (now each blow's own
  reaction: "Health" below). Missed, the duel ends as it would. "The End" is offered by no motion's
  range 20, no zone, no 1:165 and no code of KH2FM.exe (no 0x2a0 there): only this thread's 1:237.
- **KH2's player's searcher**, read with it (`FUN_1404081c0`, the PS3 build's `YS::RC_SEARCHER::exec`): the reaction
  command shown is the best of a script's 1:263 (`trap_player_set_exec_rc`, the player's + 0xdd8: priority 0xc9),
  an offer of kind 1 (range 20, 1:165: 0xcb), his own motion's range 25 (`FLAG_RC_SIGN`, + 0x638: 0xcc), the zones
  of every object (shapes of kind 5, the command at + 2 -- 131 the object's + 0x9f0's + 0x2c -- and of kind 14,
  `TYPE_REF_RC`, the object's + 0xcf0; one with the "Require" flag, 0x40, only while the player's range 26 is its
  require, then at 0xca) and the attacks carrying one (2:51). No object of KH2's has a shape of kind 14.
- **The port** (`World::override_top`, `take_command` and `deck_sync` in `shell.cpp`): the menu stands while a script
  renews it (this VBlank or the one before), and Days's command deck shows it -- its main rows (Attack, Magic, Items:
  `Ov002_DrawPanelRowText`'s main block, lines 0 to 2 its first row as Attack or a command in its place, 3 to 5 the
  rows after it) say the menu's first three commands, the fourth said by one of them chosen as the menu comes, so
  "The End" is as likely in any (of two there, the one the player can choose). As it comes the deck goes back to its
  main rows (KH2 closes its submenus: `Ov002_HandlePanelInput` 0); its text is drawn again (`Ov002_BuildTileRow`)
  whenever what the rows say changes, and once more as it goes. While an enemy holds the player Days's own input does
  not run, so the runtime moves the deck's cursor: X and Down the next row, Up the one before (Days's own steps,
  `Ov002_PanelCursorNext` and `Prev`, and their sound), and A sends the command of the row it is on as a reaction
  command taken is, when the player can choose it; on another, nothing. Free (Zexion's Lexicon after his illusion
  lets the player go: KH2 gives the menu whatever the player does), the deck shows it too and Days's own input moves
  its cursor (X, or the D-pad with "Command menu on the D-pad"; X is a reaction command's while one is offered, as
  Zexion's "Turn" is); A sends the row's command, and Days's own A is then no command: `Ov002_Hud_ActivatePanelSlot`
  not called (no attack, no list), the command made none at its commit (kind 7, as the reaction button's own press of
  a KH2 command; `khdays_kh2_enemies_menu_takes_a` and `_menu_commit`, joins in `Ov022_UpdateCommandInput`). Drawn
  in an enemy's own pictures (below), the menu has KH2's four slots and its own cursor over them instead. Joins
  in `prepare.py` (in `Ov002_DrawPanelRowText` and `Ov002_PanelAdvanceCursor`'s file). Days's deck has three main
  rows where KH2's menu has four, and greys none of them; KH2's cursor sounds are Days's.

## A command menu in an enemy's own pictures

**1:359** (`trap_obj_hook_command_image` -> `YS::STDOBJ::hook_command_image`, our reading of KH2's PS3 build) gives
the player's command menu the pictures of the object's own archive -- its .a.us's IMGD (0x18) and SEQD (0x19) named
`comd` -- through `dk::COMMAND_DRAW::setCommandImage`; **1:360** (`trap_obj_reset_command_image`) gives KH2's own
back. Zexion alone has one among the objects the tool makes: his book, opened with his Lexicon (`B_EX370`'s routine
0x08f2, and 1:360 when its thread's loop ends). The `comd` sequences are numbered as the general menu's `comw`
(zz0command.2dd: 99 clips there, 100 here; the 100th a page turning, not seen played).

- **How KH2 lays its main menu out** (`COMMAND_DRAW::createBG`, `drawCommandPage`, `drawCurrent`, `drawCurrentIcon`,
  `getTitleSeq` and their tables at 0x7e96c0, 0x7e9a50 and 0x7e9bb8 of the PS3 build): the background, clip 7 (the
  book opening, then holding with its title and four slots drawn; clip 6 when the menu is skipped), at 28, 322 of
  KH2's 512 x 448 screen; n slots 22 dots apart, the last at 388, the title 22 above the first -- clip 17 (18 once
  shown, 19 in a special state); clip 16 each slot, 11 the last; the cursor's slot clip 9 moved 8 to the right, the
  cursor's icon on it (clip 45 with a command kind's 37 to 44: in `comd` all Nobody's symbol). Each piece comes in
  three variants the tables choose by a second index (clips 9-19, 78-88 and 22-32); the first is used here.
- **The port** (`ui/reaction_command.cpp`'s `Book`; `khdays_kh2_enemies_command_image` in `shell.cpp`): while it
  stands, the HD picture there (kh2/fx/<name>/-<n>, n its effects' pages' count: -34.png for Zexion), the menu is
  drawn in place of Days's deck -- n KH2's four or more if Days's rows or a script's menu have more. A script's menu
  (1:237: his pages) fills all four slots, as KH2's, with a cursor of its own: on the deck's row as the menu comes,
  then a slot on or back each time Days's deck steps a row (round the four), A choosing the slot it is on
  (`top_offer.cursor` in `shell.cpp`; Days's deck has three rows, which had shown three of the four, the fourth only
  in a row where it could be chosen). Without one, Days's three rows in the first slots (the fourth empty, as KH2's
  Drive with none) and the cursor's slot on the deck's main row. The names in KH2's font as large as the reaction
  command's bar's; scaled so the title's top and the
  open book's foot meet the deck's box (the HUD's layout places it as the deck). `hud.c` then leaves out the deck's
  cells and sprites, `text.c` its names' canvas (`Ov002_DrawPanelRowText`'s), and the reaction command's bar takes
  the same pictures (`comd`'s clips 0-5: KH2's `getSqd` gives every part of the menu the image set). Without the
  setting "KH2 reaction commands", HD textures or the HD picture, Days's deck. The sequence viewer draws clips
  together where they stand (`khdays-kh2-sequence-viewer --with 18@0:-22,16@0:0,...`).
- **Seen** (2026-11-14, the coliseum, Zexion at level 99, a walk pressing X and A): Days's deck, then at the Lexicon
  the book opening (its cover, a page turning) and standing -- "- COMMANDS -", Attack, Magic and Items in its first
  three slots, the cursor's slot on Attack with Nobody's symbol, the fourth empty -- with "Pasar página" on Zexion's
  green bar over it; Days's names and cursor gone. With the pages free (the same walk): his pages in its slots in the
  game's language, changing as his script renews them ("Engaño", "Maquinación", "Tesoro", "Rapto", "Trampa",
  "¡Bingo!" ...), 123 of them chosen with A in 25 s (each sent to him as its message: 726, 765, 767 ...), no attack
  of Days's from those presses. What each page does is his script's; which it should do was not compared with KH2's
  play. With the four slots (the deck stepped every 40 frames by a temporary switch, since X was "Turn"'s): all four
  pages shown ("Suprimir", "¡Bingo!", "Cancelar", "Sanar" ...), the cursor round them (1, 2, 3, 0, and back once),
  A sending the page under it; Zexion opened and closed his book three times in 15 s, Days's deck between.

## The reaction command's mark

KH2 marks where a reaction command is offered with a picture of its field2d/<language>/zz0field.2dd (IMGD and SEQD
named `fiel`, the HD picture <LANGUAGE>_zz0field_2dd0.png), which the mod tool copies next to the bar's.

- **fiel's sequences** (our reading of the file against KH2's play of Cerberus's commands): 50 the mark coming -- a
  triangle with three arrows and a glow, then three green beams closing round it into a triangle -- 190 frames, from
  its frame 71 again while it stays; 49 its going (10 frames); 52, 54 and 56 others of its kind, 57 to 66 green
  glows. Which KH2FM.exe plays when was not read in the executable: in KH2's play, 50 while a command is offered, on
  Sora in Cerberus's jaws, as large as a third of the screen.
- **Placed** on the player the enemy holds: the middle of his body's spheres (P_EX100's shapes of kind 0x302 -- his
  head, chest, waist and arms -- on his bones in the held motion's pose now, put where Held's place and yaw put him;
  all his bones' middle sat low, the bones left at his root pulling it to the enemy's body), projected as the enemy's
  model is drawn (its draw's own view and projection), so the event sequences' cameras carry it as they do the model. Days's own projection for its marks
  (`func_0201653c`, as `Ov022_ProjectToScreen` uses it) gives no place during those cameras, nor for the aim point
  (Cerberus's 0x307 is on no bone: 4 m up, 3 m ahead).
- **Drawn** on the 3D layer as the prizes' pictures are (`khdays_g3d_sequence_polygon`, the 3D sequence package's
  last program, KSI_3D_PROGRAMS 21): at KH2's screen size wherever it stands (KH2's 512 dots the DS's 256), in front of
  the 3D, its canvas only as large as its two sequences draw; KH2's sequence player plays it on the GPU. The setting
  "KH2 reaction commands" and the HD textures switch it; without its piece the button's icon shows at that spot.
- **Seen**: Cerberus's three commands with the HD PACK and a PlayStation pad's icons (a diagnosis switch, not kept):
  the triangle and its arrows, then the beams, on Roxas in the jaws from each offer to its press. The event sequences'
  cameras leave him at the screen's edge in "00" and "30" (KH2's frame Sora in the middle), and the mark with him.

## Event sequences

An object's `.a.us` holds its event sequences as BAR entries of type 0x16 (OpenKH's "Animation Loader"), named by up to
four letters (Cerberus's "00", "10", "20", "30"); 1:157 starts one by that name (`FUN_1403db2f0` packs the letters, the
first in the low byte, as the entry's name lies). KH2 has one event player (`FUN_1402cc4b0`): a sequence started ends
the one before. Of KH2's objects, 189 sequences, every one with a camera; 76 set their camera from the player's
matrix (command 0x31), 4 call back their object (0x51), the others -- Cerberus's -- neither. Our reading of KH2FM.exe:

- **Commands**: a u16 size, a u16 number, then its own; size 0 ends them. Read before the sequence starts
  (`FUN_1402cc180`): 0 its name at + 10 ("re_b_he020-00"), 0xf the start, 0x10 + 4 a number (`DAT_140b64fbc`), 8 its
  length in frames at + 4, 0x13 a camera. On their frames (`FUN_1402cb5d0`, each with its frame at + 4): 6 a camera's
  span (+ 4 the camera, + 6 its first frame, + 8 its last), 0x17 the screen's trail, 0x18 a depth blur, 0x27 a camera
  shake (the three below), 0x31 the camera set from the player's matrix (+ 6 1, `FUN_1402d27a0`) or given back to the
  game (+ 6 0: Cerberus's "30" at its last frame), 0x51 a call back to its object, 0x3c an effect element on its
  object (+ 6). The loop waits for each command's frame before it goes on to the next (`FUN_1402c87f0`), so they play
  in the file's order. The PS3 build names them (`sa::EVENT::event_ctrl`'s switch, `event_file`'s for those read
  before): 0x18 `set_bin_seq_focus`, 0x31 `set_bin_seq_objcamera`, 0x4c `set_bin_seq_start_camera` (read before),
  0x51 `set_bin_tag`.
- **The camera's start and its end** (the PS3 build's, read 2026-11-19): 0x4c sets `m_startcamera` for the whole
  sequence: `dk::EventCamera::start` takes the game camera's look-at, eye and field of view (`ObjCamera`'s + 0, + 0x10,
  + 0x20) as where its own stand and aim, and each field update (`sa::EVENT::camera_ctrl` -> `EventCamera::lookAt`
  with its last flag) moves them toward the keys' with `dk::ObjCamera::springDamp` -- the look-at at rate 0.1, the eye
  at 0.07, the field of view and the roll (one vector) at 0.07 about 0.001 (the spring's own 2.5, the damping's 0.5,
  the positions' rest length 1): the shot eases in from the game's. Without it each frame is the keys' (a cut).
  0x31 + 6 0 sets `m_interpolate` (unless the sound is in its event mode); at the sequence's end
  (`event_end_sub`, or `event_cancel`) `dk::EventCamera::end(m_interpolate)` keeps the last view's eye, look-at, roll
  and field of view, and the game's camera (`ObjCamera::update`: `isEventInterpolation`) takes them as its own where
  it stands, its springs bringing it home; without it the game's camera takes up where it stood before (a cut). Final
  Xemnas's "11" (his grab) has both, his "50" the end's.
- **The screen's trail** (0x17, `FUN_1402da6a0`; drawn each frame by `FUN_14015f250` with `DAT_140abc690`'s quad):
  + 6 1 ends it (`FUN_14015f170(0)`); else it starts: the screen taken (`FUN_140153430`), then each frame laid over the
  new one as a quad of the whole view and taken again -- the picture before at the quad's vertex alpha, + 7 of 0x80
  (Cerberus's 0x3c, 0x32; the GS's `(Cs - Cd) As + Cd`), the quad + 0xc by + 0xe of KH2's 512 x 416 screen about its
  middle (halved, then doubled: Cerberus's 514 x 418, a little larger, so the trail spreads out), turned + 8 radians
  more each frame for + 0x10 frames (`FUN_14015f1a0`; Cerberus's 0: square). Cerberus's: 11 frames as the bite closes
  ("00", 98 to 109), 9 in "Jump!", 9 at the blow.
- **The depth blur** (0x18, `FUN_1402da870` -> `FUN_140172100`; drawn by `FUN_14018a9e0`): + 6 1 ends it; else the
  screen drawn again in four copies a little apart, at alpha 0x30 (0x20 in its other way), at the depth + 8 (at least
  15000; Cerberus's 20000, 5000, 1000) with the depth test, so only what lies beyond it blurs; + 7 its way. The PS3
  build: `dk::Focus::start(depth, + 7 == 0)`, `Focus::end`, drawn by `Focus::update` -- + 7 0 is its far way
  (`AddDeFocusFarInit`, `AddDeFocusFarTempDraw`: the screen drawn to a buffer of its own first), four copies 1 dot
  (16 GS units) off up, left, down and right at the depth with alpha 0x20, one at 0x80, then five at a depth of their
  own (`_UNK_00084be0`) at 0x40, 2 dots off; the other way eight copies. The depth: KH2's projection is the PS2
  library's `sceVu0ViewScreenMatrix` with the values `kn::Projection::set` asserts (z 1 to 16770000, near 1 cm, far
  4000000 cm), so the GS depth of a point d cm ahead is 16770003 / d - 3.19: 20000 is 8.4 m, 15000 11.2 m. Final
  Xemnas's "40", "50", "70", "71" and "72" turn it on and off (20000) in 42 commands. **Not played**: the far way's
  passes through its own buffer were not all read (`AddDeFocusFarInit_dt`, `AddDeFocusFarTempDraw_dt`).
- **The camera shake** (0x27, `FUN_1402da950` -> `FUN_140528bc0`; moved each frame by `FUN_140528920` until its time is
  up): the eye moved by `(r - 0.5) x amplitude x envelope` on each axis, r at random (rand()), the amplitudes + 6, + 8,
  + 0xa (cm; Cerberus's 150, 150, 0 and 80, 100, 0), x and z turned with the camera's yaw (`FUN_1401aadc0`), for
  + 0xc frames twice in ticks; the envelope + 0xe: 0 steady, 1 rising then falling, 2 rising, 3 falling. It is the
  camera's, not the pad's: the pad's vibration is a motion's trigger 31.
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
  after, and the KH2 camera takes up again as after Days's camera's frames. Its commands on frames play in the
  file's order as the frame reaches each: the shake's offset (a new one each KH2 tick) moves the event camera's eye;
  the trail goes to the host in the frame package (`trail`) and is laid over the field's 3D after its antialiasing
  (`shaders/trail3d.hlsl`, docs/RENDERING.md), at KH2's 60 frames a second whatever the picture's rate. Both end
  with the sequence. With 0x4c its camera springs from the game's (`Playing::springs`, every other KH2 tick, KH2's
  cm); with 0x31 + 6 0 reached the port's KH2 camera (`runtime/camera/kh2_camera_hooks.c`: the setting "KH2 camera")
  takes up from the sequence's last view, its own targets kept -- after any of these sequences it takes up where it
  stood, as KH2's does, not anew from Days's camera; Days's own camera (the setting off) has no springs: a cut. The
  roll is not drawn (Cerberus's are 0), nor the depth blur, nor 0x31's camera from the player's matrix (+ 6 1; told in
  the trace when a sequence starts). Played with Cerberus (2026-11-09): the four shots in turn, the HD
  PACK's KH2 Roxas in its mouth, in the air for "Jump!" and on its heads for the blow; a room smaller than KH2's arena
  leaves some shots outside its walls (KH2's event cameras meet no collision). Days's own Roxas model plays none of
  KH2's motion: it stays at the hold's place, Cerberus's middle, inside its body.

## Triggers

| Trigger | Read |
| --- | --- |
| 0, 1 | start (once, with the object's address) and main (its own thread, every tick) |
| 6 | **a hit taken** (`FUN_140410d60`: after the current state's hit routine, before the native reaction), its argument the hit; also sent to every one of 8 listening threads (`FUN_1403f47b0`). The enemies turn to their revenge mode on it ("Revenge" above) |
| 11 | a message from another object: raised by the call `FUN_14042c0d0` (one script to another object: its sender, a number, a target) and two native callers |
| 13, 14 | **leaving the ground, landing** (`FUN_1403b7920`, `FUN_1403b79d0`, reached through tables KH2 builds running): + 0x120's bit 26 (13) or 27 (14) cleared, the state machine told (`FUN_1403ca1d0` on + 0x390, 1 or 2), the trigger raised unless the bit is set again, then -- the bit still clear -- the class's virtual + 0x28 or + 0x30 (the landing: its motion) and the object's + 0x50 or + 0x58. A script sets the bit (`1:66`: 27) to keep its class from acting: the Neoshadow's 14 lands its leap (203: plays 204, `1:66`, `1:74`), the Shadow's 13 plays its fall (4). The port raises 13 when the ground drops away under it by more than a step (30 cm) and 14 when a jump, a fall from past a step or a motion in the air (range 30) meets the ground; the bit set again skips the landing motion |
| 10 | `FUN_1403b3030` (a pointer in an `.rdata` table): **not known** |
| 15 | **an attack of its own landed** (`FUN_1403d1730` -> the attacker's class virtual + 0xe0, `FUN_1403d5500`): the one hit and the attack, the attack read first (its + 0x18 its atkp entry, `2:25`); Cerberus's pinning bite begins its reaction command on it. The shell raises it as a KH2 attack's hit lands on a Days player |
| 16 | a motion's effect started (`FUN_14040dd70`: its trigger 1, `FUN_1402c6b30`): the actor, the element's number and the effect's handle -- a handler reads them as fp + 8, + 4 and + 0. The Hammer Frame's attaches its shockwave (2:10, 2:61) to its element 8 |
| 17 | a motion's "game effect" (`FUN_14040dd70`: its trigger 7): the actor and the trigger's two values |
| 19 | **a hit** (`FUN_140410700`, `FUN_140411060`): the attacker kept at the actor's + 0xd38 (what `2:88` gives back), the state's hit routine (+ 0x18, `FUN_1403caba0`) and trigger 19 run with the damage and the attack, then the attack's reaction applied (`FUN_1403d4650`), the queue cleared and the reaction state the attack's data names entered by name. The port raises both on Days's hits (the Volcanic and Blizzard Lords and Shan-Yu answer 19; Saïx's data battle's hit routine asks the attack's owner) |

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

When single-frame triggers play (the PS3 build's `YS::ANMATR::each_trigger` and `exec_trigger`): those whose frame lies
after the time before and up to the time now; a motion started (`YS::MOTION::start_motion`) plays its first frame's at
once (`exec_trigger(start, -1)`: with a "to" below 0, the frame equal to "from"), or, started over a blend (above 2
ticks: `start_blend`), at the blend's end, the motion having stood at its first frame while the blend's ticks went by.
On the PC (`FUN_1403c8a40`, `start_blend` `FUN_1403c87c0`, `add_time` `FUN_1403c6740`, on the motion controller at
actor + 0x158): a blend above 2 (`DAT_140623b18`) once a pose has been shown (+ 0x168 bit 0) sets its length (+ 0x48),
the frame it stands at (+ 0x4c) and its count, from 2 (+ 0x44); each tick `add_time` adds a tick to the count, and once
the count reaches the length it clears the blend, goes on from the start frame by what the count went past and plays
that frame's triggers, then those up to the time now. Meanwhile the pose is the one shown the tick before taken count /
length of the way to the start frame (`MOTION::exec` and `calc`), and the root does not travel. KH2 moves its motions on
before its objects' scripts run, so a motion a script starts stands at its start frame in that tick (`Actor::begin_tick`).
An enemy's own motions (`Actor::play`, `Actor::step`, the shell's `cues` and `pose`) and a held player's (above) play
so; 1:63 is not in its loop while a blend runs.

## Collision

An object's `.mdlx` holds its collision shapes in its BAR entry of type 0x17: a 64-byte header (its count first) and
20-byte records. A shape on no bone is in the actor's own axes, KH2's world's (y down): the model's turned half round
its x, its y and z the other way -- the Rabid Dog's rush sphere (kind 6, group 2) at (0, -55, -170) is 55 up and 170
ahead, every kind's aim point (0x307) above it (-50 the Rabid Dog, -80 the Soldier, -140 the Wight Knight). Read from the Soldier's 17 and the Shadow's 10: a group (byte 0), a kind word (+4: 0x300 the body's
cylinder, 0x302 a sphere on a bone, 0x306 an attack's sphere, 5 a large box), a bone (+6; 0x4000 none), an offset x, y,
z, then sizes (height, radius). The Soldier's body is group 0 (a cylinder 115 high, 90 round; spheres on bones 2, 28,
31); its attacks are groups 1, 2, 3, 4 and 7 (spheres of radius 40 to 110 on bones 88, 63, 2); group 6 is five boxes
700 round it: a reaction command's zones (kind 5, its command at + 2: 34, Cyclone; below, "Reaction command zones").
A motion's range 10 names the group an attack uses: the port tests the group's attack spheres (kind 0x306) on their
bones, in the pose the motion has that tick, against Days's players' collision spheres. Range 4 turns a group on for
its time, range 5 off (`FUN_14040d810`: `FUN_1403f1fa0` and `FUN_1403f1f80` on + 0x5a0, `YS::OBJ`'s
`CollisionFlagMotion`; OpenKH's ANB page names them), and a shape is looked for among the groups on (`FUN_1403b5650`,
`FUN_1403b4f10`). Whether a group is on (`YS::OBJ::is_enable_collision`, `FUN_1403ba550`): the script's own flags
first (+ 0x5a8, `CollisionFlagScript`, 1:52 to 1:54: a word of groups on, the next of groups off), then the motion's,
then the shapes' default -- the header's + 4, a bit a group (1, the body's group 0, in every one read). The record's other bytes (+ 1, + 3,
+ 18) are **not known**.

## Effects

An object's effects are its `.a.us`'s PAX (its BAR entry 0x12; docs/FORMATS.md, "KH2's effects"). Its 0x82
directory's 32-byte entries are `{u32 its effect's offset from the directory, u32 its emitter, u32 its number}` (the
Soldier's 9 over 4 effects, the Shadow's 3 over 3).

**Its model's own** (the PS3 build's `YS::OBJDATA::start_model_effect`, the PC's `FUN_1403e6470`): a model file
(`.mdlx`) may hold a PAX of its own (its BAR entry 0x12: Hades's `hade`, Scar's ghosts' `scar`). As the model is made
KH2 starts that PAX's first elements, each as `1:87` starts one (`FUN_1402c6b30`), by its object entry's + 0x4c --
0x3f8 and 0x138a element 0, 0x1395 elements 0 to 2, 0x13a2 0 to 8, 0x13ca 0 to 13 -- else by its number: 0x888,
0x88a to 0x88c and 0x97c element 0, 0x889 0 to 3. Hades (0x138a) wears his hair's blue flame so (no script of his
starts it; the `.a.us`'s element 0 is another, a bright orb), Scar's ghosts (0x13a2) their nine. Its HD pictures are
in the model's own folder after its textures' (a file's pictures run through its entries in BAR order:
`mdlx_texture_pictures`), each container of a texture entry that holds several counted (Hades's `tim_` holds two
sets of five: his flame's pictures are `-10` and `-11`). The shell reads it (`read_model_effects`) and starts them as
it makes the enemy (`model_effects`).

Hades's flame is ten particle spawners (`0xc9`) on his head's bone 103, at its top (the element's place 24 along
the bone), each sprite moved 30 toward the camera in view space (the key's + 0xe, + 0x10 to + 0x18): KH2's PC build,
in the rooms that take that offset as a depth bias, raises the sprite's GS depth by it (larger is nearer), so the
flame shows in front of his hair's mesh. Pushed away instead (the port's first reading), it hid behind his head.
Its ten layers stand 8 to 12 along the bone's -y, back over his swept hair: an element on a bone is placed in the
bone's own frame, which KH2 never turns (`FUN_1401a7af0` turns the bone matrix's columns alone, its y and z by -1
into its world); turning the frame's y too (the port's first reading) put them at his forehead.

What a motion's trigger 1 and the script's `1:87` start is an
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
| + 0x40 .. + 0x4c | with flags 0x10000, its **two points** (a swing trail's base and tip): u16 + 0x40 and + 0x42 their bones, f32 + 0x44 and + 0x48 how far out along each bone's x in that bone's lengths (`YS::OBJ::get_bone_pos`: the bone's matrix times (along x length, 0, 0); the length is the skeleton's scale w); + 0x4c set, the place (+ 0x1c) and the turn (+ 0x28) are offsets on those bones. `ryj::PAX::MANAGER` keeps them (`set_reference_pos`, 2 points) and `get_obj_data` sets them each tick; `pppRyjDrawShipolyBone` (0xc8) reads them. The keyblade's trail (W_EX010's element 10): bone 2 at -0.65 and -3.7 of its 21.1 cm, 14 and 78 cm out along the blade (its mesh runs to -78 on bone 2's x, its chain to +56). Enemies' own too: the Magnum Loader's elements 6, 7 and 15, the Neoshadow's 3, 4, 12 and 13, the Dusk's 8 to 11 |

The port runs each element started on the enemy (`runtime/mods/kh2_fx_draw.h`, the Dark Corridor's way: the effect
in KH2's axes and centimetres, its view the enemy's) at its size, turn (x, then y, then z: the order **not known**,
the Soldier's and the Shadow's turn about one axis only) and place, on its bone in the pose of the tick it is drawn
at; its sound starts when its time comes; its two points, when it has them, on their bones each frame
(`EffectInstance::set_points`): before, an element with them drew no swing trail at all (the Magnum Loader's blades',
the Neoshadow's claws'). KH2 gives the trail a new pair each tick (60 a second) and Days draws once every two of them:
the tick between two draws takes its pair on the way from the last draw's to the new one (the base along the line,
the tip turned round between the two ways, as a blade swings). Until 2026-11-13 both ticks took the draw's pair, each
stood twice in the trail's history and its curve went in steps (Data Roxas's keyblades). Their pictures are read between their texels whatever Days's texture
setting: KH2 does not draw its effects with nearest texels, and the DS look's made a spark a block of pixels. **Not
known**: + 8 and + 0xc (the Soldier's elements 3, 4 and 7: 1 and 1, 2, 4), and the two values `1:87` passes on.

The effects' view is the field's camera, which Days's enemies' draw does not leave behind: it folds the camera into
the projection and flushes a camera that does not turn (moved back 100 or 1024, a screen layer's). Taking that one, an
effect's view was the world: what KH2 draws facing the screen (`pppRyjMegaBirth`'s particles, `pppDrawMatrixFront`)
stood fixed in the world -- the Mole Driller's dust, a swirl facing the camera, stood up edge on beside it (2026-11-11).
The port takes the frame's own camera then, its list's first flush's (`khdays_g3d_list_camera`); the whole of a
draw (its view times its projection) is the same either way.

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

### Voices

A boss speaks with 1:262 (`trap_obj_voice`): its object's voice (`YS::VOICE`, the PS3 build's) takes the number and a
priority (`request`: kept when nothing waits or plays, else when the priority is as high or higher), then plays that
sound of its bank (`exec` -> `VOICE_EE::play` -> `SOUND::createVsbVoice(bank, number, ...)`) where the object stands,
the one before ended. The PC's battle banks are `original/voice/us/battle/<name>.win32.scd` (`he_hades`, 32 sounds
of 22 recordings, each Ogg Vorbis: docs/FORMATS.md "KH2's sound banks"), each sound at a volume of its own (0.47 to
0.77 in Hades's). **Not read**: how KH2 gives an object its bank (its area's loading; `YS::VOICE::ReadEntryId` is the
party's, by `00objentry.bin`'s + 0x4c): the mod's tool names each kind's bank (`VOICES`) and writes its sounds as
`kh2/voice/<kind>/<number>.ogg` as they are, their volumes in `volumes.txt`; the shell hears one at a time per
enemy, at its volume, among the effects (`khdays_sound_shot_play`, `_heard`, `_end`). Hades's line 13 is his end's.

## Lights

KH2's bosses darken the field for an attack with lights their scripts make (`YS::LIGHT`; the PS3 build's
`trap_light_*`). A light (`FUN_1403f6f80`: 40-byte records at 0x142ae76c0, a list from 0x142ae76a8, new ones at its end)
has a kind, flags (+ 0x10), a colour (+ 0x14, bytes, 128 as the field is) and a strength (+ 8, 1 at its making) with
its step (+ 0xc). 1:183 sets the colour and runs the strength from 0 to 1 over the ticks given (`FUN_1403b7810`: 2 or
fewer one); 1:184 runs it to 0 (`FUN_1403f76a0`). Each frame (`FUN_1403f7040`) a light's strength takes its step
(`FUN_1403b77c0`, held at 0 and 1), a light at none leaves the list (`FUN_1403f7580`), and the colours are composed,
each channel 128 times each light's 128 + (its colour - 128) x its strength (truncated), over 128, kept a byte:

- **kind 0 with flag 1** (`+ 0x10` bit 1): the room's -- `FUN_1401626f0` multiplies the map's colour and its fog's by it
  over 128 (black hides the map, `FUN_1401625f0`);
- **kind 1**: the objects' -- the area's object light (`FUN_14016f920`).

The bosses' (all kind 0, flag 1): Xaldin 80 80 80 (`atk_final`, 60 ticks in, 30 out), Axel, Xigbar, Saix and Demyx's
second 80, Demyx's first 48, the Lingering Will and B_EX400 64, the Groundshaker 96 48 48 (red); the Illuminator
16 16 16 and a kind 1 of 0 0 0, the field gone dark. The port keeps the list as KH2 (`runtime/kh2_enemies/lights.cpp`,
a KH2 tick at a time with the scripts) and tints the polygons (`khdays_g3d_set_tint`: the game's vertices' colours and
the mods' polygons', not the HUD's nor KH2's effects'): a mission world's room (Days's or a mod's, `mi/wd/wd_*`) by
the room's colour, the other models by the objects' (`runtime/mods/models.cpp`). Seen: Xaldin's room at 0.57 to 0.6
of its brightness while Roxas and Xaldin stay as they were. KH2 hides the map at black; here it is drawn black. The
fog is Days's own, not tinted. A light ends with the area, and when no KH2 enemy is left to fade it (KH2 keeps one
till its script fades it).

## Names

A KH2 enemy's name is its journal's: KH2's journal (`menu/<language>/jiminy.bar`, OpenKH's jiminy page) lists its
characters -- its Heartless, Nobodies and bosses among them -- in `JMCH` records of 36 bytes, each with a title text
(+ 6) and an object (+ 0xc, `00objentry.bin`'s number), the title one of `msg/<language>/jm.bar`'s messages (KH2's
text, as the reaction commands' names: `kh2_text`). The mod's tool finds each kind's title in the English journal --
its own model's (`M_EX010`), else its journal's (`B_AL100_FIRE` is the journal's `B_AL100_FIRE_GM`), else its
family's (`B_HE030_CLSM` is `B_HE030_GM`), else the first model named on from it (`M_EX200` is `M_EX200_NM`) -- puts
the number in its line of `kh2_enemies.txt` (its seventh column; "-" none: the Volcanic Lord's lava, the Sorcerer's
cubes) and copies each language's `jm.bar` to `kh2/msg/<language>/`. The port reads the message in the game's
language (English us, French fr, German gr, Italian it, Spanish sp): Hades is Hadès in French, Ade in Italian; the
Soldier Soldado, Soldat, Klappersoldat, Soldato; the Volcanic Lord Jefe volcánico, Poussah Volcano, Vulkanlord, Lord
Vulcano.

**Days's enemy labels** (`Ov002_BuildPanelLabels`, found by the stack under its glyphs): as the field begins Days
draws a label for each enemy class the mission has (`UI/btl/&/enemy.s`'s text of the class's number), and shows the
targeted enemy's above its bar. A join gives a class whose every row in the mission stands in for one KH2 enemy that
enemy's name in its place (`khdays_kh2_enemies_label`, `khdays_missions_kh2_enemy_of_class`): Hades in a Days
Soldier's place is labelled Hades. A class shared by a Days enemy of its own, or by two KH2 enemies, keeps Days's
name.

## Health

KH2 gives an enemy the health of its battle entry (`00battle.bin`'s `enmp`, OpenKH's 00battle page: 92-byte records,
+ 0 the number, + 4 32 health values, the first its own) times its level's (`LVPM`: (health x the level's + 99) /
100, 20 at level 1 to 181 at 99); its script names the entry with `2:9` (the Soldier's 33, 100 health; Hades's 19,
1000, or 199, 1500 in the cups). KH2's damage (`YS::SHEET::calc_damage`, the PS3 build's): the attacker's strength
(or magic) less the defender's defence, kept between two bounds, times the attack's power, the element's and the
general rates, over 1,000,000, rounded up; then the game level's, the combo's and the element's adjustments.

**In Days** a KH2 enemy stands as many of Roxas's blows as Sora's in KH2. Its entry's health over KH2's Soldier's
(`World::battle_entry`, once, as `2:9` names it; KH2's level multiplier is the same for both, so their share leaves
it out) times the Soldier's blows (`kSoldierBlows`, 4: a tuning value -- KH2's own tables leave the Soldier's to
Sora's keyblade and abilities, 2 to 90 at level 99 by his strength alone) gives its blows: Hades of the cups 60, the
Soldier 4. A blow is the area's first Days hit on a KH2 enemy (its damage before KH2's element rate), so its health
follows Roxas's level and gear: at that first hit on it its health becomes its blows times that blow. Until then
Days's own HP stands (the bar full). Measured against Days's Soldier instead (its HP at the mission's level times the
share), Hades of the cups at level 99 had 32,040 health against Roxas's 15 to 74 a hit: hundreds of blows.

The shell keeps that health (64 bits); Days's HP fields (signed 16 bits) show its share, up to 32,767, at least 1
while any is left, so the bar and Days's defeat follow it. A Days hit takes its damage after the element's rate and
the script's least (1:231); Days's fields lose what the share does, and the reaction follows the health's loss (a
small hit may leave the share as it was). An enemy that leaves and comes back keeps it.

Its script's record (2:9's, `hp_rate`: 1:228, 1:229, 1:230; the least 1:231, 1:232; 1:233) holds KH2's own numbers: its
battle entry's health (1,500 for Hades of the cups; the entries' level is 0, KH2's area gives it, so its level's
multiplier is left out) and its health's share of it -- full until Days's first hit, which measures the health. The
scripts take shares of it from their start (the Prison Keeper's leasts, 80% then 60%, 40% and none, worked out of
1:229 as its battle begins): a record in Days's stand-in's HP until the first hit and in the health after it had left
the Prison Keeper's least at 80% of the stand-in's 2,136, over its whole health of 1,200, and its least stepping
compared with values of the old measure, so it never stepped. A least (1:231) and a health set (1:233) are shares of
the record turned to the health (one set before the first hit starts the health at that share). A Days hit the least
keeps from taking any is a hit all the same (KH2's `YS::SHEET` MinHp): its reaction and trigger 6, on which the Prison
Keeper spits its captive out and lowers its least.

A KH2 player's blow (a reaction command's strike: Xaldin's Jump, Cerberus's finish) is its attack's power over 100
blows (`atkp`'s power: 100 a plain hit; Jump's 581, 150, one and a half). It lands as a hit whose amount is those blows
in the area's measured blow, in the measure Days's fields show (Days's class takes the packet's amount off them too: a
blow worked out the old way -- the player's last Days hit times the power -- had taken a Days Fire of 902 for 1,353
and ended Xaldin with half his health left). One that lands before any Days hit measured a blow waits: once the first
Days hit measures it, the enemy's health starts that much lower (a first measure taken from the strike itself had made
the blow 1 and Xaldin 60 health) -- unless they are enough to end it in KH2's own measure (its health in KH2's Soldier
blows, as much as is left of it): then it falls at once, the hit its whole Days health (Demyx's water clones, a
quarter of a blow, to the first swing of Wild Dance). Its reaction is its attacks' (`atkp`'s + 10, `YS::DAMAGE`'s
REACTION_*, the strongest of the blow's; 255, ignored, as none): the hit the scripts read carries it (+ 0x26: 2:7,
2:8), and with none the enemy makes no reaction motion and enters no "revenge" -- it goes on as it was, trigger 6
told (Roxas's `rc_duelbaut` under "The End", 1839; Cerberus's finish, 598). A Days hit's is a small one
(REACTION_SMALL, 3: what most of KH2's player's attacks give, `atkp`'s player-team entries 3 or 4, none 2).

**Damage patterns** (2026-11-14, a tester: Thunder on Roxas during his desperation move cut it short, the sky left
dark, his orbs still falling while he fought on). Before a hit reaches the script, KH2 changes its reaction by the
enemy's patterns (`YS::PIERROT::_OVR_exec_damage` -> `YS::PATTERN::Convert`, the PS3 build's 0x47b310 and 0x47a65c):
`00battle.bin`'s `patn`, 18 records of 32 bytes (`YS::PATTERN`: + 0 its number, + 1 a reaction for each of the 19 a hit
may have, 0xff none), and the enemy's `BTLOBJ::Pattern` (Default, Enable, Disable; the patterns on are
`(Default | Enable) & ~Disable`). 1:211 sets bit n of Default, 1:212 and 1:324 clear it (the PC's + 0xd10). Each
pattern on, in the table's order, maps the hit's first reaction (OrigReaction, + 0x27) to its own when it has one --
the last such wins -- into Reaction (+ 0x26); pattern 7 only on the ground, 8 only in the air, 2 only when the attacker
has a flag (+ 0x588 bit 14) no Days player has. Pattern 1 makes every flinch and blow (3 to 11, 15 to 18) none: Roxas
turns it on in the entering routine of `atk_final_hard`, `_superhard` and `_revenge` (his desperation move: flags 1,
not locked, priority 200 under "revenge"'s 300) and of his spotbeam revenges, and off in their leaving routines, so in
KH2 no hit stops them. Enable and Disable are the motion's: its ranges 36 and 37 (`FUN_14040d810`'s 0x24 and 0x25,
the PC's + 0xd14 and + 0xd18) turn a pattern, their parameter's bit, on and off while they last -- Roxas's dashing
combo `atk_tate` (motion 201) holds pattern 1 over its frames 0 to 240, his motions 205 and 208 patterns 3 and 1, so
no blow breaks them off. The port reads `patn` and applies the patterns on to Days hits and to KH2 player's blows
(`runtime/kh2_enemies/patterns.h`, `motion_states`); a reaction made none plays no motion and enters no "revenge"
(2026-11-14: a Days hit had sent Roxas into "revenge" in the middle of his combo, which then began again from where he
faced -- away from the player, the tester's "he does not aim at you"). The karma (`YS::ENEMY` + 0xaf0, the attack's + 0x2a added on each hit; 2:76 its limit, + 0xaf4)
is another matter: a motion's trigger asks the enemy's act for its revenge once the karma reaches the limit
(`YS::ANMATR::ExecTriggerCallback`) -- not read yet.

**A KH2 hit throws the Days player** back as KH2 throws its own (the hit's `Dir` and `YS::DAMAGE::get_hitback`, as 2:86
reads them): an attack of its own (2:17) along the ground from the enemy to him, as far as its `atkp`'s + 0xc (KH2's
cm; Roxas's dashing combo 75, its last blows' 10) or, at 0x7fff, KH2's battle constant for its reaction; none for an
attack that gives no reaction. Days's recoil (`Ov022_IntegrateRecoilVec`: added, then halved each frame) moves him
twice the push it is given, so half of it in Days's units is given. A motion's attack spheres (its ranges 33) the same,
from the sphere to him (they had given one Days unit each, about 2 m of recoil, where KH2's Roxas combo throws 75 cm).

An attack of the **player's team** (its `atkp`'s + 8, `YS::ATTACKPARAM`'s Team, 1): KH2's `YS::ATTACK` constructor
makes the player its owner whoever made it, its team his (`set_team`: 3 counts as 4) and its hit teams every other
(`HitTeam` all bits but its own and 0); a team of 0 takes its owner's (`YS::OBJ`'s Team, + 0x3b0). The port's: a
motion's attack (its ranges 10 and 33) whose `atkp` is of team 1 strikes the other KH2 enemies its spheres reach -- a
body as a player's, its Days sphere's radius round from its feet to KH2's player's height -- each once a range, as a
blow of the player's, and no player (`strike_enemies` in `shell.cpp`). Demyx's water clone swung in Wild Dance (its
motions 241 and 242: `atkp` 1326 to 1329) so strikes Demyx and the other clones.

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
- **Hit** (`Ov107_AiState_ApplyHit`, joined where its damage is worked out: kept from taking it under the least
  health its script set, 1:231; and after its class's handler took the hit): its HP down, KH2's small
  reaction plays (the queue cleared, + 0x18c bit 9 set, `revenge` entered, then trigger 6: "Revenge" above; a kind
  without `revenge` has its script wait) unless a locked state runs; its HP gone,
  the state `dead` is entered (unlocked: the game's own entry). Days defeats it at once and reuses it for the next
  wave: the KH2 enemy's end goes on where it was, drawn as the 3D list closes, until it has faded out.
- **Taken by its wave** (`Ov107_Spawner_FindFreeActor`, joined): a wave's spawner takes an idle actor whose class
  is its row's -- rows of one stand-in class share the spawners' kinds entry (`Ov107_AllocSortedEntry`), so Days alone
  took any of the class's actors, and a defeated Rabid Dog's served the Strafer's row. The join takes only an actor
  made for a row with the same KH2 enemy as the slot's (the slot's owner tag, its spawner's place among the field's
  objects << 8 | its group, names the row: `khdays_enemies_slot_row`).
- **Made by another's script** (1:0): a Days enemy of its maker's class made at once where its maker stands
  (`khdays_enemies_make`, as the mods' interface makes one; its making through `Ov002_CreateActorFromMarker` names its
  maker's row, the kind its script asked for put in its place), its KH2 actor started in the same call; its kind
  dropped when its Days enemy is retired (a later wave may reuse the actor). **Counted by its own script** (1:328):
  defeated by Days as a hit of all its health defeats it, its KH2 end its script's own.
- **Put by a script** (1:148, 1:153, 1:220): the place its script gives it in a tick is kept (until 2026-11-11 the
  place Days had was written over it), and the place another's script gives it between two frames (the cubes put the
  Sorcerer they made where they stand) is taken at the next frame's start, not Days's. 1:220's step stops at the
  room's walls as KH2's sweep does (a body's sphere half a metre round swept along it, stopped at the first wall).
  Until 2026-11-13 it stepped the whole length: Days's collision pushes back only one standing in a wall, so a step
  through one (Roxas's 2.5 m before his hold) left it outside. A KH2 actor put by 1:148, 1:153 or 1:217 (2026-11-19)
  stops short of the first wall between the player it goes for and the point, swept as a step at the higher of the
  two: KH2 puts it there unchecked, its arenas being larger than Days's rooms -- Final Xemnas's warp (`mode_bomb`: 18 m
  from the player, the player's way from the world's middle turned 157 to 203 degrees at random -- across KH2's
  arena) left him outside the coliseum made from he00, behind its gate; a leap onto a rock (Scar's) still lands, the sweep passing over it.
- **The room's walls stop its way** (2026-11-14): each tick its way along the ground -- its motion's root, its
  steering, its slide, a vacuum's pull, on the ground or in the air -- is swept by a sphere of its Days sphere's radius
  (half a metre at least), its centre that radius and a step (30 cm) over its feet, against the room's walls: up to the
  first, the rest along it (its part into the wall taken out), swept again and stopped at the next; its steering and
  its slide end there (`FUN_140421450`). A held player's moves are swept the same way. Days's collision keeps three
  lists: `Collision_CastRay` walks a node's first (floors and slopes; `Coll_WalkTreeForHit`), the sphere cast
  (`Collision_RunSphereCast`, `Coll_WalkSphereTreeForHit`) its second, the walls -- as `Actor_ResolveMove` sweeps
  Days's own characters. Before, the port's wall checks cast rays and met no wall at all: the Lingering Will's knuckle
  rush (motion 203, 23 m in a second and a half) left the coliseum through its ring's and its stands' walls (built
  from KH2's `he00` collision, both there) and stopped 50 m out where its floor ends. The scripts' casts of the room
  (`World::ray`: 1:347's line of sight, 6:51's segment and 6:52's normal, 1:141's ground) meet the three lists, the
  nearest, as the camera's line does (the sphere cast with no radius is a line); 1:247's point round another is swept
  out to with the actor's body and kept its body short of the first wall (the Will's warp beside the player, 4 to 6 m
  off him, had put him outside the ring, where Days's player cannot reach him).
- **Jumps and falls** end on the ground Days last found under it (+ 0x13c, + 0x140): Days's collision lets a fast
  fall through the floor (the Wight Knight's 5 m leap fell on under the room, its script waiting on the ground).
- **Its height is KH2's**: between two steps Days lowers a stand-in that is off the ground by 0.148 units a frame
  (seen: the place read back that much under the place given, the ground's distance + 0x13c measured from the place
  given; not Days's pull, `Ov107_AiState_IntegrateVelocity`, whose + 0x60 0x40 made no difference). KH2 carries it up
  and down itself, so the height given is kept when Days lowers it; Days lifting it (out of the ground) is kept. Before,
  the Neoshadow's leap (203: up 1.8 m by its motion alone) came down by frame 33, before its dive's attack (frame 40).
- **Motions in the air**: KH2's air classes add no pull while + 0x18c has 0x1000020 or bit 6 (`FUN_1403cb100`,
  `FUN_1403cb500`): ranges 3, 24 and 30 (`FUN_14040d810`: 30 sets bit 24, which `1:60` counts as off the ground).
  The port's pull leaves those motions alone; its jumps (1:55, 1:156) still follow their own arc in them.
- **Leaving and coming back**: KH2's enemies enter `leave` when the player is far (their `mode_battle` measures it
  against their handle's + 0x38 on the screen, 20 or 25 m, or + 0x3c off it, 12 or 18 m; `1:56` says which): they
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
