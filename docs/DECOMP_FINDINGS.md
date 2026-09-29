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
Checked against khdays-decomp `0788aae45`.
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

### 1.2 Struct results spelled one way at the definition, another at the call

A struct returned by value travels, in the ARM ABI, through a pointer the
caller passes in r0. Five functions spell that pointer out on one side and
not on the other. mwcc makes the same code of both forms; any other compiler
does not, and the port had to fix each by hand:

- `SND_GetFirstInstDataPos` is defined `void (struct S *p)` and zeroes both
  words through it (`str r1,[r0]`, `str r1,[r0,#4]`, `0x02009024`).
  `LoadSingleWaves` declares the SDK's
  `SNDInstPos SND_GetFirstInstDataPos(const SNDBankData *)` and passes its
  stack slot (`add r0,sp,#0` before the `bl` at `0x0201c484`). On x86 an
  8-byte struct comes back in EDX:EAX with no pointer passed, so the
  definition as written zeroed the bank's first two words. The SDK's form in
  both places (the bank is never read) says what the ROM does.
- `FS_GetOverlayFileID` is the same: defined
  `void (FsOverlayInfo *dst, int *overlay)`, declared by its callers
  (`FSi_LoadOverlayInfoCore`, `FS_LoadOverlayImage`, `FS_LoadOverlayInfo`)
  as `FSFileID FS_GetOverlayFileID(const FSOverlayInfo *)`.
- `NNSi_G2dFontGetTextRect` the other way round: defined returning
  `NNSG2dTextRect`, while `Text_AlignAnchor` and
  `Ov002_SceneLayoutPanelWindow` declare it `void` and pass the pointer.
- `Ov237_RotateByActorHeading` and `Ov252_TurnVecY` are defined
  `void (VecFx32 *out, ...)` and their callers (seven in Ov237,
  `Ov252_CruiseTick`) declare them returning `VecFx32`. For a 12-byte struct
  x86 passes the pointer in the same place, but the caller then copies the
  struct through the pointer the callee returns in EAX, which a `void`
  definition leaves unset (MSVC: `call; mov edx,[eax]`).

### 1.3 `ModelAnimSet_Bind` returns `texSrc`

`ModelAnimSet_Bind` is `void`, and both its callers,
`Resource_BindFileToSlot` and `Snd_RegisterSeqAndBind`, return its value as
an `int`. The ROM leaves its fourth argument in r0: it stores `texSrc` at
`[sp,#4]` on entry and reloads it for the last store
(`ldr r0,[sp,#4]; strh r0,[fp,#0xa]`, `0x0202a378`). Declared `int`, with
`return texSrc;` after that store, it would say so; the load is already the
one the return needs.

### 1.4 `Ov006_MissionBuildOptionRows` passes `input` unset

In single-player mode (`single_row_mode != 0`) the C never assigns `input`
before `Ov006_SetTitleMode(input)`. The ROM passes r4 there
(`mov r0,r4`, `0x02050f4c`), which the function never sets on that path: it
is its caller's, `Obj_UpdateAll`'s previous heap (`mov r4,r0` after
`func_0202362c`, `0x02023b5c`) -- an address, which `Ov006_SetTitleMode`
ignores as it does 0 (it stores only 1 to 4, `0x02055b44`-`0x02055b58`).
The ov005 handlers (`Ov005_HandleDirectionalInput`,
`Ov005_UpdateConfirmation`) say the same of their r4 in a comment; one here
would too. Compiled for another CPU the variable is whatever the stack held,
and a value from 1 to 4 would change the title mode.

### 1.5 `Ov026_CreateService` depends on an order C leaves open

`data_ov026_02091360[2] = InstantiateClass(&data_ov026_02091200, arg0);`
stores the new service at +8 of the shop's state, whose pointer the
service's constructor sets. The ROM calls first and loads the pointer after
(`bl InstantiateClass`, then `ldr r1,=0x02091360; ldr r1,[r1];
str r0,[r1,#8]`, `0x02082a90`-`0x02082a9c`); C does not order the two sides
of `=`, and MSVC optimizing loaded the pointer first, still NULL -- the shop
crashed on opening. Two statements (the call into a local, then the store)
say what the ROM does. mwcc probably gives the same code for both; worth a
look wherever a call on the right may set the pointer written through on the
left (a constructor and the scene's state, as here).

### 1.6 The enemies print their resource name without the class id

43 enemy constructors, ov202 to ov301 (35 `OvNNN_CreateNamedEntity`, 8
`OvNNN_AllocActorWithName`; the list is in 2.1), call
`OS_SPrintf(name, &data_ovNNN_...)` with the format `"Ms/%02x.p"` and no
argument for `%02x`, declared `OS_SPrintf(void *buffer, void *format)`. The
ROM passes the class id in r2 -- the same `mov r2, #id` that feeds the
`strb` to +0x19c (Ov286: `mov r2,#0x6a` at `0x020d37fc`, `strb r2,[r4,#0x19c]`,
`bl OS_SPrintf` at `0x020d380c`); the Ghidra scan finds a constant there at
all 43. Compiled for another CPU the name gets whatever the stack held, the
`Ms/` file is not found and `FS_ReadFile` reads through an unopened file:
a Halloween Town mission crashed on its first enemy (ov286), after its
cutscene.
Declaring `OS_SPrintf` variadic and passing the id -- the value already
stored at +0x19c -- says what the ROM does, and likely gives mwcc the same
code, since r2 already holds it.

### 1.7 What runs natively on it

Mission Mode's whole loop (camp menu, mission, Retirarse, results, camp menu)
and Story Mode to the second day (title, opening movie, day 255, the
clock-tower cutscene, the monologue, the next day's card, the field). With a
player's own save (day 357): loading, the main menu and its submenus, saving
to a new slot and reading it back, the Holomisiones and challenge lists,
challenge 07 in play (Axel's intro, the mission's HUD, combat), the Moogle
shop, and a Halloween Town mission up to its first enemy (1.6). The
optimized (Release) build runs the same.

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
- Every short direct call (689 at this revision) passes zeros for the words
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
Ov237_RotateByActorHeading returns (int)out
Ov252_TurnVecY returns (int)out
Ov202_AllocActorWithName -> OS_SPrintf: passes (int)0x27
Ov203_AllocActorWithName -> OS_SPrintf: passes (int)0x27
Ov204_CreateNamedEntity -> OS_SPrintf: passes (int)0x28
Ov205_CreateNamedEntity -> OS_SPrintf: passes (int)0x28
Ov214_AllocActorWithName -> OS_SPrintf: passes (int)0x2e
Ov215_AllocActorWithName -> OS_SPrintf: passes (int)0x2e
Ov216_AllocActorWithName -> OS_SPrintf: passes (int)0x2f
Ov217_AllocActorWithName -> OS_SPrintf: passes (int)0x2f
Ov219_CreateNamedEntity -> OS_SPrintf: passes (int)0x31
Ov220_CreateNamedEntity -> OS_SPrintf: passes (int)0x32
Ov234_CreateNamedEntity -> OS_SPrintf: passes (int)0x3d
Ov239_CreateNamedEntity -> OS_SPrintf: passes (int)0x42
Ov240_CreateNamedEntity -> OS_SPrintf: passes (int)0x43
Ov241_CreateNamedEntity -> OS_SPrintf: passes (int)0x44
Ov242_CreateNamedEntity -> OS_SPrintf: passes (int)0x44
Ov243_CreateNamedEntity -> OS_SPrintf: passes (int)0x45
Ov250_CreateNamedEntity -> OS_SPrintf: passes (int)0x4b
Ov251_CreateNamedEntity -> OS_SPrintf: passes (int)0x4b
Ov261_CreateNamedEntity -> OS_SPrintf: passes (int)0x55
Ov262_CreateNamedEntity -> OS_SPrintf: passes (int)0x55
Ov264_AllocActorWithName -> OS_SPrintf: passes (int)0x58
Ov269_CreateNamedEntity -> OS_SPrintf: passes (int)0x5c
Ov270_CreateNamedEntity -> OS_SPrintf: passes (int)0x5c
Ov276_AllocActorWithName -> OS_SPrintf: passes (int)0x61
Ov281_CreateNamedEntity -> OS_SPrintf: passes (int)0x66
Ov284_CreateNamedEntity -> OS_SPrintf: passes (int)0x69
Ov285_CreateNamedEntity -> OS_SPrintf: passes (int)0x6a
Ov286_CreateNamedEntity -> OS_SPrintf: passes (int)0x6a
Ov287_CreateNamedEntity -> OS_SPrintf: passes (int)0x6b
Ov288_CreateNamedEntity -> OS_SPrintf: passes (int)0x6b
Ov289_CreateNamedEntity -> OS_SPrintf: passes (int)0x6b
Ov290_CreateNamedEntity -> OS_SPrintf: passes (int)0x6c
Ov291_CreateNamedEntity -> OS_SPrintf: passes (int)0x6d
Ov292_CreateNamedEntity -> OS_SPrintf: passes (int)0x6e
Ov293_CreateNamedEntity -> OS_SPrintf: passes (int)0x6f
Ov294_CreateNamedEntity -> OS_SPrintf: passes (int)0x70
Ov295_CreateNamedEntity -> OS_SPrintf: passes (int)0x70
Ov296_CreateNamedEntity -> OS_SPrintf: passes (int)0x70
Ov297_CreateNamedEntity -> OS_SPrintf: passes (int)0x71
Ov298_CreateNamedEntity -> OS_SPrintf: passes (int)0x72
Ov299_CreateNamedEntity -> OS_SPrintf: passes (int)0x73
Ov300_CreateNamedEntity -> OS_SPrintf: passes (int)0x74
Ov301_CreateNamedEntity -> OS_SPrintf: passes (int)0x75
```
<!-- END generated:repairs -->

### 2.2 Where the port would stop (*generated*)

A call or a value the ROM shows is read, from somewhere the rewrite does not
follow (a load, a struct in registers, a stack argument).

<!-- BEGIN generated:gaps -->
*None at this revision.*
<!-- END generated:gaps -->
