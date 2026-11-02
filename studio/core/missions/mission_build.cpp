#include "core/missions/mission_build.h"

#include "core/files/containers.h"
#include "core/files/paths.h"
#include "core/missions/missions.h"

extern "C" {
#include "missions/registry.h"
#include "missions/worlds.h"
}

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <utility>

namespace studio {

namespace {

namespace fs = std::filesystem;

constexpr int kBaseMission = 1354;    // survive one minute: one area, its ending without a corridor
constexpr int kWaveCheckMission = 4;  // defeat the Mega-Shadow: the "all defeated" event (2.7)
constexpr int kTargetMission = 402;   // defeat seven Fire Plants: the enemy class its gauge counts (2.73)
constexpr int kReachMission = 1;      // reach Axel's position: a player coming within reach of a place (2.8)
constexpr int kEmblemMission = 451;   // find the Organization emblems: their overlay, class and pieces (17.2, 17.3)
// The enemies a spawner holds: Ov107_Spawner_AddSlot ends the game (OS_Terminate) at a ninth, its slots inside the
// spawner's object. A wave's line with more is made into several spawners at its place.
constexpr int kSpawnerEnemies = 8;

// The events the built area files: the clock's end (as 1354's), the ending's (its field raised, as 1354's), the last
// wave defeated, and each wave's start from the second on.
constexpr int kEventClock = 3;
constexpr int kEventEnding = 1;
constexpr int kEventAllDefeated = 5;
constexpr int kEventGoal = 6;
constexpr int kEventFirstWave = 10;

// An eliminate mission's gauge (2.34, Ov002_SetScriptRequest: its kind, total, goal) as 402's: a counter (kind 4)
// that every defeated enemy of the tracked class moves (2.73 names the class: Ov002_OnPartActorRetired adds 1 to
// tally 3); at the goal Ov002_StepProgressBar raises this field (Ov002_TriggerOnceOnFlag), which the area watches.
// A hearts mission's as 0101's: hearts (kind 2), which every defeated enemy moves by its class's hearts
// (Ov107_AiState_OnDefeat posts them to tally 2); no class to name.
constexpr int kGaugeCounter = 4;
constexpr int kGaugeHearts = 2;
constexpr int kFieldGoalReached = 8326;

// An emblems mission's Organization emblems as 0451's: panel 12 brings their commands' overlay (4.25: ov021 as
// script group 17), 2.45 and 2.75 name 392 as 0451's do (what for is not read), 17.2 makes their class in a module
// slot and 17.3 each one, its GameState field set first (the piece is out while it is). Each gives tally 3 ten
// points, eight once it has shrunk (Ov021_EmblemStep), on the counter gauge (kind 4).
constexpr int kEmblemPanel = 12;
constexpr int kEmblemResource = 392;
constexpr int kEmblemSlot = 16;
constexpr int kFirstEmblemField = 8888;
constexpr int kEmblemPoints = 10, kEmblemLatePoints = 8;
constexpr int kFlagBits = 0x4000;  // the game's flag array: its fields' bits

// A reach mission's place as 0001's: 2.8 files a position check (Ov002_CreateTaskNodePath) -- mode 0, the first
// player there; kind 0, within its radius of the point (Ov002_FitsBudget); in the room -- whose event runs c0.
constexpr int kReachCheck = 1;  // 0001's second 2.8, the first with a place

// An enemy class's record in Ms/DP.p2f (one entry per class) and its hearts' byte in it.
constexpr char kEnemyRecords[] = "Ms/DP.p2f";
constexpr std::size_t kRecordHearts = 0x6d;

// The game state fields the built mission reads as 1354 does: c0 raises the ending's, which area 11's event (2.9)
// waits for before c1; the start sets the module's and the others as 1354's start does (what each is for is open).
constexpr int kFieldEndingRaised = 8844;
constexpr int kFieldModule = 8846;
constexpr std::pair<int, int> kStartFields[] = {{8840, 4}, {8833, 4}, {kFieldEndingRaised, 1}, {8837, 1}, {8838, 1},
                                                {8839, 1}, {6318, 1}};

// A clock watch (2.6) of this kind is the time limit's end (1354's).
constexpr int kWatchClockEnd = 4;

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr)
        *why = reason;
    return false;
}

std::uint32_t le32(const std::vector<std::uint8_t>& d, std::size_t at) {
    return at + 4 <= d.size() ? static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 |
                                                           static_cast<std::uint32_t>(d[at + 3]) << 24)
                              : 0xffffffffu;
}

void add16(std::vector<std::uint8_t>& d, std::uint32_t v) {
    d.push_back(static_cast<std::uint8_t>(v & 0xff));
    d.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

void add32(std::vector<std::uint8_t>& d, std::uint32_t v) {
    add16(d, v & 0xffff);
    add16(d, v >> 16);
}

void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xff);
}

// ---- Operand slots (ScriptVm_ResolveOperand: {s16 mode, s16 sel, s32 value}) ---------------------------------

OperandSlot integer(int v) {
    return {1, 0, v};
}

OperandSlot fixed(double v) {  // 20.12
    return {0x10, 0, static_cast<std::int32_t>(std::lround(v * 4096.0))};
}

OperandSlot none(int v) {
    return {0, 0, v};
}

void set_operand(ScriptStep& s, std::size_t slot, const OperandSlot& o) {
    if (2 * slot + 1 >= s.words.size())
        return;
    s.words[2 * slot] =
        static_cast<std::uint16_t>(o.mode) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(o.sel)) << 16;
    s.words[2 * slot + 1] = static_cast<std::uint32_t>(o.value);
}

// ---- A script being written: its steps and its data ------------------------------------------------------------

struct Writer {
    Script script;

    std::uint32_t align4() {
        while (script.tail.size() % 4 != 0)
            script.tail.push_back(0);
        return static_cast<std::uint32_t>(script.tail.size());
    }
    std::uint32_t string(const std::string& s) {
        const auto at = static_cast<std::uint32_t>(script.tail.size());
        script.tail.insert(script.tail.end(), s.begin(), s.end());
        script.tail.push_back(0);
        return at;
    }
    // A text per language (mode 0x40): six words -- none, then en fr de it es -- and the texts after them.
    std::uint32_t texts(const std::array<std::string, 5>& t) {
        const std::uint32_t table = align4();
        script.tail.resize(table + 24, 0);
        put32(script.tail, table, 0xffffffffu);
        for (int l = 0; l < 5; ++l) {
            const std::string& text = t[static_cast<std::size_t>(l)].empty() ? t[0] : t[static_cast<std::size_t>(l)];
            const std::uint32_t at = string(text);
            put32(script.tail, table + 4 + 4 * static_cast<std::size_t>(l), at);
        }
        return table;
    }
    void step(const ScriptStep& s) {
        script.steps.push_back(s);
    }
};

// `step` of `from` as a step of `to`: its texts and names carried into `to`'s data.
ScriptStep carried(const Script& from, ScriptStep step, Writer& to) {
    if (raw_step(step))
        return step;  // (never copied: a branch's words are places in its own script)
    for (std::size_t slot = 0; 2 * slot + 1 < step.words.size(); ++slot) {
        OperandSlot o = operand_at(step, 2 * slot);
        if (o.mode == 2) {
            o.value = static_cast<std::int32_t>(to.string(script_string(from, static_cast<std::uint32_t>(o.value))));
            set_operand(step, slot, o);
        } else if (o.mode == 0x40) {
            std::array<std::string, 5> t;
            for (int l = 0; l < 5; ++l)
                t[static_cast<std::size_t>(l)] = script_text(from, static_cast<std::uint32_t>(o.value), l + 1);
            o.value = static_cast<std::int32_t>(to.texts(t));
            set_operand(step, slot, o);
        }
    }
    return step;
}

// The `nth` step (group, command) of `s` whose first operand is `first` (any when unset), or null.
const ScriptStep* find_step(const Script& s, int group, int command, int nth = 0, const OperandSlot* first = nullptr) {
    for (const ScriptStep& step : s.steps) {
        if (step.group != group || step.command != command)
            continue;
        if (first != nullptr) {
            const OperandSlot o = operand_at(step, 0);
            if (o.mode != first->mode || o.value != first->value)
                continue;
        }
        if (nth-- == 0)
            return &step;
    }
    return nullptr;
}

OperandSlot state_field(int field, int bits) {
    return {4, 0, field | bits << 16};
}

const Script* script_named(const ScriptArchive& a, const std::string& name) {
    for (const Script& s : a.scripts)
        if (s.name == name)
            return &s;
    return nullptr;
}

// A script archive (CAKP) of `scripts`: section 0 their names, section 1 the scripts.
std::vector<std::uint8_t> write_archive(const std::vector<Script>& scripts) {
    std::vector<std::uint8_t> names;
    add16(names, static_cast<std::uint32_t>(scripts.size()));
    std::size_t at = 2 + 2 * scripts.size();
    for (const Script& s : scripts) {
        add16(names, static_cast<std::uint32_t>(at));
        at += s.name.size() + 1;
    }
    for (const Script& s : scripts) {
        names.insert(names.end(), s.name.begin(), s.name.end());
        names.push_back(0);
    }
    KaphFile k;
    k.head = {'C', 'A', 'K', 'P', 0, 0, 0, 0};
    const std::uint32_t table0 = 0x28, table1 = table0 + 12;
    add32(k.head, table0);
    add32(k.head, table1);
    for (int s = 2; s < 8; ++s)
        add32(k.head, 0xffffffffu);
    add32(k.head, 1);  // section 0: the names
    add32(k.head, 0);
    add32(k.head, 0);
    add32(k.head, static_cast<std::uint32_t>(scripts.size()));  // section 1: the scripts
    for (std::size_t i = 0; i < 2 * scripts.size(); ++i)
        add32(k.head, 0);
    k.blocks.push_back({0, 0, 0, names});
    for (std::size_t i = 0; i < scripts.size(); ++i)
        k.blocks.push_back({1, i, 0, write_script(scripts[i])});
    return write_kaph(k);
}

// An enemy table (Ov002_LoadObjectRecordsAndDrops): the rows, then a group for each spawner.
std::vector<std::uint8_t> write_enemy_table(const std::vector<std::array<std::uint8_t, 16>>& rows,
                                            const std::vector<std::vector<std::pair<int, int>>>& groups) {
    std::vector<std::uint8_t> d;
    d.push_back(static_cast<std::uint8_t>(rows.size()));
    d.push_back(static_cast<std::uint8_t>(groups.size()));
    add16(d, 0);
    const std::size_t rows_at = 8 + 4 * groups.size();
    add32(d, static_cast<std::uint32_t>(rows_at));
    for (std::size_t g = 0; g < groups.size(); ++g)
        add32(d, 0);
    for (const auto& r : rows)
        d.insert(d.end(), r.begin(), r.end());
    for (std::size_t g = 0; g < groups.size(); ++g) {
        put32(d, 8 + 4 * g, static_cast<std::uint32_t>(d.size()));
        unsigned total = 0;
        for (const auto& [row, count] : groups[g])
            total += static_cast<unsigned>(count);
        d.push_back(static_cast<std::uint8_t>(groups[g].size()));
        d.push_back(0);
        add16(d, total);
        for (const auto& [row, count] : groups[g]) {
            d.push_back(static_cast<std::uint8_t>(row));
            d.push_back(static_cast<std::uint8_t>(count));  // its limit: as many as it has
            d.push_back(static_cast<std::uint8_t>(count));
            d.push_back(0);
        }
    }
    return d;
}

bool read_mission(const GameFiles& game, int number, MissionFile* out, std::string* why) {
    const std::vector<std::uint8_t> bytes = game.read(mission_path(number));
    if (bytes.empty())
        return fail_with(why, "cannot read " + mission_path(number));
    return parse_mission(bytes, number, out, why);
}

// The keys of room `room`'s exits in world `world` (its link rows: mi/wd/wd_<code> entry 0, the room table as
// core/rooms/worlds.cpp parse_world reads it: a room's +1 its row count, +0x1c its rows' offset, 8 bytes a row, the
// key first). A mission closes them at its start (2.39, Ov002_StampEntry finds a row by its key in the room loaded).
bool room_exits(const GameFiles& game, int world, int room, std::vector<int>* keys, std::string* why) {
    const char* code = khdays_world_code(world);
    if (code == nullptr)
        return fail_with(why, "no world " + std::to_string(world));
    const std::string path = std::string("mi/wd/wd_") + code;
    P2File archive;
    std::string problem;
    if (!parse_p2(game.read(path), &archive, &problem) || archive.entries.empty())
        return fail_with(why, path + ": " + (problem.empty() ? "no room table" : problem));
    const auto& t = archive.entries[0].data;
    if (t.empty() || room < 0 || static_cast<std::size_t>(room) >= t[0])
        return fail_with(why, path + " has no room " + std::to_string(room));
    const std::size_t off = le32(t, 4 + 4 * static_cast<std::size_t>(room));
    if (off + 0x20 > t.size())
        return fail_with(why, path + ": room " + std::to_string(room) + " outside its table");
    const std::size_t count = t[off + 1], rows = off + le32(t, off + 0x1c);
    if (rows + 8 * count > t.size())
        return fail_with(why, path + ": room " + std::to_string(room) + "'s exits outside its table");
    keys->clear();
    for (std::size_t k = 0; k < count; ++k)
        keys->push_back(t[rows + 8 * k]);
    return true;
}

// Every enemy class a game mission's table has a row of, with the first such row.
std::map<int, std::array<std::uint8_t, 16>> class_rows(const GameFiles& game) {
    std::map<int, std::array<std::uint8_t, 16>> out;
    for (const int number : game.missions) {
        MissionFile m;
        std::string why;
        if (!read_mission(game, number, &m, &why))
            continue;
        const int e = m.entry("em0");
        EnemyTable t;
        if (e < 0 || m.entry_names[static_cast<std::size_t>(e)] != "em0" ||
            !parse_enemy_table(m.p2.entries[static_cast<std::size_t>(e)].data, &t, &why))
            continue;
        for (const EnemyKind& k : t.kinds)
            out.emplace(k.raw[0], k.raw);
    }
    return out;
}

}  // namespace

bool plan_of(const KhdaysMission& m, MissionPlan* out, std::string* why) {
    if (!khdays_mission_built(&m))
        return fail_with(why, "no wave");
    if (!m.has_start)
        return fail_with(why, "a mission with waves needs where the players start (start = x y z facing)");
    MissionPlan plan;
    plan.kind = m.kind;
    plan.target = m.has_target ? m.target : -1;
    plan.goal = m.goal;
    for (int i = 0; i < m.collect_count; ++i)
        plan.collects.push_back({m.collects[i].at[0], m.collects[i].at[1], m.collects[i].at[2]});
    plan.has_reach = m.has_reach != 0;
    plan.reach = {m.reach[0], m.reach[1], m.reach[2]};
    plan.reach_radius = m.reach_radius;
    plan.world = khdays_world_index(m.world);
    plan.room = m.room;
    plan.time_ms = m.time_ms;
    for (int l = 0; l < KHDAYS_MISSION_LANGUAGES; ++l) {
        plan.notice[static_cast<std::size_t>(l)] = m.name[l] != nullptr ? m.name[l] : "";
        plan.objective[static_cast<std::size_t>(l)] = m.objective[l] != nullptr ? m.objective[l] : "";
    }
    plan.start = {m.start[0], m.start[1], m.start[2]};
    plan.facing = m.facing;
    std::map<int, std::vector<WaveSpawner>> waves;  // by their numbers
    for (int i = 0; i < m.spawner_count; ++i) {
        const KhdaysMissionSpawner& s = m.spawners[i];
        WaveSpawner w;
        w.at = {s.at[0], s.at[1], s.at[2]};
        int held = 0;
        for (int e = 0; e < s.enemy_count; ++e) {
            for (int left = s.enemies[e].count; left > 0;) {
                const int take = std::min(left, kSpawnerEnemies - held);
                w.enemies.push_back({s.enemies[e].enemy_class, take});
                held += take;
                left -= take;
                if (held == kSpawnerEnemies) {  // full: the rest in another at the same place
                    waves[s.wave].push_back(w);
                    w.enemies.clear();
                    held = 0;
                }
            }
        }
        if (!w.enemies.empty())
            waves[s.wave].push_back(w);
    }
    for (auto& [n, spawners] : waves)
        plan.waves.push_back(spawners);
    *out = std::move(plan);
    return true;
}

GameFiles extracted_game_files(const fs::path& nitrofs) {
    GameFiles g;
    auto read_file = [](const fs::path& file) {
        std::ifstream in(file, std::ios::binary);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    g.read = [nitrofs, read_file](const std::string& path) { return read_file(nitrofs / path_u8(path)); };
    g.missions = mission_numbers(nitrofs);
    return g;
}

std::vector<int> buildable_enemy_classes(const GameFiles& game) {
    std::vector<int> out;
    for (const auto& [c, row] : class_rows(game))
        out.push_back(c);
    return out;
}

std::vector<int> enemy_hearts(const GameFiles& game) {
    P2File records;
    std::string why;
    std::vector<int> out;
    if (!parse_p2(game.read(kEnemyRecords), &records, &why))
        return out;
    for (const P2Entry& e : records.entries)
        out.push_back(e.data.size() > kRecordHearts ? e.data[kRecordHearts] : 0);
    return out;
}

int waves_hearts(const std::vector<std::vector<WaveSpawner>>& waves, const std::vector<int>& hearts) {
    int total = 0;
    for (const auto& wave : waves)
        for (const WaveSpawner& sp : wave)
            for (const auto& e : sp.enemies)
                if (e.enemy_class >= 0 && static_cast<std::size_t>(e.enemy_class) < hearts.size())
                    total += hearts[static_cast<std::size_t>(e.enemy_class)] * e.count;
    return total;
}

bool build_mission(const GameFiles& game, const MissionPlan& plan, std::vector<std::uint8_t>* out, std::string* why) {
    const bool eliminate = plan.kind == KHDAYS_MISSION_ELIMINATE;
    const bool hearts = plan.kind == KHDAYS_MISSION_HEARTS;
    const bool emblems = plan.kind == KHDAYS_MISSION_EMBLEMS;
    const bool reach = plan.kind == KHDAYS_MISSION_REACH;
    const bool to_goal = eliminate || hearts || emblems;  // cleared at its gauge's goal
    const bool by_waves = !to_goal && !reach;             // cleared by its last wave
    // (an emblems or reach mission may have no enemies)
    if ((plan.waves.empty() && by_waves) || (!plan.waves.empty() && plan.waves[0].empty()))
        return fail_with(why, "no wave: a mission made here has its enemies in waves");
    if (emblems && plan.collects.empty())
        return fail_with(why, "no Organization emblem to collect");
    if (reach && !plan.has_reach)
        return fail_with(why, "no place to reach");
    MissionFile base, check, target, reach_base, emblem_base;
    if (!read_mission(game, kBaseMission, &base, why) || !read_mission(game, kWaveCheckMission, &check, why) ||
        (eliminate && !read_mission(game, kTargetMission, &target, why)) ||
        (reach && !read_mission(game, kReachMission, &reach_base, why)) ||
        (emblems && !read_mission(game, kEmblemMission, &emblem_base, why)))
        return false;
    const int area_e = base.entry("11"), start_e = base.entry("_s"), table_e = base.entry("em0"),
              table_m_e = base.entry("em0m"), check_e = check.entry("11");
    if (area_e < 0 || start_e < 0 || table_e < 0 || table_m_e < 0 || check_e < 0)
        return fail_with(why, "the base missions are not as this studio knows them");
    const ScriptArchive& area = base.entries[static_cast<std::size_t>(area_e)].archive;
    const ScriptArchive& start = base.entries[static_cast<std::size_t>(start_e)].archive;
    const Script* area_i = script_named(area, "_i");
    const Script* start_i = script_named(start, "_i");
    const Script* check_i = script_named(check.entries[static_cast<std::size_t>(check_e)].archive, "_i");
    const Script* ending0 = script_named(area, "c0");
    const Script* ending1 = script_named(area, "c1");
    if (area_i == nullptr || start_i == nullptr || check_i == nullptr || ending0 == nullptr || ending1 == nullptr)
        return fail_with(why, "the base missions' scripts are not as this studio knows them");
    std::vector<int> exits;
    if (!room_exits(game, plan.world, plan.room, &exits, why))
        return false;

    // the enemy table: a row for each class, a group for each spawner (in wave order)
    const auto catalog = class_rows(game);
    std::vector<int> classes;
    std::vector<std::array<std::uint8_t, 16>> rows;
    std::vector<std::vector<std::pair<int, int>>> groups;
    // row -> its actors (2.49): one for every enemy of every wave (whether a defeated one's actor serves a later
    // wave is not known)
    std::map<int, int> pool;
    for (const auto& wave : plan.waves) {
        for (const WaveSpawner& sp : wave) {
            if (sp.enemies.empty())
                return fail_with(why, "a spawner without enemies");
            std::vector<std::pair<int, int>> parts;
            for (const auto& e : sp.enemies) {
                const auto found = catalog.find(e.enemy_class);
                if (found == catalog.end())
                    return fail_with(why, "enemy class " + std::to_string(e.enemy_class) + ": no game mission has it");
                if (e.count < 1 || e.count > 255)
                    return fail_with(why, "a count is 1-255");
                int row = -1;
                for (std::size_t r = 0; r < classes.size(); ++r)
                    if (classes[r] == e.enemy_class)
                        row = static_cast<int>(r);
                if (row < 0) {
                    row = static_cast<int>(classes.size());
                    classes.push_back(e.enemy_class);
                    auto raw = found->second;
                    raw[4] = 0;  // its level: the mission's (mdb.z), not the row's own adjustment
                    rows.push_back(raw);
                }
                parts.emplace_back(row, e.count);
                pool[row] += e.count;
            }
            groups.push_back(parts);
        }
    }
    // (the table's counts are bytes: the game reads them so)
    if (groups.size() > 255 || rows.size() > 255)
        return fail_with(why, "more spawners than an enemy table holds (255)");

    // ---- area 11: its world and room, the enemies' pool, the first wave, the start, HUD, clock, notice, events
    Writer w;
    w.script.name = "_i";
    auto copy = [&](Writer& to, int group, int command, int nth = 0, const OperandSlot* first = nullptr,
                    const Script* from = nullptr) -> ScriptStep* {
        const Script& source = from != nullptr ? *from : *area_i;
        const ScriptStep* s = find_step(source, group, command, nth, first);
        if (s == nullptr)
            return nullptr;
        to.step(carried(source, *s, to));
        return &to.script.steps.back();
    };
    bool missing = false;
    auto need = [&](ScriptStep* s) -> ScriptStep& {
        static ScriptStep dummy;
        if (s == nullptr) {
            missing = true;
            return dummy;
        }
        return *s;
    };
    // a script queued on an event (2.5, as 1354's: the area's own list), run while the chosen room is loaded (its
    // third operand, Ov002_RegisterEventSlot's index in the loaded rooms' table; 1354's names its arena, and a
    // room not loaded leaves the script never run)
    auto queue = [&](Writer& to, int event, const std::string& script) {
        ScriptStep& q = need(copy(to, 2, 5));
        set_operand(q, 1, integer(event));
        set_operand(q, 2, integer(plan.room));
        set_operand(q, 3, none(plan.room));
        const std::uint32_t name = to.string(script);
        set_operand(q, 4, {2, 0, static_cast<std::int32_t>(name)});
        set_operand(q, 5, none(static_cast<std::int32_t>(name)));
    };
    // when every enemy out is defeated (2.7 check 1, every spawner's group: 0004's step, its owner slot none)
    auto when_all_defeated = [&](Writer& to, int event, const std::string& script) {
        ScriptStep& c = need(copy(to, 2, 7, 0, nullptr, check_i));
        set_operand(c, 1, integer(event));
        set_operand(c, 4, integer(-1));
        set_operand(c, 5, integer(0));
        queue(to, event, script);
    };
    std::size_t next_group = 0;
    auto spawners = [&](Writer& to, const std::vector<WaveSpawner>& wave) {
        for (const WaveSpawner& sp : wave) {
            ScriptStep& s = need(copy(to, 2, 32));
            set_operand(s, 0, integer(static_cast<int>(next_group++)));
            for (int k = 0; k < 3; ++k) {
                set_operand(s, 5 + static_cast<std::size_t>(k), fixed(sp.at[static_cast<std::size_t>(k)]));
                set_operand(s, 12 + static_cast<std::size_t>(k), fixed(sp.at[static_cast<std::size_t>(k)]));
            }
        }
    };
    auto next_of = [&](std::size_t wave) {
        return wave + 1 < plan.waves.size() ? "w" + std::to_string(wave + 2) : std::string("c0");
    };
    auto event_of = [&](std::size_t wave) {
        return wave + 1 < plan.waves.size() ? kEventFirstWave + static_cast<int>(wave) : kEventAllDefeated;
    };

    need(copy(w, 2, 0));
    set_operand(need(copy(w, 2, 25)), 0, integer(plan.world));
    set_operand(need(copy(w, 2, 26)), 1, integer(plan.room));
    need(copy(w, 2, 13));
    ScriptStep& tables = need(copy(w, 2, 36));
    set_operand(tables, 0, integer(static_cast<int>(groups.size())));
    set_operand(tables, 1, integer(static_cast<int>(rows.size())));
    for (std::size_t r = 0; r <= rows.size(); ++r)
        set_operand(need(copy(w, 2, 48)), 0, integer(r < rows.size() ? static_cast<int>(r) : -1));
    need(copy(w, 2, 14));
    for (const auto& [row, n] : pool)
        for (int k = 0; k < n; ++k)
            set_operand(need(copy(w, 2, 49)), 1, integer(row));
    if (!plan.waves.empty())
        spawners(w, plan.waves[0]);
    need(copy(w, 2, 50));
    need(copy(w, 2, 10));
    for (int p = 0; p < 4; ++p) {
        // each player in the chosen room (its second operand: the room, whose loaded slot Ov002_GetCtxTableByte
        // finds; 1354's says its arena), at the start
        ScriptStep& s = need(copy(w, 2, 12, p));
        set_operand(s, 1, integer(plan.room));
        for (int k = 0; k < 3; ++k)
            set_operand(s, 3 + static_cast<std::size_t>(k), fixed(plan.start[static_cast<std::size_t>(k)]));
        set_operand(s, 6, integer(plan.facing));
    }
    // the room's own exits closed (1354's steps name its arena's keys: another room has others, and a key the
    // loaded room lacks leaves Ov002_StampEntry without a row to write)
    for (const int key : exits)
        set_operand(need(copy(w, 2, 39)), 1, integer(key));
    need(copy(w, 2, 16, 0));
    if (plan.time_ms > 0)
        set_operand(need(copy(w, 2, 16, 1)), 2, integer(plan.time_ms));
    set_operand(need(copy(w, 2, 20)), 0, {0x40, 0, static_cast<std::int32_t>(w.texts(plan.notice))});
    need(copy(w, 2, 53));
    if (plan.time_ms > 0) {
        // the clock's end: a counter watch of kind 4 (1354's), then its ending
        const OperandSlot zero = integer(0);
        const ScriptStep* clock = nullptr;
        for (int k = 0; (clock = find_step(*area_i, 2, 6, k, &zero)) != nullptr; ++k)
            if (operand_at(*clock, 4).value == kWatchClockEnd)
                break;
        if (clock == nullptr)
            missing = true;
        else
            w.step(carried(*area_i, *clock, w));
        queue(w, kEventClock, "c0");
    }
    // the last wave defeated is no end of its own where a gauge's goal or a place is (the target class's last enemy,
    // or the last hearts, are among them, and c0 run twice is not known to be harmless)
    if (by_waves || plan.waves.size() > 1)
        when_all_defeated(w, event_of(0), next_of(0));
    {
        // the ending: once c0 raises its field, 4000 ms later c1 (to area 99, the results)
        const ScriptStep* ending = nullptr;
        for (int k = 0; (ending = find_step(*area_i, 2, 9, k)) != nullptr; ++k)
            if (operand_at(*ending, 10).value == state_field(kFieldEndingRaised, 1).value)
                break;
        if (ending == nullptr)
            missing = true;
        else
            w.step(carried(*area_i, *ending, w));
        queue(w, kEventEnding, "c1");
        if (to_goal && ending != nullptr) {
            // the goal reached: the same watch on the gauge's field, c0 at once
            ScriptStep goal = carried(*area_i, *ending, w);
            set_operand(goal, 1, integer(kEventGoal));
            set_operand(goal, 5, state_field(kFieldGoalReached, 1));
            set_operand(goal, 8, integer(0));
            w.step(goal);
            queue(w, kEventGoal, "c0");
        }
    }
    if (reach) {
        // a player within the radius of the place: c0 (0001's position check, its operands set)
        const int reach_e = reach_base.entry("11");
        const Script* reach_i =
            reach_e < 0 ? nullptr : script_named(reach_base.entries[static_cast<std::size_t>(reach_e)].archive, "_i");
        ScriptStep& at = need(reach_i == nullptr ? nullptr : copy(w, 2, 8, kReachCheck, nullptr, reach_i));
        set_operand(at, 1, integer(kEventGoal));
        set_operand(at, 2, integer(0));  // mode 0: the first player there
        set_operand(at, 3, integer(plan.room));
        set_operand(at, 4, none(plan.room));
        for (int k = 0; k < 3; ++k)
            set_operand(at, 5 + static_cast<std::size_t>(k), fixed(plan.reach[static_cast<std::size_t>(k)]));
        set_operand(at, 8, integer(0));  // kind 0: a sphere
        set_operand(at, 9, fixed(plan.reach_radius));
        set_operand(at, 12, integer(0));
        queue(w, kEventGoal, "c0");
    }
    need(copy(w, 0, 16));
    need(copy(w, 2, 1));
    need(copy(w, 0, 3));

    std::vector<Script> area_scripts = {*ending0, *ending1};
    for (std::size_t k = 1; k < plan.waves.size(); ++k) {
        Writer wave;
        wave.script.name = "w" + std::to_string(k + 1);
        spawners(wave, plan.waves[k]);
        if (by_waves || k + 1 < plan.waves.size())
            when_all_defeated(wave, event_of(k), next_of(k));
        need(copy(wave, 0, 3));
        area_scripts.push_back(wave.script);
    }
    area_scripts.push_back(w.script);

    // ---- the start: chests, emblems, the enemy table, the objective, the state the ending reads; then area 11
    Writer s;
    s.script.name = "_i";
    auto copy_start = [&](int group, int command, int nth = 0, const OperandSlot* first = nullptr) {
        return copy(s, group, command, nth, first, start_i);
    };
    const int emblem_e = emblems ? emblem_base.entry("_s") : -1;
    const Script* emblem_i =
        emblem_e < 0 ? nullptr : script_named(emblem_base.entries[static_cast<std::size_t>(emblem_e)].archive, "_i");
    auto copy_emblem = [&](int group, int command, const OperandSlot* first = nullptr) {
        return emblem_i == nullptr ? nullptr : copy(s, group, command, 0, first, emblem_i);
    };
    const OperandSlot emblem_panel = integer(kEmblemPanel), emblem_resource = integer(kEmblemResource);
    need(copy_start(2, 0));
    need(copy_start(2, 78));
    need(copy_start(2, 76));  // the chests (trbox, by the file's key)
    need(copy_start(2, 83));  // the emblems (evi)
    if (emblems)
        need(copy_emblem(4, 25, &emblem_panel));
    need(copy_start(2, 31));  // the enemy table
    if (emblems)
        need(copy_emblem(2, 45, &emblem_resource));
    need(copy_start(2, 46));
    const OperandSlot module_flag = state_field(kFieldModule, 1);
    need(copy_start(0, 0, 0, &module_flag));
    need(copy_start(4, 16));
    ScriptStep& gauge = need(copy_start(2, 34));
    if (eliminate) {
        int counted = 0;
        for (const auto& wave : plan.waves)
            for (const WaveSpawner& sp : wave)
                for (const auto& e : sp.enemies)
                    counted += e.enemy_class == plan.target ? e.count : 0;
        if (counted == 0)
            return fail_with(why, "no wave has an enemy of the target class " + std::to_string(plan.target));
        set_operand(gauge, 0, integer(kGaugeCounter));
        set_operand(gauge, 1, integer(counted));
        set_operand(gauge, 2, integer(plan.goal > 0 && plan.goal < counted ? plan.goal : counted));
        const int target_e = target.entry("_s");
        const Script* target_i =
            target_e < 0 ? nullptr : script_named(target.entries[static_cast<std::size_t>(target_e)].archive, "_i");
        ScriptStep& tracked = need(target_i == nullptr ? nullptr : copy(s, 2, 73, 0, nullptr, target_i));
        set_operand(tracked, 0, integer(plan.target));
    } else if (hearts) {
        const int total = waves_hearts(plan.waves, enemy_hearts(game));
        if (total == 0)
            return fail_with(why, "no heart to collect: none of the waves' enemies gives any (" +
                                      std::string(kEnemyRecords) + ")");
        if (plan.goal > total)
            return fail_with(why, "goal " + std::to_string(plan.goal) + ": the waves' enemies give " +
                                      std::to_string(total) + " hearts");
        set_operand(gauge, 0, integer(kGaugeHearts));
        set_operand(gauge, 1, integer(total));
        set_operand(gauge, 2, integer(plan.goal > 0 ? plan.goal : total));
    } else if (emblems) {
        const int count = static_cast<int>(plan.collects.size());
        if (kFirstEmblemField + count > kFlagBits)
            return fail_with(why, std::to_string(count) +
                                      " Organization emblems: the game's flag array has fields for " +
                                      std::to_string(kFlagBits - kFirstEmblemField));
        if (plan.goal > kEmblemPoints * count)
            return fail_with(why, "goal " + std::to_string(plan.goal) + ": the emblems give " +
                                      std::to_string(kEmblemPoints * count) + " points at most");
        set_operand(gauge, 0, integer(kGaugeCounter));
        set_operand(gauge, 1, integer(kEmblemPoints * count));
        set_operand(gauge, 2, integer(plan.goal > 0 ? plan.goal : kEmblemLatePoints * count));
        need(copy_emblem(2, 75, &emblem_resource));
    }
    set_operand(need(copy_start(2, 52)), 1, {0x40, 0, static_cast<std::int32_t>(s.texts(plan.objective))});
    if (emblems) {
        // each emblem's field set, their class, then each one in the room
        const OperandSlot first_field = state_field(kFirstEmblemField, 1);
        for (std::size_t k = 0; k < plan.collects.size(); ++k)
            set_operand(need(copy_emblem(0, 0, &first_field)), 0,
                        state_field(kFirstEmblemField + static_cast<int>(k), 1));
        ScriptStep& made = need(copy_emblem(17, 2));
        set_operand(made, 0, integer(kEmblemSlot));
        set_operand(made, 1, integer(static_cast<int>(plan.collects.size())));
        for (std::size_t k = 0; k < plan.collects.size(); ++k) {
            ScriptStep& e = need(copy_emblem(17, 3));
            set_operand(e, 0, integer(kEmblemSlot));
            set_operand(e, 1, integer(static_cast<int>(k)));
            set_operand(e, 2, integer(plan.room));
            set_operand(e, 3, state_field(kFirstEmblemField + static_cast<int>(k), 1));
            for (int c = 0; c < 3; ++c)
                set_operand(e, 4 + static_cast<std::size_t>(c), fixed(plan.collects[k][static_cast<std::size_t>(c)]));
            set_operand(e, 7, integer(0));
        }
    }
    for (const auto& [field, bits] : kStartFields) {
        const OperandSlot f = state_field(field, bits);
        need(copy_start(0, 0, 0, &f));
    }
    const OperandSlot to_area = none(-1);
    need(copy_start(2, 3, 0, &to_area));
    need(copy_start(0, 3));
    std::vector<Script> start_scripts;
    for (const Script& sc : start.scripts)
        if (sc.name != "_i")
            start_scripts.push_back(sc);  // its `dg`, as it is
    start_scripts.push_back(s.script);

    if (missing)
        return fail_with(why, "a step of the base missions this studio copies is not there");

    MissionFile made = base;
    made.p2.entries[static_cast<std::size_t>(area_e)].data = write_archive(area_scripts);
    made.p2.entries[static_cast<std::size_t>(start_e)].data = write_archive(start_scripts);
    const auto table = write_enemy_table(rows, groups);
    made.p2.entries[static_cast<std::size_t>(table_e)].data = table;
    made.p2.entries[static_cast<std::size_t>(table_m_e)].data = table;
    for (auto& e : made.entries)
        e.kind = MissionEntry::empty;  // (written from p2 as set above)
    std::vector<std::uint8_t> bytes = write_p2(made.p2);

    // read back as the game's are read: every script, every queued script there, the table
    MissionFile back;
    std::string back_why;
    if (!parse_mission(bytes, 0, &back, &back_why))
        return fail_with(why, "the built mission does not read back: " + back_why);
    for (const MissionEntry& e : back.entries) {
        if (e.kind != MissionEntry::script_archive)
            continue;
        std::set<std::string> names(e.archive.names.begin(), e.archive.names.end());
        for (const Script& sc : e.archive.scripts) {
            for (const ScriptStep& st : sc.steps) {
                if (st.group == 2 && st.command == 5) {
                    const OperandSlot o = operand_at(st, 8);
                    const std::string name = script_string(sc, static_cast<std::uint32_t>(o.value));
                    if (o.mode == 2 && names.count(name) == 0)
                        return fail_with(why, "the built mission queues a script it has not: " + name);
                }
            }
        }
    }
    EnemyTable t;
    if (!parse_enemy_table(table, &t, &back_why) || t.groups.size() != groups.size())
        return fail_with(why, "the built enemy table does not read back: " + back_why);
    *out = std::move(bytes);
    return true;
}

}  // namespace studio
