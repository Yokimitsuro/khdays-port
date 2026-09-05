#include "khdays/resource/mission.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

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

std::optional<std::string> story_movie_reference(
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
        return find_movie(khdays::assets::extract_p2_subfile(
            mission_archive.data(), mission_archive.size(), index));
    }
    return std::nullopt;
}

std::optional<std::string> load_story_movie(
    const std::uint16_t mission_id,
    const std::uint32_t day) {
    return story_movie_reference(
        khdays::vfs::read("mi/mi/" + std::to_string(mission_id)), day);
}

}  // namespace khdays::resource
