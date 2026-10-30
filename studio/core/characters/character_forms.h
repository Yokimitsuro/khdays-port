// A character's forms and stats in its character.ini (docs/CHARACTERS.md, "Forms" and "Stats"): the forms' parts
// ([form <id>], [form <id> actions] ...) found, added, renamed, copied, taken away and put in order; the [stats]
// lines as a table of levels; the base's own level table (ba/ch/<code>/lv.b.z) read from the player's copy.
#pragma once

#include "core/characters/character_files.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace studio {

// A form's sections besides its own part (`[form <id> <section>]`).
const std::vector<std::string>& form_sections();

// The part of form `id` (empty: the character's own) for `section` ("" its own part, "actions" ...). The
// character's own: "model" for the form's part, the section's name for the rest.
std::string form_part(const std::string& id, const std::string& section);

// The forms in the order the port numbers them (each one's first part), and each form's parts in the text.
std::vector<std::string> form_ids(const IniText& ini);
std::vector<std::string> form_part_names(const IniText& ini, const std::string& id);

// The game's characters a form plays as: a character's bases and the dual-wield Roxas (roxas_dual, r2).
const std::vector<std::string>& form_bases();
std::string form_base_code(const std::string& base);

// A form id is letters, digits, '_' and '-', not "default".
bool valid_form_id(const std::string& id);

// Changes of a form's parts; each keeps the rest of the text as written. False with the reason (an id taken ...).
bool add_form(IniText* ini, const std::string& id, std::string* why);
bool rename_form(IniText* ini, const std::string& from, const std::string& to, std::string* why);
bool duplicate_form(IniText* ini, const std::string& from, const std::string& to, std::string* why);
void remove_form(IniText* ini, const std::string& id);
// One place earlier (-1) or later (+1) among the forms (a tie of priorities goes to the earlier).
void move_form(IniText* ini, const std::string& id, int step);

// The description's format raised to what its forms and stats need (2), never lowered.
void raise_format(IniText* ini);

// The six values of a level row, by KhdaysCharacterStat.
const std::vector<std::string>& stat_names();

// A [stats] part as levels, each with the values given there (-1 not given).
using StatRow = std::array<long, 6>;
std::map<int, StatRow> stats_of(const std::vector<std::pair<std::string, std::string>>& entries);
std::vector<std::pair<std::string, std::string>> stats_entries(const std::map<int, StatRow>& rows);

// The base's level table unpacked (KHDAYS_STATS_TABLE bytes and more); empty when it cannot be read (*why).
std::vector<std::uint8_t> base_level_table(const std::filesystem::path& nitrofs, const std::string& code,
                                           std::string* why);

// The table the port makes from `base` and `rows` (stats.c); empty when `base` is short.
std::vector<std::uint8_t> level_table(const std::vector<std::uint8_t>& base, const std::map<int, StatRow>& rows);
long level_value(const std::vector<std::uint8_t>& table, int level, int stat);

}  // namespace studio
