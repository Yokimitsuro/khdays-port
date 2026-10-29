// The game's missions as its script machine reads them: synthetic script archives and mission files read and written back
// byte for byte, steps walked, operands read by their modes. Every file made here.
#include "test.h"

#include "core/missions/command_meanings.h"
#include "core/missions/command_names.h"
#include "core/files/containers.h"
#include "core/missions/mission_db.h"
#include "core/missions/mission_build.h"
#include "core/missions/mission_edit.h"
#include "core/missions/missions.h"
#include "core/missions/mission_names.h"
#include "core/missions/mod_missions.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace studio;

namespace {

using Bytes = std::vector<std::uint8_t>;

void add16(Bytes& d, std::uint32_t v) {
    d.push_back(static_cast<std::uint8_t>(v & 0xff));
    d.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}
void add32(Bytes& d, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}
void put32(Bytes& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xff);
}
void align(Bytes& d, std::size_t a) {
    while (d.size() % a)
        d.push_back(0);
}

// A step: group, command, its words.
void step(Bytes& d, std::uint8_t g, std::uint8_t c, const std::vector<std::uint32_t>& words) {
    d.push_back(g);
    d.push_back(c);
    add16(d, static_cast<std::uint32_t>(1 + words.size()));
    for (const auto w : words)
        add32(d, w);
}

// A script: its size word, then its steps.
Bytes script(const std::vector<Bytes>& steps) {
    Bytes d;
    add32(d, 0);
    for (const auto& s : steps)
        d.insert(d.end(), s.begin(), s.end());
    put32(d, 0, static_cast<std::uint32_t>(d.size()));
    return d;
}

// A CAKP archive as the game lays one out: names in section 0, the scripts in section 1.
Bytes cakp(const std::vector<std::string>& names, const std::vector<Bytes>& scripts) {
    Bytes d = {'C', 'A', 'K', 'P'};
    add32(d, 0);
    for (int s = 0; s < 8; ++s)
        add32(d, 0xffffffffu);
    put32(d, 8, static_cast<std::uint32_t>(d.size()));
    add32(d, 1);
    const std::size_t names_slot = d.size();
    add32(d, 0);
    add32(d, 0);
    put32(d, 12, static_cast<std::uint32_t>(d.size()));
    add32(d, static_cast<std::uint32_t>(scripts.size()));
    const std::size_t script_slots = d.size();
    for (std::size_t i = 0; i < 2 * scripts.size(); ++i)
        add32(d, 0);
    // the names
    align(d, 0x20);
    const std::size_t names_at = d.size();
    Bytes table;
    add16(table, static_cast<std::uint32_t>(names.size()));
    std::size_t text = 2 + 2 * names.size();
    for (const auto& n : names) {
        add16(table, static_cast<std::uint32_t>(text));
        text += n.size() + 1;
    }
    for (const auto& n : names) {
        table.insert(table.end(), n.begin(), n.end());
        table.push_back(0);
    }
    d.insert(d.end(), table.begin(), table.end());
    put32(d, names_slot, static_cast<std::uint32_t>(names_at));
    put32(d, names_slot + 4, static_cast<std::uint32_t>(table.size()));
    for (std::size_t i = 0; i < scripts.size(); ++i) {
        align(d, 0x20);
        put32(d, script_slots + 4 * i, static_cast<std::uint32_t>(d.size()));
        put32(d, script_slots + 4 * (scripts.size() + i), static_cast<std::uint32_t>(scripts[i].size()));
        d.insert(d.end(), scripts[i].begin(), scripts[i].end());
    }
    align(d, 0x20);
    return d;
}

Bytes p2(const std::vector<Bytes>& entries) {
    P2File p;
    p.base = 0x200;
    p.head.assign(0x200, 0);
    p.head[0] = 'P';
    p.head[1] = '2';
    p.head[2] = static_cast<std::uint8_t>(entries.size());
    put32(p.head, 0xc, 0x200);
    for (const auto& e : entries) {
        P2Entry entry;
        entry.data = e;
        p.entries.push_back(entry);
    }
    return write_p2(p);
}

}  // namespace

TEST(a_script_archive_reads_back_byte_for_byte) {
    Bytes a, b, c;
    step(a, 0, 3, {});                                   // a step of no operands
    step(b, 2, 5, {0x00000001, 7, 0x00000010, 0x1800});  // an integer 7 and a 20.12 1.5
    step(c, 0, 1, {0x40});                               // one raw word
    const Bytes s0 = script({a, b, c});
    const Bytes s1 = script({b});
    const Bytes s2 = script({});
    const Bytes data = cakp({"main", "sub", "end"}, {s0, s1, s2});
    ScriptArchive arc;
    std::string why;
    CHECK(parse_script_archive(data, &arc, &why));
    CHECK(arc.names == std::vector<std::string>({"main", "sub", "end"}));
    CHECK(arc.scripts.size() == 3 && arc.scripts[0].name == "main" && arc.scripts[0].steps.size() == 3);
    CHECK(arc.scripts[2].steps.empty());
    const ScriptStep& st = arc.scripts[0].steps[1];
    CHECK(st.group == 2 && st.command == 5 && st.length() == 5);
    CHECK(write_script_archive(arc) == data);
    // the operands read by their modes
    const OperandSlot i = operand_at(st, 0), f = operand_at(st, 2);
    CHECK(i.mode == 1 && i.value == 7 && f.mode == 0x10 && f.value == 0x1800);
    CHECK(operands_text(st) == "7, 1.500");
    CHECK(operands_text(arc.scripts[0].steps[2]) == "@00044");  // 0.1 reads a word: a jump to block + 4 + 0x40
    // a step changed: the script's size follows
    arc.scripts[1].steps.push_back(arc.scripts[0].steps[0]);
    ScriptArchive again;
    CHECK(parse_script_archive(write_script_archive(arc), &again, &why));
    CHECK(again.scripts[1].steps.size() == 2 && again.scripts[1].steps[1].command == 3);
}

TEST(a_step_past_its_script_is_refused) {
    Bytes a;
    step(a, 0, 3, {1, 2});
    Bytes s = script({a});
    s[6] = 9;  // the step now claims nine words
    Script out;
    std::string why;
    CHECK(!parse_script(s, &out, &why) && why.find("past") != std::string::npos);
    s[6] = 0;  // and none
    CHECK(!parse_script(s, &out, &why));
}

TEST(a_mission_file_reads_back_entry_for_entry) {
    Bytes a;
    step(a, 0, 3, {});
    const Bytes arc1 = cakp({"one"}, {script({a})});
    const Bytes arc2 = cakp({"x", "y"}, {script({a, a}), script({})});
    Bytes table;
    add32(table, 2);
    add32(table, 12);
    add32(table, 14);
    add16(table, 0xbeef);
    add16(table, 0xcafe);
    const Bytes file = p2({arc1, table, Bytes(), arc2});
    MissionFile m;
    std::string why;
    CHECK(parse_mission(file, 101, &m, &why));
    CHECK(m.entries.size() == 4);
    CHECK(m.entries[0].kind == MissionEntry::script_archive && m.entries[1].kind == MissionEntry::table &&
          m.entries[2].kind == MissionEntry::empty && m.entries[3].kind == MissionEntry::script_archive);
    CHECK(m.entries[3].archive.scripts.size() == 2 && m.entries[3].archive.scripts[0].steps.size() == 2);
    P2File back;
    CHECK(parse_p2(write_mission(m), &back, &why) && back.entries.size() == 4);
    for (std::size_t e = 0; e < 4; ++e)
        CHECK(back.entries[e].data == m.p2.entries[e].data);
    CHECK(mission_path(101) == "mi/mi/0101" && mission_path(10000) == "mi/mi/10000");
}

TEST(the_mission_database_tables_read_back) {
    Bytes mdb;
    add32(mdb, 2);
    for (int r = 0; r < 2; ++r) {
        add32(mdb, static_cast<std::uint32_t>(r + 1));
        for (int k = 0; k < 14; ++k)
            add32(mdb, static_cast<std::uint32_t>(k));
    }
    CountedRecords c;
    std::string why;
    CHECK(parse_counted_records(mdb, 60, &c, &why) && c.records.size() == 2 && write_counted_records(c) == mdb);
    mdb.push_back(0);
    CHECK(!parse_counted_records(mdb, 60, &c, &why));  // its count is not its size
    Bytes evi;
    add32(evi, 2);
    add32(evi, 12);
    add32(evi, 16);
    add32(evi, 0x11111111);
    add32(evi, 0x22222222);
    add32(evi, 0x33333333);
    OffsetRecords o;
    CHECK(parse_offset_records(evi, &o, &why) && o.offsets.size() == 2 && write_offset_records(o) == evi);
    CHECK(o.record(0).size() == 4 && o.record(1).size() == 8);
}

TEST(the_command_tables_name_the_games_steps) {
    // the main program's first command and the field's (from the decompilation's tables)
    const CommandName* n = command_name(0, 0);
    CHECK(n != nullptr && std::strcmp(n->handler, "Game_ActionAssign") == 0);
    n = command_name(2, 0);
    CHECK(n != nullptr && std::strcmp(n->handler, "Ov002_ScriptCmd_EnterPhase") == 0);
    CHECK(command_name(18, 0) == nullptr);  // no table while the field runs
    CHECK(command_name_count() > 150);
}

TEST(entry_names_enemy_tables_and_meanings_read_as_the_game_reads_them) {
    // a mission of three entries with names in its P2 header
    Bytes start;
    step(start, 2, 26, {1, 1, 1, 4});  // LoadPeerIntoSlot(1, 4)
    const Bytes arc = cakp({"_i"}, {script({start})});
    Bytes em;
    em.push_back(2);  // rows
    em.push_back(2);  // groups
    add16(em, 0);
    add32(em, 0);   // the rows' offset, set below
    add32(em, 16);  // group 0
    add32(em, 24);  // group 1
    add16(em, 1);   // group 0: one part, total 3
    add16(em, 3);
    em.insert(em.end(), {0, 3, 3, 0});
    add16(em, 1);  // group 1: endless
    add16(em, 0xffff);
    em.insert(em.end(), {1, 0xff, 2, 0});
    put32(em, 4, static_cast<std::uint32_t>(em.size()));
    for (int r = 0; r < 2; ++r) {
        Bytes row(16, 0);
        row[0] = static_cast<std::uint8_t>(r == 0 ? 26 : 5);
        row[5] = 4;
        row[10] = row[11] = 0xff;  // drop key -1
        em.insert(em.end(), row.begin(), row.end());
    }
    Bytes file = p2({Bytes(), arc, em});
    const std::size_t names_at = 0x10 + 4 * 3 + 4 * 2;
    const char* names[] = {"11.Z", "_s.Z", "em0"};
    for (int i = 0; i < 3; ++i)
        std::memcpy(file.data() + names_at + 8 * i, names[i], std::strlen(names[i]));
    MissionFile m;
    std::string why;
    CHECK(parse_mission(file, 1, &m, &why));
    CHECK(m.entry_names == std::vector<std::string>({"11.Z", "_s.Z", "em0"}));
    CHECK(m.entry("_s") == 1 && m.entry("em0") == 2 && m.entry("99") == -1);
    EnemyTable t;
    CHECK(parse_enemy_table(m.p2.entries[2].data, &t, &why));
    CHECK(t.kinds.size() == 2 && t.kinds[0].id() == 26 && t.kinds[1].param() == 4 && t.kinds[0].drop_key() == -1);
    CHECK(t.groups.size() == 2 && t.groups[0].total == 3 && t.groups[0].parts[0].limit == 3 &&
          t.groups[0].parts[0].count == 3);
    CHECK(t.groups[1].parts[0].limit == 0xff && t.groups[1].parts[0].count == 2 && t.groups[1].parts[0].spawned() == 2);
    // the step's meaning, from its handler: a room into a slot
    const StepMeaning sm = step_meaning(m.entries[1].archive.scripts[0].steps[0]);
    CHECK(sm.kind == StepMeaning::load_room && sm.args == std::vector<std::string>({"1", "4"}));
    CHECK(meaning_text(sm) == "slot 1 <- room 4");
    // a command whose handler is not read: no meaning
    ScriptStep unknown;
    unknown.group = 2;
    unknown.command = 0;
    CHECK(step_meaning(unknown).kind == StepMeaning::none);
}

TEST(a_scripts_data_holds_its_names_and_texts_by_language) {
    // a script whose code queues "cmp" on event 0, shows a notice in each language and jumps
    Bytes queue, notice, jump, branch, keep;
    step(queue, 2, 5, {1, 0, 1, 0, 1, 0xffffffffu, 0, 0xffffffffu, 2, 0x0, 0, 0x0, 1, 0, 1, 0});
    step(notice, 2, 20, {0x40, 0x4});
    step(jump, 0, 1, {0x10});
    step(branch, 0, 5, {0x30, 0x8});
    step(keep, 0, 10, {0xffffffffu});
    Bytes s = script({queue, notice, jump, branch, keep});
    // its data: "cmp" at 0, a table of a text per language at 4 (none for 0), the texts after it
    const std::size_t data = s.size();
    s.insert(s.end(), {'c', 'm', 'p', 0});
    for (std::uint32_t k = 0; k < 6; ++k)
        add32(s, k == 0 ? 0xffffffffu : 0x1c + 4 * (k - 1));
    for (const char* t : {"en", "fr", "de", "it", "es"}) {
        s.insert(s.end(), t, t + 2);
        s.insert(s.end(), {0, 0});
    }
    const Bytes arc_data = cakp({"_i"}, {s});
    ScriptArchive arc;
    std::string why;
    CHECK(parse_script_archive(arc_data, &arc, &why));
    const Script& sc = arc.scripts[0];
    CHECK(sc.tail.size() == s.size() - data);
    CHECK(write_script_archive(arc) == arc_data);
    CHECK(script_string(sc, 0) == "cmp");
    CHECK(script_text(sc, 4, 1) == "en" && script_text(sc, 4, 5) == "es" && script_text(sc, 4, 0).empty());
    CHECK(script_string(sc, 4).empty());  // a table, not a text
    // the steps read with the script's data
    CHECK(operands_text(sc.steps[1], &sc, 3) == "text(data+0x4) \"de\"");
    CHECK(operands_text(sc.steps[2]) == "@00014");
    CHECK(operands_text(sc.steps[3]) == "data+0x30, @0000c");
    CHECK(operands_text(sc.steps[4]) == "-1");
    StepMeaning m = step_meaning(sc.steps[0], &sc);
    CHECK(m.kind == StepMeaning::on_event && m.args == std::vector<std::string>({"0", "cmp"}));
    CHECK(meaning_text(m) == "on event 0, run this area's script \"cmp\"");
    m = step_meaning(sc.steps[1], &sc, 2);
    CHECK(m.kind == StepMeaning::notice && m.args == std::vector<std::string>({"fr"}));
    CHECK(step_meaning(sc.steps[1]).args == std::vector<std::string>({"text(data+0x4)"}));  // no script: the place
    CHECK(meaning_text(step_meaning(sc.steps[2])) == "go to @00014");
    CHECK(meaning_text(step_meaning(sc.steps[3])) == "unless data+0x30, go to @0000c");  // no script: its place
    CHECK(step_meaning(sc.steps[4]).kind == StepMeaning::no_pending_jump);
    // a branch's expression in the data: state[8192:1] == 0 && !var[3]
    {
        Bytes ex = script({branch});
        const std::size_t base = ex.size();
        ex.resize(base + 0x30, 0);
        const auto push = [&](std::uint16_t mode, std::int32_t value) {
            add16(ex, 0);
            add16(ex, mode);
            add16(ex, 0);
            add32(ex, static_cast<std::uint32_t>(value));
        };
        push(4, 0x00012000);
        push(1, 0);
        add16(ex, 7);  // ==
        push(8, 3);
        add16(ex, 2);   // !
        add16(ex, 13);  // &&
        add16(ex, 0xf);
        ScriptArchive ea;
        CHECK(parse_script_archive(cakp({"_i"}, {ex}), &ea, &why));
        const Script& es = ea.scripts[0];
        CHECK(expression_text(es, 0x30) == "(state[8192:1] == 0) && !var[3]");
        CHECK(meaning_text(step_meaning(es.steps[0], &es)) == "unless (state[8192:1] == 0) && !var[3], go to @0000c");
        CHECK(expression_text(es, 0x31).empty());  // not where an expression starts: no end found or no reading
        CHECK(expression_text(es, 0x1000).empty());
    }
    // an event's condition: event 0 when the state's field 8326 (one bit) == 1, threshold 4000
    Bytes when;
    step(when, 2, 9, {1, 0, 1, 0, 1, 0xffffffffu, 1, 1, 1, 0, 4, 0x00012086, 1, 7, 1, 1, 1, 4000});
    Bytes w2 = script({when});
    ScriptArchive arc2;
    CHECK(parse_script_archive(cakp({"_i"}, {w2}), &arc2, &why));
    const StepMeaning wm = step_meaning(arc2.scripts[0].steps[0]);
    CHECK(wm.kind == StepMeaning::event_when);
    CHECK(meaning_text(wm) == "event 0 when state[8326:1] == 1, threshold 4000");
    // the time limit: 2.16 (7, 0, 180000), then shown as it runs; 2.74 adds 5 s to it
    Bytes limit, shown, more;
    step(limit, 2, 16, {1, 7, 1, 0, 1, 180000});
    step(shown, 2, 16, {1, 7, 1, 0xffffffffu, 1, 0xffffffffu});
    step(more, 2, 74, {1, 1, 1, 2, 1, 5000});
    ScriptArchive ta;
    CHECK(parse_script_archive(cakp({"_i"}, {script({limit, shown, more})}), &ta, &why));
    CHECK(meaning_text(step_meaning(ta.scripts[0].steps[0])) == "a time limit of 180 s on the HUD, counted from now");
    CHECK(step_meaning(ta.scripts[0].steps[1]).kind == StepMeaning::time_limit_shown);
    CHECK(meaning_text(step_meaning(ta.scripts[0].steps[2])) == "the time limit + 5 s");
    // a player's start: slot 1 near (1.5, 0, -2) facing -90 degrees; slot 2 at a marker named in the data
    Bytes at_point, at_marker;
    step(at_point, 2, 12,
         {1, 1, 1, 0, 0, 0, 0x10, 0x1800, 0x10, 0, 0x10, static_cast<std::uint32_t>(-0x2000), 1,
          static_cast<std::uint32_t>(-90)});
    step(at_marker, 2, 12, {1, 2, 1, 0, 2, 0});
    Bytes ms = script({at_point, at_marker});
    ms.insert(ms.end(), {'p', 'e', 'n', 't', '0', 0, 0, 0});
    ScriptArchive sa;
    CHECK(parse_script_archive(cakp({"_i"}, {ms}), &sa, &why));
    const Script& ss = sa.scripts[0];
    CHECK(meaning_text(step_meaning(ss.steps[0], &ss)) ==
          "player slot 1 near (1.500, 0.000, -2.000) facing -90 degrees, kind 0");
    CHECK(meaning_text(step_meaning(ss.steps[1], &ss)) == "player slot 2 at the room's marker \"pent0\", kind 0");
    const ScriptPlaces sp = script_places(ss);
    CHECK(sp.starts.size() == 2 && sp.starts[0].player == 1 && sp.starts[0].at[0] == 0x1800 &&
          sp.starts[0].at[2] == -0x2000 && sp.starts[0].heading == -90 && sp.starts[1].marker == "pent0");
}

TEST(a_missions_chests_and_emblems_read_as_the_field_reads_them) {
    // trbox's layout: a count, its offsets, then each mission's set: key, count, rows of 0x14
    Bytes d;
    add32(d, 2);
    add32(d, 0);
    add32(d, 0);
    const auto set = [&](std::uint16_t mission, int rows) {
        const std::size_t at = d.size();
        add16(d, mission);
        d.push_back(static_cast<std::uint8_t>(rows));
        d.push_back(0);
        for (int r = 0; r < rows; ++r) {
            d.push_back(static_cast<std::uint8_t>(r));      // slot
            d.push_back(2);                                 // bucket
            add16(d, 251);                                  // contents
            add16(d, static_cast<std::uint32_t>(608 + r));  // flag
            add16(d, 90);                                   // heading
            add32(d, 0x1800);
            add32(d, 0);
            add32(d, static_cast<std::uint32_t>(-0x3000));
        }
        return at;
    };
    put32(d, 4, static_cast<std::uint32_t>(set(951, 1)));
    put32(d, 8, static_cast<std::uint32_t>(set(952, 2)));
    std::vector<PlacedSet> sets;
    std::string why;
    CHECK(parse_placed_sets(d, &sets, &why));
    CHECK(sets.size() == 2 && sets[1].mission == 952 && sets[1].rows.size() == 2);
    const PlacedSet* s = placed_set(sets, 952);
    CHECK(s != nullptr && s->rows[1].b0 == 1 && s->rows[1].h2 == 251 && s->rows[1].h4 == 609 && s->rows[1].h6 == 90 &&
          s->rows[1].at[0] == 0x1800 && s->rows[1].at[2] == -0x3000);
    CHECK(placed_set(sets, 10) == nullptr);
    d.resize(d.size() - 4);  // a row cut short
    CHECK(!parse_placed_sets(d, &sets, &why));
    // the steps that spawn them
    Bytes chests, emblems;
    step(chests, 2, 76, {1, 6, 2, 0, 0x10, 0x2000});
    step(emblems, 2, 83, {1, 7, 1, 8, 2, 0x304, 2, 0x3c0});
    Bytes sc = script({chests, emblems});
    sc.insert(sc.end(), {'b', 'o', 'x', 0});
    ScriptArchive a;
    CHECK(parse_script_archive(cakp({"_s"}, {sc}), &a, &why));
    CHECK(meaning_text(step_meaning(a.scripts[0].steps[0], &a.scripts[0])) ==
          "the mission's chests from trbox, their class in module slot 6, model \"box\"");
    CHECK(step_meaning(a.scripts[0].steps[1]).kind == StepMeaning::emblems);
}

namespace {

Bytes file_bytes(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_bytes(const std::filesystem::path& f, const Bytes& d) {
    std::filesystem::create_directories(f.parent_path());
    std::ofstream(f, std::ios::binary)
        .write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

// A mission like the game's: _s.Z with a start (MoveSlot), a spawner of group 0, a time limit and a
// notice in five languages; em0 with two kinds and one group.
Bytes edit_mission() {
    Bytes start, spawner, time, notice;
    step(start, 2, 12, {1, 0, 1, 0, 0, 0, 1, 3, 1, 1, 1, 7, 1, 90});         // player 0 at (3, 1, 7) facing 90
    step(spawner, 2, 32, {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 1, 0, 1, 5});  // group 0 at (2, 0, 5)
    step(time, 2, 16, {1, 7, 1, 0, 1, 60000});                               // 60 s
    step(notice, 2, 20, {0x40, 0});
    Bytes sc = script({start, spawner, time, notice});
    for (std::uint32_t k = 0; k < 6; ++k)
        add32(sc, k == 0 ? 0xffffffffu : 0x18 + 4 * (k - 1));
    for (const char* t : {"en", "fr", "de", "it", "es"}) {
        sc.insert(sc.end(), t, t + 2);
        sc.insert(sc.end(), {0, 0});
    }
    Bytes em;
    em.push_back(2);
    em.push_back(1);
    add16(em, 0);
    add32(em, 0);
    add32(em, 12);
    add16(em, 1);
    add16(em, 3);
    em.insert(em.end(), {0, 3, 3, 0});
    put32(em, 4, static_cast<std::uint32_t>(em.size()));
    for (int r = 0; r < 2; ++r) {
        Bytes row(16, 0);
        row[0] = static_cast<std::uint8_t>(r == 0 ? 0 : 70);  // Shadow, Darkside
        em.insert(em.end(), row.begin(), row.end());
    }
    Bytes file = p2({cakp({"_i"}, {sc}), em});
    const std::size_t names_at = 0x10 + 4 * 2 + 4 * 1;
    std::memcpy(file.data() + names_at, "_s.Z", 4);
    std::memcpy(file.data() + names_at + 8, "em0", 3);
    return file;
}

// trbox / evi with one set: the mission's, one row
Bytes edit_sets(std::uint16_t mission) {
    Bytes d;
    add32(d, 1);
    add32(d, 8);
    add16(d, mission);
    d.push_back(1);
    d.push_back(0);
    d.insert(d.end(), {0, 2});
    add16(d, 251);
    add16(d, 608);
    add16(d, 90);
    add32(d, 0x1000);
    add32(d, 0);
    add32(d, 0x2000);
    return d;
}

}  // namespace

TEST(a_mission_changed_in_place_and_saved_into_the_mod) {
    const auto root = studio_test::scratch("mission_edit");
    const auto nitrofs = root / "nitrofs", mod = root / "mods" / "M";
    write_bytes(nitrofs / "mi/mi/0105", edit_mission());
    write_bytes(nitrofs / "mi/mi/trbox", edit_sets(105));
    write_bytes(nitrofs / "mi/mi/evi", edit_sets(105));
    std::filesystem::create_directories(mod);

    MissionEdit e;
    std::string why;
    CHECK(load_mission_edit(nitrofs, mod, 105, &e, &why));
    CHECK(!e.mission_from_mod && e.trbox_sets.size() == 1);
    Script& sc = e.mission.entries[0].archive.scripts[0];
    // the start moved and turned, the spawner moved, the time made 90 s
    set_fx(sc.steps[0], 3, 4 * 4096 + 2048);
    set_int(sc.steps[0], 6, 180);
    set_fx(sc.steps[1], 5, -4096);
    set_int(sc.steps[2], 2, 90000);
    ScriptPlaces places = script_places(sc);
    CHECK(places.starts.size() == 1 && places.starts[0].at[0] == 4 * 4096 + 2048 && places.starts[0].at[1] == 4096 &&
          places.starts[0].heading == 180);
    CHECK(places.spawners.size() == 1 && places.spawners[0].at[0] == -4096);
    CHECK(meaning_text(step_meaning(sc.steps[2], &sc)) == "a time limit of 90 s on the HUD, counted from now");
    // a longer text in Spanish: written past the data, the others kept
    CHECK(set_step_text(sc, sc.steps[3], 0, 5, "Derrota al Darkside"));
    CHECK(script_text(sc, 0, 5) == "Derrota al Darkside" && script_text(sc, 0, 1) == "en");
    CHECK(!set_step_text(sc, sc.steps[2], 0, 5, "x"));  // an integer: no text there
    // the enemy group: 5 of row 1 (the Darkside)
    auto& em = e.mission.p2.entries[static_cast<std::size_t>(e.mission.entry("em0"))].data;
    EnemyTable t;
    CHECK(parse_enemy_table(em, &t, &why));
    t.groups[0].parts[0].row = 1;
    t.groups[0].parts[0].count = 5;
    t.groups[0].parts[0].limit = 5;
    CHECK(write_enemy_table_over(em, t, &why));
    EnemyTable back;
    CHECK(parse_enemy_table(em, &back, &why) && back.groups[0].parts[0].row == 1 &&
          back.groups[0].parts[0].spawned() == 5 && back.groups[0].total == 5);
    t.groups[0].parts.push_back({});  // a part added: refused, nothing moves
    CHECK(!write_enemy_table_over(em, t, &why));
    // a chest moved, its contents changed
    PlacedSet* chests = placed_set_of(e.trbox_sets, 105);
    CHECK(chests != nullptr);
    chests->rows[0].at[2] = -0x3000;
    chests->rows[0].h2 = 300;
    CHECK(write_placed_set_over(e.trbox, *chests, &why));

    std::vector<std::string> written;
    CHECK(save_mission_edit(nitrofs, mod, e, &written, &why));
    CHECK(std::filesystem::exists(mod / "files/mi/mi/0105") && std::filesystem::exists(mod / "files/mi/mi/trbox"));
    CHECK(!std::filesystem::exists(mod / "files/mi/mi/evi"));  // unchanged: the game's
    // read back from the mod: every change there
    MissionEdit again;
    CHECK(load_mission_edit(nitrofs, mod, 105, &again, &why));
    CHECK(again.mission_from_mod && again.trbox_from_mod && !again.evi_from_mod);
    const Script& sc2 = again.mission.entries[0].archive.scripts[0];
    CHECK(script_places(sc2).starts[0].heading == 180 && script_text(sc2, 0, 5) == "Derrota al Darkside");
    const PlacedSet* c2 = placed_set(again.trbox_sets, 105);
    CHECK(c2 != nullptr && c2->rows[0].h2 == 300 && c2->rows[0].at[2] == -0x3000);
    // given back: the mod's copies gone (trbox then the game's again)
    CHECK(give_back_mission(nitrofs, mod, 105, &why));
    CHECK(!std::filesystem::exists(mod / "files/mi/mi/0105") && !std::filesystem::exists(mod / "files/mi/mi/trbox"));
    // saving the game's own values writes nothing
    MissionEdit plain;
    CHECK(load_mission_edit(nitrofs, mod, 105, &plain, &why));
    written.clear();
    CHECK(save_mission_edit(nitrofs, mod, plain, &written, &why) && written.empty());
    CHECK(file_bytes(nitrofs / "mi/mi/0105") == edit_mission());
}

TEST(a_mission_made_from_another_in_its_slot) {
    const auto root = studio_test::scratch("mission_copy");
    const auto nitrofs = root / "nitrofs", mod = root / "mods" / "M";
    write_bytes(nitrofs / "mi/mi/0105", edit_mission());
    write_bytes(nitrofs / "mi/mi/0106", edit_mission());
    // trbox with a set of one row for each; evi only for 105 (106 has none)
    Bytes trbox;
    add32(trbox, 2);
    add32(trbox, 12);
    add32(trbox, 12 + 24);
    for (const int mission : {105, 106}) {
        add16(trbox, static_cast<std::uint32_t>(mission));
        trbox.push_back(1);
        trbox.push_back(0);
        trbox.insert(trbox.end(), {0, 2});
        add16(trbox, mission == 105 ? 251 : 7);
        add16(trbox, 608);
        add16(trbox, 90);
        add32(trbox, 0);
        add32(trbox, 0);
        add32(trbox, 0);
    }
    write_bytes(nitrofs / "mi/mi/trbox", trbox);
    write_bytes(nitrofs / "mi/mi/evi", edit_sets(105));
    std::filesystem::create_directories(mod);

    MissionEdit from;
    std::string why;
    CHECK(load_mission_edit(nitrofs, mod, 105, &from, &why));
    set_int(from.mission.entries[0].archive.scripts[0].steps[2], 2, 45000);  // an unsaved change: 45 s
    std::vector<std::string> written;
    bool sets = true;
    CHECK(copy_mission_to(nitrofs, mod, from, 106, &written, &sets, &why));
    CHECK(!sets);  // evi: 106 has no set to take 105's emblems
    MissionEdit made;
    CHECK(load_mission_edit(nitrofs, mod, 106, &made, &why));
    CHECK(made.mission_from_mod && made.trbox_from_mod);
    CHECK(meaning_text(step_meaning(made.mission.entries[0].archive.scripts[0].steps[2],
                                    &made.mission.entries[0].archive.scripts[0])) ==
          "a time limit of 45 s on the HUD, counted from now");
    const PlacedSet* c106 = placed_set(made.trbox_sets, 106);
    const PlacedSet* c105 = placed_set(made.trbox_sets, 105);
    CHECK(c106 != nullptr && c106->rows[0].h2 == 251 && c105 != nullptr &&
          c105->rows[0].h2 == 251);                             // 105's chest in 106's slot, 105 untouched
    CHECK(!std::filesystem::exists(mod / "files/mi/mi/0105"));  // the source stays the game's
}

namespace {

// A mission file with named entries (8 bytes each after the descriptors).
Bytes named_p2(const std::vector<Bytes>& entries, const std::vector<std::string>& names) {
    Bytes file = p2(entries);
    const std::size_t n = entries.size(), names_at = 0x10 + ((n + 1) / 2) * 4 + 4 * n;
    for (std::size_t e = 0; e < n; ++e)
        std::memcpy(file.data() + names_at + 8 * e, names[e].data(), names[e].size());
    return file;
}

constexpr std::uint32_t kInt = 1, kFixed = 0x10, kField = 4;

std::uint32_t field(int f, int bits) {
    return static_cast<std::uint32_t>(f | bits << 16);
}

// The pieces the mission builder takes from the game: 1354's area 11 (its steps, c0, c1), its start, its
// enemy table (classes 0-4); 0004's "all defeated" check (2.7).
studio::GameFiles builder_game() {
    Bytes i;
    step(i, 2, 0, {kInt, 1});
    step(i, 2, 25, {kInt, 4, kInt, 1});
    step(i, 2, 26, {kInt, 0, kInt, 2});
    step(i, 2, 13, {kInt, 0xffffffffu});
    step(i, 2, 36, {kInt, 4, kInt, 1});
    step(i, 2, 48, {kInt, 0});
    step(i, 2, 14, {kInt, 0});
    step(i, 2, 49, {kInt, 0, kInt, 0});
    std::vector<std::uint32_t> spawner;
    for (int s = 0; s < 16; ++s)
        spawner.insert(spawner.end(), {s >= 5 ? kFixed : kInt, 0});
    step(i, 2, 32, spawner);
    step(i, 2, 50, {});
    step(i, 2, 10, {kInt, 0});
    for (std::uint32_t p = 0; p < 4; ++p)
        step(i, 2, 12, {kInt, p, kInt, 2, 0, 2, kFixed, 0, kFixed, 0, kFixed, 0, kInt, 0});
    for (std::uint32_t k = 0; k < 5; ++k)
        step(i, 2, 39, {kInt, 0, kInt, k, kInt, 0, kInt, 0xffffffffu});
    step(i, 2, 16, {kInt, 0, kInt, 0, kInt, 0});
    step(i, 2, 16, {kInt, 7, kInt, 0, kInt, 60000});
    step(i, 2, 20, {kInt, 0});
    step(i, 2, 53, {kInt, 0, kInt, 0, kInt, 2000});
    step(i, 2, 6, {kInt, 0, kInt, 3, kInt, 4, kInt, 0});  // the clock's watch (kind 4)
    step(i, 2, 5, {kInt, 0, kInt, 3, kInt, 2, 0, 2, kInt, 0, 0, 0, kInt, 0, kInt, 0});
    step(i, 2, 9,
         {kInt, 0, kInt, 1, kInt, 0xffffffffu, kInt, 1, kInt, 0, kField, field(8844, 1), kInt, 7, kInt, 1, kInt, 4000});
    step(i, 0, 16, {kInt, 21, kInt, 15});
    step(i, 2, 1, {kInt, 4});
    step(i, 0, 3, {});
    Bytes end;
    step(end, 0, 3, {});
    const Bytes area = cakp({"c0", "c1", "_i"}, {script({end}), script({end}), script({i})});

    Bytes s;
    step(s, 2, 0, {kInt, 1});
    for (const int c : {78, 76, 83, 31, 46})
        step(s, 2, static_cast<std::uint8_t>(c), {kInt, 0, kInt, 0, kInt, 0});
    step(s, 0, 0, {kField, field(8846, 1), kInt, 1});
    step(s, 4, 16, {kInt, 0});
    step(s, 2, 34, {kInt, 0});
    step(s, 2, 52, {kInt, 0, kInt, 0});
    for (const auto& [f, bits] :
         std::vector<std::pair<int, int>>{{8840, 4}, {8833, 4}, {8844, 1}, {8837, 1}, {8838, 1}, {8839, 1}, {6318, 1}})
        step(s, 0, 0, {kField, field(f, bits), kInt, 0});
    step(s, 2, 3, {0, 0xffffffffu, kInt, 11, kInt, 0});
    step(s, 0, 3, {});
    const Bytes start = cakp({"dg", "_i"}, {script({end}), script({s})});

    Bytes em;
    em.push_back(5);
    em.push_back(1);
    add16(em, 0);
    add32(em, 12);
    add32(em, 12 + 5 * 16);
    for (std::uint8_t c = 0; c < 5; ++c) {
        Bytes row(16, 0);
        row[0] = c;
        row[4] = 0xfb;  // the row's own level adjustment: the built one's is 0
        em.insert(em.end(), row.begin(), row.end());
    }
    em.insert(em.end(), {1, 0, 1, 0, 0, 1, 1, 0});
    const Bytes base = named_p2({area, start, em, em}, {"11.Z", "_s.Z", "em0", "em0m"});

    Bytes c;
    step(c, 2, 7, {kInt, 0, kInt, 3, kInt, 1, kInt, 0xffffffffu, kInt, 5, kInt, 9});
    const Bytes check = named_p2({cakp({"_i"}, {script({c})})}, {"11.Z"});

    studio::GameFiles g;
    g.read = [base, check](const std::string& path) {
        return path == "mi/mi/1354" ? base : path == "mi/mi/0004" ? check : Bytes();
    };
    g.missions = {4, 1354};
    return g;
}

studio::WaveSpawner spawner_of(std::vector<std::pair<int, int>> enemies, double z) {
    studio::WaveSpawner w;
    for (const auto& [c, n] : enemies)
        w.enemies.push_back({c, n});
    w.at = {0.0, 0.049, z};
    return w;
}

int count_steps(const Script& s, int group, int command) {
    int n = 0;
    for (const ScriptStep& st : s.steps)
        n += st.group == group && st.command == command;
    return n;
}

}  // namespace

TEST(a_mission_with_waves_built_of_the_games_own_steps) {
    const studio::GameFiles game = builder_game();
    studio::MissionPlan plan;
    plan.world = 4;
    plan.room = 2;
    plan.start = {0.0, 0.049, 0.0};
    plan.facing = -90;
    plan.time_ms = 120000;
    plan.notice = {"Two Waves", "", "", "", "Dos oleadas"};
    plan.objective = {"Defeat them all.", "", "", "", ""};
    plan.waves = {{spawner_of({{0, 1}}, -1.5)}, {spawner_of({{0, 2}}, 1.5), spawner_of({{3, 1}}, 3.0)}};
    Bytes built;
    std::string why;
    CHECK(studio::build_mission(game, plan, &built, &why));
    MissionFile m;
    CHECK(parse_mission(built, 20000, &m, &why));
    const int area = m.entry("11"), table = m.entry("em0");
    CHECK(area >= 0 && table >= 0);
    const ScriptArchive& a = m.entries[static_cast<std::size_t>(area)].archive;
    CHECK(a.names == std::vector<std::string>({"c0", "c1", "w2", "_i"}));
    const Script& w2 = a.scripts[2];
    const Script& i = a.scripts[3];
    CHECK(count_steps(i, 2, 32) == 1 && count_steps(w2, 2, 32) == 2);  // each wave's spawners in its script
    CHECK(count_steps(i, 2, 49) == 4);                                 // an actor for every enemy of every wave
    CHECK(count_steps(i, 2, 7) == 1 && count_steps(w2, 2, 7) == 1);    // each wave's "all defeated"
    CHECK(operand_at(i.steps[1], 0).value == 4 && operand_at(i.steps[2], 2).value == 2);  // world, room
    EnemyTable t;
    CHECK(parse_enemy_table(m.p2.entries[static_cast<std::size_t>(table)].data, &t, &why));
    CHECK(t.kinds.size() == 2 && t.groups.size() == 3);  // classes 0 and 3; three spawners
    CHECK(t.kinds[0].raw[4] == 0);                       // the level: the mission's own

    // the game's limits, said: an area's four enemy classes; a class no game mission has
    plan.waves = {{spawner_of({{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}}, 0.0)}};
    CHECK(!studio::build_mission(game, plan, &built, &why) && why.find("four") != std::string::npos);
    plan.waves = {{spawner_of({{99, 1}}, 0.0)}};
    CHECK(!studio::build_mission(game, plan, &built, &why) && why.find("99") != std::string::npos);
}

TEST(a_new_missions_waves_chests_and_emblems_added_and_taken_away) {
    const auto mod = studio_test::scratch("mission_lines") / "mods" / "M";
    const auto txt = mod / "missions" / "missions.txt";
    studio_test::write(txt, "[mission waves]\nname en = W\nobjective en = O\nworld = eh\nroom = 2\n\n"
                            "[mission other]\nname en = X\nobjective en = Y\nworld = tt\n");
    std::string why;
    CHECK(studio::set_mod_mission(mod, "waves", "start", "", "0 0.049 0 -90", &why));
    CHECK(studio::add_mod_mission_line(mod, "waves", "wave 1", "0x1 at 0 0.049 -1.5", &why));
    CHECK(studio::add_mod_mission_line(mod, "waves", "wave 2", "0x2 at 0 0.049 1.5", &why));
    CHECK(studio::add_mod_mission_line(mod, "waves", "wave 2", "3x1 at 4 0 4", &why));
    CHECK(studio::add_mod_mission_line(mod, "waves", "chest", "257 at 1 0 -2 180", &why));
    CHECK(studio::add_mod_mission_line(mod, "waves", "emblem", "2 at -1 0 2", &why));
    const Bytes before = file_bytes(txt);
    CHECK(!studio::add_mod_mission_line(mod, "waves", "wave 3", "0x0 at 0 0 0", &why));  // no enemy: refused
    CHECK(file_bytes(txt) == before);                                                    // the file as it was
    std::string text(before.begin(), before.end());
    const auto waves_end = text.find("[mission other]");
    CHECK(waves_end != std::string::npos && text.find("wave 2 = 3x1 at 4 0 4") < waves_end &&
          text.find("chest = 257 at 1 0 -2 180") < waves_end && text.find("start = 0 0.049 0 -90") < waves_end);
    int removed = 0;
    CHECK(studio::clear_mod_mission_lines(mod, "waves", "wave 2", &removed, &why) && removed == 2);
    CHECK(studio::clear_mod_mission_lines(mod, "waves", "wave", &removed, &why) && removed == 1);
    CHECK(studio::clear_mod_mission_lines(mod, "waves", "emblem", &removed, &why) && removed == 1);
    const Bytes after = file_bytes(txt);
    text.assign(after.begin(), after.end());
    CHECK(text.find("wave 1 =") == std::string::npos && text.find("wave 2 =") == std::string::npos &&
          text.find("emblem") == std::string::npos && text.find("chest = 257") != std::string::npos &&
          text.find("[mission other]") != std::string::npos);
}

TEST(a_new_missions_places_read_and_saved_at_once) {
    const auto root = studio_test::scratch("mission_places");
    const auto mod = root / "mods" / "M";
    const auto txt = mod / "missions" / "missions.txt";
    studio_test::write(txt, "[mission map]\nname en = W\nobjective en = O\nworld = eh\nroom = 2\n"
                            "start = 1 0.049 -2 90\nwave 1 = 0x2 32x1 at 0 0.049 1.5\nchest = 257 at 1.25 0 -2 180\n"
                            "emblem = 1 at -1 0 2\nlevel = 5\n\n[mission other]\nname en = X\nobjective en = Y\n"
                            "world = tt\n");
    std::string why;
    using Kind = studio::MissionSpot::Kind;
    auto spots = studio::mod_mission_spots(mod, "map", &why);
    CHECK(spots.size() == 4 && spots[0].kind == Kind::start && spots[0].heading == 90);
    CHECK(spots[1].kind == Kind::spawner && spots[1].wave == 1 && spots[1].enemies.size() == 2 &&
          spots[1].enemies[1] == std::make_pair(32, 1) && std::fabs(spots[1].at[2] - 1.5) < 1e-9);
    CHECK(spots[2].kind == Kind::chest && spots[2].contents == 257 && spots[2].heading == 180);
    CHECK(spots[3].kind == Kind::emblem && spots[3].emblem == 1);
    CHECK(studio::mission_spot_line(spots[1]) ==
          std::make_pair(std::string("wave 1"), std::string("0x2 32x1 at 0 0.049 1.5")));
    CHECK(studio::mission_spot_line(spots[2]) ==
          std::make_pair(std::string("chest"), std::string("257 at 1.25 0 -2 180")));
    CHECK(studio::mod_mission_spots(mod, "nothing", &why).empty() && !why.empty());

    // the places made others at once: where the first one was, the block's other lines kept
    spots[0].at = {3.5, 0.049, 4};
    spots[3].emblem = 2;
    spots.erase(spots.begin() + 1);  // no wave: nothing to build
    studio::MissionSpot chest;
    chest.kind = Kind::chest;
    chest.at = {-2, 0, 0.125};
    chest.contents = 3;
    spots.push_back(chest);
    const auto nitrofs = root / "nitrofs";  // no game: a mission with waves would not build
    CHECK(studio::set_mod_mission_spots(nitrofs, mod, "map", spots, &why));
    const Bytes after = file_bytes(txt);
    const std::string text(after.begin(), after.end());
    CHECK(text.find("start = 3.5 0.049 4 90\nchest = 257 at 1.25 0 -2 180\nemblem = 2 at -1 0 2\n"
                    "chest = 3 at -2 0 0.125 0\nlevel = 5\n") != std::string::npos);
    CHECK(text.find("wave") == std::string::npos && text.find("[mission other]") != std::string::npos);
    CHECK(studio::mod_mission_spots(mod, "map", &why).size() == 4);

    // a wave that does not build (here: no game files) leaves the file as it was
    studio::MissionSpot wave;
    wave.kind = Kind::spawner;
    wave.enemies = {{0, 1}};
    spots.push_back(wave);
    CHECK(!studio::set_mod_mission_spots(nitrofs, mod, "map", spots, &why) && why.find("build") != std::string::npos);
    CHECK(file_bytes(txt) == after);
}

TEST(the_games_missions_named_from_its_list) {
    // a made-up list: two rows of file 0101 (the story's, then Mission Mode's) and one of 0102
    auto record = [](int file, int number, int category, int shown, int kind, const std::u16string& name) {
        std::string r(0x30, '\0');
        auto put16 = [&r](std::size_t at, int v) {
            r[at] = static_cast<char>(v & 0xff);
            r[at + 1] = static_cast<char>((v >> 8) & 0xff);
        };
        put16(2, file);
        put16(4, number);
        put16(0xa, category);
        put16(0xe, shown);
        r[0x23] = static_cast<char>(kind);
        for (const char16_t c : name + u'\0') {
            r += static_cast<char>(c & 0xff);
            r += static_cast<char>(c >> 8);
        }
        put16(0, static_cast<int>(r.size()));
        return r;
    };
    const std::string list = std::string("\x03\0", 2) + record(101, 7, 0, 3, 0, u"Story row") +
                             record(101, 7, 0, 5, 4, u"Colección") + record(102, 8, 9, 6, 0, u"Other");
    const auto root = studio_test::scratch("mission_names");
    studio_test::write(root / "decompressed" / "UI" / "cm" / "msl_es.msi", list);
    const auto names = studio::game_mission_names(root / "nitrofs", "es");
    CHECK(names.size() == 2);
    CHECK(names.at(101).mission_mode && names.at(101).shown == 5 && names.at(101).name == "Colección");
    CHECK(!names.at(102).mission_mode && names.at(102).number == 8 && names.at(102).category == 9);
    CHECK(studio::game_mission_names(root / "nitrofs", "fr").empty());  // no list: none
}
