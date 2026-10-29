// The game's missions by name, read from the player's copy (the list Mission Mode and the story show,
// decompressed/UI/cm/msl_<language>.msi: docs/RUNTIME_MAP.md, "The missions' list"): for the tool's lists and
// choices. Nothing of the game's words is kept but in memory.
#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace studio {

struct GameMissionName {
    int file = 0;               // its mission file (mi/mi/<file>)
    int number = 0;             // the mission's number (what `unlock = cleared <number>` names)
    int shown = 0;              // the number its row shows
    int category = 0;           // the list's world tab
    bool mission_mode = false;  // its row is Mission Mode's (else the story's)
    std::string name;           // UTF-8
};

// Each mission file's row in `language`'s list (en fr de it es) -- Mission Mode's when it has one, else its first --
// by its file's number; empty when the list cannot be read.
std::map<int, GameMissionName> game_mission_names(const std::filesystem::path& nitrofs, const std::string& language);

}  // namespace studio
