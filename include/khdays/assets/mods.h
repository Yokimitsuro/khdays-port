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
    std::uint32_t fps_fixed = 0;  // 8.24 fixed point

    // Audio is optional: 9 of the game's 46 clips are video-only and carry zero
    // in all three fields.
    int audio_coding = 0;    // 3 on every clip that has audio
    int audio_channels = 0;  // 2 (stereo) wherever audio exists
    int audio_rate = 0;      // 22050, or 32728 (the DS's native rate)

    std::uint32_t largest_frame = 0;
    std::uint32_t audio_info_offset = 0;
    std::uint32_t key_table_offset = 0;
    std::uint32_t key_frame_count = 0;
    std::size_t packet_data_offset = 0;

    bool has_audio() const { return audio_channels > 0; }
    double frames_per_second() const {
        return static_cast<double>(fps_fixed) / 16777216.0;
    }
};

// Decode a MODS header. Throws std::runtime_error if the data is too small or
// the version tag is not `N3`.
ModsInfo parse_mods_header(const std::uint8_t* data, std::size_t size);
ModsInfo parse_mods_header(const std::filesystem::path& path);

}  // namespace khdays::assets
