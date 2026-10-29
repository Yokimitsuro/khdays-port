#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace khdays::assets::kh2 {

// "BAR\x01", u32 count, 8 reserved bytes, then 16-byte entries:
// u32 type, four-character name, u32 relative offset, u32 size.
struct BarEntry final {
    std::uint32_t type = 0;
    std::string name;
    std::size_t offset = 0;  // absolute within the supplied byte buffer
    std::size_t size = 0;
};

bool is_bar(const std::uint8_t* data, std::size_t size, std::size_t at = 0);
std::vector<BarEntry> bar_entries(const std::uint8_t* data, std::size_t size, std::size_t at = 0);

}  // namespace khdays::assets::kh2
