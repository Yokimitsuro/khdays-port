#include "khdays/assets/cakp.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

#include "khdays/assets/message.h"

namespace khdays::assets {

namespace {

std::uint16_t read_u16(
    const std::uint8_t* data,
    const std::size_t size,
    const std::size_t offset) {
    if (offset > size || size - offset < 2U) {
        throw std::runtime_error("CAKP: truncated u16");
    }
    return static_cast<std::uint16_t>(data[offset])
        | static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[offset + 1U]) << 8U);
}

std::uint32_t read_u32(
    const std::uint8_t* data,
    const std::size_t size,
    const std::size_t offset) {
    if (offset > size || size - offset < 4U) {
        throw std::runtime_error("CAKP: truncated u32");
    }
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(data[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("cannot open CAKP: " + path.string());
    }
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    std::vector<std::uint8_t> bytes(
        end > 0 ? static_cast<std::size_t>(end) : 0U);
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

std::vector<std::size_t> section_offsets(
    const std::uint8_t* data,
    const std::size_t size,
    const std::size_t section_offset) {
    const auto count = static_cast<std::size_t>(
        read_u32(data, size, section_offset));
    if (count > (size - section_offset - 4U) / 4U) {
        throw std::runtime_error("CAKP: section directory exceeds file");
    }
    std::vector<std::size_t> offsets;
    offsets.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const auto offset = static_cast<std::size_t>(
            read_u32(data, size, section_offset + 4U + index * 4U));
        if (offset >= size) {
            throw std::runtime_error("CAKP: member offset exceeds file");
        }
        offsets.push_back(offset);
    }
    return offsets;
}

std::string read_name(
    const std::uint8_t* data,
    const std::size_t size,
    const std::size_t offset) {
    if (offset >= size) {
        throw std::runtime_error("CAKP: name offset exceeds file");
    }
    std::size_t end = offset;
    while (end < size && data[end] != 0U) {
        ++end;
    }
    if (end == size) {
        throw std::runtime_error("CAKP: unterminated member name");
    }
    return std::string{
        reinterpret_cast<const char*>(data + offset), end - offset};
}

}  // namespace

CakpArchive decode_cakp(
    const std::uint8_t* data,
    const std::size_t size) {
    if (size < 0x28U || data[0] != 'C' || data[1] != 'A'
        || data[2] != 'K' || data[3] != 'P') {
        throw std::runtime_error("not a CAKP archive");
    }

    std::array<std::vector<std::size_t>, 8> sections;
    CakpArchive archive;
    for (std::size_t section = 0U; section < sections.size(); ++section) {
        const std::uint32_t encoded = read_u32(data, size, 8U + section * 4U);
        if (encoded == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        const auto offset = static_cast<std::size_t>(encoded);
        if (offset >= size) {
            throw std::runtime_error("CAKP: section offset exceeds file");
        }
        sections[section] = section_offsets(data, size, offset);
        archive.section_counts[section] = sections[section].size();
    }

    if (sections[0].empty() || sections[1].empty()) {
        return archive;
    }
    const std::size_t names = sections[0][0];
    const auto name_count = static_cast<std::size_t>(
        read_u16(data, size, names));
    if (name_count > (size - names - 2U) / 2U) {
        throw std::runtime_error("CAKP: name directory exceeds file");
    }
    if (name_count > sections[1].size()) {
        throw std::runtime_error("CAKP: more names than script members");
    }

    archive.scripts.reserve(name_count);
    for (std::size_t index = 0U; index < name_count; ++index) {
        const std::size_t relative = read_u16(
            data, size, names + 2U + index * 2U);
        const std::size_t name_offset = names + relative;
        const std::size_t member = sections[1][index];
        const std::size_t member_size = read_u32(data, size, member);
        if (member_size < 4U || member_size > size - member) {
            throw std::runtime_error("CAKP: script member exceeds file");
        }
        CakpScript script;
        script.name = read_name(data, size, name_offset);
        script.bytes.assign(
            data + member + 4U, data + member + member_size);
        archive.scripts.push_back(std::move(script));
    }
    return archive;
}

CakpArchive decode_cakp(const std::vector<std::uint8_t>& data) {
    return decode_cakp(data.data(), data.size());
}

CakpArchive decode_cakp(const std::filesystem::path& path) {
    return decode_cakp(read_file(path));
}

std::vector<ActionInstruction> decode_action_instructions(
    const std::uint8_t* data,
    const std::size_t size) {
    std::vector<ActionInstruction> instructions;
    std::size_t offset = 0U;
    while (offset < size) {
        if (size - offset < 4U) {
            throw std::runtime_error("action script: truncated command header");
        }
        const std::uint16_t packed = read_u16(data, size, offset + 2U);
        const std::uint16_t words = packed & 0x7ffU;
        if (words == 0U) {
            throw std::runtime_error("action script: zero-length command");
        }
        const std::size_t command_size = static_cast<std::size_t>(words) * 4U;
        if (command_size > size - offset) {
            throw std::runtime_error("action script: command exceeds member");
        }

        ActionInstruction instruction;
        instruction.offset = offset;
        instruction.group = data[offset];
        instruction.command = data[offset + 1U];
        instruction.callback_slot = static_cast<std::uint8_t>(packed >> 11U);
        instruction.word_count = words;
        instruction.payload.assign(
            data + offset + 4U, data + offset + command_size);
        for (std::size_t operand = 0U;
             operand + 8U <= instruction.payload.size(); operand += 8U) {
            ActionOperand decoded;
            decoded.kind = read_u16(
                instruction.payload.data(), instruction.payload.size(), operand);
            decoded.auxiliary = read_u16(
                instruction.payload.data(), instruction.payload.size(), operand + 2U);
            decoded.value = read_u32(
                instruction.payload.data(), instruction.payload.size(), operand + 4U);
            instruction.operands.push_back(decoded);
        }
        instructions.push_back(std::move(instruction));
        offset += command_size;
    }
    return instructions;
}

std::vector<ActionInstruction> decode_action_instructions(
    const std::vector<std::uint8_t>& data) {
    return decode_action_instructions(data.data(), data.size());
}

Ov012MovieScript decode_ov012_movie_script(
    const std::vector<std::uint8_t>& input) {
    std::vector<std::uint8_t> data = input;
    if (!data.empty() && (data[0] == 0x10U || data[0] == 0x11U)) {
        data = lz_decompress(data);
    }
    if (data.size() < 4U) {
        throw std::runtime_error("ov012 script: missing member size");
    }

    // func_02020b64 stores member + *member in the entry's +0x14 field;
    // ByteCode_ResolveOperand then adds every plain kind-2 value to it.
    const auto resource_base = static_cast<std::size_t>(
        read_u32(data.data(), data.size(), 0U));
    if (resource_base < 4U || resource_base > data.size()) {
        throw std::runtime_error("ov012 script: member exceeds package");
    }
    const auto commands = decode_action_instructions(
        data.data() + 4U, resource_base - 4U);

    const auto string_at = [&](const std::uint32_t relative) {
        const auto offset = resource_base + static_cast<std::size_t>(relative);
        if (offset >= data.size()) {
            throw std::runtime_error("ov012 script: string offset exceeds package");
        }
        std::size_t end = offset;
        while (end < data.size() && data[end] != 0U) {
            ++end;
        }
        if (end == data.size()) {
            throw std::runtime_error("ov012 script: unterminated string");
        }
        std::string text;
        text.reserve(end - offset);
        for (std::size_t i = offset; i < end; ++i) {
            // The subtitle renderer consumes 0x01..0x1f as layout/state
            // events. They are not characters and must not reach UTF-8 text.
            if (data[i] >= 0x20U) {
                text.push_back(static_cast<char>(data[i]));
            }
        }
        return text;
    };

    Ov012MovieScript result;
    int language = -1;
    std::array<std::uint32_t, 5> previous_end{};
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        const auto& command = commands[index];
        if (command.group == 3U && command.command == 2U
            && !command.operands.empty()
            && command.operands[0].kind == 2U) {
            result.movie_path = string_at(command.operands[0].value);
            if (!result.movie_path.empty() && result.movie_path.front() == '/') {
                result.movie_path.erase(result.movie_path.begin());
            }
            continue;
        }
        if (command.group == 0U && command.command == 5U) {
            ++language;
            continue;
        }
        if (language < 0 || language >= 5 || command.group != 3U
            || command.command != 1U || command.operands.empty()
            || command.operands[0].kind != 2U || index + 1U >= commands.size()) {
            continue;
        }
        const auto& wait = commands[index + 1U];
        if (wait.group != 3U || wait.command != 3U || wait.operands.empty()
            || wait.operands[0].kind != 1U) {
            throw std::runtime_error(
                "ov012 script: subtitle is not followed by a frame wait");
        }
        const auto end_frame = wait.operands[0].value;
        const auto slot = static_cast<std::size_t>(language);
        if (end_frame < previous_end[slot]) {
            throw std::runtime_error("ov012 script: subtitle frames go backwards");
        }
        result.subtitles[slot].push_back(MovieSubtitleCue{
            previous_end[slot], end_frame, string_at(command.operands[0].value)});
        previous_end[slot] = end_frame;
    }
    if (result.movie_path.empty()) {
        throw std::runtime_error("ov012 script: movie path is absent");
    }
    return result;
}

}  // namespace khdays::assets
