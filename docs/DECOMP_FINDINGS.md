# Findings for khdays-decomp from the native build

The port's native build (`native/`, branch `spike/native-decomp`) compiles the
decompilation's C with MSVC for 32-bit x86 and runs it on a PC, with the DS
memory mapped at its real addresses and the hardware emulated underneath. The
C matches the ROM byte for byte, yet running it on another CPU shows where the
source does not say what the machine code does. This file collects those
places, and what running the game taught us, for the decompilation.

It lists only what the decompilation at the revision below does not say yet.
Everything earlier versions raised and the decomp took in is gone from here;
the file's git history keeps it.

Every claim cites the ROM (addresses are ARM9; overlay code is given with its
overlay). Names are the decomp's own at the revision below. Sections marked
*generated* are refreshed by `python native/tools/decomp_findings.py`.

<!-- BEGIN generated:revision -->
Checked against khdays-decomp `2a2cd711b`.
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

At this revision the decomp has widened most of them (520 files) and keeps
79 narrow on purpose, with a comment (a wide declaration changes mwcc's
code). What is left below is more than those: some declare a different type
altogether, e.g. `Entity_Activate(void *actor, unsigned short id)` in
`Ov002_BindSlotActorModel` against the definition's
`(unsigned char *ptr, void *arg)`.

*Generated:* each function, the parameters some file declares narrower than
its definition, and the files that do.

<!-- BEGIN generated:narrowparams -->
- `Anim_GetFrame` (argument 1 as u16): `Ov023_ActorQueueMotion`
- `Anim_GetLengthQ12` (argument 1 as u16): `Ov002_UpdatePeerAnimationsAndExit`, `Ov023_ActorQueueMotion`
- `Anim_SetFrameWrapped` (argument 1 as u16): `Ov002_UpdatePeerAnimationsAndExit`
- `ArrayEntryPtrD0` (argument 0 as u16): `Ov002_ApplyRosterSlotToNode`, `Ov023_CmdAttachWeapons`, `Ov023_CmdPlaceActor`, `Ov023_CmdPlaceRelative`, `Ov023_CmdRampActorTransition`, `Ov023_CmdRampAnimFrame`, `Ov023_CmdSeatMembers`, `Ov023_CmdWarpActor`, `Ov023_Cmd_ParentEntityToEntity`, `Ov023_Cmd_PlayEntityAnimKind1`, `Ov023_Cmd_TestEntityCollisionBit`
- `EntityMgr_AttachTrackData` (argument 0 as u16): `Ov002_ConfigureGateFromPeerRow`
- `EntityMgr_GetCollEntryField14` (argument 0 as u16): `Ov002_SpawnActorElement`
- `EntityMgr_LinkChild` (argument 0 as u16, argument 1 as u16): `Ov023_CmdAttachWeapons`, `Ov023_Cmd_ParentEntityToEntity`
- `EntityMgr_ProbeGround` (argument 0 as u16): `Ov002_SpawnActorElement`, `Ov023_CmdSeatMembers`
- `EntityMgr_SetTransition` (argument 0 as u16): `Ov023_CmdRampActorTransition`, `Ov023_Cmd_PlayEntityAnimKind1`
- `Entity_Activate` (argument 1 as u16): `Ov002_BindSlotActorModel`, `Ov002_BuildSpawnRow`
- `Entity_ForwardToSlot` (argument 0 as u16): `Ov023_CmdSetupPartyActors`
- `Entity_LoadAndAttach` (argument 0 as u16): `Ov002_LoadPeerIntoSlot`
- `Entity_SetVisible` (argument 0 as u16): `Ov023_CmdPlaceActor`, `Ov023_CmdPlaceRelative`, `Ov023_CmdSetupPartyActors`, `Ov023_CmdWarpActor`, `Ov023_Cmd_ParentEntityToEntity`, `Ov023_HideScreenActors`, `Ov023_ReleaseScreenSprites`
- `Entity_SubmitRenderNode` (argument 0 as u16, argument 1 as u16): `Ov023_CmdPlaceActor`, `Ov023_CmdPlaceRelative`, `Ov023_CmdSeatMembers`
- `Entity_Tick` (argument 0 as u16): `Ov023_HideScreenActors`, `Ov023_ReleaseScreenSprites`
- `GX_SetGraphicsMode` (argument 0 as u16): `Gfx_RestoreAfterPause`, `Ov024_MobiClip_SetUpMainEngine`
- `GetTrackEntryBase` (argument 0 as u16): `Ov002_LoadPeerIntoSlot`, `Ov002_UpdatePeerAnimationsAndExit`, `Ov023_CmdWarpActor`
- `LoadArrayInt244` (argument 0 as u16): `Ov023_ActorQueueMotion`, `Ov023_Cmd_TestEntityCollisionBit`
- `LoadArrayU8At0cc` (argument 0 as u16): `Ov002_ApplyRosterSlotToNode`, `Ov023_CmdPlaceActor`
- `LoadArrayU8At0ce` (argument 0 as u16): `Ov023_CmdAttachWeapons`, `Ov023_CmdPlaceRelative`, `Ov023_Cmd_ParentEntityToEntity`
- `NNS_G3dMdlSetMdlDiffAll` (argument 1 as u16): `Ov255_DrawShakes`
- `Ov002_CreateActorClass` (argument 0 as u16): `Ov002_VmCmd7d18c`
- `Ov002_CreateElementClass` (argument 0 as u16): `Ov002_VmCmd7d004`
- `Ov002_CreateLineClass` (argument 0 as u16): `Ov002_VmCmdList7d610`
- `Ov002_CreatePieceClass` (argument 0 as u16): `Ov002_VmCmd7ceac`
- `Ov002_CreatePieceClass_2` (argument 0 as u16): `Ov002_VmCmd7d334`
- `Ov002_CreateSpareClass` (argument 0 as u16): `Ov002_CmdCreateModuleSlot`
- `Ov002_CreateTravelClass` (argument 0 as u16): `Ov002_VmCmd7d950`
- `Ov002_PlotCanvasPixel` (argument 1 as u16, argument 2 as u16, argument 3 as u8): `Ov002_BlitMask`
- `Ov002_SpawnKindIntoFreeSpot` (argument 1 as u16): `Ov002_SpawnTieredDrop`
- `Ov006_FindEntryByTag` (argument 1 as u16): `Ov006_MissionRetargetCellByTag`
- `Ov008_ApplyTempFieldsAndRestore` (argument 2 as u16, argument 3 as u16): `Ov008_SetTagValueDup`
- `Ov008_ClearGridSlot` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov008_PlaceTrackedNode`
- `Ov008_DrawMissionRow` (argument 1 as u16): `Ov008_MissionListRevealTick`
- `Ov008_Elem_SetPos` (argument 2 as s16, argument 3 as s16): `Ov008_RetargetCellByTag`
- `Ov008_FindEntryByTag` (argument 1 as u16): `Ov008_MissionRetargetCellByTag`, `Ov008_RetargetCellByTag`, `Ov008_SetTagValueDup`, `Ov008_ShowTierPage`
- `Ov008_StampTileMode` (argument 2 as u8): `Ov008_ShowTierPage`
- `Ov015_CreateChestClass` (argument 0 as u16): `Ov015_VmCmd27a0`
- `Ov015_CreatePickupClass` (argument 0 as u16): `Ov015_VmCmd22b0`
- `Ov015_CreateSpotClass` (argument 0 as u16): `Ov015_ScriptOpCreateSpots`
- `Ov025_ApplyTempFieldsByTagB` (argument 2 as s16, argument 3 as s16): `Ov025_ScrollList_DrawRow`
- `Ov025_ClearGridSlot` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov025_PlaceTrackedNode`
- `Ov025_DrawMissionRow` (argument 1 as u16): `Ov025_MissionListRevealTick`
- `Ov025_Elem_SetPos` (argument 2 as s16, argument 3 as s16): `Ov025_RetargetCellByTag`
- `Ov025_FindEntryByTag` (argument 1 as u16): `Ov025_RetargetCellByTag`, `Ov025_SetTagValueDup`
- `Ov025_GetTableValue` (argument 0 as u16): `Ov025_ScrollList_BuildRows`
- `Ov025_GetTableValueB` (argument 0 as u16): `Ov025_ScrollList_BuildRows`
- `Ov025_MissionList_HasVisibleInDays` (argument 0 as u16, argument 1 as u16): `Ov025_ScrollList_BuildRows`
- `Ov026_FillTilemapRegionPalette` (argument 2 as u8): `Ov026_ShowTierPage`
- `Ov026_FindEntryByTag` (argument 1 as u16): `Ov026_ShowTierPage`
- `Ov105_SetSlotEventHandler` (argument 0 as u16): `Ov105_SelectChannel`, `Ov105_WH_ChildConnect`, `Ov105_WH_Finalize`, `Ov105_WH_FreeBuffers`, `Ov105_WH_Initialize`, `Ov105_WH_ParentConnect`, `Ov105_WH_PortReceiveCallback`, `Ov105_WH_SetReceiver`, `Ov105_WH_SetSsid`, `Ov105_WH_StartMeasureChannel`, `Ov105_WH_StateInEndChild`, `Ov105_WH_StateInMeasureChannel`, `Ov105_WH_StateInSetMPData`, `Ov105_WH_StateInStartChildMP`, `Ov105_WH_StateOutEnd`, `Ov105_WH_StateOutMeasureChannel`, `Ov105_WH_StateOutStartChild`, `Ov105_WH_StateOutStartChildMP`
- `Ov107_InvokeHitCallback` (argument 3 as u8): `Ov208_ContactSweep`, `Ov209_ContactSweep`, `Ov212_AreaAttackSweep`, `Ov214_ProcessHitTargets`, `Ov215_ProcessHitTargets`, `Ov216_ProcessHitTargets`, `Ov217_ProcessHitTargets`, `Ov218_ShotBurstTick`, `Ov219_AttackSweep`, `Ov220_AttackSweep`, `Ov221_StrikeSweepEntities`, `Ov222_StrikeSweepEntities`, `Ov223_StrikeSweepEntities`, `Ov224_StrikeSweepEntities`, `Ov225_StrikeSweepEntities`, `Ov226_StrikeSweepEntities`, `Ov227_AttackSweep`, `Ov228_ContactSweep`, `Ov229_ContactSweep`, `Ov230_ContactSweep`, `Ov231_ProbeSpawnPoint`, `Ov232_ProbeSpawnPoint`, `Ov233_ContactSweep`, `Ov237_AttackHitTest`, `Ov239_ContactSweep`, `Ov240_ContactSweep`, `Ov248_ContactSweep`, `Ov249_ContactSweep`, `Ov252_ReboundHitTest`, `Ov254_ReboundHitTest`, `Ov256_AttackHitTest`, `Ov260_AttackSweep`, `Ov263_ProbeSpawnPoint`, `Ov264_ProcessHitTargets`, `Ov265_ProbeSpawnPoint`, `Ov266_AreaAttackSweep`, `Ov267_AreaAttackSweep`, `Ov268_ContactSweep`, `Ov276_ContactSweep`, `Ov280_ProbeSpawnPoint`
- `Ov231_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov231_AiFireVolleyTick`
- `Ov232_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov232_AiFireVolleyTick`
- `Ov263_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov263_AiFireVolleyTick`
- `Ov265_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov265_AiFireVolleyTick`
- `Ov280_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov280_AiFireVolleyTick`
- `PMi_SendSleepStart` (argument 0 as u16, argument 1 as u16): `PM_GoSleepMode`
- `Res_RequestIdPair` (argument 0 as s16): `Ov287_Actor_InitClassAndSpawnParts`, `Ov288_Actor_InitClassAndSpawnParts`, `Ov289_Actor_InitClassAndSpawnParts`
- `StoreValueInNamedEntry` (argument 0 as u16): `Ov002_LoadPeerIntoSlot`
- `TailForwardTrackEntry` (argument 0 as u16): `Ov023_CmdSeatMembers`, `Ov023_CmdSetupPartyActors`
- `func_0202c208` (argument 0 as u16): `Ov002_PlaceSlotMarkerOnGround`, `Ov002_UpdateSpawnedSpots`, `Ov022_SettlePointOnGround`
- `func_0202c248` (argument 0 as u16): `Ov002_UpdateSpawnedSpots`, `Ov022_SettlePointOnGround`
<!-- END generated:narrowparams -->

### 1.2 The tag trackers' `swap`, seen running

The header of the eight `OvNNN_TickTagTrackerNodes` (the same 0x1a0 bytes)
keeps, as a ROM bug, the `swap` read uninitialised when the table has no
getter at +0x48, and speaks of the case "with a +0x44 setter". What the game
does, measured in the Holomisiones challenge list (Ov025, day 357): both
tables there (`0x021cb390`, `0x021cb3dc`, 32 nodes) have neither a getter
(+0x48) nor a setter (+0x44), only the apply at +0x3c. `swap` is `sb`,
which the function never sets on that path; its value is its caller's r9.
From the camp menu's update (`Ov025_GetIdleHandler` → `Ov025_CommitPage` →
`Ov025_TickSelectionWidget`, none of which touches r9) that is what
`Obj_UpdateAll` left: the previous update's result (`blx r0; mov sb, r0`,
`0x02023b64`/`0x02023b68`), here `Ov002_SceneStep`'s 0 -- so nothing is
called. With a nonzero r9 the ROM would call the missing setter, address 0,
which the ARM9 maps to the ITCM: `OSi_VBlankInterruptHandler` would run twice
per stepped node. Other callers set `sb` before the call (e.g.
`Ov025_TickPageScroll`, `mov sb, #0` at `0x0209b300`), so the value is the
caller's in each case. Worth a line in the header: which r9 each path leaves,
and that "no setter" means a call to address 0.

The port reproduces the first path (runtime.c `khdays_rom_r9`) and stops,
naming it, on any read it cannot account for.

### 1.3 `Node_CallHook80` hands the hook its node

`Node_CallHook80(char *p)` calls the node's +0x80 hook as `f()`. The ROM
keeps the node in r0 for it: `ldr r1,[r0,#0x80]; cmp r1,#0; popeq; blx r1`
(`0x0203c8e8`-`0x0203c8f4`), and the hooks read it --
`ProcessListChildren(int this_)` is one (`mov r4, r0` at `0x0203c180`).
Should read `f(p)`. Reached when an enemy region is hidden
(`Ov107_Region_SetVisible` → `Node_CallHook80`); natively the hook walked a
list at a garbage address. A call through a pointer, so the ROM scan behind
the lists in 2 does not see it.

### 1.4 Arithmetic on a `void *`

`Ov006_SendNetworkPacket` copies the payload to
`MISSION_CONTEXT->primaryBuffer + 4`, and `primaryBuffer` is a `void *` in
the new shared `MissionContext` (`include/game/mission_lobby.h`). mwcc and
GCC take `void *` arithmetic as `char *`; standard C, and MSVC, do not. A
`char *` field, or a cast there, says the same.

### 1.5 What runs natively on it

Mission Mode's whole loop (camp menu, mission, Retirarse, results, camp menu)
and Story Mode to the second day (title, opening movie, day 255, the
clock-tower cutscene, the monologue, the next day's card, the field). With a
player's own save (day 357): loading, the main menu and its submenus, saving
to a new slot and reading it back, the Holomisiones and challenge lists, and
challenge 07 in play (Axel's intro, the mission's HUD, combat).

## 2. Known, and kept as they are

What remains cannot be written in C without changing the ROM's code (or
follows a value through a chain too deep to spell out), as the decomp has
said: the `void` functions whose r0 a caller uses
(`Ov107_RegisterChildInRegion`, `Ov107_RemoveChildFromRegion`,
`Ov107_ProcessObjectTick`), the fifth, stack argument of
`Ov107_Actor_SetAttachSlot` and the variadic call in `Ov226`, and the
NitroSDK functions the SDK itself declares `void`. The port handles these
itself; the lists below are what it rewrites and where it would stop, kept for
reference and to show when a new revision changes them.

### 2.1 Rewritten by the port (*generated*)

<!-- BEGIN generated:repairs -->
```
NNS_FndFreeToExpHeap returns the result of RecycleRegion
NNSi_FndFreeFromDefaultHeap returns the result of NNS_FndFreeToExpHeap
OS_FreeToHeap returns the result of OS_RestoreInterrupts
OSi_FreeStackAlloc returns the result of OS_FreeToHeap
Ov107_Actor_DetachFromRegion returns the result of Ov107_RemoveChildFromRegion
Ov107_HandleRegionEvent returns the result of Ov107_RegisterChildInRegion
SND_GetFirstInstDataPos returns (int)p
```
<!-- END generated:repairs -->

### 2.2 Where the port would stop (*generated*)

The value comes from somewhere the rewrite does not follow (a load, a
struct in registers, a stack argument). None has been reached so far.

<!-- BEGIN generated:gaps -->
```
DispatchWithReentrantScratch's value: sources: 0202e518 ldr r0,[0x202e538] = 0x20475d0 DAT_020475d0 = 0x0 Reset; 0202e52c bl 0x0202e474 => 0x202e474 RigWork_Update
FSi_WaitForCardThread's value: sources: 01ff8110 ldr r0,[0x1ff8124] = 0x27e0078 DAT_027e0078 = 0x0 Reset
FreeInstanceMemory's value: sources: 0203d1b0 bl 0x020236ac => 0x20236ac NNSi_FndFreeFromDefaultHeap; entry:r0
ModelAnimSet_Bind's value: sources: 0202a378 ldr r0,[sp,#0x4]
Ov002_FillMapRows's value: sources: arm9_ov002::02053b68 bl 0x02053bb8 => 0x2053bb8 Ov002_GetItemResource; arm9_ov002::02053b9c bl 0x02053c18 => 0x2053c18 Ov002_WriteMapRow
Ov011_SetupTitleBackgrounds's value: sources: arm9_ov011::0205da30 orr r0,r0,#0x33
Ov107_InvokeSlot0x74's value: sources: arm9_ov107::020c2b48 blx r2; entry:r0
Ov107_ProcessObjectTick's value: sources: arm9_ov107::020c6a3c add r0,r0,#0x1; arm9_ov107::020c6ab4 add r0,r0,#0x1; arm9_ov107::020c6b28 add r0,r0,#0x1; arm9_ov107::020c6b8c add r0,r0,#0x1; arm9_ov107::020c6bf0 add r0,r0,#0x1; arm9_ov107::020c6c94 add r0,r0,#0x1; arm9_ov107::020c7358 bic r0,r0,#0x2; arm9_ov107::020c736c orr r0,r0,#0x2
Ov107_RegisterChildInRegion's value: sources: arm9_ov107::020c4e9e add r0,#0x9c; arm9_ov107::020c4eae blx 0x0203bfb4 => 0x203bfb4 RegisterSubscriberSlot
Ov107_RemoveChildFromRegion's value: sources: arm9_ov107::020c4eb6 add r0,#0x9c; arm9_ov107::020c4ec6 blx 0x0203bfe8 => 0x203bfe8 RemoveChildFromListByPtr
Ov237_RotateByActorHeading's value: sources: arm9_ov237::020cdbc8 ldmia r5,{r0,r1,r2}
Ov252_TurnVecY's value: sources: arm9_ov252::020cdb6c ldmia r4,{r0,r1,r2}
SetSubitemState's value: sources: 0203ba24 ldrsh r0,[r2,r0]; 0203ba68 bic r0,r0,#0x1; 0203ba78 add r0,r0,r4, lsl #0x2; 0203bab0 orr r0,r0,#0x2; entry:r0
SoundMgr_Update's value: sources: 020333a4 bl 0x02019bf0 => 0x2019bf0 NNS_SndMain; tail 02032f78 addls pc,pc,r1, lsl #0x2; tail 02032fb0 addls pc,pc,r0, lsl #0x2; tail 02033034 addls pc,pc,r1, lsl #0x2; tail 020331d0 addls pc,pc,r0, lsl #0x2
TileSurface_Init's value: sources: 0202ff24 mov r0,#0x0; 0202ff4e blx 0x02014174 => 0x2014174 Tilemap_FillRect
Ov107_Actor_SetAttachSlot called from Ov117_InitEffectActor without what the ROM passes (argument 4: stack)
Ov107_Actor_SetAttachSlot called from Ov118_InitEffectActor without what the ROM passes (argument 4: stack)
Ov107_Actor_SetAttachSlot called from Ov185_Actor_Construct_2 without what the ROM passes (argument 4: stack)
Ov107_Actor_SetAttachSlot called from Ov186_InitEffectActor without what the ROM passes (argument 4: stack)
Ov107_Actor_SetAttachSlot called from Ov187_InitEffectActor without what the ROM passes (argument 4: stack)
Ov226_Projectile_SetupFlight called from Ov226_HandleMessageArgs without what the ROM passes (argument 4: stack)
```
<!-- END generated:gaps -->
