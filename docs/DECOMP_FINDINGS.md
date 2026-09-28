# Findings for khdays-decomp from the native build

The port's native build (`native/`, branch `spike/native-decomp`) compiles the
decompilation's C with MSVC for 32-bit x86 and runs it on a PC, with the DS
memory mapped at its real addresses and the hardware emulated underneath. The
C matches the ROM byte for byte, yet running it on another CPU shows where the
source does not say what the machine code does. This file collects those
places, and what running the game taught us, for the decompilation.

Every claim cites the ROM (addresses are ARM9; overlay code is given with its
overlay). Names are the decomp's own at the revision below. Sections marked
*generated* are refreshed by `python native/tools/decomp_findings.py`.

<!-- BEGIN generated:revision -->
Checked against khdays-decomp `fc1849474`.
<!-- END generated:revision -->

## 1. Calls resolved to the wrong overlay

Overlays share address ranges. A call into such a range runs whatever overlay
was loaded there last; the instruction holds only the address, so a matching
build cannot tell which overlay's function the C should name. The relocations
already say it is ambiguous (`module:overlays(8,24)`); the C then names one
candidate, and not always the one that is loaded.

**Confirmed at run time:** `Ov012_StartOpeningMovie` (ov012) opens the opening
movie with `Ov008_RebuildShopList(&request)` -- a `MobiClipOpenRequest` passed
to ov008's shop list. The call at `0x0205b1f0` reaches
`Ov024_MobiClip_OpenStreams` (ov024 is the MobiClip module, loaded for the
movie at the same address, `0x020846c0`). Natively the shop function ran and
crashed; with the call routed to ov024 the movie opens.

The native build now routes every such function reference through a thunk
that picks the overlay loaded at the time, so none of these blocks it. For the
decomp they are naming questions; with the real names several are visibly off
(e.g. ov022 gameplay code naming `Ov000_Title_CreateLogoObjects`). The
`ov029_pointers_020b2f70` rows are by design (a table of entry points into
whichever enemy overlay is loaded in a slot).

*Generated:* every function reference whose relocation lists several overlays
with a function at that address, the candidate the C names, and all of them.

<!-- BEGIN generated:ambiguous -->
| Address | Referring module | Referring functions | The C names | All candidates |
|---|---|---|---|---|
| `0x0204cac0` | ov022 | `Ov022_DrawSelectionMarkers`, `Ov022_DrawStateMarker`, `Ov022_ReadSelectionInput`, `Ov022_SetActorInputEnabled`, `Ov022_UpdateUiSelectionMarker`, `func_ov022_02086e80` | `Ov000_Title_CreateLogoObjects` | ov000 `Ov000_Title_CreateLogoObjects`, ov001 `Ov001_ClearVideoMemory`, ov002 `Ov002_ReaimActor`, ov003 `Ov003_IsGlobalBit0Set`, ov007 `Ov007_SetupSubDisplay`, ov008 `Ov008_GetPlayerMask`, ov009 `Ov009_ClassCtor`, ov010 `Ov010_BindResourceHandle` |
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
| `0x020846c0` | ov012 | `Ov012_StartOpeningMovie` | `Ov008_RebuildShopList` | ov008 `Ov008_RebuildShopList`, ov024 `Ov024_MobiClip_OpenStreams` |
| `0x02084b14` | ov012 | `Ov012_RunOpeningScene` | `Ov024_MobiClip_StopPlayback` | ov024 `Ov024_MobiClip_StopPlayback`, ov025 `Ov025_GetPageB` |
| `0x02086620` | ov002 | `Ov002_Camera_UpdateFollow`, `Ov002_TickCamera`, `Ov002_TickLockedCamera` | `Ov022_IsBit2SetVia0x20` | ov022 `Ov022_IsBit2SetVia0x20`, ov024 `Ov024_MobiClip_GetDecoderCodeCached` |
| `0x02086620` | ov106 | `Ov106_UpdateInteractPrompt` | `Ov022_IsBit2SetVia0x20` | ov022 `Ov022_IsBit2SetVia0x20`, ov024 `Ov024_MobiClip_GetDecoderCodeCached` |
| `0x02087298` | ov002 | `Ov002_StepRosterSlotRelease` | `func_ov022_02087298` | ov022 `func_ov022_02087298`, ov023 `Ov023_ActorStartMotion` |
| `0x020888ec` | ov002 | `Ov002_SetRosterHighlight`, `Ov002_SpareEntryHandleMessage`, `Ov002_SpareEntryStep`, `Ov002_SpareEntryTryBegin` | `func_ov022_020888ec` | ov022 `func_ov022_020888ec`, ov025 `Ov025_ReleaseTwoSlots_2` |
| `0x020b3220` | ov029 | `ov029_pointers_020b2f70` | `func_ov030_020b3220` | ov030 `func_ov030_020b3220`, ov031 `func_ov031_020b3220`, ov032 `func_ov032_020b3220`, ov033 `func_ov033_020b3220`, ov034 `func_ov034_020b3220`, ov035 `func_ov035_020b3220`, ov036 `func_ov036_020b3220`, ov037 `func_ov037_020b3220`, ov038 `func_ov038_020b3220`, ov039 `func_ov039_020b3220`, ov040 `func_ov040_020b3220`, ov041 `func_ov041_020b3220`, ov042 `func_ov042_020b3220`, ov043 `func_ov043_020b3220`, ov044 `func_ov044_020b3220`, ov045 `func_ov045_020b3220`, ov046 `func_ov046_020b3220`, ov047 `func_ov047_020b3220`, ov048 `func_ov048_020b3220`, ov049 `func_ov049_020b3220` |
| `0x020b5a20` | ov029 | `ov029_pointers_020b2f70` | `func_ov050_020b5a20` | ov050 `func_ov050_020b5a20`, ov051 `func_ov051_020b5a20`, ov052 `func_ov052_020b5a20`, ov053 `func_ov053_020b5a20`, ov054 `func_ov054_020b5a20`, ov055 `func_ov055_020b5a20`, ov056 `func_ov056_020b5a20`, ov057 `func_ov057_020b5a20`, ov058 `func_ov058_020b5a20`, ov059 `func_ov059_020b5a20`, ov060 `func_ov060_020b5a20`, ov061 `func_ov061_020b5a20`, ov062 `func_ov062_020b5a20`, ov063 `func_ov063_020b5a20`, ov064 `func_ov064_020b5a20`, ov065 `func_ov065_020b5a20`, ov066 `func_ov066_020b5a20`, ov067 `func_ov067_020b5a20`, ov068 `func_ov068_020b5a20` |
| `0x020b8100` | ov029 | `ov029_pointers_020b2f70` | `func_ov070_020b8100` | ov070 `func_ov070_020b8100`, ov071 `func_ov071_020b8100`, ov072 `func_ov072_020b8100`, ov073 `func_ov073_020b8100`, ov074 `func_ov074_020b8100`, ov075 `func_ov075_020b8100`, ov076 `func_ov076_020b8100`, ov077 `func_ov077_020b8100`, ov078 `func_ov078_020b8100`, ov079 `func_ov079_020b8100`, ov080 `func_ov080_020b8100`, ov081 `func_ov081_020b8100`, ov082 `func_ov082_020b8100`, ov083 `func_ov083_020b8100`, ov084 `func_ov084_020b8100`, ov085 `func_ov085_020b8100`, ov086 `func_ov086_020b8100`, ov087 `func_ov087_020b8100` |
| `0x020b8194` | ov022 | `Ov022_UpdateSubsystems` | `Ov082_ShutdownAndFree` | ov082 `Ov082_ShutdownAndFree`, ov106 `Ov106_SetField8CCC` |
| `0x020ba7c0` | ov029 | `ov029_pointers_020b2f70` | `func_ov088_020ba7c0` | ov088 `func_ov088_020ba7c0`, ov089 `func_ov089_020ba7c0`, ov090 `func_ov090_020ba7c0`, ov091 `func_ov091_020ba7c0`, ov092 `func_ov092_020ba7c0`, ov093 `func_ov093_020ba7c0`, ov094 `func_ov094_020ba7c0`, ov095 `func_ov095_020ba7c0`, ov096 `func_ov096_020ba7c0`, ov097 `func_ov097_020ba7c0`, ov098 `func_ov098_020ba7c0`, ov099 `func_ov099_020ba7c0`, ov100 `func_ov100_020ba7c0`, ov101 `func_ov101_020ba7c0`, ov102 `func_ov102_020ba7c0`, ov103 `func_ov103_020ba7c0`, ov104 `func_ov104_020ba7c0` |
| `0x020cc5a8` | ov016 | `Ov016_FollowerComplete` | `Ov233_NotifyPartsThenBase` | ov233 `Ov233_NotifyPartsThenBase`, ov291 `Ov291_RequestSubState6AndLatchTarget` |
| `0x020cc64c` | ov069 | `Ov069_TallyMissionRecords` | `Ov302_InitObjectWithList` | ov214 `Ov214_releaseHandles`, ov264 `Ov264_releaseHandles`, ov302 `Ov302_InitObjectWithList` |
| `0x020cc6dc` | ov069 | `Ov069_TallyMissionRecords` | `Ov302_FindListObjectWithField10Zero` | ov256 `Ov256_RemoveItemsFromScene`, ov302 `Ov302_FindListObjectWithField10Zero` |
| `0x020cc718` | ov069 | `Ov069_TallyMissionRecords` | `Ov302_GetId10` | ov123 `Ov123_InitNode`, ov302 `Ov302_GetId10` |
<!-- END generated:ambiguous -->

## 2. Calls that rely on what the ARM registers held

On the ARM9 the first four arguments travel in r0-r3 and the result in r0. C
that passes fewer arguments than the callee reads, or uses the "result" of a
function defined `void`, still compiles to the ROM's bytes: the callee finds
whatever the registers held, usually the result of the previous call. On any
other ABI those values are garbage.

The port scans the ROM for each such call and return (Ghidra, a backward
register slice; `native/abi/ghidra_abi.txt`, functions keyed by
`module@address`) and rewrites the C where the source of the value is
mechanical. Numbers at the revision above: see the generated lists below.

### 2.1 Checked by hand, with the ROM evidence

- **`Scene_AdvanceToPending`** unloads the ending scene's overlay with
  `UnloadOverlaySync(0)`; the ROM passes the overlay id as the second argument:
  `0x020209c4 ldr r1,[r4,#4]` (the scene entry), `0x020209cc ldr r1,[r1,#0]`
  (its `overlayId`). Should read `UnloadOverlaySync(0, s->entry->overlayId)`,
  as `Overlay105_Release` already does.
- **`Ov000_DestroyObject`** frees the node with
  `NNSi_FndFreeFromDefaultHeap()`; the ROM passes `b`: `0x02055a8e str r1,[sp,#0]`
  keeps it, `0x02055ac4 ldr r0,[sp,#0]` passes it.
- **`Ov012_RunOpeningScene`**, when the lid opens again, calls
  `func_0201e428(); SetMasterBrightnessMain(); func_0201e438(); SetMasterBrightnessSub();`.
  The ROM passes each saved value straight on (`0x0205b7a0`, `0x0205b7a8`):
  `SetMasterBrightnessMain(func_0201e428())`,
  `SetMasterBrightnessSub(func_0201e438())`; the two getters return the saved
  brightness.
- **`Session_Init_2`** calls `func_0203065c(); Session_LayoutPacketSlots();`;
  the ROM passes the first result on (`0x02030994 blx 0x0203065c`,
  `0x02030998` the call): `Session_LayoutPacketSlots(func_0203065c())` -- the
  link mode sizes the message slots. Natively the slots stayed unsized and a
  message was copied through a null buffer.
- **`Ov024_MobiClip_DecodeAudioEntryChecked_3`** is a four-argument
  pass-through: the ROM (`0x0208505c`) saves only r3/lr and calls
  `Ov024_MobiClip_BlitFrame` with r0-r3 untouched, which are what
  `Ov024_MobiClip_FrameAlarm` passes (decoder, buffer, `0x100`, 0). The C
  takes and passes one argument; `BlitFrame` takes four.
- **Empty functions used as object updates.** `Obj_UpdateAll` calls each
  update with the update's own address in r0 (`0x02023b60 ldr r0,[r1,#0x14]`,
  `blx r0`) and stores a nonzero result as the next update. An empty function
  (`bx lr`) therefore returns itself -- "stay in this state".
  `Ov006_MemberMenuNextStateNoOp` is such an update; as `void f(void) {}`
  natively it returned garbage (0x14) and the next frame jumped there. There
  are 176 functions `void f(void) {}` in the tree. A spelling that says what
  the ROM does, and that mwcc should still emit as a lone `bx lr`, is
  `void *f(void *self) { return self; }` (not verified against the compiler).
- **Struct returns** (not wrong, but not portable): `FS_GetOverlayFileID` and
  `NNSi_G2dFontGetTextRect` return two-word structs through the ARM ABI's
  hidden pointer, which their definitions or callers spell out by hand
  (`Text_AlignAnchor`, `Ov002_SceneLayoutPanelWindow`). x86 returns 8-byte
  structs in registers, so the port adjusts them.

### 2.2 All mechanical repairs (*generated*)

What the port passes or returns, per call, from the ROM. Each line is a place
where the C could say it explicitly.

<details><summary>repairs</summary>

<!-- BEGIN generated:repairs -->
```
BitArray_SetBit returns (int)bits
CamAnim_AllocPlayer returns the result of NNS_FndAllocFromAllocator
CollModel_FindEntry returns the result of FindEntryByNameNoCase
Collision_CastRay returns the result of Collision_RunRayCast
Collision_CastRayEx returns the result of Collision_RunRayCast
Collision_CastSimple returns the result of Collision_CastNearest
Collision_CastSphere returns the result of Collision_RunSphereCast
Collision_CastSphereEx returns the result of Collision_RunSphereCast
EntityMgr_RunCastSimple returns the result of Collision_CastNearest
EntityMgr_RunRayCast returns the result of Collision_RunRayCast
EntityMgr_RunSphereCast returns the result of Collision_RunSphereCast
EntityMgr_RunSphereCastSimple returns the result of Collision_RunSphereCast
GX_ResetBankForTex returns the result of resetBankForX_
GX_ResetBankForTexPltt returns the result of resetBankForX_
GameState_GetField returns the result of BitArray_GetField
Gfx_EnqueueSurface returns the result of GFXi_EnqueueCommand
InstantiateClass returns the result of RunClassConstructor
NNS_FndFreeToExpHeap returns the result of RecycleRegion
NNSi_FndFreeFromDefaultHeap returns the result of NNS_FndFreeToExpHeap
OS_FreeToHeap returns the result of OS_RestoreInterrupts
OSi_FreeStackAlloc returns the result of OS_FreeToHeap
Obj_ForwardInnerPayload returns the result of Text_DrawGlyph
Ov000_BackupWrite returns the result of CARD_GetResultCode
Ov000_EmitCommandAndStoreHandle returns the result of CARD_GetResultCode
Ov002_CreateRecordFromTemplate returns the result of Ov002_FindFreeElem
Ov002_UpdateFieldMusic returns the result of GameState_IsFlagSet
Ov006_ForwardRtcAvailability returns the result of Ov006_CountPlayersInMask
Ov008_CARD_TryWaitRomAsync returns the result of CARD_TryWaitRomAsync
Ov008_EmitCommandAndStoreHandle returns the result of CARD_GetResultCode
Ov008_FirstPositiveCountNodeOfList returns the result of Ov008_FirstPositiveCountNode
Ov008_ForwardRtcAvailability returns the result of Ov008_CountPlayersInMask
Ov008_GetNextMissionEntry returns the result of Ov008_FindNextMissionEntry
Ov008_GetNextMissionEntry_2 returns the result of Ov008_NthZeroCountNode
Ov008_GetNextMissionEntry_3 returns the result of Ov008_VarTable_GetRecordOfEntry
Ov008_GetNextMissionEntry_4 returns the result of func_ov008_0205665c
Ov008_GetNextMissionEntry_5 returns the result of Ov008_FindListObjectById
Ov008_Link_Poll returns the result of Obj_GetWord28
Ov008_MissionPollKeys returns the result of Ov008_CountPlayersInMask
Ov008_ReadInputHeader returns the result of Mem_ReadU16
Ov008_ResetEntry returns the result of Ov008_CodeToSlotTile
Ov008_SweepElements returns the result of Ov008_SweepFreeElementBuffers
Ov008_VarTable_GetRecordOfEntry returns the result of Ov008_GetVarRecordByIndex
Ov009_EmitCommandAndStoreHandle returns the result of CARD_GetResultCode
Ov011_InitTitleTileSurface returns the result of TileSurface_InitAndUpload4bpp
Ov011_SetupTitleTileSurfaces returns the result of Ov011_InitTitleTileSurface
Ov022_ForwardArg1 returns the result of IsArgEqualGlobalHalf4
Ov022_MarshalStateHalf12 returns the result of func_02031384
Ov022_ResetTimersAndMaybeSignal returns the result of Ov022_ActorSetState
Ov022_ResolveGuardBreakState returns the result of Ov022_ActorSetState
Ov024_MobiClip_DecoderInitTrampoline returns the result of Ov024_MobiClip_OpenContainer
Ov024_MobiClip_ResetStreamState returns (int)ctx
Ov025_EmitCommandAndStoreHandle returns the result of CARD_GetResultCode
Ov025_FirstPositiveCountNodeOfList returns the result of Ov025_FirstPositiveCountNode
Ov025_GetCurrentListId returns the result of Ov025_GetId10
Ov025_GetNextMissionEntry returns the result of Ov025_FindListObjectWithField10Zero
Ov025_GetNextMissionEntry_2 returns the result of Ov025_NthZeroCountNode
Ov025_GetNextMissionEntry_3 returns the result of Ov025_VarTable_GetRecordOfEntry
Ov025_GetNextMissionEntry_4 returns the result of func_ov025_0208a26c
Ov025_GetNextMissionEntry_5 returns the result of Ov025_FindListObjectById
Ov025_MissionList_HasVisibleInDays returns the result of Ov025_MissionList_HasVisibleMissionInDays
Ov025_PageA_GetVarRecord returns the result of Ov025_GetVarRecordByIndex
Ov025_SweepElements returns the result of Ov025_SweepFreeElementBuffers
Ov025_Tutorial_IsTopicUnlocked returns the result of GameState_IsFlagSet
Ov105_WHi_MeasureChannel returns the result of Ov105_WM_MeasureChannel
Ov107_Actor_DetachFromRegion returns the result of Ov107_RemoveChildFromRegion
Ov107_HandleRegionEvent returns the result of Ov107_RegisterChildInRegion
Ov107_MoveNodeAndRelayout returns the result of Ov107_UpdateCollisionSphere
Ov107_UpdateCollisionSphere returns the result of SphereToAABB
Ov302_AppendEligibleRecords returns the result of Ov302_ParseRecordListAppendMatches
Ov302_AppendRecordsBelowMax returns the result of Ov302_ParseRecordListAppendMatches
Ov302_AppendRecordsInRange returns the result of Ov302_ParseRecordListAppendMatches
Ov302_AppendRecordsKind4Id10 returns the result of Ov302_ParseRecordListAppendMatches
Ov302_AppendRecordsKindNonZero returns the result of Ov302_ParseRecordListAppendMatches
Ov302_AppendRecordsKindZero returns the result of Ov302_ParseRecordListAppendMatches
Ov302_AppendTriggerableInRange returns the result of Ov302_ParseRecordListAppendMatches
SND_GetFirstInstDataPos returns (int)p
SphereToAABB returns (int)dst
StoreGlobalByteAt0 returns (int)arg0
func_ov022_02089604 returns the result of Anim_GetFrame
func_ov022_020a1c28 returns the result of Ov022_ResolveReachSweep
func_ov022_020a1c80 returns the result of Ov022_ResolveReachSweep
Actor_SetVecAndSyncChild -> Node_SetPosAndNotify: passes src
Anim_GetFrame -> Anim_GetChannelState: passes khdays_arg0, khdays_arg1
Anim_GetLengthQ12 -> Anim_GetChannelState: passes khdays_arg0, khdays_arg1
CARDi_ReadRomSyncCore -> CARDi_CheckPulledOutCore: passes khdays_c0
CamAnim_SelectAnim -> NNS_G3dGetAnmByIdx: passes khdays_arg1
CollModel_GetEntryField14 -> CollModel_FindEntry: passes khdays_arg0
CommitCachedByteIfChanged -> ScriptVm_ReadOperandInt: passes khdays_arg1
ForwardToHandlerOrCurrentObject -> NNS_SndPlayerStopSeqBySeqArcIdx: passes khdays_arg1, khdays_arg2
Game_UpdateObjectMotion -> Obj_PrepAltTransform: passes obj
NNS_FndDestroyExpHeap -> NNSi_FndFinalizeHeap: passes khdays_arg0
NNS_FndDestroyFrmHeap -> NNSi_FndFinalizeHeap: passes khdays_arg0
Node_SetRotationFromMtx -> Quat_FromMtx33: passes khdays_arg1
Ov000_ArmAutoAdvanceTimer -> Ov000_WriteSlotHeaders: passes khdays_arg0
Ov000_EmitSplinePair -> Ov000_GetEntryPosition: passes param_1, param_2
Ov000_FillAnchorPair -> Ov000_GetEntryPosition: passes param_2
Ov000_InitFromDescAndMark -> ObjNode_InitFromDesc: passes khdays_arg1
Ov000_LookupTypeCode -> Slot4_GetIfOccupied: passes khdays_c0
Ov000_MarkSceneReady -> Ov000_BeginCardTransfer: passes khdays_arg0
Ov000_WaitSubMenuResult -> Ov000_SetSubSceneHalf1C: passes khdays_c0
Ov002_Actor_SetNodeEnabled -> Ov002_SetSceneNodeEnabled: passes khdays_arg1
Ov002_AnnounceSelection -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_AnnounceSelection -> Ov002_ForwardToSubDc_2: passes khdays_c0
Ov002_AnnounceWithSound -> Ov002_AnnounceSelection: passes arg1
Ov002_BuildOptionsPage -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_Camera_GetPresetHeight -> GetEntryField20ByIndex: passes khdays_c0
Ov002_ConfirmMission -> Ov002_World_IsFlagBitSet: passes khdays_c0
Ov002_DrawRecordLine -> Ov002_TryBeginPanelRequest: passes (int)0x0
Ov002_EnterDimmedScene -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_FindSiblingOfKindE -> Ov002_GetSlotTableByte: passes khdays_arg0
Ov002_GetCueEntryValue -> Ov002_GetCueEntry: passes khdays_arg0
Ov002_IsObjectFree -> Ov002_Element_IsAllowed: passes nSlot
Ov002_IsPlayerInTriggerRadius -> GetEntryField20ByIndex: passes khdays_c0
Ov002_IsRequestForUs -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_LinkPageRefresh -> Ov002_CountTextLines: passes khdays_c0
Ov002_LoadCueTable -> Ov002_RelocateResourceHeader: passes khdays_c0
Ov002_OpenConfirmPrompt -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_PrintHelpPage -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_PublishRequestKind9 -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_RecordMissionChoice -> Ov002_ClearCharBlock: passes khdays_c0
Ov002_RedrawOptionsPage -> Ov002_ForwardToSubDc_2: passes khdays_c0
Ov002_RedrawPartyStrip -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_RefreshCaptionWidget -> Ov002_ForwardToSubDc_2: passes khdays_c0
Ov002_RefreshCaptionWidget -> Ov002_ForwardToSubDc_3: passes khdays_c0
Ov002_ResetFaders -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_ScriptCmd_AdvanceRosterSetup -> Ov002_AdvanceRosterSetup: passes khdays_c0
Ov002_ScriptCmd_AdvanceRosterSetup -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_AppendPendingId -> Ov002_AppendPendingId: passes khdays_c0
Ov002_ScriptCmd_AppendPendingId -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_CreateFieldContext -> Ov002_CreateFieldContext: passes khdays_c0
Ov002_ScriptCmd_CreateFieldContext -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_DispatchStateEnter -> Ov002_DispatchStateEnter: passes khdays_c0
Ov002_ScriptCmd_DispatchStateEnter -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_EnterPhase -> Ov002_EnterPhase: passes khdays_c0
Ov002_ScriptCmd_EnterPhase -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_NotifyNodesOfKind -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_RecreateObjectSlot -> Ov002_RecreateObjectSlot: passes khdays_c0
Ov002_ScriptCmd_RecreateObjectSlot -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SetGlobalByte1F -> Ov002_SetGlobalByte1F: passes khdays_c0
Ov002_ScriptCmd_SetGlobalByte1F -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SetGlobalFlagOnce -> Ov002_SetGlobalFlagOnce: passes khdays_c0
Ov002_ScriptCmd_SetGlobalFlagOnce -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SetPendingText -> ByteCode_ResolveOperand: passes khdays_arg1
Ov002_ScriptCmd_SetSessionActive -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SetSessionIdle -> Ov002_SetSessionIdle: passes khdays_c0
Ov002_ScriptCmd_SetSessionIdle -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SetWorldByte8BAD -> Ov002_World_SetByte8BAD: passes khdays_c0
Ov002_ScriptCmd_SetWorldByte8BAD -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SetWorldByte8C9C -> Ov002_World_SetByte8C9C: passes khdays_c0
Ov002_ScriptCmd_SetWorldByte8C9C -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SetWorldByte8D68 -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SetWorldHalf8D5C -> Ov002_World_SetHalf8D5C: passes khdays_c0
Ov002_ScriptCmd_SetWorldHalf8D5C -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SkipIntOperand -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SkipIntOperand_2 -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_SnapshotPausedObject -> ByteCode_ResolveOperand: passes khdays_arg1
Ov002_ScriptCmd_SnapshotPausedObject -> Ov002_SnapshotPausedObject: passes khdays_c0
Ov002_ScriptCmd_SwitchPanelOverlay -> Ov002_SwitchPanelOverlay: passes khdays_c0
Ov002_ScriptCmd_SwitchPanelOverlay -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_ScriptCmd_UpdateSlotLookup -> Ov002_UpdateSlotLookup: passes khdays_c0
Ov002_ScriptCmd_UpdateSlotLookup -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov002_SetEmbeddedSceneNodeEnabled -> Ov002_SetSceneNodeEnabled: passes khdays_arg1
Ov002_SetEmbeddedSceneNodeEnabled_2 -> Ov002_SetSceneNodeEnabled: passes khdays_arg1
Ov002_SetPanelMode_2 -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_Slot_SetCellData -> Ov002_RebindSlotToCell: passes khdays_arg2, khdays_arg3
Ov002_StartCaptionVoice -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_StepCaptionScreen -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov002_TeardownBriefing -> Ov002_Ctx_InvokeTagTrackerCallback: passes khdays_c0
Ov004_LookupTypeCode -> Slot4_GetIfOccupied: passes khdays_c0
Ov004_QueryFieldBySelector -> Ov004_AppendEligibleRecords: passes khdays_arg2
Ov004_QueryFieldBySelector -> Ov004_AppendRecordsBelowMax: passes khdays_arg2
Ov004_QueryFieldBySelector -> Ov004_AppendRecordsInRange: passes khdays_arg2
Ov004_QueryFieldBySelector -> Ov004_AppendRecordsKind4Id10: passes khdays_arg2
Ov004_QueryFieldBySelector -> Ov004_AppendRecordsKindNonZero: passes khdays_arg2
Ov004_QueryFieldBySelector -> Ov004_AppendRecordsKindZero: passes khdays_arg2
Ov004_QueryFieldBySelector -> Ov004_AppendTriggerableInRange: passes khdays_arg2
Ov004_QueryFieldBySelector -> Ov004_FindBestRecordAppend: passes khdays_arg2, khdays_arg3
Ov005_ApplyOffsetSum -> Ov005_GetEntryBlock2c: passes param_2
Ov005_EmitSplinePair -> Ov005_ApplyFirstValidSlot: passes param_1, param_2
Ov005_FillAnchorPair -> Ov005_ApplyFirstValidSlot: passes param_2
Ov005_GetLocalMemberKind -> Slot4_GetIfOccupied: passes khdays_c0
Ov005_InitFromDescAndMark -> ObjNode_InitFromDesc: passes khdays_arg1
Ov005_LookupTypeCode -> Slot4_GetIfOccupied: passes khdays_c0
Ov005_QueryFieldBySelector -> Ov005_AppendEligibleRecords: passes khdays_arg2
Ov005_QueryFieldBySelector -> Ov005_AppendRecordsBelowMax: passes khdays_arg2
Ov005_QueryFieldBySelector -> Ov005_AppendRecordsInRange: passes khdays_arg2
Ov005_QueryFieldBySelector -> Ov005_AppendRecordsKind4Id10: passes khdays_arg2
Ov005_QueryFieldBySelector -> Ov005_AppendRecordsKindNonZero: passes khdays_arg2
Ov005_QueryFieldBySelector -> Ov005_AppendRecordsKindZero: passes khdays_arg2
Ov005_QueryFieldBySelector -> Ov005_AppendTriggerableInRange: passes khdays_arg2
Ov005_QueryFieldBySelector -> Ov005_FindBestRecordAppend: passes khdays_arg2, khdays_arg3
Ov005_ReleasePanelViewVeneer -> Ov005_ClearStateFreeLists: passes khdays_arg0
Ov008_ApplyOffsetSum -> Ov008_GetEntryBlock2c: passes param_2
Ov008_ClearSlotBit -> Ov008_MapCodeToSlotIndex: passes khdays_arg0
Ov008_DetailPanel_SetScroll -> Ov008_LayoutDetailPanel: passes param_2
Ov008_EmitSplinePair -> Ov008_GetEntryPos: passes param_1, param_2
Ov008_FillAnchorPair -> Ov008_GetEntryPos: passes param_2
Ov008_InitFromDescAndMark -> ObjNode_InitFromDesc: passes khdays_arg1
Ov008_InitPanel -> Ov008_SetBusyFlag: passes khdays_c0
Ov008_LookupTypeCode -> Slot4_GetIfOccupied: passes khdays_c0
Ov008_MarkSlotUsed -> Ov008_MapCodeToSlotIndex: passes khdays_arg0
Ov008_QueryFieldBySelector -> Ov008_AppendEligibleRecords: passes khdays_arg2
Ov008_QueryFieldBySelector -> Ov008_AppendRecordsBelowMax: passes khdays_arg2
Ov008_QueryFieldBySelector -> Ov008_AppendRecordsInRange: passes khdays_arg2
Ov008_QueryFieldBySelector -> Ov008_AppendRecordsKind4Id10: passes khdays_arg2
Ov008_QueryFieldBySelector -> Ov008_AppendRecordsKindNonZero: passes khdays_arg2
Ov008_QueryFieldBySelector -> Ov008_AppendRecordsKindZero: passes khdays_arg2
Ov008_QueryFieldBySelector -> Ov008_AppendTriggerableInRange: passes khdays_arg2
Ov008_QueryFieldBySelector -> Ov008_FindBestRecordAppend: passes khdays_arg2, khdays_arg3
Ov008_SetCtxField95fc -> Ov008_EnableBothHalves: passes value
Ov009_EmitSplinePair -> Ov009_ApplyFirstValidSlot: passes param_1, param_2
Ov009_FillAnchorPair -> Ov009_ApplyFirstValidSlot: passes param_2
Ov009_InitFromDescAndMark -> ObjNode_InitFromDesc: passes khdays_arg1
Ov009_LookupTypeCode -> Slot4_GetIfOccupied: passes khdays_c0
Ov009_MarkSlotUsed -> Ov009_MapCodeToSlotIndex: passes khdays_arg0
Ov009_QueryFieldBySelector -> Ov009_AppendEligibleRecords: passes khdays_arg2
Ov009_QueryFieldBySelector -> Ov009_AppendRecordsBelowMax: passes khdays_arg2
Ov009_QueryFieldBySelector -> Ov009_AppendRecordsInRange: passes khdays_arg2
Ov009_QueryFieldBySelector -> Ov009_AppendRecordsKind4Id10: passes khdays_arg2
Ov009_QueryFieldBySelector -> Ov009_AppendRecordsKindNonZero: passes khdays_arg2
Ov009_QueryFieldBySelector -> Ov009_AppendRecordsKindZero: passes khdays_arg2
Ov009_QueryFieldBySelector -> Ov009_AppendTriggerableInRange: passes khdays_arg2
Ov009_QueryFieldBySelector -> Ov009_FindBestRecordAppend: passes khdays_arg2, khdays_arg3
Ov012_InitAndDispatchTriple -> ByteCode_ResolveOperand: passes khdays_arg1
Ov022_GetStreamTimestamp -> GetEntryField20ByIndex: passes khdays_arg0
Ov022_StartPauseMenu -> Ov022_GetEntryField66: passes khdays_c0
Ov022_VeneerTo_Ov022_CopyBlock2c00 -> Ov022_CopyBlock2c00: passes khdays_arg0
Ov023_ActorFinish_2 -> Ov023_ActorFinish: passes khdays_arg0
Ov023_CmdEntry_SpawnEntityFollower -> Ov023_Cmd_SpawnEntityFollower: passes khdays_arg1
Ov023_CmdOpenDialog -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov023_CmdSetEventFlag -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov023_CmdSetGateFlag -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov023_CmdStoreGlobalValue -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov023_CmdStoreSlot48 -> Slot48_StoreAtCurrentIndex: passes khdays_arg1
Ov023_CmdStoreSlot48_2 -> Slot48_StoreAtCurrentIndex: passes khdays_arg1
Ov023_ScriptCmd_SetEventFlag -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov023_ScriptCmd_SetGateFlagWithSound -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov023_VmTickActor -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov024_CmdPrepareStream -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov024_CmdSetStreamByte -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov024_FreeStackAllocPassthrough -> StackAlloc_FreeIfSetB: passes a
Ov024_MobiClip_DecodeAudioEntryChecked_5 -> Ov024_MobiClip_StepAudio: passes khdays_arg1
Ov024_MobiClip_DecoderFreeBuffers -> func_ov024_02085e48: passes a
Ov024_MobiClip_DecoderFreeBuffers_2 -> Ov024_MobiClip_CloseContainer: passes a
Ov025_DetailPanel_SetScroll -> Ov025_LayoutDetailPanel: passes param_2
Ov025_EmitSplinePair -> Ov025_ApplyFirstValidSlot: passes param_1, param_2
Ov025_FillAnchorPair -> Ov025_ApplyFirstValidSlot: passes param_2
Ov025_InitFromDescAndMark -> ObjNode_InitFromDesc: passes khdays_arg1
Ov025_LookupTypeCode -> Slot4_GetIfOccupied: passes khdays_c0
Ov025_QueryFieldBySelector -> Ov025_AppendEligibleRecords: passes khdays_arg2
Ov025_QueryFieldBySelector -> Ov025_AppendRecordsBelowMax: passes khdays_arg2
Ov025_QueryFieldBySelector -> Ov025_AppendRecordsInRange: passes khdays_arg2
Ov025_QueryFieldBySelector -> Ov025_AppendRecordsKind4Id10: passes khdays_arg2
Ov025_QueryFieldBySelector -> Ov025_AppendRecordsKindNonZero: passes khdays_arg2
Ov025_QueryFieldBySelector -> Ov025_AppendRecordsKindZero: passes khdays_arg2
Ov025_QueryFieldBySelector -> Ov025_AppendTriggerableInRange: passes khdays_arg2
Ov025_QueryFieldBySelector -> Ov025_FindBestRecordAppend: passes khdays_arg2, khdays_arg3
Ov026_EmitSplinePair -> Ov026_ApplyFirstValidSlot: passes param_1, param_2
Ov026_FillAnchorPair -> Ov026_ApplyFirstValidSlot: passes param_2
Ov026_GetLocalMemberKind -> Slot4_GetIfOccupied: passes khdays_c0
Ov026_InitFromDescAndMark -> ObjNode_InitFromDesc: passes khdays_arg1
Ov026_QueryFieldBySelector -> Ov026_AppendEligibleRecords: passes khdays_arg2
Ov026_QueryFieldBySelector -> Ov026_AppendRecordsBelowMax: passes khdays_arg2
Ov026_QueryFieldBySelector -> Ov026_AppendRecordsInRange: passes khdays_arg2
Ov026_QueryFieldBySelector -> Ov026_AppendRecordsKind4Id10: passes khdays_arg2
Ov026_QueryFieldBySelector -> Ov026_AppendRecordsKindNonZero: passes khdays_arg2
Ov026_QueryFieldBySelector -> Ov026_AppendRecordsKindZero: passes khdays_arg2
Ov026_QueryFieldBySelector -> Ov026_AppendTriggerableInRange: passes khdays_arg2
Ov026_QueryFieldBySelector -> Ov026_FindBestRecordAppend: passes khdays_arg2, khdays_arg3
Ov032_UnloadEnemyOverlay -> Ov032_DisposeAndFreeChild: passes khdays_c0
Ov033_ClearStateIfReadyWhenActive -> Sequence_UpdateTracks: passes khdays_arg1
Ov035_TickTwoPhaseAnimOfSlot -> Ov035_TickTwoPhaseAnim: passes khdays_arg2
Ov040_ForwardToThreeSubHandlers -> Ov040_StepSequenceSlot: passes arg2
Ov042_ClearState1IfReady -> Sequence_UpdateTracks: passes khdays_arg1
Ov044_ForwardArmPlayerTarget -> Ov044_ArmPlayerTarget: passes khdays_arg1
Ov051_ClearStateIfReadyWhenActive -> Sequence_UpdateTracks: passes khdays_arg1
Ov052_UnloadEnemyOverlay -> Ov052_DisposeAndFreeChild: passes khdays_c0
Ov054_TickTwoPhaseAnimOfSlot -> Ov054_TickTwoPhaseAnim: passes khdays_arg2
Ov059_ForwardToThreeSubHandlers -> Ov059_StepSequenceSlot: passes arg2
Ov061_ClearState1IfReady -> Sequence_UpdateTracks: passes khdays_arg1
Ov063_ForwardArmPlayerTarget -> Ov063_ArmPlayerTarget: passes khdays_arg1
Ov069_LookupTypeCode -> Slot4_GetIfOccupied: passes khdays_c0
Ov069_MarkCurrentItemEquipped -> ScriptVm_ReadOperandInt: passes khdays_arg0, khdays_arg1
Ov071_ClearStateIfReadyWhenActive -> Sequence_UpdateTracks: passes khdays_arg1
Ov072_UnloadEnemyOverlay -> Ov072_DisposeAndFreeChild: passes khdays_c0
Ov074_TickTwoPhaseAnimOfSlot -> Ov074_TickTwoPhaseAnim: passes khdays_arg2
Ov079_ForwardToThreeSubHandlers -> Ov079_StepSequenceSlot: passes arg2
Ov081_ClearState1IfReady -> Sequence_UpdateTracks: passes khdays_arg1
Ov082_ForwardArmPlayerTarget -> Ov082_ArmPlayerTarget: passes khdays_arg1
Ov089_ClearStateIfReadyWhenActive -> Sequence_UpdateTracks: passes khdays_arg1
Ov091_TickTwoPhaseAnimOfSlot -> Ov091_TickTwoPhaseAnim: passes khdays_arg2
Ov096_ForwardToThreeSubHandlers -> Ov096_StepSequenceSlot: passes arg2
Ov098_ClearState1IfReady -> Sequence_UpdateTracks: passes khdays_arg1
Ov099_ForwardArmPlayerTarget -> Ov099_ArmPlayerTarget: passes khdays_arg1
Ov106_CmdSetGateFlag -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov106_ScriptCmd_SetGateFlagWithSound -> ScriptVm_ReadOperandInt: passes khdays_arg1
Ov107_InstantiateFieldClass -> InstantiateClass: passes khdays_arg1
Ov107_MoveNodeAndRelayout -> Srt_SetTranslation: passes v
Ov107_RefreshAndSelectChild -> RefreshObjectCallbacks: passes khdays_arg1
Ov107_TrackJointMotion -> Obj_RenderModel: passes khdays_arg1
Ov114_WindupTick -> Ov114_PerformSwingSweep: passes (int)0x1
Ov131_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov132_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov133_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov141_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov142_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov143_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov147_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov147_ResetSetupAndPropagate -> RefreshObjectCallbacks: passes arg2
Ov148_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov148_ResetSetupAndPropagate -> RefreshObjectCallbacks: passes arg2
Ov149_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov150_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov151_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov152_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov161_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov162_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov163_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov164_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov165_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov178_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov179_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov180_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov181_StepWindUp -> Ov181_SwingSweep: passes (int)0x1
Ov182_StepWindUp -> Ov182_SwingSweep: passes (int)0x1
Ov183_StepWindUp -> Ov183_SwingSweep: passes (int)0x1
Ov184_StepWindUp -> Ov184_SwingSweep: passes (int)0x1
Ov197_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov197_ResetSetupAndPropagate -> RefreshObjectCallbacks: passes arg2
Ov198_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov198_ResetSetupAndPropagate -> RefreshObjectCallbacks: passes arg2
Ov199_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov199_ResetSetupAndPropagate -> RefreshObjectCallbacks: passes arg2
Ov202_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov203_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov206_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov207_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov208_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov209_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov212_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov214_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov214_resetChildAndDispatch -> RefreshObjectCallbacks: passes param2
Ov215_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov215_resetChildAndDispatch -> RefreshObjectCallbacks: passes param2
Ov216_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov216_resetChildAndDispatch -> RefreshObjectCallbacks: passes param2
Ov217_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov217_resetChildAndDispatch -> RefreshObjectCallbacks: passes param2
Ov221_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov222_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov223_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov224_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov225_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov226_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov227_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov228_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov229_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov230_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov233_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov235_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov235_FinishWhenFlagClear -> Task_MarkFinished: passes obj
Ov236_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov236_HandleMessage -> Ov236_SpawnReactionTaskFromHit: passes self
Ov239_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov240_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov244_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov244_WindupTick -> Ov244_PerformSwingSweep: passes (int)0x1
Ov245_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov245_FourShape_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov245_FourShape_AiStep_QueueAction0OnAnimEndB -> SetIndexedSlot: passes (int)0x0
Ov245_Variant_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov248_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov248_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov249_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov254_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov254_AiStep_QueueAction0OnAnimEnd_2 -> SetIndexedSlot: passes (int)0x0
Ov254_AiStep_QueueAction0OnAnimEnd_3 -> SetIndexedSlot: passes (int)0x0
Ov254_AiStep_QueueAction0OnAnimEnd_4 -> SetIndexedSlot: passes (int)0x0
Ov255_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov257_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov258_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov260_SettleTick -> Collision_CastRayEx: passes (int)0x0
Ov264_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov264_resetChildAndDispatch -> RefreshObjectCallbacks: passes param2
Ov266_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov267_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov268_AiStep_QueueAction0OnAnimEnd -> SetIndexedSlot: passes (int)0x0
Ov274_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov275_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov276_AiStep_QueueAction0 -> SetIndexedSlot: passes (int)0x0
Ov277_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov277_WindupTick -> Ov277_PerformSwingSweep: passes (int)0x1
Ov278_FinishIfSubFlagClear -> Task_MarkFinished: passes obj
Ov278_HandleMessage -> Ov278_SpawnReactionTaskFromHit: passes self
Ov298_Pose1SetupWithTimerThenAdvance -> Rand16NextScaled: passes (int)0x1fe0
Ov302_QueryFieldBySelector -> Ov302_SelectAndSpawnEncounter: passes khdays_arg3
PMi_SetLEDAsync -> PM_SendUtilityCommandAsync: passes khdays_arg1, khdays_arg2
ScriptCmd_DispatchToHandler -> ScriptVm_ResolveOperand: passes khdays_arg1
ScriptCmd_DispatchToHandler -> dispatchToHandlerAtOffset: passes khdays_c0
ScriptCmd_FreeSlotEntry -> ScriptVm_ReadOperandInt: passes khdays_arg1
ScriptCmd_QueueSoundKind1 -> ScriptVm_ReadOperandInt: passes khdays_arg0, khdays_arg1
ScriptCmd_StoreDoubled -> ScriptVm_ReadOperandInt: passes khdays_arg1
ScriptVm_ReadOperandFx32 -> ScriptVm_ResolveOperand: passes khdays_arg0, khdays_arg1
ScriptVm_ReadOperandInt -> ScriptVm_ResolveOperand: passes khdays_arg1
Session_Init_2 -> Session_LayoutPacketSlots: passes khdays_c0
SlotTable_AddEntry -> SlotTable_FindFree: passes a0
Srt_SetRotationAxisAngle -> QuatFromAxisAngle: passes khdays_arg1, khdays_arg2
StoreField74ThenForward -> ModelInst_Init: passes khdays_arg3
Text_VSNPrintf -> Text_VSNPrintf_2: passes khdays_arg0, khdays_arg1, khdays_arg2, khdays_arg3
Vec3TransformViaTempMtx -> Mtx33_FromQuat: passes unused
func_020116e4 -> GetUnpackedAnimBankImpl_: passes khdays_arg0, khdays_arg1
func_0201696c -> NNS_FndFreeToAllocator: passes khdays_arg0, khdays_arg1
func_0201a55c -> ShutdownPlayer: passes khdays_arg0
func_0202019c -> StrNCaseCmp: passes khdays_arg0, khdays_arg1, khdays_arg2
func_02023768 -> NNS_FndResizeForMBlockExpHeap: passes khdays_arg1, khdays_arg2
func_02023ad0 -> Obj_Destroy: passes khdays_arg0
func_0202afe8 -> SceneNode_JointCallback: passes khdays_arg0
func_0202c604 -> ListPushFront: passes khdays_arg1
func_0202c614 -> DList_Unlink: passes khdays_arg1
func_0203243c -> Obj_LoadResourceNode: passes khdays_arg1
func_02032444 -> SlotTable_AddEntry: passes khdays_arg0, khdays_arg1, khdays_arg2
func_ov000_02058360 -> Ov000_ClearStateFreeLists: passes khdays_arg0
func_ov004_0204ecec -> Ov004_ClearStateFreeLists: passes khdays_arg0
func_ov005_0204e0b0 -> func_0203243c: passes khdays_arg0, khdays_arg1
func_ov008_02053464 -> Ov008_ClearStateFreeLists: passes khdays_arg0
func_ov008_0205475c -> func_0203243c: passes khdays_arg0, khdays_arg1
func_ov008_0205665c -> Ov008_GetVarRecordByIndex: passes khdays_arg0, khdays_arg1
func_ov008_020594c4 -> Ov008_HandleCancelFlags: passes khdays_arg0
func_ov008_0205968c -> Ov008_Menu_AdvanceIntoPanel: passes khdays_arg0, khdays_arg1, khdays_arg2, khdays_arg3
func_ov008_0205c574 -> Ov008_TweenSlotValue: passes khdays_arg0, khdays_arg1, khdays_arg2, khdays_arg3
func_ov008_020676a0 -> Ov008_SweepElements: passes khdays_c0
func_ov008_02078154 -> Ov008_MissionMenuBack: passes khdays_arg0
func_ov008_020782c8 -> Ov008_SweepElements: passes khdays_c0
func_ov009_020507d4 -> Ov009_ClearStateFreeLists: passes khdays_arg0
func_ov012_0205bb78 -> TileTextRenderer_Destroy: passes khdays_arg0
func_ov022_020b15a4 -> Ov022_BuildResNodeSet: passes khdays_arg0, khdays_arg1
func_ov024_020835cc -> TileTextRenderer_Destroy: passes khdays_arg0
func_ov024_02083d00 -> NNSi_FndFreeFromDefaultHeap: passes khdays_arg0
func_ov024_020850f8 -> Ov024_MobiClip_Alloc: passes khdays_arg0
func_ov024_02085104 -> func_ov024_02083d00: passes khdays_arg0
func_ov024_02085e3c -> Ov024_MobiClip_Alloc: passes khdays_arg0
func_ov024_02085e48 -> func_ov024_02083d00: passes khdays_arg0
func_ov025_02087254 -> Ov025_ClearStateFreeLists: passes khdays_arg0
func_ov025_02088410 -> func_0203243c: passes khdays_arg1
func_ov025_0208a26c -> Ov025_GetVarRecordByIndex: passes khdays_arg0, khdays_arg1
func_ov025_0208bdf8 -> Ov025_MenuBack: passes khdays_arg0
func_ov025_0208eb08 -> Ov025_BlitClampedSlot: passes khdays_arg0, khdays_arg1, khdays_arg2, khdays_arg3
func_ov025_02099a80 -> Ov025_SweepElements: passes khdays_c0
func_ov025_020ad7ac -> Ov025_TeardownOrInit: passes khdays_arg0
func_ov025_020aebbc -> Ov025_ScrollList_Confirm: passes khdays_arg0
func_ov025_020aed20 -> Ov025_SweepElements: passes khdays_c0
func_ov028_0208ab14 -> Ov028_RC4_EncryptInstructions: passes khdays_arg0, khdays_arg1, khdays_arg2, khdays_arg3
func_ov035_020b3b08 -> Ov035_BindRig: passes khdays_arg0
func_ov036_020b3928 -> Ov036_BindRig: passes khdays_arg0
func_ov039_020b3e80 -> Ov039_BindRig: passes khdays_arg0
func_ov046_020b3a70 -> Ov046_BindRig: passes khdays_arg0
func_ov054_020b6308 -> Ov054_BindRig: passes khdays_arg0
func_ov055_020b6128 -> Ov055_BindRig: passes khdays_arg0
func_ov058_020b6680 -> Ov058_BindRig: passes khdays_arg0
func_ov065_020b6270 -> Ov065_BindRig: passes khdays_arg0
func_ov069_020ba244 -> Ov069_ClearStateFreeLists: passes khdays_arg0
func_ov074_020b89e8 -> Ov074_BindRig: passes khdays_arg0
func_ov075_020b8808 -> Ov075_BindRig: passes khdays_arg0
func_ov078_020b8d60 -> Ov078_BindRig: passes khdays_arg0
func_ov084_020b8950 -> Ov084_BindRig: passes khdays_arg0
func_ov091_020bb0a8 -> Ov091_BindRig: passes khdays_arg0
func_ov092_020baec8 -> Ov092_BindRig: passes khdays_arg0
func_ov095_020bb420 -> Ov095_BindRig: passes khdays_arg0
func_ov101_020bb010 -> Ov101_BindRig: passes khdays_arg0
func_ov105_020bf900 -> Ov105_WH_StateInSetMPData: passes khdays_arg0, khdays_arg1, khdays_arg2
func_ov107_020c3190 -> FreeInstanceMemory: passes khdays_arg0
func_ov107_020c9c1c -> func_02023ad0: passes khdays_arg0
Anim_GetFrame takes 2 parameters (its own r0..r1 pass through)
Anim_GetLengthQ12 takes 2 parameters (its own r0..r1 pass through)
CamAnim_SelectAnim takes 2 parameters (its own r1..r1 pass through)
CollModel_GetEntryField14 takes 1 parameters (its own r0..r0 pass through)
CommitCachedByteIfChanged takes 2 parameters (its own r1..r1 pass through)
ForwardToHandlerOrCurrentObject takes 3 parameters (its own r1..r2 pass through)
NNS_FndDestroyExpHeap takes 1 parameters (its own r0..r0 pass through)
NNS_FndDestroyFrmHeap takes 1 parameters (its own r0..r0 pass through)
Node_SetRotationFromMtx takes 2 parameters (its own r1..r1 pass through)
Ov000_ArmAutoAdvanceTimer takes 1 parameters (its own r0..r0 pass through)
Ov000_InitFromDescAndMark takes 2 parameters (its own r1..r1 pass through)
Ov000_MarkSceneReady takes 1 parameters (its own r0..r0 pass through)
Ov002_Actor_SetNodeEnabled takes 2 parameters (its own r1..r1 pass through)
Ov002_FindSiblingOfKindE takes 1 parameters (its own r0..r0 pass through)
Ov002_GetCueEntryValue takes 1 parameters (its own r0..r0 pass through)
Ov002_IsRequestForUs takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_AdvanceRosterSetup takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_AppendPendingId takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_CreateFieldContext takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_DispatchStateEnter takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_EnterPhase takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_NotifyNodesOfKind takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_RecreateObjectSlot takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetGlobalByte1F takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetGlobalFlagOnce takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetPendingText takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetSessionActive takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetSessionIdle takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetWorldByte8BAD takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetWorldByte8C9C takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetWorldByte8D68 takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SetWorldHalf8D5C takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SkipIntOperand takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SkipIntOperand_2 takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SnapshotPausedObject takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_SwitchPanelOverlay takes 2 parameters (its own r1..r1 pass through)
Ov002_ScriptCmd_UpdateSlotLookup takes 2 parameters (its own r1..r1 pass through)
Ov002_SetEmbeddedSceneNodeEnabled takes 2 parameters (its own r1..r1 pass through)
Ov002_SetEmbeddedSceneNodeEnabled_2 takes 2 parameters (its own r1..r1 pass through)
Ov002_Slot_SetCellData takes 4 parameters (its own r2..r3 pass through)
Ov004_QueryFieldBySelector takes 4 parameters (its own r2..r3 pass through)
Ov005_InitFromDescAndMark takes 2 parameters (its own r1..r1 pass through)
Ov005_QueryFieldBySelector takes 4 parameters (its own r2..r3 pass through)
Ov005_ReleasePanelViewVeneer takes 1 parameters (its own r0..r0 pass through)
Ov008_ClearSlotBit takes 1 parameters (its own r0..r0 pass through)
Ov008_InitFromDescAndMark takes 2 parameters (its own r1..r1 pass through)
Ov008_MarkSlotUsed takes 1 parameters (its own r0..r0 pass through)
Ov008_QueryFieldBySelector takes 4 parameters (its own r2..r3 pass through)
Ov009_InitFromDescAndMark takes 2 parameters (its own r1..r1 pass through)
Ov009_MarkSlotUsed takes 1 parameters (its own r0..r0 pass through)
Ov009_QueryFieldBySelector takes 4 parameters (its own r2..r3 pass through)
Ov012_InitAndDispatchTriple takes 2 parameters (its own r1..r1 pass through)
Ov022_GetStreamTimestamp takes 1 parameters (its own r0..r0 pass through)
Ov022_VeneerTo_Ov022_CopyBlock2c00 takes 1 parameters (its own r0..r0 pass through)
Ov023_ActorFinish_2 takes 1 parameters (its own r0..r0 pass through)
Ov023_CmdEntry_SpawnEntityFollower takes 2 parameters (its own r1..r1 pass through)
Ov023_CmdOpenDialog takes 2 parameters (its own r1..r1 pass through)
Ov023_CmdSetEventFlag takes 2 parameters (its own r1..r1 pass through)
Ov023_CmdSetGateFlag takes 2 parameters (its own r1..r1 pass through)
Ov023_CmdStoreGlobalValue takes 2 parameters (its own r1..r1 pass through)
Ov023_CmdStoreSlot48 takes 2 parameters (its own r1..r1 pass through)
Ov023_CmdStoreSlot48_2 takes 2 parameters (its own r1..r1 pass through)
Ov023_ScriptCmd_SetEventFlag takes 2 parameters (its own r1..r1 pass through)
Ov023_ScriptCmd_SetGateFlagWithSound takes 2 parameters (its own r1..r1 pass through)
Ov023_VmTickActor takes 2 parameters (its own r1..r1 pass through)
Ov024_CmdPrepareStream takes 2 parameters (its own r1..r1 pass through)
Ov024_CmdSetStreamByte takes 2 parameters (its own r1..r1 pass through)
Ov024_MobiClip_DecodeAudioEntryChecked_5 takes 2 parameters (its own r1..r1 pass through)
Ov025_InitFromDescAndMark takes 2 parameters (its own r1..r1 pass through)
Ov025_QueryFieldBySelector takes 4 parameters (its own r2..r3 pass through)
Ov026_InitFromDescAndMark takes 2 parameters (its own r1..r1 pass through)
Ov026_QueryFieldBySelector takes 4 parameters (its own r2..r3 pass through)
Ov033_ClearStateIfReadyWhenActive takes 2 parameters (its own r1..r1 pass through)
Ov035_TickTwoPhaseAnimOfSlot takes 3 parameters (its own r2..r2 pass through)
Ov042_ClearState1IfReady takes 2 parameters (its own r1..r1 pass through)
Ov044_ForwardArmPlayerTarget takes 2 parameters (its own r1..r1 pass through)
Ov051_ClearStateIfReadyWhenActive takes 2 parameters (its own r1..r1 pass through)
Ov054_TickTwoPhaseAnimOfSlot takes 3 parameters (its own r2..r2 pass through)
Ov061_ClearState1IfReady takes 2 parameters (its own r1..r1 pass through)
Ov063_ForwardArmPlayerTarget takes 2 parameters (its own r1..r1 pass through)
Ov069_MarkCurrentItemEquipped takes 2 parameters (its own r0..r1 pass through)
Ov071_ClearStateIfReadyWhenActive takes 2 parameters (its own r1..r1 pass through)
Ov074_TickTwoPhaseAnimOfSlot takes 3 parameters (its own r2..r2 pass through)
Ov081_ClearState1IfReady takes 2 parameters (its own r1..r1 pass through)
Ov082_ForwardArmPlayerTarget takes 2 parameters (its own r1..r1 pass through)
Ov089_ClearStateIfReadyWhenActive takes 2 parameters (its own r1..r1 pass through)
Ov091_TickTwoPhaseAnimOfSlot takes 3 parameters (its own r2..r2 pass through)
Ov098_ClearState1IfReady takes 2 parameters (its own r1..r1 pass through)
Ov099_ForwardArmPlayerTarget takes 2 parameters (its own r1..r1 pass through)
Ov106_CmdSetGateFlag takes 2 parameters (its own r1..r1 pass through)
Ov106_ScriptCmd_SetGateFlagWithSound takes 2 parameters (its own r1..r1 pass through)
Ov107_InstantiateFieldClass takes 2 parameters (its own r0..r1 pass through)
Ov107_RefreshAndSelectChild takes 2 parameters (its own r1..r1 pass through)
Ov107_TrackJointMotion takes 2 parameters (its own r1..r1 pass through)
Ov302_QueryFieldBySelector takes 4 parameters (its own r3..r3 pass through)
PMi_SetLEDAsync takes 3 parameters (its own r1..r2 pass through)
ScriptCmd_DispatchToHandler takes 2 parameters (its own r1..r1 pass through)
ScriptCmd_FreeSlotEntry takes 2 parameters (its own r1..r1 pass through)
ScriptCmd_QueueSoundKind1 takes 2 parameters (its own r0..r1 pass through)
ScriptCmd_StoreDoubled takes 2 parameters (its own r1..r1 pass through)
ScriptVm_ReadOperandFx32 takes 2 parameters (its own r0..r1 pass through)
ScriptVm_ReadOperandInt takes 2 parameters (its own r1..r1 pass through)
Srt_SetRotationAxisAngle takes 3 parameters (its own r1..r2 pass through)
StoreField74ThenForward takes 4 parameters (its own r3..r3 pass through)
Text_VSNPrintf takes 4 parameters (its own r0..r3 pass through)
func_020116e4 takes 2 parameters (its own r0..r1 pass through)
func_0201696c takes 2 parameters (its own r0..r1 pass through)
func_0201a55c takes 1 parameters (its own r0..r0 pass through)
func_0202019c takes 3 parameters (its own r0..r2 pass through)
func_02023768 takes 3 parameters (its own r1..r2 pass through)
func_02023ad0 takes 1 parameters (its own r0..r0 pass through)
func_0202afe8 takes 1 parameters (its own r0..r0 pass through)
func_0202c604 takes 2 parameters (its own r1..r1 pass through)
func_0202c614 takes 2 parameters (its own r1..r1 pass through)
func_0203243c takes 2 parameters (its own r1..r1 pass through)
func_02032444 takes 3 parameters (its own r0..r2 pass through)
func_ov000_02058360 takes 1 parameters (its own r0..r0 pass through)
func_ov004_0204ecec takes 1 parameters (its own r0..r0 pass through)
func_ov005_0204e0b0 takes 2 parameters (its own r0..r1 pass through)
func_ov008_02053464 takes 1 parameters (its own r0..r0 pass through)
func_ov008_0205475c takes 2 parameters (its own r0..r1 pass through)
func_ov008_0205665c takes 2 parameters (its own r0..r1 pass through)
func_ov008_020594c4 takes 1 parameters (its own r0..r0 pass through)
func_ov008_0205968c takes 4 parameters (its own r0..r3 pass through)
func_ov008_0205c574 takes 4 parameters (its own r0..r3 pass through)
func_ov008_02078154 takes 1 parameters (its own r0..r0 pass through)
func_ov009_020507d4 takes 1 parameters (its own r0..r0 pass through)
func_ov012_0205bb78 takes 1 parameters (its own r0..r0 pass through)
func_ov022_020b15a4 takes 2 parameters (its own r0..r1 pass through)
func_ov024_020835cc takes 1 parameters (its own r0..r0 pass through)
func_ov024_02083d00 takes 1 parameters (its own r0..r0 pass through)
func_ov024_020850f8 takes 1 parameters (its own r0..r0 pass through)
func_ov024_02085104 takes 1 parameters (its own r0..r0 pass through)
func_ov024_02085e3c takes 1 parameters (its own r0..r0 pass through)
func_ov024_02085e48 takes 1 parameters (its own r0..r0 pass through)
func_ov025_02087254 takes 1 parameters (its own r0..r0 pass through)
func_ov025_02088410 takes 2 parameters (its own r1..r1 pass through)
func_ov025_0208a26c takes 2 parameters (its own r0..r1 pass through)
func_ov025_0208bdf8 takes 1 parameters (its own r0..r0 pass through)
func_ov025_0208eb08 takes 4 parameters (its own r0..r3 pass through)
func_ov025_020ad7ac takes 1 parameters (its own r0..r0 pass through)
func_ov025_020aebbc takes 1 parameters (its own r0..r0 pass through)
func_ov028_0208ab14 takes 4 parameters (its own r0..r3 pass through)
func_ov035_020b3b08 takes 1 parameters (its own r0..r0 pass through)
func_ov036_020b3928 takes 1 parameters (its own r0..r0 pass through)
func_ov039_020b3e80 takes 1 parameters (its own r0..r0 pass through)
func_ov046_020b3a70 takes 1 parameters (its own r0..r0 pass through)
func_ov054_020b6308 takes 1 parameters (its own r0..r0 pass through)
func_ov055_020b6128 takes 1 parameters (its own r0..r0 pass through)
func_ov058_020b6680 takes 1 parameters (its own r0..r0 pass through)
func_ov065_020b6270 takes 1 parameters (its own r0..r0 pass through)
func_ov069_020ba244 takes 1 parameters (its own r0..r0 pass through)
func_ov074_020b89e8 takes 1 parameters (its own r0..r0 pass through)
func_ov075_020b8808 takes 1 parameters (its own r0..r0 pass through)
func_ov078_020b8d60 takes 1 parameters (its own r0..r0 pass through)
func_ov084_020b8950 takes 1 parameters (its own r0..r0 pass through)
func_ov091_020bb0a8 takes 1 parameters (its own r0..r0 pass through)
func_ov092_020baec8 takes 1 parameters (its own r0..r0 pass through)
func_ov095_020bb420 takes 1 parameters (its own r0..r0 pass through)
func_ov101_020bb010 takes 1 parameters (its own r0..r0 pass through)
func_ov105_020bf900 takes 3 parameters (its own r0..r2 pass through)
func_ov107_020c3190 takes 1 parameters (its own r0..r0 pass through)
func_ov107_020c9c1c takes 1 parameters (its own r0..r0 pass through)
```
<!-- END generated:repairs -->

</details>

### 2.3 Not resolved mechanically (*generated*)

The port stops the game if one of these is reached, and resolves it from the
ROM by hand then (as in 2.1).

<details><summary>gaps</summary>

<!-- BEGIN generated:gaps -->
```
ClearGlobalArrayInt's value: sources: 02030d1c add r0,r1,r0, lsl #0x2
DispatchWithReentrantScratch's value: sources: 0202e518 ldr r0,[0x202e538] = 0x20475d0 DAT_020475d0 = 0x0 Reset; 0202e52c bl 0x0202e474 => 0x202e474 RigWork_Update
FSi_WaitForCardThread's value: sources: 01ff8110 ldr r0,[0x1ff8124] = 0x27e0078 DAT_027e0078 = 0x0 Reset
FreeInstanceMemory's value: sources: 0203d1b0 bl 0x020236ac => 0x20236ac NNSi_FndFreeFromDefaultHeap; entry:r0
ModelAnimSet_Bind's value: sources: 0202a378 ldr r0,[sp,#0x4]
Ov002_AbortSession's value: sources: arm9_ov002::0206ba50 mvn r0,#0x0; arm9_ov002::0206ba6c bl 0x02088474
Ov002_FillMapRows's value: sources: arm9_ov002::02053b68 bl 0x02053bb8 => 0x2053bb8 Ov002_GetItemResource; arm9_ov002::02053b9c bl 0x02053c18 => 0x2053c18 Ov002_WriteMapRow
Ov002_FindSiblingOfKindE's value: sources: arm9_ov002::02072bb2 blx 0x02076688 => 0x2076688 Ov002_List_GetWord; arm9_ov002::02072bc4 ldr r0,[r0,#0x4]
Ov008_SweepFreeElementBuffers's value: sources: arm9_ov008::020557e0 ldr r0,[r5,#0x38]; arm9_ov008::020557fa ldr r0,[r5,#0x38]
Ov011_SetupTitleBackgrounds's value: sources: arm9_ov011::0205da30 orr r0,r0,#0x33
Ov024_MobiClip_OpenContainer's value: definition not found
Ov025_SweepFreeElementBuffers's value: sources: arm9_ov025::02089488 ldr r0,[r5,#0x38]; arm9_ov025::020894a2 ldr r0,[r5,#0x38]
Ov107_InvokeSlot0x74's value: sources: arm9_ov107::020c2b48 blx r2; entry:r0
Ov107_ProcessObjectTick's value: sources: arm9_ov107::020c6a3c add r0,r0,#0x1; arm9_ov107::020c6ab4 add r0,r0,#0x1; arm9_ov107::020c6b28 add r0,r0,#0x1; arm9_ov107::020c6b8c add r0,r0,#0x1; arm9_ov107::020c6bf0 add r0,r0,#0x1; arm9_ov107::020c6c94 add r0,r0,#0x1; arm9_ov107::020c7358 bic r0,r0,#0x2; arm9_ov107::020c736c orr r0,r0,#0x2
Ov107_RegisterChildInRegion's value: sources: arm9_ov107::020c4e9e add r0,#0x9c; arm9_ov107::020c4eae blx 0x0203bfb4 => 0x203bfb4 RegisterSubscriberSlot
Ov107_RemoveChildFromRegion's value: sources: arm9_ov107::020c4eb6 add r0,#0x9c; arm9_ov107::020c4ec6 blx 0x0203bfe8 => 0x203bfe8 RemoveChildFromListByPtr
Ov237_RotateByActorHeading's value: sources: arm9_ov237::020cdbc8 ldmia r5,{r0,r1,r2}
Ov252_TurnVecY's value: sources: arm9_ov252::020cdb6c ldmia r4,{r0,r1,r2}
SetIndexedSlot's value: sources: 0203c634 add r0,r0,r1, lsl #0x2
SetSubitemState's value: sources: 0203ba24 ldrsh r0,[r2,r0]; 0203ba68 bic r0,r0,#0x1; 0203ba78 add r0,r0,r4, lsl #0x2; 0203bab0 orr r0,r0,#0x2; entry:r0
SoundMgr_Update's value: sources: 020333a4 bl 0x02019bf0 => 0x2019bf0 NNS_SndMain; tail 02032f78 addls pc,pc,r1, lsl #0x2; tail 02032fb0 addls pc,pc,r0, lsl #0x2; tail 02033034 addls pc,pc,r1, lsl #0x2; tail 020331d0 addls pc,pc,r0, lsl #0x2
ResSlot_ReleaseResource called from FreeAllResourceTables without what the ROM passes (argument 0: 0202a4ac ldr r0,[r7,#0xc])
Ov002_IsPanelModeSet called from Ov002_ApplyTally without what the ROM passes (no call in the C)
Ov002_IsPanelModeSet called from Ov002_CreateAndRestoreHud without what the ROM passes (Ov002_RepublishHud gives no value to pass)
Ov002_AddPanelCounter called from Ov002_DispatchHudCounterCommand without what the ROM passes (argument 3: arm9_ov002::0206ace6 ldr r3,[0x0206adcc] = 0x206f5c0 Ov002_GetStartTicks = 0xe92d4010; arm9_ov002::0206acf0 ldr r3,[0x0206add0] = 0x206f630 Ov002_GetEndTicks = 0xe92d4010; arm9_ov002::0206acfa ldr r3,[0x0206add4] = 0x206f604 Ov002_GetTimeoutTicks = 0xe59f101c; arm9_ov002::0206ad32 ldr r3,[0x0206adcc] = 0x206f5c0 Ov002_GetStartTicks = 0xe92d4010; arm9_ov002::0206ad3c ldr r3,[0x0206add0] = 0x206f630 Ov002_GetEndTicks = 0xe92d4010; arm9_ov002::0206ad46 ldr r3,[0x0206add4] = 0x206f604 Ov002_GetTimeoutTicks = 0xe59f101c; arm9_ov002::0206ad6c ldr r3,[0x0206add8] = 0x206dda8 Ov002_GetTimeoutRemaining = 0xe92d4010; arm9_ov002::0206ad9a ldr r3,[0x0206addc] = 0x206dde4 Ov002_GetRemainingTicks = 0xe92d4010)
Ov002_ForwardToSubDc_4 called from Ov002_HudSetSlotValue without what the ROM passes (no call in the C)
Ov002_LoadPanelSlots called from Ov002_OpenPanelScreen without what the ROM passes (argument 2: arm9_ov002::0205674a blx 0x02023930 => 0x2023930 InstantiateClass)
Ov002_RequestPanelScreen called from Ov002_PostScoreRecord without what the ROM passes (argument 4: stack)
Ov002_GetWord20 called from Ov002_SceneStepPanel without what the ROM passes (argument 0: arm9_ov002::020616b8 moveq r0,#0x0)
Ov002_PostCrawlScoreLine called from Ov002_ScriptCmd_PostCrawlScoreLine without what the ROM passes (previous callee 0x2088474 has no symbol)
NNSi_FndFreeFromDefaultHeap called from Ov004_FreeWorkBuffers without what the ROM passes (argument 0: arm9_ov004::0204ccc4 ldr r0,[r4,#0x3c]; arm9_ov004::0204ccdc ldr r0,[r4,#0x40]; arm9_ov004::0204ccf4 ldr r0,[r4,#0x44]; arm9_ov004::0204cd0c ldr r0,[r4,#0x48])
Ov004_QueryFieldBySelector called from Ov004_InitObjectWithList without what the ROM passes (argument 3: arm9_ov004::0204d29c bl 0x0201ef9c => 0x201ef9c Archive_LoadFile)
NNSi_FndFreeFromDefaultHeap called from Ov005_FreeWorkBuffers without what the ROM passes (argument 0: arm9_ov005::0204e6f8 ldr r0,[r4,#0x3c]; arm9_ov005::0204e710 ldr r0,[r4,#0x40]; arm9_ov005::0204e728 ldr r0,[r4,#0x44]; arm9_ov005::0204e740 ldr r0,[r4,#0x48])
Ov005_QueryFieldBySelector called from Ov005_InitObjectWithList without what the ROM passes (argument 3: arm9_ov005::0204ecd0 bl 0x0201ef9c => 0x201ef9c Archive_LoadFile)
NNSi_FndFreeFromDefaultHeap called from Ov008_FreeWorkBuffers without what the ROM passes (argument 0: arm9_ov008::02055f94 ldr r0,[r4,#0x3c]; arm9_ov008::02055fac ldr r0,[r4,#0x40]; arm9_ov008::02055fc4 ldr r0,[r4,#0x44]; arm9_ov008::02055fdc ldr r0,[r4,#0x48])
Ov008_QueryFieldBySelector called from Ov008_InitMissionList without what the ROM passes (argument 3: arm9_ov008::0205656c bl 0x0201ef9c => 0x201ef9c Archive_LoadFile)
Ov008_QueryFieldBySelector called from Ov008_RebuildQueryList without what the ROM passes (argument 3: arm9_ov008::02056700 bl 0x02056418 => 0x2056418 ov008_DestroyAllListObjects)
NNSi_FndFreeFromDefaultHeap called from Ov009_FreeWorkBuffers without what the ROM passes (argument 0: arm9_ov009::02055080 ldr r0,[r4,#0x3c]; arm9_ov009::02055098 ldr r0,[r4,#0x40]; arm9_ov009::020550b0 ldr r0,[r4,#0x44]; arm9_ov009::020550c8 ldr r0,[r4,#0x48])
Ov009_QueryFieldBySelector called from Ov009_InitObjectWithList without what the ROM passes (argument 3: arm9_ov009::02055658 bl 0x0201ef9c => 0x201ef9c Archive_LoadFile)
Ov011_BlitTileRow called from Ov011_StepPaneScroll without what the ROM passes (argument 5: stack)
Ov011_TickLayoutAnimator called from Ov011_TickTitleMenu without what the ROM passes (argument 0: arm9_ov011::0205cc00 mvneq r0,#0x1)
func_02023ad0 called from Ov023_RebuildSubObject without what the ROM passes (argument 0: arm9_ov023::02084034 ldr r0,[r1,r0])
SetMasterBrightnessMain called from Ov024_MobiClip_UpdatePlayback without what the ROM passes (argument 0: arm9_ov024::02083084 bl 0x0201e428 => 0x201e428 LoadGlobalS8_027e0084; arm9_ov024::0208319c bl 0x02005760 => 0x2005760 GXx_GetMasterBrightness_)
SetMasterBrightnessSub called from Ov024_MobiClip_UpdatePlayback without what the ROM passes (argument 0: arm9_ov024::0208308c bl 0x0201e438 => 0x201e438 LoadGlobalS8At1_027e0084; arm9_ov024::020831a8 bl 0x02005760 => 0x2005760 GXx_GetMasterBrightness_)
NNSi_FndFreeFromDefaultHeap called from Ov025_FreeWorkBuffers without what the ROM passes (argument 0: arm9_ov025::02089ba4 ldr r0,[r4,#0x3c]; arm9_ov025::02089bbc ldr r0,[r4,#0x40]; arm9_ov025::02089bd4 ldr r0,[r4,#0x44]; arm9_ov025::02089bec ldr r0,[r4,#0x48])
Ov025_QueryFieldBySelector called from Ov025_InitObjectWithList without what the ROM passes (argument 3: arm9_ov025::0208a17c bl 0x0201ef9c => 0x201ef9c Archive_LoadFile)
Ov025_QueryFieldBySelector called from Ov025_RebuildQueryList without what the ROM passes (argument 3: arm9_ov025::0208a310 bl 0x0208a028 => 0x208a028 ov025_DestroyAllListObjects)
NNSi_FndFreeFromDefaultHeap called from Ov026_FreeWorkBuffers without what the ROM passes (argument 0: arm9_ov026::020846d8 ldr r0,[r4,#0x3c]; arm9_ov026::020846f0 ldr r0,[r4,#0x40]; arm9_ov026::02084708 ldr r0,[r4,#0x44]; arm9_ov026::02084720 ldr r0,[r4,#0x48])
Ov026_QueryFieldBySelector called from Ov026_InitObjectWithList without what the ROM passes (argument 3: arm9_ov026::02084cb0 bl 0x0201ef9c => 0x201ef9c Archive_LoadFile)
Ov105_SetField30IfModeAllows called from Ov105_RunStep3 without what the ROM passes (argument 0: arm9_ov105::020be4e4 moveq r0,#0x1)
Ov105_SetField30IfModeAllows called from Ov105_RunStep3OnSecondHandle without what the ROM passes (argument 0: arm9_ov105::020bf690 moveq r0,#0x1)
Ov105_SetField30IfModeAllows called from Ov105_RunStep3OnThirdHandle without what the ROM passes (argument 0: arm9_ov105::020bec98 moveq r0,#0x1)
Ov105_SetField30IfModeAllows called from Ov105_RunStep3ViaBackend without what the ROM passes (argument 0: arm9_ov105::020be5a0 moveq r0,#0x1)
Ov105_SetField30IfModeAllows called from Ov105_StepOrFallBack without what the ROM passes (argument 0: arm9_ov105::020bf6b8 ldrh r0,[r0,#0x2]; arm9_ov105::020bf6d8 bl 0x020bd558 => 0x20bd558 Ov105_SetSessionCallback)
Ov226_Projectile_SetupFlight called from Ov226_HandleMessageArgs without what the ROM passes (argument 4: stack)
Collision_CastSphereEx called from Ov228_AiIntegrateMotion without what the ROM passes (argument 4: stack)
Collision_CastSphereEx called from Ov229_AiIntegrateMotion without what the ROM passes (argument 4: stack)
Collision_CastSphereEx called from Ov230_AiIntegrateMotion without what the ROM passes (argument 4: stack)
Collision_CastSphereEx called from Ov233_AiIntegrateMotion without what the ROM passes (argument 4: stack)
Collision_CastSphereEx called from Ov248_AiIntegrateMotion without what the ROM passes (argument 4: stack)
Collision_CastSphereEx called from Ov249_AiIntegrateMotion without what the ROM passes (argument 4: stack)
Collision_CastSphereEx called from Ov276_ProbeBoxAhead without what the ROM passes (argument 4: stack)
NNSi_FndFreeFromDefaultHeap called from Ov302_FreeWorkBuffers without what the ROM passes (argument 0: arm9_ov302::020cc0b4 ldr r0,[r4,#0x3c]; arm9_ov302::020cc0cc ldr r0,[r4,#0x40]; arm9_ov302::020cc0e4 ldr r0,[r4,#0x44]; arm9_ov302::020cc0fc ldr r0,[r4,#0x48])
Ov302_QueryFieldBySelector called from Ov302_InitObjectWithList without what the ROM passes (argument 3: arm9_ov302::020cc68c bl 0x0201ef9c => 0x201ef9c Archive_LoadFile)
SoundMgr_StartStream called from Scene_Leave without what the ROM passes (argument 1: 02023074 ldr r1,[r0,#0x0])
```
<!-- END generated:gaps -->

</details>

## 3. C++ (the MobiClip readers in ov024)

Not wrong in the decomp, but worth recording next to the class declarations:

- mwcc gives a virtual destructor **two** vtable slots, the complete-object
  destructor and then the deleting one: `delete p` loads slot 1
  (`Ov024_MobiClip_MakeStream` at `0x02084ec4`: `ldr r1,[r0]; ldr r1,[r1,#4]; blx r1`), and
  `data_ov024_020939c4` holds exactly those two. Compilers with one slot per
  virtual destructor (MSVC) put every later method one slot early.
- The vtable entries are C functions taking `this` as their first argument
  (r0), which the class declarations rely on.

## 4. Requests

- **MobiClip's frame decoder as C.** `Ov024_MobiClip_DecodeFrame`
  (`0x020859d4`) calls into ITCM at `0x01ff9a64`: `data_ov024_0208c8c4`
  (`mobiclip_payload.s`, 0x659c bytes of position-independent ARM code)
  copied there by `Ov024_MobiClip_GetDecoderCodeCached`. It is the only game
  code a native build cannot run, and it blocks the story's start (the
  opening movie). For checking a decompilation of it: FFmpeg's `mobiclip`
  decoder reproduces this game's luma byte for byte against the DS's VRAM.
  Naming: `Ov024_MobiClip_OpenContainer` stores that copy as
  `pDecoder->pQuantTables`, but it is code -- `Ov024_MobiClip_DecodeFrame`
  calls it through the pointer at `+0x38`.
- **The link layer in local (single-player) mode.** See 5, mission lobby.

## 5. Game flow, as observed running the game

- **Scene ids** (`StoreGlobalPairAt10` / `Scene_AdvanceToPending`). The scene
  table at `0x02042548` ({overlay id, class descriptor}, 8 bytes each) maps
  1 → ov000, 2 → ov002, 3 → ov003, 5 → ov004, 6 → ov005, 7 → ov006,
  8 → ov011, 9 → ov009, 10 → ov007, 11 → ov012, 12 → ov010, 19 → ov008
  (0, 4 and 13-18 have none). Seen running: 1 is the logos, the title, all
  its menus and the save-file screen; 7 the Mission Mode character select;
  11 the opening movie (after "new game" in Story Mode); 5 the day title
  card that follows it ("Día 255 ~El sol se pone rojo~"); 2 the field, where
  the clock-tower cutscene of day 255 plays (`Ov002_BeginMissionRun`,
  `Ov002_SessionTick`); 19 the mission lobby (after START on the character
  select).
- **`func_01ff80a8` is the game's VBlank count**: it returns the counter at
  `data_027e0088`, which `OSi_VBlankInterruptHandler` increments (the struct
  there is {counter, callback list}). `Ov012_RunOpeningScene`'s playback loop
  spins on it; `func_02001020` stores its argument there (its old name,
  srand, was a shape match).
- **New game and DS Protect.** `Ov000_BootRunSelector` (`0x0204ee24`) with
  selector `0x191` loads ov028 and requests scene 11 only if
  `func_ov028_0208b490` (always true), `func_ov028_0208b120` (not a
  flashcart: ROM pages below 0x8000 must mirror 0x8000) and
  `func_ov028_0208b2e0` (not an emulator) all pass; otherwise it requests no
  scene at all and the game sits on a black screen with no error. The
  emulator test (`func_ov028_0208abd0`) fails for an all-zero MAC (read by
  `OS_GetMacAddress` from `0x027ffcf4`) or for `00:09:BF:00:00:31` with a 1/1
  birthday and no nickname.
- **The opening scene's script waits on the movie.** `Game_RunActionScript`
  stands on a command (op `03.03`) whose action is `Ov012_MayWaitFrames`: while
  `Ov012_IsRequestStateTwo` holds (the movie state at `+0x8bd8`/`+0x8bdc` is 2)
  and not enough frames are buffered it waits. If `Ov024_MobiClip_OpenStreams`
  fails, `Ov012_StartOpeningMovie` only sets flag 2 and `Ov012_RunOpeningScene`
  skips the block that would set state 3, so the script waits forever: a movie
  that fails to open is not a path the game recovers from.
- **Mission lobby.** `Ov008_MissionLobbyStartTransfer` moves to
  `Ov008_MissionLobbyPoll`, which waits for `+0x4f4` of the lobby context,
  set by `Ov008_Link_RequestLeave` (`0x0207b908`). In single player the
  message it waits for has to come back through the link layer's local loop
  (`func_0203065c` returns the link mode; mode 1 copies sent messages into the
  local slots). Natively it does not come back yet; under investigation.
- **The ARM7's X/Y word** at `0x027fffa8` reads `0x2c00` with nothing held and
  the lid open (every DeSmuME savestate of this game): X, Y and debug in bits
  10, 11 and 13, active low; the hinge in bit 15 (1 = closed). Game code reads
  it together with KEYINPUT (`(KEYINPUT | *0x027fffa8) ^ 0x2fff`).

## 6. Smaller things

- **`Ov001_SeedMathRandContexts`** reads `r` and `x` before writing them
  (`x ^= v`, `tmp = r ^ v`): the seeds start from whatever the registers held.
  Harmless for a random seed, but the source says nothing about it; a
  run-time checker flags it.
- **The C library defines C runtime names** (`strlen`, `strncmp`, `strncpy`,
  `strtol`, `abs` in `libs/msl`). Fine for the DS; any host build has to
  rename them.

## 7. Resolved since earlier reports

- The twelve data tables a renaming pass had dropped from `delinks.txt` are
  back (`45e840239`).
