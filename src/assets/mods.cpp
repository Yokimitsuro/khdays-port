#include "khdays/assets/mods.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace khdays::assets {

namespace {
std::uint16_t read16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t read32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
           | (static_cast<std::uint32_t>(p[2]) << 16)
           | (static_cast<std::uint32_t>(p[3]) << 24);
}

constexpr std::size_t kHeaderSize = 0x30;
}  // namespace

ModsInfo parse_mods_header(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size < kHeaderSize) {
        throw std::runtime_error("MODS: data smaller than the header");
    }
    if (std::memcmp(data, "MODS", 4) != 0) {
        throw std::runtime_error("MODS: invalid magic");
    }
    if (data[4] != 'N' || data[5] != '3') {
        throw std::runtime_error("MODS: unsupported version tag (expected N3)");
    }
    if (read16(data + 0x06) != 0x0aU) {
        throw std::runtime_error("MODS: unsupported video codec");
    }
    ModsInfo info;
    info.frame_count = read32(data + 0x08);
    info.width = static_cast<int>(read32(data + 0x0c));
    info.height = static_cast<int>(read32(data + 0x10));
    info.fps_fixed = read32(data + 0x14);
    info.audio_coding = read16(data + 0x18);
    info.audio_channels = read16(data + 0x1a);
    info.audio_rate = static_cast<int>(read32(data + 0x1c));
    info.largest_frame = read32(data + 0x20);
    info.audio_info_offset = read32(data + 0x24);
    info.key_table_offset = read32(data + 0x28);
    info.key_frame_count = read32(data + 0x2c);

    std::size_t offset = kHeaderSize;
    for (;;) {
        if (offset + 4U > size) {
            throw std::runtime_error("MODS: unterminated N3 parameter list");
        }
        const bool end = data[offset] == 'H' && data[offset + 1U] == 'E';
        const auto parameter_size =
            static_cast<std::size_t>(read16(data + offset + 2U)) * 4U;
        offset += 4U;
        if (parameter_size > size - offset) {
            throw std::runtime_error("MODS: truncated N3 parameter block");
        }
        offset += parameter_size;
        if (end) {
            break;
        }
    }
    info.packet_data_offset = offset;
    return info;
}

ModsInfo parse_mods_header(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error("MODS: cannot open " + path.string());
    }
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < static_cast<std::streamoff>(kHeaderSize)) {
        throw std::runtime_error("MODS: short read on " + path.string());
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!file) {
        throw std::runtime_error("MODS: short read on " + path.string());
    }
    return parse_mods_header(data.data(), data.size());
}

}  // namespace khdays::assets
