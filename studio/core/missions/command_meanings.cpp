#include "core/missions/command_meanings.h"

#include "core/missions/command_names.h"
#include "core/missions/enemy_names.h"

#include <cstdio>
#include <cstring>

namespace studio {

namespace {

// A slot as a value: its integer, number, or what it refers to.
std::string value_text(const OperandSlot& o) {
    char buf[48];
    if (o.mode == 1)
        std::snprintf(buf, sizeof(buf), "%d", o.value);
    else if (o.mode == 0x10)
        std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(o.value) / 4096.0);
    else if (o.mode == 4)
        std::snprintf(buf, sizeof(buf), "state[%u:%u]", static_cast<unsigned>(o.value) & 0xffff,
                      static_cast<unsigned>(o.value) >> 16);
    else if (o.mode == 8)
        std::snprintf(buf, sizeof(buf), "var[%d]", o.value);
    else
        std::snprintf(buf, sizeof(buf), "(mode %04x, %d, %d)", static_cast<unsigned>(o.mode) & 0xffff, o.sel, o.value);
    return buf;
}

// An integer slot (ScriptVm_ReadOperandInt: 0 when not an integer), shown as read when not one.
std::string int_text(const OperandSlot& o, const char* (*name)(int) = nullptr) {
    if (o.mode != 1)
        return value_text(o);
    if (name != nullptr && name(o.value) != nullptr)
        return name(o.value);
    return std::to_string(o.value);
}

// Milliseconds as seconds: 180000 -> "180", 1500 -> "1.5".
std::string seconds_text(std::int32_t ms) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(ms) / 1000.0);
    std::string s = buf;
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

// A step's offset in its script, as the views number steps (block + 4 + word).
std::string target_text(std::uint32_t word) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "@%05x", word + 4);
    return buf;
}

// What an operand ByteCode_ResolveOperand reads points to: a text of the
// script's data (mode 2 at data + value, mode 0x40 through its table by
// language), or the place when it holds none.
std::string data_text(const OperandSlot& o, const Script* script, int language) {
    char buf[32];
    if (o.mode == 2) {
        const std::string t =
            script != nullptr ? script_string(*script, static_cast<std::uint32_t>(o.value)) : std::string();
        if (!t.empty())
            return t;
        std::snprintf(buf, sizeof(buf), "data+0x%x", static_cast<unsigned>(o.value));
        return buf;
    }
    if (o.mode == 0x40) {
        const std::string t =
            script != nullptr ? script_text(*script, static_cast<std::uint32_t>(o.value), language) : std::string();
        if (!t.empty())
            return t;
        std::snprintf(buf, sizeof(buf), "text(data+0x%x)", static_cast<unsigned>(o.value));
        return buf;
    }
    return value_text(o);
}

}  // namespace

const char* world_code(int index) {
    static const char* const codes[] = {"tt", "aw", "he", "al", "eh", "nm", "hb", "zz", "pp", "bb", "tw", "pi"};
    return index >= 0 && index < 12 ? codes[index] : nullptr;
}

StepMeaning step_meaning(const ScriptStep& s, const Script* script, int language) {
    const auto op = [&](std::size_t k) { return operand_at(s, 2 * k); };
    const std::size_t slots = s.words.size() / 2;
    const CommandName* n = command_name(s.group, s.command);
    const auto is = [&](const char* handler) { return n != nullptr && std::strcmp(n->handler, handler) == 0; };
    // Game_ActionAssign (src/engine): destination (a variable, mode 8, or a state field, mode 4) = source
    // the three commands of group 0 that read words (missions.h raw_step): the script's own jumps
    if (raw_step(s)) {
        if (is("ScriptCmd_SetElemOffset") && !s.words.empty())
            return {StepMeaning::go_to, {target_text(s.words[0])}};
        if (is("CommitRecordCursor") && s.words.size() >= 2) {
            // the expression when the script is given and it reads, else its place
            const std::string e = script != nullptr ? expression_text(*script, s.words[0], language) : std::string();
            char buf[24];
            std::snprintf(buf, sizeof(buf), "data+0x%x", s.words[0]);
            return {StepMeaning::branch_unless, {e.empty() ? std::string(buf) : e, target_text(s.words[1])}};
        }
        if (is("Game_ActionSetBranchTarget") && !s.words.empty()) {
            if (s.words[0] == 0xffffffffu)
                return {StepMeaning::no_pending_jump, {}};
            return {StepMeaning::pending_jump, {target_text(s.words[0])}};
        }
        return {};
    }
    // Ov002_ScriptCmd_SnapshotPausedObject: Ov002_SnapshotPausedObject(ByteCode_ResolveOperand(op 0)):
    // the text, UTF-8 to UCS-2, to Ov002_ShowNoticeText (when the field has its notice object)
    if (is("Ov002_ScriptCmd_SnapshotPausedObject") && slots >= 1 && (op(0).mode == 2 || op(0).mode == 0x40)) {
        return {StepMeaning::notice, {data_text(op(0), script, language)}};
    }
    // Ov002_ScriptQueueTextItem (screen, event, match, tag, what, ..., style, y): with tag 0, a slot that
    // runs the script named by `what` (Ov002_NewTextItem copies the name) is put on event `event`'s list
    // (Ov002_RegisterEventSlot); screen 0: the first list and this area's script (RunTextItemNoTag:
    // the entry named by the area's number), else the second list (event | 0x1000) and the start's
    // (RunTextItemTagged: "_s")
    if (is("Ov002_ScriptQueueTextItem") && slots >= 5 && op(3).mode == 0 && (op(4).mode == 2 || op(4).mode == 0x40)) {
        const bool start = !(op(0).mode == 1 && op(0).value == 0);
        return {start ? StepMeaning::on_event_start : StepMeaning::on_event,
                {int_text(op(1)), data_text(op(4), script, language)}};
    }
    // Ov002_ScriptSubmitTaskNode: a node comparing each field of the game's state with `value`
    // (op 7 ==, 8 >, 9 <, 10 >=, 11 <=, 12 !=; mode 0 any field, else all), filed under the event's
    // number in the bank its event list shares (flag 0 the first, else event | 0x1000). A positive
    // threshold starts a lap before the event counts as met.
    if (is("Ov002_ScriptSubmitTaskNode") && slots >= 5 && op(3).mode == 1 && op(3).value >= 0 &&
        slots >= 8 + static_cast<std::size_t>(op(3).value)) {
        static const char* const ops[] = {"==", ">", "<", ">=", "<=", "!="};
        const std::size_t count = static_cast<std::size_t>(op(3).value);
        const OperandSlot cmp = op(5 + count), value = op(6 + count);
        const std::string sign =
            cmp.mode == 1 && cmp.value >= 7 && cmp.value <= 12 ? ops[cmp.value - 7] : "op " + value_text(cmp);
        std::string when;
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t w = static_cast<std::uint32_t>(op(5 + i).value);
            char field[40];
            std::snprintf(field, sizeof(field), "state[%u:%u]", w & 0xffff, w >> 16);
            when += (i == 0                                ? ""
                     : op(4).mode == 1 && op(4).value == 0 ? " || "
                                                           : " && ") +
                    std::string(field) + " " + sign + " " + int_text(value);
        }
        std::string event = int_text(op(1));
        if (!(op(0).mode == 1 && op(0).value == 0))
            event += "+0x1000";
        return {StepMeaning::event_when, {event, when, int_text(op(7 + count))}};
    }
    // Ov002_StartFieldEffect (command, value, limit): Ov002_DispatchHudCounterCommand. Command 0 value 0
    // makes the HUD (Ov002_CreateAndRestoreHud); command 7 starts the mission clock now when value is 0
    // (root +0x8d00) and, when limit >= 0, sets the time limit in milliseconds (root +0x8d04), the panel
    // then counting down Ov002_GetRemainingTicks: limit - (now - start)
    if (is("Ov002_StartFieldEffect") && slots >= 3 && op(0).mode == 1 && op(1).mode == 1 && op(2).mode == 1) {
        if (op(0).value == 0 && op(1).value == 0)
            return {StepMeaning::hud_on, {}};
        if (op(0).value == 7 && op(1).value == 0 && op(2).value >= 0)
            return {StepMeaning::time_limit, {seconds_text(op(2).value)}};
        if (op(0).value == 7 && op(1).value != 0 && op(2).value < 0)
            return {StepMeaning::time_limit_shown, {}};
        return {};
    }
    // Ov002_ScriptCmd_EditSessionClock (field, op, ms): Ov002_EditSessionClock on the clock's start (0) or
    // its limit (1): op 0 stamps now, 1 sets, 2 adds, 3 takes away, never below 0
    if (is("Ov002_ScriptCmd_EditSessionClock") && slots >= 3 && op(0).mode == 1 && op(1).mode == 1 && op(2).mode == 1) {
        if (op(0).value == 0 && op(1).value == 0)
            return {StepMeaning::clock_restart, {}};
        if (op(0).value == 1 && op(1).value == 1)
            return {StepMeaning::time_limit_set, {seconds_text(op(2).value)}};
        if (op(0).value == 1 && op(1).value == 2)
            return {StepMeaning::time_limit_add, {seconds_text(op(2).value)}};
        if (op(0).value == 1 && op(1).value == 3)
            return {StepMeaning::time_limit_sub, {seconds_text(op(2).value)}};
        return {};
    }
    // Ov002_ScriptMoveSlot (player, kind, tag, x, y, z, heading): tag 0, the point and the heading in
    // degrees (turned to a rotation over 0x168); else the operand names a marker of the room
    // (ByteCode_ResolveOperand -> Ov002_ResolveNamedPlacement). Ov002_WriteSessionMarker files it for
    // the player; a point is spread by player (Ov002_ScatterPlaceByIndex) unless state flag 0x20e7.
    if (is("Ov002_ScriptMoveSlot") && slots >= 3) {
        if (op(2).mode == 0 && slots >= 7) {
            return {StepMeaning::player_at,
                    {int_text(op(0)), int_text(op(1)), value_text(op(3)), value_text(op(4)), value_text(op(5)),
                     int_text(op(6))}};
        }
        if (op(2).mode == 2 || op(2).mode == 0x40) {
            return {StepMeaning::player_at_marker,
                    {int_text(op(0)), int_text(op(1)), data_text(op(2), script, language)}};
        }
        return {};
    }
    // Ov002_ScriptCmd_ThreeMixedOperands (module slot, model, a 20.12): Ov002_ScriptSpawnTimedSet makes a
    // class of chests in that module slot and a chest for each row of the mission's set in trbox
    if (is("Ov002_ScriptCmd_ThreeMixedOperands") && slots >= 2) {
        return {StepMeaning::chests, {int_text(op(0)), data_text(op(1), script, language)}};
    }
    // Ov002_ScriptCmd_SpawnStateSpot: Ov002_SpawnStateSpots spawns each emblem of the mission's set in evi
    // whose bit the save does not hold (its four operands are read, not used there)
    if (is("Ov002_ScriptCmd_SpawnStateSpot")) {
        return {StepMeaning::emblems, {}};
    }
    if (is("Game_ActionAssign") && slots >= 2) {
        return {StepMeaning::assign, {value_text(op(0)), value_text(op(1))}};
    }
    // Ov002_ScriptCmd_LoadWorldLinkResources: Ov002_LoadWorldLinkResources(world, context) opens
    // mi/wd/wd_<data_ov002_0207f0a4[world]>
    if (is("Ov002_ScriptCmd_LoadWorldLinkResources") && slots >= 2) {
        return {StepMeaning::load_world, {int_text(op(0), world_code), int_text(op(1))}};
    }
    // Ov002_ScriptCmd_LoadPeerIntoSlot: Ov002_LoadPeerIntoSlot(slot, peer): the room of that place in
    // the world's table into one of the four slots
    if (is("Ov002_ScriptCmd_LoadPeerIntoSlot") && slots >= 2) {
        return {StepMeaning::load_room, {int_text(op(0)), int_text(op(1))}};
    }
    // Ov002_ScriptCmd_LoadObjectRecords: Ov002_LoadObjectRecordsAndDrops reads em0 (em0m in multiplayer)
    if (is("Ov002_ScriptCmd_LoadObjectRecords")) {
        return {StepMeaning::load_enemies, {}};
    }
    // Ov002_ScriptCmd_CreateActorFromMarker: Ov002_CreateActorFromMarker(source, row): an actor of the
    // class the enemy table's row names (Ov107_SpawnEntityClass(row's id)) at that source
    if (is("Ov002_ScriptCmd_CreateActorFromMarker") && slots >= 2) {
        return {StepMeaning::enemy_at, {int_text(op(0)), int_text(op(1))}};
    }
    // Ov002_ResolveFourDescriptors (kind, total, goal, flags): Ov002_SetScriptRequest sets the mission gauge (root
    // +0x8c94). Ov002_ApplyTally moves it: kind 2 by tally 2 (hearts), kinds 0, 1 and 4 by tally 3 (defeats and
    // counted things); at the goal Ov002_StepProgressBar raises state 8326 (Ov002_TriggerOnceOnFlag)
    if (is("Ov002_ResolveFourDescriptors") && slots >= 3 && op(0).mode == 1) {
        const StepMeaning::Kind kinds[] = {StepMeaning::gauge_other, StepMeaning::gauge_defeat,
                                           StepMeaning::gauge_hearts, StepMeaning::gauge_other,
                                           StepMeaning::gauge_counter};
        const int k = op(0).value;
        return {k >= 0 && k <= 4 ? kinds[k] : StepMeaning::gauge_other,
                {int_text(op(1)), int_text(op(2)), int_text(op(0))}};
    }
    // Ov002_ScriptCmd_SetEventParams (class, n): Ov002_Event_SetParams tracks an enemy class (object context +0xa,
    // its count +0xc cleared, n at +0xe): Ov002_OnPartActorRetired adds 1 to tally 3 for each defeated enemy whose
    // row is of that class (402: class 20, "Defeat seven Fire Plants!")
    if (is("Ov002_ScriptCmd_SetEventParams") && slots >= 1 && op(0).mode == 1) {
        const char* name = enemy_name(op(0).value);
        return {StepMeaning::gauge_class, {int_text(op(0)), name != nullptr ? name : "?"}};
    }
    // Ov002_ScriptOpInvokeSlot0 (-, kind, amount): Ov002_AddMissionTally(0, kind, amount) (0102's e1: the Guardian
    // defeated, tally 3 + 1)
    if (is("Ov002_ScriptOpInvokeSlot0") && slots >= 3) {
        return {StepMeaning::tally_add, {int_text(op(1)), int_text(op(2))}};
    }
    // Ov002_SubmitRequestBlockKind1 (gate, event, check, group, owner, threshold): a node of
    // Ov002_AllocScriptCallback whose check Ov002_NodeRunCheckPhase runs -- 0 asks whether the group is defeated
    // (Ov002_QueryEntryValue(group)), 1 whether every one is (the group packed as single | multiplayer << 16)
    if (is("Ov002_SubmitRequestBlockKind1") && slots >= 4 && op(2).mode == 1 && op(3).mode == 1) {
        if (op(2).value == 1)
            return {StepMeaning::all_defeated, {int_text(op(1))}};
        if (op(2).value == 0) {
            const int single = op(3).value & 0xffff, multi = (op(3).value >> 16) & 0xffff;
            std::string group = std::to_string(single);
            if (multi != 0 && multi != single)
                group += " (" + std::to_string(multi) + " in multiplayer)";
            return {StepMeaning::group_defeated, {int_text(op(1)), group}};
        }
        return {};
    }
    // Ov002_ScriptCreateSlotObject: (group, source, mask, mode, gate, x, y, z, a, tag, b, c, x, y, z, mode)
    // -> Ov002_CreateSlotObjectAndStart: a spawner object at (x, y, z) adding the group's enemies
    if (is("Ov002_ScriptCreateSlotObject") && slots >= 8) {
        return {StepMeaning::spawner,
                {int_text(op(0)), int_text(op(1)), value_text(op(5)), value_text(op(6)), value_text(op(7))}};
    }
    return {};
}

ScriptPlaces script_places(const Script& s) {
    ScriptPlaces p;
    for (std::size_t k = 0; k < s.steps.size(); ++k) {
        const ScriptStep& st = s.steps[k];
        const CommandName* n = command_name(st.group, st.command);
        if (n == nullptr)
            continue;
        const auto op = [&](std::size_t i) { return operand_at(st, 2 * i); };
        const auto fx = [&](std::size_t i) {
            const OperandSlot o = op(i);  // ScriptVm_ReadOperandFx32: an integer as a whole number
            return o.mode == 1 ? o.value * 4096 : o.mode == 0x10 ? o.value : 0;
        };
        const std::size_t slots = st.words.size() / 2;
        if (std::strcmp(n->handler, "Ov002_ScriptCmd_LoadWorldLinkResources") == 0 && slots >= 1 && op(0).mode == 1) {
            p.world = op(0).value;
        } else if (std::strcmp(n->handler, "Ov002_ScriptCmd_LoadPeerIntoSlot") == 0 && slots >= 2 && op(0).mode == 1 &&
                   op(1).mode == 1) {
            p.rooms.push_back({op(0).value, op(1).value});
        } else if (std::strcmp(n->handler, "Ov002_ScriptMoveSlot") == 0 && slots >= 3 && op(0).mode == 1) {
            ScriptPlaces::Start st0;
            st0.step = k;
            st0.player = op(0).value;
            st0.kind = op(1).mode == 1 ? op(1).value : -1;
            if (op(2).mode == 0 && slots >= 7) {
                st0.at = {fx(3), fx(4), fx(5)};
                st0.heading = op(6).mode == 1 ? op(6).value : 0;
            } else if (op(2).mode == 2) {
                st0.marker = script_string(s, static_cast<std::uint32_t>(op(2).value));
                if (st0.marker.empty())
                    st0.marker = "?";
            } else {
                continue;
            }
            p.starts.push_back(st0);
        } else if (std::strcmp(n->handler, "Ov002_ScriptCreateSlotObject") == 0 && slots >= 8 && op(0).mode == 1) {
            ScriptPlaces::Spawner sp;
            sp.step = k;
            sp.group = op(0).value;
            sp.source = op(1).mode == 1 ? op(1).value : -1;
            sp.at = {fx(5), fx(6), fx(7)};
            p.spawners.push_back(sp);
        }
    }
    return p;
}

std::string meaning_text(const StepMeaning& m) {
    auto arg = [&](std::size_t k) { return k < m.args.size() ? m.args[k] : std::string(); };
    switch (m.kind) {
    case StepMeaning::none: return {};
    case StepMeaning::assign: return arg(0) + " = " + arg(1);
    case StepMeaning::load_world: return "world " + arg(0) + ", context " + arg(1);
    case StepMeaning::load_room: return "slot " + arg(0) + " <- room " + arg(1);
    case StepMeaning::load_enemies: return "the enemy table (em0, em0m in multiplayer) and its drops (eid.z)";
    case StepMeaning::enemy_at: return "the enemy of row " + arg(1) + " at source " + arg(0);
    case StepMeaning::spawner:
        return "a spawner of group " + arg(0) + " at (" + arg(2) + ", " + arg(3) + ", " + arg(4) + "), source " +
               arg(1);
    case StepMeaning::go_to: return "go to " + arg(0);
    case StepMeaning::branch_unless: return "unless " + arg(0) + ", go to " + arg(1);
    case StepMeaning::pending_jump: return "a jump kept for later, to " + arg(0);
    case StepMeaning::no_pending_jump: return "no jump kept for later";
    case StepMeaning::notice: return "a notice: \"" + arg(0) + "\"";
    case StepMeaning::on_event: return "on event " + arg(0) + ", run this area's script \"" + arg(1) + "\"";
    case StepMeaning::on_event_start:
        return "on event " + arg(0) + " of the second list, run the start's script \"" + arg(1) + "\"";
    case StepMeaning::event_when: return "event " + arg(0) + " when " + arg(1) + ", threshold " + arg(2);
    case StepMeaning::hud_on: return "the HUD made";
    case StepMeaning::time_limit: return "a time limit of " + arg(0) + " s on the HUD, counted from now";
    case StepMeaning::time_limit_shown: return "the time limit shown as it runs";
    case StepMeaning::time_limit_add: return "the time limit + " + arg(0) + " s";
    case StepMeaning::time_limit_set: return "the time limit set to " + arg(0) + " s";
    case StepMeaning::time_limit_sub: return "the time limit - " + arg(0) + " s";
    case StepMeaning::clock_restart: return "the mission clock started now";
    case StepMeaning::player_at:
        return "player slot " + arg(0) + " near (" + arg(2) + ", " + arg(3) + ", " + arg(4) + ") facing " + arg(5) +
               " degrees, kind " + arg(1);
    case StepMeaning::chests:
        return "the mission's chests from trbox, their class in module slot " + arg(0) + ", model \"" + arg(1) + "\"";
    case StepMeaning::emblems: return "the mission's emblems from evi, those not yet found";
    case StepMeaning::player_at_marker:
        return "player slot " + arg(0) + " at the room's marker \"" + arg(2) + "\", kind " + arg(1);
    case StepMeaning::gauge_defeat: return "the mission gauge: targets defeated, " + arg(1) + " of " + arg(0);
    case StepMeaning::gauge_hearts: return "the mission gauge: hearts, " + arg(1) + " of " + arg(0);
    case StepMeaning::gauge_counter: return "the mission gauge: a counter, " + arg(1) + " of " + arg(0);
    case StepMeaning::gauge_other: return "the mission gauge, kind " + arg(2) + ": " + arg(1) + " of " + arg(0);
    case StepMeaning::gauge_class:
        return "each defeated enemy of class " + arg(0) + " (" + arg(1) + ") counts on the mission gauge";
    case StepMeaning::tally_add: return "the mission tally of kind " + arg(0) + " + " + arg(1);
    case StepMeaning::group_defeated: return "event " + arg(0) + " when group " + arg(1) + " is defeated";
    case StepMeaning::all_defeated: return "event " + arg(0) + " when every enemy out is defeated";
    }
    return {};
}

std::string operands_meaning(const ScriptStep& s, const Script* script) {
    return meaning_text(step_meaning(s, script));
}

}  // namespace studio
