#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/cakp.h"

namespace {

void put_u16(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::uint16_t value) {
    data[offset] = static_cast<std::uint8_t>(value);
    data[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
}

void put_u32(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        data[offset + byte] =
            static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

void expect(const bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    try {
        std::vector<std::uint8_t> data(0xa0U, 0U);
        std::copy_n("CAKP", 4U, data.begin());
        put_u32(data, 8U, 0x28U);
        put_u32(data, 12U, 0x30U);
        for (std::size_t section = 2U; section < 8U; ++section) {
            put_u32(data, 8U + section * 4U, 0xffffffffU);
        }

        put_u32(data, 0x28U, 1U);
        put_u32(data, 0x2cU, 0x38U);
        put_u32(data, 0x30U, 1U);
        put_u32(data, 0x34U, 0x50U);
        put_u16(data, 0x38U, 1U);
        put_u16(data, 0x3aU, 4U);
        std::copy_n("_i", 3U, data.begin() + 0x3cU);

        // One 20-byte instruction plus the member's four-byte size.
        put_u32(data, 0x50U, 24U);
        data[0x54U] = 2U;
        data[0x55U] = 25U;
        put_u16(data, 0x56U, static_cast<std::uint16_t>(5U | (3U << 11U)));
        put_u16(data, 0x58U, 1U);
        put_u32(data, 0x5cU, 12U);
        put_u16(data, 0x60U, 4U);
        put_u32(data, 0x64U, 34U);

        const auto archive = khdays::assets::decode_cakp(data);
        expect(archive.section_counts[0] == 1U
                   && archive.section_counts[1] == 1U,
               "section counts");
        expect(archive.scripts.size() == 1U
                   && archive.scripts[0].name == "_i",
               "named script");
        const auto instructions =
            khdays::assets::decode_action_instructions(
                archive.scripts[0].bytes);
        expect(instructions.size() == 1U, "instruction count");
        const auto& command = instructions[0];
        expect(command.group == 2U && command.command == 25U,
               "opcode pair");
        expect(command.callback_slot == 3U && command.word_count == 5U,
               "packed command metadata");
        expect(command.operands.size() == 2U
                   && command.operands[0].kind == 1U
                   && command.operands[0].value == 12U
                   && command.operands[1].kind == 4U
                   && command.operands[1].value == 34U,
               "operand decode");

        bool threw = false;
        try {
            std::vector<std::uint8_t> bad{0U, 0U, 0U, 0U};
            (void)khdays::assets::decode_action_instructions(bad);
        } catch (const std::exception&) {
            threw = true;
        }
        expect(threw, "zero-length command rejected");

        std::cout << "CAKP test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CAKP test failed: " << error.what() << '\n';
        return 1;
    }
}
