#include "khdays/assets/actor_actions.h"

#include <limits>
#include <set>
#include <utility>

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

std::int16_t read_s16(
    const std::uint8_t* data, const std::size_t size,
    const std::size_t offset) {
    return static_cast<std::int16_t>(read_u16(data, size, offset));
}

std::int32_t read_s32(
    const std::uint8_t* data, const std::size_t size,
    const std::size_t offset) {
    return static_cast<std::int32_t>(read_u32(data, size, offset));
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

std::vector<ActorComboAction> decode_actor_combo_chain(
    const std::uint8_t* ci_data,
    const std::size_t ci_size,
    const std::uint8_t* cm_data,
    const std::size_t cm_size,
    const std::uint8_t variant,
    const std::size_t group,
    const std::uint8_t combo_kind) {
    std::vector<ActorComboAction> result;
    if (ci_data == nullptr || cm_data == nullptr || group > 1U) {
        return result;
    }
    constexpr std::size_t kHeader = 4U;
    constexpr std::size_t kRecordSize = 0x34U;
    constexpr std::int16_t kEntryLink = 0x80;
    const std::size_t record = kHeader
        + static_cast<std::size_t>(variant) * kRecordSize;
    if (record + kRecordSize > ci_size) {
        return result;
    }

    const std::size_t half = group == 0U ? 0U : 3U;
    std::size_t tables[3]{};
    std::size_t counts[3]{};
    for (std::size_t pool = 0U; pool < 3U; ++pool) {
        tables[pool] = read_u32(
            ci_data, ci_size, record + (half + pool) * 4U);
        counts[pool] = read_u16(
            ci_data, ci_size,
            record + 0x18U + group * 6U + pool * 2U);
        if (counts[pool] > 0x1000U
            || (counts[pool] != 0U
                && (tables[pool] > ci_size
                    || counts[pool] > (ci_size - tables[pool]) / 8U))) {
            return {};
        }
    }
    if (counts[0] == 0U && counts[1] == 0U) {
        return result;
    }

    std::size_t pool = combo_kind <= 1U ? 1U : 0U;
    if (counts[pool] == 0U) {
        return result;
    }
    std::size_t node = 0U;
    std::int32_t step = combo_kind <= 1U ? -1 : 1;
    std::set<std::pair<std::size_t, std::size_t>> visited;

    // The runtime has at most 64 unique action links. The same bound also
    // keeps malformed or cyclic input from walking forever.
    while (result.size() < 64U && node < counts[pool]
           && visited.emplace(pool, node).second) {
        const std::size_t descriptor = tables[pool] + node * 8U;
        const std::uint32_t action_id =
            read_u32(ci_data, ci_size, descriptor);

        if (cm_size >= 4U) {
            const std::size_t record_count =
                read_u32(cm_data, cm_size, 0U) & 0xFFU;
            if (record_count <= (cm_size - 4U) / 4U) {
                for (std::size_t index = 0U; index < record_count; ++index) {
                    const std::size_t cm_record =
                        read_u32(cm_data, cm_size, 4U + index * 4U);
                    if (cm_record + 0x20U > cm_size
                        || read_u32(cm_data, cm_size, cm_record)
                            != action_id) {
                        continue;
                    }
                    ActorComboAction action;
                    action.action_id = action_id;
                    action.animation_row =
                        read_u32(cm_data, cm_size, cm_record + 0x08U);
                    action.transition_frame_q12 =
                        read_s32(cm_data, cm_size, cm_record + 0x14U);
                    action.input_start_q12 =
                        read_s32(cm_data, cm_size, cm_record + 0x18U);
                    action.input_end_q12 =
                        read_s32(cm_data, cm_size, cm_record + 0x1cU);
                    result.push_back(action);
                    break;
                }
            }
        }

        const std::int16_t left =
            read_s16(ci_data, ci_size, descriptor + 4U);
        bool has_next = false;
        std::size_t next_pool = pool;
        std::size_t next_node = 0U;

        // func_ov022_020b0ac8 switches to list 1 when nStep reaches
        // Group::nKind. A 0x80 link carries the same pool as its alternate.
        if (pool == 0U && step >= 0
            && step + 1 >= static_cast<std::int32_t>(combo_kind)) {
            if (counts[1] != 0U
                && (left >= 0 || left == kEntryLink)) {
                has_next = true;
                next_pool = 1U;
                next_node = 0U;
            }
            step = -1;
        } else if (left == kEntryLink) {
            if (pool == 0U && counts[1] != 0U) {
                has_next = true;
                next_pool = 1U;
                next_node = 0U;
            }
        } else if (left >= 0
                   && static_cast<std::size_t>(left) < counts[pool]) {
            has_next = true;
            next_node = static_cast<std::size_t>(left);
        }

        if (!has_next) {
            break;
        }
        if (step >= 0) {
            ++step;
        }
        pool = next_pool;
        node = next_node;
    }
    return result;
}

bool actor_combo_accepts_input(
    const ActorComboAction& action, const std::int32_t frame_q12) {
    const std::int32_t end = action.input_end_q12 == -0x1000
        ? std::numeric_limits<std::int32_t>::max()
        : action.input_end_q12;
    return action.input_start_q12 <= frame_q12 && frame_q12 < end;
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
