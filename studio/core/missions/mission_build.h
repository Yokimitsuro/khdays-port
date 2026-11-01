// A new mission's file built from a plan -- where it is played, where the players start, its time limit, its
// notice and objective, its waves of enemies -- out of the game's own steps: each step is a copy of one the game's
// missions use, its operands set (docs/STUDIO.md, "New missions"; docs/RUNTIME_MAP.md 18, "How a Mission Mode
// mission flows"). The base is mission 1354's file: its area 99 and its ending scripts as they are, its start and
// its area made again without the pieces of its own arena.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct KhdaysMission;  // port/runtime/missions/registry.h: a mission as missions.txt describes it

namespace studio {

// A spawner of a wave: its enemies (an enemy class of the game and how many) and where they come out.
struct WaveSpawner {
    struct Enemies {
        int enemy_class = 0;
        int count = 1;
    };
    std::vector<Enemies> enemies;
    std::array<double, 3> at{};
};

struct MissionPlan {
    int kind = 0;   // KhdaysMissionKind (port/runtime/missions/registry.h)
    int world = 0;  // the game's world index (tt 0 ... tw 10)
    int room = 0;
    std::array<double, 3> start{};
    int facing = 0;                                // degrees
    int time_ms = 0;                               // 0: no time limit
    std::array<std::string, 5> notice, objective;  // en fr de it es; empty: the English one
    std::vector<std::vector<WaveSpawner>> waves;   // the first at the start, each next once all are defeated
    int target = -1;                               // eliminate: the enemy class to defeat
    int goal = 0;  // eliminate: how many of the target class; hearts: how many hearts; emblems: points (0: all)
    std::vector<std::array<double, 3>> collects;  // emblems: its Organization emblems
    bool has_reach = false;                       // reach: the place a player goes to, within its radius
    std::array<double, 3> reach{};
    double reach_radius = 0;
};

// The plan a mission's description gives: false (with why) when the port does not build it (no wave, and no
// emblems or place of its kind's) or it has no start.
bool plan_of(const KhdaysMission& m, MissionPlan* out, std::string* why);

// The game's files as the builder reads them: a game path ("mi/mi/1354") to its bytes (empty: none), the game's
// mission files' numbers, and its ARM9 overlay table (an overlay's 32 bytes, its RAM size at +8: how many enemy
// code slots an enemy's overlay takes). khdays-studio reads the extracted files; the port, its ROM.
struct GameFiles {
    std::function<std::vector<std::uint8_t>(const std::string& path)> read;
    std::vector<int> missions;
    std::vector<std::uint8_t> overlay_table;
};
GameFiles extracted_game_files(const std::filesystem::path& nitrofs);

// The enemy classes the game's missions have a row of (the classes a wave can use).
std::vector<int> buildable_enemy_classes(const GameFiles& game);

// How many hearts a defeated enemy gives, by its class (Ms/DP.p2f, the class's record: Ov107_LoadSpawnRecord's
// byte 0x6d); empty when the file does not read. Shadows and Possessors give none.
std::vector<int> enemy_hearts(const GameFiles& game);

// How many hearts every enemy of `waves` gives together, `hearts` by class (enemy_hearts).
int waves_hearts(const std::vector<std::vector<WaveSpawner>>& waves, const std::vector<int>& hearts);

// The mission file for `plan`, read back by the mission reader before it is handed out. Its enemy classes in the
// order their code finds room in the game's enemy slots (load_order). False with why (a class no game mission has,
// more than four classes, classes whose code does not fit together, no wave...).
bool build_mission(const GameFiles& game, const MissionPlan& plan, std::vector<std::uint8_t>* out, std::string* why);

// An order the enemy classes' code is loaded in that finds each its slots, the given order when it does (indices
// into `classes`); empty when none does. Ov107_SpawnEntityClass loads a class's overlay at the first free column
// it has one in, the overlay taking its RAM size / 0x1e00 + 1 of the six (Ov107_LoadEnemyOverlay); a class
// without a column left stops the game (OS_Terminate).
std::vector<int> load_order(const std::vector<int>& classes, const std::vector<std::uint8_t>& overlay_table);

}  // namespace studio
