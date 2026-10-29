// A mod's new missions -- missions/missions.txt and each missions/<key>/ -- read by the port's own reader
// (port/runtime/missions/registry.c): made from a game mission, their settings changed line by line, checked.
// docs/STUDIO.md, "New missions".
#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace studio {

// A setting of a mission's block: its name and whether it takes a language (name, objective, description).
struct MissionSetting {
    const char* name;
    bool language;
    const char* help;
};
const std::vector<MissionSetting>& mission_settings();

// The game's five languages as the files name them: en fr de it es.
const std::vector<std::string>& mission_languages();

// What a mission's block says, as the port reads it: its texts by language (empty: the English one) and the
// rest as written; empty when the mod has no such mission (or it does not read: *why).
struct ModMission {
    std::string key;
    std::vector<std::string> name, objective, description;  // by mission_languages()
    int category = -1, room = 0, level = 0, time_ms = 0, template_file = 0;
    std::string world, unlock;
    bool own_file = false;  // missions/<key>/mission.p2, else its template's file as it is
    std::string start;      // where the players start, as written ("x y z facing"); empty: the file's own
    // its waves, chests and emblems as written, in the file's order: ("wave 2", "0x2 at 0 0 1.5"), ...
    std::vector<std::pair<std::string, std::string>> lines;
};
std::vector<ModMission> read_mod_missions(const std::filesystem::path& mod_dir, std::vector<std::string>* problems);

// A new mission `key` in the mod, made from game mission `template_file` (its file copied as
// missions/<key>/mission.p2), played in `world`'s room `room`: its block written into missions.txt with the
// key as its name and objective, both read back before it reports. False with the reason.
bool new_mod_mission(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir, const std::string& key,
                     int template_file, const std::string& world, int room, std::string* why);

// Setting `name` (`language` for a text, else empty) of mission `key` made `value` (texts: a line break as
// written); the file read back by the port's reader and left as it was when the mission would not read.
bool set_mod_mission(const std::filesystem::path& mod_dir, const std::string& key, const std::string& name,
                     const std::string& language, const std::string& value, std::string* why);

// A line of a setting a mission may have many of -- `wave <n>`, `chest`, `emblem` -- added to mission `key`'s
// block, or every line of it taken away (`wave` takes every wave's, `wave 2` the second's: *removed how many);
// each written when the mission still reads as the port reads it.
bool add_mod_mission_line(const std::filesystem::path& mod_dir, const std::string& key, const std::string& setting,
                          const std::string& value, std::string* why);
bool clear_mod_mission_lines(const std::filesystem::path& mod_dir, const std::string& key, const std::string& setting,
                             int* removed, std::string* why);

// Whether mission `key`, when it has waves (*has_waves), builds as the port builds it at its start
// (mission_build.h); false with why. Its places over no floor of its room (its start, spawners, chests,
// emblems) are said in *off_floor, one line each.
bool check_mod_mission_build(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir,
                             const std::string& key, bool* has_waves, std::vector<std::string>* off_floor,
                             std::string* why);

// A place of a mission's, as its block writes it: where the players start, a spawner of a wave with its enemies,
// a chest, an emblem. Places in the game's units (1 = 4096 in 20.12).
struct MissionSpot {
    enum class Kind { start, spawner, chest, emblem };
    Kind kind = Kind::spawner;
    std::array<double, 3> at{};
    int heading = 0;                           // the start's facing, a chest's heading (degrees)
    int wave = 1;                              // a spawner's (1-255)
    std::vector<std::pair<int, int>> enemies;  // a spawner's: (class, count)
    int contents = 0;                          // a chest's: an item of the game's
    int emblem = 0;                            // an emblem's: which of the mission's three badges (0-2)
};

// Mission `key`'s places as the port reads them (its start first, when it has one); empty when it does not read
// (*why).
std::vector<MissionSpot> mod_mission_spots(const std::filesystem::path& mod_dir, const std::string& key,
                                           std::string* why);

// A place as its block's line: its setting (start, wave <n>, chest, emblem) and value.
std::pair<std::string, std::string> mission_spot_line(const MissionSpot& s);

// Mission `key`'s start, waves, chests and emblems made `spots` at once: its block's lines of those replaced, in the
// place of the first of them (else at its end), written when the mission reads as the port reads it and builds as
// the port builds it; else left as it was. False with the reason.
bool set_mod_mission_spots(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir,
                           const std::string& key, const std::vector<MissionSpot>& spots, std::string* why);

// Every mission of the mod checked as the port takes them: the descriptions, each one's file (its own or its
// template's), its world and room. One line each, problems first said "problem:". True with none.
bool check_mod_missions(const std::filesystem::path& nitrofs, const std::filesystem::path& mod_dir,
                        std::vector<std::string>* lines);

}  // namespace studio
