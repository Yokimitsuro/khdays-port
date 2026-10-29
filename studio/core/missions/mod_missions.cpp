#include "core/missions/mod_missions.h"

#include "core/files/file_write.h"
#include "core/files/paths.h"
#include "core/missions/mission_build.h"
#include "core/missions/missions.h"
#include "core/rooms/room_collision.h"
#include "core/rooms/worlds.h"

extern "C" {
#include "missions/registry.h"
#include "missions/tables.h"
#include "missions/worlds.h"
}

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>

namespace studio {

namespace {

namespace fs = std::filesystem;

bool fail_with(std::string* why, const std::string& reason) {
    if (why != nullptr)
        *why = reason;
    return false;
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> read_bytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path missions_txt(const fs::path& mod_dir) {
    return mod_dir / "missions" / "missions.txt";
}

std::string mission_file_path(int file) {
    char path[32];
    std::snprintf(path, sizeof(path), "mi/mi/%04d", file);
    return path;
}

// The reports of the port's reader, as lines.
struct Reports {
    std::vector<std::string> lines;
};

void reported(const char* mod, int line, const char* why, void* context) {
    (void)mod;
    auto* r = static_cast<Reports*>(context);
    r->lines.push_back(line > 0 ? "missions.txt:" + std::to_string(line) + ": " + why : why);
}

// The mod's descriptions read by the port's reader; the reports into `reports`.
void read_registry(const std::string& text, KhdaysMissions* r, Reports* reports) {
    khdays_missions_read(r, "mod", "mod", text.data(), text.size(), reported, reports);
}

bool has_key(const KhdaysMissions& r, const std::string& key) {
    for (int i = 0; i < r.count; ++i)
        if (key == r.list[i].key)
            return true;
    return false;
}

bool valid_key(const std::string& key) {
    if (key.empty())
        return false;
    for (const char c : key)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
            return false;
    return true;
}

// Writes missions.txt as `text`, through a temporary file the port's reader reads first: mission `key` must
// be there.
bool write_missions_txt(const fs::path& mod_dir, const std::string& text, const std::string& key, std::string* why) {
    const WriteResult r = write_into_mod(
        mod_dir, missions_txt(mod_dir),
        [&](const fs::path& temporary) -> std::string {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return out ? std::string() : "cannot write " + u8(temporary);
        },
        [&](const fs::path& temporary) -> std::string {
            KhdaysMissions back{};
            Reports reports;
            read_registry(read_text(temporary), &back, &reports);
            const bool there = has_key(back, key);
            khdays_missions_free(&back);
            if (!there)
                return "mission " + key + " would not read: " + (reports.lines.empty() ? "?" : reports.lines.back());
            return {};
        });
    if (!r.ok)
        return fail_with(why, r.locked ? "missions/missions.txt is in use" : "missions/missions.txt: " + r.why);
    return true;
}

// The lines of a text (without their ends).
std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
        at = end + 1;
    }
    return lines;
}

std::string trimmed(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

// A text as the file writes it: line breaks as \n, a backslash doubled.
std::string escaped(const std::string& value) {
    std::string out;
    for (const char c : value) {
        if (c == '\n')
            out += "\\n";
        else if (c == '\\')
            out += "\\\\";
        else if (c != '\r')
            out += c;
    }
    return out;
}

// The lines of mission `key`'s block: its header's at *start, the next block's (or the end) at *end.
bool block_of(const std::vector<std::string>& lines, const std::string& key, std::size_t* start, std::size_t* end) {
    const std::string header = "[mission " + key + "]";
    *start = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (trimmed(lines[i]) == header)
            *start = i;
    if (*start == lines.size())
        return false;
    *end = *start + 1;
    while (*end < lines.size() && trimmed(lines[*end]).rfind('[', 0) != 0)
        ++*end;
    return true;
}

// A line's setting as written before its '=' (`name  en` as `name en`), empty for a comment or a blank line.
std::string setting_of(const std::string& line) {
    const std::size_t equals = line.find('=');
    const std::string t = trimmed(line);
    if (equals == std::string::npos || t.empty() || t[0] == '#')
        return {};
    std::string setting = trimmed(line.substr(0, equals));
    const std::size_t space = setting.find_first_of(" \t");
    if (space != std::string::npos)
        setting = setting.substr(0, space) + " " + trimmed(setting.substr(space));
    return setting;
}

// Where a new line of a block goes: after its last line that is not blank.
std::size_t block_append_at(const std::vector<std::string>& lines, std::size_t start, std::size_t end) {
    std::size_t at = end;
    while (at > start + 1 && trimmed(lines[at - 1]).empty())
        --at;
    return at;
}

// missions.txt as `lines`, when mission `key` still reads as the port reads it; else nothing written.
bool write_checked(const fs::path& mod_dir, const std::vector<std::string>& lines, const std::string& key,
                   const std::string& setting, std::string* why) {
    std::string out;
    for (const std::string& l : lines)
        out += l + "\n";
    KhdaysMissions back{};
    Reports reports;
    read_registry(out, &back, &reports);
    const bool there = has_key(back, key);
    khdays_missions_free(&back);
    if (!there)
        return fail_with(why, setting + ": " + (reports.lines.empty() ? "not a valid value" : reports.lines.back()));
    return write_missions_txt(mod_dir, out, key, why);
}

}  // namespace

const std::vector<MissionSetting>& mission_settings() {
    static const std::vector<MissionSetting> settings = {
        {"name", true, "its name in the list"},
        {"objective", true, "what to do, shown in the list's info"},
        {"description", true, "a longer briefing (default: the objective)"},
        {"category", false, "the list's category, 0-11 (default: its world's)"},
        {"world", false, "the world it is played in (tt, aw, he, al, eh, nm, zz, pp, bb, tw)"},
        {"room", false, "its first room"},
        {"start", false, "where the players start: x y z facing (degrees)"},
        {"level", false, "the enemies' level base, 1-120 (default: its template's)"},
        {"time", false, "the time limit the list shows, in milliseconds"},
        {"unlock", false, "start, cleared <number> or cleared <mod id>/<key>"},
        {"template", false, "the game mission it was made from"},
    };
    return settings;
}

const std::vector<std::string>& mission_languages() {
    static const std::vector<std::string> languages = [] {
        std::vector<std::string> l;
        for (int i = 0; i < KHDAYS_MISSION_LANGUAGES; ++i)
            l.push_back(khdays_mission_language(i));
        return l;
    }();
    return languages;
}

std::vector<ModMission> read_mod_missions(const fs::path& mod_dir, std::vector<std::string>* problems) {
    std::vector<ModMission> out;
    std::error_code ec;
    if (!fs::is_regular_file(missions_txt(mod_dir), ec))
        return out;
    KhdaysMissions r{};
    Reports reports;
    const std::string text = read_text(missions_txt(mod_dir));
    const std::vector<std::string> file_lines = lines_of(text);
    read_registry(text, &r, &reports);
    for (int i = 0; i < r.count; ++i) {
        const KhdaysMission& m = r.list[i];
        ModMission mm;
        mm.key = m.key;
        for (int l = 0; l < KHDAYS_MISSION_LANGUAGES; ++l) {
            mm.name.push_back(m.name[l] != nullptr ? m.name[l] : "");
            mm.objective.push_back(m.objective[l] != nullptr ? m.objective[l] : "");
            mm.description.push_back(m.description[l] != nullptr ? m.description[l] : "");
        }
        mm.category = m.category;
        mm.world = m.world;
        mm.room = m.room;
        mm.level = m.level;
        mm.time_ms = m.time_ms;
        mm.template_file = m.template_file;
        mm.unlock = m.unlock_after != nullptr ? std::string("cleared ") + m.unlock_after
                    : m.unlock_number != 0    ? "cleared " + std::to_string(m.unlock_number)
                                              : "start";
        mm.own_file = fs::is_regular_file(mod_dir / "missions" / path_u8(mm.key) / "mission.p2", ec);
        std::size_t start = 0, end = 0;
        if (block_of(file_lines, mm.key, &start, &end)) {
            for (std::size_t l = start + 1; l < end; ++l) {
                const std::string setting = setting_of(file_lines[l]);
                const auto eq = file_lines[l].find('=');
                if (eq == std::string::npos)
                    continue;
                std::string value = file_lines[l].substr(eq + 1);
                const auto hash = value.find('#');
                value = trimmed(hash == std::string::npos ? value : value.substr(0, hash));
                if (setting == "start")
                    mm.start = value;
                else if (setting.rfind("wave ", 0) == 0 || setting == "chest" || setting == "emblem")
                    mm.lines.emplace_back(setting, value);
            }
        }
        out.push_back(std::move(mm));
    }
    khdays_missions_free(&r);
    if (problems != nullptr)
        *problems = reports.lines;
    return out;
}

bool new_mod_mission(const fs::path& nitrofs, const fs::path& mod_dir, const std::string& key, int template_file,
                     const std::string& world, int room, std::string* why) {
    if (!valid_key(key))
        return fail_with(why, "a key is letters, digits, '_' and '-'");
    if (khdays_world_index(world.c_str()) < 0)
        return fail_with(why, "no world " + world + " (tt, aw, he, al, eh, nm, zz, pp, bb, tw)");
    World w;
    std::string world_why;
    if (!load_world(nitrofs, world, &w, &world_why))
        return fail_with(why, "world " + world + ": " + world_why);
    if (room < 0 || room >= static_cast<int>(w.rooms.size()))
        return fail_with(why, "world " + world + " has rooms 0-" + std::to_string(w.rooms.size() - 1));
    const fs::path game_file = nitrofs / path_u8(mission_file_path(template_file));
    std::error_code ec;
    if (template_file <= 0 || !fs::is_regular_file(game_file, ec))
        return fail_with(why, "no game mission " + mission_file_path(template_file));
    std::string text = read_text(missions_txt(mod_dir));
    {
        KhdaysMissions r{};
        Reports reports;
        read_registry(text, &r, &reports);
        const bool taken = has_key(r, key);
        khdays_missions_free(&r);
        if (taken)
            return fail_with(why, "the mod has a mission " + key + " already");
    }
    // its file first: a block whose file is missing would be left out by the port
    const fs::path own = mod_dir / "missions" / path_u8(key) / "mission.p2";
    const std::vector<std::uint8_t> game = read_bytes(game_file);
    const WriteResult copied = write_into_mod(
        mod_dir, own,
        [&](const fs::path& temporary) {
            std::string e = copy_bytes(game_file, temporary);
            return e;
        },
        [&](const fs::path& temporary) {
            return read_bytes(temporary) == game ? std::string() : "the copy differs from the game's file";
        });
    if (!copied.ok)
        return fail_with(why, "missions/" + key + "/mission.p2: " + copied.why);
    if (!text.empty() && text.back() != '\n')
        text += '\n';
    if (text.empty())
        text = "# The mod's new missions (khdays-studio; docs/STUDIO.md, \"New missions\").\n";
    char block[512];
    std::snprintf(block, sizeof(block),
                  "\n[mission %s]\nname en = %s\nobjective en = %s\nworld = %s\nroom = %d\ntemplate = %04d\n",
                  key.c_str(), key.c_str(), key.c_str(), world.c_str(), room, template_file);
    text += block;
    return write_missions_txt(mod_dir, text, key, why);
}

bool set_mod_mission(const fs::path& mod_dir, const std::string& key, const std::string& name,
                     const std::string& language, const std::string& value, std::string* why) {
    bool known = false, takes_language = false;
    for (const MissionSetting& s : mission_settings()) {
        if (name == s.name) {
            known = true;
            takes_language = s.language;
        }
    }
    if (!known)
        return fail_with(why, "no setting " + name);
    if (takes_language != !language.empty())
        return fail_with(why, takes_language ? name + " takes a language (en, fr, de, it, es)"
                                             : name + " takes no language");
    if (!takes_language && value.find('#') != std::string::npos)
        return fail_with(why, "a '#' starts a comment there");
    std::vector<std::string> lines = lines_of(read_text(missions_txt(mod_dir)));
    std::size_t start, end;
    if (!block_of(lines, key, &start, &end))
        return fail_with(why, "the mod has no mission " + key);
    const std::string wanted = takes_language ? name + " " + language : name;
    const std::string line = wanted + " = " + (takes_language ? escaped(value) : trimmed(value));
    bool replaced = false;
    for (std::size_t i = start + 1; i < end && !replaced; ++i) {
        if (setting_of(lines[i]) == wanted) {
            lines[i] = line;
            replaced = true;
        }
    }
    if (!replaced)
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(block_append_at(lines, start, end)), line);
    return write_checked(mod_dir, lines, key, name, why);
}

bool add_mod_mission_line(const fs::path& mod_dir, const std::string& key, const std::string& setting,
                          const std::string& value, std::string* why) {
    if (value.find('#') != std::string::npos)
        return fail_with(why, "a '#' starts a comment there");
    std::vector<std::string> lines = lines_of(read_text(missions_txt(mod_dir)));
    std::size_t start, end;
    if (!block_of(lines, key, &start, &end))
        return fail_with(why, "the mod has no mission " + key);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(block_append_at(lines, start, end)),
                 setting + " = " + trimmed(value));
    return write_checked(mod_dir, lines, key, setting, why);
}

bool clear_mod_mission_lines(const fs::path& mod_dir, const std::string& key, const std::string& setting, int* removed,
                             std::string* why) {
    std::vector<std::string> lines = lines_of(read_text(missions_txt(mod_dir)));
    std::size_t start, end;
    if (!block_of(lines, key, &start, &end))
        return fail_with(why, "the mod has no mission " + key);
    *removed = 0;
    for (std::size_t i = end; i-- > start + 1;) {
        const std::string s = setting_of(lines[i]);
        // `wave` clears every wave, `wave 2` that one
        if (s == setting || (setting.find(' ') == std::string::npos && s.rfind(setting + " ", 0) == 0)) {
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
            ++*removed;
        }
    }
    return write_checked(mod_dir, lines, key, setting, why);
}

namespace {

// A number as a line writes it: up to three decimals, no trailing zeros.
std::string number_text(double v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", v);
    std::string s = text;
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    return s == "-0" ? "0" : s;
}

std::string at_text(const std::array<double, 3>& at) {
    return number_text(at[0]) + " " + number_text(at[1]) + " " + number_text(at[2]);
}

}  // namespace

std::vector<MissionSpot> mod_mission_spots(const fs::path& mod_dir, const std::string& key, std::string* why) {
    std::vector<MissionSpot> out;
    KhdaysMissions r{};
    Reports reports;
    read_registry(read_text(missions_txt(mod_dir)), &r, &reports);
    const KhdaysMission* m = nullptr;
    for (int i = 0; i < r.count; ++i)
        if (key == r.list[i].key)
            m = &r.list[i];
    if (m == nullptr) {
        fail_with(why, "the mod has no mission " + key + " that reads");
    } else {
        if (m->has_start) {
            MissionSpot s;
            s.kind = MissionSpot::Kind::start;
            s.at = {m->start[0], m->start[1], m->start[2]};
            s.heading = m->facing;
            out.push_back(s);
        }
        for (int i = 0; i < m->spawner_count; ++i) {
            MissionSpot s;
            s.kind = MissionSpot::Kind::spawner;
            s.at = {m->spawners[i].at[0], m->spawners[i].at[1], m->spawners[i].at[2]};
            s.wave = m->spawners[i].wave;
            for (int e = 0; e < m->spawners[i].enemy_count; ++e)
                s.enemies.emplace_back(m->spawners[i].enemies[e].enemy_class, m->spawners[i].enemies[e].count);
            out.push_back(s);
        }
        for (int i = 0; i < m->chest_count; ++i) {
            MissionSpot s;
            s.kind = MissionSpot::Kind::chest;
            s.at = {m->chests[i].at[0], m->chests[i].at[1], m->chests[i].at[2]};
            s.contents = m->chests[i].item;
            s.heading = m->chests[i].heading;
            out.push_back(s);
        }
        for (int i = 0; i < m->emblem_count; ++i) {
            MissionSpot s;
            s.kind = MissionSpot::Kind::emblem;
            s.at = {m->emblems[i].at[0], m->emblems[i].at[1], m->emblems[i].at[2]};
            s.emblem = m->emblems[i].kind;
            out.push_back(s);
        }
    }
    khdays_missions_free(&r);
    return out;
}

std::pair<std::string, std::string> mission_spot_line(const MissionSpot& s) {
    switch (s.kind) {
    case MissionSpot::Kind::start: return {"start", at_text(s.at) + " " + std::to_string(s.heading)};
    case MissionSpot::Kind::spawner: {
        std::string enemies;
        for (const auto& [enemy_class, count] : s.enemies)
            enemies += std::to_string(enemy_class) + "x" + std::to_string(count) + " ";
        return {"wave " + std::to_string(s.wave), enemies + "at " + at_text(s.at)};
    }
    case MissionSpot::Kind::chest:
        return {"chest", std::to_string(s.contents) + " at " + at_text(s.at) + " " + std::to_string(s.heading)};
    case MissionSpot::Kind::emblem: return {"emblem", std::to_string(s.emblem) + " at " + at_text(s.at)};
    }
    return {};
}

bool set_mod_mission_spots(const fs::path& nitrofs, const fs::path& mod_dir, const std::string& key,
                           const std::vector<MissionSpot>& spots, std::string* why) {
    const std::string before = read_text(missions_txt(mod_dir));
    std::vector<std::string> lines = lines_of(before);
    std::size_t start, end;
    if (!block_of(lines, key, &start, &end))
        return fail_with(why, "the mod has no mission " + key);
    // the block's places taken out, the new ones where the first was
    std::size_t at = 0;
    for (std::size_t i = end; i-- > start + 1;) {
        const std::string s = setting_of(lines[i]);
        if (s == "start" || s == "chest" || s == "emblem" || s.rfind("wave ", 0) == 0) {
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
            at = i;
            --end;
        }
    }
    if (at == 0)
        at = block_append_at(lines, start, end);
    std::vector<std::string> added;
    for (const MissionSpot& s : spots) {
        const auto [setting, value] = mission_spot_line(s);
        added.push_back(setting + " = " + value);
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), added.begin(), added.end());
    if (!write_checked(mod_dir, lines, key, "its places", why))
        return false;
    // built as the port builds it, else put back as it was
    bool has_waves = false;
    std::string built_why;
    if (!check_mod_mission_build(nitrofs, mod_dir, key, &has_waves, nullptr, &built_why)) {
        std::vector<std::string> back = lines_of(before);
        std::string ignored;
        write_checked(mod_dir, back, key, "its places", &ignored);
        return fail_with(why, "it would not build: " + built_why);
    }
    return true;
}

namespace {

// The places of mission `m` over no floor of its room, said: its start, its spawners, chests and emblems.
std::vector<std::string> off_the_floor(const fs::path& nitrofs, const KhdaysMission& m) {
    std::vector<std::string> out;
    World w;
    std::string why;
    if (!load_world(nitrofs, m.world, &w, &why) || m.room < 0 || m.room >= static_cast<int>(w.rooms.size()))
        return out;
    const auto& data =
        w.archive.entries[static_cast<std::size_t>(w.rooms[static_cast<std::size_t>(m.room)].collision_member)].data;
    RoomCollision c;
    if (data.empty() || !parse_collision(data, &c, nullptr))
        return out;
    auto check = [&](const std::string& what, const double* at) {
        double y = 0;
        if (ground_under(c, at[0] * 4096.0, at[2] * 4096.0, (at[1] + 4.0) * 4096.0, &y))
            return;
        char where[96];
        std::snprintf(where, sizeof(where), " at (%.3f, %.3f, %.3f)", at[0], at[1], at[2]);
        out.push_back(what + where + " is over no floor of " + m.world + " room " + std::to_string(m.room));
    };
    if (m.has_start)
        check("the start", m.start);
    for (int i = 0; i < m.spawner_count; ++i)
        check("a spawner of wave " + std::to_string(m.spawners[i].wave), m.spawners[i].at);
    for (int i = 0; i < m.chest_count; ++i)
        check("chest " + std::to_string(i + 1), m.chests[i].at);
    for (int i = 0; i < m.emblem_count; ++i)
        check("emblem " + std::to_string(i + 1), m.emblems[i].at);
    return out;
}

}  // namespace

bool check_mod_mission_build(const fs::path& nitrofs, const fs::path& mod_dir, const std::string& key, bool* has_waves,
                             std::vector<std::string>* off_floor, std::string* why) {
    *has_waves = false;
    KhdaysMissions r{};
    Reports reports;
    read_registry(read_text(missions_txt(mod_dir)), &r, &reports);
    const KhdaysMission* m = nullptr;
    for (int i = 0; i < r.count; ++i)
        if (key == r.list[i].key)
            m = &r.list[i];
    bool ok = true;
    if (m != nullptr && off_floor != nullptr)
        *off_floor = off_the_floor(nitrofs, *m);
    if (m == nullptr) {
        ok = fail_with(why, "the mod has no mission " + key + " that reads");
    } else if (m->spawner_count > 0) {
        *has_waves = true;
        MissionPlan plan;
        std::vector<std::uint8_t> bytes;
        ok = plan_of(*m, &plan, why) && build_mission(extracted_game_files(nitrofs), plan, &bytes, why);
    }
    khdays_missions_free(&r);
    return ok;
}

bool check_mod_missions(const fs::path& nitrofs, const fs::path& mod_dir, std::vector<std::string>* lines) {
    std::vector<std::string> problems;
    const std::vector<ModMission> missions = read_mod_missions(mod_dir, &problems);
    bool ok = true;
    for (const std::string& p : problems) {
        lines->push_back("problem: " + p);
        ok = false;
    }
    for (const ModMission& m : missions) {
        std::string about = m.key + ": " + m.world + " room " + std::to_string(m.room);
        std::error_code ec;
        World w;
        std::string world_why;
        if (!load_world(nitrofs, m.world, &w, &world_why)) {
            lines->push_back("problem: " + m.key + ": world " + m.world + ": " + world_why);
            ok = false;
        } else if (m.room >= static_cast<int>(w.rooms.size())) {
            lines->push_back("problem: " + m.key + ": world " + m.world + " has rooms 0-" +
                             std::to_string(w.rooms.size() - 1));
            ok = false;
        }
        if (m.own_file) {
            about += ", its own mission file";
        } else if (m.template_file != 0 &&
                   fs::is_regular_file(nitrofs / path_u8(mission_file_path(m.template_file)), ec)) {
            about += ", " + mission_file_path(m.template_file) + "'s file as it is";
        } else {
            lines->push_back("problem: " + m.key + ": no missions/" + m.key + "/mission.p2 and no template's");
            ok = false;
        }
        lines->push_back(about);
    }
    // the rows the port adds to the list, in each language, against the game's own list
    KhdaysMissions r{};
    Reports reports;
    KhdaysFieldStore store{};
    read_registry(read_text(missions_txt(mod_dir)), &r, &reports);
    khdays_missions_number(&r, &store, nullptr, nullptr);
    for (int l = 0; l < KHDAYS_MISSION_LANGUAGES && r.count > 0; ++l) {
        const fs::path list = nitrofs.parent_path() / "decompressed" / "UI" / "cm" /
                              ("msl_" + mission_languages()[static_cast<std::size_t>(l)] + ".msi");
        std::vector<std::uint8_t> game = read_bytes(list);
        std::size_t size = 0;
        unsigned char* made = game.empty() ? nullptr : khdays_tables_list(game.data(), game.size(), &r, l, &size);
        if (!game.empty() && made == nullptr) {
            lines->push_back("problem: the list in " + mission_languages()[static_cast<std::size_t>(l)] +
                             " could not take the rows");
            ok = false;
        }
        std::free(made);
    }
    khdays_field_store_free(&store);
    khdays_missions_free(&r);
    lines->push_back(std::to_string(missions.size()) + " mission(s), " +
                     (ok ? std::string("no problem") : std::string("problems above")));
    return ok;
}

}  // namespace studio
