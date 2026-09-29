# What the running game does

A map of the game as the native build (`native/`) runs it: which overlays
each scene loads, what the screens' object updates are, what the buttons do
in each menu, and what the data at an address holds. It is for naming and
commenting the decompilation; what its C should say differently is in
[DECOMP_FINDINGS.md](DECOMP_FINDINGS.md).

Only what was seen is here. Each entry says how -- a trace (the environment
variables at the end), the ROM, a save -- and anything not seen is marked
*not identified*. Names are khdays-decomp's at `2a2cd711b`; addresses are
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

Inside the field (scene 2), without a scene change:

- On entering it: ov023 (`ov023_event_script`), ov028, ov022
  (`ov022_battle`), ov107 (`ov107_enemy_common`), ov029 (`system/ov029`),
  ov030 (`ov030_player_roxas`); in the story and with the save also ov069
  (`screens/ov069`), right after ov022. ov024 loads when the field plays a
  movie (day 255's clock tower).
- The pause menu's "Retirarse" in a mission: the battle hub leaves the pause
  (`Ov022_StateAdvanceAfterPause` → `Ov022_EndKeySharingSession`), the field
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
  then ov114 (`ov114_enemy_00`) at `0x020cbf20` -- where ov108 was --
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

- **The game's frame count** is `data_0204c058[2]`, which `Obj_UpdateAll`
  adds one to per unpaused pass. On the title the game runs one frame per two
  VBlanks (VBlank 913 was game frame 430; `KHDAYS_TRACE_INPUT`).
- **KEYINPUT** is read once per game frame (the reads kept pace with the game
  frame count, `KHDAYS_TRACE_INPUT`).
- **`data_0204c190`** holds the keys pressed this frame; X is bit 0x400 (read
  there by `Ov025_PollPageInput` with X pressed, 2026-09-29).
- **ov025's camp menu, X on its scrolling list page:** `Ov025_PollPageInput`
  → `Ov025_MenuInputDispatch_06` (bit 0x400) → the page class's hook at
  +0x28, `Ov025_SetupMenuButtons` → `Ov025_ScrollMenuMoveTo(p, p->nSelectedRow,
  1, 0)`. With the day-357 save the page's list had 22 rows. Which list that
  is: *not identified*.

## 4. Data

| Address | What it holds | Seen with |
|---|---|---|
| `0x0204bda8` | the scene controller (section 1) | every run's frame line |
| `0x0204c058` | the object list: [1] the object being updated, [2] the game's frame count, [3] the first object | `Obj_UpdateAll`'s C; the count against VBlanks in `KHDAYS_TRACE_INPUT` |
| `*0x020b2e7c` | the field's players: 8 entries of 0xc bytes; entry +4 points to a record whose +0x20 is the actor, and the actor's position is at +0x48c (x, y, z, 20.12 fixed point) | `KHDAYS_TRACE_PLAYER`: entry 0 moved as Roxas walked in the Grey Area |
| `0x021cb390`, `0x021cb3dc` | ov025's two tag-tracker tables in the challenge list, 32 nodes each, with an apply at +0x3c and neither getter (+0x48) nor setter (+0x44) | read while the Holomisiones challenge list was up (day 357) |

The save file is a 64 KB raw backup that starts with `KH358DAY` (a player's
day-357 save, which the game loads, and writes to a new slot and reads back).

## 5. How to see it

The native build prints these to stderr when the variable is set:

- `KHDAYS_TRACE_OVERLAYS` -- each overlay as it starts, with the frame and
  the scene ids.
- `KHDAYS_TRACE_UPDATES` -- each object update's change to the next.
- `KHDAYS_TRACE_INPUT` -- each step of a `KHDAYS_INPUT` script, with the
  VBlank, the game's frame count and the pad reads so far.
- `KHDAYS_TRACE_PLAYER` -- the field players' positions every 30 frames.
- `KHDAYS_TRACE_SCRIPT` -- each step of the action-script interpreter.

`KHDAYS_HEADLESS=1` runs without a window; `KHDAYS_INPUT` plays a script of
key presses (`native/runtime/input.c`).
