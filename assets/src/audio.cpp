#include "khdays/assets/audio.h"
#include "khdays/assets/file_bytes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Decoder for Nintendo DS SDAT waveforms: navigate the SDAT container
// (INFO/FAT/FILE) to a SWAR wave archive, then decode its SWAV waveforms
// (PCM8, PCM16, or IMA-ADPCM) into neutral 16-bit PCM. The DS IMA-ADPCM
// step/index tables and decode follow the public Nintendo DS documentation.

namespace khdays::assets {

namespace {

using ByteVector = std::vector<std::uint8_t>;

std::uint16_t read_u16(const ByteVector& d, const std::size_t o) {
    if (o + 2U > d.size()) {
        throw std::runtime_error("SDAT: read past end of file");
    }
    return static_cast<std::uint16_t>(d[o] | (d[o + 1U] << 8U));
}

std::uint32_t read_u32(const ByteVector& d, const std::size_t o) {
    if (o + 4U > d.size()) {
        throw std::runtime_error("SDAT: read past end of file");
    }
    return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1U]) << 8U) |
           (static_cast<std::uint32_t>(d[o + 2U]) << 16U) | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

ByteVector read_file(const std::filesystem::path& path) {
    return read_file_bytes(path, "SDAT");
}

int clamp_int(const int value, const int low, const int high) {
    return value < low ? low : (value > high ? high : value);
}

// DS IMA-ADPCM tables.
constexpr std::array<int, 16> kIndexTable{-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<int, 89> kStepTable{
    7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
    31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
    130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
    544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
    2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

std::int16_t decode_adpcm_nibble(int& predictor, int& index, const unsigned nibble) {
    const int step = kStepTable[static_cast<std::size_t>(index)];
    int diff = step >> 3;
    if ((nibble & 4U) != 0U)
        diff += step;
    if ((nibble & 2U) != 0U)
        diff += step >> 1;
    if ((nibble & 1U) != 0U)
        diff += step >> 2;
    predictor = (nibble & 8U) != 0U ? predictor - diff : predictor + diff;
    predictor = clamp_int(predictor, -32768, 32767);
    index = clamp_int(index + kIndexTable[nibble], 0, 88);
    return static_cast<std::int16_t>(predictor);
}

}  // namespace

DecodedAudio decode_swav(const std::uint8_t* swav, const std::size_t size) {
    if (size < 0x0CU) {
        throw std::runtime_error("SWAV waveform is too small");
    }
    const auto format = swav[0];
    const auto loop_flag = swav[1];
    const auto rate = static_cast<std::uint32_t>(swav[2] | (swav[3] << 8U));
    const auto loop_start_words = static_cast<std::uint32_t>(swav[6] | (swav[7] << 8U));
    const auto non_loop_words = static_cast<std::uint32_t>(swav[8]) | (static_cast<std::uint32_t>(swav[9]) << 8U) |
                                (static_cast<std::uint32_t>(swav[10]) << 16U) |
                                (static_cast<std::uint32_t>(swav[11]) << 24U);

    // Sample data length is (loop_start + non_loop) 32-bit words, clamped to the
    // bytes actually available.
    std::size_t data_len = (static_cast<std::size_t>(loop_start_words) + non_loop_words) * 4U;
    if (data_len > size - 0x0CU) {
        data_len = size - 0x0CU;
    }
    const std::uint8_t* data = swav + 0x0C;

    DecodedAudio audio;
    audio.sample_rate = rate;
    audio.channels = 1;
    audio.loops = loop_flag != 0U;

    switch (format) {
    case 0: {  // PCM8
        audio.samples.reserve(data_len);
        for (std::size_t i = 0U; i < data_len; ++i) {
            // Scale 8-bit to 16-bit with a multiply; shifting a negative
            // value left is undefined behavior.
            audio.samples.push_back(
                static_cast<std::int16_t>(static_cast<int>(static_cast<std::int8_t>(data[i])) * 256));
        }
        audio.loop_start = loop_start_words * 4U;
        break;
    }
    case 1: {  // PCM16
        const std::size_t count = data_len / 2U;
        audio.samples.reserve(count);
        for (std::size_t i = 0U; i < count; ++i) {
            audio.samples.push_back(static_cast<std::int16_t>(data[i * 2U] | (data[i * 2U + 1U] << 8U)));
        }
        audio.loop_start = loop_start_words * 2U;
        break;
    }
    case 2: {  // IMA-ADPCM
        if (data_len < 4U) {
            throw std::runtime_error("ADPCM waveform has no header");
        }
        int predictor = static_cast<std::int16_t>(data[0] | (data[1] << 8U));
        int index = clamp_int(data[2], 0, 88);
        audio.samples.reserve((data_len - 4U) * 2U);
        for (std::size_t i = 4U; i < data_len; ++i) {
            const auto byte = data[i];
            audio.samples.push_back(decode_adpcm_nibble(predictor, index, byte & 0x0FU));
            audio.samples.push_back(decode_adpcm_nibble(predictor, index, byte >> 4U));
        }
        // loop_start counts words including the 1-word ADPCM header.
        audio.loop_start = loop_start_words > 0U ? (loop_start_words - 1U) * 8U : 0U;
        break;
    }
    default: throw std::runtime_error("unsupported SWAV format " + std::to_string(format));
    }
    return audio;
}

DecodedAudio decode_strm(const std::uint8_t* strm, const std::size_t strm_size) {
    const ByteVector d(strm, strm + strm_size);
    if (strm_size < 0x68U || d[0] != 'S' || d[1] != 'T' || d[2] != 'R' || d[3] != 'M') {
        throw std::runtime_error("stream is not a valid STRM");
    }
    const std::size_t s = 0;  // STRM base
    // HEAD block fields (see docs/AUDIO or the format notes).
    const auto format = d[s + 0x18U];
    const auto loop_flag = d[s + 0x19U];
    const int channels = std::max(1, static_cast<int>(read_u16(d, s + 0x1AU)));
    const auto rate = static_cast<std::uint32_t>(read_u16(d, s + 0x1CU));
    const auto loop_start = read_u32(d, s + 0x20U);
    const auto num_samples = read_u32(d, s + 0x24U);
    const auto data_offset = read_u32(d, s + 0x28U);
    const auto num_blocks = read_u32(d, s + 0x2CU);
    const auto block_len = read_u32(d, s + 0x30U);
    const auto last_block_len = read_u32(d, s + 0x38U);
    const std::size_t data_start = s + data_offset;

    // Decode each channel (walking its per-block runs), then interleave.
    std::vector<std::vector<std::int16_t>> per_channel(static_cast<std::size_t>(channels));
    for (int c = 0; c < channels; ++c) {
        auto& out = per_channel[static_cast<std::size_t>(c)];
        int predictor = 0;
        int adpcm_index = 0;
        for (std::uint32_t b = 0U; b < num_blocks; ++b) {
            const bool last = b == num_blocks - 1U;
            const std::size_t len = last ? last_block_len : block_len;
            const std::size_t base =
                data_start + static_cast<std::size_t>(b) * channels * block_len + static_cast<std::size_t>(c) * len;
            if (base + len > d.size()) {
                break;
            }
            if (format == 0U) {  // PCM8
                for (std::size_t i = 0U; i < len; ++i) {
                    out.push_back(
                        static_cast<std::int16_t>(static_cast<int>(static_cast<std::int8_t>(d[base + i])) * 256));
                }
            } else if (format == 1U) {  // PCM16
                for (std::size_t i = 0U; i + 1U < len; i += 2U) {
                    out.push_back(static_cast<std::int16_t>(d[base + i] | (d[base + i + 1U] << 8U)));
                }
            } else {  // IMA-ADPCM: each block restarts with a 4-byte header
                if (len < 4U) {
                    continue;
                }
                predictor = static_cast<std::int16_t>(d[base] | (d[base + 1U] << 8U));
                adpcm_index = clamp_int(d[base + 2U], 0, 88);
                for (std::size_t i = 4U; i < len; ++i) {
                    const auto byte = d[base + i];
                    out.push_back(decode_adpcm_nibble(predictor, adpcm_index, byte & 0x0FU));
                    out.push_back(decode_adpcm_nibble(predictor, adpcm_index, byte >> 4U));
                }
            }
        }
    }

    DecodedAudio audio;
    audio.sample_rate = rate;
    audio.channels = static_cast<std::uint16_t>(channels);
    audio.loops = loop_flag != 0U;
    audio.loop_start = loop_start;
    const std::size_t frames =
        per_channel.empty() ? 0U : std::min(static_cast<std::size_t>(num_samples), per_channel[0].size());
    audio.samples.reserve(frames * static_cast<std::size_t>(channels));
    for (std::size_t f = 0U; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            audio.samples.push_back(per_channel[static_cast<std::size_t>(c)][f]);
        }
    }
    return audio;
}

DecodedAudio load_wav(const std::filesystem::path& path) {
    const auto d = read_file(path);
    const auto has_tag = [&](const std::size_t off, const char* tag) {
        return off + 4U <= d.size() && d[off] == tag[0] && d[off + 1U] == tag[1] && d[off + 2U] == tag[2] &&
               d[off + 3U] == tag[3];
    };
    if (d.size() < 12U || !has_tag(0U, "RIFF") || !has_tag(8U, "WAVE")) {
        throw std::runtime_error("not a WAV file: " + path.string());
    }

    std::uint16_t format = 0U;
    std::uint16_t channels = 1U;
    std::uint16_t bits = 16U;
    std::uint32_t rate = 0U;
    std::size_t data_off = 0U;
    std::size_t data_size = 0U;

    std::size_t p = 12U;
    while (p + 8U <= d.size()) {
        const auto chunk_size = static_cast<std::size_t>(read_u32(d, p + 4U));
        const std::size_t body = p + 8U;
        if (has_tag(p, "fmt ") && body + 16U <= d.size()) {
            format = read_u16(d, body);
            channels = read_u16(d, body + 2U);
            rate = read_u32(d, body + 4U);
            bits = read_u16(d, body + 14U);
        } else if (has_tag(p, "data")) {
            data_off = body;
            data_size = chunk_size <= d.size() - body ? chunk_size : d.size() - body;
        }
        p = body + chunk_size + (chunk_size & 1U);  // chunks are word-aligned
    }

    if (format != 1U) {
        throw std::runtime_error("WAV is not PCM: " + path.string());
    }

    DecodedAudio audio;
    audio.sample_rate = rate;
    audio.channels = channels == 0U ? 1U : channels;
    if (bits == 16U) {
        const std::size_t count = data_size / 2U;
        audio.samples.reserve(count);
        for (std::size_t i = 0U; i < count; ++i) {
            audio.samples.push_back(
                static_cast<std::int16_t>(d[data_off + i * 2U] | (d[data_off + i * 2U + 1U] << 8U)));
        }
    } else if (bits == 8U) {
        audio.samples.reserve(data_size);
        for (std::size_t i = 0U; i < data_size; ++i) {
            audio.samples.push_back(static_cast<std::int16_t>((static_cast<int>(d[data_off + i]) - 128) * 256));
        }
    } else {
        throw std::runtime_error("unsupported WAV bit depth");
    }
    return audio;
}

}  // namespace khdays::assets
