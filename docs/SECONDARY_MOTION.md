# Secondary motion (cloth, cords, hair)

A mod's model can move parts of itself that its animation does not: a coat's
skirt, drawstrings, short hair. The port simulates them on the
CPU after the pose is final (the game's animation, or KH2 motions retargeted and
cross-faded, `KH2_MODELS.md`) and before skinning: the simulation rebuilds the
bones that carry those parts, and the model is drawn through its bones as
always. What moves, how stiffly, what it collides with and how animations
override it is the model's own text file; nothing about a character is in the
code.

Code: `port/runtime/mods/secondary/`. Tests: `port/tests/secondary_motion_test.cpp`
(`khdays-secondary-motion`). Tool: `port/tools/secondary_replay.cpp`
(`khdays-secondary-replay`). The setups in use: `tools/hd/kh2_models/`, each model's `mdl` folder.

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
motion. The file and its includes, one that could not be opened too, are read
again within a second of being saved (the actors start afresh on the new setup);
saved with errors while running, the model keeps its last good setup until they
are fixed.

| Variable | Effect |
|---|---|
| `KHDAYS_SECONDARY_DEBUG=1` | draws particles (white), anchors (red), links (cyan, toward red when stretched past 10 %; anchors orange) and colliders (yellow cores and end rings) over the model; or `debug on` in the file |
| `KHDAYS_SECONDARY_PROFILE=1` | every ten seconds, each actor's cost (p50/p95/p99 per update, per-phase per tick, startup, rebases, restarts, recoveries, room triangles) in the session log; or `profile on` |
| `KHDAYS_SECONDARY_TRACE=1` | every draw: the game's animations (frame, length, weight), how far the hips, the most-moved body bone and the most-moved cloth control moved since the last draw (cm), in the session log, with each one's slot, a pose cut and a fresh start marked, how it found its place in the world (`placed by ...`) and where it is (`at`, metres); a KH2 model's `kh2 trace` line beside it: the game's state, each animation's weight as the game gave it, the key, slot, time and weight each played, the idle fade and `finish` tail; with `weapon_motions`, `hand trace` (the weapon's joint, the frame written and the game's own, moved and turned since the last draw, in the root's space); a `held` weapon's `held trace` (its start against the frame written, its nodes from the first); for a jump at a loop or a cut |
| `KHDAYS_SECONDARY_OFF=1` | no model loads its secondary motion |
| `KHDAYS_SECONDARY_NO_PREWARM=1` | rigs are cooked as their models load, not ahead on a thread of their own once the first loads (`model_link.cpp` prewarm_model_secondary: a later load takes its rig from there when its files are unchanged) |

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
# environment on [thickness <t>] [ground]: the room's floors, walls, ceilings and objects; `ground` the floors alone
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
| `topology mesh` | neighbours from the triangles whose corners' heaviest bones are its controls |
| `connect <a> <b>` / `disconnect <a> <b>` | a link added, or a mesh link cut (a slit, a seam) |
| `exclude <bone>...` | left out of its strips |
| `follow_bone <bone> radius <r> [weight <w>] [always <a>]` | its reference shape also rides a body bone near it (below) |
| `lean_bone <bone> [rotate <x> <y> <z>]` | what `lean` turns its reference shape toward: the reference bone as that bone carries it, at their rest turn, turned by those degrees about the reference bone's own axes (x, then y, then z) (below) |
| `offset <bone> <x y z>` | a control's point, bone-local |
| `bone <bone> <setting> <value> [hem <value>]` | one control's own setting; `bone <bone> mode <mode>` |
| `hang <x y z>` | chains: hung straight that way (model space at rest) before settling |
| `settle on [steps <n>]` | its reference shape hung under gravity against the colliders at load |
| `twist neighbours|reference` | surfaces: a control's twist from its row (default) or its reference |
| `collide <tag>...` | the collider tags it collides with (no `collide` line: every collider; a bare `collide` or `collide none`: none) |
| `self_collision <name>` | groups with one name collide with each other and themselves |
| `environment on\|off` | `off`: the room's collision leaves the group alone (default on) |
| `clip_world [inset <length>]` | surfaces: the room's polygons inside the closed volume its strips make are not drawn (below) |
| `shell_push <tag>... [from <0-1>]` | `cutscene_sim`: the shell pushed out of the colliders with these tags (below) |
| `mode`, `authored_policy`, `wind`, any setting | as below |

**Settings** (in groups, presets, `bone` lines and controls; `mass` and `thickness`
are read once at load, so a control setting them is an error). A value may end
`hem <value> [curve <n>]`: the setting runs from its plain value at the anchor to
that one at the free end, by depth along the strip raised to `curve` (above 0, up
to 8; default 1, linear).

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
| `follow_bone_scale` | 0..3 | 1 | of its `follow_bone` shares it takes; by depth (`hem`), per bone or per animation (`control`); over 1 the legs carry it more than `always` and `weight` give, 95 % at most in all |
| `drape` | 0 or 1 | 1 | its target swung out of the body colliders down its strip (eased, below) |
| `draw_guard` | 0 or 1 | 1 | a smoothed drawn face cutting into the body deeper than the solved one moved out (below) |
| `thickness` | length | 0.01 m | contact radius of controls, edges and faces |
| `friction` | 0..1 | 0.2 | of a control against a collider |
| `face_friction` | 0..1 | 0 | of a face resting on a collider, from at most 1 cm of its push (below) |
| `no_sim` | 0..1 | 0 | not simulated: each substep on its target (its reference bones, the legs' `follow_bone` carry, the drape round the body) instead of where its motion would take it, that far of the way (`no_sim` alone is 1); contact with the body still keeps it out of the legs (from the last substep, so it keeps its side), the room does not touch it |
| `game_shape` | 0..1 | 0 | its target taken that far onto its place on the replaced game model's coat, and at 1 kept there after the links and contact, unsmoothed (below) |
| `cutscene_sim` | 0 or 1 | 0 | simulated about its place on the game model's coat, never inside the shell those places make (below); alone on a line: 1 |
| `lean` | 0..1 | 0 | of the way its reference shape, and the colliders on its reference bone, turn about that bone's head toward its group's `lean_bone`'s carry (below); none without a `lean_bone` |
| `rotate_only` | 0 or 1 | 0 | a rebuilt bone only turns toward its point; its head stays where its simulated parent carries it or the animation has it (1: no stretch) |
| `wind_drag` | 1/s | 1 | how quickly it takes up the air's velocity |

Values outside a setting's range are errors (`damping -1 is out of range (0 to
200)`). **Precedence**, lowest first: the defaults; a preset's base (`extends`)
then the preset; the group; its `bone` lines; then at run time the animation
controls by priority (below).

**Colliders** are the body's, on bones:

```text
collider <name> sphere <bone> radius <r> [offset x y z] [tags ...]
collider <name> capsule <bone> <end bone>|length <l> radius <r> [<end r>]
         [offset x y z] [end_offset x y z] [fit [<percentile>] [scale <s>]] [clear] [side rest|motion] [tags ...]
```

A collider on a bone the simulation moves (a hood's) follows that bone as simulated, not as
animated: the last tick's rebuilt frame, kept on its group's reference bone and carried by that
bone as posed now, so only the cloth's own swing is a tick behind (`Simulation::shapes`); until a
tick has rebuilt it (at load, after a reset) it stands where the animation has the bone. Tag it
apart from the bone's own group. A capsule runs from its bone's point (`offset`, bone-local) to its end bone's
(`end_offset`), or `length` along the bone's x; two radii make it tapered. `fit`
sizes it to the vertices skinned mostly to its bone beside its segment (that
percentile of their distances, per half for the two radii; vertices past its ends
belong to its neighbours: a thigh measured to its hip end came out 13.6 cm where
the thigh is 9-10); `clear` shrinks it so every control it
collides with starts outside it at rest. A control still inside a collider at
rest is left out of it, with a warning naming both and the depth. `side rest` (the
default) keeps what it touches on the side it has at rest, `side motion` on the
side it came from (below, "How contact keeps the legs out"). Tags (default `body`)
are what groups' `collide` lines name.

**Lean** (`lean_bone`, `lean`): a coat hangs from the hips (its first `attach`), so an animation
that tips the hips against the spine tips its shape with them. With `lean`, usually set by a
control, its reference shape turns about the hips' head that share of the way to the turn the
`lean_bone` gives the hips (as at rest against it, plus the line's `rotate`), and so do the
colliders on the hips (the group's mean `lean`, the most of any leaning group on that bone),
or the coat would rest on them where it was. Its anchors, smoothing space and the other
colliders stay (`Simulation::lean_of`, `reference_point`, `reference_frame`, `shapes`).
Tried on riku_h's glides (`lean_bone 97 rotate 0 0 -20`, `lean 0.4` in the hover: the back rows
23-36 to 21-30 cm behind the spine line; more, or any in the forward glide, let the thighs and
seat through, since only the hips' colliders lean), then replaced by turning the pelvis itself
(KH2_MODELS.md, `ds_pose rotate` on a paired bone), which moves the body, its colliders and the
coat together; no shipped file uses it now.

**The game model's coat** (`game_shape`, `cutscene_sim`, `set_game_mesh` in
`model_link.cpp`). Only a group that sets one of them, or that an animation control
sets them on, is locked, and only with follow bones and one forked strip (the
front). The game coat is the triangles on the reference bone's node, those above it,
its children and the follow bones' nodes and their children, in the pieces reaching
the hips (not the game's own legs); its places welded within 2 cm.

- The back, each side's middle strip and the strips in front of them are each laid
  from their fixed point (the waist) down two paths of the game coat's edges (shortest
  along them), top to knee vertex and on to bottom vertex, each the place nearest its
  way around at the coat's top, the game's knee height and the coat's bottom, each
  place taken once (the front's first, then the back, the sides, the strips in front).
  Each bone runs from the last one's end to the first place on the path its length
  away, the bones of each section scaled alike (bisection) so the knee row (the strip
  point nearest the mod's knee) ends exactly on the knee vertex and the last on the
  bottom.
- The forked front runs down its middle to the slit's top (the highest open place in
  front, under the top), its two panels down the slit's edges to their corners (where
  the edge stops falling steeper than it runs). Its points below its first down to the
  fork lie halfway between the strips in front of the sides at that row, its bones
  there turned halfway between theirs (their tails kept on their points). Each panel's
  points between the fork and the corner take their neighbour's bow: on the line from
  the point above to the one below (their share of the way at rest), out by as much as
  the strip beside the panel stands out of its own such line at that row.
- The strips behind the sides lie row by row on an arc between their side's and the
  back's points, at their share of the angle between them at rest, bowed outward from
  the coat's middle by tan(angle / 4) / 2 of the gap times 4t(1 - t).

`game_shape` takes the target that far onto those places (at 1, the particle too,
after the links and contact). With `cutscene_sim` the places are the targets (not the
bones' or the legs' carry, and not draped), and the strips side by side round the body,
closed across the slit, make a shell: each substep a particle behind its place's
tangent plane (the shell's faces round it, by area) is put back on it, after the
constraints and last, so it slides along the shell and swings out freely. It moves in
its hips' frame: its damping, inertia, the pull's damping and the contact's velocity
clamp are about its place as the hips carry it (`reference_bone_frame`), and the
shell's push is contact (`pushed_`), so the game's legs push it without throwing it.
Its links, tethers, rows and faces take the places' lengths, with the group's
compliances. Only the colliders its group's `collide` names touch it; not the room, not
itself; drawn unsmoothed. At a start, a cut, a jump and when the game's pose snaps (a
place a particle takes moving more than 0.35 m about its group's hips in one draw,
treated as a camera cut, `actor.cpp` kGameSnap) it is put on its places, still, and
drawn there. A cut within 0.5 s of the last settle is not settled again (kSettleGap).

`shell_push <tag>... [from <0-1>]` pushes that shell out of the colliders with these
tags each substep, before the cloth moves (`Simulation::push_shell`): a face within a
core's radius plus the particle's thickness has its corners moved out, by where on it
the core comes nearest (barycentric), four passes; `from` leaves each core's start out.
The faces across the slit are left out. The places move, not the cloth, so nothing
squeezes it.

**`clip_world [inset <length>]`** (surfaces): the room's polygons inside the closed
volume the group's strips make are not drawn, so a coat through a wall shows its own
inside there and not the wall; a wall really in front of the model still covers it. The
volume (`rig.cpp` clip_volumes_of): each strip from its fixed point down to its end a
column (a forked strip's shared top in both branches), the columns round the body by
their ends' angle about the coat's axis at rest, each stitched to the next by its share
of the way down, a quad split along the diagonal the drawn mesh's faces use, closed all
round across a slit too, capped by fans from the middle of the fixed points and of the
ends, wound outward at rest; the session log says its faces and that it is closed. Each
draw it is posed with the model's skin, each corner `inset` inside the drawn controls
along its side faces' normals (default 0, on them; negative: outside; at most 5 cm
either way), and sent as not-drawn polygons (gfx/g3d.h `khdays_g3d_clip_polygon`,
docs/RENDERING.md "Clip volumes"). The floor inside the hem is left out too.

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
  slot 644                  # only while that KH2 mset slot plays it (motions.txt); several, ranges: slot 3 10-12
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
animations does not pop. The envelope eases in only from a `time` range that
starts after the animation does: one from 0 (or none) is not eased again each
time the animation starts over, which the game's looping animations do (its run
every 0.6 s); turning on is covered by the easing toward what the animations ask.
Eased at every start, a coat guided by its KH2 motion (`own_motions`) fell to
0.44 of its weight and back over 0.15 s at each loop, sagging toward the plain
cloth (replay `--days`, Mickey's run, `KHDAYS_REPLAY_CONTROLS`). Settings are blended toward its values by that weight;
mode by authority; its wind replaces the group's by weight.

**Tuning the KH2 coats** (a pass over every coat model, replay `--days` with the KH2 slots,
2026-11-08; each character's file says what it changed and why):
- demyx_h is left out of all of it: set up again as before the pass, he looked better (user).
- Foot and heel capsules inside an ankle-length hem threw the coat's front panels across the shin
  (Mickey: 37 cm a frame, the coat stretched 636% at rest): they carry tag `feet`, which the coat does
  not collide with, where that measured better (Mickey, Zexion, Xigbar, Saix, Xemnas, Xaldin, Riku;
  not Axel, Larxene, Marluxia or Roxas, whose coats end higher or whose runs got worse).
- The game's own animations with no KH2 motion (attacks, guard, magic) fling a coat on full world inertia
  in spins: a `days_moves` control over them (inertia 0.6, follow 8 Hz, hem 3) kept the legs inside in
  Saix's, Xemnas's, Xaldin's and Riku's spins (legs out 15-25 cm before).
- Drawstrings: `damping 3.5`, `max_speed 5`; colliding with the arms too where the strings do not rest
  inside the torso collider (not Roxas's or Xemnas's).
- Thighs thinned to 0.9 front to back (`mesh_scale`) and the pelvis hidden only where the coat closes all
  round (`hide kh2_2 from <coat waist> to <hip joints or the slit's top>`); the replay does not draw those.
- Left open: the drawn-face guard (`draw_guard`, on or off) causes most of the remaining pops in the game's
  animations (off: jitter halves, legs show 9-21 cm through); Lexaeus's `B002`, roxas_bf_h's
  `b_bf_B000` and Xemnas's guard still open or jump the coat. The replay's numbers also depend on which
  scenarios ran before in one process (each scenario does not start a fresh actor).
- Second round (same day): a coat whose KH2 motion flares it (Xaldin's hover, hem radius 54.5 cm) hangs in the
  game's animations round its KH2 rest, a narrow tube (30.5 cm), pulled tight over the game's wide stance: a
  `ds_pose <slot> <coat bones>` (the motion's first frame) and a guided control over the game's animations give it
  the motion's shape (his idle: off its targets 19.9 -> 5.1 cm, the coat's jitter summed over 28 animations about
  halved; not his `B000`, whose kick crosses the bell's front). One round pelvis capsule's end stood the back of
  Riku's coat out behind his seat where the hips tip back (the glides, `Ca000` and `Ca001`): two side by side
  (`offset 0 0 +-5`, flatter front to back) and a loose control for the held glides (`follow_frequency 2 hem 0.7`,
  the back strips not following the raised thighs) brought its back rows from 33-47 to 27-36 cm behind the spine
  line in the hover, 32-46 to 26-33 forward (his seat 16-20). Lexaeus's legs through his coat in the game's attacks: a swing cone turned a panel
  the leg had pushed out straight back into it; `days_moves` with `swing_limit 180` and `row_max_strain 0.3 hem
  4` took the frames with a leg out (10 runs a move) from 416 to 72; his `B006` and the roll still let a thigh
  raised over the hip through the top rows for a frame or two. The glides' base of the spine: the pelvis turned
  under (KH2_MODELS.md, `ds_pose`); with the coat colliding with his feet again and capsules along the soles to
  the toes (the ankle capsules run down from the ankle), the feet no longer come through it there (5.7 cm before).

**Wind**: `wind world|actor|bone <bone> <x y z> speed <m/s> [gust <f>]
[frequency <Hz>] [turbulence <f>]` or `wind off` (global, per group, per control).
Gusts and turbulence are smooth sums of sines with a per-actor phase; the air drags
each control by its velocity relative to the air (`wind_drag`).

## Solver

XPBD (Macklin et al. 2016) on contiguous arrays sized at load. A tick allocates
little: the room's triangle buffer until it has its working size, the controls'
order, and the copies of the game's places an actor that has them takes. A tick
(`simulation rate`, 60 Hz in the shipped files):

1. The room's triangles near the actor gathered once.
2. Per substep (`substeps`, 3 in the shipped files): each needed bone's frame interpolated between the tick's
   two poses (positions linearly, rotations nlerp); the colliders and anchors
   placed; each control's target computed; its velocity integrated relative to
   its target's: the target's acceleration not felt scaled by `1 - inertia`,
   damping `exp(-damping h)`, gravity, the air (`wind_drag`), and `max_speed`.
3. Per iteration (`iterations`), multipliers reset each substep: links (compliance / h²),
   the pull toward the target (XPBD with damping: compliance 1/(m w^2), gamma =
   2 zeta / (w h)), tethers, `max_distance`, swing cones, strain limits; then
   contact: body colliders (controls each iteration; edges and each coarse face,
   exactly, on the substep's last), the room, self-contact (last iteration);
   then anchors re-projected (the body attachment wins over contact).
4. Twice more: strain limits, control contacts, anchors (a push spreads to the
   neighbours instead of tearing one control off them).
5. Velocities from positions. A group with a non-finite particle, or one 4 m
   from its target, starts again on its targets (counted as a recovery).

Contact: a control crossing a capsule's core in one substep is pushed back to the
side it came from; an edge contact moves no end more than twice the overlap (an end
near a fixed anchor would otherwise be thrown), a face contact moves all its corners
alike, past the core's end when pierced, at most 50 cm. Friction removes
tangential motion relative to the collider's surface up to `friction` x the
overlap. A one-sided room face pushes back to its front a control that crossed it
this substep, sits less than 5 cm behind it, or belongs in front (a floor is solid
below; a wall where the target is in front), up to 30 cm deep; deeper is left (a
wall's other face). A floor over the group's reference bone (the hips, for the
coat: a ramp's or a platform's underside) only keeps cloth from crossing it
downward: pushed up through one 2.5 m over Roxas's head, the coat stayed on its top,
stretched up to it (F12's marks: a 28 degree floor pushing it 1.5 m in a tick).
Self-contact: a control against the faces of its
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
together or cross. A bone with no swing limit (`swing_limit` 180, the keychain
preset) is not eased back when folded: it may hang anywhere about its reference,
opposite it too, and a chain's first bone turns from where it pointed at its last
rebuild, not from its reference (the turn opposite has no clear axis); the bones
under it turn from their parent's, never far. Eased back, the Kingdom Key's first
link pointed up, away from the chain, and jittered whenever the blade was held tip
down (Days' glide hover); the test `free_chain_upside_down` turns a blade 180
degrees under such a chain.

**Drawn clipping in a run or a jump.** The solver kept the legs out of the coat,
but the drawn shape trailed it (`smoothing`) and a calf or knee swinging through
it cut the lagging faces: the replay's "legs in coat ... drawn" count, at 30
draws a second, was run 127 of 211 frames (20 cm deep), attack 62, spawn 60,
jump 37. A drawn face deeper in a body collider than its solved face is now moved
toward the solved one as far as it takes to come out (`guard_drawn_faces`); none
of those frames are left. Moving it all the way at once, and easing a face toward
the solved one as it comes within 4 cm, both brought the twitch back (the idles
rest that close to the thighs). With `smoothing 0.08`, drawn jitter p99: battle
idle 5.1 -> 4.3 cm, calm 1.7 -> 1.4; run 36 -> 50 cm (its turns 143 -> 124 deg),
attack 14 -> 27, guard 13 -> 19: in the fast moves the coat is pushed along by
the leg instead of being passed through.

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
and the actor starts afresh, as after `idle_reset` unseen. A jump of every
group's reference bone past `teleport` within a tick (the character moved or
turned by the game) rebases the state rigidly (shape kept, relative velocity
zero) and skips the tick. When only some jump (an arm snapping to an animation
with no blend carries a chain on it), those groups alone are rebased, their bones
(the reference bone, those under it, the colliders they touch) start the ticks
in the new pose, and the rest is simulated through the draw (`Actor::update`):
rebasing everything for it skipped the coat's tick, and going from the calm
idle to the guard (no blend; the replay with an instant 60 degree turn) the legs
jumped through the coat, 13-20 cm deep on that frame, a leg left outside it in
game. A pose cut the model says it made (`ActorInput::pose_cut`: a KH2 motion taking
the whole pose with no fade) in which a body collider's end the cloth touches moves
more than 12 cm about the first group's reference bone starts the actor afresh
(`kPoseCutJump`): carried on, the coat kept the side of a leg it came from, the wrong
one after the jump (KH2's aerial damage cut in from an attack: a foot 92 cm in one
draw, the leg out through the coat); started, the legs sweep in from rest and drape
it round them. A start places every control on its target at the
first pose, still relative to it (a moving start moves with it), relaxes contacts
`first_pose_iterations` times, then lets the groups that hang (`settle on`: the
drawstrings, the hood) settle in that pose: 40 ticks with the pose held, one
substep each, each control keeping 0.75 of its velocity per tick
(`Simulation::settle`; the other groups are put back as they were, then the first
tick takes up the body's velocity). They start in their rest shape on their bones,
and a bent first pose (a cutscene's) flung them.

A camera cut is a new shot: cutscenes re-stage their actors across one (a restarted
animation, a step aside, a turn), and under `teleport`'s limits the cloth took that
for a move made in one frame. The draw compares the flushed camera with the last
draw's, as the actor sees it: the camera turning more than 25 degrees, or its place
from the actor's root swinging round it more than 25 degrees or coming nearer or going
further by more than 0.75 m (the model's units), is a cut (`model_link.cpp`
`kCutDistance`, `kCutDegrees`). Judged by its jump in the world (0.75 m), the field
camera swinging round Roxas as he ran (the stick held sideways, the camera turned the
other way) moved 0.76-0.85 m a game frame (6.3 degrees round him, his run 18-26 cm),
so every draw was a cut: the cloth restarted and settled mid-run, clipping into the
legs (114 cuts in a traced session). A cut: the state is rebased as for a
teleport and the hanging groups settled in the new pose. In the replay's `cut`
scenario (the battle idle restarted, the actor moved 45 cm and 30 degrees
turned) a drawstring tip moved 30 cm in the frame after the cut and 2.6 cm a frame
on average over the next second; with it, 4.3 cm (the restart itself) and 0.26 cm.
A battle-idle start: 3.5 cm at most in its first second, now 0.5. A cut costs one
update about 9 ms longer. `KHDAYS_SECONDARY_TRACE` prints each draw's camera jump (in the world and round the actor)
and marks a cut.

Actors are the drawn render objects, each with its own state; an actor unseen for
`idle_reset` plus a second is dropped (no table is ever cleared wholesale).

## In the game

`models.cpp` calls `apply_model_secondary` in `draw` after `play_kh2` (or the
game's pose) and before the weapon attachments and skinning. Each needed bone's
frame is `world_of_view x skin x inverse(inverse bind)` divided by the actor's
world units per model unit (from the root node's matrix): the solver's space has
the world's axes (y up) and the model's metres (KH2 centimetres / 100, Days' world
about a metre a unit). Rebuilt bones go back the same way into `skin`. That scale
is the one the actor started with (a change past 2 % starts a new actor), not each
draw's: it divides the whole world position, and a root's fixed-point rounding
wobbles it. The Kingdom Key's root is the game's hand matrix, whose scale wobbled
by up to 0.09 % a draw: 40 m from the map's origin its keychain's anchor moved
1.5 cm a draw in the world while the blade drawn stood still (each axis's error the
same share of its coordinate, the trace's `at` positions), 6 m out 3 mm; the chain
shook on the hill and not at its foot.

`world_of_view` takes the camera out without the camera's own matrix:

- The model's place without the camera, `P`: a field actor's world matrix (actor
  +0x10c, which `Ov002_SubmitActorDraw` multiplies onto the view; that function
  draws attached parts, the weapons), or else the base transform the 3D engine's
  flush sent last (`NNS_G3dGlb` +0x94 rotation, +0xb8 translation, +0xc4 scale,
  fx32; `khdays_g3d_base`, hooked in `NNS_G3dGlbFlush` and `NNS_G3dGlbFlushP`).
  `Obj_RenderModel` and other draws set it before `NNS_G3dDraw`. The base is used
  only when the draw starts from it: camera x base for `NNS_G3dGlbFlush` (which
  multiplies both into the position matrix), identity for `NNS_G3dGlbFlushP`
  (which folds both into the projection).
- `world_of_view = P x R x inverse(root)`, where `R = inverse(start) x root` is the
  root node in `P`'s space. `R` is the game's own node matrix, but read back from
  the GE's fixed-point (1/4096) products with the camera, so each camera turn
  changes its rounding. `R` is therefore held while it differs from the held one
  by less than 1 cm and 1 degree (Days' `roxas_R` never moves). Otherwise the body
  shook in the world by a fraction of a millimetre whenever the camera turned, and
  hair and drawstrings swung with it.
- A draw starting from neither (a weapon, from the hand's saved matrix): the view's
  way to the world as a draw placed by one of the two above found it in the same
  frame under the same flushed camera (said once). The weapon's own scene node can
  pass for an actor's matrix that its draw does not start from; taken, the view
  leaked into the world and turning the camera swung the Kingdom Key's keychain.
  An actor's matrix is only taken as the start's (`fx_close`) when no such frame's
  view is at hand.
- Neither, and no other draw this frame: the latest flush's camera
  (`khdays_g3d_camera`), said once in the session log. Every session log until 2026-10-30 shows Roxas placed this way
  ("placed in the world by the flushed camera"): his body is not an
  `Ov002_SubmitActorDraw` draw.

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

Only the player's room is taken, as an actor's own ground casts take only its
room's group (`Actor_ResolveGround`: `GetTrackEntryBase` of its link's +0x46):
the field's link context (`data_ov002_0207fa10`, overlay 2) holds the current slot
(+0xd) and each of its four slots' room (+0x2f), and a slot's collision is the
model group of its number (`Ov002_LoadPeerIntoSlot`: `Entity_LoadAndAttach(slot)`).
Every loaded group was taken before, and Agrabah keeps a neighbouring room's
collision loaded over the one Roxas stands in: room 0's 1.2 m sand mound lay under
him in an empty plaza and threw his coat up (F12's marks). Without overlay 2, or
with a slot not loaded, every group is taken. Assumed: the current slot is the
player's link's group (its writer is not in the decompilation's C); in game the
plaza's flares stopped with it. F12's lines name the room and group taken.

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

`tools/hd/kh2_models/graphics/ba/ch/ro/def_hb.p.z/mdl/roxas_b_h.secondary.txt`, beside
`roxas_b_h.mdlx` (byte for byte N_EX830) in the
HD pack; its motions stay `roxas_b_h.motions.txt`'s (P_EX110's, retargeted).

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
- Cuffs 118-128 / 235-245: not simulated, fitted round the hands by bones.txt's `cuff`.
- Hair: single-bone tufts under 170, guided (`kh2_hair`). P_EX110's motions key
  the hair's sway (its tufts 116-159 are N_EX830's 172-215 less 56; motions.txt
  maps them): 2-3 degrees in the calm idle at 0.3 Hz, up to 19 rms in the battle
  idle at 0.4-0.6 Hz, 3-19 in the run at 2.8-3.8 Hz, 15-45 rms in attacks; the
  back cards 3-23. A damped spring driven by the head's acceleration fits none of
  it (R^2 about 0.1): it is keyed by hand, in time with each motion. N_EX830_MEMO's
  one idle sways the front tufts 1-4.5 degrees and keys no back cards (not used).
  So the hair follows the keyed sway closely: 6 Hz at 0.7 of critical damping
  (flat to about 3 Hz, no resonance), `damping 3`, 15 degree swing (never reached),
  `guided_inertia 0.2`. A stop adds 0.8-1.4 cm of follow-through, settled in
  0.2-0.4 s (replay `run`, tufts 172 189 205); an attack takes it 0.4-0.5 cm rms off
  the keyed sway. Slower springs (3-4.5 Hz) lagged the keyed sway and wobbled on
  their own (jelly: 0.7-1.0 cm rms, 4-6 reversals a second in an attack); 7 Hz,
  0.8, damping 6 and a 9 degree limit shook against the limit at about 10 Hz after
  a stop (rubbery). The flat cards on the back of the head (181, 183, 185) and the
  tuft they hang on (178) are their own group (`hair_back`, `kh2_hair_card`): 6 Hz
  critically damped, `damping 5`, 6 degrees, `guided_inertia 0.1`, 0.3-0.4 cm rms
  off their keyed motion in an attack; their own settings alone changed little, as
  they ride 178. In the head collider: none over idle, run, attack, jump, guard.
- Hood: one bone (101); it settles onto the back and swings as one piece (folding
  would need more bones than the model has).
- Colliders: pelvis, spine and chest (`torso`: hood, drawstrings), thighs (from
  12 cm down the bone), calves, feet, heels, arms, forearms (to the wrist), hands,
  head. The feet run down from the ankle; the heels (radius 6 cm, 4.3 cm behind
  it) cover the shoe's back, which reaches 8.6 cm behind the ankle.
  The coat collides with the pelvis and legs only.
- Placement: by the 3D engine's base transform, the camera taken out (see "In the
  game").
- Hair: `rotate_only` (each tuft turns about its root, which stays on the head).

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
khdays-secondary-replay roxas_b_h.mdlx roxas_b_h.secondary.txt roxas_b_h.motions.txt --out out
khdays-secondary-replay ... --scenario run --actors 8 --draw-hz 30 --repeat 2
khdays-secondary-replay ... --reference B_EX390.mdlx B_EX390.mset 0,10,140,208
```

Plays the scenarios (idle, run with a 180 degree turn and a dead stop, a run round a
circle turning all the while (`circle`, `KHDAYS_REPLAY_TURN=<degrees a second>`, default 120: the
stick held sideways while the camera turns; the coat Roxas clips no more there than in a straight
run, up to 180 a second), attack with
lunges, jump with landing, guard, launched into KH2's aerial damage front and back
and cut to its landing (`air_front`, `air_back`; `KHDAYS_REPLAY_AIR=<s>` the time in
the air, `KHDAYS_REPLAY_FROM=<slot>` the motion launched out of, no-fade segments
sent as pose cuts unless `KHDAYS_REPLAY_NO_POSE_CUT=1`), wall and corner while turning and backing,
spawn while running, teleport, a camera cut, a platform rising under the coat) on the KH2
motions as the port does (retargeted, matrix cross-fades between segments, root
motion taken out, the actor placed on a path), and writes `report.txt` (clipping
of the skinned simulated vertices beyond their rest overlap with the colliders
their group uses, vertices under the room, coat strain, recoveries, rebases, cost
percentiles, per-phase cost) and a contact sheet per scenario of the proxies
(colliders, coarse faces, links, anchors). It reads motions.txt's `rotate_motions`, `mset ... own`,
`motions` and `carry` lines as the port does; the scenarios name P_EX110's slots,
so for a model that adds P_EX110.mset from a slot on, `--slot-offset <slot>` moves
them there. `--reference`'s slots are seen by the controls' `slot` lines, and a
reference model with this model's bone count drives all of them (else bones 0-94):
the dual wield Roxas (B_EX390 on its own motions) is checked against its own bake
with `--reference roxas_bf_h.mdlx B_EX390.mset 0,1,4,5`.

Options: `--out <dir>` (contact sheets and the report), `--scenario <name>` (idle, calm,
idle_loop, calm_loop, run, circle, attack, jump, guard, air_front, air_back, wall, spawn,
teleport, cut, platform, days, all; default all), `--slot-offset <n>`, `--actors <n>` (simulate n
at once, the benchmark; default 1), `--draw-hz <hz>` (default 60), `--repeat <n>` (draws
each n times), `--reference <B.mdlx> <B.mset> <slot,...>`.

**The game's own animations** (`--days`): the model is posed as the game poses it, by the
port's `models.cpp` itself linked into the tool (`khdays_models_offline_pose`: one draw's pose
up to the cloth, so bones.txt's pairs, `place`, `spine`, `ik`, `ds_pose`, twists and cuffs all
apply; the game's side stubbed in `port/tools/replay_game_stubs.cpp`), and the cloth simulated
on it. Give the model from its mod's folder (`<mods>/<mod>/graphics/<name>.mdlx`, where the port
finds it) and the game model it replaces:

```bash
khdays-secondary-replay xaldin_h.mdlx xaldin_h.secondary.txt xaldin_h.motions.txt --days xa_def_h.nsbmd \
    --days-anim b_xa_A000=xa_def_h.nsbca#0 --days-anim b_xa_A001=xa_def_h.nsbca#1 \
    --days-scenario settle=b_xa_A000:2,b_xa_A001:3:5,b_xa_A000:4 --scenario days
```

`--days <BMD0>` the game model's bytes (its file's BMD0 block); `--days-anim
<name>=<BCA0 file>#<index>` one of its animations by the name motions.txt knows it, played
alone as the scenario `days:<name>` (two loops, 4 s at least, 30 frames a second);
`--days-scenario <name>=<anim>:<seconds>[:<m/s>],...` several one after another, each crossed
from the last by the scenarios' fade (two game animations: models.cpp plays both by their shares, the game's nodes
mixed, as the game cross-fades), moving forward at that speed. `--scenario days` plays
only these. Each game animation goes to models.cpp with its length in frames, which a `follow` section
stretches onto (the boss Saix's `016_004`). An animation motions.txt gives a slot is posed by its KH2 motion there (models.cpp plays it), but
the cloth's controls are not told the slot (`slot` lines do not apply): for such an animation the KH2 scenarios
(`idle`) are the game's case. **Hair** (`--hair <group,...> [--hair-ignore <bones>]`): the report's `hair:` line
for those groups' vertices, how many of their triangles' edges pass through the drawn body's
triangles (the most in a frame, the frames with any, the hair and body bones crossing most:
on thin cloth, a hood, a crossing is what shows), and their twitch, each vertex's
frame-to-frame acceleration in the first group's reference bone's frame (cm; from the
quarter second on). `--hair-ignore` bones' triangles are not the body (other hair, hidden
weapons).

Diagnostics, on stderr:

| Variable | Effect |
|---|---|
| `KHDAYS_REPLAY_DEBUG=<strain>` | the worst links once strain passes it, and every leg in a coarse face with its side; each new deepest simulated vertex past its rest depth in a collider (the report's `worst penetration`), with its bone and the collider |
| `KHDAYS_REPLAY_LEGS=1` | each frame's deepest leg in the coat (cm) |
| `KHDAYS_REPLAY_DUMP=<frame>` | the coat's controls and targets in the hips' space (`KHDAYS_REPLAY_DUMP_WORLD`: in the world) |
| `KHDAYS_REPLAY_REFDUMP=<frame>` | the bake's and the simulated coat there (`--reference`) |
| `KHDAYS_REPLAY_JITTER=1` | each frame's worst drawn control, the legs' largest step and any coat bone turning over 60 deg past its last step |
| `KHDAYS_REPLAY_LEGPOSE=<slot>` | the mset skeleton's legs and this model's after the retarget (knee bends), once |
| `KHDAYS_REPLAY_TRACE=<bone,...>` | those controls each frame (drawn, solved, targets, nearest collider) |
| `KHDAYS_REPLAY_CLIP=<name>` | each frame's deepest simulated vertex in the colliders so named (`KHDAYS_REPLAY_CLIP_BY=<collider>`: of the groups that collide with it) |
| `KHDAYS_REPLAY_POKE=<bone,...>` | those bones' body vertices through the coat's drawn surface |
| `KHDAYS_REPLAY_NO_SIM=1` | draws the animation alone |
| `KHDAYS_REPLAY_ROLL=<degrees>`, `KHDAYS_REPLAY_LIFT=<m>`, `KHDAYS_REPLAY_NOISE=<mm>` | turn the actor about its x, raise it, shake it each draw |
| `KHDAYS_REPLAY_GROUP=<group>` | the group whose bones' turns the jitter measures (coat) |
| `KHDAYS_REPLAY_TWIST=1` | their largest roll about their own length each frame |
| `KHDAYS_REPLAY_SHEET=<frame>,<step>[,<columns>,<size>]` | the contact sheet's frames |
| `KHDAYS_REPLAY_MODEL_DUMP=<frame>` | that frame's drawn model into `--out`: each vertex (m, its heaviest bone) and each bone's frame, to fit colliders to |
| `KHDAYS_REPLAY_SPREAD=<cm>[,<bone>-<bone>...]` | each frame whose farthest drawn vertex from the root bone is past that, with its bone (a mesh stretched off the body); the bones listed left out (hidden weapons) |
| `KHDAYS_REPLAY_DAYS_TRACE=1` | `--days`: each draw, the joint that moved most since the last |
| `KHDAYS_REPLAY_DAYS_BONES=<bone>,...` | `--days`: each draw, those KH2 bones' places (cm, model space): feet off the ground (`lift`), a weapon bone against its hand |
| `KHDAYS_REPLAY_HAIR_PAIRS=<n>` | `--hair`: the report lists that many hair-bone / body-bone pairs (6 by default) |
| `KHDAYS_REPLAY_CONTROLS=1` | each frame, every control's weight and the animations the cloth was given (name, KH2 slot, frame, length, share) |
| (note) | `--days` runs give the cloth what the game does: the KH2 slot and section models.cpp plays for each animation and the game's own looping frame; the plain scenarios give the slot and the time since the segment began |

The test's real-model checks: `KHDAYS_SECONDARY_ASSETS` names the folder the HD pack
installs `roxas_b_h` in (`mods/<Mod>/graphics/ba/ch/ro/def_hb.p.z/mdl`).

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
  waist, hips and legs (pelvis, thighs, calves, feet).
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
- Faces have friction (`face_friction`, 0.9 for the coat). Without it a face
  resting on a knee slid round it freely: halfway through the battle idle (P_EX110
  slot 0, frames 66-104 at 30 Hz) the front-left panel climbed the left knee about
  10 cm in a quarter second while the knee moved 1 cm, held there, then dropped
  back off, and the drawstring hanging in front was caught between it and the chest
  and shook (up to 7 cm a frame). Found with `KHDAYS_REPLAY_TRACE`: switching off
  only the left calf's face contact removed both. Friction is drawn from at most
  1 cm of a face's push (`kFaceFrictionReach`): from all of it, a coat pressed
  between a leg and a wall held in place and clipped. In the replay 0.45 left the
  drawstring shaking and 0.6 to 0.9 all stopped the climb; 0.9 (chosen in game)
  lets the wall scenario's drawn coat into a leg up to 3.2 cm in 13 of 301 frames
  (1.4 cm in 2 without face friction). The drawstrings no longer collide with the
  coat (no `self_collision`) and have no pull toward a shape (`follow_frequency 0`).

## Not yet / residual cases

- A room box's turn sense is unverified; characters' cylinders are not colliders.
- The heels still come through the back hem now and then in a run, a jump and
  against a wall (replay, `KHDAYS_REPLAY_POKE=9,18`, vertices behind the ankle:
  run 2.9 cm in 57 of 421 frames before the heel colliders, 3.1 cm in 36 after;
  jump 25 to 4, spawn 33 to 17). The kick-up meets the back strips (kh2_30, 54,
  78) between controls, where the rendered panels leave the coarse contact
  surface; radius 8 heels pinned the coat at the wall (legs 18 cm in it).
- The pelvis's own vertices are drawn 5 % thinner front to back (`mesh_scale
  kh2_2 1 0.95 1` in the bones file, docs/RENDERING.md): the butt came through
  the coat by up to about 1 cm in nearly every frame but the calm idle's.
- A draw placed by the flushed camera (neither an actor's matrix nor the base
  transform) still feels the camera turn (the GE's rounding).
- B_EX390's authored flares are larger than any body-driven setting produces;
  matching one motion closely needs an authored control (`mode guided` on a
  model whose motions carry coat tracks). The dual wield Roxas does this for
  B_EX390's own slots (`control own_motions`, `slot 0-256`, `mode guided`,
  `follow_frequency 6 hem 3`, `follow_damping 0.8`, `follow_bone_scale 0`): the
  coat's RMS from the bake in slots 0, 1, 4, 5 is 4.3, 4.1, 6.0 and 14.7 cm against
  20.9, 14.1, 14.4 and 34.4 simulated on the hips and thighs.
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
