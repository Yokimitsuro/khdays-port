#include "khdays/assets/audio.h"

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
    return static_cast<std::uint32_t>(d[o])
        | (static_cast<std::uint32_t>(d[o + 1U]) << 8U)
        | (static_cast<std::uint32_t>(d[o + 2U]) << 16U)
        | (static_cast<std::uint32_t>(d[o + 3U]) << 24U);
}

ByteVector read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("cannot open SDAT file: " + path.string());
    }
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("cannot size SDAT file: " + path.string());
    }
    ByteVector data(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!data.empty()) {
        stream.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    }
    return data;
}

int clamp_int(const int value, const int low, const int high) {
    return value < low ? low : (value > high ? high : value);
}

// DS IMA-ADPCM tables.
constexpr std::array<int, 16> kIndexTable{
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<int, 89> kStepTable{
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};

std::int16_t decode_adpcm_nibble(int& predictor, int& index, const unsigned nibble) {
    const int step = kStepTable[static_cast<std::size_t>(index)];
    int diff = step >> 3;
    if ((nibble & 4U) != 0U) diff += step;
    if ((nibble & 2U) != 0U) diff += step >> 1;
    if ((nibble & 1U) != 0U) diff += step >> 2;
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
    const auto loop_start_words =
        static_cast<std::uint32_t>(swav[6] | (swav[7] << 8U));
    const auto non_loop_words = static_cast<std::uint32_t>(swav[8])
        | (static_cast<std::uint32_t>(swav[9]) << 8U)
        | (static_cast<std::uint32_t>(swav[10]) << 16U)
        | (static_cast<std::uint32_t>(swav[11]) << 24U);

    // Sample data length is (loop_start + non_loop) 32-bit words, clamped to the
    // bytes actually available.
    std::size_t data_len =
        (static_cast<std::size_t>(loop_start_words) + non_loop_words) * 4U;
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
                audio.samples.push_back(static_cast<std::int16_t>(
                    static_cast<std::int8_t>(data[i]) << 8));
            }
            audio.loop_start = loop_start_words * 4U;
            break;
        }
        case 1: {  // PCM16
            const std::size_t count = data_len / 2U;
            audio.samples.reserve(count);
            for (std::size_t i = 0U; i < count; ++i) {
                audio.samples.push_back(static_cast<std::int16_t>(
                    data[i * 2U] | (data[i * 2U + 1U] << 8U)));
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
                audio.samples.push_back(
                    decode_adpcm_nibble(predictor, index, byte & 0x0FU));
                audio.samples.push_back(
                    decode_adpcm_nibble(predictor, index, byte >> 4U));
            }
            // loop_start counts words including the 1-word ADPCM header.
            audio.loop_start = loop_start_words > 0U
                ? (loop_start_words - 1U) * 8U
                : 0U;
            break;
        }
        default:
            throw std::runtime_error(
                "unsupported SWAV format " + std::to_string(format));
    }
    return audio;
}

struct Sdat final {
    ByteVector data;
    std::vector<std::uint32_t> wave_archive_file_ids;  // per wave archive
    std::vector<std::pair<std::uint32_t, std::uint32_t>> fat;  // offset, size

    struct SeqInfo final {
        std::uint32_t file_id = 0xFFFFFFFFU;
        int bank = -1;
        std::uint8_t volume = 127;
    };
    std::vector<SeqInfo> sequences;

    struct BankInfo final {
        std::uint32_t file_id = 0xFFFFFFFFU;
        std::array<int, 4> wave_archives{-1, -1, -1, -1};
    };
    std::vector<BankInfo> banks;
};

std::shared_ptr<Sdat> open_sdat(const std::filesystem::path& path) {
    auto sdat = std::make_shared<Sdat>();
    sdat->data = read_file(path);
    const auto& d = sdat->data;
    if (d.size() < 0x40U || d[0] != 'S' || d[1] != 'D' || d[2] != 'A'
        || d[3] != 'T') {
        throw std::runtime_error("resource is not an SDAT file");
    }

    const auto info_off = static_cast<std::size_t>(read_u32(d, 0x18U));
    const auto fat_off = static_cast<std::size_t>(read_u32(d, 0x20U));
    if (info_off + 4U > d.size() || fat_off + 12U > d.size()) {
        throw std::runtime_error("SDAT INFO/FAT offsets are invalid");
    }

    // INFO category 3 = wave archives: u32 count, then count u32 entry offsets
    // (relative to INFO); each entry begins with the SWAR's FAT file id.
    const auto wave_record = info_off
        + static_cast<std::size_t>(read_u32(d, info_off + 0x08U + 3U * 4U));
    const auto wave_count = read_u32(d, wave_record);
    sdat->wave_archive_file_ids.reserve(wave_count);
    for (std::uint32_t i = 0U; i < wave_count; ++i) {
        const auto entry_rel = read_u32(d, wave_record + 4U + i * 4U);
        sdat->wave_archive_file_ids.push_back(
            entry_rel == 0U ? 0xFFFFFFFFU
                            : read_u32(d, info_off + entry_rel));
    }

    // FAT: u32 count at +0x08, then 16-byte records {offset, size, 0, 0}.
    const auto fat_count = read_u32(d, fat_off + 0x08U);
    sdat->fat.reserve(fat_count);
    for (std::uint32_t i = 0U; i < fat_count; ++i) {
        const auto rec = fat_off + 0x0CU + static_cast<std::size_t>(i) * 16U;
        sdat->fat.emplace_back(read_u32(d, rec), read_u32(d, rec + 4U));
    }

    const auto category_record = [&](const std::size_t category) {
        return info_off
            + static_cast<std::size_t>(
                  read_u32(d, info_off + 0x08U + category * 4U));
    };

    // INFO category 0 = sequences: u16 file_id, u16 unk, u16 bank, u8 volume.
    const auto seq_record = category_record(0);
    const auto seq_count = read_u32(d, seq_record);
    sdat->sequences.reserve(seq_count);
    for (std::uint32_t i = 0U; i < seq_count; ++i) {
        const auto entry_rel = read_u32(d, seq_record + 4U + i * 4U);
        Sdat::SeqInfo seq;
        if (entry_rel != 0U) {
            const auto e = info_off + entry_rel;
            seq.file_id = read_u16(d, e);
            seq.bank = static_cast<int>(read_u16(d, e + 4U));
            seq.volume = d[e + 6U];
        }
        sdat->sequences.push_back(seq);
    }

    // INFO category 2 = banks: u16 file_id, u16 unk, u16 wave_archive[4].
    const auto bank_record = category_record(2);
    const auto bank_count = read_u32(d, bank_record);
    sdat->banks.reserve(bank_count);
    for (std::uint32_t i = 0U; i < bank_count; ++i) {
        const auto entry_rel = read_u32(d, bank_record + 4U + i * 4U);
        Sdat::BankInfo bank;
        if (entry_rel != 0U) {
            const auto e = info_off + entry_rel;
            bank.file_id = read_u16(d, e);
            for (int s = 0; s < 4; ++s) {
                const auto wa = read_u16(d, e + 4U + static_cast<std::size_t>(s) * 2U);
                bank.wave_archives[static_cast<std::size_t>(s)] =
                    wa == 0xFFFFU ? -1 : static_cast<int>(wa);
            }
        }
        sdat->banks.push_back(bank);
    }
    return sdat;
}

std::size_t sdat_sequence_count(const Sdat& sdat) {
    return sdat.sequences.size();
}

SdatSequence sdat_sequence(const Sdat& sdat, const std::size_t index) {
    if (index >= sdat.sequences.size()) {
        throw std::runtime_error("sequence index out of range");
    }
    const auto& info = sdat.sequences[index];
    if (info.file_id >= sdat.fat.size()) {
        throw std::runtime_error("sequence has no file");
    }
    const auto& [offset, size] = sdat.fat[info.file_id];
    if (static_cast<std::size_t>(offset) + size > sdat.data.size()
        || size < 0x1CU || sdat.data[offset] != 'S' || sdat.data[offset + 1U] != 'S'
        || sdat.data[offset + 2U] != 'E' || sdat.data[offset + 3U] != 'Q') {
        throw std::runtime_error("sequence is not a valid SSEQ");
    }
    SdatSequence seq;
    seq.data = sdat.data.data() + offset;
    seq.size = size;
    seq.bank = info.bank;
    seq.volume = info.volume;
    return seq;
}

SdatBank sdat_bank(const Sdat& sdat, const std::size_t index) {
    if (index >= sdat.banks.size()) {
        throw std::runtime_error("bank index out of range");
    }
    const auto& info = sdat.banks[index];
    if (info.file_id >= sdat.fat.size()) {
        throw std::runtime_error("bank has no file");
    }
    const auto& [offset, size] = sdat.fat[info.file_id];
    if (static_cast<std::size_t>(offset) + size > sdat.data.size()
        || size < 0x3CU || sdat.data[offset] != 'S' || sdat.data[offset + 1U] != 'B'
        || sdat.data[offset + 2U] != 'N' || sdat.data[offset + 3U] != 'K') {
        throw std::runtime_error("bank is not a valid SBNK");
    }
    SdatBank bank;
    bank.data = sdat.data.data() + offset;
    bank.size = size;
    bank.wave_archives = info.wave_archives;
    return bank;
}

std::size_t sdat_wave_archive_count(const Sdat& sdat) {
    return sdat.wave_archive_file_ids.size();
}

// Locate the SWAR blob backing a wave archive; returns (offset, size).
namespace {
std::pair<std::size_t, std::size_t> wave_archive_blob(
    const Sdat& sdat, const std::size_t index) {
    if (index >= sdat.wave_archive_file_ids.size()) {
        throw std::runtime_error("wave archive index out of range");
    }
    const auto file_id = sdat.wave_archive_file_ids[index];
    if (file_id >= sdat.fat.size()) {
        throw std::runtime_error("wave archive has no file");
    }
    const auto& [offset, size] = sdat.fat[file_id];
    if (static_cast<std::size_t>(offset) + size > sdat.data.size()
        || size < 0x3CU
        || sdat.data[offset] != 'S' || sdat.data[offset + 1U] != 'W'
        || sdat.data[offset + 2U] != 'A' || sdat.data[offset + 3U] != 'R') {
        throw std::runtime_error("wave archive is not a valid SWAR");
    }
    return {offset, size};
}
}  // namespace

std::size_t sdat_wave_count(const Sdat& sdat, const std::size_t index) {
    const auto [offset, size] = wave_archive_blob(sdat, index);
    (void)size;
    return read_u32(sdat.data, offset + 0x38U);
}

DecodedAudio sdat_waveform(
    const Sdat& sdat,
    const std::size_t wave_archive_index,
    const std::size_t swav_index) {
    const auto [swar, swar_size] = wave_archive_blob(sdat, wave_archive_index);
    const auto count = read_u32(sdat.data, swar + 0x38U);
    if (swav_index >= count) {
        throw std::runtime_error("SWAV index out of range");
    }
    const auto swav_rel =
        read_u32(sdat.data, swar + 0x3CU + swav_index * 4U);
    const std::size_t swav_off = swar + swav_rel;
    const std::size_t swav_end = swav_index + 1U < count
        ? swar + read_u32(sdat.data, swar + 0x3CU + (swav_index + 1U) * 4U)
        : swar + swar_size;
    if (swav_off >= swav_end || swav_end > sdat.data.size()) {
        throw std::runtime_error("SWAV waveform is out of range");
    }
    return decode_swav(sdat.data.data() + swav_off, swav_end - swav_off);
}

std::vector<std::uint8_t> to_wav(const DecodedAudio& audio) {
    const std::uint32_t data_bytes =
        static_cast<std::uint32_t>(audio.samples.size() * 2U);
    const std::uint32_t byte_rate =
        audio.sample_rate * audio.channels * 2U;
    const std::uint16_t block_align = static_cast<std::uint16_t>(audio.channels * 2U);

    std::vector<std::uint8_t> wav;
    wav.reserve(44U + data_bytes);
    const auto put32 = [&](std::uint32_t v) {
        wav.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        wav.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
        wav.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
        wav.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
    };
    const auto put16 = [&](std::uint16_t v) {
        wav.push_back(static_cast<std::uint8_t>(v & 0xFFU));
        wav.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    };
    const auto tag = [&](const char* s) {
        wav.insert(wav.end(), s, s + 4);
    };

    tag("RIFF");
    put32(36U + data_bytes);
    tag("WAVE");
    tag("fmt ");
    put32(16U);
    put16(1U);  // PCM
    put16(audio.channels);
    put32(audio.sample_rate);
    put32(byte_rate);
    put16(block_align);
    put16(16U);  // bits per sample
    tag("data");
    put32(data_bytes);
    for (const auto sample : audio.samples) {
        put16(static_cast<std::uint16_t>(sample));
    }
    return wav;
}

DecodedAudio load_wav(const std::filesystem::path& path) {
    const auto d = read_file(path);
    const auto has_tag = [&](const std::size_t off, const char* tag) {
        return off + 4U <= d.size() && d[off] == tag[0] && d[off + 1U] == tag[1]
            && d[off + 2U] == tag[2] && d[off + 3U] == tag[3];
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
            data_size = chunk_size <= d.size() - body ? chunk_size
                                                      : d.size() - body;
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
            audio.samples.push_back(static_cast<std::int16_t>(
                d[data_off + i * 2U] | (d[data_off + i * 2U + 1U] << 8U)));
        }
    } else if (bits == 8U) {
        audio.samples.reserve(data_size);
        for (std::size_t i = 0U; i < data_size; ++i) {
            audio.samples.push_back(static_cast<std::int16_t>(
                (static_cast<int>(d[data_off + i]) - 128) << 8));
        }
    } else {
        throw std::runtime_error("unsupported WAV bit depth");
    }
    return audio;
}

}  // namespace khdays::assets
