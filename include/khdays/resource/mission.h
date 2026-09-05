#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace khdays::resource {

struct StorySequence final {
    std::optional<std::string> movie_path;
    std::optional<std::uint32_t> stored_day;
    std::optional<std::uint32_t> request_kind;
    std::optional<std::uint32_t> request_argument;
};

// Extract the named `<day>.Z` CAKP bundle from a mission P2 archive.
std::optional<std::vector<std::uint8_t>> story_day_bundle(
    const std::vector<std::uint8_t>& mission_archive,
    std::uint32_t day);

std::optional<std::vector<std::uint8_t>> load_story_day_bundle(
    std::uint16_t mission_id,
    std::uint32_t day);

// Decode the verified persistent-day assignment and pending request issued by
// the bundle's `_i` action script, together with its referenced MobiClip.
std::optional<StorySequence> story_sequence(
    const std::vector<std::uint8_t>& mission_archive,
    std::uint32_t day);

std::optional<StorySequence> load_story_sequence(
    std::uint16_t mission_id,
    std::uint32_t day);

// Resolve the MobiClip referenced by `<day>.Z` inside a mission P2 archive.
// Mission 10000 is the story dispatcher used by ov002: ov004 writes the
// selected day, then the `_s` script selects the matching named CAKP bundle.
std::optional<std::string> story_movie_reference(
    const std::vector<std::uint8_t>& mission_archive,
    std::uint32_t day);

// Load `mi/mi/<mission_id>` through the VFS and resolve its selected-day movie.
std::optional<std::string> load_story_movie(
    std::uint16_t mission_id,
    std::uint32_t day);

}  // namespace khdays::resource
