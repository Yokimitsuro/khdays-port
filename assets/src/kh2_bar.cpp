#include "khdays/assets/kh2_bar.h"

#include <stdexcept>

namespace khdays::assets::kh2 {

bool is_bar(const std::uint8_t* data, std::size_t size, std::size_t at) {
    return data != nullptr && at <= size && size - at >= 16 && data[at] == 'B' && data[at + 1] == 'A' &&
           data[at + 2] == 'R' && data[at + 3] == 1;
}

std::vector<BarEntry> bar_entries(const std::uint8_t* data, std::size_t size, std::size_t at) {
    if (!is_bar(data, size, at)) {
        throw std::runtime_error("KH2 BAR: missing BAR header");
    }
    const auto word = [data](std::size_t p) {
        return std::uint32_t(data[p]) | (std::uint32_t(data[p + 1]) << 8) | (std::uint32_t(data[p + 2]) << 16) |
               (std::uint32_t(data[p + 3]) << 24);
    };
    const auto count = word(at + 4);
    if (count > (size - at - 16) / 16) {
        throw std::runtime_error("KH2 BAR: truncated entry table");
    }
    std::vector<BarEntry> entries;
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto p = at + 16 + std::size_t(i) * 16;
        const std::size_t offset = word(p + 8), length = word(p + 12);
        if (length != 0 && (offset > size - at || length > size - at - offset)) {
            throw std::runtime_error("KH2 BAR: entry extends past the archive");
        }
        std::string name;
        for (std::size_t c = 0; c < 4 && data[p + 4 + c] != 0; ++c) {
            name += static_cast<char>(data[p + 4 + c]);
        }
        entries.push_back({word(p), name, at + offset, length});
    }
    return entries;
}

}  // namespace khdays::assets::kh2
