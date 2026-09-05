#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace khdays::resource {

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
