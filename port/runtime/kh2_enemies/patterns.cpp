// KINGDOM HEARTS II's damage patterns (patterns.h).
#include "patterns.h"

#include <cstring>

namespace khdays::kh2_enemies {

std::vector<DamagePattern> parse_damage_patterns(const std::uint8_t *section, std::size_t size)
{
    std::vector<DamagePattern> out;
    if (section == nullptr || size < 8)
        return out;
    std::uint32_t count = 0;
    std::memcpy(&count, section + 4, 4);
    for (std::size_t i = 0; i < count && 8 + 32 * (i + 1) <= size; ++i) {
        DamagePattern p;
        p.number = section[8 + 32 * i];
        std::memcpy(p.to.data(), section + 8 + 32 * i + 1, p.to.size());
        out.push_back(p);
    }
    return out;
}

int pattern_reaction(const std::vector<DamagePattern> &patterns, std::uint32_t on, bool air, int first)
{
    int reaction = first;
    if (on == 0 || first < 0 || first >= 19)
        return reaction;
    for (const DamagePattern &p : patterns) {
        if (p.number >= 32 || ((on >> p.number) & 1u) == 0 || p.number == 2 || (p.number == 7 && air) ||
            (p.number == 8 && !air))
            continue;
        if (p.to[std::size_t(first)] != 0xff)
            reaction = p.to[std::size_t(first)];
    }
    return reaction;
}

} // namespace khdays::kh2_enemies
