#include "core/missions/mission_build.h"

#include "core/files/containers.h"
#include "core/files/paths.h"
#include "core/missions/missions.h"

extern "C" {
#include "missions/registry.h"
#include "missions/worlds.h"
}

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
constexpr int kMostClasses = 4;       // an area's enemy classes (Ov002_CreateActorFromMarker's four places)

// The events the built area files: the clock's end (as 1354's), the ending's (its field raised, as 1354's), the last
// wave defeated, and each wave's start from the second on.
constexpr int kEventClock = 3;
constexpr int kEventEnding = 1;
constexpr int kEventAllDefeated = 5;
constexpr int kEventFirstWave = 10;

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
    if (m.spawner_count == 0)
        return fail_with(why, "no wave");
    if (!m.has_start)
        return fail_with(why, "a mission with waves needs where the players start (start = x y z facing)");
    MissionPlan plan;
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
        for (int e = 0; e < s.enemy_count; ++e)
            w.enemies.push_back({s.enemies[e].enemy_class, s.enemies[e].count});
        waves[s.wave].push_back(w);
    }
    for (auto& [n, spawners] : waves)
        plan.waves.push_back(spawners);
    *out = std::move(plan);
    return true;
}

GameFiles extracted_game_files(const fs::path& nitrofs) {
    GameFiles g;
    g.read = [nitrofs](const std::string& path) {
        std::ifstream in(nitrofs / path_u8(path), std::ios::binary);
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    g.missions = mission_numbers(nitrofs);
    return g;
}

std::vector<int> buildable_enemy_classes(const GameFiles& game) {
    std::vector<int> out;
    for (const auto& [c, row] : class_rows(game))
        out.push_back(c);
    return out;
}

bool build_mission(const GameFiles& game, const MissionPlan& plan, std::vector<std::uint8_t>* out, std::string* why) {
    if (plan.waves.empty() || plan.waves[0].empty())
        return fail_with(why, "no wave: a mission made here has its enemies in waves");
    MissionFile base, check;
    if (!read_mission(game, kBaseMission, &base, why) || !read_mission(game, kWaveCheckMission, &check, why))
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
    if (static_cast<int>(classes.size()) > kMostClasses)
        return fail_with(why, "more than four enemy classes: the game makes an area's enemies of four at most");
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
    // a script queued on an event (2.5, as 1354's: the area's own list)
    auto queue = [&](Writer& to, int event, const std::string& script) {
        ScriptStep& q = need(copy(to, 2, 5));
        set_operand(q, 1, integer(event));
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
    spawners(w, plan.waves[0]);
    need(copy(w, 2, 50));
    need(copy(w, 2, 10));
    for (int p = 0; p < 4; ++p) {
        ScriptStep& s = need(copy(w, 2, 12, p));
        for (int k = 0; k < 3; ++k)
            set_operand(s, 3 + static_cast<std::size_t>(k), fixed(plan.start[static_cast<std::size_t>(k)]));
        set_operand(s, 6, integer(plan.facing));
    }
    for (int k = 0; k < 5; ++k)
        need(copy(w, 2, 39, k));
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
    }
    need(copy(w, 0, 16));
    need(copy(w, 2, 1));
    need(copy(w, 0, 3));

    std::vector<Script> area_scripts = {*ending0, *ending1};
    for (std::size_t k = 1; k < plan.waves.size(); ++k) {
        Writer wave;
        wave.script.name = "w" + std::to_string(k + 1);
        spawners(wave, plan.waves[k]);
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
    need(copy_start(2, 0));
    need(copy_start(2, 78));
    need(copy_start(2, 76));  // the chests (trbox, by the file's key)
    need(copy_start(2, 83));  // the emblems (evi)
    need(copy_start(2, 31));  // the enemy table
    need(copy_start(2, 46));
    const OperandSlot module_flag = state_field(kFieldModule, 1);
    need(copy_start(0, 0, 0, &module_flag));
    need(copy_start(4, 16));
    need(copy_start(2, 34));
    set_operand(need(copy_start(2, 52)), 1, {0x40, 0, static_cast<std::int32_t>(s.texts(plan.objective))});
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
