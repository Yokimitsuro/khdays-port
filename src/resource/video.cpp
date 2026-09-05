#include "khdays/resource/video.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "khdays/vfs/filesystem.h"

namespace khdays::resource {

namespace {

std::uint16_t read16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
}

std::uint32_t read32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
           | (static_cast<std::uint32_t>(p[1]) << 8U)
           | (static_cast<std::uint32_t>(p[2]) << 16U)
           | (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("MobiClip: cannot open " + path.string());
    }
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error("MobiClip: cannot size " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream && !bytes.empty()) {
        throw std::runtime_error("MobiClip: short read on " + path.string());
    }
    return bytes;
}

std::optional<std::size_t> appended_data_size(
    const std::vector<std::uint8_t>& bytes) {
    for (std::size_t appended = 0; appended < 0x20U; appended += 4U) {
        if (bytes.size() < appended + 8U) {
            return std::nullopt;
        }
        const auto header_word = read32(bytes.data() + bytes.size() - appended - 8U);
        const auto header_size = static_cast<std::size_t>(header_word >> 24U);
        const auto compressed_size = static_cast<std::size_t>(header_word & 0x00ffffffU);
        if (header_size < 8U || header_size > bytes.size() - appended
            || compressed_size > bytes.size() - appended) {
            continue;
        }
        const auto padding_begin = bytes.size() - appended - header_size;
        const auto padding_end = bytes.size() - appended - 8U;
        if (std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(padding_begin),
                        bytes.begin() + static_cast<std::ptrdiff_t>(padding_end),
                        [](const std::uint8_t value) { return value == 0xffU; })) {
            return appended;
        }
    }
    return std::nullopt;
}

// Nintendo DS backward-LZ (BLZ/code compression). Overlay 24 is stored this
// way in NitroFS; decompression proceeds from the end toward the beginning.
std::vector<std::uint8_t> decompress_blz(
    const std::vector<std::uint8_t>& input) {
    const auto appended = appended_data_size(input);
    if (!appended.has_value()) {
        return input;
    }
    const auto core_size = input.size() - *appended;
    if (core_size < 8U) {
        throw std::runtime_error("MobiClip: invalid BLZ header");
    }
    const auto header_word = read32(input.data() + core_size - 8U);
    const auto extra_size = static_cast<std::size_t>(
        read32(input.data() + core_size - 4U));
    if (extra_size == 0U) {
        return input;
    }
    const auto header_size = static_cast<std::size_t>(header_word >> 24U);
    auto compressed_size = static_cast<std::size_t>(header_word & 0x00ffffffU);
    compressed_size = std::min(compressed_size, core_size);
    if (header_size > compressed_size || compressed_size > core_size) {
        throw std::runtime_error("MobiClip: invalid BLZ lengths");
    }
    const auto passthrough_size = core_size - compressed_size;
    const auto compressed_data_size = compressed_size - header_size;
    const auto output_size = core_size + extra_size - passthrough_size;
    std::vector<std::uint8_t> decoded(output_size);

    std::size_t written = 0;
    std::size_t consumed = 0;
    std::uint8_t flags = 0;
    std::uint8_t mask = 1;
    const auto byte_from_end = [&](const std::size_t index) -> std::uint8_t {
        if (index >= compressed_data_size) {
            throw std::runtime_error("MobiClip: truncated BLZ stream");
        }
        return input[passthrough_size + compressed_data_size - 1U - index];
    };

    while (written < output_size) {
        if (mask == 1U) {
            flags = byte_from_end(consumed++);
            mask = 0x80U;
        } else {
            mask >>= 1U;
        }
        if ((flags & mask) == 0U) {
            decoded[output_size - 1U - written++] = byte_from_end(consumed++);
            continue;
        }

        const auto first = byte_from_end(consumed++);
        const auto second = byte_from_end(consumed++);
        const auto length = static_cast<std::size_t>((first >> 4U) + 3U);
        auto distance = static_cast<std::size_t>(
            ((static_cast<unsigned int>(first & 0x0fU) << 8U) | second) + 3U);
        if (distance > written) {
            if (written < 2U) {
                throw std::runtime_error("MobiClip: invalid BLZ back-reference");
            }
            distance = 2U;
        }
        for (std::size_t i = 0; i < length && written < output_size; ++i) {
            const auto source_from_end = written - distance;
            decoded[output_size - 1U - written] =
                decoded[output_size - 1U - source_from_end];
            ++written;
        }
    }

    std::vector<std::uint8_t> result;
    result.reserve(passthrough_size + decoded.size() + *appended);
    result.insert(result.end(), input.begin(),
                  input.begin() + static_cast<std::ptrdiff_t>(passthrough_size));
    result.insert(result.end(), decoded.begin(), decoded.end());
    result.insert(result.end(),
                  input.begin() + static_cast<std::ptrdiff_t>(core_size), input.end());
    return result;
}

}  // namespace

khdays::assets::MobiClipCoefficientTables load_mobiclip_coefficient_tables() {
    const auto& root = khdays::vfs::data_root();
    if (root.empty()) {
        throw std::runtime_error("MobiClip: extracted-data root is not set");
    }
    const auto table_blob = read_file(root / "system" / "arm9_overlay_table.bin");
    constexpr std::size_t kOverlayEntrySize = 32U;
    constexpr std::uint32_t kDecoderOverlay = 24U;
    const std::uint8_t* entry = nullptr;
    for (std::size_t offset = 0; offset + kOverlayEntrySize <= table_blob.size();
         offset += kOverlayEntrySize) {
        if (read32(table_blob.data() + offset) == kDecoderOverlay) {
            entry = table_blob.data() + offset;
            break;
        }
    }
    if (entry == nullptr) {
        throw std::runtime_error("MobiClip: overlay 24 is absent from the overlay table");
    }
    const auto ram_address = read32(entry + 4U);
    const auto ram_size = static_cast<std::size_t>(read32(entry + 8U));
    const auto file_id = read32(entry + 24U);
    std::ostringstream name;
    name << "file_" << std::setw(5) << std::setfill('0') << file_id << ".bin";
    const auto compressed = read_file(root / "nitrofs" / "_unnamed" / name.str());
    const auto overlay = decompress_blz(compressed);
    if (overlay.size() < ram_size) {
        throw std::runtime_error("MobiClip: overlay 24 decompressed shorter than RAM size");
    }

    constexpr std::uint32_t kTableAddresses[2] = {0x0208a7c4U, 0x020886c4U};
    khdays::assets::MobiClipCoefficientTables tables{};
    for (std::size_t variant = 0; variant < 2U; ++variant) {
        if (kTableAddresses[variant] < ram_address) {
            throw std::runtime_error("MobiClip: coefficient table precedes overlay RAM");
        }
        const auto offset = static_cast<std::size_t>(
            kTableAddresses[variant] - ram_address);
        constexpr std::size_t kTableBytes = 0x2100U;
        if (offset + kTableBytes > overlay.size()) {
            throw std::runtime_error("MobiClip: coefficient table exceeds overlay 24");
        }
        for (std::size_t i = 0; i < 4096U; ++i) {
            tables[variant].lookup[i] = read16(overlay.data() + offset + i * 2U);
        }
        std::copy_n(overlay.data() + offset + 0x2000U, 256U,
                    tables[variant].residue.data());
    }
    return tables;
}

khdays::assets::ModsVideoDecoder load_mods_video(
    const std::string_view game_path) {
    return {khdays::vfs::read(game_path), load_mobiclip_coefficient_tables()};
}

}  // namespace khdays::resource
