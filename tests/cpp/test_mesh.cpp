#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/mesh.h"

namespace {

void write_u16(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::uint16_t value) {
    data.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
    data.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::uint32_t value) {
    data.at(offset) = static_cast<std::uint8_t>(value & 0xFFU);
    data.at(offset + 1U) = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    data.at(offset + 2U) = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    data.at(offset + 3U) = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void write_magic(
    std::vector<std::uint8_t>& data,
    const std::size_t offset,
    const std::string& magic) {
    for (std::size_t index = 0U; index < magic.size(); ++index) {
        data.at(offset + index) = static_cast<std::uint8_t>(magic[index]);
    }
}

// Build a Nitro name dictionary with a single entry, matching the layout the
// decoder expects: a 4-byte relative offset followed by a 16-byte name.
std::vector<std::uint8_t> make_dictionary(
    const std::string& name,
    const std::uint32_t relative_offset) {
    constexpr std::size_t dictionary_size = 40U;
    std::vector<std::uint8_t> data(dictionary_size, 0U);

    data[1] = 1U;                            // entry count
    write_u16(data, 2U, dictionary_size);
    write_u16(data, 4U, 8U);
    write_u16(data, 6U, 16U);                // entry header offset
    write_u16(data, 16U, 4U);                // entry unit size
    write_u16(data, 18U, 8U);                // names offset
    write_u32(data, 20U, relative_offset);   // the single entry

    for (std::size_t index = 0U; index < name.size() && index < 15U; ++index) {
        data[24U + index] = static_cast<std::uint8_t>(name[index]);
    }

    return data;
}

// Synthesize a BMD0 with one model whose render command stream draws a single
// piece: a triangle from three VTX_16 vertices at (1,0,0), (0,1,0), (0,0,1).
// The model has no bones, so the matrix palette is the identity and rest-pose
// positions equal the raw positions.
std::vector<std::uint8_t> make_triangle_bmd0() {
    constexpr std::size_t mdl0_offset_in_file = 0x14U;
    constexpr std::size_t model_offset = 0x30U;    // within the MDL0 section
    constexpr std::size_t render_off = 0x80U;      // relative to the model
    constexpr std::size_t materials_off = 0x90U;
    constexpr std::size_t pieces_off = 0xB0U;
    constexpr std::size_t mesh_relative = 0x30U;   // relative to the mesh list
    constexpr std::size_t mesh_list_offset = model_offset + pieces_off;
    constexpr std::size_t mesh_offset = mesh_list_offset + mesh_relative;
    constexpr std::size_t command_relative = 0x10U;
    constexpr std::size_t command_offset = mesh_offset + command_relative;
    constexpr std::size_t command_length = 9U * 4U;  // 9 packed words
    constexpr std::size_t mdl0_size = command_offset + command_length;

    std::vector<std::uint8_t> mdl0(mdl0_size, 0U);
    write_magic(mdl0, 0U, "MDL0");
    write_u32(mdl0, 4U, static_cast<std::uint32_t>(mdl0_size));

    const auto model_dictionary =
        make_dictionary("tri_model", static_cast<std::uint32_t>(model_offset));
    std::copy(model_dictionary.begin(), model_dictionary.end(), mdl0.begin() + 8);

    write_u32(mdl0, model_offset + 0x04U, static_cast<std::uint32_t>(render_off));
    write_u32(mdl0, model_offset + 0x08U, static_cast<std::uint32_t>(materials_off));
    write_u32(mdl0, model_offset + 0x0CU, static_cast<std::uint32_t>(pieces_off));
    write_u32(mdl0, model_offset + 0x10U, 0U);       // inverse binds (unused)
    mdl0[model_offset + 0x17U] = 0U;                 // num objects (bones)
    write_u32(mdl0, model_offset + 0x1CU, 0x1000U);  // up scale = 1.0 (20.12)
    write_u16(mdl0, model_offset + 0x24U, 3U);       // header vertex count

    // Render commands: Draw piece 0, then End.
    mdl0[model_offset + render_off + 0U] = 0x05U;
    mdl0[model_offset + render_off + 1U] = 0x00U;
    mdl0[model_offset + render_off + 2U] = 0x01U;

    const auto mesh_dictionary =
        make_dictionary("tri_mesh", static_cast<std::uint32_t>(mesh_relative));
    std::copy(
        mesh_dictionary.begin(),
        mesh_dictionary.end(),
        mdl0.begin() + static_cast<std::ptrdiff_t>(mesh_list_offset));

    write_u16(mdl0, mesh_offset + 2U, 16U);  // header size
    write_u32(mdl0, mesh_offset + 8U, static_cast<std::uint32_t>(command_relative));
    write_u32(mdl0, mesh_offset + 12U, static_cast<std::uint32_t>(command_length));

    // Command stream: BEGIN_VTXS(triangles), 3x VTX_16, END_VTXS.
    std::size_t cursor = command_offset;
    write_u32(mdl0, cursor, 0x23232340U);  // [BEGIN_VTXS, VTX_16, VTX_16, VTX_16]
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000000U);  // BEGIN_VTXS param: mode 0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00001000U);  // v0 p0: X=4096, Y=0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000000U);  // v0 p1: Z=0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x10000000U);  // v1 p0: X=0, Y=4096
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000000U);  // v1 p1: Z=0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000000U);  // v2 p0: X=0, Y=0
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00001000U);  // v2 p1: Z=4096
    cursor += 4U;
    write_u32(mdl0, cursor, 0x00000041U);  // [END_VTXS, NOP, NOP, NOP]

    // Wrap the MDL0 in a minimal single-section BMD0 container.
    const auto file_size = mdl0_offset_in_file + mdl0.size();
    std::vector<std::uint8_t> file(file_size, 0U);
    write_magic(file, 0U, "BMD0");
    write_u16(file, 4U, 0xFEFFU);
    write_u16(file, 6U, 2U);
    write_u32(file, 8U, static_cast<std::uint32_t>(file_size));
    write_u16(file, 0x0CU, 0x10U);
    write_u16(file, 0x0EU, 1U);  // one section
    write_u32(file, 0x10U, static_cast<std::uint32_t>(mdl0_offset_in_file));

    std::copy(
        mdl0.begin(),
        mdl0.end(),
        file.begin() + static_cast<std::ptrdiff_t>(mdl0_offset_in_file));

    return file;
}

bool close_to(const float value, const float expected) {
    return std::fabs(value - expected) < 1.0e-4F;
}

}  // namespace

int main() {
    const auto path =
        std::filesystem::temp_directory_path() / "khdays_mesh_test.nsbmd";

    try {
        const auto data = make_triangle_bmd0();
        {
            std::ofstream stream{path, std::ios::binary};
            stream.write(
                reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        }

        const auto model = khdays::assets::decode_model_geometry(path);

        if (model.name != "tri_model") {
            throw std::runtime_error("unexpected model name");
        }
        if (model.header_vertex_count != 3U) {
            throw std::runtime_error("unexpected header vertex count");
        }
        if (model.meshes.size() != 1U
            || model.meshes.front().name != "tri_mesh") {
            throw std::runtime_error("unexpected mesh list");
        }

        const auto& mesh = model.meshes.front();

        // The decoded vertex count must equal the header vertex count.
        if (mesh.vertices.size() != model.header_vertex_count) {
            throw std::runtime_error(
                "decoded vertex count does not match the header");
        }
        if (mesh.indices.size() != 3U) {
            throw std::runtime_error("expected exactly one triangle");
        }
        if (mesh.indices[0] != 0U
            || mesh.indices[1] != 1U
            || mesh.indices[2] != 2U) {
            throw std::runtime_error("unexpected triangle winding");
        }

        const auto& v = mesh.vertices;
        if (!close_to(v[0].position[0], 1.0F)
            || !close_to(v[0].position[1], 0.0F)
            || !close_to(v[0].position[2], 0.0F)
            || !close_to(v[1].position[1], 1.0F)
            || !close_to(v[2].position[2], 1.0F)) {
            throw std::runtime_error("decoded positions are incorrect");
        }

        // With no bones the palette is the identity, so rest-pose positions
        // equal the raw positions.
        if (model.palette.empty()) {
            throw std::runtime_error("model palette is empty");
        }
        const auto posed = khdays::assets::posed_position(model, v[0]);
        if (!close_to(posed[0], 1.0F)
            || !close_to(posed[1], 0.0F)
            || !close_to(posed[2], 0.0F)) {
            throw std::runtime_error("posed position is incorrect");
        }

        const auto obj = khdays::assets::to_wavefront_obj(model);
        if (obj.find("\nf ") == std::string::npos
            || obj.find("tri_mesh") == std::string::npos) {
            throw std::runtime_error("OBJ export is missing geometry");
        }

        std::filesystem::remove(path);
        std::cout << "Mesh decoder test passed" << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << "Mesh decoder test failed: " << error.what() << '\n';
        return 1;
    }
}
