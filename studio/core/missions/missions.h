// The game's missions as its script machine reads them, and written back: mission files, script
// archives, scripts, steps and their operand slots (docs/FORMATS.md, "Missions"). What a step means is
// the handler its group's table gives it (command_names.h); what each handler does with its words is
// read one command at a time and never guessed. Everything is written back as read.
#pragma once

#include "core/files/containers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace studio {

struct ScriptStep {
    std::uint8_t group = 0, command = 0, field = 0;
    std::vector<std::uint32_t> words;  // after the 4-byte header
    std::size_t length() const {
        return 1 + words.size();
    }  // in words
};

struct Script {
    std::string name;
    std::vector<ScriptStep> steps;
    std::vector<std::uint8_t> tail;  // its data: the bytes of its block past its code (texts, tables, expressions)
};

// A script archive: the archive as laid out (its blocks), its names, and its
// scripts parsed from section 1's blocks.
struct ScriptArchive {
    KaphFile pack;
    std::vector<std::string> names;
    std::vector<Script> scripts;            // in section 1's order
    std::vector<std::size_t> script_block;  // each script's block in `pack.blocks`
};

struct MissionEntry {
    enum Kind { empty, script_archive, table } kind = empty;
    ScriptArchive archive;  // when an archive
};

struct MissionFile {
    int number = 0;
    P2File p2;
    std::vector<MissionEntry> entries;
    // each entry's name, from the P2 header (8 bytes each after its tables:
    // MsgArchive_FindEntryByName): `_s.Z` the start (its script `_i` runs when
    // the field is built: Ov002_StreamFormattedLine("_s", "_i")), `11.Z`,
    // `21.Z` ... areas loaded by number, `em0` / `em0m` the enemy table (single
    // player / multiplayer: Ov002_LoadObjectRecordsAndDrops)
    std::vector<std::string> entry_names;
    int entry(const std::string& name) const;  // the entry whose name starts with `name`, or -1
};

// A mission's enemy table (`em0`, `em0m`) as Ov002_LoadObjectRecordsAndDrops reads it: rows (an enemy
// kind, its drop) and groups of parts {row, limit, count, last player}, a spawner adding `count` of the
// row's class up to `limit` (docs/FORMATS.md, "Missions").
struct EnemyKind {
    std::array<std::uint8_t, 16> raw{};
    int id() const {
        return raw[0];
    }
    int kind() const {
        return raw[1] & 15;
    }
    int high() const {
        return raw[1] >> 4;
    }
    int param() const {
        return static_cast<std::int8_t>(raw[5]);
    }
    int drop_key() const {
        return static_cast<std::int16_t>(raw[10] | raw[11] << 8);
    }
    int drop_chance() const {
        return static_cast<std::int16_t>(raw[12] | raw[13] << 8);
    }
};
struct EnemyGroupPart {
    std::uint8_t row = 0, limit = 0, count = 0, last_player = 0;
    int spawned() const {
        return limit == 0xff ? count : std::min<int>(limit, count);
    }
};
struct EnemyGroup {
    std::uint8_t state = 0;   // +1 (bit 0: the group already made, Ov002_CreateSpawner skips it)
    std::uint16_t total = 0;  // +2, not read by the code
    std::vector<EnemyGroupPart> parts;
};
struct EnemyTable {
    std::vector<EnemyKind> kinds;
    std::vector<EnemyGroup> groups;
};
bool parse_enemy_table(const std::vector<std::uint8_t>& d, EnemyTable* out, std::string* why);

// An operand slot read from two words, and whether its mode is one the
// script machine reads as a slot (0, 1, 2, 4, 8, 0x10, 0x40 or with the 0x80
// bit: every step of the game's scripts but raw_step's reads as such slots).
struct OperandSlot {
    std::int16_t mode = 0, sel = 0;
    std::int32_t value = 0;
};
OperandSlot operand_at(const ScriptStep& s, std::size_t word);
bool slot_mode(std::int16_t mode);
// The three commands that read their words as they are, not as slots: 0.1
// ScriptCmd_SetElemOffset (go to the step at block + 4 + word), 0.5
// CommitRecordCursor (unless the expression at data + word 0 holds, go to
// block + 4 + word 1: Script_EvalExpr), 0.10 Game_ActionSetBranchTarget (a
// jump kept for later, to block + 4 + word; none when -1).
bool raw_step(const ScriptStep& s);
// The text at `offset` of a script's data (up to its NUL, no control
// characters), empty when there is none.
std::string script_string(const Script& s, std::uint32_t offset);
// A mode-0x40 operand's text (ByteCode_ResolveOperand): the table at `table`
// of the script's data holds a word per language -- GetLanguage's 1 en, 2 fr,
// 3 de, 4 it, 5 es -- the offset of its text in the data, or -1.
std::string script_text(const Script& s, std::uint32_t table, int language);
// One slot as text: its integer, number, field, variable, place in the
// data (with the text there, when the script is given) or table entry.
std::string slot_text(const OperandSlot& o, const Script* script = nullptr, int language = 1);
// The expression at `offset` of a script's data (Script_EvalExpr): postfix
// code of u16 -- 0 pushes the 8-byte slot after it, 1 negates, 2 is not, 3-6
// + - * /, 7-12 == > < >= <= !=, 13 &&, 14 ||, 0xf ends it -- as infix text;
// empty when it does not read so (an unknown code, too few values, no end,
// or not one value left).
std::string expression_text(const Script& s, std::uint32_t offset, int language = 1);
// A step's words as text: each pair read as a slot where its mode is one,
// else the word in hex; a jump's target as the step's offset in the script
// (@offset); with the script, a text its data holds where an operand points
// (in `language`, as script_text). A reading aid: a command's own layout is
// read from its handler before its words are given a meaning.
std::string operands_text(const ScriptStep& s, const Script* script = nullptr, int language = 1);

bool parse_script(const std::vector<std::uint8_t>& block, Script* out, std::string* why);
std::vector<std::uint8_t> write_script(const Script& s);

bool parse_script_archive(const std::vector<std::uint8_t>& data, ScriptArchive* out, std::string* why);
std::vector<std::uint8_t> write_script_archive(const ScriptArchive& a);

bool parse_mission(const std::vector<std::uint8_t>& file, int number, MissionFile* out, std::string* why);
std::vector<std::uint8_t> write_mission(const MissionFile& m);

// The mission files the extraction has (`mi/mi/<digits>`), by number.
std::vector<int> mission_numbers(const std::filesystem::path& nitrofs);
std::string mission_path(int number);  // mi/mi/0101 (four digits at least)
bool load_mission(const std::filesystem::path& nitrofs, int number, MissionFile* out, std::string* why);

// --check-missions: every mission file and the mission database's files
// read and written back, every script walked; one line per file and a total
// (with the steps whose command no table names); false on any difference.
bool check_all_missions(const std::filesystem::path& nitrofs, const std::function<void(const std::string&)>& line);

}  // namespace studio
