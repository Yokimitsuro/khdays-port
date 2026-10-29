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
    int world = 0;  // the game's world index (tt 0 ... tw 10)
    int room = 0;
    std::array<double, 3> start{};
    int facing = 0;                                // degrees
    int time_ms = 0;                               // 0: no time limit
    std::array<std::string, 5> notice, objective;  // en fr de it es; empty: the English one
    std::vector<std::vector<WaveSpawner>> waves;   // the first at the start, each next once all are defeated
};

// The plan a mission's description gives: false (with why) when it has no wave or no start.
bool plan_of(const KhdaysMission& m, MissionPlan* out, std::string* why);

// The game's files as the builder reads them: a game path ("mi/mi/1354") to its bytes (empty: none), and the
// game's mission files' numbers. khdays-studio reads the extracted files; the port, its ROM.
struct GameFiles {
    std::function<std::vector<std::uint8_t>(const std::string& path)> read;
    std::vector<int> missions;
};
GameFiles extracted_game_files(const std::filesystem::path& nitrofs);

// The enemy classes the game's missions have a row of (the classes a wave can use).
std::vector<int> buildable_enemy_classes(const GameFiles& game);

// The mission file for `plan`, read back by the mission reader before it is handed out. False with why (a class
// no game mission has, more than four classes, no wave...).
bool build_mission(const GameFiles& game, const MissionPlan& plan, std::vector<std::uint8_t>* out, std::string* why);

}  // namespace studio
