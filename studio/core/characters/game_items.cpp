// The game's items a form's rule names (game_items.h).
#include "core/characters/game_items.h"

#include "core/files/containers.h"
#include "core/files/paths.h"

#include <cstdint>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace studio {

namespace {

constexpr std::size_t kRecord = 0x64;
constexpr int kItemsEntry = 0x15;
constexpr int kSynthesis = 2;
constexpr int kGears = 4;

std::vector<std::uint8_t> read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

// A list of the message database (u32 count, u32 ends[count], UTF-16 strings) as UTF-8, control codes left out.
std::vector<std::string> strings_of(const std::vector<std::uint8_t>& d) {
    std::vector<std::string> out;
    auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 | static_cast<std::uint32_t>(d[at + 3]) << 24);
    };
    if (d.size() < 4)
        return out;
    const std::uint32_t count = u32(0);
    const std::size_t data = (static_cast<std::size_t>(count) + 1) * 4;
    if (data > d.size())
        return out;
    std::size_t start = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t end = u32(4 + 4 * static_cast<std::size_t>(i));
        std::string s;
        for (std::size_t at = data + start; at + 1 < data + end && at + 1 < d.size(); at += 2) {
            const std::uint32_t c = d[at] | d[at + 1] << 8;
            if (c < 0x20)
                continue;  // its control codes, its NUL
            if (c < 0x80) {
                s += static_cast<char>(c);
            } else if (c < 0x800) {
                s += static_cast<char>(0xc0 | (c >> 6));
                s += static_cast<char>(0x80 | (c & 0x3f));
            } else {
                s += static_cast<char>(0xe0 | (c >> 12));
                s += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                s += static_cast<char>(0x80 | (c & 0x3f));
            }
        }
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        out.push_back(s);
        start = end;
    }
    return out;
}

bool entry_of(const fs::path& path, int entry, std::vector<std::uint8_t>* out, std::string* why) {
    P2File p2;
    std::string reason;
    if (!parse_p2(read_all(path), &p2, &reason)) {
        *why = u8(path) + ": " + reason;
        return false;
    }
    if (entry >= static_cast<int>(p2.entries.size())) {
        *why = u8(path) + ": no entry " + std::to_string(entry);
        return false;
    }
    *out = p2.entries[static_cast<std::size_t>(entry)].data;
    return true;
}

}  // namespace

std::vector<GameItem> game_items(const fs::path& nitrofs, const std::string& language, std::string* why) {
    std::vector<GameItem> items;
    std::vector<std::uint8_t> records, names;
    std::string reason;
    if (!entry_of(nitrofs / "db" / "db.p2", kItemsEntry, &records, &reason) ||
        !entry_of(nitrofs / "db" / path_u8("db_" + language + ".p2"), kItemsEntry, &names, &reason)) {
        if (why != nullptr)
            *why = reason;
        return items;
    }
    const std::vector<std::string> texts = strings_of(names);
    for (std::size_t at = 4, id = 1; at + kRecord <= records.size(); at += kRecord, ++id) {
        GameItem item;
        item.id = static_cast<int>(id);
        item.category = records[at];
        item.one_cell = static_cast<std::int8_t>(records[at + 8]) < 0;
        const std::size_t name = 2 * (id - 1);
        item.name = name < texts.size() ? texts[name] : std::string();
        if (!item.name.empty())
            items.push_back(item);
    }
    return items;
}

std::vector<GameItem> game_gears(const std::vector<GameItem>& items) {
    std::vector<GameItem> out;
    for (const auto& i : items)
        if (i.category == kGears && !i.one_cell)
            out.push_back(i);
    return out;
}

std::vector<GameItem> game_links(const std::vector<GameItem>& items) {
    std::vector<GameItem> out;
    for (const auto& i : items)
        if (i.category == kGears && i.one_cell)
            out.push_back(i);
    return out;
}

std::vector<GameItem> game_panels(const std::vector<GameItem>& items) {
    std::vector<GameItem> out;
    for (const auto& i : items)
        if (i.category != kSynthesis)
            out.push_back(i);
    return out;
}

}  // namespace studio
