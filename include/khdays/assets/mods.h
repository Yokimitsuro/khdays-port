#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace khdays::assets {

// A MobiClip "MODS" container — the game's `mv/*.mods` cutscenes.
//
// The header layout below was read out of the game's own reader and then
// verified across all 46 of its clips. Note the game validates the **version
// tag** (`N3`) byte-wise and never compares the `MODS` word itself; the tag
// gates how frames are parsed, so it is what we check.
struct ModsInfo final {
    std::uint32_t frame_count = 0;
    int width = 0;   // always 256 in this game
    int height = 0;  // always 160 (letterboxed inside the DS's 192)

    // Audio is optional: 9 of the game's 46 clips are video-only and carry zero
    // in all three fields.
    int audio_coding = 0;    // 3 on every clip that has audio
    int audio_channels = 0;  // 2 (stereo) wherever audio exists
    int audio_rate = 0;      // 22050, or 32728 (the DS's native rate)

    bool has_audio() const { return audio_channels > 0; }
};

// Decode a MODS header. Throws std::runtime_error if the data is too small or
// the version tag is not `N3`.
ModsInfo parse_mods_header(const std::uint8_t* data, std::size_t size);
ModsInfo parse_mods_header(const std::filesystem::path& path);

}  // namespace khdays::assets
