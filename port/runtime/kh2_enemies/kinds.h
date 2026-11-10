// The KINGDOM HEARTS II enemy kinds the player's mod holds (contracts: "Days - KH2 Enemies", kh2_enemies.txt): one a
// line, tab-separated -- name, title, covered (yes / no), how many calls its script makes, the missing ones.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace khdays::kh2_enemies {

struct Kind final {
    std::string name;    // its object's file name: M_EX010
    std::string title;   // shown to authors: Soldier
    bool covered = false;
    int calls = 0;
    std::string missing; // the calls not handled, `kind:number` apart by spaces; "-" none
    bool plays_player = false; // its script plays the player's motions itself (1:29): its zones' commands offered
};

std::vector<Kind> read_kinds(const std::string &text);
// A kind by name, case aside; null when the list has none.
const Kind *find_kind(const std::vector<Kind> &kinds, const std::string &name);
// The list in `folder` (kh2_enemies.txt), each kind kept only when its files are there (kh2/obj/<name>.mdlx, .mset);
// `why` told once for each one left out and for a missing list.
std::vector<Kind> load_kinds(const std::string &folder, const std::function<void(const std::string &)> &why);

} // namespace khdays::kh2_enemies
