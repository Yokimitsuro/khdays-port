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
Checked against khdays-decomp `5f4f68329`.
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
changes mwcc's code). The 42 functions left below are those and whatever
else still declares a narrower parameter.

*Generated:* each function, the parameters some file declares narrower than
its definition, and the files that do.

<!-- BEGIN generated:narrowparams -->
- `Anim_GetFrame` (argument 1 as u16): `Ov023_ActorQueueMotion`
- `Anim_GetLengthQ12` (argument 1 as u16): `Ov002_UpdatePeerAnimationsAndExit`, `Ov023_ActorQueueMotion`
- `Anim_SetFrameWrapped` (argument 1 as u16): `Ov002_UpdatePeerAnimationsAndExit`
- `ArrayEntryPtrD0` (argument 0 as u16): `Ov002_ApplyRosterSlotToNode`, `Ov023_CmdAttachWeapons`, `Ov023_CmdPlaceActor`, `Ov023_CmdPlaceRelative`, `Ov023_CmdRampActorTransition`, `Ov023_CmdRampAnimFrame`, `Ov023_CmdSeatMembers`, `Ov023_CmdWarpActor`, `Ov023_Cmd_ParentEntityToEntity`, `Ov023_Cmd_PlayEntityAnimKind1`, `Ov023_Cmd_TestEntityCollisionBit`
- `EntityMgr_GetCollEntryField14` (argument 0 as u16): `Ov002_SpawnActorElement`
- `EntityMgr_LinkChild` (argument 0 as u16, argument 1 as u16): `Ov023_CmdAttachWeapons`, `Ov023_Cmd_ParentEntityToEntity`
- `EntityMgr_ProbeGround` (argument 0 as u16): `Ov002_SpawnActorElement`, `Ov023_CmdSeatMembers`
- `EntityMgr_SetTransition` (argument 0 as u16): `Ov023_CmdRampActorTransition`, `Ov023_Cmd_PlayEntityAnimKind1`
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
- `Ov002_PlotCanvasPixel` (argument 1 as u16, argument 2 as u16, argument 3 as u8): `Ov002_BlitMask`
- `Ov002_SpawnKindIntoFreeSpot` (argument 1 as u16): `Ov002_SpawnTieredDrop`
- `Ov006_FindEntryByTag` (argument 1 as u16): `Ov006_MissionRetargetCellByTag`
- `Ov008_ApplyTempFieldsAndRestore` (argument 2 as u16, argument 3 as u16): `Ov008_SetTagValueDup`
- `Ov008_ClearGridSlot` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov008_PlaceTrackedNode`
- `Ov008_Elem_SetPos` (argument 2 as s16, argument 3 as s16): `Ov008_RetargetCellByTag`
- `Ov008_FindEntryByTag` (argument 1 as u16): `Ov008_MissionRetargetCellByTag`, `Ov008_RetargetCellByTag`, `Ov008_SetTagValueDup`, `Ov008_ShowTierPage`
- `Ov008_StampTileMode` (argument 2 as u8): `Ov008_ShowTierPage`
- `Ov025_ApplyTempFieldsByTagB` (argument 2 as s16, argument 3 as s16): `Ov025_ScrollList_DrawRow`
- `Ov025_ClearGridSlot` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov025_PlaceTrackedNode`
- `Ov025_Elem_SetPos` (argument 2 as s16, argument 3 as s16): `Ov025_RetargetCellByTag`
- `Ov025_FindEntryByTag` (argument 1 as u16): `Ov025_RetargetCellByTag`, `Ov025_SetTagValueDup`
- `Ov025_GetTableValue` (argument 0 as u16): `Ov025_ScrollList_BuildRows`
- `Ov025_GetTableValueB` (argument 0 as u16): `Ov025_ScrollList_BuildRows`
- `Ov025_MissionList_HasVisibleInDays` (argument 0 as u16, argument 1 as u16): `Ov025_ScrollList_BuildRows`
- `Ov026_FillTilemapRegionPalette` (argument 2 as u8): `Ov026_ShowTierPage`
- `Ov026_FindEntryByTag` (argument 1 as u16): `Ov026_ShowTierPage`
- `Ov107_InvokeHitCallback` (argument 3 as u8): `Ov208_ContactSweep`, `Ov209_ContactSweep`, `Ov212_AreaAttackSweep`, `Ov214_ProcessHitTargets`, `Ov215_ProcessHitTargets`, `Ov216_ProcessHitTargets`, `Ov217_ProcessHitTargets`, `Ov218_ShotBurstTick`, `Ov219_AttackSweep`, `Ov220_AttackSweep`, `Ov221_StrikeSweepEntities`, `Ov222_StrikeSweepEntities`, `Ov223_StrikeSweepEntities`, `Ov224_StrikeSweepEntities`, `Ov225_StrikeSweepEntities`, `Ov226_StrikeSweepEntities`, `Ov227_AttackSweep`, `Ov228_ContactSweep`, `Ov229_ContactSweep`, `Ov230_ContactSweep`, `Ov231_ProbeSpawnPoint`, `Ov232_ProbeSpawnPoint`, `Ov233_ContactSweep`, `Ov237_AttackHitTest`, `Ov239_ContactSweep`, `Ov240_ContactSweep`, `Ov248_ContactSweep`, `Ov249_ContactSweep`, `Ov252_ReboundHitTest`, `Ov254_ReboundHitTest`, `Ov256_AttackHitTest`, `Ov260_AttackSweep`, `Ov263_ProbeSpawnPoint`, `Ov264_ProcessHitTargets`, `Ov265_ProbeSpawnPoint`, `Ov266_AreaAttackSweep`, `Ov267_AreaAttackSweep`, `Ov268_ContactSweep`, `Ov276_ContactSweep`, `Ov280_ProbeSpawnPoint`
- `PMi_SendSleepStart` (argument 0 as u16, argument 1 as u16): `PM_GoSleepMode`
- `StoreValueInNamedEntry` (argument 0 as u16): `Ov002_LoadPeerIntoSlot`
- `TailForwardTrackEntry` (argument 0 as u16): `Ov023_CmdSeatMembers`, `Ov023_CmdSetupPartyActors`
- `func_0202c208` (argument 0 as u16): `Ov002_PlaceSlotMarkerOnGround`, `Ov002_UpdateSpawnedSpots`, `Ov022_SettlePointOnGround`
- `func_0202c248` (argument 0 as u16): `Ov002_UpdateSpawnedSpots`, `Ov022_SettlePointOnGround`
<!-- END generated:narrowparams -->

### 1.2 What runs natively on it

Mission Mode's whole loop (camp menu, mission, Retirarse, results, camp menu)
and Story Mode to the second day (title, opening movie, day 255, the
clock-tower cutscene, the monologue, the next day's card, the field). With a
player's own save (day 357): loading, the main menu and its submenus, saving
to a new slot and reading it back, the Holomisiones and challenge lists,
challenge 07 in play (Axel's intro, the mission's HUD, combat), the Moogle
shop, a Halloween Town mission's fights and Fire, and a reset back to the
title. The optimized (Release) build runs the same.

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
  on x86: cdecl stacks its two `VecFx32` in the words
  `Ov226_Projectile_SetupFlight` reads.
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
  (`input` in `Ov006_MissionBuildOptionRows`, `direction` and `action` in
  ov005's handlers, all commented in the decomp), the port passes 0, which
  those functions take the same way.
- Every short direct call (681 at this revision) passes zeros for the words
  the definition takes and it does not. On x86 those slots are the caller's frame,
  and an optimizing compiler keeps other values in a parameter's slot once the
  parameter is dead: MSVC's Release build crashed in Ov025's camp menu on
  exactly that, when `Ov025_ScrollMenuMoveTo`'s menu pointer came back
  changed from a callee's callee.

The lists below are what the port rewrites and where it would stop, kept for
reference and to show when a new revision changes them.

### 2.1 Rewritten by the port (*generated*)

<!-- BEGIN generated:repairs -->
```
NNS_FndFreeToExpHeap returns the result of RecycleRegion
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
*None at this revision.*
<!-- END generated:gaps -->
