# The KH2 camera

The **KH2 camera** switch (Settings → Enhancements, under Analog camera; `kh2_camera` in the settings
file, off by default) makes the field's gameplay camera move as Kingdom Hearts II's does. Cutscenes,
scripted cameras and the Select look view stay Days' own. It needs the game's Controls on Type A; with
Type B the camera stays Days'.

The arithmetic is `port/runtime/camera/kh2_camera.c`, on its own and tested by
`port/tests/kh2_camera_test.c`. `kh2_camera_hooks.c` joins it to the game through `prepare.py`'s `HOOKS`.
`KHDAYS_TRACE_KH2_CAMERA=1` prints each frame's yaw, radius, look-at, eye and input to stderr.

## KH2's camera

Verified in the PS3 debug build's `EBOOT.ELF` (`libdk/objcamera.cpp`, `targetcamera.cpp`). KH2's y points
down; below "above" means what it looks like on screen, and heights are as the port computes them, y up.
Angles are radians in the code. A tick is a 60 Hz vsync: `dk::Vsync::vsyncWait` (0x81cc8) sets
`s_frameTime`, the ticks a frame took, and every rate below is multiplied by it. The field updates every other
tick, 30 times a second: `Title::entry000` (0x2a0210) leaves with `setLimit(1)`, and the only other callers of
`setLimit` are the menus (`Tz::MenuCommon::SetOneInter`, undone on leaving) and a debug menu's callback.

### The parameters

`dk::paramTable` (0x7ea0f4; `ObjCamera::setParam` 0x67790), one row by kind of map, picked by
`YS::PLAYER::start_player_camera` (0x47e758) from `AREAINFO::IsIndoor`. The indoor FOV is written by the
file's static initialiser (0x68f0c), as `DEFAULT_INDOOR_FOV`.

| | Outdoor (type 0) | Indoor (type 1) |
|---|---|---|
| Horizontal FOV | 1.5 rad (85.9°) | 60° |
| Vertical FOV | 69.9° | 46.8° |
| Start radius | 420 | 600 |
| Lock-on radius | 380 | 520 |
| Least radius | 250 | 400 |
| Greatest radius | 500 | 700 |
| Height curve | 0.008 | 0.005 |

The FOV is horizontal in a 4:3 frame: `kn::Camera::init` (0xda010) gives 512x416 aspects of 0.8 and
0.8667, so 640x480, and `kn::Projection::setFOV` (0xd9600) takes half the angle's tangent. How the PS3
version then fills 16:9 was not traced. The port keeps the vertical FOV, as its widescreen shows more at the
sides (`RENDERING.md`).

### Following (`ObjCamera::objective`, 0x67de0)

- **Look-at** (`calcAtTarget` 0x67138): 170 above the player's position.
- **Eye** (`calcEyeTarget` 0x66ec0): round the look-at at the radius, across the ground only: x + r sin(yaw),
  z + r cos(yaw). Its height is `look-at - 150 + (r - least)^2 * curve`, so one number sets how far back and
  how high. Outdoors the eye is over the feet by 20 at radius 250, 155.2 at 380, 251.2 at 420, 520 at 500.
  The yaw turns to its target at up to 10° a tick (`HOME_TURN_SPEED`).
- **The stick** (`objective_control` 0x669d8, `objectiveUpDown` 0x668f8): the yaw and its target by 3° a tick
  (`OBJECTIVE_MOVE_SPEED`), the radius by 8 a tick, each times how far the stick is pushed overall, within
  the radius's limits. `YS::HARDPAD::get_stick_value` (0x473da8) takes a dead zone of 60 of 127.5 off each
  axis (`YS::PAD::Init` 0x474304) and keeps the length as `w`, at most 1.
- **Auto** (`auto_camera` 0x66768): when the player moved, the movement stick is on, the look stick is off and
  the movement stick's y (down) is under 0.9, the yaw turns to behind the player, by
  `(|x|+1) * 2 * (|y|+1) * amount * (0.25° + 0.01° * (speed - 8))` a tick (1° sideways at speed 8), and the
  radius goes to the start at 0.5 a tick (`auto_updown` 0x666e8). Manual has none of this:
  `CameraConfig::isManual` (0x8cd10) is the Config's choice, swapped while R2 is held.
- **Reset** (bit 0x20000 in `objective_control`): the yaw's target behind the player, the start radius, the
  eye back on its last target, then the slide; at once when the movement stick is on. No code counts a second
  press: each press puts the eye back on its target, so a press during the slide jumps it there.
- **Springs** (`setCamera` 0x68258, `springDamp` 0x67f98), once an update, not scaled by time:
  `current + d/|d| * rate * (2.5 * (1 - |d|) + 0.5 * (d . (prevTarget - target) * rate) / |d|)` with
  `d = current - target`. The eye's rate is `0.07 + 0.0006 * (speed - 8)`, the look-at's 0.03 more, so at
  speed 8 they close 17.5 % and 25 % of the way each update.
- **speed** is `obj + 0x114`, not how fast the player is going: `YS::PLAYER::_OVR_make_movement` reads
  it as the run speed, beside the walk speed at +0x110, and nothing in the player's code was seen writing it each
  frame. A stat, then, the same standing still.
- **Collision** (`ObjCamera::collision` 0x67900): one line from the look-at to the eye against the map's camera
  mesh (`Octree::intersectLineCamera`), and the eye goes 5 off what it hits along that surface's normal (30 for
  `DangleCamera`). There is no clearance above the floor.

### Locked on (`TargetCamera::update` 0x7dc48, `updateTargetCamera` 0x7d4d0, `control` 0x7cfb8)

- The **look-at** is 30 % of the way from the point 170 above the player to the target. The **eye still goes
  round that point above the player**, not round the look-at, and collision casts from it.
- With the look-at more than 300 from the point 100 above the player, and steeply above it, the look-at stays
  over the player (a target far overhead).
- `checkVisibleObject` (0x7cd64) tests the target and the point 100 above the player against
  x 32..480, y 8..408 of 512x416 (`dk::rectTable` 0x7ea658). For each one below the bottom, the radius falls
  2 a tick; with neither, it goes to the lock-on radius at 1 a tick (`autoLock_updown` 0x66678). With one off
  screen, the yaw's target is the line from the look-at to the player, plus or minus 20°.
- The stick works as when following. The reset (`control`) aims the yaw down the line from the look-at to the
  player, at the lock-on radius.

## In the game

- **Units.** Days' units are KH2's / 100 (the user's measure; Roxas' models agree, `KH2_MODELS.md`). The module
  works in KH2's units throughout and the hooks convert at the edges. That matters beyond the numbers: the
  spring's rest length of 1 is not scale-free.
- **Ticks.** The springs are per update and KH2 updates at 30 Hz, so a Days frame at 30 fps is one update. At 20
  and 60 fps the rate is shared out to keep the same time. The rest takes the frame's ticks as KH2 does.
- **speed.** Taken as 8, the base KH2's formulas subtract, on the assumption that it is Sora's stat. So the eye's
  rate is 0.07 and the chase 1° a tick sideways, whoever plays.
- **Frames.** `Ov002_Camera_UpdateFollow` and `Ov002_TickCamera` mark a frame as the KH2 camera's when
  `khdays_kh2_camera_claims` allows: Type A, camera flag 2 set, none of the flags for a script's camera or a
  pending reset, and mode 0, 3 (a shake) or 8. `Ov002_TickCameraTransition` then runs
  it just before the shake: Days' camera has done all its own work, and it is overwritten, so its flags, its
  shake and its fades go on as ever. A frame not marked is plain Days'. How the camera comes back depends on what
  those frames did (`kh2_camera_takeover`). If Days' camera never ran, nothing moved it: a pause holds every
  object (`gPauseMode` 1), and the camera goes on as it was. If it ran a few frames it only set itself up, as in
  a new field or leaving the camp (`Ov002_InitWorldState`'s `Ov002_Camera_SetAnchor`), and the camera goes
  straight to its place with its own FOV. After a third of a second or more they were seen, as in a cutscene, and
  the springs carry it over from where they ended.
- **Collision** is KH2's rule on the game's casts (`khdays_kh2_camera_collide`), at the two places KH2 casts: the
  wanted eye, then the eye the springs reached. `Ov002_ResolveCameraAgainstCollision` would hold the eye 0xc00
  (75 of KH2's units) off the floor and sweep a sphere that drags it in along the line, which is what cut the
  player off at the knees. `Ov002_CastRayAlongMotion` passes over upright walls when asked for a normal, so
  `Ov002_CastRayForContactPoint` takes those, the eye backing off along the line. The map's ceiling is kept.
  KH2's `BlindCollision` has no counterpart.
- **Config.** Flags at +0x3c: 1 Manual (Config `0x37bf`), 2 slow, 4 fast, 8 Y inverted, 0x10 X inverted, 0x20 Type
  B. There is no switch between Auto and Manual in play: KH2's R2 is target switching on the port's controls, and
  Days' hold of R (camera flag 0x10000000) is not read. Normal is KH2's speed; Slow is 0.8x, Fast 1.25x (the
  port's choice), on the stick and mouse alone. The percentage in Sticks and camera is for Days' camera and does
  not apply: at 150 %, Normal would not be KH2's.
- **Reset.** With lock-on on its own buttons, the player's own R (the right stick's click in the KH2 preset) is the
  reset: `input.c` takes it before the game sees it, because the game's R is the lock-on's and two quick clicks
  would lock on. The R presses the lock-on's sequencer plays raise camera flag 8 too; the hook clears that without
  resetting. Without separate lock-on, R is both, as on the DS, and flag 8 resets. `Ov002_TickLockedCamera` never
  starts. The Select view has the same reset.
- **Select view.** It stays this camera, the same eye and controls, with no chase. Its FOV goes to the indoor row's,
  46.8° vertical (Days' own is 40°), through two easings in a row, about half a second each way.
- **FOV.** Days has one field FOV: a half angle of 0x1555 (30°, so 60° vertical) from `Ov002_InitWorldState`, and
  0xe39 in the look view. No indoor flag was found, so the outdoor row is used everywhere, its FOV for the Select
  view aside. The hook writes the FOV's sine and cosine (words 0 and 1) and, when the game's camera takes over,
  puts back what the angles at +0xa0 and +0xa2 say.

### Left to see in the game

Not verified, each an assumption the numbers rest on:

- The player's position (`entry + 0x48c`) is at the feet, as KH2's is taken to be.
- The run speed stat is 8 for Sora. No code gives the number, only the formulas' base.
- The height of Days' lock-on point (`func_ov022_020865b4`), which KH2's `YS::TARGET::get_camera_pos` gets from a
  collision part.
- The dead zone: a quarter of the snapshot's axes is KH2's only with the port's default 30 %.
