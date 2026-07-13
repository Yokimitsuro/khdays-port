#include "khdays/assets/mesh.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Neutral-mesh decoder for Nintendo DS MDL0 geometry. The file/section/
// dictionary parsing mirrors the self-contained style of tex0.cpp and mdl0.cpp;
// the geometry interpreter walks the packed GPU command stream and reconstructs
// positions, texture coordinates, vertex colors and triangle indices.
//
// Command semantics follow the Nintendo DS geometry engine as documented by
// GBATEK (fixed-point formats, VTX_* variants, and BEGIN_VTXS primitive modes).

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
        throw std::runtime_error("cannot open model file: " + path.string());
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

        if (section_size < 8U || section_size > data.size() - section_offset) {
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

ByteVector copy_section(const ByteVector& data, const SectionView& section) {
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

    const auto count = static_cast<std::size_t>(data[dictionary_offset + 1U]);
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

// Sign-extend the low `bits` of `value` to a signed integer.
std::int32_t sign_extend(const std::uint32_t value, const unsigned bits) {
    const std::uint32_t mask = (1U << bits) - 1U;
    const std::uint32_t masked = value & mask;
    const std::uint32_t sign_bit = 1U << (bits - 1U);
    if ((masked & sign_bit) != 0U) {
        return static_cast<std::int32_t>(masked | ~mask);
    }
    return static_cast<std::int32_t>(masked);
}

float expand_channel_5_to_8(const std::uint32_t value5) {
    const auto channel = value5 & 0x1FU;
    return static_cast<float>((channel << 3U) | (channel >> 2U));
}

// Number of 32-bit parameter words consumed by a GPU command. Covers the
// opcodes that appear in MDL0 mesh command lists; unexpected opcodes throw so
// malformed streams cannot desynchronize the parser.
std::size_t gpu_parameter_words(const std::uint8_t opcode) {
    switch (opcode) {
        case 0x00:  // NOP
        case 0x11:  // MTX_PUSH
        case 0x15:  // MTX_IDENTITY
        case 0x41:  // END_VTXS
            return 0U;

        case 0x10:  // MTX_MODE
        case 0x12:  // MTX_POP
        case 0x13:  // MTX_STORE
        case 0x14:  // MTX_RESTORE
        case 0x20:  // COLOR
        case 0x21:  // NORMAL
        case 0x22:  // TEXCOORD
        case 0x24:  // VTX_10
        case 0x25:  // VTX_XY
        case 0x26:  // VTX_XZ
        case 0x27:  // VTX_YZ
        case 0x28:  // VTX_DIFF
        case 0x29:  // POLYGON_ATTR
        case 0x2A:  // TEXIMAGE_PARAM
        case 0x2B:  // PLTT_BASE
        case 0x30:  // DIF_AMB
        case 0x31:  // SPE_EMI
        case 0x32:  // LIGHT_VECTOR
        case 0x33:  // LIGHT_COLOR
        case 0x40:  // BEGIN_VTXS
        case 0x50:  // SWAP_BUFFERS
        case 0x60:  // VIEWPORT
        case 0x72:  // VEC_TEST
            return 1U;

        case 0x23:  // VTX_16
        case 0x71:  // POS_TEST
            return 2U;

        case 0x1B:  // MTX_SCALE
        case 0x1C:  // MTX_TRANS
        case 0x70:  // BOX_TEST
            return 3U;

        case 0x1A:  // MTX_MULT_3X3
            return 9U;
        case 0x17:  // MTX_LOAD_4X3
        case 0x19:  // MTX_MULT_4X3
            return 12U;
        case 0x16:  // MTX_LOAD_4X4
        case 0x18:  // MTX_MULT_4X4
            return 16U;
        case 0x34:  // SHININESS
            return 32U;

        default:
            throw std::runtime_error("unsupported Nintendo DS GPU opcode");
    }
}

// Primitive assembler: accumulates vertex indices emitted between BEGIN_VTXS
// and END_VTXS, then turns them into triangles according to the primitive mode.
struct PrimitiveGroup final {
    int mode = -1;                      // 0 tris, 1 quads, 2 tri strip, 3 quad strip
    std::vector<std::uint32_t> vertices;
};

void flush_primitive_group(
    const PrimitiveGroup& group,
    std::vector<std::uint32_t>& indices) {
    const auto& v = group.vertices;
    const auto count = v.size();

    switch (group.mode) {
        case 0:  // separate triangles
            for (std::size_t i = 0U; i + 2U < count; i += 3U) {
                indices.insert(indices.end(), {v[i], v[i + 1U], v[i + 2U]});
            }
            break;

        case 1:  // separate quads -> two triangles each
            for (std::size_t i = 0U; i + 3U < count; i += 4U) {
                indices.insert(indices.end(), {v[i], v[i + 1U], v[i + 2U]});
                indices.insert(indices.end(), {v[i], v[i + 2U], v[i + 3U]});
            }
            break;

        case 2:  // triangle strip (alternating winding)
            for (std::size_t i = 2U; i < count; ++i) {
                if ((i & 1U) == 0U) {
                    indices.insert(
                        indices.end(), {v[i - 2U], v[i - 1U], v[i]});
                } else {
                    indices.insert(
                        indices.end(), {v[i - 1U], v[i - 2U], v[i]});
                }
            }
            break;

        case 3:  // quad strip: each new pair closes a quad with the previous pair
            for (std::size_t i = 0U; i + 3U < count; i += 2U) {
                indices.insert(
                    indices.end(), {v[i], v[i + 1U], v[i + 3U]});
                indices.insert(
                    indices.end(), {v[i], v[i + 3U], v[i + 2U]});
            }
            break;

        default:
            break;  // no active primitive
    }
}

khdays::assets::NeutralMesh decode_mesh_geometry(
    const ByteVector& mdl0,
    const std::size_t command_offset,
    const std::size_t command_length,
    const std::string& name,
    const float model_scale) {
    if (
        command_offset > mdl0.size()
        || command_length > mdl0.size() - command_offset) {
        throw std::runtime_error("mesh GPU command list exceeds MDL0");
    }

    khdays::assets::NeutralMesh mesh;
    mesh.name = name;

    // Running vertex state, updated by COLOR/TEXCOORD/MTX_RESTORE and the
    // various vertex commands. Positions are stored in units of 1/4096.
    std::array<std::int32_t, 3> position{0, 0, 0};
    std::array<std::int32_t, 2> texel{0, 0};
    std::array<std::uint8_t, 4> color{255U, 255U, 255U, 255U};
    int matrix_id = -1;
    float texture_width = 0.0F;
    float texture_height = 0.0F;

    PrimitiveGroup group;

    const auto emit_vertex = [&]() {
        khdays::assets::NeutralVertex vertex;
        vertex.position = {
            fixed_20_12(position[0]) * model_scale,
            fixed_20_12(position[1]) * model_scale,
            fixed_20_12(position[2]) * model_scale,
        };

        // DS texture coordinates are 1.11.4 signed texels. Normalize to 0..1
        // when the current texture size is known.
        const float s = static_cast<float>(texel[0]) / 16.0F;
        const float t = static_cast<float>(texel[1]) / 16.0F;
        vertex.texcoord = {
            texture_width > 0.0F ? s / texture_width : s,
            texture_height > 0.0F ? t / texture_height : t,
        };
        vertex.color = color;
        vertex.matrix_id = matrix_id;

        const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(vertex);
        group.vertices.push_back(index);
    };

    std::size_t cursor = command_offset;
    const auto end = command_offset + command_length;

    while (cursor < end) {
        if (end - cursor < 4U) {
            throw std::runtime_error("mesh GPU command packet is truncated");
        }

        const auto opcode_word = read_u32(mdl0, cursor, "GPU opcode packet");
        cursor += 4U;

        std::array<std::uint8_t, 4> opcodes{
            static_cast<std::uint8_t>(opcode_word & 0xFFU),
            static_cast<std::uint8_t>((opcode_word >> 8U) & 0xFFU),
            static_cast<std::uint8_t>((opcode_word >> 16U) & 0xFFU),
            static_cast<std::uint8_t>((opcode_word >> 24U) & 0xFFU),
        };

        for (const auto opcode : opcodes) {
            const auto words = gpu_parameter_words(opcode);
            const auto parameter_bytes = words * 4U;
            if (parameter_bytes > end - cursor) {
                throw std::runtime_error(
                    "GPU parameters exceed mesh command list");
            }

            const auto param = [&](const std::size_t which) {
                return read_u32(
                    mdl0, cursor + which * 4U, "GPU command parameter");
            };

            switch (opcode) {
                case 0x14: {  // MTX_RESTORE
                    matrix_id =
                        static_cast<int>(param(0U) & 0x1FU);
                    break;
                }
                case 0x20: {  // COLOR
                    const auto value = param(0U);
                    color = {
                        static_cast<std::uint8_t>(
                            expand_channel_5_to_8(value)),
                        static_cast<std::uint8_t>(
                            expand_channel_5_to_8(value >> 5U)),
                        static_cast<std::uint8_t>(
                            expand_channel_5_to_8(value >> 10U)),
                        255U,
                    };
                    break;
                }
                case 0x22: {  // TEXCOORD
                    const auto value = param(0U);
                    texel[0] = sign_extend(value & 0xFFFFU, 16U);
                    texel[1] = sign_extend((value >> 16U) & 0xFFFFU, 16U);
                    break;
                }
                case 0x2A: {  // TEXIMAGE_PARAM
                    const auto value = param(0U);
                    const auto s_size = (value >> 20U) & 0x07U;
                    const auto t_size = (value >> 23U) & 0x07U;
                    texture_width = static_cast<float>(8U << s_size);
                    texture_height = static_cast<float>(8U << t_size);
                    break;
                }
                case 0x23: {  // VTX_16
                    const auto p0 = param(0U);
                    const auto p1 = param(1U);
                    position[0] = sign_extend(p0 & 0xFFFFU, 16U);
                    position[1] = sign_extend((p0 >> 16U) & 0xFFFFU, 16U);
                    position[2] = sign_extend(p1 & 0xFFFFU, 16U);
                    emit_vertex();
                    break;
                }
                case 0x24: {  // VTX_10 (each coord 4.6 signed -> scale to 4.12)
                    const auto value = param(0U);
                    position[0] = sign_extend(value, 10U) * 64;
                    position[1] = sign_extend(value >> 10U, 10U) * 64;
                    position[2] = sign_extend(value >> 20U, 10U) * 64;
                    emit_vertex();
                    break;
                }
                case 0x25: {  // VTX_XY
                    const auto value = param(0U);
                    position[0] = sign_extend(value & 0xFFFFU, 16U);
                    position[1] = sign_extend((value >> 16U) & 0xFFFFU, 16U);
                    emit_vertex();
                    break;
                }
                case 0x26: {  // VTX_XZ
                    const auto value = param(0U);
                    position[0] = sign_extend(value & 0xFFFFU, 16U);
                    position[2] = sign_extend((value >> 16U) & 0xFFFFU, 16U);
                    emit_vertex();
                    break;
                }
                case 0x27: {  // VTX_YZ
                    const auto value = param(0U);
                    position[1] = sign_extend(value & 0xFFFFU, 16U);
                    position[2] = sign_extend((value >> 16U) & 0xFFFFU, 16U);
                    emit_vertex();
                    break;
                }
                case 0x28: {  // VTX_DIFF (10-bit signed 4.12 deltas)
                    const auto value = param(0U);
                    position[0] += sign_extend(value, 10U);
                    position[1] += sign_extend(value >> 10U, 10U);
                    position[2] += sign_extend(value >> 20U, 10U);
                    emit_vertex();
                    break;
                }
                case 0x40: {  // BEGIN_VTXS
                    flush_primitive_group(group, mesh.indices);
                    group = PrimitiveGroup{};
                    group.mode = static_cast<int>(param(0U) & 0x03U);
                    break;
                }
                case 0x41: {  // END_VTXS
                    flush_primitive_group(group, mesh.indices);
                    group = PrimitiveGroup{};
                    break;
                }
                default:
                    break;  // matrix/material/lighting commands: skip params
            }

            cursor += parameter_bytes;
        }
    }

    // A well-formed stream ends with END_VTXS, but flush any trailing group in
    // case the final command list omits it.
    flush_primitive_group(group, mesh.indices);

    return mesh;
}

}  // namespace

namespace khdays::assets {

NeutralModel decode_model_geometry(
    const std::filesystem::path& input_path,
    const std::size_t model_index) {
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

    const auto model_dictionary = parse_dictionary(mdl0, 8U, "model");
    if (model_dictionary.names.size() != model_dictionary.entries.size()) {
        throw std::runtime_error("model dictionary is inconsistent");
    }
    if (model_index >= model_dictionary.names.size()) {
        throw std::runtime_error(
            "model index " + std::to_string(model_index)
            + " is out of range (" + std::to_string(model_dictionary.names.size())
            + " models)");
    }

    const auto model_offset = static_cast<std::size_t>(
        read_entry_u32(model_dictionary.entries[model_index], "model"));
    if (model_offset > mdl0.size() || mdl0.size() - model_offset < 0x40U) {
        throw std::runtime_error("model header exceeds MDL0");
    }

    NeutralModel result;
    result.name = model_dictionary.names[model_index];
    result.header_vertex_count =
        read_u16(mdl0, model_offset + 0x24U, "vertex count");

    const auto meshes_relative = static_cast<std::size_t>(
        read_u32(mdl0, model_offset + 0x0CU, "meshes offset"));
    const auto model_scale =
        fixed_20_12(read_s32(mdl0, model_offset + 0x1CU, "up scale"));

    const auto mesh_list_offset = model_offset + meshes_relative;
    const auto mesh_dictionary =
        parse_dictionary(mdl0, mesh_list_offset, "mesh");
    if (mesh_dictionary.names.size() != mesh_dictionary.entries.size()) {
        throw std::runtime_error("mesh dictionary is inconsistent");
    }

    result.meshes.reserve(mesh_dictionary.names.size());

    for (std::size_t index = 0U; index < mesh_dictionary.names.size(); ++index) {
        const auto mesh_relative = static_cast<std::size_t>(
            read_entry_u32(mesh_dictionary.entries[index], "mesh"));
        const auto mesh_offset = mesh_list_offset + mesh_relative;

        if (mesh_offset > mdl0.size() || mdl0.size() - mesh_offset < 16U) {
            throw std::runtime_error("mesh header exceeds MDL0");
        }

        const auto command_relative = static_cast<std::size_t>(
            read_u32(mdl0, mesh_offset + 8U, "mesh command offset"));
        const auto command_length = static_cast<std::size_t>(
            read_u32(mdl0, mesh_offset + 12U, "mesh command length"));
        const auto command_offset = mesh_offset + command_relative;

        result.meshes.push_back(
            decode_mesh_geometry(
                mdl0,
                command_offset,
                command_length,
                mesh_dictionary.names[index],
                model_scale));
    }

    return result;
}

std::string to_wavefront_obj(const NeutralModel& model) {
    std::ostringstream stream;
    stream << "# khdays-port neutral mesh export\n";
    stream << "# model: " << model.name << '\n';

    std::uint32_t vertex_base = 1U;  // OBJ indices are 1-based

    for (const auto& mesh : model.meshes) {
        stream << "o " << mesh.name << '\n';

        for (const auto& vertex : mesh.vertices) {
            stream
                << "v " << vertex.position[0]
                << ' ' << vertex.position[1]
                << ' ' << vertex.position[2] << '\n';
        }
        for (const auto& vertex : mesh.vertices) {
            stream
                << "vt " << vertex.texcoord[0]
                << ' ' << vertex.texcoord[1] << '\n';
        }

        for (std::size_t i = 0U; i + 2U < mesh.indices.size(); i += 3U) {
            const auto a = vertex_base + mesh.indices[i];
            const auto b = vertex_base + mesh.indices[i + 1U];
            const auto c = vertex_base + mesh.indices[i + 2U];
            stream
                << "f " << a << '/' << a
                << ' ' << b << '/' << b
                << ' ' << c << '/' << c << '\n';
        }

        vertex_base += static_cast<std::uint32_t>(mesh.vertices.size());
    }

    return stream.str();
}

}  // namespace khdays::assets
