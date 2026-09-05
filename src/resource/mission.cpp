#include "khdays/resource/mission.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include "khdays/assets/cakp.h"
#include "khdays/assets/message.h"
#include "khdays/vfs/filesystem.h"

namespace khdays::resource {

namespace {

std::uint16_t read_u16(
    const std::vector<std::uint8_t>& data,
    const std::size_t offset) {
    if (offset + 2U > data.size()) {
        throw std::runtime_error("mission P2: truncated u16");
    }
    return static_cast<std::uint16_t>(data[offset])
        | static_cast<std::uint16_t>(data[offset + 1U] << 8U);
}

std::string fixed_name(
    const std::vector<std::uint8_t>& data,
    const std::size_t offset) {
    if (offset + 8U > data.size()) {
        throw std::runtime_error("mission P2: truncated name table");
    }
    std::size_t length = 0U;
    while (length < 8U && data[offset + length] != 0U) {
        ++length;
    }
    return std::string{
        reinterpret_cast<const char*>(data.data() + offset), length};
}

std::optional<std::string> find_movie(
    const std::vector<std::uint8_t>& script) {
    constexpr std::string_view prefix = "/mv/";
    constexpr std::string_view suffix = ".mods";
    for (std::size_t i = 0U; i + prefix.size() < script.size(); ++i) {
        bool prefix_matches = true;
        for (std::size_t j = 0U; j < prefix.size(); ++j) {
            if (script[i + j] != static_cast<std::uint8_t>(prefix[j])) {
                prefix_matches = false;
                break;
            }
        }
        if (!prefix_matches) {
            continue;
        }
        std::size_t end = i + prefix.size();
        while (end < script.size() && script[end] >= '0'
               && script[end] <= '9') {
            ++end;
        }
        if (end == i + prefix.size() || end + suffix.size() > script.size()) {
            continue;
        }
        bool suffix_matches = true;
        for (std::size_t j = 0U; j < suffix.size(); ++j) {
            if (script[end + j] != static_cast<std::uint8_t>(suffix[j])) {
                suffix_matches = false;
                break;
            }
        }
        if (suffix_matches) {
            // VFS paths are relative; the script spelling deliberately keeps
            // its leading slash, so discard only that byte.
            return std::string{
                reinterpret_cast<const char*>(script.data() + i + 1U),
                end + suffix.size() - i - 1U};
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> story_day_bundle(
    const std::vector<std::uint8_t>& mission_archive,
    const std::uint32_t day) {
    if (mission_archive.size() < 0x10U || mission_archive[0] != 'P'
        || mission_archive[1] != '2') {
        throw std::runtime_error("mission archive is not a P2 container");
    }
    const std::size_t count = read_u16(mission_archive, 2U) & 0x1ffU;
    const std::size_t descriptor_offset =
        0x10U + ((count + 1U) / 2U) * 4U;
    const std::size_t names_offset = descriptor_offset + count * 4U;
    if (names_offset + count * 8U > mission_archive.size()) {
        throw std::runtime_error("mission P2: name table exceeds archive");
    }

    const std::string wanted = std::to_string(day) + ".Z";
    for (std::size_t index = 0U; index < count; ++index) {
        if (fixed_name(mission_archive, names_offset + index * 8U) != wanted) {
            continue;
        }
        return khdays::assets::extract_p2_subfile(
            mission_archive.data(), mission_archive.size(), index);
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> load_story_day_bundle(
    const std::uint16_t mission_id,
    const std::uint32_t day) {
    return story_day_bundle(
        khdays::vfs::read("mi/mi/" + std::to_string(mission_id)), day);
}

std::optional<std::string> story_movie_reference(
    const std::vector<std::uint8_t>& mission_archive,
    const std::uint32_t day) {
    const auto bundle = story_day_bundle(mission_archive, day);
    return bundle ? find_movie(*bundle) : std::nullopt;
}

std::optional<StorySequence> story_sequence(
    const std::vector<std::uint8_t>& mission_archive,
    const std::uint32_t day) {
    const auto bundle = story_day_bundle(mission_archive, day);
    if (!bundle) {
        return std::nullopt;
    }

    StorySequence sequence;
    sequence.movie_path = find_movie(*bundle);
    const auto archive = khdays::assets::decode_cakp(*bundle);
    const auto init = std::find_if(
        archive.scripts.begin(), archive.scripts.end(),
        [](const khdays::assets::CakpScript& script) {
            return script.name == "_i";
        });
    if (init == archive.scripts.end()) {
        return sequence;
    }

    const auto commands = khdays::assets::decode_action_instructions(
        init->bytes);
    for (const auto& command : commands) {
        // Game_ActionAssign (group 0, command 0): a mode-4 destination packs
        // field id in its low half and width in its high half. Field 0,width 9
        // is the persistent story day.
        if (command.group == 0U && command.command == 0U
            && command.operands.size() >= 2U
            && command.operands[0].kind == 4U
            && command.operands[0].value == 0x00090000U
            && command.operands[1].kind == 1U) {
            sequence.stored_day = command.operands[1].value;
        }

        // func_02022290 (group 0, command 12) stores the two resolved
        // operands as the pending request kind and argument, then yields.
        if (command.group == 0U && command.command == 12U
            && command.operands.size() >= 2U
            && command.operands[0].kind == 1U
            && command.operands[1].kind == 1U) {
            sequence.request_kind = command.operands[0].value;
            sequence.request_argument = command.operands[1].value;
        }
    }
    return sequence;
}

std::optional<StorySequence> load_story_sequence(
    const std::uint16_t mission_id,
    const std::uint32_t day) {
    return story_sequence(
        khdays::vfs::read("mi/mi/" + std::to_string(mission_id)), day);
}

std::optional<std::string> load_story_movie(
    const std::uint16_t mission_id,
    const std::uint32_t day) {
    const auto bundle = load_story_day_bundle(mission_id, day);
    return bundle ? find_movie(*bundle) : std::nullopt;
}

}  // namespace khdays::resource
