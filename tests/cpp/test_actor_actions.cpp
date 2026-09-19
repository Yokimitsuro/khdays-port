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

    std::vector<std::uint8_t> ci(0x98U, 0U);
    put_u32(ci, 0U, 1U);
    put_u32(ci, 4U, 0x70U);
    put_u32(ci, 8U, 0x80U);
    put_u16(ci, 4U + 0x18U, 2U);
    put_u16(ci, 4U + 0x1aU, 1U);
    put_u32(ci, 0x70U, 12U);
    put_u16(ci, 0x74U, 1U);
    put_u16(ci, 0x76U, 0xFFFFU);
    put_u32(ci, 0x78U, 18U);
    put_u16(ci, 0x7CU, 0x80U);
    put_u16(ci, 0x7EU, 0xFFFFU);
    put_u32(ci, 0x80U, 24U);
    put_u16(ci, 0x84U, 0xFFFFU);
    put_u16(ci, 0x86U, 0xFFFFU);
    const auto ids = khdays::assets::decode_actor_action_ids(
        ci.data(), ci.size(), 0U, 0U);
    expect(ids.size() == 2U && ids[0] == 12U && ids[1] == 18U,
           "ci primary node ids retain graph order");

    std::vector<std::uint8_t> cm(0xe0U, 0U);
    put_u32(cm, 0U, 3U);
    put_u32(cm, 4U, 0x20U);
    put_u32(cm, 8U, 0x60U);
    put_u32(cm, 12U, 0xa0U);
    put_u32(cm, 0x20U, 12U);
    put_u32(cm, 0x28U, 6U);
    put_u32(cm, 0x34U, 0x1000U);
    put_u32(cm, 0x38U, 0x3000U);
    put_u32(cm, 0x3cU, 0x9000U);
    put_u32(cm, 0x60U, 18U);
    put_u32(cm, 0x68U, 9U);
    put_u32(cm, 0x74U, 0x2000U);
    put_u32(cm, 0x78U, 0x4000U);
    put_u32(cm, 0x7cU, 0xfffff000U);
    put_u32(cm, 0xa0U, 24U);
    put_u32(cm, 0xa8U, 11U);
    expect(khdays::assets::actor_action_animation_row(
               cm.data(), cm.size(), 12U) == 6U,
           "cm maps action id to am.p2 row");
    expect(!khdays::assets::actor_action_animation_row(
                cm.data(), cm.size(), 99U),
           "unknown cm action id is absent");

    const auto combo = khdays::assets::decode_actor_combo_chain(
        ci.data(), ci.size(), cm.data(), cm.size(), 0U, 0U, 3U);
    expect(combo.size() == 3U,
           "ci walk crosses into pool 1 for the combo finisher");
    expect(combo[0].action_id == 12U && combo[1].action_id == 18U
               && combo[2].action_id == 24U,
           "combo action ids retain ov022 left-link order");
    expect(combo[0].animation_row == 6U
               && combo[1].animation_row == 9U
               && combo[2].animation_row == 11U,
           "combo nodes resolve their CM animation tags");
    expect(combo[0].transition_frame_q12 == 0x1000
               && combo[0].input_start_q12 == 0x3000
               && combo[0].input_end_q12 == 0x9000,
           "CM transition and input windows retain Fx12 precision");
    expect(!khdays::assets::actor_combo_accepts_input(combo[0], 0x2fff)
               && khdays::assets::actor_combo_accepts_input(
                   combo[0], 0x3000)
               && !khdays::assets::actor_combo_accepts_input(
                   combo[0], 0x9000),
           "finite combo input window is half open");
    expect(khdays::assets::actor_combo_accepts_input(
               combo[1], 0x7ffffffe),
           "-0x1000 CM upper bound means no upper limit");

    std::cout << "actor action parser tests passed\n";
    return EXIT_SUCCESS;
}
