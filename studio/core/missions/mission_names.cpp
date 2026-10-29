// The game's missions by name (mission_names.h).
#include "core/missions/mission_names.h"

#include "core/files/paths.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace studio {

namespace {

std::uint16_t u16_at(const std::vector<std::uint8_t>& d, std::size_t at) {
    return static_cast<std::uint16_t>(d[at] | (d[at + 1] << 8));
}

// UTF-16 (little end first) from `at` to its NUL or `end`, as UTF-8.
std::string utf16_text(const std::vector<std::uint8_t>& d, std::size_t at, std::size_t end) {
    std::string out;
    for (; at + 1 < end; at += 2) {
        std::uint32_t c = u16_at(d, at);
        if (c == 0)
            break;
        if (c >= 0xd800 && c < 0xdc00 && at + 3 < end) {  // a pair
            const std::uint32_t low = u16_at(d, at + 2);
            if (low >= 0xdc00 && low < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                at += 2;
            }
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

}  // namespace

std::map<int, GameMissionName> game_mission_names(const fs::path& nitrofs, const std::string& language) {
    std::map<int, GameMissionName> out;
    std::ifstream in(nitrofs.parent_path() / "decompressed" / "UI" / "cm" / path_u8("msl_" + language + ".msi"),
                     std::ios::binary);
    const std::vector<std::uint8_t> d{std::istreambuf_iterator<char>(in), {}};
    if (d.size() < 2)
        return out;
    // a u16 count, then records led by their u16 size: +2 the file, +4 the mission's number, +0xa the category,
    // +0xe the number shown, +0x23 the kind (4 Mission Mode's), from +0x30 its texts, its name first
    const std::size_t count = u16_at(d, 0);
    std::size_t at = 2;
    for (std::size_t i = 0; i < count && at + 2 <= d.size(); ++i) {
        const std::size_t size = u16_at(d, at);
        if (size < 0x32 || at + size > d.size())
            break;
        GameMissionName m;
        m.file = u16_at(d, at + 2);
        m.number = u16_at(d, at + 4);
        m.category = u16_at(d, at + 0xa);
        m.shown = u16_at(d, at + 0xe);
        m.mission_mode = d[at + 0x23] == 4;
        m.name = utf16_text(d, at + 0x30, at + size);
        const auto found = out.find(m.file);
        if (!m.name.empty() && (found == out.end() || (m.mission_mode && !found->second.mission_mode)))
            out[m.file] = m;
        at += size;
    }
    return out;
}

}  // namespace studio
