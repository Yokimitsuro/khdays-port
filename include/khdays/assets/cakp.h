#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace khdays::assets {

struct ActionOperand final {
    std::uint16_t kind = 0U;
    std::uint16_t auxiliary = 0U;
    std::uint32_t value = 0U;
};

// One command consumed by Game_RunActionScript (func_02020e58). The packed
// halfword at command+2 stores the size in 32-bit words and an optional async
// callback slot in its upper five bits.
struct ActionInstruction final {
    std::size_t offset = 0U;
    std::uint8_t group = 0U;
    std::uint8_t command = 0U;
    std::uint8_t callback_slot = 0U;
    std::uint16_t word_count = 0U;
    std::vector<ActionOperand> operands;
    std::vector<std::uint8_t> payload;
};

struct CakpScript final {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

struct CakpArchive final {
    std::array<std::size_t, 8> section_counts{};
    std::vector<CakpScript> scripts;
};

// Decode the named section-1 scripts of an on-disk CAKP/KAPH-style archive.
// Its eight section tables store base-relative offsets until func_02025464
// relocates them on DS; this reader resolves them without mutating the input.
CakpArchive decode_cakp(const std::uint8_t* data, std::size_t size);
CakpArchive decode_cakp(const std::vector<std::uint8_t>& data);
CakpArchive decode_cakp(const std::filesystem::path& path);

// Split a script member (bytes after its u32 member size) using the exact
// packed-length rule from func_02020e58.
std::vector<ActionInstruction> decode_action_instructions(
    const std::uint8_t* data, std::size_t size);
std::vector<ActionInstruction> decode_action_instructions(
    const std::vector<std::uint8_t>& data);

}  // namespace khdays::assets
