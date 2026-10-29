# Secondary motion (cloth, cords, hair)

A mod's model can move parts of itself that its animation does not: a coat's
skirt, sleeve cuffs, drawstrings, short hair. The port simulates them on the
CPU after the pose is final (the game's animation, or KH2 motions retargeted and
cross-faded, `KH2_MODELS.md`) and before skinning: the simulation rebuilds the
bones that carry those parts, and the model is drawn through its bones as
always. What moves, how stiffly, what it collides with and how animations
override it is the model's own text file; nothing about a character is in the
code.

Code: `port/runtime/mods/secondary/`. Tests: `port/tests/secondary_motion_test.cpp`
(`khdays-secondary-motion`). Tool: `port/tools/secondary_replay.cpp`
(`khdays-secondary-replay`). Example setup: `docs/examples/secondary/`.

## Using it

```text
mods/<Mod>/graphics/<file>/mdl/<model>.mdlx            (or .gltf, .glb, .fbx)
mods/<Mod>/graphics/<file>/mdl/<model>.secondary.txt   its secondary motion
mods/<Mod>/graphics/<file>/mdl/kh2_secondary_presets.txt   presets it includes
```

`<model>.secondary.txt` sits beside the model as `bones.txt` and `motions.txt`
do. Its problems are written to the session log and stderr with file and line
(`secondary: roxas_b_h: roxas_b_h.secondary.txt:41: error: there is no bone
kh2_999 ...`); a file with errors leaves the model drawn without secondary
motion. The file and its includes are read again within a second of being saved
(the actors start afresh on the new setup).

| Variable | Effect |
|---|---|
| `KHDAYS_SECONDARY_DEBUG=1` | draws particles (white), anchors (red), links (cyan, toward red when stretched past 10 %; anchors orange) and colliders (yellow cores and end rings) over the model; or `debug on` in the file |
| `KHDAYS_SECONDARY_PROFILE=1` | every ten seconds, each actor's cost (p50/p95/p99 per update, per-phase per tick, startup, rebases, restarts, recoveries, room triangles) in the session log; or `profile on` |
| `KHDAYS_SECONDARY_TRACE=1` | every draw: the game's animations (frame, length, weight), how far the hips, the most-moved body bone and the most-moved cloth control moved since the last draw (cm), in the session log; for a jump at a loop or a cut |
| `KHDAYS_SECONDARY_OFF=1` | no model loads its secondary motion |

## The model it works on

The solver knows neither KH2 nor the game. It cooks the file against the model's
neutral skeleton and skinned mesh (`describe`: the importer's joints with their
names, parents and rest matrices; every vertex with up to four bones and
weights), the same for `.mdlx`, glTF and FBX. A KH2 model's bones are named
`kh2_<n>`, and a bare number in the file means that name; other models' bones
are named as their files name them, or by `path:Root/Spine/Chest` (each a
descendant of the one before).

**Controls.** Each simulated bone is a control: a particle at a point of the bone,
in its own space. That point is the next control's head when the next one in its
strip hangs below it (a chain joint); otherwise the influence-weighted middle of
the vertices it carries (an end, an ornament); otherwise its first child's head.
`offset <bone> <x y z>` sets it. A zero-length helper with no vertices and no
child further on cannot be a control (an error says so); helpers between
controls (N_EX830's 27-28 between 26 and 29) are carried. A vertex shared with
other bones follows the control by its weight, and the cook says how many each
control shares.

**Anchors.** A strip's first control hangs from a fixed particle at its own head,
riding its parent as the animation moves it. Under another simulated bone of
the same group (a hair tuft on a tuft) the anchor rides that bone as rebuilt.

**Rebuilding a bone** from its solved point `p`: its reference frame (below) is
swung so its offset points from its head to `p`; on surfaces its twist follows
its row neighbours (limited by `twist_limit`); `translation = p - R * S *
offset`; its scale is the animation's. A control below another control is
carried by that one's rebuilt frame, so parent motion is never applied twice;
bones below controls that are not controls are carried as the animation places
them under it.

## The file

Version first; then statements, `#` comments. Blocks end with `end`.

```text
secondary_motion 1          # the format version this port reads
units cm                    # lengths in this file: m (default) or cm
include kh2_secondary_presets.txt   # beside this file; its own `units`
simulation rate 60 substeps 4 iterations 2 max_catchup 4
gravity 0 -9.81 0           # m/s^2, the world's y up
environment on thickness 2  # the room's collision; its contact thickness
teleport 150 100            # a jump this far (or turn, degrees) in a tick rebases
idle_reset 1                # s unseen before an actor starts afresh
first_pose_iterations 30    # contact relaxation before the first picture
wind world 1 0 0 speed 2 gust 0.4 frequency 0.3 turbulence 0.2
lod 8 substeps 2 iterations 1 self_collision off   # from 8 (units) away
lod 20 frozen                                       # drawn as animated
debug off
profile off
```

**Groups.** `group <name> surface|chain|hair [preset <name>]` ... `end`:

| Line | Meaning |
|---|---|
| `attach <bone>...` | the body bone(s) it hangs from; the first is its reference frame |
| `strip <bone>...` | a surface's controls, anchor to hem (not necessarily parent to child) |
| `chain <bone>...` | a chain's controls |
| `tuft <bone>...` / `tufts <bone>...` | hair: one tuft (a short chain), or one single-bone tuft each |
| `ring <bone>...` | controls joined around in a closed loop (a cuff's circumference) |
| `topology mesh` | neighbours from the triangles whose corners' heaviest bones are its controls |
| `connect <a> <b>` / `disconnect <a> <b>` | a link added, or a mesh link cut (a slit, a seam) |
| `exclude <bone>...` | left out of its strips |
| `follow_bone <bone> radius <r> [weight <w>] [always <a>]` | its reference shape also rides a body bone near it (below) |
| `offset <bone> <x y z>` | a control's point, bone-local |
| `bone <bone> <setting> <value> [hem <value>]` | one control's own setting; `bone <bone> mode <mode>` |
| `hang <x y z>` | chains: hung straight that way (model space at rest) before settling |
| `settle on [steps <n>]` | its reference shape hung under gravity against the colliders at load |
| `twist neighbours|reference` | surfaces: a control's twist from its row (default) or its reference |
| `collide <tag>...` | the collider tags it collides with (none named: every collider) |
| `self_collision <name>` | groups with one name collide with each other and themselves |
| `mode`, `authored_policy`, `wind`, any setting | as below |

**Settings** (in groups, presets, `bone` lines and controls). A value may end
`hem <value>`: the setting runs from its plain value at the anchor to that one at
the free end, by depth along the strip.

| Setting | Unit | Default | |
|---|---|---|---|
| `mass` | kg a control | 0.05 | |
| `stretch_compliance` | m/N | 0 | along strips and chains (0 inextensible) |
| `row_compliance` | m/N | 0 | around rows, between strips |
| `shear_compliance` | m/N | 1e-5 | across faces |
| `bend_compliance` | m/N | 1e-3 | two links apart, straight through a control |
| `max_strain` | fraction | 0.08 | how far contact may stretch strips and anchors |
| `row_max_strain` | fraction | 0.08 | the same for rows and shear |
| `damping` | 1/s | 1 | velocity relative to the target lost: exp(-damping dt) |
| `follow_frequency` | Hz | 2 | the pull toward the target shape (0 none) |
| `follow_damping` | ratio | 0.6 | of critical damping of that pull |
| `gravity_scale` | | 1 | |
| `max_distance` | length | 0 (none) | from the target |
| `tether_slack` | fraction | 0.05 | past the rest path from the strip's anchor |
| `swing_limit` | degrees | 180 | from the target direction about the head |
| `twist_limit` | degrees | 180 | about its own axis |
| `inertia` | 0..2 | 1 | of the attachment's acceleration felt (1 physical) |
| `guided_inertia` | 0..2 | 0.5 | the same in guided mode |
| `max_speed` | m/s | 20 | relative to the target |
| `smoothing` | s | 0 | the drawn shape eased toward the solved one, in its reference bone's space (below) |
| `follow_bone_scale` | 0..1 | 1 | of its `follow_bone` shares it takes; by depth (`hem`), per bone or per animation (`control`) |
| `drape` | 0 or 1 | 1 | its target swung out of the body colliders down its strip (eased, below) |
| `thickness` | length | 0.01 m | contact radius of controls, edges and faces |
| `friction` | 0..1 | 0.2 | |
| `wind_drag` | 1/s | 1 | how quickly it takes up the air's velocity |
| `wind_lift` | 0..1 | 0.5 | (kept for surfaces' normal force; not used yet) |

Values outside a setting's range are errors (`damping -1 is out of range (0 to
200)`). **Precedence**, lowest first: the defaults; a preset's base (`extends`)
then the preset; the group; its `bone` lines; then at run time the animation
controls by priority (below).

**Colliders** are the body's, on bones:

```text
collider <name> sphere <bone> radius <r> [offset x y z] [tags ...]
collider <name> capsule <bone> <end bone>|length <l> radius <r> [<end r>]
         [offset x y z] [end_offset x y z] [fit [<percentile>] [scale <s>]] [clear] [tags ...]
```

A capsule runs from its bone's point (`offset`, bone-local) to its end bone's
(`end_offset`), or `length` along the bone's x; two radii make it tapered. `fit`
sizes it to the vertices skinned mostly to its bone beside its segment (that
percentile of their distances, per half for the two radii; vertices past its ends
belong to its neighbours: a thigh measured to its hip end came out 13.6 cm where
the thigh is 9-10); `clear` shrinks it so every control it
collides with starts outside it at rest. A control still inside a collider at
rest is left out of it, with a warning naming both and the depth. Tags (default
`body`) are what groups' `collide` lines name.

**Reference shape and modes.** A group's target shape is its rest shape (or the
settled one) carried by its first `attach` bone. `follow_bone` lets controls near
another body bone ride it too (`Simulation::blend_weights`). With `always`, a
control within `radius` of the bone's segment at rest rides it in every pose by
`always x (1 - distance / radius)`: the legs carry a coat's shape, so a wide
stance spreads it in its target and contact only catches what is left (the
leg-driven skirt bones of shipped bone-cloth rigs). `weight` is the most it rides
once the bone comes nearer its hanging place than at rest (smoothstep over that
approach): a thigh raised into the coat carries the panels near it. The first
attach keeps the rest (at least 5 %); positions and frames are blended.

| `mode` | Target | |
|---|---|---|
| `simulated` (default) | the reference shape on the body | the animation does not move it |
| `guided` | the animation's own pose of the bones | a soft target (its sway not doubled: `guided_inertia`) |
| `authored` | the animation | `authored_policy exact`: the animation's matrices as they are, scale and all; `collision_corrected`: held to the animation but pushed out of contact |

Exact authored output and guaranteed contact can conflict; the policy says which
wins. Authority moves inside the solver (a control's target and the pull's
strength blend toward the animation), never as a final blend of poses, and the
particles keep their state while authored, so a release continues from where
the animation left them. Authored cloth is never guessed from which bones have
keys: B_EX390's slots 140 and 208 move their coat through helpers and
constraints.

**Animation controls** change any of this while game animations play:

```text
control <name>
  animation b_ro_Ca003 ...  # the game's animation names (KHDAYS_TRACE_MODELS=1 logs them); * any
  slot 644                  # only while that KH2 mset slot plays it (motions.txt)
  section 12                # only in the motions.txt section from that frame
  time 0 1.2 seconds        # within the animation: seconds, frames (30 a second) or ticks (60); `end`
  loop on                   # the time wraps at the animation's length
  groups coat               # or `bones 32 40 ...`
  weight 1
  blend 0.15 0.3            # eased in and out over the time range, and on and off (seconds)
  priority 1                # higher ones applied over lower ones
  mode guided               # and/or authored_policy, settings, `wind ...`
end
```

Its weight is the playing animations' blend weights (the game's cross-fade
ratios) times its time envelope, eased by its blends, so a cut between
animations does not pop. Settings are blended toward its values by that weight;
mode by authority; its wind replaces the group's by weight.

**Wind**: `wind world|actor|bone <bone> <x y z> speed <m/s> [gust <f>]
[frequency <Hz>] [turbulence <f>]` (global, per group, per control). Gusts and
turbulence are smooth sums of sines with a per-actor phase; the air drags each
control by its velocity relative to the air (`wind_drag`).

## Solver

XPBD (Macklin et al. 2016) on contiguous arrays sized at load; no allocation in a
tick once the room's triangle buffer has its working size. A tick (fixed 60 Hz):

1. The room's triangles near the actor gathered once.
2. Per substep (4): each needed bone's frame interpolated between the tick's
   two poses (positions linearly, rotations nlerp); the colliders and anchors
   placed; each control's target computed; its velocity integrated relative to
   its target's: the target's acceleration not felt scaled by `1 - inertia`,
   damping `exp(-damping h)`, gravity, the air (`wind_drag`), and `max_speed`.
3. Per iteration (2), multipliers reset each substep: links (compliance / h²),
   the pull toward the target (XPBD with damping: compliance 1/(m w^2), gamma =
   2 zeta / (w h)), tethers, `max_distance`, swing cones, strain limits; then
   contact: body colliders (controls each iteration; edges and four samples per
   coarse face on the substep's last), the room, self-contact (last iteration);
   then anchors re-projected (the body attachment wins over contact).
4. Twice more: strain limits, control contacts, anchors (a push spreads to the
   neighbours instead of tearing one control off them).
5. Velocities from positions. A group with a non-finite particle, or one 4 m
   from its target, starts again on its targets (counted as a recovery).

Contact: a control crossing a capsule's core in one substep is pushed back to the
side it came from; an edge or face contact moves no corner more than twice the
overlap (a corner near a fixed anchor would otherwise be thrown). Friction removes
tangential motion relative to the collider's surface up to `friction` x the
overlap. A one-sided room face pushes back to its front a control that crossed it
this substep, sits less than 5 cm behind it, or belongs in front (a floor is solid
below; a wall where the target is in front), up to 30 cm deep; deeper is left (a
wall's other face). Self-contact: a control against the faces of its
self-collision groups, from the side it was on, neighbours (two links) and pairs
already touching at rest (a slit, an overlapping layer) left out.

Drawn shape (`smoothing`, `Simulation::reconstruct`): each control's drawn point
is eased toward its solved one by `1 - exp(-dt / smoothing)` a tick, in its group's
reference bone space, so the body's own motion is never delayed; a drawn point
that would sit inside a body collider the solved one is clear of is pushed out on
the solved point's side. Bones are rebuilt on the drawn points. A bone whose link
is squeezed under 50 % of its rest length or folded back past its head eases from
the shortest turn onto its point back to its reference's (the turn has no clear
direction there), and the twist from row neighbours fades out where they bunch
together or cross.

**Once a cycle (measured in game, `KHDAYS_SECONDARY_TRACE`).** A jump of the
lower back coat once per animation cycle (Days' animations and KH2 motions alike;
in the calm idle 2.6 s into each 3 s cycle, not at the loop's seam) with the body,
the colliders and the targets still: the drape swung a target lying between a
thigh and a calf out of one into the other, a different answer step to step, and
the coat chased it. The drape's swing is now eased in the reference bone's space
over 0.06 s (`Simulation::ease_drape`). Replay at the game's 30 draws a second,
drawn jitter p99: calm idle through its loops 11 -> 1.6 cm, battle idle 11.6 ->
5.1; at 60: calm 3.6 -> 0.9, attack 13 -> 8.8. The wall scenario keeps a leg in
the coat for more frames (7 -> 30 of 301).

**Twitching (measured, replay `jitter` line).** A leg sweeping through the coat
gave contact, the strip links and the strain limits no common answer: in one
tick a control under the belt moved 27-37 cm by links, 25-46 cm by contact and
up to 63 cm in the closing passes, a different place each frame. Coat controls
moved up to 50 cm a frame relative to the hips while their targets moved 1-5, and
coat bones turned past 200 deg. A per-substep cap on contact's push left the legs
in the coat every frame; a lower `max_speed` changed little. What N_EX830 uses:
strips that may stretch (`max_strain 0.6`, KH2's own coat stretches about 2x in
the battle idle), no roll (`twist_limit 0`), `smoothing 0.05` and the rebuild
above. Drawn jitter, p99 per frame (before -> after): battle idle 52 -> 3 cm and
115 -> 17 deg, calm 1.6 -> 0.6 cm, run 57 -> 23 cm and 223 -> 102 deg, guard 26 ->
21 cm and 143 -> 63 deg; clipping as before (run 13 frames over 1 cm, attack 4).

## Time

The simulation runs on the game's clock, not the draws: `gObjSystem`'s frame
count (`Obj_UpdateAll` advances it only while not paused) times the frame's length
at the game's frame-rate mode (`GetFrameRateMode`: 30, 20 or 60 a second). Ticks
are counted whole from that clock, so a draw repeated, a second render pass or
the render rate change nothing; a paused game stands; a held animation frame
does not stop the cloth. Poses between two draws are interpolated for the ticks
between them. More than `max_catchup` ticks due at once skips the rest. A jump of
the clock backwards or past ten seconds (a save state, a reload) is a new epoch
and the actor starts afresh, as after `idle_reset` unseen. A jump of a group's
reference bone past `teleport` within a tick rebases the state rigidly (shape
kept, relative velocity zero). A start places every control on its target at the
first pose, still relative to it (a moving start moves with it), relaxes contacts
`first_pose_iterations` times and only then draws.

Actors are the drawn render objects, each with its own state; an actor unseen for
`idle_reset` plus a second is dropped (no table is ever cleared wholesale).

## In the game

`models.cpp` calls `apply_model_secondary` in `draw` after `play_kh2` (or the
game's pose) and before the weapon attachments and skinning. Each needed bone's
frame is `inverse(camera) x skin x inverse(inverse bind)` divided by the actor's
world units per model unit (from the root node's matrix): the solver's space has
the world's axes (y up) and the model's metres (KH2 centimetres / 100, Days' world
about a metre a unit). The camera is the latest flush's (`khdays_g3d_camera`);
it only converts. Rebuilt bones go back the same way into `skin`.

**The game's rooms** (`days_environment.cpp`, read only; layout from the
decompilation): `gEntityMgr` (0x0204c208) holds eight model groups at +4 (8 bytes:
u16 flags, u16 count, u32 blob list; `GetTrackEntryBase`, `ModelGroup_Open`); the
casts read only each list's first blob. A blob is used once its +0x74 bit 0x8000
says its pointers are resolved. Faces: grounds (+0xa0, count +0x7c, 0x88 bytes),
walls (+0xa4, +0x7e, 0x84), ceilings (+0xa8, +0x80, 0x84); each u16 flags +0x10
(0x4000 disabled, checked at every gather: walls that open), u16 corners +0x12
(3 or 4), s16 normal +0x14, fx32 corners +0x50, u8 named-record slots +0x80 (slots
1-2 naming a record tagged 5 are pass-through, left out). World point = stored -
the blob's cast offset (+0x64). Each blob's faces are cooked into a BVH when it
appears or its header changes (counts, region, face tables: a reloaded room can
reuse an address). Room boxes (vtable 0x02042928) are snapshotted each frame from
the quadtree's object lists (+0x9c, nodes +0x7a, 0x20 each, list +8, next +4):
position +0x2c, size +0x38/+0x3c/+0x40 (half extents width/2 and depth/2,
`RoomBox_UpdateBounds`), angle +0x44 (its sense not verified). Characters'
cylinders are not cloth colliders. Nothing calls the game's casts.

## Setting up another character

1. Find the bones. `KHDAYS_TRACE_MODELS=1` lists a model's joints; for KH2 models
   the replay tool's cook output and `docs/KH2_MODELS.md`. Group the cloth's
   bones into strips anchor-to-hem, chains, tufts.
2. Write `<model>.secondary.txt`: `secondary_motion 1`, `units`, `include` the
   presets, colliders (`fit` and `clear` give a start), groups, `topology mesh`
   for surfaces, `disconnect` across slits.
3. Run the replay tool on it (below): it prints the cook's warnings (shared
   vertices, controls inside colliders, loose surfaces), the clipping, stretch and
   cost per scenario, and draws contact sheets of the simulation's proxies.
4. Tune: presets first, then `hem` values, `follow_bone`, `bone` overrides,
   animation controls for moves that need them.
5. In the game, `KHDAYS_SECONDARY_DEBUG=1` and edit the file while it runs.

## N_EX830 (the coat Roxas)

`docs/examples/secondary/N_EX830.secondary.txt`, installed as
`roxas_b_h.secondary.txt` beside `roxas_b_h.mdlx` (byte for byte N_EX830) in the
test mod; its motions stay `roxas_b_h.motions.txt`'s (P_EX110's, retargeted).

- Coat: 8 strips under the belt (26-32, 34-40, 42-48, 50-56, 58-64, 66-72,
  74-80, 82-85-88-90), the front forking under 85 (85-92-94); 43 controls, 44
  listed with 85 shared. Their vertices sit exactly at their points (each bone's
  ring at its tip; the hem bones' at 17.4-17.6 cm, not their stored 23.4-23.6 cm
  length). `topology mesh` finds the coarse surface (64 faces; the upper row
  joins bone 2's vertices, read as the anchors); `disconnect 88 92` keeps the
  slit. `follow_bone 5` and `14` (thighs), `always 0.7` within 30 cm, carry the coat's reference with
  the legs (calm idle: contact's push off the targets 14.7 -> 10.1 cm, battle idle 24.6 -> 17.3). Not the
  shins: below the knee the coat traced the calf back instead of falling off the knee. The back strips
  (26-32, 50-56, 74-80) take less (`follow_bone_scale` 0.6 to 0.2 toward the hem). The pull toward the
  shape on the hips is weak at the hem (`follow_frequency 4.0 hem 0.3`) and the rows free there (`hem 4`,
  `hem 0.05`): in the battle idle that shape tips with the pelvis to 35 cm behind the knees and under the
  floor, and pulled the lower coat back after it went over the knee (front hem 26 -> 41 cm ahead of the
  hips, the knee's front at 44). The calm idle sways a little more for it (drawn jitter 0.6 -> 3.6 cm).
  Simulated: P_EX110 has no coat tracks.
- Drawstrings 103-106 and 220-223 from helpers 102 and 219 (bind pose points
  forward): hung down and settled against the chest at load.
- Cuffs 118-128 / 235-245: four two-control strips joined by two rings each,
  stiff, colliding with the hands only.
- Hair: single-bone tufts under 170 (and 181, 183, 185 on 178): guided, 7 Hz,
  10 degree swing.
- Hood: one bone (101); it settles onto the back and swings as one piece (folding
  would need more bones than the model has).
- Colliders: pelvis, spine and chest (`torso`: hood, drawstrings), thighs (from
  12 cm down the bone), calves, feet, arms, forearms (to the wrist), hands, head.
  The coat collides with the pelvis and legs only.
- Placement: a field actor's world matrix (actor +0x10c, which Ov002_SubmitActorDraw
  multiplies onto the view before drawing actor +0x24) times the inverse of the
  matrix its draw starts from, so the camera cancels out; other draws fall back to
  the latest flushed camera.

**Measured against B_EX390's bake.** B_EX390 shares bones 0-94 with N_EX830 (the
same coat rig at rest). Its baked coat keeps every strip's length and stretches
its rows 50-470 % (the skirt flares between strips), so the coat preset lets rows
stretch (`row_compliance`, `row_max_strain 1 hem 1.5`) and strips not. Driven by
B_EX390's body, the simulated hem swings 40-70 % as far as the bake's; the bake's
coat is authored per motion (B_EX390's motions are a boss's, with far wider
flares than the player's), and no body-driven setting reproduces it control for
control (RMS 33-45 cm), as the earlier fits found.

## Replay tool

```bash
khdays-secondary-replay N_EX830.mdlx N_EX830.secondary.txt roxas_b_h.motions.txt --out out
khdays-secondary-replay ... --scenario run --actors 8 --draw-hz 30 --repeat 2
khdays-secondary-replay ... --reference B_EX390.mdlx B_EX390.mset 0,10,140,208
```

Plays the scenarios (idle, run with a 180 degree turn and a dead stop, attack with
lunges, jump with landing, guard, wall and corner while turning and backing,
spawn while running, teleport, a platform rising under the coat) on the KH2
motions as the port does (retargeted, matrix cross-fades between segments, root
motion taken out, the actor placed on a path), and writes `report.txt` (clipping
of the skinned simulated vertices beyond their rest overlap with the colliders
their group uses, vertices under the room, coat strain, recoveries, rebases, cost
percentiles, per-phase cost) and a contact sheet per scenario of the proxies
(colliders, coarse faces, links, anchors).

## Results

Measured 2026-10-29 with the replay tool (Release, MSVC 19.44 /O2), AMD Ryzen 7
7800X3D (8 cores, 4.2 GHz), 64 GB; N_EX830 fully enabled: 7 groups (coat,
drawstrings, cuffs, hood, hair), 90 controls, 41 anchors, 266 links, 64 coarse
faces, 16 colliders, room contact, self-contact, wind off; 3 substeps x 2
iterations at 60 Hz. Cook and settle at load: 170 ms.

"Legs in coat" is how far any leg collider's core reaches into or through a
coarse coat face (the coat's actual drawn geometry: its vertices sit on the
controls), over every frame:

| Scenario | legs in coat | frames over 0.5 cm |
|---|---|---|
| idle (KH2's crouched battle stance) | 0 | 0 of 241 |
| run, 180 degree turn, dead stop | 0 | 0 of 421 |
| attack with lunges | 0 | 0 of 211 |
| jump and landing | 0 | 0 of 211 |
| guard | 0 | 0 of 181 |
| spawned while running | 0 | 0 of 121 |
| teleport | 0 | 0 of 241 |
| platform rising under the back hem | 0 | 0 of 241 |
| wall and corner while turning | 19.9 cm | 2 of 301 (coat pinned between a leg and the wall) |

Cost per actor update: p50 484 us, p95 769 us, p99 951 us (one tick per
draw); per tick: model contact 320 us, solve 86, self-contact 27, rebuilding the
bones 23, room 18, pose 7; first pose 3-4 ms. The 0.5 ms p95 aim is not met: exact
face-against-capsule contact costs that much more than the sampled faces did.
At peak the coat's strips stretch well past their 6 % (the body wins over the
cloth's length whenever both cannot hold): up to 3.6x in the run's knee lifts.

### How contact keeps the legs out

Each was found by measuring the legs against the coat's faces and reading why:

- The coat is a shell around the body: each control and face has an outward
  direction (away from its group's reference bone's vertical axis at rest).
  A face a body collider pierces is carried out that way past the core's end (as
  cloth rides over a kick, at most 50 cm); a face merely touching keeps the side
  it was on at the substep's start (a knee out through the slit stays in front).
- A face is pushed out whole (all its corners by the same amount): shared by
  barycentric weights it pivots on a far corner and stays inside.
- Targets are draped: each strip's target shape is laid down from its anchor,
  segment by segment at its own lengths, swung out of any body collider it would
  enter. The pull then leads around a raised knee.
- At the first pose the colliders sweep in from their rest places, so a stance
  with the legs already apart starts with the coat around them.
- Colliders with `side motion` (arms, hands) only push away from where a part came
  from and come first; the body (`side rest`) comes last, so cloth caught between
  a hand and a leg stays out of the leg. N_EX830's coat collides only with the
  waist, hips and legs (pelvis, thighs, calves, feet); the cuffs with the hands.
- Contact closes every substep (after the strain limits), and its push changes a
  control's velocity by no more than `max_speed` past its target's (a push moves
  cloth, it does not launch it).
- The thighs start 12 cm down the bone: the hip root is the pelvis capsule's.
- Authored clearance: each collider-control and collider-face pair's distance at
  rest (`Rig::point_clearance`, `face_clearance`) caps its contact distance
  (never under half the collider's radius), so a coat that hangs nearer a leg
  than the leg's collider is held there as authored, not pushed out to the
  collider. Against P_EX100's baked coat (below) it took the battle idle from
  19.3 to 17.9 cm and the calm idle from 6.4 to 4.4 cm, with fewer clipping frames.

## Not yet / residual cases

- Wind's surface lift (`wind_lift`) is parsed, not applied.
- A room box's turn sense is unverified; characters' cylinders are not colliders.
- The camera is the latest flush's; a flush after a model's draw in the same
  frame would show as motion (not seen).
- B_EX390's authored flares are larger than any body-driven setting produces;
  matching one motion closely needs an authored control (`mode guided` on a
  model whose motions carry coat tracks).
- The calm stance in game still flares (hem about 1.7x the rest drape) where
  P_EX110's motions, mapped onto N_EX830 with IK, hold the knees 7-8 cm further
  apart than KH2's own coat Roxas (P_EX100) stands; the rigid-on-the-hips target
  then has to be pushed round them; the legs now carry the targets (`always`).
  Measured on calm idle (P_EX110 slot 2, tick 0): P_EX110's own knee bends 26 deg,
  N_EX830's after the bone map and `ik` 42 deg (battle idle 91 vs 100): its hip
  lands 3.4 cm lower while the ankle is pinned where P_EX110's is (`Retarget::apply`
  limbs), so the knee folds and swings out. A retarget fix, not a cloth one.
- The wall scenario keeps the coat inside the body for many frames since the
  authored-clearance cap (a wall pressing the coat onto the pelvis).

### Against KH2's own coat (P_EX100)

`--reference P_EX100.mdlx P_EX100.mset 0,2` drives this model with KH2's coat
Roxas's motions (bones 0-94 shared) and compares the coat with its baked tracks.
Slot 0 is the battle idle (`bIdl`), 2 the calm idle (`Idle`). The bake stretches
its coat links up to about 2x in the battle idle (5x in the battle run), and its
hem spreads to about 3x the rest radius there, 1.5x in the calm idle.

| Coat | battle idle RMS | calm idle RMS |
|---|---|---|
| simulated (rest shape on the hips) | 17.9 cm | 4.4 cm |
| guided, links at rest lengths | 17.1 cm | 4.2 cm |
| guided, links and tethers at the animation's lengths | 3.2 cm | 1.1 cm |

In guided mode the links, strain limits and tethers take the animated lengths
(`Simulation::animated_lengths`), so a coat animation that stretches is followed
rather than held at the rest lengths. Some of P_EX100's coat motions are
hand-made; the idles are KH2's. Its mset carries effect entries whose offsets run
past their motion's archive; those entries are skipped (`bar_entries`, `skip_bad`).
