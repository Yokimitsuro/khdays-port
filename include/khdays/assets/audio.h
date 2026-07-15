#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace khdays::assets {

// Neutral decoded audio: interleaved signed 16-bit PCM. The engine and the
// platform audio backend depend only on this, never on the DS SWAV format.
struct DecodedAudio final {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 1;
    std::vector<std::int16_t> samples;  // interleaved
    bool loops = false;
    std::uint32_t loop_start = 0;  // loop point, in sample frames
};

// Decode one SWAV waveform to PCM16. `swav` points at the SWAV header
// (u8 format [0=PCM8, 1=PCM16, 2=IMA-ADPCM]; u8 loop; u16 rate; u16 time;
// u16 loop_start; u32 non_loop_len; then sample data). Throws on malformed data.
DecodedAudio decode_swav(const std::uint8_t* swav, std::size_t size);

// An opened SDAT sound archive. Holds the file bytes and parsed directory so
// waveforms can be pulled out on demand. Create with open_sdat().
struct Sdat;

// Open and index an SDAT sound archive. Throws std::runtime_error on failure.
std::shared_ptr<Sdat> open_sdat(const std::filesystem::path& path);

// Number of wave archives (SWAR) in the SDAT.
std::size_t sdat_wave_archive_count(const Sdat& sdat);

// Number of waveforms (SWAV) inside one wave archive.
std::size_t sdat_wave_count(const Sdat& sdat, std::size_t wave_archive_index);

// Decode waveform `swav_index` from wave archive `wave_archive_index`.
DecodedAudio sdat_waveform(
    const Sdat& sdat,
    std::size_t wave_archive_index,
    std::size_t swav_index);

// Serialize decoded audio as a canonical 16-bit PCM WAV file (for export and
// testing).
std::vector<std::uint8_t> to_wav(const DecodedAudio& audio);

}  // namespace khdays::assets
