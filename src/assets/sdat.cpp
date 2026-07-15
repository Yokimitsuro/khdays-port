#include "khdays/assets/sdat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Metadata reader for Nintendo DS SDAT sound archives. Parses the container
// header and the SYMB (symbol) block to list the named contents by category.
// The full audio pipeline (sequence playback, banks, streams) is out of scope
// here; this only inventories what the archive contains.

namespace {

using ByteVector = std::vector<std::uint8_t>;

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
        stream.read(reinterpret_cast<char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    }
    return data;
}

std::string read_string(
    const ByteVector& d,
    const std::size_t start,
    const std::size_t limit) {
    std::string name;
    for (std::size_t i = start; i < limit && i < d.size(); ++i) {
        if (d[i] == 0U) {
            break;
        }
        name.push_back(static_cast<char>(d[i]));
    }
    return name;
}

// Read a flat SYMB name list at `record_off` (relative to the SYMB block).
// Entries are u32 offsets, also relative to the SYMB block, to NUL-terminated
// names; a zero offset means an unnamed entry.
std::vector<std::string> read_name_list(
    const ByteVector& data,
    const std::size_t symb_offset,
    const std::size_t symb_size,
    const std::uint32_t record_off) {
    std::vector<std::string> names;
    if (record_off == 0U || record_off + 4U > symb_size) {
        return names;
    }
    const auto base = symb_offset + record_off;
    const auto count = read_u32(data, base);
    if (count > 0xFFFFU) {
        return names;  // implausible; avoid runaway
    }
    names.reserve(count);
    for (std::uint32_t i = 0U; i < count; ++i) {
        const auto entry_off = base + 4U + static_cast<std::size_t>(i) * 4U;
        if (entry_off + 4U > symb_offset + symb_size) {
            break;
        }
        const auto name_off = read_u32(data, entry_off);
        if (name_off == 0U || name_off >= symb_size) {
            names.emplace_back();
            continue;
        }
        names.push_back(
            read_string(data, symb_offset + name_off, symb_offset + symb_size));
    }
    return names;
}

}  // namespace

namespace khdays::assets {

SdatInventory read_sdat_inventory(const std::filesystem::path& input_path) {
    const auto data = read_file(input_path);
    if (data.size() < 0x40U || data[0] != 'S' || data[1] != 'D'
        || data[2] != 'A' || data[3] != 'T') {
        throw std::runtime_error("resource is not an SDAT file");
    }

    const auto symb_offset = static_cast<std::size_t>(read_u32(data, 0x10U));
    const auto symb_size = static_cast<std::size_t>(read_u32(data, 0x14U));
    if (symb_offset == 0U || symb_size < 0x40U
        || symb_offset + symb_size > data.size()
        || data[symb_offset] != 'S' || data[symb_offset + 1U] != 'Y'
        || data[symb_offset + 2U] != 'M' || data[symb_offset + 3U] != 'B') {
        throw std::runtime_error("SDAT has no usable SYMB block");
    }

    // Eight category record offsets at SYMB + 0x08.
    std::array<std::uint32_t, 8> record_offsets{};
    for (std::size_t i = 0U; i < 8U; ++i) {
        record_offsets[i] = read_u32(data, symb_offset + 0x08U + i * 4U);
    }

    const auto list = [&](const std::size_t category) {
        return read_name_list(
            data, symb_offset, symb_size, record_offsets[category]);
    };

    SdatInventory inventory;
    inventory.sequences = list(0);
    inventory.sequence_archives = list(1);
    inventory.banks = list(2);
    inventory.wave_archives = list(3);
    inventory.players = list(4);
    inventory.groups = list(5);
    inventory.stream_players = list(6);
    inventory.streams = list(7);
    return inventory;
}

}  // namespace khdays::assets
