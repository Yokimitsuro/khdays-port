#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/mdl0.h"

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
    const std::uint32_t relative_offset) {
    constexpr std::size_t dictionary_size = 40U;
    std::vector<std::uint8_t> data(dictionary_size, 0U);

    data[1] = 1U;
    write_u16(data, 2U, dictionary_size);
    write_u16(data, 4U, 8U);
    write_u16(data, 6U, 16U);
    write_u16(data, 16U, 4U);
    write_u16(data, 18U, 8U);
    write_u32(data, 20U, relative_offset);

    for (
        std::size_t index = 0U;
        index < name.size() && index < 15U;
        ++index) {
        data[24U + index] =
            static_cast<std::uint8_t>(name[index]);
    }

    return data;
}

std::vector<std::uint8_t> make_test_bmd0() {
    constexpr std::size_t mdl0_offset_in_file = 0x14U;
    constexpr std::size_t model_offset = 0x30U;
    constexpr std::size_t model_size = 0xE0U;
    constexpr std::size_t mdl0_size = model_offset + model_size;

    std::vector<std::uint8_t> mdl0(mdl0_size, 0U);
    write_magic(mdl0, 0U, "MDL0");
    write_u32(mdl0, 4U, mdl0_size);

    const auto model_dictionary =
        make_dictionary("sample_model", model_offset);
    std::copy(
        model_dictionary.begin(),
        model_dictionary.end(),
        mdl0.begin() + 8);

    const auto model = model_offset;
    constexpr std::uint32_t render_commands_offset = 0x80U;
    constexpr std::uint32_t materials_offset = 0x40U;
    constexpr std::uint32_t meshes_offset = 0x70U;
    constexpr std::uint32_t inverse_bind_offset = model_size;

    write_u32(mdl0, model + 0x00U, model_size);
    write_u32(mdl0, model + 0x04U, render_commands_offset);
    write_u32(mdl0, model + 0x08U, materials_offset);
    write_u32(mdl0, model + 0x0CU, meshes_offset);
    write_u32(mdl0, model + 0x10U, inverse_bind_offset);

    mdl0[model + 0x17U] = 1U;
    mdl0[model + 0x18U] = 1U;
    mdl0[model + 0x19U] = 1U;

    write_u32(mdl0, model + 0x1CU, 0x1000U);
    write_u32(mdl0, model + 0x20U, 0x1000U);
    write_u16(mdl0, model + 0x24U, 1U);
    write_u16(mdl0, model + 0x26U, 1U);
    write_u16(mdl0, model + 0x28U, 1U);
    write_u16(mdl0, model + 0x2AU, 0U);
    write_u16(mdl0, model + 0x32U, 0x1000U);
    write_u16(mdl0, model + 0x34U, 0x1000U);
    write_u16(mdl0, model + 0x36U, 0x1000U);

    const auto material_list = model + materials_offset;
    write_u16(mdl0, material_list, 44U);
    write_u16(mdl0, material_list + 2U, 84U);

    const auto material_dictionary =
        make_dictionary("material", 0x80U);
    std::copy(
        material_dictionary.begin(),
        material_dictionary.end(),
        mdl0.begin()
            + static_cast<std::ptrdiff_t>(material_list + 4U));

    const auto mesh_list = model + meshes_offset;
    const auto mesh_dictionary =
        make_dictionary("mesh", 0x40U);
    std::copy(
        mesh_dictionary.begin(),
        mesh_dictionary.end(),
        mdl0.begin() + static_cast<std::ptrdiff_t>(mesh_list));

    const auto mesh = mesh_list + 0x40U;
    write_u16(mdl0, mesh + 0U, 0U);
    write_u16(mdl0, mesh + 2U, 16U);
    write_u32(mdl0, mesh + 4U, 0U);
    write_u32(mdl0, mesh + 8U, 0x10U);
    write_u32(mdl0, mesh + 12U, 16U);

    const auto commands = mesh + 0x10U;

    // Packet: BEGIN_VTXS, TEXCOORD, VTX_10, END_VTXS.
    write_u32(mdl0, commands, 0x41242240U);
    write_u32(mdl0, commands + 4U, 0U);
    write_u32(mdl0, commands + 8U, 0U);
    write_u32(mdl0, commands + 12U, 0U);

    const auto file_size = mdl0_offset_in_file + mdl0.size();
    std::vector<std::uint8_t> file(file_size, 0U);

    write_magic(file, 0U, "BMD0");
    write_u16(file, 4U, 0xFEFFU);
    write_u16(file, 6U, 2U);
    write_u32(file, 8U, file_size);
    write_u16(file, 0x0CU, 0x10U);
    write_u16(file, 0x0EU, 1U);
    write_u32(file, 0x10U, mdl0_offset_in_file);

    std::copy(
        mdl0.begin(),
        mdl0.end(),
        file.begin()
            + static_cast<std::ptrdiff_t>(mdl0_offset_in_file));

    return file;
}

}  // namespace

int main() {
    const auto path =
        std::filesystem::temp_directory_path()
        / "khdays_mdl0_test.nsbmd";

    try {
        const auto data = make_test_bmd0();

        {
            std::ofstream stream{path, std::ios::binary};
            stream.write(
                reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        }

        const auto file = khdays::assets::inspect_mdl0(path);

        if (file.models.size() != 1U) {
            throw std::runtime_error("expected one model");
        }

        const auto& model = file.models.front();
        if (
            model.name != "sample_model"
            || model.material_names.size() != 1U
            || model.material_names.front() != "material"
            || model.meshes.size() != 1U
            || model.meshes.front().name != "mesh") {
            throw std::runtime_error("parsed MDL0 names are incorrect");
        }

        const auto& commands = model.meshes.front().gpu_commands;
        if (
            commands.packet_count != 1U
            || commands.vertex_command_count != 1U
            || commands.opcode_counts.at("BEGIN_VTXS") != 1U
            || commands.opcode_counts.at("TEXCOORD") != 1U
            || commands.opcode_counts.at("VTX_10") != 1U
            || commands.opcode_counts.at("END_VTXS") != 1U) {
            throw std::runtime_error("GPU command summary is incorrect");
        }

        std::filesystem::remove(path);
        std::cout << "MDL0 parser test passed" << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << "MDL0 parser test failed: " << error.what() << '\n';
        return 1;
    }
}
