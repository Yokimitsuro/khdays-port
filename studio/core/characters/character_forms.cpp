// A character's forms and stats in its character.ini (character_forms.h).
#include "core/characters/character_forms.h"

#include "core/files/paths.h"

#include "khdays/assets/message.h"  // lz_decompress

extern "C" {
#include "characters/registry.h"
#include "characters/stats.h"
}

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace studio {

namespace {

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr)
        *why = reason;
    return false;
}

// A part's form id and section: "form dual actions" -> dual, actions; false for a part of no form.
bool form_of_part(const std::string& part, std::string* id, std::string* section) {
    if (part.rfind("form ", 0) != 0)
        return false;
    std::istringstream words(part.substr(5));
    if (!(words >> *id))
        return false;
    section->clear();
    words >> *section;
    return true;
}

// A form id is letters, digits, '_' and '-', not "default".
bool valid_form_id(const std::string& id) {
    return valid_character_key(id) && id != "default";
}

}  // namespace

std::string form_part(const std::string& id, const std::string& section) {
    if (id.empty())
        return section.empty() ? "character" : section;
    if (section.empty() || section == "model")
        return "form " + id;
    return "form " + id + " " + section;
}

std::vector<std::string> form_ids(const IniText& ini) {
    std::vector<std::string> out;
    for (const auto& part : ini.part_names()) {
        std::string id, section;
        if (form_of_part(part, &id, &section) && std::find(out.begin(), out.end(), id) == out.end())
            out.push_back(id);
    }
    return out;
}

std::vector<std::string> form_part_names(const IniText& ini, const std::string& wanted) {
    std::vector<std::string> out;
    for (const auto& part : ini.part_names()) {
        std::string id, section;
        if (form_of_part(part, &id, &section) && id == wanted)
            out.push_back(part);
    }
    return out;
}

const std::vector<std::string>& form_bases() {
    static const std::vector<std::string> bases = [] {
        std::vector<std::string> b;
        for (int i = 0; i < KHDAYS_CHARACTER_FOLDERS; ++i)
            b.emplace_back(khdays_character_base_name(i));
        return b;
    }();
    return bases;
}

std::string form_base_code(const std::string& base) {
    for (int i = 0; i < KHDAYS_CHARACTER_FOLDERS; ++i)
        if (base == khdays_character_base_name(i))
            return khdays_character_base_code(i);
    return {};
}

void raise_format(IniText* ini) {
    if (std::atoi(ini->get("character", "format").c_str()) < KHDAYS_CHARACTER_FORMAT)
        ini->set("character", "format", std::to_string(KHDAYS_CHARACTER_FORMAT));
}

bool add_form(IniText* ini, const std::string& id, std::string* why) {
    if (!valid_form_id(id))
        return fail_with(why, "a form's id is letters, digits, '_' and '-', not default");
    const auto ids = form_ids(*ini);
    if (std::find(ids.begin(), ids.end(), id) != ids.end())
        return fail_with(why, "the character has a form " + id + " already");
    ini->set(form_part(id, ""), "priority", "0");
    raise_format(ini);
    return true;
}

bool rename_form(IniText* ini, const std::string& from, const std::string& to, std::string* why) {
    if (from == to)
        return true;
    if (!valid_form_id(to))
        return fail_with(why, "a form's id is letters, digits, '_' and '-', not default");
    const auto ids = form_ids(*ini);
    if (std::find(ids.begin(), ids.end(), to) != ids.end())
        return fail_with(why, "the character has a form " + to + " already");
    for (const auto& part : form_part_names(*ini, from))
        ini->rename_part(part, "form " + to + part.substr(5 + from.size()));
    return true;
}

bool duplicate_form(IniText* ini, const std::string& from, const std::string& to, std::string* why) {
    if (!valid_form_id(to))
        return fail_with(why, "a form's id is letters, digits, '_' and '-', not default");
    const auto ids = form_ids(*ini);
    if (std::find(ids.begin(), ids.end(), to) != ids.end())
        return fail_with(why, "the character has a form " + to + " already");
    for (const auto& part : form_part_names(*ini, from))
        ini->copy_part(part, "form " + to + part.substr(5 + from.size()));
    return true;
}

void remove_form(IniText* ini, const std::string& id) {
    for (const auto& part : form_part_names(*ini, id))
        ini->remove_part(part);
}

void move_form(IniText* ini, const std::string& id, int step) {
    const auto ids = form_ids(*ini);
    const auto at = std::find(ids.begin(), ids.end(), id);
    if (at == ids.end())
        return;
    const auto i = static_cast<std::ptrdiff_t>(at - ids.begin());
    const auto j = i + (step < 0 ? -1 : 1);
    if (j < 0 || j >= static_cast<std::ptrdiff_t>(ids.size()))
        return;
    // the later form's parts put before the earlier one's first
    const std::string& earlier = ids[static_cast<std::size_t>(std::min(i, j))];
    const std::string& later = ids[static_cast<std::size_t>(std::max(i, j))];
    ini->move_parts(form_part_names(*ini, later), form_part_names(*ini, earlier).front());
}

const std::vector<std::string>& stat_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n(KHDAYS_STATS);
        n[KHDAYS_STAT_STRENGTH] = "strength";
        n[KHDAYS_STAT_MAGIC] = "magic";
        n[KHDAYS_STAT_DEFENCE] = "defence";
        n[KHDAYS_STAT_HP] = "hp";
        n[KHDAYS_STAT_UNKNOWN_8] = "unknown_8";
        n[KHDAYS_STAT_UNKNOWN_12] = "unknown_12";
        return n;
    }();
    return names;
}

std::map<int, StatRow> stats_of(const std::vector<std::pair<std::string, std::string>>& entries) {
    std::map<int, StatRow> rows;
    for (const auto& [level_text, value] : entries) {
        const int level = std::atoi(level_text.c_str());
        if (level < 1 || level > KHDAYS_CHARACTER_LEVELS)
            continue;  // told by the port's reader
        auto [row, made] = rows.try_emplace(level);
        if (made)
            row->second.fill(-1);
        std::istringstream words(value);
        std::string word;
        long v = 0;
        while (words >> word >> v) {
            if (word == "defense")
                word = "defence";
            const auto& names = stat_names();
            const auto s = std::find(names.begin(), names.end(), word);
            if (s != names.end())
                row->second[static_cast<std::size_t>(s - names.begin())] = v;
        }
    }
    return rows;
}

std::vector<std::pair<std::string, std::string>> stats_entries(const std::map<int, StatRow>& rows) {
    // the order a line is written in: hp first, as the game's menus show them
    static const int order[] = {KHDAYS_STAT_HP,     KHDAYS_STAT_STRENGTH,  KHDAYS_STAT_MAGIC,
                                KHDAYS_STAT_DEFENCE, KHDAYS_STAT_UNKNOWN_8, KHDAYS_STAT_UNKNOWN_12};
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [level, row] : rows) {
        std::string line;
        for (const int s : order) {
            if (row[static_cast<std::size_t>(s)] < 0)
                continue;
            line += (line.empty() ? "" : " ") + stat_names()[static_cast<std::size_t>(s)] + " " +
                    std::to_string(row[static_cast<std::size_t>(s)]);
        }
        if (!line.empty())
            out.emplace_back(std::to_string(level), line);
    }
    return out;
}

std::vector<std::uint8_t> base_level_table(const fs::path& nitrofs, const std::string& code, std::string* why) {
    const fs::path file = nitrofs / "ba" / "ch" / path_u8(code) / "lv.b.z";
    std::ifstream in(file, std::ios::binary);
    const std::vector<std::uint8_t> packed{std::istreambuf_iterator<char>(in), {}};
    std::vector<std::uint8_t> table = packed.empty() ? packed : khdays::assets::lz_decompress(packed);
    if (table.size() < KHDAYS_STATS_TABLE) {
        if (why != nullptr)
            *why = u8(file) + (packed.empty() ? ": cannot be read" : ": not a level table");
        return {};
    }
    return table;
}

std::vector<std::uint8_t> level_table(const std::vector<std::uint8_t>& base, const std::map<int, StatRow>& rows) {
    std::vector<KhdaysCharacterStatPoint> points;
    for (const auto& [level, row] : rows)
        for (int s = 0; s < KHDAYS_STATS; ++s)
            if (row[static_cast<std::size_t>(s)] >= 0)
                points.push_back({level, s, row[static_cast<std::size_t>(s)]});
    std::vector<std::uint8_t> out(KHDAYS_STATS_TABLE);
    if (!khdays_stats_table(points.data(), static_cast<int>(points.size()), base.data(), base.size(), out.data()))
        return {};
    return out;
}

long level_value(const std::vector<std::uint8_t>& table, int level, int stat) {
    return table.size() < KHDAYS_STATS_TABLE ? -1 : khdays_stats_value(table.data(), level, stat);
}

}  // namespace studio
