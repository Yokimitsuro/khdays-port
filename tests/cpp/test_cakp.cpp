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

void append_u16(std::vector<std::uint8_t>& data, const std::uint16_t value) {
    data.push_back(static_cast<std::uint8_t>(value));
    data.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& data, const std::uint32_t value) {
    for (std::size_t byte = 0U; byte < 4U; ++byte) {
        data.push_back(static_cast<std::uint8_t>(value >> (byte * 8U)));
    }
}

void append_command(std::vector<std::uint8_t>& data, const std::uint8_t group,
                    const std::uint8_t command,
                    const std::vector<std::uint8_t>& payload) {
    expect((payload.size() + 4U) % 4U == 0U, "aligned command payload");
    data.push_back(group);
    data.push_back(command);
    append_u16(data, static_cast<std::uint16_t>((payload.size() + 4U) / 4U));
    data.insert(data.end(), payload.begin(), payload.end());
}

std::vector<std::uint8_t> operand(const std::uint16_t kind,
                                  const std::uint32_t value) {
    std::vector<std::uint8_t> result;
    append_u16(result, kind);
    append_u16(result, 0U);
    append_u32(result, value);
    return result;
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

        // A minimal ov012 raw-member package. Offsets are deliberately
        // relative to the end of the instruction member, not its first byte.
        std::vector<std::uint8_t> movie(4U, 0U);
        auto open = operand(2U, 0U);
        const auto unused = operand(0U, 0U);
        open.insert(open.end(), unused.begin(), unused.end());
        open.insert(open.end(), unused.begin(), unused.end());
        append_command(movie, 3U, 2U, open);
        append_command(movie, 0U, 5U, std::vector<std::uint8_t>(8U, 0U));
        const auto line_command_offset = movie.size();
        append_command(movie, 3U, 1U, operand(2U, 0U));
        append_command(movie, 3U, 3U, operand(1U, 31U));
        append_command(movie, 3U, 4U, {});
        append_command(movie, 0U, 3U, {});
        const auto resource_base = movie.size();
        put_u32(movie, 0U, static_cast<std::uint32_t>(resource_base));
        const std::string path = "/mv/802.mods";
        movie.insert(movie.end(), path.begin(), path.end());
        movie.push_back(0U);
        const auto text_offset = movie.size() - resource_base;
        movie.push_back(1U);  // renderer control: reset, not visible text
        const std::string line = "Pitiful Heartless,";
        movie.insert(movie.end(), line.begin(), line.end());
        movie.push_back(0U);
        put_u32(movie, line_command_offset + 8U,
                static_cast<std::uint32_t>(text_offset));

        const auto decoded_movie =
            khdays::assets::decode_ov012_movie_script(movie);
        expect(decoded_movie.movie_path == "mv/802.mods", "ov012 movie path");
        expect(decoded_movie.subtitles[0].size() == 1U,
               "ov012 English cue count");
        expect(decoded_movie.subtitles[0][0].start_frame == 0U
                   && decoded_movie.subtitles[0][0].end_frame == 31U
                   && decoded_movie.subtitles[0][0].text == line,
               "ov012 cue base, timing, and control filtering");

        std::cout << "CAKP test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CAKP test failed: " << error.what() << '\n';
        return 1;
    }
}
