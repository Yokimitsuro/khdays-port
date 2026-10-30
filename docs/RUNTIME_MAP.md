# What the running game does

A map of the game as the native build (`port/`) runs it: which overlays
each scene loads, what the screens' object updates are, what the buttons do
in each menu, and what the data at an address holds. It is for naming and
commenting the decompilation; what its C should say differently is in
[DECOMP_FINDINGS.md](DECOMP_FINDINGS.md).

Only what was seen is here. Each entry says how -- a trace (the environment
variables at the end), the ROM, a save -- and anything not seen is marked
*not identified*. Names are khdays-decomp's at `ea39ead5e`; addresses are
ARM9. Overlay names in parentheses are the decomp's directory names.

## 1. Scenes and their overlays

The scene controller is at `0x0204bda8`: the scene's object (+0), its table
entry (+4), the current id (+8), the pending id (+0xc) and its argument
(+0x10) (`src/calls/func_0202099c.c`). While a scene's overlays load, the
current id reads 0 and the pending id holds the next one.

Seen with `KHDAYS_TRACE_OVERLAYS` (each overlay as `FS_StartOverlay` starts
it, with the scene ids at that moment) over four walks: Mission Mode to a
mission and back to the camp, Story Mode from a new game to day 2's field,
and a day-357 save loaded into the Grey Area and challenge 07.

| Scene | What it is | Overlays started for it |
|---|---|---|
| (boot) | before any scene | ov001 (`ov001_boot`), frame 1; ov028 (`ov028_dsprotect`), frame 2 |
| 1 | logos, title, its menus, the save-file list | ov000 (`ov000_title`), pending 1 |
| 11 | the opening movie (START skips it) | ov012 (`ov012_opening`), ov024 (`ov024_mobiclip`); ov028 just before, still in scene 1 |
| 5 | the day's calendar card (day 255 and day 2 in a new game, day 357 on loading the save) | ov004 (`ov004_calendar`); ov028 once the card is up |
| 2 | the field, story and missions alike | ov002 (`ov002_field`); see below |
| 10 | the monologue after the clock-tower scene (day 255) | ov007 (`ov007_monologue`) |
| 7 | Mission Mode's character select | ov006 (`ov006_mission_mode_select`) |
| 19 | Mission Mode's camp | ov008 (`ov008_camp_menu`), then ov302 (`field/ov302`) once scene 19 is current |
| 6 | a mission's results | ov005 (`ov005_mission_result`) |
| 12 | "Error de conexión. Pulsa A para volver a la pantalla principal." | ov010 (`scenes/ov010`) |

**Overlays unloaded.** An overlay is loaded for the port from `FS_StartOverlay` until
`FS_UnloadOverlayImage` (the end of `FS_UnloadOverlay`, the info's first word its id:
`khdays_overlay_ended`) or another overlay starting over its range. Before 2026-10-21 only the
second counted, and one the game unloaded but left in memory stayed "loaded": the credits' ov011
(`0205ac40-0205e980`) is overlapped by neither the title's ov000 (`0204cac0-0205ac40`) nor the
post-credits save's ov009, so after the credits the one-screen situation stayed "the-end" (it is
decided before the scene) and the port's HD screens (the title, the save lists, the menus) gave way
to the DS's until the field's ov002 started over it (the testers: "when you finish the game, the
HUD returns to its original state"). Traced: on a new game every overlay ends as its scene is
left (ov000 at f1732, ov012 and ov024 at f2154, ov004 at f2372; ov028 starts and ends at once).
An unloaded overlay is no longer *running* (`khdays_overlay_loaded`), but its functions stay its
own until another overlay starts over its range: the game calls into code it has just unloaded.
At mission 61's end (seen, 2026-10-21) ov106 ended and `Ov022_UpdateSubsystems` then called
`0x020b8194` in it; the first version, which also let go of its functions, stopped the game there.

**Resets.** `OS_ResetSystem` (0x02003948) restarts the console, and
`Boot_InitScene` picks the first scene from the word it left at
`0x027ffc20`: 0 (a cold boot) and -2 give scene 1, anything else scene 12.
`Game_RunSceneLoop` passes the wireless state (after the wireless loop,
`Ov105_WH_GetLastError() << 16 | Ov105_WH_GetDisconnectReason() | 0x80000000`); the others
pass -2: `Ov008_MainMenuExit` (Mission Mode's "Principal"),
`Ov006_MissionBootWatchdog`, `Ov006_MissionInitVideoScene`,
`Ov008_MissionInitVideoScene`, `Ov008_MissionScene_Release`, the
`PageTeardown` of ov008, ov009 and ov025, and `Ov010_TeardownWorkArea`. Seen:
booted with 0x80000000, the game shows scene 12; A there resets with
0xfffffffe and the game comes back on the title. The port boots the game
again in its own process, the window staying (`port/runtime/core/start.c`
`reset_boot`, since 2026-10-05; before, the process started itself again):
the game's threads' contexts deleted, the ARM7's services and sound driver
started anew, main RAM, the shared WRAM and the data area cleared, the
overlays unloaded, the alarms and registers reset, the firmware's work done
again with the parameter, then crt0 and `main`. Seen: Mission Mode's
"Principal" -> "¿Salir del juego?" -> yes: reset with 0xfffffffe, the title
and its music; a multiplayer session losing a member: reset with
0x80000000, scene 12, A, reset with 0xfffffffe, the title, and a mission
played after it -- three boots in one process. The `reset` walk
(`port/tools/regress.py`) takes the first path and plays on to the camp
again; the scripted input carries on across the reset.

Inside the field (scene 2), without a scene change:

- On entering it: ov023 (`ov023_event_script`), ov028, ov022
  (`ov022_battle`), ov107 (`ov107_enemy_common`), ov029 (`system/ov029`),
  ov030 (`ov030_player_roxas`); in the story and with the save also ov069
  (`screens/ov069`), right after ov022. ov024 loads when the field plays a
  movie (day 255's clock tower).
- The pause menu's "Retirarse" in a mission: the battle hub leaves the pause
  (`Ov022_StateAdvanceAfterPause` → `Ov022_StateCloseOv106Scene`), the field
  goes `Ov002_FinishSaveStep` → `Ov002_EnterResultScene`, which starts ov027
  (`ov027_game_over`); its updates `Ov027_GameOverFadeIn` →
  `Ov027_PollConfirmAndAdvance` wait for A (the "PULSA A" screen), and then
  scene 6 loads.
- START in the Grey Area (day 357): ov025 (`ov025_camp_menu_2`) and ov302.
  The scene id stays 2.
- An enemy's constructor opens its resource `Ms/<class id, two hex digits>.p`
  (the ROM's `OS_SPrintf` with the id in r2, 43 constructors in ov202-ov301;
  ov286's class id is 0x6a: the first enemy a Halloween Town mission created,
  after its cutscene, in a player's run).
- The Moogle shop: ov026 (`ov026_shop`), reached from an action script
  (`Game_RunActionScript` → `Ov002_ScriptCmd_DispatchStateEnter` →
  `Ov002_DispatchStateEnter`, whose `pMake` is `Ov026_CreateService`; seen in
  a crash report).
- Challenge 07 (day 357), on starting it: ov002 again, ov013 (`field/ov013`),
  ov023, ov028, ov022, ov107, then six overlays in a row, ov108 to ov113,
  each only BSS (0x1e00 bytes, at `0x020cbf40`, `0x020cdd60`, ...; no code),
  then ov114 (`ov114_enemy_shadow`) at `0x020cbf20` -- where ov108 was --
  ov029, ov030 and ov088 (`ov088_player_axel_4`). What ov108-ov113 are for:
  *not identified*.
- Left idle on the title, the game starts its attract movie
  (`Ov000_TickMenuLoop` → `Ov000_FadeOutAndStartMovie`), loading ov012 and
  ov024 without leaving scene 1.

## 2. The screens' object updates

An object's update returns the next one (`Obj_UpdateAll`, `0x02023adc`).
Seen with `KHDAYS_TRACE_UPDATES`, which prints every change:

- **Boot logos to the title menu:** `Ov000_LogoFadeState` →
  `Ov000_LogoFadeState2` → `Ov000_LogoFadeState3` → `Ov000_HandoffState` →
  `Ov000_MenuFadeInState` → `Ov000_TickMenuLoop`.
- **The title menu:** each accepted choice goes
  `Ov000_TickMenuLoop` → `Ov000_TickMenuLevelChange` → `Ov000_TickMenuLoop`
  (the next level of options).
- **Leaving the title's menu, and back** (2026-10-09, with a copy of the
  maintainer's save): NEW GAME goes `Ov000_TickFadeOutToScene` →
  `Ov000_WaitSubMenuResult` while the difficulty screen runs; CONTINUE and
  Mission Mode's SOLO go `Ov000_BootFadeAndSubScreenSetup` →
  `Ov000_TickBootTeardown` while the save list or the character select runs;
  THEATER MODE goes `Ov000_TickFadeThenPublishContext` →
  `Ov000_FinishMoviePlayback` while its list runs (`Ov000_EnterListScene`,
  `Ov000_TickListSceneInput`). Each fade is the lower screen's master
  brightness to black over 16 steps. Back from any of them (B): that state →
  `Ov000_ReentryState` → `Ov000_TickBootFadeIn` (black to lit over 16 steps)
  → `Ov000_TickMenuLoop`. The title's page does not change on leaving: what
  the menu is, is the scene object's update (its word 5, `+0x14`), its
  context the object's word 8 (`+0x20`).
- **Leaving the title for Mission Mode:** `Ov000_TickMenuLoop` →
  `Ov000_BootFadeAndSubScreenSetup` → `Ov000_TickBootTeardown` →
  `Ov000_TickBootFadeTransition` → `Ov000_BootDispatch` → destroyed; the
  save-file list runs meanwhile as another object (`Ov000_TickLoadScene` →
  `Ov000_TickSelectionScene`). `Ov000_TickBootFadeTransition` moves on only
  when stream 1 has stopped (`SoundStrm_HasPlaybackPos(1) == 0`, in its C).
- **Mission Mode's character select (ov006):**
  `Ov006_MissionSelectionSendTick` → `Ov006_MissionMenuOpenTick` →
  `Ov006_UpdateSelectionConfirmationState` →
  `Ov006_SynchronizeMissionEntries` →
  `Ov006_UpdateMissionEntrySynchronization` → `Ov006_GetIdleHandlerGated`;
  `Ov006_MissionBuildOptionRows` → `Ov006_UpdateMissionMemberMenuScreen` →
  `Ov006_MemberMenuNextStateNoOp`.
- **The camp (ov008):** `Ov008_MainMenuTopState` →
  `Ov008_MainMenuTopState_2` → `Ov008_CommitSelectedPage` →
  `Ov008_RouteCommittedPageState` → `Ov008_RefreshAndPickScene` →
  `Ov008_CommitSynchronizedSnapshotState` → destroyed; the lobby alongside
  (`Ov008_MissionLobbyStartTransfer` → `Ov008_MissionLobbyPoll`).
- **Entering a mission's field (ov002):** `Ov002_TickGameplayState` →
  `Ov002_EnterSceneIfValidated` → `Ov002_BeginMissionRun` →
  `Ov002_SessionTick` → `Ov002_RunPendingCallbacks` →
  `Ov002_TryAdvancePhase` → `Ov002_EnterDialogSceneIfAllowed` →
  `Ov002_TeardownGameplayScene` → `Ov002_FinishSaveStep` →
  `Ov002_TickGameplayState`; the event script alongside
  (`Ov023_PollForNextState` → `Ov023_LoadScripts` → `Ov023_SceneRun` →
  `Ov023_BeginClose`), and the battle hub (`Ov022_StateGameplayHub` ⇄
  `Ov022_StateWaitForAction` → `Ov022_StateFinishAction`).

## 3. Input and menus

- **The game's frame count** is `gObjSystem[2]`, which `Obj_UpdateAll`
  adds one to per unpaused pass. On the title the game runs one frame per two
  VBlanks (VBlank 913 was game frame 430; `KHDAYS_TRACE_INPUT`).
- **KEYINPUT** is read once per game frame (the reads kept pace with the game
  frame count, `KHDAYS_TRACE_INPUT`).
- **`gPadPressed`** holds the keys pressed this frame; X is bit 0x400 (read
  there by `Ov025_PollPageInput` with X pressed, 2026-09-29).
- **ov025's camp menu, X on its scrolling list page:** `Ov025_PollPageInput`
  → `Ov025_MenuInputDispatch_06` (bit 0x400) → the page class's hook at
  +0x28, `Ov025_SetupMenuButtons` → `Ov025_ScrollMenuMoveTo(p, p->nSelectedRow,
  1, 0)`. With the day-357 save the page's list had 22 rows. Which list that
  is: *not identified*.

### 3.1 The field's controls

Seen with `KHDAYS_TRACE_CONTROLS`, `KHDAYS_TRACE_PAD` and `KHDAYS_TRACE_INPUT`
on 2026-09-30: a Mission Mode mission from the start of its field (the
`play` regression walk's route), where Config option 0 (`0x37c4`) was 0 and
option 6 (`0x37c6`) 1 (the guest game's state, which `Ov000_HandleLoadConfirm`
copies whole from `/ttl/ttl.p2`, holds 1; the port now starts every new game at 0,
X with the D-pad, the guest's included: `port/tools/prepare.py`),
and challenge 07's field from the player's day-357 Story Mode save, where
both were 0. What only the decompilation's C says, not a trace, is marked
*(C, not traced)*.

- **Frame rate.** Field play sets frame-rate mode 0 (`Ov002_TickGameplayState`
  passes heap+0x8b54 = 0 to `SetGameMode`): one game frame per two VBlanks.
  `KHDAYS_TRACE_PAD` over a Mission Mode run read mode 0 in every game frame,
  on the title, the character select (scene 7), the camp (19) and the field
  (2); modes 1 (20 fps) and 2 (60 fps) were not seen.
- **The actors read the pad a game frame late.** The local player's actor has
  its buttons at +0x18 (pressed) and +0x1a (held), filled from the control
  packet: what `Pad_Sample` read in game frame N is there in frame N+1 (Up
  read in frame 2329 was +0x1a = 0x40 in 2330; B read in 2577, in 2578).
- **Flip Foot** (the Tricky Monkey's status, `+0x2770` = 5): `Ov022_StepChargeSequence` swaps the
  held Up/Down and Left/Right at `+0x1a` (`Ov022_SwapPairedStateFlags`), `Ov022_TranslateCommandToAction`
  makes the heading from them and swaps them back. Analog movement's stick heading turns around when
  the game's points away from it (`khdays_analog_heading_turned`); with the status forced, Right on
  the stick moved Roxas as Right on the D-pad did (`KHDAYS_TRACE_PLAYER`), the other way from without it.
- **How soon the field answers.** On the `play` walk's route (`KHDAYS_TRACE_PLAYER=1`,
  `KHDAYS_TRACE_PAD`, `KHDAYS_TRACE_SOUND`): Right read in game frame 2513 had Roxas moved
  by the VBlank after it, reaching full speed about 8 game frames later (a quarter of a
  second); A read in game frame 2615 started the first sounds 2 game frames later and
  `se_rox_weap_bg_hit` 7 frames later. The game itself answers within a game frame or two;
  the rest is its own pace (30 game frames a second, the walk's ramp, the attack's
  animation). Headless, so the picture's way to the screen is not in these numbers.
- **Heading.** `Ov022_HeadingFromHeldDpad` turned the held D-pad into Up 0, Left
  0x3fff, Down 0x8000, Right 0xbfff, Up+Left 0x2000 (seen; its C gives
  Up+Right 0xe000, Down+Left 0x6000, Down+Right 0xa000). Left and Right are
  one short of a quarter turn: its sum starts from -1. `Ov022_SteerIdleByAim`
  steers to that aim plus `nAngleBias` (+0x478), which followed the camera:
  0xc000 with the camera's yaw at 0xc000, 0x7b00 after the camera was turned
  to 0x7b00. The heading is relative to the camera.
- **Speed.** The step rate (+0x6b4) rose by 0x80 each game frame from 0 to
  the rate, 0x333 for that mission's character (0x35e for Roxas in challenge
  07), and dropped to the rate at once when over it (0x3a6 → 0x333 after a
  landing). The animation speed scale handed to `Ov022_SetAnimSpeed` was
  0x1000 (no slot-rule speed-up). *(C, not traced:)* `Ov022_SetAnimSpeed`
  keeps the scale at +0x458 and puts frame time × scale at +0x2aba, the delta
  `Sequence_UpdateTracks` advances the actor's animation tracks by
  (`Ov022_StepScaledAnimTracks`; Roxas's `Ov030_UpdateMotionController` takes it
  too); `Ov022_ActorSetState` sets it back to 0x1000 on every change of
  state. Moving plays the actor's pre-draw animation 1, standing 0. A walk
  animation: *not identified* (Roxas's set names its animations only by
  number, `b_ro_A000`…, and no code found picks another one to move).
- **Buttons, with option 0 = 0 and option 6 = 1:**
  - A: `Ov002_Hud_ActivatePanelSlot` (the selected command); states 3, then
    14, back to 0 some 20 game frames later.
  - B: `Ov002_AcceptRequestAndNotify` and `Ov002_RequestCrawlSkip`; states 1
    (the jump), 22 about 19 frames later, 0 about 6 after that -- also while
    running, the heading kept.
  - Y: `Ov002_RequestCrawlSkip` only, no change of state (guard and dodge
    were not seen: that character may not have them).
  - X: `Ov002_PanelCursorNext`, which moves the command deck's cursor on
    (its C: `Ov002_PanelAdvanceCursor`). X held with Down moved the
    character.
  - L held: nothing to the camera. L with A: `Ov002_SelectShortcut(0)`,
    a shortcut (its C: L with A, X, Y, B gives 0, 1, 2, 3; with option 0 = 1
    it is L and R held together).
  - R, held or tapped: read by `Ov022_ReadSelectionInput`, with no target in
    reach (activation state 0, selected index -1); nothing to the camera.
- **Camera** (`Ov002_Camera_UpdateFollow`). The yaw target (camera state
  word 0x20) moves by the rate (word 0x2a) each game frame while bit 4 (down)
  or 8 (up) of its input bits is set, swapped by camera flag 0x10 (Config
  `0x37c3`); the pitch (word 0x21) moves by word 0x2b plus |pitch / 0xa000|
  for bits 1 and 2 within its limits, swapped by flag 8 (Config `0x37c2`).
  - The touch screen turns it: a drag from x 128 to 230 set bit 4 every game
    frame; the rate went from 0x300 to 0xa00 as the drag sped up (its C caps
    it at 0xa00, 0xf00 at 20 fps), the target from 0xc000 to 0x7b00, and the
    bits went back to 0 when the stylus lifted. *(C, not traced:)* a vertical
    drag of more than 4 pixels sets bits 1 and 2.
  - Select opens a look view: camera flag 0x2000 came up (traced; its C also
    sets 0x800, which narrows the view, word 0x28 from 0x1555 to 0xe39), and
    the D-pad turned the camera instead of moving Roxas: Left set bit 8 and
    the target rose by the rate (0x180) each frame, Up set bit 2 and the pitch
    fell. B left it: flags 0x1003 (the view widening back) and the pitch back
    at its preset (`Ov002_Camera_LeaveLookView`; its C: Select again or A, L, R,
    X, Y leave it too). It is not a camera reset.
  - *(C, not traced:)* with Config option 0 = 1 (camera flag 0x20, from
    `Ov002_InitWorldState`), R held sets bit 4 and L held bit 8, and L
    pressed twice within 0x400 raises flag 8 (`Ov002_TickLockedCamera`);
    with option 0 = 0, R pressed without L raises flag 8. The game's Config
    page calls this "R: Reset camera / Lock on / Toggle chase cam" (section
    3.2).
  - Running sideways with no camera bits, the game turns the camera itself:
    the target stayed 0x180-0x280 ahead of the yaw while running left (its C:
    once word 0x37 passes 0x333, target = yaw ± 0x300). *(C, not traced:)*
    holding R about 15 frames switches that off or on (flag 0x10000000;
    Config `0x37bf` sets how it starts); with a direction held and no camera
    bits the pitch goes back to the preset height every frame.
  - The rate was 0x300 in the mission's field and 0x180 in challenge 07's.
- **Command deck** (`Ov022_UpdateCommandInput`). With option 6 = 1 ("X Only"),
  X pressed with no HUD panel open calls `Ov002_PanelCursorNext` (traced).
  With option 6 = 0 ("X + D-pad", traced in challenge 07): X pressed alone
  arms it (menu state 1/1, then 1/2 while X is held with no direction); let
  go without a direction, X calls `Ov002_PanelCursorNext`. With X held,
  Down calls `Ov002_PanelCursorNext`, Up `Ov002_PanelCursorPrev`
  (`Ov002_PanelStepCursor`, the cursor back), Left `Ov002_PanelCursorStepLeft`
  and Right `Ov002_PanelCursorStepRight`, each once per press, and Roxas does
  not move; let go after that, X does nothing more (menu 0/2, then 0/0). The
  deck word its C reads for Down and Up (`Ov022_GetRepeatKeys`, a halfword
  at +0x4c of a setup context) is the key repeat: a key's bit on the frame
  it is pressed, then again every 3 to 4 frames once held some 15 frames.
- **Lock-on** (`Ov022_ReadSelectionInput`). Traced in challenge 07 with
  Controls: Type A: with an enemy in reach the game picks a candidate itself
  (selection index 2 or 3, value 1) without locking on, and a single R press
  then locks nothing; R pressed twice within 6 frames calls
  `Ov022_TryLockOn`, the game's lock-on: with a candidate in reach
  selection flag 4 came up (flags 0x22 → 0x26; its C:
  `Ov022_SetSelectionEnabled(1)`, which also plays sound 5), and when the
  candidate went out of reach two frames later the game let it go itself
  (`Ov022_SetSelectionEnabled(0)`); with none in reach nothing locks. While
  selection flag 0x20 is down (flags 0x02, seen for some frames in the
  fight) R presses do nothing to it. *(C, not traced:)* while selection
  flag 8 is up the function returns before it reads the buttons at all.
  The port's lock-on buttons turn into R presses only on the frames it
  goes past both (`khdays_controls_selection_live`), so elsewhere they are
  the DS's R and L: with the first Holo-Mission's tutorial pages up, R1
  did nothing until then (the maintainer's report, 2026-09-30).
  *(C, not traced:)* with a lock, R
  pressed twice within 9 game frames (the repeat counters gain 0x1000 a
  frame, the test is < 0x9000) releases it; one R press asks
  `Ov022_PickSelectorTarget(0x100)` for the next target (then 0x200 until
  none, at most 30) with option 0 = 0; with option 0 = 1 R tapped (let go
  within 9 frames) picks 0x100 and L tapped 0x200, the previous. So on the
  DS the lock is a double press of R, as the Config page's "R: ... Lock on"
  sits beside the single press's camera reset and the long press's chase
  cam.

### 3.2 The Config page

The camp menu (START in the field: Panels, Holo-Missions, Files, Tutorials,
Config, Save, Title Screen) opens the Config page, three tabs switched with
L and R. Seen in each language with `KHDAYS_LANGUAGE` and `KHDAYS_SHOTS` on
2026-09-30 (day-357 save); the red cell is the option's value (RIGHT on
Controls turned Type B red, and so it stayed on opening the page again).
Leaving it with B puts the camp menu's cursor back on Panels.

| Field | en | fr | de | it | es |
|---|---|---|---|---|---|
| (the page) | Config | Configuration | Konfig. | Configura | Preferencias |
| `0x37c4` | Controls: Type A / Type B | Commandes: Type A / Type B | Steuerung: Typ A / Typ B | Comandi: Tipo A / Tipo B | Controles: Tipo A / Tipo B |
| `0x37bf` | Chase Cam: Auto / Manual | Mode caméra: Auto / Manuel | Verf.kamera: Auto / Manuell | Visuale ins.: Auto / Manuale | Cámara seguim.: Automática / Manual |
| `0x37c0` | Cam Speed: Slow / Normal / Fast | Vitesse caméra: Lente / Normale / Rapide | Kamerageschw.: Langs. / Normal / Schnell | Velocità vis.: Lenta / Normale / Veloce | Velocidad cámara: Lenta / Normal / Rápida |
| `0x37c3` | Cam X-Axis: Normal / Inverted | Axe X caméra: Normal / Inversé | Kam. X-Achse: Normal / Seitenverk. | Asse X vis.: Normale / Invertito | Eje X cámara: Normal / Invertido |
| `0x37c2` | Cam Y-Axis: Normal / Inverted | Axe Y caméra: Normal / Inversé | Kam. Y-Achse: Normal / Seitenverk. | Asse Y vis.: Normale / Invertito | Eje Y cámara: Normal / Invertido |
| `0x37c5` | Cursor Position: Revert / Stay | Curseur: Mobile / Statique | Cursor-Position: Zurück / Bleiben | Cursore: Torna / Resta | Posición cursor: Volver / Igual |
| `0x37c6` | Command List: X + ✚ / X Only | Liste commandes: X + ✚ / Seul X | Befehlsliste: X + ✚ / Nur X | Elenco com.: X + ✚ / Solo X | Comandos: X + ✚ / Sólo X |

(X and ✚, the D-pad, are drawn as icons; the third tab holds the combat
partners' behaviour.) The help lines name what the options do; in English,
Controls Type A: "R: Reset camera / Lock on / Toggle chase cam; L: Access
shortcuts"; Type B: "R: Rotate camera / Lock on; L: Rotate or reset camera;
L + R: Access shortcuts"; Chase Cam Auto: "The camera will stay behind you
while you move. Hold R to switch to Manual (control type A only)"; Cam X-Axis
Normal: "Slide right / SELECT & D-pad: Pan camera right"; Cam Y-Axis Normal:
"Slide up / SELECT & D-pad: Tilt camera up"; Command List X + D-pad: "Hold
down X and you can use the D-pad to navigate the list", X Only: "Turn off
list navigation using the D-pad. You can still cycle through commands by
pressing X". The route there is `port/tools/regress_walks/config.input`
(the player's save: title, load, the camp menu, Config).

## 4. Data

| Address | What it holds | Seen with |
|---|---|---|
| `0x0204bda8` | the scene controller (section 1) | every run's frame line |
| `0x0204c058` | the object list: [1] the object being updated, [2] the game's frame count, [3] the first object | `Obj_UpdateAll`'s C; the count against VBlanks in `KHDAYS_TRACE_INPUT` |
| `*0x020b2e7c` | the field's players: 8 entries of 0xc bytes; entry +4 points to a record whose +0x20 is the actor, and the actor's position is at +0x48c (x, y, z, 20.12 fixed point) | `KHDAYS_TRACE_PLAYER`: entry 0 moved as Roxas walked in the Grey Area |
| `0x021cb390`, `0x021cb3dc` | ov025's two tag-tracker tables in the challenge list, 32 nodes each, with an apply at +0x3c and neither getter (+0x48) nor setter (+0x44) | read while the Holomisiones challenge list was up (day 357) |

The save file is a 64 KB raw backup that starts with `KH358DAY` (a player's
day-357 save, which the game loads, and writes to a new slot and reads back).

**The mission worlds' rooms are KINGDOM HEARTS II's maps** (seen
2026-10-18 in the graphics dump, the player's KH2 copy and each room drawn beside
its map). Days kept KH2's coordinates: a room lies on its KH2 map at KH2's units
x 0.01 with the same axes and no move (the Usual Spot: 67% of its floor on the
map's). Twilight Town, Beast's Castle, The World That Never Was and Olympus
Coliseum also kept KH2's numbering: a room's model is named after its map
(`mi/wd/wd_tt#2/mdl/tt_02` from `map/tt02.map`), often its textures too
(`tt02_000` ...), and its id is mostly the map's number (ids from 84 up are
variants and Days's own). Agrabah's and Halloween Town's did not: Days's
`al_02*` streets are KH2's `al00`, its `al_01` (the Cave of Wonders' ring of
rocks) `al07`, its `nm_02`, `nm_03` and `nm_06` graveyard rooms `nm02`; its
palace rooms and water caves, the Coliseum's arenas with blocks and Halloween
Town's crater were remade, and Agrabah's ruins and Halloween Town's square sit
elsewhere than their maps. The 53 pairs, each with its evidence, are
`studio/data/kh2_rooms.txt`; aw, pp, tw and zz are worlds KH2 lacks, and
`dp_00`, `zz_46*`, `zz_53`, `eh_90/91`, `tt_95`, `al_16b` and `eh_3x` are Days's
own rooms.

## 5. How to see it

The native build prints these to stderr when the variable is set:

- `KHDAYS_TRACE_OVERLAYS` -- each overlay as it starts, with the frame and
  the scene ids.
- `KHDAYS_TRACE_VIDEO` -- each place a movie frame is blitted to, once.
- `KHDAYS_TRACE_TIMING` -- every 600 VBlanks, what the game's time did since
  the start (`timing: V vblanks, L logic frames, A alarm interrupts, C tick
  count-ups; F movie flips (hash, vblanks first-last), O outside the line
  window; accesses: timer reads, timer writes, DISPSTAT, VCOUNT`), and each
  game instruction that touches the timers' registers, DISPSTAT or VCOUNT,
  named once (section 10).
- `KHDAYS_FRAME_LOG=1` -- a line per picture the window draws, in
  `saves/logs/frames-<date>-<time>.log`: the game's time (ms), the newest
  logic frame, where the picture falls between the two latest (`alpha`), the
  polygons placed between of all, `between`/`newest`/`cut`, the two logic
  frames' VBlank times, and the camera's translation and turn between them
  (section 7, "The picture at the monitor's rate").
- `KHDAYS_TRACE_UPDATES` -- each object update's change to the next.
- `KHDAYS_TRACE_CAPTURE` -- each screen blend's frame as the window draws it:
  the bank shown from its copy, or at the DS's size and why, and the capture
  taken again (section 7, "The screen blends at the port's resolution").
- `KHDAYS_TRACE_INPUT` -- each step of a `KHDAYS_INPUT` script, with the
  VBlank, the game's frame count and the pad reads so far.
- `KHDAYS_TRACE_PLAYER[=N]` -- the field players' positions every N frames (30 if no number is given).
- `KHDAYS_TRACE_SCRIPT` -- each step of the action-script interpreter.
- `KHDAYS_TRACE_SCENES` -- the field's event scenes (section 21): the field's record and script,
  the scene controller's pending scene, the event's status, each skip target set and cleared, the
  game's movies (`port/runtime/movies/scene_trace.c`).
- `KHDAYS_TEST_DAY=<day>` -- the story's day set at the first field's construction (game-state
  field 0): a new game's first field runs that day's script of mission 10000 (section 21).
- `KHDAYS_TEST_EVENT=<mission>/<script>` -- the first field built from that mission and script.
- `KHDAYS_TEST_SKIP_AT=<frames>` -- that many frames after a scene's stretch begins, the port asks
  the game's own skip, as the event pause's Skip does (section 21).
- `KHDAYS_TEST_SCENE=<slot>:<entry>` -- the event's first scene path (`4:3` on day 8) written over
  with this one (`4:6`: EV_S#6), so that scene plays in its own place; the port leaves 30 frames after
  the event closes (section 21).
- `KHDAYS_SCENE_SHOTS=<dir>` -- while a scene of the field's event runs, both screens every 30 frames
  as `<dir>/<scene>_<frame>.bmp` (EV_S_4_02400.bmp), headless only (the window's composition draws
  without the frames these are made from); a day's monologue as `mnl_<record>_<frame>.bmp`.
- `KHDAYS_SCENE_STATE=<file>` -- the event machine's state (0x2000 bytes) written at each event's
  close as `<file>.<n>`, to compare a skip by the HD course with the DS's.
- `KHDAYS_TEST_SCENE_START=<ms>` -- START pressed that many milliseconds into each HD scene's videos
  (the scripted input cannot: the game, held for them, reads none); the game's pause then opens over
  the paused video and the script's presses choose in it.
- `KHDAYS_TRACE_PAD` -- every game frame's buttons: what the port gave
  `Pad_Sample` and what it made of them (held, pressed).
- `KHDAYS_TRACE_CONTROLS` -- what the field does with its controls, when it
  changes: the camera's input bits and targets, the lock-on and command-menu
  input and the calls they make, the movement heading and speed, the local
  player's states, the frame-rate mode (`port/runtime/input/controls_hooks.c`).

`KHDAYS_HEADLESS=1` runs without a window; `KHDAYS_INPUT` plays a script of
key presses, touches and stick positions (`port/runtime/input/input.h`).

**No ARM code runs** (since 2026-10-02): the port has no ARM
interpreter. A call into DS code that no compiled function answers stops
the game, on stderr and in the session log:
`khdays-native: a call through a function reference with no native
function: 0xADDRESS (from CALLER, ...): stopped` (`port/runtime/core/window.c`,
exit code 5; earlier `ds_calls.c`, exit code 16). Before,
the interpreter ran it, and `KHDAYS_TRACE_ARM` listed what it ran; on the
regression walks nothing by the end: the movies' decoder and blit are
native (section 9), the ITCM's functions run natively when called by
address (section 7, "Texture matrices"), and `DGTi_Hash2ProcessBlock` (the
SHA-1 the save's load ran) and `Util_QuickSortWithWork` (the ranking sort
of ov003) are C (`port/hal/sdk_dgt.c`, `sdk_util.c`). What the build
still keeps as the ROM's bytes only is listed in
`build/port/gen-x64/rom_code.txt`: MobiClip's routines the game never
reaches -- the audio decoders of kinds 1 and 2
(`Ov024_MobiClip_AudioTransformDecode`, `func_ov024_02087318_unk`: every
movie with sound is kind 3, section 9) and the deblocking filter
(`func_ov024_02092e60_unk`: the one call reaching
`Ov024_MobiClip_BlitFrame`, from `Ov024_MobiClip_FrameAlarm`, passes mode
0) -- and a label inside a routine the HAL has. Seen (2026-10-02): a call
made on purpose through the DS address 0x02092e60 and one through
`absolute.asm`'s symbol (`Ov024_MobiClip_AudioTransformDecode`, 0x02086958,
reached at the image base plus it) each stopped so, the line in the session
log too; the four walks, Debug and Release, reach their scenes without a
stop.

For the sound (section 6):

- `KHDAYS_SOUND=0` -- no sound driver (the command lists taken as run,
  `port/runtime/core/arm7.c`); unset, the native one (`port/sound/snd7_*.c`).
- `KHDAYS_TRACE_SOUND_COMMANDS` -- the commands the ARM9 sends, counted by
  id, and how many lists a VBlank carries, every 600 VBlanks; and for each
  stream the game asks for, the milliseconds until it is prepared and until
  its first sound (`sound: stream ... prepared after N ms, heard after N ms`),
  and for each sequence `StartSeq` starts, how long its loading took
  (`sound: sequence ... loaded and started after N ms`).
- `KHDAYS_FILES` -- unset: the game's ROM reads answered by the port
  (section 8); `card`: through the emulated card, as before, the reference.
- `KHDAYS_LOG` -- the session log, `saves/logs/session-<date>-<time>.log`: on
  in the Debug build unless `KHDAYS_LOG=0`, in Release with `KHDAYS_LOG=1`.
  One line per event, `<seconds> f<frame> <what>`: the scene changes, the
  overlays started, the game's file reads (one line per file and second, with
  bytes and milliseconds), each stream asked for and its delays, each sequence
  started (music, effects or voices; the battle music is one) and each player
  stopped, each movie stream the game opens (`movie stream N opened: <path>`)
  and a mod's movie in its place -- `replaced by <file>`, `its replacement
  stopped`, or why not (a second stream, a file it cannot read) -- and the
  channels its sound takes and releases (`sound: a movie's sound on the
  channels of mask 0x3`), every stretch over 50 ms the game ran without a wait
  (after the reads made in it), a call into DS code with no native function
  (`stop:`), a crash, and F12's marks -- each with the DS's two screens two
  VBlanks later in `saves/marks/mark_<n>_frame_<frame>.bmp` and, for that
  frame, engine A's DISPCNT, BG controls and scrolls, blending and windows
  and the pause's pieces as the pause part reads them (`mark <n>: ...`; the
  frame before it is drawn whole too, so the shot has its 3D). Flushed once a second, on a mark, at
  exit and on a crash. `regress.py` keeps a walk's log beside its own
  (`<walk>.session.log`).
- `KHDAYS_AUDIO_WAV=FILE` -- the sound as it goes out, also written to a WAV
  file, kept valid as it grows: the DS's mixer's 16-bit stereo at 32,728 Hz
  (`ds_exact = on`), or the PC's 32-bit float stereo at the device's rate.
- `KHDAYS_STREAM_CHECK=1` -- each sample a channel takes from the port's
  reading of a stream compared with what the game's ring holds, and every
  600 VBlanks how many differed and why (section 6).
- `KHDAYS_TRACE_SOUND` -- each channel start (its registers), each word to
  and from the ARM9 on the sound's tag, the driver's periodic alarms, and
  each replaced sound as it starts and ends.

For the picture (`port/runtime/display/display.c`, `port/host/render/gpu_present.c`):

- `KHDAYS_GFX` -- whose 2D the window shows: unset, the port's own
  (`port/gfx`, `port/host/render/gfx_render.c`); `reference`, the 2D engines'
  (`port/gpu/gpu2d.c`) with engine A drawn again at N x by
  `gpu_raster3d.c`'s composition, as before the port drew its own graphics; `check`, the port's,
  read back each frame and compared -- at 1x with the 2D engines' (the
  software's 3D on, under both), at N x with the old composition -- every
  300 frames how many pixels differed.
- `KHDAYS_GFX_CHECK=N` -- at one VBlank in N, the port's 2D composed on the
  CPU (`port/gfx/compose_cpu.c`, the code the GPU runs) and compared with
  the 2D engines', even headless; the first differing frames written out as
  BMPs into `KHDAYS_SHOT_DIR`, and every 600 VBlanks a count and the time a
  screen took. At N = 1 on a headless walk the game's thread had no time
  left (it stopped making frames): N of 2 or more.
- `KHDAYS_TRACE_ORIGIN=1` -- where the game's data comes from
  (`port/runtime/core/origin.c`): each file the engine loads (its path, `@hex`
  for a packed entry, into which buffer), and each copy of 32 bytes or more
  into a VRAM bank with the file and offset its bytes came from, or "no
  file" with the first non-zero bytes and the callers.
- `KHDAYS_DUMP_GRAPHICS=DIR` -- each 3D texture the game draws, written the
  first time it is drawn as `DIR/<name>.png` (`port/runtime/mods/textures.c`):
  decoded as the port draws it, with the palette it is drawn with, named
  `<file>[#<entry>]/tex/<texture>[@<palette>]` (`names.cpp`); and with the
  window, each 2D tile sheet the layers and sprites are found drawing, as
  `--dump-graphics` writes it (`port/runtime/mods/tiles2d.c`). With a window
  the video settings' "Dump graphics" does the same into
  `saves/dump/graphics`. `KHDAYS_TRACE_TEXTURES=1` reports each texture's
  name as it is first drawn, or why it has none. With or without a window,
  the colours each 2D sheet is drawn with too
  (`port/runtime/mods/sheet_colours.c`, for the studio): every 8 VBlanks, each
  shown cell of the text and affine layers and each sprite that may show,
  of 4 or 8 bits, its tile traced to a sheet by the bytes' origin (only
  while they are still the sheet's); per tile the colours it was drawn with
  most often -- a palette row's 16, or the 256 of the engine's palette (row
  0) or of the extended palette it takes (its number the row) --, written
  every 300 VBlanks as `DIR/<file>[#<entry>]/chr/<n>.colours`
  (`row <r> <colours, BGR555 in hex>` lines, then `<first>-<last> <row>`;
  a set of colours under the game's row most of its tiles had, the other
  colours a row had for other tiles under rows of their own from 17), what
  the file already held kept for the tiles this session did not see (so
  sessions, and the copies of a multiplayer walk, add up).
  `KHDAYS_TRACE_SHEET_COLOURS=1` reports, per engine and layer, the tiles
  shown and why those not traced were not. Seen on the walks (2026-10-09):
  the field HUD's `UI/btl/es/main.p2#0/chr/0` (no screen, cells or palette
  in its data; its BG3 map built by the game's code) in rows 0, 2, 8 and 9,
  where a single row had been guessed. Its portraits (`UI/btl/main.p2#1`)
  are copied row by row into that sheet by `Ov002_LoadPanelSlots` with
  `MI_CpuCopy8` -- the one SDK copy the origin did not follow, now followed
  for 32 bytes or more -- with the slot's portrait palette copied into the
  slot's row (2 + slot; colour 15 the slot's player colour,
  `data_ov002_0207db8c`): one row, other colours per portrait.
- `khdays-native --dump-graphics [DIR]` -- without the game running, every
  texture of every NitroSystem resource in the ROM (each file's data, each
  "P2" archive entry's) as `DIR/<name>.png` (default `saves/dump/graphics`;
  inside a repository only under its `saves/`), decoded as the port draws
  them, each with the palettes the models' materials and the texture
  pattern animations pair it with, else its `<texture>_pl`; one paired
  with none is written with the first palette under the texture's name
  alone. And every model as `DIR/<file>[#<entry>]/mdl/<model>.gltf`
  (`assets/src/gltf_export.cpp`): a node per bone at its rest matrix under
  its parent, one skin, the meshes in their rest pose with each vertex
  weighted to the bones its matrix is made of, their normals, colours and
  texture coordinates, and the textures the dump wrote beside them. And
  every 2D tile sheet (each NCGR of a file's data) as
  `DIR/<file>[#<entry>]/chr/<n>.png`: an indexed PNG whose pixels are the
  game's palette indices, its tiles in rows of the NCGR's own width when it
  gives one that holds them all, else of 16; shown with the data's first
  NCLR, or greys when it has none (the game's palette for it is then in
  another file).
- `KHDAYS_HD_GRAPHICS=1` -- without a window, the replacements are looked
  for as the video settings' "HD graphics" (on by default) does with one:
  `mods/<Mod>/graphics/<name>.png` drawn instead of the texture of that name,
  or `<file>/tex/<texture>.png` for it with any palette, and
  `mods/<Mod>/graphics/<file>/mdl/<model>.gltf` drawn instead of that model
  (`port/runtime/mods/models.h`). The 2D's sheets need the window (the port's
  own 2D).
- `KHDAYS_TRACE_TILES=1` -- each 2D tile sheet the layers and the sprites
  are found drawing (`port/runtime/mods/tiles2d.c`), named, the first time: its
  tiles, their size and how many a row (the dump's and a replacement's
  layout).
- `KHDAYS_TRACE_MODELS=1` -- each model the game draws, named, the first
  time (and whether a mod replaces it); for a replacement, which node each
  of its joints follows and how far its rest pose is turned. `=2` also
  writes, every 300 draws of a replaced model, each node's turn from its
  rest after the model's first node's, in degrees. With the session log on
  too, a replaced model's whole diagnosis goes into the log: each joint's
  node and turn, each node at rest, the nodes each of its first draws sends,
  and at its 120th and 600th draw every node's matrix sent, every joint's
  matrix (K) and where each mesh falls on the screen -- enough to pose the
  mod's model again offline. A replaced model's line says how many of its
  meshes are drawn translucent: a glTF mesh by its material's alphaMode
  (OPAQUE: alpha ignored; MASK: each texel drawn whole or not by
  alphaCutoff; BLEND: translucent), an FBX mesh when any texel of its
  texture or any vertex colour's alpha is part way. (Taking a whole MASK body as translucent because of
  its soft cut-out edges drew mods/HD Xion Boss's Final Xion seen through
  itself, in shards: 2026-10-09.) The vertex colours' alpha counts as glTF
  has it, per triangle by its corners' mean (the DS's alpha is a
  polygon's): under a MASK's cutoff the triangle is left out, a BLEND
  triangle's polygon alpha is lowered by it, OPAQUE ignores it (an FBX
  whose vertex alphas are all 0 or all 255 is taken as painted without
  alpha). A remade Usual Spot shaded its floor in vertex colours black at
  alpha 0.05: drawn opaque, it was a black hexagon before the curtain
  (2026-10-18). The graphics dump and the studio write a mesh that does
  not say (a DS model's, an FBX's) as BLEND when its texture's texels (A3I5,
  A5I3) or its vertex colours have an alpha of their own, else MASK.
- `KHDAYS_TRACE_PRIZES=1` -- the field's prize pictures as the game loads
  them (their names and table places), a mod's effect for one found or
  refused, each prize taken (its kind, tier and picture) and the player's HP
  and munny each time they change (section 33).

- `KHDAYS_SHOTS=FRAME FRAME ...` -- both screens of those VBlanks as the 2D
  and 3D engines drew them, 256x192 each, as `KHDAYS_SHOT_DIR/frame_NNNNN.bmp`
  (default directory `shots`). Headless, only those frames (and the two
  before them) are drawn at all.
- `KHDAYS_SHOTS_HD=DIR` -- with a window, at the same frames, both screens as
  the window presents them, at the resolution they were drawn:
  `DIR/frame_NNNNN_upper.png` and `_lower.png`.
- `KHDAYS_RECORD_FRAMES=DIR` -- (with `KHDAYS_SHOTS`, only the frames it
  lists) each frame drawn as the frame package the
  game thread hands the host (`port/runtime/display/frame_package.h`: the screens,
  the 3D list, the texture VRAM, the 2D composition records), as
  `DIR/frame_NNNNN.pkg`, the 3D list's polygons and vertices after it, then
  the same polygons before clipping with their keys, then the port's elements
  (each screen's records, what they stand for left out, and each
  image they draw once) (format 13; nothing in it has a fixed size). The packages hold the game's data: record into
  `saves/frames`, which git ignores, never into the repository.
  `khdays-render-test` reads them there.
- `KHDAYS_CHECK_COMPOSITION=1` -- every frame drawn (even headless), and
  engine A composed again from its composition records and the 3D, which must
  give the 2D engine's own picture; a frame that does not is reported, and a
  count every 300 frames.
- `KHDAYS_PROFILE=1` -- every 300 frames drawn, the game thread's time drawing
  the 2D (engine A's share, with its composition records, apart) and the 3D
  (`display:`), and the host's: how many frames the game showed that the
  window never drew, and how long preparing the 3D at a higher internal
  resolution took (`host:`); once a second, how long the game ran between its
  waits and the longest it ran without one (`wait:`), the game's ROM reads the
  port answered and the commands the emulated card still got (`files:`,
  section 8), the movie frames decoded and the time their decode and blit
  took (`video:`, section 9), and how many times the sound's output ran dry, how much it
  held, how long the game's thread spent catching the sound driver up and
  how long the driver itself took, its channels' runs not counted
  (`sound:`).
- `KHDAYS_GPU_DEBUG=1` -- the graphics card's device made with its debug
  layer and validation (when the system has them installed).
- `KHDAYS_TRACE_PXI=1` -- each message between the game and the ARM7's
  services counted by tag and kind, the first of each kind with the game's
  functions behind it, the counts every 3600 VBlanks (section 12).
- `KHDAYS_TEST_IPC=1` -- one read of IPCSYNC at its DS address at start, as
  an access the port does not reach would be: the game stops naming the
  register, the read and the function (exit code 4, section 11).
- `KHDAYS_TEST_DS_ADDRESS=1` -- one read of main RAM at its DS address at
  start, outside the game's memory, as an access the port's side did not
  route through it would be: the game stops naming the address, the read
  and the function (exit code 6, section 13).
- `SDL_GPU_DRIVER=vulkan` (or `direct3d12`) -- SDL's choice of graphics
  backend overridden when the Video page's Graphics API is Automatic (which
  is otherwise Vulkan under Wine or Proton); the port carries the shaders
  for both.
- `KHDAYS_TEST_GPU_LOST=FRAME` -- at that frame the graphics card is taken as
  lost, as when Windows resets its driver (a `DXGI_ERROR_DEVICE_HUNG`): the
  port says so with the last 3D it sent the card (frame, internal resolution,
  polygons, vertices, the busiest tile), in the log and the session log, makes
  its renderer again and carries on. SDL's renderer does not report the loss on
  Direct3D 12, so the port notices it from its own calls' errors; lost twice at
  the same internal resolution above 1, that resolution is refused for the
  session (the 1x picture, said on the settings screen). Seen once on a
  player's RTX 4060 at 2x (2026-10-02, a mission's fight): before this, the
  port then crashed in SDL drawing on the lost device.
- `KHDAYS_RENDER_OUT=DIR` -- `khdays-render-test` also writes each recorded
  frame as the graphics card drew it at 1x, 2x and 4x.
- `KHDAYS_WIDE=COLUMNS` -- without a window, the 3D lists made that many DS
  pixels wide, as the window does in widescreen (342 is 16:9), to record
  and test wide frames headless.
- `KHDAYS_TRACE_2D="FRAME FRAME ..."` -- at those frames, what both 2D
  engines show: DISPCNT and the blending, each BG layer (on, kind, priority,
  scroll), each OBJ on the screen (position, size, tiles, palette,
  priority), and in widescreen the HUD pieces found and where each went.
  Headless, list the frames in `KHDAYS_SHOTS` too, so they are drawn.
- `KHDAYS_TRACE_3D_CALLERS=FRAME` -- the polygons the game sends in the eight
  VBlanks before that frame, by the native call chain that sent them (each
  chain once, with its first polygon and a count at the end), and every
  polygon drawn in screen space (an orthographic projection: w 1) with its
  own chain.
- `KHDAYS_UI=PART=MODE,...` -- the port's presentation (section
  14): for each part (`text`, `hud`, `pause`, `camp`, `save`, `mission`,
  `shop`, or `all`) `off` (the game's drawing), `check` or `on` (the port's
  elements). Under `check` both screens are composed on the CPU every 60th
  frame twice where the checked elements are -- as the game drew them, and
  with what the parts stand for left out and their elements in its place --
  and the first difference per element is said (stderr, the session log),
  with both pictures as BMP files in `KHDAYS_SHOT_DIR` when it is set; a
  count of the frames checked every 60 of them.
- `KHDAYS_TRACE_TEXT=1` -- each of the game's text canvases (section 14):
  made (by which surface, its size and colours), sent by the transfer queue
  (where to), written over (by a transfer, or not), shown (its layer, its
  origin cell) or not on the screen, let go.
- `KHDAYS_TRACE_SITUATION=1` -- each VBlank, the game's data the one-screen
  picture tells the situation from (section 15: the scene, the overlays, the
  pause's mode and kind, the SDK's VRAM banks and reserved timers, the texture
  VRAM manager's regions, three overlays' variables, the display's registers),
  each change on stderr and in the session log; with `[video] screens = one`
  the situation the port told, at each change (`one screen: frame N, field
  [hud minimap ...]`).
- `KHDAYS_WATCH=ADDRESS[,ADDRESS...]` -- the 4 bytes at each DS address
  (hex; up to four, the processor's debug registers) watched for writes on
  the game's thread: each function that writes one said once, with its
  stack. An I/O register's address (0x04000000 on) watches the register
  model's own bytes -- a register that only holds a value, as DISPCNT, which
  no hook sees written.

**Frames that show the same** (with a window): the game runs at 30 frames a
second, so at about every other VBlank nothing the display reads has changed
-- the display registers, VRAMCNT, the palettes, OAM, VRAM, the 3D list and
what the 3D engine drew. Such a VBlank shows the frame before it again, so it
is not drawn again and the window keeps showing that frame
(`port/runtime/display/display.c` `unchanged`, compared byte for byte). At the title
this took the game thread from running flat out (11-41 waits a second at 1x,
2-14 at 4x and 16:9) to 66-80 waits a second at 1x and 67-74 at 4x and 16:9
(there, with the widescreen records made only where the HUD's pieces land). Recording, the composition
check, `KHDAYS_TRACE_2D` and the frames `KHDAYS_SHOTS` names are always
drawn. When the window draws the 3D itself (an internal resolution above 1,
or widescreen), the game thread does not also draw it in software: its 1x
picture, which the window does not show then, is left without the 3D.

**Which screens show the 3D** (observed from recorded frame packages,
2026-09-30; POWCNT1 bit 15 and engine A's DISPCNT bit 3 per frame): the title
(scene 1, the 3D logo), the Mission Mode map (scene 19, the world globe) and
the field (scene 2) all draw the 3D on BG0 of engine A, and engine A is the
upper screen. Scenes not yet looked at this way: character select (7), the
Story Mode day screens (5, 10), results (6).

**Graphics card** (checked 2026-09-30 on the maintainer's machine, Windows 10,
the 32-bit build): SDL gives the port a Direct3D 12 device, and its renderer
runs on it. The device is made on the host's thread, which starts after the
DS's address ranges are claimed (`port/runtime/core/start.c` claims them first
thing in `main`, `khdays_host_start` comes after), so the graphics driver's
libraries load into a process whose DS ranges are already taken, as the
delay-loaded DLLs do. With the device refused (`SDL_GPU_DRIVER=nothing`) the
port falls back to SDL's own renderer (Direct3D 11 there) and says, in the
player's language, that the video settings that need the graphics card are
unavailable; the game plays as before.

**The field's HUD** (traced 2026-09-30 with `KHDAYS_TRACE_2D` and
`KHDAYS_TRACE_3D_CALLERS` in the Mission Mode tutorial's rooms, in the Castle
and in the pause menu):

| Piece | Drawn by | Where on the DS | In widescreen |
|---|---|---|---|
| Command deck (ORDEN, Atacar, Magia, Objetos) | engine A's BG layers (BG1-BG3, above the 3D on BG0; OBJs off) | pixels 0-87 x 136-191 | to the left edge |
| Portrait, HP gauge, player tag | engine A's BG layers | pixels 160-255 x 136-191 | to the right edge |
| Hearts counter (heart, count, line) | the 3D engine: screen-space quads (w 1) sent by `Ov002_SceneDrawPanelWidgets` and `Ov002_SceneDrawPanelTotal`, which `Ov002_SceneStepPanel` calls with the panel's own camera | pixels 0-62 x 1-24 | to the left edge |
| Countdown numbers | the 3D engine, `Ov002_SceneStepPanelHud` (`Ov002_DrawHudNumber` at x 0, the middle) | the middle | stays |
| Markers that follow the game's camera | the 3D engine, `Ov002_DrawMarkerQueue` with the game's camera; the battle's selection and corner markers from ov022 (`Ov022_DrawSelectionMarkers`, `Ov022_PlaceCornerMarkers`), at their targets' positions | where their target is | stay (not the HUD) |
| Pause dialog (PAUSA, Sí/No) | engine A's BG2, the hearts counter's layer | the middle | stays (it crosses the middle) |

The port marks the polygons `Ov002_SceneStepPanel` sends with the panel's
camera (and `Ov002_SceneStepPanelHud`'s) as the HUD's (`port/tools/prepare.py`
HOOKS, `khdays_hud_3d`), and in widescreen moves them and the BG layers'
pieces to the sides (`gpu2d.h` KhdaysGpu2dWide, `host/render/gpu_raster3d.c`):
a piece is the 2D cells or HUD polygons that touch one another; wholly in the
left half it keeps its distance to the left edge, in the right half to the
right edge; across the middle it stays. OBJs never move. Cutscene movies
(bitmap BG and OBJs, no 3D) are not widened at all.

On two screens (2026-10-15, the mission walk at 16:9 and the maintainer's
window at about 3.7:1) the picture is made wider only where the 3D is the
game's world -- the field's screens (its pause, a tutorial's page, the game
over) and the title (`khdays_situation_widens`, from the same game state as
the one-screen decision). In the camps' menus and the world selector the 3D
is a picture inside the game's 2D: wider, the camp menu showed blue beside its
frame and the world selector the end of its stars, and the world selector's
rank ("Leyenda") went to the right edge; they stay at the DS's 256 columns.
The port's own elements a layout line does not place move as the game's
pieces do, in groups whose cells touch (`elements.c`): a mod's pause hand
drawn over its line's left end moved alone to the left edge before.

**The 3D as the first target** (GBATEK, "DS 3D Final 2D Output": blending
with BG0 as the first target "via 3D Alpha-values", probably EVA = A/2 and
EVB = 16 - A/2, not BLDALPHA's): an opaque 3D pixel stays itself. The game
over (ov027) runs with BLDCNT 0x2641 (BG0 the first target; BG1, BG2 and the
backdrop the second) and BLDALPHA 0x1000 (EVA 0, EVB 16): weighing the opaque
3D by EVA 0, the port lost the kneeling Roxas on two screens; the 3D layer
now never takes BLDALPHA's weights (`compose.h`, `gpu2d.c`, `compose.hlsl`).

**Widescreen, what the game sends beside its view**: with the lists made 342
columns wide (16:9) the field sends geometry beyond the DS's 256 columns --
in a room under the tracks 28 of its 591 polygons lie wholly beside the DS's
view, and the room's walls, crates and furniture there are drawn. The game
computes and culls nothing differently, since nothing it reads changes; what
it leaves out of its own drawing when it is outside its 4:3 view (actors, if
it tests them against the camera) is still to be seen with enemies at the
sides.

## 6. Sound

How the port plays the game's sound, and what running it showed
(2026-09-30).

**The two sides.** The ARM9's sound code -- NitroSDK's (`libs/nitro/snd`),
NitroSystem's (`libs/nns/snd`) and the game's own sound manager
(`src/engine/SoundMgr_*`) -- runs as the decomp's C. The ARM7's side is the
sound bridge (`port/runtime/sound/sound_bridge.c`):

- A word the ARM9 sends on the sound's FIFO tag (7) goes to the ARM7's sound
  driver at once, on the game's thread, the driver first brought up to the
  DS clock -- as the ARM7 takes it within microseconds.
- The sound thread (`port/host/app/sound_output.c`) runs the driver and the
  channels up to the clock every millisecond, so the sound keeps its time
  whatever the game's thread is doing.
- The driver's replies wait in a queue that the game's thread moves into the
  FIFO at its event points. A waiting reply wakes the game's thread from its
  wait.
- The game's thread catches the driver up at an event point only when one of
  the driver's alarms is due by then and the sound thread has not run it: the
  shared work the ARM9 reads changes only when the driver does something.

**The driver** is the native one: the ARM7 binary's sound driver written in
C (`port/sound/snd7_*.c`), each function citing the ARM7 address of the
routine it is (the map below), its tables read from the ROM's copy of the
ARM7 binary. It was checked against the binary's own driver, interpreted,
until the ARM interpreter left the port (2026-10-02). **The
channels and the DS's mixer** are `port/sound`'s (`channels.c`, `mix_ds.c`). **The
mixer** heard is the PC's by default (`mix_pc.c`: each channel as it is at
each output sample, at the device's rate, in floats, without the DS's
10-bit rounding or its bias); `ds_exact = on` in the settings file gives
the DS's, at 32,728 Hz. On `khdays-sound-test`'s program the PC's is
within one of the DS's 10-bit steps of it wherever the DS does not clip
(1.000 at most, over 76,171 samples).

**The two drivers agreed** (`KHDAYS_SOUND_CHECK=1` while the port had the
interpreted one, the four walks' scripts, Debug build, 2026-09-30): after
every step -- a word taken, an alarm fired --
the channels' registers and state, the shared work, the sequencer's players
and tracks (their records, byte for byte) and the replies were the same: 0
steps differing in 107,255 (mission), 154,203 (story), 150,203 (play) and
153,128 (challenge 07). What the check found that the listing did not show
was about the check itself: the native driver's copy of the shared work must
start as the ARM9 initialised it (`SNDi_InitSharedWork`: every variable -1)
before sending it, and the checked driver must take each step before the
reference, whose finished tag lets the ARM9 reuse its command lists.

**What the driver costs** (the play walk's script, `KHDAYS_PROFILE`'s
`sound:` lines, the driver's own time without the channels it runs, over
147 seconds): the native driver 0.90 ms a second on average, 1.28 at most,
in the Debug build, and 0.64 and 0.87 in the Release build; the interpreted
one 9.22 and 14.02, and 8.54 and 12.88. With the native one
no ARM7 instruction is interpreted (`KHDAYS_TRACE_ARM` over the mission
script's first minute: no run of the ARM7's, against 66 entries listed with
`KHDAYS_SOUND=emulated`). Moving them there
left the output unchanged, sample for sample (`khdays-sound-test`'s golden
program: FNV-1a 0x3bf6f0a3 over 40,000 samples, before and after).

**Entering the field from the Mission Mode map** (the play walk's script,
Release build, `KHDAYS_PROFILE`, with a window):

- Before (b0f2373, `saves/sound_reference/play.log`): "7 waits, 2997.6 ms
  running, 66.0 ms waiting" in the second before scene 2.
- With the sound thread: "32 waits, 2696.8 ms running, 377.1 ms waiting".
  The load is the game's own work, and still runs about 3 s without waiting.
- Over the 199 s run the output never ran dry. The game's thread spent about
  3 ms a second catching the driver up in play, and about 10 ms a second
  during the load.
- With the native driver, the port's own reading of the streams and the
  PC's mixer (2026-09-30, the same script): "36 waits, 2756.6 ms running,
  421.5 ms waiting" in the second before scene 2; over 198 s the output
  never ran dry, and the game's thread spent 0.91 ms a second on average
  catching the driver up, 3.34 at most.
- The music streams no longer depend on NitroSystem's stream thread getting
  its turn: the port reads them itself (below, "The port's own reading of
  the streams").

**What the ARM9 reads that the ARM7 writes** (the decomp's C, `ea39ead5e`):
the shared work, 0x280 bytes the ARM9 allocates (`sSharedWork`, 0x020447a0)
and hands the ARM7 with `SND_COMMAND_SHARED_WORK` (`SND_CommandInit`); every
reader goes through the pointer `SNDi_SharedWork` (0x02046280), which only
these functions take:

| Offset | What | Read by |
|---|---|---|
| +0x00 | the finished command tag (u32) | `SNDi_GetFinishedCommandTag` -> `SND_RecvCommandReply` (with `SND_COMMAND_BLOCK` it spins, `OS_SpinWait(100)`, until the tag moves), `SND_IsFinishedCommandTag` |
| +0x04 | a bit per sequence player playing (u32) | `SND_GetPlayerStatus` |
| +0x08, +0x0a | a bit per channel, per capture unit (u16 each) | nothing in the game |
| +0x20 + 0x24 * p | player p: 16 local variables (s16), then a word | `SND_GetPlayerLocalVariable` (+0x20 + 0x24 * p + 2 * v) |
| +0x260 | 16 global variables (s16) | nothing in the game |

`SNDi_InitSharedWork` sets it up: the tag, the status words and the global
word 0, every variable -1.

**The commands the game sends** (`KHDAYS_TRACE_SOUND_COMMANDS` over the four
walks' scripts, Debug build, 2026-09-30): a list at most once or twice per
VBlank, and only when there is something to send (mission 151 lists in 8,267
VBlanks, story 54 in 12,573, play 386 in 6,682, the save into challenge 07 930
in 12,760); a few words of 0 besides. By id (`include/nitro/snd.h`):

- used: 1 STOP_SEQ, 2 PREPARE_SEQ, 3 START_PREPARED_SEQ (the game never uses
  0 START_SEQ: it prepares, then starts), 6 PLAYER_PARAM, 7 TRACK_PARAM
  (thousands in the field: 1,012 in play, 2,826 in challenge 07), 9
  ALLOCATABLE_CHANNEL, 10 PLAYER_LOCAL_VAR, 12 START_TIMER, 13 STOP_TIMER, 14
  SETUP_CHANNEL_PCM, 18 SETUP_ALARM, 20 CHANNEL_VOLUME, 21 CHANNEL_PAN, 26
  LOCK_CHANNEL, 27 UNLOCK_CHANNEL, 29 SHARED_WORK (once, at boot), 30-32
  INVALIDATE_SEQ, _BANK, _WAVE;
- not seen: 0, 4 PAUSE_SEQ, 5 SKIP_SEQ, 8 MUTE_TRACK, 11 PLAYER_GLOBAL_VAR,
  15-17 (PSG, noise, capture), 19 CHANNEL_TIMER, 22-25 (surround decay, master
  volume and pan, output selector), 28 STOP_UNLOCKED_CHANNEL, 33
  READ_DRIVER_INFO.

**The ARM7's driver, mapped** (from a listing of
the segment the native driver reads its tables from; ARM7 addresses; each
role from what the code does):

| Address | What | Evidence |
|---|---|---|
| 0x037feef8 | `SND_Init` | the port's entry; creates the driver's thread (0x037ff008) with `OS_CreateThread`, wakes it |
| 0x037ff008 | the driver's thread | its message queue at 0x03807800 (8 entries, 0x037fc6ac); sets up the channels, the sequencer and the alarms (below), enables the sound (0x037fe69c: SOUNDCNT bit 15), output selectors 0 (0x037fe7cc) and master volume 0x7f (0x037fe7bc); a periodic alarm (0x03807820) every 0xaa8 = 2728 ticks of 64 cycles (174,592 cycles, 5.21 ms, handler 0x037fefec); then forever: a message (0x037fc760), and with 1 the periodic work, then 0x037ff1c0, 0x03801f1c, 0x037fff54(periodic), 0x037ff3ac(periodic), 0x03801c8c, 0x037feec4 |
| 0x037fefec | the periodic alarm's handler | sends message 1 to the thread's queue (0x037fc6d4, not blocking) |
| 0x037fefc8 | a wake | sends message 2 to the same queue: the thread runs at once, without the periodic work |
| 0x03801ed8 | the FIFO side's setup | the command list queue at 0x03808e98 (8 entries), the sound tag's receive callback 0x038025cc, the shared work pointer (0x03807d14) cleared |
| 0x038025cc | the sound tag's receive callback | a word of 0x02000000 or more (a command list) goes to the list queue, not waking the thread; a word of 0 wakes it (0x037fefc8) |
| 0x03801ea8 | the ARM9's alarms' handler | sends its argument on tag 7, again while `PXI_SendWordByFifo` fails (the FIFO full) |
| 0x037fc6ac, 0x037fc6d4, 0x037fc760 | the OS's message queue: set up, send, receive | the queue's words (+0x10 buffer, +0x14 size, +0x18 first, +0x1c count), the sleep on +8 and the wakeup |
| 0x037fd3a0 | an alarm set up | clears its handler and +8 |
| 0x03807d18 | 16 channels of 0x54 bytes | 0x037ff164 clears each (+0 its number, +3 flags) and two words at 0x03807cf0 |
| 0x037ff1c0 | per channel, every message | walks the 16 channels by their +3 flags |
| 0x037ff3ac | per channel, with the periodic work | the same 16, their state bits at +3 |
| 0x03808258, 0x03808498 | 16 sequence players of 0x24 bytes, 32 tracks of 0x40 | 0x037ffef0 clears them; their fields in `snd7_seq.c`, laid out as the ARM7's (the ARM9 sets them by offset, `PLAYER_PARAM`, `TRACK_PARAM`) |
| 0x037fff54 | the sequencer's step | with the periodic work, each started, unpaused player's tempo counter (+0x1c) gives a tick for every 240, then grows by tempo (+0x18) x ratio (+0x1a) / 256 (tempo 120: 96 ticks a second); its tracks play the ticks (0x03800cf0), and their number goes to +0x40 + 0x24 x n of the shared work; then every started player's notes take their tracks' settings (0x03800b98); the active players' bits to +4 |
| 0x038000b4 | a player prepared | its tempo, volume and priority reset, a track for the sequence's first commands, and when they begin with 0xfe a track for each other bit of the 16-bit mask after it; its 16 variables -1, its ticks 0; active, not started (0x038002f0 starts it) |
| 0x03800cf0 | a tick of a player's tracks | each track's notes count their length down, then its commands run until one makes it wait: 0x00-0x7f a note (velocity, length), 0x80 rest, 0x81 program, 0x93 another track opened, 0x94 jump, 0x95 call, 0xa0/0xa1 the next command's argument a random number / a variable, 0xa2 only if the last comparison held, 0xb0-0xbd variable arithmetic and comparisons, 0xc0-0xd7 byte settings (pan, volume, transpose, bend, priority, tie, portamento, LFO, envelope, loop start, expression, mute), 0xe0 LFO delay, 0xe1 tempo, 0xe3 sweep pitch, 0xfc loop end, 0xfd return, 0xff end |
| 0x03800290, 0x03800784 | a sequence byte | read through a 16-byte cache (0x03807cfc: its start, its end, the 4 words) |
| 0x03800b98 | a track's settings to its notes | volume from the player's, the track's and its expression through the decibel table (0x03806e84), pitch from the bend and its range, pan, the LFO's settings |
| 0x03801840 | a variable's place | 0-15 the player's own (+0x20 + 0x24 x n), 16 and up global (+0x260), none without the shared work -- where the argument reader (0x038007f0) then returns what its caller held in r5, the argument's type 4 |
| 0x03801948 | a bank program's instrument for a key | types 1-5 directly, 16 a drum set (one per key from its lowest to its highest), 17 a key split (up to 8 ranges) |
| 0x03801b08 | a note on a channel with its instrument | 1 a wave of the bank's archives (+0x18, 8 bytes each), 4 a wave by address, 2 PSG (channels 8-13), 3 noise (14-15); its envelope and pan the instrument's |
| 0x037ff950 | a channel for a note | of those not locked (nor weakly locked, unless the track was given its channels), in the order at 0x03807088, the lowest priority, then the lowest level; none if it outranks the note |
| 0x03808c98 | 8 alarms of 0x40 bytes | 0x03801d24 clears them; 0x03801d50 sets one up (period, handler data) |
| 0x03801f1c | the command lists | a switch over the command's id at +4, `cmp r0,#0x21`: 34 cases, the 34 commands |
| 0x03801c8c | the shared work's status | through the pointer at 0x03807d14: a bit per channel active (0x037feb50) at +8, per capture unit (0x037ff14c) at +0xa |
| 0x037feec4 | a random number | x = x * 0x19660d + 0x3c6ef35f (0x03807404), the upper 16 bits |

Reached from the thread and its handlers: 112 functions, about 4,100
instructions; the largest are 0x03800cf0 (721, reached from the sequencer)
and the command lists (423). The tag (+0) is 0x03801f1c's, one more after
each list; the player status (+4) the sequencer's (0x037fff54, 0x038000b4,
0x03800340). The 0xd6 command (print a variable) does nothing: the ARM7
looks for a flag at 0x03807cf8 that nothing in its driver sets.

**The music streams' ring** (NitroSystem's stream player, the decomp's C in
`libs/nns/snd/calls`, `ea39ead5e`; the game starts streams only through
`NNS_SndArcStrmStart`, which prepares with no callbacks):

- Each of the 4 stream players (`NNS_SND_STRM_PLAYER_NUM`) takes its
  channels from the sound archive's stream-player info
  (`NNS_SndArcStrmSetupPlayer`: `playerInfo->chNoList`) and a buffer of 512
  bytes x 4 per channel (`BLOCK_SIZE`, `BLOCK_NUM`).
- `NNS_SndArcStrmStart` (0x0201cb3c) prepares, then starts once the ring is
  filled. `PrepareStrm` (0x0201ce18) opens the stream's file in the archive,
  its header (`NNSSndStrmData`) becoming `player->info`; the first sample is
  `sampleRate x offset / 1000` (the offset in ms); IMA-ADPCM from a sample
  inside a block decodes that block from its start first (`dirtyFlag`);
  its channels are the file's, 2 when the archive forces stereo, at most the
  player's, and two are panned 0 and 127. ADPCM is decoded by the ARM9 into
  PCM16.
- `NNS_SndStrmSetup` (0x0201aa40): per channel a ring of `chBufLen` =
  2,048 bytes (the buffer split per channel, in multiples of 4 x 32), set up
  as a looped PCM8 or PCM16 channel over the whole ring, its timer
  `info.timer << 5` (the file's timer counts ticks of 64 cycles a sample);
  and an alarm every `timer x samples in the ring / 4` ticks -- each time a
  quarter of the ring has played. The whole ring is filled first (the
  callback with `interval` 1); `NNS_SndStrmStart` (0x0201abe0) then starts
  the channels and the alarm with one `SND_StartTimer`. Seen (`KHDAYS_TRACE_SOUND`, the title): its
  music on channels 6 and 7, PCM8, looped, pans 0 and 127, rings at
  0x0218e980 and 0x0218f180, TMR 0xfe00 (32,728 Hz), LEN 0x200 words.
- Each alarm (`AlarmCallback`, `StrmCallback`) queues the quarter just
  played (`curBuffer`, 0 to 3 in turn) for the stream thread
  (`StrmCallback_2` 0x0201d460, `StrmThread`), which reads the next samples
  into it (`NNSi_SndArcStrm_MakeWaveData` 0x0201d6dc). **When two loads of
  the player are already waiting, the oldest is dropped and its quarter set
  to zero** (`StrmCallback_2`): the silence the game's long loads make.
- `MakeWaveData` writes the samples one after another from the first; after
  the last block (`lastBlockSamples`) a looped stream goes on from
  `loopStart`, one that is not writes zeros, and 4 quarters later
  (`finishCounter`) `NNSi_SndArcStrmMain` stops it. A channel past the
  file's gets zeros (mono) or a copy of the first.
- So, with every load in time, the ring's sample j on the channel's w-th
  pass holds sample `w x R + j` of that sequence (R the ring's samples): the
  quarter played up to alarm m is loaded with what it plays 4 quarters on,
  and each load has 3 quarters (768 PCM16 samples) to be ready.
- What the game reads back: `NNS_SndArcStrmGetCurrentPlayingPos` -- the
  position the loads have reached (`curSample`), in ms
  (`SoundMgr_GetStreamNextPos`, `SoundStrm_HasPlaybackPos`) -- and the
  players' flags; the stop (`ForceStopStrm_2` 0x0201d0e0, through
  `ForceStopStrm` and `ShutdownStrm`) stops the channels and the alarm and
  waits for the ARM7 to have done it.

**The port's own reading of the streams** (`port/sound/stream_reader.c`):

- Where `PrepareStrm` returns TRUE, a hook (`port/tools/prepare.py`) hands
  the bridge the player's channels and rings, the file's header, the first
  sample and where the file is: the sound archive's file in the ROM
  (`player->file.prop.file.top` and `bottom`: 0x0037ce00 to 0x03f817ac,
  62,933,420 bytes, in the player's copy), the stream at
  `player->fileOffset` in it. The first sample is the one `PrepareStrm`
  computed at its start (`tmpU64`): by its end `curSample` has moved on,
  because `NNS_SndStrmSetup`'s first load wakes the stream thread, which
  fills the whole ring before `PrepareStrm` goes on (seen: 2,048 samples on,
  the title's ring).
- A channel that then starts on one of those rings takes its samples from
  the reader, which makes them from the file with `MakeWaveData`'s steps on
  its own state, reading the ROM with its own open files on the sound
  thread. The game's player goes on as before; what it reads back is its own.
- `KHDAYS_STREAM_CHECK=1` compares each sample a heard channel takes with
  the ring's. Over the four walks' scripts (Debug, 2026-09-30): the same in
  every sample while the stream thread keeps up (1.3 million in the title
  and menus); where the stream thread is starved -- an IMA-ADPCM stream at
  52,365 Hz on channels 13 and 14 while its screen loads, in the mission,
  story and play scripts -- about 420,000 samples differ, and all but
  0.2 % are the stream thread's: zeros where the player dropped a quarter
  (`StrmCallback_2`), and the file's samples from quarters of the ring
  before (the player behind by the quarters it dropped). The rest are the
  quarters' edges, where that lag changes.
- The reader's steps on their own: `khdays-sound-test`'s synthetic streams
  (PCM16 stereo, looped, from the start and from inside; PCM8 mono on two
  channels, silent or copied, not looped; IMA-ADPCM from inside a block,
  looped inside a block and inside the last one -- the decoder going on
  from the state the end left, as NitroSystem's does -- and from an odd
  sample), against the sequence worked out sample by sample.

**Music, effects and voices** (for the volumes; NitroSystem's player code in
the decomp's C, `libs/nns/snd/calls`, and the sound archive's SYMB and INFO):

- The archive's players: sequence players 0-10 `PLAYER_BGM`,
  `PLAYER_BGM_EVENT` (music), `PLAYER_SE`, `PLAYER_MAGIC`, `PLAYER_WEAPON`,
  `PLAYER_GIMMICK`, `PLAYER_SE_EVENT` (effects), `PLAYER_VO1`-`4` (voices);
  stream players 0 `PLAYER_STRM_BGM` (Title_BGM_PCM8,
  Staffroll_PASSION_ADPCM, Ending_ADPCM), 1 `PLAYER_STRM`
  (STRM_GAME_START_ADPCM, STRM_MISSION_START_ADPCM, zz80701kr) and 2
  `PLAYER_STRM_V01` (106 voice lines: di0401rk_days, rox_warai_S01, ...).
  zz80701kr is named like the voice lines but sits on `PLAYER_STRM`, so it
  counts as music; what it is has not been heard.
- A sequence's ARM7 player: NitroSystem keeps 16 sequence players
  (`data_0204a320`), each with its ARM7 player number fixed at
  `NNSi_SndPlayerInit`. `NNSi_SndPlayerAllocSeqPlayer` takes one for a
  sequence (`AllocSeqPlayer`) and puts it on the archive player's list
  (`InsertPlayerList`: `seqPlayer->player = &data_0204a760[playerNo]`, the
  player number of the sequence's record, `StartSeq`, or of the sequence
  archive's entry, `StartSeqArc`); `NNSi_SndPlayerStartSeq` (0x0201a568)
  then sends `SND_PrepareSeq(seqPlayer->playerNo, ...)`. A hook there tells
  the bridge which archive player each ARM7 player serves.
- A channel's category: a stream's from its stream player; a note's from its
  ARM7 player -- found from the channel's owner, its track, and the player
  that has that track, in the native driver's records or the ARM7's
  (`KhdaysSnd7Driver.channel_player`); a note its track let go keeps its
  category; a channel that starts with neither is one the game set up
  itself and counts as an effect, said once -- but the movies' (ov024's,
  channels 0 and 1, between `Ov024_MobiClip_StartAudio`'s
  `SND_LockChannel(3, 0)` and `Ov024_MobiClip_ReleaseAudioChannel`'s unlock),
  which are the movies' own category (section 9).
- Seen (`KHDAYS_TRACE_SOUND`, the play script): an ARM7 player serves one
  archive player, then another -- ARM7 player 9 played for `PLAYER_BGM`
  once and for `PLAYER_SE_EVENT` six times -- so the category follows each
  start, not a fixed table. With `music = 0` (the mission script, with a
  window, the DS's mixer to a WAV file), the title's music is silent (every
  second 0 in RMS) and the menu's effects remain.

**Replaced sounds** (`port/runtime/sound/sound_mods.c`, `port/sound/replace.c`;
the files a mod gives under `mods/<Mod>/sounds/`, named as
`port/sound/archive.h` names each sound from the archive's SYMB):

- Found once per identifier through the mods' order
  (`khdays::vfs::find_in_mods`), decoded once on a thread of their own to
  16 bits (WAV PCM 8/16/24/32 or float, FLAC by dr_flac, Ogg by
  stb_vorbis), kept up to 384 MB, the least recently used let go. A file
  that cannot be read is named once with the reason, and the original
  plays.
- The loop: a WAV's `smpl`, else a FLAC or Ogg file's Vorbis comments
  `LOOPSTART`/`LOOPLENGTH`, else the original's loop scaled to the file's
  rate.
- A sequence or an archive entry: `StartSeq`/`StartSeqArc` (hooks at
  0x0201c734, 0x0201c830) name it and the command list that prepares it
  (`seqPlayer->commandTag`); the file starts when the ARM7 has done that
  list (`SND_IsFinishedCommandTag`'s test on the shared work's +0) and the
  player is started, plays under the player's volume (+6 of its record, in
  tenths of a dB: `SND_SetPlayerVolume`, with the archive's volume and the
  fades), and ends with the player. Its channels are left out of the mix.
- A stream: `PrepareStrm` (0x0201ce18) names it; the file follows the
  stream sample its channels play, through their volume and pan.
- A wave: `NNSi_SndArcLoadWaveArc` (0x0201be70) says where each wave
  archive is; a channel that starts (PCM or IMA-ADPCM) on a wave's data --
  SAD the wave's parameters + 12, the wave found in the archive's table as
  the driver finds it (an offset below 0x02000000 from the archive, else an
  address: a wave archive loaded wave by wave) -- takes its samples from
  the file: its index-th since its start, at the channel's pitch, through
  its envelope, volume and pan. Only for wave archives a mod has a folder
  for (`sounds/wave/<archive>/`), looked at once each.
- At the original's rate the file's samples are held as a channel holds
  its own, so an original written out and put back plays the same; at
  another rate they are interpolated (Catmull-Rom).

**The originals written out** (`khdays-native --dump-sounds [DIR [NAME...]]`,
`port/runtime/sound/sound_dump.c`; default `saves/dump/sounds`, and inside a
repository only under its `saves/`):

- Headless: the ROM and the static module's image (decompressed as crt0
  does; the tables the sound reads are in it), nothing of the game run.
- `wave/<archive>/<index>.wav`: each wave of each wave archive, decoded as
  the DS's channel decodes it (`khdays_sound_decode`: PCM8 x 256, PCM16,
  IMA-ADPCM after its header word), mono at the wave's rate, its loop in
  `smpl` from SNDWaveParam (+6 the loop's start, +8 its length, in words).
- `strm/<name>.wav`: each stream once through, made by the port's stream
  reader as NitroSystem's player makes it, at its rate, its loop in `smpl`.
- `seq/<name>.wav`, `seqarc/<archive>/<entry>.wav`: each sequence and each
  entry, played offline (`port/sound/render.c`) by the native driver and
  the PC's mixer at 48,000 Hz. The bank, its four wave archives (assigned as
  `SND_AssignWaveArc` does) and the sequence go to free main RAM
  (0x02200000); the commands are `StartSeq`'s and `StartSeqArc`'s --
  `SND_PrepareSeq`, `SND_SetTrackAllocatableChannel` with the archive
  player's `allocChBitFlag`, the channel priority (player parameter +4),
  then `SND_StartPreparedSeq` -- delivered halfway between two of the
  driver's periodic steps. The player's volume stays at 0 dB (a
  replacement plays under it) and the variables at -1.
- How long a sequence plays: until it ends and its last note has died away
  (at most 30 s after); or, when the sequencer's state at a tick (the
  player's record and each of its tracks' 0x3c bytes: place, wait, calls,
  loops, settings) comes back to one it had, for that loop twice, the
  second time as the file's loop; otherwise 600 s. The tempo's counter
  (+0x1c of the player) is left out: it only says where a tick fell between
  two of the driver's steps, and with it a loop comes back only once it
  falls the same way again -- first tried, TwilightR_B, TwilightR_F and
  DestinysForce found no loop in 600 s. So the file's loop joins within one
  step (5.21 ms).
- The whole archive (Debug build): 8134 files, none failed -- 3845 waves,
  112 streams, 37 sequences (2 of the 39 records are empty) and 4140
  archive entries; of the 4177 played, 2032 loop and 2145 end, none cut at
  600 s; 2.3 GB. The two times round of a loop agree in their 50 ms
  energy (TwilightR_B 0.976, TwilightR_F 0.964, against 0.01-0.21 between
  unrelated parts); sample for sample they correlate 0.79-0.86, the notes
  falling on the driver's steps a little differently each time round, as
  they do on the DS.

## 7. Graphics

What the game asks of the DS's graphics hardware, as found for the port's
own graphics.

**What a frame costs and changes today** (gates G1, G3: the play script,
Release build, with a window, `KHDAYS_PROFILE`, 2026-10-01; per frame drawn,
averaged over 300):

- The software 2D engines: 2.5-3.7 ms (engine A 1.2-1.8 ms of it); the
  software 3D 0.15-5.4 ms (5.4 in the field).
- Writes to graphics registers: engine A's 2D 0-1.1, engine B's 0.2-5.2,
  VRAMCNT and POWCNT 0-2.9 -- the game's 2D touches its registers a handful
  of times a frame. The 3D's (0x060, 0x320-0x6a3, the geometry commands among
  them) 8 on the menus to 7,300 in the field; these counts include the writes
  a DMA makes to the geometry FIFO, which `io_regs.c` takes without a trap.
- One trapped access (a VCOUNT read, 4096 of them timed): 8.68 us -- so the
  2D's trapped writes cost under 0.05 ms a frame.
- 2 KB pages changed since the frame before: VRAM (banks A-I, 328 pages) 0.0
  to 1.6 on average, at most 191 in one frame (a load); palettes 0.00-0.05;
  OAM 0.00-0.33.

The profile prints the pages still; the register writes and the trapped
access's cost it no longer counts -- the game's C reaches the registers
without the trap (section 11).

**Registers within a frame** (gate G2): the game changes none during a
frame. The positive forms searched: an HBlank or VCount interrupt (IE bits
1, 2), and a DMA started at HBlank or with the display (DMA start modes 2,
3). The masks the decomp's C enables with a constant are 1 (VBlank), 8
(timer 0), 0x40000 (IPC receive), 0x200000 (geometry FIFO), the timers' and
the card's (`OS_EnableIrqMask`, `OS_SetIrqFunction`); the SDK sets DMAs
immediately, at VBlank, from the card and to the geometry FIFO
(`MIi_DmaSetParams`' callers); and the port stops on any other DMA start
mode (`port/runtime/core/io_regs.c`, `dma_control_written`), which no walk or
player's run has met. So one set of registers draws a whole frame.

**Display capture** (gate G4, from the C): three users --

- `Gfx_SetupBlendCapture` (engine): the main screen shows VRAM bank C
  directly (`GX_SetGraphicsMode(0xa, ...)`, the VRAM display mode) while
  engine A's picture blended with that bank (A+B, EVA and 16 - EVA) is
  captured back into it -- a feedback blend (a cross-fade or a trail); with
  EVA 16, graphics mode and a plain capture instead. Sub OBJ takes bank D,
  the LCDC bank C.
- `Gfx_SetupSubScreenCapture` (engine): 3D on both screens -- the main
  screen's picture (A, or A+B blended) captured into bank D, the sub screen
  showing BG3 as a 256x256 direct-colour bitmap from bank C; `Gfx_RestoreAfterPause`
  sets the same VRAM display mode of bank D.
- `Ov023_ApplyScreenBlend` / `Ov023_CmdStartScreenBlend` (event scripts): an
  A+B capture into bank D with the scene's blend weight, while the weight is
  not 16; in the dual-3D mode (the global mode's bit 3) the weight goes to the
  dual-3D block instead.

**The port's own 2D** (`port/gfx`): each frame both screens'
display control, layers, sprites, windows and effects are decoded at VBlank
into a frame description (`frame.h`; the windows' vertical flags carried
from frame to frame as the 2D engines carry them), and composed per pixel by
`compose.h`, one piece of code the CPU and the GPU both compile. The frame
package carries the descriptions and the 2D memory by 16 KB page with
counts; the host uploads the pages that changed and composes both screens
in a compute shader at N x, engine A with its 3D from `gpu_raster3d.c`'s
targets (also at 1x: while the window draws the 3D, the software's is off)
and, in widescreen, its HUD's pieces moved to the sides as before. Seen
(Release, 2026-10-01):

- Against the 2D engines, composed on the CPU at one VBlank in 4 over the
  four walks: 0 of 28,804 screens differed.
- On the GPU (Direct3D 12), the play script with a window: at 4x, 4:3 and
  16:9, 0 pixels differed from the old composition in every 300 frames (32
  of them each). At 1x, with the software's 3D under both: 0 pixels differed
  from the 2D engines' in every 300 frames (25 of them). A first 1x run had
  had the software's 3D off under both (the window asked for the 3D), so the
  port showed no 3D at 1x and the check could not see it; the window now
  rasterizes the 3D at 1x too (`gpu_raster3d.c`), and the check keeps the
  software's on.
- The field at 1x, the port's picture (the 3D rasterized on the GPU) against
  the 2D engines' over the software's 3D (`KHDAYS_SHOTS` at VBlanks 9300 and
  10000): 1 pixel of 49,152 differs on the upper screen, by 8 of 255; none
  on the lower.
- Without a usable GPU (`SDL_GPU_DRIVER` set to a driver that does not
  exist) the port says why in a message box and in its log, and does not
  start the game.
- What the 2D costs the game's thread per frame on the play script: 0.04-0.13
  ms (describing both screens and tracking their memory's pages), against
  2.5-3.7 ms for the 2D engines.

In these the captured bank is shown (VRAM display, a bitmap BG); a CPU read
of it is not in their C -- whether any other code reads it: to confirm with
a trace.

**The 3D on both screens** (2026-10-08): the screen engine B shows from a
captured bank was black every other VBlank -- the mission review (scene 3,
ov003: `Ov003_DisplaySetup` calls `Gfx_ToggleCaptureMode`; its 3D 60 times a
second, the screens swapping each VBlank) blinked black, and so did the event
scenes that alternate the 3D (the maintainer). The host keeps engine A's
picture of each package once the game uses the capture (`frame_package.h`
`capture`), at the port's resolution and as wide as it was drawn; when engine
B is on the screen engine A was on the VBlank before, the window shows that
picture there instead (`gfx_render.c` `khdays_gfx_render_captured`) -- what
the DS shows, engine B showing only the capture then
(`Gfx_SetupSubScreenCapture`: BG3 alone). Two lists drawn on different
screens are never drawn between (`gpu_present.c`).
`Ov009_LoadMenuBgWithVariantChars` names a register `REG_DISPCAPCNT` but
writes BG2's scroll (DECOMP_FINDINGS 1.4).

**The port's capture** (2026-10-14, `display.c` `display_capture`): each
capture the game arms is taken at the VBlank of the frame it takes, after
that frame is drawn, so a bank shown and written at once shows the frame
before. Source A: engine A's BG, 3D and OBJ as display mode 1 composes them,
before the master brightness (`khdays_gpu2d_graphics`), or its 3D alone --
the list that frame shows, drawn by the software then when the window draws
the 3D. Source B: the bank of engine A's VRAM block at the read offset.
GBATEK's blend, the sizes and offsets of DISPCAPCNT, and the enable cleared
as the DS clears it (the game never reads it back). A write into a bank
nothing shows (LCDC, not engine A's VRAM display) does not make the next
VBlank a new frame. The session log names each kind of capture once.
Seen on the story walk: the prologue's first camera cut (frame 11082) arms
`c0330010` (`Ov023_CmdStartScreenBlend`: A+B at EVA 16 into bank D) and
engine A shows bank D (DISPCNT `002e0f3c`); its frames 11084-11108 and
11312, black before (an empty bank shown), are now the cross-fade from the
shot before to the new one -- what the testers' black screens in cutscenes
were.

**A capture into a bank not mapped to the LCDC** (2026-10-17): the DS writes a
capture only into a bank mapped to the LCDC (GBATEK, DISPCAPCNT's write block);
the port wrote it whatever the bank's mapping. After a multiplayer mission won,
the field asks for the mission review (scene 3, ov003: `Ov002_StepSessionMenu`'s
case 0 -> `Ov002_SendSeatTally`, in a session and without the give-up flag),
whose VBlank callback (`Ov003_DisplaySetup` -> `Gfx_ToggleCaptureMode`) arms a
capture every VBlank, into bank C or D in turn, until `Ov003_Teardown`
unregisters it. The last one armed is taken by the port at the next VBlank,
after the results (scene 6) have mapped bank C to their BG (VRAMCNT 0x84) and
loaded their characters there: the results' upper screen showed the captured
pixels as vertical stripes (the players' reports, always after a mission won in
multiplayer). Seen with `KHDAYS_TEST_REVIEW` (a give-up in a session goes on to
the review as a won mission does: `runtime/menus/retry.c`) on two copies of the
multiplayer walk: the review's captures all into banks in the LCDC (VRAMCNT 0x80),
one at the scene change into bank C at 0x84, the results striped; now that
capture is not written, the results whole, the review and the story's screen
blends (f11082, bank D) as before.

**The screen blends at the port's resolution** (2026-10-16, `gfx_render.c`
`take_capture`): shown from the DS's bank, each cut was a 256x192 picture,
stretched, without the sides of a wider one (a player's report: the models
not upscaled and the picture narrowed with blue bars). The window takes each
capture again from what it shows (engine A's screen, or the one-screen
picture), composed as engine A's layers and 3D, and blends it with its copy
of the bank as the DS does (`capture.hlsl`). While engine A shows the bank
(the VRAM display mode) the window composes its layers anyway and shows its
copy over them. The package carries the capture armed at its VBlank and, per
bank, the captures taken into it, the count after the last that read no bank
(a blend's first: engine A's picture alone) and whether the bank still holds
its last capture (a sum of what it wrote): a copy is shown only when it was
taken at that first capture or after (packages the window never drew can
skip some), or is made then from the frame shown when the bank holds that
first capture alone. Seen on the story walk's prologue (`KHDAYS_TRACE_CAPTURE`,
one screen and two at 16:9): the first cut's capture at f11090 (EVA 16,
engine A still showing its layers), then from f11092 the bank shown from its
copy, captures every second VBlank with EVA 1, 1, 2, ... and EVB 16 - EVA, at
the window's resolution and width.

**Texture matrices** (the ROM, and `KHDAYS_TRACE_ARM` on the play walk,
2026-10-01): `NNSi_G3dSendTexMtxMode0` and `NNSi_G3dSendTexMtxMode2` take the
texture matrix's calculator from a table of eight in the ITCM -- 0x01ffa1f8
and 0x01ffa598, indexed by the material result's flags & 7 -- that holds
the functions' DS addresses: 0x020192xx-0x02019bxx in the main module, and
the ITCM's own `Mtx44_SetIdentity2D` (0x01ffa42c) and `Mtx44_SetIdentity2D_2`
(0x01ffa5b8) in slot 7. The build listed no ITCM function at its DS address,
because the MobiClip decoder is copied over the ITCM while a movie plays, so
those two ran interpreted: about 21,000 calls on the play walk. The ITCM's
functions are now listed with their sizes and run natively while their bytes
are still the game's (`port/runtime/core/overlays.c`, against a copy of the
ITCM as the game starts); on the play walk no ARM9 instruction is
interpreted.

**G3D's draw** (gate G5, from the C): NitroSystem's `NNS_G3dDraw` is the
decomp's `NNS_G3dDraw` (engine). It refreshes the render object's
animation hints when its flag 0x10 says they are stale, then runs
`NNSi_G3dDrawInternal` (`NNSi_G3dDrawInternal`, in ITCM) with the global render
state `NNS_G3dRS` (`NNS_G3dRS`) or, when there is none, a 0x188-byte one
on its stack. That clears the state; points it at the model's SBC program
(or the render object's own); copies the model's position scales (+0xe0,
+0xe4) and the render object's one callback -- function, command, timing --
into the state's tables by command (+0x0c functions, +0x8c timings); calls
the render object's init function (`NNS_G3dRenderObjSetInitFunc`, one site);
and dispatches each SBC byte through the 14 handlers at 0x027e0660 in DTCM
(`NNS_G3dFuncSbcTable`, filled by the misnamed
`G3d_InitSbcFuncTable`) until RET sets the state's flag 0x20:

| # | SBC command | handler (decomp name) | what it sends |
|---|-------------|-----------------------|---------------|
| 0 | NOP | `NNSi_G3dFuncSbc_NOP` | -- |
| 1 | RET | `NNSi_G3dFuncSbc_RET` | -- (ends the draw) |
| 2 | NODE | `NNSi_G3dFuncSbc_NODE` | nothing: the node's visibility, from the visibility animations |
| 3 | MTX | `NNSi_G3dFuncSbc_MTX` (misnamed) | MTX_RESTORE of a stack slot |
| 4 | MAT | `NNSi_G3dFuncSbc_MAT` | the material: colours, polygon attributes, texture, palette (with the material animations) |
| 5 | SHP | `NNSi_G3dFuncSbc_SHP` | the shape's display list (`NNS_G3dGeSendDL`) |
| 6 | NODEDESC | `NNSi_G3dFuncSbc_NODEDESC` | the joint's matrix from the joint animations; MTX_STORE |
| 7 | BB | `NNSi_G3dFuncSbc_BB` | a billboard: reads the clip matrix back |
| 8 | BBY | `NNSi_G3dFuncSbc_BBY` | a Y billboard: the same |
| 9 | NODEMIX | `NNSi_G3dFuncSbc_NODEMIX` | a skinning matrix blended from stack slots; reads the clip and vector matrices back |
| 10 | CALLDL | `NNSi_G3dFuncSbc_CALLDL` | a display list in the program |
| 11 | POSSCALE | `NNSi_G3dFuncSbc_POSSCALE` | MTX_SCALE by the model's scale |
| 12 | ENVMAP | `NNSi_G3dFuncSbc_ENVMAP` | the texture matrix for an environment map (`NNS_G3dGetCurrentMtx`) |
| 13 | PRJMAP | `NNSi_G3dFuncSbc_PRJMAP` | the texture matrix for a projected map (the same, and the clip matrix) |

It is called from 50 places in 46 files (`Obj_RenderModel`, `Scene_DrawNode`,
the enemies', players', battle's and event scripts' overlays), and
`NNS_G3dDraw1Mat1Shp` from 4 (`InstancedModel_Draw`, `ShadowVolume_Draw`
twice, ov255). This build of NitroSystem calls
back at one point only: NODEDESC at timing C, once the joint's matrix is the
current matrix and before it is stored (`NNSi_G3dFuncSbc_NODEDESC`); NODE,
MAT, SHP, BB, BBY, ENVMAP and PRJMAP call none ("as built here, without the
render callbacks"). The game's 23 `NNS_G3dRenderObjSetCallBack` calls all ask
for that point (command 6, timing 3). What the callbacks touch: the state's
node-valid flag (+0x08 bit 0x10) and current node (+0xae), the position
scales, the current matrix -- read with `NNS_G3dGetCurrentMtx`, changed with
MTX_SCALE or a multiply through `GX_SendFifoWords` (`SceneNode_JointCallback`
attaches children at a joint so) -- and their own entry (+0x24, +0x92), which
`Ov144_NodeMatrixCallback` clears to stop for the rest of the draw. After a
draw, `NNS_G3dGetResultMtx` (`NNS_G3dGetResultMtx`: `Obj_RenderModel`,
`Scene_DrawNode`, ov147) restores a joint's stored slot and reads it back.

**The direct geometry** (gate G6): outside NitroSystem's draw the game
sends geometry through NitroSystem's command buffer, `GX_SendFifoWords` (158
calls in 49 files: the field's flat rectangles, sprite quads and actors; battle's
animation slots, voice tags and wind; the event scripts' noise quads and
tiles and joint rotations; the players' trails, effects and bone matrices;
the menus' quads and object graphics in ov005, ov008 and ov025; ov106's
noise), through register macros (`Billboard_DrawList`: textured quads), and
by storing SwapBuffers and the viewport (`main.c`, `Ov001_SetupDisplayRegs`).
By name it calls only `G3X_SetHOffset` (24 sites), `G3X_SetClearColor` (20),
`G3_TexCoordPx`, `G3_VtxXY`, `G3_Vtx`, `G3_LoadMtx43`, `G3X_InitMtxStack` and
`G3X_ResetMtxStack`. All of it, NitroSystem's included, ends as words to
GXFIFO (0x04000400) or the command ports (0x04000440-0x040005ff): CPU stores
(`MIi_CpuSend32` for blocks) or a DMA (`MI_SendGXCommandAsync`, the display
lists).

**What the game reads back** (gate G7): the clip matrix and the vector
matrix (`G3X_GetClipMtx`, `G3X_GetVectorMtx`: in NODEMIX, BB, BBY, PRJMAP, and
in `NNS_G3dGetCurrentMtx`, which loads an identity projection to read the
position matrix as the clip matrix -- 59 calls in 24 of the game's files: the
callbacks above, attachments, effects); GXSTAT's busy bit, before those reads
and in the SDK's stack resets (`G3X_IsGeometryBusy`), its matrix stack levels
(`G3X_GetMtxStackLevelPV`/`PJ`, in `G3X_InitMtxStack`) and its FIFO level
(the command buffer's DMA). The box, position and vector tests and the
polygon and vertex counts are not read: no `G3_BoxTest`, `G3_PositionTest`,
`G3_VectorTest` or their result getters, and no read of 0x04000604-0x0400063f
by address, in the game or the libraries. So the joints' matrices have to be
where the callbacks and `NNS_G3dGetResultMtx` ask for them, as the DS
computes them, and nothing else of the geometry is read.

**The port's own 3D geometry** (`port/gfx/g3d_geometry.c`):
what the game and NitroSystem send the geometry engine -- packed words to
GXFIFO, stores to the command ports, display lists by DMA -- is run at once
by the port's geometry, whose state (the four matrix stacks, the vertex's
attributes, the lights, the material, the viewport) is the DS's in the DS's
fixed point; it builds the polygon list the GPU draws (`gpu_raster3d.c`) and
the software rasterizer draws as the reference (`gpu3d.c`, now nothing
else). There is no FIFO: nothing is ever busy or waits, GXSTAT reads the
FIFO empty, and a SwapBuffers closes the list being built, which the next
VBlank shows (the DS holds what follows a SwapBuffers until then; here it
goes into the next list at once). NitroSystem's single words
(`GX_SendFifoWords`, `NNSi_G3dGeBufferCommand1`, prepare.py HOOKS) and its
blocks (`MIi_CpuSend32`) reach it without the I/O trap. Seen (2026-10-01):

- Each walk's every command recorded and replayed into the old engine (the
  commit before, `gpu3d.c` with its FIFO) and into the port's geometry, the
  list shown compared after every VBlank: mission 14,931 VBlanks, story
  14,931, play 11,941, run07 17,919 -- 22.7 million polygons shown in all,
  0 VBlanks differ.
- With a window, `KHDAYS_GFX=check` on the play script: at 1x 0 pixels
  differed from the 2D engines over the software's 3D in every 300 frames
  (27 of them); at 4x, 4:3 and 16:9, 0 from the old composition (24 each).
- `khdays-render-test` on packages recorded anew (the play walk at VBlanks
  3500 to 11000; the earlier recordings predate the 2D's package): the GPU's
  3D at 1x differs from the software's on 0.002% of the pixels where the 3D
  takes part, a channel by 8 of 255 at most; elsewhere on none.
- The four walks, Debug and Release, reach their scenes.

A list has no limit but memory: the DS's polygon and vertex RAM (2048
polygons, 6144 vertices) no longer drops what comes after it. Each
polygon the DS would have dropped is kept, marked, and drawn by the GPU; the
reference leaves it out as the DS does, and the log says once when a list
goes beyond. No walk's list does (2026-10-01); a synthetic list of 3000
triangles kept all 3000, 952 of them marked, and the reference drew the
DS's 2048.

Known differences from the DS: anti-aliasing (DISP3DCNT bit 4) is drawn
without it, as by the reference; texture coordinates from normals and from
vertices follow GBATEK's shifts, not verified against the hardware (both
said once in the log when used).

**At N x** (2026-10-05, what the maintainer saw at high resolution): the
models leave gaps narrower than a DS pixel between their parts (down the
middle of Roxas's face and coat; their shared vertices are exact before and
after the projection, so the gaps are the models'), which the DS's pixels do
not fall in and N x pixels do -- the background showed through as a line.
Above 1x the GPU widened each opaque polygon by a quarter of a DS pixel on
every side (not shadows, not translucent polygons; the attributes stayed
between the real edges). On 2026-10-10 the gaps no longer showed without it
(Roxas's face and coat in 28 frames of the tutorial at 8x, no antialiasing,
from behind and in front), and it bloated every model -- a mod's many small
triangles most: the maintainer's Riku came out with its silhouette and hair
in steps and its face smeared, the worse the smaller it drew, once its MASK
material made it opaque -- so polygons cover what they cover at N x again.
The
port's texture smoothing keeps the DS's coverage -- whether a pixel shows is
the nearest texel's alpha -- and blends colour only from texels the polygon
maps: blending alpha too made the day cards' numbers bold and hair cards grow
edges, and taps past the polygon's texels took colours from the texture's
other side or another part of the atlas.
It does not smooth a polygon drawn in screen space (an orthographic
projection, every w 1) at its texture's own size or smaller -- the game's 2D
drawn with the 3D engine: the HUD's pieces, the day cards, the noise over
Sora's memories (`Ov023_DrawNoiseOverlay`: twelve 64 x 64 tiles, a texel to a
DS pixel), which smoothed turned to blobs with a line at every tile's edge
(2026-10-05); those stay sharp, as the 2D layers. A model under an
orthographic camera with its texture magnified (the title's logo) is still
smoothed.

**Shadow polygons** (2026-10-05): the characters' shadows are shadow volumes
(`ShadowVolume_Draw`: the volume as a mask, polygon ID 0 with front faces
culled, then as the shadow, ID 0x3f with none culled). A surface is in
shadow where it lies between the volume's front and rear faces: both
rasterizers mark the stencil where the mask's rear face fails the depth test,
draw a shadow only where a mark is and its own test passes, not over a pixel
of its own ID (GBATEK), and start a pixel's marks afresh when a mask reaches
it after another polygon (`gpu3d.c` `plot`). GBATEK's wording, which they
followed before (the mask set where it passes, the shadow drawn where it is
clear, clearing it), let the second pass's rear faces shade everything above
the floor: the whole volume showed, a dark octagon around each character;
now the shadow lies flat on the floor. The render test's packages were recorded again (the play
walk, two screens): the GPU at 1x and the software agree on every pixel.

**Calls into DS memory** (found on the way, in the 32-bit build the port
had until 2026-10-03; the rewritten game calls a DS address through its
function reference, section 13): MSVC's linker adds the image
base to an absolute symbol in a call's fixup, so a native call to a routine
`absolute.asm` names at its DS address -- code the build had no C for, such
as MobiClip's blit then -- lands at the image base plus that address. With
the image at 0x00400000 such calls into main RAM had landed in its mirror
and worked by chance; the image now sits at 0x10000000 (the Debug image,
21 MB at 0x00400000, had let what Windows allocates before `main` reach
main RAM, one start in two), `memory.c` keeps the range above it free, and
`ds_calls.c` takes such a call back to its DS address: to the native
function there, or to the stop that names it (section 5).

**Where the graphics come from** (gate G9, `KHDAYS_TRACE_ORIGIN` on the
play walk, 2026-10-01): the engine reads every file through three loads --
`Loader_RequestFile`, `FileLoader_LoadInto` and `Archive_LoadFile` -- by
path, or by a packed id that opens an entry inside a pack (its place in the
ROM then names the pack and the entry's offset), and LZ-decompresses the
`.z` ones whole with `MI_ReadUncompLZ8`; no other decompression runs on
graphics. From the buffer the data reaches VRAM by DMA or the SDK's CPU
copies. Followed that way, the walk's 336 loads and 11,650 copies into VRAM
gave:

- With a file (240 copies): the field's HUD (`UI/btl/main.p2`,
  `UI/btl/es/main.p2`, `bl000`/`bl001.NCGR`, `bm_lo_TT08.NSCR`, the map's
  characters in `UI/btl/mapchr.p2`), the menus (`UI/cm/cm.p2`,
  `cmb_es.p2`, `cmo_es.p2`, `UI/mlt/res.p2`, `res_es.p2`, the `.BGUI`
  screens), the hearts counter's textures (`UI/hcnt/arc`, `arc_es`), Roxas's
  model, weapons and lights (`ba/ch/ro/def_hb.p.z`, `w_.p2`, `li_e*.p.z`),
  the effects (`ba/ef/*.p.z`), Twilight Town (`mi/wd/wd_tt`, 294,256 bytes
  of textures, with their palettes), the moogle and the prizes (`mo/`), the
  title (`ttl/ttl.p2`) -- textures and texture palettes into LCDC banks,
  the 2D's characters, screens and palettes into the BG and OBJ banks.
- With no file (11,410 copies, nearly all of them every frame): what the
  game builds in RAM itself -- tile maps (2 KB to BG A, 4.8 KB to BG B),
  text canvases (13 KB to BG B), the lower screen's map (48 KB to BG B) and
  a 24 KB block to BG A, each uploaded anew whenever it changes. Their first
  non-zero bytes were looked for in every game file, decompressed or raw:
  a few short ones matched, the large ones none. These are the game's own
  drawing, not files.

**The 3D's textures by name** (`port/runtime/mods/textures.c`,
`names.cpp`): a polygon's TEXIMAGE_PARAM and PLTT_BASE say where its
texture and palette sit in the slots; the slots' pages say which bank,
origin.c which file and offset, and the NitroSystem resources in that
file's data -- found by their BMD0/BTX0 headers, inside KAPH or D2KP
containers or not -- their names in the TEX0 dictionaries. A packed entry
is read as the engine reads it: the "P2" archive's header (`P2` magic, u16
count at +2, the data's start at +0xc, u16 offsets in 0x200-byte units at
+0x10, then u32 sizes with bit 31 for an LZ-compressed entry), so
`mi/wd/wd_tt@1600` is `mi/wd/wd_tt#2`. On the play walk every texture drawn
was named (250 look-ups, none without a name): Twilight Town's
(`mi/wd/wd_tt#2/tex/tt02_000@tt02_000_pl` ...), Roxas's
(`ba/ch/ro/def_hb.p.z/tex/ro_h_hair00@ro_h_hair00_pl` ...), his weapon's,
the menus' 3D (`UI/cm/cm.p2#13` ...), the title's, the moogle's, the
hearts counter's and the effects'. The names follow the game's data from the
start whenever the window shows HD graphics (the default) or something
dumps: on the play script that cost the game's thread 624 against 617 ms
of work a second with them off.

**HD textures from mods**: with "HD graphics" on, a texture
whose name has a PNG in a mod (`mods/<Mod>/graphics/<name>.png`, the mods'
order) is drawn from that PNG instead of from the DS's texture memory --
any size (a whole multiple of the original is expected; another is
stretched, said once), read once, kept for the run. The frame package's
polygons carry which replacement they draw; the GPU has all of them in one
buffer, added as they appear, and the 3D pass samples the replacement at
the place of the original's mapping -- the DS's repeat and flip, at the
replacement's resolution, nearest or with the port's texture smoothing.
The software reference draws the game's own. Seen (2026-10-01, the play
script at 4x with a test mod): Twilight Town's floor drawn from a 256x256
PNG over its 64x64 original, finer than the DS could show, and Roxas's
hair00 from a 128x256 one. With no mod, the 4x check (`KHDAYS_GFX=check`)
found 0 pixels in every 300 frames (21) and the walks were unchanged.

While a mod's textures are drawn (and the picture is not being compared
with the old way) the 3D and engine A's composition work in 8 bits a
channel instead of the DS's 6 and 5 (`fine` in `raster3d.hlsl` and
`finish3d.hlsli`, `KDF_FINE` in `compose.h`): vertex colours, texels, the
toon table, the rear plane, edge marking and fog, the 2D's blending,
brightening and master brightness on wider channels, the 2D's own colours
widened from 5 bits. With a gradient PNG over Twilight Town's floor the
screen showed 236 levels of red and 203 of green there, where 5 bits allow
32. With no mod nothing of it runs.

**The dump without a game** (`--dump-graphics`, 2026-10-01): 13,750
textures from 2,942 files' data in 43 seconds, 17 of them paired with no
palette by any model or animation. Of the 149 textures the play walk drew
(and the in-game dump wrote), 147 came out under the same names with the
same pixels; the other two are drawn with another texture's palette --
`UI/cm/cm.p2#13`'s `Dtnd_02` with `Dtnd_01_pl`, `mo/mo#0`'s `gate01` with
`gate00_pl`, though each has its own `_pl` -- which the game's code
chooses, not its data: only the in-game dump names them so, and a
replacement named by the texture alone covers them.

The same pass writes 2,912 models as glTF. Roxas's (`ba/ch/ro/def_hb.p.z`,
`roxas_b_h`) opens in Blender with its 24 bones (`roxas_R`, `Bip01`, the
biped's bones, `ro_w_tg_R`, `roxas00`, `roxas_tg`), its textures and its
rest pose: arms lowered, 1.603 units tall.

**HD 2D from mods** (`port/runtime/mods/tiles2d.c`,
2026-10-01): with "HD graphics" on, a 2D tile sheet whose name has a PNG in
a mod (`mods/<Mod>/graphics/<file>[#<entry>]/chr/<n>.png`, the dump's name
and layout, any whole multiple of the sheet's size) is drawn instead of the
tiles the game copied from it. At each VBlank the game's thread maps the
layers' and sprites' memory of both engines (a word for each 32 bytes) for
the 16 KB pages whose bytes, bank or origin changed: where a tile of a
replaced sheet starts -- named by where its bytes came from (origin.c) and
the NCGRs of that file's data, its bytes still the sheet's, so tiles the
game draws over itself (its text) stay its own -- the replacement and the
tile's number. The map rides in the frame package by page; the composition
reads it beside the tiles and takes the replacement's texel at the output
pixel's place inside the DS pixel (flips, affine layers and sprites with
their fractional coordinates): an indexed PNG's pixels are palette indices,
drawn with the palettes the game draws the tiles with; a true-colour one's
own colours, in 8 bits a channel through the blending and the master
brightness. SDL's GPU API binds at most 8 storage buffers to a pass
(`MAX_STORAGE_BUFFERS_PER_STAGE`): the maps follow the layers' and sprites'
memory in their buffers, the palettes the descriptions, and every
replacement (the 3D's textures, models' and 2D sheets) shares one buffer
(`port/host/render/gpu_hd.c`); with 11 bound the composition failed intermittently,
showing frames it had not drawn.

Seen (the play script, 2026-10-01): 40 sheets drawn (the title's, the
menus', the field HUD's `UI/btl/es/main.p2#0/chr/0`, the map's); all 40
put back as the dump wrote them, at 1x with `KHDAYS_GFX=check`: 0 pixels
differed from the 2D engines' in every 300 frames (22 of them). The HUD's
sheet four times larger (Scale2x twice, indices kept) at 4x: the command
deck's rounded ends smooth where the DS's are steps, in the game's colours;
the same sheet as a smoothed true-colour PNG: drawn in its own colours.

**Models from mods** (`port/runtime/mods/models.cpp`,
2026-10-01): with "HD graphics" on, a model whose name has a glTF in a mod
(`mods/<Mod>/graphics/<file>[#<entry>]/mdl/<model>.gltf`, the name the dump
writes it under) is drawn instead of the game's. The game draws its model
as always: three hooks (prepare.py HOOKS) see NNSi_G3dDrawInternal start
and end (`NNSi_G3dDrawInternal`, its NNSG3dResMdl named through origin.c and the
file's MDL0 dictionary), each node's position matrix as
NNSi_G3dFuncSbc_NODEDESC leaves it (after the node's callback, before it is
stored), and the SHP command (`NNSi_G3dFuncSbc_SHP`), whose display list is not
sent. At the draw's end the mod's meshes are skinned on their own skeleton
from those matrices, lit by the DS's lighting with the material and lights
the game set (in floats), and added to the list as polygons of their own
(`KHDAYS_G3D_MOD`, clip space in floats: no DS precision or RAM limit)
with the glTF's textures at their size; the software reference leaves them
out. The mod's joints follow the game's nodes by name, or as
`<model>.bones.txt` pairs them; a joint that follows none moves with its
parent. A joint that follows a node is drawn turned as the node is turned
from its rest, its head where its parent puts it (the mod's proportions
kept); when the way to its next following joint points more than 20
degrees away from the game's at rest (a T-pose against the game's lowered
arms), it is turned at rest to match, and its children with it.

Seen (the play script, 2026-10-01): models drawn by name -- `title`, Roxas
(`ba/ch/ro/def_hb.p.z/mdl/roxas_b_h`, coat and all), his keyblade
(`ba/ch/ro/w_.p2#0/mdl/ro_w00600`), the world map's (`UI/cm/cm.p2#N`), the
hearts counter's (`UI/hcnt/arc#N`). Roxas's own dumped glTF as the mod:
drawn as the game draws him, every joint on its node with no turn (frame
10000 at 4x); with `scale 1.3` one larger Roxas and none beneath him, the
keyblade still at the game's hand. A Sora model (4 meshes, 9,072 vertices,
3,024 triangles, 227 joints and its own weights, against Roxas's 970
vertices, 618 triangles and 24 nodes), converted from FBX to glTF unchanged:
20 joints paired by position and hierarchy, the upper arms turned 77.7
degrees at rest, the rest under 20; idle, running and attacking with the
keyblade in his hand. Roxas's `Bip01_Neck` moves none of his vertices, and
its turn from rest is not his body's (in the combat stance, against
`roxas_R`: `Bip01` 53 degrees, `Bip01_Spine1` 83, `Bip01_Neck` 97,
`Bip01_Head` 15, `KHDAYS_TRACE_MODELS=2`): Sora's hood, hung from the joint
paired with it, swung up behind his head; paired with nothing (moving with
the chest), and his neck with `Bip01_Head`, it stays down.

**FBX and `.glb` models** (`assets/src/fbx.cpp` with ufbx, `gltf.cpp`,
`models.cpp`): the mod's model is `<model>.gltf`, else `.glb`, else `.fbx`, the
same neutral form from each. ufbx scales an FBX's geometry to metres but turns
its axes in the nodes' transforms, so a skinned FBX mesh's vertices are carried
to its bind pose (`bind_to_world * geometry_to_bone`) and each joint's inverse
bind is the inverse of its `bind_to_world`, a joint shared by every skin that
uses its bone at one bind pose -- as glTF's. FBX's cluster `Transform` is the
geometry in the bone at bind (`inverse(TransformLink) * mesh`), not the mesh's
own transform. Seen (2026-10-05): Roxas's dumped glTF written as ASCII FBX in
centimetres and Z up (13 geometries, 24 joints) imports to the same 882 corners
-- positions, texels, colours, weights, joints and palette -- as the glTF. The
maintainer's KH2 rips (KHAssetStudio, FBX SDK 2020.3.9, binary 7500, centimetres,
Y up): Sora's `P_EX100.fbx` (4 meshes, 228 bones `Bone0`..`Bone227`, one weight a
vertex, textures stored under the ripper's paths and found by name beside it)
over the three Roxas models, with `mods/Sora`'s pairs renamed to the FBX's bones;
`W_EX010.fbx` (Kingdom Key, 12 bones: the body `Bone2`, the chain `Bone3`..) over
`ro_w00600` (Days' Kingdom Key: its texture the gold guard, the silver blade, the
chain and token), `Bone2 ro_w_00`, `Bone3 ro_w_01`, `scale 1.07` (1.434 m against
1.343 m). On the play script with a window: Sora idle, running and attacking in
Roxas's place, the upper arms turned 77.7 degrees at rest, the KH2 key with its
chain and token. Sora floated over the floor while his root bone `Bone0` was
unpaired: an unpaired root follows the model's first node, not the game's root;
paired with `Bip01` (and `scale 0.92`) his feet are on the floor. The key is
placed by the game's weapon joints, where Roxas's hand is; Sora's hand is placed
by his own arm's proportions, so in some poses (running, frame 5100 of the play
walk) the key is off his hand -- open. His hood's bones are paired with nothing
and move with his head: it does not swing (the game has no cloth for it). An FBX
has no two-sided flag: its meshes are drawn two-sided (the game's own models
are, in the dump's glTF).

**The picture at the monitor's rate** (`gfx/g3d_interp.c`,
`host/render/gpu_present.c`): each 3D list keeps, besides its clipped polygons, the
same polygons as the game sent them -- before clipping, the culled ones too --
with a key: the model draw it came from (`NNSi_G3dDrawInternal`'s resource and
how many draws of it came before in the list) and its place in that draw, or
for one outside any draw its material (texture, palette and attributes but
alpha) and its place among that material's -- numbered across all of them
before 2026-10-10, an effect whose count changed moved every loose polygon
after it onto another's. A match also needs the same texture, palette,
attributes but alpha and texture drawn instead; a mod's model's polygons are
emitted inside the game model's draw (before 2026-10-10 they came after its
end, loose, and at `smooth` a replaced boss came out in shards: the Xion boss
of mods/HD Xion Boss, matched triangle with another triangle). The render
test puts recorded packages of close frames through the pictures between
lists and says how many matched polygons moved over a quarter of the view,
by draw: on that boss's fight, at most a part of a draw (a slash near the
eye). The game's own list is clipped
from them by the same function the host's pictures use (`khdays_g3d_finish`),
so the DS's pace draws exactly the list the port drew before: on the four
walks 25,200 lists built again from their polygons before clipping were the
game's own, byte for byte (a temporary check, 2026-10-02), and the render test
rebuilds its recorded packages' lists the same. With
`picture_pace = smooth` the window draws a picture at every refresh: the
polygons the two latest logic frames share (the same key and as many
vertices -- in the field the median share is all of them) have their vertices
placed between the older's and the newer's, in clip space, then the list is
clipped and drawn as the game's. The newer list reaches the host in the
package of the VBlank after the one that first shows it (median 20.6 ms
after); the picture moves to it over one logic frame from that package's
VBlank -- up to one logic frame behind the DS's pace's picture. Nothing is
interpolated into a new scene or across a camera cut: a camera that turned
more than the field camera can in a logic frame (0xa00, 14.06 degrees: the
cap in section 3) or moved more than 1.0 in its translation (a bound the
camera code does not give: the gap measured on the story walk, normal frames
at most about 0.05, its cutscene's cuts 3.2 and more). The camera is
NitroSystem's flush's: `NNS_G3dGlbFlush` (NNS_G3dGlbFlush,
the engine's) and `NNS_G3dGlbFlushP`, the first of each list. One GPU frame
is in flight, so a picture is drawn for the refresh that shows it.
Measured on the maintainer's 165 Hz monitor (`KHDAYS_FRAME_LOG`, the play
walk's field, 25 s): 165 pictures a second, none more than 1.5 times the
period apart (8.9 ms the largest), each a new one save while the next logic
frame was late (192 of 4,125 the newest list held).

## 8. Files

How the game's ROM reads reach it, read in the decomp (`libs/nitro/card`,
`libs/nitro/fs`, `src/overlays/system/ov028_dsprotect`) and seen with
`KHDAYS_PROFILE` and the session log (`KHDAYS_LOG`), 2026-10-01.

**The path.** Every read of the game's ROM goes through `CARDi_ReadRom`
(0x0200eea4), whose only caller is the FS ROM archive's
`FSi_ReadRomCallback`: `FS_ReadFile`, `FS_ReadFileAsync`, `FS_LoadOverlay*`
and the engine's loaders all end there. `CARDi_ReadRom` locks the card's
common state (`data_020464e0`: the source at +0x1c -- the asked offset plus
`data_02046b00` --, the destination at +0x20, the length at +0x24, the
callback and its argument at +0x38/+0x3c, the flags at +0x114), then starts
a card DMA (`CARDi_TryReadCardDma`, for aligned reads; completion in the
card's interrupt, `CARDi_OnReadCard`) or runs `CARDi_ReadRomSyncCore` -- on
the card's task thread when asynchronous. That one copies (the 512-byte page
cache, then the read function card command by card command), checks the
card is still in (`CARDi_ReadRomIDCore`), sets the result, clears the busy
flags, wakes the threads waiting and calls the callback (`FSi_OnRomReadDone`).

With the port's reads (`KHDAYS_FILES` unset; `port/tools/prepare.py`
HOOKS, `port/runtime/core/files.c`) the DMA is not tried, and
`CARDi_ReadRomSyncCore`'s copy is one copy of the length from the port's
image of the ROM (`port/runtime/core/rom.cpp`: a mod's file first, then the
extracted data) into the destination; the rest runs as the SDK runs it.

**The rest of the card.** Every other piece of code that names the card's
registers (`CARDi_SetRomOp`, ROMCTRL `0x040001a4`, the data port
`0x04100010`):

| Code | What it reads | With the port's reads |
|---|---|---|
| `CARDi_ReadRomIDCore` (0x0200edb0) | the chip ID (command 0xb8, 4 bytes), after every read, for `CARDi_CheckPulledOutCore` to compare with the copy kept at boot (0x027ff800 / 0x027ffc00) | the value the card answers (`KHDAYS_CARD_CHIP_ID`, which boot.c records), without the command |
| `Ov028_DSProt_ROMUtil_Read` (DS Protect's ROM read) | six 0x200-byte pages for its flashcart check (`Ov028_DSProt_ROMTest_IsBad`): 0, 0x200, 0x400, 0x8000, 0x8200, 0x8400, by driving the registers itself; a cartridge answers the first three from 0x8000 (a page read below 0x8000 returns 0x8000 + (address & 0x1ff)), a flashcart does not | each page from the port's image with that rule, its words where its loop puts the card's (`prepare.py` FILE_HOOKS); seen in the session log: 0x8000 three times, 0x8200, 0x8400, twice at boot |
| `CARDi_TryReadCardDma`, `CARDi_SetCardDma`, `CARDi_OnReadCard` | the card DMA | not reached |
| `func_01ff84b8` (`Boot_ReloadModules`, `OSi_FinalizeReset`) | the binaries again after `OS_ResetSystem` | not run: the port boots the game again in its process (`port/runtime/core/start.c` `reset_boot`) |

The save does not go through the card's bus: the backup's requests are PXI
messages to the ARM7, which `port/runtime/core/arm7.c` answers. With the port's
reads the emulated card gets no command: on the play walk `files:` says
`card: 0 commands` every second, and `port/runtime/core/card.c` would name the
first ones that still arrived (none did).

**Measured** (the play walk's script, Release, with a window,
`KHDAYS_PROFILE`; the card's numbers from before the port's reads):

| | Through the card | The port's reads |
|---|---|---|
| Longest stretches without a wait | 4875, 2538, 2033, 1604, 951 ms | 534, 447, 356, 355, 247 ms |
| The card | up to 2,670 reads (1.3 MB) a second | 0 commands |
| `strm/Title_BGM_PCM8` heard after it was asked for (three runs) | 117.7 ms | 100-103 ms |
| `strm/STRM_GAME_START_ADPCM` | 20.0 ms | 7-12 ms |
| `strm/STRM_MISSION_START_ADPCM` | 24.2 ms | 13-18 ms |
| The port's reads, the most in a second | -- | 9.9 ms (entering the field) |

The four regression walks reach the same scenes. A read's cost is mostly
opening its file the first time (about 0.15 ms, against 0.01 ms for a read of
a file already open; up to 192 stay open). What is left in the stretches is
not reading: entering the field the game read 712 KB in under 10 ms during
its 534 ms; opening the camp menu in a player's session it stopped 283 ms
with about 5 ms of reads. What it does there is *not identified* (to
profile). The battle music (a sequence: `StartSeq` loads its
bank, waves and data, then starts it, 0.3 ms on the walks, the data already
loaded) and a first Fire are not on any walk; a player's session log shows
both.

### 8.1 The game's language

Read in the decomp and seen in the session logs (`read` lines), 2026-10-07.

**One halfword, written once.** The language is `gLangPath`'s first halfword
(bss 0x0204c1ec; 1 English .. 5 Spanish, the firmware's numbering). Its only
writer is `Msg_SetLanguage`, whose only caller is `main`: the
language `OS_GetOwnerInfo` copied from the firmware's user settings
(0x027ffc80 + 0x64). `GetLanguage` reads it (35 functions, mostly scene and
menu builders choosing a layout or a string per language), and
`Msg_BuildLangPath` puts its two letters (`data_020427d4[lang]`) in place of
a path's `&` each time one is opened: by `Archive_LoadFile`,
`FileLoader_LoadInto`, `FileLoader_LoadSync`, `Loader_RequestFile`,
`Msg_OpenContainerAndReadHeader`, and directly by `Ov022_EnterFourRowScene`,
`Ov022_LoadMarkerPanel`, `Ov002_ResetTitleWidget` and the MobiClip stream's
open. The decomp names 55 localized paths. `OS_GetOwnerInfo` reads the
firmware's area again in Mission Mode's peer sync (`Ov006_MissionPeerSyncState`,
`Ov006/Ov008_MissionUpdateSelectionState`).

**When the scenes load their localized files:**

| Scene | Files | When |
|---|---|---|
| 1 title | `db/db_&.p2`, `UI/load/lrd_&.s.z` | at the logos' end |
| 7 Mission Mode select | `db_&`, `UI/mlt/res_&.p2`, `UI/mlt/mlt_&.s.z` | at entry, parts of `res_&` later |
| 19 camp menu | `db_&`, `UI/cm/cmo_&.p2`, `UI/cm/str/{root,status,select,world_id}_&.s.z`, `UI/cm/msl_&.msi.z` | again at each page it opens |
| 2 field | `pause/pausebg_&.pbg.z`, `db_&`, `ev/hint_&.z`, `ba/ef/{st,mi,_c}_&`, `UI/btl/&/{map.p2, info, bl000.NCGR, bl001.NCGR, magic, cmd, enemy, text, chara}`, `UI/hcnt/arc_&` | at a mission's start |
| 2 field, its end | `UI/srslt/res_&`, `UI/pnl/3d_&.pak.z`, `world_id_&`, `msl_&` | the results |
| 2 field | `gameover/gameoverbg_&.pbg.z` | when shown |

**Text is drawn when a screen is built.** The builders load a string table,
draw the strings into character canvases and free the table
(`Ov002_DrawPanelRowText` with `UI/btl/&/cmd.s.z`, `Ov002_BuildPanelLabels`
with `enemy.s.z`, `Ov002_BuildTextItem` with `text.s.z`); words drawn as
pictures go to VRAM when loaded (`UI/btl/&/bl000.NCGR.z` through
`Ov002_UploadFileToSubBg1Char`). A screen already built keeps its language
until the game builds it again.

**The message databases are kept.** `MsgDb_LoadDb(id, mode)` keeps up to 0x21
databases in the table at `data_0204c238` (0x14 bytes each: two packs, the
records and their count, a reference count at +0xe, a mode at +0x10), each
read from `db/db_&.p2` and not read again while it is loaded
(`MsgDb_IsLoaded`): one loaded in English stays English across scenes.

**Switched in the middle of a scene** (the language walk: the mission walk's
script with `KHDAYS_LANGUAGE_SWITCH`, a switch in each scene, 2026-10-07):
what the scene builds afterwards comes in the new language (the save screen,
the camp menu's later pages, the game over screen); what it loaded at its
entry stays (the title's modes, the Mission Mode select, the camp menu's
headings and buttons as pictures of `cmo_&`/`cmb_&`, the field's HUD and
panels, the results). The camp menu's later pages so mix two languages
("Choix de la mission" over "Wähle eine Mission"). A scene entered after the
switch is whole in the new language. Loading the kept databases again works
only with the replaced packs freed before `Scene_AdvanceToPending` makes the
scene's heap again (`HeapState_Recreate`): freed after, they faulted the
heap's walk in `Ov027_ExitScene`. A scene ends only by itself
(`Scene_AdvanceToPending` waits for `Instance_ReleaseIfDead`), so the screen
on show cannot be built again without leaving it.

**What the port does when the language changes** (`port/runtime/core/language.c`):
the settings menu's request waits for the scene on show to end; at
`Scene_AdvanceToPending`'s hook (the scene has ended, the next is not built
yet) the halfword and the firmware's take the new language, so every scene is
built whole in one language. The session log says each request and each
switch, with how many databases are still loaded then.

## 9. Movies

The game's 46 movies (`mv/*.mods`, 256x160; 37 with stereo sound, all of
audio kind 3, 9 without) and how ov024 plays them, read in the decomp
(`src/overlays/system/ov024_mobiclip`, `libs/mobiclip`) and seen with
`KHDAYS_TRACE_VIDEO`, `KHDAYS_PROFILE` and the checks below, 2026-10-02.

**The path.** `Ov024_MobiClip_OpenContainer` sets up the decoder's state
(0x454 bytes: the lookup table `data_ov024_0208a7c4` at +0x3c,
`data_ov024_020886c4` at +0x40, the 8-bit clamp table + 0x40 at +0x44, the
size) and copies the frame decoder -- a position-independent ARM payload,
0x0208c8c4 -- to the ITCM (0x01ff9a64). A frame alarm
(`Ov024_MobiClip_FrameAlarm`) paces the frames from the stream's time base;
`Ov024_MobiClip_DecodeFrame` decodes one into the newest of the plane ring,
and `Ov024_MobiClip_BlitFrame` turns it into 15-bit pixels with
`Ov024_MobiClip_BlitRows` (YCoCg through the 5-bit ramp
`data_ov024_0209352c`, the checkerboard dither) into the front buffer: on the
story walk four of them, 160 rows of 512 bytes -- `0x06008000` and
`0x06208000` (BG VRAM of engines A and B), `0x06400000` and `0x06600000` (their
OBJ VRAM). The opening shows on the upper screen with its subtitles.

**Native** (`port/tools/prepare.py` HOOKS, `port/runtime/movies/video.c`): the
decoder is the decomp's portable reconstruction
(`libs/mobiclip/video/portable`, `MobiClip_DecodeFrameCore`), called where
`Ov024_MobiClip_DecodeFrame` calls the payload; the payload is still copied
to the ITCM, as the game does, and nothing runs it. `Ov024_MobiClip_BlitRows`
is C translated from its assembly (`port/hal/sdk_mobiclip.c`). The sound:
`Ov024_MobiClip_StepAudio` decodes kind 3 (`Ov024_MobiClip_DecodeAdpcm`) in C
into rings that channels 0 and 1 play; kind 1's `Ov024_MobiClip_AudioTransformDecode`
and kind 2's `func_ov024_02087318_unk` (FastAudio; both assembly) are never
reached -- every movie of this game with sound is kind 3. Nor is the
deblocking filter `func_ov024_02092e60_unk` (assembly), which
`Ov024_MobiClip_BlitFrame` runs for a mode other than 0: the one call
reaching it, from `Ov024_MobiClip_FrameAlarm`, passes 0.

**Checked** (while the port had the ARM interpreter, until 2026-10-02; the
checks left with it). In the game (`KHDAYS_VIDEO_CHECK=1`, every frame of the story
walk's two movies; its last report, 1500 frames, 0 differing): each frame
decoded by the native decoder from the payload's state and planes, and the payload's frame, equal in the bytes used,
the planes and what the decoder keeps (the format bit, the quant/scan tables,
the quantizer, the coefficient table); a shadow decoded by the native decoder
alone, frame after frame, equal to the payload's every frame; every blit equal
to the ROM's routine's. Outside it (`--check-movies`): the 46 movies, 37,185
frames, decoded and blitted by the native code and by the ROM's payload and
blit interpreted, each with its own state from the first frame: every frame
the same.

**The picture** (`port/runtime/movies/video.c`, `tiles2d.c`, `gfx/compose.h`):
after each blit the port keeps the frame where it went -- the VRAM bank and
offset of the front buffer, what the blit wrote there -- and converts its
planes at full precision: the blit's own equations (G = Y + Cg, R = Y + Co -
Cg, B = Y - Co - Cg), clamped to 0-255, where the ROM's 5-bit ramp
(`data_ov024_0209352c`: 0 below 0, v >> 3 up to 255, 31 above) and the
checkerboard dither (the anti-diagonal pixels of each quad four levels lower)
are left out. The picture is a replacement that changes (`textures.h`
`khdays_textures_add_changing`, a new version each frame, uploaded again by
`host/render/gpu_hd.c`), and the 2D's HD map marks every 32 bytes of the buffer that
still hold what the blit wrote; a direct-colour bitmap layer or a bitmap
sprite reading them draws the picture there (`kd_hd_bitmap`), at its own
size, the subtitles and everything else over it as the game sets them, the
screen composed in 8 bits a channel (`KHDAYS_WANT_MOVIE`). `[video] movies =
ds` keeps the DS's pixels. Checked with a window and `KHDAYS_GFX=check` (the
GPU's 2D against the DS's 2D engines at 1x) on the story walk: with `ds`, 0
pixels differed in every report; with `pc`, only while a movie plays, about
40,960 pixels a screen (256x160) -- the opening and the clock-tower cutscene
of scene 2. Seen side by side, the DS's checkerboard and the port's smooth
picture.

**The sound** (`port/runtime/sound/sound_bridge.c`): the channels
`Ov024_MobiClip_StartAudio` locks (mask 3: channels 0 and 1, the left and
right rings) are the category `movies` until
`Ov024_MobiClip_ReleaseAudioChannel` unlocks them, with their own volume
(`[sound] movies`); the samples are the game's. Seen (a window,
`KHDAYS_AUDIO_WAV`, the story walk's opening, 27.8-34.7 s of the session
log): at `movies = 50` the movie's sound has half the level it has at 100
(RMS ratio 0.500), the title's music before it the same (0.993).

**A mod's movie** (`port/runtime/movies/video_mod.c`; `mods/<Mod>/movies/<id>.mpg`,
pl_mpeg): `Ov024_MobiClip_OpenStreamSlot` gives each stream's path as the game
built it -- the opening's is slot 1's, `/mv/802.mods`, its only stream -- and
`Ov024_MobiClip_StopPlayback` its end; a movie of one stream whose `<id>` a
mod has plays the mod's picture (a canvas of the game's frame's shape,
256x160, the mod's frame in its middle) where the game's frames show, at the
game's frame index over the movie's frame rate (the header's 0x14, 24
fractional bits), and its sound (a ring filled ahead, from the first frame,
resampled to the output) in the movie channels' place. Seen on the story walk
with a test video (1280x720, 30 frames a second, a 440 Hz tone): the test
picture under the game's subtitles, its clock at 4 s some 4.5 s into the
movie; the tone in the sound only while the movie plays; START's skip
stopping it; the same scenes. A truncated file: the game's movie, said once.

**Times** (the story walk, Release, the game's thread): interpreted, 2.19 ms
to decode a frame and 2.34 ms to blit it; native, 0.25 ms and 0.03 ms.

**HD videos in the movies' place** (`port/runtime/movies/movie_hd.c`,
`port/host/movies/movie_hd_win.c`; the player's `data/cutscenes/cinematics/hd<id>.mp4`,
`[video] movies = hd`). Read in the decomp for it:

- Every movie is bracketed by `Session_SetMoviePlaying(1)` and `(0)`
  (`Ov024_MobiClip_StartPlayback`, `Ov012_StartOpeningMovie`; the ends of
  `Ov024_MobiClip_UpdatePlayback` and `Ov012_RunOpeningScene`).
  `Ov024_MobiClip_OpenStreams` opens up to three streams: stream 0 on the upper
  screen, 1 and 2 on the lower; `Ov024_MobiClip_StopPlayback` ends the movie, after
  which the player's action script may start another before its teardown.
- The skip: `Ov024_MobiClip_UpdatePlayback` reads START from KEYINPUT on a rising
  edge (`nSkipLatch`, seeded by `Ov024_MobiClip_OpenPlayer` with START as held at the
  open) only when the player's flags (root heap + 2) have bit 3, which
  `Ov024_MobiClip_OpenPlayer` sets from the argument the decomp names `bSubScreen`
  -- it is the skip's permission, a name for the decomp to reconsider; the skip
  sets bit 2 and a fade of 16 steps (`Ov024_MobiClip_StepScreenFade`). The opening
  (`Ov012_RunOpeningScene`) reads START the same way with no bit-3 condition.
- The ending: the credits scene, ov011 (scene 8), plays "843.z" from `UI/sf/m.p2`
  through `Ov024_MobiClip_InstallStreamSourceVtbl` (not `mv/843.mods`), then the
  staff roll. The title's Theater enters it too: `Ov000_ActivateSceneObject`
  sends list entry 0 to the opening (ov012) and entry 27 to ov011, which
  `Ov000_LeaveTitleForScene` instantiates with argument 1; after the movie, with an
  argument and the movie skipped (`data_ov024_02093a20`), the scene leaves at once,
  otherwise the roll runs (`Ov011_TickMoviePlaybackState`).

The course: when a stream opens (the hook of `Ov024_MobiClip_OpenStreamSlot` above)
for a movie with an HD video, the window shows the video over its whole area with
its sound (an SDL audio stream of its own, the video's clock; the picture an NV12
texture), and the game plays its own movie behind it, unseen, silent (its output's
gain 0) and given none of the player's buttons. At `Ov024_MobiClip_StopPlayback`
the game is held (the clock's movie reason, as the settings screen holds it) until
the video ends. START where bit 3 says the DS lets skip: the video stops, and if the
game's movie still plays the port gives it START until bit 2 says it took the skip.

Seen (the story walk's script with a window, Release, 2026-10-05): the opening opens
stream 1 (`/mv/802.mods`) and its HD video in under 0.1 s; the walk's START stopped the
video at once, the game took the skip and its fade and went on 0.26 s later. Scene 2's
movie is `/mv/818.mods` (stream 0): its HD video ended 0.39 s before the game's movie,
its last picture kept meanwhile; 6,419 pictures shown, 35 passed over (0.55 %), each at
most 6.7 ms off the video's clock. The scenes the same as without HD (1 11 5 2 10 5 2).
SDL's audio stream keeps its resampler's last frames until it is flushed (56 bytes at
the end of `hd818.mp4`): the video's end waits for them, so the stream is flushed at the
sound's end. A file cut short (its pictures ending well before the length its header
says) fails as a broken one does: the game's movie shows from where it is.
`KHDAYS_TRACE_MOVIE_HD=1` prints, once a second while a video plays, where it is (its
clock, the pictures decoded and taken, the sound queued, the ends found); at each video's
close stderr says how many pictures it showed and passed over.

The credits scene in the decomp: ov011's stream interface opens through
`Ov024_MobiClip_CreatePlayerTask`, which instantiates the class at `data_ov024_02093904` with
`{resource, argument != 0, "843.z"}` -- the shape of `Ov024_MobiClip_OpenPlayer`'s
`{sector, bSubScreen, path}`, so the movie's skip permission is the scene's argument
(read, not yet seen running). Its staff roll (`Ov011_TickTitleMenu`): with an argument
START leaves it (mode 4) and its pauses last 180 frames; without one its pauses wait for
A; its natural end is mode 3's `Ov011_StartTitleExit`. The HD video for 843 (388.8 s)
carries the roll too, so the course is not held at the movie's end: the roll runs on
behind the video, held at `Ov011_StartTitleExit` if the video still plays, and the
video's end shows the game wherever the roll is (its pauses wait for the player's A).
**Not yet seen**: a run through the Theater's entry 27 (with a copy of the player's
save) and the story's own ending (scene 8's argument).

**Seen, not identified**: decoding a frame with the payload from the state
the native decoder left after the frame before -- not its own -- goes wrong
(the opening's fifth frame: 2,258 bytes used against the native decoder's
194), so the payload reads, from one frame to the next, a field of the state
the portable decoder treats as the frame's scratch. Which one is *not
identified*; neither decoder on its own is affected (the checks above).

## 10. Time and interrupts

The game's time is the port's own: the PC's clock, counted in the
DS's cycles (33,513,982 a second, `port/runtime/core/clock.c`) -- the unit the
SDK's tick, the sound driver and the game's data count in -- less the time the
settings screen pauses.

**The frame loop** (`port/runtime/core/events.c`): a VBlank every 560,190 cycles
(59.826 a second, the game's rate); at each, as before, the frame the game
finished goes to the host, the input for the next, the VBlank interrupt's bit,
the DMAs armed for VBlank and the ARM7's per-frame services. The game's main
loop (`src/engine/main.c`) waits one VBlank to begin a logic frame and, in
frame-rate mode 0, one more: one logic frame per two VBlanks.

**The SDK's tick and alarm timer** (`port/hal/sdk_time.c`, prepare.py
HOOKS): NitroSDK's tick is timer 0 at 64 cycles a tick, its overflows carried
by `OSi_CountUpTick`; its alarms wake on timer 1, which `OSi_SetTimer`
programs to overflow `delta + 1` ticks on for the queue's head (2 when past
due, 0x10000 at most). Natively `OS_GetTick` is the clock's cycles / 64 plus
an offset (`OS_InitTick` makes it 0; `OS_SetTick` -- NitroSDK's
`OS_SetTick` -- sets it), `OS_GetTickLo` its low 16 bits, and `OSi_SetTimer`
and `OSi_ArrangeTimer` set and clear a deadline in cycles that the frame loop
raises timer 1's interrupt at; the queue, its handler and their callers stay
the decomp's C. No timer register is touched (`KHDAYS_TRACE_TIMING` on the
four walks, 2026-10-02: 0 accesses, against 19,355 to 175,145 a walk before).
Alarms on the walks: only the movies' (`Ov024_MobiClip_FrameAlarm`,
`Ov024_MobiClip_TickSlot`); the frame alarm re-arms itself one tick ahead while
no frame is primed, so its interrupts count how often one is delivered.

**The pause scene and the time**: `Game_EnterPauseScene`
saves the tick and the VBlank count and `Scene_Leave` restores them -- but only
when `data_0204c240` bit 2 or the session mode's bit 1 is clear. Mission
Mode's field pause (START; "PAUSA", Continuar / Retirarse) goes through both
with `data_0204c240` = 0x0f and the mode 0x0002: the time runs on through the
pause. The Story Mode field's START menu (the run07 walk) does not go through
them. The restore path is reached by neither.

**Mission Mode's pause and the port's Retry** (2026-10-10, read in the C and
seen on the mission walk with `KHDAYS_TRACE_UPDATES` and `KHDAYS_TRACE_RETRY`).
The pause draws its lines in engine A's BG2 (screen base 0xf800, characters
0xc000: `pause/pausebg_&.pbg`'s, the strips `pause/etcicon.NSCR`'s, the title
`pauseicon.NSCR`, "Are you sure?" `pausemsg.NSCR`), 11 cells from cell 10:
"PAUSA" on row 8, Continue on row 11 and Give Up on row 13, the one the cursor
is on lit (red, the hand in the strip, blinking between two strips every 16
frames: `Input_DebounceSlot`). No sprites. The words are the 8-dot font's
(`text/font_eu_08.nftr`, white with a shadow one dot right and down).
With the pause kind's bit 1 (Mission Mode's) the context (`data_0204be08`'s
second word) has three entries (+0xd0), the middle one never drawn
(`SubObject_NudgeAndRedraw`) and passed over by the cursor (+0xd4,
`TabPanel_HandleUpDown`); A on Give Up opens "Are you sure?" (two entries: Yes
on row 13, No on row 15; No, at first), whose yes calls
`Ov002_PauseMissionScene`. Give Up then: the battle hub fades out
(`Ov022_StateAdvanceAfterPause`, `Ov022_StateCloseOv106Scene`), the field's
`Ov002_FinishSaveStep` -> `Ov002_EnterResultScene` -> the game over (ov027:
`Ov027_GameOverFadeIn` -> `Ov027_PollConfirmAndAdvance`, whose A sets its mode
to 8) -> `Ov002_RetireSubFlow` (flag 0x2087) -> `Ov002_PickNextPhase` ->
`Ov002_StepSessionMenu`, which asks for scene 6 (the results), the camp (19)
after them. The port's Retry (`runtime/menus/retry.c`, `ui/pause.cpp`; the pause part,
on by default) shows three lines in a mission played alone (the session
setup's players, +4, 1): Continue, Retry on row 13 (a line of the game's, its
words taken out, the port's written in that font), Give Up on row 15; the
game's cursor is kept on Continue or on Give Up, so A on Retry opens the game's
"Are you sure?". After its yes the game over is given mode 8 at once (it
leaves dark) and `Ov002_StepSessionMenu` asks for the field again (scene 2,
as `Ov008_MainMenuExit` starts a mission) with flag 0x2087 cleared: the field
starts as from the camp (`Ov002_EnterSceneIfValidated` ->
`Ov002_BeginMissionRun`, the event scripts loaded again), Roxas at the
mission's start with his full HP.

**The story's challenges** (2026-10-17, challenge 07 from the story camp's
Holo-Missions with the player's save, `KHDAYS_TRACE_RETRY`): a challenge sets
`data_0204c240`'s first byte to 3 (`Ov025_MissionList_StartMission`: bit 0 a
Holo-Mission, bit 1 a challenge; Mission Mode's is bit 2) and copies the
challenge's targets to `data_0204c254`. Its opening words are a scene's: the pause
then is a scene's (the mode word 4, two entries, Continue and Skip). In play the
pause is Mission Mode's (the mode word's bit 1, three entries, Continue and Give
Up), and Give Up's yes goes straight to the results (scene 6, from
`Ov002_StepSessionMenu`'s case 0, no game over), so the port's Retry runs there as
in Mission Mode. One thing differs: with bit 1 and not bit 2, the field's
construction multiplies the three targets at `data_0204c254 + 0x10` by 100 when
its u16 at +0xe is 0 (`Ov002_ConstructGameplayScene`), and only the challenge's
start copies them in, so a Retry would leave them 100 times larger each time; the
port divides them back by 100 when it takes the Retry. Seen: targets 105 and 140,
10500 and 14000 in play, the same after a Retry; the field again, without the
results.

What the pause draws is in its panel's draw entries
(`SubObject_SetDrawParams`: the context's +0xc, then +0x18 a slot, 0x18
bytes each -- source cell x, source row twice, destination x and y, width,
height, in cells) and each slot's lit strip (the context's +0xb0, 8 bytes
apart: 0 not lit, 1 and 2 the two it blinks between). Slots 0-2 are the
lines, from `etcicon.NSCR` (33 x 12 cells, kept as a 32-cell block and a
1-cell one): Continue at cell 0,0, Skip at 11,0, an empty capsule at 22,0
(the middle entry Mission Mode hides), Give Up at 0,6, Yes at 11,6, No at
22,6, each 11 x 2, its two lit strips the next two pairs of rows; slot 3 the
title (`pauseicon.NSCR`, at 10,8, 12 x 2), slot 4 the question
(`pausemsg.NSCR`, at 9,11, 14 x 2). The first page draws 3, 0 and 1 (a
scene's: Continue, Skip; BG3), or 3, 0 and 2 (Mission Mode's: Continue, Give
Up; BG2); only 3 with game flag 0x2483 (`SubObject_SetupDrawsForMode`);
"Are you sure?" draws 4, 0 and 1 (`SubObject_SetupDraws`). The words, read
from the strips (2026-10-10): English Continue, Skip, Withdraw, Yes, No, PAUSE,
"Are you sure?"; French Reprendre, Passer, Abandonner, Oui, Non, PAUSE, "En
êtes-vous sûr ?"; German Fortfahren, überspringen, Abbrechen, Ja, Nein,
PAUSE, "Sicher?"; Italian Continua, Salta, Ritirati, Sì, No, PAUSA, "Sei
sicuro?"; Spanish Continuar, Omitir, Retirarse, Sí, No, PAUSA, "¿Seguro?".
The pause part reads these (`khdays_pause_view`) to draw a mod's pictures of
each piece in its place (`ui/pause.cpp`, `tools/hd/hd_pause.py`).

**The display line**: no scan of 263 lines. VCOUNT's
readers are two: `Ov024_MobiClip_PresentOnVBlank`, which runs in the VBlank
interrupt's task list and flips a movie's front buffer only while the line is
in 0xa0-0x103 -- it is answered 192 while the VBlank handler runs, where the
DS reads it -- and `OS_GetLowEntropyData`, entropy, answered from the clock.
DISPSTAT's VBlank bit, which `SetMasterBrightnessMain` and `...Sub` read to
apply a brightness at once or leave it to the VBlank handler, is set for the
DS's VBlank length (149,100 cycles) after each VBlank, and while the VBlank
handler runs. `OSi_CancelDma0` no longer waits for line 0: the port's DMAs run
at once, so none is in flight. Before, the port computed the line from the
time since VBlank, and a VBlank handler delivered late missed the movie's
window: on the story walk 1,959 of the presenter's calls fell outside it, and
a frame waiting at such a call waited a VBlank more; now none does (1,720 frames shown, the opening
skipped with START as the walk does).

**Where interrupts are taken**: where the game waits
(`OS_Halt`, the SDK's waits: `khdays_runtime_wait`), where it unmasks them
(IME set, the CPSR's I bit cleared), and in the loops that spin on something
only an interrupt advances, each given a wait point (prepare.py HOOKS,
`khdays_wait_point`: halt until an interrupt is due, take it, check again):

| Loop | Waits for | Advanced by |
|---|---|---|
| `Ov012_RunOpeningScene`'s `do ... while (Ov024_TickStreamSlots() == 0)` | the opening movie's streams to drain; its timeline steps when the VBlank count changes | the movie's frame alarm (timer 1) and the VBlank handler |
| `Ov024_MobiClip_UpdatePlayback`'s `while (Ov024_TickStreamSlots() == 0)` | the other movies' streams | the same |
| `TP_WaitBusy` | the touch panel's busy bits | the PXI callback (`TPi_TpCallback`, IPC receive) |
| `PMi_WaitBusy` (interrupts enabled) | the power manager's reply flag | the PXI callback (IPC receive) |

The first two were found by the walks (without the watcher the story walk
stopped as the opening began) and by where the watcher's injections landed
(`KHDAYS_TRACE_IRQ=2`: 11,353 in `Ov024_MobiClip_UpdatePlayback`, 716 in
`Ov012_RunOpeningScene` on the story walk); the last two from the C (no walk
reaches them). The rest of the injections landed in computation -- vector
maths, matrix loads, NitroSystem's node blending -- which takes the interrupt
at its next unmask or wait instead: VBlanks merged into the one before (the
first not yet taken when the next came) on run07 without the watcher, 3, at
loads, against 2 with it. The watcher (`async_irq.c`), which suspended the
game's thread to deliver between two instructions, was off by default from
then, and has left the port with the models of the DS's timers and display
scan (below).

**No model of the timers or the scan**: `io_regs.c` keeps no
timer and computes no display line. A game access to a timer register or a
VCOUNT read is said once with the game's instruction that made it (`io: ...
by <function> (<address>): no model of it, 0 read`) and reads 0; none on the
walks. DISPSTAT's VBlank bit comes from the frame loop; its V-count match bit
reads 0 (the game sets no V-count interrupt).

**Measured** (`KHDAYS_TRACE_TIMING`, Release, the four walks, 2026-10-02, the
same scenes as before): logic frames 300 per 600 VBlanks in every stretch
without a load, as before; in load-heavy stretches more than before (run07:
259-284 against 240-267 a 600-VBlank stretch), the trapped timer reads gone; no
second with the sound's output run dry.

## 11. The game thread's cost

**The trap was the field's slowdown** (2026-10-02, a player's report: 20-24
logic frames a second in the field once enemies appear, 30 on the DS). A
script that fights in the play walk's mission (Release, with a window,
`KHDAYS_PROFILE`): the game's thread ran 640-650 ms of every second in the
field -- about 20 ms a logic frame, against the two VBlanks (33 ms) the main
loop paces it to -- with the default video settings and with the player's
(internal resolution and aspect = window, texture smoothing) alike: not the
graphics card's work. Stack samples (`KHDAYS_STALL_SAMPLES`): about half of
the busy samples in Windows' exception dispatch (`KiUserExceptionDispatcher`,
`io_handler`). The `io:` lines counted about 100,000 accesses a second
faulting into `io_trap.c`, each 6-9 us in a 32-bit process on 64-bit Windows:
the math unit (divider and square root) from NitroSDK's vector maths
(`VEC_Normalize` 16,000-18,000 a second, `FX_DivAsync`, `VEC_Mag`,
`VEC_Distance`, the ITCM's normalize pair `func_01ffa7fc`), the port's own
interrupt dispatch (`khdays_irq_deliver`, `func_01ff8148`: 16,000), its matrix
copies to the geometry ports (`MI_Copy64B`, `MI_Copy36B`: 16,000),
`OS_EnableIrqMask` / `OS_DisableIrqMask` (13,000), NitroSystem's node
blending, the DMA functions and a few direct geometry stores.

**Without the fault** (the same registers, the same values): the HAL reads
the interrupt registers and the math unit's operands in the register model's
own view, acknowledges IF through it and copies to and from registers through
it word by word (`khdays_io_load`, `khdays_io_store`); the decomp's C reaches
the math unit and IME / IE through `khdays_io_cp` / `khdays_io_plain`
(`compat/include/nitro/hw.h` for the SDK's macros, prepare.py
`UNTRAPPED_REGISTER` for casts of their addresses). The math unit computes when
its results are read, from the operands as they stand -- the values the
hardware gives once no longer busy, which every reader waits for (the busy
bits are never set). Measured the same way: the field's 651 ms a second down
to 128, the faults down to about 17,500 a second; the game's thread idle 78%
of the samples.

Then the SDK's own accesses through pointers (prepare.py HOOKS, through
`khdays_io_load` / `khdays_io_store`: the DMA functions `MIi_DmaSetParams`,
its ITCM twin `func_01ff85d0`, `MIi_CheckAnotherAutoDMA`, `MI_WaitDma`;
GXSTAT in `G3X_GetClipMtx` / `G3X_GetVectorMtx`; NitroSystem's node blending
`NNSi_G3dFuncSbc_NODEMIX`'s matrix commands) and a word stored to a geometry
port through a cast of its address (prepare.py `GEOMETRY_STORE`: `G3_LoadMtx43`,
`G3_MultMtx43`, `G3_MultMtx33`): the field's 128 ms a second down to 43, the
faults to about 1,300 a second (the matrix stack's resets and level reads,
NitroSystem's billboards, the ARM7's FIFO). The sound ran dry no second.

**Without the trap** (2026-10-03). Counted first (`KHDAYS_TRACE_IO`, while
the trap was there: each trapped access by the game's instruction, register,
width and direction), the five walks made about 2.2 million trapped accesses,
1,305 kinds in 215 functions: the geometry's ports, the DMA's control,
EXMEMCNT, GXSTAT, engine B's 2D... Then `port/tools/registers.py`, which
prepare.py runs on every decompiled file, gives each access its register's
kind, read off `io_regs.c`:

- a register that only holds a value (the 2D of both engines, the DMA's
  addresses and fill, EXMEMCNT, POWCNT, IME, IE, the 3D's settings) is
  reached at its place in the register model's memory (`khdays_io_plain`, a
  constant address: that memory is a static array, `memory.c`);
- one that acts when written (DISPSTAT, the DMA's control, IF, VRAMCNT, the
  card's ROMCTRL, GXFIFO, the geometry's ports, GXSTAT) or is computed when
  read (DISPSTAT, KEYINPUT, the geometry's results, the math unit, the card's
  data port) through `khdays_io_store` / `khdays_io_load`; a read of one that
  only acts on a write (ROMCTRL's busy bit, IF) is the model's memory too.

The C spells an address many ways, and the rewrite brings each to one
literal first: its own macros and the shared headers', constant sums
(`HW_REG_BASE + 0x440`), `#define NAME(p) (*(T *)(p))`. A struct laid over
registers is laid out from its definition. A pointer set to a register is
followed through its uses: reaching value registers only, at constant
offsets, it points into the model's memory; otherwise it keeps the DS address
and each access through it is routed at `&(lvalue)` -- right whatever the
kind, and for addresses computed at run time and pointers the C moves (a DMA
channel's, a timer's). The SDK functions given a register's address
(`G2x_SetBlendAlpha_` and the like) reach as far as their own C says; the
port's routines (`MIi_CpuSend32`...) and the DMA's take the DS address as it
is. Five files the rewrite does not follow were read by hand (its
`REVIEWED_PLAIN_FILES`); `compat/include/nitro/mi.h` routes the DMA's waits;
two hooks (`MIi_DmaSetChannelRegs`, `CARDi_ReadCard`'s data port). What
still names a register in another form is listed in
`build/port/gen-x64/registers_left.txt` -- empty at this revision.

The five walks then make no trapped access, in Debug and in Release; the
fight script's field costs the game's thread 36.8 ms a second (38.8 before,
the same script). **The trap is gone** with it: the registers' DS addresses
are only reserved, and an access there -- one the rewrite did not reach --
stops the game, naming the register, read or written, and the game's
instruction and function, exit code 4 (`io_guard.c`; `KHDAYS_TEST_IPC=1`
makes one). Nothing decodes or carries out an instruction any more.

## 12. The ARM7's services

What the game asks of the DS's ARM7, as measured on the walks
(`KHDAYS_TRACE_PXI=1`, the five walks, 2026-10-02) and read from the C.

**What it sends.** Four services, the same messages on every walk:

| Tag | Service | What the game sends | Sent by |
|---|---|---|---|
| 13 | the GBA slot | command 1 (INIT_MODULE_INFO), once at boot; the slot is empty | `CTRDG_Init` |
| 6 | the touch panel | set stability (command 3) once; automatic sampling on (1) and off (2) at each scene that uses the stylus; the ARM7 sends a sample notice (0x10) each frame while on -- 13,000 to 45,000 a walk | `Ov001_InitTouchPanel`, `Touch_StartAutoSampling`, `Obj_Destroy` |
| 7 | the sound | the command lists' addresses and "process" words; the driver's replies | `SND_FlushCommand`, `RequestCommandProc` |
| 11 | the backup memory | INIT and the command block's address, IDENTIFY, READ (200-650 a walk); the save walk PROGRAM (8) and VERIFY (9) | `CARD_IdentifyBackup`, `CARDi_RequestStreamCommandCore` |

None reaches the clock (5), power management (8), the microphone (9), the
wireless (10), the OS (12) or the card's pulled-out notice (14). From the C:
the ROM links only two RTC requests, the time (0x12, whose one caller is the
wireless code's `RTC_GetTime`) and status 2 (0x27, sent in answer to an
alarm notice of the ARM7's), and no request for the date; every
`PM_SetLCDPower` (`main`, `Ov011_TickTitleMenu`, `Ov012_RunOpeningScene`,
`Ov024_MobiClip_UpdatePlayback`) and `PM_GoSleepMode` is behind the hinge bit
of the word at 0x027fffa8 (1 = the lid closed), which a PC never sets;
`OS_ResetSystem` is the port's own. The shared VBlank count the DS's ARM7
keeps at 0x027ffc3c -- read by power management and the wireless code,
written by nothing on the ARM9 -- is not kept (the multiplayer
specification's).

**The transport** (`port/runtime/core/pxi.c`). NitroSDK's PXI is the port's own:
the three SDK functions that touched the IPC registers (`PXI_InitFifo`,
`PXI_SendWordByFifo`, `PXIi_HandlerRecvFifoNotEmpty`, prepare.py HOOKS) hand
their words to it. A send goes to the ARM7's service for its tag at once; a
reply waits in a queue that grows as needed, and while one waits the receive
interrupt's request (IF bit 18) is raised, so the SDK's handler takes it at
the port's delivery points (section 10) -- in practice at the
`OS_RestoreInterrupts` after the send, as before. The callback table, the
interrupt's installation, the masks and the dispatch are the SDK's. The
touch panel's frame samples are skipped while a touch reply of an earlier
frame still waits (the ARM9 behind): none skipped on the walks, before or
after. No model of the IPC registers is left, and the game's C reaches none
of them: an access to IPCSYNC, IPCFIFOCNT, IPCFIFOSEND or IPCFIFORECV at its
DS address stops the game naming it (section 11; `KHDAYS_TEST_IPC=1` makes
one at start to see it).

**The save.** The backup chip stays in memory, loaded from
`saves/khdays.sav` (erased past what the file holds). When a save action of
the game's ends -- the end of `CARDi_RequestStreamCommandCore`, through which
every backup write, program and erase goes -- a changed chip is written to
the file whole: a temporary file beside it, flushed to the disk, renamed
over it (`host/app/files.h`). A crash leaves the old save or the new one; a
commit that fails leaves the file as it was and is the game's failed save
(`CARD_RESULT_FAILURE`). The camp menu's Save writes the file twice, once
per copy of the slot the game keeps (the slot's two copies of 0x2018 bytes,
each signed with SHA-1: `port/tools/save_file.py`), about 2 ms each. The
file keeps its length or reaches the end of what the game wrote.

**A request without an answer** is said once for its tag and kind, with the
game's functions that sent it, and refused: the error bit on the request's
own words (each library's callback takes it as a failed request), or, for
the backup, a failed result in the command block. Nothing in the walks is
refused.

## 13. The game's memory

Where the game's memory is, the game's C rewritten with references
(every build since, the 64-bit one; until 2026-10-03 also a
32-bit build, with or without them), as measured on the five walks
(2026-10-03).

**A pointer of the game's is a reference**: 4 bytes, the DS address it had.
The game's C and the HAL are rewritten so (`port/tools/references`, run by
`prepare.py` on all 24,531 files): a dereference adds the
window's start to the reference (`KHDAYS_W`), pointer arithmetic is scaled
by the pointee's size, an object at a DS address is its address as a
constant, a function value is its function reference. The game's own logic
is unchanged; its numeric offsets (`*(int *)(ctx + 0x2c0)`) and its words
taken for pointers mean what they meant on the DS.

**The window** (`runtime/core/memory.c`) is one range Windows places -- at
0x11500000 in the 32-bit process on the maintainer's machine, above 4 GB in
the 64-bit one; the session log names it -- each region at the window's
start plus its DS address: main RAM viewed at its mirrors, the TCMs, the
WRAMs, palettes, VRAM, OAM, the GBA slot's read-only granules; above the
DS's ranges the register model's memory (0x0b100000), the data area
(0x0b200000: the 233 objects the DS names nowhere -- statics, 132 string
literals -- 4 KB) and the stacks of the contexts the game's threads run on
(0x0c000000), the game's own flow on one of them. The rest of the window is
inaccessible, and so are the DS's own addresses outside it: an access there
is one the port's side did not route through the window, and stops the game
naming the address, the access and the function, exit code 6
(`KHDAYS_TEST_DS_ADDRESS=1` makes one).

**A function reference** is the DS address of the function where the
runtime finds its native function by it (`ds_functions.c`: an overlay's
while it is loaded there, an address overlays share by the overlay loaded
now, the ITCM's while its bytes are the game's), else a number the build
gives it (from 0xfe000000; three of them). A call through one finds the
native function (`runtime/core/window.c`, cached until an overlay loads); a call
through one with no native function -- the ROM's code -- stops the game,
naming it. The BIOS's IRQ vector holds OS_IrqHandler's DS address.

**What the C says only the DS's way** -- declarations that agree with their
definition word for word on the DS's stack and nowhere else (DECOMP_FINDINGS
1.5) -- is passed as the DS laid it out: 59 functions take their arguments
in a block of 4-byte slots in the caller's frame, the same on x86 and x64.
The calls through a declaration without a prototype (7,787) were checked
against their definitions too, their arguments promoted: the 8 whose words
differ are calls of those 59. The ABI repairs (`port/abi`: the arguments
passed for missing ones, the narrow parameters and returns, the tail calls
that forward four words) are written in the C, and mean the same on x64.

**The port's side** reaches the game's memory through `KHDAYS_W` and hands
the game references: what it reads at DS addresses (the display, VRAM banks,
the ARM7's shared area, the sound's lists, the card backup's transfers, the
DMA, the movie's blit), the sound archive's path (by its DS address,
`gen-x64/khdays_ds_symbols.h`), the movie decoder's state (the
decomp's portable decoder takes host pointers: `runtime/movies/video.c` hands it
the state as its own and brings back what it records), the ARM9 BIOS's logo
(`boot.c`, the BIOS being in no process's memory).

**The cost.** The fight script's field costs the game's thread 18.8 ms a
second rewritten (32-bit) and 14.3 ms in the 64-bit build, against 24.3 ms
for the build without references (Release, a window). One difference
between them: without references a call through a DS address faults into
`ds_calls.c`; how much of the gap that is was not measured. Its frames are
the same, pixel for pixel, in all three (a first 64-bit run differed in the
arrival portal's particles at one frame, a second matched: not identified).

## 14. The presentation the port draws

The port draws the game's presentation itself in parts (`KHDAYS_UI`,
section 5), each an element standing in one slot of the DS's composition --
a BG layer, or the sprites at a priority -- with what the part stands for
left out of that slot: a text layer's map cells, or sprites
(`port/gfx/elements.h`). What each part draws is read here before its code;
the check (`KHDAYS_UI=<part>=check`) composes the game's
picture and the port's in the same frame and says the first difference.

**The text (gate G1).** Read in the C, and seen with `KHDAYS_TRACE_TEXT` on
the five walks (Release, 2026-10-04):

- *The canvases.* The game draws all its text into NitroSystem character
  canvases (`NNSG2dCharCanvas`), made by two surfaces:
  `TileTextRenderer_Init` (0x0202f834) -- a canvas for one of eight BG
  layers (0-3 engine A's, 4-7 engine B's), its buffer allocated and cleared,
  its layer's map filled by `Tilemap_FillRect` (0x02014174) row by row from
  the frame's first character with the frame's palette -- and
  `TileSurface_AddCanvas` (0x0202fff8) -- a canvas of the surface's size, 4
  or 8 bits by its `field_38`, appended to the surface's list (the list
  object's canvas at +8, its buffer at +0x20). A renderer's canvas is let go
  by `TileTextRenderer_Destroy` (0x0202f9ec); a surface's canvases with their
  surface (`FreeAllListNodeSubBuffers`, the decomp's TileSurface_Release:
  each canvas's buffer, then its object), which leaves the freed structure
  naming its buffer until the heap uses the memory again. The port follows
  the frees (`NNS_FndFreeToExpHeap`): a canvas whose buffer is freed is gone.
  Before that (to 2026-10-10), the tutorial list's heading (ov025,
  `Ov025_Tutorial_LoadBackground`: its characters from `UI/cm/cmb_&.p2`
  member 1 where there is one -- French, German, Italian, Spanish -- else
  `UI/cm/cm.p2` member 0x43's, English) was, in Spanish (seen), read into
  the buffer of the camp menu's canvas of 32x2 and its load taken for that
  canvas's: drawn as its text, torn.
- *How many.* Canvases made and shown on each walk: mission 62 and 50, play
  55 and 43, run07 56 and 43, save 34 and 25, story 25 and 14. The title
  (scene 1) has three surface canvases on engine B's BG3 (18x2, 32x13, 32x6
  characters, 4 bits) and renderers of 32x24 on engine A's and B's BG3 (or
  32x6 on A's BG3 from cell 0,11); Mission Mode's character select (7)
  renderers of 32x24 on both BG3s, engine B's 8 bits; the camp (19) 44 to 51
  surface canvases, 4 and 8 bits (the lists' rows of 20x2, 15x2 and 5x2, and
  22x17, 22x8, 25x30, 20x32, 9x10, 32x2), on engine A's BG1 and engine B's
  BG0 and BG2; the field (2) 15 to 53 (the INFORMATION line's 64x2 on engine
  A's BG1, a map 64 cells wide; 8x24, 8x6, 10x1, 11x2, 15x2, 25x2 of 4 bits;
  the field's menus' 15x15, 20x32 and 25x30 of 8 bits) and renderers of 32x3
  and 32x2 from cell 0,20 (palette 13); the results (6) load into both
  engines' BG2; the monologue (10) a renderer of 32x6 on engine A's BG3.
- *How they reach VRAM.* Through the SDK's character loads
  (`GX_LoadBG0Char`-`GX_LoadBG3Char`, `GXS_LoadBG0Char`-`GXS_LoadBG3Char`):
  those the transfer queue makes (`Gfd_RunTransferTask` 0x020114ac, its
  loader table `data_02041934`: 4-7 engine A's BG characters, 20-23 engine
  B's, 19 and 35 the sprites') and those routines make themselves
  (`Ov008_DrawPageBRows`, `Ov008_FlushMissionListGraphics`,
  `Ov002_BuildTileRow`, `Ov002_HoldPanelScreen`, `Ov002_UploadCaptionChars`,
  ...): following only the queue, 25 of run07's 57 canvases went unseen. None was
  loaded into the sprites' characters (`GX_LoadOBJ`, `GXS_LoadOBJ`) on the
  walks. Never loaded on them: three or four 10x1 of the field, an 8x2 and
  two of the camp's three 25x30, a 15x15 -- whether something else shows them
  is *not identified*.
- *VRAM is reused.* A canvas outlives its characters in VRAM: another
  canvas's load, or a picture's, lands on them while the canvas lives on (on
  the mission walk 30 times by a load the port follows, 26 by a writer it
  does not), and a picture's map, laid out row by row like a canvas's, then
  shows its characters at cells that match the canvas's first ones. The port
  shows a canvas only while VRAM holds what its loads put there, from the
  origin most of the screen's cells showing its characters agree on.
- *Writers outside the canvas table* (`KHDAYS_WATCH` on bytes the first
  design's copy differed in): `TextWindow_ScrollUp` (0x0202fb8c) scrolls a
  window's canvas by copying its characters -- so the game reads its
  canvases' pixels back; `Ov002_DrawLoadedCaption` (ov002 0x02061eac) copies
  a caption into a canvas's buffer (`MI_CpuCopy8`);
  `Ov008_UpdateCampaignMenuManager` (ov008 0x0204f510) fills a work area
  lying over one (`MI_CpuFill8`); and the heap reuses a let-go canvas's
  buffer. The canvases' pixels are the game's state, which the port leaves as
  the game makes it.
- *Engine B's BG3.* `TileTextRenderer_Init` takes layer 7's map size and
  colours from BG2CNT_SUB (0x0400100c), as for layer 6 -- the ROM's literal
  pool says so, and the C does too.
- *The maps.* A canvas shown is found on its layer's map laid out as
  `Tilemap_FillRect` lays one, row by row from its first character; the
  renderers' frames name palette 15, 13 (the field's bottom ones) or 0 (the
  8-bit one, and the 32x6 ones).
- *The fonts.* The C names `text/font_eu_08.nftr`, `_08s`, `_10`, `_10s`,
  `_10all` and `UI/sf/sffont_8.nftr`, `sffont_10.nftr` (ov011) -- the same
  paths for the five languages; the card's `font_jp_*` are not named.
- *The check.* `KHDAYS_UI=text=check` on the five walks and the fight
  script, Debug and Release, and the story and mission walks in each of the
  five languages (`KHDAYS_LANGUAGE`; the camp's menu is the language's):
  no difference, every 60th frame where the text's elements are (60 to 240
  frames a walk and a screen).

**The field's HUD (gate G2).** Seen with `KHDAYS_TRACE_2D` and
`KHDAYS_TRACE_HUD` (each text layer's cells that show something, grouped
with the cells they touch, with the files their characters came from) on
the five walks every 300 frames (Release, 2026-10-04):

- *BG layers only.* In the field neither screen shows a sprite. Engine A:
  BG0 the 3D, BG1 (priority 0) the text -- the deck's labels are canvases
  -- BG2 (priority 1) empty in play (the pause menu draws there), BG3
  (priority 2) the HUD's frames. The hearts counter at the top left is no BG
  cell: the 3D's, left to the 3D renderer.
- *The pieces*, each where the game lays it out and drawn from its own files
  (`origin.h`):

  | Piece | Screen, layer | Dots | Characters from |
  |---|---|---|---|
  | the command deck | A, BG3 (its labels BG1) | 0,136-88,192 | `UI/btl/<lang>/main.p2` |
  | the portrait, gauge and tag | A, BG3 | 160,136-256,192 | `UI/btl/<lang>/main.p2`, `UI/btl/main.p2` |
  | INFORMATION's bar (its line BG0) | B, BG1 | 0,0-256,24 | `UI/btl/<lang>/bl000.NCGR` |
  | the area's name (its text BG0) | B, BG2 | 32,32-224,48 | `UI/btl/<lang>/bl001.NCGR` |
  | the target | B, BG2 | 32,48-96,144 | `bl001.NCGR`; the picture has no file |
  | the map | B, BG2 and BG3 | 104,48-224,144 | `bl001.NCGR`; BG3 `UI/btl/mapchr.p2` (the player's marker has none) |
  | the mission's gauge (its name BG0) | B, BG2 | 0,152-256,192 | `bl001.NCGR` |

  Engine B's BG3 is the area's map (`UI/btl/mapchr.p2`, all 825 cells of
  the screen), scrolled to where the player is and seen through the map's
  box in the panels over it; the lower screen's panels are one group of
  cells, the pieces regions of it. In the story's field the lower screen shows the tutorial's
  panel instead; the upper screen's pieces are the same.
- *The pieces and the mods' HD sheets* (2026-10-15). The port draws these pieces itself
  (`ui/hud.c`) from their cells' palette indices, so a mod's HD sheet for their characters was not
  drawn there: the portrait of a mod's `UI/btl/main.p2#1/chr/0` stayed the game's though the sheet
  was read. A cell whose character a mod's sheet draws (the tiles' map, `tiles2d.h`
  `khdays_tiles2d_bg_tile`) now takes the sheet's texels at the picture's resolution: an indexed
  sheet's indices with the cell's palette, a sheet in colour in an image of its own over the piece
  (the same id, so it goes where the piece goes). The portraits are that sheet's 13 Organization
  members (48 x 48 each, a smaller one beside it; Roxas the last, at 0,584), each with its own
  palette row; `chr/1` holds Xion, Mickey, Sora, Riku, Donald and Goofy.
- *What else the field shows.* Scene 2 is more than the field: on the walks
  32 different screens were seen in it -- the field with its HUD, the Grey
  Area's camp menus (ov025) and save, the pause menu, the tutorials'
  panels, the event scripts' dialogs, Mission Mode's mission list. A piece
  is the port's only while its region shows its own files' characters, so
  none of those is taken for one.
- *Sprites in a piece.* A piece there takes the sprites whose middle lies in
  its region too -- normal and translucent ones, not affine, not 8-bit in the
  extended palettes -- as elements in the sprites' slot of their priority,
  their entries left out: in challenge 07 the portrait's gauge on engine A is
  six 256-colour sprites at y 181. The mission's notice beads are sprites as
  well (`Ov002_DrawNoticeGauge`, the lower screen's display cells); the
  panel's counter of the mission's chests ("4/4" in mission 18) goes with the
  mission's piece.
- *Reaching a mission* (scripts from the player's save, 2026-10-04): the camp
  menu's Holomisiones, "Mis. normales" or "Desafíos", then a day (the list:
  days 8, 9, 10, 11, 12, 13, 14, 15~, 22, 23, 24, 25, 26~, 51~, 71, ...),
  then its missions (day 51~: 16 to 19, each with its badges and its chests
  found). Starting a mission is no change of scene: the field stays scene 2
  -- the run07 walk's second `s2` therefore never starts, and its steps for
  challenge 07's play never run (its scenes are still the expected ones).
- *On one screen* (a layout's `screen upper`): a
  piece of the lower screen drawn on the upper one in direct colour from
  engine B's palettes; the text in it (engine B's BG0) with it -- an element
  no line places goes with the smallest placed piece holding the middle of
  what the screen shows of it -- and, since engine A's BG0 is the 3D, in the
  BG layer drawn above the others there, above its elements. A first draft
  of the field on one screen (16:9, the play walk's mission) shows the deck,
  the portrait, INFORMATION, the area, the target, the map and the mission's
  gauge together.
- *The badges on the mission's panel* (a port addition, `ui/badges.c`,
  2026-10-08; read in the running game with the player's save, missions 16
  to 19; the badges are section 18's emblems of `evi`). The DS shows a
  mission's badges only in the camp's mission list:
  on each row (`Ov008_DrawMissionRow`) the cells (0x15 + i, 4 x row + 1) of
  its BG2, left of the chest icon, hold the ordeal badge and the blazon (lit
  by bits 0 and 1 of the save's field 0x92b + 4 x the mission's number) and
  the unity badge (bit 3). Which ones a mission has comes from
  `UI/cm/msl_<lang>.msi.z` (the same records in all five languages): a u16
  count, then records led by their u16 size -- +2 the room key (the word the
  list starts the mission with, `data_0204c240` +2), +4 the mission's number,
  +0xc 1 for a record the list does not count, +0x23 the kind: 0 the mission,
  1 its ordeal badge, 2 its blazon, 4 its unity badge; `Ov008_BuildRankMask`
  sets bit kind - 1 for every record of the room key. 82 room keys have
  badges: the ordeal badge only, the unity badge only, both, or all three
  (the blazon's record carries the challenge's number, 100 + the mission's).
  The list's layout (`UI/cm/cm.p2` entry 19) gives its tags 0x82 / 0x83 (an
  ordeal badge or blazon found / not yet) and 0x84 / 0x85 (the unity badge)
  a cell of its fifth screen each, row 4: BG characters 0x81 / 0x80 and
  0x83 / 0x82 -- the map entries KHDAYS_TRACE_HUD read on the rows. The
  characters (8 bits a pixel) are those of `cm.p2` entry 17, the list's BG2
  characters (`Ov008_LoadMissionListGraphics`; a language's `cmb_<lang>.p2`
  entry 7 loaded over them from character 0x80 holds the same four), the
  palette entry 20's (or `cmb_<lang>.p2` entry 6: the same colours). The
  field's panel has the chest icon and the chests' counter only: the counter
  is the two BG2 characters from 0xd3 (`Ov002_DrawPageCounter` writes them
  at 0x1a60), the chest icon the cell left of it ((216, 160) in mission 18).
  The port draws the badges in the three cells left of the chest icon, as
  in the list, lit by the saved bits or by the field context's byte at
  +0x8d4e + 4 x the bit (`Ov002_RequestMode`, set when one is picked up);
  a mission with no chests (11 with badges: 10, 33, 37, 53, 58, 71, 77, 78,
  80, 92, 93, their room keys not in `mi/mi/trbox`) has no chest icon nor
  counter, the panel's cells there as its layout has them (mission 58, Day
  194-197, seen 2026-10-18: row 19 the panel's upper edge, characters 0x090,
  0x091, then 0x092 across; row 20 the name, columns 27-30 blank characters
  0xce-0xd1, palette 10), so the badges take the icon's and the counter's
  place, columns 27-29 of the row under the edge;
  none in Mission Mode (`data_0204c240` bit 1), nor for a number the list
  does not give the room key's mission (a challenge's). The panel's text is
  the list's name of the mission with a full stop in missions 17 and 18
  (mission 18's: 33 letters from x 16 to 165). The longest names with
  badges have 35 letters (Spanish): at that rate about x 178, short of the
  badges' cells at 192 -- an estimate, not drawn. Not understood: with the
  test copy of the player's save whose two copies hold none of mission 18's
  bits, the running game's state holds 1 there (GameState_GetField at the
  list) and the list shows the ordeal badge found. A badge's icon lights
  when it is picked up (the maintainer, playing, 2026-10-08).
- *The crowns on the story's day list* (a port addition, `ui/crowns.c`,
  2026-10-15; read in the running game with `saves/khdays.sav`, every day
  complete, and an older save, some days not). The story's Holo-Missions
  (ov025) list the days on engine B: page B (`Ov025_ScrollList_*`, the
  context's +0x95a0, the context the second word of `data_ov025_020b5744`)
  holds the rows at +4 (0x10 bytes each: the first and the last day of the
  span), their count at +8, the cursor at +0x2c8 and the scroll at +0x2d0 in
  dots, 16 a row (a key press moves it a whole row). `Ov025_ScrollList_BuildRows`
  makes a row of a span of `data_ov025_020b39e4`'s table when a record of
  `msl_<lang>.msi.z` falls in it: +6 its first day on or after the span's,
  +8 its last on or before, +0xc 0 (`Ov025_MissionList_HasVisibleMissionInDays`);
  54 rows in a complete game (8, 9, ..., 15-21, 26-50, ..., 358-399). A day's
  missions show how far they are only on the list that day opens
  (`Ov025_DrawMissionRow`): the cleared seal (field 0x28e4 + 3 x the mission
  is 3: LISTO), "found / total" chests (`Ov025_CountGroupFlagsSet`) and the
  badges as above; the 0-3 of field 0x2a4c + 3 x the mission are Mission
  Mode's crowns, not shown there (0 for mission 15 in the complete save).
  The chests are `mi/mi/trbox` (the game asks for `mi/mi/trBox`;
  `Ov025_RelocateOffsetTable2`): the group count in its first byte, each
  group's offset from the file's start at +4 + 4 x its number; a group's
  room key (u16, the record's +2), its chest count (s8) at +2, from +8 its
  chests 0x14 bytes apart (a group's last one 16 bytes in the file), each
  a found flag less 9 (s16) first -- 89 groups; mission 15 (Agrabah, room
  key 401) has 5, the "5/5" its row shows. Opening days 26-50 with the older
  save shows mission 15 at 4/5: that row has no crown. The list's rows: on
  engine B's BG1 (row text, 8 bits a pixel) over the plates in BG2 and BG3,
  no windows; a row's plate takes x 16-214 and y 18-30 of the row's 16 from
  y 16 less the scroll, and the list shows ten rows, its area the touch area
  `Ov025_ScrollList_Open` gives it (0xe0 x 0xa0 at (16, 16)). The crown is
  the gold one of `cm.p2` entry 17's four (black, bronze, silver, gold:
  characters 0x40-0x47 over 0x60-0x67), its dots in columns 2-15 and rows
  1-13 of its 16: drawn at (195, 17 + 16 x row - scroll) in BG1, it fills
  the plate's height at its right end. Challenges have a list of their own
  (by world, with their stamps), without the day list.

## 15. One screen

With `[video] screens = one` (the default since 2026-10-04; `two` is the DS's
two screens) the window shows one picture. Each frame the port tells the
situation the game is in from what the game runs (15.6), and a composition
places pieces of both screens -- as the port composed them, at the internal
resolution -- in the picture. From 2026-10-04 to 2026-10-15 the situation and
most layouts were another project's, read with the maintainer's permission;
the port replaced each with its own, and nothing of it is left. That
decision looked at the frames before, so a picture standing still was decided
again once a VBlank (`khdays_present_refresh_settling`); the port's own reads
the game's state at each frame and needs no history (`khdays_situation_settling`
is 0).

**The picture** (`port/host/render/onescreen.c`, `port/gfx/onescreen.h`,
`port/shaders/onescreen.hlsl`): both screens are composed as in two-screen
mode, at the internal resolution, with their elements and a mod's HD sheets,
but without master brightness and engine A's 2D apart from its 3D: each
texel K and F such that the screen shows K + F x X for the 3D's colour X
(`compose.h` `kd_split`; every blend the DS makes with the 3D is linear in
it), and 0 where no 2D shows over the 3D. Which layers show is decided by the
3D as drawn at that DS pixel -- the middle 256 columns of the 3D drawn as wide
as the picture -- so a layer under the 3D shows where the 3D is transparent,
as on the DS; with no 3D drawn on the card the 3D counts as opaque there. The
one-screen pass then gives each texel of the picture -- C x 192 DS dots, C the
aspect's width -- the first of the situation's shapes that shows there: a
rectangle of a screen, moved and scaled, with its effects (a colour made
transparent, a colour seen through, dimmed, faded borders, opacity, mirrored,
repeated along its rows or columns, the 3D in its place), an upper shape over
the 3D of its new place; where none shows, the 3D -- the one 3D the DS draws,
engine A's, whichever screen it is on, so a 3D on the lower screen alone is
the picture's. With the 3D on both screens the game moves engine A to the
other screen at every VBlank, one VBlank's 3D the upper screen's view and the
next the lower's: the picture is made on the upper's and the other VBlanks
keep it (before, every other picture had no 3D and the dialog blinked black,
2026-10-05). A situation no section of the compositions names takes the
common rule (`menu`). What the game draws in screen space (not the HUD's),
when it covers the whole DS view -- Sora's memories: the noise's twelve tiles
over the picture -- is stretched from the DS's 256 columns to the wider 3D
(`gpu_raster3d.c` `stretch_overlay`): the wider 3D shows more of the scene
beside the DS's view, but a picture drawn in screen space has nothing beside
it (the port's wider 3D had left the room behind at the sides, the maintainer,
2026-10-05). Screen space covering less keeps its shape (the title's logo,
the day card: unchanged). Then the situation's brightness (the upper
screen's, the lower's, each half's or each piece's own). The HUD's
screen-space 3D (the hearts counter: the polygons the HUD's own drawing sends,
and the game over's orb) moves with the composition's 3D shapes
(`gpu_raster3d.c` `place_hud`), by pieces -- the polygons whose boxes touch,
as widescreen moves them -- a piece with the first shape that holds it whole,
else where the game draws it (2026-10-08: by polygons, the countdown to the
portal at a multiplayer mission's end lost its hundreds digit, wholly left of
the middle, to the hearts counter's shape). Side by side and stacked
situations show both screens whole, without the pass.

**The compositions** (`port/ui/compositions/port_days.txt`, compiled in; a
mod's `layouts/compositions.txt` beginning with `units port` replaces it
whole; the format in `docs/COMPOSITIONS.md`): a layout of the port's for each
screen (15.6). The lengths are the port's units, the HUD size's factor (15.6);
with two screens nothing changed (2026-10-04: the walks, the graphics check
at 1x, 2x 16:9 and 4x 4:3, the fight's cost -- the game's thread 24.8 ms a
second, 24.9 with one screen).

**The game over** (seen 2026-10-05; a challenge reached the run07 walk's way
with a copy of the player's save, the player's HP set to 0 by a temporary
change that was not kept; `KHDAYS_TRACE_SITUATION`, `KHDAYS_WATCH=0204c240`,
`KHDAYS_TRACE_3D_CALLERS`, `KHDAYS_SHOTS_HD` with the window): ov027
(`ov027_game_over`) is the field's sub-flow of kind 9
(`Ov002_LoadSubFlowOverlay`, from `Ov002_EnterResultScene`'s reasons -3 and
-4); the scene stays 2. The one picture tells it by ov027 being loaded
(15.6). Its picture: BG0 the 3D at priority 0 -- the
fallen characters (`Scene_DrawNode` from `Ov027_UpdateModels`) and a floating
orb (two polygons `Ov002_DrawSpriteQuad` sends from
`Ov027_UpdateFlushForward`) -- and **under it** BG1 at priority 2, the menu's
lines (`gameover/gameoverbg_&.pbg`, the strips `Ov027_LayoutChoicePanel`
places, the lit one with the hand). Until then the one picture took the 3D as
opaque everywhere, and the lines never showed (the maintainer's report: only
Roxas and the orb). "Continuar" is left out and the cursor starts on
"Retirarse" when bit 1 of `data_0204c240` is set (the ROM at
0x02082c42-0x02082c54): a challenge from the mission counter sets that byte
to 3, a plain replay to 1 (`Ov025_MissionList_StartMission`); with the bit
clear both lines show (a story mission's: the maintainer's pictures). The orb is the HUD's 3D shown over
the 2D (`prepare.py` HOOKS on `Ov027_UpdateFlushForward`,
`KHDAYS_G3D_OVER_2D`): the game over's 3D shape places it with the lines,
and its pixels (bit 29 of the 3D's colour) go over the shapes with its
alpha, as the DS blends BG0 over BG1 (BLDCNT 0x2641); seen small, at the
chosen line's right end. Widescreen with two screens leaves it in place (the
line crosses the middle). Its 3D shape and the lines are both at the DS's size
(`scale fixed(1)`), so the orb keeps its line at every HUD size (at another
scale than the lines', at size 1 it had left its line for the middle: the
maintainer's picture).

- *The field's HUD, the port's own layout* (2026-10-08, after the
  maintainer's sketches): the HUD of the field and of the field with the 3D
  on both screens in `port/ui/compositions/port_days.txt`. Measured on the screens of missions 17 and 18: the mission's panel is the
  lower screen's 4,152-252,192 (the bevels' outside the backdrop, 8-bit 36
  36 36, made transparent), its text row y 156-168, the separator y 171-172,
  the chest icon at (216, 160) and the counter's glyphs at 228-240; the
  map's box 103,52-225,140 with its frame, its colours white (its ground),
  239 239 247 (its floor), 109 109 109 (its walls), the markers red and
  blue; the INFORMATION banner the lower screen's 0,0-240,24 (its label at
  y 0-8, its line y 9-22 between white lines at y 8 and 23, the player's
  mark at x 240-256). The map is a window of 104 x 64 in the middle of its
  box (the game keeps it scrolled to the player), its white not shown and
  its floor seen through (`see-through`, the port's effect: one colour's
  texels at an opacity over the 3D, the rest as they are), over the panel's
  black stretched, both fading out at their edges, the enemy's health over
  it in the corner as Kingdom Hearts II has them. On one screen the badges'
  cells left of the chest icon hold a badge and its found and total count in
  the counter's own glyphs (`ui/badges.c`). The INFORMATION banner on the DS
  keeps its last message; the port's state `info-new` (host/render/onescreen.c)
  holds for 300 frames after the sum of its line's pixels changes, and the
  banner is shown, over everything, only then. At a mission's start
  (`info-upper`, frames 6466-6786 in mission 17) the game shows the
  mission's information on the upper screen (the same banner, its 0,0-240,
  24) and draws no deck and no portrait (they come at frame ~6810), but the
  lower screen has the panel and the map already: the port's state
  `mission-panel` (the panel's upper edge, y 152 grey 0x6d and y 153 grey
  0xae) shows them from then on, the upper banner where the lower one goes.
  The hearts counter's piece of the HUD's 3D (heart, digits, line) grows
  with the glow when hearts are picked up to x -34.6..62.5, y 0..54.4
  (mission 17, frames 7100-7320): past the screen's left edge, it was not
  wholly inside the 3D shape (0,0)-(128,77) and stayed where the DS draws
  it, under the panel; the shape now starts at (-64, -64) and ends at x
  127.5, short of the middle where the portal's countdown ends its digits.
  `hide3d` hid nothing until then (its rectangle was empty): the pause's and
  a tutorial's lines now hide the HUD's 3D, as their comments say.
- **The label of a mission played again** (2026-10-17, the maintainer's
  pictures; mission 11 in multiplayer with the player's save, and challenge
  07): in the events of a mission played again the upper screen shows a label
  at its top left, rows 0-13 -- "HOLOMEMORIA" in Mission Mode, a challenge's
  "RELOJ PARADO" -- with "Todos pueden pulsar START y omitir" at its bottom in
  multiplayer. An event is the state `dialog`, which shows the upper screen
  whole at the picture's size, so the label was shown large (at 16:9 towards
  the middle). The port's state `holo` (`data_0204c240`'s bits 0-2: the story
  camp's Holo-Missions 1, a challenge 3, Mission Mode bit 2, 7 on the
  multiplayer walk's host; 0 in a story's own mission and the Grey Area) puts
  the label at the picture's top left against its edges, at the HUD's size,
  and the rows under it whole as before; a challenge's label in play goes there
  too. Seen in windows on both copies of mission 11: the label in the corner,
  the line to skip at the bottom.

### 15.1 The title as the HD collection's theater menu (2026-10-09)

Asked for by the maintainer: the title's menu as the 358/2 Days theater of
KINGDOM HEARTS HD 1.5+2.5 ReMIX draws its main menu, with the game's
options. `tools/hd/theater_title.py` makes it from the player's own copy
(nothing of the collection is in the repository) as a mod of animated
pictures (`docs/COMPOSITIONS.md`, `backdrop <file>.txt`); `port_days.txt`'s
`[title]` shows them. They fit inside the window's picture (`backdrop ... fit`,
2026-10-14): covering it, as at first, at 4:3 or 16:10 the 16:9 menu lost its
sides -- the logo, DEVELOPED BY and the settings key cut (a tester's Steam
Deck, 16:10); now its ground shows above and below, and the settings key's
click follows the same scale (`gpu_present.c`).

**What the port reads of the title** (`ui/situation_take.c`, the scene
object's context, `Ov000_TickMenuLoop`'s `Ov000MenuContext`): `+0x0` the
state's counter, `+0x4c2d` the page (0 the first level, else the story level
or, `+0x4c31` altLayout set, the mission level), `+0x4c2e` each page's row,
`+0x4c53` loadAvailable (CONTINUE shown), `+0x4c54` extraOptionAvailable
(THEATER MODE shown); the language: the one the player just chose
(`khdays_language_wanted`), at once, else `GetLanguage()` -- the game's own
text takes a new language only when its scene ends (section 8.1), and the
title is one scene from the logos to the difficulty screen. The menu shows while the
object's update is one of `Ov000_MenuFadeInState` (from its counter's 0x3c
on: before then the lower screen is white, master brightness +16),
`Ov000_TickMenuLoop`, `Ov000_TickMenuLevelChange`, the fades its choices
start (`Ov000_TickFadeOutToScene`, `Ov000_BootFadeAndSubScreenSetup`,
`Ov000_TickFadeThenPublishContext`, `Ov000_FadeOutAndStartMovie`) and the way
back (`Ov000_ReentryState`, `Ov000_TickBootFadeIn`); in the screens after it
the page still holds the level left (the first version, which read only the
page, covered the difficulty screen and the theater's list). Seen in the
trace: the menu's states change every second VBlank (master brightness steps
every 4 VBlanks where the C steps every 2 counts).

**The theater's main menu** (`Image/Mare.hed_out/original/<lang
folder>/l2d/days_theatre.l2d`, `sp uk fr gr it` for es en fr de it; the
structures are OpenKh's Birth by Sleep L2D, version 0500): layout 113, 22
nodes in a 960 x 540 space. Its groups 900-908 have no sprite of their own
and move and fade their children: control 0 still; control 1 appears from
nothing over 44 frames (900, the key art's, by frame 16; the others from 12
to 44); control 2 fades out (16 frames; 900 over 14-30); controls 4-7 slide
the bars 200 units right or in from 200 left over 16 frames, fading (which
moment of the theater's program plays each is not in the data). The option
nodes 104, 107, 108, 111 (REPRODUCIR, CAPÍTULOS, EXTRAS, ATRÁS): control 0
selected (the orange square, the bar, the black word, and sprite 29, a light
added over the square, scaled 0.5 → 1.2 → 0.5 over 180 frames, again and
again), control 3 not selected (grey); no transition between them. Node id 0
is the settings key ("Esc", a gear, AJUSTES DEL JUEGO; the port's settings
open with Esc too). The menu's words are in its font
(`<lang folder>/font/menufont.tm2`, 32 x 32 cells, and `menufont.cod`, 8
bytes a glyph: its UTF-16 code, its cell's x and y, a byte, its width; no
space glyph). The port's pictures: on the menu's first showing every group
appears (control 1), on a change of level the bars appear so, then the
selected option's loop; their clock is the game's VBlanks, drawn at the
display's pace (the still DS menu sends no new frames: `alike`). Their 60
frames a second are the port's reading of the L2D's frames, not checked
against the collection.

**The settings key** (node id 0): three parts -- the bar's gradient, the
words, and the key with its gear in one (block 0, 98 x 64 texels at
u 1002, v 1810 in `sp`: the key "Esc" in its columns 2-43, the gear's blue
from column 45). The theater's button icons are its own
`<lang folder>/font/Icon.tm2` (1024 x 1024, cells of 52 x 52; sticks with
arrows 58 x 58), indexed by `FontIcon.inf`: a header of 8 u16 (1024, 1024,
the entry count 178, then 0) and 16-byte entries (u16 code in the private
use area, x0, y0, x1, y1, three 0); several codes share a cell. Xbox, then
PlayStation, then a third family of plain dots, in rows: the face buttons
(east, south, north, west), shoulders and triggers, stick clicks, plain
sticks, the D-pad's four, then start and back (OPTIONS and the touch pad
for PlayStation). There is no icon for the Guide button. The settings
menu's own copy (`Image/SettingMenu.hed_out/original/<lang folder>/font/`)
holds the same icons at 30 x 30 in another order. Both are localized (the
keyboard's Mayús, Retr, Supr in Spanish).

**The boot logos** (2026-10-09, in a window): the DS shows three pairs, one
for each of `Ov000_LogoFadeState`, `...2`, `...3` (VBlanks 1-181, 185-365,
369-549 from a cold start, each from white over 64, held about 56, to white
over 60, the lower screen's master brightness): SQUARE ENIX under Disney
Interactive Studios, MobiClip under h.a.n.d., "Licensed by Nintendo" under
the copyright ("© 2009 Disney", "Desarrollado por SQUARE ENIX": localized);
`Ov000_HandoffState` then hands over to the menu. The theater's own boot
screens are layouts 130 (Disney), 140 (SQUARE ENIX) and 150 (its copyright
card: "© Disney.", "Developed by SQUARE ENIX." and the trademark line) of
`days_theatre.l2d`, dark on white; node 100's control 1 fades each over 275
frames (from nothing 10-80, held to 200, gone by 275), control 0 holds it.
Their texels are the same in sp, uk, fr, gr and it (the card is English in
all five). The PS4 collection's launcher (`launcher.l2d`, layouts 10, 20,
30) has the same three for a black ground. The port shows the theater's
three in the DS's three turns, held, the game's fades through white kept.

### 15.2 The new game's dialogs and the movies' subtitles (2026-10-09)

Asked for by the maintainer: NEW GAME's steps as KINGDOM HEARTS Dream Drop
Distance HD (HD 2.8) draws its new game's, without its vibration question
and with one for the subtitles. `tools/hd/newgame_dialog.py` makes the
pictures from the player's copies; `runtime/menus/newgame.c` follows the game and
asks the port's question.

**The game's new game** (read in the decomp, seen in the trace): after NEW
GAME the title's object waits in `Ov000_WaitSubMenuResult` while a sub-scene
runs `Ov000_DispatchIndexedCallback` each frame; its context is
`data_ov000_0205ac28`'s (the word after `data_ov000_0205ac24`). `+0x4bc4`
the step, the index into `data_ov000_0205a86c`'s six callbacks: 0 a fade
out, 1 a fade in, 2 the difficulty (`Ov000_NavigateModeSelect`), 3 the review
(`Ov000_SelectMenuSubMode`), 4 after B, 5 after a start. `+0x1c` the step's
input (UP/DOWN move the difficulty, wrapping over three; LEFT picks the
review's yes, RIGHT its no), `+0x20` the difficulty (0-2), `+0x24` the
review's button (0 yes, 1 no): A on the difficulty sets it to 1 -- the
review comes with no selected (the decomp names the field
`actionAccepted` there) --, A on yes starts (step 5), A on no or B goes back
to the difficulty. After a start the object goes on to
`Ov000_TickBootFadeTransition` (the fade to black), `Ov000_BootDispatch`,
and scene 11, the opening. The port shows the review it started from through
that fade.

**The port's question** comes between the difficulty and the review: when
the step turns from 2 to 3 and the question's pictures exist (one screen, a
mod's `layouts/title/<lang>/newgame1_2_0.txt` and `newgame2_2_0.txt`), the game gets no buttons until
it is answered: UP and DOWN move the port's cursor, A answers (`[video]
subtitles`, kept in the settings file), B is given to the game (back to the
difficulty). The buttons held when it is answered stay the port's until let
go: the game's pad would take the A as a new press on the review's no (seen:
back to the difficulty).

**DDD's dialogs** (`kh3d_first.hed_out/original/menu/en/title/title_menu/bin/
title_menu_n.l2d`, OpenKh's BBS L2D like the theater's; its texture a CTRT
block whose width is at +0x20, 512, and the HD texture beside it,
`remastered/.../US_title_menu.dds`, 1024): layout 1 the shade (black, 75 %),
2 the heading's tab, 3 the window with three buttons (node ids 3, 4, 5; the
glove's anchors 20, 22, 24), 5 with two (3, 4), 7 the settings (rows 41-43,
buttons 5, 6); control 0 still, 1 appears, 4 selected. The pieces are grey
texels tinted by the layout: the window's blue corners, the selected bar's
red, the selected ring's yellow, its brightness pulsing 1.0 to 1.99 over 60
frames. The words: `message/<lang>/system/bin/ct00000.ctd` and `ct00200.ctd`
(`@CTD`, the entry count at 0x0e, the table's offset at 0x10, 8-byte entries
of id, group, text offset, style, UTF-16 text; ids repeat across groups),
drawn in `font/en/bin/helpfont.bcfnt` (FINF, TGLP, CWDH, CMAP) and its HD
sheets. Behind them the theater's menu stands as the story level showed it
(NEW GAME selected, CONTINUE when a save is there: `newgame1_*` and
`newgame2_*`), darkened by the shade (the maintainer, 2026-10-09).

**No blink between the title and its dialogs (2026-10-09).** NEW GAME's way
into them (`Ov000_TickFadeOutToScene`: `SetMasterBrightnessSub` to -16 over
16 frames), the new game's own steps (`Ov000_TickFadeOutFromObjTimer`,
`Ov000_TickFadeInFromObjTimer`) and the way back (`Ov000_ReentryState`, then
`Ov000_TickBootFadeIn`) fade only the lower screen; the start's
(`Ov000_TickBootFadeTransition`) fades both, through white to black. The
port's picture took the lower screen's brightness, so each step blinked
black. In those states (`title-lit`: the dialogs, NEW GAME's fade, the way
back from the dialogs) it takes the upper screen's (`port_days.txt`
`[title if title-lit]`). Seen with `KHDAYS_TRACE_TITLE` (each change of the
title's picture and the lower screen's brightness, then): NEW GAME at frame 1062,
the lower screen to 0x8010 by 1096 with the picture lit, the dialogs from
1098 (the lower screen back from black, not shown), back from them at 2640
with the lower screen's fade-in not shown either, the menu's own fades
shown again from 2678. The trace gives both screens' brightness since: the
upper screen's stays 0x0000 from NEW GAME (frame 1061) through the dialogs'
opening (1097-1135), so what still blinked there was the port's pictures:
Dream Drop Distance's shade over the title appears in 16 frames and holds
its last (its control's fourth field 1, as every appearing control's; the
selected button's light, 0, repeats every 60 frames, and the theater's
options' lights every 180), but the dialogs' 20-frame opening started it
again from its light first frame at frame 17 (the background measured at a
mean grey of 49 at frame 16, 131 at 17, 52 in the loop); each change of row
started the selected button's light again from its brightest frame; and
the dialogs' atlas, read only when NEW GAME was chosen, held the picture
for 110 ms. Since then the tools hold such controls on their last frame,
the port goes on with a picture's clip and loop where another of its group
left them, and the dialogs' pictures are read ahead from the first logo on
(`preload`, `docs/COMPOSITIONS.md`).

**The movies' subtitles** (`Ov024_MobiClip_SetUpMainEngine`,
`Ov024_MobiClip_SetUpSubEngine`): the movie's engine in mode 3, its picture in
BG3 (BG3CNT 0x4284, priority 3) and in bitmap sprites, the subtitles' text in
BG0 (screen base 12) and its shadow in BG1 (13, one dot right) and BG2 (14,
one dot down). Seen in the opening (scene 11, ov012): engine B with BG0CNT
0x0c00, BG1CNT 0x0d01, BG2CNT 0x0e02, BG3CNT 0x4287, DISPCNT turning between
0x00210f33 and 0x00211733 every four VBlanks (BG3 or the sprites). With
`[video] subtitles = off` the port leaves BG0-BG2 of such an engine out of
the picture while ov024 or ov012 is loaded (`runtime/movies/video.c`); seen: the
opening's frames 2150-2450 without "Pobres sincorazón" and the lines after.
The game's `Session_SetMoviePlaying` marks a movie only where a session
context exists, so the port does not read it.

**The HD videos.** The collection's own (`STEAM/Mare/MOVIE/Days/dt/hd<id>.mp4`, one folder for
every language; also `DOP`, the opening, 299.7 s; `DED`, 382.4 s, and `CED`,
215.3 s, neither the DS's ending's length) are 1280 x 720, 30 frames a
second, without subtitles. Their first 0x100 bytes are XORed with a 16-byte
pattern (the header's zeros show it); the rest is a plain MP4
(`tools/hd/theater_movies.py` writes them as the port plays them).

**The scenes' mark of a skip** (GameState field 0x2484, a bit). Every event scene starts with
`00.05` (`CommitRecordCursor`): it tests an expression in its data, field 0x2484 = 1, and when it
holds goes on into its skip path -- the skip target (+0x1c), a fade (`01.11`), `00.00`
(`Game_ActionAssign`, mode 4: `GameState_SetField(0x2484, 1, 1)`) and a jump to its end -- else
jumps over it to set its target (`00.0a`) and play. So a skip (the pause's Skip, or the port's own
after an HD scene) sets the mark, and the scenes after it skip themselves until one clears it at
its end (EV_S#196 does, `GameState_SetField(0x2484, 1, 0)`; EV_S#38 does not). A player's
session (day 71, mission 0106, whose end section `99:_e` loads `4:38` and `4:39` and runs them):
hd034 for EV_S#38 watched to its end, the port's skip asked at f14216, EV_S#39 (the ice cream)
loaded at f14246 with its music, then the mission's review at f14253 without its stretch begun.
The port now clears the mark when its own skip ends a scene (the HD videos watched, or a scene the
scene map's `skip` leaves to the HD movie before it), and leaves it to the player's Skip. Seen
(KHDAYS_TEST_SCENE=4:38 in the story walk, a mod's map giving EV_S#38 hd034 and EV_S#0 hd035): hd034
to its end, the skip, then EV_S#0 began its stretch two frames later and hd035 played; before, with
the game's skip asked in EV_S#177, EV_S#0 reached its end six frames later without beginning.

**Movies the theater's videos outlast** (the DS's `mv/<id>.mods`, 14.985 pictures a second,
against the theater's): 829, 831, 832, 834, 835 and 848 last as long within a second; 830 is 43.8
s of hd830's 194.6, whose lines from 44 s are EV_S#168's (Riku and Mickey, `¡Riku!` to `Te lo
prometo, Riku.`); 836 is 72.9 s of hd836's 129.8, whose lines from 73 s are EV_S#177's (Riku and
the hooded Roxas, `¿Quién eres?` to `¡Cállate!`), and hd140, in none of the theater's chapters,
is that end again in Japanese without subtitles; 833 is 128.1 s of hd833's 272.2, whose lines end
by 125.1 s and which fades to black from 127.5 to 129.4 s, then shows Roxas and the fight against
Xion (to her last form breaking, black from 270.6 s); 837 is 146.4 s of hd837's 169.1, its last
line (`Lo he aceptado.`, 166 s) in no script of the game. The theater's chapters: day 354 hd129,
hd130a, hd830 (nothing after it); day 357 hd833, hd848, then hd834; day 358 hd835, hd836, hd837,
hd840. So the scene map skips EV_S#168 after hd830 and EV_S#177 after hd836 (`skip`, only when
that video was the movie shown), and plays hd833 in three parts along the fight (the
maintainer's pick): to 128.4 s for 833, its fade to black; from 129.4 to 173.2 s for EV_S#182
(Roxas in the dark, then on Twilight Town's wall, his Keyblade drawn, his leap); from 173.2 s,
the fight to Xion's last form breaking, for EV_S#183 (`mv#833 hd833[:128.4]`, `EV_S#182
hd833[129.4:173.2]`, `EV_S#183 hd833[173.2:]`). Mission 0114 (day 357) runs the fight so: its
script 99:_e plays EV_S#179 (wd_tt and wd_aw loaded) before area 11, EV_S#180 (wd_aw to wd_nm)
before 21, EV_S#181 (wd_nm to wd_al) before 31, EV_S#182 (wd_al to wd_tt, Roxas's Keyblade and
`mi/mo/xo.p2` loaded) before 41, the fight's last part, then once it is won EV_S#183 and area 97,
whose script 97:_m holds the subtitles of the movies 833 and 848; EV_S#179-183 have no lines and
each sets a skip target (the census's). EV_S#176, the scene with no lines nearby, is Roxas with two
Keyblades among Neoshadows in The World That Never Was, day 358, seen with
`KHDAYS_TEST_SCENE=4:176`. Seen: the decoder plays hd833 to 128.367 s (3851 pictures in
128.41 s), from 129.400 to 173.200 s (1315 pictures in 43.84 s) and from 173.200 s to 272.200 s
(2971 pictures in 99.03 s, the seek dropping the pictures and sound before the part), and hd808
from 2.000 to 4.967 s; EV_S#182 swapped into day 7 (`KHDAYS_TEST_SCENE=4:182`) is recognised and
given hd833 (headless, the DS's scene plays: Video `hd` is the window's); the story walk with
EV_S#177 swapped in and its skip asked at once (`KHDAYS_TEST_SCENE=4:177`, `KHDAYS_TEST_SKIP_AT=0`)
ended the stretch 6 frames later and went on.

**The theater's subtitles.** It draws them over its videos from each video's
event, `<lang folder>/event/<lang>/hd/hd<id>/hd<id>.exia2` (XML, `EXUSIA2_XML`;
the same file in every folder, its header naming the language it was written
in): a `SCHEDULE_CATEGORY_TEXT_MOVIE` for each of JP, EN, FR, GE, IT and SP
(`PARAM Language="LANGUAGE_SP"`), each `DATA FrameStart FrameEnd TextID`
(its `Text` the language's draft, without accents). The frames are the
video's, 30 a second: the sound's schedule runs 0 to 3223 over hd818's
107.6 s. TextID is `group << 16 | id` in the CTD tables of
`<lang folder>/message/<lang>/event/hd/bin/`: groups 0x391-0x3a8 in
`cthd800.ctd` (the videos'), and 0x200-0x2a8 in `cthd000`, `050`, `100`, `300`
(the event scenes', whose lines hd830 and hd836 say too); Re:coded's tables
(`cthd500`, `550`, `600`, groups 0x2fa-0x32f) pass 64 KB, which the entries'
16-bit text offsets do not reach. 28 of the 39 videos with an event have
lines, the same in the five languages but for one or two lines (hd802: 29 in
English, 42 in the others). The font is `<lang folder>/font/eventfont.tm2`
(2048 x 2048, 8-bit indices into 256 colours in the PS2's CSM1 order; cells
of 32 x 32: `eventfont.inf`) and `eventfont.cod` (1,212 glyphs, as the menu
font's), the glyphs grey and outlined in black in the texels, shown white.
Measured on the theater itself (2026-10-15: the maintainer's window captures of
the collection, 2000 x 1158 windows whose pictures are 1998 x 1125, scaled to
1920 x 1080 -- hd818's two lines "Son los días que han pasado..." and the
one-line "No, eres tú el que llegas tarde."): the cells drawn 1.445 times
their size, a space 13.75 texels, each glyph advancing its `.cod` width (the
three lines' white, fitted at once, within a dot of the captures'); the lines
51 apart, the last one's cell top at y 956 (955 and 958 measured), the block
centred on x 964 (964-965.5), each line from its left. The port draws the lines the tool made
over the video (`host/movies/movie_subtitles.c`) by the shown picture's own frame
number when the file is at 30 or 30000/1001 frames a second (the same
pictures, retimed), else by its time at 30; seen in a window (2026-10-09,
2736 x 1536, the opening, the player's own clean 1080p hd802): each line over
the pictures the DS shows it over ("Pobres sincorazón," over the Nobodies'
sign, "La furia de la llave espada" over Lexaeus). Against the theater
(2026-10-15, the story walk's hd818 in a 2736 x 1536 window, both scaled to
1920 x 1080): its two lines within a dot of the theater's across, 2-3 dots
higher.

### 15.3 The settings menu in the HD collection's pieces (2026-10-09)

Asked for by the maintainer: the port's settings menu as the collection's own
(AJUSTES DEL JUEGO), in the new game's dialogs' tones. The collection's menu is
`Image/SettingMenu.hed_out/original/<lang folder>/l2d/setting_1525.l2d` (the
theater's L2D; `setting_28.l2d` its HD 2.8 twin), four blocks of texture, 21
layouts the menu's code places (its words are drawn by code: the nodes 161-167,
711, 861, 961 have no sprite). Read (`tools/hd/theater_settings.py`):

- **100** the screen: 101 its ground (crowns and hearts), 102 the heading's
  tab with the menu's name in its texels (localized) and the band's lines (y
  126-150 and 936-958 of 1080), 103 five faded pictures drawn at once (a
  screen, a speaker with notes, a gamepad, a keyboard, a film), of which the
  menu shows the one of its section (the maintainer's captures: Pantalla,
  Sonido, Mando, Teclado, Antes de retransmitir).
- **150** the sections' column: 181 the line beside it (x 536-548, y
  206-904, three parts upright); its words' nodes at x 100, 48 apart.
- **700** a section's tab (412 x 48, one part): control 0 shown, 3 dimmed, 4
  moving right over 10 frames, 5 flashing, 6 chosen (18 texels right).
- **750, 800** a row's capsule (left 26, middle, right 30 texels; 60 high), 4
  selected (74 high with its glow); **850, 900** a switch's (862 its red
  check, 36 x 36); **950** a number's, 962 its slider: the track (390 x 24),
  its two ends, the filled part, the knob; **1020** 1027, 1028 a choice's
  arrows (one picture, the right one at a negative scale); **650** the
  question: 661 its window (left 44, middle, right 136 texels, 298 high, its
  title PREGUNTA in its texels), 662 and 663 its answers; **1010** 340 the
  hints' bar (two halves, 122-1798 x 954-1006); **400** 411 the gamepads'
  pictures, in its control 4 three at once (a PlayStation's, 524 x 298; an
  Xbox's; another, of no make), the menu showing one; **350** 361 the
  keyboard's (12 parts), **300** 329 the mouse's; **550** the brightness test
  and 1020's other nodes the HUD's preview (not used).

The menu's font is its `menufont` (the same file in the five folders; no
space glyph). The port (`host/settings/settings_skin.cpp`) draws each three-part piece
at any width (the ends scaled with its height, the middle stretched), the
selected states over the shown ones, and gives ImGui the font through its own
`ImFontLoader` (ImGui 1.92) from the font's picture, Roboto merged for what it
lacks. The pictures for the port's sections are the port's choice: the
keyboard for Controls, the gamepad for Enhancements, Sticks and Multiplayer,
the screen for Video and General, the speaker for Sound, the film for
Language; the Controls section's gamepad picture is the HD menu's of the
connected pad's family (Nintendo's and others' the one of no make). Seen in a
window (2026-10-09, 2736 x 1536): the sections, the rows, the slider, the
checks, the arrows, each section's picture, and the PlayStation pad's picture
with a DualSense connected.

### 15.4 The save list in Dream Drop Distance HD's pieces, 99 files (2026-10-09)

Asked for by the maintainer: the list of save files as Dream Drop Distance HD
draws its own, in Days's tones, with Days's faces and words, and more files
than the DS's three ("total ya no es una ds"; a fixed number, as DDD has).

**The title's list (ov000), read in the C.** Its context is
`*data_ov000_0205ac24` while it runs (`Ov000_LoadScreenInit`); its update
(`Ov000_TickSelectionScene`) runs one of nine states (`data_ov000_0205a710`,
the state at +0x4ad0): 0 a fade out, 1 a fade in, 2 the files
(`Ov000_HandleSaveSlotInput`), 3 the question before loading
(`Ov000_HandleLoadConfirm`), 4 a file being deleted, then deleted
(`Ov000_PollSaveCheckState`, its step at +0x4ae0: 2 done), 5 the questions
before deleting (`Ov000_TickMarkerMenuInput`), 6 the file being read
(`Ov000_BeginSaveCheck`), 7 leaving, 8 done. Its cursor (+0x4b08) is 0-2 a
slot, 3 Mission Mode's guest's row (shown when +0x4acc is set); its keys are
the title's key repeat (`KeyRepeat_Update` on the title's context's +0x4be8:
its delay at +2, its interval at +4, in VBlanks at frame rate mode 2, times 2
or 3 at modes 0 and 1). On the files A loads (state 3), X deletes (state 5:
"¿Borrar archivo %d?", then "¿Seguro?", +0x4ad4 the second), B leaves; an empty
slot beeps (`PlaySound(0, 4)`). The questions' buttons are LEFT for yes (+0x4b70
is 1) and RIGHT for no, no by default. Each slot's record (+0x4b10, 0x20 bytes,
`Ov000_RecordLoadResult`): +0 the day (field 0, 9 bits), +2 the level (the
panels' grid counted by `Ov000_BuildMenuGrid` and
`Ov000_RebuildViewAndCountCells`, plus one: no field holds it), +4 the munny
(the state's +8), +8 the difficulty (field 0x40a), +0xc the play time in
seconds (the state's +0), +0x10 what the slot holds (1 a save, 0 none, 2 both
copies broken), +0x18 the story seen to its end (field 0x44e is 6: the crown),
+0x1c the face (field 0xc4b). The words each state shows are
`Ov000_DrawLoadPageText`'s, of `UI/load/lrd_&.s.z`: 0 the list's title, 1 and 2
its keys (the DS's A, B and X are the font's U+3349, U+3314 and U+3322), 3 load,
4 and 5 delete, 9 and 10 deleting and deleted, 12 the level, 13 a broken file,
14 delete a broken one, 16-18 the guest's, 19-21 a card error, 22 "¿Seguro?".
The DS's row draws the day and the difficulty as sprites (`UI/cm/cmo_&.p2`
entry 3: cell 38 DÍA, 28-37 its digits, 40-43 the difficulties, the fourth
CRITICAL), the rest as text (`Ov000_DrawSaveSlotText`: "Nv. %d", "%d
platines", H:MM:SS).

**The faces are rolled.** At each save the camp's `BeginSaveToSlot` (ov008,
ov025) writes field 0xc4b with `Rand16NextScaled(2)` before day 26 and
`Rand16NextScaled(3)` from it (0..1, then 0..2), field 0xc98 with the slot, and
the save counter (the menu's +0x1ef8, plus one) into fields 0xc77 and 0xc87;
the title starts its cursor on the slot whose counter is the highest
(`Ov000_TickLoadScene`). The DS draws a saved slot's plate with frame 2 + the
face of entry 3's animation 13 (`Ov000_RefreshSelectionGroupDraw`: frames 46,
47, 48, 49, 50, 45 -- a broken slot, an empty one, Roxas, Axel, Xion, the
guest's bar).

**A slot's reads and writes.** The game reads a copy of 0x2018 bytes (copy 0 at
(2 x slot) x 0x2018 + 0x20, copy 1 after it): first eight words zero, no save;
the magic 0xc8f592a6 and the SHA-1 of the 0x1cac bytes from +0x18 at +4, a save
(`Ov000_PollSaveLoad`, `Ov000_VerifySha1Signature`); otherwise copy 1, and
neither, broken. Deleting writes 0x20 zero bytes at each copy's head
(`Ov000_WriteSlotHeaders`); a chip without the game's 32-byte signature
(KH358DAY four times) is formatted (`Ov000_ProbeSaveMedia`: the signature, then
each copy's head zeroed). The camp writes both copies whole
(`Ov025_CommitSaveToSlot`).

**The port's (2026-10-09).** Its list has 99 files (`runtime/menus/saveslots.h`):
files 1-3 are the chip's slots, in `saves/khdays.sav` as always; files 4-99 the
port's, each in `saves/slots/slotNN.sav` (the slot's two copies). The game's
slots are reached through a view: the chip's own, or -- on the title and while
a camp's menu is open, the list's pieces in a mod -- the port's, where game
slot 1 is any file and slots 2 and 3 are blank (their writes dropped). The port's list
(`runtime/menus/savelist.c`) keeps its own cursor over the files with the game's key
repeat and sounds; on A or X on a file it brings that file under game slot 1,
tells the game's record of that slot what the file holds, takes the game's
cursor to slot 1 (a press at a time) and hands the game the button; its
questions, reading, deleting and fades are the game's, shown by the port in
its words (`host/screens/save_screen.c`), the files' levels counted by the game's own
functions on the game's thread with its state under `gGameState` for that
while. The pieces: `tools/hd/save_screen.py`, from DDD's
`menu/<lang>/camp/save/bin/save00_n.l2d` (layout 1 node 2 the screen, 500 the
heading's tab; 5 node 210 the list's window, 5555 the scrollbar's track, 5556
its thumb; 4 node 2 a file's row, control 4 selected, node 14 the crown; 7
nodes 2 and 3 a question's yes and no; the gratext texture's LOAD, SAVE, MUNNY
and TIME in each language), DDD's help font and its input icons
(`font/<lang>/bin/iconfont_n.ctt`: an Xbox pad's, a PlayStation's, the
keyboard's keys with each language's names), Days's three faces. Seen in a
window (2026-10-09, a copy of the maintainer's save): the list with file 1's
day 357, level 55, 157458 munny, Experto, 30:29:30, the crown and Axel's face;
the cursor down to file 5 (a copy of file 1 placed as `slot05.sav`) and up,
held DOWN repeating to file 40; file 5 loaded (DÍA 357, then the field); file 5
deleted through both questions ("Borrando archivo...", "Se ha borrado el
archivo 5."), its file's two heads zero and `khdays.sav` unchanged.

**The camps' save screens, read in the C.** The story's camp menu (ov025, the
Grey Area's) and Mission Mode's camp (ov008, scene 19) run the same save screen
(`Ov025_SaveMenuConfirm` is `Ov008_SaveMenuConfirm`'s copy). A camp's running
screen is the entry of its handler table A (`data_ov025_020b4ab0`,
`data_ov008_0209009c`, 8 bytes an entry) its index names
(`data_ov025_020b574c`, `data_ov008_02090f0c`); the save screen's entry holds its hooks
(`data_ov025_020b4e2c`, `data_ov008_02090460`: open `SaveMenuInitStep`, step
`SaveMenuTick`, confirm `SaveMenuConfirm`, back `SaveMenu_OnBack`). Its state
(0x1f00 bytes) is the camp's context's +0x959c, the context being the second
word of `data_ov025_020b5744` or `data_ov008_02090f04`: +0 the slot chosen
(`StepSelectionBackward` and `StepSelectionForward` step it, wrapping over
three; UP steps it, as the port's list takes it to slot 1), +4 the phase (0 the slots; 1 the question,
"¿Sobrescribir los datos de %d?" when the slot holds something, else
"¿Crear nuevo archivo de datos en %d?"; 2 saving; 3 the slot read again; 4
"Guardado completado."; 5 a card error), +0xc the opening's step (0 set up, 1
the three slots read, 2 laid out with the cursor on field 0xc98's slot, 3
done), +0x10 the slots' entries (0x1c bytes, +0x10 what the slot holds), +0x64
the question's answer (1 yes), +0x23c a card error, +0x240 which, +0x1ef8 the
highest save counter of the slots read; the keys repeat with the context's
+0x963e (`CommitPage`'s `KeyRepeat_Step`). Its words: `UI/cm/str/sav_&.s.z`
(`SaveMenuDrawPrompt`: 0 the screen's title, 1 its note with the slots, 2 and 3
the questions, 5 and 6 yes and no, 7 saved, 11 saving, 10 and 13 the errors).
The port's list shows the title and, as the load list's keys, the game's A and
B prompt (`mlt_&.s` record 63: B goes back from the slots, the port hands it
over) in the note's place -- the question says the same; the theater's hand at
the selected row, or at the question's selected button, in both lists.
The port's list stands for it as for the title's (the file under game slot 1,
its entry told what the file holds, the game's cursor taken there with UP, A
handed over); while the screen opens it shows the game the file saved last,
so the game's next save counter goes on from the highest of all the files. Seen
(2026-10-09, a copy of the maintainer's save): the story's (file 1 loaded, the
Grey Area's camp menu, Guardar, down to file 2, "¿Crear nuevo archivo de datos
en 2?", yes, "Guardado completado.", file 2's row filled; `khdays.sav`'s slot 2
signed whole, slot 1 unchanged) and Mission Mode's (file 1 loaded on its list,
the character select, its camp's Guardar, the same).

**Mission Mode's guest's row.** Its words ("JUEGO INVITADO", "GUEST PLAY",
"INVITATION", "GASTSPIEL", "GIOCA OSPITE") are no cell of `cmo_&.p2` entry 3:
they are its characters after those of cell 44 ("SIN DATOS", 8 characters) and
two of a bar's end -- 10 characters, the words centred in their 80 x 8 dots --
in every language (seen in the characters the game's list loads, with
`KHDAYS_DUMP_GRAPHICS`). The port's list puts the guest's row after file 99
(UP from file 1 reaches it), with those words; A there takes the game's cursor
to its row 4 and hands it A ("¿Empezar a jugar como invitado?"). After 99 files
the row is out of sight: Y (the list takes no Y) does the same from any row,
the help's keys saying it with the row's words (the maintainer's choice,
2026-10-10; seen: Y on file 2, the game's question).

**The game's own lists in pages (2026-10-15).** Without the HD list's pieces the
game's own lists reach the 99 files too (`[save] files = 99`, the default; `3` the
DS's three), drawn and run by the game: game slot g is file top + g
(`khdays_slots_show_page`). Read in the C and seen running:
- The rows are drawn from the game's records of its three slots: the title's at
  its list context's +0x4b10, 0x20 each (+0 the day, +2 the level, +4 the munny,
  +8 the difficulty, +0xc the play time, +0x10 what the slot holds, +0x14 its
  latched copy, +0x18 the crown, +0x1c the face); the camps' at the save screen's
  state +0x10, 0x1c each (the same to +0x10, then +0x14 the crown, +0x18 the face:
  `BuildSaveSlotEntry`'s stores). The row text reads the latched copy
  (`Ov000_DrawSaveSlotText`, every frame): with only +0x10 written, a row that held
  a save when the list opened drew its details over an empty file; the plates and
  digits read +0x10 (`Ov000_RefreshSelectionGroupDraw`,
  `Ov000_UpdateNumberDisplays`, which the game calls after a delete). The port
  writes a page's records from the files and calls those (the camps':
  `SaveMenu_RefreshRows` and `RefreshSaveRowDigits`, as `SaveMenuTick` after a save).
- The cursor: the title's `nPageIndex` (+0x4b08) steps with UP/DOWN and
  `PlaySound(0, 0)`, wrapping over three (four with the guest's row);
  `Ov000_PlaceCursorByMode(1, row)` places the hand. The camps' save screen steps
  its slot (+0) with UP back and DOWN on (its hooks: `StepSelectionBackward` and
  `StepSelectionForward` in the up/down places), `PlaceElementByVariant` placing it.
  The port takes the press at a page's edge and every step of that hold until the
  key is let go: given the key again while it is held, the game took it as a new
  press and stepped once more (seen: UP from file 1 landed on 98, not 99).
- The questions name the file as the row + 1 (`Ov000_DrawLoadPageText`'s
  `pageIndex + 1`, the camps' `SaveMenuDrawPrompt`'s `nSlot + 1`); prepare.py's
  HOOKS make it the file's (`khdays_savelist_file_number`). Seen: "¿Cargar archivo
  99?", "¿Borrar archivo 50?", "¿Sobrescribir los datos de 50?" -- the save went to
  `slot50.sav` alone, `khdays.sav` and `slot99.sav` byte for byte as before.
- The rows' numbers ("01" to "03") are two 8x8 sprites each of `cmo_&.p2` entry 3's
  white digits 0-3: its 8-bit characters 44, 42, 46, 48 (engine B's sprites step
  128 bytes, DISPCNT bits 20-21 = 2, so OBJ tile 21 is character 42 and every other
  character is unused). The sheet has 0-9 in the same shapes in cyan (characters 22
  to 40, every other one) and in yellow (0 to 18, the day's); the white ones are
  the cyan shapes with each cyan colour mapped row by row (fill 172 on rows 0-3, 168
  on 4-5, 155 on 6, the outline 1), which gives the white 1, 2 and 3 exactly. The
  port finds the game's digit sprites by their pixels on either engine (the
  title's list is on the lower screen, the camps' on the upper), hides them and
  draws the file's number (`ui/save_numbers.cpp`); over file 99's, the autosave's,
  "AUTO" in the game's 8-dot font in the same colours, on the plate's free dots
  before its day (the maintainer's choice, 2026-10-15; the HD list puts it over its
  number circle). Seen on the title (story and Mission Mode), the story camp's
  Save and on one screen; Mission Mode camp's Save is greyed in the saves tried.
- The pages open on the file saved last (the title: the game's cursor starts on
  its three's highest save counter) and, in a camp, on the file being played on
  the row of field 0xc98 (where the game's cursor starts); after the camp's slots
  are read the port gives its save counter (+0x1ef8) the highest of all the files,
  so the next save goes on from it. The guest's row (Mission Mode's) comes after
  file 99: UP from file 1, or Y from anywhere (as on the HD list); the walks reach
  it with UP, which the DS's own list also wraps to.

**The autosave (2026-10-17).** Asked for by the maintainer (2026-10-15): after each
cleared mission the game saves itself, by default into file 99
(`runtime/menus/autosave.c`; `[save] autosave = file99 | played | off`).

- **What owes one.** `Ov005_CommitMissionResults` records the results (a join as it
  starts, the mods' `mission_end`). The autosave is owed when the routine records the
  mission cleared, by its own branches on the results' configuration
  (`data_ov005_0205b85c`): reward mode 255 (the story's) with flag 0x2086 and its +0x64
  clear (else it only marks the mission seen), mode 8 ranked 0-3, the others ranked
  0-2; never with flag 0x2087 (the give-up, `Ov002_RetireSubFlow`). A give-up from
  Mission Mode's pause records rank -1.
- **Where the game lets the player save next.** `Ov005_UpdateMenuExitTransition`:
  with the transition's flag 4 the results go to Mission Mode's camp (scene 19,
  ov008); with flag 1 or 2 it sets flag 0x18bd or 0x18c9 ("back from a mission") and
  goes to the field (transition 10000, 10001 on day 357), whose phase 11 loads the
  story's camp (ov025, `Ov002_LoadSubFlowOverlay`, kinds 8, 11 and 13) with the
  prompt -4 or -6 for those flags (`Ov002_SetPhasePromptSelector`); when that camp
  closes `Ov002_RetireSubFlow` clears both flags. The camps' save code is overlay
  code: the card context each copy keeps (12 bytes: `data_ov008_02090fb4`,
  `data_ov025_020b5760`) lies in the field's overlays' memory (ov025's in ov030's,
  Roxas's), so it runs only while its camp is loaded. The autosave is written the
  first time a camp's menu waits for the player: its running screen 0 (the words at
  0x02090f0c, 0x020b574c), an option or an answer under its cursor (entry 1-9 or
  101-107, the menu's state at the context's +0x959c, +0x88), the card idle
  (`CARDi_TryWaitAsync`). Back on the title, nothing is owed any more.
- **The write.** The camp's own save, as its save screen runs it
  (`SaveMenuConfirm`, `SaveMenuTick`): a 0x1f00-byte block of the game's heap stands
  for the screen's state (its faces at +0x28, card operation +0x238, save counter
  +0x1ef8 given the highest of all the files, the two kept flags +0x1efc);
  `Sleep_Block`; `BeginSaveToSlot(block, slot)` into the game slot the page view
  puts the file in (on the row of field 0xc98, where the files allow); then
  `CARDi_WaitAsync` and `PollSaveCardOp` until it is no longer -1 (0 written, 3
  failed); `LatchTick` when written; `Sleep_Unblock`. It is all done at one
  Pad_Sample, waiting for the card as the game's own synchronous reads do, so nothing
  of the game runs between the state's signing and its two copies. The view is put
  back as it was; with file 99, field 0xc98 too (the camps' save screens start their
  cursor on it: the manual Save starts where it did). A failed write leaves the file
  as it was (`khdays_slots_commit` writes whole files atomically); the player is told
  either way, three seconds at the upper screen's top left in the game's 8-dot font
  (`ui/autosave_notice.cpp`). A port file whose write fails is read again from the
  disk (`khdays_slots_commit`): before, its new bytes stayed waiting and every later
  save, the chip's too, failed with it. Mission Mode's guest game has no file and is never
  saved; "the file being played" is the one the title loaded or a camp's Save wrote
  last (`khdays_savelist_played_file`), a game never saved the first empty file.
- **Seen** (`KHDAYS_AUTOSAVE_TEST` owes one when the game leaves the title and at
  every result, for scripted runs that cannot clear a mission;
  `KHDAYS_TRACE_AUTOSAVE` traces it): the save walk, at the story's camp opened in
  the Grey Area (ov025, screen 0, entry 1), wrote `slot99.sav`, both copies signed,
  counter 188 over the files' 187; the walk's own Save into file 2 in the same camp
  26 seconds later differs from it in four bytes: the play time, the counter and
  field 0xc98, and the walk's Save still went to file 2 as without the autosave.
  With "the file being played" the same walk wrote file 1 (the one loaded), file 99
  untouched; switched off, only the walk's own file 2 changed. Mission Mode with the
  player's file loaded: at its camp after the results (ov008, screen 0, entry 101)
  `slot99.sav` written (the chip untouched), and loaded again from CONTINUE's list
  (the day's calendar, then the field). The mission walk and both copies of the
  multiplayer walk (guest games) reached the camp and wrote nothing. File 99
  read-only: "failed", the file byte for byte as it was. Not seen yet: a mission
  cleared in play, the story's camp that opens by itself after one, the notice on
  screen.

**Not the port's yet:** the save after the story's ending (scene 9, ov009,
which ov011's end asks for: `Ov011_DestroyScene`, `Scene_RequestPending(9)`)
shows the game's own three slots, the chip's own view.

### 15.5 THEATER MODE as the HD collection's theater (2026-10-09)

Asked for by the maintainer: MODO CINE's list as the 358/2 Days theater of
the HD 1.5+2.5 ReMIX shows its chapters ("como el de steam"), with the
length on the right and the background changing with the selection, the
DS's small pictures left out.

**The game's list (ov000), read in the C.** THEATER MODE goes
`Ov000_TickFadeThenPublishContext` → `Ov000_FinishMoviePlayback` on the
title's object while the list runs as an object of its own:
`Ov000_EnterListScene` (both screens to -16, `UI/thr/thr.p2`'s resources,
`Ov000_FillListRows`, `Ov000_ReflowList(28)`), then `Ov000_FadeTransitionTick`
(its tween on both screens' master brightness; mode 3 the fade in), then
`Ov000_TickListSceneInput` (mode 4: UP and DOWN move one, LEFT and RIGHT a
page, A `Ov000_ActivateSceneObject`, B or START `Ov000_List_StartClose`; the
lower screen's touch too). After A or B, `Ov000_FadeTransitionTick` again:
mode 0 → `Ov000_StartMovieFromMenuRow` (the row's movie, "%d.z" of
`UI/thr/m.p2`, through ov024's stream source) → `Ov000_WaitSubSceneThenMenu`
→ the list again; mode 1 (rows 0 and 27, the opening and the ending) →
`Ov000_LeaveTitleForScene`; mode 2 → the list destroyed, the title's
`Ov000_ReentryState`. Its context is the object's heap
(`NNSi_FndGetCurrentRootHeap`, `gObjSystem[1][8]`): +0 the page's first row
and +2 the row selected (s16 each), +0x966a the mode, +0x9670 the 28 rows
(0x18 bytes each: +0 the name's text, +4 the length's, +8 the movie's number).
`Ov000_DrawListScene` draws ten rows from the first (the name at x 0x18, the
length at 0xd0, 16 dots apart from y 0x13) on the lower screen, and the
selected one's name and length and the help on the upper one.

**The rows' data.** `thr.p2` holds 9 entries (P2: the entries' offsets as u16
sectors of 0x200 from 0x10, their sizes as u32 from the next 4-aligned
place -- `rom.cpp`'s reading; a size's top bit an LZ11 entry); entry 7 is the
movies (28 u16: 802, 803, 805, 810, 813, 817, 818, 821, 822, 823, 824, 825,
846, 828, 829, 830, 847, 831, 832, 833, 848, 834, 835, 836, 837, 840, 842,
843), entry 8 the rows' small pictures. `UI/thr/thr_&.s` holds 57 records:
0 the help (the DS's A, then the words), 1-28 the scenes' names, 29-56 their
lengths ("05:00"). Seen in the game (Spanish, a copy of the maintainer's save):
all 28 listed.

**The theater's data** (`Image/Mare.hed_out/original`, no documentation of
them known; read 2026-10-09). `data/DaysTheater.thd` ("THD@", u16 the count at
6: 189): its scenes, 28-byte records from 0x10, then their strings; a record's
u16 at 8, 12 and 16 are the offsets (from the records' end) of its event file
(`hd803/hd803.exia2`), its video (`hd803.mp4`; `DOP.mp4` the opening's,
`DED.mp4` its ending's) and its music. `data/DaysTheater.chp` ("CHP@", u16 the
count at 6: 109): its chapters, 96-byte records from 8, then their strings; a
record's u16 at 2 is its day, at 8 its artwork's name's offset (`tz008b.tm2`,
in `bg/artwork/`, 1280 x 720), at 32 its scenes' count and from 36 their THD
indices. Each DS movie is the THD scene of event `hd<movie>/`; 27 of the 28
are in a chapter (818 in two: day 255's prologue and day 255's own, the
later one in the list's order), 843 in none (the theater's ending is
`DED.mp4`, in chapter 109 with 842: day 359, `tz359a`). The list's layout is
118 of `<lang>/l2d/days_theatre.l2d`, at the size the theater draws it (the
maintainer's capture of the theater, 1920 x 1080, matches its texels to the
pixel): node 106 the artwork's place (the whole picture), 700 the veil over
it (black, 0.95 on the left to 0.62 on the right), 102 the heading (CAPÍTULOS,
CHAPTERS...), 103 the "play all" bar, nine rows of 82 from y 210 (110 the bar
and its dark square, controls 1 still and 5 Days's red, 4 Re:coded's blue, 0
the square lit; 111 DÍA/DAY/TAG/JOUR; 112-115 the day's digits' places; 116 the
line; 1190 a report's icon), 104 the help bar, 800 the scrollbar. The day's
digits are a strip of the texture (block 0, 600-1320 x 1070-1125, 0-9 and ~)
tinted (243, 234, 39) in the capture; the names are the event font's (its
subtitles'), white outlined in black, capitals 26 high; the glove is the
launcher's (`launcher.l2d` block 0, its part 65); `font/Icon.tm2` holds the
pads' buttons and the keyboard's keys (`FontIcon.inf`: u32 the count at 4,
16 bytes a box from 0x10). On the capture a day's first chapter shows the dark
square and DAY (day 0 the square only), the chapters after it on the same day
neither.

**The port's list.** `tools/hd/theater_cine.py` writes those pieces, the
font and the artworks; `runtime/menus/cinelist.c` reads the game's list (its context
taken when one of its updates runs, `diag.c`'s check); `host/screens/cine_screen.c`
draws it over the window while it runs: the game's page of ten in the
theater's rows (the first in the play-all bar's place), the game's cursor,
each scene's day as its chapter's, its artwork behind (read on a thread when
the list shows, coming in over the last in 250 ms -- the port's), the help
with the player's input's icon, the game's fades. Seen (2026-10-09, Spanish,
the same save): the list, DOWN, RIGHT (the second page, rows 10-19), A on
"La captura de Xion" (825: the fade, the HD video with its subtitles), START
(the video skipped, the list again), B (the title's menu again).

**The theater's chapters (2026-10-17).** THEATER MODE shows on the title only
when the save's story is complete (`Ov000_HandoffState`: game-state field
0x44e, 3 bits, equal to 6), and the game then lists all its 28 movies: no
chapter of the port's needs a lock of its own. A chapter record's u16s at 24
and 26 name it, `<lang dir>/ctd/ct00100.ctd`'s message (id, group 50): 67 of
the 109 have one; each of the others (35 with videos) is on the day of the
named one before it, as its continuation (the opening's has none: the game's
word for it). `theater_cine.py` writes the named chapters with videos, 53,
each with its videos and its unnamed followers' in a row (`chapter` lines,
DOP and DED as hd802 and hd843, CED left out). With Video `hd`, the port's
list takes the game's: `cine_screen.c` keeps the chapters whose videos are
there (each one's length from its MP4's `mvhd`), its own cursor (a row with
UP and DOWN, round at the ends as the game's; a page with LEFT and RIGHT;
held directions repeat), the game's sound for a move; A plays the chapter's
videos as a scene's course with no event (`khdays_movie_hd_chapter`, the
game's list held under them, START or their end back to it); the game gets B
and START alone (`khdays_cinelist_buttons`), its closing. The buttons it
reads are those the game's list would have got (`khdays_cinelist_seen`: the
player's and a script's). Seen (Spanish, the maintainer's full save, one
screen): the 53 chapters, DOWN to "Sin corazón" (day 9), A (hd007a with its
subtitles, 565 pictures, none late), the list again on the same chapter.

### 15.6 The port's own situations (2026-10-14)

`port/ui/situation_decide.c` tells the screen from what the game runs, measured on the
regression walks (`port/tools/regress_walks/`, frames from the start) with
`KHDAYS_TRACE_UPDATES=all` (the object updates each frame runs, when the set changes),
`KHDAYS_TRACE_SITUATION` (the inputs below), `KHDAYS_TRACE_CAMP` and shots; the update
functions it watches are listed in `port/ui/situation_updates.h` (their bits in the inputs,
`khdays_situation_ran`; a VBlank in which no object ran keeps the set before, as the game steps
every two VBlanks in its menus). `KHDAYS_SITUATION_COMPARE=1` runs the decision it replaces
beside it and logs every frame where they differ.

| Situation / state | Signal | Measured |
|---|---|---|
| intro | scene 0 before any other (the boot); the boot logos' states (`Ov000_LogoFadeState`..3, `Ov000_HandoffState`) | play walk f1-563 |
| title | scene 1 otherwise; scene 7 (Mission Mode's character select) as well | play walk f564-1146, f1949-2469 |
| load-menu | scene 1 while `Ov000_TickLoadScene` or `Ov000_TickSelectionScene` runs (the save list) | play walk f1147-1945 |
| movie | the game's own movie: from `Ov024_MobiClip_OpenStreamSlot` to `Ov024_MobiClip_StopPlayback` (`khdays_movie_game_playing`); scene 11 (the opening) | story walk f1616-2026, f2262-8729 (`Ov024_MobiClip_UpdatePlayback` runs) |
| day-counter | scene 5 | story walk f2035-2252, f9424-10136 |
| thoughts | scene 10 | story walk f8754-9424 |
| camp-menu, world-selector, save-menu | the loaded camp's screen (`khdays_camp_screen`, its table A's index): 0 the menu, Mission Mode's 2 Panels too; Mission Mode's 1 Holo-Missions; the story's 4 Save; -1 (between two, or the camp's Character page opening the character select) keeps the screen before | play walk f2475-2879 / f2881-3739; camp_panels script f3099-5297; save walk f3431-5267 |
| settings | Mission Mode's camp screen 5 (Preferences) | camp_cfg script from f3501 |
| shop | Mission Mode's shop: `Ov008_OpenShopState`, then `Ov008_WaitShopClosed` while it is open (the camp's screen -1 meanwhile); the field's moogle: ov026 loaded, over the field's overlays (ov022, ov069, ov107, ov029, ov030 load again when it closes) | camp_shop script f3097-4525; a player's session (2026-10-16) f12604-12765, f14505 |
| mission-review | scene 6 (ov005) | mission walk f6561-7730 |
| game-over | ov027 loaded (`Ov027_GameOverFadeIn` .. `Ov027_GameOverIdle`) | mission walk f5565-6555 |
| pause | `gPauseMode` != 0 | mission walk f5029-5529 (2) |
| the-end | ov011 loaded (the credits, scene 8: `Ov011_CreateScene` .. `Ov011_DestroyScene`) | the Theater's last entry with a copy of the maintainer's save, from f3809 (after the movie's last subtitle) to the list again near f26400 |
| tutorial | `data_ov002_0207f9fc` != 0: set by `Ov002_OpenTutorialPage`, cleared by `Ov002_TeardownBriefing` | story walk from f12009 |
| field-3d-both | engine A changes screens at two of the last 8 VBlanks (POWCNT1 bit 15; the game shows its 3D on both screens so, with the display capture), and stays while one of them does: the game skips a change where its frame runs late (a dialog opening), and with two in a row counted the layout became the upper screen's 3D, then the lower's, for the two VBlanks after each skip -- the testers' flickering cutscenes (2026-10-21) | a player's log of missions 46, 47, 51 and 53: the three layouts in turn, every VBlank, through events with Sora's memories |
| field | scene 2 otherwise | -- |
| info-upper | the panel of a mission's start (`data_ov002_0207f614`, its state +0x18c, 1-8 from f4207 to f4582 on the play walk) in 3-6: the mission's information on the upper screen, no deck | play walk f4229-4562 |
| dialog | the field's event (ov023: `data_ov023_0208a784[1]`) has its status halfword's (+0x8758c) bit 1 set and bit 3 (closing, `Ov023_BeginClose`) clear while `Ov023_SceneRun` runs (an event over leaves 0xa there while ov023 stays loaded: mission 56 f6672-7997 runs, then 0xa); or a person talks in the field: the panel of a mission's start in state 10, its box across the upper screen's lower half (9-11 the talk: in 9 the HUD is still drawn, `Ov002_LeavePanelForMap` clearing BG1 and BG3 as it moves to 10; in 11 the HUD comes back) (no ov023 then; the story walk's input, then A through day 7's event, START on the tutorial's page, and UP and A towards Saix in the Grey Area) | story walk f10155-11926; Saix's words: state 10 f12954-13067, the box to f13064, the HUD again from f13082 in state 11 |
| hud | the field, tutorial or field-3d-both otherwise | -- |
| challenge | the story's camp started the mission as a challenge: `data_0204c240` bit 1 (`Ov025_MissionList_StartMission`: 3 with the challenge's thresholds copied, 1 otherwise); after Mission Mode's camp the byte is its session's (`Ov008SessionInfo`: 3 a wireless host), so not a challenge | run07 walk from f5359; the multiplayer walk (host, 3) not one |
| lower-3d | engine A on the lower screen (POWCNT1 bit 15 clear) and no change of screens in the last 8 VBlanks | story walk f11933-11943 |
| lower-shown | field-3d-both and the lower screen shows a picture: a quarter of its dots or more lit and its brightness not faded past 12 of 16, kept until 8 decisions dark (`host/render/onescreen.c`) | Holo-Missions 61 (mission 304), the Guard Armor in area 71: Sora's memories under the game's noise |
| party-4 | ov022's member count (`func_ov022_020882f8`'s root +0x34) >= 4 | -- |
| event | the pause opened while an event of the field runs (ov023's scene there: `data_ov023_0208a784[1]` set): a cutscene's pause, "Continue" and "Skip", the lower screen without the field's HUD | story walk with START in its dialog, f10737-11034; none in the mission walk's battle pause (f5029-5529) |

**The compositions' units** (2026-10-14): `port/ui/compositions/port_days.txt` begins with
`units port`: a margin or an offset is in the picture's DS dots and a scale makes a DS dot that
many of them, both times the HUD's factor for the player's HUD size 1-8 -- 0.6, 0.7, 0.85, 1, 1.15,
1.3, 1.5, 1.75 (the maintainer's choice from the field's HUD drawn at each size); `fixed(N)` does
not follow it; the 2D and the 3D shapes share it. The field's HUD at size 4 is the picture it was
before the file's values were converted (each x 2/3: the one-screen test's digest of its shapes).
A file without the line is read in the older units until the last of them is gone.

**The port's layouts** (`port_days.txt`, each approved by the maintainer from pictures at 16:9,
16:10 and 4:3, 2026-10-14): without the HD mod, the boot logos and the title's two screens side by
side, each with its brightness; the title's save list (its lower screen), the camp's save list and
the game's settings (their upper screen) at the DS's size in the middle, the other screen behind
covering the picture (`scale cover`) and dimmed to 0.35 (`dim`, toward black: an opacity mixed the
other screen with the screen's own blue backdrop). The whole screen keeps the question, the help
and the buttons, so these need no state of their own.
Then (the same day): the camp's, the shop's and a mission's results' two screens side by side,
each with its brightness (the window shows each screen's middle 256 columns, touching); the day's
card, Roxas's thoughts and the game over the upper screen whole at the DS's size in the middle, the
game over's orb (`shape3d hud ... scale fixed(1)`) with its lines. A real game over (a death) was
not reached by any route: its layout is unseen.
Then the field's (the same day): a dialog (an event's text) the upper screen whole at the DS's
size in the middle, the 3D across the picture behind it; a challenge in play its label (the upper
screen's 0,0-132,14) at the top under the countdown; the pause its menu (the upper screen's
72,64-184,140) at the DS's size in the middle, the lower screen's INFORMATION, map and mission panel
where the field's HUD has them; a tutorial's page (the lower screen) at the DS's size in the
middle, the field behind it -- its 2D and its 3D (`3d`) -- dimmed to 0.35; the world selector its 3D
across the picture, the upper screen's header across the top (its title at the left, its help at
the right and its plain columns repeated between: 111-158 are black over the red line, measured in
Spanish), the world's band (the upper screen's rows 176-191) at the bottom left and the mission
list (the lower screen whole) at the right, smaller the narrower the picture -- 0.62 of the DS's
size at 16:9, 0.45 at 16:10, 0.38 at 4:3 -- so that the world stays seen beside it (at 4:3 it
still touches the world's right edge; moving the world instead would need the 3D drawn past the
DS's 256 columns at 4:3, which the port does only for wider pictures). `repeat x` repeats a shape
along its own rows only (before, it claimed every row as well, and its black strip covered the
world's band). The 3D on the lower screen alone (lower-3d) was seen only in a fade to black
(story walk f11929-11953): no layout of its own.
Then Sora's memories (2026-10-19, Holo-Missions 61 = mission 304 from the player's 100% save: the
Coliseum's games, areas 21-61 a round each, the Guard Armor in area 71): with the 3D on both screens
the lower screen shows Sora's own fight with the Guard Armor in Traverse Town under the game's noise,
in step with Roxas's -- before the battle in its event, and a few seconds into it. On one screen the
event shows the lower screen in the picture's place while they last (as KH Melon Mix does), and the
battle the two screens as one, joined in the middle (`halves`: Roxas's left half and Sora's right
half, the fights in step, each at its own brightness), the HUD over them. The route: the door north of the exterior's start
(x 0, z -19.6) to the first event, Phil's "¿Todo listo?" answered Sí (UP then A), then A all along.
The two fights take turns, a frame each: `Ov023_Dual3DUpdate` publishes the screen it draws in
`data_0204be04`, and `Ov022_ReadSelectionInput` drops L and R on the memory's frames (not 0), so a
lock-on press made then was lost (the testers: "it does not let me lock on"). The port keeps such a
press for the player's next frame, and reads the lock-on's state on the player's frames alone
(`port/tools/prepare.py`, `runtime/input/controls_hooks.c`).
Then the credits (2026-10-15, through the Theater's last entry, "El lugar de siempre", with a copy
of the maintainer's save and `movies = ds`): after the movie, ov011's staff roll runs up both
screens as one -- the names, the characters' pictures at the left, the game's logo, last the END
picture over both, fading to white -- and the list comes back. Its layout: the two screens side by
side, each with its brightness. The scene log keeps scene 1 throughout (ov011 runs inside the
title's scene). A situation no section names takes the common rule (`menu`); the old
compositions file is gone, every layout is `port_days.txt`'s. Without the port's HD screens (the
walks' runs: "camp off, save off, mission off, shop off"), the camp, the save lists, the title and
Mission Mode's character select show these DS layouts (the character select the title's: its two
screens side by side; play walk f1966-2469).

**The effects** (2026-10-15): a key or see-through colour matches a texel's colour exactly in the
DS's 5 bits. Measured on the field's lower screen (mission walk f4700-4950): the map's ground is
(31,31,31), its floor (29,29,30), the panels' ground (4,4,4), each a single colour, while the
pieces themselves hold colours one or two steps from them ((30,30,30) in the map, (3,3,3) in the
panel's edge), so no tolerance. The effects no layout of the port's uses (grey inversion, cut and
rounded corners, turning, a fade toward a colour) are gone; the field's HUD drawn before and after
is the same, dot for dot.

**The pause in an event and the banner at a mission's start** (2026-10-15, the maintainer playing
mission 77's cabin cutscene and the mission walk): an event's pause shows its menu alone -- the
lower screen's INFORMATION, map and panel, which the battle's pause takes, are not there (the
`event` state); at a mission's start the upper screen's information shows, and the lower screen's
first message after it, once its line stands still (30 frames: the game draws it over a frame or
two), counts as seen, so the INFORMATION banner does not show the start twice.

**The pause's pieces and the world selector's list** (2026-10-15, the maintainer's pictures): the
game darkens the lower screen to half under its pause (each 5-bit colour c - c x 8 / 16, measured
on the mission walk: the map's white 31 to 16, its floor 29 to 15, the ground 4 to 2, the same
texels), so the pause's pieces key and see through the darkened colours too and show clean. The
world selector shows its lower screen whole at the right -- the list's frame alone (9,9-247,183)
left out a mission's page (wider, to x 252), its buttons' help (rows 183-191) and the L and R of
the worlds (its top corners) -- its corners rounded (`round`, the port's: a radius in the
source's dots), at the HUD's scale like the header and the world's band (0.55), so that a small
HUD keeps the world in view.

**A layer that gives nothing** (2026-10-15, traced on the play, mission, story, run07 and save
walks: every change of engine A's BLDCNT and BLDALPHA while the 3D is BG0 and a second target):
the alpha blends over the 3D hold EVA 16 (4,354 frames) or 0 (628: BLDALPHA 0x1000, EVB 16 --
the day's card's sprites with BLDCNT 0x2f40, story f9414-9924, and the mission's information
between its fades with 0x2148); the weights between come only in the fades, 1-3 for a frame or
two (12 runs). So a layer or a semi-transparent sprite blended over the 3D with EVA 0 and EVB 16
does not show over the 3D in the one picture (`compose.h` `kd_split`), and one at 1-3 shows
faintly, as on the DS.

The rest of the old decision's states (minimap, target, gauge, info-lower, delete-prompt,
name-label, sora-lower, movie-lower, movie-upper) were what its own layouts needed; each layout
of the port's that needs one names it, and its signal is measured then.

## 16. Mission Mode multiplayer

Seen 2026-10-05 with two copies of the Release build on one PC (a host and a
guest, each in its own directory with a copy of the walks' save, scripted
input, `KHDAYS_TRACE_WM=1`), on the port's wireless layer
(`port/hal/sdk_wireless.c`; ov105, NitroSDK's WM and `wh.c`, is not built):

- **The way in.** Title, Mission Mode, Multiplayer, the save list (Guest Play
  with this save), "Begin playing as a guest?", then "Begin DS Wireless
  Communications?" (the cursor on No) -- record 16 of `UI/load/lrd_&.s.z`,
  which the port serves as "Begin network play?" (`runtime/core/text_patches.cpp`:
  the records that speak of the DS's wireless, power switch and Game Card in
  `lrd_&`, `/UI/sys/sys_&`, `UI/mlt/mlt_&` and `UI/cm/str/sav_&`, the files
  the game opens; `UI/load/str/&/lrd.s.z` holds older wordings no code opens). Scene 7 starts (ov006): the helper is
  initialised and a scan starts at once (`WH_Initialize`, `WH_StartScan` with
  ggid 0x800356) -- "Select a group to join", four rows and "Create a Group".
- **A room.** Create a Group: the scan ends (`WH_Finalize` from SCANNING), the
  channel is "measured", `WH_ParentConnect` (a new tgid, up to 3 guests): "Seeking
  players...". The guest's list shows the room under the host's name about
  half a second later (the next announcement). The name is the DS owner's
  nickname the game reads at 0x027ffc86 (`OS_GetOwnerInfo`), which the
  port fills (`runtime/core/boot.c`: `KHDAYS_NAME`, else the settings'
  nickname, else "Player" and four digits made from the machine, the
  player's number for player 2 on; again when the settings close, since the
  game reads it as Mission Mode's multiplayer starts); left empty, the row
  shows nothing.
- **Joining.** The guest picks the row: `WH_ChildConnect`; the host's judge
  (`Ov006_MatchMissionStartPacket`) accepts; both show 1P and 2P with their
  names and the signal icon. Proceed: the shared character select (each sees
  the other's cursor), START, scene 19 on both in the same frame: the host
  selects a mission ("Host is selecting a mission..." on the guest), "MISSION
  CONFIRMED", the guest's START ("Press START to join"), the host's START.
- **The mission.** Scene 2 on both in the same frame (8989): each copy plays
  its own character and sees the other's move.
- **The players' names in the field (2026-10-08).** The field shows only
  the J labels; the names are the port's (`port/ui/player_names.cpp`). The
  game holds them while the group forms, in the Mission Mode context's
  selection message (+0x42c: the members present at +8, a bit per player
  number, their names at +0xa, 11 UTF-16 characters each): ov006's context
  (0x020565e4) and ov008's (0x02090f24) point to it. Seen with two copies:
  both read "Player9454" (1) and "Player6492" (2), the host's own and the
  guest's copied in, and keep them for the mission; the labels on the upper
  screen at 199,175 (the player's own) and 216,120 (the ally). The field's
  upper screen shows no sprites (DISPCNT A 0x00210f18, OBJ off), so a name
  is drawn in the party's layer (BG3); one in the sprites' slot never
  showed. With three or four players the other labels are at y 96 and 72
  (the four-player shots); names on those rows not yet seen.
- **Leaving it.** In a session the pause asks "Are you sure?"; yes takes both
  copies to the game-over screen (ov027) in the same frame, which waits until
  both press A; then scene 6, "Mission Review" (ABORTED) on both.
- **A member gone.** The guest's process killed in the field: the host's
  connected bitmap loses the guest, `Game_RunSceneLoop` resets the console,
  and the game boots again in the same window into scene 12, the connection
  error (§1, Resets).
- **Over a slow network.** The same route with the session's delay and loss
  knobs on both copies (`KHDAYS_NET_DELAY_MS`, `KHDAYS_NET_LOSS`, one way):
  30 ms and 2% lost, the field entered 4 frames apart; 150 ms and 10%, 11
  frames; 500 ms and 10%, 31 frames -- the delay itself -- and the Mission
  Review reached every time. The game's packets are not lockstep: each copy
  goes on with what it has. Two things the port does for it: a datagram
  carries the records not yet acknowledged, so one lost is made good by the
  next; and the engine reads a received record as a run of messages up to
  its length (`CmdPacket_Dispatch`, into a packet slot that holds two of the
  sender's largest frames, `Session_LayoutPacketSlots`), so the records that
  waited go to it together. Before either, 150 ms and 10% kept up to 69
  records (more than a second) waiting for the game.
- **Through a server.** The same route with the local network off on both
  copies (`KHDAYS_NET_LAN=0`) and a `khdays-server` of the same PC
  (`KHDAYS_NET_SERVER`): the guest's list shows the room the server lists,
  the join is introduced by the server (CONNECT, PEER) and goes straight to
  the host; with punching off (`KHDAYS_NET_PUNCH=0`) it goes through the
  server after 1.5 s, and so does the whole session -- 7,670 datagrams
  relayed in a minute of it, the round trip still 0.1 ms. The game sees no
  difference: the same records, the same scenes, the Mission Review.
- **Over the internet (2026-10-06).** The `multiplayer` route with both
  copies of this PC sent through the maintainer's public server (a rented
  machine, `KHDAYS_NET_SERVER=212.227.76.27`, the local network off): the
  room listed there, the join introduced; the host's address as the server
  sees it is this PC's own router, which does not loop a datagram back to
  its own network, so after 1.5 s the guest went through the server, and the
  whole session with it -- a 75.7 ms round trip (this PC to the server and
  back twice), no record resent, the field entered 3 frames apart, the
  Mission Review 2. Copies behind two different routers try each other
  straight first.
- **What a session asks of the server (2026-10-06).** Its busiest second
  one way, counted by the server per pair and direction: 68 datagrams on the
  `relayed` walk, 98 at 150 ms and 10% lost (the resends and the records
  joined after a loss come in bursts). The server relays up to four times
  that, 392 a second each way per pair, and drops and counts the rest.
  Everything it holds -- groups, pairs -- counts against a memory budget (an
  eighth of the machine's memory, or `--memory`); past it, the one heard
  from least recently goes: real hosts register every 2 s and real sessions
  send dozens of datagrams a second, so a flood's own go first. With a
  1 MB budget and 64 senders flooding it at a hundred times a session's
  rate (the `khdays-server` test), a relayed session went on untouched
  (282 of 282 datagrams relayed, the longest gap 32 ms), the server holding
  1,023 KB of its 1,024, evicting 1,324 entries in a second.
- **A reflection through the players, closed.** A CONNECT with a forged
  source made the server introduce that address to a host, which then
  punched toward it every 100 ms for 3 s: one datagram to the server, thirty
  from a player's PC toward an address of the sender's choosing. Now a guest
  already introduced to a group is not introduced again, a host is told of
  introductions no faster than every 250 ms, and a host punches toward at
  most as many guests as its group has free places.
- **The walks.** `multiplayer`, `online` and `relayed`
  (`port/tools/regress.py`) play this route with two copies of the build
  at once, players 1 and 2, from the title to the Mission Review of the
  mission quit together -- on the local network, met through a server,
  relayed by it; both players must go through the same scenes.
  `multiplayer4` plays it with four copies through a server of this PC.

### 16.1 Who runs the battle

Counted 2026-10-06 with the session's census (`KHDAYS_TRACE_WM=1`: each
message by its gate, the first halfword of the game's packet on port 12):

- **The host simulates.** Each battle tick the host sends gate 0
  (`Ov022_SendActorStatePacket`: four 0x30-byte records -- the character's
  event word +0x464, position, HP, the replay's animation index +0x6bc, ...)
  and everyone sends gate 3 (`Ov022_SendControlPacket`: 12 bytes, the slot
  and the buttons held, the chosen command). The host applies its own gate 3
  at once (`MsgQueue_SendGate` in link mode 2) and a guest's through its
  request table (`Ov022_PublishRequestToSlot`); a guest copies gate 0 into
  the same memory (`func_ov022_020897dc`: the table and the states are a
  union at `data_ov022_020b2ea4` + 0x14) and draws every character from it
  (`Ov022_RunReplayFrame`). Shots are gate 9 (`Ov022_SendPlacementMessage`,
  `Ov022_RunShotCommand`), made by the host. So a guest's own action shows a
  round trip late: +2, +8, +12 frames at 33, 126, 207 ms, measured.
- **The tick.** The battle steps every second frame (frame mode 0, 30 a
  second) or every third (mode 1, 20 a second, `gObjSystem` at
  0x0204c058); `Ov002_ConstructGameplayScene` picks mode 1 with three or more
  players (`Session_GetSetup()` + 4). The port keeps mode 0 for any count
  (prepare.py's join at that function; `KHDAYS_MP_DS_PACE=1` keeps the
  game's choice): with four players, 150 ticks and 150 new 3D pictures in
  5 s, every action as quick as with two.
- **Two controls in one tick.** When two of a guest's control packets reach
  the host in one tick, the second overwrites the first's command in the
  request table: a press lost (78 of 100 attacks at 126 ms with 10% loss).
  The port hands the game one control per member per tick: 86 of 100, as
  with no loss (the 14 the game's own: Xigbar's reload).
- **One a tick, as the guest made them.** Over the internet the controls
  come unevenly -- two in a tick, then none. Handed as they came, the host
  ran a held button a tick longer or shorter than the guest had, and the
  guest's prediction of its own character was corrected 8 to 20 times a
  second (the first online test, 2026-10-07: the character jerked and spun).
  The host now hands each member's controls from a store kept ahead of the
  game: filled to its size first, one a tick after; a tick with none grows
  it by one, a whole second too full leaves out one held repeat (never a
  command), ten seconds without running dry shrink it by one. With 30 ms
  one way and up to 25 ms more at random (`KHDAYS_NET_JITTER_MS`), the
  corrections went from 30 of the position and 58 of the heading to none,
  the store one control deep (the guest's actions a tick later on the host).
- **More than one send a tick, with enemies.** In the field with no enemies
  the host's game sends on port 12 once a tick (gate 0, with gate 11's
  acknowledgements in the same send). With enemies it sends more often than
  it ticks: in mission 0101's first area (served as the tutorial by a mod,
  two copies, `KHDAYS_TRACE_WM=1`), 1,882 frames of the field with two sends
  -- the tick's (gate 0, with gate 11's acknowledgements or a gate 9 shot)
  and one of three gate 1 messages, the reliable channel's -- and 61 with one
  send of gate 11 alone.
  Read from a host's session log of the first four-player
  mission with enemies (mission 0113, 2026-10-07): per 5 s, 150 sends with
  gate 0 (150 ticks) but 100 controls handed and 100 ticks counted dry for
  each guest -- 200 counted ticks. The store took each send for a tick: every
  extra one ran it dry and made it one deeper, and it never stopped growing --
  1,560 ticks deep after three minutes, up to 922 of a guest's records
  waiting for the game behind its held controls, its acknowledgements among
  them, so the host's reliable channel resent ever more (gate 11 sent 3,243
  times in 5 s, against 30 in the census above), until a guest's link failed.
  The tick is now a send that carries gate 0 (on a guest one that carries
  its gate 3), and the store grows no deeper than the longest gap between
  two of the member's controls in the last 20 s (`khdays-net-test`'s paced
  case: the host sending twice a tick, the guest once: the old store had a
  control 3.3 s late after 18 s, now none later than 16 ms; in mission 0101
  at 35 ms one way and up to 25 more, the store one control deep, never dry,
  at most one record waiting, the guest's prediction off by 0.20 units at
  most).
- **A guest's own character, predicted.** On a guest the port runs its own
  character as a host runs its own (`runtime/net/mp_predict.c`): inside its
  update the game's "am I the host" checks of the battle's actors and the
  characters' overlays answer as a host's -- not those that hit or damage
  others, change HP, items or the mission's tallies, or send (prepare.py
  `ROLE_KEPT`); its last control copied in by the game's
  `Ov022_LoadNodeFromResource`; first `func_ov022_020ad474` (which resets the
  event word and merges the pending one: without it every attack after the
  first stops two frames in). The host's record of it is compared with the
  guest's predicted path: never off at one way 0 to 100 ms, off by 0.05 units
  at most with 10% loss (moved by the difference). A hit the host decides
  (its record's event word 0x100 for the 28 ticks of the stagger) is shown
  as the host runs it, then the character restarts from the idle state as
  `Ov022_ResetActorAfterPlace` does. Its update's messages are left to the
  host, but for its shots (gate 9, the only ones it makes): those it applies
  at once as a host applies its own (`dispatchByObjTypeBits`, the way
  `MsgQueue_SendGate` and `AsyncMessage_Flush` apply a host's), and the
  host's copy of each is left out of the host's record when it comes (the
  first own shot with the same first four bytes: operation, owner, mode,
  channel, shot). At 10% loss, 172 of 172 shot commands applied at once and
  their 172 copies left out, none different by a byte. Measured with two
  copies on one PC: the guest's attack at +10 to +11 frames at
  round trips of 0.1 to 206 ms, the host's own at +11 (+17 to +27 before);
  its weapon's shot (the projectile's sound) at +14 to +15, as the host's run
  of the same shot at no delay (+24 and more before). **Off by default since
  2026-10-08** (`KHDAYS_NET_PREDICT=1` turns it on): in a mission played
  online (mission 0113, the maintainer a guest at a 23 ms round trip, the
  network clean) the host's record moved the predicted character 60 to 140
  times every 5 s, by up to 0.6 units and at times 2 to 3, and in the 5 s
  report right after an area change (the field built again at 700.9 s, the
  report at 703.2 s) by 46.65 units -- why not traced; the path kept from the
  area before is a candidate -- and the character went through the floor and
  died (`se_sys_multiplayer_dead` from 705.4 s). The game's route
  costs a guest its own actions a round trip later: 2 frames at 33 ms.
- **What keeps the prediction with the host's run.** A shot channel's budget
  (+0x18 of a channel at the actor's +0x2644) is spent by every copy's spawn
  and given back only by the host's commands -- a guest's own predicted
  reload does not refill it -- so a guest must spend exactly as the host: a
  host copy that comes before the guest makes the shot (the host can run a
  press a tick before the guest's own update) is applied and the guest's own
  then not; an own spawn the host never made gives its unit back. After a hit,
  the host's run is shown until its animation is one the character shows in
  its idle state (`func_ov022_020a37d0`), not just until the stagger ends; a
  host animation the prediction does not show for longer than the path leads
  is shown so too. The record's heading (+0x18, `func_ov022_02088254`) is
  kept with the path as the position is.
- **Four on the HUD.** With four members the allies' rows sit at y 120, 96
  and 72 of the upper screen (their labels; the portraits up to 8 above), the
  third above the one-screen shape's top of 84 of the time; with the
  battle's member count at 4 (`data_ov022_020b2e78`[1] + 0x34, what
  `func_ov022_020882f8` returns) the shape starts at 64.

### 16.2 The port's list of rooms (2026-10-09)

Asked for by the maintainer: the groups' list as the save list (Dream Drop
Distance HD's pieces) with each room's name, players and ping (through the
server, as a session with it would be), as many rooms as there are, a search
by name, and a room's own name and optional password.

**The game's list, read in the C.** "Select a group to join" is the screen
whose update is `Ov006_UpdateMissionMenuOptionScreen` (scene 7, after
`Ov006_MissionMenuTick`) or `Ov008_UpdateMissionMenuOptionScreen` (the Mission
Mode camp), the same code in both overlays. The rooms it knows are in the
session context (`MissionGlobals`' first word: `data_ov006_020565e4`,
`data_ov008_02090f24`): +0x100 their count, +0x104 up to four WMBssDesc of
0xc0 bytes, each its host's id at +4 -- `Ov006_MissionUpsertRowByKey` keeps
the first four found, `Ov006_MissionExpireRows` drops one after 600 frames
unheard. Its menu (`data_ov006_02056660`, `data_ov008_02090fa0`: pointers)
has the cursor at +0x38: 0 "Create a Group", 1-4 the rooms. A there goes
`func_ov006_020505b0` → `Ov006_CheckMissionMenuConfirmInput`: cursor 0
creates, another commits room cursor - 1 (`Ov006_MissionCommitRowSelection`
→ `Ov006_MissionSelectStateCallback` → `WH_ChildConnect`); B leaves. Its
words are `UI/mlt/mlt_&.s.z`'s: 0x3c "Elige un grupo para unirte.", 0x3d the
note, 0x3e "Crear un grupo", 0x3f its keys (the DS's A and B), 0x40 on the
members' screen. A join refused for a full room (`WM_ERRCODE_OVER_MAX_ENTRY`)
or a closed one (`WM_ERRCODE_NO_ENTRY`) takes the game back to its list -- when the wireless
layer's state is 8, a failed connection: `Ov006_UpdateMissionMenuConfirmScreen` goes back to
`Ov006_MissionMenuTick` on 8 and shows its connection error on 0, 9 or 10, and
`Ov006_MissionSelectStateCallback` reads the two codes there. The port's layer gave 9 until
2026-10-15 (seen: a wrong password ended in "Error de conexión" and the title); it gives 8 now
(seen: back on the list, the password said wrong, the room still there).

**The rooms' details.** A room's announcement (`ANNOUNCE`, and the server's
`REGISTER`/`ROOMS`, which keep it whole) carries, after the game's
information, "KHR1", the host's round trip to the server, the players it
takes, a password flag and its salt, and the room's name (`net_session.c`);
its byte 11 was already the guests in it. A password is proven with a digest
of the salt, the guest's id and the password's digest; a join without the
proof is refused (`KHDAYS_NET_REFUSE_PASSWORD`, which the wireless layer
gives the game as a closed room). The ping shown is the guest's round trip to
the server (LIST to ROOMS) and the host's (REGISTER to REGISTERED), or on the
local network a ping straight to the host.

**The port's list** (`runtime/net/roomlist.c`, `host/screens/room_screen.c`, in the HD
theater's chapter list's pieces from `tools/hd/theater_mission.py`):
Create a Group and the search first, then every room known (the client
already asked the server for every page); on a room, the game is told of that room only
(`khdays_net_scan_only`), and A puts the game's cursor on its row and hands
the game A; Create a Group first asks the room's name (the owner's nickname
by default) and password. Seen (2026-10-09, two copies of this PC, a copy
without a window as the host): on the local network and through a
khdays-server of this PC (the local network off), the host's room listed on
the guest's list as "Sora", "Anfitrión: Sora", 1/4 players, 0 ms; the guest's
UP and A, and both on the game's members' screen (J1 Sora, J2 Riku2). The
same in the theater's pieces (the second day): the guest's DOWN twice, over
the search to the room, and A.

**The members' screen** is `Ov006_UpdateMissionMenuSelectionScreen` (and
ov008's) on both sides. Its menu's +0x20 is 1 on the host (the game's
`sessionReady`); the session context's +0x434 has a bit for each player there
(the host's bit 0, a guest's its aid) and +0x436 their names (11 UTF-16
characters each). It writes 0x40 "Buscando jugadores..." on the host (a
guest's alternates 0x34 "Espera..." and 0x40 every 60 frames), 0x41 or 0x42
as the note, 0x43 "Seguir" with the cursor once `WH_GetBitmap` has two
players, and its keys 0x45 (host) or 0x46 (guest); the host's A goes on only
with two players (`Ov006_ReadMissionMenuAction`). The port draws it (a
card for each of the four places, the room's name, the ping to each as the
session measures it, Proceed lit with two players) and leaves its buttons
to the game. After the host's A the game runs `Ov006_MissionBuildOptionRows`
(ov008: `Ov008_MissionMenuWaitReady`) while the screens fade to black, then
the character select; the port keeps the room through it (seen: captures
every 0.4 s from the host's A, the room fading out and the character select
fading in, none of the DS's screens). Seen (the same two copies): the host's "Sora" with Proceed dim,
then Riku2 in at 0 ms and Proceed lit; the guest's Sora "Anfitrión" at 0 ms,
its own row "Tú", "El anfitrión busca a más jugadores. Espera...".

Between the list and the members' screen the game runs `func_ov006_020505b0`,
`Ov006_CheckMissionMenuConfirmInput` and `Ov006_UpdateMissionMenuConfirmScreen`
(ov008: `func_ov008_0207c498` and its two), about a second while a guest
joins; the port shows its members' screen through them too (seen: captures
every 0.4 s of the guest's join, none of the DS's screens). The game's bars
for the four places (`UI/mlt/res_&.p2` entry 0, cells 0-3, coloured with
`res.p2` entry 1's palette) are blue, red, yellow and green (their labels'
middle shade: 24,49,148; 148,24,33; 148,115,24; 24,148,41) and labelled 1P-4P
(English), J1-J4 (Spanish and French), S1-S4 (German), G1-G4 (Italian); the
port fills each row's circle with its place's colour and writes its label
in it.

**Without the theater's pieces: the game's own screens (2026-10-15).** The same
list (`roomlist.c`: every room, the search, a room's name and password, the
password asked, the pings) is drawn by the game's own "Select a group to join"
and members' screen, in its look, as the maintainer chose. Read in the C: each
frame `Ov006_UpdateMissionMenuOptionScreen` draws its four labels with
`Ov006_MissionDrawTextRunFwd(text, 0x57, 0x23 + 0x18 * i, colour, 1, 0, 0)` (colour
1 white for the rooms it knows, 3 grey for the others, whose label is
`data_ov006_020563d4`, "------"), 0x3d (the note) centred at (128, 96) on the
upper screen, 0x3e (its button's word) centred at (128, 152); the call's align is
0 left, 1 right, 3 centred (`Ov006_MissionDrawTextRun`). It writes its cursor to
the scene's +0x95ac (`Ov006_MissionScene_SetByte95AC`), where its hand is drawn
from: 0 the button, 1-4 the plates; the plate under the hand turns red, the
button too. Its +0x950c is the link level (`Ov006_SetMissionCursorSelection` is
misnamed: it is given `Ov105_WH_GetLinkLevel()`), the 16x16 icon at (240, 160) on
the members' screen. ov008's list and members' screen are the same code
(`Ov008_ForwardSevenArgs` its text call). Measured (no mods, two copies): the
plates are five sprites, x 72-192 at y 32, 56, 80, 104; the button 56-200 x 140-164.

prepare.py's hooks (`_rooms2d_hooks`, the four functions in both overlays) give
those calls the port's strings and the hand the port's cursor (`ui/rooms2d.c`):
the rows in the game's order -- the search, every room, Create a Group on the
button -- four on the plates from the one on the first (moved by one past the
first or last plate, as the save lists), a room's name cut to the DS's ten
characters, its players right-aligned at x 184, the step's note or "wrong
password" in place of 0x3d, "Join" in place of 0x3e on the password step; the
members' screen the room's name centred at (128, 24) and each member's ping, or
"You", right-aligned at x 208. The strings go into buffers the hooked function
declares: the game's code reads only its own memory, and a host pointer stopped
the game (`khdays_reference`). The list opens on Create a Group and goes round,
as the game's (`Ov006_UpdateMissionMenuCursor`: its repeat after 6 frames, then
every 2, and its sounds `PlaySound(0, 0)`, `(0, 1)`, `(0, 3)`); Create a Group
opens the name and password on the plates with the hand on the button, so A, A
creates the room as the DS's one A did. Each room's signal is the game's link
icon (`UI/mlt/res.p2` entry 1, cells 12-15: one bar in red to four in green, link
levels 0-3) for its ping read as a member's (under 100 ms 3, under 200 ms 2, else
1), right of its plate; a room with a password a lock before its players in the
icon's own colours (the game has none: none of its fonts holds one;
`ui/room_icons.cpp`). Seen (2026-10-15, copies of this PC on the local network,
two screens and one, the five languages): six rooms reached past the plates, two
with a lock; a wrong password back on the list said, the right one joined; a
room created as "Sala Riku" with a password listed so by a third copy; the
members' screen with the room's name, "0 ms" and "Tú". With the theater's pieces
the theater's screens, as before.

Two faults found on the way, which the theater's list had too: a frame the PC
held up (a capture saved) was taken for the game leaving its list, which opened
afresh with the cursor on Create a Group (the list's presence is now counted in
the game's frames, not VBlanks); and the cursor moved onto another room went back
to the room it was on (the rows are now rebuilt after the cursor's row is noted).
`KHDAYS_TEST_TYPE` types texts into the fields for a run without a keyboard
(`|` between them).

### 16.3 The character select on one screen (2026-10-09)

Mission Mode's character select, alone and in a group, is the update
`Ov006_UpdateMissionMemberMenuScreen` (scene 7, after `Ov006_MissionBuildOptionRows`;
`Ov008_LobbyStep` is its twin in the Mission Mode camp, scene 19: the camp's
Character option, alone or in a group -- `Ov008_RouteCommittedPageState` ->
`Ov008_EnterSubMenu` -> `Ov008_MissionMenuArmWireless` -> `_Enter` -> `_WaitReady`
-> `Ov008_LobbyStep`, the camp's screen index -1 meanwhile). Seen (2026-10-10,
the maintainer's save, alone): Character opens the port's select screen as in
scene 7, the moves Roxas, Xion, Riku, Goofy, Donald, A on Donald back to the
camp's menu with Donald's model, face and numbers. What it holds:

- **The places**: the session context (MissionGlobals' first word) +0x4ac, six
  bytes a place (`Ov006_GetMissionRowInfo`): +1 bit 0 a player there, bit 1
  chosen (its A), +2 its character, +4 a count of its moves; the players'
  names at +0x434 and +0x436 as on the members' screen.
- **The characters**, in the game's order (`UI/mlt/mlt_&.s.z` records
  0x0b-0x1d their names, 0x1f-0x31 their weapons, 0x1e and 0x32 the "¿ ?"):
  0-12, the Organization's thirteen, always; 13-18 -- Xion, the king, Sora,
  Riku, Donald, Goofy -- only with their bit in the menu's +0x30
  (`Ov006_ResolveMissionSelection`). The DS's grid (`data_ov006_02056410`, two
  rows of twelve) holds the thirteen, and the bought ones are put in it
  (`data_ov006_020563f8`'s six, `Ov006_FindMissionGridPosition`); the others
  are not shown at all. The maintainer's save shows 17 (no king, no Sora); a
  copy without a save, 13.
- **A move** (`Ov006_UpdateMissionMemberSelectionInput`): left and right along
  the DS's row, up and down between its two, never onto a character another
  place is on; then `Ov006_MissionArmCursorRequest` -- the host's row itself,
  a guest's request at +0x4e0 (the same six bytes, sent to the host, which
  answers with the row) -- and no other move until it is answered
  (`Ov006_MissionIsEntryActive`: the request's count is the row's). A
  chooses, B unchooses; START with every place chosen goes on
  (`Ov006_SetPendingInput`): the update returns
  `Ov006_MemberMenuNextStateNoOp` while the screens fade out to scene 19.
- **The gauges**: nine bytes a character from `data_ov006_020561e0`
  (`Ov006_MissionApplyParameterRow`; row 0x13 the "¿ ?"'s): the left column --
  VT máx., Fuerza, Defensa, Magia, the captions `data_ov006_020561d0` bytes 6-9
  -- from bytes 4, 0, 2, 1; the right -- Vel. ataq., Letal %, Bon. let., bytes
  0-2 -- from 7, 3, 8. Seen against the DS's own gauges with Donald: 1, 1, 1,
  10 and 4, 4, 3.
- **Its messages**: the heading 0x33 (choose), 0x34 (wait), 0x37 (START), a
  guest that chose 0x34 and 0x35 in turn; the help 0x38, 0x39, 0x3a (ov008:
  0x36 or 0x33, 0x3b or 0x38).

The port draws it on one screen (`runtime/menus/charselect.c`, `host/screens/select_screen.c`,
the pieces from `tools/hd/theater_mission.py`): the characters the game
lets be chosen, in its order, five a row; the directions are the port's, and
a move is written as the game's own request right after its update, under the
same conditions, so it is one step whatever the grids (an A pressed before it
is answered waits for the answer). Seen: alone with the maintainer's save --
Roxas, right to Xion and Riku, down to Goofy and round to Xaldin, left to
Xigbar, up to Donald, A, START, scene 19; captures every 0.3 s show the port's
screen fading in from black and out to black, none of the DS's. Two copies on
the local network, no save: J1 on Xemnas, J2 on Xigbar; the host's right went
over Xigbar to Xaldin; the guest's left twice (Xemnas, then round to Roxas),
right three times, the third over Xaldin to Vexen; its A gave "Espera al
anfitrión." and "Espera..." in turn and its plate the red ring.

### 16.4 The camp menu on one screen (2026-10-10)

Mission Mode's camp (scene 19, ov008) runs one screen of its table A
(`data_ov008_0209009c`, eight classes, its index the word at
`data_ov008_02090f0c`) and one page of its table B (`data_ov008_02090064`, its
index the next word), while its object stays in `Ov008_RouteCommittedPageState`.
The menu page is screen 0 (`Ov008_SetupScene`: the options, the description
and the character's 3D model on the upper screen) over page 0
(`Ov008_TickMenuState`: the status panel on the lower). Screen 1 is not the
menu but the holo-missions' world map (`Ov008_MainMenu_StateTick`: its
`+0x13fc` is `world_id_&.s.z`, its "entries" worlds, a locked one written
`select_&.s` record 4).

- **The menu's state** is the camp's context's +0x959c (the context the
  second word of `data_ov008_02090f04`; `Ov008_GetMenuContext`): +4 the
  `root_&.s.z` table, +0x88 the entry selected (`Ov008_HandleMenuEntrySelection`:
  101-107 Mission Mode's seven options, 7 and 8 a question's answers), +0x5c4
  the record of `root_&.s` that describes it (`Ov008_MapMenuObjectTypeToIcon`:
  101 10, 102 9, 103 29, 104 30, 105 31, 106 16, 107 17; else 8, the empty
  one). The screen's class has no B (`pfnCancel` 0).
- **The line over the menu** (seen 2026-10-14, two copies, `KHDAYS_TRACE_CAMP`): the menu's state
  +0x5c0 is a `root_&.s` record `Ov008_Menu_UpdateDirectionalPrompt` chooses and the DS draws at its
  top band's right -- the host 0x17 "Select a mission.", then with a mission chosen 0x19 "Waiting
  for the other players...", once every guest accepted 0x1a "Press START to begin."; a guest 0x1b
  "Host is selecting a mission...", then 0x16 "Press START to join.", once accepted 0x18 "Press B
  to withdraw."; 0x1c the communication error. After START both copies' entry is 9 (the group
  waiting for the mission), its description record 8 (empty): `Ov008_DrawMenuValue` draws 0x20
  "Stay waiting for your mission." instead while the game state's flag 0x200c is set and the
  session is not ready. The port's camp did not know entry 9, so a guest that accepted saw the
  DS's two screens again (a tester's "P2 back into the original menu"), and it drew no line: no
  way to tell the host had chosen a mission, nor a START to press. Now it reads both (`camp.c`
  `notice`, `waiting`): the line in the top band before the settings key, the options dimmed and
  no hand while waiting. Seen: the host's three lines and the guest's three in turn, the guest
  in the port's camp until the field.
- **Title Screen's question** (`Ov008_Menu_ToggleDetailPanel`): A on 107 shows
  entries 7 and 8 (yes, no) -- shown only while it is open -- with the cursor
  on 8, and writes `root_&.s` record 0x15 ("¿Salir del juego?") in the
  description's place (the panel's +0x5c6 bit 5 set while it is open; on
  leaving it the cursor goes back to 107). Seen (2026-10-10): A on Title Screen
  8, LEFT 7, RIGHT 8, B back to 107.
- **The options' names** are pictures only: `UI/cm/cmo_&.p2` entry 1 (its
  palette entry 9's), cells 14 (Holo-Missions), 15 (Panels), 22 (Shop), 23
  (Character), 24 (Mission Config), 18 (Save), 19 (Title Screen); 16 Files, 17
  Config, 25 Tutorials, 26-30 the story camp's others; 32-47 and 48-62 the
  same in two other colours; 0 and 1 PLATINES and PUNTOS CORAZÓN, 68-74 the
  members' places (MISIÓN, PANEL, TIENDA, ...). No string table has them.
- **The money and the heart points** are the game's state's +8 and +0xc
  (`gGameState`; seen: 157,458 and 46,509 on the maintainer's save, the DS's
  PLATINES and PUNTOS CORAZÓN), both put on the screen in eight digits up to
  999999 (`Ov008_PushCountersToEventFlags`). The shop's price check takes +0xc
  (`Ov008_CanConfirmShopRow`, which the decompilation calls `nMunny`): the
  heart points, by these values (the shop itself not watched yet).
- **The status panel** (`Ov008_GetPageB`'s, `Ov008_DrawStatusField`) draws
  each field when it changes: 0 the name, 2, 3, 5, 6 the level's four (Nv., VT,
  Habilidades, Vínculos: `status_&.s` records 0-3), 7, 9, 0xb Fuerza, Magia,
  Defensa (up to 999), 0xc Letal % (Q12, up to 100), 0xd Bon. Letal (up to
  255), 0xe EXP, 0xf the next level's (negative: "--"), 0x11 the slots, two
  values (the second 120 when the game's count at +0x811 is over 0x69), 0x12 the
  trials, two counts, 0x13 the rank (a record of `status_&.s`, 15-20), 0x14
  its tab; labels records 4-14. With an animated change it adds the panel's
  offsets (+0x1dc, +0x1e0).

The port draws the menu page on one screen (`runtime/menus/camp.c`,
`host/screens/camp_screen.c`, the pieces from `tools/hd/ddd_camp.py`; prepare.py
joins `Ov008_DrawStatusField` to the port): the one-screen picture shows the
screen's backdrop and the game's own 3D model -- the upper screen's whole 3D, its
frame (117, 29, 87 x 101, before BG0's scroll) moved and scaled so that the frame's
117, 38, 80 x 89 fills the screen's window (`KHDAYS_SHAPE3D_ALL`, `whole`,
`host/render/onescreen.c`); the screen draws the rest over it, its backdrop again
around the window, so that what reaches out of it (Xaldin's lances) is cut there as
the DS's frame cuts it. At first only the polygons with their middle in the frame
were placed: Marluxia's scythe lost its blade, its shaft and tip left floating (the
maintainer's screen, 2026-10-14); with the whole 3D placed the blade shows, cut at
the window's edge. Seen (2026-10-10, the
maintainer's save): Roxas and Xaldin (the tallest, his head at dot 43) whole, the
options and their descriptions as the cursor moves, the numbers the DS's, in
Spanish and English; Holo-Missions and back, the game's screens and the port's
again. Title Screen's question is the port's too: the question in the
description's place -- in the port's words, "Return to the main menu?" in each
language (`TXT_CAMP_BACK_TO_MENU`, the maintainer's, in place of the game's
"Quit the game?") -- its two answers in DDD's save screen's buttons (layout 7,
nodes 2 and 3) with the game's yes and no (`UI/cm/str/sav_&.s` records 5 and
6), the hand at the one the cursor is on, the options dimmed and the prompt
whole (B goes back). The status panel's last row is the game mode, as the DS's
lower screen has it (`cmo_&.p2` entry 2, cells 20-24, pictures only: its label,
the story's three difficulties -- the game state's two bits at 0x40a -- and
Mission Mode's own). Every other screen and page is the game's.

**The story's camp menu** (ov025, the Grey Area's, START in its field) is the
same code under other names and addresses: its screens' table
`data_ov025_020b4ab0` (index `data_ov025_020b574c`, the page's the next word),
its context the second word of `data_ov025_020b5744` (+0x959c the screen's
state), its object in `Ov025_GetIdleHandler` while a screen runs, its status
panel `Ov025_DrawStatusField` (the same panel). Its menu screen (screen 0,
`Ov025_SetupScene`) keeps the entry the cursor is on at +0x88 as the entry's id
(`Ov025_Hub_OnEntryHover`): 1-6 and 9 its seven options top to bottom (Panels,
Holo-Missions, Files, Tutorials, Config, Save, Title Screen; `root_&.s`
records 9, 10, 12, 15, 11, 16, 17 their descriptions), A on Holo-Missions 10
and 11 (Normal Missions, Challenges: 18, 19), on Files 12 and 13 (Roxas's
Diary, Enemy Profiles: 13, 14) -- the game's list shows those two in its place
--, 7 and 8 the answers of the question its +0x5c0 says is open
(`Ov025_Hub_SetSubMenu`: "¿Salir del juego?"). Its options' names are
`cmo_&.p2` entry 1's cells 15, 14, 16, 25, 17, 18, 19, the two-option lists'
26, 27, 29, 30. Its frame of the model is larger than Mission Mode's (the upper
screen's 115-253 x 20-140 dots with its edge) and its camera nearer; its day
(DÍA 357 on the frame) the game state's field 9. The port draws it as Mission
Mode's (no group, the day's tag on the model's box, the game's "DAY %d" of
`UI/btl/&/info.s.z`), its model's view as large as Mission Mode's (Roxas as wide
and in the same place, measured on the two camps' pictures), its question as
Mission Mode's: the same record, 0x15. Seen (2026-10-10, the maintainer's save,
day 357): the options with their descriptions, A on Holo-Missions and its two
options, B back; Roxas whole in the box, as wide as Mission Mode's; A on Title
Screen 8, LEFT 7, RIGHT 8, B back to 9; "Experto" as the game mode.

Its options the game has not opened yet (`Ov025_UpdateMenuOptionLocksAndDrawCategory`, run
by `Ov025_Hub_InitEntries`): Panels until day 11, Files until day 8 (the game state's field
9), Tutorials until one of flags 0x3c2b-0x3c63 is set, Holo-Missions while the screen's +0x8c
and +0x90 are 0; its return the option the cursor starts on (the first open one of Panels,
Holo-Missions, Files, Tutorials, else Config). A locked option's entry has its alternate set
on (`Ov025_PushSubitemSet`: +0x84 bit 0), which the DS draws dimmed and the cursor passes
(`Ov025_MoveFocusByDpad` skips an entry whose +0x84 bit 0 is set); the entries are the
context's list at +0x4a38 (an `NNSFndList`), each one's id at +0xc (`Ov025_FindEntryById`,
the same in ov008). The port reads that bit for the rows it shows and dims their names. Seen
(2026-10-10, a new game, day 7): Panels, Holo-Missions and Files dimmed, the cursor from
Tutorials DOWN to Config and Save and back; the maintainer's save (day 357) none.

**The Preferences page** (both camps' screen 5: `Ov025_Config_Setup`,
`Ov008_SetupItemMenu`) keeps in the screen's state (the context's +0x959c, as
the menu's) +0 the tab shown, +2 its list of tabs (0 the story's, 1 Mission
Mode's; 2 has the first two tabs only), +0x4c the setting the cursor is on,
+0x4e each setting's value (two bytes each; `Ov025_Config_LoadValues` fills
them from the game state, `Ov025_Config_SaveValues` writes them back). Its
tables: the settings (`data_ov025_020b4f64`, `data_ov008_02090598`, 25 of
0x14 bytes: +2 the name's `cfg_&.s` record, +3 the help's, +6 enabled, +7 the
values' count -- 0 a heading --, +8 the values, 4 bytes each: +2 the name's
record, +3 the help's), the lists (`data_ov025_020b4ee8`,
`data_ov008_0209051c`, 0x12 bytes: +2 the tabs' count, +3 each tab's page)
and the pages (`data_ov025_020b4ed0`, `data_ov008_02090504`: the first
setting and the count): the story's tabs are settings 0-2 (Controls, Chase
Cam, Cam Speed), 3-6 (the camera's axes, Cursor Position, Command List) and
7-9 (the heading "Combat Partner Behavior", Target, Support), Mission Mode's
0-2, 3-6, 10-14, 15-19 and 20-24 (the mission's rules). The DS draws a tab's
settings, each one's values side by side, the chosen setting's help and its
value's help in yellow (`Ov025_DrawMenuPageTexts`), a setting not enabled as
"?" (record 3); the title over the tabs is record 0 ("Adjust your
settings.") or the tab's (`SelectTierIfActive`: Mission Mode's first two
tabs 1, Player-Unique Settings, the others 2, Universal Settings). Seen
(2026-10-10, the maintainer's save): DOWN and UP go round a tab's settings,
RIGHT and LEFT round a setting's values (not a setting not enabled), R and L
round the tabs, the cursor on a tab's first setting but its heading; alone
in Mission Mode settings 15-18 and 24 show as "?" (flag 0x200d, see
`DECOMP_FINDINGS.md` 1.4). The port draws the page in the menu (the
maintainer's mock-up, 2026-10-10): Preferences lit, the other options dimmed,
one panel where the model's box and the status panel are
(`camp_page_backdrop.png`), the game's tabs grouped by their titles into
sections (the story's player settings, then its heading's; Mission Mode's
player and universal settings) as the settings menu's tabs between L and R,
the section's settings as its rows with the names and values between its
arrows, every setting there ("?" where the game has it), the hand at the
one chosen, a scroll bar's thumb past ten; the help and the value's help in
the description's place. Its directions and L and R are the port's
(`khdays_camp_buttons`): a step over a section's settings, or a section's
first, pressing the game's own R, L, DOWN and UP to take its cursor there
(the game's sounds); RIGHT, LEFT and B the game's. Seen (2026-10-10): the
story's sections from Controls to Command List over two tabs and round,
Combat Partner Behavior's Support changed (Damage to Recovery); Mission
Mode's fifteen rules with the five "?", scrolled to Lives; four copies on one PC
(the four-player walk's way into the camp, the host's Preferences): Friendly
Fire (Low), Friendly Lock-ons, Rank Crowns and Handicaps enabled with their
words, Handicaps changed (No to Yes), Limit Breaks still "?" (its +0x9b0 not
set in a new game). The scroll bar is DDD's save screen's track and thumb. With
three players or more the game steps every three VBlanks: the camp's view
must last that long between its updates (it flickered off at two, a held
direction counting again). The one-screen
picture of a backdrop of its own and no 3D is made at the internal size, not
the DS's (`gpu_present.c`).

**A group's members** (`Ov008_Menu_RefreshSlotPanel`, which shows each one's
face, its place's label and a tag of where it is): the places in the camp's
mask (its heap, `data_ov008_02090f00`, +0x2c: 1 alone, `Ov008_UpdateMenuInput`)
but those whose record says 7 (`Ov008_BuildSlotMatchMask(7)`); a place's record
is the heap's +0x2e, six bytes a place (`Ov008_GetPlayerRecord`), its +3 where
it is: 0 the menu, 1-6 the options' pages in their order, 8 ready -- it
accepted the mission (`Ov008_AreSelectedMenuSlotsReady` counts the places at
8). The tags (`Ov008_PageToIconRow`: 1 to 6 rows 0-5, 0 row 6, 8 row 7) are
`cmo_&.p2` entry 1's cells 68-75 (MISIÓN, PANEL, TIENDA, PERS., CONFIG.,
SALVAR, MENÚ, ACEPTAR in Spanish), pictures only. The port draws a plate a
place along the top band, where the DS has it: its colour and label, its
face, its player's name (the character's when the session has none), and
where it is -- the option's name, DDD's title word (Menú, written as text), or
the game's word for accepting (`mlt_&.s` record 63's) with the plate ringed in its colour. Seen
(2026-10-10, two copies on the local network, the multiplayer walk's inputs,
no save): J1 and J2 on the menu; the host's Holo-Missions, its plate on the
guest's "Holomisiones" and back; the guest's START to join, its plate on the
host's "Aceptar" and ringed in red.

## 17. Mission worlds and their rooms

Read from the ROM's data over all ten shipped worlds (`mi/wd/wd_{al,aw,bb,eh,he,nm,pp,tt,tw,zz}`)
with khdays-studio (`--world-info`, `--check-worlds`) and from the decompilation's loaders; the
movement rows in the game (the native build, `KHDAYS_TRACE_PLAYER`).

- **A world** is a P2 archive: entry 0 the room table, entry 1 the gate models, then each room's
  collision and its KAPH models. A room (`Ov002_LoadPeerIntoSlot`, `Ov002PeerRecord`): +0 its id
  (what missions search; ids repeat within a world -- a room is its place in the table), +1 its
  link rows, +2 its objects, +3 the archive entry of its collision (empty in 8 rooms), +0x14 and
  +0x18 two budgets (the second five times the first), +0x1c the link rows (8 bytes: key, target
  slot, pad, gate state, flags, radius), +0x20 the objects (u32: flags, archive entry -- the KAPH
  models it draws; 196 of 209 point at a KAPH, 13 at empty entries). 8 collision entries and 14
  model entries are shared by two or three rooms.
- **A room's collision** (`Coll_ResolveModelBlobPointers`): a 0xb0 header (+0x70 the resource
  flags `Ov002_LoadPeerIntoSlot` requests, +0x74 the flags whose 0x8000 says "pointers resolved",
  zero on disk; +0x84 the quadtree's root: centre x, centre z, size -- a power of two at least the
  faces' extent -- and the smallest node's size), then the placement markers (0x18 bytes: name,
  x, y, z, a facing in 65536ths; `pentKK_i` is the i-th place a character arriving through gate KK
  appears, `Ov002_ResolveNamedPlacement`), the tree (0x20 nodes: per-list child bits, the first
  ground/wall/ceiling of its run, parent, four children), the grounds (0x88), walls and ceilings
  (0x84) and the named records (0x14). A face: bounds (min x, min z, max x, max z -- all 14,264
  faces), flags (0x8000 the last of its node's run; nothing else on disk), 3 or 4 corners (convex
  but one), its plane, slots at +0x80 (0 gates; 1 and 2 surface tags and walls that open; a
  ground's slot 3 a small number 0-14 that is no name) and, for a ground, one word not read.
- **A world's gate models** (entry 1: a count, then 0x90 each, keyed by a link row's key): five
  descriptors of 0x1c as `Ov002_BuildModelSlot` uses them -- kind, the archive entry it loads (0
  none, 255 the gate itself, 45 and 46 Twilight Town's `gate_lock2` and `lightwall`), a turn in
  65536ths, the position (three words, the pointer `Render_SubmitNode` takes) and a scale equal on
  the three axes. `wd_tt` key 6's first is at (4.78, 0.03, -0.82) turned 90 degrees, on room 0's
  `gate06` wall (x 4.51).
- **The planes** on disk are only the face's: its normal (v1 - v0) x (v2 - v0) normalised to
  4096, its distance at v0; the four edge planes are zero and made at load by
  `RoomPoly_BuildPlanes` (`RoomPoly_BuildPlanesVertical` for grounds: the edges stand vertical).
  The game's arithmetic gives the stored face plane for 15,018 of the 15,104 faces the rooms
  load (2/4096 on a normal); the others were made by the tool that made the data.
- **The corners turn towards the side a character stands on**: (v1 - v0) x (v2 - v0) points up
  for 4,119 of the 4,124 grounds and down for every ceiling. The game's walls are vertical (their
  normal's y at most 0.11), its ceilings face down (y at most -0.28), its grounds up (y at least
  0.29 but for 5).
- **The walk's depth** (`CollCast_TestModel`, `Coll_WalkSpatialTreeForHit`): eight frames at
  `data_027e06e4` (0x10 each, up to the hit record at `data_027e0764`), one per level; the game's
  trees go no deeper than eight levels (a root at most 128 times its smallest node).
- **The memory a room's collision takes** (`KHDAYS_TRACE_HEAP`: the game's default heap, the one
  `NNSi_FndAllocFromDefaultExpHeap` allocates from, its handle where 0x0204c028 points): in the
  play walk's field (`wd_tt` room 0) it had 1,691,284 bytes free in 22 blocks, the largest
  1,636,588; with the room's 7,628-byte collision replaced by one of 2,284 bytes, exactly 5,344
  bytes more -- the collision comes from that heap. The game's largest room collision is 77,624
  bytes. That heap now lives in the port's region, beyond the DS's 4 MB
  (section 19); a room's collision is bounded only by its format: 32,767 faces of each kind.
- **A replaced room in the game** (the play walk's first room, `wd_tt` room 0, "under the
  tracks", replaced by a 12 x 12 test room of 7 grounds and 6 walls made by khdays-studio): the
  character walks on the new floor (y 0.00), climbs the ramp exactly on its slope (y 0.55 at
  x 2.10 on a ramp rising 0.5 per unit from x 1) to the platform (y 1.50), is stopped by every
  wall (0.88 to 0.93 from it), and falls through the hole to y -30.68 before the game puts it back
  at the room's start; the new model is drawn where its collision is, with HD graphics on and off.
  With gate06's original wall kept in the new room (khdays-studio's default when no new face
  takes the name), walking into it takes the exit to room 1 as in the game. With the ramp's
  faces naming `slide` (a record the room did not have, added after its own), the character
  cannot climb it: it slides back at its foot (x 0.7 to 1.0) where without the tag it reached
  the platform.

## 18. Missions as the game's script machine reads them

Read from the shipped data (`mi/mi/`) and the decompilation, every script of the game walked by
khdays-studio (`--check-missions`, `--mission-commands`).

- **A mission file** `mi/mi/<number>` (116 of them) is a P2 archive of script archives ("CAKP"),
  small counted tables and empty entries; the field opens it by the start record's number
  (`Ov002_ConstructGameplayScene`, the format `mi/mi/%04d`).
- **A script archive** is laid out as KAPH under the magic `CAKP`: section 0's member 0 names the
  scripts (u16 count, u16 offsets, the names), section 1 holds them (`Archive_FindNameIndex`,
  `Archive_GetMember`, `Script_Start`). The names seen: `_i`, `_e`, `cmp`, `cnt`, `dg`, `e0`, `e1`...
- **A script** is a word -- the size of its code, the word included -- its steps, then its data: the
  rest of its block (`Script_Start`: the steps run from block + 4, the data is at block + size). A
  step: byte 0 the command group, byte 1 the command, a u16 whose bits 0-10 are its length in words
  (its header included) and bits 11-15 a slot field, then its words (`Game_RunActionScript`, the one
  interpreter: `data_020425ec[group] + command * 8` is a {handler, continuation} pair). All 2,777
  scripts of the 1,109 archives end their steps exactly at their size; 70,224 steps of 161 commands;
  the slot field is 0 in every one. The data holds the script's texts, names and expressions.
- **Operands** are 8-byte slots {s16 mode, s16 sel, s32 value} (`ScriptVm_ResolveOperand`): mode 1
  an integer, 0x10 a 20.12 number, 4 a field of the game's state, 8 a variable of the running
  context, the 0x80 bit an entry of the context's table, 2 a place in the script's data
  (`ByteCode_ResolveOperand`: data + value), 0x40 a table there of a text per language (the word for
  `GetLanguage`'s 1-5 -- en fr de it es -- the text's offset, -1 none), 0 nothing. Every step of the
  game's scripts reads as such slots but three commands of group 0, which read words: 0.1
  `ScriptCmd_SetElemOffset` (go to block + 4 + word), 0.5 `CommitRecordCursor` (unless the
  expression at data + word 0 holds -- `Script_EvalExpr` -- go to block + 4 + word 1) and 0.10
  `Game_ActionSetBranchTarget` (a jump kept for later; -1 none). An expression is postfix code of u16:
  0 pushes the 8-byte slot after it, 1 negates, 2 is not, 3-6 + - * /, 7-12 == > < >= <= !=, 13 &&,
  14 ||, 0xf ends it. All 6,391 of the game's branches read so to one value, and every other step's
  words read as slots.
- **What a mission says and when its scripts run**: an area's `_i` shows the mission's name, a text per
  language in its data, with 2.20 (`Ov002_ScriptCmd_SnapshotPausedObject` -> `Ov002_ShowNoticeText`),
  and puts its other scripts on the field's event lists with 2.5 (`Ov002_ScriptQueueTextItem`: with
  operand 3 of mode 0, a slot running the script named by operand 4 -- `Ov002_NewTextItem` copies the
  name -- goes on event operand 1's list, `Ov002_RegisterEventSlot`; operand 0 = 0: the first list
  and the area's own script, `RunTextItemNoTag`; else the second list and the start's, `"_s"`). In
  0010's first area: event 0 runs `cmp`, 1 `e0`, 2 `cnt`.
- **An event's condition** is a task node filed under the same number in the same bank as its list
  (`Ov002_RegisterTaskNode`, `data_ov002_0207fa04` + 0xc, or + 0x12c for event | 0x1000): 2.9
  (`Ov002_ScriptSubmitTaskNode` -> `Ov002_CreatePairCompareNode`) compares fields of the game's state
  with a value (op 7 ==, 8 >, 9 <, 10 >=, 11 <=, 12 !=; mode 0 any field, else all) and, with a
  positive threshold, starts a lap before it counts as met (`Ov002_NodeEvaluate`); 2.8
  (`Ov002_ScriptSubmitPathTask` -> `Ov002_CreateTaskNodePath`) a node of a place, its kind 0-4
  `Ov002_FitsBudget`, `TaskNodePathInRange`, `IsWithinRangeHorizontal`, `PointInBox3D`, `PointInBox2D`;
  2.6 (`Ov002_ScriptAwardAndShow`) a counter watch (`Ov002_NodeWatchCounterPhase`), kinds 5 and 6 the
  HUD counter saved at field 0x20ad. In 0010: event 0 (`cmp`) when `state[8326:1] == 1`, threshold
  4000; event 1 (`e0`) a place node; event 2 (`cnt`) a counter watch (kind 0, amount 1). What field
  8326 holds and the threshold's unit are not read yet.
- **The time limit**: 2.16 (named `Ov002_StartFieldEffect`, it calls `Ov002_DispatchHudCounterCommand`)
  with command 0, value 0 makes the HUD (551 of its 574 steps); with command 7 it starts the mission
  clock now (value 0; root +0x8d00) and sets the limit in milliseconds (root +0x8d04), the panel then
  counting down `Ov002_GetRemainingTicks` (limit - (now - start)). The game's eight: 0161 180 s, 0301
  60 s, 0302 80 s, 0303 90 s (in scripts `t1`), 1352 120 s, 1354 60 s, 9005 30 s and (7, -1, -1)
  showing it again. 2.74 (`Ov002_EditSessionClock`) edits the clock's start or limit -- stamp, set,
  add, take away; the game's six add 5 s to the limit (0158, 9005's area scripts). The HUD countdown
  of `Ov002_StartHudTimer` (kind 1) is not started by any decompiled caller. Checked in the game
  (`KHDAYS_TRACE_MISSION`): mission 1354 served as 0010 by a mod set the limit to 60000 ms and made
  the four spawners its first area's steps name; the mission ended (scene 6) 66 s after the field
  began. Chests and emblems are not put out with mode flag 0x0204c240 bit 1 set (the Mission Mode
  play walk); the story walk's mission 10000 has no 2.76 step.
- **Where the players start**: 2.12 (`Ov002_ScriptMoveSlot`: player slot, kind, then a point and a
  heading in degrees when operand 2's mode is 0, else a marker of the room named in the data, through
  `Ov002_ResolveNamedPlacement`); `Ov002_WriteSessionMarker` files it for the player, a point spread
  by player (`Ov002_ScatterPlaceByIndex`) unless state flag 0x20e7. Mission 0010's first area puts
  slots 0-3 near (0, 0.049, 0) facing -90 degrees in room 0 of `tt`; the play walk's player was traced
  there at (0.00, 0.01, 0.00).
- **The groups' tables while the field runs** (`StoreGlobalArrayEntry`'s callers): 0 the main
  program's `data_02042640`; 2 and 4 ov002's (`data_ov002_0207f134`, `0207f484`); 3 ov012's five
  (the scenes played in a mission); n + 5 the table of panel n (`Ov002_SwitchPanelOverlay`, rows
  `data_ov002_0207f58c` + 8n: overlay id, then the table) -- 5 ov013, 6 ov014, 7 ov019, 8 ov016
  (field breakables), 9 ov020, 10 ov015 (field pickups), 13 ov017 (field deposits), 16 ov018, 17
  ov021 (prize boxes); 0x13 ov069's (`Ov002_BringUpMissionOverlays`). Group 18 (panel 13) has no
  table: its step, one in almost every archive (119), is skipped by the interpreter.
- **A mission file's entries** are named in its P2 header (eight bytes a name after the size and
  descriptor tables, as `MsgArchive_FindEntryByName` reads them): `_s.Z`, the start, whose `_i` the
  field runs when it is built (`Ov002_StreamFormattedLine("_s", "_i")`); script archives named by a
  number (`11.Z`, `21.Z`, `31.Z`, `99.Z`...), each an area of the mission with its own `_i`; the enemy
  tables `em0` and `em0m` (multiplayer).
- **The enemy table** (`Ov002_LoadObjectRecordsAndDrops`): u8 rows, u8 groups, a pad, the rows'
  offset, the groups' offsets. A row (16 bytes) is an enemy: +0 its class (`Ov107_SpawnEntityClass`),
  +4 a signed adjustment to the mission's level (below), +10 its drop key, +12 its drop chance; a group
  is a count, a state byte, a total, then parts of four bytes {row, limit, count, last player}. All
  227 tables of the game read so: the total is the parts' limits summed (2,444 of the 2,850 groups,
  where the counts are the limits too; 57 where only the limits sum to it) or -1 (349, every count
  0xff); the code does not read it. `eid.z` holds the drops by class.
- **An enemy's id** (a row's +0): `Ov107_SpawnEntityClass(id)` loads that enemy's own overlay --
  row `id` of `data_ov107_020cb6a4` (six bytes per id, 0xff none: 118 ids, as `eid.z`'s 118 rows)
  holds in the first column the enemy slots reach `b`, and `Ov107_LoadEnemyOverlay` starts overlay
  113 + b + 1, which the decompilation names after its enemy (`ov114_enemy_shadow`,
  `ov244_enemy_darkside`...). Read against the missions: id 0 (Shadow) in the tutorial 0010, id 70
  (Darkside) only in 0105 (Twilight Town; its notice "Eliminate the Darkside") and in 2000/2001,
  which load every enemy. `Ov002_CreateActorFromMarker` gives the Darkside (0x46) Shadows (id 0) as
  its following kind. The `mi/ch/<hex>.z` files are NOT numbered by this id (`mi/ch/30.z` holds the
  Darkside's model `DS01`; id 0x30 is Lock).
- **A mission changed by khdays-studio**: the mission file rebuilt into the mod as
  `files/mi/mi/<number>` (operands changed in place, a longer text written past the script's data and
  its table entry pointed at it), `trbox` / `evi` likewise. Checked in the game (play walk, mission
  0010 from a mod): the player's start moved to X 2.5 (`KHDAYS_TRACE_PLAYER`: (2.50, 0.01, 0.00), was
  (0.00, 0.01, 0.00)) and the mission's panel showed the Spanish notice typed in the studio. A
  mission's content copied into another's slot (0105 into 0010) plays there: the play walk then
  started at 0105's start (0.00, 0.03, -2.30) and showed 0105's opening scene. The Mission Mode
  list and the saved progress are kept by mission index in fixed save areas (`Ov025_DrawMissionRow`:
  status 0x2ca4 + 3 x id, stage 0x2a4c + 3 x id, rewards 0x92b + 4 x id): a mission with a slot of
  its own needs those kept elsewhere by the port (still open).
- **A spawner** (`Ov002_ScriptCreateSlotObject`, group 2 command 32; 16 operands): a group of the
  table, a source, a mask, a mode, a gate, a point (20.12), then a radius-like number, a tag, two
  numbers, a second point and a last number. `Ov002_CreateSlotObjectAndStart` makes the slot's object
  -- its point at +0x104, its tag at +0x4a -- and adds each part's enemies, `limit == 0xff ? count :
  min(limit, count)` of them. The table of slots is made first by `InitObjectTables` (a step of the
  same script); an area without that step has no spawners. Checked in the game with
  `KHDAYS_TRACE_MISSION` (port/runtime/core/runtime.c: the slots of `*data_ov002_0207fa14`): the play
  walk's mission is 0010 (its first area, rooms 0 and 1 of `tt`, has no `InitObjectTables` and no slot
  table appears); with mission 0101 served as 0010 by a mod, its first area made one spawner, group 0,
  tag 5, at (-2.622, 1.249, -24.931) -- the step at 0x0e4 of that area's `_i`.
- **`mdb.z`** (a count, then 111 records of 0x3c bytes; `Ov002_SelectTableEntry`): a record's first
  short is the **mission's number**, the one `mi/mi/%04d` is formatted with (the last record whose key
  matches wins); +2 goes to the slot context's +0x11 (0..5); +4 to the field record's +0x10 (0x1000 in
  every one); +8 in single player, +0xc in multiplayer (equal in every record), a byte, to the field's
  +0x8bae -- the base that `Ov002_GetEffectiveHeight` adds an enemy row's +4 to, capped at 0x78 and
  at least 1 (1 in 0101, 2 in 0102 ... 61 in 0114: it rises through the missions); +0x10 a 20.12
  number and +0x14 five pairs {s32, 20.12} (all (-1, 1.0)) go to the slot context's +0x98 and +0x9c,
  their readers not found. 18 files have no record (0152, 0153, 0161, 2000, 2001, 3000, 8000-8010,
  9025) and 15 records no file (0155, 0353, 0955, 1201-1204, 1251, 1253, 1353, 9020-9024).
- **Chests and emblems** are not in the scripts but in `trbox` (the code spells `mi/mi/trBox`) and
  `evi`: a count, its offsets, then a set per mission -- u16 the mission's number (the key
  `Ov002_FindHandlerByKey` matches with `Ov002_GetStateWord`), s8 its count, a byte, rows of 0x14
  bytes {s8, s8, s16, s16, s16, a point}. A chest (`Ov002_ScriptSpawnTimedSet`, called by 2.76
  `Ov002_ScriptCmd_ThreeMixedOperands`: module slot, model, a number) is {slot -- its state field
  0x20c0 + 2 x slot --, bucket, contents' key, flag -- GameState flag + 9 set: opened, left out --,
  heading in degrees, point}; an emblem (`Ov002_SpawnStateSpots`, called by 2.83) is {bit in the
  save's field 0x92b + 4 x the mission's index -- set: found, left out --, kind, -, a value kept per
  emblem, -, point}. The game's: 89 missions with 396 chests, 76 with 150 emblems. Group 17's own
  prize boxes (17.0 a class with a shuffled list of prizes -- 0 item, 1 munny, 2 keyed object, 3
  nothing --, 17.1 a box) appear only in 1053, 1351 and 2001, its emblems (17.3) in five missions.
- **Beside them**: `eid.z` (118 of 24: the drops by enemy class).
- **How a Mission Mode mission flows** (read in 0004, 0010, 0160 and 1354, traced with `KHDAYS_TRACE_MISSION`,
  which logs the state fields below as they change, 2026-10-24): the start's `_i` sets the mission up -- its chests
  (2.76), emblems (2.83), enemy table (2.31), objective texts (2.52), the dark corridor (4.18, 4.19 at a point) --
  and posts area 11 (2.3, `Ov002_ScriptPostLinkMessage`); an area's `_i` loads its world and room, makes its
  spawners, the players' starts, the HUD, the notice, and files its events. Area 99's `_e` is where the others go
  between areas and at the end; state 8862 (or 8853) tells it which area comes next, 8320 which phase an area is in.
  **Waves**: a spawner (2.32) puts its group out when its step runs (`Ov002_CreateSlotObjectAndStart`, every part
  at once, unless the group is already settled), so a later wave is a script with spawners run on an event: 0160's
  `e0` makes two groups and queues itself again on event 0 with 2.7. **2.7** (`Ov002_SubmitRequestBlockKind1`:
  gate, event, check, -, slot, threshold) with check 1 fires when every spawner row's group has settled -- all its
  enemies defeated (`Ov002_NodeRunCheckPhase` -> `Ov002_QueryEntryValue(-1, filter)`); 0004 (defeat the
  Mega-Shadow) files it twice, at once (event 3, its `e1`: state 8326 = 1, the objective done, 8839 = 0, the
  corridor) and after 4000 ms (event 4, `e2`: to area 99, then back to area 11 in its second phase, the corridor
  open). **The end**: in 1354 (survive one minute) the clock's end is a counter watch (2.6 kind 4) whose `c0` raises
  state 8844, an event on it (2.9, 4000 ms) runs `c1`, which posts area 99, and its `_e` ends the mission (0.12): the
  trace saw state 8326 go 0 -> 1 at the clock's end and the results 4.7 s later. The field root's `+0x8ba8`, the
  key of `mdb.z`, `trbox` and `evi`, is the mission's file number (`Ov002_ConstructGameplayScene` copies it from
  `data_0204c240` + 2 and formats `mi/mi/%04d` with it).
- **Chests and emblems in Mission Mode**: neither is put out with mode flag bit 1 (`Ov002_ScriptSpawnTimedSet`
  returns at once, `Ov002_SpawnStateSpots` skips them): Mission Mode's own missions show none. An emblem found sets
  bit `kind` of the mission's 4 bits at `0x92b + 4n` (the same bits Mission Mode's list asks for its rows).
- **Spawners** (2.32, `Ov002_CreateSlotObjectAndStart`; read 2026-10-28 and traced with `KHDAYS_TRACE_MISSION`,
  which logs each spawner's enemies out and the region's limits): a spawner is a node of a region (the source 2.14
  made), started by its trigger sphere (the last operand, a radius: 1354's 100) when a player is inside it, then
  every 0x1000 ticks it binds a free actor of its region (made by 2.49) to each empty place of its group's parts. A
  region ticks only the nodes it set running, which it does when a player joins it: in a game area that is after
  the spawners are made (the 2.12 starts come after 2.32), so they run. A spawner made later in the area -- a wave
  after the first -- stays still (seen: wave 2's spawner made, 0 of its 2 enemies out for 100 s, its node's state
  4 against the first's 7). A row's +1 low nibble is the most of its class out at once (0, no limit, in every game
  row).

## 19. The game's heaps

Read from the decompilation and measured in the port.

- **Where the game's memory comes from**: two NitroSystem expanded heaps `Ov001_CreateMainAndSubHeaps`
  (ov001 0x0204ce40, from `Ov001_BootInit` through `func_ov001_0204cf5c`) makes at boot from the
  main arena -- the **main heap**, 0xc8000 bytes from the arena's low end (handle at
  `data_0204c044`, `data_0204c024` points at it), and the **sub heap**, the whole rest up to
  `HW_MAIN_MEM_MAIN_END` 0x023e0000 (handle at `data_0204c030`; `data_0204c02c` and `gCurrentHeap`,
  0x0204c028, point at it: the default heap). Nothing else takes from `OS_ARENA_MAIN` and nothing
  else makes a heap; `HeapState_Recreate` remakes one of the two over its own region at a scene's
  change, `Heap_SetCurrent(0)` makes the sub heap current again.
- **The DS's figures** (the port with the DS's heaps, the maintainer's HD room nm 9 through its boss
  fight, both heaps every 60 frames): the main heap `0x020d7418`..`0x0219f3e0`, never more than
  32,824 bytes free (24,456 at the least); the sub heap `0x0219f418`..`0x023e0000` (2.25 MB),
  2,134,660 free at the start, 793,408 at the least, current in every sample. The main arena so
  runs from 0x020d7418 (after the code and the overlays' region) to 0x023e0000.
- **The port's**: main RAM stays the DS's 4 MB with its mirrors -- the code, the
  overlays, the shared area at 0x027ff000, the DTCM over the mirror -- and the two heaps are made
  over a region of the game's memory above the DS's ranges: the main heap `0x20000000`..`0x30000000`,
  the sub heap `0x30000000`..`0x80000000`. The region is reserved without memory; the first access
  to a granule (64 KB) commits it (`runtime/core/memory.c`), so the PC gives the game only what it
  touches; at the port's reset the region is given back with main RAM's clearing. Every game
  address stays below 0x80000000 and every heap under 2 GiB: the game keeps heap handles and free
  sizes in `int` (`Heap_SetCurrent`, `AllocFromExpHeapWrapper`, `FontResource_Destroy`).
- **File handles name archive headers by 24 bits**: a packed archive's member is named
  `0x80000000 | ((header + 0x8000) & 0xfffffc) << 7 | index` -- made in some 200 places (the engine,
  the message databases, nearly every enemy overlay, the movie player) -- and read back by
  `Archive_OpenSubfileByHandle` (0x020250bc) and `Archive_SubfileIsCompressed` (0x02025074) as
  `((handle >> 7) & 0xfffffc) + 0x01ff8000`, so a header must lie in `0x01ff8000`..`0x02ff8000`.
  The headers are the ones `Msg_OpenContainerAndReadHeader` (0x02024ee8, from 63 files) builds: 16
  bytes (`+8` the `FSArchive`, `+0xc` the image's top) and the member table. The first build with
  every allocation in the region stopped at the title's first sound (`FSi_SendCommand` through a
  garbage `FSArchive`). So the headers alone are kept in **headers' heaps** where the DS's two upper
  main RAM mirrors were (`0x02800000`..`0x02c00000` the sub heap's, `0x02c00000`..`0x02ff8000` the
  main heap's; nothing reads main RAM through those mirrors), allocated from the one of the game's
  current heap, freed back to it whatever heap the game names, remade with its game heap.
- **A header's place and the resource cache** (2026-10-14): `ResCache_FindSlot` (128 entries of
  0x30 bytes at `gFileLoader`'s +8: a reference count, and at +0x10 a name or a file handle) finds a
  resource already loaded by its handle, and keeps it while it is used -- after its header is freed:
  `Ov022_BuildActorSlot` frees a character's weapons' header (`ba/ch/<code>/w_.p2`) as soon as their
  entry is made. In the headers' heaps, which hold nothing else, the next header took the freed
  one's place, so its handles were the old ones and the cache gave the old entries: in a session
  with Mickey as the host, the guest's Axel held Mickey's two keyblades on both copies, and the
  guest stopped leaving the game-over screen (`Ov027_ExitScene`, its heaps' walk through a block
  freed into the wrong heap); with `KHDAYS_DS_MEMORY=1` Axel had his chakrams. On the DS other
  blocks fill such a place first. The port keeps a header's place that a cache entry in use still
  names aside until none does (`game_heap.c`, the session log says the first time); seen with the
  same session: Axel's chakrams, both copies to the Mission Review.
- **The joins** (`port/tools/prepare.py`): `Ov001_CreateMainAndSubHeaps`'s two creations go to
  `khdays_game_heap` (`runtime/core/game_heap.c`); `MIi_CheckDma0SourceAddress` (main 0x02004084), whose
  panic for a channel-0 source at or above 0x08000000 is the DS's cartridge bus, lets a source in
  the game's memory pass; `NNS_FndAllocFromExpHeapEx` (main 0x020107fc) tells the port when a heap
  gives no block (`memory: the <heap> could not give ...`); `Msg_OpenContainerAndReadHeader`,
  `NNS_FndFreeToExpHeap` and `HeapState_Recreate` keep the headers in their heaps, and the two
  decoders log a handle whose header lies anywhere else (none seen).
- **What the port itself assumed of 4 MB**, changed with it: its checks of a game pointer
  (`khdays_game_ram`: main RAM or the heaps' region) in the camp, the character select, the cinema
  and room and save lists, the new game, Retry, the badges, the players' names, the situation the
  one-screen layout reads and the sound mods; the sound's sample reads (main RAM folded as before,
  the region as it is) and a channel's source address, which kept the DS register's 27 bits -- in the
  channel model and in the native driver's `SETUP_CHANNEL_PCM` (`SND_SetupChannelPcm` puts the bare
  address there): the streamed music and the effects the game sets up itself were silent until both; the
  texture origins' keys (the region keyed by its own address, above every VRAM key) and the lookup
  of where a model's data came from, which took only main RAM's keys -- a mod's room model was not
  drawn until it took the heaps' too; the I/O guard, which judged a fault in the window by the host
  address's low bits (they can look like a register's in the region).
- **Measured** (the maintainer's HD room nm 9 through the boss fight, Release): the main heap at
  most 794,688 bytes used (267 MB free; on the DS 24,456 at the least), the sub heap 1,562,792 (1.34
  GB free; on the DS 793,408); 2,555,904 bytes committed. The same mission with nm room 9's
  collision split to 8,081,760 bytes: the sub heap 9,022,564 bytes used, 10,289,152
  committed, the character on the floor (y -23.13) through the fight.
- **The testing knob**: `KHDAYS_DS_MEMORY=1` makes the heaps exactly as the game does on the DS
  (the session log says "as on the DS"); `KHDAYS_TRACE_HEAP=1` writes both heaps (range, size,
  used, free, blocks, largest; the current one marked) and the committed bytes every 60 frames.
- **A room's collision has a format limit of its own**: a node of the collision tree names its
  first face in 16 bits, so a room holds at most 32,767 faces of each kind (khdays-studio refuses
  more); the largest such room made from nm room 9 is 8,081,760 bytes.

## 20. What gameplay mods hear and change

The mods' interface (`runtime/scripts`, `docs/SCRIPTING.md`) joins the game at these points, each a
documented build-time edit (`port/tools/prepare.py` `HOOKS`); read in the running game 2026-10-16
with `KHDAYS_TRACE_MODS=1`, the example mods (`port/mods_examples`) and the walks.

- **The frame point**: `main`'s frame loop (0x02000cac), right after `SoundMgr_Update`: the game's
  logic of the frame is done, before its pacing. Every scene the walks pass (1 title, 7 character
  select, 19 Mission Mode's camp, 5 the story's camp, 2 the field, 6 the results) reaches it once
  per logic frame: 974 frame points over 1,948 VBlanks in the title, 1,214 over 2,432 in the field.
  The pause reaches it too (`gPauseMode` 1: the objects stand still).
- **The field begins**: `Ov002_ConstructGameplayScene`, again after Retry. `Ov002_BeginMissionRun`
  starts a run of it more than once a mission (its gameplay state 2 entered again: mission 94 started
  two runs 32 frames apart), so the mission's start is the first run after the field is made. In
  Story Mode the Grey Area is a field too: walking into it raised a start with `data_0204c23c` 0.
- **The results**: `Ov005_CommitMissionResults` runs after a give-up as well (mission 94 given up
  from the pause: rank -1, `rewardMode` not 255), with flag 0x2087 (`Ov002_RetireSubFlow`) set from
  the game over on. A cleared or lost mission is not traced yet.
- **The game's state** made anew or loaded from a save happens in the title (scene 1): loading the
  walks' save changed 20 item counts and the munny (300) there, before the character select.
- **The local player's battle actor** is `GetEntryField20ByIndex(Session_GetLocalPlayerIndex())`:
  the field players' table at `*(data_ov022_020b2e78 + 4)`, read with the battle (ov022) loaded; HP
  at +0x12, maximum +0x16, position +0x48c. In mission 94 (Mission Mode's tutorial) the D-pad moved
  it from (0, 0.01, 0) to (5.11, 1.83, -34.99); a mod's `player.set_hp` through `Ov022_ActorSetHp`
  took Roxas from 62 to 55 of 62 and raised the `hp` event for that actor. Every HP change of a
  battle actor goes through `Ov022_ActorSetHp` (`Ov022_ReceiveHit`, `Ov022_ApplyDamageAndFlagHit`,
  items, the party's network state).

## 21. Event scenes and their skip

Read in the decompilation and the shipped data, then traced with `KHDAYS_TRACE_SCENES` (2026-10-17,
the story walk and `KHDAYS_TEST_DAY`).

- **The scenes are the event archives' entries.** `ev/EV_S.p2` (206 entries, the story's shared
  scenes) and the worlds' `EV_TT`, `EV_NM`, `EV_BB`, `EV_AL`, `EV_AW`, `EV_HE`, `EV_PP` and
  `EV_DP` hold scripts of the game's one script machine (section 18): a word, the offset of the
  script's data, the steps from 4, then the data -- its UTF-8 texts and, for each text a step
  names (operand mode 0x40), a record of seven words: the Japanese, English, French, German,
  Italian and Spanish texts' offsets in the data, -1 for one the ROM lacks (the scenes have no
  Japanese), then -1. `Ov023_LoadScripts` opens `ev/EV_S.p2` and the world's archive when the field's
  event begins; the field's script (mission 10000's day script, a mission's area) calls the scene's
  script one level down (depth 1 of the event's machine).
- **A scene's skip.** Step 0.10 (`Game_ActionSetBranchTarget`) keeps a jump for later: the target
  (`state + 0x584`, the entry's script start + the operand) and its depth (`+0x580`); -1 forgets it;
  either way it clears the skip request (`+0x588`). 675 of the archives' entries set one target,
  +0x1c, in their first steps (the step at 0x50 of the entry) and clear it at their end; 74 set
  none. Skip in an event's pause (`PauseMenu_Frame`, entry 1 when the mode word lacks bit 1) stops
  the sound effects (`SoundMgr_StopAllSePlayers`) and sets the request through
  `Ov023_FlushTextBox` (`SetWordAt0x588To1` on the event's state at root + 0x4b88); the machine's
  next step (`Game_UnwindActionStack`) lets the stretch's waits and callbacks go, pops to the
  target's depth and goes on at the target, and `PauseMenu_OpenIfAllowed` runs the event until the
  main screen's master brightness reaches -16. What follows the target is the scene's end: it runs
  whether the player watched or skipped. Traced: a skip the port asked 60 frames into day 8's first
  scene (EV_S#3) jumped to +0x1c on the next step, the scene cleared its target at +0x16ec and the
  event closed (status 0xa, then 0) 17 frames later.
- **Which scene runs.** The running script, read from memory when its target is set (the depth's
  entry +0xc is the start of its steps, 4 bytes after the entry's first word), is its ROM entry byte
  for byte: day 7's morning runs EV_S#196 (the castle at night), then EV_S#0 (Roxas in his room);
  day 8's starts with EV_S#3 (Saïx sends Roxas on missions). The movies' subtitle schedules set
  targets too, from their `_m` sections (the prologue's movie 818: mission 10000's script 400,
  section `_m`; group 3 is the movie player's, ov024 / ov012).
- **The day's script.** `Ov002_ConstructGameplayScene` copies its record (its argument, else
  `data_0204c240`: the mission at +2, the script at +4) to the field's root +0x8ba8 / +0x8baa and
  opens `mi/mi/%04d`; `Ov023_LoadScripts` names the script `Ov002_FormatResultLine(0)` ("%d" of
  +0x8baa). In the story both fields of the walk were built from mission 10000, script 0, and the
  script number became the story's day in the same frame (400 the prologue, 7 on day 7): the day is
  the game state's field 0 (9 bits, the save's day). Setting it to 8 at the first field's construction
  (`KHDAYS_TEST_DAY=8`) made that field run 10000's script 8 and its first scene.
- **Where a scene is played.** The field's scripts load a scene with step 0.2
  (`Game_ActionLoadArenaResource`) naming it by a text of their data, `<slot>:<entry>`: slot 4 the
  shared archive EV_S, slot 5 the mission's world's (`EV_TT`...), slot 3 a path built from the event
  code (`Ov023_BuildEventTextPath`), 0 a mission script's own section (`0:sys`, `0:lbcmn`). Day 7's
  `_e` loads `4:196`, `4:0` and `4:2`, day 8's `4:3` and `4:4`; over the missions' scripts 285 such
  loads name slot 4 and 487 slot 5, 302 different scenes (EV_S#6, the clock tower on day 8, from the
  end of mission 0001, `mi/0001/99:_e`). Written over in memory (`KHDAYS_TEST_SCENE=4:6` on day 8's
  `4:3`), the event played EV_S#6 whole in its own place, the clock tower of Twilight Town: a scene
  loads its stage and characters itself (its data names them, `/mi/wd/wd_tt`, `mi/ch/07.z`).
- **The day's monologue.** Scene 10 (ov007) is Roxas's words over black before the calendar, no
  script: `Ov007_SceneInit` picks a record of `ui/mnl/mnl_&.s` (a count at +4, then eight texts,
  each a length word and UTF-16) from the story's day, the game state's field 0: days 7 to 13 give
  records 1 to 7, any other day record 0 (the prologue's, after the clock tower: day 255). A
  (`gPadPressed & 1`) shows the next lines; START (`& 8`) goes to the fade-out from any of its
  steps; `Ov007_RequestSceneChange` then asks for the calendar. The theater's voice-only clips tell
  the same texts in their own words: hd000 record 0, hd003 record 1, hd007a to hd007e records 3 to
  7; record 2 (day 8, his first day of work) has none. Their pictures are black and their sound is
  the voice alone (digital silence between the lines, no music), so the port plays only their sound
  over the game's page (`mnl#<record>` in the scene map), from `Ov007_SceneInit` to
  `Ov007_RequestSceneChange`, at the voices' volume.
- **How the map was made, and checked.** `tools/hd/scene_match.py` pairs scenes by their lines (the
  theater's subtitles against the scripts' texts, in story order); the theater retranslated its
  lines, so short scenes were paired by meaning, read side by side. `tools/hd/scene_census.py` plays
  every shared scene in place (`KHDAYS_TEST_SCENE`) and keeps its pictures; `tools/hd/scene_compare.py`
  sets them over the video's at the same fractions, one sheet per entry, and every pair was looked
  at (scenes without lines were paired by them alone). `tools/hd/scene_report.py` says where each of
  the collection's 142 videos plays (a scene, a monologue, a movie, or not played and why) and fails
  on an entry whose video the collection or whose script the game lacks.
- **START over an HD scene.** The video and its sound pause and the game is let go with START,
  pressed every other frame until its pause opens: `PauseMenu_PollInput` refuses one while the
  screen's brightness moves, and a scene held at its start is in its fade-in (seen: the first START
  refused, the pause open 35 frames later). The pause's panel is engine A's BG3 over the scene's 3D,
  which the DS darkens (BLDCNT 0xc1, BLDY 8); on one screen the port draws the situation's shapes
  with the video's picture behind them and no 3D, on two the game's screens as they are. Skip
  (`Ov023_FlushTextBox`, +0x588) unwinds the event and clears its target, which ends the course;
  Continue (`PauseMenu_GetMode` back to 0, the request clear) holds the game again and the video
  goes on. Seen on day 7's castle (hd002), both ways.
- **No cut between the game and an HD scene.** Cut at once, the game's picture gave way to the video
  mid-move and its sound stopped mid-note (the maintainer, 2026-10-21: Xion's third phase won, the
  video of Roxas back in Twilight Town came in at once). The game's sound is made as its clock runs:
  held, it makes none, so it cannot go down while held. The port lets the game go on for half a
  second as its target is set, its picture under a black growing to full and its sound going down
  with it (`khdays_movie_hd_game_level`, the sound thread setting the stream's gain), without the
  player's buttons; then holds it and starts the video. The scene's first video comes up from black
  and silence over its first half second, its last goes down to them over its last
  (`khdays_hd_fades`: its pictures' level, its sound scaled sample by sample, from the file's time),
  and once the scene ends the game comes up from black and silence over half a second. A Theater
  chapter starts at once from its list. A movie's videos fade the same way, its first in and its
  last out, but the game goes on through its own movie, unheld: the DS's movies begin and end in
  black, and a part cut out of a longer video (hd833's first, to 128.4 s, cut halfway through its
  fade to black with its music still playing) goes down to black and silence instead of stopping
  mid-picture (the maintainer, 2026-10-22). hd833's music cannot be left out of its first part:
  it is in the video's one sound track, mixed with the voices (from about 52 s, a wide stereo bed
  under the centred lines, the side channel up from -48 to -30 dB).
- **The pause over a movie.** The DS has none: START skips a movie where its player allows it
  (`Ov024_MobiClip_UpdatePlayback`'s flag bit 3), and the game's pause belongs to the event scenes
  (`PauseMenu_PollInput`, `gPauseAllowed`). A player asked for one over the HD movies, as over the HD
  scenes; the maintainer chose the DS's own look (2026-10-21), so the port draws the scene pause from
  the game's pieces (`host/screens/movie_pause.cpp`). `Pause_LoadResources` loads
  `pause/pausebg_<lang>.pbg` (a D2KP pack: its screen, 256 4-bit tiles, the palette) and the screens
  `pauseicon.NSCR`, `pausemsg.NSCR` and `etcicon.NSCR` (33 x 12 cells, kept in 32 x 32 blocks as
  `func_020130b0` reads them); `SubObject_SetupDrawsForMode`, for a scene (no mission, field 0x2483
  clear), copies into the pack's screen the title (`pauseicon` 12 x 2 to cells 10, 8), Continue
  (`etcicon` 11 x 2 from 0, 0 to 10, 11) and Skip (from 11, 0 to 10, 13) -- `SubObject_SetDrawParams`
  (srcX, srcY, dstX, dstY, w, h, srcY doubled); an entry lit takes the cells 2 rows down and its blink
  the cells 4 rows down (`SubObject_NudgeAndRedraw`), flipped every 16 frames (`Input_DebounceSlot`);
  up and down wrap (`TabPanel_HandleUpDown`); `PauseMenu_Open` puts the screen on BG3 and darkens the
  rest by 8. The tiles hold PAUSE, Continue, Skip, Give Up, Yes, No and Are you sure? in each
  language. The port pauses the video and holds the game (its movie waits with it), darkens the
  video by half and draws the panel as the DS's screen would show it, 4:3 in the middle; A takes the
  entry lit, START (or B) continues; Skip does what START did. Seen over hd802 (the opening) in a
  window: PAUSA, Continuar lit with the glove, Omitir. Silent, as the scenes' pause is under their
  video (the game's sound muted).
- **The theater's music over its videos.** Most of the theater's videos carry the voices and effects
  alone (hd848, Xion's end: near silence up to 2.9 s between lines, the DS movie 848's sound the same
  to a tenth of a decibel); the theater plays music over them from each event file's
  `SCHEDULE_CATEGORY_BGM`: `BGM1` cues (`FrameStart`, `FrameEnd`, `SoundName` bgm_NNN, 30 frames a
  second), `VOLUME1` ramps (to `Param` percent between two frames, from the level before; all of
  curve 0), and `BgmStartSet` / `BgmEndSet` (FIELD: the music comes from the video before or goes
  on into the next; STOP). 129 of the 214 event files have cues, the same in every language; one
  track each at a time; hd848 plays bgm_222 from frame 390 to 5080 at 50 %, faded out over its last
  80 frames. The tracks are `sound/bgm_NNN.win32.scd` (SEDBSSCF, version 3: the tables at the
  header's size, u16 at 0x0e; the sounds' offsets at the tables' +0x0c; a sound's 0x20-byte header:
  its stream's size, channels, rate, codec 6 = Ogg Vorbis, its loop's start and end in bytes into
  the stream, always a page's start, and the size of what precedes the stream: a 0x20-byte Ogg
  header whose third byte the Vorbis header pages are XORed with, the seek table's size and the
  Vorbis header's at +0x10, the seek table, the pages). A loop's end of 0 means the track plays
  once; 21 of the tracks the port's videos use, 48,000 Hz stereo, some shorter than their cues
  (hd063's 204.8 s on a track of 52.7 s), so the loop matters. The DS plays music under those
  scenes from its own sequences, which the port silences while a video plays; the theater's
  schedule is followed instead (`tools/hd/theater_movies.py --music`, `host/movies/theater_music.c`).
  Seen: hd004 (day 8) played bgm_213 50.4 s from its first frame, the video 1,513 frames; hd818 (the
  prologue's movie) bgm_219 from frame 1038 for 64.6 s, both as their schedules say.

## 22. The field's enemies

Read in the decompilation, then traced with `KHDAYS_TRACE_ENEMIES=1` (2026-10-22: the play walk with
`KHDAYS_TEST_EVENT=2/0`, which opens mission 2 in place of mission 10, and the test mod
`port/tests/mods_v2/waves`).

- **An area makes its enemies once, from the mission's enemy table.** The table (`em0`, `em0m` in
  multiplayer) is loaded by the start script (`Ov002_ScriptCmd_LoadObjectRecords`) into the field's
  objects (`data_ov002_0207fa14` + 4: its rows of 16 bytes, its groups). An area that has enemies
  then, in its own start script: makes its tables (`Ov002_ScriptCmd_InitObjectTables(spawners,
  places)`), gives some rows a place (`Ov002_ScriptCmd_UpdateSlotLookup(row)`, `-1` to close: the
  row's class registered with ov107's actor manager), makes a source per room
  (`Ov002_ScriptCmd_RecreateObjectSlot(i)`: an ov107 region, objects + 0x1c, whose members are shown
  and hidden with their room), makes every enemy it will use, idle, in a source
  (`Ov002_ScriptCmd_CreateActorFromMarker(source, row)`), puts its spawners
  (`Ov002_ScriptCreateSlotObject`: a group of the table at a point, with a reach of 14-16 units) and
  last files every actor by its id (`Ov002_ScriptCmd_BuildSceneIdTable`). Mission 10's first area
  (11) does none of it -- ov107 is not even loaded there (the earlier "no source whenever it was
  tried"); its areas 21 and 31 do. Mission 2's first area made 3 Shadows (row 0, class 0) in source 0
  at the area's build; its spawner ran op 0 every 32 frames.
- **The level is the row's.** `Ov002_GetEffectiveHeight(row)` -- the mission's level (root + 0x8bae)
  plus the row's adjustment, 1 to 120 -- goes to the actor's +0x1ec, `Ov107_AiState_LoadStats`,
  which reads the class's stats at that level (`+0x19d`): its HP (`+0x218` maximum, `+0x21a` now),
  its eight attacks' power (`+0x290 + 6 * i`, through `+0x1d8`, `Ov107_StoreSlotEvent`). A Shadow
  of mission 2: level 1, HP 75, attacks 3 and 9; at level 6, HP 93, attacks 6 and 18. A Yellow
  Opera at level 1: HP 75, attacks 7 and 8.
- **A hit** (`Ov107_AiState_ApplyHit`) takes `+0x21a` down (75, 67, 59, 37, 24 ... 2 seen); at 0
  the enemy is defeated: `Ov107_AiState_OnDefeat` (the class's `+0x1d4`; `+0x25c` the last
  attacker) hands the players amounts from the class's table (`+0x1a0`) and `+0x19f` (which the row's
  boost raises) through ov022, then calls `+0x1e8`, which a spawner sets to
  `Ov107_Spawner_OnActorRetired` (its slot back, the group's tally and drops,
  `Ov002_OnPartActorRetired`). By then the HP reads full again. Enemies' HP never goes through
  `Ov022_ActorSetHp` (the players' alone).
- **Set going** (`Ov107_Spawner_Run` op 0): the HP refilled, the actor's home and reach (`+0x1f4`,
  `Ov107_StoreHitInfo`), moved (`Ov107_MoveNodeAndRelayout`) and its event 0 (`+0x1cc`): its +0x60
  word gains 0x300; 0x100 is cleared at the defeat and once sent away. Event 1 sends it away (the
  spawner's op 1 when the player leaves it): the Shadow left in 67 frames.
- **A Shadow starts flat** (ov114): made, its height scale (`+0x390`, 0x1000 whole) is 1, a dark
  shape on the ground (`Ov114_InitNodeAndRegisterHandlers`), and it waits for its +0x60 bit 0
  (`Ov114_SetVisFlagsEnterState`); then its stored action, its entry (`+0x1c9`, the spawner's slot's
  `+3`; 1 as it is made, `func_ov107_020c6624`), is queued and any action but 0 gives it its whole
  height (`Ov114_DispatchSubStateByte`). Entry 1 makes it flat again and lets it rise in about a
  second (`Ov114_AiTargetAndFace`, `Ov114_AiFadeInTick`), 2 sidestep at its height
  (`Ov114_StartSidestep`), 6 a dark shape that bounces out (7, `Ov114_BounceEntry`). While flat or sunk
  its body is away: `+0x1ae` bit 0 set and its hit shape (`+0x388`) off. Mission 94's spawner gives its
  three Shadows entry 2 (traced: `entries 2 2 2`): they come standing. Seen with a mod's three Shadows
  when a mod's enemy kept the made actor's entry, 1: flat as they were made, half risen half a second
  later, up in a second; held by Stop while rising, a Shadow stayed flat for the hold (its tasks stand
  still).
- **A source shows its enemies** (`Ov107_Region_SetVisible`): its `+0x40` bit 0, and each enemy linked
  to it (its list at `+0x44`) told through its own `+0x18` (`Ov107_BroadcastValueToChildren`,
  `Ov107_AiState_SetVisible`: its model drawn, its `+0x40` bit 0). An enemy made after the source was
  shown is never told: it stands, strikes and is targeted, undrawn. Seen with a mod's three Shadows
  (the maintainer saw them as invisible, the lock-on ring on the floor): none drawn by either
  rasterizer while the mission's own were, their state whole (height 0x1000, hit shape on).
- **An area's end** releases them all (`Ov002_ReleaseResources`: the actor manager's node destroyed,
  ov107 unloaded, the tables freed). Mission 2 moves to its next area once its enemies there are
  defeated: Shadows (row 0) in the first, Yellow Operas (row 1) in the next, then another.

**A mod's enemy** (`runtime/scripts/enemies_link.c`) is made the way the area makes its own --
`Ov002_CreateActorFromMarker` with a row of the kind the area placed and the source of the room shown
-- then set going as op 0 does, without a spawner: its spawner word holds its source so that no
mission spawner takes it while it stands, and it gives none of the mission's tallies or drops. It
enters as the area's spawners let its kind enter (their slots' entry, learnt as they run), the made
actor's own when none of them has its kind, and is shown as its source is (above). Seen with the Stop
walk's three Shadows: drawn, coming standing and striking at once as the mission's own. The
id table is made again after each new actor: one made after it has an id past the table, and the
first message to it read beyond (seen: a fault in `Ov107_DispatchMessage`). An idle actor a mod made
before is reused for the next of its kind (its level's stats loaded again first): the test mod's
second wave of 3 reused the first wave's 2 defeated Shadows and the one sent away. Seen with the test
mod: HP times 2 gave 150; level + 5 and strength times 2 gave level 6, HP 93, attacks 12 and 36, and
the player took 18-19 from those Shadows against 8-9 from the mission's own (Yellow Operas: 14-16
against 6-8).

**Any kind** (seen 2026-10-22, the test mods `waves` and `arena-edge` in mission 2's first area). A
kind's code is an overlay of its own, loaded as its first enemy is made (`Ov107_SpawnEntityClass`), not
as the area starts: the enemies' overlays share six places of 0x1e00 bytes, each kind has builds of its
overlay linked for some of the places (`data_ov107_020cb6a4`, six bytes a kind, 0xff none: the Shadow
at the first only, the Yellow Opera at the first and third, ...), and the next kind takes the first
place from the next free one (`data_ov107_020cbb04`) it has a build for, overlay 114 + the build,
taking its size / 0x1e00 + 1 places (`Ov107_LoadEnemyOverlay`); past the six the game stops
(`OS_Terminate`). A kind loaded keeps its place (`data_ov107_020cbb20`, its place + 1) for the field.
The table's row the enemy is made from only gives it its kind, level, boost, resistances' profile and
two values, all read as it is made. So a kind the area does not make is made from a row of the area's,
written as that kind with the mission's level for the call and put back after; the port first checks
the kind's overlay is loaded or fits. Seen: with the Shadow's place taken, 76 of the 118 kinds fit; a
Soldier (another mission's: HP 372 at level 1) was made, drawn, targeted and fought, and 28 fitted
after it; a Bulky Vendor next, then 5; the Turret then had no room and was refused, where the game
would have stopped.

**The mission's own held back** (`enemies.hold_mission`): its spawners' op 0 (an idle enemy set going
as the player comes near, every 32 frames) is skipped at its join while a mod holds them; the
mission's enemies stay idle in their sources, nothing of the area's tally moves and the area never
ends by them. Seen in mission 2's first area: the spawner's op 0 skipped every 32 frames while the
test mod's waves fought, and the mission stayed in its first area.

**The player placed** as the game's scripts place a player (`Ov002_ScriptPlaceSlot` ->
`Ov002_ApplyRosterSlotToNode` -> `func_ov022_02088218`): its actor moved with its child
(`Actor_SetVecAndSyncChild`), its position (`+0x48c`) set and its motion reset
(`Ov022_ResetActorAfterPlace`, which reads the battle's frame time: the battle context
`data_ov022_020b2e60` exists only once `Ov022_BeginScene` ran; before it the field's players are
already there, and the routine faults). The battle's beginning places the player itself. Seen: moved 3
units along x, then kept within 6 units of a point while chasing enemies.

## 23. The player's defeat and how a run ends

Read in the decompilation, then traced in the game (2026-10-22: the play walk opening
mission 2 with `KHDAYS_TEST_EVENT=2/0` -- Mission Mode, `data_0204c23c` still 94 -- with the test mod
`port/tests/mods_v2/last-stand`, and the mission walk's give-up).

- **A hit on the player** comes through `Ov022_ReceiveHit(actor, request)` on the host alone: the
  request's `+0x1c` is its source (an ov107 enemy, its kind at `+0x19c`), `Ov022_ComputeDamageTaken`
  the amount, then `Ov022_ActorSetHp(actor, HP - amount)`. `Ov022_ActorSetHp` holds the players' HP
  only (an enemy's is its own, section 22) and is where a lethal value is decided: in Mission Mode the
  host turns it into 1 for a peer `Ov002_MarkPeerReady` refuses. Seen with the test mod holding the
  defeat: 6 -> 1 and 1 -> 1 (damage 10 and 23 from its own strength-5 Shadows, the attacker's handle
  and kind told), no downed state; given back, the next hit took 1 -> 0.
- **A fall in Mission Mode** (alone): the field revived the player with full HP a few seconds after
  each fall (0 -> 62 of 62, a blue mark at the top right gone each time); after the third the next fall
  brought the game over's sub-flow (`Ov002_LoadSubFlowOverlay(9)`: ov027, "PULSA A",
  `Ov027_PollConfirmAndAdvance` waiting for A) with flag 0x2087 set, 0 -> 62 just before it. A went
  to the results, the mission "CANCELADA" (cancelled).
- **A give-up from the pause** goes through the same sub-flow (ov027 up, flag 0x2087), the player's
  HP never at 0: what tells the two apart is the fall just before.
- **The results are recorded as the results screen closes** (`Ov005_CloseResultScreen` calls
  `Ov005_CommitMissionResults` first): after "A / B Salir" and "¿Cerrar el repaso de la misión?" Sí.
  The defeat's run: rank -1, flag 0x2087; the give-up's: rank -1, flag 0x2087. Mission 2 cleared by
  attacking alone through its three areas (25,646 VBlanks in the field): the results counted 15
  mission points with the crown, a reward, rank 0, no flag 0x2087.

A mod's `ending` (`runtime/scripts/scripts.c`): `"lost"` when ov027 came up within 600 game frames of
the local player falling to 0 HP in that run, `"abandoned"` with flag 0x2087 otherwise, `"cleared"`
when the results record the mission cleared (`Ov005_CommitMissionResults`' own rule, shared with the
autosave), `"lost"` otherwise. Seen: the defeat's walk `lost`, the mission walk's give-up `abandoned`,
mission 2 cleared `cleared`.

## 24. Starting and leaving a mission

Read in the decompilation, then traced with `KHDAYS_TRACE_UPDATES` and `KHDAYS_TRACE_MISSION_START=1`
(2026-10-22, the play walk's save in Mission Mode's camp).

- **Mission Mode's camp** (scene 19, ov008) runs one scene task: `Ov008_MainMenuTopState` ->
  `_TopState_2` -> `Ov008_CommitSelectedPage` -> `Ov008_RouteCommittedPageState`, which stays while
  the camp's menu and its pages run (their ten modes through `Ov008_RunModeCallback`, not objects of
  their own). Once a page commits (`Ov008_CommitPage`'s `+0x9614`) with action 8 (`+0x9678`) it goes
  on to `Ov008_RefreshAndPickScene` -> `Ov008_CommitSynchronizedSnapshotState` -> destroy, and the
  scene class's method `Ov008_MainMenuExit` asks for the field (`Scene_RequestPending(2)`): a scene
  ends only when its task ends (`Scene_AdvanceToPending`).
- **The player's way in**: Holo-Missions (the menu's first option) opens the list of a world's
  missions (the play walk's save: Villa Crepúsculo, "Tutorial del modo Misión"); A shows its details,
  A asks "¿Emprender esta misión?" (No by default), LEFT and A answer Yes:
  `Ov008_StartSelectedMission` writes the mission (`data_0204c23c` its Mission Mode number,
  `data_0204c240` the field record: flags 0xf alone, 0x13, the mission file, kind; the thresholds at
  `data_0204c254`). Back on the menu the camp shows "MISIÓN ACEPTADA, Pulsa START" -- alone, with no
  flag the port's view reads (0x200c stays clear) -- and START leaves for the field. The list of
  missions (`Ov008_GetNextMissionEntry_5`) exists only while Holo-Missions' page is up.
- **A mod's `mission.start(file)`** (`runtime/menus/mission_start.c`): the port presses that way for
  the player, each press waiting for the page it is for (the camp's screen as `camp.c` reads it), the
  player's buttons held back meanwhile, and the field record is given the mod's mission file at
  `Ov002_ConstructGameplayScene` (as `KHDAYS_TEST_EVENT` does). Seen: asked at the camp's arrival,
  Holo-Missions 90 game frames later, the list 45, the details 45, Yes 57, START 45, the field 78 later
  with the record `0f 13 02 00 00 00 04 00` -- mission file 2 in place of the tutorial's 10, its first
  area's Shadows -- and `data_0204c23c` the tutorial's 94 (the results' mission).
- **Leaving** as the pause's Give Up does: `PauseMenu_ConfirmFrame`'s Yes on the entry calls
  `Ov002_PauseMissionScene` (the field's root `+0x8b64` bit 0, `+0x8b68` |= 0x10) and sets game field
  0x20ef; the field's session tick then goes through the game over's sub-flow (ov027, flag 0x2087) to
  the results. A mod's `mission.leave()` does the same: seen 600 frames into the tutorial,
  `abandoned`, the camp again; refused during mission 2's opening (its battle not loaded yet).

## 25. A mod's title entries and screens

Read in the port's own code and seen in a window (2026-10-22, the title walk with the test mod
`port/tests/mods_v2/menu`, with the HD PACK and without; window captures of the game's window only).

- **The title's menu** is ov000's (scene 1): the port reads its level and row from the scene
  object's context (section 15.1) and with the HD PACK draws the HD theater's menu as pictures made
  for each level, count and row (`backdrop {menu}-{count}-{row}`), so a line of the port's own cannot
  join the pictures' rows. The menu waits for the player while the scene object's update is
  `Ov000_TickMenuLoop`; it takes UP, DOWN, A and B: Y is free there.
- **A mod's entry** shows as a prompt in the title's bottom right corner (the HD title's settings key
  is in its top right): the player's input for Y and the entry's label (several entries: "Mods", a
  list of them). Y opens it (`runtime/scripts/screens.c` at `Pad_Sample`, the game given no Y).
- **A mod's screens** are drawn over the window by the port (`host/screens/mod_screen.c`), after the
  game's presented picture -- the HD shots (`KHDAYS_SHOTS_HD`) do not hold them, only a capture of the
  window does. Their look: the mod's own layout, else the HD theater's list the room list is drawn in
  (the HD PACK's `layouts/rooms/<lang>/rooms.txt`: its veil, rows, glove, scroll and help bar), else
  the game's own font over the game's picture dimmed. While one shows the game gets no button (the
  presses move it at `Pad_Sample`, as the port's other menus do); the buttons held when it closes stay
  the port's until let go, so the A that closed it does not reach the title.
- **Input icons**: in an HD layout an input the player has on the mouse (the keyboard's default Y is
  the right button, A the left) had neither icon nor name and drew nothing; it now draws Kenney's mouse
  icons, as the Controls section does (`layout_screen.c`).

## 26. The deck

Read in the decompilation, then each panel's record traced as the port reads it and checked against the decks of four saves (2026-10-22: the 100% save and the run07 walk's, 120
open cells all set; the old save and the play walk's, 96 set of 100); the English names from
`db_en.p2`'s entry 0x15 (each item's name and description).

- **Where**: `gGameState + 0xee0`, 3 pages of 40 cells (8 rows of 5) of item ids, page, row and column
  in that order. A panel's id stands at its own cell; the other cells a multi-cell panel covers hold 0
  or a one-cell panel set on it (one id a cell). Open are the first cells: item 1's copies (the Slot
  Releaser) + 15, at most 120 (`Ov008_GridMenuInitStep`); the camp marks the rest locked
  (`Ov008_InitPageGrid`).
- **The copies owned** are `aItemCount` (`+0x810`), set or not: a reward adds one
  (`Ov008_AddItemCount`, at most 99, the owned flag 0x4db + id); the camp sets at most that many
  (`Ov008_LoadGridFromSave`); `Ov004_RemoveLoadoutEntries` takes set copies from the last cell.
- **The panels' records** are message database 0x15 (`MsgDb_LoadRecord15`, 630 records of 0x64 bytes
  from id 1, `data_0204c238`'s row 0x15: `+8` the records, `+0xc` their count): `+0` the category, `+6`
  the kind, `+8` the shape (signed; -1 one cell), `+9` / `+0xa` the panel's own cell in its shape. The
  shapes are database 0x1b, a 64-bit mask each, bit column + 8 x row (`MsgDb_FetchEntryPair0x1b`).
- **Categories**, by the names: 0 items (Potion ...), 1 magic (Fire ...; multi-cell Magic LV2,
  Doublecast ...), 2 synthesis materials, recipes and techs, 3 Level Up and the LV Doublers (and the
  Slot Releaser, Backpack, Limit Pass, the badges), 4 the gears (multi-cell) and the units marked Ｌ
  (one cell), 5 the abilities (multi-cell) and their links marked Ｌ (one cell), 6 the rings and charms
  (kind 300 all). The camp's Panels list only what `Ov008_IsGridItemEligible` lets in (not items
  513-630, 1, 12, 392, 403-416, 450-452, 456, 457), in the tabs All, 0, 3, 1, 5, 4, 6, 2
  (`data_ov008_0208f208`).
- **How the camp holds it** (`Ov008_CanPlaceShape`, `Ov008_CanPlaceRecord`, `Ov008_LoadGridFromSave`),
  every save's deck so: a multi-cell panel within its page, on open cells, over no other; a link (one
  cell, category 4 or 5) on a cell of a multi-cell panel of its category, not that panel's own (the
  camp drops one that is not); other one-cell panels on a cell of their own or on such a panel of
  their category (Level Up on a Doubler, magic on Doublecast); one gear (items 0xbf-0xf0), one
  multi-cell ability of a kind, one ring or charm of a kind (so one in all). Links of a kind repeat.
- **When it counts**: the player's stats are folded from the deck as a camp commits a mission
  (`Ov008_RefreshAndPickScene` -> `Ov008_ResetPartyMemberAndLayout` -> `Ov008_RunTransientLayoutPass`
  -> `PlayRecord_FoldFrame`); a deck changed in the field counts from the next mission. The camp's
  Panels keep their own copy and write it back as they close (`Ov008_DestroyGridMenu`); the results
  take set potions, ethers and elixirs (items 2-11) beyond their limits
  (`Ov005_ClampEquippedItemCounts`); the calendar drops set copies the player no longer owns
  (`Ov004_BuildMissionObjectLists`). The deck's three presets are at `+0xc10`, 0xf0 bytes each
  (`Ov008_RunPendingGridAction`).
- **Seen** with the test mod `port/tests/mods_v2/loadout` (Mission Mode's camp, the play walk's save:
  its Mission Mode game has an empty deck and 25 open cells): Level Up and Fire set at cells 1.1.1 and
  1.1.2, mission file 2 started, left 600 frames in, the kept deck restored as it ended; back at the
  camp the deck as kept.

## 27. The game's music

Read in the decompilation and in the session logs' sound lines (`sound: seq/<name> starts on player
N`).

- **The tracks** are the sound archive's 39 sequences (`seq/TwilightR_F`, `seq/Herc_B`, ...; 0 and 27
  unnamed): every sequence is music, the effects are sequence archives' entries. `_F` and `_B` are a
  world's field and battle tracks; `data_02042124` gives each of the first 48 numbers its kind (1 a
  field's, 2 a battle's, 3 the bosses' and a few others), so a switch from a field's track to its battle's keeps the field's place to
  come back to.
- **The sound manager** (`gSoundMgr`) plays them on one player (`+0xb44c4`), the track playing at
  `+0xb46f6` (-1 none). Every change of music is a request in its queue of four (`ScriptQueue_Push`,
  `+0xb47a2`), made through one of four routines: `SoundMgr_QueueKind1` (kind 1),
  `SetSelectionIfChanged` (2), `RequestQueue_SetOrPushKind3` (3: stop, with a fade) and
  `SoundMgr_QueueRequest` (4 `SoundMgr_SwitchBgm`, 5 `SoundMgr_SwitchBgmResume`: switch with a fade,
  5 resuming a field track's place). `SoundMgr_Update` takes one request a frame, fades the track out
  (`NNS_SndPlayerStopSeq`), clears the music's sound heap and has the loader read the next
  (`StoreU16FieldAndRefresh`, `Loader_PostIdRequest`), then starts it
  (`NNS_SndArcPlayerStartSeq(player, track)`): a request's track is the sequence's number.
- **Who asks**: the event scripts (`Game_ActionEnqueueCmdIfChanged`: SwitchBgmResume for tracks 1, 6,
  8, 10, 12, 14, 17 and 34, SwitchBgm for the others), the field between its field and battle tracks
  (`Ov002_TickGameplayState`, `Ov002_StepIdleNudge`, fade 0x1e), the battle's end and the movies.
  `Ov002_UpdateFieldMusic` has nothing of music: it takes the mission bar's icon as every step is
  done.
- **A mod's track** (`music.play`) is asked for the game's way (`SoundMgr_SwitchBgm`); meanwhile the
  four routines keep the game's requests back (prepare.py's joins, `khdays_link_music_request`), and
  the last of them is made as the mod's track ends.

## 28. The magic

Read in the decompilation and traced with research knobs the port had for it (`KHDAYS_TRACE_STOP`,
`KHDAYS_TEST_HOLD`, `KHDAYS_TEST_STOP_EFFECT`, removed once this was written) on Mission Mode's first mission (94,
Twilight Town's square, "learn the basics of combat"), the panels set and three Shadows made at the player by a
research mod, 2026-10-22 and 23. Days began a sixth magic, Stop, and left it unfinished;
the port restores it (`runtime/battle/`).

- **What the game left of Stop**: its effect, `ba/ma/st.p.z` (a model) and `s0_mo`-`s2_mo` (one animation each
  level), beside the other magics' (`fi` and `f0`-`f2` Fire, `bl`/`b` Blizzard, `th`/`t` Thunder, `ae`/`a` Aero,
  `cu`/`c` Cure); its places in ov022's name tables, 18 for six magics of three levels (`data_ov022_020b2394` the
  level animations `f0`...`s2`, `data_ov022_020b23dc` the models, Stop's three a filler, `noting`); and its names in
  the battle's list (`UI/btl/<lang>/magic.s.z`, 18 names): English Stop, Stopra, Stopga, French Stop, Stop +, Stop X,
  the other languages empty. No panel, no description in any file or language, no battle templates (`ba/ch/mp.z`:
  75, 15 magics x 5), no code that casts it.
- **The panels' table** (message database 0x15, `db.p2` entry 0x15: 630 records of 0x64 bytes from id 1): `+0` the
  category (1 magic), `+4` its picture + 1 (below; 0 none), `+6` its kind (a parameter, below: Fire 2 ... Curaga 16),
  `+8` its shape, from `+0x18` its magics, four of three bytes (`MsgDb_LoadRecord15` widens them to `+0x34`-`+0x53`
  of the record it makes): a magic's index in the battle (Fire 1 ... Curaga 15; 0 none), a level and casts (Fire's
  1, 1, 1; Magic LV2-LV4 0, 2-4, 1; Doublecast, Triplecast, Quadcast 0, 0, 2-4); `+0x58` its price, `+0x60` the
  number of its words. Records 513-630 are blank (category 2, number 0, kind -1, no words here) and the camp leaves
  them out (`Ov008_IsGridItemEligible`, `Ov025_IsGridItemEligible`), but 513-627 are items a game gives: each has
  words of its own (its `+0x60`, 8000 on), and the maintainer's 100% save owns one of each of them (its counts at
  `gGameState`+0x810), the older save most; 628-630 no save at hand owns and no table of `db.p2` names. Database
  0x16's decoder walks 630 entries (`MsgDb_DecodeDb16`), one an item: the table is not to be grown. The words of
  database `id` are a pack of their own
  (`MsgDb_LoadDb` loads it beside the records, `+4` of the database's row): a count, each string's end, then the
  strings (`P2_ExtractString`), two per panel (its name, its description). A database is loaded from its pack into a
  fresh block of the current heap each time (`Archive_LoadFile`) and freed with the current heap's free
  (`ResSlot_Release_2`). Seen: the port writes Stop's three panels over 628-630 as the table loads (`stop: panels
  628-630 written (es)`, once at each load: seven loads from the camp to the field), and the camp sets them (the
  scripting interface's `deck.give(628, true)`: `true`, cells 1.1.1-1.1.3, category 1).
- **Parameters**: message database 0x13 names 115 kinds (two words each: Fire ... Curaga 1-15, Dodge Roll 16, every
  one used); `Params_BuildIndex` gives each a field of the party member's bit array (`gPartyMembers`, 0x104 bytes a
  player, from `+0x28`), the battle reading a kind's count with `Slot_EvalPackedParam(player, kind)`. A panel's kind
  `+6` is its parameter plus one. A magic's casts are a pair {left, most} at `+0x9a + 2 x kind`
  (`ClampAndStoreLevelEntry`, `Load2DArrayU8` through `data_0204c714`): the record holds them for kinds 1-53 only.
  The database's records are 4 bytes from 1 (`MsgDb_DecodeDb13`), the third a parameter's highest level: 5 for each
  of the 15 magics, 3 for Dodge Roll.
- **A magic's casts and level from the deck**, as the camp folds the deck for a mission (`Ov008_BuildMenuGrid`,
  `Ov008_RebuildViewAndCountCells`, `PlayRecord_FoldFrame`): the panels of several cells are laid out first
  (`Ov008_LayoutPanelEntry`; a panel of one cell is none of them), each one of magic giving every magic panel set on
  its other cells its own first magic's level and casts (`Ov008_TallySlots`); the panels on cells of their own then
  give each of their magics a cast and no level (`Ov008_FoldPanelIntoTally`); a magic panel on a panel of another
  category, none. Each magic's sums (`Ov008_AddToSecondaryTally`) are held to its parameter's highest level and 99
  casts, a 0 made 1; the casts are added to its pair and the level becomes its parameter (`StoreBytePairKeepMin`,
  `Slot_EvalPackedParamWith`): the parts the battle builds for it (`Ov022_BuildSlotPool`, its templates 1-5). So
  Fire on a Magic LV3 is a cast at level 3, on a Doublecast two casts at level 1, and two Fires on one Magic LV3 two
  casts at level 5.
- **The battle's magic** (ov022): `Ov022_BuildSlotPool` walks 15 kinds, slot kind `k` from parameter `k + 1`, at
  most 5 parts, one constructor each (`data_ov022_020b29e4[15]`). The pool (the actor's `+0x2288`, 0x70 bytes,
  cleared by `Ov022_InitEntryState`) keeps 15 slot places from `+0x18`; the word after them, `+0x54`, holds the cast's
  level animation (its grid entry, `Ov022_BuildEntryLabelText`), which 12 handlers bind (`Ov022_BindBlockAnimations`,
  `func_ov022_020b15a4`) and `Ov022_StepReactionState` waits on; the actor follows (`+0x58`). `+0xc` is the slot
  being cast (`Ov022_SetRecordValue`), and only that slot is stepped and drawn (`func_ov022_0209093c`). Seen: Fire
  and Fira set gave slot kinds 0 and 1 one part each, the other 13 none; a 16th slot written at `+0x54` was
  overwritten as the level animation loaded, and its draw ran a byte no SBC handler takes (`NNSi_G3dDrawInternal`
  called a null). The
  field's HUD (`Ov002_CreateAndRestoreHud`) enables the magic submenu's rows from parameters 1-15 and shows each
  row's casts (`Ov002_HudSetSlotValue`); its panel session keeps 18 rows (`Ov002_CreatePanelSession`, the 18 names
  of `magic.s.z`), a row's {index, count} at `+0x32` and the enabled mask at `+0x4a8` (`Ov002_Panel_IsSlotUsable`).
  So the submenu was made for 18 magics and the battle for 15. Seen: the submenu MAGIA listing Piro and Piro+ (the
  Spanish game), a cast greying the Magic command once its casts were spent.
- **An enemy's tick** is `Ov107_ProcessObjectTick(self, delta)`, 30 a second (seen: 120 of them in 238 frames). A
  frozen enemy (status `+0x1c4` 2, Blizzard's) or one in the air (8) has its tasks (`+0x3c`) updated with no time
  (`ObjList_Update(list, 0)`) and its motion zeroed in the contacts' pass. An AI task's `stop` field is not a hold:
  `ObjList_Update` tears down a task whose `stop` is set. Seen with the port holding a Shadow through that frozen
  path for 120 ticks (`KHDAYS_TEST_HOLD=120`): it stood, but bodies touching it still pushed it a little
  (`Ov107_AiState_ResolveContacts`: 0.6 units over the four seconds, Roxas attacking it), its action changed with
  the hit it took (2 to 5) and the hits during the hold lowered its health (67 to 58).
- **A hit on an enemy** (`Ov107_AiState_ApplyHit`): dropped during a hit's grace (`+0x2e4`), with `+0x1ac` bit 0 or
  status 0x10; its damage from `Ov107_CalcHitDamage`, the enemy's own handler (`+0x1d0`) lowering its health
  (`+0x21a`, most `+0x218`). A hit may give a status by its flags (`+2`'s byte, mask 0x88c): 1 burning (time
  0xc000, timed damage), 2 frozen (0x5000), 4 jolted (0xf000), 8 air-tossed (0x6000); each chance scaled by the
  enemy's resistance `+0x1b4[i]`, none where its immunity bits `+0x1b0` say so; pending in `+0x1c5` until it holds
  in `+0x1c4`. The five records `Ov107_AiState_LoadResistances` loads at `+0x314` are health thresholds, each a
  chance and an amount of drops when a hit crosses it (message kind 11), not resistances. Seen: Roxas's attack
  `04 00 00 40 ...` 8-9 damage to two Shadows at once (75 health), Fire `08 00 80 00 ...` 66.
- **A magic's cast**, traced: the submenu's A gives the actor command 8 with the row's magic index
  (`Ov022_UpdateCommandInput`, `Ov002_Panel_GetSelectedCellArg`), made a pending action 4
  (`Ov022_TranslateCommandToAction`); the cast starts in `Ov022_BeginSubStateDispatch`, which calls the magic's
  handler (`data_ov022_020b2a20[index]`, 15 of them; the step and update tables `data_ov022_020b2a5c`,
  `data_ov022_020b2a98` too; `data_ov022_020b2ad4` has 18 places, Stop's three empty), then spends a cast (seen:
  `Load2DArrayU8` 1, `ClampAndStoreLevelEntry` 0 of 1). Between them the pending action is allowed
  (`Ov022_IsSlotActionAllowed`: the pool's kind bit and the casts left), the actor takes the magic's pose
  (`Ov022_GetSlotMoveMode`, 15 indexes) and the index becomes the pool's cast slot (`Ov022_SetRecordValue`, from
  `Ov022_SetAnimState` or the player overlays' `ApplyModeChange` with the command's own argument); the pool's states
  then run (`Ov022_StepReactionState`: 1 and 2 a message, 3 the level animation loaded and waited on, 6, 5 the
  begin, 6, 8, 9, back to no slot). The level's animation is loaded as the cast starts
  (`Ov022_BuildEntryLabelText`, `ba/ma/<level>_mo.p.z`) into a buffer of the actor's own (`data_ov022_020b2eb8`,
  `FileLoader_LoadInto` at the entry's `+0x2c`), and its animation objects are bound to the slot's model
  (`Ov022_BindBlockAnimations`, `BindAnimTrack`), which first removes the ones bound before by walking the model's
  list (`NNS_G3dRenderObjRemoveAnmObj`). That walk holds only because each slot always loads the same animation into
  the same buffer, its objects landing where the last ones were: seen with one slot binding Stop's `s0_mo` and then
  `s1_mo`, the walk ran through `s1_mo`'s bytes and faulted (`removeLink_`). The actor's state sent to the other
  players carries the magic's index (`Ov022_SendActorStatePacket`, 10 bits). The submenu's cursor starts at its
  first row, then stays where the last cast left it, a row with no casts left included; down from the last row in
  use goes back to the first (seen with Stop's three rows). Seen too: with Shadows striking Roxas, the submenu's A
  on a magic cast nothing, the submenu left open, until they stopped.
- **Stop's effect** is a clock (`ef_stop_0`: number1-number12, needle1-needle4, rings and lines), each level's
  animation moving one object (`ef_stop_0`, `ef_stpr_0`, `ef_stpg_0`), as Cure's and Cura's do (Fire's, Blizzard's
  and Thunder's move two to four: a cast, a flight, a hit). Seen with the port's test knob
  (`KHDAYS_TEST_STOP_EFFECT=<magic>`, that magic loading Stop's model and first animation): with Fire's handlers a
  purple clock with gold numerals stood round the target, then the game stopped (frame 7560, no frame after);
  with Aeroga's and with Cure's the cast went on and nothing of the clock showed. Seen with Stop's slot in a place
  of its own taking Cure's handlers (below): the purple clock with its gold numerals stood on the ground round Roxas
  for a moment as the cast began; Stopga's is wider, with four crystal hands and red numerals.
- **Stop in the port's battle** (`runtime/battle/stop_magic.c`, prepare.py's joins): the pool's level animation
  word is the port's, so `+0x54` is Stop's place, 15; its slot is Cure's constructor over `st.p.z` with a kind of
  its own (15, so `Ov022_ApplyReactionHit` heals no one), Cure's begin, step and update handlers, its level's
  animation `s<level>_mo` with the animations bound before forgotten as it loads; its levels are the submenu's cells
  15-17, their casts the port's. It holds only enemies standing (set going, `+0x60` 0x100): the first casts also
  held three of the mission's own still idle in the area's table. The other magics, with the level animation's word
  the port's, cast as before (seen: Fire, Blizzard, Thunder, Aero and Cure, each to its last state). Seen on the
  walk (`port/tools/regress_walks/stop.input` with the test mod `port/tests/mods_v2/stop`: mission 94, its own enemies
  held back, three Shadows made four units from Roxas): Stopga (on a Magic LV2) held the three for 300 ticks (reach 6
  units); Stopra, cast while they were held, held them again for 180, not added to the 102 left; Stop, cast as that
  hold ended, for 120; Roxas's hits during the last hold were kept (13, 22, 13) and each Shadow took its sum at once
  as it was released.
- **The panels' pictures** (`ui/pnl/3d_&.pak.z`, a KAPH archive: its member 7 a table of 213 BTX0 files, each a 16x16
  picture of 4 bits and a palette of 8 colours) are loaded by the camp's two menus and the results
  (`Ov008_InitGridMenuTextures`, `Ov025_InitGridMenuTextures`, `Ov005_AllocateResultTextures`), the first 0xd1 of them
  into VRAM; a panel shows picture `+4` - 1 (Fire's 0x22 ... Curaga's 0x30, Magic LV2-4 0x31-0x33). The pictures'
  palettes keep their edges and ground in colours 2-4: the magics' purple (0x1c47, 0x342a, 0x5031), the Level Up's
  orange. Pictures 0x0d-0x1f are no panel's: orbs, a leaf, a flower, crosses, an eye, a sun, a moon, a lamp, a clock
  (0x1b), "ES x2", "ES x3", all on orange. Database 0x1b is `db.p2`'s entry 0x1a: a database above 0x15 is its pack's
  entry `id` - 1 (`MsgDb_LoadDb`).
- **Stop's panels' picture** is that clock, its edges and ground made the magics' (colours 2-4 of Blizzard's, 0x25)
  as the archive loads (`khdays_stop_pictures`). Seen in the camp's Panels: the three in the deck and in the magic tab
  (Paro, Paro+, Paro++ after Magic LV2 and Doublecast, as `Nv. mágico +2` and `Conjuro x2`), the panel's description
  with its status word highlighted.
- **The shop's pictures** are 2D (`ui/shop/res.p2` and `res_i18n.p2`, loaded by `Ov026_InitPanelSlotManagers` and
  `Ov008_InitPanelSlotManagers`): D2KP packs whose cell bank has 208 cells, the same pictures as the 3D ones, each one
  OAM of 16x16 at 8 bits on one 256-colour palette; a row shows cell `+4` - 1 (`Ov026_DrawShopRowMarks`). The clock
  there keeps its orange edges and ground as indices of its own (0x74, 0x2a, 0x0f; the magics' 0x15, 0x05, 0x02);
  the port gives it the magics' as such a pack loads (`Archive_LoadFile`, `khdays_stop_file_loaded`).
- **The moogle's recipes** (database 0x1f, `db.p2` entry 0x1e: 101 records of 0x2c bytes) are each a product, 1, the
  munny, then four ingredients {item, count} (`Ov026_BuildSlotPacketB`). A magic's: its recipe item (Blizzard Recipe
  ... Curaga Recipe, items 251-265), the level before, two of its element's pebble, gem or crystal; 700 to 2600 munny,
  Cure's the dearest (1600, 2000, 2600). The moogle lists a recipe once its first two ingredients have been owned (the
  owned flags, 0x4db + id) and its product is not one held back (`Ov026_IsRecipeListed`, `Ov026_IsRewardItemShown`);
  a recipe made is marked by its place in the table (a bit of `gGameState` + 0x1060, `Ov026_QueryRecordUnlock`,
  `Ov026_CommitSynthesisOrder`). Database 0x20 (`db.p2` entry 0x1f, 106 records of 0x1c bytes) is the items the
  moogle sells: an item, its munny, and a last word from 26 to 357.
- **Stop's recipes** are added after the game's when the table loads (`stop_magic.txt`'s `recipe` lines; places
  101-103, bits no save sets and the DS's game never reads): the ice and thunder materials a grade above the other
  magics' same level, the level before, Stopga a Diamond too, and more munny than any magic (2400, 3200, 4200), Stop
  being strong; no recipe item, the game having none for Stop and no item id left for one.
  Seen at Mission Mode's camp (the test mod `port/tests/mods_v2/stop_shop`, the walk `stop_shop`): with the two gems
  given, the Synthesise list showed Stop at 2400 after the save's three recipes, its ingredients and its description;
  Yes made it (OBTAIN Stop), the platines down from 2600 to 200, the gems spent, Stop owned.
- **A save with Stop in an older port**: Stop's panels are items 628-630 of the save's own arrays (their counts at
  `+0x810`, their owned flags, the deck's cells at `+0xee0`), so the DS's format holds them and an older port reads
  them as blank records. Seen: the full save with Stop, Stopra and Stopga on three of its Cure cells (the scripting
  interface's `deck.restore`) loaded by the copy of the port installed on 2026-10-22, which has no Stop, played into
  the field and saved again: no fault, the save whole, the three still owned and in the deck. A walk's anchors by
  name (`uOv000_TickMenuLoop`) need the build's symbols, which an installed copy has not: the same presses counted
  from boot do.
- **Stop's casts and Magic LV** are folded from the deck by the camp's rule for the other magics (above): a Stop panel
  on a cell of its own gives its level a cast, on a Magic LV or a Doublecast that panel's level and casts, its
  highest level Blizzard's (5). The game has no part for Stop's levels above 1 (its slot is Cure's first), so a
  Magic LV lengthens the hold instead: by `stop_magic.txt`'s `magic_lv` hundredths (25) for each level above 1.
  Seen on the walk with Stop on a Doublecast and Stopga on a Magic LV2: `casts 2 1 1, Magic LV 1 1 2`, Stopga's hold
  300 ticks.
- **Stop in a room**: the battle is the host's (an enemy takes hits only on its owner's copy, `+0x50` 1, in
  `Ov107_AiState_ApplyHit`), and every copy plays every player's casts (the cast's message 0xc carries the slot in 5
  bits, `Ov022_MarshalStateHalf12`; `Ov022_OnEndMessage` makes it the receiver's cast slot), so the port gives every
  player's actor Stop's slot in a room, sends the level as Stop's cells 15-17 in that message, and each copy's casts
  and Magic LVs in its pings (every 250 ms; the host's list everyone's). On every copy a cast marks its zone and the
  enemies' ticks are held there; an enemy's copy on a guest (`+0x50` 2) counts as set going while the host's state
  has it active (`+0x60` bit 0). Rooms take only copies of the same build, so every member reads the same Stop, and
  only copies that agree on Stop being on (below).
  With the room's friendly fire on (`data_0204c240` bit 2 and `data_0204c248`: 1 low, 2 high), a cast holds the other
  players its zone reaches, never its caster: the host keeps a held player's input, state and motion
  (`Ov022_ActorUpdate`), and every copy its animation. Seen on two copies (the walks `stop_room_1`, `stop_room_2`,
  the test mod `port/tests/mods_v2/stop_room`): each of the six Stops held the other player for 120 ticks, Stopga
  for 240 and Stopra for 180, taken on both copies within two frames of each other and released together.
- **Stop turned off** (`[gameplay] stop_magic = off`, Settings > General, read as the game starts): every part of
  it answers as without Stop (`khdays_stop_set_enabled`): no records written over the blank ones, no words, no
  recipes, no names in the battle's list, no slot, cells or holds; the game is the DS's. A save's Stop panels stay
  as items 628-630 and on their cells, unseen, as an older port keeps them. Its being off counts in the room's
  fingerprint as a gameplay mod would, so a room takes only copies that agree; a refused guest is told the host
  plays without it, or, its own off, that the host plays with it. Seen on the walk `stop` with it off: the session
  log's `stop: off`, the test mod's three panels refused (`unknown panel`), mission 94 played to its time with no
  fault.

## 29. Missions and worlds of mods

Read in the decompilation and seen with the test mod `port/tests/mods_v2/missions` (the walk `missions`), 2026-10-24.
The port adds a mod's missions to the game's own list and plays them as the game's (`runtime/missions/`).

- **The missions' list** (`UI/cm/msl_<lang>.msi.z`, 273 records in each language): a u16 count, then records led by
  their u16 size (72-1,076 bytes). `Ov008_BuildRecordNode` makes a 0x58-byte node of each: `+2` the mission file's
  number, `+4` the mission's number, `+6`/`+8` the first and last day of the story it is listed on, `+0xa` its
  category (the world picker's: a mission's world, `tt` 0 ... `bb` 9), `+0xc` 1 for the story's hidden "next day"
  rows (900-916), `+0xe` the number the row shows, `+0x10` a tier, `+0x14`-`+0x17` bytes, `+0x18` the time limit the
  info shows (seconds), `+0x1a` its enemies, `+0x1c`-`+0x2e` the info's further lines (`+0x28` the results' reward
  kind), `+0x23` its kind; then four UTF-16 texts: its name, a briefing, the objective the info shows last, a format.
- **A record's kind**: 0 is a mission's row in the story's lists (`Ov008_RecordFilter_InRange` and its kin, by the
  day: 108 rows), 4 its row in Mission Mode's (80), 1 and 2 its other entries there (65, 20). A mission has a row of
  each kind it takes, all with its file and number; Mission Mode's tutorial is mission 94 on file 0010, a kind-4 row
  alone, shown as 00.
- **Mission Mode's list** (scene 19, `Ov008_MainMenu_StateTick`) is built a category at a time from the rows
  `Ov008_IsEncounterEligible` takes for the list's flags (its category << 8, bit 3 the kind-4 rows): a row of
  another kind or category is left out, one whose number is 900 or more too, then one whose last day is not past in
  the story (`GameState_GetField(0, 9)`, 401 once field 0x44e reaches 6), and one whose mission's 4 bits at
  `0x92b + 4n` lack its kind's bit. When the camp has no entry flagged (`Ov008_AnyEntryFlagged`: the words from its
  context's +0x9758, as many as +0x9750 says), the list is the tutorial's row alone (list kind 7,
  `Ov008_RecordFilter_Kind4Id10`). Seen: the walks' guest game (a level-1 Roxas, no file) lists 00, the tutorial, in
  every world.
  The list's rows of a world go by DOWN and UP; UP on its first row goes to its last. L and R change the world.
- **What counts the game's missions**: `Ov008_CalcMissionCompletionPercent` (the camp's percentage) counts the
  rows of kinds 1 and 2 (`kind & 3`), not Mission Mode's own (kind 4); `Ov005_UpdateAllMissionsClearedFlag` asks
  missions 1-93 by number. A row of kind 4 alone never changes either.
- **A mission's progress fields** (the game state's flag array, `gGameState` + 0x10, 0x4000 bits): status
  `0x28e4 + 3n` (1 seen, 2 cleared, 3 complete: the story's results), crown `0x2a4c + 3n`, Mission Mode's rank
  `0x2ca4 + 3n` (the only one Mission Mode's results write: its rows' reward kind is 8), the 4 bits at `0x92b + 4n`,
  and the best result `gGameState` + 0x198c + 4n for n below 200 (`Ov005_CommitMissionResults`, three reads in
  `Ov005_InitializeResultConfiguration`, the two camps' `*_MissionMenu_DrawInfo`). Each block holds 120 missions;
  the game reaches no field past the array: the port says so in the session log when one is asked for (`missions:
  the game reached field ...`), and no run here has said it.
- **The port's new missions** (`runtime/missions/`): a mod's `missions/missions.txt` gives each mission a number
  whose four fields all lie past the array (from 3510: 0x92b + 4 x 3510 = 0x4003), its number's last two digits the
  ones it is shown with (95 on, after the tutorial's 94), and a file number from 20000; `GameState_GetField`,
  `_SetField`, `_IsFlagSet`, `_SetFlag` and `_ClearFlag` answer its fields from the port's store, and the best result
  joins answer for numbers past 200, so the game's own code shows, starts, ranks and records it. As
  `UI/cm/msl_<lang>.msi.z` and `mi/mi/mdb.z` load (`Archive_LoadFile`) the port hands them back with the new
  missions' rows after the game's: in the list a Mission Mode row made from its template's (its days past the
  story's, so no story list takes it), its texts in that language (English where it has none); in `mdb.z` its
  template's row. `Ov008_IsEncounterEligible` takes a new mission's row by the port's own test (its category, the
  kinds the list asks for, its unlock) since its number is past 900; `FS_OpenFile` opens `mi/mi/<its file>` where
  the port placed the mod's file in the ROM's image (or its template's file, for a mission without its own), the
  file table not naming it. Its progress is kept in `saves/missions/fileNN.txt` beside the save file (read when the
  title loads file NN, written after each save action of file NN), never in the DS's save.
- **Seen** (the walk `missions`, the test mod `port/tests/mods_v2/missions`: a mission on 1354's file, "survive for
  60 seconds", listed in Twilight Town's category; the maintainer's save, file 1): its row last of Twilight Town's
  (95, "El reloj perdido"); started, the game opened `mi/mi/20000`, the mod heard mission 3595; cleared when the
  clock ran out (rank 0); the autosave wrote file 99 and the port `saves/missions/file99.txt` (`test.missions/clock 0
  0 4 0 -1`: Mission Mode's rank 4, no status, no best result, as the game records a Mission Mode mission). With the
  autosave into the file played: file 1 loaded again with the mod showed the row with its crown; without the mod the
  list ended at 92 as the game's, and the save's every mission field (1-119, the four kinds) was as before the run.
- **A mission built from its description** (2026-10-28): a mission with waves in `missions.txt` (`start`, `wave
  <n>`, `chest`, `emblem`, beside its time, texts, world and room) is built by the port at its start
  (`runtime/missions/build.cpp`, khdays-studio's `core/missions/mission_build.cpp` compiled in) from the game's own
  steps: 1354's area (world, room, actors, HUD, clock, the ending's `c0`/`c1`), its start's `_i` (the chests, the
  emblems, the enemy table, the objective) and 0004's "all defeated" check (2.7). Wave 1's spawners are made in the
  area's `_i`; each next wave is a script (`w2`, `w3` ...) queued on the last one's "all defeated" event, the last's
  queues `c0` (state 8844 raised, then `c1` 4000 ms later to area 99 and the results). Each enemy of every wave has
  its own actor (2.49). A spawner made by a later wave would never run (section 18, "Spawners"): the join at
  `Ov002_CreateSlotObjectAndStart` sets it running with its region in the port's missions only (session log:
  `missions: <mission>: a spawner made after its region started, running with it`).
- **Chests and emblems of a new mission**: their sets are added to `mi/mi/trbox` and `mi/mi/evi` as they load (the
  key the mission's file number; a chest's room byte is the mission's room, its opened flag one of the port's past
  the flag array, kept in the save part). Seven places of ov002 leave chests and emblems out in Mission Mode (mode flag
  bit 1): `Ov002_ScriptSpawnTimedSet` (the chests put out), `Ov002_SpawnStateSpots` (the emblems' pickups),
  `Ov002_NoteSeatAnswer` (a chest opened marked in the root's +0x8d80), `Ov002_RaiseSeatFlagsForKey` (the marked
  chests' flags raised at the end), `Ov002_ModuleHasKey` (the chests' module asked by the panel and the items),
  `Ov002_GetPanelValueBand` and `Ov002_FillMissionResult` (Mission Mode's own panel and results, left as they are);
  the first five are joined to act as the story's for a new mission. `Ov002_AppendPendingId`'s ids are the message
  database's (the names the field shows, `Ov002_BuildKeyEntryTable`), not files.
- **Seen** (the walk `missions_waves`, the test mod `port/tests/mods_v2/missions_waves`: two waves of Shadows in eh
  room 2, a chest, an emblem, 120 s; the maintainer's save): listed as 95 "Dos oleadas"; wave 1's Shadow out at the
  start (`KHDAYS_TRACE_MISSION`: 1 of 1 enemies out), defeated by the attack button; wave 2's spawner made, set
  running, its two Shadows out (2 of 2) and defeated; state 8326 0 -> 1, "Misión completada", the results (crown
  reward), the camp. The chest put out (1 of 1 placed), opened with Examinar ("Ganas: Poción", the panel's 1/1), its
  mark (0001) raised at the end and kept: `test.waves/waves 0 0 4 0 -1 1`. **The emblem**: its pickup is made at its
  point (the field's pickups, id 1 at (0, 0.049, -1.5), by `Ov002_SpawnStateSpots` with the mission's number 3595),
  but in these runs it was neither drawn nor picked up: *open*.

## 30. Characters of mods

Read in the decompilation and traced with the walk `mission` (`KHDAYS_WATCH` on the member records and the seats),
2026-10-28. The port adds a mod's playable characters to Mission Mode (`runtime/characters/`).

- **A player's character** is byte `+3` of its member record, `gPartyMembers` (0x0204c678, four records of 0x104
  bytes: `+0` the slot, `+1` a kind, `+2` the level, `+3` the character; `PartyMember_Reset` writes the header, the
  level row `LevelTable_ReadEntry(character, level)` into `+8` and the character's signature skill from
  `data_02042160`, a table of 22: characters 0x00-0x15). Its numbers are the data folders' (`data_02042a70`, 20
  codes under `ba/ch/`): 0 ro, 1 ax, 2 xi, 3 sa, 4 xa, 5 so, 6 de, 7 la, 8 le, 9 lu, 10 ma, 11 ri, 12 ve, 13 xe,
  14 xo, 15 ze, 16 mi, 17 do, 18 go, 19 r2. The game maps three past those onto others itself: 0x13 and 0x14 onto
  0, 0x15 onto 0xe (`PartyMember_RebuildDerived`'s growth row; `Ov022_FillSelectorStates` and
  `LevelTable_ReadEntry` 0x14 and 0x15).
- **From the select to the field** (seen, the walk `mission`, Roxas): the select's places keep the select's own
  numbers (`+2`, a signed byte: `Ov006_GetMissionRowInfo`), 0-18 in its order (the Organization's thirteen from
  Xemnas to Roxas, then Xion, the king, Sora, Riku, Donald, Goofy); `Ov006_MenuEntryToSlot` turns one into the
  folders' number (0 -> 0xd ... 12 -> 0, 13 -> 0xe, 14 -> 0x10, 15 -> 5, 16 -> 0xb, 17 -> 0x11, 18 -> 0x12) as
  `Ov006_UpdateMissionEntrySynchronization` writes the seats, `data_020429c8` (four of 8 bytes: a word set when
  taken, the character's word; `Slot4_GetIfOccupied`), VBlank 2405. The camp (`Ov008_CommitSelectedPage`) resets
  member 0 with its seat's character (`Ov008_ResetPartyMemberAndLayout` -> `PartyMember_ResetWithKind(0, 0, c, 0)`),
  VBlank 2480; the field rewrites the header (`Ov002_UpdatePartyEntries`, VBlank 5565). In multiplayer the camp
  copies the host's four records whole (`Ov008_CommitSynchronizedSnapshotState`): byte `+3` travels with them.
- **A player becomes an actor**: `Ov022_FillSelectorStates` loads the overlay of the group named by the character
  for the player's slot (`Ov029_AcquireOverlaySlot(slot, character)`, `data_ov029_020b30b0[20][4]`: ov030-048 the
  first player's, ov049 the dual-wielding Roxas, ov050-104 the others'), whose constructor (`Ov030_CreateActor` for
  Roxas) sets the actor's `+9` (the member's slot) and `+0xc` (a constant of the overlay: its character), loads its
  body's pack by a literal path (`ba/ch/ro/def.p.z`, `def_hb`, `def_hho`) through `TailForwardTrackEntry(slot, path,
  1, slot + 7)` -- called by the 74 player overlays' constructors and by `Ov023_CmdSetupPartyActors` /
  `Ov023_CmdSeatMembers` (there with packed handles, bit 31 set) -- and finds its bones by name (`roxas_tg`,
  `ro_w_tg_R/L`, `roxas_R`, `Bip01`).
- **The folder's readers** by the actor's `+0xc` or a character: `LevelTable_ReadEntry` (`lv.b.z`),
  `Ov022_BuildActorSlot` (`w_.p2`), `Ov022_BuildActionTable` (`ci.b.z`, `cm.b.z`, and the weapon's move block
  through `Ov002_LoadCharacterWeapon`, `wp.b.z`, read Xigbar's way for character 2), `Ov022_LoadActionRows`
  (`am.p2`), `Ov022_LoadActorEffects`, `Ov022_StartAnimRequest` (`gl`, `rg`, `as`, `ma0`, `ma2`, `it` `.p.z`,
  `eta<n>.p.z`), `Ov023_CmdSetupPartyActors`, `Ov008_LoadCharacterWeapon`, and Mission Mode's camp model
  (`Ov008_Menu_InitSceneObject`: records of 0x34 by character at `data_ov008_0208e9c4`, the first word its code;
  `def_hb` for 0, `def` for 5 and 0x10-0x12, `def_h` for the rest; `Ov008_Menu_LoadSceneText` its weapons' texts,
  Roxas's for Xion).
- **The port's characters**: a mod's `characters/<key>/character.ini` ([CHARACTERS.md](CHARACTERS.md)) gives a
  character a number from 0x16 (their identities' order; 0x7f the last, the places' signed byte). Only the
  select's places hold it: the seats and the members get its base's number (`Ov006_MenuEntryToSlot` /
  `Ov008_MenuEntryToSlot` joined), so that the game's tables of characters -- the results', the camp's, the
  shop's, the saves -- never see one they do not have, and the port keeps which seat is a new character's (the
  joins at the seats' writes, `Ov006_UpdateMissionEntrySynchronization`, `Ov008_MissionLobbyPoll`; forgotten at
  the title). The base's overlay plays it. Where the game opens a seat's character's data, the joins
  (`port/tools/prepare.py`, "Characters of mods") give the character's own folder `ba/ch/c<number>/`, which the
  port serves at the start (the mod's `data/` files, else copies of the base's: their own places in the ROM, so
  that a model loaded from there is known as the character's and drawn as its model). The game's own select
  screens draw a place on one as its base's place (`Ov006_UpdateMissionMemberMenuScreen`, `Ov008_LobbyStep`:
  their gauges, model pose, names and slots index tables of 19, which a new number would run past: seen,
  `Ov006_MissionApplyParameterRow` stopped the game reading row 0x16). Their input
  (`Ov00N_UpdateMissionMemberSelectionInput`) looks for the place on the DS's grid (clamped to 18 there) and,
  on any button, writes back the grid's character -- -1 for one it does not hold -- and the screen asks for
  its place every frame (`Ov006_MissionArmCursorRequest`, seen with a watch): joined to keep the place's own
  number unless a direction moves it.
- **Its animations** (`skeleton = own`, seen 2026-10-28 with the maintainer's character on Roxas, the walk
  `missions_waves`): each draw's render object carries the joint animation the game plays (`anmJnt`, its
  frame and its BCA0 resource). Roxas's: standing `b_ro_A000` (39 frames), running `b_ro_A001` (18), the
  ground combo `b_ro_B009`, `b_ro_B008`, `b_ro_B015`, then `b_ro_A004`; his folder's packs hold `b_ro_A000`-
  `A011` (`def*.p.z`), `b_ro_e000`-`E010` (`etc.p.z`), `b_ro_D000` (`it.p.z`), `b_ro_D001`-`D003` (`ma0`-`ma2`),
  `b_ro_L000`/`L001` (`li.p.z`), `b_ro_E011`/`E013`/`E014` (`eta*.p.z`); the B-series comes from elsewhere
  (not found in his folder as text). The port draws the character's skeleton from the clip its description
  gives each name, at the game's animation's point.
- **Its weapon**: the game puts the weapon on the base model's `*_w_tg_R` node, read during the body's draw
  by the node's callback (`NNS_G3dGetCurrentMtx` at `NODEDESC`, timing 3: the camp's
  `Ov008_RefreshMatchingMatrices`; the field's actor finds the node by name in `Ov030_CreateActor`). The
  port loads the character's own weapon bone's matrix there first, so the game's weapon follows it.
- **Its voice** (read 2026-10-28; heard in `missions_waves`'s session log): an actor's sounds start through
  `Ov022_PlayEntityVoice(actor, archive, entry)` (its action's cues from `Ov022_StepReactionCues`, its hurt,
  the player overlay's own: `Ov030_UpdateMotionController`) and `Ov022_SpawnVoiceCue`, both into `Slot_Spawn` ->
  `NNS_SndArcPlayerStartSeqArcEx`, which starts the entry at once. The actor's seat is its +9, its character
  its +0xc (`PlayEntityVoice`'s switch on it picks Roxas, Sora, Riku, Xion, Mickey, Donald). Roxas's attack
  shouts: `seqarc/PLY_ROX/vo_rs02atNN` on a voices' player; his swings `seqarc/WEP_ROX26/se_rox_weap_swingNN`
  and run steps `seqarc/FIELD_ROX_FS_LAST/se_rox_fs_last_run01/02` are effects. The port is told the actor
  around those two `Slot_Spawn`s, and a new character's `[sounds]` choose the entry's file then.
- **Its combos** (`ba/ch/<code>/ci.b.z`, unpacked by its name's `.z`; read 2026-10-28 from Roxas's): a word
  counting the keyblades (Roxas 18), then 0x34 bytes each -- the six pools' places from the file's start, their
  counts (u16 x3 a graph), each graph's four entry points into its first pool (s16 x4) -- then the pools, 8
  bytes a node (the attack's number, then s16 the node it goes on to and the branch: an index in the pool,
  0x80 the graph's entry point with the next pool as its alternate, -1 none). The keyblade's record is the one
  `Ov022_RebaseAnimRecord` takes (`ActionTable` +0x18); its graph 0 is the ground combo, 1 the air one
  (`Ov022_BuildNodeGraph`): pool 0 the chain, pool 1 the finisher, pool 2 the branches' nodes; entry 1 is where
  the chain's last node goes on (0: from the start), entry 2 its last node. Roxas's first keyblade: ground
  `0 1`, finisher `2`; air `3 4`, finisher `5`; the others' attacks are numbered by keyblade (1010, 1060, 1000;
  2060, 2080, 2050 ...).
- **Its attacks** (`cm.b.z`): a word whose low byte counts them (Roxas 96), their places, then each a record of
  15 words -- its number, its hits' kind, the animation's row its attack plays (`Ov022_FindOrClaimTagSlot`'s tag:
  16 rows a keyblade), ..., the hits' count -- and its hits (`Ov022_UnpackChannelGrid`). A sweeping hit (kind
  0, `Ov022_RunContactMode0`) is 15 words: its damage (fx32, `Ov022_ComputeStrengthDamage`'s factor), its first
  and last frame, its push out and up, two flags, a level, then its place (x, y, z: turned by the actor's facing),
  its radius, its spread's cosine and its event. Roxas's first hit: damage 0.75 over frames 6-12 at 1.19 radius;
  the finisher five hits of 1.5 sweeping round over frames 12-21. `Ov022_LinkActionAnims` keeps at most 64
  attacks of a keyblade's two graphs. The port writes a new character's `[moveset]` into its own copies of
  both (unpacked, rewritten, packed again as LZ11 literals) when the game starts.
- **Its Limit** (seen 2026-10-28 in `missions_waves` with the HP kept low by a test mod, the tutorial's text:
  hold A once the HP is in the yellow): Roxas's plays `b_ro_L000` as it starts, then `b_ro_L001` at each of
  its hits with `b_ro_A003`/`b_ro_A004` between them, shouting `vo_rs02at01`/`vo_rs02at03`; the combo comes
  back after it. Its animations are his folder's `li.p.z` (one BCA0) and its effects `li_e0`-`li_e3`,
  `li_ea0` (`Ov030_BuildActorHandles`, `Ov030_initStateSlotsDispatch`, the paths written in ov030). Its
  hits are not `cm.b`'s attacks (`Ov022_RunComboContacts` never runs during them): ov030's own code
  (`Ov030_EmitTimelineEffect`: a hit every 6 frames, its damage `Ov022_ScaleRowValues` with factor 1.23,
  1.17 for its other kind). Every player overlay makes its own moves' damage through
  `Ov022_ScaleRowValues` (the actor's seat's strength and bonuses, capped at 999, times the factor), which
  the port joins for a new character's `[limit] damage`.

## 31. Mods' native plugins

- **How the game closes** (read 2026-10-28, `host/app/window.c`): the window's close (`SDL_EVENT_QUIT`) ends the
  process at once with `_Exit(0)` from the window's thread, the game's thread still running its frame; nothing of the
  game is stopped. The port's native plugins (`port/runtime/plugins`, docs/MODDING_NATIVE.md) are stopped on the
  game's thread first: the window asks (`khdays_plugins_quit`) and waits up to a second, the game's thread stops them
  at its next frame (`khdays_plugins_frame`, from `khdays_host_frame`). Seen with the SDK's Hello World: its goodbye
  logged and its library released before the process ended.
- **The first hook points** (seen 2026-10-28 with `sdk/examples/AdvancedNativeMod` in `missions_waves`):
  `Ov022_ActorSetHp` runs for the player's HP (a Shadow's hit of 1 on a 359-HP player) but not for the enemies'
  (the Lua mods' `hp` event hears no enemy either): their HP goes another way, a point of its own to find.
  `PlaySound` starts the menus' sound effects (6-7 a scene there) but none of the field's, which go through
  `Slot_Spawn` (`Ov022_PlayEntityVoice`, section 30).
- **The enemies' and the field's points** (seen 2026-10-28, the same mission with `sdk/examples/CustomCharacter`):
  `Ov107_AiState_ApplyHit` runs for each hit an enemy takes; after it, a Shadow's HP read 0 of 75 from a 618-damage
  hit and the enemy was still standing (+0x60 bit 0x100): the defeat comes after the routine returns. `Slot_Spawn`
  started 83 sounds in that field and none in the menus before it (where `PlaySound` started 3-7 a scene).
- **The standing enemies** are ov107's actor manager's list (`gOv107ActorManager` + 4, `List_First`/`List_Next`: +4
  its first node, +0x10 its end, a node's +4 the next, +0xc an element whose first word is the actor): the port walks
  it without the list's cursor (+0x24 of the list), which the game's own walks keep, and takes those set going
  (+0x60 bit 0x100). The id table (+0x48, `Ov107_Scene_BuildIdTable`) is not used: it is made again after each new
  actor, and between the two its size (+0x24) is ahead of it.
- **A player's attacks** (`Ov022_RunComboContacts`, each battle tick of an attack, section 30): the record's first
  half-word is the attack's id as `[moveset]` names it. Roxas's combo with his own files went 13090, 13080, 13150
  (Ventus's rewritten ones 0, 1, 2), each attack's hits worked out several times as its frames test them
  (`Ov022_ComputeStrengthDamage` 1-6 times an attack).
- **A player's states** (seen 2026-10-29 with `Ov022_ActorSetState`'s hook point, Roxas's code, `missions_waves`):
  the routine takes the state's number and answers its routine (the DS address the actor keeps at +0x460, 0 for
  none), which the caller makes the actor's. The combo is 3 then 14 (`Ov022_StepComboState`, mode 0x27); the Limit 23
  as it starts (`Ov022_SelectForcedState` when the actor's state block says so: mode 0x2e, its `b_ro_L000`), then at
  each hit 34 (`Ov030_ArmPlayerTarget`) and 24 at once (the pending action 5: mode 0x2f, ov030's own state
  routine, `b_ro_L001`), and between the hits 2 and 22 (`Ov022_StepJumpState`, modes 3 and 4: its `A003`/`A004`).
  A before-function that skips 23, 34 and 24 for an actor (answering 0) keeps the Limit from starting: the actor
  stays in its state, none of the Limit's animations plays (`sdk/examples/CustomLimit`).
- **The enemies' manager** also holds actors with no HP: beside the waves mission's Shadows one of kind byte 67
  (maximum HP 0) stood (+0x60 bit 0x100) and took none of the hits dealt to it; kh.actors leaves them out.
- **A plugin's hit** (`kh.combat`): the players' packet (`Ov022_SendSweepHit`) built with no part node, so that
  `Ov107_CalcHitDamage` takes its damage as it is; given to `Ov107_AiState_ApplyHit` with the player's +0x4ec as its
  source, as the game does. Seen: 241 damage spins took Shadows of 75 and 38 HP to 0 (defeated, rewarded), and a
  Shadow moved by `Ov107_MoveNodeAndRelayout` after a hit answered it had moved.

## 32. Roxas's dual wield and the characters' forms

- **Another character, not another model** (read and seen 2026-10-30): the game plays Roxas's dual wield as
  character 19, whose data folder (`data_02042a70[19]`) is `r2`, a whole folder of its own: `def.p.z`, `def_h.p.z`
  (models), `am.p2`, `ab.p2` (animations), `ci.b.z`, `cm.b.z` (combos and attacks), `li*.p.z` (its Limit),
  `w_.p2`, `wp.b.z` (its two weapons), `lv.b.z`, `ma*`, `it`, `etc`. `Ov022_FillSelectorStates` loads each player's
  overlay by the member's character byte (`gPartyMembers + 0x104 * n + 3`) through `Ov029_AcquireOverlaySlot`; its
  table `data_ov029_020b30b0[20][4]` gives row 19 overlays 49, 68, 87 and 104 (one a player: four can play it at
  once), row 0 (Roxas) ov030 alone; 0x14 and 0x15 are mapped onto 0 and 0xe there, 0x13 is not. Seen in Mission
  Mode with the Zero Gear (item 0xe6) and three Ability Units (item 0xf1) linked on it: the next camp loads
  `ba/ch/r2/def_h.p.z` and `w_.p2`, the next field `def_h.p.z`, `ci.b.z`, `cm.b.z` and `am.p2` from `r2` instead of
  `ro`; the shot shows Roxas hooded with a keyblade in each hand. With two units, `ro`'s.
- **The items** (`db/db.p2` entry 0x15: a count, then 630 records of 0x64 bytes from item 1; `+0` the panel's
  deck category, `+6` its kind, `+8` its shape, signed, -1 one cell; `db/db_<lang>.p2` entry 0x15: a name and a
  description an item, from item 1): the Zero Gear is category 4 (gears), shape 21, kind 240; the Ability Unit
  category 4, one cell (a link, its name marked Ｌ), kind 251, set on a cell of the gear.
- **Where the member becomes 19** (each step seen with `KHDAYS_WATCH` on member 0's record, seat 0's word and the
  camp's record of seat 0, the deck set at the select so that the first camp makes it):
  1. The camp's construction (`Ov008_MainMenuInit`) walks the deck: `Ov008_BuildMenuListFrom(gGameState + 0xee0)`
     sums the panels' records into a stat block (`Ov008_FoldPanelIntoTally`, `Ov008_FoldRecordIntoTally`, at the
     walk's `+0x2c`); its first word not 0 sets game flag 0x2010, else clears it (set with three Ability Units on
     the Zero Gear, clear with two). The camp's Panels can flip it too (`Ov008_GridMenuBack`,
     `Ov008_ToggleOptionFlag`).
  2. It makes the seat's 6-byte camp record (`+2` bit 1 = flag 0x2010, `+4` the seat's character) and
     `Ov008_RecordInputCoords` -- misnamed: it is the seat's camp record, not input -- stores it at the camp's
     context `+0x2e + 6 x seat` and sends it to the room (`Ov008_SendMenuMessage(3)` in a session, else 2).
  3. `Ov008_CommitSelectedPage` -> `Ov008_RefreshSaveSlotWidget(seat)` rewrites the seat's character word
     (`data_020429c8[seat] + 4`) with `Ov008_GetEntryStatusByte(seat)`: the row's character, and 0x13 when it is 0
     (Roxas) and the record's bit 1 is set.
  4. `Ov008_ResetPartyMemberAndLayout` -> `PartyMember_ResetWithKind(0, 0, 0x13, 0)` -> `PartyMember_Reset` writes
     member 0's character 0x13; the field loads overlay 49, whose actor's `+0xc` is 19, its data `ba/ch/r2/`.
  The story's route is another: `Ov002_AdvanceRosterSetup` (a mission script's command, mode 7) writes member 0's
  character 0x13 itself (mode 6: 0x14).
- **The forms** (docs/CHARACTERS.md, "Forms"): the port's join after step 3's `Ov008_GetEntryStatusByte` (prepare.py,
  `Ov008_RefreshSaveSlotWidget`; runtime/characters/forms.c) gives a mod's character's seat the base of the form its
  rules choose, the game's own choice made first (the game's Roxas keeps his). Before it, a Roxas-based mod character
  with the Zero Gear and three units got 0x13 and its data opened `r2`'s folder, not its own: now it plays its
  default form's base unless one of its forms is on 19. A form's data is served as a character's (section 30) from a
  folder of its own, `ba/ch/c100/` on (`Port_CharacterCode`: any number), and `khdays_characters_of(seat, kind)`
  answers the folder of the seat's form whose base is `kind`.
- **Its weapons** (seen 2026-10-30, `KHDAYS_TRACE_MODELS=1`, the forms walk): with the dual-wield Roxas the field
  draws `w_.p2#0/mdl/ro_w13600` and `w_.p2#80/mdl/ro_w14600`, one in each hand; `r2`'s `w_.p2` also holds
  `#40` `bf_w00_trace` and `#160` `ro_w13600_hit`, not drawn as weapons there. Each weapon model's nodes are its
  `_a` and `_b` (`ro_w13600_a`, `ro_w13600_b`), its blade along `z` (0.49-0.85 for `ro_w13600`, 0.65-1.03 for
  `ro_w14600`, in the graphics dump's glTF).
  The port draws a character's or a form's `[weapons]` file for the model of that name in its own folder.
- **In a room** each copy makes every seat's word from the room's records (step 2's record travels; the host's camp
  copies its four members to the guests, `Ov008_CommitSynchronizedSnapshotState`), so a form chosen from a deck,
  which only its player's copy has, would differ between copies: there the seat takes its first form on the base the
  game chose, the same on every copy.
- **The level rows** (`LevelTable_ReadEntry`: `ba/ch/<code>/lv.b.z`, 100 rows of 16 bytes, read through section 30's
  join): `u16` at `+0` strength (`Ov022_ComputeStrengthDamage`, the member's `+8`), `+2` magic (`+0xa`), `+4` defence
  (`Ov022_ComputeDamageTaken`, `+0xc`), `+6` the most HP (`Ov022_ApplyMissionLevelStats`), `+8` not known, a pad, and
  an `int` at `+0xc`, not known (0x5000, 5 in 20.12, at every level of the tables read). Roxas's level 1 is 9, 34, 7,
  62 and 6 (`+0` to `+8`), his level 100 83, 212, 140, 359 and 31. `r2`'s table is the same as `ro`'s.

## 33. The field's prizes

- **They are pictures, not models** (read 2026-10-30 in the decompilation, seen with `KHDAYS_TRACE_PRIZES=1`): the HP
  orbs, munny and items a battle or an object drops are billboard nodes, each one textured quad facing the camera,
  drawn by `Billboard_DrawList` (main@020287e8). A node: +0x00 the next, +0x08 the address of its picture's texture
  words, +0x0c its place (VecFx32), +0x18 its width and +0x1a its height (fx16: the quad goes from -width/2 to width/2
  and from its foot, 0, to its height on the billboard matrix's turn, DS 0x020428e0, after the view matrix at DS
  0x020473e0), +0x1c..+0x1f the texture's corners s0 t0 s1 t1, +0x20 its
  alpha (0..31), +0x22 its polygon id.
- **The pictures** come from `mo/prize.pak.z` (11 BTX0), loaded by `Ov002_LoadShopTable` (ov002@02078ef8) into the
  field stage's table at +0x60, 8 bytes a picture, in the pack's order. Seen in Mission Mode's field (the
  `missions_waves` walk): 0 `ef_pp`, 1 `prize_01`, 2 `prize_03`, 3 `prize_05`, 4 `prize_06`, 5 `prize_07`,
  6 `prize_08`, 7 `prize_10`, 8 `prize_11`, 9 `he_prize`, 10 `born`, their texture words at DS 0x300187dc + 8 x n.
  A node's +0x08 points at one of these entries.
- **The kinds** (`data_ov002_0207e67c`: 16 of {s8 picture, s8, s8, pad, s16, s16}) as `Ov002_CollectNearbySpots`
  (ov002@02078bac) takes them: kind 2 tier 0 is `prize_01`, the HP orb (the HP rose as each was taken: 355 to 358, 358
  to 359 of 359), kind 3 tier 1 is `prize_03`, the munny crystal (the save's munny unchanged during the mission: Mission
  Mode's munny is the results' PLATINES line, +15 there for this mission's drops). A Shadow dropped one of each.
  The table's picture bytes (its data, `ov002_spawn_rows.c`): kind 0 `ef_pp` (the grey crystal), 1 `he_prize`,
  2 `prize_01`, 3 `prize_03`, 6-8 `prize_05`-`prize_07`, 13 `born`, 15 `prize_11`; 9-11 none (0xff).
  `Ov002_BuildSpawnRow` (read in the C) changes two: kind 0 placed at a spot (mode 3) shows `prize_08`, the blue
  crystal, its tier + 10; kind 15 tier 3 shows `prize_10`. Kind 0 is put out only in Mission Mode (`data_0204c240`
  bit 2, `Ov002_SpawnKindIntoFreeSpot`). The node's width (+0x18) and height (+0x1a) come from its speed (tier 0
  0x200, 1 0x380, others and items 0x500): kind 0 twice as wide and four times as tall (its pictures 16 x 32), kind
  1 four times both, the rest twice both. Kinds 0 and 1 make the same pickup sound (`Ov002_PlaySlotActionSound`).
- **The port draws a mod's effect instead** (spec 038, `port/ui/prize_effects.cpp`): with a mod's
  `graphics/mo/prize.pak.z/fx/<picture>.txt` (`port/ui/prize_effects_model.h`, made from KH2's `obj/PRIZE.mdlx` by
  `tools/mods/kh2_prizes.py`) and the settings' "KH2 prizes and chests" on, `Billboard_DrawList` skips the node
  (a join in `port/tools/prepare.py`) and the effect's current frame is drawn at its place on the 3D layer's
  sequences, its body as wide as the picture. Taking it is the game's own: with the switch off the same walk took the
  same kinds, the HP rose the same and the results gave the same PLATINES.
- **The chests** (seen 2026-10-30, the `missions_waves` walk, with `KHDAYS_TRACE_MODELS=2` and the chest replaced by
  its own dumped glTF): the mission's chest is `mo/mo#97`'s `eh_box_b`. Every world's chest model (`mo/mo` entries
  94-99, 101, 102: `<world>_box_b`) has the nodes `dummy0` -> `box2` -> `box1` and `light01`-`light03` (the light
  rays), at rest all at the model's place, 0.982 wide, 0.717 high, 0.783 deep. Opened, `box1` -- the lid -- is drawn
  96 degrees from its rest and `box2` 2; the light rays show as it opens. With KH2's `F_EX040_EH` in its place (the
  KH2 prizes mod: lid `j3` paired with `box1`), the lid turned back on KH2's own hinge as the game opened it.
