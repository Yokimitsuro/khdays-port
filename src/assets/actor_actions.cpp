#include "khdays/assets/actor_actions.h"

namespace khdays::assets {

namespace {

std::uint16_t read_u16(
    const std::uint8_t* data, const std::size_t size, const std::size_t offset) {
    if (offset + 2U > size) {
        return 0U;
    }
    return static_cast<std::uint16_t>(data[offset])
        | static_cast<std::uint16_t>(data[offset + 1U] << 8U);
}

std::uint32_t read_u32(
    const std::uint8_t* data, const std::size_t size, const std::size_t offset) {
    if (offset + 4U > size) {
        return 0U;
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(data[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

std::uint8_t narrowed_word(
    const std::uint8_t* row, const std::size_t offset) {
    return static_cast<std::uint8_t>(read_u32(row, 0x40U, offset));
}

}  // namespace

std::vector<ActorWeaponProfile> decode_actor_weapon_profiles(
    const std::uint8_t* data, const std::size_t size) {
    std::vector<ActorWeaponProfile> result;
    if (data == nullptr) {
        return result;
    }
    const std::size_t count = size / 0x40U;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto* row = data + index * 0x40U;
        ActorWeaponProfile profile;
        profile.model_index = narrowed_word(row, 0x00U);
        profile.hit_model_index = narrowed_word(row, 0x04U);
        profile.combo_variant = narrowed_word(row, 0x20U);
        profile.combo_kind = narrowed_word(row, 0x24U);
        profile.combo_kind_alt = narrowed_word(row, 0x28U);
        result.push_back(profile);
    }
    return result;
}

std::vector<std::uint32_t> decode_actor_action_ids(
    const std::uint8_t* data,
    const std::size_t size,
    const std::uint8_t variant,
    const std::size_t group) {
    std::vector<std::uint32_t> result;
    if (data == nullptr || group > 1U) {
        return result;
    }
    constexpr std::size_t kHeader = 4U;
    constexpr std::size_t kRecordSize = 0x34U;
    const std::size_t record = kHeader
        + static_cast<std::size_t>(variant) * kRecordSize;
    if (record + kRecordSize > size) {
        return result;
    }

    // Ov022_BuildNodeGraph receives two halves from the record. Each half has
    // three table offsets followed by three u16 counts. The normal entry starts
    // at pool 0; its descriptors are {u32 actionId, s16 left, s16 right}.
    const std::size_t half = group == 0U ? 0U : 3U;
    const std::size_t table = read_u32(data, size, record + half * 4U);
    const std::size_t count_offset = record + 0x18U + group * 6U;
    const std::size_t count = read_u16(data, size, count_offset);
    if (count == 0U || count > 0x1000U || table + count * 8U > size) {
        return result;
    }
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        result.push_back(read_u32(data, size, table + index * 8U));
    }
    return result;
}

std::optional<std::size_t> actor_action_animation_row(
    const std::uint8_t* data,
    const std::size_t size,
    const std::uint32_t action_id) {
    if (data == nullptr || size < 4U) {
        return std::nullopt;
    }
    const std::size_t count = read_u32(data, size, 0U) & 0xFFU;
    if (count == 0U || count > (size - 4U) / 4U) {
        return std::nullopt;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const std::size_t record = read_u32(data, size, 4U + index * 4U);
        if (record + 12U > size) {
            continue;
        }
        if (read_u32(data, size, record) == action_id) {
            return static_cast<std::size_t>(read_u32(data, size, record + 8U));
        }
    }
    return std::nullopt;
}

}  // namespace khdays::assets
