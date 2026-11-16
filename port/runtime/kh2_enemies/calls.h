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

// Every handler the port has, by group: kind 0 (math and chance), the state machine, the actor's own fields, its
// motions (and jumps and fading), its moving and facing, and what it reaches of the world (its target, effects,
// threads; the actor's World).
void add_math(ai::Calls &calls);
void add_states(ai::Calls &calls);
void add_actor_fields(ai::Calls &calls);
void add_motions(ai::Calls &calls);
void add_moving(ai::Calls &calls);
void add_world(ai::Calls &calls);
inline void add_all(ai::Calls &calls)
{
    add_math(calls);
    add_states(calls);
    add_actor_fields(calls);
    add_motions(calls);
    add_moving(calls);
    add_world(calls);
}

// KH2's chance (0:16 to 0:18): x = x * 0x10dcd, 32 bits; its seed per session.
void seed_chance(std::uint32_t seed);
// Its next value (the game's own draws share it: Final Xemnas's lasers pick theirs by it)
std::uint32_t kh2_chance();

// The memory 2:21 shares among a field's objects (trap_teamwork_alloc) let go, as KH2 lets it go with its field
// (FUN_1403f6ea0): the next field's objects make theirs anew; the target slots' owners with it.
void forget_teamwork();

} // namespace khdays::kh2_enemies
