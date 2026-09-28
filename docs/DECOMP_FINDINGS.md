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
Checked against khdays-decomp `54a63ca0e`.
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

*Generated:* each function, the parameters some file declares narrower than
its definition, and the files that do.

<!-- BEGIN generated:narrowparams -->
- `Anim_GetFrame` (argument 1 as u16): `Ov023_ActorQueueMotion`, `Ov023_ActorUpdateMotions`, `Ov252_FetchArmourRecords`
- `Anim_GetLengthQ12` (argument 1 as u16): `Ov002_UpdatePeerAnimationsAndExit`, `Ov022_StepStandingShot`, `Ov023_ActorQueueMotion`, `Ov023_ActorUpdateMotions`
- `Anim_SetFrameWrapped` (argument 1 as u16): `Ov002_ApplyAnimMode`, `Ov002_RebindAnimTracks`, `Ov002_UpdatePeerAnimationsAndExit`, `Ov022_BindBlockAnimations`, `Ov022_ReleaseRigSlots`, `Ov023_ActorUpdateMotions`, `Ov030_RebindEmitterSlots`, `Ov032_EffectBlockStop`, `Ov036_ApplyChargeLevelOnce`, `Ov038_RearmSequenceSlots`, `Ov043_RebindAnimTracks`, `Ov044_RebindEmitterSlots`, `Ov048_RebindAnimTracks`, `Ov052_EffectBlockStop`, `Ov055_ApplyChargeLevelOnce`, `Ov057_BindSwingSequencePhase`, `Ov062_RebindAnimTracks`, `Ov063_RebindEmitterSlots`, `Ov067_RebindAnimTracks`, `Ov072_EffectBlockStop`, `Ov075_ApplyChargeLevelOnce`, `Ov077_RearmSequenceMode`, `Ov082_RebindEmitterSlots`, `Ov086_RebindAnimTracks`, `Ov092_ApplyChargeLevelOnce`, `Ov094_RearmSequenceSlots`, `Ov099_RebindEmitterSlots`, `Ov103_RebindAnimTracks`, `Ov252_SwapArmourAnim`
- `ArrayEntryPtrD0` (argument 0 as u16): `Ov002_ApplyRosterSlotToNode`, `Ov023_ActorFinish`, `Ov023_CmdAttachWeapons`, `Ov023_CmdPlaceActor`, `Ov023_CmdPlaceRelative`, `Ov023_CmdRampActorTransition`, `Ov023_CmdRampAnimFrame`, `Ov023_CmdSeatMembers`, `Ov023_CmdStopCamera`, `Ov023_CmdWarpActor`, `Ov023_Cmd_FaceEntityTowards`, `Ov023_Cmd_NudgeEntityByCamera`, `Ov023_Cmd_ParentEntityToEntity`, `Ov023_Cmd_PlayEntityAnimKind1`, `Ov023_Cmd_PlayNamedMotionOrNode`, `Ov023_Cmd_SeekEntityAnim`, `Ov023_Cmd_SetEntityLookupValue`, `Ov023_Cmd_SetEntityScale`, `Ov023_Cmd_StartEntityMotionParam`, `Ov023_Cmd_StartNamedMotion`, `Ov023_Cmd_TestEntityCollisionBit`, `Ov023_PlaceActorModel`, `Ov023_ReadTargetPosition`
- `BitArray_SetField` (argument 3 as u16): `Slot_EvalPackedParamWith`
- `CARD_LockRom` (argument 0 as u16): `FSi_RomArchiveProc`
- `CARD_UnlockBackup` (argument 0 as u16): `Ov000_Backup_WriteChunked`, `Ov000_WriteSlotHeaders`, `Ov008_Backup_WriteChunked`, `Ov008_CommitSaveToSlot`, `Ov009_Backup_WriteChunked`, `Ov009_CommitSaveToSlot`, `Ov025_Backup_WriteChunked`, `Ov025_CommitSaveToSlot`
- `CARD_UnlockRom` (argument 0 as u16): `FSi_RomArchiveProc`
- `CTRDGi_LockByProcessor` (argument 0 as u16): `CTRDG_IsExisting`, `CTRDGi_InitModuleInfo`, `CTRDGi_IsAgbCartridgeAtInit`, `CTRDGi_RestoreAccessCycle`, `CTRDGi_SendtoPxi`
- `CTRDGi_UnlockByProcessor` (argument 0 as u16): `CTRDG_IsExisting`, `CTRDGi_InitModuleInfo`, `CTRDGi_IsAgbCartridgeAtInit`, `CTRDGi_RestoreAccessCycle`, `CTRDGi_SendtoPxi`
- `Callbacks_SetByte` (argument 0 as u8): `PauseMenu_Open`, `PauseMenu_PollInput`
- `CardUnlockAfterKeyShare` (argument 0 as u16): `Ov000_Backup_WriteChunked`, `Ov000_WriteSlotHeaders`, `Ov008_Backup_WriteChunked`, `Ov008_CommitSaveToSlot`, `Ov009_Backup_WriteChunked`, `Ov009_CommitSaveToSlot`, `Ov025_Backup_WriteChunked`, `Ov025_CommitSaveToSlot`
- `EntityMgr_AttachTrackData` (argument 0 as u16): `Ov002_ConfigureGateFromPeerRow`
- `EntityMgr_FindCollEntry` (argument 0 as u16): `Ov002_ResolveNamedPlacement`, `Ov002_ScriptPlaceSlot`
- `EntityMgr_GetCollEntryField14` (argument 0 as u16): `Ov002_SpawnActorElement`
- `EntityMgr_LinkChild` (argument 0 as u16, argument 1 as u16): `Ov023_CmdAttachWeapons`, `Ov023_Cmd_ParentEntityToEntity`
- `EntityMgr_ProbeGround` (argument 0 as u16): `Ov002_ResolveActorModelAndBones`, `Ov002_SpawnActorElement`, `Ov023_CmdSeatMembers`, `Ov023_CmdSetAnchor`, `Ov023_Cmd_FaceEntityTowards`, `Ov023_ReadTargetPosition`
- `EntityMgr_RunCastSimple` (argument 0 as u16): `Ov022_ResolveReachSweep`
- `EntityMgr_RunRayCast` (argument 0 as u16): `Ov002_TestRosterSlotGroundRay`, `Ov022_DropGroundMark`, `Ov022_ResolveReachSweep`
- `EntityMgr_RunSphereCast` (argument 0 as u16): `Ov022_DrivePartnerTarget`, `Ov022_FindClimbTarget`, `Ov022_ResolveReachSweep`
- `EntityMgr_SetTransition` (argument 0 as u16): `Ov023_CmdRampActorTransition`, `Ov023_Cmd_PlayEntityAnimKind1`
- `Entity_Activate` (argument 1 as u16): `Ov002_BindSlotActorModel`, `Ov002_BuildSpawnRow`
- `Entity_ForwardToSlot` (argument 0 as u16, argument 1 as u16): `Ov023_CmdSetupPartyActors`, `Ov023_Cmd_StartEntityMotionParam`, `Ov030_CreateActor`, `Ov031_ActorCtor`, `Ov032_Boot`, `Ov033_Boot`, `Ov034_Boot`, `Ov036_Boot`, `Ov037_Boot`, `Ov038_BuildRigObject`, `Ov039_Boot`, `Ov040_BuildRigObject`, `Ov041_BuildRigObject`, `Ov042_BuildRigObject`, `Ov043_Construct`, `Ov044_InitPanelObject`, `Ov045_Boot`, `Ov046_BuildRigObject`, `Ov047_PanelCtor`, `Ov048_Boot`, `Ov049_Boot`, `Ov050_ActorCtor`, `Ov051_Boot`, `Ov052_Boot`, `Ov053_Boot`, `Ov055_Boot`, `Ov056_Boot`, `Ov057_BuildRigObject`, `Ov058_Boot`, `Ov059_BuildRigObject`, `Ov060_BuildRigObject`, `Ov061_BuildRigObject`, `Ov062_Construct`, `Ov063_InitPanelObject`, `Ov064_Boot`, `Ov065_BuildRigObject`, `Ov066_PanelCtor`, `Ov067_Boot`, `Ov068_Boot`, `Ov070_ActorCtor`, `Ov071_Boot`, `Ov072_Boot`, `Ov073_Boot`, `Ov075_Boot`, `Ov076_Boot`, `Ov077_BuildRigObject`, `Ov078_Boot`, `Ov079_BuildRigObject`, `Ov080_BuildRigObject`, `Ov081_BuildRigObject`, `Ov082_InitPanelObject`, `Ov083_Boot`, `Ov084_BuildRigObject`, `Ov085_PanelCtor`, `Ov086_Boot`, `Ov087_Boot`, `Ov088_ActorCtor`, `Ov089_Boot`, `Ov090_Boot`, `Ov092_Boot`, `Ov093_Boot`, `Ov094_BuildRigObject`, `Ov095_Boot`, `Ov096_BuildRigObject`, `Ov097_BuildRigObject`, `Ov098_BuildRigObject`, `Ov099_InitPanelObject`, `Ov100_Boot`, `Ov101_BuildRigObject`, `Ov102_PanelCtor`, `Ov103_Boot`, `Ov104_Boot`
- `Entity_LoadAndAttach` (argument 0 as u16): `Ov002_LoadPeerIntoSlot`, `Ov023_CmdLoadActor`
- `Entity_SetVisible` (argument 0 as u16): `Ov023_CmdPlaceActor`, `Ov023_CmdPlaceRelative`, `Ov023_CmdSetupPartyActors`, `Ov023_CmdWarpActor`, `Ov023_Cmd_ParentEntityToEntity`, `Ov023_HideScreenActors`, `Ov023_ReleaseScreenActors`, `Ov023_ReleaseScreenSprites`
- `Entity_SubmitRenderNode` (argument 0 as u16, argument 1 as u16): `Ov023_CmdPlaceActor`, `Ov023_CmdPlaceRelative`, `Ov023_CmdSeatMembers`, `Ov023_Cmd_AttachEntityToEntity`
- `Entity_Tick` (argument 0 as u16): `Ov023_HideScreenActors`, `Ov023_ReleaseScreenSprites`
- `GX_SetGraphicsMode` (argument 0 as u16): `Gfx_RestoreAfterPause`, `Ov024_MobiClip_SetUpMainEngine`
- `GameState_GetField` (argument 0 as u16, argument 1 as u16, argument 1 as u8): `Ov002_BuildPieceNode`, `Ov002_DispatchNodeEvent`, `Ov002_DispatchNodeEventGated`, `Ov002_ElementAttach`, `Ov002_ElementFinishWithMode`, `Ov002_ElementHandOverSlot`, `Ov002_ElementPhase_WatchStateBit`, `Ov002_ElementRebindModel`, `Ov002_ElementRebuildVisuals`, `Ov002_ElementReportHit`, `Ov002_ElementRestartCycle`, `Ov002_ElementRestoreModel`, `Ov002_ElementRestorePose`, `Ov002_ElementRetire`, `Ov002_ElementTickTearDown`, `Ov002_ElementTickTimer`, `Ov002_FireEntryOnce`, `Ov002_GetNodeClassField`, `Ov002_GetNodePayload`, `Ov002_IsMapEntryActionable`, `Ov002_RebindActorModel`, `Ov002_RebindActorModel_2`, `Ov002_RebindAnimatedActorModel`, `Ov002_RefreshEntryIfLive`, `Ov002_TravelElementStep`, `Ov002_TriggerEntryActive`, `Ov002_TriggerEntrySecondary`, `Ov002_TryRegisterElementHit`, `Ov015_PickupCollect`, `Ov015_PickupTakenStep`, `Ov015_PickupUpdate`, `Ov015_SpotDefFindNearestFreeEntry`, `Ov015_SpotUpdate`, `Ov015_TriggerHitTest`, `Ov015_TriggerUpdate`, `Ov016_BreakableHit`, `Ov016_BreakableRelease`, `Ov016_BreakableStart`, `Ov016_FollowerStart`, `Ov016_FollowerStep`, `Ov016_HazardSetState`, `Ov016_HazardStart`, `Ov016_KickableHit`, `Ov016_KickableQueryParamA`, `Ov016_KickableReturnHome`, `Ov016_KickableStart`, `Ov016_KickableTargetPosition`, `Ov016_LiftStep`, `Ov017_DepositStart`, `Ov017_ItemGivenStep`, `Ov017_ItemHit`, `ScriptVm_ResolveOperand`
- `GameState_SetField` (argument 0 as u16, argument 1 as u16, argument 1 as u8, argument 2 as s16, argument 2 as u16): `Game_ActionAssign`, `Game_ApplyModeFlags`, `Ov000_TeardownTitleScene`, `Ov002_ConfirmMission`, `Ov002_DispatchHudCounterCommand`, `Ov002_ElementRetire`, `Ov002_ElementTickTearDown`, `Ov002_ForwardLinkRequest`, `Ov002_PublishStateChange`, `Ov002_ScriptAwardAndShow`, `Ov002_TravelElementStep`, `Ov002_TryRegisterElementHit`, `Ov008_SaveItemCounts`, `Ov009_CommitSaveFields`, `Ov015_PickupCollect`, `Ov015_PickupTakenStep`, `Ov015_PickupUpdate`, `Ov016_BreakableHit`, `Ov016_FollowerStep`, `Ov016_HazardSetState`, `Ov017_DepositStep`, `Ov017_ItemGivenStep`, `Ov025_Config_SaveValues`
- `GetEntryField20ByIndex` (argument 0 as u8): `Ov002_ElementHandOverSlot`
- `GetTrackEntryBase` (argument 0 as u16): `Mover_ResolveFloor`, `Ov002_LoadPeerIntoSlot`, `Ov002_UpdatePeerAnimationsAndExit`, `Ov023_CmdWarpActor`, `Ov023_PlaceActorModel`, `Ov023_RequestGroupResources`
- `Load2DArrayU8` (argument 0 as u8): `Ov022_UpdateCommandInput`
- `LoadArrayInt244` (argument 0 as u16): `Ov023_ActorQueueMotion`, `Ov023_ActorStartEffectTimer`, `Ov023_ActorStepEffects`, `Ov023_ActorUpdateMotions`, `Ov023_Cmd_TestEntityCollisionBit`
- `LoadArrayU8At0cc` (argument 0 as u16): `Ov002_ApplyRosterSlotToNode`, `Ov023_ActorFinish`, `Ov023_CmdPlaceActor`, `Ov023_RebuildVisibleEntries`
- `LoadArrayU8At0ce` (argument 0 as u16): `Ov023_CmdAttachWeapons`, `Ov023_CmdPlaceRelative`, `Ov023_CmdSetAnchor`, `Ov023_Cmd_AttachEntityToEntity`, `Ov023_Cmd_ParentEntityToEntity`, `Ov023_Cmd_SpawnEntityFollower`, `Ov023_ReadTargetPosition`
- `LookupPairKey` (argument 1 as u16, argument 2 as u16): `Cmd_DispatchWithFlag`, `Gfx_DispatchByPairKeyA`, `Gfx_DispatchByPairKeyB`
- `NNS_G3dMdlSetMdlDiffAll` (argument 1 as u16): `Ov255_DrawShakes`
- `NNSi_G3dGeBufferCommand1` (argument 1 as u8): `NNSi_G3dSbcCmdSetPolygonAttr`
- `OS_LockByWord` (argument 0 as u16): `OS_LockCard`
- `OS_UnlockByWord` (argument 0 as u16): `OS_UnlockCard`
- `OSi_SetTimerReserved` (argument 0 as u16): `OS_InitTick`
- `OSi_UnlockVram` (argument 0 as u16): `disableBankForX_`
- `Ov000_ReleaseTwoSlotsEx` (argument 2 as u16): `Ov000_RefreshSelectionGroupDraw`, `Ov000_SetHandleActiveState`
- `Ov002_AdvanceSlotPhase` (argument 0 as u8): `Ov002_SetLapRunning`
- `Ov002_ClearListA` (argument 0 as u16): `Ov022_StartPauseMenu`
- `Ov002_CollectNearbySpots` (argument 2 as u8): `Ov022_ActorUpdate`
- `Ov002_CreateActorClass` (argument 0 as u16): `Ov002_VmCmd7d18c`
- `Ov002_CreateElementClass` (argument 0 as u16): `Ov002_VmCmd7d004`
- `Ov002_CreateHandlerRecord` (argument 0 as u16, argument 1 as u16, argument 2 as s16, argument 3 as u16): `Ov002_QueueTextItem`
- `Ov002_CreateLineClass` (argument 0 as u16): `Ov002_VmCmdList7d610`
- `Ov002_CreatePieceClass` (argument 0 as u16): `Ov002_VmCmd7ceac`
- `Ov002_CreatePieceClass_2` (argument 0 as u16): `Ov002_VmCmd7d334`
- `Ov002_CreatePlacedPiece` (argument 2 as u16): `Ov002_ScriptCmdSpawnElement`
- `Ov002_CreateSpareClass` (argument 0 as u16): `Ov002_CmdCreateModuleSlot`
- `Ov002_CreateTravelClass` (argument 0 as u16): `Ov002_VmCmd7d950`
- `Ov002_Ctx_SetTagTrackerNodeArmed_3` (argument 1 as u8): `Ov002_PrintHelpPage`, `Ov002_ShowPairFocus`
- `Ov002_DrawPageCounter` (argument 0 as u16, argument 1 as u16): `Ov002_PushScrollSpan`
- `Ov002_DrawStamp` (argument 1 as u16, argument 2 as u16, argument 3 as u8): `Ov002_PlotStroke`
- `Ov002_Elem_SetPos` (argument 2 as u16, argument 3 as u16): `Ov002_ReapplyEditsAndCommit`
- `Ov002_FindKeyEntryIndex` (argument 0 as s16): `Ov002_ElementHandOverSlot`, `Ov002_ElementTickTearDown`, `Ov002_OnPartActorRetired`, `Ov017_ItemGivenStep`, `Ov017_ItemHandleMessage`, `Ov017_ItemHit`, `Ov021_PrizeBoxStep`
- `Ov002_ForwardToSubDc` (argument 0 as u16): `Ov002_MapPageCreate`, `Ov002_MapTouchToCell`, `Ov002_PrintHelpPage`, `Ov002_ShowPairFocus`, `Ov002_SwitchSlotCues`, `Ov002_UpdateGaugeRow`
- `Ov002_GetActorSlotByte` (argument 0 as u8): `Ov022_SelectStunState`
- `Ov002_GetPanelWord0220` (argument 0 as u8, argument 1 as u16): `Ov022_ActorSetHp`
- `Ov002_GetPanelWord0220Alt` (argument 0 as u8): `Ov022_ActorUpdate`
- `Ov002_GetRootField8d14` (argument 0 as s16): `Ov002_ElementHandOverSlot`, `Ov002_ElementTickTearDown`, `Ov002_OnPartActorRetired`, `Ov017_ItemGivenStep`, `Ov017_ItemHandleMessage`, `Ov017_ItemHit`
- `Ov002_HandlePanelInput` (argument 0 as u8): `Ov002_RepublishHud`
- `Ov002_HudSetSlotValue` (argument 0 as u8, argument 1 as u8): `Ov022_UpdateCommandInput`
- `Ov002_Hud_RefreshUnless8` (argument 0 as u16): `Ov002_CreateAndRestoreHud`
- `Ov002_IsObjectFree` (argument 1 as u8): `Ov022_TranslateCommandToAction`, `Ov022_UpdateCommandInput`
- `Ov002_LinkPageRefresh` (argument 0 as u16): `Ov002_ClosePageIfAllowed`
- `Ov002_LinkSyncReadPeer` (argument 1 as u8): `Ov002_MapTouchToCell`, `Ov002_PollPageTouches`
- `Ov002_List_GetSlot` (argument 0 as u16): `Ov002_CanAcceptSlotRequest`, `Ov002_SessionTick`, `Ov022_EndAnchoredAction`
- `Ov002_List_GetWord` (argument 0 as u16, argument 0 as u8): `Ov002_FindSiblingOfKindE`, `Ov015_ChestNotifyNeighbours`, `Ov022_FindApproachDir`, `Ov022_ScoreCandidateByFacing`, `Ov022_SweepCapsuleOverGroup`, `Ov022_SweepFanOverGroup`
- `Ov002_List_ScaleEntryTag` (argument 0 as u8, argument 1 as u16): `Ov002_ScriptBuildBindingPayload`, `Ov002_ScriptCmdInvokeObjectCallback48`, `Ov002_ScriptDriveWidget`, `Ov002_ScriptIsEntryFree`, `Ov002_VmSetEntryValue`, `Ov015_ScriptOpCreateSpots`, `Ov016_BreakableStep`, `Ov017_MarshalFxAndDispatch`, `Ov017_RegisterPairAndDispatch`, `Ov017_ScriptOpRetirePiece`
- `Ov002_MakeSecondaryVramKey` (argument 0 as u16): `Ov002_ConfirmSaveSlot`, `Ov002_ShowSlotSummary`
- `Ov002_MarkPeerReady` (argument 0 as u8): `Ov022_ActorSetHp`
- `Ov002_PanelAddSubEntryAndRepaint` (argument 0 as u16, argument 1 as u16): `Ov022_DriveOwnedSound`
- `Ov002_PanelDrawSlotCursor` (argument 0 as u16): `Ov002_AnnounceSelection`
- `Ov002_PanelWriteSlotLabel` (argument 2 as u16, argument 3 as u16): `Ov002_PanelApplyCursorMove`, `Ov002_PanelRepaintCachedEntry`
- `Ov002_Panel_MoveCursor` (argument 0 as u8): `Ov002_RepublishHud`
- `Ov002_Panel_SelectGroup` (argument 0 as u8): `Ov002_RepublishHud`
- `Ov002_PlotCanvasPixel` (argument 1 as u16, argument 2 as u16, argument 3 as u8): `Ov002_BlitMask`
- `Ov002_PositionSubDcHandle` (argument 1 as u16): `Ov002_UpdateGaugeRow`
- `Ov002_PositionSubDcHandle_2` (argument 1 as s16, argument 2 as s16, argument 2 as u16): `Ov002_CreateEntry`, `Ov002_PanelPushSlotState`, `Ov002_PanelRepaintGroup`, `Ov002_PanelRepaintListGroup`, `Ov002_PanelRepaintSubListGroup`, `Ov002_PlayCaptionCues`, `Ov002_UpdateGaugeRow`
- `Ov002_PositionSubDcHandle_3` (argument 1 as s16): `Ov002_DrawNoticeGauge`
- `Ov002_PositionSubDcHandle_4` (argument 1 as s16, argument 2 as s16): `Ov002_MoveWidgetToTarget`, `Ov002_PanelPushSlotState`, `Ov002_PanelShowValueRow`
- `Ov002_PublishStateChange` (argument 2 as s16): `Ov002_PostMessage`
- `Ov002_RemoveEntryAndReopen` (argument 0 as u16, argument 1 as u16): `Ov022_DriveOwnedSound`
- `Ov002_ResetSlotActorPose` (argument 0 as u16): `Ov002_ReleaseAllSlotObjects`
- `Ov002_SetRootField8b41` (argument 0 as u8): `Ov002_ElementChoiceStep`, `Ov002_SpareEntryStep`
- `Ov002_SetScrollPosition` (argument 1 as u16): `Ov022_UpdateSubsystems`
- `Ov002_SetSessionActive` (argument 1 as u8): `Ov002_ElementChoiceStep`
- `Ov002_ShowEntryLabel` (argument 0 as u16, argument 1 as u16, argument 2 as u8): `Ov022_UpdateSelectionController`
- `Ov002_SpawnActorElement` (argument 1 as u16, argument 2 as u16): `Ov002_ScriptCmdSpawnActorElement`
- `Ov002_SpawnAllDrops` (argument 1 as u16): `Ov022_SpawnMemberDrops`, `Ov252_HandleMessage`, `Ov287_Actor_HandleEvent`, `Ov288_Actor_HandleEvent`, `Ov289_Actor_HandleEvent`
- `Ov002_SpawnKindIntoFreeSpot` (argument 1 as u16): `Ov002_SpawnAllDrops`, `Ov002_SpawnTieredDrop`
- `Ov002_SpawnLineElement` (argument 1 as u16, argument 2 as u16): `Ov002_ScriptCmdSpawnLineElement`
- `Ov002_SpawnSpareEntry` (argument 1 as u16, argument 2 as u16): `Ov002_ScriptCmdSpawnSpareEntry`
- `Ov002_SpawnSpot` (argument 2 as u16): `Ov022_Member_ShowDamage`
- `Ov002_StampEntry` (argument 2 as u16, argument 3 as s8): `Ov002_ScriptDriveFourOperands`
- `Ov002_WriteTileQuad9000` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov002_PanelDrawCounter`
- `Ov005_DrawResultTile` (argument 1 as u8, argument 2 as u8): `Ov005_DrawResultGauge`
- `Ov005_FindEntryByTag` (argument 1 as u16): `Ov005_InvokeTagCallback`
- `Ov006_FindEntryByTag` (argument 1 as u16): `Ov006_MissionRetargetCellByTag`
- `Ov006_MissionDrawTextRunFwd` (argument 3 as u8): `Ov006_UpdateMissionMemberMenuScreen`, `Ov006_UpdateMissionMenuConfirmScreen`, `Ov006_UpdateMissionMenuOptionScreen`, `Ov006_UpdateMissionMenuSelectionScreen`
- `Ov006_MissionScene_SetByte95AD` (argument 0 as u8): `Ov006_UpdateMissionMenuOptionScreen`
- `Ov006_SetMissionRowSlotValue` (argument 1 as u16): `Ov006_UpdateMissionMemberMenuScreen`
- `Ov008_ApplyTempFieldsAndRestore` (argument 2 as u16, argument 3 as s16, argument 3 as u16): `Ov008_SetTagValueDup`, `Ov008_UpdateListCursorCells`
- `Ov008_ApplyTempFieldsByTag` (argument 2 as s16, argument 3 as s16): `Ov008_LayoutMissionBadges`
- `Ov008_BuildTagTrackerNode` (argument 3 as u16): `Ov008_BuildLayoutFromTagTable`
- `Ov008_BumpRowCounter` (argument 2 as s8): `Ov008_DropLiftedNode`, `Ov008_PlaceDraggedNode`, `Ov008_RemoveGridNode`
- `Ov008_ClearGridSlot` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov008_ClearNodeCells`, `Ov008_PlaceTrackedNode`
- `Ov008_DispatchFrom2DTable` (argument 1 as s16, argument 2 as s16): `Ov008_RaiseSelectedItemWidget`, `Ov008_ShowItemList`
- `Ov008_DrawMissionRow` (argument 1 as u16): `Ov008_MissionListRevealTick`
- `Ov008_Elem_SetPos` (argument 2 as s16, argument 2 as u16, argument 3 as s16, argument 3 as u16): `Ov008_ReapplyEditsAndCommit`, `Ov008_RetargetCellByTag`, `Ov008_SetTagValueAndNotify`, `Ov008_SetTagValueAndNotify2`
- `Ov008_FindEntryByTag` (argument 1 as u16): `Ov008_ApplyTempFieldsByTag`, `Ov008_HideShopList`, `Ov008_InitializeSavePageLayout`, `Ov008_MissionRetargetCellByTag`, `Ov008_OpenSellDialog`, `Ov008_ReloadTabIcons`, `Ov008_RetargetCellByTag`, `Ov008_SetTagValueDup`, `Ov008_ShopRefreshSelection`, `Ov008_ShowTierPage`, `Ov008_UpdateCounterPanel`, `Ov008_UpdateDecimalDisplay`
- `Ov008_FindFirstThresholdRow` (argument 0 as u16): `Ov008_FireNodeTutorialOnce`
- `Ov008_ForwardSevenArgs` (argument 3 as u8): `Ov008_UpdateMissionMenuConfirmScreen`, `Ov008_UpdateMissionMenuOptionScreen`, `Ov008_UpdateMissionMenuSelectionScreen`
- `Ov008_GetItemTableEntry` (argument 0 as u16): `Ov008_ShowItemList`
- `Ov008_GetPageTableEntry` (argument 0 as u16): `Ov008_RegisterSlotCells`, `Ov008_SetupItemMenu`
- `Ov008_GetTableValue` (argument 0 as u16): `Ov008_FireNodeTutorialOnce`
- `Ov008_MissionListSelectRow` (argument 1 as u16): `Ov008_HandleKind4Message`
- `Ov008_MissionScene_SetByte95AD` (argument 0 as u8): `Ov008_UpdateMissionMenuOptionScreen`
- `Ov008_PixelToTileCell` (argument 2 as u16, argument 3 as u16): `Ov008_TouchPickUpGridNode`, `Ov008_UpdateGridDrag`, `Ov008_UpdateNodeDrag`
- `Ov008_ProcessAndCleanup` (argument 1 as u16): `Ov008_DropLiftedNode`
- `Ov008_ReleaseTwoSlotsEx` (argument 2 as u16): `Ov008_SaveMenu_RefreshRows`
- `Ov008_RetargetCellByTag` (argument 0 as u16): `Ov008_RegisterSlotCells`, `Ov008_SetupItemMenu`
- `Ov008_SetMissionRowSlotValue` (argument 1 as u16): `Ov008_LobbyStep`
- `Ov008_SetTickSlotByte` (argument 0 as u8): `Ov008_UpdateMissionMenuSelectionScreen`
- `Ov008_ShowThreeDigitCells` (argument 1 as u16): `Ov008_UpdateCounterPanel`
- `Ov008_StampTileMode` (argument 2 as u8): `Ov008_ShopRefreshSelection`, `Ov008_ShowTierPage`
- `Ov009_ReleaseTwoSlotsEx_2` (argument 2 as u16): `Ov009_SaveMenu_RefreshRows`
- `Ov014_Instantiate` (argument 0 as u16): `Ov014_VmCmd07c4`
- `Ov015_CreateChestClass` (argument 0 as u16): `Ov015_VmCmd27a0`
- `Ov015_CreatePickupClass` (argument 0 as u16): `Ov015_VmCmd22b0`
- `Ov015_CreateSpotClass` (argument 0 as u16): `Ov015_ScriptOpCreateSpots`
- `Ov015_SpawnChest` (argument 2 as u16): `Ov015_MarshalFxAndDispatch2`
- `Ov016_CreateBreakableClass` (argument 0 as u16): `Ov016_VmCmd1200`
- `Ov016_CreateEntry` (argument 0 as u16): `Ov016_VmCmdCreateEntry`
- `Ov016_CreateEntryClass80` (argument 0 as u16): `Ov016_VmCmdCreateEntryClass80`
- `Ov016_CreateFollowerClass` (argument 0 as u16): `Ov016_InitTripleAndDispatch`
- `Ov016_CreateHazardClass` (argument 0 as u16): `Ov016_VmCmd14b0`
- `Ov017_CreateDepositClass` (argument 0 as u16): `Ov017_MarshalAndDispatch`
- `Ov017_CreateItemClass` (argument 0 as u16): `Ov017_VmCmd0cb4`
- `Ov020_CreateEntity` (argument 1 as u16, argument 2 as u16): `Ov020_MarshalAndDispatch`
- `Ov021_CreateEmblemClass` (argument 0 as u16): `Ov021_VmCmd0e30`
- `Ov021_CreatePrizeBoxClass` (argument 0 as u16): `Ov021_VmCmdEntryList`
- `Ov022_BindAnimationTracks` (argument 1 as s16): `func_ov022_020894a0`
- `Ov022_ClampAngleTowardTarget` (argument 1 as u16): `Ov032_HoverStep`, `Ov052_HoverStep`, `Ov072_HoverStep`
- `Ov022_GetMarkerState` (argument 0 as u8): `Ov022_UpdateSubsystems`
- `Ov022_GetSlotMoveMode` (argument 1 as u16): `Ov030_ApplyModeChange`, `Ov044_ApplyModeChange`, `Ov063_ApplyModeChange`, `Ov082_ApplyModeChange`, `Ov099_ApplyModeChange`
- `Ov022_MarshalNetworkRecord` (argument 4 as u16): `Ov022_ResolveShotHit`, `Ov022_SendInterruptRecord`, `Ov040_HandleMessage`, `Ov059_HandleMessage`, `Ov079_HandleMessage`, `Ov096_HandleMessage`
- `Ov022_Member_ShowDamage` (argument 1 as u16): `func_ov022_02089e20`
- `Ov022_Member_ShowSpotMessage` (argument 0 as u8): `Ov002_ElementHandOverSlot`
- `Ov022_SetRecordValue` (argument 1 as u16): `Ov030_ApplyModeChange`, `Ov044_ApplyModeChange`, `Ov063_ApplyModeChange`, `Ov082_ApplyModeChange`, `Ov099_ApplyModeChange`
- `Ov022_StartSlotEffect` (argument 2 as u16): `Ov022_UpdateSubsystems`
- `Ov022_StepDustEmitter` (argument 5 as u16): `Ov022_UpdateSubsystems`
- `Ov022_StepSpinEffect` (argument 2 as u16): `Ov022_UpdateSubsystems`
- `Ov025_ApplyTempFieldsByTag` (argument 2 as s16, argument 3 as s16): `Ov025_MissionMenu_DrawTags`
- `Ov025_ApplyTempFieldsByTagB` (argument 2 as s16, argument 3 as s16): `Ov025_ScrollList_DrawRow`, `Ov025_ScrollList_PlaceMarkers`
- `Ov025_ClearGridSlot` (argument 1 as u16, argument 2 as u16, argument 3 as u16): `Ov025_ClearNodeCells`, `Ov025_PlaceTrackedNode`
- `Ov025_DispatchFrom2DTable` (argument 2 as s16): `Ov025_ShowItemList`
- `Ov025_DrawMissionRow` (argument 1 as u16): `Ov025_MissionListRevealTick`
- `Ov025_Elem_SetPos` (argument 2 as s16, argument 2 as u16, argument 3 as s16, argument 3 as u16): `Ov025_ReapplyEditsAndCommit`, `Ov025_Reports_PlaceMarkers`, `Ov025_RetargetCellByTag`, `Ov025_Tutorial_PlaceMarkers`
- `Ov025_FindEntryByTag` (argument 1 as u16): `Ov025_InitializeSavePageLayout`, `Ov025_RetargetCellByTag`, `Ov025_SetTagValueDup`, `Ov025_UpdateDecimalDisplay`
- `Ov025_FindFirstThresholdRow` (argument 0 as u16): `Ov025_FireNodeTutorialOnce`
- `Ov025_GetItemTableEntry` (argument 0 as u16): `Ov025_ShowItemList`
- `Ov025_GetPageTableEntry` (argument 0 as u16): `Ov025_RegisterSlotCells`
- `Ov025_GetTableValue` (argument 0 as u16): `Ov025_ScrollList_BuildRows`
- `Ov025_GetTableValueB` (argument 0 as u16): `Ov025_FireNodeTutorialOnce`, `Ov025_ScrollList_BuildRows`
- `Ov025_MissionListSelectRow` (argument 1 as u16): `Ov025_HandleKind4Message`
- `Ov025_MissionList_HasVisibleInDays` (argument 0 as u16, argument 1 as u16): `Ov025_ScrollList_BuildRows`
- `Ov025_PackSlotTag` (argument 0 as u8): `Ov025_Reports_LoadEntries`
- `Ov025_PixelToTileCell` (argument 2 as u16, argument 3 as u16): `Ov025_TouchPickUpGridNode`, `Ov025_UpdateGridDrag`, `Ov025_UpdateNodeDrag`
- `Ov025_ProcessAndCleanup` (argument 1 as u16): `Ov025_DropLiftedNode`
- `Ov025_ReleaseTwoSlotsEx_2` (argument 2 as u16): `Ov025_Hub_InitializeWidgets`, `Ov025_SaveMenu_RefreshRows`
- `Ov025_RetargetCellByTag` (argument 0 as u16): `Ov025_RegisterSlotCells`
- `Ov025_SetCtxFields9638And963a` (argument 0 as s16, argument 1 as s16): `Ov025_ScrollList_Confirm`
- `Ov026_FillTilemapRegionPalette` (argument 2 as u8): `Ov026_ShopRefreshSelection`, `Ov026_ShowTierPage`
- `Ov026_FindEntryByTag` (argument 1 as u16): `Ov026_HideShopList`, `Ov026_OpenSellDialog`, `Ov026_ReloadTabIcons`, `Ov026_ShopRefreshSelection`, `Ov026_ShowTierPage`, `Ov026_UpdateCounterPanel`
- `Ov026_ShowThreeDigitCells` (argument 1 as u16): `Ov026_UpdateCounterPanel`
- `Ov105_SetSlotEventHandler` (argument 0 as u16): `Ov105_SelectChannel`, `Ov105_WH_ChildConnect`, `Ov105_WH_Finalize`, `Ov105_WH_FreeBuffers`, `Ov105_WH_Initialize`, `Ov105_WH_ParentConnect`, `Ov105_WH_PortReceiveCallback`, `Ov105_WH_SetReceiver`, `Ov105_WH_SetSsid`, `Ov105_WH_StartMeasureChannel`, `Ov105_WH_StateInEndChild`, `Ov105_WH_StateInMeasureChannel`, `Ov105_WH_StateInSetMPData`, `Ov105_WH_StateInStartChildMP`, `Ov105_WH_StateOutEnd`, `Ov105_WH_StateOutMeasureChannel`, `Ov105_WH_StateOutStartChild`, `Ov105_WH_StateOutStartChildMP`
- `Ov105_WHi_MeasureChannel` (argument 1 as u16): `Ov105_SelectChannel`, `Ov105_WH_ChildConnect`, `Ov105_WH_Finalize`, `Ov105_WH_FreeBuffers`, `Ov105_WH_Initialize`, `Ov105_WH_ParentConnect`, `Ov105_WH_PortReceiveCallback`, `Ov105_WH_SetReceiver`, `Ov105_WH_SetSsid`, `Ov105_WH_StartMeasureChannel`, `Ov105_WH_StateInEndChild`, `Ov105_WH_StateInMeasureChannel`, `Ov105_WH_StateInSetMPData`, `Ov105_WH_StateInStartChildMP`, `Ov105_WH_StateOutEnd`, `Ov105_WH_StateOutMeasureChannel`, `Ov105_WH_StateOutStartChild`, `Ov105_WH_StateOutStartChildMP`
- `Ov105_WmInit` (argument 1 as u16): `Ov105_WMi_InitializeEx`
- `Ov107_CreateNodeBodyTask` (argument 2 as u8): `Ov214_HandleSpawnCommand`, `Ov215_HandleSpawnCommand`, `Ov216_HandleSpawnMessage`, `Ov217_HandleSpawnMessage`, `Ov219_HandleMessage`, `Ov220_HandleMessage`, `Ov231_OnEffectMessage`, `Ov232_OnEffectMessage`, `Ov258_OnMessage`, `Ov263_OnEffectMessage`, `Ov264_handleType5Command`, `Ov265_OnEffectMessage`, `Ov280_OnEffectMessage`
- `Ov107_CreateNodeXformTask` (argument 2 as u8, argument 3 as u8): `Ov212_OnMessage`, `Ov228_OnEffectMessage`, `Ov229_OnEffectMessage`, `Ov230_OnEffectMessage`, `Ov233_OnEffectMessage`, `Ov240_HandleMessage`, `Ov248_OnEffectMessage`, `Ov249_OnEffectMessage`, `Ov259_OnMessage`, `Ov266_OnMessage`, `Ov267_OnMessage`, `Ov276_HandleMessage`, `Ov297_HandleMessage`, `Ov298_HandleMessage`, `Ov299_HandleMessage`
- `Ov107_CreateNodeXformTaskFx24` (argument 2 as u8): `Ov231_OnEffectMessage`, `Ov232_OnEffectMessage`, `Ov258_OnMessage`, `Ov263_OnEffectMessage`, `Ov265_OnEffectMessage`, `Ov280_OnEffectMessage`
- `Ov107_FindMessageHandler` (argument 0 as u16, argument 0 as u8): `Ov107_Region_OnSyncMessage`, `Ov253_MsgHookSlots`, `Ov287_AreaSweepAttack_Tick`, `Ov288_AreaSweepAttack_Tick`, `Ov289_AreaSweepAttack_Tick`
- `Ov107_InvokeHitCallback` (argument 3 as u8): `Ov191_BoxSweepPush`, `Ov192_BoxSweepPush`, `Ov193_BoxSweepPush`, `Ov200_TickBeam`, `Ov201_TickBeam`, `Ov208_ContactSweep`, `Ov209_ContactSweep`, `Ov212_AreaAttackSweep`, `Ov214_ProcessHitTargets`, `Ov215_ProcessHitTargets`, `Ov216_ProcessHitTargets`, `Ov217_ProcessHitTargets`, `Ov218_ShotBurstTick`, `Ov219_AttackSweep`, `Ov220_AttackSweep`, `Ov221_StrikeSweepEntities`, `Ov222_StrikeSweepEntities`, `Ov223_StrikeSweep`, `Ov223_StrikeSweepEntities`, `Ov224_StrikeSweepEntities`, `Ov225_StrikeSweepEntities`, `Ov226_FlightTick`, `Ov226_StrikeSweepEntities`, `Ov227_AttackSweep`, `Ov228_ContactSweep`, `Ov229_ContactSweep`, `Ov230_ContactCheck`, `Ov230_ContactSweep`, `Ov231_ProbeSpawnPoint`, `Ov232_ProbeSpawnPoint`, `Ov233_ContactSweep`, `Ov237_AttackHitTest`, `Ov237_ShotFlightTick`, `Ov237_SparkUpdate`, `Ov238_AttackHitTest`, `Ov239_ContactSweep`, `Ov240_ContactSweep`, `Ov248_ContactSweep`, `Ov249_ContactSweep`, `Ov249_LeapFlightTick`, `Ov252_ReboundHitTest`, `Ov254_ReboundHitTest`, `Ov256_AttackHitTest`, `Ov258_AttackHitTest`, `Ov258_ShotUpdate`, `Ov260_AttackSweep`, `Ov263_ProbeSpawnPoint`, `Ov264_ProcessHitTargets`, `Ov265_ProbeSpawnPoint`, `Ov266_AreaAttackSweep`, `Ov267_AreaAttackSweep`, `Ov268_ContactSweep`, `Ov271_TickBeam`, `Ov276_ContactSweep`, `Ov280_ProbeSpawnPoint`
- `Ov231_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov231_AiFireVolleyTick`
- `Ov232_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov232_AiFireVolleyTick`
- `Ov256_Claw_New` (argument 1 as u8): `Ov256_EnemyConstruct`
- `Ov256_Shard_New` (argument 1 as u8): `Ov256_EnemyConstruct`
- `Ov258_AimMarker` (argument 1 as u8): `Ov258_GuardTick`
- `Ov258_New` (argument 1 as u8): `Ov258_Construct`
- `Ov258_StepCue` (argument 3 as u16): `Ov258_ComboTick`, `Ov258_GuardTick`, `Ov258_LeapTick`, `Ov258_SlamTick`, `Ov258_StompTick`, `Ov258_TickBarrage`
- `Ov259_PlaySound` (argument 2 as u16): `Ov259_OnHit`, `Ov259_SoundCueTick`
- `Ov263_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov263_AiFireVolleyTick`
- `Ov265_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov265_AiFireVolleyTick`
- `Ov280_Item_RelayoutAndStoreVec` (argument 3 as s8): `Ov280_AiFireVolleyTick`
- `PMi_SendSleepStart` (argument 0 as u16, argument 1 as u16): `PM_ForceToPowerOffAsync`, `PM_GoSleepMode`, `PMi_CommonCallback`
- `PMi_WriteRegisterAsync` (argument 0 as u16, argument 1 as u16): `PMi_WriteRegister`
- `PushCommand_0A` (argument 2 as s16): `NNS_SndPlayerWriteVariable`
- `Rand16NextScaled` (argument 0 as u16): `Ov002_OnPieceDefeated`, `Ov002_PostMessage`, `Ov008_BeginSaveToSlot`, `Ov009_CommitSaveFields`, `Ov025_BeginSaveToSlot`
- `Render_DrawViewLists` (argument 0 as u16): `Ov023_DropPeer`
- `Render_SubmitNode` (argument 1 as u16): `Ov002_BuildModelSlot`, `Ov002_RebindActorAndReattach`, `Ov002_RebindActorModel`, `Ov002_RebindActorModelAndPalette`, `Ov002_RebindActorModelByKind`, `Ov002_RebindActorModelIfAlive`, `Ov002_RebindActorModel_2`, `Ov002_RebindAnimatedActorModel`, `Ov015_ChestUpdate`, `Ov015_PickupShow`, `Ov015_RebindActorModelIfVisible`, `Ov016_RebindActorAndApplyStatePalette`, `Ov016_RebindActorModelAndIdle`, `Ov016_RebindActorModelWithBones`, `Ov021_PrizeBoxRefresh`
- `Req_SetPendingFields` (argument 2 as u16): `Ov022_StartPauseMenu`
- `Res_RequestIdPair` (argument 0 as s16): `Ov022_RequestVoiceIds`, `Ov287_Actor_InitClassAndSpawnParts`, `Ov288_Actor_InitClassAndSpawnParts`, `Ov289_Actor_InitClassAndSpawnParts`
- `SNDi_ProcessEntryAlt` (argument 0 as u16): `Ov023_ActorUpdateMotions`
- `SetIndexedSlot` (argument 1 as u16): `Ov137_StartLeapMotion`, `Ov138_StartLeapMotion`, `Ov158_StartLeapMotion`, `Ov159_StartLeapMotion`, `Ov160_StartLeapMotion`, `Ov246_StartLeapMotion`, `Ov247_StartLeapMotion`
- `Slot_ForwardToEntry` (argument 2 as u16): `Ov008_DrawThreeDigitCounter`, `Ov026_DrawThreeDigitCounter`
- `Slot_Spawn` (argument 3 as u16): `Ov016_BreakableStep`, `Ov016_HazardSetState`, `Ov021_EmblemHandleMessage`, `Ov021_PrizeBoxStep`, `Ov022_SpawnVoiceCue`, `Ov022_StepLocalActorCues`, `Ov213_HandleMessage`, `Ov273_HandleMessage`
- `StoreBytePairKeepMin` (argument 2 as u8): `PartyMember_RebuildDerived`
- `StoreValueInNamedEntry` (argument 0 as u16): `Ov002_LoadPeerIntoSlot`
- `SymbolGroup_FindName` (argument 0 as u16): `Ov002_ConfigureGateFromPeerRow`
- `Table_FindKey` (argument 1 as u16): `SaveSlot_AddItem`
- `TailForwardTrackEntry` (argument 0 as u16): `Ov023_CmdSeatMembers`, `Ov023_CmdSetupPartyActors`
- `TailForwardTrackEntry_2` (argument 0 as u16): `Ov023_CmdActorSpeak`, `Ov023_Cmd_PlayEntityAnimByName`, `Script_Cmd_PlayEntityCutsceneCam`, `Script_Cmd_PlayEntityCutsceneCamWait`
- `Tilemap_FillRect` (argument 6 as u16): `Ov012_StartOpeningMovie`
- `dispatchByObjTypeBits` (argument 1 as u16): `AsyncMessage_Flush`
- `func_01ff9044` (argument 2 as s16, argument 3 as s16): `Ov002_BuildSpreadOffset`
- `func_02010b88` (argument 2 as u16): `NNS_SndHeapCreate`
- `func_02013484` (argument 2 as u16, argument 3 as u16): `Ov000_BlitTileRequest`, `Ov008_BlitQueuedImage`, `Ov026_BlitQueuedImage`
- `func_02016320` (argument 3 as u16): `Ov107_TrackJointMotion`
- `func_0202ba44` (argument 0 as u16): `Ov023_CmdActorSpeak`
- `func_0202c208` (argument 0 as u16): `Ov002_PlaceSlotMarkerOnGround`, `Ov002_UpdateSpawnedSpots`, `Ov022_FindClimbTarget`, `Ov022_SettlePointOnGround`
- `func_0202c248` (argument 0 as u16): `Ov002_UpdateSpawnedSpots`, `Ov022_FindClimbTarget`, `Ov022_SettlePointOnGround`
- `func_02033770` (argument 0 as u8): `Ov002_TickGameplayState`
- `func_ov022_020b1554` (argument 0 as u8): `Ov022_TranslateCommandToAction`
- `func_ov105_020bf900` (argument 1 as u16): `Ov006_SendNetworkPacket`, `Ov008_SendPacket`
<!-- END generated:narrowparams -->

### 1.2 Previously asked for

Nothing left. Everything the previous version asked for is in:
the four hand-checked calls say what the ROM leaves or pass it, the narrow
return types are declared as defined, ov008's mission globals are declared
once, the split headers are joined, the node position and the element counts
are passed and returned, and ov005 and ov007 are named after what they show.

Mission Mode's whole loop (camp menu, mission, Retirarse, results, camp menu)
and Story Mode to the second day (title, opening movie, day 255, the
clock-tower cutscene, the monologue, the next day's card, the field) run
natively on it.

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
