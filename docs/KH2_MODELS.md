# KH2 models (.mdlx)

KINGDOM HEARTS II's character and object models (`obj/<name>.mdlx` in the
player's 1.5+2.5 copy), drawn by the port instead of a Days model. Read by
`assets/src/kh2_mdlx.cpp` (`kh2::import_mdlx`) on top of the KH2 map readers in
`assets/src/kh2_map.cpp` (BAR, object models, texture containers), and drawn
like any mod's model (`RENDERING.md`, "Mods' graphics"). Nothing of the game is
in the repository; the file comes from the player.

Stage 1 (this page): the model at rest (a T-pose) with its textures, on Days'
node it is paired with. Animations, maps and collision come later.

## Using one

```text
mods/<Mod>/graphics/<file>[#<entry>]/mdl/<model>.mdlx
mods/<Mod>/graphics/<file>[#<entry>]/mdl/<model>.bones.txt
```

`<file>/mdl/<model>` is the Days model's name as `--dump-graphics` writes it.
`.mdlx` is tried after `.gltf`, `.glb` and `.fbx`. The joints are the KH2 bones,
named `kh2_<bone index>`; `<model>.bones.txt` pairs them with Days' nodes as
for any mod model (`kh2_0 roxas_R`). Bones without a pair follow their parent.
KH2's units are centimetres: the importer makes them metres (x 0.01), the frame
mods' models use, so a KH2 character comes out at a Days character's size
(Days' Roxas is 1.60 tall in the dump, KH2's 1.69) without a `scale` line.

## The file

Little-endian on the PC (the HD collections); the PS3 build in IDA is
big-endian. A BAR (`kh2_bar.h`): entry type 0x04 the model, 0x07 its textures,
others not read (0x17 seen). The model's header follows 0x90 bytes the game
keeps for itself.

**Model** (`kn::ModelSKLRAW`, PS3 `model_sklWIN.cpp`): u32 kind (3: an object
or character), u32 at +0xc the offset of a chained model (a shadow model; not
read), u16 bone count +0x10, u16 texture count +0x12, u32 bone table +0x14,
u32 +0x18, u32 subpart count +0x1c, then the subparts (0x20 bytes each,
`kn::ModelSKLRAWGroup`): u32 attribute, u32 texture (GS info index), u32
polygon count, u16 hasVB, u16 alt, u32 DMA chain offset, u32 matrix info
offset, u32 packet size, u32 reserved. Bones are 0x40 bytes: u32 index, i32
parent, scale, rotation (radians), translation (16 bytes each); a bone's matrix
is its parent's x T x Rz x Ry x Rx x S.

**Attribute** (`kn::ModelSKLRAWAttribute`, LE bit order on the PC): bit 0 draw
alpha phase, 1 translucent, 2 multi, 6-10 part, 11-15 mesh, 16-20 priority,
21 alphaEx, 22-26 UV scroll, 27 additive, 28 subtractive, 29 specular, 30 no
light. Translucent subparts are drawn blended, the others alpha-tested.

**Packets.** A subpart's DMA chain: ref tags of 4 quadwords bring a bone's
matrix (the tag's address is the bone; its UNPACK's address the VU slot), other
ref tags data from the model, a cnt tag inline data, and closes a packet. The
packet's VIF UNPACKs fill VU memory; its header, u32 words h[0..]
(`kn::SKLPOLYGONDATA::SKLPOLYGONDATA`, 0x6ac7b8):

| Word | Meaning |
|---|---|
| h0 | 1 model, 2 shadow |
| h4, h5 | strip vertex count, list address (u, v as s16 / 4096; index; flag) |
| h6 | matrix affiliation list: how many positions go with each matrix |
| h8, h9 | colour count, address (V4-8, 0x80 = 1.0) |
| h10, h11 | weight group count (0-7), address of their counts |
| h12, h13 | position count, address (x, y, z, w) |
| h14 | blended index list address |
| h15 | matrix count |
| h16, h17 | normal count, address (a 5-quadword header) |

A strip flag ends a triangle as in the map models (`kh2_map.cpp`).

**Blended vertices** (`setVertexWithWeight` in `SKLPOLYGONDATA::setVertex`,
0x6ad418). With h10 > 0 the strip indexes blended vertices, not positions.
Weight group g (0-based) holds count[g] vertices of g + 1 positions each; the
group's position indices follow on from h14, each group's list starting on a
quadword. A position's w is its weight (> 0, asserted) and its x y z are
already scaled by it: the vertex is the sum of matrix x (x, y, z, w) over its
positions. Each position's matrix is the one whose affiliation range holds the
position's index. Verified on P_EX110: every vertex's weights add to 1.0 and
the mesh closes up at rest. The importer keeps a vertex's four heaviest bones
(at most two seen).

**Textures** (`kn::Texture`, `kn::convertTexture` 0x110dc8, `kn::makeTexture`
0x11055c): u16 type, u16 flag, u32 colour count, picture count, GS info count,
then the offsets of the pictures' numbers, the send packets (0x90 bytes each,
the palette's first; BITBLTBUF at +0x20, TRXPOS +0x30, TRXREG +0x40, the
image's address at +0x74), the GS infos (0xA0 bytes; TEX0 at +112, CLAMP at
+128), the pixels and the palette. The importer uploads each packet's picture
into a model of the GS's memory as the GS would (PSMCT32 or PSMT8 transfers)
and reads every texture back in its own format (the palette's packet sends a whole
64 x 32 block, though a file keeps only its colours: W_EX010 stores 976, 3,904 of the
8,192 bytes, and ends before the rest, so what the file has is sent and the rest
left 0; read whole, its texture failed and drew white): P_EX110's 8-bit textures are
sent as 32-bit pixels at half the width, so the read-back undoes the GS's
swizzle. The palette is read as a 16 x 16 PSMCT32 rectangle at CBP with CSM1's
order (entries 8-15 and 16-23 of each 32 swapped); `kn::clt2vram` is the PS3's
equivalent. PS2 alpha (0x80 = opaque) is doubled. The whole TW x TH texture is
kept, texture coordinates over it as stored.

## Motions (.mset)

A KH2 model plays KH2's own motions in place of the game's animations, chosen
per animation by `<model>.motions.txt` beside the `.mdlx`:

```text
mset P_EX110.mset   # beside the model
root_motion off     # off (default): the game places the model; on: the body also travels
ik_adjust on        # on (default): the feet on the game's floor (IKAdjust, below); off: as authored
b_ro_A000 0         # <the game's animation> <mset slot>
b_ro_E005 13        # a section: the jump from frame 0 ...
b_ro_E005@12 14 blend 3   # ... the fall from BCA frame 12, cross-faded over 3 frames
```

A slot is the mset's BAR entry number (0 first). A game animation with no line,
an empty slot (`DUMM`) or one it cannot play keeps the game's animation (named
once in the log); a motion whose data turns out unreadable when it plays (a cut-short file)
is named once too, and the game's pose stands in for it. `KHDAYS_TRACE_MODELS=1` logs the names of the animations each
replaced model plays as they change.

**Sections.** `<animation>@<frame>` plays a slot from that BCA frame (30 a
second) until the next section's, for one game animation that is two KH2
motions. The section is chosen by the game's frame at every draw, so an
animation the game starts partway in starts in that section, and one the game
sets back plays the earlier section again. A section's motion starts from its
own beginning when the game's frame enters it. `blend <frames>` cross-fades
from the pose last shown (default: a cut). Before the first section the game's
animation plays.

**Follow.** A Days action that lasts while a button is held (the guard,
`b_ro_Ca003` in `ba/ch/ro/ab.p2` entry 3, 36 frames) does so by the game
setting the animation's frame, not by anything in the BCA0 (a J0AC has only
its frame and node counts and flags; `func_ov022_02097038` sets the frame,
e.g. `Ov022_StepHeldMoveState` holds frame 18). By default a motion's clock
runs on, so a KH2 motion shorter than the hold plays out and stands at its end.
`follow` makes the motion's time the game's frame instead, the section's frames
stretched onto the motion's length:

```text
b_ro_Ca003 40 follow blend 4 loop_blend 32   # the game holds and loops it; the KH2 guard does the same
```

The motion holds where the game holds, goes back where the game goes back
(cross-faded from the pose last shown over `loop_blend` frames, `blend`'s when
it is not given) and ends where the game's animation ends. Once the game's frame
passes the frame it last set back from (the button let go), what is left of the
loop's fade ends within `blend` frames, so a long loop blend does not drag into
the release. Its speed is the game's animation's stretched to the motion.

**Finish.** A Days animation the game cuts short by going back to its idle (or a
KH2 motion longer than the game's animation) can play its KH2 motion out:

```text
idle b_ro_A000                   # the game's idle animations a `finish` plays out into
b_ro_B000 604 blend 8 finish     # the rest of 604, then the idle's motion
b_ro_A007 54 finish 24           # the rest of the damage (54), then slot 24, then the idle's
```

While the game plays the animation nothing changes: another attack, a move, a jump,
a fall or a hit takes over where Days lets it. When the game goes on to one of its
`idle` animations (and plays nothing else) before the KH2 motion has ended, the
motion plays on from where it stood, then each slot after `finish` once from its
start, then the idle's motion fades in; each piece cross-fades into the next over the
line's `blend` frames (a cut without one). The game's own fade out of the animation
does not count against it, but the game playing anything else does: it drops the
tail, which fades out from where it stands over the same frames where the game fades,
and goes at once where the game cuts (another animation at its whole weight on its
first draw: a hit, the death; an idle fade likewise). Faded on the game's frames, it
stood over a hit's first eight frames (the coat restarted round its pose, the legs
then swept through it) and over the death's slow motion for seconds, half of each
pose mixed (his head bent out of shape). Its clock is the
game's animation frames (2 ticks each), so pauses and slow-downs carry over; its
pieces play in place (their root motion out, as every motion here), on the floor
(IKAdjust), and the weapon keeps the KH2 hand's grip (`kh2_tail.h`, `play_kh2`).
Measured on P_EX110: the damage motions R200-R203 (slots 42-54) end on KH2's calm
idle (0.4 degrees mean from slot 2's first pose), slot 24 (R010, 26 ticks) starts
there (0.8) and ends on the battle idle (0.4 from slot 0's): KH2's readying; the
attacks R300, R302 and R304 end exactly on the battle idle, R331 13 degrees off.
Days' attacks are 24-42 frames (48-84 ticks) against 50-80 ticks.

**Into the idle.** `<game animation> blend <frames>` (no slot) gives the game's
own animation a fade into the idle; a section's `blend` is also its fade into the
idle. When the game goes on to one of its `idle` animations (none playing the draw
before) from the animation it played most, and that has a fade, the pose last shown
fades into the game's over those frames, the game's own fade not counted; a KH2
motion or the game's own pose alike. Anything else the game plays (an attack, a
move, the animation played anew) ends it at once, and a `finish` tail fades itself.
Its clock is the game's animation frames (`kh2_moves.h`, `play_kh2`). The pose is held
relative to the model's root, where the game puts it now: held in the view's space,
it stayed where he was while he moved on (an attack's lunge) and he slid forward and
back. Every mix of poses on the model (the game's fades, a `finish` tail, these)
mixes each joint's frames, turns the shorter way round (`attached_frame.h`
`blend_frames`): a mix of the matrices shrank a joint between turns far apart (his
head squashed in the game's fade from the aerial damage into the death, `b_ro_e003`).
The motions' poses (and the game's own, at its share) are mixed joint by joint on the
parent's mixed frame, each pose's joint taken from its parent in that pose (the root in
the model's space): mixed in the model's space, the keyblade arm crossing from the
battle idle into the weapon walk lost its length and twisted, each hand's place the mean
of two, off the forearm's mixed turn. The idle fade's held pose still mixes in the
model's space.

**In the air.** Days' damage in the air plays the ground damage animations and holds
them on a frame (`Ov022_StepInterruptible`: frame 9) until a landing, a recovery in
the air or the fall it times out into. The game's state says it: an actor enters
state 5 for a hit in the air or one that launches it, state 4 for one that leaves it
standing (`Ov022_ResolveGuardBreakState`), and lands in state 0x16 (animation 4,
`b_ro_A004`); `Ov022_EnterActorState`'s hook (prepare.py, `controls_hooks.c`'s
`khdays_controls_actor_entered_state`) hands each change to `khdays_models_actor_state`, by the
actor's scene node (+0x20), whose render object (+0x24) is the model drawn:

```text
landing b_ro_A004                          # the game's landing animation
air b_ro_A005 66 land 70 finish 24         # state 5: KH2's aerial damage (66 front, 74 back) loops
air b_ro_A007 74 land 78 finish 24         # ... landing out of it: KH2's landing, then 24, then the idle
```

In state 5 the line's animation plays its `air` slot from its start, on the game's
frames (60 a second, standing while paused: Days' frame stands still) and looping
from its loop point (P_EX110's R210 and R213 from tick 8). The game leaves it as Days
does: the fall, a recovery, a landing. Days lands it through its fall state for a
frame or two (state 5 ends in state 2, which lands at once), so a landing state
entered 8 vblanks or less after state 5 is a landing out of it: the `landing`
animation plays the `land` slot (R211, R214: 50 ticks, played once) for as long as
it plays, then its `finish` slots as a `finish` line's (`kh2_moves.h`). A fall it
recovered into lands as a jump does. While the game fades the animation out after
state 5 (into the landing, or the fall a timed-out damage recovers into:
`Ov022_StepInterruptible` goes to 0x16 on the ground, else 2), it keeps its `air`
slot; it went back to its ground slot for that fade (R200 at frame 9, cut in under
the landing). Played anew (its weight rising) or gone, it is the ground's again.
Both slots start as the game starts them: a cut
where it cuts (the port adds no fade), a cross-fade where it fades. `blend` on an
`air` line is the `finish` tail's fade into the idle after the landing.
The session log says once that the states reach the model's draws.

**Cuts and the cloth.** A draw where a KH2 motion takes the whole pose with no fade
(a new motion or section, or one started over) tells the cloth so
(`ActorInput::pose_cut`); when a body collider's end moves more than 12 cm about the
coat's reference bone in it, the cloth starts afresh on the new pose, draped round the
legs as a first draw is, instead of carried on from the last pose with a leg on the
wrong side of it (KH2's aerial damage starting out of `b_ro_B002`: a foot 92 cm in one
draw, the leg out through the coat). The cloth's `control` lines see the slot a game
animation plays, an `air` or `land` slot included. KH2's front landing (R211, slot
70) throws a thigh up and forward into a deep crouch, away from where the coat hangs,
so the thighs' carry (`follow_bone`) fell to its `always` share and the leg came out in
front of the coat, whatever the cut, the substeps, `no_sim` or `max_distance` (its
target hangs behind the leg). N_EX830's file has its front and side strips ride the
thighs through the crouch (`control land_front`: `follow_bone_scale 3` for its first
0.4 s, `docs/SECONDARY_MOTION.md`); the back landing (R214) needs none.

**`cut`.** A section's `cut` makes its motion take the whole pose at once where the
game fades into its animation (its weight rising): every other animation's weight is
taken as 0 until the game's fade is done (`b_ro_e003 66 cut`, the death). Days fades
from one animation into another by a table of the pair (`Ov022_SetAnimState`:
`Ov002_GetModeBlendFrames(new, old, model)`, not decompiled; an animation already
playing is not set again), so the same animation is cut into from some and faded
into from others: the death after a hit in the air, on the ground or a guard. It
needs a slot: the game's own pose comes blended from the game.

`start_loop` starts a section's clock at the motion's loop point instead of its
first tick (with `follow`, the section stretched onto the loop point to the end),
for a game that plays a start and a loop as two animations where KH2 has one
motion with a loop. When another game animation was playing the same slot up to
then (the start), the motion carries on from that one's time instead: Days'
`b_ro_A009` is 45 frames, 90 ticks of slot 30's 110 before its loop, and jumping
to 110 snapped. `offset <x> <y> <z> [to <x> <y> <z>]` moves the whole pose,
in KH2 centimetres on the model's axes (x its left, y up, z forward), gliding
from the first to the second over the motion's length; a fade out of the
section keeps the offset last shown. Roxas's ledge (Days' `b_ro_A009` grab,
`A010` hang, `A011` climb up; KH2's P_EX110 slots 30, a grab looping from tick
110, and 34):

```text
b_ro_A009 30 offset -0.8 -5.4 -42.4
b_ro_A010 30 start_loop offset -0.8 -5.4 -42.4
b_ro_A011 34 offset -0.8 -5.4 -42.4 to -0.8 -10.8 2.6
```

`rotate <x> <y> <z>` turns the whole pose about the model's origin (degrees, x
then y then z on the model's axes, before `offset`): a motion lying another way
than the game's. With `KHDAYS_SECONDARY_TRACE=1` a section's first draw logs where
the joints the game drives are in the game's pose and in the motion's (cm, the
root's space), to tell whether it needs one.

`motions <file> [<entry>] at <slot> [own]` adds another mset's motions from that slot
on (past the `mset`'s last; its slots then play as the `mset`'s do, on the same
`skeleton`, or with `own` on this model's own bones, below); with an entry, that
entry of a BAR of sets. KH2's GAMEOVER.mset is
such a BAR: AK10 (#252) and AK12 (#253) are sets of their own, AK10's slots 2, 14,
26 and 34 moving 228, 229, 133 and 238 model joints, AK12's 2 and 14 228 and 133.
Slot 14 (`0140`, 300 ticks looping from 0) is Roxas's: P_EX110's 229 joints, lying
about 150 cm long and under 57 cm high, its hips rising 46 to 58 cm. Days' game
over screen draws Roxas's model playing `ro_con00` (300 frames); slot 14 lies as it
does, head and feet the same way (seen in game), with no `rotate`:

```text
motions GAMEOVER.mset AK10 at 1000
ro_con00 1014
```

`walk <game animation>[@<frame>] <slot> ...` and `camp <game animation>[@<frame>]
<slot> ...` are lines of the animation's own, with every option an animation's line
takes, kept under a name of their own (`<animation>#walk`, `#camp`). The port's walk
(the stick pushed short of running, `controls_hooks.c` `khdays_controls_move_rate`)
moves the actor slower and plays the game's run slowed by as much; while it is on
(`khdays_controls_walk_share`, by the actor's scene node, from its steer this game frame or
the one before; `KHDAYS_TRACE_CONTROLS` logs the share each draw reads), an animation with a `walk`
line plays it instead, at its own pace (vblanks, not the slowed run's frames), mixed
with the animation's own sections by a weight easing between them over the walk line's
`blend` frames, only while the game plays the animation whole: while the game fades it in
(Days crosses idle and run over 5 frames) the walk takes it as it is, and while it fades it
out (no steer then) the mix is kept, so KH2's run never cuts in between. That `blend` is
not an idle fade: the walk fades into the idle with the game, as the run does. While a camp menu draws the model (overlay 25 or its twin 8, as
`runtime/menus/camp.c` knows them), an animation with a `camp` line plays it. With
`weapon_motions` the keyblade plays the same slots (W_EX010_RX has 4 and 7, not 6: the
weaponless walk):

```text
walk b_ro_A001 4 blend 6   # with the keyblade
walk b_ro_e002 6 blend 6   # without
camp b_ro_A000 7           # the camp menu's idle
```

The camp menus' model saves its hand joints' matrices for the keyblade from its own
node callback (`Ov025_RefreshMatchingMatrices`, `Ov008_RefreshMatchingMatrices`:
`NNS_G3dGetCurrentMtx` into its object at +0x150 and +0x180, keyed by the node at
+0x148 and +0x14c), not `Obj_CaptureSelectedJointMtx`; both are hooked to note it
(`khdays_models_joint_saved_at`), so a `weapon` line holds there too.

Measured in the root node's frame (`def_hb` BCA0 entries 5-7 on its model; slot
30 and 34 on N_EX830 through the bone map, root motion out): hanging, Days' left
hand (on the ledge) is at (0.2, 133.0, 43.6) cm and KH2's at (1.0, 138.4, 86.0),
so KH2 hangs 42.4 cm too far forward and 5.4 cm too high; slot 34 starts where
slot 30's loop is and A011 where A010 is. At the climb's end KH2's hips rise 33.7
cm over its idle's (slot 0) and Days' 22.9 over theirs (A000), 2.6 cm less far
back: the climb glides to (-0.8, -10.8, 2.6) so it meets the idle as Days' does
(the head agrees to 0.4 cm in height).

Days' jump is one animation per weapon state, where KH2 has a jump and a fall
(`Ov022_StepJumpState`, `Ov022_EnterActorState`; Roxas's `etc.p.z` / `def_hb.p.z`):

| Animation | Weaponless / Keyblade | Length | Played |
|---|---|---|---|
| 2 | `b_ro_E004` / `b_ro_A002` | 3 frames | the crouch on the ground before take-off |
| 3 | `b_ro_E005` / `b_ro_A003` | 27 frames | the air: from frame 0 on a jump; from frame 12 (`0xc000`) on walking off a ledge; while falling the game sets it back to frame 16 (`0x10000`) at its end |
| 4 | `b_ro_E006` / `b_ro_A004` | 15 frames | the landing (state 0x16) |

So a section from frame 12 is the fall: a ledge starts in it, and the game's
loop back to 16 stays in it while its motion runs on and loops from its own
loop point. The section changes at frame 12 whatever the jump's height: the
game's rising/falling phase (aim mode 1/2) is not read yet.

**The game's side** (`port/runtime/mods/models.cpp`). NNSi_G3dDrawInternal
hands the port its render object; its joint animations (anmJnt +0x10, each
NNSG3dAnmObj: frame +0 fx32, ratio +4, resAnm +8, next +0x10) are named by
their BCA0's JNT0 dictionary (`names.cpp` `khdays_names_animation`). A motion's
clock starts when its section starts and moves with the animation's frame (2
ticks a BCA frame), so the game's pauses and speeds carry over. A frame set back
is counted on, one step on (the last forward step), when the KH2 motion loops
(its loop point before its end): it loops itself, smoothly, whatever the game's
frames did (the idles; the fall set back to 16; a run wrapping after a cross-fade
held it on its last frame: `Sequence_UpdateTracks` clamps the incoming animation
at its end until the fade is over). A KH2 motion that does not loop starts its
section over instead (`kh2_clock.h`; a followed section takes the frame either
way): a hit while a damage animation plays rolls one of its direction's two
(`Ov022_TurnAwayFromDrift`: A005/A006 in front, A007/A008 behind), and the same
one again is not requested anew (`Ov022_SetAnimState` ignores a repeat) but
rewound to frame 0. Counted on, the 70-tick KH2 damage motion ran to its end and
stood there while the 30-frame Days animation played again. Telling a rewind from
a loop by the game's frames instead (a wrap from near the end, a frame not held)
restarted the run after each cross-fade, snapping it (worse with haste). Past its end the motion
continues from its loop point (retFrame), or holds when there is none. During
the game's cross-fade (two animations, their ratios summing to 1) each side is
blended at the game's weight, the game's own pose standing in for an unmapped one.
Each actor (render object) keeps its own pose, started afresh when the render object draws
another model or has not drawn for 6 game frames (`actor_of`), so nothing of a pose or tail from
before shows. A pause does not count: no game frame passes in it.

**Root motion.** Days' animations never move Roxas's root node (`roxas_R`; all
12 of def_hb's checked): their movement is on `Bip01`, and the actor's place is
the game's. KH2's bones carry the motion's travel themselves (P_EX110's R101,
R300-R304: bone 0 travels as far as PositionInfo says), and KH2 moves the object
by PositionInfo (`YS::MOTION::get_trans_direct`) while taking it out of the bones
(`MotionContext::negativeTrans`). So with `root_motion off` the port multiplies
the bones by the inverse of PositionInfo's matrix and places them on the root
node: the travel is drawn once, by the game. `on` keeps it in the bones. The
root node is the game model's first (Roxas's `roxas_R`, never animated), not
what bone 0 is paired with, so `kh2_0 Bip01` in `bones.txt` moves the body with
Days' hips in Days' animations without adding them to KH2's motions.

**Another model's mset.** A model can play an mset made for another model's
bones (the coat Roxas, N_EX830, on the street-clothes Roxas's P_EX110.mset):

```text
skeleton P_EX110.mdlx   # the model the mset was made for, beside this one
bone 0-3 0-3            # <this model's bone> <its bone>, or two ranges as long
ik 5 6 7                # a limb: upper, lower and end bone of this model
ik 111 112 113 133      # ... reaching where bone 133's bone carries the end
ik 14 15 16 lengths     # ... at the mset model's limb lengths and way (the knee bends as its does;
                        #     the two bones stretched along their length only, their children not)
```

The motion plays on the other model's skeleton exactly as it would there
(IKAdjust with that model's feet), then `kh2::Retarget` gives this model's
bones the pose: a mapped bone is turned from its rest as its bone is from its
own, its head where its parent carries it (this model's lengths; a root moved
as its bone moves); an unmapped bone (a coat, sleeves) stays at rest under its
parent. An `ik` limb is then bent at this model's own lengths so its end reaches
where the other model's end bone (or the `ik` line's fourth, a bone it reaches
with) carries this end's rest place, its middle toward where the other's middle
carries this middle's: the law of cosines about an up vector, as
`Motion::calcIKJoint`.

**Two skeletons.** `mset <file> own` and `motions ... at <slot> own` mark an
mset made for this model's own bones: its slots play on this model's skeleton
and are not retargeted, while the others play on the `skeleton`'s and are
(`own_slot`, `skeleton_of`, `pose_on_model` in `models.cpp`; a track's player is
made anew when its slot's skeleton changes). So one model plays KH2's motions
made for it and another model's at once: the dual wield Roxas below plays
B_EX390.mset on its own bones and P_EX110's and GAMEOVER's retargeted. Without a
`skeleton` line every slot is the model's own, as before. A bone the other
skeleton has no match for can ride another of this model's bones as an own
motion holds it:

```text
carry 95 149 at 0   # bone 95 on bone 149, at its frame on 149 in slot 0's first frame
```

`kh2::Retarget::carry` then places it on that bone's posed frame (unscaled) in
every retargeted motion, its children with it, instead of by its own source or
parent; the slot must be an `own` mset's.

A bone that carries others (its children, its own vertices) must follow a bone
that moves the same body part. A bone no vertex follows can sit far from its
rest frame in a motion: P_EX110's hip bone 3 turns about 90 degrees from its
hips' mesh (bone 2) in idle, and its thigh's IK bones (7, 9) are built by the
IK solve; only its thigh skin bone 16 turns with the thigh. Mapped onto bone 3
(which carries its legs and coat), N_EX830's hips came out turned 90 degrees and
its legs twisted. So N_EX830's bone 3, hip joints and toe joints follow their
parents, its thigh follows 16, its ankle and foot 11; the rest of its table
(spine, arms by their own chains, hands, fingers, head, hair) pairs bones one
for one. Riding each bone on its own source instead (as if skinned to it) fixed
the hips but moved the hands and fingers and bent the sleeve cuffs, so the
placement stays by parents. Checked offline on P_EX110.mset's motions: the upper
body exactly as before the hips' fix, bone 3 turned as P_EX110's hips within
their 3.7 degree rest difference, the coat fixed to the hips, the legs at their
own lengths, the hip joints 5-6 cm from P_EX110's (13-16 before; the rest is the
root's place, kept as it was).

The hooded coat Roxas (B_EX390, as Days' hooded `roxas_ho_h` in `def_hho.p.z`)
has N_EX830's bones at the same rest places (all within 0.3 cm, the same
parents) but numbered apart: 273 bones, without N_EX830's hood (100-106), face
and hair (171-223) and 276-281, with two keyblades of its own (95-116). So its
files are N_EX830's renumbered: N 0-94 the same, 95-99 +22, 107-158 +15, 165-170
+9, 224-275 -29, 282-307 -35. Its drawstrings are its own (182-186, 187-191,
under the head's base, 176, hanging straight down a centimetre into the coat's
chest at rest); `tools/hd/kh2_models/graphics/ba/ch/ro/def_hho.p.z/mdl/roxas_ho_h.secondary.txt` hangs them forward
against a collider of their own over the chest. The keyblades are skinned to bones
96-105 and 107-116 alone and hang 60 cm beside him at rest; bones.txt's `hide`
over every height (`hide kh2_95 ... kh2_116 from 1000 to -1000`) drops their
triangles. Shrunk to a thousandth with `mesh_scale <bone> 0 0 0`, they still showed
in game as one-pixel lines.

Its collar is skinned rigidly to the neck (175, from 133 to 145 cm, blended into
the hood's 178 only at the top), and P_EX110 turns its head 20-75 degrees on the
neck in its idles, all of it in bone 113 (111 and 112 stay still on the neck,
110). So 175-178 all take 113's turn (`bone 175 113` and so on; checked in game):
on 110, the hood turned that far off the collar. 176-177 carry the drawstrings:
left on 111-112, they turned with the neck while the collar turned with the head,
and the strings ran back along the neck before they fell. In bones.txt, 175 pairs with Days'
`Bip01_Head` and 176-178 have no pair of their own: they ride 175.

B_EX390 is also Days' dual wield Roxas (`ba/ch/r2/def_h.p.z`, `roxas_bf_h`), which
plays KH2's own motions for B_EX390 (`mset B_EX390.mset own`, 257 slots) as well as
P_EX110's (`motions P_EX110.mset at 1000`) and the game over (`motions
GAMEOVER.mset AK10 at 2000`, `ro_con00 2014`), those two retargeted as for
`roxas_ho_h`. Days' model (the game's animations `b_bf_*`) holds its keyblades at
`ro_h_L` and `ro_h_R`, not `ro_w_tg_*`. B_EX390's keyblades hang from bones 95
(left) and 106 (right), each blade along its bone's -x, the flat across z. In every
B_EX390 motion checked (slots 0, 1, 4, 5, 20), 95 rides the left weapon bone 149
and 106 the right one 222 rigidly (5.3 and 5.1 cm out, one turn throughout). So:

- `weapon ro_h_L 95 rotate 180 90 0` and `weapon ro_h_R 106 rotate 180 90 0`: the
  turn of the hooded model's `weapon ro_w_tg_R 222 rotate 0 -90 90` taken onto 106's
  axes, so Days' keyblade (along its +z, teeth +y) lies along the baked blade
  (along -x, teeth toward -y, where the blade reaches further: 25 cm against 15);
- `carry 95 149 at 0` and `carry 106 222 at 0`: in P_EX110's motions the keyblade
  bones ride its weapon bones (P_EX110's 63 and 172) at that frame (offline: 6.2
  and 5.5 cm from them in slots 0, 2, 8, 604, the frame on them B_EX390's exactly);
- bones.txt pairs `kh2_95 ro_h_L` and `kh2_106 ro_h_R` with `place`: in the game's
  own animations the bones sit on its weapon joints, and the keyblades take the
  game's own frame there (its share of the pose is whole). A placed joint holding
  its own node's weapon is turned at load so the `weapon` line's frame on it is the
  node's (`load_kh2_motions`: its correction N (W T)^-1, N the node's rest turn, W
  the joint's, T the line's). With the joint's own rest turn left in, a fade
  between a B_EX390 motion and a game animation spun the keyblades a whole turn:
  the joint's blend and the hold's turn toward the game's frame went opposite ways.
  Neither node is a paired joint's only paired child, so nothing above them is
  turned or stretched.

The left blade's turn is the right's, unchecked in game: Days' left keyblade may be
made the other way about.


**Weapons.** The game places what an actor holds on joints of its own model:
`Ov030_CreateActor` looks up `ro_w_tg_L`, `ro_w_tg_R` and `roxas_R` (actor
+0x514..+0x51c), and while the body is drawn `Obj_CaptureSelectedJointMtx`
(ITCM 0x01fffe68, a node callback) saves each one's current position matrix
(MtxFx43) at +0x528/+0x558/+0x588 (`func_ov022_0209c774`: the attachment for a
part's kind); the weapon models are drawn on those. A KH2 model's motions follow
the KH2 bones, not those joints, so `motions.txt` can name the bone instead:

```text
weapon ro_w_tg_R 172   # <the game's joint> <KH2 bone>
```

A hook after the save hands the port the matrix; at the body's draw end it is
written over with the bone as drawn (its joint's skinning matrix times its rest
pose: the KH2 motion, or the game's pose for an unmapped animation), in the
nodes' space (DS fixed point, the same layout). It is turned as the game's
own saved matrix is by the game's own animation's share of the pose
(`attached_frame.h`): in an unmapped animation the weapon keeps the game's grip
at the bone's place, in a KH2 motion the bone's frame and the line's turn, and
across a fade between them it turns from one to the other. KH2's grip (the bone
and turn) is not Days': the KH2 hand follows the game's hand, but Days holds its
keyblade turned against it, and in `b_ro_D002` (`ba/ch/ro/ma1.p.z`, fire cast in
the air) a blade Days points straight ahead pointed about 45 degrees up. The weapon
bone itself never turns against the hand in P_EX110's motions (0 degrees in all). The weapon's root sits on the
bone's frame, as KH2 attaches one: right for a KH2 weapon model; a Days weapon
model, made for Days' joint, may sit turned. A KH2 weapon's rest pose is not
its held one: W_EX010's bone 0 rests 1.2 m from its origin, turned (0, 270,
270 degrees), and every motion of W_EX010_RX.mset puts it within 6 cm of the
origin nearly unturned (123-147 degrees from rest), so the weapon is drawn right
only while its own motion plays (its slot matching the body's: R000 at 0 in
both). A weapon drawn before the body in a
frame takes the last frame's place, as in the game. Joints without a line keep
the game's place; the line works without an mset.

`weapon ro_w_tg_R 251 rotate 0 -90 90` turns that frame (degrees about the
bone's own x, then y, then z) for a Days weapon. KH2 attaches a weapon to the
bone `PARTY::hand_to_bone` names (`YS::WEAPON::WEAPON` 0x4bb468: `OBJ::attach`
on it, the weapon's mset synced to the body's), so a KH2 keyblade's own frame
is the bone's. Days' Kingdom Key (ro_w00600, a textured card) runs along its
+z from the grip (tip 0.83, handle end -0.17; teeth toward +y, from its
texture's opaque texels); KH2's (W_EX010, bone 0's frame) runs along +x (tip
78 cm), teeth toward -y, flat across z. The turn taking one onto the other,
Days x to KH2 +z, y to -y, z to +x (`rotate 0 -90 180`); of the 24
axis-aligned turns it lands the Days blade nearest KH2's (5.2 cm mean, a card
against a solid blade; teeth the other way 6.3 cm, every other turn 7-21). In
game that held the blade 90 degrees off in its own plane (pointing at the floor
in the battle idle, where bone 251's z faces the camera), so the frame KH2
holds its keyblade in is not bone 251's own: a further -90 degrees about the
bone's z (`rotate 0 -90 90`) puts the blade along the bone's -y.
Which bone `hand_to_bone` names for N_EX830 (its skeleton table) is not read. KH2 also turns
its keyblade in the hand per motion (the weapon mset's bone 0: 15 degrees about
the blade's flat normal in W_EX010_RX's slot 0, 40 in slot 2).

`weapon_motions <mset> <mdlx> <bone>` gives the held weapon those turns: each KH2
pose of the body plays the same slot of the weapon's mset at its time, on the
mdlx's bones, mixed by the same weights (KH2's weapon mset is synced to the body's:
W_EX010_RX's 819 slots are P_EX110's, its 42 motions the same names and lengths;
the five body slots without one, 6, 10, 218, 534 and 558, play while the keyblade
is put away). `<bone>` is the weapon itself: W_EX010's bone 2 carries the blade's
834 vertices, 3 the keychain's ring, 4-10 its links and 11 its token (bones 0 and 1
none; an mdlx's palette entry 0 is an identity, so entry n is bone n-1). The weapon
is drawn at the `weapon` line's frame times its blade's turn from its held pose
(slot 0's first frame), so in KH2's idle it is the line's turn alone. A slot the
weapon has no motion for, and the game's own animations, count as Days' hold
(their share of the pose held as the game holds it); without the line nothing
changes. A replaced weapon model whose `bones.txt` says `held <joint>` has its
bones from the root to that joint placed from the same pose and the bones under it
carried (W_EX010 as `w_.p2#0/mdl/ro_w00600.mdlx` with `held kh2_2`, its keychain in
its own `.secondary.txt` as a chain); a weapon model drawn before its body takes
the last frame's pose, and one whose body has not been drawn for a second stands
as the game's nodes place it. Days' own keyblade, unreplaced, gets the turns
through the frame it is drawn at; dumped to glTF and put back as the mod its
keychain (one bone, `ro_w_01` at the handle's end, under `ro_w_00`) can be
simulated as well.

**Fingers under the game's animations.** Days' models have no fingers, so a
KH2 model's unpaired bones ride their parents at rest (an open hand) whenever
the game's own animation poses it. `ds_pose` lines in `motions.txt` turn them:

```text
ds_pose 2 135-156 252-273                  # as mset slot 2 (frame 0) turns them
ds_pose 30@12 141-156 for b_ro_A005         # another slot's frame for one animation
ds_pose rotate 0 0 20 252-257 for b_ro_e000  # or degrees about each bone's own axes
```

Each bone takes its turn from its parent as the slot's pose (through the bone
map) has it against rest, about its head; the game animation with the most
weight that plays no KH2 motion picks its own lines, else the default's (lines
without `for`; later lines win). It changes only the game's share of the pose:
a KH2 motion's fingers are its own. Bones paired with a game node in
`bones.txt` are left to it.

**Hands that respond.** `hand` lines in `motions.txt` pose the fingers each draw
under the game's own animations (a KH2 motion's fingers stay its own), move the
hand out of the body and the room, and rest it on what it presses. They win over
`ds_pose` for the bones they name (`models.cpp` `pose_hands`, the maths in
`hand_pose.h`):

```text
hand 250 229 252-273 back 0 0 -1   # hand bone, forearm bone, fingers; `back`: out of the back of the hand, its axes
hand_bend 10 45                    # the open pose's share of the wrist's bend back, degrees
hand_fist 1.5 3                    # the fist's share of the hand's speed against the body, m/s
hand_walk 2 0.45 0.85 0.8          # the fist's share of how fast that bone travels: from, to, the most it gives
hand_contact 0.07 0.13 0.8         # the palm's distance to a surface for the flat pose, all to none; most speed
hand_grip_fade 0.15                # seconds for the grip to close or open
hand_lag 0.6                       # of the wrist's turn the fingers lag by
hand_finger_link 0.8               # how much of a finger's opening for collision each beside it takes; 0 each alone
hand_body 98 axis 98 166 margin 1  # a part of the body: the bones carrying it, its axis, a margin in cm
hand_pose fist rotate 2 80 -3 259  # relaxed|open|fist|flat|grip: degrees about the bone's axes, x then y then z
hand_anim fist both e_rox_911a     # one animation's pose whatever the rules say (the grip still wins)
```

A bone a pose does not list is at rest in it. Later poses win over earlier ones by
their shares: relaxed, open, fist, flat, grip. On e_rox_hb the poses are means of
frames of KH2's 191 loose cutscene motions (`.anb`: a BAR holding one motion, not an
mset's entry), and the grip is P_EX110's battle stance on the keyblade (slot 0). What
those motions show, over 28,940 hand frames: fingers curl a median 120 degrees (the
sum of a finger's three joints), 68 past a 50 degree bend back, and 200 above 2 m/s
of hand speed. Their resting hand curls more toward the little finger. The right hand's turns are the
left's with x and z negated. Measured on Days' side, over the scenes recorded: walks travel about 1
m/s (`e_rox_908a`-`910a`), runs about 5 (`e_rox_911a`), shuffling steps under 0.5.

The bend is read from the forearm's direction in the hand's axes (+x along the
fingers, checked on N_EX830: bones 134 and 251 lie along it), so only a bend toward
the back of the hand counts: not one toward the palm, one sideways, or a twist about
the forearm. N_EX830's left hand has its back along +z and its right along -z (from
the fingers' curl in the grip).

The grip takes over while something is attached to the hand. The game has one
place for that: `SceneNode_AttachToModelJoint` links a child under its parent's
scene node by a joint name, and `Ov023_CmdAttachWeapons` and
`Ov023_Cmd_ParentEntityToEntity` (the event script's commands) both go through it.
A scene node (a drawn object's render object at +0x20) has its parent at +0xbc,
first child at +0xc0, next sibling at +0xc4 and joint at +0xc8. A child counts while
its own render object was drawn in the last 6 vblanks. Its flags are no guide: the
byte `Render_DrawViewLists` tests (the entity's +8, bit 8) read 7, clear, on every
attached child recorded over 58 scenes, drawn or not, so why that draw path passes it
is not understood. The joint is on a hand when it is that hand's game node or under
it, or when a `weapon` line puts it on a bone under the hand's. `ro_w_tg_R` is a
child of `Bip01_R_Hand` (on roxas_b_h, session log; not read off e_rox_hb's own
nodes).

Contact: the room's collision (the cloth's, `gather_room`) holds the bed, as floor
faces. A face counts when it lies straight under the palm (within 4 cm of the
palm's foot on its plane: nearest by distance took a pillow's or the bed frame's
height, and the hand jumped 4.6 cm), the palm faces it and the elbow is in front of
it. Once found it is kept 3 cm further and at a lower facing, and its plane eased
over 0.12 s, so the hand glides over a step. The hand then turns at most 50 degrees
onto it and its palm's middle comes to 2.2 cm over it.

The push: the wrist moved out of the body's parts and the room, onto a surface it
rests on, on a critically damped spring (22 per second), the upper arm and forearm
bent to reach it at their own lengths and the hand moved whole. So it may lag what
pushes it for a moment. A part of the body (`hand_body`) is how far its vertices
reach by distance along an axis and angle round it, ten stations of sixteen, so the
chest is its own shape and not a capsule; a point passing its end comes out without
a jump. Each finger opens toward the flat pose as far as it must to clear them, found
by bisection, from 0.9 cm off the body's own surface (the parts' margins are the wrist's:
with them, a hand hanging by the thigh stood open, 0.33 on average in `e_rox_951`, now
0.02). The four fingers give way together: each opens at least `hand_finger_link` (0.8)
per finger of what the others need, the thumb alone, so one finger is not left curled
while the next lies flat. Each opens from its own curl, so they keep their differences,
and with nothing in the way none opens. Eased both ways (0.06 s opening, 0.2 s closing).
It stays in the grip while the hand holds something. The fingers lag the wrist's turn
and the hand's travel by up to 14 degrees, the index first.

Not covered: `Ov023_Cmd_AttachEntityToEntity` hangs an actor off a parent's track
by its position alone, with no scene link (`Ov023_ActorFollowParent`), so it does
not count as held; whether a cutscene uses it for something in a hand is not
checked. A prop only animated near the hand is not attached either. The other arm is
no part of the body here. Checked by numbers, not by eye: a headless run's shots leave
out replaced models. Last measured on 5 scenes (the bed's and the ice cream's); before
the smoothing, one deep frame of 17 cm was left unexplained.

`KHDAYS_TRACE_MODELS=1` logs each hand's bend, shares, hold and push twice a second, and
names a joint something is attached to that is on no hand.

`swing <joint> <by joint> <frame joint> [<share>]` in `bones.txt`, beside `twist`: the
joint and the joints under it swung about its head by a share of how far the other
joint's x has swung off where `frame` would carry it. e_rox_hb's cuff roots tilt 0.7
with the hand (`twist` alone leaves them straight on the forearm as the wrist bends).

**Cuffs.** N_EX830's and B_EX390's sleeve cuffs are fitted round the hand each draw,
not simulated (`models.cpp` `pose_cuffs`, the maths in `cuff_fit.h`):

```text
cuff kh2_116 kh2_133 kh2_117 kh2_126 kh2_123 kh2_120   # root, hand, the four flaps in order round the ring
cuff_fit 0.3 20 0.72 0.35 0.62 0.74 2                  # for all a model's cuffs
```

`cuff_fit`: clearance (cm), most open (degrees), shortest (of a flap's length), how much of
a flap's opening its neighbours take, where along the cuff a point starts being cleared
by shortening and where only by it (of its length), most slide (cm).

`cuff_thumb <more clearance cm> [<most open degrees>]` tunes the last
flap of each `cuff` line, the thumb's tab, alone: its neighbours copy none of it. Clearance
counts by how much of a point's surface the tab carries; most open 0 or left out is
`cuff_fit`'s. Over 3 scenes on e_rox_hb, `cuff_thumb 0.5 28` took the tab's mean opening from
9.3 to 12.0 degrees with the others unchanged, and the hand through the cuff from a median
0.43 cm to 0.33 (99th percentile 1.27 to 0.96).

`cuff_pose` gives the cuff an authored pose for a hand bent up, toward the back of the hand,
blended by how far it is bent, with the fit answering what is left on top:

```text
cuff_pose 110 2.66                              # the hand's bend the pose is for, degrees, toward each cuff's third flap; its pivot, cm
cuff_pose_bone kh2_244 0 0 68.3 0.69 1 1        # a bone's turn from its parent there (x y z degrees, as `hand_pose`) and scale
swing kh2_233 kh2_250 kh2_230 0.8 kh2_240       # the root swings for every bend but toward kh2_240, which the pose answers
```

A bone's change is its rest turn and scale against its parent's, so children ride it and a
scaled parent carries them in. Its share is the hand's bend in the plane toward the third
flap, as measured in the root's frame, over the pose's bend: 0 for a hand bent the other
way, held at all of it past that, eased by a critically damped spring. The surface is baked at six even shares; each draw fits on the two either side and mixes their answers, and each flap
opens from where it stands there, so nothing is corrected twice. Lines for a joint a model
lacks do nothing; bones with no line keep their rest place but ride their parents.

With a `cuff_pose`, the hand's `flat` pose is the fingers at its full bend: over the last tenth of the
way they go to it by the bend alone (`cuff_pose_straight`, not while gripping). Below that, the bend
brings in the `open` pose, and only contact straightens the fingers toward `flat`: the palm's nearness
to a surface, and each finger opening to clear what it would go into. The `open` installed is halfway
from `flat` to `relaxed`, slerped per bone.

A `cuff_pose_bone` also takes the root and the root's parent, the sleeve's ring (kh2_232, kh2_115),
which holds the coat's forearm from a head far up the arm: scaled along its x, it shrinks the forearm
and brings the cuff up with it (0.67 takes the root 4.9 cm up). Its change goes on last, in its own
frame, since `swing` turns the root apart from it.

Over the last quarter of the way to the pose (`cuff_pose_hold`), what moves the cuff and hand on
top of it fades out, so at the full bend the pose shows as authored: the cuff's fit and sway, and
the fingers' fists and lag. Nothing then clears the hand, so what clips there stays, and past the
full bend it stays so. Hands in the recorded scenes bend up to 108 degrees.
Still on top:
`twist`, `swing` to the side, the grip when holding, animation overrides, and the surfaces' turn.

The pose covers one direction. A hand bent palm-down or sideways keeps the `swing`: without
it, about a quarter of the frames over 4 scenes were palm-down 15 to 40 degrees, and they were
the deepest by far. The direction it leaves out is read from the root's own frame, since `twist` has
already turned that; and what it swings to the side keeps its size rather than being scaled
back up, or the little left of a hand bent near square swings the root 50 degrees.

The pose installed is hand-authored, a full bend of 110 degrees (a posed FBX of the right hand): each flap bone's local turn
and scale read against the rest FBX, rebuilt within 0.001 cm with no shear, and the flat hand's
`hand_pose flat` lines from the same file. In that file the hand pivots at its helper, 2.86 cm past
its head, so at a bend of 86.6 degrees the head stands 3.9 cm from the wrist; in game it stays within about
1 cm, where 46 of the hand's mesh edges stand through the cuff against none as authored. So the
cuff slides the other way along that arc (`cuff_pose_slide`, the second number of `cuff_pose`, in
the model's own scale), the hand staying where the game puts it, with the wrist ring and all under
it, the sleeve above stretching to meet it. Rest axes are the same on all four models'
cuffs, read from the files: 0.02 degrees at most, with bones +15 left, -29 right on B_EX390. The
left's lines are the right's mirrored: rest frames are right's with x negated and y flipped,
checked per bone, and clipping over the same bends matches within 0.01 cm.

Why not cloth: the cuff is 11.8 cm long from the wrist, almost to the knuckles, four flaps
of eight points in all. At rest the thumb stands about 1 cm outside the thumb-side flap, so
contact never rested: the follow spring pulled the flap into the thumb and contact threw
it out, a cycle of 10 vblanks with the hand still (1.8 cm in `e_rox_102`). So nothing
collides. The cuff's rest surface is baked by distance along its axis and angle round it,
with how far each flap moves it per radian; the far end's tabs have real gaps, which stay
gaps. Each draw the hand's vertices (thinned to a centimetre apart) are read in the cuff's
frame. The whole cuff first slides across its axis, since near the wrist a flap has no
leverage (B_EX390's animators move theirs up to 2-3 cm); near the far end a point is cleared
by the flap drawing back past it; the flaps open for the rest. All of it on critically
damped springs, with a soft spring on each flap for sway and droop on top, so there is
nothing to fight and it cannot overshoot.

Sharing a flap's shortening with its neighbours was measured and left out: the same clipping and jitter over 3 scenes, with the flaps shortened more
often. B_EX390's own motions scale neighbouring flaps far apart too (0.3 on the palm side,
1.0 on the thumb side, read from its motions), and the shortening here falls the same
way: palm flap 0.82 of its length on average, thumb tab 0.86, the others about 0.97.

Against the drawn mesh, over 3 scenes on e_rox_hb, the hand's vertices through the cuff went
from a median 2.83 cm to 0.43 (99th percentile 6.23 to 1.27), and the flaps' ends' jitter
p99 from 0.79 to 0.66 cm (the baseline over 6 scenes). What is left is mostly the thumb's outer
joints where the thumb stands far out (`e_rox_102`). Flaps open a median 10 degrees, 20 at
most. B_EX390's own-motion slots scale their flaps as authored and the fit does not read
that, so it may open more than they need. roxas_ho_h and roxas_bf_h share N_EX830's cuff
layout (bones +15 left, -29 right, checked at rest) but no headless scene draws them.

Over all 161 cutscenes (before `cuff_thumb`): through a median 0.59 cm, 99th percentile
1.27, worst 5.93, past 0.3 cm in 69 % of frames; the ends' jitter p99 0.36 cm, none over 3 cm.
So jitter is gone and clipping is reduced, not gone: `e_rox_962` (p99 4.84), `e_rox_960` (3.04)
and `e_rox_974` (2.90) are the worst, most at the opening's limit. Measured by numbers; the user
judged the result in game.

**The format** (`kn::MotionPrototype0RAW`, all offsets from the header after the
0x90 bytes the game keeps): u32 type +0 (`kn::motionSetup` 0xf8e8c: 0
`MotionPrototype0`, 1 `MotionRAW`, every bone's matrix baked per frame, not
played; a weapon's motions carry 1 at +4, not their type), u16 model joints +0x10, u16 joints with the helpers
+0x12, then table offsets and counts: helpers' hierarchy +0x18 (kn::ModelHRC,
0x40 each: u16 id, s16 parent +4, scale with the bone's length in w +0x10,
rotation +0x20, translation +0x30), flags +0x1C (u32 per joint in the order
they are solved: id bits 0-15, IK 16-17 (1 aim, 2 and 3 two bones bending
either way), keep its global place 18, global turn 19, 20 unused, calculated
21 (placed by the IK before it), toRot 22, effector from the table at +0xA0
23), time count +0x20 and table +0x44 (ticks), constants +0x24/+0x28 (u16
joint, u16 track, f32), PositionInfo +0x2C, direct curves +0x30/+0x34 (the
model's joints), indirect +0x38/+0x3C (the helpers), keys +0x40 (u16 type in
bits 0-1 and time index above, u16 value, u16 left and right slope), values
+0x48, slopes +0x4C, constraints +0x50/+0x54 (u8 type, u8 active, u16 passive,
u16 active[2], u16 activation count and base), activation +0x58 (f32 time,
u32 on), limiters +0x5C (0x30 each), expressions +0x60/+0x64 (u16 joint, u16
track, u16, u16 node), their nodes +0x68/+0x6C (u32: type in the low byte, global
bit 8, joint in the high half; f32 value; u16 car, cdr); start, end, fps and
loop frame at +0x90.

Each joint's pass (`calcMatrix` 0xfc500): its expressions (tracks 0-2 scale,
3-5 rotation in degrees, 6-8 translation); its active constraints (0 position,
5 between two joints and aimed at the second, 2 the turn of another, 3 aimed at
another, 4 an IK up vector, 6 scale, 12 position limiter, 13 rotation limiter);
an IK chain to the position constraint on the joint after it (`calcIKJoint`:
the second bone placed only at the first's length along X, and the first only
at its translation's x, as the PS3 build does); else its own turn, Rz * Ry * Rx.
Expressions (`evalExpression` 0xfb048) work in degrees: 0-5 sin cos tan asin acos
atan, 6 ln, 7 exp, 8 abs, 9 pow, 10 sqrt, 11 min, 12 max, 13 mean of a list, 14
if, 15 and 43 an F-curve at a time (43 in degrees), 16 the distance between two
joints, 17 fmod, 18-22 + - * / %, 23-27 == > >= < <=, 28 and, 29 or, 30 the
frame, 31 a constant, 32-34 a joint's translation, 35-37 its rotation, 38-40 its
scale (local, or global with bit 8). Limiters (`calcLimitValue` 0xf9878): type 1
a sphere, else a box; with damping eased over the last width (the PS3 build
eases the minimum side above min + width; kept).

**IKAdjust.** The two-bone IK can lift a foot onto the ground: the model's
collision list (BAR entry 0x17, 20-byte YS::COLLISION_ELEM after a count) names
the feet (types 10 and 11, the bone and its height), and KH2 casts a line down
through the foot's target (`make_ik_adjust`, `calcMatrix`'s Octree test).
`MotionPlayer::evaluate` does the same through a ground callback.

KH2 (`calcMatrix` 0xfc500, its world y down): the line runs from the IK target
one leg (the child's global scale times its length) above to the foot's Height
below it, a leg and two Heights below for a `below` foot (type 11);
`dk::Octree::intersectLineIk` tests it against the background only, and a hit
puts the target the Height above it. `make_ik_adjust` (0x45992c) lists the feet
only while the object touches the ground (`YS::OBJ::is_hit_bg`, or its +0x588
flag 0x20) and its motion's IK flag is up; otherwise none.

Days' side (`models.cpp` `floor_of`). `Mover_Step` puts a mover with gravity on
its floor every frame through `Mover_ResolveFloor` (five rays down from above
its owner, the highest hit below its head; a steep slope where the floor
normal's y is under 0xf33: Days' world is y up, like the solver's). Standing,
its vertical speed (+0x38) is 0x80000000 and only that is called;
`Actor_ResolveGround`, which casts up (ceilings), runs only in the air. A hook
in `Mover_ResolveFloor` hands the port the mover: its owner at +0 (the cast's
`exclude`), its floor's collision index at +0x46. The owner's animation state
is assumed at +4 (`func_ov022_020a3c78`) and its render object at +0x20 of that,
so a drawn render object R would belong to owner R - 0x24 (not yet seen to hold
in game; `KHDAYS_TRACE_MODELS` names the movers near R when none matches). While
the mover is on its floor (seen within 4 vblanks, its speed 0x80000000) the
model's feet are put on that floor: the solver's world is the inverse of the
latest flush's camera (`NNS_G3dGlb` +0x4c, `khdays_g3d_camera`) times where the
game draws the model, times centimetres to metres and the root motion taken
out. A line is cast with the game's own `Collision_RunRayCast` on
`GetTrackEntryBase(index)`, the actor left out, in DS fixed point, the hit point
by `Vec3ScaleAddQ27` as the game finds it.
`ik_adjust off` in `motions.txt` turns it off. Not done: KH2's per-motion IK
flag (the motion object's bit 0) and what it does with a hit beyond the foot
(`check_ik`: a script callback); the hips are not lowered on a slope.

## Faces (texture animation)

A character's eyes and mouth are sprites drawn over a rectangle of one of its
textures: a `TEXA` chunk each in the footer after the texture data (then
`_KN5`). Layout from the PS3 EBOOT (`kn::TEXAnmWork::getSequenceNo`, `::set`,
`::play`, `::getPixelData`, `::getX`, `::getW`); offsets into the chunk's body,
little-endian on the PC:

| Offset | Field |
|---|---|
| +2 | target texture (GS info index) |
| +4 | layers (`multi`) |
| +6 | bits per pixel (8 seen) |
| +8, +10 | first and last slot id |
| +12, +14 | script and sprite counts |
| +16 | x (& 0xfff; 0x1000: the size is the texture's) |
| +18, +20, +22 | y, width, height |
| +24 | slot table: a u16 per id, script + 1 (0 none) |
| +28 | script table (u32 offsets) |
| +32 | sprite pixels, row-major, sprite i at i x w x h, the target's palette |
| +36 | default script |

A script's steps are 8 + 2 x layers bytes: u16 code (low 4 bits: 0 a sprite,
1 the texture's own picture, 2 a jump by the signed 12-bit `code >> 4` steps
from the jump's own step, 3 stop), u16 min and max frames (`vrand(min, max +
1)`), u16 sprite. `play()` plays the default script unless `set(slot)` was
called since the last play; a script that starts shows its first step a frame
before it moves. Each draw sends the whole texture, then the sprite of each
script at a sprite step (`kn::ModelSKL::setTexture`): a stop or a base step
shows the texture's own picture.

N_EX830: eyes over tex1 at (0, 20), 96 x 48, 18 sprites, default slot 12
(sprite 4 for 2, sprite 5 for 6, sprite 0 for 30-240: the blink); slots 1, 3,
5, 7 hold shapes 0-3, 13/15/17/19 the same shapes blinking, 10 closed, 65-162
the special eyes (no blinking variants). Mouth over tex1 at (18, 76), 48 x 32,
17 sprites, default slot 30 (closed); 33 talks, 64 and 161 talk in other
shapes, 66-163 shapes.

**Motion attributes** (an mset entry's inner BAR entry of type 0x10,
`YS::ANMATR`): u8 range count, u8 trigger count, u16 the triggers' offset;
ranges u16 start, u16 end, u8 id, u8 parameter count, u16 parameters; triggers
u16 frame, u8 id, u8 parameter count, parameters. Frames are the motion's ticks
(R000 is 300 ticks long and its ranges end at 300). `CheckRangeCallback`: range
23 holds a face slot every frame it covers (`OBJ::texanm_set` ->
`ModelObj::setTexanm` -> `TEXAnmWork::set`). `ExecTriggerCallback`: trigger 4
sets the default script (`texanm_start` -> `setDefaultSequence`), 5 puts the
file's back (`texanm_stop` -> `resetDefaultSequence`). Field talk
(`SYSRC::TalkThread`) starts slot 33 for the talk and stops it after.

**In the port** (`runtime/mods/face.cpp`, `models.cpp` `play_face`): the face
plays once a game frame (one for all actors of a model; VBlanks taken as KH2's
60 Hz frames) and the changed rectangle is patched into a changing copy of the
texture. A block holds, first that applies: `face talk <slot>` while the
dialogue's text types (a letter within 6 VBlanks) and its portrait is one of
`face speaker <portrait>...`; `face ds <pattern animation> <slot>` while the
game plays that texture pattern animation (its render object's anmMat; the
cutscene Roxas closes his eyes with `e_ro_eye02`), unless the slot it would
cover is one of `face closed <slot>...` (eyes already closed); `face portrait
<portrait> [last|<slot>...] [talk <slot>]` for the speaker's last portrait
(`last`: each block's last script; `talk`: the talk slot while it shows, in
place of `face talk`), held across boxes and other speakers until his next
portrait, dropped when the model goes undrawn for 5 seconds (another scene);
the playing KH2 motion's ranges (`face ranges off` leaves them and the
triggers out). Lines in `<model>.motions.txt`. The portrait comes from
`Ov002_SceneApplyPanelRequest` (pReq[5]) and ends with
`Ov002_SceneClosePanelStep`; letters from the text crawl (`ui/text.c`).
Unverified: that Roxas's portraits are 30 and 131-141, and that cutscene
dialogue goes through the same panel.

## Secondary motion

A KH2 model's coat, cords and hair can be simulated over its motions (or the
game's animation) from `<model>.secondary.txt` beside it: `SECONDARY_MOTION.md`,
with N_EX830's setup.

## HD pictures

A folder beside the model with its name (`roxas_b_h.mdlx`, `roxas_b_h/`) replaces its textures, as the
remaster's `remastered/obj/<name>.mdlx/` holds them: `-N.png` or `-N.dds` (the PNG first; `image.h`,
`dds.h`: BC3 or plain 32-bit). The numbering is the rooms' (`TextureContainer`, `kh2_map.h`): each GS
info in turn, each followed by one sprite sheet per TEXA drawn over it. N_EX830 (verified by the sizes
against its GS infos and TEXAs, and by each sheet's alpha equal to the model's sprites'):

| File | What | Size | The model's |
|---|---|---|---|
| `-0` | tex0 | 1024 x 1024 | 256 x 256 |
| `-1` | tex1 (the face) | 1024 x 1024 | 256 x 256 |
| `-2` | the eyes over tex1, 18 sprites stacked upright | 384 x 3456 | 96 x 48 each |
| `-3` | the mouth over tex1, 17 sprites | 192 x 2176 | 48 x 32 each |

A picture may be any size: it covers the texture's region (`region_uv`), the model's texture
coordinates and its blocks' places go by the scale. A sprite sheet's rows must divide by its sprites;
a sprite not at the block's size is resized (nearest), so a sheet still fits a picture of another
scale, and the model's own sprites are used, resized, where there is no sheet. A file missing leaves
the model's own; one unreadable, or a sheet that does not divide, is reported in the log, as is what
was used. Each model has its own folder: only the model beside it changes.

A texture under a face block changes as it plays, so its whole picture is sent to the card at each
change of sprite, and it has no smaller copies (`gpu_hd.c` `has_smaller`: not for pictures that
change). At 1024 x 1024 and drawn small it may flicker as a still picture without them would.

## In the HD pack

`tools/hd/kh2_models.py` puts the models in `mods/HD PACK/graphics/`, run by the installer with the
other HD tools. The models, motions and HD pictures are the player's: copied as they are from
1.5+2.5's `kh2_first` and `kh2_fifth` (`obj/<name>`, its pictures among the entry's remastered assets),
as `installer/hd_inputs.txt` lists them. The settings are the port's own, in the repository under
`tools/hd/kh2_models/`, a tree laid out as the mod's, with `kh2_secondary_presets.txt` once at its top and
copied beside each model: **change them there**, as a rebuild overwrites the mod's copies (to try a
change, edit the mod's copy in game, then bring it back).

| Days' model | Place under `graphics/` | KH2's | Also read from the collection |
|---|---|---|---|
| `roxas_b_h` | `ba/ch/ro/def_hb.p.z/mdl` | N_EX830 | P_EX110 (mdlx, mset), GAMEOVER.mset, W_EX010.mdlx, W_EX010_RX.mset |
| `roxas_ho_h` | `ba/ch/ro/def_hho.p.z/mdl` | B_EX390 | the same |
| `roxas_bf_h` | `ba/ch/r2/def_h.p.z/mdl` | B_EX390 | B_EX390.mset, P_EX110 (mdlx, mset), GAMEOVER.mset |
| `e_rox_hb` | `mi/ch/0C.z/mdl` | N_EX830 | P_EX110 (mdlx, mset) |
| `ro_w00600` | `ba/ch/ro/w_.p2#0/mdl` | W_EX010 | |

A new file a setting names is added to `SOURCES` and `MODELS` in the tool
(`installer/tests/kh2_models_test.py` fails until it is), and to `hd_inputs.txt` by tracing.

## Not yet

- 4-bit textures (PSMT4) and other formats: reported and drawn white.
- Region clamp is drawn as repeat (the renderer repeats mods' textures).
- Normals, UV scroll and the chained shadow model are not read; of entry 0x17
  only the feet (types 10 and 11).
