#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/tex0.h"

namespace {

void write_u16(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::uint16_t value) {
    data.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
    data.at(offset + 1U) =
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::uint32_t value) {
    data.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
    data.at(offset + 1U) =
        static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    data.at(offset + 2U) =
        static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    data.at(offset + 3U) =
        static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void write_magic(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::string& magic) {
    for (std::size_t index = 0U; index < magic.size(); ++index) {
        data.at(offset + index) =
            static_cast<std::uint8_t>(magic[index]);
    }
}

std::vector<std::uint8_t> make_dictionary(
    const std::string& name,
    const std::vector<std::uint8_t>& entry) {
    constexpr std::size_t count = 1U;
    const auto unit_size = entry.size();
    constexpr std::size_t nodes_size = (count + 1U) * 4U;
    constexpr std::size_t entry_header_offset = 8U + nodes_size;
    const auto names_relative_offset = 4U + count * unit_size;
    const auto dictionary_size =
        entry_header_offset
        + names_relative_offset
        + count * 16U;

    std::vector<std::uint8_t> data(dictionary_size, 0U);
    data[1] = 1U;
    write_u16(
        data,
        2U,
        static_cast<std::uint16_t>(dictionary_size));
    write_u16(data, 4U, 8U);
    write_u16(
        data,
        6U,
        static_cast<std::uint16_t>(entry_header_offset));

    write_u16(
        data,
        entry_header_offset,
        static_cast<std::uint16_t>(unit_size));
    write_u16(
        data,
        entry_header_offset + 2U,
        static_cast<std::uint16_t>(names_relative_offset));

    const auto entries_start = entry_header_offset + 4U;
    std::copy(
        entry.begin(),
        entry.end(),
        data.begin() + static_cast<std::ptrdiff_t>(entries_start));

    const auto names_start =
        entry_header_offset + names_relative_offset;
    for (
        std::size_t index = 0U;
        index < name.size() && index < 15U;
        ++index) {
        data[names_start + index] =
            static_cast<std::uint8_t>(name[index]);
    }

    return data;
}

std::vector<std::uint8_t> make_test_bmd0() {
    const std::uint32_t texture_parameter = 4U << 26U;

    std::vector<std::uint8_t> texture_entry(8U, 0U);
    write_u32(texture_entry, 0U, texture_parameter);

    std::vector<std::uint8_t> palette_entry(4U, 0U);

    const auto texture_dictionary =
        make_dictionary("sample", texture_entry);
    const auto palette_dictionary =
        make_dictionary("sample_pl", palette_entry);

    constexpr std::size_t texture_dictionary_offset = 0x3CU;
    const auto palette_dictionary_offset =
        texture_dictionary_offset + texture_dictionary.size();
    const auto texture_data_offset =
        (palette_dictionary_offset + palette_dictionary.size() + 7U)
        & ~std::size_t{7U};

    std::vector<std::uint8_t> texture_data(64U, 0U);
    for (std::size_t index = 0U; index < texture_data.size(); ++index) {
        texture_data[index] =
            static_cast<std::uint8_t>(index);
    }

    const auto palette_data_offset =
        texture_data_offset + texture_data.size();
    std::vector<std::uint8_t> palette_data(128U, 0U);

    for (std::size_t index = 0U; index < 64U; ++index) {
        const auto channel =
            static_cast<std::uint16_t>(index & 0x1FU);
        const auto color = static_cast<std::uint16_t>(
            channel | (channel << 5U) | (channel << 10U));
        write_u16(palette_data, index * 2U, color);
    }

    const auto tex0_size =
        palette_data_offset + palette_data.size();
    std::vector<std::uint8_t> tex0(tex0_size, 0U);
    write_magic(tex0, 0U, "TEX0");
    write_u32(
        tex0,
        4U,
        static_cast<std::uint32_t>(tex0.size()));
    write_u16(
        tex0,
        0x0CU,
        static_cast<std::uint16_t>(texture_data.size() / 8U));
    write_u16(
        tex0,
        0x0EU,
        static_cast<std::uint16_t>(texture_dictionary_offset));
    write_u32(
        tex0,
        0x14U,
        static_cast<std::uint32_t>(texture_data_offset));
    write_u16(
        tex0,
        0x30U,
        static_cast<std::uint16_t>(palette_data.size() / 8U));
    write_u32(
        tex0,
        0x34U,
        static_cast<std::uint32_t>(palette_dictionary_offset));
    write_u32(
        tex0,
        0x38U,
        static_cast<std::uint32_t>(palette_data_offset));

    std::copy(
        texture_dictionary.begin(),
        texture_dictionary.end(),
        tex0.begin()
            + static_cast<std::ptrdiff_t>(texture_dictionary_offset));
    std::copy(
        palette_dictionary.begin(),
        palette_dictionary.end(),
        tex0.begin()
            + static_cast<std::ptrdiff_t>(palette_dictionary_offset));
    std::copy(
        texture_data.begin(),
        texture_data.end(),
        tex0.begin()
            + static_cast<std::ptrdiff_t>(texture_data_offset));
    std::copy(
        palette_data.begin(),
        palette_data.end(),
        tex0.begin()
            + static_cast<std::ptrdiff_t>(palette_data_offset));

    std::vector<std::uint8_t> mdl0(8U, 0U);
    write_magic(mdl0, 0U, "MDL0");
    write_u32(mdl0, 4U, 8U);

    constexpr std::size_t section_count = 2U;
    constexpr std::size_t section_table_end =
        0x10U + section_count * 4U;
    constexpr std::size_t mdl0_offset = section_table_end;
    const auto tex0_offset = mdl0_offset + mdl0.size();
    const auto file_size = tex0_offset + tex0.size();

    std::vector<std::uint8_t> file(file_size, 0U);
    write_magic(file, 0U, "BMD0");
    write_u16(file, 4U, 0xFEFFU);
    write_u16(file, 6U, 2U);
    write_u32(
        file,
        8U,
        static_cast<std::uint32_t>(file_size));
    write_u16(file, 0x0CU, 0x10U);
    write_u16(
        file,
        0x0EU,
        static_cast<std::uint16_t>(section_count));
    write_u32(
        file,
        0x10U,
        static_cast<std::uint32_t>(mdl0_offset));
    write_u32(
        file,
        0x14U,
        static_cast<std::uint32_t>(tex0_offset));

    std::copy(
        mdl0.begin(),
        mdl0.end(),
        file.begin() + static_cast<std::ptrdiff_t>(mdl0_offset));
    std::copy(
        tex0.begin(),
        tex0.end(),
        file.begin() + static_cast<std::ptrdiff_t>(tex0_offset));

    return file;
}

}  // namespace

int main() {
    const auto path =
        std::filesystem::temp_directory_path()
        / "khdays_tex0_test.nsbmd";

    try {
        const auto data = make_test_bmd0();

        {
            std::ofstream stream{path, std::ios::binary};
            stream.write(
                reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        }

        const auto names =
            khdays::assets::list_tex0_textures(path);
        if (names.size() != 1U || names.front() != "sample") {
            throw std::runtime_error("unexpected TEX0 texture list");
        }

        const auto texture =
            khdays::assets::load_tex0_texture(path, "sample");

        if (
            texture.width != 8
            || texture.height != 8
            || texture.format_name != "PLTT256"
            || texture.rgba.size() != 8U * 8U * 4U) {
            throw std::runtime_error("decoded texture metadata is incorrect");
        }

        std::filesystem::remove(path);
        std::cout << "TEX0 parser test passed" << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << "TEX0 parser test failed: " << error.what() << '\n';
        return 1;
    }
}
