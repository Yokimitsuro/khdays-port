# Findings for khdays-decomp from the native build

The port's native build (`port/`, branch `native-port`) compiles the
decompilation's C with MSVC for x64 and runs it on a PC -- rewritten at build
time with every pointer a 4-byte reference, the DS address it had, into a
range Windows places (until 2026-10-03 compiled for 32-bit x86,
the DS memory mapped at its real addresses) -- with what the C asks of the
DS's hardware answered by the port's own implementations. The
C matches the ROM byte for byte, yet running it on another CPU shows where the
source does not say what the machine code does. This file collects those
places, and what running the game taught us, for the decompilation.

It lists only what the decompilation at the revision below does not say yet.
Everything earlier versions raised and the decomp took in is gone from here;
the file's git history keeps it.

Every claim cites the ROM (addresses are ARM9; overlay code is given with its
overlay). Names are the decomp's own at the revision below. Sections marked
*generated* are refreshed by `python port/tools/decomp_findings.py`.

<!-- BEGIN generated:revision -->
Checked against khdays-decomp `93535afd7`.
<!-- END generated:revision -->

## 1. Open

### 1.1 Parameters declared narrower than the definition takes them

Found running an optimized build. `Ov022_ActorUpdate` declares
`Ov002_CollectNearbySpots(int nArea, struct Sub *pSub, u8 nId)`; the
definition takes `u32 nPlayerIndex` and hands it on, through
`func_ov022_020886f8`, to `GetEntryField20ByIndex` as a table index. mwcc
converts the argument to `u8` at the call and passes it extended in its
register, so the ROM is right. A compiler that defines only the narrow part
of the argument -- MSVC optimizing, on the stack, leaves the upper 24 bits as
they were -- turns the index into garbage, and the game crashed on the first
enemy near Roxas. Declarations that match the definitions, as the narrow
return types now do, would say what the ROM does; the port converts such an
argument itself and passes it as an `int`.

The decomp has widened most of them (520 files at `2a2cd711b`, another 153
since) and keeps some narrow on purpose, with a comment (a wide declaration
changes mwcc's code). The 23 functions left below are those and whatever
else still declares a narrower parameter.

*Generated:* each function, the parameters some file declares narrower than
its definition, and the files that do.

<!-- BEGIN generated:narrowparams -->
- `ArrayEntryPtrD0` (argument 0 as u16): `Ov023_CmdRampAnimFrame`
- `EntityMgr_LinkChild` (argument 0 as u16, argument 1 as u16): `Ov023_CmdAttachWeapons`, `Ov023_Cmd_ParentEntityToEntity`
- `EntityMgr_RayCastWithParams` (argument 0 as u16): `Ov002_UpdateSpawnedSpots`
- `EntityMgr_SphereCastWithParams` (argument 0 as u16): `Ov002_UpdateSpawnedSpots`
- `Entity_SetVisible` (argument 0 as u16): `Ov023_Cmd_ParentEntityToEntity`
- `Entity_SubmitRenderNode` (argument 0 as u16, argument 1 as u16): `Ov023_CmdPlaceActor`, `Ov023_CmdPlaceRelative`, `Ov023_CmdSeatMembers`
- `GX_SetGraphicsMode` (argument 0 as u16): `Gfx_RestoreAfterPause`, `Ov024_MobiClip_SetUpMainEngine`
- `GetTrackEntryBase` (argument 0 as u16): `Ov023_CmdWarpActor`
- `LoadArrayU8At0ce` (argument 0 as u16): `Ov023_CmdAttachWeapons`
- `NNS_G3dMdlSetMdlDiffAll` (argument 1 as u16): `Ov255_DrawShakes`
- `Ov002_PlotCanvasPixel` (argument 1 as u16, argument 2 as u16, argument 3 as u8): `Ov002_BlitMask`
- `Ov002_SpawnKindIntoFreeSpot` (argument 1 as u16): `Ov002_SpawnTieredDrop`
- `Ov008_ClearGridSlot` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov008_PlaceTrackedNode`
- `Ov008_FindEntryByTag` (argument 1 as u16): `Ov008_RetargetCellByTag`
- `Ov008_StampTileMode` (argument 2 as u8): `Ov008_ShowTierPage`
- `Ov025_ApplyTempFieldsByTagB` (argument 2 as s16, argument 3 as s16): `Ov025_ScrollList_DrawRow`
- `Ov025_ClearGridSlot` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov025_PlaceTrackedNode`
- `Ov025_FindEntryByTag` (argument 1 as u16): `Ov025_RetargetCellByTag`
- `Ov026_FillTilemapRegionPalette` (argument 2 as u8): `Ov026_ShowTierPage`
- `Ov107_InvokeHitCallback` (argument 3 as u8): `Ov208_ContactSweep`, `Ov209_ContactSweep`, `Ov212_AreaAttackSweep`, `Ov214_ProcessHitTargets`, `Ov215_ProcessHitTargets`, `Ov216_ProcessHitTargets`, `Ov217_ProcessHitTargets`, `Ov218_ShotBurstTick`, `Ov219_AttackSweep`, `Ov220_AttackSweep`, `Ov221_StrikeSweepEntities`, `Ov222_StrikeSweepEntities`, `Ov223_StrikeSweepEntities`, `Ov224_StrikeSweepEntities`, `Ov225_StrikeSweepEntities`, `Ov226_StrikeSweepEntities`, `Ov227_AttackSweep`, `Ov228_ContactSweep`, `Ov229_ContactSweep`, `Ov230_ContactSweep`, `Ov231_ProbeSpawnPoint`, `Ov232_ProbeSpawnPoint`, `Ov233_ContactSweep`, `Ov237_AttackHitTest`, `Ov239_ContactSweep`, `Ov240_ContactSweep`, `Ov248_ContactSweep`, `Ov249_ContactSweep`, `Ov252_ReboundHitTest`, `Ov254_ReboundHitTest`, `Ov256_AttackHitTest`, `Ov260_AttackSweep`, `Ov263_ProbeSpawnPoint`, `Ov264_ProcessHitTargets`, `Ov265_ProbeSpawnPoint`, `Ov266_AreaAttackSweep`, `Ov267_AreaAttackSweep`, `Ov268_ContactSweep`, `Ov276_ContactSweep`, `Ov280_ProbeSpawnPoint`
- `PMi_SendSleepStart` (argument 0 as u16, argument 1 as u16): `PM_GoSleepMode`
- `StoreValueInNamedEntry` (argument 0 as u16): `Ov002_LoadPeerIntoSlot`
- `TailForwardTrackEntry` (argument 0 as u16): `Ov023_CmdSetupPartyActors`
<!-- END generated:narrowparams -->

### 1.2 What runs natively on it

Mission Mode's whole loop (camp menu, mission, Retirarse, results, camp menu)
and Story Mode to the second day (title, opening movie, day 255, the
clock-tower cutscene, the monologue, the next day's card, the field). With a
player's own save (day 357): loading, the main menu and its submenus, saving
to a new slot and reading it back, the Holomisiones and challenge lists,
challenge 07 in play (Axel's intro, the mission's HUD, combat), the Moogle
shop, a Halloween Town mission's fights and Fire, and a reset back to the
title. At this revision both the Debug and the optimized (Release) build run the
regression walks (Story Mode to the field, a Mission Mode mission in play
and its Retirarse, the player's save into challenge 07).

Routines the decomp keeps as assembly run as C the port wrote from them
(`port/hal`, each citing its source): the MTX routines, the BIOS and OS
stand-ins, and since 2026-10-01 DGT's SHA-1 block transform
(`DGTi_Hash2ProcessBlock`, `libs/nitro/dgt/asm_stubs/auto/`): the same
state as the ROM's routine, interpreted, on the six calls loading the
player's save (two of 7,296 bytes, four of 64), and FIPS 180-1's
SHA-1("abc"). It is SHA-1's plain compression, except that it hashes one
block even for a length of 0 (it checks the length after the block).
`Util_QuickSortWithWork` (NitroSDK's MATH_QSort,
`libs/nitro/math/asm_stubs/auto/`, which ov003's `Ov003_RankPlayers` calls)
is C since 2026-10-02 (`port/hal/sdk_util.c`): 1,952 sorts -- element
widths 1, 2, 3, 4, 5, 8, 12 and 16 bytes, 0 to 300 elements, compared by a
signed first byte so that ties show the algorithm's order, one run in four
with many ties -- each the same, byte for byte, as the ROM's routine,
interpreted, on a copy of the array, and in order. It is a quicksort on a
stack of ranges, not recursive; its comparisons of addresses are signed,
as the routine's branches are. With it, no routine the game reaches is left
as the ROM's bytes, and the port's ARM interpreter is gone (RUNTIME_MAP
section 5). What is left is MobiClip's code the game never reaches: the
audio decoders of kinds 1 and 2 (`Ov024_MobiClip_AudioTransformDecode`,
`func_ov024_02087318_unk` -- `Ov024_MobiClip_StepAudio` picks them by the
stream's kind, and all 37 movies with sound are kind 3) and the deblocking
filter (`func_ov024_02092e60_unk` -- `Ov024_MobiClip_BlitFrame` runs it for
a mode other than 0, and the one call reaching it, from
`Ov024_MobiClip_FrameAlarm`, passes 0).

The movies' frame decoder is the decomp's portable reconstruction
(`libs/mobiclip/video/portable`), called where `Ov024_MobiClip_DecodeFrame`
calls the payload, and `Ov024_MobiClip_BlitRows` is C written from its
assembly (`port/hal/sdk_mobiclip.c`): every frame of the 46 movies (37,185)
the same as the ROM's payload and blit give (RUNTIME_MAP section 9).

### 1.3 Seen, not identified yet

- `mobiclip_frame_core.h` says the payload keeps no state between frames
  beyond the quantizer, the format bit, the coefficient table and the
  quant/scan tables, the rest being the frame's scratch. The payload run from
  the state the portable decoder left after a frame (its scratch, not the
  payload's) decodes the next one wrongly -- `802`'s fifth frame: 2,258 bytes
  used against 194 -- while each decoder on its own agrees with the other on
  all 37,185 frames of the 46 movies. So the payload reads some of that
  "scratch" from the frame before. Which field is *not identified*
  (2026-10-02, `KHDAYS_VIDEO_CHECK` while it ran the payload second).

- Skipping the story's first movie with Start (the story walk's script, Debug
  build, 2026-09-30) sometimes ends `Ov024_MobiClip_UpdatePlayback` with ESP
  not where its frame left it: the run-time check at its epilogue (`cmp
  ebp,esp` after its locals are freed), reached from `Obj_UpdateAll`. Two of
  four runs with the native sound driver, none of two with the interpreted
  one -- the drivers answer the same, so it is the moment the press lands:
  only the skip path (`wFlags & 4`) runs the `Ov024_MobiClip_PlayerPollAbort`
  wait on `OS_WaitVBlankIntr` and `Table_TailCallWithEntry(0, 0x14)`
  (`NNS_SndArcStrmStop` with a fade). Every function it calls directly is C
  with its own frame check, so the one that moved ESP or EBP is below the C
  -- a thread switch, an injected interrupt, or the interpreted MobiClip
  decoder the stop destroys -- *not identified*. The game goes on, and the
  walk reaches its scenes.
  **It is the port's, not the C's** (2026-10-02, Debug, the
  interpreter long gone): with the port's interrupt watcher on (an
  interrupt injected between two of the game's instructions by suspending
  its thread, `port/runtime/async_irq.c`), 2 of 10 story walks had the
  check at that epilogue; with the watcher off and the movie's loops waiting
  natively for their interrupts, 0 of 20 story walks and 0 of 20 play walks.
  The watcher is off by default since. What in the injection moves ESP is
  *not identified*.
- The same check, once in eight walks, at the end of `CARDi_ReadCard` on
  the card's task thread (`CARDi_TaskThread`, `CARDi_ReadRomSyncCore`), in
  the play script's field load. Both functions spin in the game's code --
  one on the card's registers, one on `Ov024_MobiClip_PlayerPollAbort` --
  where the port's watcher may deliver an interrupt between any two
  instructions (`port/runtime/async_irq.c`): a lead, *not identified*.
  Since the port answers the game's ROM reads itself (RUNTIME_MAP §8),
  `CARDi_ReadCard` no longer runs unless `KHDAYS_FILES=card`.

### 1.4 Names that say something else

What this section reported, the decomp took up on 2026-10-08 and 2026-11-05 (its
`docs/renames/2026-10-08-findings-names.tsv` and `2026-11-05-*.tsv`, checked against each body);
the port follows its names since. Left open:

- DS Protect's functions in ov028 are listed in
  `config/arm9/overlays/ov028/symbols.txt` as `kind:data(any)` --
  `Ov028_DSProt_ROMUtil_Read` (its ROM read, `dsprot_rom_util.c`) among them
  -- because their bodies are encrypted in the ROM; the C defines them as
  functions, several to a file. A tool that goes by the symbols' kinds finds
  no function there (the port's build hooks the file instead,
  `port/tools/prepare.py` FILE_HOOKS). Found 2026-10-01.
- The game state's +0xc, which `Ov008_CanConfirmShopRow` and
  `Ov008_Shop_DrawRow` declare `nMunny`, holds the heart points: the camp's
  bottom screen shows +8 as PLATINES (157,458 on the maintainer's save) and
  +0xc as PUNTOS CORAZÓN (46,509). Same finding.
- The missions' list (`UI/cm/msl_&.msi.z`, `docs/RUNTIME_MAP.md` 29), found for the port's new missions,
  2026-10-24: `Ov008_AnyEntryFlagged` decides between the list of rows and the tutorial's alone.
- The characters (2026-10-28):
  - `Ov022_RunContactMode0`'s `ScaledFanSlot.nFace` (+0x00) is the hit's damage factor (fx32, given to
    `Ov022_ComputeStrengthDamage`), not a face; with `Ov022_UnpackChannelGrid`'s `Channel` it is `nA`, whose
    `nB`/`nC` are the first and last frame, `nD`/`nE` the push out and up, and the mode-0 tail's `nA`-`nC` the
    hit's place, `nD` its radius, `nE` its spread's cosine (RUNTIME_MAP section 30, "Its attacks").
  - `Ov022_BuildNodeGraph`'s graphs are a keyblade's combos (graph 0 the ground one, 1 the air one): `nValue`
    an attack's number in `cm.b`, `pLeft` the next attack, the pools the chain, its finisher and the branches.
- The field's prizes (2026-10-30; `docs/RUNTIME_MAP.md` section 33):
  - `Ov002_LoadShopTable` (ov002@02078ef8) loads no shop: it reads `mo/prize.pak.z` (11 BTX0) and keeps each
    picture's texture words in the field stage's table at +0x60 (8 bytes a picture, the pack's order).
  - `data_ov002_0207e67c` is 16 prize kinds of 8 bytes: {s8 picture (an index into that table), s8, s8, pad,
    s16, s16}.
  - A billboard node (`Billboard_DrawList`'s list) holds at +0x08 the address of its picture's texture words in
    that table, and +0x18..+0x22 the quad: s16 width, s16 height, the texture corners s0 t0 s1 t1 (bytes), the
    alpha (0..31) and the polygon id.

- The mission gauge (2026-11-01; `docs/RUNTIME_MAP.md` section 18, "The mission gauge"):
  - `Ov002_ResolveFourDescriptors` (script command 2.34) sets the mission gauge: its four operands are the
    gauge's kind, total, goal and flags, handed to `Ov002_SetScriptRequest`, which keeps them at the field root's
    +0x8c94 -- a "set mission gauge" pair (kind 1 targets defeated, 2 hearts, 4 a counter).
  - `Ov002_ScriptCmd_SetEventParams` (2.73) and `Ov002_Event_SetParams` track an enemy class for the gauge: the
    object context's +0xa is the class (`Ov002_OnPartActorRetired` names it `nTrackedRowId` and compares it with
    the enemy row's byte 0, `bId`, which is the row's enemy class, not a row index), +0xc the defeats counted
    (`nRetiredCount`), +0xe the second operand.
  - `Ov002_ScriptOpInvokeSlot0` (2.38) adds to the mission tally: `Ov002_AddMissionTally(0, kind, amount)`, its
    first operand read and left unused.
  - `Ov002_SubmitRequestBlockKind1` (2.7) files a "defeated" watch, not a request: its third operand is the check
    `Ov002_NodeRunCheckPhase` runs (0 one group, 1 every group), its fourth the group (single-player in the low
    half, multiplayer in the high one). `Ov002_AllocScriptCallback` makes that node; it allocates no callback.
  - `Ov002_TriggerOnceOnFlag`, called by `Ov002_StepProgressBar` when the gauge's done reaches its goal, raises
    state 8326, the mission's goal reached.
  - The spawner slot's `mask` (`Ov107_Spawner_AddSlot`, from 2.32's third operand) is the enemy's status byte
    +0x179 (`Ov107_SetStatusAndEmit`), not a mask.
  - `Ov022_SetPlayerScale(seat, value)` posts a defeated enemy's hearts (`Ov107_AiState_OnDefeat` hands it the
    enemy's +0x19f): tally 2, the hearts in the high half and a bonus by the player's step in the low one; it scales
    no player. The enemy's `field_19f` and its spawn record's `field_5d` (`Ov107_LoadSpawnRecord`, byte 0x6d of the
    class's Ms/DP.p2f record) are the hearts it gives.
  - `Ov002_FitsBudget` (a position check's kind 0, installed by `Ov002_CreateTaskNodePath`) tests whether a
    player is within the node's radius (+0x20) of its point (+0x2c); there is no budget. Script command 2.8,
    `Ov002_ScriptSubmitPathTask`, files the check (gate, event, mode, room, the place, kind, radius, height), so
    "path task" is a position check; its node's `bOwner` (+0x18) is the players' mode (`nMode`: 0 the first, 1 all).
  - `Ov021_VmCmd0e30` (17.2) makes the Organization emblems' class (`Ov021_CreateEmblemClass`) and
    `Ov021_MarshalFxAndDispatch2` (17.3) puts one out (`Ov021_EmblemCreate`); `Ov021_ResetChildrenScoreExceptSelf`
    restarts the other emblems' timers, which decide their points (10 or 8), not a score.
  - `Ov107_EnemyTableHasEntry(class, column)` and `data_ov107_020cb6a4` (6 bytes a class) say which of the six
    enemy code columns a class's overlay is built for (overlay 114 + the byte, 0xff none); `Ov107_LoadEnemyOverlay`
    returns how many columns the overlay takes, `data_ov107_020cbb04` is the next free column.

Taken up (for the record of what the old names were):

- `CARDi_ReadRomSyncCore` (`libs/nitro/card/calls/`) reached the card's
  common state by byte offsets where its siblings declare `CARDiCommon`; it
  declares it now.
- The wireless layer (found 2026-10-05). The
  functions named `*EndKeySharing*` / `*CloseKeySharing*` ended no key
  sharing: each destroys a scene's task object through the veneer to
  `Obj_Destroy` (now `VeneerTo_Obj_Destroy`), e.g. `Ov002_DestroyPartyObject`,
  `Ov022_StateCloseOv106Scene`, `Ov008_ReleaseMissionLobby`; Days'
  multiplayer shares state by messages on WM port 12 (`AsyncMessage_Flush`),
  not keys. `func_01ff8128` / `func_01ff8138` are `WH_GetCurrentAid` /
  `WH_GetBitmap` (`wh.c`'s `sMyAid`, `sConnectBitmap`). Among ov105's
  functions the game calls, the old names said other things than the `wh.c`
  and WM functions they are: `Ov105_GetWord24` (`Ov105_WH_GetSystemState`),
  `Ov105_GetState` (`_WH_GetLastError`), `Ov105_GetStatusLow`
  (`_WH_GetDisconnectReason`), `Ov105_SetParamWord8` (`_WH_SetGgid`),
  `Ov105_SetBuffer` (`_WH_SetUserGameInfo`), `Ov105_SetPacketFilter`
  (`_WH_SetJudgeAcceptFunc`), `Ov105_EnterState1AndResolveId`
  (`_WH_GetMeasureChannel`), `Ov105_KickIdleHandler` (`_WH_Reset`),
  `Ov105_RunScriptedStepState3` (`_WH_End`), `Ov105_SetSlotEventHandler`
  (`_WM_SetPortCallback`), `Ov105_ResourceEntryCallback`
  (`_WM_StartParent`), `Ov105_GetTransitionFrame` (`_WM_GetAllowedChannel`),
  `func_ov105_020bf240` (`_WH_GetLinkLevel`), `func_ov105_020bf900`
  (`_WH_SendData`), `Ov105_GetSessionChannel` / `Ov105_GetSessionLinkState`
  (`_WM_GetAID` / `_WM_GetConnectedAIDs`).
- `include/nitro/wm.h`'s `WMPortSendCallback` put the data pointer at
  +0x10; the engine reads it at +0x14 (`AsyncMessage_OnSendDone`, before
  `ClearNodeIfHeadMatches`). `wm.h` has it at +0x14 now.
- `main` handed `Record_EnsureAllocatedAndSetId` the boot profile's word
  commented "SetGameMode", declared with two parameters where its definition
  took one; the value is the game's language, stored in `gLangPath`'s first
  halfword (`GetLanguage`, `Msg_BuildLangPath`'s `&`, 1..5 the firmware's
  English .. Spanish). It is `Msg_SetLanguage(int language, int unused)` now,
  declared as defined. Found 2026-10-07 (`docs/RUNTIME_MAP.md` 8.1).
- What the decomp took up on 2026-11-05: the camp's screen 1 is the holo-missions' world map
  (`Ov008_MainMenu_*`, `Ov025_MainMenu_*` are `*_WorldMap_*`); the Preferences page's tables
  (`data_ov008_02090598`, `data_ov025_020b4f64` are `gOv008ConfigSettings`, `gOv025ConfigSettings`, with
  their `...Lists` and `...Pages`), `Ov008_ArmUnseenMarkers` (`Ov008_Config_EnableModeSettings`) and
  `*_SelectTierIfActive` (`*_Config_GetTabTitleRecord`); Mission Mode's list row test
  (`*_IsEncounterEligible`, `*_IsMissionModeRowListed`) and its filters (`Ov008_RecordFilter_KindZero`,
  `_Kind4Id10`: `_StoryRow`, `_TutorialRow`); `data_02046334` and `func_0200afb8` (`fsi_arc_rom`,
  `FSi_InitRom`); an enemy's level, home and attack power (`Ov002_GetEffectiveHeight`,
  `Ov107_StoreHitInfo`, `Ov107_StoreSlotEvent`: `Ov002_GetMarkerEnemyLevel`, `Ov107_SetHomeAndReach`,
  `Ov107_SetAttackPower`), its HP at +0x218 (`hp`), the magic pool's 15 slots; the Shadow's height
  scale (`Ov114_FadeInSubObject`, `Ov114_EasePursuitSpeed`, `Ov114_AiFadeOutTick`, `Ov114_AiFadeInTick`:
  `Ov114_SinkIntoGround`, `Ov114_RiseFromGround`, `Ov114_AiSinkTick`, `Ov114_AiRiseTick`); the panels'
  record +4 (`a4`: `nIconId`); the field's spawners (`Ov002_ScriptCreateSlotObject`,
  `Ov002_CreateSlotObjectAndStart`, `Ov002_StartSlotAnim`: `Ov002_ScriptCreateSpawner`,
  `Ov002_CreateSpawner`, `Ov002_StartSpawnerTrigger`, their operands as proposed), the marker row's
  nibble (`nMaxOut`), `func_ov107_020c0ea0` (`Ov107_Spawner_Tick`), a node's state bits
  (`Ov107_SetBit0OfField0x40`, `Ov107_SetBit1OfField0x40`: `Ov107_Node_SetShown`, `_SetRunning`) and a
  chest's room (`nBucket`: `nRoom`); the characters (`NNSG3dRenderObj`'s +0x08..+0x20 named;
  `*_MenuEntryToSlot`, `*_MenuSlotToEntry`: `*_SelectPlaceToCharacter`, `*_CharacterToSelectPlace`;
  `data_ov008_0208e9c4`: `gOv008CampCharacterRecords`; `Ov008_Menu_InitSceneObject`'s character;
  `Ov022_PlayEntityVoice(pActor, nSeqArc, nSeq)` with the actor's `nSeat` and `nCharacter`;
  `Ov002_GetPanelWord0220`: `Ov002_SetSeatHpGauge`; `Ov022_ScaleRowValues`: `Ov022_ScaleMoveDamage`;
  `Ov008_RecordInputCoords`, `Ov008_RefreshSaveSlotWidget`, `Ov008_GetEntryStatusByte`:
  `Ov008_StoreSeatCampRecord`, `Ov008_RefreshSeatCharacter`, `Ov008_GetSeatCharacter`); the mission's
  level (`Ov002_GetRootField8bae`: `Ov002_GetMissionLevel`), the event skip (`SetWordAt0x588To1`,
  `Ov023_FlushTextBox`: `ActionScript_RequestSkip`, `Ov023_RequestEventSkip`), an event script's name
  (`Ov002_FormatResultLine`: `Ov002_FormatEventScriptName`) and the game's heaps (`data_0204c024`,
  `data_0204c02c`, `data_0204c044`, `data_0204c030`, `func_ov001_0204cf5c`: `gMainHeap`, `gSubHeap`,
  their handles `gMainHeapHandle`, `gSubHeapHandle`, `Ov001_InitGameHeaps`).
- Later on 2026-11-05 (`-4.tsv`, `-5.tsv`): the Shadow's sink and rise in its copies (ov244, ov277: `*_AiEnterFadeIn`,
  `*_AiEnterFadeOut`, `*_FadeInSubObject`, `*_EasePursuitSpeed`, `*_AiFadeInTick`, `*_AiFadeOutTick`: `*_AiEnterSink`,
  `*_AiEnterSinkAndStay`, `*_SinkIntoGround`, `*_RiseFromGround`, `*_AiRiseTick`, `*_AiSinkTick`) and the Mega and
  Gigas Shadows' height scale (ov181-184, ov250, ov251: `*_AiRiseTick`, `*_AiRiseSlowTick`, `*_AiSinkTick`); the
  seat's HP gauge (`Ov002_GetPanelWord0220Alt`, `_Idx`: `Ov002_ShowSeatHpGauge`, `Ov002_GetSeatHpGauge`);
  `Ov008_SetupItemMenu` (`Ov008_Config_Setup`); `data_0204c23c` (`gMissionNumber`); a mission's badges
  (`*_BuildRankMask`, `*_BuildRankMask2`: `*_BuildBadgeMask`, `*_BuildBadgeMask2`; `FIELD_MISSION_REWARDS`:
  `FIELD_MISSION_BADGES`, with `FIELD_MISSION_STATUS`, `_CROWNS`, `_MODE_RANK`); the missions' list record and
  node (`Ov008MissionListRecord`, `Ov008MissionListEntry`'s `nCategory`, `nShownNumber`; `nKind`, which
  `Ov008_CalcMissionCompletionPercent` tests, and `Ov002_SpawnStateSpots`' badge bits); the enemy's hooks
  (`func_ov107_020c6624`: `Ov107_AiState_Init`; `Ov244_RelaySlotEvent`, `Ov277_RelaySlotEvent`: `*_SetAttackPower`).

### 1.5 What agrees with the ROM only in the DS's frame or names

Locals that were one buffer only in the DS's frame, taken up by the decomp
on 2026-10-08: the tailbunker family's joint callback
(`Ov221`-`Ov227_SyncPartNodesToJoint`) and the blade family's effect callback (`Ov228`,
`Ov229`, `Ov230`, `Ov233`, `Ov248`, `Ov249_SyncEffectNodesToJoints`) asked
`NNS_G3dGetCurrentMtx` / `NNS_G3dGetResultMtx` for the whole 4x3 matrix
(0x30 bytes) into a 0x24-byte local and read its translation from a second
one, the ROM's one 0x30-byte frame; natively the call wrote 12 bytes past
the first, over MSVC's stack cookie, and the process ended entering a room
with such an enemy (2026-10-05, `Ov222_SyncPartNodesToJoint`) and the second
area of mission 72 (2026-10-07, `Ov228_SyncEffectNodesToJoints`). Their C
has one `MtxFx43` now; the port's fixes for them are gone.

A linker constant declared as a function:
`libs/nitro/os/calls/OSi_InitStackChecker.c` declares
`extern void SDK_IRQ_STACKSIZE(void);` and uses its address, 0x800, as a
size.

And a name that is Windows' too: NitroSystem's `CreateThread`
(`libs/nns/snd/calls/CreateThread.c`) has the name of Windows'
`CreateThread`, and on x64, where a C function's symbol is its bare name,
the two collide (the port renames the game's).

What the C assumes of a 4 MB DS, met giving the game's heaps more room than
the DS's main RAM (2026-10-11):

- Everything the game allocates comes from the two heaps
  `Ov001_CreateMainAndSubHeaps` (ov001 0x0204ce40) makes from the main arena:
  `MAIN_HEAP_SIZE` (0xc8000) from its low end, then the whole rest up to
  `HW_MAIN_MEM_MAIN_END` (0x023e0000) -- half of it under DS Protect's
  emulator or flashcart answer. Nothing else in `src/` or `libs/` takes from
  `OS_ARENA_MAIN`, and nothing else creates a heap (`HeapState_Recreate`
  remakes one of the two over its own region). Measured in the port with the
  game's data: the main heap is 800 KB with never more than 33 KB free; the
  sub heap 2.25 MB.
- The heaps' handles and sizes are typed as what they are since 2026-11-05 (`GameHeap`,
  `include/game/heap.h`; `FontResource_Destroy` returns a `u32`).
- `MIi_CheckDma0SourceAddress` (main 0x02004084) panics for a channel-0
  source at or above 0x08000000 -- the DS's cartridge bus, which DMA channel
  0 cannot read; a fact of the DS's buses, not of the data. The port lets a
  source in its own memory pass (its DMA is a copy).
- A file handle names a packed archive's header by 24 bits from 0x01ff8000; since 2026-11-05 one
  helper makes it and two read it (`ARCHIVE_HANDLE`, `ARCHIVE_HANDLE_HEADER`, `ARCHIVE_HANDLE_INDEX`,
  `include/game/archive.h`), where the 16 MB bound of the DS's main RAM window lives.

- Group 3 of the script machine's tables is the movie player's (`Ov024_MobiClip_OpenPlayer`, ov012's
  opening): the missions' `_m` sections are the movies' subtitle schedules. Found 2026-10-17
  (`docs/RUNTIME_MAP.md` 21).

## 2. Known, and kept as they are

What remains cannot be written in C without changing the ROM's code, as the
decomp has said: the `void` functions whose r0 a caller uses
(`Ov107_RegisterChildInRegion`, `Ov107_RemoveChildFromRegion`), the fifth,
stack argument of `Ov107_Actor_SetAttachSlot` and the variadic call in
`Ov226`, the NitroSDK functions the SDK itself declares `void`, and the calls
that pass fewer arguments than their callee's definition takes. The port
handles these itself, and at this revision nothing is left where it would
stop:

- Of the 25 values some declaration takes from a `void` definition, no C
  reads 23 (the call is a statement of its own, the value is handed to a
  function that takes no such argument, or it is returned where nothing reads
  it). The other two, `Ov107_ProcessObjectTick`'s and `SetSubitemState`'s,
  are returned by hooks at +0xc and +0x1dc, and every `blx` of those hooks in
  the ROM sets r0 next or returns it unread.
- `Ov107_Actor_SetAttachSlot`'s fifth argument is the outgoing slot its five
  callers fill themselves; the port passes that. `Ov226`'s call is the same
  on x86 -- cdecl stacks its two `VecFx32` in the words
  `Ov226_Projectile_SetupFlight` reads -- and on x64 as the port passes it,
  in a block as the DS laid it out, as it passes the variadic calls
  (`references_block_abi.txt` beside its generated sources).
- 150 files add `(x - x)` with `x` a local never set (`RandNextScaled(n) +
  (v - v)`, the lever for mwcc's register copy). The sum is the same
  whatever `x` holds, but reading an unset local is undefined behaviour,
  which an optimizing compiler may act on; the port starts those locals at 0.
  Debug builds report each one as it runs (`Ov114_StartSidestep`,
  `Ov118_PickRandomSignedSpeed`, `Ov286_Chase_DecideAttack` so far).
- Calls through pointers pass what the C gives them, often fewer arguments
  than the function reached takes (the ARM leaves the registers as they
  were). The port cannot pad those, so the functions whose address is taken
  are compiled without optimization: unoptimized, a function writes a
  parameter's slot only where its C assigns the parameter. Casting Fire,
  the optimized build returned to address 0 with the saved registers
  zeroed, where the unoptimized one ran on; with these functions
  unoptimized the optimized build casts it too.
- `ModelAnimSet_Bind` returns `texSrc` in r0, which `Resource_BindFileToSlot`
  returns (the decomp's comment says why the C cannot); the port's definition
  returns it. Where the ROM passes a caller's r4 that the C leaves unset
  (`input` in `Ov006_MissionBuildOptionRows`, `nValue` in
  `Ov008_MissionMenuWaitReady`, `direction` and `action` in ov005's
  handlers, all commented in the decomp), the port passes 0, which those
  functions take the same way; for `nCell` in `Ov002_MapTouchToCell` it
  passes 0x1d, what the ROM's r4 there (the cue context's address,
  `Ov002_HandlePageTouch` 0x02065be4) clamps to.
- `Ov291_TurnTick` (Pete, 0x020ccab4) passes r8 as its callers left it when
  no actor is clear of flag 0x10000, as the decomp says, and
  `func_ov022_02086834` writes through it. `ObjList_Update` and
  `Ov107_ContainerNode_Tick` leave r8 alone; the tick is reached through
  the node pointer `Ov107_InitContainerNode` sets, so which caller's r8 it
  is is *not identified* -- a trace in Pete's fight would say. Until then
  the port runs the C as it is.
- `Ov001_SeedMathRandContexts` mixes the entropy into accumulators it never
  sets, as the decomp says: r4 (`r`) and r6:r5 (`x`, low:high). The functions
  called before it keep those registers, so they hold `main`'s: r4 is 1
  (`ldr r4,=1` at 0x02000be0, the overlay `FS_LoadOverlay` loads), and r5
  and r6 are what crt0's autoload (`func_020009fc`) left, the last block's
  destination and its end with BSS -- the DTCM block, 0x027e0000 and
  0x027e0e60 (the table at 0x0204c1e0). The port starts `r` at 1 and `x` at
  0x027e0000027e0e60. Debug builds reported the unset read at every boot.
- Every short direct call (758, in 631 places, at this revision) passes zeros for the words
  the definition takes and it does not. Natively those slots (on x86 all of
  them, on x64 those past the four argument registers) are the caller's frame,
  and an optimizing compiler keeps other values in a parameter's slot once the
  parameter is dead: MSVC's Release build crashed in Ov025's camp menu on
  exactly that, when `Ov025_ScrollMenuMoveTo`'s menu pointer came back
  changed from a callee's callee.
  Three script ops were not zeros on the DS: `Ov019_StoreSlotIndex`,
  `Ov019_RecordStatHighWater` and `RegisterActorOrRetry` reach
  `ScriptVm_ReadOperandInt` with r0/r1 as the interpreter passed them (the VM
  state and the operand pointer); zeros there ended the process in a
  mission's script (2026-10-05). Their C passes both since 2026-10-08.
  Of the padded calls whose callee reads a word the call does not pass (323
  at the 2026-10-05 revision), a linear scan of each caller's ROM code up to the call
  finds 58 more that reach it with that register as they came; in each the
  callee only hands that word on to a function that does not take it
  (`func_ov022_020ad44c`'s third and fourth to `GetEffectAnchor`'s two, the
  page getters' and `CARD_TryWaitRomAsync`'s none, `Archive_LoadFile`'s two)
  -- read by hand.
- A bare `return;` in a function with a value gives the native caller what
  happens to be in the register; the ROM gives its r0 on that path, and some
  callers read it. Of the decomp's 15 (2026-10-05, a scan for them), those
  read: `Ov022_StartPauseMenu`, an object's update, returns r0 as it came
  while the lock byte is set -- its own address, as `Obj_UpdateAll` calls
  each update (`ldr r0,[r1,#0x14]; blx r0`, 0x02023b60), so it stays and
  tries again; natively it returned 1 and the port stopped in mission 61 (the
  maintainer). `Ov022_TestShotHit`'s early exit leaves the damage
  `Ov022_ComputeStrengthDamage` gave, or `Ov022_ScaleMoveDamage`'s 0 (`mov
  r0,#0`, 0x020a2410), read as the hit; the four menu `Clamp...AndNotify`
  (Ov025, Ov008: the same bytes) the first argument. The port returns those
  values (prepare.py `ABI_FIXES`). `Ov022_ResolveGuardBreakState` (every
  path `Ov022_ActorSetState`'s result) and `Ov181`-`Ov184_OnHit` (the actor
  on the sub-state 10 request) return theirs in the decomp's C since
  2026-10-08. The others' callers drop the value
  (`Ov002_ResolveCameraAgainstCollision`, `Ov002_SendTallyUpdate`), or it
  needs a malformed subtitle (the two `MobiClip_SubtitleNextChar`).
- `Ov002_CreateAndRestoreHud` (`src/overlays/scenes/ov002_field/`) calls
  `Ov002_IsPanelModeSet()` with nothing, and its comment says a panel is
  always installed by then. The ROM's `Ov002_IsPanelModeSet` returns r0 as it
  came when there is none (`cmp r1,#0; bxeq lr`, 0x02061b8c) -- here what
  `Ov002_RepublishHud` left -- and there was none in mission 61 (2026-10-05,
  the maintainer's run stopped at the port's check for it). It does not
  matter: the branch it guards sums the tally rows (the count,
  `func_ov022_020882f8`, only reads) and calls `Ov002_AddToPanelTotal`, which
  returns at once without a panel. The port passes 0 (prepare.py
  `ABI_FIXES`).
The lists below are what the port rewrites and where it would stop, kept for
reference and to show when a new revision changes them.

### 2.1 Rewritten by the port (*generated*)

<!-- BEGIN generated:repairs -->
```
NNSi_FndFreeFromDefaultHeap returns the result of NNS_FndFreeToExpHeap
OSi_FreeStackAlloc returns the result of OS_FreeToHeap
Ov107_Actor_DetachFromRegion returns the result of Ov107_RemoveChildFromRegion
Ov107_HandleRegionEvent returns the result of Ov107_RegisterChildInRegion
```
<!-- END generated:repairs -->

### 2.2 Where the port would stop (*generated*)

A call or a value the ROM shows is read, from somewhere the rewrite does not
follow (a load, a struct in registers, a stack argument).

<!-- BEGIN generated:gaps -->
```
NNS_FndFreeToExpHeap's value: no call of RecycleRegion in the C
```
<!-- END generated:gaps -->
