// The KINGDOM HEARTS II enemy kinds the player's mod holds (contracts: "Days - KH2 Enemies", kh2_enemies.txt): one a
// line, tab-separated -- name, title, covered (yes / no), how many calls its script makes, the missing ones, "player"
// when its script plays the player's motions, its name's text in KH2's journal messages ("-" none), the objects KH2
// places with it ("name x y z yaw" each, apart by ";"; "-" none), the mission KH2's area runs with its battle (its msn
// file's name, kh2/msn/<name>.bar; "-" none), the walk paths of its battle ("number x y z x y z ..." each, apart by ";";
// "-" none).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace khdays::kh2_enemies {

// An object KH2 places with a boss as its battle begins (its area's spawn: the Hydra's Phil, Pegasus and Phil's urn),
// none an enemy: where, in KH2's cm from the boss, in its frame (+ z the way from it to the player), turned by yaw
struct Companion final {
    std::string name;
    float x = 0.0F, y = 0.0F, z = 0.0F, yaw = 0.0F;
};

// A walk path of a boss's battle (its area's spawn set's: Shenzi's, Banzai's and Ed's round the graveyard): its number
// and its points, in KH2's cm in the boss's own frame as a companion's place
struct WalkPath final {
    int number = 0;
    std::vector<float> points; // x, y, z each
};

struct Kind final {
    std::string name;    // its object's file name: M_EX010
    std::string title;   // shown to authors: Soldier
    bool covered = false;
    int calls = 0;
    std::string missing; // the calls not handled, `kind:number` apart by spaces; "-" none
    bool plays_player = false; // its script plays the player's motions itself (1:29): its zones' commands offered
    int journal = -1;          // its name's text in KH2's journal messages (kh2/msg/<language>/jm.bar); -1 none
    std::vector<Companion> with; // the objects KH2 places with it
    std::string mission;         // the mission KH2 runs with its battle (Demyx's HE17_MS105); "" none
    std::vector<WalkPath> paths;  // the walk paths of its battle (1:177, 5:14)
};

std::vector<Kind> read_kinds(const std::string &text);
// A kind by name, case aside; null when the list has none.
const Kind *find_kind(const std::vector<Kind> &kinds, const std::string &name);
// The list in `folder` (kh2_enemies.txt), each kind kept only when its object is there (kh2/obj/<name>.mdlx; its .mset
// when it has motions);
// `why` told once for each one left out and for a missing list.
std::vector<Kind> load_kinds(const std::string &folder, const std::function<void(const std::string &)> &why);

} // namespace khdays::kh2_enemies
