# Findings for khdays-decomp from the native build

The port's native build (`native/`, branch `spike/native-decomp`) compiles the
decompilation's C with MSVC for 32-bit x86 and runs it on a PC, with the DS
memory mapped at its real addresses and the hardware emulated underneath. The
C matches the ROM byte for byte, yet running it on another CPU shows where the
source does not say what the machine code does. This file collects those
places, and what running the game taught us, for the decompilation.

It lists only what the decompilation at the revision below does not say yet.
Everything earlier versions raised that the decomp has taken in -- the calls
passing what the ROM passes, the tail-call wrappers, the empty functions, the
work-buffer frees, the overlay names, the MobiClip decoder, the ARM7 map, the
sound notes -- is gone from here; the file's git history keeps it.

Every claim cites the ROM (addresses are ARM9; overlay code is given with its
overlay). Names are the decomp's own at the revision below. Sections marked
*generated* are refreshed by `python native/tools/decomp_findings.py`.

<!-- BEGIN generated:revision -->
Checked against khdays-decomp `7a078f80e`.
<!-- END generated:revision -->

## 1. Calls that rely on what the ARM registers held

On the ARM9 the first four arguments travel in r0-r3 and the result in r0. C
that passes fewer arguments than the callee reads, or uses the "result" of a
function defined `void`, still compiles to the ROM's bytes: the callee finds
whatever the registers held. On any other ABI those values are garbage.

The port scans the ROM for each such call and return (Ghidra, a backward
register slice; `native/abi/ghidra_abi.txt`, functions keyed by
`module@address`) and rewrites the C where the source of the value is
mechanical. At this revision the scan covers the calls between overlays and
the new parameters and return values too.

### 1.1 Checked by hand, still open

- **`Ov002_CreateAndRestoreHud`** calls `Ov002_IsPanelModeSet()`; the
  definition takes the value it returns when no panel exists
  (`Ov002_IsPanelModeSet(int fallback)`, `0x02061b80`: `bxeq lr` with r0
  untouched). The ROM's r0 there is what `Ov002_RepublishHud` (C: `void`)
  left, which follows several calls deep. `Ov002_ApplyTally`'s call now
  passes its fallback; this one does not. The port stops if that path is
  taken (a panel is always installed so far).
- **`Ov002_OpenPanelScreen`** calls `Ov002_LoadPanelSlots(ctx->aSlots, &sRes)`;
  the definition is `(pSlots, pDst, int nPalDst, int i)` and assigns both
  last parameters before reading them (`0x020550dc` loads r2 from the literal
  pool; r3 is only pushed, for alignment, until `0x0205510c` sets it). The
  same case as `Ov011_TickLayoutAnimator` and `Ov011_BlitTileRow`, whose
  headers now say why the parameters stay; this one says nothing. On a stack
  ABI the two writes land in the caller's frame (the port saw its saved
  registers overwritten), so it passes zeros there.
- **`Ov002_PostScoreRecord`** declares
  `Ov002_RequestPanelScreen(void *, u64, int, int)`; the definition takes
  `(u16 *, u32 nHoldLo, u32 nHoldHi, int, int)`. The same five words either
  way (mwcc passes the u64 in r1:r2), but the two spellings could agree.
- **`Ov002_TickScene`** queues a VRAM transfer from `ctx->aHandles[i]`
  (`0x020590fc ldr r2,[r5,r6,lsl #2]`) for every dirty player slot, and for
  the empty slots of a one-player mission that handle is 0: the ROM DMAs
  from address 0, where the bus has nothing (GBATEK says only that DMA
  cannot reach the TCMs). Faithful to the ROM; worth a comment.

### 1.2 Mechanical repairs (*generated*)

Calls and returns the port rewrites from the ROM. Each is a place where the
C leaves the value to the registers.

<!-- BEGIN generated:repairs -->
```
NNS_FndFreeToExpHeap returns the result of RecycleRegion
NNSi_FndFreeFromDefaultHeap returns the result of NNS_FndFreeToExpHeap
OS_FreeToHeap returns the result of OS_RestoreInterrupts
OSi_FreeStackAlloc returns the result of OS_FreeToHeap
Ov008_SweepElements returns the result of Ov008_SweepFreeElementBuffers
Ov025_SweepElements returns the result of Ov025_SweepFreeElementBuffers
Ov107_Actor_DetachFromRegion returns the result of Ov107_RemoveChildFromRegion
Ov107_HandleRegionEvent returns the result of Ov107_RegisterChildInRegion
SND_GetFirstInstDataPos returns (int)p
Ov125_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov126_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov166_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov167_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov168_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov169_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov170_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov175_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov176_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov177_RelayoutAndStoreVec -> Ov107_MoveNodeAndRelayout: passes arg1
Ov178_RunSetupThenSetHw60HighBit0 -> Ov107_MoveNodeAndRelayout: passes khdays_arg1
Ov179_RunSetupThenSetHw60HighBit0 -> Ov107_MoveNodeAndRelayout: passes khdays_arg1
Ov180_RunSetupThenSetHw60HighBit0 -> Ov107_MoveNodeAndRelayout: passes khdays_arg1
Ov244_RunSetupThenSetHw60HighBit0 -> Ov107_MoveNodeAndRelayout: passes khdays_arg1
Ov277_RunSetupThenSetHw60HighBit0 -> Ov107_MoveNodeAndRelayout: passes khdays_arg1
Ov178_RunSetupThenSetHw60HighBit0 takes 2 parameters (its own r1..r1 pass through)
Ov179_RunSetupThenSetHw60HighBit0 takes 2 parameters (its own r1..r1 pass through)
Ov180_RunSetupThenSetHw60HighBit0 takes 2 parameters (its own r1..r1 pass through)
Ov244_RunSetupThenSetHw60HighBit0 takes 2 parameters (its own r1..r1 pass through)
Ov277_RunSetupThenSetHw60HighBit0 takes 2 parameters (its own r1..r1 pass through)
```
<!-- END generated:repairs -->

### 1.3 Not resolved mechanically (*generated*)

The value comes from somewhere the rewrite does not follow (a load, a
struct in registers, a stack argument). The port stops at each if it is
reached; none has been so far.

<!-- BEGIN generated:gaps -->
```
DispatchWithReentrantScratch's value: sources: 0202e518 ldr r0,[0x202e538] = 0x20475d0 DAT_020475d0 = 0x0 Reset; 0202e52c bl 0x0202e474 => 0x202e474 RigWork_Update
FSi_WaitForCardThread's value: sources: 01ff8110 ldr r0,[0x1ff8124] = 0x27e0078 DAT_027e0078 = 0x0 Reset
FreeInstanceMemory's value: sources: 0203d1b0 bl 0x020236ac => 0x20236ac NNSi_FndFreeFromDefaultHeap; entry:r0
ModelAnimSet_Bind's value: sources: 0202a378 ldr r0,[sp,#0x4]
Ov002_FillMapRows's value: sources: arm9_ov002::02053b68 bl 0x02053bb8 => 0x2053bb8 Ov002_GetItemResource; arm9_ov002::02053b9c bl 0x02053c18 => 0x2053c18 Ov002_WriteMapRow
Ov008_SweepFreeElementBuffers's value: sources: arm9_ov008::020557e0 ldr r0,[r5,#0x38]; arm9_ov008::020557fa ldr r0,[r5,#0x38]
Ov011_SetupTitleBackgrounds's value: sources: arm9_ov011::0205da30 orr r0,r0,#0x33
Ov025_SweepFreeElementBuffers's value: sources: arm9_ov025::02089488 ldr r0,[r5,#0x38]; arm9_ov025::020894a2 ldr r0,[r5,#0x38]
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

## 2. Declarations

- **Narrow return types declared wider elsewhere.** 47 functions are defined
  returning `u8`/`s8`/`u16`/`s16`/`char`/`short`, and 500 declarations in
  other files spell them `int`/`u32`/`s32`/`BOOL`. On the ARM the callee
  leaves the value extended in r0, so the ROM is fine; a host compiler
  returns only the narrow part (MSVC leaves the rest of EAX as it was). The
  function, its type and how many files declare it wider:
  `FX_AcosIdx` (u16, 1), `FX_Atan2` (unsigned short, 111), `GetGlobalU16At6`
  (unsigned short, 12), `Mem_ReadU16` (unsigned short, 3),
  `NNS_G2dFontFindGlyphIndex` (u16, 4), `NNS_G2dGetAnimCtrlCurrentFrame`
  (u16, 1), `OS_ReadOwnerOfLockWord` (unsigned short, 1),
  `Ov000_UpdateLoadState` (u8, 1), `Ov002_FindNamedValue` (short, 1),
  `Ov002_GetActorSlotByte` (signed char, 2), `Ov002_GetCtxModeByte` (signed
  char, 47), `Ov002_GetPanelField01a4` (unsigned short, 1),
  `Ov002_GetSessionSlotValue` (u16, 1), `Ov002_NodeGetResult` (signed char,
  7), `Ov004_WalkRecordsAppendMatching` (unsigned short, 7),
  `Ov005_RoundRewardPercent` (u16, 1), `Ov005_WalkRecordsAppendMatching`
  (unsigned short, 7), `Ov008_CalcMissionCompletionPercent` (u16, 1),
  `Ov008_CountSpareItemsOfChild` (u8, 1), `Ov008_GetTableValue` (unsigned
  short, 1), `Ov008_ReadInputHeader` (unsigned short, 1),
  `Ov008_WalkRecordsAppendMatching` (unsigned short, 7),
  `Ov009_WalkRecordsAppendMatching` (unsigned short, 7),
  `Ov012_MobiClip_SubtitleNextChar` (unsigned short, 1),
  `Ov022_MarshalStateHalf12` (unsigned short, 1),
  `Ov024_MobiClip_SubtitleNextChar` (unsigned short, 1),
  `Ov025_CalcMissionCompletionPercent` (u16, 1), `Ov025_GetId10` (unsigned
  short, 1), `Ov025_GetPageAByte14F3` (unsigned char, 2),
  `Ov025_GetStateByte14f0` (unsigned char, 2),
  `Ov025_WalkRecordsAppendMatching` (unsigned short, 7),
  `Ov026_CountSpareItemsOfChild` (u8, 1), `Ov026_WalkRecordsAppendMatching`
  (unsigned short, 7), `Ov105_EnterState1AndResolveId` (unsigned short, 2),
  `Ov105_GetSessionLinkState` (unsigned short, 1), `Ov105_SelectChannel`
  (s16, 1), `Ov105_WM_GetLinkLevel` (u16, 17), `Ov105_WM_GetNextTgid` (u16,
  2), `Ov254_ReboundHitTest` (u8, 1), `Ov302_GetId10` (unsigned short, 1),
  `SNDi_SetAlarmHandler` (u8, 1), `Sequence_UpdateTracks` (unsigned short,
  162), `func_02031384` (unsigned short, 14), `func_ov022_02088254` (unsigned
  short, 7), `func_ov022_020882bc` (unsigned char, 2), `func_ov022_020882f8`
  (unsigned char, 38), `func_ov022_02088cdc` (unsigned short, 1).
- **`data_ov008_02090f24` is declared seventeen ways** across ov008 (as
  `MissionContext *`, `char *`, `int`, `int[]`, `MissionGlobals`,
  `CardXferOwner *`, ...). `Ov008_MissionApplyEntryUpdate` reads it as a pair
  `{context, controller_instance}`; the controller is the next word,
  `0x02090f28`, which `Ov008_Link_Poll` reads too. One declaration of the
  block would say what it is.
- **Four definitions still put the return type on a line of their own**:
  `Get4x4IdxRegion_` (`libs/nitro/nns/calls/func_02010f7c.c`),
  `GetFrameEnd_` and `GetFrameLoopBegin_` (`func_02011c88.c`),
  `NNSi_G2dGetCellVramTransferData` (`func_02012354.c`).

## 3. Calls to shared addresses (*generated*)

For reference: every function reference whose relocation lists several
overlays with a function at that address, the candidate the C names, and all
of them. The ones known to be wrong are fixed at this revision; the port
routes all of them through a thunk to the overlay loaded at the time. The
`ov029_pointers_020b2f70` rows are by design (a table of entry points into
whichever enemy overlay is loaded in a slot).

<!-- BEGIN generated:ambiguous -->
| Address | Referring module | Referring functions | The C names | All candidates |
|---|---|---|---|---|
| `0x0204cac0` | ov022 | `Ov022_DrawSelectionMarkers`, `Ov022_DrawStateMarker`, `Ov022_ReadSelectionInput`, `Ov022_UpdateCameraAndViews`, `Ov022_UpdateUiSelectionMarker`, `func_ov022_02086e80` | `Ov002_ReaimActor` | ov000 `Ov000_Title_CreateLogoObjects`, ov001 `Ov001_ClearVideoMemory`, ov002 `Ov002_ReaimActor`, ov003 `Ov003_IsGlobalBit0Set`, ov007 `Ov007_SetupSubDisplay`, ov008 `Ov008_GetPlayerMask`, ov009 `Ov009_ClassCtor`, ov010 `Ov010_BindResourceHandle` |
| `0x0204cac0` | ov022 | `func_ov022_020aeef8` | `Ov010_BindResourceHandle` | ov000 `Ov000_Title_CreateLogoObjects`, ov001 `Ov001_ClearVideoMemory`, ov002 `Ov002_ReaimActor`, ov003 `Ov003_IsGlobalBit0Set`, ov007 `Ov007_SetupSubDisplay`, ov008 `Ov008_GetPlayerMask`, ov009 `Ov009_ClassCtor`, ov010 `Ov010_BindResourceHandle` |
| `0x0204cd7c` | main | `main` | `Ov001_BootInit` | ov001 `Ov001_BootInit`, ov009 `Ov009_Menu_VBlankTick` |
| `0x0204cecc` | ov022 | `Ov022_SendControlPacket` | `Ov002_GetU16Via0x20Then0x58` | ov002 `Ov002_GetU16Via0x20Then0x58`, ov004 `Ov004_CanTriggerActionInRange` |
| `0x0205127c` | ov022 | `Ov022_DrawSelectionMarkers`, `Ov022_PlaceCornerMarkers`, `Ov022_UpdateUiSelectionMarker` | `Ov002_ResourceNodeCallback` | ov002 `Ov002_ResourceNodeCallback`, ov008 `Ov008_GetCtxWord9758` |
| `0x0205127c` | ov023 | `Ov023_ReleaseArmedSlots` | `Ov002_ResourceNodeCallback` | ov002 `Ov002_ResourceNodeCallback`, ov008 `Ov008_GetCtxWord9758` |
| `0x020519b0` | ov022 | `Ov022_SetAnimState` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov030 | `Ov030_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov035 | `Ov035_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov036 | `Ov036_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov039 | `Ov039_ApplyMode` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov041 | `Ov041_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov044 | `Ov044_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov045 | `Ov045_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov046 | `Ov046_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov047 | `Ov047_RequestState` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov049 | `Ov049_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov054 | `Ov054_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov055 | `Ov055_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov058 | `Ov058_ApplyMode` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov060 | `Ov060_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov063 | `Ov063_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov064 | `Ov064_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov065 | `Ov065_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov066 | `Ov066_RequestState` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov068 | `Ov068_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov074 | `Ov074_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov075 | `Ov075_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov078 | `Ov078_ApplyMode` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov080 | `Ov080_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov082 | `Ov082_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov083 | `Ov083_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov084 | `Ov084_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov085 | `Ov085_RequestState` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov087 | `Ov087_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov091 | `Ov091_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov092 | `Ov092_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov095 | `Ov095_ApplyMode` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov097 | `Ov097_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov099 | `Ov099_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov100 | `Ov100_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov101 | `Ov101_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov102 | `Ov102_RequestState` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x020519b0` | ov104 | `Ov104_ApplyModeChange` | `Ov002_GetModeBlendFrames` | ov002 `Ov002_GetModeBlendFrames`, ov008 `Ov008_MenuInputDispatch_10`, ov009 `Ov009_ReleaseTwoSlotsEx` |
| `0x02056fb8` | main | `Game_EnterPauseScene`, `Scene_Leave` | `Ov002_HoldPanelScreen` | ov002 `Ov002_HoldPanelScreen`, ov008 `Ov008_UpdateDecimalDisplay` |
| `0x02061b18` | ov022 | `Ov022_UpdateRumble` | `Ov002_StopAllEmitters` | ov002 `Ov002_StopAllEmitters`, ov008 `Ov008_LiftTrackedNode` |
| `0x0206f6e4` | ov013 | `Ov013_TaskPollResourceReady` | `Ov002_SetLapRunning` | ov002 `Ov002_SetLapRunning`, ov008 `Ov008_LoadWeaponStats` |
| `0x0207fd2c` | ov022 | `Ov022_TrackWallPress` | `Ov013_SpawnAtOrientedOffset` | ov013 `Ov013_SpawnAtOrientedOffset`, ov014 `Ov014_DispatchTouchAction` |
| `0x020846c0` | ov012 | `Ov012_StartOpeningMovie` | `Ov024_MobiClip_OpenStreams` | ov008 `Ov008_RebuildShopList`, ov024 `Ov024_MobiClip_OpenStreams` |
| `0x02084b14` | ov012 | `Ov012_RunOpeningScene` | `Ov024_MobiClip_StopPlayback` | ov024 `Ov024_MobiClip_StopPlayback`, ov025 `Ov025_GetPageB` |
| `0x02086620` | ov002 | `Ov002_Camera_UpdateFollow`, `Ov002_TickCamera`, `Ov002_TickLockedCamera` | `Ov022_IsBit2SetVia0x20` | ov022 `Ov022_IsBit2SetVia0x20`, ov024 `Ov024_MobiClip_GetDecoderCodeCached` |
| `0x02086620` | ov106 | `Ov106_UpdateInteractPrompt` | `Ov022_IsBit2SetVia0x20` | ov022 `Ov022_IsBit2SetVia0x20`, ov024 `Ov024_MobiClip_GetDecoderCodeCached` |
| `0x02087298` | ov002 | `Ov002_StepRosterSlotRelease` | `func_ov022_02087298` | ov022 `func_ov022_02087298`, ov023 `Ov023_ActorStartMotion` |
| `0x020888ec` | ov002 | `Ov002_SetRosterHighlight`, `Ov002_SpareEntryHandleMessage`, `Ov002_SpareEntryStep`, `Ov002_SpareEntryTryBegin` | `func_ov022_020888ec` | ov022 `func_ov022_020888ec`, ov025 `Ov025_ReleaseTwoSlots_2` |
| `0x020b3220` | ov029 | `ov029_pointers_020b2f70` | `func_ov030_020b3220` | ov030 `func_ov030_020b3220`, ov031 `func_ov031_020b3220`, ov032 `func_ov032_020b3220`, ov033 `func_ov033_020b3220`, ov034 `func_ov034_020b3220`, ov035 `func_ov035_020b3220`, ov036 `func_ov036_020b3220`, ov037 `func_ov037_020b3220`, ov038 `func_ov038_020b3220`, ov039 `func_ov039_020b3220`, ov040 `func_ov040_020b3220`, ov041 `func_ov041_020b3220`, ov042 `func_ov042_020b3220`, ov043 `func_ov043_020b3220`, ov044 `func_ov044_020b3220`, ov045 `func_ov045_020b3220`, ov046 `func_ov046_020b3220`, ov047 `func_ov047_020b3220`, ov048 `func_ov048_020b3220`, ov049 `func_ov049_020b3220` |
| `0x020b5a20` | ov029 | `ov029_pointers_020b2f70` | `func_ov050_020b5a20` | ov050 `func_ov050_020b5a20`, ov051 `func_ov051_020b5a20`, ov052 `func_ov052_020b5a20`, ov053 `func_ov053_020b5a20`, ov054 `func_ov054_020b5a20`, ov055 `func_ov055_020b5a20`, ov056 `func_ov056_020b5a20`, ov057 `func_ov057_020b5a20`, ov058 `func_ov058_020b5a20`, ov059 `func_ov059_020b5a20`, ov060 `func_ov060_020b5a20`, ov061 `func_ov061_020b5a20`, ov062 `func_ov062_020b5a20`, ov063 `func_ov063_020b5a20`, ov064 `func_ov064_020b5a20`, ov065 `func_ov065_020b5a20`, ov066 `func_ov066_020b5a20`, ov067 `func_ov067_020b5a20`, ov068 `func_ov068_020b5a20` |
| `0x020b8100` | ov029 | `ov029_pointers_020b2f70` | `func_ov070_020b8100` | ov070 `func_ov070_020b8100`, ov071 `func_ov071_020b8100`, ov072 `func_ov072_020b8100`, ov073 `func_ov073_020b8100`, ov074 `func_ov074_020b8100`, ov075 `func_ov075_020b8100`, ov076 `func_ov076_020b8100`, ov077 `func_ov077_020b8100`, ov078 `func_ov078_020b8100`, ov079 `func_ov079_020b8100`, ov080 `func_ov080_020b8100`, ov081 `func_ov081_020b8100`, ov082 `func_ov082_020b8100`, ov083 `func_ov083_020b8100`, ov084 `func_ov084_020b8100`, ov085 `func_ov085_020b8100`, ov086 `func_ov086_020b8100`, ov087 `func_ov087_020b8100` |
| `0x020b8194` | ov022 | `Ov022_UpdateSubsystems` | `Ov106_SetField8CCC` | ov082 `Ov082_ShutdownAndFree`, ov106 `Ov106_SetField8CCC` |
| `0x020ba7c0` | ov029 | `ov029_pointers_020b2f70` | `func_ov088_020ba7c0` | ov088 `func_ov088_020ba7c0`, ov089 `func_ov089_020ba7c0`, ov090 `func_ov090_020ba7c0`, ov091 `func_ov091_020ba7c0`, ov092 `func_ov092_020ba7c0`, ov093 `func_ov093_020ba7c0`, ov094 `func_ov094_020ba7c0`, ov095 `func_ov095_020ba7c0`, ov096 `func_ov096_020ba7c0`, ov097 `func_ov097_020ba7c0`, ov098 `func_ov098_020ba7c0`, ov099 `func_ov099_020ba7c0`, ov100 `func_ov100_020ba7c0`, ov101 `func_ov101_020ba7c0`, ov102 `func_ov102_020ba7c0`, ov103 `func_ov103_020ba7c0`, ov104 `func_ov104_020ba7c0` |
| `0x020cc5a8` | ov016 | `Ov016_FollowerComplete` | `Ov233_NotifyPartsThenBase` | ov233 `Ov233_NotifyPartsThenBase`, ov291 `Ov291_RequestSubState6AndLatchTarget` |
| `0x020cc64c` | ov069 | `Ov069_TallyMissionRecords` | `Ov302_InitObjectWithList` | ov214 `Ov214_releaseHandles`, ov264 `Ov264_releaseHandles`, ov302 `Ov302_InitObjectWithList` |
| `0x020cc6dc` | ov069 | `Ov069_TallyMissionRecords` | `Ov302_FindListObjectWithField10Zero` | ov256 `Ov256_RemoveItemsFromScene`, ov302 `Ov302_FindListObjectWithField10Zero` |
| `0x020cc718` | ov069 | `Ov069_TallyMissionRecords` | `Ov302_GetId10` | ov123 `Ov123_InitNode`, ov302 `Ov302_GetId10` |
<!-- END generated:ambiguous -->

## 4. Game flow, as observed running the game

What the scene table's header does not list yet (it names 1, 2, 5, 7, 11 and
19):

- **Scene 10 (ov007)** follows the clock-tower cutscene of day 255: Roxas's
  narration over his portrait ("Como te imaginas, todo empieza por algún
  sitio.", advanced with A). Story Mode then goes 10 → 5 (the next day's
  title card) → 2.
- **Scene 6 (ov005)** is the results screen of Mission Mode ("Resultados",
  the mission's number and name, CATEGORÍA MISIÓN / MODO MISIÓN, PTOS.
  MISIÓN, P. CORAZÓN, PLATINES, EXP, Recompensas / Objetos recogidos). The
  directory is named `ov005_story_result`; what it showed was a Mission Mode
  mission.
- **Leaving a mission.** In the field (2): START opens PAUSA
  (Continuar / Retirarse); Retirarse asks "¿Seguro?" with No selected. Sí
  fades to a black screen, still scene 2, with a blinking "PULSA A". A goes
  to scene 6, the mission marked CANCELADA; A there asks "¿Cerrar el repaso
  de la misión?" (No selected; left is Sí), and Sí returns to the mission
  lobby (19).
