// KINGDOM HEARTS II's damage patterns (00battle.bin's patn; docs/KH2_ENEMIES.md "Damage patterns"): before a hit
// reaches an enemy's script KH2 changes its reaction by the patterns the enemy has on (YS::PATTERN::Convert, the PS3
// build's 0x47a65c, from YS::PIERROT::_OVR_exec_damage): Roxas's desperation move takes no flinch so.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace khdays::kh2_enemies {

// One pattern (YS::PATTERN, 32 bytes): its number (the enemy's bit for it) and, for each of the 19 reactions a hit
// may have (YS::DAMAGE's REACTION_*), the one it makes of it, 0xff none
struct DamagePattern final {
    int number = 0;
    std::array<std::uint8_t, 19> to{};
};

// The patterns of a patn section (its bytes: a version, a u32 count at + 4, then the records); none when too short
std::vector<DamagePattern> parse_damage_patterns(const std::uint8_t *section, std::size_t size);

// A hit's reaction once the patterns `on` (bit n: pattern number n) have changed its first one: each on, in the
// table's order, maps `first` to its own when it has one, the last such winning; pattern 7 only on the ground, 8 only
// in the air (`air`), 2 only when the attacker is not the player (`not_player`: YS::PATTERN::Convert asks the attacker's
// + 0x588 bit 0x4000, which the player's has and a party member's -- Riku's in Final Xemnas's battle -- has not). `first`
// itself when none does.
int pattern_reaction(const std::vector<DamagePattern> &patterns, std::uint32_t on, bool air, int first,
                     bool not_player = false);

} // namespace khdays::kh2_enemies
