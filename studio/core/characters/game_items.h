// The game's items a form's rule names (docs/CHARACTERS.md, "Forms"): each one's number, its panel's deck category
// and shape (db/db.p2 entry 0x15: a count, then a 0x64-byte record an item from item 1) and its name in a language
// (db/db_<lang>.p2 entry 0x15: a name and a description an item, from item 1) -- read from the player's copy;
// nothing of the game's words is kept but in memory.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace studio {

struct GameItem {
    int id = 0;
    int category = 0;      // the deck's: 4 gears and their links, 5 abilities ... (RUNTIME_MAP section 26)
    bool one_cell = false;  // a link or a one-cell panel (shape -1)
    std::string name;       // UTF-8, the game's marks kept (a link's Ｌ)
};

// Every item with a panel record, by number; empty when the files cannot be read (*why).
std::vector<GameItem> game_items(const std::filesystem::path& nitrofs, const std::string& language, std::string* why);

// The gears (category 4, more than one cell), their links (category 4, one cell) and every panel (any category but
// the synthesis materials, 2), from `items`.
std::vector<GameItem> game_gears(const std::vector<GameItem>& items);
std::vector<GameItem> game_links(const std::vector<GameItem>& items);
std::vector<GameItem> game_panels(const std::vector<GameItem>& items);

}  // namespace studio
