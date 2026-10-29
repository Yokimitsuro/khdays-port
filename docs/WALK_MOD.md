# The Walk mod

Analog movement below the run threshold moves the player at the walk speed's share of the game's own
(`[enhancements] walk_speed`), and the game draws its run slowed down. With the **Walk** mod the player's
model walks instead, with a walk of the game's own. Everything here runs only when a folder of `mods/` holds
`days-mods/walk.txt` (`port/runtime/mods/installed.h`); without it nothing changes.

## Where the walk comes from

KH 358/2 Days has no field walk, but its event motions do: `e_rox_908a`, `e_xio_908a`, `e_lex_908`,
`e_zex_908`, `e_vex_908`, `e_mar_908`. The mod's folder carries, for the Organization's playable members (Xemnas
and Demyx aside), `animations/walk/<code>.khw` (`<code>` the field folder's: `ro`, `xo`, `ax`...), made from the
player's own copy of the game. The others have none and walk as without the mod, their run slowed down: Xemnas,
whose run is a float, Demyx, and Sora, Donald, Goofy, Mickey and Riku. The clips:

- the six characters above with their own walk, the others with Roxas's retargeted by the Bip01 names (the
  same biped rig), the hips scaled by the leg's length and the feet on the ground of the character's idle;
- played at the pace that keeps the feet from sliding against the character's own run (`b_xx_A001`), then 80%
  of it for the mod's walk speed of 55;
- the body, arms included, is the walk's own, the character walking as the game's event walks him; over it only
  what the game itself shows when the character stands with the weapon out (the idle `b_xx_A000`): the hand
  that holds a weapon takes the idle's wrist, so the weapon is gripped as the game grips it, and the arm swings
  as the walk swings it; a character whose idle carries the weapon in a way a swinging arm could not (Saix's
  claymore, Marluxia's scythe, Xigbar's arrowguns, Axel's chakrams, Lexaeus's tomahawk) holds the whole arm as
  the idle does, with a fifth of the walk's swing. Every weapon joint rides in its hand as the
  idle holds it, the four that are children of the model's root (Axel's, Xigbar's, Larxene's, Vexen's) too;
- Axel's idle holds his chakrams unevenly; walking, his left arm is the idle's, its chakram leaning out at the
  bottom, and his right arm and chakram are its mirror image across the chest;
- Roxas's and Xion's Keyblade, which the idle holds out to the side, points forward and about 30 degrees down on
  every frame: the arm that holds it swings 60% of the walk's swing, and the wrist turns the walk's own hand by
  the least that keeps the blade there. Roxas has two clips: `ro.khw`, his unarmed walk with the walk's own arm
  and hand, and `ro_k.khw` with the Keyblade's. The Keyblade's keychain (node 1 of every Keyblade model in
  `ba/ch/ro/w_.p2`, which Xion's are too, at the pommel, its shapes along the node's -z: back along the blade)
  would point up behind the lowered blade; it hangs from the pommel instead, as a damped pendulum carried by the
  walk's hand (a third of the hand's sway drives it: it stays within a few degrees of straight down). The
  two-Keyblade Roxas (Mission Mode's, `ba/ch/r2`) holds both the same way, the left blade along the right one's
  mirror image, and both keychains hang (his `w_.p2` has one Keyblade model, drawn in both hands);
- Zexion's book, Luxord's cards and Xaldin's lances have no joint in the model: the game draws
  the weapon as a model of its own with the player's base matrix, and puts its place on the player into the
  weapon's own joints (the one under the root; Xaldin's two lances in two), for the motion it plays, which
  under the walk is still the run. Their holding hand takes the idle's wrist, and a second clip
  (`<code>_w.khw`, two joints per holding hand: the hand in the model's space frame by frame, and the hand as
  the idle holds it) lets the port move the weapon to it (below);
- every cycle lasts as long as Roxas's (the event walks' own strides are short), at the pace that keeps the
  feet from sliding at the mod's walk speed of 55;
- the frames each foot is planted (where it is farthest forward), for the footsteps.

## Making the mod

`tools/mods/walk_clips.py` makes the whole mod folder from your own copy of the game (its extraction in
`data/extracted`, as the port reads it): the clips and the switch `days-mods/walk.txt`. It reads the field
models (`ba/ch/<code>/def.p.z`: their skeletons and their idle and run), the event models and walks (`mi/ch`,
`mi/mo`), and writes nothing else; numpy is all it needs (`tools/mods/nitro3d.py` reads the DS's models and
motions).

```powershell
py .\tools\mods\walk_clips.py --data .\data\extracted\<hash>          # mods/Days - Walk
```

The installer's helper runs it into an install: `installer/setup_steps.py build-mods <install> --walk`.

A KHW1 file: `"KHW1"`, the frame count, the joint count and the frames a second (u32, u32, f32), then for each
frame and each of the field model's joints a rotation quaternion and a local offset (7 f32); then, optionally,
`"STEP"`, a count and that many frames (u32), the footsteps, and `"HANG"`, a weapon file's folder (4 bytes,
`ba/ch/<folder>/w_.p2`), a node of its models (u32), the frame count (u32) and that node's rotation on every
frame (4 f32), the keychain.

## In the port

`port/runtime/input/walk.c`:

- `khdays_walk_movement` (`controls_hooks.c`, each frame the game moves the local actor): whether this frame
  walks, and the stride's pace from the speed given.
- `khdays_walk_draw_begin` / `_end` (`prepare.py`'s hook on `Scene_DrawNode`): the local player's model is the
  one drawn; its clip by the model's file (`ba/ch/<code>/`), its weight easing in and out (out of the walk
  only after three frames at full speed; on letting go of the stick the stride slows to a stop over the game's
  own standing). A second clip `<code>_k.khw` may hold the walk with the weapon out, played while the character
  plays an armed set of motions (the set at `Actor+0x2c28` not Roxas's unarmed one at `+0x2c2c`): Roxas's.
  A character without a clip is left to the game: its run slowed and its own footsteps.
- `khdays_walk_send_begin` / `_end` (the hook on `NNSi_G3dFuncSbc_NODEDESC`'s `sendJnt`): each joint the model
  sends is the walk's over the game's result, given back after the send (the result is the motion's cache).
- `khdays_walk_model_draw` / `_end` (from `runtime/mods/models.cpp`, at every model's draw): whether the
  model drawn is the local player's weapon (`ba/ch/<code>/w_.p2`). Then, in the `sendJnt` hook, each joint of
  it the game gives a translation is a place on the player: while the character stands it is where the idle
  holds the weapon, kept; with the walk on it is made over with the same hold against the walk's hand this
  frame (the `_w` clip's), eased by the walk's weight. Without a `<code>_w.khw` nothing is touched. A model
  of the clip's `HANG` file whose last node is the keychain's (a Keyblade, not its swings' traces or hits) has
  that node turned as the clip has it this frame, eased by the walk's weight.
- The footsteps: the game's own come from the run it still plays underneath, slowed, out of time with the
  walk's feet. While the walk has the feet (`khdays_sound_bridge_walk_steps`, `sound_bridge.c`) a footstep the
  game starts (a `FIELD_*FS_*` archive's entry) is not heard, and at each frame the clip's feet plant the walk
  plays the one the game last asked for, the surface's own, its `run01` and `run02` alternated.

With the mod and the walk speed at the port's default (50), the window sets it to 55 once and saves it; any
other value the player chose is kept. The test `khdays-walk` runs on a clip named by `KHDAYS_WALK_CLIP`
(a mod's `animations/walk/ro.khw`) and is skipped without one.
