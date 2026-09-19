#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace khdays::assets {

// The packed 0x20-byte weapon/action record produced by
// ov002::func_02052308 from one 0x40-byte row of ba/ch/<actor>/wp.b.z.
// Only fields whose consumers are understood are named here.
struct ActorWeaponProfile final {
    std::uint8_t model_index = 0;
    std::uint8_t hit_model_index = 0;
    std::uint8_t combo_variant = 0;
    std::uint8_t combo_kind = 0;
    std::uint8_t combo_kind_alt = 0;
};

// Decode every complete 0x40-byte row in a decompressed wp.b.z file. The
// narrowing matches func_ov002_02052308; malformed trailing bytes are ignored.
std::vector<ActorWeaponProfile> decode_actor_weapon_profiles(
    const std::uint8_t* data, std::size_t size);

// Reconstruct the primary node walk for one group of a ci.b.z variant. ci.b.z
// is already decompressed here. Group 0 is the normal ground chain and group 1
// the alternate chain selected by ov022's state entry.
std::vector<std::uint32_t> decode_actor_action_ids(
    const std::uint8_t* data,
    std::size_t size,
    std::uint8_t variant,
    std::size_t group);

// cm.b.z records are indexed by an action id. Word 2 of the matching record is
// the tag claimed in the 16-row action table; ov022 subsequently uses that tag
// as the sub-file index in am.p2. Returns no value for a missing/malformed id.
std::optional<std::size_t> actor_action_animation_row(
    const std::uint8_t* data,
    std::size_t size,
    std::uint32_t action_id);

}  // namespace khdays::assets
