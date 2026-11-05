// What a script command does with its operands, for the commands whose
// handler has been read: each operand's name and how
// to show its value. A command not listed here is shown as read
// (missions.h operands_text), never given a guessed meaning.
#pragma once

#include "core/missions/missions.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace studio {

// What a step does, for the commands whose handler has been read: a kind
// and its values as text (the window words it in its language, the command
// line in English: meaning_text).
struct StepMeaning {
    enum Kind {
        none,             // the handler not read yet
        assign,           // {0} = {1} (Game_ActionAssign)
        load_world,       // world {0}, context {1} (Ov002_LoadWorldLinkResources)
        load_room,        // slot {0} <- room {1} (Ov002_LoadPeerIntoSlot)
        load_enemies,     // the enemy table em0 / em0m and eid.z (Ov002_LoadObjectRecordsAndDrops)
        enemy_at,         // the enemy of row {1} at source {0} (Ov002_CreateActorFromMarker)
        spawner,          // a spawner of group {0} at ({2}, {3}, {4}), source {1} (Ov002_ScriptCreateSlotObject)
        go_to,            // go to {0} (ScriptCmd_SetElemOffset)
        branch_unless,    // unless {0} (the expression, or its place), go to {1} (CommitRecordCursor)
        pending_jump,     // a jump kept for later, to {0} (Game_ActionSetBranchTarget)
        no_pending_jump,  // none kept (Game_ActionSetBranchTarget(-1))
        notice,           // a notice showing {0} (Ov002_ScriptCmd_SnapshotPausedObject -> Ov002_ShowNoticeText)
        on_event,         // on event {0}, this area's script {1} (Ov002_ScriptQueueTextItem -> RunTextItemNoTag)
        on_event_start,   // on event {0} of the second list, the start's script {1} (-> RunTextItemTagged: "_s")
        event_when,  // event {0} when {1}, threshold {2} (Ov002_ScriptSubmitTaskNode -> Ov002_CreatePairCompareNode)
        hud_on,  // the HUD made (Ov002_StartFieldEffect -> Ov002_DispatchHudCounterCommand(0, 0): Ov002_CreateAndRestoreHud)
        time_limit,        // a time limit of {0} s on the HUD, counted from now (command 7, value 0)
        time_limit_shown,  // the time limit shown as it runs (command 7, -1, -1)
        time_limit_add,    // the time limit + {0} s (Ov002_EditSessionClock(1, 2, ms))
        time_limit_set,    // the time limit set to {0} s (Ov002_EditSessionClock(1, 1, ms))
        time_limit_sub,    // the time limit - {0} s, not below 0 (Ov002_EditSessionClock(1, 3, ms))
        clock_restart,     // the mission clock started now (Ov002_EditSessionClock(0, 0, -))
        player_at,         // player slot {0} near ({2}, {3}, {4}) facing {5} degrees, kind {1} (Ov002_ScriptMoveSlot)
        player_at_marker,  // player slot {0} at the room's marker {2}, kind {1} (Ov002_ScriptMoveSlot by name)
        chests,  // the mission's chests from trbox, their class in module slot {0}, model {1} (Ov002_ScriptSpawnTimedSet)
        emblems,  // the mission's emblems from evi, those not yet found (Ov002_SpawnStateSpots)
        // the mission gauge (Ov002_ResolveFourDescriptors -> Ov002_SetScriptRequest(kind, total, goal, flags)):
        gauge_defeat,    // kind 1, targets defeated: goal {1} of {0}
        gauge_hearts,    // kind 2, hearts collected: goal {1} of {0}
        gauge_counter,   // kind 4, a counter: goal {1} of {0}
        gauge_other,     // another kind {2}: goal {1} of {0}
        gauge_class,     // each defeated enemy of class {0} ({1}) counts on it (Ov002_Event_SetParams: tally 3)
        tally_add,       // the mission tally of kind {0} + {1} (Ov002_ScriptOpInvokeSlot0 -> Ov002_AddMissionTally)
        group_defeated,  // event {0} when group {1} is defeated (Ov002_SubmitRequestBlockKind1, check 0)
        all_defeated,    // event {0} when every enemy out is defeated (Ov002_SubmitRequestBlockKind1, check 1)
        // a position check (Ov002_ScriptSubmitPathTask -> Ov002_CreateTaskNodePath, kind 0: Ov002_FitsBudget):
        place_first,  // event {0} when the first player comes within {1} of ({2}, {3}, {4}), room {5} (mode 0)
        place_all,    // event {0} when every player is within {1} of ({2}, {3}, {4}), room {5} (mode 1)
        panel,        // panel {0}'s overlay loaded, its commands script group {1} (Ov002_SwitchPanelOverlay)
        org_emblems,  // {1} Organization emblems, their class in module slot {0} (Ov021_CreateEmblemClass)
        org_emblem,   // Organization emblem {1} of slot {0} at ({3}, {4}, {5}), room {2}, out while {6} is set
    } kind = none;
    std::vector<std::string> args;
};
// With the script, the texts its data holds where the step points (in
// `language`: GetLanguage's 1-5, en fr de it es).
StepMeaning step_meaning(const ScriptStep& s, const Script* script = nullptr, int language = 1);

// The places a script sets, as its steps read them: the world it loads, the
// rooms it puts in slots, its spawners (Ov002_ScriptCreateSlotObject: an enemy
// group of the mission's table at a point, 20.12).
struct ScriptPlaces {
    int world = -1;                          // data_ov002_0207f0a4's index
    std::vector<std::pair<int, int>> rooms;  // (slot, room index)
    struct Spawner {
        std::size_t step = 0;
        int group = -1, source = -1;
        std::array<std::int32_t, 3> at{};
    };
    std::vector<Spawner> spawners;
    // where the players start (Ov002_ScriptMoveSlot -> Ov002_WriteSessionMarker): a player slot at a
    // point (spread by Ov002_ScatterPlaceByIndex) with a heading, or at a marker of the room by name
    struct Start {
        std::size_t step = 0;
        int player = -1, kind = -1, heading = 0;
        std::array<std::int32_t, 3> at{};
        std::string marker;  // empty: at `at`
    };
    std::vector<Start> starts;
};
ScriptPlaces script_places(const Script& s);
std::string meaning_text(const StepMeaning& m);
// meaning_text(step_meaning(s, script)), empty when not read
std::string operands_meaning(const ScriptStep& s, const Script* script = nullptr);

// The world codes in the order of `data_ov002_0207f0a4` (the index
// Ov002_LoadWorldLinkResources takes): tt aw he al eh nm hb zz pp bb tw pi.
const char* world_code(int index);

}  // namespace studio
