#include "khdays/assets/mdl0.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using ByteVector = std::vector<std::uint8_t>;

struct SectionView final {
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct Dictionary final {
    std::vector<std::string> names;
    std::vector<ByteVector> entries;
};

std::uint16_t read_u16(
    const ByteVector& data,
    const std::size_t offset,
    const std::string_view label) {
    if (offset > data.size() || data.size() - offset < 2U) {
        throw std::runtime_error(
            std::string{label} + ": cannot read u16 at offset "
            + std::to_string(offset));
    }

    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[offset])
        | (static_cast<std::uint16_t>(data[offset + 1U]) << 8U));
}

std::int16_t read_s16(
    const ByteVector& data,
    const std::size_t offset,
    const std::string_view label) {
    return static_cast<std::int16_t>(read_u16(data, offset, label));
}

std::uint32_t read_u32(
    const ByteVector& data,
    const std::size_t offset,
    const std::string_view label) {
    if (offset > data.size() || data.size() - offset < 4U) {
        throw std::runtime_error(
            std::string{label} + ": cannot read u32 at offset "
            + std::to_string(offset));
    }

    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(data[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

std::int32_t read_s32(
    const ByteVector& data,
    const std::size_t offset,
    const std::string_view label) {
    return static_cast<std::int32_t>(read_u32(data, offset, label));
}

ByteVector read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error(
            "cannot open model file: " + path.string());
    }

    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0) {
        throw std::runtime_error(
            "cannot determine model file size: " + path.string());
    }

    const auto size = static_cast<std::size_t>(end);
    stream.seekg(0, std::ios::beg);

    ByteVector data(size);
    if (size > 0U) {
        stream.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(size));
    }

    if (!stream && size > 0U) {
        throw std::runtime_error(
            "cannot read complete model file: " + path.string());
    }

    return data;
}

bool has_magic(
    const ByteVector& data,
    const std::size_t offset,
    const std::string_view magic) {
    if (offset > data.size() || data.size() - offset < magic.size()) {
        return false;
    }

    return std::equal(
        magic.begin(),
        magic.end(),
        data.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::unordered_map<std::string, SectionView> parse_sections(
    const ByteVector& data) {
    if (data.size() < 0x10U || !has_magic(data, 0U, "BMD0")) {
        throw std::runtime_error("resource is not a BMD0/NSBMD file");
    }

    const auto section_count = read_u16(data, 0x0EU, "section count");
    if (section_count == 0U || section_count > 64U) {
        throw std::runtime_error("invalid Nitro section count");
    }

    const auto table_end =
        0x10U + static_cast<std::size_t>(section_count) * 4U;
    if (table_end > data.size()) {
        throw std::runtime_error("Nitro section table exceeds the file");
    }

    std::unordered_map<std::string, SectionView> sections;

    for (std::uint16_t index = 0U; index < section_count; ++index) {
        const auto section_offset = static_cast<std::size_t>(
            read_u32(
                data,
                0x10U + static_cast<std::size_t>(index) * 4U,
                "section offset"));

        if (section_offset > data.size() || data.size() - section_offset < 8U) {
            throw std::runtime_error("Nitro section offset exceeds the file");
        }

        const auto section_size = static_cast<std::size_t>(
            read_u32(data, section_offset + 4U, "section size"));

        if (
            section_size < 8U
            || section_size > data.size() - section_offset) {
            throw std::runtime_error("Nitro section size exceeds the file");
        }

        const std::string magic{
            reinterpret_cast<const char*>(data.data() + section_offset),
            4U,
        };
        sections[magic] = SectionView{section_offset, section_size};
    }

    return sections;
}

ByteVector copy_section(
    const ByteVector& data,
    const SectionView& section) {
    return ByteVector{
        data.begin() + static_cast<std::ptrdiff_t>(section.offset),
        data.begin()
            + static_cast<std::ptrdiff_t>(section.offset + section.size),
    };
}

Dictionary parse_dictionary(
    const ByteVector& data,
    const std::size_t dictionary_offset,
    const std::string_view label) {
    if (
        dictionary_offset > data.size()
        || data.size() - dictionary_offset < 8U) {
        throw std::runtime_error(
            std::string{label} + " dictionary offset is invalid");
    }

    const auto count =
        static_cast<std::size_t>(data[dictionary_offset + 1U]);
    const auto dictionary_size = static_cast<std::size_t>(
        read_u16(data, dictionary_offset + 2U, "dictionary size"));
    const auto entry_header_relative = static_cast<std::size_t>(
        read_u16(data, dictionary_offset + 6U, "entry header offset"));

    if (count == 0U) {
        return {};
    }

    if (
        dictionary_size < 8U
        || dictionary_size > data.size() - dictionary_offset) {
        throw std::runtime_error(
            std::string{label} + " dictionary exceeds its section");
    }

    const auto dictionary_end = dictionary_offset + dictionary_size;
    const auto entry_header = dictionary_offset + entry_header_relative;

    if (entry_header > dictionary_end || dictionary_end - entry_header < 4U) {
        throw std::runtime_error(
            std::string{label} + " entry header exceeds dictionary");
    }

    const auto unit_size = static_cast<std::size_t>(
        read_u16(data, entry_header, "dictionary entry size"));
    const auto names_relative = static_cast<std::size_t>(
        read_u16(data, entry_header + 2U, "dictionary names offset"));

    if (unit_size == 0U) {
        throw std::runtime_error(
            std::string{label} + " dictionary entry size is zero");
    }

    const auto entries_start = entry_header + 4U;
    const auto entries_size = count * unit_size;
    const auto names_start = entry_header + names_relative;
    const auto names_size = count * 16U;

    if (
        entries_start > dictionary_end
        || entries_size > dictionary_end - entries_start
        || names_start > dictionary_end
        || names_size > dictionary_end - names_start) {
        throw std::runtime_error(
            std::string{label} + " entries exceed dictionary");
    }

    Dictionary dictionary;
    dictionary.names.reserve(count);
    dictionary.entries.reserve(count);

    for (std::size_t index = 0U; index < count; ++index) {
        const auto entry_start = entries_start + index * unit_size;
        dictionary.entries.emplace_back(
            data.begin() + static_cast<std::ptrdiff_t>(entry_start),
            data.begin()
                + static_cast<std::ptrdiff_t>(entry_start + unit_size));

        const auto name_start = names_start + index * 16U;
        std::string name;

        for (std::size_t character = 0U; character < 16U; ++character) {
            const auto value = data[name_start + character];
            if (value == 0U) {
                break;
            }
            name.push_back(static_cast<char>(value));
        }

        dictionary.names.push_back(std::move(name));
    }

    return dictionary;
}

std::uint32_t read_entry_u32(
    const ByteVector& entry,
    const std::string_view label) {
    if (entry.size() < 4U) {
        throw std::runtime_error(
            std::string{label} + " dictionary entry is smaller than u32");
    }

    return static_cast<std::uint32_t>(entry[0])
        | (static_cast<std::uint32_t>(entry[1]) << 8U)
        | (static_cast<std::uint32_t>(entry[2]) << 16U)
        | (static_cast<std::uint32_t>(entry[3]) << 24U);
}

float fixed_20_12(const std::int32_t value) {
    return static_cast<float>(value) / 4096.0F;
}

float fixed_4_12(const std::int16_t value) {
    return static_cast<float>(value) / 4096.0F;
}

std::string gpu_opcode_name(const std::uint8_t opcode) {
    switch (opcode) {
        case 0x00:
            return "NOP";
        case 0x10:
            return "MTX_MODE";
        case 0x11:
            return "MTX_PUSH";
        case 0x12:
            return "MTX_POP";
        case 0x13:
            return "MTX_STORE";
        case 0x14:
            return "MTX_RESTORE";
        case 0x15:
            return "MTX_IDENTITY";
        case 0x16:
            return "MTX_LOAD_4X4";
        case 0x17:
            return "MTX_LOAD_4X3";
        case 0x18:
            return "MTX_MULT_4X4";
        case 0x19:
            return "MTX_MULT_4X3";
        case 0x1A:
            return "MTX_MULT_3X3";
        case 0x1B:
            return "MTX_SCALE";
        case 0x1C:
            return "MTX_TRANS";
        case 0x20:
            return "COLOR";
        case 0x21:
            return "NORMAL";
        case 0x22:
            return "TEXCOORD";
        case 0x23:
            return "VTX_16";
        case 0x24:
            return "VTX_10";
        case 0x25:
            return "VTX_XY";
        case 0x26:
            return "VTX_XZ";
        case 0x27:
            return "VTX_YZ";
        case 0x28:
            return "VTX_DIFF";
        case 0x29:
            return "POLYGON_ATTR";
        case 0x2A:
            return "TEXIMAGE_PARAM";
        case 0x2B:
            return "PLTT_BASE";
        case 0x30:
            return "DIF_AMB";
        case 0x31:
            return "SPE_EMI";
        case 0x32:
            return "LIGHT_VECTOR";
        case 0x33:
            return "LIGHT_COLOR";
        case 0x34:
            return "SHININESS";
        case 0x40:
            return "BEGIN_VTXS";
        case 0x41:
            return "END_VTXS";
        case 0x50:
            return "SWAP_BUFFERS";
        case 0x60:
            return "VIEWPORT";
        case 0x70:
            return "BOX_TEST";
        case 0x71:
            return "POS_TEST";
        case 0x72:
            return "VEC_TEST";
        default: {
            std::ostringstream stream;
            stream
                << "UNKNOWN_0x"
                << std::uppercase
                << std::hex
                << std::setw(2)
                << std::setfill('0')
                << static_cast<int>(opcode);
            return stream.str();
        }
    }
}

std::size_t gpu_parameter_words(const std::uint8_t opcode) {
    switch (opcode) {
        case 0x00:
        case 0x11:
        case 0x15:
        case 0x41:
            return 0U;

        case 0x10:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2A:
        case 0x2B:
        case 0x30:
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x40:
        case 0x50:
        case 0x60:
        case 0x72:
            return 1U;

        case 0x23:
        case 0x71:
            return 2U;

        case 0x1B:
        case 0x1C:
        case 0x70:
            return 3U;

        case 0x1A:
            return 9U;
        case 0x17:
        case 0x19:
            return 12U;
        case 0x16:
        case 0x18:
            return 16U;
        case 0x34:
            return 32U;

        default:
            throw std::runtime_error(
                "unsupported Nintendo DS GPU opcode "
                + gpu_opcode_name(opcode));
    }
}

bool is_vertex_opcode(const std::uint8_t opcode) {
    return opcode >= 0x23U && opcode <= 0x28U;
}

khdays::assets::GpuCommandSummary inspect_gpu_commands(
    const ByteVector& data,
    const std::size_t command_offset,
    const std::size_t command_length) {
    if (
        command_offset > data.size()
        || command_length > data.size() - command_offset) {
        throw std::runtime_error("mesh GPU command list exceeds MDL0");
    }

    khdays::assets::GpuCommandSummary summary;
    std::size_t cursor = command_offset;
    const auto end = command_offset + command_length;

    while (cursor < end) {
        if (end - cursor < 4U) {
            throw std::runtime_error(
                "mesh GPU command packet is truncated");
        }

        const auto opcode_word =
            read_u32(data, cursor, "GPU opcode packet");
        cursor += 4U;
        ++summary.packet_count;

        for (int byte_index = 0; byte_index < 4; ++byte_index) {
            const auto opcode = static_cast<std::uint8_t>(
                (opcode_word >> (byte_index * 8)) & 0xFFU);
            const auto name = gpu_opcode_name(opcode);
            const auto parameter_words = gpu_parameter_words(opcode);
            const auto parameter_bytes = parameter_words * 4U;

            if (parameter_bytes > end - cursor) {
                throw std::runtime_error(
                    "GPU parameters exceed mesh command list for " + name);
            }

            cursor += parameter_bytes;
            ++summary.command_count;
            ++summary.opcode_counts[name];

            if (is_vertex_opcode(opcode)) {
                ++summary.vertex_command_count;
            }
        }
    }

    if (cursor != end) {
        throw std::runtime_error(
            "mesh GPU parser did not end at the declared command length");
    }

    return summary;
}

khdays::assets::MeshInfo parse_mesh(
    const ByteVector& mdl0,
    const std::size_t mesh_list_offset,
    const std::string& name,
    const std::uint32_t relative_offset) {
    const auto mesh_offset =
        mesh_list_offset + static_cast<std::size_t>(relative_offset);

    if (mesh_offset > mdl0.size() || mdl0.size() - mesh_offset < 16U) {
        throw std::runtime_error("mesh header exceeds MDL0");
    }

    const auto header_size = read_u16(
        mdl0,
        mesh_offset + 2U,
        "mesh header size");
    if (header_size != 16U) {
        throw std::runtime_error("unexpected mesh header size");
    }

    const auto command_relative = read_u32(
        mdl0,
        mesh_offset + 8U,
        "mesh command offset");
    const auto command_length = read_u32(
        mdl0,
        mesh_offset + 12U,
        "mesh command length");

    const auto command_offset =
        mesh_offset + static_cast<std::size_t>(command_relative);

    khdays::assets::MeshInfo mesh;
    mesh.name = name;
    mesh.command_offset = command_relative;
    mesh.command_length = command_length;
    mesh.gpu_commands = inspect_gpu_commands(
        mdl0,
        command_offset,
        command_length);
    return mesh;
}

khdays::assets::ModelInfo parse_model(
    const ByteVector& mdl0,
    const std::string& name,
    const std::uint32_t model_relative_offset) {
    const auto model_offset =
        static_cast<std::size_t>(model_relative_offset);

    if (model_offset > mdl0.size() || mdl0.size() - model_offset < 0x40U) {
        throw std::runtime_error("model header exceeds MDL0");
    }

    khdays::assets::ModelInfo model;
    model.name = name;
    model.model_size = read_u32(mdl0, model_offset, "model size");
    model.render_commands_offset = read_u32(
        mdl0,
        model_offset + 0x04U,
        "render command offset");
    model.materials_offset = read_u32(
        mdl0,
        model_offset + 0x08U,
        "materials offset");
    model.meshes_offset = read_u32(
        mdl0,
        model_offset + 0x0CU,
        "meshes offset");
    model.inverse_bind_matrices_offset = read_u32(
        mdl0,
        model_offset + 0x10U,
        "inverse bind matrices offset");

    if (
        model.model_size < 0x40U
        || model.model_size > mdl0.size() - model_offset) {
        throw std::runtime_error("model size exceeds MDL0");
    }

    model.bone_matrix_count = mdl0[model_offset + 0x17U];
    model.material_count = mdl0[model_offset + 0x18U];
    model.mesh_count = mdl0[model_offset + 0x19U];

    model.up_scale = fixed_20_12(
        read_s32(mdl0, model_offset + 0x1CU, "up scale"));
    model.down_scale = fixed_20_12(
        read_s32(mdl0, model_offset + 0x20U, "down scale"));

    model.vertex_count = read_u16(
        mdl0,
        model_offset + 0x24U,
        "vertex count");
    model.polygon_count = read_u16(
        mdl0,
        model_offset + 0x26U,
        "polygon count");
    model.triangle_count = read_u16(
        mdl0,
        model_offset + 0x28U,
        "triangle count");
    model.quad_count = read_u16(
        mdl0,
        model_offset + 0x2AU,
        "quad count");

    model.bounding_x = fixed_4_12(
        read_s16(mdl0, model_offset + 0x2CU, "bounding x"));
    model.bounding_y = fixed_4_12(
        read_s16(mdl0, model_offset + 0x2EU, "bounding y"));
    model.bounding_z = fixed_4_12(
        read_s16(mdl0, model_offset + 0x30U, "bounding z"));
    model.bounding_width = fixed_4_12(
        read_s16(mdl0, model_offset + 0x32U, "bounding width"));
    model.bounding_height = fixed_4_12(
        read_s16(mdl0, model_offset + 0x34U, "bounding height"));
    model.bounding_depth = fixed_4_12(
        read_s16(mdl0, model_offset + 0x36U, "bounding depth"));

    const auto materials_offset =
        model_offset + static_cast<std::size_t>(model.materials_offset);
    if (materials_offset > mdl0.size() || mdl0.size() - materials_offset < 4U) {
        throw std::runtime_error("material list exceeds MDL0");
    }

    const auto material_dictionary = parse_dictionary(
        mdl0,
        materials_offset + 4U,
        "material");
    model.material_names = material_dictionary.names;

    const auto mesh_list_offset =
        model_offset + static_cast<std::size_t>(model.meshes_offset);
    const auto mesh_dictionary = parse_dictionary(
        mdl0,
        mesh_list_offset,
        "mesh");

    if (
        mesh_dictionary.names.size()
        != mesh_dictionary.entries.size()) {
        throw std::runtime_error("mesh dictionary is inconsistent");
    }

    model.meshes.reserve(mesh_dictionary.names.size());

    for (
        std::size_t index = 0U;
        index < mesh_dictionary.names.size();
        ++index) {
        model.meshes.push_back(
            parse_mesh(
                mdl0,
                mesh_list_offset,
                mesh_dictionary.names[index],
                read_entry_u32(
                    mesh_dictionary.entries[index],
                    "mesh")));
    }

    if (model.material_names.size() != model.material_count) {
        throw std::runtime_error(
            "material dictionary count does not match model header");
    }

    if (model.meshes.size() != model.mesh_count) {
        throw std::runtime_error(
            "mesh dictionary count does not match model header");
    }

    return model;
}

}  // namespace

namespace khdays::assets {

ModelFileInfo inspect_mdl0(const std::filesystem::path& input_path) {
    const auto file = read_file(input_path);
    const auto sections = parse_sections(file);
    const auto iterator = sections.find("MDL0");

    if (iterator == sections.end()) {
        throw std::runtime_error("BMD0 contains no MDL0 section");
    }

    const auto mdl0 = copy_section(file, iterator->second);
    if (mdl0.size() < 8U || !has_magic(mdl0, 0U, "MDL0")) {
        throw std::runtime_error("invalid MDL0 section");
    }

    const auto model_dictionary = parse_dictionary(
        mdl0,
        8U,
        "model");

    if (
        model_dictionary.names.size()
        != model_dictionary.entries.size()) {
        throw std::runtime_error("model dictionary is inconsistent");
    }

    ModelFileInfo file_info;
    file_info.source_path = input_path;
    file_info.models.reserve(model_dictionary.names.size());

    for (
        std::size_t index = 0U;
        index < model_dictionary.names.size();
        ++index) {
        file_info.models.push_back(
            parse_model(
                mdl0,
                model_dictionary.names[index],
                read_entry_u32(
                    model_dictionary.entries[index],
                    "model")));
    }

    return file_info;
}

std::string format_model_report(const ModelFileInfo& file_info) {
    std::ostringstream stream;
    stream << "File: " << file_info.source_path.string() << '\n';
    stream << "Models: " << file_info.models.size() << "\n\n";

    for (std::size_t model_index = 0U;
         model_index < file_info.models.size();
         ++model_index) {
        const auto& model = file_info.models[model_index];

        stream
            << "Model " << model_index << ": " << model.name << '\n'
            << "  Size: " << model.model_size << " bytes\n"
            << "  Bones/matrices: "
            << static_cast<unsigned int>(model.bone_matrix_count) << '\n'
            << "  Materials: "
            << static_cast<unsigned int>(model.material_count) << '\n'
            << "  Meshes: "
            << static_cast<unsigned int>(model.mesh_count) << '\n'
            << "  Vertices: " << model.vertex_count << '\n'
            << "  Polygons: " << model.polygon_count
            << " (triangles " << model.triangle_count
            << ", quads " << model.quad_count << ")\n"
            << "  Bounding box: origin("
            << model.bounding_x << ", "
            << model.bounding_y << ", "
            << model.bounding_z << ") size("
            << model.bounding_width << ", "
            << model.bounding_height << ", "
            << model.bounding_depth << ")\n"
            << "  Materials:\n";

        for (const auto& material : model.material_names) {
            stream << "    - " << material << '\n';
        }

        stream << "  Meshes:\n";

        for (const auto& mesh : model.meshes) {
            stream
                << "    - " << mesh.name
                << ": " << mesh.command_length << " command bytes, "
                << mesh.gpu_commands.packet_count << " packets, "
                << mesh.gpu_commands.vertex_command_count
                << " vertex commands\n";

            stream << "      Opcodes:";
            for (const auto& [name, count] :
                 mesh.gpu_commands.opcode_counts) {
                stream << ' ' << name << '=' << count;
            }
            stream << '\n';
        }

        stream << '\n';
    }

    return stream.str();
}

}  // namespace khdays::assets
