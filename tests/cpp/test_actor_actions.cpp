#include "khdays/assets/actor_actions.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void put_u16(std::vector<std::uint8_t>& data, const std::size_t offset,
             const std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void put_u32(std::vector<std::uint8_t>& data, const std::size_t offset,
             const std::uint32_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    data[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    data[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

}  // namespace

int main() {
    std::vector<std::uint8_t> wp(0x80U, 0U);
    put_u32(wp, 0x00U, 4U);
    put_u32(wp, 0x04U, 7U);
    put_u32(wp, 0x20U, 2U);
    put_u32(wp, 0x24U, 3U);
    put_u32(wp, 0x28U, 5U);
    put_u32(wp, 0x40U, 0x104U);  // narrows exactly like the DS routine
    const auto profiles = khdays::assets::decode_actor_weapon_profiles(
        wp.data(), wp.size());
    expect(profiles.size() == 2U, "wp rows are decoded");
    expect(profiles[0].model_index == 4U
               && profiles[0].hit_model_index == 7U,
           "weapon model fields retain their separate indices");
    expect(profiles[0].combo_variant == 2U
               && profiles[0].combo_kind == 3U
               && profiles[0].combo_kind_alt == 5U,
           "wp combo selectors are decoded");
    expect(profiles[1].model_index == 4U,
           "wp integer fields narrow to bytes");

    std::vector<std::uint8_t> ci(0x90U, 0U);
    put_u32(ci, 0U, 1U);
    put_u32(ci, 4U, 0x70U);
    put_u16(ci, 4U + 0x18U, 2U);
    put_u32(ci, 0x70U, 12U);
    put_u16(ci, 0x74U, 1U);
    put_u16(ci, 0x76U, 0xFFFFU);
    put_u32(ci, 0x78U, 18U);
    put_u16(ci, 0x7CU, 0x80U);
    put_u16(ci, 0x7EU, 0xFFFFU);
    const auto ids = khdays::assets::decode_actor_action_ids(
        ci.data(), ci.size(), 0U, 0U);
    expect(ids.size() == 2U && ids[0] == 12U && ids[1] == 18U,
           "ci primary node ids retain graph order");

    std::vector<std::uint8_t> cm(0x60U, 0U);
    put_u32(cm, 0U, 2U);
    put_u32(cm, 4U, 0x20U);
    put_u32(cm, 8U, 0x40U);
    put_u32(cm, 0x20U, 12U);
    put_u32(cm, 0x28U, 6U);
    put_u32(cm, 0x40U, 18U);
    put_u32(cm, 0x48U, 9U);
    expect(khdays::assets::actor_action_animation_row(
               cm.data(), cm.size(), 12U) == 6U,
           "cm maps action id to am.p2 row");
    expect(!khdays::assets::actor_action_animation_row(
                cm.data(), cm.size(), 99U),
           "unknown cm action id is absent");

    std::cout << "actor action parser tests passed\n";
    return EXIT_SUCCESS;
}
