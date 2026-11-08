// The calls KINGDOM HEARTS II's enemy scripts make, as the port answers them (docs/KH2_ENEMIES.md, "The Soldier's
// calls"). Their shapes (how many arguments, whether a value comes back) are KH2's own, read from the player's
// executable into the mod (`calls.txt`: `kind:number arguments back` a line); their handlers are the port's.
#pragma once

#include "khdays/assets/kh2_ai.h"

#include <string>

namespace khdays::kh2_enemies {

namespace ai = khdays::assets::kh2::ai;

// The shapes from a `calls.txt`'s text; returns how many were read (0: none, the text not one).
int read_shapes(ai::Calls &calls, const std::string &text);

// Every handler the port has, by group: kind 0 (math and chance), the state machine, the actor's own fields.
void add_math(ai::Calls &calls);
void add_states(ai::Calls &calls);
void add_actor_fields(ai::Calls &calls);
inline void add_all(ai::Calls &calls)
{
    add_math(calls);
    add_states(calls);
    add_actor_fields(calls);
}

// KH2's chance (0:16 to 0:18): x = x * 0x10dcd, 32 bits; its seed per session.
void seed_chance(std::uint32_t seed);

} // namespace khdays::kh2_enemies
