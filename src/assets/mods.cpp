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

constexpr std::size_t kHeaderSize = 0x24;
}  // namespace

ModsInfo parse_mods_header(const std::uint8_t* data, const std::size_t size) {
    if (data == nullptr || size < kHeaderSize) {
        throw std::runtime_error("MODS: data smaller than the header");
    }
    // The game checks the version tag, not the magic — mirror that.
    if (data[4] != 'N' || data[5] != '3') {
        throw std::runtime_error("MODS: unsupported version tag (expected N3)");
    }
    ModsInfo info;
    info.frame_count = read32(data + 0x08);
    info.width = static_cast<int>(read32(data + 0x0c));
    info.height = static_cast<int>(read32(data + 0x10));
    info.audio_coding = read16(data + 0x18);
    info.audio_channels = read16(data + 0x1a);
    info.audio_rate = static_cast<int>(read32(data + 0x1c));
    return info;
}

ModsInfo parse_mods_header(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error("MODS: cannot open " + path.string());
    }
    std::vector<std::uint8_t> head(kHeaderSize);
    file.read(reinterpret_cast<char*>(head.data()),
              static_cast<std::streamsize>(head.size()));
    if (file.gcount() < static_cast<std::streamsize>(kHeaderSize)) {
        throw std::runtime_error("MODS: short read on " + path.string());
    }
    return parse_mods_header(head.data(), head.size());
}

}  // namespace khdays::assets
