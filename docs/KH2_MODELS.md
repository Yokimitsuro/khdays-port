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
and reads every texture back in its own format: P_EX110's 8-bit textures are
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
once in the log). `KHDAYS_TRACE_MODELS=1` logs the names of the animations each
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
ticks a BCA frame), so the game's pauses and speeds carry over; a frame set back
counts as one step on (the last forward step). Past its end the motion
continues from its loop point (retFrame), or holds when there is none. During
the game's cross-fade (two animations, their ratios summing to 1) each side is
blended at the game's weight, the game's own pose standing in for an unmapped one.
Each actor (render object) keeps its own pose.

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
nodes' space (DS fixed point, the same layout). The weapon's root sits on the
bone's frame, as KH2 attaches one: right for a KH2 weapon model; a Days weapon
model, made for Days' joint, may sit turned. A KH2 weapon's rest pose is not
its held one: W_EX010's bone 0 rests 1.2 m from its origin, turned (0, 270,
270 degrees), and every motion of W_EX010_RX.mset puts it within 6 cm of the
origin nearly unturned (123-147 degrees from rest), so the weapon is drawn right
only while its own motion plays (its slot matching the body's: R000 at 0 in
both). A weapon drawn before the body in a
frame takes the last frame's place, as in the game. Joints without a line keep
the game's place; the line works without an mset.

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

## Not yet

- 4-bit textures (PSMT4) and other formats: reported and drawn white.
- Region clamp is drawn as repeat (the renderer repeats mods' textures).
- Normals, UV scroll and the chained shadow model are not read; of entry 0x17
  only the feet (types 10 and 11).
- The remastered HD textures (`remastered/obj/<name>.mdlx/-N.dds`) are not used.
