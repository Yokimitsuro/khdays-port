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
rotate_motions 0 0 180 117.5  # every motion turned (degrees about x, y, z) about the point that high (cm)
b_ro_A000 0         # <the game's animation> <mset slot>
b_ro_E005 13        # a section: the jump from frame 0 ...
b_ro_E005@12 14 blend 3   # ... the fall from BCA frame 12, cross-faded over 3 frames
```

`rotate_motions <x> <y> <z> [<height>]` turns every KH2 motion's pose (the
model's root space, KH2 centimetres, as `turn_of`: about x, then y, then z) about
the point `height` cm over the root, before anything else reads it (`ds_pose`'s
slot poses, `carry`, the blend); the root motion taken out is turned with it
(R T⁻¹ R⁻¹) and IKAdjust's world takes the turn as its first step, so the feet still
find the game's floor (`models.cpp` `pose_on_model`, `taken_of`, `play_track`).
Xigbar's (B_EX140) stand him on the ceiling, as KH2's fight has him: `0 0 180
117.5` maps (x, y, z) to (-x, 235 - y, z), his ankles from 220.2 cm to their rest
14.8. Verified offline on his slots 0, 1, 10 and 11 (the root motion is a
translation in all four); unchecked in game.

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
animation plays; the first section entered straight from it fades the game's pose
out over its `blend` (Larxene's rise is the game's, then her fall at
`b_la_A003@12`: with no KH2 pose to fade from, it cut).

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
idle. Every replacement's dodge roll (`Ca002`, the full turn of its hips in 18 frames)
has none: the game blends out of it itself, and the extra fade caused problems in game (user). When the game goes on to one of its `idle` animations (none playing the draw
before) from the animation it played most, and that has a fade, the pose last shown
fades into the game's over those frames, the game's own fade not counted; a KH2
motion or the game's own pose alike. Anything else the game plays (an attack, a
move, the animation played anew) ends it at once, and a `finish` tail fades itself.
Its clock is the game's animation frames (`kh2_moves.h`, `play_kh2`). The pose is held
relative to the model's root, where the game puts it now: held in the view's space,
it stayed where he was while he moved on (an attack's lunge) and he slid forward and
back. Every mix of poses on the model (the game's fades, a `finish` tail, these)
mixes each joint's frames as KH2 does (`attached_frame.h` `blend_frames`, PS3
`kn::blend` 0xF659C, called from `kn::Motion::calc`): x and y axes each mixed and made
unit, z = x cross y, y = z cross x. A plain mix of the matrices shrank a joint between
turns far apart (his head squashed in the game's fade into the death, `b_ro_e003`).
KH2's weight is linear, time over blend frames (`YS::MOTION::calc` 0x45C068); the
weights here stay the game's and `blend` lines'. Not ported: `kn::no_envelope_blend`
(0xF66A8), the same mix starting from the axes that agree most, for helper bones only.
The motions' poses (and the game's own, at its share) are mixed joint by joint on the
parent's mixed frame, each pose's joint taken from its parent in that pose (the root in
the model's space), a joint skipping helper bones up to its envelope parent as
`kn::Model::getParentEnvelope` (0xF11DC; bone +0xC bit 0 marks a helper): mixed in the model's space, the keyblade arm crossing from the
battle idle into the weapon walk lost its length and twisted, each hand's place the mean
of two, off the forearm's mixed turn. A weapon joint that `place ... ride`s a hand mixes on
that hand's mixed frame instead (`Joint::mix_parent`, mixed in `Model::mix_order`, each joint
after the one it mixes on): B_EX390's keyblades (95, 106) hang from the root, so crossing
from his KH2 idle or walk into the game's run moved them in a line while the arms swung,
off his hands and back. One pose at its whole weight comes out the same either way, so
a throw still leaves the hand. The idle fade's held pose still mixes in the model's space.

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

`air_floor on` keys the `air` lines on the game's floor contact instead, for the boss Riku, who runs none of the
player's actor states (ov260; his damage is `Ov260_SwipeEntry`, poses 4 and 5). His render object's user pointer
(NNSG3dRenderObj +0x2c, set by `Ov260_ArmModelCallback`) is his ov107 object, checked by its body linking back to the
render object (+0x384, its +0x88, +0x20); its +0x17a bit 0 is set every tick its floor ray hits
(`Ov107_AiState_IntegrateVelocity`, cleared first by `Ov107_RollTransformHistory`). Off the floor for two draws he is
in the air; back on it for two while the damage still plays, the `land` slot plays from its start on the game's
frames until the game moves on, at whatever frame (no `landing` animation, no `finish`). Unchecked in game.

**`cut_blend <frames>`.** Where the game cuts from one animation into another (the new one at its whole weight on
its first draw, or set back from partway, not a loop wrapping round), the pose last shown is held relative to the
root and fades into the new over that many of the game's frames (vblanks since the last draw, two a frame; standing
while paused), the weapon's motion pose with it. Not where the new animation carries on the KH2 slot just shown (a
`start_loop` run after its start), nor where an idle fade starts at the same draw. The boss Riku cuts everywhere
(`Ov260_BindClip` drops the clip and binds the next at frame 0); only the drawn pose changes, not the game's
animations, frames or timing. `cut_blend_except <animation>...` leaves cuts into those animations cuts (the game
set one back included, and their `air` and `land` slots); a cut out of them still fades: the boss Riku's damage cuts
in, and fades out of the damage, its landing or the recovery after them into whatever his AI plays next (user).

**`after <slot>...`** on a section (or an `air` line's `land`): once its motion ends while the game still plays the
animation, those slots play in turn from their starts, cut together, the last one looping. Unlike `finish`, which
waits for the game's idle, this plays inside the game's own animation and ends the moment the game plays anything
else. With it, the game's animation wrapping round from its end goes on (the last slot loops); set back from partway it
starts over. The boss Riku's damage (`after 24 0`): P_EH000's damage motion (slot 42) is 48 ticks, Days' 60 (30
frames), so KH2's readying (24) starts at frame 24 and the idle follows while his damage plays on (offline trace).

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
to 110 snapped. `start_loop always` starts at the loop point even then: Saix's rise
(`b_sa_A003`) follows his 3-frame take-off (`A002`) on the same slot (03A), and from
the take-off's time it played the jump's start instead of its loop.
`offset <x> <y> <z> [to <x> <y> <z>]` moves the whole pose,
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

`lift <cm>` holds the section's lowest foot `cap · tanh(h / cap)` over where its
feet stand at the motion's first tick, `h` the height of its root motion then: a
motion that flies floats about that many centimetres up while its root is up and
lands as its root comes down (`models.cpp` `lift_by`, a vertical offset each draw,
kept through a fade as `offset` is). The feet are the ends of bones.txt's `ik ...
exact` legs, else the skeleton's own (its collision list's). The boss Saix's
transformation (`016_004`, B_EX160's 81A, slot 232) flies him up: its root motion
climbs to 344 cm by tick 160 and is back at 0 by 180, and with it taken out his feet
still rose 11-58 cm (95 at tick 170) and came back to the ground mid-flight (ticks
140-150: the root still climbing while the bones come down), so the feet's own rise
could not tell the flight. Measured offline (`--days`, `lift 8`, `follow` onto the
game's 162 frames): his feet from 16 to 23 cm by frame 8, there until frame 137, on
the ground (15) from 140 to its end.

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
node: the travel is drawn once, by the game. `on` keeps it in the bones. Not every motion agrees: B_EX170_LAST.mset's 01M (slot 152) moves its bones about 0.67 times as far as its root motion (158 of 238 cm at tick 40, 688 of 1026 at 60); why is not known (a scale of that model's, perhaps). A line's `offset` puts such a pose back. The
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
(`own_slot`, `skeleton_of`, `pose_on_model` in `model_kh2_motions.cpp`; a track's player is
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

A weapon with no bones of its own, skinned to the hand with it, cannot be hidden by
bone: `hide_mesh <index>...` drops whole meshes of the file, numbered from 0 in its
own order (B_EX400's knives are meshes 3, 5 and 4, 7, each on one hand alone, its
texture 3). Their vertices stay, as `hide` leaves them; what fits round the hand
(a `cuff`) takes only the vertices a drawn triangle uses.

A model that keeps two looks in one file switches between them by the game's
animation: `limit_animations <name|prefix*>...` names the animations of the second
look (a name, or a prefix ending in `*`), `limit_show_mesh <index>...` draws those
meshes only while they hold at least half of the game's pose, `limit_hide_mesh
<index>...` leaves those out then (B_EX160's berserk face and its eye piece, meshes
7 and 5, against the normal 4 and 6, in Saix's `b_sa_L*`). A first try read the
actor's limit block instead (its +0x22f8 in state 9, or 6 with flag 0x200, the test
`Ov033_UpdateGuardedSlots` keeps his berserk effect by): the face did not swap when
his limit started (in game), so it was taken out.

`limit_shapes <name|prefix*>...` counts the game's own shapes instead: the second look
shows while the game draws one of them (in that draw, `khdays_models_shape`). A model
that switches looks by its materials' alpha fits it: the DS sends no shape whose
material is fully transparent (`NNSi_G3dFuncSbc_MAT` sets the render state's flag 2,
`NNSi_G3dFuncSbc_SHP` draws only with `(flags & 0x303) == 1`). The boss Saix's body
(`Ms/53.p` model `016`) keeps his berserk head as the shapes `head_02_*`, their
materials `02_*` clear but in the material animation `016_B` (docs/RUNTIME_MAP.md,
section 22), so `limit_shapes head_02*` swaps B_EX160's faces with them.

A model the game hides by scaling it to nothing (its first node's axes all 0: the
boss Saix's claymore not shown, `Ov259_SwapShells`) is not drawn at all.

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
- bones.txt pairs `kh2_95 ro_h_L` and `kh2_106 ro_h_R` with `place ... ride`: in the
  game's own animations the bones take the game's weapon joints' turn, and their
  place moved by how far each KH2 hand is from the game's (on the nodes alone, his
  hands left the keyblades in a run). A placed joint holding
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
weapon la_h_L hand 127 # <the game's joint> hand <KH2 bone>: no bone of its own to hold it
```

`hand`: the game's frame for what it attaches to that joint carried by the KH2
hand as the game's hand node carries it (the bone and one paired joint above it
must be paired): KH2 hand x (game hand)^-1 x the game's frame. Held, it stays in
the KH2 fist in KH2 motions and the game's own animations alike. Sent away from the
game's hand (a throw), from 0.6 to 1.5 forearms off, it is only moved by how far
the KH2 hand stands from the game's, keeping the game's turn, so a knife metres away
does not swing round as the hand turns. Days' Larxene throws hers in her own
animations (`la_h_L`, `la_h_R` hang from her root); B_EX400 has no weapon bones.

`weapon <node> <bone> [rotate <x> <y> <z>] thrown`: held on the bone while the game keeps the weapon in its
grip (the node at rest in its hand node's frame, the hand the paired joint the bone hangs from), and, the further
the game sends it from there (0.6 to 1.5 of the hand's reach to the paired joint above it), the more it goes where
the game puts it, moved by how far the KH2 hand stands from the game's and turned as the game turns it, as a
`place ... ride` weapon bone does (`roxas_bf_h`'s 95 and 106). For a model with no weapon bone of its own to place:
P_EX030's 101, which holds Goofy's shield, carries his fingers. Days' Goofy keeps `go_h_R` within 16 cm of its grip
in every animation but `b_go_B007` (the throw, out to 1.7 m) and `b_go_D003` (53 cm).

Some players hold their weapon on no joint at all: the game draws it as an object of
its own on the player's base, its own animation keeping a joint of it on the hand
(Zexion's book, `ba/ch/ze/w_.p2`: root `ze_w_R` at the base, `ze_w00` exactly on
Days' `Bip01_R_Hand` at rest, the covers `ze_w00_L`, `ze_w00_R` under it; ov045
looks up no weapon joint). `weapon_model` puts it on a KH2 bone in KH2 motions:

```text
weapon_model ba/ch/ze/w_ ze_w00 94 hand 254 rotate -90 180 0 offset -9.5 -0.1 5.0
# <the game's weapon file> <its node that carries its place> <KH2 bone> hand <KH2 bone> [rotate] [offset cm, its own axes]
```

A model may have several `weapon_model` lines, one a node (each its own hand), and
`weapon_model <file> <node> weapon [rotate] [offset]` loads the node on the KH2 weapon's `weapon_motions` bone as the
body last drew it (`HeldWeapon::blade`: the `weapon` line's frame times `from_game` times the bone's pose, where a
`held ... body` weapon's bone is drawn), times the line's turn and offset (about and along that bone's axes), its own
scale kept: what rides the KH2 weapon rather than the hand, through every turn of its motion. The game's weapon frame
(`HeldWeapon::frame`) would not do: under W_EH000's held pose its axes are not the keyblade's (first try, user: the
sword through his head in the run). The boss Riku's energy sword (Ms/54.p's `B13E011`, node `EF_B13E01`) lies on his
sword's grip `wA` within 1.5 cm and 2 degrees in all 30 of his animations, its blade along +z; on W_EH000's bone 2 the
keyblade's blade runs along -x (its keychain, bones 3-11, along +x), so `rotate -90 0 90` (the turn about the blade
assumed). Named by its child, not `Dummy03`, which another effect of the file (`B13E002_03`) also has.

The KH2 bone may be left out (`weapon_model <file> <node> hand <bone>`): the weapon is
then carried by its hand alone, in KH2 motions too (below). Xaldin's two lances are
the nodes `xa_w_L` and `xa_w_R01` of one model (`ba/ch/xa/w_.p2`'s `a` models), each
on its own hand; KH2's six lances float behind him and none is held.

The node may be several names separated by commas, any of them matching: Demyx's
sitars (`ba/ch/de/w_.p2`) name it `de_w00`, or after the model on de_w01200,
de_w02400, de_w07300 and de_w07320. The `hand` need not be a hand: any KH2 bone the
weapon is measured on (Demyx's left hand, which carries KH2's sitar, bone 264 from the
root, most steadily).

When the `hand` is paired with a game node, the game's own animations move the weapon
too: its place is carried by the drawn KH2 hand as the game's hand node carries it
(`hand × game hand⁻¹ × weapon`), since the KH2 arms reach elsewhere than Days' (Demyx's
shoulders stand 8 cm higher, his arms 6% longer: the sitar sank below and behind his
hand, by a different amount in each animation). As `weapon ... hand`, from 0.6 to 1.5
reaches (the hand to the paired joint above it) away from the game's hand only the
hands' offset moves it. That carried place is also the game's grip a fade starts from.

While a model of that file with a node of that name is drawn (its body drawn within
the last second; the file's pages, effects and swing traces have none, read from the
model's node dictionary at `NNSG3dResMdl` +0x40), that node's matrix is loaded where
the game has just placed it (`NNSi_G3dFuncSbc_NODEDESC`, `khdays_models_node_placed`:
after the joint is sent, before the matrix is stored for what hangs from it, in the
space `khdays_models_node` reads). Both grips are read on the body's `hand` as drawn:
the game's, from that matrix, and KH2's, the bone on the hand as the KH2 motions alone
pose them (`play_kh2`, each pose by its own share), times the line's turn and offset.
The node goes on the hand by the two mixed by the KH2 motions' share, its own scale
kept, so a fade turns it once from one grip to the other. Tried first and dropped:
the bone's frame from the mixed pose (an unpaired bone swings between the motion's
place and where the game's pose leaves it: Zexion's book's rest, 2.3 m up), and
rewriting the node's result in the joint send (its space taken from the draw's start
matrix, it landed 1.5 m off, and one cover, which restores the node's stored matrix,
stayed with the game's).

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
as the game's nodes place it. `held <joint> rest` needs no motions, for a weapon
made at rest where the bone holding it is at rest (measured: W_EX010's, _10's and
_20's bone 2 and B_EX390's 95 and 106 have one head and axes): it moves as that bone
has moved from rest, under the game's animations too, as `place` keeps those bones
on the game's weapon joints. The log says so if the two rests differ. `held <joint> rest
<x> <y> <z>` gives the turn into the game's weapon frame itself, for a cutscene's own
object, which no `weapon` line holds and which need not be in a hand: it moves as
the game's model moves from rest. Days' own keyblade, unreplaced, gets the turns
through the frame it is drawn at; dumped to glTF and put back as the mod its
keychain (one bone, `ro_w_01` at the handle's end, under `ro_w_00`) can be
simulated as well.

`held <joint> turn <x> <y> <z>` is `held <joint>` for a model made in other axes than the weapon whose motions
pose it: its joint's rest frame is turned (degrees about its own x, then y, then z, as a `weapon` line's) into the
weapon bone's before the pose places it (`Model::held_onto`, between the pose and the joint's inverse bind).
Goofy's F_TT150 (`go_w01600`) stands at the origin, its face turned 45 degrees to +x+z and its wide end up, where
W_EX030, whose mset poses it, lies flat at his hand, face down; `turn 135 0 90` puts F_TT150's face along W_EX030's
bone 2's x (its centre 10.0 cm out, W_EX030's 10.9) and its wide end along that bone's -y, up in his idle.

`held <joint> body` is `held <joint>` for a weapon the game draws somewhere other than the frame the body's `weapon`
line gives it: the joints go on that frame (`HeldWeapon::frame`, the last `hold` of the body's draw) instead of the
model's own root. The boss Riku's sword object (Ms/54.p's `B13_02w`, `Ov260_Construct`'s +0x390 part) is drawn at his
base, its own animation keeping its grip node `wA` on his right hand (offline: 7.4-11.1 cm from `Bip01_R_Hand` and at
most 6 degrees off one grip in all 30 of his animations; nothing reads `sa_h_R`, the node under his hand that his
`weapon` line names). So his KH2 hand holds his keyblade (W_EX010_20, posed by W_EH000.mset) as the player's does, its motions and
keychain included.

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
`bones.txt` take only `rotate`, on top of the turn their node gives them (a slot's
pose would fight the node): riku_h's `ds_pose rotate 0 0 -30 2 for b_ri_Ca000
b_ri_Ca001` turns his pelvis back under him in the game's glides, where the game leans
its upper spine back 33 degrees further than in its idle and the pelvis tipped forward
30-36 degrees on the lumbar bone (the run's -26), arching his back and standing the
coat out behind the seat; the hips' children (the thighs) go where it carries them.
The same turn, each animation's own, brings the pelvis's tilt on the lower spine (the bone
paired with `Bip01_Spine`, against the model's KH2 idle) to about +3 degrees in the game's
run and glides wherever it was over +10, for every coat model but xaldin_h (his `spine ...
hips` places the pelvis after it), demyx_h (set up as before all
of this: user) and larxene_h's hover (she looked worse turned: user): up to 63 degrees in roxas_bf_h's forward glide (turned 66). It costs feet
through the coats in some glides, where the coat ignores the feet or nothing covers the toes
(SECONDARY_MOTION.md).

**Hands that respond.** `hand` lines in `motions.txt` pose the fingers each draw
under the game's own animations (a KH2 motion's fingers stay its own), move the
hand out of the body and the room, and rest it on what it presses. They win over
`ds_pose` for the bones they name (`model_hands.cpp` `pose_hands`, the maths in
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
not simulated (`model_hands.cpp` `pose_cuffs`, the maths in `cuff_fit.h`):

```text
cuff kh2_116 kh2_133 kh2_117 kh2_126 kh2_123 kh2_120   # root, hand, the four flaps in order round the ring
cuff_fit 0.3 20 0.72 0.35 0.62 0.74 2                  # for all a model's cuffs
```

A cuff fits round the hand's drawn vertices: those of a `hide`n weapon baked into the hand stay in the mesh, and
N_CM000_BTL's scythe (1101 of them, 2 m long, under the right hand) threw its right cuff open in his own motions.
The fit opens each flap from its rest on the root; a KH2 motion that flares the flaps itself (his own, the right up
to 58 degrees; B_EX390's, up to 75) has them opened twice. `cuff_motion_flaps off` puts them back at rest first, so
they fit as in the game's own animations (marluxia_h; roxas_bf_h keeps them). The motion's scale stays: each bone
goes back on its parent as at rest, each axis scaled as the motion scales it against its parent (N_CM040_BTL's idle
shortens Vexen's 27 cm flaps to 0.41 of their length, its walk to 0.7: reset whole, they hung at full length). A
parent the motion flattens to nothing on an axis gives no ratio there (under 0.01: the bone keeps the parent's), and the
ratio is at most 2: B_EX150's walk scales Luxord's left flaps' y and z to 0.00, and flat against flat mid-blend made
the ratio about 10^4, a flap a kilometre long (the replay, a fade from his idle into slot 1).

A flap whose bones all stand at its hinge (B_EX400's: one bone each, its head the
root's, no tip bone) ends its last bone's own length along that bone's x (KH2's
`kn::ModelHRC` scale w, bone +0x1C: 7.3 cm there; B_EX390's 5.94 matches where its
tip bone stands, 5.91), with no tip of its own to bend.

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
1.5+2.5's `kh2_first`, `kh2_fifth` and `kh2_sixth` (`obj/<name>`, its pictures among the entry's remastered assets),
as `installer/hd_inputs.txt` lists them. The settings are the port's own, in the repository under
`tools/hd/kh2_models/`, a tree laid out as the mod's, with `kh2_secondary_presets.txt` once at its top and
copied beside each model: **change them there**, as a rebuild overwrites the mod's copies (to try a
change, edit the mod's copy in game, then bring it back).

Each model's `bones.txt` says `switch kh2_models`: the player turns them all off and on in Settings,
Enhancements, **KH2 models** (`kh2_models` in the settings file, on by default), at once, the game's own
models drawn while it is off. A new model's settings carry the same line.

| Days' model | Place under `graphics/` | KH2's | Also read from the collection |
|---|---|---|---|
| `roxas_b_h` | `ba/ch/ro/def_hb.p.z/mdl` | N_EX830 | P_EX110 (mdlx, mset), GAMEOVER.mset, W_EX010.mdlx, W_EX010_RX.mset |
| `roxas_ho_h` | `ba/ch/ro/def_hho.p.z/mdl` | B_EX390 | the same |
| `roxas_bf_h` | `ba/ch/r2/def_h.p.z/mdl` | B_EX390 | B_EX390.mset, P_EX110 (mdlx, mset), GAMEOVER.mset |
| `e_rox_hb` | `mi/ch/0C.z/mdl` | N_EX830 | P_EX110 (mdlx, mset) |
| `e_roh_hb` | `mi/ch/47.z/mdl` | B_EX390 | as `roxas_ho_h`, whose cloth it takes, at `scale 0.93` (he never sits or kneels), with `e_rox_hb`'s dynamic hands |
| `roxas_b` | `ba/ch/ro/def.p.z/mdl` | N_EX830 | as `roxas_b_h`, whose settings it takes |
| `roxas_bf` | `ba/ch/r2/def.p.z/mdl` | B_EX390 | as `roxas_bf_h`, whose settings it takes |
| `axel_h` | `ba/ch/ax/def_h.p.z/mdl` | B_EX110 | B_EX110.mset; its weapon bones 147 and 234 placed on `ax_h_L` and `ax_h_R` (`place ... ride`: they stand from his hands as the nodes from the game's, and Days' animations move those nodes up to 2.8 m from them, measured; the nodes hang from the root, not the hands), its baked weapons hidden; the hair on its own `scalp` collider; no cuffs yet |
| `axel` | `ba/ch/ax/def.p.z/mdl` | B_EX110 | as `axel_h`, whose settings it takes |
| `marluxia_h` | `ba/ch/ma/def_h.p.z/mdl` | N_CM000_BTL | N_CM000_BTL.mset: the idle, the port's walk (1; the run stays the game's), jump (3, the take-off and the rise), fall (4, from the game's 12th frame in the air) and landing (5) as `roxas_b_h`'s jump, the damage on `axel_h`'s slots (the two msets' slots 0-21 hold the same names); every line blended over 8 frames, and each of the game's own animations given an 8-frame fade into the idle; `cuff_motion_flaps off`; his legs and coat strips numbered as B_EX110's, his arms and cuffs as B_EX390's (left -17, right +12), so the cuffs are `roxas_bf_h`'s; the scythe's bone 258 (under 257, at the right hand's end; his motions turn it 6.6 degrees off 257) placed on `ma_h_R` (`place ... ride`; Days' node hangs from the hand), the baked scythe hidden; `weapon ma_h_R 258 rotate -90 0 -90` lays Days' scythe along the baked one: at rest the baked shaft runs along 258's +x, the blade across it toward -z (measured on both meshes) |
| `marluxia` | `ba/ch/ma/def.p.z/mdl` | N_CM000_BTL | as `marluxia_h`, whose settings it takes |
| `larxene_h` | `ba/ch/la/def_h.p.z/mdl` | B_EX400 | B_EX400.mset: the idle, fall (4, from the game's 12th frame in the air, from its loop: its first 16 ticks come out of a KH2 jump she has none of), landing (5) and damage on `marluxia_h`'s slots, no walk or jump (slot 3 is empty, her 1-2 not a walk); every line blended over 8 frames, the game's own animations fading into the idle. No finger bones (fists) and no weapon bones: her knives, meshes 3, 4, 5, 7, hidden (`hide_mesh`), Days' carried by her hands (`weapon ... hand`). Cuffs as `roxas_bf_h`'s on one-bone flaps, `cuff_motion_flaps off`. Her antennae's strands (149, 156) on Days' `la_hair_L`, `la_hair_R`. Heels: the feet's offset 7.7 cm |
| `larxene` | `ba/ch/la/def.p.z/mdl` | B_EX400 | as `larxene_h`, whose settings it takes |
| `zexion_h` | `ba/ch/ze/def_h.p.z/mdl` | B_EX370 | B_EX370.mset on `larxene_h`'s slots, with the port's walk on 1 (01A; the run stays the game's) and the fall (4, a loop from its first tick, so no `start_loop`); every line blended over 8 frames, the game's own animations fading into the idle. Legs and coat as N_CM000_BTL's (the front strip from 83: 82 has no vertices), spine and collar +21, arms, cuffs and fingers B_EX390's +4 left, +33 right, so `roxas_bf_h`'s cuffs, `cuff_motion_flaps off`. His book (bones 93-113, meshes 5 and 6, over his head at rest) hidden; Days' book, drawn as an object of its own, put on KH2's (bone 94) in KH2 motions with `weapon_model`. Feet's offset 9.8 cm |
| `zexion` | `ba/ch/ze/def.p.z/mdl` | B_EX370 | as `zexion_h`, whose settings it takes |
| `vexen_h` | `ba/ch/ve/def_h.p.z/mdl` | N_CM040 | with N_CM040_BTL.mset (its skeleton N_CM040's, bone for bone; N_CM040.mset does not exist) on `zexion_h`'s slots but the walk and run, both 02A (slot 2; his slot 1 is empty), and the fall from its loop (`start_loop`, 18 of 38 ticks); every line blended over 8 frames. Bones 0-154 N_CM000_BTL's, then +2 to the head, +9 from the chest piece; cuffs `marluxia_h`'s. His shield bone 156 (under 155 at the left hand's end) placed on `ve_w_tg` (`place ... ride`; the node hangs from the root, on Days' left hand at rest), baked shield hidden; `weapon ve_w_tg 156` with no turn: Days' ve_w00600 lies in its axes as the baked shield in 156's. Hair: seven strands, each a `tuft` chain, on a scalp inside every root. Feet's offset 2.1 cm |
| `vexen` | `ba/ch/ve/def.p.z/mdl` | N_CM040 | as `vexen_h`, whose settings it takes |
| `lexaeus_h` | `ba/ch/le/def_h.p.z/mdl` | N_CM020_BTL | N_CM020_BTL.mset on `vexen_h`'s slots but no walk or run (1-3 empty); the fall from its loop (10 of 38 ticks); every line blended over 8 frames. N_CM000_BTL's skeleton without its 31 hair bones (0-167 the same, -31 from the chest piece on), so `marluxia_h`'s cuffs. His weapon bone 227 (under 226 at the right hand's end) placed on `le_h_R` (`place ... ride`; the node hangs from Days' right hand), baked weapon hidden; `weapon le_h_R 227 rotate 0 180 -90`: of the 24 axis-aligned turns it lays Days' le_w00600 nearest the baked one (5.1 cm mean, against 9.9 turned about its length the other way). Feet's offset 2.3 cm |
| `lexaeus` | `ba/ch/le/def.p.z/mdl` | N_CM020_BTL | as `lexaeus_h`, whose settings it takes |
| `demyx_h` | `ba/ch/de/def_h.p.z/mdl` | B_EX120_HB | B_EX120_HB.mset (B_EX120.mset holds four motions; the mdlx is B_EX120's byte for byte): `zexion_h`'s slots, the walk on 1 (01A, also slot 2: no run of its own), no jump (52A in 3), the fall (53A) from its loop (12 of 36 ticks), the landing 54A; every line blended over 8 frames. Legs, coat and drawstrings numbered as B_EX110's, one chest bone fewer from the spine on (arms Axel's -1 to the wrist). His cuffs (114-128) hang from the wrist and end short of it: `twist` only, no `cuff`. His sitar (bone 264, meshes 3 and 6) hidden; Days' sitar, drawn as an object of its own, put on KH2's with `weapon_model` measured on his left hand (`rotate 0 -90 -90`, 14.0 cm mean against 18.0 mirrored). Feet's offset 4.7 cm |
| `demyx` | `ba/ch/de/def.p.z/mdl` | B_EX120_HB | as `demyx_h`, whose settings it takes |
| `xaldin_h` | `ba/ch/xa/def_h.p.z/mdl` | B_EX130 | B_EX130.mset: he flies in KH2 (no walk, run, jump or landing), so one line, `b_xa_Ca000` on slot 0 (00A); the game's own animations fade into its idle over 8 frames. Legs, coat and drawstrings numbered as B_EX110's; arms, cuffs and hands N_CM000_BTL's +2 (right +141), so `marluxia_h`'s cuffs. His six lances (306-317, meshes 3-8, 10, 12-14, 16, 17) hidden; Days' two (`xa_w_L`, `xa_w_R01`) carried by his hands with `weapon_model ... hand` and no KH2 bone. His skeleton reshaped to the game's, at his KH2 size (KH2's Xaldin is built otherwise: thigh 47.7 cm to 53.4, upper arm 29.6 to 36.5, hips to neck 76.1 to 66.9, shoulders 6 cm higher): `game_scale 1.041` (the game's neck at his), `spine kh2_96 kh2_166 hips kh2_3 onto`, knees, ankles and shoulders `place`d; his arms keep their length (`ik`), his feet their rest turn (`align ... rest`). At his own lengths the game's wide idle locked his knees straight. The back hair piece (171-177) not simulated (simulated, it swung and pulled its braids into his head); its three braids and the twelve from the head drawstrings simulated against his head or scalp, two capsules across his back, his shoulders, neck and hood (the hood's own cloth against the back capsules, not the chest's, which its root started inside: it jittered). KH2's rest hangs them through his body: `ds_pose rotate` turns each out of it in the game's own animations (the least turn leaving it 1.2 cm clear at rest, found against his mesh), the pose they are simulated from |
| `xaldin` | `ba/ch/xa/def.p.z/mdl` | B_EX130 | as `xaldin_h`, whose settings it takes |
| `xigbar_h` | `ba/ch/xi/def_h.p.z/mdl` | B_EX140 | B_EX140.mset: idle 0 (00A), walk 1 (01A; no run of its own), the damage from the front 10 (20A: the game's A005, A006), from behind 11 (22A: A007, A008); no jump, fall or landing. His motions stand him upside down on the ceiling: `rotate_motions 0 0 180 117.5`. Skeleton and body mesh `demyx_h`'s (B_EX120_HB) to the collar, so his pairs, feet (ankle 14.8 over its sole, the game's 10.2) and colliders; hand 118, fingers 120-141 (Demyx's -15), right +73; neck 159, head 162. His arrowguns (231-240, meshes 9-24) hidden; Days' (`xig_h_L`, `xig_h_R`) carried by his hands with `weapon ... hand`. Cuffs left alone. Ponytail (166-171 under its tie, 165, which stays on the head) a drawstring simulated against his head, back, shoulders and collar, its reference turned 35 degrees up (`hang 0 0 -1`: KH2's rest runs it through the hood); his KH2 motions key it (`control own_hair`). Not against the hood: capsules on the simulated hood jerked it in the game's run (10.7 cm p99 frame to frame in the replay, 3.2 without), so it may pass through the hood |
| `xigbar` | `ba/ch/xi/def.p.z/mdl` | B_EX140 | as `xigbar_h`, whose settings it takes |
| `luxord_h` | `ba/ch/lu/def_h.p.z/mdl` | B_EX150 | B_EX150.mset, Marluxia's slots: idle 0, walk 1 (no run of its own), jump 3, fall 4, landing 5, damage 10-13, the aerial damage 16 and 18 landing on slot 5 (his 31A and 33A run 80 ticks with no loop). Skeleton and legs `demyx_h`'s (B_EX120_HB), so his pairs, feet (ankle 14.9 over its sole, the game's 11.6) and colliders; arm, cuff and hand `xaldin_h`'s numbering on the left (right +71), so its cuff lines (his cuff as wide as Marluxia's). No hair bones. His five giant cards (236-245, meshes 4-8) hidden; Days' card (`lu_w00a`, the `a` models of `w_.p2`; at rest on `Bip01_R_Hand`'s axes, 8.2 cm along the fingers) carried by his right hand in the game's own animations; in KH2 motions on his hand at its own grip (`weapon_model ... 204 hand 204 rotate -41 -33 13 offset 6.7 1.5 -4.5`: the least turn from Days' grip pointing its face, its -z, at his eyes in his KH2 idle; Days' grip faced it into his palm, and the game's own grip spun it in his idle-walk blends); the `b` models' fan left to the game |
| `luxord` | `ba/ch/lu/def.p.z/mdl` | B_EX150 | as `luxord_h`, whose settings it takes |
| `saix_h` | `ba/ch/sa/def_h.p.z/mdl` | B_EX160 | B_EX160.mset, Marluxia's slots: idle 0, walk 1 (no run of its own: the game's run stays; his berserk run is B_EX160_2's, unused), jump 3, fall 4, landing 5, damage 10-13, the aerial damage 16 and 18 landing on 17 and 19. Skeleton `luxord_h`'s to the collar, his right arm +213, so `luxord_h`'s cuffs renumbered; jaw 182. His claymore's bone 379 placed on Days' `sa_h_R` riding his right hand, Days' claymore drawn on it (`weapon sa_h_R 379 rotate 0 90 0`: Days' length along its z, KH2's along x, the grips together); KH2's two claymores (meshes 10-13, 16) hidden. Both faces in one file: the berserk face (7, on the jaw) and its yellow-eyed eye piece (5) drawn only in his limit, the normal ones (4, 6) the rest of the time (`limit_animations b_sa_L*`, `limit_show_mesh`, `limit_hide_mesh`); his rise (the game's `b_sa_A003` before its 12th frame) from 03A's loop (`start_loop always`: straight after his take-off on the same slot, `start_loop` alone carried the take-off's time on). Hair: 30 `kh2_hair` tufts, his long back strands among them; the two manes by his face held by the head. His KH2 motions hold 379 turned 179 degrees (about the blade) from where the game's animations hold `sa_h_R` in his hand, the same in every slot measured, so the claymore flipped between them: `weapon sa_h_R 379 rotate 138.4 -87.4 10.9` is the turn that holds it in KH2 motions as the game's do (the placed joint's correction takes the line's turn out again in the game's animations, so only KH2's change). His five long strands hang at rest straight down from the head, and the game's animations tip the head 17 degrees further forward on the chest than KH2's idle, so the hair they are guided to ran through his chest: `ds_pose 0 176-180 213-222 272-281` turns them as his idle does (the replay's crossings of the chest's mesh down by four fifths in the game's run) |
| `saix` | `ba/ch/sa/def.p.z/mdl` | B_EX160 | as `saix_h`, whose settings it takes |
| `xemnas_h` | `ba/ch/xe/def_h.p.z/mdl` | B_EX170 | B_EX170.mset: idle 0, walk 1 (no run of its own), no jump (slot 3 empty), the fall 4 from the game's 12th frame in the air, landing 5, damage 10-13, the aerial damage 16 and 18 landing on 17 and 19; B_EX170_LAST.mset after its 256 slots (`motions ... at 256 own`: B_EX170_LAST.mdlx's 320 bones are B_EX170's, bone for bone), its 00M for the game's `b_xe_Ca000`, its 01M (408) for `b_xe_Ca001` from its loop point, its end (`start_loop`: it does not loop, so the last frame holds), `offset 0 0 337.6`: its bones travel only two thirds of its root motion (688.4 of 1026.0 cm at tick 60), so taking the root motion out left him behind his place. Legs and coat as B_EX110's; arms, cuffs and hands `xaldin_h`'s +15 (right +138), so its cuffs; hood 116; the hair down his back (101-114) hangs from the collar. No weapon in the file; Days' (`xe_h_L`, `xe_h_R`, on the game's hands) carried by his hands with `weapon ... hand`. Hair: three `kh2_hair` groups, the top and fringe on the head's end, the side locks on the neck's end, the back hair's six strands on its base |
| `xemnas` | `ba/ch/xe/def.p.z/mdl` | B_EX170 | as `xemnas_h`, whose settings it takes |
| `riku_h` | `ba/ch/ri/def_h.p.z/mdl` | N_EX950 | P_EH000 (mdlx, mset), W_EH000 (mdlx, mset): the coat Riku with the blindfold on the playable Riku's motions, set up as `roxas_b_h` (P_EH000's slots are numbered as P_EX110's). N_EX950 is N_EX830's rig at his size (hips 117.4 cm against 106.2), numbered as it through the head, the right arm +11; so `roxas_b_h`'s pairs, cuffs, coat, drawstrings and hood, colliders sized by his. Its body, arms and hair share P_EH000's rest places bone for bone (`bone` lines, no `ik`). Idle 0, walk 4, run 8, jump 14, fall 18, landing 22 then 24, damage 42-54 then 24, the aerial damage 66 and 74 landing on 70 and 78. Every line, and the game's own animations into the idle, fade over 8 frames. The ledge as Roxas's: both hang by the left hand, KH2's 47.8 cm too far forward and 39.8 too low (`offset 3.1 39.8 -47.8`); the climb (34) `follow`ed onto the game's 15 frames, gliding to `-3.5 -12.5 7.7`. His sword (meshes 4, 5) hidden; the game's on bone 262 (`weapon ri_h_R 262 rotate 0 -90 90`: `ri_h_R`'s rest frame gives Roxas's turn), turning in his hand by W_EH000.mset. His back hair (172, 173-187 under it) hangs at rest through the hood, which lies flat behind his neck: P_EH000's motions key it where it belongs, and under the game's own animations `ds_pose rotate 0 0 30 172` lays it over the hood (1.9 cm off it at rest; no hair edge through the settled hood in the game's idle, run and hang offline). His pelvis 15 % and thighs 10 % thinner front to back (`mesh_scale`), their colliders given the radii `fit` finds on the thinner meshes (pelvis 14.9-16.5 cm, thighs 10.0-8.7). The pelvis alone hidden under the coat (`hide kh2_2 from 109 to 96 fade 4`: below the coat's waist ring, 110-116 cm, to the hip joints; 46 triangles dropped, 16 fading), the legs drawn. The blindfold's tails (210-211, 213) drawstrings, 2 cm thick. Feet's offset 6.9 cm |
| `riku` | `ba/ch/ri/def.p.z/mdl` | N_EX950 | as `riku_h`, whose settings it takes |
| `mickey` | `ba/ch/mi/def.p.z/mdl` | P_EX200 | P_EX200.mset (his own), W_EX200 (mdlx, mset): Days' only Mickey model (no `def_h`), the same size (head pivot 56.7 cm against 56.5, hips 40.2 against 36.9: no `scale`). Idle 0, walk 4, run 8, jump 12, fall 16 (from the game's 12th frame in the air), landing 20; damage from the front 40 (A005, A006), from behind 44 (A007, A008); every line and the game's own animations fade over 8 frames. Pairs: spine 78, 79, the chest's top 80 as the neck node (it carries the arms), the head's pivot 82; arms clavicle, shoulder, elbow, wrist (113, 117-119; right +44), the gloves riding the wrists; the tail's two nodes on 71 and 73 of 70-75. Feet's offset 2.1 cm. His weapon on 169 (`weapon mi_w_tg_R 169 rotate 90 -90 0`, from `mi_w_tg_R`'s rest frame), turning by W_EX200.mset (its slots P_EX200's). Coat (8 strips to the ankles, 24-69) and drawstrings (103-105, 110-112) as `axel_h`'s: guided by his own motions, simulated under the game's; KH2's own coat keys left his legs 25 cm through it (replay, authored), guided none (11.3 cm p99 frame to frame in his run, 9.4 in his idle) |
| `donald` | `ba/ch/do/def.p.z/mdl` | P_EX020 | P_EX020.mset (his own, numbered as P_EX110's), W_EX020 (mdlx, mset): Days' only Donald model (no `def_h`). KH2's is shorter (hat 98 cm against 105, head pivot 69.6 against 74.4, hips 32.5 against 28.7): no `scale`. Slots as `riku_h`'s (idle 0, walk 4, run 8, jump 14, fall 18, landing 22 then 24; damage 42-54 then 24, in the air 66/74 landing 70/78; ledge 30, climb 34), every line and the game's own animations fading over 8 frames. Ledge: the left hand moved onto the game's (`offset 9.9 11.8 -45.1`; KH2's hung 45 cm into the wall), the climb gliding to the game's end (`to -3.1 -13.0 -1.1`; 0.4-1.5 cm off it in the replay). Pairs: hips 2, spine 34 (KH2's 30-35 curve up the belly from the root), chest 38, neck 39 (the arms' parent), head pivot 79; legs 13/14/17 (right +9); arms 42, 46, 47, 50 (right +77). Feet's offset -1.0 cm (ankle 3.8 over the sole against 4.8). Weapon on 131, the right hand's end (`weapon do_h_R 131 rotate 180 0 0`): posed by W_EX020.mset it stays 5.2-5.4 cm from his closed fingers in every motion, any other bone 7-77 cm. No coat. Hood 71-72 (`kh2_hood`, on 70); as hair (`kh2_hair`): the tail 10-11 (on 9), the forehead feather 89 (on 88), the belt's flaps 154, 157; 8-10, 70-71 and 88-89 share points, so the controls start after them. Pelvis and head colliders given (fitted ones held the tail and hood inside); the flaps and feather collide with nothing (his feet came 5.5 cm through the flaps in KH2's run) |
| `goofy` | `ba/ch/go/def.p.z/mdl` | P_EX030 | P_EX030.mset (his own), W_EX030 (mdlx, mset): Days' only Goofy model (no `def_h`). KH2's is a fifth shorter (hat 182 cm against 218, head pivot 143.6 against 170.1, hips 77.5 against 90.0): no `scale`, the KH2 characters keeping their sizes to each other. Slots as `donald`'s, every line and the game's own animations fading over 8 frames. Ledge: the left hand moved onto the game's (`offset -5.3 37.2 -53.5`; KH2's hung 54 cm into the wall, 37 low), the climb gliding to the game's end (`to -12.4 25.3 0.3`; within 0.1 cm of the hand, 1.8 of the end, in the replay). Pairs: hips 2, waist 3, chest 9, its top 10 (the arms' parent) as the neck, head pivot 50; legs 130/131/136 (hanging from the root; right +13); arms 13, 17, 18, 26 (right +74). Feet's offset -3.7 cm (ankle 7.7 over the sole against 11.3). Weapon on 101, the right hand's end (`weapon go_h_R 101 rotate 180 0 0 thrown`: `go_h_R` hangs from the game's root, and his throws, `b_go_B007` and `b_go_D003`, send it off his grip). No coat or hood. Ears 62-64, 77-79 as the drawstrings (`kh2_string`, hung at load, colliding with head, torso and arms as `riku_h`'s blindfold; the arms knock them in KH2's run, 20 cm p99 frame to frame, as `riku_h`'s strings, 27); as hair: the hat 84-86 (on 83) and its top's tuft 59-60 (on 58), colliding with nothing |
| `ro_w00600` | `ba/ch/ro/w_.p2#0/mdl` | W_EX010 | |
| `ro_w13600` | `ba/ch/ro/w_.p2#21/mdl` | W_EX010_10 | `ro_w00600`'s settings |
| `ro_w15600` | `ba/ch/ro/w_.p2#22/mdl` | W_EX010_W0 | nothing on its keychain's bones: no `.secondary.txt` |
| `ro_w9999a` | `ba/ch/ro/w_.p2#26/mdl` | W_EX010_P0 | `ro_w00600`'s settings |
| `ro_w9999b` | `ba/ch/ro/w_.p2#27/mdl` | F_TT580 | three bones, made at the origin along +z: no `held` |
| `ro_w13600` | `ba/ch/r2/w_.p2#0/mdl` | W_EX010_10 | left hand, `held kh2_2 rest` |
| `ro_w14600` | `ba/ch/r2/w_.p2#80/mdl` | W_EX010_20 | right hand, `held kh2_2 rest` |
| `ri_w01600` | `ba/ch/ri/w_.p2#1/mdl` | W_EH000 | Riku's: `held kh2_2`, posed by his `weapon_motions` (W_EH000.mset); keychain 3-11 as `ro_w00600`'s |
| `ri_w14600` | `ba/ch/ri/w_.p2#2/mdl` | W_EX010_20 | `ri_w01600`'s settings: W_EH000.mset's pose too (its bone 2 rests where W_EH000's does) |
| `mi_w00600` | `ba/ch/mi/w_.p2#0/mdl` | W_EX200 | `ro_w00600`'s settings, posed by Mickey's W_EX200.mset |
| `mi_w01600` | `ba/ch/mi/w_.p2#1/mdl` | W_EX010_30 | the same |
| `do_w00600` | `ba/ch/do/w_.p2#0/mdl` | W_EX020 | `held kh2_3` (its 744 vertices on 3), posed by Donald's W_EX020.mset |
| `do_w01600` | `ba/ch/do/w_.p2#1/mdl` | F_TT160 | `held kh2_2`: made in W_EX020's frame (bones 0-2 the same axes) at another holder's hand, 108 cm up, so posed by W_EX020.mset; its roll in the hand W_EX020's posed bone 2's, not checked against KH2 |
| `do_w02600` | `ba/ch/do/w_.p2#2/mdl` | W_EX020_B0 | `do_w00600`'s settings (the same four bones and rest) |
| `go_w00600` | `ba/ch/go/w_.p2#0/mdl` | W_EX030 | `held kh2_3` (its 534 vertices on 3), posed by Goofy's W_EX030.mset |
| `go_w01600` | `ba/ch/go/w_.p2#1/mdl` | F_TT150 | `held kh2_2 turn 135 0 90`: made at the origin standing, its face turned 45 degrees, so turned onto W_EX030's bone 2 (above) |
| `go_w02600` | `ba/ch/go/w_.p2#2/mdl` | W_EX030_B0 | `go_w00600`'s settings (the same four bones and rest) |
| `e_ro_w` | `mi/ob/17.z/mdl` | W_EX010 | `held kh2_2 rest 180 90 0`, `ro_w00600`'s chain |
| `ro_w9999b` | `mi/ob/67.z/mdl` | F_TT580 | as in `w_.p2#27`: no `held`, no chain |
| `ro_w00600` | `mi/ob/B7.z/mdl` | W_EX010 | `held kh2_2 rest 180 90 0`, `ro_w00600`'s chain |
| `ro_w13600` | `mi/ob/B0.z/mdl` | W_EX010_10 | the same |
| `ro_w14600` | `mi/ob/B1.z/mdl` | W_EX010_20 | the same |
| `016` | `Ms/53.p#0/mdl` | B_EX160 | the boss Saix's body (his skeleton is `saix_h`'s node for node, its root `016`): `saix_h`'s bones (379 on `sa_h_R` riding the right hand, `weapon sa_h_R 379 rotate 0 90 0`) and cloth; his claymore meshes hidden; the berserk face by `limit_shapes head_02*`. B_EX160.mset with B_EX160_2.mset at 256: `016_000` 00A (0), `016_002` the berserk idle (256), `016_001` the berserk run (257, `follow`: its stride on the game's 18 frames), `016_004` 81A (232, `follow lift 8`); the rest Days' own, fading into the two idles. The game's claymore object (`Ov259_HelperUpdate`) reads `sa_h_R` back while held (`NNS_G3dGetResultMtx`, `Obj_RenderModel`), so it sits in the KH2 hand (offline: 10.7-11.1 cm from the hand bone in Days' animations) and goes where KH2 keys 379 in his motions: 81A sends it up to 157 cm off the hand |
| `A_016` | `Ms/53.p#1/mdl` | B_EX160 | Mission Mode's body: `016`'s settings (`saix_h`'s cloth through them) |
| `W00` | `Ms/53.p#60/mdl` | B_EX160 | his claymore object's normal model, as B_EX160's claymore meshes 10-13 (the rest hidden): `held kh2_379 rest 0 90 0` (W00 and W01 share one frame, the blade along z from -37.9 cm; 379's along x from -38.6) |
| `W01` | `Ms/53.p#62/mdl` | B_EX160 | the berserk one: 10, 11, 13 and the long spikes 16 for the short ones 12 (assumed from their shapes) |
| `A_W00`, `A_W01` | `Ms/53.p#61`, `#63/mdl` | B_EX160 | Mission Mode's: `W00`'s and `W01`'s settings |
| `B13_01body` | `Ms/54.p#0/mdl` | N_EX950 | the boss Riku's body (his skeleton is the player's node for node at rest, the hands 7.3 degrees off, its root `B13_01body`): `riku_h`'s bones and cloth; his own motions.txt with only P_EH000's idle (0), run (8), damage (42 front `005`, 50 back `006`), aerial damage (66, 74) and its landings (70, 78) by `air_floor`; the 6-frame run start `002` and stop `004` play KH2's run and idle (carried on by `003` and `001`, `start_loop`); the rest Days' own, every cut faded over 8 frames (`cut_blend`) but into his damage (`cut_blend_except`); KH2's readying and idle after his damage and its landing (`after 24 0`); his energy sword on the keyblade (`weapon_model ... weapon`); `weapon sa_h_R 262` and W_EH000's `weapon_motions` as `riku_h`'s. ov260 plays pose p as `B13_01body_<p + 1>` (`Ov260_SetPose`: clip p + 2) |
| `A_B13_01body` | `Ms/54.p#1/mdl` | N_EX950 | Mission Mode's body: `B13_01body`'s settings |
| `B13_02w`, `A_B13_02w` | `Ms/54.p#32`, `#33/mdl` | W_EX010_20 | his sword object as `ri_w14600`'s keyblade: `held kh2_2 body` on his KH2 hand, posed by his W_EH000.mset, `ri_w01600`'s keychain. `B13_03K` (#34: a 15-bone chain `Ov260_SetPose` shows in combat poses only, assumed his swing trail) and the effects are left to the game |

`SAME_SETTINGS` in the tool gives a model another's settings under its own name; what it has of its own stays, and
what the other takes from a third comes along (`A_016` from `016`, `016`'s cloth from `saix_h`).
The low poly bodies have the same nodes and animation names as the high poly ones (read from `def.p`, `def_h.p`
and `def_hb.p`).

A new file a setting names is added to `SOURCES` and `MODELS` in the tool
(`installer/tests/kh2_models_test.py` fails until it is), and to `hd_inputs.txt` by tracing.

## Not yet

- 4-bit textures (PSMT4) and other formats: reported and drawn white.
- Region clamp is drawn as repeat (the renderer repeats mods' textures).
- Normals, UV scroll and the chained shadow model are not read; of entry 0x17
  only the feet (types 10 and 11).
