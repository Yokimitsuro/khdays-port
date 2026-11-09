// khdays-secondary-replay's stand-ins for the running game (replay_game_stubs.cpp), which port/runtime/mods/models.cpp
// reads through when the replay poses a model by the game's own animations (`--days`).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

// The game model models.cpp is asked for: its file and offset (as the game's origin table names them), the mod model's
// name (the mods' `graphics/<name>.mdlx`), the game model's BMD0 bytes, and the folder holding the mods.
void replay_game_set_model(const std::string &file, unsigned offset, const std::string &name,
                           std::vector<unsigned char> bmd0, const std::filesystem::path &mods);
// The game's VBlank count: one a draw.
void replay_game_tick();
